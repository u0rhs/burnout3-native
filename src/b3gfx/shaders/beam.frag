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
    vec4 tun;         // x: the grade's share taken back in a tunnel (0.8 x enclosed), y / z: its near / far distance (m);
                      // w (weather, §38): the road's wetness 0..1: each light's mirror glint on the ground it lights
    vec4 lpos[8];     // xyz the lamp; w: its gain (0..1)
    vec4 ldir[8];     // xyz the beam's axis (normalised)
    mat4 sun_vp;      // SUN SHADOWS: world -> the map's clip space
    vec4 sun_p;       // x the map's side, y the height the light drops over the box's depth (m), z the kernel, w the
                      // darkening at full shadow (0: no shadow at night)
    vec4 sshape;      // street lights: x / y the cos of the pool's inner / outer angle from straight down, z d0 (m), w gain
    vec4 srange;      // xy the pool's fade (m from the lamp), z the lamp count
    vec4 spos[32];    // xyz the lamp; w its gain (the fade at the cap, nightfix)
    vec4 scol[32];    // rgb its tint; w the pool's scale (d0 and the fade x this: the lamp's height, nightfix)
    vec4 bshape;      // billboard floodlights (nightfix): x / y cos of the cone's inner / outer half angle, z gain, w count
    vec4 bpos[16];    // xyz the fixture (1 m in front of the board's bottom edge); w the board's height (m)
    vec4 bdir[16];    // xyz the fixture's axis (up and back towards the board); w its gain (the fade at the cap)
};
#include "sun_shadow.glsl"

vec3 world_at(vec2 frag, float z) {
    vec2 uv = frag * range.zw;
    vec4 p = inv * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, z, 1.0);
    return p.xyz / p.w;
}

// (round 3e) each light's strength at P and its direction, computed where needed (no per-light arrays: a pixel's
// registers stay few; the two loops recompute the cheap part)
float beam_e(int i, vec3 P, out vec3 l) {
    vec3 L = P - lpos[i].xyz;
    float d = length(L);
    l = L / max(d, 1e-4);
    if (d < 0.05 || d > range.y) return 0.0;
    float c = dot(l, ldir[i].xyz);
    if (c <= shape.y) return 0.0;   // outside the cone (and so behind the lamp)
    // a hot core inside a dim spill, so the beam reads as a beam: a bright spot ahead and a visible edge
    float cone = shape.w * smoothstep(shape.y, shape.x, c) + core.z * smoothstep(core.y, core.x, c);
    float r = d / shape.z;
    return lpos[i].w * cone * (1.0 - smoothstep(range.x, range.y, d)) / (1.0 + r * r);
}
// street lights: a pool under each lamp (a downward cone, a distance falloff); nightfix: scaled by the lamp's height
// (scol.w), faded at the cap (spos.w)
float street_e(int i, vec3 P, out vec3 l) {
    vec3 L = P - spos[i].xyz;
    float d = length(L);
    l = L / max(d, 1e-4);
    float sc = scol[i].w;
    if (d < 0.05 || d > srange.y * sc) return 0.0;
    float c = -l.y;   // the cosine from straight down
    if (c <= sshape.y) return 0.0;
    float r = d / (sshape.z * sc);
    return spos[i].w * sshape.w * smoothstep(sshape.y, sshape.x, c) * (1.0 - smoothstep(srange.x * sc, srange.y * sc, d)) / (1.0 + r * r);
}
// billboard floodlights (nightfix): a narrow fan from each fixture up the board's face, bright at the bottom edge,
// falling off upwards and between fixtures (round 3d: the falloff over 0.25 x the board's height, 2..6 m)
float board_e(int i, vec3 P, out vec3 l) {
    vec3 L = P - bpos[i].xyz;
    float d = length(L);
    l = L / max(d, 1e-4);
    float sz = bpos[i].w;
    if (d < 0.05 || d > 1.2 * sz) return 0.0;
    float c = dot(l, bdir[i].xyz);
    if (c <= bshape.y) return 0.0;
    float r = d / clamp(0.25 * sz, 2.0, 6.0);
    return bdir[i].w * bshape.z * smoothstep(bshape.y, bshape.x, c) * (1.0 - smoothstep(0.9 * sz, 1.2 * sz, d)) / (1.0 + r * r);
}

// weather (§38): a light's glint on the wet ground it lights: the mirror direction of its ray off an upward face towards
// the camera, a tight lobe, as much as the light reaches there (e), on the ground only (N.y), x the wetness
vec3 wet_glint(float e, vec3 l, vec3 N, vec3 P) {
    float up = smoothstep(0.7, 0.9, N.y);
    if (up <= 0.0) return vec3(0.0);
    vec3 R = reflect(l, vec3(0.0, 1.0, 0.0));
    float s = pow(max(dot(R, normalize(eye.xyz - P)), 0.0), 28.0);
    return vec3(0.35 * tun.w * up * min(e, 1.5) * s);
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
    float keep = 1.0;   // prod(1 - a_i)
    int n = int(eye.w + 0.5), ns = int(srange.z + 0.5), nb = int(bshape.w + 0.5);
    float m = 1.0;   // the moon shadow's factor on the graded frame (SUN SHADOWS at night)
    // first whether any light reaches (cheap); the normal (four more depth taps) and the shading only where one does
    bool any = false;
    vec3 l;
    for (int i = 0; i < n && !any; ++i) any = beam_e(i, P, l) > 1e-4;
    for (int i = 0; i < ns && !any; ++i) any = street_e(i, P, l) > 1e-4;
    for (int i = 0; i < nb && !any; ++i) any = board_e(i, P, l) > 1e-4;
    vec3 tsum = vec3(0.0);   // the lights' tints weighted by their strength
    float esum = 0.0;
    vec3 glint = vec3(0.0);   // weather: the wet ground's mirror of the lamps (added light, frame units)
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
        if (any) {
            for (int i = 0; i < n; ++i) {
                float e = beam_e(i, P, l);
                if (e <= 1e-4) continue;
                // the road takes the beam at a grazing angle (N.L ~ 0.05): it must still light up, so the facing term
                // has a floor of 0.55 for faces the beam grazes, rising to 1 for faces turned to it, and drops to 0 for
                // faces turned away (the backs of walls and cars)
                float ndl = dot(N, -l);
                float facing = smoothstep(-0.25, 0.05, ndl) * (0.55 + 0.45 * max(ndl, 0.0));
                keep *= exp(-e * facing);   // a_i = 1 - exp(-e): never 1, so the graded frame always shows a little
                tsum += e * facing * tint.rgb;
                esum += e * facing;
                if (tun.w > 0.0) glint += wet_glint(e, l, N, P) * tint.rgb;
            }
            for (int i = 0; i < nb; ++i) {
                float e = board_e(i, P, l);
                if (e <= 1e-4) continue;
                float ndl = dot(N, -l);
                // the board's face, not its back (the fan grazes the face: lit nearly fully from a few degrees)
                float facing = smoothstep(-0.02, 0.06, ndl) * (0.75 + 0.25 * max(ndl, 0.0));
                keep *= exp(-e * facing);
                tsum += e * facing * vec3(1.3, 1.15, 0.95);   // warm, a little above the daytime colour near the fixture
                esum += e * facing;
            }
            for (int i = 0; i < ns; ++i) {
                float e = street_e(i, P, l);
                if (e <= 1e-4) continue;
                float ndl = dot(N, -l);
                float facing = smoothstep(-0.25, 0.05, ndl) * (0.55 + 0.45 * max(ndl, 0.0));
                keep *= exp(-e * facing);
                tsum += e * facing * scol[i].rgb;
                esum += e * facing;
                if (tun.w > 0.0) glint += wet_glint(e, l, N, P) * scol[i].rgb;
            }
        }
    }
    float a = 1.0 - keep;
    float A = 1.0 - m * (1.0 - t) * keep;   // the graded frame's weight is m (1 - t) keep: the tunnel, the beams, the shadow
    if (A <= 1e-4) discard;
    vec3 c = texelFetch(src, ip, 0).rgb;
    // the part of A that is the tunnel's (untinted), the rest the beams' (tinted)
    vec3 lt = esum > 1e-6 ? tsum / esum : tint.rgb;   // the lamps' tint, by their share of the light
    vec3 rgb = c * (t * keep + a * lt) / A + glint / A;
    out_color = vec4(rgb, A);
}
