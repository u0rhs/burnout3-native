// b3vucheck_corona: the corona checks of docs/native-renderer.md §16, on a scene capture made with
// B3PORT_SCENE_CORONA_CHECK=1 (scene_capture.cpp: the game's own corona slots for player 0 in every tick).
//
//   EE      b3replay::build_coronas (the slots rebuilt from the captured inputs, the way RenderLightCoronas 0x256690,
//           func_00257080, AddTrafficCorona 0x256FD0 and func_00257230 write them) against the slots the game wrote:
//           per array the count, then every slot's position, fade distance, colour and size
//   VU      the game's corona microprogram VU1 0x478260 in the b3asset VU1 interpreter on the game's slots (the VIF
//           stream of CB3CoronaRenderer::EndRender 0x3070B0 / UploadCodeAndStaticData 0x306E80 / ConstructDMAData
//           0x3076D0: camera block 1015..1023, BASE 0 / OFFSET 0x90, per 72 slots UNPACK V4-32 to TOPS, ITOP, MSCAL /
//           MSCNT) against b3asset::corona_sprites: every GS sprite bit for bit
//   GPU     b3replay::corona_quad (the drawing path's float model) against the program's sprites: the same slots
//           drawn, the same colour, corners within a pixel (projected with the captured camera rows)
// Nothing is written.
//
//   b3vucheck_corona --capture <file> [--vu dir] [--every n] [--dump n]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "b3asset/coronashade.h"
#include "b3asset/export.h"
#include "b3asset/vif.h"
#include "b3asset/vu1.h"
#include "../../src/b3replay/capture.h"
#include "../../src/b3replay/coronas.h"

using namespace b3asset;

namespace {

int g_dump = 0;
float bitsf(std::uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }
std::uint32_t fbits(float f) { std::uint32_t v; std::memcpy(&v, &f, 4); return v; }

struct Stat {
    double max_err = 0;
    std::uint64_t n = 0, bad = 0;
    void add(double e, double tol) { ++n; max_err = std::max(max_err, e); if (e > tol) ++bad; }
};
std::map<std::string, Stat> g_stats;
void cmp(const std::string& what, double got, double want, double tol) { g_stats[what].add(std::fabs(got - want), tol); }
std::map<std::string, std::uint64_t> g_count;

struct Stream {
    std::vector<std::uint8_t> b;
    void w(std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
};

// The program on one array's slots, as the EE side sends them; the GIF packets it kicks.
bool run_vu(const VuMicroprogram& prog, const CoronaCamera& cam, const std::vector<CoronaSlotWords>& slots,
            std::vector<GifPacket>& got) {
    VifInterpreter vif;
    Vu1 vu(vif.mem);
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
        if (!vu.run(prog, start, 0, itop, on_xgkick, &err)) { std::fprintf(stderr, "VU1 0x478260: %s\n", err.c_str()); ok = false; }
    };
    Stream s;
    s.w(0x01000101);   // STCYCL 1,1
    s.w(0x6C0903F7);   // UNPACK V4-32 x 9 -> 1015
    for (const auto& r : cam.row)
        for (std::uint32_t v : r) s.w(v);
    s.w(0x03000000);   // BASE 0
    s.w(0x02000090);   // OFFSET 0x90
    for (std::size_t i = 0; i < slots.size(); i += 72) {
        const std::uint32_t n = std::uint32_t(std::min<std::size_t>(72, slots.size() - i));
        s.w(0x01000101);
        s.w(0x6C008000u | (2 * n) << 16);
        for (std::uint32_t k = 0; k < n; ++k)
            for (std::uint32_t v : slots[i + k].w) s.w(v);
        s.w(0x04000000u | n);                       // ITOP n
        s.w(i == 0 ? 0x15000000u : 0x17000000u);    // MSCAL 0 / MSCNT
    }
    if (!vif.run(s.b.data(), s.b.size(), on_kick, &err)) { std::fprintf(stderr, "VIF: %s\n", err.c_str()); return false; }
    return ok;
}

CoronaSlotWords words(const b3replay::CoronaSlot& s) {
    CoronaSlotWords w;
    for (int l = 0; l < 4; ++l) { w.w[l] = fbits(s.pos[l]); w.w[4 + l] = fbits(s.col[l]); }
    return w;
}

// The program's view rows (camera record +0xE0, 1015..1018: x/w, y/w in 0..1 across the window) as GS pixels.
void project(const CoronaCamera& cam, const float p[3], double& gx, double& gy) {
    double v[3];
    for (int l = 0; l < 3; ++l) {
        const int lane = l == 2 ? 3 : l;
        v[l] = p[0] * double(bitsf(cam.row[0][lane])) + p[1] * double(bitsf(cam.row[1][lane])) + p[2] * double(bitsf(cam.row[2][lane])) +
               double(bitsf(cam.row[3][lane]));
    }
    const double W = bitsf(cam.row[6][0]), H = bitsf(cam.row[6][1]);
    gx = bitsf(cam.row[5][0]) + (v[0] / v[2] - 0.5) * W;
    gy = bitsf(cam.row[5][1]) + (v[1] / v[2] - 0.5) * H;
}

void check_tick(const VuMicroprogram& prog, const b3replay::Tick& t, const b3replay::Tick* prev, std::uint64_t& ticks) {
    const auto& in = t.coronas;
    if (!in.valid || !in.check || !in.vu_cam_valid || !t.cam.valid || !b3replay::race_tick(t)) return;
    ++ticks;
    // the EE comparison needs a frame rendered between the previous poll and this one (the arrays' half flipped): in a
    // frame of N ticks, records 2..N hold vehicles moved on by the ticks since that frame's render
    const bool rendered = prev && prev->coronas.half[0] >= 0 && prev->coronas.half[0] != in.half[0];
    ++g_count[rendered ? "ticks after a render (EE compared)" : "ticks without a render since the last poll (EE skipped)"];
    // camera point: +0x1F810 against the viewport camera (pos - 2 x at)
    for (int l = 0; l < 3; ++l) cmp("cam_point vs pos - 2 at (m)", in.cam_point[l], t.cam.m.r[3][l] - 2.0f * t.cam.m.r[2][l], 1e-3);
    cmp("K (+0x208) vs 1 / half_w", bitsf(in.vu_cam.row[7][2]), 1.0 / t.cam.half_w, 1e-6);
    // EE
    b3replay::CoronaArrays mine;
    b3replay::build_coronas(in, t.vehicles, in.cam_point, t.cam.lod_pos, mine);
    for (int a = 0; a < 3 && rendered; ++a) {
        const auto& g = in.game[a];
        const auto& m = mine.slot[a];
        g_count["slots game a" + std::to_string(a)] += g.size();
        g_count["slots ours a" + std::to_string(a)] += m.size();
        if (g.size() != m.size()) {
            ++g_count["EE count differs a" + std::to_string(a) + (g.size() > m.size() ? " (game more)" : " (ours more)")];
            if (g_dump > 0) {
                --g_dump;
                std::printf("poll %u array %d: game %zu slots, ours %zu\n", t.poll, a, g.size(), m.size());
                for (const auto& s : g) std::printf("  game %10.3f %10.3f %10.3f w %4.0f  col %7.3f %7.3f %7.3f size %6.3f\n", s.pos[0], s.pos[1], s.pos[2], s.pos[3], s.col[0], s.col[1], s.col[2], s.col[3]);
                for (std::size_t i = 0; i < m.size(); ++i) { const auto& s = m[i]; std::printf("  ours %10.3f %10.3f %10.3f w %4.0f  col %7.3f %7.3f %7.3f size %6.3f src %d\n", s.pos[0], s.pos[1], s.pos[2], s.pos[3], s.col[0], s.col[1], s.col[2], s.col[3], int(mine.src[a][i])); }
            }
            continue;
        }
        // in the game's order when it matches, else matched by position
        std::vector<std::size_t> pick(g.size());
        bool ordered = true;
        for (std::size_t i = 0; i < g.size(); ++i) {
            float d = 0;
            for (int l = 0; l < 3; ++l) d = std::max(d, std::fabs(g[i].pos[l] - m[i].pos[l]));
            if (d > 0.05f || g[i].pos[3] != m[i].pos[3]) ordered = false;
        }
        if (ordered) for (std::size_t i = 0; i < g.size(); ++i) pick[i] = i;
        else {
            ++g_count["EE other order a" + std::to_string(a)];
            // global greedy: the closest (same fade distance) pairs first
            std::vector<std::tuple<float, std::size_t, std::size_t>> pairs;
            for (std::size_t i = 0; i < g.size(); ++i)
                for (std::size_t j = 0; j < m.size(); ++j) {
                    if (m[j].pos[3] != g[i].pos[3]) continue;
                    float d = 0;
                    for (int l = 0; l < 3; ++l) d += (g[i].pos[l] - m[j].pos[l]) * (g[i].pos[l] - m[j].pos[l]);
                    pairs.emplace_back(d, i, j);
                }
            std::sort(pairs.begin(), pairs.end());
            std::vector<bool> ug(g.size()), um(m.size());
            for (const auto& [d, i, j] : pairs) {
                if (ug[i] || um[j]) continue;
                ug[i] = um[j] = true;
                pick[i] = j;
            }
        }
        const std::string A = " a" + std::to_string(a);
        for (std::size_t i = 0; i < g.size(); ++i) {
            const auto& x = g[i];
            const auto& y = m[pick[i]];
            for (int l = 0; l < 3; ++l) cmp("EE position (m)" + A, y.pos[l], x.pos[l], 2e-3);
            cmp("EE fade distance" + A, y.pos[3], x.pos[3], 0);
            const bool boost = mine.src[a][pick[i]] == b3replay::CoronaSource::kBoost;
            static const char* kSrc[] = {"traffic", "crashed", "signal", "unassigned", "race car", "boost"};
            const std::string S = std::string(" (") + kSrc[int(mine.src[a][pick[i]])] + ")";
            for (int l = 0; l < 3; ++l) cmp("EE colour (GS units)" + A + S, y.col[l], x.col[l], 0.05);
            for (int l = 0; l < 3; ++l) cmp("EE position by source (m)" + A + S, y.pos[l], x.pos[l], 2e-3);
            if (g_dump > 0 && (std::fabs(y.pos[0] - x.pos[0]) > 0.01f || std::fabs(y.col[1] - x.col[1]) > 0.05f)) {
                --g_dump;
                std::printf("poll %u%s game %.3f %.3f %.3f col %.3f %.3f %.3f size %.3f | ours %.3f %.3f %.3f col %.3f %.3f %.3f size %.3f\n", t.poll, S.c_str(),
                            x.pos[0], x.pos[1], x.pos[2], x.col[0], x.col[1], x.col[2], x.col[3], y.pos[0], y.pos[1], y.pos[2], y.col[0], y.col[1], y.col[2], y.col[3]);
            }
            cmp("EE size (m)" + A, y.col[3], x.col[3], 1e-3);
            if (boost) {
                bool exact = true;
                for (int l = 0; l < 4; ++l) exact = exact && fbits(y.col[l]) == fbits(x.col[l]) && fbits(y.pos[l]) == fbits(x.pos[l]);
                ++g_count[std::string(exact ? "EE boost slots bit-exact" : "EE boost slots not bit-exact") + A];
            }
        }
        if (mine.dropped[a]) g_count["EE dropped (array full)" + A] += std::uint64_t(mine.dropped[a]);
    }
    // VU: the program on the game's slots against the model; GPU path against both
    for (int a = 0; a < 3; ++a) {
        std::vector<CoronaSlotWords> sw;
        for (const auto& s : in.game[a]) sw.push_back(words(s));
        if (sw.empty()) continue;
        std::vector<GifPacket> got;
        if (!run_vu(prog, in.vu_cam, sw, got)) { ++g_count["VU run failed"]; continue; }
        std::vector<const GifVertex*> gv;
        for (const auto& p : got)
            for (const auto& v : p.verts) gv.push_back(&v);
        const auto want = corona_sprites(in.vu_cam, sw);
        g_count["VU sprites"] += want.size();
        g_count["VU slots"] += sw.size();
        if (gv.size() != 2 * want.size()) {
            ++g_count["VU sprite count differs"];
            if (g_dump > 0) { --g_dump; std::printf("poll %u array %d: program %zu vertices, model %zu sprites\n", t.poll, a, gv.size(), want.size()); }
            continue;
        }
        for (std::size_t i = 0; i < want.size(); ++i) {
            const auto& s = want[i];
            for (int k = 0; k < 2; ++k) {
                const GifVertex& v = *gv[2 * i + k];
                cmp("VU x (1/16 px)", v.x, s.x[k], 0);
                cmp("VU y (1/16 px)", v.y, s.y[k], 0);
                cmp("VU z", v.z, s.z[k], 0);
                cmp("VU s", v.s, bitsf(s.st[k][0]), 0);
                cmp("VU t", v.t, bitsf(s.st[k][1]), 0);
                cmp("VU q", v.q, bitsf(s.st[k][2]), 0);
                cmp("VU r", v.r, s.rgba[0], 0);
                cmp("VU g", v.g, s.rgba[1], 0);
                cmp("VU b", v.b, s.rgba[2], 0);
                cmp("VU a", v.a, s.rgba[3], 0);
                cmp("VU prim", v.prim, 0x56, 0);
            }
        }
        // GPU path: corona_quad from the viewport camera
        std::vector<bool> drawn(sw.size(), false);
        for (const auto& s : want) drawn[std::size_t(s.slot)] = true;
        std::size_t wi = 0;
        for (std::size_t i = 0; i < sw.size(); ++i) {
            b3replay::CoronaQuad q;
            const bool mine_drawn = b3replay::corona_quad(t.cam, in.game[a][i], q);
            if (mine_drawn != drawn[i]) {
                ++g_count[mine_drawn ? "GPU drawn, program not" : "GPU not drawn, program drawn"];
                if (drawn[i]) ++wi;
                continue;
            }
            if (!drawn[i]) continue;
            const auto& s = want[wi++];
            // the drawing path's fade is host float (the program's is truncating): a channel can land 1 apart
            for (int l = 0; l < 3; ++l) {
                cmp("GPU colour (GS units)", q.rgb[l], s.rgba[l], 1);
                if (q.rgb[l] != s.rgba[l]) ++g_count["GPU colour channels 1 apart"];
            }
            // corners: quad corner 0 (top left) = sprite vertex 0, corner 2 = vertex 1
            for (int k = 0; k < 2; ++k) {
                double gx, gy;
                project(in.vu_cam, q.corner[k == 0 ? 0 : 2], gx, gy);
                cmp("GPU corner x (px)", gx, s.x[k] / 16.0, 1.0);
                cmp("GPU corner y (px)", gy, s.y[k] / 16.0, 1.0);
            }
            ++g_count["GPU sprites compared"];
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* h = std::getenv("HOME");
    std::string vu_dir = std::string(h ? h : ".") + "/b3/burnout3-retail-decomp/assembly/assets/vu";
    std::string capture;
    int every = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--vu" && i + 1 < argc) vu_dir = argv[++i];
        else if (s == "--capture" && i + 1 < argc) capture = argv[++i];
        else if (s == "--every" && i + 1 < argc) every = std::max(1, std::atoi(argv[++i]));
        else if (s == "--dump" && i + 1 < argc) g_dump = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "usage: b3vucheck_corona --capture <file> [--vu dir] [--every n] [--dump n]\n"); return 2; }
    }
    if (capture.empty()) { std::fprintf(stderr, "usage: b3vucheck_corona --capture <file> [--vu dir] [--every n] [--dump n]\n"); return 2; }
    VuMicroprogram prog;
    {
        std::vector<std::uint8_t> d;
        const std::string path = vu_dir + "/game_00478260.textbin.bin";
        std::string why;
        if (!read_file(path, d)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
        if (!prog.load_textbin(d, &why)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), why.c_str()); return 1; }
    }
    b3replay::Capture cap;
    std::string why;
    if (!cap.load(capture, &why, false)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
    std::uint64_t ticks = 0;
    for (std::size_t i = 0; i < cap.ticks.size(); i += std::size_t(every)) check_tick(prog, cap.ticks[i], i ? &cap.ticks[i - 1] : nullptr, ticks);
    std::printf("%s: %llu ticks with the game's corona slots (of %zu)\n", capture.c_str(), static_cast<unsigned long long>(ticks), cap.ticks.size());
    for (const auto& [k, v] : g_count) std::printf("  %-40s %llu\n", k.c_str(), static_cast<unsigned long long>(v));
    bool all = true;
    for (const auto& [k, v] : g_count)   // a slot count that differs is a failure too
        if (k.rfind("EE count differs", 0) == 0 && v) all = false;
    for (const auto& [k, s] : g_stats) {
        std::printf("  %-40s n %10llu  max |diff| %-12.6g over tolerance %llu\n", k.c_str(), static_cast<unsigned long long>(s.n), s.max_err,
                    static_cast<unsigned long long>(s.bad));
        all = all && s.bad == 0;
    }
    std::printf("%s\n", all ? "ALL WITHIN TOLERANCE" : "DIFFERENCES OVER TOLERANCE");
    return all ? 0 : 1;
}
