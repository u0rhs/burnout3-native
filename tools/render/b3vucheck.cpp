// b3vucheck: offline VU1 reference check for the native renderer's track shading (docs/native-renderer.md §11).
//
// Runs the game's own VU1 microprograms (the decomp's `assembly/assets/vu/game_*.textbin.bin`, read only) in the
// b3asset VU1 interpreter on decoded track batches, and compares what they send to the GS (per-vertex colour, STQ,
// screen position, fog, ADC/culling) with the C++ model of the same math that the GPU shaders implement
// (include/b3asset/trackshade.h). Game data and the decomp are read only; nothing is written.
//
//   b3vucheck check <route dir> [--units a n] [--samples n] [--vu dir]   compare program output with the model
//   b3vucheck dump  <route dir> <material type> [--units a n] [--vu dir] print one batch's input and GS output
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "b3asset/export.h"
#include "b3asset/raster.h"
#include "b3asset/track.h"
#include "b3asset/trackshade.h"
#include "b3asset/vu1.h"

using namespace b3asset;
namespace fs = std::filesystem;

namespace {

// Per-output comparison statistics, keyed by "program/output".
struct Stat { std::uint64_t n = 0, bad = 0; double max_err = 0; };
struct CheckTotals {
    std::uint64_t q_read_pending = 0, branch_vi_backup = 0, batches = 0, vertices = 0, culled = 0, drawn = 0;
    std::map<std::string, Stat> st;
    std::uint64_t facing[2][2] = {};
    std::uint64_t floors[2] = {};  // near-horizontal triangles by the sign of the emitted-order normal's y  // [world normal of the emitted triangle faces the eye][the program drew it]
    std::vector<std::string> examples;
};
void note(CheckTotals& t, const std::string& key, double err, double tol, const std::string& what) {
    Stat& s = t.st[key];
    ++s.n;
    s.max_err = std::max(s.max_err, err);
    if (err > tol) {
        ++s.bad;
        if (s.bad <= 4) t.examples.push_back(key + ": " + what);
    }
}
void compare_batch(const VuMemory& mem, std::uint32_t itop, const std::map<std::uint32_t, int>&,
                   const TrackMaterialInfo& m, const ShadeScene& sc, std::uint32_t clip, std::uint32_t prog,
                   const std::vector<GifPacket>& out, CheckTotals& t) {
    std::vector<TrackVertexOut> want;
    shade_track_batch(sc, m, track_batch_inputs(mem, itop, m.type), want);
    char pk[16];
    std::snprintf(pk, sizeof pk, "%06X%s", prog, clip ? "c" : sc.no_specular ? "n" : "");
    const std::string P = pk;
    ++t.batches;
    if (out.size() != 1 || out[0].verts.size() != itop) {
        note(t, P + " packet", 1, 0, "kicks " + std::to_string(out.size()) + " verts " +
             std::to_string(out.empty() ? 0 : out[0].verts.size()) + " want " + std::to_string(itop));
        return;
    }
    for (std::uint32_t i = 0; i < itop; ++i) {
        const GifVertex& g = out[0].verts[i];
        const TrackVertexOut& w = want[i];
        ++t.vertices;
        char buf[200];
        const double gu = g.s / g.q, gv = g.t / g.q;
        std::snprintf(buf, sizeof buf, "v%u got u %.5f v %.5f want %.5f %.5f", i, gu, gv, double(w.u), double(w.v));
        if (std::fabs(w.q) > 1e-6 && std::isfinite(gu)) {
            note(t, P + " u", std::fabs(gu - w.u), 2e-3 + 1e-4 * std::fabs(w.u), buf);
            note(t, P + " v", std::fabs(gv - w.v), 2e-3 + 1e-4 * std::fabs(w.v), buf);
        }
        std::snprintf(buf, sizeof buf, "v%u got q %.8g want %.8g", i, double(g.q), double(w.q));
        note(t, P + " q", std::fabs(g.q - w.q) / std::max(1e-6, std::fabs(double(w.q))), 1e-3, buf);
        if (w.q > 0) {
            std::snprintf(buf, sizeof buf, "v%u got x %.2f y %.2f want %.2f %.2f", i, g.x / 16.0, g.y / 16.0, double(w.x), double(w.y));
            const double ex = std::fabs(double(std::int32_t(g.x)) / 16.0 - std::fmod(double(w.x) + 65536.0, 4096.0));
            const double ey = std::fabs(double(std::int32_t(g.y)) / 16.0 - std::fmod(double(w.y) + 65536.0, 4096.0));
            if (std::fabs(w.x - 2048) < 2000 && std::fabs(w.y - 2048) < 2000) note(t, P + " xy", std::max(ex, ey), 0.2, buf);
        }
        const std::uint8_t gc[4] = {g.r, g.g, g.b, g.a};
        for (int c = 0; c < 4; ++c) {
            std::snprintf(buf, sizeof buf, "v%u lane %d got %u want %u", i, c, gc[c], w.rgba[c]);
            note(t, P + (c < 3 ? " rgb" : " alpha"), std::fabs(double(gc[c]) - double(w.rgba[c])), c < 3 ? 0 : 2, buf);
        }
        if (w.fog >= 0 && w.q > 0) {
            std::snprintf(buf, sizeof buf, "v%u got F %u want %d", i, g.f, w.fog);
            note(t, P + " fog", std::fabs(double(g.f) - w.fog), 1, buf);
        }
        if (!clip && i >= 2 && !(mem.qw(3 * i)[3] & 0x8000) && !(m.flags & 0x20) && m.type != 3) {
            // Emitted order as strip_to_triangles: (i-2, i-1, i) for even i, (i-1, i-2, i) for odd i.
            const std::uint32_t ia = (i & 1) ? i - 1 : i - 2, ib = (i & 1) ? i - 2 : i - 1;
            float A[3], B[3], C[3];
            for (int k = 0; k < 3; ++k) { A[k] = mem.f(3 * ia, k); B[k] = mem.f(3 * ib, k); C[k] = mem.f(3 * i, k); }
            const float u1[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]}, u2[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]};
            const float n[3] = {u1[1] * u2[2] - u1[2] * u2[1], u1[2] * u2[0] - u1[0] * u2[2], u1[0] * u2[1] - u1[1] * u2[0]};
            const float e[3] = {sc.eye[0] - A[0], sc.eye[1] - A[1], sc.eye[2] - A[2]};
            const bool face = n[0] * e[0] + n[1] * e[1] + n[2] * e[2] > 0;
            if (w.q > 0 && out[0].verts[i - 1].q > 0 && out[0].verts[i - 2].q > 0) ++t.facing[face][!g.adc];
            const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (nl > 0 && std::fabs(n[1]) > 0.9f * nl) ++t.floors[n[1] > 0];
        }
        if (!clip && i >= 2) {
            const bool gd = !g.adc;
            if (gd) ++t.drawn; else if (!(mem.qw(3 * i)[3] & 0x8000)) ++t.culled;
            std::snprintf(buf, sizeof buf, "v%u got drawn %d want %d", i, gd, w.drawn);
            note(t, P + " drawn", gd != w.drawn, 0, buf);
        }
    }
}
void print_totals(const CheckTotals& t) {
    std::printf("batches %llu, vertices %llu (drawn %llu, back-face culled %llu); Q reads while a DIV was pending %llu;"
                " branch-VI backups %llu\n", (unsigned long long)t.batches, (unsigned long long)t.vertices,
                (unsigned long long)t.drawn, (unsigned long long)t.culled, (unsigned long long)t.q_read_pending,
                (unsigned long long)t.branch_vi_backup);
    std::printf("culling vs world winding (emitted order (i-2,i-1,i) even / (i-1,i-2,i) odd, normal = (b-a)x(c-a)):\n"
                "  normal faces the eye: drawn %llu culled %llu; faces away: drawn %llu culled %llu\n",
                (unsigned long long)t.facing[1][1], (unsigned long long)t.facing[1][0], (unsigned long long)t.facing[0][1],
                (unsigned long long)t.facing[0][0]);
    std::printf("  near-horizontal triangles: emitted-order normal up %llu, down %llu\n", (unsigned long long)t.floors[1],
                (unsigned long long)t.floors[0]);
    std::printf("%-16s %10s %10s %12s\n", "program/output", "compared", "mismatch", "max error");
    for (const auto& kv : t.st)
        std::printf("%-16s %10llu %10llu %12.6g\n", kv.first.c_str(), (unsigned long long)kv.second.n,
                    (unsigned long long)kv.second.bad, kv.second.max_err);
    for (const auto& e : t.examples) std::printf("  e.g. %s\n", e.c_str());
}

struct Batch {
    std::unique_ptr<VuMemory> mem;
    std::uint32_t itop = 0;
    std::map<std::uint32_t, int> fmts;
    int material = -1, transform = -1;
};

struct Args {
    std::string route, vu_dir, cmd;
    int first = 0, count = 6, samples = 200, type = 0;
    bool verbose = false;
};

std::string default_vu_dir() {
    const char* h = std::getenv("HOME");
    return std::string(h ? h : ".") + "/b3/burnout3-retail-decomp/assembly/assets/vu";
}

bool load_prog(const std::string& dir, std::uint32_t addr, VuMicroprogram& p) {
    char name[64];
    std::snprintf(name, sizeof name, "/game_%08X.textbin.bin", addr);
    std::vector<std::uint8_t> d;
    if (!read_file(dir + name, d)) { std::fprintf(stderr, "cannot read %s%s\n", dir.c_str(), name); return false; }
    std::string why;
    if (!p.load_textbin(d, &why)) { std::fprintf(stderr, "%s: %s\n", name, why.c_str()); return false; }
    return true;
}


std::map<std::string, std::map<int, std::uint64_t>> hist;  // stats: vertex alpha classes per material state

struct RunState {
    std::map<std::uint32_t, VuMicroprogram> progs;
    std::mt19937 rng{12345};
    CheckTotals tot;
    int dumped = 0;
};

int run_route(const Args& a, const std::string& route, RunState& rs) {
    std::vector<std::uint8_t> st, sm;
    if (!read_file(route + "/STATIC.DAT", st)) { std::fprintf(stderr, "no STATIC.DAT in %s\n", route.c_str()); return 1; }
    read_file(route + "/STREAMED.DAT", sm);
    std::vector<Batch> batches;
    std::map<int, int> per_type;
    std::vector<TrackMaterialInfo> mats;
    TrackOptions opt;
    opt.first_unit = a.first;
    opt.unit_count = a.count;
    opt.lines = true;
    opt.extra_backdrops = true;
    // Material types are only known after the reader loaded the materials: keep everything, sample afterwards.
    opt.on_batch = [&](const VuMemory& vu, std::uint32_t itop, const std::map<std::uint32_t, int>& f, int mi, int tr) {
        Batch b;
        b.mem = std::make_unique<VuMemory>(vu);
        b.itop = itop;
        b.fmts = f;
        b.material = mi;
        b.transform = tr;
        batches.push_back(std::move(b));
    };
    GsMemory gs;
    Model model;
    std::string why;
    if (!read_track(Bytes{st.data(), st.size()}, Bytes{sm.data(), sm.size()}, opt, gs, model, &why, &mats)) {
        std::fprintf(stderr, "%s: %s\n", route.c_str(), why.c_str());
        return 1;
    }
    auto& progs = rs.progs;
    auto& rng = rs.rng;
    auto& tot = rs.tot;
    auto& dumped = rs.dumped;
    const float sun[3] = {0.3f, 0.8f, -0.52f};
    if (a.cmd == "stats") {  // vertex alpha by (type, TEST, ALPHA): what the GS alpha path sees
        for (const Batch& b : batches) {
            if (b.material < 0 || std::size_t(b.material) >= mats.size()) continue;
            const TrackMaterialInfo& m = mats[std::size_t(b.material)];
            if (b.fmts.count(2) == 0 || b.fmts.at(2) != 0xE) continue;
            char k[96];
            std::snprintf(k, sizeof k, "type %u TEST %05x ALPHA %02x/%02x flags&0x1f %02x", m.type, m.test, m.alpha, m.alpha_fix, m.flags & 0x1F);
            for (std::uint32_t i = 0; i < b.itop; ++i) {
                const int av = int(b.mem->qw(3 * i + 2)[3] & 0xFF);
                ++hist[k][av == 0 ? 0 : av < 0x40 ? 1 : av < 0x80 ? 2 : av == 0x80 ? 3 : 4];
            }
        }
        return 0;
    }
    for (const Batch& b : batches) {
        if (b.material < 0 || std::size_t(b.material) >= mats.size()) continue;
        const TrackMaterialInfo& m = mats[std::size_t(b.material)];
        if (m.type == 4 || m.type == 5) continue;  // instance programs need the instance constants (not modelled yet)
        if (a.cmd == "dump" && int(m.type) != a.type) continue;
        if (per_type[int(m.type)]++ >= a.samples) continue;
        const std::uint32_t pa = track_program(m);
        if (!progs.count(pa) && !load_prog(a.vu_dir, pa, progs[pa])) return 1;
        // Camera: 25 m back and 12 m up from the batch centre, looking at it.
        float c[3] = {0, 0, 0};
        for (std::uint32_t i = 0; i < b.itop; ++i)
            for (int k = 0; k < 3; ++k) c[k] += b.mem->f(3 * i, k) / float(b.itop);
        const float ang = std::uniform_real_distribution<float>(0, 6.2831853f)(rng);
        const float eye[3] = {c[0] + 80 * std::cos(ang), c[1] + 50, c[2] + 80 * std::sin(ang)};
        ShadeScene sc = shade_scene_look_at(eye, c, sun);
        sc.uv_scroll_on = m.flags & 0x200;
        sc.uv_scroll = 0.375f;
        for (int variant = 0; variant < 3; ++variant) {
            // 0: fast path; 1: fast path, RenderMesh's last argument set (no specular); 2: guard-band test path.
            const std::uint32_t clip = variant == 2 ? 2u : 0u;
            sc.no_specular = variant == 1;
            if (variant == 1 && m.type != 1 && m.type != 2) continue;
            VuMemory mem = *b.mem;
            track_vu_constants(sc, m, clip, mem);
            Vu1 vu(mem);
            std::vector<GifPacket> out;
            std::string err;
            const auto kick = [&](std::uint32_t qa) {
                GifPacket p;
                std::string e2;
                if (!read_gif_packet(mem, qa, p, &e2)) std::fprintf(stderr, "GIF: %s\n", e2.c_str());
                out.push_back(std::move(p));
            };
            if (!vu.run(progs[pa], 0, 0, b.itop, kick, &err) || !vu.run(progs[pa], vu.tpc, 0, b.itop, kick, &err)) {
                std::fprintf(stderr, "VU1 %06X: %s\n", pa, err.c_str());
                return 1;
            }
            tot.q_read_pending += vu.stats.q_read_pending;
            tot.branch_vi_backup += vu.stats.branch_vi_backup;
            if (a.cmd == "dump" && dumped < 1 && clip == 0) {
                ++dumped;
                std::printf("material %d type %u flags 0x%04x test 0x%x alpha 0x%02x/%02x p4 %g p8 %g prog %06X itop %u kicks %zu cycles %llu\n",
                            b.material, m.type, m.flags, m.test, m.alpha, m.alpha_fix, double(m.param4), double(m.param8), pa,
                            b.itop, out.size(), (unsigned long long)vu.stats.cycles);
                for (auto& kv : b.fmts) std::printf("  unpack @%u fmt 0x%x\n", kv.first, kv.second);
                for (int r = 28; r < 32; ++r) {
                    float f4[4];
                    std::memcpy(f4, vu.vf[r], 16);
                    std::printf("  vf%d %g %g %g %g\n", r, double(f4[0]), double(f4[1]), double(f4[2]), double(f4[3]));
                }
                {
                    float vz = sc.view[3][2];
                    for (int k = 0; k < 3; ++k) vz += b.mem->f(0, k) * sc.view[k][2];
                    std::printf("  model w of vertex 0: %g\n", double(vz));
                }
                for (std::uint32_t i = 0; i < std::min<std::uint32_t>(b.itop, 8); ++i) {
                    const std::uint32_t* q1 = b.mem->qw(3 * i + 1);
                    const std::uint32_t* q2 = b.mem->qw(3 * i + 2);
                    std::printf("  in %2u pos %9.3f %9.3f %9.3f w %04x  @1 %08x %08x %08x %08x  @2 %08x %08x %08x %08x\n", i,
                                double(b.mem->f(3 * i, 0)), double(b.mem->f(3 * i, 1)), double(b.mem->f(3 * i, 2)),
                                b.mem->qw(3 * i)[3], q1[0], q1[1], q1[2], q1[3], q2[0], q2[1], q2[2], q2[3]);
                }
                for (std::size_t k = 0; k < out.size(); ++k) {
                    std::printf(" kick %zu: %zu verts, %zu A+D, %u qw\n", k, out[k].verts.size(), out[k].ad.size(), out[k].qwords);
                    for (auto& ad : out[k].ad) std::printf("   A+D reg 0x%02x = %016llx\n", ad.first, (unsigned long long)ad.second);
                    for (std::size_t i = 0; i < std::min<std::size_t>(out[k].verts.size(), 8); ++i) {
                        const GifVertex& v = out[k].verts[i];
                        std::printf("   v%2zu prim %03x rgba %3u %3u %3u %3u stq %9.5f %9.5f %9.6f xyz %5.1f %5.1f %8u f %3u adc %d\n", i,
                                    v.prim, v.r, v.g, v.b, v.a, double(v.s), double(v.t), double(v.q), v.x / 16.0, v.y / 16.0, v.z, v.f, v.adc);
                    }
                }
            }
            if (a.cmd == "check") compare_batch(*b.mem, b.itop, b.fmts, m, sc, clip, pa, out, tot);
        }
    }
    return 0;
}

int run(const Args& a) {
    // A route folder, or a folder of areas/routes (e.g. the install's TRACKS): every route below it.
    std::vector<std::string> routes;
    if (fs::exists(a.route + "/STATIC.DAT")) routes.push_back(a.route);
    else
        for (const auto& e : fs::recursive_directory_iterator(a.route))
            if (e.is_regular_file() && e.path().filename() == "STATIC.DAT") routes.push_back(e.path().parent_path().string());
    std::sort(routes.begin(), routes.end());
    RunState rs;
    for (const std::string& r : routes) {
        if (int rc = run_route(a, r, rs)) return rc;
    }
    if (a.cmd == "stats") {
        std::printf("%-58s %9s %9s %9s %9s %9s\n", "material state", "a=0", "<0x40", "<0x80", "=0x80", ">0x80");
        for (const auto& kv : hist) {
            std::printf("%-58s", kv.first.c_str());
            for (int c = 0; c < 5; ++c) std::printf(" %9llu", (unsigned long long)(kv.second.count(c) ? kv.second.at(c) : 0));
            std::printf("\n");
        }
        return 0;
    }
    if (a.cmd == "check") {
        std::printf("routes %zu\n", routes.size());
        print_totals(rs.tot);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    a.vu_dir = default_vu_dir();
    if (argc < 3) {
        std::fprintf(stderr, "usage: b3vucheck check <route dir> [--units first count] [--samples n] [--vu dir] [-v]\n"
                             "       b3vucheck dump <route dir> <material type> [--units first count] [--vu dir]\n");
        return 2;
    }
    a.cmd = argv[1];
    a.route = argv[2];
    int i = 3;
    if (a.cmd == "dump") { if (argc < 4) return 2; a.type = std::atoi(argv[3]); i = 4; }
    if (a.cmd == "stats") a.count = -1;
    for (; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--units" && i + 2 < argc) { a.first = std::atoi(argv[i + 1]); a.count = std::atoi(argv[i + 2]); i += 2; }
        else if (s == "--samples" && i + 1 < argc) a.samples = std::atoi(argv[++i]);
        else if (s == "--vu" && i + 1 < argc) a.vu_dir = argv[++i];
        else if (s == "-v") a.verbose = true;
    }
    return run(a);
}
