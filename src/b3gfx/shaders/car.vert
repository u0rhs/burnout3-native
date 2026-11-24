#version 450
// b3gfx car vertex shader: the per-vertex math of the car VU1 program 0x46BB80 (docs/car-shading.md §4-5; C++ model
// src/b3asset/carshade.cpp, checked against the program in a VU interpreter by tools/render/b3vucheck_car, and this
// shader's world-space form against the model by b3vucheck_car --shader). Colours leave in GS units (0x80 = 1.0).
// Damage deformation (§4.1, render6): the program's zone blend, checked per vertex against the VU1 interpreter by
// b3vucheck_car --shader --zones. Not done here: the guard-band ADC (the GPU clips).
layout(location = 0) in vec3 in_pos;      // part space
layout(location = 1) in vec3 in_normal;   // part space, (b - 128) / 128 as the program reads it (not normalised)
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;
layout(location = 4) in uvec2 in_extra;   // the two deformation influences (weight << 8 | zone << 2)

layout(set = 1, binding = 0) uniform CarVertexUniforms {
    mat4 view_proj;
    mat4 model;     // part -> world
    mat4 light;     // normal -> the axes of the SH matrix (the body's rotation for panels)
    mat4 sh;        // R&H irradiance matrix of the car's SH coefficients, world axes
    vec4 eye;       // camera position
    vec4 view_dir;  // xyz: normalise(car - camera); w: k of the part-space SH matrix (b3asset::car_irradiance_k)
    vec4 base;      // VU1 1010 (GS units)
    vec4 full;      // VU1 1011
    mat4 zone[8];   // RenderDeformedObject's zone matrices (part space, column-major; VU1 974 + 4 k)
    vec4 deform;    // x: zone count (VU1 1013.z; 0 = RenderObject, no deformation)
};

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_colour;   // RGBAQ bytes as the GS keeps them
layout(location = 2) out vec3 v_refl;     // reflected eye ray, world
layout(location = 3) out float v_w;
layout(location = 4) out vec3 v_nrm;      // world normal (not normalised), enhanced reflections (car.frag)
layout(location = 5) out vec3 v_view;     // world eye -> point, enhanced reflections
layout(location = 6) out vec3 v_part;     // weather (§38): the point in part space (the rain's beads stay on the body)
layout(location = 7) out vec3 v_pnrm;     // weather: the part-space normal (which plane the beads lie in)

// FTOI0 (truncation), then the low 8 bits: the program does not clamp, the GIF RGBAQ fields keep 8 bits [C §4.2].
float gs_byte(float x) { return float(int(clamp(x, -2147483648.0, 2147483520.0)) & 0xFF); }

// A zone matrix's share: the program reads the matrix at (974 + u) mod 1024 (u the u16 influence, zone bits included)
// and scales it by u / 65535 [C 0x02C..0x099]; addresses outside the header's zones read other VU memory: none in the
// car files' data (b3vucheck_car counts them), taken as zero here.
mat4 zone_share(uint u) {
    int k = int((974u + u) & 1023u) - 974;
    if (k < 0 || (k & 3) != 0 || k / 4 >= int(deform.x)) return mat4(0.0);
    return zone[k / 4] * (float(u) / 65535.0);
}

void main() {
    vec3 pos = in_pos, nrm = in_normal;
    if (deform.x > 0.0) {
        // blended rows = a Za + b Zb + (1 - a - b) I, the identity in the 3x3 only; n' = n rows3x3, not renormalised
        mat4 Z = zone_share(in_extra.x) + zone_share(in_extra.y);
        float rest = 1.0 - float(in_extra.x) / 65535.0 - float(in_extra.y) / 65535.0;
        pos = (Z * vec4(in_pos, 1.0)).xyz + rest * in_pos;
        nrm = mat3(Z) * in_normal + rest * in_normal;
    }
    vec4 wpos = model * vec4(pos, 1.0);
    gl_Position = view_proj * wpos;
    v_w = gl_Position.w;
    v_uv = in_uv;
    // E(n) = (n,1) M (n,1)^T [C 0x0F3..0x11B] with the VU0's part-space matrix = the world one + k diag(1,1,1,-1)
    vec4 nl = vec4(mat3(light) * nrm, 1.0);
    float E = dot(nl, sh * nl) + view_dir.w * (dot(nrm, nrm) - 1.0);
    // Fresnel-like alpha: f = |n . V| with V the camera-to-car direction (one per part), F = 2f - f^2
    vec3 n = mat3(model) * nrm;
    float f = abs(dot(n, view_dir.xyz));
    float F = 2.0 * f - f * f;
    vec3 rgb = base.rgb + E * (full.rgb - base.rgb);
    float a = max(0.0, full.a) - F * (full.a - base.a);
    v_colour = vec4(gs_byte(rgb.r), gs_byte(rgb.g), gs_byte(rgb.b), gs_byte(a));
    // Env pass [C 0x12F..0x1B2]: d = p - eye, r = d - 2 (n.d) n (raw n); the sphere-map basis only parametrises r.
    vec3 d = wpos.xyz - eye.xyz;
    v_refl = d - 2.0 * dot(n, d) * n;
    v_nrm = n;
    v_view = d;
    v_part = pos;
    v_pnrm = nrm;
}
