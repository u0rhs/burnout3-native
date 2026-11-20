// steam.h -- Settings > Launcher > ADD TO STEAM (GitHub issue #8): the launcher as a non-Steam game in each Steam
// account's library, with its own artwork (steam_art.h, or the player's own images). No SDL; tests/test_launcher_steam.cpp.
//
// What it writes, per Steam account (<steam>/userdata/<account id>/config/):
//   shortcuts.vdf          Steam's binary VDF list of non-Steam games: one entry added (the launcher, its folder as the
//                          start folder), every existing entry kept byte for byte; an entry with the same Exe already
//                          there is left as it is (its pictures are updated instead). The first file seen is kept as
//                          shortcuts.vdf.b3backup; the new one is written to a temporary file and renamed over it.
//   grid/<appid>p.png      the library's cover (600 x 900)       grid/<appid>.png        the wide capsule (920 x 430)
//   grid/<appid>_hero.png  the game page's background            grid/<appid>_logo.png   the logo over it
//   grid/<appid>_icon.png  the icon (the entry's "icon" field)   (.jpg instead of .png: the player's own JPEG)
// Steam keeps this list in memory and writes it back when it exits, so nothing is written while Steam runs
// (steam_running): the player closes Steam first.
#ifndef B3LAUNCHER_STEAM_H
#define B3LAUNCHER_STEAM_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b3launcher::steam {

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

// The library's name for it: the game's own (the README's title, the launcher's credits)
inline constexpr const char* kAppName = "Burnout 3: Takedown";

// ---- the shortcut's id ----
// CRC-32 (IEEE, the zlib / PNG one)
std::uint32_t crc32(std::string_view s);
// Steam's id of a non-Steam game: crc32(exe + name) | 0x80000000, `exe` as stored in shortcuts.vdf (quoted). The
// grid files are named with this unsigned value; the VDF's "appid" field holds the same 32 bits as a signed int32.
std::uint32_t shortcut_appid(std::string_view exe, std::string_view name);
std::int32_t vdf_int(std::uint32_t appid);
// A path as shortcuts.vdf stores Exe and StartDir: in double quotes
std::string vdf_quoted(std::string_view path);

// ---- the grid's file names ----
enum class Art { kCover, kWide, kHero, kLogo, kIcon };
inline constexpr Art kArts[] = {Art::kCover, Art::kWide, Art::kHero, Art::kLogo, Art::kIcon};
// "<appid>p.png", "<appid>.png", "<appid>_hero.png", "<appid>_logo.png", "<appid>_icon.png" (`ext`: "png" or "jpg")
std::string grid_name(std::uint32_t appid, Art art, std::string_view ext = "png");
// What an image file holds by its first bytes: "png", "jpg", or empty (neither)
std::string image_kind(const Bytes& head);

// ---- shortcuts.vdf (binary VDF) ----
// One non-Steam game of the file: its key ("0", "1", ...), the fields the launcher reads, and its bytes [begin, end)
struct Shortcut {
    std::string key;
    std::optional<std::uint32_t> appid;   // the "appid" field (absent in files from older Steam versions)
    std::string name, exe, start_dir, icon;
    std::size_t begin = 0, end = 0;
};
struct ShortcutsFile {
    bool ok = false;
    std::string error;                 // why it was not understood (then nothing is written to it)
    Bytes bytes;                       // the file as read (an empty file: the bytes of an empty list)
    std::vector<Shortcut> entries;
    std::size_t insert_at = 0;         // the "shortcuts" map's end byte: a new entry goes just before it
};
// Reads a whole shortcuts.vdf; no bytes (no file yet) is an empty list
ShortcutsFile parse_shortcuts(const Bytes& bytes);
// The fields of a new entry (Exe and StartDir unquoted; quoted when written)
struct NewShortcut {
    std::string name = kAppName;
    std::string exe, start_dir, icon, launch_options;
};
// The id of `s` in the library: crc32 of its quoted Exe and name
std::uint32_t appid_of(const NewShortcut& s);
// The bytes of one entry (map `key`: appid, AppName, Exe, StartDir, icon, ... sortas, tags), as Steam writes them
Bytes encode_entry(const std::string& key, const NewShortcut& s);
// The file with `s` added after the existing entries (their bytes unchanged), under the next free numeric key
Bytes with_entry(const ShortcutsFile& f, const NewShortcut& s);
// The same program: quotes and surrounding spaces ignored; on Windows also case and / against \ (`fold`)
bool same_exe(std::string_view a, std::string_view b, bool fold);
bool fold_paths();   // true on Windows
// robust2: macOS App Translocation: an app opened from where it was downloaded (not moved to Applications) runs from a
// read-only random copy (/private/var/folders/.../AppTranslocation/<id>/d/...), a path that is gone the next start
bool translocated(std::string_view exe_path);
const Shortcut* find_by_exe(const ShortcutsFile& f, std::string_view exe, bool fold);
// An existing entry's id: its "appid" field, else (an older file) the one Steam computes from its Exe and name
std::uint32_t entry_appid(const Shortcut& e);

// ---- files ----
// `data` to a temporary file beside `to`, then renamed over it (std::filesystem replaces on Windows too)
bool write_atomic(const fs::path& to, const Bytes& data, std::string* why);
// `vdf` copied to <vdf>.b3backup unless that is already there (the first one seen is kept)
bool backup_once(const fs::path& vdf, std::string* why);
fs::path backup_path(const fs::path& vdf);
bool read_file(const fs::path& p, Bytes& out, std::size_t limit);

// ---- where Steam is ----
enum class Os { kWindows, kLinux, kMac };
Os this_os();
struct Places {
    fs::path home;                         // $HOME / the user's profile folder
    fs::path xdg_data_home;                // $XDG_DATA_HOME (Linux; empty: ~/.local/share)
    std::vector<fs::path> registry;        // Windows: Steam's SteamPath / InstallPath from the registry
    std::vector<fs::path> program_files;   // Windows: Program Files (x86), Program Files
};
Places this_places();   // from the environment (and the registry on Windows)
// The folders Steam may be in, most likely first (they may not exist)
std::vector<fs::path> candidate_roots(const Places& p, Os os);
// The ones that are Steam folders (a userdata or config folder in them), each once (~/.steam/steam is a link to
// ~/.local/share/Steam): canonical paths (on Windows normalized as given)
std::vector<fs::path> find_roots(const Places& p, Os os);
// The accounts that have signed in on this computer: <root>/userdata/<numeric id> (not 0), sorted
std::vector<std::string> account_ids(const fs::path& root);

// ---- is Steam running ----
// Linux: a process named "steam" under `proc` (/proc/<pid>/comm)
bool steam_running_in_proc(const fs::path& proc);
// This computer: Windows: steam.exe in the process list, or the registry's ActiveProcess pid alive; Linux: /proc;
// macOS: a process named steam_osx. Steam is never stopped by the launcher.
bool steam_running();

// ---- adding ----
struct ArtFile {
    Art art = Art::kCover;
    std::string ext = "png";   // "png" or "jpg"
    Bytes bytes;
};
struct Request {
    NewShortcut shortcut;          // icon filled in per account (its grid/<appid>_icon file) when an icon is given
    std::vector<fs::path> roots;   // find_roots
    std::vector<ArtFile> art;
};
struct AccountResult {
    fs::path config;               // <root>/userdata/<id>/config
    std::string id;
    bool ok = false;               // in the library now (added, or there already)
    bool added = false;            // a new entry written (false: it was there)
    std::uint32_t appid = 0;
    std::string why;               // !ok: why not
    std::string art_why;           // ok, but a picture could not be written
};
struct Result {
    bool steam_found = false;
    std::vector<AccountResult> accounts;
    std::size_t ok_count() const;
    std::size_t added_count() const;
};
Result add(const Request& r);
// The result in the launcher's words (one line; English, translated by the launcher's tables)
std::string summary(const Result& r);

}  // namespace b3launcher::steam

#endif
