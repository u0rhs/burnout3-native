// fpspanel.h (fpsoverlay2): the counter's numbers from a frame timeline, the graph's scale, the view cycle, the layout in
// ten window shapes (4:3 .. 32:9) at every HUD SIZE, keeping clear of notices and of photo mode's plates, the plates' quads and the per-frame device calls.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "a11y.h"
#include "fpspanel.h"
#include "notice.h"
#include "photohud.h"
#include "test.h"

using namespace b3replay;

namespace {

struct Win { int w, h; };
// ten window shapes: 4:3, 16:10, 16:9, 21:9, 32:9, at small and large sizes
const Win kWins[] = {{640, 480}, {1024, 768}, {1280, 960}, {1280, 720}, {1920, 1080}, {1920, 1200}, {2560, 1080}, {3440, 1440}, {3840, 1080}, {5120, 1440}};
const FpsView kViews[] = {FpsView::kMinimal, FpsView::kStandard, FpsView::kDetailed};
const FpsCorner kCorners[] = {FpsCorner::kTopLeft, FpsCorner::kTopRight, FpsCorner::kBottomLeft, FpsCorner::kBottomRight};

constexpr std::uint64_t kSec = 1000000000ull;

// `n` frames `ms` apart from `t0`; returns the time after the last
std::uint64_t run(FpsPanel& p, std::uint64_t t0, int n, double ms) {
    std::uint64_t t = t0;
    for (int i = 0; i < n; ++i) {
        t += std::uint64_t(ms * 1e6);
        p.frame(t);
    }
    return t;
}

bool inside(const FpsPanel::Rect& r, int w, int h, float margin) {
    return r.x >= margin - 0.5f && r.y >= margin - 0.5f && r.x + r.w <= float(w) - margin + 0.5f && r.y + r.h <= float(h) - margin + 0.5f;
}
bool meet(const FpsPanel::Rect& a, float bx, float by, float bw, float bh) { return a.x < bx + bw && bx < a.x + a.w && a.y < by + bh && by < a.y + a.h; }

}  // namespace

TEST(view_cycle_and_settings) {
    // the shortcut: minimal, standard, detailed, off, round
    FpsView v = FpsView::kMinimal;
    const FpsView want[] = {FpsView::kStandard, FpsView::kDetailed, FpsView::kOff, FpsView::kMinimal, FpsView::kStandard};
    for (FpsView w : want) { v = fps_view_next(v); CHECK(v == w); }
    CHECK(fps_view_from_setting(0) == FpsView::kOff && fps_view_from_setting(1) == FpsView::kMinimal && fps_view_from_setting(2) == FpsView::kStandard &&
          fps_view_from_setting(3) == FpsView::kDetailed && fps_view_from_setting(4) == FpsView::kOff && fps_view_from_setting(-1) == FpsView::kOff);
    CHECK(fps_corner_from_setting(0) == FpsCorner::kTopLeft && fps_corner_from_setting(1) == FpsCorner::kTopRight &&
          fps_corner_from_setting(2) == FpsCorner::kBottomLeft && fps_corner_from_setting(3) == FpsCorner::kBottomRight && fps_corner_from_setting(9) == FpsCorner::kTopLeft);
    // photo mode's display up: the top left whatever was chosen; else as chosen
    for (FpsCorner c : kCorners) CHECK(fps_corner_in_photo(c, true) == FpsCorner::kTopLeft && fps_corner_in_photo(c, false) == c);
}

TEST(state_against_the_target) {
    CHECK(fps_state(60, 60) == FpsState::kAtTarget && fps_state(57, 60) == FpsState::kAtTarget && fps_state(200, 60) == FpsState::kAtTarget);
    CHECK(fps_state(56.9f, 60) == FpsState::kBelow && fps_state(45, 60) == FpsState::kBelow);
    CHECK(fps_state(44.9f, 60) == FpsState::kFarBelow && fps_state(0, 60) == FpsState::kFarBelow);
    CHECK(fps_state(120, 144) == FpsState::kBelow && fps_state(136.8f, 144) == FpsState::kAtTarget && fps_state(100, 144) == FpsState::kFarBelow);
    CHECK(fps_state(60, 0) == FpsState::kAtTarget);   // no target: 60
}

TEST(graph_scale) {
    const float t = 1000.0f / 60.0f;
    CHECK_NEAR(FpsPanel::graph_top_ms(0, t), 2 * t, 1e-3);       // an empty or fast graph: twice the target
    CHECK_NEAR(FpsPanel::graph_top_ms(10, t), 2 * t, 1e-3);
    CHECK_NEAR(FpsPanel::graph_top_ms(16.7f, t), 2 * t, 1e-3);
    CHECK_NEAR(FpsPanel::graph_top_ms(33.0f, t), 3 * t, 1e-3);   // a tenth above the peak, in whole targets
    CHECK_NEAR(FpsPanel::graph_top_ms(40.0f, t), 3 * t, 1e-3);
    CHECK_NEAR(FpsPanel::graph_top_ms(60.0f, t), 4 * t, 1e-3);
    CHECK_NEAR(FpsPanel::graph_top_ms(400.0f, t), 4 * t, 1e-3);  // one huge spike does not flatten the rest
    const float t144 = 1000.0f / 144.0f;
    CHECK_NEAR(FpsPanel::graph_top_ms(5, t144), 2 * t144, 1e-3);
    CHECK(FpsPanel::graph_top_ms(20, 0) >= 2 * t - 1e-3);        // no target: 60 Hz's
    for (float peak : {0.0f, 3.0f, 17.0f, 50.0f, 1000.0f}) {
        const float top = FpsPanel::graph_top_ms(peak, t);
        CHECK(top >= 2 * t - 1e-3 && top <= 4 * t + 1e-3 && (peak * 1.1f <= top || top >= 4 * t - 1e-3));
    }
    CHECK_NEAR(FpsPanel::spike_ms(t), 2 * t, 1e-3);              // at 60 Hz: 33.3 ms
    CHECK_NEAR(FpsPanel::spike_ms(t144), t144 + 8.0f, 1e-3);     // at 144: 8 ms over the target
}

TEST(numbers_from_a_frame_timeline) {
    FpsPanel p;
    CHECK(p.stats(5 * kSec).frames == 0);
    std::uint64_t t = run(p, kSec, 600, 1000.0 / 60.0);   // 10 s at 60
    FpsStats s = p.stats(t);
    CHECK_NEAR(s.fps, 60.0, 0.6);
    CHECK_NEAR(s.avg, 60.0, 0.6);
    CHECK_NEAR(s.low1, 60.0, 0.6);
    CHECK_NEAR(s.max_ms, 16.67, 0.05);
    CHECK_NEAR(s.frame_ms, 16.67, 0.2);
    CHECK(s.frames >= 470 && s.frames <= 481);   // the last 8 s
    // one 100 ms hitch: the worst frame, a low 1 % far under the average, the graph's column holds it
    p = FpsPanel{};
    t = run(p, kSec, 300, 1000.0 / 60.0);
    t = run(p, t, 1, 100.0);
    t = run(p, t, 150, 1000.0 / 60.0);
    s = p.stats(t);
    CHECK_NEAR(s.max_ms, 100.0, 0.01);
    CHECK(s.avg > 55 && s.avg < 60);
    CHECK(s.low1 < 40 && s.low1 > 25);           // 1 % of ~450 frames = 4: (100 + 3 x 16.7) / 4 ms -> 26.6 fps
    float g[FpsPanel::kColumns];
    p.graph(t, g);
    float peak = 0;
    int filled = 0;
    for (float v : g) { peak = std::max(peak, v); filled += v > 0; }
    CHECK_NEAR(peak, 100.0, 0.01);
    CHECK(filled >= 38);   // ~7.5 s of frames
    CHECK(g[FpsPanel::kColumns - 1] > 0);        // the newest column
    // the graph ages: 12 s later nothing is left
    p.graph(t + 12 * kSec, g);
    for (float v : g) CHECK(v == 0);
    CHECK(p.stats(t + 12 * kSec).frames == 0);
    // a slow run: 30 fps
    p = FpsPanel{};
    t = run(p, kSec, 300, 1000.0 / 30.0);
    s = p.stats(t);
    CHECK_NEAR(s.fps, 30.0, 0.5);
    CHECK_NEAR(s.frame_ms, 33.3, 0.5);
}

TEST(detail_lines_follow_what_is_known) {
    FpsInfo i;
    CHECK(FpsPanel::detail_lines(i).empty());
    i.latency_ms = 18.4f;
    i.width = 1920; i.height = 1080; i.msaa = 4; i.preset = "HIGH";
    i.gpu = "TEST GPU"; i.backend = "VULKAN";
    const std::vector<std::string> l = FpsPanel::detail_lines(i);
    CHECK(l.size() == 3 && l[0] == "LATENCY 18 MS" && l[1] == "1920X1080  4X MSAA  HIGH" && l[2] == "TEST GPU  VULKAN");
    i.latency_ms = -1;
    CHECK(FpsPanel::detail_lines(i).size() == 2);   // no latency line without a measurement
    i.msaa = 1;
    CHECK(FpsPanel::detail_lines(i)[0] == "1920X1080  NO MSAA  HIGH");
    i.gpu.clear();
    CHECK(FpsPanel::detail_lines(i)[1] == "VULKAN");
}

TEST(layout_fits_every_window_and_hud_size) {
    for (const Win& wn : kWins)
        for (int hud = 0; hud < 4; ++hud)
            for (FpsView v : kViews)
                for (FpsCorner c : kCorners) {
                    const FpsPanel::Layout L = FpsPanel::layout(wn.w, wn.h, hud, v, c, 3);
                    CHECK(!L.plate.empty());
                    CHECK(L.margin == std::round(0.045f * float(wn.h)));
                    CHECK(inside(L.plate, wn.w, wn.h, L.margin));
                    // anchored to its corner at the margin, whatever the aspect
                    const bool right = c == FpsCorner::kTopRight || c == FpsCorner::kBottomRight, bottom = c == FpsCorner::kBottomLeft || c == FpsCorner::kBottomRight;
                    CHECK_NEAR(right ? float(wn.w) - L.plate.x - L.plate.w : L.plate.x, L.margin, 0.5);
                    CHECK_NEAR(bottom ? float(wn.h) - L.plate.y - L.plate.h : L.plate.y, L.margin, 0.5);
                    // sized by the height and HUD SIZE (the window is big enough here: nothing is shrunk)
                    const float base = std::max(0.75f, float(wn.h) / 720.0f) * a11y::hud_size_scale(hud);
                    CHECK(L.scale <= base + 1e-4);
                    if (v == FpsView::kDetailed && (wn.h >= 720 || hud == 0)) CHECK(L.scale >= std::min(base, 1.0f) * 0.99f);   // (capped to about 12 % of the width, but not under a 720-line size)
                    // the small views never pass about 8 % of the window's width (nor shrink below what fits)
                    if (v != FpsView::kDetailed) { if (L.scale > 1.001f) CHECK(L.plate.w <= 0.08f * float(wn.w) * 1.06f + 2); }
                    else if (L.scale > 1.001f) CHECK(L.plate.w <= 0.12f * float(wn.w) * 1.06f + 2);
                    // the views grow: minimal < standard < detailed
                    const FpsPanel::Layout m = FpsPanel::layout(wn.w, wn.h, hud, FpsView::kMinimal, c, 3);
                    const FpsPanel::Layout d = FpsPanel::layout(wn.w, wn.h, hud, FpsView::kDetailed, c, 3);
                    if (L.scale == m.scale && d.scale == m.scale) CHECK(m.plate.w * m.plate.h < d.plate.w * d.plate.h);
                }
    CHECK(FpsPanel::layout(1280, 720, 0, FpsView::kOff, FpsCorner::kTopLeft, 0).plate.empty());
    CHECK(FpsPanel::layout(0, 0, 0, FpsView::kStandard, FpsCorner::kTopLeft, 0).plate.empty());
    // HUD SIZE grows the plate (a window with room)
    const float w100 = FpsPanel::layout(3840, 720, 0, FpsView::kDetailed, FpsCorner::kTopLeft, 0).plate.w;
    const float w150 = FpsPanel::layout(3840, 720, 3, FpsView::kDetailed, FpsCorner::kTopLeft, 0).plate.w;
    CHECK(w150 / w100 > 1.3 && w150 / w100 < 1.6);   // (whole-pixel measures: about 1.5)
    // the small views on a big window: a small plate (3200 x 1800: at most 8 % of the width), standard a line taller than minimal, detailed the big one
    const FpsPanel::Layout bm = FpsPanel::layout(3200, 1800, 0, FpsView::kMinimal, FpsCorner::kTopLeft, 0);
    const FpsPanel::Layout bs = FpsPanel::layout(3200, 1800, 0, FpsView::kStandard, FpsCorner::kTopLeft, 0);
    const FpsPanel::Layout bd = FpsPanel::layout(3200, 1800, 0, FpsView::kDetailed, FpsCorner::kTopLeft, 3);
    CHECK(bm.plate.w <= 0.08f * 3200 + 2 && bs.plate.w <= 0.08f * 3200 + 2);
    CHECK(bm.plate.h < bs.plate.h && bs.plate.h < 0.5f * bd.plate.h && bs.plate.w < 0.7f * bd.plate.w);
    CHECK(bs.plate.h < 130 && bm.plate.h < 100);
    CHECK(bd.plate.w <= 0.12f * 3200 + 3);
    // the same 720-line size in a 16:9 and a 32:9 window: the plate is the same, at the edge
    const FpsPanel::Layout a = FpsPanel::layout(1280, 720, 0, FpsView::kStandard, FpsCorner::kTopRight, 0);
    const FpsPanel::Layout b = FpsPanel::layout(2560, 720, 0, FpsView::kStandard, FpsCorner::kTopRight, 0);
    CHECK(a.plate.w == b.plate.w && a.plate.h == b.plate.h && a.plate.y == b.plate.y);
    CHECK_NEAR(float(2560) - b.plate.x - b.plate.w, a.margin, 0.5);
    // a window too small for the plate at its size: it shrinks to fit
    const FpsPanel::Layout tiny = FpsPanel::layout(200, 150, 3, FpsView::kDetailed, FpsCorner::kBottomRight, 3);
    CHECK(!tiny.plate.empty() && tiny.scale < 1.0f && inside(tiny.plate, 200, 150, tiny.margin));
}

TEST(layout_keeps_clear_of_the_race_hud) {
    // the race HUD's plates reach 14 % of the height from the top and 13 % from the bottom at 100 %, more when HUD SIZE grows them
    CHECK_NEAR(FpsPanel::game_hud_top(720, 0), 187, 1);
    CHECK(FpsPanel::game_hud_top(720, 3) > FpsPanel::game_hud_top(720, 0) && FpsPanel::game_hud_bottom(720, 3) > FpsPanel::game_hud_bottom(720, 0));
    for (const Win& wn : kWins)
        for (int hud = 0; hud < 4; ++hud)
            for (FpsView v : kViews)
                for (FpsCorner c : kCorners) {
                    const FpsPanel::Layout L = FpsPanel::layout(wn.w, wn.h, hud, v, c, 3, nullptr, true);
                    CHECK(!L.plate.empty() && inside(L.plate, wn.w, wn.h, L.margin));
                    const bool bottom = c == FpsCorner::kBottomLeft || c == FpsCorner::kBottomRight;
                    if (bottom) CHECK(L.plate.y + L.plate.h <= float(wn.h) - FpsPanel::game_hud_bottom(wn.h, hud) + 0.5f);
                    else CHECK(L.plate.y >= FpsPanel::game_hud_top(wn.h, hud) - 0.5f);
                    // the side margin is the same with or without the HUD
                    const FpsPanel::Layout P = FpsPanel::layout(wn.w, wn.h, hud, v, c, 3);
                    CHECK(L.plate.x == P.plate.x || L.scale != P.scale);
                }
}

TEST(layout_keeps_clear_of_a_notice) {
    int moved_n = 0;   // the cases in which the plate had to move (there must be some, or the test proves nothing)
    for (const Win& wn : kWins)
        for (int hud : {0, 3})
            for (FpsView v : kViews)
                for (FpsCorner c : kCorners) {
                    // a notice of three lines, as safe mode's: at the top centre, or at the bottom (photo mode's old help box)
                    for (bool bottom : {false, true}) {
                        Notice n;
                        n.bottom = bottom;
                        n.show({"SAFE MODE: THE LAST TWO STARTS DID NOT END CLEANLY", "SOME SETTINGS ARE LOWERED FOR THIS RUN", "SETTINGS / DISPLAY TO CHANGE THEM"}, 5.0f);
                        float b[4];
                        CHECK(n.box(wn.w, wn.h, b));
                        const FpsPanel::Rect nb{b[0], b[1], b[2], b[3]};
                        const FpsPanel::Layout free = FpsPanel::layout(wn.w, wn.h, hud, v, c, 3);
                        const FpsPanel::Layout L = FpsPanel::layout(wn.w, wn.h, hud, v, c, 3, &nb);
                        CHECK(L.plate.x == free.plate.x && L.plate.w == free.plate.w && L.scale == free.scale);   // only vertically
                        CHECK(inside(L.plate, wn.w, wn.h, L.margin));
                        const bool topc = c == FpsCorner::kTopLeft || c == FpsCorner::kTopRight;
                        if (!meet(free.plate, nb.x, nb.y, nb.w, nb.h)) continue;
                        if (topc == !bottom) {   // in the notice's way, on its side of the window: it moves clear if there is room
                            const bool moved = L.plate.y != free.plate.y;
                            if (moved) { CHECK(!meet(L.plate, nb.x, nb.y, nb.w, nb.h)); ++moved_n; }
                        }
                    }
                }
    CHECK(moved_n > 20);
    // the usual case: 1080p, top left, the top notice over it in a 4:3 window moves down under it
    Notice n;
    n.show({"BUG REPORT SAVED", "Burnout3-20261115-120000.zip", "IN DESKTOP"}, 5.0f);
    float b[4];
    CHECK(n.box(1024, 768, b));
    const FpsPanel::Rect nb{b[0], b[1], b[2], b[3]};
    const FpsPanel::Layout free = FpsPanel::layout(1024, 768, 0, FpsView::kStandard, FpsCorner::kTopLeft, 0);
    const FpsPanel::Layout moved = FpsPanel::layout(1024, 768, 0, FpsView::kStandard, FpsCorner::kTopLeft, 0, &nb);
    if (meet(free.plate, nb.x, nb.y, nb.w, nb.h)) CHECK(moved.plate.y >= nb.y + nb.h);
    else CHECK(moved.plate.y == free.plate.y);
    // a notice that is not showing has no box
    Notice none;
    CHECK(!none.box(1024, 768, b));
}

TEST(avoidance_shift_starts_again_when_the_layout_changes) {
    // a notice pushes a top-left plate down; then the corner, the view, the window or the scale changes: the first plate placed
    // is the natural one, inside the window, not the old plate's shift added to a new place
    const int w = 1280, h = 720;
    const FpsPanel::Rect nb{100, 20, 800, 90};   // a wide notice over the top left
    FpsPanel p;
    std::uint64_t t = kSec;
    const FpsPanel::Layout nat = FpsPanel::layout(w, h, 0, FpsView::kStandard, FpsCorner::kTopLeft, 0);
    const FpsPanel::Layout want = FpsPanel::layout(w, h, 0, FpsView::kStandard, FpsCorner::kTopLeft, 0, &nb);
    CHECK(want.plate.y > nat.plate.y);   // the notice does push it down
    FpsPanel::Rect r;
    for (int i = 0; i < 40; ++i) { t += 16000000ull; r = p.place(nat, want, FpsView::kStandard, t); }
    CHECK_NEAR(r.y, want.plate.y, 1.0);   // the shift is fully in
    for (FpsCorner c : {FpsCorner::kBottomLeft, FpsCorner::kBottomRight, FpsCorner::kTopRight}) {
        FpsPanel q = p;   // the same panel state, then a new corner
        const FpsPanel::Layout n2 = FpsPanel::layout(w, h, 0, FpsView::kStandard, c, 0);
        const FpsPanel::Layout w2 = FpsPanel::layout(w, h, 0, FpsView::kStandard, c, 0);   // (no notice in its way there)
        const FpsPanel::Rect f = q.place(n2, w2, FpsView::kStandard, t + 16000000ull);
        CHECK(f.x == n2.plate.x && f.y == n2.plate.y && f.w == n2.plate.w && f.h == n2.plate.h);
        CHECK(inside(f, w, h, n2.margin));
    }
    {   // the scale changes (HUD SIZE): the same
        FpsPanel q = p;
        const FpsPanel::Layout n2 = FpsPanel::layout(w, h, 3, FpsView::kStandard, FpsCorner::kTopLeft, 0);
        const FpsPanel::Rect f = q.place(n2, n2, FpsView::kStandard, t + 16000000ull);
        CHECK(f.y == n2.plate.y && inside(f, w, h, n2.margin));
    }
    {   // the view changes, the window changes, the race HUD comes up
        FpsPanel q = p;
        const FpsPanel::Layout n2 = FpsPanel::layout(w, h, 0, FpsView::kDetailed, FpsCorner::kTopLeft, 3);
        CHECK(q.place(n2, n2, FpsView::kDetailed, t + 16000000ull).y == n2.plate.y);
        FpsPanel q2 = p;
        const FpsPanel::Layout n3 = FpsPanel::layout(1920, 1080, 0, FpsView::kStandard, FpsCorner::kTopLeft, 0);
        CHECK(q2.place(n3, n3, FpsView::kStandard, t + 16000000ull).y == n3.plate.y);
        FpsPanel q3 = p;
        const FpsPanel::Layout n4 = FpsPanel::layout(w, h, 0, FpsView::kStandard, FpsCorner::kTopLeft, 0, nullptr, true);
        const FpsPanel::Rect f = q3.place(n4, n4, FpsView::kStandard, t + 16000000ull);
        CHECK(f.y == n4.plate.y && f.y >= FpsPanel::game_hud_top(h, 0) - 0.5f);
    }
    // an extreme shift (a notice tall enough to push past the edge) is clamped inside the safe area
    {
        FpsPanel q;
        const FpsPanel::Layout bn = FpsPanel::layout(w, h, 0, FpsView::kStandard, FpsCorner::kTopLeft, 0);
        FpsPanel::Layout bw = bn;
        bw.plate.y += 5000;   // a wanted place far below the window
        FpsPanel::Rect z;
        for (int i = 0; i < 400; ++i) { t += 16000000ull; z = q.place(bn, bw, FpsView::kStandard, t); }
        CHECK(inside(z, w, h, bn.margin));
    }
}

TEST(layout_keeps_clear_of_photo_modes_plates) {
    const PhotoKeys keys;
    for (const Win& wn : kWins)
        for (int hud = 0; hud < 4; ++hud)
            for (PromptDevice dev : {PromptDevice::kKeyboard, PromptDevice::kPlayStation})
                for (PhotoUi ui : {PhotoUi::kFull, PhotoUi::kReadouts})
                    for (FpsView v : kViews)
                        for (FpsCorner chosen : kCorners) {
                            const PhotoHud::Layout pl = PhotoHud::layout(wn.w, wn.h, hud, dev, keys, ui);
                            if (pl.readouts.empty()) continue;
                            const FpsPanel::Layout L = FpsPanel::layout(wn.w, wn.h, hud, v, fps_corner_in_photo(chosen, true), 3);
                            CHECK(!L.plate.empty());
                            CHECK(!meet(L.plate, pl.readouts.x, pl.readouts.y, pl.readouts.w, pl.readouts.h));
                            if (!pl.strip.empty()) CHECK(!meet(L.plate, pl.strip.x, pl.strip.y, pl.strip.w, pl.strip.h));
                        }
}

// A device that only counts: the per-frame calls of draw_list
struct CountDev : b3gfx::Device {
    int vb_up = 0, ib_up = 0, tex_create = 0, tex_up = 0;
    const char* driver_name() const override { return "count"; }
    int msaa() const override { return 1; }
    const char* present_mode_name() const override { return ""; }
    int frames_in_flight() const override { return 0; }
    void wait_swapchain() override {}
    b3gfx::BufferId create_vertex_buffer(const b3gfx::Vertex*, std::size_t) override { return 0; }
    b3gfx::BufferId create_index_buffer(const std::uint32_t*, std::size_t) override { return 0; }
    b3gfx::TextureId create_texture(const std::vector<b3gfx::TextureLevel>&) override { ++tex_create; return 1; }
    void update_texture(b3gfx::TextureId&, int, int, const std::uint8_t*) override { ++tex_up; }
    void clear_resources() override {}
    void update_vertex_buffer(b3gfx::BufferId& id, const b3gfx::Vertex*, std::size_t) override { ++vb_up; id = 5; }
    void update_index_buffer(b3gfx::BufferId& id, const std::uint32_t*, std::size_t) override { ++ib_up; id = 6; }
    bool draw_frame(const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, int*, int*) override { return true; }
    bool draw_offscreen(int, int, const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, std::vector<std::uint8_t>&) override { return true; }
    bool draw_views(const std::vector<ViewPass>&, const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, int*, int*) override { return true; }
    bool draw_views_offscreen(int, int, const std::vector<ViewPass>&, const b3gfx::FrameParams&, const std::vector<b3gfx::DrawItem>&, std::vector<std::uint8_t>&) override { return true; }
};

// the lit cells of the gauge in a plate's quads: opaque quads in the gauge's strip
int lit_cells(const std::vector<b3gfx::Vertex>& vs, const FpsPanel::Layout& L) {
    int n = 0;
    for (std::size_t q = 0; q + 3 < vs.size(); q += 4) {
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (std::size_t k = 0; k < 4; ++k) { x0 = std::min(x0, vs[q + k].pos[0]); x1 = std::max(x1, vs[q + k].pos[0]); y0 = std::min(y0, vs[q + k].pos[1]); y1 = std::max(y1, vs[q + k].pos[1]); }
        const bool rect = vs[q].pos[0] == vs[q + 3].pos[0] && vs[q + 1].pos[0] == vs[q + 2].pos[0];
        if (!rect && x0 >= 0.04f * L.plate.w && x1 <= 0.3f * L.plate.w && y1 < 0.9f * L.plate.h && (vs[q].rgba >> 24) == 128) ++n;
    }
    return n;
}

TEST(quads_match_their_layout) {
    FpsPanel p;
    std::uint64_t t = run(p, kSec, 300, 1000.0 / 60.0);
    t = run(p, t, 1, 90.0);
    t = run(p, t, 150, 1000.0 / 60.0);
    FpsInfo info;
    info.latency_ms = 22;
    info.width = 1920; info.height = 1080; info.msaa = 4; info.preset = "HIGH"; info.gpu = "A VERY LONG GPU NAME THAT DOES NOT FIT THE PLATE AT ALL"; info.backend = "VULKAN";
    CHECK(FpsPanel::detail_row_count(info) == int(FpsPanel::detail_lines(info).size()));
    FpsInfo part;
    part.msaa = 1;
    CHECK(FpsPanel::detail_row_count(part) == int(FpsPanel::detail_lines(part).size()) && FpsPanel::detail_row_count(FpsInfo{}) == 0);
    const FpsStats st = p.stats(t);
    float g[FpsPanel::kColumns];
    p.graph(t, g);
    std::size_t count[4] = {};
    for (const Win& wn : {Win{1280, 720}, Win{640, 480}, Win{3840, 1080}, Win{3200, 1800}})
        for (FpsView v : kViews) {
            const FpsPanel::Layout L = FpsPanel::layout(wn.w, wn.h, 0, v, FpsCorner::kTopLeft, int(FpsPanel::detail_lines(info).size()));
            std::vector<b3gfx::Vertex> vs;
            std::vector<std::uint32_t> is;
            FpsPanel::build(L, v, st, g, info, vs, is);
            CHECK(!vs.empty() && vs.size() % 4 == 0 && is.size() == vs.size() / 4 * 6);
            if (wn.w == 3200) count[int(v)] = vs.size() / 4;
            int slanted = 0;
            for (std::size_t q = 0; q + 3 < vs.size(); q += 4) {
                for (std::size_t k = 0; k < 4; ++k) {   // inside the plate
                    CHECK(vs[q + k].pos[0] >= -3.0f && vs[q + k].pos[0] <= L.plate.w + 3.0f);
                    CHECK(vs[q + k].pos[1] >= -3.0f && vs[q + k].pos[1] <= L.plate.h + 3.0f);
                }
                const bool rect = vs[q].pos[0] == vs[q + 3].pos[0] && vs[q + 1].pos[0] == vs[q + 2].pos[0] && vs[q].pos[1] == vs[q + 1].pos[1] && vs[q + 2].pos[1] == vs[q + 3].pos[1];
                slanted += !rect;
            }
            CHECK(slanted <= 8);   // the gauge's cells, the two corner triangles and the two cut edges: everything else is a rectangle
            for (std::uint32_t i : is) CHECK(i < vs.size());
            CHECK(lit_cells(vs, L) == 3);
        }
    CHECK(count[1] < count[2] && count[2] < count[3]);   // minimal < standard < detailed
    // the state beyond its colour: three lit cells at the target, one far below it, none with no frames yet
    const FpsPanel::Layout L = FpsPanel::layout(1280, 720, 0, FpsView::kMinimal, FpsCorner::kTopLeft, 0);
    FpsInfo plain;
    plain.target_fps = 60;
    float none[FpsPanel::kColumns] = {};
    FpsStats good = st, far = st;
    good.fps = 60;
    far.fps = 30;
    std::vector<b3gfx::Vertex> a, b;
    std::vector<std::uint32_t> ia, ib;
    FpsPanel::build(L, FpsView::kMinimal, good, none, plain, a, ia);
    FpsPanel::build(L, FpsView::kMinimal, far, none, plain, b, ib);
    CHECK(lit_cells(a, L) == 3 && lit_cells(b, L) == 1);
    FpsPanel::build(L, FpsView::kMinimal, FpsStats{}, none, plain, a, ia);
    CHECK(lit_cells(a, L) == 0 && !a.empty());
    FpsPanel::build(L, FpsView::kOff, st, g, info, a, ia);
    CHECK(a.empty() && ia.empty());
}

TEST(draw_list_touches_the_device_only_when_the_quads_change) {
    for (FpsView v : kViews) {
        CountDev dev;
        FpsPanel p;
        FpsInfo info;
        info.width = 3200; info.height = 1800; info.msaa = 4; info.preset = "HIGH"; info.gpu = "GPU"; info.backend = "METAL";
        std::vector<b3gfx::DrawItem> items;
        std::uint64_t t = kSec;
        const int frames = 600;   // 6 s at 100 fps
        for (int i = 0; i < frames; ++i) {
            t += 10000000ull;
            p.frame(t);
            info.latency_ms = 18.0f + 0.001f * float(i);   // (a latency that moves every frame, within the same whole ms)
            items.clear();
            p.draw_list(dev, 3200, 1800, t, v, FpsCorner::kTopLeft, 0, info, nullptr, false, items);
            CHECK(items.size() == 1 && items[0].index_count > 0 && items[0].texture != b3gfx::kNone);
        }
        CHECK(dev.tex_create == 1 && dev.tex_up == 0);   // the 1 x 1 white texture, once; never updated
        // the quads: every 0.5 s (6 s: 12 or 13 builds), not every frame
        CHECK(dev.vb_up >= 11 && dev.vb_up <= 14 && dev.vb_up == dev.ib_up);
    }
    // a moving plate (a notice's avoidance) re-uploads nothing: it moves in the matrix
    CountDev dev;
    FpsPanel p;
    std::vector<b3gfx::DrawItem> items;
    FpsPanel::Rect nb{0, 0, 400, 120};
    std::uint64_t t = kSec;
    for (int i = 0; i < 40; ++i) {
        t += 10000000ull;
        p.frame(t);
        items.clear();
        p.draw_list(dev, 1280, 720, t, FpsView::kStandard, FpsCorner::kTopLeft, 0, FpsInfo{}, i < 20 ? nullptr : &nb, false, items);
    }
    CHECK(dev.vb_up == 1);   // (0.4 s: one build)
}

// A device with a real id table, as SdlDevice's: clear_resources restarts the ids from 0 (the game's own then take them)
struct IdDev : CountDev {
    unsigned epoch = 0;
    std::size_t bufs = 0, texs = 0;
    int stale_writes = 0;   // a buffer update through an id the device does not hold
    unsigned resource_epoch() const override { return epoch; }
    b3gfx::BufferId create_vertex_buffer(const b3gfx::Vertex*, std::size_t) override { return b3gfx::BufferId(bufs++); }
    b3gfx::BufferId create_index_buffer(const std::uint32_t*, std::size_t) override { return b3gfx::BufferId(bufs++); }
    b3gfx::TextureId create_texture(const std::vector<b3gfx::TextureLevel>&) override { return b3gfx::TextureId(texs++); }
    void clear_resources() override { ++epoch; bufs = texs = 0; }
    void update_vertex_buffer(b3gfx::BufferId& id, const b3gfx::Vertex*, std::size_t) override { upd(id); }
    void update_index_buffer(b3gfx::BufferId& id, const std::uint32_t*, std::size_t) override { upd(id); }
    void upd(b3gfx::BufferId& id) {
        if (id == b3gfx::kNone) id = b3gfx::BufferId(bufs++);
        else if (id >= bufs) ++stale_writes;
    }
};

TEST(a_track_load_clears_the_plates_buffers_and_texture) {
    // Crash mode (and every track load) calls clear_resources; the ids the plate kept then named the game's own buffers and
    // texture: the plate was drawn with a game texture (a dark translucent red box in Crash mode) and wrote its quads into a game buffer
    IdDev dev;
    FpsPanel p;
    std::vector<b3gfx::DrawItem> items;
    std::uint64_t t = kSec;
    const auto frames = [&](int n) {
        for (int i = 0; i < n; ++i) {
            t += 10000000ull;
            p.frame(t);
            items.clear();
            p.draw_list(dev, 1280, 720, t, FpsView::kDetailed, FpsCorner::kTopLeft, 0, FpsInfo{}, nullptr, false, items);
        }
    };
    frames(5);
    CHECK(items.size() == 1);
    dev.clear_resources();
    // the game's track load: its own buffers and textures, from id 0
    const b3gfx::BufferId game_vb = dev.create_vertex_buffer(nullptr, 0), game_ib = dev.create_index_buffer(nullptr, 0);
    const b3gfx::TextureId game_tex = dev.create_texture({});
    frames(1);   // the very next frame, no 0.5 s wait
    CHECK(items.size() == 1);
    CHECK(items[0].texture != game_tex && items[0].texture < dev.texs);
    CHECK(items[0].vertices != game_vb && items[0].vertices != game_ib && items[0].indices != game_vb && items[0].indices != game_ib);
    CHECK(items[0].vertices < dev.bufs && items[0].indices < dev.bufs);
    CHECK(dev.stale_writes == 0);
    frames(100);
    CHECK(dev.stale_writes == 0 && dev.texs == 2);   // (the game's and ours, once)
}

TEST_MAIN("b3replay fpspanel (fpsoverlay2)")
