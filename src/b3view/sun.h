// The sun corona and lens flare for b3gfx (water1, docs/native-renderer.md §21; the game's logic in b3asset/sunflare.h).
#ifndef B3VIEW_SUN_H
#define B3VIEW_SUN_H

#include <string>

#include "b3asset/enviro.h"
#include "b3asset/sunflare.h"
#include "b3gfx/b3gfx.h"

namespace b3view {

struct Sun {
    bool on = false;                     // the route draws a sun (ENVIRO +0xA4 set, not one of Prepare's four track IDs)
    b3gfx::TextureId texture = b3gfx::kNone;   // ENVIRO +0xA4 "suncorona" (128 x 256 PSMT8)
    int tex_w = 0, tex_h = 0;
    b3asset::SunFlareTable table;        // D_004BB610 from the game's ELF
    float colour[3] = {};                // D_01D9B970 = sun colour (+0x70) x its w (+0x20 of block 0) x 128
    float corona_scale = 0;              // ENVIRO +0x90 (x graphics manager +0 = the radius in pixels)
    // `game`: the folder holding SLUS_210.50 (the lens-flare table; without it only the corona is drawn).
    bool load(b3gfx::Device& dev, const b3asset::Enviro& env, const std::string& route, const std::string& game, std::string* why);
    // loadlag1: the lens-flare table read beforehand (any thread: read_flare), then load() with it
    struct Flare { b3asset::SunFlareTable table; bool ok = false; std::string why; };
    static Flare read_flare(const std::string& game);
    bool load(b3gfx::Device& dev, const b3asset::Enviro& env, const std::string& route, const Flare& flare, std::string* why);
    // Fills fp.sun_* for the camera at fp.eye with fp.view_proj. `display_aspect`: the output's width / height as shown
    // (4/3 for a 640 x 448 PS2-shaped image; the window's for square pixels): the game's pixel grid is 448 rows and
    // 480 x display_aspect columns. `gm0`: graphics manager +0 (the corona radius scale); `aspect`: SetupCamera's
    // mrAspectRatio (y radius / x radius; <= 0: 448 / 480, round on the display [G]); `now`: the world clock.
    void frame(b3gfx::FrameParams& fp, float display_aspect, int gm0, float aspect, float now) const;
};

}  // namespace b3view

#endif
