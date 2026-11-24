// core.h -- the launcher's logic without SDL (remaster-features §8 "Launcher / setup program"; tests/test_launcher.cpp):
// the launcher's own ini, the game data folder's check and search, the game program and its arguments, the HD pack
// folder's check, the music folder's songs and rows, the save files, the last run's state as words, and the settings
// rows' value steps.
//
// The launcher edits the files the game reads and nothing else: pc_settings.ini (include/b3settings/settings.h) and
// run_state.ini (safemode.h) in the user's data folder (b3io::user_dir(), include/b3port/user_dir.h), the saves folder's
// active-slot.txt and profile-N.sav (src/native/save_store.h). Its own choice of game data folder is launcher.ini
// beside them; the game gets it as `native_play --game <folder>`.
#ifndef B3LAUNCHER_CORE_H
#define B3LAUNCHER_CORE_H

#include "b3settings/page.h"
#include "b3settings/safemode.h"
#include "update.h"   // update1: the update check's on / off and last answer

#include "../native/input_keys.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b3launcher {

namespace fs = std::filesystem;

// Paths as UTF-8 text (the ini files, SDL's dialogs) and back; on Windows a std::string path would be the ANSI code page.
fs::path path_from_utf8(const std::string& s);
std::string utf8_of(const fs::path& p);

// ---- launcher.ini (beside pc_settings.ini) ----
struct Config {
    std::string game_dir;   // the disc folder (SYSTEM.CNF, SLUS_210.50, DATA, TRACKS, ...); empty: search
    // 1.1: the save folder chosen on the Saves page (a cloud or network folder shared by two computers); empty: the
    // default, saves_folder(pref). native_play reads it too (save_store.h chosen_saves_root). Written only when set.
    std::string saves_dir;
    update::Cache update;   // update1: the update_* keys (update.h)
    // issue #8: ADD TO STEAM's pictures of the player's choice (PNG / JPEG files, UTF-8 paths; steam.h); empty: the
    // port's own (steam_art.h). Written only when set.
    std::string steam_cover, steam_hero, steam_logo;
    // saveflow: the profile-N-unsaved-<stamp>.sav files the player chose "Leave it" (or "Use it") for on the Saves page
    // (file names; one unsaved_left line each)
    std::vector<std::string> unsaved_left;
    int showroom_series = -1;   // showroom2: the SHOWROOM's last series (showroom.h's ids; -1: none yet, the first)
    bool operator==(const Config&) const = default;
};
std::string config_to_ini(const Config& c);
Config config_from_ini(std::string_view text);           // unknown keys ignored
// portable mode (1.3.0): game_dir, saves_dir and the Steam pictures that lie inside `install` (the launcher's folder)
// are written as "<install>/..." and read back against it (b3port/user_dir.h): load_config / save_config do it with the
// program's own folder, so a USB stick whose drive letter changes still finds them
void config_paths_to_install(Config& c, const fs::path& install);
void config_paths_from_install(Config& c, const fs::path& install);
bool load_config(const fs::path& path, Config& c);       // false: no file (c unchanged)
bool save_config(const fs::path& path, const Config& c, std::string* why = nullptr);

// The files of the user's data folder (`pref`: b3io::user_dir(), as the game's window uses).
fs::path settings_file(const fs::path& pref);    // pc_settings.ini
fs::path run_state_file(const fs::path& pref);   // run_state.ini
fs::path config_file(const fs::path& pref);      // launcher.ini
fs::path logs_folder(const fs::path& pref);      // logs: the game's run logs and pad recordings (b3report/run_logs.h)
// Where a started game's own stdout / stderr go: the null device ("NUL" / "/dev/null"). The game writes its run log
// itself (native_play --log <logs>/run-<date-time>.log, tools/native/run_log.h).
fs::path null_device();
fs::path saves_folder(const fs::path& pref);     // saves (save_store.h default_saves_root: the same folder)
// 1.1: the saves folder in use: `chosen` (Config::saves_dir, UTF-8) when set, else saves_folder(pref)
fs::path saves_folder(const fs::path& pref, const std::string& chosen);

// ---- the game data folder ----
// mac5 (integrity): native_play's check of the program [C] retail SLUS_210.50 (also the disc a replay file names,
// src/native/replay_file.h)
inline constexpr std::uint64_t kProgramSize = 4073816;
inline constexpr std::uint64_t kProgramFnv = 0x790a7666929b62eaull;

struct GameItem {
    std::string name;       // as on the disc ("SLUS_210.50", "TRACKS")
    std::string what;       // "the game program", "the tracks"
    bool folder = false;
    bool required = true;
    bool found = false;
};
struct GameCheck {
    bool exists = false;    // the folder is there
    bool ok = false;        // every required item found
    std::vector<GameItem> items;
    fs::path suggestion;    // a folder inside it that is the game's (the user chose the folder above it)
    std::string summary;    // one line for the status
    // mac5 (integrity): set when `ok` but the data will not run: the program is not the retail SLUS_210.50 (another
    // version or a bad copy; native_play checks the same size and FNV-1a 64), or a disc image ends before a file in it
    // does (a truncated download / dump). Empty when all is well.
    std::string warning;
};
// Case-insensitive names (a disc copied on Linux may be lower case).
GameCheck check_game_folder(const fs::path& dir);
// chd1: a CHD file the port cannot read (b3asset::disc::image_error: damaged, cut short, one that needs its parent):
// "The CHD image cannot be read (...): <path>" (translated as one `{}` entry), else empty
std::string chd_problem(const fs::path& path);
// Where the launcher looks when none is remembered (or the remembered one is gone), in order: the remembered folder,
// <launcher folder>/game, <launcher folder>/data/game, <launcher folder>/../data/game (build/ in the repo),
// <cwd>/data/game.
std::vector<fs::path> game_folder_candidates(const std::string& remembered, const fs::path& exe_dir, const fs::path& cwd);
// The first candidate that checks out (else empty).
fs::path find_game_folder(const std::string& remembered, const fs::path& exe_dir, const fs::path& cwd);

// ---- starting the game ----
// native_play beside the launcher (native_play.exe on Windows).
fs::path game_program(const fs::path& exe_dir);
// native_play's arguments (without the program): --game <dir>, and --safe-mode when asked for.
std::vector<std::string> game_arguments(const std::string& game_dir, bool safe_mode);
// One Windows command line from argv[] (CommandLineToArgvW's quoting rules: quotes around arguments with spaces,
// tabs or quotes, backslashes doubled before a quote).
std::string windows_command_line(const std::vector<std::string>& argv);

// ---- the HD pack folder (the check of src/b3replay/pcpage.cpp hd_folder_done: .dds / .png images in it) ----
struct HdCheck {
    bool exists = false;
    std::size_t images = 0;      // counted up to `limit`
    bool more = false;           // stopped at the limit
};
HdCheck check_hd_folder(const fs::path& dir, std::size_t limit = 100000);

// ---- the music folder (the custom soundtrack, remaster-features §6; in the game SETTINGS > AUDIO > MUSIC) ----
// The folder and the playlist mode are Settings::music_folder / playlist_mode in pc_settings.ini, as the in-game page
// keeps them; the songs are the game's own scan (src/native/custom_music.h list_audio_files / read_tags), so the count
// is the one native_play plays from.
struct MusicSong {
    std::string path;                 // UTF-8
    std::string title, artist, album; // the tags (the file name when untagged, as the EA TRAX popup shows them)
};
struct MusicCheck {
    bool exists = false;              // the folder is there
    std::size_t songs = 0;            // MP3 / FLAC / Ogg Vorbis / WAV files in it and its subfolders
    std::vector<MusicSong> first;     // the first `list` of them in the game's order, with their tags
    std::string error;                // the scan stopped early (a folder it could not read)
};
MusicCheck check_music_folder(const fs::path& dir, std::size_t list = 6);
// The in-game MUSIC section's rows (INTERFACE tab, as native_play shows it: MUSIC FOLDER, REMOVE MUSIC FOLDER while a
// folder is chosen, PLAYLIST), `status` the last choice's result as the game words it. Edits: b3settings::apply_edit.
std::vector<b3settings::Row> music_rows(const b3settings::Settings& s, const std::string& status = {});
// The PLAYLIST row of music_rows (the same values, words and description as in the game).
b3settings::Row playlist_row(const b3settings::Settings& s);
b3settings::Row custom_music_volume_row(const b3settings::Settings& s);   // musicvol1: CUSTOM MUSIC VOLUME
b3settings::Row custom_music_loudness_row(const b3settings::Settings& s);   // musicloud: CUSTOM MUSIC LOUDNESS
// nowplaying1: the OTHER MUSIC APPS row of music_rows, `now` what is detected (the row's "Now:" text)
b3settings::Row other_music_apps_row(const b3settings::Settings& s, const std::string& now = {});
// The Music page's "Detected" line for OTHER MUSIC APPS: on (the setting), `available` (this system has a reader),
// `reading` (a poll has run), `ok` / `why` / `what` the poller's last result (b3nowplaying::Poller::Snapshot,
// b3nowplaying::describe). *level: 0 neutral, 1 good, 2 a problem.
std::string now_playing_status(bool on, bool available, bool reading, bool ok, const std::string& why, const std::string& what, int* level);

// ---- the save files (save_store.h layout) ----
constexpr int kSaveSlots = 5;
struct SaveSlot {
    int slot = 1;
    bool sav = false;                    // profile-N.sav exists
    bool legacy = false;                 // an older profile-N folder (read when the .sav is absent)
    std::uintmax_t bytes = 0;            // the .sav's size
    std::optional<std::int64_t> modified;   // the .sav's (else the folder's) time, Unix seconds
    bool active = false;                 // active-slot.txt names it
    std::string problem;                 // saveflow: why the game cannot play it (save_store.h profile_problem); empty: fine
    struct Unsaved {                     // saveflow: a save the game wrote beside it (save_store.h unsaved_saves)
        fs::path path;
        std::optional<std::int64_t> modified;
    };
    std::vector<Unsaved> unsaved;        // newest first
};
std::vector<SaveSlot> list_save_slots(const fs::path& saves_root);
// "61 KB", "1.2 MB"
std::string format_bytes(std::uintmax_t bytes);
// "2026-11-08 21:14" in local time
std::string format_time(std::int64_t unix_seconds);

// ---- the last run (safemode.h) ----
enum class LastRun { kNone, kClean, kUnclean, kSafeNext };
struct RunSummary {
    LastRun kind = LastRun::kNone;
    std::string text;
};
// `have_file`: run_state.ini exists. kSafeNext: the next start without --safe-mode is a safe one (start_run's count).
RunSummary summarize_run(const b3settings::RunState& r, bool have_file);

// ---- launchskip1: starting the game without the launcher's window ----
// Settings::launcher_at_start off ("Start the game directly next time" on the Play page, OPEN THE LAUNCHER AT START in
// the game's PC SETTINGS > GAME > STARTING AND LOADING): starting the launcher starts the game at once, as START GAME does. The launcher
// still shows when one of these holds (the first that does is the reason given).
enum class SkipBlock {
    kNone,          // skip: start the game
    kSetting,       // OPEN THE LAUNCHER AT START is on (the default)
    kArgument,      // started with --launcher
    kHeld,          // Shift held (keyboard), or Back / Select on a controller, while it started
    kNoGameData,    // no game data set, or it is missing / incomplete
    kBadGameData,   // the game data is there but will not run (GameCheck::warning)
    kNoProgram,     // native_play is not beside the launcher
    kLastRun,       // the last run did not end cleanly (or the game is still running; safe mode may be next)
    kPortable,      // portable mode (1.3.0): the game asked for its data to be moved, or the old copy can be removed (asks first)
};
struct SkipInput {
    bool launcher_at_start = true;   // Settings::launcher_at_start
    bool launcher_argument = false;  // --launcher
    bool held = false;               // Shift / Back / Select held at the start
    bool game_ok = false;            // GameCheck::ok
    bool game_warning = false;       // !GameCheck::warning.empty()
    bool have_program = false;       // game_program() exists
    LastRun last_run = LastRun::kNone;
};
SkipBlock skip_launcher(const SkipInput& in);
// Why the launcher opened, for its Play page (empty for kNone and kSetting)
std::string skip_block_text(SkipBlock b);

// ---- the settings rows (page.h build_content) ----
// The value a row moves to on a step `dir` (-1 / +1): the in-game page's rule (clamped at the ends), or with `wrap`
// round to the other end (a click / confirm on a choice). nullopt: no change (a section, an action, a disabled row, an end).
std::optional<std::size_t> step_value(const b3settings::Row& r, int dir, bool wrap);
std::size_t value_count(const b3settings::Row& r);

// ---- the keyboard as the game maps it (src/native/input_keys.h default_key_map) ----
struct KeyRow {
    std::uint16_t scancode = 0;
    std::string menus;     // what the key does in menus ("Cross", "Up")
    std::string driving;   // ... and while driving ("Accelerate", "Steer left")
};
// One row per key in the map's order; a control bound in both contexts shows in both columns.
std::vector<KeyRow> key_rows(const std::vector<b3native::input::KeyBinding>& map);
const char* menu_name(b3native::input::Ds2 c);    // "Cross", "D-pad up", ...
const char* drive_name(b3native::input::Ds2 c);   // "Accelerate", "Boost", ...

// The launcher's tabs of the in-game page: every tab but SAVES (the launcher's own page), the HD pack's rows as the
// game shows them when a folder is chosen.
// settingsui: the launcher's window. The minimum (1024x576: the canvas at 0.8, its text still readable), and the
// size it opens at for the usable area (points) of the display it opens on: 1280x720, or 1440x810 when the area is 900
// or more high (the same 16:9 canvas at 1.125); never more than the area (smaller in both, kept 16:9).
constexpr int kMinWindowW = 1024, kMinWindowH = 576;

// settingsui: a strip of tabs `w` wide for labels of these text widths: 40 around each label, tighter (down to 16)
// before it has to scroll; when it still does not fit, the shift that puts the chosen tab in the middle, clamped so
// that the first tab starts at 0 and the last ends `kStripEdge` (the arrow's room) inside the right end. Spans are
// (x, width) from the strip's left edge before the shift is taken off.
constexpr float kStripEdge = 30;
struct StripLayout {
    std::vector<std::pair<float, float>> spans;   // x already shifted
    float total = 0, shift = 0;
    bool over = false;
};
StripLayout strip_layout(const std::vector<float>& text_w, float w, std::size_t sel);
std::pair<int, int> default_window_size(int usable_w, int usable_h);

// xboximport: `xbox_imported`: an Import Xbox textures pack is in the user data folder (the XBOX TEXTURES row)
b3settings::Content settings_content(const b3settings::Settings& s, const std::string& hd_status, bool hd_has_images,
                                     const std::vector<std::pair<std::string, std::string>>& languages = {},
                                     const std::string& language_status = {}, bool xbox_imported = false,   // 1.0.1: the imported languages
                                     const std::vector<b3settings::Env::DjVoice>& dj = {},   // 1.3.0: the DJ VOICE / TRAINING VIDEOS import rows
                                     const std::string& dj_status = {});

// ---- osk1: a text field's text and cursor ----
// UTF-8 text with a cursor (a byte offset, always at a character's start): typed text goes in at the cursor, the
// cursor moves a whole character at a time.
struct TextEdit {
    std::string text;
    std::size_t cursor = 0;
    TextEdit() = default;
    TextEdit(std::string s) : text(std::move(s)), cursor(text.size()) {}
    void set(std::string s) { text = std::move(s); cursor = text.size(); }
    void insert(std::string_view s);
    bool erase_back();      // the character before the cursor (Backspace); false: none
    bool erase_forward();   // the character after it (Delete)
    void left();
    void right();
    void home() { cursor = 0; }
    void end() { cursor = text.size(); }
    bool empty() const { return text.empty(); }
};
// The byte offset of the character before / after `at` in UTF-8 text (0 / size at the ends)
std::size_t utf8_prev(std::string_view s, std::size_t at);
std::size_t utf8_next(std::string_view s, std::size_t at);
// Pasted text for a path field: line breaks dropped, one pair of quotes around it removed
std::string clean_pasted_path(std::string s);

// ---- osk1: the launcher's on-screen keyboard (a text field with a controller, the touch screen or the mouse) ----
// Five rows 13 units wide: digits, three rows of letters with the characters paths need (- _ : \ . / ~ ,), then
// symbols, space, the cursor keys, paste and cancel; Backspace, Clear and Done at the right. Three layers: lower case,
// upper case (Shift: for one character; Shift again: caps lock) and symbols / accented letters. The model has no SDL:
// main.cpp draws it and feeds it the pad, the mouse and the keys.
enum class OskKind : std::uint8_t { kChar, kShift, kSymbols, kSpace, kBackspace, kClear, kLeft, kRight, kPaste, kSystem, kCancel, kDone };
enum class OskLayer : std::uint8_t { kLower, kUpper, kSymbols };
struct OskKey {
    OskKind kind = OskKind::kChar;
    std::string chars[3];    // kChar: the character on each layer (UTF-8)
    int row = 0;
    float x = 0, w = 1;      // in key units (a row is kOskRowUnits wide)
};
inline constexpr float kOskRowUnits = 13;
inline constexpr int kOskRows = 5;
// `system_key`: a key that shows the system's own on-screen keyboard (where SDL has one: Steam's in Gaming Mode)
std::vector<OskKey> osk_layout(bool system_key);
// What a key shows on `layer` ("a", "A", "Space", "Done", ...)
std::string osk_label(const OskKey& k, OskLayer layer);
// The key D-pad direction (dx, dy: one of them -1 / +1) moves to from key `from`: along the row (wrapping round), or
// to the next row's key nearest under the middle of this one (wrapping round from the bottom row to the top)
int osk_move(const std::vector<OskKey>& keys, int from, int dx, int dy);
// The first key with this kind (-1: none)
int osk_find(const std::vector<OskKey>& keys, OskKind kind);
// The key that types `ch` and the layer it is on (false: no key types it)
bool osk_find_char(const std::vector<OskKey>& keys, std::string_view ch, int& key, OskLayer& layer);

struct OskState {
    OskLayer layer = OskLayer::kLower;
    bool caps = false;       // Shift twice: upper case stays
    int focus = 0;           // the key the pad is on
};
enum class OskResult : std::uint8_t { kNone, kDone, kCancel, kPaste, kSystem };
// A key pressed: types into `edit` (or edits it) and returns what main.cpp must do (kPaste: the clipboard at the cursor)
OskResult osk_press(OskState& st, const std::vector<OskKey>& keys, int key, TextEdit& edit);
// The pad's Y / Triangle: lower case -> upper case (one character) -> symbols -> lower case
void osk_cycle_layer(OskState& st);

// ---- osk1: the launcher's own folder browser (a controller or the touch screen: no system file dialog) ----
struct BrowseEntry {
    std::string name;        // UTF-8, as shown
    fs::path path;
    bool folder = false;
};
struct BrowseList {
    bool ok = false;         // the folder could be read
    std::string error;
    std::vector<BrowseEntry> entries;   // folders, then the files with one of the extensions; each sorted ignoring case
    std::size_t other_files = 0;        // files not listed (not one of the extensions)
    bool more = false;                  // stopped at `limit` entries
};
// The folder's subfolders and (with `exts`, lower case without the dot) its files with one of those extensions; names
// starting with "." (hidden on Linux and macOS) are left out
BrowseList list_folder(const fs::path& dir, const std::vector<std::string>& exts, std::size_t limit = 4000);
// Whether `p` is a file with one of the extensions (lower case, no dot)
bool has_extension(const fs::path& p, const std::vector<std::string>& exts);
// The folder above (`p` itself at a drive's or the file system's root)
fs::path parent_folder(const fs::path& p);
bool is_root_folder(const fs::path& p);

struct BrowsePlace {
    std::string label;       // "Home", "SD card", "C:", ...
    fs::path path;
    bool drive = false;      // a drive, the root folder or mounted media (else a folder)
};
// Mounted drives and media under the given roots: /run/media/<user>/<label> (the Steam Deck's SD card; also
// /run/media/<label>), /media/<user>/<label>, /mnt/<label>, /Volumes/<label> (macOS). `user`: the user's own folder
// under a root shows its contents, other users' do not.
std::vector<BrowsePlace> mounted_media(const std::vector<fs::path>& roots, const std::string& user);
// The browser's starting points, in order: the folders given (the current choice, home, the user folders, the
// launcher's folder: each once and only when there), then the drives (Windows: every drive letter) or the root folder
// and mounted media (else)
std::vector<BrowsePlace> browse_places(const std::vector<BrowsePlace>& folders);

}  // namespace b3launcher

#endif
