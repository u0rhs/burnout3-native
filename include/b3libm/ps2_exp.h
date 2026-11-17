// b3libm/ps2_exp.h -- bit-exact ports of retail's __ieee754_logf and __ieee754_powf. Method in ps2_core.h. Structure:
// newlib's ef_log.c / ef_pow.c (fdlibm), checked against the retail asm; constants are retail's bit patterns.
#ifndef B3LIBM_PS2_EXP_H
#define B3LIBM_PS2_EXP_H

#include "b3libm/ps2_basic.h"

namespace b3libm::ps2 {

// __ieee754_logf @ 0x11A5C8 (newlib ef_log.c, no-denormal / no-Inf build):
//  - zero (any |x| < 0x00800000 encoding) returns the constant 0xFF7FFFFF (-MAX) from 0x4B2B6C [C 0x11A5EC];
//  - negative x returns (x-x)/0 = 0/0, which the PS2 FPU model turns into +MAX (ps2float.h D2) [C 0x11A600-0x11A610];
//  - no Inf/NaN or subnormal-scaling branch; exponent-255 inputs are ordinary numbers.
// ln2_hi 0x3F317180, ln2_lo 0x3717F7D1, Lg1..Lg7 as loaded at 0x11A720-0x11A79C.
inline F logf(F x) {
    constexpr F ln2_hi = fb(0x3F317180), ln2_lo = fb(0x3717F7D1);
    constexpr F Lg1 = fb(0x3F2AAAAA), Lg2 = fb(0x3ECCCCCC), Lg3 = fb(0x3E924925), Lg4 = fb(0x3E638E29),
                Lg5 = fb(0x3E3A3324), Lg6 = fb(0x3E1CD04E), Lg7 = fb(0x3E178896);
    B3LIBM_B(230, 0);
    std::int32_t ix = word(x);
    if ((ix & 0x7FFFFFFF) <= 0x007FFFFF) { B3LIBM_B(231, 12); return fb(0xFF7FFFFF); }
    if (ix < 0) { B3LIBM_B(232, 19); const F d = x - x; return d / kZero; }
    std::int32_t k = (ix >> 23) - 127;
    ix &= 0x007FFFFF;
    const std::int32_t i = (ix + 0x004AFB20) & 0x00800000;             // 0x95f64 << 3
    x = from_word(ix | (i ^ 0x3F800000));                              // normalize x or x/2
    k += i >> 23;
    const F f = x - kOne;
    if ((0x007FFFFF & (15 + ix)) < 16) {                               // |f| < 2^-20
        B3LIBM_B(233, 0);
        if (f == kZero) {
            if (k == 0) { B3LIBM_B(234, 39); return kZero; }
            B3LIBM_B(235, 49);
            const F dk = to_float(k);
            return dk * ln2_hi + dk * ln2_lo;
        }
        const F R = f * f * (fb(0x3F000000) - fb(0x3EAAAAAA) * f);     // 0.5, 1/3 [C 0x11A6B8-0x11A6D8]
        if (k == 0) { B3LIBM_B(236, 47); return f - R; }
        B3LIBM_B(237, 59);
        const F dk = to_float(k);
        return dk * ln2_hi - ((R - dk * ln2_lo) - f);
    }
    B3LIBM_B(238, 0);
    const F s = f / (fb(0x40000000) + f);
    const F dk = to_float(k);
    const F z = s * s;
    const std::int32_t ii = ix - 0x0030A3D0;                            // 0x6147a << 3
    const F w = z * z;
    const std::int32_t j = 0x0035C288 - ix;                             // 0x6b851 << 3
    const F t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    const F t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    const F R = t2 + t1;
    if ((ii | j) > 0) {
        B3LIBM_B(239, 0);
        const F hfsq = fb(0x3F000000) * f * f;
        if (k == 0) { B3LIBM_B(240, 94); return f - (hfsq - s * (hfsq + R)); }
        B3LIBM_B(241, 104);
        return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
    }
    if (k == 0) { B3LIBM_B(242, 89); return f - s * (f - R); }
    B3LIBM_B(243, 100);
    return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
}

// __ieee754_powf @ 0x11A8A8 (newlib ef_pow.c, no-denormal / no-Inf build, FLT_UWORD_EXP_MAX 0x43010000 and
// FLT_UWORD_EXP_MIN 0x42FC0000 [C 0x11AE20, 0x11AE88]). Differences from newlib's current source:
//  - no NaN / Inf-of-x / Inf-of-y branches; zero tests are |w| < 0x00800000 encodings;
//  - the |y| > 2^27 overflow/underflow returns are folded constants: huge*huge = 0x7F7FFFFF (loaded from 0x4B2BF4,
//    named `one` in our symbol table but holding MAX) and tiny*tiny = +0 [C 0x11AAA8-0x11AAD8];
//  - in that branch the series uses t = x - 1, not ax - 1 (the older newlib text) [C 0x11AAF8: sub.s f12, f20(x)];
//  - the final overflow/underflow returns are computed: s*huge*huge, s*tiny*tiny with huge 0x7149F2C9, tiny 0x0DA2425F.
// Special x (|x| zero encoding or exactly 1.0): z = |x|, 1/z for y < 0, (z-z)/(z-z) for (-1)^non-int, -z for odd y.
inline F powf(F x, F y) {
    constexpr F bp[2] = {fb(0x3F800000), fb(0x3FC00000)};             // 0x4B2B70
    constexpr F dp_h[2] = {fb(0), fb(0x3F15C000)};                    // 0x4B2B78
    constexpr F dp_l[2] = {fb(0), fb(0x35D1CFDC)};                    // 0x4B2B80
    constexpr F L1 = fb(0x3F199999), L2 = fb(0x3EDB6DB7), L3 = fb(0x3EAAAAAB), L4 = fb(0x3E8BA304),
                L5 = fb(0x3E6C3254), L6 = fb(0x3E53F142);             // [C 0x11AC34-0x11AC7C]
    constexpr F P1 = fb(0x3E2AAAAA), P2 = fb(0xBB360B60), P3 = fb(0x388AB354), P4 = fb(0xB5DDEA0E),
                P5 = fb(0x3331BB4B);                                  // [C 0x11AF64-0x11AFB0]
    constexpr F lg2 = fb(0x3F317217), lg2_h = fb(0x3F317200), lg2_l = fb(0x35BFBE8C), ovt = fb(0x3338AA3B);
    constexpr F cp = fb(0x3F76384E), cp_h = fb(0x3F763800), cp_l = fb(0x369DC39F);
    constexpr F ivln2 = fb(0x3FB8AA3A), ivln2_h = fb(0x3FB8AA00), ivln2_l = fb(0x36ECA56F);
    constexpr F two = fb(0x40000000), three = fb(0x40400000), half = fb(0x3F000000);
    constexpr std::int32_t kMask = static_cast<std::int32_t>(0xFFFFF000);
    auto chop = [](F v) { return from_word(word(v) & kMask); };

    B3LIBM_B(260, 0);
    const std::int32_t hx = word(x), hy = word(y);
    std::int32_t ix = hx & 0x7FFFFFFF;
    const std::int32_t iy = hy & 0x7FFFFFFF;
    if (iy <= 0x007FFFFF) { B3LIBM_B(261, 33); return kOne; }           // y == 0

    int yisint = 0;                                                    // 0 non-int, 1 odd, 2 even
    if (hx < 0) {
        B3LIBM_B(262, 8);
        if (iy >= 0x4B800000) { B3LIBM_B(263, -1); yisint = 2; }
        else if (iy >= 0x3F800000) {
            B3LIBM_B(264, 7);
            const std::int32_t k = (iy >> 23) - 0x7F;
            const std::int32_t j = iy >> (23 - k);
            if ((j << (23 - k)) == iy) { B3LIBM_B(265, 4); yisint = 2 - (j & 1); }
        }
    }
    if (iy == 0x3F800000) {                                            // y = +-1
        if (hy < 0) { B3LIBM_B(266, 44); return kOne / x; }
        B3LIBM_B(267, 39);
        return x;
    }
    if (hy == 0x40000000) { B3LIBM_B(268, 42); return x * x; }          // y = 2
    if (hy == 0x3F000000) {                                            // y = 0.5
        B3LIBM_B(269, 2);
        if (hx >= 0) { B3LIBM_B(270, 42); return sqrtf(x); }
    }

    B3LIBM_B(271, 0);
    F ax = fabsf(x);
    bool special = ix <= 0x007FFFFF;
    if (!special) { B3LIBM_B(272, 3); special = ix == 0x3F800000; }
    if (special) {                                                     // x = +-0, +-1
        B3LIBM_B(273, 53);
        F z = ax;
        if (hy < 0) { B3LIBM_B(274, 5); z = kOne / z; }
        if (hx < 0) {
            B3LIBM_B(275, 8);
            if (((ix - 0x3F800000) | yisint) == 0) { B3LIBM_B(276, 3); const F d = z - z; z = d / d; }   // (-1)^non-int
            else if (yisint == 1) { B3LIBM_B(277, 1); z = -z; }
        }
        return z;
    }
    const std::int32_t sx = static_cast<std::int32_t>(static_cast<std::uint32_t>(hx) >> 31);
    if (((sx - 1) | yisint) == 0) { B3LIBM_B(278, 58); const F d = x - x; return d / d; } // (x<0)^non-int: 0/0 -> +MAX in the model

    F t1, t2;
    if (iy > 0x4D000000) {                                             // |y| > 2^27
        B3LIBM_B(279, 0);
        if (ix < 0x3F7FFFF8) {
            if (hy < 0) { B3LIBM_B(280, 65); return fb(0x7F7FFFFF); }
            B3LIBM_B(281, 65);
            return kZero;
        }
        if (ix > 0x3F800007) {
            if (hy > 0) { B3LIBM_B(282, 69); return fb(0x7F7FFFFF); }
            B3LIBM_B(283, 69);
            return kZero;
        }
        B3LIBM_B(284, 0);
        const F t = x - kOne;                                          // x, not ax [C 0x11AAF8]
        const F w = (t * t) * (half - t * (fb(0x3EAAAAAA) - t * fb(0x3E800000)));
        const F u = ivln2_h * t;
        const F v = t * ivln2_l - w * ivln2;
        t1 = chop(u + v);
        t2 = v - (t1 - u);
    } else {
        B3LIBM_B(285, 0);
        std::int32_t n = (ix >> 23) - 0x7F;
        const std::int32_t j = ix & 0x007FFFFF;
        ix = j | 0x3F800000;
        int k;
        if (j <= 0x1CC471) { B3LIBM_B(286, 86); k = 0; }               // |x| < sqrt(3/2)
        else if (j < 0x5DB3D7) { B3LIBM_B(287, 91); k = 1; }           // |x| < sqrt(3)
        else { B3LIBM_B(288, 95); k = 0; n += 1; ix -= 0x00800000; }
        ax = from_word(ix);
        const F u = ax - bp[k];
        const F v = kOne / (ax + bp[k]);
        const F s = u * v;
        const F s_h = chop(s);
        F t_h = from_word(((ix >> 1) | 0x20000000) + 0x00040000 + (k << 21));
        F t_l = ax - (t_h - bp[k]);
        const F s_l = v * ((u - s_h * t_h) - s_h * t_l);
        F s2 = s * s;
        F r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
        r = r + s_l * (s_h + s);
        s2 = s_h * s_h;
        t_h = chop(s2 + three + r);
        t_l = r - ((t_h - three) - s2);
        const F uu = s_h * t_h;
        const F vv = s_l * t_h + t_l * s;
        const F p_h = chop(uu + vv);
        const F p_l = vv - (p_h - uu);
        const F z_h = cp_h * p_h;
        const F z_l = cp_l * p_h + p_l * cp + dp_l[k];
        const F t = to_float(n);
        t1 = chop(((z_h + z_l) + dp_h[k]) + t);
        t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);
    }

    F s = kOne;                                                        // sign of the result
    if (((sx - 1) | (yisint - 1)) == 0) { B3LIBM_B(289, 2); s = fb(0xBF800000); }   // (-x)^odd

    const F y1 = chop(y);
    const F p_l0 = (y - y1) * t1 + y * t2;
    F p_h = y1 * t1;
    F p_l = p_l0;
    F z = p_l + p_h;
    std::int32_t j = word(z);
    const std::int32_t i = j & 0x7FFFFFFF;
    constexpr F huge = kHuge, tiny = kTiny;
    if (j > 0) {
        B3LIBM_B(290, 0);
        if (i > 0x43010000) { B3LIBM_B(291, 138); return s * huge * huge; }   // overflow
        if (i == 0x43010000) {
            B3LIBM_B(292, 0);
            if (p_l + ovt > z - p_h) { B3LIBM_B(293, 148); return s * huge * huge; }
        }
    } else {
        B3LIBM_B(294, 0);
        if (i > 0x42FC0000) { B3LIBM_B(295, 137); return s * tiny * tiny; }   // underflow
        if (i == 0x42FC0000) {
            B3LIBM_B(296, 0);
            if (p_l <= z - p_h) { B3LIBM_B(297, 143); return s * tiny * tiny; }
        }
    }
    B3LIBM_B(298, 0);
    // 2^(p_h + p_l)
    std::int32_t k = (i >> 23) - 0x7F;
    std::int32_t n = 0;
    F tt = z;                                                          // p_l + p_h, recomputed below if p_h moves
    if (i > 0x3F000000) {                                              // |z| > 0.5: n = [z + 0.5]
        B3LIBM_B(299, 24);
        n = j + (0x00800000 >> (k + 1));
        k = ((n & 0x7FFFFFFF) >> 23) - 0x7F;
        const F t = from_word(n & ~(0x007FFFFF >> k));
        n = ((n & 0x007FFFFF) | 0x00800000) >> (23 - k);
        if (j < 0) { B3LIBM_B(300, 0); n = -n; }
        p_h = p_h - t;
        tt = p_l + p_h;
    }
    const F t = chop(tt);
    const F u = t * lg2_h;
    const F v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
    z = u + v;
    const F w = v - (z - u);
    const F tz = z * z;
    const F tq = z - tz * (P1 + tz * (P2 + tz * (P3 + tz * (P4 + tz * P5))));
    const F r = (z * tq) / (tq - two) - (w + z * w);
    z = kOne - (r - z);
    j = word(z);
    j += static_cast<std::int32_t>(static_cast<std::uint32_t>(n) << 23);
    if ((j >> 23) <= 0) { B3LIBM_B(301, 210); z = scalbnf(z, n); }      // [C 0x11B040]
    else { B3LIBM_B(302, 207); z = from_word(j); }
    return s * z;
}

}  // namespace b3libm::ps2

#endif
