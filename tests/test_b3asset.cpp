// b3asset decoders (docs/native-renderer.md §10): GS memory addressing, raster upload replay and CLUTs, the VIF
// interpreter, strips, PNG; plus real-data checks against the game install when B3_GAME_DIR is defined (Makefile GAME).
#include <cstring>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <filesystem>

#include "b3asset/coronashade.h"
#include "b3asset/car.h"
#include "b3asset/crash.h"
#include "b3asset/enviro.h"
#include "b3asset/export.h"
#include "b3asset/gs_memory.h"
#include "b3asset/model.h"
#include "b3asset/png.h"
#include "b3asset/postfx.h"
#include "b3asset/raster.h"
#include "b3asset/sunflare.h"
#include "b3asset/track.h"
#include "b3asset/trackshade.h"
#include "b3asset/vif.h"
#include "b3asset/vu1.h"
#include "test.h"

using namespace b3asset;

namespace {

// librw's swizzle() (src/ps2/ps2raster.cpp, MIT): the position of PSMT8/PSMT4 pixel (x, y) inside the image RW uploads
// as PSMCT32/PSMCT16 at half width and height. An independent derivation of the same GS layouts.
std::uint32_t librw_swizzle(std::uint32_t x, std::uint32_t y, std::uint32_t logw) {
    auto X = [&](int n) { return (x >> n) & 1; };
    auto Y = [&](int n) { return (y >> n) & 1; };
    x ^= (Y(1) ^ Y(2)) << 2;
    const std::uint32_t nx = (x & 7) | ((x >> 1) & ~7u);
    const std::uint32_t ny = (y & 1) | ((y >> 1) & ~1u);
    const std::uint32_t n = Y(1) | X(3) << 1;
    return n | nx << 2 | ny << (logw - 1 + 2);
}

std::uint64_t ad(std::uint64_t data) { return data; }

// Appends a quadword (two u64) to a byte vector.
void qw(std::vector<std::uint8_t>& v, std::uint64_t lo, std::uint64_t hi) {
    std::uint8_t b[16];
    std::memcpy(b, &lo, 8);
    std::memcpy(b + 8, &hi, 8);
    v.insert(v.end(), b, b + 16);
}

// A GIF packet uploading `bytes` with TRXPOS/TRXREG under the BITBLTBUF set elsewhere.
void gif_upload(std::vector<std::uint8_t>& v, std::uint32_t dsax, std::uint32_t dsay, std::uint32_t rrw, std::uint32_t rrh,
                const std::vector<std::uint8_t>& bytes) {
    qw(v, 3 | (1ull << 60), 0xE);  // NLOOP 3, PACKED, NREG 1 (A+D)
    qw(v, ad(std::uint64_t(dsax) << 32 | std::uint64_t(dsay) << 48), 0x51);
    qw(v, ad(rrw | std::uint64_t(rrh) << 32), 0x52);
    qw(v, 0, 0x53);
    const std::size_t nq = (bytes.size() + 15) / 16;
    qw(v, nq | (2ull << 58), 0);  // IMAGE
    std::vector<std::uint8_t> pad = bytes;
    pad.resize(nq * 16, 0);
    v.insert(v.end(), pad.begin(), pad.end());
}

std::uint32_t fnv(const std::vector<std::uint8_t>& v) {
    std::uint32_t h = 2166136261u;
    for (std::uint8_t c : v) h = (h ^ c) * 16777619u;
    return h;
}

}  // namespace

TEST(gs_addresses_are_a_bijection_per_page) {
    for (int psm : {PSMCT32, PSMCT16, PSMCT16S, PSMT8, PSMT4}) {
        const int pw = psm == PSMCT32 ? 64 : (psm == PSMT8 || psm == PSMT4) ? 128 : 64;
        const int ph = psm == PSMCT32 ? 32 : psm == PSMT8 ? 64 : psm == PSMT4 ? 128 : 64;
        const int bits = psm_bits(psm);
        std::vector<int> seen(8192 * 8 / bits, 0);
        bool in_page = true;
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x) {
                std::uint32_t a = GsMemory::address(psm, 0, psm == PSMT8 || psm == PSMT4 ? 2 : 1, x, y);
                if (bits == 4) { /* nibble address */ } else a /= bits / 8;
                if (a >= seen.size()) { in_page = false; continue; }
                ++seen[a];
            }
        CHECK(in_page);
        bool once = true;
        for (int s : seen) once &= s == 1;
        CHECK(once);
    }
}

TEST(gs_t8_from_ct32_matches_librw_swizzle) {
    for (std::uint32_t w : {128u, 256u})
        for (std::uint32_t h : {64u, 128u}) {
            std::uint32_t logw = 0;
            while ((1u << logw) < w) ++logw;
            std::vector<std::uint8_t> up(w * h);
            for (std::size_t i = 0; i < up.size(); ++i) up[i] = std::uint8_t(i * 7 + (i >> 8) * 13);
            GsMemory gs;
            const std::uint32_t tbw = w / 64;
            gs.set_bitbltbuf(std::uint64_t(tbw / 2) << 48 | std::uint64_t(PSMCT32) << 56);
            gs.set_trxpos(0);
            gs.set_trxreg((w / 2) | std::uint64_t(h / 2) << 32);
            gs.start_transfer();
            CHECK(gs.transfer(up.data(), up.size()) == up.size());
            const std::uint32_t mask = (1u << (logw + 2)) - 1;
            int bad = 0;
            for (std::uint32_t y = 0; y < h; ++y)
                for (std::uint32_t x = 0; x < w; ++x) {
                    // librw unswizzles in groups of 4 rows: the source offset is (y & ~3) * w + swizzle & mask.
                    const std::uint32_t s = (y & ~3u) * w + (librw_swizzle(x, y, logw) & mask);
                    bad += gs.read(PSMT8, 0, tbw, x, y) != up[s];
                }
            CHECK(bad == 0);
        }
}

TEST(gs_t4_from_ct16_matches_librw_swizzle) {
    for (std::uint32_t w : {128u, 256u})
        for (std::uint32_t h : {128u, 256u}) {
            std::uint32_t logw = 0;
            while ((1u << logw) < w) ++logw;
            std::vector<std::uint8_t> up(w * h / 2);
            for (std::size_t i = 0; i < up.size(); ++i) up[i] = std::uint8_t(i * 29 + (i >> 9) * 5);
            GsMemory gs;
            const std::uint32_t tbw = w / 64;
            gs.set_bitbltbuf(std::uint64_t(tbw / 2) << 48 | std::uint64_t(PSMCT16) << 56);
            gs.set_trxpos(0);
            gs.set_trxreg((w / 2) | std::uint64_t(h / 2) << 32);
            gs.start_transfer();
            CHECK(gs.transfer(up.data(), up.size()) == up.size());
            const std::uint32_t mask = (1u << (logw + 2)) - 1;
            int bad = 0;
            for (std::uint32_t y = 0; y < h; ++y)
                for (std::uint32_t x = 0; x < w; ++x) {
                    const std::uint32_t s = (y & ~3u) * w + (librw_swizzle(x, y, logw) & mask);  // nibble index
                    const std::uint32_t want = (up[s >> 1] >> ((s & 1) * 4)) & 0xF;
                    bad += gs.read(PSMT4, 0, tbw, x, y) != want;
                }
            CHECK(bad == 0);
        }
}

TEST(gs_transfer_ct24_and_t4_nibble_order) {
    GsMemory gs;
    gs.set_bitbltbuf(std::uint64_t(1) << 48 | std::uint64_t(PSMCT24) << 56);
    gs.set_trxpos(0);
    gs.set_trxreg(2 | 1ull << 32);
    gs.start_transfer();
    const std::uint8_t px[6] = {1, 2, 3, 4, 5, 6};
    CHECK(gs.transfer(px, 6) == 6);
    CHECK(gs.read(PSMCT24, 0, 1, 0, 0) == 0x030201u);
    CHECK(gs.read(PSMCT24, 0, 1, 1, 0) == 0x060504u);
    gs.clear();
    gs.set_bitbltbuf(std::uint64_t(2) << 48 | std::uint64_t(PSMT4) << 56);
    gs.set_trxreg(2 | 1ull << 32);
    gs.start_transfer();
    const std::uint8_t b = 0xA5;
    gs.transfer(&b, 1);
    CHECK(gs.read(PSMT4, 0, 2, 0, 0) == 0x5);  // low nibble first
    CHECK(gs.read(PSMT4, 0, 2, 1, 0) == 0xA);
}

TEST(raster_replays_upload_chain_with_csm1_palette) {
    // A 16x16 PSMT8 raster uploaded as CT32 8x8 (RW's swizzled form) + a 256-entry CT32 CLUT (16x16, CSM1) at block 4.
    std::vector<std::uint8_t> f(0x200, 0);
    const std::uint32_t w = 16, h = 16;
    std::vector<std::uint8_t> idx(w * h);
    for (std::uint32_t i = 0; i < idx.size(); ++i) idx[i] = std::uint8_t(i);  // index = linear position
    // The swizzled upload: probe which upload byte each T8 pixel reads, then store that pixel's index there.
    std::vector<std::uint8_t> up(w * h);
    {
        GsMemory probe;
        probe.set_bitbltbuf(std::uint64_t(1) << 48 | std::uint64_t(PSMCT32) << 56);
        probe.set_trxreg((w / 2) | std::uint64_t(h / 2) << 32);
        probe.start_transfer();
        std::vector<std::uint8_t> pos(up.size());
        for (std::size_t i = 0; i < pos.size(); ++i) pos[i] = std::uint8_t(i);  // byte i of the upload
        probe.transfer(pos.data(), pos.size());
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x) up[probe.read(PSMT8, 0, 2, x, y)] = idx[y * w + x];
    }
    std::vector<std::uint8_t> clut(256 * 4);
    for (int i = 0; i < 256; ++i) {
        // CLUT image row-major 16x16; entry e lives at (j & 15, j >> 4) with j = e with bits 3/4 swapped.
        const int j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
        const std::uint32_t c = std::uint32_t(i) | std::uint32_t(255 - i) << 8 | 0x40u << 16 | std::uint32_t(i < 128 ? 0x80 : 0x40) << 24;
        std::memcpy(&clut[std::size_t((j >> 4) * 16 + (j & 15)) * 4], &c, 4);
    }
    // Header.
    auto put32 = [&](std::size_t o, std::uint32_t v) { if (f.size() < o + 4) f.resize(o + 4); std::memcpy(&f[o], &v, 4); };
    auto put64 = [&](std::size_t o, std::uint64_t v) { if (f.size() < o + 8) f.resize(o + 8); std::memcpy(&f[o], &v, 8); };
    put32(0x0C, w); put32(0x10, h); put32(0x14, 8);
    f[0x20] = 4; f[0x23] = 0x25;  // texture, PAL8 | 8888
    const std::uint64_t tex0 = (std::uint64_t(2) << 14) | (std::uint64_t(PSMT8) << 20) | (4ull << 26) | (4ull << 30) |
                               (1ull << 34) | (1ull << 61);
    put64(0x3C, tex0);
    put32(0x44, 4);  // palette at block 4
    std::memcpy(&f[0xA8], "synthetic", 9);
    // Upload block at 0x100: two transfers.
    put32(0x24, 0x100);
    put32(0x100, 2);
    put32(0x108, 2);
    std::vector<std::uint8_t> pix, pal;
    gif_upload(pix, 0, 0, w / 2, h / 2, up);
    gif_upload(pal, 0, 0, 16, 16, clut);
    const std::size_t pix_at = 0x200, pal_at = pix_at + pix.size();
    f.resize(pal_at + pal.size());
    std::memcpy(&f[pix_at], pix.data(), pix.size());
    std::memcpy(&f[pal_at], pal.data(), pal.size());
    auto chain = [&](std::size_t e, std::uint64_t bitblt, std::size_t src, std::size_t qwc) {
        put32(e, 0x10000002); put32(e + 12, 0x50000002);
        put64(e + 16, 1 | (1ull << 60)); put64(e + 24, 0xE);
        put64(e + 32, bitblt); put64(e + 40, 0x50);
        put32(e + 48, 0x30000000u | std::uint32_t(qwc)); put32(e + 52, std::uint32_t(src)); put32(e + 60, 0x50000000u | std::uint32_t(qwc));
    };
    chain(0x110, std::uint64_t(1) << 48 | std::uint64_t(PSMCT32) << 56, pix_at, pix.size() / 16);
    chain(0x150, std::uint64_t(4) << 32 | std::uint64_t(1) << 48 | std::uint64_t(PSMCT32) << 56, pal_at, pal.size() / 16);
    GsMemory gs;
    DecodedRaster r;
    std::string why;
    CHECK(decode_raster(Bytes{f.data(), f.size()}, 0, gs, r, 0, &why));
    CHECK(r.hdr.name == "synthetic");
    CHECK(r.levels.size() == 1);
    int bad = 0;
    for (std::uint32_t i = 0; i < w * h; ++i) {
        const std::uint8_t* p = &r.levels[0].rgba[i * 4];
        const std::uint8_t e = idx[i];
        bad += p[0] != e || p[1] != 255 - e || p[2] != 0x40 || p[3] != (e < 128 ? 0xFF : 0x80);
    }
    CHECK(bad == 0);
    CHECK(r.translucent);
    CHECK(r.max_alpha == 0x80);
}

TEST(color_conversion_alpha_scale) {
    CHECK_BITS(gs_color_to_rgba8(PSMCT32, 0x80112233u, true), 0xFF112233u);
    CHECK_BITS(gs_color_to_rgba8(PSMCT32, 0x40112233u, true), 0x80112233u);
    CHECK_BITS(gs_color_to_rgba8(PSMCT32, 0xFF112233u, true), 0xFF112233u);  // > 0x80 clamps
    CHECK_BITS(gs_color_to_rgba8(PSMCT32, 0x00112233u, false), 0xFF112233u);  // TCC 0: RGB only
    CHECK_BITS(gs_color_to_rgba8(PSMCT16, 0x801F, true), 0xFF0000FFu);
}

TEST(vif_skipping_write_offset_mode_and_kicks) {
    // STROW 65536.0, STCYCL CL 3 WL 1, STMOD 1, UNPACK V3-8 unsigned x2 at 2, ITOP 2, MSCNT.
    std::vector<std::uint32_t> s = {
        0x30000000, 0x47800000, 0x47800000, 0x47800000, 0x47800000,
        0x01000103, 0x05000001,
        0x6A02C002, 0x0302FF80, 0x00000001,  // bytes 80 FF 02 03 | 01 00 00 00 (6 bytes used, padded)
        0x05000000, 0x6C018000, 0x3F800000, 0x40000000, 0x40400000, 0x00008000,  // V4-32 x1 at 0
        0x04000002, 0x17000000,
    };
    VifInterpreter vif;
    int kicks = 0;
    std::uint32_t lanes[2][3] = {};
    float pos[3] = {};
    std::uint32_t w = 0, itop = 0;
    std::string why;
    CHECK(vif.run(reinterpret_cast<const std::uint8_t*>(s.data()), s.size() * 4,
                  [&](const VuMemory& vu, std::uint32_t it, VifInterpreter::KickKind kind, std::uint32_t) {
                      ++kicks;
                      itop = it;
                      CHECK(kind == VifInterpreter::kMscnt);
                      for (int v = 0; v < 2; ++v) for (int c = 0; c < 3; ++c) lanes[v][c] = vu.qw(2 + 3 * v)[c];
                      for (int c = 0; c < 3; ++c) pos[c] = vu.f(0, c);
                      w = vu.qw(0)[3];
                  }, &why));
    CHECK(kicks == 1);
    CHECK(itop == 2);
    CHECK_BITS(lanes[0][0], 0x47800080u);
    CHECK_BITS(lanes[0][1], 0x478000FFu);
    CHECK_BITS(lanes[0][2], 0x47800002u);
    CHECK_BITS(lanes[1][0], 0x47800003u);
    CHECK_BITS(lanes[1][1], 0x47800001u);
    CHECK_NEAR(pos[0], 1.0, 0);
    CHECK_NEAR(pos[2], 3.0, 0);
    CHECK_BITS(w, 0x8000u);
    CHECK(vif.stats.unpacks["V3-8"] == 1 && vif.stats.unpacks["V4-32"] == 1);
}

TEST(vif_v4_16_sign_extension_and_mask) {
    // V4-16 signed x1 at 0 (CL=WL=1): 0xFFFF -> -1; then masked V4-8 with lane 3 = ROW, lane 2 write-protected.
    std::vector<std::uint32_t> s = {
        0x01000101,
        0x6D010000, 0x0002FFFF, 0x7FFF8000,
        0x30000000, 1, 2, 3, 0x1234,
        0x20000000, (1u << 6) | (3u << 4),  // mask row 0: lane 3 -> ROW (01), lane 2 -> protect (11)
        0x7E010001, 0x44332211,
    };
    VifInterpreter vif;
    CHECK(vif.run(reinterpret_cast<const std::uint8_t*>(s.data()), s.size() * 4, nullptr));
    const std::uint32_t* q0 = vif.mem.qw(0);
    CHECK_BITS(q0[0], 0xFFFFFFFFu);
    CHECK_BITS(q0[1], 0x00000002u);
    CHECK_BITS(q0[2], 0xFFFF8000u);
    CHECK_BITS(q0[3], 0x00007FFFu);
    const std::uint32_t* q1 = vif.mem.qw(1);
    CHECK_BITS(q1[0], 0x11u);
    CHECK_BITS(q1[1], 0x22u);
    CHECK_BITS(q1[2], 0u);
    CHECK_BITS(q1[3], 0x1234u);
}

TEST(vif_filling_write) {
    // CL 1, WL 2 (filling): one quadword from data, then one b3asset fills from ROW. The game's car packets use only
    // skipping writes (CL 3 or 6, WL 1); filling is implemented for completeness.
    std::vector<std::uint32_t> s = {0x30000000, 9, 9, 9, 9, 0x01000201, 0x68020000, 1, 2, 3};
    VifInterpreter vif;
    CHECK(vif.run(reinterpret_cast<const std::uint8_t*>(s.data()), s.size() * 4, nullptr));
    CHECK_BITS(vif.mem.qw(0)[0], 1u);
    CHECK_BITS(vif.mem.qw(1)[0], 9u);
}

TEST(strip_restarts_and_winding) {
    // 0 1 2 3 | restart at 4,5 | 6
    std::vector<bool> adc = {false, false, false, false, true, true, false};
    std::vector<std::uint32_t> t;
    strip_to_triangles(10, adc, t);
    const std::vector<std::uint32_t> want = {10, 11, 12, 12, 11, 13, 14, 15, 16};
    CHECK(t == want);
    // A single flag skips one triangle only; the next vertex closes (i-2, i-1, i) again, oriented by the parity of i.
    std::vector<bool> one = {false, false, false, true, false};
    t.clear();
    strip_to_triangles(0, one, t);
    const std::vector<std::uint32_t> want1 = {0, 1, 2, 2, 3, 4};
    CHECK(t == want1);
}

// lines1: a line-mesh batch as the type-3 meshes carry it (STCYCL 3/1, V4-32 positions + ADC at 3i, V4-8 unsigned colours
// at 3i+2), through the VIF, into quads: segments (0,1), (2,3) skipped by vertex 3's ADC, (4,5).
TEST(track_line_batch_synthetic_packet) {
    const float P[6][3] = {{0, 0, 0}, {10, 0, 0}, {0, 5, 0}, {0, 5, 10}, {1, 2, 3}, {4, 5, 6}};
    auto fb = [](float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; };
    std::vector<std::uint32_t> s = {0x01000103, 0x05000000, 0x6C068000};
    for (int i = 0; i < 6; ++i) {
        for (int c = 0; c < 3; ++c) s.push_back(fb(P[i][c]));
        s.push_back(i == 3 ? 0x8000u : 0u);
    }
    s.push_back(0x6E06C002);   // V4-8 unsigned x6 at 2
    for (int i = 0; i < 6; ++i) s.push_back(0x80353535u + std::uint32_t(i) * 0x00010101u);
    s.push_back(0x04000006);   // ITOP 6
    s.push_back(0x17000000);   // MSCNT
    VifInterpreter vif;
    std::vector<Vertex> v;
    std::vector<std::uint32_t> idx;
    std::uint32_t segs = 0;
    CHECK(vif.run(reinterpret_cast<const std::uint8_t*>(s.data()), s.size() * 4,
                  [&](const VuMemory& vu, std::uint32_t n, VifInterpreter::KickKind, std::uint32_t) {
                      segs += track_line_batch(vu, n, v, idx);
                  }));
    CHECK(segs == 2);
    CHECK(v.size() == 8);
    const std::vector<std::uint32_t> want = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    CHECK(idx == want);
    if (v.size() == 8) {
        // quad of segment (0, 1): this end, the other end, (side, end), this end's colour
        CHECK(v[0].pos[0] == 0 && v[0].normal[0] == 10 && v[0].uv[0] == -1 && v[0].uv[1] == 0);
        CHECK(v[1].uv[0] == 1 && v[1].uv[1] == 0);
        CHECK(v[2].pos[0] == 10 && v[2].normal[0] == 0 && v[2].uv[0] == -1 && v[2].uv[1] == 1);
        CHECK_BITS(v[0].rgba, 0x80353535u);
        CHECK_BITS(v[2].rgba, 0x80363636u);
        // segment (4, 5): vertex 4's colour (index 4), the ends' positions
        CHECK(v[4].pos[2] == 3 && v[4].normal[2] == 6 && v[7].pos[1] == 5 && v[7].normal[1] == 2);
        CHECK_BITS(v[4].rgba, 0x80393939u);
    }
}

// lines1: the GPU line's width rule (one GS pixel = output rows / 448, at least 1 pixel), its box-filter coverage and the
// screen-space expansion mesh.vert performs (b3asset::track_line_expand is its twin)
TEST(track_line_width_and_expansion) {
    CHECK_NEAR(track_line_width(448), 1.0, 0);
    CHECK_NEAR(track_line_width(224), 1.0, 0);     // never thinner than one output pixel
    CHECK_NEAR(track_line_width(240), 1.0, 0);
    CHECK_NEAR(track_line_width(1080), 1080.0 / 448.0, 1e-6);
    CHECK_NEAR(track_line_width(2160), 2160.0 / 448.0, 1e-6);
    CHECK_NEAR(track_line_coverage(0, 1), 1.0, 0);
    CHECK_NEAR(track_line_coverage(0.5f, 1), 0.5, 1e-6);
    CHECK_NEAR(track_line_coverage(-1.0f, 1), 0.0, 0);
    CHECK_NEAR(track_line_coverage(0, 2.5f), 1.0, 0);
    CHECK_NEAR(track_line_coverage(1.75f, 2.5f), 0.0, 1e-6);
    CHECK_NEAR(track_line_coverage(1.25f, 2.5f), 0.5, 1e-6);
    // the integral of the coverage across the line is its width (the GS line's "ink", 1 GS pixel)
    for (float w : {1.0f, 1080.0f / 448.0f, 4.0f}) {
        double sum = 0;
        for (int i = -4000; i <= 4000; ++i) sum += track_line_coverage(float(i) / 1000.0f, w) / 1000.0;
        CHECK_NEAR(sum, w, 2e-3);
    }
    CHECK_NEAR(track_line_fog(5, 1000), 1.0, 0);
    CHECK_NEAR(track_line_fog(502.5f, 1000), 0.5, 1e-6);
    CHECK_NEAR(track_line_fog(2000, 1000), 0.0, 0);
    // a horizontal segment on screen: every vertex of the quad moves vertically by width / 2 + 1 pixels; both ends of
    // side +1 land on the same side; the offset holds in pixels at any depth (w)
    for (const float H : {1080.0f, 480.0f, 240.0f}) {
        const float W = H * 16.0f / 9.0f, width = track_line_width(H);
        const float a[4] = {-0.2f, 0.1f, 0.5f, 1.0f}, b[4] = {0.6f, 0.2f, 0.25f, 2.0f};   // b: (0.3, 0.1) in NDC
        float o[4], e = 0;
        CHECK(track_line_expand(a, b, 1.0f, 0.0f, W, H, width, o, &e));
        CHECK_NEAR(e, 0.5 * width + 1.0, 1e-6);
        // screen direction (0.5 W/2, 0 H/2) per unit: horizontal -> the offset is along y in pixels
        CHECK_NEAR((o[1] / o[3] - 0.1) * 0.5 * H, 0.5 * width + 1.0, 1e-3);
        CHECK_NEAR(o[0] / o[3], -0.2, 1e-6);
        CHECK(o[2] == a[2] && o[3] == a[3]);
        CHECK(track_line_expand(b, a, 1.0f, 1.0f, W, H, width, o, &e));
        CHECK_NEAR((o[1] / o[3] - 0.1) * 0.5 * H, 0.5 * width + 1.0, 1e-3);   // end 1, side +1: the same side
        CHECK(track_line_expand(b, a, -1.0f, 1.0f, W, H, width, o, &e));
        CHECK_NEAR((o[1] / o[3] - 0.1) * 0.5 * H, -(0.5 * width + 1.0), 1e-3);
        CHECK_NEAR(e, -(0.5 * width + 1.0), 1e-6);
    }
    // an end behind the eye moves along the segment onto w = kLineNearW; both behind: collapsed
    const float front[4] = {0, 0, 0.1f, 1.0f}, behind[4] = {0, 2.0f, 0.1f, -1.0f}, behind2[4] = {1, 1, 0, -0.5f};
    float o[4], e = 0;
    CHECK(track_line_expand(behind, front, 1.0f, 0.0f, 1920, 1080, 2, o, &e));
    CHECK_NEAR(o[3], kLineNearW, 1e-7);
    CHECK(!track_line_expand(behind, behind2, 1.0f, 0.0f, 1920, 1080, 2, o, &e));
    CHECK(o[0] == 0 && o[1] == 0 && o[3] == 1);
}

// Hand-assembled micro program: loads, a dependent ADD (stall), MAX on an integer word (raw compare, no denormal
// flush), E bit with its delay slot, and the TPC where MSCNT continues.
TEST(vu1_interpreter_basics) {
    VuMicroprogram p;
    const std::uint32_t nop_up = 0x000002FFu, nop_lo = 0x8000033Cu, ebit = 0x40000000u;
    const std::uint32_t prog[][2] = {
        {0x01E10000u, nop_up},          // lq.xyzw vf01, 0(vi00)
        {0x01E20001u, nop_up},          // lq.xyzw vf02, 1(vi00)
        {0x01E50002u, nop_up},          // lq.xyzw vf05, 2(vi00)
        {nop_lo, 0x01E208E8u},          // add.xyzw vf03, vf01, vf02
        {nop_lo, 0x0100292Bu},          // max.x vf04, vf05, vf00
        {0x03E01803u, nop_up | ebit},   // sq.xyzw vf03, 3(vi00)   [E]
        {0x03E02004u, nop_up},          // sq.xyzw vf04, 4(vi00)   (delay slot)
        {0x03E01805u, nop_up},          // sq.xyzw vf03, 5(vi00)   (must not run)
    };
    for (std::uint32_t i = 0; i < 8; ++i) { p.lower[i] = prog[i][0]; p.upper[i] = prog[i][1]; }
    VuMemory m;
    for (int l = 0; l < 4; ++l) {
        m.w[0 * 4 + l] = t::fbits(1.5f + float(l));
        m.w[1 * 4 + l] = t::fbits(0.25f);
    }
    m.w[2 * 4 + 0] = 0x8000u;  // an ADC flag word, a denormal as a float
    Vu1 vu(m);
    std::string why;
    CHECK(vu.run(p, 0, 0, 0, nullptr, &why));
    CHECK_NEAR(t::bitsf(m.w[3 * 4 + 0]), 1.75, 0);
    CHECK_NEAR(t::bitsf(m.w[3 * 4 + 3]), 4.75, 0);
    CHECK_BITS(m.w[4 * 4 + 0], 0x8000u);
    CHECK_BITS(m.w[5 * 4 + 0], 0u);
    CHECK(vu.tpc == 7);
    CHECK(vu.stats.stall_cycles >= 3);  // ADD waits for the LQ of vf02 (4-cycle latency)
}

TEST(png_encoder_layout) {
    const std::uint8_t px[8] = {255, 0, 0, 255, 0, 255, 0, 128};
    const auto png = encode_png_rgba(2, 1, px);
    CHECK(png.size() > 57);
    CHECK(std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);
    // IEND chunk with its fixed CRC.
    CHECK(std::memcmp(png.data() + png.size() - 12, "\0\0\0\0IEND\xae\x42\x60\x82", 12) == 0);
}

// CB3CrashRamp::Prepare's corners and Draw's 18 vertices (crash2, b3asset/crash.h) on an axis-aligned type-2 ramp.
TEST(crash_ramp_corners_and_faces) {
    const float m[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {10, 2, 20}};
    float c[6][3];
    crash_ramp_corners(m, 2, c);   // b 7, a 2.55, c 3.2, d 1
    const float want[6][3] = {{7.45f, 2, 27}, {12.55f, 2, 27}, {7.45f, 2, 13}, {12.55f, 2, 13}, {7.45f, 5.2f, 27}, {12.55f, 3, 27}};
    for (int p = 0; p < 6; ++p)
        for (int k = 0; k < 3; ++k) CHECK_NEAR(c[p][k], want[p][k], 1e-5);
    ImVertex v[kCrashRampVertices];
    crash_ramp_vertices(c, 2, v);
    // the slope's two triangles share the type-2 diagonal p5-p2; every other face stands on the base or the back edge
    CHECK(std::memcmp(v[12].pos, c[5], 12) == 0 && std::memcmp(v[13].pos, c[2], 12) == 0 && std::memcmp(v[17].pos, c[2], 12) == 0);
    crash_ramp_vertices(c, 0, v);
    CHECK(std::memcmp(v[13].pos, c[4], 12) == 0 && std::memcmp(v[14].pos, c[3], 12) == 0);
    for (int i = 0; i < kCrashRampVertices; ++i) CHECK(v[i].pos[1] >= 2.0f && v[i].uv[0] >= 0 && v[i].uv[1] <= 1);
    ImVertex q[kCrashPickupVertices];
    crash_pickup_vertices(q);
    CHECK(q[0].pos[1] == 1 && q[1].pos[0] == -1 && q[1].uv[0] == 1 && q[5].pos[0] == 1 && q[5].uv[1] == 1);
}

TEST(sun_flare_game_arithmetic) {
    // water1 (docs/native-renderer.md §21): OcclusionTest / RenderCorona's integer math in 1/16 px [L drafts]
    SunFlareTable t;
    t.valid = true;
    for (int i = 0; i < 12; ++i) { t.sec[i].pos = 1.0f - 0.25f * float(i); t.sec[i].size = 0.5f; t.sec[i].rgb[0] = t.sec[i].rgb[1] = t.sec[i].rgb[2] = 0.25f; t.sec[i].quadrant = std::uint32_t(i % 4); }
    SunFlareInput in;
    in.sun_px[0] = 100.25f; in.sun_px[1] = 50.0f;
    in.radius = 160.0f; in.aspect = 448.0f / 480.0f;
    in.colour[0] = 120.9f; in.colour[1] = 115.4f; in.colour[2] = 100.9f;
    in.now = 12.5f;
    SunFlare f = sun_flare(in, t, 128, 256);
    CHECK(f.drawn && f.sprites.size() == 13);
    CHECK_NEAR(f.fade, 1.0, 1e-6);                        // the whole 14 x 14 px box on screen
    CHECK_NEAR(f.box[0], 93.0, 1e-6); CHECK_NEAR(f.box[2], 108.0, 1e-6);   // (1604 - 112) & ~15, (1716 + 15) & ~15
    CHECK(f.sprites[0].rgb[0] == 120 && f.sprites[0].rgb[2] == 100);
    CHECK_NEAR(f.sprites[0].x0, 100.25 - 160.0, 1e-6);
    CHECK_NEAR(f.sprites[0].y1, 50.0 + 2389 / 16.0, 1e-6);  // int(2560 x 0.9333) = 2389
    CHECK_NEAR(f.sprites[0].v1, 127.5 / 256.0, 1e-6);       // the upper half with the secondaries
    // secondary 0 sits on the sun (pos 1), secondary 4 on the screen centre (pos 0)
    CHECK_NEAR(0.5 * (f.sprites[1].x0 + f.sprites[1].x1), 100.25, 1e-6);
    CHECK_NEAR(0.5 * (f.sprites[5].x0 + f.sprites[5].x1), 320.0, 1e-6);
    CHECK_NEAR(f.sprites[4].u0, 64.5 / 128.0, 1e-6);       // quadrant 3: right half, lower quarter
    CHECK_NEAR(f.sprites[4].v0, 192.5 / 256.0, 1e-6);
    for (int k = 1; k < 13; ++k) CHECK(f.sprites[k].rgb[0] >= 22 && f.sprites[k].rgb[0] <= 30);   // 0.25 x [0.75, 1] x 120
    // at the edge: the box clamped, the fade below 1; past 6.5 px: not drawn
    in.sun_px[0] = 2.0f;
    f = sun_flare(in, t, 128, 256);
    CHECK(f.drawn && f.fade < 1.0f && f.box[0] == 0.0f);
    CHECK_NEAR(f.fade, float(((144) * 224) / 256) / 196.0f, 1e-6);   // (32 + 112) x 224
    in.sun_px[0] = -7.0f;
    CHECK(!sun_flare(in, t, 128, 256).drawn);
    CHECK(!route_has_sun("x/TRACKS/EU/M2_V2") && !route_has_sun("x/TRACKS/US/P2_V1/") && route_has_sun("x/TRACKS/EU/C1_V1"));
}

#ifdef B3_GAME_DIR
// GAMEDATA.BGD of every route: the directory and each stage's pickups and ramps parse inside the file; the race2 junction
// (US/C3_V1 stage 6, a reported spot) holds one type-0 ramp; GLOBAL.TXD has the ramp and pickup textures.
// modsec: a modded car file's offsets point anywhere. The bytes sit at the very end of a readable page with an
// unreadable page after it, so a read past the file's end faults instead of reading the heap.
#if defined(__linux__) || defined(__APPLE__)
struct GuardedBytes {
    std::uint8_t* base = nullptr;
    std::size_t map = 0;
    Bytes b;
    explicit GuardedBytes(const std::vector<std::uint8_t>& v) {
        const std::size_t pg = std::size_t(sysconf(_SC_PAGESIZE));
        const std::size_t data = (v.size() + pg - 1) / pg * pg;
        map = data + pg;
        void* m = mmap(nullptr, map, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        base = static_cast<std::uint8_t*>(m);
        mprotect(base + data, pg, PROT_NONE);
        std::uint8_t* at = base + data - v.size();
        if (!v.empty()) std::memcpy(at, v.data(), v.size());
        b = Bytes{at, v.size()};
    }
    ~GuardedBytes() { munmap(base, map); }
};
void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) { std::memcpy(v.data() + at, &x, 4); }
TEST(car_reader_checks_file_offsets) {
    GsMemory gs;
    Model m;
    std::string why;
    auto bad = [&](const std::vector<std::uint8_t>& v, CarOptions o, const char* part) {
        GuardedBytes g(v);
        const bool ok = !read_car(g.b, o, gs, m, &why) && why.find(part) != std::string::npos;
        if (!ok) std::printf("    (got: %s)\n", why.c_str());
        return ok;
    };
    std::vector<std::uint8_t> v(0x10, 0);
    put32(v, 0, 0x17);
    CHECK(bad(v, {}, "too short"));   // npanel / nwheel and the LOD table past the end
    v.assign(0x200, 0);
    put32(v, 0, 0x17);
    put32(v, 0x60, 0x1F0);   // the texture's header runs past the end
    CarOptions tex;
    tex.texture = true;
    CHECK(bad(v, tex, "car texture outside the file"));
    put32(v, 0x60, 0);
    put32(v, 0x4C, 0x100);   // LOD 0 block
    put32(v, 0x100, 0xFC);   // list 0 at 0x1FC: its count bytes and entries offset past the end
    CHECK(bad(v, {}, "car list outside the file"));
    put32(v, 0x100, 0x80);   // list 0 at 0x180: one entry at 0x1F8, 16 bytes past the end
    v[0x180] = 1;
    put32(v, 0x184, 0x78);
    CHECK(bad(v, {}, "car list entry outside the file"));
}
TEST(gif_transfer_stays_in_the_file) {   // a PACKED A+D tag whose loop runs past the end of the file
    std::vector<std::uint8_t> v(32, 0);
    const std::uint64_t tag = 4 /*nloop*/ | std::uint64_t(1) << 60 /*nreg 1*/;
    const std::uint64_t regs = 0xE;
    std::memcpy(v.data(), &tag, 8);
    std::memcpy(v.data() + 8, &regs, 8);
    GuardedBytes g(v);
    GsMemory gs;
    run_gif(g.b, 0, 100, gs);   // (qwc larger than the file: stops at its end)
    CHECK(true);
}
#endif

TEST(real_gamedata_all_routes) {
    int routes = 0, stages = 0, ramps = 0, pickups = 0;
    for (const auto& area : std::filesystem::directory_iterator(std::string(B3_GAME_DIR) + "/TRACKS")) {
        if (!area.is_directory()) continue;   // TRACKS also holds the route movies (*.M2V)
        for (const auto& r : std::filesystem::directory_iterator(area.path())) {
            std::vector<std::uint8_t> d;
            if (!read_file((r.path() / "GAMEDATA.BGD").string(), d)) continue;
            std::vector<GameStage> st;
            std::string why;
            CHECK(read_game_data(Bytes{d.data(), d.size()}, st, &why));
            ++routes;
            stages += int(st.size());
            for (const auto& s : st) {
                ramps += int(s.ramps.size());
                pickups += int(s.pickups.size());
                for (const auto& rp : s.ramps) CHECK(rp.type >= 0 && rp.type <= 3);
                for (const auto& p : s.pickups) CHECK(p.type >= 0 && p.type < 9);
            }
            if (r.path().filename() == "C3_V1" && area.path().filename() == "US") {
                CHECK(st.size() == 14 && st[6].ramps.size() == 1 && st[6].ramps[0].type == 0 && st[6].pickups.size() == 13);
                CHECK_NEAR(st[6].ramps[0].m[3][0], 4918.38, 0.01);
                CHECK_NEAR(st[6].starts[0][0], 5075.53, 0.01);
            }
        }
    }
    std::printf("    GAMEDATA.BGD: %d routes, %d stages, %d crash ramps, %d crash pickups\n", routes, stages, ramps, pickups);
    CHECK(routes == 36);
    std::vector<std::uint8_t> txd;
    CHECK(read_file(std::string(B3_GAME_DIR) + "/DATA/GLOBAL.TXD", txd));
    GsMemory gs;
    DecodedRaster r;
    CHECK(find_txd_raster(Bytes{txd.data(), txd.size()}, kCrashRampTexture, gs, r));
    for (const char* n : kCrashPickupTextures) CHECK(find_txd_raster(Bytes{txd.data(), txd.size()}, n, gs, r));
}

TEST(real_txd_all_textures_decode) {
    for (const char* name : {"/DATA/GLOBAL.TXD", "/DATA/FRONTEND.TXD"}) {
        std::vector<std::uint8_t> d;
        CHECK(read_file(std::string(B3_GAME_DIR) + name, d));
        Bytes f{d.data(), d.size()};
        std::vector<TxdEntry> es;
        CHECK(read_txd(f, es));
        GsMemory gs;
        int ok = 0;
        for (const auto& e : es) { DecodedRaster r; ok += decode_raster(f, e.offset, gs, r); }
        CHECK(ok == int(es.size()));
        CHECK(es.size() == (std::strstr(name, "GLOBAL") ? 194u : 221u));
    }
}

TEST(real_txd_known_texture) {
    std::vector<std::uint8_t> d;
    CHECK(read_file(std::string(B3_GAME_DIR) + "/DATA/GLOBAL.TXD", d));
    GsMemory gs;
    DecodedRaster r;
    CHECK(decode_raster(Bytes{d.data(), d.size()}, 0xC40, gs, r));
    CHECK(r.hdr.name == "16_curve");
    CHECK(r.hdr.psm() == PSMT4 && r.hdr.width == 16 && r.hdr.height == 16);
    // Regression guard (value recorded after a visual check of the decoded PNGs, 2026-11-02).
    CHECK_BITS(fnv(r.levels[0].rgba), 0x7246C56Au);
}

TEST(real_car_comp1) {
    std::vector<std::uint8_t> d;
    CHECK(read_file(std::string(B3_GAME_DIR) + "/PVEH/COMP/CAR1.BGV", d));
    GsMemory gs;
    Model m;
    std::string why;
    CHECK(read_car(Bytes{d.data(), d.size()}, CarOptions{}, gs, m, &why));
    CHECK(m.vertices.size() == 7634);
    CHECK(m.indices.size() / 3 == 4806);  // GS strip rule: a single ADC flag skips one triangle (was 4326)
    CHECK(m.stats["palettes"] == 8);
    CHECK_NEAR(m.bmin[0], -1.016, 0.001);
    CHECK_NEAR(m.bmax[2], 2.064, 0.001);
    CHECK(m.textures.size() == 1 && m.textures[0].levels[0].w == 256);
}
TEST(real_sun_flare_table) {
    // water1: D_004BB610 (12 x 0x18) and D_004E1978 from the retail ELF [C]
    SunFlareTable t;
    std::string why;
    CHECK(read_sun_flare_table(std::string(B3_GAME_DIR) + "/SLUS_210.50", t, &why));
    CHECK(t.valid && t.secondaries);
    CHECK_NEAR(t.sec[0].pos, 1.48, 1e-6);
    CHECK_NEAR(t.sec[11].pos, -1.0, 1e-6);
    CHECK_NEAR(t.sec[11].size, 0.8, 1e-6);
    CHECK(t.sec[0].quadrant == 2 && t.sec[5].quadrant == 1 && t.sec[2].quadrant == 0);
}

TEST(real_water_materials_c1_v1) {
    // water1 (§21): RenderWater forces the STATIC +0x30 meshes to type 7 (= the type-0 program + the mirror); the
    // file's materials are type 0 there and type 6 blended by As on the +0x2C surface (all 37 routes: census)
    std::vector<std::uint8_t> st, sm;
    CHECK(read_file(std::string(B3_GAME_DIR) + "/TRACKS/EU/C1_V1/STATIC.DAT", st));
    CHECK(read_file(std::string(B3_GAME_DIR) + "/TRACKS/EU/C1_V1/STREAMED.DAT", sm));
    GsMemory gs;
    Model m;
    std::string why;
    CHECK(read_track(Bytes{st.data(), st.size()}, Bytes{sm.data(), sm.size()}, TrackOptions{}, gs, m, &why));
    int refl = 0, surf = 0;
    for (const auto& d : m.draws) {
        if (d.part.rfind("water+30 ", 0) == 0) { ++refl; CHECK((d.material & 0xFFFF) == 0); }
        else if (d.part.rfind("water ", 0) == 0) { ++surf; CHECK((d.material & 0xFFFF) == 6 && d.blend == DrawRange::kBlend && d.anim >= 0); }
    }
    CHECK(refl == 6 && surf == 5);
}

TEST(real_car_paint_clut_table) {
    // live2 (docs/native-renderer.md §20): CB3VehicleRenderer::SetTexture 0x21FF60 [C matched] adds the raster's u16
    // table at +0x90 to CBP for paint < count (+0xA0), else uses the base CLUT; the table is not 4 x paint.
    std::vector<std::uint8_t> d;
    CHECK(read_file(std::string(B3_GAME_DIR) + "/PVEH/COMP/CAR1.BGV", d));
    const Bytes f{d.data(), d.size()};
    const std::size_t tex = f.u32(0x60);
    CHECK(f.p[tex + 0xA0] == 8);
    const std::uint32_t want[8] = {0, 4, 16, 20, 8, 12, 24, 28};
    for (int k = 0; k < 8; ++k) CHECK(palette_cbp_add(f, tex, k) == want[k]);
    CHECK(palette_cbp_add(f, tex, 8) == 0 && palette_cbp_add(f, tex, 200) == 0 && palette_cbp_add(f, tex, -1) == 0);
    // paint k through read_car = the raster decoded with the CLUT at the table's block; paints 2 and 4 differ
    GsMemory gs;
    std::vector<std::vector<std::uint8_t>> paint;
    for (int k = 0; k < 8; ++k) {
        Model m;
        CarOptions co;
        co.palette = k;
        std::string why;
        CHECK(read_car(f, co, gs, m, &why));
        DecodedRaster r;
        CHECK(decode_raster_clut(f, tex, gs, r, want[k], &why));
        CHECK(!m.textures.empty() && m.textures[0].levels[0].rgba == r.levels[0].rgba);
        paint.push_back(m.textures.empty() ? std::vector<std::uint8_t>{} : m.textures[0].levels[0].rgba);
    }
    CHECK(paint[2] != paint[4] && paint[3] != paint[5]);
    Model m8;
    CarOptions co8;
    co8.palette = 8;   // past the count: the base CLUT, as paint 0 (before live2: paint 8 mod 8 = 0 too; 9 -> 1 was wrong)
    CHECK(read_car(f, co8, gs, m8, nullptr) && m8.textures[0].levels[0].rgba == paint[0]);
    co8.palette = 9;
    CHECK(read_car(f, co8, gs, m8, nullptr) && m8.textures[0].levels[0].rgba == paint[0]);
}

TEST(real_track_as_c1_v1) {
    std::vector<std::uint8_t> st, sm;
    CHECK(read_file(std::string(B3_GAME_DIR) + "/TRACKS/AS/C1_V1/STATIC.DAT", st));
    CHECK(read_file(std::string(B3_GAME_DIR) + "/TRACKS/AS/C1_V1/STREAMED.DAT", sm));
    GsMemory gs;
    Model m;
    std::string why;
    std::vector<TrackMaterialInfo> mats;
    CHECK(read_track(Bytes{st.data(), st.size()}, Bytes{sm.data(), sm.size()}, TrackOptions{}, gs, m, &why, &mats));
    CHECK(why.empty());
    CHECK(m.stats["units"] == 49);
    CHECK(m.stats["materials"] == 181 && mats.size() == 181);
    CHECK(m.stats["textures"] == 180);
    CHECK(m.stats["meshes"] == 1527);
    CHECK(m.stats["instance placements"] == 2004);
    CHECK(m.stats["prop placements"] == 377);
    CHECK(m.transforms.size() == 2381);
    CHECK(m.vertices.size() == 185740);
    CHECK(m.indices.size() / 3 == 112868);  // GS strip rule (was 107726)
    CHECK(m.stats["batches without positions"] == 0);
    for (const auto& d : m.draws) {
        CHECK(d.first_index + d.index_count <= m.indices.size());
        if (d.texture >= int(m.textures.size())) CHECK(false);
    }
    // anim1: the header's update list (7 materials) and CB3StaticTrack::Update's arithmetic on two of them
    CHECK(m.track_anims.size() == 7);
    const TrackAnim* arrows = nullptr;
    const TrackAnim* sign = nullptr;
    for (const auto& a : m.track_anims) {
        if (a.material == 129) arrows = &a;
        if (a.material == 19) sign = &a;
    }
    CHECK(arrows && arrows->flags == 0x208 && arrows->frames.size() == 1 && arrows->f14 == 1.2f && arrows->f18 == 0.125f);
    CHECK(sign && sign->frames.size() == 6 && sign->number[1] == 4 && sign->number[5] == 12);
    if (arrows && sign) {
        TrackAnimState s = track_anim_prepare(*arrows);
        // steps of 0.125 at 1.2 per second, at most one per tick
        for (int j = 1; j <= 60; ++j) track_anim_update(*arrows, s, float(j) / 60.0f);
        CHECK(s.t == 1.125f);
        CHECK(std::fabs(track_uv_scroll(s.t) - 0.875f) < 1e-6f);
        // frame k shows for (number[k+1] - number[k]) x +0x14 s; frame 0 first for +0x18
        TrackAnimState c = track_anim_prepare(*sign);
        track_anim_update(*sign, c, 1.0f);
        CHECK(c.cur == 0);
        track_anim_update(*sign, c, 1.0167f);
        CHECK(c.cur == 1 && c.p12 == 5 && c.period == 4.0f && c.t == 1.0167f);
        track_anim_update(*sign, c, 5.0f);
        CHECK(c.cur == 1);
        track_anim_update(*sign, c, 5.1f);
        CHECK(c.cur == 2 && c.p12 == 8 && c.period == 3.0f);
        int draws = 0;
        for (const auto& d : m.draws) draws += d.anim >= 0;
        CHECK(draws > 0);
    }
}
#endif

#if defined(B3_GAME_DIR) && defined(B3_VU_DIR)
// The shader-accuracy check (docs/native-renderer.md §11.4) on one route: the game's track VU1 programs, run in the
// interpreter on real batches, against the C++ model the shaders implement (full run: tools/render/b3vucheck).
TEST(real_track_vu1_reference) {
    std::vector<std::uint8_t> st, sm;
    CHECK(read_file(std::string(B3_GAME_DIR) + "/TRACKS/AS/C1_V1/STATIC.DAT", st));
    CHECK(read_file(std::string(B3_GAME_DIR) + "/TRACKS/AS/C1_V1/STREAMED.DAT", sm));
    struct B { VuMemory mem; std::uint32_t itop; int mi; };
    std::vector<B> batches;
    TrackOptions o;
    o.unit_count = 4;
    o.on_batch = [&](const VuMemory& vu, std::uint32_t itop, const std::map<std::uint32_t, int>&, int mi, int tr) {
        if (tr < 0 && batches.size() < 400) batches.push_back({vu, itop, mi});
    };
    GsMemory gs;
    Model model;
    std::vector<TrackMaterialInfo> mats;
    CHECK(read_track(Bytes{st.data(), st.size()}, Bytes{sm.data(), sm.size()}, o, gs, model, nullptr, &mats));
    std::map<std::uint32_t, VuMicroprogram> progs;
    int compared = 0, bad = 0;
    for (B& b : batches) {
        const TrackMaterialInfo& m = mats[std::size_t(b.mi)];
        if (m.type == 3 || m.type > 6) continue;
        const std::uint32_t pa = track_program(m);
        if (!progs.count(pa)) {
            char name[64];
            std::snprintf(name, sizeof name, "/game_%08X.textbin.bin", pa);
            std::vector<std::uint8_t> img;
            CHECK(read_file(std::string(B3_VU_DIR) + name, img));
            CHECK(progs[pa].load_textbin(img));
        }
        float c[3] = {};
        for (std::uint32_t i = 0; i < b.itop; ++i) for (int k = 0; k < 3; ++k) c[k] += b.mem.f(3 * i, k) / float(b.itop);
        const float eye[3] = {c[0] + 60, c[1] + 40, c[2] - 50}, sun[3] = {0.3f, 0.8f, -0.52f};
        const ShadeScene sc = shade_scene_look_at(eye, c, sun);
        VuMemory mem = b.mem;
        track_vu_constants(sc, m, 0, mem);
        Vu1 vu(mem);
        std::vector<GifPacket> out;
        const auto kick = [&](std::uint32_t qa) { GifPacket g; CHECK(read_gif_packet(mem, qa, g)); out.push_back(g); };
        CHECK(vu.run(progs[pa], 0, 0, b.itop, kick) && vu.run(progs[pa], vu.tpc, 0, b.itop, kick));
        std::vector<TrackVertexOut> want;
        shade_track_batch(sc, m, track_batch_inputs(b.mem, b.itop, m.type), want);
        if (out.size() != 1 || out[0].verts.size() != b.itop) { ++bad; continue; }
        for (std::uint32_t i = 0; i < b.itop; ++i) {
            const GifVertex& g = out[0].verts[i];
            const TrackVertexOut& w = want[i];
            ++compared;
            const bool same = g.r == w.rgba[0] && g.g == w.rgba[1] && g.b == w.rgba[2] && std::abs(int(g.a) - int(w.rgba[3])) <= 2 &&
                              std::fabs(g.s / g.q - w.u) < 2e-3f && std::fabs(g.t / g.q - w.v) < 2e-3f &&
                              (i < 2 || w.q <= 0.0f || w.q > 1.0f || !g.adc == w.drawn);
            if (!same) ++bad;
        }
    }
    CHECK(compared > 5000);
    CHECK(bad == 0);
    std::printf("    %d vertices compared, %d mismatches\n", compared, bad);
}
#endif

// The corona program's model (b3asset/coronashade.h, corona1) on a synthetic camera: a camera at the origin looking
// along +z (view window 1 x 0.75 at distance 1, 640 x 448, the race camera's z constants). One slot draws; the others
// are the program's four drops (no fade distance, beyond K x fade distance, colour <= 8 after the fade, off screen).
TEST(corona_program_model_synthetic) {
    std::uint8_t rec[0x230] = {};
    auto put = [&](int off, float v) { std::memcpy(rec + off, &v, 4); };
    put(0xE0 + 0x00, 0.5f);           // R0 (x): x / w = 0.5 + 0.5 x / (w half_w)
    put(0xE0 + 0x14, -0.5f / 0.75f);  // R1 (y): y down
    put(0xE0 + 0x20, 0.5f);           // R2 (z): + 0.5 w, depth w = z
    put(0xE0 + 0x24, 0.5f);
    put(0xE0 + 0x28, 1.0f);
    put(0x168, 5032309.0f);
    put(0x170, 2048.0f);
    put(0x174, 2048.0f);
    put(0x178, 1174.5f);
    put(0x188, -0.6000179648f);
    put(0x198, 1.000059962f);
    put(0x200, 1.0f);
    put(0x204, 0.75f);
    put(0x208, 1.0f);
    put(0x218, 640.0f);
    put(0x21C, 448.0f);
    const b3asset::CoronaCamera cam = b3asset::corona_camera(rec);
    auto slot = [](float x, float y, float z, float fade, float r, float g, float b, float size) {
        b3asset::CoronaSlotWords w;
        const float v[8] = {x, y, z, fade, r, g, b, size};
        for (int k = 0; k < 8; ++k) w.w[k] = t::fbits(v[k]);
        return w;
    };
    const std::vector<b3asset::CoronaSlotWords> slots = {
        slot(0, 0, 10, 400, 64, 32, 16, 2),      // drawn
        slot(0, 0, 10, 0, 64, 32, 16, 2),        // unused slot (EndUpdate's w = 0)
        slot(0, 0, 500, 400, 64, 32, 16, 2),     // beyond K x 400
        slot(0, 0, 10, 400, 8, 8, 8, 2),         // 7.8 after the fade: not above 8
        slot(-20, 0, 10, 400, 64, 32, 16, 2),    // left of the window
    };
    const auto out = b3asset::corona_sprites(cam, slots);
    CHECK(out.size() == 1);
    if (out.size() != 1) return;
    const auto& s = out[0];
    CHECK(s.slot == 0);
    CHECK(s.col[0] == 62 && s.col[1] == 31 && s.col[2] == 15);   // x (1 - 10 / 400), truncated
    CHECK(s.rgba[3] == 0x80);
    // a square 2 m across at 10 m: half extent 640 / 4 x 2 / 10 = 32 px, 448 / (4 x 0.75) x 0.2 = 29.87 px
    CHECK(std::abs(int(s.x[0]) - (2048 - 32) * 16) <= 1 && std::abs(int(s.x[1]) - (2048 + 32) * 16) <= 1);
    CHECK(std::abs(int(s.y[0]) - int(std::lround((2048 - 29.8667) * 16))) <= 1);
    // Z of the second corner: zShift + zScale / (10 - 1) + 2 / 10
    CHECK(std::abs(int(s.z[1]) - int(1174.5 + 5032309.0 / 9.0 + 0.2)) <= 1);
    CHECK(t::bitsf(s.st[0][0]) == 0.0f && t::bitsf(s.st[1][0]) == t::bitsf(s.st[1][2]));   // ST (0, 0) -> (1, 1) after / Q
}

TEST_MAIN("b3asset")

TEST(bloom_gains_first_downsample) {
    // post1 (docs/native-renderer.md §25): CB3Bloom::FirstDownsample 0x2653B0's integer set-up [L draft, C asm]
    BloomGains g = bloom_gains(0.65f);   // roadrage1's threshold (EU/C4)
    CHECK(g.c == 166 && g.k1 == 255 && g.k2 == 195 && g.k3 == 128);
    g = bloom_gains(0.75f);              // race2's
    CHECK(g.c == 191 && g.k1 == 255 && g.k2 == 255 && g.k3 == 136);
    CHECK(bloom_gains(1.0f).c == 254);   // sltiu 0xFF: at most 254
    CHECK(bloom_gains(-0.5f).c == 254);  // a negative int compares unsigned: 254 too
    // the three factors multiply to about s / 128 = 1.05 x 256 / (255 - c)
    CHECK_NEAR(255.0 * 195.0 * 128.0 / (128.0 * 128.0 * 128.0), 1.05 * 256.0 / 89.0, 0.02);
    CHECK(bloom_feedback_fix(0.5f) == 59 && bloom_feedback_fix(0.9f) == 106);
    CHECK(bloom_intensity_rgb(1.0f) == 128 && bloom_intensity_rgb(3.0f) == 255);
}

TEST(directional_blur_motion_branch) {
    // post1: UpdateCamera 0x4256C0's motion branch [L draft; constants from the ELF]
    const float cam[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
    const float old_point[3] = {0.1f, 0.05f, 10.5f};   // the point 10 m ahead moved by (0.1, 0.05, 0.5) in camera space
    DirBlur d = directional_motion(old_point, 1.0f, 1.0f + 1.0f / 60.0f, cam, false, 0);
    CHECK(d.drawn);
    CHECK_NEAR(d.amount, 0.020408163 * (std::sqrt(36.0 + 9.0 + 900.0) - 1.0), 2e-4);
    CHECK_NEAR(d.centre[0], 0.5 + 6.0 * 0.998 / 30.0, 1e-4);
    CHECK_NEAR(d.centre[1], 0.5 + 3.0 * 0.998 / 30.0, 1e-4);
    const double e = std::sqrt(double(d.centre[0]) * d.centre[0] + double(d.centre[1]) * d.centre[1]);   // the far corner is (0, 0)
    CHECK_NEAR(d.zoom, 1.0 - 0.012 * (0.5 + 0.5 * d.amount) / e, 1e-6);
    DirBlur s = directional_motion(old_point, 1.0f, 1.0f + 1.0f / 60.0f, cam, true, 0.5f);
    CHECK_NEAR(s.amount, 0.5 * d.amount, 1e-6);
    // slow: no amount (the call still returns 1); no time step: nothing
    const float still[3] = {0.0f, 0.0f, 10.01f};
    CHECK(!directional_motion(still, 1.0f, 1.0f + 1.0f / 60.0f, cam, false, 0).drawn);
    CHECK(!directional_motion(old_point, 1.0f, 1.0f, cam, false, 0).drawn);
    // moving backwards: v flipped, the same centre
    const float back[3] = {-0.1f, -0.05f, 9.5f};
    DirBlur b = directional_motion(back, 1.0f, 1.0f + 1.0f / 60.0f, cam, false, 0);
    CHECK_NEAR(b.centre[0], d.centre[0], 1e-5);
    CHECK_NEAR(b.centre[1], d.centre[1], 1e-5);
}
