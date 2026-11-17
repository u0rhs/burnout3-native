#version 450
// b3gfx mesh vertex shader (GLSL 450 -> SPIR-V -> MSL; tools/render/build_shaders.sh). SDL_GPU bindings: vertex
// uniform buffers in set 1.
//
// shade.x = 1 selects the game's track model (docs/native-renderer.md §11), the per-vertex math of the track VU1
// microprograms, checked against them in a VU1 interpreter by tools/render/b3vucheck (C++ twin:
// src/b3asset/trackshade.cpp). Colours leave this stage in GS units / 128 (0x80 = 1.0), alpha likewise.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;
layout(location = 4) in uvec2 in_extra;

layout(set = 1, binding = 0) uniform VertexUniforms {
    mat4 view_proj;
    mat4 model;
    vec4 eye;      // camera position
    vec4 right;    // camera right vector (type-2 texgen uses x, z)
    vec4 sun;      // direction towards the sun (the game's light 0)
    vec4 shade;    // x: 0 simple / 1 track model / 2 sky (track model, in_pos = direction around the camera),
                   // y: material type, z: material +0x04, w: material +0x08
    vec4 inst;     // rgb: instance colour (GS bytes / 128), w: instance alpha (GS bytes), w < 0: not an instance
    vec4 flags;    // x: 1 = specular off (RenderMesh's last argument), y: UV scroll added to u
    vec4 line;     // line meshes (type 3): x, y: the target's size in pixels, z: the line width in pixels, w: the camera's far
};

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out float v_w;   // view depth (clip w), for the fog
// line meshes: x = signed distance from the centre line (pixels), y = half the line width (pixels); screen-linear
layout(location = 4) noperspective out vec2 v_line;

// The programs' float-bit tricks: ITOF0 of a float's bits, and FTOI0 then those integer bits read as a float (the VU
// has no Inf/NaN: exponent 255 is +-Fmax).
float bits_as_float(float f) { return float(floatBitsToInt(f)); }
float int_as_float(float v) {
    int i = int(clamp(trunc(v), -2147483648.0, 2147483520.0));
    if (((i >> 23) & 0xFF) == 0xFF) return i < 0 ? -3.4028235e38 : 3.4028235e38;
    return intBitsToFloat(i);
}
// FTOI0 (saturating), low 8 bits as the GIF RGBAQ field takes them.
float alpha_byte(float a) {
    if (!(a > -2147483648.0)) return 0.0;
    if (a >= 2147483520.0) return 255.0;
    return float(int(a) & 0xFF);
}
// Phong specular through the bit-trick pow: ~2 * strength * max(R.V, 0.01 exponent)^exponent, R the sun reflected
// about the normal (s8 units), V from `from` to the vertex [C programs 0x470650 0x0067-0x00CD, 0x481DD0 0x00B6-].
float spec_term(vec3 pos, vec3 n, vec3 from) {
    vec3 L = -sun.xyz;
    float k = dot(n, L * 0.00012400025);
    vec3 R = k * n - L;
    vec3 D = pos - from;
    float s = max(dot(R, D) * inversesqrt(dot(D, D)), shade.w * 0.0099999998);
    float K = bits_as_float(shade.z + shade.z) - shade.w * 1065353216.0;
    return int_as_float(K + bits_as_float(s) * shade.w);
}

// Line meshes (type 3, docs/native-renderer.md §11.8; C++ twin: b3asset::track_line_expand): each segment is a quad
// whose four vertices carry this end (in_pos), the other end (in_normal) and (side, end) in in_uv. Both ends go to clip
// space (an end behind the eye is first moved along the segment onto w = 1e-3), and the vertex moves across the
// segment's screen direction by width / 2 + 1 pixels (the extra pixel holds the coverage ramp).
const float kLineNearW = 1e-3;
vec4 line_clip(vec4 p, vec4 q) {
    if (p.w >= kLineNearW) return p;
    return p + (q - p) * ((kLineNearW - p.w) / (q.w - p.w));
}

void main() {
    vec4 wpos = model * vec4(in_pos, 1.0);
    v_line = vec2(0.0);
    if (shade.x > 0.5 && shade.x < 1.5 && int(shade.y + 0.5) == 3) {
        vec4 a = view_proj * wpos;
        vec4 b = view_proj * (model * vec4(in_normal, 1.0));
        v_uv = vec2(0.0);
        v_normal = vec3(0.0);
        if (a.w < kLineNearW && b.w < kLineNearW) {   // the whole segment behind the eye
            gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
            v_w = 1.0;
            v_color = vec4(0.0);
            return;
        }
        vec4 a1 = line_clip(a, b), b1 = line_clip(b, a);
        vec2 pa = a1.xy / a1.w * 0.5 * line.xy, pb = b1.xy / b1.w * 0.5 * line.xy;
        vec2 d = in_uv.y > 0.5 ? pa - pb : pb - pa;   // end 0 -> end 1 for all four vertices
        float len = length(d);
        d = len > 1e-6 ? d / len : vec2(1.0, 0.0);
        float ext = in_uv.x * (0.5 * line.z + 1.0);
        gl_Position = vec4(a1.xy + vec2(-d.y, d.x) * ext * (2.0 / line.xy) * a1.w, a1.zw);
        v_w = a1.w;
        v_line = vec2(ext, 0.5 * line.z);
        // RGBA = the input bytes [C program 0x476130]; F = clamp(255 (far - w) / (far - fogPlane)) with RwCamera's
        // fogPlane 5 [L], as 0..1 in v_normal.x (the GS interpolates F per pixel)
        v_color = vec4(in_color.rgb * (255.0 / 128.0), in_color.a * (255.0 / 128.0));
        v_normal.x = line.w > 5.0 ? clamp((line.w - a1.w) / (line.w - 5.0), 0.0, 1.0) : 1.0;
        return;
    }
    // Sky: a direction (w = 0) lands on the far plane of the reverse-Z infinite projection, camera-centred.
    gl_Position = shade.x > 1.5 ? view_proj * vec4(in_pos, 0.0) : view_proj * wpos;
    v_w = gl_Position.w;
    v_uv = in_uv;
    v_color = in_color;
    v_normal = mat3(model) * in_normal;
    if (shade.x < 0.5) return;

    int type = int(shade.y + 0.5);
    vec3 col = in_color.rgb * (255.0 / 128.0);
    float alpha = in_color.a * 255.0;                    // GS bytes
    if (inst.w >= 0.0) { col = inst.rgb; alpha = inst.w; }
    vec3 n = round(in_normal * 127.0);                   // the s8 normal the programs read
    float intensity = float(int(in_extra.x << 24) >> 24); // s8
    v_uv.x += flags.y;
    if (type == 1) {
        // Specular into the vertex alpha, eye 10 m above the camera, floor 4 [C 0x470650].
        alpha = flags.x > 0.5 ? 0.0 : alpha_byte(max(intensity * spec_term(wpos.xyz, n, eye.xyz + vec3(0, 10, 0)), 4.0));
    } else if (type == 2) {
        // Reflection texgen [C 0x481DD0]: R = D - 2 (n.D) n with D from the camera; u from R.xz against the camera
        // right, v from R.y; bits(x^-1/2) ~ 1.5 bits(1.0) - bits(x)/2.
        vec3 D = wpos.xyz - eye.xyz;
        vec3 R = D - n * (dot(n, D) * 0.00012400025);
        float K = bits_as_float(1.0) * 1.5;
        float rh = int_as_float(K - bits_as_float(R.x * R.x + R.z * R.z) * 0.5);
        v_uv.x = trunc((0.5 + (R.x * right.x * 0.25 + R.z * right.z * 0.25) * rh) * 4096.0) / 4096.0;
        bool lit = flags.x < 0.5 && shade.z > 0.0;
        if (!lit) {
            float rs = int_as_float(K - bits_as_float(dot(R, R)) * 0.5);
            v_uv.y = trunc((0.5 - R.y * 0.5 * rs) * 4096.0) / 4096.0;
            alpha = 0.0;
        } else {
            v_uv.y = trunc((0.5 - 0.5 * R.y * inversesqrt(dot(D, D))) * 4096.0) / 4096.0;
            alpha = alpha_byte(intensity * spec_term(wpos.xyz, n, eye.xyz));
        }
    }
    v_color = vec4(col, alpha / 128.0);
}
