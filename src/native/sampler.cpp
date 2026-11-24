// sampler.cpp -- slowcpu (issue #15): see sampler.h.
#include "sampler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "b3host/native_backend.h"   // native_chain, describe_function

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#if !defined(__APPLE__)
#include <ucontext.h>   // the SIGPROF handler reads the PC (Linux only; macOS's SDK refuses <ucontext.h> without _XOPEN_SOURCE)
#endif
#endif

namespace b3native::sampler {
namespace {

using b3host::gaddr;
constexpr unsigned kChain = 24;     // the innermost entries of a chain kept per sample
constexpr unsigned kThreads = 64;   // the host threads that run the game (the game thread and the guest threads)

struct Reg {
    const void* const* ctx_slot = nullptr;   // the thread's platform_threads t_ctx (null in it: the main context)
    const gaddr* chain = nullptr;            // its native_chain::t_chain / t_depth
    const unsigned* depth = nullptr;
    bool game = false;
#if defined(_WIN32)
    HANDLE handle = nullptr;
#elif !defined(__APPLE__)
    pthread_t thread{};
#endif
};
// (Fable) held for a whole sample and by register / unregister: a thread that ends takes itself out first, so no sample
// reads its thread-local slots after they are gone
Reg g_reg[kThreads];
unsigned g_nreg = 0;
std::mutex g_reg_mu;
unsigned g_overflow = 0;   // threads not registered: kThreads were registered at once

bool g_on = false;
std::string g_path;
std::uint64_t g_from = 0;
unsigned g_us = 1000;
std::atomic<bool> g_active{false}, g_idle{false}, g_stop{false};
std::thread g_timer;

// one sample in flight (the timer waits for it before the next)
struct Snap {
    std::uintptr_t pc = 0;
    unsigned depth = 0;
    gaddr chain[kChain]{};
};
Snap g_snap;
std::atomic<unsigned> g_seq{0};

// the aggregate (the timer thread only)
std::uint64_t g_total = 0, g_running = 0, g_idle_n = 0, g_missed = 0, g_guest = 0;
std::unordered_map<std::uintptr_t, std::uint32_t> g_leaf;
std::map<gaddr, std::uint32_t> g_top, g_incl;
std::chrono::steady_clock::time_point g_t0;

void take(const Snap& s, bool guest) {
    ++g_running;
    if (guest) ++g_guest;
    if (s.pc) ++g_leaf[s.pc];
    const unsigned n = std::min(s.depth, kChain);
    ++g_top[n ? s.chain[n - 1] : 0];
    gaddr seen[kChain];
    unsigned ns = 0;
    for (unsigned k = 0; k < n; ++k) {
        bool dup = false;
        for (unsigned j = 0; j < ns && !dup; ++j) dup = seen[j] == s.chain[k];
        if (!dup) { seen[ns++] = s.chain[k]; ++g_incl[s.chain[k]]; }
    }
}

// the chain's innermost kChain entries (the target thread's own, or read through its published pointers)
void copy_chain(Snap& s, const gaddr* chain, unsigned depth) {
    const unsigned d = std::min(depth, b3host::native_chain::kMax);
    const unsigned n = std::min(d, kChain);
    s.depth = n;
    for (unsigned k = 0; k < n; ++k) s.chain[k] = chain[d - n + k];
}

#if !defined(_WIN32) && !defined(__APPLE__)
void on_signal(int, siginfo_t*, void* uc) {   // on the sampled thread: its own context and chain
    const ucontext_t* u = static_cast<const ucontext_t*>(uc);
#if defined(__x86_64__)
    g_snap.pc = std::uintptr_t(u->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
    g_snap.pc = std::uintptr_t(u->uc_mcontext.pc);
#else
    (void)u;
    g_snap.pc = 0;
#endif
    copy_chain(g_snap, b3host::native_chain::t_chain, b3host::native_chain::t_depth);
    g_seq.fetch_add(1, std::memory_order_release);
}
#endif

// the registered thread that holds the baton (null: none of them, e.g. a host thread the sampler does not know)
const Reg* running() {
    const void* baton = b3native::platform::host_baton();
    const void* main = b3native::platform::host_ctx_main();
    for (unsigned i = 0; i < g_nreg; ++i) {
        const void* c = *g_reg[i].ctx_slot;
        if ((c ? c : main) == baton) return &g_reg[i];
    }
    return nullptr;
}

void sample_once() {
    std::lock_guard<std::mutex> l(g_reg_mu);
    const Reg* r = running();
    ++g_total;
    if (!r) { ++g_missed; return; }
    if (r->game && g_idle.load(std::memory_order_relaxed)) { ++g_idle_n; return; }
    Snap s;
#if defined(_WIN32)
    if (SuspendThread(r->handle) == DWORD(-1)) { ++g_missed; return; }
    CONTEXT c{};
    c.ContextFlags = CONTEXT_CONTROL;
    if (GetThreadContext(r->handle, &c)) {
#if defined(_M_ARM64) || defined(__aarch64__)
        s.pc = std::uintptr_t(c.Pc);
#else
        s.pc = std::uintptr_t(c.Rip);
#endif
    }
    copy_chain(s, r->chain, *r->depth);
    ResumeThread(r->handle);   // (nothing allocated while it was suspended: it may hold the heap's lock)
#elif defined(__APPLE__)
    copy_chain(s, r->chain, *r->depth);   // (no instruction pointer on macOS: the chain only)
#else
    const unsigned before = g_seq.load(std::memory_order_acquire);
    if (pthread_kill(r->thread, SIGPROF) != 0) { ++g_missed; return; }
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    while (g_seq.load(std::memory_order_acquire) == before) {
        if (std::chrono::steady_clock::now() > until) { ++g_missed; return; }
        std::this_thread::yield();
    }
    s = g_snap;
#endif
    take(s, !r->game);
}

std::string module_of(std::uintptr_t pc, std::uintptr_t* base) {
#if defined(_WIN32)
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(pc), &m) && m) {
        GetModuleFileNameA(m, name, MAX_PATH);
        *base = reinterpret_cast<std::uintptr_t>(m);
        return name;
    }
#else
    Dl_info i{};
    if (dladdr(reinterpret_cast<void*>(pc), &i) && i.dli_fname) {
        *base = reinterpret_cast<std::uintptr_t>(i.dli_fbase);
        return i.dli_fname;
    }
#endif
    *base = 0;
    return "?";
}

void write_file() {
    std::FILE* f = std::fopen(g_path.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "sampler: cannot write %s\n", g_path.c_str());
        return;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
    std::fprintf(f, "# b3sample 1 interval_us=%u from_vblank=%llu seconds=%.1f\n", g_us, static_cast<unsigned long long>(g_from), secs);
    std::fprintf(f, "# samples %llu: running %llu (on guest threads %llu), idle (the pacing wait) %llu, missed %llu\n",
                 static_cast<unsigned long long>(g_total), static_cast<unsigned long long>(g_running),
                 static_cast<unsigned long long>(g_guest), static_cast<unsigned long long>(g_idle_n),
                 static_cast<unsigned long long>(g_missed));
    if (g_overflow) std::fprintf(f, "# %u threads not sampled (more than %u at once)\n", g_overflow, kThreads);
    const double run = g_running ? double(g_running) : 1.0;
    auto table = [&](const char* title, const std::map<gaddr, std::uint32_t>& m) {
        std::vector<std::pair<std::uint32_t, gaddr>> v;
        for (const auto& kv : m) v.push_back({kv.second, kv.first});
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::fprintf(f, "## %s (%% of the running samples)\n", title);
        for (std::size_t i = 0; i < v.size() && i < 60; ++i)
            std::fprintf(f, "%6.2f %8u %08X %s\n", 100.0 * v[i].first / run, v[i].first, v[i].second,
                         v[i].second ? b3host::describe_function(v[i].second).c_str() : "(no guest function: the platform, the IOP, the sound)");
    };
    table("guest function, innermost entered through the dispatcher (self)", g_top);
    table("guest function, anywhere in the chain (inclusive)", g_incl);
    // the leaves by module: "<module>\t<offset hex>\t<count>" (tools/native/sample_report.py names them)
    std::fprintf(f, "## leaf\n");
    std::vector<std::pair<std::uint32_t, std::uintptr_t>> v;
    for (const auto& kv : g_leaf) v.push_back({kv.second, kv.first});
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& e : v) {
        std::uintptr_t base = 0;
        const std::string m = module_of(e.second, &base);
        std::fprintf(f, "%s\t%llx\t%u\n", m.c_str(), static_cast<unsigned long long>(e.second - base), e.first);
    }
    std::fclose(f);
    std::printf("native_play: sampler: %llu samples (%llu running, %llu idle) written to %s\n", static_cast<unsigned long long>(g_total),
                static_cast<unsigned long long>(g_running), static_cast<unsigned long long>(g_idle_n), g_path.c_str());
    std::fflush(stdout);
}

}  // namespace

bool on() { return g_on; }

void register_this_thread() {
    if (!g_on) return;
    std::lock_guard<std::mutex> l(g_reg_mu);
    const unsigned n = g_nreg;
    if (n >= kThreads) {   // (Fable) more threads at once than slots: that one is not sampled, said once
        if (g_overflow++ == 0) std::printf("native_play: sampler: more than %u threads at once; the others are not sampled\n", kThreads);
        return;
    }
    Reg& r = g_reg[n];
    r = Reg{};
    r.ctx_slot = b3native::platform::host_ctx_slot();
    r.chain = b3host::native_chain::t_chain;
    r.depth = &b3host::native_chain::t_depth;
    r.game = !g_timer.joinable();   // (start() registers the game thread before its timer starts)
#if defined(_WIN32)
    r.handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
    if (!r.handle) return;
#elif !defined(__APPLE__)
    r.thread = pthread_self();
#endif
    g_nreg = n + 1;
}

void unregister_this_thread() {
    if (!g_on) return;
    std::lock_guard<std::mutex> l(g_reg_mu);
    const void* const* slot = b3native::platform::host_ctx_slot();
    for (unsigned i = 0; i < g_nreg; ++i) {
        if (g_reg[i].ctx_slot != slot) continue;
#if defined(_WIN32)
        if (g_reg[i].handle) CloseHandle(g_reg[i].handle);
#endif
        g_reg[i] = g_reg[g_nreg - 1];   // (the last one into its place)
        g_reg[--g_nreg] = Reg{};
        return;
    }
}

void start() {
    const char* p = std::getenv("B3_SAMPLE");
    if (!p || !*p || g_on) return;
    g_path = p;
    if (const char* e = std::getenv("B3_SAMPLE_FROM")) g_from = std::strtoull(e, nullptr, 10);
    if (const char* e = std::getenv("B3_SAMPLE_US")) g_us = std::max(200u, unsigned(std::strtoul(e, nullptr, 10)));
#if !defined(_WIN32) && !defined(__APPLE__)
    struct sigaction sa{};
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, nullptr) != 0) {
        std::fprintf(stderr, "sampler: SIGPROF handler not installed; off\n");
        return;
    }
#endif
    g_on = true;
    register_this_thread();
    std::printf("native_play: sampler on (B3_SAMPLE): every %u us from VBlank %llu, to %s\n", g_us, static_cast<unsigned long long>(g_from),
                g_path.c_str());
    std::fflush(stdout);
    g_timer = std::thread([] {
#if defined(_WIN32)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);   // (its ticks on time; it sleeps between them)
#endif
        bool began = false;
#if defined(_WIN32)
        // (Windows' sleeps end on the scheduler's tick, 15.6 ms unless raised: a high-resolution waitable timer instead,
        // Windows 10 1803 and later; else the plain sleep)
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
#endif
        while (!g_stop.load()) {
#if defined(_WIN32)
            LARGE_INTEGER due;
            due.QuadPart = -10LL * LONGLONG(g_us);   // relative, in 100 ns
            if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
            else std::this_thread::sleep_for(std::chrono::microseconds(g_us));
#else
            std::this_thread::sleep_for(std::chrono::microseconds(g_us));
#endif
            if (!g_active.load(std::memory_order_relaxed)) continue;
            if (!began) { began = true; g_t0 = std::chrono::steady_clock::now(); }
            sample_once();
        }
#if defined(_WIN32)
        if (timer) CloseHandle(timer);
#endif
    });
}

void vblank(std::uint64_t v) {
    if (g_on && !g_active.load(std::memory_order_relaxed) && v >= g_from) g_active.store(true);
}

void idle(bool waiting) {
    if (g_on) g_idle.store(waiting, std::memory_order_relaxed);
}

void stop_and_write() {
    if (!g_on || !g_timer.joinable()) return;
    g_stop.store(true);
    g_timer.join();
    write_file();
}

}  // namespace b3native::sampler
