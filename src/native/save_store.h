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

namespace b3native::save {

// The per-user data folder the port's settings live in (SDL_GetPrefPath("b3port", "native"), which holds
// pc_settings.ini): %APPDATA%\b3port\native, ~/Library/Application Support/b3port/native, or
// ${XDG_DATA_HOME:-~/.local/share}/b3port/native. Empty when the platform gives none (no HOME / APPDATA).
std::filesystem::path user_data_dir();
// user_data_dir() / "saves" (empty when user_data_dir() is).
std::filesystem::path default_saves_root();

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
bool store_profile(const std::filesystem::path& root, int slot, const Directories& dirs, std::string* error = nullptr);
// mac5 (save backups, proposal 1): before profile-N.sav is replaced, the old file is copied to
// <root>/backups/profile-N-<YYYYMMDD-HHMMSS>.sav (local time `now_unix`) and only the newest `keep` copies of that slot
// stay. Nothing when there is no old file. Returns the copy's path (empty: none made). store_profile calls it with the
// current time and kKeepBackups.
constexpr int kKeepBackups = 10;
std::filesystem::path backup_profile(const std::filesystem::path& root, int slot, std::int64_t now_unix, int keep = kKeepBackups);
bool write_active_slot(const std::filesystem::path& root, int slot, std::string* error = nullptr);

// The game's save from any importable file or folder (save_formats.h read_any), as the card directories to store:
// times missing from the format (.max) set to `now_unix`. nullopt (and *error) when there is no Burnout 3 save in it.
std::optional<Directories> import_game_save(const std::filesystem::path& from, std::int64_t now_unix,
                                            std::string* error = nullptr);
// Replaces slot `slot`'s .sav with the save in `from` (the old one kept as profile-N.sav.bak).
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
    // (the file is left for the user; status() says so).
    bool attach(int slot, std::string* error = nullptr);
    SaveCard& device() { return card_; }
    const std::filesystem::path& root() const { return root_; }
    int slot() const { return slot_; }
    ProfileSource source() const { return source_; }
    bool persist() const { return persist_; }

    // The game's thread, once per VBlank: flushes a settled save, runs a queued request.
    void tick(std::uint64_t vblank);
    // Writes the card's current contents to profile-N.sav now (when the game wrote since the last flush).
    bool flush(std::string* error = nullptr);

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
    void set_status(bool ok, std::string text);

    std::filesystem::path root_;
    SaveCard card_;
    int slot_ = 1;
    ProfileSource source_ = ProfileSource::kNone;
    bool persist_ = true;
    std::uint64_t seen_writes_ = 0, flushed_writes_ = 0, last_change_ = 0, now_ = 0, retry_at_ = 0;
    struct Shared;
    Shared* shared_;
};

}  // namespace b3native::save

#endif
