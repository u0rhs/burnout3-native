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
#include "synthetic_car.h"
#include "test.h"

#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

using namespace b3modtool;
using b3asset::Bytes;
using b3asset::GsMemory;
using namespace synth;

namespace {

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

// carmods1: a car file of version 0x15 / 0x16 (players' car mods from earlier builds; the game's loader never reads the
// word) reads as the same file at 0x17; other versions are refused by the readers, saying which
TEST(older_vehicle_versions_read) {
    const Buf f17 = synthetic_car();
    GsMemory gs;
    b3asset::Model m17;
    std::string why;
    CHECK(b3asset::read_car(Bytes{f17.data(), f17.size()}, {}, gs, m17, &why));
    for (std::uint32_t v : {0x15u, 0x16u}) {
        Buf f = f17;
        p32(f, 0, v);
        b3asset::Model m;
        why.clear();
        CHECK(b3asset::read_car(Bytes{f.data(), f.size()}, {}, gs, m, &why));
        CHECK(m.vertices.size() == m17.vertices.size() && m.indices == m17.indices && m.stats["version"] == int(v));
        CarFile car;
        CHECK(car_read(f, car, &why) && car.meshes.size() == 1);
        CHECK(!b3asset::car_atlas_triangles(Bytes{f.data(), f.size()}).empty());
    }
    for (std::uint32_t v : {0x14u, 0x18u, 0u}) {
        Buf f = f17;
        p32(f, 0, v);
        b3asset::Model m;
        why.clear();
        CHECK(!b3asset::read_car(Bytes{f.data(), f.size()}, {}, gs, m, &why) && why.find("not a vehicle file") != std::string::npos);
        CarFile car;
        why.clear();
        CHECK(!car_read(f, car, &why) && why.find("not a vehicle file") != std::string::npos);
        b3asset::CarPaintMap pm;
        why.clear();
        CHECK(!b3asset::car_paint_map(Bytes{f.data(), f.size()}, pm, &why) && why.find("not a vehicle file") != std::string::npos);
    }
}

// carmods1: the June-prototype disc's own 0x15 files have an older layout (the first LOD block at 0x16E0, 16 bytes
// before the retail 0x16F0): every reader refuses any first LOD block below 0x16F0, naming it; the byte layout decides,
// not the version word (a players' 0x15 mod in the retail layout reads, above)
TEST(prototype_layout_refused) {
    const Buf f17 = synthetic_car();
    CHECK(!b3asset::car_old_layout(Bytes{f17.data(), f17.size()}));
    for (std::uint32_t v : {0x15u, 0x17u}) {
        Buf f = f17;
        p32(f, 0, v);
        p32(f, 0x4C + 4 * 3, 0x16E0);   // the LOD block 16 bytes earlier, as the prototype disc's files have it
        CHECK(b3asset::car_old_layout(Bytes{f.data(), f.size()}));
        CHECK(b3asset::car_file_error(Bytes{f.data(), f.size()}).find("at 0x16E0") != std::string::npos);
        GsMemory gs;
        b3asset::Model m;
        std::string why;
        CHECK(!b3asset::read_car(Bytes{f.data(), f.size()}, {}, gs, m, &why) && why.find("prototype-disc car file") != std::string::npos);
        CarFile car;
        why.clear();
        CHECK(!car_read(f, car, &why) && why.find("prototype-disc car file") != std::string::npos);
        b3asset::CarPaintMap pm;
        why.clear();
        CHECK(!b3asset::car_paint_map(Bytes{f.data(), f.size()}, pm, &why) && why.find("prototype-disc car file") != std::string::npos);
    }
}

TEST(first_lod_below_the_retail_offset_refused) {   // not only 0x16E0: anything non-zero below 0x16F0
    const Buf f17 = synthetic_car();
    for (std::uint32_t at : {0x100u, 0x1000u, 0x16E0u, 0x16EFu}) {
        Buf f = f17;
        p32(f, 0x4C + 4 * 3, at);
        CHECK(b3asset::car_old_layout(Bytes{f.data(), f.size()}));
        char want[16];
        std::snprintf(want, sizeof want, "0x%X", at);
        CHECK(b3asset::car_file_error(Bytes{f.data(), f.size()}).find(want) != std::string::npos);
    }
    Buf f = f17;
    p32(f, 0x4C + 4 * 3, 0x2000);   // later than the retail offset: the readers check it against the file as before
    CHECK(!b3asset::car_old_layout(Bytes{f.data(), f.size()}));
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
