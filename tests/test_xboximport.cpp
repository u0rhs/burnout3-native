// Import Xbox textures (b3asset/xdvdfs.h, xboxtex.h, xboximport.h): everything synthetic, made here (no disc data):
// XDVDFS images with a small tree (and hostile ones), Xbox textures in each format, the Morton order, the colour check
// and detail transfer, the car paint mapping, the DDS writer, and the import end to end on a made-up PS2 file and Xbox
// image (the file named after the PS2 raster's key, found by the pack lookup).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include "b3asset/bc.h"
#include "b3asset/gs_memory.h"
#include "b3asset/hdpack.h"
#include "b3asset/raster.h"
#include "b3asset/xboximport.h"
#include "b3asset/xboxtex.h"
#include "b3asset/xdvdfs.h"
#include "b3port/fileio.h"
#include "test.h"

using namespace b3asset;
namespace fs = std::filesystem;
namespace xi = b3asset::xboximport;

namespace {

void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint16_t v) {
    if (d.size() < at + 2) d.resize(at + 2);
    std::memcpy(&d[at], &v, 2);
}
void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    if (d.size() < at + 4) d.resize(at + 4);
    std::memcpy(&d[at], &v, 4);
}
void put64(std::vector<std::uint8_t>& d, std::size_t at, std::uint64_t v) {
    if (d.size() < at + 8) d.resize(at + 8);
    std::memcpy(&d[at], &v, 8);
}
void write(const fs::path& p, const std::vector<std::uint8_t>& d) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}
fs::path temp(const char* name) {
    const fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// ---- an XDVDFS image ----
struct Node {
    std::string name;
    bool dir = false;
    std::vector<std::uint8_t> data;
    std::vector<Node> kids;
    std::uint32_t sector = 0, size = 0;
};
Node file(const std::string& n, std::vector<std::uint8_t> d) { Node x; x.name = n; x.data = std::move(d); return x; }
Node folder(const std::string& n, std::vector<Node> k) { Node x; x.name = n; x.dir = true; x.kids = std::move(k); return x; }

std::uint32_t table_bytes(const Node& d) {
    std::uint32_t n = 0;
    for (const Node& k : d.kids) n += (14 + std::uint32_t(k.name.size()) + 3) & ~3u;
    return n;
}
void place(Node& d, std::uint32_t& next) {   // sectors: the folder's table, then its children
    d.size = d.dir ? table_bytes(d) : std::uint32_t(d.data.size());
    d.sector = next;
    next += std::max<std::uint32_t>(1, (d.size + 2047) / 2048);
    for (Node& k : d.kids) place(k, next);
}
void emit(const Node& d, std::vector<std::uint8_t>& img, std::uint64_t base) {
    const std::size_t at = std::size_t(base + std::uint64_t(d.sector) * 2048);
    if (img.size() < at + std::max<std::uint32_t>(d.size, 1)) img.resize(at + std::max<std::uint32_t>(d.size, 1), 0);
    if (!d.dir) {
        std::copy(d.data.begin(), d.data.end(), img.begin() + std::ptrdiff_t(at));
        return;
    }
    // a right-leaning tree: each entry's right child is the next
    std::uint32_t o = 0;
    for (std::size_t i = 0; i < d.kids.size(); ++i) {
        const Node& k = d.kids[i];
        const std::uint32_t len = (14 + std::uint32_t(k.name.size()) + 3) & ~3u;
        put16(img, at + o, 0);
        put16(img, at + o + 2, i + 1 < d.kids.size() ? std::uint16_t((o + len) / 4) : 0);
        put32(img, at + o + 4, k.sector);
        put32(img, at + o + 8, k.size);
        img[at + o + 12] = k.dir ? 0x10 : 0x20;
        img[at + o + 13] = std::uint8_t(k.name.size());
        std::memcpy(&img[at + o + 14], k.name.data(), k.name.size());
        o += len;
    }
    for (const Node& k : d.kids) emit(k, img, base);
}
std::vector<std::uint8_t> xiso(Node root, std::uint64_t base = 0) {
    std::uint32_t next = 33;
    place(root, next);
    std::vector<std::uint8_t> img(std::size_t(base) + 33 * 2048, 0);
    const std::size_t vd = std::size_t(base) + 32 * 2048;
    std::memcpy(&img[vd], "MICROSOFT*XBOX*MEDIA", 20);
    put32(img, vd + 0x14, root.sector);
    put32(img, vd + 0x18, root.size);
    std::memcpy(&img[vd + 0x7EC], "MICROSOFT*XBOX*MEDIA", 20);
    emit(root, img, base);
    img.resize((img.size() + 2047) / 2048 * 2048, 0);
    return img;
}
std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

// ---- Xbox textures ----
Image pattern(int w, int h, int seed, bool alpha = false) {
    Image im;
    im.w = w;
    im.h = h;
    im.rgba.resize(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &im.rgba[(std::size_t(y) * w + x) * 4];
            p[0] = std::uint8_t(40 + (x * 160) / w + seed);
            p[1] = std::uint8_t(60 + (y * 150) / h);
            p[2] = std::uint8_t(90 + ((x + y) * 50) / (w + h) + seed / 2);
            p[3] = alpha ? std::uint8_t(x < w / 2 ? 255 : 0) : 255;
        }
    return im;
}
// One serialized texture (D3D header + the raster tail, the pixels at +0x80, a P8's palettes after them)
std::vector<std::uint8_t> xtex(const std::string& name, int fmt, int lw, int lh, const std::vector<std::uint8_t>& pixels,
                               const std::vector<std::vector<std::uint8_t>>& palettes = {}) {
    std::vector<std::uint8_t> d(0x80, 0);
    put32(d, 0, 0x00040001);
    put32(d, 4, 0x80);
    put32(d, 12, 0x20u | std::uint32_t(fmt) << 8 | 1u << 16 | std::uint32_t(lw) << 20 | std::uint32_t(lh) << 24);
    put32(d, 0x38, 1u << lw);
    put32(d, 0x3C, 1u << lh);
    std::memcpy(&d[0x48], name.data(), std::min<std::size_t>(name.size(), 31));
    d.insert(d.end(), pixels.begin(), pixels.end());
    d[0x69] = std::uint8_t(palettes.size());
    for (std::size_t i = 0; i < palettes.size(); ++i) {
        put32(d, 0x14 + 4 * i, std::uint32_t(d.size()));
        d.insert(d.end(), palettes[i].begin(), palettes[i].end());
    }
    while (d.size() % 16) d.push_back(0);
    return d;
}
std::vector<std::uint8_t> dxt(const Image& im, int fmt) {
    std::vector<std::uint8_t> out;
    bc_encode(fmt == xbox::kDXT1 ? kBC1 : fmt == xbox::kDXT3 ? kBC2 : kBC3, im.rgba.data(), im.w, im.h, out);
    return out;
}
// swizzled P8 indices of a row-by-row index image
std::vector<std::uint8_t> swizzled(const std::vector<std::uint8_t>& idx, int w, int h) {
    std::vector<std::uint8_t> s(idx.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) s[xbox::swizzle(std::uint32_t(x), std::uint32_t(y), std::uint32_t(w), std::uint32_t(h))] = idx[std::size_t(y) * w + x];
    return s;
}

// ---- a PS2 file: CT32 rasters in the layout read_raster_header / decode_raster replay ----
void qw(std::vector<std::uint8_t>& v, std::uint64_t lo, std::uint64_t hi) {
    const std::size_t at = v.size();
    put64(v, at, lo);
    put64(v, at + 8, hi);
}
void gif_upload(std::vector<std::uint8_t>& v, std::uint32_t rrw, std::uint32_t rrh, const std::vector<std::uint8_t>& px) {
    qw(v, 3 | (1ull << 60), 0xE);
    qw(v, 0, 0x51);
    qw(v, rrw | std::uint64_t(rrh) << 32, 0x52);
    qw(v, 0, 0x53);
    const std::size_t nq = (px.size() + 15) / 16;
    qw(v, nq | (2ull << 58), 0);
    std::vector<std::uint8_t> pad = px;
    pad.resize(nq * 16, 0);
    v.insert(v.end(), pad.begin(), pad.end());
}
// A w x h CT32 raster (power-of-two size, w >= 64), PS2 alpha (0x80 opaque)
std::vector<std::uint8_t> ps2_raster(const std::string& name, const Image& im) {
    const int lw = int(std::log2(im.w)), lh = int(std::log2(im.h));
    const std::uint32_t tbw = std::max(1, im.w / 64);
    std::vector<std::uint8_t> f(0x200, 0);
    put32(f, 0x0C, std::uint32_t(im.w));
    put32(f, 0x10, std::uint32_t(im.h));
    put32(f, 0x14, 32);
    f[0x20] = 4;
    f[0x23] = 0x05;
    put64(f, 0x3C, std::uint64_t(tbw) << 14 | std::uint64_t(PSMCT32) << 20 | std::uint64_t(lw) << 26 | std::uint64_t(lh) << 30 | 1ull << 34);
    std::memcpy(&f[0xA8], name.data(), name.size());
    put32(f, 0x24, 0x100);
    put32(f, 0x100, 1);
    put32(f, 0x108, 1);
    std::vector<std::uint8_t> px(im.rgba);
    for (std::size_t i = 3; i < px.size(); i += 4) px[i] = std::uint8_t((px[i] + 1) / 2);   // 255 -> 0x80
    std::vector<std::uint8_t> up;
    gif_upload(up, std::uint32_t(im.w), std::uint32_t(im.h), px);
    const std::size_t at = 0x200;
    f.resize(at);
    f.insert(f.end(), up.begin(), up.end());
    const std::size_t e = 0x110;
    put32(f, e, 0x10000002);
    put32(f, e + 12, 0x50000002);
    put64(f, e + 16, 1 | (1ull << 60));
    put64(f, e + 24, 0xE);
    put64(f, e + 32, std::uint64_t(tbw) << 48 | std::uint64_t(PSMCT32) << 56);
    put64(f, e + 40, 0x50);
    put32(f, e + 48, 0x30000000u | std::uint32_t(up.size() / 16));
    put32(f, e + 52, std::uint32_t(at));
    put32(f, e + 60, 0x50000000u | std::uint32_t(up.size() / 16));
    while (f.size() % 16) f.push_back(0);
    return f;
}

int max_diff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    int m = 0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) m = std::max(m, std::abs(int(a[i]) - int(b[i])));
    return a.size() == b.size() ? m : 999;
}

}  // namespace

TEST(xdvdfs_lists_and_reads_a_small_tree) {
    const fs::path d = temp("b3_test_xdvdfs");
    const Node root = folder("", {file("default.xbe", bytes("XBEH")), folder("Data", {file("Global.txd", bytes("global")), file("Empty.bin", {})}),
                                  folder("Tracks", {folder("US", {folder("C1_V1", {file("static.dat", std::vector<std::uint8_t>(5000, 7))})})})});
    write(d / "a.iso", xiso(root));
    XdvdfsImage im;
    std::string why;
    CHECK(im.open(d / "a.iso", &why));
    CHECK(im.base() == 0);
    std::size_t files = 0;
    for (const auto& e : im.list()) files += !e.dir;
    CHECK(files == 4 && im.list().size() == 8);   // 4 files, 4 folders (Data, Tracks, US, C1_V1)
    std::vector<std::uint8_t> got;
    CHECK(im.read("data/GLOBAL.TXD", got) && std::string(got.begin(), got.end()) == "global");   // any case
    CHECK(im.read("Tracks\\US\\C1_V1\\static.dat", got) && got.size() == 5000 && got[4999] == 7);
    CHECK(im.read("Data/Empty.bin", got) && got.empty());
    CHECK(!im.find("Data"));            // a folder is not a file
    CHECK(!im.find("Data/missing.txd"));
    // not an Xbox image: a plain message
    write(d / "b.iso", std::vector<std::uint8_t>(80 * 2048, 0));
    CHECK(!im.open(d / "b.iso", &why) && why.find("not an Xbox disc image") != std::string::npos);
    CHECK(!im.open(d / "none.iso", &why) && why == "cannot open the file");
    fs::remove_all(d);
}

TEST(xdvdfs_survives_hostile_trees) {
    const fs::path d = temp("b3_test_xdvdfs_bad");
    // a folder whose entry points back at the root (a cycle), an entry pointing at itself, a file past the image's end
    std::vector<std::uint8_t> img = xiso(folder("", {folder("Loop", {}), file("ok.bin", bytes("ok")), file("far.bin", bytes("far"))}));
    const std::size_t vd = 32 * 2048;
    std::uint32_t root;
    std::memcpy(&root, &img[vd + 0x14], 4);
    const std::size_t t = std::size_t(root) * 2048;
    // entry 0 "Loop": make it the root folder itself (its sector and size)
    std::uint32_t rsize;
    std::memcpy(&rsize, &img[vd + 0x18], 4);
    put32(img, t + 4, root);
    put32(img, t + 8, rsize);
    put16(img, t, 0);   // left child: itself (offset 0 is "none": no loop), and the right chain stays
    // entry 2 "far.bin": a sector far past the end
    std::size_t o = 0;
    for (int i = 0; i < 2; ++i) o += (14 + std::size_t(img[t + o + 13]) + 3) & ~std::size_t(3);
    put32(img, t + o + 4, 0x00100000);
    put16(img, t + o, std::uint16_t(o / 4));   // its left child: itself
    write(d / "bad.iso", img);
    XdvdfsImage im;
    std::string why;
    CHECK(im.open(d / "bad.iso", &why));
    CHECK(im.find("ok.bin") && !im.find("far.bin"));   // the file outside the image is left out
    CHECK(im.list().size() < 10);                      // the cycle was read once
    // a root folder outside the image: refused with a reason
    put32(img, vd + 0x14, 0x00200000);
    write(d / "bad2.iso", img);
    CHECK(!im.open(d / "bad2.iso", &why) && why.find("outside the image") != std::string::npos);
    // a full dump: the game partition further in (the smallest offset the reader tries, 0x2080000, kept sparse)
    {
        std::vector<std::uint8_t> part = xiso(folder("", {file("default.xbe", bytes("X"))}));
        std::ofstream f(d / "full.iso", std::ios::binary);
        f.seekp(std::streamoff(0x2080000));
        f.write(reinterpret_cast<const char*>(part.data()), std::streamsize(part.size()));
    }
    CHECK(im.open(d / "full.iso", &why) && im.base() == 0x2080000 && im.find("default.xbe"));
    fs::remove_all(d);
}

TEST(morton_order_on_non_square_sizes) {
    // 4 x 2: x bit 0, y bit 0, x bit 1 (y has no more bits)
    CHECK(xbox::swizzle(1, 0, 4, 2) == 1);
    CHECK(xbox::swizzle(0, 1, 4, 2) == 2);
    CHECK(xbox::swizzle(2, 0, 4, 2) == 4);
    CHECK(xbox::swizzle(3, 1, 4, 2) == 7);
    // 2 x 8: x bit 0, y bit 0, then y bits 1, 2
    CHECK(xbox::swizzle(1, 0, 2, 8) == 1 && xbox::swizzle(0, 1, 2, 8) == 2 && xbox::swizzle(0, 2, 2, 8) == 4 && xbox::swizzle(1, 7, 2, 8) == 15);
    for (const auto& [w, h] : std::vector<std::pair<int, int>>{{8, 4}, {4, 16}, {64, 8}, {1, 32}}) {   // a bijection
        std::vector<int> seen(std::size_t(w) * h, 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) ++seen[xbox::swizzle(std::uint32_t(x), std::uint32_t(y), std::uint32_t(w), std::uint32_t(h))];
        CHECK(std::all_of(seen.begin(), seen.end(), [](int c) { return c == 1; }));
        std::vector<std::uint8_t> idx(seen.size()), back(seen.size());
        for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = std::uint8_t(i * 7);
        xbox::unswizzle(swizzled(idx, w, h).data(), back.data(), std::uint32_t(w), std::uint32_t(h), 1);
        CHECK(back == idx);
    }
}

TEST(xbox_textures_decode_dxt_and_swizzled_p8) {
    const Image a = pattern(16, 8, 0), b = pattern(8, 8, 30, true);
    // P8 8 x 4 with two palettes, B G R A
    std::vector<std::uint8_t> idx(32);
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = std::uint8_t(i * 5 % 256);
    std::vector<std::uint8_t> pal0(1024), pal1(1024);
    for (int i = 0; i < 256; ++i) {
        const std::uint8_t c0[4] = {std::uint8_t(i), std::uint8_t(255 - i), 9, 200}, c1[4] = {1, 2, std::uint8_t(i), 255};
        std::memcpy(&pal0[std::size_t(i) * 4], c0, 4);
        std::memcpy(&pal1[std::size_t(i) * 4], c1, 4);
    }
    std::vector<std::uint8_t> f(36, 0xEE);   // junk before (a static.dat's own data)
    const std::vector<std::vector<std::uint8_t>> texs = {xtex("Dxt1Tex", xbox::kDXT1, 4, 3, dxt(a, xbox::kDXT1)),
                                                         xtex("Dxt5Tex", xbox::kDXT5, 3, 3, dxt(b, xbox::kDXT5)),
                                                         xtex("Dxt3Tex", xbox::kDXT3, 3, 3, dxt(b, xbox::kDXT3)),
                                                         xtex("P8Tex", xbox::kP8, 3, 2, swizzled(idx, 8, 4), {pal0, pal1})};
    for (const auto& t : texs) f.insert(f.end(), t.begin(), t.end());
    const auto found = xbox::scan(f.data(), f.size());
    CHECK(found.size() == 4);
    std::map<std::string, xbox::Texture> by;
    for (const auto& t : found) by[t.name] = t;
    CHECK(by["Dxt1Tex"].width == 16 && by["Dxt1Tex"].height == 8 && by["Dxt1Tex"].format == xbox::kDXT1);
    CHECK(by["P8Tex"].palettes.size() == 2 && by["P8Tex"].width == 8 && by["P8Tex"].height == 4);
    std::vector<std::uint8_t> got, ref(16 * 8 * 4);
    CHECK(xbox::decode(f.data(), f.size(), by["Dxt1Tex"], got));
    bc_decode(kBC1, dxt(a, xbox::kDXT1).data(), 16, 8, ref.data());
    CHECK(got == ref);
    CHECK(max_diff(got, a.rgba) <= 24);
    ref.assign(8 * 8 * 4, 0);
    CHECK(xbox::decode(f.data(), f.size(), by["Dxt5Tex"], got));
    bc_decode(kBC3, dxt(b, xbox::kDXT5).data(), 8, 8, ref.data());
    CHECK(got == ref && got[3] == 255 && got[(8 * 0 + 7) * 4 + 3] == 0);
    CHECK(xbox::decode(f.data(), f.size(), by["Dxt3Tex"], got) && got[3] == 255);
    CHECK(xbox::decode(f.data(), f.size(), by["P8Tex"], got, 0));
    bool p8 = true;
    for (int i = 0; i < 32; ++i) {
        const std::uint8_t k = idx[std::size_t(i)];
        p8 = p8 && got[std::size_t(i) * 4] == 9 && got[std::size_t(i) * 4 + 1] == 255 - k && got[std::size_t(i) * 4 + 2] == k && got[std::size_t(i) * 4 + 3] == 200;
    }
    CHECK(p8);
    CHECK(xbox::decode(f.data(), f.size(), by["P8Tex"], got, 1) && got[0] == idx[0] && got[2] == 1 && got[3] == 255);
    // a texture whose pixels run past the file's end is not taken
    std::vector<std::uint8_t> cut(texs[0].begin(), texs[0].begin() + 0x90);
    CHECK(xbox::scan(cut.data(), cut.size()).empty());
    // a TXD: its directory's entries
    std::vector<std::uint8_t> txd = {0x00, 0x00, 0x3C, 0x54, 0x81, 0xED, 0xDE, 0xBC};
    put32(txd, 8, 2);
    txd.resize(48, 0);
    put32(txd, 16 + 8, 48);
    put32(txd, 32 + 8, std::uint32_t(48 + texs[0].size()));
    txd.insert(txd.end(), texs[0].begin(), texs[0].end());
    txd.insert(txd.end(), texs[3].begin(), texs[3].end());
    const auto t2 = xbox::scan(txd.data(), txd.size());
    CHECK(t2.size() == 2 && t2[0].name == "Dxt1Tex" && t2[1].name == "P8Tex");
}

TEST(detail_transfer_keeps_the_ps2_picture) {
    // the "Xbox" texture: 2x, darker (0.8), with fine detail the PS2 one lacks
    const Image ps2 = pattern(32, 32, 0);
    Image xb;
    xb.w = xb.h = 64;
    xb.rgba.resize(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            for (int k = 0; k < 4; ++k) {
                const int v = ps2.rgba[(std::size_t(y / 2) * 32 + x / 2) * 4 + k];
                const int detail = ((x ^ y) & 1) ? 12 : -12;
                xb.rgba[(std::size_t(y) * 64 + x) * 4 + k] = std::uint8_t(k == 3 ? 255 : std::clamp(int(v * 0.8 + detail), 0, 255));
            }
    const xi::Check c = xi::colour_check(ps2, xb);
    CHECK(c.ok && c.psnr > 35);
    CHECK_NEAR(c.gain[0], 1.25, 0.03);
    const Image out = xi::detail_transfer(ps2, xb, c, true);
    CHECK(out.w == 64 && out.h == 64);
    // down to the PS2 size: the PS2 picture (the detail averages out); the detail is there at the full size
    const Image down = xi::box_down(out, 32, 32);
    double sum = 0;
    for (std::size_t i = 0; i < down.rgba.size(); ++i) sum += std::abs(int(down.rgba[i]) - int(ps2.rgba[i]));
    CHECK(sum / double(down.rgba.size()) < 1.5);
    CHECK(max_diff(down.rgba, ps2.rgba) <= 6);
    CHECK(std::abs(int(out.rgba[0]) - int(out.rgba[4])) >= 20);   // neighbours differ by the checker (x 1.25)
    // the art differs (noise): the check fails; much darker (gain outside 0.5..2): fails
    Image noise = xb;
    for (std::size_t i = 0; i < noise.rgba.size(); i += 4)
        for (int k = 0; k < 3; ++k) noise.rgba[i + std::size_t(k)] = std::uint8_t((i * 2654435761u >> (8 + 4 * k)) & 0xFF);
    CHECK(!xi::colour_check(ps2, noise).ok);
    Image dark = xb;
    for (std::size_t i = 0; i < dark.rgba.size(); i += 4)
        for (int k = 0; k < 3; ++k) dark.rgba[i + std::size_t(k)] = std::uint8_t(dark.rgba[i + std::size_t(k)] / 3);
    const xi::Check cd = xi::colour_check(ps2, dark);
    CHECK(!cd.ok && cd.gain[0] > 2);
    // bilinear: a whole multiple up of a flat image is flat; wrap reads the far edge
    Image flat = pattern(4, 4, 0);
    for (std::size_t i = 0; i < flat.rgba.size(); ++i) flat.rgba[i] = 77;
    CHECK(max_diff(xi::bilinear(flat, 16, 16, false).rgba, std::vector<std::uint8_t>(16 * 16 * 4, 77)) == 0);
    Image edge = flat;
    edge.rgba[(3 * 4 + 3) * 4] = 255;   // the last texel
    CHECK(xi::bilinear(edge, 8, 8, true).rgba[0] > 77 && xi::bilinear(edge, 8, 8, false).rgba[0] == 77);
}

TEST(car_paint_mapping_from_the_ps2_decode) {
    // PS2 8x8 paint; the Xbox index image 16x8: index = the PS2 texel's colour class; one Xbox texel's index is wrong
    Image ps2;
    ps2.w = ps2.h = 8;
    ps2.rgba.resize(8 * 8 * 4);
    const std::uint8_t cols[4][4] = {{200, 30, 30, 255}, {30, 200, 30, 255}, {30, 30, 200, 128}, {240, 240, 240, 255}};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) std::memcpy(&ps2.rgba[(std::size_t(y) * 8 + x) * 4], cols[(x / 2 + y / 4) % 4], 4);
    std::vector<std::uint8_t> idx(16 * 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x) idx[std::size_t(y) * 16 + x] = std::uint8_t(10 + ((x / 2) / 2 + y / 4) % 4);
    idx[5] = 10 + (((5 / 2) / 2 + 0) % 4 + 1) % 4;   // another class's index: its colour differs, the PS2 texel is kept
    xi::CarCheck cc;
    const Image out = xi::car_map(ps2, idx, 16, 8, cc);
    CHECK(cc.ok && out.w == 16 && out.h == 8);
    CHECK(cc.fallback > 0 && cc.fallback < 0.02);
    CHECK(cc.psnr > 40);
    bool same = true;   // every texel the PS2 one at its place (the wrong index fell back)
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x) same = same && std::memcmp(&out.rgba[(std::size_t(y) * 16 + x) * 4], &ps2.rgba[(std::size_t(y) * 8 + x / 2) * 4], 4) == 0;
    CHECK(same);
    // an index image unrelated to the paint: not consistent
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = std::uint8_t(i % 3);
    xi::car_map(ps2, idx, 16, 8, cc);
    CHECK(!cc.ok);
}

TEST(dds_writer_bc1_bc3_and_ps2_alpha) {
    int fmt = 0;
    const Image opaque = pattern(64, 32, 5), cut = pattern(32, 32, 0, true);
    auto d = xi::encode_dds(opaque, &fmt);
    CHECK(fmt == kBC1);
    DdsInfo info;
    CHECK(parse_dds(d.data(), d.size(), info) && info.fmt == kBC1 && info.w == 64 && info.h == 32 && d.size() == 128 + 64 * 32 / 2);
    Image back;
    CHECK(decode_dds(d.data(), d.size(), back) && max_diff(back.rgba, opaque.rgba) <= 12);
    d = xi::encode_dds(cut, &fmt);
    CHECK(fmt == kBC3);
    CHECK(parse_dds(d.data(), d.size(), info) && info.fmt == kBC3);
    std::vector<std::uint8_t> raw(32 * 32 * 4);
    bc_decode(kBC3, d.data() + 128, 32, 32, raw.data());
    CHECK(raw[3] == 0x80 && raw[(31) * 4 + 3] == 0);   // stored in PS2 units: opaque 0x80
    CHECK(decode_dds(d.data(), d.size(), back) && back.rgba[3] == 255 && back.rgba[31 * 4 + 3] == 0);   // loaded: 255 again
    // extend: the edge repeated
    const Image e = xi::extend(pattern(4, 2, 0), 8, 4);
    CHECK(e.w == 8 && e.h == 4 && std::memcmp(&e.rgba[(3 * 8 + 7) * 4], &pattern(4, 2, 0).rgba[(1 * 4 + 3) * 4], 4) == 0);
}

TEST(hd_each_raster_base_key_is_the_first_candidate) {
    const std::vector<std::uint8_t> f = ps2_raster("brick", pattern(64, 64, 3));
    GsMemory gs;
    std::vector<HdKey> all, base;
    hd_each_raster(Bytes{f.data(), f.size()}, gs, [&](const HdRasterVariant& v) { if (v.base()) all.push_back(v.base()->key); return true; });
    const std::size_t n = hd_each_raster(Bytes{f.data(), f.size()}, gs, [&](const HdRasterVariant& v) {
        if (v.base()) base.push_back(v.base()->key);
        return true;
    }, false);
    CHECK(n == 1 && all.size() == 1 && base == all);
    DecodedRaster r;
    CHECK(decode_raster(Bytes{f.data(), f.size()}, 0, gs, r) && r.levels[0].w == 64 && r.levels[0].rgba[3] == 255);
    HdKey k;
    CHECK(hd_key(gs, r.hdr, 0, 1, k) && k == all[0]);
}

// The made-up disc pair under `d` (game/: the PS2 files; `xbox_name`: the Xbox image): one route's static.dat with
// "Brick" (64x64), "water1" and "Same"; the Xbox "brick" 128x128 DXT1 (darker, with detail), "water1" 128x128, "Same"
// at the PS2 size. Returns the PS2 static.dat; `brick_out` receives the PS2 Brick picture.
std::vector<std::uint8_t> made_up_disc(const fs::path& d, const fs::path& xbox_name, Image* brick_out = nullptr) {
    // the PS2 game: one route's static.dat with "Brick" (64x64), "water1" (64x64) and "Same" (64x64); Global.txd
    const Image brick = pattern(64, 64, 10), water = pattern(64, 64, 40), same = pattern(64, 64, 70);
    std::vector<std::uint8_t> ps2 = ps2_raster("Brick", brick);
    const std::size_t w_at = ps2.size(), s_at = w_at + ps2_raster("water1", water).size();
    { auto r = ps2_raster("water1", water); ps2.insert(ps2.end(), r.begin(), r.end()); }
    { auto r = ps2_raster("Same", same); ps2.insert(ps2.end(), r.begin(), r.end()); }
    (void)s_at;
    write(d / "game" / "TRACKS" / "US" / "C1_V1" / "STATIC.DAT", ps2);
    write(d / "game" / "DATA" / "GLOBAL.TXD", std::vector<std::uint8_t>(64, 0));
    // the Xbox disc: "brick" 128x128 DXT1 (darker, with detail), "water1" 128x128, "Same" at the PS2 size
    Image xb;
    xb.w = xb.h = 128;
    xb.rgba.resize(128 * 128 * 4);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            for (int k = 0; k < 4; ++k)
                xb.rgba[(std::size_t(y) * 128 + x) * 4 + k] =
                    std::uint8_t(k == 3 ? 255 : std::clamp(int(brick.rgba[(std::size_t(y / 2) * 64 + x / 2) * 4 + k] * 0.85) + ((x / 4 + y / 4) % 2 ? 6 : -6), 0, 255));
    std::vector<std::uint8_t> st(64, 0x11);
    for (const auto& t : {xtex("brick", xbox::kDXT1, 7, 7, dxt(xb, xbox::kDXT1)), xtex("water1", xbox::kDXT1, 7, 7, dxt(xb, xbox::kDXT1)),
                          xtex("Same", xbox::kDXT1, 6, 6, dxt(same, xbox::kDXT1))})
        st.insert(st.end(), t.begin(), t.end());
    std::vector<std::uint8_t> empty_txd = {0x00, 0x00, 0x3C, 0x54, 0x81, 0xED, 0xDE, 0xBC, 0, 0, 0, 0, 0, 0, 0, 0};
    write(d / xbox_name, xiso(folder("", {file("default.xbe", bytes("XBEH")),
                                           folder("Data", {file("Global.txd", empty_txd), file("Frontend.txd", empty_txd)}),
                                           folder("Tracks", {folder("US", {folder("C1_V1", {file("static.dat", st)})})})})));
    if (brick_out) *brick_out = brick;
    return ps2;
}

TEST(import_end_to_end_on_a_made_up_disc) {
    const fs::path d = temp("b3_test_xboximport");
    Image brick;
    const std::vector<std::uint8_t> ps2 = made_up_disc(d, "xbox.iso", &brick);
    const fs::path out = d / "user" / xi::kFolderName;
    xi::Options o;
    o.need_bytes = 0;
    o.threads = 2;
    // cancelled before it starts: nothing written, no folder left
    std::atomic<bool> cancel{true};
    xi::Result r = xi::run(d / "xbox.iso", d / "game", out, {}, &cancel, o);
    CHECK(!r.ok && r.cancelled && !fs::exists(out));
    fs::path partial = out;
    partial += ".partial";
    CHECK(!fs::exists(partial));
    // the import
    int calls = 0;
    r = xi::run(d / "xbox.iso", d / "game", out, [&](const xi::Progress&) { ++calls; }, nullptr, o);
    CHECK(r.ok && r.error.empty());
    CHECK(r.files == 2 && r.keys == 3 && r.written == 1 && r.cars == 0 && calls > 0);
    CHECK(r.skipped[xi::kWater] == 1 && r.skipped[xi::kNotLarger] == 1);
    CHECK(!fs::exists(partial));
    // the file is named after the PS2 raster's key, and the pack lookup (the game's, b3hdkeys') finds it
    GsMemory gs;
    DecodedRaster pr;
    CHECK(decode_raster(Bytes{ps2.data(), ps2.size()}, 0, gs, pr));
    HdKey k;
    CHECK(hd_key(gs, pr.hdr, 0, 1, k));
    const fs::path dds = out / (hd_key_name(k) + ".dds");
    CHECK(fs::is_regular_file(dds));
    HdPack pack;
    CHECK(pack.open(out.string()) && pack.files.size() == 1);
    const std::string* hit = hd_lookup(pack, gs, pr.hdr, 0);
    CHECK(hit && fs::path(*hit).filename() == dds.filename());
    Image im;
    CHECK(load_dds(dds.string(), im) && im.w == 128 && im.h == 128);
    const Image down = xi::box_down(im, 64, 64);   // the PS2 colours at the PS2 size (BC1 and the detail aside)
    double sum = 0;
    for (std::size_t i = 0; i < down.rgba.size(); ++i) sum += std::abs(int(down.rgba[i]) - int(brick.rgba[i]));
    CHECK(sum / double(down.rgba.size()) < 4.0);
    // import.ini
    const xi::Status s = xi::read_status(out);
    CHECK(s.present && s.version == xi::kVersion && s.image == "xbox.iso" && s.written == 1 && s.bytes == r.bytes && s.date > 0);
    // again: the earlier import replaced
    r = xi::run(d / "xbox.iso", d / "game", out, {}, nullptr, o);
    CHECK(r.ok && r.written == 1 && fs::is_regular_file(dds));
    // a disc that is not this game; not an Xbox image; no game files: plain messages, the import left as it was
    write(d / "other.iso", xiso(folder("", {file("default.xbe", bytes("XBEH"))})));
    r = xi::run(d / "other.iso", d / "game", out, {}, nullptr, o);
    CHECK(!r.ok && r.error == "this Xbox disc image is not Burnout 3: Takedown" && fs::is_regular_file(dds));
    r = xi::run(d / "game" / "DATA" / "GLOBAL.TXD", d / "game", out, {}, nullptr, o);
    CHECK(!r.ok && r.error.find("not an Xbox disc image") == 0);
    r = xi::run(d / "xbox.iso", d / "nothing", out, {}, nullptr, o);
    CHECK(!r.ok && r.error.find("the game's files were not found") == 0);
    o.need_bytes = ~0ull >> 1;   // more than any drive has
    r = xi::run(d / "xbox.iso", d / "game", out, {}, nullptr, o);
    CHECK(!r.ok && r.error.find("not enough free space") == 0 && fs::is_regular_file(dds));
    // Remove
    std::string why;
    CHECK(xi::remove(out, &why) && !fs::exists(out) && !xi::read_status(out).present);
    fs::remove_all(d);
}

TEST(damaged_images_claiming_huge_sizes_are_not_allocated) {
    // review: a damaged image's sizes are never allocated (x8 threads): a file over the cap is not read, a texture over
    // 2048 a side is not taken, and an import over them ends with a result (no exception, no crash)
    std::vector<std::uint8_t> big = xtex("Huge", xbox::kDXT1, 12, 12, std::vector<std::uint8_t>(64, 0));   // claims 4096 x 4096
    CHECK(xbox::scan(big.data(), big.size()).empty());
    std::vector<std::uint8_t> cut = xtex("Cut", xbox::kDXT5, 11, 11, std::vector<std::uint8_t>(64, 0));   // 2048: data not in the file
    CHECK(xbox::scan(cut.data(), cut.size()).empty());
    const fs::path d = temp("b3_test_xboximport_huge");
    made_up_disc(d, "xbox.iso");
    // the Xbox static.dat's entry claims 70 MB (the image extended sparsely to hold it, so the claim is in range)
    std::vector<std::uint8_t> img;
    {
        std::ifstream f(d / "xbox.iso", std::ios::binary);
        img.assign(std::istreambuf_iterator<char>(f), {});
    }
    XdvdfsImage im;
    CHECK(im.open(d / "xbox.iso"));
    const XdvdfsImage::Entry* e = im.find("Tracks/US/C1_V1/static.dat");
    CHECK(e != nullptr);
    if (e) {
        // its directory entry: the u32 size after the sector of the entry named static.dat
        const std::string name = "static.dat";
        const auto at = std::search(img.begin(), img.end(), name.begin(), name.end());
        CHECK(at != img.end());
        const std::size_t ent = std::size_t(at - img.begin()) - 14;
        const std::uint32_t claim = 70u << 20;
        put32(img, ent + 8, claim);
        write(d / "huge.iso", img);
        {
            std::fstream f(d / "huge.iso", std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(std::streamoff(e->offset + claim - 1));
            f.put('\0');
        }
        im.close();
        CHECK(im.open(d / "huge.iso") && im.find("Tracks/US/C1_V1/static.dat") && im.find("Tracks/US/C1_V1/static.dat")->size == claim);
        im.close();
        xi::Options o;
        o.need_bytes = 0;
        const xi::Result r = xi::run(d / "huge.iso", d / "game", d / "out" / xi::kFolderName, {}, nullptr, o);
        CHECK(!r.ok && r.error.find("no Xbox texture matched") == 0 && r.skipped[xi::kNoXboxFile] == 2 && r.skipped[xi::kWater] == 1);   // the file was left unread
        o.max_file_bytes = 1024;   // the PS2 file over the cap as well: nothing read, nothing written
        const xi::Result r2 = xi::run(d / "xbox.iso", d / "game", d / "out" / xi::kFolderName, {}, nullptr, o);
        CHECK(!r2.ok && r2.keys == 0);
    }
    fs::remove_all(d);
}

TEST(import_into_a_non_ascii_user_folder_loads) {
    // review (fsrobust's proof): the user data folder holds the user's name; the import writes there and the pack (the
    // game's HdPack, by its UTF-8 path) finds and loads the files
    const fs::path d = temp("b3_test_xboximport_utf8");
    const fs::path user = d / b3io::u8path("Юрий Jürgen 東京") / "b3port" / "native";
    const std::vector<std::uint8_t> ps2 = made_up_disc(d, b3io::u8path("Xbox диск é.iso"));
    xi::Options o;
    o.need_bytes = 0;
    const fs::path out = user / xi::kFolderName;
    const xi::Result r = xi::run(d / b3io::u8path("Xbox диск é.iso"), d / "game", out, {}, nullptr, o);
    CHECK(r.ok && r.written == 1);
    const xi::Status s = xi::read_status(out);
    CHECK(s.present && s.image == "Xbox диск é.iso");
    HdPack pack;
    CHECK(pack.open(b3io::u8string(out)) && pack.files.size() == 1);
    GsMemory gs;
    DecodedRaster pr;
    CHECK(decode_raster(Bytes{ps2.data(), ps2.size()}, 0, gs, pr));
    const std::string* hit = hd_lookup(pack, gs, pr.hdr, 0);
    Image im;
    CHECK(hit && load_dds(*hit, im) && im.w == 128);
    // the hook as the game runs it: hd_folders with the import last, then a decode replaced
    HdTextures hd;
    CHECK(hd.open(hd_folders("", false, {}, b3io::u8string(out))) && hd.pack.files.size() == 1);
    DecodedRaster again;
    CHECK(decode_raster(Bytes{ps2.data(), ps2.size()}, 0, gs, again) && hd.apply(gs, again, 0) && again.hd && again.levels[0].w == 128);
    CHECK(xi::remove(out) && !fs::exists(out));
    fs::remove_all(d);
}

TEST(cut_outs_keep_their_colour_and_alpha_rules) {
    // review: (a) a PS2 texture that is opaque throughout stays opaque (BC1) whatever the Xbox alpha; (b) an Xbox alpha
    // that means something else is not used (the PS2 alpha alone); (c) no dark fringe beside holes whose colour is black
    // (PS2 holes; DXT1 punch-through decodes black)
    const int W = 32, H = 32;
    Image ps2;   // bright yellow-green, opaque left half, a hole (black, alpha 0) in the right half
    ps2.w = W;
    ps2.h = H;
    ps2.rgba.resize(std::size_t(W) * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            std::uint8_t* q = &ps2.rgba[(std::size_t(y) * W + x) * 4];
            const bool hole = x >= W / 2;
            q[0] = hole ? 0 : 200;
            q[1] = hole ? 0 : 220;
            q[2] = hole ? 0 : 60;
            q[3] = hole ? 0 : 255;
        }
    Image xb;   // 2x, darker, the same hole as punch-through black
    xb.w = 2 * W;
    xb.h = 2 * H;
    xb.rgba.resize(std::size_t(xb.w) * xb.h * 4);
    for (int y = 0; y < xb.h; ++y)
        for (int x = 0; x < xb.w; ++x) {
            std::uint8_t* q = &xb.rgba[(std::size_t(y) * xb.w + x) * 4];
            const std::uint8_t* p = &ps2.rgba[(std::size_t(y / 2) * W + x / 2) * 4];
            for (int k = 0; k < 3; ++k) q[k] = std::uint8_t(p[k] * 0.8);
            q[3] = p[3];
        }
    xi::Check c = xi::colour_check(ps2, xb);
    CHECK(c.ok && c.alpha_agrees && !c.ps2_opaque);
    Image out = xi::detail_transfer(ps2, xb, c, false);
    // the last opaque Xbox texels before the hole (x = 31): the PS2 colour, not darkened by the hole's black
    const std::uint8_t* edge = &out.rgba[(std::size_t(10) * xb.w + 31) * 4];
    CHECK(std::abs(int(edge[0]) - 200) <= 3 && std::abs(int(edge[1]) - 220) <= 3 && edge[3] >= 200);
    // without the spreading (the old way) the bilinear blend with black would darken them: the bled picture is not black there
    const Image bled = xi::bleed(ps2, false);
    CHECK(bled.rgba[(std::size_t(10) * W + 20) * 4] == 200 && bled.rgba[(std::size_t(10) * W + 20) * 4 + 3] == 0);
    // (a) an opaque PS2 texture: opaque output whatever the Xbox alpha says
    Image opaque = ps2;
    for (std::size_t i = 0; i < opaque.rgba.size(); i += 4) { opaque.rgba[i] = 120; opaque.rgba[i + 1] = 90; opaque.rgba[i + 2] = 70; opaque.rgba[i + 3] = 255; }
    Image xa = xb;
    for (int y = 0; y < xa.h; ++y)
        for (int x = 0; x < xa.w; ++x) {
            std::uint8_t* q = &xa.rgba[(std::size_t(y) * xa.w + x) * 4];
            q[0] = 96; q[1] = 72; q[2] = 56;
            q[3] = (x / 4 + y / 4) % 2 ? 255 : 160;   // an alpha of its own
        }
    c = xi::colour_check(opaque, xa);
    CHECK(c.ok && c.ps2_opaque);
    out = xi::detail_transfer(opaque, xa, c, true);
    bool all_opaque = true;
    for (std::size_t i = 3; i < out.rgba.size(); i += 4) all_opaque = all_opaque && out.rgba[i] == 255;
    int fmt = 0;
    xi::encode_dds(out, &fmt);
    CHECK(all_opaque && fmt == kBC1);
    // (b) the Xbox alpha disagrees (a mask of its own over the PS2 cut-out): the up-scaled PS2 alpha alone
    Image xm = xb;
    for (std::size_t i = 3; i < xm.rgba.size(); i += 4) xm.rgba[i] = 255;   // opaque where the PS2 one has its hole
    c = xi::colour_check(ps2, xm);
    CHECK(!c.alpha_agrees && !c.ps2_opaque);
    out = xi::detail_transfer(ps2, xm, c, false);
    const Image up = xi::bilinear(xi::bleed(ps2, false), xb.w, xb.h, false);
    bool same_alpha = true;
    for (std::size_t i = 3; i < out.rgba.size(); i += 4) same_alpha = same_alpha && out.rgba[i] == up.rgba[i];
    CHECK(same_alpha);
}

TEST(an_exception_in_the_import_is_a_result) {
    // review: out of memory (or any exception) on a worker thread or on the caller's never reaches std::terminate: a
    // failed result, the .partial folder removed (here thrown by the progress callback, which the workers call)
    const fs::path d = temp("b3_test_xboximport_throw");
    made_up_disc(d, "xbox.iso");
    xi::Options o;
    o.need_bytes = 0;
    o.threads = 4;
    const fs::path out = d / "user" / xi::kFolderName;
    fs::path partial = out;
    partial += ".partial";
    xi::Result r = xi::run(d / "xbox.iso", d / "game", out, [](const xi::Progress& p) { if (p.done >= 1) throw std::bad_alloc(); }, nullptr, o);
    CHECK(!r.ok && !r.cancelled && r.error == "out of memory while reading the disc image (a damaged image?)");
    CHECK(!fs::exists(out) && !fs::exists(partial));
    r = xi::run(d / "xbox.iso", d / "game", out, [](const xi::Progress&) { throw std::runtime_error("disk gone"); }, nullptr, o);   // (the first call: this thread)
    CHECK(!r.ok && r.error == "the import stopped: disk gone" && !fs::exists(partial));
    fs::remove_all(d);
}

TEST(folders_are_removed_through_the_given_remover) {
    // modsec: the .partial / .old folders and Remove go through Options::remove_tree (the launcher: b3mod::remove_tree);
    // a link inside the import folder is removed as the link, never the folder it points to
    const fs::path d = temp("b3_test_xboximport_rm");
    made_up_disc(d, "xbox.iso");
    std::vector<fs::path> seen;
    xi::Options o;
    o.need_bytes = 0;
    o.remove_tree = [&](const fs::path& p) { seen.push_back(p); return xi::remove_tree_plain(p); };
    const fs::path out = d / "user" / xi::kFolderName;
    std::atomic<bool> cancel{true};
    xi::run(d / "xbox.iso", d / "game", out, {}, &cancel, o);
    CHECK(!seen.empty() && seen.back().filename() == "xbox-textures.partial");
    seen.clear();
    CHECK(xi::run(d / "xbox.iso", d / "game", out, {}, nullptr, o).ok);
    CHECK(std::any_of(seen.begin(), seen.end(), [](const fs::path& p) { return p.filename() == "xbox-textures.old"; }));
    fs::create_directories(d / "keep");
    std::ofstream(d / "keep" / "precious.txt") << "x";
    std::error_code ec;
    fs::create_directory_symlink(d / "keep", out / "link", ec);
    seen.clear();
    std::string why;
    CHECK(xi::remove(out, &why, o.remove_tree) && !fs::exists(out) && seen.size() == 2);
    CHECK(fs::exists(d / "keep" / "precious.txt"));   // (the link went, not what it pointed to)
    fs::remove_all(d);
}

TEST(hd_folders_put_the_import_last) {
    const auto f = hd_folders("/player", false, {"/modA", "/modB"}, "/xbox");
    CHECK(f.size() == 4 && f[0] == "/modA" && f[2] == "/player" && f[3] == "/xbox");
    CHECK(hd_folders("/player", true, {"/modA"}, "/xbox").size() == 1);   // HD TEXTURES off: the player's packs both off
    const auto g = hd_folders("", false, {}, "/xbox");
    CHECK(g.size() == 1 && g[0] == "/xbox");
}

TEST_MAIN("xboximport")
