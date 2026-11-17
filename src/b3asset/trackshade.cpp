#include "b3asset/trackshade.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace b3asset {

namespace {
float bits_as_float(float f) { std::uint32_t b; std::memcpy(&b, &f, 4); return float(std::int32_t(b)); }  // ITOF0 of the bits
float int_as_float(float v) {  // FTOI0, then the integer's bits read as a float; exponent 255 is +-Fmax on the VU
    const std::int32_t i = std::int32_t(std::max(-2147483648.0f, std::min(2147483520.0f, std::trunc(v))));
    float f;
    std::memcpy(&f, &i, 4);
    if (!std::isfinite(f)) f = std::signbit(f) ? -3.4028235e38f : 3.4028235e38f;
    return f;
}
std::uint8_t alpha_byte(float a) {  // FTOI0 (saturating), low 8 bits as the GIF RGBAQ field takes them
    if (!(a > -2147483648.0f)) return 0;
    if (a >= 2147483520.0f) return 0xFF;
    return std::uint8_t(std::int32_t(a));
}
float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
}  // namespace

// [C ReallySetMaterial 0x26B2F0 + PrepareRenderStatesForRenderingTrack 0x26B8E0 (asm/game/unit_0026B8E0.s)]:
// renderer +0x30 = -(environment manager +0xB0[light]) (w 10), +0x40 = camera position + (0, 10, 0) (w 1), +0x50 =
// camera position, +0x60 = camera right vector (camera data +0xA0); types 1/2 replace the w lanes with the material's
// +0x08 / +0x04 parameters.
ShadeVectors shade_vectors(const ShadeScene& sc, const TrackMaterialInfo& m) {
    ShadeVectors v{};
    for (int i = 0; i < 3; ++i) {
        v.c1007[i] = -sc.sun[i];
        v.c1008[i] = sc.eye[i] + (i == 1 && m.type != 2 ? 10.0f : 0.0f);
    }
    v.c1007[3] = (m.type == 1 || m.type == 2) ? m.param8 : 10.0f;
    v.c1008[3] = (m.type == 1 || m.type == 2) ? m.param4 : 1.0f;
    if (m.type == 1) v.c1009[0] = 4.0f;
    if (m.type == 2) {
        v.c1009[0] = sc.right[0];
        v.c1009[1] = sc.right[2];
        v.c1009_flag = m.param4 > 0.0f;
    }
    return v;
}

std::vector<TrackVertexIn> track_batch_inputs(const VuMemory& vu, std::uint32_t n, std::uint32_t type) {
    std::vector<TrackVertexIn> out(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        TrackVertexIn& v = out[i];
        for (int c = 0; c < 3; ++c) v.pos[c] = vu.f(3 * i, c);
        v.adc = vu.qw(3 * i)[3] & 0x8000;
        const std::uint32_t* uq = vu.qw(3 * i + 1);
        v.uv[0] = float(std::int32_t(uq[0])) / 4096.0f;
        v.uv[1] = float(std::int32_t(uq[1])) / 4096.0f;
        const std::uint32_t* cq = vu.qw(3 * i + 2);
        for (int c = 0; c < 4; ++c) v.rgba[c] = std::uint8_t(cq[c]);
        if (type == 1 || type == 2) v.rgba[3] = 0;
        // Per-vertex vectors are stored transposed per group of four vertices: component c of vertex 4g+j is lane j
        // of qword `base + stride*(4g+c)` (type 1: the normal block at 204, stride 1; type 2: the UV slot, stride 3).
        const std::uint32_t g = i / 4, j = i % 4;
        for (int c = 0; c < 4; ++c) {
            if (type == 1) v.n[c] = std::int8_t(vu.qw(204 + 4 * g + std::uint32_t(c))[j]);
            if (type == 2) v.n[c] = std::int8_t(vu.qw(3 * (4 * g + std::uint32_t(c)) + 1)[j]);
        }
    }
    return out;
}

// Type 1 [C program 0x470650 0x0067-0x00CD]: Phong specular into the vertex alpha. L = renderer +0x30 (towards-sun
// negated), R = 2 (n.L) n - L with n = normal/127 (the program folds 127 * 0.000124 = 1/63.5 into L), V = unit vector
// from the eye (camera + 10 up) to the vertex, s = max(R.V, 0.01 * exponent); pow through the float-bits log trick:
// bits(result) = exponent * bits(s) + bits(2 * strength) - exponent * bits(1.0), i.e. ~2 * strength * s^exponent;
// alpha = max(4, intensity * result).
float track_specular(const float pos[3], const std::int8_t n[4], const float eye_up[3], const float to_sun[3],
                     float strength, float exponent) {
    const float L[3] = {-to_sun[0], -to_sun[1], -to_sun[2]};
    const float Ls[3] = {L[0] * 0.00012400025f, L[1] * 0.00012400025f, L[2] * 0.00012400025f};
    const float N[3] = {float(n[0]), float(n[1]), float(n[2])};
    const float k = dot3(N, Ls);
    const float R[3] = {k * N[0] - L[0], k * N[1] - L[1], k * N[2] - L[2]};
    const float D[3] = {pos[0] - eye_up[0], pos[1] - eye_up[1], pos[2] - eye_up[2]};
    const float inv = 1.0f / std::sqrt(dot3(D, D));
    const float s = std::max(dot3(R, D) * inv, exponent * 0.0099999998f);
    const float K = bits_as_float(strength + strength) - exponent * 1065353216.0f;
    const float p = int_as_float(K + bits_as_float(s) * exponent);
    return std::max(float(n[3]) * p, 4.0f);
}

void shade_track_batch(const ShadeScene& sc, const TrackMaterialInfo& m, const std::vector<TrackVertexIn>& in,
                       std::vector<TrackVertexOut>& out) {
    const ShadeVectors cv = shade_vectors(sc, m);
    const std::size_t n = in.size();
    out.assign(n, TrackVertexOut{});
    const bool cull = !(m.flags & 0x20) && m.type != 3;
    for (std::size_t i = 0; i < n; ++i) {
        const TrackVertexIn& v = in[i];
        TrackVertexOut& o = out[i];
        float vx = sc.view[3][0], vy = sc.view[3][1], vz = sc.view[3][2];
        for (int k = 0; k < 3; ++k) {
            vx += v.pos[k] * sc.view[k][0];
            vy += v.pos[k] * sc.view[k][1];
            vz += v.pos[k] * sc.view[k][2];
        }
        o.q = 1.0f / vz;
        o.x = sc.shift[0] + (vx - 0.5f * vz) * sc.scale[0] * o.q;
        o.y = sc.shift[1] + (vy - 0.5f * vz) * sc.scale[1] * o.q;
        std::memcpy(o.rgba, v.rgba, 4);
        o.u = v.uv[0] + (sc.uv_scroll_on ? sc.uv_scroll : 0.0f);
        o.v = v.uv[1];
        if (m.type == 3) {
            // [C program 0x476130 0x0038-0x0057] RW-style linear fog: F = clamp(255 * (far - w) / (far - fogPlane)),
            // from xMaxYMax128.zw in the w lanes of 1018/1019.
            o.fog = int(std::max(0.0f, std::min(255.0f, (vz - sc.shift[3]) * sc.scale[3])));
        }
        if (m.type == 1) {
            o.rgba[3] = sc.no_specular ? 0 : alpha_byte(track_specular(v.pos, v.n, cv.c1008, sc.sun, m.param4, m.param8));
        } else if (m.type == 2) {
            // Reflection texgen [C program 0x481DD0]: D = vertex - camera, R = D - 2 (n.D) n (n = normal/127, folded
            // into 0.000124 = 2/127^2). u = 0.5 + (R.xz . right.xz / 4) / |R.xz| in both paths, with the float-bits
            // inverse square root bits(x^-1/2) ~ 1.5 bits(1.0) - bits(x) / 2 (path A spells it as 0.5/sqrt with
            // bits(0.5) + bits(1.0)/2 and halves right.xz instead; same result).
            //   path A (0x005F, specular off or material +0x04 <= 0): v = 0.5 - R.y * 0.5 * rsq(|R|^2) (fast), alpha 0;
            //   path B (0x00B6, the lit path): v = 0.5 - 0.5 R.y / |D| (exact reciprocal square root), and alpha =
            //   intensity (attribute w) * the type-1 specular power term with the eye at the camera, no floor of 4.
            const float D[3] = {v.pos[0] - cv.c1008[0], v.pos[1] - cv.c1008[1], v.pos[2] - cv.c1008[2]};
            const float N[3] = {float(v.n[0]), float(v.n[1]), float(v.n[2])};
            const float k = dot3(N, D) * 0.00012400025f;
            const float R[3] = {D[0] - N[0] * k, D[1] - N[1] * k, D[2] - N[2] * k};
            const float K = bits_as_float(1.0f) * 1.5f;
            const float rh = int_as_float(K - bits_as_float(R[0] * R[0] + R[2] * R[2]) * 0.5f);
            const float hd = R[0] * (cv.c1009[0] * 0.25f) + R[2] * (cv.c1009[1] * 0.25f);
            o.u = std::trunc((0.5f + hd * rh) * 4096.0f) / 4096.0f;
            const bool lit = !sc.no_specular && cv.c1009_flag;
            if (!lit) {
                const float rs = int_as_float(K - bits_as_float(dot3(R, R)) * 0.5f);
                o.v = std::trunc((0.5f - R[1] * 0.5f * rs) * 4096.0f) / 4096.0f;
                o.rgba[3] = 0;
            } else {
                const float inv = 1.0f / std::sqrt(dot3(D, D));
                o.v = std::trunc((0.5f - 0.5f * R[1] * inv) * 4096.0f) / 4096.0f;
                const float L[3] = {-sc.sun[0], -sc.sun[1], -sc.sun[2]};
                const float Ls[3] = {L[0] * 0.00012400025f, L[1] * 0.00012400025f, L[2] * 0.00012400025f};
                const float kl = dot3(N, Ls);
                const float RL[3] = {kl * N[0] - L[0], kl * N[1] - L[1], kl * N[2] - L[2]};
                const float s2 = std::max(dot3(RL, D) * inv, m.param8 * 0.0099999998f);
                const float KP = bits_as_float(m.param4 + m.param4) - m.param8 * 1065353216.0f;
                o.rgba[3] = alpha_byte(float(v.n[3]) * int_as_float(KP + bits_as_float(s2) * m.param8));
            }
        }
        o.drawn = i >= 2 && !v.adc;
        if (o.drawn && cull) {
            // [C program 0x46F0D0 0x0076-0x0086]: screen-space cross product of the last two edges; a set sign of z
            // sets ADC (MAC flag 0x20 read by FMAND, added to 0x7FE0 = 0x8000). The edge order alternates with the
            // vertex index (the strip's winding).
            const float e1x = out[i - 1].x - out[i - 2].x, e1y = out[i - 1].y - out[i - 2].y;
            const float e2x = o.x - out[i - 1].x, e2y = o.y - out[i - 1].y;
            const float cz = e1x * e2y - e1y * e2x;
            if ((i & 1) ? cz < 0.0f : cz > 0.0f) o.drawn = false;
        }
    }
}

// Program per material [C ReallySetMaterial 0x26B2F0 switch, src/nonmatching/ReallySetMaterial__16CB3TrackRendererFv.cpp].
std::uint32_t track_program(const TrackMaterialInfo& m) {
    const bool alt = m.flags & 0x20;
    switch (m.type) {
    case 4: case 5: return 0x471FA0;
    case 3: return 0x476130;
    case 1: return alt ? 0x472EB0 : 0x470650;
    case 2: return alt ? 0x4803F0 : 0x481DD0;
    case 7: return 0x47E300;
    default: return alt ? 0x4745C0 : 0x46F0D0;
    }
}

static void put(VuMemory& m, std::uint32_t qw, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    m.w[qw * 4 + 0] = a; m.w[qw * 4 + 1] = b; m.w[qw * 4 + 2] = c; m.w[qw * 4 + 3] = d;
}
static std::uint32_t fb(float f) { std::uint32_t v; std::memcpy(&v, &f, 4); return v; }
static void putf(VuMemory& m, std::uint32_t qw, float a, float b, float c, float d) { put(m, qw, fb(a), fb(b), fb(c), fb(d)); }

// Writes the constants the EE side sends (ReallySetMaterial: 1005-1009; RenderMesh: 1013; ReallySetupCameraData:
// 1014-1023) for `scene` and material `m`.
void track_vu_constants(const ShadeScene& sc, const TrackMaterialInfo& m, std::uint32_t clip_flags, VuMemory& mem) {
    const std::uint32_t prim = (m.type == 3 ? 0xE9u : 0x4Cu) | (m.texture >= 0 || m.type == 2 ? 0x10u : 0u);
    putf(mem, 1005, 0, 0, 0, 0);
    put(mem, 1005, sc.uv_scroll_on ? 1u : 0u, fb(sc.uv_scroll), 0, 0);
    put(mem, 1006, 0, (prim << 15) | 0x30004000u, 0x412, 0);
    const ShadeVectors v = shade_vectors(sc, m);
    putf(mem, 1007, v.c1007[0], v.c1007[1], v.c1007[2], v.c1007[3]);
    putf(mem, 1008, v.c1008[0], v.c1008[1], v.c1008[2], v.c1008[3]);
    put(mem, 1009, fb(v.c1009[0]), fb(v.c1009[1]), v.c1009_flag, 0);
    put(mem, 1013, clip_flags, 0, sc.no_specular ? 1u : 0u, 1);
    for (int r = 0; r < 4; ++r)
        putf(mem, 1014 + r, sc.view[r][0], sc.view[r][1], sc.view[r][2], sc.view[r][2]);
    putf(mem, 1018, sc.scale[0], sc.scale[1], sc.scale[2], sc.scale[3]);
    putf(mem, 1019, sc.shift[0], sc.shift[1], sc.shift[2], sc.shift[3]);
    for (int r = 0; r < 4; ++r) putf(mem, 1020 + r, sc.guard[r][0], sc.guard[r][1], sc.guard[r][2], sc.guard[r][3]);
}

// Camera looking at `target` from `eye`, 60 degrees across, 640x448 with the game's 2048-centred GS window.
ShadeScene shade_scene_look_at(const float eye[3], const float target[3], const float sun[3]) {
    ShadeScene sc;
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& c : f) c /= l;
    // right = fwd x up (+Y up): the game's screen x grows along it (right-handed; with up x fwd the programs would cull
    // the road seen from above, see the culling statistics).
    float r[3] = {-f[2], 0, f[0]};
    l = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    r[0] /= l; r[2] /= l;
    const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};  // r x f = up
    const float tx = std::tan(0.5236f), ty = tx * 448.0f / 640.0f;
    float colx[3], coly[3];
    for (int i = 0; i < 3; ++i) {
        colx[i] = r[i] / (2 * tx) + 0.5f * f[i];
        coly[i] = -u[i] / (2 * ty) + 0.5f * f[i];
    }
    for (int i = 0; i < 3; ++i) { sc.view[i][0] = colx[i]; sc.view[i][1] = coly[i]; sc.view[i][2] = f[i]; }
    sc.view[3][0] = -(eye[0] * colx[0] + eye[1] * colx[1] + eye[2] * colx[2]);
    sc.view[3][1] = -(eye[0] * coly[0] + eye[1] * coly[1] + eye[2] * coly[2]);
    sc.view[3][2] = -(eye[0] * f[0] + eye[1] * f[1] + eye[2] * f[2]);
    for (int i = 0; i < 3; ++i) { sc.eye[i] = eye[i]; sc.right[i] = r[i]; sc.sun[i] = sun[i]; }
    return sc;
}

float track_line_width(float rows) { return std::max(1.0f, rows / kLineRows); }

float track_line_coverage(float d, float width) { return std::clamp(0.5f * width + 0.5f - std::fabs(d), 0.0f, 1.0f); }

float track_line_fog(float w, float far, float fog_plane) {
    return far > fog_plane ? std::clamp((far - w) / (far - fog_plane), 0.0f, 1.0f) : 1.0f;
}

bool track_line_expand(const float self[4], const float other[4], float side, float end, float vw, float vh, float width,
                       float out[4], float* edge) {
    float a[4], b[4];
    std::memcpy(a, self, sizeof a);
    std::memcpy(b, other, sizeof b);
    if (edge) *edge = 0.0f;
    if (a[3] < kLineNearW && b[3] < kLineNearW) {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        return false;
    }
    // onto the plane w = kLineNearW (the other end stays where it is for this vertex's direction)
    auto clip = [](float* p, const float* q) {
        if (p[3] >= kLineNearW) return;
        const float t = (kLineNearW - p[3]) / (q[3] - p[3]);
        for (int k = 0; k < 4; ++k) p[k] += (q[k] - p[k]) * t;
    };
    float a0[4];
    std::memcpy(a0, a, sizeof a0);
    clip(a, b);
    clip(b, a0);
    const float pa[2] = {a[0] / a[3] * 0.5f * vw, a[1] / a[3] * 0.5f * vh};
    const float pb[2] = {b[0] / b[3] * 0.5f * vw, b[1] / b[3] * 0.5f * vh};
    // the segment's direction from end 0 to end 1, so that both ends offset `side` to the same side
    float d[2] = {pb[0] - pa[0], pb[1] - pa[1]};
    if (end > 0.5f) { d[0] = -d[0]; d[1] = -d[1]; }
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1]);
    if (len > 1e-6f) { d[0] /= len; d[1] /= len; } else { d[0] = 1.0f; d[1] = 0.0f; }
    const float ext = side * (0.5f * width + 1.0f);
    out[0] = a[0] + -d[1] * ext * (2.0f / vw) * a[3];
    out[1] = a[1] + d[0] * ext * (2.0f / vh) * a[3];
    out[2] = a[2];
    out[3] = a[3];
    if (edge) *edge = ext;
    return true;
}

}  // namespace b3asset
