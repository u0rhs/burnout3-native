// settings1: the PC SETTINGS page's rows (include/b3settings/page.h).
// The menu model (class Menu: layout, colours, input, animation) is in menu.cpp.
#include "b3settings/page.h"
#include "b3settings/i18n.h"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <map>
#include <unordered_map>

namespace b3settings {

namespace {

// Row ids (stable: the menu keeps the focus on a row id across rebuilds).
enum : std::uint32_t {
    kRowPreset = 1, kRowFullscreen, kRowVsync, kRowFrameCap, kRowInterp, kRowHud, kRowFeLayout, kRowMovieFill, kRowLatency, kRowLead, kRowFlight, kRowPauseUnfocused, kRowFastLoading,
    kRowMsaa = 20, kRowFilter, kRowHd, kRowCarDetail, kRowShadows, kRowSkids, kRowSpecular, kRowSun, kRowCoronas, kRowDither, kRowWholeTrack,
    kRowBlur = 40, kRowRadial, kRowBloom, kRowPost, kRowSparks, kRowScrapes, kRowPopcorn, kRowDamage, kRowGlass,
    kRowFov = 60, kRowFovCorrection, kRowCameraDistance, kRowCameraHeight,   // 62 / 63: 1.1 chase camera
    kRowGlyphs = 81, kRowSkipHint, kRowElsewhere, kRowDateFormat = 85,
    kRowPlaylistMode = 90, kRowOtherMusicApps,   // kRowOtherMusicApps = 91: nowplaying1
    kRowCustomMusicVolume = 92,   // musicvol1 (CUSTOM MUSIC VOLUME; after PLAYLIST)
    kRowSoundOutput = 93,         // audioq (SOUND OUTPUT; AUDIO > OUTPUT, first)
    kRowCustomMusicLoudness = 94, // musicloud (CUSTOM MUSIC LOUDNESS; after CUSTOM MUSIC VOLUME)
    kRowFlashes = 120, kRowHudColour, kRowBoostMode, kRowHudSize,   // accessibility (kRowHudSize = 123: textsize1)
    kRowGhost = 130,   // ghost1 (Time Attack ghosts)
    kRowSpeedrunTimer = 140, kRowInstantRestart,   // speedrun1
    kRowMirrorMode = 430, kRowCrashbreakerAll = 431, kRowCrashbreakerCars = 432,   // gameopts (GAME > GAMEPLAY; 430-439 are gameopts')
    // party1 (530-549)
    kRowPartyCrazyAi = 530, kRowPartyTukTuks = 531, kRowPartyMoonJump = 532, kRowPartyCrash, kRowPartyAiDriver, kRowPartyKamikaze,
    kRowTrafficCheck = 433,   // trafficcheck (GAME > GAMEPLAY: TRAFFIC CHECK (2 PLAYERS); 430-432 are gameopts')
    kRowRumble = 150, kRowRumbleStrength, kRowRumbleTriggers,   // rumble1 (controller rumble)
    kRowAnalogPedals = 272,   // 1.0.1 ANALOG TRIGGERS
    kRowGarageCamera = 290,   // garagecam1 (GARAGE CAMERA; CAR > GARAGE, first)
    kRowBootTo = 470, kRowIdlePause = 471, kRowLongEventEnd = 472,   // gamemods1 (470-489 gamemods1's)
    kRowOrbitCamera = 440,    // orbitcam1 (ORBIT CAMERA; DISPLAY > CAMERA, after CAMERA HEIGHT; 440-444 orbitcam's)
    kRowRpmMeter = 450, kRowBoostShake = 451,   // hudmods (DISPLAY > RACE HUD; 450-459 hudmods')
    kRowTakedownPitch = 452,                    // hudmods (AUDIO > SOUND)
    kRowOrbitStyle = 441,     // orbitcam2 (ORBIT STYLE: Look / Swing; after ORBIT CAMERA)
    kRowCameraRoll = 460,     // camroll1 (CAMERA ROLL; DISPLAY > CAMERA, after ORBIT STYLE; 460-469 betacam's)
    kRowFinishCamera = 461,   // finishcam1 (FINISH CAMERA; DISPLAY > CAMERA, after CAMERA ROLL)
    kRowCrashCamera = 500,    // options3 (CRASH CAMERA; DISPLAY > CAMERA, after ORBIT CAMERA; 500-529 options3's)
    kRowReplayCameraCuts = 660,   // replaycams (REPLAY CAMERA CUTS; DISPLAY > CAMERA, after CRASH CAMERA; 660-669 replaycams')
    kRowDeformation = 502,    // options3 (DEFORMATION; GAME > GAMEPLAY, last)
    kRowDriverLabels = 501,   // options3 (DRIVER LABELS; GAME > INTERFACE, after FPS COUNTER POSITION)
    kRowRecordReplays = 160,   // replay1 (replay files)
    kRowRaceReplay = 620,      // replayview (RACE REPLAY; GAME > MODES, after RECORD REPLAYS; 620-629 replayview's)
    kRowDeveloperMods = 170,   // tune4 (the tuning console)
    kRowSplitLayout = 180,     // splitlayout1 (the 2-player split screen's layout; DISPLAY > LAYOUT)
    kRowCarPaint = 280, kRowPaintRed, kRowPaintGreen, kRowPaintBlue, kRowDecals,   // carpaint (CAR > CAR PAINT)
    kRowPaintFinish = 285, kRowP2Paint, kRowP2Red, kRowP2Green, kRowP2Blue,           // padpaint (FINISH; PLAYER 2 CAR PAINT)
    kRowP2Decals = 291,                                                              // (290: GARAGE CAMERA)
    kRowPaintPreset = 292, kRowP2PaintPreset = 293,   // settingsui: COLOUR / P2 COLOUR (CAR > CAR PAINT: the ready colours)
    kRowBoostElsewhere = 124,                         // settingsui: GAME > ACCESSIBILITY's pointer to CONTROLS > DRIVING > BOOST
    kRowTimeOfDay = 370, kRowMoon, kRowSurfaceDetail, kRowAmbientOcclusion,   // night (GRAPHICS > LIGHTING, after WORLD; 300-351: page.h CONTROLS)
    kRowSunShadows = 390,   // sunshadow (GRAPHICS > LIGHTING, after AMBIENT OCCLUSION; 390-394 are sunshadow's)
    kRowTunnelLights = 374,   // nightfix (TUNNEL LIGHTS; GRAPHICS > LIGHTING, after SUN SHADOWS)
    kRowWeather = 650,        // weather (WEATHER; GRAPHICS > LIGHTING, after TUNNEL LIGHTS; 650-659 are weather's)
    kRowRainVolume = 651,     // weather (RAIN VOLUME: AUDIO, held back with WEATHER)
    kRowAdaptiveTriggers = 190, kRowAdaptiveStrength,   // pad1 (DualSense adaptive triggers; CONTROLS > FEEDBACK)
    kRowSeriesShoulders = 200,   // carsel1 (car series on L1 / R1; CAR > GARAGE)
    kRowCarStats = 490, kRowTurnSignals = 491, kRowRoadRageParts = 492,   // carstats1 / blinkers1 / rrparts1 (490-499 kept for them)
    kRowLauncherAtStart = 210,   // launchskip1 (OPEN THE LAUNCHER AT START; GAME > STARTING AND LOADING)
    kRowBatterySaver = 245, kRowRenderScale = 246, kRowUpscaling = 247,   // 247: 1.1 UPSCALING      // effects2 (BATTERY SAVER; DISPLAY > WINDOW)
    kRowCarReflect = 235,        // reflect1 (CAR REFLECTIONS; CAR > CAR LOOK, after CAR DETAIL)
    kRowReflectDistance = 610, kRowReflectDetail = 611, kRowReflectUpdate = 612,   // reflect2 (after CAR REFLECTIONS; 610-619 kept)
    kRowReflectResolution = 614,   // reflect2 D (REFLECTION RESOLUTION, after REFLECTION UPDATE)
    kRowPropShadows = 613,       // propshadow1 (PROP SHADOWS; GRAPHICS > LIGHTING, after SUN SHADOWS)
    kRowBloomLevel = 260,        // bloom2 (BLOOM: Off / Soft / Standard; replaces the on / off toggle kRowBloom on the page)
    kRowFps = 255, kRowFpsCorner,   // fpsoverlay2 (FPS COUNTER and its corner; GAME > INTERFACE)
    kRowSpeedUnits = 220, kRowPhotoRow = 240,   // units1 (SPEED UNITS; GAME > LANGUAGE AND UNITS)
    kRowSettingsBack = 241,      // settingsback (SETTINGS BACK BUTTON; GAME > INTERFACE, after PHOTO MODE IN PAUSE MENU)
    kRowXboxTextures = 383,      // xboximport (XBOX TEXTURES; GRAPHICS > TEXTURES, after HD TEXTURES)
    kRowHdMemory = 389,          // texpack (HD TEXTURE MEMORY; GRAPHICS > TEXTURES, after XBOX TEXTURES)
    kRowProfile = 110, kRowSaveFolder, kRowSaveStatus,   // save1 (the actions: page.h kAction*)
    // extras (EXTRAS; 700-729): SINGLE EVENT, SELECT OPTIONS' rows one by one, SELECT CAR
    kRowExtraEliminator = 700, kRowExtraMoreRivals = 701,
    kRowExtraTraffic = 702, kRowExtraLaps, kRowExtraTimedElimination, kRowExtraTransmission, kRowExtraStart, kRowExtraRivalStyle,
    kRowExtraBurnoutPoints, kRowExtraTimeLimit, kRowExtraTrafficPattern, kRowExtraRivalsCars, kRowExtraTrafficCheck,   // 702-712
    kRowExtraHeavyweights = 713, kRowExtraTrafficSeries, kRowExtraCustomCars, kRowExtraBetaCars, kRowExtraCarViewer,   // 713-717
    kRowExtraRoadRageRivals = 718,
    kRowExtraNight = 719, kRowExtraRain = 720, kRowNightWorldTour = 721, kRowRainWorldTour = 722,   // EXPERIMENTAL
    kSection = 0xFFFF0000u,
};

// extras: a SELECT OPTIONS row's setting by its EXTRAS row id
bool* extra_row_field(Settings& s, std::uint32_t id) {
    switch (id) {
    case kRowExtraTraffic: return &s.extra_traffic;
    case kRowExtraLaps: return &s.extra_laps;
    case kRowExtraRoadRageRivals: return &s.extra_road_rage_rivals;
    case kRowExtraTimedElimination: return &s.extra_timed_elimination;
    case kRowExtraTransmission: return &s.extra_transmission;
    case kRowExtraStart: return &s.extra_start;
    case kRowExtraRivalStyle: return &s.extra_rival_style;
    case kRowExtraBurnoutPoints: return &s.extra_burnout_points;
    case kRowExtraTimeLimit: return &s.extra_time_limit;
    case kRowExtraTrafficPattern: return &s.extra_traffic_pattern;
    case kRowExtraRivalsCars: return &s.extra_rivals_cars;
    case kRowExtraTrafficCheck: return &s.extra_traffic_check;
    case kRowExtraEliminator: return &s.extra_eliminator;
    case kRowExtraMoreRivals: return &s.extra_more_rivals;
    case kRowExtraHeavyweights: return &s.extra_heavyweights;
    case kRowExtraTrafficSeries: return &s.extra_traffic_series;
    case kRowExtraCarViewer: return &s.extra_car_viewer;
    case kRowExtraNight: return &s.extra_night;
    case kRowExtraRain: return &s.extra_rain;
    default: return nullptr;
    }
}
bool extra_row_on(const Settings& s, std::uint32_t id) {
    bool* f = extra_row_field(const_cast<Settings&>(s), id);
    return f && *f;
}

const int kCaps[] = {kFrameCapAuto, 0, 30, 60, 120, 144, 165, 240};   // slowcpu: Auto first
// carpaint: the paint sliders' levels, 0 .. 255 in steps of 15
constexpr int kPaintStep = 15;
constexpr std::size_t kPaintLevels = 255 / kPaintStep + 1;

// nowplaying1: OTHER MUSIC APPS' description (the launcher's Music page shows the same row)
std::string other_music_apps_text(const Settings& s, const Env& env) {
#if defined(__APPLE__)
    (void)s;
    (void)env;
    return "Not available on macOS: it has no public way for a game to read what another app is playing.";
#else
    std::string d = "Shows the song another app plays (Spotify, a browser, any player in the system's media controls) "
                    "in the EA TRAX popup when the song changes. Its sound stays in that app: turn RADIO VOLUME down in "
                    "Driver Details to hear only it. Drawn only.";
    if (s.other_music_apps && !env.now_playing.empty()) d += " Now: " + env.now_playing + ".";
    return d;
#endif
}
// mac5: BUTTON PROMPTS' values in the page's order (the ini's values: 6, the PS4 / PS5 style, came last; src/b3replay/prompts.h PromptMode)
const int kPromptOrder[] = {0, 1, 6, 2, 3, 4, 5};
const int kDeformOrder[] = {1, 2, 0, 3, 4};   // options3: DEFORMATION's page order (None, 50%, 100%, 150%, 200%) as stored values
const int kMsaa[] = {1, 2, 4, 8};
const Preset kPresets[] = {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra, Preset::kCustom};
constexpr const char* kRestart = " Applies at the next start.";

Row section(std::uint32_t id, std::string label) {
    Row r;
    r.id = kSection + id;
    r.kind = RowKind::kSection;
    r.label = std::move(label);
    return r;
}
Row toggle(std::uint32_t id, std::string label, bool on, std::string desc) {
    Row r;
    r.id = id;
    r.kind = RowKind::kToggle;
    r.label = std::move(label);
    r.values = {"Off", "On"};
    r.value = on ? 1 : 0;
    r.description = std::move(desc);
    return r;
}
Row choice(std::uint32_t id, std::string label, std::vector<std::string> values, std::size_t value, std::string desc) {
    Row r;
    r.id = id;
    r.kind = RowKind::kChoice;
    r.label = std::move(label);
    r.values = std::move(values);
    r.value = std::min(value, r.values.empty() ? std::size_t(0) : r.values.size() - 1);
    r.description = std::move(desc);
    return r;
}
Row slider(std::uint32_t id, std::string label, std::size_t levels, std::size_t value, std::string text, std::string desc) {
    Row r;
    r.id = id;
    r.kind = RowKind::kSlider;
    r.label = std::move(label);
    r.levels = levels;
    r.value = std::min(value, levels - 1);
    r.level_text = std::move(text);
    r.description = std::move(desc);
    return r;
}
Row note(std::uint32_t id, std::string label, std::string value, std::string desc) {
    Row r;
    r.id = id;
    r.kind = RowKind::kAction;
    r.label = std::move(label);
    r.values = {std::move(value)};
    r.description = std::move(desc);
    r.disabled = true;
    return r;
}
template <class T, std::size_t N> std::size_t index_of(const T (&a)[N], T v) {
    for (std::size_t i = 0; i < N; ++i) if (a[i] == v) return i;
    return 0;
}
std::size_t fov_level(int fov) { return fov <= 0 ? std::size_t((kGameFov - kFovMin) / kFovStep) : std::size_t((std::clamp(fov, kFovMin, kFovMax) - kFovMin) / kFovStep); }
int fov_of_level(std::size_t level) {
    const int deg = kFovMin + int(level) * kFovStep;
    return deg == kGameFov ? 0 : std::min(deg, kFovMax);
}

// settingsui: FRAME RATE LIMIT's values: the page's, and the ini's own value among them when it is another (90 written
// by hand shows as 90, not as Unlimited; apply_edit reads the same list from the same settings)
std::vector<int> frame_caps(int current) {
    std::vector<int> caps(std::begin(kCaps), std::end(kCaps));
    if (current > 0 && std::find(caps.begin(), caps.end(), current) == caps.end())
        caps.insert(std::upper_bound(caps.begin() + 2, caps.end(), current), current);   // (after Auto and Unlimited)
    return caps;
}

// settingsui: the paint's ready colours (COLOUR / P2 COLOUR, above the RGB rows); each channel a step of the sliders'
struct PaintPreset { const char* name; int r, g, b; };
constexpr PaintPreset kPaintPresets[] = {
    {"Red", 225, 30, 30},    {"Orange", 255, 120, 0},  {"Yellow", 255, 210, 0},   {"Green", 30, 165, 60},    {"Blue", 30, 90, 225},
    {"Purple", 120, 45, 180}, {"Pink", 240, 90, 165},  {"White", 240, 240, 240}, {"Silver", 165, 165, 180}, {"Black", 15, 15, 15},
};
constexpr std::size_t kPaintPresetCount = std::size(kPaintPresets);
// The preset the colour is, or kPaintPresetCount (Custom: a colour of the player's own)
std::size_t paint_preset_index(const int rgb[3]) {
    for (std::size_t k = 0; k < kPaintPresetCount; ++k)
        if (kPaintPresets[k].r == rgb[0] && kPaintPresets[k].g == rgb[1] && kPaintPresets[k].b == rgb[2]) return k;
    return kPaintPresetCount;
}
// The row's values: the presets, and Custom after them only while the colour is none of them (so left / right step
// through the presets alone)
std::vector<std::string> paint_preset_names(const int rgb[3]) {
    std::vector<std::string> v;
    for (const PaintPreset& p : kPaintPresets) v.push_back(p.name);
    if (paint_preset_index(rgb) == kPaintPresetCount) v.push_back("Custom");
    return v;
}
// paint2
const char* const kLayerNames[kPaintLayers] = {"Body", "Decals", "Wheels", "Boost glow"};
// A layer's COLOUR row: Game, the ready colours, and Custom while the colour is none of them; chips: Game none (-2)
Row layer_colour_row(const Settings& s0, int player, int layer, std::uint32_t id, const char* label, const char* desc) {
    Settings& s = const_cast<Settings&>(s0);   // (read only here)
    const PaintLayerRef L = paint_layer(s, player, layer);
    const int rgb[3] = {*L.rgb[0], *L.rgb[1], *L.rgb[2]};
    std::vector<std::string> values{"Game"};
    for (const std::string& n : paint_preset_names(rgb)) values.push_back(n);
    const std::size_t preset = paint_preset_index(rgb);
    const std::size_t value = *L.custom != 1 ? 0 : preset < kPaintPresetCount ? preset + 1 : values.size() - 1;
    Row r = choice(id, label, values, value, desc);
    std::int32_t sw = 0;
    for (int k = 0; k < 3; ++k) sw = sw << 8 | std::clamp(rgb[k], 0, 255);
    r.swatch = *L.custom == 1 ? sw : -1;
    r.chips.push_back(-2);
    for (std::int32_t c : paint_preset_colours()) r.chips.push_back(c);
    if (r.chips.size() < values.size()) r.chips.push_back(sw);
    return r;
}

// settingsui: nine tabs (AUDIO its own). Measured with the game's GlobalFont at the tab bar's size (4:3): with each tab
// as wide as its label needs (menu.cpp tab_edges) every language's labels stay above the 75 % shrink floor, French the
// least at 82 %, German 94 % (equal widths would cut nine tabs in every language, even English's CONTROLS at 72 %).
// false is the fallback of eight, the music section at the end of GAME
constexpr bool kAudioTab = true;

// AUDIO: where the game's own volumes are, then the music folder (the game's page; the launcher: its Music page)
void audio_rows(Tab& t, const Settings& s, const Env& env) {
    t.rows.push_back(note(kRowElsewhere, "VOLUMES AND DJ", "Driver Details",
                          "The game's own sound: RADIO VOLUME, the effects, the speech and the DJ are in DRIVER DETAILS > SETTINGS > AUDIO."));
    // audioq (src/audio/spu2_mixer_impl.h Limiter): the final output's loud moments
    t.rows.push_back(section(15, "OUTPUT"));
    t.rows.push_back(choice(kRowSoundOutput, "SOUND OUTPUT", {"Clean", "Original"}, std::size_t(std::clamp(s.sound_output, 0, 1)),
                            "Clean: the loudest moments (a crash over the engines and the music) are turned down for an instant "
                            "instead of clipping. Original: the PS2's own output, which crackles when it clips. The game is unchanged."));
    // hudmods (tools/native/takedown_pitch.h; remaster-features §29): sound only, off by default
    t.rows.push_back(section(44, "RACE SOUNDS"));
    t.rows.push_back(toggle(kRowTakedownPitch, "TAKEDOWN STREAK PITCH", s.takedown_pitch,
                            "On: outside World Tour, each takedown after your 10th since your last crash sounds higher, "
                            "as World Tour's takedown targets do. A crash starts the count again. Sound only."));
    if (s.extra_rain) {   // extras: EXPERIMENTAL RAIN on: its rain's sound (0..200 %, steps of 10)
        const int pct = std::clamp(s.rain_volume, 0, 200);
        t.rows.push_back(slider(kRowRainVolume, "RAIN VOLUME", 21, std::size_t(pct / 10), std::to_string(pct) + "%",
                                "The rain's sound, on top of the game's own EFFECTS volume."));
    }
    if (env.launcher) {
        t.rows.push_back(section(12, "MUSIC"));
        Row page = note(kActionOpenMusic, "MUSIC FOLDER", "Music",
                        "The launcher's Music page: your own music folder, the playlist, its volume and other music apps.");
        page.disabled = false;
        t.rows.push_back(page);
        return;
    }
    if (!env.music) return;   // the custom soundtrack (remaster-features §6; a host that plays it: native_play)
    t.rows.push_back(section(12, "MUSIC"));
    std::string name = "None";
    if (!s.music_folder.empty()) {
        std::string d = s.music_folder;
        while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
        const std::size_t cut = d.find_last_of("/\\");
        name = cut == std::string::npos ? d : d.substr(cut + 1);
    }
    std::string desc = "Your own music: a folder of MP3, FLAC, Ogg Vorbis or WAV files (subfolders too). The EA TRAX "
                       "popup shows each song's title, artist and album. RADIO VOLUME and the DJ stay in Driver Details.";
    if (!s.music_folder.empty()) desc += " Chosen: " + s.music_folder + ".";
    if (!env.music_status.empty()) desc += " " + env.music_status;
    Row pick = note(kActionChooseMusic, "MUSIC FOLDER", name, desc);
    pick.disabled = false;
    t.rows.push_back(pick);
    if (!s.music_folder.empty()) {
        Row none = note(kActionClearMusic, "REMOVE MUSIC FOLDER", "Remove", "Stop using the chosen folder (the files stay).");
        none.disabled = false;
        t.rows.push_back(none);
    }
    t.rows.push_back(choice(kRowPlaylistMode, "PLAYLIST", {"EA Trax only", "Custom only", "Mixed"}, std::size_t(std::clamp(s.playlist_mode, 0, 2)),
                            "Which songs the radio plays: the game's EA TRAX songs, the songs of your music folder, or "
                            "both. Your songs are listed after the game's on its EA TRAX page (200 at most), each set like "
                            "the game's songs. Custom only skips the game's songs. The game's shuffle setting applies to "
                            "your songs too. From the next song."));
    {   // musicvol1 (tools/native/custom_soundtrack.h): the custom songs' own level, on top of RADIO VOLUME
        const int pct = std::clamp(s.custom_music_volume, 0, 200) / 10 * 10;
        t.rows.push_back(slider(kRowCustomMusicVolume, "CUSTOM MUSIC VOLUME", 21, std::size_t(pct / 10), std::to_string(pct) + "%",
                                "How loud your own songs play, on top of RADIO VOLUME. They follow RADIO VOLUME (DRIVER "
                                "DETAILS > SETTINGS > AUDIO) and the radio's fades like the game's songs. At 100% a song "
                                "plays as loud as the game plays its own songs. A limiter keeps loud songs from clipping. "
                                "The game's songs and the DJ do not change. Live."));
    }
    // musicloud (tools/native/custom_soundtrack.h, b3music::LoudnessService): each song brought to the game's level
    t.rows.push_back(choice(kRowCustomMusicLoudness, "CUSTOM MUSIC LOUDNESS", {"Off", "Even out"},
                            std::size_t(std::clamp(s.custom_music_loudness, 0, 1)),
                            "Even out: each of your songs is measured once (in the background; kept for the next start) "
                            "and played as loud as the game's own songs, so a quiet file and a loud one sound alike. "
                            "A quiet song with loud peaks is raised only a little. Off: each file plays at its own "
                            "level. Live."));
    // nowplaying1 (tools/native/now_playing_popup.h): the system's now-playing interface, drawn only
    Row apps = choice(kRowOtherMusicApps, "OTHER MUSIC APPS", {"Off", "Show now playing"},
                      std::size_t(std::clamp(s.other_music_apps, 0, 1)), other_music_apps_text(s, env));
#if defined(__APPLE__)
    apps.disabled = true;   // no reader on macOS (src/native/now_playing.h)
#endif
    t.rows.push_back(apps);
}

}  // namespace

// settingsui (2026-11-20): nine tabs, each row where a player looks for it (every row id and ini key as before; only the
// tab, the section and a few labels moved): DISPLAY (the window, the layouts, the camera, the frame pacing), GRAPHICS
// (the quality preset first: it sets the rows of this tab and EFFECTS), EFFECTS, CAR (the paint, the car's look, the car
// pages), CONTROLS (feedback, driving, the bindings, the gyro), AUDIO (the music folder; the game's own volumes are in
// DRIVER DETAILS), GAME (the interface, language and units, the modes, accessibility, starting and loading), EXTRAS (the
// port's gameplay and content additions, each off by default: extras, §7.46), SAVES (the
// game only) and MODS (the code mods' rows, then DEVELOPER MODS). Section ids are ids too (Menu keeps rows by id): new
// sections take 31 and up (30: LIGHTING, kept for the night branch).
static_assert(kRowHd == kRowIdHdTextures && kRowXboxTextures == kRowIdXboxTextures, "page.h's row ids");

Row launcher_at_start_row(const Settings& s) {
    return toggle(kRowLauncherAtStart, "OPEN THE LAUNCHER AT START", s.launcher_at_start,
                  "On: starting the game shows the launcher first. Off: the launcher starts the game at once. "
                  "Hold Shift, or Back / Select on a controller, while it starts to open the launcher anyway. "
                  "Applies at the next start.");
}

Row portable_row(const Env::Portable& p, bool launcher) {
    const bool v = p.request >= 0 && !launcher ? p.request != 0 : p.on;
    Row r = toggle(kRowPortable, "PORTABLE MODE", v,
                   std::string("On: everything the port keeps for you (settings, saves, mods, logs, replays) is in a folder called "
                               "appdata beside the launcher, so the game can be carried on a USB stick. Off: in this computer's user folder. ") +
                       (launcher ? "The launcher moves your data now: it is copied and checked first, and the old folder is kept until the "
                                   "game has started from the new one."
                                 : "The launcher moves your data at its next start (it asks first)."));
    if (!p.disabled.empty()) {
        r.disabled = true;
        r.description += " " + p.disabled;
    }
    return r;
}

Content build_content(const Settings& s, const Env& env) {
    Content c;
    {
        Tab t{"DISPLAY", {}};
        t.rows.push_back(section(1, "WINDOW"));
        t.rows.push_back(toggle(kRowFullscreen, "FULLSCREEN", s.fullscreen, "Borderless fullscreen on the window's display (F11 also switches it)."));
        t.rows.push_back(toggle(kRowVsync, "VSYNC", s.vsync, "Waits for the display before showing each frame."));
        {   // a limit the page does not offer (frame_cap = 90 written by hand) is listed as itself, not as Unlimited
            const std::vector<int> caps = frame_caps(s.frame_cap);
            std::vector<std::string> names;
            std::size_t at = 0;
            for (std::size_t k = 0; k < caps.size(); ++k) {
                names.push_back(caps[k] == kFrameCapAuto ? "Auto" : caps[k] ? std::to_string(caps[k]) : "Unlimited");
                if (caps[k] == s.frame_cap) at = k;
            }
            t.rows.push_back(choice(kRowFrameCap, "FRAME RATE LIMIT", names, at,
                                    "The most frames per second drawn. The game itself always runs at its own speed. "
                                    "Auto: 60 on a processor with 4 threads or fewer, so the game itself keeps up; no limit on others. "
                                    "Unlimited: no limit at all, for fast processors."));
        }
        t.rows.push_back(toggle(kRowBatterySaver, "BATTERY SAVER", s.battery_saver,
                                "On: while the PC runs on battery, the frame rate is held to 60 and the sparks and crash debris drawn at "
                                "most at Medium. Plugged in, nothing changes. Drawn only: the game is the same."));
        t.rows.push_back(section(31, "LAYOUT"));
        t.rows.push_back(choice(kRowHud, "RACE HUD LAYOUT", {"4:3", "Wide"}, s.hud_wide ? 1 : 0,
                                "Wide moves the race HUD to the window's edges. 4:3 keeps the PS2 layout."));
        t.rows.push_back(choice(kRowFeLayout, "MENU LAYOUT", {"Edges", "4:3", "Panels"}, std::size_t(std::clamp(s.fe_layout, 0, 2)),
                                "The menus on a wide screen: panels to the edges, the PS2's 4:3 picture, or anchored to the edges."));
        t.rows.push_back(choice(kRowMovieFill, "MENU MOVIE SIDES", {"Black", "Blur"}, s.movie_fill ? 1 : 0,
                                "Beside the 4:3 menu movies on a wide screen: black, or a blurred copy of the movie."));
        // splitlayout1 (src/b3replay/splitlayout.h): the 2-player views
        t.rows.push_back(choice(kRowSplitLayout, "SPLIT SCREEN", {"Top / bottom", "Side by side"}, std::size_t(std::clamp(s.split_layout, 0, 1)),
                                "The two players' views in a 2-player race. Top / bottom is the game's own; side by side puts "
                                "them next to each other. Each view keeps its shape and its own HUD. Drawn only."));
        // hudmods (src/b3replay/hudmods.h; remaster-features §29): the race HUD's additions, drawn only, off by default
        t.rows.push_back(section(43, "RACE HUD"));
        t.rows.push_back(toggle(kRowRpmMeter, "RPM METER", s.rpm_meter,
                                "On: a thin bar of the engine revs under the speedometer, red near the rev limit. For the "
                                "automatic and the manual gearbox alike. Drawn only."));
        t.rows.push_back(toggle(kRowBoostShake, "BOOST BAR SHAKE", s.boost_shake,
                                "On: the boost bar shakes while you boost, as in the game's 2004 preview build. Drawn only."));
        t.rows.push_back(section(32, "CAMERA"));
        {
            const std::size_t levels = std::size_t((kFovMax - kFovMin) / kFovStep + 1);
            const int shown = s.fov > 0 ? s.fov : kGameFov;
            t.rows.push_back(slider(kRowFov, "FIELD OF VIEW", levels, fov_level(s.fov), shown == kGameFov ? "90 (GAME)" : std::to_string(shown),
                                    "How much of the road the race view shows, at full boost. The game's camera is unchanged."));
        }
        t.rows.push_back(toggle(kRowFovCorrection, "FOV CORRECTION", s.fov_correction,
                                "With a changed field of view, moves the view so the car keeps its size."));
        // 1.1: the chase camera further back / higher (drawn only; View::apply_fov)
        t.rows.push_back(choice(kRowCameraDistance, "CAMERA DISTANCE", {"-10%", "Game", "+10%", "+20%", "+30%", "+40%", "+50%"},
                                std::size_t(std::clamp(s.camera_distance, 90, 150) / 10 - 9),
                                "How far behind the car the chase camera sits. Game: the original camera. Drawn only."));
        t.rows.push_back(choice(kRowCameraHeight, "CAMERA HEIGHT", {"Game", "+0.25 m", "+0.5 m", "+0.75 m", "+1 m"},
                                std::size_t(std::clamp(s.camera_height, 0, 100) / 25),
                                "Raises the chase camera above the car; it tilts down to keep the car in view. Game: the original camera. Drawn only."));
        // orbitcam1 (src/b3replay/orbitcam.h): drawn only
        t.rows.push_back(toggle(kRowOrbitCamera, "ORBIT CAMERA", s.orbit_camera,
                                "On: in a race, the right stick turns the chase camera around your car to look left, right or behind; "
                                "it stops short of walls. A moment after you let go, the view eases back behind the car. "
                                "The mouse does it too, with the right button held. "
                                "On the PlayStation controller layout the right stick is the throttle and brake, so it stays the game's there. "
                                "The game is unchanged."));
        {   // orbitcam2: LOOK (the stick points the view, as in Burnout Revenge) or SWING (orbitcam1's: the stick turns it)
            Row r = choice(kRowOrbitStyle, "ORBIT STYLE", {"Look", "Swing"}, std::size_t(std::clamp(s.orbit_style, 0, 1)),
                           "Look: the view turns to where you point the stick (down looks behind), as in Burnout Revenge. "
                           "Swing: the stick turns the view round while you hold it, and up or down raises or lowers it. Drawn only.");
            r.disabled = !s.orbit_camera;
            t.rows.push_back(std::move(r));
        }
        // camroll1 (src/b3replay/orbitcam.h ChaseRoll): drawn only
        t.rows.push_back(choice(kRowCameraRoll, "CAMERA ROLL", {"Off", "Subtle", "Strong"}, std::size_t(std::clamp(s.camera_roll, 0, 2)),
                                "In a race, the chase camera leans into corners. "
                                "Subtle: up to 4 degrees. Strong: up to 8 degrees. "
                                "The game is unchanged."));
        // finishcam1 (src/b3replay/finishcam.h): drawn only
        t.rows.push_back(choice(kRowFinishCamera, "FINISH CAMERA", {"Game", "Set pieces"}, std::size_t(std::clamp(s.finish_camera, 0, 1)),
                                "After you cross the finish line, the camera cycles through 12 set pieces of angles around the finish, "
                                "5 seconds each, from the game's own data. "
                                "Game: the game's finish shot. "
                                "The game is unchanged."));
        // options3 (src/b3replay/crashhold.h: drawn only)
        t.rows.push_back(toggle(kRowCrashCamera, "CRASH CAMERA", s.crash_camera,
                                "On: when you crash, the game cuts to its crash camera, as the original. Off: the view stays behind "
                                "your car. The takedown camera is the game's own option (DRIVER DETAILS > SETTINGS). Camera only."));
        // replaycams (src/b3replay/replaycams.h: drawn only)
        t.rows.push_back(choice(kRowReplayCameraCuts, "REPLAY CAMERA CUTS", {"Game", "Stay"}, s.replay_camera_stay ? 1 : 0,
                                "Game: in a race replay, a crash cuts to the game's crash camera, whatever camera you chose. "
                                "Stay: the camera you chose stays on. Camera only."));
        // the frame pacing rows together, last: most players leave them on Auto
        t.rows.push_back(section(3, "SMOOTHNESS"));
        t.rows.push_back(toggle(kRowInterp, "FRAME INTERPOLATION", s.interpolation,
                                "Smooth frames between the game's own. Off shows its 60 / 30 steps as on the PS2."));
        t.rows.push_back(choice(kRowLatency, "LATENCY MODE", {"Auto", "Low", "Smooth", "Newest"}, std::size_t(std::clamp(s.latency, 0, 3)),
                                std::string("Low: least input delay. Smooth: steadiest motion. Newest: no blending.") + kRestart));
        t.rows.push_back(choice(kRowLead, "PREDICTION", {"Auto", "0%", "25%", "50%", "75%", "100%"}, s.lead < 0 ? 0 : std::size_t(std::clamp(s.lead, 0, 100) / 25 + 1),
                                std::string("How far Low mode looks past the newest game tick.") + kRestart));
        t.rows.push_back(choice(kRowFlight, "FRAMES IN FLIGHT", {"Auto", "1", "2", "3"}, s.frames_in_flight < 1 ? 0 : std::size_t(std::min(s.frames_in_flight, 3)),
                                std::string("Frames the GPU may queue: 1 is the most direct, more can be smoother. "
                                            "Auto: 1, or 2 while the graphics card holds the frame rate below its target (with LATENCY MODE Auto).") + kRestart));
        c.tabs.push_back(std::move(t));
    }
    {
        Tab t{"GRAPHICS", {}};
        {   // the first row of the tab it changes (and EFFECTS)
            std::vector<std::string> presets;
            for (Preset p : kPresets) presets.push_back(preset_name(p));
            t.rows.push_back(choice(kRowPreset, "QUALITY PRESET", presets, index_of(kPresets, matching_preset(s)),
                                    "Sets every quality and effects row at once. HIGH is the default."));
        }
        std::vector<std::string> msaa;
        for (int m : kMsaa) msaa.push_back(m == 1 ? "Off" : "MSAA " + std::to_string(m) + "x");
        t.rows.push_back(section(10, "IMAGE"));
        t.rows.push_back(choice(kRowMsaa, "ANTI-ALIASING", msaa, index_of(kMsaa, s.msaa), std::string("Smooths jagged edges.") + kRestart));
        t.rows.push_back(choice(kRowFilter, "TEXTURE FILTERING",
                                {"Point", "Bilinear (PS2)", "Trilinear", "Anisotropic 2x", "Anisotropic 4x", "Anisotropic 8x", "Anisotropic 16x"},
                                std::size_t(s.filter), std::string("Sharpness of textures at an angle.") + kRestart));
        {   // effects2: the 3D picture at a share of the window (a weak GPU / handheld); the HUD and menus stay sharp
            std::vector<std::string> scales;
            for (int pct : kRenderScales) scales.push_back(std::to_string(pct) + "%");
            t.rows.push_back(choice(kRowRenderScale, "RENDER SCALE", scales, render_scale_index(s.render_scale),
                                    "How large the 3D picture is drawn, as a share of the window, then scaled up. Lower is faster on a weak "
                                    "graphics chip; the HUD and menus stay sharp. 100% is the normal picture. "
                                    "Above 100%, it is drawn larger and scaled down: smoother edges and finer detail, for a large cost. "
                                    "150% and 200% want a graphics card with plenty of memory."));
            t.rows.push_back(choice(kRowUpscaling, "UPSCALING", {"Bilinear", "Sharp"}, std::size_t(std::clamp(s.upscaling, 0, 1)),
                                    "How a RENDER SCALE below 100% is brought up to the window. Sharp: a sharper picture with "
                                    "clean edges, for a small cost. Bilinear: softer, the cheapest."));
        }
        t.rows.push_back(toggle(kRowDither, "DITHER", s.dither, "A fine pattern that hides the steps in smooth gradients such as the sky."));
        if (env.graphics_api)   // d3d12 (Windows)
            t.rows.push_back(choice(kRowGraphicsApi, "GRAPHICS API", {"Auto", "Vulkan", "D3D12"}, std::size_t(std::clamp(s.graphics_api, 0, 2)),
                                    std::string("The graphics interface the game draws with. Auto uses Vulkan, and Direct3D 12 when no working "
                                                "Vulkan device can be created. Try D3D12 if the picture shows faults with your graphics card.") + kRestart));
        t.rows.push_back(section(33, "TEXTURES"));
        if (env.hd_pack || env.xbox_textures)
            t.rows.push_back(toggle(kRowHd, "HD TEXTURES", s.hd_textures, "The HD texture pack given at start (applies when the game next starts). "
                                    "F4 switches the HD textures on and off at once while you play, to compare."));   // hdkey
        if (env.xbox_textures)   // xboximport; settingsui: beside the HD switch and the pack
            t.rows.push_back(toggle(kRowXboxTextures, "XBOX TEXTURES", s.xbox_textures,
                                    "The textures imported from your Xbox disc in the launcher, below your own HD pack and the mods' packs. "
                                    "It is a small improvement: some building fronts, signs and car paint are somewhat sharper. "
                                    "Used while HD TEXTURES is on. Applies at the next start."));
        if (env.hd_pack || env.launcher)   // texpack: how a pack's PNG / BC7 files are kept (with a pack, and in the launcher)
            t.rows.push_back(choice(kRowHdMemory, "HD TEXTURE MEMORY", {"Auto", "Full quality", "Compressed"},
                                    std::size_t(std::clamp(s.hd_texture_memory, 0, 2)),
                                    "How an HD texture pack's PNG and BC7 files are kept on the graphics card. "
                                    "Full quality: as the files are, as PCSX2 keeps them; they take about four times the memory. "
                                    "Compressed: packed when they load, a quarter of the memory, a little less sharp. "
                                    "Auto: full quality on a graphics card with 6 GB of its own memory or more, compressed otherwise. "
                                    "Applies at the next start."));
        if (env.launcher) {   // the launcher's HD Textures page chooses, downloads and removes the pack
            Row page = note(kActionOpenHdPage, "HD TEXTURE PACK", "HD Textures",
                            "The launcher's HD Textures page: choose the pack's folder, get the pack, or stop using it.");
            page.disabled = false;
            t.rows.push_back(page);
        } else {   // mac5: the HD pack's folder (remaster-features §8)
            std::string name = "None";
            if (!s.hd_pack_dir.empty()) {
                std::string d = s.hd_pack_dir;
                while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
                const std::size_t cut = d.find_last_of("/\\");
                name = cut == std::string::npos ? d : d.substr(cut + 1);
            }
            std::string desc = "A PCSX2 texture replacement folder for this game (its SLUS-21050 folder, or the folder above it). Applies at the next start.";
            if (!s.hd_pack_dir.empty()) desc += std::string(" Chosen: ") + s.hd_pack_dir + (env.hd_pack_loaded ? " (in use)." : " (not in use yet).");
            if (!env.hd_pack_status.empty()) desc += " " + env.hd_pack_status;
            Row pick = note(kActionChooseHdPack, "HD TEXTURE PACK", name, desc);
            pick.disabled = false;
            t.rows.push_back(pick);
            if (!s.hd_pack_dir.empty()) {
                Row none = note(kActionClearHdPack, "REMOVE HD PACK", "Remove", "Stop using the chosen folder from the next start (the files stay).");
                none.disabled = false;
                t.rows.push_back(none);
            }
        }
        t.rows.push_back(section(11, "WORLD"));
        t.rows.push_back(choice(kRowWholeTrack, "DRAW DISTANCE", {"PS2", "Whole track"}, s.whole_track ? 1 : 0,
                                "Whole track: every part of the course drawn, no pop-in in the distance. PS2: the original's streamed set."));
        t.rows.push_back(toggle(kRowShadows, "CAR SHADOWS", s.shadows, "The shadows under the cars."));
        t.rows.push_back(toggle(kRowSkids, "SKID MARKS", s.skid_marks, "Tyre marks on the road."));
        t.rows.push_back(toggle(kRowSpecular, "ROAD SHINE", s.specular, "The road's specular highlight pass."));
        t.rows.push_back(toggle(kRowSun, "SUN AND LENS FLARE", s.sun, "The sun's glow and its lens flare."));
        t.rows.push_back(toggle(kRowCoronas, "CAR AND TRAFFIC LIGHTS", s.coronas, "The glow of head, tail and traffic lights and the boost flame."));
        // night (remaster-features §17 / §18): GRAPHICS' last section, after WORLD
        t.rows.push_back(section(30, "LIGHTING"));
        // TIME OF DAY and MOON (rows 370-371): night mode is held back in 1.3.0, still being finished (street lamps,
        // billboard lights, covered roads); it returns in an update
        t.rows.push_back(toggle(kRowSurfaceDetail, "SURFACE DETAIL", s.surface_detail,
                                "Relief and sheen on the road, kerbs and walls from their textures, lit by the sun or the moon. Drawn only."));
        if (s.extra_night)   // extras: EXPERIMENTAL NIGHT on: the moon of its night races
            t.rows.push_back(toggle(kRowMoon, "MOON", s.moon,
                                    "The moon in the night sky, and its cool light on the cars, the road and the walls facing it. "
                                    "It lights the road's relief too, a small extra cost even with SURFACE DETAIL off."));
        // AMBIENT OCCLUSION (row 373) is held back in 1.3.0: it draws noise lines over the picture; it returns fixed in an update
        t.rows.push_back(toggle(kRowSunShadows, "SUN SHADOWS", s.sun_shadows,
                                "Real shadows of the cars on the road, cast by the sun or, at night, the moon. "
                                "None in tunnels and under bridges. Sharper with ANTI-ALIASING at 4x or more. Drawn only."));
        t.rows.push_back(toggle(kRowPropShadows, "PROP SHADOWS", s.prop_shadows,
                                "With SUN SHADOWS: the lamp posts, signs, barriers and other roadside props cast shadows too. Drawn only."));
        // xboxglow: Xbox (remaster-features §35.5; its Xbox all lamps value was withdrawn)
        t.rows.push_back(choice(kRowTunnelLights, "TUNNEL LIGHTS", {"Game", "Glow", "Xbox"},
                                std::size_t(std::clamp(s.tunnel_lights, 0, 2)),
                                "Glow: the light fittings in tunnels and underpasses glow, with small halos. "
                                "Xbox: their lit parts glow through the bloom, as on the Xbox. Game: the original look. Drawn only."));
        // WEATHER (row 650: Dry / Rain, "Rain: falling rain, wet roads that mirror the lights, spray from the wheels and a
        // grey sky. Dry: the game's own weather. Drawn only.") is held back with night mode until it is released
        c.tabs.push_back(std::move(t));
    }
    {
        Tab t{"EFFECTS", {}};
        t.rows.push_back(section(20, "POST"));
        t.rows.push_back(toggle(kRowBlur, "MOTION BLUR", s.motion_blur, "The game's speed blur."));
        t.rows.push_back(slider(kRowRadial, "BOOST BLUR", 11, std::size_t(std::clamp(s.radial_blur, 0, 100) / 10),
                                std::to_string(std::clamp(s.radial_blur, 0, 100) / 10 * 10) + "%", "The radial blur while boosting. 50% is the default."));
        t.rows.push_back(choice(kRowBloomLevel, "BLOOM", {"Off", "Soft", "Standard"}, s.bloom ? std::size_t(std::clamp(s.bloom_level, 1, 2)) : 0,
                                "The glow around bright lights. Soft: a gentle glow that keeps the detail of signs and sky. Standard: the game's own, strong glow."));
        t.rows.push_back(choice(kRowPost, "POST QUALITY", {"PS2", "PC"}, s.post_pc ? 1 : 0,
                                "PC draws blur and bloom at the window's resolution; PS2 at the console's buffer sizes."));
        t.rows.push_back(section(21, "CRASHES"));
        // effects2: Off / Low / Medium / High; High is the game's whole set, Low and Medium draw a share of the particles
        const std::vector<std::string> levels = {"Off", "Low", "Medium", "High"};
        t.rows.push_back(choice(kRowSparks, "SPARKS", levels, std::size_t(std::clamp(s.sparks, 0, 3)),
                                "Sparks from scrapes and crashes. High: all of them. Medium: half. Low: a quarter, still bright. A weaker PC "
                                "draws crashes faster with less. Drawn only: the crash is the same."));
        t.rows.push_back(toggle(kRowScrapes, "SCRAPES", s.scrapes, "The scrape streaks along walls and cars (drawn only)."));
        t.rows.push_back(choice(kRowPopcorn, "CRASH DEBRIS", levels, std::size_t(std::clamp(s.popcorn, 0, 3)),
                                "The small bits of a crash. High: all of them. Medium: half. Low: a quarter. Drawn only: the crash is the same."));
        t.rows.push_back(toggle(kRowDamage, "DAMAGE", s.damage, "Bent bodywork. Off draws undamaged cars; the crash itself is unchanged."));
        t.rows.push_back(toggle(kRowGlass, "GLASS DAMAGE", s.glass_damage, "Cracked and broken glass. Off draws intact glass."));
        c.tabs.push_back(std::move(t));
    }
    {
        Tab t{"CAR", {}};
        // carpaint (remaster-features §15): player 1's car, drawn only
        t.rows.push_back(section(19, "CAR PAINT"));
        t.rows.push_back(choice(kRowCarPaint, "CAR PAINT", {"Game", "Custom"}, s.car_paint == 1 ? 1 : 0,
                                "Custom: your car in your own colour (the three rows below) instead of the paint picked in the car select. "
                                "Cars with one fixed livery keep it. Drawn only: the game, replays and saves keep the car select's paint. "
                                "Changing the colour on SELECT CAR sets it back to Game."));   // paintsel
        // padpaint: the colour rows with a swatch of the colour they make; player 2's the same below. settingsui: a row of
        // ready colours above them (it sets the three and turns the custom paint on)
        const auto colour_rows = [&t](std::uint32_t preset_id, const char* preset_label, const std::uint32_t ids[3], const char* const names[3],
                                      const int vals[3], bool custom) {
            std::int32_t rgb = 0;
            for (int k = 0; k < 3; ++k) rgb = rgb << 8 | std::clamp(vals[k], 0, 255);
            Row p = choice(preset_id, preset_label, paint_preset_names(vals), paint_preset_index(vals),
                           "A ready colour: sets the three rows below and turns the custom paint on. Custom: the colour of the rows below.");
            p.swatch = rgb;
            t.rows.push_back(p);
            for (int k = 0; k < 3; ++k) {
                const int v = std::clamp(vals[k], 0, 255);
                Row r = slider(ids[k], names[k], kPaintLevels, std::size_t((v + kPaintStep / 2) / kPaintStep), std::to_string(v),
                               "The custom paint's colour, 0 to 255 for each of red, green and blue.");
                r.disabled = !custom;
                r.swatch = rgb;
                t.rows.push_back(r);
            }
        };
        {
            const char* const names[3] = {"PAINT RED", "PAINT GREEN", "PAINT BLUE"};
            const std::uint32_t ids[3] = {kRowPaintRed, kRowPaintGreen, kRowPaintBlue};
            const int vals[3] = {s.paint_red, s.paint_green, s.paint_blue};
            colour_rows(kRowPaintPreset, "COLOUR", ids, names, vals, s.car_paint == 1);
        }
        t.rows.push_back(choice(kRowPaintFinish, "FINISH", {"Gloss", "Metallic", "Pearl", "Chrome"}, std::size_t(std::clamp(s.paint_finish, 0, 3)),
                                "The paint's finish on both players' cars. Gloss: the game's. Metallic: a stronger reflection in the "
                                "paint's colour, with a sparkle of flakes in the light. Pearl: the colour shifts as the car turns to the "
                                "light. Chrome: a mirror finish that shows the world around the car, tinted by the paint's colour. "
                                "Drawn only."));
        t.rows.push_back(toggle(kRowDecals, "DECALS", s.car_decals,
                                "Off: your car without its logos, numbers and stripes, in plain paint; lights, grilles and wheels stay. Drawn only."));
        // paint2: the other layers' colours (any colour: the paint panel's COLOUR PICKER on SELECT CAR)
        const auto layer_rows = [&](int player) {
            static const char* const labels[2][3] = {{"DECAL COLOUR", "WHEEL COLOUR", "BOOST GLOW COLOUR"},
                                                      {"P2 DECAL COLOUR", "P2 WHEEL COLOUR", "P2 BOOST GLOW COLOUR"}};
            static const char* const desc[3] = {
                "The colour of the logos, numbers and stripes. Game: their own colours. Any colour: the paint panel on SELECT CAR. Drawn only.",
                "The colour of the wheels' rims (the tyres stay dark). Game: their own. Any colour: the paint panel on SELECT CAR. Drawn only.",
                "The colour of the exhaust's glow while boosting. Game: the game's. Any colour: the paint panel on SELECT CAR. Drawn only."};
            for (int l = 1; l < kPaintLayers; ++l)
                t.rows.push_back(layer_colour_row(s, player, l, kRowLayerColour + 4u * std::uint32_t(player) + std::uint32_t(l),
                                                  labels[player][l - 1], desc[l - 1]));
        };
        layer_rows(0);
        t.rows.push_back(section(23, "PLAYER 2 CAR PAINT"));
        t.rows.push_back(choice(kRowP2Paint, "P2 CAR PAINT", {"Game", "Custom"}, s.p2_car_paint == 1 ? 1 : 0,
                                "Player 2's car in the split screen: Custom draws it in the colour of the three rows below instead "
                                "of the paint picked in the car select. Drawn only. Changing the colour on SELECT CAR sets it back to Game."));   // paintsel
        {
            const char* const names[3] = {"P2 PAINT RED", "P2 PAINT GREEN", "P2 PAINT BLUE"};
            const std::uint32_t ids[3] = {kRowP2Red, kRowP2Green, kRowP2Blue};
            const int vals[3] = {s.p2_paint_red, s.p2_paint_green, s.p2_paint_blue};
            colour_rows(kRowP2PaintPreset, "P2 COLOUR", ids, names, vals, s.p2_car_paint == 1);
        }
        t.rows.push_back(toggle(kRowP2Decals, "P2 DECALS", s.p2_car_decals,
                                "Off: player 2's car without its logos, numbers and stripes, in plain paint. Drawn only."));
        layer_rows(1);   // paint2
        t.rows.push_back(section(34, "CAR LOOK"));
        t.rows.push_back(choice(kRowCarDetail, "CAR DETAIL", {"Game", "Highest"}, s.car_detail_highest ? 1 : 0,
                                "Highest draws every car at full detail at any distance."));
        t.rows.push_back(choice(kRowCarReflect, "CAR REFLECTIONS", {"Off", "Original", "Enhanced"}, std::size_t(std::clamp(s.car_reflections, 0, 2)),
                                "Enhanced: glossy sky and sun reflections on the paint and glass. Original: the game's own. Off: none."));
        // reflect2 (docs/car-shading.md §10): the env map's track part, drawn only
        t.rows.push_back(choice(kRowReflectDistance, "REFLECTION DISTANCE", {"Game", "200 m", "300 m"}, std::size_t(std::clamp(s.reflect_distance, 0, 2)),
                                "How far the cars' reflections reach. Game: 100 m, as the original. "
                                "Further shows more of the track in the paint and costs more."));
        t.rows.push_back(choice(kRowReflectDetail, "REFLECTION DETAIL", {"Track", "Track and props", "Track, props and cars"},
                                std::size_t(std::clamp(s.reflect_detail, 0, 2)),
                                "Track and props: the barriers, signs, cones and lamp posts near the car show in its reflections too. "
                                "Track, props and cars: the other cars nearby as well."));
        t.rows.push_back(choice(kRowReflectUpdate, "REFLECTION UPDATE", {"Every frame", "Every 2nd frame"}, std::size_t(std::clamp(s.reflect_update, 0, 1)),
                                "Every 2nd frame: the reflections are redrawn half as often, for slower graphics cards."));
        t.rows.push_back(choice(kRowReflectResolution, "REFLECTION RESOLUTION", {"Standard", "High"}, std::size_t(std::clamp(s.reflect_resolution, 0, 1)),
                                "High: sharper reflections, drawn at twice the size. Standard: as the original."));
        // blinkers1 (tools/native/turn_signals.h): the racers' lights only
        t.rows.push_back(choice(kRowTurnSignals, "TURN SIGNALS", {"Off", "On"}, std::size_t(std::clamp(s.turn_signals, 0, 1)),
                                "On: the other racers' indicators blink before a bend, on every track. "
                                "Their lights only: the race is unchanged."));
        t.rows.push_back(section(35, "GARAGE"));
        // garagecam1 (src/b3replay/garagecam.h): drawn only
        t.rows.push_back(toggle(kRowGarageCamera, "GARAGE CAMERA", s.garage_camera,
                                "On: on the car pages (car select, your garage), the right stick or a mouse drag on the car "
                                "turns the view around the car; a second after you let go, the car spins on its own again. "
                                "The game is unchanged."));
        // carsel1 (src/native/input_keys.h ShoulderSeries): on by default, input-changing
        t.rows.push_back(choice(kRowSeriesShoulders, "CAR SERIES ON L1 / R1", {"Off", "On"}, std::size_t(std::clamp(s.series_shoulders, 0, 1)),
                                "On: in the car select, the shoulder buttons (L1 / R1, LB / RB, or their keys) change the car "
                                "series as Left / Right do, and show beside the series arrows. On by default. Changes the input "
                                "the game gets; Off is the original."));
        // carstats1 (tools/native/car_stats.h): the car select page only
        t.rows.push_back(choice(kRowCarStats, "CAR STATS", {"Game", "More"}, std::size_t(std::clamp(s.car_stats, 0, 1)),
                                "More: the car select page also shows GRIP and ACCELERATION, worked out from each car's handling. "
                                "Game: TOP SPEED and WEIGHT only."));
        c.tabs.push_back(std::move(t));
    }
    {
        Tab t{"CONTROLS", {}};
        // rumble1 (controller rumble, src/native/pad_actuator.h)
        t.rows.push_back(section(16, "FEEDBACK"));
        t.rows.push_back(choice(kRowRumble, "RUMBLE", {"Off", "On"}, std::size_t(std::clamp(s.rumble, 0, 1)),
                                "On: the game's own rumble on your controllers (player 1's and player 2's), as on the PS2's "
                                "DualShock 2. The game's VIBRATION option (Driver Details) still applies. Off is the original port."));
        {
            const int pct = std::clamp(s.rumble_strength, 10, 100) / 10 * 10;
            t.rows.push_back(slider(kRowRumbleStrength, "RUMBLE STRENGTH", 10, std::size_t(pct / 10 - 1), std::to_string(pct) + "%",
                                    "How strongly the controllers rumble. 100% is the full motor speed the game asks for."));
        }
        t.rows.push_back(toggle(kRowRumbleTriggers, "TRIGGER RUMBLE", s.rumble_triggers,
#ifdef _WIN32   // issue #16: on Windows it also picks SDL's controller path at the start (b3port/trigger_rumble.h)
                                std::string("Also the triggers' own motors, on controllers that have them (Xbox One and Series).") + kRestart));
#else
                                "Also the triggers' own motors, on controllers that have them (Xbox One and Series)."));
#endif
        // pad1 (src/native/pad_triggers.h): DualSense adaptive triggers, drawn from the game's state (nothing sent to the game)
        t.rows.push_back(choice(kRowAdaptiveTriggers, "ADAPTIVE TRIGGERS", {"Off", "On"}, std::size_t(std::clamp(s.adaptive_triggers, 0, 1)),
                                "On: a DualSense's triggers push back while you drive: the brake firmly, the accelerator lightly, "
                                "buzzing while you boost, with a kick when you crash. Free in menus and when paused. The game is unchanged."));
        {
            const int pct = std::clamp(s.adaptive_triggers_strength, 10, 100) / 10 * 10;
            t.rows.push_back(slider(kRowAdaptiveStrength, "TRIGGER STRENGTH", 10, std::size_t(pct / 10 - 1), std::to_string(pct) + "%",
                                    "How strongly the adaptive triggers push back and buzz. 100% is the full effect."));
        }
        // how the inputs behave while driving (each changes the input the game gets; off / Hold is the original)
        t.rows.push_back(section(36, "DRIVING"));
        t.rows.push_back(toggle(kRowAnalogPedals, "ANALOG TRIGGERS", s.analog_pedals,
                                "On: a trigger set to accelerate or brake works gradually, like a pedal (light pressure, light "
                                "throttle). Both triggers together still powerslide. Off: a trigger acts as a button."));
        t.rows.push_back(choice(kRowBoostMode, "BOOST", {"Hold", "Toggle"}, std::size_t(std::clamp(s.boost_mode, 0, 1)),
                                "Toggle: press boost once to keep boosting, again to stop (also stops when you pause or the "
                                "race ends). Changes the input the game gets; Hold is the original."));
        t.rows.push_back(choice(kRowInstantRestart, "INSTANT RESTART", {"Off", "On"}, std::size_t(std::clamp(s.instant_restart, 0, 1)),
                                "On: F5, or SELECT + START on a controller, restarts the event while you drive, through the "
                                "game's own pause menu (RESTART, YES). The game gets those menu presses; Off is the original."));
        if (env.controls.on) {   // remap1 (src/native/input_bindings.h): the host's bindings editor (native_play's page)
            const Env::Controls& k = env.controls;
            t.rows.push_back(section(300, "BINDINGS"));
            t.rows.push_back(choice(kRowCtlPlayer, "PLAYER", {"Player 1", "Player 2", "Shortcuts"}, k.player,
                                    "Whose controls the rows below show. The keyboard is shared: player 2's keys (none at "
                                    "first) plug in a second controller for them. Shortcuts: the window's own keys (restart, "
                                    "fullscreen, screenshot, bug report, the tuning overlay, the settings sections) and photo "
                                    "mode's controls."));
            t.rows.push_back(choice(kRowCtlDevice, "DEVICE", {"Keyboard", "Controller"}, k.device,
                                    "The keyboard's keys, or the buttons, sticks and triggers of this player's controllers."));
            if (k.device == 1 && k.player < 2)   // padlayout (1.2.1)
                t.rows.push_back(choice(kRowCtlLayout, "CONTROLLER LAYOUT", {k.layout_auto, "Xbox", "PlayStation", "Nintendo", "Custom"},
                                        std::min<std::size_t>(k.layout, 4),
                                        "Which buttons do what on this player's controller. Auto: the layout of the controller's "
                                        "console, picked again when the controller changes: a PlayStation controller gets the PS2 "
                                        "game's, any other controller the original Xbox game's. Xbox, PlayStation: that layout on "
                                        "any controller. Nintendo: the Xbox layout by label, A confirms and boosts, B goes back and "
                                        "is the Crashbreaker. Custom: your own bindings in the rows below. Changing a binding below "
                                        "makes it Custom."));
            if (k.device == 0 && k.player < 2)
                t.rows.push_back(choice(kRowCtlView, "KEYS FOR", {"Driving", "Menus"}, k.view,
                                        "A key can do one thing while you drive and another in the menus (W accelerates, and "
                                        "moves up in a menu). The rows below show and change one of the two."));
            if (k.device == 1 && k.player < 2)   // padlayout: the controller's views
                t.rows.push_back(choice(kRowCtlView, "BUTTONS FOR", {"Driving", "Menus"}, k.view,
                                        "A button can do one thing while you drive and another in the menus (on the Xbox layout "
                                        "A boosts, and confirms in a menu). The rows below show and change one of the two."));
            std::uint32_t next = kActionCtlFirst;   // the rows without an id of their own, in order
            for (std::size_t a = 0; a < k.actions.size(); ++a) {
                const std::uint32_t id = k.actions[a].id ? k.actions[a].id : next++;
                if (!k.actions[a].id && id > kActionCtlLast) continue;
                Row r = note(id, k.actions[a].label, k.actions[a].value, k.actions[a].description);
                r.disabled = false;
                t.rows.push_back(r);
            }
            Row reset = note(kActionCtlReset, "RESET TO DEFAULTS", "Reset",
                             k.player < 2 ? "This player's keyboard or controller back to the original layout (the other device stays). "
                                            "To put one control back: SQUARE on it (the key bound to SQUARE on a keyboard)."
                                          : "The shortcuts' keys or buttons (this device) back to the original ones. To put one back: "
                                            "SQUARE on it (the key bound to SQUARE on a keyboard).");
            reset.disabled = false;
            t.rows.push_back(reset);
            Row reset_all = note(kActionCtlResetAll, "RESET ALL CONTROLS", "Reset all",
                                 "Everything back to the original layout: both players' keyboards and controllers, the gyro "
                                 "settings and the shortcuts.");
            reset_all.disabled = false;
            t.rows.push_back(reset_all);
            if (!k.status.empty()) t.rows.push_back(note(kRowCtlStatus, "LAST CHANGE", "Saved", k.status));
            if (k.gyro && k.player < 2) {   // pad1: gyro steering (opt-in, input-changing; per player, saved with the controls)
                t.rows.push_back(section(37, "GYRO"));
                t.rows.push_back(choice(kRowCtlGyroMode, "GYRO STEERING", {"Off", "Mix", "Replace"}, std::min<std::size_t>(k.gyro_mode, 2),
                                        "Steer by moving a controller that has a gyro (DualSense, DualShock 4, Switch Pro, Steam "
                                        "Deck). Mix: with the stick; Replace: instead of the stick's steering. While driving only. "
                                        "Changes the input the game gets; Off is the original."));
                t.rows.push_back(choice(kRowCtlGyroAxis, "GYRO AXIS", {"Tilt", "Turn"}, std::min<std::size_t>(k.gyro_axis, 1),
                                        "Tilt: lean the controller like a steering wheel (level is straight). Turn: turn it left "
                                        "and right (straight where you start driving)."));
                std::vector<std::string> sens;
                for (int v = 25; v <= 300; v += 25) sens.push_back(std::to_string(v) + "%");
                t.rows.push_back(choice(kRowCtlGyroSensitivity, "GYRO SENSITIVITY", sens, std::min<std::size_t>(k.gyro_sensitivity, sens.size() - 1),
                                        "How far to move for full lock: 100% is 40 degrees of tilt (30 of turn); 200% half that."));
                std::vector<std::string> dz;
                for (int v = 0; v <= 10; ++v) dz.push_back(std::to_string(v) + " DEG");
                t.rows.push_back(choice(kRowCtlGyroDeadZone, "GYRO DEAD ZONE", dz, std::min<std::size_t>(k.gyro_dead_zone, 10),
                                        "How far the controller moves from straight before it steers."));
                if (!k.gyro_recentre_labels.empty())
                    t.rows.push_back(choice(kRowCtlGyroRecentre, "GYRO RECENTRE", k.gyro_recentre_labels,
                                            std::min(k.gyro_recentre, k.gyro_recentre_labels.size() - 1),
                                            "The button that makes the controller's present position straight. It still does what "
                                            "it is bound to as well."));
            }
        } else if (env.launcher) {   // the launcher's Controllers page edits the same bindings
            t.rows.push_back(section(300, "BINDINGS"));
            Row page = note(kActionOpenControllers, "BUTTONS AND KEYS", "Controllers",
                            "The launcher's Controllers page: test your controllers, and change the buttons and keys of both "
                            "players, the shortcuts and the gyro steering.");
            page.disabled = false;
            t.rows.push_back(page);
        }
        c.tabs.push_back(std::move(t));
    }
    if (kAudioTab) {
        Tab t{"AUDIO", {}};
        audio_rows(t, s, env);
        c.tabs.push_back(std::move(t));
    }
    {
        Tab t{"GAME", {}};
        t.rows.push_back(section(38, "INTERFACE"));
        t.rows.push_back(choice(kRowGlyphs, "BUTTON PROMPTS", {"Auto", "PlayStation", "PS4 / PS5", "Xbox", "Nintendo", "Generic", "Keyboard"},
                                index_of(kPromptOrder, std::clamp(s.button_prompts, 0, 6)),
                                "The buttons the prompts show. Auto: the controller or keyboard you used last. PS4 / PS5: OPTIONS for START."));
        t.rows.push_back(toggle(kRowSkipHint, "VIDEO SKIP HINT", s.skip_hint,
                                "A corner hint with the button that skips the video playing. Every video plays unless you skip it."));
        // photorow1 (tools/native/pc_menu.h): the pause menu's PHOTO MODE row
        t.rows.push_back(toggle(kRowPhotoRow, "PHOTO MODE IN PAUSE MENU", s.photo_row,
                                "On: the race's pause menu has a PHOTO MODE row after SETTINGS, which opens photo mode (the key "
                                "P, or L3 + R3 on a controller, does too). Off: the pause menu is the game's own. From the next pause."));
        // settingsback (tools/native/settings_back.h): Back on the game's own SETTINGS pages
        t.rows.push_back(choice(kRowSettingsBack, "SETTINGS BACK BUTTON", {"Keep changes", "Cancel"}, std::size_t(s.settings_back_keeps ? 0 : 1),
                                "Leaving a page of DRIVER DETAILS > SETTINGS (GAMEPLAY, AUDIO, VIDEO, CONTROLLERS) with Back. "
                                "Keep changes: what you changed there stays. Cancel: Back puts the old values back, as on the PS2 "
                                "(confirm keeps them)."));
        t.rows.push_back(choice(kRowFps, "FPS COUNTER", {"Off", "Minimal", "Standard", "Detailed"}, std::size_t(std::clamp(s.fps_view, 0, 3)),
                                "A frame-rate readout in a corner. Minimal: a small plate with the number. Standard: also the "
                                "1% low. Detailed: a big plate with a frame-time graph, the average, the latency and the render settings. F3 cycles the views."));
        t.rows.push_back(choice(kRowFpsCorner, "FPS COUNTER POSITION", {"Top left", "Top right", "Bottom left", "Bottom right"},
                                std::size_t(std::clamp(s.fps_corner, 0, 3)), "The corner the FPS counter sits in."));
        // options3 (src/b3replay/driverlabels.h)
        t.rows.push_back(choice(kRowDriverLabels, "DRIVER LABELS", {"Off", "Names", "Names + distance", "Distance"},
                                std::size_t(std::clamp(s.driver_labels, 0, 3)),
                                "Over the rival cars in a race: each driver's name, the distance to them, or both. Drawn only."));
        t.rows.push_back(section(39, "LANGUAGE AND UNITS"));
        {   // 1.0.1 (b3asset/language.h): the game's text in an imported language; English is the game's own
            std::vector<std::string> names{"English"};
            std::size_t at = 0;
            for (std::size_t k = 0; k < env.languages.size(); ++k) {
                names.push_back(env.languages[k].second);
                if (env.languages[k].first == s.language) at = k + 1;
            }
            // whole sentences, so that translate() finds each one (i18n.h)
            std::string desc = "The language of the game's own text and of these settings: menus, messages, hints and event names. ";
            desc += env.language_import ? "Other languages come from your own European copy of the game: IMPORT LANGUAGES copies them from its disc image once."
                                        : "Other languages come from your own European copy of the game (the launcher's IMPORT LANGUAGES).";
            desc += " Applies at the next start.";
            if (!s.language.empty() && at == 0) desc += " The chosen language (" + s.language + ") is not imported: English is used.";
            t.rows.push_back(choice(kRowLanguage, "LANGUAGE", names, at, desc));
            if (env.language_import) {
                std::string idesc = "Choose a European disc image (or its folder) of Burnout 3: Takedown: its languages are copied "
                                    "to this PC (two small files each); the disc is not needed afterwards.";
                idesc += " Its DJ voices (about 300 MB a language) and race training videos (about 130 MB a language) can then be imported in the rows below.";
                if (!env.language_status.empty()) idesc += " " + env.language_status;
                Row imp = note(kActionImportLanguages, "IMPORT LANGUAGES", "Choose disc", idesc);
                imp.disabled = false;
                t.rows.push_back(imp);
            }
            // 1.3.0 (b3asset/language.h dj_*): the DJ in the chosen language, or the English one with translated menus
            if (!env.languages.empty() || !s.language.empty())
                t.rows.push_back(choice(kRowDjVoice, "VOICES", {"Language", "English"}, std::size_t(std::clamp(s.voices, 0, 1)),
                                        "The Crash FM DJ and the race training's narrator. Language: they speak the chosen LANGUAGE "
                                        "(French, German, Italian and Spanish have their own; UK English, Dutch and Swedish have the "
                                        "English voices, as on their discs). The training's narration is on your disc already; the "
                                        "DJ's lines need importing (the DJ VOICE rows of the launcher). English: the US voices, with "
                                        "the menus in your language. Applies at the next start."));
            if (env.language_import)
                for (const Env::DjVoice& d : env.dj_voices) {
                    std::uint32_t id = 0;
                    for (const std::string& c : {std::string("FR"), std::string("GE"), std::string("IT"), std::string("SP")}) {
                        if (c == d.code) break;
                        ++id;
                    }
                    if (id > kActionDjLast - kActionDjFirst) continue;
                    const auto mb = [](std::uint64_t b) { return std::to_string((b + (1u << 19)) >> 20); };
                    std::string value, desc;
                    if (d.progress >= 0) {
                        value = "Cancel (" + std::to_string(std::clamp(d.progress, 0, 100)) + " %)";
                        desc = d.training ? "Copying the training videos from the disc. Cancel stops it and keeps nothing."
                                          : "Copying the DJ's lines from the disc. Cancel stops it and keeps nothing.";
                    } else if (d.imported_bytes) {
                        value = "Remove (" + mb(d.imported_bytes) + " MB)";
                        desc = d.training ? "Imported: the race training videos in this language are on this PC. Remove deletes them; "
                                            "the training then shows the English pictures."
                                          : "Imported: the DJ's lines in this language are on this PC. Remove deletes them; the DJ is then English.";
                    } else {
                        value = "Import (" + mb(d.disc_bytes) + " MB)";
                        desc = d.training ? "Copies the race training videos' pictures in this language (their on-screen text) from the "
                                            "chosen disc to this PC, once. Without them the training shows the English pictures with the "
                                            "narration in your language."
                                          : "Copies the DJ's lines in this language from the chosen disc to this PC, once.";
                        desc += " Needs " + mb(d.disc_bytes) + " MB of free space.";
                    }
                    if (!env.dj_status.empty()) desc += " " + env.dj_status;   // the last import's result (not translated)
                    Row r = note((d.training ? kActionTrainingFirst : kActionDjFirst) + id, (d.training ? "TRAINING VIDEOS: " : "DJ VOICE: ") + d.name,
                                 value, desc);
                    r.disabled = false;
                    t.rows.push_back(r);
                }
        }
        // units1 (tools/native/speed_units.h): the game's own words (kph / mph, KM / MILES, M / FT)
        t.rows.push_back(choice(kRowSpeedUnits, "SPEED UNITS", {"MPH", "KM/H"}, std::size_t(std::clamp(s.speed_units, 0, 1)),
                                "Speeds and distances in miles or kilometres: the speedometer, the distance messages, "
                                "records and stats. MPH is the original US game's. The speedometer changes from the next race."));
        t.rows.push_back(choice(kRowDateFormat, "DATE FORMAT", {"Month/Day/Year", "Day/Month/Year"}, std::size_t(std::clamp(s.date_format, 0, 1)),
                                "How the game writes dates, such as a profile's DATE OF SAVE. Month/Day/Year is the original US game's."));
        t.rows.push_back(section(14, "MODES"));
        // ghost1 (Time Attack ghosts, src/b3replay/ghost.h)
        t.rows.push_back(choice(kRowGhost, "GHOST", {"Off", "Best lap"}, std::size_t(std::clamp(s.ghost, 0, 1)),
                                "Best lap: your laps in Time Attack are kept, and the best one on the track races with you as a "
                                "see-through car. Drawn only: the game never knows it is there."));
        // speedrun1 (remaster-features §5 "Speedrun support", src/b3replay/speedrun.h)
        t.rows.push_back(choice(kRowSpeedrunTimer, "SPEEDRUN TIMER", {"Off", "Event", "Event + splits"},
                                std::size_t(std::clamp(s.speedrun_timer, 0, 2)),
                                "The event's time from the start signal to the finish, in game time (the game's own timer: the "
                                "result screen's numbers), with your best per event and car. Event + splits: each lap too. Drawn only."));
        // replay1 (replay files, src/native/replay_file.h)
        t.rows.push_back(toggle(kRowRecordReplays, "RECORD REPLAYS", s.record_replays != 0,
                                "On: each session, from the start of the game to its end, is kept as a replay file that "
                                "plays it again exactly (the launcher's Replays page). The newest 20 are kept. Applies at the next start."));
        // accessibility (remaster-features "Accessibility")
        t.rows.push_back(section(13, "ACCESSIBILITY"));
        t.rows.push_back(choice(kRowFlashes, "SCREEN FLASHES", {"Full", "Reduced"}, std::size_t(std::clamp(s.screen_flashes, 0, 1)),
                                "Reduced tones down full-screen white flashes and the flicker of camera changes. Drawn only."));
        t.rows.push_back(choice(kRowHudColour, "HUD COLOUR FILTER", {"Off", "Deuteranopia", "Protanopia", "Tritanopia"},
                                std::size_t(std::clamp(s.hud_colour_filter, 0, 3)),
                                "Shifts the HUD's and menus' colours so that they stay apart for colour-blind players. "
                                "The race itself is not changed."));
        // textsize1: the race HUD's components scaled about their own anchors (a11y.h); the menus stay at 100 %
        t.rows.push_back(choice(kRowHudSize, "HUD SIZE", {"100%", "115%", "130%", "150%"}, std::size_t(std::clamp(s.hud_size, 0, 3)),
                                "Draws the race HUD larger: position, lap, boost, speed, scores and the race's messages, each "
                                "grown from its corner. Menus keep their size. Drawn only."));
        // BOOST (Hold / Toggle) is with the driving inputs now: a pointer where an accessibility setting is looked for
        t.rows.push_back(note(kRowBoostElsewhere, "BOOST HOLD OR TOGGLE", "Controls",
                              "Boost by holding the button, or press once to boost and again to stop: CONTROLS > DRIVING > BOOST."));
        // behaviour rows that were on DISPLAY, INTERFACE and SAVES
        t.rows.push_back(section(40, "STARTING AND LOADING"));
        t.rows.push_back(toggle(kRowFastLoading, "FAST LOADING", s.fast_loading,
                                "Loading screens go as fast as this computer can. The game is the same, only the wait is "
                                "shorter; the loading screen's sound is muted meanwhile."));
        t.rows.push_back(toggle(kRowPauseUnfocused, "PAUSE WHEN UNFOCUSED", s.pause_unfocused,
                                "The game holds while its window is in the background (the native game only)."));
        // launchskip1: the launcher's own start (src/b3launcher/core.h skip_launcher); the game only keeps the setting.
        // In the game too: a player who turned it off never sees the launcher. settingsui: the launcher shows it on its
        // own LAUNCHER tab (launcher_at_start_row)
        if (!env.launcher) t.rows.push_back(launcher_at_start_row(s));
        if (!env.launcher && env.portable.shown) t.rows.push_back(portable_row(env.portable, false));   // portable mode (1.3.0)
        // gamemods1 (tools/native/boot_boost.h): START AT
        t.rows.push_back(choice(kRowBootTo, "START AT", {"Intro films", "Title screen", "Main menu, profile 1", "Main menu, profile 2",
                                                         "Main menu, profile 3"},
                                std::size_t(std::clamp(s.boot_to, 0, 4)),
                                "Where the game starts after it has loaded. Title screen: the intro films are not played. "
                                "Main menu: also Start on the title and that profile loaded, as LOAD PROFILE does; a profile "
                                "the save does not hold leaves the game's profile choice. Applies at the next start."));
        t.rows.push_back(choice(kRowSaveScreens, "SAVE SCREENS", {"Hide", "Show"}, s.hide_save_screens ? 0 : 1,
                                "Hide: the AUTOSAVE page after a race is skipped quickly and not shown, as the game's card "
                                "messages are. The save and the game are the same. Show: the page as on the PS2."));
        if (!kAudioTab) audio_rows(t, s, env);   // the fallback: eight tabs, the music last on GAME
        c.tabs.push_back(std::move(t));
    }
    {   // extras (2026-11-23; docs/pc-settings.md §7.46): the port's gameplay and content additions, each off by default:
        // with every row off, the game plays as it shipped (the visual ones are on GRAPHICS / DISPLAY with the presets)
        Tab t{"EXTRAS", {}};
        constexpr const char* kRowOn = "On: SINGLE EVENT's SELECT OPTIONS has this row. Off: the game's own page.";
        t.rows.push_back(section(45, "SINGLE EVENT"));
        t.rows.push_back(toggle(kRowExtraEliminator, "ELIMINATOR", s.extra_eliminator,
                                "On: SINGLE EVENT's SELECT MODE has a fifth mode, ELIMINATOR: the last car is out at the end of "
                                "each lap. Off: the game's four modes."));
        t.rows.push_back(toggle(kRowExtraMoreRivals, "MORE RIVALS", s.extra_more_rivals,
                                "On: a SINGLE EVENT race's RIVALS row goes up to 7, for 8 cars. Off: up to 5, as in the game."));
        t.rows.push_back(section(46, "SELECT OPTIONS"));
        const std::pair<std::uint32_t, const char*> race_rows[] = {
            {kRowExtraTraffic, "TRAFFIC"}, {kRowExtraLaps, "LAPS"}, {kRowExtraRoadRageRivals, "ROAD RAGE RIVALS"},
            {kRowExtraTimedElimination, "TIMED ELIMINATION"},
            {kRowExtraTransmission, "TRANSMISSION"}, {kRowExtraStart, "START"}, {kRowExtraRivalStyle, "RIVAL STYLE"},
            {kRowExtraBurnoutPoints, "BURNOUT POINTS"}, {kRowExtraTimeLimit, "TIME LIMIT"},
            {kRowExtraTrafficPattern, "TRAFFIC PATTERN"}, {kRowExtraRivalsCars, "RIVALS' CARS"}, {kRowExtraTrafficCheck, "TRAFFIC CHECK"},
        };
        for (const auto& [id, label] : race_rows) t.rows.push_back(toggle(id, label, extra_row_on(s, id), kRowOn));
        t.rows.push_back(section(47, "SELECT CAR"));
        t.rows.push_back(toggle(kRowExtraHeavyweights, "HEAVYWEIGHTS", s.extra_heavyweights,
                                "On: the HEAVYWEIGHTS series in every SINGLE EVENT mode. Off: in Crash only, as in the game."));
        t.rows.push_back(toggle(kRowExtraTrafficSeries, "TRAFFIC SERIES", s.extra_traffic_series,
                                "On: SINGLE EVENT's SELECT CAR has the TRAFFIC and TRAFFIC SPECIAL series: drive a car, bus or "
                                "tuk-tuk from the traffic."));
        t.rows.push_back(toggle(kRowExtraCustomCars, "CUSTOM CARS", s.extra_custom_cars != 0,
                                "On: the car mods in your custom cars folder, as the CUSTOM series. Applies at the next start."));
        t.rows.push_back(toggle(kRowExtraBetaCars, "BETA CARS", s.extra_beta_cars != 0,
                                "On: the prototype cars the launcher imported, as the BETA series. Applies at the next start."));
        t.rows.push_back(toggle(kRowExtraCarViewer, "CAR VIEWER", s.extra_car_viewer,
                                "On: YOUR GARAGE opens the game's leftover car viewer."));
        // gameopts (tools/native/game_options.h): the gameplay options, off by default; their races keep their results apart
        t.rows.push_back(section(41, "GAMEPLAY"));
        t.rows.push_back(toggle(kRowMirrorMode, "MIRROR MODE", s.mirror_mode,
                                "Single races and two-player races on mirrored tracks: the world is drawn reversed left to right "
                                "and the steering follows it, so left still steers left on the screen. The HUD is not mirrored; "
                                "the writing on signs is. These races keep their results apart from your records. "
                                "Applies from the next race."));
        // gamemods1: Off / Traffic only / Full (Full: the rivals and the other player caught in the blast too, a car it wrecks
        // counted as your Aftertouch Takedown; game_options.h blast)
        t.rows.push_back(choice(kRowCrashbreakerAll, "CRASHBREAKER IN EVERY MODE", {"Off", "Traffic only", "Full"},
                                s.crashbreaker_all ? (s.crashbreaker_full ? 2u : 1u) : 0u,
                                "After a crash in a single or two-player race, Time Attack or Road Rage, the CRASHBREAKER button "
                                "blows your wreck up as in Crash mode, wrecking the traffic around it. Full: the rivals and the "
                                "other player in the blast are wrecked too, each counted as your Aftertouch Takedown. These races keep "
                                "their results apart from your records. Applies from the next race."));
        if (s.crashbreaker_all)   // (its companion, only with it on)
            t.rows.push_back(choice(kRowCrashbreakerCars, "CRASHBREAKER AFTER", {"0", "1", "2", "3"},
                                    std::size_t(std::clamp(s.crashbreaker_cars, 0, 3)),
                                    "How many vehicles your crash must hit before the CRASHBREAKER button works, as Crash mode "
                                    "counts them (races have less traffic, so fewer). 0: ready as soon as you crash. "
                                    "Applies from the next race."));
        // trafficcheck (tools/native/traffic_check.h): TRAFFIC CHECK for two-player races (Single Event: SELECT OPTIONS' row),
        // off by default; its races keep their results apart
        t.rows.push_back(choice(kRowTrafficCheck, "TRAFFIC CHECK (2 PLAYERS)", {"Off", "Revenge", "Extreme"},
                                std::size_t(std::clamp(s.traffic_check, 0, 2)),
                                "Two-player races: ram a car, van or lorry going your way and it is launched ahead instead of "
                                "wrecking you, for boost and a scored chain, as in Burnout Revenge. A car you check that hits your "
                                "rival takes them out for you. Revenge: oncoming traffic, buses and big rigs still crash you. Extreme: "
                                "they are shoved aside too. Single races have their own TRAFFIC CHECK row on SELECT OPTIONS. "
                                "These races keep their results apart from your records. Applies from the next race."));
        // options3 (tools/native/game_mods.h)
        t.rows.push_back(choice(kRowDeformation, "DEFORMATION", {"None", "50%", "100%", "150%", "200%"},
                                index_of(kDeformOrder, std::clamp(s.deformation, 0, 4)),
                                "How much the cars' bodies crumple when they hit something, in Single Event and two-player races. "
                                "100% is the original game's. "
                                "Another amount changes the race after the first hard hit, so these races keep their results "
                                "apart from your records. Applies from the next race."));
        // rrparts1 (tools/native/road_rage_parts.h): gameplay, so the race's records are kept apart
        t.rows.push_back(choice(kRowRoadRageParts, "ROAD RAGE PARTS", {"Game", "All loose"}, std::size_t(std::clamp(s.road_rage_parts, 0, 1)),
                                "All loose: in a single-event Road Rage, every loose part can come off a damaged car, not only the ones the game lets go. "
                                "Loose parts are physical, so the race is not the original: its records are kept apart. "
                                "Game: the original."));
        // gamemods1 (tools/native/game_limits.h): the game's two limits, kept by default (On: the change)
        t.rows.push_back(toggle(kRowIdlePause, "NO IDLE PAUSE", !s.idle_pause,
                                "On: a race never pauses itself. Off: as in the game, it pauses after 5 minutes without input."));
        t.rows.push_back(toggle(kRowLongEventEnd, "NO EVENT TIME LIMIT", !s.long_event_end,
                                "On: an event goes on until it ends the normal way; one kept going past the game's limit keeps no "
                                "records. Off: as in the game, an event still running after 25 to 40 minutes is ended for every car."));
        // party1 (tools/native/party_modes.h): PARTY MODES, off by default; their races keep their results apart
        t.rows.push_back(section(42, "PARTY MODES"));
        constexpr const char* kApart = " These races keep their results apart from your records. Applies from the next race.";
        t.rows.push_back(choice(kRowPartyCrazyAi, "CRAZY AI PHYSICS", {"Off", "Basic", "Local Wildlife", "Mayhem"},
                                std::size_t(std::clamp(s.party_crazy_ai, 0, 3)),
                                std::string("The rivals' physics go haywire: their wheels never leave the road as far as the game "
                                            "knows, and gravity barely holds them. Local Wildlife: they tumble without crashing. "
                                            "Mayhem: a rival that flips over blows up. Your own car drives as usual.") + kApart));
        t.rows.push_back(toggle(kRowPartyTukTuks, "TUK-TUKS", s.party_tuk_tuks,
                                std::string("In SINGLE EVENT, you and every rival drive the tuk-tuk from the city traffic, with the "
                                            "engine sound and handling of a compact. Crash events too.") + kApart));
        t.rows.push_back(choice(kRowPartyMoonJump, "MOON JUMP", {"Off", "Low", "Medium", "High"}, std::size_t(std::clamp(s.party_moon_jump, 0, 3)),
                                std::string("Hold the MOON JUMP button to lift your car into the air (4 on the keyboard; a controller "
                                            "button can be set in CONTROLS).") + kApart));
        t.rows.push_back(toggle(kRowPartyCrash, "CRASH ON DEMAND", s.party_crash,
                                std::string("The CRASH button crashes your car at once (5 on the keyboard; a controller button can "
                                            "be set in CONTROLS).") + kApart));
        t.rows.push_back(toggle(kRowPartyAiDriver, "AI DRIVER BUTTON", s.party_ai_driver,
                                std::string("The AI DRIVER button hands your car to the game's own AI driver, and back on the next "
                                            "press (6 on the keyboard; a controller button can be set in CONTROLS).") + kApart));
        t.rows.push_back(toggle(kRowPartyKamikaze, "KAMIKAZE TRAFFIC", s.party_kamikaze,
                                std::string("Traffic close ahead of any race car throws itself at it. Expect many wrecks.") + kApart));
        // EXPERIMENTAL (1.4.0, for feedback): night and rain, per race on SELECT OPTIONS (race_time.h, weather §38)
        t.rows.push_back(section(49, "EXPERIMENTAL"));
        constexpr const char* kExperimental = " Experimental: work in progress. Tell us what you think.";
        t.rows.push_back(toggle(kRowExtraNight, "NIGHT (EXPERIMENTAL)", s.extra_night,
                                std::string("On: SINGLE EVENT's SELECT OPTIONS has a TIME OF DAY row (Day / Night). Drawn only: your "
                                            "progress and records count as usual.") + kExperimental));
        if (s.extra_night)
            t.rows.push_back(choice(kRowNightWorldTour, "NIGHT IN WORLD TOUR", {"Off", "Always", "Random"},
                                    std::size_t(std::clamp(s.night_world_tour, 0, 2)),
                                    "World Tour's events at night: Always, or Random (each event as it starts). Drawn only: "
                                    "your progress and records count as usual."));
        t.rows.push_back(toggle(kRowExtraRain, "RAIN (EXPERIMENTAL)", s.extra_rain,
                                std::string("On: SINGLE EVENT's SELECT OPTIONS has a WEATHER row (Clear / Rain). Drawn only: your "
                                            "progress and records count as usual.") + kExperimental));
        if (s.extra_rain)
            t.rows.push_back(choice(kRowRainWorldTour, "RAIN IN WORLD TOUR", {"Off", "Always", "Random"},
                                    std::size_t(std::clamp(s.rain_world_tour, 0, 2)),
                                    "World Tour's events in the rain: Always, or Random (each event as it starts). Drawn only: "
                                    "your progress and records count as usual."));
        t.rows.push_back(section(48, "REPLAYS"));
        // replayview (tools/native/replay_view.h)
        t.rows.push_back(toggle(kRowRaceReplay, "RACE REPLAY", s.replays != 0,
                                "On: a race's results page offers REPLAY (F6, or Square / X): the game's own replay of the race you "
                                "have just driven. Every offline mode but Crash, which has its own. Your results, records and "
                                "progress stay as they were. Applies at the next start."));
        c.tabs.push_back(std::move(t));
    }
    if (env.profile > 0) {   // save1
        Tab t{"SAVES", {}};
        t.rows.push_back(note(kRowProfile, "SAVE FILE IN USE", "File " + std::to_string(env.profile),
                              "The game saves by itself to profile-" + std::to_string(env.profile) +
                                  ".sav in the save folder (its three profiles). Copy the file to take them to another computer."));
        std::vector<std::string> files;
        for (std::size_t k = 0; k < env.profile_used.size(); ++k)
            files.push_back("File " + std::to_string(k + 1) + (env.profile_used[k] ? "" : " (new)"));
        const int next = env.next_profile > 0 ? env.next_profile : env.profile;
        t.rows.push_back(choice(kRowSaveFile, "SAVE FILE AT START", files, std::size_t(std::clamp(next, 1, int(files.size())) - 1),
                                "Each save file holds three profiles of its own: more files, more profiles. Applies at the next start."));
        Row folder = note(kRowSaveFolder, "SAVE FOLDER", "Open", env.saves_folder);
        folder.id = kActionOpenSaveFolder;
        folder.disabled = false;
        t.rows.push_back(folder);
        Row ex = note(kActionExportSave, "EXPORT SAVE", "Export", "Writes this profile to a .sav file of your choice.");
        ex.disabled = false;
        t.rows.push_back(ex);
        Row im = note(kActionImportSave, "IMPORT SAVE", "Import",
                      "Replaces this profile with a saved game: a .sav, a PS2 save (.psu, .max) or a PCSX2 memory card "
                      "(.ps2, or a folder card). PCSX2's card is usually Mcd001.ps2 in its memcards folder. Applies at the next start.");
        im.disabled = false;
        t.rows.push_back(im);
        if (!env.save_status.empty())
            t.rows.push_back(note(kRowSaveStatus, "LAST RESULT", env.save_status.find("FAILED") != std::string::npos ? "Failed" : "Done",
                                  env.save_status));
        c.tabs.push_back(std::move(t));
    }
    {   // sdk2b: the code mods' rows (a section per mod), then DEVELOPER MODS: the last row of the last tab
        Tab t{"MODS", env.mod_rows};
        // tune4 (the tuning console, docs/modding/tuning.md)
        t.rows.push_back(section(18, "DEVELOPER"));
        t.rows.push_back(toggle(kRowDeveloperMods, "DEVELOPER MODS", s.developer_mods != 0,
                                "On: the tuning console. A DEVELOPER tab here and F9 in a race show the game's tuning values "
                                "and let you change them (that changes gameplay; replays keep the changes). Applies at the next start."));
        c.tabs.push_back(std::move(t));
    }
    for (const Tab& t : env.extra_tabs) c.tabs.push_back(t);   // tune4: the host's tabs (DEVELOPER)
    return c;
}

std::vector<std::uint32_t> duplicate_ids(const Content& c) {
    std::map<std::uint32_t, int> seen;
    std::vector<std::uint32_t> out;
    for (const Tab& t : c.tabs)
        for (const Row& r : t.rows)
            if (++seen[r.id] == 2) out.push_back(r.id);
    return out;
}

namespace {

// 1.1 (i18n.h): one language's table, looked up by the English text; the `{}` entries apart, longest first
struct Lookup {
    std::unordered_map<std::string, std::string> exact;
    struct Pattern { std::string head, tail, text; };
    std::vector<Pattern> patterns;
};
const Lookup* lookup_for(const std::string& code) {
    static const std::map<std::string, Lookup> all = [] {
        std::map<std::string, Lookup> m;
        for (const i18n::Table& t : i18n::kTables) {
            Lookup& l = m[t.code];
            for (std::size_t k = 0; k < t.count; ++k) {
                const std::string en = t.entries[k].en;
                const std::size_t at = en.find("{}");
                if (at == std::string::npos) l.exact.emplace(en, t.entries[k].text);
                else l.patterns.push_back({en.substr(0, at), en.substr(at + 2), t.entries[k].text});
            }
            std::stable_sort(l.patterns.begin(), l.patterns.end(), [](const Lookup::Pattern& a, const Lookup::Pattern& b) {
                return a.head.size() + a.tail.size() > b.head.size() + b.tail.size();
            });
        }
        return m;
    }();
    const auto it = all.find(code);
    return it == all.end() ? nullptr : &it->second;
}
// One label, value or sentence: the table's, a `{}` entry's with the text it stands for carried over, or itself (and
// then added to `missing` when given).
std::string translate_one(const Lookup& l, const std::string& s, std::vector<std::string>* missing = nullptr) {
    if (const auto it = l.exact.find(s); it != l.exact.end()) return it->second;
    for (const Lookup::Pattern& p : l.patterns)
        if (s.size() > p.head.size() + p.tail.size() && s.compare(0, p.head.size(), p.head) == 0 &&
            s.compare(s.size() - p.tail.size(), p.tail.size(), p.tail) == 0) {
            std::string out = p.text;
            const std::size_t at = out.find("{}");
            if (at != std::string::npos) out.replace(at, 2, s.substr(p.head.size(), s.size() - p.head.size() - p.tail.size()));
            return out;
        }
    if (missing) missing->push_back(s);
    return s;
}
std::string translate_with(const Lookup& l, const std::string& text, std::vector<std::string>* missing = nullptr) {
    if (text.empty()) return text;
    if (l.exact.count(text)) return l.exact.at(text);
    if (const std::string whole = translate_one(l, text); whole != text) return whole;
    // a description: sentence by sentence (a sentence ends at ". "), each the table's or itself
    std::string out;
    std::size_t from = 0;
    while (from < text.size()) {
        std::size_t end = text.find(". ", from);
        end = end == std::string::npos ? text.size() : end + 1;
        if (!out.empty()) out += ' ';
        out += translate_one(l, text.substr(from, end - from), missing);
        from = end;
        while (from < text.size() && text[from] == ' ') ++from;
    }
    return out;
}

}  // namespace

std::string ui_language(const Settings& s, const Env& env) {
    for (const auto& l : env.languages)
        if (!s.language.empty() && l.first == s.language) return s.language;
    return {};
}

std::string translate_text(const std::string& text, const std::string& language) {
    const Lookup* l = language.empty() ? nullptr : lookup_for(language);
    return l ? translate_with(*l, text) : text;
}

std::string translate_format(const std::string& pattern, const std::string& arg, const std::string& language) {
    const Lookup* l = language.empty() ? nullptr : lookup_for(language);
    std::string out = pattern;
    if (l)
        if (const auto it = l->exact.find(pattern); it != l->exact.end()) out = it->second;
    if (l && out == pattern)   // the table keeps `{}` entries apart: the one whose key is this pattern
        for (const Lookup::Pattern& p : l->patterns)
            if (p.head + "{}" + p.tail == pattern) { out = p.text; break; }
    if (const std::size_t at = out.find("{}"); at != std::string::npos) out.replace(at, 2, arg);
    return out;
}

namespace {
void translate_into(Content& c, const Lookup& l, std::vector<std::string>* missing) {
    const auto tr = [&](std::string& s) { s = translate_with(l, s, missing); };
    for (Tab& t : c.tabs) {
        tr(t.label);
        for (Row& r : t.rows) {
            tr(r.label);
            // settingsui: the folder rows' value is the player's folder name (as it is; "None" is ours)
            const bool folder = (r.id == kActionChooseHdPack || r.id == kActionChooseMusic) && !(r.values.size() == 1 && r.values[0] == "None");
            if (!folder)
                for (std::string& v : r.values) tr(v);
            tr(r.level_text);
            tr(r.description);
        }
    }
    tr(c.popup);
    tr(c.title);
    tr(c.footer_keys);
    tr(c.footer_back);
    Content::Words& w = c.words;   // settingsui: the prompt bar
    for (std::string* x : {&w.move, &w.change, &w.choose, &w.close, &w.section, &w.rebind, &w.add, &w.remove, &w.reset, &w.click,
                           &w.wheel, &w.scroll, &w.right_click, &w.close_short, &w.hue, &w.shade, &w.fine, &w.accept, &w.cancel})
        tr(*x);
}
}  // namespace

void translate(Content& c, const std::string& language) {
    if (const Lookup* l = language.empty() ? nullptr : lookup_for(language)) translate_into(c, *l, nullptr);
}

std::vector<std::string> untranslated(Content c, const std::string& language) {
    std::vector<std::string> missing;
    if (const Lookup* l = lookup_for(language)) translate_into(c, *l, &missing);
    return missing;
}

std::vector<std::int32_t> paint_preset_colours() {   // paintsel
    std::vector<std::int32_t> v;
    for (const PaintPreset& p : kPaintPresets) v.push_back(std::int32_t(p.r << 16 | p.g << 8 | p.b));
    return v;
}

Content paint_panel_content(const Settings& s, int player) { return paint_panel_content(s, player, kLayerBody); }   // paintsel

// ---- paint2: the car's paint layers ----

PaintLayerRef paint_layer(Settings& s, int player, int layer) {
    const bool p2 = player == 1;
    PaintLayerRef r;
    switch (std::clamp(layer, 0, kPaintLayers - 1)) {
    case kLayerBody:
        r = p2 ? PaintLayerRef{&s.p2_car_paint, {&s.p2_paint_red, &s.p2_paint_green, &s.p2_paint_blue}}
               : PaintLayerRef{&s.car_paint, {&s.paint_red, &s.paint_green, &s.paint_blue}};
        break;
    case kLayerDecals:
        r = p2 ? PaintLayerRef{&s.p2_decal_paint, {&s.p2_decal_red, &s.p2_decal_green, &s.p2_decal_blue}}
               : PaintLayerRef{&s.decal_paint, {&s.decal_red, &s.decal_green, &s.decal_blue}};
        break;
    case kLayerWheels:
        r = p2 ? PaintLayerRef{&s.p2_wheel_paint, {&s.p2_wheel_red, &s.p2_wheel_green, &s.p2_wheel_blue}}
               : PaintLayerRef{&s.wheel_paint, {&s.wheel_red, &s.wheel_green, &s.wheel_blue}};
        break;
    default:
        r = p2 ? PaintLayerRef{&s.p2_flame_paint, {&s.p2_flame_red, &s.p2_flame_green, &s.p2_flame_blue}}
               : PaintLayerRef{&s.flame_paint, {&s.flame_red, &s.flame_green, &s.flame_blue}};
        break;
    }
    return r;
}

void paint_factory(Settings& s, int player) {
    for (int l = 0; l < kPaintLayers; ++l) *paint_layer(s, player, l).custom = 0;
    (player == 1 ? s.p2_car_decals : s.car_decals) = true;
}

void rgb_to_hsv(const int rgb[3], float& h, float& s, float& v) {
    const float r = float(std::clamp(rgb[0], 0, 255)) / 255.0f, g = float(std::clamp(rgb[1], 0, 255)) / 255.0f,
                b = float(std::clamp(rgb[2], 0, 255)) / 255.0f;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) { h = 0.0f; return; }
    if (mx == r) h = 60.0f * std::fmod((g - b) / d + 6.0f, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
    else h = 60.0f * ((r - g) / d + 4.0f);
}

void hsv_to_rgb(float h, float s, float v, int rgb[3]) {
    h = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f);
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float c = v * s, x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    const float o[3] = {r + m, g + m, b + m};
    for (int k = 0; k < 3; ++k) rgb[k] = std::clamp(int(std::lround(o[k] * 255.0f)), 0, 255);
}

void ColourPicker::open(const int rgb[3]) {
    for (int k = 0; k < 3; ++k) rgb_[k] = saved_[k] = std::clamp(rgb[k], 0, 255);
    rgb_to_hsv(rgb_, h_, s_, v_);
    on_ = true;
    fine_ = false;
    was_fine_ = was_accept_ = was_cancel_ = true;   // the press that opened it acts only after its release
}

ColourPicker::Result ColourPicker::step(const PickerInput& in) {
    if (!on_) return Result::kNone;
    const bool fine = in.fine && !was_fine_, accept = in.accept && !was_accept_, cancel = in.cancel && !was_cancel_;
    was_fine_ = in.fine;
    was_accept_ = in.accept;
    was_cancel_ = in.cancel;
    if (cancel) {
        on_ = false;
        for (int k = 0; k < 3; ++k) rgb_[k] = saved_[k];
        return Result::kCancelled;
    }
    if (accept) {
        on_ = false;
        return Result::kAccepted;
    }
    if (fine) fine_ = !fine_;
    const float dt = std::clamp(in.seconds, 0.0f, 0.1f), k = fine_ ? 0.25f : 1.0f;
    const float h0 = h_, s0 = s_, v0 = v_;
    s_ = std::clamp(s_ + dt * 0.8f * k * float(int(in.right) - int(in.left)), 0.0f, 1.0f);
    v_ = std::clamp(v_ + dt * 0.8f * k * float(int(in.up) - int(in.down)), 0.0f, 1.0f);
    h_ = std::fmod(h_ + dt * 180.0f * k * float(int(in.hue_up) - int(in.hue_down)) + 360.0f, 360.0f);
    if (in.pointer && in.pointer_held) {   // the square or the bar where the pointer is held
        const float x = (*in.pointer)[0], y = (*in.pointer)[1];
        const auto inside = [&](const float (&b)[4]) { return b[2] > 0 && x >= b[0] && x <= b[0] + b[2] && y >= b[1] && y <= b[1] + b[3]; };
        if (inside(in.sq)) {
            s_ = std::clamp((x - in.sq[0]) / in.sq[2], 0.0f, 1.0f);
            v_ = std::clamp(1.0f - (y - in.sq[1]) / in.sq[3], 0.0f, 1.0f);
        } else if (inside(in.bar)) {
            h_ = std::clamp((y - in.bar[1]) / in.bar[3], 0.0f, 0.9999f) * 360.0f;   // (a vertical bar: red at the top)
        }
    }
    if (h_ == h0 && s_ == s0 && v_ == v0) return Result::kNone;
    int o[3];
    hsv_to_rgb(h_, s_, v_, o);
    const bool moved = o[0] != rgb_[0] || o[1] != rgb_[1] || o[2] != rgb_[2];
    for (int c = 0; c < 3; ++c) rgb_[c] = o[c];
    return moved ? Result::kChanged : Result::kNone;
}


Content paint_panel_content(const Settings& s, int player, int layer, const Content::Picker& picker) {   // paint2
    const bool p2 = player == 1;
    layer = std::clamp(layer, 0, kPaintLayers - 1);
    Settings& sm = const_cast<Settings&>(s);   // (read only here)
    const PaintLayerRef L = paint_layer(sm, player, layer);
    const bool custom = *L.custom == 1;
    Content c;
    c.title = p2 ? "PLAYER 2 CAR PAINT" : "CAR PAINT";
    c.picker = picker;
    Tab t{"CAR", {}};
    t.rows.push_back(choice(kRowPaintLayer, "LAYER", {kLayerNames[0], kLayerNames[1], kLayerNames[2], kLayerNames[3]}, std::size_t(layer),
                            "Which part of the car the rows below colour: the body, its decals (logos, numbers and stripes), the "
                            "wheels' rims or the boost glow. One colour of each for all your cars."));
    t.rows.push_back(layer_colour_row(s, player, layer, kRowLayerColour + 4u * std::uint32_t(player) + std::uint32_t(layer), "COLOUR",
                                      "Game: the game's own colour. A ready colour, or Custom: the colour of the rows below."));
    {
        char hex[16];
        std::snprintf(hex, sizeof hex, "#%02X%02X%02X", *L.rgb[0], *L.rgb[1], *L.rgb[2]);
        Row pick = note(kActionPaintPicker, "COLOUR PICKER", hex,
                        "Any colour: the square sets how strong and how bright it is, the bar its hue (L1 / R1). CONFIRM keeps it, "
                        "BACK puts the colour before back.");
        pick.disabled = false;
        pick.swatch = (*L.rgb[0] & 255) << 16 | (*L.rgb[1] & 255) << 8 | (*L.rgb[2] & 255);
        t.rows.push_back(pick);
    }
    {
        const char* const names[3] = {"PAINT RED", "PAINT GREEN", "PAINT BLUE"};
        std::int32_t rgb = 0;
        for (int k = 0; k < 3; ++k) rgb = rgb << 8 | std::clamp(*L.rgb[k], 0, 255);
        for (int k = 0; k < 3; ++k) {
            const int v = std::clamp(*L.rgb[k], 0, 255);
            Row r = slider(kRowLayerRgb + 3u * (4u * std::uint32_t(player) + std::uint32_t(layer)) + std::uint32_t(k), names[k], kPaintLevels,
                           std::size_t((v + kPaintStep / 2) / kPaintStep), std::to_string(v),
                           "The custom paint's colour, 0 to 255 for each of red, green and blue.");
            r.disabled = !custom;
            r.swatch = rgb;
            t.rows.push_back(r);
        }
    }
    const Content all = build_content(s, Env{});
    for (const std::uint32_t id : {std::uint32_t(kRowPaintFinish), p2 ? std::uint32_t(kRowP2Decals) : std::uint32_t(kRowDecals)})
        for (const Tab& tab : all.tabs)
            for (const Row& r : tab.rows)
                if (r.id == id && r.kind != RowKind::kSection) {
                    Row o = r;
                    if (p2 && o.label.rfind("P2 ", 0) == 0) o.label = o.label.substr(3);
                    t.rows.push_back(std::move(o));
                }
    Row factory = note(kActionPaintFactory + std::uint32_t(p2 ? 1 : 0), "FACTORY COLOURS", "Reset",
                       "Every layer back to the game's colours, and the decals on.");
    factory.disabled = false;
    t.rows.push_back(factory);
    c.tabs.push_back(std::move(t));
    return c;
}

// ---- nextstart: the rows read at the game's start, the question box, the pending marker ----

bool next_start_row(std::uint32_t id) {
    switch (id) {
    case kRowLatency: case kRowLead: case kRowFlight: case kRowMsaa: case kRowFilter: case kRowGraphicsApi:
    case kRowHd: case kRowXboxTextures: case kRowHdMemory: case kActionChooseHdPack:
    case kRowLanguage: case kRowDjVoice: case kRowRecordReplays: case kRowRaceReplay: case kRowBootTo: case kRowDeveloperMods:
    case kRowSaveFile:
    case kRowExtraCustomCars: case kRowExtraBetaCars:   // extras: the extra cars are added to the game's list at the start
        return true;
#ifdef _WIN32
    case kRowRumbleTriggers:   // issue #16: the controller path is chosen at the start (b3port/trigger_rumble.h)
        return true;
#endif
    default:
        return false;
    }
}

namespace {
// The shown value of each next-start row of `c` (a choice / toggle: its value's text; a note: its one text)
std::vector<std::pair<const Row*, std::string>> next_start_values(const Content& c) {
    std::vector<std::pair<const Row*, std::string>> out;
    for (const Tab& t : c.tabs)
        for (const Row& r : t.rows)
            if (next_start_row(r.id) && !r.values.empty()) out.emplace_back(&r, r.values[std::min(r.value, r.values.size() - 1)]);
    return out;
}
}  // namespace

std::vector<PendingChange> next_start_changes(const Settings& from, const Env& env_from, const Settings& to, const Env& env_to) {
    const Content a = build_content(from, env_from), b = build_content(to, env_to);
    const auto va = next_start_values(a), vb = next_start_values(b);
    std::vector<PendingChange> out;
    for (const auto& [row, now] : vb)
        for (const auto& [old_row, then] : va)
            if (old_row->id == row->id) {
                if (then != now) out.push_back({row->id, row->label, then, now});
                break;
            }
    return out;
}

void revert_next_start(Settings& s, const Settings& from) {
    s.latency = from.latency;
    s.lead = from.lead;
    s.frames_in_flight = from.frames_in_flight;
    s.msaa = from.msaa;
    s.filter = from.filter;
    s.graphics_api = from.graphics_api;
    s.hd_textures = from.hd_textures;
    s.xbox_textures = from.xbox_textures;
    s.hd_texture_memory = from.hd_texture_memory;
    s.hd_pack_dir = from.hd_pack_dir;
    s.language = from.language;
    s.voices = from.voices;
    s.record_replays = from.record_replays;
    s.replays = from.replays;
    s.boot_to = from.boot_to;
    s.developer_mods = from.developer_mods;
#ifdef _WIN32
    s.rumble_triggers = from.rumble_triggers;   // issue #16
#endif
}

Content restart_ask_content(const std::vector<PendingChange>& changes, bool in_race, bool can_restart) {
    Content c;
    c.title = "APPLIES AT THE NEXT START";
    c.title_menu_face = true;   // (the heading face has no X, nor the translations' accents)
    Tab t{"RESTART", {}};
    for (std::size_t k = 0; k < changes.size(); ++k) {   // section rows: shown, never focused (the focus starts on KEEP)
        Row r = section(kRowAskChange, changes[k].label + ": " + changes[k].before + "  >  " + changes[k].after);
        t.rows.push_back(std::move(r));
    }
    const auto choice_row = [&](std::uint32_t id, const char* label, const char* value, std::string desc) {
        Row r = note(id, label, value, std::move(desc));
        r.disabled = false;
        t.rows.push_back(std::move(r));
    };
    // KEEP first: the box's default, so a stray confirm never restarts (a race in progress)
    choice_row(kActionAskKeep, "KEEP", "Next start", "Saved: the game uses it from its next start.");
    if (can_restart) {
        std::string restart = "Saved, then the game closes and starts again at the title screen, the change in use.";
        if (in_race) restart += " The race in progress ends.";
        choice_row(kActionAskRestart, "APPLY AND RESTART", "Restart", restart);
    }
    choice_row(kActionAskRevert, "REVERT", "Undo", "These settings go back to what they were when the page opened.");
    c.tabs.push_back(std::move(t));
    c.footer_back = "BACK  KEEP";
    c.words.close = c.words.close_short = "KEEP";   // (the prompt bar: Back keeps)
    return c;
}

void mark_pending(Content& c, const Settings& started, const Env& env_started, const std::string& language) {
    const Content then = build_content(started, env_started);
    const auto was = next_start_values(then);
    const std::string note_text = translate_text("Changed: applies at the next start.", language);
    for (Tab& t : c.tabs)
        for (Row& r : t.rows) {
            if (!next_start_row(r.id) || r.values.empty()) continue;
            for (const auto& [row, value] : was)
                if (row->id == r.id) {
                    const std::size_t at = std::min(r.value, r.values.size() - 1);
                    // (the untranslated text compared: `then` is in English, `c` translated, so the index for choices)
                    const bool differs = r.kind == RowKind::kAction ? translate_text(value, language) != r.values[at]
                                                                   : row->value != r.value;
                    if (differs) {
                        r.values[at] += " *";
                        r.description = note_text + " " + r.description;
                    }
                    break;
                }
        }
}

bool language_edit(Settings& s, const Env& env, const Edit& e) {
    if (e.action || e.id != kRowLanguage) return false;
    s.language = e.value == 0 || e.value > env.languages.size() ? std::string() : env.languages[e.value - 1].first;
    return true;
}

bool apply_edit(Settings& s, const Edit& e) {
    const std::size_t v = e.value;
    if (e.id >= kRowLayerColour && e.id < kRowLayerColour + 8u && !e.action) {   // paint2: a layer's COLOUR (Game, ready, Custom)
        const int player = int(e.id - kRowLayerColour) / 4, layer = int(e.id - kRowLayerColour) % 4;
        const PaintLayerRef L = paint_layer(s, player, layer);
        if (v == 0) {
            *L.custom = 0;
        } else if (v - 1 < kPaintPresetCount) {
            const PaintPreset& p = kPaintPresets[v - 1];
            *L.rgb[0] = p.r;
            *L.rgb[1] = p.g;
            *L.rgb[2] = p.b;
            *L.custom = 1;
        } else {
            *L.custom = 1;   // Custom: the colour as it is
        }
        return true;
    }
    if (e.id >= kRowLayerRgb && e.id < kRowLayerRgb + 24u && !e.action) {   // paint2: a layer's RED / GREEN / BLUE
        const int n = int(e.id - kRowLayerRgb), player = n / 12, layer = (n / 3) % 4, ch = n % 3;
        *paint_layer(s, player, layer).rgb[ch] = int(std::min(v, kPaintLevels - 1)) * kPaintStep;
        return true;
    }
    switch (e.id) {
    case kRowPreset: if (v < std::size(kPresets)) s = with_preset(s, kPresets[v]); return true;
    case kRowFullscreen: s.fullscreen = v != 0; return true;
    case kRowVsync: s.vsync = v != 0; return true;
    case kRowFrameCap: {   // settingsui: the list the row showed (frame_caps: the ini's own value among the page's)
        const std::vector<int> caps = frame_caps(s.frame_cap);
        if (v < caps.size()) s.frame_cap = caps[v];
        return true;
    }
    case kRowInterp: s.interpolation = v != 0; return true;
    case kRowHud: s.hud_wide = v != 0; return true;
    case kRowFeLayout: s.fe_layout = int(std::min<std::size_t>(v, 2)); return true;
    case kRowMovieFill: s.movie_fill = v != 0; return true;
    case kRowSplitLayout: s.split_layout = v ? 1 : 0; return true;   // splitlayout1
    case kRowPauseUnfocused: s.pause_unfocused = v != 0; return true;
    case kRowFastLoading: s.fast_loading = v != 0; return true;   // fastload1
    case kRowLatency: s.latency = int(std::min<std::size_t>(v, 3)); return true;
    case kRowLead: s.lead = v == 0 ? -1 : int(std::min<std::size_t>(v, 5) - 1) * 25; return true;
    case kRowFlight: s.frames_in_flight = v == 0 ? -1 : int(std::min<std::size_t>(v, 3)); return true;
    case kRowMsaa: if (v < std::size(kMsaa)) s.msaa = kMsaa[v]; return true;
    case kRowFilter: if (v <= std::size_t(Filter::kAniso16)) s.filter = static_cast<Filter>(v); return true;
    case kRowRenderScale: if (v < std::size(kRenderScales)) s.render_scale = kRenderScales[v]; return true;   // effects2
    case kRowUpscaling: s.upscaling = v ? 1 : 0; return true;   // 1.1
    case kRowDither: s.dither = v != 0; return true;   // perf1
    case kRowGraphicsApi: if (v > 2) return false; s.graphics_api = int(v); return true;   // d3d12
    case kRowHd: s.hd_textures = v != 0; return true;
    case kRowXboxTextures: s.xbox_textures = v != 0; return true;   // xboximport
    case kRowHdMemory: s.hd_texture_memory = int(std::min<std::size_t>(v, 2)); return true;   // texpack
    case kRowCarDetail: s.car_detail_highest = v != 0; return true;
    case kRowCarReflect: s.car_reflections = int(std::min<std::size_t>(v, 2)); return true;   // reflect1
    case kRowReflectDistance: s.reflect_distance = int(std::min<std::size_t>(v, 2)); return true;   // reflect2
    case kRowReflectDetail: s.reflect_detail = int(std::min<std::size_t>(v, 2)); return true;
    case kRowReflectUpdate: s.reflect_update = int(std::min<std::size_t>(v, 1)); return true;
    case kRowReflectResolution: s.reflect_resolution = int(std::min<std::size_t>(v, 1)); return true;
    case kRowShadows: s.shadows = v != 0; return true;
    case kRowSkids: s.skid_marks = v != 0; return true;
    case kRowSpecular: s.specular = v != 0; return true;
    case kRowSun: s.sun = v != 0; return true;
    case kRowCoronas: s.coronas = v != 0; return true;
    case kRowWholeTrack: s.whole_track = v != 0; return true;
    case kRowBlur: s.motion_blur = v != 0; return true;
    case kRowRadial: s.radial_blur = int(std::min<std::size_t>(v, 10)) * 10; return true;
    case kRowBloom: s.bloom = v != 0; return true;
    case kRowBloomLevel: s.bloom = v != 0; if (v != 0) s.bloom_level = int(std::min<std::size_t>(v, 2)); return true;   // bloom2
    case kRowPost: s.post_pc = v != 0; return true;
    case kRowSparks: s.sparks = int(std::min<std::size_t>(v, 3)); return true;
    case kRowScrapes: s.scrapes = v != 0; return true;
    case kRowPopcorn: s.popcorn = int(std::min<std::size_t>(v, 3)); return true;
    case kRowBatterySaver: s.battery_saver = v != 0; return true;
    case kRowDamage: s.damage = v != 0; return true;
    case kRowGlass: s.glass_damage = v != 0; return true;
    case kRowFov: s.fov = fov_of_level(v); return true;
    case kRowFovCorrection: s.fov_correction = v != 0; return true;
    case kRowCameraDistance: s.camera_distance = int(std::min<std::size_t>(v, 6) + 9) * 10; return true;   // 1.1
    case kRowCameraHeight: s.camera_height = int(std::min<std::size_t>(v, 4)) * 25; return true;
    case kRowCarPaint: s.car_paint = v ? 1 : 0; return true;   // carpaint
    case kRowPaintRed: s.paint_red = int(std::min(v, kPaintLevels - 1)) * kPaintStep; return true;
    case kRowPaintGreen: s.paint_green = int(std::min(v, kPaintLevels - 1)) * kPaintStep; return true;
    case kRowPaintBlue: s.paint_blue = int(std::min(v, kPaintLevels - 1)) * kPaintStep; return true;
    case kRowDecals: s.car_decals = v != 0; return true;
    case kRowPaintFinish: s.paint_finish = int(std::min<std::size_t>(v, 3)); return true;   // padpaint (options3: 3 chrome)
    case kRowP2Paint: s.p2_car_paint = v ? 1 : 0; return true;
    case kRowP2Red: s.p2_paint_red = int(std::min(v, kPaintLevels - 1)) * kPaintStep; return true;
    case kRowP2Green: s.p2_paint_green = int(std::min(v, kPaintLevels - 1)) * kPaintStep; return true;
    case kRowP2Blue: s.p2_paint_blue = int(std::min(v, kPaintLevels - 1)) * kPaintStep; return true;
    case kRowP2Decals: s.p2_car_decals = v != 0; return true;
    case kActionPaintFactory:       // paint2: FACTORY COLOURS (player 1, 2)
    case kActionPaintFactory + 1:
        if (!e.action) return false;
        paint_factory(s, int(e.id - kActionPaintFactory));
        return true;
    case kRowPaintPreset:   // settingsui: a ready colour (Custom: the colour as it is) and the custom paint on
    case kRowP2PaintPreset:
        if (v < kPaintPresetCount) {
            const PaintPreset& p = kPaintPresets[v];
            const bool p2 = e.id == kRowP2PaintPreset;
            (p2 ? s.p2_paint_red : s.paint_red) = p.r;
            (p2 ? s.p2_paint_green : s.paint_green) = p.g;
            (p2 ? s.p2_paint_blue : s.paint_blue) = p.b;
            (p2 ? s.p2_car_paint : s.car_paint) = 1;
        }
        return true;
    case kRowTimeOfDay: s.time_of_day = v ? 1 : 0; return true;   // night
    case kRowMoon: s.moon = v != 0; return true;
    case kRowSurfaceDetail: s.surface_detail = v != 0; return true;
    case kRowAmbientOcclusion: s.ambient_occlusion = v != 0; return true;
    case kRowSunShadows: s.sun_shadows = v != 0; return true;   // sunshadow
    case kRowPropShadows: s.prop_shadows = v != 0; return true;   // propshadow1
    case kRowTunnelLights: s.tunnel_lights = int(std::min<std::size_t>(v, 2)); return true;   // nightfix, xboxglow
    case kRowWeather: s.weather = v != 0 ? 1 : 0; return true;   // weather
    case kRowRainVolume: s.rain_volume = int(std::min<std::size_t>(v, 20)) * 10; return true;   // weather (a slider of 21 steps: 0..200 %)
    case kRowFps: s.fps_view = int(std::min<std::size_t>(v, 3)); return true;   // fpsoverlay2
    case kRowFpsCorner: s.fps_corner = int(std::min<std::size_t>(v, 3)); return true;
    case kRowSkipHint: s.skip_hint = v != 0; return true;   // mac5
    case kRowSpeedUnits: s.speed_units = v ? 1 : 0; return true;   // units1
    case kRowRpmMeter: s.rpm_meter = v != 0; return true;             // hudmods
    case kRowBoostShake: s.boost_shake = v != 0; return true;         // hudmods
    case kRowTakedownPitch: s.takedown_pitch = v != 0; return true;   // hudmods
    case kRowDateFormat: s.date_format = v ? 1 : 0; return true;   // mac5
    case kRowPhotoRow: s.photo_row = v != 0; return true;   // photorow1
    case kRowSettingsBack: s.settings_back_keeps = v == 0; return true;   // settingsback (0 Keep changes, 1 Cancel)
    case kRowLauncherAtStart: s.launcher_at_start = v != 0; return true;   // launchskip1
    case kRowPlaylistMode: s.playlist_mode = int(std::min<std::size_t>(v, 2)); return true;   // custom soundtrack
    case kRowOtherMusicApps: s.other_music_apps = v ? 1 : 0; return true;   // nowplaying1
    case kRowCustomMusicVolume: s.custom_music_volume = int(std::min<std::size_t>(v, 20)) * 10; return true;   // musicvol1
    case kRowSoundOutput: s.sound_output = v ? 1 : 0; return true;   // audioq
    case kRowCustomMusicLoudness: s.custom_music_loudness = v ? 1 : 0; return true;   // musicloud
    case kRowFlashes: s.screen_flashes = v ? 1 : 0; return true;   // accessibility
    case kRowHudColour: s.hud_colour_filter = int(std::min<std::size_t>(v, 3)); return true;
    case kRowBoostMode: s.boost_mode = v ? 1 : 0; return true;
    case kRowHudSize: s.hud_size = int(std::min<std::size_t>(v, 3)); return true;   // textsize1
    case kRowGhost: s.ghost = v ? 1 : 0; return true;   // ghost1
    case kRowMirrorMode: s.mirror_mode = v != 0; return true;           // gameopts
    case kRowCrashbreakerAll: s.crashbreaker_all = v != 0; s.crashbreaker_full = v == 2; return true;   // (gamemods1: Full)
    case kRowCrashbreakerCars: s.crashbreaker_cars = int(std::min<std::size_t>(v, 3)); return true;
    case kRowPartyCrazyAi: s.party_crazy_ai = int(std::min<std::size_t>(v, 3)); return true;   // party1
    case kRowPartyMoonJump: s.party_moon_jump = int(std::min<std::size_t>(v, 3)); return true;
    case kRowPartyCrash: s.party_crash = v != 0; return true;
    case kRowPartyAiDriver: s.party_ai_driver = v != 0; return true;
    case kRowPartyKamikaze: s.party_kamikaze = v != 0; return true;
    case kRowPartyTukTuks: s.party_tuk_tuks = v != 0; return true;
    case kRowSpeedrunTimer: s.speedrun_timer = int(std::min<std::size_t>(v, 2)); return true;   // speedrun1
    case kRowRaceReplay: s.replays = v != 0 ? 1 : 0; return true;   // replayview
    case kRowInstantRestart: s.instant_restart = v ? 1 : 0; return true;
    case kRowTrafficCheck: s.traffic_check = int(std::min<std::size_t>(v, 2)); return true;   // trafficcheck
    case kRowRumble: s.rumble = v ? 1 : 0; return true;   // rumble1
    case kRowRumbleStrength: s.rumble_strength = int(std::min<std::size_t>(v, 9) + 1) * 10; return true;
    case kRowRumbleTriggers: s.rumble_triggers = v != 0; return true;
    case kRowAnalogPedals: s.analog_pedals = v != 0; return true;   // 1.0.1
    case kRowAdaptiveTriggers: s.adaptive_triggers = v ? 1 : 0; return true;   // pad1
    case kRowAdaptiveStrength: s.adaptive_triggers_strength = int(std::min<std::size_t>(v, 9) + 1) * 10; return true;
    case kRowSaveScreens: s.hide_save_screens = v == 0; return true;   // autosave1 (Hide, Show)
    case kRowBootTo: s.boot_to = int(std::min<std::size_t>(v, 4)); return true;   // gamemods1
    case kRowIdlePause: s.idle_pause = v == 0; return true;   // extras: NO IDLE PAUSE (On: the change)
    case kRowLongEventEnd: s.long_event_end = v == 0; return true;   // extras: NO EVENT TIME LIMIT (On: the change)
    case kRowExtraCustomCars: s.extra_custom_cars = v != 0 ? 1 : 0; return true;   // extras
    case kRowNightWorldTour: s.night_world_tour = int(std::min<std::size_t>(v, 2)); return true;
    case kRowRainWorldTour: s.rain_world_tour = int(std::min<std::size_t>(v, 2)); return true;
    case kRowExtraBetaCars: s.extra_beta_cars = v != 0 ? 1 : 0; return true;
    case kRowExtraEliminator: case kRowExtraMoreRivals: case kRowExtraTraffic: case kRowExtraLaps: case kRowExtraTimedElimination:
    case kRowExtraRoadRageRivals: case kRowExtraNight: case kRowExtraRain:
    case kRowExtraTransmission: case kRowExtraStart: case kRowExtraRivalStyle: case kRowExtraBurnoutPoints: case kRowExtraTimeLimit:
    case kRowExtraTrafficPattern: case kRowExtraRivalsCars: case kRowExtraTrafficCheck: case kRowExtraHeavyweights:
    case kRowExtraTrafficSeries: case kRowExtraCarViewer:
        *extra_row_field(s, e.id) = v != 0;
        return true;
    case kRowSeriesShoulders: s.series_shoulders = v ? 1 : 0; return true;   // carsel1
    case kRowCarStats: s.car_stats = v ? 1 : 0; return true;                 // carstats1
    case kRowTurnSignals: s.turn_signals = v ? 1 : 0; return true;           // blinkers1
    case kRowRoadRageParts: s.road_rage_parts = v ? 1 : 0; return true;      // rrparts1
    case kRowGarageCamera: s.garage_camera = v != 0; return true;   // garagecam1
    case kRowOrbitCamera: s.orbit_camera = v != 0; return true;     // orbitcam1
    case kRowOrbitStyle: s.orbit_style = int(std::min<std::size_t>(v, 1)); return true;   // orbitcam2
    case kRowCameraRoll: s.camera_roll = int(std::min<std::size_t>(v, 2)); return true;   // camroll1
    case kRowFinishCamera: s.finish_camera = v != 0 ? 1 : 0; return true;   // finishcam1
    case kRowCrashCamera: s.crash_camera = v != 0; return true;     // options3
    case kRowReplayCameraCuts: s.replay_camera_stay = v != 0; return true;   // replaycams
    case kRowDeformation: if (v < std::size(kDeformOrder)) s.deformation = kDeformOrder[v]; return true;   // options3
    case kRowDriverLabels: s.driver_labels = int(std::min<std::size_t>(v, 3)); return true;   // options3
    case kRowRecordReplays: s.record_replays = v ? 1 : 0; return true;   // replay1
    case kRowDeveloperMods: s.developer_mods = v ? 1 : 0; return true;   // tune4
    case kRowDjVoice: s.voices = v ? 1 : 0; return true;   // 1.3.0 VOICES
    case kRowGlyphs: if (v < std::size(kPromptOrder)) s.button_prompts = kPromptOrder[v]; return true;   // mac5
    default: return false;
    }
}

}  // namespace b3settings
