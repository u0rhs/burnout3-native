// The game's post passes (post1, docs/native-renderer.md §25): the arithmetic the native renderer and the checks share.
// Decomp citations: HEAD b21a8ccc3e (read only). Tags: [C] matched source / asm, [L] nonmatching drafts read with the asm.
#ifndef B3ASSET_POSTFX_H
#define B3ASSET_POSTFX_H

namespace b3asset {

// CB3Bloom::FirstDownsample 0x2653B0's integer set-up from the threshold (bloom +0x0) [L draft, C asm 0x2653B0..0x265558]:
// c = int(255 x threshold + 0.5) (at most 254), s = 32768 / (255 - c) x 1.05 (D_004E0D88), then three FIX / RGBAQ
// factors k1, k2, k3 = int(s + 0.9999) (D_004E0FAC; at most 255), s x= 128 / k after each, so k1 k2 k3 / 128^3 = s / 128.
// The passes: B = (frame - c) x k1 / 128 (ALPHA_1 0xA4 over a fill of c: COLCLAMP keeps it >= 0), then the 2:1 copy into
// the half-size buffer by RGBAQ k3 (MODULATE) and ALPHA_2 0xA8 FIX k2, each step clamped to 255.
struct BloomGains {
    int c = 0;              // the threshold colour (GS bytes)
    int k1 = 0, k2 = 0, k3 = 0;
};
BloomGains bloom_gains(float threshold);

// The bloom's other constants [C matched Blend 0x264C30 / L drafts of SecondDownsample 0x2651C0, Blur 0x264E60,
// CopyToScreen 0x2649D0]: SecondDownsample's RGBAQ 0x87 (x 135/128), Blur's RGBAQ 0x8A (x 138/128) with ALPHA 0x64 FIX
// 0x54 (84/128 of the shifted copy), Blend's FIX int(118 x feedback) on the kept P (ALPHA 0x29: Cs + Cd x FIX / 128),
// CopyToScreen's RGBAQ int(128 x intensity) (at most 255) added to the frame (ALPHA 0x29 FIX 0x80).
constexpr float kBloomSecondGain = 135.0f / 128.0f;
constexpr float kBloomBlurGain = 138.0f / 128.0f;
constexpr float kBloomBlurMix = 84.0f / 128.0f;
inline int bloom_feedback_fix(float feedback) { return int(118.0f * feedback); }
inline int bloom_intensity_rgb(float intensity) { const int c = int(128.0f * intensity); return c > 255 ? 255 : c; }

// CB3DirectionalBlurBase::UpdateCamera 0x4256C0's motion branch [L nonmatching draft; constants from the asm / ELF]:
// the camera point 10 m ahead from the previous call (`old`, camera space through this call's camera: right, up, at,
// pos rows), its velocity over dt = now - last; amount (|v| - 1) / 49 (0.0204081628), none below 1/127.5 (0.00784313772,
// the call still returns 1), at most 1; the centre = 0.998 v.xy / max(v.z, 0.01) + 0.5 (v flipped when v.z < 0); zoom =
// 1 - 0.012 (0.5 + 0.5 a) / |centre - the far corner| (the corner (0 or 1, 0 or 1) opposite the centre's direction);
// amount x= func_0030A990 when `scale` (flags 4 / 0x20 with the boost switch +0x52).
struct DirBlur {
    bool drawn = false;     // amount set (false: the call returned 1 with amount 0, or dt <= 0)
    float centre[2] = {0.5f, 0.5f};
    float zoom = 0.9999f;
    float amount = 0;
};
DirBlur directional_motion(const float old_point[3], float last, float now, const float cam[4][3], bool scale, float k);

}  // namespace b3asset

#endif
