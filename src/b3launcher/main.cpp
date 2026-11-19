// b3launcher / Burnout3Launcher.exe -- the setup program beside the game (remaster-features §8 "Launcher / setup
// program"): the game's PC settings, the HD pack folder, the music folder (the custom soundtrack), the game data folder,
// the save files, their stats, the replays and a controller test,
// usable without starting the game; then START GAME or START IN SAFE MODE (native_play beside it), and it closes.
//
// It edits only the files the game reads (core.h): pc_settings.ini through b3settings (the in-game SETTINGS page's
// own rows: b3settings::build_content / apply_edit), run_state.ini (safe mode), the saves folder (save_store.h), and
// its own launcher.ini (the game data folder). Mouse, keyboard and gamepad.
//
// It looks like the game's front end once the game data folder is known: the fonts, logo, grid and arc are read from
// the disc files at run time (gameart.h; load_look), the prompt bar's buttons come from src/b3replay/prompts.h. Without
// them (or with $B3LAUNCHER_PLAIN=1) it keeps its plain dark look.
//
// osk1: usable with a controller alone (a Steam Deck in Gaming Mode, a PC by the TV): a pad or a touch on a folder /
// file button opens the launcher's own folder list (Browser) instead of the system's dialog, and a text field gets
// the launcher's on-screen keyboard (core.h osk_layout; SDL's system keyboard too where there is one).
//
// update1: at each start (1.2.1; once a day before) (Settings > Launcher > CHECK FOR UPDATES, on by default) it asks GitHub in the background whether
// a newer release is out (update.h, update_net.h) and says so on the Play page and the Credits page; it never
// downloads anything. B3LAUNCHER_UPDATE_FILE=<file>: the answer read from that file instead (a test hook); a
// --screenshot run never makes a request.
//
//   b3launcher [--pref DIR] [--size WxH]          DIR: the user data folder (default SDL_GetPrefPath("b3port", "native"))
//   b3launcher --screenshot DIR [--pref DIR]      draws each page into DIR/*.png and exits (checks; offscreen driver)
//   b3launcher --launcher                         shows the launcher even when it is set to start the game at once
//
// launchskip1: with OPEN THE LAUNCHER AT START off (Settings::launcher_at_start; the Play page's "Start the game
// directly next time"), the launcher starts the game at once (START GAME, after a one-second "Starting" screen in which
// Shift or a controller's Back / Select opens the launcher instead), except as core.h skip_launcher lists.
#include "core.h"
#include "draw.h"
#include "font.h"
#include "gameart.h"
#include "showroom.h"
#include "showroom_view.h"
#include "spawn.h"
#include "update.h"       // update1: the update check (the Play page's banner, Settings > Launcher, Credits)
#include "update_net.h"
#include "oneclick.h"        // 1.1 one-click updates: the signed release's package
#include "installer.h"       // ... put in place of this one (the previous version kept)
#include "steam.h"           // issue #8: ADD TO STEAM (Settings > Launcher)
#include "steam_art.h"

#include "b3port/app_icon.h"   // the built-in window icon
#include "b3asset/discfs.h"   // patches3: the game folder's files a mod's patches apply to
#include "b3asset/language.h"   // 1.0.1: LANGUAGE, IMPORT LANGUAGES
#include "b3asset/hdpack.h"   // showhd1: the showroom's HD packs (the game's list) and its check's test pack
#include "b3asset/png.h"
#include "b3mod/archive.h"   // modkit1b: Add mod (a .zip / .b3mod)
#include "b3mod/mods.h"
#include "b3mod/codemod.h"   // sdk2b: code mods: trust, settings rows, the last start's report
#include "b3report/bug_report.h"
#include "b3report/run_logs.h"
#include "b3report/sysinfo.h"
#include "b3report/version.h"
#include "b3settings/page.h"
#include "b3settings/safemode.h"
#include "b3settings/settings.h"

#include "../native/input_keys_sdl.h"
#include "../native/now_playing.h"   // nowplaying1: the Music page's OTHER MUSIC APPS line
#include "../native/pcsx2_cards.h"   // 1.1: the save imports start in PCSX2's memory card folder
#include "../native/pad_triggers.h"
#include "../native/replay_file.h"
#include "../native/save_stats.h"
#include "../native/save_store.h"
#include "../b3replay/glyphdraw.h"
#include "../b3replay/prompts.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __APPLE__
#include <cstdint>
#include <mach-o/dyld.h>   // macpkg1: _NSGetExecutablePath (the launcher's own folder inside the .app)
#endif


#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace b3launcher;
namespace D = b3launcher::draw;

namespace {

// ---- theme ----
// The plain look (no game data yet), and the game's front end once its art is loaded (game_theme: the colours of the
// in-game PC SETTINGS page, b3settings/page.cpp kText / kTextDim / kAccent / kPanel / kBoxEdge, after the menus)
namespace col {
SDL_FColor bg = D::rgba(0x0D0F13), side = D::rgba(0x13161C), panel = D::rgba(0x191D25), panel2 = D::rgba(0x222834),
           panel3 = D::rgba(0x2B3240), line = D::rgba(0x2A303B), text = D::rgba(0xEDEFF3), dim = D::rgba(0xA0A8B6),
           faint = D::rgba(0x6E7686), accent = D::rgba(0xFF7A1A), accent_hi = D::rgba(0xFF9A4A),
           ok = D::rgba(0x4CC38A), warn = D::rgba(0xF2B544), bad = D::rgba(0xF0605D), ink = D::rgba(0x15110D),
           edge = D::rgba(0x2A303B, 0.0f);
void game_theme() {
    bg = D::rgba(0x040A17);
    side = D::rgba(0x020713, 0.55f);
    panel = D::rgba(0x0E1C3A, 0.82f);
    panel2 = D::rgba(0x18305E, 0.88f);
    panel3 = D::rgba(0x21437F, 0.92f);
    line = D::rgba(0x4674BA, 0.55f);
    text = D::rgba(0xECF4FF);
    dim = D::rgba(0xAAC9F4);
    faint = D::rgba(0x7088B0);
    accent = D::rgba(0x5CB6FF);
    accent_hi = D::rgba(0x9AD4FF);
    ink = D::rgba(0x04101F);
    ok = D::rgba(0x58D49A);
    warn = D::rgba(0xF4C24E);
    bad = D::rgba(0xFF6A5E);
    edge = D::rgba(0x68A0E2, 0.75f);
}
}

constexpr float kSideW = 268, kHeaderH = 92, kFooterH = 108, kPadX = 36;
constexpr float kBody = 16, kSmall = 13.5f, kTitle = 27, kSection = 12.5f;

enum Page { kPlay, kSettings, kHd, kMusic, kMods, kGame, kSaves, kStats, kShowroom, kReplays, kPads, kCredits, kPageCount };
const char* const kPageNames[kPageCount] = {"Play", "Settings", "HD Textures", "Music", "Mods", "Game Data", "Saves", "Stats", "Showroom", "Replays", "Controllers", "Credits"};
const char* const kPageSubtitles[kPageCount] = {
    "Start the game, or start it in safe mode with its settings turned down.",
    "The game's own PC settings: the same rows as SETTINGS in the game, saved to the same file.",
    "A PCSX2 texture replacement folder for Burnout 3, used from the next start.",
    "Your own music on the game's radio: the same settings as SETTINGS > INTERFACE > MUSIC in the game.",
    "Mods in the mods folder: which are on, and their order. Your disc and its files are never changed.",
    "Your Burnout 3 disc (NTSC-U, SLUS-21050): its image, or a folder with its contents.",
    "The save files and the one the game uses. The game saves by itself.",
    "Each profile's stats as the game saved them, and local achievements worked out from them.",
    "Every player car on your disc in 3D on a turntable, with the game's name for it.",
    "Your recorded sessions, played back exactly as they were played (SETTINGS > INTERFACE > RECORD REPLAYS).",
    "Connected controllers, live, and the keyboard as the game maps it.",
    "Who made this port, and the people and projects it stands on.",
};

enum class Nav { Up, Down, Left, Right, Confirm, Back, PrevPage, NextPage, PrevTab, NextTab };
constexpr double kPadTestHold = 0.8;   // seconds of B / Circle held that stop the controller button test

struct Widget {
    SDL_FRect r{};
    std::function<void()> activate;
    std::function<void(int)> change;                 // left / right on a value
    std::function<void(float, float)> click;          // a mouse click at (x, y) (else activate)
    std::function<void()> alt;                        // a right click (what left / right do, for the mouse)
    std::function<void()> reset;                      // rebind2: Delete, or X / Square on a controller: this one back to its default
    std::string desc;
    // launcherux1: Up / Down's target when it is not the nearest widget above / below (-1: the nearest). Ui::chain sets
    // them on a group that reads as one list: value widgets side by side (left / right change them, so only up /
    // down can move between them) and a list drawn in columns.
    int up = -1, down = -1;
};

// 1.1: kSavesFolder   // 1.0.1: kLanguageImage   // iso1: kGameImage; modkit1b: kModArchive
enum class DialogKind { kHdFolder, kGameFolder, kExport, kImportFile, kImportFolder, kGameImage, kMusicFolder, kModArchive, kLanguageImage, kSavesFolder,
                        kSteamCover, kSteamHero, kSteamLogo };   // issue #8: ADD TO STEAM's own pictures

struct Modal {
    std::string title, text;
    struct Button {
        std::string label;
        std::function<void()> fn;
        bool danger = false;
    };
    std::vector<Button> buttons;
    std::size_t focus = 0;
    bool input = false;                 // a text field (a typed path)
    TextEdit field;                     // osk1: its text and cursor
    std::function<void(const std::string&)> on_text;
    std::vector<SDL_FRect> rects;       // the buttons as last drawn
    // osk1: the launcher's on-screen keyboard under the field (opened with a controller or the touch screen, or by
    // the keyboard mark in the field), its keys and state, and what was drawn (the mouse)
    bool osk = false;
    std::vector<OskKey> keys;
    OskState osk_st;
    std::vector<SDL_FRect> key_rects;
    SDL_FRect field_rect{}, osk_toggle{};
    std::size_t view = 0;               // the field's first character shown (a long path scrolls with the cursor)
};

// osk1: the launcher's own folder list (a controller or the touch screen; no system file dialog there)
struct Browser {
    DialogKind kind{};
    std::string title, what;            // what: "folder", "disc image", "file", ...
    std::vector<std::string> exts;      // the files listed (lower case, no dot)
    bool pick_files = false;            // A on a listed file chooses it
    bool pick_folder = true;            // Start chooses the folder shown
    std::string export_name;            // kExport: the file written into the chosen folder
    fs::path dir;
    BrowseList list;
    std::vector<BrowsePlace> places;
    int column = 1;                     // 0: the places, 1: the folder's list
    int sel = 0, place_sel = 0, first = 0, rows_shown = 1;
    bool follow = true;                 // the list scrolls to the highlighted row (the wheel scrolls on its own)
    std::string note;                   // the last action's result (a folder that cannot be opened, ...)
    std::string status;                 // what is in the folder shown (the game's files, songs, images)
    int status_level = 0;               // 0 neutral, 1 good, 2 a problem
    std::vector<SDL_FRect> row_rects, place_rects;   // as drawn (the mouse)
    SDL_FRect list_rect{}, up_r{}, type_r{}, cancel_r{}, choose_r{};
};

struct DialogResult {
    DialogKind kind;
    bool ok = false;        // a path chosen
    bool failed = false;    // no dialog on this system
    std::string path, error;
};

struct App;
App* g_app = nullptr;

struct App {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    Text* text = nullptr;
    float scale = 1, W = 1280, H = 720;

    fs::path exe_dir, pref, cwd;
    SDL_Texture* icon = nullptr;   // the B3 icon (assets/b3-icon.png beside the launcher): the header and the window icon
    Config cfg;
    b3settings::Settings settings;
    std::string settings_note;   // the last save's result
    fs::path game_dir;
    GameCheck game;
    std::string game_note;
    HdCheck hd;
    std::string hd_checked;      // the folder `hd` is of
    unsigned hd_gen = 0;         // showhd1: +1 each time it is scanned again (the showroom indexes its packs again)
    std::string hd_note;
    MusicCheck music;            // the music folder's songs (the game's scan)
    std::string music_checked;   // the folder `music` is of
    std::string music_note;      // the last choice's result, in the in-game page's words
    // 1.0.1 LANGUAGE (b3asset/language.h): the languages imported into <pref>/languages (code, name), the last import's result
    std::vector<std::pair<std::string, std::string>> languages;
    std::string language_note;
    // nowplaying1: OTHER MUSIC APPS' reader for the Music page's "Detected" line (made the first time the page shows
    // with the option on; it reads only while that page shows and the option is on)
    std::unique_ptr<b3nowplaying::Poller> now_playing;
    // modkit1: the Mods page (include/b3mod/mods.h): what is installed, and mods.ini's order and switches
    std::vector<b3mod::Mod> mods;
    b3mod::Order mod_order;      // merged with what is installed (new mods at the end, off)
    std::string mods_note;
    bool mods_note_bad = false;
    // modkit1b: what the next start would run with (select: [requires]), its overlay (no file reads) and the paths
    // more than one of those mods has
    b3mod::Selection mod_sel;
    b3mod::Overlay mod_overlay;
    std::vector<b3mod::FileConflict> mod_conflicts;
    // sdk2b: code mods -- what the last start did with each (mods-state.ini) and their settings rows (as the last start
    // saw them, with the values of mod-settings/<id>.ini)
    std::vector<b3mod::ModState> mod_state;
    std::map<std::string, std::vector<b3mod::SettingRow>> mod_rows;
    std::vector<SaveSlot> slots;
    std::string save_note;
    bool save_note_bad = false;
    // 1.1: the user's folders PCSX2's memory card folders are looked for in (pcsx2_cards.h); set: the screenshot check's
    // stand-ins instead of this computer's
    std::optional<b3native::save::Pcsx2Homes> pcsx2_homes;
    // the STATS page (save_stats.h): every profile in use in the save files, read only
    struct StatsEntry {
        int slot = 1;            // the file (profile-N.sav)
        bool active = false;     // the file in use
        b3native::save::ProfileStats p;
    };
    std::vector<StatsEntry> stats;
    std::vector<std::string> stats_notes;   // files that could not be read
    std::size_t stats_sel = 0;
    // the REPLAYS page (src/native/replay_file.h): the replay files in <pref>/replays, read when the page opens
    std::vector<b3native::replay::Entry> replays;
    std::size_t replay_sel = 0;
    std::string replay_note;
    bool replay_note_bad = false;
    b3settings::RunState run;
    bool have_run = false;

    // UI state
    int page = kPlay;
    bool in_content = false;
    int focus = 0;
    float scroll[kPageCount] = {};
    bool reveal_focus = true;   // scroll the focused widget into view (keyboard / pad); the wheel turns it off
    bool mouse_on = false;      // the mouse was the last input: hover marks and its description show (keys / pad: the focus only)
    std::size_t settings_tab = 0;
    std::vector<Widget> widgets, side_widgets;
    std::optional<Modal> modal;
    std::optional<Browser> browser;   // osk1: the launcher's folder list (under a modal: its text field)
    float mouse_x = -1, mouse_y = -1;
    bool pad_used_last = false;
    bool touch_last = false;          // osk1: the last click was a touch on the screen (a Steam Deck's): as a pad
    SDL_Rect viewport{};
    float content_h = 0;

    // controllers
    std::vector<SDL_Gamepad*> pads;
    std::size_t pad_sel = 0;
    bool pad_test = false;
    double east_held_since = -1;   // the button test: B / Circle held since (kPadTestHold stops it)
    b3native::input::KeyPad keys;
    std::string rumble_note;
    // remap1: the control bindings (controls.ini, src/native/input_bindings.h), edited on the Controllers page
    b3native::input::Controls controls = b3native::input::default_controls();
    std::string controls_note;
    std::size_t ctl_player = 0, ctl_device = 0, ctl_view = 0;   // the editor's player, device, keyboard view (driving / menus)
    b3native::input::RebindFlow flow;
    double flow_t = 0;                                          // the flow's clock (its timeout)
    std::vector<std::pair<SDL_JoystickID, std::array<bool, 10>>> flow_rest;   // per pad: each analog input seen at rest
    int ctl_first_widget = -1;                                  // the first action row's widget (screenshots)
    // pad1: gyro steering's live readout (the selected pad's sensors, on only while this page shows it with the player's
    // gyro on) and the DualSense trigger test (both put back when done and at the end)
    b3native::pad::GyroSteer gyro;
    SDL_JoystickID gyro_pad = 0;
    bool gyro_recentre_held = false;
    SDL_JoystickID trig_test_pad = 0;
    double trig_test_until = 0;
    std::string trig_note;

    // the gamepad's held directions (hold repeat)
    struct Hold {
        bool down = false;
        double next = 0;
    } hold[4];   // up, down, left, right
    Hold erase;  // osk1: B / Circle held on the on-screen keyboard: Backspace repeats
    bool trig_l = false, trig_r = false;

    // dialogs (their callbacks may come on another thread)
    std::mutex dlg_m;
    std::vector<DialogResult> dlg_results;
    int dialogs_open = 0;   // system file dialogs open: a pad does not drive the launcher behind them
    std::string dialog_start;   // osk1: the last one's starting point (the launcher's list starts there when it failed)

    // the game started
    std::optional<Child> child;
    double child_started = 0;
    // launchskip1: the start without the launcher (core.h skip_launcher). skip_at > 0: the "Starting" screen shows and
    // the game starts at that time unless Shift / Back / Select is held first; skipping: it was started that way
    double skip_at = 0;
    bool skipping = false;
    SkipBlock skip_block = SkipBlock::kSetting;   // why the launcher shows (a line on the Play page)
    bool skip_no_spawn = false;                   // a check: the start is counted, not made (B3LAUNCHER_SHOT_START=1 makes it)
    int skip_starts = 0;
    fs::path run_log;   // its run log (native_play --log)

    // test builds: the bug report (the Play page)
    bool report_save = false;   // "include my save": the active profile-N.sav goes in
    fs::path report_dir;        // where the zip goes; empty: Desktop, else Documents, else the user data folder
    bool report_open = true;    // open the folder afterwards (not in a screenshot check)
    bool quit = false;

    // update1: the update check (update.h): one request on a thread of its own, its answer into cfg.update (launcher.ini)
    struct UpdateJob {
        std::mutex m;
        bool done = false;
        update::Response r;
    };
    struct Upd {
        std::shared_ptr<UpdateJob> job;   // a request under way
        std::thread th;
        std::string file;                 // B3LAUNCHER_UPDATE_FILE / the checks: the answer read from this file, no request
        bool net = true;                  // false (--screenshot): never a request, whatever is asked
    } upd;
    // 1.1 one-click updates (oneclick.h, installer.h): the latest release fetched, checked and installed on a thread
    struct InstallJob {
        std::mutex m;
        int step = 0;                     // 0 asking GitHub, 1 the signed list, 2 downloading, 3 checking, 4 writing, 5 installing
        bool done = false, ok = false;
        std::string why, tag;
        fs::path package;                 // macOS: the checked dmg, opened for the player
    };
    struct Inst {
        std::shared_ptr<InstallJob> job;
        std::thread th;
        bool installed = false;           // the files are swapped: the next start is the new version
        std::string tag, note;            // the version installed; the last attempt's result in words
    } inst;
    fs::path self_exe;                    // this launcher's own file, found at start (a swap moves the running one)
    // issue #8: ADD TO STEAM (steam.h). A press waits for the next frame, so that "Adding..." shows while it works
    struct SteamUi {
        bool pending = false;
        std::string note;                     // the last attempt in words (or a picture chosen)
        int level = 0;                        // 0 a hint, 1 done, 2 done with a warning, 3 not done
        int widget = -1;                      // the Add to Steam button as last drawn (the screenshot check)
        bool checks = false;                  // --screenshot: Steam's own folders are never written, only `places`
        std::optional<steam::Places> places;  // the checks: a made-up home instead of this computer's
        std::optional<bool> running;          // the checks: Steam running or not, instead of the process list
    } steam_ui;

    // the game's front-end look (gameart.h), from the game data folder; off: the plain look
    struct Look {
        bool on = false;
        std::string loaded_for;                       // the folder it was read from
        GameArt art;
        std::map<std::string, SDL_Texture*> tex;      // by texture name
        SDL_Texture* font_tex[3] = {};
        std::map<std::tuple<int, int, int>, std::pair<SDL_Texture*, float>> prompts;   // (device, button, px) -> glyph, width px
    } look;

    // the SHOWROOM page (photo2): the cars of the game data, the 3D view (made on the first visit) and its picture
    struct Show {
        std::vector<showroom::CarEntry> cars;
        std::string listed_for, note, error;
        std::size_t sel = 0, loaded_index = std::size_t(-1);
        int paint = 0, loaded_paint = -1;
        showroom::Turntable tt;
        std::unique_ptr<showroom::View> view;
        std::vector<std::uint8_t> pixels;
        SDL_Texture* tex = nullptr;
        int tex_w = 0, tex_h = 0;
        SDL_FRect rect{};
        bool drag = false;
        double last_t = 0;
    } show;

    double now() const { return double(SDL_GetTicksNS()) / 1e9; }
};

// ---- files ----

void save_settings(App& a);

// 1.1: a Steam Deck (Steam's SteamDeck=1, or the board's DMI name: Jupiter = LCD, Galileo = OLED)
bool on_steam_deck() {
    if (const char* e = std::getenv("SteamDeck"); e && e[0] == '1') return true;
#if defined(__linux__)
    std::ifstream f("/sys/devices/virtual/dmi/id/product_name");
    std::string name;
    if (f && std::getline(f, name)) return name == "Jupiter" || name == "Galileo";
#endif
    return false;
}

void load_settings(App& a) {
    a.settings = b3settings::Settings{};
    std::string why;
    std::error_code ec;
    const fs::path file = settings_file(a.pref);
    if (!fs::exists(file, ec) && on_steam_deck()) {
        // 1.1: the first start on a Steam Deck: MEDIUM (MSAA 2x, the PS2-size post, half the particles) and vsync, which
        // hold its 60 Hz screen where HIGH ran at about 55; saved once, so every later change is the player's own
        a.settings = b3settings::with_preset(a.settings, b3settings::Preset::kMedium);
        a.settings.vsync = true;
        save_settings(a);
        a.settings_note = "Steam Deck: started from the MEDIUM preset with VSYNC on (change them in Settings).";
        return;
    }
    if (b3settings::load_file(utf8_of(file), a.settings, &why) && !why.empty()) a.settings_note = "Read with notes: " + why;
}

void save_settings(App& a) {
    std::string why;
    if (b3settings::save_file(utf8_of(settings_file(a.pref)), a.settings, &why)) a.settings_note = "Saved to " + utf8_of(settings_file(a.pref));
    else a.settings_note = "Cannot save: " + why;
}

void refresh_hd(App& a, bool force = false) {
    if (!force && a.hd_checked == a.settings.hd_pack_dir) return;
    a.hd_checked = a.settings.hd_pack_dir;
    a.hd = a.settings.hd_pack_dir.empty() ? HdCheck{} : check_hd_folder(path_from_utf8(a.settings.hd_pack_dir));
    ++a.hd_gen;
}

// The music folder's songs (custom_music.h's scan: the files native_play plays). Again when the folder changes, and
// each time the Music page is opened (songs added since).
void refresh_music(App& a, bool force = false) {
    if (!force && a.music_checked == a.settings.music_folder) return;
    a.music_checked = a.settings.music_folder;
    a.music = a.settings.music_folder.empty() ? MusicCheck{} : check_music_folder(path_from_utf8(a.settings.music_folder));
}

// 1.1: the saves folder in use: the one chosen on the Saves page (launcher.ini saves_dir), else the default
fs::path saves_root(const App& a) { return saves_folder(a.pref, a.cfg.saves_dir); }

// The stats of every profile in the save files (read only: nothing is written)
void load_stats(App& a) {
    a.stats.clear();
    a.stats_notes.clear();
    const fs::path root = saves_root(a);
    for (const SaveSlot& s : a.slots) {
        if (!s.sav && !s.legacy) continue;
        std::string err;
        const auto loaded = b3native::save::load_profile(root, s.slot, &err);
        std::optional<std::vector<b3native::save::ProfileStats>> st;
        if (loaded && loaded->source != b3native::save::ProfileSource::kNone) st = b3native::save::decode_game_save(loaded->dirs, &err);
        else if (loaded) continue;
        if (!st) {
            a.stats_notes.push_back("File " + std::to_string(s.slot) + ": " + err);
            continue;
        }
        for (b3native::save::ProfileStats& p : *st) a.stats.push_back({s.slot, s.active, std::move(p)});
    }
    if (a.stats_sel >= a.stats.size()) a.stats_sel = 0;
}

// modkit1: the installed mods and their order (mods.ini merged with the folder: a mod removed drops out, a new one
// joins at the end, off). Written back only when the user changes something.
void load_look(App& a);

// modkit1b: the set the next start runs with (as native_play: the enabled ids, then [requires]), its files and conflicts
void update_mod_set(App& a) {
    a.mod_sel = b3mod::select(a.mods, b3mod::enabled_ids(a.mod_order));
    // patches3: the mods' patches are applied to the game folder's files as the game will (a patch that does not fit
    // this disc: the card says so), so the art below shows them too; without a game folder they are listed only
    b3mod::PatchEnv penv;
    if (a.game.ok) {
        const fs::path root = a.game_dir;
        penv.cache = b3mod::patch_cache(b3mod::mods_folder(a.pref));
        penv.read_disc = [root](const std::string& key, std::vector<std::uint8_t>& out) {
            const fs::path p = b3asset::disc::resolve_ci(root, key);
            return !p.empty() && b3asset::disc::read_file(p, out);
        };
    }
    a.mod_overlay = b3mod::Overlay::build(a.mod_sel.mods, false, a.game.ok ? &penv : nullptr);
    a.mod_sel.errors.insert(a.mod_sel.errors.end(), a.mod_overlay.errors().begin(), a.mod_overlay.errors().end());
    a.mod_conflicts = b3mod::conflicts(a.mod_overlay);
}

void refresh_mods(App& a) {
    a.mods = b3mod::discover(b3mod::mods_folder(a.pref));
    b3mod::Order saved;
    b3mod::load_order(b3mod::order_file(a.pref), saved);
    a.mod_order = b3mod::merged_order(saved, a.mods);
    update_mod_set(a);
    // sdk2b: the last start's report and the code mods' rows with the values saved since
    a.mod_state = b3mod::load_state(a.pref);
    a.mod_rows.clear();
    for (const b3mod::ModState& st : a.mod_state) {
        std::vector<b3mod::SettingRow> rows = st.rows;
        const auto saved_values = b3mod::load_values(b3mod::settings_file(a.pref, st.id));
        for (b3mod::SettingRow& r : rows)
            if (const auto it = saved_values.find(r.key); it != saved_values.end())
                if (const int v = b3mod::value_from_text(r, it->second); v >= 0) r.value = v;
        if (!rows.empty()) a.mod_rows[st.id] = std::move(rows);
    }
}

// sdk2b: a code mod's library for this system (empty: none) and whether the user allowed it to run (DESIGN.md §2.5)
fs::path code_library(const b3mod::Mod& m) {
    const std::string& rel = m.manifest.code.path_for(b3mod::host_system());
    return m.manifest.code.present && !rel.empty() ? m.dir / b3mod::path_from_utf8(rel) : fs::path();
}
bool code_allowed(const App& a, const b3mod::Mod& m) {
    const fs::path lib = code_library(m);
    std::error_code ec;
    return lib.empty() || !fs::exists(lib, ec) || b3mod::trusted(a.pref, b3mod::library_digest(lib));
}
const b3mod::ModState* code_state(const App& a, const std::string& id) {
    for (const b3mod::ModState& s : a.mod_state)
        if (s.id == id) return &s;
    return nullptr;
}

void save_mods(App& a, const std::string& what) {
    std::string why;
    a.mods_note_bad = false;
    if (b3mod::save_order(b3mod::order_file(a.pref), a.mod_order, &why)) a.mods_note = what + " Used from the next start.";
    else { a.mods_note = "Cannot save the mods' order: " + why; a.mods_note_bad = true; }
    update_mod_set(a);
    load_look(a);   // a mod may change the art the launcher shows (DATA/GLOBAL.TXD, DATA/FRONTEND.TXD)
}

void message(App& a, const std::string& title, const std::string& text);

// modkit1b: Add mod -- a .zip / .b3mod unpacked into the mods folder (b3mod/archive.h); a mod already installed with
// that id asks first
void add_mod(App& a, const fs::path& archive, bool replace) {
    b3mod::InstallOptions opt;
    opt.replace = replace;
    const b3mod::InstallResult r = b3mod::install_archive(archive, b3mod::mods_folder(a.pref), opt);
    if (!r.ok && r.exists && !replace) {
        Modal m;
        m.title = "Replace " + r.id + "?";
        m.text = r.errors.front() + ". Replace it with version " + r.version + " from " + utf8_of(archive.filename()) +
                 "? Its folder is replaced; its place in the list and its on / off switch stay.";
        m.buttons = {{"Replace", [&a, archive]() { add_mod(a, archive, true); }, true}, {"Cancel", nullptr}};
        a.modal = m;
        return;
    }
    refresh_mods(a);
    if (!r.ok) {
        std::string why;
        for (std::size_t k = 0; k < r.errors.size() && k < 6; ++k) why += (k ? "\n" : "") + r.errors[k];
        if (r.errors.size() > 6) why += "\n... and " + std::to_string(r.errors.size() - 6) + " more";
        a.mods_note = "Not added (" + utf8_of(archive.filename()) + "): " + r.errors.front();
        a.mods_note_bad = true;
        message(a, "Mod not added", utf8_of(archive.filename()) + " was not installed; nothing was written.\n" + why);
        return;
    }
    a.mods_note = std::string(r.replaced ? "Replaced " : "Added ") + r.name + " (" + r.id + " " + r.version + ", " + std::to_string(r.files) +
                  (r.files == 1 ? " file" : " files") + ")." + (r.replaced ? "" : " It is off: turn it on to use it.");
    a.mods_note_bad = false;
    load_look(a);
}

// The replay files (read when the Replays page opens and after a delete: each file is read whole for its header)
void load_replays(App& a) {
    a.replays = b3native::replay::list(b3native::replay::replays_dir(a.pref));
    if (a.replay_sel >= a.replays.size()) a.replay_sel = 0;
}

void refresh_languages(App& a);   // 1.0.1 (the SETTINGS page)

void refresh(App& a) {
    a.game = check_game_folder(a.game_dir);
    a.slots = list_save_slots(saves_root(a));
    load_stats(a);
    std::error_code ec;
    a.have_run = fs::exists(run_state_file(a.pref), ec);
    a.run = b3settings::load_run_state(utf8_of(run_state_file(a.pref)));
    refresh_hd(a);
    refresh_music(a);
    refresh_languages(a);   // 1.0.1
    refresh_mods(a);
}

void set_game_dir(App& a, const fs::path& p, bool remember) {
    a.game_dir = p;
    a.game = check_game_folder(p);
    if (!a.game.ok && !a.game.suggestion.empty()) {
        a.game_note = "Using " + utf8_of(a.game.suggestion) + " (the game's files are in it).";
        a.game_dir = a.game.suggestion;
        a.game = check_game_folder(a.game_dir);
    }
    if (remember) {
        a.cfg.game_dir = utf8_of(a.game_dir);
        std::string why;
        if (!save_config(config_file(a.pref), a.cfg, &why)) a.game_note = "Cannot remember the folder: " + why;
    }
}

// ---- update1: the update check (update.h) ----

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
bool update_checking(const App& a) { return a.upd.job != nullptr; }
update::State update_state(const App& a) { return update::state(a.cfg.update, b3port::version(), update_checking(a)); }

// Asks for the latest release in the background (nothing when the check is off or one is under way). The answer comes
// from the test hook's file when there is one, else from GitHub (update_net.h) unless this run makes no requests.
void start_update_check(App& a) {
    if (a.upd.job || !a.cfg.update.enabled) return;
    auto job = std::make_shared<App::UpdateJob>();
    const std::string file = a.upd.file, ua = update::user_agent(b3port::version());
    const bool net = a.upd.net;
    a.upd.job = job;
    a.upd.th = std::thread([job, file, net, ua]() {
        update::Response r;
        if (!file.empty()) {
            std::ifstream f(path_from_utf8(file), std::ios::binary);
            std::ostringstream ss;
            if (f && (ss << f.rdbuf())) r = update::response_from_file_text(ss.str());
            else r.error = "no answer file";
        } else if (net) {
            r = update::https_get(update::kLatestUrl, ua);
        } else {
            r.error = "no requests in a check run";
        }
        std::lock_guard<std::mutex> l(job->m);
        job->r = std::move(r);
        job->done = true;
    });
}

// The answer, once it is there: into launcher.ini. True when one came.
bool poll_update(App& a) {
    if (!a.upd.job) return false;
    update::Response r;
    {
        std::lock_guard<std::mutex> l(a.upd.job->m);
        if (!a.upd.job->done) return false;
        r = std::move(a.upd.job->r);
    }
    if (a.upd.th.joinable()) a.upd.th.join();
    a.upd.job.reset();
    if (!a.cfg.update.enabled) return true;   // turned off meanwhile: the answer is dropped
    update::apply(a.cfg.update, r, unix_now());
    save_config(config_file(a.pref), a.cfg);
    return true;
}

// ---- 1.1 one-click updates ----

bool oneclick_available(const App& a) {
    std::array<std::uint8_t, 32> key{};
    return a.upd.net && oneclick::release_key(key) && !a.self_exe.empty();
}

// Asks GitHub for the latest release, fetches this system's package checked against the signed hash list
// (oneclick.h), and installs it beside the launcher (installer.h; macOS: the dmg is opened for the player).
void start_install(App& a) {
    if (a.inst.job || a.inst.installed || !oneclick_available(a)) return;
    auto job = std::make_shared<App::InstallJob>();
    const std::string ua = update::user_agent(b3port::version());
    const fs::path dir = a.pref / "updates", install_dir = a.exe_dir;
    a.inst.job = job;
    a.inst.note.clear();
    a.inst.th = std::thread([job, ua, dir, install_dir]() {
        const auto set_step = [&](int s) { std::lock_guard<std::mutex> l(job->m); job->step = s; };
        const auto finish = [&](bool ok, std::string why) {
            std::lock_guard<std::mutex> l(job->m);
            job->ok = ok;
            job->why = std::move(why);
            job->done = true;
        };
        const update::Response latest = update::https_get(update::kLatestUrl, ua);
        update::Release rel;
        std::string why;
        if (!latest.ok || latest.status != 200 || !update::parse_release(latest.body, rel, &why))
            return finish(false, "could not read the latest release" + (latest.ok ? std::string() : " (" + latest.error + ")"));
        if (!update::is_newer(rel.tag, b3port::version())) return finish(false, "this is already the latest version");
        {
            std::lock_guard<std::mutex> l(job->m);
            job->tag = rel.tag;
        }
        std::array<std::uint8_t, 32> key{};
        if (!oneclick::release_key(key)) return finish(false, "one-click updates are off in this build");
        const oneclick::Fetch fetch = [ua](const std::string& url, bool download, std::size_t max) {
            return update::https_get(url, ua, download ? 30 * 60 * 1000 : 15000, max, download);
        };
        const oneclick::Result got = oneclick::fetch_package(rel, update::this_platform(), key, fetch, dir,
                                                             [&](oneclick::Step s) { set_step(1 + int(s)); });
        if (!got.ok) return finish(false, got.why);
        if (update::this_platform() == update::Platform::kMac) {
            std::lock_guard<std::mutex> l(job->m);
            job->package = got.package;
            job->ok = job->done = true;
            return;
        }
        set_step(5);
        const installer::Result in = installer::install(got.package, install_dir);
        std::error_code ec;
        fs::remove(got.package, ec);
        finish(in.ok, in.why);
    });
}

// The result, once it is there. True when one came.
bool poll_install(App& a) {
    if (!a.inst.job) return false;
    bool ok;
    std::string why, tag;
    fs::path package;
    {
        std::lock_guard<std::mutex> l(a.inst.job->m);
        if (!a.inst.job->done) return false;
        ok = a.inst.job->ok;
        why = a.inst.job->why;
        tag = a.inst.job->tag;
        package = a.inst.job->package;
    }
    if (a.inst.th.joinable()) a.inst.th.join();
    a.inst.job.reset();
    if (!ok) {
        a.inst.note = "Update not installed: " + why + ".";
        return true;
    }
    a.inst.tag = tag;
    if (!package.empty()) {   // macOS: the checked disk image, opened; the player drags the app as on the first install
        const std::string url = "file://" + utf8_of(package);
        SDL_OpenURL(url.c_str());
        a.inst.note = "The update " + tag + " is downloaded and checked: its disk image is open. Quit, then drag the app to Applications.";
        return true;
    }
    a.inst.installed = true;
    a.inst.note = "Update " + tag + " installed. Restart the launcher to use it (your saves and settings stay as they are).";
    return true;
}

// Starts the launcher file that is now in place (the new version) and quits this one
void restart_launcher(App& a) {
    std::string err;
    if (auto c = spawn(a.self_exe, {}, a.exe_dir, {}, &err)) {
        release(*c);
        a.quit = true;
    } else {
        a.inst.note = "Could not restart the launcher (" + err + "): close it and start it again.";
    }
}

// At the end: a request still under way is left to finish on its own (its timeout bounds it; quitting does not wait)
void end_update_check(App& a) {
    if (!a.upd.th.joinable()) return;
    bool done = false;
    {
        std::lock_guard<std::mutex> l(a.upd.job->m);
        done = a.upd.job->done;
    }
    if (done) a.upd.th.join();
    else a.upd.th.detach();
}

void set_update_check(App& a, bool on) {
    a.cfg.update.enabled = on;
    save_config(config_file(a.pref), a.cfg);
    if (on && update::due(a.cfg.update, unix_now())) start_update_check(a);
}

// ---- the game's look ----

SDL_Texture* make_texture(SDL_Renderer* r, const ArtImage& im) {
    if (im.w <= 0 || im.h <= 0 || im.rgba.size() < std::size_t(im.w) * std::size_t(im.h) * 4) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, im.w, im.h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, im.rgba.data(), im.w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

// mac5 (2026-11-10: the launcher's font looked rough): the game's font pictures are small (made for a
// 640 x 448 screen), so drawn 2-4x larger with linear filtering their edges went soft. As the game's renderer sharpens
// its magnified fonts (hud2's coverage cut), the picture is upsampled 4x here and its alpha pushed through a narrow
// ramp around the stroke's half coverage: crisp edges up to 4x, still antialiased (the ramp spans ~1 upsampled texel).
ArtImage sharpened_font(const ArtImage& im, int k = 4, float cut = 0.45f) {
    if (im.w <= 0 || im.h <= 0 || im.rgba.size() < std::size_t(im.w) * std::size_t(im.h) * 4) return im;
    ArtImage o = im;
    o.w = im.w * k;
    o.h = im.h * k;
    o.rgba.assign(std::size_t(o.w) * std::size_t(o.h) * 4, 0);
    const auto at = [&](int x, int y, int c) {
        x = std::clamp(x, 0, im.w - 1);
        y = std::clamp(y, 0, im.h - 1);
        return float(im.rgba[(std::size_t(y) * std::size_t(im.w) + std::size_t(x)) * 4 + std::size_t(c)]);
    };
    const float ramp = 0.5f / float(k);   // half the ramp's width, in coverage
    for (int y = 0; y < o.h; ++y) {
        const float fy = (float(y) + 0.5f) / float(k) - 0.5f;
        const int y0 = int(std::floor(fy));
        const float ty = fy - float(y0);
        for (int x = 0; x < o.w; ++x) {
            const float fx = (float(x) + 0.5f) / float(k) - 0.5f;
            const int x0 = int(std::floor(fx));
            const float tx = fx - float(x0);
            std::uint8_t* d = &o.rgba[(std::size_t(y) * std::size_t(o.w) + std::size_t(x)) * 4];
            for (int c = 0; c < 4; ++c) {
                const float a = at(x0, y0, c) * (1 - tx) * (1 - ty) + at(x0 + 1, y0, c) * tx * (1 - ty) + at(x0, y0 + 1, c) * (1 - tx) * ty +
                                at(x0 + 1, y0 + 1, c) * tx * ty;
                float v = a;
                if (c == 3) v = 255.0f * std::clamp((a / 255.0f - (cut - ramp)) / (2 * ramp), 0.0f, 1.0f);
                d[c] = std::uint8_t(std::clamp(v + 0.5f, 0.0f, 255.0f));
            }
        }
    }
    return o;
}

// The game's fonts and front-end textures from the game data folder (read at run time: nothing of the game's is in the
// launcher); the plain look stays when they are not there. $B3LAUNCHER_PLAIN=1 keeps the plain look.
void load_look(App& a) {
    // modkit1b: the mods that are on answer for the art's files, as in the game (their set is part of what was read)
    std::string key = utf8_of(a.game_dir);
    for (const b3mod::ActiveMod& m : a.mod_overlay.mods()) key += "|" + m.id + " " + m.version;
    if (!a.ren || !a.game.ok || a.look.loaded_for == key) return;
    if (const char* e = std::getenv("B3LAUNCHER_PLAIN"); e && *e == '1') return;
    a.look.loaded_for = key;
    GameArt art = load_game_art(a.game_dir, a.mod_overlay.empty() ? nullptr : &a.mod_overlay);
    if (!art.ok()) {
        std::fprintf(stderr, "b3launcher: the game's look is not available (%s): the plain one\n", art.why.c_str());
        return;
    }
    if (a.text) a.text->set_game_fonts(nullptr, nullptr, nullptr, nullptr);
    for (auto& [n, t] : a.look.tex) SDL_DestroyTexture(t);
    for (SDL_Texture*& t : a.look.font_tex)
        if (t) SDL_DestroyTexture(std::exchange(t, nullptr));
    a.look.tex.clear();
    a.look.art = std::move(art);
    // the arc and the grid tinted here (drawn untinted: SDL's software renderer loses a tinted texture's alpha)
    auto tinted = [](ArtImage im, unsigned rgb, float alpha_scale) {
        for (std::size_t k = 0; k + 3 < im.rgba.size(); k += 4) {
            im.rgba[k] = std::uint8_t(rgb >> 16);
            im.rgba[k + 1] = std::uint8_t(rgb >> 8);
            im.rgba[k + 2] = std::uint8_t(rgb);
            im.rgba[k + 3] = std::uint8_t(std::min(255.0f, float(im.rgba[k + 3]) * alpha_scale));
        }
        return im;
    };
    for (const auto& [n, im] : a.look.art.images) {
        const ArtImage use = n == "Halo" ? tinted(im, 0x6FA8FF, 1.7f) : n == "grid" ? tinted(im, 0x5C8FD8, 0.11f) : im;
        if (SDL_Texture* t = make_texture(a.ren, use)) a.look.tex[n] = t;
    }
    for (int k = 0; k < 3; ++k) a.look.font_tex[k] = make_texture(a.ren, sharpened_font(a.look.art.font[k].tex));
    if (a.text) a.text->set_game_fonts(&a.look.art.font[kGlobalFont], a.look.font_tex[kGlobalFont], &a.look.art.font[kHeadFont], a.look.font_tex[kHeadFont]);
    col::game_theme();
    D::set_round(0.3f);
    a.look.on = true;
    std::printf("b3launcher: the game's look from %s (%zu textures, %zu + %zu + %zu glyphs)\n", a.look.loaded_for.c_str(), a.look.tex.size(),
                a.look.art.font[0].glyphs.size(), a.look.art.font[1].glyphs.size(), a.look.art.font[2].glyphs.size());
}

SDL_Texture* look_tex(const App& a, const char* name) {
    const auto it = a.look.tex.find(name);
    return it == a.look.tex.end() ? nullptr : it->second;
}

// A textured quad (UVs 0..1 of the picture), tinted: geometry, so every renderer blends it with the texture's alpha
void blit(App& a, SDL_Texture* t, SDL_FRect dst, SDL_FColor tint, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    // clipped to the window here (SDL's software renderer drops the blending of a quad that leaves the window)
    if (dst.w <= 0 || dst.h <= 0) return;
    const float du = (u1 - u0) / dst.w, dv = (v1 - v0) / dst.h;
    if (dst.x < 0) { u0 -= dst.x * du; dst.w += dst.x; dst.x = 0; }
    if (dst.y < 0) { v0 -= dst.y * dv; dst.h += dst.y; dst.y = 0; }
    if (dst.x + dst.w > a.W) { u1 -= (dst.x + dst.w - a.W) * du; dst.w = a.W - dst.x; }
    if (dst.y + dst.h > a.H) { v1 -= (dst.y + dst.h - a.H) * dv; dst.h = a.H - dst.y; }
    if (dst.w <= 0 || dst.h <= 0) return;
    const SDL_Vertex v[4] = {{{dst.x, dst.y}, tint, {u0, v0}}, {{dst.x + dst.w, dst.y}, tint, {u1, v0}},
                             {{dst.x + dst.w, dst.y + dst.h}, tint, {u1, v1}}, {{dst.x, dst.y + dst.h}, tint, {u0, v1}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(a.ren, t, v, 4, idx, 6);
}

std::string file_url(const fs::path& p) {
    std::string s = utf8_of(fs::absolute(p)), out;
    for (char& c : s)
        if (c == '\\') c = '/';
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~' || c >= 0x80) out += char(c);
        else {
            char b[4];
            std::snprintf(b, sizeof b, "%%%02X", c);
            out += b;
        }
    }
    return (out.size() > 1 && out[1] == ':') ? "file:///" + out : "file://" + out;
}

bool open_folder(const fs::path& p, std::string* note) {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (SDL_OpenURL(file_url(p).c_str())) return true;
    if (note) *note = std::string("Cannot open the folder: ") + SDL_GetError() + ". It is " + utf8_of(p);
    return false;
}

// ---- dialogs ----

void SDLCALL dialog_done(void* user, const char* const* files, int) {
    const auto kind = static_cast<DialogKind>(reinterpret_cast<std::intptr_t>(user));
    DialogResult r;
    r.kind = kind;
    if (!files) {
        r.failed = true;
        r.error = SDL_GetError();
    } else if (files[0]) {
        r.ok = true;
        r.path = files[0];
    }
    std::lock_guard<std::mutex> l(g_app->dlg_m);
    g_app->dlg_results.push_back(r);
}

void open_browser(App& a, DialogKind kind, const std::string& start, const std::string& note = {});

// osk1: a controller or the touch screen was the last input (the on-screen keyboard and the launcher's folder list)
bool pad_like(const App& a) { return a.pad_used_last || a.touch_last; }

// osk1: Steam's gamepad UI (the Steam Deck's Gaming Mode, Big Picture): a desktop file dialog cannot be driven with
// the pad there (it is another program's window, with no controller support), so the launcher's own list opens
bool gamepad_ui() {
    for (const char* v : {"SteamGamepadUI", "SteamTenfoot"})
        if (const char* e = std::getenv(v); e && *e == '1') return true;
    const char* d = std::getenv("XDG_CURRENT_DESKTOP");
    return d && std::strstr(d, "gamescope");
}

// osk1: the launcher's folder list instead of the system's file dialog: a controller or the touch screen chose, or
// Steam's gamepad UI; $B3LAUNCHER_FILE_DIALOG=launcher / system decides for every input
bool use_own_browser(const App& a) {
    if (const char* e = std::getenv("B3LAUNCHER_FILE_DIALOG"); e && *e) {
        if (std::strcmp(e, "launcher") == 0) return true;
        if (std::strcmp(e, "system") == 0) return false;
    }
    return pad_like(a) || gamepad_ui();
}

// 1.1: PCSX2's memory card folders on this computer (pcsx2_cards.h), most likely first
std::vector<b3native::save::Pcsx2CardFolder> pcsx2_folders(const App& a) {
    if (a.pcsx2_homes) return b3native::save::pcsx2_card_folders(*a.pcsx2_homes);
    b3native::save::Pcsx2Homes h;
    auto user_folder = [](SDL_Folder f) { const char* p = SDL_GetUserFolder(f); return p ? path_from_utf8(p) : fs::path(); };
    h.home = user_folder(SDL_FOLDER_HOME);
    h.documents = user_folder(SDL_FOLDER_DOCUMENTS);
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) h.xdg_config = fs::path(x);
#ifdef _WIN32
    for (const char* v : {"ProgramFiles", "ProgramFiles(x86)"})
        if (const char* p = std::getenv(v); p && *p) h.program_files.push_back(fs::path(p));
#endif
    return b3native::save::pcsx2_card_folders(h);
}

void open_dialog(App& a, DialogKind kind, const std::string& start) {
    if (use_own_browser(a)) {   // osk1
        open_browser(a, kind, start);
        return;
    }
    a.dialog_start = start;
    void* user = reinterpret_cast<void*>(static_cast<std::intptr_t>(kind));
    ++a.dialogs_open;   // until its answer (dialog_done: always called, also on an error)
    std::string at = start;
    if (at.empty() && (kind == DialogKind::kImportFile || kind == DialogKind::kImportFolder)) {   // 1.1: in PCSX2's cards
        const auto f = pcsx2_folders(a);
        if (!f.empty()) at = utf8_of(f.front().path / "");   // (a trailing separator: the dialog opens inside it)
    }
    const char* loc = at.empty() ? nullptr : at.c_str();
    if (kind == DialogKind::kExport) {
        static const SDL_DialogFileFilter f[] = {{"Burnout 3 port save (*.sav)", "sav"}};
        SDL_ShowSaveFileDialog(dialog_done, user, a.win, f, 1, loc);
    } else if (kind == DialogKind::kImportFile) {
        static const SDL_DialogFileFilter f[] = {{"Saves and memory cards", "sav;psu;max;ps2;mcd;bin"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(dialog_done, user, a.win, f, 2, loc, false);
    } else if (kind == DialogKind::kModArchive) {   // modkit1b
        static const SDL_DialogFileFilter f[] = {{"Mods (*.b3mod, *.zip)", "b3mod;zip"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(dialog_done, user, a.win, f, 2, loc, false);
    } else if (kind == DialogKind::kSteamCover || kind == DialogKind::kSteamHero || kind == DialogKind::kSteamLogo) {   // issue #8
        static const SDL_DialogFileFilter f[] = {{"Images (*.png, *.jpg)", "png;jpg;jpeg"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(dialog_done, user, a.win, f, 2, loc, false);
    } else if (kind == DialogKind::kGameImage || kind == DialogKind::kLanguageImage) {   // iso1: the disc image, read in place (b3asset/discfs.h)
        static const SDL_DialogFileFilter f[] = {{"Disc images (*.iso, *.bin, *.chd)", "iso;bin;chd"}, {"All files", "*"}};   // chd1: .chd
        SDL_ShowOpenFileDialog(dialog_done, user, a.win, f, 2, loc, false);
    } else {
        // iso1: the game data was a disc image: its folder to start in
        std::error_code ec;
        const std::string dir = kind == DialogKind::kGameFolder && fs::is_regular_file(path_from_utf8(start), ec)
                                    ? utf8_of(path_from_utf8(start).parent_path()) : at;
        SDL_ShowOpenFolderDialog(dialog_done, user, a.win, dir.empty() ? nullptr : dir.c_str(), false);
    }
}

void handle_path(App& a, DialogKind kind, const std::string& path);

// osk1: the system's own on-screen keyboard (SDL_HasScreenKeyboardSupport: Steam's in the Steam Deck's Gaming Mode,
// shown by SDL_StartTextInput; none on Windows, macOS and a Linux desktop); $B3LAUNCHER_SYSTEM_KEYBOARD=0: never
bool system_keyboard() {
    if (const char* e = std::getenv("B3LAUNCHER_SYSTEM_KEYBOARD"); e && *e == '0') return false;
    return SDL_HasScreenKeyboardSupport();
}

// Text input on (typed characters arrive as SDL_EVENT_TEXT_INPUT). `show_system`: the system's on-screen keyboard
// too, where there is one (a controller or the touch screen opened the field); else SDL's default (on a Steam Deck
// SDL shows Steam's keyboard for any text field)
void start_text_input(App& a, bool show_system) {
    if (show_system && system_keyboard()) SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    else SDL_ResetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD);
    SDL_StartTextInput(a.win);
}

void stop_text_input(App& a) {
    SDL_StopTextInput(a.win);
    SDL_ResetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD);
}

// osk1: the on-screen keyboard under a text field (its focus on Q)
void osk_open(App& a) {
    if (!a.modal || !a.modal->input) return;
    Modal& m = *a.modal;
    m.osk = true;
    m.keys = osk_layout(system_keyboard());
    m.osk_st = OskState{};
    int q = 0;
    OskLayer l{};
    if (osk_find_char(m.keys, "q", q, l)) m.osk_st.focus = q;
}

// A text field for a path (no dialog on this system, or asked for). osk1: with a controller or the touch screen, the
// launcher's on-screen keyboard opens under it (and the system's own where there is one)
void ask_path(App& a, DialogKind kind, const std::string& title, const std::string& start) {
    Modal m;
    m.title = title;
    m.text = "Type or paste (Ctrl+V) the full path.";
    m.input = true;
    m.field.set(start);
    m.on_text = [&a, kind](const std::string& p) { handle_path(a, kind, p); };
    m.buttons = {{"OK", nullptr}, {"Cancel", nullptr}};   // OK: on_text (modal_nav)
    a.modal = m;
    const bool pad = pad_like(a);
    if (pad) osk_open(a);
    start_text_input(a, pad);
}

// osk1: the text field closed: `ok` with its text (OK, Enter, Done, Start), else cancelled
void text_close(App& a, bool ok) {
    if (!a.modal) return;
    auto fn = a.modal->on_text;
    const std::string v = a.modal->field.text;
    a.modal.reset();
    stop_text_input(a);
    a.erase.down = false;
    if (ok && fn && !v.empty()) fn(v);
}

// ---- osk1: the launcher's folder list ----

const char* browse_title(DialogKind k) {
    switch (k) {
    case DialogKind::kHdFolder: return "Choose the HD texture pack folder";
    case DialogKind::kGameFolder: return "Choose the game data folder or disc image";
    case DialogKind::kGameImage: return "Choose the game's disc image or folder";
    case DialogKind::kLanguageImage: return "Choose a European disc image of the game (its languages are copied)";
    case DialogKind::kMusicFolder: return "Choose your music folder";
    case DialogKind::kModArchive: return "Add a mod: choose its .b3mod or .zip file";
    case DialogKind::kImportFile: return "Import a save file";
    case DialogKind::kImportFolder: return "Import a folder card";
    case DialogKind::kExport: return "Export the save to a folder";
    case DialogKind::kSavesFolder: return "Choose the save folder";   // 1.1
    case DialogKind::kSteamCover: return "Choose a cover picture for Steam (PNG or JPEG)";   // issue #8
    case DialogKind::kSteamHero: return "Choose a background picture for Steam (PNG or JPEG)";
    case DialogKind::kSteamLogo: return "Choose a logo picture for Steam (PNG or JPEG)";
    }
    return "Choose";
}

// What is in the folder shown, in the page's words
void browse_status(Browser& b) {
    b.status.clear();
    b.status_level = 0;
    std::size_t files = 0;
    for (const BrowseEntry& e : b.list.entries) files += e.folder ? 0 : 1;
    switch (b.kind) {
    case DialogKind::kGameFolder:
    case DialogKind::kGameImage: {
        const GameCheck g = check_game_folder(b.dir);
        if (g.ok) {
            b.status = "The game's files are here: Start chooses this folder.";
            b.status_level = 1;
        } else if (!g.suggestion.empty()) {
            b.status = "The game's files are in " + utf8_of(g.suggestion.filename()) + " here: Start chooses it.";
            b.status_level = 1;
        } else {
            b.status = files ? "Disc images here: A on one chooses it. Or open the folder you copied the disc's files into."
                             : "No game files here: open the folder you copied the disc's files into, or a folder with your disc image.";
        }
        break;
    }
    case DialogKind::kLanguageImage:   // 1.0.1
        b.status = files ? "Disc images here: A on one copies its languages (a European copy of the game)."
                         : "No disc images here: open the folder that holds your European disc image.";
        b.status_level = files ? 1 : 0;
        break;
    case DialogKind::kHdFolder:
        b.status = files ? std::to_string(files) + (b.list.more ? "+" : "") + (files == 1 ? " image" : " images") +
                               " (.dds / .png) in this folder. Start chooses it."
                         : "No .dds / .png images directly in this folder. A pack keeps them in subfolders: choose its "
                           "SLUS-21050 folder or the folder above it.";
        b.status_level = files ? 1 : 0;
        break;
    case DialogKind::kMusicFolder:
        b.status = files ? std::to_string(files) + (b.list.more ? "+" : "") + (files == 1 ? " song" : " songs") +
                               " in this folder (its subfolders count too). Start chooses it."
                         : "No songs directly in this folder (its subfolders count too). Start chooses it.";
        b.status_level = files ? 1 : 0;
        break;
    case DialogKind::kExport:
        b.status = "Start saves " + b.export_name + " into this folder.";
        break;
    case DialogKind::kImportFolder:
        b.status = "Open the folder card's folder, then Start.";
        break;
    case DialogKind::kModArchive:
    case DialogKind::kSteamCover:   // issue #8
    case DialogKind::kSteamHero:
    case DialogKind::kSteamLogo:
        b.status = files ? "A on a file chooses it." : "No such files in this folder.";
        break;
    case DialogKind::kImportFile:   // 1.1: PCSX2's card named
        if (!b3native::save::pcsx2_first_card(b.dir).empty()) {
            b.status = "PCSX2's memory cards: the card in its first slot is Mcd001.ps2. A on it chooses it.";
            b.status_level = 1;
        } else {
            b.status = files ? "A on a file chooses it." : "No such files in this folder.";
        }
        break;
    case DialogKind::kSavesFolder:   // 1.1
        if (b3native::save::has_saves(b.dir)) {
            b.status = "Saves are in this folder: Start uses them.";
            b.status_level = 1;
        } else {
            b.status = "No saves in this folder yet: Start chooses it (your saves can be copied into it).";
        }
        break;
    }
}

// Shows `dir` (its subfolders and the files of the kind asked for), `select` highlighted when it is listed
bool browse_to(App& a, const fs::path& dir, const fs::path& select = {}) {
    Browser& b = *a.browser;
    BrowseList l = list_folder(dir, b.exts);
    if (!l.ok) {
        b.note = "Cannot open " + utf8_of(dir) + ": " + l.error;
        return false;
    }
    b.note.clear();
    b.dir = dir;
    b.list = std::move(l);
    b.sel = 0;
    b.first = 0;
    b.follow = true;
    for (std::size_t k = 0; k < b.list.entries.size(); ++k)
        if (!select.empty() && b.list.entries[k].path.filename() == select.filename()) b.sel = int(k);
    b.column = 1;
    browse_status(b);
    return true;
}

void open_browser(App& a, DialogKind kind, const std::string& start, const std::string& note) {
    a.browser.emplace();
    Browser& b = *a.browser;
    b.kind = kind;
    b.title = browse_title(kind);
    switch (kind) {
    case DialogKind::kGameFolder:
    case DialogKind::kGameImage: b.exts = {"iso", "bin", "chd"}; b.pick_files = true; break;   // a folder, or a disc image (chd1: .chd)
    case DialogKind::kLanguageImage: b.exts = {"iso", "bin", "chd"}; b.pick_files = true; break;   // 1.0.1
    case DialogKind::kHdFolder: b.exts = {"dds", "png"}; break;                          // shown, not chosen
    case DialogKind::kMusicFolder: b.exts = {"mp3", "flac", "ogg", "oga", "wav"}; break;
    case DialogKind::kModArchive: b.exts = {"b3mod", "zip"}; b.pick_files = true; b.pick_folder = false; break;
    case DialogKind::kImportFile: b.exts = {"sav", "psu", "max", "ps2", "mcd", "bin"}; b.pick_files = true; b.pick_folder = false; break;
    case DialogKind::kImportFolder: break;
    case DialogKind::kExport: b.export_name = start.empty() ? "Burnout3-save.sav" : start; break;
    case DialogKind::kSavesFolder: break;   // 1.1
    case DialogKind::kSteamCover:   // issue #8
    case DialogKind::kSteamHero:
    case DialogKind::kSteamLogo: b.exts = {"png", "jpg", "jpeg"}; b.pick_files = true; b.pick_folder = false; break;
    }
    const bool importing = kind == DialogKind::kImportFile || kind == DialogKind::kImportFolder;
    // where it starts: the current choice (a file's folder, with the file highlighted), else home
    auto user_folder = [](SDL_Folder f) { const char* p = SDL_GetUserFolder(f); return p ? path_from_utf8(p) : fs::path(); };
    const fs::path home = user_folder(SDL_FOLDER_HOME);
    fs::path from = kind == DialogKind::kExport ? fs::path() : path_from_utf8(start), select;
    std::error_code ec;
    if (!from.empty() && fs::is_regular_file(from, ec)) {
        select = from;
        from = from.parent_path();
    }
    const std::vector<b3native::save::Pcsx2CardFolder> pcsx2 = importing ? pcsx2_folders(a) : std::vector<b3native::save::Pcsx2CardFolder>{};
    if (importing && from.empty() && !pcsx2.empty()) from = pcsx2.front().path;   // 1.1: an import starts in PCSX2's cards
    if (kind == DialogKind::kImportFile && select.empty() && !from.empty()) select = b3native::save::pcsx2_first_card(from);
    while (!from.empty() && !fs::is_directory(from, ec) && !is_root_folder(from)) from = parent_folder(from);   // (gone: the nearest folder above)
    if (from.empty() || !fs::is_directory(from, ec)) from = kind == DialogKind::kExport ? user_folder(SDL_FOLDER_DOCUMENTS) : home;
    if (from.empty() || !fs::is_directory(from, ec)) from = a.exe_dir;
    std::vector<BrowsePlace> places;
    if (!start.empty() && kind != DialogKind::kExport) places.push_back({"Current choice", select.empty() ? from : select.parent_path()});
    for (const auto& f : pcsx2) places.push_back({f.label, f.path});   // 1.1
    places.push_back({"Home", home});
    places.push_back({"Desktop", user_folder(SDL_FOLDER_DESKTOP)});
    places.push_back({"Documents", user_folder(SDL_FOLDER_DOCUMENTS)});
    places.push_back({"Downloads", user_folder(SDL_FOLDER_DOWNLOADS)});
    if (kind == DialogKind::kMusicFolder) places.push_back({"Music", user_folder(SDL_FOLDER_MUSIC)});
    places.push_back({"Launcher folder", a.exe_dir});
    b.places = browse_places(places);
    if (!browse_to(a, from, select) && !browse_to(a, home)) browse_to(a, a.exe_dir);
    if (!note.empty()) b.note = note;
}

// A choice made: the folder list closes and the page takes the path
void browse_done(App& a, DialogKind kind, const fs::path p) {   // (p by value: it may be the list's, gone with it)
    a.browser.reset();
    handle_path(a, kind, utf8_of(p));
}

// Start: the folder shown (Export: a new file in it); in a file list the highlighted file
void browse_choose(App& a) {
    Browser& b = *a.browser;
    const BrowseEntry* e = b.sel < int(b.list.entries.size()) ? &b.list.entries[std::size_t(b.sel)] : nullptr;
    if (!b.pick_folder) {
        if (e && !e->folder) browse_done(a, b.kind, e->path);
        else b.note = "Choose a file: A on it.";
        return;
    }
    if (b.kind == DialogKind::kExport) {
        fs::path to = b.dir / path_from_utf8(b.export_name);
        std::error_code ec;
        const fs::path stem = to.stem(), ext = to.extension();
        for (int n = 2; fs::exists(to, ec) && n < 1000; ++n) to = b.dir / path_from_utf8(utf8_of(stem) + "-" + std::to_string(n) + utf8_of(ext));   // never over a file
        browse_done(a, b.kind, to);
        return;
    }
    browse_done(a, b.kind == DialogKind::kGameImage ? DialogKind::kGameFolder : b.kind, b.dir);
}

// A: a folder opens; a file of the kind asked for is chosen
void browse_open(App& a) {
    Browser& b = *a.browser;
    if (b.column == 0) {
        if (b.place_sel < int(b.places.size())) browse_to(a, b.places[std::size_t(b.place_sel)].path);
        return;
    }
    if (b.sel >= int(b.list.entries.size())) return;
    const BrowseEntry e = b.list.entries[std::size_t(b.sel)];
    if (e.folder) browse_to(a, e.path);
    else if (b.pick_files) browse_done(a, b.kind == DialogKind::kGameFolder ? DialogKind::kGameImage : b.kind, e.path);
    else b.note = "The files are shown to help you find the folder: Start chooses the folder they are in.";
}

// B: the folder above (the one left highlighted); at the top, the places
void browse_up(App& a) {
    Browser& b = *a.browser;
    if (b.column == 0) {
        a.browser.reset();   // B in the places: closed
        return;
    }
    if (is_root_folder(b.dir)) {
        b.column = 0;
        return;
    }
    const fs::path from = b.dir;
    browse_to(a, parent_folder(from), from);
}

// Type a path instead (the folder shown filled in)
void browse_type(App& a) {
    const DialogKind kind = a.browser->kind;
    std::string start = utf8_of(a.browser->dir);
    if (!start.empty() && start.back() != '/' && start.back() != '\\') start += char(fs::path::preferred_separator);
    if (kind == DialogKind::kExport) start += a.browser->export_name;
    const std::string title = a.browser->title;
    a.browser.reset();
    ask_path(a, kind, title, start);
}

void browse_nav(App& a, Nav n) {
    Browser& b = *a.browser;
    b.follow = true;
    const int count = b.column == 0 ? int(b.places.size()) : int(b.list.entries.size());
    int& s = b.column == 0 ? b.place_sel : b.sel;
    const int page = std::max(1, b.rows_shown - 1);
    switch (n) {
    case Nav::Up: s = std::max(0, s - 1); break;
    case Nav::Down: s = std::max(0, std::min(count - 1, s + 1)); break;
    case Nav::PrevPage:
    case Nav::PrevTab: s = std::max(0, s - page); break;
    case Nav::NextPage:
    case Nav::NextTab: s = std::max(0, std::min(count - 1, s + page)); break;
    case Nav::Left: b.column = 0; break;
    case Nav::Right: b.column = 1; break;
    case Nav::Confirm: browse_open(a); break;
    case Nav::Back: browse_up(a); break;
    }
}

void choose_saves_folder(App& a, const fs::path& to);   // 1.1 (the SAVES page)
void steam_choose(App& a, DialogKind kind, const fs::path& p);   // issue #8 (Settings > Launcher)

void handle_path(App& a, DialogKind kind, const std::string& path) {
    const fs::path p = path_from_utf8(path);
    const fs::path root = saves_root(a);
    const int slot = b3native::save::read_active_slot(root);
    std::string err;
    switch (kind) {
    case DialogKind::kHdFolder: {
        a.settings.hd_pack_dir = path;
        save_settings(a);
        refresh_hd(a, true);
        a.hd_note = !a.hd.exists ? "That folder is not there." : a.hd.images ? "Folder chosen: in use from the next start."
                                                                              : "Chosen, but no .dds / .png images were found in it.";
        break;
    }
    case DialogKind::kMusicFolder: {   // the in-game page's MUSIC FOLDER (pcpage.cpp music_folder_done): the same words
        a.settings.music_folder = path;
        save_settings(a);
        refresh_music(a, true);
        a.music_note = !a.music.exists ? "That folder is not there."
                       : a.music.songs ? std::to_string(a.music.songs) + " songs found: in use from the next song (PLAYLIST: Custom only or Mixed)."
                                       : "Chosen, but no MP3, FLAC, Ogg Vorbis or WAV files were found in it.";
        break;
    }
    case DialogKind::kGameFolder:
    case DialogKind::kGameImage:   // iso1: a folder or a disc image, checked the same way
        a.game_note.clear();
        set_game_dir(a, p, true);
        if (a.game_note.empty()) a.game_note = a.game.ok ? "Remembered: the game starts with this folder." : a.game.summary;
        load_look(a);
        break;
    case DialogKind::kModArchive:   // modkit1b
        add_mod(a, p, false);
        break;
    case DialogKind::kSavesFolder:   // 1.1
        choose_saves_folder(a, p);
        return;   // (refreshed there; a question may be open)
    case DialogKind::kSteamCover:   // issue #8
    case DialogKind::kSteamHero:
    case DialogKind::kSteamLogo:
        steam_choose(a, kind, p);
        return;
    case DialogKind::kLanguageImage: {   // 1.0.1: copy the disc's languages into <pref>/languages (b3asset/language.h)
        std::string why;
        const auto done = b3asset::language::import_from_disc(p, a.pref, &why);
        refresh_languages(a);
        if (done.empty()) a.language_note = chd_problem(p).empty() ? "No languages imported: " + why + "." : chd_problem(p);   // chd1
        else {
            std::string names;
            for (const auto& l : done) names += (names.empty() ? "" : ", ") + l.name;
            a.language_note = "Imported: " + names + ". Choose one in LANGUAGE (from the next start).";
        }
        break;
    }
    case DialogKind::kExport: {
        fs::path to = p;
        if (to.extension().empty()) to += ".sav";
        auto prof = b3native::save::load_profile(root, slot, &err);
        std::optional<b3native::save::Directories> game;
        if (prof) game = b3native::save::game_save(prof->dirs, &err);
        const bool ok = game && b3native::save::write_sav_file(to, *game, &err);
        a.save_note = ok ? "Exported file " + std::to_string(slot) + " to " + utf8_of(to) : "EXPORT FAILED: " + err;
        a.save_note_bad = !ok;
        break;
    }
    case DialogKind::kImportFile:
    case DialogKind::kImportFolder: {
        std::error_code ec;
        fs::create_directories(root, ec);
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const bool ok = b3native::save::import_profile(root, slot, p, now, &err);
        a.save_note = ok ? "Imported into file " + std::to_string(slot) + " (the old one kept as profile-" + std::to_string(slot) +
                               ".sav.bak). The game loads it at the next start."
                         : "IMPORT FAILED: " + err;
        a.save_note_bad = !ok;
        break;
    }
    }
    refresh(a);
}

void poll_dialogs(App& a) {
    std::vector<DialogResult> r;
    {
        std::lock_guard<std::mutex> l(a.dlg_m);
        r.swap(a.dlg_results);
    }
    a.dialogs_open = std::max(0, a.dialogs_open - int(r.size()));
    for (const DialogResult& d : r) {
        if (d.ok) handle_path(a, d.kind, d.path);
        else if (d.failed) {   // osk1: no file dialog on this system: the launcher's own list (it can also type a path)
            open_browser(a, d.kind, a.dialog_start, "There is no file dialog on this system (" + d.error + "): choose here, or type the path.");
        }
    }
}

// ---- starting the game ----

std::string log_tail(const fs::path& p, int lines) {
    std::ifstream f(p, std::ios::binary);
    std::vector<std::string> all;
    std::string l;
    while (std::getline(f, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        all.push_back(l);
    }
    std::string out;
    for (std::size_t k = all.size() > std::size_t(lines) ? all.size() - std::size_t(lines) : 0; k < all.size(); ++k) out += all[k] + "\n";
    return out;
}

void message(App& a, const std::string& title, const std::string& text) {
    Modal m;
    m.title = title;
    m.text = text;
    m.buttons = {{"OK", nullptr}};
    a.modal = m;
}

// sdk2b (DESIGN.md §2.5): a code mod runs as a program on the user's PC, so the launcher asks once per library (its
// SHA-256 in mods-trusted.ini; a new version of the library is asked about again) before the game may load it
void ask_code_trust(App& a, const std::vector<const b3mod::Mod*>& mods, const std::string& yes, std::function<void()> then) {
    std::string names;
    for (std::size_t k = 0; k < mods.size(); ++k) names += (k ? ", " : "") + mods[k]->manifest.name + " " + mods[k]->manifest.version;
    Modal m;
    m.title = mods.size() == 1 ? "Allow this mod's code to run?" : "Allow these mods' code to run?";
    m.text = names + (mods.size() == 1 ? " contains code" : " contain code") +
             ": a library the game loads and runs as a program on your PC, with your user's rights, like any program you "
             "install. Allow code only from people you trust, ideally with its source. You are asked again when the "
             "library changes (a new version of the mod).";
    struct Ask {
        std::string id, version, digest;   // (the library's hash as it was asked about; the mods list may be re-read meanwhile)
    };
    std::vector<Ask> list;
    for (const b3mod::Mod* md : mods) list.push_back({md->manifest.id, md->manifest.version, b3mod::library_digest(code_library(*md))});
    m.buttons = {{yes, [&a, list, then]() {
                      for (const Ask& md : list) {
                          std::string why;
                          if (!b3mod::add_trust(a.pref, md.digest, md.id, md.version, &why)) {
                              a.mods_note = "Cannot record the permission: " + why;
                              a.mods_note_bad = true;
                              return;
                          }
                      }
                      if (then) then();
                  }},
                 {"Cancel", nullptr}};
    m.focus = 1;
    a.modal = m;
}

void start_game(App& a, bool safe, const std::vector<std::string>& extra = {}) {
    if (!safe) {   // sdk2b: the code mods that are on must be allowed first (the game refuses one that is not)
        std::vector<const b3mod::Mod*> ask;
        for (const b3mod::Mod* m : a.mod_sel.mods)
            if (!code_allowed(a, *m)) ask.push_back(m);
        if (!ask.empty()) {
            ask_code_trust(a, ask, "Allow and start", [&a, extra]() { start_game(a, false, extra); });
            return;
        }
    }
    const fs::path prog = game_program(a.exe_dir);
    std::error_code ec;
    if (!fs::exists(prog, ec)) {
        message(a, "The game is not here", "The launcher starts " + prog.filename().string() + " from its own folder, and it is not there:\n" + utf8_of(prog));
        return;
    }
    if (!a.game.ok) {
        message(a, "No game data", "Choose your Burnout 3 disc image, or the folder with its files, on the Game Data page first.\n" + a.game.summary);
        return;
    }
    std::string err;
    auto args = game_arguments(utf8_of(fs::absolute(a.game_dir, ec)), safe);
    args.insert(args.end(), extra.begin(), extra.end());   // replay1: --replay <file>
    // test builds: the game writes its run log and pad recording (logs/run-<date-time>.log, .pads.txt; the newest
    // ten runs kept) itself; its own stdout / stderr go nowhere (on Windows no console window either)
    // (a path with characters beyond ASCII is not passed: native_play's narrow arguments on Windows could not carry
    // it; the game then names the log itself in the same folder, and the launcher reads the newest one)
    fs::create_directories(logs_folder(a.pref), ec);
    a.run_log = b3port::runlog::new_run(logs_folder(a.pref), std::time(nullptr)).log;
    const std::string log_arg = utf8_of(a.run_log);
    if (std::all_of(log_arg.begin(), log_arg.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x80; })) {
        args.push_back("--log");
        args.push_back(log_arg);
    } else {
        a.run_log.clear();
    }
    auto c = spawn(prog, args, a.exe_dir, null_device(), &err);
    if (!c) {
        message(a, "Cannot start the game", err);
        return;
    }
    a.child = *c;
    a.child_started = a.now();
}

// test builds: a "file:///..." URL of a folder (SDL_OpenURL shows it in the file manager)
std::string folder_url(const fs::path& dir) {
    std::string p = utf8_of(dir);
    for (char& ch : p)
        if (ch == '\\') ch = '/';
    std::string u = p.size() > 1 && p[1] == ':' ? "file:///" : "file://";
    const char* hex = "0123456789ABCDEF";
    for (unsigned char ch : p) {
        if (std::isalnum(ch) || ch == '/' || ch == ':' || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch >= 0x80) u += char(ch);
        else { u += '%'; u += hex[ch >> 4]; u += hex[ch & 15]; }
    }
    return u;
}

// test builds: the bug report (include/b3report/bug_report.h): the newest runs' logs and pad recordings, the ini
// files, the version, the system; the active save only when asked. Shown, then its folder opened.
void make_report(App& a) {
    refresh(a);
    b3port::BugReportInput in;
    in.pref = a.pref;
    const char* desk = SDL_GetUserFolder(SDL_FOLDER_DESKTOP);
    const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    in.out_dir = !a.report_dir.empty() ? a.report_dir
                                       : b3port::report_folder({desk ? path_from_utf8(desk) : fs::path(), docs ? path_from_utf8(docs) : fs::path(), a.pref});
    if (a.report_save)
        for (const SaveSlot& s : a.slots)
            if (s.active && s.sav) in.save_file = b3native::save::profile_file(saves_root(a), s.slot);
    in.system_info = b3port::system_info({}, true);
    in.system_info += "game data: " + utf8_of(a.game_dir) + " (" + a.game.summary + (a.game.warning.empty() ? "" : " " + a.game.warning) + ")\n";
    in.system_info += "launcher: " + utf8_of(a.exe_dir) + "\n";
    in.origin = "the launcher";
    const b3port::BugReportResult r = b3port::make_bug_report(in);
    if (!r.ok) {
        message(a, "No bug report", "The report could not be written: " + r.error);
        return;
    }
    std::size_t runs = 0;
    for (const std::string& e : r.entries)
        if (e.size() > 4 && e.compare(e.size() - 4, 4, ".log") == 0) ++runs;
    message(a, "Bug report saved",
            utf8_of(r.zip) + "\n\nIt holds " + (runs == 1 ? std::string("the log of the last run and its controls") : runs == 0 ? std::string("no run logs yet") : "the logs of the last " + std::to_string(runs) + " runs and their controls") + ", your settings files, the "
            "version and a short system description" + (in.save_file.empty() ? std::string(" (no save, no game data)") : ", your active save (no game data)") +
            ". Send it with a few words on what happened and when.");
    if (a.report_open) SDL_OpenURL(folder_url(r.zip.parent_path()).c_str());
}

void poll_child(App& a) {
    if (!a.child) return;
    if (auto code = poll_exit(*a.child)) {
        a.child.reset();
        refresh(a);
        if (a.run_log.empty())   // (named by the game: the newest run)
            if (const auto runs = b3port::runlog::runs(logs_folder(a.pref)); !runs.empty()) a.run_log = logs_folder(a.pref) / (runs.front() + ".log");
        message(a, "The game ended at once", "native_play ended with code " + std::to_string(*code) + ". The end of its log (" +
                                                 utf8_of(a.run_log) + "):\n\n" + log_tail(a.run_log, 8));
        return;
    }
    if (a.now() - a.child_started > 2.5) {   // it is running: the launcher's work is done
        release(*a.child);
        a.child.reset();
        a.quit = true;
    }
}

// ---- launchskip1: the start without the launcher (core.h skip_launcher) ----

constexpr double kSkipGrace = 1.0;   // the "Starting" screen's time to hold Shift / Back / Select

// Shift (the system's view, else SDL's once the window has the focus), or Back / Select on any controller, held now
bool skip_held(const App& a) {
    if (shift_key_held() || (SDL_GetModState() & SDL_KMOD_SHIFT)) return true;
    for (SDL_Gamepad* g : a.pads)
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) return true;
    return false;
}

SkipInput skip_input(const App& a, bool launcher_argument, bool held) {
    SkipInput in;
    in.launcher_at_start = a.settings.launcher_at_start;
    in.launcher_argument = launcher_argument;
    in.held = held;
    in.game_ok = a.game.ok;
    in.game_warning = !a.game.warning.empty();
    std::error_code ec;
    in.have_program = fs::exists(game_program(a.exe_dir), ec);
    in.last_run = summarize_run(a.run, a.have_run).kind;
    return in;
}

// The "Starting" screen over the Play page; the game starts after kSkipGrace (skip_poll)
void begin_skip(App& a) {
    a.page = kPlay;
    a.in_content = true;
    a.focus = 0;
    a.skip_at = a.now() + kSkipGrace;
}

void cancel_skip(App& a) {
    a.skip_at = 0;
    a.skip_block = SkipBlock::kHeld;
    std::printf("b3launcher: Shift / Back held: the launcher opens\n");
}

// Each frame while the "Starting" screen shows: a key or button held opens the launcher, else at its time the game
// starts as START GAME starts it (the launcher closes once it runs; it shows again when the game ends at once)
void skip_poll(App& a) {
    if (a.skip_at <= 0) return;
    if (skip_held(a)) {
        cancel_skip(a);
        return;
    }
    if (a.now() < a.skip_at) return;
    a.skip_at = 0;
    ++a.skip_starts;
    if (a.skip_no_spawn) return;
    start_game(a, false);
    a.skipping = a.child.has_value();
}

// ---- drawing helpers ----

struct Ui {
    App& a;
    std::vector<Widget>& out;
    int add(Widget w) {
        out.push_back(std::move(w));
        return int(out.size()) - 1;
    }
    bool focused(int i) const { return a.in_content && !a.modal && i == a.focus; }
    bool hovered(int i) const {
        const SDL_FRect& r = out[std::size_t(i)].r;
        return a.mouse_on && !a.modal && a.mouse_x >= r.x && a.mouse_x < r.x + r.w && a.mouse_y >= r.y && a.mouse_y < r.y + r.h;
    }
    // Widgets first..last (in the order added) as one list for Up / Down: each one's down is the next, each one's up the
    // one before (left / right keep their own meaning)
    void chain(int first, int last) {
        for (int k = first; k < last; ++k) {
            out[std::size_t(k)].down = k + 1;
            out[std::size_t(k + 1)].up = k;
        }
    }
};

float text(App& a, std::string_view s, float x, float y, float size, SDL_FColor c, bool bold = false) {
    return a.text->draw(s, x, y, size, c, bold);
}
float text_right(App& a, std::string_view s, float right, float y, float size, SDL_FColor c, bool bold = false) {
    const float w = a.text->width(s, size, bold);
    a.text->draw(s, right - w, y, size, c, bold);
    return w;
}
float text_center(App& a, std::string_view s, float cx, float y, float size, SDL_FColor c, bool bold = false) {
    const float w = a.text->width(s, size, bold);
    a.text->draw(s, cx - w * 0.5f, y, size, c, bold);
    return w;
}
// Wrapped text; returns the height used
float paragraph(App& a, std::string_view s, float x, float y, float w, float size, SDL_FColor c, bool bold = false) {
    float yy = y;
    for (const std::string& l : a.text->wrap(s, size, w, bold)) {
        a.text->draw(l, x, yy, size, c, bold);
        yy += a.text->line_height(size);
    }
    return yy - y;
}

// The game's selection marks around a highlighted label: a tick and a bar before it, a bar after it and a line on
// towards `line_to` (0: a tick)
void brackets(float x0, float x1, float top, float h, SDL_FColor c, float line_to = 0) {
    const float mid = top + h * 0.5f, t = 2.0f;
    D::rect(SDL_FRect{x0 - 12, mid - t * 0.5f, 9, t}, c);
    D::rect(SDL_FRect{x0 - 4, top + 3, t, h - 6}, c);
    D::rect(SDL_FRect{x1 + 3, top + 3, t, h - 6}, c);
    D::rect(SDL_FRect{x1 + 4, mid - t * 0.5f, line_to > x1 + 14 ? line_to - x1 - 4 : 9, t}, c);
}

void focus_ring(const SDL_FRect& r, float radius) {
    D::round_rect_outline(SDL_FRect{r.x - 3, r.y - 3, r.w + 6, r.h + 6}, radius + 3, 2.0f, col::accent);
}

enum class Style { kPrimary, kNormal, kDanger, kQuiet };

int button(Ui& ui, const SDL_FRect& r, const std::string& label, Style st, bool enabled, const std::string& desc,
           std::function<void()> fn) {
    App& a = ui.a;
    Widget w;
    w.r = r;
    w.desc = desc;
    if (enabled) w.activate = std::move(fn);
    const int i = ui.add(std::move(w));
    const bool f = ui.focused(i), h = ui.hovered(i) && enabled;
    SDL_FColor fill = col::panel2, fg = col::text;
    if (st == Style::kPrimary) {
        fill = enabled ? (f || h ? col::accent_hi : col::accent) : col::panel2;
        fg = enabled ? col::ink : col::faint;
    } else {
        if (f || h) fill = col::panel3;
        if (st == Style::kQuiet && !(f || h)) fill = D::alpha(col::panel2, 0.6f);
        if (!enabled) fg = col::faint;
        if (st == Style::kDanger && enabled) fg = col::bad;
    }
    D::round_rect(r, 8, fill);
    if (a.look.on && st != Style::kPrimary && st != Style::kDanger) D::round_rect_outline(r, 8, 1.0f, enabled ? col::edge : D::alpha(col::edge, 0.35f));
    if (st == Style::kDanger && enabled) D::round_rect_outline(r, 8, 1.5f, D::alpha(col::bad, 0.7f));
    if (f) focus_ring(r, 8);
    const float size = st == Style::kPrimary && r.h >= 56 ? 19.0f : kBody;
    text_center(a, label, r.x + r.w * 0.5f, r.y + (r.h - a.text->line_height(size)) * 0.5f, size, fg, st == Style::kPrimary || f);
    return i;
}

// A status line: a dot, a title and its text
// 1.1: a status line's title column: 150, or wider for a translated title that needs it
float status_title_w(App& a, const std::string& title) { return std::max(150.0f, a.text->width(title, kBody, true) + 14); }

float status_line(App& a, float x, float y, float w, SDL_FColor dot, const std::string& title, const std::string& body) {
    D::disc(x + 6, y + 11, 5, dot);
    text(a, title, x + 22, y, kBody, col::text, true);
    const float tw = status_title_w(a, title);
    const float hgt = paragraph(a, body, x + 22 + tw, y, w - 22 - tw, kBody, col::dim);
    return std::max(hgt, a.text->line_height(kBody)) + 10;
}

// 1.1: capitals of UTF-8 text in the launcher's languages (ASCII, Latin-1's letters, oe); other characters as they are
std::string utf8_upper(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {   // U+00C0..U+00FF
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            const unsigned cp = 0xC0u + (d & 0x3Fu);
            const bool lower = cp >= 0xE0 && cp <= 0xFE && cp != 0xF7;
            o += char(0xC3);
            o += char(lower ? d - 0x20 : d);
            i += 2;
        } else if (c == 0xC5 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x93) {   // oe
            o += "\xC5\x92";
            i += 2;
        } else {
            o += char(std::toupper(c));
            ++i;
        }
    }
    return o;
}

float section(App& a, const std::string& s, float x, float y) {
    const std::string up = utf8_upper(a.text->translated(s));
    text(a, up, x, y, a.look.on ? kSection + 1.5f : kSection, col::accent, true);
    return a.text->line_height(a.look.on ? kSection + 1.5f : kSection) + 6;
}

// ---- the PLAY page ----

std::string slot_line(const SaveSlot& s) {
    std::string t = "File " + std::to_string(s.slot);
    if (s.sav) t += " (profile-" + std::to_string(s.slot) + ".sav, " + format_bytes(s.bytes) + (s.modified ? ", saved " + format_time(*s.modified) : "") + ")";
    else if (s.legacy) t += " (an older profile-" + std::to_string(s.slot) + " folder: read, and saved as a .sav at the first save)";
    else t += " (new: the game makes it at its first save)";
    return t;
}

std::string settings_line(const b3settings::Settings& s) {
    std::string t = std::string(b3settings::preset_name(b3settings::matching_preset(s))) + " preset, ";
    t += s.fullscreen ? "fullscreen" : "window";
    t += s.vsync ? ", vsync" : "";
    t += s.msaa > 1 ? ", MSAA " + std::to_string(s.msaa) + "x" : ", no MSAA";
    if (s.frame_cap > 0) t += ", " + std::to_string(s.frame_cap) + " fps limit";
    t += s.hd_pack_dir.empty() ? ", no HD pack" : s.hd_textures ? ", HD pack" : ", HD pack off";
    return t;
}

// update1: "Update available" under the start buttons, when a newer release is out: its name, date and first notes,
// and Download (its page on github.com in the browser; the launcher downloads nothing itself). Returns the y after it.
float play_update_banner(App& a, Ui& ui, float x, float y, float w) {
    if (update_state(a) != update::State::kAvailable) return y;
    const update::Release& r = a.cfg.update.latest;
    const float h = 64, bw = std::min(190.0f, w * 0.3f);
    D::round_rect(SDL_FRect{x, y, w, h}, 12, col::panel);
    D::round_rect_outline(SDL_FRect{x, y, w, h}, 12, 1.5f, D::alpha(col::accent, 0.85f));
    D::disc(x + 22, y + h * 0.5f, 6, col::accent);
    const float tx = x + 40, tw = w - 40 - (oneclick_available(a) ? 2 * bw + 12 : bw) - 36;
    text(a, a.text->elide(update::status_text(a.cfg.update, update::State::kAvailable), kBody, tw, true), tx, y + 10, kBody, col::text, true);
    std::string sub;
    if (const std::string d = update::published_date(r.published); !d.empty()) sub = "Released " + d;
    for (const std::string& n : r.notes) sub += (sub.empty() ? "" : "  \u00B7  ") + n;
    if (sub.empty()) sub = "A newer version of the port is out.";
    text(a, a.text->elide(sub, kSmall, tw), tx, y + 12 + a.text->line_height(kBody), kSmall, col::dim);
    const std::string url = r.url;
    const bool oneclick_on = oneclick_available(a);
    if (oneclick_on) {   // 1.1: install it from here (a signed release), or the page as before
        const bool busy = a.inst.job != nullptr;
        if (a.inst.installed)
            button(ui, SDL_FRect{x + w - bw - 12, y + 12, bw, h - 24}, "Restart now", Style::kPrimary, true,
                   "Starts the new version of the launcher. Your saves and settings stay as they are.", [&a]() { restart_launcher(a); });
        else
            button(ui, SDL_FRect{x + w - bw - 12, y + 12, bw, h - 24}, busy ? "Installing..." : "Install update", Style::kPrimary, !busy,
                   "Downloads the new version, checks that it is the port's own signed release, and puts it in place of this one. "
                   "The previous version is kept: Settings > Launcher can undo the update.",
                   [&a]() { start_install(a); });
        button(ui, SDL_FRect{x + w - 2 * bw - 24, y + 12, bw, h - 24}, "Release page", Style::kNormal, true,
               "Opens the release's page (" + url + ") in your browser.", [url]() { SDL_OpenURL(url.c_str()); });
    } else {
        button(ui, SDL_FRect{x + w - bw - 12, y + 12, bw, h - 24}, "Download", Style::kNormal, true,
               "Opens the release's page (" + url + ") in your browser, to download the new version there. The launcher installs nothing itself.",
               [url]() { SDL_OpenURL(url.c_str()); });
    }
    y += h + 20;
    if (a.inst.job || !a.inst.note.empty()) {   // the install's progress, or its result
        std::string line = a.inst.note;
        if (a.inst.job) {
            int step;
            {
                std::lock_guard<std::mutex> l(a.inst.job->m);
                step = a.inst.job->step;
            }
            static const char* kSteps[] = {"Asking GitHub for the release...", "Checking the release's signature...", "Downloading the update...",
                                           "Checking the download...", "Saving the download...", "Installing..."};
            line = kSteps[std::clamp(step, 0, 5)];
        }
        y += paragraph(a, line, x + 4, y - 8, w - 8, kSmall, a.inst.note.rfind("Update not", 0) == 0 ? col::warn : col::dim) + 8;
    }
    return y;
}

void page_play(App& a, Ui& ui, float x, float y, float w) {
    // the banner
    const SDL_FRect banner{x, y, w, 104};
    D::round_rect(banner, 14, col::panel);
    D::gradient(SDL_FRect{x + 14, y, w * 0.62f - 14, 104}, D::alpha(col::accent, 0.26f), D::alpha(col::accent, 0.0f),
                D::alpha(col::accent, 0.12f), D::alpha(col::accent, 0.0f));
    D::rect(SDL_FRect{x, y + 14, 4, 76}, col::accent);
    if (SDL_Texture* logo = look_tex(a, "titlescreen_logo")) {   // the title screen's logo (FRONTEND.TXD), its letters only
        const SDL_FRect src{18, 44, 476, 168};
        const float lh = 100, lw = lh * src.w / src.h;
        blit(a, logo, SDL_FRect{x + w - lw - 18, y + 2, lw, lh}, D::rgba(0xFFFFFF), src.x / 512, src.y / 256, (src.x + src.w) / 512,
             (src.y + src.h) / 256);
        paragraph(a, "The native PC port. Pick your settings here or in the game's own SETTINGS page.", x + 30, y + 22,
                  w - lw - 70, kBody + 1, col::dim, true);
    } else {
        text(a, "BURNOUT 3: TAKEDOWN", x + 30, y + 16, 30, col::text, true);
        text(a, "The native PC port. Pick your settings here or in the game's own SETTINGS page.", x + 30, y + 62, kBody, col::dim);
    }
    y += 104 + 20;

    const fs::path prog = game_program(a.exe_dir);
    std::error_code ec;
    const bool have_prog = fs::exists(prog, ec);
    const bool can_start = have_prog && a.game.ok;
    const float bw = std::min(340.0f, (w - 24) * 0.5f);
    button(ui, SDL_FRect{x, y, bw, 64}, "START GAME", Style::kPrimary, can_start,
           can_start ? "Starts the game with your settings. The launcher closes once it runs."
                     : !a.game.ok ? "Set the game data (disc image or folder) first: Game Data page."
                                  : "The game program (" + prog.filename().string() + ") is not beside the launcher: keep them in one folder.",
           [&a]() { start_game(a, false); });
    button(ui, SDL_FRect{x + bw + 16, y, bw, 64}, "START IN SAFE MODE", Style::kNormal, can_start,
           "Starts this run with safe settings (low quality, no MSAA, no HD pack, a window, vsync). Your settings file stays as it is.",
           [&a]() { start_game(a, true); });
    y += 64 + 14;
    {   // launchskip1: the start without the launcher (Settings::launcher_at_start: the game's OPEN THE LAUNCHER AT START)
        const bool skip_on = !a.settings.launcher_at_start;
        button(ui, SDL_FRect{x, y, bw * 2 + 16, 44}, std::string("Start the game directly next time (skip this launcher): ") + (skip_on ? "ON" : "OFF"),
               Style::kQuiet, true,
               "On: starting the launcher starts the game at once, with these settings, saves, mods and music. To see the "
               "launcher again, hold Shift (or Back / Select on a controller) while it starts, or turn OPEN THE LAUNCHER AT "
               "START back on in the game's SETTINGS > INTERFACE.",
               [&a]() {
                   a.settings.launcher_at_start = !a.settings.launcher_at_start;
                   save_settings(a);
               });
        y += 44;
        const std::string why = skip_on ? skip_block_text(a.skip_block) : std::string();
        if (!why.empty()) {
            text(a, why, x + 4, y + 8, kSmall, col::warn);
            y += 8 + a.text->line_height(kSmall);
        }
        y += 22;
    }
    y = play_update_banner(a, ui, x, y, w);   // update1

    // status
    const float px = x + 24, pw = w - 48;
    float sy = y + 18;
    const float top = y;
    // measure first: draw into the panel after its height is known (two passes on a cheap layout)
    auto rows = [&](bool draw_it) {
        float yy = sy;
        auto line = [&](SDL_FColor dot, const std::string& t, const std::string& b) {
            if (draw_it) yy += status_line(a, px, yy, pw, dot, t, b);
            else {
                const float tw = status_title_w(a, t);
                yy += std::max(float(a.text->wrap(b, kBody, pw - 22 - tw).size()) * a.text->line_height(kBody), a.text->line_height(kBody)) + 10;
            }
        };
        line(!a.game.ok ? col::bad : a.game.warning.empty() ? col::ok : col::warn, "Game data",
             !a.game.ok ? a.game.summary + " Set it on the Game Data page."
                        : a.game.warning.empty() ? utf8_of(a.game_dir) : utf8_of(a.game_dir) + ". " + a.game.warning);
        line(have_prog ? col::ok : col::bad, "Game program",
             have_prog ? utf8_of(prog) : prog.filename().string() + " is not beside the launcher (" + utf8_of(a.exe_dir) + ").");
        const SaveSlot* act = nullptr;
        for (const SaveSlot& s : a.slots)
            if (s.active) act = &s;
        line(col::ok, "Save file", act ? slot_line(*act) : "File 1");
        const RunSummary rs = summarize_run(a.run, a.have_run);
        line(rs.kind == LastRun::kSafeNext ? col::bad : rs.kind == LastRun::kUnclean ? col::warn : col::ok, "Last run", rs.text);
        line(col::ok, "Settings", settings_line(a.settings));
        {   // modkit1
            std::string on;
            std::size_t n = 0;
            for (const b3mod::OrderEntry& e : a.mod_order.entries)
                if (e.enabled) on += (n++ ? ", " : "") + e.id;
            std::string more;   // modkit1b: on but left out at the next start ([requires]); safe mode takes none
            if (n && a.mod_sel.mods.size() < n) more += "; " + std::to_string(n - a.mod_sel.mods.size()) + " of them left out (see the Mods page)";
            if (n && rs.kind == LastRun::kSafeNext) more += "; the next start is in safe mode: without mods";
            line(n ? col::warn : col::ok, "Mods", n ? std::to_string(n) + " on: " + on + more + " (the Mods page)" : "None on: the game as on your disc.");
        }
        line(col::ok, "Version", std::string(b3port::version()) + " (built " + b3port::build_date() + "): Burnout 3 native port, a test build");
        return yy;
    };
    const float end = rows(false);
    D::round_rect(SDL_FRect{x, top, w, end - top + 8}, 12, col::panel);
    rows(true);
    y = end + 8 + 18;

    // test builds: a bug report for the developers
    button(ui, SDL_FRect{x, y, 260, 44}, "Make a bug report", Style::kNormal, true,
           "Puts the logs and controls of your last runs, your settings files, the version and a short system description in "
           "one zip on your Desktop (or in Documents) and opens its folder. No game data; your save only when you choose "
           "it. In the game, F10 makes one too.",
           [&a]() { make_report(a); });
    button(ui, SDL_FRect{x + 276, y, 300, 44}, a.report_save ? "Include my save: YES" : "Include my save: NO", Style::kQuiet, true,
           "Whether the report also holds your active save file (profile-N.sav): useful when the problem is in your "
           "progress. Off by default.",
           [&a]() { a.report_save = !a.report_save; });
    y += 44 + 14;
    if (a.have_run && a.run.running) {
        button(ui, SDL_FRect{x, y, 300, 44}, "Clear the crash count", Style::kNormal, true,
               "The last run did not end cleanly. This marks it as ended, so the next start uses your own settings (safe mode is "
               "not chosen by itself).",
               [&a]() {
                   b3settings::save_run_state(utf8_of(run_state_file(a.pref)), b3settings::clean_end());
                   refresh(a);
               });
        button(ui, SDL_FRect{x + 316, y, 200, 44}, "Quit", Style::kQuiet, true, "Closes the launcher.", [&a]() { a.quit = true; });
    } else {
        button(ui, SDL_FRect{x, y, 200, 44}, "Quit", Style::kQuiet, true, "Closes the launcher.", [&a]() { a.quit = true; });
    }
    y += 44;
    a.content_h = y;
}

// ---- the SETTINGS page ----

void refresh_languages(App& a) {   // 1.0.1: the imported languages (b3asset/language.h)
    a.languages.clear();
    for (const auto& l : b3asset::language::on_disc(b3asset::language::imported_root(a.pref)))
        if (l.supported) a.languages.emplace_back(l.code, l.name);
}

void settings_edit(App& a, std::uint32_t id, std::size_t value) {
    b3settings::Edit e;
    e.id = id;
    e.value = value;
    b3settings::Env env;   // 1.0.1: LANGUAGE's values are the imported languages
    env.languages = a.languages;
    if (b3settings::language_edit(a.settings, env, e) || b3settings::apply_edit(a.settings, e)) save_settings(a);
}

void confirm_reset(App& a) {
    Modal m;
    m.title = "Reset every setting?";
    m.text = "Every PC setting goes back to its default (the High preset, a window, no frame limit), and the HD pack and music "
             "folders are cleared. Your saves are not touched.";
    m.buttons = {{"Reset", [&a]() {
                      a.settings = b3settings::Settings{};
                      save_settings(a);
                      refresh_hd(a, true);
                      refresh_music(a, true);
                      a.settings_note = "Every setting is back to its default. " + a.settings_note;
                  }, true},
                 {"Cancel", nullptr}};
    m.focus = 1;
    a.modal = m;
}

void action_row(App& a, std::uint32_t id) {
    if (id == b3settings::kActionImportLanguages) open_dialog(a, DialogKind::kLanguageImage, utf8_of(a.game_dir.parent_path()));   // 1.0.1
    else if (id == b3settings::kActionChooseHdPack) open_dialog(a, DialogKind::kHdFolder, a.settings.hd_pack_dir);
    else if (id == b3settings::kActionClearHdPack) {
        a.settings.hd_pack_dir.clear();
        save_settings(a);
        refresh_hd(a, true);
        a.hd_note = "No HD pack from the next start.";
    } else if (id == b3settings::kActionChooseMusic) open_dialog(a, DialogKind::kMusicFolder, a.settings.music_folder);
    else if (id == b3settings::kActionClearMusic) {   // REMOVE MUSIC FOLDER (pcpage.cpp): the same words
        a.settings.music_folder.clear();
        save_settings(a);
        refresh_music(a, true);
        a.music_note = "No music folder: the game's EA TRAX songs.";
    }
}

// A settings row; `set` takes a new value (none: the PC settings' own row, settings_edit; a mod's row: its own file)
void setting_row(App& a, Ui& ui, const b3settings::Row& row, float x, float y, float w, float h,
                 std::function<void(std::size_t)> set = nullptr) {
    Widget wd;
    wd.r = SDL_FRect{x, y, w, h};
    wd.desc = row.description;
    const std::uint32_t id = row.id;
    const float vw = std::min(330.0f, w * 0.46f);
    const float vx = x + w - vw - 14;
    if (row.kind == b3settings::RowKind::kAction) {
        if (!row.disabled) wd.activate = [&a, id]() { action_row(a, id); };
    } else if (!row.disabled) {
        const b3settings::Row r = row;
        if (!set) set = [&a, id](std::size_t v) { settings_edit(a, id, v); };
        wd.change = [r, set](int dir) {
            if (auto v = step_value(r, dir, false)) set(*v);
        };
        wd.activate = [r, set]() {
            if (auto v = step_value(r, +1, true)) set(*v);
        };
        wd.click = [r, set, vx, vw](float mx, float) {
            int dir = 1;
            if (r.kind != b3settings::RowKind::kToggle && mx >= vx && mx < vx + vw * 0.5f) dir = -1;
            if (r.kind == b3settings::RowKind::kSlider && mx >= vx && mx < vx + vw) {   // a click on the bar sets the level
                const float t = std::clamp((mx - vx - 24) / (vw - 48), 0.0f, 1.0f);
                const std::size_t lv = std::size_t(std::lround(t * float(r.levels - 1)));
                if (lv != r.value) set(lv);
                return;
            }
            if (auto v = step_value(r, dir, true)) set(*v);
        };
    }
    const int i = ui.add(std::move(wd));
    const bool f = ui.focused(i), hv = ui.hovered(i);
    D::round_rect(SDL_FRect{x, y, w, h}, 8, f ? col::panel3 : hv ? col::panel2 : col::panel);
    const float ty = y + (h - a.text->line_height(kBody)) * 0.5f;
    if (a.look.on) {   // the menus: light-blue italic rows, the highlighted one white, larger, in brackets
        const float fs = f ? kBody + 3 : kBody;
        const float fy = y + (h - a.text->line_height(fs)) * 0.5f;
        const float lw = text(a, row.label, x + 22, fy, fs, row.disabled ? col::faint : f ? col::text : col::dim, true);
        if (f) brackets(x + 22, x + 22 + lw, y + 6, h - 12, col::text);
    } else {
        if (f) D::rect(SDL_FRect{x, y + 8, 3, h - 16}, col::accent);
        text(a, row.label, x + 18, ty, kBody, row.disabled ? col::faint : col::text, f);
    }
    const SDL_FColor vc = row.disabled ? col::faint : col::text;
    switch (row.kind) {
    case b3settings::RowKind::kToggle: {
        const bool on = row.value != 0;
        const SDL_FRect pill{x + w - 14 - 52, y + (h - 26) * 0.5f, 52, 26};
        D::round_rect(pill, 13, on ? col::accent : f ? D::rgba(0x434B5C) : col::panel3);
        D::disc(on ? pill.x + pill.w - 13 : pill.x + 13, pill.y + 13, 9.5f, on ? col::ink : col::dim);
        text_right(a, on ? "On" : "Off", pill.x - 12, ty, kBody, on ? col::text : col::dim);
        break;
    }
    case b3settings::RowKind::kChoice: {
        const std::string v = row.value < row.values.size() ? row.values[row.value] : "";
        const bool can_l = !row.disabled && row.value > 0, can_r = !row.disabled && row.value + 1 < row.values.size();
        D::chevron(vx + 10, y + h * 0.5f, 12, -1, 2.0f, can_l ? col::dim : D::alpha(col::faint, 0.4f));
        D::chevron(vx + vw - 10, y + h * 0.5f, 12, +1, 2.0f, can_r ? col::dim : D::alpha(col::faint, 0.4f));
        text_center(a, a.text->elide(v, kBody, vw - 48), vx + vw * 0.5f, ty, kBody, vc, f);
        // the position among the values
        const std::size_t n = row.values.size();
        if (n > 1 && n <= 12) {
            const float dw = 6, gap = 4, total = float(n) * dw + float(n - 1) * gap;
            for (std::size_t k = 0; k < n; ++k)
                D::round_rect(SDL_FRect{vx + vw * 0.5f - total * 0.5f + float(k) * (dw + gap), y + h - 9, dw, 3}, 1.5f,
                              k == row.value ? col::accent : col::panel3);
        }
        break;
    }
    case b3settings::RowKind::kSlider: {
        const float bx = vx + 24, bw = vw - 48 - 70, by = y + h * 0.5f;
        const float t = row.levels > 1 ? float(row.value) / float(row.levels - 1) : 0;
        D::round_rect(SDL_FRect{bx, by - 3, bw, 6}, 3, col::panel3);
        D::round_rect(SDL_FRect{bx, by - 3, bw * t, 6}, 3, col::accent);
        D::disc(bx + bw * t, by, 8, f ? col::text : col::dim);
        text_right(a, row.level_text, vx + vw - 6, ty, kBody, vc, f);
        break;
    }
    case b3settings::RowKind::kAction: {
        const std::string v = row.values.empty() ? "" : row.values[0];
        if (row.disabled) {
            text_right(a, a.text->elide(v, kBody, vw), x + w - 18, ty, kBody, col::dim);
        } else {
            const float bw = std::min(vw, a.text->width(v, kBody) + 36);
            const SDL_FRect b{x + w - 14 - bw, y + 7, bw, h - 14};
            D::round_rect(b, 7, f ? col::accent : col::panel3);
            text_center(a, v, b.x + b.w * 0.5f, b.y + (b.h - a.text->line_height(kBody)) * 0.5f, kBody, f ? col::ink : col::text, f);
        }
        break;
    }
    default: break;
    }
}

// update1: the launcher's own settings under every tab (launcher.ini, not the game's file): CHECK FOR UPDATES and
// Check now with the last answer beside it. Returns the y after them.
float settings_launcher(App& a, Ui& ui, float x, float y, float w) {
    y += 26;
    y += section(a, "Launcher", x + 4, y);
    b3settings::Row r;
    r.kind = b3settings::RowKind::kToggle;
    r.label = "CHECK FOR UPDATES";
    r.values = {"Off", "On"};
    r.value = a.cfg.update.enabled ? 1 : 0;
    r.description = "Each time the launcher starts, asks GitHub whether a newer release of the port is out (shown on the Play page). Nothing else is "
                    "sent; GitHub sees your IP address. Nothing is downloaded until you choose Install update. Off: no request at all.";
    setting_row(a, ui, r, x, y, w, 46, [&a](std::size_t v) { set_update_check(a, v != 0); });
    y += 46 + 10;
    const bool on = a.cfg.update.enabled, busy = update_checking(a);
    button(ui, SDL_FRect{x, y, 200, 44}, busy ? "Checking..." : "Check now", Style::kNormal, on && !busy,
           on ? "Asks GitHub for the latest release now (the launcher also checks each time it starts)."
              : "Turn CHECK FOR UPDATES on first: while it is off the launcher makes no request.",
           [&a]() { start_update_check(a); });
    const update::State st = update_state(a);
    std::string line = update::status_text(a.cfg.update, st);
    if (a.cfg.update.checked_at > 0 && st != update::State::kOff && st != update::State::kChecking)
        line += ". Last checked " + format_time(a.cfg.update.checked_at);
    text(a, a.text->elide(line, kSmall, w - 220), x + 220, y + (44 - a.text->line_height(kSmall)) * 0.5f, kSmall,
         st == update::State::kAvailable ? col::accent : col::faint);
    y += 44;
    if (!a.exe_dir.empty() && !a.inst.job && installer::can_roll_back(a.exe_dir)) {   // 1.1: the previous version is kept
        y += 10;
        button(ui, SDL_FRect{x, y, 260, 44}, "Undo the last update", Style::kNormal, true,
               "Puts the previous version back (the one an update replaced), then restart the launcher. Your saves and settings stay as they are.",
               [&a]() {
                   const installer::Result r = installer::roll_back(a.exe_dir);
                   a.inst.installed = r.ok;
                   a.inst.note = r.ok ? "The previous version is back. Restart the launcher to use it." : "Could not undo the update: " + r.why + ".";
               });
        if (a.inst.installed)
            button(ui, SDL_FRect{x + 276, y, 200, 44}, "Restart now", Style::kPrimary, true, "Starts the launcher that is now in place.",
                   [&a]() { restart_launcher(a); });
        if (!a.inst.note.empty())
            text(a, a.text->elide(a.inst.note, kSmall, w - 500), x + 492, y + (44 - a.text->line_height(kSmall)) * 0.5f, kSmall, col::faint);
        y += 44;
    }
    return y;
}

// ---- issue #8: ADD TO STEAM (steam.h) ----

constexpr std::uintmax_t kSteamImageMax = 32u << 20;   // a picture of the player's own: up to 32 MB

// The B3 icon's files, most likely first: beside the launcher (Windows / a release), else the repo's assets/ (a build
// from the repo), the .app's Contents/Resources (macpkg1)
std::vector<fs::path> icon_files(const App& a) {
    return {a.exe_dir / "b3-icon.png", a.exe_dir / "assets" / "b3-icon.png", a.exe_dir / ".." / "assets" / "b3-icon.png",
            a.exe_dir / ".." / "Resources" / "b3-icon.png", a.cwd / "assets" / "b3-icon.png"};
}

// A picture of the player's own chosen (Cover image... / Background image... / Logo image...): a PNG or JPEG file,
// remembered in launcher.ini and copied into Steam at the next Add to Steam
void steam_choose(App& a, DialogKind kind, const fs::path& p) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(p, ec);
    char head[16] = {};
    std::ifstream f(p, std::ios::binary);
    f.read(head, sizeof head);
    const steam::Bytes h(head, head + std::max<std::streamsize>(0, f.gcount()));
    if (ec || size > kSteamImageMax || steam::image_kind(h).empty()) {
        a.steam_ui.note = "Not used: choose a PNG or JPEG image (up to 32 MB).";
        a.steam_ui.level = 3;
        return;
    }
    std::string& slot = kind == DialogKind::kSteamCover ? a.cfg.steam_cover : kind == DialogKind::kSteamHero ? a.cfg.steam_hero : a.cfg.steam_logo;
    slot = utf8_of(p);
    save_config(config_file(a.pref), a.cfg);
    a.steam_ui.note = "Chosen: it goes into Steam at the next Add to Steam.";
    a.steam_ui.level = 0;
}

// The pictures for Steam: the player's own where chosen, else the port's (steam_art.h); the wide capsule is always
// the port's, the icon the B3 icon (its 512 px file when it is there, else the one built into the launcher)
bool steam_art_files(App& a, std::vector<steam::ArtFile>& out, std::string& why) {
    auto add = [&](steam::Art art, const std::string& chosen, const std::function<steam::Bytes()>& make) {
        if (!chosen.empty()) {
            steam::Bytes b;
            std::string kind;
            if (!steam::read_file(path_from_utf8(chosen), b, kSteamImageMax) || (kind = steam::image_kind(b)).empty()) {
                why = "Cannot read your image " + chosen + ": choose it again, or Use the port's art.";
                return false;
            }
            out.push_back({art, kind, std::move(b)});
            return true;
        }
        out.push_back({art, "png", make()});
        return !out.back().bytes.empty() || (why = "Could not make the pictures.", false);
    };
    const auto icon = [&a]() {
        std::error_code ec;
        steam::Bytes b;
        for (const fs::path& f : icon_files(a))
            if (fs::exists(f, ec) && steam::read_file(f, b, kSteamImageMax) && steam::image_kind(b) == "png") return b;
        steam_art::Image im;
        im.w = im.h = b3port::kAppIconSize;
        im.rgba.assign(b3port::kAppIconRgba, b3port::kAppIconRgba + sizeof b3port::kAppIconRgba);
        return steam_art::encode_png(im, true);
    };
    return add(steam::Art::kCover, a.cfg.steam_cover, [] { return steam_art::encode_png(steam_art::cover(), false); }) &&
           add(steam::Art::kWide, "", [] { return steam_art::encode_png(steam_art::wide(), false); }) &&
           add(steam::Art::kHero, a.cfg.steam_hero, [] { return steam_art::encode_png(steam_art::hero(), false); }) &&
           add(steam::Art::kLogo, a.cfg.steam_logo, [] {
               std::size_t n = 0;
               const std::uint8_t* p = steam_art::logo_png(&n);
               return steam::Bytes(p, p + n);
           }) &&
           add(steam::Art::kIcon, "", icon);
}

// Add to Steam pressed (run on the frame after, so that "Adding..." shows): the launcher into every Steam account's
// library (steam::add), never while Steam runs
void steam_run(App& a) {
    App::SteamUi& u = a.steam_ui;
    u.pending = false;
    auto done = [&u](const std::string& note, int level) {
        u.note = note;
        u.level = level;
        std::printf("b3launcher: Add to Steam: %s\n", note.c_str());
    };
    if (u.checks && !u.places) return done("A check run writes only to a made-up Steam folder.", 3);
    if (a.self_exe.empty()) return done("Cannot find the launcher's own program file, so it cannot be added.", 3);
    if (u.running ? *u.running : steam::steam_running()) {
        std::string m = "Steam is running. Close Steam, then press Add to Steam again: Steam rewrites its list of games when it closes.";
        if (on_steam_deck()) m += " On a Steam Deck, do this in Desktop Mode: exit Steam from its icon in the taskbar first.";
        return done(m, 3);
    }
    steam::Request r;
    r.roots = steam::find_roots(u.places ? *u.places : steam::this_places(), steam::this_os());
    r.shortcut.exe = utf8_of(a.self_exe);
    r.shortcut.start_dir = utf8_of(a.self_exe.parent_path());
    std::string why;
    if (!r.roots.empty() && !steam_art_files(a, r.art, why)) return done(why, 3);
    const steam::Result res = steam::add(r);
    std::string note = steam::summary(res);
    bool flatpak = false, warn = false;
    for (const steam::AccountResult& acc : res.accounts) {
        flatpak = flatpak || (acc.ok && utf8_of(acc.config).find("com.valvesoftware.Steam") != std::string::npos);
        warn = warn || !acc.why.empty() || !acc.art_why.empty();
    }
    if (flatpak)
        note += " Flatpak's Steam needs to see this folder: flatpak override --user --filesystem=" + utf8_of(a.self_exe.parent_path()) +
                " com.valvesoftware.Steam";
    done(note, res.ok_count() == 0 ? 3 : warn ? 2 : 1);
}

// Settings > Launcher's Steam part: Add to Steam with its result, the player's own pictures, and why it helps
float settings_steam(App& a, Ui& ui, float x, float y, float w) {
    App::SteamUi& u = a.steam_ui;
    y += 26;
    y += section(a, "Steam", x + 4, y);
    const float bw = 200;
    u.widget = button(ui, SDL_FRect{x, y, bw, 44}, u.pending ? "Adding..." : "Add to Steam", Style::kNormal, !u.pending,
                      "Adds Burnout 3: Takedown to the Steam library of every account on this computer, with its own cover, background "
                      "and logo. Close Steam first: it rewrites its list of games when it closes.",
                      [&a]() {
                          a.steam_ui.pending = true;
                          a.steam_ui.note.clear();
                      });
    const SDL_FColor nc = u.level == 1 ? col::ok : u.level == 2 ? col::warn : u.level == 3 ? col::bad : col::faint;
    const std::string line = u.pending ? "Adding to Steam..." : u.note.empty() ? "Big Picture and the Steam Deck then show it with its own pictures." : u.note;
    const float lh = a.text->line_height(kSmall);
    const float ph = float(a.text->wrap(line, kSmall, w - bw - 20).size()) * lh;   // beside the button, centred on it
    y += std::max(44.0f, paragraph(a, line, x + bw + 20, y + std::max(0.0f, (44 - ph) * 0.5f), w - bw - 20, kSmall, nc));
    y += 10;
    // the player's own pictures (PNG / JPEG): each button with what it uses under it, then back to the port's own
    struct Pick { const char* label; DialogKind kind; std::string* chosen; const char* desc; };
    const Pick picks[] = {
        {"Cover image...", DialogKind::kSteamCover, &a.cfg.steam_cover,
         "Choose your own picture for the library's cover (PNG or JPEG; 600 x 900 fits best). It goes into Steam at the next Add to Steam."},
        {"Background image...", DialogKind::kSteamHero, &a.cfg.steam_hero,
         "Choose your own picture for the game page's background (PNG or JPEG; 1920 x 620 or 3840 x 1240 fits best). It goes into Steam at the next Add to Steam."},
        {"Logo image...", DialogKind::kSteamLogo, &a.cfg.steam_logo,
         "Choose your own logo shown over the background (a PNG with a transparent background fits best). It goes into Steam at the next Add to Steam."},
    };
    const float gap = 12, pw = std::min(200.0f, (w - 3 * gap) / 4);
    float px = x;
    for (const Pick& p : picks) {
        const DialogKind kind = p.kind;
        std::string* chosen = p.chosen;
        button(ui, SDL_FRect{px, y, pw, 44}, p.label, Style::kQuiet, !u.pending, p.desc, [&a, kind, chosen]() { open_dialog(a, kind, *chosen); });
        const std::string what = chosen->empty() ? a.text->translated("The port's own") : utf8_of(path_from_utf8(*chosen).filename());
        text_center(a, a.text->elide(what, kSmall, pw), px + pw * 0.5f, y + 48, kSmall, chosen->empty() ? col::faint : col::dim);
        px += pw + gap;
    }
    const bool any = !a.cfg.steam_cover.empty() || !a.cfg.steam_hero.empty() || !a.cfg.steam_logo.empty();
    button(ui, SDL_FRect{px, y, pw, 44}, "Use the port's art", Style::kQuiet, any && !u.pending,
           "Forgets the pictures you chose: the next Add to Steam uses the port's own cover, background and logo.", [&a]() {
               a.cfg.steam_cover.clear();
               a.cfg.steam_hero.clear();
               a.cfg.steam_logo.clear();
               save_config(config_file(a.pref), a.cfg);
               a.steam_ui.note = "The next Add to Steam uses the port's own pictures.";
               a.steam_ui.level = 0;
           });
    y += 44 + 4 + lh + 12;
    // 1.2.1: art the player picks themselves (the port ships only its own pictures): SteamGridDB's search, in the browser
    button(ui, SDL_FRect{x, y, 280, 44}, "Find art online...", Style::kQuiet, true,
           "Opens SteamGridDB's pictures for Burnout 3: Takedown in your browser; download a cover, background or logo you like, "
           "then choose it with the buttons above.",
           []() { SDL_OpenURL("https://www.steamgriddb.com/search/grids?term=Burnout%203%20Takedown"); });
    y += 44 + 12;
    y += paragraph(a, "Started from Steam, the game has Steam Input and the overlay, and Steam's desktop controller layout no longer "
                      "clicks on the desktop while you play.",
                   x, y, w, kSmall, col::faint) + 4;
    return y;
}

void page_settings(App& a, Ui& ui, float x, float y, float w) {
    const std::string hd_status = a.hd_note;
    const b3settings::Content c = settings_content(a.settings, hd_status, a.hd.images > 0, a.languages, a.language_note);
    if (c.tabs.empty()) return;
    a.settings_tab = std::min(a.settings_tab, c.tabs.size() - 1);
    // the tab bar: one widget (left / right switch, a click picks)
    const float th = 44;
    std::vector<float> tx;
    float total = 0;
    for (const auto& t : c.tabs) total += a.text->width(t.label, kBody, true) + 40;
    {
        Widget wd;
        wd.r = SDL_FRect{x, y, w, th};
        wd.desc = "The settings' groups, as in the game. Left / right (or PageUp / PageDown, LT / RT) switch them.";
        const std::size_t n = c.tabs.size();
        wd.change = [&a, n](int dir) { a.settings_tab = (a.settings_tab + n + std::size_t(dir + int(n))) % n; };
        std::vector<std::pair<float, float>> spans;
        float cx = x;
        for (const auto& t : c.tabs) {
            const float tw = a.text->width(t.label, kBody, true) + 40;
            spans.push_back({cx, tw});
            cx += tw;
        }
        wd.click = [&a, spans](float mx, float) {
            for (std::size_t k = 0; k < spans.size(); ++k)
                if (mx >= spans[k].first && mx < spans[k].first + spans[k].second) a.settings_tab = k;
        };
        wd.activate = [&a, n]() { a.settings_tab = (a.settings_tab + 1) % n; };
        const int i = ui.add(std::move(wd));
        const bool f = ui.focused(i);
        D::round_rect(SDL_FRect{x, y, std::min(w, total), th}, 10, col::panel);
        if (f) focus_ring(SDL_FRect{x, y, std::min(w, total), th}, 10);
        for (std::size_t k = 0; k < c.tabs.size(); ++k) {
            const auto [sx, sw] = spans[k];
            const bool sel = k == a.settings_tab;
            if (sel) D::round_rect(SDL_FRect{sx + 4, y + 4, sw - 8, th - 8}, 8, col::accent);
            text_center(a, c.tabs[k].label, sx + sw * 0.5f, y + (th - a.text->line_height(kBody)) * 0.5f, kBody,
                        sel ? col::ink : col::dim, true);
        }
    }
    y += th + 18;
    const auto& rows = c.tabs[a.settings_tab].rows;
    for (const auto& r : rows) {
        if (r.kind == b3settings::RowKind::kSection) {
            y += 6;
            y += section(a, r.label, x + 4, y);
            continue;
        }
        setting_row(a, ui, r, x, y, w, 46);
        y += 46 + 6;
    }
    y += 18;
    button(ui, SDL_FRect{x, y, 260, 44}, "Reset all settings...", Style::kDanger, true,
           "Every PC setting back to its default (asks first). Your saves are not touched.", [&a]() { confirm_reset(a); });
    text(a, a.text->elide(a.settings_note.empty() ? "Settings file: " + utf8_of(settings_file(a.pref)) : a.settings_note, kSmall, w - 290),
         x + 280, y + (44 - a.text->line_height(kSmall)) * 0.5f, kSmall, col::faint);
    y += 44;
    y = settings_launcher(a, ui, x, y, w);   // update1
    y = settings_steam(a, ui, x, y, w);      // issue #8
    a.content_h = y;
}

// ---- the HD TEXTURES page ----

// The HD pack the page points to: its creator's own post and download (never mirrored or bundled by the port).
// An empty download URL hides that button. The download is the creator's own MediaFire link.
const char* const kHdPackName = "Burnout 3: Takedown USA HD Remaster";
const char* const kHdPackAuthor = "Panda_Venom";
const char* const kHdPackThread = "https://gbatemp.net/threads/burnout-3-takedown-usa-hd-remaster.621068/";
const char* const kHdPackDownload = "https://www.mediafire.com/file/gec85ndkk6b4wm4/SLUS-21050.7z/file";
const char* const kHdPackSupport = "https://ko-fi.com/pandavenom";

void page_hd(App& a, Ui& ui, float x, float y, float w) {
    const bool set = !a.settings.hd_pack_dir.empty();
    const float top = y;
    float yy = y + 20;
    yy += section(a, "Folder", x + 24, yy);
    yy += paragraph(a, set ? a.settings.hd_pack_dir : "None chosen.", x + 24, yy, w - 48, 18, set ? col::text : col::dim, true) + 8;
    SDL_FColor dot = col::faint;
    std::string st = "The game draws its own textures.";
    if (set) {
        if (!a.hd.exists) { dot = col::bad; st = "The folder is not there (moved or on a drive that is not connected)."; }
        else if (a.hd.images == 0) { dot = col::warn; st = "No .dds / .png images in it: not a texture pack folder?"; }
        else {
            dot = col::ok;
            st = std::to_string(a.hd.images) + (a.hd.more ? "+" : "") + " images (.dds / .png). Used from the next start";
            st += a.settings.hd_textures ? "." : ", but HD TEXTURES is off (Settings > Graphics).";
            if (!a.settings.hd_textures) dot = col::warn;
        }
    }
    yy += status_line(a, x + 24, yy, w - 48, dot, "Status", st);
    if (!a.hd_note.empty()) yy += status_line(a, x + 24, yy, w - 48, col::faint, "Last change", a.hd_note);
    D::round_rect(SDL_FRect{x, top, w, yy - top + 10}, 12, col::panel);
    // drawn again over the panel (the panel's height is known only now)
    yy = y + 20;
    yy += section(a, "Folder", x + 24, yy);
    yy += paragraph(a, set ? a.settings.hd_pack_dir : "None chosen.", x + 24, yy, w - 48, 18, set ? col::text : col::dim, true) + 8;
    yy += status_line(a, x + 24, yy, w - 48, dot, "Status", st);
    if (!a.hd_note.empty()) yy += status_line(a, x + 24, yy, w - 48, col::faint, "Last change", a.hd_note);
    y = yy + 10 + 22;

    float bx = x;
    button(ui, SDL_FRect{bx, y, 230, 48}, "Choose folder...", Style::kPrimary, true,
           "The folder of a PCSX2 texture pack for this game (its SLUS-21050 folder, or the folder above it). With a controller: the launcher's own folder list.",
           [&a]() { open_dialog(a, DialogKind::kHdFolder, a.settings.hd_pack_dir); });
    bx += 246;
    button(ui, SDL_FRect{bx, y, 190, 48}, "Type a path...", Style::kNormal, true, "Type or paste the folder's path instead. With a controller: an on-screen keyboard.",
           [&a]() { ask_path(a, DialogKind::kHdFolder, "HD texture pack folder", a.settings.hd_pack_dir); });
    bx += 206;
    button(ui, SDL_FRect{bx, y, 150, 48}, "Open", Style::kNormal, set && a.hd.exists, "Opens the folder in the file manager.",
           [&a]() { open_folder(path_from_utf8(a.settings.hd_pack_dir), &a.hd_note); });
    bx += 166;
    button(ui, SDL_FRect{bx, y, 150, 48}, "Remove", Style::kDanger, set, "Stops using the folder from the next start (its files stay).",
           [&a]() { action_row(a, b3settings::kActionClearHdPack); });
    y += 48 + 22;
    {
        // the in-game row (b3settings::build_content's GRAPHICS tab: HD TEXTURES, there while a folder is chosen); a
        // row of its own when none is, so the switch is reachable here too (Settings::hd_textures either way)
        b3settings::Row r;
        for (const auto& t : settings_content(a.settings, a.hd_note, a.hd.images > 0, a.languages, a.language_note).tabs)
            for (const auto& row : t.rows)
                if (row.kind == b3settings::RowKind::kToggle && row.label == "HD TEXTURES") r = row;
        if (r.label.empty()) {
            r.kind = b3settings::RowKind::kToggle;
            r.label = "HD TEXTURES";
            r.values = {"Off", "On"};
            r.value = a.settings.hd_textures ? 1 : 0;
            r.description = "The HD texture pack given at start (applies when the game next starts).";
        }
        const float h = 46;
        setting_row(a, ui, r, x, y, w, h);
        ui.out.back().activate = [&a]() { a.settings.hd_textures = !a.settings.hd_textures; save_settings(a); };
        ui.out.back().click = [&a](float, float) { a.settings.hd_textures = !a.settings.hd_textures; save_settings(a); };
        ui.out.back().change = [&a](int dir) {
            const bool v = dir > 0;
            if (a.settings.hd_textures != v) { a.settings.hd_textures = v; save_settings(a); }
        };
        y += h + 22;
    }
    y += paragraph(a, "Packs made for PCSX2 work as they are: choose the pack's SLUS-21050 folder (in PCSX2's textures "
                      "folder), or the folder above it. The game reads it at its next start; this page only checks that "
                      "the folder has images in it.",
                   x + 4, y, w - 8, kSmall, col::faint);
    // the pack to get: credit and links to its creator's post
    y += 22;
    y += section(a, "Get the HD pack", x + 4, y);
    y += paragraph(a, std::string(kHdPackName) + " by " + kHdPackAuthor + ": new textures for the whole game, made for "
                      "PCSX2. Download it (a .7z archive), extract it (7-Zip on Windows, The Unarchiver or Keka on a Mac), then "
                      "choose the extracted SLUS-21050 folder above. The minor texture glitches the post mentions are an "
                      "emulator bug: the port draws the pack without them.",
                   x + 4, y, w - 8, kBody, col::text) + 12;
    {
        float lx = x;
        if (*kHdPackDownload) {
            const std::string url = kHdPackDownload;
            button(ui, SDL_FRect{lx, y, 250, 48}, "Download (MediaFire)", Style::kNormal, true,
                   "Opens the pack's download (" + url + ") in your browser. The launcher downloads nothing itself.",
                   [url]() { SDL_OpenURL(url.c_str()); });
            lx += 266;
        }
        const std::string url = kHdPackThread;
        button(ui, SDL_FRect{lx, y, 200, 48}, "Creator's post", Style::kNormal, true,
               std::string("Opens ") + kHdPackAuthor + "'s post about the pack (" + url + ") in your browser.",
               [url]() { SDL_OpenURL(url.c_str()); });
        lx += 216;
        const std::string kofi = kHdPackSupport;
        button(ui, SDL_FRect{lx, y, 230, 48}, "Support the creator", Style::kNormal, true,
               std::string("Opens ") + kHdPackAuthor + "'s Ko-fi page (" + kofi + ") in your browser.",
               [kofi]() { SDL_OpenURL(kofi.c_str()); });
        y += 48 + 12;
    }
    a.content_h = y;
}

// ---- the CREDITS page (2026-11-10) ----
// The author and the thank-yous, the same list as README.md's Acknowledgements. Each entry is a row that opens its
// web page (Enter / A or a click). The licences themselves ship as THIRD-PARTY-NOTICES.txt and LICENSES/ beside the game.

struct Credit { const char* name; const char* what; const char* url; };
const Credit kCreditList[] = {
    {"Hidden Palace", "For preserving the Burnout prototype discs: their debug builds, link maps and symbols give the "
     "decompilation its names, and through it this port's.", "https://hiddenpalace.org/"},
    {"The Cutting Room Floor", "For their research into those prototypes and what they contain.", "https://tcrf.net/"},
    {"Takedawn (xenophobentx)", "Research notes on the Xbox version: the race clock, crash cameras, effects, HUD and "
     "track formats.", "https://github.com/xenophobentx/takedawn"},
    {"sp00nznet: burnout3 and xboxrecomp", "The first Xbox recompilation and its notes on what booting the game needs.",
     "https://github.com/sp00nznet/burnout3"},
    {"burnout3-sw (antoxa2584x)", "Notes on the reflection cube, the bloom / blur composite and real-time races.",
     "https://github.com/antoxa2584x/burnout3-sw"},
    {"Nahelam", "The most extensive public reverse engineering of the retail game, and its function names.",
     "https://github.com/Nahelam/PS2-Game-Mods"},
    {"ThirteenAG and Aero_", "Their PS2 widescreen patches located the game's widescreen and HUD code.",
     "https://github.com/ThirteenAG/WidescreenFixesPack"},
    {"librw (aap)", "A reference for RenderWare's PS2 rasters and geometry packets.", "https://github.com/aap/librw"},
    {"Burn3 (UwUCammie)", "Another PC project for the game.", "https://github.com/UwUCammie/Burn3"},
    {"PS2Recomp (ran-j) and burnout3-pc", "Other PC projects for the game.", "https://github.com/ran-j/PS2Recomp"},
    {"Panda_Venom", "The Burnout 3: Takedown USA HD Remaster, the HD texture pack for PCSX2 that the HD Textures page "
     "links to and this port can use.", "https://gbatemp.net/threads/burnout-3-takedown-usa-hd-remaster.621068/"},
    {"PCSX2 and mymcplus", "Their texture-pack and memory-card formats, which this port reads, and PS2 save tooling.",
     "https://pcsx2.net/"},
    {"splat and decomp.me", "The decompilation tooling.", "https://decomp.me/"},
    {"ps2tek and PS2SDK", "The PS2 homebrew community's hardware references.", "https://psi-rockin.github.io/ps2tek/"},
};

void page_credits(App& a, Ui& ui, float x, float y, float w) {
    const float top = y;
    float yy = y + 20;
    // the author's panel: its height measured first, then the panel and its text
    const char* who = "Burnout 3: Takedown, native on Windows, Linux and macOS, by u0rhs.";
    const char* game = "Burnout 3: Takedown is by Criterion Games, published by Electronic Arts. This port needs your own "
                       "copy of the game and contains none of its data.";
    // update1: the version and the update check's last answer
    const update::State ust = update_state(a);
    const std::string ver = "Version " + std::string(b3port::version()) + " (built " + b3port::build_date() + "). " +
                            update::status_text(a.cfg.update, ust) + ".";
    float hh = a.text->line_height(kSection + 1.5f) + 6;
    hh += a.text->wrap(who, kBody, w - 48, true).size() * a.text->line_height(kBody) + 6;
    hh += a.text->wrap(game, kSmall, w - 48).size() * a.text->line_height(kSmall);
    hh += 8 + a.text->wrap(ver, kSmall, w - 48).size() * a.text->line_height(kSmall);
    D::round_rect(SDL_FRect{x, top, w, hh + 40}, 12, col::panel);
    yy += section(a, "This port", x + 24, yy);
    yy += paragraph(a, who, x + 24, yy, w - 48, kBody, col::text, true) + 6;
    yy += paragraph(a, game, x + 24, yy, w - 48, kSmall, col::dim) + 8;
    yy += paragraph(a, ver, x + 24, yy, w - 48, kSmall, ust == update::State::kAvailable ? col::accent : col::faint);
    y = top + hh + 40 + 22;
    if (ust == update::State::kAvailable) {
        const std::string url = a.cfg.update.latest.url;
        button(ui, SDL_FRect{x, y - 8, 280, 44}, "Download " + a.cfg.update.latest.tag, Style::kNormal, true,
               "Opens the release's page (" + url + ") in your browser. The launcher installs nothing itself.",
               [url]() { SDL_OpenURL(url.c_str()); });
        y += 44 + 14;
    }

    y += section(a, "Thank you", x + 4, y);
    for (const Credit& c : kCreditList) {
        const float pad = 14;
        const auto lines = a.text->wrap(c.what, kSmall, w - 2 * pad);
        const float h = pad + a.text->line_height(kBody) + 4 + lines.size() * a.text->line_height(kSmall) + 4 +
                        a.text->line_height(kSmall - 1) + pad;
        Widget wd;
        wd.r = SDL_FRect{x, y, w, h};
        wd.desc = std::string("Opens ") + c.url + " in your browser.";
        const std::string url = c.url;
        wd.activate = [url]() { SDL_OpenURL(url.c_str()); };
        const int i = ui.add(std::move(wd));
        const bool f = ui.focused(i), hv = ui.hovered(i);
        D::round_rect(SDL_FRect{x, y, w, h}, 10, f || hv ? col::panel3 : col::panel);
        if (f) focus_ring(SDL_FRect{x, y, w, h}, 10);
        float ty = y + pad;
        text(a, c.name, x + pad, ty, kBody, col::text, true);
        ty += a.text->line_height(kBody) + 4;
        for (const std::string& l : lines) {
            text(a, l, x + pad, ty, kSmall, col::dim);
            ty += a.text->line_height(kSmall);
        }
        text(a, c.url, x + pad, ty + 4, kSmall - 1, col::accent);
        y += h + 10;
    }
    y += 12;
    y += paragraph(a, "The licences of the libraries and code this port uses are in THIRD-PARTY-NOTICES.txt and the LICENSES "
                      "folder next to the game.",
                   x + 4, y, w - 8, kSmall, col::faint);
    a.content_h = y;
}

// ---- the MUSIC page ----
// The custom soundtrack (remaster-features §6): the folder, its songs (the game's own scan and tags: custom_music.h),
// and the in-game PLAYLIST and CUSTOM MUSIC VOLUME rows. The same settings as SETTINGS > INTERFACE > MUSIC in the game
// (Settings::music_folder, playlist_mode and custom_music_volume in pc_settings.ini), so a change here shows there and
// the other way round. PLAYLIST EA Trax only (the default) is the feature off: the game's music untouched.

void page_music(App& a, Ui& ui, float x, float y, float w) {
    const bool set = !a.settings.music_folder.empty();
    const MusicCheck& m = a.music;
    const bool have = set && m.songs > 0;
    const int mode = std::clamp(a.settings.playlist_mode, 0, 2);
    SDL_FColor dot = col::faint;
    std::string st = "Choose a folder of MP3, FLAC, Ogg Vorbis or WAV files (subfolders too).";
    if (set) {
        if (!m.exists) { dot = col::bad; st = "The folder is not there (moved, or on a drive that is not connected)."; }
        else if (!have) { dot = col::warn; st = "No MP3, FLAC, Ogg Vorbis or WAV files in it or its subfolders."; }
        else {
            dot = col::ok;
            st = std::to_string(m.songs) + (m.songs == 1 ? " song" : " songs") + " found (MP3, FLAC, Ogg Vorbis and WAV, subfolders included).";
            if (m.songs > 200) st += " The game's EA TRAX page lists the first 200.";   // custom_soundtrack.h kMaxCustom
        }
        if (!m.error.empty()) st += " Not every folder could be read: " + m.error + ".";
    }
    // what the radio plays with these settings (custom_soundtrack.h: no songs, the game's)
    SDL_FColor pdot = col::ok;
    std::string pl;
    if (mode == 0) {
        pdot = col::faint;
        pl = "EA Trax only: the game's own songs (your music is off).";
        if (have) pl += " Choose Custom only or Mixed in PLAYLIST below to hear your songs.";
    } else if (!have) {
        pdot = col::warn;
        pl = std::string(mode == 1 ? "Custom only" : "Mixed") + ", but there are no songs of yours to play: the game's EA TRAX songs play.";
    } else {
        pl = mode == 1 ? "Custom only: the songs of your music folder." : "Mixed: your songs and the game's EA TRAX songs.";
        pl += " Each song plays as set on the game's EA TRAX page.";
    }
    const float lh = a.text->line_height(kBody);
    auto status_h = [&](const std::string& body) {   // status_line's height, measured
        return std::max(float(a.text->wrap(body, kBody, w - 48 - 22 - 150).size()) * lh, lh) + 10;
    };
    const float top = y;
    const std::string path = set ? a.settings.music_folder : "None chosen.";
    auto body = [&](bool draw_it) {
        float yy = top + 20;
        if (draw_it) section(a, "Music folder", x + 24, yy);
        yy += a.text->line_height(a.look.on ? kSection + 1.5f : kSection) + 6;
        if (draw_it) paragraph(a, path, x + 24, yy, w - 48, 18, set ? col::text : col::dim, true);
        yy += float(a.text->wrap(path, 18, w - 48, true).size()) * a.text->line_height(18) + 8;
        if (draw_it) status_line(a, x + 24, yy, w - 48, dot, "Songs", st);
        yy += status_h(st);
        if (draw_it) status_line(a, x + 24, yy, w - 48, pdot, "Radio", pl);
        yy += status_h(pl);
        if (!a.music_note.empty()) {
            if (draw_it) status_line(a, x + 24, yy, w - 48, col::faint, "Last change", a.music_note);
            yy += status_h(a.music_note);
        }
        return yy + 10;
    };
    const float end = body(false);
    D::round_rect(SDL_FRect{x, top, w, end - top}, 12, col::panel);
    body(true);
    y = end + 22;

    // the buttons (a row that wraps, as on the Game Data page)
    float bx = x;
    auto at = [&](float bw) {
        if (bx > x && bx + bw > x + w) { bx = x; y += 48 + 16; }
        const SDL_FRect r{bx, y, bw, 48};
        bx += bw + 16;
        return r;
    };
    button(ui, at(230), "Choose folder...", Style::kPrimary, true,
           "A folder of your music: MP3, FLAC, Ogg Vorbis or WAV files (subfolders too). In the game: MUSIC FOLDER. With a controller: the launcher's own folder list.",
           [&a]() { action_row(a, b3settings::kActionChooseMusic); });
    button(ui, at(190), "Type a path...", Style::kNormal, true, "Type or paste the folder's path instead. With a controller: an on-screen keyboard.",
           [&a]() { ask_path(a, DialogKind::kMusicFolder, "Music folder", a.settings.music_folder); });
    button(ui, at(130), "Open", Style::kNormal, set && m.exists, "Opens the folder in the file manager.",
           [&a]() { open_folder(path_from_utf8(a.settings.music_folder), &a.music_note); });
    button(ui, at(150), "Remove", Style::kDanger, set,
           "Stop using the chosen folder (the files stay). In the game: REMOVE MUSIC FOLDER.",
           [&a]() { action_row(a, b3settings::kActionClearMusic); });
    y += 48 + 22;

    // the in-game PLAYLIST row: its values, words and description (b3settings::build_content), edited as the game does
    setting_row(a, ui, playlist_row(a.settings), x, y, w, 46);
    y += 46 + 22;
    setting_row(a, ui, custom_music_volume_row(a.settings), x, y, w, 46);   // musicvol1
    y += 46 + 22;

    // nowplaying1: OTHER MUSIC APPS (src/native/now_playing.h) -- what the system's media controls say plays now
    {
        const bool available = b3nowplaying::system_interface() != nullptr, on = a.settings.other_music_apps != 0;
        if (on && available && !a.now_playing)
            a.now_playing = std::make_unique<b3nowplaying::Poller>([](std::string* why) { return b3nowplaying::system_provider(why); });
        if (a.now_playing) a.now_playing->set_enabled(on);
        const b3nowplaying::Poller::Snapshot snap = a.now_playing ? a.now_playing->snapshot() : b3nowplaying::Poller::Snapshot{};
        const bool reading = snap.reads > 0 || (!snap.why.empty() && snap.why != "off");
        const std::string what = b3nowplaying::describe(snap.track);
        int level = 0;
        const std::string st = now_playing_status(on, available, reading, snap.ok, snap.why, what, &level);
        const SDL_FColor ndot = level == 1 ? col::ok : level == 2 ? col::warn : col::faint;
        const float ntop = y;
        auto panel = [&](bool draw_it) {
            float yy = ntop + 20;
            if (draw_it) section(a, "Other music apps", x + 24, yy);
            yy += a.text->line_height(a.look.on ? kSection + 1.5f : kSection) + 6;
            if (draw_it) status_line(a, x + 24, yy, w - 48, ndot, "Detected", st);
            yy += std::max(float(a.text->wrap(st, kBody, w - 48 - 22 - 150).size()) * lh, lh) + 10;
            return yy + 10;
        };
        const float nend = panel(false);
        D::round_rect(SDL_FRect{x, ntop, w, nend - ntop}, 12, col::panel);
        panel(true);
        y = nend + 22;
        setting_row(a, ui, other_music_apps_row(a.settings, on && snap.ok ? what : std::string()), x, y, w, 46);
        y += 46 + 22;
    }

    // the first songs, as the EA TRAX popup names them (their tags, else the file name)
    if (have) {
        const float ptop = y, avail = w - 48 - 40;
        const float c1 = x + 24 + 40, c2 = c1 + avail * 0.44f, c3 = c1 + avail * 0.74f;
        const std::size_t more = m.songs - m.first.size();
        const std::string tail = more ? "... and " + std::to_string(more) + " more." : std::string();
        auto list = [&](bool draw_it) {
            float yy = ptop + 20;
            if (draw_it) section(a, m.songs > m.first.size() ? "The first songs" : "Songs", x + 24, yy);
            yy += a.text->line_height(a.look.on ? kSection + 1.5f : kSection) + 6;
            if (draw_it) {
                text(a, "TITLE", c1, yy, kSmall, col::faint, true);
                text(a, "ARTIST", c2, yy, kSmall, col::faint, true);
                text(a, "ALBUM", c3, yy, kSmall, col::faint, true);
            }
            yy += a.text->line_height(kSmall) + 4;
            for (std::size_t k = 0; k < m.first.size(); ++k) {
                const MusicSong& s = m.first[k];
                if (draw_it) {
                    text_right(a, std::to_string(k + 1), c1 - 14, yy, kBody, col::faint);
                    text(a, a.text->elide(s.title, kBody, c2 - c1 - 16, true), c1, yy, kBody, col::text, true);
                    text(a, a.text->elide(s.artist, kBody, c3 - c2 - 16), c2, yy, kBody, col::dim);
                    text(a, a.text->elide(s.album, kBody, x + w - 24 - c3), c3, yy, kBody, col::faint);
                }
                yy += lh + 6;
            }
            if (!tail.empty()) {
                if (draw_it) text(a, tail, c1, yy, kBody, col::faint);
                yy += lh + 6;
            }
            return yy + 10;
        };
        const float lend = list(false);
        D::round_rect(SDL_FRect{x, ptop, w, lend - ptop}, 12, col::panel);
        list(true);
        y = lend + 22;
    }
    y += paragraph(a, "Your songs play through the game's radio: its RADIO VOLUME, its shuffle setting and the EA TRAX popup with "
                      "each song's title, artist and album from its tags. The game reads the folder when its next song starts. "
                      "In the game these are SETTINGS > INTERFACE > MUSIC: a change in either place shows in the other. "
                      "OTHER MUSIC APPS shows the song another app plays (Spotify, a browser, any player in the system's "
                      "media controls) in the same popup; its sound stays in that app.",
                   x + 4, y, w - 8, kSmall, col::faint);
    a.content_h = y;
}

// ---- the MODS page (modkit1, docs/modding/DESIGN.md §1) ----

void page_mods(App& a, Ui& ui, float x, float y, float w) {
    const fs::path folder = b3mod::mods_folder(a.pref);
    std::size_t on = 0;
    for (const b3mod::OrderEntry& e : a.mod_order.entries) on += e.enabled ? 1 : 0;
    const float lh = a.text->line_height(kBody);
    auto status_h = [&](const std::string& body) { return std::max(float(a.text->wrap(body, kBody, w - 48 - 22 - 150).size()) * lh, lh) + 10; };
    const std::string count = a.mods.empty() ? "None installed. Put each mod in its own folder in the mods folder (a folder with a mod.ini), then Look again."
                                             : std::to_string(a.mods.size()) + (a.mods.size() == 1 ? " mod" : " mods") + " installed, " +
                                                   std::to_string(on) + " on.";
    const std::string order = "The game reads a file from the first mod in this list that has it and is on, else from your disc. "
                              "Nothing is written to the disc or its folder.";
    // modkit1b: the paths more than one mod that is on has (the first one wins)
    std::string clash;
    if (!a.mod_conflicts.empty()) {
        std::set<std::string> who;
        for (const b3mod::FileConflict& c : a.mod_conflicts) {
            who.insert(c.winner);
            who.insert(c.hidden.begin(), c.hidden.end());
        }
        clash = std::to_string(a.mod_conflicts.size()) + (a.mod_conflicts.size() == 1 ? " file is" : " files are") + " in more than one mod that is on (" +
                std::to_string(who.size()) + " mods): the mod higher in the list wins. Each card lists its own.";
    }
    const std::string path = utf8_of(folder);
    const float top = y;
    auto body = [&](bool draw_it) {
        float yy = top + 20;
        if (draw_it) section(a, "Mods folder", x + 24, yy);
        yy += a.text->line_height(a.look.on ? kSection + 1.5f : kSection) + 6;
        if (draw_it) paragraph(a, path, x + 24, yy, w - 48, 18, col::text, true);
        yy += float(a.text->wrap(path, 18, w - 48, true).size()) * a.text->line_height(18) + 8;
        if (draw_it) status_line(a, x + 24, yy, w - 48, a.mods.empty() ? col::faint : on ? col::warn : col::ok, "Installed", count);
        yy += status_h(count);
        if (draw_it) status_line(a, x + 24, yy, w - 48, col::faint, "Order", order);
        yy += status_h(order);
        if (!clash.empty()) {
            if (draw_it) status_line(a, x + 24, yy, w - 48, col::warn, "Conflicts", clash);
            yy += status_h(clash);
        }
        if (!a.mods_note.empty()) {
            if (draw_it) status_line(a, x + 24, yy, w - 48, a.mods_note_bad ? col::bad : col::faint, "Last change", a.mods_note);
            yy += status_h(a.mods_note);
        }
        return yy + 10;
    };
    const float end = body(false);
    D::round_rect(SDL_FRect{x, top, w, end - top}, 12, col::panel);
    body(true);
    y = end + 22;
    button(ui, SDL_FRect{x, y, 250, 48}, "Open mods folder", Style::kPrimary, true,
           "Opens the mods folder in the file manager (made when it is not there yet). Each mod is a folder in it.",
           [&a, folder]() { open_folder(folder, &a.mods_note); });
    button(ui, SDL_FRect{x + 266, y, 170, 48}, "Look again", Style::kNormal, true, "Reads the mods folder again (mods added or removed since).",
           [&a]() {
               refresh_mods(a);
               a.mods_note = "Read the mods folder again: " + std::to_string(a.mods.size()) + (a.mods.size() == 1 ? " mod." : " mods.");
               a.mods_note_bad = false;
           });
    button(ui, SDL_FRect{x + 452, y, 190, 48}, "Add mod...", Style::kNormal, true,   // modkit1b
           "Installs a mod from a .b3mod or .zip file: unpacked into its own folder in the mods folder (checked first: nothing "
           "outside that folder is written). New mods are off.",
           [&a]() { open_dialog(a, DialogKind::kModArchive, ""); });
    y += 48 + 12;
    button(ui, SDL_FRect{x, y, 260, 44}, a.settings.developer_mods ? "Developer mods: On" : "Developer mods: Off", Style::kQuiet, true,   // sdk2b / tune4
           "For mod authors: code mods that say [code] platform = yes may then change the game's platform layer (the kernel, "
           "the disc and pad drivers). Leave it off unless a mod's author asks for it.",
           [&a]() {
               // the one switch: PC SETTINGS > INTERFACE > DEVELOPER MODS (pc_settings.ini; the tuning console too)
               a.settings.developer_mods = a.settings.developer_mods ? 0 : 1;
               save_settings(a);
               a.mods_note = a.settings.developer_mods ? "Developer mods on (also PC SETTINGS > INTERFACE > DEVELOPER MODS)." : "Developer mods off.";
               a.mods_note_bad = false;
           });
    y += 44 + 22;
    // sdk2b: the focusable items of card k (Turn on, Move up, Move down, then its settings rows)
    auto card_items = [&a](std::size_t k) {
        if (k >= a.mod_order.entries.size()) return 3;
        const auto it = a.mod_rows.find(a.mod_order.entries[k].id);
        const b3mod::Mod* m = b3mod::find_mod(a.mods, a.mod_order.entries[k].id);
        return 3 + (it != a.mod_rows.end() && m && (m->manifest.code.present || m->manifest.script.present) ? int(it->second.size()) : 0);
    };

    // one card per mod, in load order: its manifest, its problems, on / off and its place
    for (std::size_t k = 0; k < a.mod_order.entries.size(); ++k) {
        const b3mod::OrderEntry e = a.mod_order.entries[k];
        const b3mod::Mod* m = b3mod::find_mod(a.mods, e.id);
        if (!m) continue;
        const b3mod::Manifest& mf = m->manifest;
        std::string meta = "id " + mf.id + "  |  version " + mf.version + "  |  api " + std::to_string(mf.api);
        if (!mf.author.empty()) meta += "  |  by " + mf.author;
        meta += "  |  folder " + m->folder;
        const float ctop = y, tw = w - 48;
        auto card = [&](bool draw_it) {
            float yy = ctop + 16;
            if (draw_it) {
                text_right(a, std::to_string(k + 1), x + 40, yy, 18, col::faint, true);
                const float nw = text(a, a.text->elide(mf.name, 18, tw - 200, true), x + 52, yy, 18, m->usable() ? col::text : col::dim, true);
                const char* st = !m->usable() ? "CANNOT BE USED" : e.enabled ? "ON" : "OFF";
                text(a, st, x + 52 + nw + 14, yy + 2, kBody, !m->usable() ? col::bad : e.enabled ? col::ok : col::faint, true);
            }
            yy += a.text->line_height(18) + 4;
            if (draw_it) paragraph(a, meta, x + 52, yy, tw - 28, kSmall, col::faint);
            yy += float(a.text->wrap(meta, kSmall, tw - 28).size()) * a.text->line_height(kSmall) + 4;
            if (!mf.description.empty()) {
                if (draw_it) paragraph(a, mf.description, x + 52, yy, tw - 28, kBody, col::dim);
                yy += float(a.text->wrap(mf.description, kBody, tw - 28).size()) * lh + 4;
            }
            for (const std::string& er : m->errors) {
                if (draw_it) paragraph(a, "Error: " + er, x + 52, yy, tw - 28, kBody, col::bad);
                yy += float(a.text->wrap("Error: " + er, kBody, tw - 28).size()) * lh + 2;
            }
            for (const std::string& wn : m->warnings) {
                if (draw_it) paragraph(a, "Note: " + wn, x + 52, yy, tw - 28, kBody, col::warn);
                yy += float(a.text->wrap("Note: " + wn, kBody, tw - 28).size()) * lh + 2;
            }
            // modkit1b: what it requires, why the next start leaves it out, and its files that another mod also has
            auto line = [&](const std::string& t, SDL_FColor c) {
                if (draw_it) paragraph(a, t, x + 52, yy, tw - 28, kBody, c);
                yy += float(a.text->wrap(t, kBody, tw - 28).size()) * lh + 2;
            };
            if (!mf.requirements.empty()) {
                std::string req = "Requires: ";
                for (std::size_t q = 0; q < mf.requirements.size(); ++q)
                    req += (q ? ", " : "") + mf.requirements[q].id + (mf.requirements[q].min_version.empty() ? "" : " " + mf.requirements[q].min_version + " or newer");
                line(req, col::dim);
            }
            if (!mf.tuning_preset.empty())   // tune4: a tuning preset changes gameplay by nature (docs/modding/tuning.md)
                line("Changes gameplay: a tuning preset (" + mf.tuning_preset + "). Replays and ghosts made with it are marked with this mod.", col::warn);
            if (e.enabled)
                for (const std::string& er : a.mod_sel.errors)
                    if (er.rfind(mf.id + ": ", 0) == 0) line("Off at the next start: " + er.substr(mf.id.size() + 2), col::bad);
            for (const std::string& nt : a.mod_sel.notes)
                if (nt.rfind(mf.id + " ", 0) == 0) line("Note: " + nt, col::warn);
            if (e.enabled) {
                const b3mod::ModConflicts mc = b3mod::conflicts_of(a.mod_conflicts, mf.id);
                auto list = [](const std::vector<std::pair<std::string, std::string>>& v) {
                    std::string t;
                    for (std::size_t q = 0; q < v.size() && q < 4; ++q) t += (q ? ", " : "") + v[q].first + " (" + v[q].second + ")";
                    if (v.size() > 4) t += " and " + std::to_string(v.size() - 4) + " more";
                    return t;
                };
                if (!mc.wins.empty())
                    line("Wins over another mod for " + std::to_string(mc.wins.size()) + (mc.wins.size() == 1 ? " file: " : " files: ") + list(mc.wins), col::dim);
                if (!mc.hidden.empty())
                    line("Hidden by a mod above for " + std::to_string(mc.hidden.size()) + (mc.hidden.size() == 1 ? " file: " : " files: ") + list(mc.hidden), col::warn);
            }
            if (mf.code.present) {   // sdk2b: a code mod -- what it is, whether it may run, what the last start did
                const fs::path lib = code_library(*m);
                std::error_code ec;
                line(std::string("Contains code: runs as a program on your PC") +
                         (lib.empty() ? " (no library for this system: it cannot run here)."
                                      : !fs::exists(lib, ec) ? " (its library " + utf8_of(lib) + " is missing)."
                                      : code_allowed(a, *m) ? " (allowed to run)."
                                                            : " (not allowed yet: you are asked when it is turned on or the game starts)."),
                     lib.empty() || !fs::exists(lib, ec) ? col::bad : col::warn);
                if (mf.code.gameplay) line("Changes gameplay: times and replays made with it are kept apart.", col::warn);
                if (mf.code.platform) line(std::string("Asks to change the platform layer: ") + (a.settings.developer_mods ? "allowed (Developer mods on)." : "only with Developer mods on."), col::warn);
                if (const b3mod::ModState* st = code_state(a, mf.id))
                    line(st->loaded ? "At the last start: loaded." : "At the last start: not loaded: " + st->reason, st->loaded ? col::dim : col::bad);
                if (a.mod_rows.count(mf.id)) line("Its settings (also in the game: PC SETTINGS > MODS); a change here applies at the next start:", col::dim);
            }
            if (mf.script.present) {   // lua6: a script mod -- it runs inside the game, through the script API only
                line("A Lua script (" + mf.script.main_file + "): it runs inside the game and can only do what the script API offers.", col::dim);
                line(mf.script.gameplay ? "Changes gameplay: times and replays made with it are kept apart." : "Only reads and draws (gameplay = no).",
                     mf.script.gameplay ? col::warn : col::dim);
                if (const b3mod::ModState* st = code_state(a, mf.id))
                    line(st->loaded ? "At the last start: loaded." : "At the last start: not loaded: " + st->reason, st->loaded ? col::dim : col::bad);
                if (a.mod_rows.count(mf.id)) line("Its settings (also in the game: PC SETTINGS > MODS); a change here applies at the next start:", col::dim);
            }
            return yy + 8;
        };
        const float cend = card(false);
        const std::vector<b3mod::SettingRow>* rows = nullptr;   // sdk2b: the code mod's settings, under its buttons
        if (const auto it = a.mod_rows.find(e.id); it != a.mod_rows.end() && (m->manifest.code.present || m->manifest.script.present)) rows = &it->second;
        const float rh = 44, rows_h = rows ? float(rows->size()) * (rh + 6) + 4 : 0;
        const float bh = 40, ch = cend - ctop + bh + 14 + rows_h;
        D::round_rect(SDL_FRect{x, ctop, w, ch}, 12, col::panel);
        card(true);
        const float by = cend;
        const std::string id = e.id;
        button(ui, SDL_FRect{x + 52, by, 170, bh}, e.enabled ? "Turn off" : "Turn on", e.enabled ? Style::kNormal : Style::kPrimary,
               m->usable() || e.enabled,
               m->usable() ? (e.enabled ? "The game stops using this mod from the next start." : "The game uses this mod from the next start.")
                           : "This mod cannot be used: see its error.",
               [&a, id]() {
                   for (b3mod::OrderEntry& x2 : a.mod_order.entries)
                       if (x2.id == id) {
                           const b3mod::Mod* md = b3mod::find_mod(a.mods, id);
                           if (!x2.enabled && md && !code_allowed(a, *md)) {   // sdk2b: asked once per library
                               ask_code_trust(a, {md}, "Allow and turn on", [&a, id]() {
                                   for (b3mod::OrderEntry& x3 : a.mod_order.entries)
                                       if (x3.id == id && !x3.enabled) {
                                           x3.enabled = true;
                                           save_mods(a, "Turned on " + id + " (its code allowed to run).");
                                       }
                               });
                               return;
                           }
                           x2.enabled = !x2.enabled;
                           save_mods(a, std::string(x2.enabled ? "Turned on " : "Turned off ") + id + ".");
                       }
               });
        button(ui, SDL_FRect{x + 52 + 186, by, 140, bh}, "Move up", Style::kNormal, k > 0,
               "Earlier in the list: this mod wins over the ones below it when two have the same file.", [&a, k, card_items]() {
                   if (k == 0 || k >= a.mod_order.entries.size()) return;
                   const int above = card_items(k - 1);   // the same button, one card up
                   std::swap(a.mod_order.entries[k - 1], a.mod_order.entries[k]);
                   save_mods(a, "Moved " + a.mod_order.entries[k - 1].id + " up.");
                   a.focus = std::max(0, a.focus - above);
               });
        button(ui, SDL_FRect{x + 52 + 342, by, 150, bh}, "Move down", Style::kNormal, k + 1 < a.mod_order.entries.size(),
               "Later in the list: the mods above it win when two have the same file.", [&a, k, card_items]() {
                   if (k + 1 >= a.mod_order.entries.size()) return;
                   const int below = card_items(k + 1);
                   std::swap(a.mod_order.entries[k], a.mod_order.entries[k + 1]);
                   save_mods(a, "Moved " + a.mod_order.entries[k + 1].id + " down.");
                   a.focus += below;
               });
        if (rows) {   // sdk2b: the code mod's settings rows (its mod-settings/<id>.ini)
            for (std::size_t q = 0; q < rows->size(); ++q) {
                const b3mod::SettingRow& sr = (*rows)[q];
                b3settings::Row r;
                r.id = b3settings::kModRowBase + std::uint32_t(q);
                r.label = sr.label;
                r.description = sr.description;
                r.value = std::size_t(std::max(sr.value, 0));
                if (sr.kind == b3mod::SettingKind::kSlider) {
                    r.kind = b3settings::RowKind::kSlider;
                    r.levels = sr.count;
                    r.level_text = std::to_string(sr.value);
                } else {
                    r.kind = sr.kind == b3mod::SettingKind::kToggle ? b3settings::RowKind::kToggle : b3settings::RowKind::kChoice;
                    r.values = sr.values;
                }
                auto set = [&a, id, q](std::size_t v) {
                    auto it = a.mod_rows.find(id);
                    if (it == a.mod_rows.end() || q >= it->second.size() || v >= it->second[q].count) return;
                    it->second[q].value = int(v);
                    std::string why;
                    if (b3mod::save_values(b3mod::settings_file(a.pref, id), id, it->second, &why)) {
                        a.mods_note = id + ": " + it->second[q].label + " = " + b3mod::value_text(it->second[q], int(v)) + ". Used from the next start.";
                        a.mods_note_bad = false;
                    } else {
                        a.mods_note = "Cannot save " + id + "'s settings: " + why;
                        a.mods_note_bad = true;
                    }
                };
                // (a click as on a PC settings row: the value's left half one back, a slider's bar its level)
                setting_row(a, ui, r, x + 52, by + bh + 10 + float(q) * (rh + 6), w - 52 - 24, rh, set);
            }
        }
        y = ctop + ch + 14;
    }
    y += 8;
    y += paragraph(a, "A mod replaces some of the game's files with its own (its data folder holds them at their disc paths) and "
                      "may add new ones. Only the files of mods that are on are read, and only by the game: your disc image or folder "
                      "stays as it is. The game's log names the mods it ran with and a hash of their files, so a bug report or a "
                      "replay shows them. How to make one: docs/modding in the port's source.",
                   x + 4, y, w - 8, kSmall, col::faint);
    a.content_h = y;
}

// ---- the GAME DATA page ----

void page_game(App& a, Ui& ui, float x, float y, float w) {
    const float top = y;
    auto body = [&](bool draw_it) {
        float yy = top + 20;
        if (draw_it) section(a, "Folder or disc image", x + 24, yy);
        yy += a.text->line_height(kSection) + 6;
        const std::string p = a.game_dir.empty() ? "Not found." : utf8_of(a.game_dir);
        if (draw_it) paragraph(a, p, x + 24, yy, w - 48, 18, a.game_dir.empty() ? col::dim : col::text, true);
        yy += float(a.text->wrap(p, 18, w - 48, true).size()) * a.text->line_height(18) + 12;
        // the checklist, two columns
        const float cw = (w - 48) * 0.5f;
        for (std::size_t k = 0; k < a.game.items.size(); ++k) {
            const GameItem& i = a.game.items[k];
            const float ix = x + 24 + float(k % 2) * cw, iy = yy + float(k / 2) * 32;
            if (draw_it) {
                const SDL_FColor c = i.found ? col::ok : i.required ? col::bad : col::warn;
                if (i.found) D::check(ix + 9, iy + 11, 16, 2.4f, c);
                else D::cross(ix + 9, iy + 11, 16, 2.4f, c);
                const float nw = text(a, i.name, ix + 28, iy, kBody, a.game.exists ? col::text : col::faint, true);
                text(a, i.what + (i.required ? "" : " (optional)"), ix + 28 + nw + 9, iy, kBody, col::dim);
            }
        }
        yy += float((a.game.items.size() + 1) / 2) * 32 + 6;
        if (draw_it) {
            const SDL_FColor c = a.game.ok ? col::ok : col::bad;
            D::disc(x + 30, yy + 11, 5, c);
            paragraph(a, a.game.summary, x + 46, yy, w - 70, kBody, a.game.ok ? col::text : col::bad);
        }
        yy += float(a.text->wrap(a.game.summary, kBody, w - 70).size()) * a.text->line_height(kBody) + 8;
        if (!a.game.warning.empty()) {   // mac5 (integrity): another version / an incomplete image
            if (draw_it) {
                D::disc(x + 30, yy + 11, 5, col::warn);
                paragraph(a, a.game.warning, x + 46, yy, w - 70, kBody, col::warn);
            }
            yy += float(a.text->wrap(a.game.warning, kBody, w - 70).size()) * a.text->line_height(kBody) + 8;
        }
        if (!a.game_note.empty()) {
            if (draw_it) paragraph(a, a.game_note, x + 46, yy, w - 70, kSmall, col::dim);
            yy += float(a.text->wrap(a.game_note, kSmall, w - 70).size()) * a.text->line_height(kSmall) + 6;
        }
        return yy + 10;
    };
    const float end = body(false);
    D::round_rect(SDL_FRect{x, top, w, end - top}, 12, col::panel);
    body(true);
    y = end + 22;
    float bx = x;
    // iso1: a row that wraps when the next button does not fit (the disc image button made it wider than the page)
    auto at = [&](float bw) {
        if (bx > x && bx + bw > x + w) { bx = x; y += 48 + 16; }
        const SDL_FRect r{bx, y, bw, 48};
        bx += bw + 16;
        return r;
    };
    button(ui, at(230), "Choose folder...", Style::kPrimary, true,
           "The folder you copied the disc's files into (SYSTEM.CNF, SLUS_210.50, DATA, TRACKS, ...). With a controller: the launcher's own folder list.",
           [&a]() { open_dialog(a, DialogKind::kGameFolder, utf8_of(a.game_dir)); });
    button(ui, at(250), "Choose disc image...", Style::kPrimary, true,
           "Your disc's image file (.iso, or a raw .bin), read in place: nothing is extracted. A .chd image of the disc works too. "
           "With a controller: the launcher's own file list.",   // chd1: the sentence on .chd
           [&a]() { open_dialog(a, DialogKind::kGameImage, utf8_of(a.game_dir)); });
    button(ui, at(190), "Type a path...", Style::kNormal, true, "Type or paste the folder's or the disc image's path instead. With a controller: an on-screen keyboard.",
           [&a]() { ask_path(a, DialogKind::kGameFolder, "Game data folder or disc image", utf8_of(a.game_dir)); });
    button(ui, at(170), "Search again", Style::kNormal, true,
           "Looks in the usual places again: game or data/game beside the launcher, and the folder it was started from.",
           [&a]() {
               const fs::path f = find_game_folder("", a.exe_dir, a.cwd);
               if (f.empty()) a.game_note = "Not found in the usual places: choose the folder.";
               else {
                   set_game_dir(a, f, true);
                   a.game_note = "Found and remembered: " + utf8_of(f);
                   load_look(a);
               }
           });
    button(ui, at(130), "Open", Style::kNormal, a.game.exists, "Opens the folder (a disc image: the folder it is in) in the file manager.",
           [&a]() {
               std::error_code ec;   // iso1: a disc image's folder; never create anything for it
               if (fs::is_regular_file(a.game_dir, ec)) open_folder(a.game_dir.parent_path(), &a.game_note);
               else open_folder(a.game_dir, &a.game_note);
           });
    y += 48 + 22;
    y += paragraph(a, "You need your own copy of Burnout 3: Takedown (USA, SLUS-21050). The port includes no game data. Choose "
                      "your disc's image (a .iso as disc dumping tools make it, or a raw .bin), or copy everything on the disc "
                      "into one folder and choose that folder. A .chd image of the disc works too. The game reads its files "
                      "from it each start (native_play --game). Nothing in it is changed.",
                   x + 4, y, w - 8, kSmall, col::faint);
    a.content_h = y;
}

// ---- the SAVES page ----

void confirm_import(App& a, bool folder) {
    const int slot = b3native::save::read_active_slot(saves_root(a));
    Modal m;
    m.title = "Replace file " + std::to_string(slot) + "?";
    m.text = "The save you choose replaces file " + std::to_string(slot) + " (profile-" + std::to_string(slot) +
             ".sav); the old one is kept beside it as profile-" + std::to_string(slot) +
             ".sav.bak. A .sav, a PS2 save (.psu, .max) or a PCSX2 memory card (.ps2, or a folder card).";
    // 1.1: which PCSX2 file (the dialog starts in PCSX2's memcards folder when there is one)
    m.text += folder ? " From PCSX2: the folder card's folder, in PCSX2's memcards folder."
                     : " From PCSX2: its memory card, usually Mcd001.ps2 in its memcards folder.";
    if (const auto f = pcsx2_folders(a); !f.empty()) m.text += " Found: " + utf8_of(f.front().path);
    m.buttons = {{folder ? "Choose a folder card" : "Choose a file", [&a, folder]() {
                      open_dialog(a, folder ? DialogKind::kImportFolder : DialogKind::kImportFile, "");
                  }},
                 {"Cancel", nullptr}};
    a.modal = m;
}

// 1.1: the folders compared as the same one (a chosen folder that is the default's: no choice)
bool same_folder(const fs::path& x, const fs::path& y) {
    std::error_code ec;
    if (fs::equivalent(x, y, ec)) return true;
    std::error_code ex, ey;
    const fs::path cx = fs::weakly_canonical(x, ex), cy = fs::weakly_canonical(y, ey);
    if (ex || ey) return x.lexically_normal() == y.lexically_normal();
    return cx.lexically_normal() == cy.lexically_normal();
}

// 1.1: the save folder changed to `to` (the default: saves_folder(pref)), after the saves in use were copied there when
// `copy` (copy_saves: nothing there replaced, nothing removed here). launcher.ini keeps the choice; native_play uses
// it from its next start.
void use_saves_folder(App& a, const fs::path& to, bool copy) {
    const fs::path from = saves_root(a), def = saves_folder(a.pref);
    const bool to_default = same_folder(to, def);
    std::error_code ec;
    if (to_default) fs::create_directories(def, ec);
    std::string note;
    a.save_note_bad = true;
    {   // the game writes its saves there: a folder it cannot write in is refused
        const fs::path probe = to / ".b3port-write-check";
        std::ofstream f(probe, std::ios::binary | std::ios::trunc);
        const bool ok = bool(f << "b3");
        f.close();
        fs::remove(probe, ec);
        if (!ok) {
            a.save_note = "Cannot use that folder: nothing can be written in it. The save folder is not changed.";
            refresh(a);
            return;
        }
    }
    if (copy) {
        std::string err;
        if (b3native::save::copy_saves(from, to, &err) < 0) {
            a.save_note = "COPY FAILED: " + err + ". The save folder is not changed.";
            refresh(a);
            return;
        }
        note = "Your saves were copied there. ";
    }
    const std::string kept = a.cfg.saves_dir;
    a.cfg.saves_dir = to_default ? std::string() : utf8_of(to);
    std::string why;
    if (!save_config(config_file(a.pref), a.cfg, &why)) {
        a.cfg.saves_dir = kept;
        a.save_note = note + "Cannot remember the folder: " + why;
        refresh(a);
        return;
    }
    a.save_note_bad = false;
    a.save_note = note + (to_default ? "The default save folder is used from the next start."
                                     : "This save folder is used from the next start.");
    if (!copy && b3native::save::has_saves(from)) a.save_note += " The saves in the old folder stay there.";
    refresh(a);
}

// 1.1: a save folder chosen (Choose save folder..., or Use the default folder). A folder without saves while the one in
// use has some: the player is asked whether to copy them there first.
void choose_saves_folder(App& a, const fs::path& to) {
    const fs::path from = saves_root(a);
    if (same_folder(to, from)) {
        a.save_note = "That folder is the save folder already.";
        a.save_note_bad = false;
        refresh(a);
        return;
    }
    std::error_code ec;
    if (!same_folder(to, saves_folder(a.pref)) && !fs::is_directory(to, ec)) {
        a.save_note = "That folder is not there. The save folder is not changed.";
        a.save_note_bad = true;
        refresh(a);
        return;
    }
    if (b3native::save::has_saves(to) || !b3native::save::has_saves(from)) {
        use_saves_folder(a, to, false);
        return;
    }
    Modal m;
    m.title = "Copy your saves there?";
    m.text = "The folder you chose has no saves yet. Copy the saves in use into it (" + utf8_of(to) +
             ")? Nothing is deleted or replaced: the old folder keeps its files.";
    m.buttons = {{"Copy and use it", [&a, to]() { use_saves_folder(a, to, true); }},
                 {"Use it empty", [&a, to]() { use_saves_folder(a, to, false); }},
                 {"Cancel", nullptr}};
    a.modal = m;
}

void page_saves(App& a, Ui& ui, float x, float y, float w) {
    const fs::path root = saves_root(a);
    const bool chosen = !a.cfg.saves_dir.empty();
    const float lw = std::max(140.0f, text(a, "Save folder", x, y, kBody, col::text, true) + 14);   // 1.1: a longer translated label
    text(a, a.text->elide(utf8_of(root), kBody, w - lw - 200), x + lw, y, kBody, col::dim);
    button(ui, SDL_FRect{x + w - 190, y - 10, 190, 42}, "Open save folder", Style::kNormal, true,
           "Opens the folder in the file manager. Copy a profile-N.sav to take its three profiles to another computer.",
           [&a, root]() { open_folder(root, &a.save_note); });
    y += 46;
    // 1.1: the save folder of the player's choice (a cloud or network folder two computers share)
    {
        float bx = x;
        button(ui, SDL_FRect{bx, y, 230, 42}, "Choose save folder...", Style::kNormal, true,
               "Keeps the saves in another folder: a cloud folder (OneDrive, Google Drive, Dropbox) or a network folder that "
               "another computer uses too. Used from the next start. Asks before copying your saves there.",
               [&a, root]() { open_dialog(a, DialogKind::kSavesFolder, utf8_of(root)); });
        bx += 246;
        button(ui, SDL_FRect{bx, y, 230, 42}, "Use the default folder", Style::kNormal, chosen,
               chosen ? "Back to the save folder beside the game's settings. Used from the next start. Asks before copying your saves there."
                      : "The default save folder is in use.",
               [&a]() { choose_saves_folder(a, saves_folder(a.pref)); });
        bx += 246;
        text(a, a.text->elide(chosen ? "Your chosen folder" : "The default folder", kSmall, x + w - bx), bx,
             y + (42 - a.text->line_height(kSmall)) * 0.5f, kSmall, chosen ? col::accent : col::faint);
        y += 42 + 14;
    }
    for (const SaveSlot& s : a.slots) {
        Widget wd;
        wd.r = SDL_FRect{x, y, w, 58};
        const int n = s.slot;
        wd.desc = "Press to use file " + std::to_string(n) + " from the next start. Each file holds three profiles of its own.";
        wd.activate = [&a, n]() {
            std::string err;
            if (b3native::save::write_active_slot(saves_root(a), n, &err)) a.save_note = "File " + std::to_string(n) + " is used from the next start.";
            else a.save_note = "Cannot write active-slot.txt: " + err;
            a.save_note_bad = !err.empty();
            refresh(a);
        };
        const int i = ui.add(std::move(wd));
        const bool f = ui.focused(i), hv = ui.hovered(i);
        const SDL_FRect r{x, y, w, 58};
        D::round_rect(r, 10, f ? col::panel3 : hv ? col::panel2 : col::panel);
        if (s.active) D::round_rect_outline(r, 10, 1.5f, D::alpha(col::accent, 0.8f));
        if (f) D::rect(SDL_FRect{x, y + 10, 3, 38}, col::accent);
        // the slot's radio mark
        D::ring(x + 28, y + 29, 10, 2, s.active ? col::accent : col::faint);
        if (s.active) D::disc(x + 28, y + 29, 5, col::accent);
        text(a, "File " + std::to_string(s.slot), x + 52, y + 8, 18, col::text, true);
        std::string sub;
        if (s.sav) sub = "profile-" + std::to_string(s.slot) + ".sav   |   " + format_bytes(s.bytes) + (s.modified ? "   |   saved " + format_time(*s.modified) : "");
        else if (s.legacy) sub = "An older profile-" + std::to_string(s.slot) + " folder (read; saved as a .sav at the first save)";
        else sub = "Empty: the game makes it at its first save";
        text(a, sub, x + 52, y + 32, kSmall, col::dim);
        if (s.active) {
            const float bw = a.text->width("IN USE", kSmall, true) + 20;
            D::round_rect(SDL_FRect{x + w - bw - 16, y + 17, bw, 24}, 12, col::accent);
            text_center(a, "IN USE", x + w - bw * 0.5f - 16, y + 17 + (24 - a.text->line_height(kSmall)) * 0.5f, kSmall, col::ink, true);
        }
        y += 58 + 8;
    }
    y += 14;
    const int act = b3native::save::read_active_slot(root);
    const SaveSlot* cur = nullptr;
    for (const SaveSlot& s : a.slots)
        if (s.slot == act) cur = &s;
    const bool has = cur && (cur->sav || cur->legacy);
    float bx = x;
    button(ui, SDL_FRect{bx, y, 230, 46}, "Export file " + std::to_string(act) + "...", Style::kNormal, has,
           has ? "Writes the file in use to a .sav file of your choice (a copy: the game keeps using its own)." : "The file in use is empty: nothing to export.",
           [&a, act]() { open_dialog(a, DialogKind::kExport, "Burnout3-file-" + std::to_string(act) + ".sav"); });
    bx += 246;
    button(ui, SDL_FRect{bx, y, 250, 46}, "Import into file " + std::to_string(act) + "...", Style::kNormal, true,
           "Replaces the file in use with a saved game: a .sav, a PS2 save (.psu, .max) or a PCSX2 memory card (.ps2). "
           "PCSX2's card is usually Mcd001.ps2 in its memcards folder. Asks first.",
           [&a]() { confirm_import(a, false); });
    bx += 266;
    button(ui, SDL_FRect{bx, y, 250, 46}, "Import a folder card...", Style::kNormal, true,
           "Replaces the file in use with the Burnout 3 save on a PCSX2 folder memory card. Asks first.", [&a]() { confirm_import(a, true); });
    y += 46 + 16;
    if (!a.save_note.empty()) y += paragraph(a, a.save_note, x + 4, y, w - 8, kBody, a.save_note_bad ? col::bad : col::ok) + 6;
    y += paragraph(a, "The game saves by itself to the file in use (its three profiles inside). Choose another file to keep "
                      "separate profiles; a new one starts empty. A save folder in the cloud or on the network can be shared "
                      "by two computers: play on one at a time, and let the folder sync before starting on the other.",
                   x + 4, y, w - 8, kSmall, col::faint);
    a.content_h = y;
}

// ---- the STATS page ----
// The game's own stats of each profile in the save files (src/native/save_stats.h: read only) and the local
// achievements worked out from them. A row of profiles to choose from, the chosen one's totals, then the achievements.

std::string upper(std::string s) { return utf8_upper(s); }

void progress_bar(const SDL_FRect& r, double frac, SDL_FColor fill) {
    D::round_rect(r, r.h * 0.5f, D::alpha(col::faint, 0.35f));
    frac = std::clamp(frac, 0.0, 1.0);
    if (frac > 0) D::round_rect(SDL_FRect{r.x, r.y, std::max(r.h, float(r.w * frac)), r.h}, r.h * 0.5f, fill);
}

std::string metres(float m) { return b3native::save::group_digits(std::uint64_t(std::lround(double(m)))) + " m"; }

void page_stats(App& a, Ui& ui, float x, float y, float w) {
    namespace S = b3native::save;
    const SDL_FColor gold = col::warn;
    if (a.stats.empty()) {
        float yy = y + 22;
        const float top = y;
        auto body = [&](bool draw_it) {
            float h = 0;
            const std::string t1 = "No saved profile yet.";
            const std::string t2 = "The game keeps its own stats in every profile it saves: medals, takedowns, crash damage, the longest "
                                   "drift and more. Play, let the game save, and they show here with the achievements earned from them. "
                                   "This page only reads the save files.";
            if (draw_it) text(a, t1, x + 24, yy, 19, col::text, true);
            h += a.text->line_height(19) + 8;
            h += draw_it ? paragraph(a, t2, x + 24, yy + h, w - 48, kBody, col::dim) : float(a.text->wrap(t2, kBody, w - 48).size()) * a.text->line_height(kBody);
            for (const std::string& n : a.stats_notes) {
                h += 8;
                h += draw_it ? paragraph(a, n, x + 24, yy + h, w - 48, kSmall, col::bad) : float(a.text->wrap(n, kSmall, w - 48).size()) * a.text->line_height(kSmall);
            }
            return h;
        };
        const float h = body(false);
        D::round_rect(SDL_FRect{x, top, w, h + 44}, 12, col::panel);
        D::rect(SDL_FRect{x, top + 14, 4, h + 16}, col::accent);
        body(true);
        a.content_h = top + h + 44;
        return;
    }
    if (a.stats_sel >= a.stats.size()) a.stats_sel = 0;

    // the profiles
    float bx = x;
    for (std::size_t k = 0; k < a.stats.size(); ++k) {
        const App::StatsEntry& e = a.stats[k];
        const std::string label = "FILE " + std::to_string(e.slot) + "   " + upper(e.p.name.empty() ? "Profile " + std::to_string(e.p.index + 1) : e.p.name);
        const float bw = a.text->width(label, kBody, true) + 44;
        if (bx > x && bx + bw > x + w) {
            bx = x;
            y += 42 + 10;
        }
        button(ui, SDL_FRect{bx, y, bw, 42}, label, k == a.stats_sel ? Style::kPrimary : Style::kNormal, true,
               "Shows the stats of profile " + std::to_string(e.p.index + 1) + " in file " + std::to_string(e.slot) +
                   (e.active ? " (the file in use)." : "."),
               [&a, k]() { a.stats_sel = k; });
        bx += bw + 12;
    }
    y += 42 + 20;

    const App::StatsEntry& e = a.stats[a.stats_sel];
    const S::ProfileStats& p = e.p;
    const std::vector<S::Achievement> list = S::achievements(p);
    int done = 0;
    for (const S::Achievement& c : list) done += c.unlocked();

    // the summary
    {
        const float h = 104;
        D::round_rect(SDL_FRect{x, y, w, h}, 12, col::panel);
        D::gradient(SDL_FRect{x + 12, y, w * 0.55f, h}, D::alpha(col::accent, 0.22f), D::alpha(col::accent, 0.0f), D::alpha(col::accent, 0.10f),
                    D::alpha(col::accent, 0.0f));
        D::rect(SDL_FRect{x, y + 14, 4, h - 28}, col::accent);
        const float rw = std::min(320.0f, w * 0.38f);
        text(a, a.text->elide(p.name.empty() ? "Profile " + std::to_string(p.index + 1) : p.name, 28, w - rw - 60, true), x + 28, y + 16, 28, col::text, true);
        text(a, "File " + std::to_string(e.slot) + ", profile " + std::to_string(p.index + 1) + (e.active ? ", the file in use" : ""), x + 30,
             y + 16 + a.text->line_height(28) + 4, kSmall, col::dim);
        const float rx = x + w - rw - 24;
        text(a, "ACHIEVEMENTS", rx, y + 16, kSection + 1, col::accent, true);
        text_right(a, std::to_string(done) + " / " + std::to_string(list.size()), rx + rw, y + 10, 26, done == int(list.size()) ? gold : col::text, true);
        progress_bar(SDL_FRect{rx, y + 56, rw, 10}, list.empty() ? 0.0 : double(done) / double(list.size()), gold);
        text(a, done == int(list.size()) ? "Every one unlocked." : std::to_string(int(list.size()) - done) + " still to unlock.", rx, y + 74, kSmall, col::dim);
        y += h + 16;
    }

    // three tiles: the World Tour's medals, the signature takedowns, the headlines
    {
        const float gap = 14, tw = (w - 2 * gap) / 3, th = 104;
        const S::MedalCounts all = p.all();
        struct Tile {
            const char* title;
            int have, of;
            std::string line;
        };
        const Tile tiles[3] = {
            {"World Tour medals", all.medals(), S::kEvents,
             S::group_digits(std::uint64_t(all.gold)) + " gold, " + S::group_digits(std::uint64_t(all.silver)) + " silver, " +
                 S::group_digits(std::uint64_t(all.bronze)) + " bronze"},
            {"Signature takedowns", p.signature_takedowns, S::kSignatureTakedowns, "Each one done at least once"},
            {"Crash headlines", p.headlines, S::kHeadlines, "Crash damage big enough for the news"},
        };
        for (int k = 0; k < 3; ++k) {
            const Tile& t = tiles[k];
            const float tx = x + float(k) * (tw + gap);
            D::round_rect(SDL_FRect{tx, y, tw, th}, 10, col::panel);
            if (a.look.on) D::round_rect_outline(SDL_FRect{tx, y, tw, th}, 10, 1.0f, D::alpha(col::edge, 0.6f));
            text(a, a.text->elide(upper(a.text->translated(t.title)), kSection + 1, tw - 32, true), tx + 16, y + 12, kSection + 1, col::accent, true);
            const bool full = t.have >= t.of;
            const float vw = text(a, std::to_string(t.have), tx + 16, y + 32, 26, full ? gold : col::text, true);
            text(a, "/ " + std::to_string(t.of), tx + 16 + vw + 8, y + 32 + (a.text->line_height(26) - a.text->line_height(kBody)) * 0.7f, kBody, col::dim);
            progress_bar(SDL_FRect{tx + 16, y + 72, tw - 32, 6}, t.of ? double(t.have) / double(t.of) : 0.0, full ? gold : col::accent);
            text(a, a.text->elide(t.line, kSmall - 1, tw - 32), tx + 16, y + 82, kSmall - 1, col::faint);
        }
        y += th + 22;
    }

    // the career totals, two columns
    {
        y += section(a, "Career", x, y);
        const auto n = [](std::uint64_t v) { return S::group_digits(v); };
        const auto split = [&](std::uint64_t off, std::uint64_t on) {
            return n(off + on) + (on ? "   (" + n(on) + " online)" : std::string());
        };
        const std::pair<std::string, std::string> rows[] = {
            {"Takedowns", split(p.takedowns_offline, p.takedowns_online)},
            {"Slams", split(p.slams_offline, p.slams_online)},
            {"Crash damage", n(p.crash_damage)},
            {"Burnout points", n(p.burnout_points)},
            {"Race events played", split(p.race_events_offline, p.race_events_online)},
            {"Crash events played", split(p.crash_events_offline, p.crash_events_online)},
            {"Race events, gold", n(std::uint64_t(p.race.gold)) + " / " + std::to_string(S::kRaceEvents)},
            {"Crash events, gold", n(std::uint64_t(p.crash.gold)) + " / " + std::to_string(S::kCrashEvents)},
            {"Longest oncoming", p.longest_oncoming_m > 0 ? metres(p.longest_oncoming_m) : "-"},
            {"Longest drift", p.longest_drift_m > 0 ? metres(p.longest_drift_m) : "-"},
            {"Cars raced", n(std::uint64_t(p.cars_driven)) + " / " + std::to_string(S::kCars)},
            {"Events played", n(p.events_played())},
        };
        const int count = int(sizeof rows / sizeof rows[0]), half = (count + 1) / 2;
        const float gap = 28, cw = (w - gap) / 2, rh = 34;
        D::round_rect(SDL_FRect{x, y, w, float(half) * rh + 16}, 12, col::panel);
        for (int k = 0; k < count; ++k) {
            const float cx = x + (k < half ? 0 : cw + gap) + 18, ry = y + 8 + float(k % half) * rh;
            if (k % half) D::rect(SDL_FRect{cx, ry, cw - 36, 1}, D::alpha(col::line, 0.6f));
            text(a, rows[k].first, cx, ry + (rh - a.text->line_height(kBody)) * 0.5f, kBody, col::dim);
            text_right(a, rows[k].second, cx + cw - 36, ry + (rh - a.text->line_height(kBody)) * 0.5f, kBody, col::text, true);
        }
        y += float(half) * rh + 16 + 22;
    }

    // the achievements
    y += section(a, "Achievements (local: from the save's stats, nothing online)", x, y);
    {
        const float gap = 14, cw = (w - gap) / 2, ch = 86;
        for (std::size_t k = 0; k < list.size(); ++k) {
            const S::Achievement& c = list[k];
            const float cx = x + float(k % 2) * (cw + gap), cy = y + float(k / 2) * (ch + 12);
            const SDL_FRect r{cx, cy, cw, ch};
            const bool on = c.unlocked();
            Widget wd;
            wd.r = r;
            wd.desc = std::string(c.title) + ": " + c.description + (on ? "  Unlocked." : "  Progress: " + S::group_digits(c.progress) + " of " + S::group_digits(c.target) + ".");
            const int i = ui.add(std::move(wd));
            const bool f = ui.focused(i), hv = ui.hovered(i);
            D::round_rect(r, 10, f ? col::panel3 : hv ? col::panel2 : on ? col::panel : D::alpha(col::panel, 0.6f));
            if (on) D::round_rect_outline(r, 10, 1.5f, D::alpha(gold, 0.75f));
            else if (a.look.on) D::round_rect_outline(r, 10, 1.0f, D::alpha(col::edge, 0.35f));
            if (f) focus_ring(r, 10);
            // the mark: a gold disc with a tick, or an empty ring
            const float mx = cx + 32, my = cy + 30;
            if (on) {
                D::disc(mx, my, 15, gold);
                D::check(mx, my, 16, 2.6f, col::ink);
            } else {
                D::ring(mx, my, 15, 2, col::faint);
            }
            const float tx = cx + 62, tw = cw - 62 - 16;
            const std::string badge = on ? "UNLOCKED" : "LOCKED";
            const float bwid = a.text->width(badge, kSmall - 1, true) + 16;
            text(a, a.text->elide(c.title, kBody + 1, tw - bwid - 10, true), tx, cy + 12, kBody + 1, on ? col::text : col::dim, true);
            D::round_rect(SDL_FRect{cx + cw - bwid - 14, cy + 13, bwid, 20}, 10, on ? gold : D::alpha(col::faint, 0.35f));
            text_center(a, badge, cx + cw - bwid * 0.5f - 14, cy + 13 + (20 - a.text->line_height(kSmall - 1)) * 0.5f, kSmall - 1, on ? col::ink : col::dim, true);
            text(a, a.text->elide(c.description, kSmall, tw), tx, cy + 38, kSmall, col::dim);
            const std::string prog = S::group_digits(c.progress) + " / " + S::group_digits(c.target);
            const float pw = a.text->width(prog, kSmall - 1);
            progress_bar(SDL_FRect{tx, cy + 66, tw - pw - 12, 6}, c.target ? double(c.progress) / double(c.target) : 0.0, on ? gold : col::accent);
            text_right(a, prog, tx + tw, cy + 59, kSmall - 1, on ? col::text : col::faint);
        }
        y += float((list.size() + 1) / 2) * (ch + 12);
    }
    y += 6;
    y += paragraph(a, "Read from the profile files as the game saved them (this page never changes a save). Values the game "
                      "works out from its own tables, such as game complete %, cars collected and medals per region, are in "
                      "the game's own STATS page.",
                   x + 4, y, w - 8, kSmall, col::faint);
    for (const std::string& note : a.stats_notes) y += 6 + paragraph(a, note, x + 4, y + 6, w - 8, kSmall, col::bad);
    a.content_h = y;
}

// ---- the REPLAYS page (replay1, remaster-features §5 "Replay files"; src/native/replay_file.h) ----
// The replay files in <pref>/replays, newest first: when, how long, the file; Play runs the game with --replay (the
// recorded inputs from the boot on, a throwaway copy of the card: the player's saves are not touched), Delete asks
// first, Open folder shows them. RECORD REPLAYS (PC SETTINGS > INTERFACE) is shown and switched here too.

// Whether this port plays it: "" (yes), or why not
std::string replay_verdict(const b3native::replay::Entry& e, b3native::replay::Compat* out = nullptr) {
    if (!e.replay) return "This file " + e.error + ".";
    const b3native::replay::Compat c = b3native::replay::compatible(*e.replay, {kProgramSize, kProgramFnv, b3port::version()});
    if (out) *out = c;
    return c.verdict == b3native::replay::Compat::kRefuse ? c.text : std::string();
}

void play_replay(App& a, const b3native::replay::Entry& e) {
    // native_play's narrow arguments on Windows carry ASCII only: a file in the replays folder goes by its name alone
    // (native_play looks for a bare name there), any other path must be ASCII
    const std::string full = utf8_of(e.path), name = utf8_of(e.path.filename());
    auto ascii = [](const std::string& s) { return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; }); };
    std::error_code ec;
    const bool in_folder = fs::equivalent(e.path.parent_path(), b3native::replay::replays_dir(a.pref), ec);
    std::string arg = ascii(full) ? full : in_folder && ascii(name) ? name : std::string();
    if (arg.empty()) {
        message(a, "Cannot play this replay", "Its file name has letters the game cannot be given on this system. Rename it (letters A-Z, digits) and try again.");
        return;
    }
    start_game(a, false, {"--replay", arg});
}

void confirm_delete_replay(App& a, std::size_t k) {
    if (k >= a.replays.size()) return;
    const fs::path p = a.replays[k].path;
    Modal m;
    m.title = "Delete this replay?";
    m.text = utf8_of(p.filename()) + " is deleted for good.";
    m.buttons = {{"Delete", [&a, p]() {
                      std::error_code ec;
                      const bool ok = fs::remove(p, ec);
                      a.replay_note = ok ? "Deleted " + utf8_of(p.filename()) + "." : "Cannot delete it: " + ec.message();
                      a.replay_note_bad = !ok;
                      load_replays(a);
                  }, true},
                 {"Cancel", nullptr}};
    a.modal = m;
}

void page_replays(App& a, Ui& ui, float x, float y, float w) {
    namespace R = b3native::replay;
    const fs::path dir = R::replays_dir(a.pref);
    text(a, "Replays folder", x, y, kBody, col::text, true);
    text(a, a.text->elide(utf8_of(dir), kBody, w - 160 - 200), x + 160, y, kBody, col::dim);
    button(ui, SDL_FRect{x + w - 190, y - 10, 190, 42}, "Open folder", Style::kNormal, true,
           "Opens the folder in the file manager. A replay file is small: copy it to keep it (a renamed one is never removed) or "
           "to send it; it plays on any PC with this release of the port and the same disc.",
           [&a, dir]() { open_folder(dir, &a.replay_note); });
    y += 46;
    {   // RECORD REPLAYS, as in the game's PC SETTINGS > INTERFACE
        const bool on = a.settings.record_replays != 0;
        button(ui, SDL_FRect{x, y, 300, 46}, on ? "Recording: On" : "Recording: Off", on ? Style::kNormal : Style::kQuiet, true,
               "RECORD REPLAYS (also in the game's SETTINGS > INTERFACE). On: each session, from the start of the game to its end, "
               "is kept here. The newest 20 are kept. Applies at the next start.",
               [&a]() {
                   a.settings.record_replays = a.settings.record_replays ? 0 : 1;
                   save_settings(a);
               });
        paragraph(a, on ? "Each session of the game is recorded from its start to its end and kept here (the newest 20)."
                        : "Turn recording on to keep your sessions here; they play back exactly as you played them.",
                  x + 316, y + 4, w - 316, kSmall, col::dim);
        y += 46 + 18;
    }
    if (a.replays.empty()) {
        const float top = y;
        const std::string t1 = "No replays yet.";
        const std::string t2 = "A replay is a recording of the game's inputs from its start, with the save it started from, so it "
                               "plays the session again exactly: every race, crash and menu as it was. Turn Recording on, play, quit "
                               "the game, and the session shows here.";
        const float h = a.text->line_height(19) + 8 + float(a.text->wrap(t2, kBody, w - 48).size()) * a.text->line_height(kBody);
        D::round_rect(SDL_FRect{x, top, w, h + 44}, 12, col::panel);
        D::rect(SDL_FRect{x, top + 14, 4, h + 16}, col::accent);
        text(a, t1, x + 24, top + 22, 19, col::text, true);
        paragraph(a, t2, x + 24, top + 22 + a.text->line_height(19) + 8, w - 48, kBody, col::dim);
        y = top + h + 44 + 14;
    }
    if (a.replay_sel >= a.replays.size()) a.replay_sel = 0;
    for (std::size_t k = 0; k < a.replays.size(); ++k) {
        const R::Entry& e = a.replays[k];
        const std::string bad = replay_verdict(e);
        Widget wd;
        wd.r = SDL_FRect{x, y, w, 58};
        wd.desc = bad.empty() ? "Press to choose this replay, then Play below (or press again to play it)." : bad;
        wd.activate = [&a, k]() {
            if (a.replay_sel == k && k < a.replays.size() && replay_verdict(a.replays[k]).empty() && a.game.ok) play_replay(a, a.replays[k]);
            a.replay_sel = k;
        };
        const int i = ui.add(std::move(wd));
        const bool f = ui.focused(i), hv = ui.hovered(i), sel = k == a.replay_sel;
        const SDL_FRect r{x, y, w, 58};
        D::round_rect(r, 10, f ? col::panel3 : hv ? col::panel2 : col::panel);
        if (sel) D::round_rect_outline(r, 10, 1.5f, D::alpha(col::accent, 0.8f));
        if (f) D::rect(SDL_FRect{x, y + 10, 3, 38}, col::accent);
        D::ring(x + 28, y + 29, 10, 2, sel ? col::accent : col::faint);
        if (sel) D::disc(x + 28, y + 29, 5, col::accent);
        std::string title = e.replay ? format_time(e.replay->started) : utf8_of(e.path.filename());
        if (e.replay) title += "   " + R::format_length(e.replay->vblanks);
        text(a, title, x + 52, y + 8, 18, bad.empty() ? col::text : col::faint, true);
        std::string sub = utf8_of(e.path.filename()) + "   |   " + format_bytes(e.bytes);
        if (e.replay) {
            if (const std::string* d = e.replay->info_value("driving")) sub += "   |   driving: " + *d;
            sub += "   |   " + e.replay->version;
        }
        text(a, a.text->elide(sub, kSmall, w - 52 - 150), x + 52, y + 32, kSmall, col::dim);
        if (!bad.empty()) {
            const float bw = a.text->width("CANNOT PLAY", kSmall, true) + 20;
            D::round_rect(SDL_FRect{x + w - bw - 16, y + 17, bw, 24}, 12, D::alpha(col::bad, 0.8f));
            text_center(a, "CANNOT PLAY", x + w - bw * 0.5f - 16, y + 17 + (24 - a.text->line_height(kSmall)) * 0.5f, kSmall, col::ink, true);
        }
        y += 58 + 8;
    }
    if (!a.replays.empty()) {
        y += 14;
        const R::Entry& e = a.replays[a.replay_sel];
        R::Compat c;
        const std::string bad = replay_verdict(e, &c);
        const bool can = bad.empty() && a.game.ok;
        button(ui, SDL_FRect{x, y, 230, 50}, "Play", Style::kPrimary, can,
               can ? "Plays the chosen replay in the game's window: the recorded controls from the start, a throwaway copy of the "
                     "save it started from (your own saves are not touched). Start pauses it; closing the window ends it."
                   : !bad.empty() ? bad : "Choose your game data on the Game Data page first.",
               [&a]() { if (a.replay_sel < a.replays.size()) play_replay(a, a.replays[a.replay_sel]); });
        button(ui, SDL_FRect{x + 246, y + 2, 180, 46}, "Delete...", Style::kDanger, true, "Deletes the chosen replay file. Asks first.",
               [&a]() { confirm_delete_replay(a, a.replay_sel); });
        y += 50 + 14;
        if (e.replay) {
            std::string about = "Recorded " + format_time(e.replay->started) + " by " + e.replay->version;
            if (const std::string* h = e.replay->info_value("host")) about += " on " + *h;
            if (const std::string* end = e.replay->info_value("end")) about += "; it ended with " + *end;
            about += ". " + std::string(e.replay->card ? "It starts from the save of file " + std::to_string(e.replay->card_slot) + " as it was then."
                                                       : "It started without a memory card.");
            if (c.verdict == R::Compat::kWarn) about += " " + c.text;
            y += paragraph(a, about, x + 4, y, w - 8, kSmall, col::faint) + 6;
        }
    }
    if (!a.replay_note.empty()) y += paragraph(a, a.replay_note, x + 4, y, w - 8, kBody, a.replay_note_bad ? col::bad : col::ok) + 6;
    a.content_h = y;
}

// ---- the CONTROLLERS page ----

std::string pad_type_name(SDL_GamepadType t) {
    switch (t) {
    case SDL_GAMEPAD_TYPE_XBOX360: return "Xbox 360";
    case SDL_GAMEPAD_TYPE_XBOXONE: return "Xbox One / Series";
    case SDL_GAMEPAD_TYPE_PS3: return "PlayStation 3";
    case SDL_GAMEPAD_TYPE_PS4: return "PlayStation 4";
    case SDL_GAMEPAD_TYPE_PS5: return "PlayStation 5";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return "Switch Pro";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return "Joy-Con (L)";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return "Joy-Con (R)";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "Joy-Con pair";
    case SDL_GAMEPAD_TYPE_STANDARD: return "Standard";
    default: {
        const char* s = SDL_GetGamepadStringForType(t);
        return s && *s ? s : "Unknown";
    }
    }
}

bool pad_rumbles(SDL_Gamepad* g) {
    return SDL_GetBooleanProperty(SDL_GetGamepadProperties(g), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
}

b3native::input::GamepadState pad_state(SDL_Gamepad* g) {
    return b3native::input::gamepad_state(g);   // as src/b3replay/nativepad.cpp poll_gamepads reads it
}

void face_symbol(App& a, SDL_GamepadButtonLabel l, float cx, float cy, float r, SDL_FColor c) {
    switch (l) {
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: D::cross(cx, cy, r * 1.55f, r * 0.2f, c); break;
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: D::ring(cx, cy, r * 0.5f, r * 0.17f, c); break;
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: D::square(cx, cy, r * 1.5f, c); break;
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: D::triangle(cx, cy + r * 0.05f, r * 1.4f, c); break;
    default: {
        const char* t = l == SDL_GAMEPAD_BUTTON_LABEL_A ? "A" : l == SDL_GAMEPAD_BUTTON_LABEL_B ? "B" : l == SDL_GAMEPAD_BUTTON_LABEL_X ? "X"
                      : l == SDL_GAMEPAD_BUTTON_LABEL_Y ? "Y" : "?";
        text_center(a, t, cx, cy - a.text->line_height(r * 1.1f) * 0.5f, r * 1.1f, c, true);
    }
    }
}

void draw_pad(App& a, SDL_Gamepad* g, float x, float y, float w, float h) {
    D::round_rect(SDL_FRect{x, y, w, h}, 14, col::panel);
    const SDL_GamepadType type = SDL_GetGamepadType(g);
    const bool ps = type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(g, b); };
    auto ax = [&](SDL_GamepadAxis s) { return float(SDL_GetGamepadAxis(g, s)) / 32767.0f; };
    const SDL_FColor on = col::accent, off = col::panel3;
    // triggers and shoulders
    const float tw = w * 0.2f;
    for (int side = 0; side < 2; ++side) {
        const float tx = side ? x + w - 28 - tw : x + 28;
        const float v = std::clamp(ax(side ? SDL_GAMEPAD_AXIS_RIGHT_TRIGGER : SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f, 1.0f);
        D::round_rect(SDL_FRect{tx, y + 18, tw, 14}, 7, off);
        if (v > 0.01f) D::round_rect(SDL_FRect{tx, y + 18, tw * v, 14}, 7, on);
        text(a, side ? (ps ? "R2" : "RT") : (ps ? "L2" : "LT"), side ? tx - 30 : tx + tw + 8, y + 15, kSmall, col::dim, true);
        const bool sh = btn(side ? SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER : SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        D::round_rect(SDL_FRect{tx, y + 40, tw, 22}, 8, sh ? on : off);
        text_center(a, side ? (ps ? "R1" : "RB") : (ps ? "L1" : "LB"), tx + tw * 0.5f, y + 40 + (22 - a.text->line_height(kSmall)) * 0.5f,
                    kSmall, sh ? col::ink : col::dim, true);
    }
    const float cy1 = y + h * 0.47f, cy2 = y + h * 0.76f;
    const float r = std::min(w, h) * 0.075f;
    // the D-pad and the face buttons
    const float dpx = ps ? x + w * 0.2f : x + w * 0.36f, dpy = ps ? cy1 : cy2;
    const float fbx = x + w * 0.8f, fby = cy1;
    const float lsx = ps ? x + w * 0.36f : x + w * 0.2f, lsy = ps ? cy2 : cy1;
    const float rsx = ps ? x + w * 0.64f : x + w * 0.64f, rsy = cy2;
    struct Dir { SDL_GamepadButton b; float dx, dy; int chev; };
    for (const Dir& d : {Dir{SDL_GAMEPAD_BUTTON_DPAD_UP, 0, -1, -2}, Dir{SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0, 1, 2},
                         Dir{SDL_GAMEPAD_BUTTON_DPAD_LEFT, -1, 0, -1}, Dir{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 1, 0, 1}}) {
        const float bx = dpx + d.dx * r * 1.15f, by = dpy + d.dy * r * 1.15f;
        const bool p = btn(d.b);
        D::round_rect(SDL_FRect{bx - r * 0.55f, by - r * 0.55f, r * 1.1f, r * 1.1f}, 4, p ? on : off);
        D::chevron(bx, by, r * 0.45f, d.chev, 2.0f, p ? col::ink : col::dim);
    }
    struct Face { SDL_GamepadButton b; float dx, dy; };
    for (const Face& f : {Face{SDL_GAMEPAD_BUTTON_SOUTH, 0, 1}, Face{SDL_GAMEPAD_BUTTON_EAST, 1, 0}, Face{SDL_GAMEPAD_BUTTON_WEST, -1, 0},
                          Face{SDL_GAMEPAD_BUTTON_NORTH, 0, -1}}) {
        const float bx = fbx + f.dx * r * 1.3f, by = fby + f.dy * r * 1.3f;
        const bool p = btn(f.b);
        D::disc(bx, by, r * 0.62f, p ? on : off);
        face_symbol(a, SDL_GetGamepadButtonLabel(g, f.b), bx, by, r * 0.62f, p ? col::ink : col::dim);
    }
    // the sticks
    auto stick = [&](float sx, float sy, SDL_GamepadAxis axx, SDL_GamepadAxis axy, SDL_GamepadButton click, const char* name) {
        const bool p = btn(click);
        D::disc(sx, sy, r * 1.05f, p ? D::alpha(col::accent, 0.35f) : col::panel2);
        D::ring(sx, sy, r * 1.05f, 2, col::panel3);
        const float vx = std::clamp(ax(axx), -1.0f, 1.0f), vy = std::clamp(ax(axy), -1.0f, 1.0f);
        D::line(sx, sy, sx + vx * r * 0.8f, sy + vy * r * 0.8f, 2, D::alpha(col::accent, 0.6f));
        D::disc(sx + vx * r * 0.8f, sy + vy * r * 0.8f, r * 0.34f, (std::fabs(vx) > 0.2f || std::fabs(vy) > 0.2f || p) ? on : col::dim);
        text_center(a, name, sx, sy + r * 1.15f, kSmall - 1, col::faint);
    };
    stick(lsx, lsy, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_BUTTON_LEFT_STICK, ps ? "L3" : "LS");
    stick(rsx, rsy, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_BUTTON_RIGHT_STICK, ps ? "R3" : "RS");
    // back / guide / start
    const char* back = type == SDL_GAMEPAD_TYPE_PS3 ? "SELECT" : type == SDL_GAMEPAD_TYPE_PS4 ? "SHARE" : type == SDL_GAMEPAD_TYPE_PS5 ? "CREATE"
                     : type == SDL_GAMEPAD_TYPE_XBOXONE ? "VIEW" : "BACK";
    const char* start = type == SDL_GAMEPAD_TYPE_PS3 ? "START" : ps ? "OPTIONS" : type == SDL_GAMEPAD_TYPE_XBOXONE ? "MENU" : "START";
    struct Mid { SDL_GamepadButton b; float dx; const char* t; };
    for (const Mid& m : {Mid{SDL_GAMEPAD_BUTTON_BACK, -1, back}, Mid{SDL_GAMEPAD_BUTTON_GUIDE, 0, "GUIDE"}, Mid{SDL_GAMEPAD_BUTTON_START, 1, start}}) {
        const float bw = 64, bx = x + w * 0.5f + m.dx * 74 - bw * 0.5f, by = y + h * 0.33f;
        const bool p = btn(m.b);
        D::round_rect(SDL_FRect{bx, by, bw, 22}, 11, p ? on : off);
        text_center(a, m.t, bx + bw * 0.5f, by + (22 - a.text->line_height(11)) * 0.5f, 11, p ? col::ink : col::dim, true);
    }
}

void ds2_strip(App& a, const std::array<std::uint8_t, 32>& s, float x, float y, float w) {
    using b3native::input::Ds2;
    const unsigned bits = unsigned(s[0]) | (unsigned(s[1]) << 8);
    static const Ds2 order[] = {Ds2::Cross, Ds2::Circle, Ds2::Square, Ds2::Triangle, Ds2::L1, Ds2::R1, Ds2::L2, Ds2::R2,
                                Ds2::Up, Ds2::Down, Ds2::Left, Ds2::Right, Ds2::Start, Ds2::Select, Ds2::L3, Ds2::R3};
    float bx = x;
    float by = y;
    for (Ds2 d : order) {
        const char* n = menu_name(d);
        const float bw = a.text->width(n, kSmall, true) + 18;
        if (bx + bw > x + w) {
            bx = x;
            by += 30;
        }
        const bool p = !((bits >> unsigned(d)) & 1u);
        D::round_rect(SDL_FRect{bx, by, bw, 24}, 12, p ? col::accent : col::panel2);
        text_center(a, n, bx + bw * 0.5f, by + (24 - a.text->line_height(kSmall)) * 0.5f, kSmall, p ? col::ink : col::dim, true);
        bx += bw + 6;
    }
    // the left stick as the game gets it (bytes 4 / 5)
    auto axis = [](std::uint8_t b) { const float v = (float(b) - 127.5f) / 127.5f; return std::fabs(v) < 0.01f ? 0.0f : v; };   // neutral 0x80 / 0x7F
    const float lx = axis(s[4]), ly = axis(s[5]);
    char buf[64];
    std::snprintf(buf, sizeof buf, "Left stick  %+.2f  %+.2f", lx, ly);
    text(a, buf, bx + 8, by + (24 - a.text->line_height(kSmall)) * 0.5f, kSmall, col::dim);
}

// ---- remap1: the control bindings (src/native/input_bindings.h; the in-game SETTINGS > INTERFACE > CONTROLS edits the
// same controls.ini) ----

fs::path controls_file(const fs::path& pref) { return pref / "controls.ini"; }

void controls_load(App& a) {
    std::vector<std::string> warnings;
    a.controls_note.clear();
    if (!b3native::input::load_controls(utf8_of(controls_file(a.pref)), a.controls, &warnings)) a.controls = b3native::input::default_controls();
    if (!warnings.empty()) a.controls_note = "controls.ini read with notes: " + warnings.front() + (warnings.size() > 1 ? " (and more)" : "");
    a.keys.set_map(b3native::input::key_map(a.controls.player[0]));
}

void controls_save(App& a, const std::string& what) {
    std::string why;
    a.keys.set_map(b3native::input::key_map(a.controls.player[0]));
    a.controls_note = b3native::input::save_controls(utf8_of(controls_file(a.pref)), a.controls, &why) ? what + ": saved (the game reads it at its next start)."
                                                                                                       : "Cannot save the controls: " + why;
}

b3native::input::PadStyle pad_style_of(App& a) {
    if (a.pads.empty()) return b3native::input::PadStyle::Generic;
    SDL_Gamepad* g = a.pads[std::min(a.pad_sel, a.pads.size() - 1)];
    const SDL_GamepadType t = SDL_GetGamepadType(g);
    if (t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5) return b3native::input::PadStyle::PlayStation;
    switch (SDL_GetGamepadButtonLabel(g, SDL_GAMEPAD_BUTTON_SOUTH)) {
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return b3native::input::PadStyle::PlayStation;
    case SDL_GAMEPAD_BUTTON_LABEL_A: return b3native::input::PadStyle::Xbox;
    case SDL_GAMEPAD_BUTTON_LABEL_B: return b3native::input::PadStyle::Nintendo;
    default: return b3native::input::PadStyle::Generic;
    }
}

std::size_t ctl_pl(const App& a) { return std::min<std::size_t>(a.ctl_player, 1); }   // the player of the editor (rebind2: 2 is the shortcuts)
bool ctl_shortcuts(const App& a) { return a.ctl_player >= 2; }
b3native::input::Device ctl_device(const App& a) { return a.ctl_device == 0 ? b3native::input::Device::Keyboard : b3native::input::Device::Gamepad; }
b3native::input::KeyCtx ctl_view(const App& a) {   // padlayout: the controller's view too
    return a.ctl_view == 0 ? b3native::input::KeyCtx::Drive : b3native::input::KeyCtx::Menu;
}
// padlayout: the family of the controller the page shows (the selected one; Generic without one): the layout Auto gives
b3native::input::PadStyle editor_family(const App& a) {
    if (a.pads.empty()) return b3native::input::PadStyle::Generic;
    return b3native::input::pad_family(int(SDL_GetGamepadType(a.pads[std::min(a.pad_sel, a.pads.size() - 1)])));
}
void editor_families(App& a) {
    for (int p = 0; p < b3native::input::kPlayers; ++p) b3native::input::set_page_family(a.controls, p, editor_family(a));
}
std::string lower_words(std::string s) {
    // 1.2.1: UTF-8 aware (a translated label: "CAPTURE D'ÉCRAN" -> "Capture D'Écran"): a letter after a letter lowered,
    // ASCII and Latin-1 (U+00C0..U+00DE, not the sign U+00D7)
    bool prev = false;
    for (std::size_t k = 0; k < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[k]);
        if (c == 0xC3 && k + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[k + 1]);
            const unsigned cp = 0xC0u + (d & 0x3Fu);
            const bool letter = cp != 0xD7 && cp != 0xF7;
            if (letter && prev && cp <= 0xDE) s[k + 1] = char(d + 0x20);
            prev = letter;
            k += 2;
        } else {
            const bool letter = std::isalpha(c) != 0;
            if (letter && prev) s[k] = char(std::tolower(c));
            prev = letter;
            k += (c < 0x80) ? 1 : (c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2);
        }
    }
    return s;
}

// What a finished flow changed, for the note: "Player 1 cross: ENTER, Z" / "Shortcuts screenshot: F12"
std::string flow_what(const App& a) {
    namespace in = b3native::input;
    const in::RebindFlow& f = a.flow;
    if (f.is_port())
        return "Shortcuts " + lower_words(in::port_label(f.port_action())) + ": " +
               in::bound_port_text(a.controls, f.port_action(), f.device(), pad_style_of(const_cast<App&>(a)));
    return "Player " + std::to_string(f.player() + 1) + " " + lower_words(in::action_label(f.action())) + ": " +
           in::bound_text(a.controls, f.player(), f.device(), f.action(), f.view(), pad_style_of(const_cast<App&>(a)));
}

// The modal for the flow's state: waiting (Cancel), a conflict (Swap / Clear / Keep both / Cancel); none when it ended
void flow_modal(App& a) {
    namespace in = b3native::input;
    using S = in::RebindFlow::State;
    const in::RebindFlow& f = a.flow;
    const std::string act = lower_words(f.is_port() ? in::port_label(f.port_action()) : in::action_label(f.action()));
    const std::string who = f.is_port() ? std::string("Shortcuts, ") + (f.device() == in::Device::Gamepad ? "controller" : "keyboard")
                                        : "Player " + std::to_string(f.player() + 1) +
                                              (f.device() == in::Device::Gamepad ? (f.view() == in::KeyCtx::Menu ? ", controller in menus" : ", controller while driving")
                                                                                 : f.view() == in::KeyCtx::Drive ? ", keyboard while driving" : ", keyboard in menus");
    if (f.state() == S::Waiting) {
        Modal m;
        m.title = f.device() == in::Device::Keyboard ? "Press a key for " + act : "Press a controller input for " + act;
        m.text = who + ". " + (f.device() == in::Device::Keyboard ? "Press the key now" : "Press a button, or push a stick or trigger, on any controller") +
                 ". Esc cancels (" + std::to_string(std::max(1, int(std::ceil(f.remaining())))) + " s).";
        m.buttons = {{"Cancel", [&a]() { a.flow.cancel(); }}};
        a.modal = m;
        return;
    }
    if (f.state() == S::Conflict) {
        const std::string inl = in::input_label(f.pressed(), pad_style_of(a));
        std::string list;
        for (const in::Use& u : f.conflicts()) {
            if (!list.empty()) list += ", ";
            if (u.port) list += lower_words(in::port_label(u.port_action));
            else {
                if (f.is_port() || u.player != f.player()) list += "player " + std::to_string(u.player + 1) + " ";
                list += lower_words(in::action_label(u.action));
            }
        }
        Modal m;
        m.title = inl + " is in use";
        const std::string swap = f.swap_input() ? "Swap: they get " + in::input_label(*f.swap_input(), pad_style_of(a)) + " (" + act + "'s before)" : "Swap: they lose it";
        m.text = who + ": " + inl + " also drives " + list + ". " + swap + ". Clear: they lose " + inl +
                 ". Keep both: " + inl + " drives all of them.";
        auto pick = [&a](int c) {
            return [&a, c]() {
                a.flow.set_choice(c);
                a.flow.choose(a.controls);
                controls_save(a, flow_what(a));
            };
        };
        m.buttons = {{"Swap", pick(0)}, {"Clear", pick(1)}, {"Keep both", pick(2)}, {"Cancel", [&a]() { a.flow.cancel(); }}};
        a.modal = m;
        return;
    }
    a.modal.reset();
}

void flow_begin(App& a, b3native::input::Ds2 action, bool add) {
    a.flow.begin(int(ctl_pl(a)), ctl_device(a), action, ctl_view(a), add);
    a.flow_t = a.now();
    a.flow_rest.clear();
    flow_modal(a);
}

void flow_begin_port(App& a, b3native::input::PortAction action, bool add) {   // rebind2: a shortcut or a photo mode control
    a.flow.begin_port(action, ctl_device(a), add);
    a.flow_t = a.now();
    a.flow_rest.clear();
    flow_modal(a);
}

// After an input: done (saved), a conflict (its modal) or still waiting
void flow_after(App& a) {
    using S = b3native::input::RebindFlow::State;
    if (a.flow.state() == S::Done)
        controls_save(a, flow_what(a));
    flow_modal(a);
}

// A window event while the flow waits: true when it took it (a key or pad button never reaches the page meanwhile)
bool flow_event(App& a, const SDL_Event& e) {
    namespace in = b3native::input;
    if (a.flow.state() != in::RebindFlow::State::Waiting) return false;
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.repeat) return true;
        if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
            a.flow.cancel();
            a.modal.reset();
            a.controls_note = "Rebinding cancelled.";
            return true;
        }
        // rebind2: any key, the modifier keys themselves too (Ctrl + a key is not one key)
        if (a.flow.device() == in::Device::Keyboard && (e.key.scancode >= SDL_SCANCODE_LCTRL || !(e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)))) {
            a.flow.input(a.controls, {in::Device::Keyboard, std::uint16_t(e.key.scancode), in::PadInput::None});
            flow_after(a);
        }
        return true;
    }
    if (e.type == SDL_EVENT_KEY_UP) return true;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (a.flow.device() == in::Device::Gamepad) {
            const in::PadInput p = in::pad_input_of_button(e.gbutton.button);
            if (p != in::PadInput::None) {
                a.flow.input(a.controls, {in::Device::Gamepad, 0, p});
                flow_after(a);
            }
        } else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
            a.flow.cancel();
            a.modal.reset();
            a.controls_note = "Rebinding cancelled.";
        }
        return true;
    }
    return false;
}

// Each loop: the timeout, a stick or trigger pushed from rest (a controller flow), the modal's countdown
void flow_poll(App& a) {
    namespace in = b3native::input;
    using S = in::RebindFlow::State;
    if (!a.flow.active()) return;
    if (!a.modal) {   // the modal closed (Back, a click on Cancel)
        a.flow.cancel();
        a.controls_note = "Rebinding cancelled.";
        return;
    }
    if (a.flow.state() != S::Waiting) return;
    const double t = a.now();
    const int before = int(std::ceil(a.flow.remaining()));
    a.flow.tick(float(t - a.flow_t));
    a.flow_t = t;
    if (a.flow.device() == in::Device::Gamepad)
        for (SDL_Gamepad* g : a.pads) {
            const SDL_JoystickID id = SDL_GetGamepadID(g);
            auto it = std::find_if(a.flow_rest.begin(), a.flow_rest.end(), [id](const auto& r) { return r.first == id; });
            if (it == a.flow_rest.end()) { a.flow_rest.push_back({id, {}}); it = a.flow_rest.end() - 1; }
            const in::GamepadState s = in::gamepad_state(g);
            for (int k = 0; k < 10 && a.flow.state() == S::Waiting; ++k) {
                const auto p = in::PadInput(int(in::PadInput::LeftXNeg) + k);
                const float v = in::pad_input_amount(s, p, 0.24f);
                if (v < 0.3f) it->second[std::size_t(k)] = true;
                else if (v > 0.6f && it->second[std::size_t(k)]) {
                    a.flow.input(a.controls, {in::Device::Gamepad, 0, p});
                    flow_after(a);
                }
            }
        }
    if (a.flow.state() == S::Cancelled) {
        a.modal.reset();
        a.controls_note = "Rebinding cancelled (no input in " + std::to_string(int(in::RebindFlow::kTimeout)) + " s).";
    } else if (a.flow.state() == S::Waiting && int(std::ceil(a.flow.remaining())) != before) {
        flow_modal(a);   // the countdown
    }
}

// A choice row's value area: x from .x to .y is its left half (the "<": a click there is one value back)
SDL_FPoint choice_half(const SDL_FRect& r) {
    const float vw = std::min(160.0f, r.w * 0.55f), vx = r.x + r.w - vw - 8;
    return SDL_FPoint{vx, vx + vw * 0.5f};
}

// A choice row of the editor: label left, the value between arrows; left / right and a click change it
void ctl_choice(App& a, Ui& ui, const SDL_FRect& r, const std::string& label, const std::vector<std::string>& values, std::size_t& v,
                const std::string& desc) {
    Widget wd;
    wd.r = r;
    wd.desc = desc;
    const std::size_t n = values.size();
    wd.change = [&v, n](int dir) { v = (v + n + std::size_t(dir + int(n))) % n; };
    wd.activate = [&v, n]() { v = (v + 1) % n; };
    wd.click = [&v, n, half = choice_half(r)](float mx, float) { v = (v + n + (mx >= half.x && mx < half.y ? n - 1 : 1)) % n; };
    const int i = ui.add(std::move(wd));
    const bool f = ui.focused(i), hv = ui.hovered(i);
    D::round_rect(r, 8, f ? col::panel3 : hv ? col::panel2 : col::panel);
    if (f) D::rect(SDL_FRect{r.x, r.y + 8, 3, r.h - 16}, col::accent);
    const float ty = r.y + (r.h - a.text->line_height(kBody)) * 0.5f;
    text(a, label, r.x + 14, ty, kBody, col::dim, f);
    const float vw = std::min(160.0f, r.w * 0.55f), vx = r.x + r.w - vw - 8;
    D::chevron(vx + 10, r.y + r.h * 0.5f, 11, -1, 2.0f, col::dim);
    D::chevron(vx + vw - 10, r.y + r.h * 0.5f, 11, +1, 2.0f, col::dim);
    text_center(a, values[std::min(v, n - 1)], vx + vw * 0.5f, ty, kBody, col::text, f);
}

// pad1: a choice like ctl_choice whose change goes through `set` (the gyro rows: saved at once)
void gyro_choice(App& a, Ui& ui, const SDL_FRect& r, const std::string& label, const std::vector<std::string>& values, std::size_t v,
                 const std::string& desc, std::function<void(std::size_t)> set) {
    Widget wd;
    wd.r = r;
    wd.desc = desc;
    const std::size_t n = values.size();
    wd.change = [v, n, set](int dir) { set((v + n + std::size_t(dir + int(n))) % n); };
    wd.activate = [v, n, set]() { set((v + 1) % n); };
    wd.click = [v, n, set, half = choice_half(r)](float mx, float) { set((v + n + (mx >= half.x && mx < half.y ? n - 1 : 1)) % n); };
    const int i = ui.add(std::move(wd));
    const bool f = ui.focused(i), hv = ui.hovered(i);
    D::round_rect(r, 8, f ? col::panel3 : hv ? col::panel2 : col::panel);
    if (f) D::rect(SDL_FRect{r.x, r.y + 8, 3, r.h - 16}, col::accent);
    const float ty = r.y + (r.h - a.text->line_height(kBody)) * 0.5f;
    text(a, label, r.x + 14, ty, kBody, col::dim, f);
    const float vw = std::min(160.0f, r.w * 0.55f), vx = r.x + r.w - vw - 8;
    D::chevron(vx + 10, r.y + r.h * 0.5f, 11, -1, 2.0f, col::dim);
    D::chevron(vx + vw - 10, r.y + r.h * 0.5f, 11, +1, 2.0f, col::dim);
    text_center(a, values[std::min(v, n - 1)], vx + vw * 0.5f, ty, kBody, col::text, f);
}

// padlayout: a full-width choice like gyro_choice with a wide value (Auto's "Auto: PlayStation controller")
void layout_choice(App& a, Ui& ui, const SDL_FRect& r, const std::string& label, const std::vector<std::string>& values, std::size_t v,
                   const std::string& desc, std::function<void(std::size_t)> set) {
    Widget wd;
    wd.r = r;
    wd.desc = desc;
    const std::size_t n = values.size();
    const float vw = std::min(420.0f, r.w * 0.6f), vx = r.x + r.w - vw - 8;
    wd.change = [v, n, set](int dir) { set((v + n + std::size_t(dir + int(n))) % n); };
    wd.activate = [v, n, set]() { set((v + 1) % n); };
    wd.click = [v, n, set, vx, vw](float mx, float) { set((v + n + (mx >= vx && mx < vx + vw * 0.5f ? n - 1 : 1)) % n); };
    const int i = ui.add(std::move(wd));
    const bool f = ui.focused(i), hv = ui.hovered(i);
    D::round_rect(r, 8, f ? col::panel3 : hv ? col::panel2 : col::panel);
    if (f) D::rect(SDL_FRect{r.x, r.y + 8, 3, r.h - 16}, col::accent);
    const float ty = r.y + (r.h - a.text->line_height(kBody)) * 0.5f;
    text(a, label, r.x + 14, ty, kBody, col::dim, f);
    D::chevron(vx + 10, r.y + r.h * 0.5f, 11, -1, 2.0f, col::dim);
    D::chevron(vx + vw - 10, r.y + r.h * 0.5f, 11, +1, 2.0f, col::dim);
    text_center(a, a.text->elide(values[std::min(v, n - 1)], kBody, vw - 48, f), vx + vw * 0.5f, ty, kBody, col::text, f);
}

// pad1 (each frame): the selected pad's gyro on while the Controllers page shows it with the editor's player's gyro on
// (off otherwise), and the trigger test's end; release: everything back (the launcher closing)
void pads_motion(App& a, bool release = false) {
    SDL_JoystickID want = 0;
    if (!release && a.page == kPads && !a.pads.empty() && a.controls.player[ctl_pl(a)].gyro.mode != 0) {
        SDL_Gamepad* g = a.pads[std::min(a.pad_sel, a.pads.size() - 1)];
        if (SDL_GamepadHasSensor(g, SDL_SENSOR_GYRO)) want = SDL_GetGamepadID(g);
    }
    if (want != a.gyro_pad) {
        for (const auto& [id, on] : {std::pair{a.gyro_pad, false}, std::pair{want, true}}) {
            SDL_Gamepad* g = id ? SDL_GetGamepadFromID(id) : nullptr;
            if (!g) continue;
            SDL_SetGamepadSensorEnabled(g, SDL_SENSOR_GYRO, on);
            if (SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL)) SDL_SetGamepadSensorEnabled(g, SDL_SENSOR_ACCEL, on);
        }
        a.gyro_pad = want;
        a.gyro.reset();
        a.gyro_recentre_held = false;
    }
    if (a.trig_test_pad && (release || a.now() > a.trig_test_until)) {
        if (SDL_Gamepad* g = SDL_GetGamepadFromID(a.trig_test_pad)) {
            const b3native::pad::Ds5Block off = b3native::pad::ds5_triggers_off();
            SDL_SendGamepadEffect(g, off.data(), int(off.size()));
        }
        a.trig_test_pad = 0;
        a.trig_note.clear();
    }
}

// The editor: player, device and view; a row per action (Enter / click: rebind, right: add, left: remove the last);
// reset. Returns the height used.
float controls_editor(App& a, Ui& ui, float x, float y, float w) {
    namespace in = b3native::input;
    float yy = y;
    yy += section(a, "Controls", x, yy);
    yy += paragraph(a, "Which key or controller input gives each PS2 button, per player. The game reads the same buttons as "
                       "before: only the input behind each one changes. Saved in controls.ini; the game reads it at its start "
                       "(in the game: SETTINGS > INTERFACE > CONTROLS).", x + 4, yy, w - 8, kSmall, col::dim) + 8;
    const bool shortcuts = ctl_shortcuts(a);
    const bool kb = a.ctl_device == 0 && !shortcuts;
    const bool padv = a.ctl_device != 0 && !shortcuts;   // padlayout: the controller's layout and views
    editor_families(a);
    const int cols = kb || padv ? 3 : 2;
    const float cw = (w - float(cols - 1) * 12) / float(cols);
    // launcherux1: the editor reads as one list for Up / Down (Ui::chain), from Player to Reset to defaults: left /
    // right change the choices side by side and the actions, so up / down step through the row of choices, then the
    // actions in their order (down the first column, then the second), the gyro rows, Reset
    const int first = int(ui.out.size());
    ctl_choice(a, ui, SDL_FRect{x, yy, cw, 44}, "Player", {"Player 1", "Player 2", "Shortcuts"}, a.ctl_player,
               "Whose controls. The keyboard is shared: player 2's keys (none at first) plug in a second controller for them. "
               "Shortcuts: the window's own keys (restart, fullscreen, screenshot, bug report, the tuning overlay, the settings "
               "sections) and photo mode's controls.");
    ctl_choice(a, ui, SDL_FRect{x + cw + 12, yy, cw, 44}, "Device", {"Keyboard", "Controller"}, a.ctl_device,
               "The keyboard's keys, or the buttons, sticks and triggers of this player's controllers.");
    if (kb)
        ctl_choice(a, ui, SDL_FRect{x + 2 * (cw + 12), yy, cw, 44}, "Keys for", {"Driving", "Menus"}, a.ctl_view,
                   "A key can do one thing while you drive and another in the menus (W accelerates, and moves up in a menu).");
    if (padv)
        ctl_choice(a, ui, SDL_FRect{x + 2 * (cw + 12), yy, cw, 44}, "Buttons for", {"Driving", "Menus"}, a.ctl_view,
                   "A button can do one thing while you drive and another in the menus (on the Xbox layout A boosts, and confirms in a menu).");
    yy += 44 + 12;
    if (padv) {   // padlayout (1.2.1): CONTROLLER LAYOUT, Auto worded for the controller shown
        const in::PlayerControls& pc = a.controls.player[ctl_pl(a)];
        layout_choice(a, ui, SDL_FRect{x, yy, w, 44}, "Layout",
                      {in::auto_text(editor_family(a), !a.pads.empty()), "Xbox", "PlayStation", "Custom"}, std::size_t(pc.layout),
                      "Which buttons do what on this player's controller. Auto: the layout of the controller's console, picked again when "
                      "the controller changes: a PlayStation controller gets the PS2 game's, any other controller the original Xbox game's. "
                      "Xbox, PlayStation: that layout on any controller. Custom: your own bindings in the rows below. Changing a binding "
                      "below makes it Custom.",
                      [&a](std::size_t v) {
                          const in::PadLayout l = in::PadLayout(std::min<std::size_t>(v, 3));
                          if (l == a.controls.player[ctl_pl(a)].layout) return;
                          in::set_layout(a.controls, int(ctl_pl(a)), l);
                          controls_save(a, "Player " + std::to_string(ctl_pl(a) + 1) + " controller layout " +
                                               in::layout_text(a.controls.player[ctl_pl(a)], editor_family(a), !a.pads.empty()));
                      });
        yy += 44 + 12;
    }
    const in::Device dev = ctl_device(a);
    const in::KeyCtx view = ctl_view(a);
    const in::PadStyle style = pad_style_of(a);
    // the rows: a game button of the player, or a shortcut (rebind2); one list, two columns
    struct Row {
        std::string label, desc, value;
        bool moved = false;
        std::function<void()> activate, add, remove, reset;
        std::function<void()> menu;
    };
    std::vector<Row> rows;
    const std::string hint = "Enter or click: press a new input for it. Right: add one more. Left: remove the last. Delete (X or Square on a controller): "
                             "back to the default. Right-click: the same as buttons.";
    if (shortcuts) {
        for (int pa = 0; pa < in::kPortActions; ++pa) {
            const in::PortAction act = in::PortAction(pa);
            if (dev == in::Device::Gamepad && !in::port_has_pad(act)) continue;
            Row r;
            r.label = lower_words(a.text->translated(in::port_label(act)));   // 1.2.1: in the player's language
            r.desc = std::string(in::port_use(act)) + " " + hint + (dev == in::Device::Gamepad ? " A controller shortcut is every input of it held together." : "");
            r.value = in::bound_port_text(a.controls, act, dev, style);
            r.moved = dev == in::Device::Keyboard ? a.controls.port.keys[std::size_t(act)] != in::default_port().keys[std::size_t(act)]
                                                  : a.controls.port.pad[std::size_t(act)] != in::default_port().pad[std::size_t(act)];
            r.activate = [&a, act]() { flow_begin_port(a, act, false); };
            r.add = [&a, act]() { flow_begin_port(a, act, true); };
            r.remove = [&a, act]() {
                const in::Controls before = a.controls;
                in::remove_last_port(a.controls, act, ctl_device(a));
                if (!(a.controls == before))
                    controls_save(a, "Shortcuts " + lower_words(in::port_label(act)) + ": " + in::bound_port_text(a.controls, act, ctl_device(a), pad_style_of(a)));
            };
            r.reset = [&a, act]() {
                const in::Controls before = a.controls;
                in::reset_port(a.controls, act, ctl_device(a));
                if (!(a.controls == before))
                    controls_save(a, "Shortcuts " + lower_words(in::port_label(act)) + ": " + in::bound_port_text(a.controls, act, ctl_device(a), pad_style_of(a)));
            };
            rows.push_back(std::move(r));
        }
    } else {
        for (const in::Ds2 act : in::action_order()) {
            Row r;
            r.label = lower_words(a.text->translated(in::action_label(act)));
            const std::string drive = in::action_use(act, true), menus = in::action_use(act, false);
            r.desc = std::string(drive.empty() ? "" : "Driving: " + drive + ". ") + (menus.empty() ? "" : "Menus: " + menus + ". ") + hint;
            r.value = in::bound_text(a.controls, int(ctl_pl(a)), dev, act, view, style);
            r.moved = in::moved(a.controls, int(ctl_pl(a)), dev, act, view);   // padlayout: per view, against the layout's
            r.activate = [&a, act]() { flow_begin(a, act, false); };
            r.add = [&a, act]() { flow_begin(a, act, true); };
            r.remove = [&a, act]() {
                const in::Controls before = a.controls;
                in::remove_last(a.controls, int(ctl_pl(a)), ctl_device(a), act, ctl_view(a));
                if (!(a.controls == before))
                    controls_save(a, "Player " + std::to_string(ctl_pl(a) + 1) + " " + lower_words(in::action_label(act)) + ": " +
                                         in::bound_text(a.controls, int(ctl_pl(a)), ctl_device(a), act, ctl_view(a), pad_style_of(a)));
            };
            r.reset = [&a, act]() {
                const in::Controls before = a.controls;
                in::reset_action(a.controls, int(ctl_pl(a)), ctl_device(a), act, ctl_view(a));
                if (!(a.controls == before))
                    controls_save(a, "Player " + std::to_string(ctl_pl(a) + 1) + " " + lower_words(in::action_label(act)) + ": " +
                                         in::bound_text(a.controls, int(ctl_pl(a)), ctl_device(a), act, ctl_view(a), pad_style_of(a)));
            };
            rows.push_back(std::move(r));
        }
    }
    const std::size_t per = (rows.size() + 1) / 2;
    const float colw = (w - 16) * 0.5f, rh = 36;
    a.ctl_first_widget = int(ui.out.size());
    for (std::size_t k = 0; k < rows.size(); ++k) {
        Row& r = rows[k];
        const float rx = x + float(k / per) * (colw + 16), ry = yy + float(k % per) * (rh + 4);
        Widget wd;
        wd.r = SDL_FRect{rx, ry, colw, rh};
        wd.desc = r.desc;
        wd.activate = r.activate;
        wd.reset = r.reset;
        wd.alt = [&a, label = r.label, value = r.value, act = r.activate, add = r.add, rem = r.remove, rst = r.reset]() {   // launcherux1: all of it as buttons
            Modal m;
            m.title = label;
            m.text = (ctl_shortcuts(a) ? std::string("Shortcuts") : "Player " + std::to_string(ctl_pl(a) + 1)) +
                     (ctl_device(a) == b3native::input::Device::Gamepad ? (ctl_shortcuts(a) ? ", controller" : ctl_view(a) == b3native::input::KeyCtx::Drive ? ", controller while driving" : ", controller in menus")
                      : ctl_shortcuts(a) ? ", keyboard" : ctl_view(a) == b3native::input::KeyCtx::Drive ? ", keyboard while driving" : ", keyboard in menus") +
                     ": " + value + ". Change it to a new input, add one more, remove the last one, or put it back to the default.";
            m.buttons = {{"Change", act}, {"Add one more", add}, {"Remove the last", rem}, {"Back to default", rst}, {"Cancel", nullptr}};
            m.focus = 4;
            a.modal = m;
        };
        wd.change = [add = r.add, rem = r.remove](int dir) {
            if (dir > 0) add();
            else rem();
        };
        const int i = ui.add(std::move(wd));
        const bool f = ui.focused(i), hv = ui.hovered(i);
        D::round_rect(SDL_FRect{rx, ry, colw, rh}, 7, f ? col::panel3 : hv ? col::panel2 : col::panel);
        if (f) D::rect(SDL_FRect{rx, ry + 7, 3, rh - 14}, col::accent);
        const float ty = ry + (rh - a.text->line_height(kSmall)) * 0.5f;
        text(a, a.text->elide(r.label, kSmall, colw * 0.55f, f), rx + 12, ty, kSmall, col::text, f);
        const std::string& v = r.value;
        const float vw = std::min(colw * 0.45f, a.text->width(v, kSmall, true) + 20);
        const SDL_FRect chip{rx + colw - vw - 8, ry + 5, vw, rh - 10};
        D::round_rect(chip, 6, v == "-" ? D::alpha(col::panel3, 0.5f) : r.moved ? D::alpha(col::accent, 0.85f) : col::panel3);
        text_center(a, a.text->elide(v, kSmall, vw - 12, true), chip.x + chip.w * 0.5f, chip.y + (chip.h - a.text->line_height(kSmall)) * 0.5f, kSmall,
                    r.moved ? col::ink : v == "-" ? col::faint : col::text, true);
    }
    yy += float(per) * (rh + 4) + 8;
    if (dev == in::Device::Gamepad && !shortcuts) {   // pad1: this player's gyro steering (opt-in; saved in controls.ini)
        in::GyroControls& g = a.controls.player[ctl_pl(a)].gyro;
        const float gw = (w - 2 * 12) / 3.0f;
        std::vector<std::string> sens, dz, rc;
        for (int v = 25; v <= 300; v += 25) sens.push_back(std::to_string(v) + "%");
        for (int v = 0; v <= 10; ++v) dz.push_back(std::to_string(v) + " deg");
        const auto& ch = in::gyro_recentre_choices();
        for (in::PadInput p : ch) rc.push_back(p == in::PadInput::None ? "None" : lower_words(in::pad_input_label(p, style)));
        const std::string who = "Player " + std::to_string(ctl_pl(a) + 1) + " gyro ";
        const auto set = [&a, who](const char* what, auto apply) {
            return [&a, who, what, apply](std::size_t v) {
                in::GyroControls& gc = a.controls.player[ctl_pl(a)].gyro;
                const in::GyroControls before = gc;
                apply(gc, v);
                if (!(gc == before)) controls_save(a, who + what);
            };
        };
        gyro_choice(a, ui, SDL_FRect{x, yy, gw, 44}, "Gyro", {"Off", "Mix", "Replace"}, g.mode,
                    "Steer by moving a controller with a gyro (DualSense, DualShock 4, Switch Pro, Steam Deck) while driving. Mix: "
                    "with the stick; Replace: instead of the stick's steering. Changes the input the game gets; Off is the original.",
                    set("steering", [](in::GyroControls& c, std::size_t v) { c.mode = std::uint8_t(std::min<std::size_t>(v, 2)); }));
        gyro_choice(a, ui, SDL_FRect{x + gw + 12, yy, gw, 44}, "Gyro axis", {"Tilt", "Turn"}, g.axis,
                    "Tilt: lean the controller like a steering wheel (level is straight). Turn: turn it left and right (straight "
                    "where you start driving).",
                    set("axis", [](in::GyroControls& c, std::size_t v) { c.axis = std::uint8_t(std::min<std::size_t>(v, 1)); }));
        gyro_choice(a, ui, SDL_FRect{x + 2 * (gw + 12), yy, gw, 44}, "Sensitivity", sens, std::size_t(std::clamp(g.sensitivity, 25, 300) / 25 - 1),
                    "How far to move for full lock: 100% is 40 degrees of tilt (30 of turn); 200% half that.",
                    set("sensitivity", [](in::GyroControls& c, std::size_t v) { c.sensitivity = int(std::min<std::size_t>(v, 11) + 1) * 25; }));
        yy += 44 + 8;
        gyro_choice(a, ui, SDL_FRect{x, yy, gw, 44}, "Dead zone", dz, std::size_t(std::clamp(g.dead_zone, 0, 10)),
                    "How far the controller moves from straight before it steers.",
                    set("dead zone", [](in::GyroControls& c, std::size_t v) { c.dead_zone = int(std::min<std::size_t>(v, 10)); }));
        gyro_choice(a, ui, SDL_FRect{x + gw + 12, yy, gw, 44}, "Recentre", rc,
                    std::size_t(std::find(ch.begin(), ch.end(), g.recentre) - ch.begin()),
                    "The button that makes the controller's present position straight. It still does what it is bound to as well.",
                    set("recentre button", [&ch](in::GyroControls& c, std::size_t v) { c.recentre = ch[std::min(v, ch.size() - 1)]; }));
        yy += 44 + 12;
    }
    button(ui, SDL_FRect{x, yy, 200, 40}, "Reset this list", Style::kNormal, true,
           "This player's keyboard or controller (or the shortcuts of this device) back to the original layout (the other device stays).", [&a]() {
               if (ctl_shortcuts(a)) {
                   for (int pa = 0; pa < b3native::input::kPortActions; ++pa) b3native::input::reset_port(a.controls, b3native::input::PortAction(pa), ctl_device(a));
                   controls_save(a, std::string("Shortcuts ") + (a.ctl_device == 0 ? "keyboard" : "controller") + " reset to the defaults");
                   return;
               }
               b3native::input::reset(a.controls, int(ctl_pl(a)), ctl_device(a));
               controls_save(a, "Player " + std::to_string(ctl_pl(a) + 1) + (a.ctl_device == 0 ? " keyboard" : " controller") + " reset to the defaults");
           });
    button(ui, SDL_FRect{x + 212, yy, 220, 40}, "Reset all controls", Style::kDanger, true,
           "Everything back to the original layout: both players' keyboards and controllers, the gyro settings and the shortcuts. It asks first.", [&a]() {
               Modal m;
               m.title = "Reset all controls?";
               m.text = "Both players' keyboards and controllers, the gyro settings and the shortcuts go back to the original layout.";
               m.buttons = {{"Reset all", [&a]() {
                                 b3native::input::reset_all(a.controls);
                                 controls_save(a, "All controls reset to the defaults");
                             }, true},
                            {"Cancel", nullptr}};
               m.focus = 1;
               a.modal = m;
           });
    ui.chain(first, int(ui.out.size()) - 1);
    text(a, a.text->elide(a.controls_note.empty() ? "Controls file: " + utf8_of(controls_file(a.pref)) : a.controls_note, kSmall, w - 460), x + 444,
         yy + (40 - a.text->line_height(kSmall)) * 0.5f, kSmall, col::faint);
    yy += 40 + 10;
    return yy - y;
}

void page_pads(App& a, Ui& ui, float x, float y, float w) {
    const float lw = 330, rx = x + lw + 24, rw = w - lw - 24;
    float ly = y;
    if (a.pads.empty()) {
        D::round_rect(SDL_FRect{x, ly, lw, 92}, 12, col::panel);
        text(a, "No controller connected", x + 20, ly + 16, kBody, col::text, true);
        paragraph(a, "Connect one (USB or Bluetooth): it shows here at once.", x + 20, ly + 42, lw - 40, kSmall, col::dim);
        ly += 92 + 12;
    }
    if (a.pad_sel >= a.pads.size()) a.pad_sel = 0;
    for (std::size_t k = 0; k < a.pads.size(); ++k) {
        SDL_Gamepad* g = a.pads[k];
        Widget wd;
        wd.r = SDL_FRect{x, ly, lw, 64};
        wd.desc = "Shows this controller's buttons and sticks live.";
        wd.activate = [&a, k]() { a.pad_sel = k; };
        const int i = ui.add(std::move(wd));
        const bool f = ui.focused(i), hv = ui.hovered(i), sel = k == a.pad_sel;
        D::round_rect(SDL_FRect{x, ly, lw, 64}, 10, f ? col::panel3 : hv ? col::panel2 : col::panel);
        if (sel) D::round_rect_outline(SDL_FRect{x, ly, lw, 64}, 10, 1.5f, D::alpha(col::accent, 0.8f));
        const char* nm = SDL_GetGamepadName(g);
        text(a, a.text->elide(nm ? nm : "Controller", kBody, lw - 32, true), x + 16, ly + 10, kBody, col::text, true);
        text(a, pad_type_name(SDL_GetGamepadType(g)) + (pad_rumbles(g) ? "   |   rumble" : "") + (SDL_GamepadHasSensor(g, SDL_SENSOR_GYRO) ? "   |   gyro" : "") +
                    (SDL_GetGamepadType(g) == SDL_GAMEPAD_TYPE_PS5 ? "   |   adaptive triggers" : ""),
             x + 16, ly + 36, kSmall, col::dim);
        ly += 64 + 8;
    }
    ly += 6;
    SDL_Gamepad* sel = a.pads.empty() ? nullptr : a.pads[a.pad_sel];
    button(ui, SDL_FRect{x, ly, lw, 44}, a.pad_test ? "Stop the button test" : "Test buttons", a.pad_test ? Style::kPrimary : Style::kNormal,
           sel != nullptr,
           a.pad_test ? "Testing: every button shows only. To stop: hold B / Circle (the right face button), press Start and Back / Select together, or press Esc."
                      : "Every button then shows only, without moving round the launcher. Hold B / Circle or press Esc to stop.",
           [&a]() { a.pad_test = !a.pad_test; a.east_held_since = -1; });
    ly += 44 + 8;
    const bool rumble = sel && pad_rumbles(sel);
    button(ui, SDL_FRect{x, ly, lw, 44}, "Rumble test", Style::kNormal, rumble,
           rumble ? "Shakes the controller for a moment." : "This controller cannot rumble (or SDL cannot drive it).",
           [&a, sel]() {
               const bool ok = SDL_RumbleGamepad(sel, 0xC000, 0xC000, 700);
               a.rumble_note = ok ? "Rumble sent." : std::string("No rumble: ") + SDL_GetError();
           });
    ly += 44 + 8;
    if (!a.rumble_note.empty()) {
        text(a, a.rumble_note, x + 4, ly, kSmall, col::faint);
        ly += 22;
    }
    // pad1: the DualSense's adaptive triggers (SETTINGS > ADAPTIVE TRIGGERS uses them in a race)
    const bool ds = sel && SDL_GetGamepadType(sel) == SDL_GAMEPAD_TYPE_PS5;
    button(ui, SDL_FRect{x, ly, lw, 44}, "Trigger test", Style::kNormal, ds,
           ds ? "The left trigger resists as the brake does and the right one buzzes as when boosting, for 3 seconds."
              : "Adaptive triggers need a DualSense (PS5) controller.",
           [&a, sel]() {
               namespace pd = b3native::pad;
               const int pct = std::clamp(a.settings.adaptive_triggers_strength, 10, 100);
               const pd::Ds5Block b = pd::ds5_trigger_block(pd::trigger_resist(pd::kBrakeStart, pd::scaled(pd::kBrakeForce, pct)),
                                                            pd::trigger_vibrate(pd::kBoostHz, pd::scaled(pd::kBoostAmpMax, pct), 0));
               const bool ok = SDL_SendGamepadEffect(sel, b.data(), int(b.size()));
               if (ok) { a.trig_test_pad = SDL_GetGamepadID(sel); a.trig_test_until = a.now() + 3.0; }
               a.trig_note = ok ? "Trigger test: pull both triggers (" + std::to_string(pct) + "%, TRIGGER STRENGTH)."
                                : std::string("No trigger effect: ") + SDL_GetError();
           });
    ly += 44 + 8;
    if (!a.trig_note.empty()) {
        text(a, a.trig_note, x + 4, ly, kSmall, col::faint);
        ly += 22;
    }
    // right: the selected pad, live, and what the game sees
    float ry = y;
    if (sel) {
        draw_pad(a, sel, rx, ry, rw, 250);
        if (a.pad_test) {
            const float bw = a.text->width("TESTING", kSmall, true) + 20;
            D::round_rect(SDL_FRect{rx + rw * 0.5f - bw * 0.5f, ry + 14, bw, 24}, 12, col::accent);
            text_center(a, "TESTING", rx + rw * 0.5f, ry + 14 + (24 - a.text->line_height(kSmall)) * 0.5f, kSmall, col::ink, true);
        }
        ry += 250 + 14;
    } else {
        D::round_rect(SDL_FRect{rx, ry, rw, 120}, 14, col::panel);
        text_center(a, "The controller you connect shows here.", rx + rw * 0.5f, ry + 48, kBody, col::faint);
        ry += 120 + 14;
    }
    std::array<std::uint8_t, 32> pad = b3native::input::neutral_sample();
    for (SDL_Gamepad* g : a.pads) {   // remap1: through player 1's bindings; padlayout: the layout for each pad, in menus
        const auto one = b3native::input::gamepad_sample(pad_state(g), a.controls.player[0], {}, b3native::input::KeyCtx::Menu,
                                                         b3native::input::pad_family(int(SDL_GetGamepadType(g))));
        pad = b3native::input::merge_samples(pad.data(), one.data());
    }
    const auto seen = a.keys.compose(a.pads.empty() ? nullptr : pad.data());
    ry += section(a, "What the game gets (every controller and the keyboard, in menus)", rx, ry);
    ds2_strip(a, seen, rx, ry, rw);
    ry += 30 * 2 + 10;
    if (sel && a.gyro_pad == SDL_GetGamepadID(sel)) {   // pad1: gyro steering, live, as a race would get it
        namespace in = b3native::input;
        const in::GyroControls& gy = a.controls.player[ctl_pl(a)].gyro;
        in::GamepadState st = pad_state(sel);
        in::apply_gyro(st, a.gyro, gy, true, false, a.gyro_recentre_held);
        const float steer = (float(in::gamepad_sample(st, a.controls.player[ctl_pl(a)], {}, in::KeyCtx::Drive,
                                                      in::pad_family(int(SDL_GetGamepadType(sel))))[4]) - 128.0f) / (st.gyro_steer >= 0 ? 127.0f : 128.0f);
        ry += section(a, "Gyro steering (player " + std::to_string(ctl_pl(a) + 1) + ", while driving)", rx, ry);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s %+.1f deg   gyro %+.2f   the game's steering %+.2f%s", gy.axis ? "Turn" : "Tilt",
                      double(a.gyro.angle(b3native::pad::GyroAxis(gy.axis)) * 180.0f / b3native::pad::kPi), double(st.gyro_steer),
                      double(steer), a.gyro.has_data() ? "" : "   (no sensor data yet)");
        text(a, buf, rx + 4, ry, kSmall, col::dim);
        ry += 22;
        D::round_rect(SDL_FRect{rx, ry, rw, 10}, 5, col::panel2);
        D::rect(SDL_FRect{rx + rw * 0.5f - 1, ry - 2, 2, 14}, col::faint);
        D::round_rect(SDL_FRect{rx + rw * 0.5f * (1.0f + std::clamp(steer, -1.0f, 1.0f)) - 6, ry - 3, 12, 16}, 6, col::accent);
        ry += 24;
    }
    // remap1: the bindings editor (it replaced the fixed table of the keyboard's keys)
    float ky = std::max(ly, ry) + 12;
    ky += controls_editor(a, ui, x, ky, w);
    ky += paragraph(a, "The game's controller layout and the DJ's volume are in the game: DRIVER DETAILS > SETTINGS.", x + 4, ky, w - 8,
                    kSmall, col::faint);
    a.content_h = ky;
}

// ---- the SHOWROOM page (photo2; showroom.h / showroom_view.h) ----

// The car list again when the game folder changed; the GPU device on the first visit
void showroom_refresh(App& a) {
    auto& s = a.show;
    const std::string key = utf8_of(a.game_dir) + (a.game.ok ? "|ok" : "|no");
    if (s.listed_for == key) return;
    s.listed_for = key;
    s.cars.clear();
    s.note.clear();
    s.error.clear();
    s.loaded_index = std::size_t(-1);
    if (!a.game.ok) return;
    s.cars = showroom::list_cars(a.game_dir, &s.note);
    if (s.sel >= s.cars.size()) s.sel = 0;
}

// showhd1: the HD packs the game would start with now (b3replay_main's hd_enable: the mods' [textures] packs of the set
// the next start runs, in load order, then the player's own pack unless HD TEXTURES is off)
std::vector<std::string> showroom_hd_folders(const App& a) {
    std::vector<std::string> mods;
    for (const fs::path& t : a.mod_overlay.texture_packs()) mods.push_back(b3mod::path_utf8(t));
    return b3asset::hd_folders(a.settings.hd_pack_dir, !a.settings.hd_textures, mods);
}

// The selected car in the view (loaded when it, its paint or the HD packs changed)
void showroom_load(App& a) {
    auto& s = a.show;
    if (s.cars.empty()) return;
    if (!s.view) s.view = std::make_unique<showroom::View>();
    if (s.view->set_hd(showroom_hd_folders(a), a.hd_gen)) s.loaded_index = std::size_t(-1);
    if (s.loaded_index == s.sel && s.loaded_paint == s.paint) return;
    s.loaded_index = s.sel;
    s.loaded_paint = s.paint;
    std::string why;
    if (!s.view->load(a.game_dir, s.cars[s.sel], s.paint, &why)) s.error = why;
    else {
        s.error.clear();
        s.paint = s.view->paint();
        s.loaded_paint = s.paint;
    }
}

void showroom_step(App& a, int dir) {
    auto& s = a.show;
    if (s.cars.empty()) return;
    s.sel = (s.sel + s.cars.size() + std::size_t(dir < 0 ? s.cars.size() - 1 : 1)) % s.cars.size();
    s.paint = 0;
}

void page_showroom(App& a, Ui& ui, float x, float y, float w) {
    auto& s = a.show;
    showroom_refresh(a);
    const double now = a.now();
    const float dt = s.last_t > 0 ? float(std::clamp(now - s.last_t, 0.0, 0.1)) : 0.0f;
    s.last_t = now;
    if (!a.game.ok) {
        D::round_rect(SDL_FRect{x, y, w, 92}, 12, col::panel);
        text(a, "No game data yet", x + 20, y + 16, kBody, col::text, true);
        paragraph(a, "The showroom draws the cars from your own Burnout 3 disc: choose it on the Game Data page first.", x + 20, y + 42, w - 40,
                  kSmall, col::dim);
        a.content_h = y + 92;
        return;
    }
    if (s.cars.empty()) {
        D::round_rect(SDL_FRect{x, y, w, 92}, 12, col::panel);
        text(a, "No cars found", x + 20, y + 16, kBody, col::text, true);
        paragraph(a, s.note.empty() ? "The game data has no player cars (PVEH/<class>/CARn.BGV)." : s.note, x + 20, y + 42, w - 40, kSmall, col::dim);
        a.content_h = y + 92;
        return;
    }
    showroom_load(a);
    // the right stick turns it; the turntable's own spin
    float rx = 0, ry = 0;
    for (SDL_Gamepad* g : a.pads) {
        const float gx = float(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX)) / 32767.0f, gy = float(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY)) / 32767.0f;
        if (std::fabs(gx) > std::fabs(rx)) rx = gx;
        if (std::fabs(gy) > std::fabs(ry)) ry = gy;
    }
    s.tt.stick(rx, ry, dt);
    if (!s.drag) s.tt.step(dt);
    // the view: as wide as the page, at most 16:9 and what leaves room for the rows below
    const float view_h = std::max(220.0f, std::min(w * 9.0f / 16.0f, a.H - kHeaderH - kFooterH - 150));
    const SDL_FRect vr{x, y, w, view_h};
    s.rect = vr;
    D::round_rect(vr, 12, col::panel);
    std::string why;
    const int pw = std::min(1920, int(std::lround(vr.w * a.scale))), ph = std::min(1080, int(std::lround(vr.h * a.scale)));
    if (s.view && s.view->ready(&why) && s.view->loaded() && s.error.empty() && s.view->draw(s.tt, pw, ph, s.pixels)) {
        if (!s.tex || s.tex_w != pw || s.tex_h != ph) {
            if (s.tex) SDL_DestroyTexture(s.tex);
            s.tex = SDL_CreateTexture(a.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, pw, ph);
            s.tex_w = pw;
            s.tex_h = ph;
            if (s.tex) SDL_SetTextureScaleMode(s.tex, SDL_SCALEMODE_LINEAR);
        }
        if (s.tex) {
            for (std::size_t k = 3; k < s.pixels.size(); k += 4) s.pixels[k] = 255;
            SDL_UpdateTexture(s.tex, nullptr, s.pixels.data(), pw * 4);
            SDL_RenderTexture(a.ren, s.tex, nullptr, &vr);
        }
    } else {
        const std::string msg = !why.empty() ? "No 3D view: " + why : !s.error.empty() ? "Cannot show this car: " + s.error : "Loading...";
        paragraph(a, msg, x + 24, y + view_h * 0.5f - 12, w - 48, kBody, col::dim);
    }
    // the car's name over the view's bottom left, its class and file
    const showroom::CarEntry& c = s.cars[s.sel];
    D::gradient(SDL_FRect{vr.x, vr.y + vr.h - 70, vr.w, 70}, D::alpha(col::bg, 0), D::alpha(col::bg, 0), D::alpha(col::bg, 0.75f), D::alpha(col::bg, 0.75f));
    text(a, upper(c.name), x + 22, y + view_h - 62, 24, col::text, true);
    const std::string sub = c.kind + "   " + std::to_string(s.sel + 1) + " of " + std::to_string(s.cars.size()) + "   PVEH/" + c.folder + "/CAR" +
                            std::to_string(c.number) + ".BGV";
    text(a, sub, x + 24, y + view_h - 28, kSmall, col::dim);
    float ly = y + view_h + 12;
    // the rows: the car (left / right) on its own row; below it its paint and the spin (up / down moves between the rows
    // on a pad or the keys: left / right change the focused one)
    const float bw = (w - 10) / 2;
    Widget car;
    car.r = SDL_FRect{x, ly, w, 44};
    car.desc = "Left / right (or LT / RT, PgUp / PgDn): the previous / next car; down: its paint, then the turning. Drag the picture (or the right stick) to turn it, the wheel to come nearer.";
    car.change = [&a](int d) { showroom_step(a, d); };
    car.activate = [&a]() { showroom_step(a, 1); };
    car.click = [&a, r = car.r](float cx, float) { showroom_step(a, cx < r.x + r.w * 0.5f ? -1 : 1); };
    const int ci = ui.add(std::move(car));
    {
        const SDL_FRect r = ui.out[std::size_t(ci)].r;
        const bool f = ui.focused(ci), h = ui.hovered(ci);
        D::round_rect(r, 8, f || h ? col::panel3 : col::panel2);
        if (a.look.on) D::round_rect_outline(r, 8, 1.0f, col::edge);
        if (f) focus_ring(r, 8);
        text(a, "<", r.x + 16, r.y + (r.h - a.text->line_height(kBody)) * 0.5f, kBody, col::accent, true);
        text_right(a, ">", r.x + r.w - 16, r.y + (r.h - a.text->line_height(kBody)) * 0.5f, kBody, col::accent, true);
        text_center(a, a.text->elide(c.name, kBody, r.w - 70, true), r.x + r.w * 0.5f, r.y + (r.h - a.text->line_height(kBody)) * 0.5f, kBody, col::text, true);
    }
    ly += 44 + 10;
    const int np = s.view && s.view->loaded() ? s.view->paints() : 1;
    Widget paint;
    paint.r = SDL_FRect{x, ly, bw, 44};
    paint.desc = "Left / right: the car's paint jobs (the colours the game offers for it); up: the car; down: the turning.";
    paint.change = [&a, np](int d) { a.show.paint = (a.show.paint + np + d) % np; };
    paint.activate = [&a, np]() { a.show.paint = (a.show.paint + 1) % np; };
    paint.click = [&a, np, r = paint.r](float cx, float) { a.show.paint = (a.show.paint + np + (cx < r.x + r.w * 0.5f ? -1 : 1)) % np; };
    const int pi = ui.add(std::move(paint));
    {
        const SDL_FRect r = ui.out[std::size_t(pi)].r;
        const bool f = ui.focused(pi), h = ui.hovered(pi);
        D::round_rect(r, 8, f || h ? col::panel3 : col::panel2);
        if (a.look.on) D::round_rect_outline(r, 8, 1.0f, col::edge);
        if (f) focus_ring(r, 8);
        text(a, "<", r.x + 16, r.y + (r.h - a.text->line_height(kBody)) * 0.5f, kBody, col::accent, true);
        text_right(a, ">", r.x + r.w - 16, r.y + (r.h - a.text->line_height(kBody)) * 0.5f, kBody, col::accent, true);
        text_center(a, "Paint " + std::to_string(s.paint + 1) + " of " + std::to_string(np), r.x + r.w * 0.5f, r.y + (r.h - a.text->line_height(kBody)) * 0.5f,
                    kBody, col::text, f);
    }
    button(ui, SDL_FRect{x + bw + 10, ly, bw, 44}, s.tt.spin ? "Turning: on" : "Turning: off", Style::kNormal, true,
           "The turntable turning by itself, on or off. Up: the paint.", [&a]() { a.show.tt.spin = !a.show.tt.spin; });
    ui.chain(ci, int(ui.out.size()) - 1);   // launcherux1: up / down car -> paint -> turning (left / right change the first two)
    ly += 44 + 10;
    ly += paragraph(a, "Every player car on your disc, in the game's own car model. Looking here changes nothing: it unlocks nothing and the save files are not read." +
                           (s.note.empty() ? std::string() : "  (" + s.note + ")"),
                    x + 4, ly, w - 8, kSmall, col::faint);
    a.content_h = ly;
}

// ---- the frame ----

// ---- the frame in the game's look: the front end's backdrop, bands, logo, menu and prompt bar ----

void game_backdrop(App& a) {
    D::gradient(SDL_FRect{0, 0, a.W, a.H}, D::rgba(0x0A2048), D::rgba(0x061431), D::rgba(0x01050E), D::rgba(0x020814));
    if (SDL_Texture* grid = look_tex(a, "grid")) {   // the menus' grid (GLOBAL.TXD "grid": a cell's top and left lines), tiled
        const float cell = 32;
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
        const SDL_FColor tint = D::rgba(0xFFFFFF);
        for (float y = 0; y < a.H; y += cell)
            for (float x = 0; x < a.W; x += cell) {
                const int at = int(v.size());
                v.push_back({{x, y}, tint, {0, 0}});
                v.push_back({{x + cell, y}, tint, {1, 0}});
                v.push_back({{x + cell, y + cell}, tint, {1, 1}});
                v.push_back({{x, y + cell}, tint, {0, 1}});
                for (int q : {0, 1, 2, 0, 2, 3}) idx.push_back(at + q);
            }
        SDL_RenderGeometry(a.ren, grid, v.data(), int(v.size()), idx.data(), int(idx.size()));
    }
    // a soft light from the upper right, as behind the menus
    D::gradient(SDL_FRect{a.W * 0.4f, 0, a.W * 0.6f, a.H * 0.7f}, D::rgba(0x2C6CC8, 0.0f), D::rgba(0x2C6CC8, 0.18f),
                D::rgba(0x2C6CC8, 0.0f), D::rgba(0x2C6CC8, 0.02f));
    if (SDL_Texture* halo = look_tex(a, "Halo")) {   // the menus' blue arc (FRONTEND.TXD "Halo")
        const float s = 1085;   // its circle through the left edge at y 300 and the top at x 380, as on the menus
        blit(a, halo, SDL_FRect{800 - s, 900 - s, s, s}, D::rgba(0xFFFFFF));
    }
}

// A band across the window: translucent dark with the menus' thin blue lines
void game_band(App& a, float y, float h, float alpha, bool line_top, bool line_bottom) {
    D::rect(SDL_FRect{0, y, a.W, h}, D::rgba(0x01040C, alpha));
    if (line_top) D::rect(SDL_FRect{0, y, a.W, 2}, D::alpha(col::accent, 0.75f));
    if (line_bottom) D::rect(SDL_FRect{0, y + h - 2, a.W, 2}, D::alpha(col::accent, 0.75f));
}

void game_header(App& a) {
    game_band(a, 16, kHeaderH - 26, 0.62f, true, true);
    // 2026-11-09: the B3 icon here (the Play page has the game's full title logo), else the game's B3Logo
    if (a.icon) {
        blit(a, a.icon, SDL_FRect{22, 10, 64, 64}, D::rgba(0xFFFFFF));
    } else if (SDL_Texture* logo = look_tex(a, "B3Logo")) {   // GLOBAL.TXD "B3Logo" (256 x 64), as on the main menu
        blit(a, logo, SDL_FRect{10, 10, 236, 59}, D::rgba(0xFFFFFF));
    }
    // the page title as the menus' headings ("SELECT OPTION"): HeadFont's italic capitals, right-aligned
    const std::string t = utf8_upper(a.text->translated(kPageNames[a.page]));
    const float size = 30;
    const float tw = a.text->head(t, 0, 0, size, col::text, true);
    a.text->head(t, a.W - 44 - tw, 16 + (kHeaderH - 26 - a.text->line_height(size)) * 0.5f, size, D::rgba(0xD8E6F6), false);
}

// The sidebar's rows fit above its footer (the author, the version, the settings folder): the row step shrinks to fit
// (50 down to 34 px), and when even that is too tall the footer keeps only the author and the version, then the author
float side_step(float footer_top) { return std::min(50.0f, std::max(34.0f, (footer_top - 10 - 112) / float(kPageCount))); }
void side_footer(App& a, float by, float rows_end) {
    const std::string ver = a.text->elide(std::string("Version ") + b3port::version(), kSmall - 1, kSideW - 40);
    if (rows_end <= by - 50) {
        text(a, "Native PC port by u0rhs", 22, by - 44, kSmall - 1, col::dim);   // the author (2026-11-10)
        text(a, ver, 22, by - 22, kSmall - 1, col::faint);
        text(a, "Settings, saves and logs in", 22, by, kSmall - 1, col::faint);
        text(a, a.text->elide(utf8_of(a.pref), kSmall - 1, kSideW - 40), 22, by + 20, kSmall - 1, col::faint);
    } else if (rows_end <= by - 6) {
        text(a, "Native PC port by u0rhs", 22, by, kSmall - 1, col::dim);
        text(a, ver, 22, by + 20, kSmall - 1, col::faint);
    } else {
        text(a, "Native PC port by u0rhs", 22, by + 20, kSmall - 1, col::dim);
    }
}

void game_sidebar(App& a) {
    D::gradient(SDL_FRect{0, kHeaderH - 10, kSideW + 40, a.H}, D::rgba(0x01040C, 0.45f), D::rgba(0x01040C, 0.0f), D::rgba(0x01040C, 0.45f),
                D::rgba(0x01040C, 0.0f));
    a.side_widgets.clear();
    const float by = a.H - kFooterH - 52;
    const float step = side_step(by - 44);
    float y = 112;
    for (int p = 0; p < kPageCount; ++p) {
        const SDL_FRect r{14, y, kSideW - 28, step - 4};
        Widget w;
        w.r = r;
        a.side_widgets.push_back(w);
        const bool sel = p == a.page;
        const bool hv = a.mouse_on && !a.modal && a.mouse_x >= r.x && a.mouse_x < r.x + r.w && a.mouse_y >= r.y && a.mouse_y < r.y + r.h;
        const std::string up = utf8_upper(a.text->translated(kPageNames[p]));
        const float size = sel ? 24.0f : 20.0f;
        const float ty = r.y + (r.h - a.text->line_height(size)) * 0.5f;
        const SDL_FColor c = sel ? (a.in_content ? D::rgba(0xDCE8F8) : col::text) : hv ? col::accent_hi : col::dim;
        const float tw = text(a, up, r.x + 26, ty, size, c, true);
        if (sel) brackets(r.x + 26, r.x + 26 + tw, r.y + (r.h - 30) * 0.5f, 30, a.in_content ? D::alpha(col::text, 0.55f) : col::text);
        bool warn = false;
        if (p == kGame) warn = !a.game.ok || !a.game.warning.empty();
        if (p == kPlay) warn = a.have_run && a.run.running;
        if (p == kHd) warn = !a.settings.hd_pack_dir.empty() && a.hd.images == 0;
        if (p == kMusic) warn = !a.settings.music_folder.empty() && a.music.songs == 0;
        if (warn) D::disc(r.x + r.w - 10, r.y + r.h * 0.5f, 4.5f, p == kPlay ? col::warn : col::bad);
        y += step;
    }
    side_footer(a, by, y);
}

// The prompt bar's glyphs: src/b3replay/prompts.h's buttons for the device in use (the keyboard: the launcher's keys)
b3replay::PromptDevice prompt_device(const App& a) {
    if (a.pad_used_last && !a.pads.empty()) {
        SDL_Gamepad* g = a.pads[a.pad_sel < a.pads.size() ? a.pad_sel : 0];
        return b3replay::prompt_device_for_pad(int(SDL_GetGamepadType(g)), int(SDL_GetGamepadButtonLabel(g, SDL_GAMEPAD_BUTTON_SOUTH)));
    }
    return b3replay::PromptDevice::kKeyboard;
}

b3replay::PromptKeys launcher_keys() {
    using b3replay::PadButton;
    b3replay::PromptKeys k;
    k.scancode[std::size_t(PadButton::kCross)] = SDL_SCANCODE_RETURN;
    k.scancode[std::size_t(PadButton::kCircle)] = SDL_SCANCODE_ESCAPE;
    k.scancode[std::size_t(PadButton::kL1)] = SDL_SCANCODE_TAB;
    k.scancode[std::size_t(PadButton::kR1)] = SDL_SCANCODE_TAB;
    k.scancode[std::size_t(PadButton::kL2)] = SDL_SCANCODE_PAGEUP;
    k.scancode[std::size_t(PadButton::kR2)] = SDL_SCANCODE_PAGEDOWN;
    return k;
}

// One button glyph `h` units high at (x, y); returns its width
float prompt_glyph(App& a, b3replay::PromptDevice d, b3replay::PadButton b, float x, float y, float h, float alpha = 1.0f) {
    const int px = std::max(8, int(std::lround(h * a.scale)));
    const auto key = std::make_tuple(int(d), int(b), px);
    auto it = a.look.prompts.find(key);
    if (it == a.look.prompts.end()) {
        b3replay::glyph::Canvas c(px * 5, px);
        const float gw = std::min(float(c.w), b3replay::paint_prompt_glyph(c, 0, 0, float(px), d, b, launcher_keys()));
        std::vector<std::uint8_t> rgba(std::size_t(c.w) * std::size_t(c.h) * 4);
        c.store(rgba.data(), c.w, 0, 0);
        ArtImage im;
        im.w = std::max(1, int(std::ceil(gw)));
        im.h = c.h;
        im.rgba.resize(std::size_t(im.w) * std::size_t(im.h) * 4);
        for (int row = 0; row < im.h; ++row)
            std::memcpy(&im.rgba[std::size_t(row) * std::size_t(im.w) * 4], &rgba[std::size_t(row) * std::size_t(c.w) * 4], std::size_t(im.w) * 4);
        it = a.look.prompts.emplace(key, std::make_pair(make_texture(a.ren, im), gw)).first;
    }
    const float w = it->second.second / a.scale;
    if (SDL_Texture* t = it->second.first) {
        blit(a, t, SDL_FRect{x, y, float(int(std::ceil(it->second.second))) / a.scale, float(px) / a.scale}, D::rgba(0xFFFFFF, alpha));
    }
    return w;
}

void game_prompts(App& a, float y, float h) {
    using b3replay::PadButton;
    const b3replay::PromptDevice d = prompt_device(a);
    const bool keyboard = d == b3replay::PromptDevice::kKeyboard;
    const float gh = 22, ty = y + (h - a.text->line_height(kBody)) * 0.5f, gy = y + (h - gh) * 0.5f;
    if (a.pad_test && a.page == kPads) {   // how to stop: B / Circle held (with its progress), Start + Back, or Esc
        const b3replay::PromptDevice pd = keyboard ? b3replay::PromptDevice::kGeneric : d;
        const float w = a.text->width("To stop: hold", kBody, true) + a.text->width("or", kBody, true) + a.text->width("or Esc", kBody, true);
        float x = a.W - 44 - w - 3 * gh - 120;
        x += text(a, "To stop: hold", x, ty, kBody, col::dim, true) + 10;
        const float bx = x;
        x += prompt_glyph(a, pd, PadButton::kCircle, x, gy, gh) + 10;
        if (a.east_held_since >= 0) {   // the hold's progress under the glyph
            const float f = float(std::clamp((a.now() - a.east_held_since) / kPadTestHold, 0.0, 1.0));
            D::round_rect(SDL_FRect{bx, gy + gh + 3, (x - 10 - bx) * f, 3}, 1.5f, col::accent);
        }
        x += text(a, "or", x, ty, kBody, col::dim, true) + 10;
        x += prompt_glyph(a, pd, PadButton::kStart, x, gy, gh) + 4;
        x += text(a, "+", x, ty, kBody, col::dim, true) + 4;
        x += prompt_glyph(a, pd, PadButton::kSelect, x, gy, gh) + 10;
        text(a, "or Esc", x, ty, kBody, col::dim, true);
        return;
    }
    struct Item { std::vector<PadButton> buttons; const char* label; };
    std::vector<Item> items = {{{PadButton::kCross}, "Select"}, {{PadButton::kCircle}, "Back"}};
    items.push_back({keyboard ? std::vector<PadButton>{PadButton::kR1} : std::vector<PadButton>{PadButton::kL1, PadButton::kR1}, "Pages"});
    if (a.page == kSettings) items.push_back({{PadButton::kL2, PadButton::kR2}, "Tabs"});
    // right-aligned like the menus' "Select (X)"
    std::vector<float> widths;
    float total = 0;
    for (const Item& it : items) {
        float w = 0;
        for (PadButton b : it.buttons) {
            b3replay::glyph::Canvas probe(1, 1);
            w += b3replay::paint_prompt_glyph(probe, 4, 4, gh * a.scale, d, b, launcher_keys()) / a.scale + 4;
        }
        w += 6 + a.text->width(it.label, kBody + 1, true);
        widths.push_back(w);
        total += w + 34;
    }
    float x = a.W - 40 - total + 34;
    for (std::size_t k = 0; k < items.size(); ++k) {
        float xx = x;
        for (PadButton b : items[k].buttons) xx += prompt_glyph(a, d, b, xx, gy, gh) + 4;
        text(a, items[k].label, xx + 6, ty - 1, kBody + 1, col::dim, true);
        x += widths[k] + 34;
    }
}

void draw_sidebar(App& a) {
    if (a.look.on) {
        game_sidebar(a);
        return;
    }
    D::rect(SDL_FRect{0, 0, kSideW, a.H}, col::side);
    D::rect(SDL_FRect{kSideW - 1, 0, 1, a.H}, col::line);
    // the mark
    D::round_rect(SDL_FRect{24, 30, 6, 44}, 3, col::accent);
    text(a, "BURNOUT 3", 42, 26, 24, col::text, true);
    text(a, "PC LAUNCHER", 43, 56, kSection, col::accent, true);
    a.side_widgets.clear();
    const float by = a.H - 70;
    const float step = side_step(by - 44);
    float y = 112;
    for (int p = 0; p < kPageCount; ++p) {
        const SDL_FRect r{14, y, kSideW - 28, step - 4};
        Widget w;
        w.r = r;
        a.side_widgets.push_back(w);
        const bool sel = p == a.page;
        const bool hv = a.mouse_on && !a.modal && a.mouse_x >= r.x && a.mouse_x < r.x + r.w && a.mouse_y >= r.y && a.mouse_y < r.y + r.h;
        if (sel) D::round_rect(r, 9, a.in_content ? col::panel : col::panel3);
        else if (hv) D::round_rect(r, 9, col::panel);
        if (sel) D::round_rect(SDL_FRect{r.x + 6, r.y + (r.h - 22) * 0.5f, 4, 22}, 2, col::accent);
        if (sel && !a.in_content && !a.modal) focus_ring(r, 9);
        text(a, kPageNames[p], r.x + 24, r.y + (r.h - a.text->line_height(17)) * 0.5f, 17, sel ? col::text : col::dim, sel);
        // a warning mark where something needs the user
        bool warn = false;
        if (p == kGame) warn = !a.game.ok || !a.game.warning.empty();
        if (p == kPlay) warn = a.have_run && a.run.running;
        if (p == kHd) warn = !a.settings.hd_pack_dir.empty() && a.hd.images == 0;
        if (p == kMusic) warn = !a.settings.music_folder.empty() && a.music.songs == 0;
        if (warn) D::disc(r.x + r.w - 20, r.y + r.h * 0.5f, 4.5f, p == kPlay ? col::warn : col::bad);
        y += step;
    }
    // the bottom: the author, the version, where the files are
    side_footer(a, by, y);
}

std::vector<std::pair<std::string, std::string>> hint_text(App& a) {
    if (a.pad_test && a.page == kPads) return {{"To stop the test:", ""}, {"hold B / Circle", "or"}, {"Start + Back", "or"}, {"Esc", ""}};
    std::vector<std::pair<std::string, std::string>> h;
    if (a.pad_used_last) {
        std::string ok = "A", back = "B";
        if (!a.pads.empty()) {
            const auto l = SDL_GetGamepadButtonLabel(a.pads[a.pad_sel < a.pads.size() ? a.pad_sel : 0], SDL_GAMEPAD_BUTTON_SOUTH);
            if (l == SDL_GAMEPAD_BUTTON_LABEL_CROSS) { ok = "Cross"; back = "Circle"; }
            else if (l == SDL_GAMEPAD_BUTTON_LABEL_B) { ok = "B"; back = "A"; }
        }
        h = {{"D-pad / stick", "move"}, {ok, "select"}, {back, "back"}, {"LB / RB", "pages"}};
        if (a.page == kSettings) h.push_back({"LT / RT", "tabs"});
        if (a.page == kShowroom) h.push_back({"LT / RT", "cars"}), h.push_back({"R stick", "turn"});
        return h;
    }
    h = {{"Arrows", "move"}, {"Enter", "select"}, {"Esc", "back"}, {"Tab", "pages"}};
    if (a.page == kSettings) h.push_back({"PgUp / PgDn", "tabs"});
    if (a.page == kShowroom) h.push_back({"PgUp / PgDn", "cars"}), h.push_back({"Drag", "turn"});
    h.push_back({"Mouse", a.page == kPads ? "click, right-click, wheel" : "click, wheel"});
    return h;
}

bool in_rect(const SDL_FRect& r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
bool hovered(const App& a, const SDL_FRect& r) { return a.mouse_on && in_rect(r, a.mouse_x, a.mouse_y); }

// osk1: the prompts of a panel (the on-screen keyboard, the folder list): each item a pad button or two and its words,
// in lines that wrap to `w`; returns the height used
struct PadHint {
    std::vector<b3replay::PadButton> buttons;
    std::string label;
};
float pad_hints(App& a, const std::vector<PadHint>& items, float x, float y, float w, bool centre) {
    const b3replay::PromptDevice d = prompt_device(a);
    const float gh = 22, lh = 30, gap = 26;
    std::vector<float> widths;
    for (const PadHint& it : items) {
        float iw = 0;
        for (b3replay::PadButton b : it.buttons) {
            b3replay::glyph::Canvas probe(1, 1);
            iw += b3replay::paint_prompt_glyph(probe, 4, 4, gh * a.scale, d, b, launcher_keys()) / a.scale + 4;
        }
        widths.push_back(iw + 4 + a.text->width(it.label, kBody, true));
    }
    float yy = y;
    for (std::size_t k = 0; k < items.size();) {
        std::size_t end = k;
        float lw = 0;
        while (end < items.size() && (end == k || lw + gap + widths[end] <= w)) lw += (end == k ? 0 : gap) + widths[end], ++end;
        float xx = centre ? x + (w - lw) * 0.5f : x;
        for (; k < end; ++k) {
            for (b3replay::PadButton b : items[k].buttons) xx += prompt_glyph(a, d, b, xx, yy + (lh - gh) * 0.5f, gh) + 4;
            xx += 4;
            xx += text(a, items[k].label, xx, yy + (lh - a.text->line_height(kBody)) * 0.5f, kBody, col::dim, true) + gap;
        }
        yy += lh;
    }
    return yy - y;
}

// osk1: a small keyboard mark (the field's button that opens / closes the on-screen keyboard)
void keyboard_mark(const SDL_FRect& r, SDL_FColor c) {
    D::round_rect_outline(r, 4, 1.5f, c);
    const float k = std::max(2.0f, (r.w - 12) / 9.0f), top = r.y + (r.h - (3 * k + 2 * 2)) * 0.5f;
    for (int row = 0; row < 2; ++row)
        for (int col_ = 0; col_ < 5; ++col_) D::rect(SDL_FRect{r.x + 6 + float(col_) * (k + k * 0.8f), top + float(row) * (k + 2), k, k}, c);
    D::rect(SDL_FRect{r.x + r.w * 0.28f, top + 2 * (k + 2), r.w * 0.44f, k}, c);
}

// osk1: a text field with its cursor; a path longer than the field scrolls so the cursor stays in view
void draw_field(App& a, Modal& m, const SDL_FRect& f) {
    m.field_rect = f;
    D::round_rect(f, 8, col::bg);
    D::round_rect_outline(f, 8, 1.5f, col::accent);
    m.osk_toggle = SDL_FRect{f.x + f.w - 52, f.y + (f.h - 28) * 0.5f, 42, 28};
    keyboard_mark(m.osk_toggle, m.osk || hovered(a, m.osk_toggle) ? col::accent_hi : col::faint);
    const std::string& s = m.field.text;
    const std::size_t cur = std::min(m.field.cursor, s.size());
    const float tx = f.x + 12, avail = f.w - 12 - 64;
    const std::string_view sv(s);
    if (m.view > cur) m.view = cur;
    while (m.view < cur && a.text->width(sv.substr(m.view, cur - m.view), kBody) > avail - 4) m.view = utf8_next(s, m.view);
    while (m.view > 0 && a.text->width(sv.substr(utf8_prev(s, m.view)), kBody) <= avail) m.view = utf8_prev(s, m.view);
    const SDL_Rect clip{int(tx - 2), int(f.y + 2), int(avail + 4), int(f.h - 4)};
    SDL_SetRenderClipRect(a.ren, &clip);
    const float ty = f.y + (f.h - a.text->line_height(kBody)) * 0.5f;
    text(a, sv.substr(m.view), tx, ty, kBody, col::text);
    const float cx = tx + a.text->width(sv.substr(m.view, cur - m.view), kBody);
    if (m.osk || int(a.now() * 2) % 2 == 0) D::rect(SDL_FRect{cx + 1, f.y + 11, 2, f.h - 22}, col::accent);
    SDL_SetRenderClipRect(a.ren, nullptr);
    if (m.view > 0) D::gradient(SDL_FRect{tx - 2, f.y + 2, 16, f.h - 4}, col::bg, D::alpha(col::bg, 0), col::bg, D::alpha(col::bg, 0));
}

// osk1: the cursor put where the field was clicked
void field_click(App& a, float x) {
    Modal& m = *a.modal;
    const std::string& s = m.field.text;
    const float tx = m.field_rect.x + 12;
    std::size_t best = m.view;
    float best_d = 1e30f;
    for (std::size_t p = std::min(m.view, s.size());; p = utf8_next(s, p)) {
        const float d = std::fabs(tx + a.text->width(std::string_view(s).substr(m.view, p - m.view), kBody) - x);
        if (d < best_d) { best_d = d; best = p; }
        if (p >= s.size()) break;
    }
    m.field.cursor = best;
}

// osk1: the text field with the on-screen keyboard under it, in one panel
void draw_osk_modal(App& a) {
    Modal& m = *a.modal;
    D::rect(SDL_FRect{0, 0, a.W, a.H}, D::alpha(col::bg, 0.72f));
    const float w = std::min(1000.0f, a.W - 40), inner = w - 48, unit = inner / kOskRowUnits, gap = 6;
    const bool pad = prompt_device(a) != b3replay::PromptDevice::kKeyboard;
    const float hint_h = pad ? (w < 900 ? 60.0f : 30.0f) : a.text->line_height(kBody);
    const float head = 22 + a.text->line_height(22) + 10 + 48 + 16;
    const float kh = std::clamp((a.H - 32 - head - 16 - hint_h - 18 - float(kOskRows) * gap) / float(kOskRows), 30.0f, 56.0f);
    const float h = head + float(kOskRows) * (kh + gap) + 10 + hint_h + 18;
    const float x = (a.W - w) * 0.5f, y = std::max(16.0f, (a.H - h) * 0.5f);
    D::round_rect(SDL_FRect{x, y, w, h}, 16, D::alpha(col::bg, 1.0f));   // (opaque: the page does not show through the keys)
    D::round_rect(SDL_FRect{x, y, w, h}, 16, col::panel2);
    D::round_rect_outline(SDL_FRect{x, y, w, h}, 16, 1.0f, col::line);
    float yy = y + 22;
    text(a, a.text->elide(m.title, 22, inner, true), x + 24, yy, 22, col::text, true);
    yy += a.text->line_height(22) + 10;
    draw_field(a, m, SDL_FRect{x + 24, yy, inner, 48});
    yy += 48 + 16;
    m.key_rects.assign(m.keys.size(), SDL_FRect{});
    for (std::size_t k = 0; k < m.keys.size(); ++k) {
        const OskKey& key = m.keys[k];
        const SDL_FRect r{x + 24 + key.x * unit + gap * 0.5f, yy + float(key.row) * (kh + gap), key.w * unit - gap, kh};
        m.key_rects[k] = r;
        const bool focus = int(k) == m.osk_st.focus, hv = hovered(a, r);
        const bool on = (key.kind == OskKind::kShift && m.osk_st.layer == OskLayer::kUpper) ||
                        (key.kind == OskKind::kSymbols && m.osk_st.layer == OskLayer::kSymbols);
        SDL_FColor fill = key.kind == OskKind::kChar || key.kind == OskKind::kSpace ? col::panel3 : col::panel;
        if (focus) fill = key.kind == OskKind::kCancel ? col::bad : col::accent;
        else if (hv) fill = D::mix(fill, col::accent, 0.35f);
        D::round_rect(r, 7, fill);
        if (key.kind == OskKind::kDone && !focus) D::round_rect_outline(r, 7, 2.0f, col::accent);
        if (on) D::rect(SDL_FRect{r.x + r.w * 0.3f, r.y + r.h - 7, r.w * 0.4f, 3}, focus ? col::ink : m.osk_st.caps ? col::warn : col::ok);
        std::string label = osk_label(key, m.osk_st.layer);
        if (key.kind == OskKind::kShift && m.osk_st.caps) label = "Caps";
        float size = key.kind == OskKind::kChar ? kBody + 5 : kBody;
        if (a.text->width(label, size, true) > r.w - 10) size = kSmall;
        label = a.text->elide(label, size, r.w - 8, true);
        text_center(a, label, r.x + r.w * 0.5f, r.y + (kh - a.text->line_height(size)) * 0.5f, size, focus ? col::ink : col::text, true);
    }
    yy += float(kOskRows) * (kh + gap) + 10;
    using b3replay::PadButton;
    if (pad) {
        pad_hints(a, {{{PadButton::kCross}, "Type"}, {{PadButton::kCircle}, "Delete"}, {{PadButton::kSquare}, "Space"},
                      {{PadButton::kTriangle}, "Shift / symbols"}, {{PadButton::kL1, PadButton::kR1}, "Cursor"}, {{PadButton::kStart}, "Done"},
                      {{PadButton::kSelect}, "Cancel"}},
                  x + 24, yy, inner, true);
    } else {
        text_center(a, a.text->elide("Click the keys, or type.   Enter: OK   Esc: cancel   Ctrl+V: paste", kBody, inner), x + w * 0.5f, yy, kBody, col::dim);
    }
    m.rects.clear();   // (its OK / Cancel: the Done and Cancel keys)
}

void draw_modal(App& a) {
    Modal& m = *a.modal;
    if (m.input && m.osk) {
        draw_osk_modal(a);
        return;
    }
    D::rect(SDL_FRect{0, 0, a.W, a.H}, D::alpha(col::bg, 0.72f));
    const float w = std::min(620.0f, a.W - 64);
    const float tx = 0, inner = w - 56;
    (void)tx;
    const auto lines = a.text->wrap(m.text, kBody, inner);
    float h = 30 + a.text->line_height(22) + 12 + float(lines.size()) * a.text->line_height(kBody) + 28 + 48 + 28;
    if (m.input) h += 56;
    const float x = (a.W - w) * 0.5f, y = std::max(20.0f, (a.H - h) * 0.5f);
    D::round_rect(SDL_FRect{x, y, w, h}, 16, col::panel2);
    D::round_rect_outline(SDL_FRect{x, y, w, h}, 16, 1.0f, col::line);
    float yy = y + 30;
    text(a, m.title, x + 28, yy, 22, col::text, true);
    yy += a.text->line_height(22) + 12;
    for (const std::string& l : lines) {
        text(a, l, x + 28, yy, kBody, col::dim);
        yy += a.text->line_height(kBody);
    }
    yy += 20;
    if (m.input) {
        draw_field(a, m, SDL_FRect{x + 28, yy, inner, 44});
        yy += 56;
    }
    yy += 8;
    m.rects.clear();
    float bx = x + w - 28;
    for (std::size_t k = m.buttons.size(); k-- > 0;) {
        const auto& b = m.buttons[k];
        const float bw = std::max(120.0f, a.text->width(b.label, kBody, true) + 40);
        bx -= bw;
        const SDL_FRect r{bx, yy, bw, 46};
        bx -= 12;
        m.rects.insert(m.rects.begin(), r);
        const bool f = k == m.focus;
        const bool hv = a.mouse_on && a.mouse_x >= r.x && a.mouse_x < r.x + r.w && a.mouse_y >= r.y && a.mouse_y < r.y + r.h;
        SDL_FColor fill = f ? (b.danger ? col::bad : col::accent) : hv ? col::panel3 : col::panel;
        D::round_rect(r, 8, fill);
        text_center(a, b.label, r.x + r.w * 0.5f, r.y + (46 - a.text->line_height(kBody)) * 0.5f, kBody, f ? col::ink : col::text, true);
    }
}

// osk1: "...the/end/of/a/long/path" to fit `w`
std::string elide_left(App& a, const std::string& s, float size, float w, bool bold = false) {
    if (a.text->width(s, size, bold) <= w) return s;
    std::size_t at = 0;
    while (at < s.size() && a.text->width("..." + s.substr(at), size, bold) > w) at = utf8_next(s, at);
    return "..." + s.substr(at);
}

void folder_icon(float x, float y, float s, SDL_FColor c) {
    D::round_rect(SDL_FRect{x, y + s * 0.12f, s * 0.42f, s * 0.2f}, 2, c);
    D::round_rect(SDL_FRect{x, y + s * 0.24f, s, s * 0.66f}, 2, c);
}
void file_icon(float x, float y, float s, SDL_FColor c) {
    D::round_rect_outline(SDL_FRect{x + s * 0.15f, y + s * 0.04f, s * 0.7f, s * 0.9f}, 2, 1.5f, c);
    for (int k = 0; k < 3; ++k) D::rect(SDL_FRect{x + s * 0.3f, y + s * (0.3f + 0.18f * float(k)), s * 0.4f, 1.5f}, c);
}

// A button of the folder list (the mouse; the pad has its own buttons for each)
void browse_button(App& a, const SDL_FRect& r, const std::string& label, bool primary) {
    const bool hv = hovered(a, r);
    D::round_rect(r, 8, primary ? (hv ? col::accent_hi : col::accent) : hv ? col::panel3 : col::panel);
    text_center(a, label, r.x + r.w * 0.5f, r.y + (r.h - a.text->line_height(kBody)) * 0.5f, kBody, primary ? col::ink : col::text, true);
}

// osk1: the launcher's folder list: the folder shown and what is in it, the places on the left, the folder's list,
// the pad's buttons and the mouse's
void draw_browser(App& a) {
    Browser& b = *a.browser;
    D::rect(SDL_FRect{0, 0, a.W, a.H}, D::alpha(col::bg, 0.72f));
    const float w = std::min(1140.0f, a.W - 32), h = a.H - 32, x = (a.W - w) * 0.5f, y = 16;
    D::round_rect(SDL_FRect{x, y, w, h}, 16, D::alpha(col::bg, 1.0f));   // (opaque: the page does not show through the list)
    D::round_rect(SDL_FRect{x, y, w, h}, 16, col::panel2);
    D::round_rect_outline(SDL_FRect{x, y, w, h}, 16, 1.0f, col::line);
    const float inner = w - 48;
    float yy = y + 20;
    text(a, a.text->elide(b.title, 22, inner, true), x + 24, yy, 22, col::text, true);
    yy += a.text->line_height(22) + 4;
    text(a, elide_left(a, utf8_of(b.dir), kBody + 1, inner, true), x + 24, yy, kBody + 1, col::accent_hi, true);
    yy += a.text->line_height(kBody + 1) + 4;
    {
        const bool note = !b.note.empty();
        const SDL_FColor dot = note ? col::warn : b.status_level == 1 ? col::ok : b.status_level == 2 ? col::bad : col::faint;
        D::disc(x + 30, yy + a.text->line_height(kSmall) * 0.5f, 4.5f, dot);
        text(a, a.text->elide(note ? b.note : b.status, kSmall, inner - 20), x + 42, yy, kSmall, note ? col::text : col::dim);
        yy += a.text->line_height(kSmall) + 12;
    }
    // the bottom: the pad's prompts (or the keys'), and the mouse's buttons
    const bool pad = prompt_device(a) != b3replay::PromptDevice::kKeyboard;
    const float by = y + h - 18 - 44;
    const float hint_h = pad && w < 1000 ? 60.0f : 30.0f, hy = by - 8 - hint_h;
    const float top = yy, bottom = hy - 10;
    const std::string choose_label = b.kind == DialogKind::kExport ? "Save here" : "Choose this folder";
    // the places
    const float px = x + 24, pw = std::min(250.0f, w * 0.26f), row_h = 40;
    text(a, "PLACES", px + 4, top, kSection, col::accent, true);
    b.place_rects.assign(b.places.size(), SDL_FRect{});
    {
        float ry = top + a.text->line_height(kSection) + 6;
        const float ph = 36;
        for (std::size_t k = 0; k < b.places.size(); ++k) {
            const SDL_FRect r{px, ry, pw, ph - 4};
            if (r.y + r.h > bottom) break;
            b.place_rects[k] = r;
            const bool sel = b.column == 0 && int(k) == b.place_sel, here = b.places[k].path == b.dir;
            if (sel) D::round_rect(r, 7, col::accent);
            else if (hovered(a, r)) D::round_rect(r, 7, col::panel3);
            if (here) D::rect(SDL_FRect{r.x + 3, r.y + 7, 3, r.h - 14}, sel ? col::ink : col::accent);
            if (b.places[k].drive) {   // a drive: a box with a light
                const SDL_FColor c = sel ? col::ink : col::dim;
                const float iy = r.y + (r.h - 12) * 0.5f;
                D::round_rect_outline(SDL_FRect{r.x + 12, iy, 17, 12}, 2, 1.5f, c);
                D::disc(r.x + 24.5f, iy + 6, 1.6f, c);
            } else {
                folder_icon(r.x + 12, r.y + (r.h - 16) * 0.5f, 16, sel ? col::ink : col::dim);
            }
            text(a, a.text->elide(b.places[k].label, kBody, r.w - 46, true), r.x + 36, r.y + (r.h - a.text->line_height(kBody)) * 0.5f, kBody,
                 sel ? col::ink : col::text, true);
            ry += ph;
        }
    }
    // the folder's list
    const float lx = px + pw + 18, lw = x + w - 24 - lx;
    b.list_rect = SDL_FRect{lx, top, lw, bottom - top};
    D::round_rect(b.list_rect, 10, col::panel);
    const int n = int(b.list.entries.size());
    b.rows_shown = std::max(1, int((b.list_rect.h - 12) / row_h));
    if (b.follow) {
        if (b.sel < b.first) b.first = b.sel;
        if (b.sel >= b.first + b.rows_shown) b.first = b.sel - b.rows_shown + 1;
    }
    b.first = std::clamp(b.first, 0, std::max(0, n - b.rows_shown));
    b.row_rects.assign(std::size_t(n), SDL_FRect{});
    if (n == 0) {
        const std::string empty = b.list.other_files ? "No folders here (" + std::to_string(b.list.other_files) + " other files)." : "This folder is empty.";
        text(a, empty, lx + 18, top + 16, kBody, col::dim);
    }
    for (int k = b.first; k < n && k < b.first + b.rows_shown; ++k) {
        const BrowseEntry& e = b.list.entries[std::size_t(k)];
        const SDL_FRect r{lx + 6, top + 6 + float(k - b.first) * row_h, lw - 12 - (n > b.rows_shown ? 10 : 0), row_h - 4};
        b.row_rects[std::size_t(k)] = r;
        const bool sel = k == b.sel;
        const bool choosable = e.folder || b.pick_files;
        if (sel && b.column == 1) D::round_rect(r, 7, col::accent);
        else if (sel) D::round_rect(r, 7, col::panel3);
        else if (hovered(a, r)) D::round_rect(r, 7, D::alpha(col::panel3, 0.7f));
        const SDL_FColor c = sel && b.column == 1 ? col::ink : choosable ? col::text : col::faint;
        if (e.folder) folder_icon(r.x + 12, r.y + (r.h - 18) * 0.5f, 18, sel && b.column == 1 ? col::ink : col::accent_hi);
        else file_icon(r.x + 12, r.y + (r.h - 18) * 0.5f, 18, c);
        text(a, a.text->elide(e.name, kBody, r.w - 52, e.folder), r.x + 40, r.y + (r.h - a.text->line_height(kBody)) * 0.5f, kBody, c, e.folder);
    }
    if (n > b.rows_shown) {   // the scroll bar
        const float view = b.list_rect.h - 12, bh = std::max(30.0f, view * float(b.rows_shown) / float(n));
        const float sy = top + 6 + (view - bh) * float(b.first) / float(std::max(1, n - b.rows_shown));
        D::round_rect(SDL_FRect{lx + lw - 12, sy, 4, bh}, 2, col::accent);
    }
    // the prompts
    using b3replay::PadButton;
    if (pad) {
        std::vector<PadHint> hints = {{{PadButton::kCross}, b.pick_files ? "Open / choose" : "Open"}, {{PadButton::kCircle}, "Up a folder"}};
        if (b.pick_folder) hints.push_back({{PadButton::kStart}, choose_label});
        hints.push_back({{PadButton::kTriangle}, "Places"});
        hints.push_back({{PadButton::kSquare}, "Type a path"});
        hints.push_back({{PadButton::kSelect}, "Cancel"});
        pad_hints(a, hints, x + 24, hy, inner, false);
    } else {
        text(a, a.text->elide(std::string("Enter: open   Backspace: up a folder   ") + (b.pick_folder ? "Space: " + choose_label + "   " : "") +
                                  "Tab: places   Esc: cancel   Click a row twice to open it",
                              kBody, inner),
             x + 24, hy + (30 - a.text->line_height(kBody)) * 0.5f, kBody, col::dim);
    }
    // the mouse's buttons
    float bx = x + w - 24;
    auto place = [&](float bw) {
        bx -= bw;
        const SDL_FRect r{bx, by, bw, 44};
        bx -= 12;
        return r;
    };
    b.choose_r = b.pick_folder ? place(std::max(180.0f, a.text->width(choose_label, kBody, true) + 40)) : SDL_FRect{};
    b.cancel_r = place(120);
    b.type_r = place(160);
    b.up_r = place(150);
    if (b.pick_folder) browse_button(a, b.choose_r, choose_label, true);
    browse_button(a, b.cancel_r, "Cancel", false);
    browse_button(a, b.type_r, "Type a path...", false);
    browse_button(a, b.up_r, "Up a folder", false);
}

// The page's top and bottom on the screen (between the header and the footer)
float page_top(const App&) { return kHeaderH + 6; }
float page_bottom(const App& a) { return a.H - kFooterH - 6; }

// The footer's description: the widget under the pointer while the mouse is in use, else the focused one's
std::string footer_text(const App& a) {
    if (a.modal || a.browser) return {};
    std::string desc;
    if (a.mouse_on)
        for (std::size_t k = 0; k < a.widgets.size(); ++k) {
            const SDL_FRect& r = a.widgets[k].r;
            if (a.mouse_x >= r.x && a.mouse_x < r.x + r.w && a.mouse_y >= r.y && a.mouse_y < r.y + r.h && a.mouse_y > page_top(a) &&
                a.mouse_y < page_bottom(a))
                desc = a.widgets[k].desc;
        }
    if (desc.empty() && a.in_content && a.focus < int(a.widgets.size())) desc = a.widgets[std::size_t(a.focus)].desc;
    if (desc.empty() && !a.in_content) desc = std::string(kPageSubtitles[a.page]) + "  Press Enter / A (or click) to go into the page.";
    return desc;
}

void render(App& a) {
    if (a.text) {   // 1.1: the launcher's words in the chosen language when it is imported (English otherwise)
        b3settings::Env env;
        env.languages = a.languages;
        a.text->set_language(b3settings::ui_language(a.settings, env));
    }
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(a.ren, &pw, &ph);
    a.scale = std::clamp(std::min(float(pw) / 1280.0f, float(ph) / 720.0f), 0.5f, 4.0f);
    a.W = float(pw) / a.scale;
    a.H = float(ph) / a.scale;
    SDL_SetRenderScale(a.ren, a.scale, a.scale);
    a.text->set_scale(a.scale);
    D::set_renderer(a.ren, 1.0f / a.scale);
    SDL_SetRenderDrawBlendMode(a.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColorFloat(a.ren, col::bg.r, col::bg.g, col::bg.b, 1);
    SDL_RenderClear(a.ren);

    if (a.look.on) game_backdrop(a);
    draw_sidebar(a);
    // the header
    const float cx = kSideW + kPadX, cw = a.W - kSideW - 2 * kPadX;
    if (a.look.on) game_header(a);
    else {
        text(a, kPageNames[a.page], cx, 22, kTitle, col::text, true);
        text(a, a.text->elide(kPageSubtitles[a.page], kBody, cw), cx, 58, kBody, col::dim);
    }
    // the page, clipped and scrolled
    const float top = kHeaderH + 6, bottom = a.H - kFooterH - 6;
    const SDL_Rect clip{int(kSideW + 1), int(top), int(a.W - kSideW - 1), int(bottom - top)};
    SDL_SetRenderClipRect(a.ren, &clip);
    std::vector<Widget> ws;
    Ui ui{a, ws};
    const float y0 = top + 10 - a.scroll[a.page];
    a.content_h = y0;
    switch (a.page) {
    case kPlay: page_play(a, ui, cx, y0, cw); break;
    case kSettings: page_settings(a, ui, cx, y0, cw); break;
    case kHd: page_hd(a, ui, cx, y0, cw); break;
    case kMusic: page_music(a, ui, cx, y0, cw); break;
    case kMods: page_mods(a, ui, cx, y0, cw); break;
    case kGame: page_game(a, ui, cx, y0, cw); break;
    case kSaves: page_saves(a, ui, cx, y0, cw); break;
    case kStats: page_stats(a, ui, cx, y0, cw); break;
    case kShowroom: page_showroom(a, ui, cx, y0, cw); break;
    case kReplays: page_replays(a, ui, cx, y0, cw); break;
    case kPads: page_pads(a, ui, cx, y0, cw); break;
    case kCredits: page_credits(a, ui, cx, y0, cw); break;
    default: break;
    }
    SDL_SetRenderClipRect(a.ren, nullptr);
    const float full = a.content_h - y0 + 20;   // the page's height
    const float view = bottom - top;
    // keep the focused widget in view; clamp the scroll
    float& sc = a.scroll[a.page];
    if (a.reveal_focus && a.in_content && a.focus >= 0 && a.focus < int(ws.size())) {
        const SDL_FRect& r = ws[std::size_t(a.focus)].r;
        if (r.y < top + 8) sc -= (top + 8 - r.y);
        else if (r.y + r.h > bottom - 8) sc += (r.y + r.h - (bottom - 8));
    }
    sc = std::clamp(sc, 0.0f, std::max(0.0f, full - view));
    if (full > view) {   // the scroll bar
        const float bh = std::max(40.0f, view * view / full), by = top + (view - bh) * (sc / std::max(1.0f, full - view));
        D::round_rect(SDL_FRect{a.W - 10, by, 4, bh}, 2, a.look.on ? col::accent : col::panel3);
        // fades at the cut edges (the plain look's flat background)
        if (a.look.on) {
        } else if (sc > 1) D::gradient(SDL_FRect{kSideW + 1, top, a.W - kSideW - 1, 18}, col::bg, col::bg, D::alpha(col::bg, 0), D::alpha(col::bg, 0));
        if (sc < full - view - 1)
            D::gradient(SDL_FRect{kSideW + 1, bottom - 18, a.W - kSideW - 1, 18}, D::alpha(col::bg, 0), D::alpha(col::bg, 0), col::bg, col::bg);
    }
    a.widgets = std::move(ws);
    if (a.focus >= int(a.widgets.size())) a.focus = std::max(0, int(a.widgets.size()) - 1);

    // the footer: the focused item's description, then the controls
    const float fy = a.H - kFooterH;
    if (a.look.on) {   // the help bar and the prompt bar, across the window as in the menus
        game_band(a, fy, kFooterH - 44, 0.6f, true, false);
        game_band(a, a.H - 44, 44, 0.78f, true, false);
    } else {
        D::rect(SDL_FRect{kSideW, fy, a.W - kSideW, 1}, col::line);
    }
    const std::string desc = footer_text(a);
    float dy = fy + 12;
    const float dx = a.look.on ? 40.0f : cx, dw = a.look.on ? a.W - 80 : cw;
    for (const std::string& l : a.text->wrap(desc, kBody, dw, a.look.on)) {
        if (dy > a.H - 58) break;
        text(a, l, dx, dy, kBody, a.look.on ? col::dim : col::text, a.look.on);
        dy += a.text->line_height(kBody);
    }
    if (a.look.on) game_prompts(a, a.H - 44, 44);
    else D::rect(SDL_FRect{kSideW, a.H - 38, a.W - kSideW, 38}, col::side);
    if (!a.look.on) {
        float hx = cx;
        const float hy = a.H - 38 + (38 - a.text->line_height(kSmall)) * 0.5f;
        for (const auto& [key, what] : hint_text(a)) {
            if (hx > a.W - 80) break;
            hx += text(a, key, hx, hy, kSmall, col::dim, true) + 7;
            if (!what.empty()) hx += text(a, what, hx, hy, kSmall, col::faint) + 22;
        }
    }

    if (a.child || a.skip_at > 0) {   // starting (launchskip1: opaque when the launcher was skipped, with its hint first)
        D::rect(SDL_FRect{0, 0, a.W, a.H}, a.skip_at > 0 || a.skipping ? col::bg : D::alpha(col::bg, 0.8f));
        text_center(a, "Starting Burnout 3...", a.W * 0.5f, a.H * 0.5f - 30, 26, col::text, true);
        if (a.skip_at > 0)
            text_center(a, "Hold Shift, or Back / Select on a controller, to open the launcher instead.", a.W * 0.5f, a.H * 0.5f + 48, kBody,
                        col::dim);
        const float t = float(std::fmod(a.now(), 1.2) / 1.2);
        D::round_rect(SDL_FRect{a.W * 0.5f - 120, a.H * 0.5f + 20, 240, 6}, 3, col::panel3);
        D::round_rect(SDL_FRect{a.W * 0.5f - 120 + 180 * t, a.H * 0.5f + 20, 60, 6}, 3, col::accent);
    }
    if (a.browser) draw_browser(a);   // osk1
    if (a.modal) draw_modal(a);
}

// ---- input ----

// The widget Up / Down / Left / Right moves to from `from` (-1: none). launcherux1, the rule: Up / Down go to the
// nearest widget above / below (the one overlapping in x first), or to the widget's own link (Widget::up / down,
// Ui::chain) where a group reads as one list; Left / Right go to the nearest widget in the same row, and on a value
// widget (Widget::change) they change its value instead (nav). So value widgets side by side are chained, or Up /
// Down could not reach them all.
int nearest(const App& a, int from, Nav n) {
    const auto& ws = a.widgets;
    if (from < 0 || from >= int(ws.size())) return ws.empty() ? -1 : 0;
    const int link = n == Nav::Up ? ws[std::size_t(from)].up : n == Nav::Down ? ws[std::size_t(from)].down : -1;
    if (link >= 0 && link < int(ws.size())) return link;
    const SDL_FRect c = ws[std::size_t(from)].r;
    const float ccx = c.x + c.w * 0.5f, ccy = c.y + c.h * 0.5f;
    int best = -1;
    float best_score = 1e30f;
    for (int i = 0; i < int(ws.size()); ++i) {
        if (i == from) continue;
        const SDL_FRect& r = ws[std::size_t(i)].r;
        const float rx = r.x + r.w * 0.5f, ry = r.y + r.h * 0.5f;
        float primary = 0, secondary = 0;
        if (n == Nav::Up || n == Nav::Down) {
            const bool ok = n == Nav::Up ? r.y + r.h <= c.y + 2 : r.y >= c.y + c.h - 2;
            if (!ok) continue;
            primary = std::fabs(ry - ccy);
            // the overlap in x counts as near
            const float ox = std::max(0.0f, std::max(r.x - (c.x + c.w), c.x - (r.x + r.w)));
            secondary = ox + std::fabs(rx - ccx) * 0.05f;
        } else {
            const bool same_row = r.y < c.y + c.h - 2 && r.y + r.h > c.y + 2;
            const bool ok = same_row && (n == Nav::Left ? rx < ccx : rx > ccx);
            if (!ok) continue;
            primary = std::fabs(rx - ccx);
        }
        const float score = primary + secondary * 4;
        if (score < best_score) { best_score = score; best = i; }
    }
    return best;
}

void go_page(App& a, int p) {
    a.page = (p + kPageCount) % kPageCount;
    a.focus = 0;
    a.pad_test = false;
    refresh(a);
    if (a.page == kMusic) refresh_music(a, true);   // songs added to the folder since
    if (a.page == kMods) refresh_mods(a);            // mods added or removed since
    if (a.page == kReplays) load_replays(a);         // replay1: recorded since
}

// osk1: a key of the on-screen keyboard pressed (A on it, or a click)
void osk_key(App& a, int key) {
    Modal& m = *a.modal;
    m.osk_st.focus = key;
    switch (osk_press(m.osk_st, m.keys, key, m.field)) {
    case OskResult::kDone: text_close(a, true); break;
    case OskResult::kCancel: text_close(a, false); break;
    case OskResult::kPaste:
        if (char* c = SDL_GetClipboardText()) {
            m.field.insert(clean_pasted_path(c));
            SDL_free(c);
        }
        break;
    case OskResult::kSystem:   // the system's keyboard shown again (Steam's: closed with its own B)
        SDL_StopTextInput(a.win);
        start_text_input(a, true);
        break;
    case OskResult::kNone: break;
    }
}

// osk1: the pad's buttons on the on-screen keyboard: A types the key, B deletes (on an empty field: cancels), X a
// space, Y shift / symbols, LB / RB the cursor, Start done, Back / Select cancels (the D-pad and the stick: nav)
void osk_button(App& a, SDL_GamepadButton b) {
    Modal& m = *a.modal;
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: osk_key(a, m.osk_st.focus); break;
    case SDL_GAMEPAD_BUTTON_EAST:
        if (m.field.empty()) text_close(a, false);
        else m.field.erase_back();
        break;
    case SDL_GAMEPAD_BUTTON_WEST: m.field.insert(" "); break;
    case SDL_GAMEPAD_BUTTON_NORTH: osk_cycle_layer(m.osk_st); break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: m.field.left(); break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: m.field.right(); break;
    case SDL_GAMEPAD_BUTTON_START: text_close(a, true); break;
    case SDL_GAMEPAD_BUTTON_BACK: text_close(a, false); break;
    default: break;
    }
}

void modal_nav(App& a, Nav n) {
    Modal& m = *a.modal;
    if (m.input && m.osk) {   // osk1: the D-pad / stick move on the keyboard; the triggers: the field's start / end
        switch (n) {
        case Nav::Up: m.osk_st.focus = osk_move(m.keys, m.osk_st.focus, 0, -1); break;
        case Nav::Down: m.osk_st.focus = osk_move(m.keys, m.osk_st.focus, 0, 1); break;
        case Nav::Left: m.osk_st.focus = osk_move(m.keys, m.osk_st.focus, -1, 0); break;
        case Nav::Right: m.osk_st.focus = osk_move(m.keys, m.osk_st.focus, 1, 0); break;
        case Nav::Confirm: osk_key(a, m.osk_st.focus); break;
        case Nav::Back: osk_button(a, SDL_GAMEPAD_BUTTON_EAST); break;
        case Nav::PrevTab: m.field.home(); break;
        case Nav::NextTab: m.field.end(); break;
        case Nav::PrevPage: m.field.left(); break;
        case Nav::NextPage: m.field.right(); break;
        }
        return;
    }
    if (n == Nav::Left || n == Nav::Up) m.focus = m.focus > 0 ? m.focus - 1 : 0;
    else if (n == Nav::Right || n == Nav::Down) m.focus = std::min(m.focus + 1, m.buttons.size() - 1);
    else if (n == Nav::Confirm) {
        if (m.input) {
            text_close(a, m.focus == 0);   // a text field's first button: OK
            return;
        }
        auto fn = m.buttons[m.focus].fn;
        a.modal.reset();   // the button may open another
        if (fn) fn();
    } else if (n == Nav::Back) {
        if (m.input) text_close(a, false);
        else a.modal.reset();
    }
}

// rebind2: Delete / X / Square: the focused control back to its default (the controls editor's rows)
void reset_focused(App& a) {
    if (a.child || a.skip_at > 0 || a.modal || a.browser || !a.in_content || a.widgets.empty()) return;
    Widget& w = a.widgets[std::size_t(std::clamp(a.focus, 0, int(a.widgets.size()) - 1))];
    if (w.reset) w.reset();
}

void nav(App& a, Nav n) {
    a.reveal_focus = true;
    a.mouse_on = false;   // the focus shows, not what the pointer rests on
    if (a.child || a.skip_at > 0) return;
    if (a.modal) {
        modal_nav(a, n);
        return;
    }
    if (a.browser) {   // osk1
        browse_nav(a, n);
        return;
    }
    if (n == Nav::PrevPage) { go_page(a, a.page - 1); return; }
    if (n == Nav::NextPage) { go_page(a, a.page + 1); return; }
    if (n == Nav::PrevTab || n == Nav::NextTab) {
        if (a.page == kSettings) {
            const std::size_t count = settings_content(a.settings, "", false, a.languages, a.language_note).tabs.size();
            if (count) a.settings_tab = (a.settings_tab + (n == Nav::PrevTab ? count - 1 : 1)) % count;
        }
        if (a.page == kShowroom) showroom_step(a, n == Nav::PrevTab ? -1 : 1);
        return;
    }
    if (!a.in_content) {
        if (n == Nav::Up) go_page(a, a.page - 1);
        else if (n == Nav::Down) go_page(a, a.page + 1);
        else if ((n == Nav::Right || n == Nav::Confirm) && !a.widgets.empty()) {
            a.in_content = true;
            a.focus = std::clamp(a.focus, 0, int(a.widgets.size()) - 1);
        }
        return;
    }
    if (a.widgets.empty()) {
        if (n == Nav::Back || n == Nav::Left) a.in_content = false;
        return;
    }
    Widget& w = a.widgets[std::size_t(std::clamp(a.focus, 0, int(a.widgets.size()) - 1))];
    switch (n) {
    case Nav::Up:
    case Nav::Down: {
        const int to = nearest(a, a.focus, n);
        if (to >= 0) a.focus = to;
        else if (n == Nav::Up) a.scroll[a.page] = 0;   // the top row: the page's top (its title, its first lines)
        else {                                           // the bottom row: the page's end (text under the last widget)
            a.scroll[a.page] = 1e6f;                     // (render clamps it)
            a.reveal_focus = false;
        }
        break;
    }
    case Nav::Left:
    case Nav::Right:
        if (w.change) w.change(n == Nav::Left ? -1 : 1);
        else {
            const int to = nearest(a, a.focus, n);
            if (to >= 0) a.focus = to;
            else if (n == Nav::Left) a.in_content = false;
        }
        break;
    case Nav::Confirm:
        if (w.activate) w.activate();
        break;
    case Nav::Back: a.in_content = false; break;
    default: break;
    }
}

// osk1: a click on the folder list: a place opens; a row is highlighted, and opened when clicked again; the buttons
void browse_click(App& a, float x, float y) {
    Browser& b = *a.browser;
    b.follow = true;
    for (std::size_t k = 0; k < b.place_rects.size(); ++k)
        if (b.place_rects[k].w > 0 && in_rect(b.place_rects[k], x, y)) {
            b.place_sel = int(k);
            b.column = 0;
            browse_open(a);
            return;
        }
    for (std::size_t k = 0; k < b.row_rects.size(); ++k)
        if (b.row_rects[k].w > 0 && in_rect(b.row_rects[k], x, y)) {
            const bool again = b.column == 1 && b.sel == int(k);
            b.column = 1;
            b.sel = int(k);
            if (again) browse_open(a);
            return;
        }
    if (b.pick_folder && in_rect(b.choose_r, x, y)) browse_choose(a);
    else if (in_rect(b.cancel_r, x, y)) a.browser.reset();
    else if (in_rect(b.type_r, x, y)) browse_type(a);
    else if (in_rect(b.up_r, x, y)) {
        b.column = 1;
        if (!is_root_folder(b.dir)) browse_up(a);
    }
}

void click(App& a, float x, float y) {
    if (a.child) return;
    if (a.modal && a.modal->input) {   // osk1: the on-screen keyboard's keys, the field (the cursor), its keyboard mark
        Modal& m = *a.modal;
        if (in_rect(m.osk_toggle, x, y)) {
            if (m.osk) m.osk = false;
            else osk_open(a);
            return;
        }
        if (in_rect(m.field_rect, x, y)) {
            field_click(a, x);
            return;
        }
        if (m.osk) {
            for (std::size_t k = 0; k < m.key_rects.size(); ++k)
                if (in_rect(m.key_rects[k], x, y)) {
                    osk_key(a, int(k));
                    return;
                }
            return;
        }
    }
    if (a.modal) {
        for (std::size_t k = 0; k < a.modal->rects.size(); ++k) {
            const SDL_FRect& r = a.modal->rects[k];
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                a.modal->focus = k;
                modal_nav(a, Nav::Confirm);
                return;
            }
        }
        return;
    }
    if (a.browser) {
        browse_click(a, x, y);
        return;
    }
    for (std::size_t p = 0; p < a.side_widgets.size(); ++p) {
        const SDL_FRect& r = a.side_widgets[p].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            if (int(p) != a.page) go_page(a, int(p));
            a.in_content = false;
            return;
        }
    }
    if (y < kHeaderH || y > a.H - kFooterH) return;
    for (std::size_t k = 0; k < a.widgets.size(); ++k) {
        Widget& w = a.widgets[k];
        if (x >= w.r.x && x < w.r.x + w.r.w && y >= w.r.y && y < w.r.y + w.r.h) {
            a.in_content = true;
            a.focus = int(k);
            if (w.click) w.click(x, y);
            else if (w.activate) w.activate();
            return;
        }
    }
}

// A right click: the widget's `alt` (the mouse's way to what left / right do); on a value widget without one, one value back
void right_click(App& a, float x, float y) {
    if (a.child || a.modal || y < kHeaderH || y > a.H - kFooterH) return;
    if (a.browser) {   // osk1: the folder list: the folder above
        if (!is_root_folder(a.browser->dir)) browse_up(a);
        return;
    }
    for (std::size_t k = 0; k < a.widgets.size(); ++k) {
        Widget& w = a.widgets[k];
        if (x >= w.r.x && x < w.r.x + w.r.w && y >= w.r.y && y < w.r.y + w.r.h) {
            a.in_content = true;
            a.focus = int(k);
            if (w.alt) w.alt();
            else if (w.change) w.change(-1);
            return;
        }
    }
}

// A text field's keys: typed text at the cursor, Backspace / Delete, the cursor (Left / Right, Home / End), Ctrl+V
// pastes, Enter: OK, Esc: cancel
void text_key(App& a, const SDL_Event& e) {
    Modal& m = *a.modal;
    if (e.type == SDL_EVENT_TEXT_INPUT) {
        m.field.insert(e.text.text);
        return;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return;
    switch (e.key.key) {
    case SDLK_BACKSPACE: m.field.erase_back(); break;
    case SDLK_DELETE: m.field.erase_forward(); break;
    case SDLK_LEFT: m.field.left(); break;
    case SDLK_RIGHT: m.field.right(); break;
    case SDLK_HOME: m.field.home(); break;
    case SDLK_END: m.field.end(); break;
    case SDLK_V:
        if (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) {
            if (char* c = SDL_GetClipboardText()) {
                m.field.insert(clean_pasted_path(c));
                SDL_free(c);
            }
        }
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: text_close(a, true); break;
    case SDLK_ESCAPE: text_close(a, false); break;
    default: break;
    }
}

// osk1: the folder list's keys: the arrows, Enter opens, Backspace the folder above, Space chooses the folder shown,
// Tab the places, Esc cancels, a letter the next row starting with it
void browse_key(App& a, const SDL_Event& e) {
    Browser& b = *a.browser;
    const SDL_Keycode k = e.key.key;
    switch (k) {
    case SDLK_UP: nav(a, Nav::Up); break;
    case SDLK_DOWN: nav(a, Nav::Down); break;
    case SDLK_LEFT: nav(a, Nav::Left); break;
    case SDLK_RIGHT: nav(a, Nav::Right); break;
    case SDLK_PAGEUP: nav(a, Nav::PrevPage); break;
    case SDLK_PAGEDOWN: nav(a, Nav::NextPage); break;
    case SDLK_HOME: (b.column == 0 ? b.place_sel : b.sel) = 0; b.follow = true; break;
    case SDLK_END:
        (b.column == 0 ? b.place_sel : b.sel) = std::max(0, int(b.column == 0 ? b.places.size() : b.list.entries.size()) - 1);
        b.follow = true;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: nav(a, Nav::Confirm); break;
    case SDLK_BACKSPACE: b.column = 1; browse_up(a); break;
    case SDLK_SPACE:
        if (b.pick_folder) browse_choose(a);
        break;
    case SDLK_TAB: b.column = 1 - b.column; break;
    case SDLK_ESCAPE: a.browser.reset(); break;
    default:
        if ((k >= SDLK_A && k <= SDLK_Z) || (k >= SDLK_0 && k <= SDLK_9)) {   // the next name starting with it
            const int n = int(b.list.entries.size());
            for (int step = 1; step <= n; ++step) {
                const int at = (b.sel + step) % n;
                const std::string& name = b.list.entries[std::size_t(at)].name;
                if (!name.empty() && std::tolower(static_cast<unsigned char>(name[0])) == int(k)) {
                    b.sel = at;
                    b.column = 1;
                    b.follow = true;
                    break;
                }
            }
        }
        break;
    }
}

void event(App& a, const SDL_Event& in) {
    SDL_Event e = in;
    SDL_ConvertEventToRenderCoordinates(a.ren, &e);
    if (a.skip_at > 0) {   // launchskip1: the "Starting" screen takes Shift / Back / Select (the launcher opens), the
                           // window's close and the controllers coming and going; nothing else
        const bool shift = e.type == SDL_EVENT_KEY_DOWN && (e.key.scancode == SDL_SCANCODE_LSHIFT || e.key.scancode == SDL_SCANCODE_RSHIFT);
        const bool back = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK;
        if (shift || back) {
            cancel_skip(a);
            return;
        }
        if (e.type != SDL_EVENT_QUIT && e.type != SDL_EVENT_GAMEPAD_ADDED && e.type != SDL_EVENT_GAMEPAD_REMOVED) return;
    }
    b3native::input::handle_sdl_event(a.keys, in);   // the controller page's "what the game gets"
    if (flow_event(a, in)) return;                   // remap1: a rebind takes the next key / pad input
    switch (e.type) {
    case SDL_EVENT_QUIT: a.quit = true; break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: refresh(a); break;
    case SDL_EVENT_MOUSE_MOTION:
        a.mouse_x = e.motion.x;
        a.mouse_y = e.motion.y;
        a.pad_used_last = false;
        a.touch_last = e.motion.which == SDL_TOUCH_MOUSEID;   // osk1
        a.mouse_on = true;
        if (a.show.drag) a.show.tt.drag(e.motion.xrel, e.motion.yrel);   // photo2: the showroom's car turned
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        a.mouse_x = e.button.x;
        a.mouse_y = e.button.y;
        a.pad_used_last = false;
        a.touch_last = e.button.which == SDL_TOUCH_MOUSEID;   // osk1: a touch on the screen opens what a pad would
        a.mouse_on = true;
        const SDL_FRect& vr = a.show.rect;
        if (a.page == kShowroom && !a.modal && !a.browser && !a.child && e.button.x >= vr.x && e.button.x < vr.x + vr.w && e.button.y >= vr.y &&
            e.button.y < vr.y + vr.h && e.button.y > kHeaderH && e.button.y < a.H - kFooterH) {
            a.show.drag = true;
            a.in_content = true;
            break;
        }
        if (e.button.button == SDL_BUTTON_LEFT) click(a, e.button.x, e.button.y);
        else if (e.button.button == SDL_BUTTON_RIGHT) right_click(a, e.button.x, e.button.y);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        a.show.drag = false;
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        const SDL_FRect& vr = a.show.rect;
        if (a.page == kShowroom && !a.modal && a.mouse_x >= vr.x && a.mouse_x < vr.x + vr.w && a.mouse_y >= vr.y && a.mouse_y < vr.y + vr.h) {
            a.show.tt.wheel(e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y);
            break;
        }
        if (a.browser && !a.modal) {   // osk1: the folder list scrolls (the highlighted row may leave the view)
            a.browser->follow = false;
            a.browser->first = std::max(0, a.browser->first - int(std::lround(e.wheel.y * 3)));
            a.mouse_on = true;
            break;
        }
        if (!a.modal) {
            a.scroll[a.page] -= e.wheel.y * 48;
            a.reveal_focus = false;   // the wheel scrolls past a clicked row (it used to pull the page back to it)
            a.mouse_on = true;
        }
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (a.modal && a.modal->input) text_key(a, e);
        break;
    case SDL_EVENT_KEY_DOWN: {
        a.pad_used_last = false;
        a.touch_last = false;
        if (a.modal && a.modal->input) {
            text_key(a, e);
            break;
        }
        if (a.browser && !a.modal) {   // osk1
            browse_key(a, e);
            break;
        }
        const SDL_Keycode k = e.key.key;
        if (a.pad_test && a.page == kPads) {   // the test: only Esc leaves
            if (k == SDLK_ESCAPE) a.pad_test = false;
            break;
        }
        const bool shift = e.key.mod & SDL_KMOD_SHIFT;
        if (k == SDLK_UP) nav(a, Nav::Up);
        else if (k == SDLK_DOWN) nav(a, Nav::Down);
        else if (k == SDLK_LEFT) nav(a, Nav::Left);
        else if (k == SDLK_RIGHT) nav(a, Nav::Right);
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) nav(a, Nav::Confirm);
        else if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) nav(a, Nav::Back);
        else if (k == SDLK_TAB) nav(a, shift ? Nav::PrevPage : Nav::NextPage);
        else if (k == SDLK_PAGEUP) nav(a, Nav::PrevTab);
        else if (k == SDLK_PAGEDOWN) nav(a, Nav::NextTab);
        else if (k == SDLK_HOME) a.scroll[a.page] = 0;
        else if (k == SDLK_DELETE) reset_focused(a);
        break;
    }
    case SDL_EVENT_GAMEPAD_ADDED: {
        const bool have = std::any_of(a.pads.begin(), a.pads.end(), [&](SDL_Gamepad* g) { return SDL_GetGamepadID(g) == e.gdevice.which; });
        if (!have)
            if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) a.pads.push_back(g);
        break;
    }
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:   // pad1: the gyro readout's samples
        if (e.gsensor.which == a.gyro_pad) b3native::input::feed_gyro_event(a.gyro, e);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (e.gdevice.which == a.gyro_pad) a.gyro_pad = 0;            // pad1: gone (nothing to put back)
        if (e.gdevice.which == a.trig_test_pad) a.trig_test_pad = 0;
        for (auto it = a.pads.begin(); it != a.pads.end(); ++it)
            if (SDL_GetGamepadID(*it) == e.gdevice.which) {
                SDL_CloseGamepad(*it);
                a.pads.erase(it);
                break;
            }
        if (a.pads.empty()) a.pad_test = false;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        if (a.dialogs_open > 0) break;   // a system file dialog is open: the pad does not press the launcher's buttons behind it
        a.pad_used_last = true;
        for (std::size_t k = 0; k < a.pads.size(); ++k)
            if (SDL_GetGamepadID(a.pads[k]) == e.gbutton.which && a.page == kPads && !a.pad_test) a.pad_sel = k;
        a.touch_last = false;
        if (a.pad_test && a.page == kPads) {
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) a.east_held_since = a.now();
            // Start and Back / Select together stop it at once (any pad)
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)
                for (SDL_Gamepad* g : a.pads)
                    if (SDL_GetGamepadID(g) == e.gbutton.which && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START) &&
                        SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) {
                        a.pad_test = false;
                        a.east_held_since = -1;
                    }
            break;
        }
        if (a.modal && a.modal->input && !a.child) {   // osk1: a text field: its on-screen keyboard (opened by the first press)
            if (!a.modal->osk) osk_open(a);
            else osk_button(a, SDL_GamepadButton(e.gbutton.button));
            break;
        }
        if (a.browser && !a.modal && !a.child) {   // osk1: the folder list's own buttons (A, B, the shoulders: nav)
            Browser& b = *a.browser;
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                if (b.pick_folder) browse_choose(a);
                else if (b.column == 1) browse_open(a);
                break;
            }
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) { a.browser.reset(); break; }
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) { b.column = 1 - b.column; break; }
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) { browse_type(a); break; }
        }
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: nav(a, Nav::Confirm); break;
        case SDL_GAMEPAD_BUTTON_EAST: nav(a, Nav::Back); break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: nav(a, Nav::PrevPage); break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: nav(a, Nav::NextPage); break;
        case SDL_GAMEPAD_BUTTON_WEST: reset_focused(a); break;
        default: break;   // the D-pad: polled with the stick (hold repeat)
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) a.east_held_since = -1;
        break;
    default: break;
    }
}

// The D-pad and the left stick as held directions, with a hold repeat; the triggers as tab steps
void poll_pad_nav(App& a) {
    bool dir[4] = {};
    float lt = 0, rt = 0;
    for (SDL_Gamepad* g : a.pads) {
        const float x = float(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX)) / 32767.0f;
        const float y = float(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY)) / 32767.0f;
        dir[0] |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -0.6f;
        dir[1] |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 0.6f;
        dir[2] |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -0.6f;
        dir[3] |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 0.6f;
        lt = std::max(lt, float(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) / 32767.0f);
        rt = std::max(rt, float(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) / 32767.0f);
    }
    const double t = a.now();
    {   // osk1: B / Circle held on the on-screen keyboard: Backspace repeats (the first one is the press's)
        bool east = false;
        for (SDL_Gamepad* g : a.pads) east = east || SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST);
        if (a.modal && a.modal->input && a.modal->osk && east) {
            if (!a.erase.down) a.erase = {true, t + 0.45};
            else if (t >= a.erase.next) {
                a.erase.next = t + 0.06;
                a.modal->field.erase_back();
            }
        } else {
            a.erase.down = false;
        }
    }
    if (a.dialogs_open > 0) {   // a system file dialog is open: held as pressed, so nothing moves until released after it
        for (int k = 0; k < 4; ++k)
            if (dir[k]) a.hold[k] = {true, 1e300};
        a.trig_l = a.trig_l || lt > 0.3f;
        a.trig_r = a.trig_r || rt > 0.3f;
        return;
    }
    if (a.pad_test && a.page == kPads) {
        for (auto& h : a.hold) h.down = false;
        if (a.east_held_since >= 0 && t - a.east_held_since > kPadTestHold) {
            a.pad_test = false;
            a.east_held_since = -1;
        }
        return;
    }
    const Nav navs[4] = {Nav::Up, Nav::Down, Nav::Left, Nav::Right};
    for (int k = 0; k < 4; ++k) {
        auto& h = a.hold[k];
        if (dir[k] && !h.down) {
            h.down = true;
            h.next = t + 0.38;
            a.pad_used_last = true;
            nav(a, navs[k]);
        } else if (dir[k] && t >= h.next) {
            h.next = t + 0.075;
            nav(a, navs[k]);
        } else if (!dir[k]) {
            h.down = false;
        }
    }
    // edges with hysteresis: pressed past 0.6, released below 0.3
    if (lt > 0.6f && !a.trig_l) {
        a.trig_l = true;
        a.pad_used_last = true;
        nav(a, Nav::PrevTab);
    } else if (lt < 0.3f) a.trig_l = false;
    if (rt > 0.6f && !a.trig_r) {
        a.trig_r = true;
        a.pad_used_last = true;
        nav(a, Nav::NextTab);
    } else if (rt < 0.3f) a.trig_r = false;
}

// ---- screenshots (checks: --screenshot DIR) ----

bool save_shot(App& a, const fs::path& to) {
    SDL_Surface* s = SDL_RenderReadPixels(a.ren, nullptr);
    if (!s) return false;
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return false;
    std::vector<std::uint8_t> px(std::size_t(c->w) * std::size_t(c->h) * 4);
    for (int y = 0; y < c->h; ++y) std::memcpy(px.data() + std::size_t(y) * std::size_t(c->w) * 4, static_cast<const std::uint8_t*>(c->pixels) + std::size_t(y) * std::size_t(c->pitch), std::size_t(c->w) * 4);
    for (std::size_t k = 3; k < px.size(); k += 4) px[k] = 255;
    const bool ok = b3asset::write_png_rgba(utf8_of(to), c->w, c->h, px.data());
    SDL_DestroySurface(c);
    return ok;
}

// A virtual gamepad for the controller page's picture (SDL's virtual joystick driver)
SDL_JoystickID demo_pad() {
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.vendor_id = 0x054C;   // a DualSense's ids: the PS layout
    d.product_id = 0x0CE6;
    d.name = "DualSense Wireless Controller (virtual)";
    return SDL_AttachVirtualJoystick(&d);
}

// ---- showhd1: the showroom's HD textures (--screenshot) ----
// A test pack made from the disc at run time in the check's own data folder (the first car's texture under the key the
// renderer looks up for its paints 1 and 2: b3asset::hd_candidate_keys' first, the base level; solid magenta / green,
// twice the size) and a mod with a [textures] pack (yellow for paint 1), both removed afterwards. The view is drawn with
// HD TEXTURES off, on, paint 2, the mod on (with HD TEXTURES on and off) and off again: the pack's colours must cover the
// car and change nothing else, the mod's must win over the player's pack, and off must give the original back exactly.
// The view's pictures go to `dir` (showroom_hd_view_*.png). Leaves HD TEXTURES on with the pack for the "showroom_hd"
// shot; showroom_hd_end puts everything back. Returns the failures (0: PASS, or skipped without game data / GPU).
struct ShowroomHdCheck {
    b3settings::Settings saved;
    fs::path pack, mod;
    bool made = false, had_mods = true;
};

bool write_test_dds(const fs::path& to, std::uint32_t w, std::uint32_t h, const std::uint8_t rgb[3]) {
    std::vector<std::uint8_t> d(128 + std::size_t(w) * h * 4, 0);
    auto put = [&d](std::size_t at, std::uint32_t v) { std::memcpy(&d[at], &v, 4); };
    std::memcpy(&d[0], "DDS ", 4);
    put(4, 124);
    put(8, 0x100F);   // caps, height, width, pitch, pixel format
    put(12, h);
    put(16, w);
    put(20, w * 4);
    put(76, 32);
    put(80, 0x41);    // RGB + alpha, 32 bits, R in the low byte
    put(88, 32);
    put(92, 0x000000FF);
    put(96, 0x0000FF00);
    put(100, 0x00FF0000);
    put(104, 0xFF000000);
    put(108, 0x1000);
    for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
        std::uint8_t* p = &d[128 + i * 4];
        p[0] = rgb[0];
        p[1] = rgb[1];
        p[2] = rgb[2];
        p[3] = 0x80;   // opaque in PS2 units (as PCSX2's dumps)
    }
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    std::ofstream o(to, std::ios::binary);
    o.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    return bool(o);
}

int showroom_hd_check(App& a, const fs::path& dir, ShowroomHdCheck& st) {
    namespace ba = b3asset;
    auto& s = a.show;
    showroom_refresh(a);
    std::string why;
    if (!a.game.ok || s.cars.empty()) {
        std::printf("b3launcher: showroom HD check SKIPPED (no game data: the test pack is made from the disc's cars)\n");
        return 0;
    }
    if (!s.view) s.view = std::make_unique<showroom::View>();
    if (!s.view->ready(&why)) {
        std::printf("b3launcher: showroom HD check SKIPPED (no GPU device: %s)\n", why.c_str());
        return 0;
    }
    // the keys of the first car's texture, paints 1 and 2, as the renderer's decode hashes them
    const showroom::CarEntry car = s.cars[0];
    std::vector<std::uint8_t> bytes;
    if (!ba::disc::read_file(car.file, bytes) || bytes.size() < 0x100) {
        std::printf("b3launcher: showroom HD check FAIL (cannot read %s)\n", utf8_of(car.file).c_str());
        return 1;
    }
    const ba::Bytes f{bytes.data(), bytes.size()};
    const std::size_t tex_at = f.u32(0x60);
    struct Key { std::string name; std::uint32_t w = 0, h = 0; };
    std::vector<Key> keys;
    int npal = 1;
    for (int p = 0; p < 2 && p < npal; ++p) {
        ba::GsMemory gs;
        ba::DecodedRaster r;
        if (!tex_at || !ba::decode_raster(f, tex_at, gs, r, p, &why)) {
            std::printf("b3launcher: showroom HD check FAIL (%s: no texture: %s)\n", car.name.c_str(), why.c_str());
            return 1;
        }
        npal = std::max<int>(1, r.hdr.palettes);
        const std::vector<ba::HdCandidate> c = ba::hd_candidate_keys(gs, r.hdr, ba::palette_cbp_add(f, tex_at, p));
        if (c.empty()) {
            std::printf("b3launcher: showroom HD check FAIL (%s: the texture has no HD key)\n", car.name.c_str());
            return 1;
        }
        keys.push_back({ba::hd_key_name(c.front().key), 2u << (r.hdr.tex0 >> 26 & 0xF), 2u << (r.hdr.tex0 >> 30 & 0xF)});
    }
    static const std::uint8_t kMagenta[3] = {255, 0, 255}, kGreen[3] = {0, 255, 0}, kYellow[3] = {255, 255, 0};
    std::error_code ec;
    st.saved = a.settings;
    st.pack = a.pref / "showroom-hd-check";
    st.mod = b3mod::mods_folder(a.pref) / "showroom-hd-check";
    st.had_mods = fs::exists(st.mod.parent_path(), ec);
    st.made = true;
    bool wrote = write_test_dds(st.pack / "cars" / (keys[0].name + ".dds"), keys[0].w, keys[0].h, kMagenta);
    if (keys.size() > 1) wrote = wrote && write_test_dds(st.pack / (keys[1].name + ".dds"), keys[1].w, keys[1].h, kGreen);
    // (the race renderer's lookup finds the files: hd_lookup over the pack, as HdTextures::apply)
    ba::HdPack index;
    index.open(utf8_of(st.pack));
    int found = 0;
    for (int p = 0; p < int(keys.size()); ++p) {
        ba::GsMemory gs;
        ba::DecodedRaster r;
        ba::decode_raster(f, tex_at, gs, r, p);
        const std::string* hit = ba::hd_lookup(index, gs, r.hdr, ba::palette_cbp_add(f, tex_at, p));
        found += hit && fs::path(*hit).stem().string() == keys[std::size_t(p)].name;
    }

    const int W = 640, H = 360;
    showroom::Turntable tt;
    tt.spin = false;
    struct Pic { std::vector<std::uint8_t> px; std::size_t replaced = 0; bool ok = false; };
    auto draw = [&](int paint, bool with_car = true) {
        Pic r;
        s.sel = 0;
        s.paint = paint;
        showroom_load(a);   // as the page each frame: loads again when the car, the paint or the HD packs changed
        r.ok = s.error.empty() && s.view->loaded() && s.view->draw(tt, W, H, r.px, with_car);
        r.replaced = s.view->hd_replaced();
        for (std::size_t k = 3; k < r.px.size(); k += 4) r.px[k] = 255;
        return r;
    };
    auto diff = [&](const Pic& x, const Pic& y, std::size_t k, int over) {
        for (int c = 0; c < 3; ++c)
            if (std::abs(int(x.px[k * 4 + c]) - int(y.px[k * 4 + c])) > over) return true;
        return false;
    };
    a.settings.hd_pack_dir = utf8_of(st.pack);   // as the HD Textures page's folder choice (not saved: the check's own)
    a.settings.hd_textures = false;
    refresh_hd(a, true);
    const Pic plain = draw(0), floor = draw(0, false);
    a.settings.hd_textures = true;   // the HD TEXTURES switch as the Settings / HD Textures pages set it
    const Pic hd = draw(0);
    const std::size_t indexed = s.view->hd_indexed();
    const Pic hd2 = draw(1);
    // a mod with a [textures] pack: on, it wins over the player's pack (and stays with HD TEXTURES off)
    fs::create_directories(st.mod, ec);
    std::ofstream(st.mod / "mod.ini") << "[mod]\nid = showroom-hd-check\nname = Showroom HD check\nversion = 1.0\napi = 1\n"
                                         "description = A mod the launcher's showroom HD check makes and removes.\n[textures]\npack = textures\n";
    wrote = wrote && write_test_dds(st.mod / "textures" / (keys[0].name + ".dds"), keys[0].w, keys[0].h, kYellow);
    refresh_mods(a);
    for (b3mod::OrderEntry& e : a.mod_order.entries)
        if (e.id == "showroom-hd-check") e.enabled = true;
    update_mod_set(a);
    const Pic mod = draw(0);
    a.settings.hd_textures = false;
    const Pic mod_off = draw(0);
    fs::remove_all(st.mod, ec);
    refresh_mods(a);
    const Pic plain2 = draw(0);

    // the car: where the picture with it differs from the floor alone (and 2 px around it: its edges' blend)
    const std::size_t n = std::size_t(W) * H;
    std::vector<std::uint8_t> car_mask(n, 0);
    bool all_ok = plain.ok && floor.ok && hd.ok && hd2.ok && mod.ok && mod_off.ok && plain2.ok;
    if (all_ok)
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                if (!diff(plain, floor, std::size_t(y) * W + x, 4)) continue;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        if (y + dy >= 0 && y + dy < H && x + dx >= 0 && x + dx < W) car_mask[std::size_t(y + dy) * W + std::size_t(x + dx)] = 1;
            }
    struct Change { std::size_t changed = 0, outside = 0, coloured = 0; };
    // pixels changed against `base`; `coloured`: of those, the ones whose hue is the replacement's (its strongest channels)
    auto changes = [&](const Pic& x, const Pic& base, const std::uint8_t rgb[3]) {
        Change c;
        if (!all_ok) return c;
        for (std::size_t k = 0; k < n; ++k) {
            if (!diff(x, base, k, 24)) continue;
            ++c.changed;
            if (!car_mask[k]) ++c.outside;
            const int r = x.px[k * 4], g = x.px[k * 4 + 1], b = x.px[k * 4 + 2];
            const int on_min = std::min({rgb[0] ? r : 999, rgb[1] ? g : 999, rgb[2] ? b : 999});
            const int off_max = std::max({rgb[0] ? 0 : r, rgb[1] ? 0 : g, rgb[2] ? 0 : b});
            if (on_min > off_max + 20) ++c.coloured;
        }
        return c;
    };
    std::size_t car_px = 0, same_off = 0, same_mod = 0;
    for (std::size_t k = 0; all_ok && k < n; ++k) {
        car_px += car_mask[k];
        same_off += plain2.px.size() == plain.px.size() && diff(plain2, plain, k, 0);
        same_mod += diff(mod_off, mod, k, 0);
    }
    const Change ch = changes(hd, plain, kMagenta), ch2 = changes(hd2, hd, kGreen), chm = changes(mod, hd, kYellow);
    auto mostly = [](const Change& c, std::size_t car) { return c.changed >= std::max<std::size_t>(500, car / 10) && c.outside == 0 && c.coloured * 2 > c.changed; };
    const bool two = keys.size() > 1;
    const bool ok = wrote && all_ok && found == int(keys.size()) && indexed == keys.size() && plain.replaced == 0 && hd.replaced >= 1 &&
                    mostly(ch, car_px) && (!two || (hd2.replaced >= 1 && mostly(ch2, car_px))) && mod.replaced >= 1 && mostly(chm, car_px) &&
                    mod_off.replaced >= 1 && same_mod == 0 && plain2.replaced == 0 && same_off == 0;
    std::printf("b3launcher: showroom HD check %s (%s, %d paints; test pack: %s%s%s, found by the renderer's lookup %d of %zu, %zu indexed; "
                "car %zu px of %d: HD on %zu changed (%zu magenta, %zu off the car; %zu textures replaced), paint 2 %zu changed (%zu green, "
                "%zu off the car), a mod's pack over it %zu changed (%zu yellow, %zu off the car), the mod with HD TEXTURES off %zu px "
                "different, HD off again %zu px different from the original)\n",
                ok ? "PASS" : "FAIL", car.name.c_str(), npal, keys[0].name.c_str(), two ? ", " : "", two ? keys[1].name.c_str() : "", found,
                keys.size(), indexed, car_px, W * H, ch.changed, ch.coloured, ch.outside, hd.replaced, ch2.changed, ch2.coloured, ch2.outside,
                chm.changed, chm.coloured, chm.outside, same_mod, same_off);
    // the view's pictures, and where HD changed it (white: changed on the car, red: changed off it, grey: the car)
    if (all_ok) {
        Pic map = plain;
        for (std::size_t k = 0; k < n; ++k) {
            const bool c = diff(hd, plain, k, 24);
            const std::uint8_t v = c ? 255 : car_mask[k] ? 70 : 0;
            map.px[k * 4] = c && !car_mask[k] ? 255 : v;
            map.px[k * 4 + 1] = c && !car_mask[k] ? 0 : v;
            map.px[k * 4 + 2] = c && !car_mask[k] ? 0 : v;
        }
        const std::pair<const char*, const Pic*> pics[] = {{"plain", &plain}, {"hd", &hd}, {"hd_paint2", &hd2}, {"mod", &mod}, {"off", &plain2}, {"diff", &map}};
        for (const auto& [name, pic] : pics) {
            const fs::path to = dir / ("showroom_hd_view_" + std::string(name) + ".png");
            if (ba::write_png_rgba(utf8_of(to), W, H, pic->px.data())) std::printf("b3launcher: %s\n", utf8_of(to).c_str());
        }
    }
    // the window's shot next: HD TEXTURES on with the test pack, the first car, paint 1
    a.settings.hd_textures = true;
    s.sel = 0;
    s.paint = 0;
    return ok ? 0 : 1;
}

void showroom_hd_end(App& a, ShowroomHdCheck& st) {
    if (!st.made) return;
    std::error_code ec;
    a.settings = st.saved;
    fs::remove_all(st.pack, ec);
    refresh_hd(a, true);
    fs::remove_all(st.mod, ec);
    if (!st.had_mods) fs::remove_all(st.mod.parent_path(), ec);
    refresh_mods(a);
    st.made = false;
}

// ---- launcherux1: the walk (--screenshot) ----
// Every page (each settings tab, the Controllers page with each device) driven by the keys alone and by a pad alone:
// synthetic key events, and a virtual pad's D-pad and buttons through SDL and the launcher's own pad polling. From the
// side bar Right goes into the page; from there Up / Down / Left / Right (Left / Right only where they move: not on a
// value widget, where they change it) are followed until nothing new is reached. Every widget must be reached and shown
// on the screen when it is, Back must return to the side bar from each, and Down pressed on and on must show the page's
// end. The mouse: each widget's middle must click that widget (the click's own hit test; nothing is pressed) and show
// its description, and the wheel must scroll a page taller than the window down and back. Then the dialogs (every
// button reached by left / right, Back closes them) and the controls' rebind flow (started and cancelled by the keys
// and by a pad, the controls unchanged). Returns the number of failures; prints a PASS / FAIL line per page.
int walk_checks(App& a, SDL_JoystickID& vj) {
    namespace in = b3native::input;
    int fails = 0;
    const SDL_WindowID wid = SDL_GetWindowID(a.win);
    auto settle = [&]() { render(a); render(a); };   // the scroll follows the focus, then the page is drawn there
    // the pad: SDL's events of the virtual pad only (a window event must not re-read the folders mid-walk)
    auto pump = [&]() {
        SDL_UpdateJoysticks();
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type >= SDL_EVENT_GAMEPAD_AXIS_MOTION && e.type < SDL_EVENT_FINGER_DOWN) event(a, e);
    };
    if (!vj) vj = demo_pad();
    pump();
    SDL_Joystick* joy = SDL_GetJoystickFromID(vj);
    if (!joy) joy = SDL_OpenJoystick(vj);
    if (!joy) {
        std::printf("b3launcher: walk check FAIL (no virtual pad: %s)\n", SDL_GetError());
        return 1;
    }
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) SDL_SetJoystickVirtualButton(joy, b, false);
    for (int x = 0; x < SDL_GAMEPAD_AXIS_COUNT; ++x) {
        const bool trigger = x == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || x == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        SDL_SetJoystickVirtualAxis(joy, x, trigger ? SDL_JOYSTICK_AXIS_MIN : 0);
    }
    pump();
    if (SDL_Gamepad* g = SDL_GetGamepadFromID(vj); g && SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000) {   // (triggers at 0)
        SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
        SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
        pump();
    }
    a.pad_test = false;
    poll_pad_nav(a);   // (the hold and trigger states at rest)

    auto key = [&](SDL_Keycode k) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = k;
        e.key.scancode = SDL_GetScancodeFromKey(k, nullptr);
        e.key.down = true;
        e.key.windowID = wid;
        event(a, e);
    };
    auto pad = [&](SDL_GamepadButton b) {
        SDL_SetJoystickVirtualButton(joy, b, true);
        pump();
        poll_pad_nav(a);
        SDL_SetJoystickVirtualButton(joy, b, false);
        pump();
        poll_pad_nav(a);
    };
    auto press = [&](bool use_pad, Nav n) {
        switch (n) {
        case Nav::Up: use_pad ? pad(SDL_GAMEPAD_BUTTON_DPAD_UP) : key(SDLK_UP); break;
        case Nav::Down: use_pad ? pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN) : key(SDLK_DOWN); break;
        case Nav::Left: use_pad ? pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT) : key(SDLK_LEFT); break;
        case Nav::Right: use_pad ? pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) : key(SDLK_RIGHT); break;
        case Nav::Confirm: use_pad ? pad(SDL_GAMEPAD_BUTTON_SOUTH) : key(SDLK_RETURN); break;
        case Nav::Back: use_pad ? pad(SDL_GAMEPAD_BUTTON_EAST) : key(SDLK_ESCAPE); break;
        default: break;
        }
    };
    auto mouse_event = [&](SDL_EventType type, float x, float y, std::uint8_t button = 0, float wheel_y = 0) {
        float wx = x, wy = y;
        SDL_RenderCoordinatesToWindow(a.ren, x, y, &wx, &wy);
        SDL_Event e{};
        e.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) { e.motion.windowID = wid; e.motion.x = wx; e.motion.y = wy; }
        else if (type == SDL_EVENT_MOUSE_WHEEL) { e.wheel.windowID = wid; e.wheel.y = wheel_y; e.wheel.mouse_x = wx; e.wheel.mouse_y = wy; }
        else { e.button.windowID = wid; e.button.button = button; e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.x = wx; e.button.y = wy; }
        event(a, e);
    };
    auto most_scroll = [&]() {   // how far the page scrolls
        const float keep = a.scroll[a.page];
        const bool rv = a.reveal_focus;
        a.scroll[a.page] = 1e6f;
        a.reveal_focus = false;
        render(a);
        const float m = a.scroll[a.page];
        a.scroll[a.page] = keep;
        a.reveal_focus = rv;
        return m;
    };
    auto in_view = [&](int k) {
        const SDL_FRect& r = a.widgets[std::size_t(k)].r;
        const float t = page_top(a), b = page_bottom(a);
        if (r.h > b - t - 16) return r.y < b && r.y + r.h > t;
        return r.y >= t - 1 && r.y + r.h <= b + 1;
    };
    auto short_desc = [&](int k) {
        std::string d = k < int(a.widgets.size()) ? a.widgets[std::size_t(k)].desc : std::string();
        if (d.size() > 48) d = d.substr(0, 45) + "...";
        return "#" + std::to_string(k) + " \"" + d + "\"";
    };

    // the test data: two mods (one with settings rows) and two replay files, removed afterwards
    std::error_code ec;
    const fs::path mods_dir = b3mod::mods_folder(a.pref), rep_dir = b3native::replay::replays_dir(a.pref);
    const bool had_mods_dir = fs::exists(mods_dir, ec), had_rep_dir = fs::exists(rep_dir, ec);
    const fs::path state = b3mod::state_file(a.pref);
    std::string state_before;
    const bool had_state = fs::exists(state, ec);
    if (had_state) {
        std::ifstream f(state, std::ios::binary);
        state_before.assign(std::istreambuf_iterator<char>(f), {});
    }
    const fs::path mod_a = mods_dir / "launcher-walk-a", mod_b = mods_dir / "launcher-walk-b";
    const fs::path rep1 = rep_dir / "launcher-walk-1.b3replay", rep2 = rep_dir / "launcher-walk-2.b3replay";
    {
        fs::create_directories(mod_a / "data", ec);
        fs::create_directories(mod_b / "scripts", ec);
        fs::create_directories(rep_dir, ec);
        std::ofstream(mod_a / "mod.ini") << "[mod]\nid = launcher-walk-a\nname = Launcher walk A\nversion = 1.0\napi = 1\n"
                                            "description = A mod the launcher's walk check makes and removes.\n[files]\nroot = data\n";
        std::ofstream(mod_b / "mod.ini") << "[mod]\nid = launcher-walk-b\nname = Launcher walk B\nversion = 1.0\napi = 3\n"
                                            "description = A script mod with settings rows (the launcher's walk check).\n"
                                            "[script]\nmain = scripts/main.lua\ngameplay = no\n";
        std::ofstream(mod_b / "scripts" / "main.lua") << "-- the launcher's walk check\n";
        b3mod::ModState st;
        st.id = "launcher-walk-b";
        st.loaded = true;
        b3mod::SettingRow t;
        t.mod = st.id;
        t.key = "show";
        t.label = "SHOW";
        t.kind = b3mod::SettingKind::kToggle;
        t.values = {"Off", "On"};
        t.count = 2;
        b3mod::SettingRow c = t;
        c.key = "mode";
        c.label = "MODE";
        c.kind = b3mod::SettingKind::kChoice;
        c.values = {"One", "Two", "Three"};
        c.count = 3;
        st.rows = {t, c};
        std::vector<b3mod::ModState> all = b3mod::load_state(a.pref);
        all.push_back(st);
        b3mod::save_state(a.pref, all, nullptr);
        std::ofstream(rep1, std::ios::binary) << "not a replay (the launcher's walk check)";
        std::ofstream(rep2, std::ios::binary) << "not a replay either";
    }

    struct PageState {
        std::string name;
        std::function<void()> setup;
    };
    std::vector<PageState> pages = {{"Play", [&]() { go_page(a, kPlay); }}};
    {
        const auto tabs = settings_content(a.settings, a.hd_note, a.hd.images > 0, a.languages, a.language_note).tabs;
        for (std::size_t t = 0; t < tabs.size(); ++t)
            pages.push_back({"Settings > " + tabs[t].label, [&a, t]() { go_page(a, kSettings); a.settings_tab = t; }});
    }
    pages.push_back({"HD Textures", [&]() { go_page(a, kHd); }});
    pages.push_back({"Music", [&]() { go_page(a, kMusic); }});
    pages.push_back({"Mods", [&]() { go_page(a, kMods); }});
    pages.push_back({"Game Data", [&]() { go_page(a, kGame); }});
    pages.push_back({"Saves", [&]() { go_page(a, kSaves); }});
    pages.push_back({"Stats", [&]() {
                         go_page(a, kStats);
                         if (a.stats.empty())   // (no saves in the check's folder: three made-up profiles)
                             for (int k = 0; k < 3; ++k) {
                                 App::StatsEntry e;
                                 e.slot = 1;
                                 e.active = true;
                                 e.p.index = k;
                                 e.p.name = "Walk " + std::to_string(k + 1);
                                 a.stats.push_back(e);
                             }
                     }});
    pages.push_back({"Showroom", [&]() { go_page(a, kShowroom); a.show.tt.spin = false; }});
    pages.push_back({"Replays", [&]() { go_page(a, kReplays); }});
    pages.push_back({"Controllers (keyboard)", [&]() { go_page(a, kPads); a.pad_test = false; a.ctl_device = 0; a.ctl_view = 0; }});
    pages.push_back({"Controllers (controller)", [&]() { go_page(a, kPads); a.pad_test = false; a.ctl_device = 1; }});
    pages.push_back({"Credits", [&]() { go_page(a, kCredits); }});

    struct Drive {
        int count = 0, reached = 0, hidden = 0, back_bad = 0, other = 0;
        bool entered = true, end_ok = true;
        std::vector<int> missing;
        std::string note;
        bool ok() const { return entered && end_ok && missing.empty() && !hidden && !back_bad && !other; }
    };
    auto drive = [&](const PageState& ps, bool use_pad) {
        Drive R;
        a.modal.reset();
        a.mouse_on = false;
        ps.setup();
        a.in_content = false;
        a.reveal_focus = true;
        a.scroll[a.page] = 0;
        settle();
        R.count = int(a.widgets.size());
        press(use_pad, Nav::Right);   // from the side bar into the page
        settle();
        if (R.count == 0) {   // nothing to go into: the side bar stays
            R.entered = !a.in_content;
            return R;
        }
        if (!a.in_content) {
            R.entered = false;
            return R;
        }
        std::vector<char> seen(std::size_t(R.count), 0);
        std::vector<int> queue{a.focus};
        seen[std::size_t(a.focus)] = 1;
        if (!in_view(a.focus)) ++R.hidden;
        for (std::size_t q = 0; q < queue.size(); ++q) {
            const int n = queue[q];
            for (Nav d : {Nav::Up, Nav::Down, Nav::Left, Nav::Right}) {
                if (n >= int(a.widgets.size())) break;
                if ((d == Nav::Left || d == Nav::Right) && a.widgets[std::size_t(n)].change) continue;   // a value: they change it
                a.modal.reset();
                a.in_content = true;
                a.focus = n;
                press(use_pad, d);
                if (!a.modal && (!a.in_content || a.focus == n)) continue;   // the side bar (Left), or no move (nothing drawn anew)
                settle();
                if (a.modal) {
                    ++R.other;
                    R.note = "a move from " + short_desc(n) + " opened a dialog";
                    a.modal.reset();
                    continue;
                }
                if (int(a.widgets.size()) != R.count) {
                    ++R.other;
                    R.note = "a move from " + short_desc(n) + " changed the page";
                    continue;
                }
                if (!a.in_content) continue;   // Left: the side bar
                const int to = a.focus;
                if (to != n && !in_view(to)) {
                    ++R.hidden;
                    R.note = short_desc(to) + " not shown when reached";
                }
                if (to >= 0 && to < R.count && !seen[std::size_t(to)]) {
                    seen[std::size_t(to)] = 1;
                    queue.push_back(to);
                }
            }
            a.in_content = true;   // Back from here: the side bar
            a.focus = n;
            press(use_pad, Nav::Back);
            if (a.in_content || a.modal) {
                ++R.back_bad;
                R.note = "Back from " + short_desc(n) + " did not go to the side bar";
            }
            a.modal.reset();
        }
        for (int k = 0; k < R.count; ++k)
            if (!seen[std::size_t(k)]) R.missing.push_back(k);
        R.reached = int(queue.size());
        // the page's end: Down pressed on and on from the first widget
        a.in_content = true;
        a.focus = queue[0];
        a.scroll[a.page] = 0;
        a.reveal_focus = true;
        settle();
        const float most = most_scroll();
        for (int k = 0; k < R.count + 2; ++k) {
            press(use_pad, Nav::Down);
            render(a);
        }
        settle();
        R.end_ok = most <= 1 || a.scroll[a.page] >= most - 1;
        if (!R.end_ok) R.note = "Down pressed on and on stopped at " + std::to_string(int(a.scroll[a.page])) + " of " + std::to_string(int(most)) + " px";
        return R;
    };
    auto drive_text = [&](const Drive& R) {
        if (R.count == 0) return std::string(R.entered ? "PASS (no widgets: stays in the side bar)" : "FAIL (no widgets, but went into the page)");
        if (!R.entered) return std::string("FAIL (Right did not go into the page)");
        std::string t = std::string(R.ok() ? "PASS" : "FAIL") + " (" + std::to_string(R.reached) + " of " + std::to_string(R.count) + " reached";
        if (R.ok()) t += ", Back to the side bar from each, the page's end shown";
        for (std::size_t k = 0; k < R.missing.size() && k < 3; ++k) t += (k ? ", " : "; not reached: ") + short_desc(R.missing[k]);
        if (R.missing.size() > 3) t += " and " + std::to_string(R.missing.size() - 3) + " more";
        if (!R.note.empty()) t += "; " + R.note;
        return t + ")";
    };

    int page_fails = 0;
    for (const PageState& ps : pages) {
        const Drive K = drive(ps, false), P = drive(ps, true);
        // the mouse: each widget's middle clicks it (the click's hit test) and shows its description; a key: the focus's
        a.modal.reset();
        ps.setup();
        a.in_content = true;
        settle();
        const int count = int(a.widgets.size());
        int clickable = 0, hover = 0;
        std::string mnote;
        for (int k = 0; k < count; ++k) {
            a.focus = k;
            a.reveal_focus = true;
            a.mouse_on = false;
            if (!in_view(k)) settle();   // (scrolled to it)
            const SDL_FRect r = a.widgets[std::size_t(k)].r;
            const float cx = r.x + r.w * 0.5f;
            const float cy = std::clamp(r.y + r.h * 0.5f, std::max(r.y + 1, page_top(a) + 2), std::min(r.y + r.h - 1, page_bottom(a) - 2));
            int hit = -1;
            for (int j = 0; j < count && hit < 0; ++j) {
                const SDL_FRect& o = a.widgets[std::size_t(j)].r;
                if (cx >= o.x && cx < o.x + o.w && cy >= o.y && cy < o.y + o.h) hit = j;
            }
            const SDL_FRect& vr = a.show.rect;
            const bool over_view = a.page == kShowroom && cx >= vr.x && cx < vr.x + vr.w && cy >= vr.y && cy < vr.y + vr.h;
            if (hit == k && !over_view && cx > kSideW && cy > kHeaderH && cy < a.H - kFooterH) ++clickable;
            else if (mnote.empty()) mnote = short_desc(k) + " cannot be clicked";
            mouse_event(SDL_EVENT_MOUSE_MOTION, cx, cy);
            if (footer_text(a) == a.widgets[std::size_t(k)].desc) ++hover;
            else if (mnote.empty()) mnote = short_desc(k) + ": the pointer on it does not show its description";
        }
        bool key_desc = true;   // after the pointer, a key: the footer is the focused widget's again
        if (count) {
            key(SDLK_UP);
            settle();
            key_desc = !a.in_content || footer_text(a) == a.widgets[std::size_t(std::clamp(a.focus, 0, int(a.widgets.size()) - 1))].desc;
            if (!key_desc && mnote.empty()) mnote = "after a key the footer still describes the widget under the pointer";
        }
        const bool mouse_ok = clickable == count && hover == count && key_desc;
        // the wheel
        a.in_content = true;
        a.focus = 0;
        a.scroll[a.page] = 0;
        a.reveal_focus = true;
        settle();
        const float most = most_scroll();
        std::string wheel = "-- (not taller than the window)";
        bool wheel_ok = true;
        if (most > 1) {
            const float mx = a.W - 18, my = (page_top(a) + page_bottom(a)) * 0.5f;
            mouse_event(SDL_EVENT_MOUSE_MOTION, mx, my);
            for (int k = 0; k < 3; ++k) {
                mouse_event(SDL_EVENT_MOUSE_WHEEL, mx, my, 0, -1.0f);
                render(a);
            }
            const float down = a.scroll[a.page];
            for (int k = 0; k < 3; ++k) {
                mouse_event(SDL_EVENT_MOUSE_WHEEL, mx, my, 0, 1.0f);
                render(a);
            }
            const float up = a.scroll[a.page];
            wheel_ok = down >= std::min(most, 144.0f) - 1 && up <= 1;
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s (0 -> %.0f -> %.0f px of %.0f)", wheel_ok ? "PASS" : "FAIL", double(down), double(up), double(most));
            wheel = buf;
        }
        a.mouse_on = false;
        a.mouse_x = a.mouse_y = -1;
        const bool ok = K.ok() && P.ok() && mouse_ok && wheel_ok && (K.count > 0 || K.entered) && (P.count > 0 || P.entered);
        if (!ok) ++page_fails;
        std::printf("b3launcher: walk %s %s: keys %s; pad %s; mouse %s (%d of %d clickable, %d show their description%s%s); wheel %s\n",
                    ps.name.c_str(), ok ? "PASS" : "FAIL", drive_text(K).c_str(), drive_text(P).c_str(), mouse_ok ? "PASS" : "FAIL", clickable,
                    count, hover, mnote.empty() ? "" : "; ", mnote.c_str(), wheel.c_str());
    }

    // the dialogs: every button by left / right (the keys; the pad), Back closes; the mouse: a button drawn for each
    struct ModalCase {
        std::string name;
        std::function<void()> open;
    };
    const std::vector<ModalCase> modals = {
        {"Reset all settings?", [&]() { go_page(a, kSettings); confirm_reset(a); }},
        {"a message", [&]() { go_page(a, kPlay); message(a, "Walk", "A message with one button."); }},
        {"Replace file?", [&]() { go_page(a, kSaves); confirm_import(a, false); }},
        {"Delete this replay?", [&]() { go_page(a, kReplays); confirm_delete_replay(a, 0); }},
        {"Allow this mod's code to run?", [&]() {
             go_page(a, kMods);
             if (!a.mods.empty()) ask_code_trust(a, {&a.mods.front()}, "Allow", nullptr);
         }},
        {"a binding's choices (right-click)", [&]() {
             go_page(a, kPads);
             a.ctl_device = 0;
             render(a);
             a.in_content = true;
             a.focus = a.ctl_first_widget;
             settle();
             const SDL_FRect r = a.widgets[std::size_t(a.focus)].r;
             mouse_event(SDL_EVENT_MOUSE_BUTTON_DOWN, r.x + r.w * 0.5f, r.y + r.h * 0.5f, SDL_BUTTON_RIGHT);
             mouse_event(SDL_EVENT_MOUSE_BUTTON_UP, r.x + r.w * 0.5f, r.y + r.h * 0.5f, SDL_BUTTON_RIGHT);
         }},
        {"Type a path (a text field)", [&]() { go_page(a, kGame); ask_path(a, DialogKind::kGameFolder, "Game data folder", "/walk"); }},
    };
    int modal_fails = 0;
    for (const ModalCase& mc : modals) {
        std::string line;
        bool ok = true;
        for (bool use_pad : {false, true}) {
            a.modal.reset();
            a.pad_used_last = use_pad;   // osk1: what opened it (a text field: the on-screen keyboard with a pad)
            a.touch_last = false;
            mc.open();
            settle();
            if (!a.modal) {
                ok = false;
                line += std::string(use_pad ? "; pad" : "keys") + " FAIL (not opened)";
                continue;
            }
            if (a.modal->input && use_pad) {   // osk1: the on-screen keyboard: every key by the D-pad (nothing typed), drawn for the mouse; Select closes it
                const bool osk = a.modal->osk;
                const std::string before = a.modal->field.text;
                const std::size_t nk = a.modal->keys.size();
                std::vector<char> kseen(nk, 0);
                std::vector<int> q;
                if (osk && nk) {
                    q.push_back(a.modal->osk_st.focus);
                    kseen[std::size_t(q[0])] = 1;
                }
                for (std::size_t i = 0; i < q.size() && a.modal; ++i)
                    for (Nav d : {Nav::Up, Nav::Down, Nav::Left, Nav::Right}) {
                        if (!a.modal) break;
                        a.modal->osk_st.focus = q[i];
                        press(true, d);
                        if (!a.modal) break;
                        const int to = a.modal->osk_st.focus;
                        if (to >= 0 && to < int(nk) && !kseen[std::size_t(to)]) {
                            kseen[std::size_t(to)] = 1;
                            q.push_back(to);
                        }
                    }
                render(a);
                const bool drawn = a.modal && a.modal->key_rects.size() == nk;
                const bool same = a.modal && a.modal->field.text == before;
                pad(SDL_GAMEPAD_BUTTON_BACK);
                const bool closed = !a.modal;
                const bool m_ok = osk && q.size() == nk && drawn && same && closed;
                ok = ok && m_ok;
                line += std::string("; pad ") + (m_ok ? "PASS" : "FAIL") + " (" + (osk ? "the on-screen keyboard: " : "NO on-screen keyboard: ") +
                        std::to_string(q.size()) + " of " + std::to_string(nk) + " keys reached" + (drawn ? "" : ", not all drawn for the mouse") +
                        (same ? "" : ", the field CHANGED") + (closed ? ", Select closes it" : ", Select left it open") + ")";
                a.modal.reset();
                stop_text_input(a);
                continue;
            }
            const std::size_t n = a.modal->buttons.size();
            const bool text_field = a.modal->input;
            std::vector<char> seen(n, 0);
            seen[a.modal->focus] = 1;
            if (!(text_field && !use_pad)) {   // (a text field takes the arrows as text keys: Enter is OK, Esc cancels)
                for (Nav d : {Nav::Right, Nav::Left})
                    for (std::size_t k = 0; k < n && a.modal; ++k) {
                        press(use_pad, d);
                        if (a.modal) seen[std::min(a.modal->focus, n - 1)] = 1;
                    }
            } else {
                std::fill(seen.begin(), seen.end(), 1);
            }
            const bool all = std::all_of(seen.begin(), seen.end(), [](char c) { return c != 0; });
            render(a);
            const bool rects = a.modal && a.modal->rects.size() == n;
            press(use_pad, Nav::Back);
            const bool closed = !a.modal;
            const bool m_ok = all && rects && closed;
            ok = ok && m_ok;
            line += std::string(use_pad ? "; pad " : "keys ") + (m_ok ? "PASS" : "FAIL") + " (" + std::to_string(n) + (n == 1 ? " button" : " buttons") +
                    (all ? "" : ", not all reached") + (rects ? "" : ", not all drawn for the mouse") + (closed ? ", Back closes it" : ", Back left it open") + ")";
            a.modal.reset();
            SDL_StopTextInput(a.win);
        }
        if (!ok) ++modal_fails;
        std::printf("b3launcher: walk dialog \"%s\" %s: %s\n", mc.name.c_str(), ok ? "PASS" : "FAIL", line.c_str());
    }

    // the rebind flow: started by Enter / A on an action row, cancelled by Esc / B; a controller's flow takes B as its
    // input (a conflict: Circle has it), B again closes that; nothing saved
    {
        const in::Controls before = a.controls;
        a.flow.cancel();
        go_page(a, kPads);
        a.ctl_device = 0;
        a.ctl_view = 0;
        render(a);
        const auto& order = in::action_order();
        const int cross = int(std::find(order.begin(), order.end(), in::Ds2::Cross) - order.begin());
        a.in_content = true;
        a.focus = a.ctl_first_widget + cross;
        settle();
        using S = in::RebindFlow::State;
        key(SDLK_RETURN);
        bool keys_ok = a.flow.state() == S::Waiting && a.modal;
        key(SDLK_ESCAPE);
        keys_ok = keys_ok && !a.flow.active() && !a.modal;
        a.focus = a.ctl_first_widget + cross;
        pad(SDL_GAMEPAD_BUTTON_SOUTH);
        bool pad_ok = a.flow.state() == S::Waiting && a.modal;
        pad(SDL_GAMEPAD_BUTTON_EAST);
        pad_ok = pad_ok && !a.flow.active() && !a.modal;
        a.ctl_device = 1;
        settle();
        a.focus = a.ctl_first_widget + cross;
        pad(SDL_GAMEPAD_BUTTON_SOUTH);
        bool pad2_ok = a.flow.state() == S::Waiting && a.modal;
        pad(SDL_GAMEPAD_BUTTON_EAST);
        pad2_ok = pad2_ok && a.flow.state() == S::Conflict && a.modal;
        pad(SDL_GAMEPAD_BUTTON_EAST);
        flow_poll(a);
        pad2_ok = pad2_ok && !a.flow.active() && !a.modal;
        const bool same = a.controls == before;
        const bool ok = keys_ok && pad_ok && pad2_ok && same;
        if (!ok) ++modal_fails;
        std::printf("b3launcher: walk rebind flow %s (keys: Enter waits, Esc cancels %s; pad: A waits, B cancels %s; a controller's: A waits, "
                    "B taken (a conflict), B closes it %s; controls %s)\n",
                    ok ? "PASS" : "FAIL", keys_ok ? "PASS" : "FAIL", pad_ok ? "PASS" : "FAIL", pad2_ok ? "PASS" : "FAIL", same ? "unchanged" : "CHANGED");
        a.flow.cancel();
        a.modal.reset();
        a.controls = before;
        a.ctl_device = 0;
    }

    // rebind2: the shortcuts (the window's keys, photo mode's controls): any key can be taken (a modifier too), a taken one
    // asks (Swap / Clear / Keep), Delete puts one back, the list resets, Reset all asks first, and a controller's list
    // takes a chord's inputs one by one
    {
        const in::Controls before = a.controls;
        a.controls = in::default_controls();
        a.flow.cancel();
        a.modal.reset();
        go_page(a, kPads);
        a.ctl_player = 2;
        a.ctl_device = 0;
        render(a);
        const auto sendkey = [&](SDL_Scancode sc) {
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.scancode = sc;
            e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
            e.key.down = true;
            e.key.windowID = wid;
            event(a, e);
        };
        using S = in::RebindFlow::State;
        using PA = in::PortAction;
        const std::size_t shot = std::size_t(PA::Screenshot), fs_ = std::size_t(PA::Fullscreen);
        const int rows = a.ctl_first_widget >= 0 ? int(a.widgets.size()) - a.ctl_first_widget : 0;
        a.in_content = true;
        a.focus = a.ctl_first_widget + int(shot);
        settle();
        key(SDLK_RETURN);
        const bool waits = a.flow.state() == S::Waiting && a.flow.is_port() && a.flow.port_action() == PA::Screenshot && a.modal;
        sendkey(SDL_SCANCODE_LCTRL);   // a modifier key is a key too (nothing is reserved but Escape)
        const bool took = !a.flow.active() && a.controls.port.keys[shot] == std::vector<std::uint16_t>{224};
        a.focus = a.ctl_first_widget + int(fs_);
        key(SDLK_RETURN);
        sendkey(SDL_SCANCODE_LCTRL);   // Screenshot has it: asks
        const bool asks = a.flow.state() == S::Conflict && a.modal && a.flow.conflicts().size() == 1 && a.flow.conflicts()[0].port;
        key(SDLK_ESCAPE);
        flow_poll(a);
        const bool kept = !a.flow.active() && !a.modal && a.controls.port.keys[shot] == std::vector<std::uint16_t>{224} &&
                          a.controls.port.keys[fs_] == in::default_port().keys[fs_];
        a.focus = a.ctl_first_widget + int(shot);
        key(SDLK_DELETE);   // this one back to its default
        const bool reset_one = a.controls == in::default_controls();
        a.focus = a.ctl_first_widget + int(shot);
        key(SDLK_RETURN);
        sendkey(SDL_SCANCODE_ESCAPE);   // Escape cancels the capture: it is never taken
        const bool esc = !a.flow.active() && !a.modal && a.controls == in::default_controls();
        // a controller's list: the chord's inputs one by one (the first replaces, Right adds)
        a.ctl_device = 1;
        render(a);
        a.focus = a.ctl_first_widget + int(PA::Restart);
        settle();
        pad(SDL_GAMEPAD_BUTTON_SOUTH);
        const bool pad_waits = a.flow.state() == S::Waiting && a.flow.is_port() && a.flow.device() == in::Device::Gamepad;
        pad(SDL_GAMEPAD_BUTTON_EAST);   // taken: Circle has it (a conflict)
        const bool pad_asks = a.flow.state() == S::Conflict && a.modal;
        pad(SDL_GAMEPAD_BUTTON_EAST);
        flow_poll(a);
        const bool pad_back = !a.flow.active() && !a.modal && a.controls == in::default_controls();
        // Reset all: asks first (Cancel keeps), then does it
        a.ctl_device = 0;
        a.controls.port.keys[shot] = {99};
        a.controls.player[1].keys[std::size_t(in::Ds2::Cross)] = {{20, in::KeyCtx::Any}};
        render(a);
        const std::size_t before_modals = a.modal ? 1 : 0;
        std::function<void()> reset_all;
        for (std::size_t k = a.widgets.size(); k-- > 0 && !reset_all;)
            if (a.widgets[k].desc.rfind("Everything back to the original layout", 0) == 0) reset_all = a.widgets[k].activate;
        bool all_ok = bool(reset_all);
        if (reset_all) {
            reset_all();
            all_ok = all_ok && a.modal && before_modals == 0 && !(a.controls == in::default_controls());   // asked, nothing changed yet
            key(SDLK_ESCAPE);
            all_ok = all_ok && !a.modal && !(a.controls == in::default_controls());
            reset_all();
            if (a.modal) a.modal->buttons[0].fn();
            all_ok = all_ok && a.controls == in::default_controls();
        }
        const bool ok = waits && took && asks && kept && reset_one && esc && pad_waits && pad_asks && pad_back && all_ok && rows >= 30;
        if (!ok) ++modal_fails;
        std::printf("b3launcher: walk shortcuts %s (Enter waits %s, a modifier key taken %s, a taken key asks %s, Esc keeps %s, Delete resets one %s, "
                    "Esc is never taken %s; controller: A waits %s, a taken button asks %s, B cancels %s; Reset all asks first %s; %d rows)\n",
                    ok ? "PASS" : "FAIL", waits ? "PASS" : "FAIL", took ? "PASS" : "FAIL", asks ? "PASS" : "FAIL", kept ? "PASS" : "FAIL",
                    reset_one ? "PASS" : "FAIL", esc ? "PASS" : "FAIL", pad_waits ? "PASS" : "FAIL", pad_asks ? "PASS" : "FAIL", pad_back ? "PASS" : "FAIL",
                    all_ok ? "PASS" : "FAIL", rows);
        a.flow.cancel();
        a.modal.reset();
        a.controls = before;
        a.ctl_player = 0;
        a.ctl_device = 0;
    }

    // the test data away
    fs::remove_all(mod_a, ec);
    fs::remove_all(mod_b, ec);
    fs::remove(rep1, ec);
    fs::remove(rep2, ec);
    if (!had_mods_dir) fs::remove_all(mods_dir, ec);
    if (!had_rep_dir) fs::remove_all(rep_dir, ec);
    if (had_state) std::ofstream(state, std::ios::binary) << state_before;
    else fs::remove(state, ec);
    a.modal.reset();
    a.mouse_on = false;
    go_page(a, kPlay);
    a.in_content = true;
    fails = page_fails + modal_fails;
    std::printf("b3launcher: walk check %s (%zu page states, %d failed; %zu dialogs and the rebind flow, %d failed)\n", fails ? "FAIL" : "PASS",
                pages.size(), page_fails, modals.size(), modal_fails);
    {   // the controller button test stops by a pad alone: Start + Back together, and B / Circle held (kPadTestHold)
        go_page(a, kPads);
        a.in_content = true;
        a.pad_test = true;
        a.east_held_since = -1;
        SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_START, true);
        pump();
        poll_pad_nav(a);
        const bool start_alone_kept = a.pad_test;   // Start alone is a button to test, not a stop
        SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_BACK, true);
        pump();
        poll_pad_nav(a);
        const bool combo_stopped = !a.pad_test;
        SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_START, false);
        SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_BACK, false);
        pump();
        a.pad_test = true;
        a.east_held_since = -1;
        SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_EAST, true);
        pump();
        poll_pad_nav(a);
        const bool tap_kept = a.pad_test;   // a short press is tested, not a stop
        render(a);   // (the hold's progress bar drawn)
        if (a.east_held_since >= 0) a.east_held_since -= kPadTestHold + 0.05;   // as if held that long
        poll_pad_nav(a);
        const bool hold_stopped = !a.pad_test;
        SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_EAST, false);
        pump();
        poll_pad_nav(a);
        const bool ok = start_alone_kept && combo_stopped && tap_kept && hold_stopped;
        std::printf("b3launcher: button test stop check %s (Start alone kept it %s; Start + Back stopped it %s; a B tap kept it %s; B held %.1f s stopped it %s)\n",
                    ok ? "PASS" : "FAIL", start_alone_kept ? "PASS" : "FAIL", combo_stopped ? "PASS" : "FAIL", tap_kept ? "PASS" : "FAIL",
                    kPadTestHold, hold_stopped ? "PASS" : "FAIL");
        if (!ok) ++fails;
        a.pad_test = false;
        go_page(a, kPlay);
        a.in_content = true;
    }
    return fails;
}

// ---- 1.1: the save folder of the player's choice and the PCSX2 import (--screenshot) ----
// In scratch folders under the check's data folder (--pref), removed afterwards: a chosen folder with a save, then an
// empty one chosen (the copy question, "Copy and use it": the save copied, launcher.ini's saves_dir as native_play reads
// it), back to a folder with saves (no question, nothing copied), back to the default (no saves_dir line). Then the
// import with a stand-in PCSX2 memcards folder (Mcd001.ps2): the question names it, the folder list starts there with
// the card highlighted. Shots: saves_copy_offer.png, saves_chosen.png, import_confirm.png, import_pcsx2.png in `shots`.
// The launcher.ini choice is put back. Returns the number of failures.
int saves_folder_checks(App& a, const fs::path& shots) {
    int bad = 0;
    auto report = [&](bool ok, const std::string& what) {
        std::printf("b3launcher: save folder check %s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
        if (!ok) ++bad;
    };
    auto shot = [&](const char* name) {
        render(a);
        render(a);
        const fs::path to = shots / name;
        if (save_shot(a, to)) std::printf("b3launcher: %s\n", utf8_of(to).c_str());
    };
    std::error_code ec;
    const fs::path base = a.pref / "savefolder-check";
    fs::remove_all(base, ec);
    const fs::path first = base / "first", cloud = base / "cloud";
    fs::create_directories(first, ec);
    fs::create_directories(cloud, ec);
    std::string err;
    const b3native::save::Directories none;
    report(b3native::save::write_sav_file(b3native::save::profile_file(first, 2), none, &err) &&
               b3native::save::write_active_slot(first, 2, &err),
           "a stand-in save " + err);
    const std::string kept = a.cfg.saves_dir;
    a.modal.reset();
    go_page(a, kSaves);
    a.in_content = true;
    a.focus = 0;
    use_saves_folder(a, first, false);
    report(saves_root(a) == first && b3native::save::chosen_saves_root(a.pref) == first, "a chosen folder in use and in launcher.ini");
    // an empty folder chosen: asked first
    choose_saves_folder(a, cloud);
    report(a.modal && a.modal->title == "Copy your saves there?" && a.modal->buttons.size() == 3, "the copy question for an empty folder");
    shot("saves_copy_offer.png");
    if (a.modal && !a.modal->buttons.empty()) {
        auto fn = a.modal->buttons[0].fn;   // Copy and use it
        a.modal.reset();
        if (fn) fn();
    }
    report(fs::is_regular_file(b3native::save::profile_file(cloud, 2), ec) && b3native::save::read_active_slot(cloud) == 2 &&
               fs::is_regular_file(b3native::save::profile_file(first, 2), ec),
           "the save copied (the old folder keeps it)");
    report(saves_root(a) == cloud && b3native::save::chosen_saves_root(a.pref) == cloud && !a.save_note_bad,
           "the new folder in use (launcher.ini: " + utf8_of(b3native::save::chosen_saves_root(a.pref)) + "): " + a.save_note);
    a.focus = 1;
    shot("saves_chosen.png");
    // a folder with saves: no question
    choose_saves_folder(a, first);
    report(!a.modal && saves_root(a) == first, "a folder with saves: used at once");
    // the default folder: no saves_dir line in launcher.ini
    a.modal.reset();
    use_saves_folder(a, saves_folder(a.pref), false);
    std::string ini;
    {
        std::ifstream f(config_file(a.pref), std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        ini = ss.str();
    }
    report(a.cfg.saves_dir.empty() && ini.find("saves_dir") == std::string::npos && b3native::save::chosen_saves_root(a.pref).empty(),
           "back to the default folder (no saves_dir in launcher.ini)");
    // the import: a stand-in PCSX2 memcards folder
    b3native::save::Pcsx2Homes h;
    h.home = base / "home";
    const fs::path cards = b3native::save::pcsx2_card_folders(h, false).front().path;
    fs::create_directories(cards, ec);
    { std::ofstream(cards / "Mcd001.ps2", std::ios::binary) << "stand-in"; }
    { std::ofstream(cards / "Mcd002.ps2", std::ios::binary) << "stand-in"; }
    a.pcsx2_homes = h;
    confirm_import(a, false);
    report(a.modal && a.modal->text.find("Mcd001.ps2") != std::string::npos && a.modal->text.find(utf8_of(cards)) != std::string::npos,
           "the import question names Mcd001.ps2 and the folder found");
    shot("import_confirm.png");
    a.modal.reset();
    open_browser(a, DialogKind::kImportFile, "");
    const Browser& b = *a.browser;
    const bool sel_card = b.sel < int(b.list.entries.size()) && b.list.entries[std::size_t(b.sel)].path.filename() == "Mcd001.ps2";
    report(b.dir == cards && sel_card && !b.places.empty() && b.places[0].label.rfind("PCSX2 memory cards", 0) == 0 &&
               b.status.find("Mcd001.ps2") != std::string::npos,
           "the import's folder list starts in PCSX2's memcards with Mcd001.ps2 highlighted");
    shot("import_pcsx2.png");
    // put back
    a.browser.reset();
    a.modal.reset();
    a.pcsx2_homes.reset();
    if (a.cfg.saves_dir != kept) {
        a.cfg.saves_dir = kept;
        save_config(config_file(a.pref), a.cfg);
    }
    a.save_note.clear();
    a.save_note_bad = false;
    fs::remove_all(base, ec);
    refresh(a);
    std::printf("b3launcher: save folder check %s\n", bad ? "FAIL" : "PASS");
    return bad;
}

// ---- osk1: the on-screen keyboard and the folder list by a pad alone (--screenshot) ----
// A virtual pad (as in the walk: SDL events and the launcher's own pad polling) types a path into the Music page's
// "Type a path..." on the launcher's on-screen keyboard and confirms it with Start, then picks a folder (HD Textures >
// Choose folder...) and a disc image (Game Data > Choose disc image...) in the launcher's folder list. Scratch folders
// under the check's data folder (--pref), removed afterwards; the settings and the game data folder are put back.
// Shots: osk_keyboard.png, osk_symbols.png, browse.png, browse_places.png in `shots`. Returns the number of failures.
int osk_checks(App& a, SDL_JoystickID& vj, const fs::path& shots) {
    const SDL_WindowID wid = SDL_GetWindowID(a.win);
    auto pump = [&]() {
        SDL_UpdateJoysticks();
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type >= SDL_EVENT_GAMEPAD_AXIS_MOTION && e.type < SDL_EVENT_FINGER_DOWN) event(a, e);
    };
    if (!vj) vj = demo_pad();
    pump();
    SDL_Joystick* joy = SDL_GetJoystickFromID(vj);
    if (!joy) joy = SDL_OpenJoystick(vj);
    if (!joy) {
        std::printf("b3launcher: osk check FAIL (no virtual pad: %s)\n", SDL_GetError());
        return 2;
    }
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) SDL_SetJoystickVirtualButton(joy, b, false);
    for (int x = 0; x < SDL_GAMEPAD_AXIS_COUNT; ++x) SDL_SetJoystickVirtualAxis(joy, x, 0);
    pump();
    a.pad_test = false;
    poll_pad_nav(a);
    auto tap = [&](SDL_GamepadButton b) {
        SDL_SetJoystickVirtualButton(joy, b, true);
        pump();
        poll_pad_nav(a);
        SDL_SetJoystickVirtualButton(joy, b, false);
        pump();
        poll_pad_nav(a);
        render(a);
    };
    auto shot = [&](const char* name) {
        render(a);
        render(a);
        const fs::path to = shots / name;
        if (save_shot(a, to)) std::printf("b3launcher: %s\n", utf8_of(to).c_str());
    };
    auto focus_widget = [&](const std::string& desc_start) {
        render(a);
        for (std::size_t k = 0; k < a.widgets.size(); ++k)
            if (a.widgets[k].desc.rfind(desc_start, 0) == 0) {
                a.in_content = true;
                a.focus = int(k);
                a.reveal_focus = true;
                render(a);
                return true;
            }
        return false;
    };
    // the D-pad's shortest way to key `to` (the keyboard's own moves)
    auto dpad_to = [&](int to) {
        if (!a.modal || to < 0) return false;
        const std::vector<OskKey> keys = a.modal->keys;
        const int from = a.modal->osk_st.focus;
        const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
        const SDL_GamepadButton btn[4] = {SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
        std::vector<int> prev(keys.size(), -1), how(keys.size(), -1), q{from};
        prev[std::size_t(from)] = from;
        for (std::size_t i = 0; i < q.size(); ++i)
            for (int d = 0; d < 4; ++d) {
                const int n = osk_move(keys, q[i], dx[d], dy[d]);
                if (n >= 0 && prev[std::size_t(n)] < 0) {
                    prev[std::size_t(n)] = q[i];
                    how[std::size_t(n)] = d;
                    q.push_back(n);
                }
            }
        if (prev[std::size_t(to)] < 0) return false;
        std::vector<int> path;
        for (int k = to; k != from; k = prev[std::size_t(k)]) path.push_back(how[std::size_t(k)]);
        for (auto it = path.rbegin(); it != path.rend() && a.modal; ++it) tap(btn[*it]);
        return a.modal && a.modal->osk_st.focus == to;
    };
    // `s` typed with the pad: Y until the key's layer shows, the D-pad to the key, A (a space: X)
    int taps_typed = 0;
    auto type = [&](const std::string& s, std::string& why) {
        for (std::size_t i = 0; i < s.size(); i = utf8_next(s, i)) {
            if (!a.modal || !a.modal->osk) {
                why = "the keyboard closed";
                return false;
            }
            const std::string ch = s.substr(i, utf8_next(s, i) - i);
            if (ch == " ") {
                tap(SDL_GAMEPAD_BUTTON_WEST);
                ++taps_typed;
                continue;
            }
            int key = -1;
            OskLayer layer{};
            if (!osk_find_char(a.modal->keys, ch, key, layer)) {
                why = "no key types \"" + ch + "\"";
                return false;
            }
            if (a.modal->keys[std::size_t(key)].chars[std::size_t(a.modal->osk_st.layer)] != ch)   // (on this layer too: typed as it is)
                for (int k = 0; k < 3 && a.modal->osk_st.layer != layer; ++k) tap(SDL_GAMEPAD_BUTTON_NORTH);
            if (!dpad_to(key)) {
                why = "the D-pad did not reach \"" + ch + "\"";
                return false;
            }
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
            ++taps_typed;
        }
        return true;
    };
    int fails = 0;
    std::error_code ec;
    const fs::path root = a.pref / "osk-check";
    fs::remove_all(root, ec);

    // 1. the on-screen keyboard: Music > Type a path...
    {
        const fs::path music_dir = root / "Music for B3";
        fs::create_directories(music_dir, ec);
        const std::string target = utf8_of(music_dir), old_music = a.settings.music_folder;
        a.modal.reset();
        a.browser.reset();
        go_page(a, kMusic);
        const bool found = focus_widget("Type or paste");
        tap(SDL_GAMEPAD_BUTTON_SOUTH);
        const bool opened = found && a.modal && a.modal->input && a.modal->osk;
        bool clear_ok = false, typed = false, cursor_ok = false, erase_ok = false, mouse_ok = false, sym_ok = false, full = false;
        std::string why;
        if (opened) {
            clear_ok = dpad_to(osk_find(a.modal->keys, OskKind::kClear));
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
            clear_ok = clear_ok && a.modal && a.modal->field.empty();
            const std::string head = target.substr(0, utf8_prev(target, target.size()));   // all but the last character
            typed = type(head, why) && a.modal && a.modal->field.text == head;
            if (typed) shot("osk_keyboard.png");
            if (typed) {   // LB / RB: the cursor one character back and forth
                const std::size_t end = a.modal->field.cursor;
                tap(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
                const std::size_t c1 = a.modal ? a.modal->field.cursor : 0;
                tap(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
                const std::size_t c2 = a.modal ? a.modal->field.cursor : 0;
                cursor_ok = c1 == utf8_prev(head, end) && c2 == end;
                // B: the last character deleted; a mouse click on its key types it again
                tap(SDL_GAMEPAD_BUTTON_EAST);
                const std::size_t cut = utf8_prev(head, head.size());
                erase_ok = a.modal && a.modal->field.text == head.substr(0, cut);
                const std::string ch = head.substr(cut);
                int key = -1;
                OskLayer layer{};
                if (a.modal && osk_find_char(a.modal->keys, ch, key, layer)) {
                    if (a.modal->keys[std::size_t(key)].chars[std::size_t(a.modal->osk_st.layer)] != ch)
                        for (int k = 0; k < 3 && a.modal->osk_st.layer != layer; ++k) tap(SDL_GAMEPAD_BUTTON_NORTH);
                    render(a);
                    const SDL_FRect r = a.modal->key_rects[std::size_t(key)];
                    float wx = 0, wy = 0;
                    SDL_RenderCoordinatesToWindow(a.ren, r.x + r.w * 0.5f, r.y + r.h * 0.5f, &wx, &wy);
                    for (SDL_EventType t : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
                        SDL_Event e{};
                        e.type = t;
                        e.button.windowID = wid;
                        e.button.button = SDL_BUTTON_LEFT;
                        e.button.down = t == SDL_EVENT_MOUSE_BUTTON_DOWN;
                        e.button.x = wx;
                        e.button.y = wy;
                        event(a, e);
                    }
                    a.mouse_on = false;
                    mouse_ok = a.modal && a.modal->field.text == head;
                }
                // Y twice: the symbols (a shot), once more: lower case
                tap(SDL_GAMEPAD_BUTTON_NORTH);
                if (a.modal && a.modal->osk_st.layer != OskLayer::kSymbols) tap(SDL_GAMEPAD_BUTTON_NORTH);
                sym_ok = a.modal && a.modal->osk_st.layer == OskLayer::kSymbols;
                if (sym_ok) shot("osk_symbols.png");
                tap(SDL_GAMEPAD_BUTTON_NORTH);
                typed = type(target.substr(head.size()), why);
                full = a.modal && a.modal->field.text == target;
            }
            tap(SDL_GAMEPAD_BUTTON_START);
        }
        const bool chosen = !a.modal && a.settings.music_folder == target;
        const bool ok = opened && clear_ok && typed && cursor_ok && erase_ok && mouse_ok && sym_ok && full && chosen;
        if (!ok) ++fails;
        std::printf("b3launcher: osk check %s (a pad alone on Music > Type a path...: A %s the on-screen keyboard; Clear %s; %d characters "
                    "typed with the D-pad and A, Y for capitals, X for spaces %s%s%s; LB / RB moved the cursor %s; B deleted one %s; a mouse "
                    "click on its key typed it again %s; Y showed the symbols %s; Start %s \"%s\" as the music folder; the system's own "
                    "keyboard: %s)\n",
                    ok ? "PASS" : "FAIL", opened ? "opened" : "did NOT open", clear_ok ? "emptied the field" : "FAILED", taps_typed,
                    typed && full ? "PASS" : "FAIL", why.empty() ? "" : ": ", why.c_str(), cursor_ok ? "PASS" : "FAIL", erase_ok ? "PASS" : "FAIL",
                    mouse_ok ? "PASS" : "FAIL", sym_ok ? "PASS" : "FAIL", chosen ? "chose" : "did NOT choose", target.c_str(),
                    system_keyboard() ? "there (shown with the launcher's)" : "none here (the launcher's alone)");
        a.modal.reset();
        stop_text_input(a);
        a.settings.music_folder = old_music;
        save_settings(a);
        refresh_music(a, true);
        a.music_note.clear();
    }

    // 2. the folder list: HD Textures > Choose folder..., Game Data > Choose disc image...
    {
        const fs::path broot = root / "browse";
        for (const char* d : {"Alpha", "Beta/Inner", "Gamma"}) fs::create_directories(broot / d, ec);
        std::ofstream(broot / "Beta" / "tex.png", std::ios::binary) << "not an image";
        std::ofstream(broot / "disc.iso", std::ios::binary) << std::string(4096, '\0');
        std::ofstream(broot / "disc.chd", std::ios::binary) << std::string(4096, '\0');   // chd1: listed beside it
        std::ofstream(broot / "notes.txt", std::ios::binary) << "not listed";
        auto same = [](const fs::path& p, const fs::path& q) {
            std::error_code e;
            return fs::equivalent(p, q, e);
        };
        auto sel_name = [&]() {
            const Browser& b = *a.browser;
            return b.sel < int(b.list.entries.size()) ? b.list.entries[std::size_t(b.sel)].name : std::string();
        };
        const std::string old_hd = a.settings.hd_pack_dir;
        a.settings.hd_pack_dir = utf8_of(broot / "Alpha");   // the current choice: the list starts there
        a.modal.reset();
        a.browser.reset();
        go_page(a, kHd);
        const bool found = focus_widget("The folder of a PCSX2 texture pack");
        tap(SDL_GAMEPAD_BUTTON_SOUTH);
        const bool opened = found && a.browser && same(a.browser->dir, broot / "Alpha");
        bool up_ok = false, down_ok = false, into_ok = false, places_ok = false;
        std::size_t places = 0;
        std::string place_names;
        if (opened) {
            tap(SDL_GAMEPAD_BUTTON_EAST);
            up_ok = a.browser && same(a.browser->dir, broot) && sel_name() == "Alpha";
            tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            down_ok = a.browser && sel_name() == "Beta";
            shot("browse.png");
            tap(SDL_GAMEPAD_BUTTON_SOUTH);
            into_ok = a.browser && same(a.browser->dir, broot / "Beta") && a.browser->list.entries.size() == 2 &&
                      a.browser->list.entries[0].name == "Inner" && a.browser->list.entries[0].folder && a.browser->list.entries[1].name == "tex.png";
            tap(SDL_GAMEPAD_BUTTON_NORTH);
            if (a.browser) {
                places = a.browser->places.size();
                for (std::size_t k = 0; k < places && k < 4; ++k) place_names += (k ? ", " : "") + a.browser->places[k].label;
                places_ok = a.browser->column == 0 && places > 0;
            }
            shot("browse_places.png");
            tap(SDL_GAMEPAD_BUTTON_NORTH);
            places_ok = places_ok && a.browser && a.browser->column == 1;
            tap(SDL_GAMEPAD_BUTTON_START);
        }
        const bool chosen = !a.browser && same(path_from_utf8(a.settings.hd_pack_dir), broot / "Beta");
        // the disc image: the list starts in the current image's folder with it highlighted; A chooses it
        const fs::path old_game = a.game_dir;
        const Config old_cfg = a.cfg;
        const bool had_cfg = fs::exists(config_file(a.pref), ec);
        a.game_dir = broot / "disc.iso";
        go_page(a, kGame);
        const bool found2 = focus_widget("Your disc's image file");
        tap(SDL_GAMEPAD_BUTTON_SOUTH);
        const bool img_open = found2 && a.browser && same(a.browser->dir, broot) && sel_name() == "disc.iso";
        const bool chd_listed = img_open && std::any_of(a.browser->list.entries.begin(), a.browser->list.entries.end(),
                                                        [](const BrowseEntry& e) { return e.name == "disc.chd" && !e.folder; });   // chd1
        if (img_open) tap(SDL_GAMEPAD_BUTTON_SOUTH);
        const bool img_ok = img_open && !a.browser && same(a.game_dir, broot / "disc.iso");
        a.browser.reset();
        a.game_dir = old_game;
        a.cfg = old_cfg;
        if (had_cfg) save_config(config_file(a.pref), a.cfg);
        else fs::remove(config_file(a.pref), ec);
        a.game = check_game_folder(a.game_dir);
        a.game_note.clear();
        a.settings.hd_pack_dir = old_hd;
        save_settings(a);
        refresh_hd(a, true);
        a.hd_note.clear();
        const bool ok = opened && up_ok && down_ok && into_ok && places_ok && chosen && img_ok && chd_listed;
        if (!ok) ++fails;
        std::printf("b3launcher: browse check %s (a pad alone on HD Textures > Choose folder...: A %s the launcher's folder list in Alpha; "
                    "B up a folder, Alpha highlighted %s; Down to Beta %s; A into Beta (Inner, tex.png) %s; Y the places and back %s (%zu: %s%s); "
                    "Start %s Beta; Game Data > Choose disc image...: A opened it on disc.iso %s, disc.chd listed %s, A chose it %s)\n",
                    ok ? "PASS" : "FAIL", opened ? "opened" : "did NOT open", up_ok ? "PASS" : "FAIL", down_ok ? "PASS" : "FAIL",
                    into_ok ? "PASS" : "FAIL", places_ok ? "PASS" : "FAIL", places, place_names.c_str(), places > 4 ? ", ..." : "",
                    chosen ? "chose" : "did NOT choose", img_open ? "PASS" : "FAIL", chd_listed ? "PASS" : "FAIL", img_ok ? "PASS" : "FAIL");
    }
    fs::remove_all(root, ec);
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) SDL_SetJoystickVirtualButton(joy, b, false);
    pump();
    a.modal.reset();
    a.browser.reset();
    a.pad_used_last = false;
    a.mouse_on = false;
    go_page(a, kPlay);
    return fails;
}

// ---- update1: the update check with canned answers (--screenshot; never a request) ----
// Each case writes GitHub's answer into a file in DIR and points the check at it (the B3LAUNCHER_UPDATE_FILE hook):
// the same thread, file reading, parsing, launcher.ini and pages as a real answer. A newer release (the Play page's
// banner, the Credits line and Download), the running build's own release (no banner, "Up to date"), no release yet
// (404), no answer (an error: no banner, "Couldn't check"), a check run without a file (no request), and the check
// turned off (nothing started). The launcher.ini of the check's data folder is put back afterwards.
int update_checks(App& a, const fs::path& dir) {
    int fails = 0;
    const update::Cache keep = a.cfg.update;
    const std::string keep_file = a.upd.file;
    const std::optional<update::Version> run = update::parse_version(b3port::version());
    const update::Version base = run ? update::as_running(*run) : update::Version{};
    auto tag_of = [](const update::Version& v) {   // with the pre-release part ("v1.0.0-rc"): an -rc build's own tag
        std::string t = "v" + std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
        for (std::size_t i = 0; i < v.pre.size(); ++i) t += (i ? "." : "-") + v.pre[i];
        return t;
    };
    update::Version next = base;
    ++next.minor;
    next.patch = 0;
    next.pre.clear();
    auto release_json = [](const std::string& tag, const std::string& name) {
        return "{\"html_url\": \"https://github.com/u0rhs/burnout3-native/releases/tag/" + tag + "\", \"tag_name\": \"" + tag +
               "\", \"name\": \"" + name + "\", \"draft\": false, \"prerelease\": false, \"published_at\": \"2026-12-01T18:00:00Z\", "
               "\"assets\": [], \"body\": \"## What's new\\r\\n\\r\\n- **Split screen** for two players\\r\\n- Faster loading\\r\\n\"}";
    };
    auto shoot = [&](int page, const char* name, std::function<void()> setup = nullptr) {
        go_page(a, page);
        a.in_content = true;
        a.focus = 0;
        a.scroll[page] = 0;
        render(a);
        if (setup) setup();
        SDL_Event e;
        while (SDL_PollEvent(&e)) event(a, e);
        for (int k = 0; k < 3; ++k) render(a);
        const fs::path to = dir / (std::string(name) + ".png");
        if (!save_shot(a, to)) {
            std::printf("b3launcher: update check FAIL (cannot write %s)\n", utf8_of(to).c_str());
            ++fails;
        } else {
            std::printf("b3launcher: %s\n", utf8_of(to).c_str());
        }
    };
    auto has_widget = [&](int page, const std::string& part) {
        go_page(a, page);
        a.in_content = true;
        render(a);
        for (const Widget& w : a.widgets)
            if (w.desc.find(part) != std::string::npos) return true;
        return false;
    };
    auto wait_answer = [&]() {
        bool came = false;
        for (int k = 0; k < 500 && !came; ++k) {
            came = poll_update(a);
            if (!came) SDL_Delay(5);
        }
        return came;
    };
    // One check through the thread and the file
    auto run_case = [&](const std::string& file_text, bool write_file, const std::string& file_name) {
        a.cfg.update = update::Cache{};
        a.upd.file = utf8_of(dir / file_name);
        std::error_code ec;
        fs::remove(dir / file_name, ec);
        if (write_file) {
            std::ofstream f(dir / file_name, std::ios::binary);
            f << file_text;
        }
        start_update_check(a);
        return wait_answer();
    };
    auto result = [&](const char* what, bool ok, const std::string& detail) {
        std::printf("b3launcher: update check \"%s\" %s (%s)\n", what, ok ? "PASS" : "FAIL", detail.c_str());
        if (!ok) ++fails;
    };
    const std::string url_part = "github.com/u0rhs/burnout3-native/releases/tag/";

    {   // a newer release: the banner, its Download, launcher.ini
        const std::string tag = tag_of(next);
        const bool came = run_case(release_json(tag, "Takedown at 60"), true, "update_newer.json");
        Config back;
        load_config(config_file(a.pref), back);
        const update::State st = update_state(a);
        const bool banner = has_widget(kPlay, url_part + tag);
        const bool credits = has_widget(kCredits, url_part + tag);
        const bool saved = back.update.latest.tag == tag && back.update.checked_at > 0 && back.update.error.empty();
        const bool ok = came && st == update::State::kAvailable && banner && credits && saved && a.cfg.update.latest.notes.size() == 3;
        result("newer release", ok, std::string("running ") + b3port::version() + ", latest " + tag + ": " +
                                        update::status_text(a.cfg.update, st) + "; Play banner " + (banner ? "shown" : "MISSING") +
                                        ", Credits Download " + (credits ? "shown" : "MISSING") + ", launcher.ini " + (saved ? "saved" : "NOT saved"));
        shoot(kPlay, "update_available");
        shoot(kCredits, "update_available_credits");
    }
    {   // the running build's own release: nothing to say but "Up to date"
        const std::string tag = tag_of(base);
        const bool came = run_case(release_json(tag, "First release"), true, "update_current.json");
        const update::State st = update_state(a);
        const bool banner = has_widget(kPlay, url_part);
        const bool ok = came && st == update::State::kUpToDate && !banner;
        result("no newer release", ok, "latest " + tag + ": " + update::status_text(a.cfg.update, st) + "; Play banner " + (banner ? "SHOWN" : "none"));
        shoot(kCredits, "update_current_credits");
    }
    {   // no release yet (GitHub's 404)
        const bool came = run_case("HTTP/1.1 404 Not Found\n\n{\"message\": \"Not Found\"}", true, "update_404.txt");
        const update::State st = update_state(a);
        result("no release yet", came && st == update::State::kNoRelease && !has_widget(kPlay, url_part), update::status_text(a.cfg.update, st));
    }
    {   // no answer: the error, quietly
        const bool came = run_case({}, false, "update_missing.json");
        const update::State st = update_state(a);
        const bool banner = has_widget(kPlay, url_part);
        result("error", came && st == update::State::kError && !banner, update::status_text(a.cfg.update, st) + "; Play banner " + (banner ? "SHOWN" : "none"));
        shoot(kCredits, "update_error_credits");
        shoot(kSettings, "update_settings", [&]() {   // the Launcher rows at the end of the page, Check now focused
            a.settings_tab = 0;
            render(a);
            for (std::size_t k = 0; k < a.widgets.size(); ++k)
                if (a.widgets[k].desc.rfind("Asks GitHub", 0) == 0) a.focus = int(k);
            a.reveal_focus = true;
        });
    }
    {   // a check run without an answer file makes no request
        a.cfg.update = update::Cache{};
        a.upd.file.clear();
        start_update_check(a);
        const bool came = wait_answer();
        result("no request in a check run", came && a.cfg.update.error.find("no requests") != std::string::npos,
               update::status_text(a.cfg.update, update_state(a)));
    }
    {   // off: CHECK FOR UPDATES toggled off by its row on the Settings page, then nothing starts
        a.cfg.update = update::Cache{};
        a.upd.file = utf8_of(dir / "update_newer.json");
        go_page(a, kSettings);
        a.settings_tab = 0;
        a.in_content = true;
        render(a);
        bool toggled = false;
        for (Widget& w : a.widgets)
            if (w.desc.rfind("Each time the launcher starts", 0) == 0 && w.activate) {
                const auto fn = w.activate;
                fn();
                toggled = true;
                break;
            }
        Config back;
        load_config(config_file(a.pref), back);
        start_update_check(a);
        const bool started = update_checking(a);
        const update::State st = update_state(a);
        result("off", toggled && !a.cfg.update.enabled && !back.update.enabled && !started && st == update::State::kOff &&
                          !update::due(a.cfg.update, unix_now()),
               std::string("the row ") + (toggled ? "turned it off" : "NOT found") + ", launcher.ini update_check = " +
                   (back.update.enabled ? "1" : "0") + ", a check " + (started ? "STARTED" : "not started") + ": " +
                   update::status_text(a.cfg.update, st));
    }
    a.cfg.update = keep;
    a.upd.file = keep_file;
    save_config(config_file(a.pref), a.cfg);
    std::printf("b3launcher: update checks %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}

// ---- issue #8: ADD TO STEAM with a made-up Steam folder (--screenshot; this computer's Steam is never read or written) ----

struct SteamCheck {
    fs::path base, root;   // the check's folder; the made-up Steam in it
    steam::Bytes other;    // account 1001's list before: one other game
};

// A made-up home with Steam where this system looks for it: account 1001 with a list holding another game, account
// 1002 without a config folder yet. The launcher's Steam actions then use it (steam_ui.places), Steam "closed".
SteamCheck steam_check_setup(App& a, const fs::path& dir) {
    SteamCheck c;
    std::error_code ec;
    c.base = dir / "steam_check";
    fs::remove_all(c.base, ec);
    steam::Places p;
    p.home = c.base / "home";
    p.registry = {c.base / "home" / "Steam"};   // Windows: the registry's SteamPath, made up
    c.root = steam::candidate_roots(p, steam::this_os()).front();
    fs::create_directories(c.root / "userdata" / "1001" / "config", ec);
    fs::create_directories(c.root / "userdata" / "1002", ec);
    steam::NewShortcut other;
    other.name = "Some Other Game";
    other.exe = utf8_of(c.base / "other" / "other-game");
    other.start_dir = utf8_of(c.base / "other");
    c.other = steam::with_entry(steam::parse_shortcuts({}), other);
    steam::write_atomic(c.root / "userdata" / "1001" / "config" / "shortcuts.vdf", c.other, nullptr);
    a.steam_ui.places = p;
    a.steam_ui.running = false;
    return c;
}

// What one account has: our entry (last), its pictures' sizes. Returns "" when right, else what is wrong.
std::string steam_check_account(App& a, const SteamCheck& c, const std::string& id, std::uint32_t& appid, const fs::path& save_to = {}) {
    const fs::path cfg = steam::find_roots(*a.steam_ui.places, steam::this_os()).front() / "userdata" / id / "config";
    steam::Bytes b;
    steam::read_file(cfg / "shortcuts.vdf", b, 1u << 20);
    const steam::ShortcutsFile f = steam::parse_shortcuts(b);
    if (!f.ok || f.entries.empty()) return "account " + id + ": no list";
    const steam::Shortcut& e = f.entries.back();
    if (e.name != steam::kAppName || !steam::same_exe(e.exe, utf8_of(a.self_exe), steam::fold_paths()) ||
        !steam::same_exe(e.start_dir, utf8_of(a.self_exe.parent_path()), steam::fold_paths()))
        return "account " + id + ": the entry is not the launcher's";
    if (id == "1001" && (f.entries.size() != 2 || !std::equal(c.other.begin() + std::ptrdiff_t(f.entries[0].begin), c.other.begin() + std::ptrdiff_t(f.entries[0].end),
                                                             b.begin() + std::ptrdiff_t(f.entries[0].begin))))
        return "account 1001: the other game's entry changed";
    appid = steam::entry_appid(e);
    struct Want { steam::Art art; int w, h; const char* name; };
    for (const Want& w : {Want{steam::Art::kCover, steam_art::kCoverW, steam_art::kCoverH, "cover"}, Want{steam::Art::kWide, steam_art::kWideW, steam_art::kWideH, "wide"},
                          Want{steam::Art::kHero, steam_art::kHeroW, steam_art::kHeroH, "hero"}, Want{steam::Art::kLogo, 920, 459, "logo"},
                          Want{steam::Art::kIcon, 0, 0, "icon"}}) {
        steam::Bytes img;
        const fs::path f2 = cfg / "grid" / steam::grid_name(appid, w.art);
        if (!steam::read_file(f2, img, 64u << 20)) return "account " + id + ": no " + utf8_of(f2.filename());
        const auto d = steam_art::decode_png(img.data(), img.size());
        if (!d || (w.w && (d->w != w.w || d->h != w.h)) || (!w.w && (d->w < 128 || d->w != d->h))) return "account " + id + ": " + w.name + " is not the right picture";
        if (!save_to.empty()) {   // the pictures for a look
            std::error_code ec;
            fs::create_directories(save_to, ec);
            steam::write_atomic(save_to / (std::string(w.name) + ".png"), img, nullptr);
        }
    }
    if (e.icon != utf8_of(cfg / "grid" / steam::grid_name(appid, steam::Art::kIcon))) return "account " + id + ": the entry's icon is not its grid icon";
    return {};
}

// The first Add to Steam: both accounts get the entry and the pictures, 1001's other game and backup kept
bool steam_check_add(App& a, const fs::path& dir, SteamCheck& c) {
    c = steam_check_setup(a, dir);
    steam_run(a);
    std::uint32_t id1 = 0, id2 = 0;
    std::string bad = steam_check_account(a, c, "1001", id1, dir / "steam_art");
    if (bad.empty()) bad = steam_check_account(a, c, "1002", id2);
    steam::Bytes backup;
    if (bad.empty() && !(steam::read_file(steam::backup_path(c.root / "userdata" / "1001" / "config" / "shortcuts.vdf"), backup, 1u << 20) && backup == c.other))
        bad = "account 1001: no backup of the list as it was";
    if (bad.empty() && a.steam_ui.level != 1) bad = "the note: " + a.steam_ui.note;
    if (bad.empty() && id1 != steam::shortcut_appid(steam::vdf_quoted(utf8_of(a.self_exe)), steam::kAppName)) bad = "the appid is not crc32(exe + name) | 0x80000000";
    std::printf("b3launcher: steam add check %s (%s; appid %u; the pictures in %s)\n", bad.empty() ? "PASS" : "FAIL",
                bad.empty() ? a.steam_ui.note.c_str() : bad.c_str(), unsigned(id1), utf8_of(dir / "steam_art").c_str());
    return !bad.empty();
}

// Again, then with the player's own pictures (a PNG cover, a JPEG background, a text file refused), then back to the
// port's: still one entry each, the list unchanged, the grid's files replaced
bool steam_check_again(App& a, const SteamCheck& c) {
    const fs::path vdf = c.root / "userdata" / "1001" / "config" / "shortcuts.vdf";
    steam::Bytes before, after;
    steam::read_file(vdf, before, 1u << 20);
    std::string bad;
    steam_run(a);
    steam::read_file(vdf, after, 1u << 20);
    if (after != before) bad = "a second Add to Steam changed the list";
    if (bad.empty() && a.steam_ui.note.rfind("Already in Steam (2 accounts)", 0) != 0) bad = "the note: " + a.steam_ui.note;
    // the player's own pictures (a small PNG made here, the first bytes of a JPEG, a text file)
    steam_art::Image mine;
    mine.w = 60;
    mine.h = 90;
    for (int k = 0; k < mine.w * mine.h; ++k) mine.rgba.insert(mine.rgba.end(), {255, 120, 20, 255});
    const steam::Bytes cover = steam_art::encode_png(mine, false);
    const steam::Bytes jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 2, 3, 0xFF, 0xD9};
    std::error_code ec;
    fs::create_directories(c.base / "mine", ec);
    steam::write_atomic(c.base / "mine" / "my cover.png", cover, nullptr);
    steam::write_atomic(c.base / "mine" / "hero.jpg", jpeg, nullptr);
    steam::write_atomic(c.base / "mine" / "notes.txt", {'h', 'i'}, nullptr);
    steam_choose(a, DialogKind::kSteamLogo, c.base / "mine" / "notes.txt");
    if (bad.empty() && (!a.cfg.steam_logo.empty() || a.steam_ui.level != 3)) bad = "a text file was taken as a picture";
    steam_choose(a, DialogKind::kSteamCover, c.base / "mine" / "my cover.png");
    steam_choose(a, DialogKind::kSteamHero, c.base / "mine" / "hero.jpg");
    Config saved;
    load_config(config_file(a.pref), saved);
    if (bad.empty() && (saved.steam_cover != utf8_of(c.base / "mine" / "my cover.png") || saved.steam_hero != utf8_of(c.base / "mine" / "hero.jpg")))
        bad = "the chosen pictures are not in launcher.ini";
    steam_run(a);
    steam::read_file(vdf, after, 1u << 20);
    std::uint32_t id = 0;
    for (const steam::Shortcut& e : steam::parse_shortcuts(after).entries) if (e.name == steam::kAppName) id = steam::entry_appid(e);
    const fs::path grid = c.root / "userdata" / "1001" / "config" / "grid";
    steam::Bytes got;
    if (bad.empty() && after != before) bad = "Add to Steam with the player's pictures changed the list";
    if (bad.empty() && !(steam::read_file(grid / steam::grid_name(id, steam::Art::kCover), got, 1u << 20) && got == cover)) bad = "the player's cover is not in grid/";
    if (bad.empty() && !(steam::read_file(grid / steam::grid_name(id, steam::Art::kHero, "jpg"), got, 1u << 20) && got == jpeg)) bad = "the player's background is not in grid/";
    if (bad.empty() && fs::exists(grid / steam::grid_name(id, steam::Art::kHero), ec)) bad = "the port's background is still beside the player's";
    std::printf("b3launcher: steam again check %s (%s)\n", bad.empty() ? "PASS" : "FAIL", bad.empty() ? a.steam_ui.note.c_str() : bad.c_str());
    return !bad.empty();
}

// Steam running: nothing written; no Steam: said so. Then the launcher's own state back (no made-up Steam, the
// pictures' choice as before)
bool steam_check_refusals(App& a, const SteamCheck& c, bool running) {
    const fs::path vdf = c.root / "userdata" / "1001" / "config" / "shortcuts.vdf";
    steam::Bytes before, after;
    steam::read_file(vdf, before, 1u << 20);
    std::string bad;
    if (running) {
        a.steam_ui.running = true;
        steam_run(a);
        if (a.steam_ui.note.rfind("Steam is running.", 0) != 0 || a.steam_ui.level != 3) bad = "the note: " + a.steam_ui.note;
    } else {
        a.steam_ui.running = false;
        steam::Places none;
        none.home = c.base / "nobody";
        a.steam_ui.places = none;
        steam_run(a);
        if (a.steam_ui.note.rfind("Steam was not found", 0) != 0 || a.steam_ui.level != 3) bad = "the note: " + a.steam_ui.note;
    }
    steam::read_file(vdf, after, 1u << 20);
    if (bad.empty() && after != before) bad = "the list was written";
    std::printf("b3launcher: steam %s check %s (%s)\n", running ? "running" : "not found", bad.empty() ? "PASS" : "FAIL",
                bad.empty() ? a.steam_ui.note.c_str() : bad.c_str());
    return !bad.empty();
}

int screenshots(App& a, const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    // update1: the pages as with no update check answer yet (the data folder's launcher.ini is put back at the end)
    const update::Cache update_kept = a.cfg.update;
    a.cfg.update = update::Cache{};
    auto frame = [&]() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) event(a, e);
        poll_dialogs(a);
        for (int k = 0; k < 3; ++k) render(a);   // the scroll settles
    };
    struct Shot { const char* name; std::function<void()> setup; };
    SDL_JoystickID vj = 0;
    int bad_checks = 0;
    ShowroomHdCheck hd_check;
    SteamCheck steam_check;   // issue #8
    a.steam_ui.checks = true;
    auto steam_shot = [&]() {   // Settings with Add to Steam focused (the page scrolled to it)
        a.modal.reset();
        go_page(a, kSettings);
        a.settings_tab = 0;
        a.in_content = true;
        render(a);
        a.focus = std::max(0, a.steam_ui.widget);
        a.reveal_focus = false;   // the page's end in view: the whole Steam part
        a.scroll[kSettings] = 1e6f;
    };
    const std::vector<Shot> shots = {
        {"play", [&]() { go_page(a, kPlay); a.in_content = true; a.focus = 0; }},
        {"settings_display", [&]() { go_page(a, kSettings); a.settings_tab = 0; a.in_content = true; a.focus = 3; }},
        {"settings_graphics", [&]() { a.settings_tab = 1; a.focus = 2; }},
        {"settings_effects", [&]() { a.settings_tab = 2; a.focus = 2; a.scroll[kSettings] = 0; }},
        {"settings_reset_confirm", [&]() { confirm_reset(a); }},
        // issue #8: Add to Steam into a made-up Steam folder: added, again (with the player's own pictures), Steam
        // running, no Steam
        {"settings_steam", [&]() { steam_shot(); }},
        {"settings_steam_added", [&]() { if (steam_check_add(a, dir, steam_check)) ++bad_checks; steam_shot(); }},
        {"settings_steam_own", [&]() { if (steam_check_again(a, steam_check)) ++bad_checks; steam_shot(); }},
        {"settings_steam_running", [&]() { if (steam_check_refusals(a, steam_check, true)) ++bad_checks; steam_shot(); }},
        {"settings_steam_none", [&]() { if (steam_check_refusals(a, steam_check, false)) ++bad_checks; steam_shot(); }},
        {"settings_steam_reset", [&]() {   // back as before: the port's art, no made-up Steam
             a.cfg.steam_cover.clear();
             a.cfg.steam_hero.clear();
             a.cfg.steam_logo.clear();
             save_config(config_file(a.pref), a.cfg);
             a.steam_ui.places.reset();
             a.steam_ui.running.reset();
             a.steam_ui.note.clear();
             a.steam_ui.level = 0;
             steam_shot();
         }},
        {"hd", [&]() { a.modal.reset(); go_page(a, kHd); a.in_content = true; a.focus = 0; }},
        {"music", [&]() { go_page(a, kMusic); a.in_content = true; a.focus = 0; }},
        {"music_playlist", [&]() { go_page(a, kMusic); a.in_content = true; a.focus = 4; }},
        {"mods", [&]() { go_page(a, kMods); a.in_content = true; a.focus = 2; }},   // modkit1
        {"mods_last", [&]() { go_page(a, kMods); a.in_content = true; a.focus = 13; }},   // the fourth card (errors / notes; modkit1b: after Add mod; sdk2b: and Developer mods)
        {"game", [&]() { go_page(a, kGame); a.in_content = false; }},
        {"saves", [&]() { go_page(a, kSaves); a.in_content = true; a.focus = 4; }},   // (1.1: after Choose save folder... and Use the default folder: File 2)
        {"saves_folder", [&]() { if (saves_folder_checks(a, dir)) ++bad_checks; go_page(a, kSaves); a.in_content = true; a.focus = 1; }},   // 1.1
        {"stats", [&]() { go_page(a, kStats); a.in_content = false; a.scroll[kStats] = 0; }},
        {"stats_achievements", [&]() { go_page(a, kStats); a.in_content = true; a.focus = int(a.stats.size()) + 6; }},
        {"replays", [&]() { go_page(a, kReplays); a.in_content = true; a.focus = std::min(2, int(a.replays.size()) + 1); }},
        {"stats_new", [&]() {   // the second profile found (a new one in the check's saves: nothing unlocked)
             go_page(a, kStats);
             a.stats_sel = a.stats.size() > 1 ? 1 : 0;
             a.in_content = true;
             a.focus = int(a.stats_sel);
         }},
        {"controllers_none", [&]() { go_page(a, kPads); a.in_content = false; }},
        // photo2: the showroom with three cars (the first, a Super, a heavyweight), the turntable still at set angles
        {"showroom", [&]() { go_page(a, kShowroom); a.in_content = true; a.focus = 0; a.show.tt.reset(); a.show.tt.spin = false; }},
        {"showroom_super", [&]() {
             showroom_refresh(a);
             for (std::size_t k = 0; k < a.show.cars.size(); ++k) if (a.show.cars[k].folder == "SUPR" && a.show.cars[k].number == 10) a.show.sel = k;
             a.show.paint = 1;
             a.show.tt.yaw = 2.4f;
             a.show.tt.pitch = 0.12f;
         }},
        {"showroom_heavy", [&]() {
             for (std::size_t k = 0; k < a.show.cars.size(); ++k) if (a.show.cars[k].folder == "HEVY" && a.show.cars[k].number == 9) a.show.sel = k;
             a.show.paint = 0;
             a.show.tt.yaw = -0.6f;
             a.show.tt.pitch = 0.35f;
             a.show.tt.zoom = 1.1f;
         }},
        // showhd1: the first car with a test HD pack made from the disc (showroom_hd_check), then everything back
        {"showroom_hd", [&]() {
             go_page(a, kShowroom);
             a.in_content = true;
             a.focus = 0;
             a.show.tt.reset();
             a.show.tt.spin = false;
             if (showroom_hd_check(a, dir, hd_check)) ++bad_checks;
         }},
        {"showroom_hd_off", [&]() { showroom_hd_end(a, hd_check); }},
        {"credits", [&]() { go_page(a, kCredits); a.in_content = true; a.focus = 0; }},
        {"controllers_pad", [&]() {
             go_page(a, kPads);
             a.pad_test = true;
             vj = demo_pad();
             SDL_Event e;
             for (int k = 0; k < 4; ++k) while (SDL_PollEvent(&e)) event(a, e);
             if (SDL_Joystick* j = SDL_GetJoystickFromID(vj)) {
                 SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_SOUTH, true);
                 SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
                 SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTX, -16000);
                 SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTY, -9000);
                 SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 26000);
             }
             SDL_UpdateJoysticks();
             SDL_FlushEvents(SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED);
             a.pad_test = true;
             a.pad_used_last = true;
             a.in_content = true;
             a.focus = 0;
         }},
        {"controls_keyboard", [&]() {   // remap1: the bindings editor, the keyboard's driving view (a custom key shown)
             a.pad_test = false;
             a.modal.reset();
             go_page(a, kPads);
             a.ctl_player = 0; a.ctl_device = 0; a.ctl_view = 0;
             a.reveal_focus = true;   // as after a key: the focused row in view (a shot before turned it off)
             a.controls = b3native::input::default_controls();
             b3native::input::rebind(a.controls, 0, b3native::input::Device::Keyboard, b3native::input::Ds2::Cross,
                                     {b3native::input::Device::Keyboard, 14, b3native::input::PadInput::None}, b3native::input::KeyCtx::Drive,
                                     b3native::input::Resolve::Keep);
             a.in_content = true;
             render(a);
             a.focus = a.ctl_first_widget + 11;   // the first column's last row: the whole list in view
         }},
        {"controls_gamepad", [&]() {   // the controller's bindings
             a.ctl_device = 1;
             render(a);
             a.focus = a.ctl_first_widget + 11;
         }},
        {"controls_layout", [&]() {   // padlayout: CONTROLLER LAYOUT focused (Auto: the virtual DualSense, the PlayStation layout)
             a.ctl_view = 0;
             render(a);
             a.focus = a.ctl_first_widget - 1;
         }},
        {"controls_layout_xbox", [&]() {   // padlayout: the Xbox layout chosen, the driving view (A boost, B Crashbreaker)
             b3native::input::set_layout(a.controls, 0, b3native::input::PadLayout::Xbox);
             render(a);
             a.focus = a.ctl_first_widget + 5;   // R1: A, RB
         }},
        {"controls_layout_xbox_menus", [&]() {   // padlayout: the same in the menus view (A confirm, B back)
             a.ctl_view = 1;
             render(a);
             a.focus = a.ctl_first_widget;   // CROSS: RT, A
         }},
        {"controls_gyro", [&]() {   // pad1: the controller's gyro steering rows (Mix)
             a.ctl_view = 0;
             b3native::input::set_layout(a.controls, 0, b3native::input::PadLayout::Auto);
             a.controls.player[0].gyro.mode = 1;
             render(a);
             a.focus = a.ctl_first_widget + 27;   // the gyro rows (the 4th: dead zone), after the 24 action rows
         }},
        {"controls_conflict", [&]() {   // a rebind of Cross to East: Circle has it
             a.flow.begin(0, b3native::input::Device::Gamepad, b3native::input::Ds2::Cross, b3native::input::KeyCtx::Drive, false);
             a.flow.input(a.controls, {b3native::input::Device::Gamepad, 0, b3native::input::PadInput::East});
             flow_modal(a);
         }},
        {"type_path", [&]() { a.flow.cancel(); a.controls = b3native::input::default_controls(); go_page(a, kGame); a.pad_used_last = a.touch_last = false; ask_path(a, DialogKind::kGameFolder, "Game data folder", "/home/me/Games/Burnout 3/game"); }},
        {"keys", [&]() {   // the keyboard drives the page: down twice to FULLSCREEN, Enter, right on FRAME RATE LIMIT
             a.modal.reset();
             SDL_StopTextInput(a.win);
             go_page(a, kSettings);
             a.settings_tab = 0;
             a.in_content = true;
             a.focus = 0;
             render(a);
             const bool before = a.settings.fullscreen;
             const int cap_before = a.settings.frame_cap;
             auto key = [&](SDL_Keycode k) {
                 SDL_Event e{};
                 e.type = SDL_EVENT_KEY_DOWN;
                 e.key.key = k;
                 e.key.windowID = SDL_GetWindowID(a.win);
                 event(a, e);
                 render(a);
             };
             for (SDL_Keycode k : {SDLK_DOWN, SDLK_DOWN, SDLK_RETURN, SDLK_DOWN, SDLK_DOWN, SDLK_RIGHT}) key(k);
             b3settings::Settings back;
             b3settings::load_file(utf8_of(settings_file(a.pref)), back);
             const bool ok = back.fullscreen == !before && back.frame_cap != cap_before && back == a.settings;
             std::printf("b3launcher: keys check %s (fullscreen %d -> %d, frame cap %d -> %d, saved to pc_settings.ini)\n",
                         ok ? "PASS" : "FAIL", int(before), int(back.fullscreen), cap_before, back.frame_cap);
             if (!ok) ++bad_checks;
             // back as it was
             a.settings.fullscreen = before;
             a.settings.frame_cap = cap_before;
             save_settings(a);
         }},
        {"wheel", [&]() {   // the mouse wheel scrolls a page with a clicked (focused) row: it used to be pulled back to it
             a.modal.reset();
             go_page(a, kSettings);
             a.settings_tab = 1;   // GRAPHICS: taller than the window
             a.in_content = true;
             a.focus = 0;
             a.scroll[kSettings] = 1e6f;
             a.reveal_focus = false;   // (measuring the page: not pulled to the focused row)
             render(a);
             const float most = a.scroll[kSettings];   // the clamp: how far the page can scroll
             a.scroll[kSettings] = 0;
             a.reveal_focus = true;   // as after a key: the focused row in view
             render(a);
             for (int i = 0; i < 3; ++i) {
                 SDL_Event e{};
                 e.type = SDL_EVENT_MOUSE_WHEEL;
                 e.wheel.y = -1.0f;   // down
                 e.wheel.windowID = SDL_GetWindowID(a.win);
                 event(a, e);
                 render(a);
             }
             const float after = a.scroll[kSettings];
             const bool ok = after >= std::min(most, 144.0f) - 1.0f;   // the old pull-back left it at the focused row (~2 px)
             std::printf("b3launcher: wheel check %s (page scrolls up to %.0f px; three wheel steps with the first row focused: %.0f px)\n",
                         ok ? "PASS" : "FAIL", double(most), double(after));
             if (!ok) ++bad_checks;
         }},
        {"showroom_pad", [&]() {   // a pad alone: down from the car to its paint, right: the next paint; up, right: the next car
             a.modal.reset();
             go_page(a, kShowroom);
             a.in_content = true;
             a.focus = 0;
             a.show.paint = 0;
             render(a);
             const std::size_t car0 = a.show.sel;
             const int np = a.show.view && a.show.view->loaded() ? a.show.view->paints() : 1;
             nav(a, Nav::Down);
             render(a);
             nav(a, Nav::Right);
             render(a);
             const int paint1 = a.show.paint;
             const std::size_t car1 = a.show.sel;   // still the first car: right changed the paint row, not the car
             nav(a, Nav::Up);
             render(a);
             nav(a, Nav::Right);
             render(a);
             const bool ok = a.show.cars.empty() || ((np < 2 || paint1 == 1) && car1 == car0 && a.show.sel != car0);
             std::printf("b3launcher: showroom pad check %s (%zu cars; paint after down, right: %d of %d; car %zu -> %zu after up, right)\n",
                         ok ? "PASS" : "FAIL", a.show.cars.size(), paint1 + 1, np, car0 + 1, a.show.sel + 1);
             if (!ok) ++bad_checks;
         }},
        {"walk", [&]() {   // launcherux1: every page by the keys alone, a pad alone and the mouse (walk_checks)
             a.modal.reset();
             SDL_StopTextInput(a.win);
             if (walk_checks(a, vj)) ++bad_checks;
         }},
        {"osk", [&]() {   // osk1: a pad alone types a path on the on-screen keyboard and picks folders in the launcher's list
             a.modal.reset();
             SDL_StopTextInput(a.win);
             if (osk_checks(a, vj, dir)) ++bad_checks;
         }},
        {"report", [&]() {   // test builds: the bug report, into the check's own data folder (no folder opened)
             a.modal.reset();
             go_page(a, kPlay);
             a.report_dir = a.pref;
             a.report_open = false;
             make_report(a);
         }},
        {"update", [&]() {   // update1: the update check with canned answers (update_checks; no request)
             a.modal.reset();
             SDL_StopTextInput(a.win);
             if (update_checks(a, dir)) ++bad_checks;
             go_page(a, kPlay);
         }},
        {"skip", [&]() {   // launchskip1: the start without the launcher (core.h skip_launcher) and each exception
             a.modal.reset();
             SDL_StopTextInput(a.win);
             go_page(a, kPlay);
             const b3settings::Settings keep_settings = a.settings;
             const b3settings::RunState keep_run = a.run;
             const bool keep_have_run = a.have_run;
             const GameCheck keep_game = a.game;
             const SkipBlock keep_block = a.skip_block;
             // with B3LAUNCHER_SHOT_START=1 (a stand-in beside the launcher, as the "start" shot) the start is made
             const char* st = std::getenv("B3LAUNCHER_SHOT_START");
             a.skip_no_spawn = !(st && st[0] == '1');
             int fails = 0;
             auto report = [&](bool ok, const std::string& what) {
                 std::printf("b3launcher: skip check %s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
                 if (!ok) ++fails;
             };
             auto names = [](SkipBlock b) {
                 switch (b) {
                 case SkipBlock::kNone: return "start";
                 case SkipBlock::kSetting: return "setting";
                 case SkipBlock::kArgument: return "--launcher";
                 case SkipBlock::kHeld: return "held";
                 case SkipBlock::kNoGameData: return "no game data";
                 case SkipBlock::kBadGameData: return "bad game data";
                 case SkipBlock::kNoProgram: return "no game program";
                 case SkipBlock::kLastRun: return "last run";
                 }
                 return "?";
             };
             // main()'s start, on the check's state: the "Starting" screen, then (due at once) the start path
             auto at_start = [&](bool launcher_arg, bool held) {
                 a.skip_at = 0;
                 a.skip_starts = 0;
                 a.skip_block = skip_launcher(skip_input(a, launcher_arg, held));
                 if (a.skip_block == SkipBlock::kNone) {
                     begin_skip(a);
                     a.skip_at = a.now() - 0.001;   // its second over
                     skip_poll(a);
                     for (int k = 0; k < 400 && a.child; ++k) {   // the stand-in (it ends at once: "The game ended at once")
                         poll_child(a);
                         SDL_Delay(10);
                     }
                     if (a.child) { release(*a.child); a.child.reset(); }
                     a.modal.reset();
                     a.quit = false;
                     a.skipping = false;
                 }
                 return a.skip_block;
             };
             const SkipInput base = skip_input(a, false, false);
             // what a start with the setting off gives on this check's data (the start itself needs the data and the
             // game program beside the launcher)
             const SkipBlock data_block = !base.game_ok ? SkipBlock::kNoGameData : base.game_warning ? SkipBlock::kBadGameData
                                          : !base.have_program ? SkipBlock::kNoProgram : SkipBlock::kNone;
             a.have_run = true;
             a.run = b3settings::clean_end();
             a.settings.launcher_at_start = true;
             {
                 const SkipBlock b = at_start(false, false);
                 report(b == SkipBlock::kSetting && a.skip_starts == 0 && a.skip_at == 0, std::string("setting on: the launcher shows (") + names(b) + ")");
             }
             a.settings.launcher_at_start = false;
             {
                 const SkipBlock b = at_start(false, false);
                 report(b == data_block && a.skip_starts == (b == SkipBlock::kNone ? 1 : 0),
                        std::string("setting off, a clean last run: ") + names(b) + " (expected " + names(data_block) + "; started " +
                            std::to_string(a.skip_starts) + (a.skip_no_spawn ? ", counted" : ", made") + ")");
             }
             {
                 const SkipBlock b = at_start(true, false);
                 report(b == SkipBlock::kArgument && a.skip_starts == 0, std::string("--launcher: the launcher shows (") + names(b) + ")");
             }
             {
                 const SkipBlock b = at_start(false, true);
                 report(b == SkipBlock::kHeld && a.skip_starts == 0, std::string("Shift / Back held at the start: the launcher shows (") + names(b) + ")");
             }
             {
                 a.run.running = true;   // the last run did not end (the crash count's first)
                 const SkipBlock b = at_start(false, false);
                 report(b == SkipBlock::kLastRun || (data_block != SkipBlock::kNone && b == data_block),
                        std::string("a bad last run: the launcher shows (") + names(b) + ")");
                 a.run = b3settings::clean_end();
             }
             {
                 a.game = GameCheck{};   // no game data
                 const SkipBlock b = at_start(false, false);
                 report(b == SkipBlock::kNoGameData && a.skip_starts == 0, std::string("no game data: the launcher shows (") + names(b) + ")");
                 a.game = keep_game;
             }
             // during the "Starting" screen: Shift pressed (a key event), then a controller's Back held (polled)
             {
                 begin_skip(a);
                 SDL_Event e{};
                 e.type = SDL_EVENT_KEY_DOWN;
                 e.key.windowID = SDL_GetWindowID(a.win);
                 e.key.scancode = SDL_SCANCODE_LSHIFT;
                 e.key.key = SDLK_LSHIFT;
                 e.key.down = true;
                 SDL_PushEvent(&e);
                 frame();
                 skip_poll(a);
                 report(a.skip_at == 0 && a.skip_block == SkipBlock::kHeld, "Shift pressed on the Starting screen: the launcher opens");
             }
             {
                 if (!vj || !SDL_GetJoystickFromID(vj)) {
                     vj = demo_pad();
                     frame();
                 }
                 begin_skip(a);
                 SDL_Joystick* j = SDL_GetJoystickFromID(vj);
                 if (j) SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_BACK, true);
                 SDL_UpdateJoysticks();
                 SDL_FlushEvents(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_EVENT_GAMEPAD_BUTTON_DOWN);   // the poll, not the event
                 skip_poll(a);
                 report(j && a.skip_at == 0 && a.skip_block == SkipBlock::kHeld && a.skip_starts == 0,
                        std::string("Back held on a controller on the Starting screen: the launcher opens") + (j ? "" : " (no virtual pad)"));
                 if (j) SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_BACK, false);
                 SDL_UpdateJoysticks();
                 frame();
             }
             // the Play page's switch writes the setting the game's row shows (pc_settings.ini launcher_at_start)
             {
                 a.settings = keep_settings;
                 a.settings.launcher_at_start = false;
                 save_settings(a);
                 b3settings::Settings back;
                 const bool read = b3settings::load_file(utf8_of(settings_file(a.pref)), back);
                 a.settings.launcher_at_start = true;
                 save_settings(a);
                 b3settings::Settings again;
                 b3settings::load_file(utf8_of(settings_file(a.pref)), again);
                 report(read && !back.launcher_at_start && again.launcher_at_start, "the Play page's switch: pc_settings.ini launcher_at_start");
             }
             a.settings = keep_settings;
             save_settings(a);
             a.run = keep_run;
             a.have_run = keep_have_run;
             a.game = keep_game;
             a.skip_starts = 0;
             a.skip_no_spawn = false;
             if (fails) ++bad_checks;
             // the picture: the Starting screen with its hint (it stays up until the next shot)
             a.skip_block = keep_block;
             begin_skip(a);
             a.skip_at = a.now() + 1e6;
         }},
        {"start", [&]() {   // START GAME for real: native_play beside the launcher (in a check, a stand-in that ends at once)
             a.skip_at = 0;   // launchskip1: the "skip" shot's Starting screen
             a.modal.reset();
             SDL_StopTextInput(a.win);
             go_page(a, kPlay);
             // mac5 (2026-11-09: a check started the real game in a window): only with B3LAUNCHER_SHOT_START=1,
             // which a check sets when the program beside the launcher is a stand-in
             if (const char* e = std::getenv("B3LAUNCHER_SHOT_START"); !(e && e[0] == '1')) {
                 message(a, "Start skipped", "A screenshot check does not start the game (B3LAUNCHER_SHOT_START=1 with a stand-in does).");
                 return;
             }
             start_game(a, true);
             for (int k = 0; k < 400 && a.child; ++k) {
                 poll_child(a);
                 SDL_Delay(10);
             }
         }},
    };
    int bad = 0;
    for (const Shot& s : shots) {
        s.setup();
        frame();
        SDL_UpdateJoysticks();
        render(a);
        const fs::path to = dir / (std::string(s.name) + ".png");
        if (!save_shot(a, to)) { std::fprintf(stderr, "b3launcher: cannot write %s: %s\n", utf8_of(to).c_str(), SDL_GetError()); ++bad; }
        else std::printf("b3launcher: %s\n", utf8_of(to).c_str());
    }
    // photo2: B3LAUNCHER_SHOWROOM_CARS=<FOLDER>/<n>,... (e.g. HSPC/20,HEVY/36): one showroom shot per car (the names
    // against the drawn cars), showroom_<FOLDER>_<n>.png
    if (const char* list = std::getenv("B3LAUNCHER_SHOWROOM_CARS"); list && *list) {
        a.modal.reset();
        go_page(a, kShowroom);
        showroom_refresh(a);
        std::string l = list;
        for (std::size_t at = 0; at <= l.size();) {
            const std::size_t end = std::min(l.find(',', at), l.size());
            const std::string item = l.substr(at, end - at);
            at = end + 1;
            char folder[8] = {};
            int n = 0;
            if (std::sscanf(item.c_str(), "%7[A-Za-z]/%d", folder, &n) != 2) continue;
            for (std::size_t k = 0; k < a.show.cars.size(); ++k)
                if (a.show.cars[k].folder == upper(folder) && a.show.cars[k].number == n) a.show.sel = k;
            a.show.paint = 0;
            a.show.tt.reset();
            a.show.tt.spin = false;
            frame();
            render(a);
            const fs::path to = dir / ("showroom_" + upper(folder) + "_" + std::to_string(n) + ".png");
            if (!save_shot(a, to)) ++bad;
            else std::printf("b3launcher: %s\n", utf8_of(to).c_str());
        }
    }
    if (vj) SDL_DetachVirtualJoystick(vj);
    a.cfg.update = update_kept;   // update1
    save_config(config_file(a.pref), a.cfg);
    return bad || bad_checks ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string pref_arg, shot_dir;
    bool launcher_arg = false;   // launchskip1: --launcher (the launcher shows, whatever the setting)
    int win_w = 1280, win_h = 720;
    for (int k = 1; k < argc; ++k) {
        const std::string s = argv[k];
        auto next = [&]() -> std::string { return k + 1 < argc ? argv[++k] : std::string(); };
        if (s == "--pref") pref_arg = next();
        else if (s == "--screenshot") shot_dir = next();
        else if (s == "--launcher") launcher_arg = true;
        else if (s == "--size") {
            const std::string v = next();
            if (std::sscanf(v.c_str(), "%dx%d", &win_w, &win_h) != 2) { win_w = 1280; win_h = 720; }
        } else {
            std::fprintf(stderr, "usage: b3launcher [--pref DIR] [--size WxH] [--launcher] [--screenshot DIR]\n");
            return 2;
        }
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetAppMetadata("Burnout 3 Launcher", b3port::version(), "b3port.launcher");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "b3launcher: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    App app;
    g_app = &app;
    App& a = app;
    if (const char* base = SDL_GetBasePath()) a.exe_dir = path_from_utf8(base);
#ifdef __APPLE__
    // macpkg1: inside an app bundle SDL_GetBasePath() is Contents/Resources, but native_play is beside the launcher
    // (Contents/MacOS, tools/release/package_mac.sh): the folder of the executable itself
    {
        char self[4096];
        std::uint32_t n = sizeof self;
        std::error_code sec;
        if (_NSGetExecutablePath(self, &n) == 0) {
            const fs::path p = fs::weakly_canonical(path_from_utf8(self), sec);
            if (!sec && p.has_parent_path()) a.exe_dir = p.parent_path();
        }
    }
#endif
    {   // 1.1 one-click updates: this launcher's own file (found now: an update renames the running one away)
        a.self_exe = self_exe_path();
        if (!a.exe_dir.empty()) installer::clean_leftovers(a.exe_dir);   // an install cut short
    }
    std::error_code ec;
    a.cwd = fs::current_path(ec);
    if (!pref_arg.empty()) a.pref = fs::absolute(path_from_utf8(pref_arg), ec);
    else if (char* p = SDL_GetPrefPath("b3port", "native")) {   // the game window's own folder (src/b3replay/main.cpp)
        a.pref = path_from_utf8(p);
        SDL_free(p);
    }
    fs::create_directories(a.pref, ec);
    load_config(config_file(a.pref), a.cfg);
    load_settings(a);
    controls_load(a);   // remap1
    {
        const GameCheck remembered = a.cfg.game_dir.empty() ? GameCheck{} : check_game_folder(path_from_utf8(a.cfg.game_dir));
        if (remembered.ok) a.game_dir = path_from_utf8(a.cfg.game_dir);
        else {
            a.game_dir = find_game_folder("", a.exe_dir, a.cwd);
            if (a.game_dir.empty() && !a.cfg.game_dir.empty()) a.game_dir = path_from_utf8(a.cfg.game_dir);   // shown as missing
        }
    }
    refresh(a);
    // update1: the update check's test hook (an answer file instead of the request) and the day's check, in the
    // background; a --screenshot run never makes a request (its checks feed canned answers)
    if (const char* f = std::getenv("B3LAUNCHER_UPDATE_FILE"); f && *f) a.upd.file = f;
    if (!shot_dir.empty()) a.upd.net = false;
    else if (update::due(a.cfg.update, unix_now())) start_update_check(a);
    if (!a.game.ok) a.page = kGame;   // the first thing to set
    a.in_content = true;              // its first button focused (START GAME, or Choose folder)

    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (shot_dir.empty() ? 0 : SDL_WINDOW_HIDDEN);
    a.win = SDL_CreateWindow(("Burnout 3 Launcher - " + std::string(b3port::version()) + " - by u0rhs").c_str(), win_w, win_h, flags);
    if (!a.win) {
        std::fprintf(stderr, "b3launcher: window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(a.win, 900, 560);
    // the B3 icon: beside the launcher (Windows / a release), else the repo's assets/ (a build from the repo)
    SDL_Surface* icon_surface = nullptr;
    for (const fs::path& f : icon_files(a))   // (macpkg1: the .app's Contents/Resources among them)
        if (!icon_surface && fs::exists(f, ec)) icon_surface = SDL_LoadPNG(utf8_of(f).c_str());
    // (no file: the icon built into the program; the header's texture below uses the file's 512 px version when there is one)
    if (icon_surface) {
        SDL_SetWindowIcon(a.win, icon_surface);
    } else if (SDL_Surface* built_in = b3port::app_icon_surface()) {
        SDL_SetWindowIcon(a.win, built_in);
        SDL_DestroySurface(built_in);
    }
    a.ren = SDL_CreateRenderer(a.win, shot_dir.empty() ? nullptr : "software");
    if (!a.ren) a.ren = SDL_CreateRenderer(a.win, "software");
    if (a.ren && icon_surface) {
        a.icon = SDL_CreateTextureFromSurface(a.ren, icon_surface);
        if (a.icon) SDL_SetTextureScaleMode(a.icon, SDL_SCALEMODE_LINEAR);
    }
    if (icon_surface) SDL_DestroySurface(icon_surface);
    if (!a.ren) {
        std::fprintf(stderr, "b3launcher: renderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(a.ren, 1);
    auto text = std::make_unique<Text>(a.ren);   // its textures go before the renderer
    a.text = text.get();
    if (!text->load()) std::fprintf(stderr, "b3launcher: no TrueType font found: the plain one draws ($B3LAUNCHER_FONT)\n");
    load_look(a);
    {
        int n = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
            for (int k = 0; k < n; ++k)
                if (SDL_Gamepad* g = SDL_OpenGamepad(ids[k])) a.pads.push_back(g);
            SDL_free(ids);
        }
    }
    if (shot_dir.empty()) {   // launchskip1: the game at once, unless one of skip_launcher's exceptions holds
        a.skip_block = skip_launcher(skip_input(a, launcher_arg, skip_held(a)));
        if (a.skip_block == SkipBlock::kNone) {
            std::printf("b3launcher: OPEN THE LAUNCHER AT START is off: starting the game (hold Shift or Back / Select for the launcher)\n");
            begin_skip(a);
        }
    }
    int rc = 0;
    if (!shot_dir.empty()) {
        rc = screenshots(a, path_from_utf8(shot_dir));
    } else {
        while (!a.quit) {
            SDL_Event e;
            const bool live = a.page == kPads || a.page == kShowroom || a.child || a.skip_at > 0 || (a.modal && a.modal->input) || a.browser;
            if (SDL_WaitEventTimeout(&e, live || !a.pads.empty() ? 16 : 100)) {
                event(a, e);
                while (SDL_PollEvent(&e)) event(a, e);
            }
            a.keys.step(1.0f / 60.0f);
            flow_poll(a);   // remap1
            if (a.now_playing) a.now_playing->set_enabled(a.page == kMusic && a.settings.other_music_apps != 0);   // nowplaying1
            if (a.flow.state() == b3native::input::RebindFlow::State::Waiting) {   // remap1: a stick pushed to rebind does not move round the page,
                for (auto& h : a.hold) { h.down = true; h.next = 1e300; }       // not even after it was taken (held: released first)
                a.trig_l = a.trig_r = true;
            } else {
                poll_pad_nav(a);
            }
            poll_dialogs(a);
            skip_poll(a);   // launchskip1
            poll_child(a);
            poll_update(a);   // update1
            poll_install(a);  // 1.1 one-click updates
            pads_motion(a);   // pad1
            render(a);
            SDL_RenderPresent(a.ren);
            if (a.steam_ui.pending) steam_run(a);   // issue #8: after the frame that shows "Adding..."
        }
    }
    pads_motion(a, true);   // pad1: the triggers free, the sensors off
    end_update_check(a);    // update1
    if (a.inst.th.joinable()) a.inst.th.join();   // 1.1: an install under way finishes (a swap is never cut short)
    for (SDL_Gamepad* g : a.pads) SDL_CloseGamepad(g);
    if (a.show.tex) SDL_DestroyTexture(a.show.tex);   // photo2
    a.show.view.reset();
    a.text = nullptr;
    text.reset();
    for (auto& [n, t] : a.look.tex) SDL_DestroyTexture(t);
    for (SDL_Texture* t : a.look.font_tex)
        if (t) SDL_DestroyTexture(t);
    for (auto& [k, t] : a.look.prompts)
        if (t.first) SDL_DestroyTexture(t.first);
    SDL_DestroyRenderer(a.ren);
    SDL_DestroyWindow(a.win);
    SDL_Quit();
    return rc;
}
