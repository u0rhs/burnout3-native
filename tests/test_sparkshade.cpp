// Spark model (b3asset/sparkshade.h): the EE-side rebuilds and the program model on hand-made inputs; with the game's
// VU images available (B3_VU_DIR), the real program 0x46D090 in the VU1 interpreter against the model on one frame.
#include "test.h"

#include <cmath>
#include <vector>

#include "b3asset/export.h"
#include "b3asset/sparkshade.h"
#include "b3asset/vif.h"
#include "b3asset/vu1.h"

namespace {

using u32 = std::uint32_t;

// Made-up params (not the game's): lifetimes 1, 0.5, 0.5, 0.25; gravity -10; half width 0.1; blur 0.05; 100 / 0 sparks;
// s 0 -> 1 over the blur time, t 0 / 0.875; colours (1, 0.5, 0.25, 1) for every ring.
b3asset::SparkParams params() {
    b3asset::SparkParams p;
    auto put = [&](int off, float v) { p.w[off / 4] = t::fbits(v); };
    for (int r = 0; r < 4; ++r) { put(0x10 * r, 1.0f); put(0x10 * r + 4, 0.5f); put(0x10 * r + 8, 0.25f); put(0x10 * r + 12, 1.0f); }
    put(0x40, 1.0f); put(0x44, 0.5f); put(0x48, 0.5f); put(0x4C, 0.25f);
    put(0x50, -10.0f); put(0x54, 0.5f); put(0x58, 0.1f); put(0x5C, 0.05f);
    p.w[0x60 / 4] = 100;
    p.w[0x64 / 4] = 0;
    put(0x6C, 0.0f); put(0x70, 0.0f); put(0x74, 1.0f); put(0x78, 0.875f);
    return p;
}

// A camera record looking down +z from the origin: x / w = 0.5 + 0.5 x / z, y / w = 0.5 - 0.5 y / (0.7 z), w = z.
void camera(std::uint8_t rec[0x230], float shift_z) {
    std::memset(rec, 0, 0x230);
    auto put = [&](int off, float v) { std::memcpy(rec + off, &v, 4); };
    put(0xE0 + 0x00, 0.5f);            // R0: x
    put(0xE0 + 0x14, -0.5f / 0.7f);    // R1: y
    put(0xE0 + 0x20, 0.5f);            // R2: + 0.5 w, + 0.5 w, w = z
    put(0xE0 + 0x24, 0.5f);
    put(0xE0 + 0x28, 1.0f);
    put(0xE0 + 0x38, -shift_z);        // R3: the camera at z = shift_z
    put(0xE0 + 0x30, -0.5f * shift_z);
    put(0xE0 + 0x34, -0.5f * shift_z);
    put(0x168, 5032309.0f); put(0x170, 2048.0f); put(0x174, 2048.0f); put(0x178, 1174.5f);
    put(0x18C, 1000.0f); put(0x19C, 0.5f);
    put(0x200, 1.0f); put(0x204, 0.7f); put(0x208, 1.0f); put(0x20C, 1.0f / 0.7f);
    put(0x218, 640.0f); put(0x21C, 448.0f);
}

struct Scene {
    b3asset::SparkParams p = params();
    b3asset::SparkFrame hist[18];
    u32 cam914[4] = {}, tag[4] = {0, 0x802A4000u, 0x42424242u, 0};
    std::vector<b3asset::SparkBatch> batches;
    std::vector<u32> ring[4];
};

void put_spark(std::vector<u32>& ring, int i, float t, float x, float y, float z, float vx, float vy, float vz) {
    u32* B = ring.data() + 36 * (i / 4);
    const int j = i & 3;
    B[j] = t::fbits(t);
    const float v[8] = {x, y, z, y, vx, vy, vz, vy};
    for (int k = 0; k < 8; ++k) B[4 + 8 * j + k] = t::fbits(v[k]);
}

Scene scene() {
    Scene s;
    std::uint8_t rec[0x230];
    for (int k = 0; k < 18; ++k) {   // the camera moving back along z at 6 m/s, frames 1/60 s apart
        const float t = 10.0f - float(k) / 60.0f;
        camera(rec, -0.1f * float(k));
        s.hist[k] = b3asset::spark_frame_set(rec, t);
        if (k == 0) b3asset::spark_cam914_from_record(rec, s.cam914);
    }
    s.batches = b3asset::spark_bank_batches(s.p, false);
    int cap[4] = {};
    for (const auto& b : s.batches) cap[b.ring] = std::max(cap[b.ring], b.first + b.blocks);
    for (int r = 0; r < 4; ++r) {
        s.ring[r].assign(std::size_t(cap[r]) * 36, 0);
        for (int i = 0; i < 4 * cap[r]; ++i) s.ring[r][36 * std::size_t(i / 4) + std::size_t(i & 3)] = t::fbits(-9999.0f);
    }
    put_spark(s.ring[0], 0, 9.9f, 0.5f, 0.2f, 10.0f, 3.0f, 2.0f, 0.0f);     // drawn in every interval
    put_spark(s.ring[0], 1, 9.975f, -1.0f, 0.0f, 20.0f, 0.0f, 5.0f, 1.0f);  // born between frames 1 and 2: interval 0 only
    put_spark(s.ring[0], 5, 8.0f, 0.0f, 0.0f, 10.0f, 1.0f, 1.0f, 0.0f);     // dead (age 2 > 1)
    put_spark(s.ring[0], 6, 9.5f, 50.0f, 0.0f, 10.0f, 1.0f, 1.0f, 0.0f);    // outside the window: culled
    put_spark(s.ring[1], 2, 9.8f, 0.0f, -0.5f, 8.0f, 0.0f, 0.0f, -2.0f);
    return s;
}

}  // namespace

TEST(spark_ee_rebuilds) {
    const b3asset::SparkParams p = params();
    // 100 sparks over lifetimes 1 : 0.5 : 0.5 : 0.25 -> 44.4, 22.2, 22.2, 11.1 -> rounded up to 4: 44, 24, 24, 12
    const auto b = b3asset::spark_bank_batches(p, false);
    CHECK(b.size() == 4);   // 11, 6, 6, 3 blocks: one batch per ring
    if (b.size() != 4) return;
    CHECK(b[0].ring == 0 && b[0].blocks == 11 && b[0].mscalf);
    // packed lifetime: int(1023.999 / 1 x 1) & ~15 = 1008; ITOP = 1008 | 11
    CHECK(b[0].itop == (1008u | 11u));
    CHECK(b.back().ring == 3 && b.back().blocks == 3 && !b.back().mscalf);
    CHECK(b3asset::spark_bank_batches(p, true).empty());
    // RGBAQ: (1 x 64 + .5, .5 x 64 + .5, .25 x 64 + .5, 1 x 128 + .5) truncated = (64, 32, 16, 128), Q = 1.0
    CHECK(b3asset::spark_rgbaq(p, 0) == 0x3F80000080102040ull);
    bool cb = true;
    CHECK(b3asset::spark_array_renderable(p, t::fbits(10.0f), t::fbits(9.5f), t::fbits(-9999.0f), true, &cb) && !cb);
    CHECK(!b3asset::spark_array_renderable(p, t::fbits(10.0f), t::fbits(8.5f), t::fbits(9.5f), false, &cb) && !cb);
    CHECK(b3asset::spark_array_renderable(p, t::fbits(10.0f), t::fbits(8.5f), t::fbits(9.5f), true, &cb) && cb);
}

TEST(spark_intervals_and_frames) {
    Scene s = scene();
    CHECK(b3asset::spark_intervals(s.hist, s.p.w[0x5C / 4]) == 17);
    b3asset::SparkFrame h[18];
    for (int k = 0; k < 18; ++k) h[k] = s.hist[k < 4 ? k : 4];   // the history still filling: frames 4..17 repeat
    CHECK(b3asset::spark_intervals(h, s.p.w[0x5C / 4]) == 4);
    // Set: row 4 = (half_w / sx, half_h / sy, W / 2 / half_w, H / 2 / half_h), sx = 640 / 896, sy = 448 / 704
    CHECK_NEAR(t::bitsf(s.hist[0].row[4][0]), 1.0 / (640.0 / 896.0), 1e-5);
    CHECK_NEAR(t::bitsf(s.hist[0].row[4][2]), 320.0, 1e-4);
    CHECK_NEAR(t::bitsf(s.hist[0].row[4][3]), 0.5 * 448.0 / 0.7, 1e-3);
    CHECK(s.hist[3].row[5][0] == t::fbits(10.0f - 3.0f / 60.0f));
}

TEST(spark_program_model) {
    Scene s = scene();
    const int iv = b3asset::spark_intervals(s.hist, s.p.w[0x5C / 4]);
    const b3asset::SparkVuStatic st = b3asset::spark_vu_static(s.p, t::fbits(1.0f / 1023.0f), iv, s.cam914, s.tag, s.hist);
    std::vector<b3asset::SparkSegment> seg;
    b3asset::spark_segments(st, 0, s.batches[0].itop, s.ring[0].data(), s.batches[0].blocks, seg);
    CHECK(seg.size() == 3);   // sparks 0, 1, 6 in interval 0 (5 is dead)
    if (seg.size() < 3) return;
    CHECK(seg[0].spark == 0 && seg[1].spark == 1);
    // spark 0 at age 0.1: x = 0.5 + 0.3, y = 0.2 + 0.2 - 5 x 0.01 = 0.35
    CHECK_NEAR(t::bitsf(seg[0].head[0]), 0.8, 1e-5);
    CHECK_NEAR(t::bitsf(seg[0].head[1]), 0.35, 1e-5);
    b3asset::spark_segments(st, 1, s.batches[0].itop, s.ring[0].data(), s.batches[0].blocks, seg);
    CHECK(seg.size() == 2);   // spark 1 was born after frame 2 (9.975 > 10 - 2/60)
    std::vector<b3asset::SparkQuad> q;
    std::vector<int> culled;
    b3asset::spark_segments(st, 0, s.batches[0].itop, s.ring[0].data(), s.batches[0].blocks, seg);
    b3asset::spark_project(st, 0, seg, q, &culled);
    CHECK(q.size() == 2 && culled.size() == 1 && culled[0] == 6);
    if (q.empty()) return;
    // corners A +- n, B +- n: |n| = the half width (0.1 m in view space)
    const auto& c = q[0].corner;
    const double nx = (t::bitsf(c[0][0]) - t::bitsf(c[1][0])) / 2, ny = (t::bitsf(c[0][1]) - t::bitsf(c[1][1])) / 2,
                 nz = (t::bitsf(c[0][2]) - t::bitsf(c[1][2])) / 2;
    CHECK_NEAR(std::sqrt(nx * nx + ny * ny + nz * nz), 0.1, 1e-5);
    CHECK(t::bitsf(q[0].v[0].s) == 0.0f && t::bitsf(q[0].v[1].t) == 0.875f && q[0].v[0].adc && !q[0].v[2].adc);
    // the head's pixel: 2048 + 0.8 x 320 / 10 = 2073.6, +- the 0.1 m half width (3.2 px)
    CHECK_NEAR((q[0].v[0].xyz[0] & 0xFFFF) / 16.0, 2073.6, 3.3);
}

#ifdef B3_VU_DIR
TEST(spark_program_vs_vu1_interpreter) {
    std::vector<std::uint8_t> img;
    b3asset::VuMicroprogram prog;
    if (!b3asset::read_file(std::string(B3_VU_DIR) + "/game_0046D090.textbin.bin", img) || !prog.load_textbin(img)) {
        std::printf("    (no spark program image: skipped)\n");
        return;
    }
    Scene s = scene();
    const int iv = b3asset::spark_intervals(s.hist, s.p.w[0x5C / 4]);
    const b3asset::SparkVuStatic st = b3asset::spark_vu_static(s.p, t::fbits(1.0f / 1023.0f), iv, s.cam914, s.tag, s.hist);
    b3asset::VifInterpreter vif;
    b3asset::Vu1 vu(vif.mem);
    std::vector<b3asset::GifPacket> got;
    bool ok = true;
    const auto on_x = [&](u32 qa) { b3asset::GifPacket p; ok &= b3asset::read_gif_packet(vif.mem, qa, p); got.push_back(p); };
    const auto on_kick = [&](const b3asset::VuMemory&, u32 itop, b3asset::VifInterpreter::KickKind k, u32 pc) {
        ok &= vu.run(prog, k == b3asset::VifInterpreter::kMscnt ? vu.tpc : pc, 0, itop, on_x);
    };
    const auto sv = b3asset::spark_vif_static(st);
    CHECK(vif.run(sv.data(), sv.size(), on_kick));
    int quads = 0, verts = 0, bad = 0;
    for (const auto& b : s.batches) {
        got.clear();
        const u32* blocks = s.ring[b.ring].data() + 36 * b.first;
        const auto e = b3asset::spark_vif_batch(b, blocks);
        CHECK(vif.run(e.data(), e.size(), on_kick));
        std::size_t gk = 0;
        for (int k = 0; k < iv; ++k) {
            std::vector<b3asset::SparkSegment> seg;
            std::vector<b3asset::SparkQuad> q;
            b3asset::spark_segments(st, k, b.itop, blocks, b.blocks, seg);
            b3asset::spark_project(st, k, seg, q);
            if (q.empty()) continue;
            if (gk >= got.size() || got[gk].verts.size() != 4 * q.size()) { ++bad; ++gk; continue; }
            for (std::size_t i = 0; i < q.size(); ++i)
                for (int c = 0; c < 4; ++c) {
                    const auto& g = got[gk].verts[4 * i + std::size_t(c)];
                    const auto& w = q[i].v[c];
                    bad += g.x != (w.xyz[0] & 0xFFFF) || g.y != (w.xyz[1] & 0xFFFF) || g.z != ((w.xyz[2] >> 4) & 0xFFFFFF) ||
                           g.s != t::bitsf(w.s) || g.t != t::bitsf(w.t) || g.q != 1.0f || g.adc != w.adc ||
                           g.prim != b3asset::kSparkPrim;
                    ++verts;
                }
            quads += int(q.size());
            ++gk;
        }
        bad += int(got.size() - std::min(got.size(), gk));
    }
    CHECK(ok);
    CHECK(quads == 17);   // spark 0: intervals 0..4 (born at t_6), spark 1: 0, ring 1 spark 2: 0..10
    CHECK(bad == 0);
    std::printf("    program vs model: %d quads, %d vertices, %d differ\n", quads, verts, bad);
}
#endif

TEST_MAIN("test_sparkshade")
