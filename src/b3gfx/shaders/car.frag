#version 450
// b3gfx car fragment shader: the GS for the car program's two passes, folded into one draw (docs/native-renderer.md
// §12.6). Frame units as the track's (texel bytes / 255, the present pass scales by 255/128).
//   pass 1 (context 1): Cs = Ct x Cf >> 7 (MODULATE, clamped), As = At x Af >> 7; ALPHA_1 (Cs - Cd) C + Cd with
//     C = FIX (0x64) or As (0x44); destination alpha := As.
//   pass 2 (context 2, same pixels, no Z write): Ce = env texel x 1012 >> 7; ALPHA_2 0x54 Cd = lerp(Cd, Ce, Ad),
//     0x58 Cd += Ce Ad, 0x6A unchanged; destination alpha := the env texel's alpha.
// Both passes are linear in the colour behind the car, Cd0: result = X + keep Cd0, so the shader outputs X with
// alpha 1 - keep and the pipeline blends ONE, ONE_MINUS_SRC_ALPHA. The frame alpha left for the full-screen specular
// is written 0: the env map's alpha (sky components 4/5 and the sun blob write none [L]) after an env pass, and the
// pass-1 alpha of the meshes without one (lights on, mask bit 1 with C = FIX) is 0 in every case [C car-shading.md §3].
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_colour;
layout(location = 2) in vec3 v_refl;
layout(location = 3) in float v_w;
layout(location = 4) in vec3 v_nrm;
layout(location = 5) in vec3 v_view;

layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D tex;      // the mesh's texture slot (atlas / underside / glass)
layout(set = 2, binding = 1) uniform sampler2D envsky;   // the 64x32 sky backdrop (display units)

layout(set = 3, binding = 0) uniform CarFragmentUniforms {
    vec4 env_colour;  // VU1 1012 (GS units)
    vec4 mode;        // x: 1 = ALPHA_1 C = As, y: FIX / 128, z: 0 no env pass, 1 ALPHA_2 0x54, 2 0x58; w: 1 = env sky bound
                      // (display units), 2 = an env map in frame units bound (fe2: the showroom's, baked)
    vec4 to_sun;      // xyz towards the sun; w: 1 = sun blob in the env map
    vec4 sun_colour;  // frame units; w: 1 = solid, in (0, 1) the screen-door fade (mac5), < 0 -opacity (traffic-popin)
    vec4 fog;         // z_scale, z_shift, fog_max, fog_sky (as mesh.frag)
    vec4 fog_colour;  // rgb frame units, a = 1: fog on
    vec4 refl;        // x: enhanced reflections' strength (0: the game's env pass as is), y: the sky the car sees (0..1),
                      // z: Fresnel power, w: Fresnel floor
    vec4 look0;       // paint gain, paint darkening, glint size, glint gain          (b3gfx::CarLook)
    vec4 look1;       // sky gain, horizon gain, ground level, haze
    vec4 look2;       // glass weight, glass Fresnel, glass gain, glass darkening
    vec4 look3;       // rim, paint colour keep, -, strength
};

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

// The car environment map's content: RenderSkyToEnvironmentMap 0x2BC140 draws sky components 4 and 5 [C matched];
// component 4 is component 1's dome (u = azimuth turns, v = 0.765625 - 0.75 h, h = 2e - e^2 on the dome of radius
// 1 - e^2) over the backdrop [C table 0x4B9CA0], below the horizon component 3's mapping (v = 0.765625 + 0.25 h) [G: the
// game does not draw 3 into the env map]; component 5 (uv mode 1, blended) and the sphere-map warp itself are not
// modelled [G]. Looked up by the reflected ray directly instead of through the 256x256 sphere map (same directions,
// without the map's resolution loss towards its rim).
vec3 env_sky(vec3 r) {
    vec3 d = normalize(r);
    float hor = length(d.xz);
    float u = atan(d.z, d.x) * 0.15915494;
    float t = abs(d.y) / max(hor, 1e-6);
    // the dome point along d: (2e - e^2) / (1 - e^2) = t
    float e = abs(t - 1.0) < 1e-4 ? 0.5 : (sqrt(1.0 + t * t - t) - 1.0) / (t - 1.0);
    e = clamp(e, 0.0, 1.0);
    float h = 2.0 * e - e * e;
    float v = d.y >= 0.0 ? 0.765625 - 0.75 * h : 0.765625 + 0.25 * h;
    v = clamp(v, 0.5 / 32.0, 1.0 - 0.5 / 32.0);
    vec3 c = mode.w > 0.5 ? textureLod(envsky, vec2(u, v), 0.0).rgb * (mode.w > 1.5 ? 1.0 : 0.5) : vec3(0.0);  // display units -> frame
    if (to_sun.w > 0.5) {
        // RenderSunToEnvironmentMap 0x2BC070 [C name]: a sun blob; size and falloff as the main view's [G]
        float s = dot(d, to_sun.xyz);
        float blob = smoothstep(cos(0.075), cos(0.0375), s);
        c = mix(c, min(sun_colour.rgb, vec3(1.0)), blob);
    }
    return c;
}


// Enhanced car reflections (the PC port's own, presentation only; refl.x > 0): a clearcoat over the game's lit paint that
// keeps the paint's colour (the reflection brightens or darkens it, never tints). The reflected view ray looks up the same sky map the game's pass samples, with contrast: clouds
// and the bright haze just above a hard horizon line, a dark ground below it, a hard sun glint. Frame units (1.0 = 128).
// env_light(): the sky's brightness along d (>= 0, 1 = a bright cloud); env_tint(): its colour, kept saturated.
vec3 env_sharp(vec3 d, float lit, float open, out float dark, out float glint) {
    glint = 0.0;
    vec3 sky = env_sky(d);
    vec3 hz = env_sky(normalize(vec3(d.x, 0.05, d.z)));
    float lum = dot(sky, vec3(0.3333));
    vec3 tint = normalize(sky + vec3(0.02)) * 1.7;                          // the colour of the sky, not its grey
    vec3 c;
    if (d.y >= 0.0) {
        float band = exp(-d.y * 9.0);                                      // the haze hugging the horizon
        float cloud = smoothstep(0.18, 0.55, lum);
        c = tint * (0.18 + look1.x * cloud * cloud + look1.y * band * band) * (0.5 + dot(hz, vec3(0.6667)));
        dark = 0.0;
    } else {
        c = tint * look1.z;                                                   // the road and verge: a dark mirror
        dark = smoothstep(0.0, -0.12, d.y);
    }
    if (to_sun.w > 0.5) {
        float sd = max(dot(d, to_sun.xyz), 0.0);
        float disc = smoothstep(cos(0.075 * look0.z), cos(0.04 * look0.z), sd);                // a hard sun glint
        glint = (1.0 * disc + 0.08 * pow(sd, 50.0)) * open;                // white, added as light only
    }
    c += vec3(look1.w);                                                    // a uniform haze (0 by default)
    c = mix(vec3(0.03) + 0.06 * lit, c, mix(0.15, 1.0, open));             // under cover: dark surroundings, not a sky
    dark = mix(0.5, dark, open);
    glint *= open;
    return c;
}

void main() {
    // mac5: a screen-door fade (the Time Attack ghost's see-through look): a 4 x 4 ordered threshold against the fade,
    // so the car stays opaque in the depth buffer and needs no sorting
    // traffic-popin: traffic coming and going uses the opacity instead (w < 0): the 4 x 4 pattern read as a speckled,
    // flickering block on a car a few pixels across, i.e. still as a pop
    float door = sun_colour.w, opacity = 1.0;
    if (door < 0.0) { opacity = -door; door = 1.0; }
    if (door < 1.0) {
        // the 4 x 4 Bayer index from the pixel's bits (no constant array: lavapipe's handling of one was suspect)
        uint x = uint(gl_FragCoord.x) & 3u, y = uint(gl_FragCoord.y) & 3u, xy = x ^ y;
        uint b = ((xy & 1u) << 3) | ((x & 1u) << 2) | ((xy & 2u)) | ((x >> 1) & 1u);
        if (door <= (float(b) + 0.5) / 16.0) discard;
    }
    vec4 t = texture(tex, v_uv);
    const bool glass_mesh = mode.x < 0.5 && mode.y < 0.99;
    const bool enh = refl.x > 0.0 && mode.z > 0.5 && mode.z < 1.5 && env_colour.r > 100.0;   // CAR REFLECTIONS: Enhanced
    vec3 cs = min(t.rgb * (v_colour.rgb / 128.0), vec3(1.0));
    float a = min(floor(t.a * 128.0 * v_colour.a / 128.0), 255.0) / 128.0;  // As / 128 (texel alpha 0x80 = 1.0)
    float k = mode.x > 0.5 ? a : mode.y;
    vec3 X = k * cs;
    float keep = 1.0 - k;
    if (mode.z > 0.5) {
        vec3 ce = min(env_sky(v_refl) * (env_colour.rgb / 128.0), vec3(1.0));
        if (mode.z < 1.5) { X = X * (1.0 - a) + a * ce; keep *= 1.0 - a; }
        else X += a * ce;
    }
    if (enh) {
        // enhanced: a gloss added on top of the result above, which is exactly what Original draws (the game's own atlas +
        // env pass: the car's colour); with the gloss strength 0 the pixel is Original's. v_colour.rgb is the SH irradiance lighting (dark in shade, tunnels
        // and at night): the sky is not shown where the car is not under it.
        float lit = clamp(dot(v_colour.rgb, vec3(0.3333)) / 128.0, 0.0, 1.0);
        vec3 N = normalize(v_nrm), V = -normalize(v_view);
        float cs2 = clamp(abs(dot(N, V)), 0.0, 1.0);
        float fr = pow(1.0 - cs2, refl.z);
        float open = refl.y;   // no sky in a tunnel or under cover
        bool glass = glass_mesh;
        float mask = refl.x * look3.w * smoothstep(0.30, 0.85, t.a);
        float dark;
        float glint;
        vec3 E = env_sharp(normalize(v_refl), lit, open, dark, glint);
        float Ls = dot(E, vec3(0.3333));
        if (glass) {
            // dark glass, a strong sky in it and a bright rim at the edges
            float wg = mask * (look2.x + look2.y * fr);
            X = X * (1.0 - look2.w * mask) * (1.0 - wg) + wg * Ls * look2.z * mix(vec3(1.0), normalize(E + vec3(0.02)) * 1.7, 0.25) + mask * (fr * look3.x + glint * look0.w * 0.4);
            keep *= (1.0 - 0.30 * mask) * (1.0 - wg);
        } else {
            // paint: face-on stays rich (a low gloss), the sheen grows to the grazing edges; the reflection darkens the
            // body where it mirrors the ground, and adds light only where it mirrors something bright
            // the reflection is a scaling of the paint's own colour (same hue and saturation) plus a white glint: it never tints
            float F = mask * (refl.w + (1.0 - refl.w) * fr);
            float mx = max(max(X.r, X.g), X.b);
            vec3 tint = mx > 0.03 ? X / mx : vec3(1.0);
            tint = mix(E / max(max(max(E.r, E.g), E.b), 0.01), tint, look3.y);
            X = X * (1.0 - look0.y * F * dark) + F * Ls * look0.x * tint + F * glint * look0.w;   // (look0.y: an optional darkening, 0)
        }
    }
    // CB3Fog::Render is a post pass over the whole frame by Z [C]: fog the car's own contribution here; what shows
    // through glass carries the fog of its own depth (as for the track's blended draws).
    if (fog_colour.a > 0.5) {
        float f = min(fog_amount(v_w), 1.0);
        X = (1.0 - f) * X + f * fog_colour.rgb * (1.0 - keep);
    }
    out_color = vec4(X, 1.0 - keep);
    // traffic-popin: at `opacity` over the frame: the blend is X + keep Cd (premultiplied), so both scale
    if (opacity < 1.0) out_color *= opacity;
}
