// splitlayout1: the 2-player split screen's layout choice (PC SETTINGS > CAMERA > SPLIT SCREEN; docs/pc-settings.md
// §7.11, docs/native-renderer.md §29.7). Pure maths, no renderer state: the views' rectangles, each view's projection
// (its view window) and the placement of each player's 2D (HUD) inside its own view. View::split_frame uses it.
//
// What the game does [C decomp, CB3GraphicsManagerBase::SetViewportsSplitScreen 0x19E760 matched;
// SetViewportsSinglePlayer 0x19E8C0; CGtViewport::SetFOV 0x211CC0 / Update 0x211ED0]: the graphics manager (0x665EC0)
// has two layouts, chosen by its byte +0xF, and a 16:9 flag +0xD (set at start-up from the console's TV setting:
// CB3GraphicsManager 0x228160, +0xD = sceScfGetAspect() == 2 through 0x436570):
//   +0xF == 0 (top / bottom): viewport 1 (+0x90) = rows [0, h/2), viewport 2 (+0x130) = [h/2, h); aspect 8/3 (16:9 flag:
//                             32/9), view distance scale 1.35 (1.62)
//   +0xF != 0 (side by side): viewport 1 = columns [0, w/2), viewport 2 = [w/2, w); aspect 2/3 (8/9), scale 0.55 (0.75)
//   one player:               the whole raster, aspect 4/3 (16/9), scale 1.0 (1.2)
// and a viewport's view window is half_w = scale x tan(fov / 2), half_h = half_w / aspect (+0x48 / +0x4C). The aspect is
// the half's shape on a 4:3 (16:9) television, so the PS2 picture is not squashed. +0xF is written once, 0, by
// CB3GraphicsManagerBase::Construct 0x19EE40 [C asm: the only store to +0xF]: the retail game always plays top / bottom.
// The side-by-side branch stays in the code with its HUD (CB3HUDManager 0x1A0660's two HUD rects, the takedown banners'
// narrower widths 0x3A25F0 / 0x3D56D0), unused.
#ifndef B3REPLAY_SPLITLAYOUT_H
#define B3REPLAY_SPLITLAYOUT_H

namespace b3replay {

// The SPLIT SCREEN setting (b3settings::Settings::split_layout): 0 the game's own top / bottom (the renderer's path
// before splitlayout1, byte for byte), 1 side by side.
enum class SplitLayout : int { kGame = 0, kSideBySide = 1 };
SplitLayout split_layout_of(int setting);

// The game's (aspect, scale) of a viewport of `layout` (SetViewportsSplitScreen's constants), for its 16:9 flag.
struct GameViewport { float aspect = 4.0f / 3.0f, scale = 1.0f; };
GameViewport game_split_viewport(bool side_by_side, bool wide_flag);
// The 16:9 flag a split camera was set up with, from its view window's shape (8/3: 4:3, 32/9: 16:9).
bool game_split_wide_flag(float half_w, float half_h);
// tan(fov / 2) of the game camera behind a top / bottom split view window (half_w = scale x t).
float game_split_tangent(float half_w, float half_h);

// A view of the frame: its rectangle in output fractions (x0, y0, x1, y1; y down) and its shape as displayed.
struct SplitView {
    float rect[4] = {0, 0, 1, 1};
    float aspect = 1.0f;   // width / height as displayed
};
// The two views of `layout` on an output of displayed aspect `out_aspect` (width / height): player 1 first.
void split_views(SplitLayout layout, float out_aspect, SplitView views[2]);

// A view's vertical view window (half_h at distance 1) for a captured top / bottom split camera (half_w, half_h): the
// game's own value for kGame, the game's side-by-side value (its constants for the same 16:9 flag and field of view)
// for kSideBySide. Hor+: the horizontal follows the view's shape (half_w = half_h x the view's aspect).
float split_view_half_h(SplitLayout layout, float half_w, float half_h);

// The 2D of a split frame is one 640 x 448 space (HudFrame::split): player 1's HUD in rows [0, 224), player 2's in
// [224, 448) (the game's top / bottom HUD rects [C CB3HUDManager 0x1A0660]). A draw is placed into its view by an
// anchor: its point (ax, ay) of the source band goes to the same point of the view, and the draw keeps its shape at
// `scale` output pixels per 2D unit around it; a band that spans the source's full width stretches to the view's.
struct HudPlace {
    float scale = 1.0f;         // output pixels per 2D unit
    float src[4] = {0, 0, 640, 224};   // source band in 2D units (x0, y0, x1, y1)
    float dst[4] = {0, 0, 1, 1};       // destination in output pixels (x0, y0, x1, y1)
};
// The placement of view k's band (k = 0 top rows, 1 bottom rows) in `view` on an out_w x out_h output: the HUD as large
// as the game's own (out_h / 448 per unit) unless the band's width would not fit the view, then as wide as the view.
HudPlace split_hud_place(int k, const SplitView& view, int out_w, int out_h, int width = 640, int height = 448);
// The whole 2D space on the whole output at the game's size (draws that belong to neither view: a pause menu over both).
HudPlace frame_hud_place(int out_w, int out_h, int width = 640, int height = 448);
// The affine map of one draw (x' = a x + b, y' = c y + d, output pixels): anchored at (ax, ay) in 0..1, or stretched
// horizontally over the view (`stretch_x`: the source's x0..x1 onto the view's).
struct HudAffine { float a = 1, b = 0, c = 1, d = 0; };
HudAffine split_hud_affine(const HudPlace& p, float ax, float ay, bool stretch_x);
// The anchor of a draw without a HUD component: its box (2D units) against the band, thirds: 0 / 0.5 / 1 per axis.
void split_hud_anchor(const HudPlace& p, const float box[4], float* ax, float* ay);
// An item that View::hud_list laid out at 4:3 over the whole output (x_ndc = 2 x / width - 1 + 1 / out_w, y_ndc = 1 -
// 1 / out_h - 2 y / height; column-major `model`, 2D -> clip) moved by `m`: the move goes on the left of the matrix
// (x_clip' = alpha x_clip + beta w_clip, likewise y), so whatever hud_list built into it is kept. `scissor` (output
// fractions, x0 y0 x1 y1) is moved the same way and kept inside `clip` (output pixels); false: nothing left to draw.
void split_hud_model(const HudAffine& m, int width, int height, int out_w, int out_h, float model[16]);
bool split_hud_scissor(const HudAffine& m, int width, int height, int out_w, int out_h, const float clip[4], float scissor[4]);

}  // namespace b3replay

#endif
