// b3host/ps2float.h -- PS2 floating point for native code: the EE FPU (COP1) and VU0 macro-mode (COP2) arithmetic.
//
// Two build modes, chosen with -DB3_PS2_FLOAT=0|1 (default 0):
//   0  native fast: host IEEE floats (round to nearest), SSE (x86) or NEON (AArch64) for vector ops. Division/sqrt
//      keep the PS2's *safe* results for the cases that would otherwise create Inf/NaN (x/0 -> +-Fmax,
//      sqrt(x<0) -> sqrt|x|), because game code relies on them (e.g. Normalise() of a zero vector stays zero on the PS2: 0 * Fmax = 0, not NaN).
//   1  PS2-faithful: every operation goes through the bit-level model in b3fp::ref below.
//
// The bit-level model is specified in docs/specs/ps2-float.md (§4, bit exact), so a native function using it can
// be compared with tolerance 0.
// The hardware facts it rests on:
//   [C VU User's Manual v6.0 2.1.1 p26] exponent 0 is zero (no denormals); exponent 255 is an ordinary number
//       (x2^128): no Inf/NaN.
//   [C VU manual 2.2 p27] "a 24-bit calculation including hidden bits is performed, and the result is truncated";
//       conversions truncate toward 0. [C EE Core User's Manual v6.0 8.7 p164] the FPU only rounds toward 0 and does
//       not use guard/round/sticky bits, so results "may differ from IEEE 754 rounding to 0 ... usually the LSB".
//   [C VU manual 2.3 p28, EE Core manual 8.5 p162] exceptions clamp: 0/0 and x/0 -> +-MAX, exponent overflow -> +-MAX,
//       underflow -> +-0, sqrt(x<0) -> sqrt(|x|).
// Where the model (like PCSX2's clamp modes) and the manuals differ, faithful mode follows the model -- the port's
// golden runs were recorded with it -- and the difference is listed here and in docs/specs/ps2-float.md §4:
//   D1 exponent-255 operands: the model clamps them to +-Fmax (0x7F7FFFFF) first; the manual treats them as x2^128.
//   D2 "MAX" results: the model gives 0x7F7FFFFF; the EE Core manual's signed-zero table 8-5 p163 lists 0x7FFFFFFF
//      for 0/0 and x/0 on the FPU.
//   D3 add/sub: the model computes the sum in a double, then truncates; the hardware has no guard/sticky bits, so
//      when the exponents differ by more than ~24 the hardware may lose the smaller operand entirely [L, manual 8.7].
//   D4 FPU div.s / sqrt.s / rsqrt.s: the model rounds to nearest in binary32 (only the VU Q unit truncates);
//      the manual says the FPU only truncates. -DB3_PS2_FPU_DIVSQRT_CHOP=1 truncates them instead [G which is right].
// Not modelled: MAC/STATUS/FCR31 flags (no GtMath function reads them).
#ifndef B3HOST_PS2FLOAT_H
#define B3HOST_PS2FLOAT_H

#include "b3host/ps2types.h"
#include "b3host/vubind.h"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>

#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define B3_HAVE_SSE2 1
#else
#define B3_HAVE_SSE2 0
#endif
// mac1: AArch64 (Apple Silicon, Linux arm64) fast mode on NEON, lane for lane the SSE path's results (below).
#if !B3_HAVE_SSE2 && (defined(__ARM_NEON) || defined(__aarch64__))
#include <arm_neon.h>
#define B3_HAVE_NEON 1
#else
#define B3_HAVE_NEON 0
#endif

#ifndef B3_PS2_FLOAT
#define B3_PS2_FLOAT 0
#endif
#ifndef B3_PS2_FPU_DIVSQRT_CHOP
#define B3_PS2_FPU_DIVSQRT_CHOP 0
#endif

namespace b3fp {

inline std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }
inline float flt(std::uint32_t b) { return std::bit_cast<float>(b); }

inline constexpr std::uint32_t kSign = 0x80000000u;
inline constexpr std::uint32_t kExp = 0x7F800000u;
inline constexpr std::uint32_t kFmax = 0x7F7FFFFFu;   // largest IEEE-finite float ("Fmax")

}  // namespace b3fp

// The bit-level reference model (namespace b3fp::ref: clamp, chop, add ... eq): b3host/ps2float_ref.h.
#include "b3host/ps2float_ref.h"

namespace b3fp {
namespace ref {

// ---- the same operations rounded to nearest (port15: the alternative of the rounding switch below) ----
// Clamp-mode-3 operands as above, the exact result rounded to nearest even once (a binary32 sum or product is exact in a
// double up to an exponent difference of 29; past that the nearest result is the larger operand, which the double
// sum's own rounding gives), overflow -> signed Fmax, a denormal result -> signed zero. madd/msub: the product rounded,
// then the sum rounded (two roundings, as the chopped model).
inline std::uint32_t near_finish(double r) {
    const float f = static_cast<float>(r);
    const std::uint32_t b = bits(f);
    if ((b & kExp) == kExp) return (b & kSign) | kFmax;
    if ((b & kExp) == 0) return b & kSign;
    return b;
}
inline std::uint32_t add_rn(std::uint32_t a, std::uint32_t b) {
    return near_finish(static_cast<double>(flt(clamp(a))) + static_cast<double>(flt(clamp(b))));
}
inline std::uint32_t sub_rn(std::uint32_t a, std::uint32_t b) {
    return near_finish(static_cast<double>(flt(clamp(a))) - static_cast<double>(flt(clamp(b))));
}
inline std::uint32_t mul_rn(std::uint32_t a, std::uint32_t b) {
    const std::uint32_t ca = clamp(a), cb = clamp(b);
    if ((ca & kExp) == 0 || (cb & kExp) == 0) return (ca ^ cb) & kSign;   // a zero operand: signed zero, as mul()
    return near_finish(static_cast<double>(flt(ca)) * static_cast<double>(flt(cb)));
}
inline std::uint32_t madd_rn(std::uint32_t acc, std::uint32_t a, std::uint32_t b) { return add_rn(acc, mul_rn(a, b)); }
inline std::uint32_t msub_rn(std::uint32_t acc, std::uint32_t a, std::uint32_t b) { return sub_rn(acc, mul_rn(a, b)); }

}  // namespace ref

// ---------------------------------------------------------------------------------------------------------------
// The rounding switch (port15; docs/specs/ps2-float.md §5). Faithful mode only (B3_PS2_FLOAT=1):
// how the FMAC operations (add, sub, mul, madd, msub) of VU0 macro mode (b3vu) and of the FPU (b3fp below) round.
//   Rounding::Chop     exact result truncated toward zero (ps2-float.md §4.3), the PS2 manuals' rule. The default:
//                      the port's golden runs were recorded with it.
//   Rounding::Nearest  exact result rounded to nearest (port14's reading; kept for tests).
// Process-wide, set once before native code runs with b3fp::set_rounding() (tests). Division, square root, min/max and
// conversions are not affected.
// ---------------------------------------------------------------------------------------------------------------
enum class Rounding : std::uint8_t { Chop, Nearest };
inline constexpr Rounding kDefaultRounding = Rounding::Chop;
struct RoundingModel { Rounding vu0 = kDefaultRounding, cop1 = kDefaultRounding; };
namespace detail {
inline RoundingModel g_rounding{};   // constant-initialised: no environment access in generated code
}  // namespace detail
inline const RoundingModel& rounding() { return detail::g_rounding; }
inline void set_rounding(const RoundingModel& m) { detail::g_rounding = m; }
inline const char* rounding_name(Rounding r) { return r == Rounding::Nearest ? "nearest" : "chop"; }
namespace sw {   // the switched FMAC operations: Chop -> ref::op, Nearest -> ref::op_rn
inline std::uint32_t add(Rounding r, std::uint32_t a, std::uint32_t b) { return r == Rounding::Chop ? ref::add(a, b) : ref::add_rn(a, b); }
inline std::uint32_t sub(Rounding r, std::uint32_t a, std::uint32_t b) { return r == Rounding::Chop ? ref::sub(a, b) : ref::sub_rn(a, b); }
inline std::uint32_t mul(Rounding r, std::uint32_t a, std::uint32_t b) { return r == Rounding::Chop ? ref::mul(a, b) : ref::mul_rn(a, b); }
inline std::uint32_t madd(Rounding r, std::uint32_t c, std::uint32_t a, std::uint32_t b) { return r == Rounding::Chop ? ref::madd(c, a, b) : ref::madd_rn(c, a, b); }
inline std::uint32_t msub(Rounding r, std::uint32_t c, std::uint32_t a, std::uint32_t b) { return r == Rounding::Chop ? ref::msub(c, a, b) : ref::msub_rn(c, a, b); }
}  // namespace sw

// ---------------------------------------------------------------------------------------------------------------
// Scalar FPU operations as GtMath code uses them (one function per COP1 instruction; C++ operator order matters, so
// header bodies spell every operation out).
// ---------------------------------------------------------------------------------------------------------------
#if B3_PS2_FLOAT
inline float add(float a, float b) { return flt(sw::add(rounding().cop1, bits(a), bits(b))); }
inline float sub(float a, float b) { return flt(sw::sub(rounding().cop1, bits(a), bits(b))); }
inline float mul(float a, float b) { return flt(sw::mul(rounding().cop1, bits(a), bits(b))); }
inline float div(float a, float b) { return flt(ref::fpu_div(bits(a), bits(b))); }
inline float sqrt(float a) { return flt(ref::fpu_sqrt(bits(a))); }
inline float rsqrt(float n, float r) { return flt(ref::fpu_rsqrt(bits(n), bits(r))); }
inline float cvt_s_w(std::int32_t i) { return flt(ref::cvt_s_w(i)); }
inline std::int32_t cvt_w_s(float f) { return ref::cvt_w_s(bits(f)); }
inline bool lt(float a, float b) { return ref::lt(bits(a), bits(b)); }
inline bool le(float a, float b) { return ref::le(bits(a), bits(b)); }
inline float max(float a, float b) { return flt(ref::max(bits(a), bits(b))); }
inline float min(float a, float b) { return flt(ref::min(bits(a), bits(b))); }
#else
inline float add(float a, float b) { return a + b; }
inline float sub(float a, float b) { return a - b; }
inline float mul(float a, float b) { return a * b; }
inline float div(float a, float b) {
    if (b == 0.0f) return std::copysign(FLT_MAX, flt(((bits(a) ^ bits(b)) & kSign) | 0x3F800000u));
    return a / b;
}
inline float sqrt(float a) { return std::sqrt(std::fabs(a)); }
inline float rsqrt(float n, float r) {
    if (r == 0.0f) return std::copysign(FLT_MAX, r);
    return n / std::sqrt(std::fabs(r));
}
inline float cvt_s_w(std::int32_t i) { return static_cast<float>(i); }
inline std::int32_t cvt_w_s(float f) {
    if (!(std::fabs(f) < 2147483648.0f)) return f < 0.0f ? std::numeric_limits<std::int32_t>::min() : std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int32_t>(f);
}
inline bool lt(float a, float b) { return a < b; }
inline bool le(float a, float b) { return a <= b; }
inline float max(float a, float b) { return a > b ? a : b; }
inline float min(float a, float b) { return a < b ? a : b; }
#endif
inline float neg(float a) { return flt(bits(a) ^ kSign); }    // flip bit 31 (both modes)
inline float abs(float a) { return flt(bits(a) & ~kSign); }   // clear bit 31 (both modes)

}  // namespace b3fp

// -------------------------------------------------------------------------------------------------------------------
// VU0 macro-mode vector operations on u_long128 (lane i = word i: x, y, z, w).
// Lane masks here are this file's own: X=1, Y=2, Z=4, W=8 (the VU's DEST field orders them the other way round).
// Every op takes the destination's previous value `d`: lanes outside the mask keep it, as the hardware does.
// -------------------------------------------------------------------------------------------------------------------
namespace b3vu {

using Q = u_long128;
enum : unsigned { X = 1, Y = 2, Z = 4, W = 8, XY = 3, XZ = 5, XYZ = 7, XYZW = 15 };

inline Q make(float x, float y, float z, float w) {
    return b3host::make_u128(b3fp::bits(x), b3fp::bits(y), b3fp::bits(z), b3fp::bits(w));
}
inline Q vf0() { return b3host::make_u128(0, 0, 0, 0x3F800000u); }   // [C VU manual] VF00 = (0, 0, 0, 1)
inline float lane(const Q& q, unsigned i) { return b3fp::flt(q.w[i]); }
template <unsigned L> inline Q bc(const Q& a) { return b3host::make_u128(a.w[L], a.w[L], a.w[L], a.w[L]); }
inline Q splat(float f) { const std::uint32_t b = b3fp::bits(f); return b3host::make_u128(b, b, b, b); }
// qmtc2 of a float GPR value: lane x = f. The other lanes of the 128-bit GPR are whatever the register held [G]:
// they are only ever read through the x broadcast.
inline Q from_float(float f) { return splat(f); }

#if B3_PS2_FLOAT
namespace detail {
template <unsigned M, class F> inline Q lanes(const Q& d, F&& f) {
    Q r = d;
    for (unsigned i = 0; i < 4; ++i)
        if (M & (1u << i)) r.w[i] = f(i);
    return r;
}
}  // namespace detail
// VU0 FMAC: rounded as b3fp::rounding().vu0 says (the switch above).
template <unsigned M> inline Q add_raw(const Q& d, const Q& a, const Q& b) { const auto r = b3fp::rounding().vu0; return detail::lanes<M>(d, [&](unsigned i) { return b3fp::sw::add(r, a.w[i], b.w[i]); }); }
template <unsigned M> inline Q sub_raw(const Q& d, const Q& a, const Q& b) { const auto r = b3fp::rounding().vu0; return detail::lanes<M>(d, [&](unsigned i) { return b3fp::sw::sub(r, a.w[i], b.w[i]); }); }
template <unsigned M> inline Q mul_raw(const Q& d, const Q& a, const Q& b) { const auto r = b3fp::rounding().vu0; return detail::lanes<M>(d, [&](unsigned i) { return b3fp::sw::mul(r, a.w[i], b.w[i]); }); }
template <unsigned M> inline Q madd_raw(const Q& d, const Q& acc, const Q& a, const Q& b) { const auto r = b3fp::rounding().vu0; return detail::lanes<M>(d, [&](unsigned i) { return b3fp::sw::madd(r, acc.w[i], a.w[i], b.w[i]); }); }
template <unsigned M> inline Q msub_raw(const Q& d, const Q& acc, const Q& a, const Q& b) { const auto r = b3fp::rounding().vu0; return detail::lanes<M>(d, [&](unsigned i) { return b3fp::sw::msub(r, acc.w[i], a.w[i], b.w[i]); }); }
template <unsigned M> inline Q max_raw(const Q& d, const Q& a, const Q& b) { return detail::lanes<M>(d, [&](unsigned i) { return b3fp::ref::max(a.w[i], b.w[i]); }); }
template <unsigned M> inline Q min_raw(const Q& d, const Q& a, const Q& b) { return detail::lanes<M>(d, [&](unsigned i) { return b3fp::ref::min(a.w[i], b.w[i]); }); }
template <unsigned M> inline Q abs_raw(const Q& d, const Q& a) { return detail::lanes<M>(d, [&](unsigned i) { return a.w[i] & 0x7FFFFFFFu; }); }
inline float q_div(float n, float d) { return b3fp::flt(b3fp::ref::vu_div(b3fp::bits(n), b3fp::bits(d))); }
inline float q_sqrt(float x) { return b3fp::flt(b3fp::ref::vu_sqrt(b3fp::bits(x))); }
inline float q_rsqrt(float n, float r) { return b3fp::flt(b3fp::ref::vu_rsqrt(b3fp::bits(n), b3fp::bits(r))); }
#else
#if B3_HAVE_SSE2
namespace detail {
inline __m128 ld(const Q& q) { return _mm_load_ps(reinterpret_cast<const float*>(q.w)); }
inline Q st(__m128 v) { Q q; _mm_store_ps(reinterpret_cast<float*>(q.w), v); return q; }
template <unsigned M> inline __m128 maskv() {
    return _mm_castsi128_ps(_mm_set_epi32((M & 8) ? -1 : 0, (M & 4) ? -1 : 0, (M & 2) ? -1 : 0, (M & 1) ? -1 : 0));
}
template <unsigned M> inline Q blend(const Q& d, __m128 r) {
    if constexpr (M == 15) return st(r);
    else { const __m128 m = maskv<M>(); return st(_mm_or_ps(_mm_and_ps(m, r), _mm_andnot_ps(m, ld(d)))); }
}
}  // namespace detail
template <unsigned M> inline Q add_raw(const Q& d, const Q& a, const Q& b) { return detail::blend<M>(d, _mm_add_ps(detail::ld(a), detail::ld(b))); }
template <unsigned M> inline Q sub_raw(const Q& d, const Q& a, const Q& b) { return detail::blend<M>(d, _mm_sub_ps(detail::ld(a), detail::ld(b))); }
template <unsigned M> inline Q mul_raw(const Q& d, const Q& a, const Q& b) { return detail::blend<M>(d, _mm_mul_ps(detail::ld(a), detail::ld(b))); }
template <unsigned M> inline Q madd_raw(const Q& d, const Q& acc, const Q& a, const Q& b) { return detail::blend<M>(d, _mm_add_ps(detail::ld(acc), _mm_mul_ps(detail::ld(a), detail::ld(b)))); }
template <unsigned M> inline Q msub_raw(const Q& d, const Q& acc, const Q& a, const Q& b) { return detail::blend<M>(d, _mm_sub_ps(detail::ld(acc), _mm_mul_ps(detail::ld(a), detail::ld(b)))); }
template <unsigned M> inline Q max_raw(const Q& d, const Q& a, const Q& b) { return detail::blend<M>(d, _mm_max_ps(detail::ld(a), detail::ld(b))); }
template <unsigned M> inline Q min_raw(const Q& d, const Q& a, const Q& b) { return detail::blend<M>(d, _mm_min_ps(detail::ld(a), detail::ld(b))); }
template <unsigned M> inline Q abs_raw(const Q& d, const Q& a) { return detail::blend<M>(d, _mm_and_ps(detail::ld(a), _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF)))); }
#elif B3_HAVE_NEON
// The SSE path's results on NEON: fmul / fadd / fsub stay separate under -ffp-contract=off (vmlaq_f32 is avoided
// anyway), max / min as SSE's maxps / minps -- a > b ? a : b and a < b ? a : b, the second operand on NaN or equal
// (+-0) -- through a compare and a select, not fmax / fmin (which return NaN and order the zeros).
namespace detail {
inline float32x4_t ld(const Q& q) { return vld1q_f32(reinterpret_cast<const float*>(q.w)); }
inline Q st(float32x4_t v) { Q q; vst1q_f32(reinterpret_cast<float*>(q.w), v); return q; }
template <unsigned M> inline uint32x4_t maskv() {
    const std::uint32_t m[4] = {(M & 1) ? ~0u : 0u, (M & 2) ? ~0u : 0u, (M & 4) ? ~0u : 0u, (M & 8) ? ~0u : 0u};
    return vld1q_u32(m);
}
template <unsigned M> inline Q blend(const Q& d, float32x4_t r) {
    if constexpr (M == 15) return st(r);
    else return st(vbslq_f32(maskv<M>(), r, ld(d)));
}
}  // namespace detail
template <unsigned M> inline Q add_raw(const Q& d, const Q& a, const Q& b) { return detail::blend<M>(d, vaddq_f32(detail::ld(a), detail::ld(b))); }
template <unsigned M> inline Q sub_raw(const Q& d, const Q& a, const Q& b) { return detail::blend<M>(d, vsubq_f32(detail::ld(a), detail::ld(b))); }
template <unsigned M> inline Q mul_raw(const Q& d, const Q& a, const Q& b) { return detail::blend<M>(d, vmulq_f32(detail::ld(a), detail::ld(b))); }
template <unsigned M> inline Q madd_raw(const Q& d, const Q& acc, const Q& a, const Q& b) { return detail::blend<M>(d, vaddq_f32(detail::ld(acc), vmulq_f32(detail::ld(a), detail::ld(b)))); }
template <unsigned M> inline Q msub_raw(const Q& d, const Q& acc, const Q& a, const Q& b) { return detail::blend<M>(d, vsubq_f32(detail::ld(acc), vmulq_f32(detail::ld(a), detail::ld(b)))); }
template <unsigned M> inline Q max_raw(const Q& d, const Q& a, const Q& b) { const float32x4_t x = detail::ld(a), y = detail::ld(b); return detail::blend<M>(d, vbslq_f32(vcgtq_f32(x, y), x, y)); }
template <unsigned M> inline Q min_raw(const Q& d, const Q& a, const Q& b) { const float32x4_t x = detail::ld(a), y = detail::ld(b); return detail::blend<M>(d, vbslq_f32(vcltq_f32(x, y), x, y)); }
template <unsigned M> inline Q abs_raw(const Q& d, const Q& a) { return detail::blend<M>(d, vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(detail::ld(a)), vdupq_n_u32(0x7FFFFFFFu)))); }
#else
namespace detail {
template <unsigned M, class F> inline Q lanes(const Q& d, F&& f) {
    Q r = d;
    for (unsigned i = 0; i < 4; ++i)
        if (M & (1u << i)) r.w[i] = b3fp::bits(f(i));
    return r;
}
}  // namespace detail
template <unsigned M> inline Q add_raw(const Q& d, const Q& a, const Q& b) { return detail::lanes<M>(d, [&](unsigned i) { return lane(a, i) + lane(b, i); }); }
template <unsigned M> inline Q sub_raw(const Q& d, const Q& a, const Q& b) { return detail::lanes<M>(d, [&](unsigned i) { return lane(a, i) - lane(b, i); }); }
template <unsigned M> inline Q mul_raw(const Q& d, const Q& a, const Q& b) { return detail::lanes<M>(d, [&](unsigned i) { return lane(a, i) * lane(b, i); }); }
template <unsigned M> inline Q madd_raw(const Q& d, const Q& acc, const Q& a, const Q& b) { return detail::lanes<M>(d, [&](unsigned i) { return lane(acc, i) + lane(a, i) * lane(b, i); }); }
template <unsigned M> inline Q msub_raw(const Q& d, const Q& acc, const Q& a, const Q& b) { return detail::lanes<M>(d, [&](unsigned i) { return lane(acc, i) - lane(a, i) * lane(b, i); }); }
template <unsigned M> inline Q max_raw(const Q& d, const Q& a, const Q& b) { return detail::lanes<M>(d, [&](unsigned i) { return std::max(lane(a, i), lane(b, i)); }); }
template <unsigned M> inline Q min_raw(const Q& d, const Q& a, const Q& b) { return detail::lanes<M>(d, [&](unsigned i) { return std::min(lane(a, i), lane(b, i)); }); }
template <unsigned M> inline Q abs_raw(const Q& d, const Q& a) { return detail::lanes<M>(d, [&](unsigned i) { return std::fabs(lane(a, i)); }); }
#endif
// Q unit, fast mode: host IEEE, but the PS2's safe results for zero divisors and negative radicands.
inline float q_div(float n, float d) {
    if (d == 0.0f) return std::copysign(FLT_MAX, b3fp::flt(((b3fp::bits(n) ^ b3fp::bits(d)) & b3fp::kSign) | 0x3F800000u));
    return n / d;
}
inline float q_sqrt(float x) { return std::sqrt(std::fabs(x)); }
inline float q_rsqrt(float n, float r) {
    if (r == 0.0f) return n == 0.0f ? 0.0f : std::copysign(FLT_MAX, b3fp::flt(((b3fp::bits(n) ^ b3fp::bits(r)) & b3fp::kSign) | 0x3F800000u));
    return n / std::sqrt(std::fabs(r));
}
#endif

// The operations themselves: the *_raw kernels above, bound to retail's VU registers under B3_VU_BIND (b3host/vubind.h:
// `d` is then the bound register's previous value, the result is written back to it).
#define B3VU_OP3(name, K)                                                                                             \
    template <unsigned M> B3VU_INLINE Q name(const Q& d, const Q& a, const Q& b) {                                    \
        B3VU_ENTER(K, M, d, &a, &b);                                                                                  \
        const Q r = name##_raw<M>(b3vu_base_, a, b);                                                                  \
        B3VU_LEAVE(r);                                                                                                \
        return r;                                                                                                     \
    }
#define B3VU_OP4(name, K)                                                                                             \
    template <unsigned M> B3VU_INLINE Q name(const Q& d, const Q& acc, const Q& a, const Q& b) {                      \
        B3VU_ENTER(K, M, d, &a, &b);                                                                                  \
        const Q r = name##_raw<M>(b3vu_base_, acc, a, b);                                                             \
        B3VU_LEAVE(r);                                                                                                \
        return r;                                                                                                     \
    }
B3VU_OP3(add, b3host::vubind::kAdd)
B3VU_OP3(sub, b3host::vubind::kSub)
B3VU_OP3(mul, b3host::vubind::kMul)
B3VU_OP4(madd, b3host::vubind::kMadd)
B3VU_OP4(msub, b3host::vubind::kMsub)
B3VU_OP3(max, b3host::vubind::kMax)
B3VU_OP3(min, b3host::vubind::kMin)
#undef B3VU_OP3
#undef B3VU_OP4
template <unsigned M> B3VU_INLINE Q abs(const Q& d, const Q& a) {
    B3VU_ENTER(b3host::vubind::kAbs, M, d, &a, nullptr);
    const Q r = abs_raw<M>(b3vu_base_, a);
    B3VU_LEAVE(r);
    return r;
}

// VOPMULA: ACC.xyz = a.yzx * b.zxy (ACC.w kept). VOPMSUB: d.xyz = ACC - a.yzx * b.zxy (d.w kept).
// [C VU manual OPMULA/OPMSUB: W keeps the destination]
inline Q yzx(const Q& a) { return b3host::make_u128(a.w[1], a.w[2], a.w[0], a.w[3]); }
inline Q zxy(const Q& a) { return b3host::make_u128(a.w[2], a.w[0], a.w[1], a.w[3]); }
B3VU_INLINE Q opmula(const Q& acc, const Q& a, const Q& b) {
    B3VU_ENTER(b3host::vubind::kOpmula, XYZ, acc, &a, &b);
    const Q r = mul_raw<XYZ>(b3vu_base_, yzx(a), zxy(b));
    B3VU_LEAVE(r);
    return r;
}
B3VU_INLINE Q opmsub(const Q& d, const Q& acc, const Q& a, const Q& b) {
    B3VU_ENTER(b3host::vubind::kOpmsub, XYZ, d, &a, &b);
    const Q r = msub_raw<XYZ>(b3vu_base_, acc, yzx(a), zxy(b));
    B3VU_LEAVE(r);
    return r;
}

}  // namespace b3vu

#endif
