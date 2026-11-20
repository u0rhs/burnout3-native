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
#include "b3port/fileio.h"   // fsrobust: UTF-8 paths
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
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
            std::filesystem::path tmp = to;
            tmp += ".part";
            // fsrobust: written wide (the pref path can hold any name), flushed, then put in place in one step
            ok = b3io::write_synced(tmp, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())) &&
                 b3io::commit_replace(tmp, to);
            if (!ok) break;
        }
        if (ok) done.push_back(l);
    }
    if (done.empty() && why) *why = "the language files could not be copied to " + dest.string();
    return done;
}


// ---- 1.3.0: the DJ's voice (Stryker, Crash FM) in the player's language ----
//
// The US disc holds the DJ's lines in RenderWare Audio stream banks: TRACKS/E_DJ<x>.RWS (12 general banks: DJAS, DJEU,
// DJUS, DJGEN, DJWWW, DJM**) and TRACKS/<region>/<track>_V1/E_DJRACE.RWS (one per track, 18). A European disc holds the
// same 30 banks per language, the language's letter for E_: F_ (French), G_ (German), I_ (Italian) on SLES-52585 and
// S_ (Spanish) on SLES-52584, in the same container (chunk 0x80D / 0x80E / 0x80F, the same description size, segment
// count, layer, block sizes, codec UUID and bank name); only the lines' lengths differ. UK English, Dutch and Swedish
// play the English DJ on their discs too (SLES-52584's E_ banks are the US ones).
//
// The US program names the bank from the language word 0x004E2918, which stays 1 (English: it also sets number
// formats and the profile's language byte). So the port redirects the English names at the disc layer instead
// (src/native/platform_disc.cpp resolve()): with LANGUAGE French / German / Italian / Spanish and that language's DJ
// imported, TRACKS/.../E_DJ*.RWS opens TRACKS/.../<L>_DJ*.RWS from dj_root(); English (the default) is unchanged.
//
// The import copies the 30 banks of a language (286-353 MB) from the player's own disc into dj_root(pref, code), in a
// temporary folder first (dj_root + ".part") that is renamed into place once every file is written and checked.

// The DJ's letter of a language code: 'F' 'G' 'I' 'S'; 0 when the language has no DJ of its own (the English DJ).
inline char dj_letter(const std::string& code) {
    const std::string c = normalise(code);
    if (c == "FR") return 'F';
    if (c == "GE") return 'G';
    if (c == "IT") return 'I';
    if (c == "SP") return 'S';
    return 0;
}
// The languages with a DJ of their own, in the order the settings list them.
inline const std::vector<std::string>& dj_codes() {
    static const std::vector<std::string> k{"FR", "GE", "IT", "SP"};
    return k;
}

// A disc path as the overlay keys it: '/' separators, ASCII upper case, no leading separator ("TRACKS/EU/C1_V1/E_DJRACE.RWS").
inline std::string disc_key(std::string p) {
    for (char& ch : p) {
        if (ch == '\\') ch = '/';
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    std::size_t start = 0;
    while (start < p.size() && p[start] == '/') ++start;
    return p.substr(start);
}

// An English DJ bank's disc key -> the same bank of `letter`'s language ("TRACKS/EU/C1_V1/E_DJRACE.RWS", 'G' ->
// "TRACKS/EU/C1_V1/G_DJRACE.RWS"); empty when `key` is not an English DJ bank below TRACKS (or the letter is none).
inline std::string dj_counterpart(const std::string& key, char letter) {
    if (!letter || key.size() < 7 + 8 || key.compare(0, 7, "TRACKS/") != 0) return {};
    const std::size_t slash = key.rfind('/');
    const std::string name = key.substr(slash + 1);
    if (name.size() < 8 || name.compare(0, 4, "E_DJ") != 0 || name.compare(name.size() - 4, 4, ".RWS") != 0) return {};
    if (key.find("/../") != std::string::npos || key.find("/./") != std::string::npos || key.find("//") != std::string::npos) return {};
    std::string out = key;
    out[slash + 1] = letter;
    return out;
}

// One RWS stream's header (the 0x80D chunk's 0x80E description; docs/modding/file-formats/audio.md).
struct RwsHeader {
    std::uint32_t version = 0;          // the chunks' version word
    std::uint32_t header_size = 0;      // the 0x80E chunk's size
    std::uint32_t description = 0;      // the description's first word (its size as the stream source allocates it)
    std::uint32_t segments = 0, layers = 0;
    std::uint32_t blocks[3] = {};
    std::uint8_t uuid[16] = {};
    std::string bank;                   // the bank's name (up to the NUL; the rest of its 16 bytes is padding)
    std::uint64_t data_size = 0;        // the 0x80F chunk's size
    std::uint64_t largest_segment = 0;  // bytes
};
// `b` (the file's first `n` bytes, n >= the header chunk + 12) of a file of `file_size` bytes. False (why says what)
// unless it is one whole stream: 0x80D the file, 0x80E first, 0x80F after it to the file's end, every segment inside it.
inline bool rws_header(const std::uint8_t* b, std::size_t n, std::uint64_t file_size, RwsHeader& h, std::string* why = nullptr) {
    const auto fail = [&](const char* w) { if (why) *why = w; return false; };
    const auto u32 = [&](std::size_t at) {
        return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24;
    };
    h = RwsHeader{};
    if (n < 0x18 + 0x60 || u32(0) != 0x80Du) return fail("not an RWS stream (no chunk 0x80D)");
    if (std::uint64_t(u32(4)) + 12 != file_size) return fail("the 0x80D chunk is not the whole file");
    if (u32(12) != 0x80Eu) return fail("no stream header (chunk 0x80E)");
    h.version = u32(8);
    h.header_size = u32(16);
    if (u32(20) != h.version || h.header_size < 0x60 || h.header_size > 0x10000) return fail("a stream header of an unknown size");
    const std::size_t d = 0x18 + h.header_size;
    if (d + 12 > n) return fail("the stream header runs past what was read");
    if (u32(d) != 0x80Fu || u32(d + 8) != h.version) return fail("no stream data (chunk 0x80F) after the header");
    h.data_size = u32(d + 4);
    if (d + 12 + h.data_size != file_size) return fail("the stream data does not end at the file's end");
    h.description = u32(0x18);
    h.segments = u32(0x18 + 0x20);
    h.layers = u32(0x18 + 0x28);
    for (int k = 0; k < 3; ++k) h.blocks[k] = u32(0x18 + 0x30 + 4 * std::size_t(k));
    std::copy(b + 0x18 + 0x40, b + 0x18 + 0x50, h.uuid);
    for (std::size_t k = 0; k < 16 && b[0x18 + 0x50 + k]; ++k) h.bank += static_cast<char>(b[0x18 + 0x50 + k]);
    if (h.segments == 0 || h.segments > 1024 || 0x60 + 0x20ull * h.segments > h.header_size) return fail("a segment count the header cannot hold");
    for (std::uint32_t k = 0; k < h.segments; ++k) {
        const std::size_t e = 0x18 + 0x60 + 0x20 * std::size_t(k);
        const std::uint64_t size = u32(e + 0x18), at = u32(e + 0x1C);
        if (at + size > h.data_size) return fail("a segment runs past the stream data");
        h.largest_segment = std::max(h.largest_segment, size);
    }
    return true;
}
// The header of a file (a host file or one in a disc image).
inline bool rws_read_header(const std::filesystem::path& file, RwsHeader& h, std::string* why = nullptr) {
    disc::File f;
    if (!f.open(file)) {
        if (why) *why = "cannot be opened";
        return false;
    }
    std::vector<std::uint8_t> b(static_cast<std::size_t>(std::min<std::uint64_t>(f.size(), 0x10000 + 0x30)));
    const std::int64_t got = b.empty() ? 0 : f.read_at(0, b.data(), b.size());
    if (got < 0 || std::size_t(got) != b.size()) {
        if (why) *why = "cannot be read";
        return false;
    }
    return rws_header(b.data(), b.size(), f.size(), h, why);
}
// The DJ banks' format (every bank on the US and European discs): one layer, blocks 2048 / 8192 / 2048 or 4096, a
// bank name.
inline bool dj_format(const RwsHeader& h, std::string* why = nullptr) {
    const bool ok = h.layers == 1 && h.blocks[0] == 0x800 && h.blocks[1] == 0x2000 && (h.blocks[2] == 0x800 || h.blocks[2] == 0x1000) &&
                    !h.bank.empty();
    if (!ok && why) *why = "not a DJ bank (its layers, block sizes or name differ)";
    return ok;
}
// A language's bank against the English one it stands in for: the same description size, segment count, layers,
// blocks, codec UUID and bank name (the game picks a line by its segment index; the stream source finds the stream by
// its UUID and name and sizes its description from the first word).
inline bool dj_matches(const RwsHeader& lang, const RwsHeader& english, std::string* why = nullptr) {
    const char* w = nullptr;
    if (lang.segments != english.segments) w = "its line count differs from the English bank's";
    else if (lang.header_size != english.header_size || lang.description != english.description) w = "its header's size differs from the English bank's";
    else if (lang.layers != english.layers || !std::equal(lang.blocks, lang.blocks + 3, english.blocks)) w = "its layout differs from the English bank's";
    else if (!std::equal(lang.uuid, lang.uuid + 16, english.uuid)) w = "its codec differs from the English bank's";
    else if (lang.bank != english.bank) w = "its name differs from the English bank's";
    if (w && why) *why = w;
    return !w;
}

struct DjFile {
    std::string key;                 // the language's file as the disc keys it ("TRACKS/EU/C1_V1/G_DJRACE.RWS")
    std::filesystem::path path;      // where it is (on the disc, or in dj_root)
    std::uint64_t size = 0;
};
struct DjVoice {
    std::string code, name;          // "GE", "Deutsch (German)"
    char letter = 0;
    std::vector<DjFile> files;
    std::uint64_t bytes = 0;
};

// The DJ voices on `disc` (a folder or an image; or dj_root(), which has the disc's layout): for each of dj_codes(),
// its TRACKS/<L>_DJ*.RWS and TRACKS/*/*/<L>_DJRACE.RWS. Languages without any are left out.
inline std::vector<DjVoice> dj_on_disc(const std::filesystem::path& disc) {
    std::vector<DjVoice> out;
    if (disc.empty() || !disc::is_dir(disc)) return out;
    const std::filesystem::path tracks = disc::resolve_ci(disc, "TRACKS");
    if (tracks.empty() || !disc::is_dir(tracks)) return out;
    // every file name below TRACKS that is some language's DJ bank: (key, path, size)
    std::vector<DjFile> all;
    const auto bare = [](std::string n) {
        for (char& ch : n) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (const std::size_t semi = n.find(';'); semi != std::string::npos) n.resize(semi);
        return n;
    };
    const auto take = [&](const disc::Entry& e, const std::string& dir, bool race) {
        const std::string n = bare(e.name);
        if (e.dir || n.size() < 9 || n[1] != '_' || n.compare(2, 2, "DJ") != 0 || n.compare(n.size() - 4, 4, ".RWS") != 0) return;
        if (race != (n.compare(2, n.size() - 2, "DJRACE.RWS") == 0)) return;
        const std::int64_t size = disc::file_size(e.path);   // (an image's listing may leave Entry::size 0)
        all.push_back({dir + n, e.path, size > 0 ? std::uint64_t(size) : e.size});
    };
    try {
        for (const disc::Entry& e : disc::list_dir(tracks)) {
            if (!e.dir) {
                take(e, "TRACKS/", false);
                continue;
            }
            for (const disc::Entry& t : disc::list_dir(e.path))
                if (t.dir)
                    for (const disc::Entry& f : disc::list_dir(t.path)) take(f, "TRACKS/" + bare(e.name) + "/" + bare(t.name) + "/", true);
        }
    } catch (const std::exception&) {
        return out;
    }
    for (const std::string& code : dj_codes()) {
        DjVoice v;
        v.code = code;
        v.name = name_of(code);
        v.letter = dj_letter(code);
        for (const DjFile& f : all)
            if (f.key[f.key.rfind('/') + 1] == v.letter) {
                v.files.push_back(f);
                v.bytes += f.size;
            }
        std::sort(v.files.begin(), v.files.end(), [](const DjFile& a, const DjFile& b) { return a.key < b.key; });
        if (!v.files.empty()) out.push_back(std::move(v));
    }
    return out;
}

// Where a language's imported DJ lives: <preferences>/languages/DJ/<code>, in the disc's layout (TRACKS/...).
inline std::filesystem::path dj_root(const std::filesystem::path& pref, const std::string& code) {
    return imported_root(pref) / "DJ" / normalise(code);
}
// The bytes of a language's imported DJ banks; 0: none imported.
inline std::uint64_t dj_imported_bytes(const std::filesystem::path& pref, const std::string& code) {
    if (pref.empty() || normalise(code).empty()) return 0;
    std::error_code ec;
    if (!std::filesystem::is_directory(dj_root(pref, code), ec)) return 0;
    for (const DjVoice& v : dj_on_disc(dj_root(pref, code)))
        if (v.code == normalise(code)) return v.bytes;
    return 0;
}

// ---- 1.3.0: the race training videos' pictures ----
// The race training's narration is on every disc (OVID/MOVIE.RWS holds e_ f_ g_ i_ j_ s_train segments; the game picks
// the movie by language: tools/native/training_voice.h). Its pictures with their on-screen text are per language
// and only on the European discs: OVID/TRC_<tag>.M2V and TRD_<tag>.M2V (FRA, GER, ITA, SPA; about 129 MB a language).
// Imported into imported_root(pref)/VIDEO/<code>/OVID; without them the English picture plays (the same length).
inline std::string training_tag(const std::string& code) {
    const std::string c = normalise(code);
    if (c == "FR") return "FRA";
    if (c == "GE") return "GER";
    if (c == "IT") return "ITA";
    if (c == "SP") return "SPA";
    return {};
}
// A localized training video's disc key ("OVID/TRC_GER.M2V") -> its language code ("GE") and the English video's key
// ("OVID/TRC_ENG.M2V"); false for any other path.
inline bool training_video(const std::string& key, std::string* code = nullptr, std::string* english = nullptr) {
    if (key.size() != 16 || key.compare(0, 5, "OVID/") != 0 || key[5] != 'T' || key[6] != 'R' || (key[7] != 'C' && key[7] != 'D') ||
        key[8] != '_' || key.compare(12, 4, ".M2V") != 0)
        return false;
    for (const std::string& c : dj_codes())
        if (key.compare(9, 3, training_tag(c)) == 0) {
            if (code) *code = c;
            if (english) *english = key.substr(0, 9) + "ENG.M2V";
            return true;
        }
    return false;
}
// What an import copies: the DJ's banks, or the training videos' pictures.
enum class Pack { kDj, kTraining };
// Where a language's imported pack lives: <preferences>/languages/DJ/<code> or VIDEO/<code>, in the disc's layout.
inline std::filesystem::path pack_root(const std::filesystem::path& pref, const std::string& code, Pack pack) {
    return pack == Pack::kDj ? dj_root(pref, code) : imported_root(pref) / "VIDEO" / normalise(code);
}
// The training videos on `disc` (or a pack_root): each language with both its TRC_ and TRD_ pictures.
inline std::vector<DjVoice> training_on_disc(const std::filesystem::path& disc) {
    std::vector<DjVoice> out;
    if (disc.empty() || !disc::is_dir(disc)) return out;
    for (const std::string& code : dj_codes()) {
        DjVoice v;
        v.code = code;
        v.name = name_of(code);
        v.letter = dj_letter(code);
        for (const char* kind : {"TRC_", "TRD_"}) {
            const std::string key = std::string("OVID/") + kind + training_tag(code) + ".M2V";
            const std::filesystem::path p = disc::resolve_ci(disc, key);
            const std::int64_t size = p.empty() ? -1 : disc::file_size(p);
            if (size > 0) {
                v.files.push_back({key, p, std::uint64_t(size)});
                v.bytes += std::uint64_t(size);
            }
        }
        if (v.files.size() == 2) out.push_back(std::move(v));
    }
    return out;
}
inline std::vector<DjVoice> pack_on_disc(const std::filesystem::path& disc, Pack pack) {
    return pack == Pack::kDj ? dj_on_disc(disc) : training_on_disc(disc);
}
inline std::uint64_t pack_imported_bytes(const std::filesystem::path& pref, const std::string& code, Pack pack) {
    if (pref.empty() || normalise(code).empty()) return 0;
    std::error_code ec;
    if (!std::filesystem::is_directory(pack_root(pref, code, pack), ec)) return 0;
    for (const DjVoice& v : pack_on_disc(pack_root(pref, code, pack), pack))
        if (v.code == normalise(code)) return v.bytes;
    return 0;
}
// An MPEG-2 video elementary stream (the M2V pictures): a sequence header first.
inline bool m2v_ok(const std::filesystem::path& file, std::string* why = nullptr) {
    disc::File f;
    std::uint8_t b[4] = {};
    const bool ok = f.open(file) && f.size() > 4096 && f.read_at(0, b, 4) == 4 && b[0] == 0 && b[1] == 0 && b[2] == 1 && b[3] == 0xB3;
    if (!ok && why) *why = "not an MPEG-2 video (no sequence header)";
    return ok;
}

// The import's progress (on the worker's thread): bytes copied of all; a set `cancel` stops it at the next 1 MB.
struct DjProgress {
    std::function<void(std::uint64_t done, std::uint64_t total)> step;
    const std::atomic<bool>* cancel = nullptr;
};
// Removes a folder and what is in it (true: nothing left). The launcher gives b3mod::remove_tree (no link or junction
// followed; b3asset does not link b3mod); by default std::filesystem::remove_all.
using RemoveTree = std::function<bool(const std::filesystem::path&)>;
inline bool remove_all_tree(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    return !ec;
}
// The free space an import needs beyond the files: the folder's own bookkeeping and the rename's margin.
constexpr std::uint64_t kDjSpareBytes = 64ull << 20;

// Every bank of `v` checked (the format, and against the English bank on `english_disc` when that disc has it; an empty
// path: the format only). False (why names the first bad file) when one is not a DJ bank that can stand in.
inline bool pack_check(const DjVoice& v, const std::filesystem::path& english_disc, Pack pack, std::string* why = nullptr) {
    if (v.files.empty()) {
        if (why) *why = pack == Pack::kDj ? "no DJ banks" : "no training videos";
        return false;
    }
    if (pack == Pack::kTraining) {
        for (const DjFile& f : v.files) {
            std::string w;
            if (!m2v_ok(f.path, &w)) {
                if (why) *why = f.key + ": " + w;
                return false;
            }
        }
        return true;
    }
    for (const DjFile& f : v.files) {
        RwsHeader h, e;
        std::string w;
        bool ok = rws_read_header(f.path, h, &w) && h.data_size + 12 + 0x18 + h.header_size == f.size && dj_format(h, &w);
        if (ok && !english_disc.empty()) {
            std::string ek = f.key;
            ek[ek.rfind('/') + 1] = 'E';
            const std::filesystem::path ep = disc::resolve_ci(english_disc, ek);
            if (!ep.empty() && disc::is_file(ep)) ok = rws_read_header(ep, e, &w) && dj_matches(h, e, &w);
        }
        if (!ok) {
            if (why) *why = f.key + ": " + (w.empty() ? std::string("not a DJ bank") : w);
            return false;
        }
    }
    return true;
}
inline bool dj_check(const DjVoice& v, const std::filesystem::path& english_disc, std::string* why = nullptr) {
    return pack_check(v, english_disc, Pack::kDj, why);
}

// The one-time import of a language's DJ (the launcher's DJ VOICE rows; on a worker thread): every bank of `v` checked
// (dj_check), the free space checked, then copied into dj_root(pref, v.code) + ".part" and that folder renamed to
// dj_root (an older import replaced only then). Cancelled or failed: nothing is kept (the older import stays as it
// was). Never throws: false with `why` ("cancelled" when stopped).
// The training videos (Pack::kTraining) the same way into pack_root(pref, code, kTraining).
inline bool pack_import(const DjVoice& v, const std::filesystem::path& pref, const std::filesystem::path& english_disc, const DjProgress& progress,
                        Pack pack, std::string* why = nullptr, const RemoveTree& remove = remove_all_tree) {
    namespace fs = std::filesystem;
    const auto fail = [&](const std::string& w) { if (why) *why = w; return false; };
    if (pref.empty() || normalise(v.code).empty() || !dj_letter(v.code)) return fail("not a language with a voice of its own");
    const fs::path final_dir = pack_root(pref, v.code, pack);
    fs::path part = final_dir;
    part += ".part";
    std::error_code ec;
    const auto drop_part = [&]() { remove(part); };
    try {
        std::string w;
        if (!pack_check(v, english_disc, pack, &w)) return fail(w);
        fs::create_directories(final_dir.parent_path(), ec);
        if (ec) return fail("cannot create " + b3io::u8string(final_dir.parent_path()));
        drop_part();   // a crashed import's leftovers
        const fs::space_info sp = fs::space(final_dir.parent_path(), ec);
        if (!ec && sp.available < v.bytes + kDjSpareBytes)
            return fail("not enough free space: " + std::to_string((v.bytes + kDjSpareBytes + (1u << 20) - 1) >> 20) + " MB needed, " +
                        std::to_string(sp.available >> 20) + " MB free");
        fs::create_directories(part, ec);
        if (ec) return fail("cannot create " + b3io::u8string(part));
        std::vector<char> buf(1u << 20);
        std::uint64_t done = 0;
        for (const DjFile& f : v.files) {
            disc::File in;
            if (!in.open(f.path) || in.size() != f.size) { drop_part(); return fail(f.key + ": cannot be read from the disc"); }
            const fs::path to = part / fs::path(f.key);
            fs::create_directories(to.parent_path(), ec);
            std::FILE* out = b3io::fopen(to, "wb");
            if (!out) { drop_part(); return fail("cannot create " + b3io::u8string(to)); }
            bool ok = true;
            for (std::uint64_t at = 0; ok && at < f.size;) {
                if (progress.cancel && progress.cancel->load()) {
                    std::fclose(out);
                    drop_part();
                    return fail("cancelled");
                }
                const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(buf.size(), f.size - at));
                const std::int64_t got = in.read_at(at, buf.data(), n);
                ok = got == std::int64_t(n) && std::fwrite(buf.data(), 1, n, out) == n;
                at += n;
                done += n;
                if (progress.step) progress.step(done, v.bytes);
            }
            ok = std::fclose(out) == 0 && ok;
            if (!ok) { drop_part(); return fail(f.key + ": the copy failed (the disc, or a full drive)"); }
        }
        // the copies checked as the game will check them, then the folder put in place
        const std::vector<DjVoice> got = pack_on_disc(part, pack);
        if (got.size() != 1 || got[0].files.size() != v.files.size() || got[0].bytes != v.bytes || !pack_check(got[0], {}, pack, &w)) {
            drop_part();
            return fail("the copied files do not read back" + (w.empty() ? std::string() : ": " + w));
        }
        if (progress.cancel && progress.cancel->load()) { drop_part(); return fail("cancelled"); }
        fs::path old = final_dir;
        old += ".old";
        remove(old);
        const bool had = fs::exists(final_dir, ec);
        if (had) {
            fs::rename(final_dir, old, ec);
            if (ec) { drop_part(); return fail("cannot replace " + b3io::u8string(final_dir)); }
        }
        fs::rename(part, final_dir, ec);
        if (ec) {
            if (had) { std::error_code e2; fs::rename(old, final_dir, e2); }
            drop_part();
            return fail("cannot rename " + b3io::u8string(part));
        }
        remove(old);
        return true;
    } catch (const std::exception& e) {
        drop_part();
        return fail(std::string("the import stopped: ") + e.what());
    } catch (...) {
        drop_part();
        return fail("the import stopped");
    }
}

inline bool dj_import(const DjVoice& v, const std::filesystem::path& pref, const std::filesystem::path& english_disc, const DjProgress& progress,
                      std::string* why = nullptr) {
    return pack_import(v, pref, english_disc, progress, Pack::kDj, why);
}

// A language's imported pack removed (and any unfinished import). False with `why` when its folder stays.
inline bool pack_remove(const std::filesystem::path& pref, const std::string& code, Pack pack, std::string* why = nullptr,
                        const RemoveTree& remove = remove_all_tree) {
    namespace fs = std::filesystem;
    if (pref.empty() || normalise(code).empty()) return false;
    for (const char* tail : {"", ".part", ".old"}) {
        fs::path p = pack_root(pref, code, pack);
        p += tail;
        if (!remove(p)) {
            if (why) *why = "cannot remove " + b3io::u8string(p);
            return false;
        }
    }
    return true;
}
inline bool dj_remove(const std::filesystem::path& pref, const std::string& code, std::string* why = nullptr) {
    return pack_remove(pref, code, Pack::kDj, why);
}

// The game's redirect (src/native/platform_disc.cpp resolve()): the file in `dj_root` (an imported language's banks)
// that answers for the English bank `key` ("TRACKS/EU/C1_V1/E_DJRACE.RWS"; `english` its file on the disc), or an
// empty path (the English bank) with `why` set: `key` is no English DJ bank (why stays empty), the language's file is
// not there ("not imported"), or it is not a DJ bank that can stand in for the English one. `info` (optional) gets the
// language's header.
inline std::filesystem::path dj_redirect(const std::filesystem::path& dj_root, char letter, const std::string& key,
                                         const std::filesystem::path& english, std::string* why = nullptr, RwsHeader* info = nullptr) {
    std::string w;
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return std::filesystem::path{};
    };
    const std::string target = dj_counterpart(key, letter);
    if (target.empty() || dj_root.empty()) return fail({});
    const std::filesystem::path f = disc::resolve_ci(dj_root, target);
    if (f.empty() || !disc::is_file(f)) return fail("not imported");
    RwsHeader lh, eh;
    if (!rws_read_header(f, lh, &w) || !dj_format(lh, &w)) return fail(w);
    if (!rws_read_header(english, eh, &w)) return fail("the English bank " + w);
    if (!dj_matches(lh, eh, &w)) return fail(w);
    if (info) *info = lh;
    return f;
}

// A stand-in video for the checks: a sequence header, then `bytes` of zeros.
inline std::vector<std::uint8_t> training_test_video(std::size_t bytes) {
    std::vector<std::uint8_t> b(bytes + 4, 0);
    b[2] = 1;
    b[3] = 0xB3;
    return b;
}

// A stand-in bank for the checks (no game data): a valid stream of `segments` segments of `segment_bytes` each, the
// DJ layout, `bank` as its name and `tag` in its UUID (two tags: two codecs).
inline std::vector<std::uint8_t> dj_test_bank(std::uint32_t segments, std::uint32_t segment_bytes, const std::string& bank, std::uint8_t tag = 1) {
    const std::uint32_t hsz = 0x60 + 0x20 * segments + 0x40;
    const std::uint64_t data = std::uint64_t(segments) * segment_bytes;
    std::vector<std::uint8_t> b(0x18 + hsz + 12 + data, 0);
    const auto put = [&](std::size_t at, std::uint32_t v) {
        for (int k = 0; k < 4; ++k) b[at + std::size_t(k)] = std::uint8_t(v >> (8 * k));
    };
    const std::uint32_t ver = 0x1C020009u;
    put(0, 0x80D);
    put(4, std::uint32_t(b.size() - 12));
    put(8, ver);
    put(12, 0x80E);
    put(16, hsz);
    put(20, ver);
    put(0x18, 0x400 + 0x20 * segments);
    put(0x18 + 0x20, segments);
    put(0x18 + 0x28, 1);
    put(0x18 + 0x30, 0x800);
    put(0x18 + 0x34, 0x2000);
    put(0x18 + 0x38, 0x800);
    for (std::size_t k = 0; k < 16; ++k) b[0x18 + 0x40 + k] = std::uint8_t(tag + k);
    for (std::size_t k = 0; k < bank.size() && k < 15; ++k) b[0x18 + 0x50 + k] = std::uint8_t(bank[k]);
    for (std::uint32_t k = 0; k < segments; ++k) {
        put(0x18 + 0x60 + 0x20 * std::size_t(k) + 0x18, segment_bytes);
        put(0x18 + 0x60 + 0x20 * std::size_t(k) + 0x1C, k * segment_bytes);
    }
    const std::size_t d = 0x18 + hsz;
    put(d, 0x80F);
    put(d + 4, std::uint32_t(data));
    put(d + 8, ver);
    return b;
}

}  // namespace b3asset::language

#endif
