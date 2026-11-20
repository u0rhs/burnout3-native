// mods.h -- the mod loader's core (mod kit part 1, docs/modding/DESIGN.md §1; src/b3mod/mods.cpp, tests/test_mods.cpp).
//
// A mod is a folder under the user's mods folder (<user data folder>/mods/<folder>/) with a manifest, mod.ini:
//
//   [mod]
//   id = hello-text            the mod's name in mods.ini, --mods and the logs: a-z 0-9 . _ - (1..64, no leading '.')
//   name = Hello text          shown in the launcher (default: the id)
//   version = 1.0.0            any text without spaces; part of the mod set's hash
//   author = someone
//   description = One line about what it does.
//   api = 1                    the port's mod API version it was made for (kApiVersion); newer: refused, missing: 1
//   priority = 0               where a newly installed mod goes in the load order (higher: nearer the top)
//   [files]
//   root = data                the overlay folder inside the mod (default "data"): its paths are disc paths
//   patches = patches          b3patch deltas (docs/modding/b3patch.md) at disc path + ".b3patch" (default "patches"):
//                              each applied to the user's own disc file when the overlay is built (see PatchEnv)
//   [textures]
//   pack = textures            a PCSX2-format HD replacement folder (b3asset/hdpack.h) for the renderer; none by default
//   [requires]
//   other-mod = 1.2            another mod this one needs on, here version 1.2 or newer ("" or "*": any version)
//   [code]                     sdk2b: a code mod (api = 2): its library per system and two statements
//   windows = bin/win64/x.dll  (include/b3mod/codemod.h, sdk/include/b3mod_api.h)
//   linux = bin/linux-x64/x.so
//   macos = bin/macos-arm64/x.dylib
//   gameplay = no              yes: it changes physics / AI / scoring
//   platform = no              yes: it may hook the platform tier (only with Developer mods on: pc_settings.ini)
//   [tuning]
//   preset = tuning/x.ini      tune4: a tuning preset (include/b3tune/tune.h, docs/modding/tuning.md): applied to the
//                              game's tuning variables as they register; the mod then changes gameplay, whatever else
//                              it does. The file's bytes are part of the mod's hash ("tuning <path> <size> <fnv>")
//   [script]                   lua6: a script mod (api = 3): a Lua 5.4 script run by native_play (src/native/script_mods.h)
//   main = scripts/main.lua    the script native_play runs; require() finds the others in its folder
//   gameplay = no              no: the script only reads and draws (writes refused); yes / not given: it may change the game
//   budget = 1000000           Lua instructions per VBlank before the script is stopped (default 1000000)
//   memory = 64                megabytes of Lua memory before the script is stopped (default 64)
//
// The overlay: <mod>/<root>/DATA/GLOBALUS.BIN stands in for the disc's DATA\GLOBALUS.BIN. Names match ASCII-case-
// insensitively, as the disc's do. Of the active mods, the first in the load order that has a path supplies it (its
// size and its bytes); every other path is the disc's. A mod may add a path the disc does not have. Nothing is ever
// written to the disc or to a mod.
//
// The load order and the on / off switches are <user data folder>/mods.ini (the launcher's Mods page writes it):
//
//   [order]
//   hello-text = on            first line: the highest priority
//   other-mod = off
//
// No mods active = no overlay: the disc's files exactly as without this code. The active set has a hash (set_hash:
// every mod's id, version and files, with their sizes and contents), which native_play prints at the start of each
// run (the run log, so bug reports carry it) and writes into pad recordings.
//
// Standard library only (no SDL, no game code): native_play, the launcher and the tests share it.
#ifndef B3MOD_MODS_H
#define B3MOD_MODS_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace b3mod {

namespace fs = std::filesystem;

constexpr int kApiVersion = 3;   // the mod API this port provides (DESIGN.md §7): 1 data mods, 2 code mods (sdk2b), 3 script mods (lua6)
constexpr int kCodeApi = 2;      // the least api a mod with [code] gives
constexpr int kScriptApi = 3;    // the least api a mod with [script] gives
constexpr const char* kManifestName = "mod.ini";

// ---- the manifest ----
struct Requirement {
    std::string id;
    std::string min_version;   // "": any version
};
// sdk2b: [code] -- the mod's library per system (paths inside the mod) and the author's statements
struct CodeSpec {
    bool present = false;      // the manifest has a [code] section
    std::string windows_path, linux_path, macos_path;   // (the macro `linux` of GNU C++ forbids that name)
    bool gameplay = false;     // gameplay = yes: it changes physics / AI / scoring (DESIGN.md §6)
    bool platform = false;     // platform = yes: it may hook the platform tier (only with the user's Developer mods)
    // the path for a system ("windows", "linux", "macos"); "" when not given
    const std::string& path_for(std::string_view system) const {
        static const std::string none;
        return system == "windows" ? windows_path : system == "linux" ? linux_path : system == "macos" ? macos_path : none;
    }
};
// lua6: [script] -- a Lua script native_play runs (src/native/script_mods.h, docs/modding/lua-reference.md)
struct ScriptSpec {
    static constexpr std::uint64_t kDefaultBudget = 1000000, kMinBudget = 10000, kMaxBudget = 50000000;   // per VBlank
    static constexpr unsigned kDefaultMemoryMb = 64, kMaxMemoryMb = 512;
    bool present = false;      // the manifest has a [script] section
    std::string main_file;     // [script] main: the script, a path inside the mod ("scripts/main.lua")
    bool gameplay = true;      // gameplay = no: the script only reads and draws (its writes are refused)
    bool gameplay_given = false;
    std::uint64_t budget = kDefaultBudget;   // Lua instructions per VBlank
    unsigned memory_mb = kDefaultMemoryMb;   // its Lua memory
    // unsafe = yes: the script may write the game's memory (b3.mem.write*) and call the game's functions (b3.call, only
    // the ones `calls` lists). That reaches the game's own code with the script's values, so it is outside the
    // sandbox's promise: the launcher asks the user's permission as for a code mod (trust over mod.ini and the script
    // folder). Without it a script reads, draws and hooks only.
    static constexpr std::size_t kMaxCalls = 64;
    bool unsafe = false;
    std::vector<std::uint32_t> calls;        // [script] calls: the game functions b3.call may call (guest addresses)
    // the folder of `main` inside the mod ("scripts": a main at the mod's top is refused): every file below it is in the
    // mod hash, and require() finds a script's other files there
    std::string folder() const {
        const std::size_t s = main_file.find_last_of('/');
        return s == std::string::npos ? std::string() : main_file.substr(0, s);
    }
};
// The system this program was built for: "windows", "linux" or "macos" (the [code] key it loads)
const char* host_system();

struct Manifest {
    std::string id, name, version, author, description;
    int api = 0;               // 0: not given (read as 1, with a warning)
    int priority = 0;
    std::string files_root = "data";
    std::string files_patches = "patches";   // [files] patches: the b3patch folder inside the mod
    std::string textures;                    // [textures] pack: the HD replacement folder inside the mod ("": none)
    std::vector<Requirement> requirements;   // [requires], in the manifest's order
    CodeSpec code;                           // sdk2b: [code]
    ScriptSpec script;                       // lua6: [script]
    std::string tuning_preset;               // tune4: [tuning] preset (a path inside the mod; "": none)
};
struct ParsedManifest {
    Manifest m;
    std::vector<std::string> errors;     // the mod cannot be used
    std::vector<std::string> warnings;   // it can, but something is off
};
ParsedManifest parse_manifest(std::string_view text);
bool valid_id(std::string_view id);
// Versions as mods write them ("1", "1.2", "1.10.3", "2.0-beta"): dot-separated parts compared in turn, a numeric part
// by its value, any other part as text (a number before text); a missing part is 0 ("1.2" == "1.2.0"). <0, 0, >0.
int compare_versions(std::string_view a, std::string_view b);

// ---- paths ----
// A path as the game names a disc file ("cdrom0:\DATA\GLOBALUS.BIN;1", "Data\GlobalUs.bin", "/DATA/GLOBALUS.BIN") ->
// the overlay's key "DATA/GLOBALUS.BIN" (the device and the ";1" version dropped, '/' separators, ASCII upper case).
// "" when it is not a disc path or would leave the disc's root: ".." or "." components, another device or a drive
// letter, an empty name.
std::string disc_key(std::string_view path);
// A path inside a mod's folder as a manifest names one (the overlay root): relative, no ".." / "." components, no
// drive or device, no leading separator. "" is not one.
bool safe_relative(std::string_view rel);

// ---- the installed mods ----
struct Mod {
    fs::path dir;              // <mods folder>/<folder>
    std::string folder;        // the folder's name (UTF-8)
    Manifest manifest;
    std::vector<std::string> errors, warnings;
    bool usable() const { return errors.empty(); }
};
// Every folder of `mods_root` with a mod.ini, by folder name. A folder without one is skipped, and so is a folder whose
// name starts with '.' (hidden; an archive being installed); a later folder with an id already taken gets an error.
// Empty when the folder is not there.
std::vector<Mod> discover(const fs::path& mods_root);
const Mod* find_mod(const std::vector<Mod>& mods, std::string_view id);

// ---- mods.ini: the load order ----
struct OrderEntry {
    std::string id;
    bool enabled = false;
    bool operator==(const OrderEntry&) const = default;
};
struct Order {
    std::vector<OrderEntry> entries;   // first: the highest priority
    // (Developer mods is not here: it is pc_settings.ini's developer_mods, b3settings::Settings, one switch for code
    // mods' platform hooks and the tuning console; a mods.ini [options] section is ignored)
    bool operator==(const Order&) const = default;
};
fs::path mods_folder(const fs::path& pref);   // <pref>/mods
fs::path order_file(const fs::path& pref);    // <pref>/mods.ini
Order order_from_ini(std::string_view text);  // unknown lines ignored; a repeated id: the first one counts
std::string order_to_ini(const Order& o);
bool load_order(const fs::path& path, Order& o);   // false: no file (o unchanged)
bool save_order(const fs::path& path, const Order& o, std::string* why = nullptr);
// The saved order against what is installed: the saved entries whose mod is there, in their order, then the mods it
// does not name (newly installed: off), the higher manifest priority first, then by id.
Order merged_order(const Order& saved, const std::vector<Mod>& found);
std::vector<std::string> enabled_ids(const Order& o);
// "a,b , c" -> {"a", "b", "c"}; "none" or "" -> {}
std::vector<std::string> split_ids(std::string_view list);

// ---- b3patch (docs/modding/b3patch.md): the reader the loader applies patches with ----
// (b3modtool makes patches: include/b3modtool/modtool.h make_patch)
constexpr const char* kPatchSuffix = ".b3patch";
struct PatchInfo {
    std::uint64_t src_size = 0, src_fnv = 0, dst_size = 0, dst_fnv = 0;
    std::uint64_t copied = 0, inserted = 0;   // target bytes from copy runs / from insert runs
    std::size_t ops = 0;
};
// The header and the op stream checked (no source needed). false: `why` says what is wrong.
bool read_patch_info(const std::vector<std::uint8_t>& patch, PatchInfo& info, std::string* why = nullptr);
// `src` must be the file the patch was made from (size and FNV-1a 64); the result is checked against the patch's
// target size and hash.
bool apply_patch(const std::vector<std::uint8_t>& src, const std::vector<std::uint8_t>& patch, std::vector<std::uint8_t>& out,
                 std::string* why = nullptr);

// ---- the overlay ----
struct OverlayFile {
    std::string key;           // disc_key form
    fs::path host;             // the file that answers: the mod's file, or a patch's result in the cache
    std::uint64_t size = 0;
    std::uint64_t fnv = 0;     // FNV-1a 64 of its bytes (0 when not hashed)
    // a patched file: the mod's .b3patch (host is its result, made from the user's disc file); empty: a plain file
    fs::path patch;
    std::uint64_t patch_size = 0, patch_fnv = 0;   // the patch file's own size and FNV-1a 64 (what the mod hash takes)
};
struct ActiveMod {
    std::string id, version, name;
    fs::path dir, root;
    std::vector<OverlayFile> files;   // by key (plain and patched)
    std::size_t patched = 0;          // how many of `files` come from patches
    fs::path textures;                // [textures] pack: the mod's HD replacement folder (empty: none, or not there)
    std::uint64_t hash = 0;           // mod_hash(), and with a tuning preset its line (tune4)
    fs::path tuning;                  // tune4: the tuning preset file ("": none)
    std::string tuning_rel;           // as the manifest names it
    std::uint64_t tuning_size = 0, tuning_fnv = 0;
    bool changes_gameplay() const { return !tuning.empty(); }   // tune4: a preset changes gameplay by nature (DESIGN.md §6)
    std::vector<std::string> notes;   // files left out (a link out of the mod, two names differing only in case, > 4 GB)
    // sdk2b: a code mod's libraries ([code]); `library` this system's (absolute; empty: none given). Every library the
    // manifest names is in the mod hash (code_lines), so one mod folder hashes the same on every system.
    CodeSpec code;
    fs::path library;
    std::string code_lines;           // "code <system> <path> <size> <fnv 16 hex>\n" ("... missing\n") per library
    // lua6: a script mod's [script] and every file of its script folder (script_lines, in the mod hash)
    ScriptSpec script;
    std::string script_lines;         // "script <path> <size> <fnv 16 hex>\n" per file of the folder, by path
};
// media1: a mod that changes gameplay by any of its parts (DESIGN.md §6): a tuning preset, a [code] library marked
// gameplay = yes, a [script] that may write the game (gameplay not "no"). Times and ghosts made with such a set are
// kept apart from the plain game's (src/b3replay/records.h). (ActiveMod::changes_gameplay is the preset alone: the
// "gameplay tuning" notes of recordings.)
inline bool changes_gameplay_any(const ActiveMod& m) {
    return m.changes_gameplay() || (m.code.present && m.code.gameplay) || (m.script.present && m.script.gameplay);
}
// What applying a mod's patches needs (Overlay::build): the user's own disc file for a key, and the cache folder for
// the results. A patch is checked against its source's size and FNV-1a 64 (a mismatch: the whole mod is left out, with
// a message in Overlay::errors()); its result goes to `cache` as "<patch fnv>-<source fnv>.bin" (reused while its size
// and hash are the target's; written through a temporary file and a rename; the folder is safe to delete) and is served
// like any overlay file. Default cache: <mods folder>/.cache (patch_cache).
struct PatchEnv {
    std::function<bool(const std::string& key, std::vector<std::uint8_t>& out)> read_disc;   // empty: patches are not applied
    fs::path cache;
};
fs::path patch_cache(const fs::path& mods_root);   // <mods folder>/.cache
class Overlay {
public:
    // `mods` in priority order (the first wins). hash_contents false: no plain file is read (fnv 0; the launcher's view,
    // tests of the order). Patches: applied with `env` (a mod with patches and no env, or a patch that does not apply,
    // is left out: errors()); without `env` and with hash_contents false they are listed by their header alone (host
    // empty: nothing to read).
    static Overlay build(const std::vector<const Mod*>& mods, bool hash_contents = true, const PatchEnv* env = nullptr);
    bool empty() const { return mods_.empty(); }
    const std::vector<ActiveMod>& mods() const { return mods_; }
    // The mods build left out, and why ("<id>: patches/DATA/X.b3patch: ...: the mod is off for this run"), in order
    const std::vector<std::string>& errors() const { return errors_; }
    const std::vector<std::string>& left_out() const { return left_out_; }   // their ids
    // [textures]: the active mods' HD replacement folders, in priority order (the first wins over later ones and over
    // the user's own pack, which the renderer puts last)
    std::vector<fs::path> texture_packs() const;
    // The file for a disc path (any form disc_key takes), or null: the disc's own. `who`: the mod that supplies it.
    const OverlayFile* find(std::string_view disc_path, const ActiveMod** who = nullptr) const;
    // The file that answers for each path (the winner of each key), by key: what a reader of the disc that is not the
    // game (the renderer, the launcher) is given so it sees the same files (b3asset/discfs.h set_overlay).
    std::vector<std::pair<std::string, fs::path>> winners() const;
    // The hash of the whole set: "none" sets hash 0. See set_hash_text for what goes in.
    std::uint64_t set_hash() const { return set_hash_; }
    std::size_t file_count() const { return index_.size(); }   // distinct paths supplied

private:
    std::vector<ActiveMod> mods_;
    std::vector<std::string> errors_, left_out_;
    std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> index_;   // key -> (mod, file), first mod wins
    std::uint64_t set_hash_ = 0;
};

// The hashes, FNV-1a 64 over text (so another tool can recompute them):
//   mod_hash  over "file <key> <size> <fnv 16 hex>\n" per plain file and "patch <key> <patch size> <patch fnv 16 hex>\n"
//             per patched file, by key (a patch names its source and target hashes, so the same patch on the same disc
//             gives the same hash on every machine); then, for a code mod (sdk2b), "code <system> <path> <size> <fnv 16
//             hex>\n" per library [code] names (windows, linux, macos; "code <system> <path> missing\n" when the file is
//             not there); then, for a script mod (lua6), "script <path> <size> <fnv 16 hex>\n" per file of the script's
//             folder (the folder of [script] main), by path ("/" separators, as written in the mod), and "script <main>
//             missing\n" when main is not there; then, for a mod with a tuning preset (tune4), "tuning <path> <size> <fnv
//             16 hex>\n". In that order: files and patches, code lines, script lines, the tuning line (mod_hash takes the
//             first three; Overlay::build chains the tuning line onto it)
//   set_hash  over "b3mods api 1\n" (the hash text's own version, not changed by api 2) then "mod <id> <version>
//             <mod_hash 16 hex>\n" per mod, in priority order
std::uint64_t fnv1a64(const void* data, std::size_t n, std::uint64_t h = 0xCBF29CE484222325ull);
std::uint64_t mod_hash(const std::vector<OverlayFile>& files, const std::string& code_lines = {}, const std::string& script_lines = {});
std::string set_hash_text(const std::vector<ActiveMod>& mods);
// media1: the hash naming a set's own ghosts / best times folder (src/b3replay/records.h): FNV-1a 64 over "b3mods
// gameplay 1\n" then "mod <id> <version> <mod_hash 16 hex>\n" per mod that changes gameplay (changes_gameplay_any), in
// priority order, as 16 hex; "" when none does. A mod that does not change gameplay (textures, sounds, a read-only
// script) added, removed or moved leaves it as it was.
std::string gameplay_set_hash(const std::vector<ActiveMod>& mods);
std::string hex64(std::uint64_t v);
// A path as UTF-8 text (messages; on Windows path::string() would be the ANSI code page) and back
std::string path_utf8(const fs::path& p);
fs::path path_from_utf8(const std::string& s);
// FNV-1a 64 of a file's bytes (false: unreadable)
bool hash_file(const fs::path& p, std::uint64_t& fnv, std::uint64_t& size);

// One line naming the set: "none", or "hello-text 1.0.0 (1 file), other 2 (3 files); set hash 0123456789abcdef"
std::string describe(const Overlay& o);

// What native_play runs with: `ids` (in priority order) looked up in `found`. Each id must be installed and usable,
// else an error names it (the rest still selected). A repeated id counts once.
// Requirements ([requires]): a selected mod whose required mod is not selected too (not installed, off, not usable, or
// older than the version asked for) is left out, with an error naming the reason; leaving it out may leave out the mods
// that require it in turn. The order stays the one given (the list decides which file wins, requirements do not move a
// mod): a mod listed after a mod it requires gets a note, since where both have a file, the required mod's wins.
struct Selection {
    std::vector<const Mod*> mods;
    std::vector<std::string> errors;
    std::vector<std::string> notes;
};
Selection select(const std::vector<Mod>& found, const std::vector<std::string>& ids);

// ---- file conflicts (the launcher's Mods page) ----
// A path more than one mod of the set supplies: `winner` (the first in priority order) answers, `hidden` are the others
// in priority order. By key.
struct FileConflict {
    std::string key;
    std::string winner;
    std::vector<std::string> hidden;
};
std::vector<FileConflict> conflicts(const Overlay& o);
// One mod's side of them, for its card: the paths where it wins over another mod, and where another one hides its file
struct ModConflicts {
    std::vector<std::pair<std::string, std::string>> wins;      // (key, the first mod it hides)
    std::vector<std::pair<std::string, std::string>> hidden;    // (key, the mod that wins)
};
ModConflicts conflicts_of(const std::vector<FileConflict>& all, std::string_view id);

// ---- the renderer's view of the set ----
// native_play hands its active set to the renderer (b3render, in the same process) as renderer arguments, and the
// renderer builds the same overlay for the disc files it reads itself (tracks, cars, textures):
//   --mods-dir "<mods folder>" --mods a,b --mods-hash <set hash 16 hex>
// "" for an empty set (no arguments: the renderer reads the disc alone). A folder whose name holds '"' cannot be
// passed (the renderer's arguments have no escapes): "" with `why` set.
std::string renderer_args(const fs::path& mods_root, const Overlay& o, std::string* why = nullptr);

}  // namespace b3mod

#endif
