#include "b3asset/preview.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace b3asset {

namespace {
struct V3 { float x, y, z; };
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a) { const float l = std::sqrt(dot(a, a)); return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : a; }
}  // namespace

Image render_preview(const Model& m, int w, int h, const PreviewView& view) {
    Image img;
    img.w = w;
    img.h = h;
    img.rgba.assign(std::size_t(w) * h * 4, 0);
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) { img.rgba[i] = 40; img.rgba[i + 1] = 44; img.rgba[i + 2] = 52; img.rgba[i + 3] = 255; }
    if (m.vertices.empty()) return img;
    const V3 c{(m.bmin[0] + m.bmax[0]) / 2, (m.bmin[1] + m.bmax[1]) / 2, (m.bmin[2] + m.bmax[2]) / 2};
    const float radius = 0.5f * std::sqrt((m.bmax[0] - m.bmin[0]) * (m.bmax[0] - m.bmin[0]) +
                                          (m.bmax[1] - m.bmin[1]) * (m.bmax[1] - m.bmin[1]) +
                                          (m.bmax[2] - m.bmin[2]) * (m.bmax[2] - m.bmin[2]));
    const float fov = view.fov_deg * 3.14159265f / 180.0f;
    V3 eye, target = c;
    if (view.has_camera) {
        eye = {view.eye[0], view.eye[1], view.eye[2]};
        target = {view.target[0], view.target[1], view.target[2]};
    } else {
        const float yaw = view.yaw_deg * 3.14159265f / 180.0f, pitch = view.pitch_deg * 3.14159265f / 180.0f;
        const float dist = view.distance_scale * radius / std::sin(fov / 2);
        eye = {c.x + dist * std::cos(pitch) * std::sin(yaw), c.y + dist * std::sin(pitch), c.z + dist * std::cos(pitch) * std::cos(yaw)};
    }
    const V3 fwd = norm(sub(target, eye));
    const V3 right = norm(cross(fwd, V3{0, 1, 0}));
    const V3 up = cross(right, fwd);
    const float f = 1.0f / std::tan(fov / 2), aspect = float(w) / h;
    const float znear = std::max(0.01f, radius * 0.001f);
    std::vector<float> depth(std::size_t(w) * h, 1e30f);
    const V3 light = norm(V3{0.4f, 0.8f, 0.3f});

    struct P { float sx, sy, z; bool ok; };
    std::vector<P> proj(m.vertices.size());
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const Vertex& v = m.vertices[i];
        const V3 d = sub(V3{v.pos[0], v.pos[1], v.pos[2]}, eye);
        const float z = dot(d, fwd);
        proj[i].ok = z > znear;
        proj[i].z = z;
        proj[i].sx = (dot(d, right) * f / aspect / z * 0.5f + 0.5f) * w;
        proj[i].sy = (0.5f - dot(d, up) * f / z * 0.5f) * h;
    }
    for (const DrawRange& dr : m.draws) {
        const Texture* tex = dr.texture >= 0 && view.textured ? &m.textures[dr.texture] : nullptr;
        if (tex && (tex->levels.empty() || tex->levels[0].rgba.empty())) tex = nullptr;   // hdgpu: a BC level has no texels here
        for (std::uint32_t t = 0; t < dr.index_count; t += 3) {
            const std::uint32_t* idx = &m.indices[dr.first_index + t];
            const P &a = proj[idx[0]], &b = proj[idx[1]], &cc = proj[idx[2]];
            if (!a.ok || !b.ok || !cc.ok) continue;
            const float area = (b.sx - a.sx) * (cc.sy - a.sy) - (b.sy - a.sy) * (cc.sx - a.sx);
            if (std::fabs(area) < 1e-12f) continue;
            const int x0 = std::max(0, int(std::floor(std::min({a.sx, b.sx, cc.sx}))));
            const int x1 = std::min(w - 1, int(std::ceil(std::max({a.sx, b.sx, cc.sx}))));
            const int y0 = std::max(0, int(std::floor(std::min({a.sy, b.sy, cc.sy}))));
            const int y1 = std::min(h - 1, int(std::ceil(std::max({a.sy, b.sy, cc.sy}))));
            const Vertex &va = m.vertices[idx[0]], &vb = m.vertices[idx[1]], &vc = m.vertices[idx[2]];
            const V3 n = norm(V3{va.normal[0] + vb.normal[0] + vc.normal[0], va.normal[1] + vb.normal[1] + vc.normal[1],
                                 va.normal[2] + vb.normal[2] + vc.normal[2]});
            const float shade = view.shaded ? 0.45f + 0.55f * std::fabs(dot(n, light)) : 1.0f;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    float w0 = ((b.sx - px) * (cc.sy - py) - (b.sy - py) * (cc.sx - px)) / area;
                    float w1 = ((cc.sx - px) * (a.sy - py) - (cc.sy - py) * (a.sx - px)) / area;
                    float w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    // perspective-correct weights
                    const float iz = w0 / a.z + w1 / b.z + w2 / cc.z;
                    const float z = 1 / iz;
                    float &dz = depth[std::size_t(y) * w + x];
                    if (z >= dz) continue;
                    const float p0 = w0 / a.z * z, p1 = w1 / b.z * z, p2 = w2 / cc.z * z;
                    float col[4] = {1, 1, 1, 1};
                    auto vc4 = [](std::uint32_t c, int k) { return ((c >> (8 * k)) & 0xFF) / 255.0f; };
                    for (int k = 0; k < 4; ++k)
                        col[k] = (p0 * vc4(va.rgba, k) + p1 * vc4(vb.rgba, k) + p2 * vc4(vc.rgba, k)) * vc4(dr.tint, k);
                    // Tracks keep GS bytes (0x80 = 1.0) and instances take their colour from the placement.
                    if (dr.transform >= 0 && std::size_t(dr.transform) < m.transform_tints.size())
                        for (int k = 0; k < 3; ++k) col[k] = m.transform_tints[std::size_t(dr.transform)][std::size_t(k)] * 128.5f / 255.0f;
                    for (int k = 0; k < 3; ++k) col[k] *= m.colour_scale;
                    if (m.colour_scale > 1.0f) col[3] *= m.colour_scale / 2.0f;
                    if (tex) {
                        const Image& ti = tex->levels[0];
                        float u = p0 * va.uv[0] + p1 * vb.uv[0] + p2 * vc.uv[0];
                        float v = p0 * va.uv[1] + p1 * vb.uv[1] + p2 * vc.uv[1];
                        u -= std::floor(u);
                        v -= std::floor(v);
                        const int tx = std::min(ti.w - 1, int(u * ti.w)), ty = std::min(ti.h - 1, int(v * ti.h));
                        const std::uint8_t* tp = &ti.rgba[(std::size_t(ty) * ti.w + tx) * 4];
                        if (dr.blend == DrawRange::kAlphaTest && tp[3] < 128) continue;
                        for (int k = 0; k < 4; ++k) col[k] *= tp[k] / 255.0f;
                        if (dr.blend != DrawRange::kBlend) col[3] = 1.0f;
                    }
                    dz = z;
                    std::uint8_t* o = &img.rgba[(std::size_t(y) * w + x) * 4];
                    for (int k = 0; k < 3; ++k) {
                        const float s = std::min(1.0f, col[k] * shade);
                        o[k] = std::uint8_t(dr.blend == DrawRange::kBlend ? o[k] * (1 - col[3]) + 255 * s * col[3] : 255 * s);
                    }
                    o[3] = 255;
                }
        }
    }
    return img;
}

}  // namespace b3asset
