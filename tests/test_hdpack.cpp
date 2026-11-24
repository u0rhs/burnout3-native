// HD texture packs (b3asset/hdpack.h, docs/native-renderer.md §18): XXH3-64, PCSX2 replacement keys, names, DDS and
// the replacement hook on synthetic data only (no pack or game data).
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "b3asset/bc.h"
#include "b3asset/gs_memory.h"
#include "b3asset/hdcache.h"
#include "b3asset/hdpack.h"
#include "b3asset/png.h"
#include "b3asset/raster.h"
#include "test.h"

using namespace b3asset;

namespace {

std::vector<std::uint8_t> lcg_bytes(std::size_t n) {
    std::vector<std::uint8_t> b;
    std::uint32_t x = 12345;
    for (std::size_t i = 0; i < n; ++i) {
        x = x * 1103515245u + 12345u;
        b.push_back(std::uint8_t(x >> 16));
    }
    return b;
}

// CSM1 palette position of logical entry i (index bits 3 and 4 swapped; 16 per row).
void write_clut(GsMemory& gs, std::uint32_t cbp, const std::vector<std::uint32_t>& pal) {
    for (std::uint32_t i = 0; i < pal.size(); ++i) {
        const std::uint32_t j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
        gs.write(PSMCT32, cbp, 1, j & 15, j >> 4, pal[i]);
    }
}

RasterHeader header(int psm, int tw, int th, std::uint32_t tbp, std::uint32_t tbw, std::uint32_t cbp, std::uint32_t w, std::uint32_t h) {
    RasterHeader r;
    r.width = w;
    r.height = h;
    r.tex0 = std::uint64_t(tbp) | std::uint64_t(tbw) << 14 | std::uint64_t(psm) << 20 | std::uint64_t(tw) << 26 |
             std::uint64_t(th) << 30 | 1ull << 34;   // TCC 1, CPSM CT32, CSM1, CSA 0
    r.pal_offset = cbp;
    r.levels = 1;
    return r;
}

std::vector<std::uint32_t> palette(int n) {
    std::vector<std::uint32_t> p;
    for (int i = 0; i < n; ++i) p.push_back(std::uint32_t(i * 0x010203) | std::uint32_t(i & 1 ? 0x40 : 0x80) << 24);
    return p;
}

void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) { std::memcpy(&d[at], &v, 4); }

std::vector<std::uint8_t> dds_header(std::uint32_t w, std::uint32_t h, const char* fourcc) {
    std::vector<std::uint8_t> d(128, 0);
    std::memcpy(&d[0], "DDS ", 4);
    put32(d, 4, 124);
    put32(d, 12, h);
    put32(d, 16, w);
    put32(d, 76, 32);
    if (fourcc) {
        put32(d, 80, 4);
        std::memcpy(&d[84], fourcc, 4);
    } else {
        put32(d, 80, 0x41);   // RGB + alpha
        put32(d, 88, 32);
        put32(d, 92, 0x000000FF);
        put32(d, 96, 0x0000FF00);
        put32(d, 100, 0x00FF0000);
        put32(d, 104, 0xFF000000);
    }
    return d;
}

}  // namespace

TEST(xxh3_matches_reference_vectors) {
    // Expected values from the reference implementation (python xxhash 4.0.2 xxh3_64_intdigest) on the same bytes.
    const struct { std::size_t n; std::uint64_t h; } v[] = {
        {0, 0x2d06800538d394c2ull},    {1, 0xb95d064157e4a8b4ull},    {3, 0x4b2bf643aad9977cull},
        {4, 0x1a322462ca4049e4ull},    {8, 0x7b6cbf2b8364e41dull},    {9, 0x89efc25121f13282ull},
        {16, 0x65140316743838f8ull},   {17, 0xd15eb938f2b03cd0ull},   {128, 0xb9c0abe395dfa8a1ull},
        {129, 0x5ecbf4005614dab4ull},  {240, 0x084e5622e8f1d582ull},  {241, 0xe458863ec159b424ull},
        {1024, 0xc5eb3b0e7580130full}, {1025, 0x40846007943e297dull}, {3000, 0xf2303cf0ebb57fa8ull},
    };
    const auto b = lcg_bytes(3000);
    for (const auto& e : v) CHECK(xxh3_64(b.data(), e.n) == e.h);
}

TEST(replacement_names_parse_like_pcsx2) {
    HdKey k;
    CHECK(parse_hd_name("10099261ff437fb1-b448ca22fc2717b-00005994.dds", k));
    CHECK(k.tex == 0x10099261ff437fb1ull && k.clut == 0x0b448ca22fc2717bull);
    CHECK_BITS(k.bits, 0x1994u);   // bit 14 (the old TCC copy) dropped as PCSX2's RemoveUnusedBits does
    CHECK(hd_key_name(k) == "10099261ff437fb1-b448ca22fc2717b-00001994");
    CHECK(parse_hd_name("abc-00000000.png", k) && k.clut == 0 && k.tex == 0xabc);
    CHECK(!parse_hd_name("10099261ff437fb1-r64x32-00005994.dds", k));   // region names: not handled
    CHECK(!parse_hd_name("UI", k));
    CHECK(!parse_hd_name("1-2-3-4.dds", k));
    CHECK(!parse_hd_name("10099261ff437fb100-1-1.dds", k));             // more than 16 digits
}

TEST(t8_key_hashes_raw_blocks_and_logical_clut) {
    // PSMT8 32x32 = 2x2 blocks of 16x16; blocks 0..3 of a page are (0,0) (1,0) (0,1) (1,1) for 32-bit / 8-bit
    // formats (GS manual block layout), so the hash input is the 1024 bytes from TBP on in that order.
    GsMemory gs;
    const auto pal = palette(256);
    for (std::uint32_t base : {64u, 640u}) {
        gs.clear();
        for (std::uint32_t y = 0; y < 32; ++y)
            for (std::uint32_t x = 0; x < 32; ++x) gs.write(PSMT8, base, 2, x, y, (x * 7 + y * 13) & 0xFF);
        write_clut(gs, base + 32, pal);
        const RasterHeader h = header(PSMT8, 5, 5, base, 2, base + 32, 32, 32);
        HdKey k;
        CHECK(hd_key(gs, h, 0, 1, k));
        CHECK(k.tex == xxh3_64(gs.data() + std::size_t(base) * 256, 1024));
        CHECK(k.clut == xxh3_64(pal.data(), pal.size() * 4));
        CHECK_BITS(k.bits, 0x13u | 5u << 6 | 5u << 10);
        static HdKey first;
        if (base == 64) first = k;
        else CHECK(k == first);   // the key does not depend on where the texture sits in GS memory
    }
}

TEST(small_t4_key_hashes_expanded_indices) {
    // 8x8 is below PSMT4's 32x16 block: PCSX2 hashes one index byte per texel, row by row.
    GsMemory gs;
    std::vector<std::uint8_t> idx;
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 8; ++x) {
            const std::uint32_t v = (x + 3 * y) & 15;
            gs.write(PSMT4, 0, 2, x, y, v);
            idx.push_back(std::uint8_t(v));
        }
    const auto pal = palette(16);
    write_clut(gs, 100, pal);
    HdKey k;
    CHECK(hd_key(gs, header(PSMT4, 3, 3, 0, 2, 100, 8, 8), 0, 1, k));
    CHECK(k.tex == xxh3_64(idx.data(), idx.size()));
    CHECK(k.clut == xxh3_64(pal.data(), 64));
    CHECK_BITS(k.bits, 0x14u | 3u << 6 | 3u << 10);
}

TEST(mip_level_candidates_use_miptbp_and_named_size) {
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 32; ++x) gs.write(PSMT8, 0, 2, x, y, (x ^ y) & 0xFF);
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 0; x < 16; ++x) gs.write(PSMT8, 8, 2, x, y, (x + y) & 0xFF);   // level 1 (one block)
    write_clut(gs, 40, palette(256));
    RasterHeader h = header(PSMT8, 5, 5, 0, 2, 40, 32, 32);
    h.levels = 2;
    h.miptbp1 = 8 | 2ull << 14;
    const auto c = hd_candidate_keys(gs, h, 0);
    CHECK(c.size() == 3 + 1);   // named 32: [0], [0,1], [1]; named 16: [1]
    CHECK(c[0].level == 0 && c[0].first == 0 && c[0].count == 1);
    bool found = false;
    for (const auto& k : c)
        if (k.level == 0 && k.first == 1 && k.count == 1) {
            found = true;
            CHECK(k.key.tex == xxh3_64(gs.data() + 8 * 256, 256));
            CHECK_BITS(k.key.bits, 0x13u | 5u << 6 | 5u << 10);   // level 1's bytes under level 0's size bits
        }
    CHECK(found);
    CHECK(c.back().level == 1 && c.back().first == 1);
    CHECK_BITS(c.back().key.bits, 0x13u | 4u << 6 | 4u << 10);
}

TEST(dds_rgba_and_bc3_decode_with_ps2_alpha) {
    auto d = dds_header(2, 1, nullptr);
    const std::uint8_t px[8] = {10, 20, 30, 0x80, 40, 50, 60, 0x40};
    d.insert(d.end(), px, px + 8);
    Image im;
    CHECK(decode_dds(d.data(), d.size(), im));
    CHECK(im.w == 2 && im.h == 1);
    CHECK(im.rgba[0] == 10 && im.rgba[2] == 30 && im.rgba[3] == 0xFF && im.rgba[7] == 0x80);
    auto b = dds_header(4, 4, "DXT5");
    const std::uint8_t blk[16] = {0x80, 0x00, 0, 0, 0, 0, 0, 0,   // alpha: endpoint 0 everywhere
                                  0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};   // colour: c0 red 565, index 0
    b.insert(b.end(), blk, blk + 16);
    CHECK(decode_dds(b.data(), b.size(), im));
    CHECK(im.w == 4 && im.h == 4);
    CHECK(im.rgba[0] == 255 && im.rgba[1] == 0 && im.rgba[2] == 0 && im.rgba[3] == 255);
    CHECK(im.rgba[15 * 4 + 3] == 255);
    b.resize(b.size() - 1);
    CHECK(!decode_dds(b.data(), b.size(), im));   // truncated data is refused
    CHECK(!decode_dds(b.data(), 100, im));
}

TEST(hook_replaces_with_the_raster_extent_of_the_pack_image) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "b3_test_hdpack";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    // a 48x32 raster in a 64x32 TEX0 region; the replacement covers 64x32 at 4x
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 3 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster r;
    r.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    r.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});   // opaque: covered everywhere
    r.max_alpha = 0x80;
    HdKey k;
    CHECK(hd_key(gs, r.hdr, 0, 1, k));
    auto d = dds_header(256, 128, nullptr);
    for (std::uint32_t y = 0; y < 128; ++y)
        for (std::uint32_t x = 0; x < 256; ++x) {
            const std::uint8_t p[4] = {std::uint8_t(x), std::uint8_t(y), 9, 0x40};
            d.insert(d.end(), p, p + 4);
        }
    std::ofstream(dir / "sub" / (hd_key_name(k) + ".dds"), std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    std::ofstream(dir / "readme.txt") << "not a replacement";
    HdTextures hd;
    CHECK(hd.open(dir.string()));
    CHECK(hd.pack.files.size() == 1 && hd.pack.skipped == 1);
    CHECK(hd.apply(gs, r, 0));
    CHECK(r.hd && r.levels.size() == 1);
    CHECK(r.original.empty());   // hdkey: no originals_from (a viewer's pack): the decode is not kept
    CHECK(r.levels[0].w == 192 && r.levels[0].h == 128);
    const std::uint8_t* p = &r.levels[0].rgba[(5 * 192 + 191) * 4];
    CHECK(p[0] == 191 && p[1] == 5 && p[2] == 9 && p[3] == 0x80);   // DDS alpha 0x40 = half: 0x80
    // the original's alpha bounds the replacement's: 0 where the original's texel is 0, at most its maximum
    DecodedRaster lim;
    lim.hdr = r.hdr;
    lim.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0)});
    for (int y = 0; y < 32; ++y)
        for (int x = 24; x < 48; ++x) lim.levels[0].rgba[(std::size_t(y) * 48 + x) * 4 + 3] = 0x20;
    lim.max_alpha = 0x10;   // raw PS2 alpha: 0x20 once mapped; the right half is the coverage
    auto d2 = d;
    for (std::size_t i = 128 + 3; i < d2.size(); i += 4) d2[i] = 0x80;
    std::ofstream(dir / "sub" / (hd_key_name(k) + ".dds"), std::ios::binary).write(reinterpret_cast<const char*>(d2.data()), std::streamsize(d2.size()));
    HdTextures hd2;
    CHECK(hd2.open(dir.string()));
    CHECK(hd2.apply(gs, lim, 0));
    // the cut takes only transparent texels: the transparent half alpha 0; the faint (0x20) half keeps its alpha (low but
    // not transparent: a blended overlay such as US/C2_V2's haze), held to the original's maximum
    CHECK(lim.levels[0].rgba[(5 * 192 + 10) * 4 + 3] == 0);
    CHECK(lim.levels[0].rgba[(5 * 192 + 150) * 4 + 3] == 0x20);
    CHECK(lim.levels[0].rgba[(5 * 192 + 94) * 4 + 3] == 0);      // the last transparent texel, beside the faint ones
    for (int y = 0; y < 32; ++y)
        for (int x = 24; x < 48; ++x) lim.levels[0].rgba[(std::size_t(y) * 48 + x) * 4 + 3] = 0xFF;   // opaque right half
    lim.hd = false;
    lim.levels.resize(1);
    lim.levels[0].w = 48;
    lim.levels[0].h = 32;
    lim.levels[0].rgba.assign(48 * 32 * 4, 0);
    for (int y = 0; y < 32; ++y)
        for (int x = 24; x < 48; ++x) lim.levels[0].rgba[(std::size_t(y) * 48 + x) * 4 + 3] = 0xFF;
    CHECK(hd2.apply(gs, lim, 0));
    CHECK(lim.levels[0].rgba[(5 * 192 + 10) * 4 + 3] == 0);       // over the original's transparent half
    CHECK(lim.levels[0].rgba[(5 * 192 + 150) * 4 + 3] == 0x20);   // covered: clamped to the original's maximum
    // the outline: over the last transparent texel (23) the cut follows the bilinear contour at 1/4 (HD 94: 0.125 of the
    // opaque neighbour, cut; HD 95: 0.375, kept), so the pack's soft outline stays and no texel squares appear
    CHECK(lim.levels[0].rgba[(5 * 192 + 93) * 4 + 3] == 0);
    CHECK(lim.levels[0].rgba[(5 * 192 + 94) * 4 + 3] == 0);
    CHECK(lim.levels[0].rgba[(5 * 192 + 95) * 4 + 3] == 0x20);
    DecodedRaster other = r;
    other.hd = false;
    CHECK(!hd.apply(gs, other, 4));   // another CLUT: no key in the pack
    fs::remove_all(dir);
}

// patches3: the mods' [textures] packs and the player's pack as one, in priority order (b3mod Overlay::texture_packs)
TEST(packs_merge_in_priority_order) {
    namespace fs = std::filesystem;
    const fs::path root = fs::path("build") / "test_hdpack.scratch";
    fs::remove_all(root);
    const fs::path a = root / "mod-a", b = root / "mod-b", user = root / "user";
    const HdKey k1{0x1111, 0xA, 0x2213}, k2{0x2222, 0xB, 0x2213}, k3{0x3333, 0, 0x2200};
    auto put = [](const fs::path& dir, const HdKey& k) {
        fs::create_directories(dir);
        std::ofstream(dir / (hd_key_name(k) + ".dds"), std::ios::binary) << "x";
        return (dir / (hd_key_name(k) + ".dds")).string();
    };
    const std::string a1 = put(a, k1), b2 = put(b / "UI", k2), u1 = put(user, k1), u2 = put(user, k2), u3 = put(user, k3);
    HdPack p;
    CHECK(p.open(std::vector<std::string>{a.string(), b.string(), user.string()}));
    CHECK(p.dirs.size() == 3 && p.dir == a.string() && p.files.size() == 3);
    CHECK(p.find(k1) && *p.find(k1) == a1);   // the first mod's wins over the player's
    CHECK(p.find(k2) && *p.find(k2) == b2);   // the second mod's too
    CHECK(p.find(k3) && *p.find(k3) == u3);   // what no mod replaces: the player's
    CHECK(p.open(std::vector<std::string>{user.string(), a.string()}) && *p.find(k1) == u1 && *p.find(k2) == u2);
    std::string why;
    CHECK(p.open(std::vector<std::string>{a.string(), (root / "gone").string(), user.string()}, &why));
    CHECK(p.dirs.size() == 2 && why.find("not a directory") != std::string::npos && *p.find(k1) == a1);
    CHECK(!p.open(std::vector<std::string>{(root / "gone").string()}));
    // the renderer's switch: the mods' packs before the player's; --no-hd leaves the mods' on
    HdTextures* t = hd_enable(user.string(), false, {a.string(), b.string()});
    CHECK(t && hd_textures() == t && *t->pack.find(k1) == a1 && *t->pack.find(k3) == u3);
    CHECK(t->originals_from == 2 && hd_switchable());   // hdkey: the player's pack keeps its originals
    t = hd_enable(user.string(), true, {a.string()});
    CHECK(t && t->pack.find(k1) && !t->pack.find(k3));
    CHECK(!hd_switchable());   // the mods' packs alone: nothing for the HD TEXTURES key
    CHECK(!hd_enable(user.string(), true, {}) && !hd_textures());
    // showhd1: the list hd_enable opens (the launcher's showroom opens the same one)
    CHECK(hd_folders(user.string(), false, {a.string(), b.string()}) == (std::vector<std::string>{a.string(), b.string(), user.string()}));
    CHECK(hd_folders(user.string(), true, {a.string()}) == std::vector<std::string>{a.string()});
    CHECK(hd_folders(user.string(), true, {}).empty());
    set_hd_textures(nullptr);
    fs::remove_all(root);
}

// hdkey (the HD TEXTURES key): a replacement from the player's pack keeps the decode it replaced; a mod's does not
TEST(player_pack_hits_keep_the_original) {
    namespace fs = std::filesystem;
    const fs::path root = fs::path("build") / "test_hdpack_originals.scratch";
    fs::remove_all(root);
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 5 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster base;
    base.hdr = header(PSMT8, 6, 5, 0, 2, 64, 64, 32);
    base.levels.push_back(Image{64, 32, std::vector<std::uint8_t>(64 * 32 * 4, 0x7F)});
    base.levels.push_back(Image{32, 16, std::vector<std::uint8_t>(32 * 16 * 4, 0x3F)});
    base.max_alpha = 0x80;
    HdKey k;
    CHECK(hd_key(gs, base.hdr, 0, 1, k));
    auto d = dds_header(128, 64, nullptr);
    d.resize(d.size() + 128 * 64 * 4, 0x80);
    const auto put = [&](const fs::path& dir) {
        fs::create_directories(dir);
        std::ofstream(dir / (hd_key_name(k) + ".dds"), std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    };
    // a mod folder whose name the player's folder starts with: told apart by the separator
    const fs::path mod = root / "pack", user = root / "pack2";
    put(user);
    HdTextures* t = hd_enable(user.string(), false, {(root / "gone").string()});   // a mod folder that is not there
    CHECK(t && t->pack.dirs.size() == 1 && t->originals_from == 0 && hd_switchable());
    fs::create_directories(mod);
    t = hd_enable(user.string(), false, {mod.string()});
    CHECK(t && t->originals_from == 1 && hd_switchable());
    DecodedRaster r = base;
    CHECK(t->apply(gs, r, 0) && r.hd && r.levels.size() == 1 && r.levels[0].w == 128);
    CHECK(r.original.size() == 2 && r.original[0].w == 64 && r.original[1].w == 32 && r.original[0].rgba == base.levels[0].rgba);
    CHECK(t->originals == 1);
    put(mod);   // now the mod's pack has it (and wins): part of the mod, no original kept
    t = hd_enable(user.string(), false, {mod.string()});
    DecodedRaster m = base;
    CHECK(t && t->apply(gs, m, 0) && m.hd && m.original.empty() && t->originals == 0);
    set_hd_textures(nullptr);
    fs::remove_all(root);
}

// ---- hdgpu: the BC path (bc.h; HdTextures with set_hd_compressed) ----

namespace {
// A smooth test picture with a soft alpha ramp (w x h RGBA8)
Image picture(int w, int h, bool alpha) {
    Image im;
    im.w = w;
    im.h = h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint8_t p[4] = {std::uint8_t(x * 255 / std::max(w - 1, 1)), std::uint8_t(y * 255 / std::max(h - 1, 1)),
                                       std::uint8_t((x + y) * 255 / std::max(w + h - 2, 1)), std::uint8_t(alpha ? (x * 255 / std::max(w - 1, 1)) : 255)};
            im.rgba.insert(im.rgba.end(), p, p + 4);
        }
    return im;
}
int max_diff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int channel = -1) {
    int m = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (channel < 0 || int(i % 4) == channel) m = std::max(m, std::abs(int(a[i]) - int(b[i])));
    return a.size() == b.size() ? m : 999;
}
Image decoded(const Image& l) {
    Image o;
    o.w = l.w;
    o.h = l.h;
    o.rgba.resize(std::size_t(l.w) * l.h * 4);
    bc_decode(l.bc, l.blocks->data(), l.w, l.h, o.rgba.data());
    return o;
}
// A DDS file of `fmt` blocks for `im` (PS2 alpha: the picture's alpha halved), with `mips` stored levels
std::vector<std::uint8_t> bc_dds(const Image& im, int fmt, int mips = 1) {
    auto d = dds_header(std::uint32_t(im.w), std::uint32_t(im.h), fmt == kBC1 ? "DXT1" : fmt == kBC2 ? "DXT3" : "DXT5");
    if (mips > 1) {
        put32(d, 8, 0x20000 | 0x1007);
        put32(d, 28, std::uint32_t(mips));
    }
    Image l = im;
    for (int k = 0; k < mips; ++k) {
        Image half = l;
        for (std::size_t i = 3; i < half.rgba.size(); i += 4) half.rgba[i] = std::uint8_t(half.rgba[i] / 2 + (half.rgba[i] == 255));
        std::vector<std::uint8_t> b;
        bc_encode(fmt, half.rgba.data(), half.w, half.h, b);
        d.insert(d.end(), b.begin(), b.end());
        Image n;
        mip_down(l, n);
        l = std::move(n);
    }
    return d;
}
}  // namespace

TEST(bc_encode_decode_roundtrip) {
    for (int fmt : {kBC1, kBC2, kBC3}) {
        const Image im = picture(64, 32, fmt != kBC1);
        std::vector<std::uint8_t> b, b8;
        bc_encode(fmt, im.rgba.data(), im.w, im.h, b);
        bc_encode(fmt, im.rgba.data(), im.w, im.h, b8, 8);   // split by rows: the same blocks
        CHECK(b.size() == bc_level_bytes(fmt, 64, 32) && b == b8);
        Image out;
        out.w = 64;
        out.h = 32;
        out.rgba.resize(64 * 32 * 4);
        bc_decode(fmt, b.data(), 64, 32, out.rgba.data());
        for (int c = 0; c < 3; ++c) CHECK(max_diff(im.rgba, out.rgba, c) <= 12);   // R across, G down: each block a plane, not a line
        CHECK(max_diff(im.rgba, out.rgba, 3) <= (fmt == kBC2 ? 8 : fmt == kBC3 ? 4 : 0));
    }
    {   // colours on a line (a grey ramp): within the 565 step and the thirds
        Image g;
        g.w = 64;
        g.h = 4;
        for (int i = 0; i < 64 * 4; ++i) { const std::uint8_t v = std::uint8_t((i % 64) * 4); const std::uint8_t p[4] = {v, v, v, 255}; g.rgba.insert(g.rgba.end(), p, p + 4); }
        std::vector<std::uint8_t> b;
        bc_encode(kBC1, g.rgba.data(), 64, 4, b);
        Image out;
        out.w = 64;
        out.h = 4;
        out.rgba.resize(64 * 4 * 4);
        bc_decode(kBC1, b.data(), 64, 4, out.rgba.data());
        CHECK(max_diff(g.rgba, out.rgba) <= 4);
    }
    // one colour: exact up to the 565 rounding; BC1 punch-through: transparent texels on index 3
    std::uint8_t px[16][4], blk[8], back[16][4];
    for (auto& p : px) { p[0] = 200; p[1] = 100; p[2] = 50; p[3] = 255; }
    px[5][3] = 0;
    bc_encode_color(px, blk, true);
    bc_decode_block(kBC1, blk, back);
    CHECK(back[5][3] == 0 && back[0][3] == 255 && std::abs(back[0][0] - 200) <= 4 && std::abs(back[0][1] - 100) <= 2);
    // alpha only 0 and 255: exact (the 6-value mode holds both)
    std::uint8_t a[16], ab[16];
    for (int i = 0; i < 16; ++i) a[i] = i & 1 ? 255 : 0;
    std::uint8_t full[16] = {};
    bc_encode_alpha(a, full);
    std::memset(full + 8, 0, 8);
    bc_decode_block(kBC3, full, back);
    for (int i = 0; i < 16; ++i) ab[i] = back[i][3];
    CHECK(std::memcmp(a, ab, 16) == 0);
}

TEST(mip_down_matches_the_gpu_blit) {
    Image a;
    a.w = 4;
    a.h = 2;
    for (int i = 0; i < 8; ++i) { const std::uint8_t p[4] = {std::uint8_t(i * 10), 0, 255, std::uint8_t(i)}; a.rgba.insert(a.rgba.end(), p, p + 4); }
    Image b;
    mip_down(a, b);
    CHECK(b.w == 2 && b.h == 1);
    CHECK(b.rgba[0] == (0 + 10 + 40 + 50 + 2) / 4 && b.rgba[2] == 255 && b.rgba[4] == (20 + 30 + 60 + 70 + 2) / 4);
    Image c = picture(5, 3, true), d;   // odd sizes: bilinear at the texel centres, clamped at the edge
    mip_down(c, d);
    CHECK(d.w == 2 && d.h == 1 && d.rgba.size() == 8);
    Image e = picture(1, 8, false), f;
    mip_down(e, f);
    CHECK(f.w == 1 && f.h == 4);
}

TEST(dds_header_levels_and_bc1_blocks) {
    const Image im = picture(64, 32, true);
    const auto d1 = bc_dds(picture(64, 32, false), kBC1);
    DdsInfo info;
    CHECK(parse_dds(d1.data(), d1.size(), info) && info.fmt == kBC1 && info.levels == 1 && info.at == 128 && info.w == 64);
    Image ref;
    CHECK(decode_dds(d1.data(), d1.size(), ref));
    Image l;
    l.w = 64;
    l.h = 32;
    l.bc = kBC1;
    l.blocks = std::make_shared<std::vector<std::uint8_t>>(d1.begin() + 128, d1.end());
    CHECK(decoded(l).rgba == ref.rgba);   // opaque BC1: the blocks decode to decode_dds's texels
    // stored mips: only the whole ones count
    auto d = bc_dds(im, kBC3, 4);
    CHECK(parse_dds(d.data(), d.size(), info) && info.levels == 4);
    d.resize(d.size() - 1);
    CHECK(parse_dds(d.data(), d.size(), info) && info.levels == 3);
}

namespace {
struct CompressedOn {
    CompressedOn() { set_hd_compressed(true); }
    ~CompressedOn() { set_hd_compressed(false); }
};
// A pack folder with one replacement for the 48x32 T8 raster of `gs` (TEX0 64x32: the replacement covers 64x32 at 4x)
std::filesystem::path one_file_pack(const char* name, const GsMemory& gs, const DecodedRaster& r, const std::vector<std::uint8_t>& dds) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("build") / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    HdKey k;
    if (!hd_key(gs, r.hdr, 0, 1, k)) return {};
    std::ofstream(dir / (hd_key_name(k) + ".dds"), std::ios::binary).write(reinterpret_cast<const char*>(dds.data()), std::streamsize(dds.size()));
    return dir;
}
}  // namespace

TEST(hook_keeps_bc_replacements_compressed) {
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 3 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster base;
    base.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    base.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});   // opaque: the rule changes nothing
    base.max_alpha = 0x80;
    const Image pic = picture(256, 128, true);
    for (int fmt : {kBC1, kBC2, kBC3}) {
        const Image src = fmt == kBC1 ? picture(256, 128, false) : pic;
        const fs::path dir = one_file_pack("test_hdpack.bc", gs, base, bc_dds(src, fmt));
        Image ref;   // the RGBA8 path's picture: the file decoded, cropped to 192 x 128
        {
            HdTextures hd;
            CHECK(hd.open(dir.string()));
            DecodedRaster r = base;
            CHECK(hd.apply(gs, r, 0));   // compression off: RGBA8 as before
            CHECK(r.levels.size() == 1 && r.levels[0].bc == 0 && r.levels[0].w == 192);
            ref = r.levels[0];
        }
        const CompressedOn on;
        {
            HdTextures hd;
            CHECK(hd.open(dir.string()));
            DecodedRaster r = base;
            CHECK(hd.apply(gs, r, 0));   // no HdCompressScope: RGBA8 (the consumer may read texels)
            CHECK(r.levels.size() == 1 && r.levels[0].bc == 0 && hd.compressed == 0);
        }
        HdTextures hd;
        CHECK(hd.open(dir.string()));
        const HdCompressScope scope;
        DecodedRaster r = base;
        CHECK(hd.apply(gs, r, 0) && r.hd);
        CHECK(hd.compressed == 1 && hd.encoded == 0);
        CHECK(r.levels.size() == 8);   // 192x128 .. 1x1
        CHECK(r.levels[0].w == 192 && r.levels[0].h == 128 && r.levels.back().w == 1 && r.levels.back().h == 1);
        for (const Image& l : r.levels) CHECK(l.bc == fmt && l.rgba.empty() && l.blocks && l.blocks->size() == bc_level_bytes(fmt, l.w, l.h));
        const Image d0 = decoded(r.levels[0]);
        for (int c = 0; c < 3; ++c) CHECK(max_diff(d0.rgba, ref.rgba, c) == 0);   // the file's colour blocks
        CHECK(max_diff(d0.rgba, ref.rgba, 3) <= (fmt == kBC3 ? 2 : 0));       // BC1 / BC2 alpha exact, BC3 within the map
        Image m1;
        mip_down(ref, m1);
        CHECK(max_diff(decoded(r.levels[1]).rgba, m1.rgba) <= 16);   // the mips: the GPU's 2x2, BC encoded
        DecodedRaster again = base;   // the LOD model's decode: the same blocks, shared
        CHECK(hd.apply(gs, again, 0) && again.levels[0].blocks == r.levels[0].blocks);
        // TCC 0: opaque blocks
        DecodedRaster opaque = base;
        opaque.hdr.tex0 &= ~(1ull << 34);
        CHECK(hd.apply(gs, opaque, 0));
        CHECK(opaque.levels[0].bc == fmt);
        const Image o0 = decoded(opaque.levels[0]);
        int amin = 255;
        for (std::size_t i = 3; i < o0.rgba.size(); i += 4) amin = std::min(amin, int(o0.rgba[i]));
        CHECK(amin == 255);
        fs::remove_all(dir);
    }
}

TEST(hook_bc_alpha_rule_and_fallbacks) {
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 5 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster lim;   // the original transparent on its left half: the rule cuts the replacement there
    lim.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    lim.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0)});
    for (int y = 0; y < 32; ++y)
        for (int x = 24; x < 48; ++x) lim.levels[0].rgba[(std::size_t(y) * 48 + x) * 4 + 3] = 0xFF;
    lim.max_alpha = 0x80;
    Image opaque = picture(256, 128, false);
    const fs::path dir = one_file_pack("test_hdpack.bcrule", gs, lim, bc_dds(opaque, kBC3));
    Image ref;
    {
        HdTextures hd;
        CHECK(hd.open(dir.string()));
        DecodedRaster r = lim;
        CHECK(hd.apply(gs, r, 0) && hd.masked == 1);
        ref = r.levels[0];
    }
    const CompressedOn on;
    const HdCompressScope scope;
    {
        HdTextures hd;
        CHECK(hd.open(dir.string()));
        DecodedRaster r = lim;
        CHECK(hd.apply(gs, r, 0) && hd.masked == 1 && hd.compressed == 1 && hd.encoded == 1);
        CHECK(r.levels[0].bc == kBC3 && r.levels.size() == 8);
        const Image d0 = decoded(r.levels[0]);
        CHECK(d0.rgba[(5 * 192 + 10) * 4 + 3] == 0 && d0.rgba[(5 * 192 + 150) * 4 + 3] == 255);
        CHECK(max_diff(d0.rgba, ref.rgba, 3) <= 8);
        for (int c = 0; c < 3; ++c) CHECK(max_diff(d0.rgba, ref.rgba, c) == 0);   // the file's colour blocks, the alpha encoded again
        DecodedRaster again = lim;   // the LOD model: the rule's chain from the cache, still counted
        CHECK(hd.apply(gs, again, 0) && again.levels[0].blocks == r.levels[0].blocks && hd.masked == 2);
    }
    {   // the same file, a raster the rule leaves alone (its original opaque): the file's blocks, not re-encoded
        HdTextures hd;
        CHECK(hd.open(dir.string()));
        DecodedRaster r = lim;
        r.levels[0].rgba.assign(48 * 32 * 4, 0xFF);
        CHECK(hd.apply(gs, r, 0) && hd.encoded == 0 && hd.compressed == 1 && hd.masked == 0);
    }
    {   // an RGBA32 file: RGBA8
        auto d = dds_header(256, 128, nullptr);
        d.insert(d.end(), opaque.rgba.begin(), opaque.rgba.end());
        const fs::path d2 = one_file_pack("test_hdpack.bcrgba", gs, lim, d);
        HdTextures hd;
        CHECK(hd.open(d2.string()));
        DecodedRaster r = lim;
        CHECK(hd.apply(gs, r, 0) && r.levels.size() == 1 && r.levels[0].bc == 0 && hd.compressed == 0);
        fs::remove_all(d2);
    }
    {   // a crop that is not whole blocks (a 46-wide raster: 184 x 128 is, 45: 180 is; 43: 172; a 2x file of 43: 86): RGBA8
        DecodedRaster odd = lim;
        odd.hdr = header(PSMT8, 6, 5, 0, 2, 64, 43, 32);
        odd.levels[0] = Image{43, 32, std::vector<std::uint8_t>(43 * 32 * 4, 0xFF)};
        const fs::path d3 = one_file_pack("test_hdpack.bcodd", gs, odd, bc_dds(picture(128, 64, false), kBC1));
        HdTextures hd;
        CHECK(hd.open(d3.string()));
        CHECK(hd.apply(gs, odd, 0) && odd.levels.size() == 1 && odd.levels[0].bc == 0 && odd.levels[0].w == 86);
        fs::remove_all(d3);
    }
    {   // stored mips: used as they are (BC1, the whole file)
        DecodedRaster full = lim;
        full.hdr = header(PSMT8, 6, 5, 0, 2, 64, 64, 32);
        full.levels[0] = Image{64, 32, std::vector<std::uint8_t>(64 * 32 * 4, 0xFF)};
        const auto d = bc_dds(picture(256, 128, false), kBC1, 3);
        const fs::path d4 = one_file_pack("test_hdpack.bcmips", gs, full, d);
        HdTextures hd;
        CHECK(hd.open(d4.string()));
        CHECK(hd.apply(gs, full, 0) && full.levels.size() == 9 && full.levels[0].w == 256);
        const std::size_t l0 = bc_level_bytes(kBC1, 256, 128), l1 = bc_level_bytes(kBC1, 128, 64);
        CHECK(*full.levels[1].blocks == std::vector<std::uint8_t>(d.begin() + 128 + std::ptrdiff_t(l0), d.begin() + 128 + std::ptrdiff_t(l0 + l1)));
        fs::remove_all(d4);
    }
    {   // hdmips: stored mips that are black (zeroed BC1 blocks: a pack's DDS with empty levels) are not used: the walls
        // that turned black at the distance that mip is drawn. Levels 1 and 2 built from level 0 instead, as bright as it
        DecodedRaster full = lim;
        full.hdr = header(PSMT8, 6, 5, 0, 2, 64, 64, 32);
        full.levels[0] = Image{64, 32, std::vector<std::uint8_t>(64 * 32 * 4, 0xFF)};
        auto d = bc_dds(picture(256, 128, false), kBC1, 3);
        const std::size_t l0 = bc_level_bytes(kBC1, 256, 128);
        std::fill(d.begin() + 128 + std::ptrdiff_t(l0), d.end(), std::uint8_t(0));   // levels 1 and 2: black
        const fs::path d5 = one_file_pack("test_hdpack.bcdarkmips", gs, full, d);
        const std::size_t before = HdTextures::mips_rebuilt();
        HdTextures hd;
        CHECK(hd.open(d5.string()));
        CHECK(hd.apply(gs, full, 0) && full.levels.size() == 9 && full.levels[0].w == 256);
        CHECK(HdTextures::mips_rebuilt() == before + 1);
        const auto mean = [](const Image& im) {
            double s = 0;
            for (std::size_t i = 0; i < im.rgba.size(); i += 4) s += im.rgba[i] + im.rgba[i + 1] + im.rgba[i + 2];
            return s / double(im.rgba.size() / 4 * 3);
        };
        const double m0 = mean(decoded(full.levels[0])), m1 = mean(decoded(full.levels[1])), m2 = mean(decoded(full.levels[2]));
        CHECK(m0 > 60 && std::fabs(m1 - m0) < 12 && std::fabs(m2 - m0) < 12);
        fs::remove_all(d5);
    }
    fs::remove_all(dir);
}

// ---- texpack: PNG replacements (community packs ship PCSX2's PNG) ----
namespace {
// A PNG of any colour type / depth from its rows of samples as stored (filters cycled 0..4 per row, or Adam7 passes),
// deflate as stored blocks: enough to drive every branch of the reader
std::uint32_t t_crc(const std::uint8_t* p, std::size_t n, std::uint32_t c = 0) {
    c = ~c;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
void t_be32(std::vector<std::uint8_t>& o, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back(std::uint8_t(v >> s)); }
void t_chunk(std::vector<std::uint8_t>& o, const char* type, const std::vector<std::uint8_t>& d) {
    t_be32(o, std::uint32_t(d.size()));
    const std::size_t at = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), d.begin(), d.end());
    t_be32(o, t_crc(&o[at], o.size() - at));
}
int t_paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
// rows: each the packed samples of one row (no filter byte); filtered here, filter = row index % 5
void t_filter_rows(const std::vector<std::vector<std::uint8_t>>& rows, std::size_t bpp, std::vector<std::uint8_t>& out) {
    std::vector<std::uint8_t> prev;
    for (std::size_t y = 0; y < rows.size(); ++y) {
        const std::vector<std::uint8_t>& r = rows[y];
        if (prev.size() != r.size()) prev.assign(r.size(), 0);
        const int f = int(y % 5);
        out.push_back(std::uint8_t(f));
        for (std::size_t i = 0; i < r.size(); ++i) {
            const int a = i >= bpp ? r[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
            const int pr = f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : f == 4 ? t_paeth(a, b, c) : 0;
            out.push_back(std::uint8_t(r[i] - pr));
        }
        prev = r;
    }
}
std::vector<std::uint8_t> t_png(int w, int h, int depth, int ctype, bool interlace, const std::vector<std::uint8_t>& raw_idat,
                                const std::vector<std::uint8_t>& plte = {}, const std::vector<std::uint8_t>& trns = {}) {
    std::vector<std::uint8_t> o = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'}, ih;
    t_be32(ih, std::uint32_t(w));
    t_be32(ih, std::uint32_t(h));
    ih.insert(ih.end(), {std::uint8_t(depth), std::uint8_t(ctype), 0, 0, std::uint8_t(interlace ? 1 : 0)});
    t_chunk(o, "IHDR", ih);
    if (!plte.empty()) t_chunk(o, "PLTE", plte);
    if (!trns.empty()) t_chunk(o, "tRNS", trns);
    std::vector<std::uint8_t> z = {0x78, 0x01};   // zlib, stored blocks
    std::uint32_t s1 = 1, s2 = 0;
    for (std::uint8_t b : raw_idat) { s1 = (s1 + b) % 65521; s2 = (s2 + s1) % 65521; }
    for (std::size_t at = 0; at < raw_idat.size() || at == 0;) {
        const std::size_t n = std::min<std::size_t>(65535, raw_idat.size() - at);
        z.push_back(at + n >= raw_idat.size() ? 1 : 0);
        z.push_back(std::uint8_t(n)); z.push_back(std::uint8_t(n >> 8));
        z.push_back(std::uint8_t(~n)); z.push_back(std::uint8_t(~n >> 8));
        z.insert(z.end(), raw_idat.begin() + std::ptrdiff_t(at), raw_idat.begin() + std::ptrdiff_t(at + n));
        at += n;
        if (n == 0) break;
    }
    t_be32(z, s2 << 16 | s1);
    t_chunk(o, "IDAT", z);
    t_chunk(o, "IEND", {});
    return o;
}
// the reference picture: RGBA8, distinct per texel
std::uint8_t t_c(int x, int y, int k) { return std::uint8_t(k == 3 ? 40 + 7 * x + 3 * y : x * 37 + y * 11 + k * 85); }
}  // namespace

TEST(png_reader_every_colour_type_and_depth) {
    const int w = 13, h = 7;   // odd sizes: partial bytes for the low depths, short Adam7 passes
    auto check = [&](const char* what, const std::vector<std::uint8_t>& png, auto expect) {
        int ow = 0, oh = 0;
        std::vector<std::uint8_t> rgba;
        std::string why;
        const bool ok = decode_png(png.data(), png.size(), ow, oh, rgba, &why);
        CHECK(ok && ow == w && oh == h);
        if (!ok) { std::printf("  %s: %s\n", what, why.c_str()); return; }
        int bad = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int k = 0; k < 4; ++k) bad += rgba[(std::size_t(y) * w + x) * 4 + std::size_t(k)] != expect(x, y, k);
        if (bad) std::printf("  %s: %d samples differ\n", what, bad);
        CHECK(bad == 0);
    };
    auto rows_of = [&](int bpp_bytes, auto sample) {   // 8-bit samples per texel, packed
        std::vector<std::vector<std::uint8_t>> rows(h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int k = 0; k < bpp_bytes; ++k) rows[std::size_t(y)].push_back(sample(x, y, k));
        return rows;
    };
    std::vector<std::uint8_t> idat;
    // RGB8 (the rework pack's): opaque
    t_filter_rows(rows_of(3, [](int x, int y, int k) { return t_c(x, y, k); }), 3, idat);
    check("RGB8", t_png(w, h, 8, 2, false, idat), [](int x, int y, int k) { return k == 3 ? 255 : t_c(x, y, k); });
    // RGBA8
    idat.clear();
    t_filter_rows(rows_of(4, [](int x, int y, int k) { return t_c(x, y, k); }), 4, idat);
    const std::vector<std::uint8_t> rgba8 = t_png(w, h, 8, 6, false, idat);
    check("RGBA8", rgba8, [](int x, int y, int k) { return t_c(x, y, k); });
    // RGB16 (high bytes taken) with tRNS on one colour
    idat.clear();
    t_filter_rows(rows_of(6, [](int x, int y, int k) { return k % 2 ? std::uint8_t(0x5A) : t_c(x, y, k / 2); }), 6, idat);
    std::vector<std::uint8_t> trns16 = {t_c(2, 1, 0), 0x5A, t_c(2, 1, 1), 0x5A, t_c(2, 1, 2), 0x5A};
    check("RGB16+tRNS", t_png(w, h, 16, 2, false, idat, {}, trns16),
          [](int x, int y, int k) { return k == 3 ? (x == 2 && y == 1 ? 0 : 255) : t_c(x, y, k); });
    // grey + alpha 8
    idat.clear();
    t_filter_rows(rows_of(2, [](int x, int y, int k) { return t_c(x, y, k ? 3 : 0); }), 2, idat);
    check("GA8", t_png(w, h, 8, 4, false, idat), [](int x, int y, int k) { return t_c(x, y, k == 3 ? 3 : 0); });
    // grey 1-bit (scaled to 0 / 255)
    {
        std::vector<std::vector<std::uint8_t>> rows(h);
        for (int y = 0; y < h; ++y) {
            rows[std::size_t(y)].assign((w + 7) / 8, 0);
            for (int x = 0; x < w; ++x) if ((x + y) & 1) rows[std::size_t(y)][std::size_t(x / 8)] |= std::uint8_t(0x80 >> (x % 8));
        }
        idat.clear();
        t_filter_rows(rows, 1, idat);
        check("G1", t_png(w, h, 1, 0, false, idat), [](int x, int y, int k) { return k == 3 ? 255 : ((x + y) & 1 ? 255 : 0); });
    }
    // palette 4-bit with tRNS (entry 3 half transparent)
    {
        std::vector<std::uint8_t> plte, trns = {255, 255, 255, 128};
        for (int i = 0; i < 16; ++i) plte.insert(plte.end(), {std::uint8_t(i * 16), std::uint8_t(255 - i * 9), std::uint8_t(i * 5)});
        std::vector<std::vector<std::uint8_t>> rows(h);
        for (int y = 0; y < h; ++y) {
            rows[std::size_t(y)].assign((w + 1) / 2, 0);
            for (int x = 0; x < w; ++x) rows[std::size_t(y)][std::size_t(x / 2)] |= std::uint8_t(((x + 2 * y) % 16) << (x % 2 ? 0 : 4));
        }
        idat.clear();
        t_filter_rows(rows, 1, idat);
        check("P4+tRNS", t_png(w, h, 4, 3, false, idat, plte, trns), [&](int x, int y, int k) -> std::uint8_t {
            const int i = (x + 2 * y) % 16;
            return k == 3 ? (i < 4 ? trns[std::size_t(i)] : 255) : plte[std::size_t(i * 3 + k)];
        });
    }
    // Adam7 RGBA8: the seven passes, each filtered on its own
    {
        static const int px[7] = {0, 4, 0, 2, 0, 1, 0}, py[7] = {0, 0, 4, 0, 2, 0, 1}, dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
        idat.clear();
        for (int p = 0; p < 7; ++p) {
            std::vector<std::vector<std::uint8_t>> rows;
            for (int y = py[p]; y < h; y += dy[p]) {
                std::vector<std::uint8_t> r;
                for (int x = px[p]; x < w; x += dx[p])
                    for (int k = 0; k < 4; ++k) r.push_back(t_c(x, y, k));
                if (!r.empty()) rows.push_back(r);
            }
            t_filter_rows(rows, 4, idat);
        }
        check("RGBA8 Adam7", t_png(w, h, 8, 6, true, idat), [](int x, int y, int k) { return t_c(x, y, k); });
    }
    // damaged files: refused with a reason
    int ow, oh;
    std::vector<std::uint8_t> out;
    std::string why;
    std::vector<std::uint8_t> bad = rgba8;
    bad[bad.size() - 20] ^= 1;   // in the IDAT data: its CRC fails
    CHECK(!decode_png(bad.data(), bad.size(), ow, oh, out, &why) && why.find("CRC") != std::string::npos);
    CHECK(!decode_png(rgba8.data(), 40, ow, oh, out, &why) && why.find("cut off") != std::string::npos);
    // a tiny file claiming 16384 x 16384 (a few bytes of IDAT): refused before anything of that size is allocated
    {
        const std::vector<std::uint8_t> bomb = t_png(16384, 16384, 8, 6, false, std::vector<std::uint8_t>(100, 0));
        CHECK(bomb.size() < 400);
        CHECK(!decode_png(bomb.data(), bomb.size(), ow, oh, out, &why) && why.find("too short for its size") != std::string::npos);
        CHECK(out.empty());
    }
    // the writer's output reads back
    const std::vector<std::uint8_t> sevens(std::size_t(w) * h * 4, 7);
    const std::vector<std::uint8_t> mine = encode_png_rgba(w, h, sevens.data());
    CHECK(decode_png(mine.data(), mine.size(), ow, oh, out) && ow == w && out[0] == 7);
}

TEST(png_replacements_load_and_index) {
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 3 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster base;
    base.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    base.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});
    base.max_alpha = 0x80;
    HdKey k;
    CHECK(hd_key(gs, base.hdr, 0, 1, k));
    const std::string name = hd_key_name(k);
    // an RGB PNG at 4x (the rework pack's kind): opaque, its colours as they are
    std::vector<std::vector<std::uint8_t>> rows(128);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 256; ++x) rows[std::size_t(y)].insert(rows[std::size_t(y)].end(), {std::uint8_t(x), std::uint8_t(y), 9});
    std::vector<std::uint8_t> idat;
    t_filter_rows(rows, 3, idat);
    const std::vector<std::uint8_t> png = t_png(256, 128, 8, 2, false, idat);
    const fs::path root = fs::path("build") / "test_hdpack.png";
    fs::remove_all(root);
    fs::create_directories(root / "All Original Colors");
    fs::create_directories(root / "Custom" / "Original Colors");
    auto put = [](const fs::path& p, const std::vector<std::uint8_t>& d) {
        std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    };
    put(root / "All Original Colors" / (name + ".png"), png);
    // a colour variant of the same name, in a later folder: not used
    put(root / "Custom" / "Original Colors" / (name + ".png"), t_png(4, 4, 8, 2, false, std::vector<std::uint8_t>(4 * 13, 0)));
    put(root / "guide.pdf", {1, 2, 3});
    HdTextures hd;
    std::string why;
    CHECK(hd.open(std::vector<std::string>{root.string()}, &why));
    CHECK(hd.pack.files.size() == 1 && hd.pack.png_files == 1 && hd.pack.dds_files == 0 && hd.pack.skipped == 1);
    CHECK(hd.pack.repeats_ == 1 && why.find("found more than once") != std::string::npos);
    CHECK(*hd.pack.find(k) == (root / "All Original Colors" / (name + ".png")).string());   // path order: "All ..." first
    DecodedRaster r = base;
    CHECK(hd.apply(gs, r, 0) && r.hd);
    CHECK(r.levels.size() == 1 && r.levels[0].w == 192 && r.levels[0].h == 128);
    const std::uint8_t* t = &r.levels[0].rgba[(5 * 192 + 191) * 4];
    CHECK(t[0] == 191 && t[1] == 5 && t[2] == 9 && t[3] == 0xFF);   // opaque (the original's maximum 0x80, mapped)
    // an RGB PNG says nothing about alpha: over an original with a reflection-mask-like alpha (left 100, right 200,
    // mapped units) it takes the original's (bilinear, so the middle blends); an RGBA PNG keeps its own, clamped as before
    {
        DecodedRaster m = base;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 48; ++x) m.levels[0].rgba[(std::size_t(y) * 48 + x) * 4 + 3] = x < 24 ? 100 : 200;
        HdTextures h2;
        CHECK(h2.open(root.string()));
        CHECK(h2.apply(gs, m, 0) && m.hd);
        const auto alpha = [&](int x, int y) { return int(m.levels[0].rgba[(std::size_t(y) * 192 + std::size_t(x)) * 4 + 3]); };
        CHECK(alpha(10, 60) == 100 && alpha(180, 60) == 200);
        CHECK(alpha(96, 60) > 100 && alpha(96, 60) < 200);   // between the halves
        std::vector<std::vector<std::uint8_t>> rgba_rows(128);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 256; ++x) rgba_rows[std::size_t(y)].insert(rgba_rows[std::size_t(y)].end(), {std::uint8_t(x), std::uint8_t(y), 9, 255});
        std::vector<std::uint8_t> idat4;
        t_filter_rows(rgba_rows, 4, idat4);
        const fs::path solo = root / "solo";
        fs::create_directories(solo);
        put(solo / (name + ".png"), t_png(256, 128, 8, 6, false, idat4));
        HdTextures h3;
        CHECK(h3.open(solo.string()));
        DecodedRaster m2 = base;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 48; ++x) m2.levels[0].rgba[(std::size_t(y) * 48 + x) * 4 + 3] = x < 24 ? 100 : 200;
        m2.max_alpha = 0x80;
        CHECK(h3.apply(gs, m2, 0) && m2.levels[0].rgba[(60 * 192 + 10) * 4 + 3] == 255);   // its own alpha (opaque)
        fs::remove_all(solo);
    }
    // a .dds and a .png of one name in one folder: one replacement (the .dds, first in path order)
    put(root / "All Original Colors" / (name + ".dds"), [] {
        auto d = dds_header(4, 4, nullptr);
        d.resize(d.size() + 64, 0x80);
        return d;
    }());
    HdTextures both;
    CHECK(both.open(root.string()));
    CHECK(both.pack.files.size() == 1 && both.pack.dds_files == 1 && both.pack.find(k)->size() > 4 &&
          both.pack.find(k)->substr(both.pack.find(k)->size() - 4) == ".dds");
    // a damaged PNG: a failure, the original kept
    put(root / "All Original Colors" / (name + ".dds"), {});
    fs::remove(root / "All Original Colors" / (name + ".dds"));
    std::vector<std::uint8_t> broken = png;
    broken[60] ^= 0xFF;
    put(root / "All Original Colors" / (name + ".png"), broken);
    HdTextures dmg;
    CHECK(dmg.open(root.string()));
    DecodedRaster r2 = base;
    CHECK(!dmg.apply(gs, r2, 0) && !r2.hd && r2.levels[0].w == 48);
    fs::remove_all(root);
}

// ---- texpack: BC7 (DX10 DDS, decoded on the CPU) ----
namespace {
struct BitW {   // a 128-bit block written lowest bit first
    std::uint8_t b[16] = {};
    int at = 0;
    void put(unsigned v, int n) { for (int i = 0; i < n; ++i, ++at) b[at >> 3] |= std::uint8_t(((v >> i) & 1) << (at & 7)); }
};
int t_expand(int v, int bits, int p, bool has_p) {   // the spec's endpoint expansion
    int n = bits;
    if (has_p) { v = v << 1 | p; ++n; }
    v <<= 8 - n;
    return v | (v >> n);
}
int t_lerp(int e0, int e1, int w) { return ((64 - w) * e0 + w * e1 + 32) >> 6; }
}  // namespace

TEST(bc7_tables_are_consistent) {
    CHECK(bc7_tables_check());
    // every partition's texel 0 is in subset 0 and the anchors sit in their own subsets (catches a mistyped table entry):
    // checked through the decoder: a block whose subset 1 / 2 endpoints differ shows each texel's subset
    for (int part = 0; part < 64; ++part) {
        for (int mode : {1, 2}) {   // mode 1: 2 subsets, mode 2: 3 subsets (64 partitions each)
            BitW w;
            w.put(1u << mode, mode + 1);
            w.put(unsigned(part), 6);
            const int ns = mode == 1 ? 2 : 3, cb = mode == 1 ? 6 : 5;
            for (int c = 0; c < 3; ++c)
                for (int sub = 0; sub < ns; ++sub)
                    for (int e = 0; e < 2; ++e) w.put(c == 0 ? unsigned(sub * 20) : 0u, cb);   // red = the subset
            if (mode == 1) w.put(0, 2);   // shared P-bits
            // indices all 0 (anchors one bit shorter: the decoder must read the right count, or the block runs short)
            std::uint8_t px[16][4];
            bc7_decode_block(w.b, px);
            CHECK(px[0][0] == 0);   // texel 0: subset 0
            int seen[3] = {};
            for (int i = 0; i < 16; ++i) {
                const int sub = mode == 1 ? (px[i][0] == 0 ? 0 : 1) : (px[i][0] == 0 ? 0 : px[i][0] < 0x80 ? 1 : 2);
                ++seen[sub];
            }
            CHECK(seen[0] > 0 && seen[1] > 0 && (ns == 2 || seen[2] > 0));
        }
    }
}

TEST(bc7_modes_decode_by_the_spec) {
    // mode 6: 1 subset, RGBA 7.7.7.7 + per-endpoint P-bit, 4-bit indices (anchor 0: 3 bits)
    {
        BitW w;
        w.put(1u << 6, 7);
        const int r0 = 10, r1 = 100, g0 = 120, g1 = 5, b0 = 64, b1 = 64, a0 = 127, a1 = 30;
        for (int v : {r0, r1, g0, g1, b0, b1, a0, a1}) w.put(unsigned(v), 7);
        w.put(1, 1); w.put(0, 1);   // P-bits
        for (int i = 0; i < 16; ++i) w.put(unsigned(i), i == 0 ? 3 : 4);
        std::uint8_t px[16][4];
        bc7_decode_block(w.b, px);
        int bad = 0;
        for (int i = 0; i < 16; ++i) {
            const int wt = std::vector<int>{0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64}[std::size_t(i)];
            const int e[4][2] = {{t_expand(r0, 7, 1, true), t_expand(r1, 7, 0, true)}, {t_expand(g0, 7, 1, true), t_expand(g1, 7, 0, true)},
                                 {t_expand(b0, 7, 1, true), t_expand(b1, 7, 0, true)}, {t_expand(a0, 7, 1, true), t_expand(a1, 7, 0, true)}};
            for (int c = 0; c < 4; ++c) bad += px[i][c] != t_lerp(e[c][0], e[c][1], wt);
        }
        CHECK(bad == 0);
        CHECK(px[0][3] == 255 && px[0][0] == 21);   // 127 <<1 | 1 = 255; 10 <<1 | 1 = 21
    }
    // mode 5: 1 subset, RGB 7.7.7 + alpha 8, rotation, 2-bit colour and alpha indices (anchor 1 bit each)
    {
        BitW w;
        w.put(1u << 5, 6);
        w.put(2, 2);   // rotation: swap alpha and green
        const int c0[3] = {127, 0, 64}, c1[3] = {0, 127, 64};
        for (int c = 0; c < 3; ++c) { w.put(unsigned(c0[c]), 7); w.put(unsigned(c1[c]), 7); }
        w.put(200, 8); w.put(40, 8);
        for (int i = 0; i < 16; ++i) w.put(unsigned(i & 3), i == 0 ? 1 : 2);        // colour indices
        for (int i = 0; i < 16; ++i) w.put(unsigned((i >> 2) & 3), i == 0 ? 1 : 2); // alpha indices
        std::uint8_t px[16][4];
        bc7_decode_block(w.b, px);
        const int w2[4] = {0, 21, 43, 64};
        int bad = 0;
        for (int i = 0; i < 16; ++i) {
            int o[4];
            for (int c = 0; c < 3; ++c) o[c] = t_lerp(t_expand(c0[c], 7, 0, false), t_expand(c1[c], 7, 0, false), w2[i & 3]);
            o[3] = t_lerp(200, 40, w2[(i >> 2) & 3]);
            std::swap(o[3], o[1]);
            for (int c = 0; c < 4; ++c) bad += px[i][c] != o[c];
        }
        CHECK(bad == 0);
    }
    // mode 4 with the index selection bit: colour from the 3-bit indices, alpha from the 2-bit ones
    {
        BitW w;
        w.put(1u << 4, 5);
        w.put(0, 2);   // no rotation
        w.put(1, 1);   // index selection
        const int c0 = 31, c1 = 0;
        for (int c = 0; c < 3; ++c) { w.put(unsigned(c0), 5); w.put(unsigned(c1), 5); }
        w.put(63, 6); w.put(0, 6);
        for (int i = 0; i < 16; ++i) w.put(unsigned(i & 3), i == 0 ? 1 : 2);   // primary (2-bit): alpha here
        for (int i = 0; i < 16; ++i) w.put(unsigned(i & 7), i == 0 ? 2 : 3);   // secondary (3-bit): colour here
        std::uint8_t px[16][4];
        bc7_decode_block(w.b, px);
        const int w2[4] = {0, 21, 43, 64}, w3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
        int bad = 0;
        for (int i = 0; i < 16; ++i) {
            bad += px[i][0] != t_lerp(255, 0, w3[i & 7]);
            bad += px[i][3] != t_lerp(255, 0, w2[i & 3]);
        }
        CHECK(bad == 0);
    }
    // mode 0: 3 subsets, partition from the first 16, RGB 4.4.4 + per-endpoint P-bit, 3-bit indices, two anchors
    {
        const int part = 13;
        BitW w;
        w.put(1, 1);
        w.put(unsigned(part), 4);
        for (int c = 0; c < 3; ++c)
            for (int sub = 0; sub < 3; ++sub)
                for (int e = 0; e < 2; ++e) w.put(unsigned(c == 0 ? sub * 5 + e * 3 : 15 - sub), 4);
        for (int k = 0; k < 6; ++k) w.put(unsigned(k & 1), 1);
        const int a1 = 13, a2 = 15;   // Table.A3a / A3b, partition 13 (anchors 5? check below by reading the result)
        (void)a1; (void)a2;
        for (int i = 0; i < 16; ++i) w.put(0, 3);   // all index 0 (the anchors' short fields read as part of the zeros)
        std::uint8_t px[16][4];
        bc7_decode_block(w.b, px);
        // index 0 everywhere: each texel is its subset's endpoint 0, expanded with its P-bit
        const int part13[16] = {0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2};
        int bad = 0;
        for (int i = 0; i < 16; ++i) {
            const int sub = part13[i];
            bad += px[i][0] != t_expand(sub * 5, 4, 0, true);
            bad += px[i][1] != t_expand(15 - sub, 4, 0, true);
            bad += px[i][3] != 255;
        }
        CHECK(bad == 0);
    }
    // the reserved mode: transparent black
    {
        std::uint8_t blk[16] = {}, px[16][4];
        blk[5] = 0xFF;
        bc7_decode_block(blk, px);
        CHECK(px[3][0] == 0 && px[3][3] == 0);
    }
}

TEST(bc7_dds_replacements_load) {
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 3 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster base;
    base.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    base.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});
    base.max_alpha = 0x80;
    // a 256x128 BC7 file (DX10 header, DXGI 98): every block mode 6 with one solid colour per block
    std::vector<std::uint8_t> d = dds_header(256, 128, "DX10");
    d.resize(148, 0);
    put32(d, 128, 98);   // DXGI_FORMAT_BC7_UNORM
    put32(d, 132, 3);    // TEXTURE2D
    for (int by = 0; by < 32; ++by)
        for (int bx = 0; bx < 64; ++bx) {
            BitW w;
            w.put(1u << 6, 7);
            for (int v : {bx * 2, bx * 2, by * 4, by * 4, 9, 9, 64, 64}) w.put(unsigned(v), 7);   // alpha 0x80 opaque in PS2 units
            w.put(0, 2);
            w.put(0, 63);
            d.insert(d.end(), w.b, w.b + 16);
        }
    DdsInfo info;
    std::string why;
    CHECK(parse_dds(d.data(), d.size(), info, &why) && info.fmt == kBC7 && info.w == 256);
    const fs::path dir = one_file_pack("test_hdpack.bc7", gs, base, d);
    HdTextures hd;
    CHECK(hd.open(dir.string()));
    DecodedRaster r = base;
    CHECK(hd.apply(gs, r, 0) && r.hd && r.levels[0].w == 192 && r.levels[0].bc == 0);   // RGBA8
    const std::uint8_t* t = &r.levels[0].rgba[(std::size_t(8 * 4 + 1) * 192 + 20 * 4 + 2) * 4];   // block (20, 8)
    CHECK(t[0] == t_expand(40, 7, 0, true) && t[1] == t_expand(32, 7, 0, true) && t[2] == t_expand(9, 7, 0, true) && t[3] == 0xFF);
    // texpack (with hdmips): a BC7 file with stored mips: the mips are not used (reported, counted with the rebuilt
    // chains); the replacement is its level 0 as before
    {
        auto withmips = d;
        put32(withmips, 8, 0x20000 | 0x1007);   // DDSD_MIPMAPCOUNT
        put32(withmips, 28, 2);
        for (int i = 0; i < 32 * 16; ++i) withmips.insert(withmips.end(), {0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});   // a black / clear level 1
        CHECK(parse_dds(withmips.data(), withmips.size(), info, &why) && info.levels == 2);
        const fs::path d2 = one_file_pack("test_hdpack.bc7mips", gs, base, withmips);
        const std::size_t before = HdTextures::bc7_mips_unused(), rebuilt = HdTextures::mips_rebuilt();
        HdTextures hd2;
        CHECK(hd2.open(d2.string()));
        DecodedRaster r2 = base;
        CHECK(hd2.apply(gs, r2, 0) && r2.hd && r2.levels.size() == 1 && r2.levels[0].bc == 0);   // level 0 only (RGBA8)
        CHECK(HdTextures::bc7_mips_unused() == before + 1 && HdTextures::mips_rebuilt() == rebuilt);   // its own count
        const std::uint8_t* t2 = &r2.levels[0].rgba[(std::size_t(8 * 4 + 1) * 192 + 20 * 4 + 2) * 4];
        CHECK(t2[0] == t[0] && t2[1] == t[1] && t2[2] == t[2]);   // the file's level 0, as without mips
        fs::remove_all(d2);
    }
    // BC4 / BC5 / BC6H: refused with a reason (no crash, the original kept)
    for (std::uint32_t dxgi : {80u, 83u, 95u}) {
        auto bad = d;
        put32(bad, 128, dxgi);
        CHECK(!parse_dds(bad.data(), bad.size(), info, &why) && !why.empty());
    }
    fs::remove_all(dir);
}

TEST(png_encoded_to_bc_when_asked) {
    // HD TEXTURE MEMORY Compressed: a PNG becomes BC at load (opaque: BC1, with alpha: BC3) and takes the BC path; an
    // alpha-less PNG still takes the original's alpha there (the rule, re-encoded)
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 3 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster base;
    base.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    base.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});
    base.max_alpha = 0x80;
    HdKey k;
    CHECK(hd_key(gs, base.hdr, 0, 1, k));
    for (int with_alpha = 0; with_alpha < 2; ++with_alpha) {
        std::vector<std::vector<std::uint8_t>> rows(128);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 256; ++x) {
                rows[std::size_t(y)].insert(rows[std::size_t(y)].end(), {std::uint8_t(x), std::uint8_t(y), 9});
                if (with_alpha) rows[std::size_t(y)].push_back(x < 128 ? 0x80 : 0x40);
            }
        std::vector<std::uint8_t> idat;
        t_filter_rows(rows, with_alpha ? 4 : 3, idat);
        const fs::path dir = fs::path("build") / "test_hdpack.pngbc";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::vector<std::uint8_t> png = t_png(256, 128, 8, with_alpha ? 6 : 2, false, idat);
        std::ofstream(dir / (hd_key_name(k) + ".png"), std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        set_hd_compressed(true);
        set_hd_encode(true);
        {
            HdTextures hd;
            CHECK(hd.open(dir.string()));
            const HdCompressScope scope;
            DecodedRaster r = base;
            if (!with_alpha)   // an alpha-less file over an original with alpha 100 / 200: the rule's chain (BC3)
                for (int y = 0; y < 32; ++y)
                    for (int x = 0; x < 48; ++x) r.levels[0].rgba[(std::size_t(y) * 48 + x) * 4 + 3] = x < 24 ? 100 : 200;
            CHECK(hd.apply(gs, r, 0) && r.hd);
            CHECK(!r.levels.empty() && r.levels[0].bc != 0 && r.levels[0].w == 192 && r.levels[0].h == 128);
            const Image d0 = decoded(r.levels[0]);
            const std::uint8_t* t = &d0.rgba[(std::size_t(60) * 192 + 10) * 4];
            CHECK(std::abs(int(t[0]) - 10) <= 8 && std::abs(int(t[1]) - 60) <= 8);   // the colour (BC: close)
            if (with_alpha) CHECK(r.levels[0].bc == kBC3 && t[3] >= 0xF0);           // 0x80 PS2: opaque
            else CHECK(r.levels[0].bc == kBC3 && std::abs(int(t[3]) - 100) <= 8);   // the original's alpha
        }
        set_hd_encode(false);
        set_hd_compressed(false);
        fs::remove_all(dir);
    }
}

// hdmips (bc.h bc_consistent_levels): a chain whose levels keep their parent's mean is whole; a black or transparent level
// ends it there (at least level 0 stays)
TEST(bc_consistent_levels_cut_at_a_wrong_level) {
    const auto level = [](const Image& im, int fmt) {
        Image l;
        l.w = im.w;
        l.h = im.h;
        l.bc = fmt;
        auto b = std::make_shared<std::vector<std::uint8_t>>();
        bc_encode(fmt, im.rgba.data(), im.w, im.h, *b);
        l.blocks = b;
        return l;
    };
    for (int fmt : {kBC1, kBC3}) {
        std::vector<Image> good;
        Image cur = picture(128, 64, fmt == kBC3);
        for (int k = 0; k < 5; ++k) {
            good.push_back(level(cur, fmt));
            Image n;
            mip_down(cur, n);
            cur = std::move(n);
        }
        CHECK(bc_consistent_levels(good, fmt == kBC3) == good.size());
        std::vector<Image> dark = good;
        Image black = picture(dark[2].w, dark[2].h, false);
        std::fill(black.rgba.begin(), black.rgba.end(), std::uint8_t(0));
        for (std::size_t i = 3; i < black.rgba.size(); i += 4) black.rgba[i] = 0xFF;
        dark[2] = level(black, fmt);
        CHECK(bc_consistent_levels(dark, fmt == kBC3) == 2);
        if (fmt == kBC3) {   // a level gone transparent: cut when alpha counts, kept when it does not (TCC 0: opaque)
            std::vector<Image> clear = good;
            Image t = picture(clear[1].w, clear[1].h, true);
            for (std::size_t i = 3; i < t.rgba.size(); i += 4) t.rgba[i] = 0;
            Image ref = picture(128, 64, true);
            Image h1;
            mip_down(ref, h1);
            for (std::size_t i = 0; i < t.rgba.size(); i += 4) for (int c = 0; c < 3; ++c) t.rgba[i + std::size_t(c)] = h1.rgba[i + std::size_t(c)];
            clear[1] = level(t, fmt);
            CHECK(bc_consistent_levels(clear, true) == 1);
            CHECK(bc_consistent_levels(clear, false) == clear.size());
        }
        CHECK(bc_consistent_levels(std::vector<Image>(good.begin(), good.begin() + 1), true) == 1);
        std::vector<Image> odd = good;   // a level this check cannot read: nothing to judge, the chain kept
        odd[1].bc = 0;
        CHECK(bc_consistent_levels(odd, fmt == kBC3) == odd.size());
        odd[1] = good[1];
        odd[1].blocks.reset();
        CHECK(bc_consistent_levels(odd, fmt == kBC3) == odd.size());
    }
    {   // vertical stripes on a 2048 x 2048 level (512 blocks a row): a sampling stride dividing the row saw only the black
        // block columns (every 64th); every column is sampled, so the level and its true mip agree
        Image big;
        big.w = big.h = 2048;
        big.rgba.resize(std::size_t(2048) * 2048 * 4);
        for (int y = 0; y < 2048; ++y)
            for (int x = 0; x < 2048; ++x) {
                const std::uint8_t v = (x / 4) % 64 == 0 ? 0 : 230;
                std::uint8_t* p = &big.rgba[(std::size_t(y) * 2048 + std::size_t(x)) * 4];
                p[0] = p[1] = p[2] = v;
                p[3] = 0xFF;
            }
        Image half;
        mip_down(big, half);
        std::vector<Image> c = {level(big, kBC1), level(half, kBC1)};
        CHECK(bc_consistent_levels(c, false) == 2);
    }
    {   // 2 x 2 and 1 x 1 levels: only the texels inside count (the block's padding here black, the picture white)
        const auto padded = [](int w, int h) {
            std::uint8_t px[16][4];
            for (int t = 0; t < 16; ++t) {
                const std::uint8_t v = (t & 3) < w && (t >> 2) < h ? 240 : 0;
                px[t][0] = px[t][1] = px[t][2] = v;
                px[t][3] = 0xFF;
            }
            Image l;
            l.w = w;
            l.h = h;
            l.bc = kBC1;
            auto b = std::make_shared<std::vector<std::uint8_t>>(8);
            bc_encode_color(px, b->data(), false);
            l.blocks = b;
            return l;
        };
        std::vector<Image> c = {padded(4, 4), padded(2, 2), padded(1, 1)};
        CHECK(bc_consistent_levels(c, false) == 3);
    }
}


// ---- hdcache: the converted replacements on disk (include/b3asset/hdcache.h) ----

namespace {
bool same_levels(const std::vector<Image>& a, const std::vector<Image>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const Image &x = a[i], &y = b[i];
        if (x.w != y.w || x.h != y.h || x.bc != y.bc || x.rgba != y.rgba) return false;
        if (bool(x.blocks) != bool(y.blocks) || (x.blocks && *x.blocks != *y.blocks)) return false;
    }
    return true;
}
struct CacheDir {   // a fresh cache folder for the test; the cache off again after it
    std::filesystem::path dir;
    explicit CacheDir(const char* name, std::uint64_t cap_mb = 0) : dir(std::filesystem::path("build") / name) {
        std::filesystem::remove_all(dir);
        set_hd_cache_dir(dir.string(), cap_mb);
    }
    ~CacheDir() {
        hd_cache_flush();
        set_hd_cache_dir("");
        std::filesystem::remove_all(dir);
    }
};
// One apply of `base` by a fresh HdTextures on the pack `pack` (the counters it ended with in `hd_out`)
DecodedRaster apply_once(const std::filesystem::path& pack, const GsMemory& gs, const DecodedRaster& base, bool& hit,
                         std::size_t* masked = nullptr, std::size_t* files = nullptr, std::size_t* compressed = nullptr, std::size_t* encoded = nullptr) {
    HdTextures hd;
    hd.open(pack.string());
    DecodedRaster r = base;
    hit = hd.apply(gs, r, 0);
    if (masked) *masked = hd.masked;
    if (files) *files = hd.files_loaded;
    if (compressed) *compressed = hd.compressed;
    if (encoded) *encoded = hd.encoded;
    return r;
}
struct Case { const char* name; bool compressed, scope; int fmt; bool rule; };
}  // namespace

TEST(disk_cache_entries_equal_a_fresh_apply) {
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 3 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster plain;   // opaque original: the rule changes nothing
    plain.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    plain.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});
    plain.max_alpha = 0x80;
    DecodedRaster lim = plain;   // transparent left half: the rule cuts the replacement there (BC3 re-encoded on the BC path)
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 24; ++x) lim.levels[0].rgba[(std::size_t(y) * 48 + x) * 4 + 3] = 0;
    const Case cases[] = {
        {"rgba8 (no BC device)", false, false, kBC3, false},
        {"rgba8 alpha rule", false, false, kBC3, true},
        {"BC chain (BC1)", true, true, kBC1, false},
        {"BC chain (BC3)", true, true, kBC3, false},
        {"BC alpha rule (BC3 re-encoded)", true, true, kBC3, true},
        {"BC device, no compress scope (RGBA8)", true, false, kBC1, false},
        {"TCC 0", true, true, kBC3, false},
    };
    for (const Case& c : cases) {
        const DecodedRaster& base0 = c.rule ? lim : plain;
        DecodedRaster base = base0;
        if (std::string(c.name) == "TCC 0") base.hdr.tex0 &= ~(1ull << 34);
        const fs::path pack = one_file_pack("test_hdpack.cachepack", gs, base, bc_dds(picture(256, 128, c.fmt != kBC1), c.fmt));
        if (c.compressed) set_hd_compressed(true);
        std::optional<HdCompressScope> scope;
        if (c.scope) scope.emplace();
        bool hit0 = false, hit1 = false, hit2 = false;
        std::size_t m0 = 0, m1 = 0, m2 = 0, f2 = 9, c0 = 0, c2 = 0, e0 = 0, e2 = 0;
        const DecodedRaster fresh = apply_once(pack, gs, base, hit0, &m0, nullptr, &c0, &e0);   // no cache
        DecodedRaster cold, warm;
        HdCacheStats s0, s1, s2;
        {
            const CacheDir cache("test_hdpack.cache");
            s0 = hd_cache_stats();
            cold = apply_once(pack, gs, base, hit1, &m1);   // converted, then written
            CHECK(hd_cache_flush());
            s1 = hd_cache_stats();
            warm = apply_once(pack, gs, base, hit2, &m2, &f2, &c2, &e2);   // read back: the file not read at all
            s2 = hd_cache_stats();
            CHECK(hdcache::usage(cache.dir).entries == 1);
        }
        if (c.compressed) set_hd_compressed(false);
        std::printf("  %s: %zu level(s), bc %d, masked %zu\n", c.name, fresh.levels.size(), fresh.levels.empty() ? -1 : fresh.levels[0].bc, m0);
        CHECK(hit0 && hit1 && hit2 && fresh.hd && cold.hd && warm.hd);
        CHECK(s1.written == s0.written + 1 && s2.hits == s1.hits + 1);
        CHECK(f2 == 0);   // a hit reads no replacement file
        CHECK(same_levels(fresh.levels, cold.levels));
        CHECK(same_levels(fresh.levels, warm.levels));   // byte-identical to a fresh conversion
        CHECK(m0 == m1 && m0 == m2 && c0 == c2 && e0 == e2);
        CHECK(fresh.max_alpha == warm.max_alpha && fresh.translucent == warm.translucent);
        if (c.rule) CHECK(m0 == 1);
        fs::remove_all(pack);
    }
}

TEST(disk_cache_damaged_entry_is_rebuilt_and_a_changed_file_gets_new_keys) {
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 7 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster base;
    base.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    base.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});
    base.max_alpha = 0x80;
    const fs::path pack = one_file_pack("test_hdpack.cachedmg", gs, base, bc_dds(picture(256, 128, true), kBC3));
    bool hit = false;
    const DecodedRaster fresh = apply_once(pack, gs, base, hit);
    const CacheDir cache("test_hdpack.cache2");
    apply_once(pack, gs, base, hit);
    CHECK(hd_cache_flush());
    fs::path entry;
    for (const auto& e : fs::recursive_directory_iterator(cache.dir))
        if (e.path().extension() == hdcache::kExtension) entry = e.path();
    CHECK(!entry.empty());
    const std::uintmax_t full = fs::file_size(entry);
    for (int damage = 0; damage < 3; ++damage) {
        if (damage == 0) fs::resize_file(entry, full / 2);   // a half entry (a crash mid-write without the rename)
        if (damage == 1) {                                   // a byte flipped in the middle
            std::fstream f(entry, std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(std::streamoff(full / 2));
            f.put('\x5A');
        }
        if (damage == 2) { std::ofstream(entry, std::ios::binary) << "junk"; }   // not an entry at all
        const HdCacheStats s0 = hd_cache_stats();
        const DecodedRaster r = apply_once(pack, gs, base, hit);   // converted again
        CHECK(hd_cache_flush());
        const HdCacheStats s1 = hd_cache_stats();
        CHECK(hit && s1.corrupt == s0.corrupt + 1 && s1.written == s0.written + 1);
        CHECK(same_levels(fresh.levels, r.levels));
        CHECK(fs::file_size(entry) == full);   // written whole again
        const DecodedRaster again = apply_once(pack, gs, base, hit);
        CHECK(hit && hd_cache_stats().hits == s1.hits + 1 && same_levels(fresh.levels, again.levels));
    }
    // the pack's file changed (other picture, other size): a new key, the new file's result
    {
        HdKey k;
        CHECK(hd_key(gs, base.hdr, 0, 1, k));
        const auto d = bc_dds(picture(128, 64, true), kBC3);
        std::ofstream(pack / (hd_key_name(k) + ".dds"), std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    }
    hd_cache_flush();
    set_hd_cache_dir("");
    const DecodedRaster fresh2 = apply_once(pack, gs, base, hit);
    set_hd_cache_dir(cache.dir.string());
    const DecodedRaster r2 = apply_once(pack, gs, base, hit);
    CHECK(hit && fresh2.levels[0].w == 96 && same_levels(fresh2.levels, r2.levels));
    CHECK(hd_cache_flush());
    CHECK(hdcache::usage(cache.dir).entries == 2);
    std::string why;
    CHECK(hdcache::clear(cache.dir, &why) && hdcache::usage(cache.dir).entries == 0);
    fs::remove_all(pack);
}

TEST(disk_cache_holds_its_cap_oldest_first) {
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 3 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster base;
    base.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    base.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});
    base.max_alpha = 0x80;
    // RGBA8 entries of 768 x 512 x 4 = 1.5 MB (a 1024 x 512 picture): a 2 MB cap holds one
    const fs::path pack = one_file_pack("test_hdpack.cachecap", gs, base, bc_dds(picture(1024, 512, true), kBC3));
    const CacheDir cache("test_hdpack.cache3", 2);
    const HdCacheStats s0 = hd_cache_stats();
    bool hit = false;
    for (int v = 0; v < 3; ++v) {   // three keys: the original's alpha differs (the alpha rule's input)
        DecodedRaster r = base;
        r.levels[0].rgba[3] = std::uint8_t(0xFF - v);
        apply_once(pack, gs, r, hit);
        CHECK(hit && hd_cache_flush());
    }
    const HdCacheStats s1 = hd_cache_stats();
    const hdcache::Usage u = hdcache::usage(cache.dir);
    CHECK(s1.written == s0.written + 3 && s1.evicted >= s0.evicted + 2);
    CHECK(u.entries == 1 && u.bytes <= (std::uint64_t(2) << 20));
    DecodedRaster newest = base;   // the newest stays
    newest.levels[0].rgba[3] = 0xFD;
    const HdCacheStats s2 = hd_cache_stats();
    apply_once(pack, gs, newest, hit);
    CHECK(hit && hd_cache_stats().hits == s2.hits + 1);
    fs::remove_all(pack);
}

TEST(disk_cache_same_size_and_time_other_bytes_is_another_file) {
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 3 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster base;
    base.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    base.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});
    base.max_alpha = 0x80;
    Image a = picture(256, 128, true), b = a;
    for (std::size_t i = 0; i < b.rgba.size(); i += 4) b.rgba[i] ^= 0x3C;   // other colours, the same size of file
    const fs::path pack = one_file_pack("test_hdpack.cachesame", gs, base, bc_dds(a, kBC3));
    HdKey k;
    CHECK(hd_key(gs, base.hdr, 0, 1, k));
    const fs::path file = pack / (hd_key_name(k) + ".dds");
    const auto when = fs::last_write_time(file);
    const CacheDir cache("test_hdpack.cache4");
    bool hit = false;
    const DecodedRaster first = apply_once(pack, gs, base, hit);
    CHECK(hit && hd_cache_flush());
    const auto d = bc_dds(b, kBC3);
    CHECK(d.size() == fs::file_size(file));
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    fs::last_write_time(file, when);   // the same size and the same time
    set_hd_cache_dir("");
    const DecodedRaster fresh = apply_once(pack, gs, base, hit);
    set_hd_cache_dir(cache.dir.string());
    const HdCacheStats s0 = hd_cache_stats();
    const DecodedRaster r = apply_once(pack, gs, base, hit);
    CHECK(hit && hd_cache_stats().hits == s0.hits && !same_levels(first.levels, r.levels) && same_levels(fresh.levels, r.levels));
    fs::remove_all(pack);
}

TEST(prefetch_takes_no_raster_once_told_to_stop) {
    namespace fs = std::filesystem;
    GsMemory gs;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) gs.write(PSMT8, 0, 2, x, y, (x * 3 + y) & 0xFF);
    write_clut(gs, 64, palette(256));
    DecodedRaster base;
    base.hdr = header(PSMT8, 6, 5, 0, 2, 64, 48, 32);
    base.levels.push_back(Image{48, 32, std::vector<std::uint8_t>(48 * 32 * 4, 0xFF)});
    base.max_alpha = 0x80;
    const fs::path pack = one_file_pack("test_hdpack.prefetchstop", gs, base, bc_dds(picture(256, 128, true), kBC3));
    HdTextures hd;
    CHECK(hd.open(pack.string()));
    const std::vector<std::uint8_t> none(64, 0);   // (no raster in it: the stop is checked before any)
    bool stopped = false;
    CHECK(hd.prefetch_data({{"x", Bytes{none.data(), none.size()}}}, 4, [] { return false; }, 0, &stopped) == 0 && stopped);
    CHECK(hd.prefetch_data({{"x", Bytes{none.data(), none.size()}}}, 4, [] { return true; }, 0, &stopped) == 0 && !stopped);
    fs::remove_all(pack);
}

TEST_MAIN("hdpack")
