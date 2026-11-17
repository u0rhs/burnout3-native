// b3libm/ps2_trig.h -- bit-exact ports of retail's sinf, cosf, their kernels, __ieee754_rem_pio2f, acosf and asinf.
// Method in ps2_core.h. Structure: newlib's kf_sin.c, kf_cos.c, ef_rem_pio2.c, sf_sin.c, sf_cos.c, ef_acos.c,
// ef_asin.c (fdlibm), checked against the retail asm; constants are retail's bit patterns (address of the load cited).
#ifndef B3LIBM_PS2_TRIG_H
#define B3LIBM_PS2_TRIG_H

#include "b3libm/ps2_basic.h"

#if __has_include("b3libm/ps2_rempio2.h")
#include "b3libm/ps2_rempio2.h"
#define B3LIBM_HAVE_KERNEL_REM_PIO2F 1
#else
#define B3LIBM_HAVE_KERNEL_REM_PIO2F 0
#include <stdexcept>
#endif

namespace b3libm::ps2 {

inline constexpr F kHalf = fb(0x3F000000);

// __kernel_sinf @ 0x11CF20. Polynomial S1..S6 as retail loads them (each one below newlib's hex).
inline F kernel_sinf(F x, F y, int iy) {
    B3LIBM_B(60, 0);
    const std::int32_t ix = word(x) & 0x7FFFFFFF;
    if (ix < 0x32000000) {
        B3LIBM_B(61, 0);
        if (to_int(x) == 0) { B3LIBM_B(62, 17); return x; }            // [C 0x11CF4C-0x11CF58]
    }
    constexpr F S1 = fb(0xBE2AAAAA), S2 = fb(0x3C088889), S3 = fb(0xB9500D01), S4 = fb(0x3638EF1A),
                S5 = fb(0xB2D72F34), S6 = fb(0x2F2EC9D2);             // [C 0x11CF64-0x11CFD0]
    const F z = x * x;
    const F v = z * x;
    const F r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) { B3LIBM_B(63, 49); return x + v * (S1 + z * r); }
    B3LIBM_B(64, 54);
    return x - ((z * (kHalf * y - v * r) - y) - v * S1);
}

// __kernel_cosf @ 0x11C568.
inline F kernel_cosf(F x, F y) {
    B3LIBM_B(70, 0);
    const std::int32_t ix = word(x) & 0x7FFFFFFF;
    if (ix < 0x32000000) {
        B3LIBM_B(71, 0);
        if (to_int(x) == 0) { B3LIBM_B(72, 22); return kOne; }         // [C 0x11C59C-0x11C5B0]
    }
    constexpr F C1 = fb(0x3D2AAAAA), C2 = fb(0xBAB60B60), C3 = fb(0x37D00D00), C4 = fb(0xB493F27B),
                C5 = fb(0x310F74F5), C6 = fb(0xAD47D74E);             // [C 0x11C5BC-0x11C61C]
    const F z = x * x;
    const F r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3E99999A) { B3LIBM_B(73, 60); return kOne - (kHalf * z - (z * r - x * y)); }
    F qx;
    if (ix > 0x3F480000) { B3LIBM_B(74, 67); qx = fb(0x3E900000); }    // 0.28125 [C 0x11C660-0x11C67C]
    else { B3LIBM_B(75, 69); qx = from_word(ix - 0x01000000); }
    const F hz = kHalf * z - qx;
    const F a = kOne - qx;
    return a - (hz - (z * r - x * y));
}

#if !B3LIBM_HAVE_KERNEL_REM_PIO2F
inline constexpr const std::int32_t* kTwoOverPi = nullptr;
inline int kernel_rem_pio2f(const F*, F*, int, int, int, const std::int32_t*) {
    throw std::logic_error("b3libm: __kernel_rem_pio2f (|x| > 0x43490F80) is not ported in this build");
}
#endif

// npio2_hw[i] = high bits of (i+1)*pi/2, the medium path's cancellation test; read from retail at two_over_pi + 0x318
// (0x4B2F10) [C ELF; identical to fdlibm's table].
inline constexpr std::int32_t kNpio2Hw[32] = {
    0x3FC90F00, 0x40490F00, 0x4096CB00, 0x40C90F00, 0x40FB5300, 0x4116CB00, 0x412FED00, 0x41490F00,
    0x41623100, 0x417B5300, 0x418A3A00, 0x4196CB00, 0x41A35C00, 0x41AFED00, 0x41BC7E00, 0x41C90F00,
    0x41D5A000, 0x41E23100, 0x41EEC200, 0x41FB5300, 0x4203F200, 0x420A3A00, 0x42108300, 0x4216CB00,
    0x421D1400, 0x42235C00, 0x4229A500, 0x422FED00, 0x42363600, 0x423C7E00, 0x4242C700, 0x42490F00,
};

// __ieee754_rem_pio2f @ 0x11B088: x = n*pi/2 + (y[0] + y[1]). No Inf/NaN early-out in this build: exponent-255
// inputs go down the large-argument path.
inline int rem_pio2f(F x, F* y) {
    constexpr F pio2_1 = fb(0x3FC90F80), pio2_1t = fb(0x37354442), pio2_2 = fb(0x373543FF), pio2_2t = fb(0x2E85A308),
                pio2_3 = fb(0x2E85A300), pio2_3t = fb(0x248D3131), invpio2 = fb(0x3F22F984);   // [C 0x11B0F4-0x11B308]
    B3LIBM_B(80, 0);
    const std::int32_t hx = word(x);
    const std::int32_t ix = hx & 0x7FFFFFFF;
    if (ix <= 0x3F490FD8) { B3LIBM_B(81, 0); y[0] = x; y[1] = kZero; return 0; }
    if (ix <= 0x4016CBE3) {                                            // |x| < 3pi/4: one subtraction of pi/2
        B3LIBM_B(82, 0);
        if (hx > 0) {
            B3LIBM_B(83, 0);
            F z = x - pio2_1;
            if ((ix & static_cast<std::int32_t>(0xFFFFFFF0)) != 0x3FC90FD0) {
                B3LIBM_B(84, 0);
                y[0] = z - pio2_1t;
                y[1] = (z - y[0]) - pio2_1t;
            } else {
                B3LIBM_B(85, 3);
                z = z - pio2_2;
                y[0] = z - pio2_2t;
                y[1] = (z - y[0]) - pio2_2t;
            }
            return 1;
        }
        B3LIBM_B(86, 0);
        F z = x + pio2_1;
        if ((ix & static_cast<std::int32_t>(0xFFFFFFF0)) != 0x3FC90FD0) {
            B3LIBM_B(87, 0);
            y[0] = z + pio2_1t;
            y[1] = (z - y[0]) + pio2_1t;
        } else {
            B3LIBM_B(88, 3);
            z = z + pio2_2;
            y[0] = z + pio2_2t;
            y[1] = (z - y[0]) + pio2_2t;
        }
        return -1;
    }
    if (ix <= 0x43490F80) {                                            // medium: |x| ~<= 2^7*pi/2
        B3LIBM_B(89, 0);
        const F t0 = fabsf(x);
        const std::int32_t n = to_int(t0 * invpio2 + kHalf);
        const F fn = to_float(n);
        F r = t0 - fn * pio2_1;
        F w = fn * pio2_1t;
        bool quick = false;
        if (n < 32) {
            B3LIBM_B(90, 7);
            if ((ix & static_cast<std::int32_t>(0xFFFFFF00)) != kNpio2Hw[n - 1]) { B3LIBM_B(91, 0); quick = true; }
        }
        if (quick) {
            y[0] = r - w;
        } else {
            B3LIBM_B(92, 7);
            const std::int32_t j = ix >> 23;
            y[0] = r - w;
            std::int32_t i = j - ((static_cast<std::uint32_t>(word(y[0])) >> 23) & 0xFF);
            if (i > 8) {                                               // second iteration [C 0x11B29C]
                B3LIBM_B(93, 22);
                F t = r;
                w = fn * pio2_2;
                r = t - w;
                w = fn * pio2_2t - ((t - r) - w);
                y[0] = r - w;
                i = j - ((static_cast<std::uint32_t>(word(y[0])) >> 23) & 0xFF);
                if (i > 25) {                                          // third iteration [C 0x11B2F8]
                    B3LIBM_B(94, 15);
                    t = r;
                    w = fn * pio2_3;
                    r = t - w;
                    w = fn * pio2_3t - ((t - r) - w);
                    y[0] = r - w;
                }
            }
        }
        y[1] = (r - y[0]) - w;
        if (hx < 0) { B3LIBM_B(95, 21); y[0] = -y[0]; y[1] = -y[1]; return -n; }
        B3LIBM_B(96, 17);
        return n;
    }
    // large: three 8-bit chunks of x scaled by 2^-e0, then the multi-precision kernel [C 0x11B360-0x11B3F8]
    B3LIBM_B(97, 0);
    const std::int32_t e0 = (ix >> 23) - 134;
    F z = from_word(ix - (e0 << 23));
    F tx[3];
    for (int i = 0; i < 2; ++i) {
        B3LIBM_B(98, 0);
        tx[i] = to_float(to_int(z));
        z = (z - tx[i]) * fb(0x43800000);                              // two8
    }
    tx[2] = z;
    int nx = 3;
    if (!(tx[nx - 1] == kZero)) B3LIBM_B(102, -2);                     // the loop below runs no iteration
    while (tx[nx - 1] == kZero) { B3LIBM_B(99, 0); --nx; }
    const int n = kernel_rem_pio2f(tx, y, e0, nx, 2, kTwoOverPi);
    if (hx < 0) { B3LIBM_B(100, 0); y[0] = -y[0]; y[1] = -y[1]; return -n; }
    B3LIBM_B(101, -7);
    return n;
}

// sinf @ 0x11E138, cosf @ 0x11DE98 (no Inf/NaN early-out in this build).
inline F sinf(F x) {
    B3LIBM_B(110, 0);
    if ((word(x) & 0x7FFFFFFF) <= 0x3F490FD8) { B3LIBM_B(111, 20); return kernel_sinf(x, kZero, 0); }
    F y[2];
    switch (rem_pio2f(x, y) & 3) {
    case 0: B3LIBM_B(112, 76); return kernel_sinf(y[0], y[1], 1);
    case 1: B3LIBM_B(113, 73); return kernel_cosf(y[0], y[1]);
    case 2: B3LIBM_B(114, 78); return -kernel_sinf(y[0], y[1], 1);
    default: B3LIBM_B(115, 78); return -kernel_cosf(y[0], y[1]);
    }
}
inline F cosf(F x) {
    B3LIBM_B(120, 0);
    if ((word(x) & 0x7FFFFFFF) <= 0x3F490FD8) { B3LIBM_B(121, 20); return kernel_cosf(x, kZero); }
    F y[2];
    switch (rem_pio2f(x, y) & 3) {
    case 0: B3LIBM_B(122, 75); return kernel_cosf(y[0], y[1]);
    case 1: B3LIBM_B(123, 75); return -kernel_sinf(y[0], y[1], 1);
    case 2: B3LIBM_B(124, 77); return -kernel_cosf(y[0], y[1]);
    default: B3LIBM_B(125, 77); return kernel_sinf(y[0], y[1], 1);
    }
}

// The rational approximation shared by acosf and asinf: p(z)/q(z) pieces as retail loads them.
namespace detail {
inline constexpr F pS0 = fb(0x3E2AAAAA), pS1 = fb(0xBEA6B08F), pS2 = fb(0x3E4E0AA8), pS3 = fb(0xBD241146),
                   pS4 = fb(0x3A4F7F04), pS5 = fb(0x3811EF08), qS1 = fb(0xC019D138), qS2 = fb(0x4001572C),
                   qS3 = fb(0xBF303360), qS4 = fb(0x3D9DC62D);         // [C acosf 0x119CC4-0x119D58]
inline F asin_p(F z) { return z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5))))); }
inline F asin_q(F z) { return kOne + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4))); }
inline constexpr F kPi = fb(0x40490FDA), kPio2Hi = fb(0x3FC90FDA), kPio2Lo = fb(0x33A22168), kPio4Hi = fb(0x3F490FDA);
}  // namespace detail

// __ieee754_acosf @ 0x119C20 (acosf = func_0011E328, a jump to it). acos(-1) and acos(tiny) return constants gcc
// folded (pi + 2*pio2_lo -> 0x40490FDA, pio2_hi + pio2_lo -> 0x3FC90FDA) [C 0x119C5C, 0x119CA8]; |x| > 1 gives
// (x-x)/(x-x) = 0/0 -> +-MAX under the FPU model.
inline F acosf(F x) {
    using namespace detail;
    B3LIBM_B(130, 0);
    const std::int32_t hx = word(x);
    const std::int32_t ix = hx & 0x7FFFFFFF;
    if (ix == 0x3F800000) {
        if (hx > 0) { B3LIBM_B(131, 20); return kZero; }
        B3LIBM_B(132, 24);
        return kPi;
    }
    if (ix > 0x3F800000) { B3LIBM_B(133, 26); const F d = x - x; return d / d; }
    if (ix < 0x3F000000) {
        B3LIBM_B(134, 0);
        if (ix <= 0x23000000) { B3LIBM_B(135, 30); return kPio2Hi; }
        B3LIBM_B(136, 96);
        const F z = x * x;
        const F r = asin_p(z) / asin_q(z);
        return kPio2Hi - (x - (kPio2Lo - x * r));
    }
    if (hx < 0) {
        B3LIBM_B(137, 100);
        const F z = (kOne + x) * kHalf;
        const F p = asin_p(z), q = asin_q(z);
        const F s = sqrtf(z);
        const F r = p / q;
        const F w = r * s - kPio2Lo;
        const F sw = s + w;
        return kPi - (sw + sw);                                         // 2*(s+w) as add.s [C 0x119EE8]
    }
    B3LIBM_B(138, 104);
    const F z = (kOne - x) * kHalf;
    const F s = sqrtf(z);
    const F df = from_word(word(s) & static_cast<std::int32_t>(0xFFFFF000));
    const F c = (z - df * df) / (s + df);
    const F r = asin_p(z) / asin_q(z);
    const F w = r * s + c;
    const F dw = df + w;
    return dw + dw;                                                     // [C 0x11A028]
}

// __ieee754_asinf @ 0x11A048 (asinf = func_0011E340, a jump to it).
inline F asinf(F x) {
    using namespace detail;
    B3LIBM_B(140, 0);
    const std::int32_t hx = word(x);
    const std::int32_t ix = hx & 0x7FFFFFFF;
    if (ix == 0x3F800000) { B3LIBM_B(141, 31); return x * kPio2Hi + x * kPio2Lo; }
    if (ix > 0x3F800000) { B3LIBM_B(142, 29); const F d = x - x; return d / d; }
    if (ix < 0x3F000000) {
        B3LIBM_B(143, 0);
        if (ix < 0x32000000) {
            B3LIBM_B(144, 0);
            if (kHuge + x > kOne) { B3LIBM_B(145, 41); return x; }
        }
        B3LIBM_B(146, 89);
        const F t = x * x;
        const F w = asin_p(t) / asin_q(t);
        return x + x * w;
    }
    B3LIBM_B(147, 0);
    const F w0 = kOne - fabsf(x);
    const F t0 = w0 * kHalf;
    F p = asin_p(t0), q = asin_q(t0);
    const F s = sqrtf(t0);
    F t;
    if (ix >= 0x3F79999A) {                                             // |x| > 0.975
        B3LIBM_B(148, 0);
        const F w = p / q;
        const F u = s + s * w;
        t = kPio2Hi - ((u + u) - kPio2Lo);
    } else {
        B3LIBM_B(149, 14);
        const F w = from_word(word(s) & static_cast<std::int32_t>(0xFFFFF000));
        const F c = (t0 - w * w) / (s + w);
        const F r = p / q;
        const F s2 = s + s, c2 = c + c, w2 = w + w;
        p = s2 * r - (kPio2Lo - c2);
        q = kPio4Hi - w2;
        t = kPio4Hi - (p - q);
    }
    if (hx > 0) { B3LIBM_B(150, 109); return t; }
    B3LIBM_B(151, 110);
    return -t;
}

}  // namespace b3libm::ps2

#endif
