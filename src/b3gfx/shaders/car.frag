#version 450
#extension GL_GOOGLE_include_directive : require
#include "hash.glsl"   // d3dhash
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
layout(location = 6) in vec3 v_part;   // weather: part space (car.vert)
layout(location = 7) in vec3 v_pnrm;

layout(location = 0) out vec4 out_color;
#ifdef SCENE_DEPTH
layout(location = 1) out float out_depth;   // scenedepth (car_depth.frag): every car draw's depth, glass too, as the pre-pass
#endif

layout(set = 2, binding = 0) uniform sampler2D tex;      // the mesh's texture slot (atlas / underside / glass)
layout(set = 2, binding = 1) uniform sampler2D envsky;   // the 64x32 sky backdrop (display units)
layout(set = 2, binding = 2) uniform sampler2D envworld; // reflect-tunnel: the track around the car (b3gfx::EnvPass), look3.z = 1
layout(set = 2, binding = 3) uniform sampler2D paintmask; // carpaint: the car's paint map (b3asset::CarPaintMap::mask), paint.w > 0

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
    vec4 look3;       // rim, paint colour keep, 1 = envworld bound, strength
    vec4 paint;       // carpaint: rgb the paint (texel units); w the mode: 0 off, bit 0 recolour, bit 1 decals off,
                      // bit 2 metallic, bit 3 pearl, both chrome (padpaint: the finish; options3: chrome)
    vec4 paint2;      // carpaint: rgb the second colour's paint; w the reference level (main paint's mean brightest channel)
    vec4 night;       // night mode (§17): x the SH light's scale, y the env map's, z the moon key light's gain; w: 1 = on
    vec4 moon;        // night mode: rgb the moonlight (frame units), towards to_sun.xyz (the moon at night); w (weather,
                      // §38): the rain's wetness 0..1 (the wet paint's gloss and beads; 0 dry: nothing)
    vec4 paint3;      // paint2: rgb the decals' colour (paint.w bit 4)
    vec4 paint4;      // paint2: rgb the wheel's tint (paint.w bit 5: a wheel's draws)
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
// normalize(v), or `f` for a zero (or non-finite) vector: normalize(0) is NaN, which the bloom would keep
vec3 unit_or(vec3 v, vec3 f) { float l = dot(v, v); return l > 1e-24 && l < 1e30 ? normalize(v) : f; }
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

// reflect-tunnel: the track part of the env map. The game draws the streamed track's LOD blocks into its env-map cameras
// around the followed car after the sky and the sun (RenderBeforeVehicles 0x21C780, cameras 1..3, far 100 m [L draft]):
// tunnels, bridges, walls and the road reflect, and the sky shows only where nothing within 100 m covers it. The faces
// are a 3 x 2 atlas (b3gfx::env_face: forward +X, -X, +Y, -Y, +Z, -Z), each drawn 1 texel wider than 90 degrees.
// rgb frame units, a = the track's coverage (0: sky).
vec4 env_world(vec3 d) {
    vec3 a = abs(d);
    vec3 F, R, U;
    int f;
    if (a.x >= a.y && a.x >= a.z) {
        f = d.x > 0.0 ? 0 : 1;
        F = vec3(d.x > 0.0 ? 1.0 : -1.0, 0.0, 0.0); R = vec3(0.0, 0.0, F.x); U = vec3(0.0, 1.0, 0.0);
    } else if (a.y >= a.z) {
        f = d.y > 0.0 ? 2 : 3;
        F = vec3(0.0, d.y > 0.0 ? 1.0 : -1.0, 0.0); R = vec3(F.y, 0.0, 0.0); U = vec3(0.0, 0.0, 1.0);
    } else {
        f = d.z > 0.0 ? 4 : 5;
        F = vec3(0.0, 0.0, d.z > 0.0 ? 1.0 : -1.0); R = vec3(-F.z, 0.0, 0.0); U = vec3(0.0, 1.0, 0.0);
    }
    float T = float(textureSize(envworld, 0).y) * 0.5;
    float k = T / (T - 2.0);
    vec2 ndc = vec2(dot(d, R), dot(d, U)) / (max(dot(d, F), 1e-6) * k);
    vec2 uv = vec2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y);
    vec2 st = (vec2(float(f % 3), float(f / 3)) + uv) / vec2(3.0, 2.0);
    vec4 w = textureLod(envworld, st, 0.0);
    return vec4(w.rgb, clamp(w.a, 0.0, 1.0));
}

// The game's env map: the sky (and sun) with the track in front of it where the world map is bound.
vec3 env_map(vec3 r) {
    vec3 c = env_sky(r);
    if (look3.z > 0.5) {
        vec4 w = env_world(normalize(r));
        c = mix(c, w.rgb, w.a);
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
    if (look3.z > 0.5) {
        // reflect-tunnel: where the track within 100 m covers the sky (a tunnel's walls and lights, a bridge, the road),
        // the gloss mirrors it as the game's map has it; no sun glint behind it
        vec4 w = env_world(d);
        c = mix(c, w.rgb, w.a);
        dark = mix(dark, 0.0, w.a);
        glint *= 1.0 - w.a;
    }
    return c;
}

// padpaint: the finish's flakes, one value per atlas cell (a hash: no texture)
// d3dhash: an integer cell (hash.glsl), its third component the stream (independent values for one cell)
float flake_hash(vec2 c, float stream) { return hash3i(vec3(c, stream)).x; }

// padpaint: `c` turned round the grey axis by `a` radians (its hue), its brightness kept
vec3 hue_turn(vec3 c, float a) {
    const vec3 k = vec3(0.57735);
    float cs = cos(a), sn = sin(a);
    return c * cs + cross(k, c) * sn + k * dot(k, c) * (1.0 - cs);
}

void main() {
#ifdef SCENE_DEPTH
    out_depth = gl_FragCoord.z;
#endif
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
    int pm = 0;
    vec4 m = vec4(0.0);
    if (paint.w > 0.5) {
        // carpaint (docs/remaster-features.md §15): the mask's R main paint, G second colour, B decal, A the shade / 2
        m = texture(paintmask, v_uv);
        pm = int(paint.w + 0.5);
        float sh = max(t.r, max(t.g, t.b)) / max(paint2.w, 1.0 / 255.0);
        vec3 c = t.rgb;
        if ((pm & 1) != 0) {
            c = mix(c, paint.rgb * sh, m.r);
            c = mix(c, paint2.rgb * sh, m.g);
        }
        if ((pm & 2) != 0) c = mix(c, paint.rgb * (2.0 * m.a), min(m.g + m.b, 1.0));
        else if ((pm & 16) != 0) c = mix(c, paint3.rgb * sh, min(m.g + m.b, 1.0));   // paint2: the decals in their colour
        if ((pm & 32) != 0) {   // paint2: a wheel's rim tinted (its bright texels: the tyre and the darker disc and hub stay)
            float tm = max(t.r, max(t.g, t.b));
            c = mix(c, paint4.rgb * (tm / (120.0 / 255.0)), smoothstep(0.22, 0.38, tm));
        }
        t.rgb = min(c, vec3(1.0));
    }
    const bool glass_mesh = mode.x < 0.5 && mode.y < 0.99;
    const bool enh = refl.x > 0.0 && mode.z > 0.5 && mode.z < 1.5 && env_colour.r > 100.0;   // CAR REFLECTIONS: Enhanced
    vec3 light = v_colour.rgb / 128.0;
    if (night.w > 0.5) {
        // night mode: the daylight SH dimmed, the moon as a key light on the faces turned to it
        float nl = max(dot(unit_or(v_nrm, vec3(0.0, 1.0, 0.0)), to_sun.xyz), 0.0);
        light = light * night.x + moon.rgb * (night.z * nl);
    }
    vec3 cs = min(t.rgb * light, vec3(1.0));
    float a = min(floor(t.a * 128.0 * v_colour.a / 128.0), 255.0) / 128.0;  // As / 128 (texel alpha 0x80 = 1.0)
    float k = mode.x > 0.5 ? a : mode.y;
    vec3 X = k * cs;
    float keep = 1.0 - k;
    if (mode.z > 0.5) {
        vec3 ce = min(env_map(v_refl) * (env_colour.rgb / 128.0), vec3(1.0));
        if (night.w > 0.5) ce *= night.y;   // night mode: the daylight env map dimmed
        if (mode.z < 1.5) { X = X * (1.0 - a) + a * ce; keep *= 1.0 - a; }
        else X += a * ce;
    }
    if (enh) {
        // enhanced: a gloss added on top of the result above, which is exactly what Original draws (the game's own atlas +
        // env pass: the car's colour); with the gloss strength 0 the pixel is Original's. v_colour.rgb is the SH irradiance lighting (dark in shade, tunnels
        // and at night): the sky is not shown where the car is not under it.
        float lit = clamp(dot(v_colour.rgb, vec3(0.3333)) / 128.0, 0.0, 1.0);
        vec3 V = -unit_or(v_view, vec3(0.0, 0.0, -1.0)), N = unit_or(v_nrm, V);
        float cs2 = clamp(abs(dot(N, V)), 0.0, 1.0);
        float fr = pow(1.0 - cs2, refl.z);
        float open = refl.y;   // no sky in a tunnel or under cover
        bool glass = glass_mesh;
        float mask = refl.x * look3.w * smoothstep(0.30, 0.85, t.a);
        float dark;
        float glint;
        vec3 E = env_sharp(unit_or(v_refl, N), lit, open, dark, glint);
        if (night.w > 0.5) E *= night.y;   // night mode: the sky in the gloss dimmed (the moon's glint kept)
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
    // (skipped where a normal, the view or the reflection is degenerate: normalize would give NaN)
    if ((pm & 12) != 0 && !glass_mesh && dot(v_nrm, v_nrm) > 1e-12 && dot(v_view, v_view) > 1e-12 && dot(v_refl, v_refl) > 1e-12) {
        // padpaint: the finish, on the paint only (the mask's main paint and second colour; decals off: those too), over
        // whatever the reflections above gave. The paint's own colour (X) carries it: never a colour of its own
        float pw = clamp(m.r + m.g + ((pm & 2) != 0 ? m.b : 0.0), 0.0, 1.0) * smoothstep(0.30, 0.85, t.a);
        vec3 N = normalize(v_nrm), V = -normalize(v_view);
        float ndv = clamp(abs(dot(N, V)), 0.0, 1.0);
        float lit = clamp(dot(v_colour.rgb, vec3(0.3333)) / 128.0, 0.0, 1.0);
        vec3 R = normalize(v_refl);
        float mx = max(max(X.r, X.g), X.b);
        vec3 hue = mx > 0.02 ? X / mx : vec3(1.0);
        if ((pm & 12) == 4) {
            // metallic: the sky mirrored in the paint's colour, strongest towards the edges, and flakes that catch the sun
            float fr = 0.35 + 0.65 * pow(1.0 - ndv, 2.0);
            vec3 E = env_map(R);
            float Ls = dot(E, vec3(0.3333));
            X = X * (1.0 - 0.25 * pw * fr) + pw * fr * (0.55 * Ls + 0.10 * lit) * hue;
            vec2 cell = floor(v_uv * 900.0);
            float h = flake_hash(cell, 8.0 + floor(ndv * 6.0));   // the flakes that face the eye change as it moves
            float flake = smoothstep(0.93, 0.995, h);
            float sun = to_sun.w > 0.5 ? pow(max(dot(R, to_sun.xyz), 0.0), 6.0) : 0.0;
            X += pw * flake * (0.06 + 0.45 * sun) * (0.4 + 0.6 * lit) * mix(hue, vec3(1.0), 0.6);
        }
        if ((pm & 12) == 12) {
            // chrome (options3: PAINT FINISH Chrome): a mirror -- the world around the car
            // (the env map) at nearly full strength, a little of the paint's hue in it, and a hard sun glint
            float fr = 0.80 + 0.20 * pow(1.0 - ndv, 3.0);
            vec3 E = env_map(R);
            vec3 mirror = E * mix(vec3(1.0), hue, 0.25) * (0.85 + 0.30 * lit);
            X = mix(X, mirror, pw * fr);
            float sun = to_sun.w > 0.5 ? pow(max(dot(R, to_sun.xyz), 0.0), 48.0) : 0.0;
            X += pw * sun * 0.9 * vec3(1.0);
        }
        if ((pm & 12) == 8) {
            // pearl: the hue turns with the angle to the eye (face-on the paint's own; at the edges about a sixth of the
            // way round the colour wheel), with a soft white sheen there
            float edge = pow(1.0 - ndv, 2.0);
            vec3 turned = hue_turn(X, 1.0 * edge);
            X = mix(X, max(turned, vec3(0.0)), pw);
            X += pw * edge * edge * 0.18 * (0.3 + 0.7 * lit) * vec3(1.0);
        }
    }
    // (the beads' coordinates: 4 cm cells in part space on the plane the face is turned to, so they stay on the body as it
    // moves whatever its texture mapping; their size on screen taken here, in uniform control flow, for the branch below)
    vec3 an = abs(v_pnrm);
    vec2 bq = (an.x > an.y && an.x > an.z ? v_part.yz : an.y > an.z ? v_part.xz : v_part.xy) / 0.04;
    float bsize = max(fwidth(bq.x), fwidth(bq.y));
    // weather (§38): a wet car. The paint's clearcoat mirrors more of the (grey) sky, by a Fresnel term; beads of water on
    // the paint and the glass, in the car's own texture space (they stay on the body as it moves), more on faces turned
    // up, each a soft highlight and a darker rim; beads smaller than a pixel fade out (no shimmer at a distance)
    if (moon.w > 0.0 && dot(v_nrm, v_nrm) > 1e-12 && dot(v_view, v_view) > 1e-12 && dot(v_refl, v_refl) > 1e-12) {
        vec3 N = normalize(v_nrm), V = -normalize(v_view);
        float ndv = clamp(abs(dot(N, V)), 0.0, 1.0);
        float lit = clamp(dot(v_colour.rgb, vec3(0.3333)) / 128.0, 0.0, 1.0);
        float body = smoothstep(0.15, 0.40, t.a);   // (the atlas' paint and glass, not its holes: alpha 0x80 reads ~0.5)
        float Ls = dot(env_map(normalize(v_refl)), vec3(0.3333));
        if (night.w > 0.5) Ls *= night.y;
        float fr = 0.04 + 0.96 * pow(1.0 - ndv, 5.0);
        X += moon.w * body * fr * 0.35 * Ls * (0.4 + 0.6 * lit) * vec3(1.0);
        vec2 q = bq;
        vec2 cell = floor(q);
        float h = flake_hash(cell, 1.0);
        float upf = 0.35 + 0.65 * smoothstep(-0.2, 0.8, N.y);   // (more beads on the bonnet and the roof)
        float size = bsize;
        if (h > 1.0 - 0.22 * upf && size < 0.9) {
            vec2 o = vec2(flake_hash(cell, 2.0), flake_hash(cell, 3.0)) * 0.4 - 0.2;
            float r = 0.24 + 0.18 * flake_hash(cell, 4.0);
            vec2 f = (fract(q) - 0.5 - o) / r;
            float d = length(f);
            if (d < 1.0) {
                float vis = moon.w * body * (1.0 - smoothstep(0.45, 0.9, size));
                float hl = 1.0 - smoothstep(0.0, 0.6, length(f - vec2(-0.3, 0.3)));
                X = X * (1.0 - vis * 0.35 * smoothstep(0.6, 1.0, d)) + vis * hl * (0.10 + 0.20 * lit) * vec3(1.0);
            }
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
