// mpeg_dsp.h -- the two pieces of MPEG arithmetic the native movie decoder (mpeg2_decoder.cpp, the shipped game's) and
// the dev IPU model (ipu_model.h, retail libmpeg's oracle in mpeg_play / test_ipu_model) share, so the movies' pixels
// are the console's by construction: the integer IDCT and the IPU's YCbCr -> RGB colour space conversion.
//
// IDCT: Chen-Wang, 11-bit constants W_k = 2048 sqrt(2) cos(k pi / 16); rows, then columns, the output saturated to
// [-256, 255] [C 13818-2 7.5]. Exact enough for IEEE 1180 (test_ipu_model checks it against the double-precision
// definition). The IPU's own rounding is not documented [G: pixels within IDCT rounding of the console's].
// CSC: the EE User's Manual 8.6.1 steps [C]: chroma terms in s11.1 (1/128 units, low 6 bits zero), Y scaled the same,
// rounded off bit 0 and clipped to u8; alpha from SETTH's thresholds (all of R, G, B below th0: black, alpha 0;
// below th1: 0x40; else 0x80).
#ifndef B3NATIVE_MPEG_DSP_H
#define B3NATIVE_MPEG_DSP_H

#include <algorithm>
#include <cstdint>

namespace b3native::mpeg_dsp {

inline void idct(std::int16_t* blk) {
    constexpr int W1 = 2841, W2 = 2676, W3 = 2408, W5 = 1609, W6 = 1108, W7 = 565;
    int t[64];
    for (int r = 0; r < 8; ++r) {
        const std::int16_t* b = blk + 8 * r;
        int* o = t + 8 * r;
        int x1 = b[4] * 2048, x2 = b[6], x3 = b[2], x4 = b[1], x5 = b[7], x6 = b[5], x7 = b[3];
        if (!(x1 | x2 | x3 | x4 | x5 | x6 | x7)) {
            for (int k = 0; k < 8; ++k) o[k] = b[0] * 8;
            continue;
        }
        int x0 = b[0] * 2048 + 128;
        int x8 = W7 * (x4 + x5);
        x4 = x8 + (W1 - W7) * x4; x5 = x8 - (W1 + W7) * x5;
        x8 = W3 * (x6 + x7);
        x6 = x8 - (W3 - W5) * x6; x7 = x8 - (W3 + W5) * x7;
        x8 = x0 + x1; x0 -= x1;
        x1 = W6 * (x3 + x2);
        x2 = x1 - (W2 + W6) * x2; x3 = x1 + (W2 - W6) * x3;
        x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
        x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
        x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
        o[0] = (x7 + x1) >> 8; o[1] = (x3 + x2) >> 8; o[2] = (x0 + x4) >> 8; o[3] = (x8 + x6) >> 8;
        o[4] = (x8 - x6) >> 8; o[5] = (x0 - x4) >> 8; o[6] = (x3 - x2) >> 8; o[7] = (x7 - x1) >> 8;
    }
    auto clip = [](int v) { return static_cast<std::int16_t>(std::clamp(v, -256, 255)); };
    for (int c = 0; c < 8; ++c) {
        const int* b = t + c;
        std::int16_t* o = blk + c;
        int x1 = b[32] * 256, x2 = b[48], x3 = b[16], x4 = b[8], x5 = b[56], x6 = b[40], x7 = b[24];
        if (!(x1 | x2 | x3 | x4 | x5 | x6 | x7)) {
            const std::int16_t v = clip((b[0] + 32) >> 6);
            for (int k = 0; k < 8; ++k) o[8 * k] = v;
            continue;
        }
        int x0 = b[0] * 256 + 8192;
        int x8 = W7 * (x4 + x5) + 4;
        x4 = (x8 + (W1 - W7) * x4) >> 3; x5 = (x8 - (W1 + W7) * x5) >> 3;
        x8 = W3 * (x6 + x7) + 4;
        x6 = (x8 - (W3 - W5) * x6) >> 3; x7 = (x8 - (W3 + W5) * x7) >> 3;
        x8 = x0 + x1; x0 -= x1;
        x1 = W6 * (x3 + x2) + 4;
        x2 = (x1 - (W2 + W6) * x2) >> 3; x3 = (x1 + (W2 - W6) * x3) >> 3;
        x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
        x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
        x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
        o[0] = clip((x7 + x1) >> 14); o[8] = clip((x3 + x2) >> 14); o[16] = clip((x0 + x4) >> 14);
        o[24] = clip((x8 + x6) >> 14); o[32] = clip((x8 - x6) >> 14); o[40] = clip((x0 - x4) >> 14);
        o[48] = clip((x3 - x2) >> 14); o[56] = clip((x7 - x1) >> 14);
    }
}

// CSC: the chroma terms of one Cb / Cr sample (each serves four pixels), then one pixel's RGBA
struct CscChroma {
    int cr4, cr5, cb4, cb5;
};
inline CscChroma csc_chroma(int cb, int cr) {
    const int cr1 = cr - 128, cb1 = cb - 128;
    return {(0x0CC * cr1) & ~0x3F, (0x068 * cr1) & ~0x3F, (0x032 * cb1) & ~0x3F, (0x102 * cb1) & ~0x3F};
}
inline void csc_pixel(int y, const CscChroma& c, int th0, int th1, std::uint8_t* rgba) {
    const int y3 = (0x095 * (y - 16)) & ~0x3F;
    auto fin = [](int v) {   // s12.1 (in 1/128 units, low 6 bits zero): round off bit 0, clip to u8
        v >>= 6;
        v = (v + 1) >> 1;
        return v < 0 ? 0 : v > 255 ? 255 : v;
    };
    int R = fin(y3 + c.cr4), G = fin(y3 - c.cb4 - c.cr5), B = fin(y3 + c.cb5), A;
    if (R < th0 && G < th0 && B < th0) { R = G = B = 0; A = 0; }
    else if (R < th1 && G < th1 && B < th1) A = 0x40;
    else A = 0x80;
    rgba[0] = static_cast<std::uint8_t>(R);
    rgba[1] = static_cast<std::uint8_t>(G);
    rgba[2] = static_cast<std::uint8_t>(B);
    rgba[3] = static_cast<std::uint8_t>(A);
}

}  // namespace b3native::mpeg_dsp

#endif
