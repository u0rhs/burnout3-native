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
//                         is up (the same VBlanks, sooner). Default: PC SETTINGS > GAME > STARTING AND LOADING > FAST LOADING (b3r_fast_loading;
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
//                         RAM checkpoints. A window run does it by itself with PC SETTINGS > GAME > MODES > RECORD REPLAYS
//                         On (<user data folder>/replays/<date-time>.b3replay, the newest 20 kept)
//   --replay FILE         replay1: play a replay file again (a bare name: one in <user data folder>/replays): the
//                         recorded pads, card (a throwaway copy: the player's own saves are not touched), clock, options
//                         and settings; refused for another disc or port release. The window's controls are ignored but
//                         Start (Esc), which pauses the replay; it ends where the recording did. The checkpoints say
//                         whether it stayed on the recorded timeline
//   --no-movie-skip       mac4: Start / Cross do not end movies (default: they do, tools/native/movie_skip.h; mac5:
//                         while one can be, a corner hint shows the button: PC SETTINGS > GAME > INTERFACE > VIDEO SKIP HINT)
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
//                         PC SETTINGS > MODS > DEVELOPER MODS says (pc_settings.ini developer_mods: the one switch,
//                         which a window run reads and the launcher's Mods page toggles too; an offscreen run: off).
//                         On: a code mod with [code] platform = yes may hook the platform tier, and the tuning console
//                         is on (unless --tuning off)
//   --tuning on|off       tune4 (the tuning console, tools/native/tuning_console.h, docs/modding/tuning.md): record the
//                         game's tuning registry and give the console (PC SETTINGS > DEVELOPER, F9) to the window
//                         (env B3NATIVE_TUNING=0|1). Default: Developer mods (above: --developer-mods, else in a
//                         window run PC SETTINGS > MODS > DEVELOPER MODS; off by default). Off, nothing is
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
#include "sampler.h"   // slowcpu (B3_SAMPLE)
#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "b3port/game_program.h"   // ghvariant: the programs accepted
#include "b3asset/discfs.h"
#include "b3asset/language.h"   // 1.0.1: language packs
#include "audio_sink.h"
#include "gameview_native.h"
#include "crash_report.h"
#include "explore_standins.h"
#include "fast_load.h"
#include "save_screens.h"
#include "car_select.h"   // carsel1: the car series on L1 / R1
#include "race_time.h"    // todrace: DAY / NIGHT per race on the track pages
#include "paint_select.h" // paintsel: the car paint panel on SELECT CAR, the game's colour choice there
#include "carousel_models.h"   // paint3: the carousel cars whose picture is not their own (a resolver names them)
#include "input_bindings.h"   // 1.3.0: kRaceTimeThisRelease (night mode held back, the DAY / NIGHT choice with it)
using b3native::input::kRaceTimeThisRelease;
#include "b3settings/settings.h"   // extras: the EXTRAS word's bits
// raceopts: the TIME OF DAY row on SELECT OPTIONS (race_options.h) is held back with it; B3_RACE_TIME_ROW=1 shows it (tests)
namespace { bool extra_on(std::uint32_t bit, const char* env); }   // extras (below, in the file's unnamed namespace)
// extras / condrows: EXTRAS > EXPERIMENTAL > NIGHT gives the TIME OF DAY row (Day / Night); B3_RACE_TIME_ROW=1 too (tests)
inline bool race_time_row_wanted() {
    const char* e = std::getenv("B3_RACE_TIME_ROW");
    return kRaceTimeThisRelease || (e && e[0] == '1' && !e[1]) || extra_on(b3settings::kExtraNight, nullptr);
}
// condrows: EXTRAS > EXPERIMENTAL > RAIN gives the WEATHER row (Clear / Rain; race_time.h kWeatherThisRelease);
// B3_WEATHER_ROW=1 too (tests). With both rows, Night + Rain is night rain
inline bool race_weather_row_wanted() {
    const char* e = std::getenv("B3_WEATHER_ROW");
    return b3native::kWeatherThisRelease || (e && e[0] == '1' && !e[1]) || extra_on(b3settings::kExtraRain, nullptr);
}
#include "diff_check.h"
#include "poison_check.h"
#include "pc_menu.h"
#include "game_options.h"   // gameopts: MIRROR MODE, CRASHBREAKER IN EVERY MODE (PC SETTINGS > GAME > GAMEPLAY)
#include "party_modes.h"    // party1: GAME > PARTY MODES
#include "single_eliminator.h"   // elim1: ELIMINATOR in SINGLE EVENT's SELECT MODE
#include "../../src/b3replay/records.h"   // elim1: the record word's folder (the log)
#include "race_options.h"   // raceopts: TRAFFIC / LAPS / RIVALS rows on SELECT OPTIONS
#include "traffic_check.h"   // trafficcheck: TRAFFIC CHECKING (PC SETTINGS > GAME > GAMEPLAY)
#include "gearbox.h"      // gearbox: TRANSMISSION Manual, the game's own manual gearbox
#include "game_mods.h"      // options3: settings that change what the game does (DEFORMATION)
#include "heavy_race.h"   // heavyseries: HEAVYWEIGHTS on SELECT CAR for Single Event races
#include "title_fast.h"   // titlefast: the title's PRESS START prompt and Start without the game's two-second wait
#include "settings_back.h"   // settingsback: Back on the game's SETTINGS pages keeps the changes
#include "boost_start.h"   // booststart: the game's BOOST START on a standing start
#include "traffic_cars.h"   // trafficseries: TRAFFIC / TRAFFIC SPECIAL on SELECT CAR, traffic vehicles driven as race cars
#include "extra_cars.h"     // customseries: the CUSTOM series on SELECT CAR, players' car mods as extra cars
#include "card_silence.h"
#include "more_rivals.h"
#include "date_format.h"
#include "replay_view.h"   // replayview: RACE REPLAY, the game's own replay in every offline mode
#include "relaunch.h"   // nextstart: APPLY AND RESTART
#include "wreck_slots.h"  // wreckguard: the 64 crashed-traffic slots bounded
#include "speed_units.h"
#include "save_stats.h"   // gamemods1: START AT reads which profiles the save holds
#include "boot_boost.h"
#include "game_limits.h"
#include "car_viewer.h"   // gamemods1: the CAR VIEWER from YOUR GARAGE
#include "b3gameview/gameview.h"   // gamemods1: b3gv::set_car_viewer_background   // gamemods1: IDLE PAUSE, END VERY LONG EVENTS   // gamemods1: START AT (the title or the main menu with a profile, without the intro films)
#include "takedown_pitch.h"   // hudmods: TAKEDOWN STREAK PITCH (the takedown sound's pitch by the streak since the last crash)
#include "training_voice.h"   // 1.3.0 VOICES
#include "online_message.h"
#include "custom_soundtrack.h"
#include "lane_pair_fix.h"
#include "crash_ram_fix.h"
#include "car_stats.h"         // carstats1: GRIP / ACCELERATION on SELECT CAR
#include "turn_signals.h"      // blinkers1: the racers' indicators before bends
#include "road_rage_parts.h"   // rrparts1: every loose part can come off in Road Rage
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
#include "pad_context.h"   // padskip
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
#include "replay_stream.h"   // media1: the replay on disk at every checkpoint while it records
#include "replay_card.h"     // media1: the throwaway replay card removed on every way out
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
#include <cerrno>
#include <csignal>
#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#ifdef __linux__
#include <sys/syscall.h>   // the replay card's signal handler: getdents64 (async-signal-safe, no allocation)
#endif
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "b3port/fileio.h"   // fsrobust: UTF-8 paths, arguments and folders
#include "b3port/user_dir.h"   // portable mode (1.3.0)
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
    int (*restart_requested)() = nullptr;   // nextstart, optional: b3r_restart_requested (PC SETTINGS' APPLY AND RESTART)
    int (*close)(std::uint32_t) = nullptr;
    int (*perf_on)() = nullptr;   // perf3, optional: b3r_perf_on / b3r_perf_add (B3_PERF_LOG's game-thread stages)
    void (*perf_add)(int, std::uint64_t) = nullptr;
    void (*audio_push)(const std::int16_t*, std::size_t) = nullptr;   // optional (an older b3render: silent)
    unsigned long long (*audio_late)() = nullptr;   // stutter, optional: the output queue's dry runs so far (b3r_audio_late)
    void (*audio_quiet)(int) = nullptr;             // loadaudio, optional: the sound silent on purpose (b3r_audio_quiet)
    int (*feed_full)() = nullptr;                   // trafmem, optional: the shot runner's hold is full (b3r_feed_full)
    unsigned (*feed_hold)() = nullptr;              // trafmem, optional: the hold's poll (b3r_feed_hold)
    void (*pad_context)(int) = nullptr;   // mac4, optional (an older b3render: the menu bindings only)
    int (*settings_open)() = nullptr;     // mac5, optional: open the PC SETTINGS page (1: opened)
    int (*date_format)() = nullptr;       // mac5, optional: b3r_date_format (an older b3render: the game's month/day/year)
    int (*speed_units)() = nullptr;       // units1, optional: b3r_speed_units (an older b3render: the game's miles)
    int (*boot_to)() = nullptr;           // gamemods1, optional: b3r_boot_to (an older b3render: the game's start)
    int (*idle_pause)() = nullptr;        // gamemods1, optional: b3r_idle_pause (an older b3render: the game's)
    int (*long_event_end)() = nullptr;    // gamemods1, optional: b3r_long_event_end (an older b3render: the game's)
    int (*takedown_pitch)() = nullptr;    // hudmods, optional: b3r_takedown_pitch (an older b3render: the game's pitch)
    int (*music)(char*, int) = nullptr;   // optional: b3r_music, the playlist mode and music folder (custom_soundtrack.h)
    int (*music_volume)() = nullptr;      // optional: b3r_music_volume, CUSTOM MUSIC VOLUME in percent (musicvol1)
    int (*music_loudness)() = nullptr;    // musicloud, optional: b3r_music_loudness (an older b3render: Even out)
    int (*other_music_apps)() = nullptr;  // nowplaying1, optional: b3r_other_music_apps (now_playing_popup.h; an older b3render: off)
    int (*sound_output)() = nullptr;      // audioq, optional: b3r_sound_output (SOUND OUTPUT: 0 Clean, 1 Original; an older b3render: Clean)
    void (*now_playing)(const char*) = nullptr;   // nowplaying1, optional: b3r_now_playing, what is detected (the row's text)
    int (*settings_active)() = nullptr;   // mac5, optional: 1 while the page is open (the game then sees a neutral pad)
    int (*record_replays)() = nullptr;    // replay1, optional: PC SETTINGS > GAME > MODES > RECORD REPLAYS
    int (*photo_row)() = nullptr;         // photorow1, optional: b3r_photo_row (an older b3render: no row)
    int (*game_mods)() = nullptr;         // options3, optional: b3r_game_mods (game_mods.h; an older b3render: none)
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
    int (*extras)() = nullptr;            // extras, optional: b3r_extras (PC SETTINGS > EXTRAS as one word; an older b3render: 0)
    int (*traffic_check)() = nullptr;     // trafficcheck, optional: b3r_traffic_check (an older b3render: off)
    int (*game_options)() = nullptr;      // gameopts, optional: b3r_game_options (an older b3render: none)
    // carstats1 / blinkers1 / rrparts1, optional (an older b3render: the game's): CAR STATS, TURN SIGNALS, ROAD RAGE PARTS (car_stats.h, turn_signals.h,
    // road_rage_parts.h)
    int (*car_stats)() = nullptr;
    int (*turn_signals)() = nullptr;
    int (*road_rage_parts)() = nullptr;
    int (*party_keys)() = nullptr;        // party1, optional: b3r_party_keys (an older b3render: no buttons)
    // todrace, optional (an older b3render: no presses, the label never drawn): a track page (race_time.h) is up, and the
    // TIME OF DAY presses the window took there since the last call (race_time.h)
    void (*race_time_page)(int) = nullptr;
    int (*race_time_presses)() = nullptr;
    int (*night_setting)() = nullptr;   // raceopts, optional: PC SETTINGS > TIME OF DAY (1 Night)
    int (*weather_setting)() = nullptr; // weather, optional: PC SETTINGS > WEATHER (1 Rain)
    void (*effects_volume)(int) = nullptr;   // weather, optional: the game's EFFECTS volume (0..100) for the rain's sound
    // paintsel, optional (an older b3render: no panel, the rule not applied): SELECT CAR takes input for a player (-1
    // none), and the game's colour changed there (paint_select.h)
    void (*paint_page)(int) = nullptr;
    void (*paint_game)(int) = nullptr;
    int (*paint_presses)() = nullptr;
    int (*replay_presses)() = nullptr;   // replayview, optional: b3r_replay_presses (REPLAY presses since the last call)
    void (*set_replay_showing)(int) = nullptr;   // replaycams, optional: b3r_set_replay_showing (the replay being watched, per VBlank)
    int (*replays)() = nullptr;          // replayview, optional: b3r_replays (RACE REPLAY on)
    int (*shift_presses)(int) = nullptr;   // gearbox: SHIFT UP (1) / SHIFT DOWN (0) presses the window took since the last call     // the CAR PAINT PANEL presses the window took on the page since the last call
    int (*paint_toggle)(int) = nullptr;   // open the panel for a player (1) or ask it to close (0); -1: neither
    void (*fast_forward)(int) = nullptr;  // fastload1, optional: the display shows the newest frame while 1
    void (*game_behind)(int) = nullptr;   // slowcpu, optional: 1 while the game is behind real time (the renderer holds to 60)
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

std::string g_voice_code;   // 1.3.0 VOICES: the voices' language this run ("": English; training_voice.h)
bool g_training_pictures = false;   // ... the imported training pictures in use (the replay's "training_pictures")
std::string g_dj_banks;             // ... the DJ banks redirected (platform::disc::dj_banks; the replay's "dj_banks")
bool g_hide_save_screens = false;   // autosave1: SAVE SCREENS is Hide (latched per VBlank; the poll's hold probe reads it)
bool hold_probe() {   // at each poll: the AUTOSAVE page is up and hidden (the display holds the page before it)
    return g_hide_save_screens && b3native::autosave_page_up(b3host::load<std::uint32_t>(b3native::kMenuCurrentPage)) &&
           !b3native::message_box_up(b3host::load<std::uint8_t>(b3native::kMessageShown));   // saveflow: a question shows
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
    b3io::setenv(name, v);   // fsrobust: UTF-8 (Windows: the wide environment; read back with b3io::getenv)
}
void env_unset(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
// A hash of guest RAM below MB 31 (that one holds the stack, which differs harmlessly between hosts), FNV-1a per word.
// ghvariant: `masked` (a recording's "ram_hash" 2, every new one): the 8-byte words that touch the places the known
// program variants change (b3port/game_program.h kVariantRanges) are left out, so a replay recorded with the retail
// program plays back with the patched one and the other way round; a recording without the entry keeps the full hash.
std::uint64_t replay_ram_hash(bool masked) {
    static std::vector<std::uint8_t> buf(0x100000u);
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (b3host::gaddr mb = 0; mb < 0x01F00000u; mb += 0x100000u) {
        b3host::copy_from_guest(buf.data(), mb, 0x100000u);
        for (std::size_t k = 0; k < buf.size(); k += 8) {
            if (masked) {
                const std::uint32_t a = mb + std::uint32_t(k);
                bool skip = false;
                for (const b3port::ProgramRange& r : b3port::kVariantRanges) skip |= a < r.end && a + 8 > r.begin;
                if (skip) continue;
            }
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
    std::uint32_t game_mods = 0;   // options3 (game_mods.h): the mask (a recording without the entry: none)
    int units = 0;   // units1: SPEED UNITS (tools/native/speed_units.h)
    int boot_to = 0;   // gamemods1: START AT (tools/native/boot_boost.h; a recording without it: the game's start)
    int boot_profile = -1;   // gamemods1: START AT's profile in the save at the boot (1 / 0; -1: not asked, the save read)
    int idle_pause = 1, long_event_end = 1;   // gamemods1: tools/native/game_limits.h (a recording without them: the game's)
    int takedown_pitch = 0;   // hudmods: TAKEDOWN STREAK PITCH (tools/native/takedown_pitch.h; a recording without it: off)
    int music_volume = 100;   // musicvol1: CUSTOM MUSIC VOLUME (custom_soundtrack.h; host audio only, the game never reads it)
    int music_loudness = 1;   // musicloud: CUSTOM MUSIC LOUDNESS (a recording without it: Even out; sound only)
    std::string music_dir;
    std::string now_playing;   // nowplaying1: a replay's latched track (b3nowplaying::encode; empty: none recorded)
    int race_time = 0;         // todrace: the track page's DAY / NIGHT choice (race_time.h; a recording without it: Default)
    unsigned game_options = 0; // gameopts: GAMEPLAY's options asked for (game_option_rules.h bits; a recording without it: none)
    bool car_viewer_request = false;   // gamemods1: a replay's car viewer request on this VBlank ("car_viewer")
    int shift_up = 0, shift_down = 0;   // gearbox: a replay's SHIFT UP / SHIFT DOWN presses for this VBlank ("shift")
    bool party_moon = false;   // party1: MOON JUMP held (the replay's "party_moon" setting; a recording without it: never)
    int party_crash = 0, party_ai = 0;   // party1: a replay's CRASH / AI DRIVER presses for this VBlank ("party_press" events)
    int replay_presses = 0;   // replayview: a recording's REPLAY presses for this VBlank ("replay")
    bool race_time_row = false;   // raceopts: SELECT OPTIONS' TIME OF DAY row shown (a recording without the entry: none)
    bool race_weather_row = false;   // condrows: SELECT OPTIONS' WEATHER row shown (a recording without the entry: none)
    unsigned traffic_check = 0;   // trafficcheck: TRAFFIC CHECKING asked for (TcMode; a recording without the entry: off)
    bool heavy_series = false; // heavyseries: SELECT CAR's HEAVYWEIGHTS series offered (a recording without the entry: none)
    // extras: PC SETTINGS > EXTRAS' word (b3settings::extras_bits) latched; a recording without the entry: every extra on,
    // as every build before EXTRAS played (its own entries -- heavy_series, race_opts, ... -- say what it had)
    std::uint32_t extras = b3settings::kExtrasAll;
    std::vector<std::pair<b3host::gaddr, std::uint64_t>> demotes;   // extras: this VBlank's "extra_demote" events (slot, donor)
    bool traffic_series = false;   // trafficseries: SELECT CAR's TRAFFIC series offered (a recording without the entry: none)
    bool title_fast = false;   // titlefast: the title's prompt and Start at once (a recording without the entry: the game's wait)
    bool settings_back = false;   // settingsback: Back keeps a SETTINGS page's changes (a recording without the entry: the
                                  // game's cancel)
    bool crash_ram_fix = false;   // crashramfix: the rammers' flags as retail's (a recording without the entry: the draft's, as
                                  // every build before the fix played them)
    // carstats1 / blinkers1 / rrparts1: CAR STATS, TURN SIGNALS, ROAD RAGE PARTS (a recording without an entry: off, as before they existed)
    bool car_stats = false, turn_signals = false, road_rage_parts = false;
};
LiveSettings g_live;
// carstats1 / blinkers1 / rrparts1: the rows the game-side hooks read (the latched values while a replay is recorded or played)
int (*g_car_stats)() = nullptr;
int (*g_turn_signals)() = nullptr;
int (*g_road_rage_parts)() = nullptr;
// gamemods1: START AT main menu: the active save holds profile `slot` (profile-N.sav, else the older profile-N folder)
bool start_at_profile_in_save(int slot) {
    const auto root = b3io::getenv("B3NATIVE_SAVES_ROOT");
    if (!root) return false;
    const std::filesystem::path base = b3io::u8path(*root);
    const int n = b3native::save::read_active_slot(base);
    std::filesystem::path f = base / ("profile-" + std::to_string(n) + ".sav");
    std::error_code ec;
    if (!std::filesystem::exists(f, ec)) f = base / ("profile-" + std::to_string(n));
    const auto profiles = b3native::save::read_profile_stats(f);
    if (!profiles) return false;
    for (const auto& p : *profiles) if (p.index == slot) return true;
    return false;
}
bool car_stats_on() { return g_live.on ? g_live.car_stats : g_car_stats && g_car_stats() != 0; }
bool turn_signals_on() { return g_live.on ? g_live.turn_signals : g_turn_signals && g_turn_signals() != 0; }
bool road_rage_parts_on() { return g_live.on ? g_live.road_rage_parts : g_road_rage_parts && g_road_rage_parts() != 0; }
int (*g_photo_row)() = nullptr;   // photorow1: b3r_photo_row
int (*g_game_mods)() = nullptr;   // options3: b3r_game_mods
std::uint32_t game_mods_wanted() { return g_game_mods ? std::uint32_t(g_game_mods()) : 0u; }
// B3_PHOTO_ROW=0|1 overrides PC SETTINGS > GAME > INTERFACE > PHOTO MODE IN PAUSE MENU (the golden runs set 0: the game's own menu);
// no renderer: no row
bool photo_row_wanted() {
    if (const char* e = std::getenv("B3_PHOTO_ROW"); e && (e[0] == '0' || e[0] == '1') && !e[1]) return e[0] == '1';
    return g_photo_row && g_photo_row() != 0;
}
// trafficcheck (traffic_check.h): TRAFFIC CHECK (2 PLAYERS) asked for now (TcMode): $B3NATIVE_TRAFFIC_CHECK (0 / 1 / 2), else
// the setting (no renderer: off)
int (*g_traffic_check)() = nullptr;   // b3r_traffic_check
unsigned traffic_check_wanted() {
    const int e = b3native::TrafficCheck::env_override();
    if (e >= 0) return unsigned(e);
    return g_traffic_check ? b3native::traffic_check_normal(unsigned(g_traffic_check())) : 0u;
}
// gameopts (game_options.h): GAMEPLAY's options asked for now: $B3NATIVE_GAME_OPTIONS (the word, 0..8191), else the setting
// (no renderer: none)
int (*g_game_options)() = nullptr;   // b3r_game_options
unsigned game_options_wanted() {
    const unsigned e = b3native::GameOptions::env_override();
    if (e != ~0u) return e;
    return g_game_options ? b3native::game_options_normal(unsigned(g_game_options())) : 0u;
}
// extras (PC SETTINGS > EXTRAS; b3settings::extras_bits): the word asked for now. $B3NATIVE_EXTRAS ("all", or the word as a
// number: the tests' and the golden runs' switch), else the setting (no renderer: 0, the game as it shipped)
int (*g_extras)() = nullptr;   // b3r_extras
std::uint32_t extras_wanted() {
    // B3NATIVE_EXTRAS_AT=<vblank>:<word> (tests: an extra switched off or on mid-session, with something remembered)
    if (const char* e = std::getenv("B3NATIVE_EXTRAS_AT"); e && *e) {
        unsigned long long at = 0;
        char w[16] = {};
        if (std::sscanf(e, "%llu:%15s", &at, w) == 2 && b3native::platform::vblank_count() >= at)
            return std::strcmp(w, "all") == 0 ? b3settings::kExtrasAll : std::uint32_t(std::strtoul(w, nullptr, 0)) & b3settings::kExtrasMask;
    }
    if (const char* e = std::getenv("B3NATIVE_EXTRAS"); e && *e)
        return std::strcmp(e, "all") == 0 ? b3settings::kExtrasAll : std::uint32_t(std::strtoul(e, nullptr, 0)) & b3settings::kExtrasMask;
    return g_extras ? std::uint32_t(g_extras()) & b3settings::kExtrasMask : 0u;
}
// The word in effect: a replay's latched one, else the one asked for
std::uint32_t extras_now() { return g_live.on ? g_live.extras : extras_wanted(); }
// An extra on now: its own switch first (`env` 0: off, 1: on -- comparisons and tests), else its bit. While a replay is
// recorded or played (g_live.on) the switch is ignored: only B3NATIVE_EXTRAS and the settings make the recorded word
bool extra_on(std::uint32_t bit, const char* env) {
    if (!g_live.on && env)
        if (const char* e = std::getenv(env); e && (e[0] == '0' || e[0] == '1') && !e[1]) return e[0] == '1';
    return (extras_now() & bit) != 0;
}
// extras: NIGHT / RAIN IN WORLD TOUR -- World Tour's own event requests (CB3MenuChoices::RequestNewGameMode 0x30D1B0,
// every mode's request, leaving the one-player stage World Tour's, +0x1B8 0x0051B928, from the World Tour menu), so a
// load is one of its events only after one: noted here, cleared by any other mode's request and on the main menu and
// the title (a later attract demo, two-player race or online load never matches a stale stage record)
struct WorldTourRequest {
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static inline Entry next = nullptr;
    static inline bool asked = false;
    static void request(const b3host::CallArgs& a, b3host::CallResult& r) {
        next(a, r);
        asked = b3host::load<std::int32_t>(0x01E900B0u + 4u) == 0 && b3host::load<std::uint32_t>(0x0051AD00u + 0x1B8u) == 0x0051B928u;
    }
    static bool install(b3native::HookTable& hooks) {
        std::string error;
        std::vector<b3native::Hook> specs = {{0x0030D1B0u, b3native::HookLayer::kPort, &request, &next}};
        if (!hooks.install("port:world_tour_request", specs, &error)) {
            std::fprintf(stderr, "native_play: world tour request: %s\n", error.c_str());
            return false;
        }
        return true;
    }
};
// heavyseries (heavy_race.h): the HEAVYWEIGHTS series on SELECT CAR for Single Event races (EXTRAS; B3NATIVE_HEAVY_SERIES)
bool heavy_series_wanted() { return extra_on(b3settings::kExtraHeavyweights, "B3NATIVE_HEAVY_SERIES"); }
// trafficseries (traffic_cars.h): the TRAFFIC series on SELECT CAR (EXTRAS; B3NATIVE_TRAFFIC_SERIES)
bool traffic_series_wanted() { return extra_on(b3settings::kExtraTrafficSeries, "B3NATIVE_TRAFFIC_SERIES"); }
// customseries (extra_cars.h): the CUSTOM / BETA series on SELECT CAR when their folder has a car (EXTRAS, read at the
// start; B3NATIVE_CUSTOM_SERIES for both)
bool custom_series_wanted() { return extra_on(b3settings::kExtraCustomCars, "B3NATIVE_CUSTOM_SERIES"); }
bool beta_series_wanted() { return extra_on(b3settings::kExtraBetaCars, "B3NATIVE_CUSTOM_SERIES"); }
// titlefast (title_fast.h): the title's prompt and Start without the game's wait; B3NATIVE_TITLE_FAST=0: the game's
bool title_fast_wanted() { return b3native::TitleFast::requested(); }
// settingsback (settings_back.h): SETTINGS BACK BUTTON: $B3NATIVE_SETTINGS_BACK (0 / 1), else the setting (no renderer: Keep
// changes, the default)
int (*g_settings_back)() = nullptr;   // b3r_settings_back
bool settings_back_wanted() {
    if (const int e = b3native::settings_back_env(); e >= 0) return e == 1;
    return g_settings_back ? g_settings_back() != 0 : true;
}
std::atomic<bool> g_restart{false};   // nextstart: APPLY AND RESTART asked (the run ends, then relaunch.h)
int g_restart_boot_to = 0;             // START AT when it was asked
std::atomic<bool> g_replay_paused{false};   // the viewer paused a replay (the stall watchdog then waits)

// media1 (replay_card.h): the throwaway replay card removed on Ctrl+C / a terminate request. POSIX: the handler removes
// the card's known files and its folder with unlink / rmdir (async-signal-safe; paths kept beforehand), then the signal
// does what it did before. Windows: the console control handler runs on a thread of its own: remove_all, then the
// default handler ends the process. The backups/ folder a save makes: its files removed with getdents64 on Linux (no
// allocation); elsewhere it is left. Anything left (that folder, a kill) goes at the next start.
std::filesystem::path g_card_dir_path;
#ifndef _WIN32
char g_card_dir[1024];
char g_card_backups[1100];
char g_card_files[5][1100];
int g_card_nfiles = 0;
void card_signal_backups() {
#ifdef __linux__
    const int fd = open(g_card_backups, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;
    alignas(8) char buf[4096];
    for (;;) {
        const long n = syscall(SYS_getdents64, fd, buf, sizeof buf);
        if (n <= 0) break;
        for (long off = 0; off < n;) {   // struct linux_dirent64: d_ino 8, d_off 8, d_reclen 2, d_type 1, d_name
            unsigned short reclen;
            std::memcpy(&reclen, buf + off + 16, sizeof reclen);
            const char* name = buf + off + 19;
            if (!(name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))) unlinkat(fd, name, 0);
            off += reclen;
        }
    }
    close(fd);
#endif
    rmdir(g_card_backups);
}
extern "C" void card_signal(int sig) {
    for (int k = 0; k < g_card_nfiles; ++k) unlink(g_card_files[k]);
    card_signal_backups();
    rmdir(g_card_dir);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#else
BOOL WINAPI card_ctrl(DWORD) {
    std::error_code ec;
    std::filesystem::remove_all(g_card_dir_path, ec);
    return FALSE;
}
#endif
void install_card_signals(const std::filesystem::path& dir, const std::filesystem::path& profile) {
    g_card_dir_path = dir;
#ifndef _WIN32
    const std::string d = dir.string();
    if (d.size() >= sizeof g_card_dir) return;
    std::memcpy(g_card_dir, d.c_str(), d.size() + 1);
    for (const std::string& f : {profile.string(), profile.string() + ".bak", profile.string() + ".tmp", profile.string() + ".unfinished",
                                 (dir / "active-slot.txt").string()})
        if (f.size() < sizeof g_card_files[0]) std::memcpy(g_card_files[g_card_nfiles++], f.c_str(), f.size() + 1);
    if (const std::string b = (dir / "backups").string(); b.size() < sizeof g_card_backups) std::memcpy(g_card_backups, b.c_str(), b.size() + 1);
    for (int sig : {SIGINT, SIGTERM, SIGHUP}) {
        struct sigaction sa {};
        sa.sa_handler = card_signal;
        sigemptyset(&sa.sa_mask);
        sigaction(sig, &sa, nullptr);
    }
#else
    (void)profile;
    SetConsoleCtrlHandler(card_ctrl, TRUE);
#endif
}
bool process_alive(std::uint64_t pid) {
#ifndef _WIN32
    return pid <= 0x7FFFFFFFull && (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
#else
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#endif
}
std::uint64_t own_pid() {
#ifndef _WIN32
    return static_cast<std::uint64_t>(getpid());
#else
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#endif
}

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
    // robust2: the SDL3 the package carries beside native_play first (a bare name is not looked for in the program's
    // folder on macOS, so a renderer that failed to load showed no box there), then the renderer's, then the system's
#ifdef __APPLE__
    const std::string libs[] = {exe_dir + "/libSDL3.0.dylib", exe_dir + "/../Frameworks/libSDL3.0.dylib", exe_dir + "/libb3render.so",
                                "@executable_path/libSDL3.0.dylib", "libSDL3.0.dylib", "libSDL3.dylib"};
#else
    const std::string libs[] = {exe_dir + "/libSDL3.so.0", exe_dir + "/libb3render.so", "libSDL3.so.0"};
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
        if (trim(line.substr(0, eq)) == "game_dir") return b3io::install_resolved(trim(line.substr(eq + 1)), b3io::exe_dir());   // portable mode
    }
    return {};
}

int main(int argc, char** argv) {
    // robust2: Steam's Flatpak sandbox: the host's data folder when it is reachable (save_store.h), else said in the log
    // (printed now, and again once the run log is open: robust2 review 2, so bug reports carry it)
    std::string sandbox_line;
    if (const b3native::save::SandboxData sb = b3native::save::use_host_data_in_sandbox(); sb.sandbox) {
        sandbox_line = "native_play: in a Flatpak sandbox: the data folder is " + sb.dir.string();
        if (sb.host && !sb.left.empty())
            sandbox_line += " (the host's; the settings in the sandbox's own " + sb.left.string() + ", which holds no save, are left there)";
        else if (sb.host) sandbox_line += " (the host's)";
        else if (sb.two) sandbox_line += " (the sandbox's own, which holds saves; the host's holds data too: " + sb.other.string() + ")";
        else sandbox_line += " (the sandbox's own; docs/PLAYING.md: flatpak override --user --filesystem=~/.local/share/b3port com.valvesoftware.Steam)";
        std::fprintf(stderr, "%s\n", sandbox_line.c_str());
    }
#ifdef _WIN32
    // fsrobust: the arguments as UTF-8, as every path in the port (the C runtime's argv is the ANSI code page: a game,
    // saves or mods folder under C:\Users\José or with 東京 in it was mangled; the launcher passes them on a wide command
    // line). The same split as the C runtime's; when the two disagree on the count, the C runtime's argv stays.
    b3io::utf8_args(argc, argv);
#endif
    // vu0 (2026-11-07): native_play runs VU0 micro mode through the generated programs by default
    // (src/native/vu0_aot_rt.h; per launch equal to the interpreter on 3,661,844 launches of navproof14 + races14);
    // B3_VU0_AOT=0 (or check) set by the caller wins -- in a dev build only
    // (vu0n: make VU0_INTERP=1); the default build has no VU0 interpreter, so 1 is its only mode
#ifdef _WIN32
    timeBeginPeriod(1);   // mac5: 1 ms timer resolution for the pacing's sleeps (winmm)
    // winhitch (1.2.1): the process above normal, so busy normal-priority programs (an emulator, a build, a browser)
    // no longer preempt the render thread (BELOW_NORMAL inside the class: base 9, still below the game thread's 10).
    // Measured on a saturated CPU: frames over 50 ms 15-262 -> 1 (the race start). B3_PRIORITY=normal: as before.
    if (const char* pr = std::getenv("B3_PRIORITY"); !(pr && std::strcmp(pr, "normal") == 0))
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
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
        else if (s == "--data-folder") {   // portable mode (1.3.0): the user data folder this program uses, and why; nothing else
            const b3io::UserDirChoice in_use = b3io::user_dir_choice();
            std::printf("%s\n%s\n", b3io::u8string(in_use.dir).c_str(),
                        in_use.kind == b3io::UserDirKind::kPortable   ? "portable: the appdata folder beside the game"
                        : in_use.kind == b3io::UserDirKind::kOverride ? "given: B3PORT_USER_DIR"
                                                                      : "this computer's user folder");
            return 0;
        }
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
        if (!run_log_path.empty()) log = b3io::u8path(run_log_path);
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
        // musicexit (Windows): without a run log, stdout and stderr still reach their file or pipe in whole lines
        if (!b3native::runlog::logging() && b3native::runlog::start_lines()) b3native::g_crash_flush = &b3native::runlog::crash_flush;
    }
    {   // the top of every log: the build, the time, the command line
        std::printf("%s\n", b3port::version_line().c_str());
        std::printf("native_play: started %s (local time);", b3port::runlog::stamp(std::time(nullptr)).c_str());
        for (int k = 0; k < argc; ++k) std::printf(" %s", argv[k]);
        std::printf("\n");
        if (b3native::runlog::logging()) std::printf("native_play: run log %s\n", b3native::runlog::state().path.c_str());
        if (b3native::runlog::logging() && !sandbox_line.empty()) std::printf("%s\n", sandbox_line.c_str());   // robust2 review 2
        if (!offscreen) {   // portable mode (1.3.0): which data folder this run uses (include/b3port/user_dir.h)
            const b3io::UserDirChoice in_use = b3io::user_dir_choice();
            std::printf("native_play: data folder %s (%s)\n", b3io::u8string(in_use.dir).c_str(),
                        in_use.kind == b3io::UserDirKind::kPortable ? "portable: appdata beside the game"
                        : in_use.kind == b3io::UserDirKind::kOverride ? "given: B3PORT_USER_DIR" : "this computer's");
            std::error_code dec;
            if (in_use.kind == b3io::UserDirKind::kPortable && std::filesystem::is_directory(in_use.dir, dec) && !b3io::dir_writable(in_use.dir))
                std::printf("native_play: the portable data folder cannot be written (a write-protected drive?): settings and "
                            "saves are not kept this time; a save that fails is copied to %s\n",
                            b3io::u8string(b3io::platform_user_dir() / "unsaved").c_str());
        }
        std::fflush(stdout);
    }
    // replay1: a replay to play (src/native/replay_file.h): read, checked against this disc and port, then its game-side
    // environment, options and card (a throwaway copy) put in place of this run's
    std::optional<b3native::replay::Replay> replay_in;
    std::filesystem::path replay_card;   // the throwaway card's folder (removed at the end)
    b3native::replay_card::CardFolder replay_card_guard;   // media1: removed on every return, whatever the way out
    {   // media1: the cards of earlier replays whose process is gone (a kill, a crash)
        std::error_code ec;
        const std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
        if (!ec && !tmp.empty())
            if (const int gone = b3native::replay_card::sweep_stale_cards(tmp, process_alive); gone > 0)
                std::printf("native_play: replay: %d throwaway memory card%s of earlier replays removed from %s\n", gone, gone == 1 ? "" : "s",
                            tmp.string().c_str());
    }
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
        if (const std::string* form = replay_in->option("ram_hash"); b3port::ram_hash_form(form) < 0)   // ghvariant
            return refuse("The replay " + name + " checks the game's memory in a form this version does not know (ram_hash " + *form +
                          "): it was made by a newer version of the port.");
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
                          b3native::replay_card::folder_name(own_pid(), std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(replay_card, ec);
            replay_card_guard.set(replay_card);
            install_card_signals(replay_card, b3native::save::profile_file(replay_card, replay_in->card_slot));
            std::ofstream f(b3native::save::profile_file(replay_card, replay_in->card_slot), std::ios::binary);
            f.write(reinterpret_cast<const char*>(replay_in->card_sav.data()), std::streamsize(replay_in->card_sav.size()));
            f.close();
            if (ec || !f || !b3native::save::write_active_slot(replay_card, replay_in->card_slot, &why))
                return refuse("No room for the replay's memory card in " + replay_card.string() + ".");
            saves = replay_card.string();
            env_set("B3NATIVE_CARD_PERSIST", "1");
            env_set("B3NATIVE_CARD_THROWAWAY", "1");   // saveflow: no folder lock, no SAVED bubble for it (save_store.h)
        }
        std::printf("native_play: replaying %s: recorded %s by %s, %s long (to VBlank %llu), %zu pad ops, %zu checkpoints, %s; "
                    "the window's controls are ignored (Start pauses it)\n",
                    replay_file.c_str(), replay_in->info_value("recorded") ? replay_in->info_value("recorded")->c_str() : "?",
                    replay_in->version.c_str(), b3native::replay::format_length(replay_in->vblanks).c_str(),
                    static_cast<unsigned long long>(replay_in->vblanks), replay_in->pads.size(), replay_in->checks.size(),
                    replay_in->card ? ("its card (profile " + std::to_string(replay_in->card_slot) + ") in a throwaway copy").c_str() : "no memory card");
        std::fflush(stdout);
    }
    if (game.empty()) if (const auto g = b3io::getenv("B3_GAME_DIR")) game = *g;   // fsrobust: the wide value
    if (game.empty() && !offscreen) {   // started directly (not by the launcher): the launcher's choice
        game = launcher_game_dir();
        if (!game.empty()) std::printf("native_play: game data folder from launcher.ini: %s\n", game.c_str());
    }
    if (elf.empty()) if (const auto e = b3io::getenv("B3_ELF")) elf = *e;
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
        auto set_env = [](const char* name, const std::string& v) { b3io::setenv(name, v); };   // fsrobust: UTF-8
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
                root = b3io::u8string(d);   // fsrobust: UTF-8 (set wide on Windows; read with b3io::getenv)
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
            if (!d.empty() && !ec) set_env("B3NATIVE_SAVES_ROOT", b3io::u8string(d));
            else std::fprintf(stderr, "native_play: no saves folder (%s): no memory card this run\n", d.empty() ? "no user data folder" : ec.message().c_str());
        }
        if (const auto root = b3io::getenv("B3NATIVE_SAVES_ROOT")) {
            if (!std::getenv("B3NATIVE_CARD_PRESENT_VBLANK")) set_env("B3NATIVE_CARD_PRESENT_VBLANK", "0");
            if (!std::getenv("B3NATIVE_CARD_PERSIST")) set_env("B3NATIVE_CARD_PERSIST", "1");   // the game's saves to the .sav
            std::printf("native_play: saves in %s (profile %d)\n", root->c_str(), b3native::save::read_active_slot(b3io::u8path(*root)));
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
    std::string guards_off;   // hostfix: why the guest RAM guards are off this run (empty: on; B3_GUEST_GUARDS=0: the backend)
    {
        if (!mods_given)
            if (const char* e = std::getenv("B3NATIVE_MODS"); e && *e) { mods_arg = e; mods_given = true; }
        if (mods_dir.empty())
            if (const auto e = b3io::getenv("B3NATIVE_MODS_DIR")) mods_dir = *e;
        const std::filesystem::path pref = b3native::save::user_data_dir();
        const std::filesystem::path root = !mods_dir.empty() ? b3io::u8path(mods_dir)
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
        // hostfix: the guest RAM guards (b3settings::safe_mode_guards_off): safe mode asked for (the launcher's START IN
        // SAFE MODE: --safe-mode; B3_SAFE_MODE=1) starts with them off, the workaround for a "guest memory overrun" crash.
        // Safe mode chosen after two runs that did not end cleanly (run_state.ini, read here before the renderer writes
        // it, as for the mods below) keeps them on unless the previous run's log shows such an overrun.
        {
            const char* env = std::getenv("B3_SAFE_MODE");
            b3settings::StartDecision d;
            d.forced = d.safe = safe_mode || (env && std::strcmp(env, "1") == 0);
            if (!d.safe && !offscreen && !pref.empty() && !std::getenv("B3_NO_USER_SETTINGS") && render_args.find("--settings") == std::string::npos)
                d = b3settings::start_run(b3settings::load_run_state(b3mod::path_utf8(pref / "run_state.ini")), false);
            bool overrun = false;
            std::string last_log;
            if (d.safe && !d.forced && !pref.empty()) {   // the previous run's log (this run's is the newest)
                const std::filesystem::path dir = b3port::runlog::logs_dir(pref);
                const std::string mine = b3native::runlog::logging() ? b3port::runlog::detail::run_of(b3io::u8path(b3native::runlog::state().path)) : "";
                for (const std::string& r : b3port::runlog::runs(dir))
                    if (r != mine) { last_log = b3mod::path_utf8(dir / (r + ".log")); break; }
                overrun = !last_log.empty() && b3settings::log_shows_guard_overrun(last_log);
            }
            guards_off = b3settings::safe_mode_guards_off(d, overrun);
            if (d.safe && !d.forced)
                std::printf("native_play: safe mode (the last runs did not end cleanly): guest RAM guards %s (%s)\n", guards_off.empty() ? "stay on" : "off",
                            last_log.empty() ? "no earlier run log" : ((overrun ? "a guest memory overrun in " : "no guest memory overrun in ") + last_log).c_str());
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
        // PC SETTINGS > MODS > DEVELOPER MODS (pc_settings.ini developer_mods, which the launcher's Mods page
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
            const auto droot = b3io::getenv("B3_DISC_ROOT");
            const std::filesystem::path disc_root = b3io::u8path(droot ? *droot : !game.empty() ? game : "data/game");
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
                // sdk2b (DESIGN.md §2.5): a code mod is a program; a window run loads only code the user allowed in
                // the launcher (once per mod id and set of files: the SHA-256 over every library in the mod's folder,
                // in mods-trusted.ini). Offscreen runs (tests, scripts naming the mods) are not asked: a test-only
                // behaviour, the launcher never starts the game offscreen. The loader checks the set again right
                // before it loads the library (CodeMods::load).
                if (m.code.present) {
                    std::printf("native_play: mods: %s: contains code (%s%s)\n", m.id.c_str(),
                                m.library.empty() ? "no library for this system" : b3mod::path_utf8(m.library).c_str(),
                                m.code.gameplay ? "; changes gameplay" : "");
                    if (!m.library.empty()) code_digest[m.id] = b3mod::trust_digest(m.dir, m.code, m.script);
                    if (!m.library.empty() && code_digest[m.id].empty())
                        code_refused[m.id] = "its libraries could not be read (to check them against what you allowed)";
                    else if (!offscreen && !m.library.empty() && !b3mod::trusted(pref, m.id, code_digest[m.id]))
                        code_refused[m.id] = "its code is not allowed to run yet (new, or a library in it changed): turn the mod on again on the launcher's Mods page and allow it";
                }
                if (m.script.present) {   // lua6: a script (sandboxed: no trust question, unless it says unsafe = yes)
                    std::printf("native_play: mods: %s: a script (%s; %s)\n", m.id.c_str(), m.script.main_file.c_str(),
                                m.script.unsafe ? "unsafe = yes: may write the game's memory and call or replace its functions"
                                : m.script.gameplay ? "may change gameplay" : "gameplay = no: reads and draws only");
                    // modsec: an unsafe script reaches the game's own code with its values: asked as a code mod is
                    if (m.script.unsafe) {
                        code_digest[m.id] = b3mod::trust_digest(m.dir, m.code, m.script);
                        if (code_digest[m.id].empty())
                            code_refused[m.id] = "its script files could not be read (to check them against what you allowed)";
                        else if (!offscreen && !b3mod::trusted(pref, m.id, code_digest[m.id]))
                            code_refused[m.id] = "its unsafe script is not allowed to run yet (new, or a file in it changed): turn the mod on again on the launcher's Mods page and allow it";
                    }
                }
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
            // 1.3.0 VOICES (b3asset/language.h dj_*, training_voice.h): with LANGUAGE French / German / Italian / Spanish and
            // VOICES Language, the race training's narration in it (installed below) and the DJ when its banks are imported.
            // A replay plays with the voices it was recorded with (its "voice" and "dj_voice" options; none: English)
            namespace L = b3asset::language;
            if (!lroot.empty()) b3native::platform::disc::set_languages_root(lroot);   // the training's localized pictures
            std::string voice = !sp.empty() && ls.voices == 0 && L::dj_letter(ls.language) ? L::normalise(ls.language) : std::string();
            std::string dj = voice;
            if (replay_in) {
                const std::string* o = replay_in->option("voice");
                const std::string* d = replay_in->option("dj_voice");
                voice = o ? L::normalise(*o) : std::string();
                dj = d ? L::normalise(*d) : std::string();
            }
            g_voice_code = voice;
            if (!voice.empty()) {
                // the training's pictures: the imported ones when there, else the English; a replay plays the ones it had
                bool pictures = b3native::platform::disc::training_pictures(voice);
                if (replay_in) {
                    const std::string* tp = replay_in->option("training_pictures");
                    const bool want = tp && *tp == "imported";
                    if (want && !pictures) {
                        const std::string text = "This replay was recorded with the race training videos in " + L::name_of(voice) +
                                                 ", which are not imported here (the launcher's Settings > GAME > IMPORT LANGUAGES), so it would not play the same.";
                        if (!offscreen) tell_user("cannot play the replay", text, exe_dir);
                        else std::fprintf(stderr, "native_play: replay: %s\n", text.c_str());
                        return 2;
                    }
                    pictures = want;
                }
                b3native::platform::disc::use_training_pictures(pictures);
                g_training_pictures = pictures;
                std::printf("native_play: voices %s (%s): the race training's narration, its pictures %s\n", voice.c_str(), L::name_of(voice).c_str(),
                            pictures ? "imported" : "English");
            }
            if (!dj.empty()) {
                const std::string droot = pref.empty() ? std::string() : b3mod::path_utf8(L::dj_root(pref, dj));
                if (b3native::platform::disc::set_dj_voice(droot, dj)) {
                    const auto disc_env = b3io::getenv("B3_DISC_ROOT");   // (set from --game further on when unset)
                    g_dj_banks = b3native::platform::disc::dj_banks(disc_env ? *disc_env : game);   // every bank decided now (the replay's "dj_banks")
                    std::printf("native_play: dj voice %s (%s), imported in %s; banks %s\n", dj.c_str(), L::name_of(dj).c_str(), droot.c_str(),
                                g_dj_banks.c_str());
                    const std::string* want = replay_in ? replay_in->option("dj_banks") : nullptr;
                    if (replay_in && (!want || *want != g_dj_banks)) {
                        const std::string text = "This replay was recorded with other DJ banks in " + L::name_of(dj) + " (" +
                                                 (want ? *want : std::string("none")) + ", here " + g_dj_banks + "), so it would not play the same.";
                        if (!offscreen) tell_user("cannot play the replay", text, exe_dir);
                        else std::fprintf(stderr, "native_play: replay: %s\n", text.c_str());
                        return 2;
                    }
                } else if (replay_in) {
                    const std::string text = "This replay was recorded with the DJ in " + L::name_of(dj) +
                                             ", which is not imported here (the launcher's Settings > GAME > IMPORT LANGUAGES), so it would not play the same.";
                    if (!offscreen) tell_user("cannot play the replay", text, exe_dir);
                    else std::fprintf(stderr, "native_play: replay: %s\n", text.c_str());
                    return 2;
                } else
                    std::printf("native_play: dj voice %s: not imported (%s); the English DJ\n", dj.c_str(), droot.c_str());
            } else if (!replay_in && ls.voices == 1 && L::dj_letter(ls.language))
                std::printf("native_play: voices: English (VOICES)\n");
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
              (replay_in ? " --replay-playback" : "") +   // media1: a replay's laps and times are not the player's (records.h)
              mod_render_args + (mods_safe_off ? " --mods-safe-off " + std::to_string(mods_safe_off) : "");   // modkit1b
    ro.game = game;
    ro.vblank = &b3native::platform::vblank_count;
    // mac4: the platform reads the disc's files from B3_DISC_ROOT (platform_sif.cpp disc_root; its default is this PC's
    // data/game or the WSL install): --game is that folder unless B3_DISC_ROOT says otherwise
    if (!game.empty() && !std::getenv("B3_DISC_ROOT")) b3io::setenv("B3_DISC_ROOT", game);   // fsrobust: UTF-8, read with b3io::getenv
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
                                              b3native::runlog::logging() ? b3native::runlog::state().path : std::string()),
                          exe_dir);
            return 2;
        }
        auto sym = [m](const char* name) { return reinterpret_cast<void*>(GetProcAddress(m, name)); };
#else
        void* m = lib ? dlopen(lib, RTLD_NOW | RTLD_LOCAL) : nullptr;
        if (!m) {   // robust2: told in a box too (macOS, Linux), as on Windows
            const char* err = dlerror();
            const std::string why = err ? err : "not found";
            std::fprintf(stderr, "native_play: cannot load the renderer %s (%s)\n", lib ? lib : "(none)", why.c_str());
            if (!offscreen)
                tell_user("cannot start",
                          "The game's renderer (" + std::string(lib ? lib : "libb3render.so") + ") could not be loaded:\n" + why +
                              "\n\nKeep the game's files together in one folder (or reinstall the game), then start it again.",
                          exe_dir);
            return 2;
        }
        auto sym = [m](const char* name) { return dlsym(m, name); };
#endif
        r.open = reinterpret_cast<int (*)(const char*)>(sym("b3r_open_main"));
        r.run_main = reinterpret_cast<int (*)()>(sym("b3r_run_main"));
        r.pad_sample = reinterpret_cast<int (*)(int, unsigned char*, float)>(sym("b3r_pad_sample"));
        r.pad_key = reinterpret_cast<void (*)(int, int)>(sym("b3r_pad_key"));
        r.focused = reinterpret_cast<int (*)()>(sym("b3r_focused"));
        r.pad_context = reinterpret_cast<void (*)(int)>(sym("b3r_pad_context"));
        r.quit_requested = reinterpret_cast<int (*)()>(sym("b3r_quit_requested"));
        r.restart_requested = reinterpret_cast<int (*)()>(sym("b3r_restart_requested"));   // nextstart
        r.close = reinterpret_cast<int (*)(std::uint32_t)>(sym("b3r_close"));
        r.perf_on = reinterpret_cast<int (*)()>(sym("b3r_perf_on"));   // perf3
        r.perf_add = reinterpret_cast<void (*)(int, std::uint64_t)>(sym("b3r_perf_add"));
        r.audio_push = reinterpret_cast<void (*)(const std::int16_t*, std::size_t)>(sym("b3r_audio_push"));
        r.audio_late = reinterpret_cast<unsigned long long (*)()>(sym("b3r_audio_late"));   // stutter
        r.audio_quiet = reinterpret_cast<void (*)(int)>(sym("b3r_audio_quiet"));            // loadaudio
        r.feed_full = reinterpret_cast<int (*)()>(sym("b3r_feed_full"));                    // trafmem
        r.feed_hold = reinterpret_cast<unsigned (*)()>(sym("b3r_feed_hold"));               // trafmem
        r.settings_open = reinterpret_cast<int (*)()>(sym("b3r_settings_open"));
        r.settings_active = reinterpret_cast<int (*)()>(sym("b3r_settings_active"));
        r.set_paused = reinterpret_cast<void (*)(int)>(sym("b3r_set_paused"));   // photo mode
        r.photo_row = reinterpret_cast<int (*)()>(sym("b3r_photo_row"));   // photorow1
        r.photo_enter = reinterpret_cast<int (*)()>(sym("b3r_photo_enter"));
        g_photo_row = r.photo_row;
        g_settings_back = reinterpret_cast<int (*)()>(sym("b3r_settings_back"));   // settingsback (an older b3render: none)
        r.game_mods = reinterpret_cast<int (*)()>(sym("b3r_game_mods"));   // options3
        g_game_mods = r.game_mods;
        r.date_format = reinterpret_cast<int (*)()>(sym("b3r_date_format"));   // mac5
        r.speed_units = reinterpret_cast<int (*)()>(sym("b3r_speed_units"));   // units1
        r.boot_to = reinterpret_cast<int (*)()>(sym("b3r_boot_to"));   // gamemods1
        r.idle_pause = reinterpret_cast<int (*)()>(sym("b3r_idle_pause"));
        r.long_event_end = reinterpret_cast<int (*)()>(sym("b3r_long_event_end"));
        r.takedown_pitch = reinterpret_cast<int (*)()>(sym("b3r_takedown_pitch"));   // hudmods
        r.record_replays = reinterpret_cast<int (*)()>(sym("b3r_record_replays"));   // replay1
        r.music = reinterpret_cast<int (*)(char*, int)>(sym("b3r_music"));   // custom soundtrack (remaster-features §6)
        r.music_volume = reinterpret_cast<int (*)()>(sym("b3r_music_volume"));   // musicvol1
        r.music_loudness = reinterpret_cast<int (*)()>(sym("b3r_music_loudness"));   // musicloud
        r.other_music_apps = reinterpret_cast<int (*)()>(sym("b3r_other_music_apps"));   // nowplaying1
        r.sound_output = reinterpret_cast<int (*)()>(sym("b3r_sound_output"));   // audioq
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
        r.extras = reinterpret_cast<int (*)()>(sym("b3r_extras"));   // extras
        r.traffic_check = reinterpret_cast<int (*)()>(sym("b3r_traffic_check"));   // trafficcheck
        g_traffic_check = r.traffic_check;
        g_extras = r.extras;   // extras (logged once the renderer is open: b3r_extras reads its settings file)
        r.game_options = reinterpret_cast<int (*)()>(sym("b3r_game_options"));   // gameopts
        // (gameoptsfix, pre4.2: copied only after the lookup -- before it, g_game_options stayed null from gameopts on, so
        // MIRROR MODE, CRASHBREAKER IN EVERY MODE and the PARTY MODES were never taken from the settings, only from
        // $B3NATIVE_GAME_OPTIONS, which every check of them set)
        g_game_options = r.game_options;
        r.car_stats = reinterpret_cast<int (*)()>(sym("b3r_car_stats"));               // carstats1
        r.turn_signals = reinterpret_cast<int (*)()>(sym("b3r_turn_signals"));         // blinkers1
        r.road_rage_parts = reinterpret_cast<int (*)()>(sym("b3r_road_rage_parts"));   // rrparts1
        r.party_keys = reinterpret_cast<int (*)()>(sym("b3r_party_keys"));       // party1
        r.race_time_page = reinterpret_cast<void (*)(int)>(sym("b3r_race_time_page"));   // todrace
        r.race_time_presses = reinterpret_cast<int (*)()>(sym("b3r_race_time_presses"));
        r.night_setting = reinterpret_cast<int (*)()>(sym("b3r_night_setting"));   // raceopts
        r.weather_setting = reinterpret_cast<int (*)()>(sym("b3r_weather_setting"));   // weather
        r.effects_volume = reinterpret_cast<void (*)(int)>(sym("b3r_effects_volume"));   // weather
        r.paint_page = reinterpret_cast<void (*)(int)>(sym("b3r_paint_page"));   // paintsel
        r.paint_game = reinterpret_cast<void (*)(int)>(sym("b3r_paint_game"));
        r.paint_presses = reinterpret_cast<int (*)()>(sym("b3r_paint_presses"));
        r.shift_presses = reinterpret_cast<int (*)(int)>(sym("b3r_shift_presses"));   // gearbox
        r.replay_presses = reinterpret_cast<int (*)()>(sym("b3r_replay_presses"));   // replayview
        r.set_replay_showing = reinterpret_cast<void (*)(int)>(sym("b3r_set_replay_showing"));   // replaycams
        r.replays = reinterpret_cast<int (*)()>(sym("b3r_replays"));
        r.paint_toggle = reinterpret_cast<int (*)(int)>(sym("b3r_paint_toggle"));
        r.fast_forward = reinterpret_cast<void (*)(int)>(sym("b3r_fast_forward"));
        r.game_behind = reinterpret_cast<void (*)(int)>(sym("b3r_game_behind"));
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
        if (!replay_in) {   // extras: the word the start takes (the extra cars below read it once)
            const std::uint32_t x = extras_wanted();
            std::printf("native_play: extras %08X (%s)%s\n", x, x == 0 ? "none: the game as it shipped" : x == b3settings::kExtrasAll ? "all" : "some",
                        std::getenv("B3NATIVE_EXTRAS") && *std::getenv("B3NATIVE_EXTRAS") ? " from B3NATIVE_EXTRAS" : "");
            // gameoptsfix: GAMEPLAY's / PARTY MODES' word the races will latch (settings_start_check asserts it)
            const unsigned g = game_options_wanted();
            std::printf("native_play: game options %u (%s)%s; traffic check (2 players) %u%s\n", g, b3native::game_options_name(g).c_str(),
                        b3native::GameOptions::env_override() != ~0u ? " from B3NATIVE_GAME_OPTIONS" : "", traffic_check_wanted(),
                        b3native::TrafficCheck::env_override() >= 0 ? " from B3NATIVE_TRAFFIC_CHECK" : "");
        }
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
    std::string save_exit_text;                // fsrobust: the last save not written as it should be (empty: it was)
    std::uint64_t quit_hold_from = 0;          // fsrobust: the VBlank a quit waited from for a save being written
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
        b3port::name_this_thread("game");   // split2p: the crash report names the faulting thread
        b3native::sampler::start();   // slowcpu: B3_SAMPLE=<file> (off: nothing)
        struct SamplerEnd { ~SamplerEnd() { b3native::sampler::stop_and_write(); } } sampler_end;
        // the backend, as native_boot sets it up
        std::size_t n = 0;
        const b3native::TableEntry* table = b3native::install(n);
        auto nb = std::make_unique<b3host::NativeBackend>(guards_off.empty() ? nullptr : guards_off.c_str());
        std::printf("native_play: %s\n", nb->guard_text().c_str());   // hostfix: bug reports show the guard size
        {
            // iso1: from the disc folder or from inside the disc image (<image>/SLUS_210.50, b3asset/discfs.h)
            std::vector<std::uint8_t> d;
            b3asset::disc::read_file(elf, d);
            std::string err;
            const b3port::KnownProgram* known = b3port::known_program(d.size(), b3host::fnv1a64(d.data(), d.size()));   // ghvariant
            if (!known) {
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
            if (!known->retail)
                std::printf("native_play: %s: %s (accepted: the port runs the retail code; the patch changes only names and code it "
                            "never runs, b3port/game_program.h)\n", elf.c_str(), known->name);
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
        std::unique_ptr<b3native::LanePairFix> lane_pair_fix;   // the draft's lane pair line in 0x14CA20 (tools/native/lane_pair_fix.h)
        if (b3native::LanePairFix::requested()) {
            lane_pair_fix = std::make_unique<b3native::LanePairFix>();
            if (!lane_pair_fix->install(hooks)) lane_pair_fix.reset();
        }
        std::unique_ptr<b3native::CrashRamFix> crash_ram_fix;   // the draft's ram flags in 0x1B1240 (tools/native/crash_ram_fix.h)
        // (a replay playing: installed whatever $B3NATIVE_CRASH_RAM_FIX says, the recording's entry decides, as the other
        // gameplay switches follow the recording)
        if (b3native::CrashRamFix::requested() || replay_in) {
            crash_ram_fix = std::make_unique<b3native::CrashRamFix>();
            crash_ram_fix->set_on_query([] { return g_live.on ? g_live.crash_ram_fix : true; });   // (a replay: its entry)
            if (!crash_ram_fix->install(hooks)) crash_ram_fix.reset();
        }
        // carstats1 / blinkers1 / rrparts1 (car_stats.h, turn_signals.h, road_rage_parts.h): their rows from the renderer (latched in replays)
        g_car_stats = r.car_stats;
        g_turn_signals = r.turn_signals;
        g_road_rage_parts = r.road_rage_parts;
        std::unique_ptr<b3native::CarStats> car_stats;
        if (b3native::CarStats::requested()) {
            car_stats = std::make_unique<b3native::CarStats>();
            if (!car_stats->install(hooks, &car_stats_on)) car_stats.reset();
        }
        std::unique_ptr<b3native::TurnSignals> turn_signals;
        if (b3native::TurnSignals::requested()) {
            turn_signals = std::make_unique<b3native::TurnSignals>();
            if (!turn_signals->install(hooks, &turn_signals_on)) turn_signals.reset();
        }
        std::unique_ptr<b3native::RoadRageParts> road_rage_parts;
        if (b3native::RoadRageParts::requested()) {
            road_rage_parts = std::make_unique<b3native::RoadRageParts>();
            if (!road_rage_parts->install(hooks, &road_rage_parts_on)) road_rage_parts.reset();
        }
        std::unique_ptr<b3native::PcMenu> pc_menu;   // mac5: the main menu's PC SETTINGS / QUIT GAME rows (tools/native/pc_menu.h)
        if (b3native::PcMenu::requested()) {
            pc_menu = std::make_unique<b3native::PcMenu>();
            if (!pc_menu->install(hooks)) pc_menu.reset();
            else {
                pc_menu->set_photo_row_query([] { return g_live.on ? g_live.photo_row : photo_row_wanted(); });   // photorow1
                // 1.3.1: the help texts shown in the game's language (the renderer's; the game lays out 1.3.0's)
                pc_menu->set_help_text(b3native::platform::disc::language_text(),
                                       [](b3host::gaddr at, const std::u16string& t) { b3gv_native::set_ticker_text(at, t); });
            }
        }
        std::unique_ptr<b3native::GameOptions> game_options = std::make_unique<b3native::GameOptions>();   // gameopts
        game_options->set_query([] { return g_live.on ? g_live.game_options : game_options_wanted(); });
        if (!game_options->install(hooks)) game_options.reset();
        std::unique_ptr<b3native::PartyModes> party = std::make_unique<b3native::PartyModes>();   // party1 (party_modes.h)
        if (!game_options || !party->install(hooks)) party.reset();
        std::unique_ptr<b3native::SingleEliminator> single_eliminator;   // elim1 (tools/native/single_eliminator.h)
        // A replay recorded before the row existed (no "single_eliminator" option) plays with retail's four modes: its
        // Downs on SELECT MODE meant those rows
        const std::string* elim_opt = replay_in ? replay_in->option("single_eliminator") : nullptr;
        if (replay_in && !(elim_opt && *elim_opt == "1"))
            std::printf("single_eliminator: the replay was recorded without the ELIMINATOR row: SELECT MODE as retail\n");
        else if (replay_in || b3native::SingleEliminator::requested()) {
            // integfix: a replay recorded with the row needs it: B3NATIVE_ELIMINATOR=0 (a comparison switch) is not taken
            // then (its SELECT MODE Downs meant these rows; without them the playback would run apart)
            if (replay_in && !b3native::SingleEliminator::requested())
                std::printf("single_eliminator: B3NATIVE_ELIMINATOR=0 ignored: the replay was recorded with the ELIMINATOR row\n");
            single_eliminator = std::make_unique<b3native::SingleEliminator>();
            // extras: SELECT MODE's ELIMINATOR row with its EXTRAS row on ($B3NATIVE_ELIMINATOR=1: on; a replay: its word)
            single_eliminator->set_row_query([] { return extra_on(b3settings::kExtraEliminator, "B3NATIVE_ELIMINATOR"); });
            if (!single_eliminator->install(hooks)) single_eliminator.reset();
        }
        std::unique_ptr<b3native::RaceOptions> race_opts;   // raceopts: SELECT OPTIONS' own rows (tools/native/race_options.h)
        // replay compatibility: a recording made with the rows says so in its header ("race_opts" 1); one without it (made
        // before them, or with them off) plays on the game's own page, as recorded
        const bool race_opts_on = replay_in ? [&] { const std::string* o = replay_in->option("race_opts"); return o && *o == "1"; }()
                                            : b3native::RaceOptions::requested();
        if (replay_in) std::printf("native_play: replay: SELECT OPTIONS %s\n", race_opts_on ? "with the race option rows (as recorded)"
                                                                                             : "as the game's own (recorded without the race option rows)");
        if (race_opts_on) {
            race_opts = std::make_unique<b3native::RaceOptions>();
            race_opts->set_time_row([] { return g_live.on ? g_live.race_time_row : race_time_row_wanted(); });
            race_opts->set_eliminator_query([&single_eliminator] { return single_eliminator && single_eliminator->selected(); });   // elim1: no LAPS
            race_opts->set_language([] { return b3native::platform::disc::language_text(); });   // the rows' words
            // extras: the SELECT OPTIONS rows the player has on ($B3NATIVE_RACE_OPTS=1: every row; a replay: its word)
            race_opts->set_rows_query([] {
                if (!g_live.on)
                    if (const char* e = std::getenv("B3NATIVE_RACE_OPTS"); e && e[0] == '1' && !e[1]) return b3settings::kExtraRaceRows;
                return extras_now() & b3settings::kExtraRaceRows;
            });
            race_opts->set_clock([] { return b3native::platform::vblank_count(); });   // options3: TRAFFIC PATTERN Varied
            if (const char* e = std::getenv("B3_RACEOPTS_TEST_FINISH"); e && *e) race_opts->set_test_finish(std::atoi(e));   // checks only
            race_opts->set_setting_night([&r] {
                if (const char* e = std::getenv("B3_NIGHT"); e && *e) return e[0] != '0';   // (as the renderer: it wins)
                return kRaceTimeThisRelease && r.night_setting && r.night_setting() == 1;
            });
            race_opts->set_weather_row([] { return g_live.on ? g_live.race_weather_row : race_weather_row_wanted(); });
            race_opts->set_setting_rain([&r] {
                if (const char* e = std::getenv("B3_WEATHER"); e && *e) return e[0] != '0' && e[0] != 'c';   // (as the renderer)
                return b3native::kWeatherThisRelease && r.weather_setting && r.weather_setting() == 1;
            });
            // the LAPS row's "(Default)": the stage's own count in the track's GAMEDATA.BGD (race_choice.h race_bgd_laps), its
            // path and stage id asked of the game as RequestOnePlayerGameMode 0x30C2C0 makes them [C]: the track list's
            // CreateTrackPathFromIndex 0x251A30 (GetTrackIndexFromID 0x2518E0) + kacGameDataFileName, GtIDCompress
            // 0x21AEF0 of "OffSgRcF" (D_0049BA40, RACE's). Once per track (kept for the run).
            race_opts->set_track_laps([](std::uint64_t id) -> int {
                static std::map<std::uint64_t, int> known;
                static std::uint64_t stage = 0;
                if (id == 0) return 0;
                if (const auto it = known.find(id); it != known.end()) return it->second;
                constexpr b3host::gaddr kTrackList = 0x005196A0u, kBuf = b3native::race_options_detail::kScratch;
                if (stage == 0) stage = b3host::call_guest<std::uint64_t>(0x0021AEF0u, 0x0049BA40u);
                const int idx = b3host::call_guest<int>(0x002518E0u, kTrackList, id);
                int laps = 0;
                std::string path;
                if (idx >= 0) {
                    b3host::store<std::uint32_t>(kBuf, 0);
                    b3host::call_guest<void>(0x00251A30u, kTrackList, idx, kBuf, 256);
                    for (b3host::gaddr a = kBuf; a < kBuf + 255u; ++a) {
                        const char c = char(b3host::load<std::uint8_t>(a));
                        if (!c) break;
                        path += c;
                    }
                    const b3host::gaddr name = b3host::load<std::uint32_t>(0x004E1D14u) & 0x01FFFFFFu;   // kacGameDataFileName
                    for (b3host::gaddr a = name; name && a < name + 64u; ++a) {
                        const char c = char(b3host::load<std::uint8_t>(a));
                        if (!c) break;
                        path += c;
                    }
                    std::vector<std::uint8_t> bgd;
                    const std::string host = b3native::platform::disc::resolve(path);
                    if (!host.empty() && b3asset::disc::read_file(b3io::u8path(host), bgd)) laps = b3native::race_bgd_laps(bgd.data(), bgd.size(), stage);
                    for (b3host::gaddr a = kBuf; a < kBuf + 256u; a += 4) b3host::store<std::uint32_t>(a, 0);
                }
                known[id] = laps;
                std::printf("race_options: track %016llx (%s): %d laps\n", static_cast<unsigned long long>(id), path.c_str(), laps);
                std::fflush(stdout);
                return laps;
            });
            if (!race_opts->install(hooks)) race_opts.reset();
        }
        std::unique_ptr<b3native::BoostStart> boost_start;   // booststart (boost_start.h): BOOST START on START Standing
        if (race_opts) {
            boost_start = std::make_unique<b3native::BoostStart>();
            boost_start->set_standing_query([&race_opts] { return race_opts && race_opts->choice().start != 0 && race_opts->race_active(); });
            if (!boost_start->install(hooks)) boost_start.reset();
        }
        std::unique_ptr<b3native::TrafficCheck> traffic_check = std::make_unique<b3native::TrafficCheck>();   // trafficcheck
        // the race's TRAFFIC CHECK when it starts: $B3NATIVE_TRAFFIC_CHECK (tests), else a two-player race's the setting's
        // (TRAFFIC CHECK (2 PLAYERS); a replay: its entry), else SINGLE EVENT's SELECT OPTIONS row (raceopts' choice, which
        // a replay holds in its race_options code)
        traffic_check->set_query([&race_opts] {
            if (const int e = b3native::TrafficCheck::env_override(); e >= 0) return unsigned(e);
            if ((b3host::load<std::uint32_t>(b3native::kTcCurrentMode) & 0x01FFFFFFu) == b3native::kTcTwoPlayerStage)
                return g_live.on ? g_live.traffic_check : traffic_check_wanted();
            return race_opts && race_opts->race_active() ? unsigned(race_opts->choice().check) : 0u;
        });
        traffic_check->set_language([] { return b3native::platform::disc::language_text(); });   // the caption's words
        if (!traffic_check->install(hooks)) traffic_check.reset();
        std::unique_ptr<b3native::Gearbox> gearbox;   // gearbox (gearbox.h): TRANSMISSION Manual (raceopts' row)
        if (race_opts && b3native::Gearbox::requested()) {
            gearbox = std::make_unique<b3native::Gearbox>();
            gearbox->set_manual_query([&race_opts] { return race_opts && race_opts->manual_gears(); });
            if (!gearbox->install(hooks)) gearbox.reset();
        }
        std::unique_ptr<b3native::HeavyRace> heavy_race = std::make_unique<b3native::HeavyRace>();   // heavyseries (heavy_race.h)
        heavy_race->set_on_query([] { return g_live.on ? g_live.heavy_series : heavy_series_wanted(); });
        if (!heavy_race->install(hooks)) heavy_race.reset();
        std::unique_ptr<b3native::GameMods> game_mods;   // options3 (tools/native/game_mods.h)
        game_mods = std::make_unique<b3native::GameMods>();
        game_mods->set_mask([] { return g_live.on ? g_live.game_mods : game_mods_wanted(); });
        if (!game_mods->install(hooks)) game_mods.reset();
        if (single_eliminator)   // elim1: TIMED ELIMINATION is raceopts' row (its choice for the race on now)
            single_eliminator->set_timed_query([&race_opts] { return race_opts && race_opts->timed_elimination(); });
        std::unique_ptr<b3native::TitleFast> title_fast = std::make_unique<b3native::TitleFast>();   // titlefast (title_fast.h)
        title_fast->set_on_query([] { return g_live.on ? g_live.title_fast : title_fast_wanted(); });
        if (!title_fast->install(hooks)) title_fast.reset();
        std::unique_ptr<b3native::SettingsBack> settings_back = std::make_unique<b3native::SettingsBack>();   // settingsback
        settings_back->set_keep_query([] { return g_live.on ? g_live.settings_back : settings_back_wanted(); });
        if (!settings_back->install(hooks)) settings_back.reset();
        std::unique_ptr<b3native::ReplayView> replay_view;   // replayview (replay_view.h)
        // RACE REPLAY: on at the start (a replay file: as it was recorded, its "replays" option); off, none of it is hooked
        const bool replays_on = replay_in ? (replay_in->option("replays") && *replay_in->option("replays") == "1")
                                          : (r.replays && r.replays() != 0);
        if (replays_on) {
            replay_view = std::make_unique<b3native::ReplayView>();
            if (!replay_view->install(hooks)) replay_view.reset();
            else if (!replay_in) std::printf("native_play: RACE REPLAY on: the results pages offer Replay\n");   // (extrastart's check)
        }
        std::unique_ptr<b3native::WreckSlots> wreck_slots;   // wreckguard (wreck_slots.h): CrashTrafficBody bounded at 64
        if (b3native::WreckSlots::requested()) {
            wreck_slots = std::make_unique<b3native::WreckSlots>();
            if (!wreck_slots->install(hooks)) wreck_slots.reset();
        }
        // trafficseries (traffic_cars.h): the series on SELECT CAR, and the race's traffic vehicles (B3_TRAFFIC_CARS="player=
        // COMP/CAR18,rivals=COMP/CAR18": tests; gameopts' TUK-TUKS row: set_race_cars)
        std::unique_ptr<b3native::TrafficCars> traffic_cars = std::make_unique<b3native::TrafficCars>();
        traffic_cars->set_series_query([] { return g_live.on ? g_live.traffic_series : traffic_series_wanted(); });
        if (const char* e = std::getenv("B3_TRAFFIC_CARS"); e && *e && !traffic_cars->configure(e))
            std::fprintf(stderr, "native_play: B3_TRAFFIC_CARS=%s: not understood\n", e);
        if (!traffic_cars->install(hooks)) traffic_cars.reset();
        if (traffic_cars)   // paint3 (carousel_models.h): a traffic row's thumbnail drawn from its own model (its donor's picture
                            // on the carousel), in the player's paint as the car on show and the race draw it (paint3b: a
                            // traffic vehicle driven in a race is a race car slot's: the player's paint applies); classes 7 / 8
                            // only while the series are offered
                            // (TrafficCars::offered's test: otherwise they are the game's own, with pictures of their own)
            b3native::chain_carousel_resolver([](int c, int i, bool& paintable) -> std::string {
                if (c != b3native::kTrafficClass && c != b3native::kTrafficSpecialClass) return std::string();
                if (!b3native::traffic_series_event(g_live.on ? g_live.traffic_series : traffic_series_wanted(),
                                                    b3host::load<std::int32_t>(b3native::traffic_cars_detail::kChoices + 4u)))
                    return std::string();
                const std::vector<int> cars = b3native::traffic_series_cars(c);
                if (i < 0 || std::size_t(i) >= cars.size()) return std::string();
                paintable = true;
                return b3native::traffic_table()[std::size_t(cars[std::size_t(i)])].name();
            });
        // customseries (extra_cars.h): the cars of the player's folder, after the traffic series. A replay plays with the
        // cars it was made with ("extra_cars" in its header: each slot's donor and file hash) or none (no entry)
        std::unique_ptr<b3native::ExtraCars> extra_cars;
        {
            const std::string* recorded = replay_in ? replay_in->option("extra_cars") : nullptr;
            if (replay_in ? recorded != nullptr : custom_series_wanted() || beta_series_wanted()) {
                auto x = std::make_unique<b3native::ExtraCars>();
                // extras: a kind the player has off is not read (a replay: both, as recorded)
                x->scan(replay_in || custom_series_wanted() ? b3native::ExtraCars::custom_dir() : std::filesystem::path(),
                        replay_in || beta_series_wanted() ? b3native::ExtraCars::beta_dir() : std::filesystem::path());
                if (recorded && x->entry() != *recorded) {
                    std::fprintf(stderr, "native_play: replay: made with other custom cars (%s); the cars folder has %s: it cannot play as "
                                 "recorded\n", recorded->c_str(), x->entry().empty() ? "none" : x->entry().c_str());
                    return 2;
                }
                x->set_series_query([] { return true; });
                if (const char* e = std::getenv("B3_EXTRA_CARS"); e && *e && !x->configure(e))
                    std::fprintf(stderr, "native_play: B3_EXTRA_CARS=%s: not understood\n", e);
                if (x->install(hooks)) extra_cars = std::move(x);
            }
        }
        // extras: CUSTOM / BETA off this start (a live run): a remembered car of an off kind -- SELECT CAR's chosen one, the
        // choices' slots, as a save restored from a session that had it -- races as its donor (a car no longer in its
        // folder: COMPCAR1), so nothing looks up an id the game's list lacks. The folders are read for the donors only. Each
        // change is an event of the recording ("extra_demote": the slot and the donor), which playback applies.
        // B3NATIVE_EXTRAS_DEMOTE_TEST=1 (tests): both kinds treated as off here, so a chosen extra car shows it
        struct ExtraOff {
            bool custom = false, beta = false;
            std::vector<b3native::ExtraCar> cars;
            std::uint64_t demoted = 0;
        };
        static ExtraOff extra_off;
        const bool demote_test = [] { const char* e = std::getenv("B3NATIVE_EXTRAS_DEMOTE_TEST"); return e && e[0] == '1'; }();
        if (!replay_in && (demote_test || !custom_series_wanted() || !beta_series_wanted())) {
            extra_off.custom = demote_test || !custom_series_wanted();
            extra_off.beta = demote_test || !beta_series_wanted();
            extra_off.cars = b3native::extra_scan_all(b3native::ExtraCars::custom_dir(), b3native::ExtraCars::beta_dir()).cars;
        }
        if (extra_cars)   // paint3 (carousel_models.h): a CUSTOM / BETA row's thumbnail drawn from its own model (its donor's
                          // picture on the carousel), in the player's paint as the car on show; only while offered
            b3native::chain_carousel_resolver([x = extra_cars.get()](int c, int i, bool& paintable) -> std::string {
                std::string m = x->thumb_model(c, i);
                if (!m.empty()) paintable = true;
                return m;
            });
        std::unique_ptr<b3native::CardSilence> card_silence;   // save1: no card messages (tools/native/card_silence.h)
        if (b3native::CardSilence::requested() && std::getenv("B3NATIVE_SAVES_ROOT")) {
            card_silence = std::make_unique<b3native::CardSilence>();
            if (!card_silence->install(hooks)) card_silence.reset();
        }
        std::unique_ptr<b3native::MoreRivals> more_rivals;   // MORE RIVALS: up to 8 cars in a RACE (tools/native/more_rivals.h)
        // replay compatibility: a recording made with it says so ("more_rivals" 1); one without it plays with RIVALS up to
        // the game's 5 (its Right on 5 wrapped to 0)
        const bool more_rivals_on = replay_in ? [&] { const std::string* o = replay_in->option("more_rivals"); return o && *o == "1"; }()
                                              : b3native::MoreRivals::requested();
        // (a playback takes B3NATIVE_MORE_RIVALS from the recording -- replay_file.h kGameEnv, set above -- so a =0 given
        // to it is ignored; this refuses only a recording whose own environment says 0 against its header)
        if (more_rivals_on && !b3native::MoreRivals::requested()) {   // (guest RAM was sized without its reservation)
            std::fprintf(stderr, "native_play: the replay was recorded with MORE RIVALS; it cannot play with B3NATIVE_MORE_RIVALS=0\n");
            return 2;
        }
        if (replay_in && !more_rivals_on) std::printf("more_rivals: the replay was recorded without MORE RIVALS: RIVALS up to the game's 5\n");
        if (more_rivals_on) {
            more_rivals = std::make_unique<b3native::MoreRivals>();
            if (!more_rivals->install(hooks)) more_rivals.reset();
            else {
                more_rivals->set_eliminator_query([&single_eliminator] { return single_eliminator && single_eliminator->selected(); });
                // extras: more than 5 rivals only with MORE RIVALS on (a test's B3NATIVE_MORE_RIVALS=6 / 7 forces a field: on)
                more_rivals->set_on_query([] { return b3native::MoreRivals::forced() || extra_on(b3settings::kExtraMoreRivals, "B3NATIVE_MORE_RIVALS"); });
            }
        }
        // extras: MORE RIVALS (RIVALS up to 7) only with its EXTRAS row on ($B3NATIVE_MORE_RIVALS=1: on; a replay: its word)
        if (race_opts && more_rivals)
            race_opts->set_rivals_max([] { return extra_on(b3settings::kExtraMoreRivals, "B3NATIVE_MORE_RIVALS") ? b3native::MoreRivals::kMaxRivals : 0; });
        // replay1 (src/native/replay_file.h): the session recorded (--record-replay, RECORD REPLAYS) or a replay played;
        // either way the live settings below are latched per VBlank (g_live)
        const b3native::replay::Report neutral_report = [] {
            b3native::replay::Report s{};
            std::memcpy(s.data(), b3native::platform::sif::neutral_pad_sample(), 32);
            return s;
        }();
        std::unique_ptr<b3native::replay::Recorder> rrec;
        std::unique_ptr<b3native::replay::StreamWriter> rstream;   // media1: the recording so far, at each checkpoint
        std::unique_ptr<b3native::replay::Player> rplay;
        std::unique_ptr<b3native::TuningConsole> tuning;   // tune4 (installed below, before the table is sealed)
        bool units_recorded = false;   // units1: a "speed_units" entry is in the recording
        bool limits_recorded = false;  // gamemods1: "idle_pause" / "long_event_end" entries are in the recording
        bool game_mods_recorded = false;   // options3: a "game_mods" entry is in the recording
        bool takedown_pitch_recorded = false;   // hudmods: a "takedown_pitch" entry is in the recording
        bool music_volume_recorded = false;   // musicvol1: a "music_volume" entry is in the recording
        bool music_loudness_recorded = false;   // musicloud: a "music_loudness" entry is in the recording
        bool race_time_recorded = false;   // todrace: a "race_time" entry is in the recording
        bool game_options_recorded = false;   // gameopts: a "game_options" entry is in the recording
        bool party_moon_recorded = false;     // party1: the last "party_moon" entry recorded
        unsigned game_options_shown = 0;      // gameopts: the race's latched options last logged / published
        std::uint64_t game_options_fired = 0;  // gameopts: the Crashbreakers fired outside Crash mode, last logged
        std::uint64_t race_opts_word = 0;  // raceopts: the race's record word last given to the renderer
        bool traffic_check_recorded = false;   // trafficcheck: a "traffic_check" entry is in the recording
        unsigned traffic_check_shown = 0;      // trafficcheck: the race's latched mode last logged / published
        bool race_opts_active = false;     // raceopts: a Single Event stage of the page's mode is the game's mode
        bool heavy_recorded = false;       // heavyseries: a "heavy_series" entry is in the recording
        bool traffic_recorded = false;     // trafficseries: a "traffic_series" entry is in the recording
        bool traffic_racing = false;       // trafficseries: a race with a traffic vehicle as the player's car
        std::uint32_t traffic_shown = 0xFFFFFFFFu;   // trafficseries: SELECT CAR's series << 16 | car last seen
        std::uint32_t extra_shown = 0xFFFFFFFFu;     // customseries: the same for CUSTOM
        bool tuk_set = false;                        // party1: TUK-TUKS given to trafficseries (set_race_cars)
        bool extra_racing = false;                   // customseries: a race with an extra car as the player's car
        bool title_fast_recorded = false;  // titlefast: a "title_fast" entry is in the recording
        bool settings_back_recorded = false;   // settingsback: a "settings_back" entry is in the recording
        bool crash_ram_fix_recorded = false;   // crashramfix: a "crash_ram_fix" entry is in the recording
        int title_seen = 0;                // titlefast: the title page's input (1) and its prompt (2) seen this visit (the log)
        std::uint64_t title_input_vblank = 0;
        bool extra_rows_recorded[3] = {};      // carstats1 / blinkers1 / rrparts1: "car_stats" / "turn_signals" / "road_rage_parts" entries are in it
        bool heavy_racing = false;         // heavyseries: a heavyweight race is the game's (records kept apart)
        std::uint32_t heavy_shown = 0xFFFFFFFFu;   // heavyseries: SELECT CAR's series << 16 | car last seen
        auto sample_live = [&](std::uint64_t v) {   // recording: the renderer's values for this VBlank
            char buf[1024] = {};
            g_live.date = r.date_format ? r.date_format() : 0;
            g_live.music_mode = r.music ? r.music(buf, int(sizeof buf - 1)) : 0;
            g_live.music_dir = buf;
            rrec->setting(v, "date_format", std::to_string(g_live.date));
            g_live.photo_row = photo_row_wanted();   // photorow1: the row changes the menu's navigation
            rrec->setting(v, "photo_row", g_live.photo_row ? "1" : "0");
            g_live.game_mods = game_mods_wanted();   // options3: kept from the first VBlank it is not 0 (as speed units)
            if (g_live.game_mods != 0 || game_mods_recorded) {
                rrec->setting(v, "game_mods", std::to_string(g_live.game_mods));
                game_mods_recorded = true;
            }
            // gameopts: GAMEPLAY's options (they change the race; kept from the first VBlank one is on)
            g_live.game_options = game_options ? game_options_wanted() : 0u;
            if (g_live.game_options != 0 || game_options_recorded) {
                rrec->setting(v, "game_options", std::to_string(g_live.game_options));
                game_options_recorded = true;
            }
            // extras: the word (the SELECT OPTIONS rows, SELECT MODE's ELIMINATOR, RIVALS' range change navigation): every VBlank, first (the rows below
            // follow it)
            g_live.extras = extras_wanted();
            rrec->setting(v, "extras", std::to_string(g_live.extras));
            g_live.race_time_row = race_time_row_wanted();   // raceopts: the row changes SELECT OPTIONS' navigation
            if (g_live.race_time_row) rrec->setting(v, "race_time_row", "1");
            g_live.race_weather_row = race_weather_row_wanted();   // condrows: the WEATHER row (also SELECT OPTIONS' navigation)
            if (g_live.race_weather_row) rrec->setting(v, "race_weather_row", "1");
            // raceopts: the page's choice from the first VBlank on (kept when it changes; the header says the rows are there)
            if (race_opts) rrec->setting(v, "race_options", race_opts->choice().code());
            // trafficcheck: TRAFFIC CHECKING (it changes the race; kept from the first VBlank it is on)
            g_live.traffic_check = traffic_check ? traffic_check_wanted() : 0u;
            if (g_live.traffic_check || traffic_check_recorded) {
                rrec->setting(v, "traffic_check", std::to_string(g_live.traffic_check));
                traffic_check_recorded = true;
            }
            // heavyseries: the series changes SELECT CAR's navigation (kept from the first VBlank it is on)
            g_live.heavy_series = heavy_race && heavy_series_wanted();
            if (g_live.heavy_series || heavy_recorded) {
                rrec->setting(v, "heavy_series", g_live.heavy_series ? "1" : "0");
                heavy_recorded = true;
            }
            // trafficseries: the series changes SELECT CAR's navigation (kept from the first VBlank it is on)
            g_live.traffic_series = traffic_cars && traffic_series_wanted();
            if (g_live.traffic_series || traffic_recorded) {
                rrec->setting(v, "traffic_series", g_live.traffic_series ? "1" : "0");
                traffic_recorded = true;
            }
            // titlefast: the title's wait changes when Start is taken (kept from the first VBlank it is on)
            g_live.title_fast = title_fast && title_fast_wanted();
            if (g_live.title_fast || title_fast_recorded) {
                rrec->setting(v, "title_fast", g_live.title_fast ? "1" : "0");
                title_fast_recorded = true;
            }
            // settingsback: Back on a SETTINGS page keeps its changes (kept from the first VBlank it is on)
            g_live.settings_back = settings_back && settings_back_wanted();
            if (g_live.settings_back || settings_back_recorded) {
                rrec->setting(v, "settings_back", g_live.settings_back ? "1" : "0");
                settings_back_recorded = true;
            }
            // crashramfix: the rammers' flags change the race once a rammer without a trailer rams (kept from the first VBlank
            // it is on; a recording without it, as every older one: the draft's flags on playback)
            g_live.crash_ram_fix = crash_ram_fix != nullptr;
            if (g_live.crash_ram_fix || crash_ram_fix_recorded) {
                rrec->setting(v, "crash_ram_fix", g_live.crash_ram_fix ? "1" : "0");
                crash_ram_fix_recorded = true;
            }
            // carstats1 / blinkers1 / rrparts1: each row kept from the first VBlank it is on (a recording without the entry: off)
            {
                const bool now[3] = {r.car_stats && r.car_stats() != 0, r.turn_signals && r.turn_signals() != 0,
                                     r.road_rage_parts && r.road_rage_parts() != 0};
                const char* keys[3] = {"car_stats", "turn_signals", "road_rage_parts"};
                g_live.car_stats = now[0];
                g_live.turn_signals = now[1];
                g_live.road_rage_parts = now[2];
                for (int k = 0; k < 3; ++k)
                    if (now[k] || extra_rows_recorded[k]) {
                        rrec->setting(v, keys[k], now[k] ? "1" : "0");
                        extra_rows_recorded[k] = true;
                    }
            }
            // units1: kept from the first VBlank it is KM/H (a recording at MPH, the game's, has no entry: as before)
            g_live.units = r.speed_units ? (r.speed_units() == 1 ? 1 : 0) : 0;
            if (g_live.units != 0 || units_recorded) {
                rrec->setting(v, "speed_units", std::to_string(g_live.units));
                units_recorded = true;
            }
            // gamemods1: START AT, from the first VBlank (read once, at the boot; a recording at the game's start has none)
            if (v == 0) {
                g_live.boot_to = r.boot_to ? r.boot_to() : 0;
                if (const int f = b3native::BootBoost::forced(); f >= 0) g_live.boot_to = f;
                if (g_live.boot_to != 0) rrec->setting(v, "boot_to", std::to_string(g_live.boot_to));
                // and whether the save holds its profile, read now and kept (a replay does not depend on the local save)
                g_live.boot_profile = g_live.boot_to >= 2 ? (start_at_profile_in_save(g_live.boot_to - 2) ? 1 : 0) : -1;
                if (g_live.boot_profile >= 0) rrec->setting(v, "boot_profile", std::to_string(g_live.boot_profile));
            }
            // gamemods1: IDLE PAUSE / END VERY LONG EVENTS, kept from the first VBlank they are off (none: the game's)
            g_live.idle_pause = r.idle_pause ? (r.idle_pause() == 0 ? 0 : 1) : 1;
            g_live.long_event_end = r.long_event_end ? (r.long_event_end() == 0 ? 0 : 1) : 1;
            if (g_live.idle_pause == 0 || limits_recorded) rrec->setting(v, "idle_pause", std::to_string(g_live.idle_pause));
            if (g_live.long_event_end == 0 || limits_recorded) rrec->setting(v, "long_event_end", std::to_string(g_live.long_event_end));
            limits_recorded = limits_recorded || g_live.idle_pause == 0 || g_live.long_event_end == 0;
            // hudmods: TAKEDOWN STREAK PITCH, kept from the first VBlank it is on (a recording with it off has no entry: as
            // before). It changes which of the game's takedown sounds play, so a replay plays them as recorded
            g_live.takedown_pitch = r.takedown_pitch ? (r.takedown_pitch() == 1 ? 1 : 0) : 0;
            if (g_live.takedown_pitch != 0 || takedown_pitch_recorded) {
                rrec->setting(v, "takedown_pitch", std::to_string(g_live.takedown_pitch));
                takedown_pitch_recorded = true;
            }
            rrec->setting(v, "music", std::to_string(g_live.music_mode) + "|" + g_live.music_dir);
            // musicvol1: CUSTOM MUSIC VOLUME, kept from the first VBlank it is not 100 % (a recording at 100 % has no
            // entry: as before). Sound only: a replay plays the custom songs as loud as they were recorded
            g_live.music_volume = r.music_volume ? r.music_volume() : 100;
            if (g_live.music_volume != 100 || music_volume_recorded) {
                rrec->setting(v, "music_volume", std::to_string(g_live.music_volume));
                music_volume_recorded = true;
            }
            g_live.music_loudness = r.music_loudness ? r.music_loudness() : 1;   // musicloud: as music_volume (Even out: none)
            if (g_live.music_loudness != 1 || music_loudness_recorded) {
                rrec->setting(v, "music_loudness", std::to_string(g_live.music_loudness));
                music_loudness_recorded = true;
            }
            // the music folder the game would play from (custom_soundtrack.h mode(): the environment wins)
            int mode = g_live.music_mode;
            std::string folder = g_live.music_dir;
            if (const char* e = std::getenv("B3NATIVE_MUSIC_MODE"); e && e[0] >= '0' && e[0] <= '2' && !e[1]) mode = e[0] - '0';
            if (const auto e = b3io::getenv("B3NATIVE_MUSIC_DIR")) folder = *e;
            if (mode != 0 && !folder.empty() && !rrec->replay().option("music_fp." + folder)) {
                rrec->replay().options.emplace_back("music_fp." + folder, music_fingerprint(folder));
                // eatrax1: the custom songs' EA TRAX modes as they are now (custom_soundtrack.h modes_digits)
                rrec->replay().options.emplace_back("music_modes." + folder, b3native::CustomSoundtrack::modes_digits(folder));
            }
        };
        bool replay_rumble = false;   // rumble1: playback's acknowledging mode (the recording's "rumble" setting)
        std::string replay_extra_race;   // carpacks: the recording's last race's extra cars ("extra_race" events)
        unsigned extra_race_serial = 0;
        auto apply_live = [&](std::uint64_t v) {   // playback: the recorded values from this VBlank on
            for (const b3native::replay::SettingOp* op : rplay->settings(v)) {
                if (op->key == "rumble") replay_rumble = op->value == "1";
                else if (op->key == "tune") {   // tune4: a tuning edit, written on this VBlank
                    b3tune::Event ev;
                    if (tuning && b3tune::parse_event_value(op->value, ev)) { ev.vblank = v; tuning->replayed(ev); }
                }
                else if (op->key == "photo_row") g_live.photo_row = op->value == "1";   // photorow1
                else if (op->key == "traffic_check") g_live.traffic_check = b3native::traffic_check_entry(op->value.c_str());   // trafficcheck
                else if (op->key == "game_mods") g_live.game_mods = std::uint32_t(std::strtoul(op->value.c_str(), nullptr, 10));   // options3
                else if (op->key == "game_options") g_live.game_options = b3native::game_options_entry(op->value.c_str());   // gameopts
                else if (op->key == "party_moon") g_live.party_moon = op->value == "1";   // party1
                else if (op->key == "extra_race") replay_extra_race = op->value;   // carpacks: a race's extra cars as recorded
                else if (op->key == "party_press") {   // party1: this VBlank's CRASH / AI DRIVER presses ("c<n> a<n>")
                    int c = 0, a = 0;
                    if (std::sscanf(op->value.c_str(), "c%d a%d", &c, &a) == 2) { g_live.party_crash += c; g_live.party_ai += a; }
                }
                else if (op->key == "race_time_row") g_live.race_time_row = op->value == "1";   // raceopts
                else if (op->key == "race_weather_row") g_live.race_weather_row = op->value == "1";   // condrows
                else if (op->key == "race_night_row") {}   // (the one-row CONDITIONS of extras' test builds: no meaning now)
                else if (op->key == "title_fast") g_live.title_fast = b3native::title_fast_entry(op->value.c_str());   // titlefast
                else if (op->key == "settings_back") g_live.settings_back = b3native::settings_back_entry(op->value.c_str());   // settingsback
                else if (op->key == "crash_ram_fix") g_live.crash_ram_fix = op->value == "1";   // crashramfix
                else if (op->key == "traffic_series") g_live.traffic_series = b3native::traffic_series_entry(op->value.c_str());   // trafficseries
                else if (op->key == "heavy_series") g_live.heavy_series = b3native::heavy_series_entry(op->value.c_str());   // heavyseries
                else if (op->key == "extras") g_live.extras = std::uint32_t(std::strtoul(op->value.c_str(), nullptr, 10)) & b3settings::kExtrasMask;   // extras
                else if (op->key == "date_format") g_live.date = std::atoi(op->value.c_str());
                else if (op->key == "speed_units") g_live.units = std::atoi(op->value.c_str()) == 1 ? 1 : 0;   // units1
                else if (op->key == "boot_to") g_live.boot_to = std::atoi(op->value.c_str());   // gamemods1
                else if (op->key == "boot_profile") g_live.boot_profile = op->value == "1" ? 1 : 0;
                else if (op->key == "idle_pause") g_live.idle_pause = op->value == "0" ? 0 : 1;
                else if (op->key == "long_event_end") g_live.long_event_end = op->value == "0" ? 0 : 1;
                else if (op->key == "car_stats") g_live.car_stats = op->value == "1";               // carstats1
                else if (op->key == "turn_signals") g_live.turn_signals = op->value == "1";         // blinkers1
                else if (op->key == "road_rage_parts") g_live.road_rage_parts = op->value == "1";   // rrparts1
                else if (op->key == "takedown_pitch") g_live.takedown_pitch = op->value == "1" ? 1 : 0;   // hudmods
                else if (op->key == "now_playing") g_live.now_playing = op->value;   // nowplaying1
                else if (op->key == "music_volume") g_live.music_volume = std::atoi(op->value.c_str());   // musicvol1
                else if (op->key == "music_loudness") g_live.music_loudness = op->value == "0" ? 0 : 1;   // musicloud
                else if (op->key == "race_time") g_live.race_time = std::atoi(op->value.c_str());   // todrace
                else if (op->key == "car_viewer") g_live.car_viewer_request = true;   // gamemods1
                else if (op->key == "extra_demote") {   // extras: a remembered extra car's slot given its donor
                    unsigned at = 0;
                    unsigned long long donor = 0;
                    if (std::sscanf(op->value.c_str(), "%8X:%16llX", &at, &donor) == 2) g_live.demotes.emplace_back(b3host::gaddr(at), std::uint64_t(donor));
                }
                else if (op->key == "replay") g_live.replay_presses += 1;   // replayview: a REPLAY press that was acted on
                else if (op->key == "shift") {   // gearbox: this VBlank's SHIFT UP / SHIFT DOWN presses ("u<n> d<n>")
                    unsigned u = 0, d = 0;
                    if (std::sscanf(op->value.c_str(), "u%u d%u", &u, &d) == 2) { g_live.shift_up += int(u); g_live.shift_down += int(d); }
                }
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
                         {"explore_movies", explore_movies ? "1" : "0"}, {"strict", strict ? "1" : "0"},
                         {"race_opts", race_opts ? "1" : "0"},   // raceopts: SELECT OPTIONS' rows (playback follows it)
                         {"single_eliminator", single_eliminator ? "1" : "0"},   // elim1: SELECT MODE's fifth row
                         {"more_rivals", more_rivals ? "1" : "0"},   // MORE RIVALS: RIVALS up to 7 (playback follows it)
                         {"ram_hash", "2"}};   // ghvariant: the checkpoints leave the program variants' places out
            if (extra_cars) h.options.emplace_back("extra_cars", extra_cars->entry());   // customseries: the cars (playback checks them)
            h.options.emplace_back("replays", replay_view ? "1" : "0");   // replayview: RACE REPLAY (its hooks installed at the start)
            for (const char* name : b3native::replay::kGameEnv)
                if (const char* e = std::getenv(name)) h.options.emplace_back(std::string("env.") + name, e);
            if (const std::string g = b3native::tuning_gameplay_ids(mod_overlay); !g.empty()) h.info.emplace_back("gameplay", g);   // tune4
            if (!g_voice_code.empty()) h.options.emplace_back("voice", g_voice_code);   // 1.3.0: the training's narration
            if (!g_voice_code.empty()) h.options.emplace_back("training_pictures", g_training_pictures ? "imported" : "english");
            if (const std::string dj = b3native::platform::disc::dj_voice(); !dj.empty()) {   // and the DJ, bank for bank
                h.options.emplace_back("dj_voice", dj);
                h.options.emplace_back("dj_banks", g_dj_banks);
            }
            if (!mod_overlay.empty()) {   // modkit1: the mod set (playback takes these ids and checks the hash)
                h.options.emplace_back("mods.ids", mod_ids);
                h.options.emplace_back("mods.hash", b3mod::hex64(mod_overlay.set_hash()));
            }
            if (const auto root_s = b3io::getenv("B3NATIVE_SAVES_ROOT")) {   // the card as the game gets it (ProfileCard::attach)
                const std::filesystem::path root = b3io::u8path(*root_s);
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
            rstream = std::make_unique<b3native::replay::StreamWriter>(replay_out, replay_auto ? replay_out.parent_path() : std::filesystem::path());
        }
        if (replay_in) {
            rplay = std::make_unique<b3native::replay::Player>(*replay_in);
            g_live.on = true;
            apply_live(0);
        }
        static int (*g_date_format)() = nullptr;   // mac5: SETTINGS > GAME > LANGUAGE AND UNITS > DATE FORMAT (tools/native/date_format.h)
        g_date_format = r.date_format;
        b3native::DateFormat date_format;
        date_format.install(hooks, [] { return g_live.on ? g_live.date : g_date_format ? g_date_format() : 0; });
        static int (*g_speed_units)() = nullptr;   // units1: SETTINGS > GAME > LANGUAGE AND UNITS > SPEED UNITS (tools/native/speed_units.h)
        g_speed_units = r.speed_units;
        b3native::SpeedUnits speed_units;
        speed_units.install(hooks, [] { return g_live.on ? g_live.units : g_speed_units ? g_speed_units() : 0; },
                            [] { return static_cast<std::uint64_t>(b3native::platform::vblank_count()); });
        static int (*g_boot_to)() = nullptr;   // gamemods1: PC SETTINGS > GAME > STARTING AND LOADING > START AT
        g_boot_to = r.boot_to;
        static b3native::BootBoost boot_boost;
        boot_boost.install(hooks, [] { return g_live.on ? g_live.boot_to : g_boot_to ? g_boot_to() : 0; }, [](int slot) {
            // a recording's (or this session's boot-time) answer; else the save now (an older recording: the local save)
            if (g_live.on && g_live.boot_profile >= 0) return g_live.boot_profile == 1;
            return start_at_profile_in_save(slot);
        });
        static int (*g_idle_pause)() = nullptr, (*g_long_event_end)() = nullptr;   // gamemods1: GAME > MODES
        g_idle_pause = r.idle_pause;
        g_long_event_end = r.long_event_end;
        static b3native::GameLimits game_limits;
        game_limits.install(hooks, [] { return g_live.on ? g_live.idle_pause : g_idle_pause ? g_idle_pause() : 1; },
                            [] { return g_live.on ? g_live.long_event_end : g_long_event_end ? g_long_event_end() : 1; });
        WorldTourRequest::install(hooks);   // extras: NIGHT / RAIN IN WORLD TOUR's events (the request noted)
        static b3native::CarViewer car_viewer;   // gamemods1: the retail car viewer, from YOUR GARAGE (car_viewer.h)
        car_viewer.install(hooks, [] { return static_cast<std::uint64_t>(b3native::platform::vblank_count()); });
        static int (*g_takedown_pitch)() = nullptr;   // hudmods: PC SETTINGS > AUDIO > SOUND > TAKEDOWN STREAK PITCH
        g_takedown_pitch = r.takedown_pitch;
        b3native::TakedownPitch takedown_pitch;
        takedown_pitch.install(hooks, [] { return g_live.on ? g_live.takedown_pitch : g_takedown_pitch ? g_takedown_pitch() : 0; },
                               [] { return static_cast<std::uint64_t>(b3native::platform::vblank_count()); });
        b3native::TrainingVoice training_voice;   // 1.3.0 VOICES: the race training's narration (English: nothing wrapped)
        if (!g_voice_code.empty()) training_voice.install(hooks, g_voice_code);
        // the ONLINE dialog's words: the port's own instead of the PS2's "no network adaptor" (tools/native/online_message.h;
        // B3NATIVE_ONLINE_MESSAGE=0: the game's text)
        b3native::OnlineMessage online_message;
        online_message.set_language(b3native::platform::disc::language_text());   // 1.1: a language pack's words
        online_message.install(hooks);
        static int (*g_music)(char*, int) = nullptr;   // PC SETTINGS > AUDIO > MUSIC (tools/native/custom_soundtrack.h)
        g_music = r.music;
        static int (*g_music_volume)() = nullptr;   // musicvol1: CUSTOM MUSIC VOLUME
        g_music_volume = r.music_volume;
        static int (*g_music_loudness)() = nullptr;   // musicloud: CUSTOM MUSIC LOUDNESS
        g_music_loudness = r.music_loudness;
        b3native::CustomSoundtrack custom_soundtrack;
        custom_soundtrack.set_volume_source([] {   // B3NATIVE_MUSIC_VOLUME=<0..200> overrides it (tests), as _MODE does
            static const int forced = [] {
                const char* e = std::getenv("B3NATIVE_MUSIC_VOLUME");
                return e && *e ? std::clamp(std::atoi(e), 0, 200) : -1;
            }();
            if (forced >= 0) return forced;
            if (g_live.on) return g_live.music_volume;   // replay1: the latched value
            return g_music_volume ? g_music_volume() : 100;
        });
        custom_soundtrack.set_loudness_source([] {   // B3NATIVE_MUSIC_LOUDNESS=0|1 overrides it (tests)
            static const int forced = [] {
                const char* e = std::getenv("B3NATIVE_MUSIC_LOUDNESS");
                return e && (*e == '0' || *e == '1') ? *e - '0' : -1;
            }();
            if (forced >= 0) return forced;
            if (g_live.on) return g_live.music_loudness;   // replay1: the latched value
            return g_music_loudness ? g_music_loudness() : 1;
        });
        if (!std::getenv("B3NATIVE_MUSIC_OFF"))   // B3NATIVE_MUSIC_OFF=1: not installed (comparisons)
            custom_soundtrack.install(hooks, [](char* folder, int max) {
                if (g_live.on) {   // replay1: the latched value
                    if (folder && max > 0) std::snprintf(folder, static_cast<std::size_t>(max), "%s", g_live.music_dir.c_str());
                    return g_live.music_mode;
                }
                return g_music ? g_music(folder, max) : 0;
            });
        // nowplaying1 (tools/native/now_playing_popup.h): another music app's song in the EA TRAX popup, latched per
        // VBlank below (PC SETTINGS > AUDIO > MUSIC > OTHER MUSIC APPS; no poller, no system calls while it is off)
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
            to.mods_dir = !mods_dir.empty() ? b3io::u8path(mods_dir) : b3native::save::user_data_dir().empty() ? std::filesystem::path()
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
            if (r.mod_texture) h.texture_free = [&r](std::uint32_t id) { r.mod_texture(id, 0, 0, nullptr); };   // (0 x 0, no pixels: drop it)
            const bool window_values = !offscreen && !mods_pref.empty();
            if (window_values) h.saved_values = [&](const std::string& id) { return b3mod::load_values(b3mod::settings_file(mods_pref, id)); };
            code_mods = std::make_unique<b3native::CodeMods>(hooks, b3native::guest_arena(), h);
            std::vector<b3native::CodeMods::Candidate> cands;
            std::vector<b3mod::ModState> state;
            std::set<std::string> left_out;
            for (const b3mod::ActiveMod& m : mod_overlay.mods()) {
                if (!m.code.present && !m.script.present) continue;
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
                if (m.script.present) {   // lua6: a script mod, the same loader (src/native/script_mods.h)
                    cands.push_back(b3native::script_candidate(m, developer_mods, code_digest.count(m.id) ? code_digest[m.id] : std::string()));
                    continue;
                }
                b3native::CodeMods::Candidate k{m.id, m.version, m.dir, m.library, m.code.platform};
                k.code = m.code;
                k.trust_digest = code_digest[m.id];   // checked again right before the library loads
                cands.push_back(std::move(k));
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
        b3native::BehindWatch behind;   // slowcpu: the game behind real time -> the renderer holds to 60 (fast_load.h)
        std::uint64_t behind_times = 0;
        double behind_s = 0.0;
        clock::time_point behind_since{};
        std::uint64_t stall_count = 0;           // stutter: runs of late VBlanks that fell more than 2 VBlanks behind
        double stall_max_ms = 0, stall_run_ms = 0;
        bool stall_run = false;
        std::uint64_t stall_from = 0, stall_len = 0;
        const std::vector<std::pair<std::uint64_t, int>> stall_inject = [] {   // B3NATIVE_STALL (checks only)
            std::vector<std::pair<std::uint64_t, int>> out;
            const char* e = std::getenv("B3NATIVE_STALL");
            for (const char* p = e; p && *p;) {
                char* q = nullptr;
                const unsigned long long at = std::strtoull(p, &q, 10);
                if (!q || *q != ':') break;
                const long ms = std::strtol(q + 1, &q, 10);
                if (ms > 0 && ms <= 5000) out.emplace_back(at, int(ms));
                p = q && *q == ',' ? q + 1 : nullptr;
            }
            std::sort(out.begin(), out.end());
            return out;
        }();
        std::size_t next_stall = 0;
        unsigned long long audio_late_seen = 0;  // stutter: the sound's dry runs logged so far
        b3native::FeedWait feed_wait;            // trafmem
        std::uint64_t feed_waits = 0;            // trafmem: the waits for the shot runner's full hold, and their time
        double feed_wait_ms = 0;
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
        b3native::PadContext pad_ctx = b3native::PadContext::kMenus;   // padskip (pad_context.h)
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
                // movielog (issue #17): one line per start, not per Prepare call. The game calls Prepare again until the
                // movie is ready (func_003AB0A0 busy-waits on it, CB3BackgroundComponent::Update once a frame), and while
                // the previous movie's file is still open (its close ends at the next VBlank) that is ~3000 calls in one
                // VBlank: a line and a flush each made it seconds of wall time (late VBlanks, resyncs, the sound dry).
                // The same movie called again within 2 VBlanks is the same start: counted, and summed up once.
                static b3native::MovieLog log;   // (movie_skip.h)
                const std::uint64_t v = b3native::platform::vblank_count();
                std::uint64_t retried = 0;
                if (!log.call(n, v, &retried)) return;
                if (retried)
                    std::printf("native_play: movie %s: %llu Prepare calls before it played (the game waits for its file)\n",
                                log.prev.c_str(), static_cast<unsigned long long>(retried));
                std::printf("native_play: VBlank %llu: movie %s (decoder 0x%08X, flags %u)\n", static_cast<unsigned long long>(v),
                            n.c_str(), a[0], a[5]);
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
        b3native::RaceTime race_time;   // todrace (race_time.h): DAY / NIGHT per race on the track pages
        b3native::PaintSelect paint_sel;   // paintsel (paint_select.h): the car paint panel, the game's colour choice
        int paint_page_given = -1;          // what the renderer was last told (b3r_paint_page)
        std::vector<std::uint8_t> carousel_last;   // paint3: the slots last published (serial 0), empty: none
        std::uint32_t carousel_serial = 0;
        bool elim_on = false;             // elim1: an Eliminator stage is on (single_eliminator.h racing())
        std::uint64_t records_word = 0;   // raceopts / elim1: the record word last given to the renderer (records.h)
        bool race_page = false, race_published = false;
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
        bool prepace_ff = false;   // ownertests: the display / sound as a fast load's (see below; --pace-from only)
        bool load_up = false, fast_setting = false, fast_setting_known = false;
        bool autosave_up = false, span_is_load = false;   // autosave1
        const bool page_log = std::getenv("B3NATIVE_PAGE_LOG") != nullptr;
        std::uint32_t page_logged = 0xFFFFFFFFu;
        std::uint8_t box_logged = 0xFF;
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
                auto rd = [](std::uint32_t a) { std::uint32_t v = 0; std::memcpy(&v, b3host::host_pointer_checked(a, 4), 4); return v; };
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
                // ghvariant: a recording's "ram_hash" 2 (every new one) leaves the program variants' places out; older ones not
                static const bool masked = !rplay || b3port::ram_hash_form(replay_in ? replay_in->option("ram_hash") : nullptr) == 1;
                const std::uint64_t hash = replay_ram_hash(masked);
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
            // audioq: SOUND OUTPUT (the sound only: the player's choice, a replay's too; the mixer takes it at its next VBlank)
            if (r.sound_output) b3native::platform::set_sound_output_original(r.sound_output() != 0);
            if (party) {   // party1: PARTY MODES' buttons for this VBlank (live: the window's; a replay: the recording's)
                bool moon = false;
                int crash = 0, ai = 0;
                if (rplay) {
                    moon = g_live.party_moon;
                    crash = g_live.party_crash;
                    ai = g_live.party_ai;
                    g_live.party_crash = g_live.party_ai = 0;
                } else if (r.party_keys) {
                    const int k = r.party_keys();
                    moon = (k & 1) != 0;
                    crash = k >> 8 & 0xFF;
                    ai = k >> 16 & 0xFF;
                }
                if (game_options) game_options->refresh();
                const unsigned w = game_options ? game_options->latched() : 0u;
                if (rrec) {   // MOON JUMP held: a setting (each change); each press: an event of its VBlank
                    if (moon != party_moon_recorded) {
                        rrec->setting(v, "party_moon", moon ? "1" : "0");
                        party_moon_recorded = moon;
                    }
                    if (crash || ai) rrec->event(v, "party_press", "c" + std::to_string(crash) + " a" + std::to_string(ai));
                }
                if (replay_view && replay_view->showing()) moon = false, crash = ai = 0;   // replayview: none while a replay plays
                party->set_input(w, moon, crash, ai);
                if (replay_view && (moon || crash || ai)) replay_view->spoil("PARTY MODES' buttons were used (not in the game's recording)");   // replayview
            }
            if (traffic_cars) {   // party1: TUK-TUKS (PARTY MODES' word bit 6): every race car of a SINGLE EVENT race the
                                  // tuk-tuk, trafficseries' traffic vehicle as a race car (set for RequestOnePlayerGameMode,
                                  // which reads it; the race keeps its records apart as a traffic vehicle's)
                const bool tuk = ((g_live.on ? g_live.game_options : game_options_wanted()) & b3native::kPartyTuk) != 0;
                if (tuk != tuk_set) {
                    tuk_set = tuk;
                    const int k = tuk ? b3native::traffic_index(b3native::gtid_compress("COMPCAR18")) : -1;
                    traffic_cars->set_race_cars(k, k);
                    std::printf("native_play: VBlank %llu: party_modes: TUK-TUKS %s\n", static_cast<unsigned long long>(v),
                                tuk ? "on (SINGLE EVENT races: every race car the tuk-tuk, COMPCAR18)" : "off");
                    std::fflush(stdout);
                }
            }
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
            b3native::sampler::vblank(v);   // slowcpu (B3_SAMPLE_FROM)
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
                if (pacer.on) b3gv_native::set_vblank_stamp(std::uint64_t(pacer.stamp_ns()));   // stutter: no poll with the old stamp
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
                    if (behind.reset()) {   // slowcpu: a new race starts from the first hold again
                        if (r.game_behind) r.game_behind(0);
                        behind_s += std::chrono::duration<double>(now - behind_since).count();
                    }
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
            const bool autosave = hide_wanted && b3native::autosave_page_up(b3host::load<std::uint32_t>(b3native::kMenuCurrentPage)) &&
                                  !b3native::message_box_up(b3host::load<std::uint8_t>(b3native::kMessageShown));   // saveflow
            if (page_log) {   // B3NATIVE_PAGE_LOG=1: each change of the menu's current page (autosave1: which page is which)
                const std::uint32_t cur = b3host::load<std::uint32_t>(b3native::kMenuCurrentPage);
                const std::uint8_t box = b3host::load<std::uint8_t>(b3native::kMessageShown);   // saveflow: and the card box's
                if (cur != page_logged || box != box_logged) {
                    page_logged = cur;
                    box_logged = box;
                    std::printf("native_play: VBlank %llu: menu page %08X, card message box %s\n", static_cast<unsigned long long>(v), cur,
                                b3native::message_box_up(box) ? "up" : "down");
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
            // trafmem: an offscreen shot runner whose hold is full (a shot still pending far behind this unpaced game: the
            // feed keeps every race tick from the shot's warm-up on, ~0.3 MB each with TRAFFIC Extreme) holds the game
            // here until the shot is taken (b3r_feed_full; wall time only, as the scene wait below: the same VBlanks with
            // the same state). As long as the hold moves (the renderer taking its shots); 120 s without it (a renderer
            // that stopped drawing): then on, and the feed's byte bound drops the oldest.
            // (Fable: a wait that ran out is not started again for that hold: FeedWait)
            if (r.feed_full && feed_wait.should_wait(r.feed_full() != 0, r.feed_hold ? r.feed_hold() : 0)) {
                const auto t0 = clock::now();
                auto moved = t0;
                unsigned hold = r.feed_hold ? r.feed_hold() : 0;
                const auto limit = std::chrono::nanoseconds(b3native::FeedWait::kLimitNs);
                while (r.feed_full() && !r.quit_requested() && clock::now() - moved < limit) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    if (const unsigned h = r.feed_hold ? r.feed_hold() : hold; h != hold) { hold = h; moved = clock::now(); }
                }
                if (r.feed_full() && clock::now() - moved >= limit) {
                    feed_wait.ran_out(hold);
                    std::fprintf(stderr, "native_play: VBlank %llu: the renderer's shot hold has not moved for %lld s: the game goes on (the feed drops its oldest ticks)\n",
                                 static_cast<unsigned long long>(v), static_cast<long long>(b3native::FeedWait::kLimitNs / 1000000000));
                }
                const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
                ++feed_waits;
                feed_wait_ms += ms;
                if (feed_waits <= 5)
                    std::printf("native_play: VBlank %llu: %.0f ms waiting for the renderer's shot (its feed held the ticks it needs)%s\n",
                                static_cast<unsigned long long>(v), ms, feed_waits == 5 ? " (the next ones only counted)" : "");
            }
            // loadlag1 (fast_load.h scene_hold_ms): the load is over but the renderer may still be building the race's
            // scene (its route came ~0.2-0.4 s before this VBlank, the build takes longer): the game waits here, before
            // its first race VBlank, so that the race is not heard under the loading screen. Wall time only (as the
            // pacer's own sleep): the same VBlanks with the same state; the load's sound is muted until this VBlank.
            if (fl.ended && !loading && span_is_load && r.scene_wait) {
                const double spent = std::chrono::duration<double>(clock::now() - load_wall_from).count();
                const int cap = b3native::scene_hold_ms(double(v - load_from) / hz, spent);
                const int waited = r.scene_wait(cap);
                // scenewait: past the cap only while the renderer was still building the route or loading the race's
                // vehicles (src/b3replay/fastforward.h scene_wait_more: up to 10 s)
                std::printf("native_play: VBlank %llu: fast loading: %d ms waiting for the renderer's race scene (at most %d ms%s)\n",
                            static_cast<unsigned long long>(v), waited, cap,
                            waited > cap + 50 ? "; waited on: its route or the vehicles were still being prepared" : "");
                std::fflush(stdout);
            }
            // ownertests (tools/release/owner_tests): with --pace-from, the unpaced stretch before it (a scripted front end,
            // several times real time) is shown and heard as a fast load is: the newest frame, the sound muted (and not
            // counted as dry), the ticks unstamped; the paced race after it starts as after any fast load. Without
            // --pace-from the block below is the one that runs, as before
            if (pace_from) {
                const bool want = fl.fast || (pace && v < pace_from);
                if (want != prepace_ff) {
                    prepace_ff = want;
                    b3gv_native::set_unstamped(want);
                    if (r.fast_forward) r.fast_forward(want ? 1 : 0);
                    sink.gate.set_muted(want);
                    if (r.audio_quiet) r.audio_quiet(want ? 1 : 0);
                }
            } else if (fl.began || fl.ended) {
                b3gv_native::set_unstamped(fl.fast);   // the load's ticks carry no latch stamp
                if (r.fast_forward) r.fast_forward(fl.fast ? 1 : 0);   // the display: the newest frame
                sink.gate.set_muted(fl.fast);          // its sound muted (a 1-VBlank fade each way)
                // loadaudio: the output's dry run from here to the span's end (the scene wait above included) is this
                // silence, not the game late (the device's count; the sound is the same)
                if (r.audio_quiet) r.audio_quiet(fl.fast ? 1 : 0);
            }
            // play1's pacing (fast_load.h Pacer): this VBlank waits for its wall-clock time; fastload1: not while loading,
            // and the first VBlank after a load re-anchors (no catch-up). --pace-from: unpaced before N.
            if (pace) {
                if (perf) perf_pace_from = ns_of(clock::now());
                const std::int64_t due_ns = pacer.vblank(ns_of(clock::now()), pace_from && v < pace_from, pace_from && pacer.paced == 0, fl);
                if (due_ns != b3native::Pacer::kNow) {
                    b3native::sampler::idle(true);   // slowcpu: the wait is not the game's work
                    struct IdleEnd { ~IdleEnd() { b3native::sampler::idle(false); } } idle_end;
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
                // slowcpu: behind real time (a slow CPU's race) -> the renderer draws no faster than the game meanwhile
                if (!fl.fast && !(pace_from && v < pace_from) && behind.step(v, pacer.behind, pacer.resyncs)) {
                    if (r.game_behind) r.game_behind(behind.behind ? 1 : 0);
                    if (behind.behind) {
                        behind_since = clock::now();
                        if (++behind_times <= 5)
                            std::printf("native_play: VBlank %llu: the game is behind real time (%.0f ms late): the renderer holds to 60 "
                                        "frames a second until it keeps up for %s%s\n",
                                        static_cast<unsigned long long>(v), double(pacer.behind) * 1e-6,
                                        behind.hold() == b3native::BehindWatch::kForever ? "the rest of the race"
                                        : behind.hold() == 180 ? "3 s" : behind.hold() == 600 ? "10 s" : "30 s",
                                        behind_times == 5 ? " (not logged again)" : "");
                    } else {
                        behind_s += std::chrono::duration<double>(clock::now() - behind_since).count();
                        if (behind_times <= 5)
                            std::printf("native_play: VBlank %llu: the game keeps up again: the renderer's frame limit as set\n",
                                        static_cast<unsigned long long>(v));
                    }
                    std::fflush(stdout);
                }
            }
            // stutter (checks only): $B3NATIVE_STALL=<VBlank>:<ms>[,...]: the game thread stops that long at that VBlank,
            // after its pacing wait (a hitch: the display clock's margin must not keep it)
            while (next_stall < stall_inject.size() && stall_inject[next_stall].first <= v) {
                if (stall_inject[next_stall].first == v) {
                    std::printf("native_play: VBlank %llu: B3NATIVE_STALL: the game thread stops %d ms\n", static_cast<unsigned long long>(v),
                                stall_inject[next_stall].second);
                    std::this_thread::sleep_for(std::chrono::milliseconds(stall_inject[next_stall].second));
                }
                ++next_stall;
            }
            // stutter: the polls' latch stamps: this VBlank's place on the schedule (0 unpaced: the poll's own time),
            // marked when it started late
            b3gv_native::set_vblank_stamp(pace && pacer.on ? std::uint64_t(pacer.stamp_ns()) : 0, pace && pacer.on && pacer.late());
            // stutter: a stall (the game thread more than 2 VBlanks behind the schedule: the game, its sound and the
            // display all wait): one line for each run of late VBlanks, when it has caught up (at most 20; the rest
            // counted), and each time the sound ran dry, with the VBlank (the run log lines up the two)
            if (pace && pacer.on) {
                const double behind_ms = double(pacer.behind) * 1e-6;
                if (!stall_run && behind_ms > 2.0 * double(pacer.period) * 1e-6) { stall_run = true; stall_from = v; stall_len = 0; stall_run_ms = 0; }
                if (stall_run && pacer.late()) {
                    ++stall_len;
                    stall_run_ms = std::max(stall_run_ms, behind_ms);
                } else if (stall_run) {
                    stall_run = false;
                    ++stall_count;
                    stall_max_ms = std::max(stall_max_ms, stall_run_ms);
                    if (stall_count <= 20)
                        std::printf("native_play: VBlank %llu: a stall: the game thread fell %.1f ms behind its schedule, %s after %llu VBlanks%s\n",
                                    static_cast<unsigned long long>(stall_from), stall_run_ms, stall_run_ms > 250.0 ? "re-anchored" : "caught up",
                                    static_cast<unsigned long long>(stall_len), stall_count == 20 ? " (the next ones only counted)" : "");
                }
            }
            if (r.audio_late) {
                const unsigned long long u = r.audio_late();
                if (u > audio_late_seen) {
                    if (audio_late_seen < 20)
                        // (loadaudio: the game late only; a fast load's silence is not counted, b3r_audio_quiet)
                        std::printf("native_play: VBlank %llu: the sound ran dry (#%llu: the game's sound late; the device played silence until the queue refilled)\n",
                                    static_cast<unsigned long long>(v), u);
                    audio_late_seen = u;
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
                // padskip (pad_context.h): not in the game's replays (they re-run the player's car through the same calls) nor
                // while a film plays: the menus' bindings there (the orbit camera still taken in a replay)
                const b3native::PadContext ctx = b3native::pad_context(accel_seen != 0 && v - accel_vblank < b3native::kAccelRecentVBlanks,
                                                                       b3host::load<std::uint8_t>(b3native::kReplayPlayback) != 0,
                                                                       skip.film_playing());
                const bool driving = ctx == b3native::PadContext::kDriving;
                if (pad_driving) ++drive_vblanks;
                if (driving && !pad_driving) ++drive_starts;   // replay1: the file's notes
                if (ctx != pad_ctx) {
                    pad_ctx = ctx;
                    pad_driving = driving;
                    if (r.pad_context) r.pad_context(int(ctx));
                    std::printf("native_play: VBlank %llu: keys: %s\n", static_cast<unsigned long long>(v), b3native::pad_context_name(ctx));
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
            if (r.effects_volume) {   // weather (§38): the game's EFFECTS volume [C func_0028B380: D_004F5074 x 0.01 to the
                // effects' mix; func_003F2970 / 003F2FE0: the AUDIO page's row 0, 0..100 in steps of 5], read only, to the
                // rain's sound (b3render NativeAudio: the rain in the same mix as the game's sound effects)
                static int effects_sent = -1;
                const int ev = int(b3host::load<std::uint8_t>(0x004F5074u));
                if (ev != effects_sent) {
                    effects_sent = ev;
                    r.effects_volume(std::min(ev, 100));
                }
            }
            {   // todrace (race_time.h): DAY / NIGHT per race on the track pages. Live: the window's presses there; a
                // replay: the recorded choice ("race_time", from the first VBlank it was not Default). The renderer gets
                // the race's value and the label (nothing published before the page or a choice first shows)
                const std::uint32_t page = b3host::load<std::uint32_t>(b3native::kMenuCurrentPage);
                const std::uint32_t state = b3host::load<std::uint32_t>(b3native::kPageState);
                // raceopts: SELECT OPTIONS' TIME OF DAY / WEATHER rows (race_options.h) replace the label and its presses;
                // the main menu puts the page's choices back to the defaults (no choice reaches World Tour or MULTIPLAYER)
                const bool time_row = race_opts && (g_live.on ? g_live.race_time_row || g_live.race_weather_row
                                                              : race_time_row_wanted() || race_weather_row_wanted());
                if (race_opts && (page & 0x01FFFFFFu) == b3native::kRaceTimeMainMenu) race_opts->reset();
                const bool label_way = kRaceTimeThisRelease && !time_row;
                const bool on_page = label_way && b3native::race_time_page(page);   // (1.3.0: held back with night)
                if (on_page != race_page) {
                    race_page = on_page;
                    if (r.race_time_page) r.race_time_page(on_page ? 1 : 0);
                }
                int presses = label_way && r.race_time_presses ? r.race_time_presses() : 0;
                const int choice_before = race_time.choice();
                if (rplay) {
                    presses = 0;
                    race_time.set_choice(g_live.race_time);
                } else if (time_row) {
                    race_time.set_choice(race_opts->choice().time);
                }
                {   // extras: EXPERIMENTAL NIGHT / RAIN IN WORLD TOUR -- a World Tour event's conditions as its loading screen
                    // comes up (the race takes the choice there): Always, or Random. Drawn only. A replay plays the recorded
                    // choice ("race_time", set above), so nothing here runs then.
                    // - A World Tour event: CB3MenuChoices +4 (the top menu, set when it is chosen: 0 World Tour, 1 Single
                    //   Event, 2 Multiplayer) is World Tour's AND the one-player stage record (RequestOnePlayerGameMode
                    //   0x30C2C0 fills it before the load) is World Tour's (+0x1B8 0x0051B928): a stale record alone (a later
                    //   two-player or online load) is not one. The game's current mode is still the front end's then.
                    // - Random: one roll per event, from the load's VBlank; a restart (a load with no front end page since
                    //   the last one) keeps it, a new event (the globe between) rolls again.
                    // - Any other load: a choice left from a World Tour event goes back to Default (the setting's), so it
                    //   never reaches a Crash event, a two-player race or online; SINGLE EVENT keeps its page's choice.
                    static bool wt_loading = false, fe_since_load = true, have_roll = false, roll_night = false, roll_rain = false;
                    const bool loading_now = b3native::loading_page_up(page);
                    if (!loading_now && (b3host::load<std::uint32_t>(0x0051BA88u) & 0x01FFFFFFu) == 0x0051A6A8u)   // CB3FrontEnd
                        fe_since_load = true;
                    if ((page & 0x01FFFFFFu) == b3native::kRaceTimeMainMenu || (page & 0x01FFFFFFu) == 0x01E97E1Cu)   // main menu, title
                        WorldTourRequest::asked = false;
                    if (!rplay && loading_now && !wt_loading) {
                        const std::int32_t top = b3host::load<std::int32_t>(0x01E900B0u + 4u);
                        // the load's kind (fast_load.h kLoadingKind): 2 into a race; 1 after one (to the menus, or a RESTART
                        // into the same event); 3 between a Crash event's parts. A World Tour event's value is set on each of
                        // its loads (a restart's too; the front end draws by day whatever it is); any other load, of any
                        // kind, that is not SINGLE EVENT's with its row resets a left choice
                        const std::uint32_t kind = b3host::load<std::uint32_t>(b3native::kLoadingKind);
                        const bool world_tour = WorldTourRequest::asked && kind >= 1 && kind <= 3 && top == 0 &&
                                                b3host::load<std::uint32_t>(0x0051AD00u + 0x1B8u) == 0x0051B928u;
                        const std::uint32_t w = extras_now();
                        const int nw = w & b3settings::kExtraNight ? b3settings::extras_world_tour_night(w) : 0;
                        const int rw = w & b3settings::kExtraRain ? b3settings::extras_world_tour_rain(w) : 0;
                        if (world_tour && (nw || rw)) {
                            const bool again = !fe_since_load && have_roll;
                            if (!again) {
                                const std::uint32_t h = std::uint32_t(v) * 2654435761u;
                                roll_night = h >> 31;
                                roll_rain = h >> 30 & 1u;
                                have_roll = true;
                            }
                            const bool night = nw == 1 || (nw == 2 && roll_night);
                            const bool rain = rw == 1 || (rw == 2 && roll_rain);
                            race_time.set_choice(night && rain ? b3native::kRaceTimeNightRain : night ? b3native::kRaceTimeNight
                                                 : rain ? b3native::kRaceTimeRain : b3native::kRaceTimeDay);
                            std::printf("native_play: VBlank %llu: time of day: a World Tour event %s (NIGHT %s, RAIN %s%s)\n",
                                        static_cast<unsigned long long>(v), b3native::race_time_name(race_time.choice()),
                                        nw == 1 ? "Always" : nw == 2 ? "Random" : "Off", rw == 1 ? "Always" : rw == 2 ? "Random" : "Off",
                                        again && (nw == 2 || rw == 2) ? "; a restart: the same roll" : "");
                        } else if (!(time_row && top == 1) && race_time.choice() != b3native::kRaceTimeDefault) {   // (any kind: online,
                            // two-player, the attract demo never inherit it)
                            race_time.set_choice(b3native::kRaceTimeDefault);
                            std::printf("native_play: VBlank %llu: time of day: a load that is not a World Tour event: Default\n",
                                        static_cast<unsigned long long>(v));
                        }
                        fe_since_load = false;
                    }
                    wt_loading = loading_now;
                }
                const b3native::RaceTime::Step st = race_time.step(page, state, b3native::loading_page_up(page), presses);
                if (st.choice_changed || race_time.choice() != choice_before) {   // (a replay's: set before the step)
                    std::printf("native_play: VBlank %llu: time of day: %s (%s)\n", static_cast<unsigned long long>(v),
                                b3native::race_time_name(race_time.choice()),
                                (page & 0x01FFFFFFu) == b3native::kRaceTimeMainMenu ? "the main menu: back to the setting" :
                                rplay ? "the replay's" :
                                time_row ? ((page & 0x01FFFFFFu) == b3native::kSelectOptionsPage ? "SELECT OPTIONS' row" : "reset with SELECT OPTIONS")
                                         : "a press on the track page");
                    std::fflush(stdout);
                    if (rrec && (race_time_recorded || race_time.choice() != b3native::kRaceTimeDefault)) {
                        rrec->setting(v, "race_time", std::to_string(race_time.choice()));
                        race_time_recorded = true;
                    }
                    if (!rplay) g_live.race_time = race_time.choice();
                }
                if (race_opts && race_opts->take_changed()) {   // raceopts: the page's choice (the log; the replay's entry)
                    const b3native::RaceChoice& c = race_opts->choice();
                    std::printf("native_play: VBlank %llu: race options: %s (traffic %s, laps %d, road rage rivals %d)\n",
                                static_cast<unsigned long long>(v), c.code().c_str(), b3native::race_traffic_name(c.traffic),
                                int(c.laps), c.rivals ? int(c.rivals) : b3native::kRoadRageRivals);
                    std::fflush(stdout);
                }
                if (race_opts) {   // raceopts: the race on now and its options (the renderer keeps its records apart)
                    const bool active = race_opts->race_active();
                    if (active != race_opts_active) {   // the Single Event stage's AI cars [C RequestOnePlayerGameMode +0xA4]
                        race_opts_active = active;
                        if (active)
                            std::printf("native_play: VBlank %llu: race options: a SINGLE EVENT stage (mode %d, %d AI cars, options %s)\n",
                                        static_cast<unsigned long long>(v), race_opts->mode(), b3host::load<std::int32_t>(0x0051ADA4u),
                                        race_opts->choice().code().c_str());
                    }
                    const std::uint64_t w = race_opts->race_record_word();
                    if (w != race_opts_word) {
                        race_opts_word = w;
                        std::printf("native_play: VBlank %llu: race options: %s\n", static_cast<unsigned long long>(v),
                                    w ? ("the race takes " + race_opts->choice().code() + ": records kept apart").c_str()
                                      : "the game's own race");
                        std::fflush(stdout);   // (the word goes to the renderer below, with the Eliminator's bit)
                    }
                }
                if (traffic_check) {   // trafficcheck: the race's option (latched at its start) and the log (its records bits: below)
                    traffic_check->refresh();
                    const auto& tc = traffic_check->counts();   // (each event stamped with its VBlank: the shots' times)
                    const std::uint64_t ev[6] = {tc.checks, tc.trick_shots + tc.skill_shots + tc.extreme_shots, tc.takedowns, tc.own_touches, tc.chains, tc.signs};
                    static std::uint64_t ev_seen[6] = {};
                    static const char* const kEv[6] = {"check", "shot", "takedown credited", "own wreck touched", "chain over", "sign"};
                    for (int i = 0; i < 6; ++i)
                        if (ev[i] != ev_seen[i]) {
                            ev_seen[i] = ev[i];
                            std::printf("native_play: VBlank %llu: traffic_check: %s (%llu in this run)\n", static_cast<unsigned long long>(v),
                                        kEv[i], static_cast<unsigned long long>(ev[i]));
                            std::fflush(stdout);
                        }
                    if (traffic_check->mode() != traffic_check_shown) {
                        traffic_check_shown = traffic_check->mode();
                        std::printf("native_play: VBlank %llu: traffic_check: %s%s\n", static_cast<unsigned long long>(v),
                                    traffic_check_shown ? "TRAFFIC CHECKING in this race: " : "off",
                                    traffic_check_shown ? (std::string(b3native::traffic_check_name(traffic_check_shown)) + " (records kept apart)").c_str() : "");
                        std::fflush(stdout);
                    }
                }
                if (st.race_changed) {
                    std::printf("native_play: VBlank %llu: time of day: the race takes %s\n", static_cast<unsigned long long>(v),
                                b3native::race_time_name(race_time.race()));
                    std::fflush(stdout);
                }
                // B3_RACE_TIME_LABEL=0 (the golden record's pin, tools/golden/baseline.sh): no label (the choice still works)
                static const bool label_on = [] { const char* e = std::getenv("B3_RACE_TIME_LABEL"); return !(e && e[0] == '0' && !e[1]); }();
                const std::uint8_t opacity = label_on && label_way ? race_time.opacity() : 0;   // (raceopts: none without the label's way)
                // (1.3.0: held back with night: nothing published at all, so no label on the track pages; raceopts: the
                // TIME OF DAY row's tests (B3_RACE_TIME_ROW=1) publish the race's value, never a label)
                if ((kRaceTimeThisRelease || time_row) && (race_published || race_time.race() != b3native::kRaceTimeDefault || opacity != 0 ||
                                                           race_time.choice() != b3native::kRaceTimeDefault)) {
                    race_published = true;
                    b3gv_native::set_race_time(std::uint8_t(race_time.race()), std::uint8_t(race_time.choice()), opacity);
                }
            }
            if (road_rage_parts) road_rage_parts->refresh();   // rrparts1: its records latch ends with the stage
            if (game_options) {   // gameopts: the race's options (latched at its start), the log and the renderer's word
                game_options->refresh();
                if (game_options->counts().fired != game_options_fired) {
                    game_options_fired = game_options->counts().fired;
                    std::printf("native_play: VBlank %llu: game_options: a Crashbreaker went off (%llu in this run)\n",
                                static_cast<unsigned long long>(v), static_cast<unsigned long long>(game_options_fired));
                    std::fflush(stdout);
                }
                const unsigned now = game_options->latched() | game_options->record_bits() << 16;
                if (now != game_options_shown) {
                    game_options_shown = now;
                    std::printf("native_play: VBlank %llu: game_options: %s%s\n", static_cast<unsigned long long>(v),
                                b3native::game_options_name(game_options->latched()).c_str(),
                                game_options->record_bits() ? " (records kept apart)" : "");
                    std::fflush(stdout);
                    b3gv_native::set_game_options(game_options->latched());   // (the mirror; records: the word below)
                }
            }
            {   // titlefast: when the title takes input and when its PRESS START prompt shows (the log; read only, either way)
                const bool on_title = (b3host::load<std::uint32_t>(b3native::kMenuCurrentPage) & 0x01FFFFFFu) == b3native::kTitlePage;
                if (!on_title) title_seen = 0;
                else {
                    const bool input = b3host::load<std::uint32_t>(0x01E90424u) == 5u;
                    const std::uint32_t prompt = b3host::load<std::uint32_t>(b3native::kTitleComponent + 0x1Cu) & 0x01FFFFFFu;
                    const float alpha = prompt ? b3host::load<float>(prompt + 0x10u + 0xCu) : 0.0f;
                    if (input && title_seen == 0) {
                        title_seen = 1;
                        title_input_vblank = v;
                        std::printf("native_play: VBlank %llu: the title takes input\n", static_cast<unsigned long long>(v));
                    }
                    if (input && title_seen == 1 && alpha > 0.0f) {
                        title_seen = 2;
                        std::printf("native_play: VBlank %llu: the title's PRESS START prompt shows (%llu VBlanks after it takes input)\n",
                                    static_cast<unsigned long long>(v), static_cast<unsigned long long>(v - title_input_vblank));
                    }
                    std::fflush(stdout);
                }
            }
            if (extra_cars) {   // customseries: the vehicle list's entries once the game has it; SELECT CAR on CUSTOM (the log)
                extra_cars->tick();
                // carpacks (Fable's review): a race's extra cars by their bytes -- recorded with the replay; on playback the
                // same files or the replay is reported as parted from its recording
                if (extra_cars->race_serial() != extra_race_serial) {
                    extra_race_serial = extra_cars->race_serial();
                    const std::string& now_cars = extra_cars->race_entry();
                    if (rrec && !now_cars.empty()) rrec->event(v, "extra_race", now_cars);
                    // (a recording with no extra_race event -- one from before carpacks -- is not compared; the event is read by
                    // apply_live in this same hook, before this test, so a race's own event is in hand here)
                    if (rplay && !now_cars.empty() && !replay_extra_race.empty() && now_cars != replay_extra_race) {
                        std::printf("native_play: replay: VBlank %llu: the race's extra car files are not the ones it was recorded with "
                                    "(%s; recorded: %s)\n", static_cast<unsigned long long>(v), now_cars.c_str(),
                                    replay_extra_race.empty() ? "none" : replay_extra_race.c_str());
                        if (!replay_parted) replay_parted = v;
                    }
                }
                const bool up = (b3host::load<std::uint32_t>(b3native::kMenuCurrentPage) & 0x01FFFFFFu) == b3native::kCarSelectPage;
                const std::uint32_t now = up ? b3host::load<std::uint32_t>(b3native::kCarouselSeries) << 16 |
                                                   (b3host::load<std::uint32_t>(b3native::kCarouselCar) & 0xFFFFu)
                                             : 0xFFFFFFFFu;
                if (now != extra_shown) {
                    if (up && b3native::extra_series(int(now >> 16))) {
                        const std::vector<int> cars = b3native::extra_series_cars(extra_cars->cars(), int(now >> 16));
                        const std::uint32_t row = now & 0xFFFFu;
                        const int code = int(now >> 16);   // carpacks: CUSTOM, CUSTOM 2, ..., BETA
                        const std::string page = std::string(b3native::extra_series_kind(code) == b3native::kCustomSeries ? "CUSTOM" : "BETA") +
                                                 (b3native::extra_series_page(code) ? " " + std::to_string(b3native::extra_series_page(code) + 1) : "");
                        std::printf("native_play: VBlank %llu: extra_cars: SELECT CAR shows %s, car %u (%s)\n", static_cast<unsigned long long>(v),
                                    page.c_str(), row,
                                    row < cars.size() ? extra_cars->cars()[std::size_t(cars[row])].source.c_str() : "?");
                        std::fflush(stdout);
                    }
                    extra_shown = now;
                }
            }
            if (traffic_cars) {   // trafficseries: SELECT CAR on a traffic series (the log's proof), read only
                const bool up = (b3host::load<std::uint32_t>(b3native::kMenuCurrentPage) & 0x01FFFFFFu) == b3native::kCarSelectPage;
                const std::uint32_t now = up ? b3host::load<std::uint32_t>(b3native::kCarouselSeries) << 16 |
                                                   (b3host::load<std::uint32_t>(b3native::kCarouselCar) & 0xFFFFu)
                                             : 0xFFFFFFFFu;
                if (now != traffic_shown) {
                    if (up && b3native::traffic_class(int(now >> 16))) {
                        const std::vector<int> cars = b3native::traffic_series_cars(int(now >> 16));
                        const std::uint32_t row = now & 0xFFFFu;
                        std::printf("native_play: VBlank %llu: traffic_cars: SELECT CAR shows %s, car %u (%s)\n", static_cast<unsigned long long>(v),
                                    (now >> 16) == std::uint32_t(b3native::kTrafficClass) ? "TRAFFIC" : "TRAFFIC SPECIAL", row,
                                    row < cars.size() ? b3native::traffic_table()[std::size_t(cars[row])].name().c_str() : "?");
                        std::fflush(stdout);
                    }
                    traffic_shown = now;
                }
            }
            if (heavy_race) {   // heavyseries: SELECT CAR on the HEAVYWEIGHTS series (the log's proof), read only
                const bool up = (b3host::load<std::uint32_t>(b3native::kMenuCurrentPage) & 0x01FFFFFFu) == b3native::kCarSelectPage;
                const std::uint32_t now = up ? b3host::load<std::uint32_t>(b3native::kCarouselSeries) << 16 |
                                                   (b3host::load<std::uint32_t>(b3native::kCarouselCar) & 0xFFFFu)
                                             : 0xFFFFFFFFu;
                if (now != heavy_shown) {
                    if (up && now >> 16 == std::uint32_t(b3native::kHeavyClass))
                        std::printf("native_play: VBlank %llu: heavy_series: SELECT CAR shows HEAVYWEIGHTS, car %u\n", static_cast<unsigned long long>(v), now & 0xFFFFu);
                    heavy_shown = now;
                }
            }
            {   // paintsel (paint_select.h): SELECT CAR's player for the panel's button, the game's colour choice, the prompt
                const std::uint32_t page = b3host::load<std::uint32_t>(b3native::kMenuCurrentPage);
                const std::uint32_t state = b3host::load<std::uint32_t>(b3native::kPageState);
                const bool sel = (page & 0x01FFFFFFu) == b3native::kCarSelectPage;
                const int player = sel ? int(static_cast<std::int8_t>(b3host::load<std::uint8_t>(b3native::kCarSelectPlayer))) : 0;
                const std::uint32_t series = sel ? b3host::load<std::uint32_t>(b3native::kCarouselSeries) : 0;
                const std::uint32_t row = sel ? b3host::load<std::uint32_t>(b3native::kCarouselCar) : 0;
                const int colour = sel ? int(static_cast<std::int8_t>(b3host::load<std::uint8_t>(b3native::carousel_colour_addr(row)))) : 0;
                const b3native::PaintSelect::Step st = paint_sel.step(page, state, player, series, row, colour);
                const bool live = replay_pad.empty() && !rplay;   // a replay: no panel, the settings untouched
                const int give = live ? st.page : -1;
                if (give != paint_page_given) {
                    paint_page_given = give;
                    if (r.paint_page) r.paint_page(give);
                    std::printf("native_play: VBlank %llu: SELECT CAR %s\n", static_cast<unsigned long long>(v),
                                give >= 0 ? (give == 1 ? "takes input (player 2's): the car paint panel's button on" : "takes input (player 1's): the car paint panel's button on")
                                          : "gone (the car paint panel's button off)");
                    std::fflush(stdout);
                }
                if (st.colour_player >= 0) {
                    std::printf("native_play: VBlank %llu: SELECT CAR: the game's colour %d -> %d (player %d, series %u car %u)%s\n",
                                static_cast<unsigned long long>(v), st.colour_from, st.colour_to, st.colour_player + 1, series, row,
                                live && r.paint_game ? ": that player's paint back to Game" : " (replay: settings untouched)");
                    std::fflush(stdout);
                    if (live && r.paint_game) r.paint_game(st.colour_player);
                }
                if (st.prompt_changed && r.paint_page) b3gv_native::set_paint_prompt(paint_sel.opacity(), paint_sel.player());
                if (b3native::carousel_resolver()) {   // paint3 (carousel_models.h): the slots a resolver names, when they change
                    const std::vector<b3native::CarouselSlot> slots =
                        b3native::series_page_up(page)
                            ? b3native::carousel_slots([](std::uint32_t a) { return b3host::load<std::uint32_t>(a); },
                                                       [](std::uint32_t a) { return b3host::load<std::uint8_t>(a); }, b3native::carousel_resolver())
                            : std::vector<b3native::CarouselSlot>{};
                    const std::vector<std::uint8_t> rec = b3native::carousel_record(slots, 0);
                    if (rec != carousel_last && !(carousel_last.empty() && slots.empty())) {
                        carousel_last = rec;
                        b3gv_native::set_carousel_models(b3native::carousel_record(slots, ++carousel_serial));
                    }
                }
                // the panel's button: opened or closed at this VBlank (the pad withheld from here, as for PC SETTINGS: until
                // the panel has closed and the pad is released), so the game's side is on the VBlank grid
                const int presses = live && r.paint_presses ? r.paint_presses() : 0;
                for (int k = 0; k < presses && give >= 0 && r.paint_toggle; ++k) {
                    const int t = r.paint_toggle(give);
                    if (t == 1) page_hold.opened();
                    std::printf("native_play: VBlank %llu: car paint panel %s\n", static_cast<unsigned long long>(v),
                                t == 1 ? "opened (the pad withheld from the game)" : t == 0 ? "asked to close" : "not opened (another page is open)");
                    std::fflush(stdout);
                }
            }
            {   // raceopts / elim1 / heavyseries / gameopts: the renderer's record word, one for the race (records.h): raceopts'
                // options, with bit 14 for a Single Event Eliminator (the game sees a race; its ghosts and best times kept
                // apart), bit 13 for a heavyweight race, bits 11 / 12 for MIRROR MODE / the Crashbreaker
                if (replay_view) {   // replayview checks only: $B3_REPLAY_TEST_AT a replay asked for at that VBlank (any time),
                    // $B3_REPLAY_TEST_LEAVE_AT left; $B3_REPLAY_TEST_HASH_AT=<v>,<v>... the profile's and the records' hashes
                    // logged there ($B3_REPLAY_TEST_DUMP=<path>: the profile's 0x2000 bytes to <path>.<v>)
                    static const auto at = [](const char* n) { const char* e = std::getenv(n); return e ? std::atol(e) : -1L; };
                    static const long rq_at = at("B3_REPLAY_TEST_AT"), lv_at = at("B3_REPLAY_TEST_LEAVE_AT");
                    static const std::vector<long> hs_at = [] {
                        std::vector<long> r;
                        if (const char* e = std::getenv("B3_REPLAY_TEST_HASH_AT"))
                            for (const char* q = e; *q;) { r.push_back(std::atol(q)); while (*q && *q != ',') ++q; if (*q) ++q; }
                        return r;
                    }();
                    if (long(v) == rq_at) replay_view->request(true);
                    // $B3_REPLAY_TEST_AUTO=1: REPLAY (as pressed) 300 VBlanks after it is first offered, once
                    static const bool auto_on = [] { const char* e = std::getenv("B3_REPLAY_TEST_AUTO"); return e && *e && *e != '0'; }();
                    static long auto_at = -1;
                    if (auto_on && auto_at == -1 && replay_view->available()) auto_at = long(v) + 300;
                    if (auto_on && long(v) == auto_at) {
                        auto_at = -2;
                        std::printf("replay_view: check: VBlank %llu: REPLAY asked for by the test (%s)\n", static_cast<unsigned long long>(v),
                                    replay_view->request() ? "taken" : "not taken");
                        std::fflush(stdout);
                    }
                    if (long(v) == lv_at) replay_view->leave();
                    if (std::find(hs_at.begin(), hs_at.end(), long(v)) != hs_at.end()) {
                        const auto fnv = [](b3host::gaddr a, std::uint32_t n) {
                            std::uint64_t h = 1469598103934665603ull;
                            for (std::uint32_t i = 0; i < n; ++i) h = (h ^ b3host::load<std::uint8_t>(a + i)) * 1099511628211ull;
                            return h;
                        };
                        const b3host::gaddr prof = b3host::load<std::uint32_t>(0x0051BF9Cu) & 0x01FFFFFFu;   // CB3Profile (0x49C bytes)
                        if (const char* d = std::getenv("B3_REPLAY_TEST_DUMP"))
                            if (std::FILE* f = std::fopen((std::string(d) + "." + std::to_string(v)).c_str(), "wb")) {
                                for (std::uint32_t i = 0; i < 0x2000u; ++i) std::fputc(b3host::load<std::uint8_t>(prof + i), f);
                                std::fclose(f);
                            }
                        if (const char* d = std::getenv("B3_REPLAY_TEST_DUMP"))   // (and the records, 0xF000 bytes)
                            if (std::FILE* f = std::fopen((std::string(d) + ".rec." + std::to_string(v)).c_str(), "wb")) {
                                for (std::uint32_t i = 0; i < 0xF000u; ++i) std::fputc(b3host::load<std::uint8_t>(0x01EA7810u + i), f);
                                std::fclose(f);
                            }
                        std::printf("replay_view: check: VBlank %llu: the profile %016llX, the records %016llX, the takedown tally %016llX\n",
                                    static_cast<unsigned long long>(v), static_cast<unsigned long long>(fnv(prof, 0x49Cu)),
                                    static_cast<unsigned long long>(fnv(0x01EA7810u, 0xF000u)), static_cast<unsigned long long>(fnv(0x0051C610u, 0x50u)));
                        std::fflush(stdout);
                    }
                }
                std::uint64_t w = race_opts ? race_opts->race_record_word() : 0;
                const bool el = single_eliminator && b3native::SingleEliminator::racing();
                if (el != elim_on) {
                    elim_on = el;
                    if (el) single_eliminator->take_profile();
                    else std::printf("native_play: VBlank %llu: single_eliminator: the profile after the Eliminator: %d bytes changed\n",
                                     static_cast<unsigned long long>(v), single_eliminator->profile_changes());
                    std::printf("native_play: VBlank %llu: single_eliminator: %s\n", static_cast<unsigned long long>(v),
                                el ? "an ELIMINATOR stage: its records kept apart" : "not an Eliminator stage");
                    std::fflush(stdout);
                }
                if (el) w = b3native::EliminatorRows::record_word(w);
                if (const int extra = more_rivals ? more_rivals->extra() : 0)   // MORE RIVALS: the rivals in bits 6-8
                    w = b3native::more_rivals_record_word(w, 5 + extra);
                // heavyseries: a heavyweight race, bit 13 (heavy_series.h heavy_record_word; one folder with the others)
                const bool hv = heavy_race && b3native::HeavyRace::racing();
                if (hv != heavy_racing) {
                    heavy_racing = hv;
                    std::printf("native_play: VBlank %llu: heavy_series: %s (car %016llx, mode %d, %d AI cars)\n", static_cast<unsigned long long>(v),
                                hv ? "a heavyweight race: records kept apart" : "no heavyweight race",
                                static_cast<unsigned long long>(b3host::load<std::uint64_t>(0x01E900B0u + 0x80u)),
                                b3host::load<std::int32_t>(0x01E900B8u), b3host::load<std::int32_t>(0x0051ADA4u));
                    std::fflush(stdout);
                }
                if (hv) w = b3native::heavy_record_word(w);
                // trafficseries: a traffic vehicle as the player's car, bit 28 (traffic_series.h traffic_record_word)
                const bool tv = traffic_cars && b3native::TrafficCars::racing();
                if (tv != traffic_racing) {
                    traffic_racing = tv;
                    std::printf("native_play: VBlank %llu: traffic_cars: %s (car %016llx)\n", static_cast<unsigned long long>(v),
                                tv ? "a traffic vehicle's race: records kept apart" : "no traffic vehicle's race",
                                static_cast<unsigned long long>(b3host::load<std::uint64_t>(0x01E900B0u + 0x80u)));
                    std::fflush(stdout);
                }
                if (tv) w = b3native::traffic_record_word(w);
                // customseries: an extra car as the player's car, bit 29 (extra_series.h extra_record_word)
                const bool xv = extra_cars && b3native::ExtraCars::racing();
                if (xv != extra_racing) {
                    extra_racing = xv;
                    std::printf("native_play: VBlank %llu: extra_cars: %s (car %016llx)\n", static_cast<unsigned long long>(v),
                                xv ? "an extra car's race: records kept apart" : "no extra car's race",
                                static_cast<unsigned long long>(b3host::load<std::uint64_t>(0x01E900B0u + 0x80u)));
                    std::fflush(stdout);
                }
                if (xv) w = b3native::extra_record_word(w);
                // gameopts: MIRROR MODE (bit 11) / the Crashbreaker outside Crash mode (bit 12) (game_option_rules.h)
                if (game_options) w = b3native::game_options_record_word(w, game_options->record_bits());
                if (game_mods) w = b3native::GameMods::record_word(w, game_mods->race_deformation());   // options3: DEFORMATION (bits 32-34)
                // rrparts1: a Road Rage whose loose parts all came off: bit 25 (records.h, "-loose-parts"; one folder with the others)
                if (b3native::RoadRageParts::racing()) w = b3native::RoadRageParts::record_word(w);
                if (traffic_check) w = b3native::traffic_check_record_word(w, traffic_check->mode());   // trafficcheck: bits 26-27
                if (w != records_word) {
                    records_word = w;
                    std::printf("native_play: VBlank %llu: records word %04llX (%s)\n", static_cast<unsigned long long>(v), static_cast<unsigned long long>(w),
                                w ? b3replay::records::options_folder(w).c_str() : "the game's own race");
                    std::fflush(stdout);
                    b3gv_native::set_race_options(w);
                }
                if (single_eliminator && single_eliminator->timer_seconds() >= 0) {   // elim1: the countdown, in the HUD's colours
                    std::uint32_t col[8];
                    b3native::SingleEliminator::hud_colours(col);
                    b3gv_native::set_elim_timer(single_eliminator->timer_seconds(), col);
                } else {
                    b3gv_native::set_elim_timer(-1);
                }
            }
            if (replay_view) {   // replayview (replay_view.h): REPLAY on a results page / in the replay; the state for the renderer
                int presses = 0;
                if (rplay) {
                    presses = g_live.replay_presses;
                    g_live.replay_presses = 0;
                } else if (r.replay_presses) {
                    presses = r.replay_presses();
                }
                if (presses > 0) {
                    const char* did = replay_view->showing() ? (replay_view->leave(), "leave")
                                      : replay_view->request()  ? "replay"
                                                                : nullptr;
                    if (did) {
                        if (rrec) rrec->event(v, "replay", did);
                        std::printf("native_play: VBlank %llu: REPLAY: %s\n", static_cast<unsigned long long>(v), did);
                        std::fflush(stdout);
                    }
                }
                b3gv_native::set_replay_state(replay_view->showing() ? 2 : replay_view->available() ? 1 : 0);
                if (r.set_replay_showing) r.set_replay_showing(replay_view->showing() ? 1 : 0);   // replaycams: the viewer's bindings withheld
            }
            if (gearbox) {   // gearbox (gearbox.h): SHIFT UP / SHIFT DOWN on the VBlank grid, the gear for the HUD
                int up = 0, down = 0;
                if (rplay) {
                    up = g_live.shift_up, down = g_live.shift_down;
                    g_live.shift_up = g_live.shift_down = 0;
                } else if (r.shift_presses) {
                    up = r.shift_presses(1), down = r.shift_presses(0);
                }
                if ((up || down) && gearbox->manual()) {
                    if (rrec) rrec->event(v, "shift", "u" + std::to_string(up) + " d" + std::to_string(down));   // (each press: an event)
                    std::printf("native_play: VBlank %llu: gearbox: %d shift up, %d shift down (gear %d)\n",
                                static_cast<unsigned long long>(v), up, down, gearbox->gear());
                    std::fflush(stdout);
                    gearbox->press(up, down);
                }
                gearbox->end_vblank();
                const int g = gearbox->gear();
                static int gear_logged = -3, flag_logged = -2;
                if (g != gear_logged || gearbox->flag_now() != flag_logged) {
                    gear_logged = g;
                    flag_logged = gearbox->flag_now();
                    if (g >= -1 || flag_logged >= 0)
                        std::printf("native_play: VBlank %llu: gearbox: gear %d, manual flag %d\n", static_cast<unsigned long long>(v), g,
                                    flag_logged);
                }
                std::uint32_t col[8] = {};
                if (g >= -1) b3native::SingleEliminator::hud_colours(col);
                b3gv_native::set_gear(g, col);
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
                        // stutter: this VBlank's stamp was set before the pause: its poll comes after it
                        if (pace && pacer.on) b3gv_native::set_vblank_stamp(std::uint64_t(pacer.stamp_ns()));
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
            // gamemods1: START AT main menu presses Start on the title for the player (boot_boost.h; a replay holds it)
            if (have && replay_pad.empty() && !rplay && boot_boost.press_start()) s[0] = std::uint8_t(s[0] & ~0x08u);
            if (rplay) {   // extras: the recording's donor stores, on their VBlanks
                for (const auto& [at, donor] : g_live.demotes) b3host::store<std::uint64_t>(at, donor);
                g_live.demotes.clear();
            }
            if (!rplay && (extra_off.custom || extra_off.beta)) {   // extras: a remembered extra car of an off kind: its donor
                constexpr std::uint64_t kP8 = 6553600000000ull;   // 40^8: a GtID's class is its first four base-40 digits
                static const std::uint64_t kXcus = b3native::gtid_compress("XCUS") / kP8, kXbet = b3native::gtid_compress("XBET") / kP8;
                auto slot = [&](b3host::gaddr at) {
                    const std::uint64_t id = b3host::load<std::uint64_t>(at), cls = id / kP8;
                    if (!((cls == kXcus && extra_off.custom) || (cls == kXbet && extra_off.beta))) return;
                    std::uint64_t donor = b3native::gtid_compress("COMPCAR1");   // (a GtID: no slash)
                    for (const b3native::ExtraCar& c : extra_off.cars) if (c.id() == id) donor = c.donor_id();
                    b3host::store<std::uint64_t>(at, donor);
                    if (rrec) {   // the recording's event: playback stores the same on the same VBlank
                        char ev[48];
                        std::snprintf(ev, sizeof ev, "%08X:%016llX", unsigned(at), static_cast<unsigned long long>(donor));
                        rrec->event(v, "extra_demote", ev);
                    }
                    if (extra_off.demoted++ == 0)
                        std::printf("native_play: VBlank %llu: extras: a remembered %s car (%016llX, the class's digits %llu) with its "
                                    "series off races as its donor %016llX\n", static_cast<unsigned long long>(v), cls == kXcus ? "CUSTOM" : "BETA",
                                    static_cast<unsigned long long>(id), static_cast<unsigned long long>(cls), static_cast<unsigned long long>(donor));
                };
                slot(0x01E94EF8u);   // SELECT CAR's chosen car (extra_cars.h kSelectCarChosen)
                for (unsigned i = 0; i < 8; ++i) slot(0x01E900B0u + 0x80u + 8u * i);   // CB3MenuChoices' cars
            }
            // gamemods1: Select / L2 + R2 + Square on YOUR GARAGE opens the car viewer; a request is an event of the recording,
            // and playback opens it on the recording's requests only (car_viewer.h)
            if (rplay) {
                car_viewer.step(g_live.car_viewer_request);
                g_live.car_viewer_request = false;
            } else if (!extra_on(b3settings::kExtraCarViewer, "B3NATIVE_CAR_VIEWER")) {   // extras: CAR VIEWER off: never opened
                car_viewer.step(false);
            } else if (car_viewer.pad(have ? s.data() : nullptr) && rrec) {
                rrec->event(v, "car_viewer", "1");
            }
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
                // media1: at each checkpoint the recording so far to the file (this VBlank's ops all taken: it plays to here)
                if (rstream && v > 0 && v % check_every == 0) rstream->checkpoint(rrec->replay(), v);   // (encoded on the writer's thread)
            }
            // gamemods1: START AT (boot_boost.h): the intro's films end as they start, through the skip's own Abort
            if (skip.skippable() && boot_boost.skip_films() && skip.abort_now()) {   // (the setting read at the first film)
                std::printf("native_play: VBlank %llu: START AT: the intro film ended (CGtVideoDecoder::Abort)\n", static_cast<unsigned long long>(v));
                std::fflush(stdout);
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
                if (replay_view) replay_view->note_pad(s.data());   // replayview: a choice on a results page (REPLAY waits)
                b3native::platform::sif::set_pad_sample(0, s.data());
                ++pad.set;
                if (s != neutral) {
                    if (!pad.non_neutral++) { pad.first_non_neutral = v; pad.first_bytes = s; }
                }
                if (s != pad.last) ++pad.changes;
                pad.last = s;
            }
            // nextstart: PC SETTINGS' APPLY AND RESTART (relaunch.h): this run ends as QUIT GAME's, then the game starts again
            if (!g_restart.load() && r.restart_requested && r.restart_requested() != 0) {
                g_restart.store(true);
                g_restart_boot_to = r.boot_to ? r.boot_to() : 0;
                std::printf("native_play: VBlank %llu: APPLY AND RESTART: the game ends and starts again\n", static_cast<unsigned long long>(v));
                std::fflush(stdout);
            }
            if (const char* quit = r.quit_requested() ? "the window was closed" : pc_menu && pc_menu->quit_requested() ? "QUIT GAME"
                                   : g_restart.load() ? "RESTART" : nullptr) {
                // fsrobust: a save the game is writing (it spans many VBlanks) finishes and settles first, up to 5 s:
                // the card is not cut in the middle of a save
                b3native::save::ProfileCard* card = b3native::platform::sif::save_profile_card();
                if (card && card->writing() && (!quit_hold_from || v < quit_hold_from + 300)) {
                    if (!quit_hold_from) {
                        quit_hold_from = v;
                        std::printf("native_play: VBlank %llu: %s while the game is saving: the save finishes first\n", static_cast<unsigned long long>(v), quit);
                        std::fflush(stdout);
                    }
                } else {
                    end_reason = quit;
                    throw PlayEnd(end_reason);
                }
            }
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
            if (b3native::runlog::logging()) stop_text += "\n\nRun log: " + b3native::runlog::state().path;
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
        // save1: a save the game made in its last second (a replay's throwaway card: nothing written, kept or told)
        save_exit_text = b3native::platform::sif::save_card_flush(replay_in.has_value());
        if (code_mods && clean) code_mods->shutdown();   // sdk2b: on_shutdown (a normal end only)
        const double secs = std::chrono::duration<double>(clock::now() - start).count();
        const std::uint64_t vbl = b3native::platform::vblank_count();
        if (rstream) {   // media1: the last checkpoint's write done before the whole file goes over it
            rstream->stop();
            if (const std::string e = rstream->error(); !e.empty()) std::fprintf(stderr, "native_play: replay: a checkpoint's file: %s\n", e.c_str());
        }
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
        if (traffic_check && (traffic_check->counts().checks || traffic_check->counts().records_skipped || traffic_check->counts().chains ||
                              traffic_check->counts().player_traffic_crashes || traffic_check->counts().player_wreck_crashes)) {
            const auto& tc = traffic_check->counts();   // trafficcheck
            std::printf("native_play: traffic_check: %llu checks (%llu of them wrecks, %llu shunts), %llu wrecks of checked cars' chains, %llu trick shots, %llu skill shots, %llu extreme shots, %llu takedowns "
                        "credited, %llu own-wreck touches spared; %llu chains, %llu signs; %llu wrecks cleared, %llu hits not "
                        "checked (wreck slots full); records calls emptied %llu, profile counts refused %llu; player crashes on "
                        "traffic %llu, on wrecks %llu\n",
                        static_cast<unsigned long long>(tc.checks), static_cast<unsigned long long>(tc.wreck_checks),
                        static_cast<unsigned long long>(tc.shunt_checks), static_cast<unsigned long long>(tc.chain_owned),
                        static_cast<unsigned long long>(tc.trick_shots),
                        static_cast<unsigned long long>(tc.skill_shots), static_cast<unsigned long long>(tc.extreme_shots),
                        static_cast<unsigned long long>(tc.takedowns), static_cast<unsigned long long>(tc.own_touches),
                        static_cast<unsigned long long>(tc.chains), static_cast<unsigned long long>(tc.signs),
                        static_cast<unsigned long long>(tc.wrecks_cleared), static_cast<unsigned long long>(tc.not_checked_pool),
                        static_cast<unsigned long long>(tc.records_skipped), static_cast<unsigned long long>(tc.profile_skipped),
                        static_cast<unsigned long long>(tc.player_traffic_crashes), static_cast<unsigned long long>(tc.player_wreck_crashes));
        }
        if (game_options && (game_options->counts().steering || game_options->counts().armed || game_options->counts().records_skipped ||
                             game_options->counts().profile_skipped)) {   // gameopts
            const auto& gc = game_options->counts();
            std::printf("native_play: game_options: steering negated %llu times; Crashbreaker armed %llu, fired %llu; records calls "
                        "emptied %llu, profile counts refused %llu\n", static_cast<unsigned long long>(gc.steering),
                        static_cast<unsigned long long>(gc.armed), static_cast<unsigned long long>(gc.fired),
                        static_cast<unsigned long long>(gc.records_skipped), static_cast<unsigned long long>(gc.profile_skipped));
        }
        if (wreck_slots && wreck_slots->released())   // wreckguard
            std::printf("native_play: wreck_slots: %llu wrecks released with all 64 crashed-traffic slots in use\n",
                        static_cast<unsigned long long>(wreck_slots->released()));
        if (party) {   // party1
            const auto& pc = party->counts();
            if (pc.gravity_swaps || pc.wheels_kept || pc.moon_frames || pc.crashes || pc.ai_toggles || pc.kamikaze_calls || pc.traffic_induced)
                std::printf("native_play: party_modes: AI gravity swaps %llu, AI wheel contacts kept %llu, flips spared %llu, flips blown %llu, "
                            "moon jump frames %llu, crashes on demand %llu, AI driver toggles %llu, kamikaze traffic "
                            "calls %llu; traffic crashes induced %llu (by KAMIKAZE TRAFFIC %llu)\n", static_cast<unsigned long long>(pc.gravity_swaps), static_cast<unsigned long long>(pc.wheels_kept),
                            static_cast<unsigned long long>(pc.flips_spared), static_cast<unsigned long long>(pc.flips_blown),
                            static_cast<unsigned long long>(pc.moon_frames),
                            static_cast<unsigned long long>(pc.crashes), static_cast<unsigned long long>(pc.ai_toggles),
                            static_cast<unsigned long long>(pc.kamikaze_calls), static_cast<unsigned long long>(pc.traffic_induced),
                            static_cast<unsigned long long>(pc.kamikaze_induced));
        }
        std::printf("native_play: movie skip %s: %llu movies skipped; %llu movies started, %llu of them menu backgrounds (TRACKS, not skippable)\n",
                    movie_skip_on ? "on" : "off (--no-movie-skip)", static_cast<unsigned long long>(skip.skips()),
                    static_cast<unsigned long long>(skip.prepares()), static_cast<unsigned long long>(skip.exempt_prepares()));
        std::printf("native_play: %s\n", outcome.c_str());
        std::printf("native_play: %llu VBlanks in %.2f s wall (%.2f per s; %s%llu resyncs when behind)\n",
                    static_cast<unsigned long long>(vbl), secs, secs > 0 ? vbl / secs : 0.0, pace ? "" : "unpaced, ",
                    static_cast<unsigned long long>(pacer.resyncs));
        if (behind_times) {   // slowcpu
            if (behind.behind) behind_s += std::chrono::duration<double>(clock::now() - behind_since).count();
            std::printf("native_play: the game fell behind real time %llu times, %.1f s in all (the renderer held to 60 meanwhile)\n",
                        static_cast<unsigned long long>(behind_times), behind_s);
        }
        if (pace)   // stutter
            std::printf("native_play: stalls: %llu times the game thread fell more than 2 VBlanks behind its schedule (the most %.1f ms); the sound ran dry %llu times (the game late; fast loads' silence not counted)\n",
                        static_cast<unsigned long long>(stall_count), stall_max_ms, audio_late_seen);
        if (feed_waits)   // trafmem
            std::printf("native_play: %llu waits for the renderer's shots (its feed full), %.1f s in all\n", static_cast<unsigned long long>(feed_waits),
                        feed_wait_ms / 1000.0);
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
    // sweep: `ended` set when the game thread has finished (the watchdog below then stops: the renderer may still drain
    // its feed for long after the game's last VBlank, and a stall dump then reached the torn-down backend)
    std::atomic<bool> game_ended{false};
    struct Ctx { decltype(play_game)* fn; int* rc; std::atomic<bool>* ended; } ctx{&play_game, &game_rc, &game_ended};
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
            c->ended->store(true);
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
        while (!watch_stop.load() && !game_ended.load() && stall_s > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const std::uint64_t v = b3native::platform::vblank_count();
            const bool held = (r.focused && !r.focused()) || g_replay_paused.load();
            const bool page = r.settings_active && r.settings_active() != 0;
            if (v != last || held || page || v == 0) { last = v; since = std::chrono::steady_clock::now(); continue; }
            if (game_ended.load()) break;   // (the game's last VBlank: no stall, the renderer draining)
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
    if (!save_exit_text.empty()) {   // fsrobust
        std::fprintf(stderr, "native_play: %s\n", save_exit_text.c_str());
        if (!offscreen && !replay_in) tell_user("save not written", save_exit_text, exe_dir);
    }
    replay_card_guard.remove();   // replay1: the throwaway card (media1: every earlier return removes it too)
    if (replay_parted && !offscreen)
        tell_user("replay", "This replay did not play as it was recorded from " + b3native::replay::format_length(replay_parted) +
                                " on: this build or this PC gives the game something the recording's did not. What came after "
                                "that point was not the recorded session.", exe_dir);
    if (g_restart.load()) {   // nextstart (relaunch.h): everything above closed and written; the game started again
        std::vector<std::string> args;
        for (int k = 1; k < argc; ++k) args.push_back(argv[k]);
        std::printf("native_play: restart: this run ends here (the run log closed, the streams back to their own)\n");
        std::fflush(stdout);
        // what the normal end's atexit does, first (execv never returns there): the run log's streams back to their
        // originals -- left on its pipe, the new process's first output would meet a pipe with no reader (SIGPIPE)
        b3native::runlog::finish();
        if (!b3native::relaunch(argv[0], b3native::relaunch_args(args), g_restart_boot_to)) {
            std::fprintf(stderr, "native_play: restart: the game could not be started again: start it from the launcher\n");
            if (!offscreen) tell_user("restart", "The game could not be started again: start it from the launcher.", exe_dir);
        }
    }
    return game_rc;
}
