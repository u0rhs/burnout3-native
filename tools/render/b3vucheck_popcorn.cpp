// b3vucheck_popcorn: offline VU reference check of the popcorn model (include/b3asset/popcornshade.h).
//
// Runs the game's popcorn microprogram VU1 0x47C6F0 (the image's first 1728 B) in the b3asset VU interpreter with the
// inputs the EE side sends -- the VIF stream of CB3PopcornArray::Render 0x365920 (VU 1008..1023, BASE 0 / OFFSET 0xB4)
// and the array's static chain (Construct 0x366160: per <= 20 blocks UNPACK V4-32 9n to TOPS, ITOP n, MSCALF 0 /
// MSCNT) -- and compares every XGKICKed sprite with popcorn_particles() + popcorn_project(). The vf05 the program
// carries into each batch (the pair 0x0046 bug, popcornshade.h) is read from the interpreter before the batch runs and
// handed to the model; the run also counts the sprites that differ when the model uses the particle's own position.
// Synthetic frames: the four arrays with the retail CB3PopcornArrayBase::maStandardParams (read from the retail ELF,
// not stored here), particles spawned the way SpawnBouncyPopcorn 0x365E80 does, an RW-style camera (view matrix with
// x/z, y/z in 0..1) from b3vucheck_trail's chase camera. Captured frames (--capture): every tick of a scene capture with
// ContactFx::popcorn_valid and a live particle; the camera inputs are APPROXIMATED from Tick::cam (CGtViewport +0x00
// world -> screen rows as the RwCamera view matrix, 1 / half_w, 1 / half_h, half_w, znear, zfar) and Tick::trail_cam
// (record +0x160 / +0x170 as vu1DataXYZScale / Shift, offset 0) until the capture holds the RwCamera and vu1Data*;
// the program-vs-model comparison is exact either way (both sides get the same words). Nothing is written.
//
//   b3vucheck_popcorn [--frames n] [--seed n] [--elf SLUS_210.50] [--vu dir] [--dump n]
//   b3vucheck_popcorn --capture <scene capture with the fx2 contact block> [--every n] [--dump n]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "b3asset/export.h"
#include "b3asset/popcornshade.h"
#include "b3asset/trackshade.h"
#include "b3asset/vif.h"
#include "b3asset/vu1.h"
#include "../../src/b3replay/capture.h"

using namespace b3asset;

namespace {

int g_dump = 0;
std::uint32_t fbits(float f) { std::uint32_t v; std::memcpy(&v, &f, 4); return v; }
float bitsf(std::uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }
std::uint32_t rd32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }

struct Stat {
    double max_err = 0;
    std::uint64_t n = 0, bad = 0;
    void add(double e, double tol) { ++n; max_err = std::max(max_err, e); if (e > tol) ++bad; }
};
std::map<std::string, Stat> g_stats;
void cmp(const std::string& what, double got, double want, double tol) { g_stats[what].add(std::fabs(got - want), tol); }
struct Totals {
    std::uint64_t frames = 0, arrays = 0, batches = 0, kicks = 0, particles = 0, culled = 0, sprites = 0, vertices = 0;
    std::uint64_t bug_sprites_differ = 0, bug_particles = 0, p_pending = 0, q_pending = 0, ticks_live = 0;
};
Totals g_tot;

struct ArrayIn {
    PopcornParams p;
    std::uint32_t g = 0;
    std::vector<PopcornBatch> batches;
    std::vector<std::uint32_t> rec;   // 36 words per block
};
struct FrameIn {
    PopcornCamera cam;
    std::uint32_t scale[4] = {}, shift[4] = {}, offset[4] = {};
    std::uint32_t now = 0, step = 0;
    std::vector<ArrayIn> arrays;
};

bool same_sprite(const PopcornSprite& a, const PopcornSprite& b) {
    return std::memcmp(a.xyz, b.xyz, sizeof a.xyz) == 0 && std::memcmp(a.rgba, b.rgba, sizeof a.rgba) == 0 && a.index == b.index;
}

void dump_kick(const GifPacket& p, const std::vector<PopcornSprite>& want) {
    std::printf(" kick: %zu verts, model %zu sprites\n", p.verts.size(), want.size());
    for (std::size_t i = 0; i < p.verts.size(); ++i) {
        const GifVertex& v = p.verts[i];
        std::printf("  v%-3zu prim %03x rgba %3u %3u %3u %3u st %g %g q %g xy %9.4f %9.4f z %8u\n", i, v.prim, v.r, v.g, v.b,
                    v.a, v.s, v.t, v.q, v.x / 16.0 - 2048, v.y / 16.0 - 2048, v.z);
    }
    for (const auto& s : want)
        std::printf("  m%-3d rgba %d %d %d %d  (%9.4f %9.4f) - (%9.4f %9.4f) z %u\n", s.index, s.rgba[0], s.rgba[1], s.rgba[2],
                    s.rgba[3], (s.xyz[0][0] & 0xFFFF) / 16.0 - 2048, (s.xyz[0][1] & 0xFFFF) / 16.0 - 2048,
                    (s.xyz[1][0] & 0xFFFF) / 16.0 - 2048, (s.xyz[1][1] & 0xFFFF) / 16.0 - 2048, (s.xyz[1][2] >> 4) & 0xFFFFFF);
}

bool check_frame(const VuMicroprogram& prog, const FrameIn& f) {
    VifInterpreter vif;
    Vu1 vu(vif.mem);
    std::vector<GifPacket> got;
    std::string err;
    bool ok = true;
    std::uint32_t vf05[4] = {};
    const auto on_xgkick = [&](std::uint32_t qa) {
        GifPacket p;
        std::string e2;
        if (!read_gif_packet(vif.mem, qa, p, &e2)) std::fprintf(stderr, "GIF: %s\n", e2.c_str());
        got.push_back(std::move(p));
    };
    const auto on_kick = [&](const VuMemory&, std::uint32_t itop, VifInterpreter::KickKind kind, std::uint32_t pc) {
        if (!ok) return;
        std::memcpy(vf05, vu.vf[5], sizeof vf05);   // what the batch's prologue will use for block 0 particle 1
        const std::uint32_t start = kind == VifInterpreter::kMscnt ? vu.tpc : pc;
        if (!vu.run(prog, start, 0, itop, on_xgkick, &err)) { std::fprintf(stderr, "VU1 0x47C6F0: %s\n", err.c_str()); ok = false; }
    };
    for (const ArrayIn& a : f.arrays) {
        ++g_tot.arrays;
        const PopcornVuStatic st = popcorn_vu_static(a.p, f.cam, a.g, f.scale, f.shift, f.offset, f.now, f.step);
        const PopcornSetup su = popcorn_setup(st);
        const std::vector<std::uint8_t> s = popcorn_vif_static(st);
        if (!vif.run(s.data(), s.size(), on_kick, &err)) { std::fprintf(stderr, "VIF: %s\n", err.c_str()); return false; }
        PopcornStState stf = popcorn_st_start(su);
        for (const PopcornBatch& b : a.batches) {
            ++g_tot.batches;
            if (std::size_t(b.first + b.blocks) * 36 > a.rec.size()) { std::fprintf(stderr, "batch past the records\n"); return false; }
            const std::uint32_t* blocks = a.rec.data() + 36 * b.first;
            got.clear();
            const std::vector<std::uint8_t> e = popcorn_vif_batch(b, blocks);
            if (!vif.run(e.data(), e.size(), on_kick, &err)) { std::fprintf(stderr, "VIF: %s\n", err.c_str()); return false; }
            if (!ok) return false;
            std::vector<PopcornParticle> parts, own;
            popcorn_particles(su, blocks, b.blocks, vf05, parts);
            popcorn_particles(su, blocks, b.blocks, nullptr, own);
            PopcornStState st_own = stf;
            std::vector<PopcornSprite> spr, spr_own;
            std::vector<int> culled;
            popcorn_project(su, parts, stf, spr, &culled);
            popcorn_project(su, own, st_own, spr_own, nullptr);
            g_tot.particles += parts.size();
            g_tot.culled += culled.size();
            // the bug's effect: block 0 particle 1 drawn differently (or drawn / not drawn) with its own position
            {
                const PopcornSprite *x = nullptr, *y = nullptr;
                for (const auto& q : spr) if (q.index == 1) x = &q;
                for (const auto& q : spr_own) if (q.index == 1) y = &q;
                for (const auto& q : parts) if (q.index == 1) ++g_tot.bug_particles;
                if ((x == nullptr) != (y == nullptr) || (x && !same_sprite(*x, *y))) ++g_tot.bug_sprites_differ;
            }
            cmp("kicks", double(got.size()), spr.empty() ? 0 : 1, 0);
            if (spr.empty() || got.empty()) continue;
            const GifPacket& p = got[0];
            ++g_tot.kicks;
            cmp("sprite count", double(p.verts.size()), double(2 * spr.size()), 0);
            if (p.verts.size() != 2 * spr.size()) { if (g_dump > 0) { --g_dump; dump_kick(p, spr); } continue; }
            bool bad = false;
            for (std::size_t i = 0; i < spr.size(); ++i) {
                ++g_tot.sprites;
                const PopcornSprite& w = spr[i];
                for (int k = 0; k < 2; ++k) {
                    const GifVertex& g = p.verts[2 * i + std::size_t(k)];
                    ++g_tot.vertices;
                    const std::uint32_t X = w.xyz[k][0] & 0xFFFF, Y = w.xyz[k][1] & 0xFFFF, Z = (w.xyz[k][2] >> 4) & 0xFFFFFF;
                    cmp("x (1/16 px)", g.x, X, 0);
                    cmp("y (1/16 px)", g.y, Y, 0);
                    cmp("z", g.z, Z, 0);
                    cmp("prim", g.prim, kPopcornPrim, 0);
                    cmp("adc", g.adc, 0, 0);
                    cmp("fog", g.f, 0, 0);
                    bad |= g.x != X || g.y != Y || g.z != Z;
                    // ST / RGBA in force at the vertex: corner 0 of a packet's first sprite carries the GS state from before
                    // the packet (the GIF reader starts from zero), so it is compared from the second sprite on
                    if (k == 1 || i > 0) {
                        cmp("s", g.s, bitsf(w.st[k][0]), 0);
                        cmp("t", g.t, bitsf(w.st[k][1]), 0);
                        cmp("q", g.q, 1.0, 0);
                        bad |= g.s != bitsf(w.st[k][0]) || g.t != bitsf(w.st[k][1]);
                    }
                    if (k == 1) {   // the sprite's colour (flat: the second vertex's RGBAQ)
                        cmp("r", g.r, w.rgba[0] & 0xFF, 0);
                        cmp("g", g.g, w.rgba[1] & 0xFF, 0);
                        cmp("b", g.b, w.rgba[2] & 0xFF, 0);
                        cmp("a", g.a, w.rgba[3] & 0xFF, 0);
                        bad |= g.r != (w.rgba[0] & 0xFF) || g.g != (w.rgba[1] & 0xFF) || g.b != (w.rgba[2] & 0xFF) || g.a != (w.rgba[3] & 0xFF);
                    }
                }
            }
            if (bad && g_dump > 0) { --g_dump; dump_kick(p, spr); }
            cmp("kicks per batch", double(got.size()), 1, 0);
        }
    }
    g_tot.p_pending += vu.stats.p_read_pending;
    g_tot.q_pending += vu.stats.q_read_pending;
    ++g_tot.frames;
    return true;
}

// ---- synthetic frames
struct V3 { float x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

// SpawnBouncyPopcorn 0x365E80's record (host float) [L nonmatching source].
void spawn(std::uint32_t* blk, int lane, V3 pos, V3 vel, float t, float h, float g, float k14, float k18) {
    float vy = vel.y;
    if (vy < 0 && h <= 0) vy = -vy;
    float pw = pos.y, vw = vel.y;
    const float d = vy * vy - g * h;
    if (d > 0) {
        const float tt = k14 * (std::sqrt(d) + vy), gt = g * tt;
        const float nv = k18 * (vy + gt) - gt;
        pw = ((pos.y - h) - nv * tt) - 0.5f * g * tt * tt;
        vw = nv;
    }
    blk[lane] = fbits(t);
    std::uint32_t* P = blk + 4 + 8 * lane;
    const float pv[8] = {pos.x, pos.y, pos.z, pw, vel.x, vel.y, vel.z, vw};
    for (int i = 0; i < 8; ++i) P[i] = fbits(pv[i]);
}

FrameIn make_frame(std::mt19937& rng, const PopcornParams params[4]) {
    std::uniform_real_distribution<float> U(-1, 1), P(0, 1);
    FrameIn f;
    const float now = 100.0f + 400.0f * P(rng);
    const V3 car = {1500.0f * U(rng), 30.0f * U(rng), 1500.0f * U(rng)};
    const float yaw = 3.14159f * U(rng);
    const V3 fwd = {std::sin(yaw), 0, std::cos(yaw)}, side = {std::cos(yaw), 0, -std::sin(yaw)};
    const V3 eye = car - fwd * (4.0f + 6.0f * P(rng)) + V3{0, 1.0f + 3.0f * P(rng), 0}, target = car + fwd * 15.0f;
    const float e3[3] = {eye.x, eye.y, eye.z}, t3[3] = {target.x, target.y, target.z}, sun[3] = {0, 1, 0};
    const ShadeScene sc = shade_scene_look_at(e3, t3, sun);
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 3; ++l) f.cam.view[r][l] = fbits(sc.view[r][l]);
    const float tx = std::tan(0.5236f), ty = tx * 448.0f / 640.0f;
    f.cam.window_x = fbits(tx);
    f.cam.recip_x = fbits(1.0f / tx);
    f.cam.recip_y = fbits(1.0f / ty);
    f.cam.near_ = fbits(0.3f);
    f.cam.far_ = fbits(P(rng) < 0.5f ? 1000.0f : 100.0f);
    const float sc4[4] = {640.0f, 448.0f, 5032309.0f, 0}, sh4[4] = {2048.0f, 2048.0f, 1174.5f, 0};
    for (int l = 0; l < 4; ++l) { f.scale[l] = fbits(sc4[l]); f.shift[l] = fbits(sh4[l]); }
    f.offset[0] = fbits(0.5f * U(rng));
    f.offset[1] = fbits(0.5f * U(rng));
    f.now = fbits(now);
    f.step = fbits(1.0f / 30);
    for (int a = 0; a < 4; ++a) {
        if (P(rng) < 0.3f) continue;
        ArrayIn ar;
        ar.p = params[a];
        ar.g = ar.p.w[0x48 / 4];   // Construct: array +0x10 = params +0x48
        const int capy = int(ar.p.w[1]);
        ar.batches = popcorn_batches(capy);
        ar.rec.assign(std::size_t(capy / 4) * 36, 0);
        const float g = ar.p.f(0x48), k14 = -1.0f / g, k18 = -ar.p.f(0x4C), life = ar.p.f(0xC);
        const float fill = P(rng);
        for (int i = 0; i < capy / 4 * 4; ++i) {
            std::uint32_t* B = ar.rec.data() + 36 * std::size_t(i / 4);
            B[i & 3] = fbits(-9999.0f);
            if (P(rng) > fill) continue;
            const float age = P(rng) < 0.9f ? life * 1.1f * P(rng) : -0.05f * P(rng);
            const V3 pos = car + fwd * (-8.0f + 40.0f * P(rng)) + side * (5.0f * U(rng)) + V3{0, 1.2f * P(rng), 0};
            const V3 vel = V3{U(rng), 0.3f + P(rng), U(rng)} * (1.0f + 12.0f * P(rng));
            spawn(B, i & 3, pos, vel, now - age, 1.2f * P(rng), g, k14, k18);
        }
        f.arrays.push_back(std::move(ar));
    }
    return f;
}

bool read_params(const std::string& elf, PopcornParams out[4]) {
    std::vector<std::uint8_t> d;
    if (!read_file(elf, d) || d.size() < 0x34) { std::fprintf(stderr, "cannot read %s\n", elf.c_str()); return false; }
    auto u32 = [&](std::size_t o) { return rd32(d.data() + o); };
    auto u16 = [&](std::size_t o) { return std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8; };
    const std::uint32_t phoff = u32(0x1C), phes = u16(0x2A), phnum = u16(0x2C);
    const std::uint32_t va = 0x4A4DB0;   // CB3PopcornArrayBase::maStandardParams
    for (std::uint32_t i = 0; i < phnum; ++i) {
        const std::size_t ph = phoff + i * phes;
        const std::uint32_t type = u32(ph), off = u32(ph + 4), vaddr = u32(ph + 8), fsz = u32(ph + 16);
        if (type != 1 || va < vaddr || va + 4 * 0x50 > vaddr + fsz) continue;
        for (int a = 0; a < 4; ++a) out[a] = popcorn_params_from_guest(d.data() + off + (va - vaddr) + 0x50 * a);
        return true;
    }
    std::fprintf(stderr, "%s: 0x%X not in a loaded segment\n", elf.c_str(), va);
    return false;
}

// ---- captured frames (b3replay::ContactFx)
bool frame_from_capture(const b3replay::Tick& t, FrameIn& f) {
    const b3replay::ContactFx& c = t.contact;
    if (!c.popcorn_valid || !t.cam.valid) return false;
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 3; ++l) f.cam.view[r][l] = fbits(t.cam.screen[r][l]);
    f.cam.window_x = fbits(t.cam.half_w);
    f.cam.recip_x = fbits(1.0f / t.cam.half_w);
    f.cam.recip_y = fbits(1.0f / t.cam.half_h);
    f.cam.near_ = fbits(t.cam.znear);
    f.cam.far_ = fbits(t.cam.zfar);
    for (int l = 0; l < 3; ++l) { f.scale[l] = t.trail_cam[4][l]; f.shift[l] = t.trail_cam[5][l]; }
    f.now = fbits(t.now);
    f.step = fbits(1.0f / 60);
    bool live = false;
    for (int a = 0; a < 4; ++a) {
        const auto& s = c.popcorn[a];
        if (!s) continue;
        ArrayIn ar;
        const std::uint32_t prm = rd32(s->head + 0x30) & ~0x30000000u;
        const int id = prm >= 0x4A4DB0 && prm < 0x4A4DB0 + 4 * 0x50 ? int((prm - 0x4A4DB0) / 0x50) : a;
        ar.p = popcorn_params_from_guest(c.popcorn_params + 0x50 * id);
        ar.g = rd32(s->head + 0x10);
        const int capy = int(rd32(s->head + 0x24));
        ar.batches = popcorn_batches(capy);
        if (s->rec.size() < std::size_t(capy / 4) * 0x90) continue;
        ar.rec.resize(s->rec.size() / 4);
        std::memcpy(ar.rec.data(), s->rec.data(), s->rec.size());
        const float L = ar.p.f(0xC);
        for (std::size_t i = 0; i < ar.rec.size() / 36 && !live; ++i)
            for (int j = 0; j < 4; ++j) {
                const float ts = bitsf(ar.rec[36 * i + std::size_t(j)]);
                if (ts < t.now && ts >= t.now - L) live = true;
            }
        f.arrays.push_back(std::move(ar));
    }
    return live;
}

}  // namespace

int main(int argc, char** argv) {
    const char* h = std::getenv("HOME");
    const std::string decomp = std::string(h ? h : ".") + "/b3/burnout3-retail-decomp";
    std::string vu_dir = decomp + "/assembly/assets/vu";
    std::string elf = decomp + "/orig/SLUS_210.50";
    int frames = 2000, every = 1;
    unsigned seed = 20261104;
    std::string capture;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--vu" && i + 1 < argc) vu_dir = argv[++i];
        else if (s == "--elf" && i + 1 < argc) elf = argv[++i];
        else if (s == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (s == "--seed" && i + 1 < argc) seed = unsigned(std::strtoul(argv[++i], nullptr, 0));
        else if (s == "--dump" && i + 1 < argc) g_dump = std::atoi(argv[++i]);
        else if (s == "--capture" && i + 1 < argc) capture = argv[++i];
        else if (s == "--every" && i + 1 < argc) every = std::max(1, std::atoi(argv[++i]));
        else {
            std::fprintf(stderr, "usage: b3vucheck_popcorn [--frames n] [--seed n] [--elf SLUS_210.50] [--vu dir] [--dump n]\n"
                                 "       b3vucheck_popcorn --capture <file> [--every n] [--dump n]\n");
            return 2;
        }
    }
    VuMicroprogram prog;
    {
        std::vector<std::uint8_t> d;
        const std::string path = vu_dir + "/game_0047C6F0.textbin.bin";
        std::string why;
        if (!read_file(path, d)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
        if (d.size() > 1728) d.resize(1728);   // the popcorn program is the image's first chain (E map Popcorn.dsm [L])
        if (!prog.load_textbin(d, &why)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), why.c_str()); return 1; }
    }
    if (!capture.empty()) {
        b3replay::Capture cap;
        std::string why;
        if (!cap.load(capture, &why, false)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
        std::uint64_t valid = 0;
        for (std::size_t i = 0; i < cap.ticks.size(); i += std::size_t(every)) {
            valid += cap.ticks[i].contact.popcorn_valid;
            FrameIn f;
            if (!frame_from_capture(cap.ticks[i], f)) continue;
            ++g_tot.ticks_live;
            if (!check_frame(prog, f)) return 1;
        }
        std::printf("capture %s: %llu ticks with popcorn, %llu with a live particle checked (every %d; camera inputs approximated "
                    "from Tick::cam / trail_cam)\n", capture.c_str(), (unsigned long long)valid,
                    (unsigned long long)g_tot.ticks_live, every);
        frames = 0;
    }
    PopcornParams params[4];
    if (frames > 0) {
        if (!read_params(elf, params)) return 1;
        for (int a = 0; a < 4; ++a)
            std::printf("array %d: capacity %u, blend %u, life %g, size %g, far %g, g %g, bounce %g: %zu batches\n", a,
                        params[a].w[1], params[a].w[2], params[a].f(0xC), params[a].f(0x10), params[a].f(0x44),
                        params[a].f(0x48), params[a].f(0x4C), popcorn_batches(int(params[a].w[1])).size());
    }
    std::mt19937 rng(seed);
    for (int i = 0; i < frames; ++i)
        if (!check_frame(prog, make_frame(rng, params))) return 1;
    std::printf("frames %llu: arrays %llu, batches %llu; particles alive %llu, culled %llu; XGKICKs %llu, sprites %llu, "
                "vertices compared %llu; pair-0x0046 bug: block-0 particle 1 alive %llu times, drawn differently from its own "
                "position %llu times; P / Q reads pending %llu / %llu\n",
                (unsigned long long)g_tot.frames, (unsigned long long)g_tot.arrays, (unsigned long long)g_tot.batches,
                (unsigned long long)g_tot.particles, (unsigned long long)g_tot.culled, (unsigned long long)g_tot.kicks,
                (unsigned long long)g_tot.sprites, (unsigned long long)g_tot.vertices, (unsigned long long)g_tot.bug_particles,
                (unsigned long long)g_tot.bug_sprites_differ, (unsigned long long)g_tot.p_pending,
                (unsigned long long)g_tot.q_pending);
    std::uint64_t bad = 0;
    for (const auto& kv : g_stats) {
        std::printf("  %-24s n %10llu  max |err| %-12.6g over tolerance %llu\n", kv.first.c_str(),
                    (unsigned long long)kv.second.n, kv.second.max_err, (unsigned long long)kv.second.bad);
        bad += kv.second.bad;
    }
    std::printf("%s\n", bad ? "MISMATCHES" : "ALL WITHIN TOLERANCE");
    return bad ? 1 : 0;
}
