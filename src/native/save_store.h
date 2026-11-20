// save_store.h -- the user's save folder (remaster-features §8): one .sav per profile slot, the active slot, the old
// the older folder layout read and migrated, export and import (src/native/save_formats.h holds the formats).
//
// Layout of a saves root (default: user_data_dir() / "saves"; $B3NATIVE_SAVES_ROOT replaces it):
//   profile-<N>.sav     slot N's card (N 1..5), written by the port whenever the game saved
//   active-slot.txt     the slot the card shows (1 when missing)
//   profile-<N>/        the older folder layout (profile-N/BASLUS-21050/...): read when profile-<N>.sav is absent; the
//                       first save then writes profile-<N>.sav beside it (the folder is left as it was)
// Standard library only; no guest or platform state.
#ifndef B3NATIVE_SAVE_STORE_H
#define B3NATIVE_SAVE_STORE_H

#include "save_formats.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace b3native::save {

// The per-user data folder the port's settings live in (pc_settings.ini and the rest): b3io::user_dir()
// (include/b3port/user_dir.h): $B3PORT_USER_DIR, else <the program's folder>/appdata/native in portable mode, else
// %APPDATA%\b3port\native, ~/Library/Application Support/b3port/native, or ${XDG_DATA_HOME:-~/.local/share}/b3port/native.
// Empty when the platform gives none (no HOME / APPDATA).
std::filesystem::path user_data_dir();
// user_data_dir() / "saves" (empty when user_data_dir() is).
std::filesystem::path default_saves_root();

// robust2: Steam's Flatpak (com.valvesoftware.Steam) starts a non-Steam game inside its own sandbox, where XDG_DATA_HOME
// is ~/.var/app/com.valvesoftware.Steam/data: a second data folder (settings, saves, mods), apart from the one the
// launcher and the game use when started outside Steam. In a sandbox (FLATPAK_ID set, or /.flatpak-info there) whose
// view holds the host's own folder ($HOME/.local/share/b3port, given to it with `flatpak override --user
// --filesystem=~/.local/share/b3port com.valvesoftware.Steam`) and can write there, XDG_DATA_HOME is set to
// $HOME/.local/share for this process and the programs it starts, so SDL_GetPrefPath and user_data_dir() both use the
// host's folder -- unless the sandbox's own folder already holds a save (the player's saves stay where they are; with
// data in both, `two` says so). Settings alone in the sandbox's folder (the first run wrote them there, then the player
// gave the sandbox the host's folder, as PLAYING.md says) do not keep it: the host's folder is used and `left` names
// the sandbox's folder, whose settings stay behind. Call it first in main(), before any thread. Elsewhere (Windows,
// macOS, no sandbox) it does nothing.
struct SandboxData {
    bool sandbox = false;           // running in a Flatpak sandbox
    bool host = false;              // ... and the host's data folder is used (XDG_DATA_HOME set)
    bool two = false;               // ... the sandbox's own is used, and the host's holds data too
    std::filesystem::path dir;      // the data folder in use (user_data_dir())
    std::filesystem::path other;    // two: the host's data folder
    std::filesystem::path left;     // host: the sandbox's own data folder, whose settings (no save) were left behind
};
SandboxData use_host_data_in_sandbox();
// The rule itself, from what is on disk
enum class SandboxUse : std::uint8_t {
    kOwn,             // the sandbox's own folder (no sandbox, the host's not reachable / not writable, or own data only)
    kHost,            // the host's folder (XDG_DATA_HOME set to it, or already pointing there)
    kHostOwnLeft,     // the host's folder; the sandbox's own holds settings only, left behind (the player is told once)
    kOwnBothHaveData  // the sandbox's own: it holds a save and the host's holds data (the player is told there are two)
};
// own_has_saves: a save in the sandbox's own folder (only a save keeps it); own_has_settings: its pc_settings.ini or
// launcher.ini; host_has_data: anything of the player's in the host's (holds_data)
SandboxUse sandbox_use(bool sandbox, bool already_host, bool host_reachable, bool host_writable, bool own_has_saves,
                       bool own_has_settings, bool host_has_data);
// A data folder (<...>/b3port/native) that holds the player's data: pc_settings.ini, launcher.ini or a save
bool holds_data(const std::filesystem::path& data_dir);
// ... its settings: pc_settings.ini or launcher.ini
bool holds_settings(const std::filesystem::path& data_dir);

// 1.1 (a save folder of the player's choice: a cloud or network folder shared by two computers): the folder chosen on the
// launcher's Saves page, "saves_dir = <UTF-8 path>" in <data_dir>/launcher.ini (src/b3launcher/core.h Config). Empty
// when none is chosen (or no such file): the default, default_saves_root(). native_play uses it when started without
// --saves / $B3NATIVE_SAVES_ROOT, in a window.
std::filesystem::path chosen_saves_root(const std::filesystem::path& data_dir);
// A path from launcher.ini's UTF-8 text (on Windows a std::string path would be the ANSI code page).
std::filesystem::path path_from_utf8(const std::string& s);

// True when `root` holds a save of the layout above: a profile-N.sav or a profile-N folder (N 1..5).
bool has_saves(const std::filesystem::path& root);
// 1.1: the saves of `from` copied into `to` (made when missing), for a change of save folder: profile-N.sav and its
// .sav.bak, the profile-N folders, active-slot.txt and backups/. Nothing in `to` is replaced: a file already there is
// left as it is (and counted in *kept). The number of files copied; -1 (and *error) when one could not be (those
// copied stay). `from` is not changed.
int copy_saves(const std::filesystem::path& from, const std::filesystem::path& to, std::string* error = nullptr, int* kept = nullptr);

std::filesystem::path profile_file(const std::filesystem::path& root, int slot);
std::filesystem::path legacy_profile_folder(const std::filesystem::path& root, int slot);

enum class ProfileSource { kNone, kSav, kFolder };
struct LoadedProfile {
    Directories dirs;
    ProfileSource source = ProfileSource::kNone;
};
// Slot `slot`'s card contents: profile-N.sav, else the profile-N folder (`fixed_file_time` as load_profile_slot),
// else empty (a fresh card). nullopt (and *error) for a bad slot or an unreadable file / folder.
std::optional<LoadedProfile> load_profile(const std::filesystem::path& root, int slot, std::string* error = nullptr,
                                          std::optional<std::int64_t> fixed_file_time = std::nullopt);
// Writes slot `slot`'s profile-N.sav. fsrobust: `root` must be there (saves_root_ready): a save folder that has gone
// (a network folder or drive disconnected) is an error, never a new local folder in its place. Nothing is written when
// the file already holds these bytes. The new file is written and flushed to the disk beside the old one first; only
// then the old one is kept in backups/ (once per content: a retry makes no second copy) and replaced in one step. On
// any failure profile-N.sav and every backup are as they were.
bool store_profile(const std::filesystem::path& root, int slot, const Directories& dirs, std::string* error = nullptr);
// fsrobust: true when `root` is a folder. When it is not: with `create` it is made (the default folder, or one named
// on the command line); without, false and *error says the folder is not there (a chosen folder: not made).
bool saves_root_ready(const std::filesystem::path& root, bool create, std::string* error = nullptr);
// mac5 (save backups, proposal 1): before profile-N.sav is replaced, the old file is copied to
// <root>/backups/profile-N-<YYYYMMDD-HHMMSS>.sav (local time `now_unix`). fsrobust: what stays of that slot's backups
// is the newest `keep`, and the first of each of the last kKeepBackupDays days that have any (so a run of autosaves
// cannot push every older good save out). Nothing when there is no old file. Returns the copy's path (empty: none
// made). store_profile calls it with the current time and kKeepBackups.
constexpr int kKeepBackups = 8;
constexpr int kKeepBackupDays = 3;
std::filesystem::path backup_profile(const std::filesystem::path& root, int slot, std::int64_t now_unix, int keep = kKeepBackups);
// saveflow (2026-11-20): why slot `slot`'s save cannot be played: its profile-N.sav (else its older folder) does not read, or
// the game's save in it fails the game's own check (save_formats.h game_save_intact: the game would call it corrupt and
// ask for the console's browser to delete it). Empty when it loads (an empty slot included).
std::string profile_problem(const std::filesystem::path& root, int slot);
// saveflow: the launcher's "Set aside and start fresh" (a save that profile_problem refuses): slot `slot`'s profile-N.sav
// (and an older profile-N folder) renamed in `root` to profile-N-set-aside-<YYYYMMDD-HHMMSS>.sav (the folder:
// profile-N-set-aside-<stamp>): kept, never read as a profile or rotated out with the backups. The slot is then empty and
// the game starts it fresh. All or nothing: returns the new name (the .sav's, else the folder's); empty (and *error, and
// nothing changed) when there was nothing to move, a step failed, or a game on this computer plays from `root`
// (folder_lock_holder: it holds the profile and would save it back).
std::filesystem::path set_aside_profile(const std::filesystem::path& root, int slot, std::int64_t now_unix, std::string* error = nullptr);
// saveflow: the saves a game wrote beside slot `slot`'s file because profile-N.sav changed while it ran (or a save was
// imported in it): profile-N-unsaved-<stamp>.sav in `root`, newest first.
std::vector<std::filesystem::path> unsaved_saves(const std::filesystem::path& root, int slot);
// saveflow: the launcher's "Use it": `from` (one of unsaved_saves) becomes slot `slot`'s profile-N.sav as an import does
// (import_profile: the current one kept as profile-N.sav.bak and in backups/; a damaged one refused). Refused while a
// game on this computer plays from `root` (folder_lock_holder: it would save over it). False (and *error): nothing changed.
bool use_unsaved(const std::filesystem::path& root, int slot, const std::filesystem::path& from, std::int64_t now_unix,
                 std::string* error = nullptr);
// saveflow: an unreadable profile-N.sav kept aside as profile-N-unreadable-<stamp>.sav in `root` (a copy, once per
// content: one already holding these bytes is returned instead). Empty when there is no such file or no copy was made.
std::filesystem::path keep_unreadable(const std::filesystem::path& root, int slot, std::int64_t now_unix);
// saveflow: the save folder's lock, game.lock: "pid host boot start slot since" of the game playing from it, so that a
// second game on the same folder on this computer is told (the two would save over each other). Only a lock of a live
// game on this computer in this boot counts (folder_lock_live: same host name and boot, the process there and started
// when the lock says): a lock from another computer (a folder synced through the cloud or shared on the network) or
// left by a game that ended or crashed is ignored and taken over. Nothing is refused for it but the launcher's set aside.
// portable mode (1.3.0): the slot the launcher's lock names while it moves the data folder (portable.h): a game started
// meanwhile is told so (and the move stops if it writes there); a second launcher refuses to move as well
inline constexpr int kLauncherMoveSlot = -1;
struct FolderLock {
    long pid = 0;
    std::string host, boot;
    std::uint64_t start = 0;
    int slot = 0;
    std::int64_t since = 0;
};
std::optional<FolderLock> read_folder_lock(const std::filesystem::path& root);
bool folder_lock_live(const FolderLock& lock);
// Who holds `root`'s lock ("another game on this computer (file N, since <stamp>)"); empty: none that counts
std::string folder_lock_holder(const std::filesystem::path& root);
// written when no live lock is there (the game's own start); else false and *holder (folder_lock_holder's)
bool acquire_folder_lock(const std::filesystem::path& root, int slot, std::int64_t now_unix, std::string* holder = nullptr);
// removed when it is this process's
void release_folder_lock(const std::filesystem::path& root);

// active-slot.txt (replaced safely). fsrobust: `root` must be there, as store_profile.
bool write_active_slot(const std::filesystem::path& root, int slot, std::string* error = nullptr);

// The game's save from any importable file or folder (save_formats.h read_any), as the card directories to store:
// times missing from the format (.max) set to `now_unix`. nullopt (and *error) when there is no Burnout 3 save in it.
std::optional<Directories> import_game_save(const std::filesystem::path& from, std::int64_t now_unix,
                                            std::string* error = nullptr);
// Replaces slot `slot`'s .sav with the save in `from` (the old one kept as profile-N.sav.bak and in backups/; false,
// nothing changed, when it cannot be kept). `root` must be there, as store_profile. saveflow: a save the game would call
// corrupt (game_save_intact) is refused (import_game_save reads it as it is).
bool import_profile(const std::filesystem::path& root, int slot, const std::filesystem::path& from, std::int64_t now_unix,
                    std::string* error = nullptr);

// The card the game sees, kept in its profile's .sav (the card in platform_dbc.cpp's slot): slot N's contents
// as a freshly formatted card (SaveCard, save_card.h), and every save the game makes written back to
// profile-N.sav once the card has been quiet for kSettleVBlanks (the image read through its own filesystem: the
// files exactly as the game wrote them) -- with $B3NATIVE_CARD_PERSIST=1 (native_play sets it unless given); else the
// writes stay in the image (native_boot / native_check proofs on a reference folder). Export / import requests (any
// thread: the SETTINGS page) run on the game's thread in tick().
class ProfileCard {
public:
    static constexpr std::uint64_t kSettleVBlanks = 60;

    explicit ProfileCard(std::filesystem::path root);
    ~ProfileCard();   // flushes a pending save

    // false (and *error) when the slot's file cannot be read: the card is then empty and nothing is written this session
    // (the file is left for the user; status() says so). saveflow: also when the game's save in it is damaged
    // (game_save_intact: the game would call it corrupt, a dead end); the unreadable profile-N.sav is kept aside
    // (keep_unreadable), and the notice (status() beginning kNotSaved) comes again at each save the game makes.
    bool attach(int slot, std::string* error = nullptr);
    SaveCard& device() { return card_; }
    const std::filesystem::path& root() const { return root_; }
    int slot() const { return slot_; }
    ProfileSource source() const { return source_; }
    bool persist() const { return persist_; }

    // The game's thread, once per VBlank: flushes a settled save, runs a queued request.
    void tick(std::uint64_t vblank);
    // Writes the card's current contents to profile-N.sav now (when the game wrote since the last flush). A failed
    // write keeps profile-N.sav and its backups as they were (store_profile); tick() retries it every
    // kRetryVBlanks (10 s), and the first failure of a run of them, then every kRemindRetries-th (30 s), sets a status beginning
    // kSaveFailed (the window shows it as a notice: pcpage.cpp save_alert).
    bool flush(std::string* error = nullptr);
    static constexpr const char* kSaveFailed = "SAVE FAILED";
    // saveflow: a save written to profile-N.sav (the window's small "SAVED" bubble; not in the log, which has the
    // "[save] profile N saved to" line)
    static constexpr const char* kSaved = "SAVED";
    // saveflow: a save the game made that is not written to profile-N.sav (the window shows it as a notice, as kSaveFailed):
    // the file was unreadable at the start, or a save was imported in this session (the game still holds the old
    // profile), or profile-N.sav changed on the disk since the game loaded it (save_tool import, another game, a sync
    // client: it is never overwritten). In the last two the saves go to profile-N-unsaved-<stamp>.sav beside it.
    static constexpr const char* kNotSaved = "NOT SAVED";
    // saveflow: another game holds the save folder's lock (acquire_folder_lock): a notice, nothing refused
    static constexpr const char* kSaveWarning = "SAVE WARNING";
    // saveflow: where this session's saves go instead of profile-N.sav (empty: profile-N.sav)
    const std::filesystem::path& side_file() const { return side_; }
    // saveflow: tries of a read of profile-N.sav before it counts as unknown (mark(): a failed or short read)
    static constexpr int kMarkTries = 4;
    // saveflow tests: a read of profile-N.sav that comes back short this many more times (a held file, simulated)
    mutable int fail_reads_for_test = 0;
    static constexpr std::uint64_t kRetryVBlanks = 10 * kSettleVBlanks;
    static constexpr int kRemindRetries = 3;
    // fsrobust: true while the game is writing the card: writes not yet on the disk, the newest less than
    // kSettleVBlanks ago (a save spans many VBlanks; native_play holds a quit until it settles)
    bool writing() const;
    // fsrobust: the end of the session. A settled card is flushed as flush(); a card the game is still writing is not
    // written over the last good profile-N.sav (its half-written save would replace a whole one): it is kept beside it
    // as profile-N.sav.unfinished. When the save cannot be written to the save folder, a copy goes to `rescue_dir`
    // (the user data folder: local, when the save folder is a network one). Empty when all is well; else what the
    // player should be told (native_play shows it).
    // `throwaway` (a replay's temporary card, deleted at the end): nothing is written, kept beside or copied, no status
    // and no notice (empty): the replay's saves are not the player's, and a rescue copy would look like real progress.
    std::string flush_at_exit(const std::filesystem::path& rescue_dir, bool throwaway = false);

    enum class Request { kNone, kExport, kImport };
    // Any thread. The result arrives in status() (serial bumped) after the next tick().
    void post(Request r, std::filesystem::path path);
    struct Status {
        std::uint64_t serial = 0;
        bool ok = false;
        std::string text;
    };
    Status status() const;

private:
    void run(Request r, const std::filesystem::path& path);
    void set_status(bool ok, std::string text, bool log = true);
    void redirect(const std::string& why);   // saveflow: this session's saves to side_
    struct DiskMark {                          // saveflow: profile-N.sav as the game last saw it
        bool known = true;                     // false: it could not be read whole (held by another program?)
        bool exists = false;
        std::uintmax_t size = 0;
        std::uint64_t hash = 0;
    };
    // the file's mark; a failed or short read is tried again (kMarkTries, 75 ms apart: a scanner or a sync client
    // holding it for a moment, as b3io::commit_replace), then `known` is false
    DiskMark mark() const;
    bool unchanged(const DiskMark& now) const;

    std::filesystem::path root_;
    SaveCard card_;
    int slot_ = 1;
    ProfileSource source_ = ProfileSource::kNone;
    bool persist_ = true;
    bool failing_ = false;   // fsrobust: the last flush failed (the status said so; it says again when one succeeds)
    int failures_ = 0;       // fsrobust: failed flushes in the current run of them
    std::string blocked_;    // saveflow: why saves are not written this session (the notice); empty: they are
    std::filesystem::path side_;   // saveflow: where they go instead (empty: nowhere)
    DiskMark disk_;          // saveflow: profile-N.sav when the game loaded it / last wrote it
    bool locked_ = false;    // saveflow: this game holds the folder's lock
    bool throwaway_ = false;   // saveflow: B3NATIVE_CARD_THROWAWAY=1 (a replay's card): no lock, no SAVED notice, no copies
    std::uint64_t seen_writes_ = 0, flushed_writes_ = 0, last_change_ = 0, now_ = 0, retry_at_ = 0;
    struct Shared;
    Shared* shared_;
};

}  // namespace b3native::save

#endif
