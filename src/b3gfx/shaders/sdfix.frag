#version 450
// scenedepth (b3gfx_sdl.cpp, the scene depth): with MSAA the scene pass's depth target is resolved by averaging each
// pixel's samples, so at a silhouette it holds neither surface's depth (a halo for the passes that read it: the night
// lights, the AO, the depth of field). This pass writes the depth they read: where the pixel's 3 x 3 spans a
// discontinuity (its nearest and farthest differ by more than a share `a.z` of the nearest), the pixel takes whichever of
// the two its averaged value is closer to (the surface covering most of it, as a single-sample depth would hold);
// elsewhere it keeps its own. (a.w = 1: the nearest always, at such a pixel.)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_depth;
layout(set = 2, binding = 0) uniform sampler2D depth;   // the resolved scene depth (nearest, clamped)
layout(set = 3, binding = 0) uniform FixUniforms {
    vec4 a;   // xy: the texel size (uv), z: the relative difference that marks an edge
};
void main() {
    float d = textureLod(depth, v_uv, 0.0).r, hi = d, lo = d;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            if (i == 0 && j == 0) continue;
            float n = textureLod(depth, v_uv + vec2(float(i), float(j)) * a.xy, 0.0).r;
            hi = max(hi, n);
            lo = min(lo, n);
        }
    float r = d;
    if (hi - lo > a.z * hi) r = a.w > 0.5 || hi - d < d - lo ? hi : lo;
    out_depth = vec4(r, 0.0, 0.0, 1.0);
}
