// b3hw/hw.h -- host hardware hooks for port-track C (decided 2026-11-05; docs/decomp-on-host.md).
//
// Retail library code ported as C (src/libs/rw: the sky2 driver; later IOP / pad / CD glue) touches EE hardware:
// GS privileged registers (0x12000000-), DMA / VIF / GIF / INTC / timer registers (0x10000000-), and syncs. The C
// calls these hooks wherever the asm does, in the same order with the same values; decomp2host passes `b3hw_*`
// callees through unchanged (port tracks only). The current b3host backend implements them:
//   - the native backend: a state model (src/native/hw_model) that keeps exactly what the game reads back (CSR, IMR,
//     D_STAT ...), hands the renderer what it needs, and is fatal on any register it does not model;
//   - other backends (tests, the mini): their own slow path (Backend::hw_write / hw_read).
// Proof: native_check / the mini log retail's MMIO accesses (address, width, value, order) and compare them with the
// b3hw trace (b3host::hw_trace), as well as RAM.
#ifndef B3HW_HW_H
#define B3HW_HW_H

#include <cstdint>

extern "C" {
// One hardware store / load of `width` bytes (4, 8 or 16; for 16 the high half in hi) at the physical address.
void b3hw_write(std::uint32_t address, unsigned width, std::uint64_t lo, std::uint64_t hi);
std::uint64_t b3hw_read(std::uint32_t address, unsigned width);
}

inline void b3hw_write32(std::uint32_t a, std::uint32_t v) { b3hw_write(a, 4, v, 0); }
inline void b3hw_write64(std::uint32_t a, std::uint64_t v) { b3hw_write(a, 8, v, 0); }
inline void b3hw_write128(std::uint32_t a, std::uint64_t lo, std::uint64_t hi) { b3hw_write(a, 16, lo, hi); }
inline std::uint32_t b3hw_read32(std::uint32_t a) { return static_cast<std::uint32_t>(b3hw_read(a, 4)); }
inline std::uint64_t b3hw_read64(std::uint32_t a) { return b3hw_read(a, 8); }

// boot2: CPU-side hardware the asm reaches with coprocessor instructions instead of loads / stores.
// - COP2 control registers (`cfc2` / `ctc2` of vi16..vi31, e.g. vi28 = FBRST, the VU reset bits): the same trace and
//   model as memory-mapped registers, at the pseudo address B3HW_COP2_CTRL + register number (outside the EE's
//   physical range; tests/r5900_mini.h reports its cfc2 / ctc2 of those registers to on_mmio the same way).
// - COP0 Status.EIE (`di` / `ei`, `mfc0 Status & 0x10000`): the backend's interrupt-enable state (only the native
//   backend models it); not traced (the mini does not log them either).
constexpr std::uint32_t B3HW_COP2_CTRL = 0xC2000000u;
inline std::uint32_t b3hw_cop2_ctrl_read(unsigned reg) { return static_cast<std::uint32_t>(b3hw_read(B3HW_COP2_CTRL + reg, 4)); }
inline void b3hw_cop2_ctrl_write(unsigned reg, std::uint32_t v) { b3hw_write(B3HW_COP2_CTRL + reg, 4, v, 0); }
extern "C" {
void b3hw_di(void);
void b3hw_ei(void);
void b3hw_idle_wait(void);   // rt1: a busy-wait's iteration found nothing to do (b3host::g_idle_hook; none: no-op)
int b3hw_eie(void);   // 1 when COP0 Status.EIE is set (what `mfc0 $t, Status; and $t, 0x10000` tests)
// audio1: COP0 Count (`mfc0 $t, $9`; RWA's timer, ostimer.o): the EE cycle counter, 294.912 MHz. The native backend
// gives the platform's virtual EE time (docs/audio.md §5: the standalone's timer); the mini its retired count.
std::uint32_t b3hw_cop0_count(void);
}

#endif
