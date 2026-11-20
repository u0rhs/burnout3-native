// portable.h -- portable mode (1.3.0): the user data folder moved between this computer's folder and <install>/appdata
// (the folder is the marker: include/b3port/user_dir.h). Standard library only; the launcher does every move (the
// game's PORTABLE MODE row only asks for one at the launcher's next start: request_file).
//
// A move never loses the data it starts from:
//   1. everything in the data folder is copied into a .partial folder beside the target (<install>/appdata.partial,
//      or <...>/b3port/native.partial), each file's bytes hashed while read;
//   2. every copied file is read back and checked by size and hash, and the source listed again: the same files, sizes
//      and modification times as when they were copied (else something wrote there meanwhile: the move stops);
//   3. the .partial folder is renamed into place (one step: on, appdata/ appears and the install is portable);
//   4. the folder moved from is renamed to a dated name (native.moved-<date>, appdata.moved-<date>), not deleted: the
//      launcher offers "Remove the old copy" once the game has started from the new folder (Record).
// A failure in 1-3 removes the .partial folder and leaves the source as it was; a move cut short (a crash, the power)
// leaves only a .partial folder, removed at the next start (clean_partials). Turning off with data already in this
// computer's folder: the player chooses which to keep (Keep); the other is set aside under a dated name, never merged.
// A save folder the player chose elsewhere (launcher.ini saves_dir outside the data folder) is not moved.
#ifndef B3LAUNCHER_PORTABLE_H
#define B3LAUNCHER_PORTABLE_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "b3port/user_dir.h"

namespace b3launcher::portable {

namespace fs = std::filesystem;

// The row's state: hidden (macOS, an AppImage, a Flatpak), shown but disabled (why), or usable (on: the install is
// portable now)
struct Status {
    bool shown = false;
    bool usable = false;
    bool on = false;
    std::string why;   // !usable: the reason, in words
};
// exe_dir: the launcher's folder; platform: b3io::platform_user_dir(); override_dir: a data folder given (1: --pref,
// 2: $B3PORT_USER_DIR set by the player; 0 none);
// writable: the install folder can be written (b3io::dir_writable); running: the game plays (its lock, or the launcher's
// own child)
Status status(const fs::path& exe_dir, bool apple, bool appimage, bool flatpak, int override_dir, bool writable, bool running);

// The game's request (its PORTABLE MODE row): b3io::read_portable_request (include/b3port/user_dir.h)

// What a move would do
struct Plan {
    bool to_portable = false;
    fs::path from, to;               // the data folders (.../native)
    fs::path from_top;               // what is renamed away at the end: on, the data folder itself; off, <install>/appdata
    fs::path partial_top;            // the copy's folder, renamed to `to_top`: <install>/appdata.partial or native.partial
    fs::path partial;                // where the data folder goes in it (partial_top/native, or partial_top itself)
    fs::path to_top;                 // <install>/appdata, or the platform's data folder
    std::uintmax_t bytes = 0;        // the data's size
    std::size_t files = 0;
    bool target_has_data = false;    // off: this computer's folder holds files already (Keep decides)
    std::uintmax_t free = 0;         // free space where the copy goes
    bool enough_space = true;        // free >= bytes + margin (kMargin + 5 %)
};
inline constexpr std::uintmax_t kMargin = 64u << 20;
// exe_dir: the launcher's folder; platform: this computer's data folder. nullopt (and *why) when there is nothing to
// do or it cannot be done (no data folder, the target missing a parent ...)
// path_limit: the longest path the copy may make (b3io::path_limit(): Windows' 259; 0 none): a longer one refuses the
// plan, naming the cause and the path
std::optional<Plan> plan(bool to_portable, const fs::path& exe_dir, const fs::path& platform, std::string* why = nullptr,
                         std::size_t path_limit = b3io::path_limit());

enum class Keep : std::uint8_t {
    kMoved,      // the data being moved (off: this computer's folder's own data set aside, native.aside-<date>)
    kExisting    // off: this computer's folder's data; the portable data set aside (appdata.moved-<date>), not copied
};

// Progress (the launcher's modal polls it; another thread runs move)
struct Progress {
    std::atomic<std::uint64_t> done{0}, total{0};   // bytes copied and checked (each counted twice: copy, then check)
    std::atomic<int> step{0};                       // 0 copying, 1 checking, 2 renaming
    std::atomic<bool> cancel{false};                // stops before the rename (the .partial folder removed)
};

// The tests' failure points: return true to fail there
enum class Fault : std::uint8_t { kCopy, kVerify, kRenameIn, kRenameAway, kChanged };
using FaultHook = std::function<bool(Fault, const fs::path&)>;

struct Result {
    bool ok = false;
    std::string why;                 // !ok: what went wrong (the source is as it was)
    fs::path old_kept;               // the folder moved from, under its dated name (empty: it could not be renamed: `note`)
    fs::path aside;                  // off with Keep::kMoved: this computer's earlier data; kExisting: the portable data
    std::string note;                // done, with a remark (the old folder could not be renamed away)
    std::size_t files = 0;
    std::uintmax_t bytes = 0;
};
// `stamp`: the date in the dated names (YYYYMMDD-HHMMSS); a name taken gets -2, -3 ...
Result move(const Plan& p, Keep keep, const std::string& stamp, Progress* progress = nullptr, const FaultHook& fault = nullptr);

// <dir>.partial folders a move cut short left (in the install folder and beside this computer's data folder): removed.
// The number removed.
int clean_partials(const fs::path& exe_dir, const fs::path& platform);

// After a move: <new data dir>/portable-move.ini names the old copy kept, and whether the game started from the new
// folder since (the launcher offers its removal then)
struct Record {
    fs::path old_kept, aside;
    bool started = false;            // the game was started from the new folder
    bool declined = false;           // the player chose Keep it: not asked again
};
inline constexpr const char* kRecordFile = "portable-move.ini";
std::optional<Record> read_record(const fs::path& data_dir);
bool write_record(const fs::path& data_dir, const Record& r);
void clear_record(const fs::path& data_dir);
// The old copy removed (only a folder whose name ends in .moved-<...> or .aside-<...>, as move made); false and *why
bool remove_old(const fs::path& old_kept, std::string* why = nullptr);

// A folder's files and bytes (the plan's count; symbolic links are not followed)
void measure(const fs::path& dir, std::size_t& files, std::uintmax_t& bytes);

// launcher.ini's saves_dir after a move: a folder inside `from` is the same place inside `to`; elsewhere, unchanged
std::string rebase_saves_dir(const std::string& saves_dir_utf8, const fs::path& from, const fs::path& to);

// A local date stamp for the dated names
std::string stamp_now();

}  // namespace b3launcher::portable

#endif
