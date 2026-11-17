// b3vucheck_car: offline VU reference check of the car shading model (docs/car-shading.md).
//
// Runs the game's car microprograms in the b3asset VU interpreter -- VU0 0x47F6D0 (per part: vcallms 0x7D8, per mesh:
// vcallms 0xB50, as CB3VehicleRenderer::RenderObject 0x21FBB0 calls them) and VU1 0x46BB80 (per batch) -- on the
// decoded batches of real car files, with inputs built the way the EE side builds them, and compares every output
// with the C++ model in include/b3asset/carshade.h (the formulas the GPU shader implements).
// Game data and the decomp's microprogram images are read only; nothing is written.
//
//   b3vucheck_car <car file>... [--vu dir] [--dump] [--shader] [--zones <file>] [-v]
// --zones: damage zone matrices captured in a race (b3replay --info --dump-zones: u32 count 8 / 2, then count x 12
// floats) instead of random ones, 8-zone sets on the body list, 2-zone sets on the panel lists, none on the wheels, as
// RenderVehicle passes them (render6).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "b3asset/carshade.h"
#include "b3asset/export.h"
#include "b3asset/vif.h"
#include "b3asset/vu1.h"

using namespace b3asset;

namespace {

std::string g_vu_dir;
bool g_dump = false, g_verbose = false;
// --zones: captured zone sets (each count x rows x, y, z, translation)
std::vector<std::vector<float>> g_body_zones, g_panel_zones;
std::size_t g_zone_pick = 0;
bool load_zones(const std::string& path) {
    std::vector<std::uint8_t> d;
    if (!read_file(path, d)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return false; }
    for (std::size_t at = 0; at + 4 <= d.size();) {
        std::uint32_t n;
        std::memcpy(&n, d.data() + at, 4);
        at += 4;
        if ((n != 8 && n != 2) || at + 48 * n > d.size()) { std::fprintf(stderr, "%s: bad zone set\n", path.c_str()); return false; }
        std::vector<float> z(12 * n);
        std::memcpy(z.data(), d.data() + at, 48 * n);
        at += 48 * n;
        (n == 8 ? g_body_zones : g_panel_zones).push_back(std::move(z));
    }
    return !g_body_zones.empty();
}
// influences whose zone address is outside the header's zones (the VU would read other memory), per list kind
std::uint64_t g_infl_n[2] = {}, g_infl_outside[2] = {}, g_infl_misaligned = 0;

bool load_prog(std::uint32_t addr, VuMicroprogram& p) {
    char name[64];
    std::snprintf(name, sizeof name, "/game_%08X.textbin.bin", addr);
    std::vector<std::uint8_t> d;
    if (!read_file(g_vu_dir + name, d)) { std::fprintf(stderr, "cannot read %s%s\n", g_vu_dir.c_str(), name); return false; }
    std::string why;
    if (!p.load_textbin(d, &why)) { std::fprintf(stderr, "%s: %s\n", name, why.c_str()); return false; }
    return true;
}

std::uint32_t fbits(float f) { std::uint32_t v; std::memcpy(&v, &f, 4); return v; }
float bitsf(std::uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }
struct Batch {
    std::unique_ptr<VuMemory> mem;
    std::uint32_t itop = 0;
};
struct MeshEntry {
    std::uint32_t material = 0, colour = 0;
    int list = 0, index = 0;
    std::vector<Batch> batches;
};

// Walks the car's LOD block lists like b3asset::read_car (src/b3asset/car.cpp) and keeps every batch's VU memory.
bool collect(Bytes f, int lod, std::vector<MeshEntry>& out, std::string* why) {
    const std::size_t block = f.u32(0x4C + 4 * std::size_t(lod));
    if (!block) return true;
    for (int li = 0; li < 18; ++li) {
        const std::uint32_t rel = f.u32(block + 4 * std::size_t(li));
        if (!rel) continue;
        const std::size_t lst = block + rel;
        const int count = std::int8_t(f.p[lst]);
        const std::size_t entries = lst + f.u32(lst + 4);
        for (int i = 0; i < count; ++i) {
            const std::size_t e = entries + 16 * std::size_t(i);
            MeshEntry me;
            me.material = f.u32(e + 8);
            me.colour = f.u32(e + 12);
            me.list = li;
            me.index = i;
            VifInterpreter vif;
            auto kick = [&](const VuMemory& vu, std::uint32_t itop, VifInterpreter::KickKind, std::uint32_t) {
                Batch b;
                b.mem = std::make_unique<VuMemory>(vu);
                b.itop = itop;
                me.batches.push_back(std::move(b));
            };
            for (int c = 0; c < 2; ++c) {
                const std::uint32_t r2 = f.u32(e + 4 * std::size_t(c));
                if (!r2) continue;
                const std::size_t at = e + r2;
                const std::uint32_t tag = f.u32(at);
                const std::size_t qwc = tag & 0xFFFF;
                std::string err;
                if (!vif.run(f.p + at + 8, 8 + qwc * 16, kick, &err, 8)) { if (why) *why = err; return false; }
                vif.flush(kick);
            }
            out.push_back(std::move(me));
        }
    }
    return true;
}

// ---- comparison bookkeeping
struct Stat {
    double max_err = 0;
    std::uint64_t n = 0, bad = 0;
    void add(double e, double tol) { ++n; max_err = std::max(max_err, e); if (e > tol) ++bad; }
};
std::map<std::string, Stat> g_stats;
void cmp(const std::string& what, double got, double want, double tol) { g_stats[what].add(std::fabs(got - want), tol); }

struct Totals { std::uint64_t cars = 0, batches = 0, vertices = 0, env_vertices = 0, q_pending = 0, branch_backup = 0; };
Totals g_tot;
std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> g_materials;

// ---- test cases: inputs built the way the EE side builds them
struct TestCase {
    CarScene sc;
    CarPartInput part;
};

// RW view matrix rows for a camera at `eye` looking at `target` (x/z, y/z in [0,1] across a 60-degree window), as
// tools/render/b3vucheck.cpp builds it for the track.
void camera(CarScene& sc, const float eye[3], const float target[3]) {
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& c : f) c /= l;
    float r[3] = {f[2], 0, -f[0]};
    l = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    r[0] /= l; r[2] /= l;
    const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    const float tx = std::tan(0.5236f), ty = tx * 448.0f / 640.0f;
    float colx[3], coly[3];
    for (int i = 0; i < 3; ++i) {
        colx[i] = r[i] / (2 * tx) + 0.5f * f[i];
        coly[i] = -u[i] / (2 * ty) + 0.5f * f[i];
    }
    for (int i = 0; i < 3; ++i) { sc.view[i][0] = colx[i]; sc.view[i][1] = coly[i]; sc.view[i][2] = f[i]; }
    sc.view[3][0] = -(eye[0] * colx[0] + eye[1] * colx[1] + eye[2] * colx[2]);
    sc.view[3][1] = -(eye[0] * coly[0] + eye[1] * coly[1] + eye[2] * coly[2]);
    sc.view[3][2] = -(eye[0] * f[0] + eye[1] * f[1] + eye[2] * f[2]);
    for (int i = 0; i < 3; ++i) { sc.cam_pos[i] = eye[i]; sc.cam_up[i] = u[i]; }
}

TestCase make_case(std::mt19937& rng, bool damaged, bool clip, int variant) {
    std::uniform_real_distribution<float> U(-1, 1), P(0, 1);
    TestCase tc;
    // Near the origin: km-scale positions only add float cancellation (VU truncation vs host rounding) to the
    // projection, which says nothing about the shading (run with B3VUCHECK_FAR=1 to see it).
    const float far = std::getenv("B3VUCHECK_FAR") ? 2000.0f : 30.0f;
    const float car[3] = {far * U(rng), 30 * U(rng), far * U(rng)};
    const float yaw = 3.14159265f * U(rng), pitch = variant == 3 ? 0.2f * U(rng) : 0.0f;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    // part -> world: yaw about Y then a small pitch; rows right, up, at, pos
    const float R[3][3] = {{cy, 0, -sy}, {sy * sp, cp, cy * sp}, {sy * cp, -sp, cy * cp}};
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c) tc.part.mat[i][c] = R[i][c];
    for (int c = 0; c < 3; ++c) tc.part.mat[3][c] = car[c] + (variant ? 0.5f * U(rng) : 0.0f);
    for (int c = 0; c < 3; ++c) tc.part.car_pos[c] = car[c];
    const float ang = 3.14159265f * U(rng), dist = clip ? 2.5f + 2 * P(rng) : 6 + 8 * P(rng);
    const float eye[3] = {car[0] + dist * std::cos(ang), car[1] + 0.8f + 3 * P(rng), car[2] + dist * std::sin(ang)};
    camera(tc.sc, eye, car);
    tc.sc.sh[0] = 0.6f + 0.8f * P(rng);
    for (int k = 1; k < 9; ++k) tc.sc.sh[k] = (k < 4 ? 0.4f : 0.15f) * U(rng);
    tc.sc.shininess = variant == 2 ? 0.4f + 0.6f * P(rng) : 1.0f;
    tc.sc.lighting = variant == 2 ? 0.3f + 0.7f * P(rng) : 1.0f;
    tc.part.le_type = clip ? 2 : 0;
    const std::uint16_t lits[3] = {0, 0x0C, 0xFC};
    tc.part.lit_bits = lits[variant % 3];
    if (damaged) {
        tc.part.zones = 8;
        for (int z = 0; z < 8; ++z)
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 3; ++c)
                    tc.part.zone[z][r][c] = (r == c ? 1.0f : 0.0f) + (r == 3 ? 0.15f : 0.08f) * U(rng);
    }
    return tc;
}

void setv(std::uint32_t r[4], float a, float b, float c, float d) { r[0] = fbits(a); r[1] = fbits(b); r[2] = fbits(c); r[3] = fbits(d); }

// RenderObject 0x21FBB0 before `vcallms 0x7D8`: vf1-4 the part matrix, vf5-7 the SH coefficients (the 3x3 at
// CB3VehicleEssentialParams +0x5C copied by 0x220040 into scratch +0x310), vf8-11 the previous 966..969, vf12/vf13
// camera up / position (renderer +0xC0/+0xE0), vf14 car - camera (scratch +0x340) with w = object +0x08; vi3 = bool.
void load_part_registers(Vu1& v, const TestCase& tc) {
    const CarPartInput& p = tc.part;
    for (int r = 0; r < 4; ++r) setv(v.vf[1 + r], p.mat[r][0], p.mat[r][1], p.mat[r][2], r == 3 ? 1.0f : 0.0f);
    for (int r = 0; r < 3; ++r) setv(v.vf[5 + r], tc.sc.sh[3 * r], tc.sc.sh[3 * r + 1], tc.sc.sh[3 * r + 2], 0.0f);
    for (int r = 0; r < 4; ++r) setv(v.vf[8 + r], p.prev_m[r][0], p.prev_m[r][1], p.prev_m[r][2], p.prev_m[r][3]);
    setv(v.vf[12], tc.sc.cam_up[0], tc.sc.cam_up[1], tc.sc.cam_up[2], 0.0f);
    setv(v.vf[13], tc.sc.cam_pos[0], tc.sc.cam_pos[1], tc.sc.cam_pos[2], 1.0f);
    setv(v.vf[14], p.car_pos[0] - tc.sc.cam_pos[0], p.car_pos[1] - tc.sc.cam_pos[1], p.car_pos[2] - tc.sc.cam_pos[2], p.bias);
    v.vi[3] = p.rotate_lighting ? 1 : 0;
}
// RenderObject stores vf5..vf8 -> 962..965, vf1..vf4 -> 966..969, vf14 -> 970, vf9..vf11 -> 971..973 [C].
void read_part_registers(const Vu1& v, CarPartConstants& o) {
    auto rd = [&](int r, float* d4) { for (int l = 0; l < 4; ++l) d4[l] = bitsf(v.vf[r][l]); };
    for (int r = 0; r < 4; ++r) rd(5 + r, o.part[r]);
    for (int r = 0; r < 4; ++r) rd(1 + r, o.m[r]);
    rd(14, o.view);
    for (int r = 0; r < 3; ++r) rd(9 + r, o.env[r]);
}
using CmpFn = void (*)(const std::string&, double, double, double);
void compare_part(const CarPartConstants& g, const CarPartConstants& w, bool pitched, CmpFn c) {
    for (int r = 0; r < 4; ++r)
        for (int l = 0; l < 4; ++l) c("vu0 part matrix 962-965", g.part[r][l], w.part[r][l], 1e-5);
    // The SH matrix is compared by what it does: E(n) = (n,1) M (n,1)^T on unit normals (R&H matrices differ by
    // k diag(1,1,1,-1) without changing E on the sphere). Band 2 of the VU0 rotation is not an exact rotation when the
    // part is pitched/rolled (docs/car-shading.md §2): those parts are counted separately.
    for (int k = 0; k < 26; ++k) {
        const int a = k < 13 ? k : k + 1;  // the 26 neighbours of a 3x3x3 grid centre
        float n[3] = {float(a % 3 - 1), float(a / 3 % 3 - 1), float(a / 9 - 1)};
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (float& x : n) x /= l;
        auto E = [&](const float M[4][4]) {
            const float v[4] = {n[0], n[1], n[2], 1.0f};
            float e = 0;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) e += v[i] * M[i][j] * v[j];
            return e;
        };
        c(pitched ? "vu0 SH E(n), pitched parts" : "vu0 SH E(n), yawed parts", E(g.m), E(w.m), pitched ? 0.05 : 1e-5);
    }
    for (int l = 0; l < 4; ++l) c("vu0 view dir 970", g.view[l], w.view[l], 1e-5);
    // w: R (camera - position); the VU computes R camera - R position (km-scale positions: float cancellation).
    for (int r = 0; r < 3; ++r)
        for (int l = 0; l < 4; ++l) c("vu0 env basis 971-973", g.env[r][l], w.env[r][l], l == 3 ? 2e-3 : 1e-5);
}

// RenderObject before `vcallms 0xB50`: vi5 slot, vi4 mask, vi6 FIX, vi9 ALPHA byte, vf1-4 = scratch +0x00..+0x30
// (BeginRenderingSingleVehicle 0x220040; +0x34 the float argument, +0x38 the lit bits, +0x3C the header count).
void load_mesh_registers(Vu1& v, const TestCase& tc, const CarMeshInput& m) {
    const float t = 128.1f * tc.sc.lighting, h = 0.5f * t, f0 = tc.sc.shininess;
    setv(v.vf[1], h, h, h, 38.4f * f0);
    setv(v.vf[2], 0, 0, 0, 19.2f * f0);
    setv(v.vf[3], t, t, t, 128.0f * f0);
    v.vf[4][0] = fbits(128.1f * f0);
    v.vf[4][1] = fbits(tc.sc.fade);
    v.vf[4][2] = m.lit_bits;
    v.vf[4][3] = 13;
    v.vi[5] = m.tex_slot;
    v.vi[4] = m.mask;
    v.vi[6] = m.fix;
    v.vi[9] = m.alpha_byte;
}
// RenderObject: vf6/vf7/vf8 -> 1010/1011/1012, vf9 -> 1008 (A+D: x data, y FIX, z reg 0x42), vi7 -> 1013.y,
// vi8 -> 1009 (A+D ALPHA_2) [C].
void read_mesh_registers(const Vu1& v, CarMeshConstants& o) {
    for (int l = 0; l < 4; ++l) { o.base[l] = bitsf(v.vf[6][l]); o.full[l] = bitsf(v.vf[7][l]); o.env[l] = std::int32_t(v.vf[8][l]); }
    o.alpha1[0] = v.vf[9][0];
    o.alpha1[1] = v.vf[9][1];
    o.alpha2 = v.vi[8];
    o.no_env = v.vi[7];
}
void compare_mesh(const CarMeshConstants& g, const CarMeshConstants& w, CmpFn c) {
    for (int l = 0; l < 4; ++l) {
        c("vu0 base colour 1010", g.base[l], w.base[l], 1e-3);
        c("vu0 full colour 1011", g.full[l], w.full[l], 1e-3);
        c("vu0 env colour 1012", g.env[l], w.env[l], 0);
    }
    c("vu0 ALPHA_1 1008", g.alpha1[0], w.alpha1[0], 0);
    c("vu0 ALPHA_1 FIX 1008", g.alpha1[1], w.alpha1[1], 0);
    c("vu0 ALPHA_2 1009", g.alpha2, w.alpha2, 0);
    c("vu0 no-env 1013.y", g.no_env, w.no_env, 0);
}

void put(VuMemory& m, std::uint32_t qw, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    m.w[qw * 4] = a; m.w[qw * 4 + 1] = b; m.w[qw * 4 + 2] = c; m.w[qw * 4 + 3] = d;
}
void putf(VuMemory& m, std::uint32_t qw, const float* f4) { put(m, qw, fbits(f4[0]), fbits(f4[1]), fbits(f4[2]), fbits(f4[3])); }

// The per-part header (RenderObject: UNPACK V4-32 to 962, 12 qw + 4 per zone), the per-mesh block 1006..1013
// (UNPACK to 1006), and the camera block 1014..1023 (BeginVehicleRendering / SetupCameraData: camera data +0x120).
void write_vu1_constants(VuMemory& mem, const TestCase& tc, const CarPartConstants& p, const CarMeshConstants& m) {
    for (int r = 0; r < 4; ++r) putf(mem, 962 + r, p.part[r]);
    for (int r = 0; r < 4; ++r) putf(mem, 966 + r, p.m[r]);
    putf(mem, 970, p.view);
    for (int r = 0; r < 3; ++r) putf(mem, 971 + r, p.env[r]);
    for (int z = 0; z < tc.part.zones; ++z)
        for (int r = 0; r < 4; ++r) {
            const float f4[4] = {tc.part.zone[z][r][0], tc.part.zone[z][r][1], tc.part.zone[z][r][2], 0.0f};
            putf(mem, 974 + 4 * z + r, f4);
        }
    put(mem, 1006, 0, 0, 0, 0);  // no A+D prefix (its content is only forwarded)
    put(mem, 1007, 0, 0, 0, 0);
    put(mem, 1008, m.alpha1[0], m.alpha1[1], 0x42, 0);
    put(mem, 1009, m.alpha2, 0, 0x43, 0);
    putf(mem, 1010, m.base);
    putf(mem, 1011, m.full);
    put(mem, 1012, std::uint32_t(m.env[0]), std::uint32_t(m.env[1]), std::uint32_t(m.env[2]), std::uint32_t(m.env[3]));
    put(mem, 1013, std::uint32_t(tc.part.le_type), m.no_env, std::uint32_t(tc.part.zones), 0);
    for (int r = 0; r < 4; ++r) {
        const float f4[4] = {tc.sc.view[r][0], tc.sc.view[r][1], tc.sc.view[r][2], tc.sc.view[r][2]};
        putf(mem, 1014 + r, f4);
    }
    putf(mem, 1018, tc.sc.scale);
    putf(mem, 1019, tc.sc.shift);
    for (int r = 0; r < 4; ++r) putf(mem, 1020 + r, tc.sc.guard[r]);
}

void dump_batch(const VuMemory& in, std::uint32_t n, const std::vector<GifPacket>& out, const std::vector<CarVertexOut>& want) {
    std::printf("batch: %u vertices, %zu kicks\n", n, out.size());
    for (std::size_t k = 0; k < out.size(); ++k) {
        std::printf(" kick %zu: %zu verts, %zu A+D\n", k, out[k].verts.size(), out[k].ad.size());
        for (std::size_t i = 0; i < out[k].verts.size(); ++i) {
            if (i % n > 3) continue;
            const GifVertex& v = out[k].verts[i];
            std::printf("  v%-3zu prim %03x rgba %3u %3u %3u %3u stq %9.5f %9.5f %9.6f xyz %7.1f %7.1f %8u adc %d\n", i, v.prim,
                        v.r, v.g, v.b, v.a, double(v.s), double(v.t), double(v.q), v.x / 16.0, v.y / 16.0, v.z, v.adc);
            const CarVertexOut& w = want[i % n];
            if (i < n)
                std::printf("  model     rgba %3d %3d %3d %3d stq %9.5f %9.5f %9.6f xyz %7.1f %7.1f %8.0f adc %d E %g\n",
                            w.rgba[0], w.rgba[1], w.rgba[2], w.rgba[3], double(w.s), double(w.t), double(w.q),
                            double(w.x), double(w.y), double(w.z), w.adc, double(w.light));
            else
                std::printf("  model env st %9.5f %9.5f (x q %9.5f %9.5f)\n", double(w.es), double(w.et), double(w.es * w.q),
                            double(w.et * w.q));
        }
    }
    (void)in;
}

std::uint64_t g_rgba_out_of_range = 0, g_skipped_behind = 0;
void compare_batch(const std::vector<GifPacket>& out, const std::vector<CarVertexOut>& want, const CarMeshConstants& m,
                   const TestCase& tc, CmpFn c, std::uint64_t& nv, std::uint64_t& nenv) {
    std::vector<GifVertex> verts;
    for (const auto& p : out) verts.insert(verts.end(), p.verts.begin(), p.verts.end());
    const std::size_t n = want.size();
    const std::size_t expect = m.no_env ? n : 2 * n;
    c("gif vertex count", double(verts.size()), double(expect), 0);
    if (verts.size() != expect) return;
    auto rel = [](double g, double w) { return std::fabs(g - w) / std::max(1e-3, std::fabs(w)); };
    for (std::size_t i = 0; i < n; ++i) {
        const GifVertex& g = verts[i];
        const CarVertexOut& w = want[i];
        ++nv;
        const std::uint8_t gr[4] = {g.r, g.g, g.b, g.a};
        for (int k = 0; k < 4; ++k) {
            if (w.rgba[k] < 0 || w.rgba[k] > 255) ++g_rgba_out_of_range;
            // The GS keeps the low 8 bits of each RGBAQ lane; compare those (wrap-around included).
            const int d = std::abs(int(gr[k]) - int(std::uint8_t(w.rgba[k])));
            c(k < 3 ? "vu1 pass-1 rgb" : "vu1 pass-1 alpha", std::min(d, 256 - d), 0, 1);
        }
        c("gif prim pass 1", g.prim, 0x5C, 0);
        if (w.q <= 0) { ++g_skipped_behind; continue; }
        // q = 1/w: compared as w (view depth, metres); vertices within centimetres of the camera plane carry float
        // cancellation from the matrix products that a relative test on q would magnify.
        c("vu1 w = 1/q (m)", 1.0 / g.q, 1.0 / w.q, 5e-5);
        if (g_verbose && std::fabs(1.0 / g.q - 1.0 / w.q) > 5e-5)
            std::printf("    q mismatch: vertex %zu of %zu, le_type %d, env %d: got %g want %g (s %g / %g)\n", i, n,
                        tc.part.le_type, int(!m.no_env), double(g.q), double(w.q), double(g.s), double(w.s));
        c("vu1 s/q = u", g.s / g.q, w.s / w.q, 1e-4);
        c("vu1 t/q = v", g.t / g.q, w.t / w.q, 1e-4);
        const double gx = g.x / 16.0, gy = g.y / 16.0;
        if (std::fabs(w.x) < 4000 && std::fabs(w.y) < 4000 && w.x > 0 && w.y > 0) {
            c("vu1 screen x", gx, w.x, 0.15);
            c("vu1 screen y", gy, w.y, 0.15);
            if (w.z > 0 && w.z < 16777215) c("vu1 depth z (rel)", rel(double(g.z), w.z), 0, 1e-4);
        }
        if (i >= 2) c(tc.part.le_type ? "vu1 adc (guard-band clip)" : "vu1 adc", g.adc, w.adc, 0);
        if (!m.no_env) {
            const GifVertex& e = verts[n + i];
            ++nenv;
            c("gif prim pass 2", e.prim, 0x25C, 0);
            c("vu1 env s", e.s / e.q, w.es, 2e-3);
            c("vu1 env t", e.t / e.q, w.et, 2e-3);
            c("vu1 env w = 1/q (m)", 1.0 / e.q, 1.0 / w.q, 5e-5);
            const std::uint8_t er[4] = {e.r, e.g, e.b, e.a};
            for (int k = 0; k < 4; ++k) c("vu1 env rgba", er[k], std::uint8_t(m.env[k]), 0);
            c("vu1 env xy = pass 1", std::fabs(double(e.x) - g.x) + std::fabs(double(e.y) - g.y), 0, 0);
        }
    }
}

// --shader: b3gfx car.vert's world-space form of the same math (render5) against the part-space model, per vertex:
// world normal n_w = mat3(model) n, E = (n_w,1) M_world (n_w,1)^T, f = |n_w . normalise(car - camera)|, and the
// reflected eye ray r_w = d - 2 (n_w.d) n_w, which the check maps through the game's sphere-map basis (971..973) to
// compare with the model's (s, t); E and RGB against the VU0's own matrix and the VU1 lanes (the model's E is computed
// with the interpreter's 966..969). Inputs are the batch's own part-space position, normal and influences, deformed as
// car.vert does it (render6: zone_share, the rest weight on the identity), compared with the model's deformed values,
// which compare_batch checks against the interpreter's projection.
bool g_shader = false;
void compare_shader(const VuMemory& in, const std::vector<CarVertexOut>& want, const CarMeshConstants& m, const TestCase& tc,
                    bool pitched) {
    const auto& R = tc.part.mat;
    float W[4][4];
    car_irradiance_matrix(tc.sc.sh, W);
    float V[3] = {tc.part.car_pos[0] - tc.sc.cam_pos[0], tc.part.car_pos[1] - tc.sc.cam_pos[1], tc.part.car_pos[2] - tc.sc.cam_pos[2]};
    const float vl = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    for (float& c : V) c /= vl;
    // the game's sphere-map basis in world axes [C VU0 0x0FC..0x158]
    float Z[3] = {tc.sc.cam_pos[0] - R[3][0], tc.sc.cam_pos[1] - R[3][1], tc.sc.cam_pos[2] - R[3][2]};
    float zl = std::sqrt(Z[0] * Z[0] + Z[1] * Z[1] + Z[2] * Z[2]);
    for (float& c : Z) c /= zl;
    const float* up = tc.sc.cam_up;
    float X[3] = {up[1] * Z[2] - up[2] * Z[1], up[2] * Z[0] - up[0] * Z[2], up[0] * Z[1] - up[1] * Z[0]};
    const float xl = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]);
    for (float& c : X) c /= xl;
    const float Y[3] = {Z[1] * X[2] - Z[2] * X[1], Z[2] * X[0] - Z[0] * X[2], Z[0] * X[1] - Z[1] * X[0]};
    for (std::size_t vi = 0; vi < want.size(); ++vi) {
        const CarVertexOut& w = want[vi];
        // car.vert: in_pos, in_normal (raw - bias), in_extra = the two influences (b3asset car.cpp's slots)
        const std::uint32_t i = std::uint32_t(vi), g = i & ~3u, j = i & 3;
        float ip[3], inn[3], dp[3], dn[3];
        for (int c = 0; c < 3; ++c) { ip[c] = bitsf(in.qw(3 * i)[c]); inn[c] = bitsf(in.qw(3 * i + 2)[c]) - tc.part.bias; }
        const std::uint32_t ux[2] = {in.qw(3 * (g + 1) + 1)[j] & 0xFFFF, in.qw(3 * (g + 3) + 1)[j] & 0xFFFF};
        std::memcpy(dp, ip, sizeof dp);
        std::memcpy(dn, inn, sizeof dn);
        if (tc.part.zones > 0) {
            float Z[4][3] = {};
            for (std::uint32_t u : ux) {
                const int k = int((974u + u) & 1023u) - 974;
                if (k < 0 || (k & 3) != 0 || k / 4 >= tc.part.zones) continue;
                for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c) Z[r][c] += tc.part.zone[k / 4][r][c] * (float(u) / 65535.0f);
            }
            const float rest = 1.0f - float(ux[0]) / 65535.0f - float(ux[1]) / 65535.0f;
            for (int c = 0; c < 3; ++c) {
                dp[c] = Z[3][c] + Z[0][c] * ip[0] + Z[1][c] * ip[1] + Z[2][c] * ip[2] + rest * ip[c];
                dn[c] = Z[0][c] * inn[0] + Z[1][c] * inn[1] + Z[2][c] * inn[2] + rest * inn[c];
            }
            for (int c = 0; c < 3; ++c) {
                cmp("shader deformed position vs model (m)", dp[c], w.pos[c], 1e-4);
                cmp("shader deformed normal vs model", dn[c], w.normal[c], 1e-5);
            }
        }
        float n[3], p[3];
        for (int c = 0; c < 3; ++c) {
            n[c] = dn[0] * R[0][c] + dn[1] * R[1][c] + dn[2] * R[2][c];
            p[c] = dp[0] * R[0][c] + dp[1] * R[1][c] + dp[2] * R[2][c] + R[3][c];
        }
        const float n1[4] = {n[0], n[1], n[2], 1.0f};
        double E = 0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) E += double(n1[i]) * W[i][j] * n1[j];
        E += car_irradiance_k(W, R[2]) * (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] - 1.0);
        const float f = std::fabs(n[0] * V[0] + n[1] * V[1] + n[2] * V[2]), F = 2 * f - f * f;
        // against the VU0's own matrix (the model's E): exact for yawed parts, the band-2 rotation differs when pitched
        cmp(pitched ? "shader E vs VU0, pitched parts" : "shader E vs VU0, yawed parts", E, w.light, pitched ? 0.05 : 1e-4);
        for (int k = 0; k < 3; ++k)
            cmp(pitched ? "shader rgb vs VU1, pitched (ints)" : "shader rgb vs VU1, yawed (ints)",
                std::trunc(m.base[k] + float(E) * (m.full[k] - m.base[k])), w.rgba[k], pitched ? 8 : 1);
        cmp("shader alpha vs VU1 (ints)", std::trunc(std::max(0.0f, m.full[3]) - F * (m.full[3] - m.base[3])), w.rgba[3], 1);
        if (m.no_env) continue;
        const float d[3] = {p[0] - tc.sc.cam_pos[0], p[1] - tc.sc.cam_pos[1], p[2] - tc.sc.cam_pos[2]};
        const float nd = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
        const float r[3] = {d[0] - 2 * nd * n[0], d[1] - 2 * nd * n[1], d[2] - 2 * nd * n[2]};
        const float rv[3] = {-(X[0] * r[0] + X[1] * r[1] + X[2] * r[2]), -(Y[0] * r[0] + Y[1] * r[1] + Y[2] * r[2]),
                             Z[0] * r[0] + Z[1] * r[1] + Z[2] * r[2]};
        const float mz = rv[2] + std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float ml = std::sqrt(rv[0] * rv[0] + rv[1] * rv[1] + mz * mz);
        cmp("shader env s", rv[0] * 0.5f / ml + 0.5f, w.es, 2e-3);
        cmp("shader env t", rv[1] * 0.5f / ml + 0.5f, w.et, 2e-3);
    }
}

// One car: every LOD, every mesh entry, one test case per LOD (damaged, clipped and lighting variants rotate).
bool check_car(const std::string& path, const VuMicroprogram& vu0, const VuMicroprogram& vu1, std::mt19937& rng) {
    std::vector<std::uint8_t> d;
    if (!read_file(path, d)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return false; }
    Bytes f{d.data(), d.size()};
    if (f.u32(0) != 0x17) { std::fprintf(stderr, "%s: not a vehicle file\n", path.c_str()); return false; }
    ++g_tot.cars;
    for (int lod = 0; lod < 5; ++lod) {
        std::vector<MeshEntry> meshes;
        std::string why;
        if (!collect(f, lod, meshes, &why)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), why.c_str()); return false; }
        if (meshes.empty()) continue;
        const int k = int(g_tot.cars * 5 + std::uint64_t(lod));
        TestCase tc = make_case(rng, k % 2 == 1, k % 3 == 2, k % 4);
        VuMemory vu0mem;
        Vu1 v0(vu0mem);
        std::string err;
        CarPartConstants got_part{};
        for (int pass = 0; pass < 2; ++pass) {
            // pass 0 builds the SH matrix (bool true), pass 1 reuses it (bool false), as body then bits.
            tc.part.rotate_lighting = pass == 0;
            if (pass == 1) std::memcpy(tc.part.prev_m, got_part.m, sizeof got_part.m);
            load_part_registers(v0, tc);
            if (!v0.run(vu0, 0x7D8 / 8, 0, 0, nullptr, &err)) { std::fprintf(stderr, "VU0 0x7D8: %s\n", err.c_str()); return false; }
            read_part_registers(v0, got_part);
            compare_part(got_part, car_part_constants(tc.sc, tc.part), (k % 4) == 3, cmp);
        }
        const bool damaged = tc.part.zones > 0;
        for (const MeshEntry& me : meshes) {
            // influences against the header RenderVehicle uploads for the list: body 8 zones, panels 2 (wheels none)
            if (me.list <= 6)
                for (const Batch& b : me.batches)
                    for (std::uint32_t i = 0; i < b.itop; ++i) {
                        const std::uint32_t g = i & ~3u, j = i & 3;
                        for (std::uint32_t slot : {g + 1, g + 3}) {
                            const std::uint32_t u = b.mem->qw(3 * slot + 1)[j] & 0xFFFF;
                            if (u == 0) continue;
                            const int k = int((974u + u) & 1023u) - 974;
                            const int kind = me.list == 0 ? 0 : 1;
                            ++g_infl_n[kind];
                            if (k & 3) ++g_infl_misaligned;
                            if (k < 0 || k / 4 >= (kind == 0 ? 8 : 2)) ++g_infl_outside[kind];
                        }
                    }
            if (damaged && !g_body_zones.empty()) {
                // the captured sets, as RenderVehicle passes them per list
                std::memset(tc.part.zone, 0, sizeof tc.part.zone);
                tc.part.zones = me.list == 0 ? 8 : (me.list <= 6 && !g_panel_zones.empty()) ? 2 : 0;
                const auto& sets = tc.part.zones == 8 ? g_body_zones : g_panel_zones;
                if (tc.part.zones > 0) {
                    const std::vector<float>& z = sets[g_zone_pick++ % sets.size()];
                    for (int q = 0; q < tc.part.zones; ++q)
                        for (int r = 0; r < 4; ++r)
                            for (int c = 0; c < 3; ++c) tc.part.zone[q][r][c] = z[std::size_t(12 * q + 3 * r + c)];
                }
            }
            const CarMeshInput mi = car_mesh_input(me.material, me.colour, tc.part.lit_bits);
            load_mesh_registers(v0, tc, mi);
            if (!v0.run(vu0, 0xB50 / 8, 0, 0, nullptr, &err)) { std::fprintf(stderr, "VU0 0xB50: %s\n", err.c_str()); return false; }
            CarMeshConstants got_mesh{};
            read_mesh_registers(v0, got_mesh);
            compare_mesh(got_mesh, car_mesh_constants(tc.sc, mi), cmp);
            ++g_materials[{me.material, me.colour}];
            for (const Batch& b : me.batches) {
                if (!b.itop) continue;
                VuMemory mem = *b.mem;
                write_vu1_constants(mem, tc, got_part, got_mesh);
                Vu1 v1(mem);
                std::vector<GifPacket> out;
                const auto kick = [&](std::uint32_t qa) {
                    GifPacket p;
                    std::string e2;
                    if (!read_gif_packet(mem, qa, p, &e2)) std::fprintf(stderr, "GIF: %s\n", e2.c_str());
                    out.push_back(std::move(p));
                };
                if (!v1.run(vu1, 0, 0, b.itop, kick, &err) || !v1.run(vu1, v1.tpc, 0, b.itop, kick, &err)) {
                    std::fprintf(stderr, "VU1 0x46BB80: %s\n", err.c_str());
                    return false;
                }
                g_tot.q_pending += v1.stats.q_read_pending;
                g_tot.branch_backup += v1.stats.branch_vi_backup;
                ++g_tot.batches;
                std::vector<CarVertexOut> want;
                car_batch(*b.mem, b.itop, tc.sc, got_part, got_mesh, tc.part, want);
                if (g_dump && g_tot.batches <= 2) dump_batch(*b.mem, b.itop, out, want);
                compare_batch(out, want, got_mesh, tc, cmp, g_tot.vertices, g_tot.env_vertices);
                if (g_shader) compare_shader(*b.mem, want, got_mesh, tc, (k % 4) == 3);
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const char* h = std::getenv("HOME");
    g_vu_dir = std::string(h ? h : ".") + "/b3/burnout3-retail-decomp/assembly/assets/vu";
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--vu" && i + 1 < argc) g_vu_dir = argv[++i];
        else if (s == "--dump") g_dump = true;
        else if (s == "--shader") g_shader = true;
        else if (s == "--zones" && i + 1 < argc) { if (!load_zones(argv[++i])) return 1; }
        else if (s == "-v") g_verbose = true;
        else files.push_back(s);
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: b3vucheck_car <CARn.BGV|BTV>... [--vu dir] [--dump] [--shader] [-v]\n");
        return 2;
    }
    VuMicroprogram vu0, vu1;
    if (!load_prog(0x47F6D0, vu0) || !load_prog(0x46BB80, vu1)) return 1;
    std::mt19937 rng(20261102);
    for (const auto& f : files)
        if (!check_car(f, vu0, vu1, rng)) return 1;
    std::printf("cars %llu, batches %llu, vertices %llu (+%llu env-pass vertices); Q reads while a DIV was pending %llu, "
                "branch-VI backups %llu\n",
                (unsigned long long)g_tot.cars, (unsigned long long)g_tot.batches, (unsigned long long)g_tot.vertices,
                (unsigned long long)g_tot.env_vertices, (unsigned long long)g_tot.q_pending,
                (unsigned long long)g_tot.branch_backup);
    std::uint64_t bad = 0;
    for (const auto& kv : g_stats) {
        std::printf("  %-28s n %9llu  max |err| %-12.6g over tolerance %llu\n", kv.first.c_str(),
                    (unsigned long long)kv.second.n, kv.second.max_err, (unsigned long long)kv.second.bad);
        bad += kv.second.bad;
    }
    std::printf("model RGBA lanes outside 0..255 (the GS keeps the low 8 bits): %llu; vertices behind the camera "
                "(colour-only check): %llu; distinct (material, colour) words: %zu\n",
                (unsigned long long)g_rgba_out_of_range, (unsigned long long)g_skipped_behind, g_materials.size());
    if (g_verbose)
        for (const auto& kv : g_materials)
            std::printf("  material %08x colour %08x: %llu meshes\n", kv.first.first, kv.first.second, (unsigned long long)kv.second);
    std::printf("influences (nonzero) on the body / panel lists: %llu / %llu; zone address outside the header's 8 / 2 zones: "
                "%llu / %llu; not a multiple of 4: %llu%s\n",
                (unsigned long long)g_infl_n[0], (unsigned long long)g_infl_n[1], (unsigned long long)g_infl_outside[0],
                (unsigned long long)g_infl_outside[1], (unsigned long long)g_infl_misaligned,
                g_body_zones.empty() ? "" : ("; captured zone sets " + std::to_string(g_body_zones.size()) + " body / " +
                                             std::to_string(g_panel_zones.size()) + " panel").c_str());
    std::printf("%s\n", bad ? "MISMATCHES" : "ALL WITHIN TOLERANCE");
    return bad ? 1 : 0;
}
