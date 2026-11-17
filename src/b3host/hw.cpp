// The b3hw hooks (include/b3hw/hw.h; boot1, the hardware-hook decision): port-track C calls them where the
// retail asm touches EE hardware; the bound backend performs them (native: the platform's model; tests: the backend's
// own slow path). With b3host::set_hw_trace every access is also recorded, in order, for the proof against retail's
// MMIO accesses (the mini's on_mmio log).
#include "b3hw/hw.h"

#include "b3host/host.h"

#include <cstring>

extern "C" void b3hw_write(std::uint32_t address, unsigned width, std::uint64_t lo, std::uint64_t hi) {
    if (width != 4 && width != 8 && width != 16) b3host::fatal("b3hw_write: width must be 4, 8 or 16");
    std::uint8_t d[16];
    std::memcpy(d, &lo, 8);
    std::memcpy(d + 8, &hi, 8);
    if (auto* t = b3host::detail::g_hw_trace) t->push_back({address, width, true, lo, width == 16 ? hi : 0});
    b3host::backend().hw_write(address, width, d);
}

extern "C" std::uint64_t b3hw_read(std::uint32_t address, unsigned width) {
    if (width != 4 && width != 8) b3host::fatal("b3hw_read: width must be 4 or 8");
    std::uint64_t v = 0;
    b3host::backend().hw_read(address, width, &v);
    if (width == 4) v = static_cast<std::uint32_t>(v);
    if (auto* t = b3host::detail::g_hw_trace) t->push_back({address, width, false, v, 0});
    return v;
}

extern "C" void b3hw_di(void) { b3host::backend().set_interrupts_enabled(false); }
extern "C" void b3hw_ei(void) { b3host::backend().set_interrupts_enabled(true); }
extern "C" int b3hw_eie(void) { return b3host::backend().interrupts_enabled() ? 1 : 0; }
// rt1: a busy-wait in port-track code (RW's _rwDMAForceBufferSwap) tells the host it is idle: b3host::g_idle_hook,
// when an interactive host set one (native_play: jump the virtual clock to the next event), else nothing -- proof
// runs (native_boot) never set it, so their timing is unchanged
extern "C" void b3hw_idle_wait(void) {
    if (b3host::g_idle_hook) b3host::g_idle_hook();
}
extern "C" std::uint32_t b3hw_cop0_count(void) { return b3host::backend().cop0_count(); }   // audio1
