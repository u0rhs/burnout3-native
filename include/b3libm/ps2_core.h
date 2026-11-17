// b3libm/ps2_core.h -- the scalar float type the bit-exact PS2 libm port is written in.
//
// Retail links newlib's single-precision fdlibm ("LIBM.A ef_acos.o" etc. in the E map, compiled by gcc 2.x), built for
// floats without denormals or Inf/NaN [C: the zero tests are `hx < 0x00800000` and the NaN/Inf early-outs are absent,
// e.g. sinf @ 0x11E138, __ieee754_sqrtf @ 0x11B438]. Every C float operator there is one COP1 instruction
// (add.s/sub.s/mul.s/div.s, c.lt.s/c.le.s/c.eq.s, cvt.w.s/cvt.s.w, neg.s), so the port is written with `F`, whose
// operators *are* those instructions under b3fp::ref (b3host/ps2float.h): the PS2-faithful model
// (docs/specs/ps2-float.md, D1-D4). Each function body follows the retail instruction sequence, not newlib's
// source: gcc baked the decimal constants differently (S1 of __kernel_sinf is 0xBE2AAAAA in retail, 0xbe2aaaab in
// newlib), so every constant here is the bit pattern loaded by the retail code, cited by address.
//
// Verification: tests/test_libm*.cpp run the retail functions in a minimal R5900 interpreter (tests/r5900_mini.h)
// over dense input sweeps and compare bit for bit.
#ifndef B3LIBM_PS2_CORE_H
#define B3LIBM_PS2_CORE_H

#include "b3host/ps2float.h"

#include <cstdint>

namespace b3libm::ps2 {

struct F {
    std::uint32_t b;   // raw binary32 bits
};
constexpr F fb(std::uint32_t bits) { return F{bits}; }

// add.s / sub.s / mul.s: rounded as b3fp's switch says for the FPU (Chop by default, port15).
inline F operator+(F x, F y) { return {b3fp::sw::add(b3fp::rounding().cop1, x.b, y.b)}; }   // add.s
inline F operator-(F x, F y) { return {b3fp::sw::sub(b3fp::rounding().cop1, x.b, y.b)}; }   // sub.s
inline F operator*(F x, F y) { return {b3fp::sw::mul(b3fp::rounding().cop1, x.b, y.b)}; }   // mul.s
inline F operator/(F x, F y) { return {b3fp::ref::fpu_div(x.b, y.b)}; }  // div.s
inline F operator-(F x) { return {x.b ^ 0x80000000u}; }                 // neg.s
inline bool operator<(F x, F y) { return b3fp::ref::lt(x.b, y.b); }     // c.lt.s
inline bool operator<=(F x, F y) { return b3fp::ref::le(x.b, y.b); }    // c.le.s
inline bool operator>(F x, F y) { return b3fp::ref::lt(y.b, x.b); }     // c.lt.s, operands swapped
inline bool operator>=(F x, F y) { return b3fp::ref::le(y.b, x.b); }
inline bool operator==(F x, F y) { return b3fp::ref::eq(x.b, y.b); }    // c.eq.s
inline bool operator!=(F x, F y) { return !b3fp::ref::eq(x.b, y.b); }
inline std::int32_t to_int(F x) { return b3fp::ref::cvt_w_s(x.b); }     // cvt.w.s ((int)x)
inline F to_float(std::int32_t i) { return {b3fp::ref::cvt_s_w(i)}; }   // cvt.s.w ((float)i)
inline std::int32_t word(F x) { return static_cast<std::int32_t>(x.b); } // GET_FLOAT_WORD
inline F from_word(std::int32_t w) { return {static_cast<std::uint32_t>(w)}; }   // SET_FLOAT_WORD

inline F from_float(float f) { return {b3fp::bits(f)}; }
inline float to_host(F x) { return b3fp::flt(x.b); }

// ---- Exact cost: the retail code's dynamic instruction count ----
// A native override must report what the original would have retired (one per executed instruction, delay slots
// included, a nullified branch-likely slot not); then the scheduler's timeline, and with it the game, is unchanged.
// Each function here follows the retail code path for path, so the count is a sum of per-block constants:
// B3LIBM_B(id, n) on a path adds n to `cost`.
// The n were fitted against the retail code run in tests/r5900_mini.h over the port's sweeps (tools/libm_cost/fit.py
// on the rows of build/libm_cost_dump) and are checked by tests/test_libm_cost.cpp: reported cost == retired
// instructions for every sweep input of every override entry. `id` is a block number unique over b3libm, used only by
// the fit (-DB3LIBM_TRACE_BLOCKS counts block hits instead of adding n). -DB3LIBM_NO_COST compiles the accounting out.
inline thread_local std::uint32_t cost = 0;
#if defined(B3LIBM_TRACE_BLOCKS)
inline constexpr int kMaxBlocks = 512;
inline thread_local std::uint32_t block_hits[kMaxBlocks] = {};
#define B3LIBM_B(id, n) (++::b3libm::ps2::block_hits[(id)])
#elif defined(B3LIBM_NO_COST)
#define B3LIBM_B(id, n) ((void)0)
#else
#define B3LIBM_B(id, n) (::b3libm::ps2::cost += (n))
#endif

}  // namespace b3libm::ps2

#endif
