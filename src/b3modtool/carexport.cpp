// Car export: PVEH/<class>/CARn.BGV / .BTV to glTF 2.0 or OBJ (include/b3modtool/export3d.h; the layout
// docs/modding/file-formats/car.md, the same walk as b3asset::read_car with the stored lanes kept).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>

#include "b3asset/car.h"
#include "b3asset/gs_memory.h"
#include "b3asset/model.h"
#include "b3asset/raster.h"
#include "b3asset/vif.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"

namespace b3modtool {

namespace {

using b3asset::Bytes;

bool fail(std::string* why, const std::string& m) { if (why) *why = m; return false; }

const char* kSlot[20] = {"L0", "A0", "A1", "A2", "A3", "A4", "A5", "B0", "B1", "B2",
                         "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "X64", "X68"};

// Light groups (vehicle data +0x1664[k], count +0x16AC[k]) by RenderLightCoronas' use [C asm 0x256690; meaning G by
// colour, docs/native-renderer.md §16.2]; group 8 (+0x1684) holds the exhaust items of the boost glow.
const char* light_group_name(int g) {
    switch (g) {
    case 0: return "head";
    case 1: return "tail";
    case 2: return "brake";
    case 4: return "reverse";
    case 5: return "indicator_a";
    case 6: return "indicator_b";
    case 8: return "exhaust";
    default: return nullptr;
    }
}

std::string hex32(std::uint32_t v) {
    char t[16];
    std::snprintf(t, sizeof t, "0x%08X", v);
    return t;
}

void emit_batch(const b3asset::VuMemory& vu, std::uint32_t n, CarMesh& m) {
    if (!n) return;
    const std::uint32_t base = std::uint32_t(m.v.size());
    std::vector<bool> adc(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        CarVertex v;
        for (int c = 0; c < 3; ++c) v.pos[c] = vu.f(3 * i, c);
        v.w = vu.qw(3 * i)[3];
        adc[i] = v.w & 0x8000;
        for (int c = 0; c < 3; ++c) v.nrm[c] = std::uint8_t((vu.qw(3 * i + 2)[c] - 0x47800000u) & 0xFF);
        // UVs and influences interleaved per group of four vertices (b3asset::read_car)
        const std::uint32_t g = i & ~3u, j = i & 3;
        const std::uint32_t* uvq = vu.qw(3 * (g + ((j & 1) ? 2 : 0)) + 1);
        v.uv[0] = std::uint16_t(uvq[(j & 2) ? 2 : 0] & 0xFFFF);
        v.uv[1] = std::uint16_t(uvq[(j & 2) ? 3 : 1] & 0xFFFF);
        v.infl[0] = std::uint16_t(vu.qw(3 * (g + 1) + 1)[j]);
        v.infl[1] = std::uint16_t(vu.qw(3 * (g + 3) + 1)[j]);
        m.v.push_back(v);
    }
    b3asset::strip_to_triangles(base, adc, m.tri);
    m.batches.push_back(n);
}

bool run_packet(Bytes f, std::size_t at, b3asset::VifInterpreter& vif, const b3asset::VifInterpreter::KickFn& kick, std::string* why) {
    const std::uint32_t tag = f.u32(at);
    if (!f.has(at, 16) || (tag & 0xFFFF0000u) != 0x60000000u) return fail(why, "mesh at " + hex32(std::uint32_t(at)) + " is not a DMA RET packet");
    const std::size_t qwc = tag & 0xFFFF;
    if (!f.has(at, 16 + qwc * 16)) return fail(why, "mesh at " + hex32(std::uint32_t(at)) + " outside the file");
    std::string err;
    if (!vif.run(f.p + at + 8, 8 + qwc * 16, kick, &err, 8)) return fail(why, "mesh VIF: " + err);
    return true;
}

bool read_list(Bytes f, std::size_t lst, int lod, int slot, CarFile& out, std::string* why) {
    if (!f.has(lst, 12)) return fail(why, "list outside the file");
    const int count = std::int8_t(f.p[lst]);
    const int first_glass = std::int8_t(f.p[lst + 1]);
    const std::size_t entries = lst + f.u32(lst + 4);
    b3asset::VifInterpreter vif;
    for (int i = 0; i < count; ++i) {
        const std::size_t e = entries + 16 * std::size_t(i);
        if (!f.has(e, 16)) return fail(why, "list entry outside the file");
        CarMesh m;
        m.lod = lod;
        m.slot = slot;
        m.entry = i;
        m.list_at = std::uint32_t(lst);
        m.mesh_at = std::uint32_t(e + f.u32(e));
        m.cont_at = f.u32(e + 4) ? std::uint32_t(e + f.u32(e + 4)) : 0;
        m.material = f.u32(e + 8);
        m.colour = f.u32(e + 12);
        m.glass_entry = first_glass;
        const auto kick = [&](const b3asset::VuMemory& vu, std::uint32_t itop, b3asset::VifInterpreter::KickKind, std::uint32_t) {
            emit_batch(vu, itop, m);
        };
        // as b3asset::read_car: the first packet's last batch is kicked by the flush, the continuation keeps the state
        vif.reset();
        if (!run_packet(f, m.mesh_at, vif, kick, why)) return false;
        vif.flush(kick);
        if (m.cont_at && !run_packet(f, m.cont_at, vif, kick, why)) return false;
        vif.flush(kick);
        out.meshes.push_back(std::move(m));
    }
    return true;
}

bool is_identity(const std::array<float, 16>& m) {
    static const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (int i = 0; i < 16; ++i)
        if (m[std::size_t(i)] != id[i]) return false;
    return true;
}

std::array<float, 3> unit_normal(const CarVertex& v) {
    float n[3];
    for (int c = 0; c < 3; ++c) n[c] = (float(v.nrm[c]) - 128.0f) / 128.0f;
    const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l < 1e-6f) return {0.0f, 1.0f, 0.0f};
    return {n[0] / l, n[1] / l, n[2] / l};
}

std::uint64_t mix64(std::uint64_t x) {   // splitmix64's finaliser
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// What kind of mesh an entry is, from its material word (docs/car-shading.md §3: mask bits 0x04..0x80 lights,
// 0x300 glass) and the list's glass index
enum Kind { kBody, kGlass, kLight };
Kind mesh_kind(const CarMesh& m) {
    const std::uint32_t mask = m.material & 0xFFFF;
    if ((mask & 0x300) || (m.glass_entry >= 0 && m.entry >= m.glass_entry)) return kGlass;
    if (mask & 0xFC) return kLight;
    return kBody;
}

}  // namespace

const char* car_slot_name(int slot) { return slot >= 0 && slot < 20 ? kSlot[slot] : "?"; }

bool car_read(const Buf& file, CarFile& out, std::string* why) {
    out = CarFile{};
    const Bytes f{file.data(), file.size()};
    if (file.size() < 0x16F0) return fail(why, "too short for a vehicle file");
    if (const std::string e = b3asset::car_file_error(f); !e.empty()) return fail(why, e);   // carmods1: 0x15 .. 0x17, not the old layout
    out.version = f.u32(0);
    out.size = f.u32(8);
    out.kind = f.u32(0x10);
    out.panels = f.p[0x0C];
    out.wheels = f.p[0x0D];
    out.f14 = f.f32(0x14);
    out.scale = f.f32(0x18);
    for (int i = 0; i < 6; ++i) out.wheel_factor[i] = f.f32(0x1C + 4 * std::size_t(i));
    for (int i = 0; i < 5; ++i) out.lod_at[i] = f.u32(0x4C + 4 * std::size_t(i));
    out.tex_at = f.u32(0x60);
    out.list64 = f.u32(0x64);
    out.list68 = f.u32(0x68);
    if (out.panels > 6 || out.wheels > 6) return fail(why, "more than six panel or wheel frames");
    auto mat = [&](std::size_t at) {
        std::array<float, 16> m{};
        for (int i = 0; i < 16; ++i) m[std::size_t(i)] = f.f32(at + 4 * std::size_t(i));
        return m;
    };
    for (int w = 0; w < out.wheels; ++w) out.wheel_frames.push_back(mat(0xB80 + 64 * std::size_t(w)));
    for (int p = 0; p < out.panels; ++p) out.panel_frames.push_back(mat(0xD00 + 64 * std::size_t(p)));
    for (int lod = 0; lod < 5; ++lod) {
        const std::size_t block = out.lod_at[lod];
        if (!block) continue;
        if (!f.has(block, 18 * 4)) return fail(why, "LOD block outside the file");
        for (int s = 0; s < 18; ++s) {
            const std::uint32_t rel = f.u32(block + 4 * std::size_t(s));
            if (rel && !read_list(f, block + rel, lod, s, out, why)) return false;
        }
    }
    // +0x64 / +0x68: two more lists in the list format [C FixUp 0x220B70]; their use is not known (the port does not draw
    // them); kept when they decode
    for (int k = 0; k < 2; ++k) {
        const std::uint32_t at = k ? out.list68 : out.list64;
        if (!at) continue;
        CarFile tmp;
        std::string err;
        if (read_list(f, at, -1, 18 + k, tmp, &err)) for (CarMesh& m : tmp.meshes) out.meshes.push_back(std::move(m));
        else out.notes.push_back(std::string("the list at +0x") + (k ? "68" : "64") + " did not decode: " + err);
    }
    for (int g = 0; g < 18; ++g) {
        const std::uint32_t at = f.u32(0x1664 + 4 * std::size_t(g));
        const int n = f.p[0x16AC + g];
        for (int i = 0; at && i < n; ++i) {
            CarLight l;
            l.group = g;
            l.index = i;
            l.at = at + 0x30 * std::uint32_t(i);
            if (!f.has(l.at, 0x30)) return fail(why, "light outside the file");
            for (int q = 0; q < 12; ++q) l.raw[q] = f.f32(l.at + 4 * std::size_t(q));
            out.lights.push_back(l);
        }
    }
    if (out.tex_at && f.has(out.tex_at, 0xA4)) out.palettes = std::max(1, int(f.p[out.tex_at + 0xA0]));
    return true;
}

std::uint64_t triangle_set_hash(const std::vector<std::array<float, 3>>& pos, const std::vector<std::uint8_t>& extra,
                                std::size_t epv, const std::vector<std::uint32_t>& tri) {
    std::uint64_t sum = 0;
    const std::size_t ksz = 12 + epv;
    std::vector<std::uint8_t> key(3 * ksz);
    for (std::size_t t = 0; t + 2 < tri.size(); t += 3) {
        const std::uint32_t k[3] = {tri[t], tri[t + 1], tri[t + 2]};
        if (k[0] >= pos.size() || k[1] >= pos.size() || k[2] >= pos.size()) continue;
        const auto same = [&](std::uint32_t a, std::uint32_t b) { return std::memcmp(pos[a].data(), pos[b].data(), 12) == 0; };
        if (same(k[0], k[1]) || same(k[1], k[2]) || same(k[0], k[2])) continue;   // degenerate: tools drop these
        std::uint8_t one[3][64];
        for (int q = 0; q < 3; ++q) {
            std::memcpy(one[q], pos[k[q]].data(), 12);
            if (epv) std::memcpy(one[q] + 12, extra.data() + std::size_t(k[q]) * epv, epv);
        }
        int first = 0;
        for (int q = 1; q < 3; ++q)
            if (std::memcmp(one[q], one[first], ksz) < 0) first = q;
        for (int q = 0; q < 3; ++q) std::memcpy(key.data() + std::size_t(q) * ksz, one[(first + q) % 3], ksz);
        sum += mix64(b3mod::fnv1a64(key.data(), key.size()));
    }
    return sum;
}

std::uint64_t car_mesh_tri_hash(const CarMesh& m) {
    std::vector<std::array<float, 3>> pos;
    std::vector<std::uint8_t> extra;
    for (const CarVertex& v : m.v) {
        pos.push_back({v.pos[0], v.pos[1], v.pos[2]});
        const std::uint16_t e[4] = {v.uv[0], v.uv[1], v.infl[0], v.infl[1]};
        const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(e);
        extra.insert(extra.end(), p, p + 8);
    }
    return triangle_set_hash(pos, extra, 8, m.tri);
}

std::uint64_t car_mesh_raw_hash(const CarMesh& m) {
    std::uint64_t h = b3mod::fnv1a64(nullptr, 0);
    for (const CarVertex& v : m.v) {
        std::uint8_t rec[31];
        std::memcpy(rec, v.pos, 12);
        std::memcpy(rec + 12, &v.w, 4);
        std::memcpy(rec + 16, v.uv, 4);
        std::memcpy(rec + 20, v.infl, 4);
        std::memcpy(rec + 24, v.nrm, 3);
        h = b3mod::fnv1a64(rec, 27, h);
    }
    for (std::uint32_t b : m.batches) h = b3mod::fnv1a64(&b, 4, h);
    return h;
}

bool car_export(const Buf& file, const std::string& stem, const CarExportOptions& opt, FileSet& out, ExportReport* rep,
                std::string* why) {
    CarFile car;
    if (!car_read(file, car, why)) return false;
    const Bytes f{file.data(), file.size()};
    ExportReport r;
    r.notes = car.notes;

    // The texture atlas, once per paint (each paint is one CLUT of the same 8-bit picture: docs/modding/file-formats/car.md)
    b3asset::GsMemory gs;
    std::vector<std::string> paint_png;
    b3asset::RasterHeader th;
    for (int p = 0; car.tex_at && p < car.palettes; ++p) {
        b3asset::DecodedRaster dr;
        std::string err;
        if (!b3asset::decode_raster(f, car.tex_at, gs, dr, p, &err)) return fail(why, "car texture: " + err);
        th = dr.hdr;
        const std::string name = stem + "_paint" + std::to_string(p) + ".png";
        out[name] = encode_png(dr.levels[0].w, dr.levels[0].h, dr.levels[0].rgba.data());
        paint_png.push_back(name);
    }
    const int paint = paint_png.empty() ? -1 : std::clamp(opt.paint, 0, int(paint_png.size()) - 1);
    if (paint_png.empty()) r.notes.push_back("no texture atlas: the materials are untextured");
    std::vector<int> lods;
    for (int l = 0; l < 5; ++l)
        if (car.lod_at[l] && (opt.lod < 0 || opt.lod == l)) lods.push_back(l);
    if (lods.empty()) return fail(why, "the file has no LOD " + std::to_string(opt.lod));
    auto wanted = [&](const CarMesh& m) { return m.lod < 0 ? opt.lod < 0 : std::find(lods.begin(), lods.end(), m.lod) != lods.end(); };
    auto mesh_name = [&](const CarMesh& m) {
        char t[48];
        if (m.lod < 0) std::snprintf(t, sizeof t, "%s_e%02d", kSlot[m.slot], m.entry);
        else std::snprintf(t, sizeof t, "LOD%d_%s_e%02d", m.lod, kSlot[m.slot], m.entry);
        return std::string(t);
    };
    // A mesh's placements: its part frame(s) (panels at their frame, wheel lists at every wheel frame)
    auto frames_of = [&](const CarMesh& m) {
        std::vector<std::pair<std::string, std::array<float, 16>>> fr;
        std::array<float, 16> id{};
        id[0] = id[5] = id[10] = id[15] = 1;
        if (m.slot >= 1 && m.slot <= 6) fr.push_back({"", m.slot - 1 < car.panels ? gltf_matrix(car.panel_frames[std::size_t(m.slot - 1)].data()) : id});
        else if (m.slot >= 7 && m.slot <= 9 && car.wheels)
            for (int w = 0; w < car.wheels; ++w) fr.push_back({"_w" + std::to_string(w), gltf_matrix(car.wheel_frames[std::size_t(w)].data())});
        else fr.push_back({"", id});
        return fr;
    };
    bool first_bound = true;
    auto grow = [&](const std::array<float, 3>& p) {
        for (int c = 0; c < 3; ++c) {
            if (first_bound || p[c] < r.bmin[c]) r.bmin[c] = p[c];
            if (first_bound || p[c] > r.bmax[c]) r.bmax[c] = p[c];
        }
        first_bound = false;
    };

    // ---- the sidecar (both formats) ----
    Json side = Json::object();
    side.set("format", Json::str("b3modtool car export"));
    side.set("format_version", Json::integer(1));
    Json src = Json::object();
    src.set("path", Json::str(opt.source));
    src.set("size", Json::integer(std::int64_t(file.size())));
    src.set("fnv1a64", Json::str(hex64(b3mod::fnv1a64(file.data(), file.size()))));
    side.set("source", std::move(src));
    side.set("name", Json::str(opt.name));
    side.set("axes", Json::str("right-handed, Y up, metres: glTF positions are the file's; OBJ the same, v = 1 - v"));
    Json hdr = Json::object();
    hdr.set("version", Json::str(hex32(car.version)));
    hdr.set("size", Json::integer(car.size));
    hdr.set("kind_0x10", Json::integer(car.kind));
    hdr.set("panel_frames", Json::integer(car.panels));
    hdr.set("wheel_frames", Json::integer(car.wheels));
    hdr.set("f14", Json::f32(car.f14));
    hdr.set("scale_0x18", Json::f32(car.scale));
    hdr.set("wheel_factors_0x1C", Json::floats(car.wheel_factor, 6));
    Json la = Json::array();
    for (std::uint32_t v : car.lod_at) la.push(Json::str(hex32(v)));
    hdr.set("lod_blocks_0x4C", std::move(la));
    hdr.set("texture_0x60", Json::str(hex32(car.tex_at)));
    hdr.set("list_0x64", Json::str(hex32(car.list64)));
    hdr.set("list_0x68", Json::str(hex32(car.list68)));
    side.set("header", std::move(hdr));
    Json frames = Json::object();
    Json wf = Json::array(), pf = Json::array();
    for (const auto& m : car.wheel_frames) wf.push(Json::floats(m.data(), 16));
    for (const auto& m : car.panel_frames) pf.push(Json::floats(m.data(), 16));
    frames.set("wheels_0xB80", std::move(wf));
    frames.set("panels_0xD00", std::move(pf));
    side.set("frames_as_stored", std::move(frames));
    Json tex = Json::object();
    if (!paint_png.empty()) {
        tex.set("name", Json::str(th.name));
        tex.set("width", Json::integer(th.width));
        tex.set("height", Json::integer(th.height));
        tex.set("psm", Json::str(b3asset::psm_name(th.psm())));
        tex.set("levels", Json::integer(th.levels));
        tex.set("palettes", Json::integer(car.palettes));
        Json files = Json::array();
        for (const auto& n : paint_png) files.push(Json::str(n));
        tex.set("files", std::move(files));
    }
    side.set("texture", std::move(tex));
    Json lights = Json::array();
    for (const CarLight& l : car.lights) {
        Json j = Json::object();
        j.set("group", Json::integer(l.group));
        if (const char* n = light_group_name(l.group)) j.set("group_name", Json::str(n));
        j.set("index", Json::integer(l.index));
        j.set("at", Json::str(hex32(l.at)));
        j.set("raw", Json::floats(l.raw, 12));
        lights.push(std::move(j));
    }
    side.set("lights", std::move(lights));
    Json meshes = Json::array();
    for (const CarMesh& m : car.meshes) {
        Json j = Json::object();
        j.set("name", Json::str(mesh_name(m)));
        j.set("exported", Json::boolean(wanted(m)));
        j.set("lod", Json::integer(m.lod));
        j.set("slot", Json::str(kSlot[m.slot]));
        j.set("entry", Json::integer(m.entry));
        j.set("list_at", Json::str(hex32(m.list_at)));
        j.set("mesh_at", Json::str(hex32(m.mesh_at)));
        j.set("cont_at", Json::str(hex32(m.cont_at)));
        j.set("material", Json::str(hex32(m.material)));
        j.set("colour", Json::str(hex32(m.colour)));
        j.set("glass_entry", Json::integer(m.glass_entry));
        j.set("vertices", Json::integer(std::int64_t(m.v.size())));
        j.set("triangles", Json::integer(std::int64_t(m.tri.size() / 3)));
        Json b = Json::array();
        for (std::uint32_t n : m.batches) b.push(Json::integer(n));
        j.set("batches", std::move(b));
        j.set("tri_hash", Json::str(hex64(car_mesh_tri_hash(m))));
        j.set("raw_hash", Json::str(hex64(car_mesh_raw_hash(m))));
        meshes.push(std::move(j));
    }
    side.set("meshes", std::move(meshes));
    r.notes.push_back("not exported as geometry (kept in the file; an import keeps them byte for byte): the collision hull "
                      "(+0x1060), the engine-sound structures (+0x16DC), the unknown areas (+0x14, +0x6C..+0xB7F, +0x16C0)");
    r.notes.push_back("damage: the game deforms the car at run time from the per-vertex influences (_B3_INFL_0 / _1: weight << 8 | "
                      "zone << 2); the file has no damaged shapes, so there are no morph targets");
    if (car.tex_at) r.notes.push_back("texture: level 0 of the atlas per paint; its " + std::to_string(th.levels) + " stored level(s) are rebuilt by an import");

    if (opt.obj) {
        // ---- OBJ: one file per LOD, everything placed at its frames ----
        std::string mtl = "# b3modtool car export: " + (opt.name.empty() ? stem : opt.name) + "\n";
        const std::string tex_file = paint >= 0 ? paint_png[std::size_t(paint)] : "";
        mtl += "newmtl body\nKd 1 1 1\n" + (tex_file.empty() ? "" : "map_Kd " + tex_file + "\n");
        mtl += "newmtl light\nKd 1 1 1\n" + (tex_file.empty() ? "" : "map_Kd " + tex_file + "\n");
        std::map<std::uint32_t, bool> glass_alpha;
        for (const CarMesh& m : car.meshes)
            if (wanted(m) && mesh_kind(m) == kGlass) glass_alpha[m.colour & 0xFF] = true;
        for (const auto& kv : glass_alpha) {
            char t[64];
            std::snprintf(t, sizeof t, "newmtl glass_%02X\nKd 1 1 1\nd %.4f\n", kv.first, std::min(1.0, kv.first * 2 / 255.0));
            mtl += t + (tex_file.empty() ? "" : "map_Kd " + tex_file + "\n");
        }
        out[stem + ".mtl"] = Buf(mtl.begin(), mtl.end());
        std::vector<int> obj_lods = lods;
        for (int lod : obj_lods) {
            std::string o = "# b3modtool car export (" + opt.source + "), LOD " + std::to_string(lod) +
                            "; right-handed, Y up, metres\nmtllib " + stem + ".mtl\n";
            std::size_t base = 1;
            for (const CarMesh& m : car.meshes) {
                if (m.lod != lod && !(m.lod < 0 && lod == obj_lods.back() && opt.lod < 0)) continue;
                const Kind k = mesh_kind(m);
                char mn[24];
                std::snprintf(mn, sizeof mn, "glass_%02X", m.colour & 0xFF);
                for (const auto& fr : frames_of(m)) {
                    o += "o " + mesh_name(m) + fr.first + "\nusemtl " + (k == kGlass ? std::string(mn) : k == kLight ? "light" : "body") + "\n";
                    char line[160];
                    for (const CarVertex& v : m.v) {
                        const auto p = gltf_apply(fr.second, v.pos, 1.0f);
                        grow(p);
                        std::snprintf(line, sizeof line, "v %.9g %.9g %.9g\n", p[0], p[1], p[2]);
                        o += line;
                    }
                    for (const CarVertex& v : m.v) {
                        const auto t = obj_uv(v.uv[0] / 4096.0f, v.uv[1] / 4096.0f);
                        std::snprintf(line, sizeof line, "vt %.9g %.9g\n", t[0], t[1]);
                        o += line;
                    }
                    for (const CarVertex& v : m.v) {
                        const auto un = unit_normal(v);
                        auto n = gltf_apply(fr.second, un.data(), 0.0f);
                        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                        if (l > 1e-6f) for (float& c : n) c /= l;
                        std::snprintf(line, sizeof line, "vn %.6f %.6f %.6f\n", n[0], n[1], n[2]);
                        o += line;
                    }
                    // a frame with a negative determinant (a mirrored wheel) turns the winding round
                    const auto& M = fr.second;
                    const float det = M[0] * (M[5] * M[10] - M[6] * M[9]) - M[4] * (M[1] * M[10] - M[2] * M[9]) + M[8] * (M[1] * M[6] - M[2] * M[5]);
                    for (std::size_t t = 0; t + 2 < m.tri.size(); t += 3) {
                        std::size_t a = base + m.tri[t], b = base + m.tri[t + 1], c = base + m.tri[t + 2];
                        if (det < 0) std::swap(b, c);
                        std::snprintf(line, sizeof line, "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\n", a, a, a, b, b, b, c, c, c);
                        o += line;
                    }
                    base += m.v.size();
                    ++r.primitives;
                    r.vertices += m.v.size();
                    r.triangles += m.tri.size() / 3;
                }
                ++r.meshes;
            }
            const std::string name = stem + (lods.size() > 1 ? "_lod" + std::to_string(lod) : std::string()) + ".obj";
            out[name] = Buf(o.begin(), o.end());
        }
    } else {
        // ---- glTF ----
        GltfBuilder g;
        // materials: per paint, per kind (body, light, glass at its opacity)
        int sampler = g.add("samplers", [] {
            Json s = Json::object();
            s.set("magFilter", Json::integer(9729));
            s.set("minFilter", Json::integer(9987));
            s.set("wrapS", Json::integer(10497));
            s.set("wrapT", Json::integer(10497));
            return s;
        }());
        std::vector<int> tex_of_paint;
        for (std::size_t p = 0; p < paint_png.size(); ++p) {
            Json im = Json::object();
            im.set("uri", Json::str(paint_png[p]));
            im.set("name", Json::str(th.name + " paint " + std::to_string(p)));
            const int ii = g.add("images", std::move(im));
            Json t = Json::object();
            t.set("source", Json::integer(ii));
            t.set("sampler", Json::integer(sampler));
            t.set("name", Json::str(th.name + " paint " + std::to_string(p)));
            tex_of_paint.push_back(g.add("textures", std::move(t)));
        }
        std::map<std::tuple<int, int, std::uint32_t>, int> mat_index;   // (paint, kind, glass alpha byte)
        auto material = [&](int p, Kind k, std::uint32_t alpha) {
            const auto key = std::make_tuple(p, int(k), k == kGlass ? alpha : 0u);
            auto it = mat_index.find(key);
            if (it != mat_index.end()) return it->second;
            Json m = Json::object();
            char nm[48];
            std::snprintf(nm, sizeof nm, "paint%d_%s", p < 0 ? 0 : p, k == kBody ? "body" : k == kLight ? "light" : "glass");
            std::string name = nm;
            if (k == kGlass) { char t[8]; std::snprintf(t, sizeof t, "_%02X", alpha); name += t; }
            m.set("name", Json::str(name));
            Json pbr = Json::object();
            if (p >= 0) {
                Json bt = Json::object();
                bt.set("index", Json::integer(tex_of_paint[std::size_t(p)]));
                pbr.set("baseColorTexture", std::move(bt));
            }
            const float a = k == kGlass ? std::min(1.0f, float(alpha) * 2.0f / 255.0f) : 1.0f;
            const float fac[4] = {1, 1, 1, a};
            pbr.set("baseColorFactor", Json::floats(fac, 4));
            pbr.set("metallicFactor", Json::f32(k == kBody ? 0.3f : 0.0f));
            pbr.set("roughnessFactor", Json::f32(k == kGlass ? 0.05f : 0.35f));
            m.set("pbrMetallicRoughness", std::move(pbr));
            // the atlas' alpha is not coverage (docs/native-renderer.md: car atlases keep a mask there): opaque
            m.set("alphaMode", Json::str(k == kGlass ? "BLEND" : "OPAQUE"));
            m.set("doubleSided", Json::boolean(true));
            Json ex = Json::object(), b3 = Json::object();
            b3.set("paint", Json::integer(p));
            b3.set("kind", Json::str(k == kBody ? "body" : k == kLight ? "light" : "glass"));
            if (k == kGlass) b3.set("fix", Json::integer(alpha));
            ex.set("b3", std::move(b3));
            m.set("extras", std::move(ex));
            const int idx = g.add("materials", std::move(m));
            mat_index[key] = idx;
            return idx;
        };
        if (paint_png.size() > 1) {
            g.use_extension("KHR_materials_variants");
            Json vs = Json::array();
            for (std::size_t p = 0; p < paint_png.size(); ++p) {
                Json v = Json::object();
                v.set("name", Json::str("paint " + std::to_string(p)));
                vs.push(std::move(v));
            }
            Json mv = Json::object();
            mv.set("variants", std::move(vs));
            Json ex = Json::object();
            ex.set("KHR_materials_variants", std::move(mv));
            g.doc.set("extensions", std::move(ex));
        }
        // meshes
        std::vector<int> gl_mesh(car.meshes.size(), -1);
        for (std::size_t i = 0; i < car.meshes.size(); ++i) {
            const CarMesh& m = car.meshes[i];
            if (!wanted(m) || m.v.empty() || m.tri.empty()) {
                if (wanted(m) && (m.v.empty() || m.tri.empty())) r.notes.push_back(mesh_name(m) + ": no triangles, not written as a mesh (kept in the sidecar)");
                continue;
            }
            std::vector<std::array<float, 3>> pos, nrm;
            std::vector<std::array<float, 2>> uv;
            // the stored normal bytes, influence words and w lanes as float SCALARs (whole numbers), one per component:
            // Blender's glTF importer keeps float attributes (it drops an unsigned short VEC2 and clamps an unsigned byte
            // VEC4 as a colour), and pairs them with their data in a set's order, right only when they all have one
            // form (docs/modding/b3modtool.md "The export's files")
            std::vector<std::array<float, 3>> raw_n;
            std::vector<std::array<float, 2>> infl;
            std::vector<float> w;
            for (const CarVertex& v : m.v) {
                pos.push_back(gltf_position(v.pos));
                nrm.push_back(unit_normal(v));
                uv.push_back(gltf_uv(v.uv[0] / 4096.0f, v.uv[1] / 4096.0f));
                raw_n.push_back({float(v.nrm[0]), float(v.nrm[1]), float(v.nrm[2])});
                infl.push_back({float(v.infl[0]), float(v.infl[1])});
                w.push_back(float(v.w));   // 0 or 0x8000: exact as a float
            }
            Json attr = Json::object();
            attr.set("POSITION", Json::integer(g.positions(pos)));
            attr.set("NORMAL", Json::integer(g.vec3(nrm)));
            attr.set("TEXCOORD_0", Json::integer(g.vec2(uv)));
            static const char* const kXyz[] = {"X", "Y", "Z"};
            static const char* const kTwo[] = {"0", "1"};
            g.scalar_attrs(attr, "_B3_NRM", kXyz, raw_n);
            g.scalar_attrs(attr, "_B3_INFL", kTwo, infl);
            attr.set("_B3_W", Json::integer(g.float1(w)));
            Json prim = Json::object();
            prim.set("attributes", std::move(attr));
            prim.set("indices", Json::integer(g.indices(m.tri, m.v.size())));
            prim.set("mode", Json::integer(gl::kTriangles));
            const Kind k = mesh_kind(m);
            prim.set("material", Json::integer(material(paint, k, m.colour & 0xFF)));
            if (paint_png.size() > 1) {
                Json maps = Json::array();
                for (std::size_t p = 0; p < paint_png.size(); ++p) {
                    Json mp = Json::object();
                    mp.set("material", Json::integer(material(int(p), k, m.colour & 0xFF)));
                    Json vv = Json::array();
                    vv.push(Json::integer(std::int64_t(p)));
                    mp.set("variants", std::move(vv));
                    maps.push(std::move(mp));
                }
                Json mv = Json::object();
                mv.set("mappings", std::move(maps));
                Json ex = Json::object();
                ex.set("KHR_materials_variants", std::move(mv));
                prim.set("extensions", std::move(ex));
            }
            Json prims = Json::array();
            prims.push(std::move(prim));
            Json mj = Json::object();
            mj.set("name", Json::str(mesh_name(m)));
            mj.set("primitives", std::move(prims));
            Json ex = Json::object(), b3 = Json::object();
            b3.set("lod", Json::integer(m.lod));
            b3.set("slot", Json::str(kSlot[m.slot]));
            b3.set("entry", Json::integer(m.entry));
            b3.set("material", Json::str(hex32(m.material)));
            b3.set("colour", Json::str(hex32(m.colour)));
            b3.set("kind", Json::str(k == kBody ? "body" : k == kLight ? "light" : "glass"));
            ex.set("b3", std::move(b3));
            mj.set("extras", std::move(ex));
            gl_mesh[i] = g.add("meshes", std::move(mj));
            ++r.meshes;
            ++r.primitives;
            r.vertices += m.v.size();
            r.triangles += m.tri.size() / 3;
        }
        // nodes: root -> LOD<k> -> part (at its frame) -> one node per mesh entry
        auto node = [&](const std::string& name, Json extra_b3) {
            Json n = Json::object();
            n.set("name", Json::str(name));
            if (extra_b3.type == Json::Obj) {
                Json ex = Json::object();
                ex.set("b3", std::move(extra_b3));
                n.set("extras", std::move(ex));
            }
            return n;
        };
        auto attach = [&](int parent, int child) {
            Json& p = g.doc.get("nodes")->a[std::size_t(parent)];
            Json* ch = p.get("children");
            if (!ch) ch = &p.set("children", Json::array());
            ch->push(Json::integer(child));
        };
        Json rb3 = Json::object();
        rb3.set("file", Json::str(opt.source));
        rb3.set("stem", Json::str(stem));
        rb3.set("sidecar", Json::str(stem + ".b3car.json"));
        const int root = g.add("nodes", node(opt.name.empty() ? stem : opt.name, std::move(rb3)));
        for (std::size_t li = 0; li <= lods.size(); ++li) {
            const int lod = li < lods.size() ? lods[li] : -1;
            int ln = -1;
            std::map<std::string, int> part_nodes;
            for (std::size_t i = 0; i < car.meshes.size(); ++i) {
                const CarMesh& m = car.meshes[i];
                if (m.lod != lod || gl_mesh[i] < 0) continue;
                if (ln < 0) {
                    Json b = Json::object();
                    b.set("lod", Json::integer(lod));
                    ln = g.add("nodes", node(lod >= 0 ? "LOD" + std::to_string(lod) : "extra_lists", std::move(b)));
                    attach(root, ln);
                }
                const std::string pfx = lod >= 0 ? "LOD" + std::to_string(lod) + "_" : std::string();
                for (const auto& fr : frames_of(m)) {
                    const std::string pname = pfx + kSlot[m.slot] + fr.first;
                    auto it = part_nodes.find(pname);
                    if (it == part_nodes.end()) {
                        Json b = Json::object();
                        b.set("part", Json::str(kSlot[m.slot]));
                        if (!fr.first.empty()) b.set("wheel", Json::integer(std::stoi(fr.first.substr(2))));
                        Json n = node(pname, std::move(b));
                        std::array<float, 16> mtx = fr.second;
                        if (!is_identity(mtx)) n.set("matrix", Json::floats(mtx.data(), 16));
                        const int pn = g.add("nodes", std::move(n));
                        attach(ln, pn);
                        it = part_nodes.emplace(pname, pn).first;
                    }
                    Json b = Json::object();
                    b.set("entry", Json::integer(m.entry));
                    Json en = node(pname + "_e" + (m.entry < 10 ? "0" : "") + std::to_string(m.entry), std::move(b));
                    en.set("mesh", Json::integer(gl_mesh[i]));
                    const int e = g.add("nodes", std::move(en));
                    attach(it->second, e);
                    for (const CarVertex& v : m.v) grow(gltf_apply(fr.second, v.pos, 1.0f));
                }
            }
        }
        if (!car.lights.empty()) {
            const int ln = g.add("nodes", node("lights", Json()));
            attach(root, ln);
            for (const CarLight& l : car.lights) {
                const char* gn = light_group_name(l.group);
                Json b = Json::object();
                b.set("group", Json::integer(l.group));
                b.set("index", Json::integer(l.index));
                b.set("raw", Json::floats(l.raw, 12));
                Json n = node(std::string("light_") + (gn ? gn : "g" + std::to_string(l.group)) + "_" + std::to_string(l.index), std::move(b));
                n.set("translation", Json::floats(l.raw, 3));
                attach(ln, g.add("nodes", std::move(n)));
            }
        }
        Json scene = Json::object();
        scene.set("name", Json::str(opt.name.empty() ? stem : opt.name));
        Json sn = Json::array();
        sn.push(Json::integer(root));
        scene.set("nodes", std::move(sn));
        g.add("scenes", std::move(scene));
        g.doc.set("scene", Json::integer(0));
        Json aex = Json::object();
        aex.set("b3_axes", Json::str("right-handed, Y up, metres (the game's own space)"));
        aex.set("b3_sidecar", Json::str(stem + ".b3car.json"));
        g.doc.get("asset")->set("extras", std::move(aex));
        const std::string text = g.text(stem + ".bin");
        while (g.bin.size() % 4) g.bin.push_back(0);
        out[stem + ".gltf"] = Buf(text.begin(), text.end());
        out[stem + ".bin"] = g.bin;
        r.nodes = g.doc.get("nodes")->a.size();
    }
    r.textures = paint_png.size();
    Json notes = Json::array();
    for (const auto& n : r.notes) notes.push(Json::str(n));
    side.set("notes", std::move(notes));
    const std::string st = side.dump(1) + "\n";
    out[stem + ".b3car.json"] = Buf(st.begin(), st.end());
    r.files = out.size();
    if (rep) *rep = r;
    return true;
}

}  // namespace b3modtool
