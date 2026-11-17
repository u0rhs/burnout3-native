// showroom_view.h -- the launcher's showroom picture (photo2; the pure parts: showroom.h): one player car drawn with the
// renderer's own car model (src/b3view/carscene.h: the car file's meshes and paints, the vehicle renderer's glass and
// underside textures from DATA/GLOBAL.TXD, the environment map of a route's sky, as b3view draws a car), on a floor
// under a fixed light, by a b3gfx device without a window: each frame is drawn offscreen and read back, and the page
// shows it as a picture (the launcher draws with SDL_Renderer, which cannot host a b3gfx view). Everything comes from
// the user's game files, at run time.
#ifndef B3LAUNCHER_SHOWROOM_VIEW_H
#define B3LAUNCHER_SHOWROOM_VIEW_H

#include "showroom.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace b3launcher::showroom {

class View {
public:
    View();
    ~View();
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    // The GPU device, made on the first call (SDL's video subsystem must be up). False with a reason (kept: later calls
    // give the same answer at once).
    bool ready(std::string* why);
    const char* driver() const;
    // showhd1: the HD texture packs the game would draw the car with (b3asset::hd_folders: the enabled mods' [textures]
    // packs in load order, then the player's own when HD TEXTURES is on; empty: none), indexed here when the list or
    // `generation` (the same folder scanned again) changes, not per car. The loads after it replace textures exactly as
    // the race renderer does: the same decode (b3view::load_car_model -> b3asset::decode_raster_clut) runs with these
    // packs as its b3asset::HdTextures hook, for the car's paint (the CLUT is part of the key), the glass / underside
    // textures and the reflections' sky. True when that changes what a load draws (load the car again).
    bool set_hd(const std::vector<std::string>& folders, unsigned generation);
    std::size_t hd_indexed() const;    // the replacements the packs hold (0: no HD textures)
    std::size_t hd_replaced() const;   // the textures the last load took from them
    // Loads `car` in paint `paint` (wrapping at the file's paint count) from `game_dir`; false with a reason.
    bool load(const std::filesystem::path& game_dir, const CarEntry& car, int paint, std::string* why);
    bool loaded() const;
    int paints() const;   // the loaded car's paint count (1 when none)
    int paint() const;    // the paint shown
    // The loaded car on its turntable, `w` x `h` pixels into `rgba` (RGBA8 rows, top row first). False: nothing drawn.
    // `car` false: the floor and the shadow alone (the showroom HD check's car mask).
    bool draw(const Turntable& t, int w, int h, std::vector<std::uint8_t>& rgba, bool car = true);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace b3launcher::showroom

#endif
