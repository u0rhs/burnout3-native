// speedrunpanel.h -- the speedrun timer's panel in the native window (speedrun1; speedrun.h has the numbers): a dark
// box at the right edge, under the race HUD's LAP panel, with the rows speedrun::rows gives (the main timer large, the
// splits and the best smaller). Our own picture: the stroke font of glyphdraw.h rasterised once per window height into
// an atlas (one cell per character, digits of one width so the timer does not jitter), then point-sampled quads per
// character. Drawn only.
#ifndef B3REPLAY_SPEEDRUNPANEL_H
#define B3REPLAY_SPEEDRUNPANEL_H

#include <array>
#include <cstdint>
#include <vector>

#include "b3gfx/b3gfx.h"
#include "speedrun.h"

namespace b3replay {

class SpeedrunPanel {
public:
    // The panel's draw item in window pixels (`w` x `h`), appended to `out`; nothing for no rows.
    void draw_list(b3gfx::Device& dev, int w, int h, const std::vector<speedrun::Row>& rows, std::vector<b3gfx::DrawItem>& out);

    // The layout in pixels without a device (tests): the panel's box (x, y, w, h) for `rows` in a `w` x `h` window.
    static std::array<float, 4> box(int w, int h, const std::vector<speedrun::Row>& rows);

private:
    struct Glyph { float u0 = 0, v0 = 0, u1 = 0, v1 = 0, w = 0, adv = 0; };
    void build(int h, std::vector<std::uint8_t>& px);   // the atlas (straight RGBA) and its cells
    std::array<Glyph, 128> big_{}, small_{};
    float big_h_ = 0, small_h_ = 0, white_u_ = 0, white_v_ = 0;
    int built_h_ = -1, tex_w_ = 0, tex_h_ = 0;
    b3gfx::TextureId tex_ = b3gfx::kNone;
    b3gfx::BufferId vb_ = b3gfx::kNone, ib_ = b3gfx::kNone;
    b3gfx::ResourceEpoch epoch_;   // a track load clears the ids above (Device::resource_epoch)
};

}  // namespace b3replay

#endif
