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
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace b3settings {

enum class Filter : std::uint8_t { kPoint, kBilinear, kTrilinear, kAniso2, kAniso4, kAniso8, kAniso16 };
enum class Preset : std::uint8_t { kPs2, kLow, kMedium, kHigh, kUltra, kCustom };

// slowcpu (issue #15): FRAME RATE LIMIT Auto (frame_cap -1) on a CPU of this many logical processors or fewer is 60
constexpr int kFrameCapAuto = -1;
constexpr int kAutoCapThreads = 4;
// The limit in effect (0: none): Auto resolved for a CPU of `logical_cpus` (0: unknown: unlimited)
constexpr int effective_frame_cap(int frame_cap, int logical_cpus) {
    if (frame_cap != kFrameCapAuto) return frame_cap > 0 ? frame_cap : 0;
    return logical_cpus > 0 && logical_cpus <= kAutoCapThreads ? 60 : 0;
}

struct Settings {
    // Display
    bool fullscreen = false;          // borderless desktop fullscreen (F11 still toggles it)
    bool vsync = false;               // b3gfx IMMEDIATE / MAILBOX when off (applies at the next start)
    // presented frames per second (--max-fps): 0 unlimited; -1 Auto (slowcpu, the default since 1.3.1: unlimited, or 60 on
    // a CPU of kAutoCapThreads logical processors or fewer, where the game's own work needs the CPU: effective_frame_cap)
    int frame_cap = -1;
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
    int upscaling = 1;                // 1.1: RENDER SCALE's upscale, 0 bilinear (as 1.0), 1 sharp (post.frag sharp_upscale)
    int render_scale = 100;           // effects2: RENDER SCALE, % of the window the 3D picture (effects and full-screen passes with it) is
                                      // drawn at, then scaled up; the HUD and menus stay at the window's size. 100 (the default) is the
                                      // picture as before, byte for byte. 50..200 (perf4: above 100 supersampled, brought down with a box
                                      // filter); the presets: LOW 75, the others 100. Drawn only
    bool dither = false;              // perf1: an ordered dither at the final output against 8-bit gradient bands (the presets leave it)
    bool hd_textures = true;          // the HD pack, when one is given ($B3_HD_PACK / --hd-pack)
    std::string hd_pack_dir;          // mac5 (remaster-features §8): the HD pack folder chosen on the SETTINGS page or in the
                                      // launcher (a PCSX2 replacement folder; empty: none, or $B3_HD_PACK). --hd-pack wins.
                                      // Applies at the next start
    bool xbox_textures = false;       // xboximport (1.3.0): the pack Import Xbox textures built from the player's Xbox disc
    int hd_texture_memory = 0;        // texpack: HD TEXTURE MEMORY: 0 Auto, 1 Full quality (PNG / BC7 as RGBA8), 2 Compressed
                                      // (encoded to BC1 / BC3 at load); next start
                                      // (<user data>/xbox-textures, b3asset/xboximport.h), below the mods' packs and hd_pack_dir;
                                      // set by a finished import, off by default; used with hd_textures (F4). Next start
    int car_reflections = 2;          // reflect1: 0 off (no env pass), 1 original (the game's own pass), 2 enhanced (glossy sky / sun
                                      // reflections on the paint and glass; drawn only). PS2 ORIGINAL and LOW use 1
    // reflect2 (docs/car-shading.md §10): what the car env map's track part holds. REFLECTION DISTANCE 0 the game's 100 m
    // (env cameras' far plane), 1 200 m, 2 300 m; REFLECTION DETAIL 0 the track, 1 and the props; REFLECTION UPDATE 0 every
    // frame, 1 every second frame. Drawn only; HIGH (the defaults) keeps the game's map, ULTRA 200 m with the props and cars
    // (reflect_detail 2: Track, props and cars)
    int reflect_distance = 0;
    int reflect_detail = 0;
    int reflect_update = 0;
    int reflect_resolution = 0;       // reflect2 D: REFLECTION RESOLUTION 0 Standard (128 px faces, the PS2's / Xbox's), 1 High (256)
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
    // 1.1 (drawn only): the chase camera moved back (percent of the game's distance to the car, 90..150 in 10s, 100 the
    // game's) and up (centimetres, 0..100 in 25s), tilted to keep the car where it was on screen (View::apply_fov)
    int camera_distance = 100;
    int camera_height = 0;
    // carpaint (remaster-features §15; b3asset/carpaint.h), drawn only: player 1's car in a colour of the player's own
    // (car_paint 1: paint_red / green / blue, 0..255 as on screen) instead of the paint picked in the car select, and
    // car_decals false: the car without its logos, numbers and stripes. The game keeps the car select's paint (replays,
    // ghosts, saves, the other players see it); cars with one fixed livery keep it. Off by default
    int car_paint = 0;
    int paint_red = 195, paint_green = 30, paint_blue = 30;   // padpaint: on the rows' 15-step grid (was 192 / 24 / 24)
    bool car_decals = true;
    // padpaint: player 2's (the split screen's second car), the same rows; and the finish of both players' paint (0
    // Gloss: the game's, 1 Metallic: a stronger reflection in the paint's colour and a flake sparkle, 2 Pearl: the
    // paint's hue shifting with the view angle). Drawn only; Gloss and Game (the defaults) draw the cars as before
    int p2_car_paint = 0;
    int p2_paint_red = 30, p2_paint_green = 90, p2_paint_blue = 210;
    bool p2_car_decals = true;
    int paint_finish = 0;          // 0 gloss (the game's), 1 metallic, 2 pearl, 3 chrome (options3)
    // paint2 (2026-11-22): the car's other layers, per player, drawn only, one set for every car: the DECALS' colour (its
    // logos, numbers and stripes), the WHEELS' (the rims; the tyres stay dark) and the BOOST GLOW's (the exhaust glow while
    // boosting). *_paint 1: the layer in *_red / green / blue (0..255 as on screen); 0: the game's. All 0 by default
    int decal_paint = 0, decal_red = 240, decal_green = 240, decal_blue = 240;
    int wheel_paint = 0, wheel_red = 165, wheel_green = 165, wheel_blue = 180;
    int flame_paint = 0, flame_red = 255, flame_green = 120, flame_blue = 0;
    int p2_decal_paint = 0, p2_decal_red = 240, p2_decal_green = 240, p2_decal_blue = 240;
    int p2_wheel_paint = 0, p2_wheel_red = 165, p2_wheel_green = 165, p2_wheel_blue = 180;
    int p2_flame_paint = 0, p2_flame_red = 255, p2_flame_green = 120, p2_flame_blue = 0;
    // night (remaster-features §17 / §18), drawn only, off by default: time_of_day 1 = TIME OF DAY Night (the races after
    // dark: the grade, the night sky, the headlights); moon: MOON (the disc and its light, at night); surface_detail:
    // SURFACE DETAIL (relief and sheen from the track's textures); ambient_occlusion: AMBIENT OCCLUSION
    int time_of_day = 0;
    bool moon = true;
    bool surface_detail = false;
    bool ambient_occlusion = false;
    // SUN SHADOWS (remaster-features §19), drawn only, off by default: the cars' shadows from the sun (the moon at night)
    // on the road. The map's size and kernel follow ANTI-ALIASING (b3replay::apply_view); the PS2 and Low presets turn it off
    bool sun_shadows = false;
    bool prop_shadows = false;   // propshadow1 (PROP SHADOWS, 613): with SUN SHADOWS, the props and lamp posts cast too
    // TUNNEL LIGHTS (nightfix, remaster-features §35.4), drawn only: 1 Glow (default: the light fittings of tunnels and
    // underpasses glow by day, with small halos), 0 Game (the PS2's look: plain textures); xboxglow (§35.5): 2 Xbox (the
    // fittings' lit parts -- the textures' alpha glow masks -- self-lit as the Xbox draws them, glowing through the bloom).
    // 3 (Xbox all lamps, withdrawn) is read from an older ini as 2
    int tunnel_lights = 1;
    // WEATHER (weather, remaster-features §38), drawn only: 0 Dry (default, the game's), 1 Rain (falling rain, wet roads,
    // spray, a grey sky). Held back with night mode until it is released: the row is not shown, the races stay dry
    int weather = 0;
    // RAIN VOLUME (weather, §38): the rain's sound in percent of its level (0..200, 100 default), x the game's own EFFECTS
    // volume. Held back with WEATHER (the row not shown)
    int rain_volume = 100;
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
    bool settings_back_keeps = true;  // settingsback: Back on the game's DRIVER DETAILS > SETTINGS pages keeps the changes (false:
                                      // the game's cancel; tools/native/settings_back.h)
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
    // Sound output (audioq; src/audio/spu2_mixer_impl.h Limiter): 0 Clean (the default: a peak limiter instead of the
    // SPU2's hard clip on the final output, 1 ms later), 1 Original (the PS2's own clamped output). The sound only:
    // the game and its logs are the same either way (B3_AUDIO_HARD_CLIP=1 forces Original)
    int sound_output = 0;
    int custom_music_volume = 100;    // musicvol1: CUSTOM MUSIC VOLUME, 0..200 % in steps of 10: the custom songs only, on
                                      // top of RADIO VOLUME (100: as loud as the game's own songs; host audio only)
    int custom_music_loudness = 1;    // musicloud: CUSTOM MUSIC LOUDNESS, 0 Off (each file's own level), 1 Even out (each
                                      // song measured, EBU R128, and brought to the game's own songs' level; host audio only)
    int other_music_apps = 0;         // nowplaying1: 0 off (no system calls at all), 1 another app's song in the EA TRAX popup
                                      // (the system's now-playing interface; tools/native/now_playing_popup.h; drawn only)
    // Language (1.0.1, b3asset/language.h): the game's text from the player's own European disc, imported once by the
    // launcher into <preferences>/languages. Host-only: the game opens DATA/GLOBALUS.BIN and HEADUS.BIN as on the US
    // disc and gets that language's GLOBAL<code>.BIN / HEAD<code>.BIN instead (the same string ids). Next start.
    std::string language;             // a disc's code ("IT", "FR", "GE", ...); empty: English (the game's own text)
    int voices = 0;                   // 1.3.0 VOICES: 0 the LANGUAGE's voices (French, German, Italian, Spanish): the DJ when
                                      // its banks are imported (b3asset/language.h dj_*; the game opens its E_DJ*.RWS banks
                                      // and gets the language's) and the race training's narration (tools/native/
                                      // training_voice.h); 1 always the English ones. Next start
    // Accessibility (remaster-features "Accessibility"; PC SETTINGS > GAME, section ACCESSIBILITY). The defaults are
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
    // replayview: RACE REPLAY (GAME > MODES; tools/native/replay_view.h): 0 off (the results pages and the game's replay
    // untouched), 1 a race's results page offers REPLAY, the game's own replay of the race. Applies at the next start
    int replays = 0;
    // gameopts (remaster-features §24; tools/native/game_options.h), GAMEPLAY-CHANGING, opt-in, off by default: both apply
    // to SINGLE EVENT and the two-player races only (World Tour, Crash party and online stay the game's), latched when the
    // race starts, and keep that race's results apart from the game's records (no records, profile totals or unlocks;
    // ghosts and best times in their own folder). mirror_mode: MIRROR MODE, the world drawn mirrored left to right and
    // the players' steering inverted with it (the HUD stays as it is). crashbreaker_all: CRASHBREAKER IN EVERY MODE, the
    // Crashbreaker after any crash in RACE / TIME ATTACK / ROAD RAGE (Crash mode has its own)
    bool mirror_mode = false;
    bool crashbreaker_all = false;
    // gamemods1: with crashbreaker_all, Full (CRASHBREAKER IN EVERY MODE's third value): the rivals and the other player in
    // the blast are wrecked too and counted as the player's Aftertouch Takedowns (tools/native/game_options.h blast)
    bool crashbreaker_full = false;
    // CRASHBREAKER AFTER (shown with crashbreaker_all on): the vehicles the player's crash must hit (the game's own crash
    // count, CB3CrashCombo's) before the breaker is ready, 0..3; 0: at once when the crash starts
    int crashbreaker_cars = 1;
    // party1: GAME > PARTY MODES (tools/native/party_modes.h), GAMEPLAY-CHANGING, off by default, the same races, latch,
    // replay entry and records apart as GAMEPLAY's. party_crazy_ai: CRAZY AI PHYSICS 0 off, 1 Basic, 2 Local Wildlife,
    // 3 Mayhem; party_moon_jump: MOON JUMP 0 off,
    // 1..3 its lift (the MOON JUMP port action held); party_crash: CRASH ON DEMAND (the CRASH port action); party_ai_driver:
    // the AI DRIVER port action hands player 1's car to the AI and back; party_kamikaze: KAMIKAZE TRAFFIC; party_tuk_tuks:
    // TUK-TUKS, every race car of a SINGLE EVENT race the tuk-tuk (trafficseries' traffic vehicles as race cars)
    int party_crazy_ai = 0;
    int party_moon_jump = 0;
    bool party_crash = false;
    bool party_ai_driver = false;
    bool party_kamikaze = false;
    bool party_tuk_tuks = false;
    // trafficcheck (remaster-features §33; tools/native/traffic_check.h), GAMEPLAY-CHANGING, opt-in, off by default: TRAFFIC
    // CHECK (2 PLAYERS), traffic pushed ahead instead of crashing you (Burnout Revenge's traffic check), with its chain
    // scored, in the two-player races (Single Event has its own row on SELECT OPTIONS: race_choice.h). 0 Off, 1 Revenge
    // (cars and vans going your way), 2 Extreme (oncoming, crossing and heavy traffic too). Latched when the race starts;
    // that race keeps its results apart from the game's records (ghosts and best times in their own folder)
    int traffic_check = 0;
    // Controller rumble (rumble1, remaster-features §3; src/native/pad_actuator.h; native_play). 0 off (the default): the
    // game's rumble requests are left pending as before (no motor driven; the run identical to one without the row).
    // 1 on: they are acknowledged and their motor values given to the pads (SDL rumble). GAME-VISIBLE, opt-in: the game
    // then sees its requests answered (the result it ignores, and the RPCs' virtual time); gameplay does not read them
    // (docs/pc-settings.md §3.8). Strength and triggers are host-only.
    int rumble = 0;
    int rumble_strength = 100;        // the motors' speed the pads are given, percent (10..100)
    bool analog_pedals = true;        // 1.0.1 ANALOG TRIGGERS: a trigger on accelerate / brake is the game's analog throttle /
                                      // brake (src/native/input_keys.h GamepadTuning::analog_pedals); off: 1.0.0's pressure
    bool rumble_triggers = false;     // also the triggers' own motors (Xbox One / Series impulse triggers) where a pad has them
    // DualSense adaptive triggers (pad1, remaster-features §5; src/native/pad_triggers.h; the window). Presentation only:
    // 0 off (the default: no effect is ever sent to a pad), 1 on: PS5 pads' triggers resist, buzz and kick from the game's
    // state as the window reads it (nothing is written to the game; the reports are unchanged). Strength: percent (10..100)
    int adaptive_triggers = 0;
    int adaptive_triggers_strength = 100;
    // Car series on the shoulders (carsel1; src/native/input_keys.h ShoulderSeries; native_play). 1 on (the default since
    // 2026-11-23, the owner's call): on the car select page, L1 / R1 presses reach the game as Left / Right (the page's own
    // series change) and small L1 / R1 glyphs (the device's buttons) are drawn beside its series arrows. 0 off: the reports
    // untouched, nothing drawn. INPUT-CHANGING (the game reads presses the player made on other buttons; the page itself
    // reads no shoulder button). An ini of version 1 with 0 is read as 1 (from_ini: 0 was only ever the old default)
    int series_shoulders = 1;
    // carstats1 / blinkers1 / rrparts1 (tools/native/): CAR STATS (carstats1, car_stats.h): 1 (the
    // default) SELECT CAR shows GRIP and ACCELERATION bars beside TOP SPEED and WEIGHT, from each car's handling values
    // (front end only); 0 the game's two. TURN SIGNALS (blinkers1, turn_signals.h): 1 the AI racers' indicators blink
    // before bends (their lights only); default 0. ROAD RAGE PARTS (rrparts1, road_rage_parts.h): 1 every kind of
    // loose part can come off a damaged car in an offline Road Rage (loose parts are physics bodies: gameplay; that
    // race's records kept apart); default 0, the game's
    int car_stats = 1;
    int turn_signals = 0;
    int road_rage_parts = 0;
    // Garage camera (garagecam1; src/b3replay/garagecam.h): on (the default), on the front end's car pages the right
    // stick (or a mouse drag on the car) turns the view round the car, the game's spin back a second after it is let
    // go. Drawn only: the game's camera, input and state are untouched (without the stick the frames are the game's).
    // Off: the car pages as the game draws them, and a mouse click there presses Cross on the press as before
    bool garage_camera = true;
    // Orbit camera (orbitcam1; src/b3replay/orbitcam.h): on (the default), in a race the right stick (or the mouse with
    // its right button held) swings the chase camera round the car, easing back to the game's view a moment after it is
    // let go; each split-screen player with their own pad. Only pads whose layout leaves the right stick free while
    // driving (not the PlayStation layout: its right stick is the analog throttle and brake). Drawn only: the game's
    // camera, input and state are untouched (without the stick the frames are the game's). Off: none of it
    bool orbit_camera = true;
    // gamemods1 (remaster-features §37). START AT (tools/native/boot_boost.h): 0 the
    // intro films (the game's), 1 the title screen, 2..4 the main menu with profile 1..3 loaded. Read at the start
    int boot_to = 0;
    // IDLE PAUSE / END VERY LONG EVENTS (tools/native/game_limits.h): on = the game's; off: a race never pauses itself
    // after 5 minutes idle / an event is not ended by the game's 25-40 minute limits (its records then not kept)
    bool idle_pause = true;
    bool long_event_end = true;
    // extras (2026-11-23; PC SETTINGS > EXTRAS, docs/pc-settings.md §7.46): the port's gameplay and content additions,
    // each off by default, so a fresh install plays as the game shipped. native_play takes them as one word
    // (extras_bits, b3r_extras); a replay plays with the recording's. The SELECT OPTIONS rows one by one (their RaceRow
    // order, tools/native/race_choice.h; TIME OF DAY is night's, held back):
    bool extra_traffic = false;            // TRAFFIC
    bool extra_laps = false;               // LAPS
    bool extra_road_rage_rivals = false;   // ROAD RAGE's RIVALS (the game: 3; the row: 1 / 2 / 3)
    bool extra_timed_elimination = false;  // TIMED ELIMINATION (ELIMINATOR's)
    bool extra_transmission = false;       // TRANSMISSION (the game's manual gearbox)
    bool extra_start = false;              // START (Rolling / Standing, with the game's BOOST START)
    bool extra_rival_style = false;        // RIVAL STYLE
    bool extra_burnout_points = false;     // BURNOUT POINTS
    bool extra_time_limit = false;         // TIME LIMIT
    bool extra_traffic_pattern = false;    // TRAFFIC PATTERN
    bool extra_rivals_cars = false;        // RIVALS' CARS
    bool extra_traffic_check = false;      // TRAFFIC CHECK (single races; two-player: traffic_check below)
    // SINGLE EVENT: ELIMINATOR (SELECT MODE's fifth row; on by default: the owner's, 2026-11-23); MORE RIVALS (RACE's
    // RIVALS up to 7: 8 cars)
    bool extra_eliminator = true;
    bool extra_more_rivals = false;
    // SELECT CAR: HEAVYWEIGHTS in every Single Event mode (the game: Crash only); the TRAFFIC / TRAFFIC SPECIAL series;
    // the CAR VIEWER (YOUR GARAGE)
    bool extra_heavyweights = false;
    bool extra_traffic_series = false;
    bool extra_car_viewer = false;
    // CUSTOM / BETA series: 0 off, 1 on, -1 an install from before EXTRAS (pc_settings.ini version 1 or 2): on when the
    // folder already holds a usable car (resolve_extra_cars; the player put them there). The launcher's imports turn on
    int extra_custom_cars = 0;
    int extra_beta_cars = 0;
    // EXPERIMENTAL (1.4.0, for feedback; race_time.h, weather §38): NIGHT and RAIN, each its own row. NIGHT gives SELECT
    // OPTIONS a TIME OF DAY row (Day / Night), RAIN a WEATHER row (Clear / Rain); both: both rows (Night + Rain: night
    // rain); neither: none (condrows, the owner's). MOON shows with NIGHT, RAIN VOLUME with RAIN. A Single Event race at
    // night or in the rain counts as any (drawn only; the owner's). World Tour (shown with NIGHT / RAIN):
    // 0 Off, 1 Always, 2 Random (each event's start); drawn only, so its progress and records count
    bool extra_night = false;
    bool extra_rain = false;
    int night_world_tour = 0;
    int rain_world_tour = 0;
    // hudmods (remaster-features §29): opt-in, all off by default.
    // RPM METER (src/b3replay/hudmods.h): a thin segmented bar of the engine revs under the race speedometer. Drawn only
    bool rpm_meter = false;
    // BOOST BAR SHAKE (src/b3replay/hudmods.h): the boost bar shakes while the car boosts, as in the game's April 2004
    // preview build. Drawn only
    bool boost_shake = false;
    // TAKEDOWN STREAK PITCH (tools/native/takedown_pitch.h; native_play): outside World Tour the takedown sound climbs in
    // pitch once the takedowns since the last crash pass 10 (World Tour's takedown-target rule). Sound only
    bool takedown_pitch = false;
    // Orbit style (orbitcam2; ChaseOrbit::Style): 0 Look (the default; as in Burnout Revenge: the stick's direction is where
    // the view looks, eased 10 % of the way every 1/60 s, held 0.4 s after the stick is let go, the field of view up to
    // 20 degrees narrower with the eye in front of the car), 1 Swing (orbitcam1's: the stick turns the view while held)
    int orbit_style = 0;
    // Camera roll (camroll1; src/b3replay/orbitcam.h ChaseRoll): 0 off (the default), 1
    // subtle (up to 4 degrees), 2 strong (up to 8): in a race the chase camera leans into corners. Drawn only
    int camera_roll = 0;
    // Finish camera (finishcam1; src/b3replay/finishcam.h): 0 the game's (the default), 1
    // set pieces: after the finish line the race view cycles through 12 set pieces of camera angles round the line, 5 s
    // each (their offsets read from the disc's VDB.XML). Drawn only
    int finish_camera = 0;
    // options3: DRIVER LABELS over the rival cars (0 Off, 1 names, 2 names + distance, 3 distance; drawn only:
    // src/b3replay/driverlabels.h)
    int driver_labels = 0;
    // options3: CRASH CAMERA (Off: when you crash the view stays behind your car instead of the game's crash camera; drawn
    // only: src/b3replay/crashhold.h)
    bool crash_camera = true;
    bool replay_camera_stay = false;   // replaycams (REPLAY CAMERA CUTS, 660): true Stay (the viewer's angle through crashes), false Game
    // options3: DEFORMATION, how much the cars' bodies crumple (0 100 % the game's, 1 none, 2 50 %, 3 150 %, 4 200 %;
    // GAMEPLAY-CHANGING, its races' records kept apart: tools/native/game_mods.h)
    int deformation = 0;
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
    // uploadbatch (ini only, no page row): the route's textures and buffers go up to the GPU in a few batched submits
    // (b3gfx Device::upload_batch; Vulkan with a window, SDL 3.4.2 only). false: one submit per upload, as on the other
    // backends (a troubleshooting switch: slower track loads, the same picture). $B3_UPLOAD_BATCH wins
    bool upload_batch = true;
    // d3d12 (GRAPHICS API, Windows only; GRAPHICS > IMAGE): 0 Auto (Vulkan, Direct3D 12 when no working Vulkan device can
    // be created), 1 Vulkan, 2 D3D12 (each falls back to the other when it cannot start). Applies at the next start
    int graphics_api = 0;
    // scenedepth (ini only, no page row): night's lights, the AO, the depth of field and the motion blur read the scene
    // pass's own depth instead of drawing the frame's geometry a second time (b3gfx Options::scene_depth). -1: the
    // renderer's choice for its backend (off on every backend for now), 0 off, 1 on. $B3_SCENE_DEPTH wins
    int scene_depth = -1;

    bool operator==(const Settings&) const = default;
};

// The presets touch the quality and effects values only (not the window, frame rate, camera or interface).
Settings with_preset(Settings s, Preset p);
// effects2: the levels (Settings::sparks, popcorn): the name of one, the share of the particles it draws (0, 25, 50, 100 %)
enum : int { kLevelOff = 0, kLevelLow = 1, kLevelMedium = 2, kLevelHigh = 3 };
const char* level_name(int level);
// effects2: RENDER SCALE's choices (percent), the nearest one's index of a value. perf4: above 100 supersampled (drawn larger,
// brought down with a box filter; capped to what the GPU's targets take)
constexpr int kRenderScales[] = {200, 150, 125, 100, 85, 75, 67, 50};
std::size_t render_scale_index(int percent);
// effects2: BATTERY SAVER's change while the PC runs on battery (nothing when the row is off): the frame rate capped to 60
// and SPARKS / CRASH DEBRIS to at most Medium. Host-only; the stored settings are never changed (the host applies this copy).
Settings on_battery(Settings s);
int level_percent(int level);
Preset matching_preset(const Settings& s);    // kCustom when no preset gives exactly these values
const char* preset_name(Preset p);

// key = value text (also the HSET payload). Unknown keys are ignored, bad values keep the default.
// The ini's `version`: 4. 2 (2026-11-23): series_shoulders' default 0 -> 1 (a version 1 file's 0 is read as 1).
// 3 (slowcpu): frame_cap 0 of an older file read as Auto. 4 (extras, 2026-11-23): the EXTRAS rows (an older file's
// CUSTOM / BETA series are -1: on when their folder holds cars). Each step is keyed "version > 0 && version < N"; a file
// without the line (written by hand, a test's) is taken as it is.
inline constexpr int kIniVersion = 4;
std::string to_ini(const Settings& s);
// `version`: the file's `version` line (0: none)
Settings from_ini(std::string_view text, std::vector<std::string>* warnings = nullptr, int* version = nullptr);

// extras: the EXTRAS rows as one word (native_play's b3r_extras; a replay's "extras"). Bits 0..12: the SELECT OPTIONS
// rows by their RaceRow index (race_choice.h: 0 TRAFFIC, 1 LAPS, 2 ROAD RAGE's RIVALS, 4 TIMED ELIMINATION, 5
// TRANSMISSION, 6 START, 7 RIVAL STYLE, 8 BURNOUT POINTS, 9 TIME LIMIT, 10 TRAFFIC PATTERN, 11 RIVALS' CARS, 12 TRAFFIC
// CHECK; 3 TIME OF DAY is night's, held back); then the modes and series.
enum ExtraBit : std::uint32_t {
    kExtraTraffic = 1u << 0, kExtraLaps = 1u << 1, kExtraRoadRageRivals = 1u << 2, kExtraTimedElimination = 1u << 4, kExtraTransmission = 1u << 5,
    kExtraStart = 1u << 6, kExtraRivalStyle = 1u << 7, kExtraBurnoutPoints = 1u << 8, kExtraTimeLimit = 1u << 9,
    kExtraTrafficPattern = 1u << 10, kExtraRivalsCars = 1u << 11, kExtraTrafficCheck = 1u << 12,
    kExtraEliminator = 1u << 16, kExtraMoreRivals = 1u << 17, kExtraHeavyweights = 1u << 18, kExtraTrafficSeries = 1u << 19,
    kExtraCustomCars = 1u << 20, kExtraBetaCars = 1u << 21, kExtraCarViewer = 1u << 22,
    kExtraNight = 1u << 23, kExtraRain = 1u << 24,   // EXPERIMENTAL
    // World Tour (with NIGHT / RAIN): 0 Off, 1 Always, 2 Random in two bits each (extras_world_tour_night / _rain)
    kExtraNightWorldTour = 3u << 25, kExtraRainWorldTour = 3u << 27,
};
inline int extras_world_tour_night(std::uint32_t w) { return int(w >> 25 & 3u) % 3; }
inline int extras_world_tour_rain(std::uint32_t w) { return int(w >> 27 & 3u) % 3; }
inline constexpr std::uint32_t kExtraRaceRows = 0x1FF7u;   // bits 0..2, 4..12
// "all" (B3NATIVE_EXTRAS=all, race_check's default, a recording from before EXTRAS): every extra but the experimental ones
inline constexpr std::uint32_t kExtrasAll = kExtraRaceRows | 0x7F0000u;
inline constexpr std::uint32_t kExtrasMask = kExtrasAll | kExtraNight | kExtraRain | kExtraNightWorldTour | kExtraRainWorldTour;
// The word. CUSTOM / BETA at -1 (not resolved yet) count as on: the game side then takes them when the folder has cars,
// as before EXTRAS
std::uint32_t extras_bits(const Settings& s);
// The CUSTOM / BETA series of an install from before EXTRAS (-1): on when their folder holds a usable car, else off.
// true when a value changed (the caller saves)
bool resolve_extra_cars(Settings& s, bool custom_has_cars, bool beta_has_cars);
// portable mode (1.3.0): the folder settings (hd_pack_dir, music_folder) that lie inside `install` (the program's
// folder) are kept in the file as "<install>/..." (b3port/user_dir.h install_relative) and read back against it, so a
// USB stick whose drive letter changes still finds them. load_file / save_file do it with the program's own folder.
void paths_to_install(Settings& s, const std::filesystem::path& install);
void paths_from_install(Settings& s, const std::filesystem::path& install);
bool load_file(const std::string& path, Settings& s, std::string* why = nullptr);   // false: no file (s unchanged)
bool save_file(const std::string& path, const Settings& s, std::string* why = nullptr);
// settingsui: the launcher and the game both edit pc_settings.ini; each writes only what it changed. `mine` with every
// ini key it has as `base` (what this program last read or wrote) taken from `disk` (the file as it is now), the keys it
// changed kept.
Settings merge_changes(const Settings& base, const Settings& mine, const Settings& disk);
// The file read again (none: `base`), merge_changes into `mine`, written; `mine` is then what the file holds.
bool save_merged(const std::string& path, const Settings& base, Settings& mine, std::string* why = nullptr);

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
