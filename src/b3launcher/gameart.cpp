// gameart.cpp -- see gameart.h
#include "gameart.h"

#include "b3asset/discfs.h"
#include "b3asset/gs_memory.h"
#include "b3asset/raster.h"
#include "b3mod/mods.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

namespace b3launcher {

namespace fs = std::filesystem;

namespace {

bool fail(std::string* why, std::string w) {
    if (why) *why = std::move(w);
    return false;
}

std::uint32_t rd32(const std::uint8_t* p) { return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }
float rdf(const std::uint8_t* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

bool same_ci(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (std::tolower(static_cast<unsigned char>(a[k])) != std::tolower(static_cast<unsigned char>(b[k]))) return false;
    return true;
}

bool read_all(const fs::path& p, std::vector<std::uint8_t>& out) { return b3asset::disc::read_file(p, out); }   // iso1: folder or image

constexpr std::uint32_t kFontTag = 0x76657233u;   // "3rev"
constexpr std::uint32_t kTxdMagic = 0x543C0000u;

// The retail program's three font slots [C decomp sdata 0x4E1584..0x4E1598, CB3GraphicsManagerBase::Construct]
struct FontSlot { std::uint32_t def, dict; const char* tex; };
constexpr FontSlot kSlots[3] = {{0x004E1584u, 0x004E1588u, "GlobalFont"}, {0x004E1594u, 0x004E1598u, "HeadFont"},
                                {0x004E158Cu, 0x004E1590u, "SmallFont"}};

}  // namespace

// ---- fonts ----

bool GameFont::has(std::uint16_t c) const {
    return std::any_of(glyphs.begin(), glyphs.end(), [c](const FontGlyph& g) { return g.code == c; });
}

const FontGlyph* GameFont::find(std::uint16_t c) const {
    if (glyphs.empty()) return nullptr;
    for (const FontGlyph& g : glyphs)
        if (g.code == c) return &g;
    return &glyphs.back();
}

float GameFont::line() const {
    float l = 0;
    for (const FontGlyph& g : glyphs) l = std::max(l, g.oy + g.h);
    return l;
}

float GameFont::width(std::u16string_view s, float size) const {
    if (s.empty() || glyphs.empty()) return 0.0f;
    const FontGlyph* g = find(s[0]);
    float w = -g->ox;
    for (std::size_t k = 1; k < s.size(); ++k) {
        w += g->advance;
        g = find(s[k]);
    }
    w += g->ox + g->w;
    return w * scale_x * size;
}

float GameFont::size_for(float height) const {
    const float l = line();
    return l > 0 && scale_y > 0 ? height / (l * scale_y) : 0.0f;
}

bool parse_font_def(const std::uint8_t* p, std::size_t n, GameFont& out, std::string* why) {
    if (n < 0x220 || rd32(p) != kFontTag) return fail(why, "not a font definition");
    out = GameFont{};
    out.scale_x = rdf(p + 8);
    out.scale_y = rdf(p + 12);
    out.baseline = rdf(p + 16);
    if (!(out.scale_x > 0 && out.scale_x < 100 && out.scale_y > 0 && out.scale_y < 100)) return fail(why, "bad cell scale");
    std::uint32_t lo = 0xFFFFFFFFu;
    for (unsigned i = 0; i < 0x80u; ++i) lo = std::min(lo, rd32(p + 0x20 + 4 * i));
    const std::uint32_t hi = rd32(p + 0x1C);
    if (lo < 0x220 || hi < lo || (hi - lo) % 0x20u || hi + 0x20u > n || (hi - lo) / 0x20u >= 4096) return fail(why, "bad glyph table");
    for (std::uint32_t at = lo; at <= hi; at += 0x20u) {
        FontGlyph g;
        g.u = rdf(p + at);
        g.v = rdf(p + at + 4);
        g.w = rdf(p + at + 8);
        g.h = rdf(p + at + 12);
        g.ox = rdf(p + at + 16);
        g.oy = rdf(p + at + 20);
        g.advance = rdf(p + at + 24);
        g.code = std::uint16_t(p[at + 28] | p[at + 29] << 8);
        out.glyphs.push_back(g);
    }
    return true;
}

std::optional<ArtImage> txd_texture(const std::uint8_t* p, std::size_t n, std::string_view name, std::string* why) {
    const b3asset::Bytes f{p, n};
    std::vector<b3asset::TxdEntry> entries;
    if (!b3asset::read_txd(f, entries, why)) return std::nullopt;
    for (const auto& e : entries) {
        b3asset::RasterHeader h;
        if (!b3asset::read_raster_header(f, e.offset, h) || !same_ci(h.name, name)) continue;
        auto gs = std::make_unique<b3asset::GsMemory>();
        b3asset::DecodedRaster r;
        if (!b3asset::decode_raster(f, e.offset, *gs, r, 0, why) || r.levels.empty()) return std::nullopt;
        ArtImage im;
        im.w = r.levels[0].w;
        im.h = r.levels[0].h;
        im.rgba = std::move(r.levels[0].rgba);
        im.tw = 1 << ((h.tex0 >> 26) & 0xF);
        im.th = 1 << ((h.tex0 >> 30) & 0xF);
        return im;
    }
    if (why) *why = "no texture " + std::string(name);
    return std::nullopt;
}

// ---- the program file ----

bool ElfFile::parse(std::string* why) {
    segments.clear();
    const std::size_t n = bytes.size();
    if (n < 0x34 || std::memcmp(bytes.data(), "\x7F" "ELF", 4) != 0 || bytes[4] != 1 || bytes[5] != 1) return fail(why, "not a 32-bit little-endian ELF");
    const std::uint32_t phoff = rd32(&bytes[0x1C]);
    const unsigned phentsize = unsigned(bytes[0x2A] | bytes[0x2B] << 8), phnum = unsigned(bytes[0x2C] | bytes[0x2D] << 8);
    if (phentsize < 32 || phoff > n || std::size_t(phnum) * phentsize > n - phoff) return fail(why, "bad program headers");
    for (unsigned i = 0; i < phnum; ++i) {
        const std::uint8_t* ph = &bytes[phoff + std::size_t(i) * phentsize];
        if (rd32(ph) != 1) continue;   // PT_LOAD
        Segment s{rd32(ph + 8), rd32(ph + 4), rd32(ph + 16)};
        if (s.size == 0 || s.offset > n || s.size > n - s.offset) continue;
        segments.push_back(s);
    }
    if (segments.empty()) return fail(why, "no loadable segment");
    return true;
}

std::optional<std::size_t> ElfFile::offset_of(std::uint32_t va, std::size_t len) const {
    for (const Segment& s : segments)
        if (va >= s.va && std::uint64_t(va) + len <= std::uint64_t(s.va) + s.size) return std::size_t(s.offset) + (va - s.va);
    return std::nullopt;
}

std::optional<std::uint32_t> ElfFile::u32(std::uint32_t va) const {
    const auto o = offset_of(va, 4);
    if (!o) return std::nullopt;
    return rd32(&bytes[*o]);
}

bool load_game_fonts(const ElfFile& elf, GameFont (&fonts)[3], std::string* why) {
    for (int k = 0; k < 3; ++k) {
        const FontSlot& s = kSlots[k];
        const auto def = elf.u32(s.def), dict = elf.u32(s.dict);
        const auto def_at = def ? elf.offset_of(*def, 0x220) : std::nullopt;
        const auto dict_at = dict ? elf.offset_of(*dict, 16) : std::nullopt;
        if (!def_at || !dict_at) return fail(why, std::string(s.tex) + ": not in this program (not the retail SLUS_210.50?)");
        if (!parse_font_def(&elf.bytes[*def_at], elf.bytes.size() - *def_at, fonts[k], why)) return false;
        if (rd32(&elf.bytes[*dict_at]) != kTxdMagic) return fail(why, std::string(s.tex) + ": no texture dictionary");
        auto t = txd_texture(&elf.bytes[*dict_at], elf.bytes.size() - *dict_at, s.tex, why);
        if (!t) return false;
        fonts[k].tex = std::move(*t);
    }
    return true;
}

// ---- files ----

fs::path find_ci(const fs::path& dir, std::string_view rel) {
    // iso1: a disc image (or a folder in one): the image's own case-insensitive names (b3asset/discfs.h)
    if (std::error_code ec; !fs::is_directory(dir, ec)) return b3asset::disc::resolve_ci(dir, std::string(rel));
    fs::path at = dir;
    std::size_t p = 0;
    while (p <= rel.size()) {
        std::size_t e = rel.find_first_of("/\\", p);
        if (e == std::string_view::npos) e = rel.size();
        const std::string_view part = rel.substr(p, e - p);
        p = e + 1;
        if (part.empty()) {
            if (e == rel.size()) break;
            continue;
        }
        std::error_code ec;
        fs::path found;
        if (fs::exists(at / std::string(part), ec)) found = at / std::string(part);
        else
            for (fs::directory_iterator it(at, ec), end; !ec && it != end; it.increment(ec))
                if (same_ci(it->path().filename().string(), part)) {
                    found = it->path();
                    break;
                }
        if (found.empty()) return {};
        at = found;
        if (e == rel.size()) break;
    }
    return at;
}

std::u16string to_u16(std::string_view s) {
    std::u16string out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i++]);
        if (b < 0x80) { out += char16_t(b); continue; }
        int n = 0;
        unsigned cp = 0;
        if ((b & 0xE0) == 0xC0) { n = 1; cp = b & 0x1Fu; }
        else if ((b & 0xF0) == 0xE0) { n = 2; cp = b & 0x0Fu; }
        else if ((b & 0xF8) == 0xF0) { n = 3; cp = b & 0x07u; }
        else { out += u'?'; continue; }
        bool ok = true;
        for (int k = 0; k < n; ++k) {
            if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3Fu);
        }
        out += ok && cp < 0x10000 ? char16_t(cp) : u'?';
    }
    return out;
}

const ArtImage* GameArt::image(const std::string& name) const {
    const auto it = images.find(name);
    return it == images.end() ? nullptr : &it->second;
}

GameArt load_game_art(const fs::path& game_dir, const b3mod::Overlay* mods) {
    GameArt a;
    if (game_dir.empty()) {
        a.why = "no game data folder";
        return a;
    }
    {
        ElfFile elf;
        const fs::path prog = find_ci(game_dir, "SLUS_210.50");
        std::string why;
        if (prog.empty() || !read_all(prog, elf.bytes)) a.why = "no SLUS_210.50";
        else if (!elf.parse(&why) || !load_game_fonts(elf, a.font, &why)) a.why = "fonts: " + why;
        else a.fonts = true;
    }
    struct Want { const char* file; std::vector<const char*> names; };
    const Want wants[] = {{"DATA/GLOBAL.TXD", {"B3Logo", "grid", "big_curve", "FE_Element01"}},
                          {"DATA/FRONTEND.TXD", {"titlescreen_logo", "Halo"}}};
    for (const Want& w : wants) {
        std::vector<std::uint8_t> bytes;
        const b3mod::OverlayFile* mf = mods ? mods->find(w.file) : nullptr;   // modkit1b: a mod's file first
        const fs::path p = mf && !mf->host.empty() ? mf->host : find_ci(game_dir, w.file);   // (a patch not applied: no host)
        if (p.empty() || !read_all(p, bytes)) {
            a.why += (a.why.empty() ? "" : "; ") + std::string("no ") + w.file;
            continue;
        }
        for (const char* n : w.names)
            if (auto im = txd_texture(bytes.data(), bytes.size(), n)) a.images[n] = std::move(*im);
    }
    return a;
}

}  // namespace b3launcher
