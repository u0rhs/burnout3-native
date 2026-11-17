// photohud.h -- photo mode's on-screen UI (photohud1, 2026-11-15; photomode.h has the mode itself): a mode plate with
// live readouts at the top right (field of view, roll, depth of field and its focus, as segmented gauges drawn like
// the HUD's boost meter: slanted cells, amber on dark) and a controls strip at the bottom centre (the keys / pad buttons
// of each action, grouped, as key caps or as the button prompts' glyphs of the device in use: prompts.h). Both are
// cut-corner dark plates with an amber edge, the look of the notices and the skip hint, drawn from the stroke font and
// shapes of glyphdraw.h (no game art). Drawn only: one point-sampled textured quad per plate in the 2D stage, after a
// screenshot is taken, so a screenshot never holds it; nothing here reaches the camera, the replay or the game.
//
// Layout (PhotoHud::layout, pure, tests: test_photomode): sizes are those of a 720-line window scaled by the window's
// height and HUD SIZE (100 / 115 / 130 / 150 %); both plates keep a margin of 4.5 % of the height from the window's
// edges (the same on every side, so a 21:9 or 32:9 window keeps them at its edges and not at a 16:9 box), the readouts
// anchored to the top right, the strip to the bottom centre and never wider than the window less the margins. A strip
// that does not fit (a 4:3 window at HUD SIZE 150 %) loses its least important groups, then shrinks, so the two plates
// never overlap and never leave the window.
//
// H (pad: Y / Triangle) cycles the UI: everything, the readouts alone, nothing; the plates slide and fade (0.2 s).
#ifndef B3REPLAY_PHOTOHUD_H
#define B3REPLAY_PHOTOHUD_H

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "b3gfx/b3gfx.h"
#include "prompts.h"

namespace b3replay {

// The photo mode keys in one table, read by the camera's step() and by the controls strip alike, so what the strip
// shows is what the keys do. The defaults are the keys documented in docs/PLAYING.md; `*_alt` is a second key doing the
// same (0: none). A remapping of them goes through this table.
// rebind2: a gamepad control of the strip: a button position (the glyph of the device in use), or a text chip for an
// input that has no glyph (a trigger axis, a paddle, a chord)
struct PhotoPadKey {
    PadButton b = PadButton::kCross;
    std::string text;   // not empty: a chip with this text instead of the glyph
    bool operator==(const PhotoPadKey&) const = default;
};

struct PhotoKeys {
    std::uint16_t fwd = SDL_SCANCODE_W, back = SDL_SCANCODE_S, left = SDL_SCANCODE_A, right = SDL_SCANCODE_D;
    std::uint16_t up = SDL_SCANCODE_E, down = SDL_SCANCODE_Q;
    std::uint16_t fast = SDL_SCANCODE_LSHIFT, fast_alt = SDL_SCANCODE_RSHIFT;
    std::uint16_t slow = SDL_SCANCODE_LALT, slow_alt = SDL_SCANCODE_RALT;
    std::uint16_t look_left = SDL_SCANCODE_LEFT, look_right = SDL_SCANCODE_RIGHT, look_up = SDL_SCANCODE_UP, look_down = SDL_SCANCODE_DOWN;
    std::uint16_t roll_cw = SDL_SCANCODE_C, roll_ccw = SDL_SCANCODE_Z;
    std::uint16_t zoom_in = SDL_SCANCODE_EQUALS, zoom_in_alt = SDL_SCANCODE_KP_PLUS;
    std::uint16_t zoom_out = SDL_SCANCODE_MINUS, zoom_out_alt = SDL_SCANCODE_KP_MINUS;
    std::uint16_t focus_far = SDL_SCANCODE_M, focus_near = SDL_SCANCODE_N;
    std::uint16_t dof = SDL_SCANCODE_F, blur_less = SDL_SCANCODE_V, blur_more = SDL_SCANCODE_B, target = SDL_SCANCODE_G;
    std::uint16_t reset = SDL_SCANCODE_R, hide_ui = SDL_SCANCODE_H;
    std::uint16_t exit_key = SDL_SCANCODE_P, exit_alt = SDL_SCANCODE_ESCAPE;   // (P also enters, from the pause menu)
    std::uint16_t shot = SDL_SCANCODE_F12;                                     // the window's screenshot key (not ours to take)
    // the gamepad controls of the strip (rebind2: from the bindings, photomode.h photo_keys_from); the defaults are the layout in
    // the text above
    PhotoPadKey pad_up{PadButton::kR2}, pad_down{PadButton::kL2}, pad_fast{PadButton::kCross}, pad_roll_ccw{PadButton::kL1},
        pad_roll_cw{PadButton::kR1}, pad_dof{PadButton::kSelect}, pad_blur{PadButton::kStart}, pad_target{PadButton::kL3},
        pad_reset{PadButton::kSquare}, pad_hide{PadButton::kTriangle}, pad_exit{PadButton::kCircle};
    bool operator==(const PhotoKeys&) const = default;
};

// What H / Y cycles through: everything, the readouts alone, nothing (clean views)
enum class PhotoUi : std::uint8_t { kFull, kReadouts, kHidden };
PhotoUi photo_ui_next(PhotoUi u);

// The values the readouts show
struct PhotoHudData {
    float fov_deg = 60, roll_deg = 0;
    bool dof_on = false;
    int dof_level = 4, dof_levels = 6;
    bool focus_car = true;      // the focus on the player's car (else the screen centre)
    float focus_stops = 0;      // the focus offset (-4..4 stops)
    // the pictures only change with these (the whole degree, the focus to 0.05 stop)
    bool same_picture(const PhotoHudData& o) const;
};

class PhotoHud {
public:
    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;
        bool empty() const { return w <= 0 || h <= 0; }
        bool operator==(const Rect&) const = default;
    };
    struct Layout {
        Rect readouts, strip;        // window pixels; empty when not shown
        float scale = 1;             // the size unit: 1 = a 720-line window at HUD SIZE 100 %
        float margin = 0;
        int groups = 0, rows = 0;    // the strip's control groups kept, and its rows
        bool operator==(const Layout&) const = default;
    };
    // The plates' places in a `w` x `h` window at HUD SIZE level `hud_size` (a11y.h) for `device`'s controls and `ui`.
    static Layout layout(int w, int h, int hud_size, PromptDevice device, const PhotoKeys& keys, PhotoUi ui);
    // The plates' pictures (straight RGBA rows) for a layout: `w` x `h` is the layout rectangle's size
    static void picture_readouts(const Layout& l, const PhotoHudData& d, PromptDevice device, const PhotoKeys& keys, int& w, int& h,
                                 std::vector<std::uint8_t>& rgba);
    static void picture_strip(const Layout& l, PromptDevice device, const PhotoKeys& keys, int& w, int& h, std::vector<std::uint8_t>& rgba);
    // The key (scancode) of each control of the strip, in the order the strip lists them: for tests (the strip shows the
    // keys of `keys`, never fixed names)
    static std::vector<std::string> strip_labels(PromptDevice device, const PhotoKeys& keys);

    // Render thread: advance by `dt` seconds and append the plates' draw items (window pixels) to `out`
    void draw_list(b3gfx::Device& dev, int w, int h, float dt, PhotoUi ui, const PhotoHudData& data, PromptDevice device,
                   const PhotoKeys& keys, int hud_size, std::vector<b3gfx::DrawItem>& out);
    // photo mode entered: the plates slide in again
    void restart() { readouts_.shown = strip_.shown = 0; }
    // the plates' opacity (0..1) now: for tests
    float readouts_opacity() const { return readouts_.shown; }
    float strip_opacity() const { return strip_.shown; }

private:
    struct Plate {
        b3gfx::TextureId tex = b3gfx::kNone;
        b3gfx::BufferId vb = b3gfx::kNone, ib = b3gfx::kNone;
        int w = 0, h = 0;
        float shown = 0;   // 0..1, eased toward its target
        bool valid = false;
    };
    void quad(b3gfx::Device& dev, Plate& p, int w, int h, float x, float y, float op, std::vector<b3gfx::DrawItem>& out);
    Plate readouts_, strip_;
    b3gfx::ResourceEpoch epoch_;   // a track load clears the ids in the plates (Device::resource_epoch)
    Layout layout_;
    int layout_w_ = -1, layout_h_ = -1, layout_size_ = -1;
    PromptDevice layout_device_ = PromptDevice::kKeyboard;
    PhotoKeys layout_keys_;
    PhotoHudData built_data_;
    Layout built_layout_;
    PromptDevice built_device_ = PromptDevice::kKeyboard;
    PhotoKeys built_keys_;
    bool built_readouts_ = false, built_strip_ = false;
    Layout built_strip_layout_;
    PromptDevice built_strip_device_ = PromptDevice::kKeyboard;
    PhotoKeys built_strip_keys_;
};

}  // namespace b3replay

#endif
