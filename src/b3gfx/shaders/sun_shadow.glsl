// SUN SHADOWS (a remaster feature, off by default; docs/remaster-features.md §20): the cars' shadow map, looked up by
// a receiver. Included by mesh.frag's SUN_SHADOW variant (the opaque and blended track draws by day); the night light
// pass can include it as well (it then owns the shadow at night, so a headlight beam fills a moon shadow).
//
// The map (b3gfx's sun shadow pass, shaders/shadow.frag): R32F, sampled nearest and clamped; per texel the caster
// nearest to the light, as its light depth z (0..1 across the box, 0 on the light's side) + 2 x its gate k (0..255:
// the caster's sky_open, so a car in a tunnel or under a bridge casts nothing; 0: no caster, the clear value 1.0).
// The includer declares the sampler and passes it in.
//
// sun_shadow_amount(map, clip, p): 0 (lit) .. 1 (fully in a car's shadow, gated) at `clip`, the receiver's position in
// the map's clip space (sun_vp x world position: x, y -1..1 across the box, z 0..1). p: x the map's side (texels),
// y the height (m) the light drops over the box's depth (its depth x the light's sine of elevation), z 1: the 5 x 5
// kernel (ULTRA), else 3 x 3. Both kernels are box filters weighted bilinearly at their edges (a smooth penumbra of 3
// or 5 texels), from 4 or 9 gathers.

// One texel's caster against the receiver's light depth `ref`: the caster's gate where the receiver lies more than
// 3 cm below it (the bias: only cars cast, so nothing shadows itself), fading out 3 to 8 m below it (a car on a bridge
// does not shade the road under the bridge, a car in the air leaves the ground slowly). Heights: along the light x rise.
float sun_shadow_texel(float v, float ref, float depth_m) {
    float k = floor(v * 0.5);
    float dz = (ref - (v - 2.0 * k)) * depth_m;
    return (k * (1.0 / 255.0)) * step(0.03, dz) * (1.0 - smoothstep(3.0, 8.0, dz));
}

// The kernel's weight of the texel `d` texels from the footprint's base (d in [-r, r + 1]): the edge texels take the
// bilinear fractions, the others 1
float sun_shadow_weight(float d, float r, float f) { return d < -r + 0.5 ? 1.0 - f : (d > r + 0.5 ? f : 1.0); }

float sun_shadow_amount(sampler2D map, vec3 clip, vec4 p) {
    if (!(abs(clip.x) < 1.0 && abs(clip.y) < 1.0)) return 0.0;
    // the box's outer 10% fades out, so a shadow leaving the box does not end at a line
    float edge = 1.0 - smoothstep(0.9, 1.0, max(abs(clip.x), abs(clip.y)));
    vec2 uv = vec2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);   // SDL_GPU: NDC +y up, texture origin top-left
    float n = p.x;
    vec2 t = uv * n - 0.5;
    vec2 b = floor(t);
    vec2 f = t - b;
    float r = p.z > 0.5 ? 2.0 : 1.0;
    int gn = p.z > 0.5 ? 3 : 2;   // gathers per axis
    float sum = 0.0;
    for (int gy = 0; gy < 3; ++gy) {
        if (gy >= gn) break;
        for (int gx = 0; gx < 3; ++gx) {
            if (gx >= gn) break;
            // the pair of texels from base + 2 g - r: a gather at their shared corner (no rounding tie: t = corner - 0.5)
            vec2 d0 = vec2(float(2 * gx), float(2 * gy)) - r;
            vec4 g = textureGather(map, (b + d0 + 1.0) / n, 0);   // w: (x0, y0), z: (x1, y0), x: (x0, y1), y: (x1, y1)
            float wx0 = sun_shadow_weight(d0.x, r, f.x), wx1 = sun_shadow_weight(d0.x + 1.0, r, f.x);
            float wy0 = sun_shadow_weight(d0.y, r, f.y), wy1 = sun_shadow_weight(d0.y + 1.0, r, f.y);
            sum += wy0 * (wx0 * sun_shadow_texel(g.w, clip.z, p.y) + wx1 * sun_shadow_texel(g.z, clip.z, p.y)) +
                   wy1 * (wx0 * sun_shadow_texel(g.x, clip.z, p.y) + wx1 * sun_shadow_texel(g.y, clip.z, p.y));
        }
    }
    float side = 2.0 * r + 1.0;
    return sum / (side * side) * edge;
}
