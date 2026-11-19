// Capture decoding rules checked on synthetic guest images (b3replay/capture.cpp).
// cam1 (docs/native-renderer.md §26): RenderRaceCars 0x199A20 leaves out the view's own race car while its camera
// manager's IsPlayerVisible 0x2E7340 is false; the look-back camera (L1, camera behaviour 4) sets that state.
#include "test.h"

#include "../src/b3replay/capture.h"

#include <tuple>
#include <utility>

namespace {
using b3replay::Image;

constexpr std::uint32_t kWorld = 0x522660, kPcm = 0x4EE040 + 0x28E00;
constexpr std::uint32_t kCarA = 0x01000000, kCarB = 0x01010000, kData = 0x01020000;

void put32(std::vector<std::uint8_t>& b, std::uint32_t off, std::uint32_t v) { std::memcpy(b.data() + off, &v, 4); }
void putf(std::vector<std::uint8_t>& b, std::uint32_t off, float v) { std::memcpy(b.data() + off, &v, 4); }

// A race car: identity matrix at +0x00, vehicle data +0x40, drawn (+0x18E8), +0x1914 and +0x27C0 as given.
void car(Image& im, std::uint32_t at, std::uint32_t f1914, std::uint32_t view) {
    std::vector<std::uint8_t> b(0x2800, 0);
    putf(b, 0x00, 1); putf(b, 0x14, 1); putf(b, 0x28, 1); putf(b, 0x3C, 1);
    put32(b, 0x40, kData);
    b[0x18E8] = 1;
    put32(b, 0x1914, f1914);
    put32(b, 0x27C0, view);
    im.write(at, b.data(), std::uint32_t(b.size()));
}

// Two race cars (the player's: +0x1914 0, view 0; a rival: +0x1914 set) with view 0's camera manager flags +0xB80.
void scene(Image& im, std::uint32_t flags) {
    std::vector<std::uint8_t> w(0x40, 0);   // gWorld + 0x12AD1C (count) .. + 0x12AD28 (cars), from 0x64D370
    put32(w, 0x0C, 2);
    put32(w, 0x18, kCarA);
    put32(w, 0x1C, kCarB);
    im.write(kWorld + 0x12AD1C - 0xC, w.data(), std::uint32_t(w.size()));
    std::vector<std::uint8_t> d(0x70, 0);
    im.write(kData, d.data(), std::uint32_t(d.size()));
    car(im, kCarA, 0, 0);
    car(im, kCarB, 1, 0);
    std::vector<std::uint8_t> fx(0x10, 0);   // the effects manager block decode_effects requires
    im.write(0x1D9D200 + 0x2600 + 0x14630, fx.data(), 0x10);
    std::vector<std::uint8_t> m(0x10, 0);
    put32(m, 0, flags);
    im.write(kPcm + 0xB80, m.data(), 0x10);
    std::vector<std::uint8_t> m2(0x30, 0);
    im.write(kPcm + 0x9E0, m2.data(), 0x30);
}

const b3replay::Vehicle* find(const b3replay::Tick& t, std::uint32_t id) {
    for (const auto& v : t.vehicles) if (v.id == id) return &v;
    return nullptr;
}

}  // namespace

TEST(capture_own_car_hidden_while_player_not_visible) {
    for (std::uint32_t flags : {0x0u, 0x10u, 0x20u, 0x400u, 0x8u}) {
        Image im;
        scene(im, flags);
        b3replay::Capture cap;
        b3replay::Tick t;
        cap.decode(im, t);
        const auto *a = find(t, kCarA), *b = find(t, kCarB);
        CHECK(a != nullptr && b != nullptr);
        if (!a || !b) continue;
        const bool hidden = (flags & 0x30) != 0;   // IsPlayerVisible: bits 4 / 5 hide; 0x400 alone with no timer passes
        CHECK(t.player_visible == !hidden);
        CHECK(a->own_hidden == hidden);   // the view's own car follows IsPlayerVisible
        CHECK(!b->own_hidden);            // +0x1914 set: always listed
    }
}

TEST(capture_other_view_car_not_hidden) {
    Image im;
    scene(im, 0x10);
    car(im, kCarA, 0, 1);   // +0x27C0 = view 1 (split screen's other player): listed for view 0
    b3replay::Capture cap;
    b3replay::Tick t;
    cap.decode(im, t);
    const auto* a = find(t, kCarA);
    CHECK(a != nullptr && !a->own_hidden);
}

// sigcam (1.2): Camera::chase, the share of the drawn-only chase camera offset (View::apply_fov) the game camera takes,
// from the camera manager's active behaviours +0xB80: the chase slots 2 / 3 take all of it, the takedown camera (slot 1),
// the own crash (6) and the rest none; a blend (slot 10, 0x400) from slot +0x9F0 to +0x9F4 over +0x9E8 seconds takes
// it in step (GetTimeElapsed's case 2: +0x9EC - +0x9E4, its kind at +0xA04); a capture without the manager keeps 1.
TEST(capture_chase_share_follows_the_game_camera) {
    for (const auto& [flags, share] : {std::pair{0x4u, 1.0f}, {0x8004u, 1.0f}, {0x8u, 1.0f}, {0x2u, 0.0f}, {0x8002u, 0.0f},
                                       {0x40u, 0.0f}, {0x10u, 0.0f}, {0x0u, 0.0f}}) {
        Image im;
        scene(im, flags);
        b3replay::Capture cap;
        b3replay::Tick t;
        cap.decode(im, t);
        CHECK(t.cam.chase == share);
    }
    // blends: chase -> takedown camera, takedown camera -> chase, crash -> crash, at a quarter of their 0.5 s
    for (const auto& [from, to, share] : {std::tuple{2u, 1u, 0.75f}, {1u, 2u, 0.25f}, {7u, 6u, 0.0f}, {3u, 2u, 1.0f}}) {
        Image im;
        scene(im, 0x400);
        std::vector<std::uint8_t> m(0x30, 0);
        putf(m, 0x9E4 - 0x9E0, 10.0f);     // the blend's start
        putf(m, 0x9E8 - 0x9E0, 0.5f);      // its length
        putf(m, 0x9EC - 0x9E0, 10.125f);   // now
        put32(m, 0x9F0 - 0x9E0, from);
        put32(m, 0x9F4 - 0x9E0, to);
        put32(m, 0xA04 - 0x9E0, 2);        // GetTimeElapsed's kind: the behaviour's own clock
        im.write(kPcm + 0x9E0, m.data(), 0x30);
        b3replay::Capture cap;
        b3replay::Tick t;
        cap.decode(im, t);
        CHECK(std::fabs(t.cam.chase - share) < 1e-5f);
    }
    Image im;   // no camera manager captured (older recordings): the offset as before
    std::vector<std::uint8_t> fx(0x10, 0);
    im.write(0x1D9D200 + 0x2600 + 0x14630, fx.data(), 0x10);
    b3replay::Capture cap;
    b3replay::Tick t;
    cap.decode(im, t);
    CHECK(t.cam.chase == 1.0f);
}

// perf1 (docs/native-renderer.md §33): CB3GameData::Draw(0) 0x14E8C0's finish lines. A race logic's
// GetCheckpointMarkerFlags (vtable +0x44) returns 3: the finish line shows on the last lap once +0x139C > 0; 0x12
// (Burning Lap) shows it from lap 0, as "check" until the last lap; crash mode shows none.
namespace {
constexpr std::uint32_t kGameData = kWorld + 0x125B40, kTd = 0x01100000, kTrig = 0x01110000, kGame = 0x01120000,
                        kLogic = 0x01121000, kVt = 0x01122000, kFn = 0x01123000;
void finish_scene(Image& im, std::uint32_t flags, int lap, int laps, int n139c, bool crash, bool statics = true) {
    scene(im, 0);
    std::vector<std::uint8_t> g(0x590, 0);
    put32(g, 0x3A8, kTrig); put32(g, 0x3AC, 0x00AA0000); put32(g, 0x3B0, 0x00BB0000); g[0x3B4] = 1;   // one finish line
    put32(g, 0x388, kTrig); g[0x394] = 0;                                                                // a start line (+0xC 0)
    put32(g, 0x560, 1); put32(g, 0x56C, kTd);
    im.write(kGameData, g.data(), std::uint32_t(g.size()));
    std::vector<std::uint8_t> z(0x10, 0);
    im.write(0x665EA0, z.data(), 0x10);
    std::vector<std::uint8_t> td(0x400, 0);
    put32(td, 0x3EC, 1); put32(td, 0x3F0, 1);
    im.write(kTd, td.data(), std::uint32_t(td.size()));
    std::vector<std::uint8_t> trig(0x20, 0);
    putf(trig, 0x00, 100); putf(trig, 0x04, 2); putf(trig, 0x08, -50); putf(trig, 0x10, 1);   // position, direction +x
    im.write(kTrig, trig.data(), 0x20);
    std::vector<std::uint8_t> st(8, 0);
    putf(st, 0, 5); putf(st, 4, 10);
    if (statics) im.write(0x4E1568, st.data(), 8);
    std::vector<std::uint8_t> gm(0x1B8, 0);
    put32(gm, 0x1B4, kLogic);
    im.write(kGame, gm.data(), std::uint32_t(gm.size()));
    std::vector<std::uint8_t> gp(4, 0);
    put32(gp, 0, kGame);
    im.write(0x51BA88, gp.data(), 4);
    std::vector<std::uint8_t> lg(0x50, 0);
    put32(lg, 0, kVt);
    im.write(kLogic, lg.data(), 0x50);
    std::vector<std::uint8_t> vt(0xB0, 0), fn(0x100, 0);
    put32(vt, 0x44, kFn); put32(vt, 0x98, kFn + 0x40); put32(vt, 0x9C, kFn + 0x80);
    put32(fn, 0x00, 0x03E00008); put32(fn, 0x04, 0x24020000u | flags);          // GetCheckpointMarkerFlags: li v0, flags
    put32(fn, 0x40, 0x03E00008); put32(fn, 0x44, crash ? 0x24020006u : 0x0000102Du);   // slot 0x98: 6 = crash mode
    put32(fn, 0x80, 0x03E00008); put32(fn, 0x84, 0x0000102D);
    im.write(kVt, vt.data(), std::uint32_t(vt.size()));
    im.write(kFn, fn.data(), std::uint32_t(fn.size()));
    std::vector<std::uint8_t> pl(0x1900, 0);
    put32(pl, 0x10D4, std::uint32_t(lap)); put32(pl, 0x1384, std::uint32_t(laps)); put32(pl, 0x139C, std::uint32_t(n139c));
    im.write(kWorld + 0x12AD50 + 0x10D0, pl.data() + 0x10D0, 8);
    im.write(kWorld + 0x12AD50 + 0x1380, pl.data() + 0x1380, 0x20);
    im.write(kWorld + 0x12AD50 + 0x18E0, pl.data() + 0x18E0, 0x10);
}
b3replay::CrashState finish_of(std::uint32_t flags, int lap, int laps, int n139c, bool crash) {
    Image im;
    finish_scene(im, flags, lap, laps, n139c, crash);
    b3replay::Capture cap;
    b3replay::Tick t;
    cap.decode(im, t);
    return t.crash;
}
}  // namespace

TEST(capture_finish_line_on_the_last_lap) {
    auto c = finish_of(3, 1, 3, 2, false);   // lap 2 of 3: no line
    CHECK(c.valid && c.finish_known && c.finish.empty());
    c = finish_of(3, 2, 3, 0, false);        // the last lap before its first split: none yet
    CHECK(c.finish_known && c.finish.empty());
    c = finish_of(3, 2, 3, 1, false);        // the last lap: "finish" at the trigger
    CHECK(c.finish_known && c.finish.size() == 1);
    if (c.finish.size() == 1) {
        CHECK(c.finish[0].texture == 0 && c.finish[0].rgba == 0x80404040u);
        CHECK_NEAR(c.finish[0].pos[0], 100, 1e-6); CHECK_NEAR(c.finish[0].dir[0], 1, 1e-6);
        CHECK_NEAR(c.finish[0].half_h, 5, 1e-6); CHECK_NEAR(c.finish[0].half_w, 10, 1e-6);
    }
    c = finish_of(0x12, 0, 3, 1, false);     // Burning Lap's flags before the last lap: "check"
    CHECK(c.finish.size() == 1 && c.finish[0].texture == 1);
    c = finish_of(0x12, 2, 3, 1, false);     // ... and on it: "finish"
    CHECK(c.finish.size() == 1 && c.finish[0].texture == 0);
    c = finish_of(3, 2, 3, 1, true);         // crash mode: none
    CHECK(c.finish_known && c.finish.empty());
    c = finish_of(0, 2, 3, 1, false);        // Road Rage (flags 0): none
    CHECK(c.finish_known && c.finish.empty());
}

TEST(capture_finish_line_needs_its_inputs) {
    // an older capture (before perf1's fields: no DrawCheckpoint statics): not decided, nothing drawn
    Image old;
    finish_scene(old, 3, 2, 3, 1, false, false);
    b3replay::Capture cap;
    b3replay::Tick t;
    cap.decode(old, t);
    CHECK(t.crash.valid && !t.crash.finish_known && t.crash.finish.empty());
}

// perf (2026-11-12): Image::read copies per block / page now; the result must be the byte-wise rule's: true only when
// every byte of [a, a+n) was written, and on false the bytes before the first missing one copied, the rest untouched.
TEST(capture_image_read_matches_bytewise_rule) {
    Image im;
    std::map<std::uint32_t, std::uint8_t> ref;   // every written byte
    std::uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    const std::uint32_t base = 0x00400000;
    for (int w = 0; w < 300; ++w) {   // runs of 1..40 blocks over 8 pages, some across page edges, overwriting
        const std::uint32_t a = base + 16 * (rnd() % (8 * 256)), n = 16 * (1 + rnd() % 40);
        std::vector<std::uint8_t> p(n);
        for (auto& b : p) b = std::uint8_t(rnd());
        im.write(a, p.data(), n);
        for (std::uint32_t k = 0; k < n; ++k) ref[a + k] = p[k];
    }
    int full = 0, partial = 0;
    for (int r = 0; r < 4000; ++r) {
        const std::uint32_t a = base - 64 + rnd() % (9 * 4096), n = rnd() % 300;
        std::vector<std::uint8_t> got(n + 8, 0xEE);
        const bool ok = im.read(a, got.data(), n);
        bool want = true;
        std::vector<std::uint8_t> exp(n + 8, 0xEE);
        for (std::uint32_t k = 0; k < n; ++k) {
            const auto it = ref.find(a + k);
            if (it == ref.end()) { want = false; break; }
            exp[k] = it->second;
        }
        CHECK(ok == want);
        CHECK(got == exp);
        (ok ? full : partial) += n > 0;
    }
    CHECK(full > 100 && partial > 100);   // both outcomes exercised
    CHECK(im.u32(base - 16) == 0);       // never written
    im.clear();
    std::uint8_t b = 0;
    CHECK(!im.read(base, &b, 1));         // cleared (the lookup cache too)
}

// wobble1 (2026-11-18): a slammed race car (+0x199B set) is drawn at +0x1920 (CB3RaceCar::Draw 0x151A40 swaps it into
// +0x00 before RenderVehicle), and its wheels and panels are relative to that matrix too; they used to stay at +0x00's.
TEST(capture_fishtail_matrix_moves_the_parts) {
    for (int slammed : {0, 1}) {
        Image im;
        scene(im, 0);
        std::vector<std::uint8_t> d(0x70, 0);
        d[0x0C] = 1;   // one panel
        d[0x0D] = 1;   // one wheel
        im.write(kData, d.data(), std::uint32_t(d.size()));
        std::vector<std::uint8_t> c(0x2800, 0);
        im.read(kCarA, c.data(), std::uint32_t(c.size()));
        auto mat = [&](std::uint32_t off, const float (&r)[4][3]) {
            for (int a = 0; a < 4; ++a) {
                for (int k = 0; k < 3; ++k) putf(c, off + 16 * std::uint32_t(a) + 4 * std::uint32_t(k), r[a][k]);
                putf(c, off + 16 * std::uint32_t(a) + 12, a == 3 ? 1.0f : 0.0f);
            }
        };
        mat(0x80 + 0x000, {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 2}});    // wheel 0, relative
        mat(0x80 + 0x180, {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1, 0}});    // panel 0, relative
        mat(0x1920, {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}, {10, 0, 0}});        // the fishtail matrix: yawed 90 degrees
        c[0x199B] = std::uint8_t(slammed);
        im.write(kCarA, c.data(), std::uint32_t(c.size()));
        b3replay::Capture cap;
        b3replay::Tick t;
        cap.decode(im, t);
        const auto* a = find(t, kCarA);
        CHECK(a != nullptr && a->parts && a->nwheel == 1 && a->nbit == 1);
        if (!a) continue;
        const float main_x = slammed ? 10.0f : 0.0f;
        CHECK_NEAR(a->main.r[3][0], main_x, 1e-6);
        CHECK_NEAR(a->body.r[3][0], main_x, 1e-6);
        // the wheel at (1, 0, 2) and the panel at (0, 1, 0) of whichever matrix the body is drawn with
        CHECK_NEAR(a->wheel[0].r[3][0], slammed ? 12.0f : 1.0f, 1e-5);
        CHECK_NEAR(a->wheel[0].r[3][2], slammed ? -1.0f : 2.0f, 1e-5);
        CHECK_NEAR(a->wheel[0].r[0][2], slammed ? -1.0f : 0.0f, 1e-6);
        CHECK_NEAR(a->bit[0].r[3][0], main_x, 1e-5);
        CHECK_NEAR(a->bit[0].r[3][1], 1.0f, 1e-5);
        CHECK_NEAR(a->bit[0].r[0][2], slammed ? -1.0f : 0.0f, 1e-6);
    }
}

TEST_MAIN("capture")
