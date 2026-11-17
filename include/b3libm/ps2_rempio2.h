// b3libm/ps2_rempio2.h -- bit-exact port of retail's __kernel_rem_pio2f and its two_over_pi table. Method in
// ps2_core.h. Structure: newlib's kf_rem_pio2.c (fdlibm), checked against the retail asm.
#ifndef B3LIBM_PS2_REMPIO2_H
#define B3LIBM_PS2_REMPIO2_H

#include "b3libm/ps2_basic.h"

namespace b3libm::ps2 {

// two_over_pi @ 0x4B2BF8 (198 words, 8 bits each of 2/pi) [C ELF; same digits as newlib's ef_rem_pio2.c table].
inline constexpr std::int32_t kTwoOverPi[198] = {
0xA2, 0xF9, 0x83, 0x6E, 0x4E, 0x44, 0x15, 0x29, 0xFC, 0x27, 0x57, 0xD1, 0xF5, 0x34, 0xDD, 0xC0,
    0xDB, 0x62, 0x95, 0x99, 0x3C, 0x43, 0x90, 0x41, 0xFE, 0x51, 0x63, 0xAB, 0xDE, 0xBB, 0xC5, 0x61,
    0xB7, 0x24, 0x6E, 0x3A, 0x42, 0x4D, 0xD2, 0xE0, 0x06, 0x49, 0x2E, 0xEA, 0x09, 0xD1, 0x92, 0x1C,
    0xFE, 0x1D, 0xEB, 0x1C, 0xB1, 0x29, 0xA7, 0x3E, 0xE8, 0x82, 0x35, 0xF5, 0x2E, 0xBB, 0x44, 0x84,
    0xE9, 0x9C, 0x70, 0x26, 0xB4, 0x5F, 0x7E, 0x41, 0x39, 0x91, 0xD6, 0x39, 0x83, 0x53, 0x39, 0xF4,
    0x9C, 0x84, 0x5F, 0x8B, 0xBD, 0xF9, 0x28, 0x3B, 0x1F, 0xF8, 0x97, 0xFF, 0xDE, 0x05, 0x98, 0x0F,
    0xEF, 0x2F, 0x11, 0x8B, 0x5A, 0x0A, 0x6D, 0x1F, 0x6D, 0x36, 0x7E, 0xCF, 0x27, 0xCB, 0x09, 0xB7,
    0x4F, 0x46, 0x3F, 0x66, 0x9E, 0x5F, 0xEA, 0x2D, 0x75, 0x27, 0xBA, 0xC7, 0xEB, 0xE5, 0xF1, 0x7B,
    0x3D, 0x07, 0x39, 0xF7, 0x8A, 0x52, 0x92, 0xEA, 0x6B, 0xFB, 0x5F, 0xB1, 0x1F, 0x8D, 0x5D, 0x08,
    0x56, 0x03, 0x30, 0x46, 0xFC, 0x7B, 0x6B, 0xAB, 0xF0, 0xCF, 0xBC, 0x20, 0x9A, 0xF4, 0x36, 0x1D,
    0xA9, 0xE3, 0x91, 0x61, 0x5E, 0xE6, 0x1B, 0x08, 0x65, 0x99, 0x85, 0x5F, 0x14, 0xA0, 0x68, 0x40,
    0x8D, 0xFF, 0xD8, 0x80, 0x4D, 0x73, 0x27, 0x31, 0x06, 0x06, 0x15, 0x56, 0xCA, 0x73, 0xA8, 0xC9,
    0x60, 0xE2, 0x7B, 0xC0, 0x8C, 0x6B,
};

namespace rempio2_detail {
// init_jk @ 0x4B3100 has three entries, as in newlib [C ELF]. See kernel_rem_pio2f for prec 3.
inline constexpr int init_jk[3] = {4, 7, 9};
// PIo2 @ 0x4B3110 [C ELF]: several entries are one below newlib's hex (0x37D9FFFF vs 0x37da0000, 0x2E83FFFF vs
// 0x2e840000, 0x2B4FFFFF, 0x27C1FFFF, 0x22CFFFFF, 0x1BC5FFFF): gcc's decimal rounding.
inline constexpr F PIo2[11] = {fb(0x3FC90000), fb(0x39F00000), fb(0x37D9FFFF), fb(0x33A20000), fb(0x2E83FFFF),
                               fb(0x2B4FFFFF), fb(0x27C1FFFF), fb(0x22CFFFFF), fb(0x1FC40000), fb(0x1BC5FFFF),
                               fb(0x17440000)};
inline constexpr F two8 = fb(0x43800000), twon8 = fb(0x3B800000);    // loaded as immediates [C 0x11C804-0x11C814]
}  // namespace rempio2_detail

// __kernel_rem_pio2f @ 0x11C6B8 (retail args: a0 x, a1 y, a2 e0, a3 nx, t0 prec, t1 ipio2). Same branch shape as
// newlib's kf_rem_pio2.c [C]: jv = (e0-3)/8 rounds toward zero via the `e0-3 < 0 ? e0+4` adjust [C 0x11C6B8-0x11C6C8],
// the case-0/1-2/3 switch on prec returns n&7 without touching y for other prec values [C 0x11CCE0-0x11CD18].
// Arrays are 20 entries as in retail's frame (iq @ sp+0, f @ sp+0x50, fq @ sp+0xA0, q @ sp+0xF0); zeroed here (retail
// leaves them uninitialised; prec 0..2 never read an unwritten slot).
// prec must be 0, 1 or 2 (retail only passes 2, from __ieee754_rem_pio2f @ 0x11B3F0). prec 3 is broken in retail:
// init_jk[3] is the next rodata word, 0 [C ELF 0x4B310C], so jk = 0 and the code reads and writes iq[-1] below its
// frame and returns a stale stack word in y[1] [C: oracle run, y[1] differed with the frame's previous contents].
// It is not ported (the newlib case-3 compression is left out).
inline int kernel_rem_pio2f(const F* x, F* y, int e0, int nx, int prec, const std::int32_t* ipio2) {
    using namespace rempio2_detail;
    B3LIBM_B(330, 0);
    std::int32_t iq[20] = {};
    F f[20] = {}, fq[20] = {}, q[20] = {};
    const int jk = init_jk[prec];
    const int jp = jk;
    const int jx = nx - 1;
    if (e0 - 3 < 0) B3LIBM_B(331, 0);
    int jv = (e0 - 3) / 8;
    if (jv < 0) { B3LIBM_B(332, 0); jv = 0; }
    int q0 = e0 - 8 * (jv + 1);

    {
        int j = jv - jx;
        const int m = jx + jk;
        for (int i = 0; i <= m; ++i, ++j) {
            B3LIBM_B(333, 82);
            if (j < 0) { B3LIBM_B(334, -2); f[i] = kZero; }
            else f[i] = to_float(ipio2[j]);
        }
    }
    for (int i = 0; i <= jk; ++i) {
        B3LIBM_B(335, 0);
        F fw = kZero;
        for (int j = 0; j <= jx; ++j) { B3LIBM_B(336, 0); fw = fw + x[j] * f[jx + i - j]; }
        q[i] = fw;
    }

    int jz = jk;
    F z;
    int n, ih;
    for (;;) {                                                         // recompute:
        B3LIBM_B(337, 0);
        // distill q[] into iq[] reversingly
        z = q[jz];
        for (int i = 0, j = jz; j > 0; ++i, --j) {
            B3LIBM_B(338, 0);
            const F fw = to_float(to_int(z * twon8));
            iq[i] = to_int(z - fw * two8);
            z = q[j - 1] + fw;
        }
        // compute n
        z = scalbnf(z, q0);
        z = z - floorf(z * fb(0x3E000000)) * fb(0x41000000);            // z -= 8*floor(z*0.125)
        n = to_int(z);
        z = z - to_float(n);
        ih = 0;
        if (q0 > 0) {
            B3LIBM_B(339, 0);
            const std::int32_t i = iq[jz - 1] >> (8 - q0);
            n += i;
            iq[jz - 1] -= i << (8 - q0);
            ih = iq[jz - 1] >> (7 - q0);
        } else if (q0 == 0) {
            B3LIBM_B(340, -9);
            ih = iq[jz - 1] >> 8;
        } else {
            B3LIBM_B(341, -8);
            if (z >= fb(0x3F000000)) { B3LIBM_B(342, 0); ih = 2; }
        }
        if (ih > 0) {                                                  // q > 0.5
            B3LIBM_B(343, 0);
            n += 1;
            int carry = 0;
            for (int i = 0; i < jz; ++i) {                             // compute 1-q
                B3LIBM_B(344, 0);
                const std::int32_t j = iq[i];
                if (carry == 0) {
                    B3LIBM_B(345, 9);
                    if (j != 0) { B3LIBM_B(346, 0); carry = 1; iq[i] = 0x100 - j; }
                } else {
                    B3LIBM_B(347, 9);
                    iq[i] = 0xFF - j;
                }
            }
            if (q0 == 1) { B3LIBM_B(348, 24); iq[jz - 1] &= 0x7F; }
            else if (q0 == 2) { B3LIBM_B(349, 24); iq[jz - 1] &= 0x3F; }
            else B3LIBM_B(350, 0);
            if (ih == 2) {
                B3LIBM_B(351, 0);
                z = kOne - z;
                if (carry != 0) { B3LIBM_B(352, 22); z = z - scalbnf(kOne, q0); }
            }
        }
        // check if recomputation is needed
        if (z == kZero) {
            B3LIBM_B(353, 0);
            std::int32_t j = 0;
            if (jz - 1 < jk) B3LIBM_B(381, 324);                         // no iteration (also below: zero-trip loops)
            for (int i = jz - 1; i >= jk; --i) { B3LIBM_B(354, 0); j |= iq[i]; }
            if (j == 0) {
                B3LIBM_B(355, 0);
                int k = 1;
                if (iq[jk - k] != 0) B3LIBM_B(382, 0);
                while (iq[jk - k] == 0) { B3LIBM_B(356, 0); ++k; }    // k = no. of terms needed
                for (int i = jz + 1; i <= jz + k; ++i) {               // add q[jz+1] to q[jz+k]
                    B3LIBM_B(357, 0);
                    f[jx + i] = to_float(ipio2[jv + i]);
                    F fw = kZero;
                    for (int jj = 0; jj <= jx; ++jj) { B3LIBM_B(358, 8); fw = fw + x[jj] * f[jx + i - jj]; }
                    q[i] = fw;
                }
                jz += k;
                continue;
            }
        }
        break;
    }

    // chop off zero terms
    if (z == kZero) {
        B3LIBM_B(359, 0);
        jz -= 1;
        q0 -= 8;
        if (iq[jz] != 0) B3LIBM_B(383, -61);
        while (iq[jz] == 0) { B3LIBM_B(360, 0); --jz; q0 -= 8; }
    } else {                                                           // break z into 8-bit if necessary
        B3LIBM_B(361, 0);
        z = scalbnf(z, -q0);
        if (z >= two8) {
            B3LIBM_B(362, 0);                                               // not reached from rem_pio2f's inputs (q0 >= -8,
                                                                       // z < 1) [L: counted over the test sweeps]
            const F fw = to_float(to_int(z * twon8));
            iq[jz] = to_int(z - fw * two8);
            jz += 1;
            q0 += 8;
            iq[jz] = to_int(fw);
        } else {
            iq[jz] = to_int(z);
        }
    }

    // convert integer "bit" chunk to floating-point value
    B3LIBM_B(363, 0);
    F fw = scalbnf(kOne, q0);
    for (int i = jz; i >= 0; --i) {
        B3LIBM_B(364, 0);
        q[i] = fw * to_float(iq[i]);
        fw = fw * twon8;
    }
    // compute PIo2[0,...,jp]*q[jz,...,0]
    for (int i = jz; i >= 0; --i) {
        B3LIBM_B(365, 0);
        fw = kZero;
        int k = 0;
        for (;;) {
            if (k > jp) { B3LIBM_B(366, 0); break; }
            if (k > jz - i) { B3LIBM_B(367, 0); break; }
            B3LIBM_B(368, 0);
            fw = fw + PIo2[k] * q[i + k];
            ++k;
        }
        fq[jz - i] = fw;
    }

    // compress fq[] into y[]
    switch (prec) {
    case 0:
        B3LIBM_B(369, 0);
        fw = kZero;
        for (int i = jz; i >= 0; --i) { B3LIBM_B(370, 0); fw = fw + fq[i]; }
        if (ih == 0) { B3LIBM_B(371, 0); y[0] = fw; } else { B3LIBM_B(372, 0); y[0] = -fw; }
        break;
    case 1:
    case 2:
        B3LIBM_B(373, 0);
        fw = kZero;
        for (int i = jz; i >= 0; --i) { B3LIBM_B(374, 0); fw = fw + fq[i]; }
        if (ih == 0) { B3LIBM_B(375, 0); y[0] = fw; } else { B3LIBM_B(376, 0); y[0] = -fw; }
        fw = fq[0] - fw;
        if (jz < 1) B3LIBM_B(384, 0);
        for (int i = 1; i <= jz; ++i) { B3LIBM_B(377, 0); fw = fw + fq[i]; }
        if (ih == 0) { B3LIBM_B(378, 812); y[1] = fw; } else { B3LIBM_B(379, 817); y[1] = -fw; }
        break;
    default:
        B3LIBM_B(380, 0);
        break;
    }
    return n & 7;
}

}  // namespace b3libm::ps2

#endif
