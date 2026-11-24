#version 450
#extension GL_GOOGLE_include_directive : require
#include "hash.glsl"   // d3dhash
// b3gfx night mode (TIME OF DAY: Night, a remaster feature off by default; docs/remaster-features.md §17): the grade of
// the frame so far and the night sky, full-screen over the scene pass, reading a single-sample copy of the frame (`src`).
// Two draws, split by the depth test: mode 0 where something was drawn (the world: desaturated, tinted, a curve), mode 1
// where the sky shows (the far plane: a night gradient, the original sky dimmed, moonlit clouds, stars, the moon).
// Frame units (the present pass doubles them); the pipeline writes RGB only (the frame alpha stays).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(set = 3, binding = 0) uniform NightUniforms {
    mat4 inv;        // the inverse of view_proj (the sky's view ray)
    vec4 a;          // x: mode (0 world, 1 sky), y: saturation kept, z: the curve's gamma, w: its pivot
    vec4 tint;       // world multiplier rgb; w: the moonlit clouds' strength
    vec4 sky;        // the original sky's multiplier rgb; w: the stars' brightness (0: none)
    vec4 zenith;     // rgb the night sky overhead
    vec4 horizon;    // rgb at the horizon
    vec4 moon;       // xyz towards the moon; w: the disc's angular radius (0: no moon)
    vec4 moon_col;   // rgb the disc; w: the halo's strength
    vec4 eye;        // xyz the camera; w: (world, nightlights §17.2) the grade's share taken back in a tunnel on the
                     // presets without the spot pass (0.8 x enclosed, the whole world: no depth to keep the exit night)
};

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// d3dhash: integer cells (hash.glsl)
float hash13(vec3 cell) { return hash3i(cell).x; }
vec3 hash33(vec3 cell, uint stream) { return hash3i_s(cell, stream); }
float noise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a0 = hash13(vec3(i, 7.0)), a1 = hash13(vec3(i + vec2(1.0, 0.0), 7.0));
    float b0 = hash13(vec3(i + vec2(0.0, 1.0), 7.0)), b1 = hash13(vec3(i + vec2(1.0, 1.0), 7.0));
    return mix(mix(a0, a1, f.x), mix(b0, b1, f.x), f.y);
}

void main() {
    vec3 c = texelFetch(src, ivec2(gl_FragCoord.xy), 0).rgb;
    if (a.x < 0.5) {
        vec3 g = mix(vec3(luma(c)), c, a.y) * tint.rgb;
        g = pow(max(g, vec3(0.0)) / a.w, vec3(max(a.z, 0.1))) * a.w;
        out_color = vec4(mix(g, c, eye.w), 0.0);
        return;
    }
    // the view ray through this pixel (reverse-Z: the near plane at depth 1)
    vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0);
    vec4 wp = inv * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(wp.xyz / wp.w - eye.xyz);
    float pix = max(length(fwidth(d)), 1e-5);   // a pixel's angle (rad)
    float up = d.y;
    // the gradient: the horizon's colour low, the zenith's higher up (a little lighter just above the horizon)
    float h = clamp(up, 0.0, 1.0);
    vec3 col = mix(horizon.rgb, zenith.rgb, pow(smoothstep(0.0, 0.7, h), 0.6));
    // the original sky (gradient, clouds, the backdrop's mountains and skylines), dimmed: its shapes stay readable
    float sat = (max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b)) / max(max(max(c.r, c.g), c.b), 1e-3);
    float cloud = (1.0 - smoothstep(0.08, 0.3, sat)) * smoothstep(0.08, 0.3, luma(c));   // white and bright: a cloud
    col += c * sky.rgb;
    // the moon: its angle from this ray, the halo (wide and soft), the clouds it lights
    float ang = 1e3, glow = 0.0;
    if (moon.w > 0.0) {
        ang = acos(clamp(dot(d, moon.xyz), -1.0, 1.0));
        glow = moon_col.w * (0.55 * exp(-ang / (moon.w * 2.2)) + 0.22 * exp(-ang / 0.22));
    }
    col += cloud * luma(c) * tint.w * (0.4 + 6.0 * glow) * moon_col.rgb;
    // stars: one candidate per cell of a grid on the view ray (~0.3 degree cells), kept away from cell edges so a star
    // is never cut; only in clear sky (blue rather than grey or brown: no stars on clouds, haze or the backdrop's
    // mountains), fading near the horizon and inside the moon's halo
    if (sky.w > 0.0 && up > 0.02) {
        const float N = 190.0;
        vec3 p = d * N;
        vec3 cell = floor(p);
        vec3 r = hash33(cell, 0u);
        if (r.x > 0.78) {
            vec3 s = normalize(cell + 0.5 + 0.8 * (hash33(cell, 1u) - 0.5));
            vec3 fs = fract(s * N);
            if (all(greaterThan(fs, vec3(0.2))) && all(lessThan(fs, vec3(0.8))) && floor(s * N) == cell) {
                float da = length(cross(d, s));
                float size = max(pix * 0.9, 2.5e-4);
                float star = (1.0 - smoothstep(0.0, size, da)) * (0.1 + 0.9 * r.y * r.y * r.y * r.y);
                float clear = smoothstep(0.015, 0.08, c.b - c.r) + smoothstep(0.35, 0.6, up) * (1.0 - cloud);
                float keep = clamp(clear, 0.0, 1.0) * smoothstep(0.02, 0.15, up) * (1.0 - cloud);
                if (moon.w > 0.0) keep *= smoothstep(moon.w * 3.0, moon.w * 9.0, ang);
                col += sky.w * star * keep * mix(vec3(0.85, 0.9, 1.0), vec3(1.0, 0.93, 0.85), r.z);
            }
        }
    }
    if (moon.w > 0.0) {
        col += glow * moon_col.rgb;
        // the disc: a soft limb, faint maria from a fixed noise on the disc (the same face always shown)
        vec3 rt = normalize(cross(vec3(0.0, 1.0, 0.0), moon.xyz) + vec3(1e-4, 0.0, 0.0));
        vec3 uv3 = cross(moon.xyz, rt);
        vec2 q = vec2(dot(d, rt), dot(d, uv3)) / moon.w;
        float rr = length(q);
        float edge = pix / moon.w;
        float disc = 1.0 - smoothstep(1.0 - edge, 1.0 + edge, rr);
        if (disc > 0.0) {
            float mu = sqrt(max(1.0 - rr * rr, 0.0));
            float maria = noise2(q * 2.2 + 3.0) * 0.6 + noise2(q * 5.0 + 11.0) * 0.4;
            float shade = (0.72 + 0.28 * mu) * (1.0 - 0.32 * smoothstep(0.45, 0.75, maria));
            col = mix(col, moon_col.rgb * shade, disc);
        }
    }
    out_color = vec4(col, 0.0);
}
