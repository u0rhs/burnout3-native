// native_play -- the native game, interactive (play1): boot2's native backend in real time, with the game view in a
// window (pic1's gameview layer + the in-process renderer b3render) and the window as the game's pad (keys and SDL
// gamepads: src/native/input_keys, src/b3replay/nativepad.h). native_boot stays the headless, strict proof tool; this
// is a separate program on the same backend, so boot2's proofs are untouched.
//
//   native_play [--elf <SLUS_210.50>] [--game <disc folder or image>] [--offscreen] [--render-lib <lib>] [--render-args "..."]
//               [--hz 59.94] [--no-pace] [--quit-after <s>] [--keys <vblank>:<scancode>:<0|1>,...] [--strict] [--no-audio]
//
//   --elf / B3_ELF        the retail executable (checked by size and FNV-1a 64); default <game>/SLUS_210.50
//   --game / B3_GAME_DIR  the disc: a folder of its files (DATA, TRACKS, PVEH, ...) or the disc image (.iso / raw .bin /
//                         .chd, read in place: b3asset/discfs.h); the platform and the renderer read it
//   --offscreen           no window (the renderer's offscreen live mode: build/gameview); the pad still takes --keys
//   --hz / --no-pace      real time: each virtual VBlank waits for its wall-clock time (NTSC 59.94 Hz [C PS2 NTSC field
//                         rate]); a host that falls more than 250 ms behind resyncs (counted) instead of bursting
//   --pace-from N       tests: unpaced up to VBlank N, real time from there (a race measured at the game's own rate
//                         without waiting through the front end in real time)
//   --fast-loading on|off fastload1 (tools/native/fast_load.h): a paced run stops pacing while the game's loading screen
//                         is up (the same VBlanks, sooner). Default: PC SETTINGS > DISPLAY > FAST LOADING (b3r_fast_loading;
//                         B3NATIVE_FAST_LOADING=0|1 also overrides it). The loading screen's start and end are logged either way
//   --save-screens hide|show autosave1 (tools/native/save_screens.h): the AUTOSAVE page after a race is not shown (the display
//                         holds the page before it; a paced run does not wait through it). Default: PC SETTINGS > SAVES >
//                         SAVE SCREENS (b3r_hide_save_screens; B3NATIVE_SAVE_SCREENS=show|hide also overrides it)
//   --quit-after S        end after S wall-clock seconds (tests); B3NATIVE_MAX_VBLANKS still applies
//   --keys                scripted pad keys (SDL scancodes) at VBlank numbers, through the window pad's own path
//   --keys2               fe1: a second controller (port 2, connected from the start) driven by its own scripted keys
//                         (same format, the default key map, the same driving / menus context): the 2-player modes
//   --strict              no fallback (B3NATIVE_STRICT=1's behaviour): a missing function ends the run; so does a VU0
//                         launch that no generated program covers (vu0n; a dev build would interpret it). nofallback1:
//                         the default build (make NATIVE_FALLBACK=0, every release) has no fallback at all -- no R5900
//                         interpreter in the binary (tools/native/native_fallback.h) -- so a missing function always
//                         ends the run there. Either way the end is the stop path: "native_play: STOPPED: ..." with
//                         the function's address and name in the log, the window held with the message and F10's bug
//                         report until it is closed, a message box, and the run left marked as not ended cleanly
//                         (b3settings/safemode.h: the launcher's "Last run", safe mode after two). Test hook:
//                         B3NATIVE_TEST_MISSING=<hex>[,<hex>] switches those native functions off, so their first call
//                         takes that path (in a NATIVE_FALLBACK=1 build without --strict they run retail instead)
//   --explore-movies      EXPLORATION only (B3NATIVE_MOVIE_STANDIN=1 too): every movie ends at once
//                         (tools/native/explore_standins.h), logged per movie
//   --explore-retail A,B  EXPLORATION only (B3NATIVE_EXPLORE_RETAIL too): these functions (hex) run as retail code
//                         in the fallback, not natively (a known-wrong native version; explore_standins.h)
//   --explore-return A=V  EXPLORATION only (B3NATIVE_EXPLORE_RETURN too): these fallback functions return V at once
//                         without running (an unmodelled platform call the exploration does not need)
//                         The three --explore options and B3NATIVE_DIFF need the fallback: a NATIVE_FALLBACK=1 build
//                         (make NATIVE_FALLBACK=1 build/native_play), not --strict
//   --no-idle-skip        spin through the guest's busy-waits as a proof run does (default: a busy-wait that finds
//                         nothing to do jumps virtual time to the next event: platform::skip_to_next_event)
//   --no-audio            silent (the platform still mixes RW Audio on virtual time; window mode plays it through
//                         b3render's SDL audio device, b3r_audio_push; offscreen runs are always silent)
//   --record-pad FILE     mac4: write the pad reports (per change, with their VBlank) to FILE (tools/native/pad_record.h)
//   --replay-pad FILE     mac4: take the pad reports from a recording instead of the window / --keys (the same run again)
//   --record-replay FILE  replay1: write the session as a replay file (src/native/replay_file.h) at its end: the pads of
//                         both ports, typed text, the card as it was at the boot, the game-side options and settings,
//                         RAM checkpoints. A window run does it by itself with PC SETTINGS > INTERFACE > RECORD REPLAYS
//                         On (<user data folder>/replays/<date-time>.b3replay, the newest 20 kept)
//   --replay FILE         replay1: play a replay file again (a bare name: one in <user data folder>/replays): the
//                         recorded pads, card (a throwaway copy: the player's own saves are not touched), clock, options
//                         and settings; refused for another disc or port release. The window's controls are ignored but
//                         Start (Esc), which pauses the replay; it ends where the recording did. The checkpoints say
//                         whether it stayed on the recorded timeline
//   --no-movie-skip       mac4: Start / Cross do not end movies (default: they do, tools/native/movie_skip.h; mac5:
//                         while one can be, a corner hint shows the button: PC SETTINGS > INTERFACE > VIDEO SKIP HINT)
//   --saves DIR           save1: the saves folder (B3NATIVE_SAVES_ROOT; src/native/save_store.h): profile-N.sav files,
//                         active-slot.txt. Default in a window (not --offscreen, not --replay-pad): the user's own,
//                         save_store.h default_saves_root() (beside pc_settings.ini), or the folder chosen on the
//                         launcher's Saves page (launcher.ini saves_dir); otherwise no card unless given.
//                         The card is there from the start (B3NATIVE_CARD_PRESENT_VBLANK=0 unless set), as a PS2's is,
//                         and the game's saves are written back (B3NATIVE_CARD_PERSIST=1 unless set).
//   --mouse LIST          mac5: scripted mouse events through the window's own path (tools/native/menu_mouse.h):
//                         <vblank>:move:<x>:<y> (fractions of the window), <vblank>:down|up|click:<1 left|3 right>
//                         (click: down, up 4 VBlanks later), <vblank>:wheel:<steps, > 0 up>, comma separated
//   --text LIST           mac5: scripted typing: <vblank>:<text>;... ("<BS>" Backspace, "<ENTER>" Enter), as typed in
//                         the window while the game's on-screen keyboard is up
//   --log FILE            test builds: the run log (tools/native/run_log.h): stdout and stderr also go to FILE, and the
//                         pad is recorded beside it (<name>.pads.txt) unless --record-pad names a file. Default in a
//                         window run (not --offscreen, not --replay-pad; B3NATIVE_RUN_LOG=0 or --no-run-log: none):
//                         <user data folder>/logs/run-<YYYYMMDD-HHMMSS>.log; the newest 10 runs are kept
//                         (include/b3report/run_logs.h). The launcher passes it.
//   --mods LIST           modkit1 (the mod kit's loader, include/b3mod/mods.h, docs/modding/DESIGN.md §1): these mods
//                         (ids, comma separated, the first wins; "none": no mods) instead of mods.ini's; env
//                         B3NATIVE_MODS. An id that is not installed or not usable ends the run (exit 2).
//                         Default: a window run takes the mods that are on in <user data folder>/mods.ini (the
//                         launcher's Mods page); an offscreen run (tests, the golden checks) none.
//   --mods-dir DIR        modkit1: the mods folder (env B3NATIVE_MODS_DIR; default <user data folder>/mods)
//                         The active set and its hash are printed at the start ("native_play: mods: ..."), so the
//                         run log and bug reports carry it; a pad recording gets a "mods" line. modkit1b: the set is
//                         passed to the renderer (b3mod::renderer_args), which reads the same mods' files for the
//                         disc files it loads itself; a window run in safe mode (--safe-mode, B3_SAFE_MODE=1, or two
//                         unclean starts in a row: b3settings/safemode.h) takes no mods, --mods included, and says so.
//                         sdk2b (the code-mod SDK, src/native/code_mods.h, docs/modding/DESIGN.md §2): an active mod with
//                         [code] has its library loaded before the game starts (after the port's own hooks, before the
//                         table is sealed); one that does not load (a replace / replace conflict, a refusal, in a
//                         window run a library the launcher was not allowed to run) is left out with its files, and
//                         the set printed again. A window run reads the mods' settings from mod-settings/<id>.ini and
//                         writes mods-state.ini (the launcher's Mods page); an offscreen run takes their defaults.
//                         lua6: an active mod with [script] runs its Lua script (src/native/script_mods.h) through the
//                         same loader, at the same point; with Developer mods on (above) a changed script loads again at a VBlank.
//   --mod-set SPEC        sdk2b: code mods' settings, "[<vblank>:]<mod>.<key>=<value>,..." (no VBlank: from the start;
//                         env B3NATIVE_MOD_SET), recorded like the PC SETTINGS page's changes; a replay ignores them
//   --developer-mods      sdk2b / tune4: Developer mods on for this run (env B3NATIVE_DEVELOPER_MODS=1), whatever
//                         PC SETTINGS > INTERFACE > DEVELOPER MODS says (pc_settings.ini developer_mods: the one switch,
//                         which a window run reads and the launcher's Mods page toggles too; an offscreen run: off).
//                         On: a code mod with [code] platform = yes may hook the platform tier, and the tuning console
//                         is on (unless --tuning off)
//   --tuning on|off       tune4 (the tuning console, tools/native/tuning_console.h, docs/modding/tuning.md): record the
//                         game's tuning registry and give the console (PC SETTINGS > DEVELOPER, F9) to the window
//                         (env B3NATIVE_TUNING=0|1). Default: Developer mods (above: --developer-mods, else in a
//                         window run PC SETTINGS > INTERFACE > DEVELOPER MODS; off by default). Off, nothing is
//                         wrapped. A mod's [tuning] preset, --tune or a recording with tuning edits installs the
//                         registry whatever this says
//   --tune LIST           tune4: scripted edits "<vblank>:<group> | <name>[<i>]=<value>;..." (or 0x<key>[<i>]=...; env
//                         B3NATIVE_TUNE), written at those VBlanks and recorded ("tune" lines / replay entries)
//   (no --game)           without it (and B3_GAME_DIR), a window run takes the launcher's game data folder
//                         (<user data folder>/launcher.ini game_dir); none: a message box says what is missing
//
// Per virtual VBlank (platform::add_vblank_hook: after the INTC handlers, right before the pad model's pad_vblank()):
// pace, then the pad report for this VBlank from the window (b3r_pad_sample) into sif::set_pad_sample(0, ...) (pad2:
// and port 2's from the window's second gamepad, plugged in while one is seated, unless --keys2 / --replay-pad), then
// the window's close request (b3r_quit_requested) ends the run. Closing the window, --quit-after and the VBlank limit
// all end it the same way: main's call unwinds, the game view is closed (the renderer drains its feed), exit 0.
#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "b3asset/discfs.h"
#include "b3asset/language.h"   // 1.0.1: language packs
#include "audio_sink.h"
#include "gameview_native.h"
#include "crash_report.h"
#include "explore_standins.h"
#include "fast_load.h"
#include "save_screens.h"
#include "car_select.h"   // carsel1: the car series on L1 / R1
#include "diff_check.h"
#include "poison_check.h"
#include "pc_menu.h"
#include "card_silence.h"
#include "date_format.h"
#include "speed_units.h"
#include "online_message.h"
#include "custom_soundtrack.h"
#include "now_playing_popup.h"
#include "tuning_console.h"   // tune4
#include "debug_health.h"
#include "time_trace.h"
#include "menu_mouse.h"
#include "stall_dump.h"
#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif
#include "movie_skip.h"
#include "native_fallback.h"
#if !defined(B3_HW_DEV)
#define B3_HW_DEV 0
#endif
#if B3_HW_DEV
#include "hw_model_dev.h"   // dev builds only (make HW_DEV=1): the DMA chain walker, the PATH3 check
namespace {
b3native::Path3Check g_path3_check;
}
#endif
#include "pad_record.h"
#include "page_hold.h"
#include "run_log.h"
#include "render_load.h"   // dll1114: the renderer's load, its diagnosis and message
#include "b3report/run_logs.h"
#include "b3report/version.h"
#include "native_table.h"
#include "platform.h"
#include "platform_sif.h"
#include "platform_vu0.h"
#include "input_keys.h"
#include "save_store.h"
#include "platform_disc.h"
#include "b3mod/mods.h"
#include "b3mod/codemod.h"   // sdk2b: the code mods' settings, trust, the last start's report
#include "code_mods.h"       // sdk2b: the code-mod loader and its API
#include "script_mods.h"     // lua6: script mods (Lua) on that loader
#include "replay_file.h"
#include "b3settings/safemode.h"   // modkit1b: safe mode starts without mods

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <pthread.h>   // (MinGW: winpthreads)
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kElfSize = 4073816;               // [C] retail SLUS_210.50 (as native_boot)
constexpr std::uint64_t kElfFnv = 0x790a7666929b62eaull;   // [C] FNV-1a 64 of the whole file
constexpr b3host::gaddr kStackTop = 0x01FFFD60u;           // [G] as native_boot

struct PlayEnd : std::runtime_error { using std::runtime_error::runtime_error; };   // window closed / --quit-after

std::string hex32(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

b3host::CallArgs args_of(std::initializer_list<std::uint32_t> a) {
    b3host::CallArgs c;
    unsigned i = 0;
    for (std::uint32_t v : a) c.gpr[i++] = b3host::detail::gpr64(b3host::detail::sext32(v));
    c.n_gpr = 8;
    c.n_fpr = 8;
    return c;
}

// The renderer's pad functions (b3render's C ABI, src/b3replay/inproc.cpp)
struct Render {
    int (*open)(const char*) = nullptr;   // b3r_open_main: the renderer runs on this (the main) thread
    int (*run_main)() = nullptr;
    int (*pad_sample)(int, unsigned char*, float) = nullptr;
    void (*pad_key)(int, int) = nullptr;
    int (*focused)() = nullptr;   // mac5: optional (b3r_focused)
    int (*quit_requested)() = nullptr;
    int (*close)(std::uint32_t) = nullptr;
    int (*perf_on)() = nullptr;   // perf3, optional: b3r_perf_on / b3r_perf_add (B3_PERF_LOG's game-thread stages)
    void (*perf_add)(int, std::uint64_t) = nullptr;
    void (*audio_push)(const std::int16_t*, std::size_t) = nullptr;   // optional (an older b3render: silent)
    void (*pad_context)(int) = nullptr;   // mac4, optional (an older b3render: the menu bindings only)
    int (*settings_open)() = nullptr;     // mac5, optional: open the PC SETTINGS page (1: opened)
    int (*date_format)() = nullptr;       // mac5, optional: b3r_date_format (an older b3render: the game's month/day/year)
    int (*speed_units)() = nullptr;       // units1, optional: b3r_speed_units (an older b3render: the game's miles)
    int (*music)(char*, int) = nullptr;   // optional: b3r_music, the playlist mode and music folder (custom_soundtrack.h)
    int (*other_music_apps)() = nullptr;  // nowplaying1, optional: b3r_other_music_apps (now_playing_popup.h; an older b3render: off)
    void (*now_playing)(const char*) = nullptr;   // nowplaying1, optional: b3r_now_playing, what is detected (the row's text)
    int (*settings_active)() = nullptr;   // mac5, optional: 1 while the page is open (the game then sees a neutral pad)
    int (*record_replays)() = nullptr;    // replay1, optional: PC SETTINGS > INTERFACE > RECORD REPLAYS
    int (*photo_row)() = nullptr;         // photorow1, optional: b3r_photo_row (an older b3render: no row)
    int (*photo_enter)() = nullptr;       // photorow1, optional: b3r_photo_enter, the pause menu's PHOTO MODE row (1: entered)
    void (*set_paused)(int) = nullptr;    // photo mode, optional: the race's pause menu is up (src/b3replay/photomode.h)
    // mac5, optional: the mouse in the game's menus and typed text (tools/native/menu_mouse.h)
    int (*menu_actions)(std::uint32_t*, int) = nullptr;
    void (*text_mode)(int) = nullptr;
    int (*text_take)(char*, int) = nullptr;
    void (*pad_mouse)(int, float, float, int) = nullptr;   // scripted (--mouse)
    void (*pad_text)(const char*) = nullptr;               // scripted (--text)
    // rumble1, optional (an older b3render: no rumble, the requests left pending): PC SETTINGS > RUMBLE (1 on), and the
    // game's motors per port (0 / 1: small 0 / 1, large 0-255) handed to the window's pads each VBlank while it is on
    int (*rumble_mode)() = nullptr;
    void (*rumble)(int, int, int) = nullptr;
    int (*hide_save_screens)() = nullptr; // autosave1, optional: b3r_hide_save_screens (an older b3render: shown)
    int (*fast_loading)() = nullptr;      // fastload1, optional: b3r_fast_loading (an older b3render: off)
    int (*series_shoulders)() = nullptr;  // carsel1, optional: b3r_series_shoulders (an older b3render: off)
    void (*fast_forward)(int) = nullptr;  // fastload1, optional: the display shows the newest frame while 1
    int (*scene_wait)(int) = nullptr;     // loadlag1, optional: a fast load's end waits for the race's scene (fast_load.h)
    // sdk2b, optional (an older b3render: no MODS tab, no textures): the code mods' settings rows for PC SETTINGS >
    // MODS (b3mod::rows_to_text), the page's changes ("<mod>.<key>=<value>", one per call, 0: none), a value changed
    // here (a script, a replay), a mod's texture, the mod set after the code mods loaded (ids, hash)
    void (*mod_rows)(const char*) = nullptr;
    int (*mod_take)(char*, int) = nullptr;
    void (*mod_value)(const char*, const char*) = nullptr;
    void (*mod_texture)(unsigned, unsigned, unsigned, const unsigned char*) = nullptr;
    void (*mods_set)(const char*, const char*) = nullptr;
    void (*set_tune_hooks)(const B3TuneHooks*) = nullptr;   // tune4, optional: the console's variables for the page / F9
    // nofallback1, optional (an older b3render: the window closes 3 s after the game ends, as for any end): the game
    // stopped on a function this build has no native code for -- the window stays with the message (and F10's bug
    // report) until it is closed
    void (*game_stopped)(const char*) = nullptr;
};

// play1: the platform's sound (800 frames per virtual VBlank) to the renderer's audio device; only bytes cross
struct RenderSink : b3audio::AudioSink {
    void (*fn)(const std::int16_t*, std::size_t) = nullptr;
    std::uint64_t frames = 0, audible = 0;   // audible: frames with a sample off zero
    int peak = 0;
    b3native::AudioGate gate;   // fastload1: a fast load's sound muted (fast_load.h)
    bool loading = false;             // the loading screen is up (set per VBlank): its sound counted apart
    std::uint64_t load_frames = 0, load_audible = 0;
    int load_peak = 0;
    int last_peak = 0;                // the peak of the frames since the last VBlank hook (fast_load.h: a quiet VBlank)
    void push(const std::int16_t* lr, std::size_t n) override {
        frames += n;
        if (loading) load_frames += n;
        for (std::size_t k = 0; k < 2 * n; k += 2) {
            const int a = std::abs(int(lr[k])), b = std::abs(int(lr[k + 1]));
            if (a || b) { ++audible; if (loading) ++load_audible; }
            peak = std::max(peak, std::max(a, b));
            last_peak = std::max(last_peak, std::max(a, b));
            if (loading) load_peak = std::max(load_peak, std::max(a, b));
        }
        if (fn)
            if (const std::int16_t* out = gate.apply(lr, n)) fn(out, n);
        if (wav) std::fwrite(lr, 4, n, wav);
    }
    // B3NATIVE_AUDIO_RAW=<file>: the mix as raw s16 stereo 48 kHz (a check of the music without a sound device:
    // ffmpeg -f s16le -ar 48000 -ac 2 -i <file> out.wav)
    std::FILE* wav = [] { const char* e = std::getenv("B3NATIVE_AUDIO_RAW"); return e && *e ? std::fopen(e, "wb") : nullptr; }();
    ~RenderSink() { if (wav) std::fclose(wav); }
};

bool g_hide_save_screens = false;   // autosave1: SAVE SCREENS is Hide (latched per VBlank; the poll's hold probe reads it)
bool hold_probe() {   // at each poll: the AUTOSAVE page is up and hidden (the display holds the page before it)
    return g_hide_save_screens && b3native::autosave_page_up(b3host::load<std::uint32_t>(b3native::kMenuCurrentPage));
}
std::atomic<std::uint64_t> g_accel_calls{0};   // mac4: GetAccelerator calls (the key map's driving context)
b3native::MovieSkip* g_movie_skip = nullptr;   // mac4: the watch callback's target (the game thread's)

struct KeyEvent { std::uint64_t vblank; int scancode; int down; };
std::vector<KeyEvent> parse_keys(const std::string& s) {
    std::vector<KeyEvent> out;
    std::size_t a = 0;
    while (a < s.size()) {
        std::size_t b = s.find(',', a);
        if (b == std::string::npos) b = s.size();
        unsigned long long v = 0;
        int sc = 0, d = 0;
        if (std::sscanf(s.substr(a, b - a).c_str(), "%llu:%d:%d", &v, &sc, &d) == 3) out.push_back({v, sc, d});
        a = b + 1;
    }
    return out;
}

// mac5: --mouse / --text scripts
struct MouseEvent { std::uint64_t vblank; int kind; float x, y; int n; };   // kind as b3r_pad_mouse: 0 move, 1 down, 2 up, 3 wheel
std::vector<MouseEvent> parse_mouse(const std::string& s) {
    std::vector<MouseEvent> out;
    std::size_t a = 0;
    while (a < s.size()) {
        std::size_t b = s.find(',', a);
        if (b == std::string::npos) b = s.size();
        const std::string item = s.substr(a, b - a);
        unsigned long long v = 0;
        char what[16] = {};
        float x = 0, y = 0;
        int n = 0;
        if (std::sscanf(item.c_str(), "%llu:move:%f:%f", &v, &x, &y) == 3) out.push_back({v, 0, x, y, 0});
        else if (std::sscanf(item.c_str(), "%llu:%15[a-z]:%d", &v, what, &n) == 3) {
            const std::string w = what;
            if (w == "down") out.push_back({v, 1, 0, 0, n});
            else if (w == "up") out.push_back({v, 2, 0, 0, n});
            else if (w == "click") { out.push_back({v, 1, 0, 0, n}); out.push_back({v + 4, 2, 0, 0, n}); }
            else if (w == "wheel") out.push_back({v, 3, 0, 0, n});
        }
        a = b + 1;
    }
    std::stable_sort(out.begin(), out.end(), [](const MouseEvent& p, const MouseEvent& q) { return p.vblank < q.vblank; });
    return out;
}
struct TextEvent { std::uint64_t vblank; std::string text; };
std::vector<TextEvent> parse_text(const std::string& s) {
    std::vector<TextEvent> out;
    std::size_t a = 0;
    while (a < s.size()) {
        std::size_t b = s.find(';', a);
        if (b == std::string::npos) b = s.size();
        const std::string item = s.substr(a, b - a);
        const std::size_t c = item.find(':');
        if (c != std::string::npos) {
            std::string t = item.substr(c + 1);
            for (const auto& [tok, ch] : {std::pair<const char*, char>{"<BS>", '\b'}, {"<ENTER>", '\n'}})
                for (std::size_t k; (k = t.find(tok)) != std::string::npos;) t.replace(k, std::strlen(tok), 1, ch);
            out.push_back({std::strtoull(item.c_str(), nullptr, 10), t});
        }
        a = b + 1;
    }
    return out;
}

// replay1 (src/native/replay_file.h): the environment set / cleared as a replay recorded it
void env_set(const char* name, const std::string& v) {
#ifdef _WIN32
    _putenv_s(name, v.c_str());
#else
    setenv(name, v.c_str(), 1);
#endif
}
void env_unset(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
// A hash of guest RAM below MB 31 (that one holds the stack, which differs harmlessly between hosts), FNV-1a per word
std::uint64_t replay_ram_hash() {
    static std::vector<std::uint8_t> buf(0x100000u);
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (b3host::gaddr mb = 0; mb < 0x01F00000u; mb += 0x100000u) {
        b3host::copy_from_guest(buf.data(), mb, 0x100000u);
        for (std::size_t k = 0; k < buf.size(); k += 8) {
            std::uint64_t w;
            std::memcpy(&w, buf.data() + k, 8);
            h = (h ^ w) * 0x100000001B3ull;
        }
    }
    return h;
}
// The custom music folder's files as the game would play them: "<count>,<fingerprint>" (replay_file.h fingerprint)
std::string music_fingerprint(const std::string& folder) {
    std::vector<std::pair<std::string, std::uintmax_t>> files;
    for (const std::string& f : b3music::list_audio_files(folder)) {
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(std::filesystem::path(std::u8string(f.begin(), f.end())), ec);
        files.emplace_back(f.size() > folder.size() ? f.substr(folder.size()) : f, ec ? 0 : size);
    }
    char b[48];
    std::snprintf(b, sizeof b, "%zu,%016llx", files.size(), static_cast<unsigned long long>(b3native::replay::fingerprint(files)));
    return b;
}
// The live settings the game reads while it runs (date format, speed units, music), latched once per VBlank while a replay is
// recorded or played, so the game reads the same values on the same VBlanks
struct LiveSettings {
    bool on = false;
    int date = 0, music_mode = 0;
    bool photo_row = false;   // photorow1: the pause menu's PHOTO MODE row (a recording without the entry: none, as before)
    int units = 0;   // units1: SPEED UNITS (tools/native/speed_units.h)
    std::string music_dir;
    std::string now_playing;   // nowplaying1: a replay's latched track (b3nowplaying::encode; empty: none recorded)
};
LiveSettings g_live;
int (*g_photo_row)() = nullptr;   // photorow1: b3r_photo_row
// B3_PHOTO_ROW=0|1 overrides PC SETTINGS > INTERFACE > PHOTO MODE IN PAUSE MENU (the golden runs set 0: the game's own menu);
// no renderer: no row
bool photo_row_wanted() {
    if (const char* e = std::getenv("B3_PHOTO_ROW"); e && (e[0] == '0' || e[0] == '1') && !e[1]) return e[0] == '1';
    return g_photo_row && g_photo_row() != 0;
}
std::atomic<bool> g_replay_paused{false};   // the viewer paused a replay (the stall watchdog then waits)

// What the pad model was given (the check that keys reach set_pad_sample)
struct PadLog {
    std::uint64_t set = 0, non_neutral = 0, changes = 0;
    std::uint64_t first_non_neutral = 0;
    std::array<unsigned char, 32> last{}, first_bytes{};
};

}  // namespace

// lane-noise (2026-11-11): B3NATIVE_GDB_WATCH's hand-over to gdb (tools/native/ram_watch.sh breaks here and sets a
// hardware watchpoint on `host`): one call per watched guest word, at the chosen VBlank
extern "C" __attribute__((noinline, used)) void b3np_gdb_watch(void* host, unsigned guest) { asm volatile("" : : "r"(host), "r"(guest) : "memory"); }

// save1: the SETTINGS page's SAVES tab (src/b3replay/pcpage.h SaveHooks), on the card's profile (save_store.h)
static int save_info(char* folder, int n) {
    b3native::save::ProfileCard* c = b3native::platform::sif::save_profile_card();
    if (!c || n <= 0) return 0;
    std::snprintf(folder, static_cast<std::size_t>(n), "%s", c->root().string().c_str());
    return c->slot();
}
static void save_request(int kind, const char* path) {
    if (b3native::save::ProfileCard* c = b3native::platform::sif::save_profile_card(); c && path)
        c->post(kind == 1 ? b3native::save::ProfileCard::Request::kExport : b3native::save::ProfileCard::Request::kImport, path);
}
static int save_status(char* text, int n) {
    b3native::save::ProfileCard* c = b3native::platform::sif::save_profile_card();
    if (!c || n <= 0) return 0;
    const auto st = c->status();
    std::snprintf(text, static_cast<std::size_t>(n), "%s", st.text.c_str());
    return static_cast<int>(st.serial);
}

// test builds: a problem the player must see (a window run): stderr (the run log) and a message box -- Windows'
// own; elsewhere SDL's, from the renderer library's SDL (when it loads)
static void tell_user(const std::string& title, const std::string& text, const std::string& exe_dir) {
    std::fprintf(stderr, "native_play: %s: %s\n", title.c_str(), text.c_str());
    std::fflush(stderr);
#ifdef _WIN32
    (void)exe_dir;
    auto wide = [](const std::string& s) {
        std::wstring w(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0)), L'\0');
        if (!w.empty()) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), static_cast<int>(w.size()));
        while (!w.empty() && w.back() == L'\0') w.pop_back();
        return w;
    };
    MessageBoxW(nullptr, wide(text).c_str(), wide("Burnout 3 - " + title).c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#else
    using Box = bool (*)(std::uint32_t, const char*, const char*, void*);
#ifdef __APPLE__
    const std::string libs[] = {exe_dir + "/libb3render.so", "libSDL3.0.dylib", "libSDL3.dylib"};
#else
    const std::string libs[] = {exe_dir + "/libb3render.so", "libSDL3.so.0"};
#endif
    for (const std::string& l : libs)
        if (void* h = dlopen(l.c_str(), RTLD_NOW | RTLD_LOCAL))
            if (auto box = reinterpret_cast<Box>(dlsym(h, "SDL_ShowSimpleMessageBox"))) {
                box(0x10u /* SDL_MESSAGEBOX_ERROR */, ("Burnout 3 - " + title).c_str(), text.c_str(), nullptr);
                return;
            }
#endif
}

// The launcher's game data folder (<user data folder>/launcher.ini, src/b3launcher/core.h Config: "game_dir = ...")
static std::string launcher_game_dir() {
    const std::filesystem::path d = b3native::save::user_data_dir();
    if (d.empty()) return {};
    std::ifstream f(d / "launcher.ini", std::ios::binary);
    std::string line;
    auto trim = [](std::string v) {
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) v.pop_back();
        std::size_t a = 0;
        while (a < v.size() && (v[a] == ' ' || v[a] == '\t')) ++a;
        return v.substr(a);
    };
    while (std::getline(f, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        if (trim(line.substr(0, eq)) == "game_dir") return trim(line.substr(eq + 1));
    }
    return {};
}

int main(int argc, char** argv) {
    // vu0 (2026-11-07): native_play runs VU0 micro mode through the generated programs by default
    // (src/native/vu0_aot_rt.h; per launch equal to the interpreter on 3,661,844 launches of navproof14 + races14);
    // B3_VU0_AOT=0 (or check) set by the caller wins -- in a dev build only
    // (vu0n: make VU0_INTERP=1); the default build has no VU0 interpreter, so 1 is its only mode
#ifdef _WIN32
    timeBeginPeriod(1);   // mac5: 1 ms timer resolution for the pacing's sleeps (winmm)
    // the exe and b3render.dll each have their own (static) C runtime: an end the exe's runtime does not see would lose
    // its buffered lines, so they go out at once
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!std::getenv("B3_VU0_AOT")) _putenv_s("B3_VU0_AOT", "1");   // mingw's C runtime has no setenv
#else
    setenv("B3_VU0_AOT", "1", 0);
#endif
    std::string elf, game, render_lib, render_args, keys, keys2, record_pad, replay_pad, saves, mouse_script, text_script;
    std::string record_replay, replay_file;   // replay1: --record-replay / --replay
    std::string run_log_path;   // --log
    std::string mods_arg, mods_dir;   // modkit1: --mods, --mods-dir
    bool mods_given = false;
    std::string mod_set_arg;          // sdk2b: --mod-set
    bool developer_mods = false;      // sdk2b / tune4: --developer-mods (then the setting: below)
    bool run_log_off = false;   // --no-run-log
    bool safe_mode = false;   // mac5: --safe-mode
    std::uint64_t pace_from = 0;   // --pace-from N (tests): unpaced until VBlank N, then real time
    int save_screens_arg = -1;     // autosave1: --save-screens hide (1) / show (0); -1: B3NATIVE_SAVE_SCREENS, else the setting
    int fast_loading_arg = -1;     // fastload1: --fast-loading on (1) / off (0); -1: B3NATIVE_FAST_LOADING, else the setting
    int tuning_arg = -1;           // tune4: --tuning on (1) / off (0); -1: B3NATIVE_TUNING, else the setting (window runs)
    std::string tune_script;       // tune4: --tune
    bool offscreen = false, pace = true, strict = false, audio = true, explore_movies = false, idle_skip = true;
    bool movie_skip_on = true;
    std::string explore_retail = b3native::RetailFor::requested();
    std::string explore_return = b3native::ReturnStandIn::requested();
    double hz = 59.94, quit_after = 0;
    for (int k = 1; k < argc; ++k) {
        const std::string s = argv[k];
        auto next = [&]() -> std::string { return k + 1 < argc ? argv[++k] : std::string(); };
        if (s == "--elf") elf = next();
        else if (s == "--game") game = next();
        else if (s == "--offscreen") offscreen = true;
        else if (s == "--render-lib") render_lib = next();
        else if (s == "--render-args") render_args = next();
        else if (s == "--hz") hz = std::atof(next().c_str());
        else if (s == "--no-pace") pace = false;
        else if (s == "--pace-from") pace_from = std::strtoull(next().c_str(), nullptr, 10);   // perf (tests)
        else if (s == "--fast-loading") {   // fastload1
            const std::string v = next();
            if (v == "on" || v == "1") fast_loading_arg = 1;
            else if (v == "off" || v == "0") fast_loading_arg = 0;
            else { std::fprintf(stderr, "native_play: --fast-loading on|off, not '%s'\n", v.c_str()); return 2; }
        }
        else if (s == "--save-screens") {   // autosave1
            const std::string v = next();
            if (v == "hide") save_screens_arg = 1;
            else if (v == "show") save_screens_arg = 0;
            else { std::fprintf(stderr, "native_play: --save-screens hide|show, not '%s'\n", v.c_str()); return 2; }
        }
        else if (s == "--tuning") {   // tune4
            const std::string v = next();
            if (v == "on" || v == "1") tuning_arg = 1;
            else if (v == "off" || v == "0") tuning_arg = 0;
            else { std::fprintf(stderr, "native_play: --tuning on|off, not '%s'\n", v.c_str()); return 2; }
        }
        else if (s == "--tune") tune_script = next();   // tune4
        else if (s == "--quit-after") quit_after = std::atof(next().c_str());
        else if (s == "--keys") keys = next();
        else if (s == "--keys2") keys2 = next();   // fe1
        else if (s == "--mouse") mouse_script = next();   // mac5
        else if (s == "--text") text_script = next();     // mac5
        else if (s == "--strict") strict = true;
        else if (s == "--safe-mode") safe_mode = true;   // mac5: low settings for this run (b3settings/safemode.h)
        else if (s == "--no-audio") audio = false;
        else if (s == "--no-idle-skip") idle_skip = false;   // rt1
        else if (s == "--record-pad") record_pad = next();      // mac4
        else if (s == "--replay-pad") replay_pad = next();      // mac4
        else if (s == "--no-movie-skip") movie_skip_on = false;   // mac4
        else if (s == "--record-replay") record_replay = next();   // replay1
        else if (s == "--replay") replay_file = next();             // replay1
        else if (s == "--saves") saves = next();                  // save1
        else if (s == "--log") run_log_path = next();             // test builds: the run log (run_log.h)
        else if (s == "--no-run-log") run_log_off = true;
        else if (s == "--mods") { mods_arg = next(); mods_given = true; }   // modkit1
        else if (s == "--mods-dir") mods_dir = next();
        else if (s == "--mod-set") mod_set_arg = next();   // sdk2b
        else if (s == "--developer-mods") developer_mods = true;
        else if (s == "--explore-movies") explore_movies = true;   // EXPLORATION: every movie ends at once
        else if (s == "--explore-return") explore_return = next();   // EXPLORATION: answered at once
        else if (s == "--explore-retail") explore_retail = next();   // EXPLORATION: retail for these
        else { std::fprintf(stderr, "native_play: unknown option %s (see tools/native/native_play.cpp)\n", s.c_str()); return 2; }
    }
    if (save_screens_arg < 0) save_screens_arg = b3native::save_screens_env();
    if (const char* e = std::getenv("B3NATIVE_FAST_LOADING"); fast_loading_arg < 0 && e && *e) fast_loading_arg = *e != '0' ? 1 : 0;
    if (const char* e = std::getenv("B3NATIVE_TUNING"); tuning_arg < 0 && e && *e) tuning_arg = *e != '0' ? 1 : 0;   // tune4
    if (const char* e = std::getenv("B3NATIVE_TUNE"); tune_script.empty() && e && *e) tune_script = e;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    b3native::install_crash_report();   // a host fault: the native chain + backtrace (tools/native/crash_report.h)
    const std::string exe_dir = [&] {
        const std::string self = argv[0];
        const std::size_t cut = self.find_last_of("/\\");
        return cut == std::string::npos ? std::string(".") : self.substr(0, cut);
    }();
    // test builds: the run log (tools/native/run_log.h) -- every window run's output and pad in the user data folder
    {
        const char* env_off = std::getenv("B3NATIVE_RUN_LOG");
        const bool auto_log = !offscreen && replay_pad.empty() && replay_file.empty() && !run_log_off && !(env_off && std::string(env_off) == "0");
        std::filesystem::path log;
        std::error_code ec;
        if (!run_log_path.empty()) log = std::filesystem::path(run_log_path);
        else if (auto_log) {
            const std::filesystem::path pref = b3native::save::user_data_dir();
            if (!pref.empty()) log = b3port::runlog::new_run(b3port::runlog::logs_dir(pref), std::time(nullptr)).log;
        }
        if (!log.empty()) {
            if (log.has_parent_path()) std::filesystem::create_directories(log.parent_path(), ec);
            std::string why;
            if (b3native::runlog::start(log, &why)) {
                b3native::g_crash_flush = &b3native::runlog::crash_flush;
                if (record_pad.empty() && replay_pad.empty() && replay_file.empty()) record_pad = b3port::runlog::paths_for_log(log).pads.string();
                b3port::runlog::rotate(log.parent_path());   // the newest kKeepRuns runs stay
            } else {
                std::fprintf(stderr, "native_play: no run log: %s\n", why.c_str());
            }
        }
    }
    {   // the top of every log: the build, the time, the command line
        std::printf("%s\n", b3port::version_line().c_str());
        std::printf("native_play: started %s (local time);", b3port::runlog::stamp(std::time(nullptr)).c_str());
        for (int k = 0; k < argc; ++k) std::printf(" %s", argv[k]);
        std::printf("\n");
        if (b3native::runlog::state().active) std::printf("native_play: run log %s\n", b3native::runlog::state().path.c_str());
        std::fflush(stdout);
    }
    // replay1: a replay to play (src/native/replay_file.h): read, checked against this disc and port, then its game-side
    // environment, options and card (a throwaway copy) put in place of this run's
    std::optional<b3native::replay::Replay> replay_in;
    std::filesystem::path replay_card;   // the throwaway card's folder (removed at the end)
    if (!replay_file.empty()) {
        auto refuse = [&](const std::string& text) {
            if (!offscreen) tell_user("cannot play the replay", text, exe_dir);
            else std::fprintf(stderr, "native_play: replay: %s\n", text.c_str());
            return 2;
        };
        if (!replay_pad.empty() || !record_replay.empty()) return refuse("--replay plays a replay alone (not with --replay-pad or --record-replay).");
        std::filesystem::path rp(std::u8string(replay_file.begin(), replay_file.end()));
        if (std::error_code ec; !std::filesystem::exists(rp, ec) && !rp.has_parent_path())   // a bare name: the replays folder's
            rp = b3native::replay::replays_dir(b3native::save::user_data_dir()) / rp;
        const std::string name = rp.filename().string();
        std::string why;
        replay_in = b3native::replay::read_file(rp, &why);
        if (!replay_in) return refuse("The replay " + name + " " + why + ".");
        const b3native::replay::Compat c = b3native::replay::compatible(*replay_in, {kElfSize, kElfFnv, b3port::version()});
        if (c.verdict == b3native::replay::Compat::kRefuse) return refuse("The replay " + name + " cannot be played here. " + c.text);
        if (c.verdict == b3native::replay::Compat::kWarn) std::printf("native_play: replay: %s\n", c.text.c_str());
        for (const char* n : b3native::replay::kGameEnv) {
            if (const std::string* v = replay_in->option(std::string("env.") + n)) env_set(n, *v);
            else env_unset(n);
        }
        for (const auto& [k, v] : replay_in->options)
            if (k.rfind("music_fp.", 0) == 0 && music_fingerprint(k.substr(9)) != v)
                return refuse("The replay " + name + " was recorded with your own music from the folder " + k.substr(9) +
                              ". The game's radio follows those songs, so it plays the same only with the same music files there, and they are not.");
        for (const auto& [k, v] : replay_in->options)   // eatrax1: the recording's modes of the custom songs, not the file's
            if (k.rfind("music_modes.", 0) == 0) b3native::CustomSoundtrack::seed_modes(k.substr(12), v);
        if (const std::string* o = replay_in->option("idle_skip")) idle_skip = *o == "1";
        if (const std::string* o = replay_in->option("movie_skip")) movie_skip_on = *o == "1";
        if (const std::string* o = replay_in->option("explore_movies")) explore_movies = *o == "1";
        env_unset("B3NATIVE_SAVES_ROOT");
        saves.clear();
        if (replay_in->card) {   // the card as it was at the recording's boot, in a folder of its own
            std::error_code ec;
            replay_card = std::filesystem::temp_directory_path(ec) /
                          ("b3port-replay-card-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(replay_card, ec);
            std::ofstream f(b3native::save::profile_file(replay_card, replay_in->card_slot), std::ios::binary);
            f.write(reinterpret_cast<const char*>(replay_in->card_sav.data()), std::streamsize(replay_in->card_sav.size()));
            f.close();
            if (ec || !f || !b3native::save::write_active_slot(replay_card, replay_in->card_slot, &why))
                return refuse("No room for the replay's memory card in " + replay_card.string() + ".");
            saves = replay_card.string();
            env_set("B3NATIVE_CARD_PERSIST", "1");
        }
        std::printf("native_play: replaying %s: recorded %s by %s, %s long (to VBlank %llu), %zu pad ops, %zu checkpoints, %s; "
                    "the window's controls are ignored (Start pauses it)\n",
                    replay_file.c_str(), replay_in->info_value("recorded") ? replay_in->info_value("recorded")->c_str() : "?",
                    replay_in->version.c_str(), b3native::replay::format_length(replay_in->vblanks).c_str(),
                    static_cast<unsigned long long>(replay_in->vblanks), replay_in->pads.size(), replay_in->checks.size(),
                    replay_in->card ? ("its card (profile " + std::to_string(replay_in->card_slot) + ") in a throwaway copy").c_str() : "no memory card");
        std::fflush(stdout);
    }
    if (game.empty()) if (const char* g = std::getenv("B3_GAME_DIR"); g && *g) game = g;
    if (game.empty() && !offscreen) {   // started directly (not by the launcher): the launcher's choice
        game = launcher_game_dir();
        if (!game.empty()) std::printf("native_play: game data folder from launcher.ini: %s\n", game.c_str());
    }
    if (elf.empty()) if (const char* e = std::getenv("B3_ELF"); e && *e) elf = e;
    if (elf.empty() && !game.empty()) elf = game + "/SLUS_210.50";
    if (elf.empty() || game.empty()) {
        if (!offscreen)
            tell_user("no game data",
                      "The game needs the files of your Burnout 3 disc (NTSC-U, SLUS-21050) and does not know where they are.\n\n"
                      "Start Burnout3Launcher instead: its Game Data page finds or asks for them and remembers the choice. "
                      "Or start this program with --game <folder with the disc's files, or the disc image (.iso, .bin or .chd)>.",
                      exe_dir);
        else
            std::fprintf(stderr, "native_play: --game <disc folder or image (.iso / .bin / .chd)> (or B3_GAME_DIR) needed\n");
        return 2;
    }
    if (!(hz > 1 && hz < 1000)) hz = 59.94;
    // save1: the card is the user's profile file (src/native/save_store.h); B3NATIVE_SAVES_ROOT set by the caller wins
    {
        auto set_env = [](const char* name, const std::string& v) {
#ifdef _WIN32
            _putenv_s(name, v.c_str());
#else
            setenv(name, v.c_str(), 1);
#endif
        };
        const char* given = std::getenv("B3NATIVE_SAVES_ROOT");
        if (!saves.empty()) set_env("B3NATIVE_SAVES_ROOT", saves);
        else if (!(given && *given) && !offscreen && replay_pad.empty() && replay_file.empty() &&
                 !b3native::save::chosen_saves_root(b3native::save::user_data_dir()).empty()) {
            // 1.1: the folder chosen on the launcher's Saves page (launcher.ini saves_dir). Not made when it is not there
            // (a cloud drive or a network share not connected): this run has no memory card, and the player is told
            const std::filesystem::path d = b3native::save::chosen_saves_root(b3native::save::user_data_dir());
            std::error_code ec;
            std::string root;
            if (std::filesystem::is_directory(d, ec)) {
                try {
                    root = d.string();   // (the environment's text: the system's code page on Windows)
                } catch (const std::exception&) {
                    root.clear();
                }
            }
            const std::u8string u8 = d.u8string();
            const std::string shown(u8.begin(), u8.end());
            if (!root.empty()) set_env("B3NATIVE_SAVES_ROOT", root);
            else
                tell_user("save folder not found",
                          "The save folder chosen in the launcher is not available:\n" + shown +
                              "\n\nThe game starts without a memory card: nothing is saved this time. Connect the drive or "
                              "network folder and start again, or choose another save folder on the launcher's Saves page.",
                          exe_dir);
        } else if (!(given && *given) && !offscreen && replay_pad.empty() && replay_file.empty()) {
            const std::filesystem::path d = b3native::save::default_saves_root();
            std::error_code ec;
            if (!d.empty()) std::filesystem::create_directories(d, ec);
            if (!d.empty() && !ec) set_env("B3NATIVE_SAVES_ROOT", d.string());
            else std::fprintf(stderr, "native_play: no saves folder (%s): no memory card this run\n", d.empty() ? "no user data folder" : ec.message().c_str());
        }
        const char* root = std::getenv("B3NATIVE_SAVES_ROOT");
        if (root && *root) {
            if (!std::getenv("B3NATIVE_CARD_PRESENT_VBLANK")) set_env("B3NATIVE_CARD_PRESENT_VBLANK", "0");
            if (!std::getenv("B3NATIVE_CARD_PERSIST")) set_env("B3NATIVE_CARD_PERSIST", "1");   // the game's saves to the .sav
            std::printf("native_play: saves in %s (profile %d)\n", root, b3native::save::read_active_slot(root));
        } else {
            std::printf("native_play: no saves folder (--saves / B3NATIVE_SAVES_ROOT): the game has no memory card\n");
        }
    }

    // modkit1 (mod kit part 1, docs/modding/DESIGN.md §1): the active mods' data overlay in the platform's disc layer.
    // No mods: no overlay (platform_disc.cpp reads the disc exactly as before).
    b3mod::Overlay mod_overlay;
    std::string mod_ids;   // "a,b" (the pad recording's mods line)
    std::string mod_render_args;   // modkit1b: the set for the renderer (b3mod::renderer_args)
    int mods_safe_off = 0;         // modkit1b: mods safe mode left off
    // sdk2b: what the code mods' loader needs from here (the installed mods, the selection in order, the folders)
    std::vector<b3mod::Mod> mods_found;
    std::vector<const b3mod::Mod*> mods_selected;
    b3mod::PatchEnv mods_penv;   // patches3: [files] patches, for the first build and the rebuild after code mods load
    std::filesystem::path mods_pref, mods_root;
    std::string mods_from;
    std::map<std::string, std::string> code_refused, code_digest;   // id -> why it does not load (trust) / the library's SHA-256
    {
        if (!mods_given)
            if (const char* e = std::getenv("B3NATIVE_MODS"); e && *e) { mods_arg = e; mods_given = true; }
        if (mods_dir.empty())
            if (const char* e = std::getenv("B3NATIVE_MODS_DIR"); e && *e) mods_dir = e;
        const std::filesystem::path pref = b3native::save::user_data_dir();
        const std::filesystem::path root = !mods_dir.empty() ? std::filesystem::path(mods_dir)
                                           : pref.empty()     ? std::filesystem::path()
                                                              : b3mod::mods_folder(pref);
        std::vector<std::string> ids;
        std::string from;
        if (replay_in && !mods_given) {   // replay1 + modkit1: a replay plays with the mods it was recorded with
            const std::string* rec = replay_in->option("mods.ids");
            mods_arg = rec ? *rec : "none";
            mods_given = true;
        }
        if (mods_given) {
            ids = b3mod::split_ids(mods_arg);
            from = replay_in ? "the replay" : "--mods";
        } else if (!offscreen && !pref.empty()) {
            b3mod::Order order;
            if (b3mod::load_order(b3mod::order_file(pref), order)) {
                ids = b3mod::enabled_ids(order);
                from = "mods.ini";
            }
        }
        // modkit1b: safe mode starts without mods. The renderer decides safe mode for a window run (b3replay main.cpp:
        // run_state.ini beside pc_settings.ini, then writes it); this reads the same file first, without writing it, so
        // both make the same decision.
        if (!ids.empty() && !offscreen && !pref.empty() && !std::getenv("B3_NO_USER_SETTINGS") && render_args.find("--settings") == std::string::npos) {
            const char* env = std::getenv("B3_SAFE_MODE");
            const bool force = safe_mode || (env && std::strcmp(env, "1") == 0);
            const b3settings::StartDecision d = b3settings::start_run(b3settings::load_run_state(b3mod::path_utf8(pref / "run_state.ini")), force);
            if (d.safe) {
                std::printf("native_play: mods: SAFE MODE (%s): this run starts without mods (%zu on in %s)\n",
                            d.forced ? "asked for" : "the last runs did not end cleanly", ids.size(), from.c_str());
                mods_safe_off = int(ids.size());
                ids.clear();
            }
        }
        mods_pref = pref;
        mods_root = root;
        mods_from = from;
        // sdk2b / tune4: Developer mods, one switch: --developer-mods / B3NATIVE_DEVELOPER_MODS=1, else in a window run
        // PC SETTINGS > INTERFACE > DEVELOPER MODS (pc_settings.ini developer_mods, which the launcher's Mods page
        // toggles too; read here as the renderer will read it: tools/native/tuning_console.h developer_mods_on)
        if (const char* e = std::getenv("B3NATIVE_DEVELOPER_MODS"); e && *e == '1') developer_mods = true;
        if (!developer_mods && !offscreen) developer_mods = b3native::developer_mods_on(render_args, pref);
        if (!ids.empty()) {
            mods_found = b3mod::discover(root);
            const std::vector<b3mod::Mod>& found = mods_found;
            b3mod::Selection sel = b3mod::select(found, ids);
            mods_selected = sel.mods;
            // patches3: [files] patches are applied to this disc's files (B3_DISC_ROOT, else --game, as the platform reads
            // it), the results kept in <mods folder>/.cache; a mod whose patch does not fit this disc is left out
            const char* droot = std::getenv("B3_DISC_ROOT");
            const std::filesystem::path disc_root = droot && *droot ? droot : !game.empty() ? game : "data/game";
            mods_penv.cache = b3mod::patch_cache(root);
            mods_penv.read_disc = [disc_root](const std::string& key, std::vector<std::uint8_t>& out) {
                const std::filesystem::path p = b3asset::disc::resolve_ci(disc_root, key);
                return !p.empty() && b3asset::disc::read_file(p, out);
            };
            mod_overlay = b3mod::Overlay::build(sel.mods, true, &mods_penv);
            sel.errors.insert(sel.errors.end(), mod_overlay.errors().begin(), mod_overlay.errors().end());
            for (const std::string& e : sel.errors) std::fprintf(stderr, "native_play: mods: %s (folder %s)\n", e.c_str(), b3mod::path_utf8(root).c_str());
            if (!sel.errors.empty() && replay_in) {   // replay1: the recording's mods are not all here
                const std::string text = "This replay was recorded with the mods " + mods_arg + ". It plays the same only with "
                                         "the same mods installed, and " + sel.errors.front() + ".";
                if (!offscreen) tell_user("cannot play the replay", text, exe_dir);
                else std::fprintf(stderr, "native_play: replay: %s\n", text.c_str());
                return 2;
            }
            if (!sel.errors.empty() && mods_given) return 2;   // asked for by name (a script): no silent half set
            for (const std::string& n : sel.notes) std::printf("native_play: mods: %s\n", n.c_str());   // modkit1b: [requires] order
            for (const b3mod::Mod* m : sel.mods)
                for (const std::string& w : m->warnings) std::printf("native_play: mods: %s: %s\n", m->manifest.id.c_str(), w.c_str());
            for (const b3mod::ActiveMod& m : mod_overlay.mods()) {
                std::printf("native_play: mods: %s %s (\"%s\") from %s: %zu files%s%s, mod hash %s\n", m.id.c_str(), m.version.c_str(),
                            m.name.c_str(), b3mod::path_utf8(m.dir).c_str(), m.files.size(),
                            m.patched ? (" (" + std::to_string(m.patched) + " from patches)").c_str() : "",
                            m.textures.empty() ? "" : " and a texture pack", b3mod::hex64(m.hash).c_str());
                for (const std::string& n : m.notes) std::printf("native_play: mods: %s: %s\n", m.id.c_str(), n.c_str());
                mod_ids += (mod_ids.empty() ? "" : ",") + m.id;
                // sdk2b (DESIGN.md §2.5): a code mod is a program; a window run loads only a library the user allowed
                // in the launcher (once per library: its SHA-256 in mods-trusted.ini). Offscreen runs (tests, scripts
                // naming the mods) are not asked.
                if (m.code.present) {
                    std::printf("native_play: mods: %s: contains code (%s%s)\n", m.id.c_str(),
                                m.library.empty() ? "no library for this system" : b3mod::path_utf8(m.library).c_str(),
                                m.code.gameplay ? "; changes gameplay" : "");
                    if (!m.library.empty()) code_digest[m.id] = b3mod::library_digest(m.library);
                    if (!offscreen && !m.library.empty() && !b3mod::trusted(pref, code_digest[m.id]))
                        code_refused[m.id] = "its library is not allowed to run yet: turn the mod on again on the launcher's Mods page and allow it";
                }
                if (m.script.present)   // lua6: a script (sandboxed: no trust question)
                    std::printf("native_play: mods: %s: a script (%s; %s)\n", m.id.c_str(), m.script.main_file.c_str(),
                                m.script.gameplay ? "may change gameplay" : "gameplay = no: reads and draws only");
            }
        }
        if (mod_overlay.empty()) std::printf("native_play: mods: none\n");
        else std::printf("native_play: mods: active (%s; the first wins): %s\n", from.c_str(), b3mod::describe(mod_overlay).c_str());
        b3native::platform::disc::set_overlay(&mod_overlay);
        {   // 1.0.1 (b3asset/language.h): PC SETTINGS > LANGUAGE, the game's text from the player's European disc
            b3settings::Settings ls;
            const std::string sp = b3native::settings_path(render_args, pref);
            const std::string lroot = pref.empty() ? std::string() : b3mod::path_utf8(b3asset::language::imported_root(pref));
            // 1.1: the renderer's PC SETTINGS page lists the same folder's languages (and shows itself in the chosen one)
            if (!lroot.empty()) env_set("B3NATIVE_LANGUAGES_DIR", lroot);
            if (!sp.empty() && !lroot.empty() && b3settings::load_file(sp, ls) && !ls.language.empty()) {
                if (b3native::platform::disc::set_language(lroot, ls.language))
                    std::printf("native_play: language %s (%s), imported in %s\n", ls.language.c_str(), b3asset::language::name_of(ls.language).c_str(), lroot.c_str());
                else
                    std::printf("native_play: language %s: not imported (%s); the game's own text (English)\n", ls.language.c_str(), lroot.c_str());
            }
        }
        if (replay_in) {   // replay1: the same mods, file for file (the set hash covers each file's size and bytes)
            const std::string* want = replay_in->option("mods.hash");
            const std::string have = mod_overlay.empty() ? std::string() : b3mod::hex64(mod_overlay.set_hash());
            if ((want ? *want : std::string()) != have) {
                const std::string text = "This replay was recorded with mod set " + (want ? *want : std::string("(no mods)")) +
                                         " and the installed mods give " + (have.empty() ? std::string("(no mods)") : have) +
                                         ": a mod's files differ from the recording's, so it would not play the same.";
                if (!offscreen) tell_user("cannot play the replay", text, exe_dir);
                else std::fprintf(stderr, "native_play: replay: %s\n", text.c_str());
                return 2;
            }
        }
        std::string why;
        mod_render_args = b3mod::renderer_args(root, mod_overlay, &why);
        if (!why.empty()) std::printf("native_play: mods: %s: the renderer reads the disc's own files\n", why.c_str());
    }

    // the game view (before the first guest call); its renderer is opened here, not at the first poll, so the window
    // and its pad are there from the start
    b3gv_native::Options ro;
    ro.mode = offscreen ? "offscreen" : "window";
    ro.lib = render_lib;
    // mac5: the console clock (save_card.h rtc_now). A window run reads the player's own date and time; a replay reads
    // the clock its recording names; test runs (scripted keys, offscreen) keep the fixed 2004 one, so they stay exact.
    if (const char* given = std::getenv("B3NATIVE_RTC"); !(given && *given)) {
        std::string rtc;
        if (!replay_pad.empty()) {
            b3native::PadReplay peek;
            if (peek.load(replay_pad, b3native::PadReport{})) rtc = peek.rtc();
        } else if (replay_file.empty() && !offscreen && keys.empty()) {   // (a replay: its own clock, or the fixed one)
            const std::time_t now = std::time(nullptr);
            std::tm lt{}, gt{};
#ifdef _WIN32
            localtime_s(&lt, &now);
            gmtime_s(&gt, &now);
#else
            localtime_r(&now, &lt);
            gmtime_r(&now, &gt);
#endif
            long off = (lt.tm_hour - gt.tm_hour) * 3600L + (lt.tm_min - gt.tm_min) * 60L;
            if (lt.tm_year != gt.tm_year) off += lt.tm_year > gt.tm_year ? 86400L : -86400L;
            else if (lt.tm_yday != gt.tm_yday) off += lt.tm_yday > gt.tm_yday ? 86400L : -86400L;
            rtc = std::to_string(static_cast<long long>(now)) + "," + std::to_string(off);
        }
        if (!rtc.empty()) {
#ifdef _WIN32
            _putenv_s("B3NATIVE_RTC", rtc.c_str());
#else
            setenv("B3NATIVE_RTC", rtc.c_str(), 1);
#endif
            std::printf("native_play: console clock from %s (unix seconds, UTC offset)\n", rtc.c_str());
        }
    }
    ro.args = render_args + (safe_mode ? " --safe-mode" : "") + " --native-pad" + (audio && !offscreen ? " --native-audio" : "") +
              mod_render_args + (mods_safe_off ? " --mods-safe-off " + std::to_string(mods_safe_off) : "");   // modkit1b
    ro.game = game;
    ro.vblank = &b3native::platform::vblank_count;
    // mac4: the platform reads the disc's files from B3_DISC_ROOT (platform_sif.cpp disc_root; its default is this PC's
    // data/game or the WSL install): --game is that folder unless B3_DISC_ROOT says otherwise
    if (!game.empty() && !std::getenv("B3_DISC_ROOT")) {
#ifdef _WIN32
        _putenv_s("B3_DISC_ROOT", game.c_str());
#else
        setenv("B3_DISC_ROOT", game.c_str(), 1);
#endif
    }
    if (!b3gv_native::install(ro, exe_dir)) return 2;
    Render r;
    {
        const char* lib = std::getenv("B3PORT_RENDER");
#ifdef _WIN32   // win1: the same b3render.dll the capture loads (LoadLibrary returns the one module)
        // dll1114: a UTF-8 path (gameview_native.cpp), its imports from System32 only; a failure's details go to the
        // run log and the message says what the error means (tools/native/render_load.h)
        unsigned long e = ERROR_MOD_NOT_FOUND;
        HMODULE m = lib ? b3native::render_load::load_renderer(lib, &e) : nullptr;
        if (!m) {
            std::fprintf(stderr, "native_play: cannot load the renderer %s (error %lu)\n", lib ? lib : "(none)", e);
            std::fflush(stderr);
            if (!offscreen)
                tell_user("cannot start", b3native::render_load::render_load_message(e, lib ? lib : "b3render.dll",
                                              b3native::runlog::state().active ? b3native::runlog::state().path : std::string()),
                          exe_dir);
            return 2;
        }
        auto sym = [m](const char* name) { return reinterpret_cast<void*>(GetProcAddress(m, name)); };
#else
        void* m = lib ? dlopen(lib, RTLD_NOW | RTLD_LOCAL) : nullptr;
        if (!m) { std::fprintf(stderr, "native_play: cannot load the renderer %s (%s)\n", lib ? lib : "(none)", dlerror()); return 2; }
        auto sym = [m](const char* name) { return dlsym(m, name); };
#endif
        r.open = reinterpret_cast<int (*)(const char*)>(sym("b3r_open_main"));
        r.run_main = reinterpret_cast<int (*)()>(sym("b3r_run_main"));
        r.pad_sample = reinterpret_cast<int (*)(int, unsigned char*, float)>(sym("b3r_pad_sample"));
        r.pad_key = reinterpret_cast<void (*)(int, int)>(sym("b3r_pad_key"));
        r.focused = reinterpret_cast<int (*)()>(sym("b3r_focused"));
        r.pad_context = reinterpret_cast<void (*)(int)>(sym("b3r_pad_context"));
        r.quit_requested = reinterpret_cast<int (*)()>(sym("b3r_quit_requested"));
        r.close = reinterpret_cast<int (*)(std::uint32_t)>(sym("b3r_close"));
        r.perf_on = reinterpret_cast<int (*)()>(sym("b3r_perf_on"));   // perf3
        r.perf_add = reinterpret_cast<void (*)(int, std::uint64_t)>(sym("b3r_perf_add"));
        r.audio_push = reinterpret_cast<void (*)(const std::int16_t*, std::size_t)>(sym("b3r_audio_push"));
        r.settings_open = reinterpret_cast<int (*)()>(sym("b3r_settings_open"));
        r.settings_active = reinterpret_cast<int (*)()>(sym("b3r_settings_active"));
        r.set_paused = reinterpret_cast<void (*)(int)>(sym("b3r_set_paused"));   // photo mode
        r.photo_row = reinterpret_cast<int (*)()>(sym("b3r_photo_row"));   // photorow1
        r.photo_enter = reinterpret_cast<int (*)()>(sym("b3r_photo_enter"));
        g_photo_row = r.photo_row;
        r.date_format = reinterpret_cast<int (*)()>(sym("b3r_date_format"));   // mac5
        r.speed_units = reinterpret_cast<int (*)()>(sym("b3r_speed_units"));   // units1
        r.record_replays = reinterpret_cast<int (*)()>(sym("b3r_record_replays"));   // replay1
        r.music = reinterpret_cast<int (*)(char*, int)>(sym("b3r_music"));   // custom soundtrack (remaster-features §6)
        r.other_music_apps = reinterpret_cast<int (*)()>(sym("b3r_other_music_apps"));   // nowplaying1
        r.now_playing = reinterpret_cast<void (*)(const char*)>(sym("b3r_now_playing"));
        // save1: the page's SAVES tab drives the card's profile file (export / import on the game's thread)
        if (auto set_hooks = reinterpret_cast<void (*)(int (*)(char*, int), void (*)(int, const char*), int (*)(char*, int))>(
                sym("b3r_set_save_hooks")))
            set_hooks(&save_info, &save_request, &save_status);
        r.menu_actions = reinterpret_cast<int (*)(std::uint32_t*, int)>(sym("b3r_menu_actions"));   // mac5
        r.text_mode = reinterpret_cast<void (*)(int)>(sym("b3r_text_mode"));
        r.text_take = reinterpret_cast<int (*)(char*, int)>(sym("b3r_text_take"));
        r.pad_mouse = reinterpret_cast<void (*)(int, float, float, int)>(sym("b3r_pad_mouse"));
        r.pad_text = reinterpret_cast<void (*)(const char*)>(sym("b3r_pad_text"));
        r.rumble_mode = reinterpret_cast<int (*)()>(sym("b3r_rumble_mode"));   // rumble1
        r.rumble = reinterpret_cast<void (*)(int, int, int)>(sym("b3r_rumble"));
        r.fast_loading = reinterpret_cast<int (*)()>(sym("b3r_fast_loading"));     // fastload1
        r.hide_save_screens = reinterpret_cast<int (*)()>(sym("b3r_hide_save_screens"));   // autosave1
        r.series_shoulders = reinterpret_cast<int (*)()>(sym("b3r_series_shoulders"));   // carsel1
        r.fast_forward = reinterpret_cast<void (*)(int)>(sym("b3r_fast_forward"));
        r.scene_wait = reinterpret_cast<int (*)(int)>(sym("b3r_scene_wait"));   // loadlag1
        r.mod_rows = reinterpret_cast<void (*)(const char*)>(sym("b3r_mod_rows"));   // sdk2b
        r.mod_take = reinterpret_cast<int (*)(char*, int)>(sym("b3r_mod_take"));
        r.mod_value = reinterpret_cast<void (*)(const char*, const char*)>(sym("b3r_mod_value"));
        r.mod_texture = reinterpret_cast<void (*)(unsigned, unsigned, unsigned, const unsigned char*)>(sym("b3r_mod_texture"));
        r.mods_set = reinterpret_cast<void (*)(const char*, const char*)>(sym("b3r_mods_set"));
        r.set_tune_hooks = reinterpret_cast<void (*)(const B3TuneHooks*)>(sym("b3r_set_tune_hooks"));
        r.game_stopped = reinterpret_cast<void (*)(const char*)>(sym("b3r_game_stopped"));   // nofallback1
        if (!r.open || !r.run_main || !r.pad_sample || !r.pad_key || !r.quit_requested || !r.close) {
            std::fprintf(stderr, "native_play: %s lacks the pad functions (an older b3render)\n", lib);
            return 2;
        }
        const char* args = std::getenv("B3PORT_RENDER_ARGS");
        if (r.open(args ? args : "") != 0) { std::fprintf(stderr, "native_play: b3r_open_main failed\n"); return 2; }
    }
    // replay1: where the replay goes -- --record-replay, or with RECORD REPLAYS On a window session's own file
    std::filesystem::path replay_out;
    bool replay_auto = false;
    if (!record_replay.empty()) replay_out = std::filesystem::path(std::u8string(record_replay.begin(), record_replay.end()));
    else if (replay_file.empty() && replay_pad.empty() && !offscreen && r.record_replays && r.record_replays() != 0) {
        const std::filesystem::path dir = b3native::replay::replays_dir(b3native::save::user_data_dir());
        std::error_code ec;
        if (!dir.empty()) std::filesystem::create_directories(dir, ec);
        if (!dir.empty() && !ec) {
            replay_out = b3native::replay::new_file(dir, std::time(nullptr));
            replay_auto = true;
        } else {
            std::fprintf(stderr, "native_play: replay: no replays folder (%s): this session is not recorded\n", dir.empty() ? "no user data folder" : ec.message().c_str());
        }
    }
    if (!replay_out.empty()) std::printf("native_play: replay: recording this session to %s\n", replay_out.string().c_str());
    std::uint64_t replay_parted = 0;   // a replay that left the recorded timeline: the first VBlank whose checkpoint differed

    // The game on a worker thread, the renderer (window, events) on this one: macOS gives windows to the main thread
    // only (SDL3 / Cocoa), and Linux / Windows do not mind. The worker's stack: 16 MB (deep native call chains).
    int game_rc = 1;
    // nofallback1: the stop path. A call to a function with no native version (MissingNative: always in this build when
    // it has no fallback, else with --strict) ends the game cleanly: the message below in the log, the window held with it
    // (b3r_game_stopped: F10 makes the bug report there), a message box after the window, and the run left marked as not
    // ended cleanly, as a crash leaves it (run_state.ini: the launcher's "Last run" line, safe mode after two such runs).
    std::string stop_text;                     // the message box's text (empty: the game did not stop on a missing function)
    std::filesystem::path stop_state_path;     // run_state.ini the renderer marked at the start of this window run
    b3settings::RunState stop_state;           // its state at the stop, written back after the renderer's clean end
    const char* test_missing = std::getenv("B3NATIVE_TEST_MISSING");   // test hook (below)
    if (test_missing && !*test_missing) test_missing = nullptr;
    const std::filesystem::path run_state_path = [&]() -> std::filesystem::path {
        // the renderer's: beside its settings file (--settings in the renderer's arguments, else the user's), in a window
        // run only (an offscreen run has none); the test hook with an explicit --settings takes it offscreen too (tests)
        std::string set;
        if (const std::size_t at = render_args.find("--settings "); at != std::string::npos) {
            std::size_t b = at + 11;
            while (b < render_args.size() && render_args[b] == ' ') ++b;
            std::size_t e = b;
            while (e < render_args.size() && render_args[e] != ' ') ++e;
            set = render_args.substr(b, e - b);
        }
        if (offscreen && !(test_missing && !set.empty())) return {};
        if (!set.empty()) return std::filesystem::path(set).parent_path() / "run_state.ini";
        const std::filesystem::path pref = b3native::save::user_data_dir();
        if (pref.empty() || std::getenv("B3_NO_USER_SETTINGS")) return {};
        return pref / "run_state.ini";
    }();
    auto play_game = [&]() -> int {
        // whatever way the game ends, the feed is closed, so the renderer (on the main thread) ends too
        struct CloseFeed { Render& r; ~CloseFeed() { r.close(5000); } } close_feed{r};
        b3native::install_crash_stack();   // this (the game's) thread reports a stack overflow too
        // the backend, as native_boot sets it up
        std::size_t n = 0;
        const b3native::TableEntry* table = b3native::install(n);
        auto nb = std::make_unique<b3host::NativeBackend>();
        {
            // iso1: from the disc folder or from inside the disc image (<image>/SLUS_210.50, b3asset/discfs.h)
            std::vector<std::uint8_t> d;
            b3asset::disc::read_file(elf, d);
            std::string err;
            if (d.size() != kElfSize || b3host::fnv1a64(d.data(), d.size()) != kElfFnv) {
                std::fprintf(stderr, "native_play: %s is not the retail SLUS_210.50 (size %zu)\n", elf.c_str(), d.size());
                const std::string chd = b3asset::disc::image_error(game);   // chd1: a CHD that cannot be read: why
                if (!chd.empty()) std::fprintf(stderr, "native_play: %s\n", chd.c_str());
                if (!offscreen)
                    tell_user("wrong game data", !chd.empty() ? chd + "\n\nMake the CHD again from the disc, or choose the disc's .iso or folder in "
                                                                      "Burnout3Launcher (Game Data)."
                                                 : d.empty() ? "There is no game program (SLUS_210.50) in " + game +
                                                                 ".\n\nChoose the folder with your Burnout 3 disc's files in Burnout3Launcher (Game Data)."
                                                           : elf + " is not the US Burnout 3: Takedown disc's program (SLUS-21050). The port "
                                                                   "needs that disc's files.", exe_dir);
                return 2;
            }
            if (!nb->load_elf_bytes(d, elf, err)) { std::fprintf(stderr, "native_play: ELF: %s\n", err.c_str()); return 2; }
        }
        b3native::platform::install_hw_model(*nb);
        // the platform mixes on virtual IOP time either way; the sink only plays it (window mode, a device present)
        RenderSink sink;
        sink.fn = audio ? r.audio_push : nullptr;
        b3native::platform::install_rwa_iop(&sink);
        b3host::BackendScope scope(*nb);
        nb->set_stack_pointer(kStackTop);
        std::unique_ptr<b3native::NativeFallback> fallback;   // nofallback1: only in a NATIVE_FALLBACK=1 build
#if B3_NATIVE_FALLBACK
        if (!strict) {
            fallback = std::make_unique<b3native::NativeFallback>(*nb);
            fallback->install_into_backend();
        }
#endif
        const char* const needs_fallback = B3_NATIVE_FALLBACK ? "needs the fallback (not with --strict)"
                                                              : "needs the fallback: a NATIVE_FALLBACK=1 build (make NATIVE_FALLBACK=1 build/native_play)";
        // vu0n: --strict -- a VU0 launch that no generated program covers ends the run (in a dev build with the
        // interpreter too; without it, always)
        b3native::platform::vu0_set_strict(strict);
        // rt1: the guest's busy-waits (RW's _rwDMAForceBufferSwap) jump virtual time to the next event instead of spinning
        // in small cost steps (b3hw_idle_wait -> g_idle_hook); product only -- native_boot (proofs) never sets the hook
        static std::uint64_t idle_skips = 0;
        if (idle_skip)
            b3host::g_idle_hook = [] { if (b3native::platform::skip_to_next_event()) ++idle_skips; };
        struct IdleOff { ~IdleOff() { b3host::g_idle_hook = nullptr; } } idle_off;
        std::unique_ptr<b3native::MovieStandIn> movie_standin;   // EXPLORATION only (tools/native/explore_standins.h)
        if (explore_movies || b3native::MovieStandIn::requested()) {
            movie_standin = std::make_unique<b3native::MovieStandIn>();
            if (!movie_standin->install(*nb)) { std::fprintf(stderr, "native_play: --explore-movies %s\n", needs_fallback); return 2; }
        }
        // mod kit 2a: the one hook table on the native table (src/native/hook_table.h). Every change to what a
        // dispatched address runs goes on it: the port's wrappers below (port layer, in this order: the later one on
        // top), the switch-offs, and from Phase 2b the code mods. After the fallback, whose constructor installs the
        // native table itself.
        b3native::HookTable& hooks = b3native::hook_table();
        if (!hooks.attach(table, n)) { std::fprintf(stderr, "native_play: the hook table does not match the native table\n"); return 2; }
        std::unique_ptr<b3native::RetailFor> retail_for;   // EXPLORATION only (tools/native/explore_standins.h)
        if (!explore_retail.empty()) {
            if (!fallback) { std::fprintf(stderr, "native_play: --explore-retail %s\n", needs_fallback); return 2; }
            retail_for = std::make_unique<b3native::RetailFor>();
            if (!retail_for->install(*nb, *fallback, explore_retail, hooks)) return 2;
        }
        // nofallback1: the stop path's test hook (B3NATIVE_TEST_MISSING=<hex>[,<hex>]): these native functions switched
        // off on the hook table, so their first call is a call to a function missing from this build (with a fallback
        // and without --strict: retail code in the mini, as --explore-retail)
        static std::atomic<bool> test_missing_off{false};
        if (test_missing) {
            std::vector<b3host::gaddr> addrs;
            for (const char* c = test_missing; *c;) {
                char* e = nullptr;
                const auto a = static_cast<b3host::gaddr>(std::strtoul(c, &e, 16));
                if (e == c) break;
                addrs.push_back(a);
                c = *e == ',' ? e + 1 : e;
            }
            std::string why;
            for (b3host::gaddr a : addrs)
                if (!hooks.has_row(a)) why = hex32(a) + " has no native entry";
            if (addrs.empty() || !why.empty() || !hooks.switch_off("port:test_missing", addrs, &test_missing_off, &why)) {
                std::fprintf(stderr, "native_play: B3NATIVE_TEST_MISSING=%s: %s\n", test_missing, addrs.empty() ? "no address" : why.c_str());
                return 2;
            }
            for (b3host::gaddr a : addrs) {
                if (fallback) fallback->retail_for(a);
                std::printf("native_play: TEST (B3NATIVE_TEST_MISSING): %08X %s switched off: its first call is a function "
                            "missing from this build\n", a, b3host::describe_function(a).c_str());
            }
            std::fflush(stdout);
        }
        std::unique_ptr<b3native::PcMenu> pc_menu;   // mac5: the main menu's PC SETTINGS / QUIT GAME rows (tools/native/pc_menu.h)
        if (b3native::PcMenu::requested()) {
            pc_menu = std::make_unique<b3native::PcMenu>();
            if (!pc_menu->install(hooks)) pc_menu.reset();
            else pc_menu->set_photo_row_query([] { return g_live.on ? g_live.photo_row : photo_row_wanted(); });   // photorow1
        }
        std::unique_ptr<b3native::CardSilence> card_silence;   // save1: no card messages (tools/native/card_silence.h)
        if (b3native::CardSilence::requested() && std::getenv("B3NATIVE_SAVES_ROOT")) {
            card_silence = std::make_unique<b3native::CardSilence>();
            if (!card_silence->install(hooks)) card_silence.reset();
        }
        // replay1 (src/native/replay_file.h): the session recorded (--record-replay, RECORD REPLAYS) or a replay played;
        // either way the live settings below are latched per VBlank (g_live)
        const b3native::replay::Report neutral_report = [] {
            b3native::replay::Report s{};
            std::memcpy(s.data(), b3native::platform::sif::neutral_pad_sample(), 32);
            return s;
        }();
        std::unique_ptr<b3native::replay::Recorder> rrec;
        std::unique_ptr<b3native::replay::Player> rplay;
        std::unique_ptr<b3native::TuningConsole> tuning;   // tune4 (installed below, before the table is sealed)
        bool units_recorded = false;   // units1: a "speed_units" entry is in the recording
        auto sample_live = [&](std::uint64_t v) {   // recording: the renderer's values for this VBlank
            char buf[1024] = {};
            g_live.date = r.date_format ? r.date_format() : 0;
            g_live.music_mode = r.music ? r.music(buf, int(sizeof buf - 1)) : 0;
            g_live.music_dir = buf;
            rrec->setting(v, "date_format", std::to_string(g_live.date));
            g_live.photo_row = photo_row_wanted();   // photorow1: the row changes the menu's navigation
            rrec->setting(v, "photo_row", g_live.photo_row ? "1" : "0");
            // units1: kept from the first VBlank it is KM/H (a recording at MPH, the game's, has no entry: as before)
            g_live.units = r.speed_units ? (r.speed_units() == 1 ? 1 : 0) : 0;
            if (g_live.units != 0 || units_recorded) {
                rrec->setting(v, "speed_units", std::to_string(g_live.units));
                units_recorded = true;
            }
            rrec->setting(v, "music", std::to_string(g_live.music_mode) + "|" + g_live.music_dir);
            // the music folder the game would play from (custom_soundtrack.h mode(): the environment wins)
            int mode = g_live.music_mode;
            std::string folder = g_live.music_dir;
            if (const char* e = std::getenv("B3NATIVE_MUSIC_MODE"); e && e[0] >= '0' && e[0] <= '2' && !e[1]) mode = e[0] - '0';
            if (const char* e = std::getenv("B3NATIVE_MUSIC_DIR"); e && *e) folder = e;
            if (mode != 0 && !folder.empty() && !rrec->replay().option("music_fp." + folder)) {
                rrec->replay().options.emplace_back("music_fp." + folder, music_fingerprint(folder));
                // eatrax1: the custom songs' EA TRAX modes as they are now (custom_soundtrack.h modes_digits)
                rrec->replay().options.emplace_back("music_modes." + folder, b3native::CustomSoundtrack::modes_digits(folder));
            }
        };
        bool replay_rumble = false;   // rumble1: playback's acknowledging mode (the recording's "rumble" setting)
        auto apply_live = [&](std::uint64_t v) {   // playback: the recorded values from this VBlank on
            for (const b3native::replay::SettingOp* op : rplay->settings(v)) {
                if (op->key == "rumble") replay_rumble = op->value == "1";
                else if (op->key == "tune") {   // tune4: a tuning edit, written on this VBlank
                    b3tune::Event ev;
                    if (tuning && b3tune::parse_event_value(op->value, ev)) { ev.vblank = v; tuning->replayed(ev); }
                }
                else if (op->key == "photo_row") g_live.photo_row = op->value == "1";   // photorow1
                else if (op->key == "date_format") g_live.date = std::atoi(op->value.c_str());
                else if (op->key == "speed_units") g_live.units = std::atoi(op->value.c_str()) == 1 ? 1 : 0;   // units1
                else if (op->key == "now_playing") g_live.now_playing = op->value;   // nowplaying1
                else if (op->key == "music") {
                    const std::size_t bar = op->value.find('|');
                    g_live.music_mode = std::atoi(op->value.substr(0, bar).c_str());
                    g_live.music_dir = bar == std::string::npos ? std::string() : op->value.substr(bar + 1);
                }
            }
        };
        if (!replay_out.empty()) {
            rrec = std::make_unique<b3native::replay::Recorder>(neutral_report);
            b3native::replay::Replay& h = rrec->replay();
            h.started = static_cast<std::int64_t>(std::time(nullptr));
            {
                const std::time_t now = static_cast<std::time_t>(h.started);
                std::tm lt{}, gt{};
#ifdef _WIN32
                localtime_s(&lt, &now);
                gmtime_s(&gt, &now);
#else
                localtime_r(&now, &lt);
                gmtime_r(&now, &gt);
#endif
                long off = (lt.tm_hour - gt.tm_hour) * 3600L + (lt.tm_min - gt.tm_min) * 60L;
                if (lt.tm_year != gt.tm_year) off += lt.tm_year > gt.tm_year ? 86400L : -86400L;
                else if (lt.tm_yday != gt.tm_yday) off += lt.tm_yday > gt.tm_yday ? 86400L : -86400L;
                h.utc_offset = static_cast<std::int32_t>(off);
            }
            h.version = b3port::version();
            h.program_size = kElfSize;
            h.program_fnv = kElfFnv;
#if defined(_WIN32)
            const char* host = "windows";
#elif defined(__APPLE__)
            const char* host = "macos";
#else
            const char* host = "linux";
#endif
            h.info = {{"recorded", b3port::runlog::stamp(static_cast<std::time_t>(h.started))}, {"host", host}, {"build", b3port::version_line()}};
            h.options = {{"idle_skip", idle_skip ? "1" : "0"}, {"movie_skip", movie_skip_on ? "1" : "0"},
                         {"explore_movies", explore_movies ? "1" : "0"}, {"strict", strict ? "1" : "0"}};
            for (const char* name : b3native::replay::kGameEnv)
                if (const char* e = std::getenv(name)) h.options.emplace_back(std::string("env.") + name, e);
            if (const std::string g = b3native::tuning_gameplay_ids(mod_overlay); !g.empty()) h.info.emplace_back("gameplay", g);   // tune4
            if (!mod_overlay.empty()) {   // modkit1: the mod set (playback takes these ids and checks the hash)
                h.options.emplace_back("mods.ids", mod_ids);
                h.options.emplace_back("mods.hash", b3mod::hex64(mod_overlay.set_hash()));
            }
            if (const char* root = std::getenv("B3NATIVE_SAVES_ROOT"); root && *root) {   // the card as the game gets it (ProfileCard::attach)
                h.card = true;
                h.card_slot = b3native::save::read_active_slot(root);
                std::optional<std::int64_t> fixed;
                if (const char* t = std::getenv("B3NATIVE_CARD_FILE_TIME"); t && *t) fixed = std::strtoll(t, nullptr, 10);
                std::string why;
                const auto loaded = b3native::save::load_profile(root, h.card_slot, &why, fixed);
                h.card_sav = b3native::save::write_sav(loaded ? loaded->dirs : b3native::save::Directories{});   // unreadable: an empty card, as attach
            }
            g_live.on = true;
            sample_live(0);
        }
        if (replay_in) {
            rplay = std::make_unique<b3native::replay::Player>(*replay_in);
            g_live.on = true;
            apply_live(0);
        }
        static int (*g_date_format)() = nullptr;   // mac5: SETTINGS > INTERFACE > DATE FORMAT (tools/native/date_format.h)
        g_date_format = r.date_format;
        b3native::DateFormat date_format;
        date_format.install(hooks, [] { return g_live.on ? g_live.date : g_date_format ? g_date_format() : 0; });
        static int (*g_speed_units)() = nullptr;   // units1: SETTINGS > INTERFACE > SPEED UNITS (tools/native/speed_units.h)
        g_speed_units = r.speed_units;
        b3native::SpeedUnits speed_units;
        speed_units.install(hooks, [] { return g_live.on ? g_live.units : g_speed_units ? g_speed_units() : 0; },
                            [] { return static_cast<std::uint64_t>(b3native::platform::vblank_count()); });
        // the ONLINE dialog's words: the port's own instead of the PS2's "no network adaptor" (tools/native/online_message.h;
        // B3NATIVE_ONLINE_MESSAGE=0: the game's text)
        b3native::OnlineMessage online_message;
        online_message.set_language(b3native::platform::disc::language_text());   // 1.1: a language pack's words
        online_message.install(hooks);
        static int (*g_music)(char*, int) = nullptr;   // PC SETTINGS > INTERFACE > MUSIC (tools/native/custom_soundtrack.h)
        g_music = r.music;
        b3native::CustomSoundtrack custom_soundtrack;
        if (!std::getenv("B3NATIVE_MUSIC_OFF"))   // B3NATIVE_MUSIC_OFF=1: not installed (comparisons)
            custom_soundtrack.install(hooks, [](char* folder, int max) {
                if (g_live.on) {   // replay1: the latched value
                    if (folder && max > 0) std::snprintf(folder, static_cast<std::size_t>(max), "%s", g_live.music_dir.c_str());
                    return g_live.music_mode;
                }
                return g_music ? g_music(folder, max) : 0;
            });
        // nowplaying1 (tools/native/now_playing_popup.h): another music app's song in the EA TRAX popup, latched per
        // VBlank below (PC SETTINGS > INTERFACE > MUSIC > OTHER MUSIC APPS; no poller, no system calls while it is off)
        b3native::NowPlayingSource now_playing_source;
        static b3native::NowPlayingSource* g_now_playing = nullptr;
        g_now_playing = &now_playing_source;
        b3native::NowPlayingPopup now_playing_popup;
        if (!std::getenv("B3NATIVE_NOW_PLAYING_OFF"))
            now_playing_popup.install(hooks, []() -> const b3nowplaying::Track& { return g_now_playing->track(); });
        const int now_playing_env = [] {   // B3NATIVE_NOW_PLAYING=0 / 1: the setting overridden (tests)
            const char* e = std::getenv("B3NATIVE_NOW_PLAYING");
            return e && (e[0] == '0' || e[0] == '1') && !e[1] ? e[0] - '0' : -1;
        }();
        const std::uint64_t now_playing_from = [] {   // B3NATIVE_NOW_PLAYING_AT=<VBlank>: not read before it (tests: a popup on a known frame)
            const char* e = std::getenv("B3NATIVE_NOW_PLAYING_AT");
            return e && *e ? std::strtoull(e, nullptr, 10) : 0ull;
        }();
        bool now_playing_recorded = false;
        std::uint64_t now_playing_line = 0;
        b3native::DebugHealth debug_health;   // TEST ONLY: B3NATIVE_DEBUG_HEALTH=<vblank>:<value> (tools/native/debug_health.h)
        if (b3native::DebugHealth::requested()) debug_health.install(hooks);
        b3native::CallCount call_count;   // debugging: B3NATIVE_CALL_COUNT (tools/native/debug_health.h)
        if (std::getenv("B3NATIVE_CALL_COUNT")) call_count.install(hooks);
        b3native::TimeTrace time_trace;   // sif-lane: B3NATIVE_TIME_TRACE (tools/native/time_trace.h)
        if (b3native::TimeTrace::requested() && !time_trace.install(hooks)) return 2;
        std::unique_ptr<b3native::MenuMouse> menu_mouse;   // mac5: the mouse in the game's menus, typed text (menu_mouse.h)
        if (b3native::MenuMouse::requested()) {
            menu_mouse = std::make_unique<b3native::MenuMouse>();
            if (!menu_mouse->install(hooks, {r.menu_actions, r.text_mode, r.text_take})) menu_mouse.reset();
            if (menu_mouse && rplay) menu_mouse->replay_text(true);   // replay1: the recorded typing, not the window's
        }
        std::unique_ptr<b3native::DiffCheck> diff_check;   // mac5: B3NATIVE_DIFF (tools/native/diff_check.h)
        if (!b3native::DiffCheck::requested().empty()) {
            if (!fallback) { std::fprintf(stderr, "native_play: B3NATIVE_DIFF %s\n", needs_fallback); return 2; }
            diff_check = std::make_unique<b3native::DiffCheck>();
            if (!diff_check->install(*nb, *fallback, b3native::DiffCheck::requested(), table, n, hooks)) return 2;
        }
        std::unique_ptr<b3native::PoisonCheck> poison_check;   // mac5: B3NATIVE_POISON (tools/native/poison_check.h)
        if (b3native::PoisonCheck::requested()) {
            poison_check = std::make_unique<b3native::PoisonCheck>();
            if (!poison_check->install(hooks)) return 2;
        }
        // tune4 (tools/native/tuning_console.h): the tuning registry, when the console is on, a mod has a preset, a script
        // edits, or a recording being played has edits; otherwise nothing is wrapped
        {
            b3native::TuningConsole::Options to;
            to.presets = b3native::tuning_presets(mod_overlay);
            to.console = tuning_arg == 1 || (tuning_arg < 0 && developer_mods);   // the one Developer mods switch (above)
            bool recorded = false;
            if (replay_in)
                for (const b3native::replay::SettingOp& op : replay_in->settings) recorded = recorded || op.key == "tune";
            if (!replay_pad.empty()) {
                b3native::PadReplay peek;
                recorded = peek.load(replay_pad, b3native::PadReport{}) && !peek.tune_lines().empty();
            }
            to.replaying = replay_in || !replay_pad.empty();
            if (!to.replaying) to.script = tune_script;
            to.mods_dir = !mods_dir.empty() ? std::filesystem::path(mods_dir) : b3native::save::user_data_dir().empty() ? std::filesystem::path()
                          : b3mod::mods_folder(b3native::save::user_data_dir());
            to.log_registrations = [] { const char* e = std::getenv("B3NATIVE_TUNE_LOG"); return e && *e == '1'; }();
            if (to.console || !to.presets.empty() || !to.script.empty() || recorded) {
                tuning = std::make_unique<b3native::TuningConsole>();
                const bool console = to.console;
                if (!tuning->install(hooks, std::move(to))) {
                    if (!tune_script.empty() || recorded || !b3native::tuning_presets(mod_overlay).empty()) return 2;   // asked for: no half run
                    tuning.reset();
                } else if (console && r.set_tune_hooks) r.set_tune_hooks(tuning->renderer_hooks());
            }
        }
        // sdk2b (src/native/code_mods.h): the code mods' libraries, in load order, after every port wrapper. A mod that
        // does not load is left out with its files (the set rebuilt, the renderer told); fixed for the run from here on.
        std::unique_ptr<b3native::CodeMods> code_mods;
        std::vector<b3mod::SettingSet> mod_sets_start;   // every row's value at the start (recordings: "set 0 ...")
        bool any_code = false;
        for (const b3mod::ActiveMod& m : mod_overlay.mods()) any_code = any_code || m.code.present || m.script.present;   // lua6: scripts too
        if (any_code) {
            b3native::CodeMods::Host h;
            h.port_version = b3port::version_line();
            h.vblank = [] { return b3native::platform::vblank_count(); };
            h.disc_size = [](const std::string& p) -> std::int64_t {
                b3native::platform::disc::File f;
                return f.open(p) ? static_cast<std::int64_t>(f.size) : -1;
            };
            h.disc_read = [](const std::string& p, std::uint64_t off, void* dst, std::uint64_t want) -> std::int64_t {
                b3native::platform::disc::File f;
                if (!f.open(p)) return -1;
                if (off >= f.size) return 0;
                const std::uint64_t k = std::min<std::uint64_t>(want, f.size - off);
                f.read(off, dst, static_cast<std::size_t>(k));
                return static_cast<std::int64_t>(k);
            };
            if (r.mod_texture) h.texture = [&r](std::uint32_t id, std::uint32_t w, std::uint32_t hh, const std::uint8_t* px) { r.mod_texture(id, w, hh, px); };
            const bool window_values = !offscreen && !mods_pref.empty();
            if (window_values) h.saved_values = [&](const std::string& id) { return b3mod::load_values(b3mod::settings_file(mods_pref, id)); };
            code_mods = std::make_unique<b3native::CodeMods>(hooks, b3native::guest_arena(), h);
            std::vector<b3native::CodeMods::Candidate> cands;
            std::vector<b3mod::ModState> state;
            std::set<std::string> left_out;
            for (const b3mod::ActiveMod& m : mod_overlay.mods()) {
                if (m.script.present) {   // lua6: a script mod, the same loader (src/native/script_mods.h)
                    cands.push_back(b3native::script_candidate(m, developer_mods));
                    continue;
                }
                if (!m.code.present) continue;
                if (const auto it = code_refused.find(m.id); it != code_refused.end()) {
                    std::printf("native_play: mods: %s: not loaded: %s\n", m.id.c_str(), it->second.c_str());
                    left_out.insert(m.id);
                    b3mod::ModState st;
                    st.id = m.id;
                    st.reason = it->second;
                    st.digest = code_digest[m.id];
                    state.push_back(st);
                    continue;
                }
                cands.push_back({m.id, m.version, m.dir, m.library, m.code.platform});
            }
            for (const b3native::CodeMods::Result& res : code_mods->load(cands, developer_mods)) {
                b3mod::ModState st;
                st.id = res.id;
                st.loaded = res.loaded;
                st.reason = res.reason;
                st.digest = code_digest[res.id];
                for (const b3mod::SettingRow& row : code_mods->settings().rows())
                    if (row.mod == res.id) st.rows.push_back(row);
                if (!res.loaded) left_out.insert(res.id);
                state.push_back(st);
            }
            if (!offscreen && !mods_pref.empty()) {   // the launcher's Mods page shows it
                std::string why;
                if (!b3mod::save_state(mods_pref, state, &why)) std::fprintf(stderr, "native_play: mods: %s\n", why.c_str());
            }
            if (!left_out.empty()) {   // the set without them: the game and the renderer read none of their files
                std::vector<const b3mod::Mod*> keep;
                for (const b3mod::Mod* m : mods_selected)
                    if (!left_out.count(m->manifest.id)) keep.push_back(m);
                const std::vector<std::string> errors_before = mod_overlay.errors();
                mod_overlay = b3mod::Overlay::build(keep, true, &mods_penv);   // patches3: the same patches, from the cache
                if (tuning) tuning->drop_presets(left_out);   // tune4: and without their [tuning] presets
                for (const std::string& e : mod_overlay.errors())   // printed above already, unless new (the cache went away)
                    if (std::find(errors_before.begin(), errors_before.end(), e) == errors_before.end())
                        std::fprintf(stderr, "native_play: mods: %s (folder %s)\n", e.c_str(), b3mod::path_utf8(mods_root).c_str());
                mod_ids.clear();
                for (const b3mod::ActiveMod& m : mod_overlay.mods()) mod_ids += (mod_ids.empty() ? "" : ",") + m.id;
                b3native::platform::disc::set_overlay(mod_overlay.empty() ? nullptr : &mod_overlay);
                if (mod_overlay.empty()) std::printf("native_play: mods: none (the code mods above did not load)\n");
                else std::printf("native_play: mods: active (%s; the first wins; without the mods that did not load): %s\n", mods_from.c_str(),
                                 b3mod::describe(mod_overlay).c_str());
                if (r.mods_set) r.mods_set(mod_ids.c_str(), mod_overlay.empty() ? "" : b3mod::hex64(mod_overlay.set_hash()).c_str());
                if (replay_in) {   // the recording was made with these mods loaded
                    const std::string* want = replay_in->option("mods.hash");
                    const std::string have = mod_overlay.empty() ? std::string() : b3mod::hex64(mod_overlay.set_hash());
                    if ((want ? *want : std::string()) != have) {
                        const std::string text = "This replay was recorded with a code mod that did not load here, so it would not play the same.";
                        if (!offscreen) tell_user("cannot play the replay", text, exe_dir);
                        else std::fprintf(stderr, "native_play: replay: %s\n", text.c_str());
                        return 2;
                    }
                }
                if (rrec) {   // the replay file names the set the game ran with
                    auto& info = rrec->replay().info;   // tune4: the mods that change gameplay, of this set
                    info.erase(std::remove_if(info.begin(), info.end(), [](const auto& i) { return i.first == "gameplay"; }), info.end());
                    if (const std::string g = b3native::tuning_gameplay_ids(mod_overlay); !g.empty()) info.emplace_back("gameplay", g);
                    auto& opts = rrec->replay().options;
                    opts.erase(std::remove_if(opts.begin(), opts.end(), [](const auto& o) { return o.first == "mods.ids" || o.first == "mods.hash"; }), opts.end());
                    if (!mod_overlay.empty()) {
                        opts.emplace_back("mods.ids", mod_ids);
                        opts.emplace_back("mods.hash", b3mod::hex64(mod_overlay.set_hash()));
                    }
                }
            }
            // the settings: a replay's recorded values; else --mod-set's (from the start, or at a VBlank)
            b3mod::LiveSettings& ls = code_mods->settings();
            if (rplay) {
                std::vector<b3mod::SettingSet> ev;
                for (const b3native::replay::SettingOp& op : replay_in->settings)
                    if (op.key.rfind("mod.", 0) == 0)
                        if (const std::size_t dot = op.key.find('.', 4); dot != std::string::npos)
                            ev.push_back({op.vblank, op.key.substr(4, dot - 4), op.key.substr(dot + 1), op.value});
                ls.play(std::move(ev));
            } else if (!replay_pad.empty()) {
                b3native::PadReplay peek;
                if (peek.load(replay_pad, b3native::PadReport{})) ls.play(peek.sets());
            } else {
                std::string spec = mod_set_arg;
                if (spec.empty())
                    if (const char* e = std::getenv("B3NATIVE_MOD_SET")) spec = e;
                std::vector<b3mod::SettingSet> sets;
                std::string why;
                if (!b3mod::parse_set_spec(spec, sets, &why)) { std::fprintf(stderr, "native_play: --mod-set: %s\n", why.c_str()); return 2; }
                for (const b3mod::SettingSet& s : sets)
                    if (ls.find(s.mod, s.key) < 0 || b3mod::value_from_text(ls.rows()[std::size_t(ls.find(s.mod, s.key))], s.value) < 0) {
                        std::fprintf(stderr, "native_play: --mod-set: %s.%s = %s: no such setting or value (of the mods that loaded)\n",
                                     s.mod.c_str(), s.key.c_str(), s.value.c_str());
                        return 2;
                    }
                ls.schedule(std::move(sets));
            }
            for (const b3mod::SettingSet& s : ls.boundary(0))
                std::printf("native_play: mods: setting %s.%s = %s from the start\n", s.mod.c_str(), s.key.c_str(), s.value.c_str());
            mod_sets_start = ls.snapshot(0);
            for (const b3mod::SettingSet& s : mod_sets_start)
                if (rrec) rrec->setting(0, "mod." + s.mod + "." + s.key, s.value);
            if (!ls.rows().empty() && r.mod_rows) r.mod_rows(b3mod::rows_to_text(ls.rows()).c_str());
            if (code_mods->any()) {   // the overlay: queued by the mods, handed to the renderer with each frame
                static b3native::CodeMods* g_code_mods = nullptr;
                g_code_mods = code_mods.get();
                b3gv_native::set_frame_hook([] {
                    g_code_mods->frame();
                    const std::vector<std::uint8_t> rec = g_code_mods->overlay_record();
                    if (!rec.empty()) b3gv_native::set_mod_overlay(rec);
                });
            } else {
                code_mods.reset();
            }
        }
        // Fixed for the run from here on.
        hooks.seal();
        std::unique_ptr<b3native::ReturnStandIn> return_standin;   // EXPLORATION only (tools/native/explore_standins.h)
        if (!explore_return.empty()) {
            return_standin = std::make_unique<b3native::ReturnStandIn>();
            if (!return_standin->install(*nb, explore_return)) return 2;
        }
        std::printf("native_play: %zu native functions; ELF %s; fallback %s; %s, %s\n", n, elf.c_str(),
                    !B3_NATIVE_FALLBACK ? "compiled out" : strict ? "off" : "on",
                    offscreen ? "offscreen" : "window", pace ? ("paced at " + std::to_string(hz) + " Hz").c_str() : "unpaced");

        // per VBlank: pace, pad, quit
        using clock = std::chrono::steady_clock;
        const auto period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / hz));
        const auto start = clock::now();
        const auto ns_of = [](clock::time_point t) {
            return std::int64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
        };
        b3native::Pacer pacer;   // play1's pacing rule (fast_load.h)
        std::int64_t perf_prev_out = 0, perf_pace_from = 0;   // perf3: the previous hook's end, the pacing wait's start
        pacer.period = std::int64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(period).count());
        pacer.base = ns_of(start);
        const std::vector<KeyEvent> script = parse_keys(keys);
        std::size_t next_key = 0;
        // fe1: --keys2, the second controller (port 2): connected for the whole run, its report made here from its own
        // key script (src/native/input_keys: the default map), set each VBlank after port 1's
        const std::vector<KeyEvent> script2 = parse_keys(keys2);
        std::size_t next_key2 = 0;
        b3native::input::KeyPad pad2;
        if (!keys2.empty() && !rplay) {
            b3native::platform::sif::set_pad_connected(1, true);
            if (rrec) rrec->connect(0, 1, true);
            std::printf("native_play: a second controller (port 2) with %zu scripted key events\n", script2.size());
        }
        if (rplay)   // replay1: the ports plugged in before the boot
            for (const auto& [port, on] : rplay->step(0)) {
                b3native::platform::sif::set_pad_connected(int(port), on);
                if (port == 1 && on) std::printf("native_play: a second controller (port 2) from the replay\n");
            }
        // pad2: else the window's second gamepad is port 2 (b3r_pad_sample(1): src/b3replay/nativepad.h), plugged in /
        // pulled out as it connects and disconnects; not while --keys2, --replay-pad or --replay drives the pads
        const bool window_port2 = keys2.empty() && replay_pad.empty() && !rplay;
        // replay1: the viewer's pause (Start in the window), the checkpoints, the session's driving for the file's notes
        const bool viewer_pause = rplay && !offscreen && keys.empty();
        bool viewer_start_held = true;
        std::uint64_t checks_seen = 0, checks_ok = 0, drive_starts = 0, drive_vblanks = 0;
        const std::uint32_t check_every = replay_in ? std::max<std::uint32_t>(replay_in->check_every, 1) : b3native::replay::kCheckEvery;
        bool port2_plugged = false;
        // rumble1 (src/native/pad_actuator.h): the pads' actuator requests acknowledged while PC SETTINGS > RUMBLE is On
        // (the renderer's b3r_rumble_mode; B3NATIVE_RUMBLE=0|1 overrides it; a replay follows its recording's switches).
        // Off (the default) leaves them pending as before.
        const int rumble_forced = [] {
            const char* e = std::getenv("B3NATIVE_RUMBLE");
            return e && *e ? (*e == '0' ? 0 : 1) : -1;
        }();
        bool rumble_on = false, rumble_ever = false;
        const std::vector<MouseEvent> mouse_events = parse_mouse(mouse_script);   // mac5
        const std::vector<TextEvent> text_events = parse_text(text_script);
        std::size_t next_mouse = 0, next_text = 0;
        PadLog pad;
        const std::array<unsigned char, 32> neutral = [] {
            std::array<unsigned char, 32> s{};
            std::memcpy(s.data(), b3native::platform::sif::neutral_pad_sample(), 32);
            return s;
        }();
        pad.last = neutral;
        // mac4: pad recording / replay and the movie skip
        b3native::PadRecorder recorder;
        b3native::PadReplay replay;
        if (!record_pad.empty()) {
            if (recorder.open(record_pad)) {
                std::printf("native_play: recording the pad to %s\n", record_pad.c_str());
                if (const char* rtc = std::getenv("B3NATIVE_RTC")) recorder.note_rtc(rtc);   // mac5
                if (!mod_overlay.empty()) recorder.note_mods(b3mod::hex64(mod_overlay.set_hash()), mod_ids);   // modkit1
                recorder.note_gameplay(b3native::tuning_gameplay_ids(mod_overlay));   // tune4: a preset changes gameplay
                for (const b3mod::SettingSet& s : mod_sets_start) recorder.note_set(b3mod::set_line(s));      // sdk2b
            }
            else std::fprintf(stderr, "native_play: cannot write %s (no pad recording)\n", record_pad.c_str());
        }
        if (tuning)   // tune4: each edit made in this run into the pad recording and the replay file
            tuning->on_event = [&](const b3tune::Event& ev, const std::string& what) {
                recorder.note_tune(b3tune::event_line(ev, what));
                if (rrec) rrec->event(ev.vblank, "tune", b3tune::event_value(ev));
            };
        if (!replay_pad.empty()) {
            if (!replay.load(replay_pad, neutral)) { std::fprintf(stderr, "native_play: %s\n", replay.error().c_str()); return 2; }
            std::printf("native_play: replaying the pad from %s (%zu changes, recorded to VBlank %llu); the window's keys are ignored\n",
                        replay_pad.c_str(), replay.changes(), static_cast<unsigned long long>(replay.end()));
            if (tuning)   // tune4: the recorded tuning edits, on their VBlanks
                for (const std::string& l : replay.tune_lines()) {
                    b3tune::Event ev;
                    if (b3tune::parse_event_line(l, ev)) tuning->replayed(ev);
                }
            const std::string now = mod_overlay.empty() ? std::string() : b3mod::hex64(mod_overlay.set_hash());
            if (replay.mods() != now)   // modkit1: another mod set may not replay the same
                std::printf("native_play: mods: the recording was made with mod set %s, this run has %s: it may not replay the same\n",
                            replay.mods().empty() ? "none" : replay.mods().c_str(), now.empty() ? "none" : now.c_str());
        }
        bool pad_driving = false;
        std::uint64_t accel_seen = 0, accel_vblank = 0;
        bool game_paused = false;   // photo mode: the race's pause menu is up
        std::uint64_t pause_seen = 0, pause_vblank = 0;
        b3native::PageHold page_hold;   // the pad withheld for the PC SETTINGS page, on the VBlank grid (page_hold.h)
        bool page_held = false;
        b3gv_native::set_hold_probe(hold_probe);   // autosave1: the AUTOSAVE page's frames are held (save_screens.h)
        if (!b3gv_native::watch_extra(0x0012FA40u, [](std::uint32_t, const std::uint32_t*) { g_accel_calls.fetch_add(1, std::memory_order_relaxed); }))
            std::printf("native_play: no game view watch: the keys stay in the menu layout\n");
        // the movie skip learns each movie's file at CGtVideoDecoder::Prepare (0x3254F0; the name in $t0)
        if (!b3gv_native::watch_extra(b3native::MovieSkip::kPrepare, [](std::uint32_t, const std::uint32_t* a) {
                if (a[0] == b3native::MovieSkip::kDecoder && g_movie_skip) g_movie_skip->prepared(a[4]);
                std::string n;
                for (std::uint32_t i = 0; i < 64; ++i) {
                    const char c = static_cast<char>(b3host::load<std::uint8_t>(a[4] + i));
                    if (!c) break;
                    n.push_back(c);
                }
                std::printf("native_play: VBlank %llu: movie %s (decoder 0x%08X, flags %u)\n",
                            static_cast<unsigned long long>(b3native::platform::vblank_count()), n.c_str(), a[0], a[5]);
                std::fflush(stdout);
            }))
            std::printf("native_play: no game view watch: movies are not skippable\n");
        b3native::MovieSkip skip;
        skip.enabled = movie_skip_on;
        skip.load8 = [](std::uint32_t a) { return b3host::load<std::uint8_t>(a); };
        skip.store8 = [](std::uint32_t a, std::uint8_t v) { b3host::store<std::uint8_t>(a, v); };
        g_movie_skip = &skip;
        b3native::SkipHintFade hint_fade;   // mac5: the on-screen hint (movie_skip.h)
        bool hint_shown = false;
        // carsel1 (car_select.h, input_keys.h ShoulderSeries): CAR SERIES ON L1 / R1, opt-in. Each port's own translation
        // (port 2: G2's car select in MULTIPLAYER); the live reports only (a replay holds the translated ones)
        b3native::input::ShoulderSeries series1, series2;
        bool series_on = false, series_page = false;
        std::uint32_t series_shown = 0xFFFFFFFFu;   // the carousel's series / car last logged (while on)
        auto series_apply = [&](b3native::input::ShoulderSeries& t, std::array<unsigned char, 32>& rep, int port, std::uint64_t vb) {
            const bool was[2] = {t.translating(0), t.translating(1)};
            const bool changed = t.apply(rep, series_on, series_page);
            for (int k = 0; k < 2; ++k)
                if (t.translating(k) && !was[k]) {   // a press begun on the page
                    std::printf("native_play: VBlank %llu: car series: port %d %s given to the game as %s\n", static_cast<unsigned long long>(vb),
                                port + 1, k ? "R1" : "L1", k ? "Right" : "Left");
                    std::fflush(stdout);
                }
            return changed;
        };
        // fastload1: FAST LOADING (fast_load.h). The loading screen's spans are logged in every run (VBlanks, wall time).
        b3native::FastLoad fast_load;
        bool load_up = false, fast_setting = false, fast_setting_known = false;
        bool autosave_up = false, span_is_load = false;   // autosave1
        const bool page_log = std::getenv("B3NATIVE_PAGE_LOG") != nullptr;
        std::uint32_t page_logged = 0xFFFFFFFFu;
        std::uint64_t autosave_from = 0;
        std::uint64_t load_from = 0, loads = 0, load_vblanks = 0, load_audible_from = 0;
        auto load_wall_from = clock::now();
        double load_wall = 0;
        std::fflush(stdout);
        std::string end_reason;
        // rt1: the rate in windows of 600 VBlanks (10 s of PS2 time): host wall time per window, so a slow stretch
        // (a race behind real time) shows in the log, with the fallback calls made in it
        auto rate_mark = clock::now();
        const bool run_unfocused = [] { const char* e = std::getenv("B3NATIVE_RUN_UNFOCUSED"); return e && *e && *e != '0'; }();
        std::uint64_t rate_fb = 0;
#if B3_HW_DEV
        // dev builds (make HW_DEV=1; the shipped game walks no DMA chain): the PATH3 check (hw_model_dev.h Path3Check:
        // the hardware layer's PATH3 rules against a VIF code walk of every VIF1 chain, fatal on a violation, its counts
        // at the end) and mac5's B3NATIVE_VIF1_DUMP=<first>-<last>: the words of these VIF1 transfers (by the DMA model's
        // transfer count) as the transfer reads them from guest memory (the Windows VIF1 stream against Linux's)
        {
            static unsigned long long lo = 1, hi = 0;
            if (const char* e = std::getenv("B3NATIVE_VIF1_DUMP")) std::sscanf(e, "%llu-%llu", &lo, &hi);
            b3native::platform::hw_model().on_dma = [](unsigned c, std::uint32_t chcr, std::uint32_t madr, std::uint32_t qwc, std::uint32_t tadr) {
                auto& hw = b3native::platform::hw_model();
                const std::uint32_t regs[4] = {chcr, madr, qwc, tadr};
                auto rd = [](std::uint32_t a) { std::uint32_t v = 0; std::memcpy(&v, b3host::host_pointer(a, 4), 4); return v; };
                g_path3_check.kick(c, regs, hw.mskpath3(), hw.clock ? hw.clock() : 0, rd);
                const std::uint64_t n = hw.dma_count();
                static std::uint64_t vif1_hash = 0;
                if (c == 1 && n >= lo && n <= hi)
                    b3native::hash_dma_words(c, regs, vif1_hash, rd, [n](std::uint32_t a, std::uint32_t w, unsigned) {
                        std::printf("vif1 %llu %08X %08X\n", static_cast<unsigned long long>(n), a, w);
                    });
            };
        }
#endif
        // mac5: B3NATIVE_RAM_HASH=<first>-<last>:<step>: an FNV-1a hash of each MB of guest RAM at those VBlanks (where two
        // hosts' runs of one input first part: the Windows car-select hang)
        // sif-lane: B3NATIVE_RAM_HASH_IGNORE=<hex lo>-<hex hi>,...: those bytes hashed as zero (a library's own .bss that a
        // platform row no longer keeps), so two runs can be compared over everything else
        if (const char* e = std::getenv("B3NATIVE_RAM_HASH")) {
            unsigned long long lo = 0, hi = 0, step = 1;
            std::vector<std::pair<b3host::gaddr, b3host::gaddr>> ignore;
            if (const char* ig = std::getenv("B3NATIVE_RAM_HASH_IGNORE"))
                for (const char* q = ig; *q;) {
                    char* end = nullptr;
                    const unsigned long a = std::strtoul(q, &end, 16);
                    if (end == q || *end != '-') break;
                    const unsigned long b = std::strtoul(end + 1, &end, 16);
                    ignore.emplace_back(static_cast<b3host::gaddr>(a), static_cast<b3host::gaddr>(b));
                    q = *end == ',' ? end + 1 : end;
                }
            if (std::sscanf(e, "%llu-%llu:%llu", &lo, &hi, &step) >= 2 && hi >= lo && step > 0)
                b3native::platform::add_vblank_hook([lo, hi, step, ignore] {
                    const std::uint64_t v = b3native::platform::vblank_count();
                    if (v < lo || v > hi || (v - lo) % step) return;
                    // layer_check: one write per line (other threads print to stdout too; a line built from many
                    // printf calls came out split)
                    char line[64 + 32 * 9];
                    int n = std::snprintf(line, sizeof line, "native_play: VBlank %llu: ram", static_cast<unsigned long long>(v));
                    static std::vector<std::uint8_t> mbuf(0x100000u);
                    for (b3host::gaddr mb = 0; mb < 0x02000000u; mb += 0x100000u) {
                        b3host::copy_from_guest(mbuf.data(), mb, 0x100000u);
                        for (const auto& [ilo, ihi] : ignore)
                            for (b3host::gaddr a = std::max(ilo, mb); a < std::min<b3host::gaddr>(ihi, mb + 0x100000u); ++a) mbuf[a - mb] = 0;
                        std::uint64_t h = 0xCBF29CE484222325ull;
                        for (std::size_t k = 0; k < mbuf.size(); k += 8) {
                            std::uint64_t w;
                            std::memcpy(&w, mbuf.data() + k, 8);
                            h = (h ^ w) * 0x100000001B3ull;
                        }
                        n += std::snprintf(line + n, sizeof line - static_cast<std::size_t>(n), " %08x", static_cast<unsigned>(h ^ (h >> 32)));
                    }
                    std::printf("%s\n", line);
                });
        }
        // sif-lane: B3NATIVE_RAM_DUMP=<vblank>[,<vblank>...]:<file prefix>: guest RAM (32 MiB) written to <prefix>_<vblank>.bin at
        // those VBlanks (two runs compared word by word where B3NATIVE_RAM_HASH says they part)
        if (const char* e = std::getenv("B3NATIVE_RAM_DUMP")) {
            const std::string spec(e);
            const std::size_t colon = spec.find(':');
            if (colon != std::string::npos) {
                std::vector<unsigned long long> at;
                for (std::size_t q = 0; q < colon;) {
                    const std::size_t comma = std::min(spec.find(',', q), colon);
                    at.push_back(std::strtoull(spec.substr(q, comma - q).c_str(), nullptr, 10));
                    q = comma + 1;
                }
                const std::string prefix = spec.substr(colon + 1);
                b3native::platform::add_vblank_hook([at, prefix] {
                    const unsigned long long v = b3native::platform::vblank_count();
                    if (std::find(at.begin(), at.end(), v) == at.end()) return;
                    std::vector<std::uint8_t> ram(0x02000000u);
                    b3host::copy_from_guest(ram.data(), 0, 0x02000000u);
                    const std::string path = prefix + "_" + std::to_string(v) + ".bin";
                    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
                        std::fwrite(ram.data(), 1, ram.size(), f);
                        std::fclose(f);
                    }
                });
            }
        }
        // mac5: B3NATIVE_HW_LOG=<first>-<last>: the DMA model's state (HwModel::debug_state) at each of these VBlanks
        // (the Windows race-start stall: the VIF1 parser's payload count against Linux's)
        if (const char* e = std::getenv("B3NATIVE_HW_LOG")) {
            unsigned long long lo = 0, hi = 0;
            if (std::sscanf(e, "%llu-%llu", &lo, &hi) == 2 && hi >= lo)
                b3native::platform::add_vblank_hook([lo, hi] {
                    const std::uint64_t v = b3native::platform::vblank_count();
                    if (v < lo || v > hi) return;
                    std::printf("native_play: VBlank %llu: ", static_cast<unsigned long long>(v));
                    b3native::platform::hw_model().debug_state(stdout);
                });
        }
        // mac5: B3NATIVE_PEEK=<hex>[,<hex>...]: these guest words logged (once, then on every change) at each VBlank --
        // a game state machine followed without a debugger (the post-race loading page's picture loader, 0x1EA4070)
        std::vector<std::pair<b3host::gaddr, std::uint32_t>> peeks;
        if (const char* e = std::getenv("B3NATIVE_PEEK")) {
            for (const char* q = e; *q;) {
                char* end = nullptr;
                const unsigned long a = std::strtoul(q, &end, 16);
                if (end == q) break;
                peeks.emplace_back(static_cast<b3host::gaddr>(a & ~3ul), 0xFFFFFFFFu);
                q = *end == ',' ? end + 1 : end;
            }
        }
        // lane-noise: B3NATIVE_GDB_WATCH=<vblank>:<hex>[,<hex>...]: at that VBlank, b3np_gdb_watch (above) for each guest
        // word -- under tools/native/ram_watch.sh every later write that changes the word is logged with its host
        // backtrace (which native function put a value into guest RAM)
        if (const char* e = std::getenv("B3NATIVE_GDB_WATCH")) {
            unsigned long long at = 0;
            std::vector<b3host::gaddr> ws;
            const char* c = std::strchr(e, ':');
            if (c) {
                at = std::strtoull(e, nullptr, 10);
                for (const char* q = c + 1; *q;) {
                    char* end = nullptr;
                    const unsigned long a = std::strtoul(q, &end, 16);
                    if (end == q) break;
                    ws.push_back(static_cast<b3host::gaddr>(a));
                    q = *end == ',' ? end + 1 : end;
                }
                b3native::platform::add_vblank_hook([at, ws] {
                    if (b3native::platform::vblank_count() != at) return;
                    for (b3host::gaddr a : ws) b3np_gdb_watch(b3host::host_pointer(a, 4), a);
                });
            }
        }
        if (!peeks.empty())
            b3native::platform::add_vblank_hook([&peeks] {
                const std::uint64_t v = b3native::platform::vblank_count();
                for (auto& [a, last] : peeks) {
                    const std::uint32_t w = b3host::load<std::uint32_t>(a);
                    if (w == last && v > 1) continue;
                    std::printf("native_play: VBlank %llu: peek %08X = %08X\n", static_cast<unsigned long long>(v), a, w);
                    last = w;
                }
            });
        b3native::platform::add_vblank_hook([&] {
            const std::uint64_t v = b3native::platform::vblank_count();
            // perf3: the game's time between two hooks (emulation: EE, VU, the game's code) and the pacing wait
            const bool perf = r.perf_add && r.perf_on && r.perf_on();
            if (perf && perf_prev_out) r.perf_add(0, std::uint64_t(ns_of(clock::now()) - perf_prev_out));
            // replay1: the replay ends where its recording did (whose last VBlank's hook ran in full); the checkpoints; the
            // live settings for this VBlank
            if (rplay && v > replay_in->vblanks) { end_reason = "the replay ended"; throw PlayEnd(end_reason); }
            if ((rrec || rplay) && v % check_every == 0) {
                const std::uint64_t hash = replay_ram_hash();
                if (rrec) rrec->check(v, hash);
                else if (const auto want = rplay->check(v)) {
                    ++checks_seen;
                    if (*want == hash) ++checks_ok;
                    else if (!replay_parted) {
                        replay_parted = v;
                        std::printf("native_play: VBlank %llu: replay: parted from the recording here (RAM %016llx, recorded %016llx)\n",
                                    static_cast<unsigned long long>(v), static_cast<unsigned long long>(hash), static_cast<unsigned long long>(*want));
                        std::fflush(stdout);
                    }
                }
            }
            if (rrec) sample_live(v);
            else if (rplay) apply_live(v);
            if (rplay) {   // nowplaying1: the track the recording latched on this VBlank (never the system's)
                b3nowplaying::Track t;
                if (!g_live.now_playing.empty()) b3nowplaying::decode(g_live.now_playing, t);
                now_playing_source.set(t, v);
            } else {
                const bool on = now_playing_env >= 0 ? now_playing_env != 0 : r.other_music_apps && r.other_music_apps() != 0;
                const b3nowplaying::Track& t = now_playing_source.latch(on, v, v < now_playing_from);
                if (rrec && (now_playing_recorded || t.status != b3nowplaying::Status::kNone)) {   // a session without it: no op
                    rrec->setting(v, "now_playing", b3nowplaying::encode(t));
                    now_playing_recorded = true;
                }
                if (r.now_playing && now_playing_source.line_changes() != now_playing_line) {   // the row's "Now:" text
                    now_playing_line = now_playing_source.line_changes();
                    r.now_playing(now_playing_source.line().c_str());
                }
            }
            {   // another music app plays: the game's own music is silent (hold 3 s past short gaps between songs), then back
                static std::uint64_t other_last = 0;
                static bool other_seen = false;
                if (now_playing_source.track().status == b3nowplaying::Status::kPlaying) { other_last = v; other_seen = true; }
                const bool mute = other_seen && v - other_last < 180;
                if (mute != b3native::CustomSoundtrack::game_music_muted())
                    std::printf("native_play: VBlank %llu: game music %s (another music app %s)\n", static_cast<unsigned long long>(v),
                                mute ? "off" : "back", mute ? "plays" : "stopped");
                b3native::CustomSoundtrack::set_game_music_muted(mute);
            }
            if (tuning) tuning->vblank(v);   // tune4: this VBlank's tuning edits (after a replay's entries for it)
            if (code_mods) {   // sdk2b: the code mods' settings change here (recorded), then their on_vblank callbacks
                b3mod::LiveSettings& ls = code_mods->settings();
                if (r.mod_take && !ls.playing()) {   // PC SETTINGS > MODS: "<mod>.<key>=<value>" per change
                    char buf[256];
                    for (int n; (n = r.mod_take(buf, int(sizeof buf))) > 0;) {
                        const std::string c(buf, std::size_t(std::min(n, int(sizeof buf) - 1)));
                        const std::size_t dot = c.find('.'), eq = c.find('=');
                        if (dot != std::string::npos && eq != std::string::npos && dot < eq)
                            ls.request(c.substr(0, dot), c.substr(dot + 1, eq - dot - 1), c.substr(eq + 1));
                    }
                }
                for (const b3mod::SettingSet& s : ls.boundary(v)) {
                    std::printf("native_play: VBlank %llu: mods: setting %s.%s = %s\n", static_cast<unsigned long long>(v), s.mod.c_str(),
                                s.key.c_str(), s.value.c_str());
                    if (rrec) rrec->setting(v, "mod." + s.mod + "." + s.key, s.value);
                    recorder.note_set(b3mod::set_line(s));
                    if (r.mod_value) r.mod_value((s.mod + "." + s.key).c_str(), s.value.c_str());
                    if (!offscreen && !mods_pref.empty() && !ls.playing()) {   // the player's choice, kept for the next start
                        std::string why;
                        if (!b3mod::save_values(b3mod::settings_file(mods_pref, s.mod), s.mod, ls.rows(), &why))
                            std::fprintf(stderr, "native_play: mods: %s\n", why.c_str());
                    }
                }
                code_mods->vblank();
            }
            std::array<unsigned char, 32> port1{};   // replay1: what port 2 was given this VBlank (recorded)
            bool port1_set = false;
            if (v % 600 == 0) {
                const auto now = clock::now();
                const double s = std::chrono::duration<double>(now - rate_mark).count();
                const std::uint64_t fb = fallback ? fallback->total_calls() : 0;
                std::printf("native_play: VBlank %llu: %.1f VBlanks/s over the last 600 (%.2f s wall; %llu fallback calls)\n",
                            static_cast<unsigned long long>(v), s > 0 ? 600.0 / s : 0.0, s,
                            static_cast<unsigned long long>(fb - rate_fb));
                std::fflush(stdout);
                rate_mark = now;
                rate_fb = fb;
            }
            // mac5 (Mac 2026-11-08): the game holds while its window has lost the focus (a PC game's pause; the
            // intro movies had played unseen behind another window). Window mode only; B3NATIVE_RUN_UNFOCUSED=1 keeps
            // it running. Virtual time stops with it, so the game and its sound pick up where they were.
            if (pace && r.focused && !run_unfocused && !r.focused()) {
                std::printf("native_play: VBlank %llu: the window lost the focus; the game holds\n", static_cast<unsigned long long>(v));
                std::fflush(stdout);
                while (!r.focused() && !r.quit_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
                pacer.hold_ended(ns_of(clock::now()));
                rate_mark = clock::now();
                std::printf("native_play: the focus is back; the game goes on\n");
                std::fflush(stdout);
            }
            // fastload1: the loading screen (fast_load.h: the page manager's current page, read only) and FAST LOADING
            const bool loading = b3native::loading_page_up(b3host::load<std::uint32_t>(b3native::kMenuCurrentPage));
            if (loading != load_up) {
                const auto now = clock::now();
                if (loading) {
                    load_from = v;
                    load_wall_from = now;
                    load_audible_from = sink.load_audible;
                    // (its look: D_01E900B0, fast_load.h kLoadingKind: 0 boot, 2 into a race, 1 / 3 the LOADING strip)
                    std::printf("native_play: VBlank %llu: loading screen up (kind %u)\n", static_cast<unsigned long long>(v),
                                b3host::load<std::uint32_t>(b3native::kLoadingKind));
                } else {
                    const double w = std::chrono::duration<double>(now - load_wall_from).count();
                    ++loads;
                    load_vblanks += v - load_from;
                    load_wall += w;
                    std::printf("native_play: VBlank %llu: loading screen gone after %llu VBlanks (%.2f s at %.2f Hz) in %.2f s wall%s; "
                                "its sound: %llu frames audible\n",
                                static_cast<unsigned long long>(v), static_cast<unsigned long long>(v - load_from),
                                double(v - load_from) / hz, hz, w, fast_load.fast() ? " (fast loading)" : !pace ? " (unpaced run)" : "",
                                static_cast<unsigned long long>(sink.load_audible - load_audible_from));
                }
                std::fflush(stdout);
                load_up = loading;
                sink.loading = loading;
            }
            const bool fast_wanted = fast_loading_arg >= 0 ? fast_loading_arg != 0 : r.fast_loading && r.fast_loading() != 0;
            if (!fast_setting_known || fast_wanted != fast_setting) {
                fast_setting_known = true;
                fast_setting = fast_wanted;
                std::printf("native_play: VBlank %llu: fast loading %s%s\n", static_cast<unsigned long long>(v), fast_wanted ? "on" : "off",
                            fast_wanted && !pace ? " (no effect: the run is unpaced)" : "");
                std::fflush(stdout);
            }
            // (this VBlank's mix: the platform's RW Audio hook ran before this one)
            const bool quiet = sink.last_peak < b3native::kQuietPeak;
            sink.last_peak = 0;
            // autosave1 (save_screens.h): the AUTOSAVE page is up and SAVE SCREENS is Hide: the display holds the page before
            // it (hold_probe) and a paced run goes through it as through a fast load (whatever FAST LOADING says)
            const bool hide_wanted = save_screens_arg >= 0 ? save_screens_arg != 0 : r.hide_save_screens && r.hide_save_screens() != 0;
            g_hide_save_screens = hide_wanted;
            const bool autosave = hide_wanted && b3native::autosave_page_up(b3host::load<std::uint32_t>(b3native::kMenuCurrentPage));
            if (page_log) {   // B3NATIVE_PAGE_LOG=1: each change of the menu's current page (autosave1: which page is which)
                const std::uint32_t cur = b3host::load<std::uint32_t>(b3native::kMenuCurrentPage);
                if (cur != page_logged) {
                    page_logged = cur;
                    std::printf("native_play: VBlank %llu: menu page %08X\n", static_cast<unsigned long long>(v), cur);
                    std::fflush(stdout);
                }
            }
            if (autosave != autosave_up) {
                autosave_up = autosave;
                if (autosave) autosave_from = v;
                std::printf("native_play: VBlank %llu: AUTOSAVE page %s%s\n", static_cast<unsigned long long>(v), autosave ? "up (hidden)" : "gone after ",
                            autosave ? "" : (std::to_string(v - autosave_from) + " VBlanks").c_str());
                std::fflush(stdout);
            }
            const b3native::FastLoad::Step fl =
                fast_load.step(pace && !(pace_from && v < pace_from) && (loading ? fast_wanted : autosave), loading || autosave, quiet);
            if (fl.began) span_is_load = loading;
            // loadlag1 (fast_load.h scene_hold_ms): the load is over but the renderer may still be building the race's
            // scene (its route came ~0.2-0.4 s before this VBlank, the build takes longer): the game waits here, before
            // its first race VBlank, so that the race is not heard under the loading screen. Wall time only (as the
            // pacer's own sleep): the same VBlanks with the same state; the load's sound is muted until this VBlank.
            if (fl.ended && !loading && span_is_load && r.scene_wait) {
                const double spent = std::chrono::duration<double>(clock::now() - load_wall_from).count();
                const int cap = b3native::scene_hold_ms(double(v - load_from) / hz, spent);
                const int waited = r.scene_wait(cap);
                std::printf("native_play: VBlank %llu: fast loading: %d ms waiting for the renderer's race scene (at most %d ms)\n",
                            static_cast<unsigned long long>(v), waited, cap);
                std::fflush(stdout);
            }
            if (fl.began || fl.ended) {
                b3gv_native::set_unstamped(fl.fast);   // the load's ticks carry no latch stamp
                if (r.fast_forward) r.fast_forward(fl.fast ? 1 : 0);   // the display: the newest frame
                sink.gate.set_muted(fl.fast);          // its sound muted (a 1-VBlank fade each way)
            }
            // play1's pacing (fast_load.h Pacer): this VBlank waits for its wall-clock time; fastload1: not while loading,
            // and the first VBlank after a load re-anchors (no catch-up). --pace-from: unpaced before N.
            if (pace) {
                if (perf) perf_pace_from = ns_of(clock::now());
                const std::int64_t due_ns = pacer.vblank(ns_of(clock::now()), pace_from && v < pace_from, pace_from && pacer.paced == 0, fl);
                if (due_ns != b3native::Pacer::kNow) {
                    const auto due = clock::time_point(std::chrono::duration_cast<clock::duration>(std::chrono::nanoseconds(due_ns)));
#ifdef _WIN32
                    // mac5 (Windows 2026-11-09: the car shook back and forth in a race): Windows' sleeps end on its
                    // timer ticks (1 ms with timeBeginPeriod, else 15.6 ms), so the VBlanks came in bursts and the
                    // renderer's prediction from their times overshot and pulled back. Sleep to ~2 ms before, then
                    // yield to the exact time.
                    if (due - clock::now() > std::chrono::milliseconds(2)) std::this_thread::sleep_until(due - std::chrono::milliseconds(2));
                    while (clock::now() < due) std::this_thread::yield();
#else
                    std::this_thread::sleep_until(due);
#endif
                }
            }
            if (perf) {
                const std::int64_t t = ns_of(clock::now());
                if (pace) r.perf_add(1, std::uint64_t(t - perf_pace_from));
                perf_prev_out = t;
            }
            while (next_key < script.size() && script[next_key].vblank <= v) {
                r.pad_key(script[next_key].scancode, script[next_key].down);
                ++next_key;
            }
            for (; next_key2 < script2.size() && script2[next_key2].vblank <= v; ++next_key2)   // fe1
                if (script2[next_key2].scancode > 0 && script2[next_key2].scancode < 512)
                    pad2.key(static_cast<std::uint16_t>(script2[next_key2].scancode), script2[next_key2].down != 0);
            for (; next_mouse < mouse_events.size() && mouse_events[next_mouse].vblank <= v; ++next_mouse) {   // mac5
                const MouseEvent& m = mouse_events[next_mouse];
                if (r.pad_mouse) r.pad_mouse(m.kind, m.x, m.y, m.n);
                std::printf("native_play: VBlank %llu: scripted mouse %s %.3f %.3f %d\n", static_cast<unsigned long long>(v),
                            m.kind == 0 ? "move" : m.kind == 1 ? "down" : m.kind == 2 ? "up" : "wheel", double(m.x), double(m.y), m.n);
            }
            for (; next_text < text_events.size() && text_events[next_text].vblank <= v; ++next_text) {
                if (r.pad_text) r.pad_text(text_events[next_text].text.c_str());
                std::printf("native_play: VBlank %llu: scripted text (%zu characters)\n", static_cast<unsigned long long>(v), text_events[next_text].text.size());
            }
            // mac4: the key map's context: driving while the game reads the player's accelerator, i.e. calls
            // CB3ControllerMapping::GetAccelerator (0x12FA40 [C]: only from CB3PlayerCarPhysics::GetDrivingControls
            // 0x1BD020 and GetCrashingDrivingControls -- not in menus, not while paused), within the last 4 VBlanks
            // (the crash ticks run at 30 Hz). (CB3Game::IsSimulationPaused's bytes were tried first: set from VBlank 84
            // on, the front end included.)
            {
                const std::uint64_t calls = g_accel_calls.load(std::memory_order_relaxed);
                if (calls != accel_seen) { accel_seen = calls; accel_vblank = v; }
                const bool driving = accel_seen != 0 && v - accel_vblank < 4;
                if (pad_driving) ++drive_vblanks;
                if (driving && !pad_driving) ++drive_starts;   // replay1: the file's notes
                if (driving != pad_driving) {
                    pad_driving = driving;
                    if (r.pad_context) r.pad_context(driving ? 1 : 0);
                    std::printf("native_play: VBlank %llu: keys: %s\n", static_cast<unsigned long long>(v), driving ? "driving" : "menus");
                    std::fflush(stdout);
                }
            }
            {   // carsel1: the row (live, from the window's page) and the car pages (the game's current page; read only)
                const bool on = r.series_shoulders && r.series_shoulders() != 0;
                const bool up = b3native::series_page_up(b3host::load<std::uint32_t>(b3native::kMenuCurrentPage));
                if (on != series_on) {
                    std::printf("native_play: VBlank %llu: car series on L1 / R1 %s\n", static_cast<unsigned long long>(v), on ? "on" : "off");
                    std::fflush(stdout);
                }
                if (on && up != series_page) {
                    std::printf("native_play: VBlank %llu: car series: %s page %s\n", static_cast<unsigned long long>(v),
                                (b3host::load<std::uint32_t>(b3native::kMenuCurrentPage) & 0x01FFFFFFu) == b3native::kGaragePage ? "garage" : "car select",
                                up ? "up" : "gone");
                    std::fflush(stdout);
                }
                series_on = on;
                series_page = up;
                if (on && up) {   // the carousel's series and car, when they change (the log's proof of a series change)
                    const std::uint32_t now = b3host::load<std::uint32_t>(b3native::kCarouselSeries) << 16 |
                                              (b3host::load<std::uint32_t>(b3native::kCarouselCar) & 0xFFFFu);
                    if (now != series_shown) {
                        series_shown = now;
                        std::printf("native_play: VBlank %llu: car series: series %u car %u\n", static_cast<unsigned long long>(v), now >> 16, now & 0xFFFFu);
                        std::fflush(stdout);
                    }
                } else {
                    series_shown = 0xFFFFFFFFu;
                }
            }
            if (!keys2.empty() && !rplay) {   // fe1: port 2's report for this VBlank (the same context as port 1's keys)
                pad2.set_driving(pad_driving);
                pad2.step(static_cast<float>(1.0 / hz));
                auto s2 = pad2.sample();
                series_apply(series2, s2, 1, v);   // carsel1
                b3native::platform::sif::set_pad_sample(1, s2.data());
                std::memcpy(port1.data(), s2.data(), 32);
                port1_set = true;
            }
            // photo mode (src/b3replay/photomode.h): the race's pause menu is up while its page's Update runs (each
            // frame it shows), within the last 8 VBlanks; the renderer allows photo mode only then. Read only.
            if (pc_menu && r.set_paused) {
                const std::uint64_t calls = pc_menu->pause_updates();
                if (calls != pause_seen) { pause_seen = calls; pause_vblank = v; }
                const bool paused = pause_seen != 0 && v - pause_vblank < 8;
                if (paused != game_paused) {
                    game_paused = paused;
                    r.set_paused(paused ? 1 : 0);
                    std::printf("native_play: VBlank %llu: pause menu %s\n", static_cast<unsigned long long>(v), paused ? "up" : "gone");
                    std::fflush(stdout);
                }
            }
            std::array<unsigned char, 32> s{};
            bool have = r.pad_sample(0, s.data(), static_cast<float>(1.0 / hz)) != 0;   // (also steps the key ramp)
            if (!replay_pad.empty()) { s = replay.at(v); have = true; }
            {   // rumble1: the acknowledging mode for the VBlanks from here, then the motors to the window's pads
                const bool want = !replay_pad.empty() ? replay.rumble_at(v)
                                  : rplay              ? replay_rumble
                                  : rumble_forced >= 0 ? rumble_forced == 1
                                                       : (r.rumble_mode && r.rumble_mode() != 0);
                if (rrec) rrec->setting(v, "rumble", want ? "1" : "0");   // replay1: the game sees the answers (kept on change)
                if (want != rumble_on) {
                    rumble_on = want;
                    rumble_ever = rumble_ever || want;
                    b3native::platform::sif::set_actuator_ack(want);
                    recorder.note_rumble(v, want);
                    std::printf("native_play: VBlank %llu: rumble %s\n", static_cast<unsigned long long>(v),
                                want ? "on (the pads' actuator requests acknowledged)" : "off (actuator requests left pending)");
                    std::fflush(stdout);
                }
                if (rumble_on && r.rumble)
                    for (int port = 0; port < 2; ++port) {
                        unsigned char small = 0, large = 0;
                        b3native::platform::sif::pad_motors(port, small, large);
                        r.rumble(port, small, large);
                    }
            }
            if (rplay) {   // replay1: the recorded ops of this VBlank; the window's pad only pauses (Start, an edge)
                if (viewer_pause) {
                    const bool start = have && !(s[0] & 0x08);   // Start: report byte 0 bit 3, active low (b3live pad_press)
                    if (start && !viewer_start_held) {
                        g_replay_paused.store(true);
                        std::printf("native_play: VBlank %llu: replay paused (Start again to go on)\n", static_cast<unsigned long long>(v));
                        std::fflush(stdout);
                        bool held = true;
                        while (!r.quit_requested()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(20));
                            std::array<unsigned char, 32> w{};
                            const bool down = r.pad_sample(0, w.data(), 0.02f) != 0 && !(w[0] & 0x08);
                            if (down && !held) break;
                            held = down;
                        }
                        pacer.hold_ended(ns_of(clock::now()));   // the paused time does not count (as a focus hold)
                        rate_mark = clock::now();
                        g_replay_paused.store(false);
                        std::printf("native_play: replay goes on\n");
                        std::fflush(stdout);
                    }
                    viewer_start_held = start;
                }
                for (const auto& [port, on] : rplay->step(v)) {
                    b3native::platform::sif::set_pad_connected(int(port), on);
                    std::printf("native_play: VBlank %llu: port %u: a controller %s (replay)\n", static_cast<unsigned long long>(v), port + 1,
                                on ? "plugged in" : "pulled out");
                }
                if (rplay->present(1)) b3native::platform::sif::set_pad_sample(1, rplay->report(1).data());
                have = rplay->present(0);
                std::memcpy(s.data(), (have ? rplay->report(0) : neutral_report).data(), 32);
            }
            if (pc_menu && pc_menu->take_photo_request()) {   // photorow1: Confirm on the pause menu's PHOTO MODE row
                const bool entered = replay_pad.empty() && !rplay && r.photo_enter && r.photo_enter() != 0;
                std::printf("native_play: VBlank %llu: PHOTO MODE row %s\n", static_cast<unsigned long long>(v),
                            entered ? "confirmed: photo mode on" : "confirmed (photo mode did not start here)");
                std::fflush(stdout);
            }
            if (pc_menu && pc_menu->take_open_request()) {
                // a replay feeds what the game saw (neutral while the page was open): the page itself is not reopened
                const bool opened = replay_pad.empty() && !rplay && r.settings_open && r.settings_open() != 0;
                if (opened) page_hold.opened();
                std::printf("native_play: VBlank %llu: SETTINGS %s\n", static_cast<unsigned long long>(v),
                            opened ? "opened" : replay_pad.empty() && !rplay ? "(this renderer has no page)" : "(replay: not opened)");
                std::fflush(stdout);
            }
            // withheld from the VBlank of the request until the page has closed and the pad is released (page_hold.h)
            const bool page_open = replay_pad.empty() && !rplay &&
                                   page_hold.step(r.settings_active && r.settings_active() != 0, have ? s.data() : nullptr);
            if (page_open != page_held) {
                page_held = page_open;
                std::printf("native_play: VBlank %llu: pad %s\n", static_cast<unsigned long long>(v),
                            page_open ? "withheld from the game (PC SETTINGS / photo mode)" : "back to the game");
                std::fflush(stdout);
            }
            if (page_open) { s = neutral; have = true; }
            if (window_port2) {   // pad2: port 2 from the window's second gamepad (neutral while the page / photo mode hold input)
                std::array<unsigned char, 32> s2{};
                const bool p2 = r.pad_sample(1, s2.data(), static_cast<float>(1.0 / hz)) != 0;
                if (p2 != port2_plugged) {
                    port2_plugged = p2;
                    b3native::platform::sif::set_pad_connected(1, p2);
                    if (rrec) rrec->connect(v, 1, p2);
                    if (!p2) {
                        b3native::platform::sif::set_pad_sample(1, neutral.data());
                        port1 = neutral;
                        port1_set = true;
                    }
                    std::printf("native_play: VBlank %llu: port 2: a gamepad %s\n", static_cast<unsigned long long>(v),
                                p2 ? "plugged in (player 2)" : "pulled out");
                    std::fflush(stdout);
                }
                if (p2 && !page_open) series_apply(series2, s2, 1, v);   // carsel1
                if (p2) {
                    b3native::platform::sif::set_pad_sample(1, page_open ? neutral.data() : s2.data());
                    port1 = page_open ? neutral : s2;
                    port1_set = true;
                }
            }
            // carsel1: L1 / R1 on the car pages as Left / Right (the row on; a replay already holds them)
            if (have && replay_pad.empty() && !rplay && !page_open) series_apply(series1, s, 0, v);
            // mac5: the mouse's presses in the menus (none while driving; a replay already holds them), the menus record
            if (menu_mouse) {
                if (rplay) menu_mouse->feed_text(rplay->text(v));   // replay1
                if (const auto* rec = menu_mouse->vblank(v, pad_driving, page_open, have && replay_pad.empty() && !rplay ? s.data() : nullptr))
                    b3gv_native::set_menu_record(*rec);
                if (rrec) rrec->text(v, menu_mouse->taken());
            }
            if (rrec) {   // replay1: what both ports were given this VBlank
                rrec->sample(v, 0, have ? s.data() : nullptr);
                rrec->sample(v, 1, port1_set ? port1.data() : nullptr);
            }
            if (have) {
                recorder.put(v, s);
                if (skip.apply(s.data())) {
                    std::printf("native_play: VBlank %llu: movie skipped (CGtVideoDecoder::Abort on 0x%08X)\n",
                                static_cast<unsigned long long>(v), b3native::MovieSkip::kDecoder);
                    std::fflush(stdout);
                }
                // mac5: the skip hint's opacity for the renderer (drawn only; the report is not changed)
                const std::uint8_t hint = hint_fade.step(skip.skippable(), s.data());
                b3gv_native::set_skip_hint(hint);
                if ((hint != 0) != hint_shown) {
                    hint_shown = hint != 0;
                    std::printf("native_play: VBlank %llu: skip hint %s\n", static_cast<unsigned long long>(v), hint_shown ? "shown" : "hidden");
                    std::fflush(stdout);
                }
                b3native::platform::sif::set_pad_sample(0, s.data());
                ++pad.set;
                if (s != neutral) {
                    if (!pad.non_neutral++) { pad.first_non_neutral = v; pad.first_bytes = s; }
                }
                if (s != pad.last) ++pad.changes;
                pad.last = s;
            }
            if (r.quit_requested()) { end_reason = "the window was closed"; throw PlayEnd(end_reason); }
            if (pc_menu && pc_menu->quit_requested()) { end_reason = "QUIT GAME"; throw PlayEnd(end_reason); }
            if (quit_after > 0 && std::chrono::duration<double>(clock::now() - start).count() >= quit_after) {
                end_reason = "--quit-after";
                throw PlayEnd(end_reason);
            }
        });

        // crt0 as host code (native_boot's order): _InitSys, FlushCache(0), main(argc 0, argv[0] NULL), exit
        std::string outcome;
        bool clean = false;
        // nofallback1: the stop path (above, at stop_text): what the log, the window and the message box say
        auto stop_missing = [&](b3host::gaddr a) {
            const b3host::FunctionInfo* fi = b3host::function_at(a);
            const std::string name = fi ? std::string(fi->name) : b3host::describe_function(a);
            const std::string fn = hex32(a) + " " + name;
            std::printf("native_play: STOPPED: the game called %s, which has no native code in this build (%s). The game "
                        "stops here; this is a bug in the port. A bug report (F10 in the game window, or \"Make a bug report\" "
                        "in the launcher) holds this log\n",
                        (hex32(a) + " " + b3host::describe_function(a)).c_str(),
                        B3_NATIVE_FALLBACK ? "--strict: no fallback" : "and no fallback to the original PS2 code");
            std::fflush(stdout);
            stop_text = "The game stopped: it needed a part of the game that this build has no native code for.\n\nFunction: " + fn +
                        "\n\nThis is a bug in the port, not a problem with your game data or settings. Please make a bug report "
                        "(press \"Make a bug report\" in the launcher: it holds this run's log) and send it with a few words on "
                        "what you were doing.";
            if (b3native::runlog::state().active) stop_text += "\n\nRun log: " + b3native::runlog::state().path;
            if (!run_state_path.empty()) {   // the state the renderer wrote at the start (running), kept as a crash leaves it
                stop_state = b3settings::load_run_state(b3mod::path_utf8(run_state_path));
                stop_state.running = true;
                stop_state_path = run_state_path;
            }
            if (r.game_stopped) r.game_stopped(fn.c_str());   // the window: held with the message until it is closed
        };
        auto run = [&](const char* what, b3host::gaddr fn, b3host::CallArgs a) {
            a.sp = nb->stack_pointer();
            b3host::CallResult res;
            try {
                nb->run(fn, a, res);
                outcome = std::string(what) + ": returned (v0 " + hex32(res.v0.w[0]) + ")";
                return true;
            } catch (const PlayEnd& e) {
                outcome = std::string(what) + ": ended (" + e.what() + ")";
                clean = true;
            } catch (const b3native::platform::VBlankLimit& e) {
                outcome = std::string(what) + ": ended (" + e.what() + ")";
                clean = true;
            } catch (const b3native::platform::NativeExit& e) {
                outcome = std::string(what) + ": program ended (" + e.what() + ")";
                clean = true;
            } catch (const b3host::MissingNative& e) {
                outcome = std::string(what) + ": MISSING " + e.what();
                stop_missing(e.address);
            } catch (const std::exception& e) {
                outcome = std::string(what) + ": ERROR " + e.what();
            }
            return false;
        };
        const bool booted = run("_InitSys", 0x00115108u, args_of({})) && run("FlushCache(0)", 0x0010C0A0u, args_of({0}));
        if (booted) {
            const std::uint32_t argc_word = b3host::load<std::uint32_t>(0x01D9CE80u);
            if (run("main", 0x0012EB30u, args_of({argc_word, 0x01D9CE84u}))) clean = run("exit", 0x001213B8u, args_of({0})) || clean;
        }
        b3native::platform::sif::save_card_flush();   // save1: a save the game made in its last second
        if (code_mods && clean) code_mods->shutdown();   // sdk2b: on_shutdown (a normal end only)
        const double secs = std::chrono::duration<double>(clock::now() - start).count();
        const std::uint64_t vbl = b3native::platform::vblank_count();
        if (rrec) {   // replay1: the session's replay file
            b3native::replay::Replay& out = rrec->replay();
            out.vblanks = vbl;
            out.info.emplace_back("end", end_reason.empty() ? outcome : end_reason);
            out.info.emplace_back("length", b3native::replay::format_length(vbl));
            out.info.emplace_back("driving", std::to_string(drive_starts) + " stretches, " + b3native::replay::format_length(drive_vblanks));
            std::string why;
            if (b3native::replay::write_file(replay_out, out, &why)) {
                std::error_code ec;
                std::printf("native_play: replay: %zu pad ops, %zu typed, %zu settings, %zu checkpoints to VBlank %llu (%s) written to %s (%llu bytes)\n",
                            out.pads.size(), out.text.size(), out.settings.size(), out.checks.size(), static_cast<unsigned long long>(vbl),
                            b3native::replay::format_length(vbl).c_str(), replay_out.string().c_str(),
                            static_cast<unsigned long long>(std::filesystem::file_size(replay_out, ec)));
                if (replay_auto)
                    if (const int gone = b3native::replay::rotate(replay_out.parent_path()); gone > 0)
                        std::printf("native_play: replay: %d older recordings removed (the newest %d kept)\n", gone, b3native::replay::kKeepRecorded);
            } else {
                std::fprintf(stderr, "native_play: replay: not written: %s\n", why.c_str());
            }
        }
        if (rplay)
            std::printf("native_play: replay: %llu of %llu checkpoints matched; %s\n", static_cast<unsigned long long>(checks_ok),
                        static_cast<unsigned long long>(checks_seen),
                        replay_parted ? ("it parted from the recording at VBlank " + std::to_string(replay_parted) + " (" +
                                         b3native::replay::format_length(replay_parted) + ")").c_str()
                                      : "it played as recorded");
        if (recorder.is_open()) {
            std::printf("native_play: pad recording: %llu changes to VBlank %llu (%s)\n",
                        static_cast<unsigned long long>(recorder.changes()), static_cast<unsigned long long>(vbl), record_pad.c_str());
            recorder.close(vbl);
        }
        std::printf("native_play: movie skip %s: %llu movies skipped; %llu movies started, %llu of them menu backgrounds (TRACKS, not skippable)\n",
                    movie_skip_on ? "on" : "off (--no-movie-skip)", static_cast<unsigned long long>(skip.skips()),
                    static_cast<unsigned long long>(skip.prepares()), static_cast<unsigned long long>(skip.exempt_prepares()));
        std::printf("native_play: %s\n", outcome.c_str());
        std::printf("native_play: %llu VBlanks in %.2f s wall (%.2f per s; %s%llu resyncs when behind)\n",
                    static_cast<unsigned long long>(vbl), secs, secs > 0 ? vbl / secs : 0.0, pace ? "" : "unpaced, ",
                    static_cast<unsigned long long>(pacer.resyncs));
        std::printf("native_play: idle skip %s: %llu busy-wait passes jumped to the next event\n", idle_skip ? "on" : "off (--no-idle-skip)",
                    static_cast<unsigned long long>(idle_skips));
        std::printf("native_play: pad: %llu reports set, %llu not neutral, %llu changes",
                    static_cast<unsigned long long>(pad.set), static_cast<unsigned long long>(pad.non_neutral),
                    static_cast<unsigned long long>(pad.changes));
        if (pad.non_neutral) {
            std::printf("; first at VBlank %llu:", static_cast<unsigned long long>(pad.first_non_neutral));
            for (int i = 0; i < 18; ++i) std::printf(" %02X", pad.first_bytes[i]);
        }
        std::printf("\n");
        std::printf("native_play: sound: %llu frames mixed (%.2f s of 48 kHz), %llu audible, peak %d%s\n", static_cast<unsigned long long>(sink.frames),
                    sink.frames / 48000.0, static_cast<unsigned long long>(sink.audible), sink.peak,
                    sink.fn ? "" : "; not played (--no-audio or an older b3render)");
        if (tuning) {   // tune4: the summary; the console stays for the process (the renderer, draining the feed after
                        // the game has ended, may still read it)
            tuning->summary();
            (void)tuning.release();
        }
        if (rumble_ever) {   // rumble1
            std::uint64_t busy = 0;
            const std::uint64_t acked = b3native::platform::sif::actuator_requests(&busy);
            std::printf("native_play: rumble: %llu actuator requests acknowledged, %llu found the channel busy\n",
                        static_cast<unsigned long long>(acked), static_cast<unsigned long long>(busy));
        }
        // fastload1: the loading screens of the run (tools/native/fast_load.h)
        std::printf("native_play: loading screens: %llu ended, %llu VBlanks (%.2f s at %.2f Hz) in %.2f s wall; fast loading %s: "
                    "%llu spans, %llu VBlanks unpaced, %llu frames of their sound muted; the loading screens' sound: %llu of %llu frames "
                    "audible, peak %d\n",
                    static_cast<unsigned long long>(loads), static_cast<unsigned long long>(load_vblanks), double(load_vblanks) / hz, hz,
                    load_wall, fast_setting ? (pace ? "on" : "on (unpaced run)") : "off", static_cast<unsigned long long>(fast_load.spans()),
                    static_cast<unsigned long long>(fast_load.vblanks()), static_cast<unsigned long long>(sink.gate.dropped()),
                    static_cast<unsigned long long>(sink.load_audible), static_cast<unsigned long long>(sink.load_frames), sink.load_peak);
        b3native::platform::vu0_summary(stdout, "native_play: ");   // vu0n: generated vs interpreted (race_check reads it)
#if B3_HW_DEV
        g_path3_check.report(stdout, "native_play: ");   // dev builds: the PATH3 rules held over the run
#endif
        if (movie_standin) movie_standin->report(stdout);
        if (retail_for) retail_for->report(stdout);
        if (diff_check) diff_check->report(stdout);
        if (poison_check) poison_check->report(stdout);
        if (menu_mouse)
            std::printf("native_play: menu mouse: %llu presses, %llu characters typed\n", static_cast<unsigned long long>(menu_mouse->presses()),
                        static_cast<unsigned long long>(menu_mouse->typed()));
        if (return_standin) return_standin->report(stdout);
        if (fallback) fallback->report(stdout);   // every function that ran retail, its calls and first chain: the to-do list
        else if (!B3_NATIVE_FALLBACK) std::printf("fallback: none (compiled out)\n");   // nofallback1: race_check reads it
        b3gv_native::set_frame_hook(nullptr);   // sdk2b: the code mods' frame callbacks end with the game
        b3gv_native::finish();   // the capture closes the renderer if it opened it (at a poll): the feed drains;
        return clean ? 0 : 1;    // else close_feed does (no poll yet; a no-op when closed already)
    };
    struct Ctx { decltype(play_game)* fn; int* rc; } ctx{&play_game, &game_rc};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, std::size_t{16} << 20);
#ifdef __APPLE__
    // mac5 (Mac 2026-11-08: races at 38-55 VBlanks/s, the sound choppy with every hitch): a secondary thread
    // gets the default QoS class, which macOS may run on the efficiency cores; the game thread is the program's
    // real-time work, so it gets the main thread's class (user-interactive: the performance cores)
    pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    pthread_t worker;
    if (pthread_create(&worker, &attr, [](void* p) -> void* {
            auto* c = static_cast<Ctx*>(p);
            try { *c->rc = (*c->fn)(); } catch (const std::exception& e) { std::fprintf(stderr, "native_play: %s\n", e.what()); *c->rc = 1; }
            return nullptr;
        }, &ctx) != 0) { std::fprintf(stderr, "native_play: cannot start the game thread\n"); return 2; }
    pthread_attr_destroy(&attr);
    // mac5: the stall watchdog (tools/native/stall_dump.h): the VBlank count still for 15 s while the window has the
    // focus (the focus hold stops it on purpose) -> every thread's stack, once (B3NATIVE_STALL_S=<s>, 0: off)
    std::atomic<bool> watch_stop{false};
    const double stall_s = [] { const char* e = std::getenv("B3NATIVE_STALL_S"); return e ? std::atof(e) : 15.0; }();
    std::thread watchdog([&] {
        std::uint64_t last = 0;
        auto since = std::chrono::steady_clock::now();
        bool dumped = false;
        while (!watch_stop.load() && stall_s > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const std::uint64_t v = b3native::platform::vblank_count();
            const bool held = (r.focused && !r.focused()) || g_replay_paused.load();
            const bool page = r.settings_active && r.settings_active() != 0;
            if (v != last || held || page || v == 0) { last = v; since = std::chrono::steady_clock::now(); continue; }
            if (!dumped && std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count() >= stall_s) {
                dumped = true;
                char why[96];
                std::snprintf(why, sizeof why, "the game stopped at VBlank %llu for %.0f s", static_cast<unsigned long long>(v), stall_s);
                b3native::stall_dump(why);
                b3native::platform::hw_model().debug_state(stdout);   // the DMA state the game may be waiting on
                std::fflush(stdout);
            }
        }
    });
    const int render_rc = r.run_main();   // returns when the window is closed and the feed has ended
    pthread_join(worker, nullptr);
    watch_stop.store(true);
    watchdog.join();
    std::printf("native_play: renderer ended (rc %d), game rc %d\n", render_rc, game_rc);
    if (!stop_state_path.empty()) {   // nofallback1: after the renderer's clean end -- this run did not end cleanly
        const bool ok = b3settings::save_run_state(b3mod::path_utf8(stop_state_path), stop_state);
        std::printf("native_play: run state %s: %s (the next start counts it as a crash; %d before it in a row)\n",
                    b3mod::path_utf8(stop_state_path).c_str(), ok ? "this run marked as not ended cleanly" : "NOT written",
                    stop_state.unclean);
    }
    if (!stop_text.empty() && !offscreen) tell_user("the game stopped", stop_text, exe_dir);
    if (!replay_card.empty()) {   // replay1: the throwaway card
        std::error_code ec;
        std::filesystem::remove_all(replay_card, ec);
    }
    if (replay_parted && !offscreen)
        tell_user("replay", "This replay did not play as it was recorded from " + b3native::replay::format_length(replay_parted) +
                                " on: this build or this PC gives the game something the recording's did not. What came after "
                                "that point was not the recorded session.", exe_dir);
    return game_rc;
}
