// track_import_check: the census of mod kit part 3c's track import (docs/modding/b3modtool.md "track import").
//   build/track_import_check <disc> [out dir] [route ...]
// For every route (or the routes named: TRACKS/US/C1_V1 or US/C1_V1), in memory, as `b3modtool track export` and
// `track import` do:
//   writer      every mesh the files name (streamed high and LOD, backdrops and water, instance and prop models): its
//               chains decoded with every stored lane and written again: the same bytes
//   identity    export (glTF, every section) -> import unchanged: the bytes of the disc's STATIC.DAT and STREAMED.DAT
//   re-save     every mesh's vertices and triangles shuffled (a modelling tool's order): the same bytes
//   restrip     every mesh's strips rebuilt from its triangles (what an import of new triangles does): decode-identical
//               -- each mesh's triangle set with every stored field the same, and the renderer's decode (b3asset::
//               read_track, high detail and LOD) the same drawn triangles and bounds; the blocks against their slots
//   edit        the middle section raised 2 m and its colours red <-> blue (COLOR_0 and _B3_COLOR_R / _B) in the glTF: the
//               import's files decode to exactly that there, every other mesh as before, the same sizes
//   quad        a quad (two triangles) added to that section's first mesh: its strips rebuilt, decode-identical to
//               the mesh with the quad
//   textures    the largest texture's PNG recoloured (red <-> blue): written into its raster (99 % or more of its texels
//               exact), nothing else changed; a material given another of the route's textures in the glTF
// Writes nothing but [out dir] (the first failing route's files, for a look; the game's data: delete it after).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "b3asset/gs_memory.h"
#include "b3asset/raster.h"
#include "b3asset/track.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"

using namespace b3modtool;
namespace fs = std::filesystem;

namespace {

std::uint32_t rd32(const Buf& b, std::size_t at) { return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24; }

bool same_span(const Buf& f, std::size_t at, const Buf& nb) {
    return at + nb.size() <= f.size() && std::equal(nb.begin(), nb.end(), f.begin() + std::ptrdiff_t(at)) &&
           nb.size() == 16 + 16 * std::size_t(rd32(f, at) & 0xFFFF);
}

FileLoader loader(const FileSet& fsx) {
    return [&fsx](const std::string& p, Buf& out) {
        auto it = fsx.find(p);
        if (it == fsx.end()) return false;
        out = it->second;
        return true;
    };
}

std::string hexs(std::size_t v) {
    char t[24];
    std::snprintf(t, sizeof t, "0x%zX", v);
    return t;
}

// every site's mesh as decoded (in the sites' order)
bool decode_all(const Buf& st, const Buf& sm, std::vector<TrackSite>& sites, std::vector<TrackMesh>& out, std::string* why) {
    TrackFiles tf;
    if (!track_files_read(st, sm, tf, why)) return false;
    sites = tf.sites;
    out.resize(sites.size());
    for (std::size_t i = 0; i < sites.size(); ++i)
        if (!track_mesh_read(sites[i].in_static ? st : sm, sites[i].chain1, sites[i].chain2, out[i], why)) return false;
    return true;
}

// the renderer's decode: drawn triangles (two or more distinct positions) and bounds, high detail and LOD
struct Drawn { std::size_t tris = 0; float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0}; bool ok = false; };
Drawn drawn(const Buf& st, const Buf& sm, bool lod) {
    Drawn d;
    b3asset::GsMemory gs;
    b3asset::Model m;
    b3asset::TrackOptions to;
    to.lod = lod;
    to.extra_backdrops = true;
    if (!b3asset::read_track(b3asset::Bytes{st.data(), st.size()}, b3asset::Bytes{sm.data(), sm.size()}, to, gs, m)) return d;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const float* a = m.vertices[m.indices[t]].pos; const float* b = m.vertices[m.indices[t + 1]].pos; const float* c = m.vertices[m.indices[t + 2]].pos;
        if (std::memcmp(a, b, 12) && std::memcmp(b, c, 12) && std::memcmp(a, c, 12)) ++d.tris;
    }
    std::memcpy(d.bmin, m.bmin, 12);
    std::memcpy(d.bmax, m.bmax, 12);
    d.ok = true;
    return d;
}

// the glTF accessor's place in the buffer (a float or byte attribute the export wrote: tightly packed)
std::size_t accessor_at(const Json& doc, int ai) {
    const Json& a = doc.get("accessors")->a[std::size_t(ai)];
    const Json& v = doc.get("bufferViews")->a[std::size_t(a.get("bufferView")->as_int())];
    return std::size_t((v.get("byteOffset") ? v.get("byteOffset")->as_int() : 0) + (a.get("byteOffset") ? a.get("byteOffset")->as_int() : 0));
}

// the test edit: section `unit`'s high-detail mesh raised `dy` and its colours red <-> blue, in the glTF and its buffer
bool edit_section(FileSet& fsx, const std::string& stem, int unit, float dy, std::size_t& prims) {
    Json doc;
    Buf& t = fsx[stem + ".gltf"];
    if (!json_parse(std::string_view(reinterpret_cast<const char*>(t.data()), t.size()), doc)) return false;
    Buf& bin = fsx[stem + ".bin"];
    char name[32];
    std::snprintf(name, sizeof name, "section_%03d", unit);
    prims = 0;
    for (const Json& m : doc.get("meshes")->a) {
        if (m.get("name")->s != name) continue;
        for (const Json& p : m.get("primitives")->a) {
            const Json* at = p.get("attributes");
            const int pa = int(at->get("POSITION")->as_int());
            const std::size_t n = std::size_t(doc.get("accessors")->a[std::size_t(pa)].get("count")->as_int());
            const std::size_t po = accessor_at(doc, pa);
            for (std::size_t i = 0; i < n; ++i) {
                float y;
                std::memcpy(&y, &bin[po + 12 * i + 4], 4);
                y += dy;
                std::memcpy(&bin[po + 12 * i + 4], &y, 4);
            }
            if (const Json* c = at->get("COLOR_0")) {
                const std::size_t co = accessor_at(doc, int(c->as_int()));
                for (std::size_t i = 0; i < n; ++i) std::swap(bin[co + 4 * i], bin[co + 4 * i + 2]);
            }
            const Json* cr = at->get("_B3_COLOR_R");
            const Json* cb = at->get("_B3_COLOR_B");
            if (cr && cb) {   // two float SCALARs: their values swapped
                const std::size_t ro = accessor_at(doc, int(cr->as_int())), bo = accessor_at(doc, int(cb->as_int()));
                for (std::size_t i = 0; i < n; ++i)
                    for (std::size_t k = 0; k < 4; ++k) std::swap(bin[ro + 4 * i + k], bin[bo + 4 * i + k]);
            }
            ++prims;
        }
    }
    return prims > 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: track_import_check <disc> [out dir] [route ...]\n");
        return 2;
    }
    const std::string game = argv[1], out = argc > 2 ? argv[2] : "";
    std::vector<std::string> want;
    for (int i = 3; i < argc; ++i) {
        std::string k = argv[i];
        if (k.rfind("TRACKS/", 0) != 0) k = "TRACKS/" + k;
        want.push_back(k);
    }
    std::size_t routes = 0, w_meshes = 0, w_same = 0, identical = 0, resave_ok = 0, restrip_ok = 0, restrip_fit = 0, m_all = 0, m_same = 0;
    std::size_t edit_ok = 0, quad_ok = 0, grown = 0, sections = 0, tex_ok = 0, swap_ok = 0;
    std::uint64_t v_disc = 0, v_restrip = 0;
    int failures = 0;
    bool kept = false;
    for (const RouteEntry& rt : route_list(game)) {
        if (!want.empty() && std::find(want.begin(), want.end(), rt.key) == want.end()) continue;
        Buf st, sm, en;
        if (!read_host_file(rt.static_path, st) || (!rt.streamed_path.empty() && !read_host_file(rt.streamed_path, sm))) {
            std::printf("%s: cannot read\n", rt.key.c_str());
            ++failures;
            continue;
        }
        ++routes;
        std::string why, line;
        auto keep = [&](const FileSet& fsx, const Buf& s2, const Buf& m2, const char* what) {
            if (kept || out.empty()) return;
            kept = true;
            const fs::path d = fs::path(out) / rt.key.substr(7);
            for (const auto& [n, b] : fsx) write_host_file(b3mod::path_utf8(d / n), b);
            write_host_file(b3mod::path_utf8(d / (std::string(what) + "_STATIC.DAT")), s2);
            write_host_file(b3mod::path_utf8(d / (std::string(what) + "_STREAMED.DAT")), m2);
            std::printf("  (%s's export and its %s import kept in %s)\n", rt.key.c_str(), what, b3mod::path_utf8(d).c_str());
        };
        std::vector<TrackSite> sites;
        std::vector<TrackMesh> orig;
        TrackFiles tf0;
        track_files_read(st, sm, tf0);
        if (!decode_all(st, sm, sites, orig, &why)) { std::printf("%-18s decode: %s\n", rt.key.c_str(), why.c_str()); ++failures; continue; }
        // ---- the writer: every mesh's chains decoded and written again ----
        {
            std::size_t meshes = 0, same = 0;
            std::set<std::pair<bool, std::size_t>> done;
            for (std::size_t i = 0; i < sites.size(); ++i) {
                const TrackSite& s = sites[i];
                if (!done.insert({s.in_static, s.chain1}).second) continue;
                ++meshes;
                const Buf& f = s.in_static ? st : sm;
                Buf a, b;
                same += track_encode_mesh(orig[i], a, b, &why) && same_span(f, s.chain1, a) && (s.chain2 ? same_span(f, s.chain2, b) : b.empty());
            }
            w_meshes += meshes;
            w_same += same;
            if (same != meshes) ++failures;
            char t[96];
            std::snprintf(t, sizeof t, " writer %zu/%zu", same, meshes);
            line += t;
        }
        // ---- the export ----
        std::string stem = rt.key.substr(7);
        std::replace(stem.begin(), stem.end(), '/', '_');
        TrackExportOptions eo;
        eo.source = rt.key;
        FileSet fsx;
        if (!rt.enviro_path.empty()) read_host_file(rt.enviro_path, en);
        if (!track_export(st, sm, en, stem, eo, fsx, nullptr, &why)) { std::printf("%-18s export: %s\n", rt.key.c_str(), why.c_str()); ++failures; continue; }
        const std::string model = stem + ".gltf", side = stem + ".b3track.json";
        // ---- identity ----
        Buf s2, m2;
        TrackImportReport rep;
        bool ok = track_import(st, sm, loader(fsx), model, side, {}, s2, m2, &rep, &why);
        const bool same = ok && s2 == st && m2 == sm;
        identical += same;
        if (!same) {
            ++failures;
            line += ok ? " identity DIFFERS (" + std::string(s2 != st ? "STATIC" : "STREAMED") + ", " + std::to_string(rep.edited) + " edited, " + std::to_string(rep.restripped) +
                             " restripped, " + std::to_string(rep.textures_changed) + " textures, " + std::to_string(rep.textures_swapped) + " swaps)"
                       : " identity FAILED: " + why;
            if (ok) keep(fsx, s2, m2, "identity");
        } else {
            char t[96];
            std::snprintf(t, sizeof t, "; identical (%zu meshes in the model, all unchanged)", rep.meshes);
            line += t;
        }
        Json sidej;
        json_parse(std::string_view(reinterpret_cast<const char*>(fsx[side].data()), fsx[side].size()), sidej);
        // ---- re-save: every mesh's vertices and triangles in another order ----
        {
            std::vector<TrackImportMesh> ms;
            std::vector<TrackTextureEdit> sw;
            bool ok2 = track_model_read(loader(fsx), model, sidej, st, sm, ms, sw, nullptr, &why);
            std::mt19937 rng(static_cast<std::uint32_t>(routes));
            for (TrackImportMesh& m : ms) {
                std::vector<std::uint32_t> perm(m.v.size());
                std::iota(perm.begin(), perm.end(), 0u);
                std::shuffle(perm.begin(), perm.end(), rng);
                std::vector<TrackVertex> nv(m.v.size());
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
            Buf s3, m3;
            TrackImportReport r3;
            ok2 = ok2 && track_import_meshes(st, sm, ms, sw, {}, {}, s3, m3, &r3, &why) && s3 == st && m3 == sm;
            resave_ok += ok2;
            if (!ok2) ++failures;
            line += std::string("; re-saved in another order ") + (ok2 ? "identical" : "DIFFERS (" + std::to_string(r3.edited) + " edited, " + std::to_string(r3.restripped) + " restripped: " + why + ")");
        }
        // ---- restrip: decode-identical ----
        {
            TrackImportOptions ro;
            ro.restrip = true;
            ro.no_limits = true;
            Buf s3, m3;
            TrackImportReport r3;
            if (!track_import(st, sm, loader(fsx), model, side, ro, s3, m3, &r3, &why)) {
                ++failures;
                line += "; restrip FAILED: " + why;
            } else {
                std::vector<TrackSite> sites2;
                std::vector<TrackMesh> back;
                bool dec = decode_all(s3, m3, sites2, back, &why) && sites2.size() == sites.size();
                std::size_t same_m = 0;
                for (std::size_t i = 0; dec && i < sites.size(); ++i) {
                    same_m += track_mesh_hash(orig[i].v, orig[i].tri, orig[i].layout) == track_mesh_hash(back[i].v, back[i].tri, back[i].layout) && orig[i].layout == back[i].layout;
                    v_disc += orig[i].v.size();
                    v_restrip += back[i].v.size();
                }
                m_all += sites.size();
                m_same += same_m;
                dec = dec && same_m == sites.size();
                const Drawn a = drawn(st, sm, false), b = drawn(s3, m3, false), c = drawn(st, sm, true), d = drawn(s3, m3, true);
                const bool rd = a.ok && b.ok && a.tris == b.tris && !std::memcmp(a.bmin, b.bmin, 12) && !std::memcmp(a.bmax, b.bmax, 12) && c.ok && d.ok && c.tris == d.tris &&
                                !std::memcmp(c.bmin, d.bmin, 12) && !std::memcmp(c.bmax, d.bmax, 12);
                dec = dec && rd;
                restrip_ok += dec;
                // within the game's limits: every block within its slot, STATIC.DAT within its block
                bool fit = s3.size() <= std::max(kTrackStaticBlock, st.size());
                TrackFiles t3;
                if (track_files_read(s3, m3, t3))
                    for (std::size_t k = 0; k < t3.blocks.size(); ++k) fit = fit && t3.blocks[k].size <= ((k & 1) ? kTrackLodSlot : kTrackHighSlot);
                restrip_fit += fit;
                grown += r3.blocks_grown;
                if (!dec) { ++failures; keep(fsx, s3, m3, "restrip"); }
                char t[220];
                std::snprintf(t, sizeof t, "; restrip %s (%zu/%zu meshes, renderer %s), %zu blocks grown, STATIC %zu -> %zu, STREAMED %zu -> %zu%s", dec ? "decode-identical" : "DIFFERS",
                              same_m, sites.size(), rd ? "same" : "DIFFERS", r3.blocks_grown, st.size(), s3.size(), sm.size(), m3.size(), fit ? "" : " (over a limit)");
                line += t;
            }
        }
        // ---- textures and materials: the largest texture's PNG recoloured (red <-> blue); a textured material given
        // another texture; a backdrop mesh given another material of its kind ----
        {
            const Json* tl = sidej.get("textures");
            std::string tline;
            bool t_ok = tl && tl->size() > 0 && !tf0.textures.empty();
            int big = -1;
            std::size_t area = 0;
            for (std::size_t k = 0; t_ok && k < tl->size(); ++k) {
                b3asset::Image im;
                if (!decode_png(fsx[tl->a[k].s], im.w, im.h, im.rgba)) continue;
                if (std::size_t(im.w) * std::size_t(im.h) > area) { area = std::size_t(im.w) * std::size_t(im.h); big = int(k); }
            }
            t_ok = t_ok && big >= 0;
            FileSet ed = fsx;
            b3asset::Image want;
            if (t_ok) {
                decode_png(ed[tl->a[std::size_t(big)].s], want.w, want.h, want.rgba);
                for (std::size_t i = 0; i < want.rgba.size(); i += 4) std::swap(want.rgba[i], want.rgba[i + 2]);
                ed[tl->a[std::size_t(big)].s] = encode_png(want.w, want.h, want.rgba.data());
            }
            Buf s3, m3;
            TrackImportReport r3;
            t_ok = t_ok && track_import(st, sm, loader(ed), model, side, {}, s3, m3, &r3, &why) && m3 == sm && s3.size() == st.size();
            std::size_t exact = 0;
            if (t_ok) {
                b3asset::GsMemory g;
                b3asset::DecodedRaster d;
                t_ok = r3.textures_changed == 1 && b3asset::decode_raster(b3asset::Bytes{s3.data(), s3.size()}, tf0.textures[std::size_t(big)], g, d) &&
                       d.levels[0].rgba.size() == want.rgba.size();
                for (std::size_t i = 0; t_ok && i < want.rgba.size(); i += 4) exact += !std::memcmp(&d.levels[0].rgba[i], &want.rgba[i], 4);
                t_ok = t_ok && exact * 100 >= area * 99;
            }
            char t[200];
            std::snprintf(t, sizeof t, "; texture %d recoloured %s (%.1f %% of %zu texels exact)", big, t_ok ? "ok" : ("FAILED " + why).c_str(), area ? 100.0 * double(exact) / double(area) : 0.0, area);
            tline += t;
            tex_ok += t_ok;
            if (!t_ok) ++failures;
            // a texture swap and a material swap in the glTF
            Json doc;
            ed = fsx;
            bool s_ok = json_parse(std::string_view(reinterpret_cast<const char*>(ed[model].data()), ed[model].size()), doc);
            int gm_a = -1, gm_b = -1, mat_a = -1, img_b = -1, tex_b = -1;
            const Json* gms = doc.get("materials");
            for (std::size_t i = 0; s_ok && gms && i < gms->size(); ++i) {
                const Json* ex = gms->a[i].get("extras");
                const Json* b3 = ex ? ex->get("b3") : nullptr;
                const Json* pbr = gms->a[i].get("pbrMetallicRoughness");
                if (!b3 || !pbr || !pbr->get("baseColorTexture")) continue;
                const int mi = int(b3->get("material")->as_int(-1));
                const auto& mt = tf0.materials[std::size_t(mi)];
                bool animated = false;
                if (const Json* an = sidej.get("animated_materials"))
                    for (const Json& a : an->a) animated = animated || a.get("material")->as_int() == mi;
                if (animated || mt.type != 0) continue;
                if (gm_a < 0) { gm_a = int(i); mat_a = mi; continue; }
                if (tf0.materials[std::size_t(mi)].texture != tf0.materials[std::size_t(mat_a)].texture && gm_b < 0) {
                    gm_b = int(i);
                    tex_b = tf0.materials[std::size_t(mi)].texture;
                    img_b = int(doc.get("textures")->a[std::size_t(pbr->get("baseColorTexture")->get("index")->as_int())].get("source")->as_int());
                }
            }
            s_ok = s_ok && gm_a >= 0 && gm_b >= 0;
            if (s_ok) {
                // material A's texture -> B's image (every glTF material of A: its one- and two-sided forms)
                for (std::size_t i = 0; i < gms->size(); ++i) {
                    const Json* ex = gms->a[i].get("extras");
                    if (!ex || !ex->get("b3") || ex->get("b3")->get("material")->as_int() != mat_a) continue;
                    Json* bt = doc.get("materials")->a[i].get("pbrMetallicRoughness")->get("baseColorTexture");
                    Json* texs = doc.get("textures");
                    Json nt = Json::object();
                    nt.set("source", Json::integer(img_b));
                    texs->push(nt);
                    bt->set("index", Json::integer(std::int64_t(texs->size() - 1)));
                }
                const std::string txt = doc.dump();
                ed[model] = Buf(txt.begin(), txt.end());
                s_ok = track_import(st, sm, loader(ed), model, side, {}, s3, m3, &r3, &why) && r3.textures_swapped == 1 && s3.size() == st.size() && m3 == sm;
                TrackFiles t3;
                s_ok = s_ok && track_files_read(s3, m3, t3, &why) && t3.materials[std::size_t(mat_a)].texture == tex_b;
                for (std::size_t k = 0; s_ok && k < t3.materials.size(); ++k)
                    if (int(k) != mat_a) s_ok = t3.materials[k].texture == tf0.materials[k].texture;
            }
            std::snprintf(t, sizeof t, "; material %d given texture %d %s", mat_a, tex_b, s_ok ? "ok" : ("FAILED " + why).c_str());
            tline += t;
            swap_ok += s_ok;
            if (!s_ok) ++failures;
            line += tline;
        }
        // ---- edit: the middle section raised 2 m, red <-> blue ----
        if (!sm.empty()) {
            ++sections;
            const int units = int(std::int16_t(st[0x54] | st[0x55] << 8));
            const int unit = units / 2;
            FileSet ed = fsx;
            std::size_t prims = 0;
            bool e_ok = edit_section(ed, stem, unit, 2.0f, prims);
            Buf s3, m3;
            TrackImportReport r3;
            e_ok = e_ok && track_import(st, sm, loader(ed), model, side, {}, s3, m3, &r3, &why);
            std::vector<TrackSite> sites2;
            std::vector<TrackMesh> back;
            e_ok = e_ok && s3.size() == st.size() && m3.size() == sm.size() && decode_all(s3, m3, sites2, back, &why) && sites2.size() == sites.size();
            std::size_t changed = 0;
            for (std::size_t i = 0; e_ok && i < sites.size(); ++i) {
                const bool target = sites[i].kind == TrackSite::kHigh && sites[i].unit == unit && sites[i].material >= 0 &&
                                    std::size_t(sites[i].material) < tf0.materials.size() && tf0.materials[std::size_t(sites[i].material)].type != 3;
                const TrackMesh& a = orig[i];
                const TrackMesh& b = back[i];
                // the same strips, and the drawn triangles exactly the edited ones (a vertex no triangle uses, an ADC
                // filler the export leaves out, keeps its place)
                e_ok = a.v.size() == b.v.size() && a.tri == b.tri && a.batches == b.batches;
                std::vector<TrackVertex> want_v = a.v;
                for (TrackVertex& x : want_v)
                    if (target) {
                        x.pos[1] += 2.0f;
                        std::swap(x.col[0], x.col[2]);
                    }
                e_ok = e_ok && track_mesh_hash(want_v, a.tri, a.layout) == track_mesh_hash(b.v, b.tri, b.layout);
                for (std::size_t k = 0; e_ok && !target && k < a.v.size(); ++k) e_ok = !std::memcmp(&a.v[k], &b.v[k], sizeof(TrackVertex));
                changed += target;
                if (!e_ok) why = sites[i].kind == TrackSite::kHigh ? "section " + std::to_string(sites[i].unit) + " chain " + hexs(sites[i].chain1) + " differs" : "chain " + hexs(sites[i].chain1) + " differs";
            }
            edit_ok += e_ok;
            if (!e_ok) { ++failures; keep(ed, s3, m3, "edit"); }
            char t[200];
            std::snprintf(t, sizeof t, "; edit: section %d (%zu meshes) raised 2 m and recoloured %s (%zu boxes grown)", unit, changed, e_ok ? "exact" : ("DIFFERS: " + why).c_str(), r3.boxes_grown);
            line += t;
            // ---- quad: two triangles more in that section's first mesh ----
            std::vector<TrackImportMesh> ms;
            std::vector<TrackTextureEdit> sw;
            bool q_ok = track_model_read(loader(fsx), model, sidej, st, sm, ms, sw, nullptr, &why);
            TrackImportMesh* tgt = nullptr;
            std::size_t tgt_site = 0;
            for (TrackImportMesh& m : ms)
                for (std::size_t i = 0; !tgt && i < sites.size(); ++i)
                    if (sites[i].kind == TrackSite::kHigh && sites[i].unit == unit && !sites[i].in_static && !m.in_static && sites[i].chain1 == m.chain1 && !m.tri.empty()) { tgt = &m; tgt_site = i; }
            q_ok = q_ok && tgt;
            if (q_ok) {
                const std::uint32_t b0 = std::uint32_t(tgt->v.size());
                const TrackVertex base = tgt->v[tgt->tri[0]];
                for (int q = 0; q < 4; ++q) {
                    TrackVertex v = base;
                    v.pos[0] += 1.0f * float(q & 1);
                    v.pos[1] += 3.0f;
                    v.pos[2] += 1.0f * float(q >> 1);
                    tgt->v.push_back(v);
                }
                tgt->tri.insert(tgt->tri.end(), {b0, b0 + 2, b0 + 1, b0 + 1, b0 + 2, b0 + 3});
                Buf s4, m4;
                TrackImportReport r4;
                q_ok = track_import_meshes(st, sm, ms, sw, {}, {}, s4, m4, &r4, &why) && r4.restripped == 1;
                std::vector<TrackSite> sites3;
                std::vector<TrackMesh> back3;
                q_ok = q_ok && decode_all(s4, m4, sites3, back3, &why) && sites3.size() == sites.size();
                for (std::size_t i = 0; q_ok && i < sites.size(); ++i) {
                    if (sites[i].in_static == sites[tgt_site].in_static && sites[i].chain1 == sites[tgt_site].chain1)
                        q_ok = track_mesh_hash(back3[i].v, back3[i].tri, back3[i].layout) == track_mesh_hash(tgt->v, tgt->tri, orig[i].layout);
                    else
                        q_ok = track_mesh_hash(back3[i].v, back3[i].tri, back3[i].layout) == track_mesh_hash(orig[i].v, orig[i].tri, orig[i].layout);
                    if (!q_ok) why = "chain " + hexs(sites[i].chain1) + (i == tgt_site ? " (the edited mesh)" : "") + " differs";
                }
                char t2[160];
                std::snprintf(t2, sizeof t2, "; a quad more %s (%zu packets moved, %zu blocks grown)", q_ok ? "decode-identical" : ("DIFFERS: " + why).c_str(), r4.moved, r4.blocks_grown);
                line += t2;
            } else {
                line += "; a quad more: FAILED " + why;
            }
            quad_ok += q_ok;
            if (!q_ok) ++failures;
        }
        std::printf("%-18s%s\n", rt.key.c_str(), line.c_str());
        std::fflush(stdout);
    }
    std::printf("writer: %zu of %zu meshes' chains written again byte for byte in %zu routes\n", w_same, w_meshes, routes);
    std::printf("identity: %zu of %zu routes byte-identical (STATIC.DAT and STREAMED.DAT) after export -> import\n", identical, routes);
    std::printf("re-save: %zu of %zu routes byte-identical with every mesh's vertices and triangles shuffled (a modelling tool's order)\n", resave_ok, routes);
    std::printf("restrip: %zu of %zu routes decode-identical (%zu of %zu meshes' triangle sets with every stored field; the renderer's decode, high and LOD, "
                "the same drawn triangles and bounds); %llu vertices in the strips against the disc's %llu (%+.1f %%); %zu blocks grew; %zu of %zu routes within "
                "the game's limits\n", restrip_ok, routes, m_same, m_all, (unsigned long long)v_restrip, (unsigned long long)v_disc,
                v_disc ? 100.0 * (double(v_restrip) / double(v_disc) - 1.0) : 0.0, grown, restrip_fit, routes);
    std::printf("edit: %zu of %zu routes with the middle section raised 2 m and recoloured exactly (every other mesh as before, the same sizes)\n", edit_ok, sections);
    std::printf("quad: %zu of %zu routes decode-identical with a quad added to that section's first mesh (its strips rebuilt)\n", quad_ok, sections);
    std::printf("textures: %zu of %zu routes with their largest texture's PNG recoloured written back (99 %% of its texels or more exact, nothing else changed); "
                "%zu of %zu with a material's texture swapped for another of the route's\n", tex_ok, routes, swap_ok, routes);
    std::printf("track_import_check: %s\n", failures || !routes ? "FAIL" : "PASS");
    return failures || !routes ? 1 : 0;
}
