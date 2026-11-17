// run_log.h -- native_play's run log (test builds; include/b3report/run_logs.h names and rotates the files): the
// process's stdout and stderr -- native_play's own lines, the renderer's (b3render, loaded later: it gets the same
// handles) and a crash report -- also go to <pref>/logs/run-<date-time>.log.
//
// Per stream, by what it was pointed at when the run started:
//   a terminal / console, a file, a pipe (a developer's shell, a script's redirect): kept, and a copy goes to the log
//     (a pipe and a reader thread per stream: "tee"); the copy is written as it comes, unbuffered
//   nothing, or the null device (the launcher starts the game so, on Windows a console program without a console):
//     the stream is the log file itself (no thread)
// A crash (crash_report.h g_crash_flush) waits up to a second for the readers to empty their pipes before the process
// ends, so its report is in the log; a normal end (atexit) puts the streams back and joins the readers.
#ifndef B3NATIVE_RUN_LOG_H
#define B3NATIVE_RUN_LOG_H

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
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
#ifdef _WIN32
    HANDLE orig = nullptr;            // the original handle (a duplicate), nullptr: none
    HANDLE read = nullptr;
#else
    int orig = -1;
    int read = -1;
#endif
};

struct State {
    bool active = false;
    std::string path;
#ifdef _WIN32
    HANDLE log = INVALID_HANDLE_VALUE;
#else
    int log = -1;
#endif
    Stream s[2];   // stdout, stderr
};
inline State& state() {
    static State st;
    return st;
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
inline void reader(int i) {
    State& st = state();
    Stream& s = st.s[i];
    char buf[4096];
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(s.read, buf, sizeof buf, &n, nullptr) || n == 0) break;
        s.busy = true;
        write_all(s.orig, buf, n);
        write_all(st.log, buf, n);
        s.busy = false;
    }
    s.done = true;
}
inline bool pending(int i) {
    Stream& s = state().s[i];
    DWORD avail = 0;
    if (!PeekNamedPipe(s.read, nullptr, 0, nullptr, &avail, nullptr)) return s.busy;
    return avail > 0 || s.busy;
}
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
inline void reader(int i) {
    State& st = state();
    Stream& s = st.s[i];
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(s.read, buf, sizeof buf);
        if (n <= 0) break;
        s.busy = true;
        write_all(s.orig, buf, n);
        write_all(st.log, buf, n);
        s.busy = false;
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
// handler (an ioctl / PeekNamedPipe and a sleep per step).
inline void drain(int ms) {
    State& st = state();
    if (!st.active) return;
    std::fflush(stdout);
    std::fflush(stderr);
    for (int waited = 0; waited < ms; waited += 5) {
        bool any = false;
        for (int i = 0; i < 2; ++i)
            if (st.s[i].tee && !st.s[i].done && pending(i)) any = true;
        if (!any) return;
        sleep_ms(5);
    }
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

// Starts the run log at `path` (appended to: the launcher may have created it). false (and *error): no log this run,
// the streams untouched.
inline bool start(const std::filesystem::path& path, std::string* error = nullptr) {
    State& st = state();
    if (st.active) return true;
    std::fflush(stdout);
    std::fflush(stderr);
#ifdef _WIN32
    st.log = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (st.log == INVALID_HANDLE_VALUE) {
        if (error) *error = "cannot write " + path.string() + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        Stream& s = st.s[i];
        const HANDLE h = GetStdHandle(i == 0 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
        if (useful(h)) {
            HANDLE r = nullptr, w = nullptr;
            if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &s.orig, 0, FALSE, DUPLICATE_SAME_ACCESS) &&
                CreatePipe(&r, &w, nullptr, 1 << 16)) {
                s.read = r;
                s.tee = true;
                point(i + 1, w, _O_TEXT);
                CloseHandle(w);
                std::thread(reader, i).detach();
                continue;
            }
            if (s.orig) { CloseHandle(s.orig); s.orig = nullptr; }
        }
        point(i + 1, st.log, _O_TEXT);   // the stream is the log
    }
#else
    st.log = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (st.log < 0) {
        if (error) *error = "cannot write " + path.string();
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
    st.path = path.string();
    st.active = true;
    std::atexit(finish);
    return true;
}

}  // namespace b3native::runlog

#endif
