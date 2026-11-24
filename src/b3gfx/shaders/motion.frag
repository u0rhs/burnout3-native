#version 450
#extension GL_GOOGLE_include_directive : require
#include "hash.glsl"   // d3dhash
// Motion blur (photoblur, 1.3.0; photo mode only, FrameParams::Motion): a streak per pixel from a synthetic motion of
// the camera and the cars over the shutter time, gathered over displayed frames (the frame is frozen).
//   mode 0  streak, into V (full size): the pixel's world position from the depth pre-pass (reverse-Z; the sky: far
//           along its ray), moved over the shutter: inside a car's box (nearest first) by that car's motion; the camera
//           at -T/2 / +T/2 offset by -cam / +cam and turned (rm / rp about the eye). Both ends reprojected with the frame's
//           view-projection: rg = half the streak (uv, the pixel at its middle; its length clamped to b.x pixels),
//           b = the box the pixel lies in + 1 (0: none), a = its distance from the eye (m)
//   mode 3  tile max, V -> Tm (one texel per c.x x c.x pixels): the longest streak in the tile
//   mode 4  neighbour max, Tm -> Tn: the longest within c.y tiles each way (as far as a streak can reach)
//   mode 1  gather, into the accumulation A (blended: SRC_ALPHA, a = a.w = 1 / (frame + 1), a running mean): 8 time
//           slices at this frame's offsets (a.z: frame k's sub-step, so k = 0..15 cover 128 evenly spaced instants). In
//           each slice the pixel shows what covers it then: a nearer pixel along the neighbourhood's longest streak (Tn)
//           whose own streak reaches this pixel (a moving car smears over what is behind it; leaves over the sky between
//           them); else its own surface along its own streak, where a pixel outside every box skips the places that lie
//           on a car (a sharp subject does not bleed into the moving background)
//   mode 2  composite, A over the frame (RGB only, no blend); a.z = 1: the streaks in false colour (B3GFX_MOTION_DEBUG=1)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(set = 2, binding = 0) uniform sampler2D src;     // mode 1: the resolved frame (bilinear); 2: A (nearest)
layout(set = 2, binding = 1) uniform sampler2D vel;     // modes 1, 3: V; 4: Tm (nearest)
layout(set = 2, binding = 2) uniform sampler2D depth;   // mode 0: the depth pre-pass; 1: Tn (nearest)
layout(set = 3, binding = 0) uniform MotionUniforms {
    mat4 vp;        // the frame's view-projection (column-major, reverse-Z)
    mat4 inv;       // its inverse
    vec4 rm[3];     // the camera's turn at -T/2 (rows, xyz), applied to (P - eye)
    vec4 rp[3];     // ... at +T/2
    vec4 eye;       // xyz: the camera
    vec4 cam;       // xyz: the camera's offset at +T/2 (world)
    vec4 a;         // x: mode, y: boxes, z: this frame's sub-step (0..1), w: the blend weight
    vec4 b;         // x: the half streak's longest (pixels), y / z: the frame's size (pixels), w: slices
    vec4 c;         // x: the tile's size (pixels), y: the neighbour max's reach (tiles), zw: the tiles across / down
    vec4 bx[48];    // 8 boxes x 6: to-local rows 0..2 (xyz, w), lo (xyz), hi (xyz), move (xyz: the car's offset at +T/2)
};

vec3 world(vec2 uv, float d) {
    vec4 h = inv * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);
    return h.xyz / h.w;
}

// the end of the streak at side s (-1: -T/2, +1: +T/2); false: behind the camera
bool ends(vec3 p, vec3 move, float s, out vec2 uv) {
    vec3 q = p + s * move - s * cam.xyz - eye.xyz;
    vec3 r = s < 0.0 ? vec3(dot(rm[0].xyz, q), dot(rm[1].xyz, q), dot(rm[2].xyz, q))
                     : vec3(dot(rp[0].xyz, q), dot(rp[1].xyz, q), dot(rp[2].xyz, q));
    vec4 k = vp * vec4(r + eye.xyz, 1.0);
    if (k.w <= 1e-4) { uv = v_uv; return false; }
    uv = vec2(0.5 + 0.5 * k.x / k.w, 0.5 - 0.5 * k.y / k.w);
    return true;
}

float px_len(vec2 h) { return length(h * b.yz); }

void main() {
    int mode = int(a.x + 0.5);
    if (mode == 0) {
        float d = texture(depth, v_uv).r;
        vec3 p;
        if (d > 0.0) {
            p = world(v_uv, d);
        } else {   // the sky: far along the pixel's ray
            vec3 f = world(v_uv, 0.001) - eye.xyz;
            p = eye.xyz + normalize(f) * 5000.0;
        }
        int n = int(a.y + 0.5);
        int id = 0;
        vec3 move = vec3(0.0);
        if (d > 0.0)
            for (int k = 0; k < n; ++k) {
                vec3 l = vec3(dot(bx[6 * k].xyz, p) + bx[6 * k].w, dot(bx[6 * k + 1].xyz, p) + bx[6 * k + 1].w,
                              dot(bx[6 * k + 2].xyz, p) + bx[6 * k + 2].w);
                if (all(greaterThanEqual(l, bx[6 * k + 3].xyz)) && all(lessThanEqual(l, bx[6 * k + 4].xyz))) {
                    id = k + 1;
                    move = bx[6 * k + 5].xyz;
                    break;
                }
            }
        vec2 u0, u1;
        bool o0 = ends(p, move, -1.0, u0), o1 = ends(p, move, 1.0, u1);
        vec2 h = o0 && o1 ? 0.5 * (u1 - u0) : o1 ? u1 - v_uv : o0 ? v_uv - u0 : vec2(0.0);
        float len = px_len(h);
        if (len > b.x) h *= b.x / len;
        out_color = vec4(h, float(id), length(p - eye.xyz));
    } else if (mode == 3) {
        int t = int(c.x + 0.5);
        ivec2 o = ivec2(gl_FragCoord.xy) * t, lim = ivec2(b.yz) - 1;
        vec2 best = vec2(0.0);
        float bl = -1.0;
        for (int y = 0; y < t; ++y)
            for (int x = 0; x < t; ++x) {
                vec2 h = texelFetch(vel, min(o + ivec2(x, y), lim), 0).rg;
                float l = px_len(h);
                if (l > bl) { bl = l; best = h; }
            }
        out_color = vec4(best, bl, 1.0);
    } else if (mode == 4) {
        int r = int(c.y + 0.5);
        ivec2 o = ivec2(gl_FragCoord.xy), lim = ivec2(c.zw) - 1;
        vec4 best = vec4(0.0);
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                vec4 s = texelFetch(vel, clamp(o + ivec2(x, y), ivec2(0), lim), 0);
                if (s.b > best.b) best = s;
            }
        out_color = best;
    } else if (mode == 1) {
        vec4 own = texture(vel, v_uv);
        bool background = own.b < 0.5;
        // the neighbourhood's longest streak, its tile jittered by up to half a tile (a different place each frame: the
        // tiles' edges average out over the accumulation)
        // (d3dhash: an integer hash of the pixel, the frame's sub-step its stream: the same on every backend; the
        // sin() hash it replaces depended on each GPU's sin precision. float -> int -> uint: a float to uint is undefined
        // for a negative value on every backend)
        vec2 jit = hash3i_s(vec3(floor(gl_FragCoord.xy), 0.0), uint(int(floor(a.z * 1024.0 + 0.5)))).xy - 0.5;
        ivec2 tile = clamp(ivec2((gl_FragCoord.xy + jit * c.x) / c.x), ivec2(0), ivec2(c.zw) - 1);
        vec2 vn = texelFetch(depth, tile, 0).rg;
        if (px_len(vn) < px_len(own.rg)) vn = own.rg;
        vec3 here = texture(src, v_uv).rgb;
        int slices = int(b.w + 0.5);
        vec3 acc = vec3(0.0);
        for (int j = 0; j < slices; ++j) {
            float o = (float(j) + a.z) / float(slices) * 2.0 - 1.0;   // -1 .. 1 over the shutter
            // a nearer pixel whose own streak reaches this one covers it in this slice
            vec2 ty = v_uv + vn * o;
            vec4 sy = texture(vel, ty);
            // (another object, or far in front; moving across this pixel: what moves with it is in its own streak already)
            bool nearer = abs(sy.b - own.b) > 0.5 ? sy.a < own.a * 0.95 - 0.05 : sy.a < own.a * 0.7;
            bool across = px_len(sy.rg - own.rg) > 0.25 * px_len(vn) + 1.0;
            if (nearer && across && px_len(sy.rg) >= px_len(vn * o)) {
                acc += texture(src, ty).rgb;
                continue;
            }
            // else its own surface
            vec2 tc = v_uv + own.rg * o;
            acc += background && texture(vel, tc).b > 0.5 ? here : texture(src, tc).rgb;
        }
        out_color = vec4(acc / float(slices), a.w);
    } else if (a.z > 0.5) {   // B3GFX_MOTION_DEBUG=1: red the streak, green the box, blue the neighbourhood's streak
        vec4 v = texture(vel, v_uv);
        vec2 n = texelFetch(depth, ivec2(gl_FragCoord.xy / c.x), 0).rg;
        out_color = vec4(px_len(v.rg) / b.x, v.b / 8.0, px_len(n) / b.x, 1.0);
    } else {
        out_color = vec4(texture(src, v_uv).rgb, 1.0);
    }
}
