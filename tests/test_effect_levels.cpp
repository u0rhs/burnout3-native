// effects2: the particle effects' levels (src/b3replay/sparks.h): the spark and popcorn builds on hand-made captures at
// every level. High (3) is every particle, the output of the build before the levels existed; Low / Medium draw a stable,
// nested share; Off draws none. Nothing here reads a game file.
#include "test.h"

#include <cstring>
#include <memory>
#include <set>
#include <vector>

#include "b3asset/popcornshade.h"
#include "b3asset/sparkshade.h"
#include "capture.h"
#include "sparks.h"

namespace {

using u32 = std::uint32_t;

// A camera record looking down +z from the origin (as test_sparkshade's)
void camera(std::uint8_t rec[0x230], float shift_z) {
    std::memset(rec, 0, 0x230);
    auto put = [&](int off, float v) { std::memcpy(rec + off, &v, 4); };
    put(0xE0 + 0x00, 0.5f);
    put(0xE0 + 0x14, -0.5f / 0.7f);
    put(0xE0 + 0x20, 0.5f);
    put(0xE0 + 0x24, 0.5f);
    put(0xE0 + 0x28, 1.0f);
    put(0xE0 + 0x38, -shift_z);
    put(0xE0 + 0x30, -0.5f * shift_z);
    put(0xE0 + 0x34, -0.5f * shift_z);
    put(0x168, 5032309.0f); put(0x170, 2048.0f); put(0x174, 2048.0f); put(0x178, 1174.5f);
    put(0x18C, 1000.0f); put(0x19C, 0.5f);
    put(0x200, 1.0f); put(0x204, 0.7f); put(0x208, 1.0f); put(0x20C, 1.0f / 0.7f);
    put(0x218, 640.0f); put(0x21C, 448.0f);
}

void put_spark(std::vector<u32>& ring, int i, float t, float x, float y, float z, float vx, float vy, float vz) {
    u32* B = ring.data() + 36 * (i / 4);
    const int j = i & 3;
    B[j] = t::fbits(t);
    const float v[8] = {x, y, z, y, vx, vy, vz, vy};
    for (int k = 0; k < 8; ++k) B[4 + 8 * j + k] = t::fbits(v[k]);
}

// One spark array with every slot of ring 0 alive and inside the window (`n` sparks in a grid)
struct Sparks {
    b3replay::ContactFx c;
    std::uint32_t cam914[4] = {};
    int alive = 0;
    Sparks() {
        b3asset::SparkParams p;
        auto putp = [&](int off, float v) { p.w[off / 4] = t::fbits(v); };
        for (int r = 0; r < 4; ++r) { putp(0x10 * r, 1.0f); putp(0x10 * r + 4, 0.5f); putp(0x10 * r + 8, 0.25f); putp(0x10 * r + 12, 1.0f); }
        putp(0x40, 1.0f); putp(0x44, 0.5f); putp(0x48, 0.5f); putp(0x4C, 0.25f);
        putp(0x50, -10.0f); putp(0x54, 0.5f); putp(0x58, 0.1f); putp(0x5C, 0.05f);
        p.w[0x60 / 4] = 100;
        p.w[0x64 / 4] = 0;
        putp(0x6C, 0.0f); putp(0x70, 0.0f); putp(0x74, 1.0f); putp(0x78, 0.875f);
        auto history = std::make_shared<b3replay::SparkHistory>();
        std::uint8_t rec[0x230];
        for (int k = 0; k < 18; ++k) {
            camera(rec, -0.1f * float(k));
            const b3asset::SparkFrame f = b3asset::spark_frame_set(rec, 10.0f - float(k) / 60.0f);
            std::memcpy(history->data() + 0x60 * k, f.row, 0x60);
            if (k == 0) b3asset::spark_cam914_from_record(rec, cam914);
        }
        auto st = std::make_shared<b3replay::SparkArrayState>();
        std::memcpy(st->params, p.w, 0x7C);
        const auto batches = b3asset::spark_bank_batches(p, false);
        int cap = 0;
        for (const auto& b : batches) if (b.ring == 0) cap = b.first + b.blocks;
        std::vector<u32> ring(std::size_t(cap) * 36, 0);
        for (int i = 0; i < 4 * cap; ++i) ring[36 * std::size_t(i / 4) + std::size_t(i & 3)] = t::fbits(-9999.0f);
        for (int i = 0; i < 4 * cap; ++i) {   // a grid inside the window, all born 0.1 s ago
            put_spark(ring, i, 9.9f, -1.5f + 0.3f * float(i % 10), -0.5f + 0.2f * float(i / 10), 10.0f + 0.1f * float(i), 1.0f, 2.0f, 0.0f);
            ++alive;
        }
        st->rec[0][0].resize(ring.size() * 4);
        std::memcpy(st->rec[0][0].data(), ring.data(), ring.size() * 4);
        const float last = 10.0f;
        std::memcpy(st->head + 0x44, &last, 4);
        const float a0 = 1.0f / 1023.0f;
        std::memcpy(st->head + 0xA0, &a0, 4);
        c.sparks_valid = true;
        c.history_valid = true;
        c.history = history;
        c.sparks[0] = st;
        const u32 tag[4] = {0, 0x802A4000u, 0x42424242u, 0};
        std::memcpy(c.spark_d1e31a20, tag, 16);
    }
    int quads(int level, std::vector<b3gfx::Vertex>* out = nullptr) const {
        std::vector<b3gfx::Vertex> v;
        std::vector<u32> i;
        b3replay::SparkStats s;
        b3replay::build_sparks(c, cam914, v, i, &s, level);
        if (out) *out = v;
        CHECK(v.size() == 4 * std::size_t(s.quads) && i.size() == 6 * std::size_t(s.quads));
        return s.quads;
    }
};

}  // namespace

TEST(particle_share_is_stable_nested_and_the_right_size) {
    using b3replay::particle_id;
    using b3replay::particle_kept;
    int n[4] = {};
    for (std::uint32_t a = 0; a < 6; ++a)
        for (std::uint32_t b = 0; b < 2; ++b)
            for (std::uint32_t slot = 0; slot < 2000; ++slot) {
                const std::uint32_t id = particle_id(a, b, slot);
                CHECK(id == particle_id(a, b, slot));            // the same particle, the same answer: no flicker
                for (int l = 0; l <= 3; ++l) n[l] += particle_kept(l, id) ? 1 : 0;
                CHECK(!particle_kept(0, id) && particle_kept(3, id));
                CHECK(!particle_kept(1, id) || particle_kept(2, id));   // Low is part of Medium
            }
    const int total = 6 * 2 * 2000;
    CHECK(n[0] == 0 && n[3] == total);
    CHECK(n[1] > total * 22 / 100 && n[1] < total * 28 / 100);   // about a quarter
    CHECK(n[2] > total * 47 / 100 && n[2] < total * 53 / 100);   // about a half
    // neighbouring slots do not go together (a run of 4 sparks of one block is not all kept or all dropped)
    int runs_kept4 = 0;
    for (std::uint32_t slot = 0; slot < 4000; slot += 4) {
        int k = 0;
        for (std::uint32_t j = 0; j < 4; ++j) k += particle_kept(1, particle_id(0, 0, slot + j)) ? 1 : 0;
        if (k == 4) ++runs_kept4;
    }
    CHECK(runs_kept4 < 40);   // a quarter ^ 4 of 1000 blocks is about 4
}

TEST(particle_layer_is_one_step_lighter_than_the_setting_unless_full) {
    using b3replay::particle_layer_level;
    CHECK(particle_layer_level(0, false) == 0 && particle_layer_level(0, true) == 0);
    CHECK(particle_layer_level(1, false) == 1 && particle_layer_level(2, false) == 1);
    CHECK(particle_layer_level(3, false) == 2);   // the default High: a half of the particles
    CHECK(particle_layer_level(3, true) == 3 && particle_layer_level(2, true) == 2);
}

TEST(spark_quads_per_level) {
    Sparks s;
    const int off = s.quads(0), low = s.quads(1), med = s.quads(2), high = s.quads(3);
    CHECK(off == 0);
    CHECK(high > 100);                                  // the scene draws plenty
    CHECK(low < med && med < high);
    CHECK(low * 5 > high && low * 3 < high);            // about a quarter
    CHECK(med * 3 > high * 1 && med * 3 < high * 2);    // about a half
    std::vector<b3gfx::Vertex> vh, vm, vl;
    s.quads(3, &vh);
    s.quads(2, &vm);
    s.quads(1, &vl);
    // High is the unfiltered build: level 3 twice gives the same vertices, and every Low / Medium quad is one of High's
    std::vector<b3gfx::Vertex> vh2;
    s.quads(3, &vh2);
    CHECK(vh.size() == vh2.size() && std::memcmp(vh.data(), vh2.data(), vh.size() * sizeof(b3gfx::Vertex)) == 0);
    // every Low / Medium quad is one of High's: the same centre line (the head and the tail between the corner pairs); the kept
    // sparks are drawn wider (spark_width_scale), High's width is untouched
    auto centre = [](const b3gfx::Vertex& a, const b3gfx::Vertex& b, int k) { return std::lround(double(a.pos[k] + b.pos[k]) * 500.0); };
    auto quad_set = [&](const std::vector<b3gfx::Vertex>& v) {
        std::set<std::vector<long>> q;
        for (std::size_t i = 0; i + 3 < v.size(); i += 4)
            q.insert({centre(v[i], v[i + 1], 0), centre(v[i], v[i + 1], 1), centre(v[i], v[i + 1], 2), centre(v[i + 2], v[i + 3], 0),
                      centre(v[i + 2], v[i + 3], 1), centre(v[i + 2], v[i + 3], 2)});
        return q;
    };
    const auto sh = quad_set(vh), sm = quad_set(vm), sl = quad_set(vl);
    for (const auto& q : sl) CHECK(sm.count(q) == 1);
    for (const auto& q : sm) CHECK(sh.count(q) == 1);
    auto width = [](const std::vector<b3gfx::Vertex>& v, std::size_t i) {   // the corner pair's distance at the head, i = the quad
        double d = 0;
        for (int k = 0; k < 3; ++k) d += double(v[4 * i].pos[k] - v[4 * i + 1].pos[k]) * double(v[4 * i].pos[k] - v[4 * i + 1].pos[k]);
        return std::sqrt(d);
    };
    // the first quad of Low is some quad of High, 1.6 times as wide
    bool wider = false;
    for (std::size_t i = 0; i < vh.size() / 4 && !wider; ++i) {
        const std::vector<long> hk = {centre(vh[4 * i], vh[4 * i + 1], 0), centre(vh[4 * i], vh[4 * i + 1], 1), centre(vh[4 * i], vh[4 * i + 1], 2),
                                      centre(vh[4 * i + 2], vh[4 * i + 3], 0), centre(vh[4 * i + 2], vh[4 * i + 3], 1), centre(vh[4 * i + 2], vh[4 * i + 3], 2)};
        const std::vector<long> lk = {centre(vl[0], vl[1], 0), centre(vl[0], vl[1], 1), centre(vl[0], vl[1], 2), centre(vl[2], vl[3], 0),
                                      centre(vl[2], vl[3], 1), centre(vl[2], vl[3], 2)};
        if (hk == lk) {
            wider = true;
            CHECK_NEAR(width(vl, 0) / width(vh, i), double(b3replay::spark_width_scale(1)), 1e-3);
        }
    }
    CHECK(wider);
    CHECK(b3replay::spark_width_scale(3) == 1.0f && b3replay::debris_size_scale(3) == 1.0f);
    CHECK(b3replay::spark_width_scale(1) > b3replay::spark_width_scale(2) && b3replay::spark_width_scale(2) > 1.0f);
    // the same level again: the same quads (nothing random between builds)
    std::vector<b3gfx::Vertex> vl2;
    s.quads(1, &vl2);
    CHECK(vl.size() == vl2.size() && std::memcmp(vl.data(), vl2.data(), vl.size() * sizeof(b3gfx::Vertex)) == 0);
}

namespace {

b3replay::Camera game_camera() {
    b3replay::Camera g;
    g.valid = true;
    const float v[4][3] = {{0.5f, 0, 0}, {0, -0.5f / 0.7f, 0}, {0.5f, 0.5f, 1.0f}, {0, 0, 0}};
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 3; ++l) g.screen[r][l] = v[r][l];
    g.half_w = 1.0f;
    g.half_h = 0.7f;
    g.znear = 0.3f;
    g.zfar = 1000.0f;
    return g;
}

}  // namespace

TEST(popcorn_sprites_per_level) {
    b3replay::ContactFx c;
    auto st = std::make_shared<b3replay::PopcornArrayState>();
    const int cap = 400;
    std::vector<u32> rec(std::size_t(cap / 4) * 36, 0);
    for (int i = 0; i < cap; ++i) rec[36 * std::size_t(i / 4) + std::size_t(i & 3)] = t::fbits(-9999.0f);
    for (int i = 0; i < cap; ++i) {
        u32* B = rec.data() + 36 * (i / 4);
        B[i & 3] = t::fbits(9.0f + 0.001f * float(i % 50));
        const float x = -3.0f + 0.2f * float(i % 30), y = 0.5f + 0.1f * float(i / 30), z = 15.0f + 0.05f * float(i % 7);
        const float v[8] = {x, y, z, y, 1.0f, 2.0f, 0.0f, 2.0f};
        for (int k = 0; k < 8; ++k) B[4 + 8 * (i & 3) + k] = t::fbits(v[k]);
    }
    st->rec.resize(rec.size() * 4);
    std::memcpy(st->rec.data(), rec.data(), rec.size() * 4);
    const std::uint32_t prm = 0x4A4DB0;
    std::memcpy(st->head + 0x30, &prm, 4);
    const std::uint32_t capy = cap;
    std::memcpy(st->head + 0x24, &capy, 4);
    const float grav = -10.0f;
    std::memcpy(st->head + 0x10, &grav, 4);
    b3asset::PopcornParams p;
    auto putp = [&](int off, float v) { p.w[off / 4] = t::fbits(v); };
    p.w[1] = cap;
    putp(0xC, 2.0f); putp(0x10, 0.01f);
    putp(0x14, 1.0f); putp(0x18, 0.5f); putp(0x1C, 0.25f); putp(0x20, 1.0f);
    putp(0x24, 0.5f); putp(0x28, 0.5f); putp(0x2C, 0.5f); putp(0x30, 0.5f);
    putp(0x34, 1.0f); putp(0x38, 1.0f); putp(0x3C, 1.0f); putp(0x40, 0.0f);
    putp(0x44, 100.0f); putp(0x48, -10.0f); putp(0x4C, 0.5f);
    std::memcpy(c.popcorn_params, p.w, sizeof p.w);
    c.popcorn_valid = true;
    c.popcorn[0] = st;
    const b3replay::Camera game = game_camera();
    b3replay::Camera view;
    view.valid = true;
    view.m.r[0][0] = -1; view.m.r[1][1] = 1; view.m.r[2][2] = 1;
    std::uint32_t trail_cam[10][4] = {};
    const float sc[4] = {640.0f, 448.0f, 5032309.0f, 0}, sh[4] = {2048.0f, 2048.0f, 1174.5f, 0};
    for (int l = 0; l < 4; ++l) { trail_cam[4][l] = t::fbits(sc[l]); trail_cam[5][l] = t::fbits(sh[l]); }
    int n[4] = {};
    std::vector<b3gfx::Vertex> vs[4];
    for (int l = 0; l <= 3; ++l) {
        std::vector<std::uint32_t> idx;
        std::vector<b3replay::PopcornDraw> draws;
        n[l] = b3replay::build_popcorn(c, game, trail_cam, 10.0f, view, vs[l], idx, draws, l);
        CHECK(vs[l].size() == 4 * std::size_t(n[l]) && idx.size() == 6 * std::size_t(n[l]));
    }
    CHECK(n[0] == 0);
    CHECK(n[3] > 100);
    CHECK(n[1] < n[2] && n[2] < n[3]);
    CHECK(n[1] * 5 > n[3] && n[1] * 3 < n[3]);
    CHECK(n[2] * 3 > n[3] && n[2] * 3 < n[3] * 2);
    // a thinned sprite is High's, a little larger: the same centre (its four corners' mean)
    auto centres = [](const std::vector<b3gfx::Vertex>& v) {
        std::set<std::vector<long>> c;
        for (std::size_t i = 0; i + 3 < v.size(); i += 4) {
            std::vector<long> k;
            for (int a = 0; a < 3; ++a) k.push_back(std::lround(double(v[i].pos[a] + v[i + 1].pos[a] + v[i + 2].pos[a] + v[i + 3].pos[a]) * 250.0));
            c.insert(k);
        }
        return c;
    };
    const auto ch = centres(vs[3]);
    for (const auto& k : centres(vs[1])) CHECK(ch.count(k) == 1);
    for (const auto& k : centres(vs[2])) CHECK(ch.count(k) == 1);
    CHECK(b3replay::debris_size_scale(1) > b3replay::debris_size_scale(2) && b3replay::debris_size_scale(2) > 1.0f);
}

TEST_MAIN("effect levels (effects2)")
