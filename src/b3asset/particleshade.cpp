#include "b3asset/particleshade.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "b3asset/vif.h"
#include "b3asset/vu1.h"

namespace b3asset {

struct ParticleVu::Prog {
    VuMicroprogram mp;
};

namespace {

std::uint32_t rd32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
float rdf(const std::uint8_t* p) { float v; std::memcpy(&v, p, 4); return v; }
std::uint32_t fbits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
float bitsf(std::uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
void put32(std::vector<std::uint8_t>& b, std::uint32_t w) {
    for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(w >> (8 * i)));
}
void put_bytes(std::vector<std::uint8_t>& b, const std::uint8_t* p, std::size_t n, std::size_t avail) {
    const std::size_t have = std::min(n, avail);
    if (have) b.insert(b.end(), p, p + have);
    b.insert(b.end(), n - have, 0);
}

// Frame-0-style affine map: clip lanes (x, y, w) = row4 + p.x row1 + p.y row2 + p.z row3, inverted in double (as sparks.cpp).
struct ClipMap {
    double inv[3][3] = {}, b[3] = {};
    bool ok = false;
    void set(const VuMemory& mem) {
        static const int lane[3] = {0, 1, 3};
        double m[3][3];
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) m[r][c] = double(mem.f(1011 + std::uint32_t(c), lane[r]));
            b[r] = double(mem.f(1014, lane[r]));
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
    void world(const double clip[3], float out[3]) const {
        const double v[3] = {clip[0] - b[0], clip[1] - b[1], clip[2] - b[2]};
        for (int r = 0; r < 3; ++r) out[r] = float(inv[r][0] * v[0] + inv[r][1] * v[1] + inv[r][2] * v[2]);
    }
};

}  // namespace

bool particle_bank_dispatched(const std::uint8_t* head, int bank, bool crash, bool skip_flag, std::uint8_t params_5d) {
    if (params_5d == 0 && skip_flag) return false;
    const std::uint8_t* b = head + (bank == 0 ? 0xE8 : 0x114);
    if (rd32(b + 0x14) == 0 || rd32(b + 0x28) == 0) return false;   // capacity (no bank) / the live mask
    return bank == 0 || crash;
}

bool ParticleVu::load(const std::uint8_t* image, std::size_t bytes, std::string* why) {
    auto p = std::make_shared<Prog>();
    const std::vector<std::uint8_t> v(image, image + bytes);
    if (!p->mp.load_textbin(v, why)) return false;
    if (p->mp.loaded < 0x2F0) { if (why) *why = "particle program too short"; return false; }
    prog_ = std::move(p);
    loaded_ = true;
    return true;
}

bool ParticleVu::run(const std::uint8_t cam[kParticleCamBytes], const ParticleArrayIn arrays[kParticleArrays], std::vector<ParticlePacket>& out,
                     ParticleStats* stats, std::string* why, KeepFn keep, void* user, float time_override) {
    if (!loaded_) { if (why) *why = "program not loaded"; return false; }
    const VuMicroprogram& prog = prog_->mp;
    VifInterpreter vif;
    Vu1 vu(vif.mem);
    std::string err;
    int cur_array = 0, cur_bank = 0;
    bool ok = true;
    ClipMap map;
    // The GS keeps ALPHA_1 / TEST_1 between packets: the program sends them (A+D) only with the first packet of a chunk, the
    // later packets draw with the state still set. A packet without its own A+D inherits the last one (opaque, untested
    // squares otherwise: the texture's alpha was ignored).
    std::uint64_t gs_alpha = 0, gs_test = 0;
    bool gs_have_alpha = false, gs_have_test = false;
    const std::size_t first = out.size();
    const auto on_xgkick = [&](std::uint32_t qa) {
        GifPacket p;
        std::string e2;
        if (!read_gif_packet(vif.mem, qa, p, &e2)) { ok = false; err = "GIF: " + e2; return; }
        if (stats) ++stats->kicks;
        if (p.verts.empty()) return;
        map.set(vif.mem);
        // the program's screen mapping: s = row1022.xyz + vf05.xyz * (clip.x / w, clip.y / w, 1 / w), 12.4 fixed point xy and
        // floor(z) (XYZF2); [C program 0x46D750 pairs 0x134..0x14f (the stored vertex), 0x1cb.. (the class expansion)]
        const float ox = vif.mem.f(1022, 0), oy = vif.mem.f(1022, 1), oz = vif.mem.f(1022, 2);
        const float sx = bitsf(vu.vf[5][0]), sy = bitsf(vu.vf[5][1]), sz = bitsf(vu.vf[5][2]);
        ParticlePacket pk;
        pk.array = cur_array;
        pk.bank = cur_bank;
        for (const auto& ad : p.ad) {
            if (ad.first == 0x42) { pk.alpha = ad.second; pk.has_alpha = true; gs_alpha = ad.second; gs_have_alpha = true; }
            else if (ad.first == 0x47) { pk.test = ad.second; pk.has_test = true; gs_test = ad.second; gs_have_test = true; }
        }
        if (!pk.has_alpha && gs_have_alpha) pk.alpha = gs_alpha;
        if (!pk.has_test && gs_have_test) pk.test = gs_test;
        pk.prim = p.verts[0].prim;
        pk.sprite = (pk.prim & 7) == 6;
        auto unproject = [&](const GifVertex& g, float out[3]) {
            const double zs = double(g.z) + 0.5;
            const double invw = (zs - double(oz)) / double(sz);
            if (!map.ok || !(std::fabs(invw) > 1e-12) || sx == 0 || sy == 0) { if (stats) ++stats->unprojected_bad; out[0] = out[1] = out[2] = 0; return; }
            const double xs = double(std::int32_t(g.x)) / 16.0, ys = double(std::int32_t(g.y)) / 16.0, w = 1.0 / invw;
            const double clip[3] = {(xs - double(ox)) / double(sx) * w, (ys - double(oy)) / double(sy) * w, w};
            map.world(clip, out);
        };
        auto vert = [&](const GifVertex& g, std::uint32_t x, std::uint32_t y, float s, float t, const GifVertex& colour) {
            GifVertex q = g;
            q.x = x;
            q.y = y;
            ParticleVertex v;
            v.s = s;
            v.t = t;
            if (g.q != 0.0f) { v.s = s / g.q; v.t = t / g.q; }   // ST / Q: the GS divides by the vertex's own Q
            v.rgba[0] = colour.r; v.rgba[1] = colour.g; v.rgba[2] = colour.b; v.rgba[3] = colour.a;
            v.adc = g.adc;
            unproject(q, v.pos);
            return v;
        };
        if (pk.sprite) {   // two vertices per sprite (the second one's colour is the flat colour): four corners TL, TR, BL, BR
            pk.v.reserve(p.verts.size() * 2);
            for (std::size_t k = 0; k + 1 < p.verts.size(); k += 2) {
                const GifVertex &a = p.verts[k], &b = p.verts[k + 1];
                pk.v.push_back(vert(a, a.x, a.y, a.s, a.t, b));
                pk.v.push_back(vert(b, b.x, a.y, b.s, a.t, b));
                pk.v.push_back(vert(a, a.x, b.y, a.s, b.t, b));
                pk.v.push_back(vert(b, b.x, b.y, b.s, b.t, b));
            }
        } else {
            pk.v.reserve(p.verts.size());
            for (const GifVertex& g : p.verts) pk.v.push_back(vert(g, g.x, g.y, g.s, g.t, g));
        }
        if (stats) stats->vertices += pk.v.size();
        out.push_back(std::move(pk));
    };
    const auto on_kick = [&](const VuMemory&, std::uint32_t itop, VifInterpreter::KickKind kind, std::uint32_t pc) {
        if (!ok) return;
        if (stats) ++stats->batches;
        const std::uint32_t start = kind == VifInterpreter::kMscnt ? vu.tpc : pc;
        if (!vu.run(prog, start, 0, itop, on_xgkick, &err)) ok = false;
    };
    auto run_stream = [&](const std::vector<std::uint8_t>& s) {
        std::string e;
        if (!vif.run(s.data(), s.size(), on_kick, &e)) { ok = false; err = "VIF: " + e; }
        return ok;
    };
    // EndRender 0x254580: BASE 0, OFFSET 0xB4, MSCALF 0 (the program's set-up pass), then the camera rows 1017..1023
    {
        std::vector<std::uint8_t> s;
        put32(s, 0x03000000);
        put32(s, 0x020000B4);
        put32(s, 0x15000000);
        put32(s, 0x01000101);
        put32(s, 0x6C0703F9);
        const std::uint8_t* cd = cam;
        for (int k = 0; k < 4; ++k) {   // the projection rows (x, y, z, z) at +0xE0
            put32(s, rd32(cd + 0xE0 + 16 * k)); put32(s, rd32(cd + 0xE4 + 16 * k)); put32(s, rd32(cd + 0xE8 + 16 * k)); put32(s, rd32(cd + 0xE8 + 16 * k));
        }
        put32(s, rd32(cd + 0x218)); put32(s, rd32(cd + 0x21C)); put32(s, rd32(cd + 0x200)); put32(s, rd32(cd + 0x204));
        put32(s, rd32(cd + 0x170)); put32(s, rd32(cd + 0x174)); put32(s, rd32(cd + 0x178)); put32(s, rd32(cd + 0x168));
        put32(s, 6); put32(s, 0x10000000); put32(s, 0xE); put32(s, 0);   // the A+D GIF tag row 1023
        if (!run_stream(s)) { if (why) *why = err; return false; }
    }
    const float fov = rdf(cam + 0x208), minnear = rdf(cam + 0x19C);
    for (int a = 0; a < kParticleArrays && ok; ++a) {
        const ParticleArrayIn& ar = arrays[a];
        if (!ar.head) continue;
        const std::uint8_t* h = ar.head;
        for (int b = 0; b < 2 && ok; ++b) {
            const ParticleBankIn& bk = ar.bank[b];
            if (!bk.dispatch || !bk.rec || bk.capacity == 0) continue;
            cur_array = a;
            cur_bank = b;
            const std::uint8_t* bs = h + (b == 0 ? 0xE8 : 0x114);
            const std::uint32_t cap = bk.capacity, n = (cap + 59) / 60;
            // the array's rows 1000..1016 [C DispatchArray 0x254340]
            std::vector<std::uint8_t> st;
            put32(st, 0x01000101);
            put32(st, 0x6C1103E8);
            auto row = [&](const std::uint32_t r[4]) { for (int l = 0; l < 4; ++l) put32(st, r[l]); };
            for (int k = 0; k < 5; ++k) { std::uint32_t r[4]; std::memcpy(r, h + 16 * k, 16); row(r); }
            {   // 1005: the light direction in view space (D_01D9CFD0.. = the camera rotation transposed), w = array +0x5C
                float v[3], L[3][3];
                for (int l = 0; l < 3; ++l) v[l] = rdf(h + 0x50 + 4 * l);
                for (int k = 0; k < 3; ++k) for (int l = 0; l < 3; ++l) L[k][l] = rdf(cam + 0xA0 + 16 * k + 4 * l);
                std::uint32_t r[4];
                for (int k = 0; k < 3; ++k) r[k] = fbits(L[k][0] * v[0] + L[k][1] * v[1] + L[k][2] * v[2]);
                r[3] = rd32(h + 0x5C);
                row(r);
            }
            {   std::uint32_t r[4]; std::memcpy(r, h + 0x60, 16); row(r); }
            {   // 1007: class, the bank's lifetime (+0x24), +0x78, the update time
                std::uint32_t r[4]; std::memcpy(r, h + 0x70, 16);
                r[1] = rd32(bs + 0x24);
                if (time_override > -1e29f) r[3] = fbits(time_override);
                row(r);
            }
            {   // 1008: array +0x80 x the FOV scale, z at least the near clip
                std::uint32_t r[4];
                for (int l = 0; l < 4; ++l) r[l] = fbits(rdf(h + 0x80 + 4 * l) * fov);
                r[2] = fbits(std::max(bitsf(r[2]), minnear));
                row(r);
            }
            for (int k = 0; k < 4; ++k) { std::uint32_t r[4]; std::memcpy(r, h + 0x90 + 16 * k, 16); row(r); }
            { const std::uint32_t r[4] = {0, 0, 6, 0}; row(r); }       // 1013: TEX0_1 (the texture is the port's)
            { std::uint32_t r[4]; std::memcpy(r, h + 0xD0, 16); row(r); }
            { const std::uint32_t r[4] = {0, 0, 0x34, 0}; row(r); }
            { const std::uint32_t r[4] = {0, 0, 0x36, 0}; row(r); }
            const std::uint8_t* rec16 = bk.rec;
            const std::uint8_t* rec8 = rec16 + std::size_t(cap) * 16;
            const std::uint8_t* rec4 = rec8 + std::size_t(cap) * 8;
            const std::size_t total = particle_bank_bytes(cap);
            for (std::uint32_t i = 0; i < n && ok; ++i) {
                const std::uint32_t cnt = i + 1 < n ? 60 : cap - (n - 1) * 60;
                std::vector<std::uint8_t> s;
                if (i == 0) s = st;   // chunk 0 and the rows go before the first MSCNT
                put32(s, 0x01000103);
                put32(s, 0x6C008000u | (cnt << 16));
                {
                    std::vector<std::uint8_t> r(std::size_t(cnt) * 16);
                    std::memcpy(r.data(), rec16 + std::size_t(i) * 0x3C0, r.size());
                    if (keep)
                        for (std::uint32_t k = 0; k < cnt; ++k) {
                            std::uint32_t tw;
                            std::memcpy(&tw, r.data() + 16 * k + 12, 4);
                            if (bitsf(tw) > -9000.0f && !keep(user, a, b, int(i * 60 + k))) { const std::uint32_t dead = fbits(-9999.0f); std::memcpy(r.data() + 16 * k + 12, &dead, 4); }
                        }
                    s.insert(s.end(), r.begin(), r.end());
                }
                const std::uint32_t c1 = (cnt + 1) & ~1u, c2 = (cnt + 3) & ~3u;
                put32(s, 0x6D008001u | (c1 << 16));
                put_bytes(s, rec8 + std::size_t(i) * 0x1E0, std::size_t(c1) * 8, total - std::size_t(cap) * 16 - std::size_t(i) * 0x1E0);
                put32(s, 0x6E00C002u | (c2 << 16));
                put_bytes(s, rec4 + std::size_t(i) * 0xF0, std::size_t(c2) * 4, total - std::size_t(cap) * 24 - std::size_t(i) * 0xF0);
                put32(s, 0x04000000u | cnt | (i + 1 == n ? 0x100u : 0u));   // ITOP
                put32(s, 0x17000000);                                      // MSCNT
                if (!run_stream(s)) break;
            }
        }
    }
    if (!ok) {
        out.resize(first);
        if (why) *why = err;
        return false;
    }
    if (stats) stats->pairs += vu.stats.pairs;
    return true;
}

}  // namespace b3asset
