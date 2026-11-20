#version 450
// b3gfx mesh fragment shader. SDL_GPU bindings: fragment samplers in set 2, uniform buffers in set 3.
// SUN_SHADOW (build_shaders.sh compiles mesh_shadow.frag from this file with it defined): the variant for the track
// draws that receive the cars' sun shadow (SUN SHADOWS, remaster-features §20); without it the shader is unchanged.
#ifdef SUN_SHADOW
#extension GL_GOOGLE_include_directive : require
#endif
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_normal;
layout(location = 3) in float v_w;
layout(location = 4) noperspective in vec2 v_line;   // line meshes: distance from the centre line, half width (pixels)
layout(location = 5) in vec3 v_wpos;                  // world position (surface)

layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D tex;
#ifdef SUN_SHADOW
layout(set = 2, binding = 1) uniform sampler2D sun_shadow_map;   // shaders/sun_shadow.glsl
#endif

layout(set = 3, binding = 0) uniform FragmentUniforms {
    vec4 tint;       // RGBA multiplier (simple model); track model: a >= 0 = blend by this constant (GS ALPHA FIX)
    vec4 light_dir;  // xyz: direction to the light (normalized); w: 1 = an env-map face (b3gfx::EnvPass: opaque alpha 1)
    vec4 params;     // x: alpha reference (discard below), y: lighting amount, z: 1 = keep alpha / 0 = opaque,
                     // w: 1 = track model
    vec4 gs;         // track model: x: GS TEST_1, y: 1 = textured, z: 1 = blended draw (keeps its alpha), w: the sharp alpha threshold (0: off)
                     // coverage alpha (magnified HUD fonts, hud2)
    vec4 fog;        // z_scale, z_shift, fog_max, fog_sky
    vec4 fog_colour; // rgb in frame units, a = 1: fog on
    // surface (remaster, off by default; docs/remaster-features.md §18): relief and sheen from the colour texture on the
    // opaque track. surf: x relief height (0: off), y sheen strength (0: off), z fade distance (m), w sky-light share;
    // surf_sun: xyz towards the sun (normalised), w the sheen's grazing boost; surf_eye: xyz the camera, w the debug view
    vec4 surf;
    vec4 surf_sun;
    vec4 surf_eye;
    // night mode (§17): x the moonlit faces' strength (0: off): faces turned to the light (surf_sun, the moon at night)
    // brighter, faces turned away darker, at any distance (the relief fades out, this does not)
    vec4 surf_night;
#ifdef SUN_SHADOW
    // SUN SHADOWS: world -> the map's clip space; sun_p: x the map's side, y the height the light drops over the box's
    // depth (m), z the kernel (1: 5 x 5), w the darkening at full shadow (the open road's colour x 1 - w); sun_q: x the
    // open road's vertex light (luminance, GS units / 128)
    mat4 sun_vp;
    vec4 sun_p;
    vec4 sun_q;
#endif
};

#ifdef SUN_SHADOW
#include "sun_shadow.glsl"
#endif

// The game's fog [C CB3Fog::Render 0x2419C0 / GenerateClut 0x2413E0, docs/sky-enviro.md]: the 24-bit Z the programs
// write (Z = zShift + zScale / w) >> 8 indexes a CLUT; only Z < 0xFFFF is fogged.
float fog_amount(float w) {
    if (w <= 0.0) return 0.0;
    float z = fog.y + fog.x / w;
    if (z >= 65535.0) return 0.0;
    int i = int(max(z, 0.0)) >> 8;
    float v;
    if (i == 0) v = fog.w;
    else if (i < 12) v = fog.z;
    else {
        float t = (1.4 / (0.4 + float(i - 12) / 243.0) - 1.0) / 2.5;
        t = t < 0.75 ? t * t * 1.3333334 : 1.0 - 4.0 * (1.0 - t) * (1.0 - t);
        v = t * fog.z;
    }
    return float(clamp(int(0.5 + 128.0 * v), 0, 255)) / 128.0;
}

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// surface: the prelit colour stays the base; a normal per pixel from the colour texture's luminance (bright = raised),
// on a frame built from the screen-space derivatives of the position and UV (the track has no tangents, and normals only
// on the type-1/2 materials), lit by the ENVIRO sun as a factor around 1.0: the change of N.L against the flat face, so
// flat texels keep the game's colour exactly. The sheen (a Blinn-Phong lobe on the bumped normal, weighted by a gloss
// guessed from the texel: grey asphalt, grey metal) goes into the frame alpha the full-screen specular turns into the
// route's own sun specular colour. Returns the colour factor; `sheen` the alpha to add.
// The derivatives are taken by the caller in uniform control flow, before the alpha test's discard.
float surface(vec4 t, vec3 dp1, vec3 dp2, vec2 du1, vec2 du2, out float sheen) {
    sheen = 0.0;
    vec3 N0 = cross(dp1, dp2);
    float ln = length(N0);
    if (!(ln > 1e-14)) return 1.0;
    N0 /= ln;
    vec3 V = normalize(surf_eye.xyz - v_wpos);
    vec3 Nf = dot(N0, V) < 0.0 ? -N0 : N0;   // the visible side
    // night mode: the faces turned to the moon catch its light (a factor around 1 on the prelit colour)
    float face = surf_night.x > 0.0 ? 1.0 + surf_night.x * (clamp(dot(Nf, surf_sun.xyz), 0.0, 1.0) - 0.45) : 1.0;
    float fade = 1.0 - smoothstep(0.4 * surf.z, surf.z, v_w);
    if (fade <= 0.0) return face;
    // the cotangent frame [Schueler 2013]: T, B along increasing u, v
    vec3 p2 = cross(dp2, N0), p1 = cross(N0, dp1);
    vec3 T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
    float tb = max(dot(T, T), dot(B, B));
    if (!(tb > 0.0)) return 1.0;
    float im = inversesqrt(tb);
    T *= im;
    B *= im;
    // the luminance gradient per texel at the mip the footprint picks (one level coarser: steadier at a distance)
    vec2 sz = vec2(textureSize(tex, 0));
    float rho = max(length(du1 * sz), length(du2 * sz));
    float lod = clamp(log2(max(rho, 1e-6)) + 1.0, 0.0, float(textureQueryLevels(tex) - 1));
    vec2 e = exp2(lod) / sz;
    float lx = luma(textureLod(tex, v_uv + vec2(e.x, 0.0), lod).rgb) - luma(textureLod(tex, v_uv - vec2(e.x, 0.0), lod).rgb);
    float ly = luma(textureLod(tex, v_uv + vec2(0.0, e.y), lod).rgb) - luma(textureLod(tex, v_uv - vec2(0.0, e.y), lod).rgb);
    vec2 g = 0.5 * vec2(lx, ly) * surf.x * fade;
    vec3 N = normalize(Nf - T * g.x - B * g.y);
    vec3 L = surf_sun.xyz;
    float nl0 = dot(Nf, L);
    float lit = smoothstep(-0.05, 0.3, nl0);          // faces turned from the sun get the sky's share only
    float d = (dot(N, L) - nl0) * lit + surf.w * (N.y - Nf.y);
    float m = clamp(1.0 + d, 0.6, 1.4) * face;
    if (surf.y > 0.0) {
        float lum = luma(t.rgb);
        float sat = max(max(t.r, t.g), t.b) - min(min(t.r, t.g), t.b);
        float grey = 1.0 - smoothstep(0.06, 0.2, sat);
        float road = grey * smoothstep(0.03, 0.10, lum) * (1.0 - smoothstep(0.35, 0.65, lum)) * smoothstep(0.75, 0.95, Nf.y);
        float metal = grey * smoothstep(0.35, 0.6, lum) * (1.0 - smoothstep(0.3, 0.6, abs(Nf.y))) * 0.5;
        float gloss = max(road, metal);
        vec3 H = normalize(L + V);
        float nv = clamp(dot(N, V), 0.0, 1.0);
        float lobe = pow(max(dot(N, H), 0.0), mix(12.0, 48.0, gloss));
        float fres = 1.0 + surf_sun.w * pow(1.0 - nv, 4.0);
        sheen = surf.y * gloss * lobe * fres * lit * fade;
    }
    return m;
}

void main() {
    if (params.w > 3.5) {
        // Line meshes (type 3, docs/native-renderer.md §11.8): PRIM 0xE9 with ALPHA_1 0x42 = Cd - Cs x As, where AA1
        // replaces As by the pixel's coverage; untextured, so Cs = Cf, fogged by the program's F towards FOGCOL (0 [G]).
        // The pipeline blends RGB as Cd - src (REVERSE_SUBTRACT, ONE, ONE) and writes the frame alpha = As (the GS
        // writes As unblended; the full-screen specular reads it). The fog pass comes later on the PS2: the wire's
        // darkening is scaled by (1 - f) at its depth, and so is the alpha the specular reads (as for opaque draws).
        float cov = clamp(v_line.y + 0.5 - abs(v_line.x), 0.0, 1.0);
        vec3 cs = min(v_color.rgb * (128.0 / 255.0), vec3(1.0)) * v_normal.x;
        float f = fog_colour.a > 0.5 ? min(fog_amount(v_w), 1.0) : 0.0;
        out_color = vec4(cs * (cov * (1.0 - f)), cov * (1.0 - f));
        return;
    }
    if (params.w > 0.5) {   // 1: track model, 2: sky
        // The GS with TEX0 TFX = MODULATE, TCC = 1 [C track rasters]: Cv = Ct * Cf >> 7 (clamped), Av = At * Af >> 7;
        // untextured: Cv = Cf. Colours here are GS bytes / 128 from the vertex stage, texels 0..1 (alpha 0x80 = 1).
        vec4 t = texture(tex, v_uv);
        const bool surf_on = surf.x > 0.0 || surf.y > 0.0;   // a uniform: the derivatives below are in uniform control flow
        vec3 sdp1 = vec3(0.0), sdp2 = vec3(0.0);
        vec2 sdu1 = vec2(0.0), sdu2 = vec2(0.0);
        if (surf_on) { sdp1 = dFdx(v_wpos); sdp2 = dFdy(v_wpos); sdu1 = dFdx(v_uv); sdu2 = dFdy(v_uv); }
#ifdef SUN_SHADOW
        // SUN SHADOWS: the face's normal from the position's derivatives (taken here, in uniform control flow)
        vec3 shn = cross(dFdx(v_wpos), dFdy(v_wpos));
#endif
        // Magnified coverage alpha (the HUD fonts at window resolution): the bilinear ramp spans several pixels, so
        // threshold it at 0.5 with a one-pixel ramp instead, as a distance field would draw it (display choice).
        float aw = fwidth(t.a);
        // gs.w: the threshold, 0 = off (mac5: thin fonts lower, so their strokes keep their weight)
        if (gs.w > 0.0) t.a = clamp((t.a - gs.w) / max(aw, 1.0 / 64.0) + 0.5, 0.0, 1.0);
        bool textured = gs.y > 0.5;
        vec3 c = textured ? min(t.rgb * v_color.rgb, vec3(1.0)) : min(v_color.rgb * (128.0 / 255.0), vec3(1.0));
        float a = textured ? floor(t.a * 128.0 * v_color.a) : floor(v_color.a * 128.0);  // GS alpha byte
        // TEST_1: ATE bit 0, ATST bits 1-3, AREF bits 4-11, AFAIL bits 12-13 (0 = keep: the pixel is dropped).
        uint test = uint(gs.x);
        if ((test & 1u) != 0u && ((test >> 12) & 3u) == 0u) {
            uint atst = (test >> 1) & 7u;
            float aref = float((test >> 4) & 0xFFu);
            bool pass = atst == 1u || (atst == 2u && a < aref) || (atst == 3u && a <= aref) || (atst == 4u && a == aref) ||
                        (atst == 5u && a >= aref) || (atst == 6u && a > aref) || (atst == 7u && a != aref);
            if (!pass) discard;
        }
        // Alpha = As / 128: the blend factor of ALPHA (Cs-Cd)*As+Cd and the frame-buffer alpha the full-screen
        // specular pass reads; FIX blends use the constant instead.
        float oa = tint.a >= 0.0 ? tint.a : a / 128.0;
        float sheen = 0.0;
        if (surf_on) {   // surface (off by default: the C++ side sets surf only for opaque track draws)
            float m = surface(t, sdp1, sdp2, sdu1, sdu2, sheen);
            // debug view (frame units, doubled on present): red = 0.5 + the factor's change (grey where flat), blue the sheen
            if (surf_eye.w > 0.5) c = vec3(0.25 + 0.5 * (m - 1.0), 0.25, 0.25 + 0.5 * sheen);
            else c = min(c * m, vec3(1.0));
        }
#ifdef SUN_SHADOW
        {
            // only the ground receives (faces turned up towards the camera: the road, the verges; not walls, nor a
            // bridge's underside). The track is prelit with its shadows in the vertex colours, so the darkening never
            // goes below what is baked there: the receiver ends at min(its colour, the open road's x (1 - sun_p.w)),
            // the open road's level estimated from its vertex light against sun_q.x, the route's open-road light
            // (inside a baked shadow nothing changes; at a baked shadow's edge it blends; a brighter receiver takes
            // the open road's factor). The sun's specular (the frame alpha the full-screen
            // specular reads, opaque draws only) and the sheen leave the shadow alike.
            float ln = length(shn);
            vec3 nv = ln > 1e-14 ? shn / ln : vec3(0.0);
            if (dot(nv, surf_eye.xyz - v_wpos) < 0.0) nv = -nv;
            float up = smoothstep(0.55, 0.8, nv.y);
            float sh = up > 0.0 ? up * sun_shadow_amount(sun_shadow_map, (sun_vp * vec4(v_wpos, 1.0)).xyz, sun_p) : 0.0;
            if (sun_p.w < -1.5) {   // B3_SUN_SHADOW_DEBUG=2: the vertex colour's luminance (grey), blended layers in blue
                float l = luma(v_color.rgb) * 0.5;
                c = gs.z > 0.5 ? vec3(0.0, 0.0, 0.5) : vec3(l);
            } else if (sun_p.w < 0.0) {   // B3_SUN_SHADOW_DEBUG=1 (diagnostics): red the shadow, green the box, blue the ground test
                vec3 q = (sun_vp * vec4(v_wpos, 1.0)).xyz;
                float inbox = abs(q.x) < 1.0 && abs(q.y) < 1.0 ? 1.0 : 0.0;
                c = vec3(sh, 0.3 * inbox * (0.5 + 0.5 * fract(q.z * 20.0)), 0.3 * up);
            } else {
                // (a receiver brighter than the open road, bright concrete, takes the open road's factor, not more)
                float m = mix(1.0, clamp((1.0 - sun_p.w) * sun_q.x / max(luma(v_color.rgb), 1e-3), 1.0 - sun_p.w, 1.0), sh);
                c *= m;
                if (gs.z < 0.5) oa *= m;
                sheen *= m;
            }
        }
#endif
        if (fog_colour.a > 0.5 && params.w < 1.5) {
            // The fog pass runs after the full-screen specular; for opaque draws fogging here and scaling the alpha
            // the specular pass reads by (1 - f) gives the same frame: mix(C + S Ad, F, f) = mix(C, F, f) + (1-f) S Ad.
            float f = min(fog_amount(v_w), 1.0);
            c = mix(c, fog_colour.rgb, f);
            if (gs.z < 0.5) oa *= 1.0 - f;
            sheen *= 1.0 - f;
        }
        oa += sheen;
        // reflect-tunnel (b3gfx::EnvPass): in the car env map's faces an opaque track draw covers the sky behind it
        if (light_dir.w > 0.5 && gs.z < 0.5) oa = 1.0;
        out_color = vec4(c, oa);
        return;
    }
    vec4 c = v_color * tint * texture(tex, v_uv);
    if (c.a < params.x) discard;
    float ndl = abs(dot(normalize(v_normal), light_dir.xyz));
    c.rgb *= mix(1.0, 0.45 + 0.55 * ndl, params.y);
    // Frame alpha 0 for opaque simple draws: the full-screen specular pass adds S x Ad, and these draws (cars until the
    // car model is wired in) carry no reflectivity in the game model (render4: b3replay mixes them into track frames).
    // The present pass writes alpha 1, so pictures are unchanged.
    out_color = vec4(c.rgb, params.z > 0.5 ? c.a : 0.0);
}
