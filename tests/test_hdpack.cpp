// HD texture packs (b3asset/hdpack.h, docs/native-renderer.md §18): XXH3-64, PCSX2 replacement keys, names, DDS and
// the replacement hook on synthetic data only (no pack or game data).
#include <cstring>
#include <filesystem>
#include <fstream>

#include "b3asset/gs_memory.h"
#include "b3asset/hdpack.h"
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

TEST_MAIN("hdpack")
