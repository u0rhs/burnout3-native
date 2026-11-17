// Photo mode (remaster-features §5; src/b3replay/photomode.h): the free camera from and as a game camera, its
// controls, when it can be entered and left, and the pad withheld from the game until the exit's buttons are released.
#include "test.h"

#include "glyphdraw.h"
#include "photohud.h"
#include "photomode.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

using b3replay::Camera;
using b3replay::PhotoCam;
using b3replay::PhotoDof;
using b3replay::PhotoMode;
using b3replay::PhotoPad;

// A game camera at `pos` looking along the unit vector `at`, level (rows: screen left, up, at, position)
Camera game_camera(const float pos[3], const float at[3]) {
    Camera c;
    c.valid = true;
    float l[3] = {at[2], 0, -at[0]};
    const float n = std::sqrt(l[0] * l[0] + l[2] * l[2]);
    l[0] /= n;
    l[2] /= n;
    const float u[3] = {at[1] * l[2] - at[2] * l[1], at[2] * l[0] - at[0] * l[2], at[0] * l[1] - at[1] * l[0]};
    for (int k = 0; k < 3; ++k) { c.m.r[0][k] = l[k]; c.m.r[1][k] = u[k]; c.m.r[2][k] = at[k]; c.m.r[3][k] = pos[k]; }
    c.half_h = 0.5f;
    c.half_w = 0.5f * 4.0f / 3.0f;
    c.znear = 0.5f;
    c.zfar = 1000.0f;
    c.lod_pos[0] = 7;
    return c;
}

TEST(free_camera_starts_as_the_game_camera) {
    const float pos[3] = {10, 2, -30}, at[3] = {0.6f, 0.0f, 0.8f};
    const Camera g = game_camera(pos, at);
    PhotoCam p;
    p.from(g);
    CHECK_NEAR(p.fov, 2.0 * std::atan(0.5), 1e-6);
    CHECK_NEAR(p.roll, 0.0, 0.0);
    const Camera c = p.camera(g);
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 3; ++k) CHECK_NEAR(c.m.r[r][k], g.m.r[r][k], 1e-5);
    CHECK_NEAR(c.half_h, g.half_h, 1e-6);
    CHECK_NEAR(c.half_w, g.half_w, 1e-6);
    CHECK_NEAR(c.znear, 0.1f, 1e-7);   // close-ups: the near plane at most 0.1 m
    CHECK_NEAR(c.lod_pos[0], 7.0, 0.0);   // the rest stays the game's
}

TEST(roll_and_field_of_view) {
    const float pos[3] = {0, 0, 0}, at[3] = {0, 0, 1};
    const Camera g = game_camera(pos, at);
    PhotoCam p;
    p.from(g);
    p.roll = 3.14159265f / 2;   // a quarter turn clockwise: up points to screen right (-left)
    p.fov = 0.25f;
    const Camera c = p.camera(g);
    for (int k = 0; k < 3; ++k) CHECK_NEAR(c.m.r[1][k], -g.m.r[0][k], 1e-5);
    for (int k = 0; k < 3; ++k) CHECK_NEAR(c.m.r[2][k], g.m.r[2][k], 1e-6);   // the view direction unchanged
    CHECK_NEAR(c.half_h, std::tan(0.125), 1e-6);
    CHECK_NEAR(c.half_w / c.half_h, 4.0 / 3.0, 1e-5);   // the game's aspect kept
}

TEST(entering_moving_and_leaving) {
    PhotoMode& m = PhotoMode::instance();
    const float pos[3] = {0, 5, 0}, at[3] = {0, 0, 1};
    const Camera g = game_camera(pos, at);
    // not paused: P is the game's
    m.set_scene(true);
    CHECK(!m.key(SDL_SCANCODE_P, true));
    CHECK(!m.active());
    m.set_game_paused(true);
    m.set_scene(false);   // no race picture on screen (or the PC SETTINGS page over it)
    CHECK(!m.key(SDL_SCANCODE_P, true));
    m.set_scene(true);
    CHECK(!m.key(SDL_SCANCODE_P, true, false, SDL_KMOD_CTRL));   // Ctrl + P: the viewer's
    CHECK(m.key(SDL_SCANCODE_P, true));
    CHECK(m.active());
    CHECK(m.holds_input());
    CHECK(!m.key(SDL_SCANCODE_P, false));   // ups go on to the pad
    Camera c;
    CHECK(!m.camera(g, c));   // not started before the first step
    m.advance(1.0f / 60.0f, g);
    CHECK(m.camera(g, c));
    CHECK_NEAR(c.m.r[3][2], 0.0, 1e-6);
    // W held for one second: 10 m along the view
    CHECK(m.key(SDL_SCANCODE_W, true));
    for (int i = 0; i < 60; ++i) m.advance(1.0f / 60.0f, g);
    CHECK(!m.key(SDL_SCANCODE_W, false));
    m.camera(g, c);
    CHECK_NEAR(c.m.r[3][2], 10.0, 1e-3);
    CHECK_NEAR(c.m.r[3][1], 5.0, 1e-5);
    // E with Shift: up four times as fast
    m.key(SDL_SCANCODE_LSHIFT, true);
    m.key(SDL_SCANCODE_E, true);
    m.advance(0.25f, g);
    m.key(SDL_SCANCODE_E, false);
    m.key(SDL_SCANCODE_LSHIFT, false);
    m.camera(g, c);
    CHECK_NEAR(c.m.r[3][1], 15.0, 1e-3);
    // Left: turns left (yaw up: at moves toward +x, the screen left of +z)
    m.key(SDL_SCANCODE_LEFT, true);
    m.advance(0.25f, g);
    m.key(SDL_SCANCODE_LEFT, false);
    m.camera(g, c);
    CHECK(c.m.r[2][0] > 0.1f);
    // R: the game's camera again
    m.key(SDL_SCANCODE_R, true);
    m.key(SDL_SCANCODE_R, false);
    m.advance(1.0f / 60.0f, g);
    m.camera(g, c);
    for (int k = 0; k < 3; ++k) CHECK_NEAR(c.m.r[3][k], g.m.r[3][k], 1e-6);
    // Escape leaves; the game's keys are its own again
    CHECK(m.key(SDL_SCANCODE_ESCAPE, true));
    CHECK(!m.active());
    CHECK(!m.holds_input());
    CHECK(!m.camera(g, c));
    CHECK(!m.key(SDL_SCANCODE_ESCAPE, false));
    CHECK(!m.key(SDL_SCANCODE_W, true));
    m.key(SDL_SCANCODE_W, false);
}

// rebind2: photo mode's keys are the PortAction::Photo* bindings: a rebound key moves the camera, the old one does not
TEST(photo_keys_follow_the_bindings) {
    using b3native::input::PortAction;
    PhotoMode& m = PhotoMode::instance();
    const float pos[3] = {0, 5, 0}, at[3] = {0, 0, 1};
    const Camera g = game_camera(pos, at);
    b3native::input::PortControls b = b3native::input::default_port();
    b.keys[std::size_t(PortAction::PhotoMode)] = {SDL_SCANCODE_O};              // enter on O (P no longer does)
    b.keys[std::size_t(PortAction::PhotoForward)] = {SDL_SCANCODE_I};            // forward on I
    b.keys[std::size_t(PortAction::PhotoFast)] = {SDL_SCANCODE_LCTRL};           // fast on Ctrl itself
    b.keys[std::size_t(PortAction::PhotoExit)] = {SDL_SCANCODE_X};
    m.set_bindings(b);
    m.set_game_paused(true);
    m.set_scene(true);
    CHECK(!m.key(SDL_SCANCODE_P, true) && !m.active());
    CHECK(m.key(SDL_SCANCODE_O, true) && m.active());
    m.key(SDL_SCANCODE_O, false);
    m.advance(1.0f / 60.0f, g);
    CHECK(m.key(SDL_SCANCODE_W, true));   // taken (photo mode holds the keys) but W no longer moves
    for (int i = 0; i < 60; ++i) m.advance(1.0f / 60.0f, g);
    m.key(SDL_SCANCODE_W, false);
    Camera c;
    m.camera(g, c);
    CHECK_NEAR(c.m.r[3][2], 0.0, 1e-6);
    m.key(SDL_SCANCODE_I, true);
    for (int i = 0; i < 60; ++i) m.advance(1.0f / 60.0f, g);
    m.key(SDL_SCANCODE_I, false);
    m.camera(g, c);
    CHECK_NEAR(c.m.r[3][2], 10.0, 1e-3);
    // a bound Ctrl is a photo key, not the window's shortcut modifier: fast x4 with it held
    CHECK(m.key(SDL_SCANCODE_LCTRL, true, false, SDL_KMOD_CTRL));
    m.key(SDL_SCANCODE_I, true, false, SDL_KMOD_CTRL);
    m.advance(0.25f, g);
    m.key(SDL_SCANCODE_I, false);
    m.key(SDL_SCANCODE_LCTRL, false);
    m.camera(g, c);
    CHECK_NEAR(c.m.r[3][2], 20.0, 1e-3);
    CHECK(m.key(SDL_SCANCODE_X, true) && !m.active());   // the leave key
    m.key(SDL_SCANCODE_X, false);
    // the controls strip shows the bound keys and buttons; the defaults are the table's own
    CHECK(b3replay::photo_keys_from(b3native::input::default_port()) == b3replay::PhotoKeys{});
    const b3replay::PhotoKeys bk = b3replay::photo_keys_from(b);
    CHECK(bk.fwd == SDL_SCANCODE_I && bk.exit_key == SDL_SCANCODE_O && bk.exit_alt == SDL_SCANCODE_X && bk.fast == SDL_SCANCODE_LCTRL);
    const auto labels = b3replay::PhotoHud::strip_labels(b3replay::PromptDevice::kKeyboard, bk);
    CHECK(std::find(labels.begin(), labels.end(), "I") != labels.end() && std::find(labels.begin(), labels.end(), "W") == labels.end());
    b.pad[std::size_t(PortAction::PhotoDof)] = {b3native::input::PadInput::North};
    b.pad[std::size_t(PortAction::PhotoReset)] = {b3native::input::PadInput::Guide, b3native::input::PadInput::Misc1};
    const b3replay::PhotoKeys pk = b3replay::photo_keys_from(b);
    CHECK((pk.pad_dof == b3replay::PhotoPadKey{b3replay::PadButton::kTriangle, {}}) && pk.pad_reset.text == "PS+MIC");
    m.set_bindings(b3native::input::default_port());
    m.set_game_paused(false);
}

// devnav1: glyph::Canvas::strokes visits only the pixels near each segment (it measured every segment from every pixel
// of the whole box: ~1.2 s for the help box at an 1800-pixel window). The picture must be the one of the full search
// (the reference below: the code before), to the bit, for the help box's text at a few sizes.
TEST(strokes_as_the_full_search) {
    namespace gl = b3replay::glyph;
    const auto seg = [](float x, float y, gl::Pt a, gl::Pt b) {
        const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
        const float u = len2 > 0 ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / len2, 0.0f, 1.0f) : 0.0f;
        return std::hypot(x - (a.x + u * dx), y - (a.y + u * dy));
    };
    const auto reference = [&](gl::Canvas& c, const std::vector<gl::Line>& lines, float t, const float (&rgba)[4]) {
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (const gl::Line& l : lines)
            for (const gl::Pt& p : l) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
        if (x1 < x0) return;
        x0 -= t; y0 -= t; x1 += t; y1 += t;
        const int ix0 = std::max(0, int(std::floor(x0)) - 1), iy0 = std::max(0, int(std::floor(y0)) - 1);
        const int ix1 = std::min(c.w, int(std::ceil(x1)) + 1), iy1 = std::min(c.h, int(std::ceil(y1)) + 1);
        for (int y = iy0; y < iy1; ++y)
            for (int x = ix0; x < ix1; ++x) {
                float best = 1e30f;
                for (const gl::Line& l : lines)
                    for (std::size_t k = 0; k < l.size(); ++k)
                        best = std::min(best, seg(float(x) + 0.5f, float(y) + 0.5f, l[k], k + 1 < l.size() ? l[k + 1] : l[k]));
                const float cov = std::clamp(0.5f - (best - 0.5f * t), 0.0f, 1.0f);
                if (cov <= 0) continue;
                const float a = rgba[3] * cov;
                auto& d = c.px[std::size_t(y) * std::size_t(c.w) + std::size_t(x)];
                for (int k = 0; k < 3; ++k) d[k] = rgba[k] * a + d[k] * (1 - a);
                d[3] = a + d[3] * (1 - a);
            }
    };
    const float ink[4] = {0.95f, 0.8f, 0.25f, 1.0f}, bg[4] = {0.02f, 0.02f, 0.03f, 0.78f};
    int compared = 0;
    for (const float s : {0.75f, 1.0f, 1.7f}) {
        for (const std::string& text : PhotoMode::help_lines(false, 73)) {
            const float cap = 10.5f * s, t = std::max(1.3f, 1.7f * s);
            std::vector<gl::Line> l;
            const float w = gl::text(text, 3.3f, 2.6f, cap, t, l);
            gl::Canvas a(int(w) + 8, int(cap) + 8), b(int(w) + 8, int(cap) + 8);
            a.box(float(a.w) * 0.5f, float(a.h) * 0.5f, float(a.w) * 0.5f, float(a.h) * 0.5f, 3, bg);
            b.box(float(b.w) * 0.5f, float(b.h) * 0.5f, float(b.w) * 0.5f, float(b.h) * 0.5f, 3, bg);
            a.strokes(l, t, ink);
            reference(b, l, t, ink);
            bool same = a.px.size() == b.px.size();
            for (std::size_t k = 0; same && k < a.px.size(); ++k) same = a.px[k] == b.px[k];
            CHECK(same);
            ++compared;
        }
    }
    CHECK(compared == 12);
}

// devnav1: the help box kept between changes (Notice::picture_kept: one changed line redrawn in its rows) is the whole
// picture to the bit, through a zoom, a focus change, a size change and a prompt device change
TEST(help_box_kept_as_the_whole_picture) {
    b3replay::Notice n;
    PhotoDof d;
    d.on = true;
    int checked = 0;
    const auto same = [&](const std::vector<std::string>& lines, int window_h) {
        int w = 0, h = 0, w2 = 0, h2 = 0;
        std::vector<std::uint8_t> whole;
        const std::vector<std::uint8_t>& kept = n.picture_kept(lines, window_h, w, h);
        b3replay::Notice::picture(lines, window_h, w2, h2, whole);
        CHECK(w == w2 && h == h2 && kept == whole);
        ++checked;
    };
    for (int fov = 30; fov <= 120; fov += 7) same(PhotoMode::help_lines(false, fov, d, 1), 720);
    for (float st : {0.0f, 0.3f, 0.71f, -1.2f}) {
        d.stops = st;
        same(PhotoMode::help_lines(false, 64, d, 2), 720);
    }
    same(PhotoMode::help_lines(false, 64, d, 2), 1800);
    same(PhotoMode::help_lines(false, 65, d, 2), 1800);
    same(PhotoMode::help_lines(true, 65, d, 2), 1800);
    d.on = false;
    same(PhotoMode::help_lines(true, 66, d, 2), 1800);
    CHECK(checked == 21);
}

// devnav1: the held options. The field of view zooms by a ratio (narrow and wide alike), each option speeds up while
// held (x2 after 1.5 s), Alt is fine (x0.25), the roll turns 1.2 rad/s
TEST(held_options_by_ratio_and_hold) {
    PhotoMode& m = PhotoMode::instance();
    const float pos[3] = {0, 5, 0}, at[3] = {0, 0, 1};
    const Camera g = game_camera(pos, at);
    const double zoom = std::log(2.0) / 0.6;   // a halving in 0.6 s
    m.set_game_paused(true);
    m.set_scene(true);
    CHECK(m.key(SDL_SCANCODE_P, true));
    m.key(SDL_SCANCODE_P, false);
    m.advance(1.0f / 60.0f, g);
    Camera c;
    const auto fov = [&] {
        m.camera(g, c);
        return 2.0 * std::atan(double(c.half_h));
    };
    const double f0 = fov();
    // - for 0.3 s (18 steps): narrower by e^(-0.3 zoom); = for 0.3 s: back
    m.key(SDL_SCANCODE_MINUS, true);
    for (int i = 0; i < 18; ++i) m.advance(1.0f / 60.0f, g);
    m.key(SDL_SCANCODE_MINUS, false);
    const double f1 = fov();
    CHECK_NEAR(f1 / f0, std::exp(-0.3 * zoom), 1e-4);
    m.key(SDL_SCANCODE_EQUALS, true);
    for (int i = 0; i < 18; ++i) m.advance(1.0f / 60.0f, g);
    m.key(SDL_SCANCODE_EQUALS, false);
    CHECK_NEAR(fov(), f0, 1e-4);
    // the hold: 1.5 s of - with Alt (a quarter), x1 for 0.5 s then rising to x2; then x2
    m.key(SDL_SCANCODE_LALT, true);
    m.key(SDL_SCANCODE_MINUS, true);
    double sum = 0;   // the factors' integral, as photomode.cpp steps it
    for (int i = 0; i < 90; ++i) {
        sum += (1.0 + std::clamp((double(i) / 60.0 - 0.5) / 1.0, 0.0, 1.0)) / 60.0;
        m.advance(1.0f / 60.0f, g);
    }
    const double f2 = fov();
    CHECK_NEAR(f2 / f0, std::exp(-0.25 * zoom * sum), 1e-4);
    for (int i = 0; i < 30; ++i) m.advance(1.0f / 60.0f, g);
    CHECK_NEAR(fov() / f2, std::exp(-0.25 * zoom * 2.0 * 0.5), 1e-4);
    m.key(SDL_SCANCODE_MINUS, false);
    m.key(SDL_SCANCODE_LALT, false);
    // released: the next press starts at x1 again; C (roll) for 0.25 s: 0.3 rad (up leans to screen right)
    m.key(SDL_SCANCODE_C, true);
    for (int i = 0; i < 15; ++i) m.advance(1.0f / 60.0f, g);
    m.key(SDL_SCANCODE_C, false);
    m.camera(g, c);
    CHECK_NEAR(std::atan2(double(c.m.r[0][1]), double(c.m.r[0][0])), 0.3, 1e-4);
    CHECK(m.key(SDL_SCANCODE_ESCAPE, true));
    m.key(SDL_SCANCODE_ESCAPE, false);
    m.set_game_paused(false);
}

TEST(pad_combo_and_the_exit_latch) {
    PhotoMode& m = PhotoMode::instance();
    m.set_game_paused(true);
    m.set_scene(true);
    PhotoPad p;
    m.pad(p);
    p.target = true;
    m.pad(p);
    CHECK(!m.active());   // L3 alone: nothing
    p.toggle = true;
    m.pad(p);
    CHECK(m.active());
    m.pad(p);   // still held: no toggle
    CHECK(m.active());
    p = PhotoPad{};
    m.pad(p);
    p.east = true;   // B / Circle leaves ...
    m.pad(p);
    CHECK(!m.active());
    CHECK(m.holds_input());   // ... and is withheld from the game (the pause menu's Back) until released
    m.pad(p);
    CHECK(m.holds_input());
    p = PhotoPad{};
    m.pad(p);
    CHECK(!m.holds_input());
    // the pause menu going away ends photo mode
    CHECK(m.key(SDL_SCANCODE_P, true));
    m.key(SDL_SCANCODE_P, false);
    CHECK(m.active());
    m.set_game_paused(false);
    CHECK(!m.active());
    CHECK(!m.holds_input());
}

TEST(help_lines) {
    const auto k = PhotoMode::help_lines(false, 55);
    CHECK(k.size() == 4);
    CHECK(k[0] == "PHOTO MODE   FOV 55   DOF OFF");
    CHECK(k[1].find("WASD") != std::string::npos);
    CHECK(k[3].find("DEPTH OF FIELD F") != std::string::npos);
    CHECK(k[3].find("FOCUS NEAR/FAR N/M") != std::string::npos);
    const auto p = PhotoMode::help_lines(true, 40);
    CHECK(p[1].find("L STICK") != std::string::npos);
    CHECK(p[2].find("EXIT B/CIRCLE") != std::string::npos);
    CHECK(p[3].find("DEPTH OF FIELD BACK") != std::string::npos);
    PhotoDof d;
    d.on = true;
    CHECK(PhotoMode::help_lines(false, 60, d, 1)[0] == "PHOTO MODE   FOV 60   DOF 4   FOCUS CAR");
    CHECK(PhotoMode::help_lines(false, 60, d, 2)[0] == "PHOTO MODE   FOV 60   DOF 4   FOCUS CENTRE");
    d.stops = 0.5f;
    CHECK(d.status(2) == "DOF 4   FOCUS CENTRE X1.41");
    CHECK(d.status(0) == "DOF 4   FOCUS CAR X1.41");   // not drawn yet: the target
    d.target = 1;
    CHECK(d.status(0) == "DOF 4   FOCUS CENTRE X1.41");
}

// photo2: the depth of field's parameters: off by default (nothing drawn), the blur levels, the autofocus on the car's
// middle on screen (x 2^stops; without the frame's projection the car's distance along the view), the screen centre's
// when the car is not in front of the camera or off screen, or when it is asked for
TEST(depth_of_field_parameters) {
    PhotoDof d;
    const float pos[3] = {0, 2, 0}, at[3] = {0, 0, 1};
    const Camera cam = game_camera(pos, at);
    const float car[3] = {3, 0, 20};   // 20 m along the view (3 m to the side does not count)
    // the frame's projection (column-major): clip x = x, y = y - 2, w = z (the camera at (0, 2, 0) looking along +z)
    float vp[16] = {};
    vp[0] = 1;
    vp[5] = 1;
    vp[13] = -2;
    vp[11] = 1;
    int m = -1;
    auto off = d.params(cam, car, vp, &m);
    CHECK(!off.on);
    CHECK(off.radius == 0.0f);
    CHECK(m == 0);
    d.toggle();
    auto p = d.params(cam, car, vp, &m);
    CHECK(p.on);
    CHECK_NEAR(p.radius, 0.012, 1e-7);   // level 4
    CHECK(p.focus == 0.0f);   // the autofocus ...
    CHECK_NEAR(p.point[0], 0.5 + 0.5 * 3.0 / 20.0, 1e-6);   // ... at the car as the frame shows it
    CHECK_NEAR(p.point[1], 0.5 + 0.5 * 2.0 / 20.0, 1e-6);
    CHECK_NEAR(p.focus_scale, 1.0, 0.0);
    CHECK(m == 1);
    const float aside[3] = {30, 0, 20};   // in front, off screen: the centre
    p = d.params(cam, aside, vp, &m);
    CHECK(m == 2);
    CHECK(p.point[0] == 0.5f && p.point[1] == 0.5f);
    p = d.params(cam, car, nullptr, &m);   // no projection: the car's distance
    CHECK_NEAR(p.focus, 20.0, 1e-5);
    CHECK(m == 1);
    // the levels: clamped by V / B, cycled by the pad's Start; larger levels blur more
    float last = 0;
    for (int l = 1; l <= PhotoDof::kLevels; ++l) {
        d.level = l;
        CHECK(d.radius() > last);
        last = d.radius();
    }
    d.blur(+1);
    CHECK(d.level == PhotoDof::kLevels);
    d.blur(0);
    CHECK(d.level == 1);
    d.blur(-1);
    CHECK(d.level == 1);
    d.level = 4;
    // the focus offset: one stop doubles the distance; clamped at 4 stops
    d.focus(1.0f);
    CHECK_NEAR(d.params(cam, car).focus, 40.0, 1e-4);
    d.focus(-10.0f);
    CHECK_NEAR(d.stops, -4.0, 0.0);
    CHECK_NEAR(d.params(cam, car).focus, 20.0 / 16.0, 1e-5);
    // no car, or the car behind the camera: the autofocus, scaled
    p = d.params(cam, nullptr, vp, &m);
    CHECK(p.on);
    CHECK(p.focus == 0.0f);
    CHECK_NEAR(p.focus_scale, 1.0 / 16.0, 1e-7);
    CHECK(m == 2);
    const float behind[3] = {0, 0, -5};
    p = d.params(cam, behind, vp, &m);
    CHECK(p.focus == 0.0f && p.point[0] == 0.5f && m == 2);
    CHECK(d.params(cam, behind).focus == 0.0f);
    // G: the centre (the offset starts again)
    d.next_target();
    CHECK(d.target == 1);
    CHECK(d.stops == 0.0f);
    p = d.params(cam, car, vp, &m);
    CHECK(p.focus == 0.0f);
    CHECK(p.point[0] == 0.5f && p.point[1] == 0.5f);
    CHECK(m == 2);
    CHECK_NEAR(p.focus_scale, 1.0, 0.0);
    d.next_target();
    CHECK(d.target == 0);
}

// photo2: the keys (F, V / B, N / M held, G) and the pad (Back, Start, D-pad left / right, L3 alone) while photo mode is on;
// the frame's parameters only then
TEST(depth_of_field_controls) {
    PhotoMode& m = PhotoMode::instance();
    const float pos[3] = {0, 2, 0}, at[3] = {0, 0, 1};
    const Camera g = game_camera(pos, at);
    const float car[3] = {0, 0, 10};
    b3gfx::FrameParams::Dof out;
    m.set_game_paused(true);
    m.set_scene(true);
    CHECK(!m.key(SDL_SCANCODE_F, true));   // off: F is the game's
    m.key(SDL_SCANCODE_F, false);
    CHECK(!m.dof_state().on);
    CHECK(m.key(SDL_SCANCODE_P, true));
    m.key(SDL_SCANCODE_P, false);
    m.advance(1.0f / 60.0f, g);
    m.dof(g, car, nullptr, out);
    CHECK(!out.on);   // off by default
    CHECK(m.key(SDL_SCANCODE_F, true));
    m.key(SDL_SCANCODE_F, false);
    CHECK(m.key(SDL_SCANCODE_B, true));
    m.key(SDL_SCANCODE_B, false);
    m.dof(g, car, nullptr, out);
    CHECK(out.on);
    CHECK(m.dof_state().level == 5);
    CHECK_NEAR(out.focus, 10.0, 1e-5);
    CHECK(m.key(SDL_SCANCODE_M, true));   // farther, held a third of a second: half a stop (1.5 a second)
    for (int i = 0; i < 20; ++i) m.advance(1.0f / 60.0f, g);
    m.key(SDL_SCANCODE_M, false);
    m.dof(g, car, nullptr, out);
    CHECK_NEAR(out.focus, 10.0 * std::sqrt(2.0), 1e-3);
    CHECK(m.key(SDL_SCANCODE_G, true));   // the centre
    m.key(SDL_SCANCODE_G, false);
    m.dof(g, car, nullptr, out);
    CHECK(out.focus == 0.0f);
    // the pad: Back off, L3 alone the car again; L3 + R3 leaves without changing the target
    PhotoPad p;
    m.pad(p);
    p.back = true;
    m.pad(p);
    p = PhotoPad{};
    m.pad(p);
    CHECK(!m.dof_state().on);
    p.back = true;
    m.pad(p);
    p = PhotoPad{};
    m.pad(p);
    p.start = true;   // the next level, cycling: 5 -> 6
    m.pad(p);
    p = PhotoPad{};
    m.pad(p);
    CHECK(m.dof_state().on);
    CHECK(m.dof_state().level == 6);
    p.target = true;
    m.pad(p);
    p = PhotoPad{};
    m.pad(p);
    CHECK(m.dof_state().target == 0);
    p.left = true;   // nearer, held half a second (1.5 stops a second: 0.75)
    m.pad(p);
    for (int i = 0; i < 30; ++i) m.advance(1.0f / 60.0f, g);
    p = PhotoPad{};
    m.pad(p);
    CHECK_NEAR(m.dof_state().stops, -0.75, 1e-4);
    p.target = true;
    m.pad(p);
    p.toggle = true;   // L3 + R3: photo mode off, the target unchanged
    m.pad(p);
    CHECK(!m.active());
    p = PhotoPad{};
    m.pad(p);
    CHECK(m.dof_state().target == 0);
    out = b3gfx::FrameParams::Dof{};
    m.dof(g, car, nullptr, out);
    CHECK(!out.on);   // photo mode closed: nothing
    // the setting stays for the next entry; leave it off for the other tests
    CHECK(m.key(SDL_SCANCODE_P, true));
    m.key(SDL_SCANCODE_P, false);
    CHECK(m.dof_state().on);
    m.key(SDL_SCANCODE_F, true);
    m.key(SDL_SCANCODE_F, false);
    CHECK(!m.dof_state().on);
    m.key(SDL_SCANCODE_ESCAPE, true);
    m.key(SDL_SCANCODE_ESCAPE, false);
    m.set_game_paused(false);
}

// Game time (offscreen live mode): the keys stamped with the game's polls on the game thread, the camera replayed a
// poll at a time on the render thread; a shot of poll N sees the camera at N, however the replay is cut up
TEST(game_time_follows_the_polls) {
    PhotoMode& m = PhotoMode::instance();
    const float pos[3] = {0, 5, 0}, at[3] = {0, 0, 1};
    const Camera g = game_camera(pos, at);
    int asked = 0;
    auto game_cam = [&](std::uint32_t, Camera& c) { ++asked; c = g; return true; };
    m.enable_game_time();
    m.set_game_paused(true);
    m.set_scene(true);
    // one session: on from poll 101, W held for polls 131-190 (60 polls: 10 m), C (roll) for 201-230, off from 301
    auto session = [&](std::uint32_t base) {
        m.set_game_poll(base + 100);
        CHECK(m.key(SDL_SCANCODE_P, true));
        m.key(SDL_SCANCODE_P, false);
        m.set_game_poll(base + 130);
        m.key(SDL_SCANCODE_W, true);
        m.set_game_poll(base + 190);
        m.key(SDL_SCANCODE_W, false);
        m.set_game_poll(base + 200);
        m.key(SDL_SCANCODE_C, true);
        m.set_game_poll(base + 230);
        m.key(SDL_SCANCODE_C, false);
        m.set_game_poll(base + 300);
        CHECK(m.key(SDL_SCANCODE_ESCAPE, true));
        m.key(SDL_SCANCODE_ESCAPE, false);
        CHECK(!m.active());   // the game's side: off at once
    };
    session(0);
    Camera c, end1;
    m.advance_to(100, game_cam);
    CHECK(!m.camera_at(100, g, c));   // before the entry: the game's picture
    m.advance_to(101, game_cam);
    CHECK(asked == 1);
    CHECK(m.camera_at(101, g, c));
    CHECK(!m.camera_at(100, g, c));   // only at the poll the replay has reached
    CHECK_NEAR(c.m.r[3][2], 0.0, 1e-6);
    for (std::uint32_t p = 102; p <= 160; ++p) m.advance_to(p, game_cam);   // a poll at a time
    CHECK(m.camera_at(160, g, c));
    CHECK_NEAR(c.m.r[3][2], 29.0 * 10.0 / 60.0, 1e-3);   // W from 131: 29 polls by 160
    m.advance_to(250, game_cam);
    CHECK(m.camera_at(250, g, end1));
    CHECK_NEAR(end1.m.r[3][2], 10.0, 1e-3);
    m.advance_to(300, game_cam);
    CHECK(m.camera_at(300, g, c));
    m.advance_to(301, game_cam);
    CHECK(!m.camera_at(301, g, c));   // off from 301
    // the same session later, replayed in one go: the same camera, to the bit
    session(1000);
    Camera end2;
    m.advance_to(1250, game_cam);
    CHECK(m.camera_at(1250, g, end2));
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 3; ++k) CHECK(end1.m.r[r][k] == end2.m.r[r][k]);
    CHECK(end1.half_h == end2.half_h);
    m.advance_to(2000, game_cam);
    CHECK(!m.camera_at(2000, g, c));
    // photo2: the depth of field's keys at their polls: F at 2101, M held 2131-2150 (20 polls at 1.5 stops a second:
    // half a stop), off again at 2251
    b3gfx::FrameParams::Dof d;
    const float car[3] = {0, 5, 10};
    m.set_game_poll(2100);
    CHECK(m.key(SDL_SCANCODE_P, true));
    m.key(SDL_SCANCODE_P, false);
    m.key(SDL_SCANCODE_F, true);
    m.key(SDL_SCANCODE_F, false);
    m.set_game_poll(2130);
    m.key(SDL_SCANCODE_M, true);
    m.set_game_poll(2150);
    m.key(SDL_SCANCODE_M, false);
    m.set_game_poll(2250);
    m.key(SDL_SCANCODE_F, true);
    m.key(SDL_SCANCODE_F, false);
    m.set_game_poll(2300);
    m.key(SDL_SCANCODE_ESCAPE, true);
    m.key(SDL_SCANCODE_ESCAPE, false);
    m.advance_to(2101, game_cam);
    CHECK(m.dof_at(2101, g, car, nullptr, d));
    CHECK(d.on);
    CHECK_NEAR(d.focus, 10.0, 1e-5);
    CHECK(!m.dof_at(2100, g, car, nullptr, d));   // only at the poll the replay has reached
    m.advance_to(2200, game_cam);
    CHECK(m.dof_at(2200, g, car, nullptr, d));
    CHECK_NEAR(d.focus, 10.0 * std::sqrt(2.0), 1e-3);
    m.advance_to(2251, game_cam);
    d = b3gfx::FrameParams::Dof{};
    CHECK(m.dof_at(2251, g, car, nullptr, d));
    CHECK(!d.on);
    m.advance_to(2400, game_cam);
    CHECK(!m.dof_at(2400, g, car, nullptr, d));
    m.set_game_paused(false);
}

// photohud1: the photo UI's plates stay on screen, inside the margins and apart, at every aspect, HUD SIZE and device
TEST(photo_ui_layout_fits_every_aspect) {
    using b3replay::PhotoHud;
    using b3replay::PhotoUi;
    using b3replay::PromptDevice;
    const int sizes[][2] = {{800, 600}, {960, 720}, {1280, 1024}, {1280, 720}, {1920, 1080}, {2560, 1080}, {3440, 1440}, {3840, 1080}, {5120, 1440}, {1024, 576}};
    const PromptDevice devs[] = {PromptDevice::kKeyboard, PromptDevice::kPlayStation, PromptDevice::kXbox, PromptDevice::kNintendo,
                                 PromptDevice::kGeneric, PromptDevice::kPlayStationModern};
    const b3replay::PhotoKeys keys;
    int checked = 0;
    for (const auto& sz : sizes)
        for (int hud = 0; hud < 4; ++hud)
            for (PromptDevice d : devs) {
                const int w = sz[0], h = sz[1];
                const PhotoHud::Layout l = PhotoHud::layout(w, h, hud, d, keys, PhotoUi::kFull);
                const float m = l.margin;
                CHECK(!l.readouts.empty());
                CHECK(!l.strip.empty());
                CHECK(l.readouts.x >= m - 0.5f && l.readouts.y >= m - 0.5f);
                CHECK(l.readouts.x + l.readouts.w <= float(w) - m + 0.5f);
                CHECK(l.readouts.y + l.readouts.h <= float(h) - m + 0.5f);
                CHECK(l.strip.x >= m - 0.5f && l.strip.x + l.strip.w <= float(w) - m + 0.5f);
                CHECK(l.strip.y >= m - 0.5f && l.strip.y + l.strip.h <= float(h) - m + 0.5f);
                CHECK(l.strip.y >= l.readouts.y + l.readouts.h);   // apart: the readouts above the strip
                CHECK(l.groups >= 2 && l.rows >= 1);
                // at the screen's edges: the readouts' right edge and the strip's bottom edge sit on the margin
                CHECK_NEAR(l.readouts.x + l.readouts.w, float(w) - m, 0.5);
                CHECK_NEAR(l.strip.y + l.strip.h, float(h) - m, 0.5);
                CHECK_NEAR(l.strip.x + 0.5f * l.strip.w, 0.5f * float(w), 1.0);
                // the pictures are the plates' sizes, and have ink
                int pw = 0, ph = 0;
                std::vector<std::uint8_t> px;
                PhotoHud::picture_strip(l, d, keys, pw, ph, px);
                CHECK(pw == int(l.strip.w) && ph == int(l.strip.h) && px.size() == std::size_t(pw) * std::size_t(ph) * 4);
                std::size_t ink = 0;
                for (std::size_t i = 3; i < px.size(); i += 4) ink += px[i] > 128;
                CHECK(ink > px.size() / 4 / 4);
                PhotoHud::picture_readouts(l, b3replay::PhotoHudData{}, d, keys, pw, ph, px);
                CHECK(pw == int(l.readouts.w) && ph == int(l.readouts.h));
                ++checked;
            }
    CHECK(checked == 10 * 4 * 6);
}

TEST(photo_ui_states) {
    using b3replay::PhotoHud;
    using b3replay::PhotoUi;
    const b3replay::PhotoKeys keys;
    const auto full = PhotoHud::layout(1920, 1080, 0, b3replay::PromptDevice::kKeyboard, keys, PhotoUi::kFull);
    const auto ro = PhotoHud::layout(1920, 1080, 0, b3replay::PromptDevice::kKeyboard, keys, PhotoUi::kReadouts);
    const auto none = PhotoHud::layout(1920, 1080, 0, b3replay::PromptDevice::kKeyboard, keys, PhotoUi::kHidden);
    CHECK(!full.strip.empty() && !full.readouts.empty());
    CHECK(ro.strip.empty() && !ro.readouts.empty());
    CHECK(ro.readouts == full.readouts);   // the readouts do not move when the strip goes
    CHECK(none.strip.empty() && none.readouts.empty());
    CHECK(b3replay::photo_ui_next(PhotoUi::kFull) == PhotoUi::kReadouts);
    CHECK(b3replay::photo_ui_next(PhotoUi::kReadouts) == PhotoUi::kHidden);
    CHECK(b3replay::photo_ui_next(PhotoUi::kHidden) == PhotoUi::kFull);
    // HUD SIZE grows the plates, but not off the window (4:3 at 150 %: groups dropped or the plates shrunk)
    const auto big = PhotoHud::layout(1920, 1080, 3, b3replay::PromptDevice::kKeyboard, keys, PhotoUi::kFull);
    CHECK(big.readouts.w > full.readouts.w);
    // H cycles the window's UI; the keys it works with are the table's
    PhotoMode& m = PhotoMode::instance();
    m.set_game_paused(true);
    m.set_scene(true);
    CHECK(m.key(SDL_SCANCODE_P, true));
    m.key(SDL_SCANCODE_P, false);
    CHECK(m.ui() == PhotoUi::kFull);
    m.key(SDL_SCANCODE_H, true);
    m.key(SDL_SCANCODE_H, false);
    CHECK(m.ui() == PhotoUi::kReadouts);
    m.key(SDL_SCANCODE_H, true);
    m.key(SDL_SCANCODE_H, false);
    CHECK(m.ui() == PhotoUi::kHidden);
    PhotoPad p;
    m.pad(p);
    p.north = true;   // Y / Triangle: the same
    m.pad(p);
    CHECK(m.ui() == PhotoUi::kFull);
    CHECK(m.active());   // hiding the UI leaves photo mode on
    CHECK(m.key(SDL_SCANCODE_ESCAPE, true));
    m.key(SDL_SCANCODE_ESCAPE, false);
    m.set_game_paused(false);
}

// the strip shows the keys of the table, never fixed names
TEST(photo_ui_shows_the_keys_in_use) {
    using b3replay::PhotoHud;
    b3replay::PhotoKeys k;
    auto has = [](const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); };
    auto def = PhotoHud::strip_labels(b3replay::PromptDevice::kKeyboard, k);
    CHECK(has(def, "W") && has(def, "H") && has(def, "F12") && has(def, "ESC") && has(def, "SHIFT"));
    k.fwd = SDL_SCANCODE_I;
    k.hide_ui = SDL_SCANCODE_TAB;
    k.dof = SDL_SCANCODE_X;
    auto moved = PhotoHud::strip_labels(b3replay::PromptDevice::kKeyboard, k);
    CHECK(!has(moved, "W") && has(moved, "I") && has(moved, "TAB") && has(moved, "X") && !has(moved, "H"));
    // the pad's strip shows the device's own button names, the keyboard's none
    auto xb = PhotoHud::strip_labels(b3replay::PromptDevice::kXbox, k);
    CHECK(has(xb, "LB") && has(xb, "RT") && has(xb, "Y") && has(xb, "B") && !has(xb, "TAB"));
    auto ps = PhotoHud::strip_labels(b3replay::PromptDevice::kPlayStation, k);
    CHECK(has(ps, "L1") && has(ps, "TRIANGLE") && has(ps, "CIRCLE"));
    auto ns = PhotoHud::strip_labels(b3replay::PromptDevice::kNintendo, k);
    CHECK(has(ns, "ZL") || has(ns, "L"));
    // the camera follows the same table: a remapped forward key moves it
    PhotoMode& m = PhotoMode::instance();
    CHECK(m.keys().fwd == SDL_SCANCODE_W);
}

TEST(photo_ui_readout_pictures_change_with_the_values) {
    using b3replay::PhotoHud;
    using b3replay::PhotoHudData;
    const b3replay::PhotoKeys keys;
    const auto l = PhotoHud::layout(1920, 1080, 0, b3replay::PromptDevice::kKeyboard, keys, b3replay::PhotoUi::kFull);
    PhotoHudData a, b = a;
    b.fov_deg = 90;
    CHECK(!a.same_picture(b));
    b = a;
    b.fov_deg = 60.3f;
    CHECK(a.same_picture(b));   // the same whole degree
    int w = 0, h = 0;
    std::vector<std::uint8_t> pa, pb;
    PhotoHud::picture_readouts(l, a, b3replay::PromptDevice::kKeyboard, keys, w, h, pa);
    b = a;
    b.dof_on = true;
    b.roll_deg = 20;
    PhotoHud::picture_readouts(l, b, b3replay::PromptDevice::kKeyboard, keys, w, h, pb);
    CHECK(pa != pb);
}

}  // namespace

TEST_MAIN("photo mode (src/b3replay/photomode.h)")
