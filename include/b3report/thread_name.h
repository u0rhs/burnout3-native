// thread_name.h -- split2p: the port's threads by name (a Windows crash report named only "a DLL thread" in a split-screen
// race: it was the particle worker). The name is kept by the OS for the thread (Windows 10 1607+ SetThreadDescription; Linux
// pthread_setname_np, 15 characters; macOS pthread_setname_np), so any module reads it back: native_play's crash report
// (tools/native/crash_report.h) prints the faulting thread's name, as debuggers and profilers show it. SDL names its own
// threads (SDLHIDAPI, SDLAudioP..., SDLTimer) the same way.
//   name_this_thread("render")   at the start of a thread the port creates
//   named_here()                 the name this module's name_this_thread gave the calling thread ("" if none): a
//                                thread_local buffer per module (native_play and b3render each have one), no
//                                allocation, safe in a crash handler (heap corruption)
//   this_thread_name()           the OS's name for it, "" when none; allocates (GetThreadDescription: LocalAlloc), so a
//                                crash handler asks it last, for threads the port did not name (SDL's, the driver's)
#ifndef B3REPORT_THREAD_NAME_H
#define B3REPORT_THREAD_NAME_H

#include <cstddef>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace b3port {

namespace detail {
inline thread_local char t_named[32] = {};   // split2p (review): the crash handler's allocation-free copy
}  // namespace detail

inline const char* named_here() { return detail::t_named; }

#ifdef _WIN32
namespace detail {
// resolved at run time: older Windows (and older mingw headers) have no thread descriptions
inline FARPROC kernel32_proc(const char* name) {
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    return k ? GetProcAddress(k, name) : nullptr;
}
}  // namespace detail
#endif

inline void name_this_thread(const char* name) {
    if (name) {
        std::size_t k = 0;
        for (; name[k] && k + 1 < sizeof detail::t_named; ++k) detail::t_named[k] = name[k];
        detail::t_named[k] = 0;
    }
#ifdef _WIN32
    using SetDesc = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto set = reinterpret_cast<SetDesc>(reinterpret_cast<void*>(detail::kernel32_proc("SetThreadDescription")));
    if (!set || !name) return;
    wchar_t w[64];
    std::size_t n = 0;
    for (; name[n] && n + 1 < sizeof w / sizeof w[0]; ++n) w[n] = wchar_t(static_cast<unsigned char>(name[n]));   // ASCII names
    w[n] = 0;
    set(GetCurrentThread(), w);
#elif defined(__APPLE__)
    if (name) pthread_setname_np(name);
#else
    if (!name) return;
    char b[16];
    std::size_t n = 0;
    for (; name[n] && n < 15; ++n) b[n] = name[n];
    b[n] = 0;
    pthread_setname_np(pthread_self(), b);
#endif
}

inline std::string this_thread_name() {
#ifdef _WIN32
    using GetDesc = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const auto get = reinterpret_cast<GetDesc>(reinterpret_cast<void*>(detail::kernel32_proc("GetThreadDescription")));
    if (!get) return {};
    PWSTR w = nullptr;
    std::string out;
    if (SUCCEEDED(get(GetCurrentThread(), &w)) && w) {
        for (const wchar_t* p = w; *p && out.size() < 63; ++p) out += *p < 0x80 ? char(*p) : '?';
        LocalFree(w);
    }
    return out;
#else
    char b[64] = {};
    if (pthread_getname_np(pthread_self(), b, sizeof b) != 0) return {};
    return b;
#endif
}

}  // namespace b3port

#endif
