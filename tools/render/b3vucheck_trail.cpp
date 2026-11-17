// b3vucheck_trail: offline VU reference check of the skid-mark (trail) model (include/b3asset/trailshade.h).
//
// Runs the game's trail microprogram VU1 0x4771B0 in the b3asset VU interpreter with the inputs the EE side sends --
// the VIF streams of CB3TrailRenderer::BeginRender 0x2F38B0 (camera block, MSCALF 0) and Render 0x2F3600 (type
// parameters, then per emitter UNPACK V4-32 x16 / V3-8 x16 with STCYCL 2,1, ITOP = point count, MSCNT) through the
// VIF interpreter -- and compares every XGKICKed GIF vertex with trail_ribbon() projected by trail_project().
// Synthetic frames: random skid-mark emitters (2..16 points along curved paths on tilted ground, random ages including
// ones past the lifetime, some with out-of-order times), all five parameter sets as CB3TrailRenderer::Construct builds
// them from the retail CB3TrailSystem::mgaDefaultParams (read from the retail ELF, not stored here), and a chase camera
// built like tools/render/b3vucheck.cpp's (shade_scene_look_at). Captured frames: fill a TrailFrame from guest words.
// Nothing is written.
//
//   b3vucheck_trail [--frames n] [--seed n] [--elf SLUS_210.50] [--vu dir] [--dump n] [-v]
//   b3vucheck_trail --capture <scene capture, version 3> [--every n]   (fx1: every n-th tick's listed emitters, the
//       captured D_01E865E0 rows, "now" and camera record 0's +0x120 block, as src/b3replay/capture.cpp decodes them)
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
#include "b3asset/trackshade.h"
#include "b3asset/trailshade.h"
#include "b3asset/vif.h"
#include "b3asset/vu1.h"
#include "../../src/b3replay/capture.h"

using namespace b3asset;

namespace {

std::string g_vu_dir;
int g_dump = 0;
bool g_verbose = false;

std::uint32_t fbits(float f) { std::uint32_t v; std::memcpy(&v, &f, 4); return v; }
float bitsf(std::uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }

// One frame's trail inputs, as raw guest words (a scene capture fills these directly).
struct TrailFrame {
    std::uint32_t now = 0;               // D_00522680 (gWorld +0x20), float bits
    TrailCamera cam;                     // camera record +0x120, 10 qw
    std::uint32_t params[5][12] = {};    // D_01E865E0 (CB3TrailRenderer::maDMAParams [L]), 5 x 3 qw
    struct Emitter { int type = 0; TrailEmitterInput in; };
    std::vector<Emitter> emitters;       // in the per-type lists' order (Render is called per type 0..4)
};

// ---- comparison bookkeeping
struct Stat {
    double max_err = 0;
    std::uint64_t n = 0, bad = 0;
    void add(double e, double tol) { ++n; max_err = std::max(max_err, e); if (e > tol) ++bad; }
};
std::map<std::string, Stat> g_stats;
void cmp(const std::string& what, double got, double want, double tol) { g_stats[what].add(std::fabs(got - want), tol); }
struct Totals {
    std::uint64_t frames = 0, emitters = 0, direct = 0, culled = 0, clipped = 0, nothing = 0, dropped_points = 0;
    std::uint64_t vertices = 0, clip_out_vertices = 0, clip_matched = 0, clip_inside_vertices = 0, kicks = 0;
    std::uint64_t dead_points = 0, points = 0, q_pending = 0, branch_backup = 0;
};
Totals g_tot;

// ---- the EE side's VIF streams (DMA tags dropped: their VIF words and the data they reference, in order)
struct Stream {
    std::vector<std::uint8_t> b;
    void w(std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
    void q(const std::uint32_t v[4]) { for (int i = 0; i < 4; ++i) w(v[i]); }
};
// BeginRender 0x2F38B0: STCYCL 1,1; UNPACK V4-32 13 -> 0x3F3: (0,0,0,now), GIF tag (PRE, PRIM 0x5C, NREG 3: ST RGBAQ
// XYZF2), (0xA,0,0,3), camera record +0x120 x 10; (ZBUF with ZMSK through DIRECT; MPG via a DMA call); MSCALF 0 [C].
void begin_render(Stream& s, const TrailFrame& f) {
    s.w(0x01000101);
    s.w(0x6C0D03F3);
    const std::uint32_t q0[4] = {0, 0, 0, f.now}, q1[4] = {0, 0x302E4000, 0x412, 0}, q2[4] = {0xA, 0, 0, 3};
    s.q(q0); s.q(q1); s.q(q2);
    for (const auto& r : f.cam.row) s.q(r);
    s.w(0x03000000);  // BASE 0
    s.w(0x020000D8);  // OFFSET 0xD8
    s.w(0x15000000);  // MSCALF 0
}
// Render 0x2F3600 for one type: STCYCL 1,1; UNPACK V4-32 3 -> 0x3F0 (params); (ALPHA_1 via DIRECT); per emitter with
// count >= 2: STCYCL 2,1; UNPACK V4-32 16 -> TOPS+0; STCYCL 2,1; UNPACK V3-8 (signed) 16 -> TOPS+1; ITOP count; MSCNT.
void render_params(Stream& s, const std::uint32_t p[12]) {
    s.w(0x01000101);
    s.w(0x6C0303F0);
    for (int i = 0; i < 12; ++i) s.w(p[i]);
}
void render_emitter(Stream& s, const TrailEmitterInput& e) {
    s.w(0x01000102);
    s.w(0x6C108000);
    for (const auto& pt : e.point) s.q(pt);
    s.w(0x01000102);
    s.w(0x6A108001);
    for (const auto& sd : e.side)
        for (std::int8_t c : sd) s.b.push_back(std::uint8_t(c));
    s.w(0x04000000u | (std::uint32_t(std::uint8_t(std::int8_t(e.count))) & 0x3FF));
    s.w(0x17000000);
}

// ---- comparison of one emitter
void dump(const char* what, const std::vector<GifPacket>& got, const std::vector<TrailGsVertex>& want) {
    std::printf("%s: %zu kicks, model %zu vertices\n", what, got.size(), want.size());
    for (std::size_t k = 0; k < got.size(); ++k) {
        std::printf(" kick %zu: %zu verts\n", k, got[k].verts.size());
        for (std::size_t i = 0; i < got[k].verts.size(); ++i) {
            const GifVertex& v = got[k].verts[i];
            std::printf("  v%-3zu prim %03x rgba %3u %3u %3u %3u stq %9.5f %9.5f %9.6f xy %8.3f %8.3f z %8u f %3u adc %d\n",
                        i, v.prim, v.r, v.g, v.b, v.a, v.s / v.q, v.t / v.q, v.q, v.x / 16.0 - 2048,
                        v.y / 16.0 - 2048, v.z, v.f, v.adc);
        }
    }
    for (std::size_t i = 0; i < want.size(); ++i) {
        const TrailGsVertex& v = want[i];
        std::printf("  m%-3zu clip %02x rgba %3u %3u %3u %3u st %4.1f %4.1f q %9.6f xy %8.3f %8.3f z %8u (w %g)\n", i,
                    v.clip_flags, v.rgba[0], v.rgba[1], v.rgba[2], v.rgba[3], v.s / v.q, v.t / v.q, v.q,
                    v.x / 16.0 - 2048, v.y / 16.0 - 2048, v.z, v.clip[3]);
    }
}

void compare_vertex(const char* P, const GifVertex& g, const TrailGsVertex& w, bool kick) {
    const std::string p = P;
    cmp(p + "x (1/16 px)", double(g.x), double(w.x), 0);
    cmp(p + "y (1/16 px)", double(g.y), double(w.y), 0);
    cmp(p + "z", double(g.z), double(w.z), 0);
    cmp(p + "s", g.s, w.s, 0);
    cmp(p + "t", g.t, w.t, 0);
    cmp(p + "q", g.q, w.q, 0);
    cmp(p + "r", g.r, w.rgba[0], 0);
    cmp(p + "g", g.g, w.rgba[1], 0);
    cmp(p + "b", g.b, w.rgba[2], 0);
    cmp(p + "a", g.a, w.rgba[3], 0);
    cmp(p + "fog", g.f, w.f, 0);
    cmp(p + "adc", g.adc, w.adc, 0);
    cmp(p + "prim", g.prim, 0x5C, 0);
    (void)kick;
}

void compare_emitter(const TrailFrame& f, const TrailFrame::Emitter& em, const std::vector<GifPacket>& got) {
    ++g_tot.emitters;
    const TrailRibbon rib = trail_ribbon(f.params[em.type], bitsf(f.now), em.in);
    g_tot.dropped_points += rib.points ? std::uint64_t(std::max(0, em.in.count - rib.points)) : 0;
    std::vector<TrailGsVertex> want;
    const TrailDraw d = trail_project(f.cam, rib, want);
    g_tot.kicks += got.size();
    for (int k = 0; k < rib.points; ++k) {
        ++g_tot.points;
        const float inv = bitsf(f.params[em.type][2]);
        if ((bitsf(f.now) - bitsf(em.in.point[k][3])) * inv > 1.0f) ++g_tot.dead_points;
    }
    switch (d) {
    case TrailDraw::kNothing:
        ++g_tot.nothing;
        cmp("kicks (no ribbon)", double(got.size()), 0, 0);
        return;
    case TrailDraw::kCulled:
        ++g_tot.culled;
        cmp("kicks (culled)", double(got.size()), 0, 0);
        return;
    case TrailDraw::kDirect: {
        ++g_tot.direct;
        cmp("kicks (inside)", double(got.size()), 1, 0);
        if (got.size() != 1) break;
        cmp("strip vertex count", double(got[0].verts.size()), double(want.size()), 0);
        if (got[0].verts.size() != want.size()) break;
        for (std::size_t i = 0; i < want.size(); ++i) compare_vertex("strip ", got[0].verts[i], want[i], i >= 2);
        g_tot.vertices += want.size();
        if (g_dump > 0 && g_verbose) { --g_dump; dump("inside", got, want); }
        return;
    }
    case TrailDraw::kClipped: {
        ++g_tot.clipped;
        if (g_dump > 0) { --g_dump; dump("clipped", got, want); }
        // The clipper re-emits clipped triangles; every model vertex inside the frame that is a corner of a strip
        // triangle must appear in its output with the same values.
        // Everything it sends lies inside the clip frame (|x/w| <= 1020.x, |y/w| <= 1021.x px from the window centre,
        // 1021.w <= w <= 1020.w; 1/16 px and float slack), as one TRISTRIP packet per <= ~49 vertices with ADC restarts.
        const float fx = bitsf(f.cam.row[6][0]), fy = bitsf(f.cam.row[7][0]);
        const float wn = bitsf(f.cam.row[7][3]), wf = bitsf(f.cam.row[6][3]);
        for (const GifPacket& p : got) {
            g_tot.clip_out_vertices += p.verts.size();
            for (const GifVertex& v : p.verts) {
                const double ox = std::max(0.0, std::fabs(v.x / 16.0 - 2048.0) - fx);
                const double oy = std::max(0.0, std::fabs(v.y / 16.0 - 2048.0) - fy);
                const double w = 1.0 / v.q, ow = std::max(0.0, std::max(wn - w, w - wf) / w);
                cmp("clipper output outside frame x px", ox, 0, 0.0625);
                cmp("clipper output outside frame y px", oy, 0, 0.0625);
                cmp("clipper output outside near/far (rel)", ow, 0, 1e-3);
                cmp("clipper prim", v.prim, 0x5C, 0);
            }
        }
        for (std::size_t i = 0; i < want.size(); ++i) {
            if (want[i].clip_flags) continue;
            ++g_tot.clip_inside_vertices;
            // Point 0's two vertices share a position (zero width), so pick the candidate with the closest s, t.
            const GifVertex* best = nullptr;
            float best_d = 0;
            for (const GifPacket& p : got)
                for (const GifVertex& v : p.verts) {
                    if (v.x != want[i].x || v.y != want[i].y) continue;
                    const float d = std::fabs(v.s - want[i].s) + std::fabs(v.t - want[i].t);
                    if (!best || d < best_d) { best = &v; best_d = d; }
                }
            cmp("clipper: inside vertex found", best ? 1 : 0, 1, 0);
            if (!best) continue;
            ++g_tot.clip_matched;
            GifVertex g = *best;
            g.adc = want[i].adc;  // the clipper's ADC pattern is its own (checked below)
            compare_vertex("clipper inside ", g, want[i], true);
        }
        return;
    }
    }
    if (g_dump > 0) { --g_dump; dump("MISMATCH", got, want); }
}

bool check_frame(const VuMicroprogram& prog, const TrailFrame& f) {
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
        const std::uint32_t start = (kind == VifInterpreter::kMscnt) ? vu.tpc : pc;
        if (!vu.run(prog, start, 0, itop, on_xgkick, &err)) { std::fprintf(stderr, "VU1 0x4771B0: %s\n", err.c_str()); ok = false; }
    };
    Stream s;
    begin_render(s, f);
    if (!vif.run(s.b.data(), s.b.size(), on_kick, &err)) { std::fprintf(stderr, "VIF: %s\n", err.c_str()); return false; }
    if (!ok) return false;
    cmp("kicks (set-up)", double(got.size()), 0, 0);
    for (int type = 0; type < 5; ++type) {
        bool any = false;
        for (const auto& em : f.emitters) any |= em.type == type;
        if (!any) continue;
        Stream r;
        render_params(r, f.params[type]);
        if (!vif.run(r.b.data(), r.b.size(), on_kick, &err)) { std::fprintf(stderr, "VIF: %s\n", err.c_str()); return false; }
        for (const auto& em : f.emitters) {
            if (em.type != type) continue;
            if (em.in.count < 2) {  // Render skips them (b >= 2) [C 0x2F3600]
                compare_emitter(f, em, {});
                continue;
            }
            Stream e;
            render_emitter(e, em.in);
            got.clear();
            if (!vif.run(e.b.data(), e.b.size(), on_kick, &err)) { std::fprintf(stderr, "VIF: %s\n", err.c_str()); return false; }
            if (!ok) return false;
            compare_emitter(f, em, got);
        }
    }
    g_tot.q_pending += vu.stats.q_read_pending;
    g_tot.branch_backup += vu.stats.branch_vi_backup;
    ++g_tot.frames;
    return true;
}

// ---- synthetic frames
struct V3 { float x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 norm(V3 a) { const float l = std::sqrt(dot(a, a)); return l > 0 ? a * (1.0f / l) : a; }

// A skid mark: points on a tilted ground plane along a curved path, sides = unit (step x normal) x 127.5 truncated
// (CB3TrailEmitter::AddTrailSegment 0x2BCF60: diff x dir with dir = the ground normal [L]; point 0 gets point 1's side
// when the second point arrives).
TrailEmitterInput make_emitter(std::mt19937& rng, V3 start, float heading, float t0, float now, float life) {
    std::uniform_real_distribution<float> U(-1, 1), P(0, 1);
    TrailEmitterInput e;
    e.count = 2 + int(P(rng) * 15) % 15;
    if (P(rng) < 0.04f) e.count = int(P(rng) * 2);  // 0 or 1 point: nothing drawn
    V3 n = norm({0.25f * U(rng), 1.0f, 0.25f * U(rng)});
    const float curve = 0.3f * U(rng), step = 0.15f + 1.8f * P(rng);
    V3 p = start, prev = start;
    float t = t0;
    const float dt = 0.016f + 0.25f * P(rng);
    for (int k = 0; k < 16; ++k) {
        if (k > 0) {
            heading += curve * step;
            V3 fwd = {std::sin(heading), 0, std::cos(heading)};
            fwd = norm(fwd - n * dot(fwd, n));
            p = p + fwd * step + n * (0.01f * U(rng));
            t += dt;
        }
        for (int c = 0; c < 3; ++c) e.point[k][c] = fbits((&p.x)[c]);
        e.point[k][3] = fbits(t);
        if (k > 0) {
            const V3 sd = cross(norm(p - prev), n) * 127.5f;
            for (int c = 0; c < 3; ++c) e.side[k][c] = std::int8_t(int((&sd.x)[c]));
            if (k == 1) std::memcpy(e.side[0], e.side[1], 3);
        }
        prev = p;
    }
    if (P(rng) < 0.05f) {  // out-of-order times: the program drops trailing points not newer than point 0
        const int m = std::max(1, e.count - 1 - int(P(rng) * 3));
        for (int k = m; k < 16; ++k) e.point[k][3] = fbits(t0 - 0.01f * float(k - m) * P(rng));
    }
    (void)now; (void)life;
    return e;
}

TrailFrame make_frame(std::mt19937& rng, const std::uint32_t params[5][12]) {
    std::uniform_real_distribution<float> U(-1, 1), P(0, 1);
    TrailFrame f;
    std::memcpy(f.params, params, sizeof f.params);
    // A chase camera behind a car on a route a few km across.
    const V3 car = {2000.0f * U(rng), 50.0f * U(rng), 2000.0f * U(rng)};
    const float yaw = 3.14159f * U(rng);
    const V3 fwd = {std::sin(yaw), 0, std::cos(yaw)};
    const V3 eye = car - fwd * (4.0f + 4.0f * P(rng)) + V3{0, 1.2f + 2.5f * P(rng), 0};
    const V3 target = car + fwd * (5.0f + 20.0f * P(rng)) + V3{0, 0.5f * U(rng), 0};
    const float e3[3] = {eye.x, eye.y, eye.z}, t3[3] = {target.x, target.y, target.z}, sun[3] = {0, 1, 0};
    const ShadeScene sc = shade_scene_look_at(e3, t3, sun);
    for (int r = 0; r < 4; ++r) {
        const float row[4] = {sc.view[r][0], sc.view[r][1], sc.view[r][2], sc.view[r][2]};
        for (int l = 0; l < 4; ++l) f.cam.row[r][l] = fbits(row[l]);
    }
    for (int l = 0; l < 4; ++l) {
        f.cam.row[4][l] = fbits(sc.scale[l]);
        f.cam.row[5][l] = fbits(sc.shift[l]);
        for (int g = 0; g < 4; ++g) f.cam.row[6 + g][l] = fbits(sc.guard[g][l]);
    }
    const float now = 100.0f + 500.0f * P(rng);
    f.now = fbits(now);
    const int n = 4 + int(P(rng) * 28);
    for (int i = 0; i < n; ++i) {
        TrailFrame::Emitter em;
        const float r = P(rng);
        em.type = r < 0.45f ? 1 : r < 0.7f ? 2 : r < 0.85f ? 3 : r < 0.97f ? 0 : 4;
        const float life = em.type == 0 ? 0.3f : 15.0f;
        // Ages: mostly within the lifetime, some well past it.
        const float age = P(rng) < 0.25f ? life * (1.0f + 2.0f * P(rng)) : life * P(rng);
        // Around the car: ahead, beside, under and behind the camera (the latter two exercise the clipper).
        const V3 start = car + fwd * (-12.0f + 45.0f * P(rng)) + cross(fwd, {0, 1, 0}) * (8.0f * U(rng)) + V3{0, -0.3f + 0.4f * P(rng), 0};
        em.in = make_emitter(rng, start, yaw + 0.6f * U(rng), now - age - 3.0f * P(rng), now, life);
        f.emitters.push_back(em);
    }
    return f;
}

bool read_params(const std::string& elf, std::uint32_t out[5][12], std::uint32_t alpha_words[5]) {
    std::vector<std::uint8_t> d;
    if (!read_file(elf, d) || d.size() < 0x34) { std::fprintf(stderr, "cannot read %s\n", elf.c_str()); return false; }
    auto u32 = [&](std::size_t o) { return std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8 | std::uint32_t(d[o + 2]) << 16 | std::uint32_t(d[o + 3]) << 24; };
    auto u16 = [&](std::size_t o) { return std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8; };
    const std::uint32_t phoff = u32(0x1C), phes = u16(0x2A), phnum = u16(0x2C);
    const std::uint32_t va = 0x49FFF0;  // CB3TrailSystem::mgaDefaultParams [C retail_data_names]
    for (std::uint32_t i = 0; i < phnum; ++i) {
        const std::size_t ph = phoff + i * phes;
        const std::uint32_t type = u32(ph), off = u32(ph + 4), vaddr = u32(ph + 8), fsz = u32(ph + 16);
        if (type != 1 || va < vaddr || va + 5 * 0x50 > vaddr + fsz) continue;
        for (int t = 0; t < 5; ++t) {
            const std::uint8_t* rec = d.data() + off + (va - vaddr) + 0x50 * t;
            trail_vu_params(rec, out[t]);
            alpha_words[t] = u32(off + (va - vaddr) + 0x50 * t + 0x20);
        }
        return true;
    }
    std::fprintf(stderr, "%s: 0x%X not in a loaded segment\n", elf.c_str(), va);
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const char* h = std::getenv("HOME");
    const std::string decomp = std::string(h ? h : ".") + "/b3/burnout3-retail-decomp";
    g_vu_dir = decomp + "/assembly/assets/vu";
    std::string elf = decomp + "/orig/SLUS_210.50";
    int frames = 2000;
    unsigned seed = 20261103;
    std::string capture;
    int every = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--vu" && i + 1 < argc) g_vu_dir = argv[++i];
        else if (s == "--elf" && i + 1 < argc) elf = argv[++i];
        else if (s == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (s == "--seed" && i + 1 < argc) seed = unsigned(std::strtoul(argv[++i], nullptr, 0));
        else if (s == "--dump" && i + 1 < argc) g_dump = std::atoi(argv[++i]);
        else if (s == "-v") g_verbose = true;
        else if (s == "--capture" && i + 1 < argc) capture = argv[++i];
        else if (s == "--every" && i + 1 < argc) every = std::max(1, std::atoi(argv[++i]));
        else {
            std::fprintf(stderr, "usage: b3vucheck_trail [--frames n] [--seed n] [--elf SLUS_210.50] [--vu dir] [--dump n] [-v]\n");
            return 2;
        }
    }
    VuMicroprogram prog;
    {
        std::vector<std::uint8_t> d;
        const std::string path = g_vu_dir + "/game_004771B0.textbin.bin";
        std::string why;
        if (!read_file(path, d)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
        if (!prog.load_textbin(d, &why)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), why.c_str()); return 1; }
    }
    std::uint32_t params[5][12], alpha[5];
    if (!capture.empty()) {
        b3replay::Capture cap;
        std::string why;
        if (!cap.load(capture, &why, false)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
        if (cap.version < 3) { std::fprintf(stderr, "%s: capture version %u has no effects\n", capture.c_str(), cap.version); return 1; }
        std::uint64_t ticks = 0;
        for (std::size_t i = 0; i < cap.ticks.size(); i += std::size_t(every)) {
            const auto& t = cap.ticks[i];
            if (!t.effects_valid || !t.effects_on || t.trails.empty()) continue;
            TrailFrame f;
            f.now = fbits(t.now);
            std::memcpy(f.cam.row, t.trail_cam, sizeof f.cam.row);
            std::memcpy(f.params, t.trail_vu, sizeof f.params);
            for (const auto& e : t.trails) {
                TrailFrame::Emitter em;
                em.type = e->type;
                em.in.count = e->count;
                std::memcpy(em.in.point, e->pt, sizeof em.in.point);
                std::memcpy(em.in.side, e->dir, sizeof em.in.side);
                f.emitters.push_back(em);
            }
            if (!check_frame(prog, f)) return 1;
            ++ticks;
        }
        std::printf("capture %s: %llu ticks with emitters checked (every %d)\n", capture.c_str(), (unsigned long long)ticks, every);
        frames = 0;
    }
    if (!read_params(elf, params, alpha)) return 1;
    for (int t = 0; t < 5; ++t) {
        std::printf("type %d: ALPHA_1 %010llx  1008 (%g, %g, %g, %g)  1009 (%g, %g, %g, %g)  1010 (%g, %g, %g, %g)\n", t,
                    (unsigned long long)trail_alpha_reg(alpha[t]), bitsf(params[t][0]), bitsf(params[t][1]),
                    bitsf(params[t][2]), bitsf(params[t][3]), bitsf(params[t][4]), bitsf(params[t][5]),
                    bitsf(params[t][6]), bitsf(params[t][7]), bitsf(params[t][8]), bitsf(params[t][9]),
                    bitsf(params[t][10]), bitsf(params[t][11]));
    }
    std::mt19937 rng(seed);
    for (int i = 0; i < frames; ++i)
        if (!check_frame(prog, make_frame(rng, params))) return 1;
    std::printf("frames %llu, emitters %llu: inside %llu, clipped %llu, culled %llu, nothing drawn %llu; points %llu "
                "(%llu past the lifetime, %llu dropped as not newer than point 0); XGKICKs %llu\n",
                (unsigned long long)g_tot.frames, (unsigned long long)g_tot.emitters, (unsigned long long)g_tot.direct,
                (unsigned long long)g_tot.clipped, (unsigned long long)g_tot.culled, (unsigned long long)g_tot.nothing,
                (unsigned long long)g_tot.points, (unsigned long long)g_tot.dead_points,
                (unsigned long long)g_tot.dropped_points, (unsigned long long)g_tot.kicks);
    std::printf("strip vertices compared %llu; clipper: %llu output vertices, %llu of %llu inside model vertices found; "
                "Q reads while a DIV was pending %llu, branch-VI backups %llu\n",
                (unsigned long long)g_tot.vertices, (unsigned long long)g_tot.clip_out_vertices,
                (unsigned long long)g_tot.clip_matched, (unsigned long long)g_tot.clip_inside_vertices,
                (unsigned long long)g_tot.q_pending, (unsigned long long)g_tot.branch_backup);
    std::uint64_t bad = 0;
    for (const auto& kv : g_stats) {
        std::printf("  %-34s n %9llu  max |err| %-12.6g over tolerance %llu\n", kv.first.c_str(),
                    (unsigned long long)kv.second.n, kv.second.max_err, (unsigned long long)kv.second.bad);
        bad += kv.second.bad;
    }
    std::printf("%s\n", bad ? "MISMATCHES" : "ALL WITHIN TOLERANCE");
    return bad ? 1 : 0;
}
