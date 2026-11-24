// splitlayout1 (src/b3replay/splitlayout.h): the split screen's layouts -- the game's own viewport constants, each
// view's rectangle and projection (no stretching: the view window has the view's shape), the HUD placement inside each
// view at 16:9 and 21:9, the clip-space move of a 4:3 item, and the SPLIT SCREEN setting (ini, page row, edit).
#include "test.h"

#include "splitlayout.h"

#include "b3settings/page.h"
#include "b3settings/settings.h"

#include <algorithm>
#include <cmath>

using namespace b3replay;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps * std::max(1.0f, std::fabs(b)); }

// The game's top / bottom view window for tan(fov / 2) = t (CGtViewport::SetFOV: half_w = scale t, half_h = half_w / aspect)
void game_window(float t, bool wide, float* hw, float* hh) {
    const GameViewport g = game_split_viewport(false, wide);
    *hw = g.scale * t;
    *hh = *hw / g.aspect;
}

struct Out { int w, h; };
const Out kOuts[] = {{1920, 1080}, {2560, 1080}, {3440, 1440}, {1280, 720}};   // 16:9, 21:9 (64:27 and 43:18), 16:9

}  // namespace

TEST(game_constants_are_the_decomps) {
    // CB3GraphicsManagerBase::SetViewportsSplitScreen 0x19E760: (aspect, scale) per layout and 16:9 flag
    CHECK(near(game_split_viewport(false, false).aspect, 8.0f / 3.0f) && near(game_split_viewport(false, false).scale, 1.35f));
    CHECK(near(game_split_viewport(false, true).aspect, 32.0f / 9.0f) && near(game_split_viewport(false, true).scale, 1.62f));
    CHECK(near(game_split_viewport(true, false).aspect, 2.0f / 3.0f) && near(game_split_viewport(true, false).scale, 0.55f));
    CHECK(near(game_split_viewport(true, true).aspect, 8.0f / 9.0f) && near(game_split_viewport(true, true).scale, 0.75f));
    // a half of a 4:3 (16:9) screen has exactly the game's aspect: the PS2 picture is not squashed
    CHECK(near(game_split_viewport(false, false).aspect, 2.0f * (4.0f / 3.0f)));
    CHECK(near(game_split_viewport(true, false).aspect, 0.5f * (4.0f / 3.0f)));
    CHECK(near(game_split_viewport(false, true).aspect, 2.0f * (16.0f / 9.0f)));
    CHECK(near(game_split_viewport(true, true).aspect, 0.5f * (16.0f / 9.0f)));
}

TEST(the_game_cameras_flag_and_tangent_are_recovered) {
    for (float t : {0.5f, 0.7673f, 1.0f, 1.2f})
        for (bool wide : {false, true}) {
            float hw, hh;
            game_window(t, wide, &hw, &hh);
            CHECK(game_split_wide_flag(hw, hh) == wide);
            CHECK(near(game_split_tangent(hw, hh), t));
            // a FIELD OF VIEW setting scales both (View::apply_fov): the flag stays, the tangent scales
            CHECK(game_split_wide_flag(1.3f * hw, 1.3f * hh) == wide);
            CHECK(near(game_split_tangent(1.3f * hw, 1.3f * hh), 1.3f * t));
        }
}

TEST(each_views_vertical_window) {
    for (float t : {0.6f, 0.7673f}) {
        float hw, hh;
        game_window(t, false, &hw, &hh);
        CHECK(split_view_half_h(SplitLayout::kGame, hw, hh) == hh);   // the game's own, unchanged
        CHECK(near(split_view_half_h(SplitLayout::kSideBySide, hw, hh), 0.55f * t / (2.0f / 3.0f)));   // 0.825 t
        game_window(t, true, &hw, &hh);
        CHECK(near(split_view_half_h(SplitLayout::kSideBySide, hw, hh), 0.75f * t / (8.0f / 9.0f)));   // 0.84375 t
        // at the game's own side-by-side shape the window is the game's (Hor+ adds nothing at 2:3)
        game_window(t, false, &hw, &hh);
        const float v = split_view_half_h(SplitLayout::kSideBySide, hw, hh);
        CHECK(near(v * (2.0f / 3.0f), 0.55f * t));
    }
}

TEST(views_tile_the_output_and_keep_their_shape) {
    for (SplitLayout l : {SplitLayout::kGame, SplitLayout::kSideBySide})
        for (const Out& o : kOuts) {
            const float a = float(o.w) / float(o.h);
            SplitView v[2];
            split_views(l, a, v);
            float area = 0;
            for (int k = 0; k < 2; ++k) {
                const float pw = (v[k].rect[2] - v[k].rect[0]) * float(o.w), ph = (v[k].rect[3] - v[k].rect[1]) * float(o.h);
                CHECK(near(v[k].aspect, pw / ph));   // the view's projection aspect = its pixels' shape: no stretching
                area += (v[k].rect[2] - v[k].rect[0]) * (v[k].rect[3] - v[k].rect[1]);
                // a projection built from the view window (half_w = half_h x aspect) has square pixels in the view
                const float hh = 0.4f, hw = hh * v[k].aspect;
                CHECK(near((pw / 2.0f) / hw, (ph / 2.0f) / hh));
            }
            CHECK(near(area, 1.0f));
            if (l == SplitLayout::kGame) {   // player 1 on top, as the game (and the renderer before splitlayout1)
                CHECK(v[0].rect[0] == 0.0f && v[0].rect[1] == 0.0f && v[0].rect[2] == 1.0f && v[0].rect[3] == 0.5f);
                CHECK(v[1].rect[1] == 0.5f && v[1].rect[3] == 1.0f);
                CHECK(v[0].aspect == 2.0f * a);
            } else {   // player 1 on the left
                CHECK(v[0].rect[0] == 0.0f && v[0].rect[2] == 0.5f && v[1].rect[0] == 0.5f && v[1].rect[2] == 1.0f);
                CHECK(v[0].rect[1] == 0.0f && v[0].rect[3] == 1.0f && near(v[0].aspect, 0.5f * a));
            }
        }
}

TEST(side_by_side_fields_of_view_at_16_9_and_21_9) {
    // 75 degrees horizontal for one 4:3 player: t = tan 37.5
    const float t = std::tan(37.5f * 3.14159265f / 180.0f);
    float hw, hh;
    game_window(t, false, &hw, &hh);
    for (const Out& o : {Out{1920, 1080}, Out{2560, 1080}}) {
        SplitView v[2];
        split_views(SplitLayout::kSideBySide, float(o.w) / float(o.h), v);
        const float vh = split_view_half_h(SplitLayout::kSideBySide, hw, hh), vw = vh * v[0].aspect;
        const float hfov = 2.0f * std::atan(vw) * 180.0f / 3.14159265f, vfov = 2.0f * std::atan(vh) * 180.0f / 3.14159265f;
        CHECK(vfov > 60.0f && vfov < 70.0f);                 // the game's side-by-side vertical (0.825 t)
        CHECK(hfov > 50.0f && hfov < (o.w == 1920 ? 62.0f : 76.0f));   // narrower than a whole screen, wider at 21:9
    }
}

TEST(hud_bands_stay_inside_their_views) {
    for (SplitLayout l : {SplitLayout::kGame, SplitLayout::kSideBySide})
        for (const Out& o : kOuts) {
            SplitView v[2];
            split_views(l, float(o.w) / float(o.h), v);
            for (int k = 0; k < 2; ++k) {
                const HudPlace p = split_hud_place(k, v[k], o.w, o.h);
                CHECK(p.src[1] == 224.0f * float(k) && p.src[3] == 224.0f * float(k + 1));
                CHECK(p.scale <= float(o.h) / 448.0f + 1e-6f);   // never larger than the game's own HUD
                for (float ax : {0.0f, 0.5f, 1.0f})
                    for (float ay : {0.0f, 0.5f, 1.0f})
                        for (bool stretch : {false, true}) {
                            const HudAffine m = split_hud_affine(p, ax, ay, stretch);
                            // the whole band, anchored anywhere, lands inside the view
                            for (float x : {p.src[0], p.src[2]})
                                for (float y : {p.src[1], p.src[3]}) {
                                    const float X = m.a * x + m.b, Y = m.c * y + m.d;
                                    CHECK(X >= p.dst[0] - 0.01f && X <= p.dst[2] + 0.01f);
                                    CHECK(Y >= p.dst[1] - 0.01f && Y <= p.dst[3] + 0.01f);
                                }
                            CHECK(m.c == p.scale);   // the HUD keeps its shape (no vertical stretch)
                            if (!stretch) CHECK(m.a == p.scale);
                        }
            }
        }
    // the sizes: top / bottom at the game's (out_h / 448); side by side as wide as the half (16:9: 960 / 640)
    SplitView v[2];
    split_views(SplitLayout::kSideBySide, 1920.0f / 1080.0f, v);
    CHECK(near(split_hud_place(0, v[0], 1920, 1080).scale, 1.5f));
    split_views(SplitLayout::kSideBySide, 2560.0f / 1080.0f, v);
    CHECK(near(split_hud_place(1, v[1], 2560, 1080).scale, 2.0f));
    split_views(SplitLayout::kGame, 2560.0f / 1080.0f, v);
    CHECK(near(split_hud_place(1, v[1], 2560, 1080).scale, 1080.0f / 448.0f));
}

TEST(anchors_put_components_at_their_views_corners) {
    SplitView v[2];
    split_views(SplitLayout::kSideBySide, 1920.0f / 1080.0f, v);
    const HudPlace p = split_hud_place(1, v[1], 1920, 1080);   // player 2: the right half, rows 224..448 of the 2D
    // a bottom-left component (the boost bar, x 26..230): its margin to the view's left / bottom edge kept, scaled
    HudAffine m = split_hud_affine(p, 0.0f, 1.0f, false);
    CHECK(near(m.a * 26.0f + m.b, 960.0f + 1.5f * 26.0f));
    CHECK(near(m.c * 448.0f + m.d, 1080.0f));
    // a top-right one (x up to 610, y from 246): 30 units from the view's right edge, 22 from its top
    m = split_hud_affine(p, 1.0f, 0.0f, false);
    CHECK(near(m.a * 610.0f + m.b, 1920.0f - 1.5f * 30.0f));
    CHECK(near(m.c * 246.0f + m.d, 1.5f * 22.0f));
    // centred: the band's centre at the view's centre
    m = split_hud_affine(p, 0.5f, 0.5f, false);
    CHECK(near(m.a * 320.0f + m.b, 1440.0f) && near(m.c * 336.0f + m.d, 540.0f));
    // a full-width band stretches over the view's width
    m = split_hud_affine(p, 0.5f, 0.5f, true);
    CHECK(near(m.a * 0.0f + m.b, 960.0f) && near(m.a * 640.0f + m.b, 1920.0f));
    // anchors of draws without a component: by their box's place in the band (thirds)
    float ax = -1, ay = -1;
    const float speedo[4] = {500, 380, 610, 440};
    split_hud_anchor(p, speedo, &ax, &ay);
    CHECK(ax == 1.0f && ay == 1.0f);
    const float banner[4] = {64, 300, 576, 340};
    split_hud_anchor(p, banner, &ax, &ay);
    CHECK(ax == 0.5f && ay == 0.5f);
    // the game's own layout: player 1's top-left component stays where the game's wide HUD puts it (x, y scaled)
    split_views(SplitLayout::kGame, 1920.0f / 1080.0f, v);
    const HudPlace g = split_hud_place(0, v[0], 1920, 1080);
    m = split_hud_affine(g, 0.0f, 0.0f, false);
    CHECK(near(m.a * 30.0f + m.b, 30.0f * 1080.0f / 448.0f) && near(m.c * 22.0f + m.d, 22.0f * 1080.0f / 448.0f));
}

TEST(a_4_3_item_moves_in_clip_space) {
    for (const Out& o : kOuts) {
        // hud_list's matrix at 4:3 over the whole output (fw = fh = 1)
        const float sx = 2.0f / 640.0f, sy = 2.0f / 448.0f, hx = 1.0f / float(o.w), hy = 1.0f / float(o.h);
        float base[16] = {sx, 0, 0, 0, 0, -sy, 0, 0, 0, 0, 1, 0, -1.0f + hx, 1.0f - hy, 0.5f, 1};
        const HudAffine m{1.7f, 123.0f, 1.7f, -45.0f};
        float model[16];
        std::memcpy(model, base, sizeof model);
        split_hud_model(m, 640, 448, o.w, o.h, model);
        for (float x : {0.0f, 100.0f, 640.0f})
            for (float y : {0.0f, 224.0f, 448.0f}) {
                const float cx = model[0] * x + model[4] * y + model[12], cy = model[1] * x + model[5] * y + model[13];
                const float cw = model[3] * x + model[7] * y + model[15];
                // where it lands in output pixels (the half-pixel correction as hud_list's)
                const float X = (cx / cw + 1.0f - hx) * 0.5f * float(o.w), Y = (1.0f - hy - cy / cw) * 0.5f * float(o.h);
                CHECK(near(X, m.a * x + m.b, 1e-4f) && near(Y, m.c * y + m.d, 1e-4f));
                CHECK(model[14] == 0.5f && model[10] == 1.0f);   // depth untouched
            }
        // an item hud_list scaled itself (hud4's marker matrix: r about a point) keeps that scale under the move
        float scaled[16];
        std::memcpy(scaled, base, sizeof scaled);
        for (int i = 0; i < 4; ++i) { scaled[i] *= 2.0f; scaled[4 + i] *= 2.0f; }
        split_hud_model(m, 640, 448, o.w, o.h, scaled);
        const float X1 = (scaled[0] * 10.0f + scaled[12] + 1.0f - hx) * 0.5f * float(o.w);
        const float X0 = (scaled[12] + 1.0f - hx) * 0.5f * float(o.w);
        CHECK(near(X1 - X0, 2.0f * m.a * 10.0f, 1e-3f));
        // the scissor: the band's rows, moved and kept inside the clip rectangle
        float sc[4] = {0.0f, 0.5f, 1.0f, 1.0f};
        const float clip[4] = {0.5f * float(o.w), 0.0f, float(o.w), float(o.h)};
        CHECK(split_hud_scissor(HudAffine{1.0f, 0.5f * float(o.w), 1.0f, -100.0f}, 640, 448, o.w, o.h, clip, sc));
        CHECK(sc[0] >= 0.5f - 1e-6f && sc[2] <= 1.0f + 1e-6f && sc[1] >= 0.0f && sc[3] <= 1.0f);
        float off[4] = {0.0f, 0.0f, 0.1f, 0.1f};
        CHECK(!split_hud_scissor(HudAffine{1.0f, 0.0f, 1.0f, 0.0f}, 640, 448, o.w, o.h, clip, off));   // outside: not drawn
    }
}

TEST(the_split_screen_setting) {
    using namespace b3settings;
    const Settings d{};
    CHECK(d.split_layout == 0);   // the game's top / bottom
    CHECK(split_layout_of(d.split_layout) == SplitLayout::kGame && split_layout_of(1) == SplitLayout::kSideBySide);
    CHECK(split_layout_of(7) == SplitLayout::kGame);
    Settings s;
    s.split_layout = 1;
    CHECK(from_ini(to_ini(s)).split_layout == 1);
    CHECK(to_ini(s).find("split_layout = 1") != std::string::npos);
    CHECK(from_ini("split_layout = 5\n").split_layout == 0);   // out of range: the default
    // the row: DISPLAY > LAYOUT (settingsui: after MENU MOVIE SIDES, the section's last), id 180, two values; an edit sets it
    const Content c = build_content(s, Env{});
    const Row* row = nullptr;
    const Tab& disp = c.tabs[0];
    CHECK(disp.label == "DISPLAY");
    std::size_t at = 0;
    for (std::size_t k = 0; k < disp.rows.size(); ++k) if (disp.rows[k].label == "SPLIT SCREEN") { row = &disp.rows[k]; at = k; }
    for (const Tab& t : c.tabs) if (&t != &disp) for (const Row& r : t.rows) CHECK(r.label != "SPLIT SCREEN");
    CHECK(row != nullptr && at > 0 && at + 1 < disp.rows.size() && disp.rows[at - 1].label == "MENU MOVIE SIDES" &&
          disp.rows[at + 1].kind == RowKind::kSection && disp.rows[at + 1].label == "RACE HUD");   // (hudmods: RACE HUD next)
    if (row) {
        CHECK(row->id == 180 && row->values.size() == 2 && row->value == 1);
        CHECK(row->values[0] == "Top / bottom" && row->values[1] == "Side by side");
    }
    Settings e;
    CHECK(apply_edit(e, Edit{180, 1, 0, false}) && e.split_layout == 1);
    CHECK(apply_edit(e, Edit{180, 0, 0, false}) && e.split_layout == 0);
    CHECK(matching_preset(d) == Preset::kHigh);   // not a preset row
}

TEST_MAIN("split-screen layouts (src/b3replay/splitlayout.h)")
