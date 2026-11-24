// b3render.dll (inproc1, docs/native-renderer.md §23): the native renderer inside the game process.
//
// The game's scene hook (src/gameview/scene_capture.cpp, B3PORT_RENDER=<this dll>) loads this library only when
// asked, and talks to it through the plain C functions below; only bytes cross, and this side copies them, so the
// game's C++ runtime and this library's (linked statically, with SDL3 3.4.2) never share an object.
//
// b3r_open starts the render thread, which runs b3replay's own live mode (b3replay_main with --inproc: LiveFeed on
// the in-process feed, include/b3live/inproc.h; the same decode, view and draw code as `b3replay --live`). The game
// thread calls b3r_want_key / b3r_submit once per retail poll: a copy onto a lock-free list, never a wait. b3r_close
// marks the end of the feed and waits (bounded) for the render thread to drain it and finish (shots, summaries).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>
#ifdef _WIN32
#define SDL_MAIN_HANDLED   // SDL_RegisterApp only; no SDL main
#include <SDL3/SDL_main.h>
#endif

#include "b3live/inproc.h"
#include "b3gfx/perf.h"
#include "nativeaudio.h"   // play1
#include "nativepad.h"   // play1
#include "menumouse.h"  // mac5
#include "pcpage.h"     // mac5
#include "photomode.h"  // photo mode
#include "garagecam.h"  // garagecam1
#include "orbitcam.h"   // orbitcam1
#include "fastforward.h"   // fastload1
#include "game_behind.h"   // slowcpu
#include "modoverlay.h"   // sdk2b
#include "carlookpanel.h"
#include "tuneconsole.h"   // tune4
#include "live.h"          // nofallback1: set_game_stopped
#include "b3report/thread_name.h"   // split2p
#include "b3report/uncaught.h"      // musicexit
#include "b3port/user_dir.h"         // extrastart: the user's pc_settings.ini
#include "b3settings/settings_path.h"  // extrastart: the renderer's rule
#include "view.h"                   // extrastart: extra_cars_now (CUSTOM / BETA of an older install)
#include "../../tools/native/extra_series.h"   // extrastart: ExtraCar

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define B3R_EXPORT extern "C" __declspec(dllexport)
#else
#define B3R_EXPORT extern "C" __attribute__((visibility("default")))
#endif

int b3replay_main(int argc, char** argv);   // main.cpp

namespace {

b3live::Inproc* g_feed = nullptr;
std::vector<std::string> g_args;
std::thread g_thread;
std::atomic<int> g_done{0}, g_rc{0};
bool g_on_caller = false;   // play1: b3r_open_main (the renderer runs on the caller's thread)

// Splits on spaces and tabs; "..." groups (no escapes).
std::vector<std::string> split_args(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_word = false, quoted = false;
    for (; s && *s; ++s) {
        const char c = *s;
        if (c == '"') { quoted = !quoted; in_word = true; continue; }
        if (!quoted && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            if (in_word) out.push_back(cur);
            cur.clear();
            in_word = false;
            continue;
        }
        cur += c;
        in_word = true;
    }
    if (in_word) out.push_back(cur);
    return out;
}

void render_main() {
    b3port::name_this_thread("render");   // split2p (the crash report): the window, its pads and the draws
#ifdef _WIN32
    // Below the game: the game's thread must never wait for this one's CPU time.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    std::vector<char*> argv;
    for (std::string& a : g_args) argv.push_back(a.data());
    argv.push_back(nullptr);
    const int rc = b3replay_main(int(argv.size()) - 1, argv.data());
    std::printf("[b3render] render thread ended (rc %d)\n", rc);
    std::fflush(stdout);
    g_rc = rc;
    g_done = 1;
}

}  // namespace

// Starts the renderer: `args` as b3replay's command line after the program name (e.g. "--game <dir> --offscreen-live
// <dir> --shots 2500,3000"); --inproc is added. Returns 0 on success.
B3R_EXPORT int b3r_open(const char* args) {
    // play1: already open is success -- native_play opens the renderer before the boot (the window and its pad from
    // the start), and the capture opens it again at the first poll; the first opener's arguments stand.
    if (g_feed) return 0;
#ifdef _WIN32
    // musicexit: this DLL links its own libstdc++, so its own std::terminate handler: an uncaught exception in the
    // renderer's code named in the run log with its frames (include/b3report/uncaught.h). Elsewhere the C++ runtime is
    // shared and native_play's handler (tools/native/crash_report.h) already covers this library.
    b3port::install_uncaught_report("b3render.dll");
#endif
    g_feed = new b3live::Inproc();
    b3live::inproc_instance() = g_feed;
    g_args = split_args(args);
    g_args.insert(g_args.begin(), "b3render");
    g_args.push_back("--inproc");
    const bool offscreen = [] { for (const auto& a : g_args) if (a == "--offscreen-live") return true; return false; }();
    // This library's own SDL (static): its video driver is set here with override priority, so the game's environment
    // (SDL_VIDEO_DRIVER=dummy in headless runs) does not reach it.
#ifdef _WIN32
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, offscreen ? "offscreen" : "windows", SDL_HINT_OVERRIDE);
#else
    // pic1: libb3render.so (the native backend off Windows): SDL's own choice for a window (wayland / x11 / cocoa)
    if (offscreen) SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);
#endif
#ifdef _WIN32
    // Our own window class, under this module: another SDL in the same process (an SDL3.dll) may have registered
    // SDL's default class name "SDL_app" under the exe's instance already.
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&b3r_open), &self);
    if (!SDL_RegisterApp("b3render", 0, self)) std::fprintf(stderr, "[b3render] SDL_RegisterApp: %s\n", SDL_GetError());
#endif
    std::string line;
    for (const auto& a : g_args) line += " " + a;
    std::printf("[b3render] open:%s\n", line.c_str());
    std::fflush(stdout);
    if (g_on_caller) return 0;   // play1: b3r_open_main -- the caller runs the renderer (b3r_run_main)
    g_thread = std::thread(render_main);
    return 0;
}

// play1 (native_play): the renderer on the caller's thread instead of its own, for hosts where windows belong to the
// main thread (macOS: SDL3 / Cocoa). b3r_open_main prepares it as b3r_open does (the feed is open: submits queue);
// b3r_run_main then runs it there and returns when it ends (the window closed and the feed ended, or the feed closed
// by b3r_close from the game's thread). Returns the renderer's exit code.
B3R_EXPORT int b3r_open_main(const char* args) {
    if (g_feed) return 0;
    g_on_caller = true;
    return b3r_open(args);
}
B3R_EXPORT int b3r_run_main() {
    if (!g_feed || !g_on_caller || g_done) return -1;
    render_main();
    return g_rc;
}

// play1 (native_play, src/b3replay/nativepad.h): the window as the native game's pad (b3replay --native-pad).
// The DS2 report for this VBlank into out[32]: keys composed with the gamepads, the steering ramp advanced by
// `dt` seconds. Returns 1 with a report, 0 when the pad is off (no --native-pad) or the renderer is not open.
// pad2 (local 2-player): port 1 = the game's port 2, the second gamepad's report (NativePad::sample2): 1 with a report
// while a pad is in player 2's seat, 0 while port 2 is empty (one gamepad or none: always 0); `dt` unused.
B3R_EXPORT int b3r_pad_sample(int port, unsigned char* out, float dt) {
    auto& p = b3replay::NativePad::instance();
    if (!g_feed || port < 0 || port > 1 || !out || !p.enabled()) return 0;
    if (port == 1) {
        std::array<std::uint8_t, 32> s{};
        if (!p.sample2(s)) return 0;
        std::memcpy(out, s.data(), s.size());
        return 1;
    }
    const auto s = p.sample(dt);
    std::memcpy(out, s.data(), s.size());
    return 1;
}
// Scripted input for tests: an SDL scancode down (1) / up (0), as the window's key event.
B3R_EXPORT void b3r_pad_key(int scancode, int down) {
    // padlayout: kTestPadCode.. the scripted test pad's inputs (B3NATIVE_TEST_PAD; ignored without it)
    if ((scancode > 0 && scancode < 512) || (scancode >= b3replay::NativePad::kTestPadCode && scancode < b3replay::NativePad::kTestPadCode + 64))
        b3replay::NativePad::instance().key(static_cast<std::uint16_t>(scancode), down != 0);
}
// rumble1 (src/b3replay/nativepad.h, src/native/pad_actuator.h): 1 while PC SETTINGS > RUMBLE is On (native_play then
// has the game's actuator requests acknowledged), else 0 (also with no pad: no --native-pad)
B3R_EXPORT int b3r_rumble_mode() {
    const auto& p = b3replay::NativePad::instance();
    return p.enabled() && p.rumble_on() ? 1 : 0;
}
// carsel1 (src/native/input_keys.h ShoulderSeries): 1 while PC SETTINGS > CAR SERIES ON L1 / R1 is On (native_play then
// gives L1 / R1 presses on the car select page to the game as Left / Right), else 0 (also with no pad: no --native-pad)
B3R_EXPORT int b3r_series_shoulders() {
    const auto& p = b3replay::NativePad::instance();
    return p.enabled() && p.series_shoulders() ? 1 : 0;
}
// carstats1 / blinkers1 / rrparts1 (tools/native/car_stats.h, turn_signals.h, road_rage_parts.h): PC SETTINGS > CAR STATS (1 the GRIP and
// ACCELERATION bars on SELECT CAR), TURN SIGNALS (1 the racers' indicators before bends), ROAD RAGE PARTS (1 every loose
// part can come off in an offline Road Rage)
// extras (b3settings::extras_bits): PC SETTINGS > EXTRAS as one word -- the SELECT OPTIONS rows, modes and series the
// player has on (0: the game as it shipped). native_play reads it each VBlank (B3NATIVE_EXTRAS overrides it)
// extrastart (pre4: CUSTOM / BETA never loaded): native_play asks for the word at its setup, before this renderer has
// run (b3r_open_main: it runs only from b3r_run_main) and so before it has loaded the settings -- the defaults then
// (ELIMINATOR alone), and the extra cars, read once at the start, were left out. Until the page has loaded them, the
// word comes from the file the renderer will load, by its rule (main.cpp: --settings, else a window run's user
// pc_settings.ini), with an older install's CUSTOM / BETA resolved by their folders as main.cpp does.
namespace {
// Resolved once, when the renderer's arguments are known (b3r_open), then read without a lock or a copy (Fable: with no
// settings file -- offscreen runs without --settings, a window run without a user folder -- the page never loads, and
// every getter comes here for the whole run)
std::atomic<bool> g_early_ready{false};
b3settings::Settings g_early;   // (written once, before g_early_ready; the defaults when there is no file)
const b3settings::Settings& early_settings() {
    if (g_early_ready.load(std::memory_order_acquire)) return g_early;
    static const b3settings::Settings kDefaults{};
    static std::mutex m;   // (native_play's main thread and the game's thread may both ask first)
    std::lock_guard<std::mutex> lock(m);
    if (g_early_ready.load(std::memory_order_relaxed)) return g_early;
    if (!g_feed) return kDefaults;   // (before b3r_open: the arguments not known yet; not cached)
    std::string path;
    bool native_pad = false, offscreen = false;
    for (std::size_t i = 0; i < g_args.size(); ++i) {
        if (g_args[i] == "--settings" && i + 1 < g_args.size()) path = g_args[i + 1];
        else if (g_args[i] == "--native-pad") native_pad = true;
        else if (g_args[i] == "--offscreen-live") offscreen = true;
    }
    path = b3settings::settings_path_for(path, native_pad, offscreen, std::getenv("B3_NO_USER_SETTINGS") != nullptr, b3io::user_dir_slash());
    if (!path.empty()) {
        b3settings::Settings s;
        std::string why;
        if (b3settings::load_file(path, s, &why) && (s.extra_custom_cars < 0 || s.extra_beta_cars < 0)) {
            bool custom = false, beta = false;
            for (const b3native::ExtraCar& c : b3replay::extra_cars_now().cars) (c.series == b3native::kBetaSeries ? beta : custom) = true;
            b3settings::resolve_extra_cars(s, custom, beta);
        }
        g_early = s;
    }
    g_early_ready.store(true, std::memory_order_release);
    return g_early;
}
}  // namespace
// Every getter below that reads the PC settings: the page's once it has loaded them, else the file's (the start: the
// extras word, RACE REPLAY's and RECORD REPLAYS' hooks, the first VBlanks' START AT, IDLE PAUSE, units ...)
const b3settings::Settings& settings_now() {
    auto& page = b3replay::PcPage::instance();
    return page.loaded() ? page.settings() : early_settings();
}
B3R_EXPORT int b3r_extras() { return int(b3settings::extras_bits(settings_now())); }
B3R_EXPORT int b3r_car_stats() { return settings_now().car_stats != 0 ? 1 : 0; }
B3R_EXPORT int b3r_turn_signals() { return settings_now().turn_signals != 0 ? 1 : 0; }
B3R_EXPORT int b3r_road_rage_parts() { return settings_now().road_rage_parts != 0 ? 1 : 0; }
// rumble1: the game's motors on the pad in `port` (0 / 1) for this VBlank: small 0 / 1, large 0-255
B3R_EXPORT void b3r_rumble(int port, int small, int large) {
    b3replay::NativePad::instance().set_motors(port, static_cast<std::uint8_t>(small != 0), static_cast<std::uint8_t>(std::clamp(large, 0, 255)));
}
// mac4: the key map's context for the next samples: 1 while the race simulation runs (W / Up accelerate, S / Down
// brake, Space boost), 0 in menus and paused (W / Up / S / Down move, Space confirms); src/native/input_keys.h.
// padskip (tools/native/pad_context.h): 2 in the game's replay: the menus' bindings, the orbit camera still taken.
B3R_EXPORT void b3r_pad_context(int driving) { b3replay::NativePad::instance().set_driving(driving == 1, driving != 0); }
// todrace (tools/native/race_time.h): a track page is up (each VBlank), and the TIME OF DAY presses taken
// there since the last call
B3R_EXPORT void b3r_race_time_page(int up) { b3replay::NativePad::instance().set_race_time_page(up != 0); }
B3R_EXPORT int b3r_race_time_presses() { return b3replay::NativePad::instance().take_race_time_presses(); }
// paintsel (tools/native/paint_select.h): SELECT CAR takes input for `player` (0 / 1; -1: not up), each VBlank; and the
// game's own colour changed there for `player` (that player's CAR PAINT goes back to Game: PcPage::paint_game)
B3R_EXPORT void b3r_paint_page(int player) { b3replay::NativePad::instance().set_paint_page(player); }
B3R_EXPORT void b3r_paint_game(int player) { b3replay::PcPage::instance().paint_game(player); }
// nextstart: PC SETTINGS' APPLY AND RESTART chosen since the last call (native_play ends the run, then starts the game again)
B3R_EXPORT int b3r_restart_requested() { return b3replay::PcPage::instance().take_restart() ? 1 : 0; }
// paintsel: the CAR PAINT PANEL presses taken on SELECT CAR since the last call (native_play acts on them at its VBlank),
// and the panel opened for `player` (1) or asked to close (0) at once (-1: PC SETTINGS is open: neither)
B3R_EXPORT int b3r_paint_presses() { return b3replay::NativePad::instance().take_paint_presses(); }
// gearbox (tools/native/gearbox.h): the SHIFT UP (1) / SHIFT DOWN (0) presses since the last call
B3R_EXPORT int b3r_shift_presses(int up) { return b3replay::NativePad::instance().take_shift_presses(up != 0); }
// party1 (tools/native/party_modes.h): PARTY MODES' buttons since the last call (NativePad::take_party_keys)
B3R_EXPORT int b3r_party_keys() { return b3replay::NativePad::instance().take_party_keys(); }
// replayview: REPLAY presses since the last call; RACE REPLAY (GAME > MODES; $B3_REPLAYS=0 / 1 wins)
B3R_EXPORT int b3r_replay_presses() { return b3replay::NativePad::instance().take_replay_presses(); }
B3R_EXPORT void b3r_set_replay_showing(int on) { b3replay::NativePad::instance().set_replay_showing(on != 0); }   // replaycams
B3R_EXPORT int b3r_replays() {
    if (const char* e = std::getenv("B3_REPLAYS"); e && (e[0] == '0' || e[0] == '1') && !e[1]) return e[0] - '0';
    return settings_now().replays ? 1 : 0;
}
B3R_EXPORT int b3r_paint_toggle(int player) { return b3replay::PcPage::instance().paint_toggle(player); }
// play1: the native game's sound -- one VBlank's frames (48 kHz s16 stereo, L R interleaved) from native_play's
// AudioSink; played when b3replay runs with --native-audio and a device opened, else counted and dropped.
// stutter: the output queue's dry runs so far (native_play logs each with its VBlank)
// loadaudio: the game's sound is silent on purpose from here (1) until 0: a fast load's muted span and its wait for the
// race scene (tools/native/fast_load.h AudioGate); a dry run meanwhile is not counted as late
B3R_EXPORT void b3r_audio_quiet(int on) { b3replay::NativeAudio::instance().set_quiet(on != 0); }
B3R_EXPORT unsigned long long b3r_audio_late() { return static_cast<unsigned long long>(b3replay::NativeAudio::instance().late()); }
B3R_EXPORT void b3r_audio_push(const std::int16_t* lr, std::size_t frames) {
    if (lr && frames) b3replay::NativeAudio::instance().push(lr, frames);
}
// mac5: 1 while the game's window has the focus (native_play holds the game while it is 0: a Mac report of
// 2026-11-08, the intro movies played unseen behind another window)
B3R_EXPORT int b3r_focused() {
    const auto& p = b3replay::NativePad::instance();
    return p.focused() || !p.hold_unfocused() ? 1 : 0;   // PAUSE WHEN UNFOCUSED off: always "focused"
}
// mac5: the main menu's PC SETTINGS row (native_play, tools/native/pc_menu.h): open the page (pcpage.h); 1 while it is
// open (the game then sees a neutral pad)
B3R_EXPORT int b3r_settings_open() {
    if (!g_feed || !b3replay::NativePad::instance().enabled()) return 0;
    b3replay::PcPage::instance().request_open();
    return 1;
}
// (also 1 while photo mode holds the pad: photomode.h -- the game sees it neutral the same way)
B3R_EXPORT int b3r_settings_active() {
    return b3replay::PcPage::instance().active() || b3replay::PhotoMode::instance().holds_input() ||
                   b3replay::TuneConsole::instance().open() || b3replay::CarLookPanel::instance().open()   // tune4: the F9 overlay holds the pad too (reflect2: the CAR LOOK panel)
               ? 1
               : 0;
}
// tune4 (src/b3replay/tuneconsole.h, include/b3tune/tune_abi.h): native_play's tuning console for the DEVELOPER tab and
// the F9 overlay (null: none in this run)
B3R_EXPORT void b3r_set_tune_hooks(const B3TuneHooks* hooks) { b3replay::TuneConsole::instance().set_hooks(hooks); }
// photo mode (photomode.h): native_play says when the race's pause menu is up (1) or gone (0); photo mode can be entered
// only then, and ends with it
B3R_EXPORT void b3r_set_paused(int on) { b3replay::PhotoMode::instance().set_game_paused(on != 0); }
// photorow1 (tools/native/pc_menu.h): the pause menu's PHOTO MODE row. PC SETTINGS > GAME > INTERFACE > PHOTO MODE IN PAUSE MENU
// (1, default: the row is there), and the row's Confirm: photo mode on as with P (1: entered)
// options3 (tools/native/game_mods.h): the rows that change what the game does on screen, as a mask (bit 0 CRASH CAMERA
// Off); $B3_CRASH_CAMERA=0|1 wins (tests)
B3R_EXPORT int b3r_game_mods() {
    const auto& s = settings_now();   // (bit 0 not used: CRASH CAMERA is drawn only, crashhold.h)
    int deform = std::clamp(s.deformation, 0, 4);   // options3: DEFORMATION (bits 1-3; tools/native/game_mods.h)
    if (const char* e = std::getenv("B3_DEFORMATION"); e && e[0] >= '0' && e[0] <= '4' && !e[1]) deform = e[0] - '0';
    return deform << 1;
}
B3R_EXPORT int b3r_photo_row() { return settings_now().photo_row ? 1 : 0; }
// settingsback (tools/native/settings_back.h): SETTINGS BACK BUTTON, 1 Keep changes, 0 Cancel (the game's)
B3R_EXPORT int b3r_settings_back() { return settings_now().settings_back_keeps ? 1 : 0; }
B3R_EXPORT int b3r_photo_enter() { return b3replay::PhotoMode::instance().request_enter("pause menu") ? 1 : 0; }
// mac5: the date format the game should write (b3settings::Settings::date_format; tools/native/date_format.h)
B3R_EXPORT int b3r_date_format() { return settings_now().date_format; }
// units1: the speed units the game should show (b3settings::Settings::speed_units; tools/native/speed_units.h)
B3R_EXPORT int b3r_speed_units() { return settings_now().speed_units; }
// gamemods1: START AT (tools/native/boot_boost.h), IDLE PAUSE and END VERY LONG EVENTS (tools/native/game_limits.h)
B3R_EXPORT int b3r_boot_to() { return settings_now().boot_to; }
B3R_EXPORT int b3r_idle_pause() { return settings_now().idle_pause ? 1 : 0; }
B3R_EXPORT int b3r_long_event_end() { return settings_now().long_event_end ? 1 : 0; }
// hudmods: TAKEDOWN STREAK PITCH (b3settings::Settings::takedown_pitch; tools/native/takedown_pitch.h), read at each takedown
B3R_EXPORT int b3r_takedown_pitch() { return settings_now().takedown_pitch; }
// replay1: PC SETTINGS > GAME > MODES > RECORD REPLAYS (native_play reads it once, at the start: src/native/replay_file.h)
B3R_EXPORT int b3r_record_replays() { return settings_now().record_replays; }
// fastload1 (tools/native/fast_load.h): PC SETTINGS > GAME > STARTING AND LOADING > FAST LOADING (b3settings::Settings::fast_loading), read
// by native_play each VBlank; and native_play's word that a load runs unpaced (1) or is over (0): the display then
// shows the newest frame (fastforward.h)
// raceopts (tools/native/race_options.h): PC SETTINGS > TIME OF DAY (1 Night), the SELECT OPTIONS row's "(Default)"
B3R_EXPORT int b3r_night_setting() { return settings_now().time_of_day == 1 ? 1 : 0; }
// weather (race_options.h): PC SETTINGS > WEATHER (1 Rain), CONDITIONS' "(Default)"
B3R_EXPORT int b3r_weather_setting() { return settings_now().weather == 1 ? 1 : 0; }
// weather: the game's EFFECTS volume (0..100), read by native_play from the game's settings bytes; the rain's sound follows it
B3R_EXPORT void b3r_effects_volume(int pct) { b3replay::NativeAudio::instance().set_effects_volume(float(std::clamp(pct, 0, 100)) / 100.0f); }
// gameopts (tools/native/game_option_rules.h's word): PC SETTINGS > GAME > GAMEPLAY, bit 0 MIRROR MODE, bit 1 CRASHBREAKER IN
// EVERY MODE, bits 2..3 CRASHBREAKER AFTER (0..3; only with the Crashbreaker on)
B3R_EXPORT int b3r_game_options() {
    const auto& s = settings_now();
    // party1: bits 4..11 PARTY MODES (tools/native/game_option_rules.h party_word's layout)
    const auto c3 = [](int v) { return std::clamp(v, 0, 3); };
    return (s.mirror_mode ? 1 : 0) | (s.crashbreaker_all ? 2 | std::clamp(s.crashbreaker_cars, 0, 3) << 2 : 0) | c3(s.party_crazy_ai) << 4 |
           (s.party_tuk_tuks ? 1 << 6 : 0) | c3(s.party_moon_jump) << 7 | (s.party_crash ? 1 << 9 : 0) | (s.party_ai_driver ? 1 << 10 : 0) |
           (s.party_kamikaze ? 1 << 11 : 0) | (s.crashbreaker_all && s.crashbreaker_full ? 0x1000 : 0);   // gamemods1: Full (bit 12)
}
// trafficcheck (tools/native/traffic_check.h): PC SETTINGS > GAME > GAMEPLAY > TRAFFIC CHECKING (0 Off, 1 Revenge, 2 Every
// car), read by native_play when a race starts
B3R_EXPORT int b3r_traffic_check() { return std::clamp(settings_now().traffic_check, 0, 2); }
B3R_EXPORT int b3r_fast_loading() { return settings_now().fast_loading ? 1 : 0; }
// autosave1 (tools/native/save_screens.h): PC SETTINGS > SAVES > SAVE SCREENS, Hide (1, default) / Show (0)
B3R_EXPORT int b3r_hide_save_screens() { return settings_now().hide_save_screens ? 1 : 0; }
// trafmem: 1 while the offscreen shot runner's hold keeps more than LiveFeed::kHoldWaitBytes of decoded ticks (a shot
// still pending far behind an unpaced game): native_play holds the game (wall time only) until it clears
static std::atomic<std::uint32_t> g_game_poll{0};   // trafmem: the newest poll the game has pushed (note_game_poll)
// (and while the ticks from the hold to the game's newest poll -- pushed, not all decoded yet -- would: the renderer decodes
// the queue while the game waits, so the decoded ticks alone came over the drop's bound)
// trafmem: the hold's poll (UINT32_MAX: none): native_play's wait goes on while it moves (the renderer taking shots)
B3R_EXPORT unsigned b3r_feed_hold() { return b3replay::feed_hold_poll().load(std::memory_order_acquire); }
B3R_EXPORT int b3r_feed_full() {
    if (b3replay::feed_hold_full().load(std::memory_order_acquire)) return 1;
    return b3replay::LiveFeed::hold_ahead_full(b3replay::feed_hold_poll().load(std::memory_order_acquire),
                                               g_game_poll.load(std::memory_order_relaxed),
                                               b3replay::feed_tick_bytes().load(std::memory_order_acquire)) ? 1 : 0;
}
B3R_EXPORT void b3r_fast_forward(int on) { b3replay::fast_forward().store(on != 0, std::memory_order_relaxed); }
// slowcpu (game_behind.h): native_play's pacer, 1 while the game is behind real time (the renderer holds to 60 meanwhile)
B3R_EXPORT void b3r_game_behind(int on) { b3replay::game_behind().store(on != 0, std::memory_order_relaxed); }
// loadlag1 (fastforward.h): native_play at a fast load's end, the game held in its VBlank hook (wall time only): waits
// until the renderer has taken every record (the queue empty, then two more passes of its loop) and has built the route
// the STATIC.DAT header named (scene_busy off), at most `max_ms` -- or longer, up to kSceneBuildMaxMs, while that route or
// the vehicle preload is still being made (scenewait, fastforward.h). Returns the milliseconds waited.
B3R_EXPORT int b3r_scene_wait(int max_ms) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto ms = [&] { return int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()); };
    constexpr std::uint64_t kNone = ~std::uint64_t(0);
    std::uint64_t empty_at = kNone;   // the loop's epoch when the queue was seen empty
    b3replay::vehicles_preload_open().store(true, std::memory_order_release);   // vpreload (fastforward.h)
    const std::uint32_t asked = b3replay::vehicles_preload_asked().fetch_add(1, std::memory_order_acq_rel) + 1;
    // scenewait (fastforward.h scene_wait_more; Fable): past `max_ms` only while the scene is actually being made: the route's
    // decode or GPU half running, the preload running, or the route pending with the render loop going round (its epoch
    // moved in the last 100 ms: the frame between the worker's end and its pickup); never with the window away
    std::uint64_t epoch_seen = b3replay::scene_epoch().load(std::memory_order_acquire);
    int epoch_at = 0;
    const auto building = [&] {
        const int now = ms();
        if (const std::uint64_t e = b3replay::scene_epoch().load(std::memory_order_acquire); e != epoch_seen) { epoch_seen = e; epoch_at = now; }
        if (b3replay::window_away().load(std::memory_order_relaxed)) return false;
        return b3replay::route_building().load(std::memory_order_acquire) || b3replay::vehicles_preload_running().load(std::memory_order_acquire) ||
               (b3replay::scene_busy().load(std::memory_order_relaxed) && now - epoch_at < 100);
    };
    while (g_feed && !g_done && b3replay::scene_wait_more(ms(), max_ms, building())) {
        if (g_feed->queued_bytes() > 0) empty_at = kNone;
        else if (empty_at == kNone) empty_at = b3replay::scene_epoch().load(std::memory_order_acquire);
        else if (b3replay::scene_epoch().load(std::memory_order_acquire) >= empty_at + 2 && !b3replay::scene_busy().load(std::memory_order_relaxed) &&
                 b3replay::vehicles_preload_done().load(std::memory_order_acquire) >= asked) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // vpreload: the wait is over (answered, its cap, the quit): a preload still running stops, one not started never runs
    b3replay::vehicles_preload_open().store(false, std::memory_order_release);
    std::uint32_t done = b3replay::vehicles_preload_done().load(std::memory_order_acquire);
    while (done < asked && !b3replay::vehicles_preload_done().compare_exchange_weak(done, asked, std::memory_order_acq_rel)) {}
    return ms();
}
// the custom soundtrack (remaster-features §6; tools/native/custom_soundtrack.h): the playlist mode (0 EA Trax only,
// 1 custom only, 2 mixed) and the music folder (UTF-8) copied to `folder` (at most n - 1 bytes)
B3R_EXPORT int b3r_music(char* folder, int n) {
    if (!b3replay::PcPage::instance().loaded()) {   // extrastart: the file's until the renderer has loaded it
        const b3settings::Settings& s = settings_now();
        if (folder && n > 0) {
            const std::size_t k = std::min(s.music_folder.size(), std::size_t(n - 1));
            std::memcpy(folder, s.music_folder.data(), k);
            folder[k] = 0;
        }
        return s.playlist_mode;
    }
    return b3replay::PcPage::instance().music(folder, n);
}
B3R_EXPORT int b3r_music_volume() { return b3replay::PcPage::instance().loaded() ? b3replay::PcPage::instance().music_volume() : settings_now().custom_music_volume; }   // musicvol1
B3R_EXPORT int b3r_music_loudness() { return b3replay::PcPage::instance().loaded() ? b3replay::PcPage::instance().music_loudness() : settings_now().custom_music_loudness; }   // musicloud: CUSTOM MUSIC LOUDNESS
B3R_EXPORT int b3r_sound_output() { return b3replay::PcPage::instance().loaded() ? b3replay::PcPage::instance().sound_output() : settings_now().sound_output; }   // audioq: SOUND OUTPUT (0 Clean, 1 Original)
// nowplaying1 (tools/native/now_playing_popup.h): PC SETTINGS > AUDIO > MUSIC > OTHER MUSIC APPS (0 off, 1 show
// another app's song in the EA TRAX popup), read by native_play each VBlank; and what native_play detects (UTF-8, one
// line; empty: not reading), shown in that row's description
B3R_EXPORT int b3r_other_music_apps() { return settings_now().other_music_apps ? 1 : 0; }
B3R_EXPORT void b3r_now_playing(const char* line) { b3replay::PcPage::instance().set_now_playing(line ? line : ""); }

// sdk2b (the code-mod SDK, native_play's src/native/code_mods.h): the code mods' settings rows for PC SETTINGS > MODS
// (b3mod::rows_to_text), a value the game side changed, the page's changes ("<mod>.<key>=<value>", one per call; 0:
// none), a mod's texture for its overlay, and the mod set after the code mods loaded (the renderer then reads the same
// files: those of a mod that did not load are left out)
B3R_EXPORT void b3r_mod_rows(const char* text) { b3replay::PcPage::instance().set_mod_rows(text ? text : ""); }
B3R_EXPORT void b3r_mod_value(const char* qualified, const char* value) {
    if (qualified && value) b3replay::PcPage::instance().set_mod_value(qualified, value);
}
B3R_EXPORT int b3r_mod_take(char* out, int n) { return b3replay::PcPage::instance().take_mod_change(out, n); }
B3R_EXPORT void b3r_mod_texture(unsigned id, unsigned w, unsigned h, const unsigned char* rgba) {
    b3replay::ModOverlay::instance().add_texture(id, w, h, rgba);
}
void b3replay_mods_set(const char* ids, const char* hash);   // main.cpp
B3R_EXPORT void b3r_mods_set(const char* ids, const char* hash) { b3replay_mods_set(ids ? ids : "", hash ? hash : ""); }
// save1: the native game's save card for the page's SAVES tab (pcpage.h SaveHooks); null pointers: no tab
B3R_EXPORT void b3r_set_save_hooks(int (*info)(char*, int), void (*request)(int, const char*), int (*status)(char*, int)) {
    b3replay::SaveHooks h;
    h.info = info;
    h.request = request;
    h.status = status;
    b3replay::PcPage::instance().set_save_hooks(h);
}
// mac5 (remaster-features §8; include/b3live/menu_input.h, src/b3replay/menumouse.h): the mouse in the game's menus.
// The actions for the game (3 u32 each: b3live::menu_action_put) since the last call, at most `max`; native_play turns
// them into pad presses (tools/native/menu_mouse.h).
B3R_EXPORT int b3r_menu_actions(std::uint32_t* out, int max) {
    return out && max > 0 ? b3replay::MenuMouse::instance().take(out, max) : 0;
}
// Scripted mouse for tests (native_play --mouse): kind 0 move to (x, y) (fractions of the window), 1 button n down,
// 2 up (1 left, 3 right), 3 wheel n steps (> 0 up), as the window's events.
B3R_EXPORT void b3r_pad_mouse(int kind, float x, float y, int n) { b3replay::NativePad::instance().mouse(kind, x, y, n); }
// The game's on-screen keyboard is up (1) / down (0): the window's keys type (src/b3replay/nativepad.h set_text_mode).
B3R_EXPORT void b3r_text_mode(int on) { b3replay::NativePad::instance().set_text_mode(on != 0); }
// The typed characters since the last call (ASCII; 8 Backspace, 10 Enter), at most `max`.
B3R_EXPORT int b3r_text_take(char* out, int max) { return out && max > 0 ? b3replay::NativePad::instance().take_text(out, max) : 0; }
// Scripted text for tests (native_play --text), as typed in the window.
B3R_EXPORT void b3r_pad_text(const char* utf8) { b3replay::NativePad::instance().type(utf8); }
// 1 once the window was closed (the game should end), or the render thread has ended.
B3R_EXPORT int b3r_quit_requested() { return (b3replay::NativePad::instance().quit_requested() || g_done) ? 1 : 0; }
// nofallback1 (native_play's stop path): the game stopped on a function this build has no native code for ("<address>
// <name>"); the window then stays, with the message and F10's bug report, until it is closed (live.h set_game_stopped)
B3R_EXPORT void b3r_game_stopped(const char* function) { b3replay::set_game_stopped(function ? function : ""); }

// perf3 (include/b3gfx/perf.h): the game thread's stage times into the frame timing capture (B3_PERF_LOG). b3r_perf_on is 1
// when the capture is on (the render thread starts it, so ask again until it is on: a bare bool, free); b3r_perf_add is one
// sample (stage 0 game step, 1 pacing wait, 2 poll hook, 3 hand-over; ns).
B3R_EXPORT int b3r_perf_on() { return b3perf::on() ? 1 : 0; }
B3R_EXPORT void b3r_perf_add(int stage, std::uint64_t ns) {   // 0 step, 1 pace, 2 poll, 3 hand-over
    if (stage >= 0 && b3perf::kStageFrameCount + stage < b3perf::kStageCount) b3perf::sample(b3perf::kStageFrameCount + stage, ns);
}

// True (1) when the next record must be a keyframe (the first one, after a dropped record, or on the renderer's request).
B3R_EXPORT int b3r_want_key() { return g_feed && g_feed->want_key() ? 1 : 0; }

// Photo mode's game time (photomode.h): the poll a record carries (its TICK head's second word), handed over on the game
// thread before the record, so the keys that follow on that thread are stamped after it
static void note_game_poll(const void* body, std::size_t n) {
    std::uint32_t h[2];
    if (!body || n < sizeof h) return;
    std::memcpy(h, body, sizeof h);
    if (h[0] == 0x4B434954U) {   // 'TICK'
        g_game_poll.store(h[1], std::memory_order_relaxed);
        b3replay::PhotoMode::instance().set_game_poll(h[1]);
        b3replay::GarageCam::instance().note_poll(h[1]);   // garagecam1: the right stick / drags from this poll on
        b3replay::OrbitCam::instance().note_poll(h[1]);    // orbitcam1: the race's right sticks / drags
    }
}

// One poll's record (kind 1 delta / 2 keyframe; body = the 16-byte TICK head and its runs, as in a capture file).
// Returns 1 if queued, 0 if dropped. Never blocks.
B3R_EXPORT int b3r_submit(std::uint32_t kind, const void* body, std::size_t n) {
    note_game_poll(body, n);
    return g_feed && g_feed->push(kind, body, n) ? 1 : 0;
}

// lat1: the same with the record's schedule stamp: the host steady-clock time (ns) the input of the tick this record
// shows was latched (the adapter's schedule stamp, b3gv::Host; 0 = none). The display clock's low-latency
// modes follow these stamps instead of the arrival times.
B3R_EXPORT int b3r_submit_at(std::uint32_t kind, const void* body, std::size_t n, std::uint64_t sched_ns) {
    note_game_poll(body, n);
    return g_feed && g_feed->push(kind, body, n, sched_ns) ? 1 : 0;
}

// The session's end: no more records. Waits up to `timeout_ms` for the render thread to drain and finish.
// Returns the renderer's exit code, or -1 if it is still running (it is then left running; the process ends anyway).
B3R_EXPORT int b3r_close(std::uint32_t timeout_ms) {
    if (!g_feed) return -1;
    if (!g_on_caller && !g_thread.joinable()) return g_done ? g_rc.load() : -1;   // play1: closed already
    g_feed->close();
    const std::uint64_t until = b3live::now_ns() + std::uint64_t(timeout_ms) * 1000000ull;
    while (!g_done && b3live::now_ns() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::printf("[b3render] feed closed: %llu polls, %llu records (%llu keyframes), %llu dropped, %llu bytes still queued; renderer %s\n",
                static_cast<unsigned long long>(g_feed->polls.load()), static_cast<unsigned long long>(g_feed->records.load()),
                static_cast<unsigned long long>(g_feed->keyframes.load()), static_cast<unsigned long long>(g_feed->dropped.load()),
                static_cast<unsigned long long>(g_feed->queued_bytes()), g_done ? "finished" : "still running (timeout)");
    std::fflush(stdout);
    if (!g_done) { if (g_thread.joinable()) g_thread.detach(); return -1; }
    if (g_thread.joinable()) g_thread.join();   // (b3r_open_main: the caller's thread ran it)
    return g_rc;
}
