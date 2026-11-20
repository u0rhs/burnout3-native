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
#include "fastforward.h"   // fastload1
#include "modoverlay.h"   // sdk2b
#include "carlookpanel.h"
#include "tuneconsole.h"   // tune4
#include "live.h"          // nofallback1: set_game_stopped
#include "b3report/thread_name.h"   // split2p

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
// rumble1: the game's motors on the pad in `port` (0 / 1) for this VBlank: small 0 / 1, large 0-255
B3R_EXPORT void b3r_rumble(int port, int small, int large) {
    b3replay::NativePad::instance().set_motors(port, static_cast<std::uint8_t>(small != 0), static_cast<std::uint8_t>(std::clamp(large, 0, 255)));
}
// mac4: the key map's context for the next samples: 1 while the race simulation runs (W / Up accelerate, S / Down
// brake, Space boost), 0 in menus and paused (W / Up / S / Down move, Space confirms); src/native/input_keys.h.
B3R_EXPORT void b3r_pad_context(int driving) { b3replay::NativePad::instance().set_driving(driving != 0); }
// todrace (tools/native/race_time.h): a track page is up (each VBlank), and the TIME OF DAY presses taken
// there since the last call
B3R_EXPORT void b3r_race_time_page(int up) { b3replay::NativePad::instance().set_race_time_page(up != 0); }
B3R_EXPORT int b3r_race_time_presses() { return b3replay::NativePad::instance().take_race_time_presses(); }
// play1: the native game's sound -- one VBlank's frames (48 kHz s16 stereo, L R interleaved) from native_play's
// AudioSink; played when b3replay runs with --native-audio and a device opened, else counted and dropped.
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
B3R_EXPORT int b3r_photo_row() { return b3replay::PcPage::instance().settings().photo_row ? 1 : 0; }
B3R_EXPORT int b3r_photo_enter() { return b3replay::PhotoMode::instance().request_enter("pause menu") ? 1 : 0; }
// mac5: the date format the game should write (b3settings::Settings::date_format; tools/native/date_format.h)
B3R_EXPORT int b3r_date_format() { return b3replay::PcPage::instance().settings().date_format; }
// units1: the speed units the game should show (b3settings::Settings::speed_units; tools/native/speed_units.h)
B3R_EXPORT int b3r_speed_units() { return b3replay::PcPage::instance().settings().speed_units; }
// replay1: PC SETTINGS > GAME > MODES > RECORD REPLAYS (native_play reads it once, at the start: src/native/replay_file.h)
B3R_EXPORT int b3r_record_replays() { return b3replay::PcPage::instance().settings().record_replays; }
// fastload1 (tools/native/fast_load.h): PC SETTINGS > GAME > STARTING AND LOADING > FAST LOADING (b3settings::Settings::fast_loading), read
// by native_play each VBlank; and native_play's word that a load runs unpaced (1) or is over (0): the display then
// shows the newest frame (fastforward.h)
B3R_EXPORT int b3r_fast_loading() { return b3replay::PcPage::instance().settings().fast_loading ? 1 : 0; }
// autosave1 (tools/native/save_screens.h): PC SETTINGS > SAVES > SAVE SCREENS, Hide (1, default) / Show (0)
B3R_EXPORT int b3r_hide_save_screens() { return b3replay::PcPage::instance().settings().hide_save_screens ? 1 : 0; }
B3R_EXPORT void b3r_fast_forward(int on) { b3replay::fast_forward().store(on != 0, std::memory_order_relaxed); }
// loadlag1 (fastforward.h): native_play at a fast load's end, the game held in its VBlank hook (wall time only): waits
// until the renderer has taken every record (the queue empty, then two more passes of its loop) and has built the route
// the STATIC.DAT header named (scene_busy off), at most `max_ms`. Returns the milliseconds waited.
B3R_EXPORT int b3r_scene_wait(int max_ms) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto ms = [&] { return int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()); };
    constexpr std::uint64_t kNone = ~std::uint64_t(0);
    std::uint64_t empty_at = kNone;   // the loop's epoch when the queue was seen empty
    while (g_feed && !g_done && ms() < max_ms) {
        if (g_feed->queued_bytes() > 0) empty_at = kNone;
        else if (empty_at == kNone) empty_at = b3replay::scene_epoch().load(std::memory_order_acquire);
        else if (b3replay::scene_epoch().load(std::memory_order_acquire) >= empty_at + 2 && !b3replay::scene_busy().load(std::memory_order_relaxed)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ms();
}
// the custom soundtrack (remaster-features §6; tools/native/custom_soundtrack.h): the playlist mode (0 EA Trax only,
// 1 custom only, 2 mixed) and the music folder (UTF-8) copied to `folder` (at most n - 1 bytes)
B3R_EXPORT int b3r_music(char* folder, int n) { return b3replay::PcPage::instance().music(folder, n); }
B3R_EXPORT int b3r_music_volume() { return b3replay::PcPage::instance().music_volume(); }   // musicvol1
// nowplaying1 (tools/native/now_playing_popup.h): PC SETTINGS > AUDIO > MUSIC > OTHER MUSIC APPS (0 off, 1 show
// another app's song in the EA TRAX popup), read by native_play each VBlank; and what native_play detects (UTF-8, one
// line; empty: not reading), shown in that row's description
B3R_EXPORT int b3r_other_music_apps() { return b3replay::PcPage::instance().settings().other_music_apps ? 1 : 0; }
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
        b3replay::PhotoMode::instance().set_game_poll(h[1]);
        b3replay::GarageCam::instance().note_poll(h[1]);   // garagecam1: the right stick / drags from this poll on
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
