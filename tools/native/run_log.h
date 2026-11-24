// run_log.h -- native_play's run log (test builds; include/b3report/run_logs.h names and rotates the files): the
// process's stdout and stderr -- native_play's own lines, the renderer's (b3render, loaded later: it gets the same
// handles) and a crash report -- also go to <pref>/logs/run-<date-time>.log.
//
// Per stream, by what it was pointed at when the run started:
//   a terminal / console, a file, a pipe (a developer's shell, a script's redirect): kept, and a copy goes to the log
//     (a pipe and a reader thread per stream: "tee"); both get each line as soon as it is whole
//   nothing, or the null device (the launcher starts the game so, on Windows a console program without a console):
//     the stream is the log file itself (no thread; Windows: through a pipe and a reader too, for whole lines, below)
// A crash (crash_report.h g_crash_flush) waits up to a second for the readers to empty their pipes before the process
// ends, so its report is in the log; a normal end (atexit) puts the streams back and joins the readers.
//
// Windows (musicexit, 2026-11-23): whole lines. stdout (buffered: written out in 4 KB blocks that end mid-line) and
// stderr (unbuffered: written in pieces) reach the same file or pipe as two writers, and lines came out spliced into
// each other ("[b3native_play: VBlank 2: ..."; race_check then missed "VBlank limit" in a Windows run). So on Windows
// both streams always go through a pipe (with a log or without one: start_lines()), and each reader passes on only
// whole lines, one write each, under one lock for both streams. Elsewhere a line is one write already (glibc: a
// line-buffered stdout, an unbuffered stderr writes each call at once).
#ifndef B3NATIVE_RUN_LOG_H
#define B3NATIVE_RUN_LOG_H

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

namespace b3native::runlog {

struct Stream {
    bool tee = false;                 // a reader thread copies the pipe to the original and the log
    std::atomic<bool> busy{false};    // the reader holds bytes it has not written yet
    std::atomic<bool> done{false};    // the reader ended (the pipe's write end closed)
    std::atomic<unsigned long long> copied{0};   // bytes the reader has written on (musicexit: the drain's progress)
    std::string part;                 // what the reader has read and not passed on yet: a line's start (State::lines_m)
#ifdef _WIN32
    HANDLE orig = nullptr;            // the original handle (a duplicate), nullptr: none
    HANDLE read = nullptr;
#else
    int orig = -1;
    int read = -1;
#endif
};

struct State {
    std::atomic<bool> active{false};   // (read by the Windows flusher)
    std::string path;
#ifdef _WIN32
    HANDLE log = INVALID_HANDLE_VALUE;
#else
    int log = -1;
#endif
    Stream s[2];   // stdout, stderr
    std::mutex lines_m;   // the readers pass on whole lines one at a time, from either stream
};
inline State& state() {   // made once and never freed: the detached readers may outlive the statics at the end
    static State* st = new State;
    return *st;
}

// How much of `part` (a stream's bytes not passed on yet) to pass on now: up to its last line end; all of it at the end
// (`all`) or when one line runs past kLongLine. 0: wait for the line's end. (musicexit: whole lines, one write each)
inline constexpr std::size_t kLongLine = 1 << 16;
inline std::size_t line_cut(const std::string& part, bool all) {
    if (all || part.size() >= kLongLine) return part.size();
    const std::size_t cut = part.rfind('\n');
    return cut == std::string::npos ? 0 : cut + 1;
}

#ifdef _WIN32

inline void write_all(HANDLE h, const char* p, DWORD n) {
    while (h && h != INVALID_HANDLE_VALUE && n > 0) {
        DWORD w = 0;
        if (!WriteFile(h, p, n, &w, nullptr) || w == 0) return;
        p += w;
        n -= w;
    }
}
// A useful original: a console, a file or a pipe (not the NUL device, not none)
inline bool useful(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return false;
    const DWORD t = GetFileType(h);
    if (t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE) return true;
    DWORD mode = 0;
    return t == FILE_TYPE_CHAR && GetConsoleMode(h, &mode);
}
// Passes on `s`'s whole lines (line_cut), one write to each sink. With State::lines_m held.
inline void pass_lines(Stream& s, bool all) {
    const std::size_t n = line_cut(s.part, all);
    if (n == 0) return;
    write_all(s.orig, s.part.data(), DWORD(n));
    write_all(state().log, s.part.data(), DWORD(n));
    s.part.erase(0, n);
}
inline void reader(int i) {
    State& st = state();
    Stream& s = st.s[i];
    char buf[4096];
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(s.read, buf, sizeof buf, &n, nullptr) || n == 0) break;
        s.busy = true;
        {
            std::lock_guard<std::mutex> l(st.lines_m);
            s.part.append(buf, n);
            pass_lines(s, false);
        }
        s.copied += n;
        s.busy = false;
    }
    {
        std::lock_guard<std::mutex> l(st.lines_m);
        pass_lines(s, true);   // the end: a last line without its line end too
    }
    s.done = true;
}
// (no pending() here: PeekNamedPipe on the read end waits behind the reader's own ReadFile on that synchronous handle
// until more bytes come -- at the end of a run, never: drain() hung there, so `--log X` with stdout on a file or a pipe
// never exited (musicexit, 2026-11-23). drain() watches the readers' progress instead.)
inline void sleep_ms(int ms) { Sleep(DWORD(ms)); }
// Points C runtime descriptor `fd` (1 / 2) and the process's standard handle at `h` (the handle is duplicated)
inline void point(int fd, HANDLE h, int flags) {
    HANDLE d = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, 0, TRUE, DUPLICATE_SAME_ACCESS)) return;
    const int nfd = _open_osfhandle(reinterpret_cast<intptr_t>(d), flags);
    if (nfd < 0) { CloseHandle(d); return; }
    _dup2(nfd, fd);
    _close(nfd);
    SetStdHandle(fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(fd)));
}

#else

inline void write_all(int fd, const char* p, ssize_t n) {
    while (fd >= 0 && n > 0) {
        const ssize_t w = ::write(fd, p, std::size_t(n));
        if (w <= 0) return;
        p += w;
        n -= w;
    }
}
inline bool useful(int fd) {
    struct stat sb {};
    if (fstat(fd, &sb) != 0) return false;
    if (S_ISREG(sb.st_mode) || S_ISFIFO(sb.st_mode) || S_ISSOCK(sb.st_mode)) return true;
    return isatty(fd) != 0;   // a character device: a terminal yes, /dev/null no
}
inline void pass_lines(Stream& s, bool all) {   // as Windows' (whole lines; the two readers share the log)
    const std::size_t n = line_cut(s.part, all);
    if (n == 0) return;
    write_all(s.orig, s.part.data(), ssize_t(n));
    write_all(state().log, s.part.data(), ssize_t(n));
    s.part.erase(0, n);
}
inline void reader(int i) {
    State& st = state();
    Stream& s = st.s[i];
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(s.read, buf, sizeof buf);
        if (n <= 0) break;
        s.busy = true;
        {
            std::lock_guard<std::mutex> l(st.lines_m);
            s.part.append(buf, std::size_t(n));
            pass_lines(s, false);
        }
        s.copied += static_cast<unsigned long long>(n);
        s.busy = false;
    }
    {
        std::lock_guard<std::mutex> l(st.lines_m);
        pass_lines(s, true);
    }
    s.done = true;
}
inline bool pending(int i) {
    Stream& s = state().s[i];
    int avail = 0;
    if (ioctl(s.read, FIONREAD, &avail) != 0) return s.busy;
    return avail > 0 || s.busy;
}
inline void sleep_ms(int ms) {
    timespec t{0, long(ms) * 1000000L};
    nanosleep(&t, nullptr);
}

#endif

// Waits until the readers have written what is in their pipes (at most `ms`); async-signal-safe enough for a crash
// handler (POSIX: an ioctl and a sleep per step; Windows: the readers' counters and a sleep).
inline void drain(int ms) {
    State& st = state();
    if (!st.active) return;
    std::fflush(stdout);
    std::fflush(stderr);
#ifdef _WIN32
    // What was written is in the pipe; a reader wakes for it within a few ms. Done when no reader is busy and none has
    // copied anything for 50 ms (kQuietMs), or at `ms`.
    constexpr int kQuietMs = 50;
    auto progress = [&st](bool& busy) {
        unsigned long long n = 0;
        busy = false;
        for (int i = 0; i < 2; ++i) {
            if (!st.s[i].tee || st.s[i].done) continue;
            n += st.s[i].copied.load();
            busy = busy || st.s[i].busy;
        }
        return n;
    };
    bool busy = false;
    unsigned long long last = progress(busy);
    for (int waited = 0, quiet = 0; waited < ms && quiet < kQuietMs; waited += 5) {
        sleep_ms(5);
        const unsigned long long now = progress(busy);
        quiet = !busy && now == last ? quiet + 5 : 0;
        last = now;
    }
#else
    for (int waited = 0; waited < ms; waited += 5) {
        bool any = false;
        for (int i = 0; i < 2; ++i)
            if (st.s[i].tee && !st.s[i].done && pending(i)) any = true;
        if (!any) break;
        sleep_ms(5);
    }
#endif
    // a line still without its end (a crash in the middle of it): passed on as it is (try_lock: a crash on a reader)
    std::unique_lock<std::mutex> l(st.lines_m, std::try_to_lock);
    if (l.owns_lock())
        for (int i = 0; i < 2; ++i)
            if (st.s[i].tee) pass_lines(st.s[i], true);
}
inline void crash_flush() { drain(1000); }

// The normal end: the streams back to their originals (the pipes' write ends close, the readers see the end), the
// readers joined (at most 2 s), the log closed.
inline void finish() {
    State& st = state();
    if (!st.active) return;
    drain(2000);
    st.active = false;
#ifdef _WIN32
    for (int i = 0; i < 2; ++i) {
        Stream& s = st.s[i];
        if (!s.tee) continue;
        if (s.orig) point(i + 1, s.orig, _O_TEXT);
        else {
            HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (nul != INVALID_HANDLE_VALUE) { point(i + 1, nul, _O_TEXT); CloseHandle(nul); }
        }
    }
#else
    for (int i = 0; i < 2; ++i)
        if (st.s[i].tee && st.s[i].orig >= 0) dup2(st.s[i].orig, i + 1);
#endif
    for (int waited = 0; waited < 2000 && !((!st.s[0].tee || st.s[0].done) && (!st.s[1].tee || st.s[1].done)); waited += 5) sleep_ms(5);
    // (a reader still blocked -- a write end held elsewhere -- is left; the process is ending)
}

// Starts the run log at `path` (appended to: the launcher may have created it), or (no `path`, Windows) only the
// whole-line passing of stdout and stderr. false (and *error): none of it this run, the streams untouched.
inline bool start_impl(const std::filesystem::path* path, std::string* error) {
    State& st = state();
    if (st.active) return true;
    std::fflush(stdout);
    std::fflush(stderr);
#ifdef _WIN32
    if (path) {
        st.log = CreateFileW(path->c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (st.log == INVALID_HANDLE_VALUE) {
            if (error) *error = "cannot write " + path->string() + " (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
    }
    bool any = false;
    for (int i = 0; i < 2; ++i) {   // each stream through a pipe: to its original (if useful) and the log (if any)
        Stream& s = st.s[i];
        const HANDLE h = GetStdHandle(i == 0 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
        if (useful(h) && !DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &s.orig, 0, FALSE, DUPLICATE_SAME_ACCESS))
            s.orig = nullptr;
        if (!s.orig && st.log == INVALID_HANDLE_VALUE) continue;   // nowhere to pass lines to: left as it is
        HANDLE r = nullptr, w = nullptr;
        if (CreatePipe(&r, &w, nullptr, 1 << 16)) {
            s.read = r;
            s.tee = true;
            point(i + 1, w, _O_TEXT);
            CloseHandle(w);
            std::thread(reader, i).detach();
            any = true;
            continue;
        }
        if (s.orig) { CloseHandle(s.orig); s.orig = nullptr; }
        if (st.log != INVALID_HANDLE_VALUE) point(i + 1, st.log, _O_TEXT);   // (no pipe: the stream is the log)
    }
    if (!path && !any) return false;
#else
    if (!path) return false;   // (a line is one write here already)
    st.log = ::open(path->c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (st.log < 0) {
        if (error) *error = "cannot write " + path->string();
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        Stream& s = st.s[i];
        const int fd = i + 1;
        int p[2] = {-1, -1};
        if (useful(fd) && (s.orig = fcntl(fd, F_DUPFD_CLOEXEC, 3)) >= 0 && pipe(p) == 0) {
            fcntl(p[0], F_SETFD, FD_CLOEXEC);
            s.read = p[0];
            s.tee = true;
            dup2(p[1], fd);
            ::close(p[1]);
            std::thread(reader, i).detach();
            continue;
        }
        if (s.orig >= 0) { ::close(s.orig); s.orig = -1; }
        dup2(st.log, fd);   // the stream is the log
    }
#endif
    if (path) st.path = path->string();
    st.active = true;
#ifdef _WIN32
    // stdout is a pipe now, so the C runtime buffers it in 4 KB blocks: flushed every 50 ms, so a console or a script
    // sees lines as they come (an unbuffered stdout costs ~80 us a line on the printing thread: mingw's printf writes
    // it a character at a time)
    if (st.s[0].tee)
        std::thread([] {
            while (state().active) {
                Sleep(50);
                if (state().active) std::fflush(stdout);
            }
        }).detach();
#endif
    std::atexit(finish);
    return true;
}
inline bool start(const std::filesystem::path& path, std::string* error = nullptr) { return start_impl(&path, error); }
// A run log is being written (start(); not start_lines() alone)
inline bool logging() { return state().active && !state().path.empty(); }
// Windows: stdout and stderr passed on in whole lines without a run log (an offscreen run, a test's redirect); true when
// on. Elsewhere false: nothing to do.
inline bool start_lines() { return start_impl(nullptr, nullptr); }

}  // namespace b3native::runlog

#endif
