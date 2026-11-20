// mac5: the button prompts for the device in use (remaster-features §3; src/b3replay/prompts.h): the labels per device,
// the key caps from the key map, the automatic switching with its hysteresis, the glyph atlas, and the substitution of
// the game's button draws (the prompt bar's buttons from "Buttons", the d-pad hint's arms from "dpad") as captured.
#include "test.h"

#include "glyphdraw.h"
#include "prompts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

using b3replay::GlyphLayer;
using b3replay::HudVertex;
using b3replay::PadButton;
using b3replay::PromptAtlas;
using b3replay::PromptDevice;
using b3replay::PromptDeviceTracker;
using b3replay::PromptKeys;

// A quad as RenderSpritesTex sends it (v0 v1 v2, v0 v2 v3), uv in texels of a `tw` x `th` raster; `flip_v` swaps v
void quad(std::vector<HudVertex>& t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float tw, float th,
          bool flip_v = false) {
    if (flip_v) std::swap(v0, v1);
    const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    for (int k : {0, 1, 2, 0, 2, 3}) {
        HudVertex v;
        v.x = xs[k];
        v.y = ys[k];
        v.u = us[k] / tw;
        v.v = vs[k] / th;
        t.push_back(v);
    }
}

// The opaque share of an atlas rectangle
float coverage(const PromptAtlas& a, PromptAtlas::Rect r) {
    const int x0 = int(r.u0 * float(a.width())), x1 = int(r.u1 * float(a.width())), y0 = int(r.v0 * float(a.height())), y1 = int(r.v1 * float(a.height()));
    int n = 0, on = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x, ++n) on += a.rgba()[4 * (std::size_t(y) * std::size_t(a.width()) + std::size_t(x)) + 3] > 128;
    return n ? float(on) / float(n) : 0.0f;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

TEST(labels_per_device) {
    using b3replay::prompt_label;
    // Xbox and Nintendo by SDL's positions: the pad's South (Cross) is A on an Xbox pad, B on a Nintendo one
    CHECK(prompt_label(PromptDevice::kXbox, PadButton::kCross) == "A" && prompt_label(PromptDevice::kXbox, PadButton::kCircle) == "B");
    CHECK(prompt_label(PromptDevice::kXbox, PadButton::kSquare) == "X" && prompt_label(PromptDevice::kXbox, PadButton::kTriangle) == "Y");
    CHECK(prompt_label(PromptDevice::kNintendo, PadButton::kCross) == "B" && prompt_label(PromptDevice::kNintendo, PadButton::kCircle) == "A");
    CHECK(prompt_label(PromptDevice::kNintendo, PadButton::kSquare) == "Y" && prompt_label(PromptDevice::kNintendo, PadButton::kTriangle) == "X");
    CHECK(prompt_label(PromptDevice::kXbox, PadButton::kL1) == "LB" && prompt_label(PromptDevice::kXbox, PadButton::kR2) == "RT");
    CHECK(prompt_label(PromptDevice::kNintendo, PadButton::kL2) == "ZL" && prompt_label(PromptDevice::kNintendo, PadButton::kStart) == "+");
    CHECK(prompt_label(PromptDevice::kPlayStation, PadButton::kTriangle) == "TRIANGLE");
    CHECK(prompt_label(PromptDevice::kGeneric, PadButton::kL1) == "L1");
    PromptDevice d = PromptDevice::kKeyboard;
    CHECK(b3replay::prompt_device_parse("XBOX", d) && d == PromptDevice::kXbox);
    CHECK(b3replay::prompt_device_parse("generic", d) && d == PromptDevice::kGeneric);
    CHECK(!b3replay::prompt_device_parse("dreamcast", d));
    // the setting: Auto follows the device, the others fix one
    CHECK(b3replay::prompt_resolve(0, PromptDevice::kXbox) == PromptDevice::kXbox);
    CHECK(b3replay::prompt_resolve(1, PromptDevice::kXbox) == PromptDevice::kPlayStation);
    CHECK(b3replay::prompt_resolve(5, PromptDevice::kXbox) == PromptDevice::kKeyboard);
}

TEST(pad_models) {
    using b3replay::prompt_device_for_pad;
    // SDL3's SDL_GamepadType: 0 unknown, 1 standard, 2 Xbox 360, 3 Xbox One, 4 PS3, 5 PS4, 6 PS5, 7 Switch Pro;
    // SDL_GamepadButtonLabel: 1 A, 2 B, 5 Cross (nativepad.cpp static_asserts them)
    CHECK(prompt_device_for_pad(4, 5) == PromptDevice::kPlayStation);         // PS3: the game's own pictures
    CHECK(prompt_device_for_pad(5, 5) == PromptDevice::kPlayStationModern);   // PS4
    CHECK(prompt_device_for_pad(6, 5) == PromptDevice::kPlayStationModern);   // PS5
    CHECK(prompt_device_for_pad(0, 5) == PromptDevice::kPlayStation);         // a Cross pad of no known model
    CHECK(prompt_device_for_pad(3, 1) == PromptDevice::kXbox && prompt_device_for_pad(2, 1) == PromptDevice::kXbox);
    CHECK(prompt_device_for_pad(7, 2) == PromptDevice::kNintendo);
    CHECK(prompt_device_for_pad(0, 0) == PromptDevice::kGeneric);
    CHECK(b3replay::prompt_label(PromptDevice::kPlayStationModern, PadButton::kStart) == "OPTIONS");
    CHECK(b3replay::prompt_label(PromptDevice::kPlayStationModern, PadButton::kSelect) == "CREATE");
    CHECK(b3replay::prompt_label(PromptDevice::kPlayStationModern, PadButton::kCross) == "CROSS");
    PromptDevice d = PromptDevice::kKeyboard;
    CHECK(b3replay::prompt_device_parse("ps5", d) && d == PromptDevice::kPlayStationModern);
    CHECK(b3replay::prompt_device_parse("PS4", d) && d == PromptDevice::kPlayStationModern);
    CHECK(b3replay::prompt_device_parse("playstation", d) && d == PromptDevice::kPlayStation);
    CHECK(b3replay::prompt_resolve(6, PromptDevice::kXbox) == PromptDevice::kPlayStationModern);
    // no pad known: the original look
    CHECK(PromptDeviceTracker{}.active() == PromptDevice::kPlayStation);
}

TEST(modern_playstation_replaces_only_start) {
    PromptAtlas a;
    a.build(PromptDevice::kPlayStationModern, {}, 1080);
    // the face buttons and the d-pad stay the game's
    std::vector<HudVertex> cross, out;
    quad(cross, 584.5f, 407.8f, 605.5f, 427.4f, 1, 33, 22, 54, 128, 64);
    quad(cross, 589.5f, 412.5f, 600.5f, 422.75f, 2, 3, 13, 14, 128, 64);
    CHECK(!b3replay::prompt_substitute("Buttons", 128, 64, cross, a, out));
    std::vector<HudVertex> arm;
    quad(arm, 313, 417.62f, 327, 432.56f, 1, 0, 15, 16, 32, 16);
    CHECK(!b3replay::prompt_substitute("dpad", 32, 16, arm, a, out));
    std::vector<HudVertex> sh;
    quad(sh, 100, 100, 135, 115, 7, 5, 57, 26, 64, 32);
    CHECK(!b3replay::prompt_substitute("l1button", 64, 32, sh, a, out));
    // START (the pause menu's "START Resume") becomes OPTIONS
    std::vector<HudVertex> start;
    quad(start, 160.4f, 460.6f, 193.4f, 480.2f, 67, 39, 125, 75, 128, 64);
    quad(start, 160.4f, 460.6f, 193.4f, 480.2f, 67, 1, 125, 37, 128, 64);
    CHECK(b3replay::prompt_substitute("Buttons", 128, 64, start, a, out));
    CHECK(near(out[0].u, a.cell(PadButton::kStart, GlyphLayer::kBase).u0) && near(out[6].u, a.cell(PadButton::kStart, GlyphLayer::kSymbol).u0));
    CHECK(coverage(a, a.cell(PadButton::kStart, GlyphLayer::kSymbol)) > 0.02f);
}

TEST(key_caps_from_the_key_map) {
    namespace sc = b3native::input::sc;
    const auto& map = b3native::input::default_key_map();
    const PromptKeys menu = b3replay::prompt_keys(map, false), drive = b3replay::prompt_keys(map, true);
    auto key = [](const PromptKeys& k, PadButton b) { return k.scancode[std::size_t(b)]; };
    CHECK(key(menu, PadButton::kCross) == sc::Return);      // Enter confirms (not Space, also bound in menus)
    CHECK(key(menu, PadButton::kTriangle) == sc::Backspace); // back
    CHECK(key(menu, PadButton::kStart) == sc::Escape);
    CHECK(key(menu, PadButton::kCircle) == sc::X);
    CHECK(key(menu, PadButton::kL1) == sc::Q && key(menu, PadButton::kR1) == sc::R && key(menu, PadButton::kR2) == sc::E);
    CHECK(key(drive, PadButton::kCross) == sc::W);           // accelerate: the driving binding first
    CHECK(key(drive, PadButton::kSquare) == sc::S);
    CHECK(key(drive, PadButton::kR1) == sc::Space);          // boost
    CHECK(key(menu, PadButton::kSquare) == sc::V);           // fe1: V (Z X C V = Cross Circle Triangle Square)
    CHECK(b3replay::key_cap_name(sc::Return) == "ENTER" && b3replay::key_cap_name(sc::Q) == "Q" && b3replay::key_cap_name(sc::N1) == "1");
    CHECK(b3replay::prompt_label(PromptDevice::kKeyboard, PadButton::kStart, menu) == "ESC");
    // a remapped key shows (custom maps: the actual bound key)
    std::vector<b3native::input::KeyBinding> custom = {{sc::C, b3native::input::Ds2::Cross}};
    CHECK(b3replay::prompt_label(PromptDevice::kKeyboard, PadButton::kCross, b3replay::prompt_keys(custom, false)) == "C");
    CHECK(b3replay::prompt_label(PromptDevice::kKeyboard, PadButton::kCircle, b3replay::prompt_keys(custom, false)) == "?");
}

TEST(switching_with_hysteresis) {
    PromptDeviceTracker t;
    CHECK(!t.known() && t.active() == PromptDevice::kPlayStation);   // nothing yet: the game's own prompts
    t.press(PromptDevice::kKeyboard, 1.0);
    CHECK(t.active() == PromptDevice::kKeyboard);
    // a pad's stick brushed past half way for a moment does not switch
    t.axis(PromptDevice::kXbox, true, 2.0);
    t.axis(PromptDevice::kXbox, true, 2.05);
    t.axis(PromptDevice::kXbox, false, 2.1);
    t.axis(PromptDevice::kXbox, true, 2.12);
    CHECK(t.active() == PromptDevice::kKeyboard);
    // held: it does
    for (double s = 2.12; s < 2.4; s += 1.0 / 60) t.axis(PromptDevice::kXbox, true, s);
    CHECK(t.active() == PromptDevice::kXbox);
    // a key switches back at once; the stick still held right after does not flip it again (the settle time)
    t.press(PromptDevice::kKeyboard, 3.0);
    CHECK(t.active() == PromptDevice::kKeyboard);
    for (double s = 3.0; s < 3.4; s += 1.0 / 60) t.axis(PromptDevice::kXbox, true, s);
    CHECK(t.active() == PromptDevice::kKeyboard);
    for (double s = 3.4; s < 3.8; s += 1.0 / 60) t.axis(PromptDevice::kXbox, true, s);
    CHECK(t.active() == PromptDevice::kXbox);
    // a button press switches at once, between pads too
    t.press(PromptDevice::kNintendo, 4.0);
    CHECK(t.active() == PromptDevice::kNintendo);
    // the last pad removed: back to the keyboard when it was used, else to the game's prompts
    t.pads_gone(true, 5.0);
    CHECK(t.active() == PromptDevice::kKeyboard);
    PromptDeviceTracker u;
    u.press(PromptDevice::kXbox, 1.0);
    u.pads_gone(false, 2.0);
    CHECK(!u.known() && u.active() == PromptDevice::kPlayStation);
}

TEST(atlas_cells) {
    const PromptKeys keys = b3replay::prompt_keys(b3native::input::default_key_map(), false);
    for (PromptDevice d : {PromptDevice::kXbox, PromptDevice::kNintendo, PromptDevice::kGeneric, PromptDevice::kKeyboard, PromptDevice::kPlayStationModern}) {
        for (int h : {448, 1080, 2160}) {
            PromptAtlas a;
            a.build(d, keys, h);
            CHECK(a.width() > 0 && a.height() > 0 && a.rgba().size() == std::size_t(a.width()) * std::size_t(a.height()) * 4);
            CHECK(a.width() <= 4096 && a.height() <= 4096);
            for (int b = 0; b < b3replay::kPadButtons; ++b)
                for (GlyphLayer l : {GlyphLayer::kBase, GlyphLayer::kSymbol}) {
                    const PromptAtlas::Rect r = a.cell(PadButton(b), l);
                    CHECK(r.u1 > r.u0 && r.v1 > r.v0 && r.u0 >= 0 && r.v1 <= 1);
                    // every glyph draws something, and leaves its cell's corner clear (no bleeding into neighbours)
                    CHECK(coverage(a, r) > 0.01f);
                    const int cx = int(r.u0 * float(a.width())), cy = int(r.v0 * float(a.height()));
                    CHECK(a.rgba()[4 * (std::size_t(cy) * std::size_t(a.width()) + std::size_t(cx)) + 3] < 16);
                }
            CHECK(coverage(a, a.dpad_arm(false)) > 0.02f && coverage(a, a.dpad_arm(true)) > 0.02f);
        }
    }
    // the device's own symbols differ: Xbox A is green, Nintendo B white
    PromptAtlas x, n;
    x.build(PromptDevice::kXbox, keys, 720);
    n.build(PromptDevice::kNintendo, keys, 720);
    auto mean_green_minus_red = [](const PromptAtlas& a, PromptAtlas::Rect r) {
        double g = 0;
        int k = 0;
        for (int y = int(r.v0 * float(a.height())); y < int(r.v1 * float(a.height())); ++y)
            for (int xx = int(r.u0 * float(a.width())); xx < int(r.u1 * float(a.width())); ++xx) {
                const std::uint8_t* p = &a.rgba()[4 * (std::size_t(y) * std::size_t(a.width()) + std::size_t(xx))];
                if (p[3] > 200) { g += double(p[1]) - double(p[0]); ++k; }
            }
        return k ? g / k : 0.0;
    };
    CHECK(mean_green_minus_red(x, x.cell(PadButton::kCross, GlyphLayer::kSymbol)) > 60);
    CHECK(std::fabs(mean_green_minus_red(n, n.cell(PadButton::kCross, GlyphLayer::kSymbol))) < 10);
}

TEST(substitutes_the_prompt_bar_buttons) {
    PromptAtlas a;
    a.build(PromptDevice::kXbox, {}, 1080);
    // "Select (X)" as captured (main menu, poll 8800): the ring 584.5..605.5 (texels 1,33 21x21), the Cross symbol
    // 589.5..600.5 (texels 2,3 11x11)
    std::vector<HudVertex> tri, out;
    quad(tri, 584.5f, 407.8f, 605.5f, 427.4f, 1, 33, 22, 54, 128, 64);
    quad(tri, 589.5f, 412.5f, 600.5f, 422.75f, 2, 3, 13, 14, 128, 64);
    CHECK(b3replay::prompt_substitute("Buttons", 128, 64, tri, a, out));
    CHECK(out.size() == tri.size());
    const PromptAtlas::Rect base = a.cell(PadButton::kCross, GlyphLayer::kBase), sym = a.cell(PadButton::kCross, GlyphLayer::kSymbol);
    CHECK(near(out[0].u, base.u0) && near(out[0].v, base.v0) && near(out[2].u, base.u1) && near(out[2].v, base.v1));
    CHECK(near(out[6].u, sym.u0) && near(out[8].v, sym.v1));
    // the symbol's quad grows to the button's box (21 / 11), about its centre
    CHECK(std::fabs(out[6].x - 584.5f) < 0.05f && std::fabs(out[7].x - 605.5f) < 0.05f);
    CHECK(out[0].x == tri[0].x && out[0].y == tri[0].y);   // the base stays where the game put it
    // Triangle (texels 15,2 13x13) and Start (the 33 x 21 button: symbol 67,1, base 67,39 58x36)
    std::vector<HudVertex> t2, o2;
    quad(t2, 34.5f, 407.8f, 55.5f, 427.4f, 1, 33, 22, 54, 128, 64);
    quad(t2, 38.5f, 411.5f, 51.5f, 423.6f, 15, 2, 28, 15, 128, 64);
    quad(t2, 160.4f, 460.6f, 193.4f, 480.2f, 67, 39, 125, 75, 128, 64);
    quad(t2, 160.4f, 460.6f, 193.4f, 480.2f, 67, 1, 125, 37, 128, 64);
    CHECK(b3replay::prompt_substitute("Buttons", 128, 64, t2, a, o2));
    CHECK(near(o2[6].u, a.cell(PadButton::kTriangle, GlyphLayer::kSymbol).u0));
    CHECK(near(o2[12].u, a.cell(PadButton::kStart, GlyphLayer::kBase).u0) && near(o2[18].u, a.cell(PadButton::kStart, GlyphLayer::kSymbol).u0));
    // an HD raster (2x) is the same layout
    std::vector<HudVertex> o3;
    CHECK(b3replay::prompt_substitute("Buttons", 256, 128, tri, a, o3) && near(o3[6].u, sym.u0));
    // anything else stays the game's: an unknown piece (the Select outline, texels 30,17), another texture, PlayStation
    std::vector<HudVertex> t4 = tri, o4;
    quad(t4, 0, 0, 16, 11, 30, 17, 46, 28, 128, 64);
    CHECK(!b3replay::prompt_substitute("Buttons", 128, 64, t4, a, o4));
    CHECK(!b3replay::prompt_substitute("GlobalFont", 128, 64, tri, a, o4));
    PromptAtlas ps;
    ps.build(PromptDevice::kPlayStation, {}, 1080);
    CHECK(!b3replay::prompt_substitute("Buttons", 128, 64, tri, ps, o4));
}

// remap1: a pad's bindings moved Cross to East (and Circle to South): the prompts show the bound positions; the
// defaults change nothing (PromptKeys{})
TEST(remapped_pad_glyphs) {
    using b3native::input::Ds2;
    std::array<Ds2, 16> same{};
    for (int b = 0; b < 16; ++b) same[std::size_t(b)] = Ds2(b);
    CHECK(b3replay::prompt_pad_glyphs(same) == PromptKeys{});
    CHECK(!b3replay::prompts_substituted(PromptDevice::kPlayStation, PromptKeys{}));
    std::array<Ds2, 16> swapped = same;
    swapped[std::size_t(Ds2::Cross)] = Ds2::Circle;
    swapped[std::size_t(Ds2::Circle)] = Ds2::Cross;
    const PromptKeys k = b3replay::prompt_pad_glyphs(swapped);
    CHECK(k.shown(PadButton::kCross) == PadButton::kCircle && k.shown(PadButton::kCircle) == PadButton::kCross);
    CHECK(k.shown(PadButton::kSquare) == PadButton::kSquare && k.pad_remapped());
    CHECK(b3replay::prompt_label(PromptDevice::kXbox, PadButton::kCross, k) == "B");
    CHECK(b3replay::prompt_label(PromptDevice::kPlayStation, PadButton::kCross, k) == "CIRCLE");
    CHECK(b3replay::prompt_label(PromptDevice::kXbox, PadButton::kSquare, k) == "X");
    CHECK(b3replay::prompts_substituted(PromptDevice::kPlayStation, k));
    // the PlayStation: a moved button's draw is redrawn, the others stay the game's
    PromptAtlas ps;
    ps.build(PromptDevice::kPlayStation, k, 1080);
    std::vector<HudVertex> cross, out;
    quad(cross, 584.5f, 407.8f, 605.5f, 427.4f, 1, 33, 22, 54, 128, 64);
    quad(cross, 589.5f, 412.5f, 600.5f, 422.75f, 2, 3, 13, 14, 128, 64);
    CHECK(b3replay::prompt_substitute("Buttons", 128, 64, cross, ps, out));
    std::vector<HudVertex> tri;
    quad(tri, 34.5f, 407.8f, 55.5f, 427.4f, 1, 33, 22, 54, 128, 64);
    quad(tri, 38.5f, 411.5f, 51.5f, 423.6f, 15, 2, 28, 15, 128, 64);
    CHECK(!b3replay::prompt_substitute("Buttons", 128, 64, tri, ps, out));
    // Xbox: Cross's symbol cell now holds B (East's letter), drawn differently from the default A
    PromptAtlas xa, xb;
    xa.build(PromptDevice::kXbox, {}, 1080);
    xb.build(PromptDevice::kXbox, k, 1080);
    CHECK(xa.rgba() != xb.rgba());
    PromptAtlas xc;
    xc.build(PromptDevice::kXbox, b3replay::prompt_pad_glyphs(same), 1080);
    CHECK(xa.rgba() == xc.rgba());
}

// padr2: the default pad binds L2 / R2 to the stick clicks, so their prompts show L3 / R3 (the HUD's Crashbreaker
// picture "R2Button" next to CRASHBREAKER [C 0x3A3250]: one quad of the whole 64 x 32 raster); the other pictures stay
TEST(default_pad_stick_click_glyphs) {
    using b3native::input::Ds2;
    std::array<Ds2, 16> def{};   // input_bindings.h pad_glyphs(default_player(0)) (test_input_bindings checks it)
    for (int b = 0; b < 16; ++b) def[std::size_t(b)] = Ds2(b);
    def[std::size_t(Ds2::L2)] = Ds2::L3;
    def[std::size_t(Ds2::R2)] = Ds2::R3;
    const PromptKeys k = b3replay::prompt_pad_glyphs(def);
    CHECK(k.shown(PadButton::kR2) == PadButton::kR3 && k.shown(PadButton::kL2) == PadButton::kL3);
    CHECK(k.shown(PadButton::kR1) == PadButton::kR1 && k.shown(PadButton::kCross) == PadButton::kCross);
    CHECK(b3replay::prompt_label(PromptDevice::kPlayStation, PadButton::kR2, k) == "R3");
    CHECK(b3replay::prompt_label(PromptDevice::kPlayStationModern, PadButton::kR2, k) == "R3");
    CHECK(b3replay::prompt_label(PromptDevice::kXbox, PadButton::kR2, k) == "RS");
    CHECK(b3replay::prompt_label(PromptDevice::kNintendo, PadButton::kR2, k) == "RS");
    CHECK(b3replay::prompt_label(PromptDevice::kGeneric, PadButton::kR2, k) == "R3");
    CHECK(b3replay::prompt_label(PromptDevice::kXbox, PadButton::kL2, k) == "LS");
    CHECK(b3replay::prompt_texture("R2Button") && b3replay::prompt_texture("L1Button") && b3replay::prompt_texture("l2button"));
    CHECK(!b3replay::prompt_texture("R3Button"));
    for (PromptDevice d : {PromptDevice::kPlayStation, PromptDevice::kPlayStationModern, PromptDevice::kXbox, PromptDevice::kNintendo,
                           PromptDevice::kGeneric}) {
        PromptAtlas a;
        a.build(d, k, 1080);
        // the HUD's R2 picture: 45 x 25 at (200, 100), the whole raster
        std::vector<HudVertex> r2, out;
        quad(r2, 200, 100, 245, 125, 0, 0, 64, 32, 64, 32);
        CHECK(b3replay::prompt_substitute("R2Button", 64, 32, r2, a, out));
        CHECK(out.size() == 12);   // base, then symbol
        if (out.size() != 12) continue;
        CHECK(near(out[0].u, a.cell(PadButton::kR2, GlyphLayer::kBase).u0) && near(out[6].u, a.cell(PadButton::kR2, GlyphLayer::kSymbol).u0));
        CHECK(near(out[2].u, a.cell(PadButton::kR2, GlyphLayer::kBase).u1) && near(out[8].v, a.cell(PadButton::kR2, GlyphLayer::kSymbol).v1));
        // over the picture's part of the quad (texels 5,4 .. 59,28)
        CHECK(near(out[0].x, 200 + 45 * 5.0f / 64) && near(out[0].y, 100 + 25 * 4.0f / 32));
        CHECK(near(out[2].x, 200 + 45 * 59.0f / 64) && near(out[2].y, 100 + 25 * 28.0f / 32));
        CHECK(near(out[6].x, out[0].x) && near(out[8].y, out[2].y));
        CHECK(coverage(a, a.cell(PadButton::kR2, GlyphLayer::kSymbol)) > 0.02f);
        // a mirrored draw keeps its mirror
        std::vector<HudVertex> m;
        quad(m, 200, 100, 245, 125, 64, 0, 0, 32, 64, 32);
        CHECK(b3replay::prompt_substitute("R2Button", 64, 32, m, a, out) && out.size() == 12 &&
              near(out[0].u, a.cell(PadButton::kR2, GlyphLayer::kBase).u1) && near(out[0].x, 200 + 45 * 5.0f / 64));
        // R1 (Impact Time) did not move: the PlayStation looks keep the game's picture. As captured (the crash run,
        // 1280 x 720, poll 12100: "R1 IMPACT TIME"): texels 1,1..64,32 over 443.38,28.88..487.38,51.31
        std::vector<HudVertex> r1;
        quad(r1, 443.38f, 28.88f, 487.38f, 51.31f, 1, 1, 64, 32, 64, 32);
        const bool own = d == PromptDevice::kPlayStation || d == PromptDevice::kPlayStationModern;
        CHECK(b3replay::prompt_substitute("R1Button", 64, 32, r1, a, out) == !own);
        if (!own) {
            CHECK(out.size() == 12 && near(out[0].u, a.cell(PadButton::kR1, GlyphLayer::kBase).u0));
            CHECK(near(out[0].x, 443.38f + 44.0f * 4.0f / 63.0f) && near(out[0].y, 28.88f + 22.43f * 3.0f / 31.0f));
            CHECK(near(out[2].x, 443.38f + 44.0f * 58.0f / 63.0f) && near(out[2].y, 28.88f + 22.43f * 27.0f / 31.0f));
        }
        // the front end's shoulder picture by its raster name (base then symbol, the button's rectangle)
        std::vector<HudVertex> l2;
        quad(l2, 100, 100, 135, 115, 7, 5, 57, 26, 64, 32);
        quad(l2, 100, 100, 135, 115, 7, 5, 57, 26, 64, 32);
        CHECK(b3replay::prompt_substitute("L2Button", 64, 32, l2, a, out) && out.size() == 12);
    }
    // the PlayStation's R2 cell now reads R3: the atlas differs from the defaults'
    PromptAtlas own, moved;
    own.build(PromptDevice::kPlayStation, {}, 1080);
    moved.build(PromptDevice::kPlayStation, k, 1080);
    CHECK(own.rgba() != moved.rgba());
}

// promptround: a round button drawn in a box that is not its own (Impact Time's R1 picture bound to the Xbox A) stays
// round on screen, its letter shrinks with its disc, and the menu's face buttons (already square on screen) keep their
// quads; one letter is centred on its strokes' weight
TEST(round_buttons_stay_round_on_screen) {
    using b3native::input::Ds2;
    std::array<Ds2, 16> def{};
    for (int b = 0; b < 16; ++b) def[std::size_t(b)] = Ds2(b);
    def[std::size_t(Ds2::R1)] = Ds2::Cross;
    const PromptKeys k = b3replay::prompt_pad_glyphs(def);
    CHECK(k.shown(PadButton::kR1) == PadButton::kCross);
    PromptAtlas a;
    a.build(PromptDevice::kXbox, k, 720);
    CHECK(a.round(PadButton::kR1) && a.round(PadButton::kCross) && !a.round(PadButton::kL1) && !a.round(PadButton::kR2));
    const float ua = 448.0f / 480.0f;   // the 640 x 448 2D in the 4:3 box
    // the ink's size on screen (y units) of layer `l` of the six vertices from `o`, its cell button `b`
    auto ink_on_screen = [&](const std::vector<HudVertex>& out, std::size_t o, PadButton b, GlyphLayer l, float& w, float& h) {
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
        for (std::size_t i = o; i < o + 6; ++i) { x0 = std::min(x0, out[i].x); x1 = std::max(x1, out[i].x); y0 = std::min(y0, out[i].y); y1 = std::max(y1, out[i].y); }
        const PromptAtlas::Rect c = a.cell(b, l), ink = a.ink(b, l);
        w = (x1 - x0) * ua * (ink.u1 - ink.u0) / (c.u1 - c.u0);
        h = (y1 - y0) * (ink.v1 - ink.v0) / (c.v1 - c.v0);
    };
    std::vector<HudVertex> r1, out;
    quad(r1, 443.38f, 28.88f, 487.38f, 51.31f, 1, 1, 64, 32, 64, 32);   // as captured (the crash run, poll 12100)
    CHECK(b3replay::prompt_substitute("R1Button", 64, 32, r1, a, out, ua) && out.size() == 12);
    if (out.size() == 12) {
        float bw = 0, bh = 0, sw = 0, sh = 0;
        ink_on_screen(out, 0, PadButton::kR1, GlyphLayer::kBase, bw, bh);
        ink_on_screen(out, 6, PadButton::kR1, GlyphLayer::kSymbol, sw, sh);
        CHECK(std::fabs(bw / bh - 1.0f) < 0.03f);   // was 0.87: an upright oval
        // the symbol's quad the base's (the same centre and size)
        for (std::size_t i = 0; i < 6; ++i) CHECK(std::fabs(out[i].x - out[i + 6].x) < 1e-3f && std::fabs(out[i].y - out[i + 6].y) < 1e-3f);
        // the disc's middle where the picture's middle was
        CHECK(std::fabs(0.5f * (out[0].x + out[2].x) - (443.38f + 44.0f * 31.0f / 63.0f)) < 0.01f);
        // the letter as big against its ring as in a face button's own box (it was 0.81, over the ring's edge)
        PromptAtlas::Rect fb = a.ink(PadButton::kCross, GlyphLayer::kBase), fs = a.ink(PadButton::kCross, GlyphLayer::kSymbol);
        PromptAtlas::Rect rb = a.ink(PadButton::kR1, GlyphLayer::kBase), rs = a.ink(PadButton::kR1, GlyphLayer::kSymbol);
        const float own = (fs.v1 - fs.v0) / (fb.v1 - fb.v0), in_r1 = (rs.v1 - rs.v0) / (rb.v1 - rb.v0);
        CHECK(std::fabs(own - in_r1) < 0.08f && in_r1 < 0.7f);
    }
    // the menu's prompt bar (the select mode page, as traced): the ring 21 x 19.62 is square on screen, so kept
    std::vector<HudVertex> menu, mo;
    quad(menu, 34.5f, 407.81f, 55.5f, 427.44f, 1, 33, 22, 54, 128, 64);
    quad(menu, 38.5f, 411.56f, 51.5f, 423.69f, 15, 2, 28, 15, 128, 64);
    CHECK(b3replay::prompt_substitute("Buttons", 128, 64, menu, a, mo, ua) && mo.size() == 12);
    if (mo.size() == 12) {
        CHECK(std::fabs(mo[0].x - 34.5f) < 0.05f && std::fabs(mo[2].x - 55.5f) < 0.05f && std::fabs(mo[0].y - 407.81f) < 0.05f);
        CHECK(std::fabs(0.5f * (mo[6].x + mo[8].x) - 45.0f) < 1e-3f && std::fabs(0.5f * (mo[6].y + mo[8].y) - 417.625f) < 1e-3f);
    }
    // a pill (not round) is left as the game's picture lays it out
    std::vector<HudVertex> l1, lo;
    quad(l1, 100, 100, 135, 115, 7, 5, 57, 26, 64, 32);
    quad(l1, 100, 100, 135, 115, 7, 5, 57, 26, 64, 32);
    CHECK(b3replay::prompt_substitute("L1Button", 64, 32, l1, a, lo, ua) && lo.size() == 12 && near(lo[0].x, 100) && near(lo[2].x, 135));
    // one letter on its strokes' weight: B's stem pulls it left of its box's middle; A and X are symmetric
    using b3replay::glyph::text_mass_x;
    using b3replay::glyph::text_width;
    CHECK(text_mass_x("B", 100, 10) < 0.5f * text_width("B", 100, 10) - 1.0f);
    CHECK(std::fabs(text_mass_x("A", 100, 10) - 0.5f * text_width("A", 100, 10)) < 0.01f);
    CHECK(std::fabs(text_mass_x("X", 100, 10) - 0.5f * text_width("X", 100, 10)) < 0.01f);
}

TEST(substitutes_the_dpad_arms) {
    PromptAtlas a;
    a.build(PromptDevice::kKeyboard, b3replay::prompt_keys(b3native::input::default_key_map(), false), 720);
    // the d-pad hint as captured (select mode, poll 9600): up (flipped: its tip, texel row 0, at the centre), down,
    // left (mirrored), right
    std::vector<HudVertex> tri, out;
    quad(tri, 313, 402.69f, 327, 417.62f, 1, 0, 15, 16, 32, 16, true);
    quad(tri, 313, 417.62f, 327, 432.56f, 1, 0, 15, 16, 32, 16);
    quad(tri, 304, 411.12f, 320, 424.19f, 31, 1, 15, 15, 32, 16);
    quad(tri, 320, 411.12f, 336, 424.19f, 15, 1, 31, 15, 32, 16);
    CHECK(b3replay::prompt_substitute("dpad", 32, 16, tri, a, out));
    const PromptAtlas::Rect ud = a.dpad_arm(false), lr = a.dpad_arm(true);
    CHECK(near(out[0].u, ud.u0) && near(out[0].v, ud.v1));   // up: the top vertex samples the arm's outer end
    CHECK(near(out[6].v, ud.v0));                              // down: its top vertex the inner end
    CHECK(near(out[12].u, lr.u1) && near(out[18].u, lr.u0));   // left mirrored, right not
    for (std::size_t k = 0; k < tri.size(); ++k) CHECK(out[k].x == tri[k].x && out[k].y == tri[k].y);
    // the shoulder pictures (l1button / l2button: base then symbol, the same rectangle)
    std::vector<HudVertex> s, so;
    quad(s, 100, 100, 135, 115, 7, 5, 57, 26, 64, 32);
    quad(s, 100, 100, 135, 115, 7, 5, 57, 26, 64, 32);
    CHECK(b3replay::prompt_substitute("l1button", 64, 32, s, a, so));
    CHECK(near(so[0].u, a.cell(PadButton::kL1, GlyphLayer::kBase).u0) && near(so[6].u, a.cell(PadButton::kL1, GlyphLayer::kSymbol).u0));
}

// ---- the title's "Press START button to continue" (2026-11-10) ----

namespace {

// A font as the capture publishes it: letters, digits and '.' 8 x 10 texels on a 9 x 11 grid in a 200 x 100 raster
// (TEX0 256 x 128: a record's uv is in those units), advance 9; the space has no picture (u -1), advance 5
struct TestFont {
    b3replay::HostFont f;
    b3replay::HudTexture t;
    TestFont() {
        t.w = 200; t.h = 100; t.tw = 256; t.th = 128;
        const std::u16string cs = u"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.";
        for (std::size_t i = 0; i < cs.size(); ++i) {
            b3replay::HostFontGlyph g;
            g.u = float(i % 20 * 9) / 256.0f;
            g.v = float(i / 20 * 11) / 128.0f;
            g.w = 8.0f / 256.0f;
            g.h = 10.0f / 128.0f;
            g.advance = 9;
            g.code = cs[i];
            f.glyphs.push_back(g);
        }
        b3replay::HostFontGlyph sp;
        sp.u = -1; sp.advance = 5; sp.code = u' ';
        f.glyphs.push_back(sp);
    }
    // `s` laid out as RenderString does, at (x, y), `k` 2D units per texel; `rgba` each vertex's colour
    void draw(std::vector<HudVertex>& tri, std::u16string_view s, float x, float y, float k = 1.0f, std::uint8_t a = 0x80) const {
        for (char16_t c : s) {
            const b3replay::HostFontGlyph* g = nullptr;
            for (const auto& gg : f.glyphs) if (gg.code == c) g = &gg;
            if (g->u >= 0) {
                const float u0 = g->u * 256.0f, v0 = g->v * 128.0f;   // texels of the raster (as `quad` takes them)
                const std::size_t at = tri.size();
                quad(tri, x, y, x + 8 * k, y + 10 * k, u0, v0, u0 + 8, v0 + 10, 200, 100);
                for (std::size_t m = at; m < tri.size(); ++m) tri[m].rgba[3] = a;
            }
            x += g->advance * k;
        }
    }
};

}  // namespace

TEST(title_line_glyphs_read_back) {
    const TestFont tf;
    std::vector<HudVertex> tri;
    tf.draw(tri, u"Press START button to continue", 100, 300);
    CHECK(b3replay::font_quad_chars(tf.f, tf.t, tri) == u"PressSTARTbuttontocontinue");
    // a quad that is no glyph of the font: 0
    quad(tri, 0, 0, 8, 10, 190, 90, 198, 100, 200, 100);
    CHECK(b3replay::font_quad_chars(tf.f, tf.t, tri).back() == 0);
}

TEST(title_line_matcher) {
    using b3replay::find_text_line;
    using b3replay::kTitleLine;
    using b3replay::kTitleWord;
    const TestFont tf;
    auto find = [&](const std::vector<HudVertex>& tri) {
        return find_text_line(tri, b3replay::font_quad_chars(tf.f, tf.t, tri), kTitleLine, kTitleWord);
    };
    {   // the line alone: found, START its quads 5..9
        std::vector<HudVertex> tri;
        tf.draw(tri, u"Press START button to continue", 100, 300);
        const auto m = find(tri);
        CHECK(m.size() == 1 && m[0].line_first == 0 && m[0].line_quads == 26 && m[0].word_first == 5 && m[0].word_quads == 5);
    }
    {   // in one draw with other text (the copyright line above it, a shadow pass of it first): each copy found
        std::vector<HudVertex> tri;
        tf.draw(tri, u"Copyright 2004", 200, 400);
        tf.draw(tri, u"Press START button to continue", 101, 301);
        tf.draw(tri, u"Press START button to continue", 100, 300);
        tf.draw(tri, u"START", 100, 360);
        const auto m = find(tri);
        CHECK(m.size() == 2 && m[0].word_first == 13 + 5 && m[1].word_first == 13 + 26 + 5);
    }
    // never other text: the main menu's START item, the line inside a longer one (either end), the words run together,
    // the line with a letter missing, the letters on two rows
    for (std::u16string_view s : {u"START", u"Press START button to continue.", u"Now Press START button to continue",
                                  u"PressSTART button to continue", u"Press START button tocontinue", u"Press STRT button to continue"}) {
        std::vector<HudVertex> tri;
        tf.draw(tri, s, 100, 300);
        CHECK(find(tri).empty());
    }
    {
        std::vector<HudVertex> tri;
        tf.draw(tri, u"Press START", 100, 300);
        tf.draw(tri, u"button to continue", 100, 320);
        CHECK(find(tri).empty());
    }
}

TEST(title_start_drawn_as_the_device_button) {
    const TestFont tf;
    std::vector<HudVertex> tri, text, glyph;
    tf.draw(tri, u"Press START button to continue", 100, 300, 1.5f, 0x40);   // 1.5 units per texel, half faded
    const std::u16string chars = b3replay::font_quad_chars(tf.f, tf.t, tri);
    PromptAtlas ps;
    ps.build(PromptDevice::kPlayStation, {}, 720);
    CHECK(!b3replay::title_start_swap(tri, chars, ps, text, glyph));   // the PlayStation keeps the game's text
    for (PromptDevice d : {PromptDevice::kXbox, PromptDevice::kPlayStationModern, PromptDevice::kNintendo, PromptDevice::kKeyboard}) {
        PromptAtlas a;
        a.build(d, b3replay::prompt_keys(b3native::input::default_key_map(), false), 720);
        CHECK(b3replay::title_start_swap(tri, chars, a, text, glyph));
        CHECK(text.size() == (26 - 5) * 6 && glyph.size() == 12);
        // the button: base then symbol from the atlas's Start cells, the word's height x kTitleGlyphScale, 33 x 21,
        // its drawn part (ink) centred on the word (START: x 100 + (5 x 9 + 5) x 1.5 = 175 .. 175 + 4 x 13.5 + 12 =
        // 241; y 300..315)
        const PromptAtlas::Rect base = a.cell(PadButton::kStart, GlyphLayer::kBase), sym = a.cell(PadButton::kStart, GlyphLayer::kSymbol);
        CHECK(near(glyph[0].u, base.u0) && near(glyph[2].v, base.v1) && near(glyph[6].u, sym.u0) && near(glyph[8].u, sym.u1));
        const PromptAtlas::Rect ink = a.ink(PadButton::kStart, GlyphLayer::kBase);
        const float f0 = (ink.u0 - base.u0) / (base.u1 - base.u0), f1 = (ink.u1 - base.u0) / (base.u1 - base.u0);
        CHECK(f0 >= 0.0f && f1 <= 1.0f && f1 - f0 > 0.5f);
        if (d == PromptDevice::kXbox || d == PromptDevice::kNintendo) CHECK(f1 - f0 < 0.75f);   // round: narrower than its box
        const float gh = 15.0f * b3replay::kTitleGlyphScale, gw = gh * 33.0f / 21.0f;
        const float left = glyph[0].x + f0 * gw, right = glyph[0].x + f1 * gw;   // the drawn part
        CHECK(std::fabs(glyph[1].x - glyph[0].x - gw) < 1e-3f && std::fabs(0.5f * (left + right) - 208.0f) < 1e-3f);
        CHECK(std::fabs(glyph[0].y - (307.5f - 0.5f * gh)) < 1e-3f && std::fabs(glyph[2].y - (307.5f + 0.5f * gh)) < 1e-3f);
        CHECK(glyph[0].rgba[3] == 0x40 && glyph[11].rgba[3] == 0x40);   // the line's fade
        // the other words move out by half the difference each side: the line keeps its centre
        const float half = 0.5f * ((right - left) - 66.0f);
        CHECK(std::fabs(text[0].x - (100.0f - half)) < 1e-3f);                           // "P"
        float lo = 1e30f, hi = -1e30f;
        for (const HudVertex& v : text) { lo = std::min(lo, v.x); hi = std::max(hi, v.x); }
        float lo0 = 1e30f, hi0 = -1e30f;
        for (const HudVertex& v : tri) { lo0 = std::min(lo0, v.x); hi0 = std::max(hi0, v.x); }
        CHECK(std::fabs((lo + hi) - (lo0 + hi0)) < 1e-3f);
        CHECK(std::fabs(text[5 * 6].x - (tri[10 * 6].x + half)) < 1e-3f);                  // "b" of button
        // the gaps beside the button as beside the word: "Press" 's' (quad 4) to the button, the button to "button" 'b'
        CHECK(std::fabs((left - text[4 * 6 + 1].x) - (175.0f - tri[4 * 6 + 1].x)) < 1e-3f);
        CHECK(std::fabs((text[5 * 6].x - right) - (tri[10 * 6].x - 241.0f)) < 1e-3f);
    }
}

TEST(series_glyphs_beside_the_arrows) {
    // carsel1: the car pages' series arrows ("CarArrow", 16 x 16 here, centred on y 89, the name between them) get the
    // device's L1 left of the left one and R1 right of the right one; other CarArrow quads (the car list's, one above
    // the other) are not a pair
    std::vector<HudVertex> left, right, list;
    quad(left, 440, 81, 456, 97, 0, 0, 16, 16, 32, 16);
    quad(right, 578, 81, 594, 97, 16, 0, 0, 16, 32, 16);   // mirrored
    quad(list, 100, 120, 116, 136, 0, 0, 16, 16, 32, 16);
    quad(list, 100, 300, 116, 316, 0, 0, 16, 16, 32, 16);
    for (auto* t : {&left, &right}) for (HudVertex& v : *t) { v.rgba[0] = 0x80; v.rgba[1] = 0x60; v.rgba[2] = 0x10; v.rgba[3] = 0x50; }
    for (PromptDevice d : {PromptDevice::kPlayStation, PromptDevice::kXbox, PromptDevice::kKeyboard}) {
        PromptAtlas a;
        a.build(d, b3replay::prompt_keys(b3native::input::default_key_map(), false), 720);
        std::vector<std::vector<HudVertex>> out;
        CHECK(b3replay::series_glyphs({&list, &left, &right}, a, out));
        CHECK(out.size() == 3 && out[0].empty() && out[1].size() == 12 && out[2].size() == 12);
        const float gh = b3replay::kSeriesGlyphH, gw = gh * 35.0f / 15.0f, gap = b3replay::kSeriesGlyphGap;
        // L1: its right edge `gap` left of the left arrow; R1: its left edge `gap` right of the right arrow; centred on 89
        CHECK(near(out[1][1].x, 440 - gap) && near(out[1][0].x, 440 - gap - gw) && near(out[1][0].y, 89 - 0.5f * gh) && near(out[1][2].y, 89 + 0.5f * gh));
        CHECK(near(out[2][0].x, 594 + gap) && near(out[2][1].x, 594 + gap + gw));
        // base then symbol, from the atlas's L1 / R1 cells; white at the arrows' alpha
        const PromptAtlas::Rect lb = a.cell(PadButton::kL1, GlyphLayer::kBase), ls = a.cell(PadButton::kL1, GlyphLayer::kSymbol);
        const PromptAtlas::Rect rs = a.cell(PadButton::kR1, GlyphLayer::kSymbol);
        CHECK(near(out[1][0].u, lb.u0) && near(out[1][2].v, lb.v1) && near(out[1][6].u, ls.u0) && near(out[2][8].u, rs.u1));
        CHECK(out[1][0].rgba[0] == 0x80 && out[1][0].rgba[1] == 0x80 && out[1][0].rgba[2] == 0x80 && out[2][11].rgba[3] == 0x50);
        CHECK(coverage(a, ls) > 0.02f);   // the symbol has ink (the label: L1 / LB / the key)
    }
    // both arrows in one draw: both glyphs after it; no pair (one arrow, or a column): nothing
    std::vector<HudVertex> both = left;
    both.insert(both.end(), right.begin(), right.end());
    PromptAtlas a;
    a.build(PromptDevice::kXbox, {}, 720);
    std::vector<std::vector<HudVertex>> out;
    CHECK(b3replay::series_glyphs({&both}, a, out) && out.size() == 1 && out[0].size() == 24);
    CHECK(!b3replay::series_glyphs({&left, &list}, a, out) && out.size() == 2 && out[0].empty() && out[1].empty());
    CHECK(!b3replay::series_glyphs({&both}, PromptAtlas{}, out));   // no atlas
}

TEST_MAIN("button prompts (mac5)")
