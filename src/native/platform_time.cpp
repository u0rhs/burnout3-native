// src/native/platform_time.cpp -- the platform's virtual time and interrupt delivery (written from
// docs/specs/ee-kernel.md §6-§7): the virtual EE clock (the native backend's cost counter plus the time the
// platform jumped over), the VBlank grid, the kernel's alarms, the backend's time event (time_check), the waits for
// time (vblank, idle_step, skip_to_next_event) and the DMAC interrupt's delivery to the game's handlers.
//
// The order of delivery is part of every golden run: alarms due before the next VBlank, then DMAC completions due
// before it, then the VBlank (INTC causes 2 and 3, the VBlank hooks, the pads, the SIF completions), then late alarms.
#include "platform.h"
#include "platform_sif.h"

#include "b3host/call.h"
#include "b3host/native_backend.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace b3native::platform {
namespace {

using b3host::CallArgs;
using b3host::CallResult;
inline std::int32_t arg(const CallArgs& a, unsigned i) { return static_cast<std::int32_t>(a.gpr[i].w[0]); }
inline void ret(CallResult& r, std::int32_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(v)));
}

constexpr std::uint64_t kNever = ~std::uint64_t{0};
constexpr std::uint64_t kVBlankPeriod = 4915200;    // 294.912 MHz / 60 (§6.1)
constexpr std::uint64_t kAlarmTick = 18750;         // EE cycles per alarm tick (§6.4)
constexpr std::uint64_t kGateDelay = 64;            // the PATH3 gate's wake-up (§6.5)
constexpr std::uint64_t kMaxDmacPerVBlank = 200000; // a handler restarting transfers forever (§7)
constexpr std::int32_t kMaxAlarms = 64;

struct Alarm {
    std::int32_t id;
    std::uint32_t ticks, handler, common;
    std::uint64_t deadline;
};

// The clock's state. The guest threads run one at a time (a baton), so plain globals are enough.
struct Clock {
    b3host::NativeBackend* armed = nullptr;   // the backend whose time event this clock sets (arm_virtual_time)
    std::uint64_t skipped = 0;                // time jumped over while waiting
    std::uint64_t next_vblank = kVBlankPeriod;
    std::uint64_t gate = kNever;              // the PATH3 gate's wake-up, when armed
    std::uint64_t dmac_deliveries = 0;        // since the last VBlank
    std::function<std::uint64_t()> extra_next;
    std::function<void()> extra_due;
    std::vector<Alarm> alarms;                // in the order they were set
    bool in_time_check = false;
};
Clock g;

// The current backend as the native one (its cost counter and interrupt state); looked up again only when the bound
// backend changes.
b3host::Backend* g_bound = nullptr;
b3host::NativeBackend* g_native = nullptr;
b3host::NativeBackend& nb() {
    b3host::Backend* b = &b3host::backend();
    if (b != g_bound) {
        g_native = dynamic_cast<b3host::NativeBackend*>(b);
        if (!g_native) b3host::fatal("platform_time: needs the native backend (its cost counter and interrupt state)");
        g_bound = b;
    }
    return *g_native;
}

std::uint64_t now() { return nb().cost() + g.skipped; }
bool interrupts_on() { return nb().interrupts_enabled(); }
// The clock moves forward to t (never back). The backend's time event is not moved with it: it stays where the last
// arming put it, relative to the cost counter (later in virtual time by the jump).
void jump_to(std::uint64_t t) {
    const std::uint64_t n = now();
    if (t > n) g.skipped += t - n;
}

std::uint64_t vblank_limit() {
    static const std::uint64_t limit = [] {
        const char* e = std::getenv("B3NATIVE_MAX_VBLANKS");
        return e && *e ? std::strtoull(e, nullptr, 0) : 0ull;
    }();
    return limit;
}

// The pending alarm with the smallest deadline (the earliest set among equal ones), or null.
const Alarm* earliest_alarm() {
    const Alarm* best = nullptr;
    for (const Alarm& x : g.alarms)
        if (!best || x.deadline < best->deadline) best = &x;
    return best;
}
std::uint64_t earliest_alarm_deadline() {
    const Alarm* a = earliest_alarm();
    return a ? a->deadline : kNever;
}

void arm() {
    b3host::NativeBackend* b = g.armed;
    // none armed (unit tests), or another backend bound since (a unit test's earlier one); the platform arms its
    // backend before binding it (native_play, native_boot)
    if (!b || (b3host::detail::g_backend && static_cast<b3host::Backend*>(b) != b3host::detail::g_backend)) return;
    if (sif::sif_pending()) {   // an IOP -> EE reply waits: at the next dispatched call
        b->time_event_ = 0;
        return;
    }
    std::uint64_t t = g.next_vblank;
    if (g.extra_next) t = std::min(t, g.extra_next());
    t = std::min(t, earliest_alarm_deadline());
    t = std::min(t, g.gate);
    t = std::min(t, sif::sif_next_ready());
    t = std::min(t, hw_model().next_completion_ready());
    b->time_event_ = t == kNever ? kNever : (t > g.skipped ? t - g.skipped : 0);
}

// Interrupt context for platform-delivered handlers: interrupts off and isr depth + 1 while `call` runs; the state is
// restored after (restoring "enabled" runs the backend's pending-interrupt hook, which may deliver more: not from a
// destructor, so an error thrown by a handler ends the run as thrown).
template <class F>
void in_interrupt(F&& call) {
    b3host::NativeBackend& b = nb();
    const bool was = b.interrupts_enabled();
    b.set_interrupts_enabled(false);
    isr_enter();
    call();
    isr_exit();
    b.set_interrupts_enabled(was);
}

// One alarm, when one is due and no handler runs (§6.4). True when one was delivered.
bool deliver_alarm() {
    if (isr_depth() > 0) return false;
    const Alarm* e = earliest_alarm();
    if (!e || e->deadline > now()) return false;
    const Alarm a = *e;
    g.alarms.erase(g.alarms.begin() + (e - g.alarms.data()));
    arm();   // the alarm set changed: the time event follows at once (a due event fires inside the handler)
    in_interrupt([&] { b3host::call_guest<int>(a.handler, static_cast<std::uint32_t>(a.id), a.ticks, a.common); });
    return true;
}

// The INTC handlers of one cause (§6.3).
void dispatch_intc(std::uint32_t cause) {
    KernelModel& k = kernel();
    if (!(k.intc_mask & (1u << cause))) return;
    struct Call { std::uint32_t handler, argument; };
    Call calls[128];
    unsigned n = 0;
    for (const KernelModel::Handler& h : k.intc)
        if (h.cause == cause && n < 128) calls[n++] = {h.handler, h.argument};
    if (n == 0) return;
    hw_model().ack_intc(cause);
    const bool was = interrupts_on();
    in_interrupt([&] {
        for (unsigned i = 0; i < n; ++i) b3host::call_guest<int>(calls[i].handler, cause, calls[i].argument);
    });
    if (was) hw_model().poll_interrupts();
}

// One VBlank (§6.2).
void deliver_vblank() {
    KernelModel& k = kernel();
    const std::uint64_t limit = vblank_limit();
    if (limit > 0 && k.vblanks >= limit) throw VBlankLimit(k.vblanks);
    ++k.vblanks;
    g.dmac_deliveries = 0;
    g.next_vblank += kVBlankPeriod;
    arm();
    hw_model().vblank_start();
    sif::sif_service();
    dispatch_intc(2);   // VBlank start
    dispatch_intc(3);   // VBlank end
    std::vector<std::function<void()>>& hooks = vblank_hooks();
    for (std::size_t i = 0; i < hooks.size(); ++i) hooks[i]();
    sif::pad_vblank();
    sif::sif_service();
}

// The front DMAC completion, when it is ready before the next VBlank and not after the earliest alarm (§6.6): the clock
// jumps to it and the hardware model delivers. True when the queue moved.
bool due_completion_step(bool& tried) {
    tried = false;
    if (isr_depth() > 0) return false;
    HwModel& hw = hw_model();
    const std::uint64_t r = hw.next_completion_ready();
    if (r == kNever || r >= g.next_vblank || r > earliest_alarm_deadline()) return false;
    tried = true;
    jump_to(r);
    const std::size_t before = hw.queued_completions();
    hw.poll_interrupts();
    arm();
    return hw.queued_completions() != before;
}

}  // namespace

std::uint64_t virtual_cycles() { return now(); }

std::uint64_t next_vblank_start(std::uint64_t t) {
    const std::uint64_t nv = g.next_vblank;
    if (t <= nv) return nv - (nv - t) / kVBlankPeriod * kVBlankPeriod;
    const std::uint64_t k = (t - nv + kVBlankPeriod - 1) / kVBlankPeriod;
    return nv + k * kVBlankPeriod;
}

void set_extra_time_source(std::function<std::uint64_t()> next_cycles, std::function<void()> due) {
    g.extra_next = std::move(next_cycles);
    g.extra_due = std::move(due);
}

std::vector<std::function<void()>>& vblank_hooks() {
    static std::vector<std::function<void()>> hooks;
    return hooks;
}

void add_vblank_hook(std::function<void()> h) { vblank_hooks().push_back(std::move(h)); }

// SetAlarm / iSetAlarm(ticks, handler, common): the alarm's id (the lowest free in 1..64), -1 when the handler is 0 or
// none is free (§6.4)
void set_alarm(const CallArgs& a, CallResult& r) {
    const std::uint32_t ticks = static_cast<std::uint32_t>(arg(a, 0)) & 0xFFFFu;
    const std::uint32_t handler = static_cast<std::uint32_t>(arg(a, 1));
    const std::uint32_t common = static_cast<std::uint32_t>(arg(a, 2));
    if (handler == 0) { ret(r, -1); return; }
    std::int32_t id = 0;
    for (std::int32_t c = 1; c <= kMaxAlarms && !id; ++c) {
        bool used = false;
        for (const Alarm& x : g.alarms) used = used || x.id == c;
        if (!used) id = c;
    }
    if (!id) { ret(r, -1); return; }
    const std::uint64_t delay = std::uint64_t(std::max<std::uint32_t>(ticks, 1u)) * kAlarmTick;
    const std::uint64_t t = now();
    const std::uint64_t deadline = t > kNever - delay ? kNever : t + delay;
    g.alarms.push_back(Alarm{id, ticks, handler, common, deadline});
    ret(r, id);
    arm();
}

void release_alarm(const CallArgs& a, CallResult& r) {
    const std::int32_t id = arg(a, 0);
    for (auto it = g.alarms.begin(); it != g.alarms.end(); ++it)
        if (it->id == id) {
            g.alarms.erase(it);
            ret(r, 0);
            return;
        }
    ret(r, -1);
}

// The backend's time event (§6.5).
namespace {
void time_check_events();
}
void time_check() {
    if (g.in_time_check) return;
    g.in_time_check = true;
    {
        struct Done { ~Done() { g.in_time_check = false; } } done;
        time_check_events();
    }
    // after the guard: a switch parks this host thread here, and the next thread's time events must run
    maybe_preempt();
}
namespace {
void time_check_events() {
    if (sif::sif_pending() && interrupts_on()) sif::sif_service();
    if (g.gate != kNever && g.gate <= now()) {
        g.gate = kNever;
        arm();
    }
    if (g.extra_next && g.extra_next() <= now()) {
        if (g.extra_due) g.extra_due();
        arm();
    }
    HwModel& hw = hw_model();
    for (;;) {
        const std::uint64_t t = now();
        const std::uint64_t alarm = isr_depth() == 0 ? earliest_alarm_deadline() : kNever;
        if (alarm <= t && alarm <= g.next_vblank && deliver_alarm()) continue;
        const std::uint64_t ready = hw.next_completion_ready();
        if (ready <= t && ready <= g.next_vblank) {
            const std::size_t before = hw.queued_completions();
            hw.poll_interrupts();
            arm();
            if (hw.queued_completions() != before) continue;
        }
        if (t >= g.next_vblank && interrupts_on()) {
            deliver_vblank();
            continue;
        }
        if (alarm <= t && deliver_alarm()) continue;
        break;
    }
}
}  // namespace

void gate_latched() {
    g.gate = now() + kGateDelay;
    arm();
}

// A thread waits for the next VBlank (§6.6): the alarms and DMAC completions before it are delivered on the way.
void vblank() {
    if (!interrupts_on()) stub_unexpected("vblank", "a wait for the VBlank with interrupts disabled");
    KernelModel& k = kernel();
    const std::uint64_t target = k.vblanks + 1;
    while (k.vblanks < target) {
        if (isr_depth() == 0) {
            const Alarm* e = earliest_alarm();
            if (e && e->deadline < g.next_vblank) {
                jump_to(e->deadline);
                deliver_alarm();
                maybe_preempt();
                continue;
            }
            bool tried = false;
            if (due_completion_step(tried)) {
                maybe_preempt();
                continue;
            }
        }
        jump_to(g.next_vblank);
        deliver_vblank();
        maybe_preempt();
    }
}

bool dmac_interrupt(unsigned channel) {
    if (!interrupts_on() || isr_depth() > 0) return false;
    if (++g.dmac_deliveries > kMaxDmacPerVBlank)
        stub_unexpected("DMAC interrupt", "more than 200000 DMAC interrupts on channel " + std::to_string(channel) +
                                              " since the last VBlank (a handler restarting transfers forever)");
    hw_model().ack_dmac(channel);
    dma_log('i', channel);
    struct Call { std::uint32_t handler, argument; };
    Call calls[128];
    unsigned n = 0;
    for (const KernelModel::Handler& h : kernel().dmac)
        if (h.cause == channel && n < 128) calls[n++] = {h.handler, h.argument};
    b3host::NativeBackend& b = nb();
    b.set_interrupts_enabled(false);
    isr_enter();
    for (unsigned i = 0; i < n; ++i) b3host::call_guest<int>(calls[i].handler, static_cast<std::uint32_t>(channel), calls[i].argument);
    isr_exit();
    b.set_interrupts_enabled(true);
    return true;
}

bool skip_to_next_event() {
    if (isr_depth() > 0 || !interrupts_on()) return false;
    std::uint64_t t = g.next_vblank;
    t = std::min(t, g.gate);
    t = std::min(t, earliest_alarm_deadline());
    t = std::min(t, hw_model().next_completion_ready());
    t = std::min(t, sif::sif_next_ready());
    jump_to(t);
    time_check();
    return true;
}

// No thread is ready (§6.6): the next event -- an alarm before the VBlank and not after the front completion, else the
// due completion, else the VBlank.
void idle_step() {
    if (isr_depth() == 0) {
        const Alarm* e = earliest_alarm();
        if (e && e->deadline < g.next_vblank && e->deadline <= hw_model().next_completion_ready()) {
            jump_to(e->deadline);
            deliver_alarm();
            maybe_preempt();
            return;
        }
        bool tried = false;
        if (due_completion_step(tried)) {
            maybe_preempt();
            return;
        }
        if (tried) return;
    }
    vblank();
}

std::uint64_t vblank_count() { return kernel().vblanks; }

void arm_virtual_time(b3host::NativeBackend& b) {
    g.armed = &b;
    b.on_time = [] { time_check(); };
    arm();
}

std::function<std::uint64_t()>& guest_pending_cycles() {
    static std::function<std::uint64_t()> pending;
    return pending;
}

void rearm_virtual_time() { arm(); }

}  // namespace b3native::platform
