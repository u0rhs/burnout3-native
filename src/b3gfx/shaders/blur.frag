#version 450
// The zoom blurs (docs/native-renderer.md §21.3 radial, §25 directional). Both work in a buffer H (the GS's is half the
// frame; post1's PC quality uses the output size):
// - radial (water1; CB3RadialBlur::Render 0x2FA9A0 [L drafts, GS registers C asm]): Downsample 0x2FA770 copies the frame
//   into H (TEX0 MODULATE with SetRenderstates' RGBAQ 0x88: x 136/128, clamped), SetAlpha 0x2FA490 writes
//   "radialblurmask" into H's alpha (u = |x - centre| / radius x, v = (y - centre) / radius y below the centre, above it 0
//   or mirrored with the flip), BlurOut 0x2FA130 three times blends H with itself sampled towards the centre (s = c +
//   z (p - c), z then z^2, z^4; ALPHA 0x64 FIX 0x40 = 50 %, RGB only, the 0x88 modulation again), CopyToScreen 0x2F9F50
//   blends H over the frame by As = H's alpha x the amount (ALPHA 0x44, RGBAQ = colour x 128, amount x 128; RGB only).
// - directional (post1; CB3DirectionalBlur::Render 0x426C10 [L drafts; C SetRenderstates 0x426B70 / BlurSetRenderstates
//   0x426920 matched]): the same chain with RGBAQ 0x80 (x 1), no mask, the passes BlurOut (zoom), BlurIn (1 / zoom^2),
//   BlurOut (zoom^4), and CopyToScreen 0x426110 lerping the frame towards H by FIX = amount x 128 (ALPHA 0x64).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 2, binding = 1) uniform sampler2D mask;
layout(set = 3, binding = 0) uniform BlurUniforms {
    vec4 a;   // x: mode (0 downsample (+ mask), 1 blur pass, 2 composite), y: amount, z: colour, w: 1 = flip
    vec4 b;   // centre xy (fractions of the view, y down), radius xy
    vec4 c;   // xy: this pass's zoom towards the centre; z: the RGBAQ gain (136/128 radial, 1 directional); w: 1 = mask
};
void main() {
    int mode = int(a.x + 0.5);
    if (mode == 0) {
        vec2 d = v_uv - b.xy;
        float vv = d.y > 0.0 ? d.y / b.w : (a.w > 0.5 ? -d.y / b.w : 0.0);
        float m = c.w > 0.5 ? texture(mask, vec2(abs(d.x) / b.z, vv)).a : 1.0;
        out_color = vec4(min(texture(src, v_uv).rgb * c.z, vec3(1.0)), m);
    } else if (mode == 1) {
        vec4 h = texture(src, v_uv);
        vec3 z = min(texture(src, b.xy + c.xy * (v_uv - b.xy)).rgb * c.z, vec3(1.0));
        out_color = vec4(0.5 * z + 0.5 * h.rgb, h.a);
    } else {
        vec4 h = texture(src, v_uv);
        out_color = vec4(min(h.rgb * a.z, vec3(1.0)), h.a * a.y);
    }
}
