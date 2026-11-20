// test_modtool -- the mod kit's asset tools (include/b3modtool/modtool.h; docs/modding/b3modtool.md): path patterns,
// b3patch diff / apply, the string tables' text form, PNG, rasters written back into a texture dictionary (built here:
// swizzled and plain uploads, CLUTs, mip levels), and pack / verify on mod folders and a fake disc folder the test
// writes under build/test_modtool.scratch; part 3c: JSON, the glTF writer and its check, the game's axes and units
// against glTF's, the order-free triangle hash, a synthetic car file read, exported and imported again (the strip
// builder, the packet writer, the layout, the refusals, a GLB, an object transform; the paint atlas's recolour and
// re-quantising); a synthetic route (STATIC.DAT + STREAMED.DAT) through the track import (the packet writer in every
// vertex layout, identity, an object move, a colour painted, new triangles that grow a block, the refusals).
// Synthetic data only: no game file is read.
#include "test.h"

#include "b3asset/gs_memory.h"
#include "b3asset/png.h"
#include "b3asset/raster.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"
#include "b3modtool/gltf.h"
#include "b3modtool/meshimport.h"
#include "b3modtool/modtool.h"
#include "b3report/zip.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <random>

namespace fs = std::filesystem;
using namespace b3modtool;
using b3asset::GsMemory;
using b3asset::Image;

namespace {

// A folder on a case-sensitive disk (Linux) or not (macOS / Windows defaults: two names that differ only in case are
// one file there)
bool case_sensitive_dir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    { std::ofstream(dir / "cs_probe") << "1"; }
    const bool sensitive = !fs::exists(dir / "CS_PROBE", ec);
    fs::remove(dir / "cs_probe", ec);
    return sensitive;
}


const fs::path kRoot = fs::path("build") / "test_modtool.scratch";

void write(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << bytes;
}
std::string str(const Buf& b) { return std::string(b.begin(), b.end()); }
std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
Buf random_bytes(std::size_t n, unsigned seed) {
    std::mt19937 r(seed);
    Buf b(n);
    for (auto& c : b) c = std::uint8_t(r());
    return b;
}
bool has(const std::vector<std::string>& v, const std::string& part) {
    for (const std::string& s : v)
        if (s.find(part) != std::string::npos) return true;
    return false;
}

// ---- a synthetic texture dictionary ----
void p32(Buf& b, std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = std::uint8_t(v >> (8 * i)); }
void p64(Buf& b, std::size_t at, std::uint64_t v) { for (int i = 0; i < 8; ++i) b[at + i] = std::uint8_t(v >> (8 * i)); }
int log2c(std::uint32_t v) { int l = 0; while ((1u << l) < v) ++l; return l; }

struct Upload { int dpsm; std::uint32_t dbp, dbw, w, h; };
struct RasterSpec {
    int psm = b3asset::PSMT8, cpsm = b3asset::PSMCT32;
    std::uint32_t w = 16, h = 16, levels = 1, palettes = 0;
    std::uint32_t tbp[7] = {0, 64, 96, 0, 0, 0, 0}, tbw[7] = {1, 1, 1, 1, 1, 1, 1};
    std::uint32_t cbp = 200;
    std::vector<Upload> uploads;
    std::string name = "tex";
};

// Builds a TXD of one raster: texels and CLUT written into a GS memory by `fill`, then sent as the given uploads
// (the GS memory read back in each upload's format, as the game's swizzled rasters are).
template <class Fill>
Buf make_txd(const RasterSpec& s, Fill&& fill) {
    GsMemory g;
    fill(g);
    const std::size_t O = 0x40;
    Buf b(O + 0xC8, 0);
    p32(b, 0, 0x543C0000u); p32(b, 4, 0xBCDEED81u); p32(b, 8, 1); p32(b, 12, 0x10);
    p32(b, 0x10, 0x1234); p32(b, 0x18, std::uint32_t(O));
    p32(b, O + 0x0C, s.w); p32(b, O + 0x10, s.h); p32(b, O + 0x14, std::uint32_t(b3asset::psm_bits(s.psm)));
    b[O + 0x20] = 4;
    const std::uint64_t tex0 = std::uint64_t(s.tbp[0]) | std::uint64_t(s.tbw[0]) << 14 | std::uint64_t(s.psm) << 20 |
                               std::uint64_t(log2c(s.w)) << 26 | std::uint64_t(log2c(s.h)) << 30 | 1ull << 34 | std::uint64_t(s.cpsm) << 51;
    p64(b, O + 0x3C, tex0);
    p32(b, O + 0x44, s.cbp);
    b[O + 0x4A] = std::uint8_t((s.levels - 1) << 2);
    std::uint64_t m1 = 0, m2 = 0;
    for (int i = 0; i < 3; ++i) {
        m1 |= (std::uint64_t(s.tbp[1 + i]) | std::uint64_t(s.tbw[1 + i]) << 14) << (20 * i);
        m2 |= (std::uint64_t(s.tbp[4 + i]) | std::uint64_t(s.tbw[4 + i]) << 14) << (20 * i);
    }
    p64(b, O + 0x4C, m1);
    p64(b, O + 0x54, m2);
    b[O + 0xA0] = std::uint8_t(s.palettes);
    for (std::size_t i = 0; i < s.name.size(); ++i) b[O + 0xA8 + i] = std::uint8_t(s.name[i]);
    const std::size_t up = b.size();
    p32(b, O + 0x24, std::uint32_t(up - O));
    const std::size_t n = s.uploads.size();
    b.resize(up + 16 + 64 * n, 0);
    p32(b, up, std::uint32_t(n));
    p32(b, up + 8, std::uint32_t(n));
    for (std::size_t k = 0; k < n; ++k) {
        const Upload& u = s.uploads[k];
        const std::size_t e = up + 16 + 64 * k;
        p32(b, e, (1u << 28) | 2);
        p64(b, e + 16, 1ull | 1ull << 60);                       // GIF PACKED, 1 loop, 1 reg
        p64(b, e + 24, 0xE);                                     // A+D
        p64(b, e + 32, std::uint64_t(u.dbp) << 32 | std::uint64_t(u.dbw) << 48 | std::uint64_t(u.dpsm) << 56);
        b[e + 40] = 0x50;                                        // BITBLTBUF
        // the image data, read back from the GS memory in the upload's format
        Buf data;
        const int bits = b3asset::psm_bits(u.dpsm);
        for (std::uint32_t y = 0; y < u.h; ++y)
            for (std::uint32_t x = 0; x < u.w; ++x) {
                const std::uint32_t v = g.read(u.dpsm, u.dbp, u.dbw, x, y);
                if (bits == 4) { if (x % 2 == 0) data.push_back(std::uint8_t(v & 0xF)); else data.back() |= std::uint8_t(v << 4); }
                else for (int k2 = 0; k2 < bits / 8; ++k2) data.push_back(std::uint8_t(v >> (8 * k2)));
            }
        while (data.size() % 16) data.push_back(0);
        const std::size_t qwc = data.size() / 16;
        const std::size_t src = b.size();
        b.resize(src + 16 * (5 + qwc), 0);
        p64(b, src, 3ull | 1ull << 60);                           // PACKED, 3 loops of A+D
        p64(b, src + 8, 0xE);
        p64(b, src + 16, 0); b[src + 24] = 0x51;                  // TRXPOS 0,0
        p64(b, src + 32, std::uint64_t(u.w) | std::uint64_t(u.h) << 32); b[src + 40] = 0x52;   // TRXREG
        p64(b, src + 48, 0); b[src + 56] = 0x53;                  // TRXDIR host -> local
        p64(b, src + 64, std::uint64_t(qwc) | 2ull << 58);        // IMAGE
        std::copy(data.begin(), data.end(), b.begin() + std::ptrdiff_t(src + 80));
        p32(b, e + 48, (3u << 28) | std::uint32_t(5 + qwc));
        p32(b, e + 52, std::uint32_t(src - O));
    }
    return b;
}

// a 256-colour CLUT (CT32, CSM1) of distinct colours, alpha 0x80 except a few translucent ones
void fill_clut256(GsMemory& g, std::uint32_t cbp, bool duplicate = false) {
    for (std::uint32_t i = 0; i < 256; ++i) {
        const std::uint32_t j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
        std::uint32_t c = (i * 37 & 0xFF) | (i * 91 & 0xFF) << 8 | i << 16 | (i % 7 == 0 ? 0x40u : 0x80u) << 24;
        if (duplicate && i == 9) c = (5 * 37 & 0xFF) | (5 * 91 & 0xFF) << 8 | 5u << 16 | 0x80u << 24;   // entry 9 = entry 5
        g.write(b3asset::PSMCT32, cbp, 1, j & 15, j >> 4, c);
    }
}

RasterSpec t8_swizzled() {
    // 128x64 PSMT8 = one PSMT8 page = a 64x32 CT32 page: uploaded as CT32 (RW's "swizzled" form)
    RasterSpec s;
    s.psm = b3asset::PSMT8;
    s.w = 128; s.h = 64;
    s.tbw[0] = 2;
    s.cbp = 64;
    s.uploads = {{b3asset::PSMCT32, 0, 1, 64, 32}, {b3asset::PSMCT32, 64, 1, 16, 16}};
    return s;
}
Buf t8_txd(bool duplicate = false) {
    const RasterSpec s = t8_swizzled();
    return make_txd(s, [&](GsMemory& g) {
        for (std::uint32_t y = 0; y < s.h; ++y)
            for (std::uint32_t x = 0; x < s.w; ++x) g.write(b3asset::PSMT8, 0, 2, x, y, (x * 3 + y * 5) & 0xFF);
        fill_clut256(g, s.cbp, duplicate);
    });
}

// export -> PNG bytes -> decode -> import with a forced write: the same bytes
bool roundtrip_same(const Buf& txd, bool strict = false) {
    FileSet fsx;
    if (!txd_export(txd, "test", fsx)) return false;
    EncodeOptions o;
    o.force_write = true;
    o.strict = strict;
    Buf out;
    std::string why;
    if (!txd_import(txd, fsx, o, out, nullptr, &why)) { std::printf("    import: %s\n", why.c_str()); return false; }
    return out == txd;
}

}  // namespace

// modsec: the commands that write one file refuse to write it over one of their inputs (the real tool, built beside)
TEST(commands_do_not_write_over_their_input) {
#ifdef B3_MODTOOL_BIN
    const fs::path d = fs::absolute(kRoot / "sameout");
    fs::remove_all(d);
    fs::create_directories(d);
    write(d / "a.bin", "aaaa");
    write(d / "b.bin", "abab");
    write(d / "m.gltf", "{}");
    auto run = [&](const std::string& args) {
        const std::string cmd = std::string("\"") + B3_MODTOOL_BIN + "\" " + args + " >/dev/null 2>&1";
        return std::system(cmd.c_str());
    };
    const std::string a = "'" + (d / "a.bin").string() + "'", b = "'" + (d / "b.bin").string() + "'", m = "'" + (d / "m.gltf").string() + "'";
    CHECK(run("diff " + a + " " + b + " " + a) != 0);
    CHECK(run("diff " + a + " " + b + " " + b) != 0);
    CHECK(run("strings export " + a + " " + a) != 0);
    CHECK(run("vdb export " + a + " " + a) != 0);
    CHECK(run("preview " + m + " " + m) != 0);
    CHECK(read_text(d / "a.bin") == "aaaa" && read_text(d / "b.bin") == "abab" && read_text(d / "m.gltf") == "{}");
    CHECK(run("diff " + a + " " + b + " '" + (d / "ab.b3patch").string() + "'") == 0 && fs::exists(d / "ab.b3patch"));   // another name: fine
#endif
}

// modsec: a pack that fails leaves no partial archive, and an older archive of that name as it was
TEST(pack_failure_leaves_no_partial_archive) {
    const fs::path d = kRoot / "packfail";
    fs::remove_all(d);
    write(d / "mod" / "mod.ini", "[mod]\nid = p\nversion = 1\napi = 1\n");
    write(d / "mod" / "data" / "DATA" / "A.TXT", "a");
    const fs::path out = d / "p.b3mod";
    std::string why;
    CHECK(pack_mod((d / "mod").string(), out.string(), nullptr, &why) && fs::exists(out));
    const std::string good = read_text(out);
    write(d / "mod" / "data" / "DATA" / "B.TXT", "b");
    fs::permissions(d / "mod" / "data" / "DATA" / "B.TXT", fs::perms::none);   // unreadable: the pack fails at it
    const bool unreadable = !std::ifstream(d / "mod" / "data" / "DATA" / "B.TXT").good();   // (root reads it anyway)
    if (unreadable) {
        CHECK(!pack_mod((d / "mod").string(), out.string(), nullptr, &why));
        CHECK(read_text(out) == good);
        CHECK(!fs::exists(d / "p.b3mod.part"));
    }
    fs::permissions(d / "mod" / "data" / "DATA" / "B.TXT", fs::perms::owner_read | fs::perms::owner_write);
    CHECK(pack_mod((d / "mod").string(), out.string(), nullptr, &why) && read_text(out) != good && !fs::exists(d / "p.b3mod.part"));
}

TEST(glob_patterns) {
    CHECK(glob_match("*.TXD", "DATA/GLOBAL.TXD"));
    CHECK(glob_match("*.txd", "DATA/global.txd"));
    CHECK(glob_match("DATA/*.TXD", "DATA/FRONTEND.TXD"));
    CHECK(!glob_match("DATA/*.TXD", "DATA/SUB/X.TXD"));
    CHECK(glob_match("DATA/**", "DATA/SUB/X.TXD"));
    CHECK(glob_match("**/STATIC.DAT", "TRACKS/US/C2_V2/STATIC.DAT"));
    CHECK(glob_match("TRACKS/**/STATIC.DAT", "TRACKS/STATIC.DAT"));
    CHECK(glob_match("data\\global?s.bin", "DATA/GLOBALUS.BIN"));
    CHECK(!glob_match("DATA/GLOBAL?.BIN", "DATA/GLOBALUS.BIN"));
    CHECK(glob_match("*", "SYSTEM.CNF"));
    CHECK(!glob_match("*.RWS", "DATA/GLOBAL.TXD"));
}

TEST(b3patch_diff_apply) {
    const Buf src = random_bytes(200000, 1);
    std::vector<Buf> targets;
    targets.push_back(src);                                         // identical
    Buf t = src; for (std::size_t i = 5000; i < 5100; ++i) t[i] ^= 0x5A; targets.push_back(t);   // edited in place
    t = src; t.insert(t.begin() + 70000, 333, 7); targets.push_back(t);                         // an insert
    t = src; t.erase(t.begin() + 1000, t.begin() + 9000); targets.push_back(t);                 // a cut
    t = src; { Buf more = random_bytes(5000, 2); t.insert(t.end(), more.begin(), more.end()); } targets.push_back(t);
    t = Buf(src.begin() + 100000, src.end()); t.insert(t.end(), src.begin(), src.begin() + 100000); targets.push_back(t);   // moved
    targets.push_back(Buf{});                                       // empty
    targets.push_back(random_bytes(3000, 3));                       // nothing in common
    for (const Buf& d : targets) {
        PatchInfo pi;
        const Buf p = make_patch(src, d, &pi);
        Buf out;
        std::string why;
        CHECK(apply_patch(src, p, out, &why));
        CHECK(out == d);
        CHECK(pi.copied + pi.inserted == d.size());
        PatchInfo again;
        CHECK(read_patch_info(p, again));
        CHECK(again.src_size == src.size() && again.dst_size == d.size() && again.copied == pi.copied);
    }
    // edits in place and moves cost little
    PatchInfo pi;
    CHECK(make_patch(src, targets[1], &pi).size() < 300);
    CHECK(make_patch(src, targets[5], &pi).size() < 100);
    CHECK(make_patch(src, targets[2], &pi).size() < 500);
    // from an empty source
    const Buf p0 = make_patch(Buf{}, targets[1]);
    Buf out;
    CHECK(apply_patch(Buf{}, p0, out) && out == targets[1]);
    // the wrong source, a damaged patch
    const Buf p = make_patch(src, targets[1]);
    std::string why;
    Buf other = src;
    other[10] ^= 1;
    CHECK(!apply_patch(other, p, out, &why));
    CHECK(why.find("another file") != std::string::npos);
    CHECK(!apply_patch(Buf(src.begin(), src.end() - 1), p, out, &why));
    Buf cut(p.begin(), p.end() - 2);
    CHECK(!apply_patch(src, cut, out, &why));
    Buf bad = p;
    bad[60] ^= 0xFF;   // inside the op stream
    CHECK(!apply_patch(src, bad, out, &why));
    Buf ver = p;
    ver[8] = 2;
    CHECK(!read_patch_info(ver, pi, &why) && why.find("version") != std::string::npos);
    CHECK(!read_patch_info(Buf{'x'}, pi));
    // the header as docs/modding/b3patch.md gives it
    CHECK(std::memcmp(p.data(), "B3PATCH\0", 8) == 0);
    std::uint64_t sz = 0;
    for (int i = 7; i >= 0; --i) sz = sz << 8 | p[16 + i];
    CHECK(sz == src.size());
    std::uint64_t h = 0;
    for (int i = 7; i >= 0; --i) h = h << 8 | p[24 + i];
    CHECK(h == b3mod::fnv1a64(src.data(), src.size()));
}

TEST(strings_round_trip) {
    StringTable t;
    const std::uint8_t magic[8] = {0x08, 0x03, 0xF6, 0x69, 0xFA, 0x22, 0x36, 0xB9};
    std::memcpy(t.magic, magic, 8);
    t.s = {u"PRESS START", u"", u"two\nlines", u"back\\slash and tab\t", u"trailing  ", u"  leading", u"caf\u00E9 \u00A9 \u2122",
           u"\U0001F697 car", std::u16string(1, char16_t(0xD800)) + u"x", u"\u0001ctl\u007F", u"glyph \uE001", u"#not a comment",
           u"12 = 13", u"\r"};
    const Buf file = write_strings(t);
    CHECK(is_string_table(file.data(), file.size()));
    StringTable r;
    std::string why;
    CHECK(read_strings(file, r, &why));
    CHECK(r.s == t.s);
    const std::string text = strings_to_text(r, "TEST.BIN");
    CHECK(text.find("4 = trailing\\u0020\\u0020\n") != std::string::npos);
    CHECK(text.find("2 = two\\nlines\n") != std::string::npos);
    CHECK(text.find("7 = \xF0\x9F\x9A\x97 car\n") != std::string::npos);   // U+1F697 as UTF-8
    CHECK(text.find("8 = \\uD800x\n") != std::string::npos);              // an unpaired surrogate escaped
    StringTable fresh;
    std::memcpy(fresh.magic, magic, 8);
    std::size_t changed = 0;
    CHECK(strings_from_text(text, fresh, &why, &changed));
    CHECK(fresh.s == t.s);
    CHECK(changed == t.s.size());
    CHECK(write_strings(fresh) == file);
    // CRLF line ends and a byte order mark
    std::string crlf = "\xEF\xBB\xBF";
    for (char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
    StringTable f2;
    std::memcpy(f2.magic, magic, 8);
    CHECK(strings_from_text(crlf, f2, &why));
    CHECK(f2.s == t.s);
    // a partial import: only the lines given change; a new id continues the table
    StringTable p = r;
    CHECK(strings_from_text("# mine\n0 = PRESS ANY KEY\n14 = new one\n", p, &why, &changed));
    CHECK(changed == 2 && p.s.size() == 15 && p.s[0] == u"PRESS ANY KEY" && p.s[14] == u"new one" && p.s[2] == t.s[2]);
    StringTable q;
    CHECK(read_strings(write_strings(p), q) && q.s == p.s);
    // refused: a gap, an id twice, a bad escape, not UTF-8, a NUL
    StringTable e = r;
    CHECK(!strings_from_text("20 = x\n", e, &why) && why.find("gap") != std::string::npos);
    CHECK(!strings_from_text("1 = a\n1 = b\n", e, &why) && why.find("twice") != std::string::npos);
    CHECK(!strings_from_text("1 = a\\qb\n", e, &why) && why.find("line 1") != std::string::npos);
    CHECK(!strings_from_text("1 = \xC3(\n", e, &why) && why.find("UTF-8") != std::string::npos);
    CHECK(!strings_from_text("1 = a\\u0000\n", e, &why));
    CHECK(!strings_from_text("x = a\n", e, &why));
    CHECK(e.s == r.s);   // unchanged by a failed import
    // a table that is not packed the way write_strings packs it is refused (it could not come back byte for byte)
    Buf gap = file;
    gap.push_back(0);
    gap.push_back(0);
    CHECK(!read_strings(gap, r, &why));
    CHECK(!read_strings(Buf(16, 0), r, &why));
}

TEST(png_round_trip) {
    const Buf px = random_bytes(37 * 23 * 4, 9);
    const Buf png = encode_png(37, 23, px.data());
    int w = 0, h = 0;
    std::vector<std::uint8_t> back;
    std::string why;
    CHECK(decode_png(png, w, h, back, &why));
    CHECK(w == 37 && h == 23 && back == px);
    // the b3asset writer's PNGs (stored deflate) read too
    const std::vector<std::uint8_t> png2 = b3asset::encode_png_rgba(37, 23, px.data());
    CHECK(decode_png(Buf(png2.begin(), png2.end()), w, h, back, &why) && back == px);
    // damaged
    Buf bad = png;
    bad[40] ^= 1;
    CHECK(!decode_png(bad, w, h, back, &why));
    CHECK(!decode_png(Buf{1, 2, 3}, w, h, back, &why));
}

TEST(png_other_colour_types) {
    // a 4-bit paletted PNG with tRNS, 3x2, made here
    auto chunk = [](Buf& o, const char* type, const Buf& d) {
        for (int i = 3; i >= 0; --i) o.push_back(std::uint8_t(d.size() >> (8 * i)));
        Buf td(type, type + 4);
        td.insert(td.end(), d.begin(), d.end());
        o.insert(o.end(), td.begin(), td.end());
        const std::uint32_t c = b3port::crc32_of(td.data(), td.size());
        for (int i = 3; i >= 0; --i) o.push_back(std::uint8_t(c >> (8 * i)));
    };
    Buf o = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    chunk(o, "IHDR", {0, 0, 0, 3, 0, 0, 0, 2, 4, 3, 0, 0, 0});
    chunk(o, "PLTE", {255, 0, 0, 0, 255, 0, 0, 0, 255});
    chunk(o, "tRNS", {255, 128});
    // rows: filter 0, indices 0 1 2 | filter 0, 2 1 0 (4 bits each, high nibble first)
    const Buf raw = {0, 0x01, 0x20, 0, 0x21, 0x00};
    // zlib stream with one stored block
    Buf z = {0x78, 0x01, 1, std::uint8_t(raw.size()), 0, std::uint8_t(~raw.size()), 0xFF};
    z.insert(z.end(), raw.begin(), raw.end());
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    const std::uint32_t ad = b << 16 | a;
    for (int i = 3; i >= 0; --i) z.push_back(std::uint8_t(ad >> (8 * i)));
    chunk(o, "IDAT", z);
    chunk(o, "IEND", {});
    int w, h;
    std::vector<std::uint8_t> px;
    std::string why;
    CHECK(decode_png(o, w, h, px, &why));
    CHECK(w == 3 && h == 2);
    const std::vector<std::uint8_t> want = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255, 0, 0, 255, 255, 0, 255, 0, 128, 255, 0, 0, 255};
    CHECK(px == want);
}

TEST(txd_swizzled_t8_round_trip) {
    const Buf txd = t8_txd();
    RasterView v;
    std::string why;
    CHECK(view_raster(txd, 0x40, v, &why));
    CHECK(v.indexed && v.overlap.empty() && v.dec.levels.size() == 1 && v.dec.levels[0].w == 128);
    CHECK(v.raw[0][5 * 128 + 7] == ((7 * 3 + 5 * 5) & 0xFF));
    CHECK(roundtrip_same(txd));
    CHECK(roundtrip_same(txd, true));   // distinct CLUT colours: also from the pixels alone
    // the export's names
    FileSet fsx;
    CHECK(txd_export(txd, "test", fsx));
    CHECK(fsx.count("000_tex.png") && fsx.count("txd.txt"));
    // unedited import without forcing: nothing changes
    Buf out;
    ImportReport rep;
    CHECK(txd_import(txd, fsx, EncodeOptions{}, out, &rep, &why) && out == txd && rep.changed == 0);
}

TEST(txd_edits_read_back) {
    const Buf txd = t8_txd();
    RasterView v;
    CHECK(view_raster(txd, 0x40, v));
    // 1. colours of the CLUT moved around (a mirror): exact, CLUT kept
    Image m = v.dec.levels[0];
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) std::memcpy(&m.rgba[(std::size_t(y) * m.w + x) * 4], &v.dec.levels[0].rgba[(std::size_t(y) * m.w + (m.w - 1 - x)) * 4], 4);
    Buf a = txd;
    EncodeResult er;
    std::string why;
    CHECK(encode_raster(a, 0x40, {m}, EncodeOptions{}, &er, &why));
    CHECK(er.changed && !er.new_palette && er.nearest == 0 && a.size() == txd.size());
    RasterView va;
    CHECK(view_raster(a, 0x40, va) && va.dec.levels[0].rgba == m.rgba && va.clut_raw == v.clut_raw);
    // 2. new colours, at most 256: a new CLUT holds them exactly
    Image inv = v.dec.levels[0];
    for (std::size_t i = 0; i < inv.rgba.size(); i += 4) inv.rgba[i] = std::uint8_t(255 - inv.rgba[i]);
    Buf b = txd;
    CHECK(encode_raster(b, 0x40, {inv}, EncodeOptions{}, &er, &why));
    CHECK(er.new_palette);
    RasterView vb;
    CHECK(view_raster(b, 0x40, vb) && vb.dec.levels[0].rgba == inv.rgba);
    // 3. more than 256 colours: reduced (median cut), every texel one of the new CLUT's colours
    Image many = v.dec.levels[0];
    for (std::size_t i = 0; i < many.rgba.size(); i += 4) { many.rgba[i] = std::uint8_t(i / 4); many.rgba[i + 1] = std::uint8_t(i / 1024 * 8); many.rgba[i + 2] = 0; many.rgba[i + 3] = 255; }
    Buf c = txd;
    CHECK(encode_raster(c, 0x40, {many}, EncodeOptions{}, &er, &why));
    CHECK(er.new_palette && c.size() == txd.size());
    RasterView vc;
    CHECK(view_raster(c, 0x40, vc));
    int close = 0;
    for (std::size_t i = 0; i < many.rgba.size(); i += 4) close += std::abs(int(vc.dec.levels[0].rgba[i]) - int(many.rgba[i])) <= 16;
    CHECK(close > int(many.rgba.size() / 4) * 9 / 10);
    // 4. --keep-palette: the nearest CLUT colours, the CLUT unchanged
    Buf d = txd;
    EncodeOptions kp;
    kp.keep_palette = true;
    CHECK(encode_raster(d, 0x40, {inv}, kp, &er, &why));
    CHECK(!er.new_palette && er.nearest > 0);
    RasterView vd;
    CHECK(view_raster(d, 0x40, vd) && vd.clut_raw == v.clut_raw);
    // 5. the wrong size is refused, the file untouched
    Image small;
    small.w = 64; small.h = 64; small.rgba.assign(64 * 64 * 4, 0);
    Buf e = txd;
    CHECK(!encode_raster(e, 0x40, {small}, EncodeOptions{}, &er, &why) && why.find("size cannot change") != std::string::npos && e == txd);
}

TEST(txd_duplicate_clut_colours) {
    // two CLUT entries of one colour: unedited texels keep their index (identical bytes); from the pixels alone
    // (strict) the second entry's texels take the first: different bytes, the same picture
    const Buf txd = t8_txd(true);
    CHECK(roundtrip_same(txd));
    CHECK(!roundtrip_same(txd, true));
    FileSet fsx;
    CHECK(txd_export(txd, "t", fsx));
    EncodeOptions o;
    o.force_write = o.strict = true;
    Buf out;
    CHECK(txd_import(txd, fsx, o, out));
    RasterView a, b;
    CHECK(view_raster(txd, 0x40, a) && view_raster(out, 0x40, b) && a.dec.levels[0].rgba == b.dec.levels[0].rgba);
}

TEST(txd_ct32_mips) {
    // 32x32 CT32 with 3 levels, each its own upload
    RasterSpec s;
    s.psm = b3asset::PSMCT32;
    s.w = s.h = 32;
    s.levels = 3;
    s.tbp[0] = 0; s.tbp[1] = 32; s.tbp[2] = 64;
    s.uploads = {{b3asset::PSMCT32, 0, 1, 32, 32}, {b3asset::PSMCT32, 32, 1, 16, 16}, {b3asset::PSMCT32, 64, 1, 8, 8}};
    const Buf txd = make_txd(s, [&](GsMemory& g) {
        for (std::uint32_t l = 0; l < 3; ++l)
            for (std::uint32_t y = 0; y < (32u >> l); ++y)
                for (std::uint32_t x = 0; x < (32u >> l); ++x)
                    g.write(b3asset::PSMCT32, s.tbp[l], 1, x, y, (x * 8) | (y * 8) << 8 | (l * 60) << 16 | ((x + y) % 3 ? 0x80u : 0x30u) << 24);
    });
    CHECK(roundtrip_same(txd));
    CHECK(roundtrip_same(txd, true));
    FileSet fsx;
    CHECK(txd_export(txd, "t", fsx));
    CHECK(fsx.count("000_tex.mip1.png") && fsx.count("000_tex.mip2.png"));
    // level 0 edited, the mip PNGs as exported: the mips are rebuilt from level 0
    RasterView v;
    CHECK(view_raster(txd, 0x40, v));
    Image e = v.dec.levels[0];
    for (std::size_t i = 0; i < e.rgba.size(); i += 4) e.rgba[i + 2] = 200;
    Buf a = txd;
    EncodeResult er;
    CHECK(encode_raster(a, 0x40, {e}, EncodeOptions{}, &er));
    RasterView va;
    CHECK(view_raster(a, 0x40, va));
    CHECK(va.dec.levels[0].rgba == e.rgba);
    bool blue = true;
    for (std::size_t i = 0; i < va.dec.levels[1].rgba.size(); i += 4) blue = blue && va.dec.levels[1].rgba[i + 2] == 200;
    CHECK(blue);
    CHECK(has(er.notes, "rebuilt"));
    // --keep-mips: they stay as stored
    Buf b = txd;
    EncodeOptions km;
    km.keep_mips = true;
    CHECK(encode_raster(b, 0x40, {e}, km, &er));
    RasterView vb;
    CHECK(view_raster(b, 0x40, vb) && vb.dec.levels[1].rgba == v.dec.levels[1].rgba);
    // through the file set: renamed name part, a stray file noted
    FileSet in;
    in["000_whatever.png"] = encode_png(e.w, e.h, e.rgba.data());
    in["notes.png"] = Buf{1};
    Buf out;
    ImportReport rep;
    std::string why;
    CHECK(txd_import(txd, in, EncodeOptions{}, out, &rep, &why));
    CHECK(out == a && rep.changed == 1 && has(rep.notes, "notes.png"));
}

TEST(txd_t4_and_ct16) {
    // PSMT4 with a 16-entry CT32 CLUT, uploaded as PSMT4
    RasterSpec s;
    s.psm = b3asset::PSMT4;
    s.w = 32; s.h = 16;
    s.cbp = 100;
    s.uploads = {{b3asset::PSMT4, 0, 1, 32, 16}, {b3asset::PSMCT32, 100, 1, 8, 2}};
    const Buf t4 = make_txd(s, [&](GsMemory& g) {
        for (std::uint32_t y = 0; y < 16; ++y)
            for (std::uint32_t x = 0; x < 32; ++x) g.write(b3asset::PSMT4, 0, 1, x, y, (x + y) & 0xF);
        for (std::uint32_t i = 0; i < 16; ++i) g.write(b3asset::PSMCT32, 100, 1, i & 7, i >> 3, (i * 16) | (255 - i * 16) << 8 | 0x80u << 24);
    });
    RasterView v;
    std::string why;
    CHECK(view_raster(t4, 0x40, v, &why));
    CHECK(v.indexed && v.clut_raw.size() == 16);
    CHECK(roundtrip_same(t4));
    CHECK(roundtrip_same(t4, true));
    // CT16 direct, alpha bit
    RasterSpec c;
    c.psm = b3asset::PSMCT16;
    c.w = c.h = 16;
    c.uploads = {{b3asset::PSMCT16, 0, 1, 16, 16}};
    const Buf ct16 = make_txd(c, [&](GsMemory& g) {
        for (std::uint32_t y = 0; y < 16; ++y)
            for (std::uint32_t x = 0; x < 16; ++x) g.write(b3asset::PSMCT16, 0, 1, x, y, (x * 2) | (y * 2) << 5 | 7u << 10 | ((x ^ y) & 1 ? 0x8000u : 0u));
    });
    CHECK(roundtrip_same(ct16));
    CHECK(roundtrip_same(ct16, true));
    RasterView w;
    CHECK(view_raster(ct16, 0x40, w));
    Image e = w.dec.levels[0];
    for (std::size_t i = 0; i < e.rgba.size(); i += 4) { e.rgba[i] = 255 - e.rgba[i]; }
    Buf b = ct16;
    CHECK(encode_raster(b, 0x40, {e}, EncodeOptions{}));
    RasterView wb;
    CHECK(view_raster(b, 0x40, wb) && wb.dec.levels[0].rgba == e.rgba);
}

TEST(txd_overlapping_uploads_export_only) {
    RasterSpec s;
    s.psm = b3asset::PSMCT32;
    s.w = s.h = 16;
    s.uploads = {{b3asset::PSMCT32, 0, 1, 16, 16}, {b3asset::PSMCT32, 0, 1, 8, 8}};   // the second rewrites a corner
    const Buf txd = make_txd(s, [&](GsMemory& g) {
        for (std::uint32_t y = 0; y < 16; ++y)
            for (std::uint32_t x = 0; x < 16; ++x) g.write(b3asset::PSMCT32, 0, 1, x, y, x | y << 8 | 0x80000000u);
    });
    RasterView v;
    CHECK(view_raster(txd, 0x40, v));
    CHECK(!v.overlap.empty());
    Image e = v.dec.levels[0];
    e.rgba[0] ^= 0xFF;
    Buf b = txd;
    std::string why;
    CHECK(!encode_raster(b, 0x40, {e}, EncodeOptions{}, nullptr, &why) && why.find("export only") != std::string::npos);
    CHECK(b == txd);
    // unedited: nothing to write, no error
    CHECK(encode_raster(b, 0x40, {v.dec.levels[0]}, EncodeOptions{}));
}

TEST(pack_and_verify) {
    fs::remove_all(kRoot);
    const fs::path disc = kRoot / "disc", mod = kRoot / "mods" / "my-mod";
    const Buf disc_bin = random_bytes(4096, 11);
    write(disc / "DATA" / "THING.BIN", str(disc_bin));
    write(disc / "DATA" / "OTHER.BIN", str(random_bytes(1000, 12)));
    write(mod / "mod.ini", "[mod]\nid = my-mod\nname = My mod\nversion = 1.0\napi = 1\n");
    write(mod / "README.txt", "made-up example\n");
    write(mod / "LICENSE.txt", "CC0\n");
    write(mod / "data" / "DATA" / "NEW.TXT", "brand new file\n");
    Buf edited = disc_bin;
    edited[100] ^= 1;
    write(mod / "patches" / "DATA" / "THING.BIN.b3patch", str(make_patch(disc_bin, edited)));
    write(mod / ".DS_Store", "x");
    VerifyReport r;
    CHECK(verify_mod(mod.string(), disc.string(), r));
    CHECK(r.errors.empty());
    CHECK(r.id == "my-mod" && r.overlay_files == 1 && r.patches == 1);
    CHECK(has(r.notes, ".DS_Store"));
    // pack: mod.ini first, the dot file left out; the archive verifies the same
    const fs::path zip = kRoot / "my-mod.b3mod";
    std::size_t n = 0;
    std::string why;
    CHECK(pack_mod(mod.string(), zip.string(), &n, &why));
    CHECK(n == 5);
    Buf zb;
    CHECK(read_host_file(zip.string(), zb));
    std::vector<ZipEntry> es;
    CHECK(zip_list(zb, es, &why) && es.size() == 5 && es[0].name == "mod.ini");
    Buf ini;
    CHECK(zip_read(zb, es[0], ini) && str(ini).find("id = my-mod") != std::string::npos);
    VerifyReport rz;
    CHECK(verify_mod(zip.string(), disc.string(), rz) && rz.errors.empty() && rz.overlay_files == 1 && rz.patches == 1);
    // a copy of a disc file (anywhere in the mod), a mostly-copied file, a patch for another file
    write(mod / "data" / "DATA" / "THING.BIN", str(disc_bin));
    write(mod / "extra" / "copy.dat", str(disc_bin));
    write(mod / "data" / "DATA" / "OTHER.BIN", str(edited));   // not OTHER.BIN's bytes, but mostly THING.BIN's
    write(mod / "patches" / "DATA" / "OTHER.BIN.b3patch", str(make_patch(disc_bin, edited)));
    VerifyReport rb;
    CHECK(verify_mod(mod.string(), disc.string(), rb));
    CHECK(has(rb.errors, "data/DATA/THING.BIN: a copy of the disc's DATA/THING.BIN"));
    CHECK(has(rb.errors, "extra/copy.dat: a copy of the disc's DATA/THING.BIN"));
    CHECK(has(rb.errors, "OTHER.BIN.b3patch: made from another version"));
    // patches3: a patch and a whole file for one path (the loader refuses that mod)
    CHECK(has(rb.errors, "patches/DATA/THING.BIN.b3patch and data/DATA/THING.BIN: a patch and a file for the same disc path"));
    // without the disc the copies are not seen
    VerifyReport rn;
    CHECK(verify_mod(mod.string(), "", rn) && rn.errors.size() == 2 && has(rn.errors, "OTHER.BIN: a patch and a file for the same disc path"));
    // a mostly-copied replacement of the disc's own file: a warning, and pack refuses it unless forced
    fs::remove(mod / "data" / "DATA" / "THING.BIN");
    fs::remove(mod / "extra" / "copy.dat");
    fs::remove(mod / "patches" / "DATA" / "OTHER.BIN.b3patch");
    fs::remove(mod / "patches" / "DATA" / "THING.BIN.b3patch");
    Buf mostly = disc_bin;
    for (std::size_t i = 0; i < 200; ++i) mostly[i] = 0;
    write(mod / "data" / "DATA" / "THING.BIN", str(mostly));
    VerifyReport rm;
    CHECK(verify_mod(mod.string(), disc.string(), rm) && rm.errors.empty() && has(rm.warnings, "of it is the disc's DATA/THING.BIN"));
    CHECK(rm.mostly_disc.size() == 1 && rm.mostly_disc[0] == "data/DATA/THING.BIN");
    CHECK(!pack_allowed(rm, false, &why) && why.find("b3modtool diff") != std::string::npos && why.find("--force") != std::string::npos);
    CHECK(pack_allowed(rm, true));
    CHECK(!pack_allowed(rb, true, &why) && why.find("fix the errors") != std::string::npos);   // errors: never, forced or not
    // the loader's rules: the program, two names for one path, a bad manifest
    write(mod / "data" / "SLUS_210.50", "x");
    write(mod / "data" / "data" / "thing.bin", "y");
    VerifyReport rl;
    CHECK(verify_mod(mod.string(), "", rl));
    CHECK(has(rl.errors, "SLUS_210.50"));
    if (case_sensitive_dir(mod / "probe")) CHECK(has(rl.errors, "names differ only in case"));
    write(mod / "mod.ini", "[mod]\nname = no id\n");
    VerifyReport ri;
    CHECK(verify_mod(mod.string(), "", ri) && has(ri.errors, "mod.ini"));
    // an archive with mod.ini in a folder, and one with a path that leaves the mod (zip slip)
    {
        b3port::ZipWriter z;
        CHECK(z.open(kRoot / "nested.b3mod"));
        CHECK(z.add("my-mod/mod.ini", std::string("[mod]\nid = x\napi = 1\n"), 0));
        CHECK(z.add("../evil.txt", std::string("x"), 0));
        CHECK(z.close());
    }
    VerifyReport rz2;
    verify_mod((kRoot / "nested.b3mod").string(), "", rz2);
    CHECK(has(rz2.errors, "leaves the mod"));
    CHECK(has(rz2.errors, "found my-mod/mod.ini"));
    fs::remove_all(kRoot);
}

// patches3: b3modtool's own exports inside a mod are flagged (unless --allow-exports); patches only from [files]
// patches; a [textures] pack's files counted
TEST(verify_exports_patches_textures) {
    fs::remove_all(kRoot);
    const fs::path mod = kRoot / "mods" / "ex";
    auto clean = [&]() {
        fs::remove_all(mod);
        write(mod / "mod.ini", "[mod]\nid = ex\nversion = 1\napi = 1\n[textures]\npack = textures\n");
        write(mod / "README.txt", "made-up\n");
        write(mod / "data" / "DATA" / "NEW.BIN", "my own bytes, not the game's\n");
    };
    clean();
    VerifyReport r;
    CHECK(verify_mod(mod.string(), "", r) && r.errors.empty() && has(r.warnings, "[textures] pack textures: no replacement textures"));
    // a txd export folder (as `txd export` writes it), an audio export folder
    write(mod / "work" / "fe" / "txd.txt", "# b3modtool txd export: FRONTEND.TXD, 2 rasters\n# <file stem> ...\n000_a 8x8 T8 levels 1 offset 0x40\n");
    write(mod / "work" / "fe" / "000_a.png", std::string(40, 'p'));
    write(mod / "work" / "fe" / "001_b.png", std::string(40, 'p'));
    write(mod / "snd" / "audio.txt", "# b3modtool audio: X.RWS: RWS stream \"x\", 1 segment x sub-stream items\n000.wav 22050 Hz\n");
    write(mod / "snd" / "000.wav", std::string(40, 'w'));
    // a whole strings table and a whole tuning table as text; a partial strings text is the mod's own
    write(mod / "all-strings.txt", "# b3modtool strings: GLOBALUS.BIN, 4 strings\n0 = A\n1 = B\n2 = C\n3 = D\n");
    write(mod / "my-strings.txt", "# b3modtool strings: GLOBALUS.BIN, 4 strings\n# my change\n2 = SEE\n");
    write(mod / "tuning.txt", "# b3modtool vdb: VDB.XML: 4 values (4 named), 1 config files (1 named)\nCar | a = 1\nCar | b = 2\nCar | c = 3\n");
    VerifyReport e;
    CHECK(verify_mod(mod.string(), "", e));
    CHECK(e.errors.size() == 4);
    CHECK(has(e.errors, "work/fe/: a txd export (work/fe/txd.txt and 2 PNGs)"));
    CHECK(has(e.errors, "snd/: an audio export (snd/audio.txt and 1 WAVs)"));
    CHECK(has(e.errors, "all-strings.txt: a strings export (4 of the table's 4 strings"));
    CHECK(has(e.errors, "tuning.txt: a vdb export (3 of the table's 4 values"));
    CHECK(!has(e.errors, "my-strings.txt") && has(e.errors, "--allow-exports"));
    VerifyOptions allow;
    allow.allow_exports = true;
    VerifyReport ea;
    CHECK(verify_mod(mod.string(), "", ea, allow) && ea.errors.empty() && has(ea.notes, "a txd export") && has(ea.notes, "(allowed: --allow-exports)"));
    // the same inside a .b3mod
    const fs::path zip = kRoot / "ex.b3mod";
    CHECK(pack_mod(mod.string(), zip.string()));
    VerifyReport ez;
    CHECK(verify_mod(zip.string(), "", ez) && ez.errors.size() == 4);
    // patches: only in the [files] patches folder; elsewhere a warning; a non-patch file there too
    clean();
    const Buf src = random_bytes(512, 3);
    Buf dst = src;
    dst[7] ^= 0x55;
    write(mod / "patches" / "DATA" / "X.BIN.b3patch", str(make_patch(src, dst)));
    write(mod / "patches" / "notes.txt", "x");
    write(mod / "extra" / "Y.BIN.b3patch", str(make_patch(src, dst)));
    write(mod / "textures" / "UI" / "1a2b-3c4d-00002213.dds", "dds");
    write(mod / "textures" / "readme.txt", "x");
    VerifyReport rp;
    CHECK(verify_mod(mod.string(), "", rp) && rp.errors.empty() && rp.patches == 1 && rp.textures == 1);
    CHECK(has(rp.warnings, "extra/Y.BIN.b3patch: a b3patch outside patches/"));
    CHECK(has(rp.warnings, "patches/notes.txt: not a .b3patch file"));
    CHECK(has(rp.notes, "textures/readme.txt: not a replacement texture") && has(rp.notes, "1 replacement textures"));
    CHECK(!has(rp.notes, "not applied by the loader"));
    // another patches folder
    write(mod / "mod.ini", "[mod]\nid = ex\nversion = 1\napi = 1\n[files]\npatches = deltas\n");
    VerifyReport rq;
    CHECK(verify_mod(mod.string(), "", rq) && rq.patches == 0 && has(rq.warnings, "patches/DATA/X.BIN.b3patch: a b3patch outside deltas/"));
    fs::remove_all(kRoot);
}

TEST(file_kinds) {
    const std::uint8_t txd[8] = {0x00, 0x00, 0x3C, 0x54, 0x81, 0xED, 0xDE, 0xBC};
    CHECK(file_kind("DATA/X.TXD", txd, 8) == "txd");
    const std::uint8_t st[16] = {0x08, 0x03, 0xF6, 0x69, 0xFA, 0x22, 0x36, 0xB9};
    CHECK(file_kind("DATA/GLOBALUS.BIN", st, 16) == "strings");
    CHECK(file_kind("TRACKS/US/C2_V2/STATIC.DAT", nullptr, 0) == "track");
    CHECK(file_kind("PVEH/X/CAR1.BGV", nullptr, 0) == "car");
    CHECK(file_kind("SOUND/X.AWD", nullptr, 0) == "awd sound bank");
}

// ---- part 3b: VDB.XML ----

TEST(gt_hash_known) {
    // the key string of vdb.md's example (a name, group and file from the code)
    CHECK_BITS(std::uint32_t(gt_hash("Minimum Air Distance (Metres)Score/Boost/Air/../Export/ValueDB/Score.cfg")), 0x58C54868u);
    CHECK_BITS(std::uint32_t(vdb_key("Minimum Air Distance (Metres)", "Score/Boost/Air", "../Export/ValueDB/Score.cfg")), 0x58C54868u);
    CHECK_BITS(std::uint32_t(gt_hash("../Export/ValueDB/Score.cfg")), 0x9169203Eu);
    CHECK_BITS(std::uint32_t(gt_hash("")), 0xFFFFFFFFu);    // no final inversion
    CHECK_BITS(std::uint32_t(gt_hash("a")), 0xE84841BCu);
    CHECK(vdb_key("n", "g/", "f") == gt_hash("ng/f"));      // a group ending in '/' gets no second one
    CHECK(gt_hash_ok("abc") && !gt_hash_ok("\xC3\xA9"));
}

std::uint32_t fbits(float f) { std::uint32_t w; std::memcpy(&w, &f, 4); return w; }

Vdb sample_vdb() {
    Vdb v;
    VdbValue w;
    for (int i = 0; i < 40; ++i) {   // words with keys on both sides of 0
        w.word = fbits(float(i) * 0.25f);
        v.values[std::int32_t(0x9E3779B9u * std::uint32_t(i + 1))] = w;
    }
    VdbValue vec;
    vec.kind = VdbValue::Vector;
    vec.block = {fbits(1), fbits(2), fbits(3), fbits(0)};
    v.values[-5] = vec;
    v.values[7] = vec;
    VdbValue arr;
    arr.kind = VdbValue::Array;
    arr.block = {1, 2, 3};
    v.values[-100] = arr;
    arr.block = {5, 6};
    v.values[100] = arr;
    v.files[gt_hash("a.cfg")] = 1;
    v.files[gt_hash("b.cfg")] = 0;
    return v;
}

TEST(vdb_layout_round_trip) {
    const Vdb v = sample_vdb();
    Buf b;
    std::string why;
    CHECK(write_vdb(v, b, &why));
    const std::uint32_t nv = std::uint32_t(v.values.size());
    auto u32 = [&](std::size_t o) { return std::uint32_t(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | std::uint32_t(b[o + 3]) << 24); };
    CHECK(u32(0) == 2 && u32(4) == nv && u32(8) == 2 && u32(0xC) == 2);   // +0x08: the number of vectors
    const std::size_t endv = 0x14 + 8 * nv, first = (endv + 15) & ~std::size_t(15);
    for (std::size_t i = endv; i < first; ++i) CHECK(b[i] == 0xFD);
    std::int64_t prev = INT64_MIN;
    std::vector<std::uint32_t> offs;
    for (std::uint32_t i = 0; i < nv; ++i) {
        const std::int32_t k = std::int32_t(u32(0x14 + 8 * i + 4));
        CHECK(k > prev);   // sorted, signed
        prev = k;
        if (k == -5) offs.push_back(u32(0x14 + 8 * i));
        if (k == 7) offs.push_back(u32(0x14 + 8 * i));
        if (k == -100) offs.push_back(u32(0x14 + 8 * i));
        if (k == 100) offs.push_back(u32(0x14 + 8 * i));
    }
    // vectors first (16-aligned, in key order), then the arrays packed in key order, then the file records
    CHECK(offs.size() == 4 && offs[1] == first && offs[2] == first + 16 && offs[0] == first + 32 && offs[3] == first + 32 + 12);
    CHECK(u32(0x10) == first + 32 + 12 + 8 && b.size() == u32(0x10) + 16);
    Vdb back;
    CHECK(read_vdb(b, back, &why));
    CHECK(back.values.size() == v.values.size() && back.files == v.files && back.pad == std::string(first - endv, '\xFD'));
    for (const auto& [k, x] : v.values) CHECK(back.values[k].kind == x.kind && back.values[k].word == x.word && back.values[k].block == x.block);
    Buf again;
    CHECK(write_vdb(back, again) && again == b);
    // the game's binary search needs the order: an unsorted file is refused
    Buf bad = b;
    std::swap_ranges(bad.begin() + 0x14, bad.begin() + 0x1C, bad.begin() + 0x1C);
    CHECK(!read_vdb(bad, back, &why) && why.find("sorted") != std::string::npos);
    // the size limit
    Vdb big = v;
    for (int i = 0; i < 9300; ++i) big.values[std::int32_t(0x12345 + i * 3)] = VdbValue{};
    CHECK(!write_vdb(big, again, &why) && why.find("73728") != std::string::npos);
}

TEST(vdb_text_round_trip) {
    VdbNames names;
    std::string why;
    CHECK(vdb_names_add(names,
                        "# test table\n"
                        "v\tfloat\t0\tScore/Boost/Air\tMinimum Air Distance (Metres)\t../Export/ValueDB/Score.cfg\n"
                        "v\tint\t3\tScore/BP\tCategory BP\t../Export/ValueDB/Score.cfg\n"
                        "v\tvector\t0\tCamera/Follow/\tOffset\t../Export/ValueDB/Camera/<vehicle>.cfg\n"
                        "f\t../Export/ValueDB/Score.cfg\n",
                        &why));
    const std::int32_t kf = vdb_key("Minimum Air Distance (Metres)", "Score/Boost/Air", "../Export/ValueDB/Score.cfg");
    const std::int32_t ka = vdb_key("Category BP", "Score/BP", "../Export/ValueDB/Score.cfg");
    const std::int32_t kv = vdb_key("Offset", "Camera/Follow/", "../Export/ValueDB/Camera/SPRTCAR3.cfg");
    CHECK(names.values.count(kf) && names.values.count(ka) && names.values.count(kv));   // <vehicle> filled in
    Vdb v = sample_vdb();
    v.values[kf] = VdbValue{VdbValue::Word, fbits(25.0f), {}};
    v.values[ka] = VdbValue{VdbValue::Array, 0, {std::uint32_t(-5), 10, 15}};
    v.values[kv] = VdbValue{VdbValue::Vector, 0, {fbits(0.1f), fbits(-2.5f), fbits(1e-7f), 0x7FC00000u}};   // a NaN as its raw word
    v.files[gt_hash("../Export/ValueDB/Score.cfg")] = 1;
    Buf orig;
    CHECK(write_vdb(v, orig, &why));
    VdbTextStats st;
    const std::string text = vdb_to_text(v, names, "test", &st);
    CHECK(st.named == 3 && st.unnamed == v.values.size() - 3 && st.files_named == 1);
    CHECK(text.find("[../Export/ValueDB/Score.cfg]\n") != std::string::npos);
    CHECK(text.find("Score/Boost/Air | Minimum Air Distance (Metres) = 25.0\n") != std::string::npos);
    CHECK(text.find("Score/BP | Category BP = [-5, 10, 15]\n") != std::string::npos);
    CHECK(text.find("Camera/Follow/ | Offset = (0.1, -2.5, 1e-07, 0x7FC00000)\n") != std::string::npos);
    CHECK(text.find("@file ../Export/ValueDB/Score.cfg = 1\n") != std::string::npos);
    Vdb fresh;
    Buf rebuilt;
    CHECK(vdb_from_text(text, names, fresh, &why) && write_vdb(fresh, rebuilt, &why));
    CHECK(rebuilt == orig);
    // an edit over the original: one line is enough; a float written as an integer is still a float
    Vdb ed = v;
    std::size_t changed = 0;
    CHECK(vdb_from_text("[../Export/ValueDB/Score.cfg]\nScore/Boost/Air | Minimum Air Distance (Metres) = 30\n", names, ed, &why, &changed));
    CHECK(changed == 1 && ed.values[kf].word == fbits(30.0f));
    // errors name the line
    Vdb e2 = v;
    CHECK(!vdb_from_text("[../Export/ValueDB/Score.cfg]\nScore/BP | Category BP = [1, 2]\n", names, e2, &why) && why.find("line 2") != std::string::npos);
    CHECK(!vdb_from_text("[../Export/ValueDB/Score.cfg]\nScore/BP | Category BP = 1.5\n", names, e2, &why));
    CHECK(!vdb_from_text("Score/Boost/Air | Minimum Air Distance (Metres) = 1.0\n", names, e2, &why) && why.find("config file") != std::string::npos);
    CHECK(!vdb_from_text("0x00000001 = 1\n0x00000001 = 2\n", names, e2, &why) && why.find("line 1") != std::string::npos);
    // a new value by key, a new name in a section: both added
    Vdb e3 = v;
    CHECK(vdb_from_text("0x00000123 = 1.5\n[x.cfg]\nG | New = 7\n", names, e3, &why, &changed));
    CHECK(changed == 2 && e3.values.size() == v.values.size() + 2 && e3.values[0x123].word == fbits(1.5f) && e3.values[vdb_key("New", "G", "x.cfg")].word == 7);
}

// ---- part 3b: audio ----

double snr_db(const std::vector<std::int16_t>& a, const std::vector<std::int16_t>& b) {
    double s = 0, n = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) { s += double(a[i]) * a[i]; n += double(a[i] - b[i]) * (a[i] - b[i]); }
    return n > 0 ? 10 * std::log10(s / n) : 200;
}

std::vector<std::int16_t> test_tone(std::size_t n, double hz, double rate, unsigned seed) {
    std::mt19937 r(seed);
    std::vector<std::int16_t> s(n);
    for (std::size_t i = 0; i < n; ++i)
        s[i] = std::int16_t(std::lround(9000 * std::sin(6.2831853 * hz * double(i) / rate) + 3000 * std::sin(6.2831853 * hz * 3.7 * double(i) / rate) +
                                        int(r() % 600) - 300));
    return s;
}

TEST(adpcm_encode_decode) {
    // the decoder as the SPU2 decodes: a frame of filter 1, shift 12 and nibbles 1 gives 1, 1 + 60/64 ...
    std::uint8_t f[16] = {0x1C, 0x02};
    for (int i = 2; i < 16; ++i) f[i] = 0x11;
    const std::vector<std::int16_t> d = adpcm_decode(f, 16);
    CHECK(d.size() == 28 && d[0] == 1 && d[1] == 2);   // 1 + ((1 * 60 + 32) >> 6) = 2
    // the encoder's quality on a tone with noise
    const std::vector<std::int16_t> x = test_tone(28 * 400, 440, 32000, 1);
    const Buf enc = adpcm_encode(x.data(), x.size(), nullptr, 0);
    CHECK(enc.size() == 400 * 16);
    for (std::size_t k = 0; k < enc.size(); k += 16) CHECK(enc[k + 1] == 0x02 && (enc[k] >> 4) <= 4 && (enc[k] & 0xF) <= 12);
    const double q = snr_db(x, adpcm_decode(enc.data(), enc.size()));
    CHECK(q > 25);
    // random frames decoded and encoded again with the originals: every frame kept, byte for byte
    Buf rnd = random_bytes(16 * 300, 9);
    for (std::size_t k = 0; k < rnd.size(); k += 16) {
        rnd[k] = std::uint8_t((rnd[k] % 5) << 4 | (rnd[k + 1] % 13));
        rnd[k + 1] = k == 0 ? 0x06 : k + 16 == rnd.size() ? 0x03 : 0x02;
    }
    const std::vector<std::int16_t> rd = adpcm_decode(rnd.data(), rnd.size());
    std::size_t kept = 0;
    CHECK(adpcm_encode(rd.data(), rd.size(), rnd.data(), rnd.size(), 0x02, &kept) == rnd && kept == 300);
    // longer audio: the flags follow the original (loop start first, end flag last)
    std::vector<std::int16_t> longer = rd;
    longer.resize(rd.size() + 100, 0);
    const Buf e2 = adpcm_encode(longer.data(), longer.size(), rnd.data(), rnd.size());
    CHECK(e2.size() == 16 * 304 && e2[1] == 0x06 && e2[16 * 303 + 1] == 0x03 && e2[16 * 299 + 1] == 0x02 && e2[16 * 300 + 1] == 0x02);
}

TEST(wav_round_trip) {
    Pcm p;
    p.rate = 22050;
    p.channels = 2;
    p.s = {0, 1, -1, 32767, -32768, 1234};
    Pcm q;
    std::string why;
    CHECK(read_wav(write_wav(p), q, &why) && q.rate == 22050 && q.channels == 2 && q.s == p.s);
    // a 24-bit and a float WAV
    Buf w24 = write_wav(p);
    {
        Buf b(44 + 6 * 3);
        std::memcpy(b.data(), w24.data(), 44);
        p32(b, 4, std::uint32_t(b.size() - 8));
        b[34] = 24;
        p32(b, 40, 18);
        for (std::size_t i = 0; i < 6; ++i) { const std::uint32_t v = std::uint32_t(std::int32_t(p.s[i]) << 8); b[44 + 3 * i] = std::uint8_t(v); b[45 + 3 * i] = std::uint8_t(v >> 8); b[46 + 3 * i] = std::uint8_t(v >> 16); }
        CHECK(read_wav(b, q, &why) && q.s == p.s);
        Buf f(44 + 6 * 4);
        std::memcpy(f.data(), w24.data(), 44);
        p32(f, 4, std::uint32_t(f.size() - 8));
        f[20] = 3;
        f[34] = 32;
        p32(f, 40, 24);
        for (std::size_t i = 0; i < 6; ++i) { const float x = float(p.s[i]) / 32767.0f; std::memcpy(&f[44 + 4 * i], &x, 4); }
        CHECK(read_wav(f, q, &why) && q.s.size() == 6 && std::abs(q.s[5] - 1234) <= 1 && q.s[3] == 32767);
    }
    CHECK(!read_wav(Buf(10, 0), q, &why));
}

// a synthetic wave bank (audio.md §4): 0x800 header, the RwaWaveDict at 0x2C, list nodes, waves, names; the data
Buf make_bank(const std::vector<Buf>& waves, const std::vector<int>& rates) {
    const std::size_t hs = 0x800;
    std::vector<std::size_t> offs;
    std::size_t data = 0;
    for (const Buf& w : waves) { offs.push_back(data); data = (data + w.size() + 0x7F) & ~std::size_t(0x7F); }
    data += 0x80;   // trailing bytes after the last wave, as on the disc
    Buf b(hs + data, 0);
    p32(b, 0, 0x809);
    p32(b, 8, std::uint32_t(hs));
    p32(b, 0xC, 0x2C);
    p32(b, 0x10, 0xDEADBEEF);   // a left-over word
    p32(b, 0x14, std::uint32_t(data));
    std::memcpy(&b[0x600], "testbank", 9);
    p32(b, 0x2C + 4, 0x600);
    const std::size_t n = waves.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t node = 0x60 + 12 * i, w = 0x100 + 0x5C * i, name = 0x620 + 16 * i;
        p32(b, node, i ? std::uint32_t(node - 12) : 0);
        p32(b, node + 4, i + 1 < n ? std::uint32_t(node + 12) : 0);
        p32(b, node + 8, std::uint32_t(w));
        std::snprintf(reinterpret_cast<char*>(&b[name]), 16, "w%u", unsigned(i % 1000));
        p32(b, w + 4, std::uint32_t(name));
        for (std::size_t f : {std::size_t(0x10), std::size_t(0x2C)}) {
            p32(b, w + f, std::uint32_t(rates[i]));
            p32(b, w + f + 8, std::uint32_t(waves[i].size()));
            b[w + f + 0xC] = 4;
            b[w + f + 0xD] = 1;
        }
        p32(b, w + 0x4C, std::uint32_t(offs[i]));
        p32(b, w + 0x54, 4);
        std::memcpy(&b[hs + offs[i]], waves[i].data(), waves[i].size());
        for (std::size_t g = offs[i] + waves[i].size(); g < (i + 1 < n ? offs[i] + ((waves[i].size() + 0x7F) & ~std::size_t(0x7F)) : data); ++g)
            b[hs + g] = std::uint8_t(0xA0 + i);   // the gap bytes after a wave (left-overs, kept)
    }
    p32(b, 0x2C + 0x10, 0x60);
    p32(b, 0x2C + 0xC, std::uint32_t(0x60 + 12 * (n - 1)));
    return b;
}

Buf frames_of(const std::vector<std::int16_t>& x, std::uint8_t first, std::uint8_t last) {
    Buf e = adpcm_encode(x.data(), x.size(), nullptr, 0);
    e[1] = first;
    e[e.size() - 15] = last;
    return e;
}

TEST(awd_round_trip) {
    const Buf w0 = frames_of(test_tone(28 * 50, 300, 22050, 2), 0x06, 0x03), w1 = frames_of(test_tone(28 * 33, 800, 32000, 3), 0x02, 0x03),
              w2 = frames_of(test_tone(28 * 9, 100, 11025, 4), 0x02, 0x03);
    const Buf bank = make_bank({w0, w1, w2}, {22050, 32000, 11025});
    AudioFile af;
    std::string why;
    CHECK(is_audio_bank(bank.data(), bank.size()) && audio_read(bank, af, &why));
    CHECK(af.items.size() == 3 && af.name == "testbank" && af.items[0].loops && !af.items[1].loops && af.items[1].rate == 32000 && af.items[2].name == "w2");
    CHECK(audio_channels(bank, af, af.items[1])[0] == w1);
    // every wave through WAV and back: the same bytes
    Buf out;
    AudioImportReport rep;
    const AudioSource same = [&](const AudioItem& it, Pcm& p) { return read_wav(write_wav(audio_decode(bank, af, it)), p); };
    CHECK(audio_import(bank, same, out, &rep, &why) && out == bank && rep.unchanged == 3 && rep.reencoded == 0);
    // wave 0 made longer (and given at another rate, in stereo): the others keep their bytes, the loop and end flags stay
    Pcm longer = audio_decode(bank, af, af.items[0]);
    longer.s.resize(longer.s.size() + 28 * 20, 0);
    const AudioSource edit = [&](const AudioItem& it, Pcm& p) { if (it.index) return false; p = longer; return true; };
    CHECK(audio_import(bank, edit, out, &rep, &why) && rep.reencoded == 1 && rep.length_changed == 1);
    AudioFile ea;
    CHECK(audio_read(out, ea, &why) && ea.items.size() == 3 && ea.items[0].bytes == 70 * 16 && ea.items[0].loops);
    const Buf n0 = audio_channels(out, ea, ea.items[0])[0];
    CHECK(n0[1] == 0x06 && n0[n0.size() - 15] == 0x03);
    CHECK(audio_channels(out, ea, ea.items[1])[0] == w1 && audio_channels(out, ea, ea.items[2])[0] == w2);
    CHECK(out.size() == bank.size() + 0x100);   // 50 -> 70 frames: 0x320 -> 0x460 bytes, 0x380 -> 0x480 aligned
    CHECK(snr_db(audio_decode(bank, af, af.items[0]).s, audio_decode(out, ea, ea.items[0]).s) > 25);
    // a stereo WAV at twice the rate for wave 2: mixed to mono, resampled
    Pcm st;
    st.rate = 22050;
    st.channels = 2;
    for (std::int16_t s : audio_decode(bank, af, af.items[2]).s) { st.s.push_back(s); st.s.push_back(s); st.s.push_back(s); st.s.push_back(s); }
    const AudioSource two = [&](const AudioItem& it, Pcm& p) { if (it.index != 2) return false; p = st; return true; };
    CHECK(audio_import(bank, two, out, &rep, &why) && rep.notes.size() == 2);
    CHECK(audio_read(out, ea, &why) && ea.items[2].rate == 11025 && ea.items[2].bytes == w2.size());
}

// a synthetic stream (audio.md §3): one 0x80D chunk with its 0x80E description and the 0x80F data at 0x800; stereo
// sub-streams of 0x1000-byte channel runs in 0x2000-byte slices of each block
Buf make_stream(const std::vector<std::vector<std::array<Buf, 2>>>& seg_sub_ch, std::size_t nsub) {
    const std::size_t nseg = seg_sub_ch.size(), block = 0x2000 * nsub, data_at = 0x800;
    Buf b(data_at, 0);
    p32(b, 0, 0x80D);
    p32(b, 12, 0x80E);
    p32(b, 16, std::uint32_t(data_at - 12 - 24));
    const std::size_t d = 0x2C;
    p32(b, 24, 0x100);
    p32(b, 28, 0x14);
    p32(b, 32, 0x10);
    p32(b, 36, 0x24);
    p32(b, 40, 4);
    p32(b, d + 4, 1);
    p32(b, d + 0xC, std::uint32_t(nseg));
    p32(b, d + 0x14, std::uint32_t(nsub));
    p32(b, d + 0x1C, 0x800);
    p32(b, d + 0x20, std::uint32_t(block));
    p32(b, d + 0x24, std::uint32_t(data_at));
    std::size_t cur = d + 0x2C;
    std::memcpy(&b[cur], "test", 5);
    cur += 16;
    const std::size_t seg0 = cur;
    cur += 32 * nseg;
    const std::size_t sizes0 = cur;
    cur += 4 * nsub * nseg;
    for (std::size_t i = 0; i < nseg; ++i) { std::snprintf(reinterpret_cast<char*>(&b[cur]), 16, "%02u", unsigned(i % 100)); cur += 16; }
    const std::size_t sub0 = cur;
    cur += 40 * nsub;
    Buf data;
    for (std::size_t i = 0; i < nseg; ++i) {
        std::size_t blocks = 1;
        for (std::size_t k = 0; k < nsub; ++k) blocks = std::max(blocks, (seg_sub_ch[i][k][0].size() + 0xFFF) / 0x1000);
        Buf seg(blocks * block);
        for (std::size_t o = 0; o < seg.size(); o += 16) seg[o + 1] = 2;
        for (std::size_t k = 0; k < nsub; ++k)
            for (std::size_t c = 0; c < 2; ++c) {
                const Buf& x = seg_sub_ch[i][k][c];
                for (std::size_t o = 0; o < x.size(); ++o) seg[(o / 0x1000) * block + 0x2000 * k + c * 0x1000 + o % 0x1000] = x[o];
            }
        p32(b, seg0 + 32 * i + 4, 1);
        p32(b, seg0 + 32 * i + 0xC, 1);
        p32(b, seg0 + 32 * i + 0x18, std::uint32_t(seg.size()));
        p32(b, seg0 + 32 * i + 0x1C, std::uint32_t(data.size()));
        for (std::size_t k = 0; k < nsub; ++k) p32(b, sizes0 + 4 * (nsub * i + k), std::uint32_t(2 * seg_sub_ch[i][k][0].size()));
        data.insert(data.end(), seg.begin(), seg.end());
    }
    for (std::size_t k = 0; k < nsub; ++k) {
        const std::size_t f = sub0 + 40 * k, fmt = cur + 0x30 * k;
        p32(b, f + 0xC, 28);
        p32(b, f + 0x10, 0x2000);
        p32(b, f + 0x14, 1);
        b[f + 0x18] = 0x00;
        b[f + 0x19] = 0x10;
        b[f + 0x1A] = 16;
        p32(b, f + 0x1C, 1);
        p32(b, f + 0x20, 0x2000);
        p32(b, f + 0x24, std::uint32_t(0x2000 * k));
        p32(b, fmt, 32000);
        p32(b, fmt + 8, std::uint32_t(2 * seg_sub_ch[0][k][0].size()));
        b[fmt + 0xC] = 4;
        b[fmt + 0xD] = 2;
    }
    p32(b, data_at - 12, 0x80F);
    p32(b, data_at - 8, std::uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    p32(b, 4, std::uint32_t(b.size() - 12));
    return b;
}

TEST(rws_round_trip) {
    auto ch = [](std::size_t frames, unsigned seed) { return frames_of(test_tone(28 * frames, 200 + seed * 50, 32000, seed), 0x02, 0x02); };
    // two segments x two sub-streams; segment 0 spans two blocks, sub-stream 1 of it shorter
    const std::vector<std::vector<std::array<Buf, 2>>> spec = {
        {{ch(300, 1), ch(300, 2)}, {ch(100, 3), ch(100, 4)}},
        {{ch(50, 5), ch(50, 6)}, {ch(60, 7), ch(60, 8)}},
    };
    const Buf s = make_stream(spec, 2);
    AudioFile af;
    std::string why;
    CHECK(audio_read(s, af, &why));
    CHECK(af.kind == AudioFile::Rws && af.items.size() == 4 && af.name == "test" && af.items[1].sub == 1 && af.items[2].name == "01");
    CHECK(audio_channels(s, af, af.items[0])[1] == spec[0][0][1] && audio_channels(s, af, af.items[3])[0] == spec[1][1][0]);
    CHECK(audio_item_stem(af, af.items[3]) == "001_01.sub1");
    Buf out;
    AudioImportReport rep;
    const AudioSource same = [&](const AudioItem& it, Pcm& p) { return read_wav(write_wav(audio_decode(s, af, it)), p); };
    CHECK(audio_import(s, same, out, &rep, &why) && out == s && rep.unchanged == 4);
    // segment 0 / sub-stream 1 made longer than three blocks: segment 0 grows, segment 1 moves, the rest is unchanged
    Pcm longer = audio_decode(s, af, af.items[1]);
    longer.s.resize(2 * 28 * 800, 0);
    const AudioSource edit = [&](const AudioItem& it, Pcm& p) { if (it.index != 0 || it.sub != 1) return false; p = longer; return true; };
    CHECK(audio_import(s, edit, out, &rep, &why) && rep.reencoded == 1 && rep.length_changed == 1);
    AudioFile ea;
    CHECK(audio_read(out, ea, &why) && ea.items.size() == 4);
    CHECK(ea.segs[0].size == 4 * 0x4000 && ea.segs[1].offset == 4 * 0x4000 && out.size() == s.size() + 2 * 0x4000);
    CHECK(ea.items[1].bytes == 2 * 800 * 16);
    for (std::size_t i : {std::size_t(0), std::size_t(2), std::size_t(3)})
        CHECK(audio_channels(out, ea, ea.items[i]) == audio_channels(s, af, af.items[i]));
    CHECK(snr_db(audio_decode(s, af, af.items[1]).s, audio_decode(out, ea, ea.items[1]).s) > 25);
    // a stream frame keeps the stream's flags (0x02), and the blocks past the data are the silent padding frame
    const Buf c1 = audio_channels(out, ea, ea.items[1])[0];
    for (std::size_t k = 0; k < c1.size(); k += 16) CHECK(c1[k + 1] == 0x02);
}

// ---- part 3c: JSON, glTF, coordinates, car export (synthetic) ----

TEST(json_round_trip) {
    Json j = Json::object();
    j.set("s", Json::str("a \"quoted\"\n\\ line \x01"));
    j.set("i", Json::integer(-1234567890123ll));
    j.set("f", Json::f32(0.1f));
    j.set("d", Json::num(1.0 / 3.0));
    Json a = Json::array();
    a.push(Json::boolean(true));
    a.push(Json());
    a.push(Json::f32(-3.5e-7f));
    j.set("a", std::move(a));
    for (int indent : {-1, 1}) {
        Json back;
        std::string why;
        CHECK(json_parse(j.dump(indent), back, &why));
        CHECK(back.get("s") && back.get("s")->s == j.get("s")->s);
        CHECK(back.get("i") && back.get("i")->as_int() == -1234567890123ll);
        CHECK(back.get("f") && float(back.get("f")->n) == 0.1f);   // %.9g: a float reads back exactly
        CHECK(back.get("d") && back.get("d")->n == 1.0 / 3.0);
        CHECK(back.get("a") && back.get("a")->size() == 3 && back.get("a")->a[1].type == Json::Null &&
              float(back.get("a")->a[2].n) == -3.5e-7f);
    }
    Json bad;
    CHECK(!json_parse("{\"a\": [1, 2}", bad));
    CHECK(!json_parse("[1] x", bad));
    CHECK(json_parse("\"\\u00e9\\ud83d\\ude00\"", bad) && bad.s == "\xC3\xA9\xF0\x9F\x98\x80");
}

// The game's space is glTF's: right-handed, Y up, metres; a stored CGtMatrix3x4 (rows right, up, at, position, row
// vectors) becomes the column-major node matrix with those rows as columns, w lanes dropped
TEST(gltf_axes_and_units) {
    const float p[3] = {1.5f, -2.25f, 3.0f};
    const auto g = gltf_position(p);
    CHECK(g[0] == 1.5f && g[1] == -2.25f && g[2] == 3.0f);
    CHECK(gltf_uv(0.25f, 0.75f)[1] == 0.75f && obj_uv(0.25f, 0.75f)[1] == 0.25f);
    // a wheel frame: 180 degrees about Y (right = -X, at = -Z) at (0.76, 0, 1.24), w lanes not 0 / 1 in the file
    const float stored[16] = {-1, 0, 0, 7, 0, 1, 0, 7, 0, 0, -1, 7, 0.76f, 0, 1.24f, 7};
    const auto m = gltf_matrix(stored);
    CHECK(m[3] == 0 && m[7] == 0 && m[11] == 0 && m[15] == 1);
    // the row-vector rule (as b3asset::read_car places a part): world = x right + y up + z at + position
    const float v[3] = {0.1f, 0.2f, 0.3f};
    const auto w = gltf_apply(m, v, 1.0f);
    for (int c = 0; c < 3; ++c)
        CHECK(w[std::size_t(c)] == v[0] * stored[c] + v[1] * stored[4 + c] + v[2] * stored[8 + c] + stored[12 + c]);
    const auto d = gltf_apply(m, v, 0.0f);
    CHECK(d[0] == -0.1f && d[1] == 0.2f && d[2] == -0.3f);
    // node matrices compose parent x child, and TRS gives the same matrix
    Json n = Json::object();
    n.set("translation", Json::floats(stored + 12, 3));
    const float q[4] = {0, 1, 0, 0};   // 180 degrees about Y
    n.set("rotation", Json::floats(q, 4));
    const auto t = gltf_node_matrix(n);
    for (int i = 0; i < 16; ++i) CHECK_NEAR(t[std::size_t(i)], m[std::size_t(i)], 1e-6);
    const auto mm = gltf_mul(m, m);   // twice: identity rotation, the translation turned and added
    CHECK_NEAR(mm[0], 1, 1e-6);
    CHECK_NEAR(mm[10], 1, 1e-6);
    CHECK_NEAR(mm[12], 0, 1e-6);
    CHECK_NEAR(mm[14], 0, 1e-6);
}

TEST(gltf_writer_synthetic_mesh) {
    GltfBuilder g;
    const std::vector<std::array<float, 3>> pos = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, -2}};
    const std::vector<std::array<float, 3>> nrm = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    const std::vector<std::array<float, 2>> uv = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    const std::vector<std::array<std::uint8_t, 4>> col = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {9, 9, 9, 9}};
    const std::vector<std::uint32_t> idx = {0, 1, 2, 2, 1, 3};
    Json attr = Json::object();
    attr.set("POSITION", Json::integer(g.positions(pos)));
    attr.set("NORMAL", Json::integer(g.vec3(nrm)));
    attr.set("TEXCOORD_0", Json::integer(g.vec2(uv)));
    attr.set("COLOR_0", Json::integer(g.ubyte4(col, true)));
    attr.set("_B3_W", Json::integer(g.uint1({0, 0x8000, 0, 0})));
    Json prim = Json::object();
    prim.set("attributes", std::move(attr));
    prim.set("indices", Json::integer(g.indices(idx, pos.size())));
    Json prims = Json::array();
    prims.push(std::move(prim));
    Json mesh = Json::object();
    mesh.set("primitives", std::move(prims));
    const int mi = g.add("meshes", std::move(mesh));
    Json node = Json::object();
    node.set("mesh", Json::integer(mi));
    const float mtx[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 6, 7, 1};
    node.set("matrix", Json::floats(mtx, 16));
    const int ni = g.add("nodes", std::move(node));
    Json scene = Json::object();
    Json sn = Json::array();
    sn.push(Json::integer(ni));
    scene.set("nodes", std::move(sn));
    g.add("scenes", std::move(scene));
    g.doc.set("scene", Json::integer(0));
    const std::string text = g.text("t.bin");
    Json doc;
    std::string why;
    CHECK(json_parse(text, doc, &why));
    CHECK(g.bin.size() % 4 == 0);   // every view starts 4-aligned; the 6 u16 indices end the buffer at 4n
    std::vector<std::string> errors;
    GltfCheckStats st;
    CHECK(gltf_check(doc, g.bin, errors, &st));
    CHECK(st.vertices == 4 && st.triangles == 2 && st.nodes == 1 && st.primitives == 1);
    CHECK(st.bmin[2] == -2 && st.bmax[0] == 1);
    const Json* a0 = &doc.get("accessors")->a[0];
    CHECK(a0->get("min")->a[2].n == -2 && a0->get("max")->a[1].n == 1);
    CHECK(doc.get("accessors")->a.back().get("componentType")->as_int() == gl::kUShort);
    std::vector<double> back;
    int nc = 0;
    CHECK(gltf_read_accessor(doc, g.bin, 3, back, nc) && nc == 4 && back[0] == 1.0 && back[1] == 0.0);   // normalized
    // the check catches what breaks a file
    Json broken = doc;
    broken.get("accessors")->a[0].get("max")->a[0] = Json::f32(2);
    CHECK(!gltf_check(broken, g.bin, errors));
    std::vector<std::uint8_t> bin2 = g.bin;
    const std::size_t iv = std::size_t(doc.get("bufferViews")->a.back().get("byteOffset")->as_int());
    bin2[iv] = 9;   // index 9 of 4 vertices
    errors.clear();
    CHECK(!gltf_check(doc, bin2, errors) && has(errors, "out of range"));
    std::vector<std::uint8_t> bin3 = g.bin;
    const float nan = std::nanf("");
    std::memcpy(bin3.data() + 4, &nan, 4);
    errors.clear();
    CHECK(!gltf_check(doc, bin3, errors) && has(errors, "NaN"));
    errors.clear();
    CHECK(!gltf_check(doc, std::vector<std::uint8_t>(g.bin.begin(), g.bin.end() - 4), errors) && has(errors, "byteLength"));
    // u32 indices from 65535 vertices up (65535 is u16's restart value)
    GltfBuilder big;
    big.indices({0, 1, 2}, 65535);
    CHECK(big.doc.get("accessors")->a[0].get("componentType")->as_int() == gl::kUInt);
}

TEST(triangle_set_hash_order_free) {
    const std::vector<std::array<float, 3>> pos = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 1, 0}};
    const std::vector<std::uint8_t> ex = {1, 2, 3, 4, 5};
    const std::uint64_t h = triangle_set_hash(pos, ex, 1, {0, 1, 2, 2, 1, 3});
    CHECK(h == triangle_set_hash(pos, ex, 1, {1, 3, 2, 1, 2, 0}));   // other order, rotated, same winding
    CHECK(h == triangle_set_hash(pos, ex, 1, {0, 1, 2, 2, 1, 3, 3, 4, 1}));   // + a degenerate one (two equal positions)
    // the same triangles with their vertices duplicated (as a modelling tool may split them)
    const std::vector<std::array<float, 3>> pos2 = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}};
    const std::vector<std::uint8_t> ex2 = {1, 2, 3, 3, 2, 4};
    CHECK(h == triangle_set_hash(pos2, ex2, 1, {0, 1, 2, 3, 4, 5}));
    CHECK(h != triangle_set_hash(pos, ex, 1, {0, 2, 1, 2, 1, 3}));   // winding turned
    const std::vector<std::uint8_t> ex3 = {1, 2, 3, 7, 5};
    CHECK(h != triangle_set_hash(pos, ex3, 1, {0, 1, 2, 2, 1, 3}));   // an attribute changed
}

// A synthetic car file: the header, one LOD block with one L0 entry whose packet unpacks three vertices as the car's
// VU1 program receives them (docs/modding/file-formats/car.md "Mesh packets")
static Buf synthetic_car() {
    Buf f(0x1900, 0);
    p32(f, 0x00, 0x17);
    p32(f, 0x08, std::uint32_t(f.size()));
    p32(f, 0x10, 3);
    p32(f, 0x4C + 4 * 3, 0x16F0);   // LOD 3
    p32(f, 0x16F0 + 0, 0x48);       // slot L0 -> list at 0x1738
    const std::size_t lst = 0x1738;
    f[lst] = 1;                      // one entry
    f[lst + 1] = 0xFF;               // no glass
    p32(f, lst + 4, 0x10);           // entries at 0x1748
    p32(f, lst + 8, 0x47800080);
    const std::size_t e = 0x1748;
    p32(f, e + 0, 0x10);             // the packet at 0x1758
    p32(f, e + 8, 0x00640001);
    p32(f, e + 12, 0x8080);
    std::vector<std::uint32_t> w;    // the VIF stream after the DMA tag's first 8 bytes
    w.push_back(0);                  // NOP (tag+8)
    w.push_back(0);                  // NOP (tag+12)
    w.push_back(0x30000000);         // STROW 65536.0f x4
    for (int i = 0; i < 4; ++i) w.push_back(0x47800000);
    w.push_back(0x01000103);         // STCYCL cl 3 wl 1
    w.push_back(0x6C030000);         // UNPACK V4-32, 3 elements at 0: positions (w 0x8000 on the first: ADC)
    const float P[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0.5f}};
    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 3; ++c) w.push_back(t::fbits(P[i][c]));
        w.push_back(i == 0 ? 0x8000 : 0);
    }
    w.push_back(0x6D034001);         // UNPACK V4-16 unsigned, 3 elements at 1 (slots 1, 4, 7)
    const std::uint16_t U[3][4] = {{4096, 0, 2048, 4096}, {0x0104, 0x0208, 0x030C, 0}, {0, 4096, 0, 0}};
    for (int i = 0; i < 3; ++i) { w.push_back(U[i][0] | std::uint32_t(U[i][1]) << 16); w.push_back(U[i][2] | std::uint32_t(U[i][3]) << 16); }
    w.push_back(0x05000001);         // STMOD offset (the normals only)
    w.push_back(0x6A034002);         // UNPACK V3-8 unsigned, 3 elements at 2: normals (b - 128) / 128 on ROW
    const std::uint8_t N[12] = {128, 255, 128, 128, 128, 255, 0, 128, 128, 0, 0, 0};
    for (int i = 0; i < 3; ++i) w.push_back(std::uint32_t(N[4 * i]) | std::uint32_t(N[4 * i + 1]) << 8 | std::uint32_t(N[4 * i + 2]) << 16 | std::uint32_t(N[4 * i + 3]) << 24);
    w.push_back(0x04000003);         // ITOP 3
    while ((w.size() * 4 + 8) % 16) w.push_back(0);
    const std::size_t at = 0x1758;
    const std::uint32_t qwc = std::uint32_t((w.size() * 4 - 8) / 16);
    p32(f, at, 0x60000000u | qwc);
    p32(f, at + 4, 0);
    for (std::size_t i = 0; i < w.size(); ++i) p32(f, at + 8 + 4 * i, w[i]);
    return f;
}

TEST(car_read_and_export_synthetic) {
    const Buf f = synthetic_car();
    CarFile car;
    std::string why;
    CHECK(car_read(f, car, &why));
    CHECK(car.meshes.size() == 1);
    if (car.meshes.size() != 1) return;
    const CarMesh& m = car.meshes[0];
    CHECK(m.lod == 3 && m.slot == 0 && m.material == 0x00640001 && m.colour == 0x8080 && m.glass_entry == -1);
    CHECK(m.v.size() == 3 && m.batches.size() == 1 && m.batches[0] == 3);
    CHECK(m.tri.size() == 3 && m.tri[0] == 0 && m.tri[1] == 1 && m.tri[2] == 2);   // ADC on vertex 0 closes nothing anyway
    if (m.v.size() == 3) {
        CHECK(m.v[2].pos[1] == 1.0f && m.v[2].pos[2] == 0.5f && m.v[0].w == 0x8000);
        CHECK(m.v[0].uv[0] == 4096 && m.v[0].uv[1] == 0);            // vertex 0: slot 1 lanes 0, 1
        CHECK(m.v[1].uv[0] == 0 && m.v[1].uv[1] == 4096);            // vertex 1: slot 7 lanes 0, 1
        CHECK(m.v[2].uv[0] == 2048 && m.v[2].uv[1] == 4096);         // vertex 2: slot 1 lanes 2, 3
        CHECK(m.v[0].infl[0] == 0x0104 && m.v[1].infl[0] == 0x0208 && m.v[2].infl[0] == 0x030C);   // slot 4, lane j
        CHECK(m.v[0].infl[1] == 0);
        CHECK(m.v[0].nrm[1] == 255 && m.v[1].nrm[2] == 255 && m.v[2].nrm[0] == 0);
    }
    // ADC on vertex 2 (its w lane: word 20 of the stream): no triangle
    CarFile again;
    Buf g2 = f;
    p32(g2, 0x1758 + 8 + 4 * 20, 0x8000);
    CHECK(car_read(g2, again, &why) && again.meshes.size() == 1 && again.meshes[0].tri.empty() && again.meshes[0].v[2].w == 0x8000);
    // export
    FileSet out;
    ExportReport rep;
    CarExportOptions o;
    o.name = "Test Car";
    o.source = "PVEH/TEST/CAR1.BGV";
    CHECK(car_export(f, "TEST_CAR1", o, out, &rep, &why));
    CHECK(out.count("TEST_CAR1.gltf") && out.count("TEST_CAR1.bin") && out.count("TEST_CAR1.b3car.json"));
    Json doc, side;
    const Buf& gt = out["TEST_CAR1.gltf"];
    CHECK(json_parse(std::string(gt.begin(), gt.end()), doc, &why));
    const Buf& sj = out["TEST_CAR1.b3car.json"];
    CHECK(json_parse(std::string(sj.begin(), sj.end()), side, &why));
    CHECK(side.get("source") && side.get("source")->get("fnv1a64")->s == hex64(b3mod::fnv1a64(f.data(), f.size())));
    CHECK(side.get("meshes") && side.get("meshes")->size() == 1 && side.get("meshes")->a[0].get("material")->s == "0x00640001");
    CHECK(side.get("meshes")->a[0].get("mesh_at")->s == "0x00001758");
    std::vector<std::string> errors;
    CHECK(rep.triangles == 1 && rep.vertices == 3 && rep.meshes == 1);
    CHECK(gltf_check(doc, out["TEST_CAR1.bin"], errors));
    CHECK(doc.get("nodes")->a[0].get("name")->s == "Test Car");
    CHECK(side.get("meshes")->a[0].get("tri_hash")->s == hex64(car_mesh_tri_hash(m)));
    // the custom attributes keep the stored fields, one float SCALAR per component (_B3_NRM_X .. _Z bytes, _B3_INFL_0 / _1
    // words, _B3_W): all of one form, as Blender's importer needs; gltf_read_field puts them together again
    const Json& prim = doc.get("meshes")->a[0].get("primitives")->a[0];
    std::vector<double> vals;
    int nc = 0;
    for (const auto& [k, v] : prim.get("attributes")->o) {
        if (k.empty() || k[0] != '_') continue;
        const Json& acc = doc.get("accessors")->a[std::size_t(v.as_int())];
        CHECK(acc.get("type")->s == "SCALAR" && acc.get("componentType")->as_int() == gl::kFloat);
    }
    CHECK(gltf_read_accessor(doc, out["TEST_CAR1.bin"], int(prim.get("attributes")->get("_B3_NRM_Y")->as_int()), vals, nc) && nc == 1 && vals[0] == 255);
    CHECK(gltf_read_accessor(doc, out["TEST_CAR1.bin"], int(prim.get("attributes")->get("_B3_INFL_0")->as_int()), vals, nc) && nc == 1 && vals[1] == 0x0208);
    CHECK(gltf_read_accessor(doc, out["TEST_CAR1.bin"], int(prim.get("attributes")->get("_B3_W")->as_int()), vals, nc) && nc == 1 && vals[0] == 0x8000);
    gltf_read_field(doc, out["TEST_CAR1.bin"], *prim.get("attributes"), "_B3_NRM", {"X", "Y", "Z"}, 255.0, vals, nc);
    CHECK(nc == 3 && vals.size() == 9 && vals[1] == 255);
    gltf_read_field(doc, out["TEST_CAR1.bin"], *prim.get("attributes"), "_B3_INFL", {"0", "1"}, 65535.0, vals, nc);
    CHECK(nc == 2 && vals[2] == 0x0208);
    gltf_read_field(doc, out["TEST_CAR1.bin"], *prim.get("attributes"), "_B3_NONE", {"X"}, 1.0, vals, nc);
    CHECK(nc == 0 && vals.empty());
    CHECK(gltf_read_accessor(doc, out["TEST_CAR1.bin"], int(prim.get("attributes")->get("TEXCOORD_0")->as_int()), vals, nc) && vals[4] == 0.5);
    // OBJ: v counted from the bottom
    FileSet objs;
    o.obj = true;
    CHECK(car_export(f, "TEST_CAR1", o, objs, &rep, &why) && objs.count("TEST_CAR1.obj") && objs.count("TEST_CAR1.mtl"));
    const std::string ot(objs["TEST_CAR1.obj"].begin(), objs["TEST_CAR1.obj"].end());
    CHECK(ot.find("vt 1 1\n") != std::string::npos && ot.find("f 1/1/1 2/2/2 3/3/3") != std::string::npos);
}

// ---- part 3c, second half: the car import (synthetic) ----

namespace {
std::uint32_t rd_u32(const Buf& b, std::size_t at) { return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24; }
FileLoader set_loader(const FileSet& fsx) {
    return [&fsx](const std::string& p, Buf& out) {
        auto it = fsx.find(p);
        if (it == fsx.end()) return false;
        out = it->second;
        return true;
    };
}
// a grid of nx x nz quads in the y = 0 plane, two triangles each, one winding
void grid(int nx, int nz, std::vector<CarVertex>& v, std::vector<std::uint32_t>& tri, float y = 0) {
    v.clear();
    tri.clear();
    for (int j = 0; j <= nz; ++j)
        for (int i = 0; i <= nx; ++i) {
            CarVertex c;
            c.pos[0] = 0.1f * float(i); c.pos[1] = y; c.pos[2] = 0.1f * float(j);
            c.uv[0] = std::uint16_t(100 * i); c.uv[1] = std::uint16_t(100 * j);
            c.infl[0] = 0x0104; c.infl[1] = 0x0208;
            c.nrm[0] = 128; c.nrm[1] = 255; c.nrm[2] = 128;
            v.push_back(c);
        }
    const std::uint32_t w = std::uint32_t(nx + 1);
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) {
            const std::uint32_t a = std::uint32_t(j) * w + std::uint32_t(i), b = a + 1, c = a + w, d = c + 1;
            tri.insert(tri.end(), {a, c, b, b, c, d});
        }
}
std::uint64_t set_hash(const std::vector<CarVertex>& v, const std::vector<std::uint32_t>& tri) {
    std::vector<std::array<float, 3>> pos;
    std::vector<std::uint8_t> ex;
    for (const CarVertex& x : v) {
        pos.push_back({x.pos[0], x.pos[1], x.pos[2]});
        const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(x.uv);
        ex.insert(ex.end(), p, p + 4);
        p = reinterpret_cast<const std::uint8_t*>(x.infl);
        ex.insert(ex.end(), p, p + 4);
        ex.insert(ex.end(), x.nrm, x.nrm + 3);
    }
    return triangle_set_hash(pos, ex, 11, tri);
}
// a .gltf + .bin as one .glb (buffer 0 without a uri: the BIN chunk)
Buf make_glb(const Buf& gltf, const Buf& bin) {
    Json doc;
    json_parse(std::string(gltf.begin(), gltf.end()), doc);
    Json& b0 = doc.get("buffers")->a[0];
    Json nb = Json::object();
    nb.set("byteLength", Json::integer(std::int64_t(bin.size())));
    b0 = nb;
    std::string js = doc.dump();
    while (js.size() % 4) js.push_back(' ');
    Buf bb = bin;
    while (bb.size() % 4) bb.push_back(0);
    Buf g(12, 0);
    auto put = [&](std::uint32_t x) { for (int i = 0; i < 4; ++i) g.push_back(std::uint8_t(x >> (8 * i))); };
    put(std::uint32_t(js.size())); put(0x4E4F534Au); g.insert(g.end(), js.begin(), js.end());
    put(std::uint32_t(bb.size())); put(0x004E4942u); g.insert(g.end(), bb.begin(), bb.end());
    p32(g, 0, 0x46546C67u); p32(g, 4, 2); p32(g, 8, std::uint32_t(g.size()));
    return g;
}
}  // namespace

TEST(car_strip_mesh_winding_and_batches) {
    std::vector<CarVertex> v;
    std::vector<std::uint32_t> tri;
    grid(40, 3, v, tri);   // 240 triangles: strips cross batches
    CarMesh m;
    std::string why;
    CHECK(car_strip_mesh(v, tri, m, &why));
    CHECK(set_hash(m.v, m.tri) == set_hash(v, tri));   // the same triangles, each with its winding
    CHECK(m.tri.size() == tri.size());
    std::size_t n = 0;
    for (std::uint32_t b : m.batches) { CHECK(b >= 3 && b <= std::uint32_t(kCarMaxBatch)); n += b; }
    CHECK(n == m.v.size() && m.batches.size() > 1);
    CHECK(m.v.size() < tri.size() / 3 * 2);   // strips: far fewer vertices than three per triangle
    // separate triangles (every strip starts on an even place) and a degenerate one (left out)
    std::vector<std::uint32_t> lone;
    for (std::uint32_t t = 0; t + 2 < tri.size() && lone.size() < 3 * 37; t += 6) lone.insert(lone.end(), tri.begin() + t, tri.begin() + t + 3);
    std::vector<std::uint32_t> with_degenerate = lone;
    with_degenerate.insert(with_degenerate.end(), {0, 0, 1});
    CarMesh l;
    CHECK(car_strip_mesh(v, with_degenerate, l, &why) && set_hash(l.v, l.tri) == set_hash(v, lone) && l.tri.size() == lone.size());
    // no triangles: one batch that draws nothing
    CarMesh e;
    CHECK(car_strip_mesh(v, {}, e, &why) && e.batches.size() == 1 && e.tri.empty() && e.v.size() == 3);
    // the packets of a stream (each form)
    for (int form = 0; form < 3; ++form) {
        CarPackets pk;
        CHECK(car_encode_mesh(m, form, pk, &why));
        CHECK(pk.first.size() % 16 == 0 && pk.cont.size() % 16 == 0 && !pk.cont.empty());
        CHECK((pk.first[3] == 0x60) && std::size_t(pk.first[0] | pk.first[1] << 8) == pk.first.size() / 16 - 1);
        // every UNPACK's data starts on a quadword (its command word at 12 mod 16)
        for (const Buf* p : {&pk.first, &pk.cont})
            for (std::size_t o = 8; o + 4 <= p->size();) {
                const std::uint32_t code = std::uint32_t((*p)[o]) | std::uint32_t((*p)[o + 1]) << 8 | std::uint32_t((*p)[o + 2]) << 16 | std::uint32_t((*p)[o + 3]) << 24;
                const std::uint32_t cmd = (code >> 24) & 0x7F, num = (code >> 16) & 0xFF;
                if (cmd >= 0x60) {
                    CHECK(o % 16 == 12);
                    const int vn = (cmd >> 2) & 3, vl = cmd & 3;
                    o += 4 + ((std::size_t(num) * std::size_t(vn + 1) * std::size_t(32 >> vl) / 8 + 3) & ~std::size_t(3));
                } else {
                    o += cmd == 0x30 ? 20 : 4;
                }
            }
    }
    CarMesh big = m;
    big.batches = {std::uint32_t(m.v.size())};
    CarPackets pk;
    CHECK(!car_encode_mesh(big, false, pk, &why) && why.find("batch") != std::string::npos);
}

TEST(car_import_synthetic) {
    const Buf f = synthetic_car();
    std::string why;
    // new triangles: a 12 x 4 grid for LOD 3's L0 entry 0 (the packet grows: moved to the end of the file)
    CarImportMesh im;
    im.lod = 3;
    im.slot = 0;
    im.entry = 0;
    grid(12, 4, im.v, im.tri, 0.25f);
    Buf a;
    CarImportReport rep;
    CHECK(car_import_meshes(f, {im}, {}, CarImportOptions{}, a, &rep, &why));
    CHECK(rep.restripped == 1 && rep.moved >= 1 && rep.size_after > f.size() && rep.size_after % 16 == 0);
    CarFile ca;
    CHECK(car_read(a, ca, &why) && ca.meshes.size() == 1);
    CHECK(ca.meshes.size() == 1 && set_hash(ca.meshes[0].v, ca.meshes[0].tri) == set_hash(im.v, im.tri));
    CHECK(rd_u32(a, 8) == a.size());
    // identity: the writer's file exported and imported unchanged gives its bytes (glTF and GLB)
    CarExportOptions o;
    o.source = "PVEH/TEST/CAR1.BGV";
    FileSet fsx;
    CHECK(car_export(a, "T", o, fsx, nullptr, &why));
    Buf b;
    CHECK(car_import(a, set_loader(fsx), "T.gltf", "T.b3car.json", {}, b, &rep, &why) && b == a && rep.unchanged == 1);
    fsx["T.glb"] = make_glb(fsx["T.gltf"], fsx["T.bin"]);
    CHECK(car_import(a, set_loader(fsx), "T.glb", "T.b3car.json", {}, b, &rep, &why) && b == a);
    // an object moved in the modelling tool: its node scaled 2x -> the vertices doubled, the same stream and size
    FileSet ed = fsx;
    Json doc;
    CHECK(json_parse(str(ed["T.gltf"]), doc));
    for (Json& n : doc.get("nodes")->a)
        if (n.get("mesh")) { const float s[3] = {2, 2, 2}; n.set("scale", Json::floats(s, 3)); }
    const std::string t2 = doc.dump();
    ed["T.gltf"] = Buf(t2.begin(), t2.end());
    CHECK(car_import(a, set_loader(ed), "T.gltf", "T.b3car.json", {}, b, &rep, &why) && rep.edited == 1 && b.size() == a.size());
    CarFile cb;
    CHECK(car_read(b, cb) && cb.meshes.size() == 1 && cb.meshes[0].v.size() == ca.meshes[0].v.size());
    CHECK(cb.meshes[0].v[5].pos[0] == 2 * ca.meshes[0].v[5].pos[0] && cb.meshes[0].v[5].pos[1] == 0.5f);
    // from another file: refused
    Buf other = a;
    other[0x14] ^= 1;
    CHECK(!car_import(other, set_loader(fsx), "T.gltf", "T.b3car.json", {}, b, &rep, &why) && why.find("another") != std::string::npos);
    // refusals: an entry the file does not have, an influence zone it does not use, a bad position, the size limit
    CarImportMesh bad = im;
    bad.lod = 2;
    CHECK(!car_import_meshes(f, {bad}, {}, CarImportOptions{}, b, &rep, &why) && why.find("no such mesh") != std::string::npos);
    bad = im;
    bad.v[3].infl[0] = 0x0124;   // zone 9
    CHECK(!car_import_meshes(f, {bad}, {}, CarImportOptions{}, b, &rep, &why) && why.find("zone 9") != std::string::npos);
    bad = im;
    bad.v[3].pos[1] = std::nanf("");
    CHECK(!car_import_meshes(f, {bad}, {}, CarImportOptions{}, b, &rep, &why) && why.find("position") != std::string::npos);
    CarImportOptions small;
    small.max_size = f.size();
    CHECK(!car_import_meshes(f, {im}, {}, small, b, &rep, &why) && why.find("over the limit") != std::string::npos);
    // a mesh that is not one of the car's, and texture coordinates off the texture
    FileSet x = fsx;
    CHECK(json_parse(str(x["T.gltf"]), doc));
    doc.get("meshes")->a[0].set("name", Json::str("Cube"));
    doc.get("meshes")->a[0].set("extras", Json::object());
    for (Json& n : doc.get("nodes")->a) if (n.get("mesh")) n.set("name", Json::str("Cube"));
    const std::string t3 = doc.dump();
    x["T.gltf"] = Buf(t3.begin(), t3.end());
    CHECK(!car_import(a, set_loader(x), "T.gltf", "T.b3car.json", {}, b, &rep, &why) && why.find("not one of the car's meshes") != std::string::npos);
    x = fsx;
    CHECK(json_parse(str(x["T.gltf"]), doc));
    const int uva = int(doc.get("meshes")->a[0].get("primitives")->a[0].get("attributes")->get("TEXCOORD_0")->as_int());
    const Json& acc = doc.get("accessors")->a[std::size_t(uva)];
    const std::size_t at = std::size_t(doc.get("bufferViews")->a[std::size_t(acc.get("bufferView")->as_int())].get("byteOffset")->as_int());
    const float far = 20.0f;
    std::memcpy(&x["T.bin"][at], &far, 4);
    CHECK(!car_import(a, set_loader(x), "T.gltf", "T.b3car.json", {}, b, &rep, &why) && why.find("outside 0 .. 16") != std::string::npos);
}

// What Blender's glTF importer and exporter do to the stored fields (docs/modding/b3modtool.md "Blender"): an unsigned
// short VEC2 attribute is dropped, an unsigned byte VEC4 becomes a byte colour (clamped to 0 / 255), and attributes of
// different widths in one mesh are paired with each other's data at random, so the export writes one float SCALAR per
// component; the import reads that and the older layout, and copes when they are gone or mixed anyway.
TEST(car_import_blender_attributes) {
    const Buf f = synthetic_car();
    std::string why;
    CarImportMesh im;
    im.lod = 3;
    im.slot = 0;
    im.entry = 0;
    grid(12, 4, im.v, im.tri, 0.25f);
    for (std::size_t k = 0; k < im.v.size(); ++k) im.v[k].infl[0] = std::uint16_t(0x0100 | (k % 4) << 2);   // zones 0..3 by place
    Buf a;
    CarImportReport rep;
    CHECK(car_import_meshes(f, {im}, {}, CarImportOptions{}, a, &rep, &why));
    CarFile ca;
    CHECK(car_read(a, ca, &why) && ca.meshes.size() == 1);
    CarExportOptions o;
    o.source = "PVEH/TEST/CAR1.BGV";
    FileSet fsx;
    CHECK(car_export(a, "T", o, fsx, nullptr, &why));
    Json doc;
    CHECK(json_parse(str(fsx["T.gltf"]), doc));
    Json* attrs = doc.get("meshes")->a[0].get("primitives")->a[0].get("attributes");
    auto acc_of = [&](const char* name) -> const Json& { return doc.get("accessors")->a[std::size_t(attrs->get(name)->as_int())]; };
    auto at_of = [&](const char* name) {
        const Json& ac = acc_of(name);
        return std::size_t(doc.get("bufferViews")->a[std::size_t(ac.get("bufferView")->as_int())].get("byteOffset")->as_int());
    };
    // the stored fields as float SCALARs, one per component: what Blender keeps (FLOAT), and all of one form
    for (const char* k : {"_B3_NRM_X", "_B3_NRM_Y", "_B3_NRM_Z", "_B3_INFL_0", "_B3_INFL_1", "_B3_W"})
        CHECK(acc_of(k).get("componentType")->as_int() == gl::kFloat && acc_of(k).get("type")->s == "SCALAR");
    const std::size_t nv = std::size_t(acc_of("_B3_NRM_X").get("count")->as_int());
    const std::size_t infl_at = at_of("_B3_INFL_0");
    auto put_doc = [&](FileSet& s) { const std::string t = doc.dump(); s["T.gltf"] = Buf(t.begin(), t.end()); };
    Buf b;
    // an export in the layout before the per-component attributes (_B3_NRM VEC4, _B3_INFL VEC2, _B3_W u32): still read,
    // the same bytes
    {
        FileSet x = fsx;
        Json d2;
        CHECK(json_parse(str(x["T.gltf"]), d2));
        GltfBuilder g;
        g.doc = d2;
        g.bin = x["T.bin"];
        Json* at2 = g.doc.get("meshes")->a[0].get("primitives")->a[0].get("attributes");
        std::vector<double> n3, i2, w1;
        int c3 = 0, c2 = 0, c1 = 0;
        gltf_read_field(d2, x["T.bin"], *at2, "_B3_NRM", {"X", "Y", "Z"}, 255.0, n3, c3);
        gltf_read_field(d2, x["T.bin"], *at2, "_B3_INFL", {"0", "1"}, 65535.0, i2, c2);
        CHECK(gltf_read_accessor(d2, x["T.bin"], int(at2->get("_B3_W")->as_int()), w1, c1) && c3 == 3 && c2 == 2 && w1.size() == nv);
        std::vector<std::array<float, 4>> n4;
        std::vector<std::array<float, 2>> v2;
        std::vector<std::uint32_t> u1;
        for (std::size_t k = 0; k < nv; ++k) {
            n4.push_back({float(n3[3 * k]), float(n3[3 * k + 1]), float(n3[3 * k + 2]), 0.0f});
            v2.push_back({float(i2[2 * k]), float(i2[2 * k + 1])});
            u1.push_back(std::uint32_t(w1[k]));
        }
        Json old = Json::object();
        for (const auto& [k, v] : at2->o)
            if (k.rfind("_B3_", 0) != 0) old.set(k, v);
        old.set("_B3_NRM", Json::integer(g.vec4(n4)));
        old.set("_B3_INFL", Json::integer(g.vec2(v2)));
        old.set("_B3_W", Json::integer(g.uint1(u1)));
        *at2 = std::move(old);
        const std::string t = g.text(d2.get("buffers")->a[0].get("uri")->s);
        x["T.gltf"] = Buf(t.begin(), t.end());
        while (g.bin.size() % 4) g.bin.push_back(0);
        x["T.bin"] = g.bin;
        CHECK(car_import(a, set_loader(x), "T.gltf", "T.b3car.json", {}, b, &rep, &why) && b == a && rep.unchanged == 1);
    }
    // _B3_NRM_* as a byte colour would give them back (every component 0 or 255): left out, the same bytes
    FileSet x = fsx;
    for (const char* k : {"_B3_NRM_X", "_B3_NRM_Y", "_B3_NRM_Z"}) {
        const std::size_t nrm_at = at_of(k);
        for (std::size_t i = 0; i < nv; ++i) {
            float v;
            std::memcpy(&v, &x["T.bin"][nrm_at + 4 * i], 4);
            v = v > 0 ? 255.0f : 0.0f;
            std::memcpy(&x["T.bin"][nrm_at + 4 * i], &v, 4);
        }
    }
    CHECK(car_import(a, set_loader(x), "T.gltf", "T.b3car.json", {}, b, &rep, &why) && b == a && rep.unchanged == 1);
    bool noted = false;
    for (const std::string& n : rep.notes) noted = noted || n.find("_B3_NRM_X / _Y / _Z are not the export's") != std::string::npos;
    CHECK(noted);
    // one vertex's influence mixed by a tool (not a whole number): that vertex takes the nearest original's, the same bytes
    x = fsx;
    const float mixed = float(0x0104) + 0.5f;
    std::memcpy(&x["T.bin"][infl_at + 4 * 5], &mixed, 4);
    CHECK(car_import(a, set_loader(x), "T.gltf", "T.b3car.json", {}, b, &rep, &why) && b == a);
    noted = false;
    for (const std::string& n : rep.notes) noted = noted || n.find("1 vertices whose _B3_INFL_0 / _1") != std::string::npos;
    CHECK(noted);
    // the influences gone (an export through a tool that drops them) and the object scaled 2x in object mode: the
    // influences found by the mesh's own positions (not the moved ones), so the file's strips stay, transformed
    attrs->o.erase(std::remove_if(attrs->o.begin(), attrs->o.end(), [](const auto& kv) { return kv.first.rfind("_B3_INFL", 0) == 0; }), attrs->o.end());
    for (Json& n : doc.get("nodes")->a)
        if (n.get("mesh")) { const float s[3] = {2, 2, 2}; n.set("scale", Json::floats(s, 3)); }
    x = fsx;
    put_doc(x);
    CHECK(car_import(a, set_loader(x), "T.gltf", "T.b3car.json", {}, b, &rep, &why) && rep.edited == 1 && rep.restripped == 0 && b.size() == a.size());
    CarFile cb;
    CHECK(car_read(b, cb) && cb.meshes.size() == 1 && cb.meshes[0].v.size() == ca.meshes[0].v.size());
    bool same_infl = cb.meshes.size() == 1;
    for (std::size_t k = 0; same_infl && k < ca.meshes[0].v.size(); ++k)
        same_infl = std::memcmp(cb.meshes[0].v[k].infl, ca.meshes[0].v[k].infl, 4) == 0 && cb.meshes[0].v[k].pos[0] == 2 * ca.meshes[0].v[k].pos[0];
    CHECK(same_infl);
}

TEST(raster_paints_recolour_and_requantise) {
    // a 128 x 64 T8 raster with two paints: CLUT 0 at block 64, CLUT 1 four blocks on (the +0x90 table: 0, 4)
    RasterSpec s = t8_swizzled();
    s.palettes = 2;
    s.uploads.push_back({b3asset::PSMCT32, 68, 1, 16, 16});
    Buf txd = make_txd(s, [&](GsMemory& g) {
        for (std::uint32_t y = 0; y < s.h; ++y)
            for (std::uint32_t x = 0; x < s.w; ++x) g.write(b3asset::PSMT8, 0, 2, x, y, (x + y) % 100);
        fill_clut256(g, 64);
        for (std::uint32_t i = 0; i < 256; ++i) {
            const std::uint32_t j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
            g.write(b3asset::PSMCT32, 68, 1, j & 15, j >> 4, (i * 11 & 0xFF) | 0x40u << 8 | (255 - i) << 16 | 0x80u << 24);
        }
    });
    txd[0x40 + 0x92] = 4;
    auto paint = [&](const Buf& b, int k) {
        GsMemory g;
        b3asset::DecodedRaster d;
        b3asset::decode_raster(b3asset::Bytes{b.data(), b.size()}, 0x40, g, d, k);
        return d.levels[0];
    };
    const Image p0 = paint(txd, 0), p1 = paint(txd, 1);
    CHECK(p0.w == 128 && p0.rgba != p1.rgba);
    // nothing edited: nothing changes
    Buf a = txd;
    PaintEncodeResult r;
    std::string why;
    CHECK(encode_raster_paints(a, 0x40, {p0, p1}, &r, &why) && r.changed == 0 && a == txd);
    // a recolour of paint 1 (red <-> blue): only its CLUT; paint 0 unchanged
    Image sw = p1;
    for (std::size_t i = 0; i < sw.rgba.size(); i += 4) std::swap(sw.rgba[i], sw.rgba[i + 2]);
    CHECK(encode_raster_paints(a, 0x40, {Image{}, sw}, &r, &why) && r.changed == 1 && !r.new_indices && a.size() == txd.size());
    CHECK(paint(a, 1).rgba == sw.rgba && paint(a, 0).rgba == p0.rgba);
    // a picture edit (a square of a new colour in paint 0): the index picture is rebuilt; both paints read back exactly
    Image sq = p0;
    for (int y = 10; y < 20; ++y)
        for (int x = 30; x < 50; ++x) { std::uint8_t* q = &sq.rgba[(std::size_t(y) * 128 + std::size_t(x)) * 4]; q[0] = 1; q[1] = 2; q[2] = 3; q[3] = 255; }
    Buf b = txd;
    CHECK(encode_raster_paints(b, 0x40, {sq}, &r, &why) && r.changed == 1 && r.new_indices);
    CHECK(paint(b, 0).rgba == sq.rgba && paint(b, 1).rgba == p1.rgba);
    // a picture of another size is refused
    Image small;
    small.w = 4; small.h = 4; small.rgba.assign(64, 0);
    Buf c = txd;
    CHECK(!encode_raster_paints(c, 0x40, {small}, &r, &why) && why.find("size cannot change") != std::string::npos && c == txd);
}

// ---- part 3c: track import ----
namespace {

// a grid of track vertices (every stored field set) and its triangles
void track_grid(int nx, int nz, std::vector<TrackVertex>& v, std::vector<std::uint32_t>& tri, float y = 0, bool floats = false) {
    v.clear();
    tri.clear();
    for (int j = 0; j <= nz; ++j)
        for (int i = 0; i <= nx; ++i) {
            TrackVertex t;
            t.pos[0] = 2.0f * float(i); t.pos[1] = y; t.pos[2] = 2.0f * float(j);
            if (floats) { const float u = 0.25f * float(i), w = 0.5f * float(j); std::memcpy(&t.uv[0], &u, 4); std::memcpy(&t.uv[1], &w, 4); }
            else { t.uv[0] = 512 * i - 4096; t.uv[1] = 256 * j; }
            t.col[0] = std::uint8_t(10 + i); t.col[1] = std::uint8_t(20 + j); t.col[2] = 0x30; t.col[3] = 0x80;
            t.nrm[0] = 0; t.nrm[1] = 127; t.nrm[2] = 0; t.nrm[3] = std::int8_t(100 + (i & 7));
            for (int c = 0; c < 4; ++c) t.light[c] = 0.125f * float(c + i % 3);
            v.push_back(t);
        }
    const std::uint32_t w = std::uint32_t(nx + 1);
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) {
            const std::uint32_t a = std::uint32_t(j) * w + std::uint32_t(i), b = a + 1, c = a + w, d = c + 1;
            tri.insert(tri.end(), {a, c, b, b, c, d});
        }
}

// A synthetic route: STATIC.DAT with two untextured materials (0: type 0, 1: type 1), one unit; STREAMED.DAT with its
// high block (two meshes, one per material slot) and its LOD block (one mesh), 32 KB each
void put_mesh(Buf& f, std::size_t mesh, std::size_t at, const TrackMesh& m) {
    Buf a, b;
    track_encode_mesh(m, a, b);
    std::copy(a.begin(), a.end(), f.begin() + std::ptrdiff_t(at));
    p32(f, mesh + 0x0C, std::uint32_t(at - mesh));
    if (!b.empty()) {
        const std::size_t at2 = (at + a.size() + 63) & ~std::size_t(63);
        std::copy(b.begin(), b.end(), f.begin() + std::ptrdiff_t(at2));
        p32(f, mesh + 0x1C, std::uint32_t(at2 - mesh));
    }
    // the box: half axes 1000 m along x y z around the origin
    for (int i = 0; i < 3; ++i) { const float h = 1000.0f; std::memcpy(&f[mesh + 16 * std::size_t(i) + 4 * std::size_t(i)], &h, 4); }
    p32(f, mesh + 0x2C, 0xFFFFFFFFu);
    p32(f, mesh + 0x3C, 0x0012BFF4u);
}
void synthetic_route(Buf& st, Buf& sm) {
    st.assign(0x200, 0);
    p32(st, 0x00, 0x27);
    p32(st, 0x04, std::uint32_t(st.size()));
    p32(st, 0x08, 0x100);
    st[0x0C] = 2;
    p32(st, 0x18, 0x60);   // the texture table (no textures)
    st[0x54] = 1;
    p32(st, 0x58, 0x80);   // the unit table
    p32(st, 0x80, 0);
    p32(st, 0x84, 0x8000);
    p32(st, 0x88, 0x8000);
    p32(st, 0x8C, 0x8000);
    for (int k = 0; k < 2; ++k) {   // materials: type k, opaque
        const std::size_t b = 0x100 + 0x28 * std::size_t(k);
        p32(st, b, std::uint32_t(k));
        p32(st, b + 0x20, 0x50003);
        st[b + 0x26] = 0x64;
        st[b + 0x27] = 0x80;
    }
    sm.assign(0x10000, 0);
    std::vector<TrackVertex> v;
    std::vector<std::uint32_t> tri;
    TrackMesh m0, m1, m2;
    track_grid(30, 3, v, tri, 0.0f);
    track_strip_mesh(v, tri, track_material_layout(0, false), m0);
    track_grid(6, 2, v, tri, 5.0f);
    track_strip_mesh(v, tri, track_material_layout(1, false), m1);
    track_grid(4, 1, v, tri, 0.0f);
    track_strip_mesh(v, tri, track_material_layout(0, false), m2);
    for (int part = 0; part < 2; ++part) {
        const std::size_t blk = 0x8000 * std::size_t(part);
        p32(sm, blk, 1);
        p32(sm, blk + 8, 0x8000);
        sm[blk + 0x50] = part ? 1 : 2;
        p32(sm, blk + 0x54, 0xC40 - 0x50);   // the mesh array at +0xC40
        if (part == 0) {
            sm[blk + 0x61] = 2; sm[blk + 0x62] = 2; sm[blk + 0x63] = 2;   // two slots, drawn before the vehicles
            for (int i = 0; i < 8; ++i) sm[blk + 0x64 + std::size_t(i)] = 0xFF;
            sm[blk + 0x74] = 0; sm[blk + 0x76] = 1;                       // slot -> material
            for (int r = 0; r < 17; ++r) {
                const std::size_t row = blk + 0x11A + 0xA9 * std::size_t(r);
                sm[row] = std::uint8_t(r == 0 ? 0 : r);                   // row 0: this unit
                for (int s = 0; s < 166; ++s) sm[row + 3 + std::size_t(s)] = 0xFF;
                if (r == 0) { sm[row + 3] = 0; sm[row + 4] = 1; sm[row + 3 + 83] = 0; }
            }
            put_mesh(sm, blk + 0xC40, blk + 0x1000, m0);
            put_mesh(sm, blk + 0xC80, blk + 0x3000, m1);
        } else {
            put_mesh(sm, blk + 0xC40, blk + 0x1000, m2);
        }
    }
}

std::uint64_t mesh_set(const TrackMesh& m) { return track_mesh_hash(m.v, m.tri, m.layout); }

}  // namespace

TEST(track_writer_layouts_round_trip) {
    // every layout: strips over batches, written and read back with every stored lane
    const int layouts[] = {track_material_layout(0, true), track_material_layout(0, false), track_material_layout(1, true), track_material_layout(1, false),
                           track_material_layout(2, true), track_material_layout(4, true), track_material_layout(5, true)};
    for (int layout : layouts) {
        std::vector<TrackVertex> v;
        std::vector<std::uint32_t> tri;
        track_grid(45, 3, v, tri, 1.0f, layout & kTrackUv32);
        TrackMesh m;
        std::string why;
        CHECK(track_strip_mesh(v, tri, layout, m, &why));
        CHECK(m.batches.size() > 1 && m.tri.size() == tri.size());
        for (std::uint32_t n : m.batches) CHECK(n >= 3 && n <= std::uint32_t(track_max_batch(layout)));
        CHECK(track_mesh_hash(m.v, m.tri, layout) == track_mesh_hash(v, tri, layout));   // the same triangles, each with its winding
        Buf f(0x40, 0), a, b;
        CHECK(track_encode_mesh(m, a, b, &why) && !b.empty() && a.size() % 16 == 0 && b.size() % 16 == 0);
        const std::size_t at1 = f.size();
        f.insert(f.end(), a.begin(), a.end());
        const std::size_t at2 = f.size();
        f.insert(f.end(), b.begin(), b.end());
        TrackMesh back;
        CHECK(track_mesh_read(f, at1, at2, back, &why));
        CHECK(back.layout == layout && back.batches == m.batches && back.tri == m.tri && back.v.size() == m.v.size());
        bool same = true;
        for (std::size_t k = 0; k < m.v.size(); ++k) same = same && !std::memcmp(&back.v[k], &m.v[k], sizeof(TrackVertex));
        CHECK(same);
    }
    // a batch over the layout's limit is refused
    std::vector<TrackVertex> v;
    std::vector<std::uint32_t> tri;
    track_grid(45, 3, v, tri);
    TrackMesh m;
    CHECK(track_strip_mesh(v, tri, track_material_layout(1, true), m));
    m.batches = {std::uint32_t(m.v.size())};
    Buf a, b;
    std::string why;
    CHECK(!track_encode_mesh(m, a, b, &why) && why.find("batch") != std::string::npos);
}

TEST(track_import_synthetic) {
    Buf st, sm;
    synthetic_route(st, sm);
    std::string why;
    TrackFiles tf;
    CHECK(track_files_read(st, sm, tf, &why));
    CHECK(tf.sites.size() == 3 && tf.blocks.size() == 2 && tf.materials.size() == 2);
    // identity: exported (glTF and GLB) and imported unchanged
    TrackExportOptions o;
    o.source = "TRACKS/XX/T1_V1";
    FileSet fsx;
    CHECK(track_export(st, sm, {}, "T", o, fsx, nullptr, &why));
    Buf s2, m2;
    TrackImportReport rep;
    CHECK(track_import(st, sm, set_loader(fsx), "T.gltf", "T.b3track.json", {}, s2, m2, &rep, &why) && s2 == st && m2 == sm && rep.meshes == 3 && rep.unchanged == 3);
    fsx["T.glb"] = make_glb(fsx["T.gltf"], fsx["T.bin"]);
    CHECK(track_import(st, sm, set_loader(fsx), "T.glb", "T.b3track.json", {}, s2, m2, &rep, &why) && s2 == st && m2 == sm);
    // the section raised 2 m by its node (a modelling tool's object move): the file's strips, moved; the same sizes
    Json doc;
    FileSet ed = fsx;
    CHECK(json_parse(str(ed["T.gltf"]), doc));
    for (Json& n : doc.get("nodes")->a)
        if (n.get("name") && n.get("name")->s == "section_000") { const float t[3] = {0, 2, 0}; n.set("translation", Json::floats(t, 3)); }
    std::string t2 = doc.dump();
    ed["T.gltf"] = Buf(t2.begin(), t2.end());
    CHECK(track_import(st, sm, set_loader(ed), "T.gltf", "T.b3track.json", {}, s2, m2, &rep, &why) && rep.edited == 2 && s2 == st && m2.size() == sm.size());
    TrackFiles t3;
    CHECK(track_files_read(s2, m2, t3, &why) && t3.sites.size() == 3);
    TrackMesh a0, b0;
    CHECK(track_mesh_read(sm, tf.sites[0].chain1, tf.sites[0].chain2, a0) && track_mesh_read(m2, t3.sites[0].chain1, t3.sites[0].chain2, b0));
    CHECK(b0.v.size() == a0.v.size() && b0.v[7].pos[1] == a0.v[7].pos[1] + 2.0f && b0.v[7].pos[0] == a0.v[7].pos[0] && b0.tri == a0.tri);
    // the colours painted in a modelling tool (COLOR_0 only): the stored bytes follow (the shown colour / 4)
    ed = fsx;
    CHECK(json_parse(str(ed["T.gltf"]), doc));
    for (const Json& mj : doc.get("meshes")->a)
        if (mj.get("name")->s == "section_000_lod") {
            const int ca = int(mj.get("primitives")->a[0].get("attributes")->get("COLOR_0")->as_int());
            const Json& acc = doc.get("accessors")->a[std::size_t(ca)];
            const std::size_t at = std::size_t(doc.get("bufferViews")->a[std::size_t(acc.get("bufferView")->as_int())].get("byteOffset")->as_int());
            for (std::size_t i = 0; i < std::size_t(acc.get("count")->as_int()); ++i) ed["T.bin"][at + 4 * i + 1] = 200;   // green 200 / 255
        }
    CHECK(track_import(st, sm, set_loader(ed), "T.gltf", "T.b3track.json", {}, s2, m2, &rep, &why) && rep.edited == 1 && m2.size() == sm.size());
    CHECK(track_files_read(s2, m2, t3, &why) && track_mesh_read(m2, t3.sites[2].chain1, t3.sites[2].chain2, b0));
    CHECK(track_mesh_read(sm, tf.sites[2].chain1, tf.sites[2].chain2, a0));
    CHECK(b0.v[3].col[1] == 50 && b0.v[3].col[0] == a0.v[3].col[0] && b0.v[3].col[2] == a0.v[3].col[2] && b0.v[3].col[3] == a0.v[3].col[3]);
    // new triangles: a large grid for the high block's first mesh -- its packets move to the block's free end, the
    // block grows by 32 KB (STREAMED.DAT and the unit table follow); the renderer's reader takes the files
    std::vector<TrackImportMesh> ms;
    std::vector<TrackTextureEdit> sw;
    Json side;
    CHECK(json_parse(str(fsx["T.b3track.json"]), side));
    CHECK(track_model_read(set_loader(fsx), "T.gltf", side, st, sm, ms, sw, nullptr, &why) && ms.size() == 3 && sw.empty());
    TrackImportMesh* big = nullptr;
    for (TrackImportMesh& m : ms) if (!m.in_static && m.chain1 == tf.sites[0].chain1) big = &m;
    CHECK(big != nullptr);
    if (!big) return;
    track_grid(60, 18, big->v, big->tri, 0.5f);
    CHECK(track_import_meshes(st, sm, ms, sw, {}, {}, s2, m2, &rep, &why) && rep.restripped == 1 && rep.blocks_grown == 1 && rep.moved >= 1);
    CHECK(m2.size() == sm.size() + 0x8000 && rd_u32(s2, 0x88) == 0x10000 && rd_u32(s2, 0x84) == 0x10000 && rd_u32(m2, 8) == 0x10000);
    CHECK(track_files_read(s2, m2, t3, &why) && t3.sites.size() == 3 && track_mesh_read(m2, t3.sites[0].chain1, t3.sites[0].chain2, b0));
    CHECK(mesh_set(b0) == track_mesh_hash(big->v, big->tri, b0.layout));
    CHECK(track_mesh_read(m2, t3.sites[2].chain1, t3.sites[2].chain2, b0) && track_mesh_read(sm, tf.sites[2].chain1, tf.sites[2].chain2, a0) && mesh_set(b0) == mesh_set(a0));
    // over the slot (160 KB for a high block): refused
    track_grid(250, 40, big->v, big->tri, 0.5f);
    CHECK(!track_import_meshes(st, sm, ms, sw, {}, {}, s2, m2, &rep, &why) && why.find("slot") != std::string::npos);
    // refusals: from another file, a mesh that is not the route's, a section's material changed, UVs off the range
    Buf other = st;
    other[0x5C] ^= 1;
    CHECK(!track_import(other, sm, set_loader(fsx), "T.gltf", "T.b3track.json", {}, s2, m2, &rep, &why) && why.find("another") != std::string::npos);
    ed = fsx;
    CHECK(json_parse(str(ed["T.gltf"]), doc));
    for (Json& mj : doc.get("meshes")->a) {
        mj.set("name", Json::str("Cube"));
        for (Json& p : mj.get("primitives")->a) p.set("extras", Json::object());
    }
    for (Json& n : doc.get("nodes")->a) if (n.get("mesh")) n.set("name", Json::str("Cube"));
    t2 = doc.dump();
    ed["T.gltf"] = Buf(t2.begin(), t2.end());
    CHECK(!track_import(st, sm, set_loader(ed), "T.gltf", "T.b3track.json", {}, s2, m2, &rep, &why) && why.find("not one of the route's meshes") != std::string::npos);
    ed = fsx;
    CHECK(json_parse(str(ed["T.gltf"]), doc));
    for (Json& mj : doc.get("meshes")->a)
        if (mj.get("name")->s == "section_000_lod") mj.get("primitives")->a[0].set("material", doc.get("meshes")->a[0].get("primitives")->a[1].get("material") ? *doc.get("meshes")->a[0].get("primitives")->a[1].get("material") : Json::integer(1));
    t2 = doc.dump();
    ed["T.gltf"] = Buf(t2.begin(), t2.end());
    CHECK(!track_import(st, sm, set_loader(ed), "T.gltf", "T.b3track.json", {}, s2, m2, &rep, &why) && why.find("cannot change its material") != std::string::npos);
    ms.clear();
    CHECK(track_model_read(set_loader(fsx), "T.gltf", side, st, sm, ms, sw, nullptr, &why));
    for (TrackImportMesh& m : ms) m.v[0].pos[1] = std::nanf("");
    CHECK(!track_import_meshes(st, sm, ms, sw, {}, {}, s2, m2, &rep, &why) && why.find("position") != std::string::npos);
}

TEST_MAIN("test_modtool")
