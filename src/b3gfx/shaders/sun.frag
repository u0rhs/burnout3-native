#version 450
// Sun corona and lens flare (water1, docs/native-renderer.md §21): TEX0 MODULATE (Ct x Cv / 128, clamped) and ALPHA 0x48
// = Cs x As + Cd, where As is the corona texture's CLUT alpha, which OcclusionTest / RenderCorona overwrite with the
// sky share of the box around the sun: here the 1 x 1 mip of the occlusion target (1 = sky, 0 = covered).
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_rgb;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 2, binding = 1) uniform sampler2D occ;
layout(set = 3, binding = 0) uniform SunFragmentUniforms { vec4 p; };   // x: the 1 x 1 level of `occ`, y: 1 = test on
void main() {
    vec3 c = min(texture(tex, v_uv).rgb * v_rgb * (255.0 / 128.0), vec3(1.0));
    float a = p.y > 0.5 ? textureLod(occ, vec2(0.5), p.x).r : 1.0;
    out_color = vec4(c * a, 0.0);
}
