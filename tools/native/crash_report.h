// crash_report.h -- native_play's crash report (play1): on a host fault, the native chain of the faulting thread (the
// guest functions it was in), the last foreign guest pointer formed there, and the host backtrace, then exit 4.
// The same report as native_boot's on_crash (boot2), as a header so native_play can install it on its game thread.
//   install_crash_report()        once per process: the handlers (POSIX signals / Windows' unhandled-exception filter)
//   install_crash_stack()         on each thread that should survive a stack overflow long enough to report (POSIX:
//                                 an alternate signal stack is per thread; Windows: SetThreadStackGuarantee)
#ifndef B3NATIVE_CRASH_REPORT_H
#define B3NATIVE_CRASH_REPORT_H

#include "b3host/call.h"
#include "b3host/native_backend.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <execinfo.h>
#include <unistd.h>
#endif

namespace b3native {

// Called last, right before the process ends (native_play: run_log.h's crash_flush, so the report reaches the run log)
inline void (*g_crash_flush)() = nullptr;

inline void crash_report(const char* what) {
    if (b3host::detail::t_any_foreign)
        std::fprintf(stdout, "crash: the last foreign guest pointer formed on this thread: %08X (outside RAM/scratchpad)\n",
                     b3host::detail::t_last_foreign);
    std::fprintf(stdout, "crash: %s; native chain depth %u:", what, b3host::native_chain::t_depth);
    for (unsigned k = 0; k < b3host::native_chain::t_depth && k < b3host::native_chain::kMax; ++k)
        std::fprintf(stdout, " %08X %s", b3host::native_chain::t_chain[k],
                     b3host::describe_function(b3host::native_chain::t_chain[k]).c_str());
    std::fprintf(stdout, "\n");
    std::fflush(stdout);
#ifdef _WIN32
    void* bt[62];
    const unsigned short n = CaptureStackBackTrace(0, 62, bt, nullptr);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    for (unsigned short k = 0; k < n; ++k)   // image offsets: addr2line -e native_play.exe 0x140000000+off
        std::fprintf(stdout, "  #%u +0x%llx\n", k, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(bt[k]) - base));
    std::fflush(stdout);
    if (g_crash_flush) g_crash_flush();
    _exit(4);
#else
    void* bt[64];
    const int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, 1);
    if (g_crash_flush) g_crash_flush();
    _exit(4);
#endif
}

#ifdef _WIN32
inline LONG WINAPI crash_seh(EXCEPTION_POINTERS* e) {
    char what[96];
    std::snprintf(what, sizeof what, "exception %08lX at %p", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
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
inline void crash_signal(int sig) {
    char what[32];
    std::snprintf(what, sizeof what, "signal %d", sig);
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
    sa.sa_handler = crash_signal;
    sa.sa_flags = SA_ONSTACK;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    install_crash_stack();
}
#endif

}  // namespace b3native

#endif
