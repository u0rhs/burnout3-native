// b3libm/ps2_atan.h -- bit-exact ports of retail's atanf and __ieee754_atan2f. Method in ps2_core.h. Structure:
// newlib's sf_atan.c / ef_atan2.c (fdlibm), checked against the retail asm; constants are retail's bit patterns.
#ifndef B3LIBM_PS2_ATAN_H
#define B3LIBM_PS2_ATAN_H

#include "b3libm/ps2_basic.h"

namespace b3libm::ps2 {

namespace atan_detail {
// atanhi/atanlo/aT as retail stores them at 0x4B31F0 / 0x4B3200 / 0x4B3210 [C ELF]. Several differ from newlib's hex
// comments by one ulp (atanhi[1] 0x3F490FDA vs 0x3f490fdb, aT[1] 0xBE4CCCCC vs 0xbe4ccccd, ...): gcc's decimal rounding.
inline constexpr F atanhi[4] = {fb(0x3EED6338), fb(0x3F490FDA), fb(0x3F7B985D), fb(0x3FC90FDA)};
inline constexpr F atanlo[4] = {fb(0x31AC3769), fb(0x33222167), fb(0x33140FB4), fb(0x33A22168)};
inline constexpr F aT[11] = {fb(0x3EAAAAAB), fb(0xBE4CCCCC), fb(0x3E124924), fb(0xBDE38E38), fb(0x3DBA2E6E), fb(0xBD9D8795),
                             fb(0x3D886B34), fb(0xBD6EF16A), fb(0x3D4BDA59), fb(0xBD15A221), fb(0x3C8569D7)};
}  // namespace atan_detail

// atanf @ 0x11DAF0 (newlib sf_atan.c). No NaN test in the |x| >= 2^34 branch: every such x (exponent 255 included)
// returns +-(atanhi[3] + atanlo[3]) [C 0x11DB18-0x11DB54]. The |x| < 2^-29 probe `huge + x > one` returns x; when it
// fails (never for finite inputs under the model) the code falls into the polynomial with id = -1, as newlib does.
inline F atanf(F x) {
    using namespace atan_detail;
    B3LIBM_B(160, 0);
    const std::int32_t hx = word(x);
    const std::int32_t ix = hx & 0x7FFFFFFF;
    if (ix > 0x507FFFFF) {                                             // |x| >= 2^34
        if (hx > 0) { B3LIBM_B(161, 25); return atanhi[3] + atanlo[3]; }   // blez [C 0x11DB24]
        B3LIBM_B(162, 26);
        return -atanhi[3] - atanlo[3];
    }
    int id;
    if (ix < 0x3EE00000) {                                             // |x| < 7/16
        B3LIBM_B(163, 0);
        if (ix < 0x31000000) {
            B3LIBM_B(164, 0);
            if (x + kHuge > kOne) { B3LIBM_B(165, 36); return x; }      // [C 0x11DB7C-0x11DB98]
        }
        id = -1;
    } else {
        B3LIBM_B(166, 0);
        x = fabsf(x);
        if (ix < 0x3F980000) {                                         // |x| < 19/16
            if (ix < 0x3F300000) { B3LIBM_B(167, 0); id = 0; x = (x + x - kOne) / (fb(0x40000000) + x); }   // 7/16 <= |x| < 11/16
            else { B3LIBM_B(168, -3); id = 1; x = (x - kOne) / (x + kOne); }   // 11/16 <= |x| < 19/16
        } else {
            if (ix < 0x401C0000) { B3LIBM_B(169, -1); id = 2; x = (x - fb(0x3FC00000)) / (kOne + x * fb(0x3FC00000)); }   // < 39/16
            else { B3LIBM_B(170, -7); id = 3; x = fb(0xBF800000) / x; }
        }
    }
    const F z = x * x;
    const F w = z * z;
    // s1 = z*(aT0+w*(aT2+...)), s2 = w*(aT1+w*(aT3+...)); retail evaluates the two chains interleaved [C 0x11DC90]
    const F s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    const F s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) { B3LIBM_B(171, 67); return x - x * (s1 + s2); }
    const F r = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    if (hx < 0) { B3LIBM_B(172, 97); return -r; }
    B3LIBM_B(173, 96);
    return r;
}

// __ieee754_atan2f @ 0x11A3E8 (the atan2f wrapper func_0011E358 tail-jumps here); y = f12, x = f13 (newlib ef_atan2.c).
// This build has no NaN or Inf cases, and gcc folded the `+-tiny` terms: pi + tiny is retail's pi 0x40490FDA,
// pi_o_2 + tiny and pi_o_2 + 0.5*pi_lo are both 0x3FC90FDA [C 0x11A44C, 0x11A4B4, 0x11A4C8]. pi_lo is 0x34222168
// (newlib's older value; current newlib has -8.74e-8 = 0xb3bbbd2e) [C 0x11A570]. Zero is an encoding test
// (|w| < 0x00800000), so denormal encodings count as zero and y = 0 returns y's own bits.
inline F atan2f(F y, F x) {
    constexpr F pi = fb(0x40490FDA), pi_o_2 = fb(0x3FC90FDA), pi_lo = fb(0x34222168);
    B3LIBM_B(180, 0);
    const std::int32_t hx = word(x), ix = hx & 0x7FFFFFFF;
    const std::int32_t hy = word(y), iy = hy & 0x7FFFFFFF;
    if (hx == 0x3F800000) { B3LIBM_B(181, 16); return atanf(y); }       // x == 1.0 [C 0x11A410]
    const int m = static_cast<int>((static_cast<std::uint32_t>(hy) >> 31) | ((hx >> 30) & 2));
    if (iy <= 0x007FFFFF) {                                            // y == 0
        if (m == 2) { B3LIBM_B(182, 30); return pi; }
        if (m == 3) { B3LIBM_B(183, 36); return -pi; }                  // 0xC0490FDA
        B3LIBM_B(184, 35);
        return y;                                                      // m = 0, 1
    }
    if (ix <= 0x007FFFFF) {                                            // x == 0 [C 0x11A4A0-0x11A4BC]
        if (hy < 0) { B3LIBM_B(185, 34); return -pi_o_2; }
        B3LIBM_B(186, 37);
        return pi_o_2;
    }
    const std::int32_t k = (iy - ix) >> 23;
    F z;
    if (k > 60) { B3LIBM_B(187, 0); z = pi_o_2; }                      // |y/x| > 2^60
    else {
        bool tiny = false;
        if (hx < 0) {
            B3LIBM_B(188, 3);
            if (k < -60) { B3LIBM_B(189, 2); tiny = true; }            // |y|/x < -2^-60
        }
        if (tiny) z = kZero;
        else { B3LIBM_B(190, 11); z = atanf(fabsf(y / x)); }
    }
    switch (m) {
    case 0: B3LIBM_B(191, 41); return z;
    case 1: B3LIBM_B(192, 45); return fb(z.b ^ 0x80000000u);           // sign flip through the stack [C 0x11A550]
    case 2: B3LIBM_B(193, 51); return pi - (z - pi_lo);
    default: B3LIBM_B(194, 52); return (z - pi_lo) - pi;
    }
}

}  // namespace b3libm::ps2

#endif
