#include "capture.h"

#include <zlib.h>

#include "b3asset/discfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <limits>
#include <mutex>
#include <memory>
#include <set>
#include <unordered_map>

namespace b3replay {

Mat34 mul(const Mat34& a, const Mat34& b) {
    Mat34 o;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j)
            o.r[i][j] = a.r[i][0] * b.r[0][j] + a.r[i][1] * b.r[1][j] + a.r[i][2] * b.r[2][j] + (i == 3 ? b.r[3][j] : 0.0f);
    return o;
}

Mat34 inverse_affine(const Mat34& m) {
    // general 3x3 inverse (frames may carry scale), then the translation
    const auto& r = m.r;
    const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                      r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    Mat34 o;
    if (std::fabs(det) < 1e-12f) return o;
    const float k = 1.0f / det;
    o.r[0][0] = (r[1][1] * r[2][2] - r[1][2] * r[2][1]) * k;
    o.r[0][1] = (r[0][2] * r[2][1] - r[0][1] * r[2][2]) * k;
    o.r[0][2] = (r[0][1] * r[1][2] - r[0][2] * r[1][1]) * k;
    o.r[1][0] = (r[1][2] * r[2][0] - r[1][0] * r[2][2]) * k;
    o.r[1][1] = (r[0][0] * r[2][2] - r[0][2] * r[2][0]) * k;
    o.r[1][2] = (r[0][2] * r[1][0] - r[0][0] * r[1][2]) * k;
    o.r[2][0] = (r[1][0] * r[2][1] - r[1][1] * r[2][0]) * k;
    o.r[2][1] = (r[0][1] * r[2][0] - r[0][0] * r[2][1]) * k;
    o.r[2][2] = (r[0][0] * r[1][1] - r[0][1] * r[1][0]) * k;
    for (int j = 0; j < 3; ++j) o.r[3][j] = -(r[3][0] * o.r[0][j] + r[3][1] * o.r[1][j] + r[3][2] * o.r[2][j]);
    return o;
}

void to_colmajor(const Mat34& m, float out[16]) {
    for (int c = 0; c < 4; ++c) {
        for (int k = 0; k < 3; ++k) out[c * 4 + k] = m.r[c][k];
        out[c * 4 + 3] = c == 3 ? 1.0f : 0.0f;
    }
}

constexpr std::uint32_t kPage = 0x1000;
constexpr std::uint32_t kWorld = 0x522660, kGm = 0x665EC0;
constexpr std::uint32_t kHostView1Base = 0x41120000;   // split2: scene_capture.cpp's 'HSV1' record (view 1's results)

struct Image::Pages {
    struct Page { std::uint8_t bytes[kPage]; std::uint8_t valid[kPage / 16]; };
    std::unordered_map<std::uint32_t, std::unique_ptr<Page>> pages;
    // perf (2026-11-12): the page found last (the decoders read field after field of the same structures)
    std::uint32_t last_at = 0;
    Page* last = nullptr;
    Page* find(std::uint32_t page) {
        if (last && page == last_at) return last;
        const auto it = pages.find(page);
        if (it == pages.end()) return nullptr;
        last_at = page;
        last = it->second.get();
        return last;
    }
};

Image::Image() : p_(new Pages) {}
Image::~Image() { delete p_; }
void Image::clear() { p_->pages.clear(); p_->last = nullptr; }
// perf (2026-11-12): one page lookup per page touched, not per block / byte (the live renderer decodes every tick on its
// frame loop: the per-byte lookups were most of its time). The same bytes as before, also when a read fails part way
// (`out` holds the bytes before the first missing block).
void Image::write(std::uint32_t a, const std::uint8_t* p, std::uint32_t n) {
    for (std::uint32_t k = 0; k < n;) {   // a, n multiples of 16: whole blocks, one copy per page
        const std::uint32_t addr = a + k, off = addr & (kPage - 1);
        auto& slot = p_->pages[addr & ~(kPage - 1)];
        if (!slot) { slot = std::make_unique<Pages::Page>(); std::memset(slot->valid, 0, sizeof slot->valid); }
        const std::uint32_t run = std::min<std::uint32_t>(kPage - off, (n - k + 15) & ~15U);
        std::memcpy(slot->bytes + off, p + k, std::min(run, n - k));
        std::memset(slot->valid + off / 16, 1, run / 16);
        k += run;
    }
}
bool Image::read(std::uint32_t a, void* out, std::uint32_t n) const {
    auto* o = static_cast<std::uint8_t*>(out);
    std::uint32_t k = 0;
    while (k < n) {
        const std::uint32_t addr = a + k;
        const Pages::Page* found = p_->find(addr & ~(kPage - 1));
        if (!found) return false;
        const Pages::Page& pg = *found;
        std::uint32_t off = addr & (kPage - 1);
        while (k < n && off < kPage) {
            if (!pg.valid[off / 16]) return false;
            const std::uint32_t run = std::min<std::uint32_t>(16 - (off & 15), n - k);
            std::memcpy(o + k, pg.bytes + off, run);
            k += run;
            off += run;
        }
    }
    return true;
}
bool Image::has(std::uint32_t a, std::uint32_t n) const { std::uint8_t b; for (std::uint32_t k = 0; k < n; k += 16) if (!read(a + k, &b, 1)) return false; return n == 0 || read(a + n - 1, &b, 1); }
std::uint32_t Image::u32(std::uint32_t a) const { std::uint32_t v = 0; return read(a, &v, 4) ? v : 0; }
std::uint16_t Image::u16(std::uint32_t a) const { std::uint16_t v = 0; return read(a, &v, 2) ? v : 0; }
std::uint8_t Image::u8(std::uint32_t a) const { std::uint8_t v = 0; return read(a, &v, 1) ? v : 0; }
float Image::f32(std::uint32_t a) const { float v = 0; return read(a, &v, 4) ? v : 0.0f; }
bool Image::mat(std::uint32_t a, Mat34& m) const {
    float v[16];
    if (!read(a, v, 64)) return false;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) m.r[i][j] = v[i * 4 + j];
    return true;
}

namespace {

bool ram(std::uint32_t a) { return a >= 0x00100000 && a < 0x02000000; }

// CreateRotationMatrixOnPrincipleAxisWithoutSinCos (decomp: RenderVehicle's inline, B3O0PCH map name) [L body]
Mat34 axis_rotation(const float axis[3], float s, float c) {
    float t[3], as[3];
    for (int k = 0; k < 3; ++k) { t[k] = (1.0f - axis[k]) * (1.0f - c); as[k] = axis[k] * s; }
    Mat34 m;
    m.r[0][0] = 1.0f - t[0]; m.r[0][1] = as[2];        m.r[0][2] = -as[1];
    m.r[1][0] = -as[2];       m.r[1][1] = 1.0f - t[1]; m.r[1][2] = as[0];
    m.r[2][0] = as[1];        m.r[2][1] = -as[0];       m.r[2][2] = 1.0f - t[2];
    return m;
}

bool decode_vehicle(const Image& im, std::uint32_t ep, std::uint32_t vp, std::uint32_t id, VehicleKind kind, Vehicle& v) {
    v.id = id;
    v.kind = kind;
    v.data = im.u32(ep + 0x40);
    if (!ram(v.data) || !im.has(v.data, 0x70) || !im.mat(ep, v.main)) return false;
    // CB3RaceCar::Draw 0x151A40 [C matched] swaps the matrix at +0x1920 into +0x00 while +0x199B is set (the slammed car's
    // fishtail: CB3RaceCar::UpdateAggressiveReaction 0x151DC0 [L draft] yaws +0x1920 about the up axis with a decaying
    // sine) and only then calls RenderVehicle, so the body, the panels (+0x180 x main) and the wheels (+0x0 x main) all
    // take it. Read here, before the parts are placed: swapped after them, only the body moved (the wobble report).
    if (kind == VehicleKind::kRaceCar && im.u8(id + 0x199B) != 0) {
        Mat34 m;
        if (im.mat(id + 0x1920, m)) v.main = m;
    }
    v.lit = im.u8(ep + 0x58) & 0xFC;
    v.paint = im.u8(ep + 0x59);
    if (kind == VehicleKind::kRaceCar && im.has(id + 0x1914, 4) && im.has(id + 0x27C0, 4) && im.u32(id + 0x1914) == 0 &&
        im.u32(id + 0x27C0) < 2)
        v.player = std::int8_t(im.u32(id + 0x27C0));   // carpaint
    v.frustum = std::int8_t(im.u8(ep + 0x5B));
    v.radius = im.f32(v.data + 0x14);
    for (int k = 0; k < 9 && im.has(ep + 0x5C + 4 * std::uint32_t(k), 4); ++k) { v.sh[k] = im.f32(ep + 0x5C + 4 * std::uint32_t(k)); v.sh_count = k + 1; }
    float sr = im.f32(ep + 0x44), cr = im.f32(ep + 0x48), sp = im.f32(ep + 0x4C), cp = im.f32(ep + 0x50);
    if (sr == 0 && cr == 0) cr = 1;
    if (sp == 0 && cp == 0) cp = 1;
    float right[3] = {1, 0, 0}, at[3] = {0, 0, 1};
    im.read(0x4B9D80, right, 12);   // kRightAxis
    im.read(0x4B9D90, at, 12);      // kAtAxis (the tilt's first axis, D_004B9D90)
    v.body = mul(axis_rotation(right, sp, cp), mul(axis_rotation(at, sr, cr), v.main));
    // CB3RaceCar::Draw 0x151A40 sets the tilt to sin 0 / cos 1 before RenderVehicle [C matched]: race cars never tilt.
    if (kind == VehicleKind::kRaceCar) v.body = v.main;
    if (vp == 0 || !im.has(vp, 0x380) || !im.has(vp + 0x4A0, 0x60)) return true;
    v.parts = true;
    v.nbit = std::min<int>(7, std::max<int>(0, std::int8_t(im.u8(v.data + 0x0C))));
    v.nwheel = std::min<int>(6, std::max<int>(0, std::int8_t(im.u8(v.data + 0x0D))));
    for (int w = 0; w < v.nwheel; ++w) {
        Mat34 m;
        im.mat(vp + 64 * w, m);
        v.wheel_state[w] = std::int8_t(im.u8(vp + 0x4AC + w));
        v.wheel[w] = v.wheel_state[w] == 3 ? m : mul(m, v.main);
    }
    for (int b = 0; b < v.nbit; ++b) {
        Mat34 m;
        im.mat(vp + 0x180 + 64 * b, m);
        v.bit_state[b] = std::int8_t(im.u8(vp + 0x4B2 + b));
        v.bit[b] = v.bit_state[b] >= 3 ? m : mul(m, v.body);
    }
    v.deformed = im.u8(vp + 0x101B) != 0;
    // zone matrices for RenderDeformedObject: body +0x500 (8), panel p +0x900 + 0x80 p (2) [C RenderVehicle draft]
    if (im.has(vp + 0x500, 0x780)) {
        v.zones_valid = true;
        for (int z = 0; z < 8; ++z) im.mat(vp + 0x500 + 0x40 * std::uint32_t(z), v.zone[z]);
        for (int b = 0; b < v.nbit; ++b) {
            v.bit_deformed[b] = im.u8(vp + 0x101C + std::uint32_t(b)) != 0;
            for (int z = 0; z < 2; ++z) im.mat(vp + 0x900 + 0x80 * std::uint32_t(b) + 0x40 * std::uint32_t(z), v.bit_zone[b][z]);
        }
    }
    v.glass = im.u8(vp + 0x1014);
    v.damage = im.f32(vp + 0x370);
    v.exploded = (im.u8(vp + 0x1015) & 0x10) != 0;
    v.explode_time = im.f32(kWorld + 0x20) - im.f32(vp + 0x100C);
    for (int w = 0; w < v.nwheel; ++w) v.wheel_speed[w] = im.f32(vp + 0x4E8 + 4 * std::uint32_t(w));
    return true;
}

void decode_tick(const Image& im, Tick& t, Capture& cap) {
    t.ticks_per_frame = int(im.u32(0x51BAC4));
    // camera: player 1's viewport (graphics manager +0x90), else the full-screen one (+0x310)
    // gsoff1: one viewport's camera (+0x90 player 1 / +0x130 player 2 / +0x310 full screen)
    const auto viewport_camera = [&](std::uint32_t vpo, Camera& out) {
        const std::uint32_t vp = kGm + vpo;
        const std::uint32_t rwcam = im.u32(vp + 0x58), cgcam = im.u32(vp + 0x68);
        const std::uint32_t frame = ram(rwcam) ? im.u32(rwcam + 4) : 0;
        Camera c;
        if (!ram(frame) || !im.mat(frame + 0x10, c.m) || !im.has(vp, 0xA0)) return false;
        c.half_w = im.f32(vp + 0x48);
        c.half_h = im.f32(vp + 0x4C);
        c.znear = im.f32(vp + 0x84);
        c.zfar = im.f32(vp + 0x88);
        c.fov = ram(cgcam) ? im.f32(cgcam + 4) : 0.0f;
        im.read(vp, c.screen, 64);
        if (ram(cgcam)) im.read(cgcam + 0x20, c.lod_pos, 12);
        else std::memcpy(c.lod_pos, c.m.r[3], sizeof c.lod_pos);
        c.lod_scale = im.f32(vp + 0x90);
        if (!(c.lod_scale > 0)) c.lod_scale = 1.0f;
        float ld[5];
        if (im.read(kGm + 0x70, ld, sizeof ld) && ld[0] > 0) std::memcpy(c.lod_dist, ld, sizeof ld);
        if (!(c.half_w > 0 && c.half_h > 0)) return false;
        c.valid = true;
        out = c;
        return true;
    };
    for (std::uint32_t vpo : {0x90U, 0x310U})
        if (viewport_camera(vpo, t.cam)) break;
    const auto sun_cam = [&](std::uint32_t r, SunCam& sc) {   // water1: CB3SunRenderer::SetupCamera's record
        if (!im.has(kGm, 4) || !im.has(r, 0x230)) return;
        sc.valid = true;
        sc.gm0 = int(im.u32(kGm));
        im.read(r + 0xD0, sc.pos, 12);
        im.read(r + 0xE0, sc.m, 64);
        sc.f200 = im.f32(r + 0x200);
        sc.f208 = im.f32(r + 0x208);
        sc.f20C = im.f32(r + 0x20C);
        sc.size[0] = im.f32(r + 0x218);
        sc.size[1] = im.f32(r + 0x21C);
        sc.offset[0] = im.f32(r + 0x220);
        sc.offset[1] = im.f32(r + 0x224);
    };
    sun_cam(kGm + 0x470, t.sun);   // record 0
    // gsoff1 (§29): a 2-player split screen: player 2's view from viewport +0x130
    if (const std::uint32_t game = im.u32(0x51BA88); game == 0x51AEC0U) {
        t.split = true;
        View2In& v = t.view2;
        if (viewport_camera(0x130U, v.cam)) v.valid = true;
        // (camera record 0 then holds player 2's values: its +0xE0 matrix equals viewport +0x130's screen matrix, record 1
        // neither player's [C gsoff1mp2 polls 1751..2406]; the records feed only the VU checks and the sun trace, so
        // both views share them)
    }
    // water1: the params block func_0030A9D0 fills (&D_00666090 = graphics manager +0x1D0, view 1); B3_BLUR_PTR=1 shows
    // what D_00666274 (+0x3B4) points at
    if (std::getenv("B3_BLUR_PTR")) std::fprintf(stderr, "blur ptr %08x\n", im.u32(kGm + 0x3B4));
    // split2 (§29.6): view 2's block is +0x270 (OpenViewport 0x227360 case 2 [C]), its car player 2's +0x18EA
    for (int idx = 0; idx < (t.split ? 2 : 1); ++idx) {
        const std::uint32_t bp = kGm + 0x1D0 + 0xA0 * std::uint32_t(idx), car = kWorld + 0x12AD50 + 0x27D0 * std::uint32_t(idx);
        if (!im.has(bp, 0x44)) continue;
        RadialBlurIn& b = idx == 0 ? t.blur : t.view2.blur;
        b.valid = true;
        im.read(bp, b.centre, 8);
        im.read(bp + 8, b.radius, 8);
        im.read(bp + 0x10, b.uv_scale, 8);
        b.colour = im.f32(bp + 0x18);
        b.amount = im.f32(bp + 0x1C);
        b.flip = im.u8(bp + 0x30) != 0;
        b.texture = im.u32(bp + 0x40);
        b.on = im.u8(kGm + 0x6A) != 0;
        b.crashed = im.has(car + 0x18EA, 1) && im.u8(car + 0x18EA) != 0;
    }
    const std::uint32_t g = im.u32(0x64C994);
    if (ram(g)) { t.route_size = im.u32(g + 4); t.route_units = std::int16_t(im.u16(g + 0x54)); }
    if (ram(g) && im.read(g, t.route_head.data(), 0x60)) { t.route_base = g; t.route_head_ok = true; }   // gsoff1
    const std::uint32_t st = kWorld + 0x12A348;
    t.track_on = im.u8(st + 0x44) != 0;
    for (int j = 0; j < 17; ++j) {
        t.vis[j] = im.u16(st + 0x58 + 2 * j);
        const std::uint32_t blk = im.u32(st + 0x7C + 4 * j);
        if (ram(blk) && im.has(blk, 0x60)) { t.slot_unit[j] = int(im.u32(blk + 4)); t.slot_kind[j] = int(im.u32(blk + 0x58)); }
        const std::uint32_t lod = im.u32(st + 0xC4 + 4 * j);   // reflect-tunnel: the block the env-map cameras draw
        if (ram(lod) && im.has(lod, 0x60)) t.env_unit[j] = int(im.u32(lod + 4));
    }
    if (im.has(0x665F20, 1)) t.env_track = im.u8(0x665F20);   // reflect-tunnel: RenderBeforeVehicles' env-camera switch
    if (im.has(kWorld + 0x12AD50 + 0x19AC, 1)) {   // reflect-tunnel: BuildCustomCameraData's car (player 1's view)
        const int car = std::int8_t(im.u8(kWorld + 0x12AD50 + 0x19AC));
        if (car >= 0 && car < 6 && im.has(kWorld + 0x12AD28 + 4 * std::uint32_t(car), 4)) t.env_car = im.u32(kWorld + 0x12AD28 + 4 * std::uint32_t(car));
    }
    if (const std::uint32_t cur = im.u32(st + 0x1C4); ram(cur) && im.has(cur, 8)) t.current_unit = int(im.u32(cur + 4));
    if (im.has(0x665F10, 0x10)) t.water_on = im.u8(0x665F1D) != 0;
    if (const std::uint32_t bk = im.u32(st + 0x1C8); ram(bk) && im.has(bk + 0x64, 8)) im.read(bk + 0x64, t.backdrops, 8);
    // split1 (§29.5): player 2's streamed track 1 (gWorld+0x12A514), read as track 0 above; CB3World::Render(1) draws
    // its units, backdrops and water for view 2 [C Render__8CB3WorldFi: trk = gWorld + 0x12A348 + 0x1CC idx]
    if (const std::uint32_t st1 = st + 0x1CC; t.split && im.has(st1, 0x1CC)) {
        View2In& v = t.view2;
        v.track_valid = true;
        v.track_on = im.u8(st1 + 0x44) != 0;
        for (int j = 0; j < 17; ++j) {
            v.vis[j] = im.u16(st1 + 0x58 + 2 * std::uint32_t(j));
            const std::uint32_t blk = im.u32(st1 + 0x7C + 4 * std::uint32_t(j));
            if (ram(blk) && im.has(blk, 0x60)) { v.slot_unit[j] = int(im.u32(blk + 4)); v.slot_kind[j] = int(im.u32(blk + 0x58)); }
            const std::uint32_t lod = im.u32(st1 + 0xC4 + 4 * std::uint32_t(j));   // reflect-tunnel
            if (ram(lod) && im.has(lod, 0x60)) v.env_unit[j] = int(im.u32(lod + 4));
        }
        if (constexpr std::uint32_t p2 = kWorld + 0x12AD50 + 0x27D0 + 0x19AC; im.has(p2, 1)) {   // reflect-tunnel: player 2's car
            const int car = std::int8_t(im.u8(p2));
            if (car >= 0 && car < 6 && im.has(kWorld + 0x12AD28 + 4 * std::uint32_t(car), 4)) v.env_car = im.u32(kWorld + 0x12AD28 + 4 * std::uint32_t(car));
        }
        if (const std::uint32_t cur = im.u32(st1 + 0x1C4); ram(cur) && im.has(cur, 8)) v.current_unit = int(im.u32(cur + 4));
        if (const std::uint32_t bk = im.u32(st1 + 0x1C8); ram(bk) && im.has(bk + 0x64, 8)) im.read(bk + 0x64, v.backdrops, 8);
    }
    // CB3TrafficSystem::Draw's unit test (render6): ForEveryUnitInSafeArea 0x301220 [C matched] passes the 9 ring slots
    // (+0x4C - 4 + i) mod +0xC; IsUnitVisibleToMainCamera 0x21CFF0 [C matched]: track off -> true; else the slot's LOD
    // data (+0x17C[k]) found among the 17 main-camera slots' LOD blocks (+0xC4) -> its visibility bits & 3, not found -> true
    if (im.has(st, 0x1CC)) {
        const int n = int(im.u32(st + 0xC));
        int s0 = int(im.u32(st + 0x4C)) - 4;
        if (s0 < 0) s0 += n;
        for (int i = s0; n > 0 && n <= 20 && i < s0 + 9; ++i) {
            const int k = i % n;
            t.safe_units |= 1u << k;
            bool vis = true;
            if (im.u8(st + 0x44) != 0) {
                const std::uint32_t lod = im.u32(st + 0x17C + 4 * std::uint32_t(k));
                for (int j = 0; j < 17; ++j)
                    if (im.u32(st + 0xC4 + 4 * std::uint32_t(j)) == lod) { vis = (im.u16(st + 0x58 + 2 * std::uint32_t(j)) & 3) != 0; break; }
            }
            if (vis) t.unit_visible |= 1u << k;
        }
    }
    // split1: CB3TrafficSystem::Draw(1) walks track 1's safe area the same way [C draft Draw__16CB3TrafficSystemFi:
    // lpTrack = gWorld + 0x12A348 + 0x1CC lePlayer]
    if (const std::uint32_t st1 = st + 0x1CC; t.split && t.view2.track_valid) {
        const int n = int(im.u32(st1 + 0xC));
        int s0 = int(im.u32(st1 + 0x4C)) - 4;
        if (s0 < 0) s0 += n;
        for (int i = s0; n > 0 && n <= 20 && i < s0 + 9; ++i) {
            const int k = i % n;
            t.view2.safe_units |= 1u << k;
            bool vis = true;
            if (im.u8(st1 + 0x44) != 0) {
                const std::uint32_t lod = im.u32(st1 + 0x17C + 4 * std::uint32_t(k));
                for (int j = 0; j < 17; ++j)
                    if (im.u32(st1 + 0xC4 + 4 * std::uint32_t(j)) == lod) { vis = (im.u16(st1 + 0x58 + 2 * std::uint32_t(j)) & 3) != 0; break; }
            }
            if (vis) t.view2.unit_visible |= 1u << k;
        }
    }

    auto note_model = [&](Vehicle& v) {
        auto& h = cap.models[v.data];
        im.read(v.data, h.data(), 0x70);
        const auto [it, added] = cap.head_index.emplace(h, int(cap.heads.size()));
        if (added) cap.heads.push_back(h);
        v.model = it->second;
        const std::uint32_t raster = im.u32(v.data + 0x60);
        char name[33] = {};
        if (ram(raster) && im.read(raster + 0xA8, name, 32)) cap.model_raster[v.data] = name;
    };
    const std::uint32_t ncars = im.u32(kWorld + 0x12AD1C);
    for (std::uint32_t i = 0; i < ncars && i < 6; ++i) {
        const std::uint32_t car = im.u32(kWorld + 0x12AD28 + 4 * i);
        if (!ram(car) || im.u8(car + 0x18E8) == 0) continue;   // CB3RaceCar::Draw returns when +0x18E8 is clear
        Vehicle v;
        if (!decode_vehicle(im, car, car + 0x80, car, VehicleKind::kRaceCar, v)) continue;   // (+0x1920 while +0x199B)
        note_model(v);
        t.vehicles.push_back(v);
    }
    // the player race car's traffic list: u8 indices at traffic system +0x35C24 + 0xFE car, count u8 +0x36350 + car,
    // car = s8 gWorld + 0x12AD50 + 0x19AC (player 0) [C asm 0x3079E8, 0x307A6C..0x307ABC]
    std::vector<bool> listed(0xFE, true);
    std::vector<std::uint8_t> listings(0xFE, 1);
    const std::uint32_t ts = kWorld + 0x4DE0;
    if (im.has(ts + 0x35C20, 0x740) && im.has(kWorld + 0x12AD50 + 0x19AC, 1)) {
        const int car = std::int8_t(im.u8(kWorld + 0x12AD50 + 0x19AC));
        if (car >= 0 && car < 7) {
            t.traffic_lists = true;
            listed.assign(0xFE, false);
            listings.assign(0xFE, 0);
            const int n = im.u8(ts + 0x36350 + std::uint32_t(car));
            for (int i = 0; i < n && i < 0xFE; ++i) {
                const int k = im.u8(ts + 0x35C24 + 0xFE * std::uint32_t(car) + std::uint32_t(i));
                if (k < 0xFE) { listed[std::size_t(k)] = true; ++listings[std::size_t(k)]; }
            }
        }
    }
    // split1: player 2's list (Draw(1): race car index gWorld + 0x12AD50 + 0x27D0 + 0x19AC)
    std::vector<bool> listed2(0xFE, true);
    std::vector<std::uint8_t> listings2(0xFE, 1);   // split2
    if (constexpr std::uint32_t p2 = kWorld + 0x12AD50 + 0x27D0 + 0x19AC; t.split && im.has(ts + 0x35C20, 0x740) && im.has(p2, 1)) {
        const int car = std::int8_t(im.u8(p2));
        if (car >= 0 && car < 7) {
            t.view2.traffic_lists = true;
            listed2.assign(0xFE, false);
            listings2.assign(0xFE, 0);
            const int n = im.u8(ts + 0x36350 + std::uint32_t(car));
            for (int i = 0; i < n && i < 0xFE; ++i) {
                const int k = im.u8(ts + 0x35C24 + 0xFE * std::uint32_t(car) + std::uint32_t(i));
                if (k < 0xFE) { listed2[std::size_t(k)] = true; ++listings2[std::size_t(k)]; }
            }
        }
    }
    const std::uint32_t tv0 = kWorld + 0x4DE0 + 0x127D0;
    for (std::uint32_t k = 0; k < 0xFE; ++k) {
        const std::uint32_t tv = tv0 + 0x180 * k;
        if ((im.u8(tv + 0x174) & 1) == 0) continue;
        Vehicle v;
        if (!decode_vehicle(im, tv + 0x70, 0, tv, VehicleKind::kTraffic, v)) continue;
        v.speed = im.f32(tv + 0xC4);
        v.spin = im.f32(tv + 0x128);
        v.steer = im.f32(tv + 0x130);
        if (im.has(0x4C4950, 0x20)) {   // kXAxisVector / kYAxisVector (zero in the ELF, set at run time)
            im.read(0x4C4950, v.spin_axis, 12);
            im.read(0x4C4960, v.steer_axis, 12);
        }
        v.flags = im.u8(tv + 0x174);
        v.flags2 = im.u8(tv + 0x17A);
        v.unit = std::int8_t(im.u8(tv + 0xCA));
        std::memcpy(v.cull_pos, v.main.r[3], sizeof v.cull_pos);
        v.listed = listed[k];
        v.listed2 = listed2[k];
        v.listings2 = listings2[k];
        v.listings = listings[k];
        note_model(v);
        t.vehicles.push_back(v);
    }
    const std::uint32_t nph = im.u32(0x64517C);
    for (std::uint32_t k = 0; k < nph && k < 67; ++k) {
        const std::uint32_t ph = im.u32(kWorld + 0x122A10 + 4 * k);
        if (!ram(ph)) continue;
        Vehicle v;
        if (!decode_vehicle(im, ph + 0x1360, ph + 0x13E0, ph, VehicleKind::kCrashed, v)) continue;
        v.unit = std::int8_t(im.u8(ph + 0x13BA));
        v.lifted = im.u8(ph + 0x2402) != 0;
        // Draw measures the distance from *(+0x204) (its position row); the essential params' matrix when not captured
        std::memcpy(v.cull_pos, v.main.r[3], sizeof v.cull_pos);
        if (const std::uint32_t m = im.u32(ph + 0x204); ram(m) && im.has(m + 0x30, 12)) im.read(m + 0x30, v.cull_pos, 12);
        note_model(v);
        t.vehicles.push_back(v);
    }
    // split2 (§29.6): view 1's per-vehicle results, copied by the native capture at CB3World::Render(1)'s entry (the host
    // record 'HSV1', scene_capture.cpp on_world_render); used only when it was published at this poll
    std::uint32_t head[4];
    if (t.split && im.read(kHostView1Base, head, 16) && head[0] == 0x31565348u && head[1] == t.poll && head[2] <= 0x200 &&
        im.has(kHostView1Base + 16, 8 * head[2])) {
        t.view1_snap = true;
        std::unordered_map<std::uint32_t, std::uint32_t> at;
        for (std::uint32_t e = 0; e < head[2]; ++e) at.emplace(im.u32(kHostView1Base + 16 + 8 * e), kHostView1Base + 16 + 8 * e + 4);
        for (Vehicle& v : t.vehicles) {
            const auto it = at.find(v.id);
            if (it == at.end()) continue;
            v.v1_known = true;
            v.v1_unit = std::int8_t(im.u8(it->second));
            v.v1_frustum = std::int8_t(im.u8(it->second + 1));
            if (v.kind == VehicleKind::kTraffic) v.v1_flags2 = im.u8(it->second + 2);
            else v.v1_extra = im.u8(it->second + 2);
        }
    }
}

// Props (version 2): the prop manager at 0x64A854 (CB3PropManager::Prepare 0x2B7300 / Render 0x2F99E0 field use).
void decode_props(const Image& im, Tick& t, Capture& cap) {
    constexpr std::uint32_t kProps = 0x64A854, kPhys = 0x55EF70;
    if (!im.has(kProps, 0x9C)) return;
    const std::uint32_t place = im.u32(kProps + 4), counts = im.u32(kProps + 0x14), lists = im.u32(kProps + 0x18);
    const int nplace = im.u16(kProps + 0x10), ntypes = im.u8(kProps + 0x12);
    const std::uint32_t g = im.u32(0x64C994);
    const int nunits = ram(g) ? std::int16_t(im.u16(g + 0x54)) : 0;
    // the stage logic's slot 0xB8 is `jr ra` with the return value set in its delay slot: li v0, 1 / move v0, zero [C asm
    // 0x38C050 / 0x1343F0, the targets in all 16 CB3*Logic vtables]
    if (const std::uint32_t game = im.u32(0x51BA88); ram(game)) {
        const std::uint32_t logic = im.u32(game + 0x1B4), vt = ram(logic) ? im.u32(logic) : 0, fn = ram(vt) ? im.u32(vt + 0xB8) : 0;
        t.logic_vtable = vt;
        if (ram(fn) && im.has(fn, 8) && im.u32(fn) == 0x03E00008) {
            const std::uint32_t ds = im.u32(fn + 4);
            if (ds == 0x24020001) t.props_off = 1;
            else if (ds == 0x0000102D) t.props_off = 0;
        }
    }
    if (!ram(place) || !ram(counts) || !ram(lists) || nunits <= 0 || ntypes <= 0) return;
    t.props_valid = true;
    t.prop_placements = nplace;
    t.prop_types = ntypes;
    for (int u = 0; u < nunits; ++u) {
        const std::uint32_t cu = im.u32(counts + 4 * std::uint32_t(u)), lu = im.u32(lists + 4 * std::uint32_t(u));
        if (!ram(cu) || !ram(lu)) continue;
        for (int ty = 0; ty < ntypes; ++ty) {
            const int c = im.u8(cu + std::uint32_t(ty));
            const std::uint32_t e = im.u32(lu + 4 * std::uint32_t(ty));
            if (ram(e)) t.prop_listed += c;
            for (int k = 0; k < c && ram(e); ++k) {
                if (im.u8(e + 4 * std::uint32_t(k) + 3) == 2) t.hidden.push_back(im.u16(e + 4 * std::uint32_t(k)));
            }
        }
    }
    std::sort(t.hidden.begin(), t.hidden.end());
    t.hidden.erase(std::unique(t.hidden.begin(), t.hidden.end()), t.hidden.end());
    // the prop bodies in use: CB3PropPhysics +0x220 = its CB3Prop in the dynamic list, +0x204 = the matrix it moves
    std::map<std::uint32_t, int> body_of;
    std::map<int, std::uint32_t> body_matrix;
    const std::uint32_t mask = im.u32(kPhys + 0xE9220);
    for (int k = 0; k < 32; ++k) {
        if (!((mask >> k) & 1U)) continue;
        const std::uint32_t sl = kPhys + 0xC3900 + 0x780 * std::uint32_t(k);
        if (!im.has(sl + 0x200, 0x30)) continue;
        body_of[im.u32(sl + 0x220)] = k;
        body_matrix[k] = im.u32(sl + 0x204);
    }
    const std::uint32_t ndyn = im.u32(kProps + 0xC);
    for (std::uint32_t q = 0; q < ndyn && q < 32; ++q) {
        const std::uint32_t e = kProps + 0x1C + 4 * q;
        Prop p;
        p.placement = im.u16(e);
        p.type = im.u8(e + 2);
        if (im.u8(e + 3) == 2 || p.placement >= nplace) continue;   // Render skips state 2
        if (!im.mat(place + 0x40 * std::uint32_t(p.placement), p.m)) continue;
        if (const auto b = body_of.find(e); b != body_of.end()) {
            p.slot = b->second;
            ++cap.prop_body_checked;
            if (body_matrix[p.slot] != place + 0x40 * std::uint32_t(p.placement)) ++cap.prop_body_mismatch;
        }
        t.props.push_back(p);
    }
}

// A stage-logic virtual's value from its captured code: `jr ra` with `li v0, N` / `move v0, zero` in the delay slot, or
// func_001342F0 (the crash logics' slot 0xAC: logic +0x40 == 1 or 4) [C asm]; INT_MIN if neither.
int logic_virtual(const Image& im, std::uint32_t logic, std::uint32_t slot) {
    constexpr int kUnknown = std::numeric_limits<int>::min();
    const std::uint32_t vt = ram(logic) ? im.u32(logic) : 0, fn = ram(vt) && im.has(vt + slot, 4) ? im.u32(vt + slot) : 0;
    if (!ram(fn) || !im.has(fn, 0x24)) return kUnknown;
    if (im.u32(fn) == 0x03E00008) {
        const std::uint32_t ds = im.u32(fn + 4);
        if ((ds >> 16) == 0x2402) return std::int16_t(ds & 0xFFFF);
        if (ds == 0x0000102D) return 0;
        return kUnknown;
    }
    static const std::uint32_t k1342F0[9] = {0x8C840040, 0x24030001, 0x10830005, 0x64020001, 0x24030004, 0x10830002, 0x00000000, 0x0000102D, 0x03E00008};
    for (int k = 0; k < 9; ++k) if (im.u32(fn + 4 * std::uint32_t(k)) != k1342F0[k]) return kUnknown;
    if (!im.has(logic + 0x40, 4)) return kUnknown;
    const std::uint32_t st = im.u32(logic + 0x40);
    return st == 1 || st == 4 ? 1 : 0;
}

// func_0021C010's decision (crash2) [C asm 0x21C010; IsCrashMode = func_00131E40 0x131E40].
void decode_barriers(const Image& im, CrashState& c, std::uint32_t st = kWorld + 0x12A348) {
    constexpr int kUnknown = std::numeric_limits<int>::min();
    const std::uint32_t view = im.u32(st + 0x1C), blk = im.u32(st + 0x1C8);
    if (!im.has(0x665E50, 0x10) || !im.has(st, 0x1CC) || view >= 2 || !im.has(kWorld + 0x12AD50 + 0x27D0 * view + 0x18E0, 0x10)) return;
    c.barrier_group = ram(blk) && im.has(blk + 0x60, 0x10) ? std::int8_t(im.u8(blk + 0x6D)) : -1;
    const std::uint32_t game = im.u32(0x51BA88), logic = ram(game) ? im.u32(game + 0x1B4) : 0;
    if (!ram(logic)) c.crash_mode = 0;
    else {
        const int v98 = logic_virtual(im, logic, 0x98), v9c = logic_virtual(im, logic, 0x9C);
        if (v98 == 6) c.crash_mode = 1;
        else if (v98 != kUnknown && v9c != kUnknown) c.crash_mode = v9c >= 3 && v9c <= 5;
    }
    const bool shown = im.u8(st + 0x44) != 0 && c.barrier_group >= 0 && im.u8(0x665F1E) != 0 &&
                       im.u8(kWorld + 0x12AD50 + 0x27D0 * view + 0x18EA) == 0 && im.u8(0x665E53) == 0;
    if (!shown) { c.barriers = 0; return; }
    if (c.crash_mode == 0) { c.barriers = 1; return; }
    if (c.crash_mode < 0) return;
    const int vac = logic_virtual(im, logic, 0xAC);
    if (vac != kUnknown) c.barriers = vac == 0;
}

// perf1 (docs/native-renderer.md §33): CB3GameData::Draw(0) 0x14E8C0's start / finish lines and split checkpoints [C matched
// B3GameData_0014E8C0.cpp, asm 0x15E720 / 0x15E340]. s1 = the logic's GetCheckpointMarkerFlags (slot 0x44: 3 for races,
// 0x12 Burning Lap, 9 Lap Eliminator, 0 Road Rage [C ELF]; 0 without a logic); the race car block pl = gWorld+0x12AD50
// (view 0): lap = +0x10D4, laps = +0x1384, +0x139C. Start lines (s1 & 1, lap < 0 or crash mode with +0x18EA == 0) and
// finish lines (s1 & 2 on the last lap; s1 & 0x18 from lap 0, as "check" until the last lap, then as s1 & 2) through
// CB3StartFinishLine::Draw (nothing when its +0xC is 0: Prepare passes false for start lines, true for finish lines);
// split checkpoints (s1 & 4) always "check", tinted (0.375, 0.125, 0.125). In crash mode no finish lines.
void decode_finish(const Image& im, CrashState& c) {
    constexpr int kUnknown = std::numeric_limits<int>::min();
    constexpr std::uint32_t kGameData = 0x522660 + 0x125B40, pl = 0x522660 + 0x12AD50;
    if (!im.has(0x4E1568, 8) || !im.has(pl + 0x10D4, 4) || !im.has(pl + 0x1384, 4) || !im.has(pl + 0x139C, 4) || !im.has(pl + 0x18E0, 0x10)) return;
    const std::uint32_t game = im.u32(0x51BA88), logic = ram(game) && im.has(game + 0x1B4, 4) ? im.u32(game + 0x1B4) : 0;
    int s1 = 0, crash = 0;
    if (ram(logic)) {
        s1 = logic_virtual(im, logic, 0x44);
        const int v98 = logic_virtual(im, logic, 0x98), v9c = logic_virtual(im, logic, 0x9C);
        if (s1 == kUnknown || v98 == kUnknown || (v98 != 6 && v9c == kUnknown)) return;
        crash = v98 == 6 || (v9c >= 3 && v9c <= 5);
    }
    const std::int32_t lap = std::int32_t(im.u32(pl + 0x10D4)), laps = std::int32_t(im.u32(pl + 0x1384)), n139c = std::int32_t(im.u32(pl + 0x139C));
    const std::uint32_t td = im.u32(kGameData + 0x56C);
    const bool have_td = im.u32(kGameData + 0x560) != 0 && ram(td) && im.has(td + 0x3E0, 0x20);
    if (im.u32(kGameData + 0x560) != 0 && !have_td) return;
    std::uint32_t nfinish = have_td ? im.u32(td + 0x3F0) : 0;
    const std::uint32_t nstart = have_td ? im.u32(td + 0x3EC) : 0, nsplit = have_td ? im.u32(td + 0x3F4) : 0;
    bool s4 = false;
    if (std::getenv("B3FINISH_DEBUG")) {   // perf1: the decision's inputs, on change
        static std::string last;
        char b[200];
        std::snprintf(b, sizeof b, "finish inputs: s1 %d crash %d lap %d laps %d +0x139C %d start %u finish %u split %u flags +0xC %u/%u tex %08x/%08x obj %08x",
                      s1, crash, lap, laps, n139c, nstart, nfinish, nsplit, im.u8(kGameData + 0x3A8 + 0xC), im.u8(kGameData + 0x3B8 + 0xC),
                      im.u32(kGameData + 0x3A8 + 4), im.u32(kGameData + 0x3A8 + 8), im.u32(kGameData + 0x3A8));
        if (last != b) { std::fprintf(stderr, "%s\n", b); last = b; }
    }
    if (crash) nfinish = 0;
    if ((s1 & 2) && n139c > 0 && laps > 0 && !(lap < laps - 1)) s4 = true;
    float hh = 0, hw = 0;
    im.read(0x4E1568, &hh, 4);
    im.read(0x4E156C, &hw, 4);
    auto line = [&](std::uint32_t e, int flags, bool split) {
        if (!split && im.u8(e + 0xC) == 0) return true;   // CB3StartFinishLine::Draw returns
        const std::uint32_t o = im.u32(e), tex = im.u32(e + ((flags & 0x18) || split ? 4 : 8));
        if (!ram(o) || !im.has(o, 0x20)) return false;
        if (tex == 0) return true;   // [G] no texture prepared: B3Im3d would draw it untextured; not seen
        FinishLineState f;
        im.read(o, f.pos, 12);
        im.read(o + 0x10, f.dir, 12);
        f.half_h = hh;
        f.half_w = hw;
        f.texture = (flags & 0x18) || split ? 1 : 0;
        if (split) f.rgba = 0x80101030u;   // (0.375, 0.125, 0.125, 1) x 128 = (48, 16, 16, 128)
        c.finish.push_back(f);
        return true;
    };
    if ((s1 & 1) && (lap < 0 || (crash && im.u8(pl + 0x18EA) == 0)))
        for (std::uint32_t j = 0; j < nstart && j < 2; ++j) if (!line(kGameData + 0x388 + 0x10 * j, 0, false)) return;
    if ((s1 & 0x18) && n139c > 0 && lap >= 0) {
        if (laps > 0 && !(lap < laps - 1)) s1 = (s1 & ~0x18) | 2;
        s4 = true;
    }
    if (crash) s4 = false;
    if (s4)
        for (std::uint32_t j = 0; j < nfinish && j < 2; ++j) if (!line(kGameData + 0x3A8 + 0x10 * j, s1, false)) return;
    if (s1 & 4)
        for (std::uint32_t j = 0; j < nsplit && j < 12; ++j) if (!line(kGameData + 0x3C8 + 8 * j, 0, true)) return;
    c.finish_known = true;
}

// ghost1 (capture.h LapState): view 0's lap from its CB3Score, and whether the stage logic is the Time Attack one.
void decode_lap(const Image& im, Tick& t) {
    constexpr std::uint32_t sc = kPlayerRaceCar + 0x10C0;   // CB3Score
    LapState& l = t.lap;
    if (!im.has(sc + 0x10, 0x10) || !im.has(sc + 0x2C0, 0x10)) return;
    l.valid = true;
    l.index = std::int32_t(im.u32(sc + 0x14));
    l.laps = std::int32_t(im.u32(sc + 0x2C4));
    l.time = im.f32(sc + 0x2C8);
    if (!std::isfinite(l.time)) l.time = 0;
    if (im.has(0x51BA88, 4))
        if (const std::uint32_t game = im.u32(0x51BA88); ram(game) && im.has(game + 0x1B4, 4))
            if (const std::uint32_t logic = im.u32(game + 0x1B4); ram(logic) && im.has(logic, 4))
                l.time_attack = im.u32(logic) == kBurningLapLogicVtable;
}

// speedrun1 (capture.h ScoreState): view 0's event timing from its CB3Score; read only, absent from older captures.
void decode_score(const Image& im, Tick& t) {
    constexpr std::uint32_t sc = kPlayerRaceCar + 0x10C0;   // CB3Score
    ScoreState& s = t.score;
    if (!im.has(sc, 0x40) || !im.has(sc + 0x270, 0x20) || !im.has(sc + 0x2C0, 0x10)) return;
    s.valid = true;
    s.finish = std::int32_t(im.u32(sc + 0x8));
    s.time = im.f32(sc + 0xC);
    s.lap = std::int32_t(im.u32(sc + 0x14));
    s.laps_done = std::int32_t(im.u32(sc + 0x18));
    for (int k = 0; k < 8; ++k) s.lap_times[k] = im.f32(sc + 0x1C + 4 * std::uint32_t(k));
    s.best_lap = im.f32(sc + 0x3C);
    s.state = std::int32_t(im.u32(sc + 0x27C));
    s.wrapped = im.u8(sc + 0x2C1) != 0;
    s.laps = std::int32_t(im.u32(sc + 0x2C4));
    s.lap_time = im.f32(sc + 0x2C8);
    if (im.has(0x51BA88, 4))
        if (const std::uint32_t game = im.u32(0x51BA88); ram(game) && im.has(game + 0x1B4, 4))
            if (const std::uint32_t logic = im.u32(game + 0x1B4); ram(logic) && im.has(logic, 4)) s.logic = im.u32(logic);
}

// Crash-mode game data (crash2): CB3GameData gWorld+0x125B40, as CB3GameData::Draw 0x14E8C0 walks it [C asm]; present
// in captures from crash2's hook on, whatever the version (detected by the bytes being there).
void decode_crash(const Image& im, Tick& t) {
    constexpr std::uint32_t kGameData = 0x522660 + 0x125B40;
    CrashState& c = t.crash;
    if (!im.has(kGameData, 0x590) || !im.has(0x665EA0, 0x10)) return;
    c.valid = true;
    std::uint32_t id[2] = {im.u32(kGameData + 0x558), im.u32(kGameData + 0x55C)};
    c.stage = std::uint64_t(id[0]) | std::uint64_t(id[1]) << 32;
    c.ramps_off = im.u8(0x665EA1) != 0;
    if (const std::uint32_t game = im.u32(0x51BA88); ram(game)) c.logic = im.u32(game + 0x1B4) != 0;
    if (im.has(0x4EE040 + 0x29010, 0x20)) {
        im.read(0x4EE040 + 0x29010, c.cam_quat, 16);
        im.read(0x4EE040 + 0x29020, c.cam_pos, 12);
    }
    decode_barriers(im, c);
    if (t.split && t.view2.track_valid) {   // split1: func_0021C010(track 1) in CB3World::Render(1)
        CrashState c1;
        decode_barriers(im, c1, kWorld + 0x12A514);
        t.view2.barriers = c1.barriers;
        t.view2.barrier_group = c1.barrier_group;
    }
    decode_finish(im, c);
    if (!c.finish_known) c.finish.clear();
    const std::uint32_t td = im.u32(kGameData + 0x56C);
    if (!ram(td) || !im.has(td + 0x3E0, 0x20)) return;
    const std::uint32_t npick = im.u32(td + 0x3F8), nramp = im.u32(td + 0x3FC);
    for (std::uint32_t j = 0; j < npick && j < 20; ++j) {
        const std::uint32_t e = kGameData + 0x10 * j, p = im.u32(e);
        if (!ram(p) || !im.has(p, 0x20)) continue;   // Draw returns when the data pointer is 0 (not prepared)
        CrashPickupState q;
        im.read(p, q.pos, 12);
        q.type = im.u8(p + 0x10);
        q.taken = im.u8(e + 8) != 0;
        c.pickups.push_back(q);
    }
    for (std::uint32_t j = 0; j < nramp && j < 5; ++j) {
        const std::uint32_t e = kGameData + 0x140 + 0x70 * j, r = im.u32(e + 0x60);
        if (!ram(r) || !im.has(r, 0x50)) continue;
        CrashRampState q;
        for (int k = 0; k < 6; ++k) im.read(e + 0x10 * std::uint32_t(k), q.corner[k], 12);
        Mat34 m;
        im.mat(r, m);
        std::memcpy(q.m, m.r, sizeof q.m);
        q.type = std::int32_t(im.u32(r + 0x40));
        c.ramps.push_back(q);
    }
}

// GetTimeElapsed 0x373940 [C matched] of the player camera manager's CB3BehaviourInterpolate (at `m` + 0x8C0).
float interp_elapsed(const Image& im, std::uint32_t m) {
    const std::uint32_t b = m + 0x8C0, o = im.u32(b + 0x120);
    float t = 0;
    switch (im.u32(b + 0x144)) {
    case 0: t = im.f32(o + 0xB88); break;
    case 1: t = im.f32(o + 0xB8C); break;
    case 2: t = im.f32(b + 0x12C); break;
    default: break;
    }
    return t - im.f32(b + 0x124);
}

// IsPlayerVisible 0x2E7340 of the player camera manager at `m` [C matched]: flags +0xB80 (16, 32: hidden; 1024: the
// two timed checks), its CB3BehaviourInterpolate at +0x8C0 and GetTimeElapsed 0x373940 [C matched].
bool player_visible(const Image& im, std::uint32_t m) {
    const std::uint32_t f = im.u32(m + 0xB80);
    bool vis = (f & 16) == 0 && (f & 32) == 0;
    if (!vis || (f & 1024) == 0) return vis;
    auto elapsed = [&]() { return interp_elapsed(im, m); };
    const int a = int(im.u32(m + 0x9F0)), b = int(im.u32(m + 0x9F4));
    if ((a == 4 || a == 5) && elapsed() < 0.2f) vis = false;
    if ((b == 4 || b == 5) && im.f32(m + 0x9E8) - elapsed() < 0.2f) vis = false;
    return vis;
}

// sigcam (1.2, a report: "the signature takedown camera is broken"): Camera::chase, the share of the drawn-only chase
// camera offset the player camera manager at `m` takes. Its behaviours are slots 0..19 (Prepare 0x2E7810 [C]): 2 and 3
// are its CB3BehaviourRaceCarFollow, the chase cameras; +0xB80 is the mask of the active ones (SetActiveBehaviour
// 0x2E7150, ActivateBehaviour 0x2E72D0 [C]). Seen in road rage on US/C2_V2 [C: guest RAM by VBlank, a dev script]: the
// race 0x4 (with slot 15, the effect: 0x8004), the takedown camera 0x2 (slot 1; a signature takedown's too), the own
// crash 0x40 (slot 6), the intro slots 17..19; 0x400 (slot 10, CB3BehaviourInterpolate) while the game
// blends from slot +0x9F0 to slot +0x9F4 over +0x9E8 seconds (IsPlayerVisible's operands). The apply_fov test (a race
// car near the view axis) also holds on the takedown and crash cameras, which then were pulled back and raised.
float chase_share(const Image& im, std::uint32_t m) {
    if (!im.has(m + 0x9E0, 0x30) || !im.has(m + 0xB80, 0x10)) return 1.0f;
    const auto chase = [](std::uint32_t slot) { return slot == 2 || slot == 3; };
    const std::uint32_t f = im.u32(m + 0xB80);
    if ((f & 1024) == 0) return (f & (4 | 8)) != 0 ? 1.0f : 0.0f;
    const float total = im.f32(m + 0x9E8);
    const float p = total > 0.0f ? std::clamp(interp_elapsed(im, m) / total, 0.0f, 1.0f) : 1.0f;
    return (chase(im.u32(m + 0x9F0)) ? 1.0f - p : 0.0f) + (chase(im.u32(m + 0x9F4)) ? p : 0.0f);
}

// The shadow box and wheel base of a vehicle data (RenderAmbientShadow 0x303170 [L draft]).
bool shadow_geometry(const Image& im, std::uint32_t vd, Vehicle::Shadow& s) {
    if (!ram(vd) || !im.has(vd + 0xE80, 0x20)) return false;
    s.radius = im.f32(vd + 0x18);
    im.read(vd + 0xE80, s.sup, 12);
    im.read(vd + 0xE90, s.inf, 12);
    const int n = std::int8_t(im.u8(vd + 0x0D));
    if (n < 3 || n > 6) return false;
    s.front_z = im.f32(vd + 0xBB0 + 0x40 * 1 + 8);
    s.rear_z = im.f32(vd + 0xBB0 + 0x40 * std::uint32_t(n - 2) + 8);
    return im.has(vd + 0xBB0 + 0x40 * std::uint32_t(n - 2), 16);
}

// Effects (version 3, fx1): the shadow inputs of the tick's vehicles and the skid-mark emitters
// (scene_capture.cpp grab_effects lists the sources).
void decode_effects(const Image& im, Tick& t, Capture& cap) {
    if (!im.has(0x1D9D200 + 0x2600 + 0x14630, 0x10)) return;
    t.effects_valid = true;
    t.shadows_on = im.u8(kGm + 0x67) != 0;   // D_00665F27
    t.effects_on = im.u8(kGm + 0x6C) != 0;   // D_00665F2C
    t.player_visible = player_visible(im, 0x4EE040 + 0x28E00);
    t.cam.chase = chase_share(im, 0x4EE040 + 0x28E00);   // sigcam
    // split2: player 2's camera manager (captured in split frames since split2; not captured: visible)
    if (constexpr std::uint32_t m2 = 0x4EE040 + 0x28E00 + 0xBB0; t.split && im.has(m2 + 0x9E0, 0x30) && im.has(m2 + 0xB80, 0x10))
        t.view2.player_visible = player_visible(im, m2);
    if (im.has(0x4E0FC0, 4)) t.shadow_k = im.f32(0x4E0FC0);
    t.now = im.f32(kWorld + 0x20);
    // the view's traffic list (RenderShadows: s = gWorld + 0x12AD50 + 0x19AC)
    std::set<std::uint32_t> listed;
    const std::uint32_t tr = kWorld + 0x4DE0, sv = im.u8(kWorld + 0x12AD50 + 0x19AC);
    if (sv < 0xFE)
        for (std::uint32_t j = 0, n = im.u8(tr + 0x36350 + sv); j < n; ++j)
            if (const std::uint32_t k = im.u8(tr + 0x35C24 + 0xFE * sv + j); k < 0xFE) listed.insert(tr + 0x127D0 + 0x180 * k);
    for (auto& v : t.vehicles) {
        auto& s = v.shadow;
        std::uint32_t ep = 0;
        if (v.kind == VehicleKind::kRaceCar) {
            ep = v.id;
            // cam1: RenderRaceCars 0x199A20 lists a race car when +0x1914 != 0, or +0x27C0 != the view (0), or the
            // view's IsPlayerVisible (captures before corona1 lack +0x27C0: read as 0, the view's own car)
            v.own_hidden = im.u32(v.id + 0x1914) == 0 && im.u32(v.id + 0x27C0) == 0 && !t.player_visible;
            s.cast = im.u32(v.id + 0x1914) != 0 || t.player_visible;
            // split2: RenderRaceCars(1) / RenderShadows(1) test player 2's camera manager [L drafts: D_004EE040[view]]
            v.own_hidden2 = im.u32(v.id + 0x1914) == 0 && im.u32(v.id + 0x27C0) == 1 && !t.view2.player_visible;
            s.cast2 = im.u32(v.id + 0x1914) != 0 || t.view2.player_visible;
            float g[4];
            if (!im.read(v.id + 0x510, g, 16)) s.cast = false;
            std::memcpy(s.normal, g, 12);
            s.dist = g[3];
        } else if (v.kind == VehicleKind::kTraffic) {
            ep = v.id + 0x70;
            s.cast = listed.count(v.id) && (im.u8(v.id + 0x174) & 4) == 0 && (im.u8(v.id + 0x17A) & 2) == 0 && im.u8(v.id + 0xCB) != 0;
            // split2: RenderShadows(1) walks player 2's list with view 2's results (the poll's); view 1 then needs Draw(0)'s
            s.cast2 = v.listed2 && (im.u8(v.id + 0x174) & 4) == 0 && (im.u8(v.id + 0x17A) & 2) == 0 && im.u8(v.id + 0xCB) != 0;
            if (t.split && v.v1_known) s.cast = listed.count(v.id) && (im.u8(v.id + 0x174) & 4) == 0 && (v.v1_flags2 & 2) == 0 && v.v1_frustum != 0;
            im.read(v.id + 0x80, s.normal, 12);
            const std::uint32_t q = im.u32(tr + 0x343F0 + 4 * std::uint32_t(im.u8(v.id + 0x176)));
            if (!ram(q) || !im.has(q + 0x18, 4)) s.cast = false;
            else s.dist = im.f32(q + 0x18);
        } else {
            ep = v.id + 0x1360;
            s.cast = im.u8(v.id + 0x13BB) != 0;
            s.cast2 = s.cast;   // split2: +0x13BB is the last Draw's frustum result (view 2's); view 1's from the copy
            if (t.split && v.v1_known) s.cast = v.v1_frustum != 0;
            float g[4];
            if (!im.read(v.id + 0x1870, g, 16)) s.cast = false;
            std::memcpy(s.normal, g, 12);
            s.dist = g[3];
        }
        if (!im.mat(ep, s.ess) || !im.has(ep + 0x60, 4) || !shadow_geometry(im, im.u32(ep + 0x40), s)) s.cast = s.cast2 = false;
        else s.darkness = im.f32(ep + 0x60);
    }
    // skid marks
    im.read(0x1E865E0, t.trail_vu, sizeof t.trail_vu);
    im.read(kGm + 0x470 + 0x120, t.trail_cam, sizeof t.trail_cam);
    for (int k = 0; k < 5; ++k) {
        const std::uint32_t pa = 0x49FFF0 + 0x50 * std::uint32_t(k);
        char name[17] = {};
        if (im.read(pa, name, 16)) cap.trail_texture[k] = name;
        cap.trail_alpha[k] = im.u32(pa + 0x20);
        t.trail_alpha[k] = cap.trail_alpha[k];
        std::memcpy(t.trail_name[k], name, 16);
    }
    const std::uint32_t ts = 0x1D9D200 + 0x2600;
    for (int ty = 0; ty < 5; ++ty) {
        const std::uint32_t list = ts + 0x13C0C + 0x208 * std::uint32_t(ty), n = im.u32(list + 0x200);
        for (std::uint32_t j = 0; j < n && j < 0x80; ++j) {
            // the data the emitter's +0x8 points at when Render runs: half (+0x14634) x 0x9800 + its slot x 0x130 (the
            // hook skips the per-frame pointer swaps; scene_capture.cpp)
            const std::uint32_t e = im.u32(list + 4 * j), slot = (e - (ts + 0x13000)) / 0x14;
            const std::uint32_t d = ts + (im.u32(ts + 0x14634) & 1) * 0x9800 + slot * 0x130;
            if (e < ts + 0x13000 || slot >= 0x80) continue;
            if (!ram(e) || !ram(d) || !im.has(e, 0x14) || !im.has(d, 0x130)) continue;
            TrailEmitter te;
            te.id = e;
            te.type = ty;
            te.count = std::int8_t(im.u8(e + 0x10));
            te.flags = im.u8(e + 0x12);
            te.last = im.f32(e);
            im.read(d, te.pt, sizeof te.pt);
            im.read(d + 0x100, te.dir, sizeof te.dir);
            auto& last = cap.trail_last[e];
            if (!last || last->type != te.type || last->count != te.count || last->flags != te.flags || last->last != te.last ||
                std::memcmp(last->pt, te.pt, sizeof te.pt) != 0 || std::memcmp(last->dir, te.dir, sizeof te.dir) != 0)
                last = std::make_shared<const TrailEmitter>(te);
            t.trails.push_back(last);
        }
    }
}

// Coronas (corona1, docs/native-renderer.md §16): the inputs scene_capture.cpp grab_coronas lists. Needs game_draws and
// decode_effects (IsPlayerVisible) first.
std::shared_ptr<const CoronaTables> corona_tables(const Image& im, std::uint32_t vd, Capture& cap) {
    if (!ram(vd) || !im.has(vd + 0x1660, 0x60)) return nullptr;
    CoronaTables c;
    auto light = [&](std::uint32_t a, CoronaTables::Light& l) { im.read(a, l.pos, 12); im.read(a + 0x10, l.dir, 12); };
    for (std::uint32_t k = 0; k < 7; ++k) {
        const std::uint32_t t = im.u32(vd + 0x1664 + 4 * k), n = im.u8(vd + 0x16AC + k);
        if (n == 0 || !ram(t) || !im.has(t, 0x30 * n)) continue;
        c.group[k].resize(n);
        for (std::uint32_t i = 0; i < n; ++i) light(t + 0x30 * i, c.group[k][i]);
    }
    const std::uint32_t t = im.u32(vd + 0x1684), n = im.u8(vd + 0x16B4);
    if (n > 0 && ram(t) && im.has(t, 0x30 * n)) {
        c.exhaust.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            auto& e = c.exhaust[i];
            const std::uint32_t a = t + 0x30 * i;
            im.read(a, e.pos, 12);
            im.read(a + 0x10, e.dir, 12);
            e.u = im.f32(a + 0x24);
            e.v = im.f32(a + 0x28);
            e.a = im.u8(a + 0x2C);
            e.b = im.u8(a + 0x2D);
        }
    }
    auto& last = cap.corona_last[vd];
    if (!last || !(*last == c)) last = std::make_shared<const CoronaTables>(std::move(c));
    return last;
}

void decode_coronas(const Image& im, Tick& t, Capture& cap) {
    constexpr std::uint32_t kFx = 0x1D9D200, kTs = kWorld + 0x4DE0;
    if (!im.has(kFx + 0x1F810, 0x20) || !im.has(0x1D9D190, 0x10)) return;
    CoronaInputs& c = t.coronas;
    c.valid = true;
    im.read(kFx + 0x1F810, c.cam_point, 12);
    c.seed = im.u32(kFx + 0x1F820);
    im.read(0x1D9D190, c.scales, 16);
    c.cars_on = im.u8(kGm + 0x66) != 0;   // D_00665F26
    if (!cap.corona_k && im.has(0x4BBFE0, 0x60) && im.has(0x4C4980, 0x50) && im.has(0x4C4A10, 0x20) && im.has(0x4E0F40, 0x20)) {
        auto kp = std::make_shared<CoronaConsts>();
        CoronaConsts& k = *kp;
        k.valid = true;
        for (std::uint32_t i = 0; i < 6; ++i) im.read(0x4BBFE0 + 0x10 * i, k.light[i], 12);
        for (std::uint32_t i = 0; i < 5; ++i) im.read(0x4C4980 + 0x10 * i, k.tl[i], 12);
        for (std::uint32_t i = 0; i < 2; ++i) im.read(0x4C4A10 + 0x10 * i, k.boost[i], 12);
        k.boost_size[0] = im.f32(0x4E0E94);
        k.boost_size[1] = im.f32(0x4E09E4);
        k.boost_min = im.f32(0x4E0C6C);
        for (std::uint32_t i = 0; i < 5; ++i) k.boost_k[i] = im.f32(0x4E0F44 + 4 * i);
        for (std::uint32_t i = 0; i < 3; ++i) {
            k.capacity[i] = int(im.u32(0x4BC900 + 8 * i));
            char name[17] = {};
            if (const std::uint32_t a = im.u32(0x4BC904 + 8 * i); ram(a) || (a >= 0x4BC8D0 && a < 0x4BC900)) im.read(a, name, 16);
            k.texture[i] = name;
        }
        cap.corona_k = kp;
    }
    c.k = cap.corona_k;
    // vehicles: RenderRaceCars 0x199A20 [C nonmatching draft, calls and offsets from the asm]: listed when +0x1914, or
    // not the view's own car (+0x27C0 != 0), or IsPlayerVisible; then the unit test (+0x5A) and the frustum result (+0x5B,
    // or +0x10A2); CB3RaceCar::Draw 0x151A40 [C matched] calls ActuallyRenderRaceCarImmediateEffects when +0x18EA == 0
    auto unit_seen = [&](int u) { return u >= 0 && u < 32 && (t.safe_units >> u & 1u) && (t.unit_visible >> u & 1u); };
    const std::uint32_t td = im.u32(kTs);
    c.traffic_on = ram(td) && im.u8(kGm + 0x65) != 0;   // CB3TrafficSystem::Draw returns unless both (D_00665F25)
    for (auto& v : t.vehicles) {
        auto& o = v.corona;
        const std::uint32_t ep = v.kind == VehicleKind::kRaceCar ? v.id : v.kind == VehicleKind::kTraffic ? v.id + 0x70 : v.id + 0x1360;
        o.lights = im.u8(ep + 0x58);
        v.corona_tables = corona_tables(im, v.data, cap);
        if (v.kind != VehicleKind::kRaceCar) {
            o.valid = true;
            o.submit = c.traffic_on && v.cull == Vehicle::kDrawn;
            o.submit2 = c.traffic_on && v.cull2 == Vehicle::kDrawn;   // split2: Draw(1)'s choice
            continue;
        }
        if (!im.has(v.id + 0x118C, 0x20) || !im.has(v.id + 0x580, 0x200) || !im.has(v.id + 0x27C0, 4)) continue;
        o.valid = true;
        o.rear_red = im.u8(v.id + 0x18F0) != 0;
        o.red_boost = im.u8(v.id + 0x18F1) != 0;
        o.boost = im.f32(v.id + 0x118C + 0x14);
        for (std::uint32_t z = 0; z < 8; ++z) im.mat(v.id + 0x580 + 0x40 * z, o.zone[z]);
        const bool listed = im.u32(v.id + 0x1914) != 0 || im.u32(v.id + 0x27C0) != 0 || t.player_visible;
        const bool seen = unit_seen(std::int8_t(im.u8(v.id + 0x5A)));
        const bool inside = im.u8(v.id + 0x5B) != 0 || im.u8(v.id + 0x10A2) != 0;
        o.submit = c.cars_on && listed && seen && inside && im.u8(v.id + 0x18EA) == 0;
        float s = 0;
        for (int a = 0; a < 3; ++a) s += (v.main.r[3][a] - t.cam.lod_pos[a]) * (v.main.r[3][a] - t.cam.lod_pos[a]);
        o.sort = s;
        if (t.split) {   // split2 (§29.6): RenderRaceCars(1, 1) decides view 2 with the poll's +0x5A / +0x5B / +0x10A2
            const bool listed2 = im.u32(v.id + 0x1914) != 0 || im.u32(v.id + 0x27C0) != 1 || t.view2.player_visible;
            const std::int8_t u2 = std::int8_t(im.u8(v.id + 0x5A));
            const bool seen2 = u2 >= 0 && u2 < 32 && (t.view2.safe_units >> u2 & 1u) && (t.view2.unit_visible >> u2 & 1u);
            o.submit2 = c.cars_on && listed2 && seen2 && inside && im.u8(v.id + 0x18EA) == 0;
            float s2 = 0;
            for (int a = 0; a < 3; ++a) s2 += (v.main.r[3][a] - t.view2.cam.lod_pos[a]) * (v.main.r[3][a] - t.view2.cam.lod_pos[a]);
            o.sort2 = s2;
            if (v.v1_known)   // and view 1 with RenderRaceCars(0, 0)'s, copied at Render(1)'s entry
                o.submit = c.cars_on && listed && unit_seen(v.v1_unit) && (v.v1_frustum != 0 || v.v1_extra != 0) && im.u8(v.id + 0x18EA) == 0;
        }
    }
    static const bool trace = std::getenv("B3_SPLIT_TRACE") != nullptr;
    if (trace && t.split) {   // split2 diagnostics: the vehicles whose lights each view submits
        int lit = 0, s1 = 0, s2 = 0, both = 0;
        for (const Vehicle& v : t.vehicles) {
            if (v.corona.lights == 0) continue;
            ++lit;
            s1 += v.corona.submit;
            s2 += v.corona.submit2;
            both += v.corona.submit && v.corona.submit2;
        }
        std::fprintf(stderr, "split trace: poll %u coronas: %d vehicles with lights, submitted in view 1 %d, view 2 %d, both %d\n",
                     t.poll, lit, s1, s2, both);
    }
    // traffic lights (CB3TrafficSystem::Draw 0x3078F0, after the vehicles' immediate effects [L draft; offsets C asm])
    if (c.traffic_on && im.has(td + 0x38, 4) && im.has(td + 0xAC, 8)) {
        const std::uint32_t nl = im.u32(td + 0x38), nc = im.u32(td + 0xB0), co = im.u32(td + 0xAC);
        if (nc > 0 && nc < 0x1000 && ram(co) && im.has(co, 0x20 * nc)) {
            std::vector<CoronaTables::Light> l(nc);
            for (std::uint32_t i = 0; i < nc; ++i) { im.read(co + 0x20 * i, l[i].pos, 12); im.read(co + 0x20 * i + 0x10, l[i].dir, 12); }
            if (!cap.tl_last || !(*cap.tl_last == l)) cap.tl_last = std::make_shared<const std::vector<CoronaTables::Light>>(std::move(l));
            c.tl_coronas = cap.tl_last;
            for (std::uint32_t w = 0; w < (nc + 30) / 31; ++w) c.assigned.push_back(im.u32(kTs + 0x36320 + 4 * w));
        }
        for (std::uint32_t j = 0; j < nl && j <= 0xA9 && im.has(kTs + 0x117CC + 0x18 * j, 0x18); ++j) {
            CoronaInputs::TrafficLight L;
            L.state = im.u8(kTs + 0x117CC + 0x18 * j + 0x14);
            const std::uint32_t lt = im.u32(kTs + 0x117CC + 0x18 * j);
            if (ram(lt) && im.has(lt + 0x10, 8)) {
                L.count = im.u8(lt + 0x14);
                for (std::uint32_t q = 0; q < 4; ++q) L.idx[q] = im.u8(lt + 0x10 + q);
            }
            c.tlights.push_back(L);
        }
        c.tl_flat = im.u8(kTs + 0x363BA) != 0;
    }
    if (std::uint8_t rec[0x230]; im.read(kGm + 0x470, rec, sizeof rec)) { c.vu_cam = b3asset::corona_camera(rec); c.vu_cam_valid = true; }
    // the check capture: the slots the game wrote for player 0
    for (std::uint32_t a = 0; a < 3; ++a) {
        const std::uint32_t arr = kFx + 0x0C + 0x34 * a;
        if (!im.has(arr, 0x34)) continue;
        const std::uint32_t half = im.u8(arr + 0x2E) & 1, n = im.u16(arr + 0x2A);
        const std::uint32_t buf = im.u32(arr + 0x10 + 4 * half) & ~0x30000000u;
        c.check = true;
        c.half[a] = int(half);
        if (n == 0) continue;
        if (!ram(buf) || !im.has(buf, 0x20 * n)) { c.check = false; continue; }
        c.game[a].resize(n);
        for (std::uint32_t i = 0; i < n; ++i) { im.read(buf + 0x20 * i, c.game[a][i].pos, 16); im.read(buf + 0x20 * i + 0x10, c.game[a][i].col, 16); }
    }
}

}  // namespace

int game_lod(const Camera& cam, const Vehicle& v) {
    float d2 = 0;
    for (int k = 0; k < 3; ++k) d2 += (v.main.r[3][k] - cam.lod_pos[k]) * (v.main.r[3][k] - cam.lod_pos[k]);
    auto beyond = [&](int i) { const float r = cam.lod_dist[i] * cam.lod_scale; return d2 > r * r; };
    if (v.kind == VehicleKind::kRaceCar) return beyond(2) ? 0 : beyond(1) ? 1 : beyond(0) ? 2 : 3;
    return beyond(4) ? 0 : beyond(3) ? 1 : 2;
}

// One view's decision: view 0 from Tick's inputs into Vehicle::cull; split1: view 1 (player 2's, Draw(1)) from
// View2In's into Vehicle::cull2. The traffic's unit (+0xCA), frustum (+0x5B) and camera-inside (+0x17A) results are
// the last Draw's, which in a split frame is Draw(1) (Render(1) comes after Render(0)), so view 1's use them as they are;
// split2: view 0 then uses Draw(0)'s, copied at Render(1)'s entry (Vehicle::v1_*), when the capture has them.
static void game_draws_view(Tick& t, int view) {
    const bool v2 = view == 1;
    const float* c = v2 ? t.view2.cam.lod_pos : t.cam.lod_pos;
    const std::uint32_t safe_units = v2 ? t.view2.safe_units : t.safe_units, unit_visible = v2 ? t.view2.unit_visible : t.unit_visible;
    const bool lists = v2 ? t.view2.traffic_lists : t.traffic_lists;
    auto d2 = [&](const Vehicle& v) {
        float s = 0;
        for (int k = 0; k < 3; ++k) s += (v.cull_pos[k] - c[k]) * (v.cull_pos[k] - c[k]);
        return s;
    };
    constexpr float kMax = 75625.0f;   // 275 m squared (0x12769 converted, Draw 0x307A94; c.le.s 0x307B14)
    auto unit_seen = [&](int u) { return u >= 0 && u < 32 && (safe_units >> u & 1u) && (unit_visible >> u & 1u); };
    std::vector<std::pair<float, Vehicle*>> crashing, others, crashed;
    for (auto& v : t.vehicles) {
        Vehicle::Cull& cull = v2 ? v.cull2 : v.cull;
        cull = Vehicle::kDrawn;
        if (v.kind == VehicleKind::kRaceCar) continue;
        const float d = d2(v);
        // split2 (§29.6): view 1 of a split frame takes Draw(0)'s own results when the capture copied them
        const bool own = !v2 && v.v1_known;
        const int unit = own ? v.v1_unit : v.unit, frustum = own ? v.v1_frustum : v.frustum;
        const std::uint8_t flags2 = own ? v.v1_flags2 : v.flags2;
        if (v.kind == VehicleKind::kTraffic) {
            const bool crash = (v.flags & 4) != 0;
            if (!(d <= kMax) && !crash) { cull = Vehicle::kFar; continue; }
            if (lists && !(v2 ? v.listed2 : v.listed)) { cull = Vehicle::kNotListed; continue; }
            if (!crash && !unit_seen(unit)) { cull = Vehicle::kUnitHidden; continue; }
            if (frustum == 0) { cull = Vehicle::kOutside; continue; }
            if (flags2 & 2) { cull = Vehicle::kCameraInside; continue; }
            (crash ? crashing : others).push_back({d, &v});
        } else {
            if (!(d <= kMax)) { cull = Vehicle::kFar; continue; }
            if (!unit_seen(unit)) { cull = Vehicle::kUnitHidden; continue; }
            if (frustum == 0 && !v.lifted) { cull = Vehicle::kOutside; continue; }
            crashed.push_back({d, &v});
        }
    }
    // the caps after qsort by distance (nearest first): 8 crashing, 32 - those others, 32 crashed [C asm 0x3080CC..0x3080F8; comparator 0x309070 ascending]
    auto cap = [v2](std::vector<std::pair<float, Vehicle*>>& l, std::size_t n) {
        std::stable_sort(l.begin(), l.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t i = n; i < l.size(); ++i) (v2 ? l[i].second->cull2 : l[i].second->cull) = Vehicle::kOverCap;
        if (l.size() > n) l.resize(n);
    };
    cap(crashing, 8);
    cap(others, 32 - crashing.size());
    cap(crashed, 32);
}

void game_draws(Tick& t) {
    game_draws_view(t, 0);
    if (t.split && t.view2.valid && t.view2.track_valid) game_draws_view(t, 1);
    static const bool trace = std::getenv("B3_SPLIT_TRACE") != nullptr;
    if (trace && t.split) {   // split2 diagnostics: view 1's copy against the poll's (view 2's) results, and each view's choice
        int known = 0, differ = 0, drawn[2] = {0, 0}, traffic = 0;
        for (const Vehicle& v : t.vehicles) {
            known += v.v1_known;
            if (v.v1_known && (v.v1_unit != v.unit || v.v1_frustum != v.frustum || (v.kind == VehicleKind::kTraffic && v.v1_flags2 != v.flags2))) ++differ;
            if (v.kind == VehicleKind::kRaceCar) continue;
            ++traffic;
            drawn[0] += v.cull == Vehicle::kDrawn;
            drawn[1] += v.cull2 == Vehicle::kDrawn;
        }
        std::fprintf(stderr, "split trace: poll %u view-1 copy %d (%d vehicles, %d differ from the poll's); traffic / crashed %d, drawn %d / %d\n",
                     t.poll, int(t.view1_snap), known, differ, traffic, drawn[0], drawn[1]);
    }
}

bool Capture::load(const std::string& path, std::string* why, bool fill_gaps, std::uint32_t poll_from, std::uint32_t poll_to) {
    // zlib reads gzip-compressed captures (data/scene's) and plain ones alike (hud1)
    gzFile f = gzopen(path.c_str(), "rb");
    if (!f) { if (why) *why = "cannot read " + path; return false; }
    gzbuffer(f, 1 << 20);
    auto rd = [&](void* p, unsigned n) { return gzread(f, p, n) == int(n); };
    char magic[8];
    std::uint32_t head[2];
    if (!rd(magic, 8) || std::memcmp(magic, "B3SCENE", 8) != 0 || !rd(head, 8) || head[0] < 1 || head[0] > 3) {
        gzclose(f);
        if (why) *why = path + ": not a version 1, 2 or 3 scene capture";
        return false;
    }
    version = head[0];   // 2 adds the prop ranges, 3 the effects; the records are the same
    Image im;
    std::vector<std::uint8_t> buf;
    std::uint32_t rec[4];
    while (rd(rec, 16)) {
        if (rec[0] != 0x4B434954) { if (why) *why = "bad record tag"; gzclose(f); return false; }
        for (std::uint32_t r = 0; r < rec[3]; ++r) {
            std::uint32_t run[2];
            if (!rd(run, 8) || run[1] % 16 != 0 || run[1] > (1u << 24)) { if (why) *why = "truncated run"; gzclose(f); return false; }
            buf.resize(run[1]);
            if (!rd(buf.data(), run[1])) break;   // a killed run's tail: stop at the last whole record
            im.write(run[0], buf.data(), run[1]);
            ++runs;
            bytes += run[1];
        }
        if (rec[1] < poll_from) continue;
        if (rec[1] > poll_to) break;
        Tick t;
        t.poll = rec[1];
        t.vblank = rec[2];
        decode(im, t);   // as the live feed decodes (file versions 1..3: props from 2, effects from 3)
        ticks.push_back(std::move(t));
    }
    gzclose(f);
    // race segments: consecutive ticks on one route with the track on, a camera and at least one race car
    const auto race = race_tick;
    for (std::size_t i = 0; i < ticks.size();) {
        if (!race(ticks[i])) { ++i; continue; }
        Segment s{i, i, ticks[i].route_size, ticks[i].route_units};
        // gaps of up to a second (a pause, a camera cut) stay in the segment; the gap ticks hold the last race state
        for (std::size_t j = s.last + 1; j < ticks.size() && j <= s.last + 60; ++j)
            if (race(ticks[j]) && ticks[j].route_size == s.route_size) s.last = j;
        for (std::size_t j = s.first; j <= s.last && fill_gaps; ++j)
            if (!race(ticks[j])) { const auto poll = ticks[j].poll, vb = ticks[j].vblank; ticks[j] = ticks[j - 1]; ticks[j].poll = poll; ticks[j].vblank = vb; }
        segments.push_back(s);
        i = s.last + 1;
    }
    if (ticks.empty()) { if (why) *why = path + ": no records"; return false; }
    fe_segments = frontend_segments(ticks, segments);
    return true;
}

std::vector<Segment> frontend_segments(const std::vector<Tick>& ticks, const std::vector<Segment>& race) {
    // menu1: every run of ticks outside the race segments in which the capture holds a HUD frame (a 2D pass was drawn
    // by then); runs shorter than kMinFrontEndTicks (a race's own edge, a one-off frame) are left out
    std::vector<Segment> out;
    std::vector<char> in_race(ticks.size(), 0);
    for (const auto& s : race) for (std::size_t j = s.first; j <= s.last && j < ticks.size(); ++j) in_race[j] = 1;
    for (std::size_t i = 0; i < ticks.size();) {
        if (in_race[i] || !ticks[i].hud) { ++i; continue; }
        std::size_t j = i;
        while (j + 1 < ticks.size() && !in_race[j + 1] && ticks[j + 1].hud) ++j;
        if (j - i + 1 >= kMinFrontEndTicks) out.push_back(Segment{i, j, 0, 0});
        i = j + 1;
    }
    return out;
}

std::string route_by_header(const std::string& game, const Tick& t) {
    if (!t.route_head_ok || t.route_size == 0) return {};
    struct Head { std::string path; std::uint32_t size; std::array<std::uint8_t, 0x60> b; };
    static std::map<std::string, std::vector<Head>> cache;   // per install: every route's STATIC.DAT size and header
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    auto it = cache.find(game);
    if (it == cache.end()) {
        std::vector<Head> heads;
        // iso1: the disc folder or the disc image (b3asset/discfs.h)
        for (const auto& area : b3asset::disc::list_dir(std::filesystem::path(game) / "TRACKS")) {
            if (!area.dir) continue;
            for (const auto& route : b3asset::disc::list_dir(area.path)) {
                b3asset::disc::File f;
                if (!f.open(route.path / "STATIC.DAT")) continue;
                Head h{route.path.string(), std::uint32_t(f.size()), {}};
                if (f.read_at(0, h.b.data(), 0x60) == 0x60) heads.push_back(std::move(h));
            }
        }
        std::sort(heads.begin(), heads.end(), [](const Head& a, const Head& b) { return a.path < b.path; });
        it = cache.emplace(game, std::move(heads)).first;
    }
    std::string found;
    for (const Head& h : it->second) {
        if (h.size != t.route_size) continue;
        bool same = true;
        for (std::size_t k = 0; k < 0x60 && same; k += 4) {
            std::uint32_t a, b;
            std::memcpy(&a, t.route_head.data() + k, 4);
            std::memcpy(&b, h.b.data() + k, 4);
            same = a == b || a == b + t.route_base;
        }
        if (!same) continue;
        if (!found.empty()) return {};   // not unique
        found = h.path;
    }
    return found;
}

bool race_tick(const Tick& t) {
    if (!t.track_on || !t.cam.valid || t.route_size == 0 || t.current_unit < 0) return false;
    for (const auto& v : t.vehicles) if (v.kind == VehicleKind::kRaceCar) return true;
    return false;
}

// version 0 = the live feed (b3replay --live): the writer is this build's, which always holds the prop and effect ranges
// Animated track materials (anim1; scene_capture.cpp's block after crash2's): present when the update list and every
// listed material's [0x10, 0x20) are in the image.
static void decode_anim(const Image& im, Tick& t) {
    const std::uint32_t g = im.u32(0x64C994);
    if (!ram(g) || !im.has(g, 0x60)) return;
    const std::uint32_t mats = im.u32(g + 0x8), list = im.u32(g + 0x10), nmat = im.u16(g + 0xC), n = im.u16(g + 0xE);
    if (n == 0 || n >= 0x100 || !ram(list) || !im.has(list, 2 * n)) return;
    std::vector<AnimMaterial> out;
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint32_t i = im.u16(list + 2 * k), m = mats + 0x28 * i;
        if (i >= nmat || !ram(m) || !im.has(m + 0x10, 0x10)) return;
        AnimMaterial a;
        a.material = int(i);
        a.cur = std::int8_t(im.u8(m + 0x10));
        a.p12 = std::int8_t(im.u8(m + 0x12));
        a.p13 = std::int8_t(im.u8(m + 0x13));
        a.period = im.f32(m + 0x18);
        a.t = im.f32(m + 0x1C);
        out.push_back(a);
    }
    t.anim_valid = true;
    t.anim = std::move(out);
}

// Contact effects (fx2, docs/native-renderer.md §19): scrapes, sparks and popcorn, as scene_capture.cpp
// grab_contact_effects captures them; each part only when its bytes are in the image.
template <class T>
std::shared_ptr<const T> share_last(std::shared_ptr<const T>& last, T&& v) {
    if (!last || !(*last == v)) last = std::make_shared<const T>(std::move(v));
    return last;
}

void decode_contact(const Image& im, Tick& t, Capture& cap) {
    constexpr std::uint32_t kFx = 0x1D9D200, kScrape = kFx + 0x16C80;
    auto strip = [](std::uint32_t a) { return a & ~0x30000000u; };
    ContactFx& c = t.contact;
    if (im.has(kScrape + 0x8A80, 0x90) && im.has(0x1EA4910, 0x50) && im.has(0x4C8FF0, 0x20)) {
        c.scrapes_valid = true;
        c.scrape_gate = im.has(kWorld + 0x12AD50 + 0x18E0, 0x10) && im.u8(kWorld + 0x12AD50 + 0x18EA) != 0;
        im.read(0x1EA4910, c.scrape_params, sizeof c.scrape_params);
        im.read(0x4C8FF0, c.scrape_uvs, sizeof c.scrape_uvs);
        const std::uint32_t n = im.u32(kScrape + 0x8B0C);
        for (std::uint32_t j = 0; j < n && j < 32; ++j) {
            const std::uint32_t e = strip(im.u32(kScrape + 0x8A8C + 4 * j));
            if (!ram(e) || !im.has(e, 0x20) || !im.has(e + 0x440, 8)) continue;
            const std::uint32_t drawn = strip(im.u32(e + 0x444)), other = strip(im.u32(e + 0x440));
            if (!ram(drawn) || !im.has(drawn, 0x10)) continue;
            ScrapeEmitter s;
            s.id = e;
            s.uv = int(im.u32(e) & 3);
            s.flags = im.u16(e + 0x10);
            s.count = int(std::min<std::uint32_t>(im.u32(drawn), 16));
            s.pending = ram(other) && im.has(other, 0x10) ? int(std::min<std::uint32_t>(im.u32(other), 16)) : 0;
            if (s.count > 0 && !im.read(drawn + 0x10, s.seg, 0x20 * std::uint32_t(s.count))) continue;
            auto& last = cap.scrape_last[e];
            c.scrapes.push_back(share_last(last, std::move(s)));
        }
    }
    if (im.has(0x4E2A00, 0x10) && im.has(0x1E31A30, 0x60) && im.has(kFx + 0x21F0, 6 * 0xA8)) {
        c.sparks_valid = true;
        im.read(0x4E2A00, c.spark_statics, sizeof c.spark_statics);
        im.read(0x1E31A20, c.spark_d1e31a20, sizeof c.spark_d1e31a20);
        c.cam_index = im.u32(0x667050);
        bool live = false;
        const float rnow = im.f32(0x1E31A30 + 0x50);   // the last render's time (the hook's test uses it too)
        for (int a = 0; a < 6; ++a) {
            const std::uint32_t arr = kFx + 0x21F0 + 0xA8 * std::uint32_t(a);
            SparkArrayState s;
            im.read(arr, s.head, sizeof s.head);
            const std::uint32_t prm = strip(im.u32(arr + 0x98));
            if (ram(prm)) im.read(prm, s.params, sizeof s.params);
            for (int b = 0; b < 2; ++b) {
                const std::uint32_t bk = arr + 0x4C * std::uint32_t(b), alloc = strip(im.u32(bk + 0x48)), base0 = strip(im.u32(bk));
                if (ram(alloc) && base0 > alloc && base0 - alloc <= 0x1000 && im.has(alloc, base0 - alloc)) {
                    s.dma[b].resize(base0 - alloc);
                    im.read(alloc, s.dma[b].data(), base0 - alloc);
                }
                for (int k = 0; k < 4; ++k) {
                    const std::uint32_t base = strip(im.u32(bk + 0x10 * std::uint32_t(k))), capy = im.u32(bk + 0x10 * std::uint32_t(k) + 4);
                    if (capy == 0 || capy > 0x1000 || !ram(base) || !im.has(base, capy / 4 * 0x90)) continue;
                    s.rec[b][k].resize(capy / 4 * 0x90);
                    im.read(base, s.rec[b][k].data(), capy / 4 * 0x90);
                }
            }
            // RenderSparkArray 0x2637F0's test, as the hook evaluates it
            float life = 0, t44, t90;
            for (int k = 0; k < 4; ++k) { float f; std::memcpy(&f, s.params + 0x40 + 4 * k, 4); life = std::max(life, f); }
            std::memcpy(&t44, s.head + 0x44, 4);
            std::memcpy(&t90, s.head + 0x90, 4);
            if (ram(prm) && (t44 > rnow - life || t90 > rnow - life)) live = true;
            c.sparks[a] = share_last(cap.spark_last[a], std::move(s));
        }
        // the whole history is in the image once captured, but the hook takes it only on ticks with a renderable spark
        // array (the same test), the newest record on the others (scene_capture.cpp): so it is current only then
        SparkHistory h{};
        c.history_valid = live && im.read(0x1E31A30, h.data(), std::uint32_t(h.size()));
        if (!c.history_valid) im.read(0x1E31A30, h.data(), 0x60);
        c.history = share_last(cap.history_last, std::move(h));
    }
    if (im.has(0x4A4DB0, 4 * 0x50) && im.has(kFx + 0x20F0, 4 * 0x40)) {
        c.popcorn_valid = true;
        im.read(0x4A4DB0, c.popcorn_params, sizeof c.popcorn_params);
        for (int a = 0; a < 4; ++a) {
            const std::uint32_t arr = kFx + 0x20F0 + 0x40 * std::uint32_t(a);
            PopcornArrayState s;
            im.read(arr, s.head, sizeof s.head);
            const std::uint32_t base = strip(im.u32(arr + 0x1C)), capy = im.u32(arr + 0x24);
            if (capy > 0 && capy <= 0x1000 && ram(base) && im.has(base, (capy + 3) / 4 * 0x90)) {
                s.rec.resize((capy + 3) / 4 * 0x90);
                im.read(base, s.rec.data(), std::uint32_t(s.rec.size()));
            }
            c.popcorn[a] = share_last(cap.popcorn_last[a], std::move(s));
        }
    }
    // particles (effects3): the arrays, their banks' records, the program, the camera record
    if (im.has(0x49C570, 24 * 0x80) && im.has(kFx + 0x170, 24 * 0x150) && im.has(0x46D750, 0x1980) && im.has(0x4E29E0, 4)) {
        c.particles_valid = true;
        c.particle_skip = im.u8(0x4E29E4) != 0;
        {
            std::vector<std::uint8_t> prog(0x1980);
            im.read(0x46D750, prog.data(), 0x1980);
            c.particle_prog = share_last(cap.particle_prog_last, std::move(prog));
        }
        {
            const std::uint32_t idx = im.has(0x667050, 4) ? im.u32(0x667050) : 0;
            std::array<std::uint8_t, 0x230> cam{};
            if (idx < 4 && im.read(kGm + 0x470 + 0x230 * idx, cam.data(), 0x230)) c.particle_cam = share_last(cap.particle_cam_last, std::move(cam));
        }
        for (int a = 0; a < 24; ++a) {
            const std::uint32_t arr = kFx + 0x170 + 0x150 * std::uint32_t(a);
            ParticleArrayState s;
            im.read(arr, s.head, sizeof s.head);
            const std::uint32_t prm = strip(im.u32(arr + 0xE4));
            if (ram(prm) && im.has(prm, 0x80)) {
                im.read(prm, s.params, sizeof s.params);
                const std::uint32_t nm = strip(im.u32(prm));
                if (ram(nm) && im.has(nm, 16)) { im.read(nm, s.tex, 15); s.tex[15] = 0; }
            }
            for (int b = 0; b < 2; ++b) {
                const std::uint32_t bank = arr + (b == 0 ? 0xE8u : 0x114u), rec = strip(im.u32(bank + 8)), capy = im.u32(bank + 0x14);
                const std::uint32_t bytes = capy * 28 + (capy + 59) / 60 * 4;
                if (capy > 0 && capy <= 0x400 && ram(rec) && im.has(rec, bytes)) {
                    s.rec[b].resize(bytes);
                    im.read(rec, s.rec[b].data(), bytes);
                }
            }
            c.particles[a] = share_last(cap.particle_last[a], std::move(s));
        }
    }
}

// The post passes (post1, docs/native-renderer.md §25): CB3Bloom's params, CB3DirectionalBlurBase's state and
// UpdateCamera 0x4256C0's decision for view idx 0 (the single-player view) [L UpdateCamera draft, C its asm loads;
// C func_0030A990 matched; C CB3World::Render 0x276300 matched]. The params and the state are in every capture; the
// decision needs post1's inputs (detected by presence).
// split2 (§29.6): view idx 1 (Render(1)) from viewport 2's params (+0x270: bloom +0x2C0, directional state +0x2D0),
// D_005179C0 + 0xBB0 and player 2's block, into View2In::post.
void decode_post(const Image& im, Tick& t, int idx = 0) {
    PostIn& p = idx == 0 ? t.post : t.view2.post;
    const std::uint32_t gp = kGm + 0xA0 * std::uint32_t(idx), fl = 0x5179C0 + 0xBB0 * std::uint32_t(idx);
    if (!im.has(gp + 0x220, 0x40)) return;
    p.valid = true;
    p.bloom_on = im.u8(kGm + 0x68) != 0;
    p.bloom_threshold = im.f32(gp + 0x220);
    p.bloom_intensity = im.f32(gp + 0x224);
    p.bloom_feedback = im.f32(gp + 0x228);
    p.bloom_freeze = im.has(0x51BA90, 8) && im.u32(0x51BA94) != 0xFFFFFFFFu;
    p.dir_on = im.u8(kGm + 0x6A) != 0;
    im.read(gp + 0x230, p.dir_point, 12);
    p.dir_centre[0] = im.f32(gp + 0x240);
    p.dir_centre[1] = im.f32(gp + 0x244);
    p.dir_zoom[0] = im.f32(gp + 0x248);
    p.dir_zoom[1] = im.f32(gp + 0x24C);
    p.dir_amount = im.f32(gp + 0x250);
    p.dir_time = im.f32(gp + 0x254);
    // view idx's player block (UpdateCamera's D_00522660 + idx x 0x27D0)
    const std::uint32_t kPl = kWorld + 0x12AD50 + 0x27D0 * std::uint32_t(idx);
    if (!im.has(0x4F50A0, 0x10) || !im.has(fl, 0x30) || !im.has(kPl + 0x118C, 0x60) || !im.has(kPl + 0x2430, 4)) return;
    p.inputs = true;
    p.flags = im.u32(fl);
    p.bloom_reset = im.u8(fl + 0x26) != 0;
    p.boost_1c = im.f32(kPl + 0x118C + 0x1C);
    p.boost_20 = im.f32(kPl + 0x118C + 0x20);
    p.boost_on = im.u8(kPl + 0x118C + 0x52);
    const std::uint32_t c = im.u32(kPl + 0x2430);
    p.has_crash_obj = c != 0;
    p.crash_time = ram(c) && im.has(c + 0x3530, 0x10) ? im.f32(c + 0x3534) : 0.0f;
    p.now = im.f32(0x4F50A0);
    p.world_clock = im.f32(kWorld + 0x20);
    // UpdateCamera's return: 0 without the pointer; the crash pulse (crash time > 0, flag 0x2000 clear, within 0.3 s of
    // it: D_004E0F5C); else 0 unless flag 0x400, or boost +0x52 with func_0030A990 = 0.65 (D_004E0FC8) x (1 - (+0x20 - 1)^2)
    // x (1 - (+0x1C - 1)^2) > 0
    const float k = 0.65f * (1.0f - (p.boost_20 - 1.0f) * (p.boost_20 - 1.0f)) * (1.0f - (p.boost_1c - 1.0f) * (p.boost_1c - 1.0f));
    if (!p.has_crash_obj) p.branch = 0;
    else if (p.crash_time > 0.0f && (p.flags & 0x2000) == 0 && p.world_clock - p.crash_time < 0.3f) p.branch = 1;
    else if ((p.flags & 0x400) == 0 && (p.boost_on == 0 || !(k > 0.0f))) p.branch = 0;
    else p.branch = 2;
    // CB3World::Render: the directional blur unless Is2PlayerSplitScreen (D_0051BA88 == &D_0051AEC0) && IsCrashMode
    // && !(logic slot 0xAC); the radial blur when UpdateCamera returned 0 and the view's car +0x18EA is clear
    const std::uint32_t game = im.u32(0x51BA88), logic = ram(game) ? im.u32(game + 0x1B4) : 0;
    p.split_crash = game == 0x51AEC0 && t.crash.crash_mode == 1 && logic_virtual(im, logic, 0xAC) == 0;
    const bool crashed = im.has(kPl + 0x18EA, 1) && im.u8(kPl + 0x18EA) != 0;
    p.choice = p.branch != 0 ? (p.split_crash ? 0 : 1) : (crashed ? 0 : 2);
}

void Capture::decode(const Image& im, Tick& t) {
    decode_tick(im, t, *this);
    t.now = im.f32(kWorld + 0x20);   // the world timer is in every version's records
    decode_anim(im, t);
    game_draws(t);
    if (version != 1) decode_props(im, t, *this);
    decode_crash(im, t);
    decode_lap(im, t);   // ghost1
    decode_score(im, t);   // speedrun1
    decode_post(im, t);
    if (t.split) decode_post(im, t, 1);   // split2
    if (version == 0 || version >= 3) decode_effects(im, t, *this);
    if (version == 0 || version >= 3) decode_coronas(im, t, *this);
    if (version == 0 || version >= 3) decode_contact(im, t, *this);
    std::vector<HudAnchorRef> refs;   // hud4: the vehicles markers anchor to (this tick's: the frame drawn before the poll)
    for (const Vehicle& v : t.vehicles) {
        HudAnchorRef r;
        r.id = v.id;
        for (int k = 0; k < 3; ++k) r.pos[k] = v.main.r[3][k];
        r.race_car = v.kind == VehicleKind::kRaceCar;
        refs.push_back(r);
    }
    t.hud = hud_last = hud.decode(im, hud_last, &refs);
    t.host_settings = host_settings_last = host_settings_decode(im, host_settings_last);   // settings1
    t.host_fonts = host_fonts_last = host_fonts_decode(im, host_fonts_last);   // mac5: PC SETTINGS text
    t.host_hold = host_hold_decode(im);                                         // autosave1: the display holds
    t.skip_hint = host_skip_hint_decode(im);                                    // mac5: the video skip hint
    t.race_time = host_race_time_decode(im);                                    // todrace: DAY / NIGHT per race
    t.menu = menu_last = host_menu_decode(im, menu_last);                       // mac5: the menus, for the mouse
    {   // sdk2b: the code mods' overlay (include/b3mod/codemod.h), when its serial changed
        std::uint8_t head[16];
        if (im.read(b3mod::kOverlayBase, head, 16) && std::memcmp(head, "HMO1", 4) == 0) {
            std::uint32_t serial = 0, bytes = 0;
            std::memcpy(&serial, head + 4, 4);
            std::memcpy(&bytes, head + 8, 4);
            if ((!mod_overlay_last || mod_overlay_last->serial != serial) && bytes <= b3mod::kOverlayMax - 16) {
                std::vector<std::uint8_t> b(16 + std::size_t(bytes));
                auto l = std::make_shared<b3mod::OverlayList>();
                if (im.read(b3mod::kOverlayBase, b.data(), std::uint32_t(b.size())) && b3mod::overlay_parse(b.data(), b.size(), *l)) mod_overlay_last = l;
            }
        }
        t.mod_overlay = mod_overlay_last;
    }
    // menu1 (§22): the car the front end drew in this tick's frame (FECAR), decoded as a race car if the race car list
    // does not hold it already
    if (t.hud && !t.hud->fe_cars.empty()) {
        const std::uint32_t car = t.hud->fe_cars.back().car;
        bool listed = false;
        for (const Vehicle& v : t.vehicles) listed = listed || v.id == car;
        Vehicle v;
        bool ok = listed;
        if (!listed && ram(car) && decode_vehicle(im, car, car + 0x80, car, VehicleKind::kRaceCar, v)) {
            ok = true;
            auto& h = models[v.data];
            im.read(v.data, h.data(), 0x70);
            const auto [it, added] = head_index.emplace(h, int(heads.size()));
            if (added) heads.push_back(h);
            v.model = it->second;
            const std::uint32_t raster = im.u32(v.data + 0x60);
            char name[33] = {};
            if (ram(raster) && im.read(raster + 0xA8, name, 32)) model_raster[v.data] = name;
            t.vehicles.push_back(v);
        }
        if (ok) {
            t.fe_car = car;
            const std::uint32_t who = t.hud->fe_cars.back().player;   // padpaint
            t.fe_player = who == 1 || who == 2 ? std::int8_t(who - 1) : std::int8_t(-1);
        }
    }
}

bool Capture::apply_record(Image& im, const std::uint8_t* rec, std::size_t n, Tick& out) {
    std::uint32_t h[4];
    if (n < 16) return false;
    std::memcpy(h, rec, 16);
    if (h[0] != 0x4B434954) return false;
    std::size_t at = 16;
    for (std::uint32_t r = 0; r < h[3]; ++r) {
        std::uint32_t run[2];
        if (at + 8 > n) return false;
        std::memcpy(run, rec + at, 8);
        at += 8;
        if (run[1] % 16 != 0 || run[0] % 16 != 0 || run[1] > n - at) return false;
        im.write(run[0], rec + at, run[1]);
        at += run[1];
        ++runs;
        bytes += run[1];
    }
    out = Tick{};
    out.poll = h[1];
    out.vblank = h[2];
    decode(im, out);
    return at == n;
}

bool same_tick(const Tick& a, const Capture& ca, const Tick& b, const Capture& cb, std::string* what) {
    auto fail = [&](const std::string& w) { if (what) *what = w; return false; };
    auto same_mat = [](const Mat34& x, const Mat34& y) { return std::memcmp(x.r, y.r, sizeof x.r) == 0; };
    if (a.poll != b.poll || a.vblank != b.vblank) return fail("poll / vblank");
    if (a.ticks_per_frame != b.ticks_per_frame) return fail("ticks per frame");
    if (a.lap.valid != b.lap.valid || a.lap.time_attack != b.lap.time_attack || a.lap.index != b.lap.index || a.lap.laps != b.lap.laps ||
        a.lap.time != b.lap.time) return fail("lap (ghost1)");
    if (a.score.valid != b.score.valid || a.score.state != b.score.state || a.score.finish != b.score.finish || a.score.time != b.score.time ||
        a.score.lap != b.score.lap || a.score.laps_done != b.score.laps_done || a.score.logic != b.score.logic ||
        std::memcmp(a.score.lap_times, b.score.lap_times, sizeof a.score.lap_times) != 0) return fail("score (speedrun1)");
    if (bool(a.hud) != bool(b.hud) || (a.hud && (a.hud->serial != b.hud->serial || a.hud->draws.size() != b.hud->draws.size() ||
                                                 a.hud->stats.triangles != b.hud->stats.triangles))) return fail("HUD frame");
    const Camera &c = a.cam, &d = b.cam;
    if (c.valid != d.valid || !same_mat(c.m, d.m) || c.half_w != d.half_w || c.half_h != d.half_h || c.znear != d.znear ||
        c.zfar != d.zfar || c.fov != d.fov || std::memcmp(c.screen, d.screen, sizeof c.screen) != 0) return fail("camera");
    if (std::memcmp(c.lod_pos, d.lod_pos, sizeof c.lod_pos) != 0 || c.lod_scale != d.lod_scale ||
        std::memcmp(c.lod_dist, d.lod_dist, sizeof c.lod_dist) != 0) return fail("camera LOD inputs");
    if (a.route_size != b.route_size || a.route_units != b.route_units || a.track_on != b.track_on || a.current_unit != b.current_unit)
        return fail("route / track state");
    if (std::memcmp(a.slot_unit, b.slot_unit, sizeof a.slot_unit) != 0 || std::memcmp(a.slot_kind, b.slot_kind, sizeof a.slot_kind) != 0 ||
        std::memcmp(a.vis, b.vis, sizeof a.vis) != 0 || std::memcmp(a.backdrops, b.backdrops, sizeof a.backdrops) != 0) return fail("track slots");
    if (a.vehicles.size() != b.vehicles.size()) return fail("vehicle count");
    for (std::size_t k = 0; k < a.vehicles.size(); ++k) {
        const Vehicle &v = a.vehicles[k], &w = b.vehicles[k];
        const std::string n = "vehicle " + std::to_string(k) + ": ";
        if (v.id != w.id || v.data != w.data || v.kind != w.kind || v.paint != w.paint || v.lit != w.lit) return fail(n + "identity");
        if ((v.model < 0) != (w.model < 0) || (v.model >= 0 && ca.heads[std::size_t(v.model)] != cb.heads[std::size_t(w.model)])) return fail(n + "model header");
        if (!same_mat(v.main, w.main) || !same_mat(v.body, w.body)) return fail(n + "matrix");
        if (v.parts != w.parts || v.nwheel != w.nwheel || v.nbit != w.nbit || v.deformed != w.deformed || v.frustum != w.frustum ||
            v.radius != w.radius) return fail(n + "parts / state");
        if (v.sh_count != w.sh_count || std::memcmp(v.sh, w.sh, sizeof v.sh) != 0 || v.glass != w.glass || v.damage != w.damage ||
            v.exploded != w.exploded || v.explode_time != w.explode_time || std::memcmp(v.wheel_speed, w.wheel_speed, sizeof v.wheel_speed) != 0)
            return fail(n + "car shading inputs");
        for (int i = 0; i < v.nwheel; ++i) if (!same_mat(v.wheel[i], w.wheel[i]) || v.wheel_state[i] != w.wheel_state[i]) return fail(n + "wheel");
        for (int i = 0; i < v.nbit; ++i) if (!same_mat(v.bit[i], w.bit[i]) || v.bit_state[i] != w.bit_state[i]) return fail(n + "panel");
        if (v.speed != w.speed || v.spin != w.spin || v.steer != w.steer || v.flags != w.flags || v.flags2 != w.flags2 ||
            v.unit != w.unit || v.listed != w.listed || v.lifted != w.lifted || v.cull != w.cull || v.listed2 != w.listed2 || v.cull2 != w.cull2 || v.own_hidden != w.own_hidden ||
            v.listings2 != w.listings2 || v.v1_known != w.v1_known || v.v1_unit != w.v1_unit || v.v1_frustum != w.v1_frustum ||   // split2
            v.v1_flags2 != w.v1_flags2 || v.v1_extra != w.v1_extra || v.own_hidden2 != w.own_hidden2 ||
            std::memcmp(v.cull_pos, w.cull_pos, sizeof v.cull_pos) != 0) return fail(n + "traffic / cull inputs");
        if (v.zones_valid != w.zones_valid) return fail(n + "zones captured");
        if (v.zones_valid) {
            for (int z = 0; z < 8; ++z) if (!same_mat(v.zone[z], w.zone[z])) return fail(n + "zone matrix");
            for (int i = 0; i < v.nbit; ++i)
                if (v.bit_deformed[i] != w.bit_deformed[i] || !same_mat(v.bit_zone[i][0], w.bit_zone[i][0]) ||
                    !same_mat(v.bit_zone[i][1], w.bit_zone[i][1])) return fail(n + "panel zones");
        }
        const auto &s = v.shadow, &u = w.shadow;
        if (s.cast != u.cast || s.cast2 != u.cast2 || !same_mat(s.ess, u.ess) || std::memcmp(s.normal, u.normal, sizeof s.normal) != 0 || s.dist != u.dist ||
            s.darkness != u.darkness || s.radius != u.radius || std::memcmp(s.sup, u.sup, sizeof s.sup) != 0 ||
            std::memcmp(s.inf, u.inf, sizeof s.inf) != 0 || s.front_z != u.front_z || s.rear_z != u.rear_z) return fail(n + "shadow inputs");
        const auto &o = v.corona, &p = w.corona;
        bool zones = true;
        for (int z = 0; z < 8; ++z) zones = zones && same_mat(o.zone[z], p.zone[z]);
        if (v.listings != w.listings || o.valid != p.valid || o.submit != p.submit || o.lights != p.lights || o.rear_red != p.rear_red || o.red_boost != p.red_boost ||
            o.boost != p.boost || o.sort != p.sort || o.submit2 != p.submit2 || o.sort2 != p.sort2 || !zones || (v.corona_tables == nullptr) != (w.corona_tables == nullptr) ||
            (v.corona_tables && !(*v.corona_tables == *w.corona_tables))) return fail(n + "corona inputs");
    }
    // effects (fx1)
    if (a.effects_valid != b.effects_valid || a.shadows_on != b.shadows_on || a.effects_on != b.effects_on ||
        a.player_visible != b.player_visible || a.split != b.split || a.view2.valid != b.view2.valid ||
        (a.view2.valid && std::memcmp(&a.view2.cam.m, &b.view2.cam.m, sizeof a.view2.cam.m) != 0) ||   // gsoff1
        a.view2.track_valid != b.view2.track_valid || a.view2.track_on != b.view2.track_on ||   // split1
        a.view2.current_unit != b.view2.current_unit || a.view2.barriers != b.view2.barriers ||
        a.view2.barrier_group != b.view2.barrier_group || a.view2.unit_visible != b.view2.unit_visible ||
        a.view2.safe_units != b.view2.safe_units || a.view2.traffic_lists != b.view2.traffic_lists ||
        std::memcmp(a.view2.slot_unit, b.view2.slot_unit, sizeof a.view2.slot_unit) != 0 ||
        std::memcmp(a.view2.slot_kind, b.view2.slot_kind, sizeof a.view2.slot_kind) != 0 ||
        std::memcmp(a.view2.vis, b.view2.vis, sizeof a.view2.vis) != 0 ||
        std::memcmp(a.view2.backdrops, b.view2.backdrops, sizeof a.view2.backdrops) != 0 ||
        a.view2.player_visible != b.view2.player_visible || a.view1_snap != b.view1_snap ||   // split2
        a.shadow_k != b.shadow_k || a.now != b.now ||
        std::memcmp(a.trail_vu, b.trail_vu, sizeof a.trail_vu) != 0 || std::memcmp(a.trail_alpha, b.trail_alpha, sizeof a.trail_alpha) != 0 ||
        std::memcmp(a.trail_name, b.trail_name, sizeof a.trail_name) != 0 || std::memcmp(a.trail_cam, b.trail_cam, sizeof a.trail_cam) != 0)
        return fail("effects state");
    if (a.trails.size() != b.trails.size()) return fail("trail emitter count");
    for (std::size_t k = 0; k < a.trails.size(); ++k) {
        const TrailEmitter &x = *a.trails[k], &y = *b.trails[k];
        if (x.id != y.id || x.type != y.type || x.count != y.count || x.flags != y.flags || x.last != y.last ||
            std::memcmp(x.pt, y.pt, sizeof x.pt) != 0 || std::memcmp(x.dir, y.dir, sizeof x.dir) != 0)
            return fail("trail emitter " + std::to_string(k));
    }
    if (a.unit_visible != b.unit_visible || a.safe_units != b.safe_units || a.traffic_lists != b.traffic_lists)
        return fail("traffic unit visibility");
    const CrashState &x = a.crash, &y = b.crash;
    if (x.valid != y.valid || x.stage != y.stage || x.logic != y.logic || x.ramps_off != y.ramps_off ||
        x.ramps.size() != y.ramps.size() || x.pickups.size() != y.pickups.size() ||
        std::memcmp(x.cam_quat, y.cam_quat, sizeof x.cam_quat) != 0 || std::memcmp(x.cam_pos, y.cam_pos, sizeof x.cam_pos) != 0)
        return fail("crash game data");
    if (x.barriers != y.barriers || x.barrier_group != y.barrier_group || x.crash_mode != y.crash_mode) return fail("barriers");
    if (x.finish_known != y.finish_known || x.finish.size() != y.finish.size()) return fail("finish lines");
    for (std::size_t k = 0; k < x.finish.size(); ++k)
        if (std::memcmp(&x.finish[k], &y.finish[k], sizeof x.finish[k]) != 0) return fail("finish line " + std::to_string(k));
    for (int idx = 0; idx < 2; ++idx) {   // post1 (§25): the bloom, the directional blur state and the blur choice; split2: view 2's
        const PostIn &p = idx == 0 ? a.post : a.view2.post, &q = idx == 0 ? b.post : b.view2.post;
        if (p.valid != q.valid || p.inputs != q.inputs || p.bloom_on != q.bloom_on || p.bloom_reset != q.bloom_reset ||
            p.bloom_freeze != q.bloom_freeze || p.dir_on != q.dir_on || p.flags != q.flags || p.boost_on != q.boost_on ||
            p.has_crash_obj != q.has_crash_obj || p.split_crash != q.split_crash || p.choice != q.choice || p.branch != q.branch ||
            std::memcmp(&p.bloom_threshold, &q.bloom_threshold, 12) != 0 || std::memcmp(p.dir_point, q.dir_point, 12) != 0 ||
            std::memcmp(p.dir_centre, q.dir_centre, 8) != 0 || std::memcmp(p.dir_zoom, q.dir_zoom, 8) != 0 ||
            std::memcmp(&p.dir_amount, &q.dir_amount, 8) != 0 || std::memcmp(&p.boost_1c, &q.boost_1c, 8) != 0 ||
            std::memcmp(&p.crash_time, &q.crash_time, 4) != 0 || std::memcmp(&p.now, &q.now, 8) != 0)
            return fail("post passes");
    }
    if (a.anim_valid != b.anim_valid || a.anim.size() != b.anim.size()) return fail("animated materials");
    for (std::size_t k = 0; k < a.anim.size(); ++k) {
        const AnimMaterial &p = a.anim[k], &q = b.anim[k];
        if (p.material != q.material || p.cur != q.cur || p.p12 != q.p12 || p.p13 != q.p13 ||
            std::memcmp(&p.period, &q.period, 4) != 0 || std::memcmp(&p.t, &q.t, 4) != 0)
            return fail("animated material " + std::to_string(k));
    }
    for (std::size_t k = 0; k < x.ramps.size(); ++k)
        if (x.ramps[k].type != y.ramps[k].type || std::memcmp(x.ramps[k].corner, y.ramps[k].corner, sizeof x.ramps[k].corner) != 0)
            return fail("crash ramp " + std::to_string(k));
    for (std::size_t k = 0; k < x.pickups.size(); ++k)
        if (x.pickups[k].type != y.pickups[k].type || x.pickups[k].taken != y.pickups[k].taken ||
            std::memcmp(x.pickups[k].pos, y.pickups[k].pos, sizeof x.pickups[k].pos) != 0)
            return fail("crash pickup " + std::to_string(k));
    // coronas (corona1)
    const CoronaInputs &ci = a.coronas, &cj = b.coronas;
    auto same_light = [](const CoronaInputs::TrafficLight& l, const CoronaInputs::TrafficLight& m) {
        return l.state == m.state && l.count == m.count && std::memcmp(l.idx, m.idx, sizeof l.idx) == 0;
    };
    if (ci.valid != cj.valid || ci.cars_on != cj.cars_on || ci.traffic_on != cj.traffic_on || ci.seed != cj.seed ||
        std::memcmp(ci.cam_point, cj.cam_point, sizeof ci.cam_point) != 0 || std::memcmp(ci.scales, cj.scales, sizeof ci.scales) != 0 ||
        ci.tl_flat != cj.tl_flat || ci.assigned != cj.assigned || ci.tlights.size() != cj.tlights.size() ||
        !std::equal(ci.tlights.begin(), ci.tlights.end(), cj.tlights.begin(), same_light) ||
        (ci.tl_coronas == nullptr) != (cj.tl_coronas == nullptr) || (ci.tl_coronas && !(*ci.tl_coronas == *cj.tl_coronas)) ||
        (ci.k == nullptr) != (cj.k == nullptr) || ci.vu_cam_valid != cj.vu_cam_valid ||
        std::memcmp(ci.vu_cam.row, cj.vu_cam.row, sizeof ci.vu_cam.row) != 0)
        return fail("corona inputs");
    // contact effects (fx2)
    const ContactFx &u = a.contact, &w = b.contact;
    auto same_ptr = [](const auto& p, const auto& q) { return (p == nullptr) == (q == nullptr) && (!p || *p == *q); };
    if (u.scrapes_valid != w.scrapes_valid || u.scrape_gate != w.scrape_gate || std::memcmp(u.scrape_params, w.scrape_params, sizeof u.scrape_params) != 0 ||
        std::memcmp(u.scrape_uvs, w.scrape_uvs, sizeof u.scrape_uvs) != 0 || u.scrapes.size() != w.scrapes.size() ||
        !std::equal(u.scrapes.begin(), u.scrapes.end(), w.scrapes.begin(), same_ptr))
        return fail("scrapes");
    if (u.sparks_valid != w.sparks_valid || std::memcmp(u.spark_statics, w.spark_statics, sizeof u.spark_statics) != 0 ||
        std::memcmp(u.spark_d1e31a20, w.spark_d1e31a20, sizeof u.spark_d1e31a20) != 0 || u.cam_index != w.cam_index ||
        u.history_valid != w.history_valid || !same_ptr(u.history, w.history) ||
        !std::equal(std::begin(u.sparks), std::end(u.sparks), std::begin(w.sparks), same_ptr))
        return fail("sparks");
    if (u.popcorn_valid != w.popcorn_valid || std::memcmp(u.popcorn_params, w.popcorn_params, sizeof u.popcorn_params) != 0 ||
        !std::equal(std::begin(u.popcorn), std::end(u.popcorn), std::begin(w.popcorn), same_ptr))
        return fail("popcorn");
    if (u.particles_valid != w.particles_valid || u.particle_skip != w.particle_skip || !same_ptr(u.particle_prog, w.particle_prog) ||
        !same_ptr(u.particle_cam, w.particle_cam) || !std::equal(std::begin(u.particles), std::end(u.particles), std::begin(w.particles), same_ptr))
        return fail("particles");
    return true;
}

}  // namespace b3replay
