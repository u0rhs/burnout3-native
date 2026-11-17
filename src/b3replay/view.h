// b3replay view: the draw path shared by the offline replay (b3replay <capture>) and the live renderer (b3replay --live,
// docs/native-renderer.md §13): asset matching (route by STATIC.DAT fingerprint + camera score, cars by header bytes),
// the route's scenes (high / LOD units, instances, backdrops, props, ENVIRO.DAT sky), per-frame interpolation between
// 60 Hz ticks, and the per-frame draw list. Moved from main.cpp (render4) without changing the draw itself.
#ifndef B3REPLAY_VIEW_H
#define B3REPLAY_VIEW_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include <memory>
#include <string>
#include <tuple>
#include <array>
#include <functional>
#include <set>
#include <vector>

#include "a11y.h"
#include "b3asset/crash.h"
#include "b3gfx/b3gfx.h"
#include "capture.h"
#include "coronas.h"
#include "effects.h"
#include "menumouse.h"
#include "prompts.h"
#include "../b3view/carscene.h"
#include "../b3view/crashscene.h"
#include "../b3view/scene.h"
#include "../b3view/sun.h"

namespace b3replay {

namespace ghost { struct Lap; }   // ghost1 (ghost.h)

// ---- asset matching -------------------------------------------------------------------------------------------------

// The route under <game>/TRACKS whose STATIC.DAT has `size` and `units`, scored by the camera samples (median distance
// to the current unit's centre; the closest wins, ties by path). Empty if none.
std::string find_route(const std::string& game, std::uint32_t size, int units, const std::vector<const Tick*>& samples);
// The samples find_route scores for ticks [first, last] of `ticks` (at most ~400, evenly spaced).
template <class Ticks>
std::vector<const Tick*> route_samples(const Ticks& ticks, std::size_t first, std::size_t last) {
    std::vector<const Tick*> out;
    const std::size_t step = std::max<std::size_t>(1, (last - first) / 400);
    for (std::size_t i = first; i <= last; i += step) out.push_back(&ticks[i]);
    return out;
}

struct CarFile { std::string path; std::vector<std::uint8_t> bytes; };
std::vector<CarFile> scan_cars(const std::string& game);
// Car files whose header bytes 0x00..0x4C equal the captured vehicle data's; -1 if none (`candidates`: how many).
int match_car(const std::vector<CarFile>& cars, const ModelHead& head, int* candidates);

// ---- interpolation --------------------------------------------------------------------------------------------------

Mat34 lerp(const Mat34& a, const Mat34& b, float t);       // rotation by slerp, row lengths and translation linearly; t in (1, 3] extrapolates (perf1)
Vehicle lerp(const Vehicle& a, const Vehicle& b, float t);

// What one displayed frame shows: the tick's track state, its vehicles and camera, blended towards the next tick.
struct Frame {
    Camera tick_cam;             // fx2: the tick's game camera as captured (not interpolated)
    std::size_t tick = 0;
    float alpha = 0;
    Camera cam;
    std::vector<Vehicle> vehicles;
    int slot_unit[17], slot_kind[17];
    std::uint16_t vis[17] = {};
    std::int8_t backdrops[8];
    // props (capture version 2, props1): the tick's mode switch and hidden placements, the dynamic props interpolated
    int props_off = -1;
    std::vector<std::uint16_t> hidden;   // sorted
    int current_unit = -1;               // the camera unit (+0x1C4 block): RenderWater's water choice (live2)
    int water_on = -1;                   // D_00665F1D (-1 unknown)
    std::vector<Prop> props;
    CrashState crash;                    // crash2: the tick's crash ramps and pickups (static; not interpolated)
    // effects (capture version 3, fx1): shadow switches, the trail emitters, the clock at the displayed time
    FrameEffects fx;
    // animated track materials (anim1, §15): the tick's update-list state as captured (anim_valid), a continuous scroll
    // (period 0) blended to the displayed time; without the capture block View::anim_states recomputes it from anim_now
    bool anim_valid = false;
    std::vector<AnimMaterial> anim;
    float anim_now = 0;
    // HUD / 2D (hud1, §17): the tick's Gt2d frame, held (not interpolated) until the next tick's
    std::shared_ptr<const HudFrame> hud;
    std::uint32_t fe_car = 0;            // menu1 (§22): the front end's car is drawn (Tick::fe_car)
    // fe2 (§24): the tick's static-track fingerprint (Tick::route_size / route_units): the front end's showroom
    std::uint32_t route_size = 0;
    int route_units = 0;
    // the sun corona (water1, §21): the tick's camera-record inputs and world clock (the flicker; not interpolated)
    SunCam sun;
    float sun_now = 0;
    RadialBlurIn blur;                   // water1: the tick's radial blur parameters (held, not interpolated)
    PostIn post;                         // post1 (§25): the bloom, the directional blur, the blur choice (held)
    // gsoff1 (§29): a 2-player split-screen tick: player 2's camera (viewport +0x130; `cam` is player 1's), interpolated
    bool split = false;
    Camera cam2, tick_cam2;
    // split1 (§29.5): the tick's player 2 inputs (Tick::view2: streamed track 1's units, backdrops, camera unit and
    // barriers), which View::split_frame swaps in for view 2; set only in split frames
    View2In view2;
    // ghost1 (ghost.h): the tick's Time Attack lap and the lap's time at the displayed instant (the game's timer, CB3Score
    // +0x2C8, blended between ticks; 0 at the tick the lap began); lap_index -1: no lap running
    bool time_attack = false;
    int lap_index = -1;
    float lap_time = -1;
};

// A window of consecutive ticks, by absolute index [first, last]: the offline capture's segment or the live feed's
// recent ticks.
class Timeline {
public:
    virtual ~Timeline() = default;
    virtual std::size_t first() const = 0;
    virtual std::size_t last() const = 0;
    virtual const Tick& at(std::size_t i) const = 0;
};

// How far the camera interpolation looks for the ticks where the camera value changed (2-tick frames move it every
// second tick; a camera unchanged for longer is held, not blended towards a far change).
constexpr int kCameraKeyReach = 8;

// The frame at tick i (first <= i <= last) blended by frac in [0, 1) towards tick i + 1; vehicles matched by guest
// address and model, the camera between the ticks where its value changed (cuts not blended), looking at most `reach`
// ticks either way for those changes.
// `live` (perf1): the live feed's newest ticks, where the next camera key may not have arrived yet (interp_camera).
Frame frame_at(const Timeline& tl, std::size_t i, double frac, bool interpolate, std::size_t reach = kCameraKeyReach, bool live = false);

// traffic-popin (2026-11-10: traffic still pops in): traffic cars come and go gradually instead of at once (renderer
// only; the game's choice of what exists and where is untouched). CB3TrafficSystem::Draw draws an active traffic car only
// within 275 m of its camera [C nonmatching draft 0x3078F0, lrMaxDistanceSq = 75625], and the traffic system activates
// cars inside that radius (US/C2_V2: at 143 / 178 m in view) and removes cars inside it (at 111..265 m in view), so with
// the whole track drawn (no PS2 fog / streaming hiding them) cars appeared and vanished in plain view. Three factors give
// the car's opacity (car.frag: a premultiplied fade, Z still written), its shadow's alpha and its lights' colour:
//   appear:   0 -> 1 over kTrafficAppearSeconds of game time from the tick the car came within the radius (Vehicle::since),
//             whatever its distance (a car activated 140 m ahead fades in instead of popping);
//   distance: 1 -> 0 (smoothstep) over the last kTrafficFadeBand metres before the 275 m cut, only for what the game cuts
//             there: traffic not crashing (+0x174 bit 2) and crashed vehicles. Race cars are drawn at any distance by
//             the game and are never faded (mac5's 25 m screen door also hid them beyond 275 m);
//   vanish:   a traffic car the game removes inside the radius (not one that crashed: it goes on as a crashed vehicle)
//             stays in the frame for kTrafficVanishSeconds, moving on at its last speed and fading out (Vehicle::gone).
constexpr float kTrafficDrawRadius = 275.0f;
constexpr float kTrafficFadeBand = 75.0f;
constexpr float kTrafficAppearSeconds = 0.6f;
constexpr float kTrafficVanishSeconds = 0.6f;
// Vehicle::since for vehicle k of tick i: the world clock of the first tick of its unbroken run within the radius
// (present, not kFar, no jump of more than 20 m between ticks: a slot reused for another car), -1 when that run reaches
// back to the timeline's first tick, longer than kTrafficAppearSeconds, or the clock is unusable (stopped or reset).
// `appear_tick` (optional): that first tick's index.
float traffic_since(const Timeline& tl, std::size_t i, std::size_t k, std::size_t* appear_tick = nullptr);
// The same for every vehicle of tick i (worked out once per tick and kept: frames repeat ticks; valid until the next call).
// split2 (§29.6): view 1 = player 2's view of a split screen (its own radius, Vehicle::cull2: Vehicle::since2).
const std::vector<float>& traffic_since_tick(const Timeline& tl, std::size_t i, int view = 0);
// The traffic cars removed within the radius during the last kTrafficVanishSeconds before tick i (vanish above), as
// they are drawn at world clock `now`: appended to `out`. split2: in a split screen, those removed within either view's
// radius, with gone (view 1) / gone2 (player 2's) -1 for the view whose radius it was not in.
void traffic_vanishing(const Timeline& tl, std::size_t i, float now, std::vector<Vehicle>& out);
// The fade (opacity) of vehicle v drawn at world clock `now` with the game camera at `cam_pos` (the factors above).
float traffic_fade(const Vehicle& v, float now, const float cam_pos[3]);
// B3_TRAFFIC_PROBE=1 (live feeds) / b3replay --traffic-scan: one line per traffic car that comes within the radius at tick i
// (how: spawn / range / jump; its distance and screen position), per tick while it fades in, and per car that leaves.
struct TrafficProbeStats {
    long appear[3] = {}, appear_on_screen[3] = {};   // spawn, range, jump
    long gone_despawn = 0, gone_despawn_on_screen = 0, gone_range = 0;
    long dist_hist[6] = {};                          // on-screen appearances by distance: <50, <100, <150, <200, <250, <=275 m
};
void traffic_probe(const Timeline& tl, std::size_t i, TrafficProbeStats* stats = nullptr);

// hud4 (§17.13): what the displayed frame's 3D was drawn with, so world-anchored HUD markers can be projected again
struct HudProjection {
    float view_proj[16] = {};                   // the 3D pass's view-projection (b3gfx column-major, clip y up)
    const std::vector<Vehicle>* vehicles = nullptr;   // the frame's (interpolated) vehicles
};

// ---- the route's scenes and the draw list ---------------------------------------------------------------------------

// loadlag1: a route's files and its two decoded models (View::prepare_track), before the GPU half (View::load_prepared)
struct TrackPrep {
    std::string route;
    std::vector<std::uint8_t> st, sm;   // STATIC.DAT, STREAMED.DAT
    b3asset::Model mh, ml;              // the high units (+ instances, backdrops, props) and the LOD units
    bool ok = false;
    std::string why;
    // the rest of load_track's files, read and decoded here (load_prepared only uploads them)
    b3asset::Enviro enviro;             // ENVIRO.DAT (b3view::read_route_enviro)
    bool enviro_ok = false;
    std::vector<std::uint8_t> global_txd;   // <game>/DATA/GLOBAL.TXD (empty: not read)
    bool global_ok = false;
    b3asset::DecodedRaster blur_mask;   // its "radialblurmask"
    bool blur_ok = false;
    b3asset::DecodedRaster car_slots[5];   // b3view::decode_car_globals
    int car_slots_n = 0;
    std::string car_why;
    std::vector<std::uint8_t> bgd;      // GAMEDATA.BGD
    bool bgd_ok = false;
    b3view::Sun::Flare flare;           // the lens-flare table (SLUS_210.50)
    double ms = 0;                      // the time it took (ms)
};

// reflect1: CAR REFLECTIONS' start value, 2 Enhanced; $B3_CAR_REFLECTIONS = off | original | enhanced overrides it (the golden record's
// reference look, tools/golden/baseline.sh; apply_view keeps it over the settings' value)
inline int car_reflect_default() {
    const char* e = std::getenv("B3_CAR_REFLECTIONS");
    return !e ? 2 : !std::strcmp(e, "off") ? 0 : !std::strcmp(e, "original") ? 1 : 2;
}

// bloom2: the bloom's level, 1 Soft (the default of the PC SETTINGS) or 2 Standard (the game's own strength, drawn the way its GS
// does); $B3_BLOOM_LEVEL = soft | standard overrides the settings' value (the golden record's reference look, tools/golden/baseline.sh).
// A View starts at Standard: the offline tools (b3replay --offscreen) draw the game's own bloom unless a level is asked for.
inline int bloom_level_env(int fallback) {
    const char* e = std::getenv("B3_BLOOM_LEVEL");
    return !e ? fallback : !std::strcmp(e, "soft") ? 1 : !std::strcmp(e, "standard") ? 2 : fallback;
}

struct View {
    int bloom_level = bloom_level_env(2);       // bloom2: 1 soft, 2 standard (apply_view)
    std::string game;
    int car_lod = -1;                           // -1: the game's choice (game_lod), else forced
    bool deform = true;                         // damage deformation (render6; --no-deform for comparisons)
    bool glass_damage = true;                   // settings1: GLASS DAMAGE off draws every car's glass intact (state 0)
    int car_reflect = car_reflect_default();                        // reflect1: PC SETTINGS > CAR REFLECTIONS: 0 off, 1 original (the game's env pass), 2 enhanced
    void apply_car_reflections(std::vector<b3gfx::DrawItem>& out, std::size_t first, float strength) const;
    // settings1 CAMERA (drawn only): the game camera's view window scaled by fov_scale, the eye moved along its view
    // direction so the chased car keeps its size (fov_correction; docs/specs/settings-page.md §4)
    float fov_scale = 1.0f;
    bool fov_correction = true;
    void apply_fov(Camera& c, const std::vector<Vehicle>& vehicles) const;
    std::vector<CarFile> cars;                  // scan_cars(game), once
    std::map<ModelHead, int> head_file;         // model header -> index into cars (-1 unmatched)
    std::string route;                          // the loaded route folder ("" = none)
    std::uint32_t route_size = 0;
    int route_units = 0;
    b3view::Scene hi, lo;                       // the route's high and LOD units (+ instances, backdrops, props on hi)
    b3view::Scene sky;                          // ENVIRO.DAT: sky draws, sun, fog, specular (render3's model, §11.5)
    std::map<int, std::vector<int>> hi_unit, hi_inst, lo_unit;  // unit -> item indices
    std::map<int, std::vector<int>> backdrop;   // STATIC +0x24 index -> items
    std::map<int, std::vector<int>> barrier;    // STATIC +0x28 index -> items (func_0021C010's barriers, crash2)
    std::vector<int> always;                    // props (water: below)
    // Water (live2 §20, RenderBeforeVehicles / RenderWater 0x21D550): per STATIC +0x2C / +0x30 object its items; per unit
    // the camera unit's high block +0x6E / +0x6F (object indices, -1 none) and +0x70 (water level), from STREAMED.DAT
    b3view::WaterTable water;                   // per STATIC +0x2C / +0x30 object its hi items; per unit +0x6E / +0x6F / +0x70
    std::map<int, std::vector<int>> prop_items; // STATIC +0x48 placement -> its hi items (hidden / moved per tick; props1)
    std::map<int, int> item_prop;               // hi item -> placement
    // Cars (render5, docs/native-renderer.md §12.6): the game's car model, per (file, paint, LOD block)
    std::map<std::tuple<int, int, int>, std::unique_ptr<b3view::CarModel>> car_models;
    b3view::CarGlobals car_globals;             // texture slots 1-4 (GLOBAL.TXD), env sky and sun (ENVIRO.DAT)
    // Crash-mode game data (crash2, docs/native-renderer.md §12.9): the route's GAMEDATA.BGD stages as draws
    b3view::CrashScene crash_scene;
    int fallback_stage = -1;                    // captures without the game data: the stage found by start_stage
    bool crash_objects = true;                  // --no-crash: leave the ramps and pickups out (comparisons)
    bool finish_lines = true;
    bool free_view = false;                     // perf1 (§33.7): the frame is a free camera's (sky cap fade, own bloom)
    // perf1: draw_list's per-stage CPU times (static, vehicles, effects, game data, popcorn, coronas, sparks), ns summed
    // while prof_on (b3replay --profile)
    bool prof_on = false;
    std::array<std::uint64_t, 7> prof_ns{};
    static std::uint64_t prof_clock() { return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }                   // --no-finish (perf1, §33): leave the start / finish lines out (comparisons)
    bool animate = true;                        // --no-anim: animated materials stay at frame 0 without scroll (comparisons)
    // Effects stage (fx1, §14): shadows (and skid marks) after the cars; off with `show_effects` = false
    Effects effects;
    bool show_effects = true;
    EffectStats fx_stats;                       // of the last draw_list
    // Coronas (corona1, §16): car lights, traffic lights and the boost glow after the fog pass; off with `show_coronas`
    // = false or with the effects (`show_effects`; the game's RenderPostFogEffects shares D_00665F2C with the skid marks)
    Coronas coronas;
    bool show_coronas = true;
    // Contact effects and particles (fx2, §19): scrapes, sparks, popcorn; off with `show_particles` = false (--no-particles,
    // K) or with the effects (`show_effects`, F)
    bool show_particles = true;
    CoronaArrays corona_arrays;                 // of the last draw_list
    CoronaStats corona_stats;
    const Camera* corona_view = nullptr;        // set: coronas for this camera (a free view), else the frame's game camera
    float render_aspect = 0;                    // the render's width / height (Hor+ widens the coronas' window test)
    // Sun corona and lens flare (water1, §21): CB3SunRenderer's sprites with the GPU sky test; off with `show_sun`
    // (--no-sun / G); `sun_occlusion` = false (--no-sun-test / O) draws them without the test (the tunnel glare)
    b3view::Sun sun;
    bool show_sun = true, sun_occlusion = true;
    // The post passes (post1, §25): the zoom blur CB3World::Render picks per tick (the directional one, or the boost
    // radial blur of water1 §21) and the bloom; on by default (--no-blur / N, --no-bloom / T). `post_game`: the GS's
    // buffer sizes and taps (--post game | pc, key Z; PC by default). `radial_undecided`: captures before post1 cannot
    // tell when UpdateCamera returned 0, so the radial blur is drawn there only with this (--blur-radial).
    bool show_blur = true, show_bloom = true, post_game = false, radial_undecided = false;
    // gsoff1 (§25.8): the radial blur's strength, a factor on its amount. 0.5 by default [L: the best match to the
    // reference GS frames of the original code (race1 49820 / 6000), while every GS register of the chain equals our
    // model (decomp [C]); the cause of the factor is not found]. 1 = the decomp's model (--radial-blur 1).
    float radial_scale = 0.5f;
    mutable double bloom_last = -1;             // the displayed time (in ticks) the bloom's kept picture was last advanced to
    b3gfx::TextureId blur_mask = b3gfx::kNone;
    // HUD / 2D stage (hud1, §17): the Gt2d draws of the frame's tick over the 3D picture
    int skip_water = 0;                         // diagnostics (--skip-water[=2c|30], live2 §20): 1 all, 2 STATIC +0x2C, 3 +0x30
    std::set<int> skip_units, skip_backdrops;   // diagnostics (--skip-units / --skip-backdrops, live2 §20): leave these out
    bool show_hud = true;                       // --no-hud / U
    bool hud_wide = true;                       // --hud-layout wide (default) | 43 / Y: edge anchoring at wide aspects (hud2)
    bool hud_sharp = true;                      // --hud-filter sharp (default) | game / J: crisp magnified font edges (hud2)
    // mac5: SmallFont's sharp alpha threshold (B3_SMALLFONT_LEVEL); menu-font: 0.3 -> 0.2 (strokes 1.4 -> 1.6 texels) and
    // a floor in output pixels (view.cpp)
    float small_font_level = 0.2f;
    static constexpr float kSmallFontMinStrokePx = 3.5f;
    // mac5 (remaster-features §3): the button prompts' device (prompts.h; set per frame by the live window from
    // NativePad and SETTINGS > BUTTON PROMPTS) and the key caps it shows; the game's button draws are redrawn from our
    // atlas (PlayStation: left as the game's)
    PromptDevice prompt_device = PromptDevice::kPlayStation;
    PromptKeys prompt_keys{};
    PromptAtlas prompt_atlas;
    b3gfx::TextureId prompt_tex = b3gfx::kNone;
    PromptDevice hud_built_prompt = PromptDevice::kPlayStation;
    PromptKeys hud_built_prompt_keys{};
    // mac5 (2026-11-10): the game's fonts (the live window sets them per frame from the newest tick), for the
    // title's "Press START button to continue": its START drawn as the device's Start button (prompts.h title_start_swap)
    std::shared_ptr<const HostFonts> hud_fonts, hud_built_fonts;
    std::map<std::uint64_t, b3gfx::TextureId> hud_textures;   // by HudTexture key (uploaded on first use)
    // Accessibility (a11y.h; PC SETTINGS > INTERFACE > ACCESSIBILITY): HUD COLOUR FILTER (0 off, 1 deuteranopia, 2
    // protanopia, 3 tritanopia) remaps the 2D's colours (hud_list, host_ui_list: vertex colours and remapped copies of
    // the textures; not the movies, not the globe); SCREEN FLASHES Reduced caps full-screen bright fills. Off / Full:
    // nothing of it runs (the frames byte-identical).
    int hud_colour_filter = 0;
    bool reduce_flashes = false;
    std::map<std::pair<std::uint64_t, int>, b3gfx::TextureId> hud_textures_filtered;   // (HudTexture key, filter)
    std::map<std::uint64_t, float> hud_texture_light;   // a HUD texture's mean colour (the brightest channel, 0..1)
    b3gfx::TextureId prompt_tex_filtered = b3gfx::kNone;
    int prompt_tex_filter = 0;   // the filter prompt_tex_filtered was made with (0: none yet / atlas rebuilt)
    int hud_built_filter = 0;
    bool hud_built_flashes = false;
    // textsize1: HUD SIZE (0 100 % .. 3 150 %, a11y.h hud_size_scale): the race HUD's components scaled about their
    // EHUDAlign anchors (hud_list, not the front end); 0: nothing of it runs
    int hud_size = 0, hud_built_size = 0;
    // carsel1 (PC SETTINGS > CAR SERIES ON L1 / R1, input_keys.h ShoulderSeries): the device's L1 / R1 glyphs beside the
    // car select's series arrows (prompts.h series_glyphs; drawn only); off: nothing of it runs
    bool series_glyphs = false, hud_built_series = false;
    std::uint64_t series_glyph_frames = 0;   // frames drawn with them (the log)
    int host_built_filter = 0;
    std::uint64_t flash_cuts = 0;   // draws capped so far (the log)
    std::uint64_t static_stills = 0;   // TV static frames drawn still (the log)
    a11y::FlashDraw hud_flash_draw(const HudFrame& hud, const HudDraw& d, const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                                   std::string* name = nullptr);
    b3gfx::BufferId hud_vb = b3gfx::kNone, hud_ib = b3gfx::kNone;
    std::shared_ptr<const HudFrame> hud_built;  // the frame the cached items below were built from
    float hud_aspect = 0;
    bool hud_built_wide = false, hud_built_sharp = false, hud_built_frontend = false;
    int hud_built_fe_layout = -1;
    bool hud_built_movie_fill = false;
    int hud_built_h = 0, hud_built_w = 0;
    std::vector<b3gfx::DrawItem> hud_items;
    // wide1: a host UI layer (hud.h HostUi: the PC settings page) over the 2D, in the 640 x 448 picture's 4:3 box
    // centred in the output (as the front end's layout); PNGs or HUD textures from the paths it names, alpha blended at
    // the game's 2D intensity (the present pass doubles it)
    bool show_host_ui = true;
    // mac5: the HUD textures a "hudtex:<raster hex>" host-UI path names (the PC SETTINGS page's font quads); null: none
    const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>* host_ui_hud_textures = nullptr;
    void host_ui_list(b3gfx::Device& dev, const std::shared_ptr<const HostUi>& ui, float aspect, std::vector<b3gfx::DrawItem>& out,
                      int out_h, int out_w);
    std::map<std::string, b3gfx::TextureId> host_ui_tex;   // by path ("" white); kNone: unreadable
    b3gfx::BufferId host_vb = b3gfx::kNone, host_ib = b3gfx::kNone;
    std::shared_ptr<const HostUi> host_built;
    float host_aspect = 0;
    int host_built_h = 0, host_built_w = 0;
    std::vector<b3gfx::DrawItem> host_items;
    // fe2 (§24): the World Tour globe (HudFrame::globes) among the 2D draws; off with --no-globe
    bool show_globe = true;
    void globe_items(const HudGlobe& g, const std::function<b3gfx::TextureId(std::uint64_t)>& tex, float fw, float fh, int out_w,
                     int out_h, int width, int height, std::vector<b3gfx::Vertex>& vs, std::vector<std::uint32_t>& is,
                     std::vector<b3gfx::DrawItem>& items) const;
    // hud4: markers projected per displayed frame (--hud-markers frame, default | tick: as captured; key K)
    bool hud_reproject = true;
    std::vector<int> hud_item_marker;           // per hud_items entry: its HudFrame::markers index, -1 none
    std::vector<int> hud_item_draw;             // splitlayout1: per hud_items entry: its HudFrame::draws index, -1 none
    std::vector<std::array<float, 16>> hud_item_model;   // per entry: its 2D -> clip matrix as built
    std::vector<std::uint32_t> hud_item_count;  // per entry: its index count as built
    struct MarkerPlace { bool shown = true; float dx = 0, dy = 0, r = 1; float x = 0, y = 0; };
    std::vector<MarkerPlace> hud_marker_place;  // of the last hud_list call (per HudFrame::markers entry)
    // mac5 (menu mouse, menumouse.h): the last hud_list build's draws as drawn, per draw (HudFrame::draws) its pieces (a
    // quad's 6 vertices, else a triangle's 3) in output fractions (0..1, y down) after the wide layout; none for a draw
    // not drawn. `hud_pieces_frame`: the frame they are of.
    std::vector<std::vector<ScreenBox>> hud_draw_pieces;
    std::shared_ptr<const HudFrame> hud_pieces_frame;

    // fe2 (docs/native-renderer.md §24): the showroom behind the front end's car. CB3World::RenderFrontEnd 0x275EF0 draws
    // the static track's backdrops (RenderAllBackdrops 0x21D2E0 over the STATIC +0x24 table: range 0 = [0, data +0x14),
    // 1 = [+0x14, +0x15), 2 = [+0x15, s16 +0x1C) [C asm]) and its ENVIRO.DAT sky; the static track the front end keeps
    // loaded is found by the tick's STATIC.DAT fingerprint (TRACKS/US/C5_V1 in every capture so far [C race2, menu]).
    // Kept apart from the race route so the live viewer's race segments keep theirs.
    struct Showroom {
        std::string route;                      // "" none; `tried` the fingerprint looked up (found or not)
        std::uint32_t size = 0;
        int units = 0;
        bool tried = false;
        b3view::Scene hi, sky;                  // the backdrops; ENVIRO.DAT's sky, sun, fog, specular
        std::vector<int> range[3];              // RenderAllBackdrops' three ranges: hi item indices, table order
        b3view::CarGlobals globals;             // texture slots 1-4 and the car env map from the showroom's ENVIRO.DAT
        b3view::Sun sun;
        Effects fx;                             // GLOBAL.TXD's blobbyshadow for RenderFrontEndShadow
        // the car's environment map with the showroom in it (bake_room_env): the game renders the sky and all three
        // backdrop ranges into its env-map cameras around the car (RenderFrontEnd, BuildCustomCameraData(idx))
        b3gfx::TextureId env = b3gfx::kNone;
        float env_centre[3] = {1e30f, 1e30f, 1e30f};
    };
    Showroom room;
    bool show_room = true;                      // --no-showroom: the car on black, as menu1 drew it
    bool room_reflection = true;                // --no-reflection: RenderFrontEnd's first Draw (the floor reflection) left out
    bool room_shadow = true;                    // --no-fe-shadow: RenderFrontEndShadow 0x303750 left out
    // The showroom for a static-track fingerprint (loaded on first use, kept while the fingerprint stays); null if none.
    Showroom* showroom(b3gfx::Device& dev, std::uint32_t size, int units);
    bool room_env = true;                       // --no-room-env: the car reflects the ENVIRO.DAT sky alone (as in a race)
    // The showroom's env map seen from `centre` (CB3GraphicsManager::BuildCustomCameraData's lEnvMapCentre: the car's
    // box middle in world space [L draft 0x223130]) in the env pass's dome mapping (car.frag env_sky), baked from six
    // offscreen views of the sky and the backdrops; rebaked when the centre moves by more than a centimetre.
    void bake_room_env(b3gfx::Device& dev, Showroom& sr, const float centre[3]);
    // RenderVehicle's draws for `vehicles` (the race path's car loop): LOD from `cam`, lit with `globals`; traffic without
    // full SH take the nearest of `lights`. hide_own (cam1, §26; the race view only): skip the view's own race car while
    // IsPlayerVisible is false (Vehicle::own_hidden).
    void vehicle_draws(b3gfx::Device& dev, const Camera& cam, const std::vector<Vehicle>& vehicles, const std::vector<Vehicle>& lights,
                       const std::vector<ModelHead>& heads, bool game_cull, const float eye[3], const b3view::CarGlobals& globals,
                       std::vector<b3gfx::DrawItem>& out, bool hide_own = false);
    std::vector<float> vehicle_fades;   // traffic-popin: the frame's traffic_fade per vehicle (the shadows')
    float frame_now = 0;   // traffic-popin: the displayed frame's world clock (Frame::fx.now; draw_list sets it) for traffic_fade
    // ghost1 (ghost.h; PC SETTINGS > INTERFACE > GHOST): the Time Attack ghost, drawn after the cars while `ghost` is set
    // and the frame is a Time Attack lap (Frame::time_attack, lap_index >= 0): the lap's pose at Frame::lap_time, its car
    // file `ghost_file` (index into cars), screen-door faded (ghost::kFade; less within a few metres of the camera), lit
    // with the player's car's light, no shadow, coronas or effects. `ghost_mode`: the setting (0 off, 1 best lap; the
    // live scene loads and records only when on).
    int ghost_mode = 0;
    const ghost::Lap* ghost = nullptr;
    int ghost_file = -1;
    void ghost_draws(b3gfx::Device& dev, const Frame& f, const float eye[3], std::vector<b3gfx::DrawItem>& out);

    // Releases every GPU resource and forgets the route and the car scenes.
    void clear(b3gfx::Device& dev);
    bool load_track(b3gfx::Device& dev, const std::string& route_dir, std::string* why);   // prepare_track + load_prepared
    // loadlag1: load_track in two halves, so that the files and the decode can run on a worker (LiveScene::preload)
    // while the render thread goes on drawing. prepare_track touches no device and no View state (any thread);
    // load_prepared builds the GPU scenes and the rest on the device's thread.
    static TrackPrep prepare_track(const std::string& route_dir, const std::string& game);
    bool load_prepared(b3gfx::Device& dev, TrackPrep&& p, std::string* why);
    int car_file(const ModelHead& head);
    b3view::CarModel* car_model(b3gfx::Device& dev, int file, int paint, int lod);
    // `eye`: the render camera (car lighting view direction, env pass); LOD and panel order use the game's camera.
    void draw_list(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads, bool game_cull,
                   const float eye[3], std::vector<b3gfx::DrawItem>& out);
    // CB3GameData::Draw's crash ramps and pickups for the frame (crash2), appended to `out`.
    void crash_draws(const Frame& f, std::vector<b3gfx::DrawItem>& out) const;
    // The animated materials' state for the frame, one per hi.model.track_anims entry: the captured state when the
    // capture holds it, else CB3StaticTrack::Update replayed from the race start on the clock (b3asset::track_anim_at).
    // `captured` (optional) tells which.
    std::vector<b3asset::TrackAnimState> anim_states(const Frame& f, bool* captured = nullptr) const;
    // Per-frame parameters of the car model (the env map's sun).
    void car_frame(b3gfx::FrameParams& fp) const;
    // The post passes of the frame (water1, §21): the sun corona and lens flare into fp.sun_* (for fp.eye / fp.view_proj,
    // set first) and the boost radial blur into fp.blur. `display_aspect`: the output's width / height as displayed (4/3
    // for the PS2-shaped 640 x 448 image).
    void post_frame(const Frame& f, float display_aspect, b3gfx::FrameParams& fp) const;
    void blur_frame(const Frame& f, b3gfx::FrameParams& fp) const;   // the zoom blur part of post_frame
    void bloom_frame(const Frame& f, b3gfx::FrameParams& fp) const;  // the bloom part of post_frame
    // The HUD frame's draws as 2D-stage items (b3gfx DrawItem::overlay) for an output of `aspect` = width / height:
    // the game's 2D space (the camera raster, 640 x 448 [C]) as the PS2 shows it, a 4:3 picture, scaled to the output
    // height and centred (pillarboxed at wider aspects, letterboxed below 4:3). Nothing when show_hud is off.
    // hud_wide at aspects above 4:3 (hud2, §17.11): the game's HUD rect widened to the output, as if the PS2 frame were
    // wider: each component's draws move with its EHUDAlign anchor ((2 fx - 1) x the extra half width; centre items
    // stay), and vertices on or past the 4:3 box's left / right edge go to the output's edge (full-width bars and
    // bands); absolute components (align 8: rival markers etc.) and draws outside a listed component only get the
    // latter. Captures without object events (before hud2) get the edge rule alone. hud3: an object's edge pieces and
    // the rest of it move together (its anchor: the side its one-sided textured edge pieces touch). out_w / out_h: the
    // output's pixels (the GS-to-GPU half-pixel correction is half an output pixel).
    // hud4: with `proj` (and hud_reproject), each world-anchored marker (HudFrame::markers) is moved from the place the
    // game projected it to with its captured viewport to its point's place in this frame's 3D: the anchor vehicle's
    // interpolated position + the captured offset, through proj->view_proj, and scaled by the game's size rule.
    void hud_list(b3gfx::Device& dev, const std::shared_ptr<const HudFrame>& hud, const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures,
                  float aspect, std::vector<b3gfx::DrawItem>& out, int out_h, int out_w, const HudProjection* proj = nullptr,
                  bool frontend = false);
    // gsoff1 (§29): a 2-player split-screen frame (Frame::split) for Device::draw_views: player 1's view (cam) in the top
    // half, player 2's (cam2) in the bottom half, each a frame of its own at its half's aspect (Hor+ as one player's
    // frame is: the game's views are 640 x 224), and the 2D over the whole output (both HUDs on one 640 x 448 space,
    // HudFrame::split; markers at their captured places). Player 2's view keeps its own effects, coronas and bloom
    // history (view2_state); split2 (§29.6): and its own post passes (View2In::post / blur), own-car hiding, shadows,
    // corona submits and traffic fade (Vehicle::*2).
    void split_frame(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads,
                     const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& hud_textures, bool game_cull, bool fog,
                     bool spec, int out_w, int out_h, float view_aspect, float hud_aspect,
                     std::vector<b3gfx::Device::ViewPass>& views, b3gfx::FrameParams& ofp, std::vector<b3gfx::DrawItem>& overlay);
    // (view_aspect: each view's width / height as rendered, 0 = the game camera's own (offline 640 x 448 shots);
    // hud_aspect: the output's width / height as displayed, as hud_list takes it)
    // splitlayout1 (splitlayout.h, PC SETTINGS > CAMERA > SPLIT SCREEN): 0 the game's top / bottom (the path above,
    // unchanged), 1 side by side: the views in the left / right halves, each projected at its own shape from the game's
    // side-by-side view window (Hor+), drawn with the renderer's own culling, and each player's 2D placed in its view
    // (split_hud); view_aspect is then not used.
    int split_layout = 0;
    // splitlayout1: `items` (hud_list's items of a split frame's 2D, laid out at 4:3 over the whole output) moved into
    // their views: each draw by its anchor (split_hud_affine), markers projected through their view's camera
    void split_hud(const HudFrame& hud, const Frame& f, const std::vector<b3gfx::Device::ViewPass>& views, const float half_w[2],
                   int out_w, int out_h, std::vector<b3gfx::DrawItem>& items) const;
    struct View2State { Effects effects; Coronas coronas; double bloom_last = -1; EffectStats fx_stats; CoronaArrays corona_arrays; CoronaStats corona_stats; };
    View2State view2_state;
    void swap_view2() {
        std::swap(effects, view2_state.effects);
        std::swap(coronas, view2_state.coronas);
        std::swap(bloom_last, view2_state.bloom_last);
        std::swap(fx_stats, view2_state.fx_stats);
        std::swap(corona_arrays, view2_state.corona_arrays);
        std::swap(corona_stats, view2_state.corona_stats);
    }
    // wide1: `frontend` (front-end frames, frontend_frame) lays the 2D out as the game's 4:3 picture centred, with no
    // object anchors: only draws spanning the whole width (bands, panels, the grid) reach the output's edges (a
    // repeating texture keeps its texel scale), an untextured fill's vertices on an edge go to that edge, movies are
    // never stretched, and text keeps the 4:3 clip (tickers and the selection's sweeping ghost text end there as on
    // the PS2). Everything else stays where the 4:3 layout puts it.
    // menu1 (docs/native-renderer.md §22): a front-end frame (no route): its parameters and draw list. The car pages'
    // car (Frame::fe_car: CB3World::RenderFrontEnd's race car, with the frame's camera, viewport 1) when the frame has
    // one, with fe2's showroom around it (§24: the backdrops, sky, floor reflection, RenderFrontEndShadow, the garage env
    // map), then the 2D (menus, movies and the globe through hud_list). `aspect` = width / height of the output.
    void frontend_frame(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads,
                        const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, float aspect, int out_w, int out_h,
                        b3gfx::FrameParams& fp, std::vector<b3gfx::DrawItem>& out);
    // fe3 (§30): the side fill after the backdrop draw `d` (its item `base`), into the HUD's vertex / index lists
    void side_fill(b3gfx::Device& dev, const HudFrame& hud, const HudDraw& d, const float box[4], const b3gfx::DrawItem& base,
                   const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures, float right, float ext,
                   std::vector<b3gfx::Vertex>& vs, std::vector<std::uint32_t>& is);
    bool frontend_globals = false;              // car_globals loaded for front-end frames
    // front-end frames' 2D layout above 4:3: 2 panels (fe3, §30: per element class), 1 the centred 4:3 picture (wide1,
    // `--fe-layout 43`), 0 hud2's edge rules (`--fe-layout edges`)
    int fe_layout = 2;
    // mac5 (Mac 2026-11-08: load the whole map, so draw distance is no concern): every unit
    // of the track at its high detail, not only the 17 the PS2 streams in (--track-units resident: the PS2's set)
    bool whole_track = true;
    // the whole track's units with their bounds (built once per track: hi_unit / hi_inst items, world-space AABB), culled
    // against the game camera each frame
    struct UnitBox { float lo[3], hi[3]; std::vector<int> items; };
    std::vector<UnitBox> track_boxes;
    void build_track_boxes();
    bool movie_fill = true;                     // fe3: the backdrop's blurred side fill (--movie-fill blur); false: black
    // fe3: blurred copies for the side fill: per guest movie picture buffer (what it was made from), per still texture
    std::map<std::uint32_t, std::pair<std::shared_ptr<const HudPicture>, b3gfx::TextureId>> fill_movie;
    std::map<std::uint64_t, b3gfx::TextureId> fill_still;
    std::map<std::pair<std::uint64_t, int>, b3gfx::TextureId> fill_still_filtered;   // accessibility: (texture, HUD COLOUR FILTER)
    std::map<std::uint64_t, float> roughness_cache;   // fe3: hud_texture_roughness per (texture, sampled range)
    bool show_frontend_car = true;              // --no-fe-car
    // menu1: movie pictures (HudVideo::picture) as dynamic textures per guest picture buffer; the placeholder for movie
    // tiles without a captured picture. show_movies false (key M, --no-movies): the placeholder always
    bool show_movies = true;
    std::map<std::uint32_t, b3gfx::TextureId> movie_tex;
    std::map<std::uint32_t, std::shared_ptr<const HudPicture>> movie_up;   // what each holds now
    b3gfx::TextureId movie_placeholder = b3gfx::kNone;
};

// menu1 (§22): FrameParams for a front-end frame: no 3D, cleared black, the game's present blit (2D drawn into the
// half-intensity frame, doubled on present, as in a race).
void frontend_params(b3gfx::FrameParams& fp);

// Crash-mode stage of a capture without the game data block (version 1/2 before crash2) [L]: the stage whose start
// positions hold the segment's race cars at its first tick (each within 1 m of a different start point, the player's
// car included); -1 if no stage or more than one fits (e.g. US/C3_V1 stages 7-9 share their start points).
int start_stage(const std::vector<b3asset::GameStage>& stages, const Tick& first, int* fits = nullptr);
// The stage index for the directory id a crash2 capture names (CB3GameData +0x558), -1 if none.
int stage_by_id(const std::vector<b3asset::GameStage>& stages, std::uint64_t id);
// GtMath's CGtQuat::ConvertToMatrix right row (include/b3/GtMathPs2Quat.h of the decomp, [C] decode of 0x446C9C):
// q x sqrt(2), then (1 - yy - zz, xy + wz, xz - wy).
void quat_right(const float q[4], float out[3]);

// The game camera as a reverse-Z view-projection (eye = pos, forward = at, up = up; Hor+ from `aspect`).
void game_view_proj(const Camera& c, float aspect, float out[16]);

}  // namespace b3replay

#endif
