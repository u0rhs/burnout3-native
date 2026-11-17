#version 450
// Depth of field (photo2; photo mode only, FrameParams::Dof): a bokeh-style gather blur on the frame's depth.
//   mode 0  prep, into the half-size buffer W: rgb = the 2 x 2 average of the frame (one bilinear tap at W's texel
//           centre), a = the view distance (m) of the nearest of the four frame pixels (the depth pre-pass, reverse-Z)
//   mode 1  gather, W -> G: a golden-angle spiral over the disc of the largest blur, each tap counted when its own blur
//           circle reaches the pixel; taps behind a sharper pixel are limited to twice its circle (no background
//           bleeding onto an in-focus subject), taps in front are not (a blurred foreground spreads over what is
//           behind it); a = the share of the result that came from such a nearer, blurred foreground
//   mode 2  composite over the frame (alpha blend, RGB only): G where the pixel's own circle is larger than about a
//           pixel, or where a blurred foreground covers it; the frame itself where it is in focus
// Blur circle (signed, W pixels): R x clamp((z - F) / z, -1, 1) for focus distance F: the thin lens's 1 - F / z, near
// negative. F <= 0: the median view distance of five frame pixels around the autofocus point (the screen centre, or
// the subject: photo mode puts it on the player's car) x focus scale.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;     // mode 0: the frame; 1: W (bilinear); 2: G
layout(set = 2, binding = 1) uniform sampler2D src_n;   // mode 1: W (nearest)
layout(set = 2, binding = 2) uniform sampler2D depth;   // the depth pre-pass (nearest)
layout(set = 3, binding = 0) uniform DofUniforms {
    vec4 a;   // x: mode, y: R (largest circle radius, W pixels), z: the spiral's radius step, w: F (m; <= 0: autofocus)
    vec4 b;   // row 3 of the inverse view-projection: view distance = 1 / dot(b, (ndc x, ndc y, depth, 1))
    vec4 c;   // xy: W's texel size (uv), zw: the frame's texel size (uv)
    vec4 d;   // x: autofocus scale, y: the largest tap count, z: 1 = the circles in false colour (B3GFX_DOF_DEBUG=1)
    vec4 e;   // xy: the autofocus point (uv)
};

float view_z(vec2 uv, float dz) {
    float w = dot(b, vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, dz, 1.0));
    return w > 1.0 / 60000.0 ? min(1.0 / w, 60000.0) : 60000.0;   // the cleared far plane (0): "infinitely" far
}
float depth_z(vec2 uv) { return view_z(uv, texture(depth, uv).r); }

float focus() {
    if (a.w > 0.0) return a.w;
    vec2 p = e.xy;
    float s0 = depth_z(p), s1 = depth_z(p + vec2(-0.01, 0.0)), s2 = depth_z(p + vec2(0.01, 0.0));
    float s3 = depth_z(p + vec2(0.0, -0.015)), s4 = depth_z(p + vec2(0.0, 0.015));
    // the median of five (a stray far or near pixel at the centre does not move the focus)
    float lo = min(s0, s1), hi = max(s0, s1);
    float lo2 = min(s2, s3), hi2 = max(s2, s3);
    float m1 = max(lo, lo2), m2 = min(hi, hi2);   // the 2nd and 3rd of s0..s3 lie in [m1, m2]
    float med = clamp(s4, min(m1, m2), max(m1, m2));
    return max(0.05, med * d.x);
}
float coc(float z, float f) { return a.y * clamp((z - f) / z, -1.0, 1.0); }

void main() {
    int mode = int(a.x + 0.5);
    if (mode == 0) {
        vec3 rgb = texture(src, v_uv).rgb;
        vec2 h = 0.5 * c.zw;
        float d0 = max(max(texture(depth, v_uv + vec2(-h.x, -h.y)).r, texture(depth, v_uv + vec2(h.x, -h.y)).r),
                       max(texture(depth, v_uv + vec2(-h.x, h.y)).r, texture(depth, v_uv + vec2(h.x, h.y)).r));   // reverse-Z: nearest
        out_color = vec4(rgb, view_z(v_uv, d0));
    } else if (mode == 1) {
        float f = focus();
        vec4 c0 = texture(src_n, v_uv);
        float zc = c0.a;
        float size_c = abs(coc(zc, f));
        vec3 acc = texture(src, v_uv).rgb;
        float tot = 1.0, nearw = 0.0;
        float r = a.z, ang = 0.0;
        int n = int(d.y);
        for (int i = 0; i < n && r < a.y; ++i) {
            vec2 tc = v_uv + vec2(cos(ang), sin(ang)) * (r * c.xy);
            float zs = texture(src_n, tc).a;
            vec3 cs = texture(src, tc).rgb;
            float ks = coc(zs, f);
            float size_s = abs(ks);
            if (zs > zc) size_s = min(size_s, size_c * 2.0);
            float m = smoothstep(r - 0.5, r + 0.5, size_s);
            acc += mix(acc / tot, cs, m);
            tot += 1.0;
            if (ks < 0.0 && zs < zc) nearw += m;
            r += a.z / r;
            ang += 2.39996323;   // the golden angle
        }
        out_color = vec4(acc / tot, clamp(2.0 * nearw / tot, 0.0, 1.0));
    } else {
        float f = focus();
        float ks = coc(depth_z(v_uv), f), k = abs(ks);
        if (d.z > 0.5) {   // B3GFX_DOF_DEBUG=1: the circle, red in front of the focus, green behind, black in focus
            out_color = vec4(0.5 * max(-ks, 0.0) / a.y, 0.5 * max(ks, 0.0) / a.y, 0.0, 1.0);
            return;
        }
        vec4 g = texture(src, v_uv);
        out_color = vec4(g.rgb, max(smoothstep(0.25, 1.0, k), g.a));
    }
}
