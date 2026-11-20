// test_car_truncation -- every car reader on a car file cut short at every length (modsec: a modded car file reaches the
// Showroom, the renderer and the mod tool). A synthetic car (the header, one LOD block with one L0 entry whose packet
// unpacks three vertices, as test_modtool's car_read_and_export_synthetic; and a 16 x 16 PSMT8 texture with two
// palettes, as test_b3asset's raster_replays_upload_chain_with_csm1_palette) is cut at every length from 0 to its size
// and given to read_car (with and without the texture), car_paint_map (the Showroom's custom paint) and
// car_atlas_triangles, and to the mod tool's car_read, car_export and, for each cut its export accepted, car_import of
// that export. This binary is built with AddressSanitizer (Makefile): any read past the bytes given is a failure. The
// b3asset readers also get their bytes at a page's end before an unreadable page. No game data.
#include "b3asset/car.h"
#include "b3asset/carpaint.h"
#include "b3asset/gs_memory.h"
#include "b3asset/raster.h"
#include "b3modtool/export3d.h"
#include "test.h"

#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

using namespace b3modtool;
using b3asset::Bytes;
using b3asset::GsMemory;

namespace {

void p32(Buf& b, std::size_t at, std::uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4);
    std::memcpy(&b[at], &v, 4);
}
void p64(Buf& b, std::size_t at, std::uint64_t v) {
    if (b.size() < at + 8) b.resize(at + 8);
    std::memcpy(&b[at], &v, 8);
}
std::uint32_t fbits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
void qw(Buf& v, std::uint64_t lo, std::uint64_t hi) {
    std::uint8_t b[16];
    std::memcpy(b, &lo, 8);
    std::memcpy(b + 8, &hi, 8);
    v.insert(v.end(), b, b + 16);
}
void gif_upload(Buf& v, std::uint32_t rrw, std::uint32_t rrh, const Buf& bytes) {
    qw(v, 3 | (1ull << 60), 0xE);   // NLOOP 3, PACKED, NREG 1 (A+D)
    qw(v, 0, 0x51);
    qw(v, rrw | std::uint64_t(rrh) << 32, 0x52);
    qw(v, 0, 0x53);
    const std::size_t nq = (bytes.size() + 15) / 16;
    qw(v, nq | (2ull << 58), 0);   // IMAGE
    Buf pad = bytes;
    pad.resize(nq * 16, 0);
    v.insert(v.end(), pad.begin(), pad.end());
}

// the texture: a raster header, its upload chain, the pixels and a 256-entry CLUT; two palettes (the same CLUT block)
Buf synthetic_raster() {
    Buf f(0x200, 0);
    const std::uint32_t w = 16, h = 16;
    Buf pix_bytes(w * h), clut(256 * 4);
    for (std::size_t i = 0; i < pix_bytes.size(); ++i) pix_bytes[i] = std::uint8_t(i);
    for (std::size_t i = 0; i < clut.size(); ++i) clut[i] = std::uint8_t(i * 7);
    p32(f, 0x0C, w);
    p32(f, 0x10, h);
    p32(f, 0x14, 8);
    f[0x20] = 4;
    f[0x23] = 0x25;
    p64(f, 0x3C, (std::uint64_t(2) << 14) | (std::uint64_t(b3asset::PSMT8) << 20) | (4ull << 26) | (4ull << 30) | (1ull << 34) | (1ull << 61));
    p32(f, 0x44, 4);
    f[0xA0] = 2;   // two palettes (paint 1's CBP add at +0x92: 0, the same block)
    std::memcpy(&f[0xA8], "carpaint", 8);
    p32(f, 0x24, 0x100);
    p32(f, 0x100, 2);
    p32(f, 0x108, 2);
    Buf pix, pal;
    gif_upload(pix, w / 2, h / 2, pix_bytes);
    gif_upload(pal, 16, 16, clut);
    const std::size_t pix_at = 0x200, pal_at = pix_at + pix.size();
    f.resize(pal_at + pal.size());
    std::memcpy(&f[pix_at], pix.data(), pix.size());
    std::memcpy(&f[pal_at], pal.data(), pal.size());
    auto chain = [&](std::size_t e, std::uint64_t bitblt, std::size_t src, std::size_t q) {
        p32(f, e, 0x10000002);
        p32(f, e + 12, 0x50000002);
        p64(f, e + 16, 1 | (1ull << 60));
        p64(f, e + 24, 0xE);
        p64(f, e + 32, bitblt);
        p64(f, e + 40, 0x50);
        p32(f, e + 48, 0x30000000u | std::uint32_t(q));
        p32(f, e + 52, std::uint32_t(src));
        p32(f, e + 60, 0x50000000u | std::uint32_t(q));
    };
    chain(0x110, std::uint64_t(1) << 48 | std::uint64_t(b3asset::PSMCT32) << 56, pix_at, pix.size() / 16);
    chain(0x150, std::uint64_t(4) << 32 | std::uint64_t(1) << 48 | std::uint64_t(b3asset::PSMCT32) << 56, pal_at, pal.size() / 16);
    return f;
}

// the car (test_modtool's synthetic car) with the texture at 0x1900
Buf synthetic_car() {
    Buf f(0x1900, 0);
    p32(f, 0x00, 0x17);
    p32(f, 0x10, 3);
    p32(f, 0x4C + 4 * 3, 0x16F0);   // LOD 3
    p32(f, 0x16F0 + 0, 0x48);       // slot L0 -> list at 0x1738
    const std::size_t lst = 0x1738;
    f[lst] = 1;
    f[lst + 1] = 0xFF;
    p32(f, lst + 4, 0x10);
    p32(f, lst + 8, 0x47800080);
    const std::size_t e = 0x1748;
    p32(f, e + 0, 0x10);
    p32(f, e + 8, 0x00640001);
    p32(f, e + 12, 0x8080);
    std::vector<std::uint32_t> w = {0, 0, 0x30000000};
    for (int i = 0; i < 4; ++i) w.push_back(0x47800000);
    w.push_back(0x01000103);
    w.push_back(0x6C030000);
    const float P[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0.5f}};
    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 3; ++c) w.push_back(fbits(P[i][c]));
        w.push_back(i == 0 ? 0x8000 : 0);
    }
    w.push_back(0x6D034001);
    const std::uint16_t U[3][4] = {{4096, 0, 2048, 4096}, {0x0104, 0x0208, 0x030C, 0}, {0, 4096, 0, 0}};
    for (int i = 0; i < 3; ++i) {
        w.push_back(U[i][0] | std::uint32_t(U[i][1]) << 16);
        w.push_back(U[i][2] | std::uint32_t(U[i][3]) << 16);
    }
    w.push_back(0x05000001);
    w.push_back(0x6A034002);
    const std::uint8_t N[12] = {128, 255, 128, 128, 128, 255, 0, 128, 128, 0, 0, 0};
    for (int i = 0; i < 3; ++i)
        w.push_back(std::uint32_t(N[4 * i]) | std::uint32_t(N[4 * i + 1]) << 8 | std::uint32_t(N[4 * i + 2]) << 16 | std::uint32_t(N[4 * i + 3]) << 24);
    w.push_back(0x04000003);
    while ((w.size() * 4 + 8) % 16) w.push_back(0);
    const std::size_t at = 0x1758;
    p32(f, at, 0x60000000u | std::uint32_t((w.size() * 4 - 8) / 16));
    for (std::size_t i = 0; i < w.size(); ++i) p32(f, at + 8 + 4 * i, w[i]);
    const Buf tex = synthetic_raster();
    p32(f, 0x60, 0x1900);
    f.resize(0x1900);
    f.insert(f.end(), tex.begin(), tex.end());
    p32(f, 0x08, std::uint32_t(f.size()));
    return f;
}

// the bytes at the very end of a readable page, an unreadable page after them
struct GuardedBytes {
    std::uint8_t* base = nullptr;
    std::size_t map = 0;
    Bytes b;
    GuardedBytes(const std::uint8_t* p, std::size_t n) {
        const std::size_t pg = std::size_t(sysconf(_SC_PAGESIZE));
        const std::size_t data = (n + pg - 1) / pg * pg + pg;
        map = data + pg;
        base = static_cast<std::uint8_t*>(mmap(nullptr, map, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        mprotect(base + data, pg, PROT_NONE);
        std::uint8_t* at = base + data - n;
        if (n) std::memcpy(at, p, n);
        b = Bytes{at, n};
    }
    ~GuardedBytes() { munmap(base, map); }
};

}  // namespace

TEST(the_whole_synthetic_car_reads) {
    const Buf f = synthetic_car();
    GsMemory gs;
    b3asset::Model m;
    std::string why;
    CHECK(b3asset::read_car(Bytes{f.data(), f.size()}, {}, gs, m, &why));
    if (!why.empty()) std::printf("    (%s)\n", why.c_str());
    CHECK(m.textures.size() == 1 && m.stats["palettes"] == 2 && m.vertices.size() >= 3);
    CarFile car;
    CHECK(car_read(f, car, &why) && car.meshes.size() == 1);
    FileSet out;
    CarExportOptions o;
    o.source = "PVEH/TEST/CAR1.BGV";
    CHECK(car_export(f, "T", o, out, nullptr, &why) && out.count("T.gltf") && out.count("T.b3car.json"));
    Buf back;
    const FileLoader load = [&out](const std::string& p, Buf& b) {
        const auto it = out.find(p);
        if (it == out.end()) return false;
        b = it->second;
        return true;
    };
    // (imported; the byte-for-byte round trip is test_modtool's: this file's layout, the texture after the meshes, is
    // laid out again by the writer)
    const bool imported = car_import(f, load, "T.gltf", "T.b3car.json", {}, back, nullptr, &why);
    if (!imported) std::printf("    (import: %s)\n", why.c_str());
    CHECK(imported && !back.empty());
    b3asset::CarPaintMap pm;
    (void)b3asset::car_paint_map(Bytes{f.data(), f.size()}, pm, &why);   // (two equal palettes: refused, after decoding both)
    CHECK(!b3asset::car_atlas_triangles(Bytes{f.data(), f.size()}).empty());
}

TEST(every_reader_on_every_cut) {
    const Buf full = synthetic_car();
    std::size_t read_ok = 0, export_ok = 0, import_ok = 0;
    for (std::size_t n = 0; n <= full.size(); ++n) {
        const Buf f(full.begin(), full.begin() + std::ptrdiff_t(n));
        {
            GuardedBytes g(full.data(), n);
            for (const bool tex : {true, false}) {
                GsMemory gs;
                b3asset::Model m;
                b3asset::CarOptions co;
                co.texture = tex;
                read_ok += b3asset::read_car(g.b, co, gs, m, nullptr) ? 1 : 0;
            }
            b3asset::CarPaintMap pm;
            (void)b3asset::car_paint_map(g.b, pm, nullptr);
            (void)b3asset::car_atlas_triangles(g.b);
        }
        CarFile car;
        (void)car_read(f, car, nullptr);
        FileSet out;
        CarExportOptions o;
        o.source = "PVEH/TEST/CAR1.BGV";
        if (!car_export(f, "T", o, out, nullptr, nullptr)) continue;
        ++export_ok;
        const FileLoader load = [&out](const std::string& p, Buf& b) {
            const auto it = out.find(p);
            if (it == out.end()) return false;
            b = it->second;
            return true;
        };
        Buf back;
        import_ok += car_import(f, load, "T.gltf", "T.b3car.json", {}, back, nullptr, nullptr) ? 1 : 0;
    }
    std::printf("    %zu cuts: read_car accepted %zu, export %zu, import %zu\n", full.size() + 1, read_ok, export_ok, import_ok);
    CHECK(read_ok >= 1 && export_ok >= 1);   // (the whole file at least; every other cut only must not read past its end)
}

TEST_MAIN("test_car_truncation: every car reader on a car file cut at every length (AddressSanitizer)")
