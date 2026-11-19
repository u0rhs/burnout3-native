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
    kRowFlashes = 120, kRowHudColour, kRowBoostMode, kRowHudSize,   // accessibility (kRowHudSize = 123: textsize1)
    kRowGhost = 130,   // ghost1 (Time Attack ghosts)
    kRowSpeedrunTimer = 140, kRowInstantRestart,   // speedrun1
    kRowRumble = 150, kRowRumbleStrength, kRowRumbleTriggers,   // rumble1 (controller rumble)
    kRowAnalogPedals = 272,   // 1.0.1 ANALOG TRIGGERS
    kRowRecordReplays = 160,   // replay1 (replay files)
    kRowDeveloperMods = 170,   // tune4 (the tuning console)
    kRowSplitLayout = 180,     // splitlayout1 (the 2-player split screen's layout; CAMERA)
    kRowAdaptiveTriggers = 190, kRowAdaptiveStrength,   // pad1 (DualSense adaptive triggers; INTERFACE > CONTROLLER)
    kRowSeriesShoulders = 200,   // carsel1 (car series on L1 / R1; INTERFACE > CONTROLLER)
    kRowLauncherAtStart = 210,   // launchskip1 (OPEN THE LAUNCHER AT START; INTERFACE, after DATE FORMAT)
    kRowBatterySaver = 245, kRowRenderScale = 246, kRowUpscaling = 247,   // 247: 1.1 UPSCALING      // effects2 (BATTERY SAVER; EFFECTS, last section)
    kRowCarReflect = 235,        // reflect1 (CAR REFLECTIONS; QUALITY, after CAR DETAIL)
    kRowBloomLevel = 260,        // bloom2 (BLOOM: Off / Soft / Standard; replaces the on / off toggle kRowBloom on the page)
    kRowFps = 255, kRowFpsCorner,   // fpsoverlay2 (FPS COUNTER and its corner; INTERFACE, first)
    kRowSpeedUnits = 220, kRowPhotoRow = 240,   // units1 (SPEED UNITS; INTERFACE, before DATE FORMAT)
    kRowProfile = 110, kRowSaveFolder, kRowSaveStatus,   // save1 (the actions: page.h kAction*)
    kSection = 0xFFFF0000u,
};

const int kCaps[] = {0, 30, 60, 120, 144, 165, 240};

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

}  // namespace

Content build_content(const Settings& s, const Env& env) {
    Content c;
    {
        Tab t{"DISPLAY", {}};
        std::vector<std::string> presets;
        for (Preset p : kPresets) presets.push_back(preset_name(p));
        t.rows.push_back(choice(kRowPreset, "QUALITY PRESET", presets, index_of(kPresets, matching_preset(s)),
                                "Sets every quality and effects row at once. HIGH is the default."));
        t.rows.push_back(section(1, "WINDOW"));
        t.rows.push_back(toggle(kRowFullscreen, "FULLSCREEN", s.fullscreen, "Borderless fullscreen on the window's display (F11 also switches it)."));
        t.rows.push_back(toggle(kRowVsync, "VSYNC", s.vsync, std::string("Waits for the display before showing each frame.") + kRestart));
        std::vector<std::string> caps;
        for (int cap : kCaps) caps.push_back(cap ? std::to_string(cap) : "Unlimited");
        t.rows.push_back(choice(kRowFrameCap, "FRAME RATE LIMIT", caps, index_of(kCaps, s.frame_cap),
                                "The most frames per second drawn. The game itself always runs at its own speed."));
        t.rows.push_back(toggle(kRowPauseUnfocused, "PAUSE WHEN UNFOCUSED", s.pause_unfocused,
                                "The game holds while its window is in the background (the native game only)."));
        t.rows.push_back(toggle(kRowFastLoading, "FAST LOADING", s.fast_loading,
                                "Loading screens go as fast as this computer can. The game is the same, only the wait is "
                                "shorter; the loading screen's sound is muted meanwhile."));
        t.rows.push_back(section(2, "MOTION AND LAYOUT"));
        t.rows.push_back(toggle(kRowInterp, "FRAME INTERPOLATION", s.interpolation,
                                "Smooth frames between the game's own. Off shows its 60 / 30 steps as on the PS2."));
        t.rows.push_back(choice(kRowHud, "RACE HUD LAYOUT", {"4:3", "Wide"}, s.hud_wide ? 1 : 0,
                                "Wide moves the race HUD to the window's edges. 4:3 keeps the PS2 layout."));
        t.rows.push_back(choice(kRowFeLayout, "MENU LAYOUT", {"Edges", "4:3", "Panels"}, std::size_t(std::clamp(s.fe_layout, 0, 2)),
                                "The menus on a wide screen: panels to the edges, the PS2's 4:3 picture, or anchored to the edges."));
        t.rows.push_back(choice(kRowMovieFill, "MENU MOVIE SIDES", {"Black", "Blur"}, s.movie_fill ? 1 : 0,
                                "Beside the 4:3 menu movies on a wide screen: black, or a blurred copy of the movie."));
        t.rows.push_back(section(3, "LATENCY"));
        t.rows.push_back(choice(kRowLatency, "LATENCY MODE", {"Auto", "Low", "Smooth", "Newest"}, std::size_t(std::clamp(s.latency, 0, 3)),
                                std::string("Low: least input delay. Smooth: steadiest motion. Newest: no blending.") + kRestart));
        t.rows.push_back(choice(kRowLead, "PREDICTION", {"Auto", "0%", "25%", "50%", "75%", "100%"}, s.lead < 0 ? 0 : std::size_t(std::clamp(s.lead, 0, 100) / 25 + 1),
                                std::string("How far Low mode looks past the newest game tick.") + kRestart));
        t.rows.push_back(choice(kRowFlight, "FRAMES IN FLIGHT", {"Auto", "1", "2", "3"}, s.frames_in_flight < 1 ? 0 : std::size_t(std::min(s.frames_in_flight, 3)),
                                std::string("Frames the GPU may queue: 1 is the most direct, more can be smoother.") + kRestart));
        c.tabs.push_back(std::move(t));
    }
    {
        Tab t{"GRAPHICS", {}};
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
                                    "graphics chip; the HUD and menus stay sharp. 100% is the normal picture."));
            t.rows.push_back(choice(kRowUpscaling, "UPSCALING", {"Bilinear", "Sharp"}, std::size_t(std::clamp(s.upscaling, 0, 1)),
                                    "How a RENDER SCALE below 100% is brought up to the window. Sharp: a sharper picture with "
                                    "clean edges, for a small cost. Bilinear: softer, the cheapest."));
        }
        t.rows.push_back(toggle(kRowDither, "DITHER", s.dither, "A fine pattern that hides the steps in smooth gradients such as the sky."));
        {   // mac5: the HD pack's folder (remaster-features §8)
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
        if (env.hd_pack)
            t.rows.push_back(toggle(kRowHd, "HD TEXTURES", s.hd_textures, "The HD texture pack given at start (applies when the game next starts). "
                                    "F4 switches the HD textures on and off at once while you play, to compare."));   // hdkey
        t.rows.push_back(choice(kRowCarDetail, "CAR DETAIL", {"Game", "Highest"}, s.car_detail_highest ? 1 : 0,
                                "Highest draws every car at full detail at any distance."));
        t.rows.push_back(choice(kRowCarReflect, "CAR REFLECTIONS", {"Off", "Original", "Enhanced"}, std::size_t(std::clamp(s.car_reflections, 0, 2)),
                                "Enhanced: glossy sky and sun reflections on the paint and glass. Original: the game's own. Off: none."));
        t.rows.push_back(section(11, "WORLD"));
        t.rows.push_back(choice(kRowWholeTrack, "DRAW DISTANCE", {"PS2", "Whole track"}, s.whole_track ? 1 : 0,
                                "Whole track: every part of the course drawn, no pop-in in the distance. PS2: the original's streamed set."));
        t.rows.push_back(toggle(kRowShadows, "CAR SHADOWS", s.shadows, "The shadows under the cars."));
        t.rows.push_back(toggle(kRowSkids, "SKID MARKS", s.skid_marks, "Tyre marks on the road."));
        t.rows.push_back(toggle(kRowSpecular, "ROAD SHINE", s.specular, "The road's specular highlight pass."));
        t.rows.push_back(toggle(kRowSun, "SUN AND LENS FLARE", s.sun, "The sun's glow and its lens flare."));
        t.rows.push_back(toggle(kRowCoronas, "CAR AND TRAFFIC LIGHTS", s.coronas, "The glow of head, tail and traffic lights and the boost flame."));
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
        t.rows.push_back(section(22, "POWER"));
        t.rows.push_back(toggle(kRowBatterySaver, "BATTERY SAVER", s.battery_saver,
                                "On: while the PC runs on battery, the frame rate is held to 60 and the sparks and crash debris drawn at "
                                "most at Medium. Plugged in, nothing changes. Drawn only: the game is the same."));
        c.tabs.push_back(std::move(t));
    }
    {
        Tab t{"CAMERA", {}};
        const std::size_t levels = std::size_t((kFovMax - kFovMin) / kFovStep + 1);
        const int shown = s.fov > 0 ? s.fov : kGameFov;
        t.rows.push_back(slider(kRowFov, "FIELD OF VIEW", levels, fov_level(s.fov), shown == kGameFov ? "90 (GAME)" : std::to_string(shown),
                                "How much of the road the race view shows, at full boost. The game's camera is unchanged."));
        t.rows.push_back(toggle(kRowFovCorrection, "FOV CORRECTION", s.fov_correction,
                                "With a changed field of view, moves the view so the car keeps its size."));
        // 1.1: the chase camera further back / higher (drawn only; View::apply_fov)
        t.rows.push_back(choice(kRowCameraDistance, "CAMERA DISTANCE", {"-10%", "Game", "+10%", "+20%", "+30%", "+40%", "+50%"},
                                std::size_t(std::clamp(s.camera_distance, 90, 150) / 10 - 9),
                                "How far behind the car the chase camera sits. Game: the original camera. Drawn only."));
        t.rows.push_back(choice(kRowCameraHeight, "CAMERA HEIGHT", {"Game", "+0.25 m", "+0.5 m", "+0.75 m", "+1 m"},
                                std::size_t(std::clamp(s.camera_height, 0, 100) / 25),
                                "Raises the chase camera above the car; it tilts down to keep the car in view. Game: the original camera. Drawn only."));
        // splitlayout1 (src/b3replay/splitlayout.h): the 2-player views. On this tab, not DISPLAY: a DISPLAY row would
        // change that tab's scroll bar in the golden record's PC SETTINGS shot (online tour, live_10400)
        t.rows.push_back(choice(kRowSplitLayout, "SPLIT SCREEN", {"Top / bottom", "Side by side"}, std::size_t(std::clamp(s.split_layout, 0, 1)),
                                "The two players' views in a 2-player race. Top / bottom is the game's own; side by side puts "
                                "them next to each other. Each view keeps its shape and its own HUD. Drawn only."));
        c.tabs.push_back(std::move(t));
    }
    {
        Tab t{"INTERFACE", {}};
        t.rows.push_back(choice(kRowFps, "FPS COUNTER", {"Off", "Minimal", "Standard", "Detailed"}, std::size_t(std::clamp(s.fps_view, 0, 3)),
                                "A frame-rate readout in a corner. Minimal: a small plate with the number. Standard: also the "
                                "1% low. Detailed: a big plate with a frame-time graph, the average, the latency and the render settings. F3 cycles the views."));
        t.rows.push_back(choice(kRowFpsCorner, "FPS COUNTER POSITION", {"Top left", "Top right", "Bottom left", "Bottom right"},
                                std::size_t(std::clamp(s.fps_corner, 0, 3)), "The corner the FPS counter sits in."));
        t.rows.push_back(choice(kRowGlyphs, "BUTTON PROMPTS", {"Auto", "PlayStation", "PS4 / PS5", "Xbox", "Nintendo", "Generic", "Keyboard"},
                                index_of(kPromptOrder, std::clamp(s.button_prompts, 0, 6)),
                                "The buttons the prompts show. Auto: the controller or keyboard you used last. PS4 / PS5: OPTIONS for START."));
        t.rows.push_back(toggle(kRowSkipHint, "VIDEO SKIP HINT", s.skip_hint,
                                "A corner hint with the button that skips the video playing. Every video plays unless you skip it."));
        // photorow1 (tools/native/pc_menu.h): the pause menu's PHOTO MODE row
        t.rows.push_back(toggle(kRowPhotoRow, "PHOTO MODE IN PAUSE MENU", s.photo_row,
                                "On: the race's pause menu has a PHOTO MODE row after SETTINGS, which opens photo mode (the key "
                                "P, or L3 + R3 on a controller, does too). Off: the pause menu is the game's own. From the next pause."));
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
                if (!env.language_status.empty()) idesc += " " + env.language_status;
                Row imp = note(kActionImportLanguages, "IMPORT LANGUAGES", "Choose disc", idesc);
                imp.disabled = false;
                t.rows.push_back(imp);
            }
        }
        // units1 (tools/native/speed_units.h): the game's own words (kph / mph, KM / MILES, M / FT)
        t.rows.push_back(choice(kRowSpeedUnits, "SPEED UNITS", {"MPH", "KM/H"}, std::size_t(std::clamp(s.speed_units, 0, 1)),
                                "Speeds and distances in miles or kilometres: the speedometer, the distance messages, "
                                "records and stats. MPH is the original US game's. The speedometer changes from the next race."));
        t.rows.push_back(choice(kRowDateFormat, "DATE FORMAT", {"Month/Day/Year", "Day/Month/Year"}, std::size_t(std::clamp(s.date_format, 0, 1)),
                                "How the game writes dates, such as a profile's DATE OF SAVE. Month/Day/Year is the original US game's."));
        // launchskip1: the launcher's own start (src/b3launcher/core.h skip_launcher); the game only keeps the setting
        t.rows.push_back(toggle(kRowLauncherAtStart, "OPEN THE LAUNCHER AT START", s.launcher_at_start,
                                "On: starting the game shows the launcher first. Off: the launcher starts the game at once. "
                                "Hold Shift, or Back / Select on a controller, while it starts to open the launcher anyway. "
                                "Applies at the next start."));
        t.rows.push_back(note(kRowElsewhere, "AUDIO AND CONTROLS", "Driver Details", "Volumes, the DJ and the controller layout: DRIVER DETAILS > SETTINGS."));
        // rumble1 (controller rumble, src/native/pad_actuator.h): a section here, not a 7th tab (the tab bar's width)
        t.rows.push_back(section(16, "CONTROLLER"));
        t.rows.push_back(choice(kRowRumble, "RUMBLE", {"Off", "On"}, std::size_t(std::clamp(s.rumble, 0, 1)),
                                "On: the game's own rumble on your controllers (player 1's and player 2's), as on the PS2's "
                                "DualShock 2. The game's VIBRATION option (Driver Details) still applies. Off is the original port."));
        {
            const int pct = std::clamp(s.rumble_strength, 10, 100) / 10 * 10;
            t.rows.push_back(slider(kRowRumbleStrength, "RUMBLE STRENGTH", 10, std::size_t(pct / 10 - 1), std::to_string(pct) + "%",
                                    "How strongly the controllers rumble. 100% is the full motor speed the game asks for."));
        }
        t.rows.push_back(toggle(kRowRumbleTriggers, "TRIGGER RUMBLE", s.rumble_triggers,
                                "Also the triggers' own motors, on controllers that have them (Xbox One and Series)."));
        // pad1 (src/native/pad_triggers.h): DualSense adaptive triggers, drawn from the game's state (nothing sent to the game)
        t.rows.push_back(choice(kRowAdaptiveTriggers, "ADAPTIVE TRIGGERS", {"Off", "On"}, std::size_t(std::clamp(s.adaptive_triggers, 0, 1)),
                                "On: a DualSense's triggers push back while you drive: the brake firmly, the accelerator lightly, "
                                "buzzing while you boost, with a kick when you crash. Free in menus and when paused. The game is unchanged."));
        {
            const int pct = std::clamp(s.adaptive_triggers_strength, 10, 100) / 10 * 10;
            t.rows.push_back(slider(kRowAdaptiveStrength, "TRIGGER STRENGTH", 10, std::size_t(pct / 10 - 1), std::to_string(pct) + "%",
                                    "How strongly the adaptive triggers push back and buzz. 100% is the full effect."));
        }
        // carsel1 (src/native/input_keys.h ShoulderSeries): opt-in, input-changing
        t.rows.push_back(choice(kRowSeriesShoulders, "CAR SERIES ON L1 / R1", {"Off", "On"}, std::size_t(std::clamp(s.series_shoulders, 0, 1)),
                                "On: in the car select, the shoulder buttons (L1 / R1, LB / RB, or their keys) change the car "
                                "series as Left / Right do, and show beside the series arrows. Changes the input the game gets; "
                                "Off is the original."));
        t.rows.push_back(toggle(kRowAnalogPedals, "ANALOG TRIGGERS", s.analog_pedals,
                                "On: a trigger set to accelerate or brake works gradually, like a pedal (light pressure, light "
                                "throttle). Both triggers together still powerslide. Off: a trigger acts as a button."));
        // ghost1 (Time Attack ghosts, src/b3replay/ghost.h): a section here, not a 7th tab (the tab bar's width)
        t.rows.push_back(section(14, "TIME ATTACK"));
        t.rows.push_back(choice(kRowGhost, "GHOST", {"Off", "Best lap"}, std::size_t(std::clamp(s.ghost, 0, 1)),
                                "Best lap: your laps in Time Attack are kept, and the best one on the track races with you as a "
                                "see-through car. Drawn only: the game never knows it is there."));
        // speedrun1 (remaster-features §5 "Speedrun support", src/b3replay/speedrun.h): a section here, not a 7th tab
        t.rows.push_back(section(15, "SPEEDRUN"));
        t.rows.push_back(choice(kRowSpeedrunTimer, "SPEEDRUN TIMER", {"Off", "Event", "Event + splits"},
                                std::size_t(std::clamp(s.speedrun_timer, 0, 2)),
                                "The event's time from the start signal to the finish, in game time (the game's own timer: the "
                                "result screen's numbers), with your best per event and car. Event + splits: each lap too. Drawn only."));
        t.rows.push_back(choice(kRowInstantRestart, "INSTANT RESTART", {"Off", "On"}, std::size_t(std::clamp(s.instant_restart, 0, 1)),
                                "On: F5, or SELECT + START on a controller, restarts the event while you drive, through the "
                                "game's own pause menu (RESTART, YES). The game gets those menu presses; Off is the original."));
        // replay1 (replay files, src/native/replay_file.h): a section here, not a 7th tab (the tab bar's width)
        t.rows.push_back(section(17, "REPLAYS"));
        t.rows.push_back(toggle(kRowRecordReplays, "RECORD REPLAYS", s.record_replays != 0,
                                "On: each session, from the start of the game to its end, is kept as a replay file that "
                                "plays it again exactly (the launcher's Replays page). The newest 20 are kept. Applies at the next start."));
        // accessibility (remaster-features "Accessibility"): a section here, not a 7th tab (the tab bar's width)
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
        t.rows.push_back(choice(kRowBoostMode, "BOOST", {"Hold", "Toggle"}, std::size_t(std::clamp(s.boost_mode, 0, 1)),
                                "Toggle: press boost once to keep boosting, again to stop (also stops when you pause or the "
                                "race ends). Changes the input the game gets; Hold is the original."));
        if (env.music) {   // the custom soundtrack (remaster-features §6; a host that plays it: native_play). A section here,
                           // not a 7th tab: seven tab labels do not fit the tab bar's width
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
                                        "How loud your own songs play, on top of RADIO VOLUME. At 100% a song plays as loud as "
                                        "the game plays its own songs; raise it for quiet files. A limiter keeps loud songs "
                                        "from clipping. The game's songs and the DJ do not change. Live."));
            }
            // nowplaying1 (tools/native/now_playing_popup.h): the system's now-playing interface, drawn only
            Row apps = choice(kRowOtherMusicApps, "OTHER MUSIC APPS", {"Off", "Show now playing"},
                              std::size_t(std::clamp(s.other_music_apps, 0, 1)), other_music_apps_text(s, env));
#if defined(__APPLE__)
            apps.disabled = true;   // no reader on macOS (src/native/now_playing.h)
#endif
            t.rows.push_back(apps);
        }
        // tune4 (the tuning console, docs/modding/tuning.md): after the tab's own rows (so they stay where they were),
        // before remap1's CONTROLS (native_play's page only)
        t.rows.push_back(section(18, "DEVELOPER"));
        t.rows.push_back(toggle(kRowDeveloperMods, "DEVELOPER MODS", s.developer_mods != 0,
                                "On: the tuning console. A DEVELOPER tab here and F9 in a race show the game's tuning values "
                                "and let you change them (that changes gameplay; replays keep the changes). Applies at the next start."));
        if (env.controls.on) {   // remap1 (src/native/input_bindings.h): a section here, not a 7th tab (the tab bar's width)
            const Env::Controls& k = env.controls;
            t.rows.push_back(section(300, "CONTROLS"));
            t.rows.push_back(choice(kRowCtlPlayer, "PLAYER", {"Player 1", "Player 2", "Shortcuts"}, k.player,
                                    "Whose controls the rows below show. The keyboard is shared: player 2's keys (none at "
                                    "first) plug in a second controller for them. Shortcuts: the window's own keys (restart, "
                                    "fullscreen, screenshot, bug report, the tuning overlay, the settings sections) and photo "
                                    "mode's controls."));
            t.rows.push_back(choice(kRowCtlDevice, "DEVICE", {"Keyboard", "Controller"}, k.device,
                                    "The keyboard's keys, or the buttons, sticks and triggers of this player's controllers."));
            if (k.device == 1 && k.player < 2)   // padlayout (1.2.1)
                t.rows.push_back(choice(kRowCtlLayout, "CONTROLLER LAYOUT", {k.layout_auto, "Xbox", "PlayStation", "Custom"},
                                        std::min<std::size_t>(k.layout, 3),
                                        "Which buttons do what on this player's controller. Auto: the layout of the controller's "
                                        "console, picked again when the controller changes: a PlayStation controller gets the PS2 "
                                        "game's, any other controller the original Xbox game's. Xbox, PlayStation: that layout on "
                                        "any controller. Custom: your own bindings in the rows below. Changing a binding below "
                                        "makes it Custom."));
            if (k.device == 0 && k.player < 2)
                t.rows.push_back(choice(kRowCtlView, "KEYS FOR", {"Driving", "Menus"}, k.view,
                                        "A key can do one thing while you drive and another in the menus (W accelerates, and "
                                        "moves up in a menu). The rows below show and change one of the two."));
            if (k.device == 1 && k.player < 2)   // padlayout: the controller's views
                t.rows.push_back(choice(kRowCtlView, "BUTTONS FOR", {"Driving", "Menus"}, k.view,
                                        "A button can do one thing while you drive and another in the menus (on the Xbox layout "
                                        "A boosts, and confirms in a menu). The rows below show and change one of the two."));
            for (std::size_t a = 0; a < k.actions.size() && kActionCtlFirst + a <= kActionCtlLast; ++a) {
                Row r = note(kActionCtlFirst + std::uint32_t(a), k.actions[a].label, k.actions[a].value, k.actions[a].description);
                r.disabled = false;
                t.rows.push_back(r);
            }
            if (k.gyro && k.player < 2) {   // pad1: gyro steering (opt-in, input-changing; per player, saved with the controls)
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
        }
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
        // (after the actions: SETTINGS > SAVES > EXPORT SAVE keeps its place for the scripted checks)
        t.rows.push_back(choice(kRowSaveScreens, "SAVE SCREENS", {"Hide", "Show"}, s.hide_save_screens ? 0 : 1,
                                "Hide: the AUTOSAVE page after a race is skipped quickly and not shown, as the game's card "
                                "messages are. The save and the game are the same. Show: the page as on the PS2."));
        c.tabs.push_back(std::move(t));
    }
    if (!env.mod_rows.empty()) {   // sdk2b: the code mods' rows (only when one has added some: no tab otherwise)
        Tab t{"MODS", env.mod_rows};
        c.tabs.push_back(std::move(t));
    }
    for (const Tab& t : env.extra_tabs) c.tabs.push_back(t);   // tune4: the host's tabs (DEVELOPER)
    return c;
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

namespace {
void translate_into(Content& c, const Lookup& l, std::vector<std::string>* missing) {
    const auto tr = [&](std::string& s) { s = translate_with(l, s, missing); };
    for (Tab& t : c.tabs) {
        tr(t.label);
        for (Row& r : t.rows) {
            tr(r.label);
            for (std::string& v : r.values) tr(v);
            tr(r.level_text);
            tr(r.description);
        }
    }
    tr(c.popup);
    tr(c.title);
    tr(c.footer_keys);
    tr(c.footer_back);
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

bool language_edit(Settings& s, const Env& env, const Edit& e) {
    if (e.action || e.id != kRowLanguage) return false;
    s.language = e.value == 0 || e.value > env.languages.size() ? std::string() : env.languages[e.value - 1].first;
    return true;
}

bool apply_edit(Settings& s, const Edit& e) {
    const std::size_t v = e.value;
    switch (e.id) {
    case kRowPreset: if (v < std::size(kPresets)) s = with_preset(s, kPresets[v]); return true;
    case kRowFullscreen: s.fullscreen = v != 0; return true;
    case kRowVsync: s.vsync = v != 0; return true;
    case kRowFrameCap: if (v < std::size(kCaps)) s.frame_cap = kCaps[v]; return true;
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
    case kRowHd: s.hd_textures = v != 0; return true;
    case kRowCarDetail: s.car_detail_highest = v != 0; return true;
    case kRowCarReflect: s.car_reflections = int(std::min<std::size_t>(v, 2)); return true;   // reflect1
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
    case kRowFps: s.fps_view = int(std::min<std::size_t>(v, 3)); return true;   // fpsoverlay2
    case kRowFpsCorner: s.fps_corner = int(std::min<std::size_t>(v, 3)); return true;
    case kRowSkipHint: s.skip_hint = v != 0; return true;   // mac5
    case kRowSpeedUnits: s.speed_units = v ? 1 : 0; return true;   // units1
    case kRowDateFormat: s.date_format = v ? 1 : 0; return true;   // mac5
    case kRowPhotoRow: s.photo_row = v != 0; return true;   // photorow1
    case kRowLauncherAtStart: s.launcher_at_start = v != 0; return true;   // launchskip1
    case kRowPlaylistMode: s.playlist_mode = int(std::min<std::size_t>(v, 2)); return true;   // custom soundtrack
    case kRowOtherMusicApps: s.other_music_apps = v ? 1 : 0; return true;   // nowplaying1
    case kRowCustomMusicVolume: s.custom_music_volume = int(std::min<std::size_t>(v, 20)) * 10; return true;   // musicvol1
    case kRowFlashes: s.screen_flashes = v ? 1 : 0; return true;   // accessibility
    case kRowHudColour: s.hud_colour_filter = int(std::min<std::size_t>(v, 3)); return true;
    case kRowBoostMode: s.boost_mode = v ? 1 : 0; return true;
    case kRowHudSize: s.hud_size = int(std::min<std::size_t>(v, 3)); return true;   // textsize1
    case kRowGhost: s.ghost = v ? 1 : 0; return true;   // ghost1
    case kRowSpeedrunTimer: s.speedrun_timer = int(std::min<std::size_t>(v, 2)); return true;   // speedrun1
    case kRowInstantRestart: s.instant_restart = v ? 1 : 0; return true;
    case kRowRumble: s.rumble = v ? 1 : 0; return true;   // rumble1
    case kRowRumbleStrength: s.rumble_strength = int(std::min<std::size_t>(v, 9) + 1) * 10; return true;
    case kRowRumbleTriggers: s.rumble_triggers = v != 0; return true;
    case kRowAnalogPedals: s.analog_pedals = v != 0; return true;   // 1.0.1
    case kRowAdaptiveTriggers: s.adaptive_triggers = v ? 1 : 0; return true;   // pad1
    case kRowAdaptiveStrength: s.adaptive_triggers_strength = int(std::min<std::size_t>(v, 9) + 1) * 10; return true;
    case kRowSaveScreens: s.hide_save_screens = v == 0; return true;   // autosave1 (Hide, Show)
    case kRowSeriesShoulders: s.series_shoulders = v ? 1 : 0; return true;   // carsel1
    case kRowRecordReplays: s.record_replays = v ? 1 : 0; return true;   // replay1
    case kRowDeveloperMods: s.developer_mods = v ? 1 : 0; return true;   // tune4
    case kRowGlyphs: if (v < std::size(kPromptOrder)) s.button_prompts = kPromptOrder[v]; return true;   // mac5
    default: return false;
    }
}

}  // namespace b3settings
