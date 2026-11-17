// modoverlay.h -- the code mods' 2D over the game (mod kit Phase 2b, docs/modding/DESIGN.md §2.2 overlays): the items a
// code mod queued for a frame (B3ModApi quad / text; the 'HMO1' record, include/b3mod/overlay_record.h, decoded per tick
// as Tick::mod_overlay) drawn in the 2D stage after the game's HUD. Coordinates are the game's 640 x 448 screen, placed
// in the window's centred 4:3 box (as the PC SETTINGS page); text in the port's stroke font (glyphdraw.h), quads filled
// or textured with a mod's texture (B3ModApi.texture: b3r_mod_texture, any thread). Drawn only: nothing goes back.
#ifndef B3REPLAY_MODOVERLAY_H
#define B3REPLAY_MODOVERLAY_H

#include "b3gfx/b3gfx.h"
#include "b3mod/overlay_record.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace b3replay {

class ModOverlay {
public:
    static ModOverlay& instance();
    // Any thread: a mod's texture (straight-alpha RGBA rows), made on the device at its first use
    void add_texture(std::uint32_t id, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba);
    // Render thread: the list's draw items for a `w` x `h` window, appended to `out` (one per texture); the number of
    // items drawn
    std::size_t draw_list(b3gfx::Device& dev, int w, int h, const b3mod::OverlayList& list, std::vector<b3gfx::DrawItem>& out);
    // The box the 640 x 448 screen takes in a w x h window (x, y, width, height); for tests
    static void box(int w, int h, float& x, float& y, float& bw, float& bh);

private:
    struct Pending {
        std::uint32_t w = 0, h = 0;
        std::vector<std::uint8_t> rgba;
    };
    std::mutex m_;
    std::map<std::uint32_t, Pending> pending_;
    std::map<std::uint32_t, b3gfx::TextureId> textures_;
    b3gfx::TextureId white_ = b3gfx::kNone;
    b3gfx::ResourceEpoch epoch_;   // a track load clears every id above (Device::resource_epoch)
    std::map<std::uint32_t, Pending> kept_;   // the mods' pictures as given, to upload them again after a clear (render thread only)
    std::vector<std::pair<b3gfx::BufferId, b3gfx::BufferId>> buffers_;   // one pair per draw item of a frame
};

}  // namespace b3replay

#endif
