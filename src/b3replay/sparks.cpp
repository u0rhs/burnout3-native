#include "sparks.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

#include "b3asset/popcornshade.h"
#include "b3asset/sparkshade.h"

namespace b3replay {

namespace {

std::uint32_t rd32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
float bitsf(std::uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
std::uint32_t fbits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }

// Frame 0's affine map world -> the program's view space: A = (clip.x row4.x, clip.y row4.y, clip.w), clip = p.x R0 +
// p.y R1 + p.z R2 + R3 [C program 0x0072.., sparkshade.h]; inverted in double.
struct ViewMap {
    double inv[3][3] = {}, b[3] = {};
    bool ok = false;
    void set(const b3asset::SparkFrame& f) {
        double m[3][3];
        const double sx = bitsf(f.row[4][0]), sy = bitsf(f.row[4][1]);
        const int lane[3] = {0, 1, 3};
        const double sc[3] = {sx, sy, 1.0};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) m[r][c] = sc[r] * bitsf(f.row[c][lane[r]]);
            b[r] = sc[r] * bitsf(f.row[3][lane[r]]);
        }
        const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        ok = std::fabs(det) > 1e-30;
        if (!ok) return;
        inv[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
        inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
        inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
        inv[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
        inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
        inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
        inv[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
        inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
        inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    }
    void world(const std::uint32_t a[3], float out[3]) const {
        const double v[3] = {bitsf(a[0]) - b[0], bitsf(a[1]) - b[1], bitsf(a[2]) - b[2]};
        for (int r = 0; r < 3; ++r) out[r] = float(inv[r][0] * v[0] + inv[r][1] * v[1] + inv[r][2] * v[2]);
    }
};

}  // namespace

const char* spark_texture_name() { return "fxspark"; }

std::uint32_t particle_id(std::uint32_t array, std::uint32_t bank, std::uint32_t slot) {
    std::uint32_t h = (array * 2u + bank) * 0x9E3779B1u + slot * 0x85EBCA6Bu + 0x27D4EB2Fu;   // a 32-bit finaliser (murmur3's)
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}

float spark_width_scale(int level) { return level >= 3 ? 1.0f : level == 2 ? 1.25f : level == 1 ? 1.6f : 1.0f; }
float debris_size_scale(int level) { return level >= 3 ? 1.0f : level == 2 ? 1.12f : level == 1 ? 1.3f : 1.0f; }

bool particle_kept(int level, std::uint32_t id) {
    if (level >= 3) return true;
    if (level <= 0) return false;
    return ((id >> 7) & 3u) < std::uint32_t(level);   // Low: one residue of four, Medium: two (nested)
}

int particle_layer_level(int setting, bool full) {
    if (setting <= 0) return 0;
    if (full) return setting >= 3 ? 3 : setting;
    return setting >= 3 ? 2 : 1;
}

bool build_sparks(const ContactFx& c, const std::uint32_t cam914[4], std::vector<b3gfx::Vertex>& verts, std::vector<std::uint32_t>& idx,
                  SparkStats* stats, int level) {
    using namespace b3asset;
    if (level <= 0) return false;
    if (!c.sparks_valid || !c.history_valid || !c.history) return false;
    SparkFrame hist[18];
    for (int k = 0; k < 18; ++k) hist[k] = spark_frame_from_guest(c.history->data() + 0x60 * k);
    ViewMap vm;
    vm.set(hist[0]);
    if (!vm.ok) return false;
    std::uint32_t tag[4];
    for (int l = 0; l < 4; ++l) tag[l] = rd32(c.spark_d1e31a20 + 4 * l);
    const bool is_crash = c.spark_statics[0xC] != 0;   // mbIsCrash 0x4E2A0C (BeginRender's crash argument)
    const std::uint32_t now = hist[0].row[5][0];      // BeginRender's t (D_00522680 at the render)
    const std::size_t v0 = verts.size();
    // RenderPostFogEffects 0x261E00: the six arrays in order; RenderSparkArray 0x2637F0 picks the renderable ones and
    // EndRender 0x2636B0 sends each one's static block, its normal bank's chain and (with +0xA4) the crash bank's.
    // perf1 (§33): the work is listed as units (one batch of one bank of one array, in draw order), the units are built on
    // up to 8 threads, and their quads are appended in unit order: the same vertices and indices as one pass.
    struct Unit {
        const SparkVuStatic* st;
        const SparkArrayState* s;
        int b, intervals;
        SparkBatch bt;
        std::uint32_t array = 0, slot = 0;   // effects2: the array and the batch's ring slot, for the share a level keeps
        std::vector<b3gfx::Vertex> v;
        std::vector<std::uint32_t> i;
        int segments = 0, culled = 0, quads = 0;
    };
    std::vector<std::unique_ptr<SparkVuStatic>> statics;
    std::vector<Unit> units;
    std::vector<SparkBatch> batches;
    std::size_t work = 0;
    for (int a = 0; a < 6; ++a) {
        const auto& s = c.sparks[a];
        if (!s) continue;
        const SparkParams p = spark_params_from_guest(s->params);
        bool cb = false;
        if (!spark_array_renderable(p, now, rd32(s->head + 0x44), rd32(s->head + 0x90), is_crash, &cb)) continue;
        const int intervals = spark_intervals(hist, p.w[0x5C / 4]);
        statics.push_back(std::make_unique<SparkVuStatic>(spark_vu_static(p, rd32(s->head + 0xA0), intervals, cam914, tag, hist)));
        if (level < 3) statics.back()->q[1][2] = fbits(bitsf(statics.back()->q[1][2]) * spark_width_scale(level));   // effects2
        if (stats) { ++stats->arrays; stats->intervals = intervals; }
        for (int b = 0; b < 2; ++b) {
            if (b == 1 && !cb) break;
            std::uint32_t base[4];
            for (int r = 0; r < 4; ++r) base[r] = rd32(s->head + 0x4C * b + 0x10 * r);
            batches.clear();
            if (!spark_bank_batches_from_chain(s->dma[b].data(), s->dma[b].size(), base, batches)) batches = spark_bank_batches(p, b == 1);
            if (stats) ++stats->banks;
            for (const SparkBatch& bt : batches) {
                const auto& rec = s->rec[b][bt.ring];
                if (std::size_t(bt.first + bt.blocks) * 0x90 > rec.size()) continue;
                units.push_back(Unit{statics.back().get(), s.get(), b, intervals, bt, std::uint32_t(a), std::uint32_t(bt.ring) * 65536u + std::uint32_t(bt.first) * 4u, {}, {}});
                work += std::size_t(bt.blocks) * std::size_t(intervals);
            }
        }
    }
    auto build = [&](Unit& u) {
        std::vector<SparkSegment> segs;
        std::vector<SparkQuad> quads;
        std::vector<int> culled;
        const auto& rec = u.s->rec[u.b][u.bt.ring];
        std::vector<std::uint32_t> words(std::size_t(u.bt.blocks) * 36);
        std::memcpy(words.data(), rec.data() + std::size_t(u.bt.first) * 0x90, words.size() * 4);
        if (level < 3) {   // effects2: the sparks this level does not draw die in the copy (the empty-slot time the game uses), so the
                           // program model skips them in every interval; the share is by ring slot: the same sparks every tick
            const std::uint32_t dead = fbits(-9999.0f);
            for (int b = 0; b < u.bt.blocks; ++b)
                for (int j = 0; j < 4; ++j)
                    if (!particle_kept(level, particle_id(u.array, std::uint32_t(u.b), u.slot + std::uint32_t(4 * b + j)))) words[std::size_t(36 * b + j)] = dead;
        }
        const std::uint32_t rgba = std::uint32_t(u.bt.rgbaq);   // R G B A bytes (Q in the high word)
        for (int k = 0; k < u.intervals; ++k) {
            spark_segments(*u.st, k, u.bt.itop, words.data(), u.bt.blocks, segs);
            if (segs.empty()) continue;
            spark_project(*u.st, k, segs, quads, &culled, false);
            u.segments += int(segs.size());
            u.culled += int(culled.size());
            u.quads += int(quads.size());
            for (const SparkQuad& q : quads) {
                const std::uint32_t b0 = std::uint32_t(u.v.size());
                for (int v = 0; v < 4; ++v) {
                    b3gfx::Vertex o;
                    std::memset(&o, 0, sizeof o);
                    vm.world(q.corner[v], o.pos);
                    o.normal[1] = 1;
                    o.uv[0] = bitsf(q.v[v].s);
                    o.uv[1] = bitsf(q.v[v].t);
                    o.rgba = rgba;
                    u.v.push_back(o);
                }
                const std::uint32_t tri[6] = {b0, b0 + 1, b0 + 2, b0 + 2, b0 + 1, b0 + 3};
                u.i.insert(u.i.end(), tri, tri + 6);
            }
        }
    };
    // threads only for heavy ticks (a crash's tens of thousands of sparks): a few thousand block-intervals build in < 1 ms
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const unsigned nthreads = work >= 4096 && units.size() > 1 ? std::min<unsigned>({8u, std::max(1u, hw / 2), unsigned(units.size())}) : 1u;
    if (nthreads > 1) {
        std::atomic<std::size_t> next{0};
        auto worker = [&]() { for (std::size_t k; (k = next.fetch_add(1)) < units.size();) build(units[k]); };
        std::vector<std::thread> pool;
        for (unsigned t = 1; t < nthreads; ++t) pool.emplace_back(worker);
        worker();
        for (auto& t : pool) t.join();
    } else {
        for (Unit& u : units) build(u);
    }
    std::size_t nv = 0, ni = 0;
    for (const Unit& u : units) { nv += u.v.size(); ni += u.i.size(); }
    verts.reserve(verts.size() + nv);
    idx.reserve(idx.size() + ni);
    for (const Unit& u : units) {
        const std::uint32_t base = std::uint32_t(verts.size());
        verts.insert(verts.end(), u.v.begin(), u.v.end());
        for (std::uint32_t i : u.i) idx.push_back(base + i);
        if (stats) { stats->segments += u.segments; stats->culled += u.culled; stats->quads += u.quads; }
    }
    return verts.size() > v0;
}

int build_popcorn(const ContactFx& c, const Camera& game, const std::uint32_t trail_cam[10][4], float now, const Camera& view,
                  std::vector<b3gfx::Vertex>& verts, std::vector<std::uint32_t>& idx, std::vector<PopcornDraw>& draws, int level) {
    using namespace b3asset;
    if (!c.popcorn_valid || !game.valid || level <= 0) return 0;
    // the RwCamera inputs from the captured game camera (the interpreter check, b3vucheck_popcorn --capture, is exact
    // with these [C]); vu1DataXYZScale / Shift from the camera record (+0x120 block)
    PopcornCamera pc;
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 3; ++l) pc.view[r][l] = fbits(game.screen[r][l]);
    pc.window_x = fbits(game.half_w);
    pc.recip_x = fbits(1.0f / game.half_w);
    pc.recip_y = fbits(1.0f / game.half_h);
    pc.near_ = fbits(game.znear);
    pc.far_ = fbits(game.zfar);
    std::uint32_t scale[4] = {}, shift[4] = {}, offset[4] = {};
    for (int l = 0; l < 3; ++l) { scale[l] = trail_cam[4][l]; shift[l] = trail_cam[5][l]; }
    // texture names by params index [C maStandardParams 0x4A4DB0 dump: docs/effects-survey.md §3.4]
    static const char* const kNames[4] = {"fxpopcornspark", "fxpopcorndebris", "fxpopcorndebris", "fxpopcorndebris"};
    const float* at = view.m.r[2];
    const float* eye = view.m.r[3];
    float sr[3], up[3];   // screen right (the matrix's first row points screen left), up
    for (int k = 0; k < 3; ++k) { sr[k] = -view.m.r[0][k]; up[k] = view.m.r[1][k]; }
    int sprites = 0;
    std::vector<PopcornParticle> parts;
    std::vector<PopcornSprite> spr;
    for (int a = 0; a < 4; ++a) {
        const auto& s = c.popcorn[a];
        if (!s) continue;
        const std::uint32_t prm = rd32(s->head + 0x30) & ~0x30000000u;
        const int id = prm >= 0x4A4DB0 && prm < 0x4A4DB0 + 4 * 0x50 ? int((prm - 0x4A4DB0) / 0x50) : a;
        const PopcornParams p = popcorn_params_from_guest(c.popcorn_params + 0x50 * id);
        const int capy = int(rd32(s->head + 0x24));
        if (capy <= 0 || s->rec.size() < std::size_t(capy / 4) * 0x90) continue;
        const PopcornSetup su = popcorn_setup(popcorn_vu_static(p, pc, rd32(s->head + 0x10), scale, shift, offset, fbits(now), fbits(1.0f / 60)));
        const float far_ = std::min(game.zfar, p.f(0x44));   // the clip box's far plane [C program, popcornshade.h]
        PopcornDraw d;
        d.array = a;
        d.alpha = rd32(s->head);
        d.texture = kNames[id & 3];
        d.first = std::uint32_t(idx.size());
        PopcornStState st = popcorn_st_start(su);
        for (const PopcornBatch& b : popcorn_batches(capy)) {
            if (std::size_t(b.first + b.blocks) * 0x90 > s->rec.size()) continue;
            std::vector<std::uint32_t> words(std::size_t(b.blocks) * 36);
            std::memcpy(words.data(), s->rec.data() + std::size_t(b.first) * 0x90, words.size() * 4);
            parts.clear();
            popcorn_particles(su, words.data(), b.blocks, nullptr, parts);   // nullptr: each particle at its own position
            if (parts.empty()) continue;
            // the program's ST pattern (alternating corners; the first sprite of each later batch one texel) for the
            // game camera's sprite order; a particle the game camera culled keeps (0,0)-(1,1)
            spr.clear();
            popcorn_project(su, parts, st, spr, nullptr);
            for (const PopcornParticle& q : parts) {
                if (level < 3 && !particle_kept(level, particle_id(std::uint32_t(a), 2u, std::uint32_t(b.first) * 4u + std::uint32_t(q.index)))) continue;   // effects2
                float pos[3] = {bitsf(q.pos[0]), bitsf(q.pos[1]), bitsf(q.pos[2])};
                float dv[3] = {pos[0] - eye[0], pos[1] - eye[1], pos[2] - eye[2]};
                const float depth = dv[0] * at[0] + dv[1] * at[1] + dv[2] * at[2];
                if (!(depth > game.znear) || depth > far_) continue;   // the program's near / far clip (the GPU clips the sides)
                std::uint32_t fade[4];
                popcorn_fade(su, fbits(depth), fade);
                std::uint32_t rgba = 0;
                for (int ch = 0; ch < 4; ++ch) {
                    const float v = bitsf(q.colour[ch]) * bitsf(fade[ch]);
                    const int iv = v >= 2147483648.0f ? 0x7FFFFFFF : v <= -2147483648.0f ? int(0x80000000u) : int(v);   // FTOI0
                    rgba |= std::uint32_t(iv & 0xFF) << (8 * ch);                                                      // low 8 bits
                }
                float s0 = 0, t0 = 0, s1 = 1, t1 = 1;
                for (const PopcornSprite& g : spr)
                    if (g.index == q.index) { s0 = bitsf(g.st[0][0]); t0 = bitsf(g.st[0][1]); s1 = bitsf(g.st[1][0]); t1 = bitsf(g.st[1][1]); break; }
                const float h = level < 3 ? popcorn_view_half_extent(su, depth) * debris_size_scale(level) : popcorn_view_half_extent(su, depth);   // effects2
                const float cs[4][2] = {{-1, 1}, {1, 1}, {-1, -1}, {1, -1}};   // TL, TR, BL, BR
                const float uv[4][2] = {{s0, t0}, {s1, t0}, {s0, t1}, {s1, t1}};
                const std::uint32_t b0 = std::uint32_t(verts.size());
                for (int v = 0; v < 4; ++v) {
                    b3gfx::Vertex o;
                    std::memset(&o, 0, sizeof o);
                    for (int k = 0; k < 3; ++k) o.pos[k] = pos[k] + (cs[v][0] * sr[k] + cs[v][1] * up[k]) * h;
                    o.normal[1] = 1;
                    o.uv[0] = uv[v][0];
                    o.uv[1] = uv[v][1];
                    o.rgba = rgba;
                    verts.push_back(o);
                }
                const std::uint32_t tri[6] = {b0, b0 + 1, b0 + 2, b0 + 2, b0 + 1, b0 + 3};
                idx.insert(idx.end(), tri, tri + 6);
                ++sprites;
            }
        }
        d.count = std::uint32_t(idx.size()) - d.first;
        if (d.count > 0) draws.push_back(d);
    }
    return sprites;
}

namespace {
struct KeepCtx {
    int smoke_level, debris_level;
};
bool particle_array_is_debris(int array) { return array < 6; }   // fxdebris, fxgravel, fxsnow, fxglass: the debris family
int particle_level_of(const KeepCtx& k, int array, int bank) { return (bank == 1 || particle_array_is_debris(array)) ? k.debris_level : k.smoke_level; }
bool particle_keep(void* user, int array, int bank, int slot) {
    const KeepCtx& k = *static_cast<const KeepCtx*>(user);
    return particle_kept(particle_level_of(k, array, bank), particle_id(std::uint32_t(array) + 8u, std::uint32_t(bank), std::uint32_t(slot)));
}
}  // namespace

bool build_particles(const ContactFx& c, bool crash, int smoke_level, int debris_level, b3asset::ParticleVu& vu,
                     std::vector<b3gfx::Vertex>& verts, std::vector<std::uint32_t>& idx, std::vector<ParticleDraw>& draws,
                     ParticleBuildStats* stats) {
    using namespace b3asset;
    if (!c.particles_valid || !c.particle_cam || !vu.loaded() || (smoke_level <= 0 && debris_level <= 0)) return false;
    ParticleArrayIn in[kParticleArrays];
    const KeepCtx kc{smoke_level, debris_level};
    bool any = false;
    for (int a = 0; a < kParticleArrays; ++a) {
        const auto& s = c.particles[a];
        if (!s) continue;
        for (int b = 0; b < 2; ++b) {
            const int lv = particle_level_of(kc, a, b);
            const std::uint32_t capy = rd32(s->head + (b == 0 ? 0xE8 : 0x114) + 0x14);
            if (lv <= 0 || s->rec[b].size() != particle_bank_bytes(capy)) continue;
            if (!particle_bank_dispatched(s->head, b, crash, c.particle_skip, s->params[0x5D])) continue;
            in[a].head = s->head;
            in[a].bank[b].rec = s->rec[b].data();
            in[a].bank[b].capacity = capy;
            in[a].bank[b].dispatch = true;
            any = true;
        }
    }
    if (!any) return false;
    std::vector<ParticlePacket> pk;
    ParticleStats ps;
    std::string why;
    const bool thin = smoke_level < 3 || debris_level < 3;
    if (!vu.run(c.particle_cam->data(), in, pk, &ps, &why, thin ? &particle_keep : nullptr, const_cast<KeepCtx*>(&kc))) return false;
    if (stats) { stats->vu = ps; }
    const std::size_t i0 = idx.size();
    (void)i0;
    for (const ParticlePacket& p : pk) {
        const std::uint32_t base = std::uint32_t(verts.size());
        const std::uint32_t first = std::uint32_t(idx.size());
        for (const ParticleVertex& v : p.v) {
            b3gfx::Vertex o;
            std::memset(&o, 0, sizeof o);
            o.pos[0] = v.pos[0]; o.pos[1] = v.pos[1]; o.pos[2] = v.pos[2];
            o.normal[1] = 1;
            o.uv[0] = v.s;
            o.uv[1] = v.t;
            o.rgba = std::uint32_t(v.rgba[0]) | std::uint32_t(v.rgba[1]) << 8 | std::uint32_t(v.rgba[2]) << 16 | std::uint32_t(v.rgba[3]) << 24;
            verts.push_back(o);
        }
        // vertices with next to no opacity (RGBA alpha under 2 of 128) draw nothing visible: dropped before they cost fill
        auto faint = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) { return p.v[a].rgba[3] < 2 && p.v[b].rgba[3] < 2 && p.v[c].rgba[3] < 2; };
        if (p.sprite) {
            for (std::uint32_t k = 0; k + 3 < p.v.size(); k += 4) {
                if (faint(k, k + 1, k + 2) && p.v[k + 3].rgba[3] < 2) continue;
                const std::uint32_t b0 = base + k, tri[6] = {b0, b0 + 1, b0 + 2, b0 + 2, b0 + 1, b0 + 3};
                idx.insert(idx.end(), tri, tri + 6);
                if (stats) ++stats->quads;
            }
        } else if ((p.prim & 7) == 4) {   // TRISTRIP: a triangle per drawing kick once three vertices are in (ADC vertices only shift in)
            for (std::uint32_t k = 2; k < p.v.size(); ++k) {
                if (p.v[k].adc || faint(k - 2, k - 1, k)) continue;
                idx.push_back(base + k - 2); idx.push_back(base + k - 1); idx.push_back(base + k);
                if (stats) ++stats->triangles;
            }
        } else if ((p.prim & 7) == 3) {   // TRIANGLE
            for (std::uint32_t k = 2; k < p.v.size(); k += 3) {
                if (faint(k - 2, k - 1, k)) continue;
                idx.push_back(base + k - 2); idx.push_back(base + k - 1); idx.push_back(base + k);
                if (stats) ++stats->triangles;
            }
        }
        const std::uint32_t count = std::uint32_t(idx.size()) - first;
        if (count == 0) continue;
        const char* tex = c.particles[p.array] ? c.particles[p.array]->tex : "";
        if (!draws.empty() && draws.back().array == p.array && draws.back().alpha == p.alpha && draws.back().test == p.test && draws.back().first + draws.back().count == first) {
            draws.back().count += count;   // consecutive packets of one array and state: one draw
        } else {
            ParticleDraw d;
            d.array = p.array;
            d.bank = p.bank;
            d.texture = tex;
            d.alpha = p.alpha;
            d.test = p.test;
            d.first = first;
            d.count = count;
            draws.push_back(std::move(d));
        }
    }
    if (std::getenv("B3PORT_PARTICLE_LOG") && !pk.empty()) {
        float cp[3]; std::memcpy(cp, c.particle_cam->data() + 0xD0, 12);
        for (std::size_t k = 0; k < pk.size() && k < 12; ++k) {
            const auto& p = pk[k];
            std::fprintf(stderr, "  pk array %d bank %d tex %s prim 0x%x alpha 0x%llx%s test 0x%llx%s nv %zu v0 (%.2f %.2f %.2f) st (%.2f %.2f) rgba %d %d %d %d  cam (%.2f %.2f %.2f)\n", p.array, p.bank,
                         c.particles[p.array]->tex, p.prim, static_cast<unsigned long long>(p.alpha), p.has_alpha ? "" : "(inh)", static_cast<unsigned long long>(p.test), p.has_test ? "" : "(inh)", p.v.size(),
                         p.v[0].pos[0], p.v[0].pos[1], p.v[0].pos[2], p.v[0].s, p.v[0].t, p.v[0].rgba[0], p.v[0].rgba[1], p.v[0].rgba[2], p.v[0].rgba[3], cp[0], cp[1], cp[2]);
        }
    }
    if (stats) stats->banks = int(pk.size());
    return !draws.empty();
}

}  // namespace b3replay
