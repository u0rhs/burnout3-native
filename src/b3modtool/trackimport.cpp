// Track import: an exported (and edited) route model back into STATIC.DAT / STREAMED.DAT (include/b3modtool/export3d.h
// "track import"; the layout docs/track-format.md, the packets docs/modding/file-formats/track.md "How the packets are
// written").
//
//   track_mesh_read / track_encode_mesh   a mesh's chains with every stored lane, and the writer (the decode's inverse)
//   track_strip_mesh                      triangles -> strips -> VU batches, for meshes whose triangles changed
//   track_files_read                      every place a mesh's chains are named from, the streamed blocks' extents
//   track_import_meshes                   the new meshes over the files: packets in place, or moved when they grow
//   track_model_read                      a .gltf / .glb (the export, or a modelling tool's save of it) -> meshes
//   track_import                          the two together, checked against the export's sidecar
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#include "b3asset/gs_memory.h"
#include "b3asset/raster.h"
#include "b3asset/track.h"
#include "b3asset/vif.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"
#include "b3modtool/meshimport.h"

namespace b3modtool {

namespace {

using b3asset::Bytes;

bool fail(std::string* why, const std::string& m) { if (why) *why = m; return false; }

std::string hex32(std::uint32_t v) {
    char t[16];
    std::snprintf(t, sizeof t, "0x%08X", v);
    return t;
}

std::uint32_t rd32(const Buf& b, std::size_t at) { return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24; }
std::int32_t rds32(const Buf& b, std::size_t at) { return std::int32_t(rd32(b, at)); }
std::int16_t rds16(const Buf& b, std::size_t at) { return std::int16_t(std::uint16_t(b[at] | b[at + 1] << 8)); }
void wr32(Buf& b, std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) b[at + std::size_t(i)] = std::uint8_t(v >> (8 * i)); }
void wr16(Buf& b, std::size_t at, std::uint16_t v) { b[at] = std::uint8_t(v); b[at + 1] = std::uint8_t(v >> 8); }
float rdf(const Buf& b, std::size_t at) { const std::uint32_t u = rd32(b, at); float f; std::memcpy(&f, &u, 4); return f; }
void wrf(Buf& b, std::size_t at, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); wr32(b, at, u); }
std::uint32_t fbits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
bool has(const Buf& b, std::size_t at, std::size_t n) { return at <= b.size() && n <= b.size() - at; }
std::size_t chain_end(const Buf& f, std::size_t at) { return at + 16 + 16 * std::size_t(rd32(f, at) & 0xFFFF); }

constexpr std::uint32_t kStmod0 = 0x05000000, kStcycl31 = 0x01000103, kStcycl11 = 0x01000101, kItop = 0x04000000, kMscnt = 0x17000000;
constexpr std::uint32_t kV4_32 = 0xC, kV2_16 = 0x5, kV2_32 = 0x4, kV4_8 = 0xE, kV3_8 = 0xA;

// ---- vertex keys ----
// every stored field but w (and, for `normals` false, the normal's direction): one vertex for the strips and the sets
constexpr std::size_t kKey = 12 + 8 + 4 + 4 + 16;
struct VKey {
    std::uint8_t k[kKey];
    bool operator==(const VKey& o) const { return std::memcmp(k, o.k, sizeof k) == 0; }
};
struct VKeyHash {
    std::size_t operator()(const VKey& v) const { return std::size_t(b3mod::fnv1a64(v.k, sizeof v.k)); }
};
VKey vkey(const TrackVertex& v, bool normals = true) {
    VKey r;
    std::memcpy(r.k, v.pos, 12);
    std::memcpy(r.k + 12, v.uv, 8);
    std::memcpy(r.k + 20, v.col, 4);
    std::memcpy(r.k + 24, v.nrm, 4);
    if (!normals) std::memset(r.k + 24, 0, 3);
    std::memcpy(r.k + 28, v.light, 16);
    return r;
}
// the fields a layout does not store, zeroed (track_mesh_read gives them as 0)
void layout_mask(int layout, TrackVertex& v) {
    if (!(layout & (kTrackUv16 | kTrackUv32))) v.uv[0] = v.uv[1] = 0;
    if (!(layout & (kTrackCol4 | kTrackCol3))) std::memset(v.col, 0, 4);
    if (layout & kTrackCol3) v.col[3] = 0;
    if (!(layout & (kTrackNrm204 | kTrackNrm1))) std::memset(v.nrm, 0, 4);
    if (!(layout & kTrackLight)) std::memset(v.light, 0, 16);
}

// two stored normals that point the same way (within 2.5 degrees; a zero one counts as +Y, as the export writes it)
bool normal_close(const std::int8_t* a, const std::int8_t* b) {
    double x[3], y[3], lx = 0, ly = 0, d = 0;
    for (int c = 0; c < 3; ++c) {
        x[c] = a[c];
        y[c] = b[c];
        lx += x[c] * x[c];
        ly += y[c] * y[c];
    }
    if (lx < 1e-12) { x[0] = x[2] = 0; x[1] = 1; lx = 1; }
    if (ly < 1e-12) { y[0] = y[2] = 0; y[1] = 1; ly = 1; }
    for (int c = 0; c < 3; ++c) d += x[c] * y[c];
    return d / std::sqrt(lx * ly) > 0.999;
}

// ---- the packet writer ----
void unpack(VifStream& s, std::uint32_t cycle, std::uint32_t fmt, std::uint32_t num, std::uint32_t addr, bool usn) {
    s.align_cmd(2);
    s.put(kStmod0);
    s.put(cycle);
    s.put((0x60u | fmt) << 24 | (num & 0xFF) << 16 | 0x8000u | (usn ? 0x4000u : 0u) | addr);
}

void encode_batch(VifStream& s, const TrackVertex* v, std::uint32_t n, int layout, bool cont) {
    const std::uint32_t n4 = (n + 3) & ~3u;
    // a transposed block: element 4g + c holds component c of vertices 4g .. 4g + 3 in its four lanes
    auto lane8 = [&](std::uint32_t e, int j) -> std::uint8_t {
        const std::uint32_t vi = (e & ~3u) + std::uint32_t(j);
        return vi < n ? std::uint8_t(v[vi].nrm[e & 3]) : 0;
    };
    unpack(s, kStcycl31, kV4_32, n, 0, false);
    for (std::uint32_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) s.put(fbits(v[i].pos[c]));
        s.put(v[i].w);
    }
    if (layout & kTrackUv16) {
        unpack(s, kStcycl31, kV2_16, n, 1, false);
        for (std::uint32_t i = 0; i < n; ++i) s.put((std::uint32_t(v[i].uv[0]) & 0xFFFF) | std::uint32_t(v[i].uv[1]) << 16);
    } else if (layout & kTrackUv32) {
        unpack(s, kStcycl31, kV2_32, n, 1, false);
        for (std::uint32_t i = 0; i < n; ++i) { s.put(std::uint32_t(v[i].uv[0])); s.put(std::uint32_t(v[i].uv[1])); }
    } else if (layout & kTrackNrm1) {
        unpack(s, kStcycl31, kV4_8, n4, 1, false);
        for (std::uint32_t e = 0; e < n4; ++e)
            for (int j = 0; j < 4; ++j) s.b.push_back(lane8(e, j));
    }
    if (layout & kTrackCol4) {
        unpack(s, kStcycl31, kV4_8, n, 2, true);
        for (std::uint32_t i = 0; i < n; ++i) s.b.insert(s.b.end(), v[i].col, v[i].col + 4);
    } else if (layout & kTrackCol3) {
        unpack(s, kStcycl31, kV3_8, n, 2, true);
        for (std::uint32_t i = 0; i < n; ++i) s.b.insert(s.b.end(), v[i].col, v[i].col + 3);
        s.pad4();
    } else if (layout & kTrackLight) {
        unpack(s, kStcycl31, kV4_32, n4, 2, false);
        for (std::uint32_t e = 0; e < n4; ++e)
            for (int j = 0; j < 4; ++j) {
                const std::uint32_t vi = (e & ~3u) + std::uint32_t(j);
                s.put(vi < n ? fbits(v[vi].light[e & 3]) : 0u);
            }
    }
    if (layout & kTrackNrm204) {
        unpack(s, kStcycl11, kV4_8, n4, 204, false);
        for (std::uint32_t e = 0; e < n4; ++e)
            for (int j = 0; j < 4; ++j) s.b.push_back(lane8(e, j));
    }
    s.pad_qw();   // ITOP on a quadword, as the disc's packets have it
    s.put(kItop | n);
    if (cont) s.put(kMscnt);
}

}  // namespace

int track_material_layout(int type, bool textured) {
    switch (type) {
    case 0: case 6: return (textured ? kTrackUv16 : 0) | kTrackCol4;
    case 1: return (textured ? kTrackUv16 : 0) | kTrackCol3 | kTrackNrm204;
    case 2: return kTrackNrm1 | kTrackCol3;
    case 3: return -1;   // line strips: not exported
    case 4: return kTrackUv32;
    case 5: return kTrackUv32 | kTrackLight;
    default: return -1;
    }
}

int track_max_batch(int layout) { return layout & kTrackNrm204 ? 68 : layout & kTrackUv32 ? 76 : 80; }

bool track_mesh_read(const Buf& file, std::size_t c1, std::size_t c2, TrackMesh& out, std::string* why) {
    out = TrackMesh{};
    out.layout = -1;
    b3asset::VifInterpreter vif;
    std::string err;
    int chain_batches = 0;
    bool bad = false;
    auto kick = [&](const b3asset::VuMemory& vu, std::uint32_t n, b3asset::VifInterpreter::KickKind, std::uint32_t) {
        ++chain_batches;
        if (bad) return;
        int layout = 0;
        for (const auto& [addr, fmt] : vif.batch_unpacks) {
            if (addr == 0 && std::uint32_t(fmt) == kV4_32) continue;
            if (addr == 1 && std::uint32_t(fmt) == kV2_16) layout |= kTrackUv16;
            else if (addr == 1 && std::uint32_t(fmt) == kV2_32) layout |= kTrackUv32;
            else if (addr == 1 && std::uint32_t(fmt) == kV4_8) layout |= kTrackNrm1;
            else if (addr == 2 && std::uint32_t(fmt) == kV4_8) layout |= kTrackCol4;
            else if (addr == 2 && std::uint32_t(fmt) == kV3_8) layout |= kTrackCol3;
            else if (addr == 2 && std::uint32_t(fmt) == kV4_32) layout |= kTrackLight;
            else if (addr == 204 && std::uint32_t(fmt) == kV4_8) layout |= kTrackNrm204;
            else { err = "an unpack the writer does not know (format " + std::to_string(fmt) + " at " + std::to_string(addr) + ")"; bad = true; return; }
        }
        if (!vif.batch_unpacks.count(0) || !n || n > 80) { err = "a batch without positions"; bad = true; return; }
        if (out.layout >= 0 && layout != out.layout) { err = "batches of different layouts"; bad = true; return; }
        out.layout = layout;
        const std::uint32_t base = std::uint32_t(out.v.size());
        std::vector<bool> adc(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            TrackVertex v;
            const std::uint32_t* p = vu.qw(3 * i);
            for (int c = 0; c < 3; ++c) std::memcpy(&v.pos[c], &p[c], 4);
            v.w = p[3];
            adc[i] = v.w & 0x8000;
            const std::uint32_t g = i / 4, j = i % 4;
            if (layout & (kTrackUv16 | kTrackUv32)) { v.uv[0] = std::int32_t(vu.qw(3 * i + 1)[0]); v.uv[1] = std::int32_t(vu.qw(3 * i + 1)[1]); }
            if (layout & kTrackNrm1)
                for (int c = 0; c < 4; ++c) v.nrm[c] = std::int8_t(vu.qw(3 * (4 * g + std::uint32_t(c)) + 1)[j]);
            if (layout & (kTrackCol4 | kTrackCol3))
                for (int c = 0; c < ((layout & kTrackCol4) ? 4 : 3); ++c) v.col[c] = std::uint8_t(vu.qw(3 * i + 2)[c]);
            if (layout & kTrackLight)
                for (int c = 0; c < 4; ++c) std::memcpy(&v.light[c], &vu.qw(3 * (4 * g + std::uint32_t(c)) + 2)[j], 4);
            if (layout & kTrackNrm204)
                for (int c = 0; c < 4; ++c) v.nrm[c] = std::int8_t(vu.qw(204 + 4 * g + std::uint32_t(c))[j]);
            out.v.push_back(v);
        }
        out.batches.push_back(n);
        b3asset::strip_to_triangles(base, adc, out.tri);
    };
    for (int part = 0; part < 2; ++part) {
        const std::size_t at = part ? c2 : c1;
        if (!at) continue;
        if (!has(file, at, 16) || ((rd32(file, at) >> 28) & 7) != 6) return fail(why, "a mesh chain that is not a DMA RET packet at " + hex32(std::uint32_t(at)));
        const std::size_t qwc = rd32(file, at) & 0xFFFF;
        if (!has(file, at, 16 + 16 * qwc)) return fail(why, "a mesh chain past the end of the file");
        chain_batches = 0;
        if (!vif.run(file.data() + at + 8, 8 + qwc * 16, kick, &err, 8)) return fail(why, "mesh VIF: " + err);
        vif.flush(kick);
        if (bad) return fail(why, err);
        if (part == 0 && chain_batches != 1) return fail(why, "chain 1 holds " + std::to_string(chain_batches) + " batches (the files' have one)");
        if (part == 1 && chain_batches < 1) return fail(why, "an empty chain 2");
    }
    if (out.v.empty()) return fail(why, "a mesh without vertices");
    return true;
}

bool track_encode_mesh(const TrackMesh& m, Buf& first, Buf& cont, std::string* why) {
    first.clear();
    cont.clear();
    const int maxb = track_max_batch(m.layout);
    std::size_t total = 0;
    for (std::uint32_t n : m.batches) {
        if (n == 0 || n > std::uint32_t(maxb)) return fail(why, "a batch of " + std::to_string(n) + " vertices (1 .. " + std::to_string(maxb) + ")");
        total += n;
    }
    if (m.batches.empty() || total != m.v.size()) return fail(why, "the batches do not cover the vertices");
    VifStream a;
    a.put(0);
    a.put(0);   // the DMA tag, filled in by finish
    encode_batch(a, m.v.data(), m.batches[0], m.layout, false);
    a.finish();
    first = std::move(a.b);
    if (m.batches.size() > 1) {
        VifStream b;
        b.put(0);
        b.put(0);
        std::size_t at = m.batches[0];
        for (std::size_t k = 1; k < m.batches.size(); ++k) {
            encode_batch(b, m.v.data() + at, m.batches[k], m.layout, true);
            at += m.batches[k];
        }
        b.finish();
        cont = std::move(b.b);
    }
    if (first.size() / 16 - 1 > 0xFFFF || cont.size() / 16 > 0x10000) return fail(why, "a packet over 65535 quadwords");
    return true;
}

bool track_strip_mesh(const std::vector<TrackVertex>& verts, const std::vector<std::uint32_t>& tri_in, int layout, TrackMesh& out, std::string* why) {
    out = TrackMesh{};
    out.layout = layout;
    if (tri_in.size() % 3) return fail(why, "the index count is not a multiple of 3");
    // unique vertices (the same stored fields: one vertex), so strips run across the model's split vertices
    std::unordered_map<VKey, std::uint32_t, VKeyHash> uid;
    std::vector<TrackVertex> U;
    std::vector<std::uint32_t> remap(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        TrackVertex x = verts[i];
        layout_mask(layout, x);
        x.w = 0;
        auto it = uid.emplace(vkey(x), std::uint32_t(U.size()));
        if (it.second) U.push_back(x);
        remap[i] = it.first->second;
    }
    std::vector<std::array<std::uint32_t, 3>> T;
    for (std::size_t t = 0; t < tri_in.size(); t += 3) {
        std::array<std::uint32_t, 3> x{};
        for (int k = 0; k < 3; ++k) {
            if (tri_in[t + std::size_t(k)] >= verts.size()) return fail(why, "an index past the vertices");
            x[std::size_t(k)] = remap[tri_in[t + std::size_t(k)]];
        }
        auto same = [&](std::uint32_t a, std::uint32_t b) { return std::memcmp(U[a].pos, U[b].pos, 12) == 0; };
        if (same(x[0], x[1]) || same(x[1], x[2]) || same(x[0], x[2])) continue;
        T.push_back(x);
    }
    std::vector<std::pair<std::uint32_t, bool>> seq;
    build_strips(T, track_max_batch(layout), seq, out.batches);
    for (const auto& [vi, adc] : seq) {
        TrackVertex x = U[vi];
        x.w = adc ? 0x8000u : 0u;
        out.v.push_back(x);
    }
    if (out.v.empty()) {   // no triangles: one batch that draws nothing (every vertex ADC)
        TrackVertex z = verts.empty() ? TrackVertex{} : verts[0];
        layout_mask(layout, z);
        z.w = 0x8000u;
        out.v.assign(3, z);
        out.batches = {3};
    }
    std::size_t base = 0;
    for (std::uint32_t n : out.batches) {
        std::vector<bool> adc(n);
        for (std::uint32_t i = 0; i < n; ++i) adc[i] = out.v[base + i].w & 0x8000u;
        b3asset::strip_to_triangles(std::uint32_t(base), adc, out.tri);
        base += n;
    }
    return true;
}

std::uint64_t track_mesh_hash(const std::vector<TrackVertex>& v, const std::vector<std::uint32_t>& tri, int layout, bool normals) {
    std::vector<std::array<float, 3>> pos;
    std::vector<std::uint8_t> extra;
    for (TrackVertex x : v) {
        layout_mask(layout, x);
        pos.push_back({x.pos[0], x.pos[1], x.pos[2]});
        const VKey k = vkey(x, normals);
        extra.insert(extra.end(), k.k + 12, k.k + kKey);
    }
    return triangle_set_hash(pos, extra, kKey - 12, tri);
}

bool track_files_read(const Buf& st, const Buf& sm, TrackFiles& out, std::string* why) {
    out = TrackFiles{};
    if (st.size() < 0x60 || rd32(st, 0) != 0x27 || rd32(st, 4) != st.size()) return fail(why, "not a version 0x27 STATIC.DAT");
    {
        b3asset::GsMemory gs;
        b3asset::Model m;
        b3asset::TrackOptions to;
        to.streamed = to.backdrops = to.instances = to.props = false;
        if (!b3asset::read_track(Bytes{st.data(), st.size()}, Bytes{}, to, gs, m, why, &out.materials)) return false;
    }
    const int ntex = rds16(st, 0x16);
    for (int k = 0; k < ntex; ++k) out.textures.push_back(rd32(st, rd32(st, 0x18) + 4 * std::size_t(k)));
    auto chains = [&](const Buf& f, std::size_t mesh, TrackSite& s) {
        if (!has(f, mesh, 0x40)) return false;
        const std::uint32_t a = rd32(f, mesh + 0x0C), b = rd32(f, mesh + 0x1C);
        if (!a) return false;
        s.ref_at = mesh;
        s.chain1 = mesh + a;
        s.chain2 = b ? mesh + b : 0;
        return has(f, s.chain1, 16) && (!s.chain2 || has(f, s.chain2, 16));
    };
    const int nunits = rds16(st, 0x54);
    const std::size_t utab = rd32(st, 0x58);
    const int ninst = rds16(st, 0x34), nprop = rds16(st, 0x36);
    if (!sm.empty()) {
        std::size_t next = 0;
        for (int k = 0; k < nunits; ++k) {
            const std::size_t e = utab + 16 * std::size_t(k);
            const std::size_t hi = rd32(st, e), lo = rd32(st, e + 4), hs = rd32(st, e + 8), ls = rd32(st, e + 12);
            if (hi != next || lo != hi + hs || !has(sm, hi, hs) || !has(sm, lo, ls) || hs < 0x11A + 0xA9 * 17 || ls < 0xA0)
                return fail(why, "STREAMED.DAT's blocks are not back to back as the unit table says (unit " + std::to_string(k) + ")");
            next = lo + ls;
            if (rd32(sm, hi + 4) != std::uint32_t(k) || rd32(sm, lo + 4) != std::uint32_t(k) || rd32(sm, hi + 8) != hs || rd32(sm, lo + 8) != ls)
                return fail(why, "unit " + std::to_string(k) + "'s blocks do not carry their index and size");
            // the render table's own row: mesh -> material slot
            const int nall = sm[hi + 0x61];
            std::size_t row = 0;
            for (int r = 0; r < 17; ++r)
                if (std::int8_t(sm[hi + 0x11A + 0xA9 * std::size_t(r)]) == 0) { row = hi + 0x11A + 0xA9 * std::size_t(r); break; }
            if (!row) return fail(why, "unit " + std::to_string(k) + "'s render table has no own row");
            for (int part = 0; part < 2; ++part) {
                const std::size_t blk = part ? lo : hi, bsz = part ? ls : hs;
                TrackFiles::Block B;
                B.at = blk;
                B.size = bsz;
                std::size_t used = blk + 0x60;
                const int nm = rds16(sm, blk + 0x50);
                const std::size_t arr = std::size_t(std::int64_t(blk) + 0x50 + rds32(sm, blk + 0x54));
                used = std::max(used, arr + 0x40 * std::size_t(std::max(nm, 0)));
                std::vector<int> mat_of(std::size_t(std::max(nm, 0)), -1);
                for (int s = 0; s < nall && s < 83; ++s) {
                    const int idx = std::int8_t(sm[row + 3 + (part ? 83 : 0) + std::size_t(s)]);
                    if (idx >= 0 && idx < nm) mat_of[std::size_t(idx)] = rds16(sm, hi + 0x74 + 2 * std::size_t(s));
                }
                for (int i = 0; i < nm; ++i) {
                    TrackSite s;
                    s.kind = part ? TrackSite::kLod : TrackSite::kHigh;
                    s.unit = k;
                    s.material = mat_of[std::size_t(i)];
                    if (!chains(sm, arr + 0x40 * std::size_t(i), s)) continue;
                    if (s.chain1 < blk || chain_end(sm, s.chain1) > blk + bsz || (s.chain2 && (s.chain2 < blk || chain_end(sm, s.chain2) > blk + bsz)))
                        return fail(why, "a mesh chain outside its block (unit " + std::to_string(k) + ")");
                    used = std::max({used, chain_end(sm, s.chain1), s.chain2 ? chain_end(sm, s.chain2) : 0});
                    out.sites.push_back(s);
                }
                if (part) {   // the instance placements (+0x98 / +0x9C, relative to +0x98)
                    const std::size_t b2 = blk + 0x98, counts = b2 + rd32(sm, blk + 0x98), tab = b2 + rd32(sm, blk + 0x9C);
                    used = std::max({used, counts + std::size_t(std::max(ninst, 0)), tab + 4 * std::size_t(std::max(ninst, 0))});
                    for (int t = 0; t < ninst; ++t) {
                        const int c = std::int8_t(sm[counts + std::size_t(t)]);
                        const std::uint32_t e2 = rd32(sm, tab + 4 * std::size_t(t));
                        if (c > 0 && e2) used = std::max(used, b2 + e2 + 0x40 * std::size_t(c));
                    }
                }
                // whatever else the block holds ends before its last non-zero byte
                std::size_t nz = blk + bsz;
                while (nz > blk && !sm[nz - 1]) --nz;
                B.used = std::min(blk + bsz, (std::max(used, nz) + 63) & ~std::size_t(63));
                out.blocks.push_back(B);
            }
        }
        if (next != sm.size()) return fail(why, "STREAMED.DAT has bytes after the last unit");
    }
    for (int w = 0; w < 4; ++w) {   // the backdrop tables
        const std::size_t base = rd32(st, 0x24 + 4 * std::size_t(w));
        const int ng = rds16(st, 0x1C + 2 * std::size_t(w));
        if (!base) continue;
        for (int g = 0; g < ng; ++g) {
            const std::size_t go = base + 8 * std::size_t(g);
            const int n = rds16(st, go);
            const std::size_t objs = std::size_t(std::int64_t(go) + rds32(st, go + 4));
            for (int j = 0; j < n; ++j) {
                const std::size_t ob = objs + 0x50 * std::size_t(j);
                const int nm = rds16(st, ob + 0x40);
                if (nm <= 0) continue;
                const std::size_t meshes = std::size_t(std::int64_t(ob) + 0x40 + rds32(st, ob + 0x44));
                const std::int32_t mo = rds32(st, ob + 0x48);
                for (int k = 0; k < nm; ++k) {
                    TrackSite s;
                    s.kind = TrackSite::kBackdrop;
                    s.in_static = true;
                    s.table = w;
                    s.group = g;
                    s.owner_at = ob;
                    if (mo) { s.mat_at = std::size_t(std::int64_t(go) + mo) + 2 * std::size_t(k); s.material = rds16(st, s.mat_at); }
                    if (chains(st, meshes + 0x40 * std::size_t(k), s)) out.sites.push_back(s);
                }
            }
        }
    }
    for (int tb = 0; tb < 2; ++tb) {   // instance and prop types: the main model (+0x20, material +0x38)
        const std::size_t t0 = rd32(st, tb ? 0x3C : 0x38);
        const int n = tb ? nprop : ninst;
        for (int t = 0; t0 && t < n; ++t) {
            const std::size_t nb = t0 + 0x50 * std::size_t(t), pair = nb + 0x20;
            const std::uint32_t a = rd32(st, pair), b = rd32(st, pair + 4);
            if (!a) continue;
            TrackSite s;
            s.kind = tb ? TrackSite::kProp : TrackSite::kInstance;
            s.in_static = true;
            s.ref_at = pair;
            s.chain1 = pair + a;
            s.chain2 = b ? pair + b : 0;
            s.owner_at = nb;
            s.mat_at = nb + 0x38;
            s.material = rds16(st, s.mat_at);
            s.type = t;
            out.sites.push_back(s);
        }
    }
    return true;
}


namespace {

// a mesh's box (CB3TrackMesh +0x00 / +0x10 / +0x20: three half axes in xyz, +0x30 the centre; the w lanes hold the chain
// offsets and constants) grown to hold `v` when a vertex lies outside it: the same axes' directions, longer, the centre
// moved. Returns whether it changed.
bool grow_mesh_box(Buf& f, std::size_t at, const std::vector<TrackVertex>& v) {
    double A[3][3], L[3], d[3][3], c[3];
    for (int i = 0; i < 3; ++i) {
        L[i] = 0;
        for (int k = 0; k < 3; ++k) { A[i][k] = rdf(f, at + 16 * std::size_t(i) + 4 * std::size_t(k)); L[i] += A[i][k] * A[i][k]; }
        L[i] = std::sqrt(L[i]);
    }
    for (int k = 0; k < 3; ++k) c[k] = rdf(f, at + 0x30 + 4 * std::size_t(k));
    // the directions: the axes' own, the degenerate ones (near zero length) made perpendicular to the others
    int good = 0;
    for (int i = 0; i < 3; ++i) {
        if (L[i] > 1e-6) for (int k = 0; k < 3; ++k) d[i][k] = A[i][k] / L[i];
        good += L[i] > 1e-6;
    }
    bool ortho = good > 0;
    for (int i = 0; i < 3 && ortho; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (L[i] > 1e-6 && L[j] > 1e-6 && std::fabs(d[i][0] * d[j][0] + d[i][1] * d[j][1] + d[i][2] * d[j][2]) > 1e-3) ortho = false;
    if (!ortho) {   // not a box with perpendicular axes: the world axes around the old box's corners
        double lo[3], hi[3];
        for (int k = 0; k < 3; ++k) lo[k] = hi[k] = c[k];
        for (int s = 0; s < 8; ++s)
            for (int k = 0; k < 3; ++k) {
                double x = c[k];
                for (int i = 0; i < 3; ++i) x += ((s >> i) & 1 ? 1 : -1) * A[i][k];
                lo[k] = std::min(lo[k], x);
                hi[k] = std::max(hi[k], x);
            }
        for (int i = 0; i < 3; ++i) { for (int k = 0; k < 3; ++k) d[i][k] = i == k; L[i] = (hi[i] - lo[i]) / 2; c[i] = (hi[i] + lo[i]) / 2; }
    } else {
        for (int i = 0; i < 3; ++i) {
            if (L[i] > 1e-6) continue;
            // perpendicular to the good ones: a cross product, or a world axis made perpendicular (Gram-Schmidt)
            int a = -1, b = -1;
            for (int j = 0; j < 3; ++j) if (j != i && L[j] > 1e-6) (a < 0 ? a : b) = j;
            double x[3];
            if (a >= 0 && b >= 0) {
                x[0] = d[a][1] * d[b][2] - d[a][2] * d[b][1];
                x[1] = d[a][2] * d[b][0] - d[a][0] * d[b][2];
                x[2] = d[a][0] * d[b][1] - d[a][1] * d[b][0];
            } else {
                for (int w = 0; w < 3; ++w) {
                    for (int k = 0; k < 3; ++k) x[k] = w == k;
                    for (int j = 0; j < 3; ++j) {
                        if (j == i || !(L[j] > 1e-6 || j < i)) continue;
                        const double p = x[0] * d[j][0] + x[1] * d[j][1] + x[2] * d[j][2];
                        for (int k = 0; k < 3; ++k) x[k] -= p * d[j][k];
                    }
                    if (x[0] * x[0] + x[1] * x[1] + x[2] * x[2] > 0.1) break;
                }
            }
            const double l = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
            for (int k = 0; k < 3; ++k) d[i][k] = x[k] / l;
            L[i] = 0;
        }
    }
    double lo[3], hi[3];
    for (int i = 0; i < 3; ++i) { lo[i] = -L[i]; hi[i] = L[i]; }
    bool out = !ortho;
    for (const TrackVertex& x : v)
        for (int i = 0; i < 3; ++i) {
            const double p = (x.pos[0] - c[0]) * d[i][0] + (x.pos[1] - c[1]) * d[i][1] + (x.pos[2] - c[2]) * d[i][2];
            if (p < lo[i] - 1e-3 || p > hi[i] + 1e-3) out = true;
            lo[i] = std::min(lo[i], p);
            hi[i] = std::max(hi[i], p);
        }
    if (!out) return false;
    double nc[3] = {c[0], c[1], c[2]};
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) nc[k] += d[i][k] * (lo[i] + hi[i]) / 2;
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) wrf(f, at + 16 * std::size_t(i) + 4 * std::size_t(k), float(d[i][k] * (hi[i] - lo[i]) / 2));
    for (int k = 0; k < 3; ++k) wrf(f, at + 0x30 + 4 * std::size_t(k), float(nc[k]));
    return true;
}

// an instance / prop type's local box (+0x00 the largest x y z, +0x10 the smallest) grown to hold `v`
bool grow_aabb(Buf& f, std::size_t at, const std::vector<TrackVertex>& v) {
    bool changed = false;
    for (const TrackVertex& x : v)
        for (int k = 0; k < 3; ++k) {
            if (x.pos[k] > rdf(f, at + 4 * std::size_t(k))) { wrf(f, at + 4 * std::size_t(k), x.pos[k]); changed = true; }
            if (x.pos[k] < rdf(f, at + 0x10 + 4 * std::size_t(k))) { wrf(f, at + 0x10 + 4 * std::size_t(k), x.pos[k]); changed = true; }
        }
    return changed;
}

const char* site_kind(const TrackSite& s) {
    switch (s.kind) {
    case TrackSite::kHigh: return "section";
    case TrackSite::kLod: return "section LOD";
    case TrackSite::kBackdrop: return s.table == 0 ? "backdrop" : s.table == 1 ? "sign" : "water";
    case TrackSite::kInstance: return "instance model";
    default: return "prop model";
    }
}

std::string site_name(const TrackSite& s) {
    char t[96];
    if (s.kind == TrackSite::kHigh || s.kind == TrackSite::kLod) std::snprintf(t, sizeof t, "%s %03d (chain 0x%zX, material %d)", site_kind(s), s.unit, s.chain1, s.material);
    else if (s.kind == TrackSite::kBackdrop) std::snprintf(t, sizeof t, "%s mesh (STATIC.DAT chain 0x%zX, material %d)", site_kind(s), s.chain1, s.material);
    else std::snprintf(t, sizeof t, "%s %d (material %d)", site_kind(s), s.type, s.material);
    return t;
}

// one region the packets live in: a STREAMED.DAT block, or STATIC.DAT
struct Region {
    Buf b;
    std::size_t base = 0;        // its place in the original file
    std::size_t used = 0;        // the end of what it holds (relative)
    std::size_t limit = 0;       // the most it may hold
    bool is_static = false;
    std::vector<std::pair<std::size_t, std::size_t>> holes;   // freed packet space (relative)
};

}  // namespace

bool track_import_meshes(const Buf& st, const Buf& sm, const std::vector<TrackImportMesh>& meshes, const std::vector<TrackTextureEdit>& swaps,
                         const std::map<int, b3asset::Image>& pictures, const TrackImportOptions& opt, Buf& st_out, Buf& sm_out,
                         TrackImportReport* rep_out, std::string* why) {
    TrackImportReport rep;
    TrackFiles tf;
    if (!track_files_read(st, sm, tf, why)) return false;
    rep.static_before = st.size();
    rep.streamed_before = sm.size();
    std::map<std::pair<bool, std::size_t>, std::vector<std::size_t>> by_key;   // (in STATIC, chain 1) -> sites
    std::map<std::pair<bool, std::size_t>, std::size_t> refs;                  // a chain -> the sites that name it
    for (std::size_t i = 0; i < tf.sites.size(); ++i) {
        const TrackSite& s = tf.sites[i];
        by_key[{s.in_static, s.chain1}].push_back(i);
        ++refs[{s.in_static, s.chain1}];
        if (s.chain2) ++refs[{s.in_static, s.chain2}];
    }
    // ---- the new streams, one per chain pair ----
    struct Job {
        std::pair<bool, std::size_t> key;
        std::vector<std::size_t> sites;
        TrackMesh om, nm;
        Buf pk[2];
        bool changed = false;
        std::vector<std::pair<std::size_t, int>> material;   // (site, new material)
        std::string name;
    };
    std::map<std::pair<bool, std::size_t>, Job> jobs;
    for (const TrackImportMesh& im : meshes) {
        const auto key = std::make_pair(im.in_static, im.chain1);
        auto it = by_key.find(key);
        const std::string nm = im.name.empty() ? std::string("a mesh") : im.name;
        if (it == by_key.end() || tf.sites[it->second.front()].chain2 != im.chain2)
            return fail(why, nm + ": the route's files have no mesh at " + std::string(im.in_static ? "STATIC.DAT" : "STREAMED.DAT") + " " +
                                 hex32(std::uint32_t(im.chain1)) + ": the import changes the meshes the route has, it cannot add any");
        const TrackSite& s0 = tf.sites[it->second.front()];
        const bool world = s0.kind != TrackSite::kInstance && s0.kind != TrackSite::kProp;
        const double lim = world ? 100000.0 : 1000.0;
        for (const TrackVertex& v : im.v)
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(v.pos[c]) || std::fabs(v.pos[c]) > lim)
                    return fail(why, nm + ": a vertex position outside +-" + std::string(world ? "100 km" : "1 km (an instance model is in its own space)") + " (or not a number)");
        for (std::uint32_t x : im.tri)
            if (x >= im.v.size()) return fail(why, nm + ": an index past its vertices");
        if (im.tri.size() % 3) return fail(why, nm + ": the index count is not a multiple of 3");
        Job& j = jobs[key];
        if (j.sites.empty()) {
            j.key = key;
            j.sites = it->second;
            j.name = nm;
            const Buf& f = im.in_static ? st : sm;
            if (!track_mesh_read(f, s0.chain1, s0.chain2, j.om, why)) return fail(why, nm + ": " + *why);
        }
        const TrackMesh& om = j.om;
        const int layout = om.layout;
        // a material swap: backdrops and instance / prop models name their material themselves; a section's comes
        // from the render tables, which every unit around it shares
        if (im.new_material >= 0 && im.new_material != im.material) {
            if (world && s0.kind != TrackSite::kBackdrop)
                return fail(why, nm + ": a section's mesh cannot change its material (the render tables of the 17 units around it name it); "
                                      "edit the material's texture, or move the triangles into the section's mesh of the other material");
            if (std::size_t(im.new_material) >= tf.materials.size()) return fail(why, nm + ": material " + std::to_string(im.new_material) + " is not one of the route's");
            const auto& m2 = tf.materials[std::size_t(im.new_material)];
            if (track_material_layout(int(m2.type), m2.texture >= 0) != layout)
                return fail(why, nm + ": material " + std::to_string(im.new_material) + " (type " + std::to_string(m2.type) + (m2.texture >= 0 ? ", textured" : ", untextured") +
                                     ") draws another vertex layout than this mesh's: pick a material of the same type");
            for (std::size_t si : it->second)
                if (tf.sites[si].material == im.material) j.material.push_back({si, im.new_material});
        }
        // the new stream: the same triangles (in any order, any vertex sharing) keep the file's stream, with the model's
        // normals where they turned; an object transformed as a whole, the file's stream transformed; the same index
        // list, the file's strips with the new vertex fields; anything else new strips
        TrackMesh nm2;
        std::vector<TrackVertex> iv = im.v;
        for (TrackVertex& x : iv) layout_mask(layout, x);
        // the file's stream with the object's transform applied (positions; normals where they turned)
        auto move = [&](TrackMesh& t) {
            const auto& T = im.xform;
            const auto& N = im.nxform;
            for (TrackVertex& d : t.v) {
                const double p[3] = {d.pos[0], d.pos[1], d.pos[2]};
                for (int c = 0; c < 3; ++c)
                    d.pos[c] = float(T[std::size_t(c)] * p[0] + T[std::size_t(4 + c)] * p[1] + T[std::size_t(8 + c)] * p[2] + T[std::size_t(12 + c)]);
                if (!(layout & (kTrackNrm204 | kTrackNrm1))) continue;
                double q[3], l = 0;
                for (int c = 0; c < 3; ++c) { q[c] = N[std::size_t(c)] * d.nrm[0] + N[std::size_t(4 + c)] * d.nrm[1] + N[std::size_t(8 + c)] * d.nrm[2]; l += q[c] * q[c]; }
                l = std::sqrt(l);
                if (l < 1e-9) continue;
                std::int8_t b[3];
                for (int c = 0; c < 3; ++c) b[c] = std::int8_t(std::clamp(std::lround(127.0 * q[c] / l), -127L, 127L));
                if (!normal_close(b, d.nrm)) std::memcpy(d.nrm, b, 3);
            }
        };
        // the file's triangles with other colours, the vertices in another order (a tool that re-orders them, as
        // Blender's glTF exporter does, so the index list is not the file's): the file's stream, each vertex with the
        // colour (and the normal, where it turned) its corners have in the model's matching triangles (matched by
        // their other fields, the winding kept). False when the triangles differ in more than the colours, or a stored
        // vertex is given two colours (corners painted apart: new strips).
        auto recolour = [&](const std::vector<TrackVertex>& src, TrackMesh& t) {
            auto uncoloured = [](std::vector<TrackVertex> v) { for (TrackVertex& x : v) std::memset(x.col, 0, 4); return v; };
            const std::vector<TrackVertex> a = uncoloured(src), o = uncoloured(om.v);
            if (track_mesh_hash(a, im.tri, layout, false) != track_mesh_hash(o, om.tri, layout, false)) return false;
            if (im.tri.size() != om.tri.size()) return false;
            // a triangle by its corners' keys, rotated to start at the smallest (`rot`: where that corner was)
            using TriKey = std::array<VKey, 3>;
            auto tri_key = [](const std::vector<TrackVertex>& v, const std::uint32_t* c, int& rot) {
                VKey k[3];
                for (int i = 0; i < 3; ++i) k[i] = vkey(v[c[i]], false);
                auto less = [](const VKey& x, const VKey& y) { return std::memcmp(x.k, y.k, sizeof x.k) < 0; };
                rot = less(k[1], k[0]) ? (less(k[2], k[1]) ? 2 : 1) : (less(k[2], k[0]) ? 2 : 0);
                return TriKey{k[rot], k[(rot + 1) % 3], k[(rot + 2) % 3]};
            };
            auto tri_less = [](const TriKey& x, const TriKey& y) {
                for (int i = 0; i < 3; ++i)
                    if (const int d = std::memcmp(x[std::size_t(i)].k, y[std::size_t(i)].k, sizeof x[0].k)) return d < 0;
                return false;
            };
            std::map<TriKey, std::vector<std::pair<std::size_t, int>>, decltype(tri_less)> files(tri_less);
            for (std::size_t q = 0; q + 2 < om.tri.size(); q += 3) {
                int r = 0;
                const TriKey k = tri_key(o, &om.tri[q], r);
                files[k].push_back({q, r});
            }
            std::vector<int> from(om.v.size(), -1);   // file vertex -> the model's vertex that colours it
            for (std::size_t q = 0; q + 2 < im.tri.size(); q += 3) {
                int r = 0;
                auto f = files.find(tri_key(a, &im.tri[q], r));
                if (f == files.end() || f->second.empty()) return false;
                const auto [fq, fr] = f->second.back();
                f->second.pop_back();
                for (int i = 0; i < 3; ++i) {
                    const std::uint32_t fv = om.tri[fq + std::size_t((fr + i) % 3)], mv = im.tri[q + std::size_t((r + i) % 3)];
                    if (from[fv] >= 0 && std::memcmp(src[std::size_t(from[fv])].col, src[mv].col, 4) != 0) return false;
                    from[fv] = int(mv);
                }
            }
            // a vertex no triangle uses (an ADC filler) follows its drawn twin (the same stored fields in the file)
            std::unordered_map<VKey, std::size_t, VKeyHash> twin;
            for (std::size_t k = 0; k < om.v.size(); ++k)
                if (from[k] >= 0) twin.emplace(vkey(om.v[k]), k);
            t = om;
            for (std::size_t k = 0; k < om.v.size(); ++k) {
                int m = from[k];
                if (m < 0) {
                    auto f = twin.find(vkey(om.v[k]));
                    if (f == twin.end()) continue;
                    m = from[f->second];
                }
                TrackVertex& d = t.v[k];
                std::memcpy(d.col, src[std::size_t(m)].col, 4);
                if ((layout & (kTrackNrm204 | kTrackNrm1)) && !normal_close(src[std::size_t(m)].nrm, d.nrm)) std::memcpy(d.nrm, src[std::size_t(m)].nrm, 3);
            }
            return true;
        };
        const std::uint64_t oh = track_mesh_hash(om.v, om.tri, layout, false);
        std::uint32_t first = ~0u, last = 0;
        for (std::uint32_t x : om.tri) { first = std::min(first, x); last = std::max(last, x + 1); }
        if (om.tri.empty()) first = last = 0;
        std::vector<std::uint32_t> otri(om.tri);
        for (auto& x : otri) x -= first;
        if (!opt.restrip && track_mesh_hash(iv, im.tri, layout, false) == oh) {
            nm2 = om;
            std::unordered_map<VKey, std::vector<const TrackVertex*>, VKeyHash> at;
            for (const TrackVertex& x : iv) at[vkey(x, false)].push_back(&x);
            bool diff = false;
            if (layout & (kTrackNrm204 | kTrackNrm1))
                for (TrackVertex& d : nm2.v) {
                    auto f = at.find(vkey(d, false));
                    if (f == at.end()) continue;
                    bool close = false;
                    for (const TrackVertex* c : f->second) close = close || normal_close(c->nrm, d.nrm);
                    if (!close) { std::memcpy(d.nrm, f->second.front()->nrm, 3); diff = true; }
                }
            j.changed = j.changed || diff;
        } else if (!opt.restrip && !im.raw.empty() && [&] {
                       std::vector<TrackVertex> rv = im.raw;
                       for (TrackVertex& x : rv) layout_mask(layout, x);
                       return track_mesh_hash(rv, im.tri, layout, false) == oh;
                   }()) {
            nm2 = om;
            move(nm2);
            j.changed = true;
        } else if (!opt.restrip && (layout & (kTrackCol4 | kTrackCol3)) &&
                   (recolour(iv, nm2) || (!im.raw.empty() && [&] {
                        std::vector<TrackVertex> rv = im.raw;
                        for (TrackVertex& x : rv) layout_mask(layout, x);
                        if (!recolour(rv, nm2)) return false;
                        move(nm2);
                        return true;
                    }()))) {
            j.changed = true;
        } else if (!opt.restrip && iv.size() == last - first && im.tri == otri) {
            nm2 = om;
            // a vertex no triangle uses (an ADC filler) follows its drawn twin (the same stored fields in the file)
            std::vector<char> drawn(om.v.size(), 0);
            for (std::uint32_t x : om.tri) drawn[x] = 1;
            std::unordered_map<VKey, std::size_t, VKeyHash> twin;
            for (std::size_t k = first; k < last; ++k)
                if (drawn[k]) twin.emplace(vkey(om.v[k]), k);
            bool diff = false;
            for (std::size_t k = 0; k < om.v.size(); ++k) {
                std::size_t from = k;
                if (!drawn[k]) {
                    auto f = twin.find(vkey(om.v[k]));
                    if (f == twin.end()) continue;
                    from = f->second;
                }
                const TrackVertex& s = iv[from - first];
                TrackVertex& d = nm2.v[k];
                const std::uint32_t w = d.w;
                std::int8_t keep[3];
                std::memcpy(keep, d.nrm, 3);
                d = s;
                d.w = w;
                if (normal_close(s.nrm, om.v[from].nrm)) std::memcpy(d.nrm, keep, 3);
                layout_mask(layout, d);
                diff = diff || !(vkey(d) == vkey(om.v[k]));
            }
            j.changed = j.changed || diff;
        } else {
            if (!track_strip_mesh(iv, im.tri, layout, nm2, why)) return fail(why, nm + ": " + *why);
            j.changed = true;
        }
        Buf a, b;
        if (!track_encode_mesh(nm2, a, b, why)) return fail(why, nm + ": " + *why);
        if (!j.pk[0].empty() && (a != j.pk[0] || b != j.pk[1]))
            return fail(why, nm + ": two different edits of the same packets (" + j.name + " is drawn from them too: an instance model or a mesh shared "
                                  "by several materials is one mesh)");
        j.pk[0] = std::move(a);
        j.pk[1] = std::move(b);
        j.nm = std::move(nm2);
    }
    // ---- the regions ----
    std::vector<Region> R;
    {
        Region rs;
        rs.b = st;
        rs.is_static = true;
        rs.used = st.size();
        rs.limit = opt.max_static ? opt.max_static : std::max(kTrackStaticBlock, st.size());
        R.push_back(std::move(rs));
        for (std::size_t k = 0; k < tf.blocks.size(); ++k) {
            const TrackFiles::Block& B = tf.blocks[k];
            Region r;
            r.b.assign(sm.begin() + std::ptrdiff_t(B.at), sm.begin() + std::ptrdiff_t(B.at + B.size));
            r.base = B.at;
            r.used = B.used - B.at;
            r.limit = (k & 1) ? kTrackLodSlot : kTrackHighSlot;
            R.push_back(std::move(r));
        }
    }
    auto region_of = [&](const TrackSite& s) -> std::size_t { return s.in_static ? 0 : 1 + 2 * std::size_t(s.unit) + (s.kind == TrackSite::kLod ? 1 : 0); };
    struct Pend { Job* job; int part; std::size_t region, min_at; };
    std::vector<Pend> pend;
    for (auto& [key, j] : jobs) {
        const TrackSite& s0 = tf.sites[j.sites.front()];
        Region& rg = R[region_of(s0)];
        const Buf& f = s0.in_static ? st : sm;
        const bool same = [&] {
            for (int part = 0; part < 2; ++part) {
                const std::size_t at = part ? s0.chain2 : s0.chain1;
                if (!at) { if (!j.pk[part].empty()) return false; continue; }
                if (j.pk[part].size() != chain_end(f, at) - at || !std::equal(j.pk[part].begin(), j.pk[part].end(), f.begin() + std::ptrdiff_t(at))) return false;
            }
            return true;
        }();
        j.changed = !same;
        if (same) { ++rep.unchanged; continue; }   // (a material swap is written below)
        // how it came out: the file's strips with new fields, or new strips
        (j.nm.batches == j.om.batches && j.nm.tri == j.om.tri ? rep.edited : rep.restripped) += 1;
        std::size_t min_at = 0;
        for (std::size_t si : j.sites) min_at = std::max(min_at, tf.sites[si].ref_at - rg.base + 1);
        for (int part = 0; part < 2; ++part) {
            const std::size_t at = part ? s0.chain2 : s0.chain1;
            const Buf& nb = j.pk[part];
            const bool owned = at && refs[{s0.in_static, at}] == j.sites.size();
            if (at && owned) {
                const std::size_t a = at - rg.base, e = chain_end(f, at) - rg.base;
                if (!nb.empty() && nb.size() <= e - a) {
                    std::copy(nb.begin(), nb.end(), rg.b.begin() + std::ptrdiff_t(a));
                    std::fill(rg.b.begin() + std::ptrdiff_t(a + nb.size()), rg.b.begin() + std::ptrdiff_t(e), 0);
                    if (a + nb.size() < e) rg.holes.push_back({a + nb.size(), e});
                    continue;
                }
                std::fill(rg.b.begin() + std::ptrdiff_t(a), rg.b.begin() + std::ptrdiff_t(e), 0);
                rg.holes.push_back({a, e});
            }
            if (nb.empty()) {   // no chain 2 any more
                for (std::size_t si : j.sites) {
                    const TrackSite& s = tf.sites[si];
                    wr32(R[region_of(s)].b, s.ref_at - rg.base + (s.kind == TrackSite::kInstance || s.kind == TrackSite::kProp ? 4 : 0x1C), 0);
                }
                continue;
            }
            pend.push_back({&j, part, region_of(s0), min_at});
        }
    }
    // the packets that move: the largest first, into the freed places (best fit; 64-byte aligned, as the files' chains;
    // after the structures that name them), else at the region's free end; a block grows by 32 KB steps up to its slot
    for (Region& rg : R) {
        std::sort(rg.holes.begin(), rg.holes.end());
        std::vector<std::pair<std::size_t, std::size_t>> merged;
        for (const auto& h : rg.holes) {
            const bool zero_gap = !merged.empty() && h.first >= merged.back().second && h.first - merged.back().second < 0x100 &&
                                  std::all_of(rg.b.begin() + std::ptrdiff_t(merged.back().second), rg.b.begin() + std::ptrdiff_t(h.first), [](std::uint8_t c) { return c == 0; });
            if (!merged.empty() && (h.first <= merged.back().second || zero_gap)) merged.back().second = std::max(merged.back().second, h.second);
            else merged.push_back(h);
        }
        rg.holes = std::move(merged);
    }
    std::stable_sort(pend.begin(), pend.end(), [](const Pend& x, const Pend& y) { return x.job->pk[x.part].size() > y.job->pk[y.part].size(); });
    for (const Pend& p : pend) {
        Region& rg = R[p.region];
        const Buf& nb = p.job->pk[p.part];
        const std::size_t sz = nb.size();
        std::size_t best = rg.holes.size(), place = 0, left = 0;
        for (std::size_t h = 0; h < rg.holes.size(); ++h) {
            const std::size_t at = std::max((rg.holes[h].first + 63) & ~std::size_t(63), (p.min_at + 63) & ~std::size_t(63));
            if (at + sz > rg.holes[h].second) continue;
            const std::size_t l = rg.holes[h].second - at - sz;
            if (best == rg.holes.size() || l < left) { best = h; place = at; left = l; }
        }
        if (best < rg.holes.size()) {
            const auto h = rg.holes[best];
            rg.holes.erase(rg.holes.begin() + std::ptrdiff_t(best));
            if (place > h.first) rg.holes.push_back({h.first, place});
            if (place + sz < h.second) rg.holes.push_back({place + sz, h.second});
            std::sort(rg.holes.begin(), rg.holes.end());
        } else {
            place = (std::max(rg.used, p.min_at) + 63) & ~std::size_t(63);
            if (rg.is_static) {
                rg.b.resize(std::max(rg.b.size(), place + sz), 0);
            } else if (place + sz > rg.b.size()) {
                const std::size_t want = (place + sz + 0x7FFF) & ~std::size_t(0x7FFF);
                const TrackSite& s0 = tf.sites[p.job->sites.front()];
                if (want > rg.limit && !opt.no_limits) {
                    char m[320];
                    std::snprintf(m, sizeof m, "%s: unit %d's %s block would need %zu bytes, over the game's %zu-byte slot for it (STREAMED.DAT "
                                               "blocks are loaded into fixed slots): use fewer vertices in that section", p.job->name.c_str(), s0.unit,
                                  s0.kind == TrackSite::kLod ? "LOD" : "high-detail", place + sz, rg.limit);
                    return fail(why, m);
                }
                rg.b.resize(want, 0);
                ++rep.blocks_grown;
            }
            rg.used = place + sz;
        }
        std::copy(nb.begin(), nb.end(), rg.b.begin() + std::ptrdiff_t(place));
        for (std::size_t si : p.job->sites) {
            const TrackSite& s = tf.sites[si];
            const bool pair = s.kind == TrackSite::kInstance || s.kind == TrackSite::kProp;
            const std::size_t ref = s.ref_at - rg.base;
            wr32(rg.b, ref + (pair ? 4 * std::size_t(p.part) : p.part ? 0x1C : 0x0C), std::uint32_t(place - ref));
        }
        ++rep.moved;
    }
    // the boxes the game's culling tests, grown where an edited mesh's new positions reach out of them (only the new
    // ones: on the disc some meshes' boxes do not hold all their own vertices, and those stay as they are); the
    // material swaps
    for (auto& [key, j] : jobs) {
        std::vector<TrackVertex> fresh;
        if (j.changed) {
            std::set<std::array<std::uint32_t, 3>> old_pos;
            for (const TrackVertex& x : j.om.v) old_pos.insert({fbits(x.pos[0]), fbits(x.pos[1]), fbits(x.pos[2])});
            for (const TrackVertex& x : j.nm.v)
                if (!old_pos.count({fbits(x.pos[0]), fbits(x.pos[1]), fbits(x.pos[2])})) fresh.push_back(x);
        }
        if (!fresh.empty())
            for (std::size_t si : j.sites) {
                const TrackSite& s = tf.sites[si];
                Region& rg = R[region_of(s)];
                const bool g = s.kind == TrackSite::kInstance || s.kind == TrackSite::kProp ? grow_aabb(R[0].b, s.owner_at, fresh)
                                                                                         : grow_mesh_box(rg.b, s.ref_at - rg.base, fresh);
                rep.boxes_grown += g;
                // a backdrop object's own box (the same form, around all its meshes: the disc's equal theirs)
                if (s.kind == TrackSite::kBackdrop) rep.boxes_grown += grow_mesh_box(R[0].b, s.owner_at, fresh);
            }
        for (const auto& [si, m] : j.material) {
            wr16(R[0].b, tf.sites[si].mat_at, std::uint16_t(m));
            ++rep.materials_changed;
        }
    }
    // texture swaps: entry 0 of the material's texture list (an offset from the material) names the new raster
    const std::size_t mat0 = rd32(st, 0x08);
    for (const TrackTextureEdit& e : swaps) {
        if (e.material < 0 || std::size_t(e.material) >= tf.materials.size() || e.texture < 0 || std::size_t(e.texture) >= tf.textures.size())
            return fail(why, "a texture swap names no material or texture of the route");
        const std::size_t mb = mat0 + 0x28 * std::size_t(e.material);
        const int count = std::int8_t(st[mb + 0x11]);
        if (count < 1) return fail(why, "material " + std::to_string(e.material) + " has no texture: an untextured material's meshes have no texture coordinates");
        if (count > 1) return fail(why, "material " + std::to_string(e.material) + " is animated (" + std::to_string(count) + " frames): its texture cannot be swapped; edit the frames' PNGs instead");
        const std::size_t list = mb + rd32(st, mb + 0x0C);
        const std::uint32_t v = std::uint32_t(std::int64_t(tf.textures[std::size_t(e.texture)]) - std::int64_t(mb));
        if (rd32(R[0].b, list) != v) { wr32(R[0].b, list, v); ++rep.textures_swapped; }
    }
    // the pictures: written into their rasters in place (the size and layout never change)
    for (const auto& [k, im] : pictures) {
        if (k < 0 || std::size_t(k) >= tf.textures.size() || !im.w) continue;
        EncodeResult er;
        std::string e;
        if (!encode_raster(R[0].b, tf.textures[std::size_t(k)], {im}, EncodeOptions{}, &er, &e)) return fail(why, "texture " + std::to_string(k) + ": " + e);
        rep.textures_changed += er.changed;
        for (const std::string& n : er.notes) rep.notes.push_back("texture " + std::to_string(k) + ": " + n);
    }
    // ---- the files again ----
    Buf& S = R[0].b;
    if (S.size() != st.size()) {
        S.resize((S.size() + 0x7FF) & ~std::size_t(0x7FF), 0);   // the disc's files end on a sector
        if (S.size() > R[0].limit && !opt.no_limits) {
            char m[300];
            std::snprintf(m, sizeof m, "STATIC.DAT would be %zu bytes, over the limit of %zu (the memory block the game loads it into): use fewer vertices "
                                       "in the backdrops and instance models", S.size(), R[0].limit);
            return fail(why, m);
        }
        wr32(S, 4, std::uint32_t(S.size()));
    }
    Buf M;
    if (!sm.empty()) {
        const std::size_t utab = rd32(st, 0x58);
        for (std::size_t k = 1; k < R.size(); ++k) {
            const std::size_t at = M.size(), sz = R[k].b.size();
            wr32(R[k].b, 8, std::uint32_t(sz));
            M.insert(M.end(), R[k].b.begin(), R[k].b.end());
            const std::size_t unit = (k - 1) / 2, e = utab + 16 * unit;
            wr32(S, e + ((k - 1) & 1 ? 4 : 0), std::uint32_t(at));
            wr32(S, e + ((k - 1) & 1 ? 12 : 8), std::uint32_t(sz));
        }
    }
    // read back: every site names the bytes it should, and the rewritten meshes decode to their new streams
    TrackFiles again;
    if (!track_files_read(S, M, again, why)) return fail(why, "the new files do not read back: " + *why + " (a writer error; nothing written)");
    if (again.sites.size() != tf.sites.size()) return fail(why, "the new files do not read back with the same meshes (a writer error; nothing written)");
    for (std::size_t i = 0; i < tf.sites.size(); ++i) {
        const TrackSite& a = tf.sites[i];
        const TrackSite& b = again.sites[i];
        const Buf& f0 = a.in_static ? st : sm;
        const Buf& f1 = b.in_static ? S : M;
        auto it = jobs.find({a.in_static, a.chain1});
        for (int part = 0; part < 2; ++part) {
            const std::size_t at0 = part ? a.chain2 : a.chain1, at1 = part ? b.chain2 : b.chain1;
            Buf want = it != jobs.end() ? it->second.pk[part] : (at0 ? Buf(f0.begin() + std::ptrdiff_t(at0), f0.begin() + std::ptrdiff_t(chain_end(f0, at0))) : Buf());
            const bool ok = want.empty() ? at1 == 0 : (at1 && chain_end(f1, at1) - at1 == want.size() && std::equal(want.begin(), want.end(), f1.begin() + std::ptrdiff_t(at1)));
            if (!ok) return fail(why, site_name(a) + ": the written packets are not where the file names them (a writer error; nothing written)");
        }
        if (it != jobs.end() && it->second.changed) {
            TrackMesh back;
            if (!track_mesh_read(f1, b.chain1, b.chain2, back, why)) return fail(why, site_name(a) + ": the written packets do not decode: " + *why);
            const TrackMesh& w = it->second.nm;
            bool same = back.v.size() == w.v.size() && back.batches == w.batches && back.tri == w.tri && back.layout == w.layout;
            for (std::size_t k = 0; same && k < w.v.size(); ++k) same = vkey(back.v[k]) == vkey(w.v[k]) && back.v[k].w == w.v[k].w;
            if (!same) return fail(why, site_name(a) + ": the written packets do not read back as meant (a writer error; nothing written)");
        }
    }
    for (int pass = 0; pass < 2; ++pass) {   // and the renderer's own reader takes them
        b3asset::GsMemory gs;
        b3asset::Model m;
        b3asset::TrackOptions to;
        to.lod = pass == 1;
        to.extra_backdrops = true;
        if (!b3asset::read_track(Bytes{S.data(), S.size()}, Bytes{M.data(), M.size()}, to, gs, m, why)) return fail(why, "the renderer's reader refuses the new files: " + *why);
    }
    for (const auto& [key, j] : jobs) {
        if (!j.changed) continue;
        rep.vertices += j.nm.v.size();
        rep.triangles += j.nm.tri.size() / 3;
    }
    rep.meshes = jobs.size();
    rep.static_after = S.size();
    rep.streamed_after = M.size();
    st_out = std::move(S);
    sm_out = std::move(M);
    if (rep_out) *rep_out = std::move(rep);
    return true;
}


// ---- the model ----

namespace {

// "m12_road01", "m12_road01_2s" (a modelling tool's ".001" ignored): the STATIC material, -1 none
int material_of_name(const std::string& n) {
    if (n.size() < 2 || n[0] != 'm' || !std::isdigit(static_cast<unsigned char>(n[1]))) return -1;
    std::size_t k = 1;
    int v = 0;
    while (k < n.size() && std::isdigit(static_cast<unsigned char>(n[k])) && v < 100000) v = v * 10 + (n[k++] - '0');
    return k < n.size() && n[k] != '_' ? -1 : v;
}

int gltf_material(const Json& doc, int index) {
    const Json* ms = doc.get("materials");
    if (!ms || index < 0 || std::size_t(index) >= ms->size()) return -1;
    const Json& m = ms->a[std::size_t(index)];
    if (const Json* ex = m.get("extras"); ex && ex->get("b3") && ex->get("b3")->get("material")) return int(ex->get("b3")->get("material")->as_int(-1));
    return m.get("name") ? material_of_name(m.get("name")->s) : -1;
}

// "section_012", "section_012_lod" (a ".001" ignored): unit and LOD; false otherwise
bool section_of(std::string s, int& unit, bool& lod) {
    if (const std::size_t dot = s.rfind('.'); dot != std::string::npos && dot + 1 < s.size() && s.find_first_not_of("0123456789", dot + 1) == std::string::npos)
        s.resize(dot);
    if (s.rfind("section_", 0) != 0 || s.size() < 11) return false;
    lod = s.size() > 4 && s.compare(s.size() - 4, 4, "_lod") == 0;
    const std::string d = s.substr(8, s.size() - 8 - (lod ? 4 : 0));
    if (d.empty() || d.find_first_not_of("0123456789") != std::string::npos) return false;
    unit = std::stoi(d);
    return true;
}

// "backdrop_3", "backdrop+28_0", "water_1", "water+30_0" (a ".001" ignored): table and group
bool backdrop_of(std::string s, int& table, int& group) {
    if (const std::size_t dot = s.rfind('.'); dot != std::string::npos && dot + 1 < s.size() && s.find_first_not_of("0123456789", dot + 1) == std::string::npos)
        s.resize(dot);
    static const char* const kNames[4] = {"backdrop_", "backdrop+28_", "water_", "water+30_"};
    for (int t = 3; t >= 0; --t) {
        const std::string n = kNames[t];
        if (s.rfind(n, 0) != 0 || s.size() == n.size() || s.find_first_not_of("0123456789", n.size()) != std::string::npos) continue;
        table = t;
        group = std::stoi(s.substr(n.size()));
        return true;
    }
    return false;
}

// "instance12_model", "prop3_model": kind and type
bool model_of(std::string s, bool& prop, int& type) {
    if (const std::size_t dot = s.rfind('.'); dot != std::string::npos && dot + 1 < s.size() && s.find_first_not_of("0123456789", dot + 1) == std::string::npos)
        s.resize(dot);
    const std::size_t u = s.rfind("_model");
    if (u == std::string::npos || u + 6 != s.size()) return false;
    std::string h = s.substr(0, u);
    prop = h.rfind("prop", 0) == 0;
    if (!prop && h.rfind("instance", 0) != 0) return false;
    h = h.substr(prop ? 4 : 8);
    if (h.empty() || h.find_first_not_of("0123456789") != std::string::npos) return false;
    type = std::stoi(h);
    return true;
}

std::string base_name(const std::string& p) {
    const std::size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}


// Accessor `ai`'s elements `pick`, their bytes as they are, as a new tightly packed accessor of the same form appended
// to `doc` / `bin`; -1 for an accessor this cannot copy (sparse, no buffer view, out of range)
int slice_accessor(Json& doc, Buf& bin, int ai, const std::vector<std::uint32_t>& pick) {
    Json* accs = doc.get("accessors");
    Json* views = doc.get("bufferViews");
    if (!accs || !views || ai < 0 || std::size_t(ai) >= accs->size()) return -1;
    const Json a = accs->a[std::size_t(ai)];
    if (a.get("sparse") || !a.get("bufferView") || !a.get("componentType") || !a.get("type")) return -1;
    const int vi = int(a.get("bufferView")->as_int(-1));
    if (vi < 0 || std::size_t(vi) >= views->size()) return -1;
    const Json& v = views->a[std::size_t(vi)];
    const int ct = int(a.get("componentType")->as_int());
    const std::string& ty = a.get("type")->s;
    const std::size_t cs = ct == gl::kByte || ct == gl::kUByte ? 1 : ct == gl::kShort || ct == gl::kUShort ? 2 : 4;
    const std::size_t nc = ty == "SCALAR" ? 1 : ty == "VEC2" ? 2 : ty == "VEC3" ? 3 : ty == "VEC4" ? 4 : 0;
    if (!nc) return -1;
    const std::size_t es = cs * nc, stride = v.get("byteStride") ? std::size_t(v.get("byteStride")->as_int()) : es;
    const std::size_t base = std::size_t(v.get("byteOffset") ? v.get("byteOffset")->as_int() : 0) +
                             std::size_t(a.get("byteOffset") ? a.get("byteOffset")->as_int() : 0);
    const std::size_t count = std::size_t(a.get("count") ? a.get("count")->as_int() : 0);
    Buf part;
    for (std::uint32_t k : pick) {
        const std::size_t at = base + std::size_t(k) * stride;
        if (k >= count || at + es > bin.size()) return -1;
        part.insert(part.end(), bin.begin() + std::ptrdiff_t(at), bin.begin() + std::ptrdiff_t(at + es));
    }
    while (bin.size() % 4) bin.push_back(0);
    Json nv = Json::object();
    nv.set("buffer", Json::integer(0));
    nv.set("byteOffset", Json::integer(std::int64_t(bin.size())));
    nv.set("byteLength", Json::integer(std::int64_t(part.size())));
    bin.insert(bin.end(), part.begin(), part.end());
    views->push(std::move(nv));
    Json na = Json::object();
    na.set("bufferView", Json::integer(std::int64_t(views->size() - 1)));
    na.set("componentType", Json::integer(ct));
    if (a.get("normalized")) na.set("normalized", *a.get("normalized"));
    na.set("count", Json::integer(std::int64_t(pick.size())));
    na.set("type", Json::str(ty));
    accs->push(std::move(na));
    return int(accs->size() - 1);
}

// A primitive that stands for several of the route's meshes of one material (Blender's glTF exporter writes one
// primitive per material slot, and leaves out the primitives' extras): cut back into one primitive per mesh, with the
// b3 extras naming it, when each of its triangles (by its three positions, the winding kept) is one of those meshes'
// and every triangle of theirs (but the degenerate ones) is in it. An edit that moves vertices or adds triangles
// cannot be told apart this way: false.
bool split_joined(Json& doc, Buf& bin, const Json& p, const std::vector<const TrackSite*>& sites,
                  const std::vector<const TrackMesh*>& orig, std::vector<Json>& parts) {
    parts.clear();
    const Json* at = p.get("attributes");
    if (!at || !at->get("POSITION")) return false;
    std::vector<double> pos, idx;
    int np = 0, nx = 0;
    if (!gltf_read_accessor(doc, bin, int(at->get("POSITION")->as_int()), pos, np, nullptr) || np != 3) return false;
    const std::size_t nv = pos.size() / 3;
    if (p.get("indices")) {
        if (!gltf_read_accessor(doc, bin, int(p.get("indices")->as_int()), idx, nx, nullptr)) return false;
    } else {
        for (std::size_t k = 0; k < nv; ++k) idx.push_back(double(k));
    }
    if (idx.size() % 3) return false;
    using Key = std::array<std::uint32_t, 9>;
    auto key_of = [](const float* const q[3]) {
        std::array<std::array<std::uint32_t, 3>, 3> b;
        for (int i = 0; i < 3; ++i) std::memcpy(b[std::size_t(i)].data(), q[i], 12);
        const int r = b[1] < b[0] ? (b[2] < b[1] ? 2 : 1) : (b[2] < b[0] ? 2 : 0);   // start at the smallest
        Key k{};
        for (int i = 0; i < 3; ++i) std::memcpy(&k[std::size_t(3 * i)], b[std::size_t((r + i) % 3)].data(), 12);
        return k;
    };
    auto degenerate = [](const Key& k) {
        auto same = [&](int a, int b) { return std::equal(k.begin() + 3 * a, k.begin() + 3 * a + 3, k.begin() + 3 * b); };
        return same(0, 1) || same(1, 2) || same(0, 2);
    };
    std::map<Key, std::vector<std::size_t>> owners;   // a triangle of the files' meshes -> which of them (once per copy)
    for (std::size_t c = 0; c < orig.size(); ++c)
        for (std::size_t t = 0; t + 2 < orig[c]->tri.size(); t += 3) {
            const float* q[3];
            for (int i = 0; i < 3; ++i) q[i] = orig[c]->v[orig[c]->tri[t + std::size_t(i)]].pos;
            owners[key_of(q)].push_back(c);
        }
    std::vector<float> fp(pos.begin(), pos.end());
    std::vector<std::vector<std::uint32_t>> tris(orig.size());
    for (std::size_t t = 0; t < idx.size(); t += 3) {
        const float* q[3];
        for (int i = 0; i < 3; ++i) {
            const double x = idx[t + std::size_t(i)];
            if (x < 0 || std::size_t(x) >= nv) return false;
            q[i] = &fp[3 * std::size_t(x)];
        }
        auto it = owners.find(key_of(q));
        if (it == owners.end() || it->second.empty()) return false;
        const std::size_t c = it->second.back();
        it->second.pop_back();
        for (int i = 0; i < 3; ++i) tris[c].push_back(std::uint32_t(idx[t + std::size_t(i)]));
    }
    for (const auto& [k, left] : owners)
        if (!left.empty() && !degenerate(k)) return false;   // a triangle of the files' meshes not in the primitive
    for (std::size_t c = 0; c < orig.size(); ++c) {
        if (tris[c].empty()) continue;
        std::vector<std::uint32_t> used(tris[c]);
        std::sort(used.begin(), used.end());
        used.erase(std::unique(used.begin(), used.end()), used.end());
        Json attr = Json::object();
        for (const auto& [name, ai] : at->o) {
            const int na = slice_accessor(doc, bin, int(ai.as_int(-1)), used);
            if (na < 0) return false;
            attr.set(name, Json::integer(na));
        }
        std::vector<std::uint32_t> local;
        for (std::uint32_t x : tris[c]) local.push_back(std::uint32_t(std::lower_bound(used.begin(), used.end(), x) - used.begin()));
        Json* views = doc.get("bufferViews");
        Json* accs = doc.get("accessors");
        while (bin.size() % 4) bin.push_back(0);
        Json iv = Json::object();
        iv.set("buffer", Json::integer(0));
        iv.set("byteOffset", Json::integer(std::int64_t(bin.size())));
        iv.set("byteLength", Json::integer(std::int64_t(4 * local.size())));
        for (std::uint32_t x : local)
            for (int i = 0; i < 4; ++i) bin.push_back(std::uint8_t(x >> (8 * i)));
        views->push(std::move(iv));
        Json ia = Json::object();
        ia.set("bufferView", Json::integer(std::int64_t(views->size() - 1)));
        ia.set("componentType", Json::integer(gl::kUInt));
        ia.set("count", Json::integer(std::int64_t(local.size())));
        ia.set("type", Json::str("SCALAR"));
        accs->push(std::move(ia));
        Json one = Json::object();
        one.set("attributes", std::move(attr));
        one.set("indices", Json::integer(std::int64_t(accs->size() - 1)));
        one.set("mode", Json::integer(gl::kTriangles));
        if (const Json* m = p.get("material")) one.set("material", *m);
        char c1[16];
        std::snprintf(c1, sizeof c1, "0x%08X", unsigned(sites[c]->chain1));
        Json b3 = Json::object();
        b3.set("file", Json::str(sites[c]->in_static ? "STATIC.DAT" : "STREAMED.DAT"));
        b3.set("chain1", Json::str(c1));
        b3.set("material", Json::integer(sites[c]->material));
        Json ex = Json::object();
        ex.set("b3", std::move(b3));
        one.set("extras", std::move(ex));
        parts.push_back(std::move(one));
    }
    return !parts.empty();
}

}  // namespace

bool track_model_read(const FileLoader& load, const std::string& model, const Json& side, const Buf& st, const Buf& sm,
                      std::vector<TrackImportMesh>& out, std::vector<TrackTextureEdit>& swaps, std::vector<std::string>* notes, std::string* why) {
    out.clear();
    swaps.clear();
    Json doc;
    Buf bin;
    if (!gltf_load_doc(load, model, doc, bin, why)) return false;
    const Json* nodes = doc.get("nodes");
    const Json* meshes = doc.get("meshes");
    if (!nodes || !meshes) return fail(why, model + ": no nodes or meshes");
    std::vector<M4> world;
    std::vector<int> seen;
    if (!gltf_world(doc, world, seen, why)) return fail(why, model + ": " + *why);
    TrackFiles tf;
    if (!track_files_read(st, sm, tf, why)) return false;
    std::map<std::pair<bool, std::size_t>, std::size_t> by_key;
    std::map<std::tuple<int, bool, int>, std::size_t> by_section;   // (unit, LOD, material) -> site
    std::map<std::pair<bool, int>, std::size_t> by_model;           // (prop, type) -> site
    std::map<std::tuple<int, int, int>, std::vector<std::size_t>> by_backdrop;   // (table, group, material) -> sites
    for (std::size_t i = 0; i < tf.sites.size(); ++i) {
        const TrackSite& s = tf.sites[i];
        by_key.emplace(std::make_pair(s.in_static, s.chain1), i);
        if ((s.kind == TrackSite::kHigh || s.kind == TrackSite::kLod) && s.material >= 0) by_section.emplace(std::make_tuple(s.unit, s.kind == TrackSite::kLod, s.material), i);
        if (s.kind == TrackSite::kInstance || s.kind == TrackSite::kProp) by_model.emplace(std::make_pair(s.kind == TrackSite::kProp, s.type), i);
        if (s.kind == TrackSite::kBackdrop) {
            auto& v = by_backdrop[{s.table, s.group, s.material}];
            bool same = false;   // the same chains named twice (one mesh)
            for (std::size_t k : v) same = same || tf.sites[k].chain1 == s.chain1;
            if (!same) v.push_back(i);
        }
    }
    // each node's parent (a placement is compared with the file's below its section's node: a section moved as a whole
    // moves only its meshes)
    std::vector<int> parent(nodes->size(), -1);
    for (std::size_t ni = 0; ni < nodes->size(); ++ni)
        if (const Json* ch = nodes->a[ni].get("children"))
            for (const Json& c : ch->a)
                if (c.n >= 0 && std::size_t(c.n) < parent.size()) parent[std::size_t(c.n)] = int(ni);
    std::size_t follow = 0;   // placed objects under a moved node
    // which nodes show each mesh
    std::map<int, std::vector<int>> nodes_of;
    for (std::size_t ni = 0; ni < nodes->size(); ++ni) {
        const Json* mj = nodes->a[ni].get("mesh");
        if (!mj || !seen[ni]) continue;
        const int mi = int(mj->as_int(-1));
        if (mi < 0 || std::size_t(mi) >= meshes->size()) return fail(why, model + ": a node names a mesh that does not exist");
        nodes_of[mi].push_back(int(ni));
    }
    std::map<std::pair<bool, std::size_t>, TrackMesh> orig;   // the file's streams, read as needed
    auto original = [&](const TrackSite& s) -> const TrackMesh* {
        auto k = std::make_pair(s.in_static, s.chain1);
        auto it = orig.find(k);
        if (it != orig.end()) return &it->second;
        TrackMesh m;
        if (!track_mesh_read(s.in_static ? st : sm, s.chain1, s.chain2, m, why)) return nullptr;
        return &orig.emplace(k, std::move(m)).first->second;
    };
    // an object with several meshes of one material (backdrops, water) as Blender's glTF exporter writes it: one
    // primitive per material, without the extras; cut back into its meshes while its triangles are theirs
    std::size_t rejoined = 0;
    if (Json* mm = doc.get("meshes")) {
        for (const auto& [mi, ns] : nodes_of) {
            Json& m = mm->a[std::size_t(mi)];
            Json* prims = m.get("primitives");
            if (!prims || ns.size() != 1) continue;   // (moved as a whole: its mesh keeps the file's positions)
            const Json& nd = nodes->a[std::size_t(ns.front())];
            const std::string group = nd.get("name") && !nd.get("name")->s.empty() ? nd.get("name")->s : (m.get("name") ? m.get("name")->s : "");
            int table = 0, grp = 0;
            if (!backdrop_of(group, table, grp)) continue;
            std::vector<Json> out_prims;
            bool cut = false;
            for (const Json& p : prims->a) {
                const Json* ex = p.get("extras");
                const int pmat = p.get("material") ? gltf_material(doc, int(p.get("material")->as_int(-1))) : -1;
                auto it = by_backdrop.find({table, grp, pmat});
                std::vector<Json> parts;
                if (!(ex && ex->get("b3")) && pmat >= 0 && it != by_backdrop.end() && it->second.size() > 1) {
                    std::vector<const TrackSite*> ss;
                    std::vector<const TrackMesh*> os;
                    for (std::size_t k : it->second) {
                        ss.push_back(&tf.sites[k]);
                        os.push_back(original(tf.sites[k]));
                        if (!os.back()) return fail(why, site_name(tf.sites[k]) + ": " + *why);
                    }
                    if (split_joined(doc, bin, p, ss, os, parts)) {
                        out_prims.insert(out_prims.end(), parts.begin(), parts.end());
                        cut = true;
                        ++rejoined;
                        continue;
                    }
                }
                out_prims.push_back(p);
            }
            if (cut) prims->a = std::move(out_prims);
        }
    }
    if (notes && rejoined)
        notes->push_back(std::to_string(rejoined) + " primitive(s) standing for several meshes of one material (Blender's glTF exporter joins "
                         "them): cut back into those meshes by their triangles");
    std::map<std::pair<bool, std::size_t>, std::vector<M4>> placed;   // instance / prop model -> its nodes' matrices
    std::map<std::pair<bool, std::size_t>, std::string> taken;        // chains -> the primitive that gave them
    std::size_t no_color = 0, lost_color = 0, made_normals = 0, odd = 0;
    std::vector<std::string> moved_objects;
    for (const auto& [mi, ns] : nodes_of) {
        const Json& m = meshes->a[std::size_t(mi)];
        const std::string mname = m.get("name") ? m.get("name")->s : "";
        const Json* prims = m.get("primitives");
        if (!prims) continue;
        const std::string nname = nodes->a[std::size_t(ns.front())].get("name") ? nodes->a[std::size_t(ns.front())].get("name")->s : mname;
        for (std::size_t pi = 0; pi < prims->size(); ++pi) {
            const Json& p = prims->a[pi];
            const std::string shown = (nname.empty() ? mname : nname) + (prims->size() > 1 ? " primitive " + std::to_string(pi) : std::string());
            if (p.get("mode") && p.get("mode")->as_int() != gl::kTriangles) return fail(why, shown + ": a primitive that is not triangles (mode " + std::to_string(p.get("mode")->as_int()) + ")");
            const int pmat = p.get("material") ? gltf_material(doc, int(p.get("material")->as_int(-1))) : -1;
            // which of the route's meshes: the extras, else the section and the material, else the model's name
            std::size_t site = tf.sites.size();
            const Json* ex = p.get("extras");
            const Json* b3 = ex ? ex->get("b3") : nullptr;
            if (b3 && b3->get("file") && b3->get("chain1")) {
                const bool in_st = b3->get("file")->s == "STATIC.DAT";
                const std::size_t c1 = std::size_t(std::strtoull(b3->get("chain1")->s.c_str(), nullptr, 16));
                auto it = by_key.find({in_st, c1});
                if (it == by_key.end()) return fail(why, shown + ": its extras name a mesh the route's files do not have (" + b3->get("file")->s + " " + b3->get("chain1")->s + "): an export of another route?");
                site = it->second;
            } else {
                int unit = 0, type = 0;
                bool lod = false, prop = false;
                const std::string group = nname.empty() ? mname : nname;
                if (section_of(group, unit, lod) && pmat >= 0) {
                    auto it = by_section.find({unit, lod, pmat});
                    if (it == by_section.end())
                        return fail(why, shown + ": section " + std::to_string(unit) + (lod ? " LOD" : "") + " has no mesh of material " + std::to_string(pmat) +
                                             " (a unit's meshes are one per material, named by the render tables: the import cannot add one; use a material the section has)");
                    site = it->second;
                } else if (int table = 0, grp = 0; backdrop_of(group, table, grp) && pmat >= 0) {
                    auto it = by_backdrop.find({table, grp, pmat});
                    if (it == by_backdrop.end())
                        return fail(why, shown + ": " + group + " has no mesh of material " + std::to_string(pmat) + " (the import cannot add one; use a material the object has)");
                    if (it->second.size() > 1)
                        return fail(why, shown + ": " + group + " has " + std::to_string(it->second.size()) + " meshes of material " + std::to_string(pmat) +
                                             ", and the primitive has no b3 extras to say which (a tool that joins primitives of one material, as Blender does): "
                                             "edit this object in a tool that keeps the primitives' extras, or leave it out of the model");
                    site = it->second.front();
                } else if (model_of(mname, prop, type)) {
                    auto it = by_model.find({prop, type});
                    if (it != by_model.end()) site = it->second;
                }
                if (site == tf.sites.size())
                    return fail(why, "\"" + shown + "\" is not one of the route's meshes (no b3 extras, not a section's material, not an instance model): the import changes "
                                     "the meshes the route has and cannot add objects; join new geometry into a section's mesh");
            }
            const TrackSite& s = tf.sites[site];
            const auto key = std::make_pair(s.in_static, s.chain1);
            const bool inst = s.kind == TrackSite::kInstance || s.kind == TrackSite::kProp;
            if (inst) {
                for (int n : ns) {
                    M4 W = world[std::size_t(n)], Pi;
                    const int pn = parent[std::size_t(n)];
                    if (pn >= 0 && !m4_near_identity(world[std::size_t(pn)]) && m4_inverse(world[std::size_t(pn)], Pi)) {
                        W = m4_mul(Pi, W);
                        ++follow;
                    }
                    placed[key].push_back(W);
                }
            } else if (ns.size() > 1) {
                return fail(why, "\"" + shown + "\" is shown by " + std::to_string(ns.size()) + " nodes: a world-space mesh stands once (a linked duplicate? join it)");
            }
            const int expect_mat = b3 && b3->get("material") ? int(b3->get("material")->as_int(-1)) : s.material;
            const std::string tkey = shown + "#" + std::to_string(expect_mat);
            if (auto t = taken.find(key); t != taken.end() && !inst && t->second != tkey)
                return fail(why, "\"" + shown + "\" and \"" + t->second.substr(0, t->second.find('#')) + "\" both stand for " + site_name(s) + " (keep one primitive per mesh)");
            taken[key] = tkey;
            const TrackMesh* om = original(s);
            if (!om) return fail(why, site_name(s) + ": " + *why);
            const int layout = om->layout;
            TrackImportMesh im;
            im.in_static = s.in_static;
            im.chain1 = s.chain1;
            im.chain2 = s.chain2;
            im.material = expect_mat;
            im.name = shown;
            if (pmat >= 0 && pmat != expect_mat) im.new_material = pmat;
            // the node's transform (world-space meshes): applied to the vertices
            M4 T = inst ? kM4Identity : world[std::size_t(ns.front())];
            const bool ident = m4_near_identity(T);
            M4 N{};
            if (!ident) {
                M4 Ti;
                if (!m4_inverse(T, Ti)) return fail(why, shown + ": its node's matrix cannot be inverted (scaled to zero?)");
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c) N[std::size_t(c * 4 + r)] = Ti[std::size_t(r * 4 + c)];
                moved_objects.push_back(shown);
            }
            const Json* at = p.get("attributes");
            if (!at || !at->get("POSITION")) return fail(why, shown + ": a primitive without POSITION");
            std::vector<double> pos, uv, col, rawc, nrm, rawn, light, idx;
            int np = 0, nu = 0, nc = 0, nrc = 0, nn = 0, nrn = 0, nl = 0, nx = 0;
            auto acc = [&](const char* k, std::vector<double>& v, int& c) {
                if (!at->get(k)) return true;
                return gltf_read_accessor(doc, bin, int(at->get(k)->as_int()), v, c, why);
            };
            if (!acc("POSITION", pos, np) || np != 3) return fail(why, shown + ": POSITION: " + (np != 3 ? std::string("not VEC3") : *why));
            if (!acc("TEXCOORD_0", uv, nu) || (!uv.empty() && nu != 2)) return fail(why, shown + ": TEXCOORD_0 unreadable");
            if (!acc("COLOR_0", col, nc) || (!col.empty() && nc != 3 && nc != 4)) return fail(why, shown + ": COLOR_0 unreadable");
            if (!acc("NORMAL", nrm, nn) || (!nrm.empty() && nn != 3)) return fail(why, shown + ": NORMAL unreadable");
            // the stored fields: _B3_COLOR_R .. _A, _B3_NRM_X .. _W, _B3_LIGHT_X .. _W as the export writes them, or the
            // one VEC4 each of exports before them
            gltf_read_field(doc, bin, *at, "_B3_COLOR", {"R", "G", "B", "A"}, 255.0, rawc, nrc);
            if (nrc != 4) rawc.clear();
            gltf_read_field(doc, bin, *at, "_B3_NRM", {"X", "Y", "Z", "W"}, 127.0, rawn, nrn);
            if (nrn != 4) rawn.clear();
            gltf_read_field(doc, bin, *at, "_B3_LIGHT", {"X", "Y", "Z", "W"}, 1.0, light, nl);
            if (nl != 4) light.clear();
            const std::size_t nv = pos.size() / 3;
            if ((!uv.empty() && uv.size() / 2 != nv) || (!col.empty() && col.size() / std::size_t(nc) != nv) || (!nrm.empty() && nrm.size() / 3 != nv) ||
                (!rawc.empty() && rawc.size() / 4 != nv) || (!rawn.empty() && rawn.size() / 4 != nv) || (!light.empty() && light.size() / 4 != nv))
                return fail(why, shown + ": attributes of different lengths");
            if ((layout & (kTrackUv16 | kTrackUv32)) && uv.empty())
                return fail(why, shown + ": no texture coordinates (TEXCOORD_0): every vertex of this mesh needs its place in the texture");
            if (p.get("indices")) {
                if (!gltf_read_accessor(doc, bin, int(p.get("indices")->as_int()), idx, nx, why)) return fail(why, shown + ": indices: " + *why);
            } else {
                for (std::size_t k = 0; k < nv; ++k) idx.push_back(double(k));
            }
            if (idx.size() % 3) return fail(why, shown + ": the index count is not a multiple of 3");
            for (double x : idx) {
                if (x < 0 || std::size_t(x) >= nv) return fail(why, shown + ": an index past its vertices");
                im.tri.push_back(std::uint32_t(x));
            }
            auto whole = [](const double* v, int n, double lo, double hi) {
                for (int i = 0; i < n; ++i)
                    if (!(v[i] > lo - 1e-3 && v[i] < hi + 1e-3) || std::fabs(v[i] - std::nearbyint(v[i])) > 1e-3) return false;
                return true;
            };
            // the original vertex nearest to a position (for what the model does not carry)
            auto nearest = [&](const float* q) -> const TrackVertex& {
                const TrackVertex* best = &om->v.front();
                double bd = -1;
                for (const TrackVertex& o : om->v) {
                    double d = 0;
                    for (int c = 0; c < 3; ++c) d += double(o.pos[c] - q[c]) * double(o.pos[c] - q[c]);
                    if (bd < 0 || d < bd) { bd = d; best = &o; }
                }
                return *best;
            };
            std::size_t bad_uv = 0;
            std::vector<char> face_n(nv, 0);
            for (std::size_t k = 0; k < nv; ++k) {
                TrackVertex v;
                float raw[3];
                for (int c = 0; c < 3; ++c) raw[c] = float(pos[3 * k + std::size_t(c)]);
                if (ident) std::memcpy(v.pos, raw, 12);
                else
                    for (int c = 0; c < 3; ++c)
                        v.pos[c] = float(T[std::size_t(c)] * raw[0] + T[std::size_t(4 + c)] * raw[1] + T[std::size_t(8 + c)] * raw[2] + T[std::size_t(12 + c)]);
                if (layout & kTrackUv16) {
                    for (int c = 0; c < 2; ++c) {
                        const double t = std::nearbyint(uv[2 * k + std::size_t(c)] * 4096.0);
                        if (!(t >= -32768.0 && t <= 32767.0)) { ++bad_uv; continue; }
                        v.uv[c] = std::int32_t(t);
                    }
                } else if (layout & kTrackUv32) {
                    float f[2];
                    for (int c = 0; c < 2; ++c) f[c] = float(uv[2 * k + std::size_t(c)]);
                    // float coordinates as Blender gives them back: v' = 1 - v and back rounds v to the precision of
                    // 1 + |v| (and -0 comes back as +0): the file's vertex at this position whose coordinates are
                    // that close keeps its own bits
                    const TrackVertex* best = nullptr;
                    double bd = 1.0;
                    for (const TrackVertex& o : om->v) {
                        if (std::memcmp(o.pos, v.pos, 12) != 0) continue;
                        float g[2];
                        std::memcpy(g, o.uv, 8);
                        double d = 0;
                        for (int c = 0; c < 2; ++c) d = std::max(d, std::fabs(double(g[c]) - double(f[c])) / (2.5e-7 * (1.0 + std::fabs(double(g[c])))));
                        if (d <= bd) { bd = d; best = &o; }
                    }
                    if (best) std::memcpy(f, best->uv, 8);
                    std::memcpy(v.uv, f, 8);
                }
                const TrackVertex* near = nullptr;
                auto nearv = [&]() -> const TrackVertex& { if (!near) near = &nearest(raw); return *near; };
                if (layout & (kTrackCol4 | kTrackCol3)) {
                    const int chans = (layout & kTrackCol4) ? 4 : 3;
                    const bool have_raw = !rawc.empty() && whole(&rawc[4 * k], 4, 0, 255);
                    if (!have_raw && col.empty()) {
                        std::memcpy(v.col, nearv().col, 4);
                        ++no_color;
                    }
                    for (int c = 0; c < chans; ++c) {
                        const int r = have_raw ? int(std::lround(rawc[4 * k + std::size_t(c)])) : -1;
                        if (col.empty() || c >= nc) { if (r >= 0) v.col[c] = std::uint8_t(r); continue; }
                        // COLOR_0 as the export wrote it: the stored byte x 4 (alpha x 2), clamped; edited when it differs
                        const int shown_c = int(std::lround(std::clamp(col[k * std::size_t(nc) + std::size_t(c)], 0.0, 1.0) * 255.0));
                        const int mul = c < 3 ? 4 : 2;
                        if (r >= 0 && std::abs(std::min(255, r * mul) - shown_c) <= 1) { v.col[c] = std::uint8_t(r); continue; }
                        v.col[c] = std::uint8_t(std::clamp(int(std::lround(double(shown_c) / mul)), 0, 255));
                        if (r < 0 && shown_c >= 254) ++lost_color;
                    }
                }
                if (layout & (kTrackNrm204 | kTrackNrm1)) {
                    double n3[3] = {0, 0, 0};
                    bool have_n = !nrm.empty();
                    if (have_n) {
                        for (int c = 0; c < 3; ++c) n3[c] = nrm[3 * k + std::size_t(c)];
                        if (!ident) {
                            double t[3];
                            for (int c = 0; c < 3; ++c) t[c] = N[std::size_t(c)] * n3[0] + N[std::size_t(4 + c)] * n3[1] + N[std::size_t(8 + c)] * n3[2];
                            std::memcpy(n3, t, sizeof t);
                        }
                        const double l = std::sqrt(n3[0] * n3[0] + n3[1] * n3[1] + n3[2] * n3[2]);
                        if (l > 1e-9) for (double& x : n3) x /= l;
                        else have_n = false;
                    }
                    const bool have_raw = !rawn.empty() && whole(&rawn[4 * k], 4, -128, 127);
                    bool used = false;
                    if (have_raw) {
                        std::int8_t b[4];
                        double s[3], l = 0;
                        for (int c = 0; c < 4; ++c) b[c] = std::int8_t(std::lround(rawn[4 * k + std::size_t(c)]));
                        for (int c = 0; c < 3; ++c) { s[c] = b[c]; l += s[c] * s[c]; }
                        l = std::sqrt(l);
                        if (l < 1e-6) { s[0] = 0; s[1] = 1; s[2] = 0; l = 1; }
                        const double dot = have_n ? (s[0] * n3[0] + s[1] * n3[1] + s[2] * n3[2]) / l : 1.0;
                        v.nrm[3] = b[3];
                        if (dot > 0.999 && (ident || have_n)) { std::memcpy(v.nrm, b, 3); used = true; }
                    } else {
                        v.nrm[3] = nearv().nrm[3];
                    }
                    if (!used && have_n)
                        for (int c = 0; c < 3; ++c) v.nrm[c] = std::int8_t(std::clamp(std::lround(127.0 * n3[c]), -127L, 127L));
                    face_n[k] = !used && !have_n;
                }
                if (layout & kTrackLight) {
                    if (!light.empty()) for (int c = 0; c < 4; ++c) v.light[c] = float(light[4 * k + std::size_t(c)]);
                    else { std::memcpy(v.light, nearv().light, 16); ++odd; }
                }
                im.v.push_back(v);
                if (!ident) {
                    TrackVertex rv = v;
                    std::memcpy(rv.pos, raw, 12);
                    im.raw.push_back(rv);
                }
            }
            if (bad_uv) {
                char t[220];
                std::snprintf(t, sizeof t, "%s: %zu texture coordinates outside -8 .. 8 (the game stores them as 4.12 fixed point): keep the UVs within 8 repeats of the texture",
                              shown.c_str(), bad_uv);
                return fail(why, t);
            }
            if (std::find(face_n.begin(), face_n.end(), 1) != face_n.end()) {   // no normal at all: the faces' (area-weighted)
                std::vector<std::array<double, 3>> a(im.v.size(), {0, 0, 0});
                for (std::size_t t = 0; t + 2 < im.tri.size(); t += 3) {
                    const float* x = im.v[im.tri[t]].pos; const float* y = im.v[im.tri[t + 1]].pos; const float* z = im.v[im.tri[t + 2]].pos;
                    const double u[3] = {y[0] - x[0], y[1] - x[1], y[2] - x[2]}, w[3] = {z[0] - x[0], z[1] - x[1], z[2] - x[2]};
                    const double n[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
                    for (int q = 0; q < 3; ++q) for (int c = 0; c < 3; ++c) a[im.tri[t + std::size_t(q)]][std::size_t(c)] += n[c];
                }
                for (std::size_t k = 0; k < im.v.size(); ++k) {
                    if (!face_n[k]) continue;
                    const double l = std::sqrt(a[k][0] * a[k][0] + a[k][1] * a[k][1] + a[k][2] * a[k][2]);
                    for (int c = 0; c < 3; ++c) im.v[k].nrm[c] = std::int8_t(std::clamp(std::lround(127.0 * (l > 0 ? a[k][std::size_t(c)] / l : c == 1 ? 1.0 : 0.0)), -127L, 127L));
                    ++made_normals;
                }
            }
            if (!ident) {
                std::copy(T.begin(), T.end(), im.xform.begin());
                std::copy(N.begin(), N.end(), im.nxform.begin());
            }
            out.push_back(std::move(im));
        }
    }
    // the placements stay the file's: every instance / prop model's nodes where the file places it
    if (!placed.empty()) {
        b3asset::GsMemory gs;
        b3asset::Model m;
        b3asset::TrackOptions to;
        to.lod = false;
        to.backdrops = false;
        const int section = side.get("section") ? int(side.get("section")->as_int(-1)) : -1;
        if (section >= 0) { to.first_unit = section; to.unit_count = 1; }
        std::map<std::pair<bool, std::size_t>, std::vector<std::array<float, 16>>> file_at;
        to.on_mesh = [&](const b3asset::TrackOptions::MeshRef& r) {
            if (r.draw >= 0 && std::size_t(r.draw) < m.draws.size() && m.draws[std::size_t(r.draw)].transform >= 0)
                file_at[{r.in_static, r.chain1}].push_back(m.transforms[std::size_t(m.draws[std::size_t(r.draw)].transform)]);
        };
        if (!b3asset::read_track(Bytes{st.data(), st.size()}, Bytes{sm.data(), sm.size()}, to, gs, m, why)) return false;
        for (auto& [key, mats] : placed) {
            auto& want = file_at[key];
            const TrackSite& s = tf.sites[by_key[key]];
            // how far a node's matrix is from a placement, in tolerances (<= 1: the same placement): the 3x3 to 1e-3
            // relative, the position to 1 mm (a tool's location / rotation / scale round trip is far closer)
            const auto off = [](const M4& a, const std::array<float, 16>& b) {
                double d = 0;
                for (int i = 0; i < 16; ++i) {
                    const double x = std::fabs(a[std::size_t(i)] - double(b[std::size_t(i)]));
                    const double tol = (i >= 12 && i < 15) ? 1e-3 + 1e-6 * std::fabs(double(b[std::size_t(i)])) : 1e-3 * (1.0 + std::fabs(double(b[std::size_t(i)])));
                    d = std::max(d, x / tol);
                }
                return d;
            };
            // each node to the closest placement not taken yet (a tool's node order is not the file's)
            bool ok = mats.size() == want.size();
            std::vector<char> used(want.size(), 0);
            for (std::size_t i = 0; ok && i < mats.size(); ++i) {
                std::size_t best = want.size();
                double bd = 1.0;
                for (std::size_t k = 0; k < want.size(); ++k)
                    if (!used[k])
                        if (const double d = off(mats[i], want[k]); d <= bd) { bd = d; best = k; }
                ok = best < want.size();
                if (ok) used[best] = 1;
            }
            if (!ok)
                return fail(why, site_name(s) + ": its placements in the model are not the file's (" + std::to_string(mats.size()) + " nodes, the file places it " +
                                     std::to_string(want.size()) + " times, or one moved): the import keeps where instances and props stand (they are part of the "
                                     "route's layout); edit the model's mesh instead");
        }
    }
    // texture swaps: a material whose base colour texture is another of the route's textures
    if (const Json* gm = doc.get("materials")) {
        const Json* tex_list = side.get("textures");
        std::map<int, int> want;   // material -> texture
        for (std::size_t i = 0; i < gm->size(); ++i) {
            const int mi = gltf_material(doc, int(i));
            if (mi < 0 || std::size_t(mi) >= tf.materials.size()) continue;
            const Json* pbr = gm->a[i].get("pbrMetallicRoughness");
            const Json* bt = pbr ? pbr->get("baseColorTexture") : nullptr;
            if (!bt || !bt->get("index")) continue;
            const Json* texs = doc.get("textures");
            const Json* ims = doc.get("images");
            const int ti = int(bt->get("index")->as_int(-1));
            if (!texs || !ims || ti < 0 || std::size_t(ti) >= texs->size() || !texs->a[std::size_t(ti)].get("source")) continue;
            const int ii = int(texs->a[std::size_t(ti)].get("source")->as_int(-1));
            if (ii < 0 || std::size_t(ii) >= ims->size()) continue;
            const Json& img = ims->a[std::size_t(ii)];
            std::string uri = img.get("uri") ? base_name(img.get("uri")->s) : "";
            if (uri.empty() && img.get("name") && !img.get("name")->s.empty()) {
                // an image in a .glb's buffer: Blender names it after the PNG it was loaded from (without ".png")
                uri = img.get("name")->s;
                std::string low = uri;
                for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (low.size() < 4 || low.compare(low.size() - 4, 4, ".png") != 0) uri += ".png";
            }
            int t = -1;
            for (std::size_t k = 0; tex_list && k < tex_list->size(); ++k)
                if (tex_list->a[k].s == uri) t = int(k);
            if (t < 0) {
                if (notes) notes->push_back("material " + std::to_string(mi) + ": its texture (" + (uri.empty() ? std::string("an image without a file name") : uri) +
                                            ") is not one of the route's PNGs: kept as it is (new textures cannot be added; edit one of the PNGs)");
                continue;
            }
            if (auto w = want.find(mi); w != want.end() && w->second != t)
                return fail(why, "material " + std::to_string(mi) + " is given two different textures (its one- and two-sided glTF materials disagree)");
            want[mi] = t;
        }
        for (const auto& [mi, t] : want)
            if (t != tf.materials[std::size_t(mi)].texture) swaps.push_back({mi, t});
    }
    if (notes) {
        if (!moved_objects.empty())
            notes->push_back(std::to_string(moved_objects.size()) + " object(s) moved / turned / scaled (" + moved_objects.front() +
                             (moved_objects.size() > 1 ? " .. " + moved_objects.back() : std::string()) + "): applied to their vertices");
        if (follow)
            notes->push_back(std::to_string(follow) + " placed instances / props under a moved node: they stay where the file places them (the import moves a "
                             "section's meshes, not its placements)");
        if (no_color) notes->push_back(std::to_string(no_color) + " vertices without COLOR_0 or _B3_COLOR_R .. _A: they take the nearest original vertex's colour");
        if (lost_color) notes->push_back(std::to_string(lost_color) + " colour channels from COLOR_0 alone at full brightness (no _B3_COLOR_R .. _A): the stored byte may have been brighter (0x40 .. 0xFF all show as 1.0)");
        if (made_normals) notes->push_back(std::to_string(made_normals) + " vertices without a normal: made from the faces");
        if (odd) notes->push_back(std::to_string(odd) + " vertices without _B3_LIGHT_X .. _W (type 5 lighting): they take the nearest original vertex's");
    }
    return true;
}

bool track_import(const Buf& st, const Buf& sm, const FileLoader& load, const std::string& model, const std::string& sidecar, const TrackImportOptions& opt,
                  Buf& st_out, Buf& sm_out, TrackImportReport* rep, std::string* why) {
    Buf sj;
    Json side;
    if (!load(sidecar, sj)) return fail(why, "cannot read the sidecar " + sidecar);
    if (!json_parse(std::string_view(reinterpret_cast<const char*>(sj.data()), sj.size()), side, why)) return fail(why, sidecar + ": " + *why);
    if (!side.get("format") || side.get("format")->s != "b3modtool track export") return fail(why, sidecar + ": not a track export's sidecar");
    const Json* src = side.get("source");
    const std::string path = src && src->get("path") ? src->get("path")->s : "?";
    auto same = [&](const char* size, const char* hash, const Buf& f) {
        return src && src->get(size) && src->get(hash) && std::size_t(src->get(size)->as_int()) == f.size() && src->get(hash)->s == hex64(b3mod::fnv1a64(f.data(), f.size()));
    };
    if (!same("static_size", "static_fnv1a64", st) || !same("streamed_size", "streamed_fnv1a64", sm))
        return fail(why, "the export was made from another " + path + " than this one (its STATIC.DAT / STREAMED.DAT size and hash differ): export the route again from this disc");
    std::vector<TrackImportMesh> meshes;
    std::vector<TrackTextureEdit> swaps;
    std::vector<std::string> notes;
    if (!track_model_read(load, model, side, st, sm, meshes, swaps, &notes, why)) return false;
    // the texture pictures beside the sidecar (one per STATIC texture, as the export wrote them)
    std::map<int, b3asset::Image> pictures;
    std::size_t missing = 0;
    if (const Json* tex = side.get("textures")) {
        for (std::size_t k = 0; k < tex->size(); ++k) {
            Buf png;
            if (!load(path_dir_of(sidecar) + tex->a[k].s, png)) { ++missing; continue; }
            b3asset::Image im;
            std::string e;
            if (!decode_png(png, im.w, im.h, im.rgba, &e)) return fail(why, tex->a[k].s + ": " + e);
            pictures[int(k)] = std::move(im);
        }
    }
    if (missing) notes.push_back(std::to_string(missing) + " texture PNGs are not beside the sidecar: those textures stay as they are");
    TrackImportReport r;
    if (!track_import_meshes(st, sm, meshes, swaps, pictures, opt, st_out, sm_out, &r, why)) return false;
    // the export's meshes the model does not have any more (deleted objects): kept as they are
    if (const Json* ms = side.get("meshes")) {
        std::set<std::pair<bool, std::size_t>> exported, given;
        for (const Json& m : ms->a)
            if (m.get("file") && m.get("chain1"))
                exported.insert({m.get("file")->s == "STATIC.DAT", std::size_t(std::strtoull(m.get("chain1")->s.c_str(), nullptr, 16))});
        for (const TrackImportMesh& m : meshes) given.insert({m.in_static, m.chain1});
        for (const auto& k : exported) r.missing += !given.count(k);
        if (r.missing)
            notes.push_back(std::to_string(r.missing) + " of the export's meshes are not in the model (deleted objects?): they stay as they are in the files "
                            "(to take a mesh out, delete its faces and keep the object)");
    }
    notes.insert(notes.end(), r.notes.begin(), r.notes.end());
    r.notes = std::move(notes);
    if (rep) *rep = std::move(r);
    return true;
}

}  // namespace b3modtool
