// carpaint: the paint / livery map of a player car's atlas (include/b3asset/carpaint.h).
#include "b3asset/carpaint.h"

#include "b3asset/car.h"
#include "b3asset/model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace b3asset {

namespace {

bool fail(std::string* why, const char* what) { if (why) *why = what; return false; }

// A texel's colour changes with the paint when some palette's decode differs from palette 0's by more than this (texel
// bytes): CLUT entries of the livery are exact copies; paints differ by tens.
constexpr int kPaintSpread = 8;
// Two paint texels are the same colour (a shade of it) when their colour signatures (the texel in every palette, one
// unit vector) agree to this cosine.
constexpr float kSameColour = 0.97f;
// A fixed island is a decal when this share of its border is paint (the atlas's edge counts half, as not paint), and it
// covers less than kDecalMaxShare of the atlas (the big fixed half with the lights and wheels is never one).
constexpr float kDecalBorder = 0.75f;
constexpr int kDecalMaxShare = 10;   // 1 / 10
// Logos are anti-aliased into the paint: the main paint texels this close to a decal or a second colour (logos printed
// in a tone of the paint) belong to it.
constexpr int kDecalGrow = 2;
// A paint island inside a decal (the paint through its letters) is part of it below this share of the atlas.
constexpr int kHoleMaxShare = 50;   // 1 / 50

// A fixed island that body triangles sample is printed on paint when at least this share of those triangles' texels is
// paint (a logo on a door: the door's paint around it; a plate's own quad: next to none).
constexpr float kPrintedShare = 0.2f;

float brightest(const std::uint8_t* c) { return float(std::max({c[0], c[1], c[2]})); }

// The atlas texels whose centres a triangle covers (the texel under its centroid when none: a sliver), wrapped into the
// atlas (texture coordinates repeat).
template <class F>
void raster_triangle(const AtlasTriangle& t, int W, int H, F&& texel) {
    float x[3], y[3];
    for (int k = 0; k < 3; ++k) { x[k] = t.uv[k][0] * float(W); y[k] = t.uv[k][1] * float(H); }
    const float x0 = std::min({x[0], x[1], x[2]}), x1 = std::max({x[0], x[1], x[2]});
    const float y0 = std::min({y[0], y[1], y[2]}), y1 = std::max({y[0], y[1], y[2]});
    if (!(x1 - x0 < 2.0f * float(W)) || !(y1 - y0 < 2.0f * float(H))) return;   // degenerate or not a texture mapping
    auto wrap = [](int v, int n) { v %= n; return v < 0 ? v + n : v; };
    const float area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    bool any = false;
    if (std::fabs(area) > 1e-6f) {
        for (int Y = int(std::floor(y0)); Y <= int(std::ceil(y1)); ++Y)
            for (int X = int(std::floor(x0)); X <= int(std::ceil(x1)); ++X) {
                const float px = float(X) + 0.5f, py = float(Y) + 0.5f;
                float w[3];
                for (int k = 0; k < 3; ++k) {
                    const int a = (k + 1) % 3, b = (k + 2) % 3;
                    w[k] = ((x[b] - x[a]) * (py - y[a]) - (y[b] - y[a]) * (px - x[a])) / area;
                }
                if (w[0] < -1e-4f || w[1] < -1e-4f || w[2] < -1e-4f) continue;
                texel(wrap(X, W), wrap(Y, H));
                any = true;
            }
    }
    if (!any) texel(wrap(int(std::floor((x[0] + x[1] + x[2]) / 3.0f)), W), wrap(int(std::floor((y[0] + y[1] + y[2]) / 3.0f)), H));
}

}  // namespace

bool parse_rgb(const std::string& text, std::uint32_t& out) {
    std::string t = text;
    if (!t.empty() && t[0] == '#') t.erase(0, 1);
    if (t.size() != 6) return false;
    std::uint32_t v = 0;
    for (char ch : t) {
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else return false;
        v = v << 4 | std::uint32_t(d);
    }
    out = v;
    return true;
}

std::vector<AtlasTriangle> car_atlas_triangles(Bytes f) {
    std::vector<AtlasTriangle> out;
    for (int lod = 0; lod < 4; ++lod) {
        if (!f.has(0x4C + 4 * std::size_t(lod), 4) || !f.u32(0x4C + 4 * std::size_t(lod))) continue;
        CarOptions co;
        co.lod = lod;
        co.bake_frames = false;
        co.texture = false;
        Model m;
        GsMemory gs;
        if (!read_car(f, co, gs, m, nullptr)) continue;
        for (const DrawRange& d : m.draws) {
            if ((d.material >> 24) != 0) continue;   // texture slots 1..4: the underside and the glass (GLOBAL.TXD)
            AtlasTriangle::Kind kind = AtlasTriangle::kBody;
            if (!d.part.empty() && d.part[0] == 'B') kind = AtlasTriangle::kWheel;
            else if ((d.material & 0xFCu) != 0) kind = AtlasTriangle::kLight;   // light bits of the entry mask [C car-shading.md §3]
            for (std::uint32_t i = d.first_index; i + 2 < d.first_index + d.index_count && i + 2 < m.indices.size(); i += 3) {
                AtlasTriangle t;
                t.kind = kind;
                for (int k = 0; k < 3; ++k) {
                    const std::uint32_t vi = m.indices[i + std::uint32_t(k)];
                    if (vi >= m.vertices.size()) { t.kind = AtlasTriangle::kLight; break; }
                    t.uv[k][0] = m.vertices[vi].uv[0];
                    t.uv[k][1] = m.vertices[vi].uv[1];
                }
                out.push_back(t);
            }
        }
    }
    return out;
}

bool car_paint_map_from(const std::vector<Image>& pal, CarPaintMap& out, std::string* why, const std::vector<AtlasTriangle>* tris) {
    out = CarPaintMap{};
    if (pal.size() < 2) return fail(why, "one paint: no paint to tell from the livery");
    const int W = pal[0].w, H = pal[0].h, npal = int(std::min<std::size_t>(pal.size(), 8));
    if (W <= 0 || H <= 0) return fail(why, "empty atlas");
    for (int k = 0; k < npal; ++k)
        if (pal[std::size_t(k)].w != W || pal[std::size_t(k)].h != H || pal[std::size_t(k)].rgba.size() != std::size_t(W) * H * 4)
            return fail(why, "palette decodes differ in size");
    const int n = W * H;
    auto px = [&](int k, int p) { return &pal[std::size_t(k)].rgba[std::size_t(p) * 4]; };

    // 1. paint: the texels that change with the palette
    std::vector<std::uint8_t> paint(std::size_t(n), 0);
    int npaint = 0;
    for (int p = 0; p < n; ++p) {
        int spread = 0;
        for (int k = 1; k < npal; ++k)
            for (int c = 0; c < 3; ++c) spread = std::max(spread, std::abs(int(px(k, p)[c]) - int(px(0, p)[c])));
        paint[std::size_t(p)] = spread > kPaintSpread;
        npaint += paint[std::size_t(p)];
    }
    if (npaint == 0) return fail(why, "no texel changes with the paint");

    // 2. the main paint colour: the colour signature most paint texels share (a mode, then refined); the rest of the paint
    // is a second (livery) colour
    const int dims = npal * 3;
    std::vector<float> sig(std::size_t(n) * std::size_t(dims), 0.0f);
    for (int p = 0; p < n; ++p) {
        if (!paint[std::size_t(p)]) continue;
        float* s = &sig[std::size_t(p) * std::size_t(dims)];
        float l2 = 0;
        for (int k = 0; k < npal; ++k)
            for (int c = 0; c < 3; ++c) { const float v = float(px(k, p)[c]) + 1.0f; s[k * 3 + c] = v; l2 += v * v; }
        l2 = std::sqrt(l2);
        for (int j = 0; j < dims; ++j) s[j] /= l2;
    }
    auto dot = [&](const float* a, const float* b) { float d = 0; for (int j = 0; j < dims; ++j) d += a[j] * b[j]; return d; };
    std::vector<float> M(std::size_t(dims), 0.0f);
    {
        const int step = std::max(1, npaint / 97);
        int best = -1, best_n = -1, seen = 0;
        for (int c = 0; c < n; ++c) {
            if (!paint[std::size_t(c)] || (seen++ % step) != 0) continue;
            const float* sc = &sig[std::size_t(c) * std::size_t(dims)];
            int m = 0;
            for (int p = 0; p < n; ++p) if (paint[std::size_t(p)] && dot(sc, &sig[std::size_t(p) * std::size_t(dims)]) > kSameColour) ++m;
            if (m > best_n) { best_n = m; best = c; }
        }
        std::memcpy(M.data(), &sig[std::size_t(best) * std::size_t(dims)], sizeof(float) * std::size_t(dims));
        for (int it = 0; it < 3; ++it) {
            std::vector<float> sum(std::size_t(dims), 0.0f);
            for (int p = 0; p < n; ++p) {
                if (!paint[std::size_t(p)]) continue;
                const float* s = &sig[std::size_t(p) * std::size_t(dims)];
                if (dot(M.data(), s) > kSameColour) for (int j = 0; j < dims; ++j) sum[std::size_t(j)] += s[j];
            }
            float l2 = 0;
            for (float v : sum) l2 += v * v;
            if (!(l2 > 0)) break;
            l2 = std::sqrt(l2);
            for (int j = 0; j < dims; ++j) M[std::size_t(j)] = sum[std::size_t(j)] / l2;
        }
    }
    // class per texel: 0 fixed, 1 main paint, 2 second colour, 3 decal
    std::vector<std::uint8_t> cls(std::size_t(n), 0);
    for (int p = 0; p < n; ++p)
        if (paint[std::size_t(p)]) cls[std::size_t(p)] = dot(M.data(), &sig[std::size_t(p) * std::size_t(dims)]) > kSameColour ? 1 : 2;

    // 3. the palettes' main colours, and the reference: the brightest main paint (the most shading levels)
    float best_level = -1;
    for (int k = 0; k < npal; ++k) {
        double s[3] = {}, b = 0;
        int cnt = 0;
        for (int p = 0; p < n; ++p) {
            if (cls[std::size_t(p)] != 1) continue;
            for (int c = 0; c < 3; ++c) s[c] += px(k, p)[c];
            b += brightest(px(k, p));
            ++cnt;
        }
        if (cnt == 0) return fail(why, "no main paint");
        for (int c = 0; c < 3; ++c) out.main[k][c] = float(s[c] / cnt / 255.0);
        const float level = float(b / cnt / 255.0);
        if (level > best_level) { best_level = level; out.ref = k; }
    }
    out.ref_level = best_level;
    if (!(out.ref_level > 0.5f / 255.0f)) return fail(why, "the paints are black");

    // 4. decals: the fixed islands (4-connected) inside the paint
    std::vector<int> comp(std::size_t(n), -1);
    std::vector<int> stack, members, candidate;
    int ncomp = 0;
    for (int p0 = 0; p0 < n; ++p0) {
        if (cls[std::size_t(p0)] != 0 || comp[std::size_t(p0)] >= 0) continue;
        members.clear();
        stack.assign(1, p0);
        comp[std::size_t(p0)] = ncomp;
        int border_paint = 0, border_fixed = 0, border_edge = 0;
        while (!stack.empty()) {
            const int p = stack.back();
            stack.pop_back();
            members.push_back(p);
            const int x = p % W, y = p / W;
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                const int X = x + dx[d], Y = y + dy[d];
                if (X < 0 || Y < 0 || X >= W || Y >= H) { ++border_edge; continue; }
                const int q = Y * W + X;
                if (cls[std::size_t(q)] != 0) { ++border_paint; continue; }
                if (comp[std::size_t(q)] < 0) { comp[std::size_t(q)] = ncomp; stack.push_back(q); }
            }
        }
        ++ncomp;
        (void)border_fixed;
        if (float(border_paint) >= kDecalBorder * (float(border_paint) + 0.5f * float(border_edge)) &&
            members.size() < std::size_t(n / kDecalMaxShare))
            candidate.push_back(ncomp - 1);
    }
    // the meshes' word on each candidate island: the triangles that sample it, the paint share of their texels
    std::vector<std::uint8_t> is_decal(std::size_t(ncomp), 0);
    for (int c : candidate) is_decal[std::size_t(c)] = 1;
    if (tris && !tris->empty()) {
        std::vector<double> paint_texels(std::size_t(ncomp), 0), all_texels(std::size_t(ncomp), 0);
        std::vector<std::uint8_t> part(std::size_t(ncomp), 0);
        std::vector<int> touched;
        for (const AtlasTriangle& t : *tris) {
            touched.clear();
            int tp = 0, ta = 0;
            raster_triangle(t, W, H, [&](int x, int y) {
                const int p = y * W + x;
                ++ta;
                if (cls[std::size_t(p)] != 0) ++tp;
                else if (const int c = comp[std::size_t(p)]; c >= 0 && is_decal[std::size_t(c)] &&
                         std::find(touched.begin(), touched.end(), c) == touched.end())
                    touched.push_back(c);
            });
            for (int c : touched) {
                if (t.kind != AtlasTriangle::kBody) part[std::size_t(c)] = 1;
                paint_texels[std::size_t(c)] += tp;
                all_texels[std::size_t(c)] += ta;
            }
        }
        for (int c : candidate)
            if (part[std::size_t(c)] || (all_texels[std::size_t(c)] > 0 && paint_texels[std::size_t(c)] < kPrintedShare * all_texels[std::size_t(c)])) {
                is_decal[std::size_t(c)] = 0;
                ++out.parts_kept;
            }
    }
    for (int p = 0; p < n; ++p)
        if (cls[std::size_t(p)] == 0 && comp[std::size_t(p)] >= 0 && is_decal[std::size_t(comp[std::size_t(p)])]) cls[std::size_t(p)] = 3;
    for (int c : candidate) out.decal_islands += is_decal[std::size_t(c)];
    std::vector<std::uint8_t> grown(std::size_t(n), 0);   // paint texels taken into a decal (its edge, its holes)
    {   // the holes: small paint islands inside a decal (the paint showing through a logo's letters, a box's text) belong
        // to it; as paint they keep their own shade, a logo's ghost once the decal around them is filled
        std::vector<int> pcomp(std::size_t(n), -1);
        int np = 0;
        for (int p0 = 0; p0 < n; ++p0) {
            if (cls[std::size_t(p0)] == 0 || cls[std::size_t(p0)] == 3 || pcomp[std::size_t(p0)] >= 0) continue;
            members.clear();
            stack.assign(1, p0);
            pcomp[std::size_t(p0)] = np;
            int border_decal = 0, border_other = 0;
            while (!stack.empty()) {
                const int p = stack.back();
                stack.pop_back();
                members.push_back(p);
                const int x = p % W, y = p / W;
                const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    const int X = x + dx[d], Y = y + dy[d];
                    if (X < 0 || Y < 0 || X >= W || Y >= H) { ++border_other; continue; }
                    const int q = Y * W + X;
                    const int cq = cls[std::size_t(q)];
                    if (cq == 3) { ++border_decal; continue; }
                    if (cq == 0) { ++border_other; continue; }
                    if (pcomp[std::size_t(q)] < 0) { pcomp[std::size_t(q)] = np; stack.push_back(q); }
                }
            }
            ++np;
            if (members.size() < std::size_t(n / kHoleMaxShare) && float(border_decal) >= kDecalBorder * float(border_decal + border_other)) {
                for (int p : members) grown[std::size_t(p)] = 1;
                ++out.decal_holes;
            }
        }
    }
    for (int g = 0; g < kDecalGrow; ++g) {
        std::vector<std::uint8_t> next = grown;
        for (int p = 0; p < n; ++p) {
            if (cls[std::size_t(p)] != 1 || grown[std::size_t(p)]) continue;   // main paint next to a decal or a second colour
            const int x = p % W, y = p / W;
            bool near = false;
            for (int dy = -1; dy <= 1 && !near; ++dy)
                for (int dx = -1; dx <= 1 && !near; ++dx) {
                    const int X = x + dx, Y = y + dy;
                    if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                    const int q = Y * W + X;
                    near = cls[std::size_t(q)] >= 2 || grown[std::size_t(q)];
                }
            if (near) next[std::size_t(p)] = 1;
        }
        grown.swap(next);
    }

    // 5. the shade: main paint texels' own (reference decode's brightest channel over the mean); inpainted from the main
    // paint into the second colours and decals (each pass fills the texels next to known ones with their mean)
    std::vector<float> shade(std::size_t(n), 1.0f);
    std::vector<std::uint8_t> known(std::size_t(n), 0);
    const float ref_bytes = out.ref_level * 255.0f;
    for (int p = 0; p < n; ++p)
        if (cls[std::size_t(p)] == 1 && !grown[std::size_t(p)]) {
            shade[std::size_t(p)] = brightest(px(out.ref, p)) / ref_bytes;
            known[std::size_t(p)] = 1;
        }
    std::vector<int> todo;
    for (int p = 0; p < n; ++p) if (!known[std::size_t(p)] && (cls[std::size_t(p)] >= 2 || grown[std::size_t(p)])) todo.push_back(p);
    while (!todo.empty()) {
        std::vector<int> filled, rest;
        std::vector<float> value;
        for (int p : todo) {
            const int x = p % W, y = p / W;
            float s = 0;
            int c = 0;
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                const int X = x + dx[d], Y = y + dy[d];
                if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                const int q = Y * W + X;
                if (known[std::size_t(q)]) { s += shade[std::size_t(q)]; ++c; }
            }
            if (c) { filled.push_back(p); value.push_back(s / float(c)); }
            else rest.push_back(p);
        }
        if (filled.empty()) break;   // an island with no main paint around it: shade 1
        for (std::size_t i = 0; i < filled.size(); ++i) { shade[std::size_t(filled[i])] = value[i]; known[std::size_t(filled[i])] = 1; }
        todo.swap(rest);
    }

    // 6. the mask
    out.w = W;
    out.h = H;
    out.palettes = npal;
    out.mask.w = W;
    out.mask.h = H;
    out.mask.rgba.assign(std::size_t(n) * 4, 0);
    for (int p = 0; p < n; ++p) {
        std::uint8_t* m = &out.mask.rgba[std::size_t(p) * 4];
        const int c = cls[std::size_t(p)];
        m[0] = c == 1 ? 255 : 0;
        m[1] = c == 2 ? 255 : 0;
        m[2] = c == 3 || grown[std::size_t(p)] ? 255 : 0;
        m[3] = std::uint8_t(std::lround(std::clamp(shade[std::size_t(p)] * 0.5f, 0.0f, 1.0f) * 255.0f));
        out.main_texels += c == 1;
        out.second_texels += c == 2;
        out.decal_texels += m[2] != 0;
    }
    return true;
}

bool car_paint_map(Bytes f, CarPaintMap& out, std::string* why) {
    out = CarPaintMap{};
    if (!f.has(0, 0x64) || f.u32(0) != 0x17) return fail(why, "not a version 0x17 vehicle file");
    const std::uint32_t at = f.u32(0x60);
    if (!at || !f.has(at, 0xC8)) return fail(why, "no texture");
    const int npal = std::min<int>(8, f.p[at + 0xA0]);
    if (npal < 2) return fail(why, "one paint: no paint to tell from the livery");
    std::vector<Image> pal;
    for (int k = 0; k < npal; ++k) {
        GsMemory gs;
        DecodedRaster r;
        if (!decode_raster_ps2(f, at, gs, r, k, why) || r.levels.empty()) return false;
        pal.push_back(std::move(r.levels[0]));
    }
    const std::vector<AtlasTriangle> tris = car_atlas_triangles(f);
    return car_paint_map_from(pal, out, why, &tris);
}

}  // namespace b3asset
