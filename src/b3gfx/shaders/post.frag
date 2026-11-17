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
//   mode 4  CopyToScreen 0x2649D0: P x intensity added to the frame (ALPHA 0x29 FIX 0x80; RGB only); PC: B-spline upsample
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 2, binding = 1) uniform sampler2D src2;
layout(set = 3, binding = 0) uniform PostUniforms {
    vec4 a;   // x: mode; y: taps per axis (mode 0); z: 1 = B-spline upsample (mode 4)
    vec4 b;   // mode 0: c, k1, k2, k3 (k / 128); mode 2: offset xy (texels); mode 3: w, keep, 1 = reset, 1 = freeze; mode 4: intensity
    vec4 c;   // xy: the destination's texel size (uv); zw: the source's texel size (uv)
};
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
        vec3 q = texture(src, v_uv).rgb, p = texture(src2, v_uv).rgb;
        vec3 r = b.w > 0.5 ? p : b.z > 0.5 ? q : min(b.x * q + b.y * p, vec3(1.0));
        out_color = vec4(r, 1.0);
    } else {
        vec3 p = a.z > 0.5 ? bspline(src, v_uv, c.zw) : texture(src, v_uv).rgb;
        out_color = vec4(p * b.x, 0.0);
    }
}
