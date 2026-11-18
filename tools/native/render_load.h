// render_load.h -- native_play's load of the renderer library (dll1114): what the player is told when it does not load,
// and on Windows the load itself with its diagnosis.
//   render_load_message(error, lib, log)   the message box text for a Windows load error (portable: tested on Linux)
//   in_folder(path, folder)                 case-insensitive "path is inside folder" (either slash)
// Windows only:
//   module_dir_utf8()                       the folder of native_play.exe as UTF-8 (GetModuleFileNameW: argv[0] is in
//                                           the ANSI code page, so a user folder outside it came out as "?")
//   load_renderer(lib, &error)              LoadLibraryExW of the UTF-8 path with its imports from System32 only (they
//                                           are all system DLLs: a stray or proxy DLL in the game folder, version.dll
//                                           say, is never taken for one). A failure writes the details to stderr (the
//                                           run log): the error and its text, the files, the DLLs mapped during the
//                                           attempt, the exceptions raised on this thread during it, the non-Windows
//                                           modules in the process and the DLL files beside the library. Error 1114
//                                           (a DLL's start-up failed) is tried once more before it is reported. No
//                                           system error dialog during the load (SetThreadErrorMode).
#ifndef B3NATIVE_RENDER_LOAD_H
#define B3NATIVE_RENDER_LOAD_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace b3native::render_load {

inline bool in_folder(std::string_view path, std::string_view folder) {
    auto low = [](char c) { return c == '/' ? '\\' : (c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c); };
    while (!folder.empty() && (folder.back() == '\\' || folder.back() == '/')) folder.remove_suffix(1);
    if (folder.empty() || path.size() <= folder.size()) return false;
    for (std::size_t i = 0; i < folder.size(); ++i)
        if (low(path[i]) != low(folder[i])) return false;
    return low(path[folder.size()]) == '\\';
}

inline std::string render_load_message(unsigned long error, const std::string& lib, const std::string& log) {
    const std::string code = " (error " + std::to_string(error) + ")";
    std::string t;
    switch (error) {
    case 2: case 3: case 126:   // not found (the library or a DLL it needs)
        t = "The renderer " + lib + " was not found" + code + ". Keep native_play.exe, b3render.dll and SDL3.dll together "
            "in one folder, extracted from the zip (not run from inside it).";
        break;
    case 1114:   // ERROR_DLL_INIT_FAILED
        t = "The renderer " + lib + " was found, but Windows could not start it" + code + ": the start-up of a DLL "
            "loaded with it failed. This is usually another program that puts its own DLL into games (an overlay, a "
            "recorder, a performance monitor or a security tool) or an extra .dll file in the game folder. Close such "
            "programs or exclude native_play.exe from them, remove DLL files you added to the game folder, and try again.";
        break;
    case 193: case 216:   // ERROR_BAD_EXE_FORMAT, ERROR_EXE_MACHINE_TYPE_MISMATCH
        t = "The renderer " + lib + " is not a 64-bit Windows library" + code + ". Download the game again and extract "
            "the whole zip.";
        break;
    case 5: case 225: case 577: case 1260: case 4551:   // denied, virus, bad hash, policy, application control
        t = "Windows or a security program blocked the renderer " + lib + code + ". Allow native_play.exe and "
            "b3render.dll in it, or extract the game into another folder, and try again.";
        break;
    default:
        t = "The renderer " + lib + " did not load" + code + ". Keep native_play.exe, b3render.dll and SDL3.dll "
            "together in one folder.";
        break;
    }
    if (!log.empty()) t += "\n\nThe details are in the run log: " + log + " (attach it to a bug report).";
    return t;
}

}  // namespace b3native::render_load

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>

namespace b3native::render_load {

inline std::string utf8(const wchar_t* w, int n = -1) {
    const int k = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    if (k <= 0) return {};
    std::string s(std::size_t(k), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, n, s.data(), k, nullptr, nullptr);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
inline std::wstring wide(const std::string& s) {
    const int k = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (k <= 0) return {};
    std::wstring w(std::size_t(k), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), k);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

inline std::string module_dir_utf8() {
    std::wstring p(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, p.data(), DWORD(p.size()));
    if (n == 0 || n >= p.size()) return {};
    p.resize(n);
    const std::size_t cut = p.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::string() : utf8(p.c_str(), int(cut));
}

namespace detail {
// What happens during one load attempt (written under the loader lock: fixed storage, no allocation)
struct Mapped { wchar_t path[300]; bool loaded; std::uintptr_t base, size; };
struct Raised { DWORD code; void* at; ULONG_PTR info[2]; DWORD params; };
struct Attempt {
    DWORD thread = 0;
    Mapped mapped[48];
    unsigned n_mapped = 0, more_mapped = 0;
    Raised raised[16];
    unsigned n_raised = 0, more_raised = 0;
};
inline Attempt* g_attempt = nullptr;

struct UStr { USHORT len, max; PWSTR buf; };
struct DllNote { ULONG flags; const UStr* full; const UStr* base; void* dll_base; ULONG size; };
using NoteFn = VOID(CALLBACK*)(ULONG reason, const DllNote* data, PVOID ctx);
using RegisterFn = LONG(NTAPI*)(ULONG, NoteFn, PVOID, PVOID*);
using UnregisterFn = LONG(NTAPI*)(PVOID);

inline VOID CALLBACK on_dll(ULONG reason, const DllNote* d, PVOID) {
    Attempt* a = g_attempt;
    if (!a || !d || !d->full || !d->full->buf) return;
    if (a->n_mapped >= sizeof a->mapped / sizeof a->mapped[0]) { ++a->more_mapped; return; }
    Mapped& m = a->mapped[a->n_mapped++];
    const std::size_t n = std::min<std::size_t>(d->full->len / sizeof(wchar_t), sizeof m.path / sizeof m.path[0] - 1);
    std::wmemcpy(m.path, d->full->buf, n);
    m.path[n] = L'\0';
    m.loaded = reason == 1;   // LDR_DLL_NOTIFICATION_REASON_LOADED (2: unloaded, as a failed load is undone)
    m.base = reinterpret_cast<std::uintptr_t>(d->dll_base);
    m.size = d->size;
}
inline LONG CALLBACK on_exception(EXCEPTION_POINTERS* e) {
    Attempt* a = g_attempt;
    if (!a || GetCurrentThreadId() != a->thread || !e || !e->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    if (a->n_raised >= sizeof a->raised / sizeof a->raised[0]) { ++a->more_raised; return EXCEPTION_CONTINUE_SEARCH; }
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    Raised& x = a->raised[a->n_raised++];
    x.code = r->ExceptionCode;
    x.at = r->ExceptionAddress;
    x.params = r->NumberParameters;
    x.info[0] = r->NumberParameters > 0 ? r->ExceptionInformation[0] : 0;
    x.info[1] = r->NumberParameters > 1 ? r->ExceptionInformation[1] : 0;
    return EXCEPTION_CONTINUE_SEARCH;   // only watched: whoever handles it still does
}

inline std::string windows_dir() {
    wchar_t w[MAX_PATH];
    const UINT n = GetSystemWindowsDirectoryW(w, MAX_PATH);
    return n > 0 && n < MAX_PATH ? utf8(w, int(n)) : std::string("C:\\Windows");
}
inline std::string error_text(DWORD e) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, e, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::string s = n && buf ? utf8(buf, int(n)) : std::string();
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s.empty() ? "?" : s;
}
inline std::string file_line(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa))
        return utf8(p.c_str()) + ": missing (error " + std::to_string(GetLastError()) + ")";
    const unsigned long long size = (static_cast<unsigned long long>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
    return utf8(p.c_str()) + ": " + std::to_string(size) + " bytes";
}
inline std::string where(const Attempt& a, void* at) {   // module + offset of an address (after the load)
    const auto p = reinterpret_cast<std::uintptr_t>(at);
    for (unsigned i = 0; i < a.n_mapped; ++i)   // a DLL of the attempt (unmapped again by now when the load failed)
        if (a.mapped[i].loaded && p >= a.mapped[i].base && p - a.mapped[i].base < a.mapped[i].size) {
            char off[32];
            std::snprintf(off, sizeof off, "+0x%llx", static_cast<unsigned long long>(p - a.mapped[i].base));
            return utf8(a.mapped[i].path) + off;
        }
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(at), &m) || !m)
        return "no module";
    wchar_t name[MAX_PATH];
    const DWORD n = GetModuleFileNameW(m, name, MAX_PATH);
    char off[32];
    std::snprintf(off, sizeof off, "+0x%llx", static_cast<unsigned long long>(static_cast<char*>(at) - reinterpret_cast<char*>(m)));
    return (n ? utf8(name, int(n)) : std::string("?")) + off;
}

// The details of a failed attempt, one "native_play: renderer load: ..." line each, to stderr (the run log)
inline void report(const Attempt& a, const std::wstring& path, DWORD flags, DWORD e) {
    auto line = [](const std::string& s) { std::fprintf(stderr, "native_play: renderer load: %s\n", s.c_str()); };
    const std::string win = windows_dir();
    char f[16];
    std::snprintf(f, sizeof f, "0x%lx", static_cast<unsigned long>(flags));
    line("LoadLibraryExW(" + utf8(path.c_str()) + ", " + f + ") failed: error " + std::to_string(e) + " (" + error_text(e) + ")");
    const std::size_t cut = path.find_last_of(L"\\/");
    const std::wstring dir = cut == std::wstring::npos ? std::wstring(L".") : path.substr(0, cut);
    line("file " + file_line(path));
    line("file " + file_line(dir + L"\\SDL3.dll"));
    for (unsigned i = 0; i < a.n_mapped; ++i) {
        const std::string p = utf8(a.mapped[i].path);
        line(std::string(a.mapped[i].loaded ? "mapped " : "unmapped ") + p + (in_folder(p, win) ? "" : "   <- not from Windows"));
    }
    if (a.more_mapped) line("(" + std::to_string(a.more_mapped) + " more DLL notifications)");
    if (a.n_mapped == 0) line("no DLL was mapped during the attempt");
    for (unsigned i = 0; i < a.n_raised; ++i) {
        const Raised& x = a.raised[i];
        char head[160];
        std::snprintf(head, sizeof head, "exception %08lX at %p", static_cast<unsigned long>(x.code), x.at);
        std::string s = std::string(head) + " (" + where(a, x.at) + ")";
        if (x.code == EXCEPTION_ACCESS_VIOLATION && x.params >= 2) {
            char av[64];
            std::snprintf(av, sizeof av, ", %s %p", x.info[0] == 0 ? "reading" : x.info[0] == 8 ? "executing" : "writing",
                          reinterpret_cast<void*>(x.info[1]));
            s += av;
        }
        line(s);
    }
    if (a.more_raised) line("(" + std::to_string(a.more_raised) + " more exceptions)");
    if (a.n_raised == 0) line("no exception was raised on this thread during the attempt");
    // the modules in the process that are not Windows' own (an overlay, a hook, a security tool shows here)
    if (HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId()); s != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{};
        me.dwSize = sizeof me;
        unsigned n = 0;
        for (BOOL ok = Module32FirstW(s, &me); ok && n < 64; ok = Module32NextW(s, &me)) {
            const std::string p = utf8(me.szExePath);
            if (!in_folder(p, win)) { line("module in the process: " + p); ++n; }
        }
        CloseHandle(s);
    }
    // the DLL files beside the library (the release has b3render.dll and SDL3.dll only)
    WIN32_FIND_DATAW fd{};
    if (HANDLE h = FindFirstFileW((dir + L"\\*.dll").c_str(), &fd); h != INVALID_HANDLE_VALUE) {
        std::string names;
        do { names += (names.empty() ? "" : ", ") + utf8(fd.cFileName); } while (FindNextFileW(h, &fd));
        FindClose(h);
        line("DLL files in " + utf8(dir.c_str()) + ": " + names);
    }
    std::fflush(stderr);
}
}  // namespace detail

inline HMODULE load_renderer(const std::string& lib, unsigned long* error) {
    std::wstring path = wide(lib);
    if (std::wstring full(32768, L'\0'); !path.empty()) {   // an absolute path with backslashes (the search flags need it)
        const DWORD n = GetFullPathNameW(path.c_str(), DWORD(full.size()), full.data(), nullptr);
        if (n > 0 && n < full.size()) path.assign(full.c_str(), n);
    }
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto reg = ntdll ? reinterpret_cast<detail::RegisterFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrRegisterDllNotification"))) : nullptr;
    auto unreg = ntdll ? reinterpret_cast<detail::UnregisterFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrUnregisterDllNotification"))) : nullptr;
    // b3render.dll imports system DLLs only: they come from System32 (LOAD_LIBRARY_SEARCH_SYSTEM32), never from the game
    // folder. A Windows without the search flags (87) gets the earlier form.
    DWORD flags = LOAD_LIBRARY_SEARCH_SYSTEM32;
    // no system dialog ("Entry Point Not Found", "Bad Image") during the load: the player gets the box that says what
    // the error means (render_load_message), and an offscreen run never shows one
    DWORD old_mode = 0;
    const BOOL mode_set = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX, &old_mode);
    HMODULE m = nullptr;
    DWORD e = 0;
    for (int attempt = 0; attempt < 2 && !m; ++attempt) {
        static detail::Attempt a;
        a = detail::Attempt{};
        a.thread = GetCurrentThreadId();
        detail::g_attempt = &a;
        PVOID cookie = nullptr;
        if (reg && reg(0, detail::on_dll, nullptr, &cookie) != 0) cookie = nullptr;
        PVOID veh = AddVectoredExceptionHandler(1, detail::on_exception);
        m = path.empty() ? nullptr : LoadLibraryExW(path.c_str(), nullptr, flags);
        e = m ? 0 : (path.empty() ? ERROR_INVALID_NAME : GetLastError());
        if (!m && e == ERROR_INVALID_PARAMETER && flags != LOAD_WITH_ALTERED_SEARCH_PATH) {
            flags = LOAD_WITH_ALTERED_SEARCH_PATH;
            m = LoadLibraryExW(path.c_str(), nullptr, flags);
            e = m ? 0 : GetLastError();
        }
        if (veh) RemoveVectoredExceptionHandler(veh);
        if (cookie && unreg) unreg(cookie);
        detail::g_attempt = nullptr;
        if (m) {
            if (attempt) std::fprintf(stderr, "native_play: renderer load: the second attempt loaded it\n");
            break;
        }
        detail::report(a, path, flags, e);
        if (e != ERROR_DLL_INIT_FAILED) break;
        if (attempt == 0) {
            std::fprintf(stderr, "native_play: renderer load: error 1114, trying once more\n");
            Sleep(250);
        }
    }
    if (mode_set) SetThreadErrorMode(old_mode, nullptr);
    if (error) *error = e;
    return m;
}

}  // namespace b3native::render_load
#endif  // _WIN32
#endif  // B3NATIVE_RENDER_LOAD_H
