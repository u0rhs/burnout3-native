// Car import: an exported (and edited) car model back into the game's vehicle file (include/b3modtool/export3d.h
// "car import"; the layout docs/modding/file-formats/car.md, "How the packets are written").
//
//   car_encode_mesh     the mesh packet writer (the inverse of car_read's decode)
//   car_strip_mesh      triangles -> strips -> VU batches, for meshes whose triangles changed
//   car_import_meshes   the new meshes and paints over the original file: packets in place, or moved when they grow
//   car_model_read      a .gltf / .glb (the export, or a modelling tool's save of it) -> CarImportMesh per entry
//   car_import          the two together, checked against the export's sidecar
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

#include "b3asset/raster.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"
#include "b3modtool/meshimport.h"

namespace b3modtool {

namespace {

bool fail(std::string* why, const std::string& m) { if (why) *why = m; return false; }

std::string hex32(std::uint32_t v) {
    char t[16];
    std::snprintf(t, sizeof t, "0x%08X", v);
    return t;
}

std::uint32_t rd32(const Buf& b, std::size_t at) { return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24; }
void wr32(Buf& b, std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) b[at + std::size_t(i)] = std::uint8_t(v >> (8 * i)); }
std::uint32_t fbits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }

// ---- the packet writer ----
using Stream = VifStream;

constexpr std::uint32_t kNop = 0x00000000, kStmod0 = 0x05000000, kStmodOffset = 0x05000011, kStcycl31 = 0x01000103,
                        kStcycl61 = 0x01000106, kStrow = 0x30000000, kRow = 0x47800000, kItop = 0x04000000, kMscnt = 0x17000000;

void encode_batch(Stream& s, const CarVertex* v, std::uint32_t n, int form, bool cont) {
    // positions: V4-32 at 0 (TOPS-relative), x y z and the w lane (ADC)
    s.align_cmd(2);
    s.put(kStmod0);
    s.put(kStcycl31);
    s.put(0x6C000000u | n << 16 | 0x8000u);
    for (std::uint32_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) s.put(fbits(v[i].pos[c]));
        s.put(v[i].w);
    }
    if (form != kCarPositions) {
        // normals: V3-8 unsigned at 2, offset mode (ROW 65536.0)
        s.align_cmd(2);
        s.put(kStmodOffset);
        s.put(kStcycl31);
        s.put(0x6A000000u | n << 16 | 0xC002u);
        const std::size_t nb = s.b.size();
        for (std::uint32_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) s.b.push_back(v[i].nrm[c]);
        while ((s.b.size() - nb) % 4) s.b.push_back(0);
        // texture coordinates and influences: V4-16 unsigned at 1, interleaved per group of four vertices g..g+3
        // (b3asset::read_car): slot g = uv(g), uv(g+2); g+1 = infl0 of g..g+3; g+2 = uv(g+1), uv(g+3); g+3 = infl1.
        // The 6/1 cycle writes only the texture coordinate slots.
        const bool uv_only = form == kCarUvOnly;
        const std::uint32_t slots = (n + 3) & ~3u;
        const std::uint32_t cnt = uv_only ? slots / 2 : slots;
        s.align_cmd(2);
        s.put(kStmod0);
        s.put(uv_only ? kStcycl61 : kStcycl31);
        s.put(0x6D000000u | (cnt & 0xFF) << 16 | 0xC001u);
        auto uvw = [&](std::uint32_t i) -> std::uint32_t { return i < n ? std::uint32_t(v[i].uv[0]) | std::uint32_t(v[i].uv[1]) << 16 : 0; };
        auto infl = [&](std::uint32_t i, int k) -> std::uint32_t { return i < n ? v[i].infl[k] : 0; };
        for (std::uint32_t e = 0; e < cnt; ++e) {
            const std::uint32_t slot = uv_only ? 2 * e : e, g = slot & ~3u;
            switch (slot & 3) {
            case 0: s.put(uvw(g)); s.put(uvw(g + 2)); break;
            case 2: s.put(uvw(g + 1)); s.put(uvw(g + 3)); break;
            default: {
                const int k = (slot & 3) == 1 ? 0 : 1;
                s.put(infl(g, k) | infl(g + 1, k) << 16);
                s.put(infl(g + 2, k) | infl(g + 3, k) << 16);
            }
            }
        }
    }
    s.put(kItop | n);
    if (cont) s.put(kMscnt);
}

void finish_packet(Stream& s) { s.finish(); }

// ---- vertex keys ----
struct VKey {
    std::uint8_t k[23];
    bool operator==(const VKey& o) const { return std::memcmp(k, o.k, sizeof k) == 0; }
};
struct VKeyHash {
    std::size_t operator()(const VKey& v) const { return std::size_t(b3mod::fnv1a64(v.k, sizeof v.k)); }
};
VKey vkey(const CarVertex& v) {
    VKey r;
    std::memcpy(r.k, v.pos, 12);
    std::memcpy(r.k + 12, v.uv, 4);
    std::memcpy(r.k + 16, v.infl, 4);
    std::memcpy(r.k + 20, v.nrm, 3);
    return r;
}

// the triangle set with every stored field (positions, texture coordinates, influences, normals)
// (`normals` false: without them, for a model whose normals were re-made by a tool)
std::uint64_t full_tri_hash(std::vector<CarVertex> v, const std::vector<std::uint32_t>& tri, int form, bool normals = true) {
    std::vector<std::array<float, 3>> pos;
    std::vector<std::uint8_t> extra;
    for (CarVertex& x : v) {
        car_form_mask(form, x);
        if (!normals) x.nrm[0] = x.nrm[1] = x.nrm[2] = 0;
        pos.push_back({x.pos[0], x.pos[1], x.pos[2]});
        const std::uint16_t e[4] = {x.uv[0], x.uv[1], x.infl[0], x.infl[1]};
        const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(e);
        extra.insert(extra.end(), p, p + 8);
        extra.insert(extra.end(), x.nrm, x.nrm + 3);
    }
    return triangle_set_hash(pos, extra, 11, tri);
}

// two stored normals that point the same way (within 2.5 degrees; a zero one counts as +Y, as the export writes it)
bool normal_close(const std::uint8_t* a, const std::uint8_t* b) {
    double x[3], y[3], lx = 0, ly = 0, d = 0;
    for (int c = 0; c < 3; ++c) {
        x[c] = (double(a[c]) - 128.0) / 128.0;
        y[c] = (double(b[c]) - 128.0) / 128.0;
        lx += x[c] * x[c];
        ly += y[c] * y[c];
    }
    if (lx < 1e-12) { x[0] = x[2] = 0; x[1] = 1; lx = 1; }
    if (ly < 1e-12) { y[0] = y[2] = 0; y[1] = 1; ly = 1; }
    for (int c = 0; c < 3; ++c) d += x[c] * y[c];
    return d / std::sqrt(lx * ly) > 0.999;
}

}  // namespace

int car_slot_form(int slot) { return slot >= 18 ? kCarPositions : slot >= 7 && slot <= 9 ? kCarUvOnly : kCarFull; }

void car_form_mask(int form, CarVertex& v) {
    if (form == kCarFull) return;
    v.infl[0] = v.infl[1] = 0;
    if (form == kCarUvOnly) return;
    v.uv[0] = v.uv[1] = 0;
    v.nrm[0] = v.nrm[1] = v.nrm[2] = 0;
}

bool car_encode_mesh(const CarMesh& m, int form, CarPackets& out, std::string* why) {
    out = CarPackets{};
    std::size_t total = 0;
    for (std::uint32_t n : m.batches) {
        if (n == 0 || n > std::uint32_t(kCarMaxBatch)) return fail(why, "a batch of " + std::to_string(n) + " vertices (1 .. " + std::to_string(kCarMaxBatch) + ")");
        total += n;
    }
    if (m.batches.empty() || total != m.v.size()) return fail(why, "the batches do not cover the vertices");
    Stream first;
    first.put(0);
    first.put(0);   // the DMA tag, filled in last
    if (form != kCarPositions) {   // the normals' offset (positions-only packets have none)
        first.align_cmd(0);
        first.put(kStrow);
        for (int i = 0; i < 4; ++i) first.put(kRow);
    }
    encode_batch(first, m.v.data(), m.batches[0], form, false);
    finish_packet(first);
    out.first = std::move(first.b);
    if (m.batches.size() > 1) {
        Stream cont;
        cont.put(0);
        cont.put(0);
        std::size_t at = m.batches[0];
        for (std::size_t b = 1; b < m.batches.size(); ++b) {
            encode_batch(cont, m.v.data() + at, m.batches[b], form, true);
            at += m.batches[b];
        }
        finish_packet(cont);
        out.cont = std::move(cont.b);
    }
    if (out.first.size() / 16 - 1 > 0xFFFF || out.cont.size() / 16 > 0x10000) return fail(why, "a packet over 65535 quadwords");
    return true;
}

bool car_strip_mesh(const std::vector<CarVertex>& verts, const std::vector<std::uint32_t>& tri_in, CarMesh& out, std::string* why) {
    out.v.clear();
    out.batches.clear();
    out.tri.clear();
    if (tri_in.size() % 3) return fail(why, "the index count is not a multiple of 3");
    // unique vertices (the same stored fields: one vertex), so strips run across the model's split vertices
    std::unordered_map<VKey, std::uint32_t, VKeyHash> uid;
    std::vector<CarVertex> U;
    std::vector<std::uint32_t> remap(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        auto it = uid.emplace(vkey(verts[i]), std::uint32_t(U.size()));
        if (it.second) { U.push_back(verts[i]); U.back().w = 0; }
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
    build_strips(T, kCarMaxBatch, seq, out.batches);
    for (const auto& [vi, adc] : seq) {
        CarVertex x = U[vi];
        x.w = adc ? 0x8000u : 0u;
        out.v.push_back(x);
    }
    if (out.v.empty()) {   // no triangles: one batch that draws nothing (every vertex ADC)
        CarVertex z = verts.empty() ? CarVertex{} : verts[0];
        z.w = 0x8000u;
        out.v.assign(3, z);
        out.batches = {3};
    }
    // the triangles as car_read will give them
    std::size_t base = 0;
    for (std::uint32_t n : out.batches) {
        std::vector<bool> adc(n);
        for (std::uint32_t i = 0; i < n; ++i) adc[i] = out.v[base + i].w & 0x8000u;
        b3asset::strip_to_triangles(std::uint32_t(base), adc, out.tri);
        base += n;
    }
    return true;
}

bool car_import_meshes(const Buf& original, const std::vector<CarImportMesh>& meshes, const std::vector<b3asset::Image>& paints,
                       const CarImportOptions& opt, Buf& out, CarImportReport* rep_out, std::string* why) {
    CarImportReport rep;
    CarFile car;
    if (!car_read(original, car, why)) return false;
    rep.meshes = car.meshes.size();
    rep.size_before = original.size();
    std::map<std::tuple<int, int, int>, std::size_t> by_key;
    for (std::size_t i = 0; i < car.meshes.size(); ++i) by_key[{car.meshes[i].lod, car.meshes[i].slot, car.meshes[i].entry}] = i;
    auto mname = [&](const CarMesh& m) {
        char t[48];
        if (m.lod < 0) std::snprintf(t, sizeof t, "%s_e%02d", car_slot_name(m.slot), m.entry);
        else std::snprintf(t, sizeof t, "LOD%d_%s_e%02d", m.lod, car_slot_name(m.slot), m.entry);
        return std::string(t);
    };
    // the influence zones the file uses (an influence is weight << 8 | zone << 2)
    int max_zone = 0;
    for (const CarMesh& m : car.meshes)
        if (car_slot_form(m.slot) == kCarFull)
            for (const CarVertex& v : m.v) max_zone = std::max({max_zone, (v.infl[0] >> 2) & 63, (v.infl[1] >> 2) & 63});
    std::vector<const CarImportMesh*> given(car.meshes.size(), nullptr);
    for (const CarImportMesh& im : meshes) {
        auto it = by_key.find({im.lod, im.slot, im.entry});
        char t[64];
        std::snprintf(t, sizeof t, "LOD%d_%s_e%02d", im.lod, car_slot_name(im.slot), im.entry);
        const std::string nm = im.name.empty() ? std::string(t) : im.name;
        if (it == by_key.end()) return fail(why, nm + ": the file has no such mesh (" + t + "): the import changes the meshes the file has, it cannot add any");
        if (given[it->second]) return fail(why, nm + ": given twice");
        given[it->second] = &im;
        const bool uvo = car_slot_form(im.slot) != kCarFull;
        for (const CarVertex& v : im.v) {
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(v.pos[c]) || std::fabs(v.pos[c]) > 1000.0f) return fail(why, nm + ": a vertex position outside +-1000 m (or not a number)");
            if (!uvo)
                for (int k = 0; k < 2; ++k)
                    if ((v.infl[k] & 3) || int((v.infl[k] >> 2) & 63) > max_zone)
                        return fail(why, nm + ": influence " + hex32(v.infl[k]) + " is not one the file uses (zone " + std::to_string((v.infl[k] >> 2) & 63) +
                                             ", the file's zones are 0.." + std::to_string(max_zone) + ")");
        }
        for (std::uint32_t x : im.tri)
            if (x >= im.v.size()) return fail(why, nm + ": an index past its vertices");
        if (im.tri.size() % 3) return fail(why, nm + ": the index count is not a multiple of 3");
    }
    // the new streams
    struct Job { std::size_t mesh; CarMesh nm; CarPackets pk; std::size_t entry_at; };
    std::vector<Job> jobs;
    for (std::size_t i = 0; i < car.meshes.size(); ++i) {
        const CarMesh& om = car.meshes[i];
        const CarImportMesh* im = given[i];
        if (!im) { ++rep.missing; continue; }
        const int form = car_slot_form(om.slot);
        Job j;
        j.mesh = i;
        j.entry_at = om.list_at + rd32(original, om.list_at + 4) + 16 * std::size_t(om.entry);
        // the same triangles (in any order, any vertex sharing: a tool's re-save) keep the file's stream, with the
        // model's normals where they turned; else the same index list keeps the strips with the new vertex fields
        const bool same_set = !opt.restrip && full_tri_hash(im->v, im->tri, form, false) == full_tri_hash(om.v, om.tri, form, false);
        if (same_set) {
            j.nm = om;
            std::unordered_map<VKey, std::vector<const CarVertex*>, VKeyHash> by_key;
            for (const CarVertex& x : im->v) {
                CarVertex y = x;
                car_form_mask(form, y);
                y.nrm[0] = y.nrm[1] = y.nrm[2] = 0;
                by_key[vkey(y)].push_back(&x);
            }
            bool diff = false;
            for (CarVertex& d : j.nm.v) {
                CarVertex y = d;
                car_form_mask(form, y);
                y.nrm[0] = y.nrm[1] = y.nrm[2] = 0;
                auto it = by_key.find(vkey(y));
                if (form == kCarPositions || it == by_key.end()) continue;
                // the model's vertex at this place whose normal still points this one's way keeps the stored bytes
                bool close = false;
                for (const CarVertex* c : it->second) close = close || normal_close(c->nrm, d.nrm);
                if (!close) {
                    std::memcpy(d.nrm, it->second.front()->nrm, 3);
                    diff = true;
                }
            }
            ++(diff ? rep.edited : rep.unchanged);
        } else if (!opt.restrip && !im->raw.empty() &&
                   full_tri_hash(im->raw, im->tri, form, false) == full_tri_hash(om.v, om.tri, form, false)) {
            // the object moved / turned / scaled as a whole, its mesh the file's: the file's stream, transformed
            j.nm = om;
            const auto& T = im->xform;
            const auto& N = im->nxform;
            for (CarVertex& d : j.nm.v) {
                const double p[3] = {d.pos[0], d.pos[1], d.pos[2]};
                for (int c = 0; c < 3; ++c)
                    d.pos[c] = float(T[std::size_t(c)] * p[0] + T[std::size_t(4 + c)] * p[1] + T[std::size_t(8 + c)] * p[2] + T[std::size_t(12 + c)]);
                if (form == kCarPositions) continue;
                double n[3], q[3], l = 0;
                for (int c = 0; c < 3; ++c) n[c] = (double(d.nrm[c]) - 128.0) / 128.0;
                for (int c = 0; c < 3; ++c) { q[c] = N[std::size_t(c)] * n[0] + N[std::size_t(4 + c)] * n[1] + N[std::size_t(8 + c)] * n[2]; l += q[c] * q[c]; }
                l = std::sqrt(l);
                if (l < 1e-9) continue;
                std::uint8_t b[3];
                for (int c = 0; c < 3; ++c) b[c] = std::uint8_t(std::clamp(std::lround(128.0 + 127.0 * q[c] / l), 0L, 255L));
                if (!normal_close(b, d.nrm)) std::memcpy(d.nrm, b, 3);
            }
            ++rep.edited;
        } else if (!opt.restrip && im->v.size() == om.v.size() && im->tri == om.tri) {
            j.nm = om;   // the same stream: its strips and batches, the new vertex fields
            bool diff = false;
            // a vertex no triangle uses (an ADC filler) follows its drawn twin (the same stored fields in the file),
            // whatever order a tool kept such vertices in
            std::vector<char> drawn(om.v.size(), 0);
            for (std::uint32_t x : om.tri) drawn[x] = 1;
            std::unordered_map<VKey, std::size_t, VKeyHash> twin;
            for (std::size_t k = 0; k < om.v.size(); ++k)
                if (drawn[k]) twin.emplace(vkey(om.v[k]), k);
            for (std::size_t k = 0; k < om.v.size(); ++k) {
                CarVertex& d = j.nm.v[k];
                std::size_t from = k;
                if (!drawn[k])
                    if (auto it = twin.find(vkey(om.v[k])); it != twin.end()) from = it->second;
                const CarVertex& s = im->v[from];
                std::memcpy(d.pos, s.pos, 12);
                std::memcpy(d.uv, s.uv, 4);
                // a normal that a tool re-made but that still points the same way keeps its stored bytes
                if (!normal_close(s.nrm, om.v[from].nrm)) std::memcpy(d.nrm, s.nrm, 3);
                std::memcpy(d.infl, s.infl, 4);
                car_form_mask(form, d);
                diff = diff || !(vkey(d) == vkey(om.v[k]));
            }
            ++(diff ? rep.edited : rep.unchanged);
        } else {
            std::vector<CarVertex> vv = im->v;
            for (CarVertex& x : vv) car_form_mask(form, x);
            j.nm.lod = om.lod;
            j.nm.slot = om.slot;
            j.nm.entry = om.entry;
            if (!car_strip_mesh(vv, im->tri, j.nm, why)) return fail(why, mname(om) + ": " + *why);
            ++rep.restripped;
        }
        if (!car_encode_mesh(j.nm, form, j.pk, why)) return fail(why, mname(om) + ": " + *why);
        rep.vertices += j.nm.v.size();
        rep.triangles += j.nm.tri.size() / 3;
        jobs.push_back(std::move(j));
    }
    // placement. When every rewritten packet fits its old place, each stays there (an unedited one is the same bytes,
    // so an unedited import is the file). When one does not, the packets that move are packed largest first into the
    // freed places (16-byte aligned, after their entry; best fit; freed places with only alignment padding between
    // them joined), the rest appended; the entries' offsets follow.
    Buf work = original;
    auto span_of = [&](std::uint32_t at) -> std::pair<std::size_t, std::size_t> {
        if (!at) return {0, 0};
        return {at, at + 16 + 16 * std::size_t(rd32(original, at) & 0xFFFF)};
    };
    auto entry_of = [&](const CarMesh& m) { return m.list_at + rd32(original, m.list_at + 4) + 16 * std::size_t(m.entry); };
    std::map<std::size_t, int> refs;   // packet -> entries that point at it
    for (const CarMesh& m : car.meshes) { ++refs[m.mesh_at]; if (m.cont_at) ++refs[m.cont_at]; }
    std::vector<const Job*> job_of(car.meshes.size(), nullptr);
    for (const Job& j : jobs) job_of[j.mesh] = &j;
    auto same_bytes = [&](const Buf& nb, std::pair<std::size_t, std::size_t> sp) {
        return nb.size() == sp.second - sp.first && std::equal(nb.begin(), nb.end(), original.begin() + std::ptrdiff_t(sp.first));
    };
    bool fits = true;
    for (const Job& j : jobs) {
        const CarMesh& om = car.meshes[j.mesh];
        for (int part = 0; part < 2; ++part) {
            const std::uint32_t at = part ? om.cont_at : om.mesh_at;
            const Buf& nb = part ? j.pk.cont : j.pk.first;
            const auto sp = span_of(at);
            if (!at) { fits = fits && nb.empty(); continue; }
            if (same_bytes(nb, sp)) continue;
            fits = fits && refs[at] == 1 && nb.size() <= sp.second - sp.first;
        }
    }
    if (fits) {
        for (const Job& j : jobs) {
            const CarMesh& om = car.meshes[j.mesh];
            for (int part = 0; part < 2; ++part) {
                const std::uint32_t at = part ? om.cont_at : om.mesh_at;
                const Buf& nb = part ? j.pk.cont : j.pk.first;
                const auto sp = span_of(at);
                if (!at || same_bytes(nb, sp)) continue;
                std::copy(nb.begin(), nb.end(), work.begin() + std::ptrdiff_t(sp.first));
                std::fill(work.begin() + std::ptrdiff_t(sp.first + nb.size()), work.begin() + std::ptrdiff_t(sp.second), 0);
                if (nb.empty()) wr32(work, j.entry_at + 4 * std::size_t(part), 0);
            }
        }
    } else {
        // two layouts, the smaller kept: (a) only the packets that do not fit their place move, into the space freed
        // by them or appended; (b) every packet no other entry shares is laid out again (see above)
        auto relayout = [&](bool all, std::size_t& moved) {
            Buf w = original;
            moved = 0;
            struct Pend { std::size_t entry_at; int word; std::uint32_t old_at; Buf bytes; int group; };
            std::vector<Pend> pend;
            std::vector<std::pair<std::size_t, std::size_t>> holes;
            for (std::size_t i = 0; i < car.meshes.size(); ++i) {
                const CarMesh& m = car.meshes[i];
                const Job* j = job_of[i];
                if (!all && !j) continue;   // (a): an entry the model does not change stays as it is
                for (int part = 0; part < 2; ++part) {
                    const std::uint32_t at = part ? m.cont_at : m.mesh_at;
                    const auto sp = span_of(at);
                    Buf nb = j ? (part ? j->pk.cont : j->pk.first) : Buf(original.begin() + std::ptrdiff_t(sp.first), original.begin() + std::ptrdiff_t(sp.second));
                    if (at && refs[at] > 1) {   // shared with another entry: stays; a changed copy goes elsewhere
                        if (same_bytes(nb, sp)) continue;
                    } else if (at) {
                        if (!all && same_bytes(nb, sp)) continue;
                        if (!all && !nb.empty() && nb.size() <= sp.second - sp.first) {
                            std::copy(nb.begin(), nb.end(), w.begin() + std::ptrdiff_t(sp.first));
                            std::fill(w.begin() + std::ptrdiff_t(sp.first + nb.size()), w.begin() + std::ptrdiff_t(sp.second), 0);
                            if (sp.first + nb.size() < sp.second) holes.push_back({sp.first + nb.size(), sp.second});
                            continue;
                        }
                        std::fill(w.begin() + std::ptrdiff_t(sp.first), w.begin() + std::ptrdiff_t(sp.second), 0);
                        holes.push_back(sp);
                    }
                    if (nb.empty()) wr32(w, entry_of(m) + 4 * std::size_t(part), 0);
                    else pend.push_back({entry_of(m), part, at, std::move(nb), m.lod < 0 ? 99 : m.lod});
                }
            }
            std::sort(holes.begin(), holes.end());
            std::vector<std::pair<std::size_t, std::size_t>> merged;
            // two freed places with only zero bytes (the files' alignment padding, under 0x100) between them: one place
            auto zero_gap = [&](std::size_t x, std::size_t y) {
                return y >= x && y - x < 0x100 && std::all_of(original.begin() + std::ptrdiff_t(x), original.begin() + std::ptrdiff_t(y), [](std::uint8_t c) { return c == 0; });
            };
            for (const auto& h : holes) {
                if (!merged.empty() && (h.first <= merged.back().second || zero_gap(merged.back().second, h.first)))
                    merged.back().second = std::max(merged.back().second, h.second);
                else merged.push_back(h);
            }
            // the most constrained first (a packet goes after its entry: the later LODs' lists sit later in the file),
            // then the largest
            std::stable_sort(pend.begin(), pend.end(), [](const Pend& x, const Pend& y) {
                return x.group != y.group ? x.group > y.group : x.bytes.size() > y.bytes.size();
            });
            for (const Pend& p : pend) {
                const std::size_t sz = p.bytes.size();
                std::size_t best = merged.size(), place = 0, left = 0;
                for (std::size_t h = 0; h < merged.size(); ++h) {
                    const std::size_t at = (merged[h].first + 15) & ~std::size_t(15);
                    if (at <= p.entry_at || at + sz > merged[h].second) continue;
                    const std::size_t l = merged[h].second - at - sz;
                    if (best == merged.size() || l < left) { best = h; place = at; left = l; }
                }
                if (best < merged.size()) {
                    const auto h = merged[best];
                    merged.erase(merged.begin() + std::ptrdiff_t(best));
                    if (place > h.first) merged.push_back({h.first, place});
                    if (place + sz < h.second) merged.push_back({place + sz, h.second});
                    std::sort(merged.begin(), merged.end());
                } else {
                    place = (w.size() + 15) & ~std::size_t(15);
                    w.resize(place + sz, 0);
                }
                std::copy(p.bytes.begin(), p.bytes.end(), w.begin() + std::ptrdiff_t(place));
                wr32(w, p.entry_at + 4 * std::size_t(p.word), std::uint32_t(place - p.entry_at));
                moved += place != p.old_at;
            }
            if (w.size() % 16) w.resize((w.size() + 15) & ~std::size_t(15), 0);
            return w;
        };
        std::size_t ma = 0, mb = 0;
        Buf la = relayout(false, ma), lb = relayout(true, mb);
        if (lb.size() < la.size()) { work = std::move(lb); rep.moved = mb; }
        else { work = std::move(la); rep.moved = ma; }
    }
    if (work.size() % 16) work.resize((work.size() + 15) & ~std::size_t(15), 0);
    const std::size_t limit = opt.max_size ? opt.max_size : car.kind == 3 ? kCarMaxBgvSize : original.size();
    if (work.size() > limit) {
        char m[300];
        std::snprintf(m, sizeof m, "the new file would be %zu bytes, over the limit of %zu (%s); the game cannot load it. Use fewer vertices "
                                   "(%llu vertices in the rewritten meshes now), or leave out LODs you did not mean to change",
                      work.size(), limit, car.kind == 3 ? "memory block 1, which a player car is loaded into" : "a traffic vehicle's file may not grow",
                      (unsigned long long)rep.vertices);
        return fail(why, m);
    }
    if (work.size() != original.size()) wr32(work, 8, std::uint32_t(work.size()));
    // the paints
    if (!paints.empty() && car.tex_at) {
        PaintEncodeResult pr;
        if (!encode_raster_paints(work, car.tex_at, paints, &pr, why)) return false;
        rep.paints_changed = pr.changed;
        rep.new_palette_indices = pr.new_indices;
        for (const std::string& n : pr.notes) rep.notes.push_back(n);
    } else if (!paints.empty()) {
        rep.notes.push_back("the file has no texture atlas: the paint pictures are not used");
    }
    // read back: every rewritten entry must decode to its new stream, every other one as before
    CarFile again;
    if (!car_read(work, again, why)) return fail(why, "the new file does not read back: " + *why);
    if (again.meshes.size() != car.meshes.size()) return fail(why, "the new file does not read back with the same meshes");
    std::vector<const CarMesh*> want(car.meshes.size());
    for (std::size_t i = 0; i < car.meshes.size(); ++i) want[i] = &car.meshes[i];
    for (const Job& j : jobs) want[j.mesh] = &j.nm;
    for (std::size_t i = 0; i < car.meshes.size(); ++i) {
        const CarMesh& a = again.meshes[i];
        const CarMesh& b = *want[i];
        bool same = a.v.size() == b.v.size() && a.batches == b.batches && a.tri == b.tri;
        const int form = car_slot_form(b.slot);
        for (std::size_t k = 0; same && k < a.v.size(); ++k) {
            CarVertex x = a.v[k], y = b.v[k];
            car_form_mask(form, x);
            car_form_mask(form, y);
            same = vkey(x) == vkey(y) && x.w == y.w;
        }
        if (!same) return fail(why, mname(b) + ": the written packets do not read back as meant (a writer error; nothing written)");
    }
    rep.size_after = work.size();
    out = std::move(work);
    if (rep_out) *rep_out = std::move(rep);
    return true;
}

// ---- the model ----

namespace {

// "LOD3_L0_e00", "LOD3_B0_w2_e00", "X64_e01" (a modelling tool's ".001" ignored)
bool parse_key(std::string s, int& lod, int& slot, int& entry, int& wheel) {
    if (const std::size_t dot = s.rfind('.'); dot != std::string::npos && dot + 1 < s.size() &&
                                                   s.find_first_not_of("0123456789", dot + 1) == std::string::npos)
        s.resize(dot);
    lod = -1;
    wheel = -1;
    std::size_t p = 0;
    if (s.size() > 5 && s.compare(0, 3, "LOD") == 0 && s[3] >= '0' && s[3] <= '9' && s[4] == '_') { lod = s[3] - '0'; p = 5; }
    const std::size_t us = s.find('_', p);
    if (us == std::string::npos) return false;
    const std::string sn = s.substr(p, us - p);
    slot = -1;
    for (int i = 0; i < 20; ++i)
        if (sn == car_slot_name(i)) slot = i;
    if (slot < 0) return false;
    p = us + 1;
    if (s.size() > p + 2 && s[p] == 'w' && s[p + 1] >= '0' && s[p + 1] <= '9' && s[p + 2] == '_') { wheel = s[p + 1] - '0'; p += 3; }
    if (s.size() != p + 3 || s[p] != 'e' || !std::isdigit(static_cast<unsigned char>(s[p + 1])) || !std::isdigit(static_cast<unsigned char>(s[p + 2]))) return false;
    entry = (s[p + 1] - '0') * 10 + (s[p + 2] - '0');
    if ((slot >= 18) != (lod < 0)) return false;
    return true;
}

}  // namespace

bool car_model_read(const FileLoader& load, const std::string& model, const CarFile& car, std::vector<CarImportMesh>& out,
                    std::vector<std::string>* notes, std::string* why) {
    out.clear();
    Json doc;
    Buf bin;
    if (!gltf_load_doc(load, model, doc, bin, why)) return false;
    const Json* nodes = doc.get("nodes");
    const Json* meshes = doc.get("meshes");
    if (!nodes || !meshes) return fail(why, model + ": no nodes or meshes");
    std::map<std::tuple<int, int, int>, const CarMesh*> known;
    for (const CarMesh& m : car.meshes) known[{m.lod, m.slot, m.entry}] = &m;
    // world matrices from the scene's roots (every node reached once)
    std::vector<M4> world;
    std::vector<int> seen;
    if (!gltf_world(doc, world, seen, why)) return fail(why, model + ": " + *why);
    // which entry each mesh is: its extras, else its name, else its node's name
    struct Cand { int mesh, node, wheel; bool exact; };
    std::map<std::tuple<int, int, int>, std::vector<Cand>> cands;
    for (std::size_t ni = 0; ni < nodes->size(); ++ni) {
        const Json& n = nodes->a[ni];
        const Json* mj = n.get("mesh");
        if (!mj || !seen[ni]) continue;
        const int mi = int(mj->as_int(-1));
        if (mi < 0 || std::size_t(mi) >= meshes->size()) return fail(why, model + ": a node names a mesh that does not exist");
        const Json& m = meshes->a[std::size_t(mi)];
        const std::string mname = m.get("name") ? m.get("name")->s : "", nname = n.get("name") ? n.get("name")->s : "";
        int lod = 0, slot = -1, entry = 0, wheel = -1, w2 = -1, l2 = 0, s2 = -1, e2 = 0;
        bool ok = false;
        const Json* ex = m.get("extras");
        const Json* b3 = ex ? ex->get("b3") : nullptr;
        if (b3 && b3->get("lod") && b3->get("slot") && b3->get("entry")) {
            lod = int(b3->get("lod")->as_int());
            entry = int(b3->get("entry")->as_int());
            for (int i = 0; i < 20; ++i)
                if (b3->get("slot")->s == car_slot_name(i)) slot = i;
            ok = slot >= 0;
        }
        if (!ok) ok = parse_key(mname, lod, slot, entry, wheel);
        const bool node_ok = parse_key(nname, l2, s2, e2, w2);
        if (!ok && node_ok) { lod = l2; slot = s2; entry = e2; ok = true; }
        if (node_ok && l2 == lod && s2 == slot && e2 == entry) wheel = w2;
        if (const Json* nex = n.get("extras"); nex && nex->get("b3") && nex->get("b3")->get("wheel")) wheel = int(nex->get("b3")->get("wheel")->as_int());
        const std::string shown = nname.empty() ? mname : nname;
        if (!ok) return fail(why, "\"" + shown + "\" is not one of the car's meshes (names like LOD3_L0_e00): the import changes the meshes the file has "
                                  "and cannot add objects; join new parts into one of them");
        if (!known.count({lod, slot, entry})) {
            char t[48];
            std::snprintf(t, sizeof t, "LOD%d_%s_e%02d", lod, car_slot_name(slot), entry);
            return fail(why, "\"" + shown + "\" names " + t + ", which this car's file does not have");
        }
        const bool exact = mname.find('.') == std::string::npos;
        cands[{lod, slot, entry}].push_back({mi, int(ni), wheel, exact});
    }
    std::vector<std::string> moved_objects, no_infl, lost_nrm_meshes;
    std::size_t odd_infl = 0;
    // the stored fields are whole numbers (bytes, words) in their range; others were changed on the way
    auto whole = [](const double* v, int n, double hi) {
        for (int i = 0; i < n; ++i)
            if (!(v[i] > -1e-3 && v[i] < hi + 1e-3) || std::fabs(v[i] - std::nearbyint(v[i])) > 1e-3) return false;
        return true;
    };
    // the influence zones the file uses (as car_import_meshes checks them)
    int max_zone = 0;
    for (const CarMesh& m : car.meshes)
        if (car_slot_form(m.slot) == kCarFull)
            for (const CarVertex& v : m.v) max_zone = std::max({max_zone, (v.infl[0] >> 2) & 63, (v.infl[1] >> 2) & 63});
    for (auto& [key, cs] : cands) {
        const CarMesh& om = *known[key];
        // one mesh per entry: a mesh shared by several nodes (the wheels) is read once, at its first wheel's node
        const Cand* pick = nullptr;
        for (const Cand& c : cs)
            if (!pick || (c.mesh == pick->mesh ? (c.wheel >= 0 && (pick->wheel < 0 || c.wheel < pick->wheel)) : (c.exact && !pick->exact)))
                pick = &c;
        for (const Cand& c : cs)
            if (c.mesh != pick->mesh && c.exact == pick->exact) {
                char t[48];
                std::snprintf(t, sizeof t, "LOD%d_%s_e%02d", om.lod, car_slot_name(om.slot), om.entry);
                return fail(why, std::string("two different meshes stand for ") + t + " (joined copies of a wheel? keep one mesh per entry)");
            }
        const Json& n = nodes->a[std::size_t(pick->node)];
        const Json& m = meshes->a[std::size_t(pick->mesh)];
        CarImportMesh im;
        im.lod = om.lod;
        im.slot = om.slot;
        im.entry = om.entry;
        im.name = n.get("name") ? n.get("name")->s : (m.get("name") ? m.get("name")->s : "");
        // the part's frame as the export placed it: the mesh's positions are brought back into that frame
        std::array<float, 16> idf{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        M4 F = m4(idf);
        if (om.slot >= 1 && om.slot <= 6 && om.slot - 1 < car.panels) F = m4(gltf_matrix(car.panel_frames[std::size_t(om.slot - 1)].data()));
        else if (om.slot >= 7 && om.slot <= 9 && car.wheels) F = m4(gltf_matrix(car.wheel_frames[std::size_t(std::clamp(pick->wheel, 0, car.wheels - 1))].data()));
        M4 Fi;
        if (!m4_inverse(F, Fi)) return fail(why, im.name + ": the file's part frame cannot be inverted");
        M4 T = m4_mul(Fi, world[std::size_t(pick->node)]);
        const bool ident = m4_near_identity(T);
        M4 N{};   // normals: the inverse transpose of T's 3x3
        if (!ident) {
            M4 Ti;
            if (!m4_inverse(T, Ti)) return fail(why, im.name + ": its node's matrix cannot be inverted (scaled to zero?)");
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) N[std::size_t(c * 4 + r)] = Ti[std::size_t(r * 4 + c)];
            moved_objects.push_back(im.name);
        }
        const Json* prims = m.get("primitives");
        if (!prims || !prims->size()) return fail(why, im.name + ": a mesh without primitives");
        std::size_t bad_uv = 0;
        bool have_infl_all = true, lost_nrm = false;
        std::vector<char> infl_given, face_normal;
        std::vector<std::array<float, 3>> rawp;
        for (const Json& p : prims->a) {
            if (p.get("mode") && p.get("mode")->as_int() != gl::kTriangles) return fail(why, im.name + ": a primitive that is not triangles (mode " + std::to_string(p.get("mode")->as_int()) + ")");
            const Json* at = p.get("attributes");
            if (!at || !at->get("POSITION")) return fail(why, im.name + ": a primitive without POSITION");
            if (!at->get("TEXCOORD_0")) return fail(why, im.name + ": no texture coordinates (TEXCOORD_0): every vertex needs its place in the car's texture");
            std::vector<double> pos, nrm, uv, bn, bi, idx;
            int np = 0, nn = 0, nu = 0, nbn = 0, nbi = 0, nx = 0;
            auto acc = [&](const char* k, std::vector<double>& v, int& c) {
                return gltf_read_accessor(doc, bin, int(at->get(k)->as_int()), v, c, why);
            };
            if (!acc("POSITION", pos, np) || np != 3) return fail(why, im.name + ": POSITION: " + (np != 3 ? std::string("not VEC3") : *why));
            if (!acc("TEXCOORD_0", uv, nu) || nu != 2) return fail(why, im.name + ": TEXCOORD_0: " + (nu != 2 ? std::string("not VEC2") : *why));
            if (at->get("NORMAL") && (!acc("NORMAL", nrm, nn) || nn != 3)) return fail(why, im.name + ": NORMAL unreadable");
            // the stored fields as the export wrote them: _B3_NRM_X / _Y / _Z and _B3_INFL_0 / _1, or the one _B3_NRM
            // (VEC4) / _B3_INFL (VEC2) of exports before them (a tool may write them as floats or normalized: both read)
            gltf_read_field(doc, bin, *at, "_B3_NRM", {"X", "Y", "Z"}, 255.0, bn, nbn);
            if (nbn < 3) bn.clear();
            gltf_read_field(doc, bin, *at, "_B3_INFL", {"0", "1"}, 65535.0, bi, nbi);
            if (nbi != 2) bi.clear();
            const std::size_t nv = pos.size() / 3, base = im.v.size();
            if (uv.size() / 2 != nv || (!nrm.empty() && nrm.size() / 3 != nv) || (!bn.empty() && bn.size() / std::size_t(nbn) != nv) || (!bi.empty() && bi.size() / 2 != nv))
                return fail(why, im.name + ": attributes of different lengths");
            // _B3_NRM against NORMAL in the mesh's own space: when most stored normals no longer point where NORMAL
            // does, the attribute is not the export's any more (Blender's importer turns an unsigned byte VEC4 into a
            // byte colour, clamped) or the mesh was reshaped: NORMAL is used
            if (!bn.empty() && !nrm.empty()) {
                std::size_t agree = 0, total = 0;
                for (std::size_t k = 0; k < nv; ++k) {
                    double s[3], n[3], ls = 0, ln = 0, d = 0;
                    for (int c = 0; c < 3; ++c) {
                        s[c] = (std::nearbyint(bn[k * std::size_t(nbn) + std::size_t(c)]) - 128.0) / 128.0;
                        n[c] = nrm[3 * k + std::size_t(c)];
                        ls += s[c] * s[c];
                        ln += n[c] * n[c];
                    }
                    if (ln < 1e-12) continue;
                    if (ls < 1e-12) { s[0] = s[2] = 0; s[1] = 1; ls = 1; }
                    for (int c = 0; c < 3; ++c) d += s[c] * n[c];
                    ++total;
                    agree += d / std::sqrt(ls * ln) > 0.999;
                }
                if (2 * agree < total) { bn.clear(); lost_nrm = true; }
            }
            if (p.get("indices")) {
                if (!gltf_read_accessor(doc, bin, int(p.get("indices")->as_int()), idx, nx, why)) return fail(why, im.name + ": indices: " + *why);
            } else {
                for (std::size_t k = 0; k < nv; ++k) idx.push_back(double(k));
            }
            for (std::size_t k = 0; k < nv; ++k) {
                CarVertex v;
                double pp[3] = {pos[3 * k], pos[3 * k + 1], pos[3 * k + 2]};
                rawp.push_back({float(pp[0]), float(pp[1]), float(pp[2])});
                if (!ident)
                    for (int c = 0; c < 3; ++c) v.pos[c] = float(T[std::size_t(c)] * pp[0] + T[std::size_t(4 + c)] * pp[1] + T[std::size_t(8 + c)] * pp[2] + T[std::size_t(12 + c)]);
                else
                    for (int c = 0; c < 3; ++c) v.pos[c] = float(pp[c]);
                for (int c = 0; c < 2; ++c) {
                    const double t = uv[2 * k + std::size_t(c)] * 4096.0;
                    if (!(t > -0.5 && t < 65535.5)) { ++bad_uv; continue; }
                    v.uv[c] = std::uint16_t(std::lround(std::max(0.0, t)));
                }
                // the normal: the stored bytes while they still point where NORMAL does, else NORMAL quantised
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
                bool used_stored = false;
                if (!bn.empty() && whole(&bn[k * std::size_t(nbn)], 3, 255.0)) {
                    std::uint8_t b[3];
                    double s[3], l = 0;
                    for (int c = 0; c < 3; ++c) {
                        b[c] = std::uint8_t(std::clamp(std::lround(bn[k * std::size_t(nbn) + std::size_t(c)]), 0L, 255L));
                        s[c] = (double(b[c]) - 128.0) / 128.0;
                        l += s[c] * s[c];
                    }
                    l = std::sqrt(l);
                    // the stored normal's direction as the export wrote NORMAL (a zero one as +Y)
                    if (l < 1e-6) { s[0] = 0; s[1] = 1; s[2] = 0; l = 1; }
                    const double dot = have_n ? (s[0] * n3[0] + s[1] * n3[1] + s[2] * n3[2]) / l : 1.0;
                    if (dot > 0.999 && (ident || have_n)) { std::memcpy(v.nrm, b, 3); used_stored = true; }
                }
                if (!used_stored && have_n)
                    for (int c = 0; c < 3; ++c) v.nrm[c] = std::uint8_t(std::clamp(std::lround(128.0 + 127.0 * n3[c]), 0L, 255L));
                face_normal.push_back(!used_stored && !have_n);   // from the faces, below
                // the stored words, when they are ones the file can hold (a tool that interpolates attributes, say
                // for a subdivided face, makes others): else the nearest original vertex's, below
                bool infl_ok = !bi.empty() && whole(&bi[2 * k], 2, 65535.0);
                for (int c = 0; infl_ok && c < 2; ++c) {
                    v.infl[c] = std::uint16_t(std::lround(bi[2 * k + std::size_t(c)]));
                    infl_ok = (v.infl[c] & 3) == 0 && int((v.infl[c] >> 2) & 63) <= max_zone;
                }
                if (infl_ok) {
                    infl_given.push_back(1);
                } else if (!bi.empty()) {
                    ++odd_infl;
                    have_infl_all = false;
                    infl_given.push_back(0);
                } else {
                    have_infl_all = false;
                    infl_given.push_back(0);
                }
                im.v.push_back(v);
            }
            if (idx.size() % 3) return fail(why, im.name + ": the index count is not a multiple of 3");
            for (double x : idx) {
                if (x < 0 || std::size_t(x) >= nv) return fail(why, im.name + ": an index past its vertices");
                im.tri.push_back(std::uint32_t(base + std::size_t(x)));
            }
        }
        if (bad_uv) {
            char t[200];
            std::snprintf(t, sizeof t, "%s: %zu texture coordinates outside 0 .. 16 (the game stores them as 0 .. 65535 / 4096): keep the UVs on the texture",
                          im.name.c_str(), bad_uv);
            return fail(why, t);
        }
        // vertices without a normal: the faces' (area-weighted)
        std::vector<std::array<double, 3>> acc(im.v.size(), {0, 0, 0});
        const bool need = std::find(face_normal.begin(), face_normal.end(), 1) != face_normal.end();
        if (need) {
            for (std::size_t t = 0; t + 2 < im.tri.size(); t += 3) {
                const float* a = im.v[im.tri[t]].pos; const float* b = im.v[im.tri[t + 1]].pos; const float* c = im.v[im.tri[t + 2]].pos;
                const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const double n[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
                for (int q = 0; q < 3; ++q) for (int k = 0; k < 3; ++k) acc[im.tri[t + std::size_t(q)]][std::size_t(k)] += n[k];
            }
            for (std::size_t k = 0; k < im.v.size(); ++k) {
                CarVertex& v = im.v[k];
                if (!face_normal[k]) continue;
                const double l = std::sqrt(acc[k][0] * acc[k][0] + acc[k][1] * acc[k][1] + acc[k][2] * acc[k][2]);
                for (int c = 0; c < 3; ++c) v.nrm[c] = std::uint8_t(std::clamp(std::lround(128.0 + 127.0 * (l > 0 ? acc[k][std::size_t(c)] / l : (c == 1 ? 1.0 : 0.0))), 0L, 255L));
            }
            if (notes) notes->push_back(im.name + ": no normals in the model: made from the faces");
        }
        // influences the model does not carry: the nearest original vertex's (the damage zones follow the old shape)
        if (!have_infl_all && car_slot_form(om.slot) == kCarFull) {
            // by the mesh's own positions (before its object's move: the file's vertices are in that space), and of
            // the original vertices at the same distance the one with the same texture coordinates
            for (std::size_t k = 0; k < im.v.size(); ++k) {
                if (infl_given[k]) continue;
                double bd = -1;
                const CarVertex* best = nullptr;
                const float* p = rawp[k].data();
                for (const CarVertex& o : om.v) {
                    double d = 0;
                    for (int c = 0; c < 3; ++c) d += double(o.pos[c] - p[c]) * double(o.pos[c] - p[c]);
                    const bool same_uv = std::memcmp(o.uv, im.v[k].uv, 4) == 0;
                    if (bd < 0 || d < bd || (d == bd && same_uv && std::memcmp(best->uv, im.v[k].uv, 4) != 0)) { bd = d; best = &o; }
                }
                if (best) std::memcpy(im.v[k].infl, best->infl, 4);
            }
            no_infl.push_back(im.name);
        }
        if (lost_nrm) lost_nrm_meshes.push_back(im.name);
        if (!ident) {
            im.raw = im.v;
            for (std::size_t k = 0; k < im.raw.size(); ++k) std::memcpy(im.raw[k].pos, rawp[k].data(), 12);
            im.xform = T;
            im.nxform = N;
        }
        out.push_back(std::move(im));
    }
    auto some = [](const std::vector<std::string>& v) {
        return std::to_string(v.size()) + " mesh(es) (" + v.front() + (v.size() > 1 ? " .. " + v.back() : std::string()) + ")";
    };
    if (notes && !lost_nrm_meshes.empty())
        notes->push_back(some(lost_nrm_meshes) + ": _B3_NRM_X / _Y / _Z are not the export's stored normals any more (changed by a tool, or the "
                         "mesh reshaped): their normals come from NORMAL");
    if (notes && odd_infl)
        notes->push_back(std::to_string(odd_infl) + " vertices whose _B3_INFL_0 / _1 is not an influence the file can hold (mixed by a tool, e.g. "
                         "on a subdivided face): they take the nearest original vertex's");
    if (notes && !no_infl.empty())
        notes->push_back(some(no_infl) + " without (all of) _B3_INFL_0 / _1: those vertices take the damage influences of the nearest original "
                         "vertex (export the attributes to keep them)");
    if (notes && !moved_objects.empty())
        notes->push_back(std::to_string(moved_objects.size()) + " object(s) moved / turned / scaled (" + moved_objects.front() +
                         (moved_objects.size() > 1 ? " .. " + moved_objects.back() : std::string()) +
                         "): applied to their vertices (the parts' frames stay the file's)");
    return true;
}

bool car_import(const Buf& original, const FileLoader& load, const std::string& model, const std::string& sidecar, const CarImportOptions& opt,
                Buf& out, CarImportReport* rep, std::string* why) {
    Buf sj;
    Json side;
    if (!load(sidecar, sj)) return fail(why, "cannot read the sidecar " + sidecar);
    if (!json_parse(std::string_view(reinterpret_cast<const char*>(sj.data()), sj.size()), side, why)) return fail(why, sidecar + ": " + *why);
    if (!side.get("format") || side.get("format")->s != "b3modtool car export") return fail(why, sidecar + ": not a car export's sidecar");
    const Json* src = side.get("source");
    const std::string path = src && src->get("path") ? src->get("path")->s : "?";
    if (!src || !src->get("size") || !src->get("fnv1a64") || std::size_t(src->get("size")->as_int()) != original.size() ||
        src->get("fnv1a64")->s != hex64(b3mod::fnv1a64(original.data(), original.size())))
        return fail(why, "the export was made from another " + path + " than this one (its size and hash differ): export the car again from this disc");
    CarFile car;
    if (!car_read(original, car, why)) return false;
    std::vector<CarImportMesh> meshes;
    std::vector<std::string> notes;
    if (!car_model_read(load, model, car, meshes, &notes, why)) return false;
    std::vector<b3asset::Image> paints;
    if (const Json* tex = side.get("texture"); tex && tex->get("files")) {
        for (const Json& f : tex->get("files")->a) {
            b3asset::Image im;
            Buf png;
            if (load(path_dir_of(sidecar) + f.s, png)) {
                std::string e;
                if (!decode_png(png, im.w, im.h, im.rgba, &e)) return fail(why, f.s + ": " + e);
            } else {
                notes.push_back(f.s + " is not there: that paint stays as it is");
            }
            paints.push_back(std::move(im));
        }
    }
    CarImportReport r;
    if (!car_import_meshes(original, meshes, paints, opt, out, &r, why)) return false;
    notes.insert(notes.end(), r.notes.begin(), r.notes.end());
    r.notes = std::move(notes);
    if (rep) *rep = std::move(r);
    return true;
}

}  // namespace b3modtool
