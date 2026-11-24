#version 450
#extension GL_GOOGLE_include_directive : require
#include "hash.glsl"   // d3dhash
// b3gfx night mode's light fixtures (nightlights, docs/remaster-features.md §17.2; night only, a remaster feature off by
// default): a track draw tagged DrawItem::night_emissive (a tunnel's lamp strips and panels, by texture name) drawn once
// more after the grade and the night sky over mesh.vert, depth GEQUAL against its own first draw (reverse-Z), not
// written. Only its bright texels (brightest channel above ~0.6, so coloured neon counts) take their pre-grade colour back (`src`, the copy night.frag
// grades; x `a.x`), alpha-blended over the frame (RGB only), so the tube glows and reaches the bloom while the lamp's
// housing and the ceiling around it stay as graded.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_normal;
layout(location = 5) in vec3 v_wpos;   // the world position (mesh.vert), for the windows' hash (round 3)
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D tex;   // the draw's texture
layout(set = 2, binding = 1) uniform sampler2D src;   // the pre-grade frame (single-sample)
layout(set = 3, binding = 0) uniform EmissiveUniforms {
    vec4 a;   // x: the glow's gain over the pre-grade colour, y / z: the luminance ramp (from, to), w: the alpha test's reference (0: none)
    vec4 b;   // rgb: a tint over the glow, w: its opacity (nightfix: a front-lit billboard, its floodlights' warm wash
              // over part of the grade; 1, 1, 1, 1 for the fixtures and backlit panels)
    vec4 c;   // round 3: x the mode (4 windows, 5 shop front, 6 neon / LED, 7 retro-reflective, else the above), y the
              // texture's mean light, zw its windows' grid (columns, rows per repeat)
    vec4 d;   // round 3b: xyz the camera (the retro-reflective signs: bright when the headlights -- by the camera -- face them);
              // xboxglow (§35.5): w 1 the texture's alpha is its glow mask (modes 1, 8), 2 that and by day the Xbox look
              // (the masked texels self-lit at the texel's colour, opaque), 3 the Xbox look on the bright texels (no mask)
};
vec3 hash3(vec3 cell) { return hash3i(cell); }   // d3dhash: an integer cell (hash.glsl)
void main() {
    vec4 t = texture(tex, v_uv);
    // (an alpha-tested draw's discarded texels are below the mask's ramp too: k would be 0 there, so the order is harmless)
    if (a.w > 0.0 && t.a < a.w) discard;
    int mode = int(c.x + 0.5);
    // xboxglow (§35.5): the glow mask (the texture's alpha over the lamp's lit part) and the Xbox look
    bool masked = d.w > 0.5 && d.w < 2.5, xbox = d.w > 1.5;
    if (mode == 8) {   // TUNNEL LIGHTS by day (§35.4): the fitting's bright texels above white (the frame is HDR: 0.5 is
                       // white on screen), blended at 85 % over the prelit draw, so the tube reads as lit and the bloom glows.
                       // xboxglow: the masked texels; the Xbox look draws them self-lit at a.x (View kXboxGlowGain, 1.5)
                       // x the texel's own colour (not the tunnel's prelight: a white tube 1.4, well over the route's bloom
                       // threshold 0.65-0.75), opaque, and the frame's bloom makes the glow
        float mx8 = max(t.r, max(t.g, t.b));
        float k8 = masked ? smoothstep(0.3, 0.7, t.a) : smoothstep(0.5, 0.75, mx8);
        if (k8 <= 1e-3) discard;
        out_color = xbox ? vec4(t.rgb * a.x, k8) : vec4(t.rgb * 0.85 * a.x, k8 * 0.8);
        return;
    }
    if (mode == 6) {   // neon / LED: the lit tubes or dots (the brightest channel above ~0.35) saturated and bright, so the
                       // bloom makes the coloured halo; the board or wall behind them stays as graded
        float mx = max(t.r, max(t.g, t.b));
        float k6 = smoothstep(0.3, 0.6, mx);
        if (k6 <= 1e-3) discard;
        vec3 s6 = texelFetch(src, ivec2(gl_FragCoord.xy), 0).rgb;
        float l6 = dot(s6, vec3(0.299, 0.587, 0.114));
        out_color = vec4(max(mix(vec3(l6), s6, 1.35), vec3(0.0)) * 1.3, k6);
        return;
    }
    if (mode == 7) {   // retro-reflective: its pre-grade colour, bright when it faces the camera (and the car's headlights
                       // just ahead of it) within the beams' reach, a faint base otherwise
        vec3 n7 = cross(dFdx(v_wpos), dFdy(v_wpos));
        vec3 v7 = d.xyz - v_wpos;
        float dist = length(v7);
        float face = abs(dot(normalize(n7 + vec3(1e-9)), v7 / max(dist, 1e-3)));
        float boost = face * face * face * (1.0 - smoothstep(35.0, 90.0, dist));
        vec3 s7 = texelFetch(src, ivec2(gl_FragCoord.xy), 0).rgb;
        out_color = vec4(s7 * (0.5 + 0.9 * boost), 0.35 + 0.6 * boost);
        return;
    }
    if (mode == 4 || mode == 5) {
        // the glass: texels darker than their surroundings (the texture's local average at a mip about one window cell
        // wide: a pane in its wall; a uniform wall is never glass), and darker than the texture's mean light (c.y)
        vec2 ts = vec2(textureSize(tex, 0));
        float lodc = min(log2(max(max(ts.x / c.z, ts.y / c.w), 2.0)) + 0.5, float(textureQueryLevels(tex)) - 2.0);
        float l0 = dot(t.rgb, vec3(0.299, 0.587, 0.114));
        float la = dot(textureLod(tex, v_uv, lodc).rgb, vec3(0.299, 0.587, 0.114));
        float rel = max(l0 / max(la, 0.03), l0 / max(c.y * 1.15, 0.04));
        float glass = 1.0 - smoothstep(0.55, 0.85, rel);
        vec3 src = texelFetch(src, ivec2(gl_FragCoord.xy), 0).rgb;
        if (mode == 5) {   // a shop front: its window bright and warm, the rest a warm wash
            vec3 warm = vec3(1.0, 0.8, 0.55);
            // the window: a warm interior light and part of the texture's detail (not the texel's full daytime colour: a
            // dark stone the test takes for glass then glows dimly, not as a lit panel); the wall: 30 % of the way back
            vec3 rgb = mix(src * vec3(1.0, 0.92, 0.8) * 0.9, warm * 0.3 + src * 0.18, glass);
            out_color = vec4(rgb, mix(0.25, 0.85, glass));
            return;
        }
        if (glass <= 0.02) discard;
        // the window: its cell of the texture's grid in this repeat, and the building (25 m blocks of the world)
        vec2 g = v_uv * c.zw;
        vec3 h = hash3(vec3(floor(g), 0.0) + floor(v_wpos / 25.0) * vec3(17.0, 31.0, 7.0));
        if (h.x > 0.42) discard;   // ~42 % of the windows lit
        vec3 tint = h.y < 0.45 ? vec3(1.0, 0.78, 0.48) : h.y < 0.8 ? vec3(1.0, 0.92, 0.76) : vec3(0.74, 0.86, 1.0);
        float level = (0.26 + 0.22 * h.z) * (0.75 + 0.5 * clamp(rel, 0.0, 1.0));
        out_color = vec4(tint * level, glass * 0.95);
        return;
    }
    // the brightest channel, not the luminance: a coloured neon tube (a saturated red or blue) is as lit as a white one
    float l = max(t.r, max(t.g, t.b));
    float k = mode == 1 && masked ? smoothstep(0.3, 0.7, t.a) : smoothstep(a.y, a.z, l);   // (xboxglow: a fixture's mask)
    if (k <= 1e-3) discard;
    out_color = vec4(texelFetch(src, ivec2(gl_FragCoord.xy), 0).rgb * a.x * b.rgb, k * b.w);
}
