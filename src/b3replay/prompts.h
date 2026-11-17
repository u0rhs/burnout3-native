// prompts.h -- button prompts for the device in use (mac5, 2026-11-09; docs/remaster-features.md §3): every button glyph
// the game draws (the front end's prompt bar "Select (X)  (T) Back", its d-pad hint, the dialogs' and the race intro's
// prompts, the shoulder-button pictures) shows the PlayStation, Xbox, Nintendo or generic pad's button, or the keyboard's
// key cap with the key actually bound to it. Drawn only: the game's draws keep their place, size, colour and order;
// only their texture and texture coordinates change (and a symbol's quad grows to its button's box, so a key's name
// has room). The PlayStation look is the game's own (no substitution at all), also the default while no device is
// known.
//
// How a prompt reaches the draw list [C retail data + capture, mac5]: a prompt is a text object (GlobalFont) and a
// button object (CGt2dObject, render callback 0x382950 [C func_00382950], prepared by 0x382C00 with a button index
// 0..10) that draws two sprites from the front end's "Buttons" texture (128 x 64): the button's base (the white ring,
// tinted with the menu's text colour) and its symbol (the coloured Cross / Circle / Square / Triangle) inset in it.
// Their texel rectangles come from the static tables the __sinit 0x4D9B10 fills (base: origin D_004C9680, size
// D_004C9620; symbol: D_004C95C0 / D_004C9530; UVs by func_00382CF0): index 0 Triangle (15,2 13x13), 1 Cross (2,3
// 11x11), 2 Start (symbol 67,1 58x36: the button and its "START" label; base 67,39 58x36: its outline), 3/6/7/10
// Square (16,17 11x11), 4/9 Circle (1,16 13x13), the face buttons' base the ring (1,33 21x21); 5 and 8 draw the 64 x 32
// shoulder pictures (textures D_01EA472C / D_01EA4728: "l2button" / "l1button" [G], looked up case-insensitively: the
// GLOBAL.TXD rasters "L2Button" / "L1Button"; 7,5 50x21, base and symbol the same rectangle). The HUD draws the same
// rasters whole, one image quad each (padr2): "R2Button" beside CRASHBREAKER when it is ready (0x3A3250), "R1Button"
// (Impact Time), "L2Button" (0x435AB0); matched by name, any case. The d-pad hint (CB3DPad2dObject, callbacks 0x31C990 / 0x371BE0) draws four arms from "dpad" (32 x 16): the up / down arm (1,0 14x16,
// its tip at v 0: the centre; flipped for up) and the left / right arm (15,1 16x14, its tip at u 0; mirrored for left),
// each arm's colour saying whether that direction does something on the page.
//
// So the renderer recognises those draws by texture name and texel rectangle (prompt_substitute), and redraws them
// from our own atlas (PromptAtlas: shapes and a stroke font, glyphdraw.h, rasterised at the window's size).
#ifndef B3REPLAY_PROMPTS_H
#define B3REPLAY_PROMPTS_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../native/input_keys.h"
#include "hud.h"

namespace b3replay {

// The device whose buttons the prompts show. kPlayStation: the game's own (PS2 / PS3 pads, and no pad known);
// kPlayStationModern (2026-11-09: PS4 / PS5 pads have no Start / Select): the game's face and shoulder pictures,
// its Start as the OPTIONS button. The game never draws a Select prompt [C: the button object's 11 indices, prompts.h
// above, have no Select piece], so CREATE / SHARE are labels only and one modern device serves both pads.
enum class PromptDevice : std::uint8_t { kKeyboard, kPlayStation, kXbox, kNintendo, kGeneric, kPlayStationModern };
const char* prompt_device_name(PromptDevice d);                  // "keyboard", "PlayStation", "PS5", ...
bool prompt_device_parse(std::string_view s, PromptDevice& d);   // a name above, any case ("ps4" / "ps5": the modern one)

// A pad's device from SDL (no SDL here: its values, which nativepad.cpp static_asserts): the pad's type first (PS3:
// the game's own; PS4 / PS5: the modern PlayStation), else the label of its bottom face button (Cross: PlayStation,
// B: Nintendo, A: Xbox, else generic).
constexpr int kSdlTypePS3 = 4, kSdlTypePS4 = 5, kSdlTypePS5 = 6;           // SDL_GamepadType
constexpr int kSdlLabelA = 1, kSdlLabelB = 2, kSdlLabelCross = 5;          // SDL_GamepadButtonLabel
PromptDevice prompt_device_for_pad(int sdl_gamepad_type, int sdl_south_label);

// PC SETTINGS > INTERFACE > BUTTON PROMPTS (b3settings::Settings::button_prompts): automatic, or one device's always.
// The values are the ini's (the modern PlayStation came last, 6); the page lists it after PlayStation (kPromptModeOrder).
enum class PromptMode : std::uint8_t { kAuto, kPlayStation, kXbox, kNintendo, kGeneric, kKeyboard, kPlayStationModern };
constexpr int kPromptModes = 7;
PromptDevice prompt_resolve(int mode, PromptDevice tracked);   // the device the prompts show

// The DualShock buttons a prompt can name
enum class PadButton : std::uint8_t { kCross, kCircle, kSquare, kTriangle, kL1, kR1, kL2, kR2, kL3, kR3, kStart, kSelect, kCount };
constexpr int kPadButtons = int(PadButton::kCount);
const char* pad_button_name(PadButton b);

// The keys bound to each button (the key caps), from a key map in the menus' or the driving context: per button the
// key a player most likely uses (Enter over Space for Cross in menus; W over Enter for it while driving), 0 = none.
struct PromptKeys {
    std::array<std::uint16_t, kPadButtons> scancode{};
    // remap1 (pad devices): the glyph each button shows when the gamepad bindings moved it, 1 + PadButton (0: its own)
    std::array<std::uint8_t, kPadButtons> pad{};
    bool operator==(const PromptKeys&) const = default;
    PadButton shown(PadButton b) const {
        const std::uint8_t g = std::size_t(b) < pad.size() ? pad[std::size_t(b)] : 0;
        return g ? PadButton(g - 1) : b;
    }
    bool pad_moved(PadButton b) const { return shown(b) != b; }
    bool pad_remapped() const {
        for (std::uint8_t g : pad) if (g) return true;
        return false;
    }
};
PromptKeys prompt_keys(const std::vector<b3native::input::KeyBinding>& map, bool driving);
// remap1: a pad device's PromptKeys from the gamepad bindings' glyph positions (input_bindings.h pad_glyphs: per Ds2
// button, the Ds2 button whose default position its first bound input is at). Identity glyphs give PromptKeys{}; the
// default bindings (padr2: L2 / R2 on the stick clicks) show L2 / R2 at L3 / R3.
PromptKeys prompt_pad_glyphs(const std::array<b3native::input::Ds2, 16>& glyphs);
// Whether the prompts substitute the game's button draws at all: every device but the PlayStation (the game's own),
// and the PlayStation too once its gamepad bindings moved a button (the moved buttons are redrawn)
bool prompts_substituted(PromptDevice d, const PromptKeys& keys);
// A key cap's label for an SDL scancode: "ENTER", "ESC", "A", "F1", "UP" ... ("?" for one without a name)
std::string key_cap_name(std::uint16_t scancode);

// The text on a device's button ("A", "LB", "ZL", "+"; the key's name for the keyboard; the PlayStation's own names:
// "CROSS", "L1", "START")
std::string prompt_label(PromptDevice d, PadButton b, const PromptKeys& keys = {});

// Automatic switching (§3): the last device that produced input is the active one. A key press, a pad button or a pad
// connected switches at once; a stick or trigger only when it stays past half way for kAxisHold seconds and not within
// kAxisSettle seconds of the last switch (analog noise or a resting thumb does not flip the prompts while typing).
// Until any input the device is unknown, and the prompts show the PlayStation's (the game's own).
class PromptDeviceTracker {
public:
    static constexpr double kAxisHold = 0.15, kAxisSettle = 0.5;
    void press(PromptDevice d, double t);                    // a key or pad button went down, or a pad connected
    void axis(PromptDevice d, bool past_half, double t);     // a pad's sticks / triggers this frame (each pad, each frame)
    void pads_gone(bool keyboard_used, double t);            // the last pad was removed
    PromptDevice active() const { return known_ ? active_ : PromptDevice::kPlayStation; }
    bool known() const { return known_; }

private:
    void set(PromptDevice d, double t);
    PromptDevice active_ = PromptDevice::kPlayStation, pending_ = PromptDevice::kPlayStation;
    bool known_ = false, has_pending_ = false;
    double pending_since_ = 0, switched_at_ = -1e9;
};

namespace glyph { struct Canvas; }
// Button `b`'s glyph (its base, then its symbol) painted over `c` with its box's top left at (x0, y0), `h` pixels high;
// returns its width (a face button is square, Start / Select 33 x 21, the shoulders 35 x 15). For the PlayStation it
// is our own drawing of the symbols (the skip hint), not the game's picture.
float paint_prompt_glyph(glyph::Canvas& c, float x0, float y0, float h, PromptDevice d, PadButton b, const PromptKeys& keys = {});

// The picture layers of a button: its base (the ring / key cap / pill, tinted by the game like its ring) and its symbol
enum class GlyphLayer : std::uint8_t { kBase, kSymbol };

// Our glyphs, one cell per (button, layer) plus the d-pad's two arms, rasterised for a window `window_h` pixels high
// (a 21-unit button is 21 / 448 of it). RGBA8, straight alpha; the colours as drawn at the game's full vertex colour.
class PromptAtlas {
public:
    struct Rect { float u0 = 0, v0 = 0, u1 = 0, v1 = 0; };
    void build(PromptDevice d, const PromptKeys& keys, int window_h);
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<std::uint8_t>& rgba() const { return rgba_; }
    PromptDevice device() const { return device_; }
    const PromptKeys& keys() const { return keys_; }
    int built_for() const { return window_h_; }
    Rect cell(PadButton b, GlyphLayer l) const { return cells_[std::size_t(b) * 2 + std::size_t(l)]; }
    // The part of a cell its picture covers (alpha over 1/8; the whole cell when empty): a round Start is narrower
    // than its 33 x 21 box (the title's line lays the words out by it)
    Rect ink(PadButton b, GlyphLayer l) const { return ink_[std::size_t(b) * 2 + std::size_t(l)]; }
    Rect dpad_arm(bool left_right) const { return arms_[left_right ? 1 : 0]; }

private:
    std::vector<std::uint8_t> rgba_;
    int w_ = 0, h_ = 0, window_h_ = -1;
    PromptDevice device_ = PromptDevice::kPlayStation;
    PromptKeys keys_{};
    std::array<Rect, kPadButtons * 2> cells_{}, ink_{};
    std::array<Rect, 2> arms_{};
};

// One HUD draw of a button picture redrawn from `atlas`: `tex_name` / `tex_w` x `tex_h` are the draw's raster (its
// name and size in texels: HudTexture name, w, h), `tri` its triangles. True with `out` set (the same triangles, their
// UVs into the atlas, a symbol's quad grown to its button's box) when every quad of the draw is a known button piece;
// false (draw it as it is) otherwise or for the PlayStation.
bool prompt_substitute(std::string_view tex_name, int tex_w, int tex_h, const std::vector<HudVertex>& tri, const PromptAtlas& atlas,
                       std::vector<HudVertex>& out);
// mac5 (menu mouse): the pad button (libpad2 index, include/b3live/menu_input.h pad::) piece `piece` (quad: 6 vertices) of a
// draw of `tex_name` shows by its texel rectangle: -2 the face buttons' ring (part of a button, named by its symbol), -1
// not a button piece. The game's own draw (its UVs), not a substituted one.
int prompt_piece_button(std::string_view tex_name, int tex_w, int tex_h, const std::vector<HudVertex>& tri, std::size_t piece);
// Whether a raster name is one of the game's button pictures (the draws prompt_substitute looks at)
bool prompt_texture(std::string_view tex_name);

// ---- the title's "Press START button to continue" (2026-11-10) ----
// The title screen's line is GlobalFont text, not a button object, so prompt_substitute never sees it. The renderer
// reads a font draw's quads back as characters (their texel rectangles against the font's glyph records, which the
// capture publishes: hud.h HostFonts), finds that whole line, and draws the device's Start button in place of the
// five quads spelling START (the words after it moved by the glyph's width less the word's, half each side, so the line
// keeps the game's centre). Drawn only; the PlayStation keeps the game's text (its own Start picture is a button with
// "START" written in it, no clearer than the word).

// The character each quad (6 vertices) of a draw of font `f` shows, 0 for a quad that is none of its glyphs. `t` is
// the draw's raster (its decoded size w x h and TEX0's 2^TW x 2^TH: a glyph record's uv is in the latter's units, a
// vertex's in the former's). Empty when the draw is not quads.
std::u16string font_quad_chars(const HostFont& f, const HudTexture& t, const std::vector<HudVertex>& tri);

// One occurrence of a line of text in a draw: its quads (spaces draw none) and the word's within them
struct TextLineMatch { std::size_t line_first = 0, line_quads = 0, word_first = 0, word_quads = 0; };
// Every place `chars` (font_quad_chars) holds the whole line `line` -- its characters in order, left to right on one
// row, the gaps where `line` has a space wider than every gap between letters, and no glyph of the same row joined on
// either end -- with `word` (a whole word of `line`, no spaces) located in it.
std::vector<TextLineMatch> find_text_line(const std::vector<HudVertex>& tri, std::u16string_view chars, std::u16string_view line,
                                          std::u16string_view word);

constexpr std::u16string_view kTitleLine = u"Press START button to continue", kTitleWord = u"START";
// The glyph's height over the word's quads' (their cap height and a little: the button a bit taller than the letters)
constexpr float kTitleGlyphScale = 1.45f;
// The title's line in a font draw (`tri`, `chars`) with START drawn as the atlas's device's Start button: `text` the
// draw's quads without the word's (the rest moved apart / together), `glyph` the button's quads (base, then symbol;
// UVs into `atlas`, the word's colour). False (draw it as it is): no such line, or the PlayStation's atlas.
bool title_start_swap(const std::vector<HudVertex>& tri, std::u16string_view chars, const PromptAtlas& atlas, std::vector<HudVertex>& text,
                      std::vector<HudVertex>& glyph);

// ---- carsel1 (2026-11-13): the car series' shoulder glyphs ----
// With PC SETTINGS > CAR SERIES ON L1 / R1 on, L1 / R1 change the car series on the car select and YOUR GARAGE pages
// (src/native/input_keys.h ShoulderSeries, tools/native/car_select.h). The series' name has an arrow each side, two
// images of the "CarArrow" texture on one row (the carousel's Prepare 0x42D2B0: centred at y 89, the name between them);
// the device's L1 glyph is drawn left of the left arrow and R1 right of the right one (key caps: the keys bound to L1 /
// R1), kSeriesGlyphGap apart, kSeriesGlyphH high (the shoulder cell's 35 x 15), centred on the arrows' row, in white at
// the arrows' alpha (they fade in and out with the page). Drawn only.
constexpr std::string_view kSeriesArrowTexture = "CarArrow";
constexpr float kSeriesGlyphH = 15.0f, kSeriesGlyphGap = 4.0f;
// `arrows`: the triangles of each draw of the CarArrow texture in a frame (quads of 6 vertices, in draw order). The
// series' pair is the topmost two quads on one row (centres within 2 units, heights within 2) with a gap between them
// wider than both; `out` gets, per input draw, the glyph quads (base, then symbol; UVs into `atlas`) to draw right after
// it: L1's after the left arrow's draw, R1's after the right one's. False (nothing to add): no such pair, or no atlas.
bool series_glyphs(const std::vector<const std::vector<HudVertex>*>& arrows, const PromptAtlas& atlas,
                   std::vector<std::vector<HudVertex>>& out);

}  // namespace b3replay

#endif
