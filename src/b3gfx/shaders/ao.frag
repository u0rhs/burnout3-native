#version 450
// surface (remaster prototype, FrameParams::Surface::ao; off by default): screen-space ambient occlusion from the depth
// pre-pass (the one photo mode's depth of field draws: every Z-writing draw, cars included), for contact shading where
// the cars meet the road, in corners and under barriers. The track is prelit, so its baked light already holds the big
// shapes; this only adds the small-scale contact the PS2 could not.
//   mode 0  at half size: r = the visibility (1 = open), g = the view distance (m; 0 = the sky / far plane). Twelve
//           taps on a disc of `radius` metres around the pixel (projected), rotated per pixel; a tap occludes by how far
//           it lies above the pixel's plane (the normal from the depth's nearer neighbours), fading out past the radius.
//   mode 3  (nightfix) the half-size blur: a depth-aware 4 x 4 box of mode 0's result into a second half-size buffer.
//           Mode 0 rotates its disc by a 4 x 4 ordered pattern (16 rotations, each once in any 4 x 4 block), so the
//           box takes every rotation once: the pattern cancels exactly and nothing of it is left on screen, still or
//           moving (1.3.0's per-pixel interleaved gradient noise with a 3 x 3 filter left diagonal streaks and grain).
//   mode 1  the composite over the opaque scene (before the full-screen specular): the blurred half-size visibility
//           upsampled by its four nearest texels (bilinear weights x a depth test, so an edge keeps its own side),
//           multiplied into the frame's RGB (DST_COLOR x src); the frame alpha is kept.
//   mode 2  the same, written as is (the debug view)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D depth;   // the depth pre-pass (nearest, clamped)
layout(set = 2, binding = 1) uniform sampler2D aobuf;   // mode 1/2/3: the half-size visibility (nearest; 1/2: blurred)
layout(set = 3, binding = 0) uniform AoUniforms {
    mat4 inv;      // the inverse view-projection
    mat4 vp;       // the view-projection
    vec4 a;        // x: mode, y: strength, z: radius (m)
    vec4 b;        // xy: the depth's texel size (uv), zw: the half-size buffer's texel size (uv)
    vec4 right;    // xyz: the camera's right vector
    vec4 eye;      // xyz: the camera
};

float dep(vec2 uv) { return textureLod(depth, uv, 0.0).r; }
vec3 world(vec2 uv, float d) {
    vec4 p = inv * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);
    return p.xyz / p.w;
}

void main() {
    int mode = int(a.x + 0.5);
    if (mode == 0) {
        float d = dep(v_uv);
        if (!(d > 0.0)) { out_color = vec4(1.0, 0.0, 0.0, 1.0); return; }
        vec3 P = world(v_uv, d);
        vec2 tx = b.xy;
        vec3 pr = world(v_uv + vec2(tx.x, 0.0), dep(v_uv + vec2(tx.x, 0.0)));
        vec3 pl = world(v_uv - vec2(tx.x, 0.0), dep(v_uv - vec2(tx.x, 0.0)));
        vec3 pd = world(v_uv + vec2(0.0, tx.y), dep(v_uv + vec2(0.0, tx.y)));
        vec3 pu = world(v_uv - vec2(0.0, tx.y), dep(v_uv - vec2(0.0, tx.y)));
        vec3 dx = dot(pr - P, pr - P) < dot(P - pl, P - pl) ? pr - P : P - pl;
        vec3 dy = dot(pd - P, pd - P) < dot(P - pu, P - pu) ? pd - P : P - pu;
        vec3 N = cross(dx, dy);
        float nl = length(N);
        float z = length(P - eye.xyz);
        // a neighbour on the far plane (the sky: depth 0, an infinite point) gives an infinite or NaN normal: no occlusion
        if (!(nl > 0.0 && nl < 1e30)) { out_color = vec4(1.0, z, 0.0, 1.0); return; }
        N /= nl;
        if (dot(N, eye.xyz - P) < 0.0) N = -N;
        float R = a.z;
        vec4 c0 = vp * vec4(P, 1.0), c1 = vp * vec4(P + right.xyz * R, 1.0);
        float rx = abs(c1.x / c1.w - c0.x / c0.w) * 0.5;      // the radius in uv (x)
        rx = min(rx, 0.12);
        float asp = b.y / b.x;                                // width / height
        if (rx < 2.0 * b.x) { out_color = vec4(1.0, z, 0.0, 1.0); return; }
        // the 4 x 4 ordered (Bayer) index of this half-size pixel: the disc's rotation and the taps' radial offset
        ivec2 q = ivec2(gl_FragCoord.xy) & 3;
        const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        float noise = (bayer[q.y * 4 + q.x] + 0.5) / 16.0;
        float occ = 0.0;
        const int n = 12;
        for (int i = 0; i < n; ++i) {
            float t = (float(i) + fract(noise * 4.0 + 0.5)) / float(n);
            float ang = float(i) * 2.39996323 + noise * 6.2831853;
            vec2 uv = v_uv + vec2(cos(ang), sin(ang)) * vec2(rx, rx * asp) * t;
            float ds = dep(uv);
            if (!(ds > 0.0)) continue;
            vec3 v = world(uv, ds) - P;
            float vv = dot(v, v);
            float h = dot(v, N) * inversesqrt(vv + 1e-6);
            occ += max(h - 0.1, 0.0) * clamp(1.0 - vv / (R * R), 0.0, 1.0);
        }
        out_color = vec4(clamp(1.0 - a.y * 2.5 * occ / float(n), 0.0, 1.0), z, 0.0, 1.0);
        return;
    }
    if (mode == 3) {   // the half-size blur (above): 16 taps, each weighted by how close its distance is to this one's
        ivec2 ip = ivec2(gl_FragCoord.xy);
        ivec2 mx = textureSize(aobuf, 0) - 1;
        vec2 c = texelFetch(aobuf, ip, 0).rg;
        if (!(c.g > 0.0)) { out_color = vec4(1.0, 0.0, 0.0, 1.0); return; }
        float s = 0.0, w = 0.0;
        for (int j = -2; j <= 1; ++j)
            for (int i = -2; i <= 1; ++i) {
                vec2 o = texelFetch(aobuf, clamp(ip + ivec2(i, j), ivec2(0), mx), 0).rg;
                float k = o.g > 0.0 ? exp(-abs(o.g - c.g) / (0.03 * c.g + 0.1)) : 0.0;
                s += o.r * k;
                w += k;
            }
        out_color = vec4(w > 1e-4 ? s / w : c.r, c.g, 0.0, 1.0);
        return;
    }
    float d = dep(v_uv);
    float ao = 1.0;
    if (d > 0.0) {
        float z = length(world(v_uv, d) - eye.xyz);
        // the four nearest half-size texels: bilinear weights x the depth test
        vec2 p = v_uv / b.zw - 0.5;
        vec2 f = fract(p);
        vec2 base = (floor(p) + 0.5) * b.zw;
        float s = 0.0, w = 0.0;
        for (int j = 0; j <= 1; ++j)
            for (int i = 0; i <= 1; ++i) {
                vec2 o = textureLod(aobuf, base + vec2(float(i), float(j)) * b.zw, 0.0).rg;
                float bw = (i == 0 ? 1.0 - f.x : f.x) * (j == 0 ? 1.0 - f.y : f.y);
                float k = o.g > 0.0 ? (bw + 1e-3) * exp(-abs(o.g - z) / (0.02 * z + 0.05)) : 0.0;
                s += o.r * k;
                w += k;
            }
        ao = w > 1e-5 ? s / w : 1.0;
    }
    // mode 2 (debug): the visibility as the picture, in frame units (the present pass doubles them: 1 = white)
    out_color = vec4(vec3(mode == 2 ? ao * (128.0 / 255.0) : ao), 1.0);
}
