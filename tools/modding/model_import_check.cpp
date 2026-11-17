// model_import_check: the census of mod kit part 3c's car import (docs/modding/b3modtool.md "car import").
//   build/model_import_check <disc> [out dir] [car ...]
// For every player car (or the cars named: paths like PVEH/COMP/CAR1.BGV), in memory, as `b3modtool car export` and
// `car import` do:
//   identity    export (glTF, every LOD) -> import unchanged: the bytes of the disc's file
//   restrip     the same with every mesh's strips rebuilt from its triangles (what an import of a model re-saved by a
//               modelling tool does): decode-identical -- every entry's triangles (positions, texture coordinates,
//               influences, normals) the same set as the disc file's, every LOD of the renderer's decode
//               (b3asset::read_car) the same drawn triangles and bounds; the size against the game's limit
//   blender     the export through Blender's glTF importer and exporter (blender_resave: what they do, read from
//               their source), unedited: the disc's bytes; with LOD 3's body scaled 1.3x in object mode: those positions;
//               an export in the layout before the per-component attributes (legacy_layout): the import still gives
//               the disc's bytes, and Blender's importer does not take it (attributes of different widths)
//   edit        LOD 3's body (L0) scaled 1.3x in the glTF: the import's file decodes to exactly those positions, same
//               size; paint 0's picture with red and blue swapped: paint 0 decodes to it, the other paints unchanged
// Writes nothing but [out dir] (the first failing car's files, for a look; the game's data: delete it after).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "b3asset/car.h"
#include "b3asset/gs_memory.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"
#include "b3modtool/meshimport.h"

using namespace b3modtool;
namespace fs = std::filesystem;

namespace {

FileLoader loader(const FileSet& fsx) {
    return [&fsx](const std::string& p, Buf& out) {
        auto it = fsx.find(p);
        if (it == fsx.end()) return false;
        out = it->second;
        return true;
    };
}

std::uint64_t mesh_hash(const CarMesh& m) {
    std::vector<std::array<float, 3>> pos;
    std::vector<std::uint8_t> ex;
    for (CarVertex v : m.v) {
        car_form_mask(car_slot_form(m.slot), v);
        pos.push_back({v.pos[0], v.pos[1], v.pos[2]});
        const std::uint16_t e[4] = {v.uv[0], v.uv[1], v.infl[0], v.infl[1]};
        const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(e);
        ex.insert(ex.end(), p, p + 8);
        ex.insert(ex.end(), v.nrm, v.nrm + 3);
    }
    return triangle_set_hash(pos, ex, 11, m.tri);
}

// the renderer's decode of one LOD: triangles that draw something (two or more distinct positions), and bounds
struct Drawn { std::size_t tris = 0; float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0}; bool ok = false; };
Drawn drawn(const Buf& f, int lod) {
    Drawn d;
    b3asset::GsMemory gs;
    b3asset::Model m;
    b3asset::CarOptions co;
    co.lod = lod;
    if (!b3asset::read_car(b3asset::Bytes{f.data(), f.size()}, co, gs, m)) return d;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const float* a = m.vertices[m.indices[t]].pos; const float* b = m.vertices[m.indices[t + 1]].pos; const float* c = m.vertices[m.indices[t + 2]].pos;
        if (std::memcmp(a, b, 12) && std::memcmp(b, c, 12) && std::memcmp(a, c, 12)) ++d.tris;
    }
    std::memcpy(d.bmin, m.bmin, 12);
    std::memcpy(d.bmax, m.bmax, 12);
    d.ok = true;
    return d;
}

// scales the POSITION accessors of the meshes whose name starts with `prefix` in an exported glTF (in place)
bool scale_meshes(FileSet& fsx, const std::string& stem, const std::string& prefix, float k) {
    Json doc;
    const Buf& t = fsx[stem + ".gltf"];
    if (!json_parse(std::string_view(reinterpret_cast<const char*>(t.data()), t.size()), doc)) return false;
    Buf& bin = fsx[stem + ".bin"];
    std::size_t n = 0;
    for (const Json& m : doc.get("meshes")->a) {
        if (m.get("name")->s.rfind(prefix, 0) != 0) continue;
        const int ai = int(m.get("primitives")->a[0].get("attributes")->get("POSITION")->as_int());
        const Json& a = doc.get("accessors")->a[std::size_t(ai)];
        const Json& v = doc.get("bufferViews")->a[std::size_t(a.get("bufferView")->as_int())];
        const std::size_t at = std::size_t((v.get("byteOffset") ? v.get("byteOffset")->as_int() : 0) + (a.get("byteOffset") ? a.get("byteOffset")->as_int() : 0));
        for (std::size_t i = 0; i < std::size_t(a.get("count")->as_int()) * 3; ++i) {
            float x;
            std::memcpy(&x, &bin[at + 4 * i], 4);
            x *= k;
            std::memcpy(&bin[at + 4 * i], &x, 4);
        }
        ++n;
    }
    return n > 0;
}

// ---- Blender: what its glTF importer and exporter (glTF-Blender-IO, Blender 4.2 LTS) do to an export, read from
// their source (docs/modding/b3modtool.md "Blender"; the add-on tools/blender/b3_car_tools runs them for real, and
// tools/modding/blender_check.sh checks that once a Blender is at hand). Import (blender/imp/gltf2_blender_mesh.py):
// only the vertices the indices use, in index order (np.unique); NORMAL becomes custom split normals; the "_"
// attributes become mesh attributes by their accessor's type (blender/com/gltf2_blender_conversion.py
// get_attribute_type): float SCALAR / VEC2 / VEC3 / VEC4 -> FLOAT / FLOAT2 / FLOAT_VECTOR / FLOAT_COLOR (the values
// kept), an unsigned byte SCALAR -> INT, an unsigned byte or short VEC4 -> BYTE_COLOR (the raw values written as linear
// colours: clamped to 0..1), every other form dropped; the "_" attributes of a mesh are kept in a Python set and
// matched with their data buffers (made in the order first seen) by the set's iteration order, which changes with
// the process's string hashing: attributes of different widths in one mesh fail (numpy concatenate) or get each
// other's buffers at random, so b3modtool writes one float SCALAR per component; a node matrix becomes location / rotation / scale
// (Matrix.decompose). Export (blender/exp/gltf2_blender_gather_primitives_extract.py): one glTF vertex per distinct
// loop record, in the order np.unique sorts the records' raw bytes (the Blender vertex index first, little-endian), the
// triangles in their order; NORMAL rounded to 4 decimals and normalised; FLOAT_COLOR / FLOAT2 / FLOAT_VECTOR / FLOAT
// / INT as floats, BYTE_COLOR as normalized unsigned shorts; the node's transform as translation / rotation / scale
// (decomposed again). Y up <-> Z up, and v <-> 1 - v, are exact for these values (swaps, negations, 1 - k / 4096)
// and left out.
void decompose(const std::array<float, 16>& m, float t[3], float q[4], float s[3]) {
    for (int i = 0; i < 3; ++i) t[i] = m[std::size_t(12 + i)];
    float c[3][3];
    for (int k = 0; k < 3; ++k) {
        s[k] = std::sqrt(m[std::size_t(4 * k)] * m[std::size_t(4 * k)] + m[std::size_t(4 * k + 1)] * m[std::size_t(4 * k + 1)] + m[std::size_t(4 * k + 2)] * m[std::size_t(4 * k + 2)]);
    }
    const float det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
    if (det < 0) for (int k = 0; k < 3; ++k) s[k] = -s[k];
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < 3; ++r) c[k][r] = s[k] != 0 ? m[std::size_t(4 * k + r)] / s[k] : 0;   // c[column][row]
    auto R = [&](int r, int k) { return c[k][r]; };
    const float tr = R(0, 0) + R(1, 1) + R(2, 2);
    float x, y, z, w;
    if (tr > 0) {
        const float S = std::sqrt(tr + 1.0f) * 2;
        w = 0.25f * S; x = (R(2, 1) - R(1, 2)) / S; y = (R(0, 2) - R(2, 0)) / S; z = (R(1, 0) - R(0, 1)) / S;
    } else if (R(0, 0) > R(1, 1) && R(0, 0) > R(2, 2)) {
        const float S = std::sqrt(1.0f + R(0, 0) - R(1, 1) - R(2, 2)) * 2;
        w = (R(2, 1) - R(1, 2)) / S; x = 0.25f * S; y = (R(0, 1) + R(1, 0)) / S; z = (R(0, 2) + R(2, 0)) / S;
    } else if (R(1, 1) > R(2, 2)) {
        const float S = std::sqrt(1.0f + R(1, 1) - R(0, 0) - R(2, 2)) * 2;
        w = (R(0, 2) - R(2, 0)) / S; x = (R(0, 1) + R(1, 0)) / S; y = 0.25f * S; z = (R(1, 2) + R(2, 1)) / S;
    } else {
        const float S = std::sqrt(1.0f + R(2, 2) - R(0, 0) - R(1, 1)) * 2;
        w = (R(1, 0) - R(0, 1)) / S; x = (R(0, 2) + R(2, 0)) / S; y = (R(1, 2) + R(2, 1)) / S; z = 0.25f * S;
    }
    const float l = std::sqrt(x * x + y * y + z * z + w * w);
    q[0] = x / l; q[1] = y / l; q[2] = z / l; q[3] = w / l;
}

Json trs_node(const Json& n, const float t[3], const float q[4], const float s[3]) {
    Json o = Json::object();
    for (const auto& [k, v] : n.o)
        if (k != "matrix" && k != "translation" && k != "rotation" && k != "scale") o.set(k, v);
    o.set("translation", Json::floats(t, 3));
    o.set("rotation", Json::floats(q, 4));
    o.set("scale", Json::floats(s, 3));
    return o;
}

// An export through Blender's import and glTF export, unedited, as a .glb (`body_scale` != 1: the LOD 3 body's
// objects scaled in object mode by that much about the car's origin, as tutorial 08 does). False (and `why`) where
// Blender's importer does not take the file: "_" attributes of different widths in one mesh.
bool blender_resave(const FileSet& fsx, const std::string& stem, Buf& glb, std::string* why, float body_scale = 1.0f) {
    Json doc;
    const Buf& t = fsx.at(stem + ".gltf");
    if (!json_parse(std::string_view(reinterpret_cast<const char*>(t.data()), t.size()), doc, why)) return false;
    const Buf& bin = fsx.at(stem + ".bin");
    GltfBuilder g;
    g.doc.set("scene", *doc.get("scene"));
    g.doc.set("scenes", *doc.get("scenes"));
    // nodes: every transform as Blender's location / rotation / scale, decomposed twice (import, export)
    Json nodes = Json::array();
    for (const Json& n : doc.get("nodes")->a) {
        std::array<float, 16> m = gltf_node_matrix(n);
        const std::string name = n.get("name") ? n.get("name")->s : "";
        if (body_scale != 1.0f && name.rfind("LOD3_L0_e", 0) == 0) {
            const std::array<float, 16> S{body_scale, 0, 0, 0, 0, body_scale, 0, 0, 0, 0, body_scale, 0, 0, 0, 0, 1};
            m = gltf_mul(S, m);   // the body part's frame is the identity: about the car's origin
        }
        float tt[3], q[4], s[3];
        decompose(m, tt, q, s);
        Json tmp = trs_node(n, tt, q, s);
        decompose(gltf_node_matrix(tmp), tt, q, s);
        nodes.push(trs_node(n, tt, q, s));
    }
    g.doc.set("nodes", std::move(nodes));
    Json meshes = Json::array();
    for (const Json& m : doc.get("meshes")->a) {
        Json prims = Json::array();
        for (const Json& p : m.get("primitives")->a) {
            const Json* at = p.get("attributes");
            std::vector<double> pos, nrm, uv, idx;
            int nc = 0;
            if (!gltf_read_accessor(doc, bin, int(at->get("POSITION")->as_int()), pos, nc, why) ||
                !gltf_read_accessor(doc, bin, int(at->get("NORMAL")->as_int()), nrm, nc, why) ||
                !gltf_read_accessor(doc, bin, int(at->get("TEXCOORD_0")->as_int()), uv, nc, why) ||
                !gltf_read_accessor(doc, bin, int(p.get("indices")->as_int()), idx, nc, why))
                return false;
            // import: the used vertices in index order
            std::vector<std::uint32_t> used;
            for (double x : idx) used.push_back(std::uint32_t(x));
            std::sort(used.begin(), used.end());
            used.erase(std::unique(used.begin(), used.end()), used.end());
            // export: np.unique over the records' raw bytes, the vertex index (little-endian u32) first
            std::vector<std::uint32_t> order(used.size());
            std::iota(order.begin(), order.end(), 0u);
            auto le = [](std::uint32_t i) { return (i & 0xFF) << 24 | (i & 0xFF00) << 8 | (i >> 8 & 0xFF00) | i >> 24; };
            std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return le(a) < le(b); });
            std::vector<std::uint32_t> at_out(used.size());   // Blender vertex -> output vertex
            for (std::uint32_t k = 0; k < order.size(); ++k) at_out[order[k]] = k;
            auto blender_of = [&](std::uint32_t i) { return std::uint32_t(std::lower_bound(used.begin(), used.end(), i) - used.begin()); };
            std::vector<std::array<float, 3>> P, N;
            std::vector<std::array<float, 2>> U;
            for (std::uint32_t k : order) {
                const std::uint32_t o = used[k];
                P.push_back({float(pos[3 * o]), float(pos[3 * o + 1]), float(pos[3 * o + 2])});
                std::array<float, 3> n{};
                double l = 0;
                for (int c = 0; c < 3; ++c) { n[std::size_t(c)] = float(std::nearbyint(double(float(nrm[3 * o + std::size_t(c)])) * 1e4) / 1e4); l += double(n[std::size_t(c)]) * n[std::size_t(c)]; }
                if (l > 0) for (float& x : n) x = float(x / std::sqrt(l));
                else n = {0, 1, 0};
                N.push_back(n);
                U.push_back({float(uv[2 * o]), float(uv[2 * o + 1])});
            }
            Json attr = Json::object();
            attr.set("POSITION", Json::integer(g.positions(P)));
            attr.set("NORMAL", Json::integer(g.vec3(N)));
            attr.set("TEXCOORD_0", Json::integer(g.vec2(U)));
            std::string first_width;   // the "_" attributes' widths (every one of them: dropped forms too)
            for (const auto& [name, ai] : at->o) {
                if (name.empty() || name[0] != '_') continue;
                const std::string& type = doc.get("accessors")->a[std::size_t(ai.as_int())].get("type")->s;
                if (first_width.empty()) first_width = name + " " + type;
                else if (type != first_width.substr(first_width.find(' ') + 1)) {
                    if (why) *why = (m.get("name") ? m.get("name")->s : std::string("a mesh")) + ": Blender's glTF importer fails on \"_\" attributes of different widths (" +
                                    first_width + ", " + name + " " + type + ")";
                    return false;
                }
            }
            for (const auto& [name, ai] : at->o) {
                if (name.empty() || name[0] != '_') continue;
                const Json& a = doc.get("accessors")->a[std::size_t(ai.as_int())];
                const int comp = int(a.get("componentType")->as_int());
                const std::string type = a.get("type")->s;
                std::vector<double> vals;
                if (!gltf_read_accessor(doc, bin, int(ai.as_int()), vals, nc, why)) return false;
                std::vector<float> outv;
                bool byte_colour = false;
                if (comp == gl::kFloat || (comp == gl::kUByte && type == "SCALAR")) {
                } else if ((comp == gl::kUByte || comp == gl::kUShort) && type == "VEC4") {
                    byte_colour = true;
                } else {
                    continue;   // dropped by the import
                }
                for (std::uint32_t k : order)
                    for (int c = 0; c < nc; ++c) {
                        double x = vals[std::size_t(used[k]) * std::size_t(nc) + std::size_t(c)];
                        if (byte_colour) x = std::clamp(x, 0.0, 1.0);
                        outv.push_back(float(x));
                    }
                static const char* const types[] = {"", "SCALAR", "VEC2", "VEC3", "VEC4"};
                int ia;
                if (byte_colour) {
                    std::vector<std::uint16_t> u16;
                    for (float x : outv) u16.push_back(std::uint16_t(std::lround(double(x) * 65535.0)));
                    ia = g.accessor(g.view(u16.data(), u16.size() * 2, gl::kArrayBuffer), gl::kUShort, order.size(), "VEC4", true);
                } else {
                    ia = g.accessor(g.view(outv.data(), outv.size() * 4, gl::kArrayBuffer), gl::kFloat, order.size(), types[nc]);
                }
                attr.set(name, Json::integer(ia));
            }
            std::vector<std::uint32_t> tri;
            for (double x : idx) tri.push_back(at_out[blender_of(std::uint32_t(x))]);
            Json prim = Json::object();
            prim.set("attributes", std::move(attr));
            prim.set("indices", Json::integer(g.indices(tri, order.size())));
            prim.set("mode", Json::integer(gl::kTriangles));
            prims.push(std::move(prim));
        }
        Json mj = Json::object();
        mj.set("name", *m.get("name"));
        mj.set("primitives", std::move(prims));
        if (const Json* ex = m.get("extras")) mj.set("extras", *ex);
        meshes.push(std::move(mj));
    }
    g.doc.set("meshes", std::move(meshes));
    while (g.bin.size() % 4) g.bin.push_back(0);
    Json d = g.doc;
    Json buf = Json::object();
    buf.set("byteLength", Json::integer(std::int64_t(g.bin.size())));
    Json bufs = Json::array();
    bufs.push(std::move(buf));
    d.set("buffers", std::move(bufs));
    std::string js = d.dump();
    while (js.size() % 4) js.push_back(' ');
    glb.clear();
    auto put = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) glb.push_back(std::uint8_t(v >> (8 * i))); };
    put(0x46546C67u);
    put(2);
    put(std::uint32_t(12 + 8 + js.size() + 8 + g.bin.size()));
    put(std::uint32_t(js.size()));
    put(0x4E4F534Au);
    glb.insert(glb.end(), js.begin(), js.end());
    put(std::uint32_t(g.bin.size()));
    put(0x004E4942u);
    glb.insert(glb.end(), g.bin.begin(), g.bin.end());
    return true;
}

// The export in the layout b3modtool wrote before the stored fields were one float SCALAR per component: _B3_NRM a
// float VEC4 (the 4th 0), _B3_INFL a float VEC2, _B3_W an unsigned int SCALAR (what an older export folder holds)
bool legacy_layout(const FileSet& fsx, const std::string& stem, FileSet& out, std::string* why) {
    Json doc;
    const Buf& t = fsx.at(stem + ".gltf");
    if (!json_parse(std::string_view(reinterpret_cast<const char*>(t.data()), t.size()), doc, why)) return false;
    const Buf& bin = fsx.at(stem + ".bin");
    GltfBuilder g;
    g.doc = doc;
    g.bin = bin;
    for (Json& m : g.doc.get("meshes")->a)
        for (Json& p : m.get("primitives")->a) {
            Json* at = p.get("attributes");
            std::vector<double> n, i, w;
            int nn = 0, ni = 0, nw = 0;
            gltf_read_field(doc, bin, *at, "_B3_NRM", {"X", "Y", "Z"}, 255.0, n, nn);
            gltf_read_field(doc, bin, *at, "_B3_INFL", {"0", "1"}, 65535.0, i, ni);
            if (nn != 3 || ni != 2 || !at->get("_B3_W") || !gltf_read_accessor(doc, bin, int(at->get("_B3_W")->as_int()), w, nw, why) || nw != 1) {
                if (why && nn != 3) *why = "no _B3_NRM_X / _Y / _Z";
                return false;
            }
            std::vector<std::array<float, 4>> n4;
            std::vector<std::array<float, 2>> i2;
            std::vector<std::uint32_t> w1;
            for (std::size_t k = 0; k < w.size(); ++k) {
                n4.push_back({float(n[3 * k]), float(n[3 * k + 1]), float(n[3 * k + 2]), 0.0f});
                i2.push_back({float(i[2 * k]), float(i[2 * k + 1])});
                w1.push_back(std::uint32_t(w[k]));
            }
            Json a = Json::object();
            for (const auto& [k, v] : at->o)
                if (k.rfind("_B3_", 0) != 0) a.set(k, v);
            a.set("_B3_NRM", Json::integer(g.vec4(n4)));
            a.set("_B3_INFL", Json::integer(g.vec2(i2)));
            a.set("_B3_W", Json::integer(g.uint1(w1)));
            *at = std::move(a);
        }
    out = fsx;
    const std::string uri = doc.get("buffers")->a[0].get("uri")->s;
    const std::string text = g.text(uri);
    out[stem + ".gltf"] = Buf(text.begin(), text.end());
    while (g.bin.size() % 4) g.bin.push_back(0);
    out[stem + ".bin"] = g.bin;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: model_import_check <disc> [out dir] [PVEH/<class>/CARn.BGV ...]\n");
        return 2;
    }
    const std::string game = argv[1], out = argc > 2 ? argv[2] : "";
    std::vector<std::string> want;
    for (int i = 3; i < argc; ++i) want.push_back(argv[i]);
    std::size_t cars = 0, identical = 0, restrip_ok = 0, restrip_fit = 0, lods = 0, lods_same = 0, entries = 0, entries_same = 0;
    std::size_t edit_ok = 0, paint_ok = 0, bigger = 0, reorder_ok = 0, topo_ok = 0, topo_fit = 0, blender_ok = 0, blender_scale_ok = 0, blender_legacy_ok = 0;
    double worst_growth = 0;
    std::uint64_t v_disc = 0, v_restrip = 0;
    int failures = 0;
    bool kept = false;
    std::string note;
    for (const CarListEntry& c : car_list(game, false, &note)) {
        if (!want.empty() && std::find(want.begin(), want.end(), c.key) == want.end()) continue;
        Buf file;
        if (!read_host_file(c.path, file)) { std::printf("%s: cannot read\n", c.key.c_str()); ++failures; continue; }
        ++cars;
        std::string stem = c.key.substr(5, c.key.find('.') - 5);
        std::replace(stem.begin(), stem.end(), '/', '_');
        CarExportOptions eo;
        eo.name = c.name;
        eo.source = c.key;
        FileSet fsx;
        std::string why;
        if (!car_export(file, stem, eo, fsx, nullptr, &why)) { std::printf("%s: export: %s\n", c.key.c_str(), why.c_str()); ++failures; continue; }
        const std::string model = stem + ".gltf", side = stem + ".b3car.json";
        auto keep = [&](const Buf& got, const char* what) {
            if (kept || out.empty()) return;
            kept = true;
            const fs::path d = fs::path(out) / stem;
            for (const auto& [n, b] : fsx) write_host_file(b3mod::path_utf8(d / n), b);
            write_host_file(b3mod::path_utf8(d / (std::string(what) + ".BGV")), got);
            std::printf("  (%s's export and its %s import kept in %s)\n", c.key.c_str(), what, b3mod::path_utf8(d).c_str());
        };
        std::string line;
        // ---- identity ----
        Buf got;
        CarImportReport rep;
        bool ok = car_import(file, loader(fsx), model, side, {}, got, &rep, &why);
        const bool same = ok && got == file;
        identical += same;
        if (!same) {
            ++failures;
            std::size_t first = 0;
            while (ok && first < std::min(got.size(), file.size()) && got[first] == file[first]) ++first;
            line += ok ? " identity DIFFERS (first at 0x" + [&] { char t[16]; std::snprintf(t, sizeof t, "%zX", first); return std::string(t); }() + ")" : " identity FAILED: " + why;
            if (ok) keep(got, "identity");
        } else {
            char t[96];
            std::snprintf(t, sizeof t, " identical (%zu entries: %zu unchanged)", rep.meshes, rep.unchanged);
            line += t;
        }
        // ---- restrip: decode-identical ----
        CarImportOptions ro;
        ro.restrip = true;
        ro.max_size = 64u << 20;
        Buf rs;
        CarImportReport rr;
        if (!car_import(file, loader(fsx), model, side, ro, rs, &rr, &why)) {
            ++failures;
            line += " restrip FAILED: " + why;
        } else {
            CarFile a, b;
            bool dec = car_read(file, a) && car_read(rs, b) && a.meshes.size() == b.meshes.size();
            std::size_t e_same = 0;
            for (std::size_t i = 0; dec && i < a.meshes.size(); ++i) e_same += mesh_hash(a.meshes[i]) == mesh_hash(b.meshes[i]);
            entries += a.meshes.size();
            entries_same += e_same;
            dec = dec && e_same == a.meshes.size();
            std::size_t l_same = 0, l_all = 0;
            for (int lod = 0; lod < 5; ++lod) {
                if (!a.lod_at[lod]) continue;
                ++l_all;
                const Drawn x = drawn(file, lod), y = drawn(rs, lod);
                l_same += x.ok && y.ok && x.tris == y.tris && !std::memcmp(x.bmin, y.bmin, 12) && !std::memcmp(x.bmax, y.bmax, 12);
            }
            lods += l_all;
            lods_same += l_same;
            dec = dec && l_same == l_all;
            restrip_ok += dec;
            const bool fit = rs.size() <= kCarMaxBgvSize;
            restrip_fit += fit;
            bigger += rs.size() > file.size();
            worst_growth = std::max(worst_growth, double(rs.size()) / double(file.size()) - 1.0);
            for (const CarMesh& m : a.meshes) v_disc += m.v.size();
            for (const CarMesh& m : b.meshes) v_restrip += m.v.size();
            if (!dec) { ++failures; keep(rs, "restrip"); }
            char t[200];
            std::snprintf(t, sizeof t, "; restrip %s (%zu/%zu entries, %zu/%zu LODs), %zu -> %zu bytes%s", dec ? "decode-identical" : "DIFFERS", e_same,
                          a.meshes.size(), l_same, l_all, file.size(), rs.size(), fit ? "" : " (over the limit)");
            line += t;
        }
        // ---- a modelling tool's re-save: every mesh's vertices and triangles in another order: the same bytes ----
        {
            CarFile car;
            std::vector<CarImportMesh> ms;
            Buf rb;
            CarImportReport rr2;
            bool ok2 = car_read(file, car) && car_model_read(loader(fsx), model, car, ms, nullptr, &why);
            std::mt19937 rng(static_cast<std::uint32_t>(cars));
            for (CarImportMesh& m : ms) {
                std::vector<std::uint32_t> perm(m.v.size());
                std::iota(perm.begin(), perm.end(), 0u);
                std::shuffle(perm.begin(), perm.end(), rng);
                std::vector<CarVertex> nv(m.v.size());
                for (std::size_t k = 0; k < m.v.size(); ++k) nv[perm[k]] = m.v[k];
                std::vector<std::array<std::uint32_t, 3>> tr;
                for (std::size_t t = 0; t + 2 < m.tri.size(); t += 3) {
                    const int r = int(rng() % 3);
                    const std::uint32_t q[3] = {perm[m.tri[t]], perm[m.tri[t + 1]], perm[m.tri[t + 2]]};
                    tr.push_back({q[r], q[(r + 1) % 3], q[(r + 2) % 3]});
                }
                std::shuffle(tr.begin(), tr.end(), rng);
                m.v = std::move(nv);
                m.tri.clear();
                for (const auto& x : tr) m.tri.insert(m.tri.end(), x.begin(), x.end());
            }
            ok2 = ok2 && car_import_meshes(file, ms, {}, {}, rb, &rr2, &why) && rb == file;
            reorder_ok += ok2;
            if (!ok2) ++failures;
            line += std::string("; re-saved in another order ") + (ok2 ? "identical" : "DIFFERS (" + std::to_string(rr2.unchanged) + " unchanged, " + std::to_string(rr2.edited) + " edited, " + std::to_string(rr2.restripped) + " restripped: " + why + ")");
            // ---- new topology: LOD 3's first body mesh gets a quad more (four new vertices, two triangles) ----
            std::vector<CarImportMesh> mt;
            ok2 = car_model_read(loader(fsx), model, car, mt, nullptr, &why);
            CarImportMesh* body = nullptr;
            for (CarImportMesh& m : mt)
                if (m.lod == 3 && m.slot == 0 && m.entry == 0) body = &m;
            ok2 = ok2 && body && body->tri.size() >= 3;
            if (ok2) {
                const std::uint32_t b0 = std::uint32_t(body->v.size());
                const CarVertex base = body->v[body->tri[0]];
                for (int q = 0; q < 4; ++q) {
                    CarVertex v = base;
                    v.pos[0] += 0.1f * float(q & 1);
                    v.pos[1] += 0.3f;
                    v.pos[2] += 0.1f * float(q >> 1);
                    body->v.push_back(v);
                }
                body->tri.insert(body->tri.end(), {b0, b0 + 2, b0 + 1, b0 + 1, b0 + 2, b0 + 3});
                CarImportOptions big;
                big.max_size = 64u << 20;
                Buf tb;
                ok2 = car_import_meshes(file, mt, {}, big, tb, &rr2, &why) && rr2.restripped == 1;
                CarFile a2, b2;
                ok2 = ok2 && car_read(file, a2) && car_read(tb, b2) && a2.meshes.size() == b2.meshes.size();
                for (std::size_t i = 0; ok2 && i < b2.meshes.size(); ++i) {
                    const CarMesh& x = b2.meshes[i];
                    if (x.lod == 3 && x.slot == 0 && x.entry == 0) {
                        CarMesh want;
                        want.slot = 0;
                        want.v = body->v;
                        want.tri = body->tri;
                        ok2 = mesh_hash(x) == mesh_hash(want);
                    } else {
                        ok2 = mesh_hash(x) == mesh_hash(a2.meshes[i]);
                    }
                }
                topo_ok += ok2;
                const bool fit = ok2 && tb.size() <= kCarMaxBgvSize;
                topo_fit += fit;
                char t[120];
                std::snprintf(t, sizeof t, "; a quad more in LOD3_L0_e00 %s, %zu -> %zu bytes%s", ok2 ? "decode-identical" : "DIFFERS", file.size(), tb.size(),
                              fit ? "" : " (over the limit)");
                line += t;
            } else {
                line += "; a quad more: FAILED " + why;
            }
            if (!ok2) ++failures;
        }
        // ---- through Blender (its importer and exporter as blender_resave says): unedited, the same bytes; the LOD 3
        // body scaled 1.3x in object mode: those positions (to float rounding), the same size ----
        {
            FileSet fb = fsx;
            Buf glb, bb;
            CarImportReport br;
            bool ok3 = blender_resave(fsx, stem, glb, &why);
            fb[stem + "_blender.glb"] = glb;
            ok3 = ok3 && car_import(file, loader(fb), stem + "_blender.glb", side, {}, bb, &br, &why) && bb == file;
            blender_ok += ok3;
            if (!ok3) ++failures;
            line += std::string("; through Blender ") + (ok3 ? "identical" : "DIFFERS (" + std::to_string(br.unchanged) + " unchanged, " + std::to_string(br.edited) + " edited, " + std::to_string(br.restripped) + " restripped: " + why + ")");
            // an export in the layout before the per-component attributes (an older b3modtool's): the import still
            // gives the disc's bytes; Blender's importer does not take it (its "_" attributes have different widths)
            FileSet fl;
            bool ok5 = legacy_layout(fsx, stem, fl, &why) && car_import(file, loader(fl), model, side, {}, bb, &br, &why) && bb == file;
            std::string bwhy;
            const bool refused = ok5 && !blender_resave(fl, stem, glb, &bwhy) && bwhy.find("different widths") != std::string::npos;
            if (ok5 && !refused) why = "Blender's importer as modelled takes the old layout";
            ok5 = ok5 && refused;
            blender_legacy_ok += ok5;
            if (!ok5) ++failures;
            line += std::string(" (old layout: ") + (ok5 ? "identical, Blender refuses it)" : "FAILED " + why + ")");
            br = CarImportReport{};
            why.clear();
            bool ok4 = blender_resave(fsx, stem, glb, &why, 1.3f);
            fb[stem + "_blender.glb"] = glb;
            ok4 = ok4 && car_import(file, loader(fb), stem + "_blender.glb", side, {}, bb, &br, &why);
            if (ok4 && bb.size() != file.size()) { ok4 = false; why = "the size changed"; }
            CarFile a, b;
            ok4 = ok4 && car_read(file, a) && car_read(bb, b) && a.meshes.size() == b.meshes.size();
            double worst = 0;
            for (std::size_t i = 0; ok4 && i < a.meshes.size(); ++i) {
                const CarMesh& x = a.meshes[i];
                const CarMesh& y = b.meshes[i];
                const bool body = x.lod == 3 && x.slot == 0;
                ok4 = x.v.size() == y.v.size() && x.tri == y.tri;
                for (std::size_t k = 0; ok4 && k < x.v.size(); ++k)
                    for (int q = 0; q < 3; ++q) {
                        const double want = body ? double(x.v[k].pos[q]) * 1.3 : double(x.v[k].pos[q]);
                        worst = std::max(worst, std::fabs(double(y.v[k].pos[q]) - want));
                        if (!body) ok4 = ok4 && y.v[k].pos[q] == x.v[k].pos[q];
                    }
                if (!ok4) why = "LOD" + std::to_string(x.lod) + " slot " + std::to_string(x.slot) + " entry " + std::to_string(x.entry) + " differs";
            }
            ok4 = ok4 && worst < 1e-5;
            blender_scale_ok += ok4;
            if (!ok4) ++failures;
            char t[160];
            std::snprintf(t, sizeof t, "; body 1.3x through Blender %s (%zu edited, worst %.1e m)", ok4 ? "ok" : ("FAILED " + why).c_str(), br.edited, worst);
            line += t;
        }
        // ---- edit: LOD 3's body 1.3x, paint 0 red <-> blue ----
        FileSet ed = fsx;
        bool e_ok = scale_meshes(ed, stem, "LOD3_L0_", 1.3f);
        b3asset::Image p0;
        Buf& png = ed[stem + "_paint0.png"];
        e_ok = e_ok && decode_png(png, p0.w, p0.h, p0.rgba);
        for (std::size_t i = 0; e_ok && i < p0.rgba.size(); i += 4) std::swap(p0.rgba[i], p0.rgba[i + 2]);
        if (e_ok) png = encode_png(p0.w, p0.h, p0.rgba.data());
        Buf eb;
        CarImportReport er;
        if (!e_ok || !car_import(file, loader(ed), model, side, {}, eb, &er, &why)) {
            ++failures;
            line += "; edit FAILED: " + (e_ok ? why : std::string("the test edit"));
        } else {
            CarFile a, b;
            bool geo = car_read(file, a) && car_read(eb, b) && eb.size() == file.size() && a.meshes.size() == b.meshes.size();
            for (std::size_t i = 0; geo && i < a.meshes.size(); ++i) {
                const CarMesh& x = a.meshes[i];
                const CarMesh& y = b.meshes[i];
                const bool body = x.lod == 3 && x.slot == 0;
                geo = x.v.size() == y.v.size() && x.tri == y.tri;
                for (std::size_t k = 0; geo && k < x.v.size(); ++k)
                    for (int q = 0; q < 3; ++q) geo = geo && y.v[k].pos[q] == (body ? x.v[k].pos[q] * 1.3f : x.v[k].pos[q]);
            }
            edit_ok += geo;
            // paints: 0 is the swapped picture, the others as before
            bool pt = er.paints_changed == 1 && !er.new_palette_indices;
            const std::uint32_t tex = a.tex_at;
            for (int p = 0; pt && p < a.palettes; ++p) {
                b3asset::GsMemory g1, g2;
                b3asset::DecodedRaster d1, d2;
                pt = b3asset::decode_raster(b3asset::Bytes{file.data(), file.size()}, tex, g1, d1, p) &&
                     b3asset::decode_raster(b3asset::Bytes{eb.data(), eb.size()}, tex, g2, d2, p);
                if (!pt) break;
                if (p == 0) {
                    for (std::size_t i = 0; pt && i < p0.rgba.size(); i += 4)
                        pt = !std::memcmp(&d2.levels[0].rgba[i], &p0.rgba[i], 3) && std::abs(int(d2.levels[0].rgba[i + 3]) - int(p0.rgba[i + 3])) <= 2;
                } else {
                    pt = d1.levels[0].rgba == d2.levels[0].rgba;
                }
            }
            paint_ok += pt;
            if (!geo || !pt) { ++failures; keep(eb, "edit"); }
            line += std::string("; edit: body 1.3x ") + (geo ? "exact" : "DIFFERS") + ", paint 0 recoloured " + (pt ? "exact" : "DIFFERS");
        }
        std::printf("%-20s %-26s%s\n", c.key.c_str(), c.name.c_str(), line.c_str());
        std::fflush(stdout);
    }
    std::printf("identity: %zu of %zu cars byte-identical after export -> import\n", identical, cars);
    std::printf("restrip: %zu of %zu cars decode-identical (%zu of %zu entries' triangle sets, %zu of %zu LODs' drawn triangles and bounds); "
                "%llu vertices in the strips against the disc's %llu (%+.1f %%); %zu files grow (worst +%.1f %%), %zu of %zu within the %zu-byte limit\n",
                restrip_ok, cars, entries_same, entries, lods_same, lods, (unsigned long long)v_restrip, (unsigned long long)v_disc,
                v_disc ? 100.0 * (double(v_restrip) / double(v_disc) - 1.0) : 0.0, bigger, 100 * worst_growth, restrip_fit, cars, kCarMaxBgvSize);
    std::printf("re-save: %zu of %zu cars byte-identical with every mesh's vertices and triangles shuffled (a modelling tool's order)\n", reorder_ok, cars);
    std::printf("blender: %zu of %zu cars byte-identical through Blender's glTF import and export (as blender_resave models them), "
                "%zu of %zu with LOD 3's body scaled 1.3x there in object mode (those positions, the same size); "
                "%zu of %zu an export in the old attribute layout (an older b3modtool's) identical by the import and refused by Blender's importer\n",
                blender_ok, cars, blender_scale_ok, cars, blender_legacy_ok, cars);
    std::printf("topology: %zu of %zu cars decode-identical with a quad added to LOD 3's first body mesh (its strips rebuilt), %zu of %zu within the limit\n",
                topo_ok, cars, topo_fit, cars);
    std::printf("edit: %zu of %zu cars with LOD 3's body scaled exactly, %zu of %zu with paint 0 recoloured exactly (the other paints unchanged)\n",
                edit_ok, cars, paint_ok, cars);
    std::printf("model_import_check: %s\n", failures || !cars ? "FAIL" : "PASS");
    return failures || !cars ? 1 : 0;
}
