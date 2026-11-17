#version 450
// The bloom's one-draw composite (PC path with MSAA; its own file so post.frag's code, which the golden record's frames
// depend on, stays as it was). The resolved 3D picture (src, read texel-exact) plus the kept picture P (src2, cubic
// B-spline through four bilinear taps) x intensity, drawn as the first thing of the resumed scene pass: no blend, the
// frame alpha kept. a.w = 1: the SOFT screen blend, f + g (1 - f); else the game's own sum f + g (CopyToScreen 0x2649D0).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 2, binding = 1) uniform sampler2D src2;
layout(set = 3, binding = 0) uniform PostUniforms {
    vec4 a;   // w: 1 = soft
    vec4 b;   // x: intensity
    vec4 c;   // zw: P's texel size (uv)
};
vec3 bspline(sampler2D t, vec2 uv, vec2 texel) {
    vec2 p = uv / texel - 0.5;
    vec2 f = fract(p);
    vec2 i = floor(p);
    vec2 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0;
    vec2 w1 = (4.0 - 6.0 * f * f + 3.0 * f * f * f) / 6.0;
    vec2 w2 = (1.0 + 3.0 * f + 3.0 * f * f - 3.0 * f * f * f) / 6.0;
    vec2 w3 = f * f * f / 6.0;
    vec2 g0 = w0 + w1, g1 = w2 + w3;
    vec2 h0 = (i - 0.5 + w1 / g0) * texel;
    vec2 h1 = (i + 1.5 + w3 / g1) * texel;
    return g0.y * (g0.x * texture(t, vec2(h0.x, h0.y)).rgb + g1.x * texture(t, vec2(h1.x, h0.y)).rgb) +
           g1.y * (g0.x * texture(t, vec2(h0.x, h1.y)).rgb + g1.x * texture(t, vec2(h1.x, h1.y)).rgb);
}
void main() {
    vec4 f = texture(src, v_uv);
    vec3 g = bspline(src2, v_uv, c.zw) * b.x;
    out_color = vec4(a.w > 0.5 ? f.rgb + g * (1.0 - f.rgb) : f.rgb + g, f.a);
}
