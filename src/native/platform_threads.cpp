// src/native/platform_threads.cpp -- the kernel's threads, its scheduler and the semaphores' waits as the native platform
// answers them (written from docs/specs/ee-kernel.md §4, §5).
//
// The model: up to 255 threads (id 1 the main thread, there from the first query), 128 FIFO ready queues (priority 0
// first), a semaphore's waiters in a FIFO. The running thread changes only when it blocks (WaitSema, SleepThread),
// ends, or a ready thread of a strictly higher priority appears at a preemption check (maybe_preempt). With no thread
// ready, the blocked thread's host thread runs the idle loop (SIF completions, then platform_time.cpp's idle_step).
//
// Host side: each guest thread runs on a host thread of its own (the main thread on the caller's). Exactly one runs at
// a time: the baton (g_baton) names the host context allowed to run; a switch hands it over under one mutex and the
// receiver's condition variable, then the giver parks until the baton comes back. The guest stack pointer is the
// backend's and is kept per thread across a switch; b3asm's register file is thread_local, so per thread already. A
// terminated thread that is parked is unwound (ThreadUnwind thrown where it parked) before its terminator goes on; an
// error thrown on a guest thread's host thread is carried to the main host context and rethrown there.
#include "platform.h"

#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "hw_model.h"
#include "platform_sif.h"

#include <bit>
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#if !defined(_WIN32)
#include <pthread.h>
#endif
#include "b3report/thread_name.h"   // split2p (last: it includes windows.h)

namespace b3native::platform {

using b3host::CallArgs;
using b3host::CallResult;

namespace {

inline std::int32_t arg(const CallArgs& a, unsigned i) { return static_cast<std::int32_t>(a.gpr[i].w[0]); }
inline std::uint32_t uarg(const CallArgs& a, unsigned i) { return a.gpr[i].w[0]; }
inline void ret(CallResult& r, std::int32_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(v)));
}

// ee_thread_status_t's values [PS2SDK kernel.h THS_* / TSW_*]
constexpr std::int32_t kRunning = 1, kReady = 2, kWaiting = 4, kDormant = 0x10;
constexpr std::int32_t kWaitSleep = 1, kWaitSema = 2;
constexpr std::int32_t kMaxThreads = 256;   // ids 1..255
constexpr std::int32_t kPriorities = 128;
constexpr std::int32_t kMaxSemas = 256;     // ids 0..255
constexpr unsigned kDeadlockRounds = 600;
constexpr std::uint32_t kGameGp = 0x004E8670u;
#if !defined(_WIN32)
constexpr std::size_t kHostStack = std::size_t{8} << 20;   // macOS gives secondary threads 512 KB by default
#endif

// Thrown where a terminated (or exiting) thread's host thread is, to unwind it to its start. Not a std::exception, so
// no handler for errors takes it.
struct ThreadUnwind {};

// One host thread that runs a guest thread.
struct HostCtx {
    std::condition_variable cv;
    std::int32_t thread = 0;          // the guest thread it runs
    bool die = false;                 // terminated: unwind when the baton comes
    HostCtx* return_to = nullptr;     // whom to hand the baton back to after that unwind
    std::exception_ptr carried;       // main context: an error from another host thread, rethrown when it wakes
};

struct Thread {
    bool exists = false;
    std::int32_t id = 0, prio = 0, init_prio = 0, status = kDormant, wait = 0, wait_id = 0, wake_result = 0;
    std::uint32_t wakeups = 0, entry = 0, stack = 0, size = 0, gp = 0, argument = 0;
    HostCtx* ctx = nullptr;           // its host context while alive
    std::function<void()> resume;     // a restored thread: run instead of its entry when it first gets the baton
};

Thread g_th[kMaxThreads];
unsigned g_count = 0;                 // threads that exist
bool g_main_made = false;
std::int32_t g_cur = 1;               // the running thread (or the one blocked in the idle loop)
std::deque<std::int32_t> g_ready[kPriorities];
std::uint64_t g_ready_bits[2] = {0, 0};   // bit p: queue p not empty
std::deque<std::int32_t> g_waiters[kMaxSemas];
int g_isr = 0;

// never destroyed: parked host threads may still wait on them when the program ends
std::mutex& baton_mutex() {
    static std::mutex* m = new std::mutex;
    return *m;
}
HostCtx& main_ctx() {
    static HostCtx* c = new HostCtx;
    return *c;
}
HostCtx* g_baton = &main_ctx();
thread_local HostCtx* t_ctx = nullptr;   // null: not a guest thread's host thread (the main context)
HostCtx* self_ctx() { return t_ctx ? t_ctx : &main_ctx(); }

void mirror(const Thread& t) {
    if (t.id != 1) return;
    kernel().main_priority = t.prio;
    kernel().wakeups = t.wakeups;
}

void ensure_main() {
    if (g_main_made) return;
    g_main_made = true;
    Thread& t = g_th[1];
    if (!t.exists) ++g_count;
    t = Thread{};
    t.exists = true;
    t.id = 1;
    t.prio = kernel().main_priority;
    t.init_prio = 0;
    t.status = kRunning;
    t.entry = 0x00100220u;   // crt0's SetupThread arguments [C decomp crt0.s]
    t.stack = 0x01FF0000u;
    t.size = 0x10000u;
    t.gp = kGameGp;
    t.wakeups = kernel().wakeups;
    t.ctx = &main_ctx();
    main_ctx().thread = 1;
    g_cur = 1;
}

Thread* find_thread(std::int32_t id) {
    if (id <= 0 || id >= kMaxThreads || !g_th[id].exists) return nullptr;
    return &g_th[id];
}

KernelModel::Sema* find_sema(std::int32_t id) {
    for (auto& s : kernel().semas)
        if (s.id == id) return &s;
    return nullptr;
}

// ---- ready queues ----
void q_push(Thread& t, bool front) {
    auto& q = g_ready[t.prio];
    if (front) q.push_front(t.id);
    else q.push_back(t.id);
    g_ready_bits[t.prio >> 6] |= std::uint64_t{1} << (t.prio & 63);
    t.status = kReady;
}
void q_note_empty(std::int32_t prio) {
    if (g_ready[prio].empty()) g_ready_bits[prio >> 6] &= ~(std::uint64_t{1} << (prio & 63));
}
void q_remove(const Thread& t) {
    auto& q = g_ready[t.prio];
    for (auto it = q.begin(); it != q.end(); ++it)
        if (*it == t.id) {
            q.erase(it);
            break;
        }
    q_note_empty(t.prio);
}
std::int32_t best_prio() {
    if (g_ready_bits[0]) return std::countr_zero(g_ready_bits[0]);
    if (g_ready_bits[1]) return 64 + std::countr_zero(g_ready_bits[1]);
    return kPriorities;
}
// the front of the first non-empty queue, taken out; 0 when none is ready
std::int32_t q_pop_best() {
    const std::int32_t p = best_prio();
    if (p >= kPriorities) return 0;
    const std::int32_t id = g_ready[p].front();
    g_ready[p].pop_front();
    q_note_empty(p);
    return id;
}

void drop_waiter(std::int32_t id) {
    for (auto& w : g_waiters)
        for (auto it = w.begin(); it != w.end();)
            it = *it == id ? w.erase(it) : it + 1;
}

// §5.2: out of every queue, no wait, the initial priority, no pending wakeup
void make_dormant(Thread& t) {
    if (t.status == kReady) q_remove(t);
    drop_waiter(t.id);
    t.status = kDormant;
    t.wait = 0;
    t.wait_id = 0;
    t.prio = t.init_prio;
    t.wakeups = 0;
    mirror(t);
}

// ---- the baton ----
// Hands the baton to `to`; with `wait`, parks this host thread until it comes back (then a carried error is rethrown
// here, or a termination unwinds this thread from here).
void hand_baton(HostCtx* to, bool wait) {
    HostCtx* me = self_ctx();
    std::unique_lock<std::mutex> l(baton_mutex());
    g_baton = to;
    to->cv.notify_one();
    if (!wait) return;
    me->cv.wait(l, [me] { return g_baton == me; });
    if (me->die) throw ThreadUnwind{};
    if (me->carried) {
        std::exception_ptr e = me->carried;
        me->carried = nullptr;
        l.unlock();
        std::rethrow_exception(e);
    }
}

void thread_main(HostCtx* c);

void spawn(Thread& t) {
    auto* c = new HostCtx;   // kept: a parked host thread never outlives a reference to it
    c->thread = t.id;
    t.ctx = c;
#if defined(_WIN32)
    std::thread(thread_main, c).detach();
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kHostStack);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    const int e = pthread_create(&th, &attr, [](void* p) -> void* { thread_main(static_cast<HostCtx*>(p)); return nullptr; }, c);
    pthread_attr_destroy(&attr);
    if (e != 0) stub_unexpected("StartThread", "cannot create a host thread for thread " + std::to_string(t.id));
#endif
}

// Thread `id` (taken off its queue) runs now. Returns when this host thread's own guest thread runs again.
void run(std::int32_t id) {
    Thread& n = g_th[id];
    n.status = kRunning;
    g_cur = id;
    HostCtx* me = self_ctx();
    if (n.ctx == me) return;
    if (!n.ctx) spawn(n);
    b3host::Backend& b = b3host::backend();
    const b3host::gaddr sp = b.stack_pointer();
    hand_baton(n.ctx, true);
    b.set_stack_pointer(sp);
}

// The running thread has just blocked (its status set): the best ready thread runs; with none, the idle loop.
void block_loop(const char* who, std::int32_t self) {
    for (unsigned rounds = 0;; ++rounds) {
        if (const std::int32_t id = q_pop_best()) {
            run(id);
            return;
        }
        if (rounds == 0 && !b3host::backend().interrupts_enabled())
            stub_unexpected(who, "thread " + std::to_string(self) + " waits with interrupts disabled and no thread ready");
        if (rounds == kDeadlockRounds)
            stub_unexpected(who, "deadlock: thread " + std::to_string(self) + " waits and no thread became ready in " +
                                     std::to_string(kDeadlockRounds) + " rounds");
        sif::sif_service();
        if (const std::int32_t id = q_pop_best()) {
            run(id);
            return;
        }
        idle_step();
    }
}
void block_current(const char* who) {
    const std::int32_t self = g_cur;
    try {
        block_loop(who, self);
    } catch (const ThreadUnwind&) {
        throw;
    } catch (...) {
        // the run ends with this error (a deadlock, the VBlank limit, ...): the thread is left running, not waiting
        Thread& t = g_th[self];
        if (g_cur == self && t.status == kWaiting) {
            drop_waiter(self);
            t.status = kRunning;
            t.wait = 0;
            t.wait_id = 0;
        }
        throw;
    }
}

// The guest thread of this host thread has ended (dormant already): the next ready thread runs (VBlanks delivered
// until one is), and this host thread finishes.
void leave_ended(Thread& t) {
    t.ctx = nullptr;
    std::int32_t next = 0;
    for (unsigned n = 0; !(next = q_pop_best()); ++n) {
        if (n == kDeadlockRounds)
            stub_unexpected("ExitThread", "thread " + std::to_string(t.id) + " ended and no thread became ready in " +
                                              std::to_string(kDeadlockRounds) + " VBlanks");
        vblank();
    }
    Thread& nt = g_th[next];
    nt.status = kRunning;
    g_cur = next;
    if (!nt.ctx) spawn(nt);
    hand_baton(nt.ctx, false);
}

void thread_main(HostCtx* c) {
    t_ctx = c;
    b3port::name_this_thread(("guest " + std::to_string(c->thread)).c_str());   // split2p (the crash report)
    {
        std::unique_lock<std::mutex> l(baton_mutex());
        c->cv.wait(l, [c] { return g_baton == c; });
    }
    Thread& t = g_th[c->thread];
    std::exception_ptr err;
    try {
        try {
            b3host::backend().set_stack_pointer((t.stack + t.size) & ~15u);
            if (t.resume) {
                std::function<void()> f = std::move(t.resume);
                t.resume = nullptr;
                f();
            } else {
                b3host::call_guest<void>(t.entry, t.argument);
            }
            make_dormant(t);   // the entry returned
        } catch (const ThreadUnwind&) {
        }
        if (c->die) {   // terminated: back to the terminator
            if (t.ctx == c) t.ctx = nullptr;
            hand_baton(c->return_to, false);
            return;
        }
        leave_ended(t);
        return;
    } catch (...) {
        err = std::current_exception();
    }
    // the run ends with the error: this thread is gone, the main context's thread runs (to rethrow it)
    if (t.ctx == c) t.ctx = nullptr;
    if (t.status != kDormant) make_dormant(t);
    Thread& m = g_th[main_ctx().thread];
    if (m.status == kReady) q_remove(m);
    drop_waiter(m.id);
    m.status = kRunning;
    m.wait = 0;
    m.wait_id = 0;
    g_cur = m.id;
    main_ctx().carried = err;
    hand_baton(&main_ctx(), false);
}

// ---- semaphores ----
// SignalSema's rule; `woke` set when a waiter became ready
std::int32_t signal(std::int32_t id, bool& woke) {
    KernelModel::Sema* s = find_sema(id);
    if (!s) return -1;
    if (id >= 0 && id < kMaxSemas) {
        auto& w = g_waiters[id];
        while (!w.empty()) {
            const std::int32_t tid = w.front();
            w.pop_front();
            Thread* t = find_thread(tid);
            if (t && t->status == kWaiting && t->wait == kWaitSema && t->wait_id == id) {
                t->wait = 0;
                t->wait_id = 0;
                t->wake_result = 0;
                q_push(*t, false);
                woke = true;
                return 0;
            }
        }
    }
    if (s->count >= s->max) return -1;
    ++s->count;
    return 0;
}

}  // namespace

void isr_enter() { ++g_isr; }

void isr_exit() { --g_isr; }

int isr_depth() { return g_isr; }

// §5.4: the running thread gives way to a ready thread of a strictly higher priority (it goes to the front of its
// queue). Not inside a handler, not with only the main thread, not while the current thread is not running (the idle
// loop), not with interrupts disabled.
void maybe_preempt() {
    if (g_isr > 0) return;
    ensure_main();
    if (g_count <= 1) return;
    Thread& c = g_th[g_cur];
    if (c.status != kRunning) return;
    if (!b3host::backend().interrupts_enabled()) return;
    if (best_prio() >= c.prio) return;
    const std::int32_t next = q_pop_best();
    q_push(c, true);
    run(next);
}

std::int32_t current_thread_id() {
    ensure_main();
    return g_cur;
}

// CreateThread(ee_thread_t* p) [PS2SDK kernel.h: +4 func, +8 stack, +12 stack_size, +16 gp_reg, +20
// initial_priority] -> the id (2..255, dormant), or -1
void create_thread(const CallArgs& a, CallResult& r) {
    ensure_main();
    const b3host::gaddr p = uarg(a, 0);
    if (p & 3u) stub_unexpected("CreateThread", "parameter block not word aligned");
    std::int32_t id = -1;
    for (std::int32_t k = 2; k < kMaxThreads; ++k)
        if (!g_th[k].exists) { id = k; break; }
    if (id < 0) { ret(r, -1); return; }
    Thread& t = g_th[id];
    t = Thread{};
    t.exists = true;
    t.id = id;
    t.entry = b3host::load<std::uint32_t>(p + 4);
    t.stack = b3host::load<std::uint32_t>(p + 8);
    t.size = b3host::load<std::uint32_t>(p + 12);
    t.gp = b3host::load<std::uint32_t>(p + 16);
    t.init_prio = t.prio = static_cast<std::int32_t>(b3host::load<std::uint32_t>(p + 20));
    t.status = kDormant;
    ++g_count;
    kernel().threads.push_back(KernelModel::Thread{id, t.entry, t.stack, t.size, t.gp, t.prio, false, 0});
    ret(r, id);
}

// _StartThread(id, argument) -> the id; -1 when unknown, not dormant, priority out of 0..127 or still alive on its
// host thread
void start_thread(const CallArgs& a, CallResult& r) {
    ensure_main();
    const std::int32_t id = arg(a, 0);
    Thread* t = find_thread(id);
    if (!t || t->status != kDormant || t->prio < 0 || t->prio >= kPriorities || t->ctx) { ret(r, -1); return; }
    if (t->stack == 0) stub_unexpected("StartThread", "thread " + std::to_string(id) + " has no stack");
    t->argument = uarg(a, 1);
    for (auto& k : kernel().threads)
        if (k.id == id) {
            k.started = true;
            k.argument = t->argument;
        }
    q_push(*t, false);
    ret(r, id);
    maybe_preempt();
}

void get_thread_id(const CallArgs&, CallResult& r) {
    ensure_main();
    ret(r, g_cur);
}

// ChangeThreadPriority(id (0: the running thread), priority) -> the previous priority, or -1
void change_thread_priority(const CallArgs& a, CallResult& r) {
    ensure_main();
    std::int32_t id = arg(a, 0);
    if (id == 0) id = g_cur;
    const std::uint32_t prio = uarg(a, 1);
    Thread* t = find_thread(id);
    if (!t || t->status == kDormant || prio > 127u) { ret(r, -1); return; }
    const std::int32_t previous = t->prio;
    const auto np = static_cast<std::int32_t>(prio);
    if (t->status == kReady && np != t->prio) {
        q_remove(*t);
        t->prio = np;
        q_push(*t, false);
    } else {
        t->prio = np;
    }
    mirror(*t);
    ret(r, previous);
    maybe_preempt();
}

// SleepThread: a pending wakeup is used up at once; else the thread waits for WakeupThread
void sleep_thread(const CallArgs&, CallResult& r) {
    ensure_main();
    Thread& t = g_th[g_cur];
    if (t.wakeups > 0) {
        --t.wakeups;
        mirror(t);
        ret(r, 0);
        return;
    }
    if (g_isr > 0) stub_unexpected("SleepThread", "called inside an interrupt handler");
    t.status = kWaiting;
    t.wait = kWaitSleep;
    t.wait_id = 0;
    block_current("SleepThread");
    ret(r, 0);
}

// WakeupThread / iWakeupThread(id): a sleeping thread becomes ready; any other live one counts a wakeup
void wakeup_thread(const CallArgs& a, CallResult& r) {
    ensure_main();
    Thread* t = find_thread(arg(a, 0));
    if (!t || t->status == kDormant) { ret(r, -1); return; }
    if (t->status == kWaiting && t->wait == kWaitSleep) {
        t->wait = 0;
        t->wait_id = 0;
        t->wake_result = 0;
        q_push(*t, false);
    } else {
        if (t->wakeups != 0xFFFFFFFFu) ++t->wakeups;
        mirror(*t);
    }
    ret(r, 0);
    maybe_preempt();
}

// TerminateThread / iTerminateThread(id): another live thread becomes dormant (unwound first when it is parked on its
// host thread) -> 0; -1 when unknown, the caller or dormant; the main thread is fatal
void terminate_thread(const CallArgs& a, CallResult& r) {
    ensure_main();
    const std::int32_t id = arg(a, 0);
    Thread* t = find_thread(id);
    if (!t || id == g_cur || t->status == kDormant) { ret(r, -1); return; }
    if (id == 1 || t->ctx == &main_ctx()) stub_unexpected("TerminateThread", "the main thread cannot be terminated");
    make_dormant(*t);
    if (HostCtx* c = t->ctx) {
        c->die = true;
        c->return_to = self_ctx();
        b3host::Backend& b = b3host::backend();
        const b3host::gaddr sp = b.stack_pointer();
        hand_baton(c, true);
        b.set_stack_pointer(sp);
    }
    ret(r, 0);
}

// DeleteThread(id): a dormant thread (not the caller, no live host thread) goes -> 0; else -1
void delete_thread(const CallArgs& a, CallResult& r) {
    ensure_main();
    const std::int32_t id = arg(a, 0);
    Thread* t = find_thread(id);
    if (!t || id == g_cur || t->status != kDormant || t->ctx) { ret(r, -1); return; }
    *t = Thread{};
    --g_count;
    auto& v = kernel().threads;
    for (auto it = v.begin(); it != v.end(); ++it)
        if (it->id == id) {
            v.erase(it);
            break;
        }
    ret(r, 0);
}

// ExitThread / ExitDeleteThread: the calling thread ends (dormant; the record stays)
void exit_thread(const CallArgs&, CallResult&) {
    ensure_main();
    if (self_ctx() == &main_ctx()) stub_unexpected("ExitThread", "called by the main thread");
    make_dormant(g_th[g_cur]);
    throw ThreadUnwind{};
}

// ReferThreadStatus / iReferThreadStatus(id (0: the running thread), ee_thread_status_t* p) -> 0, or -1
void refer_thread_status(const CallArgs& a, CallResult& r) {
    ensure_main();
    std::int32_t id = arg(a, 0);
    if (id == 0) id = g_cur;
    const b3host::gaddr p = uarg(a, 1);
    const Thread* t = find_thread(id);
    if (!t || (p & 3u)) { ret(r, -1); return; }
    const bool waiting = t->status == kWaiting;
    const std::uint32_t w[12] = {static_cast<std::uint32_t>(t->status),
                                 t->entry,
                                 t->stack,
                                 t->size,
                                 t->gp,
                                 static_cast<std::uint32_t>(t->init_prio),
                                 static_cast<std::uint32_t>(t->prio),
                                 0u,
                                 0u,
                                 waiting ? static_cast<std::uint32_t>(t->wait) : 0u,
                                 waiting && t->wait == kWaitSema ? static_cast<std::uint32_t>(t->wait_id) : 0u,
                                 t->wakeups};
    for (unsigned k = 0; k < 12; ++k) b3host::store<std::uint32_t>(p + 4 * k, w[k]);
    ret(r, 0);
}

// WaitSema(id): 0 at once when the count is positive (taken); else the thread waits -> its wake result (0 signalled,
// -1 the semaphore deleted); -1 when unknown
void wait_sema(const CallArgs& a, CallResult& r) {
    ensure_main();
    const std::int32_t id = arg(a, 0);
    KernelModel::Sema* s = find_sema(id);
    if (!s) { ret(r, -1); return; }
    if (s->count > 0) {
        --s->count;
        ret(r, 0);
        return;
    }
    if (g_isr > 0) stub_unexpected("WaitSema", "would wait inside an interrupt handler");
    if (!b3host::backend().interrupts_enabled()) stub_unexpected("WaitSema", "would wait with interrupts disabled");
    if (id < 0 || id >= kMaxSemas) stub_unexpected("WaitSema", "semaphore id " + std::to_string(id));
    Thread& t = g_th[g_cur];
    t.status = kWaiting;
    t.wait = kWaitSema;
    t.wait_id = id;
    g_waiters[id].push_back(t.id);
    block_current("WaitSema");
    ret(r, t.wake_result);
}

// PollSema / iPollSema(id): the id when the count was positive (taken); else -1. A semaphore at 0 first lets pending
// SIF completions in (outside handlers, interrupts on).
void poll_sema(const CallArgs& a, CallResult& r) {
    const std::int32_t id = arg(a, 0);
    KernelModel::Sema* s = find_sema(id);
    if (s && s->count <= 0 && g_isr == 0 && b3host::backend().interrupts_enabled()) {
        sif::sif_service();
        s = find_sema(id);
    }
    if (!s || s->count <= 0) { ret(r, -1); return; }
    --s->count;
    ret(r, id);
}

bool kernel_signal_sema(std::int32_t id) {
    ensure_main();
    bool woke = false;
    const std::int32_t v = signal(id, woke);
    if (woke) maybe_preempt();
    return v == 0;
}

// SignalSema / iSignalSema(id): the first waiter becomes ready (count unchanged), else the count goes up unless at its
// maximum (-1); -1 when unknown
void signal_sema(const CallArgs& a, CallResult& r) {
    ensure_main();
    bool woke = false;
    ret(r, signal(arg(a, 0), woke));
    if (woke) maybe_preempt();
}

// DeleteSema(id): the semaphore goes; its waiters become ready with wake result -1 -> 0; -1 when unknown
void delete_sema(const CallArgs& a, CallResult& r) {
    ensure_main();
    const std::int32_t id = arg(a, 0);
    auto& v = kernel().semas;
    auto it = v.begin();
    while (it != v.end() && it->id != id) ++it;
    if (it == v.end()) { ret(r, -1); return; }
    v.erase(it);
    if (id >= 0 && id < kMaxSemas) {
        auto& w = g_waiters[id];
        while (!w.empty()) {
            const std::int32_t tid = w.front();
            w.pop_front();
            Thread* t = find_thread(tid);
            if (t && t->status == kWaiting && t->wait == kWaitSema && t->wait_id == id) {
                t->wait = 0;
                t->wait_id = 0;
                t->wake_result = -1;
                q_push(*t, false);
            }
        }
    }
    ret(r, 0);
    maybe_preempt();
}

}  // namespace b3native::platform
