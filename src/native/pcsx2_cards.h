// pcsx2_cards.h -- where PCSX2 keeps its memory cards, for the save imports (the launcher's Saves page and folder
// list, PC SETTINGS > SAVES > IMPORT SAVE): their dialogs start in the first of these folders that exists. Header
// only, standard library (the launcher and the renderer library include it; tests/test_save_formats.cpp checks it).
//
// PCSX2's usual memcards folders, most likely first:
//   Windows: <Documents>\PCSX2\memcards (1.6, and 1.7 / 2.x unless portable), <Program Files>\PCSX2\memcards and
//            <Program Files (x86)>\PCSX2\memcards (a portable copy there)
//   Linux:   ${XDG_CONFIG_HOME:-~/.config}/PCSX2/memcards, the Flatpak's ~/.var/app/net.pcsx2.PCSX2/config/PCSX2/memcards
//   macOS:   ~/Library/Application Support/PCSX2/memcards
// The card in PCSX2's first slot is Mcd001.ps2 unless renamed (the second Mcd002.ps2).
#ifndef B3NATIVE_PCSX2_CARDS_H
#define B3NATIVE_PCSX2_CARDS_H

#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>
#include <vector>

namespace b3native::save {

inline constexpr const char* kPcsx2FirstCard = "Mcd001.ps2";

enum class Pcsx2Os { kWindows, kMac, kLinux };
#if defined(_WIN32)
inline constexpr Pcsx2Os kPcsx2ThisOs = Pcsx2Os::kWindows;
#elif defined(__APPLE__)
inline constexpr Pcsx2Os kPcsx2ThisOs = Pcsx2Os::kMac;
#else
inline constexpr Pcsx2Os kPcsx2ThisOs = Pcsx2Os::kLinux;
#endif

struct Pcsx2CardFolder {
    std::string label;             // "PCSX2 memory cards", "PCSX2 memory cards (Flatpak)"
    std::filesystem::path path;    // its memcards folder
};

// The user's folders the places come from (SDL_GetUserFolder, the environment); an empty one is skipped.
struct Pcsx2Homes {
    std::filesystem::path home;            // the user's home folder
    std::filesystem::path documents;       // Windows: the Documents folder (it may be moved, to OneDrive for one)
    std::filesystem::path xdg_config;      // Linux: $XDG_CONFIG_HOME (empty: <home>/.config)
    std::vector<std::filesystem::path> program_files;   // Windows: %ProgramFiles%, %ProgramFiles(x86)%
};

// PCSX2's memcards folders for `os` (`existing`: only those that are folders on this computer), most likely first,
// each once.
inline std::vector<Pcsx2CardFolder> pcsx2_card_folders(const Pcsx2Homes& h, bool existing = true, Pcsx2Os os = kPcsx2ThisOs) {
    namespace fs = std::filesystem;
    std::vector<Pcsx2CardFolder> all;
    const std::string kLabel = "PCSX2 memory cards";
    auto add = [&](const std::string& label, const fs::path& base, std::initializer_list<const char*> parts) {
        if (base.empty()) return;
        fs::path p = base;
        for (const char* s : parts) p /= s;
        all.push_back({label, p});
    };
    switch (os) {
    case Pcsx2Os::kWindows:
        add(kLabel, h.documents, {"PCSX2", "memcards"});
        if (h.documents.empty()) add(kLabel, h.home, {"Documents", "PCSX2", "memcards"});
        for (const fs::path& p : h.program_files) add(kLabel + " (installed)", p, {"PCSX2", "memcards"});
        break;
    case Pcsx2Os::kMac:
        add(kLabel, h.home, {"Library", "Application Support", "PCSX2", "memcards"});
        break;
    case Pcsx2Os::kLinux:
        if (!h.xdg_config.empty()) add(kLabel, h.xdg_config, {"PCSX2", "memcards"});
        add(kLabel, h.home, {".config", "PCSX2", "memcards"});
        add(kLabel + " (Flatpak)", h.home, {".var", "app", "net.pcsx2.PCSX2", "config", "PCSX2", "memcards"});
        break;
    }
    std::vector<Pcsx2CardFolder> out;
    for (Pcsx2CardFolder& f : all) {
        bool seen = false;
        for (const Pcsx2CardFolder& o : out) seen = seen || o.path == f.path;
        std::error_code ec;
        if (seen || (existing && !fs::is_directory(f.path, ec))) continue;
        out.push_back(std::move(f));
    }
    return out;
}

// The card in PCSX2's first slot in `folder` (Mcd001.ps2), when it is there; else empty.
inline std::filesystem::path pcsx2_first_card(const std::filesystem::path& folder) {
    std::error_code ec;
    const std::filesystem::path p = folder / kPcsx2FirstCard;
    return std::filesystem::exists(p, ec) ? p : std::filesystem::path();
}

}  // namespace b3native::save

#endif
