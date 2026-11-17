// The PC settings of the port (settings1, docs/pc-settings.md): the values, their defaults and presets, the ini file,
// the HSET host record that carries them to the renderer, and the page's rows. No runtime dependency: native_play
// (the renderer's page, src/b3replay/pcpage.cpp) and the launcher host the same page.
//
// Hard rule (docs/remaster-features.md): every value here is drawn-only or host-only. None reaches game memory, so a
// replay is exact whatever they are set to. Defaults = main before settings1 (the "High" preset), pixel for pixel.
#ifndef B3SETTINGS_SETTINGS_H
#define B3SETTINGS_SETTINGS_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b3settings {

enum class Filter : std::uint8_t { kPoint, kBilinear, kTrilinear, kAniso2, kAniso4, kAniso8, kAniso16 };
enum class Preset : std::uint8_t { kPs2, kLow, kMedium, kHigh, kUltra, kCustom };

struct Settings {
    // Display
    bool fullscreen = false;          // borderless desktop fullscreen (F11 still toggles it)
    bool vsync = false;               // b3gfx IMMEDIATE / MAILBOX when off (applies at the next start)
    int frame_cap = 0;                // presented frames per second, 0 = unlimited (--max-fps)
    bool interpolation = true;        // frames between the game's ticks (key I); off = the game's 60 / 30 Hz steps
    bool hud_wide = true;             // the race HUD anchored to the window's edges (hud2) / the 4:3 layout (key Y)
    int fe_layout = 2;                // the front end at wide aspects: 0 edges (hud2), 1 4:3 (wide1), 2 panels (fe3; --fe-layout)
    bool movie_fill = true;           // the backdrop's blurred side fill beside the 4:3 movies (fe3; --movie-fill blur | black)
    int split_layout = 0;             // splitlayout1: the 2-player split screen, 0 the game's top / bottom, 1 side by side
                                      // (drawn only: the views, their projections and the HUD placement; src/b3replay/splitlayout.h)
    bool pause_unfocused = true;      // mac5: the native game holds while its window is unfocused (native_play; else it runs on)
    bool fast_loading = true;         // fastload1: native_play runs the game unpaced while its loading screen is up (the same
                                      // VBlanks and game state, sooner; the loading screen's sound muted; tools/native/fast_load.h)
    bool hide_save_screens = true;    // autosave1: the AUTOSAVE page after a race is not shown (the game's frames held on the last
                                      // page, the page itself run unpaced in a window: the same VBlanks, game state and save bytes;
                                      // tools/native/save_screens.h). SAVE SCREENS: Hide (default) / Show (the PS2's flash of it)
    // Latency (lat1; applies at the next start). "Auto" keeps the run's own choice (perf.sh: low in a window, smooth headless).
    int latency = 0;                  // 0 auto, 1 low, 2 smooth, 3 newest (--latency)
    int lead = -1;                    // low mode's extrapolation past the newest tick, % of a tick; -1 auto (--lead)
    int frames_in_flight = -1;        // GPU frames in flight, 1..3; -1 auto (--frames-in-flight)
    // Quality
    int msaa = 4;                     // 1, 2, 4, 8 (applies at the next start)
    Filter filter = Filter::kAniso16; // 3D textures (applies at the next start); the 2D stays bilinear
    int render_scale = 100;           // effects2: RENDER SCALE, % of the window the 3D picture (effects and full-screen passes with it) is
                                      // drawn at, then scaled up; the HUD and menus stay at the window's size. 100 (the default) is the
                                      // picture as before, byte for byte. 50..100; the presets: LOW 75, the others 100. Drawn only
    bool dither = false;              // perf1: an ordered dither at the final output against 8-bit gradient bands (the presets leave it)
    bool hd_textures = true;          // the HD pack, when one is given ($B3_HD_PACK / --hd-pack)
    std::string hd_pack_dir;          // mac5 (remaster-features §8): the HD pack folder chosen on the SETTINGS page or in the
                                      // launcher (a PCSX2 replacement folder; empty: none, or $B3_HD_PACK). --hd-pack wins.
                                      // Applies at the next start
    int car_reflections = 2;          // reflect1: 0 off (no env pass), 1 original (the game's own pass), 2 enhanced (glossy sky / sun
                                      // reflections on the paint and glass; drawn only). PS2 ORIGINAL and LOW use 1
    bool car_detail_highest = false;  // cars at LOD 3 at any distance (else the game's LOD choice)
    bool shadows = true, skid_marks = true, specular = true, sun = true, coronas = true;
    bool whole_track = true;          // mac5: every unit of the track drawn (frustum culled), no streaming pop-in; off: the PS2's
                                      // streamed set (--track-units)
    // Effects
    bool motion_blur = true;          // the zoom / directional blur the game picks (key N)
    int radial_blur = 50;             // the boost radial blur, % of the decomp's model (main: --radial-blur 0.5)
    bool bloom = true;                // key T
    int bloom_level = 1;              // bloom2: 1 Soft (a gentler glow, a screen blend), 2 Standard (the game's own strength); the BLOOM row's
                                      // Off is `bloom = false` (the level is kept)
    bool post_pc = true;              // the post passes at PC quality (else the GS's buffers and taps; key Z)
    bool scrapes = true;
    // effects2: the particle effects' levels, drawn only: 0 off, 1 low, 2 medium, 3 high. High is the game's whole set
    // (byte for byte the output before the levels); Low / Medium draw a stable share of the particles (a quarter / a half,
    // chosen once per particle so nothing flickers). The game's own state is never touched. ini: `sparks`, `popcorn`
    // (a file from before the levels, true / false, reads as High / Off)
    int sparks = 3;                   // the sparks of scrapes and crashes
    int popcorn = 3;                  // CRASH DEBRIS: the small bits of a crash
    bool damage = true;               // the game's deformation drawn (else pristine bodywork; physics untouched)
    bool glass_damage = true;         // cracked / broken glass drawn (else intact)
    // Camera (drawn only: the game's camera is retail; the view's tangents scaled, the eye dollied)
    int fov = 0;                      // horizontal field of view at full boost in degrees, 0 = the game's (90)
    bool fov_correction = true;       // keep the car's size: the eye moves along the view direction
    // Interface
    int fps_view = 0;                 // fpsoverlay2: the FPS COUNTER (src/b3replay/fpspanel.h): 0 off, 1 minimal, 2 standard, 3 detailed.
                                      // An ini from before the views holds `fps_counter = true / false`: true reads as 2, false as 0
    int fps_corner = 0;               // the counter's corner: 0 top left, 1 top right, 2 bottom left, 3 bottom right
    bool skip_hint = true;            // mac5: the corner "SKIP" hint with the device's button while a video can be skipped
                                      // (native_play; remaster-features §2). The skip itself works either way
    int button_prompts = 0;           // mac5: the button glyphs the prompts show (remaster-features §3; src/b3replay/prompts.h
                                      // PromptMode): 0 auto (the device in use), 1 PlayStation (the game's own), 2 Xbox,
                                      // 3 Nintendo, 4 generic, 5 keyboard, 6 PS4 / PS5 (the game's, Start as OPTIONS)
    int speed_units = 0;              // units1: the game's speeds and distances (the speedometer, distance messages, records,
                                      // stats): 0 miles (the US game's), 1 kilometres (tools/native/speed_units.h)
    int date_format = 0;              // mac5: the game's dates (DATE OF SAVE ...): 0 month/day/year (the US game's), 1 day/month/year
    bool photo_row = true;            // photorow1: the race pause menu gets a PHOTO MODE row (after SETTINGS; tools/native/pc_menu.h)
                                      // that enters photo mode, as P does. The presets never touch it
    bool battery_saver = false;       // effects2: BATTERY SAVER (opt-in): while the PC runs on battery, the frame rate is capped to 60
                                      // and the particle effects to Medium (host-only: native_play reads SDL's power state)
    bool launcher_at_start = true;    // launchskip1: the launcher (b3launcher) shows its window when it is started; false: it
                                      // starts the game at once with these settings (its exceptions: src/b3launcher/core.h
                                      // skip_launcher). Read by the launcher only; the game never starts the launcher
    // Music (remaster-features §6, native_play: tools/native/custom_soundtrack.h). Host-only: with playlist_mode 0 the
    // game's music is untouched; 1 / 2 put the folder's songs into the EA Trax system (the game's music state then
    // follows those songs' lengths; physics, AI and gameplay never)
    std::string music_folder;         // the user's music folder (MP3 / FLAC / Ogg Vorbis / WAV, subfolders too); empty: none
    int playlist_mode = 0;            // 0 EA Trax only (the game's), 1 custom only, 2 mixed (custom and EA Trax in turn)
    int other_music_apps = 0;         // nowplaying1: 0 off (no system calls at all), 1 another app's song in the EA TRAX popup
                                      // (the system's now-playing interface; tools/native/now_playing_popup.h; drawn only)
    // Accessibility (remaster-features "Accessibility"; PC SETTINGS > INTERFACE, section ACCESSIBILITY). The defaults are
    // the game exactly: the frames byte-identical, the pad reports untouched.
    int screen_flashes = 0;           // 0 full (the game's), 1 reduced: the 2D layer's full-screen bright fills and the
                                      // camera-change static capped (drawn only; src/b3replay/a11y.h)
    int hud_colour_filter = 0;        // 0 off, 1 deuteranopia, 2 protanopia, 3 tritanopia: a daltonize remap of the 2D layer
                                      // (HUD, menus, our pages; never the 3D scene or the movies; drawn only; a11y.h)
    int hud_size = 0;                 // textsize1: 0 100 % (the game's), 1 115 %, 2 130 %, 3 150 %: the race HUD's
                                      // components scaled about their own anchors (menus stay 100 %; drawn only; a11y.h)
    int boost_mode = 0;              // 0 hold (the game's), 1 toggle: a press of boost (R1) latches it held until the next
                                      // press, the race context ends or Start (pause). INPUT-CHANGING, opt-in: it changes the
                                      // pad reports the game reads (src/native/input_keys.h BoostLatch; native_play)
    // Time Attack ghosts (ghost1, remaster-features §5; src/b3replay/ghost.h). Drawn only: 0 off (nothing recorded or
    // drawn: the frames byte-identical), 1 best lap: the player's laps recorded to <prefs>/ghosts, the track's best one
    // drawn as a see-through car the game never knows of
    int ghost = 0;
    // Speedrun support (speedrun1, remaster-features §5; src/b3replay/speedrun.h, docs/pc-settings.md §7.3). The timer is
    // drawn only: 0 off (nothing read, kept or drawn: the frames byte-identical), 1 the event's time in game time (from
    // the game's start signal to its finish) with the best kept per event and car in <prefs>/speedrun, 2 also the lap
    // splits. Instant restart: 0 off; 1 a key (F5) / pad combo (Select + Start) restarts the event through the game's
    // own pause menu (RESTART, then YES): INPUT-INJECTING, opt-in -- the game reads menu presses the player did not make
    int speedrun_timer = 0;
    int instant_restart = 0;
    // Controller rumble (rumble1, remaster-features §3; src/native/pad_actuator.h; native_play). 0 off (the default): the
    // game's rumble requests are left pending as before (no motor driven; the run identical to one without the row).
    // 1 on: they are acknowledged and their motor values given to the pads (SDL rumble). GAME-VISIBLE, opt-in: the game
    // then sees its requests answered (the result it ignores, and the RPCs' virtual time); gameplay does not read them
    // (docs/pc-settings.md §3.8). Strength and triggers are host-only.
    int rumble = 0;
    int rumble_strength = 100;        // the motors' speed the pads are given, percent (10..100)
    bool rumble_triggers = false;     // also the triggers' own motors (Xbox One / Series impulse triggers) where a pad has them
    // DualSense adaptive triggers (pad1, remaster-features §5; src/native/pad_triggers.h; the window). Presentation only:
    // 0 off (the default: no effect is ever sent to a pad), 1 on: PS5 pads' triggers resist, buzz and kick from the game's
    // state as the window reads it (nothing is written to the game; the reports are unchanged). Strength: percent (10..100)
    int adaptive_triggers = 0;
    int adaptive_triggers_strength = 100;
    // Car series on the shoulders (carsel1; src/native/input_keys.h ShoulderSeries; native_play). 0 off (the default: the
    // reports untouched, nothing drawn). 1 on: on the car select page, L1 / R1 presses reach the game as Left / Right
    // (the page's own series change) and small L1 / R1 glyphs (the device's buttons) are drawn beside its series arrows.
    // INPUT-CHANGING, opt-in: the game reads presses the player made on other buttons
    int series_shoulders = 0;
    // Replay files (replay1, remaster-features §5; src/native/replay_file.h). Host-only: 0 off (nothing recorded), 1 on:
    // native_play keeps each session from its start to its end as <prefs>/replays/<date-time>.b3replay (the inputs, the
    // save as it was at the start; the newest 20 automatic ones kept), played back from the launcher's Replays page.
    // Read at the start of a session
    int record_replays = 0;
    // Developer mods (tune4, docs/modding/tuning.md; native_play). 0 off (the default): nothing of the tuning console is
    // installed (the game's tuning variables are not even read). 1 on, from the next start: native_play records the
    // game's tuning registry and the page gets a DEVELOPER tab (and F9 a live overlay in a race) to read and edit the
    // variables. GAMEPLAY-CHANGING when an edit is made: each one goes into the session's pad recording / replay file
    int developer_mods = 0;

    bool operator==(const Settings&) const = default;
};

// The presets touch the quality and effects values only (not the window, frame rate, camera or interface).
Settings with_preset(Settings s, Preset p);
// effects2: the levels (Settings::sparks, popcorn): the name of one, the share of the particles it draws (0, 25, 50, 100 %)
enum : int { kLevelOff = 0, kLevelLow = 1, kLevelMedium = 2, kLevelHigh = 3 };
const char* level_name(int level);
// effects2: RENDER SCALE's choices (percent), the nearest one's index of a value
constexpr int kRenderScales[] = {100, 85, 75, 67, 50};
std::size_t render_scale_index(int percent);
// effects2: BATTERY SAVER's change while the PC runs on battery (nothing when the row is off): the frame rate capped to 60
// and SPARKS / CRASH DEBRIS to at most Medium. Host-only; the stored settings are never changed (the host applies this copy).
Settings on_battery(Settings s);
int level_percent(int level);
Preset matching_preset(const Settings& s);    // kCustom when no preset gives exactly these values
const char* preset_name(Preset p);

// key = value text (also the HSET payload). Unknown keys are ignored, bad values keep the default.
std::string to_ini(const Settings& s);
Settings from_ini(std::string_view text, std::vector<std::string>* warnings = nullptr);
bool load_file(const std::string& path, Settings& s, std::string* why = nullptr);   // false: no file (s unchanged)
bool save_file(const std::string& path, const Settings& s, std::string* why = nullptr);

// The HSET host record (published at kHostSetBase, the renderer reads it per tick):
// {u32 'HSET', u32 serial, u32 payload bytes, u32 0}, then the payload (to_ini), padded to 16 bytes.
constexpr std::uint32_t kHsetMagic = 0x54455348U;   // 'HSET'
constexpr std::uint32_t kHsetMax = 0x1000U;         // header + payload
std::vector<std::uint8_t> hset_encode(const Settings& s, std::uint32_t serial);
// Header only: serial and payload size, or nullopt when not a record.
std::optional<std::uint32_t> hset_serial(const std::uint8_t* head16, std::uint32_t* bytes = nullptr);

// Degrees of the field of view row's levels, the slider's level of a value.
constexpr int kFovMin = 60, kFovMax = 120, kFovStep = 5, kGameFov = 90;
// The view's tangent scale of a field of view value (1 for the game's own).
float fov_tangent_scale(int fov);

}  // namespace b3settings

#endif
