// HUD / 2D reader (src/b3replay/hud.cpp, hud1): synthetic Gt2d event streams in the layout src/gameview/hud_capture.cpp
// publishes, decoded into triangles with the GS state, as Gt2dRenderer's packet shapes (Construct 0x21A290) produce them.
#include "test.h"

#include <algorithm>
#include <cmath>

#include "../src/b3replay/capture.h"
#include "../src/b3replay/globe.h"
#include "../src/b3replay/hud.h"
#include "b3asset/png.h"

namespace {
using b3replay::Image;

struct Stream {
    std::vector<std::uint8_t> ev;
    void u32(std::uint32_t v) { const std::size_t n = ev.size(); ev.resize(n + 4); std::memcpy(ev.data() + n, &v, 4); }
    void event(std::uint32_t kind, const std::vector<std::uint8_t>& p) {
        u32(kind);
        u32(std::uint32_t(p.size()));
        ev.insert(ev.end(), p.begin(), p.end());
        while (ev.size() & 3) ev.push_back(0);
    }
};
void put64(std::vector<std::uint8_t>& b, std::uint64_t v) { const std::size_t n = b.size(); b.resize(n + 8); std::memcpy(b.data() + n, &v, 8); }
void putf(std::vector<std::uint8_t>& b, float f) { const std::size_t n = b.size(); b.resize(n + 4); std::memcpy(b.data() + n, &f, 4); }

// STATE: Alpha / Tex1 / Clamp / Scissor / Frame qwords, _gGsOffsetXYXY, the gp block, skyTextureRaster
std::vector<std::uint8_t> state(std::uint64_t alpha, std::uint32_t cur_texture) {
    std::vector<std::uint8_t> s;
    const std::uint64_t regs[5][2] = {{alpha, 0x42}, {0x120, 0x14}, {5, 8}, {0x01BF0000027F0000ULL, 0x40}, {0xFF000000000A0046ULL, 0x4C}};
    for (auto& r : regs) { put64(s, r[0]); put64(s, r[1]); }
    for (float f : {1728.0f, 1824.0f, 1728.0f, 1824.0f}) putf(s, f);
    std::vector<std::uint8_t> gp(0x38, 0);
    std::memcpy(gp.data() + 0x18, &cur_texture, 4);
    s.insert(s.end(), gp.begin(), gp.end());
    s.resize(s.size() + 8, 0);
    return s;
}
std::uint64_t xyz(float x, float y) {   // pixel -> GS 12.4 with Gt2d's offset
    return std::uint64_t(std::uint32_t((x + 1728.0f) * 16)) | std::uint64_t(std::uint32_t((y + 1824.0f) * 16)) << 16 | 0x00FFFFFFULL << 32;
}
std::uint64_t st(float s, float t) { std::uint64_t v; std::uint32_t a = t::fbits(s), b = t::fbits(t); v = a | std::uint64_t(b) << 32; return v; }

std::shared_ptr<const b3replay::HudFrame> decode(const Stream& s, std::uint32_t serial, b3replay::HudDecoder& dec,
                                                 std::shared_ptr<const b3replay::HudFrame> prev = nullptr) {
    std::vector<std::uint8_t> p;
    for (std::uint32_t v : {0x31445548U, serial, std::uint32_t(s.ev.size()), 0U}) { const std::size_t n = p.size(); p.resize(n + 4); std::memcpy(p.data() + n, &v, 4); }
    p.insert(p.end(), s.ev.begin(), s.ev.end());
    while (p.size() & 15) p.push_back(0);
    static Image im;
    im.clear();
    im.write(b3replay::kHudBase, p.data(), std::uint32_t(p.size()));
    return dec.decode(im, prev);
}

// One batch: PRIM + RGBAQ (Gt2d's _gu128SetPrimRGBAQGIFTag), then a REGLIST of `regs` x nloop with the given 64-bit values.
std::vector<std::uint8_t> batch(std::uint64_t prim, std::uint64_t rgbaq, std::uint64_t reglist_tag, std::uint64_t regs,
                                const std::vector<std::uint64_t>& data) {
    std::vector<std::uint8_t> b;
    put64(b, 0x2400000000000001ULL); put64(b, 0x10);
    put64(b, prim); put64(b, rgbaq);
    put64(b, reglist_tag); put64(b, regs);
    for (std::uint64_t v : data) put64(b, v);
    if (b.size() & 15) put64(b, 0);
    std::vector<std::uint8_t> e;
    const std::uint32_t qwc = std::uint32_t(b.size() / 16);
    e.resize(4);
    std::memcpy(e.data(), &qwc, 4);
    e.insert(e.end(), b.begin(), b.end());
    return e;
}
}  // namespace

TEST(hud_sprite_untextured) {
    Stream s;
    s.event(1, {});
    s.event(2, state(0x44, 0));
    // RenderSpritesUntex: PRIM 0x46 (sprite, ABE), REGLIST XYZ2 XYZ2 (_gau128RegListGIFTags[1]: NREG 2, nloop 1)
    s.event(7, batch(0x46, 0x3F80000080402010ULL, 0x2400000000000001ULL, 0x55, {xyz(10, 20), xyz(110, 70)}));
    s.event(6, {});
    b3replay::HudDecoder dec;
    const auto f = decode(s, 7, dec);
    CHECK(f != nullptr);
    if (!f) return;
    CHECK(f->serial == 7);
    CHECK(f->width == 640 && f->height == 448);
    CHECK(f->draws.size() == 1);
    if (f->draws.empty()) return;
    const auto& d = f->draws[0];
    CHECK(d.texture == 0);
    CHECK(d.alpha == 0x44 && d.abe);
    CHECK(d.fbw == 10 && d.fbp == 0x46);
    CHECK(d.tri.size() == 6);
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (const auto& v : d.tri) { x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y); }
    CHECK_NEAR(x0, 10, 1e-4); CHECK_NEAR(x1, 110, 1e-4); CHECK_NEAR(y0, 20, 1e-4); CHECK_NEAR(y1, 70, 1e-4);
    CHECK(d.tri[0].rgba[0] == 0x10 && d.tri[0].rgba[1] == 0x20 && d.tri[0].rgba[2] == 0x40 && d.tri[0].rgba[3] == 0x80);
    CHECK(f->stats.sprites == 1 && f->stats.triangles == 2);
    // the same serial again: the previous frame is kept
    CHECK(decode(s, 7, dec, f) == f);
}

TEST(hud_strip_gouraud_and_blend_change) {
    Stream s;
    s.event(1, {});
    s.event(2, state(0x44, 0));
    // RenderTriStripGouraudUntex: PRIM 0x4C, REGLIST RGBAQ XYZ2 (_gau128RegListGIFTags[9]) x 4 vertices
    s.event(7, batch(0x4C, 0x3F80000080808080ULL, 0x2400000000000004ULL, 0x51,
                     {0x3F80000080000000ULL, xyz(0, 0), 0x3F80000080000000ULL, xyz(100, 0),
                      0x3F800000800000FFULL, xyz(0, 50), 0x3F800000800000FFULL, xyz(100, 50)}));
    // SetBlendMode(1): ADDRDATA tag + ALPHA_1 0x48, then a sprite
    std::vector<std::uint8_t> b;
    put64(b, 0x1000000000000002ULL); put64(b, 0xE);
    put64(b, 0x48); put64(b, 0x42);
    put64(b, 0xFF00000000000000ULL | 0xA0046); put64(b, 0x4C);
    std::vector<std::uint8_t> e(4);
    const std::uint32_t q = std::uint32_t(b.size() / 16);
    std::memcpy(e.data(), &q, 4);
    e.insert(e.end(), b.begin(), b.end());
    s.event(7, e);
    s.event(7, batch(0x46, 0x3F80000080808080ULL, 0x2400000000000001ULL, 0x55, {xyz(1, 1), xyz(2, 2)}));
    b3replay::HudDecoder dec;
    const auto f = decode(s, 1, dec);
    CHECK(f && f->draws.size() == 2);
    if (!f || f->draws.size() != 2) return;
    CHECK(f->draws[0].tri.size() == 6);          // 4-vertex strip = 2 triangles
    CHECK(f->draws[0].tri[0].rgba[0] == 0x00);   // gouraud keeps each vertex's colour
    CHECK(f->draws[0].tri[2].rgba[0] == 0xFF);
    CHECK(f->draws[1].alpha == 0x48);
    CHECK(f->stats.batches == 3);
}

TEST(hud_texture_queue_unknown_raster) {
    // SetTexture events pair with TEX0 writes in order; a raster missing from the image counts as an undecoded texture
    Stream s;
    s.event(1, {});
    s.event(2, state(0x44, 0));
    std::vector<std::uint8_t> t(8, 0);
    const std::uint32_t r = 0x00500000;
    std::memcpy(t.data(), &r, 4);
    s.event(3, t);
    std::vector<std::uint8_t> b;
    put64(b, 0x1000000000000001ULL); put64(b, 0xE);
    put64(b, 0x0000000400000000ULL); put64(b, 0x06);   // TEX0_1 (TCC 1)
    put64(b, 0x2400000000000001ULL); put64(b, 0x10);
    put64(b, 0x56); put64(b, 0x3F80000080808080ULL);
    put64(b, 0x4400000000000001ULL); put64(b, 0x5252);
    put64(b, st(0, 0)); put64(b, xyz(0, 0)); put64(b, st(1, 1)); put64(b, xyz(16, 16));
    std::vector<std::uint8_t> e(4);
    const std::uint32_t q = std::uint32_t(b.size() / 16);
    std::memcpy(e.data(), &q, 4);
    e.insert(e.end(), b.begin(), b.end());
    s.event(7, e);
    b3replay::HudDecoder dec;
    const auto f = decode(s, 2, dec);
    CHECK(f && f->stats.sprites == 1);
    if (!f) return;
    CHECK(f->stats.unknown_texture == 2);   // both triangles of the sprite
    CHECK(f->draws.size() == 1 && f->draws[0].texture == 0);
}

TEST(hud_tex0_without_settexture_is_unbound) {
    // font1: a TEX0 write no SetTexture event announced (a native SetTexture overload, 0x219C90, once hid the
    // text's calls from the hook) leaves its textured triangles without a raster: counted as unbound; the same stream
    // with the event counts none (the raster missing from the image is only "unknown")
    auto stream = [](bool announce) {
        Stream s;
        s.event(1, {});
        s.event(2, state(0x44, 0));
        if (announce) {
            std::vector<std::uint8_t> t(8, 0);
            const std::uint32_t r = 0x00500000;
            std::memcpy(t.data(), &r, 4);
            s.event(3, t);
        }
        std::vector<std::uint8_t> b;
        put64(b, 0x1000000000000001ULL); put64(b, 0xE);
        put64(b, 0x2006D40621413620ULL); put64(b, 0x06);   // TEX0_1 as the text's SetTexture writes it (race2-b14 poll 736)
        put64(b, 0x2400000000000001ULL); put64(b, 0x10);
        put64(b, 0x56); put64(b, 0x3F80000080808080ULL);
        put64(b, 0x4400000000000001ULL); put64(b, 0x5252);
        put64(b, st(0, 0)); put64(b, xyz(0, 0)); put64(b, st(1, 1)); put64(b, xyz(16, 16));
        std::vector<std::uint8_t> e(4);
        const std::uint32_t q = std::uint32_t(b.size() / 16);
        std::memcpy(e.data(), &q, 4);
        e.insert(e.end(), b.begin(), b.end());
        s.event(7, e);
        return s;
    };
    b3replay::HudDecoder d1, d2;
    const auto lost = decode(stream(false), 2, d1), seen = decode(stream(true), 2, d2);
    CHECK(lost && seen);
    if (!lost || !seen) return;
    CHECK(lost->stats.sprites == 1 && lost->stats.unbound_texture == 2);
    CHECK(seen->stats.sprites == 1 && seen->stats.unbound_texture == 0 && seen->stats.unknown_texture == 2);
}

TEST(hud_cache_texture_switch_after_flush) {
    // SetTexture of a texture-cache raster (+0x8C == 0) with palette 0 writes no TEX0: the switch takes effect after
    // the flush it does first (flag 1), or at once when nothing is pending (flag 0) [C asm 0x219B30..0x219B74]
    auto tex = [](std::uint32_t r, std::uint32_t c8c, std::uint32_t flush) {
        std::vector<std::uint8_t> t(16, 0);
        std::memcpy(t.data(), &r, 4);
        std::memcpy(t.data() + 8, &c8c, 4);
        std::memcpy(t.data() + 12, &flush, 4);
        return t;
    };
    auto sprite = [&](float x) { return batch(0x56, 0x3F80000080808080ULL, 0x4400000000000001ULL, 0x5252, {st(0, 0), xyz(x, 0), st(1, 1), xyz(x + 8, 8)}); };
    Stream s;
    s.event(1, {});
    s.event(2, state(0x44, 0x00600000));   // RenderWare's current raster after Begin
    s.event(3, tex(0x00610000, 0, 0));       // nothing pending: at once
    s.event(7, sprite(0));
    s.event(3, tex(0x00620000, 0, 1));       // a flush follows: the flushed batch keeps 0x610000
    s.event(7, sprite(10));
    s.event(7, sprite(20));
    // headers present (named, no upload block: not decodable, but each gets its own texture key)
    std::vector<std::uint8_t> p;
    for (std::uint32_t v : {0x31445548U, 3U, std::uint32_t(s.ev.size()), 0U}) { const std::size_t n = p.size(); p.resize(n + 4); std::memcpy(p.data() + n, &v, 4); }
    p.insert(p.end(), s.ev.begin(), s.ev.end());
    while (p.size() & 15) p.push_back(0);
    Image im;
    im.write(b3replay::kHudBase, p.data(), std::uint32_t(p.size()));
    for (std::uint32_t r : {0x00600000U, 0x00610000U, 0x00620000U}) {
        std::uint8_t h[0xD0] = {};
        std::snprintf(reinterpret_cast<char*>(h + 0xA8), 32, "r%06X", r);
        im.write(r, h, sizeof h);
    }
    b3replay::HudDecoder dec;
    const auto f = dec.decode(im, nullptr);
    CHECK(f != nullptr);
    if (!f) return;
    CHECK(f->stats.sprites == 3 && f->stats.unknown_texture == 6);
    // draws carry the raster's key even when it cannot be decoded: sprites 1-2 on 0x610000, sprite 3 on 0x620000
    CHECK(f->draws.size() == 2);
    if (f->draws.size() == 2) {
        CHECK(dec.textures.at(f->draws[0].texture)->raster == 0x00610000 && f->draws[0].tri.size() == 12);
        CHECK(dec.textures.at(f->draws[1].texture)->raster == 0x00620000 && f->draws[1].tri.size() == 6);
    }
    // the decoder's texture map holds one entry per raster it was asked for; the draws carry key 0 (not decoded), so
    // check the order through the map's rasters instead: 0x610000 and 0x620000 were asked for, 0x600000 too (Begin)
    std::vector<std::uint32_t> asked;
    for (const auto& kv : dec.textures) asked.push_back(kv.second->raster);
    std::sort(asked.begin(), asked.end());
    CHECK(asked.size() == 3 && asked[0] == 0x00600000 && asked[1] == 0x00610000 && asked[2] == 0x00620000);
}

TEST(hud_batch_in_slot) {
    // kind 8: the batch lives in its slot (hud_capture.cpp kSlotBase), the index holds {qwc, address}
    const std::vector<std::uint8_t> b = batch(0x46, 0x3F80000080808080ULL, 0x2400000000000001ULL, 0x55, {xyz(5, 6), xyz(7, 8)});
    std::uint32_t qwc;
    std::memcpy(&qwc, b.data(), 4);
    Stream s;
    s.event(1, {});
    s.event(2, state(0x44, 0));
    std::vector<std::uint8_t> ref(8);
    const std::uint32_t addr = 0x40100000U;
    std::memcpy(ref.data(), &qwc, 4);
    std::memcpy(ref.data() + 4, &addr, 4);
    s.event(8, ref);
    std::vector<std::uint8_t> p;
    for (std::uint32_t v : {0x31445548U, 9U, std::uint32_t(s.ev.size()), 0U}) { const std::size_t n = p.size(); p.resize(n + 4); std::memcpy(p.data() + n, &v, 4); }
    p.insert(p.end(), s.ev.begin(), s.ev.end());
    while (p.size() & 15) p.push_back(0);
    Image im;
    im.write(b3replay::kHudBase, p.data(), std::uint32_t(p.size()));
    im.write(addr, b.data() + 4, qwc * 16);
    b3replay::HudDecoder dec;
    const auto f = dec.decode(im, nullptr);
    CHECK(f && f->stats.sprites == 1 && f->draws.size() == 1);
}

TEST(hud_objects_components_and_wrap) {
    // hud2: object events tag the bytes written after their offset; the scene's component list resolves each object's
    // frame chain to a component and its align (fx from the captured D_004B7650); CLAMP_1 REPEAT is kept per draw
    Stream s;
    std::vector<std::uint8_t> sc;
    auto p32 = [](std::vector<std::uint8_t>& b, std::uint32_t v) { const std::size_t n = b.size(); b.resize(n + 4); std::memcpy(b.data() + n, &v, 4); };
    for (std::uint32_t v : {0x0066A3E4U, 0x001A07F0U, 0U, 2U}) p32(sc, v);
    for (float f : {16.0f, 16.0f, 624.0f, 432.0f}) putf(sc, f);
    p32(sc, 0x00700000U);
    const float table[18] = {0, 0, 0.5f, 0, 1, 0, 0, 0.5f, 0.5f, 0.5f, 1, 0.5f, 0, 1, 0.5f, 1, 1, 1};
    for (float f : table) putf(sc, f);
    for (std::uint32_t v : {0x00710000U, 0x004A0000U, 0x00700100U, 1U, 2U, 0U}) p32(sc, v);   // align 2: top right
    for (std::uint32_t v : {0x00720000U, 0x004A0100U, 0x00700200U, 1U, 8U, 0U}) p32(sc, v);   // align 8: absolute
    s.event(9, sc);
    s.event(1, {});
    s.event(2, state(0x44, 0));
    auto object = [&](std::uint32_t obj, std::uint32_t off, std::uint32_t f0, std::uint32_t f1) {
        std::vector<std::uint8_t> o;
        for (std::uint32_t v : {0x001D5EA0U, obj, off, 0U, 0U, 0U, 0U, f0, f1, 0x00700000U, 0U, 0U, 0U, 0U, 0U}) p32(o, v);
        p32(o, 0);
        s.event(10, o);
    };
    // one batch with two sprites: object A (a child frame of the align-2 component's frame) from byte 0, object B (the
    // absolute component) from the second sprite's GIF tag on; CLAMP_1 REPEAT written between them
    const auto a = batch(0x46, 0x3F80000080808080ULL, 0x2400000000000001ULL, 0x55, {xyz(10, 10), xyz(20, 20)});
    std::vector<std::uint8_t> clamp;
    put64(clamp, 0x1000000000000001ULL); put64(clamp, 0xE);
    put64(clamp, 0); put64(clamp, 0x08);
    const auto b = batch(0x46, 0x3F80000080808080ULL, 0x2400000000000001ULL, 0x55, {xyz(30, 30), xyz(40, 40)});
    std::vector<std::uint8_t> e(4);
    e.insert(e.end(), a.begin() + 4, a.end());
    e.insert(e.end(), clamp.begin(), clamp.end());
    const std::uint32_t second = std::uint32_t(e.size() - 4);
    e.insert(e.end(), b.begin() + 4, b.end());
    const std::uint32_t qwc = std::uint32_t((e.size() - 4) / 16);
    std::memcpy(e.data(), &qwc, 4);
    object(0x00800000U, 0, 0x00700180U, 0x00700100U);
    object(0x00800100U, second, 0x00700200U, 0x00700000U);
    s.event(7, e);
    b3replay::HudDecoder dec;
    const auto f = decode(s, 3, dec);
    CHECK(f && f->has_objects);
    if (!f) return;
    CHECK(f->components.size() == 2 && f->objects.size() == 2 && f->draws.size() == 2);
    if (f->components.size() != 2 || f->objects.size() != 2 || f->draws.size() != 2) return;
    CHECK(f->components[0].align == 2 && f->components[0].fx == 1.0f && f->components[0].fy == 0.0f);
    CHECK(f->components[1].align == 8 && f->components[1].fx == 0.5f);
    CHECK(f->objects[0].component == 0 && f->objects[1].component == 1);
    CHECK(f->draws[0].object == 0 && f->draws[1].object == 1);
    CHECK(f->draws[0].wms == 1 && f->draws[0].wmt == 1);   // the state's CLAMP_1 5: clamp, clamp
    CHECK(f->draws[1].wms == 0 && f->draws[1].wmt == 0);   // REPEAT
    CHECK_NEAR(f->hud_rect[2], 624, 1e-4);
    CHECK(f->stats.hud_scenes == 1 && f->stats.objects == 2);
}

TEST(hud_boost_bar_second_frame) {
    // fe3: in HUD mode 3 (the crash / takedown camera) CB3HUDBoostBar::UpdateTransition 0x1A3F40 hangs the bar's object on
    // the bar's own second frame (a child of the HUD root, not the component's frame); it still belongs to the bar
    auto p32 = [](std::vector<std::uint8_t>& b, std::uint32_t v) { const std::size_t n = b.size(); b.resize(n + 4); std::memcpy(b.data() + n, &v, 4); };
    const float table[18] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 1, 0.5f, 1, 1, 1, 1, 0.5f, 1, 0, 0.5f, 0.5f};
    auto run = [&](std::uint32_t pc, std::uint32_t root_of_object) {
        Stream s;
        std::vector<std::uint8_t> sc;
        for (std::uint32_t v : {0x0066A3E4U, 0x001A07F0U, 0U, 2U}) p32(sc, v);
        for (float f : {30.0f, 22.0f, 610.0f, 458.0f}) putf(sc, f);
        p32(sc, 0x006682B0U);   // the HUD root frame
        for (float f : table) putf(sc, f);
        for (std::uint32_t v : {0x0066A610U, 0x004DDEC0U, 0x00668490U, 1U, 3U, 0U}) p32(sc, v);   // _gBoostBarComponentP1, bottom left
        for (std::uint32_t v : {0x0066AE10U, 0x004DDEA0U, 0x006683D0U, 1U, 5U, 0U}) p32(sc, v);   // the speedo, bottom right
        s.event(9, sc);
        s.event(1, {});
        s.event(2, state(0x44, 0));
        std::vector<std::uint8_t> o;
        for (std::uint32_t v : {pc, 0x006675D8U, 0U, 0U, 0U, 0U, 0U, 0x006684F0U, root_of_object, 0U, 0U, 0U, 0U, 0U, 0U}) p32(o, v);
        p32(o, 0);
        s.event(10, o);
        s.event(7, batch(0x46, 0x3F80000080808080ULL, 0x2400000000000001ULL, 0x55, {xyz(28, 403), xyz(230, 427)}));
        b3replay::HudDecoder dec;
        const auto f = decode(s, 5, dec);
        return f && f->objects.size() == 1 ? f->objects[0].component : -2;
    };
    CHECK(run(0x001A6290U, 0x006682B0U) == 0);    // C2dObjectBoostBar::_Render on the bar's second frame: the bar
    CHECK(run(0x001A6290U, 0x00700000U) == -1);   // another HUD's root: not this one's bar
    CHECK(run(0x001D5FB0U, 0x006682B0U) == -1);   // another object class on an unlisted frame: no component, as before
}

TEST(hud_markers_anchor_and_project) {
    // hud4: a MARKER event (DrawPositionMarker 0x1531C0) tags the bytes after its offset until the next Begin; the point
    // projects through the captured viewport as the game does; the marker anchors to its race car (offset = point -
    // the car's position); hud_project maps a view-projection's clip space to the 2D space
    Stream s;
    auto p32 = [](std::vector<std::uint8_t>& b, std::uint32_t v) { const std::size_t n = b.size(); b.resize(n + 4); std::memcpy(b.data() + n, &v, 4); };
    s.event(1, {});
    s.event(2, state(0x44, 0));
    // a viewport that maps (x, y, z) to (x / z, y / z) in 0..1 around 0.5: rows (0.5, 0, 0), (0, -0.5, 0), (0.5, 0.5, 1), (0, 0, 0)
    auto marker = [&](std::uint32_t off, std::uint32_t car, float wx, float wy, float wz) {
        std::vector<std::uint8_t> m;
        for (std::uint32_t v : {0x001531C0U, off, 2U, car, 0x00523000U, 0U}) p32(m, v);
        for (float f : {wx, wy, wz}) putf(m, f);
        p32(m, 0x00670000U);
        for (float f : {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}) putf(m, f);
        for (float f : {1.0f, 0.7f, 1.0f, 1.0f}) putf(m, f);
        p32(m, 640); p32(m, 448);
        putf(m, 0.5f); putf(m, 1000.0f);
        s.event(11, m);
    };
    const auto a = batch(0x46, 0x3F80000080808080ULL, 0x2400000000000001ULL, 0x55, {xyz(10, 10), xyz(20, 20)});
    const auto b = batch(0x46, 0x3F80000080808080ULL, 0x2400000000000001ULL, 0x55, {xyz(30, 30), xyz(40, 40)});
    std::vector<std::uint8_t> e(4);
    e.insert(e.end(), a.begin() + 4, a.end());
    const std::uint32_t second = std::uint32_t(e.size() - 4);
    e.insert(e.end(), b.begin() + 4, b.end());
    const std::uint32_t qwc = std::uint32_t((e.size() - 4) / 16);
    std::memcpy(e.data(), &qwc, 4);
    marker(second, 0x00530000U, 2.0f, 1.5f, 10.0f);   // the second sprite is the marker's
    s.event(7, e);
    s.event(6, {});
    s.event(1, {});   // a new pass: no marker
    s.event(2, state(0x44, 0));
    s.event(7, batch(0x46, 0x3F80000080808080ULL, 0x2400000000000001ULL, 0x55, {xyz(50, 50), xyz(60, 60)}));
    std::vector<std::uint8_t> p;
    for (std::uint32_t v : {0x31445548U, 4U, std::uint32_t(s.ev.size()), 0U}) p32(p, v);
    p.insert(p.end(), s.ev.begin(), s.ev.end());
    while (p.size() & 15) p.push_back(0);
    static Image im;
    im.clear();
    im.write(b3replay::kHudBase, p.data(), std::uint32_t(p.size()));
    std::vector<b3replay::HudAnchorRef> refs(2);
    refs[0].id = 0x00540000U; refs[0].pos[0] = 2.0f; refs[0].pos[2] = 10.0f;   // under the point, but not the named car
    refs[1].id = 0x00530000U; refs[1].pos[0] = 2.1f; refs[1].pos[1] = 0.5f; refs[1].pos[2] = 10.2f;
    b3replay::HudDecoder dec;
    const auto f = dec.decode(im, nullptr, &refs);
    CHECK(f && f->markers.size() == 1 && f->draws.size() == 3);
    if (!f || f->markers.size() != 1 || f->draws.size() != 3) return;
    CHECK(f->draws[0].marker == -1 && f->draws[1].marker == 0 && f->draws[2].marker == -1);
    const auto& m = f->markers[0];
    CHECK(m.valid && m.slot == 2 && m.car == 0x00530000U && !m.points && m.sw == 640 && m.sh == 448);
    // p = (0.5 x + 0.5 z, -0.5 y + 0.5 z, z) = (6, 4.25, 10): screen (0.6 x 640, 0.425 x 448)
    CHECK_NEAR(m.x, 384.0, 1e-3);
    CHECK_NEAR(m.y, 190.4, 1e-3);
    CHECK_NEAR(m.z, 10.0, 1e-6);
    CHECK_NEAR(m.size, 0.5 * 640 * (1.5 * (1 + 9.0 / 74) / 10), 1e-3);
    CHECK(m.vehicle == 0x00530000U);
    CHECK_NEAR(m.offset[0], -0.1, 1e-5);
    CHECK_NEAR(m.offset[1], 1.0, 1e-5);
    CHECK_NEAR(m.offset[2], -0.2, 1e-5);
    // an identity view-projection at aspect 4:3: clip (0.5, -0.25, 0, 1) -> 2D (480, 280)
    const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float w[3] = {0.5f, -0.25f, 0.0f};
    float x = 0, y = 0, z = 0;
    CHECK(b3replay::hud_project(id, 4.0f / 3.0f, 640, 448, w, &x, &y, &z));
    CHECK_NEAR(x, 480.0, 1e-3);
    CHECK_NEAR(y, 280.0, 1e-3);
    CHECK_NEAR(z, 1.0, 1e-6);
    // at 16:9 the 4:3 box is the centre 3/4: clip x 0.5 is 2D x 320 + 0.5 x 320 / 0.75
    CHECK(b3replay::hud_project(id, 16.0f / 9.0f, 640, 448, w, &x, &y, &z));
    CHECK_NEAR(x, 320.0 + 160.0 / 0.75, 1e-3);
}

TEST(hud_movie_tiles_and_picture) {
    // menu1: a CGt2dObjectVideoImage object (callback 0x326960) and its VIDEO event; its textured sprite (no Gt2d texture:
    // SetTileTexture uploads outside the buffer) becomes a movie draw with u, v over the whole picture, and the captured
    // picture (host slot 0 at 0x40A00000: 16-pixel strips, 64 bytes a row) decodes to rows
    auto p32 = [](std::vector<std::uint8_t>& b, std::uint32_t v) { const std::size_t n = b.size(); b.resize(n + 4); std::memcpy(b.data() + n, &v, 4); };
    Stream s;
    s.event(1, {});
    s.event(2, state(0x44, 0));
    std::vector<std::uint8_t> o;
    for (std::uint32_t v : {0x00326960U, 0x00900000U, 0U}) p32(o, v);
    for (float f : {0.0f, 50.0f, 640.0f, 320.0f}) putf(o, f);
    for (int k = 0; k < 9; ++k) p32(o, 0);
    s.event(10, o);
    std::vector<std::uint8_t> v;
    for (std::uint32_t w : {0x00900000U, 0x00910000U, 0U, 0x00920000U, 1U, 1U, 2U, 1U, 0x3000U, 1U, 7U, 0U, 0U, 0U, 0U, 0U, 0U, 2048U}) p32(v, w);
    putf(v, 0.5f);    // the picture's size in 64-px tiles: 32 x 16 pixels
    putf(v, 0.25f);
    p32(v, 0);
    s.event(12, v);
    s.event(7, batch(0x56, 0x3F80000080808080ULL, 0x4400000000000001ULL, 0x5252, {st(0, 0), xyz(0, 50), st(1, 1), xyz(640, 370)}));
    std::vector<std::uint8_t> p;
    for (std::uint32_t w : {0x31445548U, 5U, std::uint32_t(s.ev.size()), 0U}) p32(p, w);
    p.insert(p.end(), s.ev.begin(), s.ev.end());
    while (p.size() & 15) p.push_back(0);
    Image im;
    im.write(b3replay::kHudBase, p.data(), std::uint32_t(p.size()));
    std::vector<std::uint8_t> pic(2048, 0);
    const std::uint8_t px[4] = {10, 20, 30, 0x80};
    std::memcpy(pic.data() + 1024 + 3 * 64 + 4, px, 4);   // x 17 = strip 1, column 1; row 3
    im.write(0x40A00000U, pic.data(), 2048);
    b3replay::HudDecoder dec;
    const auto f = dec.decode(im, nullptr);
    CHECK(f && f->videos.size() == 1 && f->draws.size() == 1);
    if (!f || f->videos.size() != 1 || f->draws.size() != 1) return;
    const auto& d = f->draws[0];
    CHECK(d.video == 0 && d.texture == 0 && d.tri.size() == 6);
    float u0 = 9, u1 = -9, v0 = 9, v1 = -9;
    for (const auto& t : d.tri) { u0 = std::min(u0, t.u); u1 = std::max(u1, t.u); v0 = std::min(v0, t.v); v1 = std::max(v1, t.v); }
    CHECK_NEAR(u0, 0, 1e-5); CHECK_NEAR(u1, 1, 1e-5); CHECK_NEAR(v0, 0, 1e-5); CHECK_NEAR(v1, 1, 1e-5);
    const auto& hv = f->videos[0];
    CHECK(hv.decoded == 7 && hv.mb_w == 2 && hv.mb_h == 1 && hv.picture != nullptr);
    if (!hv.picture) return;
    CHECK(hv.picture->w == 32 && hv.picture->h == 16);
    const std::uint8_t* q = hv.picture->rgba.data() + 4 * (3 * 32 + 17);
    CHECK(q[0] == 10 && q[1] == 20 && q[2] == 30 && q[3] == 255);
}

TEST(globe_event_place_and_batches) {
    // fe2 (§24): a GLOBE event (hud_capture.cpp observe_globe, at CB3Globe2dObject::_Render's entry) between Gt2d draws:
    // the draw its Suspend flushes comes before the globe, the one after its Resume after it; then the B3Im3d batches
    // _Render sends for a fully revealed sphere (globe_batches)
    auto p32 = [](std::vector<std::uint8_t>& b, std::uint32_t v) { const std::size_t n = b.size(); b.resize(n + 4); std::memcpy(b.data() + n, &v, 4); };
    const auto sprite = batch(0x46, 0x3F80000080808080ULL, 0x4400000000000001ULL, 0x55, {xyz(0, 0), xyz(10, 10)});
    Stream s;
    s.event(1, {});
    s.event(2, state(0x44, 0));
    s.event(7, sprite);                   // drawn before the globe's callback
    std::vector<std::uint8_t> g;
    p32(g, 0x01EA0000U);                  // the object
    for (float f : {0.0f, 0.0f, 500.0f}) putf(g, f);
    p32(g, 0x4000U | 0x8000U << 16);      // latitude 0x4000, longitude 0x8000
    p32(g, 0xFFFFU);                      // shape 0xFFFF (the halo keeps its alpha), 0 (a sphere)
    p32(g, 0xFFU | 0xFFU << 8 | 0x80U << 16 | 0xFFU << 24);   // alpha, reveal 0xFF, highlight, region -1
    for (float f : {0.0f, 0.0f, 640.0f, 448.0f, 1.0f, 1.0f, 1.0f, 1.0f}) putf(g, f);   // the object space, colour white
    for (int k = 0; k < 4; ++k) p32(g, 0);
    for (int k = 0; k < 5; ++k) p32(g, 0);  // no rasters
    for (int k = 0; k < 16; ++k) putf(g, (k % 5) == 0 ? 1.0f : 0.0f);
    for (float f : {0.6f, 0.45f, 0.0f, 0.0f}) putf(g, f);
    putf(g, 0.1f);
    putf(g, 10000.0f);
    s.event(14, g);
    s.event(5, {});                       // Suspend: what was pending is flushed
    s.event(7, sprite);
    std::vector<std::uint8_t> r;
    p32(r, 0);
    s.event(4, r);                        // Resume
    s.event(7, sprite);
    b3replay::HudDecoder dec;
    const auto f = decode(s, 3, dec);
    // the two sprites before the globe share one draw; the one after it starts its own (same state, not merged)
    CHECK(f && f->globes.size() == 1 && f->draws.size() == 2);
    if (!f || f->globes.size() != 1) return;
    const auto& hg = f->globes[0];
    CHECK(hg.draw_index == 1);
    CHECK(hg.lat == 0x4000 && hg.lon == 0x8000 && hg.reveal == 0xFF && hg.region == -1);
    CHECK_NEAR(hg.pos[2], 500, 0);
    CHECK_NEAR(hg.k[0], 0.6, 1e-7);
    // the static grids: unit sphere, poles at rows 0 / 32, the equator at row 16; the flat map spans x +-2, y +-1
    const auto& gr = b3replay::globe_grids();
    for (int i : {0, 16 * 65 + 7, 32 * 65 + 64, 20 * 65 + 33}) {
        const float* p = gr.sphere[i];
        CHECK_NEAR(p[0] * p[0] + p[1] * p[1] + p[2] * p[2], 1, 1e-5);
    }
    CHECK_NEAR(gr.sphere[0][1], -1, 1e-5);
    CHECK_NEAR(gr.sphere[32 * 65][1], 1, 1e-5);
    CHECK_NEAR(gr.sphere[16 * 65 + 9][1], 0, 1e-5);
    CHECK_NEAR(gr.plane[0][0], -2, 1e-6);
    CHECK_NEAR(gr.plane[32 * 65 + 64][0], 2, 1e-6);
    CHECK_NEAR(gr.plane[32 * 65 + 64][1], 1, 1e-6);
    const auto bs = b3replay::globe_batches(hg);
    // longitude 180 - 28.125 (the spin, shape 0xFFFF): cell 27, odd: 3 invisible strips; meridians (the 32 odd columns),
    // polar ends (16, every fourth from 1), parallels (17 rows); 65 surface strips; no highlight (region -1); 4 halo
    // quads (the reveal angle 360)
    std::size_t lines = 0, segs = 0, strips = 0, surf = 0, halo = 0;
    for (const auto& b : bs) {
        if (b.kind == b3replay::GlobeBatch::kLines) { ++lines; segs += b.v.size() / 2; CHECK(!b.zwrite && b.antialias); }
        else ++strips;
        if (b.texture == 0) { ++surf; CHECK(b.v.size() == 66 && b.cull); }
        if (b.texture == 4) { ++halo; CHECK(b.v.size() == 4); }
    }
    CHECK(lines == 32 + 16 + 17);
    CHECK(segs == 32 * 28 + 16 * 4 + 17 * 64);
    CHECK(surf == 65 && halo == 4 && strips == 3 + 65 + 4);
    // the wireframe colour: (0.8157, 0.898, 1, 1) x 128, truncated, alpha 0x80, doubled by RenderLinelist, capped
    const auto& lb = bs[3];
    CHECK(lb.kind == b3replay::GlobeBatch::kLines);
    CHECK(lb.v[0].rgba[0] == 208 && lb.v[0].rgba[1] == 228 && lb.v[0].rgba[2] == 255 && lb.v[0].rgba[3] == 128);
    // the depth rectangle around the centre at its depth: +-0.8125 x 0.6 x 500, +-1.0833 x 0.45 x 500
    const auto& rect = bs[2];
    CHECK_NEAR(rect.v[0].pos[0], 0.8125 * 0.6 * 500, 1e-3);
    CHECK_NEAR(rect.v[0].pos[1], 1.0833333730697632 * 0.45 * 500, 1e-3);
    CHECK_NEAR(rect.v[0].pos[2], 500, 0);
    // the surface's points lie on the radius-100 sphere around the centre
    for (const auto& b : bs)
        if (b.texture == 0)
            for (const auto& v : b.v) {
                const float dx = v.pos[0], dy = v.pos[1], dz = v.pos[2] - 500.0f;
                CHECK_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), 100, 1e-2);
                break;
            }
}

TEST(frontend_segments_between_races) {
    // menu1: the runs of HUD ticks outside the race segments, at least kMinFrontEndTicks long
    std::vector<b3replay::Tick> ticks(200);
    const auto hud = std::make_shared<b3replay::HudFrame>();
    for (std::size_t i = 0; i < ticks.size(); ++i) { ticks[i].poll = std::uint32_t(i + 1); if (i != 150) ticks[i].hud = hud; }
    std::vector<b3replay::Segment> race = {b3replay::Segment{40, 80, 1, 1}, b3replay::Segment{100, 130, 1, 1}};
    const auto fe = b3replay::frontend_segments(ticks, race);
    // 0..39 (40 ticks), 81..99 (19: too short), 131..149 (19, cut by the tick without a HUD frame), 151..199 (49)
    CHECK(fe.size() == 2);
    if (fe.size() != 2) return;
    CHECK(fe[0].first == 0 && fe[0].last == 39);
    CHECK(fe[1].first == 151 && fe[1].last == 199);
}

namespace {
// a draw of quads (two triangles each) {x0, y0, x1, y1, u0, u1}
b3replay::HudDraw quads(std::uint64_t texture, std::initializer_list<std::array<float, 6>> qs) {
    b3replay::HudDraw d;
    d.texture = texture;
    for (const auto& q : qs) {
        const float x[4] = {q[0], q[2], q[2], q[0]}, y[4] = {q[1], q[1], q[3], q[3]}, u[4] = {q[4], q[5], q[5], q[4]};
        for (int k : {0, 1, 2, 0, 2, 3}) { b3replay::HudVertex v; v.x = x[k]; v.y = y[k]; v.u = u[k]; v.v = y[k] / 16; d.tri.push_back(v); }
    }
    return d;
}
}  // namespace

TEST(frontend_wide_layout_rules) {
    // wide1: which front-end draws extend at wide aspects, and how (menu-menu1 / inproc2's draws, simplified)
    const float right = 640, ext = 100;
    float x, u, v;
    // the grid: one quad 0..656, REPEAT, 16 2D px per tile (u 0..41): extends, tiles keep their size
    const auto grid = quads(1, {{0, 0, 656, 463, 0, 41}});
    auto r = b3replay::hud_frontend_rule(grid, right);
    CHECK(r.span && !r.fill);
    b3replay::hud_frontend_vertex(grid, r, 0, right, ext, true, x, u, v);
    CHECK_NEAR(x, -100, 1e-4);
    CHECK_NEAR(u, -100.0f * 41 / 656, 1e-4);
    b3replay::hud_frontend_vertex(grid, r, 1, right, ext, true, x, u, v);
    CHECK_NEAR(x, 756, 1e-4);
    CHECK_NEAR(u, 41 + 100.0f * 41 / 656, 1e-4);
    // a clamped full-width strip (the backdrop `bg`): edges out, stretched
    const auto bg = quads(2, {{0, 388, 640, 448, 0, 0.625f}});
    r = b3replay::hud_frontend_rule(bg, right);
    CHECK(r.span);
    b3replay::hud_frontend_vertex(bg, r, 1, right, ext, false, x, u, v);
    CHECK_NEAR(x, 740, 1e-4);
    CHECK_NEAR(u, 0.625f, 1e-6);
    // the selection's ghost text: glyphs from -192 to 706, none spanning by itself: stays
    const auto ghost = quads(3, {{-192, 138, -150, 163, 0, 1}, {300, 138, 340, 163, 0, 1}, {660, 138, 706, 163, 0, 1}});
    r = b3replay::hud_frontend_rule(ghost, right);
    CHECK(!r.span && !r.fill);
    // the logo 0..320 and a band's end curve 608..640 (textured, one edge): stay
    CHECK(!b3replay::hud_frontend_rule(quads(4, {{0, 22, 320, 97, 0, 1}}), right).span);
    CHECK(!b3replay::hud_frontend_rule(quads(4, {{608, 329, 640, 359, 0, 1}}), right).span);
    // the top band's body 76..640 (untextured): its right end extends, its left end stays on the curve
    const auto body = quads(0, {{76, 22, 640, 67, 0, 0}});
    r = b3replay::hud_frontend_rule(body, right);
    CHECK(r.fill && !r.span);
    b3replay::hud_frontend_vertex(body, r, 0, right, ext, false, x, u, v);
    CHECK_NEAR(x, 76, 1e-6);
    b3replay::hud_frontend_vertex(body, r, 1, right, ext, false, x, u, v);
    CHECK_NEAR(x, 740, 1e-4);
    // the Crash Nav crosshair's vertical line past the right edge (639.9..644.9): neither
    r = b3replay::hud_frontend_rule(quads(0, {{639.9f, 0, 644.9f, 388, 0, 0}}), right);
    CHECK(!r.span && !r.fill);
    // a movie's tiles never extend
    auto movie = quads(0, {{0, 60, 640, 388, 0, 1}});
    movie.video = 0;
    r = b3replay::hud_frontend_rule(movie, right);
    CHECK(!r.span && !r.fill);
    // the ticker inside its clip region (scissor 16..623) is in a clip region (no edge vote: the d-pad no longer jumps)
    auto ticker = quads(5, {{600, 366, 640, 381, 0, 1}});
    ticker.scissor[0] = 16; ticker.scissor[1] = 360; ticker.scissor[2] = 623; ticker.scissor[3] = 385;
    CHECK(b3replay::hud_in_clip_region(ticker, right));
    auto full = ticker;
    full.scissor[0] = 0; full.scissor[2] = 639;
    CHECK(!b3replay::hud_in_clip_region(full, right));
}

TEST(frontend_translucent_layer_spans) {
    // mac5: the garage's TV static: full width, clamped, rough, alpha 106 -> covers the window; opaque (128) -> a picture
    const float right = 640, ext = 100;
    b3replay::HudFrame f;
    b3replay::HudObject o; o.pc = 0x001D5FB0U; f.objects.push_back(o);
    auto mk = [&](std::uint8_t a) { auto d = quads(20, {{0, 0, 640, 448, 0, 1}}); d.object = 0; d.wms = 1; for (auto& v : d.tri) v.rgba[3] = a; return d; };
    f.draws.push_back(mk(106));
    f.draws.push_back(mk(128));
    auto plan = b3replay::hud_frontend_plan(f, right, ext, [](const b3replay::HudDraw&) { return 82.0f; });
    CHECK(plan.rule[0].span && plan.rule[0].tile);
    CHECK(!plan.rule[1].span && !plan.rule[1].tile && plan.backdrop == 1);
    // mac5 (Windows, the car that never loaded): the opaque static is the static too when its texture says so
    plan = b3replay::hud_frontend_plan(f, right, ext, [](const b3replay::HudDraw&) { return 82.0f; },
                                       [](const b3replay::HudDraw& d) { return d.texture == 20; });
    CHECK(plan.rule[1].span && plan.rule[1].tile && plan.backdrop == -1);
}

TEST(frontend_panel_layout_plan) {
    // fe3: the panel layout's per-class decisions (home1's draws, simplified); roughness by texture id
    const float right = 640, ext = 100;
    b3replay::HudFrame f;
    auto obj = [&](std::uint32_t pc) { b3replay::HudObject o; o.pc = pc; o.addr = 0x01E90000U + std::uint32_t(f.objects.size()) * 0x50; f.objects.push_back(o); return int(f.objects.size()) - 1; };
    auto add = [&](b3replay::HudDraw d, int object) { d.object = object; for (int k = 0; k < 4; ++k) d.scissor[k] = k == 2 ? 639 : k == 3 ? 447 : 0; f.draws.push_back(d); return f.draws.size() - 1; };
    const int prompt = obj(0x003D7B40U), heading = obj(0x0031B130U), box = obj(0x001D5EA0U), ticker = obj(0x0031C990U), sel = obj(0x003720F0U),
              image = obj(0x001D5FB0U), flash = obj(0x00371BE0U);
    const auto band = add(quads(10, {{0, 359, 640, 448, 0.969f, 1}}), prompt);       // small_curve's strip: smooth
    const auto line = add(quads(0, {{0, 357, 608, 359, 0, 0}}), prompt);             // the bar's line, ends at the cap
    const auto cap = add(quads(11, {{608, 329, 640, 359, 0, 1}}), prompt);           // small_curve's corner
    const auto body = add(quads(0, {{76, 22, 640, 67, 0, 0}}), heading);
    const auto curve = add(quads(12, {{0, 0, 272, 164, 0, 1}}), heading);            // big_curve: one edge, p99 76
    const auto hilite = add(quads(0, {{0, 52, 208, 249, 0, 0}}), box);               // the map's highlight box
    auto tk = quads(13, {{600, 366, 640, 381, 0, 1}});
    tk.scissor[0] = 16; tk.scissor[2] = 623;
    f.draws.push_back(tk);
    f.draws.back().object = ticker;
    f.draws.back().scissor[1] = 360; f.draws.back().scissor[3] = 385;
    const auto tki = f.draws.size() - 1;
    const auto bar = add(quads(14, {{-101, 173, 741, 186, 0.03f, 0.45f}}), sel);     // FE, p99 254
    const auto pic = add(quads(15, {{0, 60, 640, 418, 0, 1}}), image);               // a loading picture
    const auto bars = add(quads(16, {{140, 87, 640, 113, 0.66f, 1}}), image);        // loading_bars: one edge, p99 4
    const auto glyphs = add(quads(17, {{-164, 138, 678, 163, 0, 1}}), flash);         // a text line flying in
    auto rough = [](const b3replay::HudDraw& d) {
        switch (d.texture) { case 10: return 0.0f; case 11: return 202.0f; case 12: return 76.0f; case 14: return 254.0f; case 15: return 129.0f; case 16: return 4.0f; default: return 254.0f; }
    };
    auto plan = b3replay::hud_frontend_plan(f, right, ext, rough);
    CHECK(plan.rule.size() == f.draws.size());
    CHECK(plan.rule[band].span);
    CHECK(plan.rule[cap].move == ext && !plan.rule[cap].span);
    CHECK(plan.rule[line].fill && plan.rule[line].end_dx == ext && std::fabs(plan.rule[line].end_x - 608) < 1e-4f);
    float x, u, v;
    b3replay::hud_frontend_vertex(f.draws[line], plan.rule[line], 0, right, ext, false, x, u, v);
    CHECK_NEAR(x, -100, 1e-4);
    b3replay::hud_frontend_vertex(f.draws[line], plan.rule[line], 1, right, ext, false, x, u, v);
    CHECK_NEAR(x, 708, 1e-4);
    b3replay::hud_frontend_vertex(f.draws[cap], plan.rule[cap], 0, right, ext, false, x, u, v);
    CHECK_NEAR(x, 708, 1e-4);
    // mac5: big_curve's foot goes out to the window's left edge (0 -> -100), its top end half as far (272 -> 222); the
    // heading's body, whose left end (76) sits on it, moves with it there: -100 + 50 * 76 / 272
    CHECK(plan.rule[curve].stretch_dx == -ext && plan.rule[curve].stretch_dx_hi == -0.5f * ext && plan.rule[curve].stretch_hi == 272);
    b3replay::hud_frontend_vertex(f.draws[curve], plan.rule[curve], 0, right, ext, false, x, u, v);
    CHECK_NEAR(x, -100, 1e-4);
    b3replay::hud_frontend_vertex(f.draws[curve], plan.rule[curve], 1, right, ext, false, x, u, v);
    CHECK_NEAR(x, 222, 1e-4);
    CHECK(plan.rule[body].fill && std::fabs(plan.rule[body].end_x - 76) < 1e-4f);
    CHECK_NEAR(plan.rule[body].end_dx, -100.0f + 50.0f * 76 / 272, 1e-3);
    b3replay::hud_frontend_vertex(f.draws[body], plan.rule[body], 0, right, ext, false, x, u, v);
    CHECK_NEAR(x, 76 - 100.0f + 50.0f * 76 / 272, 1e-3);
    {   // the band's lines (left ends at 161, the same draw as the body): with the curve there too, no gap
        const auto lines = quads(0, {{161, 22, 640, 24, 0, 0}});
        b3replay::hud_frontend_vertex(lines, plan.rule[body], 0, right, ext, false, x, u, v);
        CHECK_NEAR(x, 161 - 100.0f + 50.0f * 161 / 272, 1e-3);
        b3replay::hud_frontend_vertex(lines, plan.rule[body], 1, right, ext, false, x, u, v);
        CHECK_NEAR(x, 740, 1e-3);
    }
    CHECK(!plan.rule[hilite].extends());
    CHECK(plan.rule[tki].fade && !plan.rule[tki].extends());
    CHECK(plan.rule[bar].span);
    CHECK(!plan.rule[pic].extends());
    CHECK(plan.rule[bars].span);
    CHECK(!plan.rule[glyphs].extends());
    CHECK(plan.backdrop == int(pic));   // no movie: the full-width picture
    {   // a selection bar in two halves (0..287 and 367..640, no triangle spanning; the memory-card dialog): never the backdrop
        b3replay::HudFrame g;
        b3replay::HudObject o;
        o.pc = 0x003720F0U;
        g.objects.push_back(o);
        auto halves = quads(14, {{0, 291, 287, 304, 0.45f, 0.42f}, {367, 291, 640, 304, 0.42f, 0.45f}});
        halves.object = 0;
        g.draws.push_back(halves);
        const auto gp = b3replay::hud_frontend_plan(g, right, ext, rough);
        CHECK(gp.backdrop == -1 && !gp.rule[0].extends());
    }
    CHECK_NEAR(plan.box[1], 60, 1e-4);
    // a movie reaching the edges is the backdrop (its last draw), before any picture
    auto mv = quads(0, {{0, 60, 320, 388, 0, 0.5f}});
    mv.video = 0;
    const auto m0 = add(mv, obj(0x00326960U));
    mv = quads(0, {{320, 60, 640, 388, 0.5f, 1}});
    mv.video = 0;
    const auto m1 = add(mv, f.draws[m0].object);
    plan = b3replay::hud_frontend_plan(f, right, ext, rough);
    CHECK(plan.backdrop == int(m1));
    CHECK_NEAR(plan.box[0], 0, 1e-4);
    CHECK_NEAR(plan.box[2], 640, 1e-4);
    CHECK(!plan.rule[m0].extends());
    // at 4:3 (ext 0) nothing moves
    plan = b3replay::hud_frontend_plan(f, right, 0, rough);
    CHECK(plan.backdrop == -1);
    for (const auto& r : plan.rule) CHECK(!r.extends() && !r.fade);
}

TEST(host_ui_png) {
    // wide1: a PNG (b3asset's writer) read back as the host UI layer reads its path textures
    const std::string path = "build/test_hostui.png";
    // PNG round trip (RGBA, 3 x 2)
    const std::uint8_t px[24] = {1, 2, 3, 4, 50, 60, 70, 80, 255, 0, 255, 128, 9, 8, 7, 6, 0, 0, 0, 0, 200, 100, 50, 255};
    CHECK(b3asset::write_png_rgba(path, 3, 2, px));
    int w = 0, h = 0;
    std::vector<std::uint8_t> back;
    CHECK(b3replay::read_png_rgba(path, w, h, back));
    CHECK(w == 3 && h == 2 && back.size() == 24 && std::memcmp(back.data(), px, 24) == 0);
    CHECK(!b3replay::read_png_rgba("build/no-such-file.png", w, h, back));
}

TEST(race_overlay_panels_shift_by_side) {
    // perf1 (§33): the pause menu over a race (fri2 poll 10150, simplified): hud2 keeps the bands and caps; the title and
    // the prompt row go right, the menu column left, the selection bar's brackets with it; under a dialog the column stays
    const float right = 640, ext = 100;
    auto build = [&](bool dialog, bool component) {
        b3replay::HudFrame f;
        auto obj = [&](std::uint32_t pc) { b3replay::HudObject o; o.pc = pc; f.objects.push_back(o); return int(f.objects.size()) - 1; };
        auto add = [&](b3replay::HudDraw d, int object) { d.object = object; f.draws.push_back(d); return f.draws.size() - 1; };
        const int prompt = obj(0x003D7B40U), heading = obj(0x0031B130U), title = obj(0x00381030U), sel = obj(0x003720F0U),
                  menu = obj(0x00371BE0U), text = obj(0x001D66A0U), panel = obj(0x003A2850U);
        add(quads(0, {{0, 387, 608, 389, 0, 0}}), prompt);
        add(quads(11, {{608, 359, 640, 389, 0, 1}}), prompt);
        add(quads(0, {{76, 22, 640, 67, 0, 0}}), heading);
        add(quads(12, {{0, 0, 272, 164, 0, 1}}), heading);
        const auto t = add(quads(17, {{486, 34, 607, 59, 0, 1}}), title);
        const auto bar = add(quads(14, {{0, 173, 71, 186, 0.45f, 0.45f}, {71, 173, 84, 186, 0.03f, 0.42f}, {206, 173, 219, 186, 0.03f, 0.42f},
                                        {219, 173, 640, 186, 0.42f, 0.45f}}), sel);
        const auto sel_item = add(quads(17, {{23, 168, 264, 193, 0, 1}}), menu);
        const auto item = add(quads(17, {{85, 200, 184, 220, 0, 1}}), menu);
        const auto dpad = add(quads(18, {{304, 403, 336, 433, 0, 1}}), menu);
        const auto change = add(quads(17, {{341, 411, 396, 428, 0, 1}}), text);
        const auto select = add(quads(17, {{532, 411, 579, 424, 0, 1}}), text);
        if (dialog) add(quads(0, {{63, 104, 578, 330, 0, 0}}), panel);
        if (component) {   // a HUD component draw: a race frame, not an overlay
            b3replay::HudComponent c; c.align = 0; f.components.push_back(c);
            f.objects[std::size_t(title)].component = 0;
        }
        return std::make_tuple(f, t, bar, sel_item, item, dpad, change, select);
    };
    {
        auto [f, t, bar, sel_item, item, dpad, change, select] = build(false, false);
        CHECK(b3replay::hud_overlay_panels(f));
        std::vector<bool> vw;
        const auto s = b3replay::hud_overlay_shifts(f, right, ext, &vw);
        CHECK(s[t] == ext);                                                 // the title, right
        CHECK(s[dpad] == ext && s[change] == ext && s[select] == ext);      // the prompt row moves as one
        CHECK(s[item] == -ext && s[sel_item] == -ext);                      // the menu column, left
        CHECK(s[bar] == -ext && vw[bar]);                                   // the selected row's bar, vertex-wise
        CHECK(s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0);            // bands and caps: hud2's
    }
    {
        auto [f, t, bar, sel_item, item, dpad, change, select] = build(true, false);
        const auto s = b3replay::hud_overlay_shifts(f, right, ext);
        CHECK(s[item] == 0 && s[sel_item] == 0);                            // under the quit dialog: stays
        CHECK(s[t] == ext && s[change] == ext);
    }
    {
        auto f = std::get<0>(build(false, true));
        CHECK(!b3replay::hud_overlay_panels(f));                            // the race HUD: not an overlay
    }
    {   // mac5: a row zooming in as it is selected runs past 280 with its left edge off the column's: still the column
        auto f = std::get<0>(build(false, false));
        b3replay::HudDraw d = quads(17, {{30, 230, 300, 255, 0, 1}});
        d.object = 4;   // the menu
        f.draws.push_back(d);
        const auto s = b3replay::hud_overlay_shifts(f, right, ext);
        CHECK(s.back() == -ext);
    }
    {   // mac5: a longer label zoomed past the 4:3 box's left edge (x < 0) is still the column, not an edge piece
        auto f = std::get<0>(build(false, false));
        b3replay::HudDraw d = quads(17, {{-14, 230, 300, 255, 0, 1}});
        d.object = 4;   // the menu
        f.draws.push_back(d);
        const auto s = b3replay::hud_overlay_shifts(f, right, ext);
        CHECK(s.back() == -ext);
    }
}

TEST(race_results_stat_labels_stay_on_their_plates) {
    // 1.2.1 (the known issue "Takedowns label at 32:9 on the record screen"): a race's results stats page (crashnav's
    // ELIMINATOR results, poll 17300, simplified): each stat a plate (func_003CD920) at 80..560 with its label at x 110
    // and its value at the right end. The labels lie left of 280 but are the plates': they stay with them; the
    // CONTINUE / RETRY page's menu column (left of 280, beside plates at 260..600) still goes left.
    const float right = 640, ext = 400;
    b3replay::HudFrame f;
    auto obj = [&](std::uint32_t pc) { b3replay::HudObject o; o.pc = pc; f.objects.push_back(o); return int(f.objects.size()) - 1; };
    auto add = [&](b3replay::HudDraw d, int object) { d.object = object; f.draws.push_back(d); return f.draws.size() - 1; };
    const int prompt = obj(0x003D7B40U), heading = obj(0x0031B130U), plate = obj(0x003CD920U), italic = obj(0x0033CCD0U),
              menu = obj(0x00371BE0U);
    add(quads(0, {{0, 387, 608, 389, 0, 0}}), prompt);
    add(quads(0, {{76, 22, 640, 67, 0, 0}}), heading);
    std::vector<std::size_t> plates, labels, values;
    for (float y : {204.4f, 260.4f, 316.4f}) {
        plates.push_back(add(quads(11, {{80, y, 560, y + 28, 0, 1}}), plate));
        labels.push_back(add(quads(17, {{110, y + 7, 228, y + 21, 0, 1}}), italic));
        values.push_back(add(quads(17, {{470, y + 3, 539, y + 25, 0, 1}}), italic));
    }
    {
        const auto s = b3replay::hud_overlay_shifts(f, right, ext);
        for (std::size_t k = 0; k < plates.size(); ++k) {
            CHECK(s[plates[k]] == 0);
            CHECK(s[labels[k]] == 0);   // was -ext: the label at the window's left edge, its plate centred
            CHECK(s[values[k]] == 0);
        }
    }
    {   // the next page: CONTINUE / RETRY (84..228) beside plates at 260..600 (BURNOUT POINTS, TAKEDOWNS at 290)
        b3replay::HudFrame g = f;
        g.draws.resize(2);
        auto add2 = [&](b3replay::HudDraw d, int object) { d.object = object; g.draws.push_back(d); return g.draws.size() - 1; };
        const auto cont = add2(quads(17, {{85, 291, 228, 317, 0, 1}}), menu);
        const auto retry = add2(quads(17, {{85, 323, 157, 344, 0, 1}}), menu);
        const auto p = add2(quads(11, {{260, 283, 600, 311, 0, 1}}), plate);
        const auto label = add2(quads(17, {{290, 290, 425, 304, 0, 1}}), italic);
        const auto s = b3replay::hud_overlay_shifts(g, right, ext);
        CHECK(s[cont] == -ext && s[retry] == -ext);
        CHECK(s[p] == 0 && s[label] == 0);
    }
}

// menu-font: the ticker laid out again over a wide strip (hud_ticker_layout): ext 0 is the game's layout
// (CB3ScrollingText2dObject::_Render / _BuildBatchData), a wider strip shifts the text by ext and fills the added width
TEST(ticker_layout_wide_strip) {
    b3replay::HostFont f;
    f.raster = 0x1234;
    f.scale_x = f.scale_y = 1.0f / 28.0f;   // size = scale x 28 = 1 unit per font unit at scale 1
    b3replay::HostFontGlyph a;
    a.code = 'A'; a.u = 0.0f; a.v = 0.0f; a.w = 8.0f; a.h = 10.0f; a.ox = 0.0f; a.oy = 0.0f; a.advance = 10.0f;
    b3replay::HostFontGlyph sp;
    sp.code = ' '; sp.u = -1.0f; sp.advance = 6.0f;
    f.glyphs = {sp, a};
    b3replay::HudObject o;
    o.pos[0] = 16.0f; o.pos[1] = 400.0f; o.size[0] = 608.0f; o.size[1] = 20.0f;
    o.ticker_recip[0] = 1.0f / 608.0f; o.ticker_recip[1] = 1.0f / 20.0f;   // scale (1, 1)
    o.ticker_font = 0x1234;
    o.ticker_text = u"AA A";
    o.ticker_gap = 40.0f;
    o.ticker_extent = 3 * 10.0f + 6.0f + 40.0f;   // the period: the advances and the gap (76)
    const std::uint8_t rgba[4] = {0x80, 0x80, 0x80, 0x80};
    // the glyph quads' top-left corners (less the slant, 0.36 x (bottom - y); on the GS's 1/16 grid)
    auto tops = [&](float ext, float scroll) {
        o.ticker_scroll = scroll;
        std::vector<b3replay::HudVertex> v;
        CHECK(b3replay::hud_ticker_layout(o, f, 1.0f, 1.0f, ext, rgba, 0, v));
        CHECK(v.size() % 3 == 0);
        std::vector<float> xs;
        for (const b3replay::HudVertex& h : v)
            if (h.y == 400.0f && h.u == 0.0f) xs.push_back(std::round((h.x - 0.36f * 20.0f) * 8.0f) / 8.0f);
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        return xs;
    };
    {   // scroll 0: the text starts at the strip's right end (624; the clip hides it), one word drawn
        const auto xs = tops(0.0f, 0.0f);
        CHECK(xs.size() == 2 && xs[0] == 624.0f && xs[1] == 634.0f);
        const auto wide = tops(100.0f, 0.0f);   // the wide strip's right end
        CHECK(wide.size() == 2 && wide[0] == 724.0f);
    }
    {   // scroll 15 (1500 - 608 > 608: wrapped by the period): the strip is full of repetitions; the wide layout is the
        // game's shifted by ext (to the period), over the wide strip
        const float ext = 100.0f;
        const auto xs = tops(0.0f, 15.0f), wide = tops(ext, 15.0f);
        CHECK(!xs.empty() && xs.front() <= 16.0f && xs.back() >= 624.0f - 76.0f);
        CHECK(!wide.empty() && wide.front() <= 16.0f - ext && wide.back() >= 624.0f + ext - 76.0f);
        for (float x : xs) {
            bool found = false;
            for (float w : wide)
                for (float k : {-76.0f, 0.0f, 76.0f}) found = found || std::fabs(w - (x + ext + k)) < 0.125f;
            CHECK(found);
        }
    }
    {   // no record: nothing laid out
        b3replay::HudObject none;
        std::vector<b3replay::HudVertex> v;
        CHECK(!b3replay::hud_ticker_layout(none, f, 1.0f, 1.0f, 0.0f, rgba, 0, v) && v.empty());
    }
}

// wsmenu1: the PC settings page's cover quads go out to the window's edges (the picture units it shows), 4:3 unchanged.
TEST(host_ui_window_extent) {
    b3replay::HostUiExtent e = b3replay::host_ui_extent(4.0f / 3.0f);
    CHECK_NEAR(e.left, 0, 1e-3); CHECK_NEAR(e.right, 640, 1e-3); CHECK_NEAR(e.top, 0, 1e-3); CHECK_NEAR(e.bottom, 448, 1e-3);
    e = b3replay::host_ui_extent(16.0f / 9.0f);   // 16:9: the box is 3/4 of the width: 853 units across
    CHECK_NEAR(e.left, -106.67f, 0.1f); CHECK_NEAR(e.right, 746.67f, 0.1f); CHECK_NEAR(e.top, 0, 1e-3); CHECK_NEAR(e.bottom, 448, 1e-3);
    e = b3replay::host_ui_extent(32.0f / 9.0f);   // 32:9: 1707 units, still inside the grid's repeats (-1280 .. 1920)
    CHECK(e.left > -1280 && e.right < 1920 && e.left < -450 && e.right > 1090);
    e = b3replay::host_ui_extent(9.0f / 16.0f);   // portrait: the box is the width, the height grows
    CHECK_NEAR(e.left, 0, 1e-3); CHECK_NEAR(e.right, 640, 1e-3); CHECK(e.top < -300 && e.bottom > 750);
}

TEST_MAIN("test_hud")
