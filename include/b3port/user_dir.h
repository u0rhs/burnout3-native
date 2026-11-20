// user_dir.h -- the one place that decides the user data folder (portable mode, 1.3.0). Header only, standard library
// and the C runtime (the Win32 call it needs is declared below, as fileio.h does). Every program of the port asks it:
// the game (save_store.h user_data_dir), its window (b3replay), the launcher and the tools.
//
// The folder holds everything of the player's: pc_settings.ini, launcher.ini, controls.ini, run_state.ini, saves/ (and
// its backups), mods/, logs/, replays/, screenshots/, languages/, the HD warm cache, xbox-textures/, bug reports.
//
//   1. an explicit folder wins: $B3PORT_USER_DIR (the launcher's --pref sets it for the game it starts);
//   2. else, portable: <the program's folder>/appdata/native when <the program's folder>/appdata is a folder, and this is
//      not macOS (an app bundle is never written into), an AppImage (a read-only mount) or a Flatpak sandbox (/app is
//      read-only; robust2's host-folder redirect decides there);
//   3. else the platform's own: %APPDATA%\b3port\native, ~/Library/Application Support/b3port/native,
//      ${XDG_DATA_HOME:-~/.local/share}/b3port/native (SDL_GetPrefPath("b3port", "native") gives the same folder).
//
// The marker is the folder itself: no setting says "portable", so nothing can disagree with what is on the disk. Two
// installs each have their own appdata/ (the program's folder decides); a non-portable install beside a portable one
// uses the platform's folder. The launcher and the game are in the same folder, so both always agree, whatever the
// working directory.
#ifndef B3PORT_USER_DIR_H
#define B3PORT_USER_DIR_H

#include "b3port/fileio.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef _WINDOWS_   // <windows.h> not included: as libloaderapi.h declares it (HMODULE is HINSTANCE__*)
struct HINSTANCE__;
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(HINSTANCE__* module, wchar_t* name, unsigned long size);
#endif
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace b3io {

inline constexpr const char* kPortableFolder = "appdata";            // <program folder>/appdata: the marker
inline constexpr const char* kUserDirEnv = "B3PORT_USER_DIR";        // rule 1

// <exe_dir>/appdata and the data folder in it
inline fs::path portable_root(const fs::path& exe_dir) { return exe_dir / kPortableFolder; }
inline fs::path portable_dir(const fs::path& exe_dir) { return portable_root(exe_dir) / "native"; }

// The platform's own folder (rule 3); empty when the system gives none (no APPDATA / HOME)
inline fs::path platform_user_dir() {
#if defined(_WIN32)
    // fsrobust: the wide value (a user name such as José)
    if (const auto a = getenv("APPDATA")) return u8path(*a) / "b3port" / "native";
    return {};
#elif defined(__APPLE__)
    if (const auto h = getenv("HOME")) return fs::path(*h) / "Library" / "Application Support" / "b3port" / "native";
    return {};
#else
    if (const auto x = getenv("XDG_DATA_HOME")) return fs::path(*x) / "b3port" / "native";
    if (const auto h = getenv("HOME")) return fs::path(*h) / ".local" / "share" / "b3port" / "native";
    return {};
#endif
}

// This program's folder (Windows GetModuleFileNameW, macOS _NSGetExecutablePath, Linux /proc/self/exe); empty when the
// system does not say. In libb3render.so / b3render.dll it is the game's (the process's program).
inline fs::path exe_dir() {
    std::error_code ec;
#if defined(_WIN32)
    std::wstring p(32768, L'\0');
    const unsigned long n = ::GetModuleFileNameW(nullptr, p.data(), static_cast<unsigned long>(p.size()));
    if (n == 0 || n >= p.size()) return {};
    p.resize(n);
    return fs::path(p).parent_path();
#elif defined(__APPLE__)
    char self[4096];
    std::uint32_t n = sizeof self;
    if (_NSGetExecutablePath(self, &n) != 0) return {};
    const fs::path p = fs::weakly_canonical(fs::path(self), ec);
    return ec ? fs::path() : p.parent_path();
#else
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : p.parent_path();
#endif
}

inline bool in_appimage() { return getenv("APPIMAGE").has_value(); }
inline bool in_flatpak() {
#if defined(_WIN32) || defined(__APPLE__)
    return false;
#else
    std::error_code ec;
    return getenv("FLATPAK_ID").has_value() || fs::exists("/.flatpak-info", ec);
#endif
}

// The rule over what is known (the tests' table; user_dir_choice() fills it from this process)
struct UserDirCase {
    std::optional<fs::path> override_dir;   // rule 1
    fs::path exe_dir;
    bool marker = false;                    // <exe_dir>/appdata is a folder
    bool apple = false, appimage = false, flatpak = false;
    fs::path platform;                      // rule 3's folder
};
enum class UserDirKind : std::uint8_t { kOverride, kPortable, kPlatform };
struct UserDirChoice {
    fs::path dir;
    UserDirKind kind = UserDirKind::kPlatform;
};
// Portable mode can be used here at all (not macOS, an AppImage or a Flatpak); the launcher hides its row otherwise
inline bool portable_supported(bool apple, bool appimage, bool flatpak) { return !apple && !appimage && !flatpak; }
inline UserDirChoice resolve_user_dir(const UserDirCase& c) {
    if (c.override_dir && !c.override_dir->empty()) return {*c.override_dir, UserDirKind::kOverride};
    if (c.marker && !c.exe_dir.empty() && portable_supported(c.apple, c.appimage, c.flatpak))
        return {portable_dir(c.exe_dir), UserDirKind::kPortable};
    return {c.platform, UserDirKind::kPlatform};
}

inline bool apple_build() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

inline UserDirCase this_user_dir_case() {
    UserDirCase c;
    if (const auto o = getenv(kUserDirEnv)) c.override_dir = u8path(*o);
    c.apple = apple_build();
    c.appimage = in_appimage();
    c.flatpak = in_flatpak();
    if (portable_supported(c.apple, c.appimage, c.flatpak)) {
        static const fs::path self = exe_dir();   // (the program does not move while it runs)
        c.exe_dir = self;
        std::error_code ec;
        c.marker = !c.exe_dir.empty() && fs::is_directory(portable_root(c.exe_dir), ec);
    }
    c.platform = platform_user_dir();
    return c;
}
inline UserDirChoice user_dir_choice() { return resolve_user_dir(this_user_dir_case()); }
// The user data folder in use (not created here); empty when there is none
inline fs::path user_dir() { return user_dir_choice().dir; }
// The same, made when missing (as SDL_GetPrefPath does), as UTF-8 text with a trailing separator; empty when it
// cannot be made
inline std::string user_dir_slash() {
    const fs::path d = user_dir();
    if (d.empty()) return {};
    std::error_code ec;
    fs::create_directories(d, ec);
    if (!fs::is_directory(d, ec)) return {};
    std::string s = u8string(d);
    if (s.back() != '/' && s.back() != '\\') s += static_cast<char>(fs::path::preferred_separator);
    return s;
}

// A folder can be written: a probe file made and removed
inline bool dir_writable(const fs::path& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return false;
    const fs::path probe = dir / ".b3port-write-test";
    bool ok = false;
    {
        std::ofstream f(probe, std::ios::binary | std::ios::trunc);
        ok = f && (f << "x") && (f.flush(), bool(f));
    }
    fs::remove(probe, ec);
    return ok;
}

// Folders kept in the ini files (launcher.ini game_dir / saves_dir / Steam pictures, pc_settings.ini hd_pack_dir /
// music_folder) that lie inside the install folder are stored relative to it, as "<install>/<path>" ('<' and '>' never
// appear in a Windows path, so the form cannot be a real folder), and resolved against the program's folder when read:
// a USB stick whose drive letter changes (E: here, F: there), or an install folder moved, still finds them. A folder
// outside stays absolute. Comparison: component by component, ASCII case folded on Windows (E:\Games and e:\games).
inline constexpr const char* kInstallToken = "<install>";
namespace detail {
inline bool same_component(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    const std::wstring x = a.native(), y = b.native();
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); ++i) {
        wchar_t c = x[i], d = y[i];
        if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
        if (d >= L'A' && d <= L'Z') d = wchar_t(d - L'A' + L'a');
        if ((c == L'/' || c == L'\\') && (d == L'/' || d == L'\\')) continue;
        if (c != d) return false;
    }
    return true;
#else
    return a == b;
#endif
}
}  // namespace detail
// `utf8` as it is stored: "<install>/<path>" when it lies inside `install` (or is it), else as it is
inline std::string install_relative(const std::string& utf8, const fs::path& install) {
    if (utf8.empty() || install.empty() || utf8.rfind(kInstallToken, 0) == 0) return utf8;
    const fs::path p = u8path(utf8);
    if (!p.is_absolute()) return utf8;
    const fs::path pn = p.lexically_normal(), in = install.lexically_normal();
    auto pi = pn.begin();
    for (auto ii = in.begin(); ii != in.end(); ++ii, ++pi) {
        if (ii->empty() && std::next(ii) == in.end()) break;   // (a trailing separator)
        if (pi == pn.end() || !detail::same_component(*pi, *ii)) return utf8;
    }
    std::string rest;
    for (; pi != pn.end(); ++pi) {
        if (pi->empty()) continue;
        rest += "/" + u8string(*pi);
    }
    return kInstallToken + rest;
}
// back: "<install>/<path>" -> install / path (UTF-8); anything else as it is
inline std::string install_resolved(const std::string& utf8, const fs::path& install) {
    const std::string tok = kInstallToken;
    if (utf8.rfind(tok, 0) != 0 || install.empty()) return utf8;
    std::string rest = utf8.substr(tok.size());
    while (!rest.empty() && (rest.front() == '/' || rest.front() == '\\')) rest.erase(rest.begin());
    return u8string(rest.empty() ? install : install / u8path(rest));
}

// Windows' classic path limit (MAX_PATH 260, with its terminator): a longer path fails in programs that are not
// long-path aware. A move names the cause and the path instead of a bare "cannot write" (portable.h plan)
inline constexpr std::size_t kWindowsPathLimit = 259;
inline std::size_t path_limit() {
#ifdef _WIN32
    return kWindowsPathLimit;
#else
    return 0;   // none
#endif
}

// The game's PORTABLE MODE row asks the launcher for a move at its next start (only the launcher moves the data):
// <data dir>/portable-request.txt holding "on" or "off"
inline constexpr const char* kPortableRequestFile = "portable-request.txt";
inline std::optional<bool> read_portable_request(const fs::path& data_dir) {
    if (data_dir.empty()) return std::nullopt;
    std::ifstream f(data_dir / kPortableRequestFile, std::ios::binary);
    std::string w;
    if (!(f >> w)) return std::nullopt;
    if (w == "on") return true;
    if (w == "off") return false;
    return std::nullopt;
}
inline bool write_portable_request(const fs::path& data_dir, bool on, std::string* why = nullptr) {
    if (data_dir.empty()) {
        if (why) *why = "no user data folder";
        return false;
    }
    std::error_code ec;
    fs::create_directories(data_dir, ec);
    return replace_file(data_dir / kPortableRequestFile, on ? "on\n" : "off\n", why);
}
inline void clear_portable_request(const fs::path& data_dir) {
    std::error_code ec;
    if (!data_dir.empty()) fs::remove(data_dir / kPortableRequestFile, ec);
}

}  // namespace b3io

#endif
