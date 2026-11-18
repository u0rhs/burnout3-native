// b3replay HUD / 2D reader (hud.h). The event layout is hud_capture.cpp's; the GS register semantics are the GS
// manual's (PRIM, RGBAQ, ST, UV, XYZ2, TEX0, TEX1, CLAMP, SCISSOR, ALPHA, FRAME) [C research/ext/PS2Docs]; the packet
// shapes Gt2dRenderer writes are its Construct 0x21A290 tags (REGLIST ST/RGBAQ/XYZ2 groups, PACKED A+D) [C matched].
#include <array>
#include "hud.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>

#include <zlib.h>

#include "b3asset/gs_memory.h"
#include "b3asset/raster.h"
#include "capture.h"

namespace b3replay {
namespace {

enum Kind : std::uint32_t { kEvBegin = 1, kEvState = 2, kEvTexture = 3, kEvResume = 4, kEvSuspend = 5, kEvEnd = 6, kEvBatch = 7,
                            kEvBatchRef = 8, kEvScene = 9, kEvObject = 10, kEvMarker = 11, kEvVideo = 12, kEvFeCar = 13, kEvGlobe = 14,
                            kEvTicker = 15 };
constexpr std::uint32_t kMovieBase = 0x40A00000U, kMovieSlot = 0x00140000U;   // hud_capture.cpp (menu1)
constexpr std::uint32_t kVideoRender = 0x00326960U;   // CGt2dObjectVideoImage::_Render [C name, decomp tables]
// fe3: C2dObjectBoostBar::_Render and the two boost bar components (_gBoostBarComponentP1 / P2) [C names, decomp tables]
constexpr std::uint32_t kBoostBarRender = 0x001A6290U, kBoostBarP1 = 0x0066A610U, kBoostBarP2 = 0x0066B7B0U;

std::uint64_t fnv(const std::uint8_t* p, std::size_t n, std::uint64_t h = 14695981039346656037ULL) {
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
std::uint64_t rd64(const std::uint8_t* p) { std::uint64_t v; std::memcpy(&v, p, 8); return v; }
std::uint32_t rd32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
float f32_of(std::uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

// The guest raster at `r` as a self-contained "file" for b3asset (raster.h: offsets in place of pointers): the header,
// its upload block and every DMAref'd packet copied from the image, the pointers rewritten to offsets.
bool raster_file(const Image& im, std::uint32_t r, std::vector<std::uint8_t>& f, std::string* why) {
    f.assign(0x100, 0);
    if (!im.read(r, f.data(), 0xC8)) { *why = "raster header not captured"; return false; }
    const std::uint32_t up = im.u32(r + 0x24) & 0x0FFFFFFFU;
    const std::uint32_t n = im.u32(up);
    if (!up || n == 0 || n > 16 || !im.has(up, 16 + 64 * n)) { *why = "upload block not captured"; return false; }
    const std::uint32_t at = 0x100;
    f.resize(at + 16 + 64 * n);
    im.read(up, f.data() + at, 16 + 64 * n);
    std::memcpy(f.data() + 0x24, &at, 4);
    // In RAM the texture cache has made TEX0's TBP0 / CBP absolute (its VRAM slot; CBP = slot + palOffset), while the
    // upload chain stays slot-relative (BITBLTBUF DBP from 0) [C race2 capture: hud_element01 TBP 14848, CBP 14876,
    // palOffset 28]; the files hold TBP0 = CBP = 0 [C all 194 GLOBAL.TXD rasters], so do as they do.
    std::uint64_t tex0 = rd64(f.data() + 0x3C);
    tex0 &= ~(0x3FFFULL | (0x3FFFULL << 37));
    std::memcpy(f.data() + 0x3C, &tex0, 8);
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::size_t e = at + 16 + 64 * k;
        const std::uint32_t ref = rd32(f.data() + e + 48), src = rd32(f.data() + e + 52) & 0x0FFFFFFFU;
        const std::uint32_t qwc = ref & 0xFFFF;
        if (((ref >> 28) & 7) != 3) continue;   // decode_raster reports it
        const std::uint32_t off = std::uint32_t(f.size());
        f.resize(off + 16 * qwc);
        if (!im.read(src, f.data() + off, 16 * qwc)) { *why = "upload packet not captured"; return false; }
        std::memcpy(f.data() + e + 52, &off, 4);
    }
    return true;
}

struct Gs {
    std::uint64_t prim = 0, rgbaq = 0x3F80000080808080ULL, st = 0, uv = 0, tex0 = 0, tex1 = 0, clamp = 0;
    std::uint64_t scissor = 0, alpha = 0x44, frame = 0, test = 0;
    std::uint64_t tex_key = 0;
    const HudTexture* tex = nullptr;
    HudVertex q[3];
    int nq = 0, strip = 0;
};

}  // namespace

void hud_marker_project(HudMarker& m) {
    // DrawPositionMarker [L draft from the asm]: p = TransformPoint(viewport +0x00, point); culled unless near < p.z < far;
    // x = p.x / p.z x sw, y = p.y / p.z x sh (the marker is drawn `height` above it); size = 0.5 sw (1.5 (1 + (p.z - 1) /
    // 74) / p.z) +0x50, at most 320
    float p[3];
    for (int i = 0; i < 3; ++i) p[i] = m.world[0] * m.screen[0][i] + m.world[1] * m.screen[1][i] + m.world[2] * m.screen[2][i] + m.screen[3][i];
    m.z = p[2];
    m.valid = p[2] > m.znear && p[2] < m.zfar && p[2] > 0;
    if (!m.valid) return;
    const float inv = 1.0f / p[2];
    m.x = p[0] * inv * float(m.sw);
    m.y = p[1] * inv * float(m.sh);
    m.size = std::min(320.0f, 0.5f * (float(m.sw) * (inv * (1.5f * (1.0f + (p[2] - 1.0f) / 74.0f))) * m.k[2]));
}

bool hud_project(const float vp[16], float aspect, int width, int height, const float w[3], float* x, float* y, float* depth) {
    float c[4];
    for (int i = 0; i < 4; ++i) c[i] = vp[i] * w[0] + vp[4 + i] * w[1] + vp[8 + i] * w[2] + vp[12 + i];
    if (!(c[3] > 1e-4f)) return false;
    // hud_list's mapping without its half-pixel correction: x_ndc = sx x - fw, y_ndc = fh - sy y
    const float fw = std::min(1.0f, (4.0f / 3.0f) / aspect), fh = std::min(1.0f, aspect / (4.0f / 3.0f));
    const float sx = 2.0f * fw / float(width), sy = 2.0f * fh / float(height);
    *x = (c[0] / c[3] + fw) / sx;
    *y = (fh - c[1] / c[3]) / sy;
    if (depth) *depth = c[3];
    return true;
}

bool hud_project_split(const float vp[16], int view, float aspect, int width, int height, const float w[3], float* x, float* y,
                       float* depth) {
    float c[4];
    for (int i = 0; i < 4; ++i) c[i] = vp[i] * w[0] + vp[4 + i] * w[1] + vp[8 + i] * w[2] + vp[12 + i];
    if (!(c[3] > 1e-4f)) return false;
    // the view's NDC -> the output's: x spans the width, y the view's half (top: 0..1, bottom: -1..0)
    const float xn = c[0] / c[3], yn = 0.5f * c[1] / c[3] + (view == 0 ? 0.5f : -0.5f);
    const float fw = std::min(1.0f, (4.0f / 3.0f) / aspect), fh = std::min(1.0f, aspect / (4.0f / 3.0f));
    const float sx = 2.0f * fw / float(width), sy = 2.0f * fh / float(height);
    *x = (xn + fw) / sx;
    *y = (fh - yn) / sy;
    if (depth) *depth = c[3];
    return true;
}

float hud_marker_size_rule(float z) { return (1.0f + (z - 1.0f) / 74.0f) / z; }

void hud_anchor_markers(HudFrame& hf, const std::vector<HudAnchorRef>& refs) {
    for (HudMarker& m : hf.markers) {
        const HudAnchorRef* best = nullptr;
        float bd = 1e30f;
        for (const HudAnchorRef& r : refs) {
            const float dx = m.world[0] - r.pos[0], dz = m.world[2] - r.pos[2], d = std::sqrt(dx * dx + dz * dz);
            if (m.car != 0 ? r.id == m.car : d < bd) { best = &r; bd = d; if (m.car != 0) break; }
        }
        // a vehicle value (no car): the vehicle whose position is under the point (within 0.5 m horizontally) [L]
        if (!best || (m.car == 0 && bd > 0.5f)) continue;
        m.vehicle = best->id;
        m.match = bd;
        for (int k = 0; k < 3; ++k) m.offset[k] = m.world[k] - best->pos[k];
    }
}

std::shared_ptr<const HudFrame> HudDecoder::decode(const Image& im, std::shared_ptr<const HudFrame> prev,
                                                   const std::vector<HudAnchorRef>* refs) {
    std::uint8_t head[16];
    if (!im.read(kHudBase, head, 16) || rd32(head) != 0x31445548U) return nullptr;
    const std::uint32_t serial = rd32(head + 4), bytes = rd32(head + 8);
    if (prev && prev->serial == serial) return prev;
    std::vector<std::uint8_t> ev(bytes);
    if (bytes && !im.read(kHudBase + 16, ev.data(), bytes)) return prev;
    auto out = std::make_shared<HudFrame>();
    HudFrame& hf = *out;
    hf.serial = serial;
    hf.missed = rd32(head + 12);

    Gs gs;
    float offx = 0, offy = 0;   // _gGsOffsetXYXY (pixels)
    int pass_ox = 0, pass_oy = 0;   // gsoff1: the current pass's origin (Begin's scissor)
    std::deque<std::pair<std::uint32_t, int>> pending;   // SetTexture calls whose TEX0 writes are still ahead
    std::pair<std::uint32_t, int> ident{0, -1};          // the raster the current TEX0 belongs to
    std::pair<std::uint32_t, int> deferred{0, -2};       // a cache texture taking effect after the next batch (-2: none)
    bool tex0_unannounced = false;                       // font1: the current TEX0 came with no SetTexture event
    // perf (2026-11-12): the GS memory (4 MB, zeroed) only when a new texture is decoded (it used to be built for every
    // decoded frame: a 4 MB allocation and fill per tick on the live renderer's frame loop); one per call, as before
    std::unique_ptr<b3asset::GsMemory> gsmem;
    // hud2: the object whose callback wrote the bytes being interpreted; object events since the last batch switch it
    // at their byte offset in the next batch (the callback's first write goes there)
    int cur_object = -1;
    std::vector<std::pair<std::uint32_t, int>> switches;
    std::size_t next_switch = 0;
    // hud4: the same for the marker that is drawing (DrawPositionMarker's draws: its triangle and its text objects);
    // a new 2D pass (Begin) ends it
    int cur_marker = -1;
    std::vector<int> object_video;   // menu1: per hf.objects entry its HudFrame::videos index (-1 none)
    int globe_open = -1;             // fe2: the GLOBE event whose callback has not resumed Gt2d yet
    std::size_t no_merge = ~std::size_t(0);   // fe2: a draw count at which a new draw starts (a globe drew in between)
    std::vector<std::pair<std::uint32_t, int>> marker_switches;
    std::size_t next_marker = 0;
    auto at_byte = [&](std::uint32_t b) {
        while (next_switch < switches.size() && switches[next_switch].first <= b) cur_object = switches[next_switch++].second;
        while (next_marker < marker_switches.size() && marker_switches[next_marker].first <= b) cur_marker = marker_switches[next_marker++].second;
    };

    auto texture = [&](std::uint32_t r, int pal) -> std::uint64_t {
        if (!r) return 0;
        std::uint8_t h[0xC8];
        if (!im.read(r, h, 0xC8)) return 0;
        // the header's stable fields (hud_capture.cpp grab_texture): size / format, upload pointer, TEX0, palette table, name
        std::uint64_t key = fnv(h + 0x0C, 0x1C);   // TEX0 +0x3C left out: the texture cache rewrites its TBP
        key = fnv(h + 0x90, 0x10, key);
        key = fnv(h + 0xA8, 0x20, key);
        key = fnv(reinterpret_cast<const std::uint8_t*>(&r), 4, key);
        key = fnv(reinterpret_cast<const std::uint8_t*>(&pal), 4, key) | 1ULL;
        if (textures.count(key)) return key;
        auto t = std::make_shared<HudTexture>();
        t->raster = r;
        t->palette = pal;
        for (int i = 0; i < 32 && h[0xA8 + i] >= 0x20 && h[0xA8 + i] < 0x7F; ++i) t->name += char(h[0xA8 + i]);
        std::vector<std::uint8_t> f;
        if (raster_file(im, r, f, &t->why)) {
            std::uint32_t add = 0;
            if (pal > 0) add = im.u16(r + 0x90 + 2 * std::uint32_t(pal));
            b3asset::DecodedRaster d;
            if (!gsmem) gsmem = std::make_unique<b3asset::GsMemory>();
            if (b3asset::decode_raster_clut(b3asset::Bytes{f.data(), f.size()}, 0, *gsmem, d, add, &t->why) && !d.levels.empty()) {
                t->ok = true;
                t->w = int(d.hdr.width);
                t->h = int(d.hdr.height);
                t->img_w = d.levels[0].w;
                t->img_h = d.levels[0].h;
                t->rgba = std::move(d.levels[0].rgba);
                if (!d.original.empty() && d.original[0].w == t->w && d.original[0].h == t->h) t->original = std::move(d.original[0].rgba);   // hdkey
                t->tw = 1 << ((d.hdr.tex0 >> 26) & 0xF);
                t->th = 1 << ((d.hdr.tex0 >> 30) & 0xF);
                t->psm = d.hdr.psm();
                t->tfx = d.hdr.tfx();
                t->tcc = d.hdr.tcc();
            }
        }
        textures[key] = t;
        return key;
    };
    auto set_texture_state = [&]() {
        tex0_unannounced = false;
        gs.tex_key = texture(ident.first, ident.second);
        auto it = textures.find(gs.tex_key);
        gs.tex = it != textures.end() && it->second->ok ? it->second.get() : nullptr;
    };

    auto emit = [&](const HudVertex* v, int n) {
        // n = 3: a triangle; the draw's state from the registers now
        HudDraw st;
        const bool tme = (gs.prim >> 4) & 1;
        st.texture = tme ? gs.tex_key : 0;
        if (tme && !gs.tex) ++hf.stats.unknown_texture;
        if (tme && tex0_unannounced && gs.tex_key == 0) ++hf.stats.unbound_texture;
        st.alpha = std::uint8_t(gs.alpha & 0xFF);
        st.fix = std::uint8_t(gs.alpha >> 32);
        st.abe = (gs.prim >> 6) & 1;
        const std::uint32_t fbmsk = std::uint32_t(gs.frame >> 32);
        st.alpha_only = (fbmsk & 0xFF000000U) == 0 && st.alpha == 0x6A;
        st.rgb_masked = (fbmsk & 0x00FFFFFFU) == 0x00FFFFFFU;
        st.linear = (gs.tex1 >> 5) & 1;
        st.tfx_decal = ((gs.tex0 >> 35) & 3) == 1;
        st.tcc = (gs.tex0 >> 34) & 1;
        st.wms = std::uint8_t(gs.clamp & 3);
        st.wmt = std::uint8_t((gs.clamp >> 2) & 3);
        st.fbp = std::uint32_t(gs.frame & 0x1FF);
        st.fbw = std::uint32_t((gs.frame >> 16) & 0x3F);
        st.object = cur_object;
        st.marker = cur_marker;
        HudVertex vv[3];
        if (tme && cur_object >= 0 && std::size_t(cur_object) < object_video.size() && object_video[std::size_t(cur_object)] >= 0) {
            // menu1: a movie tile (SetTileTexture's VRAM upload, no Gt2d texture): u, v over the whole picture from the
            // vertex's place in the object's space, which _Render tiles with the picture's size in tiles [L draft]
            st.video = object_video[std::size_t(cur_object)];
            st.texture = 0;
            const HudVideo& hv = hf.videos[std::size_t(st.video)];
            const HudObject& o = hf.objects[std::size_t(cur_object)];
            const float fu = hv.mb_w > 0 && hv.tiles_w > 0 ? hv.tiles_w * 64.0f / float(16 * hv.mb_w) : 1.0f, fv = hv.mb_h > 0 && hv.tiles_h > 0 ? hv.tiles_h * 64.0f / float(16 * hv.mb_h) : 1.0f;
            for (int i = 0; i < n && i < 3; ++i) {
                vv[i] = v[i];
                vv[i].u = o.size[0] != 0 ? (v[i].x - o.pos[0]) / o.size[0] * fu : 0.0f;
                vv[i].v = o.size[1] != 0 ? (v[i].y - o.pos[1]) / o.size[1] * fv : 0.0f;
            }
            v = vv;
        }
        st.scissor[0] = int(gs.scissor & 0x7FF) - hf.ox;
        st.scissor[2] = int((gs.scissor >> 16) & 0x7FF) - hf.ox;
        st.scissor[1] = int((gs.scissor >> 32) & 0x7FF) - hf.oy;
        st.scissor[3] = int((gs.scissor >> 48) & 0x7FF) - hf.oy;
        st.pass_ox = pass_ox;
        st.pass_oy = pass_oy;
        HudDraw* d = hf.draws.empty() ? nullptr : &hf.draws.back();
        if (!d || hf.draws.size() == no_merge || d->texture != st.texture || d->alpha != st.alpha || d->fix != st.fix || d->abe != st.abe ||
            d->alpha_only != st.alpha_only || d->rgb_masked != st.rgb_masked || d->linear != st.linear ||
            d->tfx_decal != st.tfx_decal || d->tcc != st.tcc || d->wms != st.wms || d->wmt != st.wmt || d->fbp != st.fbp || d->fbw != st.fbw || d->object != st.object || d->marker != st.marker || d->video != st.video || std::memcmp(d->scissor, st.scissor, sizeof st.scissor) != 0 ||
            d->pass_ox != st.pass_ox || d->pass_oy != st.pass_oy) {
            hf.draws.push_back(std::move(st));
            d = &hf.draws.back();
        }
        d->tri.insert(d->tri.end(), v, v + n);
        ++hf.stats.triangles;
    };
    auto kick = [&](std::uint64_t xyz) {
        HudVertex v;
        v.x = float(xyz & 0xFFFF) / 16.0f - offx;
        v.y = float((xyz >> 16) & 0xFFFF) / 16.0f - offy;
        v.z = std::uint32_t(xyz >> 32);
        for (int k = 0; k < 4; ++k) v.rgba[k] = std::uint8_t(gs.rgbaq >> (8 * k));
        float s, t;
        if ((gs.prim >> 8) & 1) {   // FST: UV in texels (12.4)
            const float tw = gs.tex ? float(gs.tex->tw) : 1.0f, th = gs.tex ? float(gs.tex->th) : 1.0f;
            s = float(gs.uv & 0x3FFF) / 16.0f / tw;
            t = float((gs.uv >> 16) & 0x3FFF) / 16.0f / th;
        } else {
            const float q = f32_of(std::uint32_t(gs.rgbaq >> 32));
            s = f32_of(std::uint32_t(gs.st));
            t = f32_of(std::uint32_t(gs.st >> 32));
            if (q != 0 && std::isfinite(q)) { s /= q; t /= q; }
        }
        if (gs.tex) { s *= float(gs.tex->tw) / float(gs.tex->w); t *= float(gs.tex->th) / float(gs.tex->h); }
        v.u = s;
        v.v = t;
        const int type = int(gs.prim & 7);
        const bool flat = !((gs.prim >> 3) & 1);
        auto flat_colour = [&](HudVertex* tri, int n) {
            if (!flat) return;
            for (int i = 0; i < n - 1; ++i) std::memcpy(tri[i].rgba, tri[n - 1].rgba, 4);   // the last vertex's colour
        };
        switch (type) {
        case 6: {   // sprite: two corners
            gs.q[gs.nq++] = v;
            if (gs.nq < 2) return;
            const HudVertex a = gs.q[0], b = gs.q[1];
            gs.nq = 0;
            HudVertex c[4] = {a, b, a, b};   // a (x0,y0), b (x1,y1); colour of b
            c[2].x = b.x; c[2].u = b.u;      // (x1, y0)
            c[3].x = a.x; c[3].u = a.u;      // (x0, y1)
            c[2].y = a.y; c[2].v = a.v;
            c[3].y = b.y; c[3].v = b.v;
            for (auto& x : c) std::memcpy(x.rgba, b.rgba, 4);
            const HudVertex t1[3] = {c[0], c[2], c[1]}, t2[3] = {c[0], c[1], c[3]};
            emit(t1, 3);
            emit(t2, 3);
            ++hf.stats.sprites;
            ++hf.stats.prims;
            return;
        }
        case 3: {   // triangle list
            gs.q[gs.nq++] = v;
            if (gs.nq < 3) return;
            gs.nq = 0;
            HudVertex t[3] = {gs.q[0], gs.q[1], gs.q[2]};
            flat_colour(t, 3);
            emit(t, 3);
            ++hf.stats.prims;
            return;
        }
        case 4: case 5: {   // strip / fan
            if (gs.nq < 2) { gs.q[gs.nq++] = v; return; }
            HudVertex t[3] = {gs.q[0], gs.q[1], v};
            flat_colour(t, 3);
            emit(t, 3);
            if (type == 4) { gs.q[0] = gs.q[1]; gs.q[1] = v; } else gs.q[1] = v;
            ++hf.stats.prims;
            ++hf.stats.strips;
            return;
        }
        default:    // points and lines: not drawn (Gt2d writes none [C its PRIM table])
            ++hf.stats.lines;
            return;
        }
    };
    auto write_reg = [&](std::uint32_t reg, std::uint64_t v) {
        switch (reg) {
        case 0x00: gs.prim = v; gs.nq = 0; break;
        case 0x01: gs.rgbaq = v; break;
        case 0x02: gs.st = v; break;
        case 0x03: gs.uv = v; break;
        case 0x04: case 0x05: case 0x0C: case 0x0D: kick(v); break;   // XYZF2 / XYZ2 (and the no-kick forms, treated alike)
        case 0x06: {
            gs.tex0 = v;
            const bool announced = !pending.empty();
            if (announced) { ident = pending.front(); pending.pop_front(); }
            else ident = {0, -1};   // End / Suspend restoring RenderWare's texture: no draws follow
            set_texture_state();
            tex0_unannounced = !announced;
            break;
        }
        case 0x08: gs.clamp = v; break;
        case 0x14: gs.tex1 = v; break;
        case 0x40: gs.scissor = v; break;
        case 0x42: gs.alpha = v; break;
        case 0x47: gs.test = v; break;
        case 0x4C: gs.frame = v; break;
        default: break;
        }
    };
    auto run_batch = [&](const std::uint8_t* p, std::uint32_t qwc) {
        ++hf.stats.batches;
        std::uint32_t q = 0;
        while (q < qwc) {
            at_byte(16 * q);
            const std::uint64_t tag = rd64(p + 16 * q), regs = rd64(p + 16 * q + 8);
            ++q;
            const std::uint32_t nloop = tag & 0x7FFF, flg = (tag >> 58) & 3;
            std::uint32_t nreg = (tag >> 60) & 0xF;
            if (nreg == 0) nreg = 16;
            if ((tag >> 46) & 1) write_reg(0x00, (tag >> 47) & 0x7FF);   // PRE: PRIM from the tag
            if (flg == 0) {        // PACKED
                for (std::uint32_t l = 0; l < nloop && q < qwc; ++l)
                    for (std::uint32_t r = 0; r < nreg && q < qwc; ++r, ++q) {
                        at_byte(16 * q);
                        const std::uint32_t reg = (regs >> (4 * r)) & 0xF;
                        const std::uint8_t* d = p + 16 * q;
                        const std::uint64_t lo = rd64(d), hi = rd64(d + 8);
                        if (reg == 0xE) write_reg(std::uint32_t(hi & 0xFF), lo);
                        else if (reg == 0x1) write_reg(0x01, (lo & 0xFF) | ((lo >> 24) & 0xFF00) | ((hi & 0xFF) << 16) | (((hi >> 32) & 0xFF) << 24) | (gs.rgbaq & 0xFFFFFFFF00000000ULL));
                        else if (reg == 0x2) { write_reg(0x02, lo); gs.rgbaq = (gs.rgbaq & 0xFFFFFFFFULL) | (hi << 32); }
                        else if (reg == 0x3) write_reg(0x03, (lo & 0x3FFF) | (((lo >> 32) & 0x3FFF) << 16));
                        else if (reg == 0x4 || reg == 0x5) write_reg(0x05, (lo & 0xFFFF) | (((lo >> 32) & 0xFFFF) << 16) | (((hi >> 4) & 0xFFFFFFFULL) << 32));
                        else if (reg != 0xF) write_reg(reg, lo);
                    }
            } else if (flg == 1) { // REGLIST: 64-bit registers, two per quadword
                const std::uint64_t total = std::uint64_t(nloop) * nreg;
                for (std::uint64_t k = 0; k < total; ++k) {
                    const std::uint32_t qi = q + std::uint32_t(k / 2);
                    if (qi >= qwc) break;
                    const std::uint32_t reg = (regs >> (4 * (k % nreg))) & 0xF;
                    at_byte(16 * qi + 8 * std::uint32_t(k % 2));
                    if (reg != 0xE && reg != 0xF) write_reg(reg, rd64(p + 16 * qi + 8 * (k % 2)));
                }
                q += std::uint32_t((total + 1) / 2);
            } else {               // IMAGE / disabled: skip the data
                q += nloop;
            }
        }
    };

    std::vector<std::uint8_t> slot;
    std::size_t at = 0;
    while (at + 8 <= ev.size()) {
        const std::uint32_t kind = rd32(ev.data() + at), n = rd32(ev.data() + at + 4);
        const std::uint8_t* p = ev.data() + at + 8;
        at += 8 + ((n + 3) & ~3U);
        if (at > ev.size()) break;
        switch (kind) {
        case kEvBegin:
            ++hf.stats.passes;
            cur_marker = -1;
            break;
        case kEvState: {
            if (n < 0xA0) break;
            gs.alpha = rd64(p + 0x00);
            gs.tex1 = rd64(p + 0x10);
            gs.clamp = rd64(p + 0x20);
            gs.scissor = rd64(p + 0x30);
            gs.frame = rd64(p + 0x40);
            offx = f32_of(rd32(p + 0x50));
            offy = f32_of(rd32(p + 0x54));
            pass_ox = int(gs.scissor & 0x7FF);
            pass_oy = int((gs.scissor >> 32) & 0x7FF);
            const std::uint8_t* g = p + 0x60;   // _gpu64Current 0x4E28D8 ..
            ident = {rd32(g + 0x18), -1};       // _gpCurrentTexture = skyTextureRaster after Begin
            pending.clear();
            deferred = {0, -2};
            gs.prim = 0;
            gs.nq = 0;
            set_texture_state();
            if (hf.width == 0) {
                hf.ox = int(gs.scissor & 0x7FF);
                hf.oy = int((gs.scissor >> 32) & 0x7FF);
                hf.width = int((gs.scissor >> 16) & 0x7FF) - hf.ox + 1;
                hf.height = int((gs.scissor >> 48) & 0x7FF) - hf.oy + 1;
                hf.frame_reg = gs.frame;
            }
            break;
        }
        case kEvTexture: {
            // +0x8C as the call saw it (it changes at run time: the image's copy of the header can be stale), and
            // whether SetTexture flushes first; 8-byte events (early hud1 captures) only queue
            std::uint32_t c8c = 1, flush = 0;
            if (n >= 16) { c8c = rd32(p + 8); flush = rd32(p + 12); }
            if (c8c == 0 && int(rd32(p + 4)) <= 0) {
                // a texture-cache raster without a palette: RenderWare sets TEX0 outside the Gt2d buffer, right after
                // SetTexture's flush of what is pending (the next batch when a flush follows) [C asm 0x219B30..0x219B74]
                deferred = {rd32(p), int(rd32(p + 4))};
                if (flush == 0) { ident = deferred; deferred = {0, -2}; set_texture_state(); }
            } else if (n >= 8) {
                pending.emplace_back(rd32(p), int(rd32(p + 4)));
            }
            break;
        }
        case kEvResume:
            // fe2: the globe callback's Resume: its draws go after what its Suspend flushed
            if (globe_open >= 0) { hf.globes[std::size_t(globe_open)].draw_index = no_merge = hf.draws.size(); globe_open = -1; }
            if (n >= 4) ident = {rd32(p), -1};
            pending.clear();
            set_texture_state();
            break;
        case kEvBatchRef:   // the batch in its slot ({qwc, address}; hud_capture.cpp kSlotBase)
        case kEvBatch:
            if (kind == kEvBatchRef && n >= 8) {
                const std::uint32_t qwc = std::min<std::uint32_t>(rd32(p), 512);
                slot.resize(16 * std::size_t(qwc));
                if (im.read(rd32(p + 4), slot.data(), 16 * qwc)) run_batch(slot.data(), qwc);
            } else if (kind == kEvBatch && n >= 4) {
                run_batch(p + 4, std::min<std::uint32_t>(rd32(p), (n - 4) / 16));
            }
            at_byte(~0U);   // object events past the batch's end (nothing written yet): current from here
            switches.clear();
            next_switch = 0;
            marker_switches.clear();
            next_marker = 0;
            if (deferred.second != -2) { ident = deferred; deferred = {0, -2}; set_texture_state(); }
            break;
        case kEvScene: {
            // {scene, manager, hud index, n, rect[4], root frame, align table [18]}, n x {component, vtable, frame, flags, align, player}
            // (hud_capture.cpp observe_scene, at CB3HUDManager::Render)
            if (n < 4 * 27) break;
            ++hf.stats.scenes;
            const std::uint32_t hud = rd32(p + 8), nc = rd32(p + 12), root = rd32(p + 32);
            if (hud == ~0U) break;
            ++hf.stats.hud_scenes;
            for (int k = 0; k < 4; ++k) hf.hud_rect[k] = f32_of(rd32(p + 16 + 4 * k));
            float table[18];
            for (int k = 0; k < 18; ++k) hf.align_table[k] = table[k] = f32_of(rd32(p + 36 + 4 * k));
            for (std::uint32_t c = 0; c < nc && 108 + 24 * (c + 1) <= n; ++c) {
                const std::uint8_t* e = p + 108 + 24 * c;
                HudComponent hc;
                hc.addr = rd32(e);
                hc.vtable = rd32(e + 4);
                hc.frame = rd32(e + 8);
                hc.flags = rd32(e + 12);
                hc.align = int(rd32(e + 16));
                hc.player = int(rd32(e + 20));
                hc.hud = int(hud);
                hc.root = root;
                if (hc.align >= 0 && hc.align < 8) { hc.fx = table[2 * hc.align]; hc.fy = table[2 * hc.align + 1]; }
                hf.components.push_back(hc);
            }
            break;
        }
        case kEvObject: {
            // {pc, object, byte offset (~0: none), space pos x, y, size x, y, frames[8]}
            if (n < 64) break;
            hf.has_objects = true;
            ++hf.stats.objects;
            HudObject o;
            o.pc = rd32(p);
            o.addr = rd32(p + 4);
            for (int k = 0; k < 2; ++k) { o.pos[k] = f32_of(rd32(p + 12 + 4 * k)); o.size[k] = f32_of(rd32(p + 20 + 4 * k)); }
            for (int k = 0; k < 8; ++k) o.frames[k] = rd32(p + 28 + 4 * k);
            for (int k = 0; k < 8 && o.component < 0 && o.frames[k]; ++k)
                for (std::size_t c = 0; c < hf.components.size(); ++c)
                    if (hf.components[c].frame == o.frames[k]) { o.component = int(c); break; }
            // fe3: in HUD mode 3 (the crash / takedown camera's letterbox) CB3HUDBoostBar::UpdateTransition 0x1A3F40 hangs
            // the bar's object (+0x560) on the bar's own second frame (+0x69C; object +0x38 = it), and back on the
            // component's frame (+0x8) after [C matched]. Prepare 0x1A52D0 builds that frame under the HUD root at a copy of
            // the component frame's place (+0x6A0 = *+0x8) [C matched]: it is the bar's, with the bar's alignment. Without
            // this the bar lost its component in those frames and kept its 4:3 place in the wide layout (home1 40.webp,
            // roadrage1 8140..8270)
            if (o.component < 0 && o.pc == kBoostBarRender) {
                std::uint32_t top = 0;
                for (int k = 0; k < 8 && o.frames[k]; ++k) top = o.frames[k];
                for (std::size_t c = 0; c < hf.components.size(); ++c) {
                    const HudComponent& hc = hf.components[c];
                    if ((hc.addr == kBoostBarP1 || hc.addr == kBoostBarP2) && hc.root != 0 && hc.root == top) { o.component = int(c); break; }
                }
            }
            // the same object again (a callback calling another hooked one): one entry
            int idx = int(hf.objects.size());
            if (!hf.objects.empty() && hf.objects.back().addr == o.addr) idx = idx - 1;
            else {
                hf.objects.push_back(o);
                object_video.push_back(-1);
                if (o.pc == kVideoRender) {   // menu1: a movie object (hud2+ captures); its VIDEO event (menu1 captures) fills it
                    HudVideo hv;
                    hv.object = o.addr;
                    object_video.back() = int(hf.videos.size());
                    hf.videos.push_back(hv);
                }
            }
            const std::uint32_t off = rd32(p + 8);
            if (off == ~0U) { ++hf.stats.objects_no_offset; switches.emplace_back(0, idx); }
            else switches.emplace_back(off, idx);
            break;
        }
        case kEvMarker: {
            // {pc, byte offset (~0: none), slot, car, player, points, world xyz, viewport, viewport +0x00 x 16, +0x48 x 4,
            // +0x78, +0x7C, +0x84, +0x88} (hud_capture.cpp, at DrawPositionMarker 0x1531C0)
            if (n < 4 * 34) break;
            HudMarker mk;
            mk.pass_ox = pass_ox;   // gsoff1: the pass it is drawn in (split screen: its view)
            mk.pass_oy = pass_oy;
            mk.slot = rd32(p + 8);
            mk.car = rd32(p + 12);
            mk.player = rd32(p + 16);
            mk.points = rd32(p + 20) != 0;
            for (int k = 0; k < 3; ++k) mk.world[k] = f32_of(rd32(p + 24 + 4 * k));
            for (int k = 0; k < 16; ++k) mk.screen[k / 4][k % 4] = f32_of(rd32(p + 40 + 4 * k));
            for (int k = 0; k < 4; ++k) mk.k[k] = f32_of(rd32(p + 104 + 4 * k));
            mk.sw = int(rd32(p + 120));
            mk.sh = int(rd32(p + 124));
            mk.znear = f32_of(rd32(p + 128));
            mk.zfar = f32_of(rd32(p + 132));
            hud_marker_project(mk);
            const int idx = int(hf.markers.size());
            hf.markers.push_back(mk);
            const std::uint32_t off = rd32(p + 4);
            marker_switches.emplace_back(off == ~0U ? 0U : off, idx);
            break;
        }
        case kEvFeCar: {   // menu1: {index, car, viewport 1's RwCamera, 0}
            if (n < 16) break;
            HudFeCar c;
            c.index = rd32(p);
            c.car = rd32(p + 4);
            c.rwcamera = rd32(p + 8);
            hf.fe_cars.push_back(c);
            break;
        }
        case kEvTicker: {   // menu-font: {object, text address, data +0x04, +0x08, +0x0C, recip x, y, font raster, n, n codes}
            if (n < 4 * 9 || hf.objects.empty() || hf.objects.back().addr != rd32(p)) break;
            HudObject& o = hf.objects.back();
            o.ticker_scroll = f32_of(rd32(p + 8));
            o.ticker_extent = f32_of(rd32(p + 12));
            o.ticker_gap = f32_of(rd32(p + 16));
            o.ticker_recip[0] = f32_of(rd32(p + 20));
            o.ticker_recip[1] = f32_of(rd32(p + 24));
            o.ticker_font = rd32(p + 28);
            const std::uint32_t nc = std::min<std::uint32_t>(rd32(p + 32), (n - 36) / 2);
            o.ticker_text.clear();
            for (std::uint32_t k = 0; k < nc; ++k) o.ticker_text.push_back(char16_t(rd32(p + 36 + 4 * (k / 2)) >> (16 * (k & 1)) & 0xFFFFU));
            break;
        }
        case kEvGlobe: {   // menu1: hud_capture.cpp observe_globe
            if (n < 4 * 46) break;
            HudGlobe g;
            g.object = rd32(p);
            for (int k = 0; k < 3; ++k) g.pos[k] = f32_of(rd32(p + 4 + 4 * k));
            const std::uint32_t w3 = rd32(p + 16), w4 = rd32(p + 20), w5 = rd32(p + 24);
            g.lat = std::uint16_t(w3); g.lon = std::uint16_t(w3 >> 16);
            g.shape[0] = std::uint16_t(w4); g.shape[1] = std::uint16_t(w4 >> 16);
            g.alpha = std::uint8_t(w5); g.reveal = std::uint8_t(w5 >> 8); g.highlight = std::uint8_t(w5 >> 16); g.region = std::int8_t(w5 >> 24);
            for (int k = 0; k < 8; ++k) g.space[k] = f32_of(rd32(p + 28 + 4 * k));
            for (int k = 0; k < 4; ++k) g.sphere_pos[k] = f32_of(rd32(p + 60 + 4 * k));
            for (int k = 0; k < 5; ++k) g.texture[k] = rd32(p + 76 + 4 * k);
            for (int k = 0; k < 16; ++k) g.view[k / 4][k % 4] = f32_of(rd32(p + 96 + 4 * k));
            for (int k = 0; k < 4; ++k) g.k[k] = f32_of(rd32(p + 160 + 4 * k));
            g.znear = f32_of(rd32(p + 176));
            g.zfar = f32_of(rd32(p + 180));
            for (int k = 0; k < 5; ++k) g.tex_key[k] = texture(g.texture[k], -1);   // fe2: the rasters as the HUD's
            g.draw_index = hf.draws.size();
            globe_open = int(hf.globes.size());
            hf.globes.push_back(g);
            break;
        }
        case kEvVideo: {
            // menu1: {object, decoder, slot index, slot +0x00 .. +0x14, decoder +0x30, +0x38 .. +0x48, 0, host slot (~0:
            // not grabbed), bytes, data +0x00, +0x04, +0x14} (hud_capture.cpp observe_video, after the object's OBJECT event)
            if (n < 4 * 21) break;
            HudVideo hv;
            hv.object = rd32(p);
            hv.decoder = rd32(p + 4);
            hv.slot = rd32(p + 8);
            hv.pic = rd32(p + 12);
            hv.tiles_x = int(rd32(p + 16));
            hv.tiles_y = int(rd32(p + 20));
            hv.mb_w = int(rd32(p + 24));
            hv.mb_h = int(rd32(p + 28));
            hv.decoded = rd32(p + 40);
            const std::uint32_t host = rd32(p + 64), bytes = rd32(p + 68);
            hv.tiles_w = f32_of(rd32(p + 72));
            hv.tiles_h = f32_of(rd32(p + 76));
            hv.backing = (rd32(p + 80) & 0xFF) != 0;
            const bool sane = hv.mb_w > 0 && hv.mb_h > 0 && hv.mb_w <= 64 && hv.mb_h <= 64 && bytes == std::uint32_t(hv.mb_w * hv.mb_h) * 1024U;
            if (sane && host != ~0U && host < 4) {
                std::vector<std::uint8_t>& scratch = video_scratch_;
                scratch.resize(bytes);
                if (im.read(kMovieBase + kMovieSlot * host, scratch.data(), bytes)) {
                    auto& last = pictures[hv.pic];
                    std::vector<std::uint8_t>& built = picture_raw_[hv.pic];   // the bytes `last` was made from
                    const bool same = last && built.size() == scratch.size() && std::memcmp(built.data(), scratch.data(), scratch.size()) == 0;
                    if (!same || last->w != 16 * hv.mb_w || last->h != 16 * hv.mb_h) {
                        built.swap(scratch);
                        const std::vector<std::uint8_t>& raw = built;
                        auto pic = std::make_shared<HudPicture>();
                        pic->src = hv.pic;
                        pic->w = 16 * hv.mb_w;
                        pic->h = 16 * hv.mb_h;
                        pic->rgba.resize(std::size_t(pic->w) * pic->h * 4);
                        // strip c (16 px wide, the full height) at c x rows x 1024, 64 bytes a row [L SetTileTexture]
                        for (int c = 0; c < hv.mb_w; ++c)
                            for (int r = 0; r < pic->h; ++r) {
                                const std::uint8_t* src = raw.data() + std::size_t(c) * hv.mb_h * 1024 + std::size_t(r) * 64;
                                std::uint8_t* dst = pic->rgba.data() + (std::size_t(r) * pic->w + std::size_t(c) * 16) * 4;
                                std::memcpy(dst, src, 64);   // 16 pixels; then alpha 0x80 -> 0xFF
                                for (int x = 0; x < 16; ++x) dst[4 * x + 3] = std::uint8_t(std::min(255, 2 * int(src[4 * x + 3])));
                            }
                        last = pic;
                    }
                }
            }
            if (const auto it = pictures.find(hv.pic); it != pictures.end() && it->second && it->second->w == 16 * hv.mb_w && it->second->h == 16 * hv.mb_h)
                hv.picture = it->second;
            int vi = -1;
            for (std::size_t o = hf.objects.size(); o-- > 0;)
                if (hf.objects[o].addr == hv.object) { vi = object_video[o]; if (vi < 0) object_video[o] = vi = int(hf.videos.size()); break; }
            if (vi >= 0 && std::size_t(vi) < hf.videos.size()) hf.videos[std::size_t(vi)] = std::move(hv);
            else hf.videos.push_back(std::move(hv));
            break;
        }
        default:
            break;
        }
    }
    // gsoff1 (§29): a 2-player split screen (Is2PlayerSplitScreen: *0x51BA88 == 0x51AEC0). Each view's passes begin with
    // its viewport's scissor (player 1 rows 0..223, player 2 224..447), and its rival markers (DrawPositionMarker, through
    // the viewport's matrix) are in the viewport's own space, while the HUD components (both players') and award messages
    // are in the frame's [C gsoff1mp2 poll 2400: player 2's speedometer at y 409..427, its markers at y 86..107 in a pass
    // from row 224; GS frame of the same poll]. The 2D becomes one frame-sized space: markers move by their pass's
    // origin, scissors are frame rows, and the space is the main buffer's 640 x 448 (the first pass may be a small
    // effects target's).
    if (im.u32(0x51BA88) == 0x51AEC0U && !hf.draws.empty()) {
        hf.split = true;
        int w = 0, h = 0;
        std::uint32_t fbw = 0;
        for (const HudDraw& d : hf.draws) fbw = std::max(fbw, d.fbw);
        for (HudMarker& mk : hf.markers) {   // every marker (drawn or not) to frame space, with its view
            mk.view = mk.pass_oy > 0 ? 1 : 0;
            if (mk.valid) { mk.x += float(mk.pass_ox); mk.y += float(mk.pass_oy); }
        }
        for (HudDraw& d : hf.draws) {
            d.scissor[0] += hf.ox; d.scissor[2] += hf.ox;
            d.scissor[1] += hf.oy; d.scissor[3] += hf.oy;
            if (d.marker >= 0) for (HudVertex& v : d.tri) { v.x += float(d.pass_ox); v.y += float(d.pass_oy); }
            if (d.fbw == fbw) { w = std::max(w, d.scissor[2] + 1); h = std::max(h, d.scissor[3] + 1); }
        }
        hf.ox = hf.oy = 0;
        if (w > 0 && h > 0) { hf.width = w; hf.height = h; }
    }
    if (refs) hud_anchor_markers(hf, *refs);
    return out;
}

// ---- wide1: the front end's wide layout --------------------------------------------------------------------------------

HudFrontendRule hud_frontend_rule(const HudDraw& d, float right) {
    HudFrontendRule r;
    if (d.video >= 0 || d.tri.empty()) return r;   // a movie never stretches
    for (std::size_t t0 = 0; t0 + 2 < d.tri.size() && !r.span; t0 += 3) {
        const float lo = std::min({d.tri[t0].x, d.tri[t0 + 1].x, d.tri[t0 + 2].x});
        const float hi = std::max({d.tri[t0].x, d.tri[t0 + 1].x, d.tri[t0 + 2].x});
        r.span = lo <= 0.5f && hi >= right - 0.5f;
    }
    if (r.span || d.texture != 0) return r;
    float dlo = 1e30f, dhi = -1e30f;
    for (const HudVertex& v : d.tri) { dlo = std::min(dlo, v.x); dhi = std::max(dhi, v.x); }
    const bool l = std::fabs(dlo) < 0.5f, rr = std::fabs(dhi - right) < 0.5f;
    // reaching an edge from inside: not a line lying past it (the Crash Nav crosshair's vertical line at 639.9..644.9)
    r.fill = (l && dhi < right - 0.5f) || (rr && dlo > 0.5f) || (l && rr);
    return r;
}

void hud_frontend_vertex(const HudDraw& d, const HudFrontendRule& r, std::size_t vi, float right, float ext, bool repeat,
                         float& x, float& u, float& v) {
    const HudVertex& hv = d.tri[vi];
    x = hv.x; u = hv.u; v = hv.v;
    float dx = 0;
    if (r.span) {
        // its vertices on or past an edge go out by ext, the others stay (the backdrop `bg` is three full-width strips
        // fading into the movie's window: the window keeps its 4:3 place); clamped textures stretch over the added width
        dx = hv.x <= 0.5f ? -ext : hv.x >= right - 0.5f ? ext : 0.0f;
        const std::size_t t0 = vi - vi % 3;
        if (dx != 0 && repeat && t0 + 2 < d.tri.size()) {   // the grid (0..656): u / v follow the triangle's gradient
            const HudVertex &a = d.tri[t0], &b = d.tri[t0 + 1], &c = d.tri[t0 + 2];
            const float det = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (std::fabs(det) > 1e-6f) {
                u += ((b.u - a.u) * (c.y - a.y) - (c.u - a.u) * (b.y - a.y)) / det * dx;
                v += ((b.v - a.v) * (c.y - a.y) - (c.v - a.v) * (b.y - a.y)) / det * dx;
            }
        }
    } else if (r.fill) {   // its vertices on an edge go to the output's edge; the others stay on the piece they meet
        if (r.stretch_dx != 0.0f && hv.x < r.stretch_hi - 0.5f) dx = r.stretch_at(hv.x);   // mac5: on the moved curve
        else if (r.end_dx != 0.0f && std::fabs(hv.x - r.end_x) < 0.5f) dx = r.end_dx;   // fe3: meeting a moved cap
        else if (std::fabs(hv.x) < 0.5f) dx = -ext;
        else if (std::fabs(hv.x - right) < 0.5f) dx = ext;
    } else if (r.move != 0.0f) {   // fe3: a cap, whole
        dx = r.move;
    } else if (r.stretch_dx != 0.0f && r.stretch_hi > r.stretch_lo) {   // mac5: the curve's linear shift
        dx = r.stretch_at(hv.x);
    }
    x = hv.x + dx;
}

// ---- fe3: the front end's panel layout --------------------------------------------------------------------------------

namespace {
// object render callbacks [C names, decomp config/symbol_addrs_b3names.txt]
constexpr std::uint32_t kBoxRender = 0x001D5EA0U;            // CGt2dObjectBox::_Render
constexpr std::uint32_t kScrollingTextRender = 0x0031C990U;  // CB3ScrollingText2dObject::_Render
constexpr std::uint32_t kSelectionBarRender = 0x003720F0U;   // CB3SelectionBar2dObject::_Render
}

bool hud_overlay_panels(const HudFrame& f) {
    constexpr std::uint32_t kHeadingBarRender = 0x0031B130U, kPromptBarRender = 0x003D7B40U;   // [C names, decomp tables]
    bool panel = false;
    for (const HudDraw& d : f.draws) {
        if (d.object < 0 || std::size_t(d.object) >= f.objects.size()) continue;
        const HudObject& o = f.objects[std::size_t(d.object)];
        if (o.component >= 0) return false;
        panel = panel || o.pc == kHeadingBarRender || o.pc == kPromptBarRender;
    }
    return panel;
}

std::vector<float> hud_overlay_shifts(const HudFrame& f, float right, float ext, std::vector<bool>* vertexwise) {
    constexpr std::uint32_t kHeadingBarRender = 0x0031B130U, kPromptBarRender = 0x003D7B40U;
    constexpr std::uint32_t kRoundedPanelRender = 0x003A2850U;   // CB3RoundedPanel2dObject::_Render [C name]: a dialog
    const std::size_t n = f.draws.size();
    std::vector<float> out(n, 0.0f);
    auto box = [&](const HudDraw& d, float b[4]) {
        b[0] = b[1] = 1e30f; b[2] = b[3] = -1e30f;
        for (const HudVertex& v : d.tri) { b[0] = std::min(b[0], v.x); b[1] = std::min(b[1], v.y); b[2] = std::max(b[2], v.x); b[3] = std::max(b[3], v.y); }
    };
    auto pc = [&](const HudDraw& d) { return d.object >= 0 && std::size_t(d.object) < f.objects.size() ? f.objects[std::size_t(d.object)].pc : 0U; };
    float band[2] = {1e30f, -1e30f}, prompt_top = 1e30f;
    for (const HudDraw& d : f.draws) {
        if (d.tri.empty()) continue;
        float b[4];
        box(d, b);
        if (pc(d) == kHeadingBarRender && d.texture == 0) { band[0] = std::min(band[0], b[1]); band[1] = std::max(band[1], b[3]); }
        if (pc(d) == kPromptBarRender && d.texture == 0) prompt_top = std::min(prompt_top, b[1]);
    }
    enum { kNone, kBand, kPrompt, kBody };
    std::vector<int> group(n, kNone);
    float u[2][2] = {{1e30f, -1e30f}, {1e30f, -1e30f}};   // band / prompt row x extents
    for (std::size_t i = 0; i < n; ++i) {
        const HudDraw& d = f.draws[i];
        if (d.tri.empty() || pc(d) == kHeadingBarRender || pc(d) == kPromptBarRender) continue;
        float b[4];
        box(d, b);
        // at an edge: hud2's (the selection bar, bands): a vertex ON the 4:3 box's edge. (mac5, a report: RESUME, RESTART,
        // CONTROLLERS and SETTINGS still jumped right to left as they were selected; AUDIO and QUIT did not: a longer
        // label zooming in about its centre overshoots past x 0 for a few frames, and "left of 0.5" took it for an edge
        // piece, so it kept its 4:3 place until the zoom settled.)
        bool on_edge = false;
        for (const HudVertex& v : d.tri) on_edge = on_edge || std::fabs(v.x) < 0.5f || std::fabs(v.x - right) < 0.5f;
        if (on_edge) continue;
        const int g = b[1] >= prompt_top - 2.0f ? kPrompt : (b[1] >= band[0] - 2.0f && b[3] <= band[1] + 2.0f) ? kBand : kBody;
        group[i] = g;
        if (g != kBody) { float* e = u[g == kBand ? 0 : 1]; e[0] = std::min(e[0], b[0]); e[1] = std::max(e[1], b[2]); }
    }
    // the menu column: body draws entirely left of x 280, and (mac5, a report: the highlighted pause row sat apart and snapped
    // back when the selection moved) any body draw starting at one of their left edges -- the highlighted row is drawn
    // larger and runs past 280 (AUDIO 85..286 beside the others' 85..236) -- unless a dialog drawn after it covers it (the
    // quit question over the pause menu: the PS2 shows the column dimmed under the dialog, so it stays there)
    // (mac5, user again: each row "appears in the wrong place for a second and then snaps" as the selection reaches it):
    // the newly selected row zooms in about its centre (pause: 68..219, 14..273, then 85..204), so mid-zoom a long label
    // runs past 280 with its left edge away from the column's; a draw whose centre lies within the column's x extent
    // belongs to it too
    std::vector<float> lefts;
    float col_lo = 1e30f, col_hi = -1e30f;
    for (std::size_t i = 0; i < n; ++i) {
        if (group[i] != kBody) continue;
        float b[4];
        box(f.draws[i], b);
        if (b[2] <= 280.0f) { lefts.push_back(b[0]); col_lo = std::min(col_lo, b[0]); col_hi = std::max(col_hi, b[2]); }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (group[i] != kBody) continue;
        float b[4];
        box(f.draws[i], b);
        const float mid = 0.5f * (b[0] + b[2]);
        bool column = b[2] <= 280.0f || (mid >= col_lo && mid <= col_hi && mid < 280.0f);
        for (float l : lefts) column = column || (b[0] < 200.0f && std::fabs(b[0] - l) < 2.0f);
        if (!column) continue;
        bool covered = false;
        for (std::size_t j = i + 1; j < n && !covered; ++j) {
            if (pc(f.draws[j]) != kRoundedPanelRender || f.draws[j].tri.empty()) continue;
            float c[4];
            box(f.draws[j], c);
            covered = c[0] < b[2] && c[2] > b[0] && c[1] < b[3] && c[3] > b[1];
        }
        if (!covered) out[i] = -ext;
    }
    // the selection bar of a moved row (its brackets beside the item; its lines run to the 4:3 edges)
    if (vertexwise) vertexwise->assign(n, false);
    for (std::size_t i = 0; i < n && vertexwise; ++i) {
        const HudDraw& d = f.draws[i];
        if (pc(d) != kSelectionBarRender || d.tri.empty()) continue;
        float b[4];
        box(d, b);
        for (std::size_t j = 0; j < n; ++j) {
            if (group[j] != kBody || out[j] == 0.0f) continue;
            float c[4];
            box(f.draws[j], c);
            if (c[1] < b[3] && c[3] > b[1]) { out[i] = out[j]; (*vertexwise)[i] = true; break; }
        }
    }
    for (int g = 0; g < 2; ++g) {
        if (u[g][0] > u[g][1]) continue;
        const float c = 0.5f * (u[g][0] + u[g][1]) - 0.5f * right;
        const float s = c > 20.0f ? ext : c < -20.0f ? -ext : 0.0f;
        for (std::size_t i = 0; i < n; ++i) if (group[i] == (g == 0 ? kBand : kPrompt)) out[i] = s;
    }
    return out;
}

HudFrontendPlan hud_frontend_plan(const HudFrame& f, float right, float ext, const std::function<float(const HudDraw&)>& roughness,
                                  const std::function<bool(const HudDraw&)>& tv_static) {
    HudFrontendPlan plan;
    const std::size_t n = f.draws.size();
    plan.rule.resize(n);
    if (!(ext > 0.0f)) return plan;
    auto pc_of = [&](const HudDraw& d) { return d.object >= 0 && std::size_t(d.object) < f.objects.size() ? f.objects[std::size_t(d.object)].pc : 0U; };
    auto extent = [](const HudDraw& d, float& lo, float& hi) {
        lo = 1e30f; hi = -1e30f;
        for (const HudVertex& v : d.tri) { lo = std::min(lo, v.x); hi = std::max(hi, v.x); }
    };
    std::vector<int> cap_side(n, 0);   // a textured one-sided edge piece: -1 left / +1 right
    std::vector<char> picture(n, 0);   // a span that keeps 4:3 because what it samples is not smooth (a backdrop candidate)
    for (std::size_t i = 0; i < n; ++i) {
        const HudDraw& d = f.draws[i];
        HudFrontendRule& r = plan.rule[i];
        if (d.video >= 0 || d.tri.empty()) continue;   // movies keep 4:3
        const std::uint32_t pc = pc_of(d);
        if (d.texture == 0) {
            if (pc == kBoxRender) continue;   // content boxes
            r = hud_frontend_rule(d, right);    // wide1: spans and fills
            continue;
        }
        if (pc == kScrollingTextRender && hud_in_clip_region(d, right)) { r.fade = true; continue; }
        bool span = false, at_l = false, at_r = false;
        for (std::size_t t0 = 0; t0 + 2 < d.tri.size(); t0 += 3) {
            const float lo = std::min({d.tri[t0].x, d.tri[t0 + 1].x, d.tri[t0 + 2].x});
            const float hi = std::max({d.tri[t0].x, d.tri[t0 + 1].x, d.tri[t0 + 2].x});
            span = span || (lo <= 0.5f && hi >= right - 0.5f);
            at_l = at_l || std::fabs(lo) < 0.5f;
            at_r = at_r || std::fabs(hi - right) < 0.5f;
        }
        const bool repeat = d.wms == 0 || d.wms == 3;
        if (span) {
            // mac5: a translucent full-width layer (alpha < 128: drawn over the page, not a picture of its own) is an
            // effect -- the garage's TV static between cars (rough 82, alpha 106) -- and covers the window like the page
            std::uint8_t alpha = 0;
            for (const HudVertex& v : d.tri) alpha = std::max(alpha, v.rgba[3]);
            // (and the static itself when opaque: a car that has not loaded yet, Windows 2026-11-09)
            const bool layer = (alpha > 0 && alpha < 128) || (tv_static && tv_static(d));
            r.span = repeat || pc == kSelectionBarRender || roughness(d) < kFeSmooth || layer;
            r.tile = layer && !repeat && roughness(d) >= kFeSmooth;   // the static's blocks stay square: tiled, not stretched
            picture[i] = !r.span;
            continue;
        }
        if (at_l != at_r) {
            cap_side[i] = at_l ? -1 : 1;
            if (!repeat && roughness(d) < kFeSmooth) r.span = true;   // a uniform piece running off one edge stretches there (one-sided)
        }
    }
    // caps: a one-sided textured piece whose object has an untextured draw ending where the piece begins
    for (std::size_t i = 0; i < n; ++i) {
        if (cap_side[i] == 0 || f.draws[i].object < 0) continue;
        float plo, phi;
        extent(f.draws[i], plo, phi);
        const float inner = cap_side[i] > 0 ? plo : phi;
        for (std::size_t j = 0; j < n; ++j) {
            const HudDraw& g = f.draws[j];
            if (j == i || g.texture != 0 || g.object != f.draws[i].object || g.video >= 0 || !plan.rule[j].fill) continue;
            float glo, ghi;
            extent(g, glo, ghi);
            const float end = cap_side[i] > 0 ? ghi : glo;
            if (std::fabs(end - inner) >= 0.5f) continue;
            plan.rule[i] = HudFrontendRule{};
            plan.rule[i].move = float(cap_side[i]) * ext;
            plan.rule[j].end_x = inner;
            plan.rule[j].end_dx = float(cap_side[i]) * ext;
        }
    }
    // mac5 (2026-11-08): the heading bar's big_curve (a rough piece at the left 4:3 edge, 0..272) hugs the window's
    // left edge: its foot goes out by ext, its top end half as far (moved more than stretched, chosen after
    // seeing a one-sided stretch); the heading's band body and lines, whose left ends sit on the curve (76, 161), move
    // with the curve at their x
    constexpr std::uint32_t kHeadingBarPc = 0x0031B130U;   // CB3HeadingBar2dObject::_Render [C names, decomp tables]
    for (std::size_t i = 0; i < n; ++i) {
        const HudDraw& d = f.draws[i];
        if (cap_side[i] != -1 || d.texture == 0 || pc_of(d) != kHeadingBarPc || plan.rule[i].extends()) continue;
        float lo, hi;
        extent(d, lo, hi);
        if (!(hi - lo > 1.0f)) continue;
        plan.rule[i] = HudFrontendRule{};
        plan.rule[i].stretch_lo = lo; plan.rule[i].stretch_hi = hi; plan.rule[i].stretch_dx = -ext; plan.rule[i].stretch_dx_hi = -0.5f * ext;
        for (std::size_t j = 0; j < n; ++j) {
            const HudDraw& g = f.draws[j];
            if (j == i || g.texture != 0 || g.object != d.object || !plan.rule[j].fill) continue;
            float glo, ghi;
            extent(g, glo, ghi);
            if (glo <= lo + 0.5f || glo >= hi - 0.5f) continue;   // its left end on the curve
            // every vertex of it over the curve's span moves as the curve does there: the band's body (76) and its lines
            // (161: they kept their x and left a gap once the curve moved, as reported)
            plan.rule[j].end_x = glo;
            plan.rule[j].end_dx = plan.rule[i].stretch_at(glo);
            plan.rule[j].stretch_lo = lo; plan.rule[j].stretch_hi = hi;
            plan.rule[j].stretch_dx = plan.rule[i].stretch_dx; plan.rule[j].stretch_dx_hi = plan.rule[i].stretch_dx_hi;
        }
    }
    // mac5: pictures (not movies, not full width) reaching a 4:3 edge, at least 48 units each way: side-filled like the
    // backdrop over their own extent (the Crash Nav map's tiles at the left edge)
    for (std::size_t i = 0; i < n; ++i) {
        const HudDraw& d = f.draws[i];
        // one textured quad (an image or a tile of one; text, curves and panels are other shapes or full width)
        if (plan.rule[i].extends() || d.video >= 0 || d.texture == 0 || d.tri.size() != 6) continue;
        float lo, hi;
        extent(d, lo, hi);
        const bool left = std::fabs(lo) <= 0.5f, right_edge = std::fabs(hi - right) <= 0.5f;
        if (left == right_edge || hi - lo < 48.0f) continue;   // one edge, not the full width (a backdrop candidate)
        HudFrontendPlan::EdgeImage e;
        e.draw = int(i);
        e.box[0] = lo; e.box[2] = hi; e.box[1] = 1e30f; e.box[3] = -1e30f;
        std::uint8_t a = 0;
        for (const HudVertex& v : d.tri) { e.box[1] = std::min(e.box[1], v.y); e.box[3] = std::max(e.box[3], v.y); a = std::max(a, v.rgba[3]); }
        // the part the GS draws: the map's tiles run past their panel, clipped by its scissor (0..388)
        e.box[1] = std::max(e.box[1], float(d.scissor[1]));
        e.box[3] = std::min(e.box[3], float(d.scissor[3] + 1));
        if (left && float(d.scissor[0]) > 0.5f) continue;                       // clipped short of the edge
        if (right_edge && float(d.scissor[2] + 1) < right - 0.5f) continue;
        if (a == 0 || e.box[3] - e.box[1] < 48.0f) continue;
        plan.edge_images.push_back(e);
    }
    // the backdrop: the first movie reaching an edge (all its draws), else the first full-width picture
    std::map<int, std::array<float, 4>> movie_box;
    std::map<int, std::size_t> movie_last;
    std::vector<int> movie_order;
    for (std::size_t i = 0; i < n; ++i) {
        const HudDraw& d = f.draws[i];
        if (d.video < 0 || d.tri.empty()) continue;
        auto it = movie_box.find(d.video);
        if (it == movie_box.end()) { it = movie_box.emplace(d.video, std::array<float, 4>{1e30f, 1e30f, -1e30f, -1e30f}).first; movie_order.push_back(d.video); }
        for (const HudVertex& v : d.tri) {
            it->second[0] = std::min(it->second[0], v.x); it->second[1] = std::min(it->second[1], v.y);
            it->second[2] = std::max(it->second[2], v.x); it->second[3] = std::max(it->second[3], v.y);
        }
        movie_last[d.video] = i;
    }
    for (int m : movie_order) {
        const auto& b = movie_box[m];
        if (b[0] <= 0.5f || b[2] >= right - 0.5f) {
            plan.backdrop = int(movie_last[m]);
            std::copy(b.begin(), b.end(), plan.box);
            return plan;
        }
    }
    if (!movie_order.empty()) return plan;
    for (std::size_t i = 0; i < n; ++i) {
        const HudDraw& d = f.draws[i];
        if (!picture[i] || plan.rule[i].extends()) continue;
        float lo, hi;
        extent(d, lo, hi);
        if (lo < -0.5f || lo > 0.5f || hi < right - 0.5f || hi > right + 0.5f) continue;
        std::uint8_t a = 0;
        for (const HudVertex& v : d.tri) a = std::max(a, v.rgba[3]);
        if (a == 0) continue;
        plan.backdrop = int(i);
        plan.box[0] = lo; plan.box[2] = hi; plan.box[1] = 1e30f; plan.box[3] = -1e30f;
        for (const HudVertex& v : d.tri) { plan.box[1] = std::min(plan.box[1], v.y); plan.box[3] = std::max(plan.box[3], v.y); }
        break;
    }
    return plan;
}

std::uint8_t host_skip_hint_decode(const Image& im) {
    std::uint8_t head[16];
    if (!im.read(kHostSkipBase, head, 16) || rd32(head) != 0x314B5348U) return 0;   // 'HSK1'
    return std::uint8_t(std::min<std::uint32_t>(rd32(head + 8), 255U));
}

bool host_hold_decode(const Image& im) {
    std::uint8_t head[16];
    return im.read(kHostHoldBase, head, 16) && rd32(head) == 0x31444848U && rd32(head + 8) != 0;   // 'HHD1'
}

std::shared_ptr<const b3live::MenuRecord> host_menu_decode(const Image& im, std::shared_ptr<const b3live::MenuRecord> prev) {
    std::uint8_t head[16];
    if (!im.read(b3live::kHostMenuBase, head, 16) || rd32(head) != b3live::kMenuMagic) return prev;
    if (prev && prev->serial == rd32(head + 4)) return prev;
    const std::uint32_t n = std::min<std::uint32_t>(rd32(head + 12), b3live::kMenuSelects);
    std::vector<std::uint8_t> b(16 + std::size_t(n) * (4 + b3live::kMenuRows) * 4);
    if (!im.read(b3live::kHostMenuBase, b.data(), b.size())) return prev;
    auto r = std::make_shared<b3live::MenuRecord>();
    if (!b3live::menu_record_parse(b.data(), b.size(), *r)) return prev;
    return r;
}

std::shared_ptr<const HostFonts> host_fonts_decode(const Image& im, std::shared_ptr<const HostFonts> prev) {
    std::uint8_t head[16];
    if (!im.read(kHostFontBase, head, 16) || rd32(head) != 0x314E4648U) return prev;   // 'HFN1'
    const std::uint32_t serial = rd32(head + 4), n = rd32(head + 8), nf = rd32(head + 12);
    if (prev && prev->serial == serial) return prev;
    if (n > kHostFontMax - 16 || nf > 8) return prev;
    std::vector<std::uint8_t> b(n);
    if (n && !im.read(kHostFontBase + 16, b.data(), n)) return prev;
    auto f = std::make_shared<HostFonts>();
    f->serial = serial;
    std::size_t at = 0;
    const auto f32 = [&](std::size_t k) { float x; std::memcpy(&x, b.data() + k, 4); return x; };
    for (std::uint32_t k = 0; k < nf; ++k) {
        if (at + 20 > b.size()) return prev;
        HostFont font;
        font.raster = rd32(b.data() + at);
        font.scale_x = f32(at + 4);
        font.scale_y = f32(at + 8);
        font.baseline = f32(at + 12);
        const std::uint32_t ng = rd32(b.data() + at + 16);
        at += 20;
        if (ng > 4096 || at + std::size_t(ng) * 32 > b.size()) return prev;
        for (std::uint32_t g = 0; g < ng; ++g, at += 32) {
            HostFontGlyph gl;
            gl.u = f32(at); gl.v = f32(at + 4); gl.w = f32(at + 8); gl.h = f32(at + 12);
            gl.ox = f32(at + 16); gl.oy = f32(at + 20); gl.advance = f32(at + 24);
            gl.code = std::uint16_t(b[at + 28] | b[at + 29] << 8);
            font.glyphs.push_back(gl);
        }
        f->fonts.push_back(std::move(font));
    }
    return f;
}

const HostFontGlyph* HostFont::find(std::uint16_t c) const {
    if (glyphs.empty()) return nullptr;
    for (const HostFontGlyph& g : glyphs)
        if (g.code == c) return &g;
    return &glyphs.back();   // [C] the game's lookup stops at the last glyph
}

float HostFont::width(std::u16string_view text, float size) const {
    if (text.empty()) return 0.0f;
    const HostFontGlyph* g = find(text[0]);
    float w = -g->ox;
    for (std::size_t k = 1; k < text.size(); ++k) {
        w += g->advance;
        g = find(text[k]);
    }
    w += g->ox + g->w;
    return w * scale_x * size;
}

bool hud_ticker_layout(const HudObject& o, const HostFont& f, float nu, float nv, float ext, const std::uint8_t rgba[4], std::uint32_t z,
                       std::vector<HudVertex>& out) {
    if (o.ticker_font == 0 || o.ticker_text.empty() || f.glyphs.empty()) return false;
    const float scale[2] = {o.ticker_recip[0] * o.size[0], o.ticker_recip[1] * o.size[1]};
    if (!(scale[0] > 0.0f) || !(scale[1] > 0.0f)) return false;
    const float left = o.pos[0] - ext, top = o.pos[1], end = o.pos[0] + o.size[0] + ext, bottom = o.pos[1] + o.size[1];
    const float strip = 608.0f + 2.0f * ext / scale[0];   // [C] 608: the game's scroll span (its strip at scale 1)
    float offset = 100.0f * o.ticker_scroll - strip;
    if (offset > strip && o.ticker_extent > 0.0f) offset = offset - o.ticker_extent * float(int(offset / o.ticker_extent));
    offset *= scale[0];
    float cx = left - offset;
    const float size[2] = {f.scale_x * scale[0] * 28.0f, f.scale_y * scale[1] * 28.0f}, slant = 0.36f * scale[0];
    const std::u16string& text = o.ticker_text;
    std::size_t at = 0;
    // GS 12.4: the 2D vertices the GS gets are on a 1/16 grid (truncated, as Gt2d's ftoi4)
    const auto q = [](float x) { return std::floor(x * 16.0f) / 16.0f; };
    struct V { float x, y, u, v; };
    std::vector<V> strip_v;
    // one batch (_BuildBatchData): a word's glyphs as a strip; the cursor and the text move on
    const auto build = [&]() {
        strip_v.clear();
        const HostFontGlyph* g = f.find(text[at]);
        std::size_t s = at;
        for (;;) {
            if (g->u >= 0.0f) {
                if (strip_v.size() + 5 >= 0x1FD) break;
                const float x3 = cx + g->ox * size[0], y3 = top + g->oy * size[1];
                const float x2 = x3 + size[0] * g->w, y2 = y3 + size[1] * g->h;
                V v1{x3, y2, g->u, g->v + g->h}, v2{x2, y2, g->u + g->w, g->v + g->h}, v3{x3, y3, g->u, g->v}, v4{x2, y3, g->u + g->w, g->v};
                for (V* v : {&v1, &v2, &v3, &v4}) v->x += slant * (bottom - v->y);
                strip_v.insert(strip_v.end(), {v1, v1, v2, v3, v4});
            }
            cx += g->advance * size[0];
            if (++s >= text.size()) break;
            if (g->u < 0.0f) break;
            strip_v.push_back(strip_v.empty() ? V{} : strip_v.back());
            g = f.find(text[s]);
        }
        if (s >= text.size()) { at = 0; cx += o.ticker_gap * scale[0]; }
        else at = s;
    };
    int guard = 0;
    do {
        do build(); while (!(cx > left) && ++guard < 100000);
        // the join vertex before a glyph that never came (the batch ended on a space) closes nothing: drawn as the GS
        // would, a strip of what was built
        if (strip_v.size() >= 3) {
            for (std::size_t k = 2; k < strip_v.size(); ++k)
                for (const V* v : {&strip_v[k - 2], &strip_v[k - 1], &strip_v[k]}) {
                    HudVertex h;
                    h.x = q(v->x);
                    h.y = q(v->y);
                    h.u = v->u * nu;
                    h.v = v->v * nv;
                    std::memcpy(h.rgba, rgba, 4);
                    h.z = z;
                    out.push_back(h);
                }
        }
    } while (cx <= end && ++guard < 100000);
    return true;
}

std::shared_ptr<const HostSettings> host_settings_decode(const Image& im, std::shared_ptr<const HostSettings> prev) {
    std::uint8_t head[16];
    if (!im.read(kHostSetBase, head, 16) || rd32(head) != 0x54455348U) return prev;   // 'HSET'
    const std::uint32_t serial = rd32(head + 4), n = rd32(head + 8);
    if (prev && prev->serial == serial) return prev;
    if (n > kHostSetMax - 16) return prev;
    auto s = std::make_shared<HostSettings>();
    s->serial = serial;
    s->text.resize(n);
    if (n && !im.read(kHostSetBase + 16, reinterpret_cast<std::uint8_t*>(s->text.data()), n)) return prev;
    return s;
}

bool read_png_rgba(const std::string& path, int& w, int& h, std::vector<std::uint8_t>& rgba) {
    std::ifstream f(path, std::ios::binary);
    if (!f && path.size() > 2 && path[1] == ':') {   // C:\x\y -> /mnt/c/x/y
        std::string q = "/mnt/" + std::string(1, char(std::tolower(static_cast<unsigned char>(path[0])))) + path.substr(2);
        for (char& ch : q) if (ch == '\\') ch = '/';
        f.open(q, std::ios::binary);
    }
    if (!f) return false;
    const std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (d.size() < 33 || std::memcmp(d.data(), sig, 8) != 0) return false;
    const auto be = [&](std::size_t k) { return std::uint32_t(d[k]) << 24 | std::uint32_t(d[k + 1]) << 16 | std::uint32_t(d[k + 2]) << 8 | d[k + 3]; };
    int depth = 0, ctype = 0, inter = 0;
    std::vector<std::uint8_t> z;
    for (std::size_t at = 8; at + 12 <= d.size();) {
        const std::uint32_t n = be(at);
        if (at + 12 + std::size_t(n) > d.size()) return false;
        const std::uint8_t* t = d.data() + at + 4;
        if (!std::memcmp(t, "IHDR", 4) && n >= 13) {
            w = int(be(at + 8)); h = int(be(at + 12));
            depth = d[at + 16]; ctype = d[at + 17]; inter = d[at + 20];
        } else if (!std::memcmp(t, "IDAT", 4)) {
            z.insert(z.end(), t + 4, t + 4 + n);
        } else if (!std::memcmp(t, "IEND", 4)) break;
        at += 12 + n;
    }
    const int ch = ctype == 6 ? 4 : ctype == 2 ? 3 : ctype == 4 ? 2 : ctype == 0 ? 1 : 0;
    if (depth != 8 || ch == 0 || inter != 0 || w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    const std::size_t stride = std::size_t(w) * ch;
    std::vector<std::uint8_t> raw((stride + 1) * std::size_t(h));
    uLongf rl = uLongf(raw.size());
    if (uncompress(raw.data(), &rl, z.data(), uLong(z.size())) != Z_OK || rl != raw.size()) return false;
    std::vector<std::uint8_t> img(stride * std::size_t(h));
    for (int y = 0; y < h; ++y) {
        const std::uint8_t ft = raw[std::size_t(y) * (stride + 1)];
        const std::uint8_t* src = raw.data() + std::size_t(y) * (stride + 1) + 1;
        std::uint8_t* row = img.data() + std::size_t(y) * stride;
        const std::uint8_t* up = y > 0 ? row - stride : nullptr;
        for (std::size_t x = 0; x < stride; ++x) {
            const int a = x >= std::size_t(ch) ? row[x - ch] : 0, b = up ? up[x] : 0, c = up && x >= std::size_t(ch) ? up[x - ch] : 0;
            int pr = a;
            if (ft == 4) { const int pp = a + b - c, pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c); pr = pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
            const int add = ft == 0 ? 0 : ft == 1 ? a : ft == 2 ? b : ft == 3 ? (a + b) / 2 : ft == 4 ? pr : -1;
            if (add < 0) return false;
            row[x] = std::uint8_t(src[x] + add);
        }
    }
    rgba.resize(std::size_t(w) * std::size_t(h) * 4);
    for (std::size_t i = 0; i < std::size_t(w) * std::size_t(h); ++i) {
        const std::uint8_t* s = img.data() + i * ch;
        std::uint8_t* o = rgba.data() + i * 4;
        if (ch >= 3) { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; } else o[0] = o[1] = o[2] = s[0];
        o[3] = ch == 4 ? s[3] : ch == 2 ? s[1] : 255;
    }
    return true;
}

}  // namespace b3replay
