// b3replay view: the draw path shared by the offline replay (b3replay <capture>) and the live renderer (b3replay --live,
// docs/native-renderer.md §13): asset matching (route by STATIC.DAT fingerprint + camera score, cars by header bytes),
// the route's scenes (high / LOD units, instances, backdrops, props, ENVIRO.DAT sky), per-frame interpolation between
// 60 Hz ticks, and the per-frame draw list. Moved from main.cpp (render4) without changing the draw itself.
#ifndef B3REPLAY_VIEW_H
#define B3REPLAY_VIEW_H

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
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
#include <unordered_map>
#include <vector>

#include "a11y.h"
#include "xboxglow.h"   // xboxglow: tunnel_lights_env
#include "b3asset/carpaint.h"
#include "b3report/sysinfo.h"   // hdcache: b3port::ProcessMemory (the HD prepare's log)
#include "b3asset/crash.h"
#include "b3gfx/b3gfx.h"
#include "capture.h"
#include "coronas.h"
#include "rain.h"     // weather: the falling rain
#include "effects.h"
#include "menumouse.h"
#include "prompts.h"
#include "hudmods.h"   // hudmods: the race HUD's opt-in additions (RPM METER, BOOST BAR SHAKE)
#include "trackrays.h"   // orbitcam2
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

// A car file the renderer can draw. carpacks: an extra car's (the player's car folders: up to 200) is read header-only
// (its first 0x100 bytes: match_car's 0x70) until it is drawn; full() reads the whole file then (every use past the
// header goes through it)
struct CarFile {
    std::string path;
    std::vector<std::uint8_t> bytes;
    bool partial = false;
    std::vector<std::uint8_t>& full();
};
std::vector<CarFile> scan_cars(const std::string& game);
}  // namespace b3replay
namespace b3native { struct ExtraScan; }
namespace b3replay {
// customseries: the extra cars folders as the game reads them (tools/native/extra_series.h extra_scan_all)
b3native::ExtraScan extra_cars_now();
// Car files whose header bytes 0x00..0x4C equal the captured vehicle data's and whose later header words do too, relocated
// by one load offset (carmatch.h: the taxis share the first 0x4C); -1 if none (`candidates`: how many).
int match_car(const std::vector<CarFile>& cars, const ModelHead& head, int* candidates);

// ---- interpolation --------------------------------------------------------------------------------------------------

Mat34 lerp(const Mat34& a, const Mat34& b, float t);       // rotation by slerp, row lengths and translation linearly; t in (1, 3] extrapolates (perf1)
Vehicle lerp(const Vehicle& a, const Vehicle& b, float t);

// What one displayed frame shows: the tick's track state, its vehicles and camera, blended towards the next tick.
struct Frame {
    Camera tick_cam;             // fx2: the tick's game camera as captured (not interpolated)
    // orbitcam1 (orbitcam.h): how far each view (0: cam, 1: cam2) is turned from the game's camera, 0 (the game's) .. 1
    // (kOrbitMarkerFade or more): the game's world-anchored markers drawn where the game projected them shrink away by it
    float orbit_away[2] = {0.0f, 0.0f};
    std::size_t tick = 0;
    float alpha = 0;
    std::uint8_t race_time = 0;  // todrace (Tick::race_time.race): 0 the TIME OF DAY row's, 1 day, 2 night; weather: 3 rain, 4 night rain (drawn only)
    bool mirror = false;         // gameopts (Tick::race_time.game_options bit 0): MIRROR MODE's race, the 3D drawn mirrored
    Camera cam;
    std::vector<Vehicle> vehicles;
    int slot_unit[17], slot_kind[17];
    std::uint16_t vis[17] = {};
    std::int8_t backdrops[8];
    int env_unit[17] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // reflect-tunnel: Tick::env_unit / env_car / env_track
    std::uint32_t env_car = 0;
    int env_track = -1;
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
    std::shared_ptr<const HostCarousel> carousel;   // paint3: the carousel slots whose model is not their picture's (Tick::carousel)
    std::uint32_t fe_car = 0;            // menu1 (§22): the front end's car is drawn (Tick::fe_car)
    int fe_player = -1;                  // padpaint: whose car it is (Tick::fe_player)
    int fe_viewer = -1;                  // gamemods1: the CAR VIEWER's car and background colour (Tick::fe_viewer), -1 not
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
// camfixes: the chased car of camera `c` among `vehicles` as View::apply_fov takes it: `car_id` (0: any race car, the
// choice before camfixes); its id (0: none), `best` its distance along the view less a share of its radius, `best_rear`
// its rear's distance (view.cpp)
std::uint32_t chase_target(const Camera& c, const std::vector<Vehicle>& vehicles, std::uint32_t car_id, float& best, float& best_rear);

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

// reflect-tunnel: the car env map's track part (docs/car-shading.md §9) on by default; $B3_CAR_ENV = sky draws the
// map as before (the ENVIRO.DAT sky alone: the golden record's reference look, tools/golden/baseline.sh), = world forces
// it on whatever the settings say (apply_view: off with SPECULAR off, i.e. the LOW preset). -1: no override.
inline int car_env_override() {
    const char* e = std::getenv("B3_CAR_ENV");
    return !e ? -1 : !std::strcmp(e, "sky") ? 0 : !std::strcmp(e, "world") ? 1 : -1;
}

// bloom2: the bloom's level, 1 Soft (the default of the PC SETTINGS) or 2 Standard (the game's own strength, drawn the way its GS
// does); $B3_BLOOM_LEVEL = soft | standard overrides the settings' value (the golden record's reference look, tools/golden/baseline.sh).
// A View starts at Standard: the offline tools (b3replay --offscreen) draw the game's own bloom unless a level is asked for.
inline int bloom_level_env(int fallback) {
    const char* e = std::getenv("B3_BLOOM_LEVEL");
    return !e ? fallback : !std::strcmp(e, "soft") ? 1 : !std::strcmp(e, "standard") ? 2 : fallback;
}

// carpaint (docs/remaster-features.md §17, b3asset/carpaint.h): a local player's own paint, drawn only. `custom`: the
// paint drawn in `rgb` (0xRRGGBB) instead of the game's paint; `second`: the livery's second colour in `rgb2` (else a
// shade of `rgb`); `decals` false: logos, numbers and second colours drawn over in the paint. The game's paint index
// is never touched. Off (the default) draws the cars exactly as before.
// padpaint: `finish` 0 gloss (the game's), 1 metallic, 2 pearl, 3 chrome (options3) (car.frag, paint mode bits 2 / 3; chrome both).
struct CarPaint {
    bool custom = false;
    std::uint32_t rgb = 0xC31E1E, rgb2 = 0x202020;
    bool second = false;
    bool decals = true;
    int finish = 0;
    // paint2: the other layers (0xRRGGBB as on screen), each used only while its switch is on: the decals' colour (the
    // logos, numbers and stripes), the wheels' (the rims; the tyres stay dark), the boost glow's (coronas.h)
    bool decal_custom = false, wheel_custom = false, flame_custom = false;
    std::uint32_t decal_rgb = 0xF0F0F0, wheel_rgb = 0xA5A5B4, flame_rgb = 0xFF7800;
    bool on() const { return custom || !decals || finish != 0 || decal_custom || wheel_custom; }
    bool operator==(const CarPaint&) const = default;
};
// The environment's paint for player `p` (0 / 1), over `base`: $B3_PAINT = RRGGBB (player 1; $B3_PAINT_P2 player 2) a
// custom colour, `game` the game's; $B3_PAINT_SECOND = RRGGBB the second colour; $B3_DECALS = 0 decals off, 1 on;
// $B3_PAINT_FINISH = gloss / metallic / pearl (both players).
CarPaint car_paint_env(int p, CarPaint base = {});
// Night mode (remaster-features §17; drawn only, off by default): GRAPHICS > TIME OF DAY: Night (pcsettings apply_view),
// or $B3_NIGHT=1 (wins over the setting; 0 forces day). The race's frame is graded after the fog (desaturated, a cool
// tint, a curve; b3gfx's night.frag), the sky replaced by a night sky (a gradient, the original sky dimmed, moonlit
// clouds, stars, the moon with its halo at the ENVIRO sun's bearing), each car throws a headlight beam, the race cars'
// and traffic's head and tail lights (their coronas) are lit and stay bright, so the bloom makes them glow. The moon
// (MOON, $B3_MOON=0/1) lights the scene: the cars' key light and gloss, the track's faces and relief turned to it.
// Tuning (each "r,g,b" or numbers): $B3_NIGHT_WORLD (the world's multiplier), $B3_NIGHT_SKY (the original sky's),
// $B3_NIGHT_LAMP (the headlights' colour), $B3_NIGHT_BEAM = "tan of the half angle, range m, pool gain, throw gain",
// $B3_NIGHT_GRADE = "saturation, gamma", $B3_NIGHT_MOON = "r,g,b" (the moonlight), $B3_NIGHT_STARS. Nothing runs while off.
struct NightLook {
    bool on = false;
    bool moon = true;                                // MOON: the disc, its halo and its light
    float world[3] = {0.31f, 0.36f, 0.55f};          // the world's multiplier (after the desaturation)
    float sat = 0.42f, gamma = 1.18f, pivot = 0.5f;  // the grade: the saturation kept, the curve pow(c / pivot, gamma) x pivot
    float sky[3] = {0.035f, 0.045f, 0.09f};          // the original sky's share in the night sky
    float zenith[3] = {0.004f, 0.007f, 0.022f};      // the night sky's gradient (frame units)
    float horizon[3] = {0.026f, 0.036f, 0.066f};
    float stars = 0.5f;
    float moon_disc[3] = {0.72f, 0.73f, 0.76f};       // the disc (frame units: bright enough for the bloom)
    float moon_size = 0.021f;                        // its angular radius (rad)
    float halo = 0.24f;
    float cloud = 0.6f;                              // the clouds lit by the moon
    float moonlight[3] = {0.55f, 0.66f, 0.88f};      // the moon's light: silver-blue (frame units)
    float car[3] = {0.95f, 0.45f, 0.8f};             // cars: SH scale, env scale, moon key gain
    float facing = 0.55f;                            // track: moonlit faces
    float flat = 0.5f;                               // track: the baked daylight's contrast flattened (nightfix; $B3_NIGHT_FLAT)
    // the street lamps' coronas (nightfix, View::night_lamp_coronas): colour gain (GS units on the lamp's tint), half
    // size (m), the least half size as a share of the view's half width ($B3_NIGHT_LAMP_CORONA; gain 0: none)
    float lamp_corona[3] = {230.0f, 2.2f, 0.012f};
    float relief = 1.6f;                             // the track's relief by the moon when SURFACE DETAIL is off
    float lamp[3] = {1.0f, 0.93f, 0.80f};
    float shape[4] = {0.26f, 80.0f, 2.0f, 1.6f};     // light.frag: tan of the half angle, range, pool gain, throw gain
    // nightlights (§17.2): the projected beam (beam.frag): the spill's inner / outer half angle (deg), falloff d0 (m),
    // the spill's gain, fade start / end (m), dip (deg), lamp height above the ground (m), the hot core's inner / outer
    // half angle (deg) and gain; $B3_NIGHT_SPOT sets them in this order
    float spot[11] = {12.0f, 34.0f, 16.0f, 2.2f, 45.0f, 75.0f, 2.0f, 0.6f, 3.0f, 9.0f, 5.0f};   // 1.3.0: spill to 34 deg (owner: more spread)
    // how many cars get a projected beam: 8 (HIGH, ULTRA), 4 (MEDIUM), 0 = the ground quads (LOW, PS2 ORIGINAL);
    // pcsettings from the preset, $B3_NIGHT_LIGHTS wins
    int lights = 8;
    // tunnels: enclosed = smoothstep(0.6, 0.9, the roof rays' cover) x smoothstep(0.75, 0.95, the 60-degree ring's), the
    // rays cast while sky_open < 0.6 (nightfix, §35.1: sky_open jumps 0..0.37 inside a tunnel, so it only gates; lo and
    // hi are kept for the variable's layout and unused), smoothed over `tau` s; the grade loses fade x enclosed of its
    // strength within near m, full again at far m ($B3_NIGHT_TUNNEL = "lo, hi, tau, fade, near, far"; fade 0: off)
    float tunnel[6] = {0.15f, 0.35f, 0.5f, 0.8f, 60.0f, 140.0f};
    // the tunnels' light fixtures (DrawItem::night_emissive): gain over the pre-grade colour, texel luminance ramp
    // ($B3_NIGHT_FIXTURE; $B3_NIGHT_FIXTURES = "name,name,..." replaces the route's name list, to find the names)
    float fixture[3] = {1.0f, 0.6f, 0.8f};
    // street lights (§17.2): how many lamps light the road (32 HIGH / ULTRA, 8 MEDIUM, 0 LOW / PS2: from `lights`);
    // $B3_NIGHT_STREET = the count, $B3_NIGHT_STREET_SHAPE = "inner deg, outer deg, d0 m, gain, fade start m, fade end m, reference height m" (nightfix: d0
    // and the fade scale with the lamp's height over the reference, 0.75..2)
    int street = -1;                                  // -1: from `lights`
    float street_shape[7] = {45.0f, 72.0f, 7.0f, 4.0f, 14.0f, 24.0f, 12.0f};   // (gain 2.2 -> 4.0: round 3, the pools were too faint)
};
// weather (remaster-features §38): the rain's look. By day the night grade's pass with day values (b3gfx Night::lit
// false): the world greyer and a little darker, its baked sun stripes softened, the sky an overcast grey over a ghost of
// the original (its clouds and backdrop), no sun, sun shadows or lens flare, a nearer grey fog, the cars' env map dimmer.
// At night: the night's grade without stars or moon (behind the clouds), a dull sky a little lighter (the town's light
// on the clouds), the moonlight weak. Frame units (the present doubles them).
struct WeatherLook {
    float world[3] = {0.80f, 0.82f, 0.86f};
    float sat = 0.62f;
    float sky[3] = {0.30f, 0.31f, 0.33f};            // the original sky's share
    float zenith[3] = {0.19f, 0.20f, 0.22f};         // the overcast overhead
    float horizon[3] = {0.27f, 0.28f, 0.30f};        // and at the horizon (the fog's colour)
    float car[3] = {0.85f, 0.55f, 0.0f};             // the cars: SH scale, env scale, no key light (no sun)
    float flat = 0.35f;                              // the baked sun's contrast flattened (Night::flat)
    float sky_grey = 0.75f;                          // by day: the sky dome and backdrops this far to their own grey (DrawItem::overcast)
    float wet = 1.0f;                                // the wet road (FrameParams::Surface::wet)
    float reflect[3] = {0.28f, 0.29f, 0.31f};        // by day: the wet road's sky (the full-screen specular's colour)
    float night_reflect[3] = {0.05f, 0.05f, 0.055f}; // at night: the town's light on the clouds, mirrored
    float fog_near = 1.8f;                           // the fog as at this times the distance
    float fog_max = 0.8f;                            // at least this much fog far away
    float night_sky[3] = {0.040f, 0.038f, 0.042f};   // night rain: the clouds lit by the town
    float night_horizon[3] = {0.060f, 0.055f, 0.055f};
    float night_moonlight = 0.35f;                   // the moonlight's share
};
inline bool night_env_set() { const char* e = std::getenv("B3_NIGHT"); return e && *e; }
// weather (remaster-features §38): $B3_WEATHER = rain / 1 (on) or dry / 0 (off) wins over the WEATHER row
inline bool weather_env_set() { const char* e = std::getenv("B3_WEATHER"); return e && *e; }
inline bool weather_env_rain() { const char* e = std::getenv("B3_WEATHER"); return e && (*e == '1' || *e == 'r'); }
inline NightLook night_env() {
    NightLook n;
    const char* e = std::getenv("B3_NIGHT");
    n.on = e && *e && std::strcmp(e, "0") != 0;
    if (const char* m = std::getenv("B3_MOON"); m && *m) n.moon = std::strcmp(m, "0") != 0;
    const auto read = [](const char* name, float* v, int count) {
        const char* s = std::getenv(name);
        for (int k = 0; s && *s && k < count; ++k) {
            char* end = nullptr;
            const float x = std::strtof(s, &end);
            if (end == s) break;
            v[k] = x;
            s = *end == ',' ? end + 1 : end;
        }
    };
    read("B3_NIGHT_WORLD", n.world, 3);
    read("B3_NIGHT_SKY", n.sky, 3);
    read("B3_NIGHT_LAMP", n.lamp, 3);
    read("B3_NIGHT_BEAM", n.shape, 4);
    float g[2] = {n.sat, n.gamma};
    read("B3_NIGHT_GRADE", g, 2);
    n.sat = g[0];
    n.gamma = g[1] >= 0.1f ? g[1] : 0.1f;   // at least 0.1, NaN too (the curve and the fog colour's inverse divide by it)
    read("B3_NIGHT_MOON", n.moonlight, 3);
    read("B3_NIGHT_STARS", &n.stars, 1);
    read("B3_NIGHT_SPOT", n.spot, 11);
    read("B3_NIGHT_TUNNEL", n.tunnel, 6);
    read("B3_NIGHT_FIXTURE", n.fixture, 3);
    if (const char* l = std::getenv("B3_NIGHT_STREET"); l && *l) n.street = std::clamp(std::atoi(l), 0, 32);
    read("B3_NIGHT_STREET_SHAPE", n.street_shape, 7);
    read("B3_NIGHT_FLAT", &n.flat, 1);
    read("B3_NIGHT_LAMP_CORONA", n.lamp_corona, 3);
    if (const char* l = std::getenv("B3_NIGHT_LIGHTS"); l && *l) n.lights = std::clamp(std::atoi(l), 0, 8);
    return n;
}
// The moon's direction for a route whose ENVIRO sun is `to_sun`: the sun's bearing (the baked shadows then fall away
// from the moon), its height kept between 10 and 20 degrees so the moon shows in the chase camera's view.
void moon_direction(const float to_sun[3], float out[3]);

struct View {
    NightLook night = night_env();              // night mode (remaster-features §17), set in post_frame / draw_list
    // todrace: whether frame `f` is drawn at night: the race's own choice on a track page (Frame::race_time,
    // tools/native/race_time.h: 1 day, 2 night), else night.on (TIME OF DAY, $B3_NIGHT). Drawn only.
    bool night_on(const Frame& f) const { return f.race_time ? f.race_time == 2 || f.race_time == 4 : night.on; }
    // weather (§38): whether frame `f` is drawn in the rain: the race's CONDITIONS (3 rain, 4 night rain; 1 / 2 dry), else
    // WEATHER (`rain`; held back: $B3_WEATHER only). Drawn only.
    bool rain = weather_env_rain();
    WeatherLook weather_look;
    void rain_frame(const Frame& f, float aspect, b3gfx::FrameParams& fp) const;   // the rain's grade (post_frame)
    void rain_sky_tint(const Frame& f, b3gfx::DrawItem& it) const;   // the rain's sky (draw_list)
    float follow_speed(const Frame& f, float dir[3]) const;          // the followed race car's speed (its wheels) and heading
    void rain_audio_frame(const Frame& f, const b3gfx::FrameParams& fp) const;   // the rain's sound (NativeAudio)
    float rain_audio_volume() const;   // rainfe: RAIN VOLUME as the sound takes it ($B3_RAIN_VOLUME wins)
    float rain_volume = 1.0f;   // RAIN VOLUME (pcsettings: held back with weather, so 100 %), x the game's EFFECTS volume
    mutable float rain_clock_now = -1.0f;                            // (the race's clock seen last, and when: the pause)
    // weather (§38): the camera under a roof (a bridge, an overpass, a tunnel): 0..1, smoothed (~0.3 s in, ~0.5 s out, so
    // a bridge's shadow or a lattice does not flicker it); per view (split screen swaps it)
    struct RainCover { float v = 0; float now = 0; bool valid = false; };
    mutable RainCover rain_cover;
    float rain_cover_frame(const Frame& f) const;
    mutable float rain_log_last[3] = {-1.0f, -1.0f, -1.0f};   // (B3_RAIN_AUDIO_LOG: the last printed on / cover / enclosed)
    mutable std::chrono::steady_clock::time_point rain_clock_wall{};
    void lens_drops_frame(const Frame& f, b3gfx::FrameParams& fp, float enclosed, float aspect) const;   // the lens drops (rain_frame)
    bool lens_drops_on = [] { const char* e = std::getenv("B3_RAIN_DROPS"); return !(e && *e == '0'); }();   // $B3_RAIN_DROPS=0: none
    bool rain_on(const Frame& f) const { return f.race_time ? f.race_time >= 3 : rain; }
    // taillights: frame f's brake levels (Vehicle::brake) from the timeline it was built from (frame_at(tl, i, frac)),
    // only where the lights are on (night, rain); called with apply_fov after frame_at where a race is drawn
    void frame_lights(const Timeline& tl, std::size_t i, double frac, Frame& f) const;
    bool night_at_load() const { return load_race_time ? load_race_time == 2 || load_race_time == 4 : night.on; }
    bool rain_at_load() const { return load_race_time ? load_race_time >= 3 : rain; }
    // the falling rain (rain.h; per view: split screen swaps it with player 2's) and its roofs (rain_sheltered)
    mutable Rain rain_layer;
    RainLook rain_look;
    mutable std::unordered_map<std::uint64_t, bool> rain_roof;
    bool rain_sheltered(const float p[3]) const;
    bool rain_dry_built = false;
    void rain_dry_build();   // DrawItem::dry per track draw (at the load in the rain, else on the first rain frame)
    // the lights' streaks on the wet road (rain.h wet_reflection; per view) and the ground under a light (rain_ground)
    mutable QuadLayer wet_layer;
    mutable Spray spray_layer;   // the spray from the wheels (rain.h; per view)
    // draw_list's quads of a frame (the lamps' heads, the light streaks, the rain, the spray): kept, so their capacity is
    // reused (no allocation per frame); the layers build them afresh each frame
    std::vector<CoronaQuad> scratch_lamps, scratch_wet, scratch_rain, scratch_mist;
    SprayLook spray_look;
    mutable std::unordered_map<std::uint64_t, float> rain_floor;
    bool rain_ground(const float p[3], float& y) const;
    std::uint8_t load_race_time = 0;   // todrace: the race's choice when its route loads (the tunnel cover grid is built then)
    // surface (remaster-features §18): GRAPHICS > SURFACE DETAIL / AMBIENT OCCLUSION (pcsettings); $B3_SURFACE* win
    bool surface_detail = false, ambient_occlusion = false;
    // SUN SHADOWS (remaster-features §20): GRAPHICS > SUN SHADOWS (pcsettings: the map's side and kernel from ANTI-ALIASING);
    // $B3_SUN_SHADOWS=0 / 1 wins over the row, $B3_SUN_SHADOW_SIZE / _FILTER / _STRENGTH (tuning) over the rest
    bool sun_shadows = false;
    bool prop_shadows = false;   // propshadow1 (PROP SHADOWS): props and instances cast too, while sun_shadows is on
    int sun_shadow_size = 2048, sun_shadow_filter = 0;
    float road_light = 0.75f;   // the route's open-road vertex light (load_prepared: road_light_level), FrameParams::SunShadow::lit
    // The frame's shadow box (FrameParams::SunShadow without the strength) towards `to_light`, around the view's car
    // (Frame::env_car, else the race car the camera follows); false: none (off, the light under 5 degrees, no car)
    bool sun_shadow_box(const Frame& f, const float to_light[3], b3gfx::FrameParams::SunShadow& out) const;
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
    // 1.1 (drawn only): the chase camera's distance to the chased car x chase_distance and its eye raised chase_height
    // (world units, metres) along the camera's up, then turned down about its right axis so the point it looked at
    // stays in the centre; only views with a chased car (apply_fov's test), so in-car, bumper and replay cameras stay;
    // sigcam (1.2): and only the game's chase cameras (Camera::chase), so its takedown and crash cameras stay too
    float chase_distance = 1.0f;
    float chase_height = 0.0f;
    // camfixes (2026-11-22): the chased car is the view's own player car (`car_id`; view 0: kPlayerRaceCar), never
    // another car that happens to be ahead (a car passing between the camera and the player's, the car behind in a
    // look-back view: the offsets then jumped to it, or pushed the eye into the player's own car)
    void apply_fov(Camera& c, const std::vector<Vehicle>& vehicles, std::uint32_t car_id = kPlayerRaceCar) const;
    // camfixes2: the game's time of the frame apply_fov is given next, in polls (fractional between ticks; the live
    // view sets it). With it the offsets ease in and out over kChaseEase polls when the player's car starts or stops
    // being chased (a takedown camera's blend, a swinging camera turning off the car, back from a look-back), instead
    // of a step; a camera cut (the view turning faster than 30 degrees per poll) drops them at once (an old offset is
    // never put on another camera); a jump in time (the first frame, a seek) takes the frame's own weight (an isolated
    // shot matches the running view). Negative (the replay tools): no easing, as before.
    mutable double chase_clock = -1.0;
    static constexpr double kChaseEase = 10.0;
    struct ChaseEase {
        float w = 0.0f;                  // the offsets' weight now (0..1)
        double t = -1.0;                 // chase_clock at the last frame
        float at[3] = {};                // the view axis at the last frame
        float move = 0.0f, back = 0.0f, up = 0.0f, d = 0.0f;   // the full offsets last applied (w 1), the aim's distance
    };
    mutable ChaseEase chase_ease;
    std::vector<CarFile> cars;                  // scan_cars(game), once
    std::map<ModelHead, int> head_file;         // model header -> index into cars (-1 unmatched)
    std::set<std::uint64_t> payload_logged;     // payloads1: vehicles (x2, + released) whose payload draw was logged once
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
    // carpaint: the local players' paint ([0] player 1 and the front end's car, [1] player 2), and per car file its paint
    // map and the map's texture (made once, when a player's car first needs it; ok false: the car has one paint)
    CarPaint car_paint[2] = {car_paint_env(0), car_paint_env(1)};
    // padpaint: the map is made on a worker (async_paint: the live window; the car is drawn in the game's paint until it
    // is ready, no frame waits ~30 ms for it) or at once (the offline tools); kept across routes, only its texture made again
    struct PaintMap {
        bool ok = false, pending = false;
        b3asset::CarPaintMap map;
        b3gfx::TextureId tex = b3gfx::kNone;
        std::future<std::pair<bool, std::string>> job;   // pending: fills `map`; (made, why not)
        double ms = 0;
    };
    std::map<int, PaintMap> paint_maps;
    bool async_paint = false;
    bool paint_frontend = false;                // frontend_frame: the front end's car is being drawn
    // padpaint: that car (Tick::fe_car) and whose paint it takes: the car select page's player (Tick::fe_player; any other
    // page player 1's); every other race car on a front-end page keeps the game's paint
    std::uint32_t paint_fe_car = 0;
    int paint_fe_player = 0, paint_fe_logged = -2;
    const PaintMap* paint_map(b3gfx::Device& dev, int file);
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
    // paint2 / paint3: the car pages' thumbnails in the player's paint (drawn from the cars: render_car_picture), by (HudTexture key, the paint's
    // signature); hud_built_thumb: the signature the built items used (0: the game's thumbnails)
    // paint3: a car picture on its way (car_picture_begin: drawn on black and on white; car_picture_take)
    struct PictureJob {
        int ids[2] = {0, 0};          // Device::picture_begin's, 0: taken (or none)
        std::vector<std::uint8_t> px[2];
        bool failed = false;
        int w = 0, h = 0, file = -1;
        std::uint64_t begun_tick = 0; // hud_tick when begun (thumb_job_expired: given up)
        bool on() const { return ids[0] != 0 || ids[1] != 0; }
    };
    struct ThumbSlot {   // one texture per thumbnail, refilled
        std::uint64_t sig = 0;        // the paint its picture has (0: none yet)
        b3gfx::TextureId id = b3gfx::kNone;
        std::uint64_t drawn_tick = 0; // when its last picture was begun (hud_tick)
        PictureJob job;               // the picture on its way, for job_sig
        std::uint64_t job_sig = 0;
        std::uint64_t failed_sig = 0; // a picture for this paint was given up: not begun again, the game's picture stays
    };
    // paint3: a thumbnail's car (cars index) by its name, and each car's framing fitted to the game's picture
    std::map<std::string, int> thumb_file_of;
    struct ThumbFit { bool done = false; float s = 1.0f, ox = 0.0f, oy = 0.0f; PictureJob job; };
    static constexpr std::uint64_t kThumbRefreshTicks = 3;   // paint3: a stale thumbnail's picture at most every 0.1 s (30 polls a second)
    std::uint64_t hud_tick = 0;      // paint3: the frame's tick (Frame::tick; set before hud_list)
    bool thumb_wait = false;         // paint3: every thumbnail drawn and waited for in its build (deterministic shots: all but the
                                     // live window, !async_paint)
    int thumb_budget = 0;            // paint3: pictures still to begin in this build (1 a build; thumb_wait: no limit)
    bool thumb_busy = false;         // paint3: a thumbnail waits for its picture: the HUD built again next frame
    std::set<int> thumb_files_used;  // paint3: the cars this build's thumbnails are drawn from
    std::vector<PictureJob> thumb_orphans;   // paint3: jobs of thumbnails no longer drawn, taken when done
    // paint3: car models loaded only for thumbnails (car_model called from car_picture_begin, never for the scene): given
    // back once no thumbnail of theirs is drawn or on its way
    std::set<std::tuple<int, int, int>> thumb_only_models;
    bool thumb_model_ = false;
    double thumb_build_ms = 0;       // paint3 diagnostics (B3_THUMB_LOG)
    std::map<int, ThumbFit> thumb_fit;
    std::map<std::uint64_t, ThumbSlot> hud_thumb_textures;
    std::vector<b3gfx::TextureId> hud_thumb_free;   // slots of thumbnails no longer drawn (their textures refilled next)
    std::uint64_t hud_built_thumb = 0;
    // Accessibility (a11y.h; PC SETTINGS > GAME > ACCESSIBILITY): HUD COLOUR FILTER (0 off, 1 deuteranopia, 2
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
    // hudmods (hudmods.h): RPM METER and BOOST BAR SHAKE, made on a copy of the frame before it is laid out (both off:
    // nothing of it runs)
    HudModOptions hud_mod_options;
    HudMods hud_mods;
    // carsel1 (PC SETTINGS > CAR SERIES ON L1 / R1, input_keys.h ShoulderSeries): the device's L1 / R1 glyphs beside the
    // car select's series arrows (prompts.h series_glyphs; drawn only); off: nothing of it runs
    bool series_glyphs = false, hud_built_series = false;
    std::uint64_t series_glyph_frames = 0;   // frames drawn with them (the log)
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
    // options3: `slot` 1 is a second build of its own (its own buffers), for a layer drawn in the same frame as another
    // (DRIVER LABELS over the race with the race's other host-UI layers)
    void host_ui_list(b3gfx::Device& dev, const std::shared_ptr<const HostUi>& ui, float aspect, std::vector<b3gfx::DrawItem>& out,
                      int out_h, int out_w, int slot = 0);
    std::map<std::string, b3gfx::TextureId> host_ui_tex;   // by path ("" white); kNone: unreadable
    b3gfx::TextureId host_img_tex = b3gfx::kNone;   // settingsui: the host's picture ("img:" path; one slot, refilled on a change)
    std::string host_img_name;                      // the "img:" path it holds
    struct HostUiBuild {   // a host-UI layer's build: its buffers and what it was built from
        b3gfx::BufferId vb = b3gfx::kNone, ib = b3gfx::kNone;
        std::shared_ptr<const HostUi> built;
        float aspect = 0;
        int h = 0, w = 0, filter = 0;
        std::vector<b3gfx::DrawItem> items;
    };
    HostUiBuild host_build[2];
    // fe2 (§24): the World Tour globe (HudFrame::globes) among the 2D draws; off with --no-globe
    bool show_globe = true;
    void globe_items(const HudGlobe& g, const std::function<b3gfx::TextureId(std::uint64_t)>& tex, float fw, float fh, int out_w,
                     int out_h, int width, int height, std::vector<b3gfx::Vertex>& vs, std::vector<std::uint32_t>& is,
                     std::vector<b3gfx::DrawItem>& items) const;
    // hud4: markers projected per displayed frame (--hud-markers frame, default | tick: as captured; key K)
    bool hud_reproject = true;
    // orbitcam1: the frame's Frame::orbit_away (set by draw_list / split_frame / frontend_frame): a marker hud_list places
    // where the game projected it (no hud4 reprojection: the split screen's top / bottom, --hud-markers tick) is scaled
    // about its point by 1 - away, hidden at 1 (the game projected it with its own camera, not the turned one)
    float orbit_away[2] = {0.0f, 0.0f};
    // orbitcam2 (trackrays.h): the route's walls for the orbit camera, built on a worker as the track loads; null
    // before a track (waits for the build the first time)
    const TrackRays* track_rays() const { return rays_job.get(); }
    std::shared_ptr<const TrackRays> track_rays_shared() const { return rays_job.shared(); }
    mutable TrackRaysJob rays_job;   // (declared after `hi`: its worker reads it; reset in clear and destroyed first)
    std::vector<int> hud_item_marker;           // per hud_items entry: its HudFrame::markers index, -1 none
    std::vector<int> hud_item_draw;             // splitlayout1: per hud_items entry: its HudFrame::draws index, -1 none
    std::vector<std::array<float, 16>> hud_item_model;   // per entry: its 2D -> clip matrix as built
    std::vector<std::uint32_t> hud_item_count;  // per entry: its index count as built
    struct MarkerPlace { bool shown = true; float dx = 0, dy = 0, r = 1; float x = 0, y = 0; };
    std::vector<MarkerPlace> hud_marker_place;  // of the last hud_list call (per HudFrame::markers entry)
    // gameopts (MIRROR MODE): the last draw_list's frame was mirrored, so hud_list / split_hud place the world-anchored
    // markers at their mirrored points (each about its view's middle; the marker itself is not reversed)
    bool hud_mirror = false;
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
                       std::vector<b3gfx::DrawItem>& out, bool hide_own = false, float tail = -1.0f);
    // (taillights: `tail` >= 0, night or rain, the rear lamps glow at that level up to full with each car's brake level)
    std::vector<float> vehicle_fades;   // traffic-popin: the frame's traffic_fade per vehicle (the shadows')
    float frame_now = 0;   // traffic-popin: the displayed frame's world clock (Frame::fx.now; draw_list sets it) for traffic_fade
    // ghost1 (ghost.h; PC SETTINGS > GAME > MODES > GHOST): the Time Attack ghost, drawn after the cars while `ghost` is set
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
    // The paint a vehicle's model is drawn in (vehicle_draws; vpreload the same): the game's v.paint, or a local player's
    // own paint (carpaint: a custom colour over the reference paint's texture, another model in the cache); own / pm the
    // player's CarPaint and PaintMap when one applies
    struct DrawPaint { const CarPaint* own = nullptr; const PaintMap* pm = nullptr; int paint = 0; };
    DrawPaint draw_paint(b3gfx::Device& dev, int file, const Vehicle& v);
    int hd_prefetched = 0;        // hdcache: the vehicle files preload_vehicles had the HD pack convert ahead (0: none)
    double hd_prefetch_ms = 0;    // ... and the time it took
    bool hd_prefetch_cut = false; // ... stopped by the wait's end or the budget (the rest converts at first draw)
    b3port::ProcessMemory hd_prefetch_mem[2];   // ... the process's memory before and after it
    // vpreload (fastforward.h): the models of `vehicles` (each in its drawn paint, LOD blocks 0-4) loaded now (the scene
    // wait), within `budget_ms` and while `go()`; nothing without vehicles (a load out of a race); first, with an HD pack, every
    // file of `heads` has its replacements converted (hdcache). Returns the models loaded;
    // `stop`: "" all of them, else why it stopped early.
    int preload_vehicles(b3gfx::Device& dev, const std::vector<Vehicle>& vehicles, const std::vector<ModelHead>& heads,
                         double budget_ms, const std::function<bool()>& go, std::string& stop);
    void paint_car_items(std::vector<b3gfx::DrawItem>& out, std::size_t first, const b3view::CarModel& cm, const PaintMap& pm,
                         const CarPaint& own, int game_paint) const;   // paint3: the player's paint on a car's draws
    int thumb_file(const std::string& name);                         // paint3: a thumbnail's car file (cars index), -1 none
    // paint3: the thumbnails' view: yaw, pitch, vertical field of view (degrees; B3_THUMB_YAW / _PITCH / _FOV)
    float thumb_angles[3] = {[] { const char* e = std::getenv("B3_THUMB_YAW"); return float(e && *e ? std::atof(e) : -44.0); }(),
                             [] { const char* e = std::getenv("B3_THUMB_PITCH"); return float(e && *e ? std::atof(e) : 18.0); }(),
                             [] { const char* e = std::getenv("B3_THUMB_FOV"); return float(e && *e ? std::atof(e) : 30.0); }()};
    bool car_picture_begin(b3gfx::Device& dev, int file, int paint, const CarPaint* own, float s, float ox, float oy, int w, int h, PictureJob& job);
    int car_picture_take(b3gfx::Device& dev, PictureJob& job, bool wait, std::vector<std::uint8_t>& rgba);
    void thumb_search(b3gfx::Device& dev, const HudTexture& t, int file);
    void thumb_models_release(b3gfx::Device& dev);
    b3gfx::TextureId thumb_texture(b3gfx::Device& dev, const HudTexture& t, int file, int palette, const CarPaint* own, std::uint64_t key,
                                   std::uint64_t sig, std::vector<std::uint64_t>& used);   // paint3 (view.cpp)
    b3gfx::TextureId game_texture(b3gfx::Device& dev, std::uint64_t key, const std::map<std::uint64_t, std::shared_ptr<const HudTexture>>& textures);
    // paint3: the tick's carousel slots whose model is not their picture's (set before hud_list; Frame::carousel)
    std::shared_ptr<const HostCarousel> hud_carousel, hud_built_carousel;
    // paint3 (reusable for any car file, e.g. a series' cars without pictures of their own): one car file's picture as the
    // car pages' thumbnails show a car (a 3/4 front view from above, on nothing: RGBA with coverage), in game paint `paint`
    // and, with `own`, the player's paint layers; `s` / `ox` / `oy`: the framing's scale and NDC offset. false: not ready
    // (its paint map is being made) or not drawable. view.cpp has the tuning variables.
    bool render_car_picture(b3gfx::Device& dev, int file, int paint, const CarPaint* own, float s, float ox, float oy, int w, int h,
                            std::vector<std::uint8_t>& rgba);
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
    // nightlights (§17.2, from post_frame at night): the frame's headlight spots (FrameParams::Night::lamp: the
    // players' cars first, then the nearest within 150 m whose beam reaches the view, at most night.lights) and the
    // tunnel signal (Night::enclosed: the view's car's sky_open through a threshold, smoothed; night_tunnel)
    void night_lamps(const Frame& f, b3gfx::FrameParams& fp) const;
    void night_tunnel_frame(const Frame& f, b3gfx::FrameParams& fp) const;
    // nightlights (§17.2, D.2): the tunnels' light fixtures. At load, the hi / lo items whose texture name is on the
    // route's region list (night_fixture_names) get DrawItem::night_emissive (read only at night). $B3_NIGHT_TEXLOG=1
    // (a local tool to find the names): each texture first drawn while the view is enclosed (> 0.5) is logged with its
    // texels' luminance, saturation and the share of bright unsaturated texels (night_texinfo, built at load)
    void night_fixtures_load();
    void night_texlog(const std::vector<b3gfx::DrawItem>& out, std::size_t first) const;
    struct NightTex { std::string name; float lum = 0, sat = 0, bright = 0; };
    std::map<b3gfx::TextureId, NightTex> night_texinfo;
    mutable std::set<std::string> night_texlogged;
    // nightlights (§17.2): the tunnel test's second signal (night_cover): the route's opaque unit triangles (hi) in a grid
    // of 8 m cells over x / z, built at the route's load when night is on (else on the first night frame); rays from the
    // view's car upwards and 35 degrees off the vertical in eight directions, the share of them that hit within 40 m.
    // A flat table: the occupied cells' keys sorted, each with its run of triangle indices (no per-cell allocation)
    // street lights (§17.2): the route's lamps (head position, tint), found at load from the region's lamp textures
    // height: above its foot (m); extent: its group's vertical extent (m: a pole alone, or an arm / a head)
    // base: the x / z of its group's lowest point (a pole's foot, or where an arm leaves its pole); arm: the lamp is
    // 1 m or more out from its base (an arm's lens), else on its base's vertical (a pole top, a bend, a head on a post)
    struct NightStreet { float pos[3], colour[3], height, extent = 0, base[2] = {0, 0}; bool arm = false; std::uint8_t kind = 0; };
    // the pool's size for a lamp this high (m): the shape's d0 and fade x height / the reference height (street_shape[6]),
    // kept within 0.75..2
    float night_street_scale(float height) const { return std::clamp(height / std::max(night.street_shape[6], 1.0f), 0.75f, 2.0f); }
    // (nightfix: loaded at the route's load only when the race is at night or $B3_NIGHT is set, else on the first night
    // frame: no load time by day)
    mutable std::vector<NightStreet> night_streets;
    mutable bool night_streets_loaded = false;
    void night_street_load() const;
    void night_lamps_build(bool fittings_only, std::vector<NightStreet>& out) const;
    // TUNNEL LIGHTS (§35.4): GRAPHICS > LIGHTING (pcsettings; $B3_TUNNEL_LIGHTS wins): 1 the fittings glow by day too
    // (Glow), 2 as the Xbox draws them (Xbox; xboxglow §35.5: the fittings' alpha glow masks, self-lit). Without
    // settings (the offscreen tools): $B3_TUNNEL_LIGHTS (tunnel_lights_env), else Game
    int tunnel_lights = [] { const char* e = std::getenv("B3_TUNNEL_LIGHTS"); return e && *e ? tunnel_lights_env(e) : 0; }();
    // the fittings for the day halos: built on a worker at the route's load when the route has any (whatever the setting:
    // it reaches the view after the load), taken on the first day frame that draws them (waiting if not done: a few ms
    // after the load). day_route: the route has fittings, else no day glow pass runs. (Declared after `hi`, which the
    // worker reads: destroyed first, so the future's wait comes before.)
    mutable std::vector<NightStreet> day_fittings;
    mutable std::future<std::vector<NightStreet>> day_fittings_job;
    bool day_route = false;
    void day_fittings_reset();
    // xboxglow (§35.5): the route's textures with a glow mask, by the drawn texture (two textures of one name on a route are
    // told apart: the GPU texture the draw samples)
    mutable std::map<b3gfx::TextureId, bool> glow_masks;
    void night_street_frame(const Frame& f, b3gfx::FrameParams& fp) const;
    void night_lamp_coronas(const Camera& cam, std::vector<CoronaQuad>& quads, bool day = false) const;   // nightfix: the lamps' coronas
    // nightfix: the front-lit billboards' floodlights (the '+' boards of the region's fixture table): per board plane
    // 1-4 fixtures along its bottom edge, 1 m out on each side (the camera's side lights it), aimed at the board's middle
    struct NightBoard { float pos[3], axis[3], normal[3], size; };
    mutable std::vector<NightBoard> night_boards;
    void night_board_load() const;
    void night_board_frame(const Frame& f, b3gfx::FrameParams& fp) const;
    static std::array<std::uint8_t, 3> night_window_grid(const b3asset::Texture& tx);   // nightfix: mean, columns, rows
    // round 3b: the neon and LED signs (the region's '!' fixtures) as coronas: one per sign (its triangles in 3 m cells,
    // neighbours joined), at its centre, its size, the colour of its lit texels (loaded with the lamps)
    struct NightGlow { float pos[3], colour[3], half; };
    mutable std::vector<NightGlow> night_glows;
    void night_glow_load() const;
    struct NightCover {
        bool built = false;
        std::vector<float> tri;                                      // 9 floats per triangle
        std::vector<std::uint64_t> keys;                             // occupied cells, sorted
        std::vector<std::uint32_t> first;                            // keys.size() + 1 offsets into `ids`
        std::vector<std::uint32_t> ids;                              // triangle indices per cell
    };
    mutable NightCover night_cover_grid;
    void night_cover_build() const;
    float night_cover(const float pos[3], float* wide = nullptr) const;   // wide: the 60-degree ring's share (nightfix)
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
    bool hud_split_pass_ = false;   // promptround: hud_list runs for the split-screen own layout (prompts keep their quads)
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
    // splitlayout1 (splitlayout.h, PC SETTINGS > DISPLAY > LAYOUT > SPLIT SCREEN): 0 the game's top / bottom (the path above,
    // unchanged), 1 side by side: the views in the left / right halves, each projected at its own shape from the game's
    // side-by-side view window (Hor+), drawn with the renderer's own culling, and each player's 2D placed in its view
    // (split_hud); view_aspect is then not used.
    int split_layout = 0;
    // splitlayout1: `items` (hud_list's items of a split frame's 2D, laid out at 4:3 over the whole output) moved into
    // their views: each draw by its anchor (split_hud_affine), markers projected through their view's camera
    void split_hud(const HudFrame& hud, const Frame& f, const std::vector<b3gfx::Device::ViewPass>& views, const float half_w[2],
                   int out_w, int out_h, std::vector<b3gfx::DrawItem>& items) const;
    // nightlights: the tunnel signal's smoothing (per view: split screen swaps it with player 2's)
    struct NightTunnel { float enclosed = 0; float now = 0; bool valid = false; };
    mutable NightTunnel night_tunnel;
    struct View2State { Effects effects; Coronas coronas; double bloom_last = -1; EffectStats fx_stats; CoronaArrays corona_arrays; CoronaStats corona_stats; NightTunnel night_tunnel; Rain rain; QuadLayer wet; Spray spray; RainCover rain_cover; };
    View2State view2_state;
    void swap_view2() {
        std::swap(effects, view2_state.effects);
        std::swap(coronas, view2_state.coronas);
        std::swap(bloom_last, view2_state.bloom_last);
        std::swap(fx_stats, view2_state.fx_stats);
        std::swap(corona_arrays, view2_state.corona_arrays);
        std::swap(corona_stats, view2_state.corona_stats);
        std::swap(night_tunnel, view2_state.night_tunnel);
        std::swap(rain_layer, view2_state.rain);
        std::swap(wet_layer, view2_state.wet);
        std::swap(spray_layer, view2_state.spray);
        std::swap(rain_cover, view2_state.rain_cover);
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
    // reflect-tunnel: the car env map's track part. The game draws the sky, the sun and, into env-map cameras 1..3, the
    // +0xC4 (LOD) blocks of the slots those cameras see (RenderBeforeVehicles 0x21C780, D_00665F20) around race car
    // [cam +0x19AC]'s box middle (BuildCustomCameraData 0x223130), far 100 m; every car of the view reflects that map.
    // env_world_map draws it (Device::render_env) and returns CarGlobals::env_world for this frame's cars.
    bool car_env_world = car_env_override() != 0;
    int env_slot = 0;                           // alternates: a split screen's two views each keep their own map
    std::vector<std::array<float, 6>> lo_boxes; // world box (lo xyz, hi xyz) per lo item; lo > hi: none (never culled)
    std::uint32_t env_world_map(b3gfx::Device& dev, const Frame& f, const std::vector<ModelHead>& heads,
                                const b3view::CarGlobals* globals = nullptr);
    // reflect2 (docs/car-shading.md §10; Settings::reflect_*): env_far the map's far plane (100: the game's, and only the
    // slots its env cameras see; more: every streamed slot's LOD block within it), env_props the props within kEnvPropReach
    // (static and dynamic, as draw_list draws them), env_every_second the map redrawn every second call per view (the
    // other call returns the last one)
    float env_far = 100.0f;
    bool env_props = false;
    bool env_cars = false;                      // reflect2 C (REFLECTION DETAIL Track, props and cars): the other vehicles
                                                // within kEnvCarReach, at LOD 1, their own env lookup off (no feedback)
    bool env_every_second = false;
    int env_face = 128;                         // reflect2 D: REFLECTION RESOLUTION, the cube faces' size (128 / 256)
    static constexpr float kEnvPropReach = 60.0f, kEnvCarReach = 50.0f;
    std::vector<std::array<float, 6>> hi_local_boxes;   // reflect2: per hi item its mesh's box in its own space (lo > hi: none)
    struct EnvKeep { std::uint32_t car = 0; std::uint32_t map = 0xFFFFFFFFu; unsigned calls = 0; };
    EnvKeep env_keep[2];                        // reflect2: per view (env_slot's alternation is per call), the last map
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
