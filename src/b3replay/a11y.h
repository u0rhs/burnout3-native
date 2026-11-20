// a11y.h -- the accessibility rows' drawing side (remaster-features "Accessibility"; PC SETTINGS > GAME, section
// ACCESSIBILITY; docs/pc-settings.md). Pure functions (no GPU, no guest memory), unit-tested in tests/test_a11y.cpp;
// View::hud_list / host_ui_list apply them to the 2D layer only. With the defaults (Full, Off) nothing here runs, so the
// frames are the game's byte for byte.
//
// HUD COLOUR FILTER (Off / Deuteranopia / Protanopia / Tritanopia): a daltonize remap (Fidaner, Lin and Ozguven 2005, the
// form most games use): the colour as a dichromat sees it (Machado, Oliveira and Fernandes 2009, severity 1) is
// subtracted, and the lost difference is moved into the channels that remain visible (red-green losses into green and
// blue; the tritan's blue-yellow loss into red and green). One 3 x 3 matrix on the 8-bit colour values; for a
// draw it is applied to the vertex colours and to a remapped copy of the texture (GS MODULATE multiplies them, so the
// result is exact when either factor is grey: white fonts tinted by the vertex colour, coloured pictures under a grey
// vertex colour -- nearly every 2D draw). Alpha is never touched.
//
// SCREEN FLASHES (Full / Reduced), two parts:
// - a draw of the 2D layer that covers at least half the screen with a white-ish fill (all three channels light: a fade to
//   or a flash of white; never a picture, never a coloured or dark panel, the crash letterbox or the pause shade) has its
//   contribution capped: its alpha (alpha blends) or its colour (additive blends, opaque fills) scaled so that it lifts
//   the picture by at most kFlashCap of white;
// - the TV static (the game's camera-change transition, textures "Camchange" .. "Camchange5", cycled every 2 polls: a
//   15 Hz flicker over most of the screen) is drawn from its first frame only: still noise that fades as the game fades it.
// Measured (B3_FLASH_TRACE, race_check takedown / crash, 2026-11-10): in races the 2D layer draws no full-screen light
// fill; the game's own white flash is the explosion's (CB3ExplosionManager::RenderFullScreenFlash, a 3D-stage sprite),
// drawn by Effects::draw_explosion_flash from explosion_flash below (Reduced caps it at kFlashCap of white). The bloom's
// strength is constant in races.
#ifndef B3REPLAY_A11Y_H
#define B3REPLAY_A11Y_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace b3replay::a11y {

enum class ColourFilter : std::uint8_t { kOff = 0, kDeuteranopia = 1, kProtanopia = 2, kTritanopia = 3 };
ColourFilter colour_filter(int setting);   // the ini value, clamped (0 off)

using Matrix3 = std::array<float, 9>;   // row-major, on 0..1 (or 0..255) RGB
// The daltonize matrix of a filter (identity for kOff), and the dichromat simulation alone (tests: the correction must
// keep red and green apart where the simulation merges them).
Matrix3 daltonize_matrix(ColourFilter f);
Matrix3 simulate_matrix(ColourFilter f);
// One RGBA8 colour (R in the low byte, as b3gfx::Vertex::rgba) / bytes remapped; alpha kept, rounded, clamped.
std::uint32_t remap_rgba(const Matrix3& m, std::uint32_t rgba);
void remap_pixels(const Matrix3& m, std::uint8_t* rgba, std::size_t pixels);

// SCREEN FLASHES: what one 2D draw is, as far as the cap is concerned.
struct FlashDraw {
    float coverage = 0;          // share of the 2D raster its triangles' bounding box covers (clipped to its scissor), 0..1
    float light = 0;             // its vertices' mean brightest channel as drawn (MODULATE: the vertex byte x the texture's
                                 // mean / 128, untextured: the byte / 128; 1 = the 2D's white, 0x80)
    float white = 0;             // the same with the dimmest channel (white-ish: all three light)
    float alpha = 0;             // its largest vertex alpha / 128 (GS: 0x80 = 1)
    std::uint8_t gs_alpha = 0x44;   // GS ALPHA (A, B, C, D in bits 0-7)
    bool blend = true;              // PRIM ABE (off: drawn opaque)
    bool textured = false;          // a picture: never capped
};
constexpr float kFlashCoverage = 0.5f;   // a draw over at least half the screen
constexpr float kFlashWhite = 0.6f;      // ... white-ish: every channel at least this light
constexpr float kFlashCap = 0.25f;       // the most it may lift the picture, of white
// The factor (0..1] that scales the draw's alpha (alpha blends: `scale_alpha`) or its colour (additive blends, opaque
// fills); 1 = untouched (not a flash, or already within the cap).
struct FlashCut { float factor = 1.0f; bool scale_alpha = false; };
FlashCut flash_cut(const FlashDraw& d);
// The crash explosion's full-screen flash [C CB3ExplosionManager::RenderFullScreenFlash 0x399BA0 matched, CB3GraphicsManager
// full-screen colour fill 0x2276C0 draft]: `elapsed` = the world clock minus the crash time (player block +0x2430 -> +0x3534).
// From 0.025 s to 0.145 s after it the game fills the view with the colour (2.0, 1.6, 1.0) x 127.95 (bytes 255, 204, 127)
// at alpha t^2 x 128.9 (t = (0.12 - (elapsed - 0.025)) / 0.12: 1 -> 0), blended Cs As + Cd (ALPHA_1 table entry 1 = 0x48,
// additive, no Z). `reduce` (SCREEN FLASHES Reduced): the alpha scaled by flash_cut's factor so the lift is at most
// kFlashCap of white. `on` false: nothing is drawn.
struct ExplosionFlash {
    bool on = false;
    std::uint8_t r = 0, g = 0, b = 0, a = 0;   // GS bytes (alpha 0x80 = 1)
};
ExplosionFlash explosion_flash(float elapsed, bool reduce);
// The TV static's frames 2..5 ("Camchange2" .. "Camchange5"): Reduced draws "Camchange" in their place.
bool static_frame(std::string_view texture_name);
constexpr const char* kStaticFirstFrame = "Camchange";

// HUD SIZE (100 / 115 / 130 / 150 %): the race HUD drawn larger, each HUD component scaled about its own EHUDAlign
// anchor (CB3HUDComponent +0x18; D_004B7650[align] = its frame's place in the HUD rect, fx / fy 0, 0.5 or 1): a corner
// component grows from its corner toward the screen's centre, an edge-centred one from its edge's middle, a centred one
// about the centre. View::hud_list gathers each component's box (its draws where they land after the wide layout's
// shift), places its pivot (the anchor point of the HUD rect, moved with the component) and asks fit_scale /
// resolve_overlaps for the scale it may take: never off the visible 2D area, never into another component it was apart
// from at 100 %. Level 0 (100 %) runs none of this: the frames are the game's byte for byte.
constexpr int kHudSizeLevels = 4;
float hud_size_scale(int level);   // 1, 1.15, 1.3, 1.5 (out of range: 1)
int hud_size_percent(int level);   // 100, 115, 130, 150

struct Box {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    bool empty() const { return !(x0 <= x1 && y0 <= y1); }
    void add(float x, float y) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    void add(const Box& b) {
        if (!b.empty()) { add(b.x0, b.y0); add(b.x1, b.y1); }
    }
    // the boxes share area (touching edges do not count); `gap`: a margin kept between them
    bool overlaps(const Box& b, float gap = 0.0f) const {
        return !empty() && !b.empty() && x0 < b.x1 + gap && b.x0 < x1 + gap && y0 < b.y1 + gap && b.y0 < y1 + gap;
    }
};
struct Pivot { float x = 0, y = 0; };
// The anchor point of a component: (fx, fy) of `rect` (the HUD rect, 2D units), x moved by `shift` (the wide layout's
// (2 fx - 1) x the extra half width: the pivot stays at the component's side of the window).
Pivot anchor_pivot(float fx, float fy, const Box& rect, float shift);
// A coordinate / box scaled by s about p.
inline float scale_about(float v, float p, float s) { return p + s * (v - p); }
Box scaled(const Box& b, Pivot p, float s);
// The largest scale in [1, s] that keeps `b` (its box at 100 %) scaled about `p` inside `bounds` (the visible 2D area).
// A side the box already has past the bounds at 100 % (a message sliding in from off screen) does not limit it: that
// part was off screen at 100 % already, and capping by it would make the size jump when the slide ends.
float fit_scale(const Box& b, Pivot p, float s, const Box& bounds);
// One component for resolve_overlaps.
struct Part {
    Box box;                 // at 100 %
    Pivot pivot;
    float scale = 1;         // in: the scale it would take (after fit_scale); out: the scale it may take
    bool message = false;    // a centred component (the TAKEDOWN / AFTERTOUCH style messages): it gives way
};
// Lowers scales until no two parts that were apart at 100 % (by more than `gap`) overlap at their scales: a message
// against a steady (corner / edge) part gives way alone, so the steady HUD keeps its size; two steady parts (or two
// messages) get one common cap, the largest at which they stay apart. Never below 1 (at 1 they were apart).
void resolve_overlaps(std::vector<Part>& parts, float gap = 0.0f);

}  // namespace b3replay::a11y

#endif
