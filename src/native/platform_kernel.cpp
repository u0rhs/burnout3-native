// src/native/platform_kernel.cpp -- the kernel's objects as the native platform answers them (written
// from docs/specs/ee-kernel.md §2-§4, §9): the INTC / DMAC handler lists, the INTC / DMAC enable masks, CreateSema,
// SetGsCrt, GsPutIMR, the OSD configuration, DIntr / EIntr. The semaphores' waits and the threads:
// platform_threads.cpp; virtual time and interrupt delivery: platform_time.cpp.
//
// Arguments are the EE ABI's a0..a3 (a.gpr[0..3], low word); results are v0, the 32-bit value sign-extended.
#include "platform.h"

#include "b3host/call.h"
#include "b3host/native_backend.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace b3native::platform {
namespace {

using b3host::CallArgs;
using b3host::CallResult;
inline std::int32_t arg(const CallArgs& a, unsigned i) { return static_cast<std::int32_t>(a.gpr[i].w[0]); }
inline std::uint32_t uarg(const CallArgs& a, unsigned i) { return a.gpr[i].w[0]; }
inline void ret(CallResult& r, std::int32_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(v)));
}

// The game's gp (crt0's _gp), recorded with every handler the game adds.
constexpr std::uint32_t kGameGp = 0x004E8670u;

}  // namespace

namespace {
KernelModel g_kernel;
}  // namespace
KernelModel& kernel() { return g_kernel; }

// _InitSys -> InitTimer registers libkernl's TIM3 handler before the game adds any: it takes INTC id 1 (so the game's
// VBlank handler gets id 2, as on retail).
void note_libkernl_timer_handler() {
    if (!g_kernel.intc.empty()) return;
    KernelModel::add(g_kernel.intc, 11u, 0x00115D00u, 0u, 0u, kGameGp);
}

std::vector<IntcHandler> intc_handlers() {
    std::vector<IntcHandler> v;
    v.reserve(g_kernel.intc.size());
    for (const auto& h : g_kernel.intc) v.push_back(IntcHandler{h.id, h.cause, h.handler, h.argument});
    return v;
}

std::uint32_t intc_mask() { return g_kernel.intc_mask; }

// AddIntcHandler(cause, handler, next, argument) -> the id, or -1 when the list is full
void add_intc_handler(const CallArgs& a, CallResult& r) {
    ret(r, KernelModel::add(g_kernel.intc, uarg(a, 0), uarg(a, 1), uarg(a, 2), uarg(a, 3), kGameGp));
}

// RemoveIntcHandler(cause, id) -> 0, or -1 when no record has both
void remove_intc_handler(const CallArgs& a, CallResult& r) {
    ret(r, KernelModel::remove(g_kernel.intc, uarg(a, 0), arg(a, 1)));
}

// AddDmacHandler / AddDmacHandler2(channel, handler, next, argument): as INTC on the DMAC list; a channel past 9 is
// not a DMAC channel (fatal)
void add_dmac_handler(const CallArgs& a, CallResult& r) {
    const std::uint32_t ch = uarg(a, 0);
    if (ch > 9u) stub_unexpected("AddDmacHandler", "channel " + std::to_string(ch) + " (the DMAC has 0..9)");
    ret(r, KernelModel::add(g_kernel.dmac, ch, uarg(a, 1), uarg(a, 2), uarg(a, 3), kGameGp));
}

// RemoveDmacHandler(channel, id): a channel past 9 -> -1
void remove_dmac_handler(const CallArgs& a, CallResult& r) {
    const std::uint32_t ch = uarg(a, 0);
    if (ch > 9u) { ret(r, -1); return; }
    ret(r, KernelModel::remove(g_kernel.dmac, ch, arg(a, 1)));
}

namespace {
// Enable / Disable of one bit of a mask: 1 when the bit's state changes, else 0.
std::int32_t set_bit(std::uint32_t& mask, unsigned bit, bool on) {
    const std::uint32_t b = 1u << bit;
    const bool was = (mask & b) != 0;
    if (on) mask |= b;
    else mask &= ~b;
    return was != on ? 1 : 0;
}
void dmac_enable(const char* name, const CallArgs& a, CallResult& r, bool on) {
    const std::uint32_t ch = uarg(a, 0);
    if (ch > 9u) stub_unexpected(name, "channel " + std::to_string(ch) + " (the DMAC has 0..9)");
    const std::int32_t changed = set_bit(g_kernel.dmac_mask, ch, on);
    ret(r, changed);
    // D_STAT's mask half follows the enabled channels; a pending channel interrupt may be delivered here
    hw_model().set_dmac_mask(g_kernel.dmac_mask);
}
void intc_enable(const char* name, const CallArgs& a, CallResult& r, bool on) {
    const std::uint32_t cause = uarg(a, 0);
    if (cause > 31u) stub_unexpected(name, "cause " + std::to_string(cause) + " (INTC causes are 0..31)");
    ret(r, set_bit(g_kernel.intc_mask, cause, on));
}
}  // namespace

void enable_dmac(const CallArgs& a, CallResult& r) { dmac_enable("EnableDmac", a, r, true); }
void disable_dmac(const CallArgs& a, CallResult& r) { dmac_enable("DisableDmac", a, r, false); }
void enable_intc(const CallArgs& a, CallResult& r) { intc_enable("EnableIntc", a, r, true); }
void disable_intc(const CallArgs& a, CallResult& r) { intc_enable("DisableIntc", a, r, false); }

// CreateSema(ee_sema_t* p) [PS2SDK kernel.h: +4 max_count, +8 init_count, +16 attr, +20 option] -> the id, or -1
void create_sema(const CallArgs& a, CallResult& r) {
    const b3host::gaddr p = uarg(a, 0);
    if (p & 3u) stub_unexpected("CreateSema", "parameter block not word aligned");
    const auto max = static_cast<std::int32_t>(b3host::load<std::uint32_t>(p + 4));
    const auto init = static_cast<std::int32_t>(b3host::load<std::uint32_t>(p + 8));
    const std::uint32_t attr = b3host::load<std::uint32_t>(p + 16);
    const std::uint32_t option = b3host::load<std::uint32_t>(p + 20);
    ret(r, g_kernel.create_sema(init, max, attr, option));
}

// SetGsCrt(interlace, mode, ffmode): kept for the display; the GS's display registers are the game's own writes
namespace {
std::uint16_t g_crt_interlace = 0, g_crt_mode = 0, g_crt_ffmode = 0;
}  // namespace
void set_gs_crt(const CallArgs& a, CallResult& r) {
    r = CallResult{};
    g_crt_interlace = static_cast<std::uint16_t>(uarg(a, 0));
    g_crt_mode = static_cast<std::uint16_t>(uarg(a, 1));
    g_crt_ffmode = static_cast<std::uint16_t>(uarg(a, 2));
    char b[96];
    std::snprintf(b, sizeof b, "kept interlace %u mode 0x%X ffmode %u; the kernel's CRT setup not run", g_crt_interlace,
                  g_crt_mode, g_crt_ffmode);
    stub_note("SetGsCrt", b);
}

// GsPutIMR(imr): the GS's interrupt mask set to the low word; v0 the previous mask (64 bits)
void gs_put_imr(const CallArgs& a, CallResult& r) {
    HwModel& hw = hw_model();
    const std::uint64_t previous = hw.gs_imr();
    hw.set_gs_imr(uarg(a, 0));
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(previous);
}

// The OSD configuration word GetOsdConfigParam answers: 0x00012010, or B3NATIVE_OSD_CONFIG=<hex> (read once).
std::uint32_t osd_config_param() {
    static const std::uint32_t word = [] {
        const char* e = std::getenv("B3NATIVE_OSD_CONFIG");
        return e && *e ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 16)) : 0x00012010u;
    }();
    return word;
}

// GetOsdConfigParam(p): the word stored at p; v0 0
void get_osd_config_param(const CallArgs& a, CallResult& r) {
    const b3host::gaddr p = uarg(a, 0);
    if (p & 3u) stub_unexpected("GetOsdConfigParam", "destination not word aligned");
    b3host::store<std::uint32_t>(p, osd_config_param());
    ret(r, 0);
}

// GetOsdConfigParam2(p, n, offset): n zero bytes at p (no extended configuration); v0 0
void get_osd_config_param2(const CallArgs& a, CallResult& r) {
    const b3host::gaddr p = uarg(a, 0);
    const std::uint32_t n = uarg(a, 1);
    if (n > 256u) stub_unexpected("GetOsdConfigParam2", "size " + std::to_string(n) + " over 256 bytes");
    static const std::uint8_t zeros[256] = {};
    if (n) b3host::copy_to_guest(p, zeros, n);
    ret(r, 0);
}

bool dmac_handler_exists(unsigned channel) {
    for (const auto& h : g_kernel.dmac)
        if (h.cause == channel) return true;
    return false;
}

// DIntr / EIntr: 1 when interrupts were enabled before the call, else 0; then off / on
void dintr(const CallArgs&, CallResult& r) {
    b3host::Backend& b = b3host::backend();
    const bool was = b.interrupts_enabled();
    b.set_interrupts_enabled(false);
    ret(r, was ? 1 : 0);
}

void eintr(const CallArgs&, CallResult& r) {
    b3host::Backend& b = b3host::backend();
    const bool was = b.interrupts_enabled();
    ret(r, was ? 1 : 0);
    b.set_interrupts_enabled(true);
}

}  // namespace b3native::platform
