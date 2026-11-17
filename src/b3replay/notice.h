// notice.h -- a short message banner of the native window (mac5, 2026-11-09): text lines in a rounded dark box at the
// top centre of the window, held for a few seconds and faded out. First use: safe mode (include/b3settings/safemode.h,
// "SAFE MODE: the last two starts did not end cleanly ..."). Drawn only; our own picture (glyphdraw.h's stroke font),
// one point-sampled textured quad in the 2D stage, as the skip hint.
#ifndef B3REPLAY_NOTICE_H
#define B3REPLAY_NOTICE_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "b3gfx/b3gfx.h"
#include "glyphdraw.h"

namespace b3replay {

class Notice {
public:
    // Show `lines` (upper case reads best: the stroke font draws lower case as upper) for `hold_s` seconds, then a
    // one-second fade
    void show(std::vector<std::string> lines, float hold_s = 10.0f);
    bool visible() const { return !lines_.empty() && age_ < hold_ + 1.0f; }
    // Opacity 0..1 at `age` seconds after show() (held, then a linear one-second fade)
    static float opacity(float age, float hold);
    // Advance by `dt` seconds and append the banner's draw item in window pixels (`w` x `h`) to `out`
    void draw_list(b3gfx::Device& dev, int w, int h, float dt, std::vector<b3gfx::DrawItem>& out);
    // The banner's picture for a window `window_h` pixels high: straight RGBA rows, `w` x `h`
    static void picture(const std::vector<std::string>& lines, int window_h, int& w, int& h, std::vector<std::uint8_t>& rgba);
    // The same picture, kept: when one line's text changes and the box keeps its size, only that line's rows are drawn
    // again (devnav1, 2026-11-13: photo mode's help box shows the field of view and the focus, new every frame while
    // they are held; the whole box cost ~16 ms at an 1800-pixel window, the line ~1-2 ms). Equal to picture() to the bit.
    const std::vector<std::uint8_t>& picture_kept(const std::vector<std::string>& lines, int window_h, int& w, int& h);
    // The banner's box (x, y, w, h) in a `w` x `h` window while it shows (the FPS counter keeps clear of it); false: none
    bool box(int w, int h, float (&xywh)[4]) const;
    // photo mode's help box (photomode.h): at the bottom centre instead of the top
    bool bottom = false;

private:
    struct Kept {
        std::vector<std::string> lines;
        int window_h = -1, w = 0, h = 0;
        glyph::Canvas back{0, 0}, pic{0, 0};       // the box alone; the box and the lines
        std::vector<std::array<int, 2>> rows;     // each line's rows (Canvas::strokes_rows)
        std::vector<std::uint8_t> rgba;
    } kept_;
    std::vector<std::string> lines_;
    float hold_ = 10.0f, age_ = 0.0f;
    b3gfx::TextureId tex_ = b3gfx::kNone;
    b3gfx::BufferId vb_ = b3gfx::kNone, ib_ = b3gfx::kNone;
    b3gfx::ResourceEpoch epoch_;   // a track load clears the ids above (Device::resource_epoch)
    int built_h_ = -1, tex_w_ = 0, tex_h_ = 0;
    bool dirty_ = true;
};

}  // namespace b3replay

#endif
