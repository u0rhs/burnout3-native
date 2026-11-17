// fpspanel.h -- the FPS counter of the native window (gsoff1; redesigned in fpsoverlay2, docs/native-renderer.md §28,
// docs/specs/settings-page.md §5). A cut-corner dark plate with an amber edge and a short amber accent bar, the look of
// the notices and photo mode's plates (photohud.h), built from solid rectangles only (a 5 x 7 pixel font, a 1 x 1 white texture, one batched draw item whose quads are
// uploaded at most twice a second; no bitmap is drawn on the CPU and no texture is updated; no game art):
//
//   MINIMAL    the frame rate alone: a small plate of the number, a three-cell signal gauge and the word FPS.
//   STANDARD   the same plate with the 1 % low under the number (no graph); both small views stay within about 8 % of the
//              window's width, however large the window.
//   DETAILED   the big plate (about 12 % of the window's width on a big window): the number, the gauge, the 1 % low and a frame-time graph (a filled sparkline of the last
//              8 seconds in milliseconds, the slowest frame of each 0.2 s, a dashed line at the target frame time, slow
//              frames in amber and spikes in red), also the average, the current and the worst frame time, the
//              input-to-screen latency when it is measured, the render size / MSAA / preset, and the GPU and its backend.
//
// The target is the FRAME RATE LIMIT's frame time, else 60 Hz's (16.7 ms). The number, the gauge and the graph carry the
// state without colour alone: three lit cells at the target (95 % of it or more), two below it, one far below (under
// 75 %); the graph's shape and its target line show the same; the colours are mint, amber and red.
//
// Position: one of the four corners, 4.5 % of the window's height from the edges (as photo mode's plates: the same on
// every side, so a 21:9 or 32:9 window keeps the plate at its edge), scaled by the window's height and HUD SIZE. It moves
// out of the way of a notice (down from the top edge, up from the bottom edge) and, while photo mode's own display is up,
// sits in the top left (its readouts take the top right, its controls the bottom centre).
//
// Drawn only, in the 2D stage, after a screenshot is taken: a screenshot never holds it. Nothing here reaches the game.
#ifndef B3REPLAY_FPSPANEL_H
#define B3REPLAY_FPSPANEL_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "b3gfx/b3gfx.h"

namespace b3replay {

enum class FpsView : std::uint8_t { kOff, kMinimal, kStandard, kDetailed };
enum class FpsCorner : std::uint8_t { kTopLeft, kTopRight, kBottomLeft, kBottomRight };
enum class FpsState : std::uint8_t { kAtTarget, kBelow, kFarBelow };

FpsView fps_view_from_setting(int v);       // the setting's 0..3 (out of range: off)
FpsCorner fps_corner_from_setting(int v);   // 0..3 (out of range: top left)
// The shortcut's cycle: minimal, standard, detailed, off, minimal ...
FpsView fps_view_next(FpsView v);
// The state of `fps` against the target frame rate: at it from 95 %, far below it under 75 %
FpsState fps_state(float fps, float target_fps);
// While photo mode's own display is up (its UI not hidden) the counter takes the top left
FpsCorner fps_corner_in_photo(FpsCorner chosen, bool photo_ui_up);

// What the detailed view shows besides the frame rate
struct FpsInfo {
    float target_fps = 60;       // the frame rate limit, else 60
    float latency_ms = -1;       // lat1: the input-to-screen estimate; negative: not measured
    int width = 0, height = 0;   // the render size in pixels (0: unknown)
    int msaa = 0;                // 1, 2, 4, 8 (0: unknown)
    std::string preset;          // "HIGH", "CUSTOM" ... (empty: none)
    std::string gpu, backend;    // "NVIDIA GEFORCE RTX 4070", "VULKAN" (empty: unknown)
};

// The numbers of the last 8 seconds (FpsPanel::kWindowSeconds)
struct FpsStats {
    int frames = 0;        // frames in the window
    float fps = 0;         // the big number: the last 0.5 s's frames per second
    float frame_ms = 0;    // and their mean frame time
    float avg = 0;         // the window's frames per second
    float low1 = 0;        // 1 % low: the frame rate of the slowest 1 % of the window's frames (at least one)
    float max_ms = 0;      // the slowest frame
};

class FpsPanel {
public:
    static constexpr int kColumns = 40;                  // the graph: 40 columns of 0.2 s
    static constexpr double kColumnSeconds = 0.2;
    static constexpr double kWindowSeconds = kColumns * kColumnSeconds;   // 8 s

    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;
        bool empty() const { return w <= 0 || h <= 0; }
        bool operator==(const Rect&) const = default;
    };
    struct Layout {
        Rect plate;          // window pixels; empty when nothing is shown or nothing fits
        float scale = 1;     // the size unit: 1 = a 720-line window at HUD SIZE 100 %
        float margin = 0;
        float win_h = 0;             // the window's height
        float top = 0, bottom = 0;   // the distances kept from the window's top / bottom edge (the margin, or the race HUD's)
        bool operator==(const Layout&) const = default;
    };

    // One presented frame at `now_ns` (steady clock).
    void frame(std::uint64_t now_ns);
    FpsStats stats(std::uint64_t now_ns) const;
    // The graph's columns, the oldest first: the slowest frame in ms of each 0.2 s (0: no frame then)
    void graph(std::uint64_t now_ns, float (&ms)[kColumns]) const;
    // The graph's top in ms for the slowest frame `peak_ms` shown and the target frame time: 2 to 4 times the target, in
    // whole targets, a tenth above the peak
    static float graph_top_ms(float peak_ms, float target_ms);
    // A frame this slow (ms) is a spike: twice the target, and at least 8 ms over it
    static float spike_ms(float target_ms);
    // The lines the detailed view lists under the numbers, in order (latency, render settings, GPU), for `info`
    static std::vector<std::string> detail_lines(const FpsInfo& info);

    // The plate's place in a `w` x `h` window at HUD SIZE level `hud_size` (a11y.h); `detail_rows` = detail_lines().size().
    // `avoid` (a notice's box): a plate that meets it, plus a small gap, moves down (top corners) or up (bottom corners)
    // clear of it when the window has room. `game_hud`: the race HUD is up: its plates sit in all four corners (position and
    // lap at the top, the boost bar and speed at the bottom, grown by HUD SIZE), so the plate starts below the top ones and
    // ends above the bottom ones instead of at the plain margin.
    static Layout layout(int w, int h, int hud_size, FpsView view, FpsCorner corner, int detail_rows, const Rect* avoid = nullptr, bool game_hud = false);
    // The distances from the window's top / bottom edge the race HUD's plates reach at HUD SIZE level `hud_size`
    static float game_hud_top(int h, int hud_size);
    static float game_hud_bottom(int h, int hud_size);
    // The lines detail_lines() gives, counted without building them
    static int detail_row_count(const FpsInfo& info);
    // The plate's quads for a layout, in the plate's own pixels (0, 0 = its top left; `l.plate` w x h is its size), whole-pixel
    // rectangles and a few slanted ones, in drawing order (vertex colours as the 2D stage takes them); empty when off
    static void build(const Layout& l, FpsView view, const FpsStats& st, const float (&graph_ms)[kColumns], const FpsInfo& info,
                      std::vector<b3gfx::Vertex>& vs, std::vector<std::uint32_t>& is);

    // The plate's rectangle this frame (what draw_list draws): `natural` moved toward `wanted` (a notice's avoidance) by an eased
    // shift that starts again from nothing whenever `natural` or `view` changes, and kept inside the safe area. For tests.
    Rect place(const Layout& natural, const Layout& wanted, FpsView view, std::uint64_t now_ns);

    // Render thread: append the plate's draw item (window pixels) to `out`. `avoid`: see layout().
    void draw_list(b3gfx::Device& dev, int w, int h, std::uint64_t now_ns, FpsView view, FpsCorner corner, int hud_size, const FpsInfo& info,
                   const Rect* avoid, bool game_hud, std::vector<b3gfx::DrawItem>& out);

private:
    struct Sample { float ms; std::uint64_t ns; };
    static constexpr std::size_t kHistory = 8192;   // > 8 s at 1000 frames per second
    std::uint64_t last_ns_ = 0, win_start_ = 0, last_draw_ns_ = 0, built_ns_ = 0;
    unsigned win_frames_ = 0;
    float fps_ = 0, frame_ms_ = 0;
    std::vector<Sample> hist_ = std::vector<Sample>(kHistory);
    std::size_t head_ = 0, count_ = 0;
    // the quads are rebuilt every 0.5 s and when their layout, view or settings lines change
    struct Built {
        Layout layout;
        FpsView view = FpsView::kOff;
        FpsInfo info;
        FpsStats stats;
        bool valid = false;
    } built_;
    float shift_ = 0;   // the eased move out of a notice's way (px, down is positive), measured against shift_layout_ / shift_view_
    Layout shift_layout_;
    FpsView shift_view_ = FpsView::kOff;
    b3gfx::BufferId vb_ = b3gfx::kNone, ib_ = b3gfx::kNone;
    b3gfx::TextureId white_ = b3gfx::kNone;
    unsigned epoch_ = 0;   // the device's resource_epoch the three ids above were made under (a track load clears them all)
    std::vector<b3gfx::Vertex> vs_;   // the plate's quads, kept between rebuilds
    std::vector<std::uint32_t> is_;
    int built_lat_ = 0;
    struct NatKey {
        int w = 0, h = 0, hud = 0;
        FpsView view = FpsView::kOff;
        FpsCorner corner = FpsCorner::kTopLeft;
        int rows = 0;
        bool game_hud = false;
        bool operator==(const NatKey&) const = default;
    } nat_key_;
    Layout nat_;   // the natural layout for nat_key_ (kept: the layout is not worked out every frame)
    bool nat_valid_ = false;
};

}  // namespace b3replay

#endif
