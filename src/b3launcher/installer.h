// installer.h -- the one-click updater's install step (no SDL, no network; tests/test_launcher_installer.cpp).
//
// A release package (the Windows .zip of tools/release/package_win.sh, the Linux .tar.gz of package_linux.sh) holds
// every file under ONE top folder, burnout3-native-<version>-<platform>/. install() puts its files in place of the
// installed ones:
//   1. unpack: into <install>/.update-staging/files/ (emptied first), the top folder left out. Every entry is checked
//      first (b3mod::unsafe_entry_name: no absolute paths, "..", drive letters, device names ...); refused: links,
//      devices, anything else that is not a plain file or folder, anything outside the one top folder (a file beside
//      it, a second top folder), names that differ only in case, a file and a folder of one name, names the updater
//      keeps for itself at the top (previous-version, .update-*, appdata*), sizes and counts over Limits. The .tar.gz is ustar /
//      GNU tar (GNU long names and pax path / size records understood); its files keep their executable bits (POSIX).
//   2. swap, with renames only (a running program's executable and loaded libraries can be renamed on NTFS and POSIX
//      file systems, never overwritten): the old previous-version/ goes aside, then for each file of the package the
//      installed file of the same relative path (if any) moves to previous-version/<path> and the new one into place.
//      Files the install has that the package lacks stay as they are (a player's mods, notes). previous-version/ then
//      holds only the version just replaced, and its .update-manifest lists what was swapped.
//   3. all or nothing: every rename is written to a plan (.update-staging/swap-plan) before the first one; if one
//      fails, the done ones are undone in reverse (the old previous-version/ comes back too). An install cut short (the
//      power went) is undone the same way by clean_leftovers() at the next launcher start.
//   4. the staging folder and the old previous-version/ go to .update-trash/, which is deleted.
// roll_back() swaps previous-version/ and the installed files back (files new in the update move into previous-version/
// too), so a second roll_back() redoes the update.
#ifndef B3LAUNCHER_INSTALLER_H
#define B3LAUNCHER_INSTALLER_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace b3launcher::installer {

namespace fs = std::filesystem;

struct Result {
    bool ok = false;
    std::string why;          // not ok: the reason (UTF-8, one line)
    std::size_t files = 0;    // files swapped
};

struct Limits {
    std::uint64_t max_file = 1ull << 30;    // bytes in one file (1 GB)
    std::uint64_t max_total = 4ull << 30;   // bytes in all (4 GB)
    std::size_t max_entries = 20000;        // files and folders
};

// The folders and files the updater keeps in the install folder
inline constexpr const char* kStaging = ".update-staging";
inline constexpr const char* kTrash = ".update-trash";
inline constexpr const char* kPrevious = "previous-version";
inline constexpr const char* kManifest = ".update-manifest";   // in previous-version/
// portable mode (1.3.0): the player's data folder beside the launcher (include/b3port/user_dir.h kPortableFolder). Never
// in a package (one that has it is refused), never touched by an install or a roll back: only the package's own files move
inline constexpr const char* kPortableData = "appdata";

// Installs `package` (.zip or .tar.gz, told apart by their first bytes) over `install_dir`.
Result install(const fs::path& package, const fs::path& install_dir, const Limits& limits = {});
// previous-version/ holds a complete set of replaced files (its manifest reads and every file it names is there)
bool can_roll_back(const fs::path& install_dir);
// The version roll_back() would bring back ("1.1.0+1a2b3c4d" from the old VERSION.txt, else the package folder's
// name); "" when there is nothing to roll back.
std::string previous_version(const fs::path& install_dir);
// The installed version: "1.2.1+1a2b3c4d" from `install_dir`'s VERSION.txt (macOS: the app's Contents/Resources,
// beside Contents/MacOS); "" when there is none
std::string installed_version(const fs::path& install_dir);
// Why the update cannot be installed over `install_dir`, before anything is downloaded; "" when it can. Refused: an
// AppImage (`appimage`: its APPIMAGE variable; the folder is the image's read-only mount), a Flatpak (`flatpak`: the
// sandbox's /app is read-only; Flatpak updates it), a folder this user cannot write (/opt, a read-only medium: a test
// file is made and removed).
std::string install_blocker(const fs::path& install_dir, bool appimage, bool flatpak);
// Swaps previous-version/ and the files that replaced it (same rules as install: renames, all or nothing).
Result roll_back(const fs::path& install_dir);
// An interrupted install / roll back undone, .update-staging and .update-trash removed. Called at the launcher's start.
// (A plan that cannot be undone, e.g. a file still locked, is kept for the next start.)
void clean_leftovers(const fs::path& install_dir);

}  // namespace b3launcher::installer

#endif
