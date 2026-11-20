#version 450
// b3gfx night mode's light fixtures (nightlights, docs/remaster-features.md §17.2; night only, a remaster feature off by
// default): a track draw tagged DrawItem::night_emissive (a tunnel's lamp strips and panels, by texture name) drawn once
// more after the grade and the night sky over mesh.vert, depth GEQUAL against its own first draw (reverse-Z), not
// written. Only its bright texels (brightest channel above ~0.6, so coloured neon counts) take their pre-grade colour back (`src`, the copy night.frag
// grades; x `a.x`), alpha-blended over the frame (RGB only), so the tube glows and reaches the bloom while the lamp's
// housing and the ceiling around it stay as graded.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_normal;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D tex;   // the draw's texture
layout(set = 2, binding = 1) uniform sampler2D src;   // the pre-grade frame (single-sample)
layout(set = 3, binding = 0) uniform EmissiveUniforms {
    vec4 a;   // x: the glow's gain over the pre-grade colour, y / z: the luminance ramp (from, to), w: the alpha test's reference (0: none)
};
void main() {
    vec4 t = texture(tex, v_uv);
    if (a.w > 0.0 && t.a < a.w) discard;
    // the brightest channel, not the luminance: a coloured neon tube (a saturated red or blue) is as lit as a white one
    float l = max(t.r, max(t.g, t.b));
    float k = smoothstep(a.y, a.z, l);
    if (k <= 1e-3) discard;
    out_color = vec4(texelFetch(src, ivec2(gl_FragCoord.xy), 0).rgb * a.x, k);
}
