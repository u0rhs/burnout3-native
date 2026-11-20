// crash_report.h -- native_play's crash report (play1): on a host fault, the native chain of the faulting thread (the
// guest functions it was in), the last foreign guest pointer formed there, and the host backtrace, then exit 4
// (hostfix: first a "guest memory overrun" line when the fault is in the guards around guest RAM / the scratchpad).
// The same report as native_boot's on_crash (boot2), as a header so native_play can install it on its game thread.
//   install_crash_report()        once per process: the handlers (POSIX signals / Windows' unhandled-exception filter)
//   install_crash_stack()         on each thread that should survive a stack overflow long enough to report (POSIX:
//                                 an alternate signal stack is per thread; Windows: SetThreadStackGuarantee)
#ifndef B3NATIVE_CRASH_REPORT_H
#define B3NATIVE_CRASH_REPORT_H

#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "b3report/thread_name.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <execinfo.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace b3native {

#ifdef _WIN32
// split2p (a report whose frames were all offsets from native_play.exe, the fault in a DLL): the module an address is in
// (its file name) and the offset from that module's base; "?" and the address itself when it is in none
inline std::uintptr_t crash_module_of(const void* addr, char* name, std::size_t cap) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(addr), &m) || !m) {
        std::snprintf(name, cap, "?");
        return reinterpret_cast<std::uintptr_t>(addr);
    }
    wchar_t w[MAX_PATH];
    const DWORD len = GetModuleFileNameW(m, w, MAX_PATH);
    DWORD from = 0;
    for (DWORD i = 0; i < len; ++i) if (w[i] == L'\\' || w[i] == L'/') from = i + 1;
    std::size_t o = 0;
    for (DWORD i = from; i < len && o + 1 < cap; ++i) name[o++] = w[i] < 0x80 ? char(w[i]) : '?';
    name[o] = 0;
    if (o == 0) std::snprintf(name, cap, "?");
    return reinterpret_cast<std::uintptr_t>(addr) - reinterpret_cast<std::uintptr_t>(m);
}
#endif

// Called last, right before the process ends (native_play: run_log.h's crash_flush, so the report reaches the run log)
inline void (*g_crash_flush)() = nullptr;

// hostfix: a fault inside the no-access guards around guest RAM / the scratchpad (b3host::GuardedRegion) is host code
// that ran past a window: say so first, with how far
inline void crash_overrun_line(const void* fault_address) {
    char line[200];
    if (b3host::guard_overrun(fault_address, line, sizeof line)) {
        std::fprintf(stdout, "crash: %s\n", line);
        std::fflush(stdout);
    }
}

inline void crash_report(const char* what) {
    if (b3host::detail::t_any_foreign)
        std::fprintf(stdout, "crash: the last foreign guest pointer formed on this thread: %08X (outside RAM/scratchpad)\n",
                     b3host::detail::t_last_foreign);
    // split2p: the faulting thread by name and id. First the name the port gave it (b3report/thread_name.h: "game", "render",
    // "fx particles", ...; a thread_local copy, no allocation: the heap may be what broke); a thread the port did not name
    // gets the OS's name (SDL's "SDLHIDAPI", "SDLAudioP1", ...; that call allocates) after the frames are written.
    const char* named = b3port::named_here();
#ifdef _WIN32
    const unsigned long long tid = GetCurrentThreadId();
#elif defined(__linux__)
    const unsigned long long tid = static_cast<unsigned long long>(syscall(SYS_gettid));
#elif defined(__APPLE__)
    unsigned long long tid = 0;
    {
        std::uint64_t t = 0;
        if (pthread_threadid_np(nullptr, &t) == 0) tid = t;
    }
#else
    const unsigned long long tid = 0;
#endif
    // (the copy is per module: native_play's own threads here, "game" and "guest N"; the renderer's, "render" and "fx
    // particles" in b3render, by the OS's name below)
    if (*named) std::fprintf(stdout, "crash: thread \"%s\" (id %llu)\n", named, tid);
    else std::fprintf(stdout, "crash: thread id %llu (its name after the frames)\n", tid);
    std::fprintf(stdout, "crash: %s; native chain depth %u:", what, b3host::native_chain::t_depth);
    for (unsigned k = 0; k < b3host::native_chain::t_depth && k < b3host::native_chain::kMax; ++k)
        std::fprintf(stdout, " %08X %s", b3host::native_chain::t_chain[k],
                     b3host::describe_function(b3host::native_chain::t_chain[k]).c_str());
    std::fprintf(stdout, "\n");
    std::fflush(stdout);
#ifdef _WIN32
    void* bt[62];
    const unsigned short n = CaptureStackBackTrace(0, 62, bt, nullptr);
    for (unsigned short k = 0; k < n; ++k) {   // module + offset (addr2line -e <module> <its image base>+off: an unstripped build)
        char mod[MAX_PATH];
        const std::uintptr_t off = crash_module_of(bt[k], mod, sizeof mod);
        std::fprintf(stdout, "  #%u %s+0x%llx\n", k, mod, static_cast<unsigned long long>(off));
    }
    std::fflush(stdout);
    if (!*named) {   // after the frames: this allocates
        std::fflush(stdout);
        const std::string os_name = b3port::this_thread_name();
        std::fprintf(stdout, "crash: thread %llu's own name: \"%s\"\n", tid, os_name.empty() ? "none" : os_name.c_str());
    }
    if (g_crash_flush) g_crash_flush();
    _exit(4);
#else
    void* bt[64];
    const int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, 1);
    if (!*named) {   // after the frames: this allocates
        std::fflush(stdout);
        const std::string os_name = b3port::this_thread_name();
        std::fprintf(stdout, "crash: thread %llu's own name: \"%s\"\n", tid, os_name.empty() ? "none" : os_name.c_str());
    }
    if (g_crash_flush) g_crash_flush();
    _exit(4);
#endif
}

#ifdef _WIN32
inline LONG WINAPI crash_seh(EXCEPTION_POINTERS* e) {
    char what[MAX_PATH + 128], mod[MAX_PATH];
    const std::uintptr_t off = crash_module_of(e->ExceptionRecord->ExceptionAddress, mod, sizeof mod);
    std::snprintf(what, sizeof what, "exception %08lX at %p (%s+0x%llx)", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress,
                  mod, static_cast<unsigned long long>(off));
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2) {
        const std::size_t used = std::strlen(what);
        std::snprintf(what + used, sizeof what - used, ", %s %p", e->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" :
                      e->ExceptionRecord->ExceptionInformation[0] == 8 ? "executing" : "reading",
                      reinterpret_cast<void*>(e->ExceptionRecord->ExceptionInformation[1]));
        crash_overrun_line(reinterpret_cast<const void*>(e->ExceptionRecord->ExceptionInformation[1]));
    }
    crash_report(what);
    return EXCEPTION_EXECUTE_HANDLER;
}
inline void install_crash_stack() {
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
}
inline void install_crash_report() {
    SetUnhandledExceptionFilter(crash_seh);
    install_crash_stack();
}
#else
inline void crash_signal(int sig, siginfo_t* info, void*) {
    char what[32];
    std::snprintf(what, sizeof what, "signal %d", sig);
    if ((sig == SIGSEGV || sig == SIGBUS) && info) crash_overrun_line(info->si_addr);
    crash_report(what);
}
inline void install_crash_stack() {
    thread_local std::vector<char> alt(1 << 20);
    stack_t ss{};
    ss.ss_sp = alt.data();
    ss.ss_size = alt.size();
    sigaltstack(&ss, nullptr);
}
inline void install_crash_report() {
    struct sigaction sa {};
    sa.sa_sigaction = crash_signal;
    sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    install_crash_stack();
}
#endif

}  // namespace b3native

#endif
