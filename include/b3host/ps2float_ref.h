// include/b3host/ps2float_ref.h -- namespace b3fp::ref: the bit-level PS2 float model (clamp ... eq), written from
// docs/specs/ps2-float.md. Every operation takes and returns raw binary32 encodings.
//
// The model in short (section numbers are the spec's):
//   §4.1 clamp: an exponent-0 operand is the signed zero, an exponent-255 operand the signed Fmax (D1);
//   §4.2 chop:  a binary64 value narrowed toward zero, results below 2^-126 flushed to the signed zero, overflow -> Fmax;
//   §4.3 add / sub = chop of the binary64 sum of the clamped operands (D3: rounded in binary64 first), mul = chop of the
//        exact product, madd / msub = add / sub of the chopped product (not fused);
//   §4.4 VU0 Q unit: chop of the binary64 quotient / root; EE FPU: binary32 round to nearest (D4) unless
//        B3_PS2_FPU_DIVSQRT_CHOP=1; zero divisors give the signed Fmax (D2);
//   §4.5 conversions truncate; §4.6 compares on clamped values, min / max a total order on the raw encodings.
// Hot path: header-only, no out-of-line calls; the operand clamps and the narrowing compile to selects.
#ifndef B3HOST_PS2FLOAT_REF_H
#define B3HOST_PS2FLOAT_REF_H

#ifndef B3HOST_PS2FLOAT_H
#error "include b3host/ps2float.h, not b3host/ps2float_ref.h"
#endif

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace b3fp {

namespace ref {

// §4.1: exponent 0 -> signed zero, exponent 255 -> signed Fmax, otherwise unchanged.
constexpr std::uint32_t clamp(std::uint32_t b) {
    const std::uint32_t e = b & kExp;
    const std::uint32_t s = b & kSign;
    return e == 0 ? s : (e == kExp ? (s | kFmax) : b);
}

// §4.2: round toward zero to binary32 by truncating the binary64 encoding (independent of the host rounding mode).
// On the magnitude bits: below 2^-126 (zeros, binary64 denormals, binary32 denormal values) -> the signed zero; from
// 2^128 up (and Inf) -> the signed Fmax; otherwise rebias the exponent (1023 - 127 = 896) and drop the low 29 fraction
// bits, which leaves the binary32 exponent and fraction fields in bits 30..0. (Values in (FLT_MAX, 2^128) truncate to
// Fmax by themselves.)
inline std::uint32_t chop(double exact) {
    const std::uint64_t x = std::bit_cast<std::uint64_t>(exact);
    const std::uint32_t s = static_cast<std::uint32_t>(x >> 32) & kSign;
    const std::uint64_t mag = x & 0x7FFFFFFFFFFFFFFFull;
    constexpr std::uint64_t kMin = 0x3810000000000000ull;    // 2^-126
    constexpr std::uint64_t kOver = 0x47F0000000000000ull;   // 2^128
    constexpr std::uint64_t kRebias = 0x3800000000000000ull; // 896 << 52
    const std::uint32_t v = static_cast<std::uint32_t>((mag - kRebias) >> 29);
    return s | (mag < kMin ? 0u : (mag >= kOver ? kFmax : v));
}

namespace detail {
inline double val(std::uint32_t b) { return static_cast<double>(flt(clamp(b))); }   // clamped operand, exactly
}  // namespace detail

// §4.3 FMAC operations.
inline std::uint32_t add(std::uint32_t a, std::uint32_t b) { return chop(detail::val(a) + detail::val(b)); }
inline std::uint32_t sub(std::uint32_t a, std::uint32_t b) { return chop(detail::val(a) - detail::val(b)); }
// The product of two 24-bit significands is exact in binary64 (and 2^-252 .. 2^256 stays in range), so one binary64
// multiply is the exact product; a zero operand gives the zero with the XOR of the signs.
inline std::uint32_t mul(std::uint32_t a_raw, std::uint32_t b_raw) { return chop(detail::val(a_raw) * detail::val(b_raw)); }
inline std::uint32_t madd(std::uint32_t acc, std::uint32_t a, std::uint32_t b) { return add(acc, mul(a, b)); }
inline std::uint32_t msub(std::uint32_t acc, std::uint32_t a, std::uint32_t b) { return sub(acc, mul(a, b)); }

// §4.6 min / max: total order on the raw encodings. key() maps an encoding to a signed integer in that order: a
// non-negative encoding keeps its value, a negative one becomes -(magnitude field) - 1 (so -0 -> -1 < +0 -> 0).
namespace detail {
constexpr std::int32_t key(std::uint32_t b) {
    const std::int32_t i = static_cast<std::int32_t>(b);
    return i ^ ((i >> 31) & 0x7FFFFFFF);
}
}  // namespace detail
constexpr std::uint32_t min(std::uint32_t a, std::uint32_t b) { return detail::key(a) < detail::key(b) ? a : b; }
constexpr std::uint32_t max(std::uint32_t a, std::uint32_t b) { return detail::key(a) > detail::key(b) ? a : b; }

// §4.4 the VU0 FDIV unit (Q): binary64 quotient / root, then chop.
inline std::uint32_t vu_div(std::uint32_t n_raw, std::uint32_t d_raw) {
    if ((clamp(d_raw) & ~kSign) == 0) return ((n_raw ^ d_raw) & kSign) | kFmax;   // x/0 and 0/0
    return chop(detail::val(n_raw) / detail::val(d_raw));
}
inline std::uint32_t vu_sqrt(std::uint32_t x_raw) {
    return chop(std::sqrt(static_cast<double>(flt(clamp(x_raw) & ~kSign))));   // sqrt|x|; a zero gives +0
}
inline std::uint32_t vu_rsqrt(std::uint32_t n_raw, std::uint32_t r_raw) {
    if ((clamp(r_raw) & ~kSign) == 0)   // x/sqrt(0) -> signed Fmax, 0/sqrt(0) -> signed zero
        return ((n_raw ^ r_raw) & kSign) | ((clamp(n_raw) & ~kSign) != 0 ? kFmax : 0u);
    return vu_div(n_raw, vu_sqrt(r_raw));   // two truncations
}

// §4.4 the EE FPU (COP1). Special cases look at the *raw* exponent of the divisor / radicand.
inline std::uint32_t fpu_div(std::uint32_t n_raw, std::uint32_t d_raw) {
    if ((d_raw & kExp) == 0) return ((n_raw ^ d_raw) & kSign) | kFmax;
#if B3_PS2_FPU_DIVSQRT_CHOP
    return clamp(chop(detail::val(n_raw) / detail::val(d_raw)));
#else
    const float q = flt(clamp(n_raw)) / flt(clamp(d_raw));   // binary32 round to nearest (D4)
    return clamp(bits(q));                                     // Inf -> Fmax, denormal -> signed zero
#endif
}
inline std::uint32_t fpu_sqrt(std::uint32_t x_raw) {
    if ((x_raw & kExp) == 0) return x_raw & kSign;   // sqrt(+-0) = +-0 (also a denormal encoding)
#if B3_PS2_FPU_DIVSQRT_CHOP
    return clamp(chop(std::sqrt(static_cast<double>(flt(clamp(x_raw) & ~kSign)))));
#else
    return bits(std::sqrt(flt(clamp(x_raw) & ~kSign)));   // a normal radicand has a normal root
#endif
}
inline std::uint32_t fpu_rsqrt(std::uint32_t n_raw, std::uint32_t r_raw) {
    if ((r_raw & kExp) == 0) return (r_raw & kSign) | kFmax;   // the sign of r alone; n not looked at
#if B3_PS2_FPU_DIVSQRT_CHOP
    return clamp(chop(detail::val(n_raw) / std::sqrt(static_cast<double>(flt(clamp(r_raw) & ~kSign)))));
#else
    const float t = std::sqrt(flt(clamp(r_raw) & ~kSign));
    const float q = flt(clamp(n_raw)) / t;
    return clamp(bits(q));
#endif
}

// §4.5 conversions.
inline std::uint32_t cvt_s_w(std::int32_t i) { return chop(static_cast<double>(i)); }   // exact in binary64, truncated
inline std::int32_t cvt_w_s(std::uint32_t b) {
    if (((b >> 23) & 0xFFu) <= 157u) return static_cast<std::int32_t>(flt(b));   // |x| < 2^31: truncation (denormal -> 0)
    return (b & kSign) ? std::numeric_limits<std::int32_t>::min() : std::numeric_limits<std::int32_t>::max();
}

// §4.6 compares on the clamped values (no NaN can occur; -0 == +0).
inline bool lt(std::uint32_t a, std::uint32_t b) { return flt(clamp(a)) < flt(clamp(b)); }
inline bool le(std::uint32_t a, std::uint32_t b) { return flt(clamp(a)) <= flt(clamp(b)); }
inline bool eq(std::uint32_t a, std::uint32_t b) { return flt(clamp(a)) == flt(clamp(b)); }

}  // namespace ref

}  // namespace b3fp

#endif
