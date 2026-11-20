// b3replay: plays a recorded race in the native renderer from a scene capture (docs/native-renderer.md §12). The capture
// (B3PORT_SCENE_OUT, src/gameview/scene_capture.cpp) holds, per 60 Hz simulation tick, the game's camera, the resident and visible
// track stream units, and every car's matrices; b3replay loads the track and car files from the game install, matches
// them to the capture, and draws at display rate with transforms and the camera interpolated between ticks (§6.4).
//
//   b3replay <capture.b3scene> [--game <install>/game (default data/game)] [--segment N] [--from-poll P] [--speed X] [--msaa N] [--vsync] [--car-reflections off|original|enhanced]
//            [--size WxH] [--fullscreen] [--driver vulkan|metal] [--car-lod N] [--no-fog] [--no-specular] [--no-effects] [--no-hud]
//            [--no-coronas] [--no-particles] [--hud-layout wide|43] [--fe-layout panels|43|edges] [--movie-fill blur|black] [--hud-filter sharp|game] [--hud-markers frame|tick] [--no-sun] [--no-sun-test] [--no-blur] [--blur-radial] [--no-bloom] [--post pc|game]
//            [--fe-segment N] [--no-movies] [--no-fe-car]   (menu1, §22: the front end; --poll P also picks a front-end segment)
//            [--no-showroom] [--no-reflection] [--no-fe-shadow] [--no-globe] [--no-room-env]   (fe2, §24: the car pages' showroom / floor reflection / shadow)
//   b3replay <capture> --offscreen out.png --poll P [--alpha A] [--size WxH] [--free-eye x,y,z [--look-at x,y,z]] [--no-deform] [--no-crash] [--no-anim]   (no window)
//     [--dof R[,F]]   photo2: photo mode's depth of field on the shot (blur radius R of the height, focus F m; none: autofocus)
//            --offscreen <dir> --sweep A:B:S renders polls A, A+S, ... B of the segment as <dir>/native_<P>.png; --no-cull draws every
//            resident unit and vehicle (the live window's default; --offscreen otherwise applies the game's culling)
// Cars: the game's car model and draw order (docs/native-renderer.md §12.6, b3view/carscene.h); LOD as the game's
// Draw code picks it unless --car-lod N forces one.
//   b3replay <capture> --info [--game <dir>]   (no GPU: segments, matched files, camera / culling checks)
//   b3replay --live <name> [--game <dir>] [--delay TICKS] [--max-fps N] [window options]     the game session's live feed (§13): the
//            ring <name> the game writes with B3PORT_SCENE_LIVE=<name>
//   b3replay --live <name> --offscreen-live <dir> [--shots P[+A],...] [--bench WxH] [--wait S] [--verify <capture>]   (no window: frames at
//            the given polls as <dir>/live_<P>_<A>.png, as --offscreen draws them; --bench renders every display frame
//            offscreen and prints frame times; --verify compares every tick read from the ring with the same poll's
//            tick in the session's capture file; exits when the session ends)
//
// Controls: Space pause, Left/Right -/+ 5 s, PgUp/PgDn -/+ 30 s, Home segment start, , and . one tick back / forward
// (paused), +/- speed, Tab next segment (race or front end, menu1); M movie pictures / placeholder; R the front end's showroom / the car on black (fe2); C attached (game camera) / free camera; I interpolation on/off (off = the
// game's own 60 Hz steps); V game culling (the units the game's main camera found visible) / all resident units; G fog,
// H full-screen specular (render3's game model, docs/native-renderer.md §11), F effects (shadows, skid marks: §14; coronas:
// §16), L coronas alone (car and traffic lights, the boost glow), O scrapes / sparks / particles (fx2, §19), U the HUD / 2D layer (hud1, §17: the game's Gt2d draws of
// the tick shown, in a 4:3 box at the window height; K: the rival markers held as captured / projected per frame, hud4
// §17.13); P the HD texture pack on/off (with --hd-pack, §18); X sun corona, B its sky test (water1, §21); N the zoom blur
// (directional / radial as the game picks), T the bloom, Z post quality PC / game (post1, §25); free
// camera: hold the right mouse button to look, W/A/S/D/Q/E move, Shift x4, wheel speed; F11 fullscreen; Esc quits.
#include <SDL3/SDL.h>
#ifndef B3REPLAY_LIBRARY   // b3render.dll (inproc1): no SDL main
#include <SDL3/SDL_main.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <tuple>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "b3asset/car.h"
#include "b3asset/carshade.h"
#include "b3asset/export.h"
#include "b3asset/hdpack.h"
#include "b3port/fileio.h"   // xboximport: the user data folder as UTF-8
#include "b3port/user_dir.h"   // portable mode: the user data folder
#include "b3asset/png.h"
#include "b3asset/postfx.h"
#include "b3asset/track.h"
#include "capture.h"
#include "live.h"
#include "photomode.h"   // photo2: --dof aims the autofocus as photo mode does
#include "carlook.h"
#include "view.h"
#include "pcsettings.h"
#include "b3settings/safemode.h"   // mac5: safe mode
#include "b3asset/discfs.h"
#include "b3mod/mods.h"            // modkit1b: the game's mod set over the disc files this renderer reads itself
#include "../b3view/carscene.h"
#include "../b3view/scene.h"

namespace fs = std::filesystem;
using b3replay::Mat34;

namespace {

struct Args {
    std::string capture, game, offscreen, driver, live, live_out, verify, dump_zones, hd_pack;
    int fe_layout = 2;        // --fe-layout panels (fe3, default) | 43 (wide1) | edges (hud2)
    bool whole_track = true;  // --track-units all (mac5, default) | resident (the PS2's streamed set)
    bool movie_fill = true;   // --movie-fill blur (fe3, default) | black
    long sweep_from = -1, sweep_to = -1, sweep_step = 0;   // --sweep A:B:S (live2): --offscreen is a directory, one PNG per poll
    std::string pop_scan;                                  // --pop-scan <dir> (live2): world-state pops, see run_pop_scan
    std::string profile;                                   // --profile <csv> A:B:N (perf1): per-frame costs, see run_profile
    long profile_from = 0, profile_to = 0;
    int profile_sub = 4;
    bool traffic_scan = false;                             // --traffic-scan (traffic-popin): traffic appearing / leaving, view.h traffic_probe
    bool cull_scan = false;                                // --cull-scan (with --pop-scan): the vehicles the game does not draw
    bool vis_scan = false;                                 // --vis-scan (with --pop-scan): the track units the game culls
    bool lod_scan = false;                                 // --lod-scan (with --pop-scan): LOD slots against their high meshes
    int skip_water = 0;                                    // --skip-water[=2c|=30] (diagnostics)
    std::set<int> skip_units, skip_backdrops;              // --skip-units / --skip-backdrops a,b,... (diagnostics)
    bool no_cull = false;                                  // --no-cull: every resident unit and vehicle, as the live window draws by default
    std::vector<std::pair<std::uint32_t, float>> shots;
    int bench_w = 0, bench_h = 0;
    double delay = 1.5, wait = 0, max_fps = 0;
    std::uint32_t load_from = 0, load_to = 0xFFFFFFFFu;   // --polls A:B (live2): decode only these polls of the capture
    bool clock_old = false;   // --clock-db55faa (live2: the display clock before the group rule)
    bool activate = false;    // --activate (inproc1)
    bool native_pad = false;  // --native-pad (play1): the window's keys and gamepads as the native game's pad
    bool native_audio = false;  // --native-audio (play1): the native game's sound (nativeaudio.h)
    bool fps_counter = false;               // --fps-counter (gsoff1)
    bool route_header = true;               // --no-route-header (gsoff1)
    std::string latency = "low", latency_log;   // --latency low|smooth|newest, --latency-log <csv> (lat1)
    bool latency_set = false;   // perf4: a latency mode chosen (--latency or LATENCY MODE), not the default
    // mac5 (Windows 2026-11-09: the car twitched forward / back against the camera at every tick): no extrapolation
    // by default -- a pose predicted up to a tick past the newest one snapped to the real tick when it came (the chase
    // camera's spring is not linear); the display now blends between real ticks only (about a tick more latency).
    // SETTINGS > PREDICTION or --lead still choose some.
    double margin_ms = 1.0, lead = 0.0;     // --margin MS, --lead T (lat1)
    double blend_ms = 0.0;                  // --blend MS (perf1; 0 = off, the default)
    float dither = 0.0f;                    // --dither (perf1 §33.8: 1 output step; 0 = off, the default)
    int frames_in_flight = -1;              // --frames-in-flight N (lat1); -1: Auto (perf4, include/b3gfx/frames_auto.h)
    bool inproc = false;      // --inproc (inproc1, §23): live mode on the in-process feed (b3render.dll inside the game)
    std::string car_look;        // --car-look <ini> (reflect2)
    int car_reflect = -1;        // --car-reflections (reflect1): -1 the setting's / default (enhanced)
    std::string settings_path;   // --settings <ini> (settings1): our PC settings at start (a missing file: the defaults)
    std::string controls_path;   // --controls <ini> (remap1): the control bindings (native window: <prefs>/controls.ini)
    std::string ghost_dir;       // --ghost-dir <dir> (ghost1, ghost.h): Time Attack ghosts (native window: <prefs>/ghosts)
    std::string speedrun_dir;    // --speedrun-dir <dir> (speedrun1, speedrun.h): best times (native window: <prefs>/speedrun)
    bool replay_playback = false;   // --replay-playback (media1, records.h): a replay plays (no ghost or best time written)
    std::optional<b3settings::Settings> pc;   // loaded from settings_path
    bool safe_mode = false;   // --safe-mode (mac5)
    std::string mods_dir, mods, mods_hash;   // modkit1b: --mods-dir, --mods, --mods-hash (native_play's active set)
    int mods_safe_off = 0;                   // modkit1b: --mods-safe-off N (safe mode left N mods off)
    std::vector<std::string> mod_textures;   // patches3: the active mods' [textures] packs, first wins (above --hd-pack)
    int fe_segment = -1;   // menu1: --fe-segment N (a front-end segment)
    bool movies = true;    // menu1: --no-movies / M
    bool fe_car = true;    // menu1: --no-fe-car
    bool room = true, reflection = true, fe_shadow = true;   // fe2: --no-showroom / R, --no-reflection, --no-fe-shadow
    bool globe = true;     // fe2: --no-globe (the World Tour globe left out of the 2D)
    bool room_env = true;  // fe2: --no-room-env (the showroom car reflects the ENVIRO.DAT sky alone)
    int segment = -1, msaa = 4, w = 1600, h = 900, car_lod = -1;   // car_lod -1: the game's choice
    long from_poll = -1, poll = -1;
    float speed = 1.0f, alpha = 0.0f;
    bool vsync = false, fullscreen = false, info = false, size_set = false, has_free_eye = false, fog = true, specular = true, effects = true;
    bool coronas = true;
    bool particles = true;   // fx2: --no-particles
    bool sun = true, sun_test = true;                      // --no-sun / --no-sun-test (water1 §21)
    bool blur = true;                                      // --no-blur (post1 §25: the zoom blur CB3World::Render picks; water1's --blur is the default now)
    bool blur_radial = false;                              // --blur-radial: captures before post1 draw the radial blur where it is not decidable
    bool bloom = true;                                     // --no-bloom (post1 §25)
    float radial_scale = 0.5f;                             // --radial-blur S (gsoff1 §25.8; 1 = the decomp's model)
    bool post_game = false;                                // --post game | pc (post1 §25: the GS's buffer sizes and taps, or PC quality)
    int bloom_warmup = 24;                                 // --bloom-warmup N: --offscreen draws the N ticks before first (the bloom's kept picture)
    float dof_radius = 0, dof_focus = 0;   // photo2: --dof R[,F] (an offscreen frame with photo mode's depth of field)
    bool has_look_at = false, deform = true, crash = true, anim = true, hud = true, hud_wide = true, hud_sharp = true, hud_reproject = true, no_hd = false;
    bool finish = true;       // --no-finish (perf1): no start / finish lines
    float free_eye[3] = {}, look_at[3] = {};
};

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (s == "--game") a.game = next();
        else if (s == "--segment") a.segment = std::atoi(next());
        else if (s == "--fe-segment") a.fe_segment = std::atoi(next());
        else if (s == "--from-poll") a.from_poll = std::atol(next());
        else if (s == "--poll") a.poll = std::atol(next());
        else if (s == "--alpha") a.alpha = float(std::atof(next()));
        else if (s == "--speed") a.speed = float(std::atof(next()));
        else if (s == "--msaa") a.msaa = std::atoi(next());
        else if (s == "--car-lod") a.car_lod = std::atoi(next());
        else if (s == "--vsync") a.vsync = true;
        else if (s == "--fullscreen") a.fullscreen = true;
        else if (s == "--driver") a.driver = next();
        else if (s == "--offscreen") a.offscreen = next();
        else if (s == "--sweep") { if (std::sscanf(next(), "%ld:%ld:%ld", &a.sweep_from, &a.sweep_to, &a.sweep_step) != 3 || a.sweep_step <= 0) return false; }
        else if (s == "--no-cull") a.no_cull = true;
        else if (s == "--skip-water") a.skip_water = 1;
        else if (s == "--skip-water=2c") a.skip_water = 2;
        else if (s == "--skip-water=30") a.skip_water = 3;
        else if (s == "--skip-units" || s == "--skip-backdrops") {   // diagnostics: comma-separated indices left out
            auto& dst = s == "--skip-units" ? a.skip_units : a.skip_backdrops;
            for (const char* q = next(); *q;) { dst.insert(std::atoi(q)); while (*q && *q != ',') ++q; if (*q) ++q; }
        }
        else if (s == "--pop-scan") a.pop_scan = next();
        else if (s == "--cull-scan") a.cull_scan = true;
        else if (s == "--traffic-scan") a.traffic_scan = true;
        else if (s == "--vis-scan") a.vis_scan = true;
        else if (s == "--lod-scan") a.lod_scan = true;
        else if (s == "--info") a.info = true;
        else if (s == "--dump-zones") a.dump_zones = next();
        else if (s == "--no-deform") a.deform = false;
        else if (s == "--no-crash") a.crash = false;
        else if (s == "--no-finish") a.finish = false;   // perf1
        else if (s == "--no-anim") a.anim = false;
        else if (s == "--no-fog") a.fog = false;
        else if (s == "--no-effects") a.effects = false;
        else if (s == "--no-coronas") a.coronas = false;
        else if (s == "--no-particles") a.particles = false;
        else if (s == "--no-sun") a.sun = false;
        else if (s == "--no-sun-test") a.sun_test = false;
        else if (s == "--blur") a.blur = true;
        else if (s == "--no-blur") a.blur = false;
        else if (s == "--blur-radial") a.blur_radial = true;
        else if (s == "--no-bloom") a.bloom = false;
        else if (s == "--radial-blur") a.radial_scale = float(std::atof(next()));   // gsoff1 §25.8
        else if (s == "--bloom-warmup") a.bloom_warmup = std::atoi(next());
        else if (s == "--post") { const std::string v = next(); if (v == "game") a.post_game = true; else if (v == "pc") a.post_game = false; else return false; }
        else if (s == "--no-hud") a.hud = false;
        else if (s == "--no-movies") a.movies = false;   // menu1: movie tiles drawn with the placeholder
        else if (s == "--no-fe-car") a.fe_car = false;   // menu1: front-end frames 2D only
        else if (s == "--no-showroom") a.room = false;   // fe2: the car pages' car on black (menu1's frame)
        else if (s == "--no-reflection") a.reflection = false;   // fe2: without RenderFrontEnd's floor reflection
        else if (s == "--no-fe-shadow") a.fe_shadow = false;     // fe2: without RenderFrontEndShadow
        else if (s == "--no-globe") a.globe = false;             // fe2: without the World Tour globe
        else if (s == "--no-room-env") a.room_env = false;       // fe2: the car's env map without the showroom
        else if (s == "--fe-layout") { const std::string v = next(); if (v == "panels") a.fe_layout = 2; else if (v == "43") a.fe_layout = 1; else if (v == "edges") a.fe_layout = 0; else return false; }
        else if (s == "--track-units") { const std::string v = next(); if (v == "all") a.whole_track = true; else if (v == "resident") a.whole_track = false; else return false; }
        else if (s == "--movie-fill") { const std::string v = next(); if (v == "blur") a.movie_fill = true; else if (v == "black") a.movie_fill = false; else return false; }
        else if (s == "--hud-layout") { const std::string v = next(); if (v == "43") a.hud_wide = false; else if (v == "wide") a.hud_wide = true; else return false; }
        else if (s == "--hud-markers") { const std::string v = next(); if (v == "tick") a.hud_reproject = false; else if (v == "frame") a.hud_reproject = true; else return false; }
        else if (s == "--hud-filter") { const std::string v = next(); if (v == "game") a.hud_sharp = false; else if (v == "sharp") a.hud_sharp = true; else return false; }
        else if (s == "--hd-pack") a.hd_pack = next();   // a PCSX2 replacement folder (docs/native-renderer.md §18); else $B3_HD_PACK
        else if (s == "--no-hd") a.no_hd = true;
        else if (s == "--no-specular") a.specular = false;
        else if (s == "--look-at") { if (std::sscanf(next(), "%f,%f,%f", &a.look_at[0], &a.look_at[1], &a.look_at[2]) != 3) return false; a.has_look_at = true; }
        else if (s == "--dof") { if (std::sscanf(next(), "%f,%f", &a.dof_radius, &a.dof_focus) < 1) return false; }   // photo2
        else if (s == "--free-eye") { if (std::sscanf(next(), "%f,%f,%f", &a.free_eye[0], &a.free_eye[1], &a.free_eye[2]) != 3) return false; a.has_free_eye = true; }
        else if (s == "--size") { if (std::sscanf(next(), "%dx%d", &a.w, &a.h) != 2) return false; a.size_set = true; }
        else if (s == "--live") a.live = next();
        else if (s == "--profile") {   // perf1
            a.profile = next();
            if (std::sscanf(next(), "%ld:%ld:%d", &a.profile_from, &a.profile_to, &a.profile_sub) < 2 || a.profile_sub < 1) return false;
            if (a.from_poll < 0) a.from_poll = a.profile_from;   // the race segment holding A
        }
        else if (s == "--offscreen-live") a.live_out = next();
        else if (s == "--activate") a.activate = true;   // inproc1: the live window takes the focus when shown
        else if (s == "--native-pad") a.native_pad = true;       // play1: the native game's pad (nativepad.h)
        else if (s == "--native-audio") a.native_audio = true;   // play1: the native game's sound (nativeaudio.h)
        else if (s == "--fps-counter") a.fps_counter = true;     // gsoff1
        else if (s == "--no-route-header") a.route_header = false;   // gsoff1: the old route rule (comparisons)
        else if (s == "--latency") { a.latency = next(); a.latency_set = true; if (a.latency != "low" && a.latency != "smooth" && a.latency != "newest") return false; }   // lat1
        else if (s == "--latency-log") a.latency_log = next();       // lat1
        else if (s == "--margin") a.margin_ms = std::atof(next());   // lat1
        else if (s == "--lead") a.lead = std::clamp(std::atof(next()), 0.0, 1.0);   // lat1
        else if (s == "--dither") a.dither = 1.0f;   // perf1
        else if (s == "--blend") a.blend_ms = std::clamp(std::atof(next()), 0.0, 200.0);   // perf1
        else if (s == "--frames-in-flight") a.frames_in_flight = std::atoi(next());   // lat1
        else if (s == "--inproc") { a.inproc = true; if (a.live.empty()) a.live = "inproc"; }   // inproc1: b3render.dll's feed
        else if (s == "--car-reflections") {   // reflect1: off | original | enhanced (the setting's value; wins over --settings)
            const std::string v = next();
            a.car_reflect = v == "off" ? 0 : v == "original" ? 1 : v == "enhanced" ? 2 : -1;
            if (a.car_reflect < 0) return false;
        }
        else if (s == "--car-look") { a.car_look = next(); std::string why; if (!b3replay::carlook::load_explicit(a.car_look, &why)) std::fprintf(stderr, "[b3carlook] %s\n", why.c_str()); }   // reflect2: the enhanced car look's values (docs/car-look.md)
        else if (s == "--settings") a.settings_path = next();   // settings1 (docs/pc-settings.md)
        else if (s == "--controls") a.controls_path = next();   // remap1 (src/native/input_bindings.h)
        else if (s == "--ghost-dir") a.ghost_dir = next();      // ghost1 (src/b3replay/ghost.h)
        else if (s == "--speedrun-dir") a.speedrun_dir = next();   // speedrun1 (src/b3replay/speedrun.h)
        else if (s == "--replay-playback") a.replay_playback = true;   // media1 (src/b3replay/records.h): native_play --replay
        else if (s == "--safe-mode") a.safe_mode = true;   // mac5: start with safe settings (include/b3settings/safemode.h)
        else if (s == "--mods-dir") a.mods_dir = next();     // modkit1b (include/b3mod/mods.h renderer_args)
        else if (s == "--mods") a.mods = next();
        else if (s == "--mods-hash") a.mods_hash = next();
        else if (s == "--mods-safe-off") a.mods_safe_off = std::atoi(next());
        else if (s == "--delay") a.delay = std::atof(next());
        else if (s == "--clock-db55faa") a.clock_old = true;
        else if (s == "--polls") { if (std::sscanf(next(), "%u:%u", &a.load_from, &a.load_to) != 2) return false; }
        else if (s == "--max-fps") a.max_fps = std::atof(next());
        else if (s == "--wait") a.wait = std::atof(next());
        else if (s == "--verify") a.verify = next();
        else if (s == "--bench") { if (std::sscanf(next(), "%dx%d", &a.bench_w, &a.bench_h) != 2) return false; }
        else if (s == "--shots") {
            std::string list = next();
            for (std::size_t at = 0; at <= list.size();) {
                const std::size_t comma = std::min(list.find(',', at), list.size());
                const std::string item = list.substr(at, comma - at);
                unsigned poll = 0;
                float alpha = 0;
                if (std::sscanf(item.c_str(), "%u+%f", &poll, &alpha) < 1 || alpha < 0 || alpha >= 1) return false;
                a.shots.push_back({std::uint32_t(poll), alpha});
                at = comma + 1;
            }
        }
        else if (!s.empty() && s[0] != '-' && a.capture.empty()) a.capture = s;
        else return false;
    }
    return (!a.capture.empty() || !a.live.empty()) && a.w > 0 && a.h > 0;
}

// ---- the replay scene -----------------------------------------------------------------------------------------------

struct Replay {
    Args args;
    b3replay::Capture cap;
    b3replay::View view;
    std::size_t seg = 0;
    bool fe = false;   // menu1 (§22): seg indexes cap.fe_segments (a front-end stretch, drawn 2D-only)

    const b3replay::Segment& segment() const { return fe ? cap.fe_segments[seg] : cap.segments[seg]; }
    // Every segment, race and front end, in capture order: (front end, index)
    std::vector<std::pair<bool, std::size_t>> all_segments() const {
        std::vector<std::pair<bool, std::size_t>> v;
        for (std::size_t i = 0; i < cap.segments.size(); ++i) v.push_back({false, i});
        for (std::size_t i = 0; i < cap.fe_segments.size(); ++i) v.push_back({true, i});
        std::sort(v.begin(), v.end(), [&](const auto& a, const auto& b) {
            return (a.first ? cap.fe_segments[a.second] : cap.segments[a.second]).first < (b.first ? cap.fe_segments[b.second] : cap.segments[b.second]).first;
        });
        return v;
    }
    void next_segment() {
        const auto v = all_segments();
        for (std::size_t k = 0; k < v.size(); ++k)
            if (v[k].first == fe && v[k].second == seg) { fe = v[(k + 1) % v.size()].first; seg = v[(k + 1) % v.size()].second; return; }
    }
    std::string segment_name() const {
        const auto& s = segment();
        char b[96];
        std::snprintf(b, sizeof b, "%s segment %zu (polls %u..%u)", fe ? "front-end" : "race", seg, cap.ticks[s.first].poll, cap.ticks[s.last].poll);
        return b;
    }

    // The capture's current segment as a timeline (absolute tick indices).
    struct SegmentTicks : b3replay::Timeline {
        const Replay* r;
        explicit SegmentTicks(const Replay* rp) : r(rp) {}
        std::size_t first() const override { return r->segment().first; }
        std::size_t last() const override { return r->segment().last; }
        const b3replay::Tick& at(std::size_t i) const override { return r->cap.ticks[i]; }
    } timeline{this};

    bool load_capture(std::string* why) {
        if (!cap.load(args.capture, why, true, args.load_from, args.load_to)) return false;
        if (cap.segments.empty() && cap.fe_segments.empty()) { if (why) *why = "no race segment (track on, camera, race cars) and no front-end segment (HUD frames) in the capture"; return false; }
        view.game = args.game;
        view.car_lod = args.car_lod;
        view.show_effects = args.effects;
        view.show_coronas = args.coronas;
        view.show_particles = args.particles;
        view.show_sun = args.sun;
        view.sun_occlusion = args.sun_test;
        view.show_blur = args.blur;
        view.radial_undecided = args.blur_radial;
        view.show_bloom = args.bloom;
        view.radial_scale = args.radial_scale;   // gsoff1
        view.post_game = args.post_game;
        view.show_hud = args.hud;
        view.show_movies = args.movies;
        view.show_frontend_car = args.fe_car;
        view.show_room = args.room;
        view.room_reflection = args.reflection;
        view.room_shadow = args.fe_shadow;
        view.show_globe = args.globe;
        view.room_env = args.room_env;
        view.skip_units = args.skip_units; view.skip_backdrops = args.skip_backdrops; view.skip_water = args.skip_water;
        view.hud_wide = args.hud_wide;
        view.fe_layout = args.fe_layout;
        view.movie_fill = args.movie_fill;
        view.whole_track = args.whole_track;
        view.hud_sharp = args.hud_sharp;
        view.hud_reproject = args.hud_reproject;
        view.deform = args.deform;
        view.crash_objects = args.crash;
        view.animate = args.anim;
        view.cars = b3replay::scan_cars(args.game);
        if (args.pc) b3replay::apply_view(view, *args.pc);   // settings1
        if (!args.car_look.empty() && !b3replay::carlook::explicit_loaded() && !b3replay::carlook::load_explicit(args.car_look, why)) return false;
        if (args.car_reflect >= 0) view.car_reflect = args.car_reflect;   // reflect1 (View's start value: $B3_CAR_REFLECTIONS, else enhanced)
        return true;
    }

    void pick_segment() {
        fe = false;
        if (args.fe_segment >= 0 && std::size_t(args.fe_segment) < cap.fe_segments.size()) { fe = true; seg = std::size_t(args.fe_segment); return; }
        if (args.segment >= 0 && std::size_t(args.segment) < cap.segments.size()) { seg = std::size_t(args.segment); return; }
        if (args.from_poll >= 0 || args.poll >= 0) {
            // the segment (race or front end) holding the poll, else the first one ending after it
            const long p = args.poll >= 0 ? args.poll : args.from_poll;
            for (const auto& [f, i] : all_segments()) {
                const auto& s = f ? cap.fe_segments[i] : cap.segments[i];
                if (long(cap.ticks[s.last].poll) >= p) { fe = f; seg = i; return; }
            }
        }
        if (cap.segments.empty()) {   // a front-end-only capture (the menu session): the longest stretch
            std::size_t best = 0;
            for (std::size_t i = 0; i < cap.fe_segments.size(); ++i)
                if (cap.fe_segments[i].last - cap.fe_segments[i].first > cap.fe_segments[best].last - cap.fe_segments[best].first) best = i;
            fe = true;
            seg = best;
            return;
        }
        std::size_t best = 0;
        for (std::size_t i = 0; i < cap.segments.size(); ++i)
            if (cap.segments[i].last - cap.segments[i].first > cap.segments[best].last - cap.segments[best].first) best = i;
        seg = best;
    }

    std::string segment_route(std::size_t si) const {
        const auto& s = cap.segments[si];
        return b3replay::find_route(args.game, s.route_size, s.route_units, b3replay::route_samples(cap.ticks, s.first, s.last));
    }

    bool load_track(b3gfx::Device& dev, std::string* why) {
        if (fe) return true;   // the front end: no route (2D only)
        const auto& s = segment();
        const std::string route = segment_route(seg);
        if (route.empty()) { if (why) *why = "no TRACKS/*/*/STATIC.DAT with size " + std::to_string(s.route_size) + " and " + std::to_string(s.route_units) + " units under " + args.game; return false; }
        if (!view.load_track(dev, route, why)) return false;
        // captures without the game data block (before crash2): the crash stage from the race cars' start positions [L]
        const auto& t0 = cap.ticks[s.first];
        view.fallback_stage = t0.crash.valid ? -1 : b3replay::start_stage(view.crash_scene.stages, t0);
        return true;
    }

    // The scene at segment time t (seconds from the segment's first tick).
    b3replay::Frame at(double t, bool interpolate) const {
        const auto& s = segment();
        const double n = double(s.last - s.first);
        const double x = std::clamp(t * 60.0 + 1e-6, 0.0, n);
        const std::size_t i = std::size_t(std::floor(x));
        auto f = b3replay::frame_at(timeline, s.first + i, x - double(i), interpolate);
        if (!fe) view.apply_fov(f.cam, f.vehicles);   // settings1 (drawn-only FOV)
        return f;
    }

    void draw_list(b3gfx::Device& dev, const b3replay::Frame& f, bool game_cull, const float eye[3], std::vector<b3gfx::DrawItem>& out) {
        view.draw_list(dev, f, cap.heads, game_cull, eye, out);
    }
};

// Frustum class of a sphere in the game camera (0 outside, 1 inside, 2 intersecting), as GtIntersection reports it.
int frustum_class(const b3replay::Camera& c, const float pos[3], float radius) {
    float d[3] = {pos[0] - c.m.r[3][0], pos[1] - c.m.r[3][1], pos[2] - c.m.r[3][2]};
    auto dot = [&](const float* a) { return a[0] * d[0] + a[1] * d[1] + a[2] * d[2]; };
    const float x = dot(c.m.r[0]), y = dot(c.m.r[1]), z = dot(c.m.r[2]);
    const float dist[6] = {
        z - c.znear, c.zfar - z,
        (c.half_w * z - x) / std::sqrt(1 + c.half_w * c.half_w), (c.half_w * z + x) / std::sqrt(1 + c.half_w * c.half_w),
        (c.half_h * z - y) / std::sqrt(1 + c.half_h * c.half_h), (c.half_h * z + y) / std::sqrt(1 + c.half_h * c.half_h)};
    bool inside = true;
    for (float s : dist) { if (s < -radius) return 0; if (s < radius) inside = false; }
    return inside ? 1 : 2;
}

// crash2 (§12.9): per segment the crash stage (the capture's CB3GameData +0x558, or the start-position match for older
// captures), the captured ramp corners against CB3CrashRamp::Prepare's formula on the file's matrices, the pickups and
// when they were taken, the pickups' camera (camera manager quaternion) against the drawn camera, and each ramp's
// launch: the player car's ticks on the ramp's footprint and its height over the ramp's base.
void crash_info(Replay& r) {
    const auto& cap = r.cap;
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        const std::string route = r.segment_route(si);
        std::vector<std::uint8_t> bgd;
        std::vector<b3asset::GameStage> stages;
        std::string why;
        if (route.empty() || !b3asset::read_file(route + "/GAMEDATA.BGD", bgd) ||
            !b3asset::read_game_data(b3asset::Bytes{bgd.data(), bgd.size()}, stages, &why)) {
            std::printf("segment %zu crash: no GAMEDATA.BGD %s\n", si, why.c_str());
            continue;
        }
        const auto& t0 = cap.ticks[s.first];
        int fits = 0;
        const int by_start = b3replay::start_stage(stages, t0, &fits);
        long valid = 0, logic = 0, off = 0;
        std::set<std::uint64_t> ids;
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const auto& c = cap.ticks[i].crash;
            if (!c.valid) continue;
            ++valid;
            logic += c.logic;
            off += c.ramps_off;
            ids.insert(c.stage);
        }
        std::printf("segment %zu crash: %zu stages in GAMEDATA.BGD; start-position match: stage %d (%d fit); game data in %ld of %zu ticks"
                    " (stage logic %ld, ramps off %ld)\n", si, stages.size(), by_start, fits, valid, s.last - s.first + 1, logic, off);
        // poll ranges where a per-tick condition holds
        auto ranges = [&](const char* what, auto&& cond) {
            std::printf("  %s:", what);
            long n = 0;
            for (std::size_t i = s.first; i <= s.last; ++i) {
                if (!cond(cap.ticks[i])) continue;
                std::size_t j = i;
                while (j + 1 <= s.last && cond(cap.ticks[j + 1])) ++j;
                std::printf(" %u..%u", cap.ticks[i].poll, cap.ticks[j].poll);
                ++n;
                i = j;
            }
            std::printf("%s\n", n ? "" : " none");
        };
        if (valid > 0) {
            long bon = 0, boff = 0, bunk = 0, cm[3] = {};
            std::map<int, long> groups;
            for (std::size_t i = s.first; i <= s.last; ++i) {
                const auto& c = cap.ticks[i].crash;
                if (!c.valid) continue;
                (c.barriers == 1 ? bon : c.barriers == 0 ? boff : bunk)++;
                ++cm[c.crash_mode + 1];
                if (c.barriers == 1) ++groups[c.barrier_group];
            }
            std::printf("  func_0021C010 (STATIC +0x28 barriers): drawn %ld ticks, not %ld, unknown %ld; IsCrashMode 1 / 0 / unknown: %ld / %ld / %ld; groups drawn:",
                        bon, boff, bunk, cm[2], cm[1], cm[0]);
            for (const auto& [g, n] : groups) std::printf(" %d (%ld)", g, n);
            std::printf("\n");
            ranges("barriers drawn at polls", [](const b3replay::Tick& t) { return t.crash.valid && t.crash.barriers == 1; });
            ranges("ramps off (D_00665EA1) at polls", [](const b3replay::Tick& t) { return t.crash.valid && t.crash.ramps_off; });
            // perf1 (§33): CB3GameData::Draw's start / finish lines
            ranges("start / finish lines decoded at polls", [](const b3replay::Tick& t) { return t.crash.valid && t.crash.finish_known; });
            ranges("\"finish\" drawn at polls", [](const b3replay::Tick& t) {
                for (const auto& l : t.crash.finish) if (l.texture == 0) return true;
                return false;
            });
            ranges("\"check\" drawn at polls", [](const b3replay::Tick& t) {
                for (const auto& l : t.crash.finish) if (l.texture == 1) return true;
                return false;
            });
            for (std::size_t i = s.first; i <= s.last; ++i)
                if (!cap.ticks[i].crash.finish.empty()) {
                    for (const auto& l : cap.ticks[i].crash.finish)
                        std::printf("  first line at poll %u: %s at (%.2f, %.2f, %.2f) dir (%.3f, %.3f, %.3f), half %.2f x %.2f\n", cap.ticks[i].poll,
                                    l.texture ? "check" : "finish", l.pos[0], l.pos[1], l.pos[2], l.dir[0], l.dir[1], l.dir[2], l.half_w, l.half_h);
                    break;
                }
            ranges("pickup camera off the viewport camera (right rows > 0.01 apart) at polls", [](const b3replay::Tick& t) {
                float r[3], d = 0;
                b3replay::quat_right(t.crash.cam_quat, r);
                for (int j = 0; j < 3; ++j) d = std::max(d, std::fabs(r[j] - t.cam.m.r[0][j]));
                return t.crash.valid && t.cam.valid && d > 0.01f;
            });
        }
        int st = by_start;
        for (std::uint64_t id : ids) {
            st = b3replay::stage_by_id(stages, id);
            std::printf("  captured stage id %016llx -> stage %d\n", static_cast<unsigned long long>(id), st);
        }
        if (st < 0) continue;
        const auto& stage = stages[std::size_t(st)];
        std::printf("  stage %d: %zu ramps, %zu pickups (%zu with mode bit 0)\n", st, stage.ramps.size(), stage.pickups.size(),
                    static_cast<std::size_t>(std::count_if(stage.pickups.begin(), stage.pickups.end(), [](const auto& p) { return p.modes & 1; })));
        // the captured ramps against the file
        float dmax = 0, mmax = 0, qmax = 0;
        long rchecked = 0, rbad = 0, qn = 0;
        std::map<std::size_t, std::uint32_t> taken_at;
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const auto& c = cap.ticks[i].crash;
            if (!c.valid) continue;
            if (c.ramps.size() != stage.ramps.size()) { ++rbad; continue; }
            for (std::size_t k = 0; k < c.ramps.size(); ++k) {
                ++rchecked;
                for (int p = 0; p < 6; ++p)
                    for (int j = 0; j < 3; ++j) dmax = std::max(dmax, std::fabs(c.ramps[k].corner[p][j] - stage.ramps[k].corner[p][j]));
                for (int p = 0; p < 4; ++p)
                    for (int j = 0; j < 3; ++j) mmax = std::max(mmax, std::fabs(c.ramps[k].m[p][j] - stage.ramps[k].m[p][j]));
                if (c.ramps[k].type != stage.ramps[k].type) ++rbad;
            }
            for (std::size_t k = 0; k < c.pickups.size(); ++k)
                if (c.pickups[k].taken && !taken_at.count(k)) taken_at[k] = cap.ticks[i].poll;
            float right[3];
            b3replay::quat_right(c.cam_quat, right);
            if (cap.ticks[i].cam.valid) {
                for (int j = 0; j < 3; ++j) qmax = std::max(qmax, std::fabs(right[j] - cap.ticks[i].cam.m.r[0][j]));
                ++qn;
            }
        }
        if (valid > 0) {
            std::printf("  ramps: %ld ramp-ticks checked, %ld ticks with another count / type; captured corners vs Prepare's formula on the"
                        " file's matrix: max |diff| %.3g m; captured matrix vs file: max |diff| %.3g\n", rchecked, rbad, dmax, mmax);
            const auto& c = cap.ticks[s.first].crash;
            std::printf("  pickups prepared: %zu:", c.pickups.size());
            for (const auto& p : c.pickups)
                std::printf(" %s (%.1f %.1f %.1f)", b3asset::kCrashPickupTextures[p.type % 9] + 7, p.pos[0], p.pos[1], p.pos[2]);
            std::printf("\n  pickups taken:");
            for (const auto& [k, poll] : taken_at) std::printf(" %zu at poll %u", k, poll);
            std::printf("%s\n  pickup camera (camera manager quaternion's right row) vs the viewport frame's right row: max |diff| %.3g over %ld ticks\n",
                        taken_at.empty() ? " none" : "", qmax, qn);
        }
        // launch: the player car (first race car) on each ramp's footprint, its height over the ramp's base after it
        for (std::size_t k = 0; k < stage.ramps.size(); ++k) {
            const auto& rp = stage.ramps[k];
            const float b = rp.type == 0 ? 4.0f : rp.type == 1 ? 6.0f : 7.0f, a = rp.type == 0 ? 3.0f : rp.type == 1 ? 4.0f : 2.55f;
            std::uint32_t on0 = 0, on1 = 0, top_poll = 0;
            float top = -1e30f;
            for (std::size_t i = s.first; i <= s.last; ++i) {
                const b3replay::Vehicle* car = nullptr;
                for (const auto& v : cap.ticks[i].vehicles) if (v.kind == b3replay::VehicleKind::kRaceCar) { car = &v; break; }
                if (!car) continue;
                float lx = 0, ly = 0, lz = 0;
                for (int j = 0; j < 3; ++j) {
                    const float d = car->main.r[3][j] - rp.m[3][j];
                    lx += d * rp.m[0][j];
                    ly += d * rp.m[1][j];
                    lz += d * rp.m[2][j];
                }
                if (std::fabs(lx) <= a && std::fabs(lz) <= b) { if (!on0) on0 = cap.ticks[i].poll; on1 = cap.ticks[i].poll; }
                if (on1 && cap.ticks[i].poll <= on1 + 120 && ly > top) { top = ly; top_poll = cap.ticks[i].poll; }
            }
            if (on0)
                std::printf("  ramp %zu (type %d, centre %.2f %.2f %.2f): player car on its footprint polls %u..%u (launch at %u), highest %.2f m"
                            " over the ramp's base at poll %u\n", k, rp.type, rp.m[3][0], rp.m[3][1], rp.m[3][2], on0, on1, on1, top, top_poll);
            else
                std::printf("  ramp %zu (type %d, centre %.2f %.2f %.2f): the player car never on it\n", k, rp.type, rp.m[3][0], rp.m[3][1], rp.m[3][2]);
        }
    }
}

// Effects (capture version 3, fx1): per segment the shadow strips the game draws per tick (RenderShadows' conditions and
// RenderAmbientShadow's opacity test, by vehicle kind) and the skid-mark emitters / points listed, and a check of the
// captured shadow geometry (vehicle data +0x18, +0xE80/+0xE90, wheel z) against the matched car file's bytes.
// anim1 (§15): the route's animated materials, and per segment the captured state (CB3TrackMaterial +0x10..+0x1F, anim1
// captures) against (1) CB3StaticTrack::Update replayed tick by tick from the segment's first captured state with each
// tick's clock (`sim`; `sim-1`: with the previous tick's clock) and (2) the state recomputed from the clock alone
// (track_anim_at, what b3replay draws without the block): exact when every field is bit-equal, `drawn` when the frame
// and the UV scroll sent (1 - frac(t), to 1e-5) agree.
void info_anim(Replay& r) {
    const auto& cap = r.cap;
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& sg = cap.segments[si];
        const std::string route = r.segment_route(si);
        std::vector<std::uint8_t> st;
        b3asset::Model m;
        b3asset::GsMemory gs;
        b3asset::TrackOptions to;
        to.streamed = to.backdrops = to.instances = to.props = false;
        std::string why;
        if (route.empty() || !b3asset::read_file(route + "/STATIC.DAT", st) ||
            !b3asset::read_track({st.data(), st.size()}, {}, to, gs, m, &why)) {
            std::printf("segment %zu anim: no route %s\n", si, why.c_str());
            continue;
        }
        const auto& an = m.track_anims;
        // where each one is drawn: the whole route as View::load_track reads it (units, instances, backdrops, +0x28 signs, props)
        std::vector<std::map<std::string, long>> uses(an.size());
        std::vector<std::string> where(an.size());
        {
            std::vector<std::uint8_t> sm;
            b3asset::read_file(route + "/STREAMED.DAT", sm);
            b3asset::Model full;
            b3asset::GsMemory gs2;
            b3asset::TrackOptions fo;
            fo.extra_backdrops = true;
            if (b3asset::read_track({st.data(), st.size()}, {sm.data(), sm.size()}, fo, gs2, full, &why))
                for (const auto& d : full.draws) {
                    if (d.anim < 0 || std::size_t(d.anim) >= uses.size()) continue;
                    std::string kind = d.part.substr(0, d.part.find(' '));
                    if (d.part.find("instance") != std::string::npos) kind = "instance";
                    else if (d.part.rfind("backdrop+28", 0) == 0) kind = "backdrop+28";
                    uses[std::size_t(d.anim)][kind] += d.index_count / 3;
                    if (d.transform < 0 && d.index_count && where[std::size_t(d.anim)].size() < 6) {   // a few places to look at
                        float c[3] = {0, 0, 0};
                        for (std::uint32_t q = 0; q < d.index_count; ++q)
                            for (int j = 0; j < 3; ++j) c[j] += full.vertices[full.indices[d.first_index + q]].pos[j] / float(d.index_count);
                        char b[96];
                        std::snprintf(b, sizeof b, " (%.0f,%.0f,%.0f)", c[0], c[1], c[2]);
                        where[std::size_t(d.anim)] += b;
                    }
                }
        }
        std::printf("segment %zu anim: %zu animated materials on %s\n", si, an.size(), route.c_str());
        for (std::size_t k = 0; k < an.size(); ++k) {
            const auto& a = an[k];
            std::printf("  material %d flags %04x %s '%s' frames %zu  +0x12 %d +0x13 %d +0x14 %g +0x18 %g; triangles:", a.material, a.flags,
                        a.frames.size() >= 2 ? "cycle " : (a.flags & 0x200) ? (a.f18 != 0 ? "steps " : "scroll") : "none  ",
                        a.frames.empty() ? "" : m.textures[std::size_t(a.frames[0])].name.c_str(), a.frames.size(), a.p12, a.p13, a.f14, a.f18);
            for (const auto& [kind, n] : uses[k]) std::printf(" %s %ld", kind.c_str(), n);
            std::printf("; at%s\n", where[k].c_str());
        }
        const auto same = [](const b3asset::TrackAnimState& x, const b3replay::AnimMaterial& y) {
            return x.cur == y.cur && x.p12 == y.p12 && x.p13 == y.p13 && std::memcmp(&x.period, &y.period, 4) == 0 && std::memcmp(&x.t, &y.t, 4) == 0;
        };
        const auto drawn = [&](const b3asset::TrackAnim& a, const b3asset::TrackAnimState& x, const b3replay::AnimMaterial& y) {
            if (x.cur != y.cur) return false;
            if (!(a.flags & 0x200)) return true;
            const float d = std::fabs(b3asset::track_uv_scroll(x.t) - b3asset::track_uv_scroll(y.t));
            return d < 1e-5f || d > 1.0f - 1e-5f;
        };
        long valid = 0, bad_list = 0;
        std::vector<long> sim(an.size()), sim1(an.size()), clk(an.size()), clk_drawn(an.size()), clk1_drawn(an.size()), changes(an.size());
        std::vector<std::uint32_t> first_bad(an.size(), 0);
        std::vector<b3asset::TrackAnimState> s0(an.size()), s1(an.size());
        bool seeded = false;
        const b3replay::Tick* prev = nullptr;
        for (std::size_t i = sg.first; i <= sg.last; ++i) {
            const auto& t = cap.ticks[i];
            if (!t.anim_valid) { seeded = false; prev = nullptr; continue; }
            if (t.anim.size() != an.size()) { ++bad_list; continue; }
            bool ok = true;
            for (std::size_t k = 0; k < an.size(); ++k) ok = ok && t.anim[k].material == an[k].material;
            if (!ok) { ++bad_list; continue; }
            ++valid;
            for (std::size_t k = 0; k < an.size(); ++k) {
                const auto& y = t.anim[k];
                if (seeded) {
                    b3asset::track_anim_update(an[k], s0[k], t.now);
                    b3asset::track_anim_update(an[k], s1[k], prev->now);
                    sim[k] += same(s0[k], y);
                    sim1[k] += same(s1[k], y);
                    if (!same(s0[k], y) && !same(s1[k], y) && !first_bad[k]) first_bad[k] = t.poll;
                    changes[k] += prev->anim[k].cur != y.cur;
                }
                const auto c = b3asset::track_anim_at(an[k], t.now);
                clk[k] += same(c, y);
                clk_drawn[k] += drawn(an[k], c, y);
                if (prev) clk1_drawn[k] += drawn(an[k], b3asset::track_anim_at(an[k], prev->now), y);
                s0[k] = s1[k] = b3asset::TrackAnimState{y.cur, y.p12, y.p13, y.period, y.t};   // re-seed: the next tick tests one step
            }
            seeded = true;
            prev = &t;
        }
        std::printf("  %ld of %zu segment ticks hold the update list (%ld with another list)\n", valid, sg.last - sg.first + 1, bad_list);
        for (std::size_t k = 0; valid && k < an.size(); ++k)
            std::printf("  material %d: one Update step exact %ld / with the previous clock %ld (of %ld)%s; frame changes %ld;"
                        " from the clock: exact %ld, drawn the same %ld (previous clock %ld) of %ld\n", an[k].material, sim[k], sim1[k],
                        valid - 1, first_bad[k] ? (" first miss poll " + std::to_string(first_bad[k])).c_str() : "", changes[k], clk[k],
                        clk_drawn[k], clk1_drawn[k], valid);
    }
    // the clock's resets (race load / restart) and whether the materials restart with it (CB3TrackMaterial::Prepare)
    const b3replay::Tick* prev = nullptr;
    for (const auto& t : cap.ticks) {
        if (prev && t.now < prev->now) {
            std::printf("clock reset at poll %u (%.4f -> %.4f):", t.poll, prev->now, t.now);
            for (std::size_t k = 0; k < t.anim.size() && k < 8; ++k)
                std::printf(" [%d cur %d t %g -> cur %d t %g]", t.anim[k].material, k < prev->anim.size() ? prev->anim[k].cur : -1,
                            k < prev->anim.size() ? prev->anim[k].t : -1.0f, t.anim[k].cur, t.anim[k].t);
            std::printf("%s\n", t.anim_valid ? "" : " (no update list)");
        }
        prev = &t;
    }
}

// HUD / 2D (hud1, §17): what the capture's Gt2d stream holds, per segment and in all.
void info_hud(Replay& r) {
    const auto& cap = r.cap;
    std::size_t with = 0, frames = 0;
    std::uint32_t missed = 0;
    std::map<std::string, int> spaces, fbps;
    std::map<int, std::uint64_t> modes;   // ALPHA byte | ABE << 8 | alpha-only << 9 -> triangles
    std::uint64_t tris = 0, draws = 0, unknown = 0, unbound = 0, sprites = 0, strips = 0, lines = 0, decal = 0, notcc = 0, point = 0, clipped = 0;
    // hud3: per object render callback (hud2 captures): calls, draws, triangles, first / last poll it drew in
    struct Callback { std::uint64_t calls = 0, draws = 0, tris = 0; std::uint32_t first = ~0U, last = 0, frames = 0; std::vector<std::uint32_t> polls; };
    std::map<std::uint32_t, Callback> callbacks;
    const b3replay::HudFrame* last = nullptr;
    for (const auto& t : cap.ticks) {
        if (!t.hud) continue;
        ++with;
        missed = std::max(missed, t.hud->missed);
        if (t.hud.get() == last) continue;
        last = t.hud.get();
        ++frames;
        const auto& h = *t.hud;
        for (const auto& o : h.objects) ++callbacks[o.pc].calls;
        if (std::getenv("B3HUD_INFO_EXTENT")) {   // debugging: per frame, each raster name's draws' x extent (max x, triangles)
            std::map<std::string, std::pair<float, std::size_t>> ext;
            for (const auto& d : h.draws) {
                if (!d.texture || d.tri.empty()) continue;
                const auto tx = cap.hud.textures.find(d.texture);
                auto& e = ext[tx != cap.hud.textures.end() ? tx->second->name : std::string("?")];
                if (e.second == 0) e.first = -1e9f;
                for (const auto& v : d.tri) e.first = std::max(e.first, v.x);
                e.second += d.tri.size() / 3;
            }
            std::printf("hudext %u", t.poll);
            for (const auto& [n, e] : ext) std::printf(" %s:%.1f:%zu", n.c_str(), double(e.first), e.second);
            std::printf("\n");
        }
        std::set<std::uint32_t> seen;
        for (const auto& d : h.draws) {
            if (d.object < 0 || std::size_t(d.object) >= h.objects.size()) continue;
            Callback& c = callbacks[h.objects[std::size_t(d.object)].pc];
            ++c.draws;
            c.tris += d.tri.size() / 3;
            if (seen.insert(h.objects[std::size_t(d.object)].pc).second) {
                c.polls.push_back(t.poll);
                c.first = std::min(c.first, t.poll);
                c.last = std::max(c.last, t.poll);
                ++c.frames;
            }
        }
        spaces[std::to_string(h.width) + "x" + std::to_string(h.height) + " at (" + std::to_string(h.ox) + ", " + std::to_string(h.oy) + ")"]++;
        char fb[64];
        std::snprintf(fb, sizeof fb, "FBP %u FBW %u PSM %u", unsigned(h.frame_reg & 0x1FF), unsigned((h.frame_reg >> 16) & 0x3F), unsigned((h.frame_reg >> 24) & 0x3F));
        fbps[fb]++;
        tris += h.stats.triangles;
        draws += h.draws.size();
        unknown += h.stats.unknown_texture;
        unbound += h.stats.unbound_texture;
        sprites += h.stats.sprites;
        strips += h.stats.strips;
        lines += h.stats.lines;
        for (const auto& d : h.draws) {
            modes[d.alpha | (d.abe ? 0x100 : 0) | (d.alpha_only ? 0x200 : 0)] += d.tri.size() / 3;
            if (d.texture && d.tfx_decal) ++decal;
            if (d.texture && !d.tcc) ++notcc;
            if (d.texture && !d.linear) ++point;
            if (d.scissor[0] > 0 || d.scissor[1] > 0 || d.scissor[2] < h.width - 1 || d.scissor[3] < h.height - 1) ++clipped;
        }
    }
    if (with == 0) { std::printf("hud: none in this capture (captures before hud1, or B3PORT_SCENE_HUD=0)\n"); return; }
    std::printf("hud: %zu of %zu ticks have a HUD frame, %zu distinct frames; flushes the hook missed: %u\n", with, cap.ticks.size(), frames, missed);
    for (const auto& kv : spaces) std::printf("hud: 2D space (Begin's scissor) %s in %d frames\n", kv.first.c_str(), kv.second);
    for (const auto& kv : fbps) std::printf("hud: FRAME_1 at Begin %s in %d frames\n", kv.first.c_str(), kv.second);
    std::printf("hud: %llu draws, %llu triangles (%llu sprites, %llu strip/fan triangles), %llu points/lines skipped, %llu triangles with an"
                " undecoded texture; draws with TFX decal %llu, TCC 0 %llu, point sampled %llu, clipped %llu\n",
                (unsigned long long)draws, (unsigned long long)tris, (unsigned long long)sprites, (unsigned long long)strips,
                (unsigned long long)lines, (unsigned long long)unknown, (unsigned long long)decal, (unsigned long long)notcc,
                (unsigned long long)point, (unsigned long long)clipped);
    // font1: textured triangles whose raster no SetTexture event named (they draw as solid blocks)
    std::printf("hud: %llu textured triangles without a raster (SetTexture not seen)%s\n", (unsigned long long)unbound,
                unbound ? ": the capture missed a SetTexture path (one the capture does not hook)" : "");
    for (const auto& kv : modes)
        std::printf("hud: ALPHA_1 0x%02X%s%s: %llu triangles\n", kv.first & 0xFF, (kv.first & 0x100) ? "" : " (ABE off)",
                    (kv.first & 0x200) ? " (frame alpha only)" : "", (unsigned long long)kv.second);
    for (const auto& [pc, c] : callbacks)
        std::printf("hud: object callback 0x%08X: %llu calls, %llu draws, %llu triangles in %u frames (polls %u..%u)\n", pc,
                    (unsigned long long)c.calls, (unsigned long long)c.draws, (unsigned long long)c.tris, c.frames, c.frames ? c.first : 0U, c.last);
    if (const char* e = std::getenv("B3HUD_INFO_POLLS")) {   // debugging: the polls one callback drew in (runs of polls)
        const auto it = callbacks.find(std::uint32_t(std::strtoul(e, nullptr, 16)));
        if (it != callbacks.end())
            for (std::size_t i = 0; i < it->second.polls.size();) {
                std::size_t j = i;
                while (j + 1 < it->second.polls.size() && it->second.polls[j + 1] <= it->second.polls[j] + 3) ++j;
                std::printf("hud: callback %s draws in polls %u..%u\n", e, it->second.polls[i], it->second.polls[j]);
                i = j + 1;
            }
    }
    std::map<std::string, int> names;
    for (const auto& kv : cap.hud.textures) {
        const auto& t = *kv.second;
        char line[256];
        if (t.ok) std::snprintf(line, sizeof line, "%-24s %4dx%-4d (TEX0 %dx%d) PSM %2d TFX %d TCC %d palette %d", t.name.c_str(), t.w, t.h, t.tw, t.th, t.psm, t.tfx, t.tcc ? 1 : 0, t.palette);
        else std::snprintf(line, sizeof line, "%-24s raster 0x%08X: not decoded: %s", t.name.c_str(), t.raster, t.why.c_str());
        names[line]++;
    }
    std::printf("hud: %zu texture versions (raster, header, palette):\n", cap.hud.textures.size());
    for (const auto& kv : names) std::printf("hud:   %s%s\n", kv.first.c_str(), kv.second > 1 ? (" x" + std::to_string(kv.second)).c_str() : "");
}

// hud4 (§17.13): the world-anchored markers (DrawPositionMarker calls): their anchoring, and how well the replay's
// camera reproduces the game's own projection of each captured point at the tick (the 640 x 448 offscreen frame's
// view-projection: the game camera with its view window). Also against the neighbouring ticks' cameras, to tell
// which tick's camera the HUD frame was drawn with.
void info_markers(Replay& r) {
    const auto& cap = r.cap;
    std::uint64_t n = 0, valid = 0, cars = 0, values = 0, matched = 0, drawn = 0, tri_checked = 0, tri_within = 0;
    double off_xz_max = 0, off_y_min = 1e9, off_y_max = -1e9, tri_err_max = 0, z_err_max = 0;
    double err[3] = {0, 0, 0}, err_max[3] = {0, 0, 0};
    std::uint64_t err_n[3] = {0, 0, 0}, within[3] = {0, 0, 0};
    std::vector<double> errs;
    const b3replay::HudFrame* last = nullptr;
    for (std::size_t i = 0; i < cap.ticks.size(); ++i) {
        const auto& t = cap.ticks[i];
        if (!t.hud || t.hud.get() == last) continue;
        last = t.hud.get();
        const auto& h = *t.hud;
        std::vector<int> has_draw(h.markers.size(), 0);
        for (const auto& d : h.draws) {
            if (d.marker < 0 || std::size_t(d.marker) >= h.markers.size()) continue;
            has_draw[std::size_t(d.marker)] = 1;
            // the near arrow: one flat untextured triangle (x - a, y - b), (x + a, y - b), (x, y + b) around the marker's
            // centre, which is `height` = aspect x size / 3 above the projected point [L draft]
            const auto& mk = h.markers[std::size_t(d.marker)];
            if (d.texture == 0 && d.tri.size() == 3 && mk.valid) {
                const float cx = d.tri[2].x, cy = 0.5f * (d.tri[0].y + d.tri[2].y);
                const float height = float(mk.sh) / float(mk.sw) * mk.k[3] * mk.k[0] * mk.size / 3.0f;
                const double te = std::hypot(cx - mk.x, cy + height - mk.y);
                tri_err_max = std::max(tri_err_max, te);
                ++tri_checked;
                tri_within += te <= 0.5;
                if (std::getenv("B3HUD_MARKER_DEBUG") && te > 0.5)
                    std::printf("marker arrow poll %u slot %u: centre %.2f %.2f height %.2f size %.2f game %.2f %.2f tri %.2f,%.2f %.2f,%.2f %.2f,%.2f\n",
                                t.poll, mk.slot, cx, cy, height, mk.size, mk.x, mk.y, d.tri[0].x, d.tri[0].y, d.tri[1].x, d.tri[1].y, d.tri[2].x, d.tri[2].y);
            }
        }
        for (std::size_t m = 0; m < h.markers.size(); ++m) {
            const auto& mk = h.markers[m];
            ++n;
            if (mk.car) ++cars; else ++values;
            if (!mk.valid) continue;
            ++valid;
            drawn += has_draw[m];
            if (mk.vehicle) {
                ++matched;
                off_xz_max = std::max(off_xz_max, double(std::hypot(mk.offset[0], mk.offset[2])));
                off_y_min = std::min(off_y_min, double(mk.offset[1]));
                off_y_max = std::max(off_y_max, double(mk.offset[1]));
            }
            if (!has_draw[m]) continue;   // culled by the game (off screen, too near / far): nothing on screen to place
            for (int k = 0; k < 3; ++k) {   // tick i - 1, i, i + 1
                const std::size_t j = i + std::size_t(k) - 1;
                if (j >= cap.ticks.size() || !cap.ticks[j].cam.valid) continue;
                const auto& c = cap.ticks[j].cam;
                float vp[16], x, y, z;
                b3replay::game_view_proj(c, c.half_w / c.half_h, vp);
                if (!b3replay::hud_project(vp, 4.0f / 3.0f, h.width, h.height, mk.world, &x, &y, &z)) continue;
                const double e = std::hypot(double(x - mk.x), double(y - mk.y));
                err[k] += e;
                err_max[k] = std::max(err_max[k], e);
                ++err_n[k];
                within[k] += e <= 0.5;
                if (k == 1) {
                    errs.push_back(e);
                    z_err_max = std::max(z_err_max, double(std::fabs(z - mk.z)));
                    if (std::getenv("B3HUD_MARKER_DEBUG") && e > 0.5)
                        std::printf("marker poll %u slot %u: error %.3f px (game %.2f %.2f z %.3f, replay %.2f %.2f z %.3f); ticks per frame %d, camera changed from the previous tick %d\n",
                                    t.poll, mk.slot, e, mk.x, mk.y, mk.z, x, y, z, t.ticks_per_frame,
                                    i > 0 && std::memcmp(&cap.ticks[i - 1].cam.m, &t.cam.m, sizeof t.cam.m) != 0 ? 1 : 0);
                }
            }
        }
    }
    if (n == 0) { std::printf("hud markers: none (captures before hud4, or no marker drawn)\n"); return; }
    std::printf("hud markers: %llu DrawPositionMarker calls (%llu over race cars, %llu vehicle values), %llu in front of the camera,"
                " %llu with draws; anchored to a vehicle %llu\n", (unsigned long long)n, (unsigned long long)cars, (unsigned long long)values,
                (unsigned long long)valid, (unsigned long long)drawn, (unsigned long long)matched);
    std::printf("hud markers: offset point - vehicle position: horizontal at most %.4f m, height %.4f .. %.4f m\n", off_xz_max,
                matched ? off_y_min : 0.0, matched ? off_y_max : 0.0);
    std::printf("hud markers: the game's projection vs the drawn arrow's centre: %llu arrows, %llu within 0.5 px, at most %.4f px\n",
                (unsigned long long)tri_checked, (unsigned long long)tri_within, tri_err_max);
    std::sort(errs.begin(), errs.end());
    const char* name[3] = {"previous tick's", "the tick's", "next tick's"};
    for (int k = 0; k < 3; ++k)
        if (err_n[k])
            std::printf("hud markers: replay projection with %s camera vs the game's (drawn markers): mean %.4f px, max %.4f px, %llu of %llu within 0.5 px%s\n",
                        name[k], err[k] / double(err_n[k]), err_max[k], (unsigned long long)within[k], (unsigned long long)err_n[k],
                        k == 1 && !errs.empty() ? (" (median " + std::to_string(errs[errs.size() / 2]) + ", 99 % " +
                                                   std::to_string(errs[std::min(errs.size() - 1, errs.size() * 99 / 100)]) + ")").c_str() : "");
    std::printf("hud markers: depth (clip w) vs the game's z: at most %.5f\n", z_err_max);
    {   // the view window the markers were projected with: viewport +0x48 / +0x4C (half width / height at distance 1)
        std::map<int, std::uint64_t> ratio;   // x 1000
        const b3replay::HudFrame* lf = nullptr;
        for (const auto& t : cap.ticks) {
            if (!t.hud || t.hud.get() == lf) continue;
            lf = t.hud.get();
            for (const auto& mk : t.hud->markers) if (mk.k[1] > 0) ++ratio[int(std::lround(1000.0 * mk.k[0] / mk.k[1]))];
        }
        for (const auto& [k, c] : ratio) std::printf("hud markers: view window +0x48 / +0x4C = %.3f in %llu markers\n", k / 1000.0, (unsigned long long)c);
    }
    // Between ticks: where the held marker (hud1-hud3: drawn as captured) is against its point in the interpolated
    // frame (the anchor car's interpolated position + offset, the interpolated camera), over the current segment; with
    // the per-frame projection (hud4) the marker is drawn at that point.
    const auto& sg = r.segment();
    for (const double a : {0.25, 0.5, 0.75}) {
        std::vector<double> d;
        for (std::size_t i = sg.first; i < sg.last; ++i) {
            const auto& t = cap.ticks[i];
            if (!t.hud || t.hud->markers.empty()) continue;
            std::vector<int> has_draw(t.hud->markers.size(), 0);
            for (const auto& dr : t.hud->draws) if (dr.marker >= 0 && std::size_t(dr.marker) < has_draw.size()) has_draw[std::size_t(dr.marker)] = 1;
            const auto f = b3replay::frame_at(r.timeline, i, a, true);
            float vp[16];
            b3replay::game_view_proj(f.cam, f.cam.half_w / f.cam.half_h, vp);
            for (std::size_t m = 0; m < t.hud->markers.size(); ++m) {
                const auto& mk = t.hud->markers[m];
                if (!has_draw[m] || !mk.vehicle) continue;
                for (const auto& v : f.vehicles) {
                    if (v.id != mk.vehicle) continue;
                    float w[3], x, y, z;
                    for (int k = 0; k < 3; ++k) w[k] = v.main.r[3][k] + mk.offset[k];
                    if (b3replay::hud_project(vp, 4.0f / 3.0f, t.hud->width, t.hud->height, w, &x, &y, &z)) {
                        d.push_back(std::hypot(double(x - mk.x), double(y - mk.y)));
                        if (a == 0.5 && std::getenv("B3HUD_MARKER_DEBUG") && d.back() > 4.0 && mk.size > 15.0f)
                            std::printf("marker drift poll %u slot %u: %.2f px at alpha 0.5 (size %.1f, at %.0f %.0f, ticks per frame %d)\n", t.poll, mk.slot,
                                        d.back(), mk.size, mk.x, mk.y, t.ticks_per_frame);
                    }
                    break;
                }
            }
        }
        if (d.empty()) continue;
        std::sort(d.begin(), d.end());
        double sum = 0;
        for (double v : d) sum += v;
        std::printf("hud markers: held marker vs its point at alpha %.2f (polls %u..%u, 640 x 448): %zu markers, mean %.3f px, median %.3f,"
                    " 95 %% %.3f, max %.3f\n", a, cap.ticks[sg.first].poll, cap.ticks[sg.last].poll, d.size(), sum / double(d.size()), d[d.size() / 2],
                    d[std::min(d.size() - 1, d.size() * 95 / 100)], d.back());
    }
}

void info_effects(Replay& r) {
    const auto& cap = r.cap;
    if (cap.version < 3) { std::printf("effects: capture version %u has none (version 3)\n", cap.version); return; }
    std::uint64_t geo_checked = 0, geo_differ = 0;
    std::set<std::uint32_t> geo_seen;
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& sg = cap.segments[si];
        std::uint64_t n = 0, strips[3] = {}, cast[3] = {}, faded[3] = {}, em = 0, em2 = 0, pts = 0, shadows_off = 0, fx_off = 0;
        int smax = 0, emax = 0, pmax = 0;
        std::size_t smax_tick = 0, pmax_tick = 0;
        std::map<int, std::uint64_t> per_type;
        for (std::size_t i = sg.first; i <= sg.last; ++i) {
            const auto& t = cap.ticks[i];
            if (!t.effects_valid) continue;
            ++n;
            shadows_off += !t.shadows_on;
            fx_off += !t.effects_on;
            int s = 0;
            for (const auto& v : t.vehicles) {
                const int k = int(v.kind);
                if (!v.shadow.cast) continue;
                ++cast[k];
                b3gfx::Vertex q[8];
                if (t.shadows_on && b3replay::shadow_strip(v.shadow, t.shadow_k, q)) { ++strips[k]; ++s; } else ++faded[k];
                if (geo_seen.insert(v.data).second && v.model >= 0) {
                    const int f = r.view.car_file(cap.heads[std::size_t(v.model)]);
                    if (f < 0) continue;
                    const auto& b = r.view.cars[std::size_t(f)].bytes;
                    auto ff = [&](std::size_t o) { float x = 0; if (o + 4 <= b.size()) std::memcpy(&x, b.data() + o, 4); return x; };
                    const auto& h = v.shadow;
                    const int nw = cap.heads[std::size_t(v.model)][0x0D];
                    ++geo_checked;
                    if (ff(0x18) != h.radius || ff(0xE80) != h.sup[0] || ff(0xE88) != h.sup[2] || ff(0xE90) != h.inf[0] ||
                        ff(0xE98) != h.inf[2] || ff(0xBB0 + 0x40 + 8) != h.front_z || ff(0xBB0 + 0x40 * std::size_t(nw - 2) + 8) != h.rear_z)
                        ++geo_differ;
                }
            }
            if (s > smax) { smax = s; smax_tick = i; }
            int e2 = 0, p = 0;
            for (const auto& e : t.trails) {
                ++em;
                if (e->count >= 2) { ++e2; p += e->count; ++per_type[e->type]; }
            }
            em2 += std::uint64_t(e2);
            pts += std::uint64_t(p);
            emax = std::max(emax, e2);
            if (p > pmax) { pmax = p; pmax_tick = i; }
        }
        if (n == 0) continue;
        std::printf("effects segment %zu: %llu ticks; shadows off %llu, effects off %llu ticks\n", si, static_cast<unsigned long long>(n),
                    static_cast<unsigned long long>(shadows_off), static_cast<unsigned long long>(fx_off));
        const char* kn[3] = {"race cars", "traffic", "crashed"};
        for (int k = 0; k < 3; ++k)
            std::printf("  shadows %-9s: cast %llu vehicle-ticks, strips drawn %llu (%.2f per tick), none (> 1.25 m up) %llu\n", kn[k],
                        static_cast<unsigned long long>(cast[k]), static_cast<unsigned long long>(strips[k]), double(strips[k]) / double(n),
                        static_cast<unsigned long long>(faded[k]));
        std::printf("  shadow strips per tick: mean %.2f, max %d (poll %u)\n", double(strips[0] + strips[1] + strips[2]) / double(n), smax,
                    cap.ticks[smax_tick].poll);
        std::printf("  skid emitters listed %.2f per tick, drawn (>= 2 points) %.2f, points %.1f per tick; max %d emitters, %d points (poll %u)\n",
                    double(em) / double(n), double(em2) / double(n), double(pts) / double(n), emax, pmax, cap.ticks[pmax_tick].poll);
        for (const auto& [ty, c] : per_type)
            std::printf("    type %d ('%s', ALPHA %08x): %.2f drawn emitters per tick\n", ty, cap.trail_texture[ty].c_str(), cap.trail_alpha[ty],
                        double(c) / double(n));
    }
    std::printf("shadow geometry vs car files: %llu vehicle data checked, %llu differ (radius +0x18, box +0xE80/+0xE90, wheel z)\n",
                static_cast<unsigned long long>(geo_checked), static_cast<unsigned long long>(geo_differ));
}

// Contact effects (fx2, §19): per segment the scrape strips the game draws (the view's race car +0x18EA gate, listed
// emitters with >= 2 segments in the drawn buffer), and the sparks alive per tick (records newer than their ring's
// lifetime, CB3SparkArray ring slot +0xC) with the ticks where the capture holds the whole camera history.
void info_contact(Replay& r) {
    const auto& cap = r.cap;
    auto f32 = [](const std::uint8_t* p) { float f; std::memcpy(&f, p, 4); return f; };
    auto u32 = [](const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; };
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& sg = cap.segments[si];
        std::uint64_t n = 0, gate = 0, drawn_ticks = 0, emit = 0, segs = 0, listed = 0, spark_n = 0, live = 0, hist = 0, alive = 0, crash_alive = 0;
        std::uint64_t popcorn_spawns = 0, popcorn_n = 0;
        int emax = 0, amax = 0;
        std::uint32_t emax_poll = 0, amax_poll = 0;
        std::uint32_t pc_idx[4] = {}; bool pc_first = true;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> runs, sruns;
        auto add_run = [](std::vector<std::pair<std::uint32_t, std::uint32_t>>& v, std::uint32_t p) {
            if (!v.empty() && v.back().second + 1 >= p) v.back().second = p; else v.push_back({p, p});
        };
        for (std::size_t i = sg.first; i <= sg.last; ++i) {
            const auto& t = cap.ticks[i];
            const auto& c = t.contact;
            if (c.scrapes_valid) {
                ++n;
                listed += c.scrapes.size();
                if (c.scrape_gate) {
                    ++gate;
                    int e = 0;
                    for (const auto& s : c.scrapes) if (s->count >= 2) { ++e; segs += std::uint64_t(s->count); }
                    emit += std::uint64_t(e);
                    if (e > 0) { ++drawn_ticks; add_run(runs, t.poll); }
                    if (e > emax) { emax = e; emax_poll = t.poll; }
                }
            }
            if (c.sparks_valid) {
                ++spark_n;
                live += c.history_valid;
                int a_t = 0;
                for (int a = 0; a < 6; ++a) {
                    if (!c.sparks[a]) continue;
                    const auto& sa = *c.sparks[a];
                    for (int b = 0; b < 2; ++b)
                        for (int k = 0; k < 4; ++k) {
                            const float life = f32(sa.head + 0x4C * b + 0x10 * k + 0xC);
                            const auto& rec = sa.rec[b][k];
                            for (std::size_t q = 0; q + 0x90 <= rec.size(); q += 0x90)
                                for (int j = 0; j < 4; ++j) {
                                    const float st = f32(rec.data() + q + 4 * j);
                                    if (st != 0.0f && st > t.now - life && st <= t.now) { ++a_t; if (b) ++crash_alive; }
                                }
                        }
                }
                alive += std::uint64_t(a_t);
                if (a_t > 0) add_run(sruns, t.poll);
                if (a_t > amax) { amax = a_t; amax_poll = t.poll; }
                hist += c.history_valid;
            }
            if (c.popcorn_valid) {
                ++popcorn_n;
                for (int a = 0; a < 4; ++a) {
                    if (!c.popcorn[a]) continue;
                    const std::uint32_t w = u32(c.popcorn[a]->head + 0x28);
                    if (!pc_first && w != pc_idx[a]) ++popcorn_spawns;
                    pc_idx[a] = w;
                }
                pc_first = false;
            }
        }
        if (n == 0 && spark_n == 0) { std::printf("contact segment %zu: no scrape / spark state (a capture before fx2)\n", si); continue; }
        std::printf("contact segment %zu: scrapes: %llu ticks, gate (view car +0x18EA) on %llu, drawn in %llu; %.2f listed emitters per tick,"
                    " %.2f drawn strips / %.2f segments per gated tick, max %d strips (poll %u)\n", si, static_cast<unsigned long long>(n),
                    static_cast<unsigned long long>(gate), static_cast<unsigned long long>(drawn_ticks), n ? double(listed) / double(n) : 0.0,
                    gate ? double(emit) / double(gate) : 0.0, gate ? double(segs) / double(gate) : 0.0, emax, emax_poll);
        std::printf("  scrapes drawn at polls");
        for (std::size_t k = 0; k < runs.size() && k < 16; ++k) std::printf(" %u..%u", runs[k].first, runs[k].second);
        std::printf("%s\n", runs.size() > 16 ? " ..." : "");
        std::printf("  sparks: %llu ticks, %llu with a renderable array (camera history captured %llu); alive per tick %.2f (crash banks %.2f), max %d (poll %u)\n",
                    static_cast<unsigned long long>(spark_n), static_cast<unsigned long long>(live), static_cast<unsigned long long>(hist),
                    spark_n ? double(alive) / double(spark_n) : 0.0, spark_n ? double(crash_alive) / double(spark_n) : 0.0, amax, amax_poll);
        std::printf("  sparks alive at polls");
        for (std::size_t k = 0; k < sruns.size() && k < 16; ++k) std::printf(" %u..%u", sruns[k].first, sruns[k].second);
        std::printf("%s (%zu runs)\n", sruns.size() > 16 ? " ..." : "", sruns.size());
        std::printf("  popcorn: %llu ticks, %llu write-index changes\n", static_cast<unsigned long long>(popcorn_n), static_cast<unsigned long long>(popcorn_spawns));
    }
}

// Coronas (corona1, §16): per segment the slots the game writes per tick (rebuilt from the captured inputs at the tick,
// b3replay/coronas.h) by source, the sprites the program draws for them from the tick's camera, and the boost glow:
// race-car ticks with the boost intensity > 0, and the polls where it shows.
int t0k(const b3replay::Capture& cap, int a) { return cap.corona_k ? cap.corona_k->capacity[a] : 0; }
void info_coronas(Replay& r) {
    const auto& cap = r.cap;
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& sg = cap.segments[si];
        std::uint64_t n = 0, slots[3] = {}, sprites[3] = {}, by_src[6] = {}, boosting = 0, red = 0, dropped = 0, fx_off = 0;
        int mx = 0, smax[3] = {};
        std::size_t mx_tick = 0;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;   // boost runs (first poll, last poll) of any car
        for (std::size_t i = sg.first; i <= sg.last; ++i) {
            const auto& t = cap.ticks[i];
            if (!t.coronas.valid || !t.coronas.k) continue;
            ++n;
            fx_off += !t.effects_on;
            b3replay::CoronaArrays a;
            b3replay::build_coronas(t.coronas, t.vehicles, t.coronas.cam_point, t.cam.lod_pos, a);
            int s = 0;
            for (int k = 0; k < 3; ++k) {
                slots[k] += a.slot[k].size();
                smax[k] = std::max(smax[k], int(a.slot[k].size()));
                dropped += std::uint64_t(a.dropped[k]);
                for (std::size_t j = 0; j < a.slot[k].size(); ++j) {
                    ++by_src[int(a.src[k][j])];
                    b3replay::CoronaQuad q;
                    if (b3replay::corona_quad(t.cam, a.slot[k][j], q)) { ++sprites[k]; ++s; }
                }
            }
            if (s > mx) { mx = s; mx_tick = i; }
            bool any = false;
            for (const auto& v : t.vehicles)
                if (v.kind == b3replay::VehicleKind::kRaceCar && v.corona.valid && v.corona.boost > 0) {
                    ++boosting;
                    red += v.corona.red_boost;
                    any = true;
                }
            if (any) {
                if (!runs.empty() && runs.back().second + 1 >= t.poll) runs.back().second = t.poll;
                else runs.push_back({t.poll, t.poll});
            }
        }
        if (n == 0) { std::printf("coronas segment %zu: no corona inputs (a capture before corona1)\n", si); continue; }
        std::printf("coronas segment %zu: %llu ticks (effects off %llu); slots per tick: glow %.2f, boost %.2f, boost red %.2f (dropped, array full: %llu)\n",
                    si, static_cast<unsigned long long>(n), static_cast<unsigned long long>(fx_off), double(slots[0]) / double(n),
                    double(slots[1]) / double(n), double(slots[2]) / double(n), static_cast<unsigned long long>(dropped));
        std::printf("  most slots in a tick: glow %d of %d, boost %d of %d, boost red %d of %d\n", smax[0], t0k(cap, 0), smax[1], t0k(cap, 1), smax[2], t0k(cap, 2));
        const char* src[6] = {"traffic lights", "crashed lights", "traffic signals", "unassigned signals", "race-car lights", "boost glow"};
        for (int k = 0; k < 6; ++k) std::printf("  %-19s %.2f slots per tick\n", src[k], double(by_src[k]) / double(n));
        std::printf("  sprites drawn per tick (the VU program's cull, fade and threshold, tick camera): glow %.2f, boost %.2f, red %.2f; max %d (poll %u)\n",
                    double(sprites[0]) / double(n), double(sprites[1]) / double(n), double(sprites[2]) / double(n), mx, cap.ticks[mx_tick].poll);
        std::printf("  boost: %llu race-car ticks with intensity > 0 (%llu red-boost cars); boost shown at polls", static_cast<unsigned long long>(boosting),
                    static_cast<unsigned long long>(red));
        for (std::size_t k = 0; k < runs.size() && k < 16; ++k) std::printf(" %u..%u", runs[k].first, runs[k].second);
        std::printf("%s\n", runs.size() > 16 ? " ..." : "");
    }
}

// Sun corona (water1, §21): per segment the route's sun (ENVIRO.DAT, Prepare's skip list), SetupCamera's inputs, and
// the polls where the game draws the corona: SetupCamera's projection of camera + 50 x to-sun through the captured
// record +0xE0 (z > 0.01) and OcclusionTest's screen test (within 6.5 px), the same arithmetic as the game's [L draft].
void info_sun(Replay& r) {
    const auto& cap = r.cap;
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        const std::string route = r.segment_route(si);
        if (route.empty()) continue;
        std::vector<std::uint8_t> d;
        b3asset::Enviro env;
        b3asset::GsMemory gs;
        if (!b3asset::read_file(route + "/ENVIRO.DAT", d) || !b3asset::read_enviro(b3asset::Bytes{d.data(), d.size()}, gs, env)) continue;
        const bool has = b3asset::route_has_sun(route) && !env.sun_corona.levels.empty();
        const b3replay::SunCam& c0 = cap.ticks[s.first].sun;
        std::printf("sun segment %zu (%s): %s; corona scale %.3f x graphics manager +0 %d = radius %.1f px; record: size %.0f x %.0f, "
                    "offset (%.1f, %.1f), +0x200 %.4f +0x208 %.4f +0x20C %.4f -> aspect %.4f\n", si, route.c_str(),
                    has ? "drawn" : "none (Prepare skips the route or no corona raster)", double(env.corona_scale), c0.gm0,
                    double(env.corona_scale * float(c0.gm0)), double(c0.size[0]), double(c0.size[1]), double(c0.offset[0]),
                    double(c0.offset[1]), double(c0.f200), double(c0.f208), double(c0.f20C),
                    c0.size[0] > 0 ? double(c0.f200 * c0.f20C * c0.size[1] / c0.size[0]) : 0.0);
        if (!has) continue;
        const float l = std::sqrt(env.to_sun[0] * env.to_sun[0] + env.to_sun[1] * env.to_sun[1] + env.to_sun[2] * env.to_sun[2]);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
        long on = 0, n = 0;
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const b3replay::SunCam& c = cap.ticks[i].sun;
            if (!c.valid) continue;
            ++n;
            float p[3], q[3];
            for (int k = 0; k < 3; ++k) p[k] = c.pos[k] + 50.0f * env.to_sun[k] / l;
            for (int k = 0; k < 3; ++k) q[k] = c.m[0][k] * p[0] + c.m[1][k] * p[1] + c.m[2][k] * p[2] + c.m[3][k];
            // CGtMatrix3x4 rows X, Y, Z, T as stored (4 floats each, the 4th unused)
            if (q[2] <= 0.01f) continue;
            const float x = q[0] / q[2] * c.size[0], y = q[1] / q[2] * c.size[1];
            if (!(x > -6.5f && y > -6.5f && x < 6.5f + c.size[0] && y < 6.5f + c.size[1])) continue;
            ++on;
            const std::uint32_t poll = cap.ticks[i].poll;
            if (!runs.empty() && runs.back().second + 1 >= poll) runs.back().second = poll;
            else runs.push_back({poll, poll});
        }
        std::printf("  the corona is drawn at %ld of %ld ticks; polls", on, n);
        (void)0;
        for (std::size_t k = 0; k < runs.size() && k < 24; ++k) std::printf(" %u..%u", runs[k].first, runs[k].second);
        std::printf("%s\n", runs.size() > 24 ? " ..." : "");
    }
}

// The boost radial blur (water1, §21): the captured CB3RadialBlurParams per segment.
void info_blur(Replay& r) {
    const auto& cap = r.cap;
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        long n = 0, on = 0, off = 0, crashed = 0, flip = 0;
        float amax = 0;
        std::uint32_t pmax = 0;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const auto& b = cap.ticks[i].blur;
            if (!b.valid) continue;
            ++n;
            off += !b.on;
            crashed += b.crashed;
            flip += b.flip;
            if (b.amount > 0) {
                ++on;
                const std::uint32_t poll = cap.ticks[i].poll;
                if (!runs.empty() && runs.back().second + 1 >= poll) runs.back().second = poll;
                else runs.push_back({poll, poll});
            }
            if (b.amount > amax) { amax = b.amount; pmax = cap.ticks[i].poll; }
        }
        std::printf("radial blur segment %zu: params in %ld ticks; amount > 0 in %ld (max %.3f at poll %u), switch off %ld, car crashed %ld, flip %ld\n",
                    si, n, on, double(amax), pmax, off, crashed, flip);
        if (pmax) {
            std::size_t i = s.first;
            while (i < s.last && cap.ticks[i].poll < pmax) ++i;
            const auto& b = cap.ticks[i].blur;
            std::printf("  at poll %u: centre (%.3f, %.3f) radius (%.3f, %.3f) uv scale (%.4f, %.4f) colour %.3f amount %.3f texture %08x\n", pmax,
                        double(b.centre[0]), double(b.centre[1]), double(b.radius[0]), double(b.radius[1]), double(b.uv_scale[0]),
                        double(b.uv_scale[1]), double(b.colour), double(b.amount), b.texture);
        }
        std::printf("  amount > 0 at polls");
        for (std::size_t k = 0; k < runs.size() && k < 20; ++k) std::printf(" %u..%u", runs[k].first, runs[k].second);
        std::printf("%s\n", runs.size() > 20 ? " ..." : "");
    }
}

// The post passes (post1, §25): per segment the blur CB3World::Render draws (UpdateCamera's decision from post1's
// inputs), the bloom's params, and two checks of the reading: a tick UpdateCamera returned 0 has amount 0 (its first
// store), and the motion branch recomputed from the previous call's point / time and this tick's camera
// (b3asset::directional_motion) against the captured centre / zoom / amount.
void info_post(Replay& r) {
    const auto& cap = r.cap;
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        long n = 0, inputs = 0, branch[3] = {}, choice[3] = {}, bad0 = 0, dir_drawn = 0, radial_drawn = 0, calls = 0;
        long checked = 0, same_drawn = 0, mism = 0, bloom_on = 0, bloom_reset = 0, bloom_freeze = 0;
        double ec = 0, ez = 0, ea = 0;
        std::map<int, long> thresholds, intensities;   // x 1000
        float fb_min = 1e9f, fb_max = -1e9f;
        long fb_hi = 0;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
        std::uint32_t worst_poll = 0;
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const b3replay::Tick& t = cap.ticks[i];
            const b3replay::PostIn& p = t.post;
            if (!p.valid) continue;
            ++n;
            bloom_on += p.bloom_on;
            bloom_freeze += p.bloom_freeze;
            ++thresholds[int(std::lround(p.bloom_threshold * 1000))];
            ++intensities[int(std::lround(p.bloom_intensity * 1000))];
            fb_min = std::fmin(fb_min, p.bloom_feedback);
            fb_max = std::fmax(fb_max, p.bloom_feedback);
            fb_hi += p.bloom_feedback > 0.8999f;
            if (!p.inputs) continue;
            ++inputs;
            bloom_reset += p.bloom_reset;
            ++branch[p.branch];
            ++choice[p.choice];
            if (p.branch == 0 && p.dir_amount != 0) ++bad0;
            const bool dd = p.choice == 1 && p.dir_on && p.dir_amount >= 0.003921568859f;
            dir_drawn += dd;
            radial_drawn += p.choice == 2 && t.blur.valid && t.blur.on && t.blur.amount > 0;
            if (dd) {
                if (!runs.empty() && runs.back().second + 1 >= t.poll) runs.back().second = t.poll;
                else runs.push_back({t.poll, t.poll});
            }
            if (i == s.first) continue;
            const b3replay::PostIn& q = cap.ticks[i - 1].post;
            if (!q.valid || q.dir_time == p.dir_time) continue;   // no UpdateCamera call between the two polls
            ++calls;
            if (p.branch != 2 || !t.cam.valid) continue;
            const float k = 0.65f * (1.0f - (p.boost_20 - 1.0f) * (p.boost_20 - 1.0f)) * (1.0f - (p.boost_1c - 1.0f) * (p.boost_1c - 1.0f));
            const bool scale = p.boost_on != 0 && (p.flags & 0x24) != 0;
            const b3asset::DirBlur m = b3asset::directional_motion(q.dir_point, q.dir_time, p.dir_time, t.cam.m.r, scale, k);
            ++checked;
            if (m.drawn != (p.dir_amount != 0)) { ++mism; continue; }
            if (!m.drawn) { ++same_drawn; continue; }
            ++same_drawn;
            const double c = std::fmax(std::fabs(m.centre[0] - p.dir_centre[0]), std::fabs(m.centre[1] - p.dir_centre[1])) /
                             std::fmax(1.0, std::fabs(double(p.dir_centre[0])) + std::fabs(double(p.dir_centre[1])));
            if (c > ec) { ec = c; worst_poll = t.poll; }
            ez = std::fmax(ez, std::fabs(double(m.zoom) - p.dir_zoom[0]));
            ea = std::fmax(ea, std::fabs(double(m.amount) - p.dir_amount));
        }
        if (!n) continue;
        std::printf("post segment %zu: %ld ticks; bloom on %ld, freeze %ld, reset %ld; threshold", si, n, bloom_on, bloom_freeze, bloom_reset);
        for (const auto& [k, c] : thresholds) std::printf(" %.3f x %ld", k / 1000.0, c);
        std::printf(", intensity");
        for (const auto& [k, c] : intensities) std::printf(" %.3f x %ld", k / 1000.0, c);
        std::printf(", feedback %.3f..%.3f (> 0.8999 in %ld ticks)\n", double(fb_min), double(fb_max), fb_hi);
        if (!inputs) { std::printf("  no UpdateCamera inputs (a capture before post1): the blur choice is not decidable\n"); continue; }
        std::printf("  UpdateCamera inputs in %ld ticks: returned 0 %ld, crash pulse %ld, motion %ld; CB3World::Render takes neither %ld, the directional blur %ld"
                    " (drawn, amount >= 1/255: %ld), the radial blur %ld (drawn: %ld); returned 0 with amount != 0: %ld\n",
                    inputs, branch[0], branch[1], branch[2], choice[0], choice[1], dir_drawn, choice[2], radial_drawn, bad0);
        std::printf("  motion recomputed at %ld of %ld calls: drawn / not drawn agree %ld, disagree %ld; max |centre| error %.2e (relative, poll %u), zoom %.2e, amount %.2e\n",
                    checked, calls, same_drawn, mism, ec, worst_poll, ez, ea);
        std::printf("  directional blur drawn at polls");
        for (std::size_t k = 0; k < runs.size() && k < 20; ++k) std::printf(" %u..%u", runs[k].first, runs[k].second);
        std::printf("%s\n", runs.size() > 20 ? " ..." : "");
    }
}

int run_info(Replay& r) {
    const auto& cap = r.cap;
    std::printf("capture %s: %zu ticks (polls %u..%u), %llu runs, %.1f MB of guest bytes\n", r.args.capture.c_str(), cap.ticks.size(),
                cap.ticks.front().poll, cap.ticks.back().poll, static_cast<unsigned long long>(cap.runs), cap.bytes / 1048576.0);
    for (std::size_t i = 0; i < cap.fe_segments.size(); ++i) {   // menu1 (§22)
        const auto& s = cap.fe_segments[i];
        std::printf("front-end segment %zu: ticks %zu..%zu (polls %u..%u, %.1f s)\n", i, s.first, s.last, cap.ticks[s.first].poll,
                    cap.ticks[s.last].poll, (s.last - s.first + 1) / 60.0);
        // what the scene capture holds there (menu1): camera, race cars, movie objects / pictures, per run of equal kinds
        std::string prev;
        std::uint32_t from = 0;
        for (std::size_t j = s.first; j <= s.last + 1; ++j) {
            std::string k;
            if (j <= s.last) {
                const auto& t = cap.ticks[j];
                int cars = 0;
                for (const auto& v : t.vehicles) cars += v.kind == b3replay::VehicleKind::kRaceCar;
                std::size_t vids = 0, pics = 0;
                if (t.hud) for (const auto& hv : t.hud->videos) { ++vids; pics += hv.picture != nullptr; }
                char b[160];
                std::snprintf(b, sizeof b, "camera %d, track %d, vehicles %zu (race cars %d), movies %zu (pictures %zu)", t.cam.valid ? 1 : 0,
                              t.track_on ? 1 : 0, t.vehicles.size(), cars, vids, pics);
                k = b;
            }
            if (k != prev) {
                if (!prev.empty()) std::printf("  polls %u..%u: %s\n", from, cap.ticks[j - 1].poll, prev.c_str());
                prev = k;
                if (j <= s.last) from = cap.ticks[j].poll;
            }
        }
    }
    for (std::size_t i = 0; i < cap.segments.size(); ++i) {
        const auto& s = cap.segments[i];
        const std::string route = r.segment_route(i);
        std::printf("segment %zu: ticks %zu..%zu (polls %u..%u, %.1f s), route size %u units %d -> %s\n", i, s.first, s.last,
                    cap.ticks[s.first].poll, cap.ticks[s.last].poll, (s.last - s.first + 1) / 60.0, s.route_size, s.route_units,
                    route.empty() ? "(no match)" : route.c_str());
    }
    std::printf("%zu car files scanned\n", r.view.cars.size());
    for (const auto& [addr, head] : cap.models) {
        int n = 0;
        const int f = b3replay::match_car(r.view.cars, head, &n);
        const auto rn = cap.model_raster.find(addr);
        std::printf("  vehicle data %08x raster '%s' -> %s%s\n", addr, rn == cap.model_raster.end() ? "" : rn->second.c_str(),
                    f < 0 ? "(no file)" : r.view.cars[std::size_t(f)].path.c_str(), n > 1 ? " (ambiguous)" : "");
    }
    // Paint census (live2, docs/native-renderer.md §20): vehicle-ticks per segment, kind, car file and paint index (+0x59),
    // with the CLUT CB3VehicleRenderer::SetTexture picks (b3asset::palette_cbp_add: the raster's table at +0x90, the base
    // past its count) against the rule before live2 (palOffset + 4 x (paint mod count)). Traffic: drawn ticks only.
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        std::map<std::tuple<int, int, int>, long> census;   // (kind, file, paint) -> vehicle-ticks
        for (std::size_t i = s.first; i <= s.last; ++i)
            for (const auto& v : cap.ticks[i].vehicles) {
                if (v.model < 0 || std::size_t(v.model) >= cap.heads.size()) continue;
                if (v.kind == b3replay::VehicleKind::kTraffic && v.cull != b3replay::Vehicle::kDrawn) continue;
                ++census[{int(v.kind), r.view.car_file(cap.heads[std::size_t(v.model)]), int(v.paint)}];
            }
        long ticks_all = 0, ticks_wrong = 0;
        std::printf("segment %zu paint census (kind file paint: vehicle-ticks, palettes, CLUT block game / before live2):\n", si);
        for (const auto& [k, n] : census) {
            const auto [kind, file, paint] = k;
            if (file < 0) continue;
            const auto& bytes = r.view.cars[std::size_t(file)].bytes;
            const b3asset::Bytes f{bytes.data(), bytes.size()};
            const std::uint32_t tex = f.u32(0x60);
            const int npal = tex ? std::max<int>(1, f.p[tex + 0xA0]) : 1;
            const std::uint32_t game = tex ? b3asset::palette_cbp_add(f, tex, paint) : 0, before = 4u * std::uint32_t(paint % npal);
            ticks_all += n;
            if (game != before) ticks_wrong += n;
            std::printf("  %-7s %-24s paint %d: %7ld ticks, %d palettes, CLUT +%u / +%u%s\n",
                        kind == int(b3replay::VehicleKind::kRaceCar) ? "car" : kind == int(b3replay::VehicleKind::kTraffic) ? "traffic" : "crashed",
                        r.view.cars[std::size_t(file)].path.substr(r.view.cars[std::size_t(file)].path.size() > 14 ? r.view.cars[std::size_t(file)].path.size() - 14 : 0).c_str(),
                        paint, n, npal, game, before, game != before ? "  WRONG before live2" : "");
        }
        std::printf("segment %zu paint census: %ld of %ld vehicle-ticks had another CLUT before live2 (%.1f %%)\n", si, ticks_wrong, ticks_all,
                    100.0 * double(ticks_wrong) / double(std::max(1L, ticks_all)));
    }
    // Track slots (live2 pop-in search): per segment, ticks where one unit sits in several of the 17 main-camera slots
    // (high and LOD at once; draw_list keeps the first slot's), where the camera unit has no high slot, and per unit the
    // ticks it was drawn LOD while the camera was within 1 unit-step of it.
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        long dup = 0, dup_mixed = 0, dup_lfirst = 0, cur_nohigh = 0, cur_none = 0, n = 0;
        std::map<int, long> lfirst_units;
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const auto& t = cap.ticks[i];
            if (!t.track_on) continue;
            ++n;
            std::map<int, std::vector<int>> at;
            for (int j = 0; j < 17; ++j) if (t.slot_unit[j] >= 0) at[t.slot_unit[j]].push_back(j);
            bool d = false, m = false, lf = false;
            for (const auto& [u, js] : at) {
                if (js.size() < 2) continue;
                d = true;
                bool hh = false, ll = false;
                for (int j : js) (t.slot_kind[j] == 0 ? hh : ll) = true;
                if (hh && ll) { m = true; if (t.slot_kind[js[0]] != 0) { lf = true; ++lfirst_units[u]; } }
            }
            dup += d; dup_mixed += m; dup_lfirst += lf;
            if (t.current_unit >= 0) {
                const auto it = at.find(t.current_unit);
                if (it == at.end()) ++cur_none;
                else { bool h = false; for (int j : it->second) h = h || t.slot_kind[j] == 0; cur_nohigh += !h; }
            }
        }
        std::printf("segment %zu slots: %ld track ticks; a unit in 2+ slots %ld, high and LOD at once %ld (LOD slot first %ld); camera unit in no"
                    " slot %ld, only LOD %ld\n", si, n, dup, dup_mixed, dup_lfirst, cur_none, cur_nohigh);
        for (const auto& [u, c] : lfirst_units) std::printf("   unit %d: LOD slot before its high slot in %ld ticks\n", u, c);
    }
    // Checks over every segment tick: (1) the game's own frustum results for race cars (+0x5B, 0/1/2 from
    // FrustumTestDamagedVehicle) against our camera model; (2) the camera inside its current unit's quad; (3) the ticks
    // per frame seen.
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        long conf[3][3] = {}, quad_in = 0, quad_n = 0;
        std::map<int, long> tpf;
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const auto& t = cap.ticks[i];
            ++tpf[t.ticks_per_frame];
            for (const auto& v : t.vehicles) {
                if (v.kind != b3replay::VehicleKind::kRaceCar || v.frustum < 0 || v.frustum > 2 || !(v.radius > 0)) continue;
                ++conf[v.frustum][frustum_class(t.cam, v.main.r[3], v.radius)];
            }
            (void)quad_in; (void)quad_n;
        }
        std::printf("segment %zu ticks per frame:", si);
        for (auto [k, n] : tpf) std::printf(" %d x%ld", k, n);
        std::printf("\nsegment %zu race cars: the game's frustum result (+0x5B) per row, our camera model's class per column\n"
                    "   %-18s %8s %8s %8s\n", si, "", "outside", "inside", "crossing");
        for (int g = 0; g < 3; ++g)
            std::printf("   %-18s %8ld %8ld %8ld\n", g == 0 ? "game 0 (outside)" : g == 1 ? "game 1 (crossing)" : "game 2 (inside)",
                        conf[g][0], conf[g][1], conf[g][2]);
    }
    // render5: the car shading inputs per segment: the LOD the game's Draw code picks (by kind), the SH coefficients the
    // capture holds, glass states and wheel blur lists (RenderVehicle's |speed| > 25 / > 50).
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        long lods[3][4] = {}, glass[3] = {}, blur[3] = {}, shn[10] = {};
        float l00[2] = {1e30f, -1e30f}, emin = 1e30f, emax = -1e30f, scale[2] = {1e30f, -1e30f};
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const auto& t = cap.ticks[i];
            scale[0] = std::min(scale[0], t.cam.lod_scale);
            scale[1] = std::max(scale[1], t.cam.lod_scale);
            for (const auto& v : t.vehicles) {
                const int k = v.kind == b3replay::VehicleKind::kRaceCar ? 0 : v.kind == b3replay::VehicleKind::kTraffic ? 1 : 2;
                ++lods[k][b3replay::game_lod(t.cam, v)];
                ++shn[std::min(9, v.sh_count)];
                if (v.sh_count >= 1) { l00[0] = std::min(l00[0], v.sh[0]); l00[1] = std::max(l00[1], v.sh[0]); }
                if (v.sh_count == 9) {
                    // E(n) over the 26 axis / diagonal directions (R&H irradiance, docs/car-shading.md §2)
                    for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y) for (int z = -1; z <= 1; ++z) {
                        if (!x && !y && !z) continue;
                        const float l = std::sqrt(float(x * x + y * y + z * z)), n[3] = {x / l, y / l, z / l};
                        const float E = b3view::car_irradiance(v.sh, n);
                        emin = std::min(emin, E);
                        emax = std::max(emax, E);
                    }
                }
                if (v.parts) {
                    glass[std::min<int>(2, v.glass)]++;
                    for (int w = 0; w < v.nwheel; ++w) blur[std::fabs(v.wheel_speed[w]) > 50 ? 2 : std::fabs(v.wheel_speed[w]) > 25 ? 1 : 0]++;
                }
            }
        }
        // RGB lanes the VU1 program would wrap (FTOI0, low 8 bits): E(n) < 0 or 128.1 E >= 256, over every vertex of the
        // race cars' LOD 3 meshes with the captured SH, every 30th tick (lighting scale 1)
        long lanes = 0, neg = 0, over = 0;
        std::map<int, b3asset::Model> models;
        for (std::size_t i = s.first; i <= s.last; i += 30)
            for (const auto& v : cap.ticks[i].vehicles) {
                if (v.kind != b3replay::VehicleKind::kRaceCar || v.sh_count < 9) continue;
                if (v.model < 0 || std::size_t(v.model) >= cap.heads.size()) continue;
                const int file = r.view.car_file(cap.heads[std::size_t(v.model)]);
                if (file < 0) continue;
                auto it = models.find(file);
                if (it == models.end()) {
                    b3asset::GsMemory gs;
                    b3asset::CarOptions co;
                    co.bake_frames = false;
                    const auto& cf = r.view.cars[std::size_t(file)];
                    it = models.emplace(file, b3asset::Model{}).first;
                    if (!b3asset::read_car({cf.bytes.data(), cf.bytes.size()}, co, gs, it->second)) continue;
                }
                float W[4][4];
                b3asset::car_irradiance_matrix(v.sh, W);
                const float k = b3asset::car_irradiance_k(W, v.body.r[2]);
                for (const auto& d : it->second.draws) {
                    if (d.part != "L0") continue;   // the body, lit through the body matrix
                    for (std::uint32_t q = d.first_index; q < d.first_index + d.index_count; ++q) {
                        const auto& vx = it->second.vertices[it->second.indices[q]];
                        float n[3], nn = 0;
                        for (int c = 0; c < 3; ++c) {
                            n[c] = vx.normal[0] * v.body.r[0][c] + vx.normal[1] * v.body.r[1][c] + vx.normal[2] * v.body.r[2][c];
                            nn += vx.normal[c] * vx.normal[c];
                        }
                        const float n1[4] = {n[0], n[1], n[2], 1};
                        float E = k * (nn - 1);
                        for (int a = 0; a < 4; ++a) for (int b = 0; b < 4; ++b) E += n1[a] * W[a][b] * n1[b];
                        const int lane = int(128.1f * E);
                        ++lanes;
                        neg += lane < 0;
                        over += lane > 255;
                    }
                }
            }
        const auto& c0 = cap.ticks[s.first].cam;
        std::printf("segment %zu: race-car body vertex lanes (LOD 3, every 30th tick, indexed): %ld, would wrap below 0: %ld (%.3f %%), above 255: %ld\n",
                    si, lanes, neg, 100.0 * neg / std::max(1L, lanes), over);
        std::printf("segment %zu cars (render5): viewport +0x90 LOD scale %.3f..%.3f, distances %.0f %.0f %.0f / %.0f %.0f m\n", si,
                    scale[0], scale[1], c0.lod_dist[0], c0.lod_dist[1], c0.lod_dist[2], c0.lod_dist[3], c0.lod_dist[4]);
        const char* kn[3] = {"race cars", "traffic", "crashed"};
        for (int k = 0; k < 3; ++k)
            std::printf("   %-10s vehicle-ticks per game LOD: 3: %ld  2: %ld  1: %ld  0: %ld\n", kn[k], lods[k][3], lods[k][2], lods[k][1], lods[k][0]);
        std::printf("   SH coefficients captured per vehicle-tick: 9: %ld, 1: %ld, 0: %ld; L00 %.3f..%.3f; race cars' E(n) %.3f..%.3f\n",
                    shn[9], shn[1], shn[0], l00[0], l00[1], emin, emax);
        std::printf("   glass states (vehicles with parts): intact %ld, cracked %ld, broken %ld; wheel lists B0 %ld, B1 %ld, B2 %ld\n",
                    glass[0], glass[1], glass[2], blur[0], blur[1], blur[2]);
    }
    // render6: CB3TrafficSystem::Draw's choice per vehicle-tick (game_draws), checked against the game's own trace of it:
    // Draw's GetIrradiance rewrites a traffic vehicle's SH (+0xCC) only when it draws it, so between two one-tick frames an
    // SH that changed means "drawn" (a drawn vehicle that moved normally changes it too); wheel inputs; deformation.
    long dump_sets = 0;
    std::FILE* dz = r.args.dump_zones.empty() ? nullptr : std::fopen(r.args.dump_zones.c_str(), "wb");
    std::set<std::vector<float>> dumped;
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        long cull[2][7] = {}, sh_changed_culled = 0, sh_changed_drawn = 0, moved_drawn_same = 0, moved_drawn = 0, cmp_ticks = 0;
        long blur[3] = {}, lists = 0, pos204 = 0, pos204_ep = 0, deformed = 0, bit_def = 0, zones = 0;
        std::size_t max_drawn = 0;
        float spin[2] = {1e30f, -1e30f}, steer[2] = {1e30f, -1e30f}, speed[2] = {1e30f, -1e30f}, zmax = 0, zpos[3] = {};
        std::uint32_t zpoll = 0, zid = 0, near_poll[2] = {}, near_id[2] = {};
        float near_d2[2] = {1e30f, 1e30f};
        long steer_agree = 0, steer_disagree = 0;
        float ax[6] = {};
        std::map<std::uint32_t, std::pair<std::uint32_t, int>> first_def;   // vehicle id -> first poll deformed, kind
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const auto& t = cap.ticks[i];
            lists += t.traffic_lists;
            std::size_t drawn = 0;
            const bool one = i > s.first && t.ticks_per_frame == 1 && cap.ticks[i - 1].ticks_per_frame == 1 && t.poll == cap.ticks[i - 1].poll + 1;
            cmp_ticks += one;
            for (const auto& v : t.vehicles) {
                if (v.kind == b3replay::VehicleKind::kRaceCar) continue;
                const int k = v.kind == b3replay::VehicleKind::kTraffic ? 0 : 1;
                ++cull[k][v.cull];
                drawn += v.cull == b3replay::Vehicle::kDrawn;
                if (k == 1) { ++pos204; pos204_ep += std::memcmp(v.cull_pos, v.main.r[3], 12) == 0; }
                if (k == 0) {
                    spin[0] = std::min(spin[0], v.spin); spin[1] = std::max(spin[1], v.spin);
                    steer[0] = std::min(steer[0], v.steer); steer[1] = std::max(steer[1], v.steer);
                    speed[0] = std::min(speed[0], v.speed); speed[1] = std::max(speed[1], v.speed);
                    std::memcpy(ax, v.spin_axis, 12); std::memcpy(ax + 3, v.steer_axis, 12);
                    const int file = v.model >= 0 ? r.view.car_file(cap.heads[std::size_t(v.model)]) : -1;
                    float rad = 0;
                    if (file >= 0) std::memcpy(&rad, r.view.cars[std::size_t(file)].bytes.data() + 0x18, 4);
                    const float ws = rad != 0 ? std::fabs(v.speed / rad) : 0;
                    if (v.cull == b3replay::Vehicle::kDrawn) ++blur[ws > 50 ? 2 : ws > 25 ? 1 : 0];
                }
                if (k == 0 && v.cull == b3replay::Vehicle::kDrawn) {   // picture pick: the nearest drawn traffic (steering)
                    float d2 = 0;
                    for (int c = 0; c < 3; ++c) d2 += (v.main.r[3][c] - t.cam.lod_pos[c]) * (v.main.r[3][c] - t.cam.lod_pos[c]);
                    const int st = std::fabs(v.steer) > 0.1f ? 1 : 0;
                    if (d2 < near_d2[st]) { near_d2[st] = d2; near_poll[st] = t.poll; near_id[st] = v.id; }
                }
                if (k == 0 && one && v.sh_count == 9) {
                    for (const auto& u : cap.ticks[i - 1].vehicles) {
                        if (u.id != v.id || u.kind != v.kind || u.sh_count != 9) continue;
                        const bool changed = std::memcmp(u.sh, v.sh, sizeof v.sh) != 0;
                        float d2 = 0;
                        for (int c = 0; c < 3; ++c) d2 += (u.main.r[3][c] - v.main.r[3][c]) * (u.main.r[3][c] - v.main.r[3][c]);
                        // steer sign against the turn: the steered wheel's forward in the car frame is (-sin steer, 0,
                        // cos steer) (R(y, -steer) [C asm neg.s 0x308418]); the heading change along the car's right row
                        if (std::fabs(v.steer) > 0.05f && v.speed > 2.0f) {
                            float turn = 0;
                            for (int c = 0; c < 3; ++c) turn += v.main.r[2][c] * u.main.r[0][c];
                            if (std::fabs(turn) > 1e-5f) ++((turn > 0) == (-v.steer > 0) ? steer_agree : steer_disagree);
                        }
                        if (changed && v.cull != b3replay::Vehicle::kDrawn) ++sh_changed_culled;
                        if (changed && v.cull == b3replay::Vehicle::kDrawn) ++sh_changed_drawn;
                        if (v.cull == b3replay::Vehicle::kDrawn && d2 > 1e-4f) { ++moved_drawn; moved_drawn_same += !changed; }
                        break;
                    }
                }
            }
            for (const auto& v : t.vehicles) {
                if (!v.zones_valid) continue;
                ++zones;
                bool any = v.deformed;
                deformed += v.deformed;
                for (int b = 0; b < v.nbit; ++b) { bit_def += v.bit_deformed[b]; any = any || v.bit_deformed[b]; }
                if (!any) continue;
                first_def.emplace(v.id, std::make_pair(t.poll, int(v.kind)));
                // the largest translation a zone applies (the picture pick: the most deformed vehicle-tick)
                for (int z = 0; z < 8 && v.deformed; ++z) {
                    const float* tr = v.zone[z].r[3];
                    const float d = std::sqrt(tr[0] * tr[0] + tr[1] * tr[1] + tr[2] * tr[2]);
                    if (d > zmax) { zmax = d; zpoll = t.poll; zid = v.id; std::memcpy(zpos, v.main.r[3], sizeof zpos); }
                }
                if (dz && dumped.size() < 4000) {
                    std::vector<float> set;
                    if (v.deformed) for (int z = 0; z < 8; ++z) for (int a = 0; a < 4; ++a) for (int c = 0; c < 3; ++c) set.push_back(v.zone[z].r[a][c]);
                    if (!set.empty() && dumped.insert(set).second) {
                        const std::uint32_t n = 8;
                        std::fwrite(&n, 4, 1, dz);
                        std::fwrite(set.data(), 4, set.size(), dz);
                        ++dump_sets;
                    }
                    for (int b = 0; b < v.nbit; ++b) {
                        if (!v.bit_deformed[b]) continue;
                        std::vector<float> ps;
                        for (int z = 0; z < 2; ++z) for (int a = 0; a < 4; ++a) for (int c = 0; c < 3; ++c) ps.push_back(v.bit_zone[b][z].r[a][c]);
                        if (dumped.insert(ps).second) {
                            const std::uint32_t n = 2;
                            std::fwrite(&n, 4, 1, dz);
                            std::fwrite(ps.data(), 4, ps.size(), dz);
                            ++dump_sets;
                        }
                    }
                }
            }
            max_drawn = std::max(max_drawn, drawn);
        }
        const char* why[7] = {"drawn", "beyond 275 m", "not in the player's list", "unit not seen", "frustum 0", "camera inside", "over the cap"};
        for (int k = 0; k < 2; ++k) {
            std::printf("segment %zu %s (render6, Draw's choice per vehicle-tick):", si, k ? "crashed" : "traffic");
            for (int c = 0; c < 7; ++c) std::printf(" %s %ld%s", why[c], cull[k][c], c < 6 ? "," : "\n");
        }
        std::printf("   ticks with the traffic lists captured %ld of %zu; at most %zu traffic + crashed drawn in a tick; crashed: cull "
                    "position = *(+0x204) and equal to the essential params' %ld of %ld\n",
                    lists, s.last - s.first + 1, max_drawn, pos204_ep, pos204);
        std::printf("   SH trace over %ld one-tick-frame pairs: SH changed and drawn %ld, SH changed but not drawn %ld (predicate misses);"
                    " drawn and moved %ld, of them SH unchanged %ld\n",
                    cmp_ticks, sh_changed_drawn, sh_changed_culled, moved_drawn, moved_drawn_same);
        std::printf("   traffic wheels: spin %.3f..%.3f, steer %.3f..%.3f, speed +0xC4 %.2f..%.2f; axes (%.0f %.0f %.0f) (%.0f %.0f %.0f);"
                    " drawn vehicle-ticks by wheel list B0 %ld B1 %ld B2 %ld\n",
                    spin[0], spin[1], steer[0], steer[1], speed[0], speed[1], ax[0], ax[1], ax[2], ax[3], ax[4], ax[5], blur[0], blur[1], blur[2]);
        std::printf("   traffic steer sign vs the turn (|steer| > 0.05, speed > 2, one-tick pairs): wheels point into the turn %ld,"
                    " against it %ld\n", steer_agree, steer_disagree);
        std::printf("   nearest drawn traffic: %.1f m (%08x, poll %u); steering (|steer| > 0.1): %.1f m (%08x, poll %u)\n",
                    std::sqrt(near_d2[0]), near_id[0], near_poll[0], std::sqrt(near_d2[1]), near_id[1], near_poll[1]);
        std::printf("   deformation: vehicle-ticks with zones captured %ld, body deformed %ld, panel-ticks deformed %ld; %zu vehicles"
                    " deformed; largest zone translation %.3f m (%08x at poll %u, at %.1f %.1f %.1f)\n", zones, deformed, bit_def,
                    first_def.size(), zmax, zid, zpoll, zpos[0], zpos[1], zpos[2]);
        long shown = 0;
        for (const auto& [id, pk] : first_def)
            if (++shown <= 12)
                std::printf("     %s %08x first deformed at poll %u\n", pk.second == 0 ? "race car" : pk.second == 1 ? "traffic" : "crashed",
                            id, pk.first);
    }
    if (dz) { std::fclose(dz); std::printf("zone sets written to %s: %ld\n", r.args.dump_zones.c_str(), dump_sets); }
    // Props (capture version 2): the dynamic list and the hidden placements per segment, with the events (a placement
    // entering the dynamic list) and the nearest race car then; checks: every dynamic prop's own per-unit entry is hidden
    // (DynamiseProp sets the original's state to 2), and each prop body's matrix pointer is the manager's copy.
    std::printf("capture version %u; prop bodies checked %llu, matrix pointer not the manager's placement copy %llu\n", cap.version,
                static_cast<unsigned long long>(cap.prop_body_checked), static_cast<unsigned long long>(cap.prop_body_mismatch));
    for (std::size_t si = 0; si < cap.segments.size(); ++si) {
        const auto& s = cap.segments[si];
        std::size_t valid = 0, maxdyn = 0, moving = 0, not_hidden = 0, events = 0, off[3] = {};
        std::map<std::uint32_t, std::size_t> logics;
        std::set<int> ever;
        for (std::size_t i = s.first; i <= s.last; ++i) {
            const auto& t = cap.ticks[i];
            ++off[t.props_off + 1];
            ++logics[t.logic_vtable];
            if (!t.props_valid) continue;
            ++valid;
            maxdyn = std::max(maxdyn, t.props.size());
            for (const auto& p : t.props) {
                moving += p.slot >= 0;
                if (!std::binary_search(t.hidden.begin(), t.hidden.end(), std::uint16_t(p.placement))) ++not_hidden;
                bool before = false;
                if (i > s.first) for (const auto& q : cap.ticks[i - 1].props) before |= q.placement == p.placement;
                if (before) continue;
                ever.insert(p.placement);
                if (++events > 60) continue;
                float best = 1e30f;
                for (const auto& v : t.vehicles) {
                    if (v.kind != b3replay::VehicleKind::kRaceCar) continue;
                    float d2 = 0;
                    for (int k = 0; k < 3; ++k) d2 += (v.main.r[3][k] - p.m.r[3][k]) * (v.main.r[3][k] - p.m.r[3][k]);
                    best = std::min(best, std::sqrt(d2));
                }
                std::printf("   seg %zu poll %u: prop placement %d type %d dynamic (body %d) at (%.1f %.1f %.1f), nearest race car %.1f m\n", si,
                            t.poll, p.placement, p.type, p.slot, p.m.r[3][0], p.m.r[3][1], p.m.r[3][2], best);
            }
        }
        // the static placements the race cars came closest to (STATIC.DAT +0x48 translations, the type from the lists)
        if (const std::string route = r.segment_route(si); !route.empty()) {
            std::vector<std::uint8_t> st;
            b3asset::read_file(route + "/STATIC.DAT", st);
            std::uint32_t mats = 0;
            std::uint16_t np = 0;
            if (st.size() > 0x60) { std::memcpy(&mats, st.data() + 0x48, 4); std::memcpy(&np, st.data() + 0x40, 2); }
            std::vector<std::pair<float, int>> near;
            std::vector<std::uint32_t> when(np, 0);
            for (int k = 0; k < np && mats + 0x40u * std::uint32_t(k) + 0x40 <= st.size(); ++k) {
                float pos[3];
                std::memcpy(pos, st.data() + mats + 0x40 * std::size_t(k) + 0x30, 12);
                float best = 1e30f;
                for (std::size_t i = s.first; i <= s.last; ++i)
                    for (const auto& v : cap.ticks[i].vehicles) {
                        if (v.kind != b3replay::VehicleKind::kRaceCar) continue;
                        float d2 = 0;
                        for (int j = 0; j < 3; ++j) d2 += (v.main.r[3][j] - pos[j]) * (v.main.r[3][j] - pos[j]);
                        if (d2 < best) { best = d2; when[std::size_t(k)] = cap.ticks[i].poll; }
                    }
                near.push_back({std::sqrt(best), k});
            }
            // the tick whose game camera sees the most placements within 40 m (frame picking for prop pictures)
            std::size_t best_n = 0, best_i = s.first;
            for (std::size_t i = s.first; i <= s.last; i += 4) {
                std::size_t n = 0;
                for (int k = 0; k < np && mats + 0x40u * std::uint32_t(k) + 0x40 <= st.size(); ++k) {
                    float pos[3];
                    std::memcpy(pos, st.data() + mats + 0x40 * std::size_t(k) + 0x30, 12);
                    float d2 = 0;
                    for (int j = 0; j < 3; ++j) d2 += (pos[j] - cap.ticks[i].cam.m.r[3][j]) * (pos[j] - cap.ticks[i].cam.m.r[3][j]);
                    n += d2 < 40.0f * 40.0f && frustum_class(cap.ticks[i].cam, pos, 0.5f) != 0;
                }
                if (n > best_n) { best_n = n; best_i = i; }
            }
            std::printf("segment %zu: most prop placements in the game camera's view within 40 m: %zu at poll %u\n", si, best_n, cap.ticks[best_i].poll);
            std::sort(near.begin(), near.end());
            std::printf("segment %zu: race cars' closest approach to static prop placements:", si);
            for (std::size_t k = 0; k < near.size() && k < 12; ++k) std::printf(" #%d %.1f m (poll %u)", near[k].second, near[k].first, when[std::size_t(near[k].second)]);
            std::printf("\n");
        }
        const auto& last = cap.ticks[s.last];
        std::printf("segment %zu props in this mode (stage logic slot 0xB8): on %zu ticks, off %zu, unknown %zu; stage logic vtable", si, off[1], off[2], off[0]);
        for (const auto& [vt, n] : logics) std::printf(" %08x x%zu", vt, n);
        std::printf("\n");
        std::printf("segment %zu prop manager at its last tick: %d placements, %d prop types, %d CB3Props in the per-unit lists\n", si,
                    last.prop_placements, last.prop_types, last.prop_listed);
        std::printf("segment %zu props: %zu of %zu ticks decoded; %zu dynamise events (%zu placements), at most %zu dynamic at once,"
                    " %zu prop-ticks with a body; %zu hidden at the end; dynamic props whose own entry is not hidden: %zu\n", si, valid,
                    s.last - s.first + 1, events, ever.size(), maxdyn, moving, last.hidden.size(), not_hidden);
    }
    const auto& s = r.segment();
    // §6.4 check: the segment shown at 144 Hz, camera and player-car positions per displayed frame, interpolation off
    // (the game's own 60 Hz steps: frames repeat, then jump) and on (every displayed frame moves by a share of the tick)
    for (int interp = 0; interp < 2; ++interp) {
        const int hz = 144;
        const long frames = long(double(s.last - s.first) / 60.0 * hz);
        long repeat_cam = 0, repeat_car = 0, moving_cam = 0, moving_car = 0;
        std::vector<double> steps;
        auto player = [](const b3replay::Frame& f) -> const b3replay::Vehicle* {
            for (const auto& v : f.vehicles) if (v.kind == b3replay::VehicleKind::kRaceCar) return &v;
            return nullptr;
        };
        b3replay::Frame prev = r.at(0, interp != 0);
        for (long k = 1; k <= frames; ++k) {
            const b3replay::Frame f = r.at(double(k) / hz, interp != 0);
            double dc = 0, dv = 0;
            for (int j = 0; j < 3; ++j) dc += double(f.cam.m.r[3][j] - prev.cam.m.r[3][j]) * double(f.cam.m.r[3][j] - prev.cam.m.r[3][j]);
            const auto *a = player(f), *b = player(prev);
            if (a && b) for (int j = 0; j < 3; ++j) dv += double(a->main.r[3][j] - b->main.r[3][j]) * double(a->main.r[3][j] - b->main.r[3][j]);
            // only frames where the game itself moves (positions 1/60 s before and after differ) are counted
            const b3replay::Frame lo = r.at(double(k) / hz - 1.0 / 60, false), hi = r.at(double(k) / hz + 1.0 / 60, false);
            const auto *la = player(lo), *ha = player(hi);
            const bool mc = std::memcmp(lo.cam.m.r[3], hi.cam.m.r[3], 12) != 0;
            const bool mv = la && ha && std::memcmp(la->main.r[3], ha->main.r[3], 12) != 0;
            moving_cam += mc;
            moving_car += mv;
            repeat_cam += mc && dc == 0;
            repeat_car += mv && dv == 0;
            if (dc > 0) steps.push_back(std::sqrt(dc));
            prev = f;
        }
        std::sort(steps.begin(), steps.end());
        const double med = steps.empty() ? 0 : steps[steps.size() / 2], p95 = steps.empty() ? 0 : steps[steps.size() * 95 / 100];
        std::printf("display at %d Hz, interpolation %s: %ld frames; while moving, the camera repeats in %.1f %% of %ld frames, the"
                    " player car in %.1f %% of %ld; camera step median %.3f m, p95 %.3f m\n", hz, interp ? "on " : "off", frames,
                    100.0 * repeat_cam / std::max(1L, moving_cam), moving_cam, 100.0 * repeat_car / std::max(1L, moving_car), moving_car, med, p95);
    }
    for (std::size_t i = s.first; i <= s.last; i += std::max<std::size_t>(1, (s.last - s.first) / 6)) {
        const auto& t = cap.ticks[i];
        const auto& c = t.cam;
        std::printf("poll %u vb %u: cam pos (%.1f %.1f %.1f) at (%.3f %.3f %.3f) fov %.1f window %.3fx%.3f near %.2f far %.0f; unit %d; slots",
                    t.poll, t.vblank, c.m.r[3][0], c.m.r[3][1], c.m.r[3][2], c.m.r[2][0], c.m.r[2][1], c.m.r[2][2], c.fov, c.half_w,
                    c.half_h, c.znear, c.zfar, t.current_unit);
        for (int j = 0; j < 17; ++j) if (t.slot_unit[j] >= 0) std::printf(" %d%s%s", t.slot_unit[j], t.slot_kind[j] ? "L" : "H", (t.vis[j] & 3) ? "*" : "");
        std::printf("\n");
        for (const auto& v : t.vehicles) {
            if (v.kind == b3replay::VehicleKind::kTraffic) continue;
            std::printf("   %s %08x data %08x paint %d pos (%.1f %.1f %.1f) wheels %d bits %d%s\n",
                        v.kind == b3replay::VehicleKind::kRaceCar ? "car" : "crashed", v.id, v.data, v.paint, v.main.r[3][0],
                        v.main.r[3][1], v.main.r[3][2], v.nwheel, v.nbit, v.deformed ? " deformed" : "");
        }
        int ntraffic = 0;
        for (const auto& v : t.vehicles) ntraffic += v.kind == b3replay::VehicleKind::kTraffic;
        std::printf("   traffic %d\n", ntraffic);
    }
    crash_info(r);
    info_effects(r);
    info_anim(r);
    info_coronas(r);
    info_contact(r);
    info_hud(r);
    info_markers(r);
    info_sun(r);
    info_blur(r);
    info_post(r);
    return 0;
}

struct FreeCam {
    float pos[3] = {}, yaw = 0, pitch = 0, speed = 30;
    void forward(float f[3]) const { f[0] = std::sin(yaw) * std::cos(pitch); f[1] = std::sin(pitch); f[2] = std::cos(yaw) * std::cos(pitch); }
    void from(const b3replay::Camera& c) {
        for (int i = 0; i < 3; ++i) pos[i] = c.m.r[3][i];
        const float* a = c.m.r[2];
        yaw = std::atan2(a[0], a[2]);
        pitch = std::atan2(a[1], std::sqrt(a[0] * a[0] + a[2] * a[2]));
    }
    // The free view as a game camera (rows: right = screen left, up, at, pos; the game's view window): coronas face it
    b3replay::Camera camera(const b3replay::Camera& game) const {
        b3replay::Camera c = game;
        float f[3];
        forward(f);
        float l[3] = {f[2], 0, -f[0]};   // (0, 1, 0) x f: screen left
        const float ll = std::sqrt(l[0] * l[0] + l[2] * l[2]);
        if (ll > 0) { l[0] /= ll; l[2] /= ll; } else { l[0] = 1; l[2] = 0; }
        const float u[3] = {f[1] * l[2] - f[2] * l[1], f[2] * l[0] - f[0] * l[2], f[0] * l[1] - f[1] * l[0]};   // f x l
        for (int k = 0; k < 3; ++k) { c.m.r[0][k] = l[k]; c.m.r[1][k] = u[k]; c.m.r[2][k] = f[k]; c.m.r[3][k] = pos[k]; }
        return c;
    }
    void view_proj(float fovy, float aspect, float out[16]) const {
        float f[3], v[16], p[16];
        forward(f);
        const float up[3] = {0, 1, 0};
        b3gfx::mat4_look(pos, f, up, v);
        b3gfx::mat4_perspective_reverse_z(fovy, aspect, 0.1f, p);
        b3gfx::mat4_mul(p, v, out);
    }
};

int run_offscreen(Replay& r) {
    b3gfx::Options opt;
    opt.msaa = r.args.msaa;
    opt.dither = r.args.dither;   // perf1
    opt.driver = r.args.driver;
    if (r.args.pc) { b3replay::filter_options(r.args.pc->filter, opt); b3replay::scale_options(*r.args.pc, opt); }   // settings1
    if (r.args.pc && r.args.pc->dither) opt.dither = 1.0f;   // perf1: DITHER
    std::string err;
    SDL_Window* hidden = nullptr;
    const char* how = "";
    auto dev = b3gfx::create_headless(opt, &err, &hidden, &how);
    if (!dev) { std::fprintf(stderr, "b3gfx: %s\n", err.c_str()); return 1; }
    b3asset::set_hd_compressed(dev->supports_bc());   // hdgpu: HD replacements stay BC on the GPU
    std::printf("headless device: %s (%s, video driver %s)\n", dev->driver_name(), how, SDL_GetCurrentVideoDriver());
    if (!r.load_track(*dev, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    const auto& s = r.segment();
    const bool sweep = r.args.sweep_step > 0;
    std::vector<long> polls;
    if (sweep) for (long p = r.args.sweep_from; p <= r.args.sweep_to; p += r.args.sweep_step) polls.push_back(p);
    else polls.push_back(r.args.poll);
    for (const long poll : polls) {
    std::size_t i = s.first;
    while (i < s.last && long(r.cap.ticks[i].poll) < poll) ++i;
    if (sweep && (long(r.cap.ticks[i].poll) != poll)) continue;   // outside the segment
    const std::string out_png = sweep ? r.args.offscreen + "/native_" + std::to_string(poll) + ".png" : r.args.offscreen;
    auto f = b3replay::frame_at(r.timeline, i, r.args.alpha, true);
    if (!r.fe) r.view.apply_fov(f.cam, f.vehicles);   // settings1
    const auto& c = f.cam;
    int w = r.args.w, h = r.args.h;
    if (!r.args.size_set) { w = 640; h = 448; }
    if (!r.fe && f.split && !r.args.has_free_eye) {   // gsoff1 (§29): the 2-player split screen, two views
        const float va = r.args.size_set ? 2.0f * float(w) / float(h) : 0.0f, ha = r.args.size_set ? float(w) / float(h) : 4.0f / 3.0f;
        std::vector<b3gfx::Device::ViewPass> views;
        b3gfx::FrameParams ofp;
        std::vector<b3gfx::DrawItem> overlay;
        std::vector<std::uint8_t> rgba;
        if (r.view.show_bloom && r.args.bloom_warmup > 0) {   // both views' kept bloom pictures, as for one view below
            r.view.bloom_last = -1;
            r.view.view2_state.bloom_last = -1;
            for (std::size_t k = i > s.first + std::size_t(r.args.bloom_warmup) ? i - std::size_t(r.args.bloom_warmup) : s.first; k < i; ++k) {
                const auto wf = b3replay::frame_at(r.timeline, k, 0.0, true);
                if (!wf.split || !wf.cam.valid) continue;
                r.view.split_frame(*dev, wf, r.cap.heads, r.cap.hud.textures, !r.args.no_cull, r.args.fog, r.args.specular, w, h, va, ha, views, ofp, overlay);
                dev->draw_views_offscreen(w, h, views, ofp, overlay, rgba);
            }
        }
        r.view.split_frame(*dev, f, r.cap.heads, r.cap.hud.textures, !r.args.no_cull, r.args.fog, r.args.specular, w, h, va, ha, views, ofp, overlay);
        if (!dev->draw_views_offscreen(w, h, views, ofp, overlay, rgba)) { std::fprintf(stderr, "offscreen draw failed: %s\n", SDL_GetError()); return 1; }
        if (!b3asset::write_png_rgba(out_png, w, h, rgba.data())) { std::fprintf(stderr, "cannot write %s\n", out_png.c_str()); return 1; }
        if (f.hud && std::getenv("B3HUD_MARKER_TRACE") && r.view.hud_marker_place.size() == f.hud->markers.size())
            for (std::size_t m = 0; m < f.hud->markers.size(); ++m) {   // hud4's trace, per view (frame-space 2D units)
                const auto& mk = f.hud->markers[m];
                const auto& pl = r.view.hud_marker_place[m];
                // the captured world point through this frame's view of the marker: the game's projection, in its half
                float px = 0, py = 0, pz = 0;
                const bool ok = b3replay::hud_project_split(views[std::size_t(mk.view)].fp.view_proj, mk.view, ha, f.hud->width, f.hud->height, mk.world, &px, &py, &pz);
                const b3replay::Camera& vc = mk.view == 1 ? f.tick_cam2 : f.tick_cam;
                float dm = 0;
                for (int a = 0; a < 4; ++a) for (int b = 0; b < 4; ++b) dm = std::max(dm, std::fabs(vc.screen[a][b] - mk.screen[a][b]));
                std::printf("marker %zu: tick camera vs its viewport: max |screen diff| %.6f\n", m, double(dm));
                std::printf("marker %zu: view %d valid %d captured %.3f %.3f world-point %s %.3f %.3f (off %.3f %.3f) drawn %.3f %.3f (shown %d)\n", m, mk.view,
                            mk.valid ? 1 : 0, mk.x, mk.y, ok ? "at" : "behind", px, py, px - mk.x, py - mk.y, pl.x, pl.y, pl.shown ? 1 : 0);
            }
        const auto& t = r.cap.ticks[f.tick];
        std::printf("poll %u (+%.2f): split screen, route %s, %zu + %zu draws, %zu 2D items, cameras (%.1f %.1f %.1f) / (%.1f %.1f %.1f) -> %s\n", t.poll,
                    f.alpha, r.view.route.c_str(), views[0].items.size(), views[1].items.size(), overlay.size(), f.cam.m.r[3][0], f.cam.m.r[3][1],
                    f.cam.m.r[3][2], f.cam2.m.r[3][0], f.cam2.m.r[3][1], f.cam2.m.r[3][2], out_png.c_str());
        std::fflush(stdout);
        continue;
    }
    if (!r.fe && r.view.show_bloom && r.args.bloom_warmup > 0) {
        // post1 (§25): the bloom keeps its picture from render to render (CB3Bloom::Blend), so a single frame starts it
        // from the ticks before: each drawn with the game camera, its result discarded
        r.view.bloom_last = -1;
        std::vector<std::uint8_t> scratch;
        for (std::size_t k = i > s.first + std::size_t(r.args.bloom_warmup) ? i - std::size_t(r.args.bloom_warmup) : s.first; k < i; ++k) {
            auto wf = b3replay::frame_at(r.timeline, k, 0.0, true);
            if (!wf.cam.valid) continue;
            if (!r.fe) r.view.apply_fov(wf.cam, wf.vehicles);   // settings1
            b3gfx::FrameParams wp;
            b3view::frame_params(r.view.sky, wf.cam.m.r[3], wf.cam.m.r[2], r.args.fog, r.args.specular, wp);
            b3replay::game_view_proj(wf.cam, r.args.size_set ? float(w) / float(h) : wf.cam.half_w / wf.cam.half_h, wp.view_proj);
            r.view.car_frame(wp);
            std::vector<b3gfx::DrawItem> witems;
            r.view.render_aspect = r.args.size_set ? float(w) / float(h) : 0.0f;
            r.draw_list(*dev, wf, !r.args.no_cull, wp.eye, witems);
            r.view.post_frame(wf, r.args.size_set ? float(w) / float(h) : 4.0f / 3.0f, wp);
            dev->draw_offscreen(w, h, wp, witems, scratch);
        }
    }
    b3gfx::FrameParams fp;
    if (r.fe) {   // menu1 (§22): a front-end frame, 2D only
        std::vector<b3gfx::DrawItem> items;
        r.view.frontend_frame(*dev, f, r.cap.heads, r.cap.hud.textures, r.args.size_set ? float(w) / float(h) : 4.0f / 3.0f, w, h, fp, items);
        std::vector<std::uint8_t> rgba;
        if (!dev->draw_offscreen(w, h, fp, items, rgba)) { std::fprintf(stderr, "offscreen draw failed: %s\n", SDL_GetError()); return 1; }
        if (!b3asset::write_png_rgba(out_png, w, h, rgba.data())) { std::fprintf(stderr, "cannot write %s\n", out_png.c_str()); return 1; }
        std::printf("poll %u: %s, HUD frame %u, %zu 2D draws decoded, %zu items drawn%s -> %s\n", r.cap.ticks[f.tick].poll, r.segment_name().c_str(),
                    f.hud ? f.hud->serial : 0U, f.hud ? f.hud->draws.size() : std::size_t(0), items.size(),
                    f.fe_car ? " (front-end car)" : "", out_png.c_str());
        std::fflush(stdout);
        continue;
    }
    if (r.args.has_free_eye) {
        FreeCam fc;
        fc.from(c);
        for (int k = 0; k < 3; ++k) fc.pos[k] = r.args.free_eye[k];
        // look at the camera's position from the given eye
        const float* tgt = r.args.has_look_at ? r.args.look_at : c.m.r[3];
        const float d[3] = {tgt[0] - fc.pos[0], tgt[1] - fc.pos[1], tgt[2] - fc.pos[2]};
        fc.yaw = std::atan2(d[0], d[2]);
        fc.pitch = std::atan2(d[1], std::sqrt(d[0] * d[0] + d[2] * d[2]));
        fc.view_proj(2.0f * std::atan(c.half_h), float(w) / float(h), fp.view_proj);
        float fw[3];
        fc.forward(fw);
        b3view::frame_params(r.view.sky, fc.pos, fw, r.args.fog, r.args.specular, fp);
    } else {
        b3view::frame_params(r.view.sky, c.m.r[3], c.m.r[2], r.args.fog, r.args.specular, fp);
        // the game's own aspect by default (640x448 with its view window), else Hor+ at the given size
        b3replay::game_view_proj(c, r.args.size_set ? float(w) / float(h) : c.half_w / c.half_h, fp.view_proj);
    }
    r.view.car_frame(fp);
    std::vector<b3gfx::DrawItem> items;
    b3replay::Camera free_cam;
    if (r.args.has_free_eye) {
        FreeCam fc;
        fc.from(c);
        for (int k = 0; k < 3; ++k) fc.pos[k] = r.args.free_eye[k];
        const float* tgt = r.args.has_look_at ? r.args.look_at : c.m.r[3];
        const float d[3] = {tgt[0] - fc.pos[0], tgt[1] - fc.pos[1], tgt[2] - fc.pos[2]};
        fc.yaw = std::atan2(d[0], d[2]);
        fc.pitch = std::atan2(d[1], std::sqrt(d[0] * d[0] + d[2] * d[2]));
        free_cam = fc.camera(c);
        r.view.corona_view = &free_cam;
    }
    r.view.render_aspect = r.args.size_set ? float(w) / float(h) : 0.0f;
    r.view.free_view = r.args.has_free_eye;   // perf1 (§33.7): the sky's open cap faded, the bloom of this view
    r.draw_list(*dev, f, !r.args.no_cull, fp.eye, items);
    r.view.corona_view = nullptr;
    r.view.post_frame(f, r.args.size_set ? float(w) / float(h) : 4.0f / 3.0f, fp);
    r.view.free_view = false;
    // the HUD: the game's 640x448 frame is a 4:3 picture, so without --size it fills the image as on the PS2
    const std::size_t n3d = items.size();
    b3replay::HudProjection hp;
    std::memcpy(hp.view_proj, fp.view_proj, sizeof hp.view_proj);
    hp.vehicles = &f.vehicles;
    r.view.hud_list(*dev, f.hud, r.cap.hud.textures, r.args.size_set ? float(w) / float(h) : 4.0f / 3.0f, items, h, w, &hp);
    if (f.hud && std::getenv("B3HUD_MARKER_TRACE") && r.view.hud_marker_place.size() == f.hud->markers.size()) {
        // hud4: each marker's captured place, where this frame draws it, and its anchor's place in this frame's 3D (the
        // anchor vehicle's interpolated position + the captured offset through the frame's view-projection)
        const float hud_aspect = r.args.size_set ? float(w) / float(h) : 4.0f / 3.0f;
        for (std::size_t m = 0; m < f.hud->markers.size(); ++m) {
            const auto& mk = f.hud->markers[m];
            const auto& pl = r.view.hud_marker_place[m];
            float ax = 0, ay = 0, az = 0;
            bool have = false;
            for (const auto& v : f.vehicles)
                if (mk.vehicle && v.id == mk.vehicle) {
                    float wp[3];
                    for (int k = 0; k < 3; ++k) wp[k] = v.main.r[3][k] + mk.offset[k];
                    have = b3replay::hud_project(fp.view_proj, hud_aspect, f.hud->width, f.hud->height, wp, &ax, &ay, &az);
                }
            std::printf("marker %zu: slot %u car %08x vehicle %08x valid %d captured %.3f %.3f z %.3f size %.3f drawn %.3f %.3f (shown %d r %.4f)"
                        " anchor %s %.3f %.3f\n", m, mk.slot, mk.car, mk.vehicle, mk.valid ? 1 : 0, mk.x, mk.y, mk.z, mk.size,
                        r.view.hud_reproject ? pl.x : mk.x, r.view.hud_reproject ? pl.y : mk.y, pl.shown ? 1 : 0, pl.r, have ? "at" : "none", ax, ay);
        }
    }
    if (f.hud) {
        std::size_t tris = 0;
        for (std::size_t k = n3d; k < items.size(); ++k) tris += items[k].index_count / 3;
        std::printf("hud: frame %u, %zu draws decoded, %zu drawn (%zu triangles)\n", f.hud->serial, f.hud->draws.size(), items.size() - n3d, tris);
    }
    if (r.args.dof_radius > 0) {   // photo2: the depth of field (b3gfx FrameParams::Dof); focus <= 0: the autofocus
        fp.dof.on = true;
        fp.dof.radius = r.args.dof_radius;
        fp.dof.focus = r.args.dof_focus;
        if (fp.dof.focus <= 0)   // the autofocus on the player's car, as photo mode aims it (else the centre)
            for (const auto& v : f.vehicles)
                if (v.id == b3replay::kPlayerRaceCar && v.kind == b3replay::VehicleKind::kRaceCar) {
                    b3replay::PhotoDof pd;
                    pd.on = true;
                    const auto aim = pd.params(c, v.body.r[3], fp.view_proj);
                    fp.dof.point[0] = aim.point[0];
                    fp.dof.point[1] = aim.point[1];
                }
        std::printf("depth of field: radius %.4f of the height, focus %s (%.3f, %.3f)\n", double(fp.dof.radius),
                    fp.dof.focus > 0 ? std::to_string(fp.dof.focus).c_str() : "the autofocus at", double(fp.dof.point[0]), double(fp.dof.point[1]));
    }
    std::vector<std::uint8_t> rgba;
    if (!dev->draw_offscreen(w, h, fp, items, rgba)) { std::fprintf(stderr, "offscreen draw failed: %s\n", SDL_GetError()); return 1; }
    if (!b3asset::write_png_rgba(out_png, w, h, rgba.data())) { std::fprintf(stderr, "cannot write %s\n", out_png.c_str()); return 1; }
    if (!sweep) b3asset::hd_report();
    const auto& t = r.cap.ticks[f.tick];
    std::printf("poll %u (+%.2f): route %s, %zu draws, %zu vehicles, camera (%.1f %.1f %.1f) -> %s\n", t.poll, f.alpha, r.view.route.c_str(),
                items.size(), f.vehicles.size(), c.m.r[3][0], c.m.r[3][1], c.m.r[3][2], out_png.c_str());
    {
        const auto& s = r.view.fx_stats;
        std::printf("effects: %d shadow strips, %d skid emitters (%d points), %d scrape strips (%d segments), %d spark quads, %d popcorn sprites\n", s.shadows,
                    s.trail_emitters, s.trail_points, s.scrape_emitters, s.scrape_segments, s.sparks, s.popcorn);
    }
    if (sweep) { std::fflush(stdout); continue; }
    // the vehicles drawn and their LOD (game's choice from its camera, or --car-lod), nearest first (render6: only those
    // CB3TrafficSystem::Draw draws, game_draws)
    std::vector<std::pair<float, const b3replay::Vehicle*>> near;
    for (const auto& v : f.vehicles) {
        if (v.cull != b3replay::Vehicle::kDrawn) continue;
        float d2 = 0;
        for (int k = 0; k < 3; ++k) d2 += (v.main.r[3][k] - c.lod_pos[k]) * (v.main.r[3][k] - c.lod_pos[k]);
        near.push_back({std::sqrt(d2), &v});
    }
    std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t k = 0; k < near.size() && k < 12; ++k) {
        const auto& v = *near[k].second;
        const int f2 = v.model < 0 || std::size_t(v.model) >= r.cap.heads.size() ? -1 : r.view.car_file(r.cap.heads[std::size_t(v.model)]);
        std::printf("  %-7s %08x at (%.1f %.1f %.1f) up.y %+.2f %.1f m: game LOD %d -> block %d, glass %d, SH %d (L00 %.3f)%s\n",
                    v.kind == b3replay::VehicleKind::kRaceCar ? "car" : v.kind == b3replay::VehicleKind::kTraffic ? "traffic" : "crashed",
                    v.id, v.main.r[3][0], v.main.r[3][1], v.main.r[3][2], v.main.r[1][1], near[k].first, b3replay::game_lod(c, v),
                    f2 < 0 ? -1 : b3view::car_lod_block(r.view.cars[std::size_t(f2)].bytes, r.args.car_lod >= 0 ? r.args.car_lod : b3replay::game_lod(c, v)),
                    v.glass, v.sh_count, v.sh[0], f2 < 0 ? " (no file)" : "");
        if (v.sh_count == 9 && k == 0)
            std::printf("          SH %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n", v.sh[0], v.sh[1], v.sh[2], v.sh[3], v.sh[4],
                        v.sh[5], v.sh[6], v.sh[7], v.sh[8]);
    }
    }   // polls
    dev.reset();
    if (hidden) SDL_DestroyWindow(hidden);
    SDL_Quit();
    return 0;
}

// --pop-scan <dir> (live2, docs/native-renderer.md §20): every tick of the segment is drawn twice from its own camera
// (no game culling, as the live window; no vehicles, effects or HUD; the tick's own animated materials), once with the
// previous tick's world state (track slots, backdrops, props, crash objects) and once with its own, 320x224; the share
// of pixels whose colour changes by more than 32 levels is the visible pop of that tick's state change. Sorted by size;
// the 12 largest are written as <dir>/pop_<poll>_{before,after}.png.
// With --cull-scan: "before" = the tick with only the vehicles CB3TrafficSystem::Draw draws (game_draws, kDrawn),
// "after" = what the live window draws by default (game culling off: every vehicle within 275 m or crashing).
// With --vis-scan: no vehicles; "before" = the track as the game draws it (units whose main-camera visibility bits
// & 3 are set), "after" = every resident unit (the live window's default).
// With --lod-scan: no vehicles; "before" = every slot's unit at its high mesh, "after" = the slots as the game set them
// (LOD meshes where the block is a LOD block).
// perf1 (docs/native-renderer.md §33): per-frame costs over a stretch of the race segment, as the window draws it. Every
// tick of polls A..B is shown at N display times (alpha k / N: N = 4 is a 240 Hz display), each frame built (frame_at,
// draw list, post passes, 2D) and drawn offscreen at --size and waited for, without read-back. One CSV row per frame:
// CPU = building the frame's lists (with its uploads), GPU = render + submit + fence wait; the device's counters; the
// effects' counts. Deterministic input, so two builds compare frame by frame.
int run_profile(Replay& r) {
    b3gfx::Options opt;
    opt.msaa = r.args.msaa;
    opt.dither = r.args.dither;   // perf1
    opt.driver = r.args.driver;
    if (r.args.pc) { b3replay::filter_options(r.args.pc->filter, opt); b3replay::scale_options(*r.args.pc, opt); }
    if (r.args.pc && r.args.pc->dither) opt.dither = 1.0f;   // perf1: DITHER
    std::string err;
    SDL_Window* hidden = nullptr;
    const char* how = "";
    auto dev = b3gfx::create_headless(opt, &err, &hidden, &how);
    if (!dev) { std::fprintf(stderr, "b3gfx: %s\n", err.c_str()); return 1; }
    b3asset::set_hd_compressed(dev->supports_bc());   // hdgpu: HD replacements stay BC on the GPU
    if (!r.load_track(*dev, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::FILE* out = std::fopen(r.args.profile.c_str(), "w");
    if (!out) { std::fprintf(stderr, "cannot write %s\n", r.args.profile.c_str()); return 1; }
    const int w = r.args.size_set ? r.args.w : 1280, h = r.args.size_set ? r.args.h : 720;
    const float aspect = float(w) / float(h);
    std::fprintf(out, "# b3replay profile %s %dx%d msaa %d driver %s sub %d\n", r.args.capture.c_str(), w, h, dev->msaa(), dev->driver_name(), r.args.profile_sub);
    std::fprintf(out, "poll,alpha,cpu_ms,gpu_ms,items,draws,uploads,upload_kb,grows,vehicles,hud_draws,shadows,trail_points,scrape_segments,sparks,popcorn,"
                      "radial,dblur,bloom,cam_x,cam_y,cam_z,t_frame,t_static,t_vehicles,t_effects,t_gamedata,t_popcorn,t_coronas,t_sparks,t_post,t_hud\n");
    r.view.prof_on = true;
    r.view.effects.async_sparks = std::getenv("B3PROFILE_ASYNC") != nullptr;   // the live window's worker-built sparks
    const bool nodraw = std::getenv("B3PROFILE_NODRAW") != nullptr;   // the frames' lists and cameras only (no GPU work)
    if (r.fe) { std::fprintf(stderr, "profile: poll %ld is in a front-end stretch\n", r.args.profile_from); return 1; }
    const auto& s = r.segment();
    std::vector<b3gfx::DrawItem> items;
    std::vector<double> cpu, gpu;
    dev->take_stats();
    for (std::size_t i = s.first; i <= s.last; ++i) {
        const long poll = long(r.cap.ticks[i].poll);
        if (poll < r.args.profile_from || poll > r.args.profile_to) continue;
        for (int k = 0; k < r.args.profile_sub; ++k) {
            const double alpha = double(k) / double(r.args.profile_sub);
            const std::uint64_t t0 = b3live::now_ns();
            auto f = b3replay::frame_at(r.timeline, i, alpha, true);
            if (!f.cam.valid) continue;
            const std::uint64_t ta = b3live::now_ns();
            r.view.prof_ns.fill(0);
            r.view.apply_fov(f.cam, f.vehicles);
            b3gfx::FrameParams fp;
            b3view::frame_params(r.view.sky, f.cam.m.r[3], f.cam.m.r[2], r.args.fog, r.args.specular, fp);
            b3replay::game_view_proj(f.cam, aspect, fp.view_proj);
            r.view.car_frame(fp);
            items.clear();
            r.view.render_aspect = aspect;
            r.draw_list(*dev, f, !r.args.no_cull, fp.eye, items);
            const std::uint64_t tb = b3live::now_ns();
            r.view.post_frame(f, aspect, fp);
            const std::uint64_t tc = b3live::now_ns();
            b3replay::HudProjection hp;
            std::memcpy(hp.view_proj, fp.view_proj, sizeof hp.view_proj);
            hp.vehicles = &f.vehicles;
            r.view.hud_list(*dev, f.hud, r.cap.hud.textures, aspect, items, h, w, &hp);
            const std::uint64_t t1 = b3live::now_ns();
            if (!nodraw && !dev->draw_offscreen_wait(w, h, fp, items)) { std::fprintf(stderr, "offscreen draw failed: %s\n", SDL_GetError()); return 1; }
            const std::uint64_t t2 = b3live::now_ns();
            const b3gfx::Device::Stats st = dev->take_stats();
            const auto& fx = r.view.fx_stats;
            cpu.push_back(double(t1 - t0) * 1e-6);
            gpu.push_back(double(t2 - t1) * 1e-6);
            std::fprintf(out, "%ld,%.3f,%.3f,%.3f,%zu,%u,%u,%.1f,%u,%zu,%zu,%d,%d,%d,%d,%d,%.3f,%.3f,%d,%.4f,%.4f,%.4f", poll, alpha, cpu.back(), gpu.back(), items.size(),
                         st.draws, st.uploads, double(st.upload_bytes) / 1024.0, st.buffer_grows, f.vehicles.size(), f.hud ? f.hud->draws.size() : std::size_t(0),
                         fx.shadows, fx.trail_points, fx.scrape_segments, fx.sparks, fx.popcorn, double(fp.blur.amount), double(fp.dblur.amount), fp.bloom.on ? 1 : 0,
                         double(f.cam.m.r[3][0]), double(f.cam.m.r[3][1]), double(f.cam.m.r[3][2]));
            auto ms = [](std::uint64_t ns) { return double(ns) * 1e-6; };
            const auto& pn = r.view.prof_ns;
            std::fprintf(out, ",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", ms(ta - t0), ms(pn[0]), ms(pn[1]), ms(pn[2]), ms(pn[3]), ms(pn[4]), ms(pn[5]),
                         ms(pn[6]), ms(tc - tb), ms(t1 - tc));
        }
    }
    std::fclose(out);
    auto pct = [](std::vector<double> v, double q) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, std::size_t(q / 100.0 * double(v.size())))]; };
    std::printf("profile: %zu frames, CPU ms p50/p95/max %.2f/%.2f/%.2f, GPU ms p50/p95/max %.2f/%.2f/%.2f -> %s\n", cpu.size(), pct(cpu, 50), pct(cpu, 95),
                pct(cpu, 100), pct(gpu, 50), pct(gpu, 95), pct(gpu, 100), r.args.profile.c_str());
    dev.reset();
    if (hidden) SDL_DestroyWindow(hidden);
    return 0;
}

// --traffic-scan (traffic-popin, view.h traffic_probe): every tick of the segment, each traffic car that comes within the
// 275 m draw radius (spawned, from beyond it, a reused slot) with its distance and screen position, the ticks of its
// fade-in, the cars that leave; then the counts and the on-screen appearances by distance
int run_traffic_scan(Replay& r) {
    const auto& s = r.segment();
    b3replay::TrafficProbeStats st;
    for (std::size_t i = s.first; i <= s.last; ++i) b3replay::traffic_probe(r.timeline, i, &st);
    std::printf("traffic scan, ticks %zu..%zu: appear spawn %ld (on screen %ld), from beyond 275 m %ld (%ld), slot reused %ld (%ld); "
                "leave: removed %ld (on screen %ld), out of range %ld\n", s.first, s.last, st.appear[0], st.appear_on_screen[0], st.appear[1],
                st.appear_on_screen[1], st.appear[2], st.appear_on_screen[2], st.gone_despawn, st.gone_despawn_on_screen, st.gone_range);
    std::printf("  on-screen appearances by game-camera distance: <50 m %ld, 50-100 %ld, 100-150 %ld, 150-200 %ld, 200-250 %ld, 250-275 %ld\n",
                st.dist_hist[0], st.dist_hist[1], st.dist_hist[2], st.dist_hist[3], st.dist_hist[4], st.dist_hist[5]);
    return 0;
}

int run_pop_scan(Replay& r) {
    b3gfx::Options opt;
    opt.msaa = 1;
    opt.driver = r.args.driver;
    std::string err;
    SDL_Window* hidden = nullptr;
    const char* how = "";
    auto dev = b3gfx::create_headless(opt, &err, &hidden, &how);
    if (!dev) { std::fprintf(stderr, "b3gfx: %s\n", err.c_str()); return 1; }
    b3asset::set_hd_compressed(dev->supports_bc());   // hdgpu: HD replacements stay BC on the GPU
    if (!r.load_track(*dev, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    const auto& s = r.segment();
    const int w = 320, h = 224;
    struct Pop { double share; std::uint32_t poll; std::string what; std::vector<std::uint8_t> a, b; };
    std::vector<Pop> pops;
    auto draw = [&](const b3replay::Frame& f, std::vector<std::uint8_t>& rgba, bool cull = false) {
        b3gfx::FrameParams fp;
        b3view::frame_params(r.view.sky, f.cam.m.r[3], f.cam.m.r[2], r.args.fog, r.args.specular, fp);
        b3replay::game_view_proj(f.cam, float(w) / float(h), fp.view_proj);
        r.view.car_frame(fp);
        std::vector<b3gfx::DrawItem> items;
        r.view.render_aspect = float(w) / float(h);
        r.draw_list(*dev, f, cull, fp.eye, items);
        return dev->draw_offscreen(w, h, fp, items, rgba);
    };
    long events = 0;
    for (std::size_t i = s.first + 1; i <= s.last; ++i) {
        const auto& t0 = r.cap.ticks[i - 1];
        const auto& t1 = r.cap.ticks[i];
        std::set<std::pair<int, int>> a0, a1;
        for (int j = 0; j < 17; ++j) { if (t0.slot_unit[j] >= 0) a0.insert({t0.slot_unit[j], t0.slot_kind[j]}); if (t1.slot_unit[j] >= 0) a1.insert({t1.slot_unit[j], t1.slot_kind[j]}); }
        auto f1 = b3replay::frame_at(r.timeline, i, 0.0f, false);
        auto f0 = r.args.cull_scan ? f1 : b3replay::frame_at(r.timeline, i - 1, 0.0f, false);
        ++events;
        if (r.args.lod_scan) {
            f1.fx = b3replay::FrameEffects{};
            f1.hud = nullptr;
            f1.vehicles.clear();
            auto fh = f1;
            std::string lods;
            for (int j = 0; j < 17; ++j)
                if (f1.slot_unit[j] >= 0 && f1.slot_kind[j] != 0) { fh.slot_kind[j] = 0; lods += " " + std::to_string(f1.slot_unit[j]); }
            if (lods.empty()) continue;
            Pop p;
            if (!draw(fh, p.a) || !draw(f1, p.b)) continue;
            long changed = 0;
            for (std::size_t k = 0; k + 3 < p.a.size(); k += 4) {
                int m = 0;
                for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(p.a[k + std::size_t(c)]) - int(p.b[k + std::size_t(c)])));
                changed += m > 32;
            }
            p.share = double(changed) / double(w * h);
            p.poll = t1.poll;
            p.what = " LOD units:" + lods + " (camera unit " + std::to_string(t1.current_unit) + ")";
            pops.push_back(std::move(p));
            std::sort(pops.begin(), pops.end(), [](const Pop& x, const Pop& y) { return x.share > y.share; });
            if (pops.size() > 40) pops.resize(40);
            continue;
        }
        if (r.args.vis_scan) {
            f1.fx = b3replay::FrameEffects{};
            f1.hud = nullptr;
            f1.vehicles.clear();
            std::string hidden;
            for (int j = 0; j < 17; ++j)
                if (f1.slot_unit[j] >= 0 && (f1.vis[j] & 3) == 0) hidden += " " + std::to_string(f1.slot_unit[j]) + (f1.slot_kind[j] ? "L" : "H");
            if (hidden.empty()) continue;
            Pop p;
            if (!draw(f1, p.a, true) || !draw(f1, p.b, false)) continue;
            long changed = 0;
            for (std::size_t k = 0; k + 3 < p.a.size(); k += 4) {
                int m = 0;
                for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(p.a[k + std::size_t(c)]) - int(p.b[k + std::size_t(c)])));
                changed += m > 32;
            }
            p.share = double(changed) / double(w * h);
            p.poll = t1.poll;
            p.what = " units culled by the game:" + hidden + " (camera unit " + std::to_string(t1.current_unit) + ")";
            pops.push_back(std::move(p));
            std::sort(pops.begin(), pops.end(), [](const Pop& x, const Pop& y) { return x.share > y.share; });
            if (pops.size() > 40) pops.resize(40);
            continue;
        }
        if (r.args.cull_scan) {
            f0.fx = f1.fx = b3replay::FrameEffects{};
            f0.hud = f1.hud = nullptr;
            std::map<int, int> why;
            std::vector<b3replay::Vehicle> keep;
            for (const auto& v : f1.vehicles) { if (v.cull == b3replay::Vehicle::kDrawn) keep.push_back(v); else if (v.cull != b3replay::Vehicle::kFar) ++why[int(v.cull)]; }
            if (keep.size() + 0 == f1.vehicles.size() || why.empty()) continue;
            f0.vehicles = keep;
            Pop p;
            if (!draw(f0, p.a) || !draw(f1, p.b)) continue;
            long changed = 0;
            for (std::size_t k = 0; k + 3 < p.a.size(); k += 4) {
                int m = 0;
                for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(p.a[k + std::size_t(c)]) - int(p.b[k + std::size_t(c)])));
                changed += m > 32;
            }
            p.share = double(changed) / double(w * h);
            p.poll = t1.poll;
            static const char* names[] = {"drawn", "far", "not listed", "unit hidden", "outside", "camera inside", "over cap"};
            for (const auto& [c, n] : why) p.what += std::string(" ") + names[c] + " " + std::to_string(n);
            p.what += " (camera unit " + std::to_string(t1.current_unit) + ")";
            pops.push_back(std::move(p));
            std::sort(pops.begin(), pops.end(), [](const Pop& x, const Pop& y) { return x.share > y.share; });
            if (pops.size() > 40) pops.resize(40);
            continue;
        }
        for (b3replay::Frame* f : {&f0, &f1}) { f->vehicles.clear(); f->fx = b3replay::FrameEffects{}; f->hud = nullptr; }
        f0.cam = f1.cam;
        f0.anim_valid = f1.anim_valid; f0.anim = f1.anim; f0.anim_now = f1.anim_now;
        Pop p;
        if (!draw(f0, p.a) || !draw(f1, p.b)) continue;
        long changed = 0;
        for (std::size_t k = 0; k + 3 < p.a.size(); k += 4) {
            int m = 0;
            for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(p.a[k + std::size_t(c)]) - int(p.b[k + std::size_t(c)])));
            changed += m > 32;
        }
        p.share = double(changed) / double(w * h);
        p.poll = t1.poll;
        for (const auto& u : a0) if (!a1.count(u)) p.what += " -" + std::to_string(u.first) + (u.second ? "L" : "H");
        for (const auto& u : a1) if (!a0.count(u)) p.what += " +" + std::to_string(u.first) + (u.second ? "L" : "H");
        for (int k = 0; k < 8; ++k) if (f0.backdrops[k] != f1.backdrops[k]) p.what += " backdrop[" + std::to_string(k) + "] " + std::to_string(f0.backdrops[k]) + "->" + std::to_string(f1.backdrops[k]);
        if (f0.crash.barrier_group != f1.crash.barrier_group) p.what += " barrier group " + std::to_string(f0.crash.barrier_group) + "->" + std::to_string(f1.crash.barrier_group);
        if (f0.hidden != f1.hidden) p.what += " hidden props " + std::to_string(f0.hidden.size()) + "->" + std::to_string(f1.hidden.size());
        if (!f0.props.empty() || !f1.props.empty()) p.what += " dynamic props " + std::to_string(f1.props.size());
        p.what += " (camera unit " + std::to_string(t1.current_unit) + ")";
        pops.push_back(std::move(p));
        std::sort(pops.begin(), pops.end(), [](const Pop& x, const Pop& y) { return x.share > y.share; });
        if (pops.size() > 40) pops.resize(40);
    }
    std::printf("pop scan, segment polls %u..%u: %ld ticks; largest visible pops (share of pixels changing > 32 levels):\n",
                r.cap.ticks[s.first].poll, r.cap.ticks[s.last].poll, events);
    for (std::size_t k = 0; k < pops.size(); ++k) {
        std::printf("  poll %u: %6.2f %%%s\n", pops[k].poll, 100.0 * pops[k].share, pops[k].what.c_str());
        if (k < 12) {
            b3asset::write_png_rgba(r.args.pop_scan + "/pop_" + std::to_string(pops[k].poll) + "_before.png", w, h, pops[k].a.data());
            b3asset::write_png_rgba(r.args.pop_scan + "/pop_" + std::to_string(pops[k].poll) + "_after.png", w, h, pops[k].b.data());
        }
    }
    dev.reset();
    if (hidden) SDL_DestroyWindow(hidden);
    SDL_Quit();
    return 0;
}

int run_window(Replay& r) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("b3replay", r.args.w, r.args.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                                                         (r.args.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    b3gfx::Options opt;
    opt.msaa = r.args.msaa;
    opt.dither = r.args.dither;   // perf1
    opt.driver = r.args.driver;
    if (r.args.pc) { b3replay::filter_options(r.args.pc->filter, opt); b3replay::scale_options(*r.args.pc, opt); }   // settings1
    if (r.args.pc && r.args.pc->dither) opt.dither = 1.0f;   // perf1: DITHER
    opt.present = r.args.vsync ? b3gfx::PresentMode::kVsync : b3gfx::PresentMode::kUncapped;
    std::string err;
    auto dev = b3gfx::Device::create(win, opt, &err);
    if (!dev) { std::fprintf(stderr, "b3gfx: %s\n", err.c_str()); return 1; }
    b3asset::set_hd_compressed(dev->supports_bc());   // hdgpu: HD replacements stay BC on the GPU
    auto load = [&]() -> bool {
        r.view.clear(*dev);
        if (!r.load_track(*dev, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return false; }
        std::printf("%s, route %s\n", r.segment_name().c_str(), r.fe ? "none (front end, 2D only)" : r.view.route.c_str());
        return true;
    };
    b3asset::HdTextures* const hd_pack = b3asset::hd_textures();   // --hd-pack: P toggles it
    if (!load()) return 1;
    std::printf("driver %s, MSAA %dx, present %s\n", dev->driver_name(), dev->msaa(), dev->present_mode_name());

    double t = 0;   // segment time
    if (r.args.from_poll >= 0) {
        const auto& s = r.segment();
        std::size_t i = s.first;
        while (i < s.last && long(r.cap.ticks[i].poll) < r.args.from_poll) ++i;
        t = double(i - s.first) / 60.0;
    }
    float speed = r.args.speed;
    bool running = true, paused = false, attached = true, interpolate = true, game_cull = false, looking = false;
    bool fog = r.args.fog, specular = r.args.specular;
    FreeCam fc;
    Uint64 last = SDL_GetPerformanceCounter(), title_at = last;
    const double freq = double(SDL_GetPerformanceFrequency());
    int frames = 0, w = r.args.w, h = r.args.h;
    std::vector<b3gfx::DrawItem> items;
    while (running) {
        const double len = double(r.segment().last - r.segment().first) / 60.0;
        SDL_Event e;
        std::vector<b3gfx::Device::ViewPass> split_views;   // gsoff1 (§29)
    b3gfx::FrameParams split_fp;
    while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if ((e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_LEFT) && !attached) { looking = true; SDL_SetWindowRelativeMouseMode(win, true); }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_LEFT) { looking = false; SDL_SetWindowRelativeMouseMode(win, false); }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (looking) { fc.yaw -= e.motion.xrel * 0.0025f; fc.pitch = std::fmax(-1.55f, std::fmin(1.55f, fc.pitch - e.motion.yrel * 0.0025f)); }
                break;
            case SDL_EVENT_MOUSE_WHEEL: fc.speed *= e.wheel.y > 0 ? 1.25f : 0.8f; break;
            case SDL_EVENT_KEY_DOWN: {
                const SDL_Keycode k = e.key.key;
                if (k == SDLK_ESCAPE) running = false;
                else if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)))
                    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                else if (k == SDLK_SPACE) paused = !paused;
                else if (k == SDLK_LEFT) t -= 5;
                else if (k == SDLK_RIGHT) t += 5;
                else if (k == SDLK_PAGEUP) t -= 30;
                else if (k == SDLK_PAGEDOWN) t += 30;
                else if (k == SDLK_HOME) t = 0;
                else if (k == SDLK_COMMA) { paused = true; t = std::floor(t * 60 - 0.5) / 60; }
                else if (k == SDLK_PERIOD) { paused = true; t = std::floor(t * 60 + 1.0) / 60; }
                else if (k == SDLK_EQUALS || k == SDLK_KP_PLUS) speed *= 2;
                else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) speed /= 2;
                else if (k == SDLK_I) interpolate = !interpolate;
                else if (k == SDLK_V) game_cull = !game_cull;
                else if (k == SDLK_G) fog = !fog;
                else if (k == SDLK_F) r.view.show_effects = !r.view.show_effects;
                else if (k == SDLK_L) r.view.show_coronas = !r.view.show_coronas;
                else if (k == SDLK_O) r.view.show_particles = !r.view.show_particles;
                else if (k == SDLK_X) r.view.show_sun = !r.view.show_sun;
                else if (k == SDLK_B) r.view.sun_occlusion = !r.view.sun_occlusion;
                else if (k == SDLK_N) r.view.show_blur = !r.view.show_blur;
                else if (k == SDLK_T) r.view.show_bloom = !r.view.show_bloom;
                else if (k == SDLK_Z) { r.view.post_game = !r.view.post_game; r.view.bloom_last = -1; }
                else if (k == SDLK_H) specular = !specular;
                else if (k == SDLK_U) r.view.show_hud = !r.view.show_hud;
                else if (k == SDLK_Y) r.view.hud_wide = !r.view.hud_wide;
                else if (k == SDLK_J) r.view.hud_sharp = !r.view.hud_sharp;
                else if (k == SDLK_K) r.view.hud_reproject = !r.view.hud_reproject;
                else if (k == SDLK_M) r.view.show_movies = !r.view.show_movies;   // menu1
                else if (k == SDLK_R) r.view.show_room = !r.view.show_room;   // fe2: the showroom / the car on black
                else if (k == SDLK_C) { if (attached) fc.from(r.at(t, interpolate).cam); attached = !attached; }
                else if (k == SDLK_TAB && r.cap.segments.size() + r.cap.fe_segments.size() > 1) { r.next_segment(); t = 0; if (!load()) return 1; }
                else if (k == SDLK_P && hd_pack) {   // HD pack on/off: track, cars and effects reload (the HUD keeps its startup textures)
                    b3asset::set_hd_textures(b3asset::hd_textures() ? nullptr : hd_pack);
                    if (!load()) return 1;
                }
                break;
            }
            default: break;
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        const float dt = float(double(now - last) / freq);
        last = now;
        if (!paused) t += dt * speed;
        t = std::clamp(t, 0.0, len);
        const auto f = r.at(t, interpolate);
        if (!attached) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            float fw[3];
            fc.forward(fw);
            const float right[3] = {-std::cos(fc.yaw), 0, std::sin(fc.yaw)};
            const float sp = fc.speed * dt * ((keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) ? 4.0f : 1.0f);
            for (int i = 0; i < 3; ++i) {
                if (keys[SDL_SCANCODE_W]) fc.pos[i] += fw[i] * sp;
                if (keys[SDL_SCANCODE_S]) fc.pos[i] -= fw[i] * sp;
                if (keys[SDL_SCANCODE_D]) fc.pos[i] += right[i] * sp;
                if (keys[SDL_SCANCODE_A]) fc.pos[i] -= right[i] * sp;
            }
            if (keys[SDL_SCANCODE_E]) fc.pos[1] += sp;
            if (keys[SDL_SCANCODE_Q]) fc.pos[1] -= sp;
        }
        b3gfx::FrameParams fp;
        const float aspect = float(w) / float(h > 0 ? h : 1);
        if (r.fe) {}   // menu1: the front end (View::frontend_frame below)
        else if (attached) {
            b3replay::game_view_proj(f.cam, aspect, fp.view_proj);
            b3view::frame_params(r.view.sky, f.cam.m.r[3], f.cam.m.r[2], fog, specular, fp);
        } else {
            fc.view_proj(2.0f * std::atan(f.cam.half_h), aspect, fp.view_proj);
            float fw[3];
            fc.forward(fw);
            b3view::frame_params(r.view.sky, fc.pos, fw, fog, specular, fp);
        }
        r.view.car_frame(fp);
        items.clear();
        const b3replay::Camera free_cam = fc.camera(f.cam);
        r.view.corona_view = attached ? nullptr : &free_cam;
        r.view.free_view = !attached;   // perf1 (§33.7)
        r.view.render_aspect = aspect;
        if (!r.fe) r.draw_list(*dev, f, game_cull && attached, fp.eye, items);
        r.view.corona_view = nullptr;
        r.view.post_frame(f, aspect, fp);
        b3replay::HudProjection hp;
        std::memcpy(hp.view_proj, fp.view_proj, sizeof hp.view_proj);
        hp.vehicles = &f.vehicles;
        if (r.fe) r.view.frontend_frame(*dev, f, r.cap.heads, r.cap.hud.textures, aspect, w, h, fp, items);
        else r.view.hud_list(*dev, f.hud, r.cap.hud.textures, aspect, items, h, w, &hp);
        if (!r.fe && attached && f.split) {   // gsoff1 (§29): the 2-player split screen, two views (the free camera keeps one)
            items.clear();
            r.view.split_frame(*dev, f, r.cap.heads, r.cap.hud.textures, game_cull, fog, specular, w, h, 2.0f * aspect, aspect, split_views, split_fp, items);
            dev->draw_views(split_views, split_fp, items, &w, &h);
        } else
        dev->draw_frame(fp, items, &w, &h);
        ++frames;
        if (double(now - title_at) / freq >= 0.5) {
            const double secs = double(now - title_at) / freq;
            const auto& tk = r.cap.ticks[f.tick];
            char title[320];
            std::snprintf(title, sizeof title,
                          "b3replay - poll %u +%.2f - %.1f / %.1f s x%.2g%s - %s camera - interp %s - %s - %.0f fps (%.2f ms) - %dx%d - %s",
                          tk.poll, f.alpha, t, len, speed, paused ? " (paused)" : "", r.fe ? "front end" : attached ? "game" : "free", interpolate ? "on" : "off",
                          game_cull ? "game culling" : "all resident units", frames / secs, 1000.0 * secs / frames, w, h, dev->present_mode_name());
            SDL_SetWindowTitle(win, title);
            frames = 0;
            title_at = now;
        }
    }
    dev.reset();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

// modkit1b / sdk2b: the mods' files over the disc files this renderer reads itself (under g_mods_m)
std::mutex g_mods_m;
bool g_mods_ready = false, g_mods_override = false;
std::string g_mods_dir, g_mods_game, g_mods_ids, g_mods_hash;
std::string g_mods_gameplay;   // media1 (records.h): the set's hash when a mod of it changes gameplay ("": none)
// patches3: the set's [textures] packs, first wins; b3replay_main gives them to hd_enable (g_mods_hd_done from then on)
std::vector<std::string> g_mods_textures;
bool g_mods_hd_done = false;
void build_mod_overlay(const std::string& ids, const std::string& hash) {
    static std::vector<std::unique_ptr<b3mod::Overlay>> kept;   // the files' paths outlive every reader (the whole run)
    auto o = std::make_unique<b3mod::Overlay>();
    if (!ids.empty() && ids != "none") {
        const std::vector<b3mod::Mod> found = b3mod::discover(b3mod::path_from_utf8(g_mods_dir));
        const b3mod::Selection sel = b3mod::select(found, b3mod::split_ids(ids));
        for (const std::string& e : sel.errors) std::printf("[b3render] mods: %s\n", e.c_str());
        // patches3: the mods' patches from the same cache native_play filled (applied again only if it is gone)
        b3mod::PatchEnv penv;
        penv.cache = b3mod::patch_cache(b3mod::path_from_utf8(g_mods_dir));
        const fs::path disc_root = g_mods_game;
        penv.read_disc = [&disc_root](const std::string& key, std::vector<std::uint8_t>& out) {
            const fs::path p = b3asset::disc::resolve_ci(disc_root, key);
            return !p.empty() && b3asset::disc::read_file(p, out);
        };
        *o = b3mod::Overlay::build(sel.mods, true, &penv);
        for (const std::string& e : o->errors()) std::printf("[b3render] mods: %s\n", e.c_str());
    }
    // patches3: the mods' [textures] packs, above the player's own HD pack (hd_enable in b3replay_main). The HD pack is
    // opened once: a narrowing (b3r_mods_set) that comes after that leaves the packs as they were for this run (they
    // change only the picture; the game, the disc files and the set hash follow the narrowed set).
    std::vector<std::string> textures;
    for (const fs::path& t : o->texture_packs()) textures.push_back(b3mod::path_utf8(t));
    if (!g_mods_hd_done) g_mods_textures = std::move(textures);
    else if (textures != g_mods_textures)
        std::printf("[b3render] mods: the HD textures were set up before the set changed: the mods' texture packs stay as "
                    "they were until the next run\n");
    const std::string got = o->empty() ? std::string() : b3mod::hex64(o->set_hash());
    if (!hash.empty() && got != hash)
        std::printf("[b3render] mods: WARNING: set hash %s here, the game's is %s (a mod's files changed since the game read "
                    "them): the picture may differ from the game\n", got.empty() ? "(none)" : got.c_str(), hash.empty() ? "(none)" : hash.c_str());
    b3asset::disc::set_overlay(g_mods_game, o->winners());
    std::printf("[b3render] mods: %s over %s (%zu files)\n", b3mod::describe(*o).c_str(), g_mods_game.c_str(), o->file_count());
    // media1: a set that changes gameplay keeps its ghosts and best times apart (src/b3replay/records.h)
    // (named by the gameplay-changing mods alone: a texture mod added or moved keeps the folder)
    std::string gameplay_ids;
    for (const b3mod::ActiveMod& m : o->mods())
        if (b3mod::changes_gameplay_any(m)) gameplay_ids += (gameplay_ids.empty() ? "" : ",") + m.id;
    g_mods_gameplay = b3mod::gameplay_set_hash(o->mods());
    if (!gameplay_ids.empty())
        std::printf("[b3render] mods: %s change gameplay: ghosts and best times go to the folders' modded-%s\n", gameplay_ids.c_str(),
                    g_mods_gameplay.c_str());
    std::fflush(stdout);
    kept.push_back(std::move(o));
}

}  // namespace

// sdk2b (b3r_mods_set, src/b3replay/inproc.cpp): the set native_play runs with once its code mods have loaded
void b3replay_mods_set(const char* ids, const char* hash) {
    std::lock_guard<std::mutex> lock(g_mods_m);
    g_mods_override = true;
    g_mods_ids = ids ? ids : "";
    g_mods_hash = hash ? hash : "";
    if (g_mods_ready) build_mod_overlay(g_mods_ids, g_mods_hash);
}

// b3render.dll (inproc1, src/b3replay/inproc.cpp) runs the same entry on its render thread; b3replay.exe's main calls it.
int b3replay_main(int argc, char** argv) {
    Replay r;
    if (!parse(argc, argv, r.args)) {
        std::fprintf(stderr, "usage: b3replay <capture.b3scene> [--game <install>/game] [--segment N] [--from-poll P] [--speed X]"
                             " [--msaa N] [--vsync] [--size WxH] [--fullscreen] [--car-lod N] | --offscreen out.png --poll P | --offscreen <dir> --sweep A:B:S [--no-cull]; any: --polls A:B (load only those polls)"
                             " [--alpha A] [--free-eye x,y,z [--look-at x,y,z]] [--no-deform] [--no-crash] [--no-anim] | --info [--dump-zones <file>]\n"
                             "       any mode: [--hd-pack <PCSX2 replacements dir> (else $B3_HD_PACK)] [--no-hd]\n"
                             "       b3replay --live <name> [--latency low|smooth|newest] [--margin MS] [--lead T] [--frames-in-flight N] [--latency-log CSV] [--delay TICKS] [--max-fps N] [--offscreen-live <dir> [--shots P[+A],...] [--bench WxH] [--wait S]]\n");
        return 1;
    }
    // Default install: the repo's data/game (not committed; any OS, run from the repo root). native_play passes --game.
    if (r.args.game.empty()) r.args.game = "data/game";
    // modkit1b (docs/modding/DESIGN.md §1.2): the mods the game runs with (native_play passes its active set) over the
    // disc files this renderer reads itself -- the route lookup, whole-track loads, the car scan, textures -- so the
    // game and the picture never disagree. No --mods: the disc alone, as before.
    // sdk2b: native_play may narrow the set once its code mods have loaded (b3r_mods_set: a code mod that did not load
    // is left out with its files); whichever comes first, the start-up set or that call, the later one stands.
    {
        std::lock_guard<std::mutex> lock(g_mods_m);
        g_mods_dir = r.args.mods_dir;
        g_mods_game = r.args.game;
        g_mods_ready = true;
        if (g_mods_override) build_mod_overlay(g_mods_ids, g_mods_hash);
        else if (!r.args.mods.empty()) build_mod_overlay(r.args.mods, r.args.mods_hash);
    }
    // mac5: the native game's window keeps its PC SETTINGS in the user's prefs (the page saves there); offscreen runs
    // (tests) never read a user's file
    if (r.args.settings_path.empty() && r.args.native_pad && r.args.live_out.empty() && !std::getenv("B3_NO_USER_SETTINGS")) {
        if (const std::string pref = b3io::user_dir_slash(); !pref.empty()) {   // portable mode: b3port/user_dir.h
            r.args.settings_path = pref + "pc_settings.ini";
        }
    }
    // remap1: the control bindings likewise (offscreen runs: only with --controls; else the defaults)
    if (r.args.controls_path.empty() && r.args.native_pad && r.args.live_out.empty() && !std::getenv("B3_NO_USER_SETTINGS")) {
        if (const std::string pref = b3io::user_dir_slash(); !pref.empty()) {   // portable mode: b3port/user_dir.h
            r.args.controls_path = pref + "controls.ini";
        }
    }
    // ghost1: the native game's window keeps its Time Attack ghosts in the user's prefs too; offscreen runs only with
    // --ghost-dir (tests never touch a user's folder)
    if (r.args.ghost_dir.empty() && r.args.native_pad && r.args.live_out.empty() && !std::getenv("B3_NO_USER_SETTINGS")) {
        if (const std::string pref = b3io::user_dir_slash(); !pref.empty()) {   // portable mode: b3port/user_dir.h
            r.args.ghost_dir = pref + "ghosts";
        }
    }
    // speedrun1: the speedrun timer's best times likewise (offscreen runs: only with --speedrun-dir; else in memory)
    if (r.args.speedrun_dir.empty() && r.args.native_pad && r.args.live_out.empty() && !std::getenv("B3_NO_USER_SETTINGS")) {
        if (const std::string pref = b3io::user_dir_slash(); !pref.empty()) {   // portable mode: b3port/user_dir.h
            r.args.speedrun_dir = pref + "speedrun";
        }
    }
    std::string run_state_path;   // mac5: safe mode's run state (empty: not a window run of the native game)
    bool safe = false, safe_forced = false;
    bool xbox_on = false;         // xboximport: XBOX TEXTURES (the settings')
    if (!r.args.settings_path.empty()) {   // settings1: the device / window / loop rows into the arguments
        b3settings::Settings s;
        std::string why;
        if (!b3settings::load_file(r.args.settings_path, s, &why)) std::printf("[b3settings] %s: the defaults\n", why.c_str());
        else if (!why.empty()) std::printf("[b3settings] %s: %s\n", r.args.settings_path.c_str(), why.c_str());
        // mac5: safe mode (a window run of the native game only): two unclean runs in a row, or asked for
        if (r.args.native_pad && r.args.live_out.empty()) {
            run_state_path = (fs::path(r.args.settings_path).parent_path() / "run_state.ini").string();
            const char* env = std::getenv("B3_SAFE_MODE");
            const bool force = r.args.safe_mode || (env && std::strcmp(env, "1") == 0);
            const b3settings::RunState prev = b3settings::load_run_state(run_state_path);
            const b3settings::StartDecision d = b3settings::start_run(prev, force);
            b3settings::save_run_state(run_state_path, d.now);
            safe = d.safe;
            safe_forced = d.forced;
            if (safe) {
                s = b3settings::safe_settings(s);
                std::printf("[b3settings] SAFE MODE (%s): this run uses low settings; %s is kept\n",
                            d.forced ? "asked for" : "the last runs did not end cleanly", r.args.settings_path.c_str());
            }
            else if (d.now.unclean) std::printf("[b3settings] the last run did not end cleanly (%d in a row)\n", d.now.unclean);
        }
        Args& a = r.args;
        a.pc = s;
        a.msaa = s.msaa;
        a.vsync = a.vsync || s.vsync;
        a.fullscreen = a.fullscreen || s.fullscreen;
        if (s.frame_cap > 0) a.max_fps = s.frame_cap;
        a.specular = a.specular && s.specular;
        if (!s.hd_textures) a.no_hd = true;
        xbox_on = s.xbox_textures;   // xboximport
        if (a.hd_pack.empty() && !s.hd_pack_dir.empty()) a.hd_pack = s.hd_pack_dir;   // mac5: the folder chosen in SETTINGS / the launcher
        a.fe_layout = s.fe_layout;
        a.movie_fill = s.movie_fill;
        if (s.latency > 0) { a.latency = s.latency == 1 ? "low" : s.latency == 2 ? "smooth" : "newest"; a.latency_set = true; }   // lat1 rows (else the command line's)
        if (s.lead >= 0) a.lead = double(s.lead) / 100.0;
        if (s.frames_in_flight > 0) a.frames_in_flight = s.frames_in_flight;
        std::printf("[b3settings] %s: preset %s, MSAA %d, filter %d, fov %d\n", a.settings_path.c_str(),
                    b3settings::preset_name(b3settings::matching_preset(s)), s.msaa, int(s.filter), s.fov);
    }
    {   // patches3: the mods' [textures] packs of the set built so far (the start-up set, or native_play's narrowing)
        std::lock_guard<std::mutex> lock(g_mods_m);
        r.args.mod_textures = g_mods_textures;
        g_mods_hd_done = true;
    }
    {   // xboximport: the Import Xbox textures pack in the user data folder (beside pc_settings.ini), below the player's pack;
        // $B3_XBOX_TEXTURES names one (the checks' offscreen runs: on whatever the settings say)
        // (UTF-8 throughout: the user data folder holds the user's name, e.g. C:/Users/José/AppData/Roaming)
        std::string xbox;
        const std::optional<std::string> e = b3io::getenv("B3_XBOX_TEXTURES");   // (wide on Windows)
        if (e && !e->empty()) {
            xbox = *e;
            xbox_on = true;
        } else if (!r.args.settings_path.empty()) {
            const fs::path d = b3io::u8path(r.args.settings_path).parent_path() / "xbox-textures";
            std::error_code ec;
            if (fs::is_regular_file(d / "import.ini", ec)) xbox = b3io::u8string(d);
        }
        b3asset::set_hd_xbox_folder(xbox);
        b3asset::hd_enable(r.args.hd_pack, r.args.no_hd, r.args.mod_textures, xbox_on ? xbox : std::string());   // patches3: the mods' packs first
    }
    if (!r.args.live.empty()) {
        b3replay::LiveOptions o;
        const Args& a = r.args;
        o.name = a.live; o.game = a.game; o.driver = a.driver; o.out_dir = a.live_out;
        o.msaa = a.msaa; o.w = a.w; o.h = a.h; o.size_set = a.size_set; o.car_lod = a.car_lod;
        o.vsync = a.vsync; o.fullscreen = a.fullscreen; o.fog = a.fog; o.specular = a.specular; o.effects = a.effects; o.coronas = a.coronas; o.particles = a.particles; o.sun = a.sun; o.sun_test = a.sun_test; o.blur = a.blur; o.blur_radial = a.blur_radial; o.bloom = a.bloom; o.radial_scale = a.radial_scale; o.post_game = a.post_game; o.hud = a.hud; o.movies = a.movies; o.hud_wide = a.hud_wide; o.fe_layout = a.fe_layout; o.movie_fill = a.movie_fill; o.whole_track = a.whole_track; o.hud_sharp = a.hud_sharp; o.hud_reproject = a.hud_reproject; o.room = a.room; o.reflection = a.reflection; o.fe_shadow = a.fe_shadow; o.globe = a.globe; o.room_env = a.room_env;
        o.delay = a.delay; o.clock_old = a.clock_old; o.max_fps = a.max_fps; o.shots = a.shots; o.bench_w = a.bench_w; o.bench_h = a.bench_h; o.wait_writer_s = a.wait; o.verify = a.verify;
        o.activate = a.activate;
        o.fps_counter = a.fps_counter; o.route_header = a.route_header;   // gsoff1
        o.native_pad = a.native_pad; o.native_audio = a.native_audio;   // play1
        o.latency = a.latency; o.latency_log = a.latency_log; o.margin_ms = a.margin_ms; o.lead = a.lead; o.blend_ms = a.blend_ms; o.dither = a.dither; o.frames_in_flight = a.frames_in_flight;   // lat1
        o.latency_auto = !a.latency_set;   // perf4
        o.bloom_warmup = a.bloom_warmup;
        o.settings = a.pc;   // settings1
        o.settings_path = a.settings_path;   // mac5: the PC SETTINGS page saves there
        o.controls_path = a.controls_path;   // remap1: the bindings (the PC SETTINGS page's CONTROLS saves there)
        o.ghost_dir = a.ghost_dir;           // ghost1
        o.speedrun_dir = a.speedrun_dir;     // speedrun1
        o.replay_playback = a.replay_playback;   // media1
        o.gameplay_set = [] {   // media1: the mod set's own records folder (b3replay_mods_set may narrow the set later)
            std::lock_guard<std::mutex> lock(g_mods_m);
            return g_mods_gameplay;
        };
        if (a.inproc) {
            o.inproc = b3live::inproc_instance();
            if (!o.inproc) { std::fprintf(stderr, "b3replay: --inproc: no in-process feed (only inside b3render.dll)\n"); return 1; }
        }
        o.safe_mode = safe;
        o.safe_forced = safe_forced;
        o.mods_safe_off = a.mods_safe_off;   // modkit1b
        if (!a.live_out.empty()) return b3replay::run_live_offscreen(o);
        const int rc = b3replay::run_live_window(o);
        // mac5: the window's run ended cleanly (a crash or a killed hang never gets here): safe mode's count cleared.
        // nofallback1: not when the game stopped on a missing function (b3r_game_stopped): that run counts as a crash
        if (!run_state_path.empty() && b3replay::game_stopped().empty()) b3settings::save_run_state(run_state_path, b3settings::clean_end());
        return rc;
    }
    std::string err;
    if (!r.load_capture(&err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    r.pick_segment();
    if (r.args.info) return run_info(r);
    if (r.args.traffic_scan) return run_traffic_scan(r);
    if (!r.args.pop_scan.empty()) return run_pop_scan(r);
    if (!r.args.profile.empty()) return run_profile(r);
    if (!r.args.offscreen.empty()) return run_offscreen(r);
    return run_window(r);
}

#ifndef B3REPLAY_LIBRARY
int main(int argc, char** argv) { return b3replay_main(argc, argv); }
#endif
