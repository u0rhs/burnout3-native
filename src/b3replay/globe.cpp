#include "globe.h"

#include <algorithm>
#include <cmath>

namespace b3replay {

namespace {

constexpr float kDegToRad = 0.01745329238f;   // D_004E06F0 / _Render's 0.01745329238 [C ELF]
const float kRightAxis[3] = {1, 0, 0};        // kRight 0x4CA0E0, kUp 0x4CA0F0 (B3Globe2dObject.cpp .rodata, zero in the
const float kUpAxis[3] = {0, 1, 0};           // file: a static initialiser fills them) [L by name]

struct M34 { float r[4][3]; };                // CGtMatrix3x4 rows right, up, at, pos (row vectors: p' = p M)

// GtMathPs2::CreateRotationMatrix3x4(axis, angle) [L: the draft's decode of the E map inline]: axis normalised,
// SinCosFast's sin / cos (here the library's), position zero
M34 rotation(const float axis[3], float angle) {
    const float l = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    const float x = axis[0] / l, y = axis[1] / l, z = axis[2] / l;
    const float s = std::sin(angle), c = std::cos(angle), k = 1.0f - c;
    return M34{{{1.0f - (1.0f - x * x) * k, x * y * k + z * s, z * x * k - y * s},
                {x * y * k - z * s, 1.0f - (1.0f - y * y) * k, y * z * k + x * s},
                {z * x * k + y * s, y * z * k - x * s, 1.0f - (1.0f - z * z) * k},
                {0, 0, 0}}};
}

// CGtMatrix3x4::operator* [C header body]: this first, then m
M34 mul(const M34& a, const M34& b) {
    M34 o{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j) o.r[i][j] = a.r[i][0] * b.r[0][j] + a.r[i][1] * b.r[1][j] + a.r[i][2] * b.r[2][j] + (i == 3 ? b.r[3][j] : 0.0f);
    return o;
}

void transform(const M34& m, const float p[3], float o[3]) {
    for (int j = 0; j < 3; ++j) o[j] = p[0] * m.r[0][j] + p[1] * m.r[1][j] + p[2] * m.r[2][j] + m.r[3][j];
}

// _Render's inl1 [C asm]: colour x 128, vftoi0 (truncation), pminw with (255, 255, 255, 128), packed to bytes
void pack(const float c[4], std::uint8_t out[4]) {
    const int lim[4] = {255, 255, 255, 128};
    for (int k = 0; k < 4; ++k) out[k] = std::uint8_t(std::min(int(c[k] * 128.0f), lim[k]) & 0xFF);
}

// RenderLinelist / RenderTristrip's untextured colour [C asm 0x364920]: pextlb / pextlh, paddw (x 2), pminw with
// (255, 255, 255, 128)
void untextured(std::uint8_t c[4]) {
    const int lim[4] = {255, 255, 255, 128};
    for (int k = 0; k < 4; ++k) c[k] = std::uint8_t(std::min(2 * int(c[k]), lim[k]));
}

GlobeVertex vertex(const float p[3], float u, float v, const std::uint8_t c[4]) {
    GlobeVertex o;
    for (int k = 0; k < 3; ++k) o.pos[k] = p[k];
    o.uv[0] = u;
    o.uv[1] = v;
    for (int k = 0; k < 4; ++k) o.rgba[k] = c[k];
    return o;
}

}  // namespace

const GlobeGrids& globe_grids() {
    static const GlobeGrids g = [] {
        GlobeGrids o{};
        // CB3Globe2dObject::Prepare 0x3B55A0 [L draft]: lat = (i - 16) / 16, lon = (j - 32) / 32; plane = kRight 2 lon +
        // kUp clamp(lat, +-0.875) / 0.875; sphere = the up row of R(R(kUp, -180 lon).right, 90 lat - 90) x R(kUp, ...)
        for (int i = 0; i < 33; ++i) {
            const float lat = float(i - 16) / 16.0f;
            const float pitch = 90.0f * lat - 90.0f;
            const float cl = std::clamp(lat, -0.875f, 0.875f);
            for (int j = 0; j < 65; ++j) {
                const float lon = float(j - 32) / 32.0f;
                float* pl = o.plane[i * 65 + j];
                for (int k = 0; k < 3; ++k) pl[k] = kRightAxis[k] * (2.0f * lon) + kUpAxis[k] * (cl / 0.875f);
                const float yaw = kDegToRad * (-180.0f * lon);
                const M34 ry = rotation(kUpAxis, yaw);
                const M34 rp = rotation(ry.r[0], kDegToRad * pitch);
                for (int k = 0; k < 3; ++k) o.sphere[i * 65 + j][k] = rp.r[1][k];
            }
        }
        return o;
    }();
    return g;
}

std::vector<GlobeBatch> globe_batches(const HudGlobe& d) {
    const GlobeGrids& g = globe_grids();
    std::vector<GlobeBatch> out;
    const float* col = d.space + 4;   // the object space's colour (CGt2dObjectSpace +0x10)
    // _Render 0x3B2EE0 [L draft, every constant C asm]
    const float alpha = float(d.alpha) / 255.0f;
    float halo_c[4] = {col[0], col[1], col[2], col[3]}, surf_c[4] = {col[0], col[1], col[2], col[3] * alpha};
    if (d.shape[0] != 0xFFFF) halo_c[3] = 0.0f;
    const float f = 0.75f * alpha + 0.25f;
    const float line_c[4] = {0.8156862855f * f * col[0], 0.8980392218f * f * col[1], f * col[2], 1.0f * col[3]};
    std::uint8_t surf[4], halo[4], line[4];
    pack(surf_c, surf);
    pack(halo_c, halo);
    pack(line_c, line);
    const float shape = float(d.shape[1]) / 65535.0f;
    const float lat = 180.0f * float(d.lat) / 65535.0f;
    const float one_minus = 1.0f - shape;
    float lon = 360.0f * float(d.lon) / 65535.0f;
    lon = 0.0f + lon - 28.125f * (float(d.shape[0]) / 65535.0f) * one_minus;
    if (lon < 0.0f) lon += 360.0f;
    const int cell = int(lon / 5.625f);
    const float frac = 0.0f + lon - float(cell) * 5.625f;
    M34 m = mul(rotation(kUpAxis, kDegToRad * -frac), rotation(kRightAxis, kDegToRad * (one_minus * (lat * -1.0f))));
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) m.r[i][k] *= 100.0f;
    for (int k = 0; k < 3; ++k) m.r[3][k] = d.pos[k];
    const float sx = d.k[0], sy = d.k[1];   // the open viewport's +0x48 (D_00666270 -> +0x48): view window half size

    // DrawInvisiblePolys 0x3B2830 [L draft]: three colour-0 strips, Z written (B3Im3d's default): the top and bottom
    // bands at z 1 and the rectangle around the globe's centre at its depth
    {
        const std::uint8_t c0[4] = {0, 0, 0, 0};
        const float top = 0.7041666507720947f * sy, bottom = -0.7166666388511658f * sy;   // D_004E0A8C, D_004E0A90 [C ELF]
        const float bands[2][4][3] = {{{sx, sy, 1}, {-sx, sy, 1}, {sx, top, 1}, {-sx, top, 1}},
                                      {{sx, bottom, 1}, {-sx, bottom, 1}, {sx, -sy, 1}, {-sx, -sy, 1}}};
        for (const auto& b : bands) {
            GlobeBatch s;
            for (const auto& p : b) s.v.push_back(vertex(p, 0, 0, c0));
            out.push_back(std::move(s));
        }
        const float* p = d.pos;
        const float l = p[2] * (0.8125f * -sx), r = p[2] * (0.8125f * sx);
        const float t = p[2] * (1.0833333730697632f * sy), bo = p[2] * (1.0833333730697632f * -sy);   // D_004E0A94
        const float q[4][3] = {{r + p[0], t + p[1], p[2]}, {l + p[0], t + p[1], p[2]}, {r + p[0], bo + p[1], p[2]}, {l + p[0], bo + p[1], p[2]}};
        GlobeBatch s;
        for (const auto& v : q) s.v.push_back(vertex(v, 0, 0, c0));
        out.push_back(std::move(s));
    }

    const float tl = std::clamp(lat / 90.0f, -0.875f, 0.875f);
    float off[3];   // offUp + offRight
    for (int k = 0; k < 3; ++k) off[k] = kUpAxis[k] * -(tl / 0.875f) + kRightAxis[k] * (frac / 180.0f);
    // one wireframe / surface point: the sphere point placed by the globe matrix, morphed towards the scrolled plane point
    // by the shape value (_Render's inl4). Indices are flat (row x 65 + column), as the game's row-major reads: column
    // 65 is the next row's column 0 [C]; past the plane grid's end (the surface's last strip at row 32 reads the
    // static data's vertex buffer) the last plane point stands in [G]
    auto point = [&](int idx, float o[3]) {
        float p[3];
        transform(m, g.sphere[std::min(idx, 33 * 65 - 1)], p);
        const float* pl = g.plane[std::min(idx, 33 * 65 - 1)];
        for (int k = 0; k < 3; ++k) {
            const float q = (pl[k] + off[k]) * 100.0f + m.r[3][k];
            o[k] = p[k] + (q - p[k]) * shape;
        }
    };

    // the wireframe: untextured, Z not written, anti-aliased lines, alpha 0x80
    line[3] = 0x80;
    untextured(line);
    const int parity = cell % 2;
    const int limit = d.reveal < 0x80 ? 65 - (int(d.reveal) * 65) / 128 : 0;
    auto lines = [&]() {
        GlobeBatch b;
        b.kind = GlobeBatch::kLines;
        b.zwrite = false;
        b.antialias = true;
        return b;
    };
    auto seg = [&](GlobeBatch& b, int i0, int i1) {
        float p0[3], p1[3];
        point(i0, p0);
        point(i1, p1);
        b.v.push_back(vertex(p0, 0, 0, line));
        b.v.push_back(vertex(p1, 0, 0, line));
    };
    for (int c = parity; c < 65; c += 2) {   // meridians, rows 2..30
        if (c < limit) continue;
        GlobeBatch b = lines();
        for (int r = 2; r < 30; ++r) seg(b, r * 65 + c, (r + 1) * 65 + c);
        out.push_back(std::move(b));
    }
    for (int c = parity; c < 65; c += 4) {   // the polar ends, every fourth column
        if (c < limit) continue;
        GlobeBatch b = lines();
        for (int r = 0; r < 2; ++r) seg(b, r * 65 + c, (r + 1) * 65 + c);
        for (int r = 30; r < 32; ++r) seg(b, r * 65 + c, (r + 1) * 65 + c);
        out.push_back(std::move(b));
    }
    for (int r = 0; r < 33; r += 2) {        // parallels, every second row (flushed every 64 vertices: same draws)
        GlobeBatch b = lines();
        for (int c = 0; c < 64; ++c) {
            if (c < limit) continue;
            seg(b, r * 65 + c, r * 65 + c + 1);
        }
        if (!b.v.empty()) out.push_back(std::move(b));
    }

    // the surface (revealed: data +0x15 >= 0x80): the texture D_01EA4608, Z written, back faces culled; one strip per
    // column, u scrolled by the longitude cell
    if (d.reveal >= 0x80) {
        const int row_limit = d.reveal < 0xFF ? 33 - ((int(d.reveal) - 0x80) * 33) / 127 : 0;
        for (int c = 0; c < 65; ++c) {
            GlobeBatch b;
            b.texture = 0;
            b.cull = true;
            const float u0 = float(cell + 64 - (c + 1)) / 64.0f, u1 = float(cell + 64 - c) / 64.0f;
            for (int r = 0; r < 33; ++r) {
                if (r < row_limit) continue;
                const float v = float(32 - r) / 32.0f;
                float a[3], bb[3];
                point(r * 65 + c + 1, a);
                point(r * 65 + c, bb);
                b.v.push_back(vertex(a, u0, v, surf));
                b.v.push_back(vertex(bb, u1, v, surf));
            }
            if (b.v.size() > 2) out.push_back(std::move(b));
        }
        // the highlighted region: 16 columns of the sphere grid (unmorphed), its own texture, colour alpha x amount
        const float h = float(d.highlight) / 255.0f;
        const float hc_f[4] = {col[0], col[1], col[2], col[3] * h};
        std::uint8_t hc[4];
        pack(hc_f, hc);
        int tex = -1, base = 0, row0 = 0, rows = 0;
        float vs = 1;
        switch (d.region) {
        case 0: tex = 1; base = 24 - (48 - cell); row0 = 16; rows = 8; vs = 8.0f; break;
        case 1: tex = 2; base = 24 - (3 - cell); row0 = 19; rows = 8; vs = 8.0f; break;
        case 2: tex = 3; base = 24 - (20 - cell); row0 = 12; rows = 16; vs = 16.0f; break;
        default: break;
        }
        for (int c = 0; tex >= 0 && c < 16; ++c) {
            GlobeBatch b;
            b.texture = tex;
            b.cull = true;
            const float u0 = float(16 - (c + 1)) / 16.0f, u1 = float(16 - c) / 16.0f;
            for (int r = 0; r <= rows; ++r) {
                const float v = float(rows - r) / vs;
                const int i0 = std::clamp((row0 + r) * 65 + base + c + 1, 0, 33 * 65 - 1), i1 = std::clamp((row0 + r) * 65 + base + c, 0, 33 * 65 - 1);
                float a[3], bb[3];
                transform(m, g.sphere[i0], a);
                transform(m, g.sphere[i1], bb);
                b.v.push_back(vertex(a, u0, v, hc));
                b.v.push_back(vertex(bb, u1, v, hc));
            }
            out.push_back(std::move(b));
        }
    }

    // DrawGlobeHalo 0x3B17F0 [L draft]: the halo texture as four quarter quads around the centre, each mirrored, half
    // size 500 x (0.8125 sx, 1.0833 sy) at the centre's depth; past 180 degrees of the reveal angle the left two turn
    // about kUp by (angle - 270) degrees (0 from 270 on). UVs: CB3MenuImageUVs::_CalculateUV(0.5, 0.5, 255, 255, 256,
    // 256) = 0.5 / 256 .. 255.5 / 256 [L]. Z still written, no culling.
    {
        const float angle = d.reveal < 0x7F ? 360.0f * float(65 - limit) / 65.0f + frac : 360.0f;
        const float* p = d.pos;
        const float k = 500.0f / p[2];
        const float u0 = 0.5f / 256.0f, v0 = 0.5f / 256.0f, u1 = 255.5f / 256.0f, v1 = 255.5f / 256.0f;
        const float W = p[2] * (0.8125f * sx) * k, H = p[2] * (1.0833333730697632f * sy) * k;
        struct Corner { float x, y, u, v; };
        auto quad = [&](const Corner (&cs)[4], const M34* rot) {
            GlobeBatch b;
            b.texture = 4;
            for (const Corner& c : cs) {
                const float q[3] = {c.x, c.y, 0};
                float r[3] = {q[0], q[1], q[2]};
                if (rot) transform(*rot, q, r);
                for (int i = 0; i < 3; ++i) r[i] += p[i];
                b.v.push_back(vertex(r, c.u, c.v, halo));
            }
            out.push_back(std::move(b));
        };
        if (!(angle < 180.0f)) {
            const float t = angle < 270.0f ? angle - 180.0f - 90.0f : 0.0f;
            const M34 rot = rotation(kUpAxis, kDegToRad * t);
            quad({{0, H, u1, v0}, {-W, H, u0, v0}, {0, 0, u1, v1}, {-W, 0, u0, v1}}, &rot);
            quad({{0, 0, u1, v1}, {-W, 0, u0, v1}, {0, -H, u1, v0}, {-W, -H, u0, v0}}, &rot);
        }
        quad({{W, H, u0, v0}, {0, H, u1, v0}, {W, 0, u0, v1}, {0, 0, u1, v1}}, nullptr);
        quad({{W, 0, u0, v1}, {0, 0, u1, v1}, {W, -H, u0, v0}, {0, -H, u1, v0}}, nullptr);
    }
    return out;
}

}  // namespace b3replay
