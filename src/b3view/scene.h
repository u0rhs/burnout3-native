// b3view scene: loads a game file through b3asset and turns it into b3gfx buffers, textures and draw items.
#ifndef B3VIEW_SCENE_H
#define B3VIEW_SCENE_H

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "b3asset/enviro.h"
#include "b3asset/model.h"
#include "b3asset/track.h"
#include "b3gfx/b3gfx.h"

namespace b3view {

// hdkey: decoded levels as the device takes them (pointing into `levels`)
inline std::vector<b3gfx::TextureLevel> texture_levels(const std::vector<b3asset::Image>& levels) {
    std::vector<b3gfx::TextureLevel> out;
    for (const auto& l : levels) out.push_back({l.w, l.h, l.rgba.data()});
    return out;
}
// A decoded texture on the device (kNone: no levels), and the decode an HD pack replacement stands for beside it
// (`original`, DecodedRaster::original: the HD TEXTURES key's other picture, Device::create_texture_hd)
inline b3gfx::TextureId upload_texture(b3gfx::Device& dev, const std::vector<b3asset::Image>& levels,
                                       const std::vector<b3asset::Image>& original = {}) {
    if (levels.empty()) return b3gfx::kNone;
    return dev.create_texture_hd(texture_levels(levels), texture_levels(original));
}

struct LoadOptions {
    int lod = 3;      // cars
    int palette = 0;  // cars: paint variant
    bool track_lod = false;  // tracks: the units' LOD meshes instead of the high ones
    std::string region_first;      // regions: this route (e.g. "P1_V1") loads first, so its version wins in X mode
    bool region_exclusive = false;  // regions: the first route to build an area owns it (later routes only add new ground)
};

struct Scene {
    b3asset::Model model;
    std::vector<b3gfx::DrawItem> items;          // one per model.draws entry (build_scene), then any extras
    std::vector<b3gfx::TextureId> textures;      // the GPU texture of each model.textures entry
    std::vector<int> anim_items;                 // items whose material is animated (model.draws[k].anim >= 0)
    // Items prepended before model.draws' (add_enviro's sky): model.draws[k] is items[k + item_base] (water1: animate
    // used items[k] and so retextured the sky and the two draws before each animated one in b3view)
    std::size_t item_base = 0;
    // perf1 (docs/native-renderer.md §33.7): the cloud layer (sky component 2) is a dome open above 65 degrees (e 0..0.75,
    // the game's own [C maDefinitions 0x4B9CA0]); a free camera looking up sees its edge as a ring. `cloud_free` is the
    // same layer with its alpha faded to 0 over its top three rings, for free-camera views only (items[cloud_item]).
    int cloud_item = -1;
    b3gfx::DrawItem cloud_free;
    std::string description;  // one line for the title / log
    bool prelit = false;      // vertex colours carry lighting (tracks): draw unlit
    // Tracks: the route's ENVIRO.DAT (sky, sun, fog, specular; docs/sky-enviro.md), when present.
    bool has_enviro = false;
    b3asset::Enviro enviro;
    b3asset::FogModel fog;
};

// Animated track materials (anim1, docs/native-renderer.md §15): sets every animated item's texture (frames[cur]) and,
// for flag 0x200, its UV scroll track_uv_scroll(t), from `states` (one per model.track_anims entry). Fewer states than
// anims: those items are left as they are.
void animate(Scene& scene, const std::vector<b3asset::TrackAnimState>& states);

// b3view's animation clock (it has no game clock): CB3StaticTrack::Update on the 1/60 s ticks of a clock that starts
// at 0 with the materials prepared, as a race does (docs/native-renderer.md §15). at(m, s) advances to `s` seconds
// (an earlier `s` starts over) and returns the states there, a continuous scroll exactly at `s`.
class AnimClock {
public:
    const std::vector<b3asset::TrackAnimState>& at(const b3asset::Model& m, double seconds);
private:
    std::vector<b3asset::TrackAnimState> state_, out_;
    long ticks_ = 0;
};

// RenderWater (live2 / water1, docs/native-renderer.md §20.3, §21). `CB3StreamedTrack::RenderBeforeVehicles` 0x21C780
// ends, when D_00665F1D is set, with `CB3StaticTrack::RenderWater` 0x21D550 [C matched] for the camera unit's high block:
// level = +0x70, the STATIC +0x2C object [+0x6E] drawn as plain static objects (the water surface) after the STATIC +0x30
// object [+0x6F] (when >= 0) drawn with every mesh's material type forced to 7. VU1 0x47E300 (type 7) is the type-0
// program without the back-face test (0x4745C0) plus a mirror of the view matrix about y = level at entry and its
// restore at exit [C vudis diff: 16 added instruction pairs, nothing else]: the river's reflections, upside down under
// the translucent surface. Every reflection material on the 37 routes is type 0 and every surface type 6 blended by As
// [C census of the files], so the meshes' own shading is the program's. Both are drawn before the full-screen specular,
// but the GS frame shows no specular sheen on the surface (roadrage1 GS poll 11506 [C], §21.1), so b3gfx's order
// (blended after the specular) is kept.
struct WaterTable {
    std::map<int, std::vector<int>> surface, reflect;   // STATIC +0x2C / +0x30 object index -> Scene item indices
    std::vector<std::array<float, 3>> unit;             // per unit: high block +0x6E, +0x6F (object, -1 none), +0x70 level
    // Indexes `sc`'s "water g" / "water+30 g" items (b3asset part names) and reads every unit's high block from
    // STREAMED.DAT through the STATIC +0x58 table [C: equal to the camera unit's block in memory, live2 §20.3].
    void build(const Scene& sc, const std::vector<std::uint8_t>& st, const std::vector<std::uint8_t>& sm);
    bool is_water(int item) const;   // one of the indexed items (drawn only through append)
    // RenderWater's draws for camera unit `u`, appended in the game's order (reflection, then surface). `skip`
    // (diagnostics): 1 none, 2 without the +0x2C object, 3 without the +0x30 object.
    void append(const Scene& sc, int u, std::vector<b3gfx::DrawItem>& out, int skip = 0) const;
    // b3view has no camera unit: the unit whose centre (Model::waypoints, block +0x10) is nearest the eye [G].
    static int nearest_unit(const b3asset::Model& m, const float eye[3]);
};

// Tracks: loads the route's ENVIRO.DAT and prepends the sky draws (docs/sky-enviro.md). False if there is none.
bool add_enviro(b3gfx::Device& dev, const std::string& route, Scene& scene);
// loadlag1: add_enviro in two halves. read_route_enviro reads and decodes the file (no device: any thread; false: none,
// or it did not parse); add_enviro(dev, scene) then builds the sky from scene.enviro already filled by it.
bool read_route_enviro(const std::string& route, b3asset::Enviro& out);
bool add_enviro(b3gfx::Device& dev, Scene& scene);
// The sky backdrop (b3asset::build_backdrop) on the device; hdkey: with an HD gradient, the original's beside it
b3gfx::TextureId upload_backdrop(b3gfx::Device& dev, const b3asset::Enviro& env, const float to_sun[3]);

// Fills the frame parameters of the game model (sun, full-screen specular, fog, present scale) for `scene` and the
// camera; `fog` / `specular` switch those passes.
void frame_params(const Scene& scene, const float eye[3], const float fwd[3], bool fog, bool specular,
                  b3gfx::FrameParams& fp);

// Detects the file kind from the path: CARn.BGV / .BTV, or a track route folder (or its STATIC/STREAMED.DAT) and decodes it. Returns false with a reason.
bool load_model(const std::string& path, const LoadOptions& opt, b3asset::Model& out, std::string& kind, std::string* why);
// Tracks: the route folder of `path` (empty for other files).
std::string route_dir(const std::string& path);

// Uploads the model to `dev` and builds draw items.
// `share`: a scene built from the same texture table (equal names and sizes, e.g. a route's LOD model next to its main
// model): its GPU textures are used instead of uploading the table again.
bool build_scene(b3gfx::Device& dev, b3asset::Model&& model, bool prelit, Scene& out, std::string* why,
                 const Scene* share = nullptr);

}  // namespace b3view

#endif
