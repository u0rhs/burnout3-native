#version 450
// b3gfx mesh fragment shader. SDL_GPU bindings: fragment samplers in set 2, uniform buffers in set 3.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_normal;
layout(location = 3) in float v_w;
layout(location = 4) noperspective in vec2 v_line;   // line meshes: distance from the centre line, half width (pixels)

layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(set = 3, binding = 0) uniform FragmentUniforms {
    vec4 tint;       // RGBA multiplier (simple model); track model: a >= 0 = blend by this constant (GS ALPHA FIX)
    vec4 light_dir;  // xyz: direction to the light (normalized); w: 1 = an env-map face (b3gfx::EnvPass: opaque alpha 1)
    vec4 params;     // x: alpha reference (discard below), y: lighting amount, z: 1 = keep alpha / 0 = opaque,
                     // w: 1 = track model
    vec4 gs;         // track model: x: GS TEST_1, y: 1 = textured, z: 1 = blended draw (keeps its alpha), w: the sharp alpha threshold (0: off)
                     // coverage alpha (magnified HUD fonts, hud2)
    vec4 fog;        // z_scale, z_shift, fog_max, fog_sky
    vec4 fog_colour; // rgb in frame units, a = 1: fog on
};

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
        if (fog_colour.a > 0.5 && params.w < 1.5) {
            // The fog pass runs after the full-screen specular; for opaque draws fogging here and scaling the alpha
            // the specular pass reads by (1 - f) gives the same frame: mix(C + S Ad, F, f) = mix(C, F, f) + (1-f) S Ad.
            float f = min(fog_amount(v_w), 1.0);
            c = mix(c, fog_colour.rgb, f);
            if (gs.z < 0.5) oa *= 1.0 - f;
        }
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
