// fileio.h -- file names as UTF-8 on every system, and a file replaced safely (fsrobust, 2026-11-19). Header only,
// standard library and the C runtime (no <windows.h>: the few Win32 calls are declared below, as the system headers do).
//
// Windows: a narrow file name given to fopen / std::ifstream / std::ofstream / std::remove / std::rename, and a narrow
// argv or getenv(), is in the ANSI code page; std::filesystem::path takes a narrow string as UTF-8 (mingw libstdc++).
// So the port keeps every path as UTF-8 text or an fs::path, and opens through an fs::path (the streams then open wide)
// or b3io::fopen below (_wfopen). Elsewhere a narrow name is the bytes as they are, and all of this is a plain call.
//   u8path(s)          UTF-8 text -> fs::path. Never throws: text that is not UTF-8 is read as Latin-1.
//   u8string(p)        fs::path -> UTF-8 text
//   latin1_to_utf8(s)  bytes as Latin-1 (a disc image's names, any byte) -> UTF-8
//   fopen(p, mode)     fopen of an fs::path (_wfopen on Windows)
//   getenv(name)       an environment variable's value as UTF-8 (Windows: GetEnvironmentVariableW, the process's own
//                      wide block -- not the C runtime's copy, which a narrow main() builds from the ANSI one);
//                      nullopt when unset or empty
//   setenv(name, v)    sets it from UTF-8 (Windows: SetEnvironmentVariableW, children's too, and the C runtime's copy)
//   utf8_args(argc, argv)  Windows: argv replaced by the command line as UTF-8 (GetCommandLineW, CommandLineToArgvW);
//                      elsewhere nothing
//   replace_file(p, data[, durable])  writes `data` to p safely: p.tmp written and checked, then put in p's place in one
//                      step (rename; MoveFileExW REPLACE_EXISTING, retried a few times: a virus scanner or a sync
//                      client holding the file for a moment). `durable` (the default): flushed to the disk first
//                      (fsync, F_FULLFSYNC on macOS, FlushFileBuffers) and the replace written through; false for a
//                      file rewritten on every edit (controls.ini while the page is open: one durable write at its
//                      close). On any failure p is left as it was (the .tmp removed) and false (+ *error).
#ifndef B3PORT_FILEIO_H
#define B3PORT_FILEIO_H

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#ifndef _WINDOWS_   // <windows.h> not included: the kernel32 calls used here, as winbase.h / processenv.h declare them
extern "C" {
__declspec(dllimport) int __stdcall MoveFileExW(const wchar_t* from, const wchar_t* to, unsigned long flags);
__declspec(dllimport) unsigned long __stdcall GetLastError(void);
__declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableW(const wchar_t* name, wchar_t* buffer, unsigned long size);
__declspec(dllimport) int __stdcall SetEnvironmentVariableW(const wchar_t* name, const wchar_t* value);
__declspec(dllimport) wchar_t* __stdcall GetCommandLineW(void);
__declspec(dllimport) void* __stdcall LocalFree(void* mem);
__declspec(dllimport) void __stdcall Sleep(unsigned long ms);
}
#endif
#ifndef _INC_SHELLAPI   // shell32, as shellapi.h declares it
extern "C" __declspec(dllimport) wchar_t** __stdcall CommandLineToArgvW(const wchar_t* line, int* count);
#endif
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace b3io {
namespace fs = std::filesystem;

inline std::string latin1_to_utf8(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// True when `s` is well-formed UTF-8 (no overlong forms, no surrogates, at most U+10FFFF).
inline bool valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t n;
        unsigned cp;
        if (c < 0x80) { ++i; continue; }
        if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { n = 2; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 0x07; }
        else return false;
        if (s.size() - i <= n) return false;
        for (std::size_t k = 1; k <= n; ++k) {
            const unsigned char d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if ((n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))) return false;
        i += n + 1;
    }
    return true;
}

inline fs::path u8path(std::string_view s) {
    const std::string t = valid_utf8(s) ? std::string(s) : latin1_to_utf8(s);
    try {
        return fs::path(std::u8string(t.begin(), t.end()));
    } catch (...) {
        return {};
    }
}

inline std::string u8string(const fs::path& p) {
    try {
        const std::u8string u = p.u8string();
        return std::string(u.begin(), u.end());
    } catch (...) {   // (Windows: a name with an unpaired surrogate)
        return {};
    }
}

inline std::FILE* fopen(const fs::path& p, const char* mode) {
#ifdef _WIN32
    std::wstring m;
    for (const char* c = mode; *c; ++c) m.push_back(static_cast<wchar_t>(*c));
    return ::_wfopen(p.c_str(), m.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

namespace detail {
inline std::wstring ascii_wide(const char* s) {
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}
}  // namespace detail

inline std::optional<std::string> getenv(const char* name) {
#ifdef _WIN32
    const std::wstring w = detail::ascii_wide(name);
    std::wstring v;
    for (unsigned long size = 260;;) {
        v.assign(size, L'\0');
        const unsigned long n = ::GetEnvironmentVariableW(w.c_str(), v.data(), size);
        if (n == 0) return std::nullopt;   // unset (or empty)
        if (n < size) { v.resize(n); break; }
        size = n;   // the size it needs (with its terminator)
    }
    std::string s = u8string(fs::path(v));
    if (s.empty()) return std::nullopt;
    return s;
#else
    const char* v = std::getenv(name);
    if (!v || !*v) return std::nullopt;
    return std::string(v);
#endif
}

inline bool setenv(const char* name, const std::string& value) {
#ifdef _WIN32
    const std::wstring w = detail::ascii_wide(name);
    const fs::path v = u8path(value);
    const bool os = ::SetEnvironmentVariableW(w.c_str(), v.c_str()) != 0;   // the process's block (children's too)
    return ::_wputenv_s(w.c_str(), v.c_str()) == 0 && os;                   // and the C runtime's copies
#else
    return ::setenv(name, value.c_str(), 1) == 0;
#endif
}

// Windows: argc / argv as UTF-8 from the wide command line (kept for the program's life); when the system's split
// does not give the C runtime's count, the given ones stay. Elsewhere nothing (argv is the system's bytes).
inline void utf8_args(int& argc, char**& argv) {
#ifdef _WIN32
    static std::vector<std::string> args;
    static std::vector<char*> ptrs;
    int n = 0;
    wchar_t** w = ::CommandLineToArgvW(::GetCommandLineW(), &n);
    if (!w) return;
    if (n == argc) {
        args.clear();
        ptrs.clear();
        for (int k = 0; k < n; ++k) args.push_back(u8string(fs::path(w[k])));
        for (std::string& a : args) ptrs.push_back(a.data());
        ptrs.push_back(nullptr);
        argv = ptrs.data();
    }
    ::LocalFree(w);
#else
    (void)argc;
    (void)argv;
#endif
}

// replace_file's two steps, for a caller with work between them (save_store.cpp: the backup once the new save is safely
// on the disk). write_synced: `p` created / truncated, written, (durable: flushed to the disk) and checked (removed on a
// failure). commit_replace: `from` put in `to`'s place in one step (`from` removed on a failure; `to` as it was).
inline bool write_synced(const fs::path& p, std::string_view data, std::string* error = nullptr, bool durable = true) {
    std::FILE* f = b3io::fopen(p, "wb");
    if (!f) {
        if (error) *error = "cannot create " + u8string(p) + " (" + std::strerror(errno) + ")";
        return false;
    }
    int err = 0;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    if (!ok) err = errno;
    if (ok && std::fflush(f) != 0) ok = false, err = errno;
    if (ok && durable) {
#ifdef _WIN32
        if (::_commit(::_fileno(f)) != 0) ok = false, err = errno;   // FlushFileBuffers
#else
#ifdef F_FULLFSYNC
        if (::fcntl(::fileno(f), F_FULLFSYNC) != 0 && ::fsync(::fileno(f)) != 0) ok = false, err = errno;   // macOS: to the medium
#else
        if (::fsync(::fileno(f)) != 0) ok = false, err = errno;
#endif
#endif
    }
    if (std::fclose(f) != 0 && ok) ok = false, err = errno;
    if (!ok) {
        std::error_code ec;
        fs::remove(p, ec);
        if (error) *error = "cannot write " + u8string(p) + " (" + std::strerror(err ? err : EIO) + ")";
    }
    return ok;
}

inline bool commit_replace(const fs::path& from, const fs::path& to, std::string* error = nullptr, bool durable = true) {
    std::error_code ec;
#ifdef _WIN32
    const unsigned long flags = 0x1 /*MOVEFILE_REPLACE_EXISTING*/ | (durable ? 0x8 /*MOVEFILE_WRITE_THROUGH*/ : 0);
    unsigned long e = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {   // a scanner or a sync client holding `to` for a moment
        if (attempt) ::Sleep(75);
        if (::MoveFileExW(from.c_str(), to.c_str(), flags)) return true;
        e = ::GetLastError();
    }
    fs::remove(from, ec);
    if (error) *error = "cannot replace " + u8string(to) + " (Windows error " + std::to_string(e) + ")";
    return false;
#else
    if (std::rename(from.c_str(), to.c_str()) != 0) {
        const int e = errno;
        fs::remove(from, ec);
        if (error) *error = "cannot replace " + u8string(to) + " (" + std::strerror(e) + ")";
        return false;
    }
    if (durable && to.has_parent_path()) {   // the rename itself on the disk
        const int d = ::open(to.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (d >= 0) {
            ::fsync(d);
            ::close(d);
        }
    }
    return true;
#endif
}

inline fs::path tmp_beside(const fs::path& p) {
    fs::path t = p;
    t += ".tmp";
    return t;
}

inline bool replace_file(const fs::path& p, std::string_view data, std::string* error = nullptr, bool durable = true) {
    const fs::path tmp = tmp_beside(p);
    return write_synced(tmp, data, error, durable) && commit_replace(tmp, p, error, durable);
}

}  // namespace b3io

#endif
