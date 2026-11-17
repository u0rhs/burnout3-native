// b3libm/ps2_basic.h -- bit-exact ports of retail's libm helpers: fabsf, copysignf, scalbnf, floorf, ceilf,
// __ieee754_sqrtf, fabs, ceil (double). See ps2_core.h for the method; addresses are retail SLUS_210.50.
#ifndef B3LIBM_PS2_BASIC_H
#define B3LIBM_PS2_BASIC_H

#include "b3libm/ps2_core.h"

#include <cmath>
#include <cstring>

namespace b3libm::ps2 {

inline constexpr F kHuge = fb(0x7149F2C9);   // 1e30 as retail has it (newlib 0x7149f2ca) [C floorf @ 0x11DFC8]
inline constexpr F kTiny = fb(0x0DA2425F);   // 1e-30 (newlib 0x0da24260) [C scalbnf @ 0x11E110]
inline constexpr F kZero = fb(0);
inline constexpr F kOne = fb(0x3F800000);

// fabsf @ 0x11DF70, copysignf @ 0x11DE58: bit operations.
inline F fabsf(F x) { B3LIBM_B(1, 10); return fb(x.b & 0x7FFFFFFFu); }
inline F copysignf(F x, F y) { B3LIBM_B(2, 15); return fb((x.b & 0x7FFFFFFFu) | (y.b & 0x80000000u)); }

// scalbnf @ 0x11E070 (newlib sf_scalbn.c, no-denormal build: zero is hx < 0x00800000, no Inf/NaN case).
inline F scalbnf(F x, int n) {
    B3LIBM_B(3, 0);
    const std::int32_t ix = word(x);
    const std::int32_t hx = ix & 0x7FFFFFFF;
    if (hx <= 0x007FFFFF) { B3LIBM_B(4, 18); return x; }              // zero (or denormal encoding): x
    const std::int32_t k = (hx >> 23) + n;
    if (k > 0xFF) { B3LIBM_B(5, 29); return copysignf(kHuge, x) * kHuge; }   // overflow  [C 0x11E0A8-0x11E0C4]
    if (k > 0) { B3LIBM_B(6, 31); return from_word((ix & static_cast<std::int32_t>(0x807FFFFF)) | (k << 23)); }
    if (n > 50000) { B3LIBM_B(7, 0); return copysignf(kHuge, x) * kHuge; }  // [C 0x11E0F0]
    B3LIBM_B(8, 32);
    return copysignf(kTiny, x) * kTiny;                               // underflow
}

// floorf @ 0x11DF98 / ceilf @ 0x11DD80 (newlib sf_floor.c / sf_ceil.c; |x| >= 2^23 and exponent 255 return x).
inline F floorf(F x) {
    B3LIBM_B(10, 0);
    std::int32_t i0 = word(x);
    const std::int32_t ix = i0 & 0x7FFFFFFF;
    const std::int32_t j0 = (ix >> 23) - 0x7F;
    if (j0 >= 23) { B3LIBM_B(11, 12); return x; }
    if (j0 < 0) {
        B3LIBM_B(12, 0);
        if (x + kHuge > kZero) {                                      // c.lt.s 0, huge+x (the inexact probe)
            B3LIBM_B(13, 0);
            if (i0 >= 0) { B3LIBM_B(14, 0); i0 = 0; }
            else if (ix > 0x007FFFFF) { B3LIBM_B(15, 2); i0 = static_cast<std::int32_t>(0xBF800000); }
            else B3LIBM_B(16, 3);
        }
    } else {
        B3LIBM_B(17, 0);
        const std::int32_t i = 0x007FFFFF >> j0;
        if ((i0 & i) == 0) { B3LIBM_B(18, 19); return x; }
        if (x + kHuge > kZero) {
            B3LIBM_B(19, 5);
            if (i0 < 0) { B3LIBM_B(20, 4); i0 += 0x00800000 >> j0; }
            i0 &= ~i;
        }
    }
    B3LIBM_B(21, 27);
    return from_word(i0);
}
inline F ceilf(F x) {
    B3LIBM_B(30, 0);
    std::int32_t i0 = word(x);
    const std::int32_t ix = i0 & 0x7FFFFFFF;
    const std::int32_t j0 = (ix >> 23) - 0x7F;
    if (j0 >= 23) { B3LIBM_B(31, 12); return x; }
    if (j0 < 0) {
        B3LIBM_B(32, 0);
        if (x + kHuge > kZero) {
            B3LIBM_B(33, 0);
            if (i0 < 0) { B3LIBM_B(34, 0); i0 = static_cast<std::int32_t>(0x80000000); }
            else if (ix > 0x007FFFFF) { B3LIBM_B(35, 2); i0 = 0x3F800000; }
            else B3LIBM_B(36, 3);
        }
    } else {
        B3LIBM_B(37, 0);
        const std::int32_t i = 0x007FFFFF >> j0;
        if ((i0 & i) == 0) { B3LIBM_B(38, 19); return x; }
        if (x + kHuge > kZero) {
            B3LIBM_B(39, 5);
            if (i0 > 0) { B3LIBM_B(40, 4); i0 += 0x00800000 >> j0; }
            i0 &= ~i;
        }
    }
    B3LIBM_B(41, 27);
    return from_word(i0);
}

// __ieee754_sqrtf @ 0x11B438: integer bit-by-bit square root. This build rounds with `q += q & 1` when a remainder is
// left (no floating-point rounding probe) [C 0x11B4D8-0x11B4E0]; zero/denormal encodings return x, negatives
// (x-x)/(x-x) = 0/0, which the PS2 FPU model turns into +-MAX (ps2float.h D2).
inline F sqrtf(F x) {
    B3LIBM_B(50, 0);
    const std::int32_t ix0 = word(x);
    if ((ix0 & 0x7FFFFFFF) <= 0x007FFFFF) { B3LIBM_B(51, 12); return x; }
    if (ix0 < 0) { B3LIBM_B(52, 18); const F d = x - x; return d / d; }
    B3LIBM_B(53, 184);
    std::int32_t m = (ix0 >> 23) - 127;
    std::uint32_t ix = (static_cast<std::uint32_t>(ix0) & 0x007FFFFFu) | 0x00800000u;
    if (m & 1) B3LIBM_B(54, 0);
    ix <<= (m & 1);
    m >>= 1;
    ix <<= 1;
    std::uint32_t q = 0, s = 0, r = 0x01000000u;
    while (r != 0) {
        B3LIBM_B(55, 0);
        const std::uint32_t t = s + r;
        if (!(static_cast<std::int32_t>(ix) < static_cast<std::int32_t>(t))) {   // slt (signed)
            B3LIBM_B(56, 3);
            ix -= t;
            s = t + r;
            q += r;
        }
        r >>= 1;
        ix <<= 1;
    }
    if (ix != 0) { B3LIBM_B(57, 1); q += q & 1; }
    return from_word(static_cast<std::int32_t>((q >> 1) + 0x3F000000u + (static_cast<std::uint32_t>(m) << 23)));
}

// fabs @ 0x11D630 and ceil @ 0x11D2B0 (double, soft-float). fabs clears the sign bit. ceil is fdlibm's integer
// algorithm; its only floating operations are the inexact probes `huge + x > 0` through dpadd/dpcmp, which are true
// for every finite x it reaches, so the result is the exactly rounded-up integer: the host's std::ceil on the same
// bits [L: fdlibm s_ceil.c structure; the probe's soft-float result is not modelled].
inline double fabs(double x) {
    std::uint64_t b;
    std::memcpy(&b, &x, 8);
    b &= 0x7FFFFFFFFFFFFFFFull;
    std::memcpy(&x, &b, 8);
    return x;
}
inline double ceil(double x) { return std::ceil(x); }

}  // namespace b3libm::ps2

#endif
