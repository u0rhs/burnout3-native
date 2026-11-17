// b3libm/ps2_tan.h -- bit-exact ports of retail's __kernel_tanf and tanf. Method in ps2_core.h. Structure: newlib's
// kf_tan.c / sf_tan.c (fdlibm), checked against the retail asm; constants are retail's bit patterns.
#ifndef B3LIBM_PS2_TAN_H
#define B3LIBM_PS2_TAN_H

#include "b3libm/ps2_basic.h"
#include "b3libm/ps2_trig.h"   // rem_pio2f

namespace b3libm::ps2 {

namespace tan_detail {
// T[0..12] as retail stores them at 0x4B3160 [C ELF] (T[0] 0x3EAAAAAB, T[12] 0x37D95384, ...).
inline constexpr F T[13] = {fb(0x3EAAAAAB), fb(0x3E088889), fb(0x3D5D0DD1), fb(0x3CB327A3), fb(0x3C11371E),
                            fb(0x3B6B6916), fb(0x3ABEDE47), fb(0x3A1A26C7), fb(0x398137B8), fb(0x38A3F445),
                            fb(0x3895C07A), fb(0xB79BAE5E), fb(0x37D95384)};
}  // namespace tan_detail

// __kernel_tanf @ 0x11D028: tan(x + y) on [-pi/4, pi/4], iy = 1 for tan, -1 for -1/tan. Same branch shape as
// newlib's (older) kf_tan.c [C]. pio4 0x3F490FDA, pio4lo 0x33222167 [C 0x11D0EC-0x11D0FC]. The factor 2.0 in the
// |x| >= 0.6744 return is an add.s of the value to itself [C 0x11D21C]. For |x| < 2^-28 with x == 0 and iy == -1 the
// result is 1/fabsf(x) = 1/0, which the PS2 FPU model turns into +MAX (ps2float.h D2) [L: no Inf in this build].
inline F kernel_tanf(F x, F y, int iy) {
    using tan_detail::T;
    B3LIBM_B(200, 0);
    const std::int32_t hx = word(x);
    const std::int32_t ix = hx & 0x7FFFFFFF;
    if (ix < 0x31800000) {                                             // |x| < 2^-28 [C 0x11D048-0x11D05C]
        B3LIBM_B(201, 0);
        if (to_int(x) == 0) {
            if ((ix | (iy + 1)) == 0) { B3LIBM_B(202, 29); return kOne / fabsf(x); }
            if (iy == 1) { B3LIBM_B(203, 26); return x; }
            B3LIBM_B(204, 30);
            return fb(0xBF800000) / x;
        }
    }
    if (ix > 0x3F2CA13F) {                                             // |x| >= 0.6744
        B3LIBM_B(205, 0);
        if (hx < 0) { B3LIBM_B(206, 2); x = -x; y = -y; }
        const F z = fb(0x3F490FDA) - x;
        const F w = fb(0x33222167) - y;
        x = z + w;
        y = kZero;
    }
    const F z = x * x;
    const F w = z * z;
    const F r0 = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
    const F v0 = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
    const F s = z * x;
    F r = y + z * (s * (r0 + v0) + y);
    r = r + T[0] * s;
    const F wv = x + r;
    if (ix > 0x3F2CA13F) {
        B3LIBM_B(207, 100);
        const F v = to_float(iy);
        const F sign = to_float(1 - ((hx >> 30) & 2));
        F d = x - (wv * wv / (wv + v) - r);
        d = d + d;
        return sign * (v - d);
    }
    if (iy == 1) { B3LIBM_B(208, 72); return wv; }
    B3LIBM_B(209, 97);
    // -1/(x+r) computed accurately
    const F zh = from_word(word(wv) & static_cast<std::int32_t>(0xFFFFF000));
    const F v = r - (zh - x);
    const F a = fb(0xBF800000) / wv;
    const F t = from_word(word(a) & static_cast<std::int32_t>(0xFFFFF000));
    const F s2 = kOne + t * zh;
    return t + a * (s2 + t * v);
}

// tanf @ 0x11E220 (newlib sf_tan.c): no Inf/NaN early-out in this build, so every |x| > pi/4 (exponent 255 included)
// goes through __ieee754_rem_pio2f [C 0x11E24C-0x11E268].
inline F tanf(F x) {
    B3LIBM_B(215, 0);
    if ((word(x) & 0x7FFFFFFF) <= 0x3F490FDA) { B3LIBM_B(216, 20); return kernel_tanf(x, kZero, 1); }
    B3LIBM_B(217, 73);
    F y[2];
    const int n = rem_pio2f(x, y);
    return kernel_tanf(y[0], y[1], 1 - ((n & 1) << 1));
}

}  // namespace b3libm::ps2

#endif
