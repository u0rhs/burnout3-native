// Language packs (1.0.1): the game's text from the player's own European disc.
//
// The US disc holds its text in DATA/GLOBALUS.BIN and DATA/HEADUS.BIN. A European disc holds one such pair per
// language, DATA/GLOBAL<code>.BIN and DATA/HEAD<code>.BIN, in the same string-table format with the same ids (0..4005
// on the US disc; the European tables add id 4006 at the end, which the US game never asks for). So a language is
// two files taken from the player's own disc and given to the game in place of the US pair: no game data ships with
// the port, and nothing is copied or written.
//
// Codes seen on the discs: FR GE IT (Europe, SLES-52585), EN SP DU SW (Europe and Australia, SLES-52584), JA (Japan,
// and Korea's table). JA is listed but not offered: the US game's fonts have no CJK glyphs.
//
// Header-only on b3asset/discfs.h, so every program that reads the disc (native_play, the renderer, the launcher)
// gets it without another object.
#ifndef B3ASSET_LANGUAGE_H
#define B3ASSET_LANGUAGE_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "b3asset/discfs.h"

namespace b3asset::language {

struct Language {
    std::string code;     // "IT", as the disc's file names have it
    std::string name;     // "Italiano (Italian)"
    bool supported = true;
};

// The name the settings show for a code; the code itself when it is not one of the known ones.
inline std::string name_of(const std::string& code) {
    static const struct { const char* code; const char* name; } kNames[] = {
        {"US", "English (US)"}, {"EN", "English (UK)"}, {"FR", "Fran\xC3\xA7" "ais (French)"}, {"GE", "Deutsch (German)"},
        {"IT", "Italiano (Italian)"}, {"SP", "Espa\xC3\xB1" "ol (Spanish)"}, {"DU", "Nederlands (Dutch)"}, {"SW", "Svenska (Swedish)"},
        {"JA", "Japanese"},
    };
    for (const auto& n : kNames)
        if (code == n.code) return n.name;
    return code;
}

// Whether the US game can show a language's text (its fonts cover the European tables' letters, not CJK).
inline bool supported(const std::string& code) { return code != "JA"; }

// A two-letter code, upper-cased; empty when `s` is not one.
inline std::string normalise(const std::string& s) {
    if (s.size() != 2 || !std::isalpha(static_cast<unsigned char>(s[0])) || !std::isalpha(static_cast<unsigned char>(s[1]))) return {};
    std::string c = s;
    for (char& ch : c) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return c;
}

// The disc's file of a language: DATA/GLOBAL<code>.BIN (head = false) or DATA/HEAD<code>.BIN; empty when the disc has
// none. `disc` is a folder of the disc's files or a disc image (discfs.h).
inline std::filesystem::path file(const std::filesystem::path& disc, const std::string& code, bool head) {
    if (disc.empty() || normalise(code).empty()) return {};
    const std::filesystem::path p = disc::resolve_ci(disc, std::string("DATA/") + (head ? "HEAD" : "GLOBAL") + normalise(code) + ".BIN");
    return !p.empty() && disc::is_file(p) ? p : std::filesystem::path{};
}

// The languages on a disc: every GLOBAL<code>.BIN in its DATA folder that has its HEAD<code>.BIN, by code. The US
// pair is left out (it is the game's own text, English).
inline std::vector<Language> on_disc(const std::filesystem::path& disc) {
    std::vector<Language> out;
    if (disc.empty() || !disc::is_dir(disc)) return out;
    const std::filesystem::path data = disc::resolve_ci(disc, "DATA");
    if (data.empty() || !disc::is_dir(data)) return out;
    for (const disc::Entry& e : disc::list_dir(data)) {
        std::string n = e.name;
        for (char& ch : n) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (const std::size_t semi = n.find(';'); semi != std::string::npos) n.resize(semi);
        if (n.size() != 12 || n.compare(0, 6, "GLOBAL") != 0 || n.compare(8, 4, ".BIN") != 0) continue;
        const std::string code = normalise(n.substr(6, 2));
        if (code.empty() || code == "US" || file(disc, code, true).empty()) continue;
        out.push_back({code, name_of(code), supported(code)});
    }
    std::sort(out.begin(), out.end(), [](const Language& a, const Language& b) { return a.code < b.code; });
    return out;
}

// The folder imported languages live in: <preferences>/languages, holding DATA/GLOBAL<code>.BIN and HEAD<code>.BIN as
// the disc does, so file() and on_disc() read it as they read a disc.
inline std::filesystem::path imported_root(const std::filesystem::path& pref) { return pref / "languages"; }

// One-time import (the launcher's IMPORT LANGUAGES): every supported language on `disc` copied into
// imported_root(pref) (two small files each; the disc is not needed afterwards). Returns the languages copied; `why`
// says what failed when none was.
inline std::vector<Language> import_from_disc(const std::filesystem::path& disc, const std::filesystem::path& pref, std::string* why = nullptr) {
    std::vector<Language> done;
    const std::vector<Language> found = on_disc(disc);
    if (found.empty()) {
        if (why) *why = "no language files (DATA/GLOBAL??.BIN with HEAD??.BIN) on that disc";
        return done;
    }
    const std::filesystem::path dest = imported_root(pref) / "DATA";
    std::error_code ec;
    std::filesystem::create_directories(dest, ec);
    if (ec) {
        if (why) *why = "could not create " + dest.string();
        return done;
    }
    for (const Language& l : found) {
        if (!l.supported) continue;
        bool ok = true;
        for (const bool head : {false, true}) {
            std::vector<std::uint8_t> bytes;
            const std::filesystem::path src = file(disc, l.code, head);
            ok = ok && !src.empty() && disc::read_file(src, bytes) && !bytes.empty();
            if (!ok) break;
            const std::filesystem::path to = dest / ((head ? "HEAD" : "GLOBAL") + l.code + ".BIN");
            const std::filesystem::path tmp = to.string() + ".part";
            {
                std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
                ok = f && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
                if (f) ok = std::fclose(f) == 0 && ok;
            }
            if (ok) std::filesystem::rename(tmp, to, ec), ok = !ec;
            if (!ok) std::filesystem::remove(tmp, ec);
            if (!ok) break;
        }
        if (ok) done.push_back(l);
    }
    if (done.empty() && why) *why = "the language files could not be copied to " + dest.string();
    return done;
}

}  // namespace b3asset::language

#endif
