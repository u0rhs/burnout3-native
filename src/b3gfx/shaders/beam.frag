#version 450
#extension GL_GOOGLE_include_directive : require
// b3gfx night headlights as projected spot lights (nightlights, docs/remaster-features.md §17.2; night mode only, a
// remaster feature off by default). One full-screen draw in the resumed scene pass, after the world's grade and before
// the night sky, Z tested LESS against the scene's depth (the sky at the far plane is left to the night sky).
// The position comes from a single-sample depth pre-pass (`depth`, reverse-Z; the DOF's) and the inverse view-projection;
// the normal from the neighbouring depths (the smaller step on each axis, so an edge takes the surface it belongs to).
// Per lamp (up to 8): a hot core inside a wider, dimmer spill (each a smoothstep between its outer and inner cosines), a
// distance falloff 1 / (1 + (d / d0)^2)
// faded out between range.x and range.y, nothing behind the lamp, and a facing term (0.55 at a grazing angle, as the
// road is seen, up to 1 facing the lamp, 0 turned away), so walls and the backs of cars catch the light and faces
// turned away do not. The beams do not add light: the pre-grade colour (`src`, the copy
// night.frag grades) x the lamp's tint is blended over the graded frame with the alpha 1 - prod(1 - a_i), so a dark
// road lights up to at most its daytime colour, overlapping beams never exceed it, and the bloom sees something sane.
// Tunnels: `tun.x` (0.8 x enclosed) brings the pre-grade colour itself back within tun.y m, fading out by tun.z m, so
// inside a tunnel the world keeps most of its daytime look while the exit and beyond stay night.
// SUN SHADOWS (§20) at night: the moon shadow's receiver term is here, not in mesh.frag (whose track draws then take no
// shadow, so `src` is unshadowed): the graded frame's weight is m x (1 - t) x prod(1 - a_i), m = 1 - strength x the
// shadow on ground faces (sun_shadow_amount), so a headlight beam fills a moon shadow instead of restoring a darkened
// colour, and where no beam falls the shadow darkens the graded road.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;     // the pre-grade frame (single-sample)
layout(set = 2, binding = 1) uniform sampler2D depth;   // the depth pre-pass (single-sample, reverse-Z)
layout(set = 2, binding = 2) uniform sampler2D sun_shadow_map;   // SUN SHADOWS' map (shaders/sun_shadow.glsl; unused when sun_p.w is 0)
layout(set = 3, binding = 0) uniform BeamUniforms {
    mat4 inv;         // the inverse of view_proj
    vec4 eye;         // xyz the camera; w: the lamp count
    vec4 shape;       // the spill: x: cos of its inner half angle, y: of its outer, z: d0 (m), w: its gain
    vec4 core;        // the hot core: x: cos of its inner half angle, y: of its outer, z: its gain
    vec4 range;       // xy: the fade's start and end (m); zw: 1 / the target's size
    vec4 tint;        // rgb the lamps' tint
    vec4 tun;         // x: the grade's share taken back in a tunnel (0.8 x enclosed), y / z: its near / far distance (m)
    vec4 lpos[8];     // xyz the lamp; w: its gain (0..1)
    vec4 ldir[8];     // xyz the beam's axis (normalised)
    mat4 sun_vp;      // SUN SHADOWS: world -> the map's clip space
    vec4 sun_p;       // x the map's side, y the height the light drops over the box's depth (m), z the kernel, w the
                      // darkening at full shadow (0: no shadow at night)
    vec4 sshape;      // street lights: x / y the cos of the pool's inner / outer angle from straight down, z d0 (m), w gain
    vec4 srange;      // xy the pool's fade (m from the lamp), z the lamp count
    vec4 spos[24];    // xyz the lamp
    vec4 scol[24];    // rgb its tint
};
#include "sun_shadow.glsl"

vec3 world_at(vec2 frag, float z) {
    vec2 uv = frag * range.zw;
    vec4 p = inv * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, z, 1.0);
    return p.xyz / p.w;
}

void main() {
    ivec2 ip = ivec2(gl_FragCoord.xy);
    ivec2 mx = ivec2(textureSize(depth, 0)) - 1;
    float z = texelFetch(depth, clamp(ip, ivec2(0), mx), 0).r;
    if (z <= 0.0) discard;   // the far plane (the sky, the clear)
    vec3 P = world_at(gl_FragCoord.xy, z);
    float dist = length(P - eye.xyz);
    // the tunnel's share
    float t = tun.x * (1.0 - smoothstep(tun.y, tun.z, dist));
    // the beams
    float keep = 1.0;   // prod(1 - a_i)
    int n = int(eye.w + 0.5);
    float m = 1.0;   // the moon shadow's factor on the graded frame (SUN SHADOWS at night)
    // first each lamp's cone and falloff (cheap); the normal (four more depth taps) only where some lamp reaches
    float ei[8];
    vec3 li[8];
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        ei[i] = 0.0;
        li[i] = vec3(0.0);
        if (i >= n) continue;
        vec3 L = P - lpos[i].xyz;
        float d = length(L);
        if (d < 0.05 || d > range.y) continue;
        vec3 l = L / d;
        float c = dot(l, ldir[i].xyz);
        if (c <= shape.y) continue;   // outside the cone (and so behind the lamp)
        // a hot core inside a dim spill, so the beam reads as a beam: a bright spot ahead and a visible edge
        float cone = shape.w * smoothstep(shape.y, shape.x, c) + core.z * smoothstep(core.y, core.x, c);
        float r = d / shape.z;
        float fall = (1.0 - smoothstep(range.x, range.y, d)) / (1.0 + r * r);
        ei[i] = lpos[i].w * cone * fall;
        li[i] = l;
        any = any || ei[i] > 1e-4;
    }
    // the normal: for the beams' facing and the moon shadow's ground test
    // street lights: a pool under each lamp (a downward cone, a distance falloff), tinted per lamp
    float es[24];
    vec3 ls[24];
    int ns = int(srange.z + 0.5);
    for (int i = 0; i < 24; ++i) {
        es[i] = 0.0;
        ls[i] = vec3(0.0);
        if (i >= ns) continue;
        vec3 L = P - spos[i].xyz;
        float d = length(L);
        if (d < 0.05 || d > srange.y) continue;
        vec3 l = L / d;
        float c = -l.y;   // the cosine from straight down
        if (c <= sshape.y) continue;
        float r = d / sshape.z;
        es[i] = sshape.w * smoothstep(sshape.y, sshape.x, c) * (1.0 - smoothstep(srange.x, srange.y, d)) / (1.0 + r * r);
        ls[i] = l;
        any = any || es[i] > 1e-4;
    }
    vec3 tsum = vec3(0.0);   // the lights' tints weighted by their strength
    float esum = 0.0;
    bool shadow = sun_p.w > 0.0;
    if (any || shadow) {
        // the normal from the neighbours: per axis the nearer step in depth
        float zl = texelFetch(depth, clamp(ip - ivec2(1, 0), ivec2(0), mx), 0).r;
        float zr = texelFetch(depth, clamp(ip + ivec2(1, 0), ivec2(0), mx), 0).r;
        float zu = texelFetch(depth, clamp(ip - ivec2(0, 1), ivec2(0), mx), 0).r;
        float zd = texelFetch(depth, clamp(ip + ivec2(0, 1), ivec2(0), mx), 0).r;
        vec3 dx = abs(zr - z) < abs(z - zl) ? world_at(gl_FragCoord.xy + vec2(1.0, 0.0), zr) - P : P - world_at(gl_FragCoord.xy - vec2(1.0, 0.0), zl);
        vec3 dy = abs(zd - z) < abs(z - zu) ? world_at(gl_FragCoord.xy + vec2(0.0, 1.0), zd) - P : P - world_at(gl_FragCoord.xy - vec2(0.0, 1.0), zu);
        vec3 N = cross(dy, dx);
        float nl = length(N);
        N = nl > 1e-8 ? N / nl : vec3(0.0, 1.0, 0.0);
        if (dot(N, eye.xyz - P) < 0.0) N = -N;   // towards the eye
        if (shadow) {   // as mesh.frag's receiver: ground faces only (turned up), the open road's darkening
            float up = smoothstep(0.55, 0.8, N.y);
            float sh = up > 0.0 ? up * sun_shadow_amount(sun_shadow_map, (sun_vp * vec4(P, 1.0)).xyz, sun_p) : 0.0;
            m = 1.0 - sun_p.w * sh;
        }
        for (int i = 0; i < 8; ++i) {
            if (ei[i] <= 1e-4) continue;
            // the road takes the beam at a grazing angle (N.L ~ 0.05): it must still light up, so the facing term
            // has a floor of 0.55 for faces the beam grazes, rising to 1 for faces turned to it, and drops to 0 for
            // faces turned away (the backs of walls and cars)
            float ndl = dot(N, -li[i]);
            float facing = smoothstep(-0.25, 0.05, ndl) * (0.55 + 0.45 * max(ndl, 0.0));
            keep *= exp(-ei[i] * facing);   // a_i = 1 - exp(-e): never 1, so the graded frame always shows a little
            tsum += ei[i] * facing * tint.rgb;
            esum += ei[i] * facing;
        }
        for (int i = 0; i < 24; ++i) {
            if (es[i] <= 1e-4) continue;
            float ndl = dot(N, -ls[i]);
            float facing = smoothstep(-0.25, 0.05, ndl) * (0.55 + 0.45 * max(ndl, 0.0));
            keep *= exp(-es[i] * facing);
            tsum += es[i] * facing * scol[i].rgb;
            esum += es[i] * facing;
        }
    }
    float a = 1.0 - keep;
    float A = 1.0 - m * (1.0 - t) * keep;   // the graded frame's weight is m (1 - t) keep: the tunnel, the beams, the shadow
    if (A <= 1e-4) discard;
    vec3 c = texelFetch(src, ip, 0).rgb;
    // the part of A that is the tunnel's (untinted), the rest the beams' (tinted)
    vec3 lt = esum > 1e-6 ? tsum / esum : tint.rgb;   // the lamps' tint, by their share of the light
    vec3 rgb = c * (t * keep + a * lt) / A;
    out_color = vec4(rgb, A);
}
