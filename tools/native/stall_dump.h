// stall_dump.h -- every thread's stack when the game stops advancing (mac5, 2026-11-09; the first Windows race hung
// after the race start with no message). Windows: each other thread suspended, its context unwound with the x64 unwind
// tables (RtlLookupFunctionEntry / RtlVirtualUnwind), printed as image offsets for addr2line (as crash_report.h's).
// Elsewhere: only the stall line (gdb / lldb attach for the stacks).
#ifndef B3NATIVE_STALL_DUMP_H
#define B3NATIVE_STALL_DUMP_H

#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace b3native {

inline void stall_dump(const char* why) {
    std::fprintf(stdout, "stall: %s\n", why);
#ifdef _WIN32
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) { std::fflush(stdout); return; }
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (SuspendThread(t) != DWORD(-1)) {
            CONTEXT c{};
            c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(t, &c)) {
                std::fprintf(stdout, "stall: thread %lu:", te.th32ThreadID);
                for (int k = 0; k < 24 && c.Rip; ++k) {
                    const std::uintptr_t pc = std::uintptr_t(c.Rip);
                    HMODULE m = nullptr;
                    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCSTR>(pc), &m);
                    char name[MAX_PATH] = "?";
                    if (m) { GetModuleFileNameA(m, name, MAX_PATH); }
                    const char* leaf = name;
                    for (const char* p = name; *p; ++p) if (*p == '\\' || *p == '/') leaf = p + 1;
                    std::fprintf(stdout, " %s+0x%llx", leaf, static_cast<unsigned long long>(pc - (m ? reinterpret_cast<std::uintptr_t>(m) : base)));
                    DWORD64 image = 0;
                    PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
                    if (!f) {   // a leaf: its return address is on the stack top
                        c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);
                        c.Rsp += 8;
                        continue;
                    }
                    void* handler_data = nullptr;
                    DWORD64 frame = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, f, &c, &handler_data, &frame, nullptr);
                }
                std::fprintf(stdout, "\n");
            }
            ResumeThread(t);
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
#endif
    std::fflush(stdout);
}

}  // namespace b3native

#endif
