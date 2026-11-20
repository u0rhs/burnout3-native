#version 450
// The bloom (post1, docs/native-renderer.md §25; CB3Bloom::Render 0x2659C0 [C matched; L drafts of its passes, GS
// registers read from the asm]). Frame units: GS bytes / 255 (the game's frame holds 0..255; the present doubles it).
//   mode 0  FirstDownsample 0x2653B0: the frame's bright part into the half-size buffer H: per frame pixel B = (frame - c)
//           x k1 / 128 (clamped to 0..1: a fill of c, ALPHA 0xA4 FIX k1), then the 2 x 2 average x k3 / 128 (MODULATE)
//           x k2 / 128 (ALPHA 0xA8), each step clamped. The frame is read as the GS holds it: at most 1. Taps: n x n over
//           the H texel's footprint (game quality: the 2 x 2 GS frame pixels; PC: every output pixel)
//   mode 1  SecondDownsample 0x2651C0: H -> the quarter-size Q, 2 x 2 average x 135/128 (RGBAQ 0x87)
//   mode 2  Blur 0x264E60, one of its four passes on Q: Q = Q + (min(1, Q(p + offset) x 138/128) - Q) x 84/128 (ALPHA 0x64
//           FIX 0x54, RGBAQ 0x8A); offsets (+1.5, 0), (-1.5, 0), (0, +1.5), (0, -1.5) texels
//   mode 3  Blend 0x264C30: the kept picture P = min(1, w Q + keep P) (ALPHA 0x29 FIX int(118 x feedback): w = 1, keep =
//           FIX / 128 per render; over n renders keep^n and w = (1 - keep^n) / (1 - keep)), or Q (reset), or P (freeze)
//   mode 4  CopyToScreen 0x2649D0: P x intensity added to the frame (ALPHA 0x29 FIX 0x80; RGB only); PC: B-spline upsample.
//           Also RENDER SCALE's upscale of the 3D picture (z 0 bilinear; 1.1: z 2 the sharp upscale below, b.y its
//           sharpening 0..1); perf4: z 3 its downscale above 100% (box_down below; c.xy the destination's texel size)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 2, binding = 1) uniform sampler2D src2;
layout(set = 3, binding = 0) uniform PostUniforms {
    vec4 a;   // x: mode; y: taps per axis (mode 0); z: 1 = B-spline upsample, 2 = sharp upscale (mode 4)
    vec4 b;   // mode 0: c, k1, k2, k3 (k / 128); mode 2: offset xy (texels); mode 3: w, keep, 1 = reset, 1 = freeze; mode 4: intensity
    vec4 c;   // xy: the destination's texel size (uv); zw: the source's texel size (uv)
};
// NaN / Inf -> 0 (by the bits: a fast-math compiler may fold isnan away). The kept picture P feeds itself frame after
// frame, so one non-finite sample (a degenerate normal upstream) would otherwise stay in it for good
float finite0(float v) { return (floatBitsToUint(v) & 0x7F800000u) == 0x7F800000u ? 0.0 : v; }
vec3 finite0(vec3 v) { return vec3(finite0(v.x), finite0(v.y), finite0(v.z)); }
vec3 bright(vec3 v) { return min(max(min(v, vec3(1.0)) - b.x, vec3(0.0)) * b.y, vec3(1.0)); }
// Cubic B-spline through four bilinear taps (the standard trick): smooth, no ringing
vec3 bspline(sampler2D t, vec2 uv, vec2 texel) {
    vec2 p = uv / texel - 0.5;
    vec2 f = fract(p);
    vec2 i = floor(p);
    vec2 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0;
    vec2 w1 = (4.0 - 6.0 * f * f + 3.0 * f * f * f) / 6.0;
    vec2 w2 = (1.0 + 3.0 * f + 3.0 * f * f - 3.0 * f * f * f) / 6.0;
    vec2 w3 = f * f * f / 6.0;
    vec2 g0 = w0 + w1, g1 = w2 + w3;
    vec2 h0 = (i - 0.5 + w1 / g0) * texel;   // texel centres at (k + 0.5) x texel: taps k = i - 1 + w1 / g0, i + 1 + w3 / g1
    vec2 h1 = (i + 1.5 + w3 / g1) * texel;
    return g0.y * (g0.x * texture(t, vec2(h0.x, h0.y)).rgb + g1.x * texture(t, vec2(h1.x, h0.y)).rgb) +
           g1.y * (g0.x * texture(t, vec2(h0.x, h1.y)).rgb + g1.x * texture(t, vec2(h1.x, h1.y)).rgb);
}
// 1.1 UPSCALING Sharp (the port's own): a Catmull-Rom cubic over the 4 x 4 source texels round the point (16 taps at
// texel centres, so the linear sampler returns each texel as it is), held inside the range of the 2 x 2 texels it
// lies between (no halo past an edge), then sharpened by its difference from the bilinear value, less where the
// 2 x 2 contrast is already high (edges stay clean, flat detail gains)
float crom(float x) {   // Catmull-Rom weight of a tap at distance x
    x = abs(x);
    return x < 1.0 ? 1.5 * x * x * x - 2.5 * x * x + 1.0 : x < 2.0 ? -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0 : 0.0;
}
vec3 sharp_upscale(sampler2D t, vec2 uv, vec2 texel, float amount) {
    vec2 p = uv / texel - 0.5;
    vec2 i = floor(p), f = p - i;
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    vec3 lo = vec3(1e9), hi = vec3(-1e9);
    for (int y = -1; y <= 2; ++y)
        for (int x = -1; x <= 2; ++x) {
            vec3 s = texture(t, (i + vec2(float(x), float(y)) + 0.5) * texel).rgb;
            float w = crom(float(x) - f.x) * crom(float(y) - f.y);
            sum += s * w;
            wsum += w;
            if (x >= 0 && x <= 1 && y >= 0 && y <= 1) { lo = min(lo, s); hi = max(hi, s); }
        }
    vec3 cub = clamp(sum / wsum, lo, hi);
    vec3 lin = texture(t, uv).rgb;
    float contrast = dot(hi - lo, vec3(0.299, 0.587, 0.114));
    float k = amount * (1.0 - smoothstep(0.05, 0.5, contrast));
    return clamp(cub + (cub - lin) * k, lo, hi);
}
// perf4: RENDER SCALE above 100% (supersampling): the output pixel's footprint on the larger picture averaged by area, a box
// filter (each source texel weighted by the share of it the footprint covers: at 200% exactly its 2 x 2 texels, at 150%
// 2 x 2 texels with weights 1, 0.5). Taps at texel centres (the sampler returns each texel as it is); up to 5 x 5 (400%)
vec3 box_down(sampler2D t, vec2 uv, vec2 dst_texel, vec2 src_texel) {
    vec2 lo = (uv - 0.5 * dst_texel) / src_texel, hi = (uv + 0.5 * dst_texel) / src_texel;
    vec2 i0 = floor(lo);
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (int y = 0; y < 5; ++y) {
        float y0 = i0.y + float(y);
        float wy = min(hi.y, y0 + 1.0) - max(lo.y, y0);
        if (wy <= 0.0) continue;
        for (int x = 0; x < 5; ++x) {
            float x0 = i0.x + float(x);
            float wx = min(hi.x, x0 + 1.0) - max(lo.x, x0);
            if (wx <= 0.0) continue;
            sum += texture(t, (vec2(x0, y0) + 0.5) * src_texel).rgb * (wx * wy);
            wsum += wx * wy;
        }
    }
    return wsum > 0.0 ? sum / wsum : texture(t, uv).rgb;
}
void main() {
    int mode = int(a.x + 0.5);
    if (mode == 0) {
        int n = int(a.y + 0.5);
        vec3 s = vec3(0.0);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                vec2 o = (vec2(float(x), float(y)) + 0.5) / float(n) - 0.5;
                s += bright(texture(src, v_uv + o * c.xy).rgb);
            }
        s /= float(n * n);
        out_color = vec4(min(min(s * b.w, vec3(1.0)) * b.z, vec3(1.0)), 1.0);
    } else if (mode == 1) {
        out_color = vec4(min(texture(src, v_uv).rgb * (135.0 / 128.0), vec3(1.0)), 1.0);
    } else if (mode == 2) {
        vec3 h = texture(src, v_uv).rgb;
        vec3 s = min(texture(src, v_uv + b.xy * c.zw).rgb * (138.0 / 128.0), vec3(1.0));
        out_color = vec4(min(h + (s - h) * (84.0 / 128.0), vec3(1.0)), 1.0);
    } else if (mode == 3) {
        vec3 q = finite0(texture(src, v_uv).rgb), p = finite0(texture(src2, v_uv).rgb);
        vec3 r = b.w > 0.5 ? p : b.z > 0.5 ? q : min(b.x * q + b.y * p, vec3(1.0));
        out_color = vec4(finite0(r), 1.0);
    } else {
        vec3 p = a.z > 2.5 ? box_down(src, v_uv, c.xy, c.zw) : a.z > 1.5 ? sharp_upscale(src, v_uv, c.zw, b.y)
               : a.z > 0.5 ? bspline(src, v_uv, c.zw) : texture(src, v_uv).rgb;
        out_color = vec4(p * b.x, 0.0);
    }
}
