// b3port/libm_overrides.h -- the first native overrides: retail's PS2 libm leaves, from the bit-exact port in
// include/b3libm (docs/ps2-libm.md), behind the backend-neutral b3host::Override record (b3host/call.h).
//
// Each entry replaces one original function. Addresses and names are the decomp's: config/symbol_addrs_b3names.txt
// (sinf = 0x0011E138, __ieee754_acosf = 0x00119C20, ...) and, for the five unnamed wrappers func_0011E328..388 (a
// 16-byte frame, then `j __ieee754_*f`), the asm labels in assembly/asm/runtime/libm.s:8018-8068 [C]. code_bytes is
// the decomp's function size (`nonmatching <name>, <size>` in libm.s) and code_fnv1a64 the FNV-1a 64 of those bytes in
// orig/SLUS_210.50 (tests/test_overrides.cpp recomputes both from the ELF).
//
// The wrappers here take and return raw binary32 values: host `float` is only the carrier (bits in, bits out, no
// host arithmetic), so exponent-255 and denormal encodings pass through untouched, as in the EE's FPRs.
//
// Cost (2026-10-31, exact): every wrapper reports, through b3host::add_cost, the exact number of instructions the
// original retires on that call (callees included; delay slots are instructions, a nullified
// branch-likely slot is not), from the per-block constants of the bit-exact port (include/b3libm/ps2_core.h "Exact
// cost"; tests/test_libm_cost.cpp checks reported == retired on every sweep input of every entry). The wrappers at
// func_0011E328..388 add their own 5 instructions (addiu, sd, ld, j, addiu) [C libm.s:8018-8023]. A bound backend is
// required (native_play binds the native backend; tests bind a NullBackend).
//
// ceiling (X-list column 5): an upper bound of that count, for a dispatcher with limits (one that falls back to the
// original when a scheduler or step-budget boundary may fall inside this window, so the bound must hold; a larger one
// only means more fallbacks). The native table ignores it (src/native/native_table.cpp). It is the
// largest count over the sweeps + cost extras (2026-10-31) x 1.25, rounded up to 10; for the entries through
// __ieee754_sqrtf at least its analytic maximum (260: all 25 `q += r` steps taken) plus the caller's path [L].
// Sweep maxima: sinf 2736, cosf 2735, tanf 2768, atanf 107, floorf/ceilf 36, fabsf 10, copysignf 15, scalbnf 47,
// __ieee754_acosf 355, __ieee754_asinf 385, __ieee754_atan2f 182, __ieee754_logf 104, __ieee754_powf 360,
// __ieee754_sqrtf 257, __kernel_sinf 54, __kernel_cosf 69, __kernel_tanf 102 (wrappers: target + 5).
#ifndef B3PORT_LIBM_OVERRIDES_H
#define B3PORT_LIBM_OVERRIDES_H

#include "b3host/call.h"
#include "b3libm/ps2libm.h"

#include <cstdint>
#include <span>

namespace b3port::libm {

namespace detail {
using b3libm::ps2::F;
inline F in(float x) { return b3libm::ps2::from_float(x); }
// Runs one ported function and reports its exact cost; Own = the entry's own instructions before the ported code.
template <std::uint32_t Own = 0, class Fn>
inline float counted(Fn&& fn) {
    b3libm::ps2::cost = 0;
    const F r = fn();
    b3host::add_cost(Own + b3libm::ps2::cost);
    return b3libm::ps2::to_host(r);
}
inline constexpr std::uint32_t kWrapper = 5;   // func_0011E328..388 [C libm.s:8018-8023]
}  // namespace detail

namespace ps2 = b3libm::ps2;
using detail::counted;
using detail::in;
using detail::kWrapper;

// One wrapper per entry point, with the retail C signature (float args in f12.., ints in a0..; result in f0).
inline float sinf(float x) { return counted([&] { return ps2::sinf(in(x)); }); }
inline float cosf(float x) { return counted([&] { return ps2::cosf(in(x)); }); }
inline float tanf(float x) { return counted([&] { return ps2::tanf(in(x)); }); }
inline float atanf(float x) { return counted([&] { return ps2::atanf(in(x)); }); }
inline float floorf(float x) { return counted([&] { return ps2::floorf(in(x)); }); }
inline float ceilf(float x) { return counted([&] { return ps2::ceilf(in(x)); }); }
inline float fabsf(float x) { return counted([&] { return ps2::fabsf(in(x)); }); }
inline float copysignf(float x, float y) { return counted([&] { return ps2::copysignf(in(x), in(y)); }); }
inline float scalbnf(float x, int n) { return counted([&] { return ps2::scalbnf(in(x), n); }); }
inline float acosf(float x) { return counted([&] { return ps2::acosf(in(x)); }); }
inline float asinf(float x) { return counted([&] { return ps2::asinf(in(x)); }); }
inline float atan2f(float y, float x) { return counted([&] { return ps2::atan2f(in(y), in(x)); }); }
inline float logf(float x) { return counted([&] { return ps2::logf(in(x)); }); }
inline float powf(float x, float y) { return counted([&] { return ps2::powf(in(x), in(y)); }); }
inline float sqrtf(float x) { return counted([&] { return ps2::sqrtf(in(x)); }); }
inline float kernel_sinf(float x, float y, int iy) { return counted([&] { return ps2::kernel_sinf(in(x), in(y), iy); }); }
inline float kernel_cosf(float x, float y) { return counted([&] { return ps2::kernel_cosf(in(x), in(y)); }); }
inline float kernel_tanf(float x, float y, int iy) { return counted([&] { return ps2::kernel_tanf(in(x), in(y), iy); }); }
// The five wrappers (a frame, then `j __ieee754_*f`): their own 5 instructions + the target's.
inline float func_0011E328(float x) { return counted<kWrapper>([&] { return ps2::acosf(in(x)); }); }
inline float func_0011E340(float x) { return counted<kWrapper>([&] { return ps2::asinf(in(x)); }); }
inline float func_0011E358(float y, float x) { return counted<kWrapper>([&] { return ps2::atan2f(in(y), in(x)); }); }
inline float func_0011E370(float x) { return counted<kWrapper>([&] { return ps2::logf(in(x)); }); }
inline float func_0011E388(float x, float y) { return counted<kWrapper>([&] { return ps2::powf(in(x), in(y)); }); }

}  // namespace b3port::libm

// X(name, address, code_bytes, code_fnv1a64, ceiling, wrapper). The per-call cost is dynamic (reported by the
// wrapper through b3host::add_cost); `ceiling` bounds it (see the header comment).
#define B3PORT_LIBM_OVERRIDES(X)                                                                           \
    X("sinf", 0x0011E138u, 232u, 0x59ca10455e13aba9ull, 3420u, b3port::libm::sinf)                        \
    X("cosf", 0x0011DE98u, 212u, 0xd6fec836b80ae05bull, 3420u, b3port::libm::cosf)                        \
    X("tanf", 0x0011E220u, 120u, 0xc7c32674ffd616daull, 3460u, b3port::libm::tanf)                        \
    X("atanf", 0x0011DAF0u, 652u, 0x4a2aab3e58ee1a46ull, 140u, b3port::libm::atanf)                       \
    X("floorf", 0x0011DF98u, 216u, 0xad132b6d08c9b2b3ull, 50u, b3port::libm::floorf)                      \
    X("ceilf", 0x0011DD80u, 216u, 0x42825a4b4754a012ull, 50u, b3port::libm::ceilf)                        \
    X("fabsf", 0x0011DF70u, 40u, 0xf357fd3c941c1abfull, 20u, b3port::libm::fabsf)                         \
    X("copysignf", 0x0011DE58u, 60u, 0x34eaada2b1f2c6ceull, 20u, b3port::libm::copysignf)                 \
    X("scalbnf", 0x0011E070u, 200u, 0x6ef91743682c52bcull, 60u, b3port::libm::scalbnf)                    \
    X("func_0011E328", 0x0011E328u, 20u, 0x1c7d4b074cbb1255ull, 450u, b3port::libm::func_0011E328)        \
    X("__ieee754_acosf", 0x00119C20u, 1060u, 0x3fe2a9f2f39d0aadull, 450u, b3port::libm::acosf)           \
    X("func_0011E340", 0x0011E340u, 20u, 0xe5315c669bb3af36ull, 490u, b3port::libm::func_0011E340)        \
    X("__ieee754_asinf", 0x0011A048u, 928u, 0x671d520521a8e111ull, 490u, b3port::libm::asinf)            \
    X("func_0011E358", 0x0011E358u, 20u, 0x04a13065d81cbd1eull, 240u, b3port::libm::func_0011E358)        \
    X("__ieee754_atan2f", 0x0011A3E8u, 480u, 0x7bb3583ed019deecull, 230u, b3port::libm::atan2f)          \
    X("func_0011E370", 0x0011E370u, 20u, 0x4a5ca5adf4e7916dull, 140u, b3port::libm::func_0011E370)        \
    X("__ieee754_logf", 0x0011A5C8u, 736u, 0x0b7a3f240004020bull, 130u, b3port::libm::logf)              \
    X("func_0011E388", 0x0011E388u, 20u, 0x56e7ced3a1b10494ull, 460u, b3port::libm::func_0011E388)        \
    X("__ieee754_powf", 0x0011A8A8u, 2016u, 0xa91866b070629e71ull, 450u, b3port::libm::powf)             \
    X("__ieee754_sqrtf", 0x0011B438u, 204u, 0xe6f25877087756c2ull, 330u, b3port::libm::sqrtf)            \
    X("__kernel_sinf", 0x0011CF20u, 260u, 0x35fc8ae07ae05635ull, 70u, b3port::libm::kernel_sinf)          \
    X("__kernel_cosf", 0x0011C568u, 336u, 0x74d522548123e372ull, 90u, b3port::libm::kernel_cosf)          \
    X("__kernel_tanf", 0x0011D028u, 644u, 0x44723442fa715a23ull, 130u, b3port::libm::kernel_tanf)

namespace b3port {

// The table as backend-neutral records (b3host::native_entry thunks).
std::span<const b3host::Override> libm_overrides();

}  // namespace b3port

#endif
