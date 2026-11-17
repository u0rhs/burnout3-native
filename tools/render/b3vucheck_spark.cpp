// b3vucheck_spark: offline VU reference check of the spark model (include/b3asset/sparkshade.h).
//
// Runs the game's spark microprogram VU1 0x46D090 in the b3asset VU interpreter with the inputs the EE side sends -- the
// VIF streams of CB3SparkRenderer::UploadStaticDataAndCallCode 0x263420 (VU 912..1023: params row, counts, camera
// window, GIF tag, the 18-frame camera history) and each bank's static chain (CSparkBank::Construct 0x264170: per batch
// UNPACK V4-32 9n to TOPS, ITOP = n | packed lifetime, MSCALF 0 / MSCNT) through the VIF interpreter -- and compares
// every XGKICKed GIF vertex with spark_segments() + spark_project().
// Synthetic frames: the six arrays with the retail CB3SparkArrayBase::mgaArrayParams (read from the retail ELF, not stored
// here), rings filled with random sparks spawned the way SpawnBouncySpark 0x263C40 does (bounce solved on the CPU), some
// never used (-9999), some dead or born between history frames; a chase camera moving along a curve with an 18-frame
// history built by spark_frame_set (full, partly filled, or with the crash-mode half-step shift), camera records shaped
// like the race camera's (b3vucheck_trail's chase camera).
// Captured frames (--capture): every tick of a scene capture whose contact-effects block holds the whole camera history
// (ContactFx::history_valid), the renderable arrays (RenderSparkArray's test on the captured head) with their captured
// params, DMA heads (parsed: batches, ITOP, RGBAQ; checked against the rebuild) and ring blocks; camera record 0's window
// words from Tick::trail_cam (ticks with D_00667050 != 0 are skipped). Nothing is written.
//
//   b3vucheck_spark [--frames n] [--seed n] [--elf SLUS_210.50] [--vu dir] [--dump n] [-v]
//   b3vucheck_spark --capture <scene capture with the fx2 contact block> [--every n] [--dump n]
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
#include "b3asset/sparkshade.h"
#include "b3asset/trackshade.h"
#include "b3asset/vif.h"
#include "b3asset/vu1.h"
#include "b3host/ps2float.h"
#include "../../src/b3replay/capture.h"

using namespace b3asset;

namespace {

int g_dump = 0;
bool g_verbose = false;

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
    std::uint64_t frames = 0, arrays = 0, banks = 0, batches = 0, passes = 0, kicks = 0, segments = 0, culled = 0;
    std::uint64_t quads = 0, vertices = 0, intervals = 0, q_pending = 0, p_pending = 0, branch_backup = 0;
    std::uint64_t skipped_cam = 0, chains_parsed = 0, chains_rebuilt_equal = 0, crash_banks = 0, a4_differs = 0;
};
Totals g_tot;

// One bank as the program sees it: its batches and the ring blocks (raw words, 36 per block).
struct BankIn {
    std::vector<SparkBatch> batches;
    std::vector<std::uint32_t> ring[4];
};
struct ArrayIn {
    SparkParams p;
    std::uint32_t a0 = 0;
    BankIn bank[2];
    bool crash_bank = false;
};
struct FrameIn {
    std::uint32_t cam914[4] = {}, tag[4] = {};
    SparkFrame hist[18];
    std::vector<ArrayIn> arrays;   // the renderable ones, in order
};

void dump_kick(const GifPacket& p, const std::vector<SparkQuad>& want) {
    std::printf(" kick: %zu verts, model %zu quads\n", p.verts.size(), want.size());
    for (std::size_t i = 0; i < p.verts.size(); ++i) {
        const GifVertex& v = p.verts[i];
        std::printf("  v%-3zu prim %03x st %9.5f %9.5f q %g xy %9.4f %9.4f z %8u adc %d\n", i, v.prim, v.s, v.t, v.q,
                    v.x / 16.0 - 2048, v.y / 16.0 - 2048, v.z, v.adc);
    }
    for (std::size_t i = 0; i < want.size(); ++i)
        for (int c = 0; c < 4; ++c) {
            const SparkGsVertex& v = want[i].v[c];
            std::printf("  m%-3zu.%d st %9.5f %9.5f xy %9.4f %9.4f z %8u adc %d\n", i, c, bitsf(v.s), bitsf(v.t),
                        (v.xyz[0] & 0xFFFF) / 16.0 - 2048, (v.xyz[1] & 0xFFFF) / 16.0 - 2048, (v.xyz[2] >> 4) & 0xFFFFFF, v.adc);
        }
}

bool check_frame(const VuMicroprogram& prog, const FrameIn& f) {
    VifInterpreter vif;
    Vu1 vu(vif.mem);
    std::vector<GifPacket> got;
    std::string err;
    bool ok = true;
    const auto on_xgkick = [&](std::uint32_t qa) {
        GifPacket p;
        std::string e2;
        if (!read_gif_packet(vif.mem, qa, p, &e2)) std::fprintf(stderr, "GIF: %s\n", e2.c_str());
        got.push_back(std::move(p));
    };
    const auto on_kick = [&](const VuMemory&, std::uint32_t itop, VifInterpreter::KickKind kind, std::uint32_t pc) {
        if (!ok) return;
        const std::uint32_t start = kind == VifInterpreter::kMscnt ? vu.tpc : pc;
        if (!vu.run(prog, start, 0, itop, on_xgkick, &err)) { std::fprintf(stderr, "VU1 0x46D090: %s\n", err.c_str()); ok = false; }
    };
    for (const ArrayIn& a : f.arrays) {
        ++g_tot.arrays;
        const int iv = spark_intervals(f.hist, a.p.w[0x5C / 4]);
        const SparkVuStatic st = spark_vu_static(a.p, a.a0, iv, f.cam914, f.tag, f.hist);
        const std::vector<std::uint8_t> s = spark_vif_static(st);
        if (!vif.run(s.data(), s.size(), on_kick, &err)) { std::fprintf(stderr, "VIF: %s\n", err.c_str()); return false; }
        for (int b = 0; b < (a.crash_bank ? 2 : 1); ++b) {
            ++g_tot.banks;
            g_tot.crash_banks += b;
            for (const SparkBatch& bt : a.bank[b].batches) {
                ++g_tot.batches;
                const std::vector<std::uint32_t>& ring = a.bank[b].ring[bt.ring];
                if (std::size_t(bt.first + bt.blocks) * 36 > ring.size()) { std::fprintf(stderr, "batch past its ring\n"); return false; }
                const std::uint32_t* blocks = ring.data() + 36 * bt.first;
                got.clear();
                const std::vector<std::uint8_t> e = spark_vif_batch(bt, blocks);
                if (!vif.run(e.data(), e.size(), on_kick, &err)) { std::fprintf(stderr, "VIF: %s\n", err.c_str()); return false; }
                if (!ok) return false;
                // the model: one pass per interval, a kick when it has quads
                std::size_t gk = 0;
                for (int k = 0; k < iv; ++k) {
                    ++g_tot.passes;
                    std::vector<SparkSegment> segs;
                    spark_segments(st, k, bt.itop, blocks, bt.blocks, segs);
                    std::vector<SparkQuad> quads;
                    std::vector<int> culled;
                    spark_project(st, k, segs, quads, &culled);
                    g_tot.segments += segs.size();
                    g_tot.culled += culled.size();
                    if (quads.empty()) continue;
                    if (gk >= got.size()) { cmp("kicks (model has quads)", 0, 1, 0); continue; }
                    const GifPacket& p = got[gk++];
                    ++g_tot.kicks;
                    cmp("quad count", double(p.verts.size()), double(4 * quads.size()), 0);
                    if (p.verts.size() != 4 * quads.size()) { if (g_dump > 0) { --g_dump; dump_kick(p, quads); } continue; }
                    bool bad = false;
                    for (std::size_t q = 0; q < quads.size(); ++q) {
                        ++g_tot.quads;
                        for (int c = 0; c < 4; ++c) {
                            const GifVertex& g = p.verts[4 * q + std::size_t(c)];
                            const SparkGsVertex& w = quads[q].v[c];
                            const double ex = std::fabs(double(g.x) - double(w.xyz[0] & 0xFFFF));
                            const double ey = std::fabs(double(g.y) - double(w.xyz[1] & 0xFFFF));
                            const double ez = std::fabs(double(g.z) - double((w.xyz[2] >> 4) & 0xFFFFFF));
                            cmp("x (1/16 px)", g.x, w.xyz[0] & 0xFFFF, 0);
                            cmp("y (1/16 px)", g.y, w.xyz[1] & 0xFFFF, 0);
                            cmp("z", g.z, (w.xyz[2] >> 4) & 0xFFFFFF, 0);
                            cmp("s", g.s, bitsf(w.s), 0);
                            cmp("t", g.t, bitsf(w.t), 0);
                            cmp("q", g.q, bitsf(w.q), 0);
                            cmp("fog", g.f, 0, 0);
                            cmp("adc", g.adc, w.adc, 0);
                            cmp("prim", g.prim, kSparkPrim, 0);
                            bad |= ex > 0 || ey > 0 || ez > 0 || g.s != bitsf(w.s) || g.t != bitsf(w.t) || g.adc != w.adc;
                            ++g_tot.vertices;
                        }
                    }
                    if (bad && g_dump > 0) { --g_dump; dump_kick(p, quads); }
                }
                cmp("kicks (program, unmatched)", double(got.size() - std::min(got.size(), gk)), 0, 0);
            }
        }
        g_tot.intervals += std::uint64_t(iv);
    }
    g_tot.q_pending += vu.stats.q_read_pending;
    g_tot.p_pending += vu.stats.p_read_pending;
    g_tot.branch_backup += vu.stats.branch_vi_backup;
    ++g_tot.frames;
    return true;
}

// ---- synthetic frames
struct V3 { float x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

// A camera record (0x230) shaped like the race camera's: +0xE0 rows from shade_scene_look_at (x/w, y/w in 0..1, w =
// depth), the window / z constants the corona and trail checks use.
void make_cam_record(V3 eye, V3 target, std::uint8_t rec[0x230], float far) {
    std::memset(rec, 0, 0x230);
    const float e3[3] = {eye.x, eye.y, eye.z}, t3[3] = {target.x, target.y, target.z}, sun[3] = {0, 1, 0};
    const ShadeScene sc = shade_scene_look_at(e3, t3, sun);
    auto put = [&](int off, float v) { std::memcpy(rec + off, &v, 4); };
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 3; ++l) put(0xE0 + 0x10 * r + 4 * l, sc.view[r][l]);
    const float tx = std::tan(0.5236f), ty = tx * 448.0f / 640.0f;
    put(0x168, 5032309.0f);
    put(0x170, 2048.0f);
    put(0x174, 2048.0f);
    put(0x178, 1174.5f);
    put(0x18C, far);
    put(0x19C, 0.5f);
    put(0x200, tx);
    put(0x204, ty);
    put(0x208, 1.0f / tx);
    put(0x20C, 1.0f / ty);
    put(0x218, 640.0f);
    put(0x21C, 448.0f);
}

std::uint32_t array_a0(const SparkParams& p) {   // CB3SparkArray::Construct 0x264040 [C matched]
    namespace fr = b3fp::ref;
    const std::uint32_t* l = &p.w[0x40 / 4];
    const std::uint32_t m = fr::max(l[3], fr::max(l[2], fr::max(l[0], l[1])));
    return fr::mul(m, 0x3A802008u);   // D_004E1160
}

// SpawnBouncySpark 0x263C40's record (host float; the program takes whatever words it gets).
void spawn(std::uint32_t* blk, int lane, V3 pos, V3 vel, float t, float h, float g, float bounce, float life) {
    float vy = vel.y;
    if (vy < 0 && h <= 0) vy = -vy;
    float pw = pos.y, vw = vel.y;
    const float dd = vy * vy - 2.0f * g * h, tt = vy + life * g;
    if (dd > 0 && dd < tt * tt) {
        const float tm = (-1.0f / g) * (std::sqrt(dd) + vy), gt = g * tm;
        const float nv = -bounce * (vy + gt) - gt;
        pw = ((pos.y - h) - nv * tm) - 0.5f * g * tm * tm;
        vw = nv;
    }
    blk[lane] = fbits(t);
    std::uint32_t* P = blk + 4 + 8 * lane;
    const float pv[8] = {pos.x, pos.y, pos.z, pw, vel.x, vel.y, vel.z, vw};
    for (int i = 0; i < 8; ++i) P[i] = fbits(pv[i]);
}

FrameIn make_frame(std::mt19937& rng, const SparkParams params[6]) {
    std::uniform_real_distribution<float> U(-1, 1), P(0, 1);
    FrameIn f;
    f.tag[0] = 0; f.tag[1] = 0x802A4000u; f.tag[2] = 0x42424242u; f.tag[3] = 0;   // D_01E31A20 [C Construct 0x263C10]
    const float now = 100.0f + 400.0f * P(rng);
    const float dtf = P(rng) < 0.7f ? 1.0f / 60 : 1.0f / 30;
    // a car on a curve; the camera behind it; frame k at now - k dtf (some frames repeat: the history's start)
    const V3 c0 = {1500.0f * U(rng), 30.0f * U(rng), 1500.0f * U(rng)};
    const float yaw0 = 3.14159f * U(rng), speed = 10.0f + 60.0f * P(rng), turn = 0.6f * U(rng);
    const float far = P(rng) < 0.5f ? 1000.0f : 10000.0f;
    auto car_at = [&](float t, float& yaw) {
        yaw = yaw0 + turn * (t - now);
        return c0 + V3{std::sin(yaw), 0, std::cos(yaw)} * (speed * (t - now));
    };
    const int filled = P(rng) < 0.2f ? 1 + int(P(rng) * 17) : 18;
    const bool crash_shift = P(rng) < 0.1f;
    std::uint8_t rec[0x230];
    for (int k = 0; k < 18; ++k) {
        const int kk = std::min(k, filled - 1);
        const float t = now - dtf * float(kk);
        float yaw;
        const V3 car = car_at(t, yaw);
        const V3 fwd = {std::sin(yaw), 0, std::cos(yaw)};
        make_cam_record(car - fwd * 6.0f + V3{0, 2.0f, 0}, car + fwd * 15.0f, rec, far);
        f.hist[k] = spark_frame_set(rec, t);
        if (k == 0) spark_cam914_from_record(rec, f.cam914);
        if (crash_shift && k > 0) f.hist[k].row[5][0] = fbits(bitsf(f.hist[k].row[5][0]) - dtf * 0.5f);
    }
    float yaw;
    const V3 car = car_at(now, yaw);
    const V3 fwd = {std::sin(yaw), 0, std::cos(yaw)}, side = {std::cos(yaw), 0, -std::sin(yaw)};
    for (int a = 0; a < 6; ++a) {
        if (a > 0 && P(rng) < 0.5f) continue;   // not renderable this frame
        ArrayIn ar;
        ar.p = params[a];
        ar.a0 = array_a0(ar.p);
        ar.crash_bank = ar.p.w[0x64 / 4] != 0 && P(rng) < 0.5f;
        const float g = ar.p.f(0x50), bounce = ar.p.f(0x54);
        for (int b = 0; b < 2; ++b) {
            BankIn& bk = ar.bank[b];
            bk.batches = spark_bank_batches(ar.p, b == 1);
            int cap[4] = {};
            for (const SparkBatch& s : bk.batches) cap[s.ring] = std::max(cap[s.ring], s.first + s.blocks);
            const float fill = P(rng);
            for (int r = 0; r < 4; ++r) {
                const float life = ar.p.f(0x40 + 4 * r);
                bk.ring[r].assign(std::size_t(cap[r]) * 36, 0);
                for (int blk = 0; blk < cap[r]; ++blk) {
                    std::uint32_t* B = bk.ring[r].data() + 36 * blk;
                    for (int j = 0; j < 4; ++j) {
                        B[j] = fbits(-9999.0f);   // CSparkBank::Prepare
                        if (P(rng) > fill) continue;
                        // spawn times: mostly alive, some just dead or born within the last frames, a few in the future
                        const float age = P(rng) < 0.85f ? life * 1.2f * P(rng) : 0.3f * P(rng) - 0.05f;
                        const V3 pos = car + fwd * (-10.0f + 60.0f * P(rng)) + side * (6.0f * U(rng)) + V3{0, 1.5f * P(rng), 0};
                        const V3 vel = V3{U(rng), 0.2f + P(rng), U(rng)} * (2.0f + 25.0f * P(rng)) + fwd * (speed * P(rng));
                        spawn(B, j, pos, vel, now - age, 1.5f * P(rng), g, bounce, life);
                    }
                }
            }
        }
        f.arrays.push_back(std::move(ar));
    }
    return f;
}

bool read_params(const std::string& elf, SparkParams out[6]) {
    std::vector<std::uint8_t> d;
    if (!read_file(elf, d) || d.size() < 0x34) { std::fprintf(stderr, "cannot read %s\n", elf.c_str()); return false; }
    auto u32 = [&](std::size_t o) { return rd32(d.data() + o); };
    auto u16 = [&](std::size_t o) { return std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8; };
    const std::uint32_t phoff = u32(0x1C), phes = u16(0x2A), phnum = u16(0x2C);
    const std::uint32_t va = 0x4C7710;   // CB3SparkArrayBase::mgaArrayParams [C E map / retail_data_names]
    for (std::uint32_t i = 0; i < phnum; ++i) {
        const std::size_t ph = phoff + i * phes;
        const std::uint32_t type = u32(ph), off = u32(ph + 4), vaddr = u32(ph + 8), fsz = u32(ph + 16);
        if (type != 1 || va < vaddr || va + 6 * 0x7C > vaddr + fsz) continue;
        for (int a = 0; a < 6; ++a) out[a] = spark_params_from_guest(d.data() + off + (va - vaddr) + 0x7C * a);
        return true;
    }
    std::fprintf(stderr, "%s: 0x%X not in a loaded segment\n", elf.c_str(), va);
    return false;
}

// ---- captured frames (b3replay::ContactFx, the fx2 block)
bool frame_from_capture(const b3replay::Tick& t, FrameIn& f) {
    const b3replay::ContactFx& c = t.contact;
    if (!c.sparks_valid || !c.history_valid || !c.history) return false;
    if (c.cam_index != 0) { ++g_tot.skipped_cam; return false; }
    for (int k = 0; k < 18; ++k) f.hist[k] = spark_frame_from_guest(c.history->data() + 0x60 * k);
    // 914 = camera record +0x170, +0x174, +0x178, +0x168; trail_cam = record 0 +0x120 (10 qw)
    f.cam914[0] = t.trail_cam[5][0];
    f.cam914[1] = t.trail_cam[5][1];
    f.cam914[2] = t.trail_cam[5][2];
    f.cam914[3] = t.trail_cam[4][2];
    for (int l = 0; l < 4; ++l) f.tag[l] = rd32(c.spark_d1e31a20 + 4 * l);
    const bool is_crash = c.spark_statics[0xC] != 0;   // mbIsCrash 0x4E2A0C
    const std::uint32_t now = f.hist[0].row[5][0];    // BeginRender's t (= D_00522680 at the render)
    for (int a = 0; a < 6; ++a) {
        const auto& s = c.sparks[a];
        if (!s) continue;
        ArrayIn ar;
        ar.p = spark_params_from_guest(s->params);
        ar.a0 = rd32(s->head + 0xA0);
        bool cb = false;
        if (!spark_array_renderable(ar.p, now, rd32(s->head + 0x44), rd32(s->head + 0x90), is_crash, &cb)) continue;
        ar.crash_bank = cb;
        if (cb != (s->head[0xA4] != 0)) ++g_tot.a4_differs;
        for (int b = 0; b < 2; ++b) {
            if (b == 1 && !cb) break;
            std::uint32_t base[4];
            int capy[4];
            for (int r = 0; r < 4; ++r) {
                base[r] = rd32(s->head + 0x4C * b + 0x10 * r);
                capy[r] = int(rd32(s->head + 0x4C * b + 0x10 * r + 4));
            }
            BankIn& bk = ar.bank[b];
            if (!spark_bank_batches_from_chain(s->dma[b].data(), s->dma[b].size(), base, bk.batches)) {
                std::fprintf(stderr, "poll %u array %d bank %d: chain not parsed (%zu bytes)\n", t.poll, a, b, s->dma[b].size());
                return false;
            }
            ++g_tot.chains_parsed;
            const std::vector<SparkBatch> rb = spark_bank_batches(ar.p, b == 1);
            bool same = rb.size() == bk.batches.size();
            for (std::size_t i = 0; same && i < rb.size(); ++i)
                same = rb[i].ring == bk.batches[i].ring && rb[i].first == bk.batches[i].first && rb[i].blocks == bk.batches[i].blocks &&
                       rb[i].itop == bk.batches[i].itop && rb[i].mscalf == bk.batches[i].mscalf && rb[i].rgbaq == bk.batches[i].rgbaq;
            g_tot.chains_rebuilt_equal += same;
            cmp("EE: chain rebuilt from params == captured", same, 1, 0);
            for (int r = 0; r < 4; ++r) {
                const auto& rec = s->rec[b][r];
                if (int(rec.size()) != capy[r] / 4 * 0x90) { std::fprintf(stderr, "ring %d/%d/%d not captured\n", a, b, r); return false; }
                bk.ring[r].resize(rec.size() / 4);
                std::memcpy(bk.ring[r].data(), rec.data(), rec.size());
            }
        }
        f.arrays.push_back(std::move(ar));
    }
    return true;
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
        else if (s == "-v") g_verbose = true;
        else {
            std::fprintf(stderr, "usage: b3vucheck_spark [--frames n] [--seed n] [--elf SLUS_210.50] [--vu dir] [--dump n] [-v]\n"
                                 "       b3vucheck_spark --capture <file> [--every n] [--dump n]\n");
            return 2;
        }
    }
    VuMicroprogram prog;
    {
        std::vector<std::uint8_t> d;
        const std::string path = vu_dir + "/game_0046D090.textbin.bin";
        std::string why;
        if (!read_file(path, d)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
        if (!prog.load_textbin(d, &why)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), why.c_str()); return 1; }
    }
    if (!capture.empty()) {
        b3replay::Capture cap;
        std::string why;
        if (!cap.load(capture, &why, false)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
        std::uint64_t ticks = 0, with = 0;
        for (std::size_t i = 0; i < cap.ticks.size(); i += std::size_t(every)) {
            FrameIn f;
            if (!frame_from_capture(cap.ticks[i], f)) continue;
            ++with;
            if (f.arrays.empty()) continue;
            if (!check_frame(prog, f)) return 1;
            ++ticks;
        }
        std::printf("capture %s: %llu ticks with the whole history, %llu with renderable arrays checked (every %d); "
                    "%llu skipped (camera record != 0); chains parsed %llu, equal to the rebuild %llu; +0xA4 differs %llu\n",
                    capture.c_str(), (unsigned long long)with, (unsigned long long)ticks, every,
                    (unsigned long long)g_tot.skipped_cam, (unsigned long long)g_tot.chains_parsed,
                    (unsigned long long)g_tot.chains_rebuilt_equal, (unsigned long long)g_tot.a4_differs);
        frames = 0;
    }
    SparkParams params[6];
    if (frames > 0) {
        if (!read_params(elf, params)) return 1;
        for (int a = 0; a < 6; ++a) {
            const auto bt = spark_bank_batches(params[a], false), bc = spark_bank_batches(params[a], true);
            std::printf("array %d: life %g %g %g %g, g %g, bounce %g, half width %g, blur %g, capacity %u/%u: %zu + %zu "
                        "batches, ITOP ring 0 %u, RGBAQ ring 0 %016llx\n", a, params[a].f(0x40), params[a].f(0x44),
                        params[a].f(0x48), params[a].f(0x4C), params[a].f(0x50), params[a].f(0x54), params[a].f(0x58),
                        params[a].f(0x5C), params[a].w[0x60 / 4], params[a].w[0x64 / 4], bt.size(), bc.size(),
                        bt.empty() ? 0u : bt[0].itop, (unsigned long long)(bt.empty() ? 0 : bt[0].rgbaq));
        }
    }
    std::mt19937 rng(seed);
    for (int i = 0; i < frames; ++i)
        if (!check_frame(prog, make_frame(rng, params))) return 1;
    std::printf("frames %llu: arrays %llu, banks %llu (crash %llu), batches %llu, passes %llu (intervals %llu); segments "
                "%llu, culled by the clip test %llu; XGKICKs %llu, quads %llu, vertices compared %llu; P reads pending %llu, "
                "branch-VI backups %llu\n",
                (unsigned long long)g_tot.frames, (unsigned long long)g_tot.arrays, (unsigned long long)g_tot.banks,
                (unsigned long long)g_tot.crash_banks, (unsigned long long)g_tot.batches, (unsigned long long)g_tot.passes,
                (unsigned long long)g_tot.intervals, (unsigned long long)g_tot.segments, (unsigned long long)g_tot.culled,
                (unsigned long long)g_tot.kicks, (unsigned long long)g_tot.quads, (unsigned long long)g_tot.vertices,
                (unsigned long long)g_tot.p_pending, (unsigned long long)g_tot.branch_backup);
    std::uint64_t bad = 0;
    for (const auto& kv : g_stats) {
        std::printf("  %-40s n %10llu  max |err| %-12.6g over tolerance %llu\n", kv.first.c_str(),
                    (unsigned long long)kv.second.n, kv.second.max_err, (unsigned long long)kv.second.bad);
        bad += kv.second.bad;
    }
    std::printf("%s\n", bad ? "MISMATCHES" : "ALL WITHIN TOLERANCE");
    return bad ? 1 : 0;
}
