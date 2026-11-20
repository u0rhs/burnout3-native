// Device::resource_epoch: every 2D panel that keeps buffer / texture ids between frames (the notice, the skip hint, the
// speedrun timer, the car look and tuning panels' box, photo mode's plates, the code mods' overlay) makes them again after
// a track load (View::clear -> clear_resources restarts the ids from 0, and the game's own then take them). Before, such a
// panel drew with a game texture and wrote its quads into a game buffer (the FPS counter in Crash mode, tests/test_fpspanel.cpp).
#include <functional>
#include <string>
#include <vector>

#include "carlookpanel.h"
#include "modoverlay.h"
#include "notice.h"
#include "photohud.h"
#include "skiphint.h"
#include "speedrunpanel.h"
#include "test.h"

using namespace b3replay;

namespace {

// A device with a real id table, as SdlDevice's: clear_resources restarts the ids from 0
struct IdDev : b3gfx::Device {
    unsigned epoch = 0;
    std::size_t bufs = 0, texs = 0;
    int stale = 0;   // an update through an id the device does not hold
    const char* driver_name() const override { return "ids"; }
    int msaa() const override { return 1; }
    const char* present_mode_name() const override { return ""; }
    int frames_in_flight() const override { return 0; }
    void wait_swapchain() override {}
    unsigned resource_epoch() const override { return epoch; }
    b3gfx::BufferId create_vertex_buffer(const b3gfx::Vertex*, std::size_t) override { return b3gfx::BufferId(bufs++); }
    b3gfx::BufferId create_index_buffer(const std::uint32_t*, std::size_t) override { return b3gfx::BufferId(bufs++); }
    b3gfx::TextureId create_texture(const std::vector<b3gfx::TextureLevel>&) override { return b3gfx::TextureId(texs++); }
    void update_texture(b3gfx::TextureId& id, int, int, const std::uint8_t*) override {
        if (id == b3gfx::kNone) id = b3gfx::TextureId(texs++);
        else if (id >= texs) ++stale;
    }
    void clear_resources() override { ++epoch; bufs = texs = 0; }
    void upd(b3gfx::BufferId& id) {
        if (id == b3gfx::kNone) id = b3gfx::BufferId(bufs++);
        else if (id >= bufs) ++stale;
    }
    void update_vertex_buffer(b3gfx::BufferId& id, const b3gfx::Vertex*, std::size_t) override { upd(id); }
    void update_index_buffer(b3gfx::BufferId& id, const std::uint32_t*, std::size_t) override { upd(id); }
    bool draw_frame(const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, int*, int*) override { return true; }
    bool draw_offscreen(int, int, const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, std::vector<std::uint8_t>&) override { return true; }
    bool draw_views(const std::vector<ViewPass>&, const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, int*, int*) override { return true; }
    bool draw_views_offscreen(int, int, const std::vector<ViewPass>&, const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, std::vector<std::uint8_t>&) override { return true; }
};

constexpr std::size_t kGame = 12;   // the game's own buffers and textures after a track load

// `draw` appends a panel's items for a frame. Draw, load a "track", draw: the items must name the panel's own new
// resources (ids at or past the game's), never the game's, and nothing may be written through an id the device lacks.
void survives_a_track_load(const char* what, const std::function<void(b3gfx::Device&, std::vector<b3gfx::DrawItem>&)>& draw) {
    IdDev dev;
    std::vector<b3gfx::DrawItem> items;
    draw(dev, items);
    draw(dev, items);
    CHECK(!items.empty());
    if (items.empty()) std::printf("  (%s drew nothing)\n", what);
    dev.clear_resources();
    for (std::size_t k = 0; k < kGame; ++k) { dev.create_vertex_buffer(nullptr, 0); dev.create_texture({}); }
    for (int frame = 0; frame < 3; ++frame) {
        items.clear();
        draw(dev, items);
        CHECK(!items.empty());
        for (const b3gfx::DrawItem& it : items) {
            CHECK(it.texture >= kGame && it.texture < dev.texs);
            CHECK(it.vertices >= kGame && it.vertices < dev.bufs && it.indices >= kGame && it.indices < dev.bufs);
        }
        CHECK(dev.stale == 0);
    }
}

}  // namespace

TEST(notice) {
    Notice n;
    n.show({"A NOTICE"}, 1000.0f);
    survives_a_track_load("notice", [&](b3gfx::Device& d, std::vector<b3gfx::DrawItem>& o) { n.draw_list(d, 1280, 720, 0.01f, o); });
}

TEST(skip_hint) {
    SkipHint s;
    survives_a_track_load("skip hint", [&](b3gfx::Device& d, std::vector<b3gfx::DrawItem>& o) { s.draw_list(d, 1280, 720, 1.0f, PromptDevice::kKeyboard, o); });
}

TEST(speedrun_panel) {
    SpeedrunPanel p;
    const std::vector<speedrun::Row> rows = {{"TIME", "1:23.45", speedrun::Row::kWhite}, {"BEST", "1:20.00", speedrun::Row::kGold}};
    survives_a_track_load("speedrun panel", [&](b3gfx::Device& d, std::vector<b3gfx::DrawItem>& o) { p.draw_list(d, 1280, 720, rows, o); });
}

TEST(car_look_panel) {
    CarLookPanel& p = CarLookPanel::instance();
    p.set_enabled(true);
    p.open_now(true);
    survives_a_track_load("car look panel", [&](b3gfx::Device& d, std::vector<b3gfx::DrawItem>& o) { p.draw_list(d, 1280, 720, o); });
    p.open_now(false);
    p.set_enabled(false);
}

TEST(photo_hud) {
    PhotoHud hud;
    const PhotoKeys keys;
    survives_a_track_load("photo hud", [&](b3gfx::Device& d, std::vector<b3gfx::DrawItem>& o) {
        hud.draw_list(d, 1280, 720, 1.0f, PhotoUi::kFull, PhotoHudData{}, PromptDevice::kKeyboard, keys, 0, o);
    });
}

TEST(mod_overlay) {
    ModOverlay& m = ModOverlay::instance();
    const std::uint8_t px[4 * 4] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    m.add_texture(7, 2, 2, px);
    b3mod::OverlayList list;
    b3mod::OverlayItem q;
    q.kind = 1; q.x = 10; q.y = 10; q.w = 50; q.h = 50; q.texture = 7;
    b3mod::OverlayItem t;
    t.kind = 2; t.x = 100; t.y = 100; t.h = 20; t.text = "HI";
    list.items = {q, t};
    survives_a_track_load("mod overlay", [&](b3gfx::Device& d, std::vector<b3gfx::DrawItem>& o) { m.draw_list(d, 1280, 720, list, o); });
    // the mod's picture was uploaded once, given once: after the load it is made again (its quad has its own texture, not the white one)
    IdDev dev;
    std::vector<b3gfx::DrawItem> items;
    m.draw_list(dev, 1280, 720, list, items);
    dev.clear_resources();
    items.clear();
    m.draw_list(dev, 1280, 720, list, items);
    CHECK(items.size() == 2 && items[0].texture != items[1].texture);
    CHECK(dev.texs == 2 && dev.stale == 0);
    // modsec: dropped (its mod stopped): its quad is no longer drawn, and a later clear does not bring it back
    m.add_texture(7, 0, 0, nullptr);
    items.clear();
    m.draw_list(dev, 1280, 720, list, items);
    CHECK(items.size() == 1);
    dev.clear_resources();
    items.clear();
    m.draw_list(dev, 1280, 720, list, items);
    CHECK(items.size() == 1 && dev.stale == 0);
}

// The draw list copies a DrawItem per draw (~2,000 a frame): the car's ~700-byte parameter block is held by pointer, so a track
// item stays small, and a copy of a car item never shares a block it then changes (car_mut unshares it).
TEST(draw_item_stays_small_and_car_params_copy_on_write) {
    CHECK(sizeof(b3gfx::DrawItem) <= 320);
    b3gfx::DrawItem a;
    CHECK(!a.car_ && a.car().fade == 1.0f && a.car().zones == 0);
    a.car_mut().fade = 0.25f;
    b3gfx::DrawItem b = a;
    CHECK(b.car().fade == 0.25f);
    b.car_mut().fade = -0.5f;
    CHECK(a.car().fade == 0.25f && b.car().fade == -0.5f);
    b3gfx::DrawItem t;   // an item that never was a car costs no block
    b3gfx::DrawItem t2 = t;
    CHECK(!t2.car_);
}

// Device::create_texture_hd: an HD replacement the device refuses (a side past its limit, out of memory) gives the
// original texture, not kNone (drawn white); the pair is only recorded when both exist
struct LimitDev : IdDev {
    int limit = 64;
    std::vector<int> made_w;
    int pairs = 0;
    b3gfx::TextureId create_texture(const std::vector<b3gfx::TextureLevel>& l) override {
        if (l.empty() || l[0].w > limit || l[0].h > limit) return b3gfx::kNone;
        made_w.push_back(l[0].w);
        return b3gfx::TextureId(texs++);
    }
    void set_texture_original(b3gfx::TextureId, b3gfx::TextureId, std::size_t, std::size_t) override { ++pairs; }
};
TEST(hd_texture_creation_failure_falls_back_to_the_original) {
    std::vector<std::uint8_t> px(std::size_t(128) * 128 * 4, 0x80);
    LimitDev dev;
    const b3gfx::TextureId big = dev.create_texture_hd({{128, 128, px.data()}}, {{32, 32, px.data()}});
    CHECK(big != b3gfx::kNone);
    CHECK(dev.made_w.size() == 1 && dev.made_w[0] == 32);   // the original, alone
    CHECK(dev.pairs == 0);
    const b3gfx::TextureId ok = dev.create_texture_hd({{64, 64, px.data()}}, {{32, 32, px.data()}});
    CHECK(ok != b3gfx::kNone && dev.pairs == 1 && dev.made_w.size() == 3);
    CHECK(dev.create_texture_hd({{128, 128, px.data()}}, {}) == b3gfx::kNone);   // no original: none, as before
}

TEST_MAIN("b3replay panels after a track load (Device::resource_epoch)")
