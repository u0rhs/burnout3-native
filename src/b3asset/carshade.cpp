#include "b3asset/carshade.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace b3asset {

namespace {
float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void cross3(const float* a, const float* b, float* o) {
    const float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
void normalise3(float* v) {
    const float l = std::sqrt(dot3(v, v));
    if (l > 0) for (int i = 0; i < 3; ++i) v[i] /= l;
}
float lanef(const VuMemory& m, std::uint32_t qw, int lane) { return m.f(qw, lane); }
std::uint32_t lanei(const VuMemory& m, std::uint32_t qw, int lane) { return m.qw(qw)[lane]; }
// FTOI0: truncation toward zero, saturating [C VU manual].
std::int32_t ftoi0(float v) {
    if (v >= 2147483647.0f) return 0x7FFFFFFF;
    if (v <= -2147483648.0f) return std::int32_t(0x80000000u);
    return std::int32_t(v);
}
// Ramamoorthi & Hanrahan (2001) irradiance-matrix constants; the VU0 code loads exactly these [C 0x47F6D0 0x02..0x10].
constexpr float kC1 = 0.429043f, kC2 = 0.511664f, kC3 = 0.743125f, kC4 = 0.886227f, kC5 = 0.247708f;
}  // namespace

void car_irradiance_matrix(const float L[9], float w[4][4]) {
    const float m[4][4] = {{kC1 * L[8], kC1 * L[4], kC1 * L[7], kC2 * L[3]},
                           {kC1 * L[4], -kC1 * L[8], kC1 * L[5], kC2 * L[1]},
                           {kC1 * L[7], kC1 * L[5], kC3 * L[6], kC2 * L[2]},
                           {kC2 * L[3], kC2 * L[1], kC2 * L[2], kC4 * L[0] - kC5 * L[6]}};
    std::memcpy(w, m, sizeof m);
}

float car_irradiance_k(const float m[4][4], const float at[3]) {
    float a[3] = {at[0], at[1], at[2]};
    normalise3(a);
    float q = 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) q += a[i] * m[i][j] * a[j];
    return 0.5f * (q - m[2][2]);
}

// ---- VU0 0x47F6D0 at 0x0FB (vcallms 0x7D8) [C listing; checked by b3vucheck_car]
CarPartConstants car_part_constants(const CarScene& sc, const CarPartInput& in) {
    CarPartConstants o{};
    // 962..965: the part matrix as given, translation w = 0 (VU0 0x0FB `mulx.w vf04`) -> VU1 prepares it [C].
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) o.part[r][c] = in.mat[r][c];
        o.part[r][3] = 0.0f;
    }
    // 966..969: with the bool argument, the 4x4 SH irradiance matrix (R&H eq. 12) of the coefficients, rotated into
    // the part's space: M_part = R M_world R^T with R = the part's 3x3 rows [C VU0 path at 0x000, run in b3vucheck_car:
    // one coefficient at a time gives exactly R&H's matrix, a yawed part the rotated one]. Without it the previous
    // part's matrix is reused (the bits reuse the body's) [C 0x15A, 0x15C..0x15F].
    if (in.rotate_lighting) {
        float w[4][4];
        car_irradiance_matrix(sc.sh, w);
        float r4[4][4] = {};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r4[i][j] = in.mat[i][j];
        r4[3][3] = 1.0f;
        float t[4][4] = {};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k) t[i][j] += r4[i][k] * w[k][j];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += t[i][k] * r4[j][k];
                o.m[i][j] = s;
            }
    } else {
        std::memcpy(o.m, in.prev_m, sizeof o.m);
    }
    // 970: normalise(R (car - camera)): the direction from the camera to the car, in part space; w = bias [C 0x11C..0x142].
    float d[3] = {in.car_pos[0] - sc.cam_pos[0], in.car_pos[1] - sc.cam_pos[1], in.car_pos[2] - sc.cam_pos[2]};
    for (int i = 0; i < 3; ++i) o.view[i] = dot3(in.mat[i], d);
    normalise3(o.view);
    o.view[3] = in.bias;
    // 971..973: a view basis built from the direction part -> camera Z and the camera's up: X = normalise(up x Z),
    // Y = Z x X; row i = (-R_i.X, -R_i.Y, R_i.Z), w = R_i.(camera - part position) [C 0x0FC..0x158].
    float Z[3] = {sc.cam_pos[0] - in.mat[3][0], sc.cam_pos[1] - in.mat[3][1], sc.cam_pos[2] - in.mat[3][2]};
    float e[3] = {Z[0], Z[1], Z[2]};
    normalise3(Z);
    float X[3], Y[3];
    cross3(sc.cam_up, Z, X);
    normalise3(X);
    cross3(Z, X, Y);
    for (int i = 0; i < 3; ++i) {
        o.env[i][0] = -dot3(in.mat[i], X);
        o.env[i][1] = -dot3(in.mat[i], Y);
        o.env[i][2] = dot3(in.mat[i], Z);
        o.env[i][3] = dot3(in.mat[i], e);
    }
    return o;
}

CarMeshInput car_mesh_input(std::uint32_t material_word, std::uint32_t colour_word, std::uint16_t lit_bits) {
    CarMeshInput m;
    m.mask = std::uint16_t(material_word & 0xFFFF);
    m.alpha_byte = std::uint8_t(material_word >> 16);
    m.tex_slot = std::uint8_t(material_word >> 24);
    m.fix = std::uint8_t(colour_word >> 8);  // byte 1 = the current FIX (SetGlassState copies byte 0 there) [C]
    m.lit_bits = lit_bits;
    return m;
}

// ---- VU0 0x47F6D0 at 0x16A (vcallms 0xB50) [C listing 0x16A..0x19F; inputs from BeginRenderingSingleVehicle
// 0x220040: (h,h,h,38.4 f0), (0,0,0,19.2 f0), (t,t,t,128 f0), t = 128.1 f1, h = t/2, and 128.1 f0]
CarMeshConstants car_mesh_constants(const CarScene& sc, const CarMeshInput& in) {
    CarMeshConstants o{};
    const float t = 128.1f * sc.lighting, h = 0.5f * t;
    float env[4] = {128, 128, 128, 128.1f * sc.shininess * sc.fade};
    o.alpha1[0] = 0; o.alpha1[1] = 0;
    o.no_env = 0;
    o.alpha2 = 0;
    if ((in.lit_bits & in.mask & 0xFC) != 0) {
        // A light that is on: flat 192 (1.5x), opaque-ish alpha 0, no env pass [C 0x173..0x176].
        for (int i = 0; i < 3; ++i) o.base[i] = o.full[i] = 192.0f;
        o.base[3] = o.full[3] = 0.0f;
        env[3] = 0.0f;
        o.no_env = 2;
    } else {
        // Paint/body: base 0 -> full t (colour = E(n) * 128.1 f1); glass (0x100/0x200): half ambient [C 0x177..0x17C].
        const bool glass = in.mask & 0x300;
        for (int i = 0; i < 3; ++i) { o.base[i] = glass ? h : 0.0f; o.full[i] = t; }
        o.base[3] = (glass ? 38.4f : 19.2f) * sc.shininess;
        o.full[3] = 128.0f * sc.shininess;
    }
    if (in.tex_slot == 4) {
        // Slot 4 (broken glass, SetGlassState state 2) [C 0x181..0x186]: constant alpha = FIX byte, env colour 20,
        // ALPHA_1 0x44 (Cs-Cd)*As+Cd, ALPHA_2 0x58 Cs*Ad+Cd.
        o.base[3] = o.full[3] = float(in.fix);
        env[0] = env[1] = env[2] = 20.0f;
        env[3] = 0.0f;
        o.alpha1[0] = 0x44;
        o.alpha1[1] = 0;
        o.alpha2 = 0x58;
    } else {
        o.alpha1[0] = in.alpha_byte;
        o.alpha1[1] = std::uint32_t(ftoi0(float(in.fix) * sc.fade));
        const bool c_is_as = (in.alpha_byte & 0x30) == 0;  // ALPHA C selector = As
        if (in.mask & 2) {
            // Bit 1 [C 0x18D..0x194]: env pass blends as Cd (only alpha written) when C = As, else no env pass.
            o.alpha2 = 0x6A;
            env[3] = 0.0f;
            if (c_is_as) {
                o.base[3] = o.full[3] = 128.0f;
            } else {
                o.base[3] = o.full[3] = 0.0f;
                o.no_env = 2;
            }
        } else if (c_is_as) {
            // [C 0x198..0x19A]: alpha 128 (opaque), env colour 20, ALPHA_2 0x58: Cd += env * Ad.
            env[0] = env[1] = env[2] = 20.0f;
            o.base[3] = o.full[3] = 128.0f;
            o.alpha2 = 0x58;
        } else {
            // [C 0x19B]: Fresnel alpha kept, env colour 128, ALPHA_2 0x54: Cd = lerp(Cd, env, Ad).
            o.alpha2 = 0x54;
        }
    }
    for (int i = 0; i < 4; ++i) o.env[i] = ftoi0(env[i]);
    return o;
}

float rear_lamp_level(std::uint16_t mask, float tail, float brake) {
    if (mask & 0x10) return std::max(tail, brake);
    if (mask & 0x08) return tail;
    return -1.0f;
}

CarMeshConstants car_mesh_constants_level(const CarScene& sc, const CarMeshInput& in, float level) {
    CarMeshInput off = in, on = in;
    off.lit_bits = std::uint16_t(in.lit_bits & ~0x18u);
    on.lit_bits = std::uint16_t(in.lit_bits | (in.mask & 0x18u));
    const CarMeshConstants a = car_mesh_constants(sc, off), b = car_mesh_constants(sc, on);
    const float t = std::clamp(level, 0.0f, 1.0f);
    if (t >= 1.0f || (off.lit_bits & in.mask & 0xFC) != 0) return b;   // full, or lit by another of its bits
    if (t <= 0.0f) return a;
    CarMeshConstants o = a;   // the unlit pass set-up (its env pass fading out as the lamp comes on)
    for (int i = 0; i < 4; ++i) {
        o.base[i] = a.base[i] + t * (b.base[i] - a.base[i]);
        o.full[i] = a.full[i] + t * (b.full[i] - a.full[i]);
        o.env[i] = ftoi0(float(a.env[i]) + t * float(b.env[i] - a.env[i]));
    }
    return o;
}

// ---- VU1 0x46BB80 [C listing; structure: 0x004 prepare the part transform (once per part, 965.w), 0x025 deform
// (1013.z zones) or 0x0DB, the lighting loop 0x0F3 / 0x0B3, 0x11C the env pass (1013.y == 0) 0x12F..0x1B2 or the UV
// expansion 0x1B5, then the projection 0x1C5 (0x1EC with the guard-band clip, 1013.x bit 1; 0x219/0x24C with the
// env pass), 0x289 the A+D prefix and XGKICK]
void car_batch(const VuMemory& in, std::uint32_t n, const CarScene& sc, const CarPartConstants& part,
               const CarMeshConstants& mesh, const CarPartInput& pin, std::vector<CarVertexOut>& out) {
    out.assign(n, CarVertexOut{});
    const bool deform = pin.zones != 0;
    const float bias = part.view[3];
    // Screen transform: part matrix x the RW view matrix, x/z and y/z re-centred, scaled, shifted [C 0x00A..0x024].
    for (std::uint32_t i = 0; i < n; ++i) {
        CarVertexOut& v = out[i];
        float p[3], nr[3];
        for (int c = 0; c < 3; ++c) { p[c] = lanef(in, 3 * i, c); nr[c] = lanef(in, 3 * i + 2, c) - bias; }
        const std::uint32_t g = i & ~3u, j = i & 3;
        if (deform) {
            // Two influences per vertex: (weight << 8 | zone << 2), weight share = value / 65535 (zone bits included);
            // zone matrix at 974 + value (mod 1024; the weights are multiples of 4, so this is 974 + 4 zone) [C 0x02C..0x099].
            const std::uint32_t ia = lanei(in, 3 * (g + 1) + 1, int(j)) & 0xFFFF, ib = lanei(in, 3 * (g + 3) + 1, int(j)) & 0xFFFF;
            const float a = float(ia) / 65535.0f, b = float(ib) / 65535.0f, rest = 1.0f - a - b;
            const std::uint32_t za = (974 + ia) & 1023, zb = (974 + ib) & 1023;
            int zai = int(za) - 974, zbi = int(zb) - 974;
            float rows[4][3];
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 3; ++c) {
                    const float A = (zai >= 0 && zai / 4 < 8) ? pin.zone[zai / 4][r][c] : 0.0f;
                    const float B = (zbi >= 0 && zbi / 4 < 8) ? pin.zone[zbi / 4][r][c] : 0.0f;
                    rows[r][c] = A * a + B * b + (r == c ? rest : 0.0f);
                }
            float q[3], m[3];
            for (int c = 0; c < 3; ++c) {
                q[c] = rows[3][c] + rows[0][c] * p[0] + rows[1][c] * p[1] + rows[2][c] * p[2];
                m[c] = rows[0][c] * nr[0] + rows[1][c] * nr[1] + rows[2][c] * nr[2];
            }
            std::memcpy(p, q, sizeof p);
            std::memcpy(nr, m, sizeof nr);
        }
        std::memcpy(v.pos, p, sizeof p);
        std::memcpy(v.normal, nr, sizeof nr);
        // Lighting: E(n) = (n,1) M (n,1)^T (not clamped); colour = base + E (full - base); alpha from the Fresnel-like
        // term F = 1 - (1 - |n.V|)^2: alpha = max(0, full.a) - F (full.a - base.a); FTOI0 [C 0x0F3..0x11B].
        float mn[4];
        for (int c = 0; c < 4; ++c) mn[c] = part.m[3][c] + nr[0] * part.m[0][c] + nr[1] * part.m[1][c] + nr[2] * part.m[2][c];
        const float E = mn[3] + nr[0] * mn[0] + nr[1] * mn[1] + nr[2] * mn[2];
        const float f = std::fabs(dot3(nr, part.view));
        const float F = 2.0f * f - f * f;
        v.light = E;
        for (int c = 0; c < 3; ++c) v.rgba[c] = ftoi0(mesh.base[c] + E * (mesh.full[c] - mesh.base[c]));
        v.rgba[3] = ftoi0(std::max(0.0f, mesh.full[3]) - F * (mesh.full[3] - mesh.base[3]));
        // UV: vertex g+j reads slot g + (j odd ? 2 : 0), lanes (j < 2 ? x,y : z,w), unsigned / 4096 [C 0x1B5..0x1C4].
        const std::uint32_t* uq = in.qw(3 * (g + ((j & 1) ? 2 : 0)) + 1);
        const float u = float(uq[(j & 2) ? 2 : 0] & 0xFFFF) / 4096.0f, vv = float(uq[(j & 2) ? 3 : 1] & 0xFFFF) / 4096.0f;
        // Projection [C 0x00A..0x024, 0x1C9..0x1E9]: the part matrix and the RW view matrix are combined first
        // (model-view, as a GPU would), then applied to the part-space position.
        float vc[3];
        for (int c = 0; c < 3; ++c) {
            float mv[4];
            for (int r = 0; r < 4; ++r)
                mv[r] = pin.mat[r][0] * sc.view[0][c] + pin.mat[r][1] * sc.view[1][c] + pin.mat[r][2] * sc.view[2][c] +
                        (r == 3 ? sc.view[3][c] : 0.0f);
            vc[c] = mv[3] + p[0] * mv[0] + p[1] * mv[1] + p[2] * mv[2];
        }
        const float q = 1.0f / vc[2];
        v.q = q;
        v.s = u * q;
        v.t = vv * q;
        v.x = sc.shift[0] + sc.scale[0] * (vc[0] * q - 0.5f);
        v.y = sc.shift[1] + sc.scale[1] * (vc[1] * q - 0.5f);
        v.z = sc.shift[2] + sc.scale[2] * q;
        v.adc = lanei(in, 3 * i, 3) & 0x8000;
        if (pin.le_type & 2) {
            // Guard band [C 0x1EC..0x216]: ADC when any of the last three vertices lies outside
            // |x'/a|, |y'/b| <= w, |z0 + z1 w| <= w (1022 = (a, 1/a, z0, ..), 1023 = (b, 1/b, z1, ..)).
            auto outside = [&](std::uint32_t k) {
                const CarVertexOut& o2 = out[k];
                const float cw = 1.0f / o2.q;
                const float gx = (o2.x - sc.shift[0]) * cw * sc.guard[2][1], gy = (o2.y - sc.shift[1]) * cw * sc.guard[3][1];
                const float gz = sc.guard[2][2] + sc.guard[3][2] * cw;
                const float aw = std::fabs(cw);
                return std::fabs(gx) > aw || std::fabs(gy) > aw || std::fabs(gz) > aw;
            };
            bool o3 = outside(i);
            if (i >= 1) o3 = o3 || outside(i - 1);
            if (i >= 2) o3 = o3 || outside(i - 2);
            v.adc = v.adc || o3;
        }
        // Sphere map [C 0x12F..0x1B2]: d = p - eye (part space), r = d - 2 (n.d) n, rv = basis * r,
        // m = (rv.x, rv.y, rv.z + |d|), (s, t) = rv.xy / (2 |m|) + 0.5.
        if (!mesh.no_env) {
            const float d[3] = {p[0] - part.env[0][3], p[1] - part.env[1][3], p[2] - part.env[2][3]};
            const float nd = dot3(nr, d);
            const float r[3] = {d[0] - 2 * nd * nr[0], d[1] - 2 * nd * nr[1], d[2] - 2 * nd * nr[2]};
            float rv[3];
            for (int c = 0; c < 3; ++c) rv[c] = part.env[0][c] * r[0] + part.env[1][c] * r[1] + part.env[2][c] * r[2];
            const float mz = rv[2] + std::sqrt(dot3(d, d));
            const float ml = std::sqrt(rv[0] * rv[0] + rv[1] * rv[1] + mz * mz);
            v.es = rv[0] * 0.5f / ml + 0.5f;
            v.et = rv[1] * 0.5f / ml + 0.5f;
        }
    }
}

}  // namespace b3asset
