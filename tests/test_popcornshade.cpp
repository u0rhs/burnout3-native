// Popcorn model (b3asset/popcornshade.h): EE rebuilds and the program model on hand-made inputs; with the game's VU
// images (B3_VU_DIR), the real program 0x47C6F0 in the VU1 interpreter against the model on one frame, including the
// pair-0x0046 bug (block 0 particle 1 takes the stale vf05).
#include "test.h"

#include <cmath>
#include <vector>

#include "b3asset/export.h"
#include "b3asset/popcornshade.h"
#include "b3asset/vif.h"
#include "b3asset/vu1.h"

namespace {

using u32 = std::uint32_t;

// Made-up params (not the game's): capacity 100, lifetime 2, size 0.01, colours (1,0.5,0.25,1) -> (0.5,0.5,0.5,0.5),
// fade row (1,1,1,0), far 100, gravity -10, bounce 0.5.
b3asset::PopcornParams params() {
    b3asset::PopcornParams p;
    auto put = [&](int off, float v) { p.w[off / 4] = t::fbits(v); };
    p.w[1] = 100;
    put(0xC, 2.0f); put(0x10, 0.01f);
    put(0x14, 1.0f); put(0x18, 0.5f); put(0x1C, 0.25f); put(0x20, 1.0f);
    put(0x24, 0.5f); put(0x28, 0.5f); put(0x2C, 0.5f); put(0x30, 0.5f);
    put(0x34, 1.0f); put(0x38, 1.0f); put(0x3C, 1.0f); put(0x40, 0.0f);
    put(0x44, 100.0f); put(0x48, -10.0f); put(0x4C, 0.5f);
    return p;
}

struct Frame {
    b3asset::PopcornParams p = params();
    b3asset::PopcornCamera cam;
    u32 scale[4], shift[4], offset[4] = {};
    std::vector<u32> rec;
    b3asset::PopcornVuStatic st;
};

void put_particle(std::vector<u32>& rec, int i, float t, float x, float y, float z, float vx, float vy, float vz) {
    u32* B = rec.data() + 36 * (i / 4);
    B[i & 3] = t::fbits(t);
    const float v[8] = {x, y, z, y, vx, vy, vz, vy};
    for (int k = 0; k < 8; ++k) B[4 + 8 * (i & 3) + k] = t::fbits(v[k]);
}

// RW-style view matrix looking down +z from the origin: X = 0.5 x + 0.5 z, Y = -0.5 y / 0.7 + 0.5 z, depth z.
Frame frame() {
    Frame f;
    const float v[4][3] = {{0.5f, 0, 0}, {0, -0.5f / 0.7f, 0}, {0.5f, 0.5f, 1.0f}, {0, 0, 0}};
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 3; ++l) f.cam.view[r][l] = t::fbits(v[r][l]);
    f.cam.window_x = t::fbits(1.0f);
    f.cam.recip_x = t::fbits(1.0f);
    f.cam.recip_y = t::fbits(1.0f / 0.7f);
    f.cam.near_ = t::fbits(0.3f);
    f.cam.far_ = t::fbits(1000.0f);
    const float sc[4] = {640.0f, 448.0f, 5032309.0f, 0}, sh[4] = {2048.0f, 2048.0f, 1174.5f, 0};
    for (int l = 0; l < 4; ++l) { f.scale[l] = t::fbits(sc[l]); f.shift[l] = t::fbits(sh[l]); }
    f.rec.assign(25 * 36, 0);
    for (int i = 0; i < 100; ++i) f.rec[36 * std::size_t(i / 4) + std::size_t(i & 3)] = t::fbits(-9999.0f);
    put_particle(f.rec, 0, 9.0f, 0.0f, 0.0f, 10.0f, 1.0f, 2.0f, 0.0f);   // age 1: drawn
    put_particle(f.rec, 1, 9.5f, 1.0f, 0.5f, 20.0f, 0.0f, 0.0f, 0.0f);   // block 0 particle 1 (the bug's slot)
    put_particle(f.rec, 2, 7.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f);   // age 3 > 2: dead
    put_particle(f.rec, 3, 10.5f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f);  // not yet born
    put_particle(f.rec, 5, 9.9f, 0.0f, 0.0f, 200.0f, 0.0f, 0.0f, 0.0f);  // beyond the far clip (100)
    for (int i = 80; i < 88; ++i) put_particle(f.rec, i, 9.0f + 0.1f * float(i - 80), -2.0f + 0.5f * float(i - 80), 1.0f, 15.0f, 0, 0, 0);
    f.st = b3asset::popcorn_vu_static(f.p, f.cam, f.p.w[0x48 / 4], f.scale, f.shift, f.offset, t::fbits(10.0f), t::fbits(1.0f / 60));
    return f;
}

}  // namespace

TEST(popcorn_ee_rebuilds) {
    const auto b = b3asset::popcorn_batches(900);   // 225 blocks: 11 x 20 + 5
    CHECK(b.size() == 12 && b[0].mscalf && !b[1].mscalf && b.back().blocks == 5 && b.back().first == 220);
    CHECK(b3asset::popcorn_batches(100).size() == 2);
    const Frame f = frame();
    // zA = (n + f') / (f' - n), f' = min(1000, 100)
    CHECK_NEAR(t::bitsf(f.st.q[1][2]), 100.3 / 99.7, 1e-6);
    CHECK(f.st.q[2][2] == t::fbits(100.0f) && f.st.q[3][2] == t::fbits(100.0f));
    CHECK_NEAR(t::bitsf(f.st.q[8][2]), 0.01 * 1.2, 1e-7);   // size = +0x10 x (0.2 + view window)
    CHECK(f.st.q[9][0] == t::fbits(128.0f) && f.st.q[10][3] == t::fbits(64.0f));
}

TEST(popcorn_program_model) {
    const Frame f = frame();
    const b3asset::PopcornSetup s = b3asset::popcorn_setup(f.st);
    std::vector<b3asset::PopcornParticle> parts;
    b3asset::popcorn_particles(s, f.rec.data(), 25, nullptr, parts);   // all 25 blocks as one list
    CHECK(parts.size() == 3 + 8);   // 0, 1, 5 (dead 2, unborn 3), 80..87
    if (parts.empty()) return;
    // particle 0 at age 1: x = 1, y = 2 - 5 = -3 (g/2 = -5), z = 10; colour c0 + (c1 - c0) x 1/4
    CHECK_NEAR(t::bitsf(parts[0].pos[0]), 1.0, 1e-6);
    CHECK_NEAR(t::bitsf(parts[0].pos[1]), -3.0, 1e-5);
    CHECK_NEAR(t::bitsf(parts[0].colour[0]), 128.0 - 64.0 / 4, 1e-3);
    std::vector<b3asset::PopcornSprite> spr;
    std::vector<int> culled;
    b3asset::PopcornStState st = b3asset::popcorn_st_start(s);
    b3asset::popcorn_project(s, parts, st, spr, &culled);
    CHECK(culled.size() == 1 && culled[0] == 5);
    CHECK(spr.size() == 10);
    if (spr.size() < 3) return;
    // ST: (0,0)-(1,1), then (1,1)-(0,0), alternating
    CHECK(spr[0].st[0][0] == 0 && spr[0].st[1][0] == t::fbits(1.0f) && spr[1].st[1][0] == 0);
    // depth fade on alpha only (row (1,1,1,0)): 1 - d / 100 at d = 10
    CHECK_NEAR(spr[0].rgba[3], (128.0 - 64.0 / 4) * 0.9, 1.0);
    CHECK_NEAR(b3asset::popcorn_view_half_extent(s, 16.0f), 0.012 * 4, 1e-6);
}

#ifdef B3_VU_DIR
TEST(popcorn_program_vs_vu1_interpreter) {
    std::vector<std::uint8_t> img;
    b3asset::VuMicroprogram prog;
    if (!b3asset::read_file(std::string(B3_VU_DIR) + "/game_0047C6F0.textbin.bin", img)) {
        std::printf("    (no popcorn program image: skipped)\n");
        return;
    }
    img.resize(std::min<std::size_t>(img.size(), 1728));
    CHECK(prog.load_textbin(img));
    const Frame f = frame();
    const b3asset::PopcornSetup s = b3asset::popcorn_setup(f.st);
    b3asset::VifInterpreter vif;
    b3asset::Vu1 vu(vif.mem);
    // a sentinel in vf05: what the program uses for block 0 particle 1 (pair 0x0046 loads vf06 instead)
    const float sentinel[4] = {0.0f, 0.0f, 12.0f, 0.0f};
    for (int l = 0; l < 4; ++l) vu.vf[5][l] = t::fbits(sentinel[l]);
    std::vector<b3asset::GifPacket> got;
    bool ok = true;
    const auto on_x = [&](u32 qa) { b3asset::GifPacket p; ok &= b3asset::read_gif_packet(vif.mem, qa, p); got.push_back(p); };
    u32 vf05[4] = {};
    const auto on_kick = [&](const b3asset::VuMemory&, u32 itop, b3asset::VifInterpreter::KickKind k, u32 pc) {
        std::memcpy(vf05, vu.vf[5], sizeof vf05);
        ok &= vu.run(prog, k == b3asset::VifInterpreter::kMscnt ? vu.tpc : pc, 0, itop, on_x);
    };
    const auto sv = b3asset::popcorn_vif_static(f.st);
    CHECK(vif.run(sv.data(), sv.size(), on_kick));
    b3asset::PopcornStState st = b3asset::popcorn_st_start(s);
    int sprites = 0, bad = 0, bug = 0;
    for (const auto& b : b3asset::popcorn_batches(100)) {
        got.clear();
        const u32* blocks = f.rec.data() + 36 * b.first;
        const auto e = b3asset::popcorn_vif_batch(b, blocks);
        CHECK(vif.run(e.data(), e.size(), on_kick));
        std::vector<b3asset::PopcornParticle> parts, own;
        b3asset::popcorn_particles(s, blocks, b.blocks, vf05, parts);
        b3asset::popcorn_particles(s, blocks, b.blocks, nullptr, own);
        if (b.first == 0) bug += parts[1].pos[2] != own[1].pos[2];   // the sentinel's z (12) vs the particle's (20)
        std::vector<b3asset::PopcornSprite> spr;
        b3asset::popcorn_project(s, parts, st, spr);
        if (spr.empty()) { bad += !got.empty(); continue; }
        if (got.size() != 1 || got[0].verts.size() != 2 * spr.size()) { ++bad; continue; }
        for (std::size_t i = 0; i < spr.size(); ++i)
            for (int k = 0; k < 2; ++k) {
                const auto& g = got[0].verts[2 * i + std::size_t(k)];
                bad += g.x != (spr[i].xyz[k][0] & 0xFFFF) || g.y != (spr[i].xyz[k][1] & 0xFFFF) ||
                       g.z != ((spr[i].xyz[k][2] >> 4) & 0xFFFFFF) || g.prim != b3asset::kPopcornPrim;
                if (k == 1)
                    bad += g.s != t::bitsf(spr[i].st[1][0]) || g.r != u32(spr[i].rgba[0] & 0xFF) || g.a != u32(spr[i].rgba[3] & 0xFF);
            }
        sprites += int(spr.size());
    }
    CHECK(ok);
    CHECK(bug == 1);       // the program drew block 0 particle 1 from vf05, not from its record
    CHECK(sprites == 9);   // 10 less particle 81: block 0 lane 1 of batch 2 takes the zero vf05 batch 1 prefetched (culled)
    CHECK(bad == 0);
    std::printf("    program vs model: %d sprites, %d differ; block 0 particle 1 taken from vf05: %d\n", sprites, bad, bug);
}
#endif

TEST_MAIN("test_popcornshade")
