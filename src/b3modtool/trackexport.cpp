// Track export: TRACKS/<area>/<route>/{STATIC.DAT, STREAMED.DAT, ENVIRO.DAT} to glTF 2.0 or OBJ, split by streamed
// unit ("section") (include/b3modtool/export3d.h; the layout docs/track-format.md, the reader b3asset::read_track).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "b3asset/enviro.h"
#include "b3asset/gs_memory.h"
#include "b3asset/track.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"

namespace b3modtool {

namespace {

using b3asset::Bytes;

bool fail(std::string* why, const std::string& m) { if (why) *why = m; return false; }

std::string hex32(std::uint32_t v) {
    char t[16];
    std::snprintf(t, sizeof t, "0x%08X", v);
    return t;
}

std::string hexbytes(const std::uint8_t* p, std::size_t n) {
    std::string s;
    char t[4];
    for (std::size_t i = 0; i < n; ++i) { std::snprintf(t, sizeof t, "%02x", p[i]); s += t; }
    return s;
}

struct Batch { std::uint32_t base = 0, n = 0; std::vector<std::uint32_t> w; std::vector<std::array<float, 4>> light; };

// A draw's place in the export
struct DrawInfo {
    b3asset::TrackOptions::MeshRef ref;
    std::vector<std::uint32_t> batches;   // vertex counts (the decoded copy's)
    std::string group;                    // "section_012", "backdrop_3", "water_0", ...
    int kind = 0;                         // 0 world-space mesh, 1 instance placement, 2 prop placement
    int type = -1;                        // instance / prop type index
};

}  // namespace

bool track_export(const Buf& static_dat, const Buf& streamed_dat, const Buf& enviro, const std::string& stem,
                  const TrackExportOptions& opt, FileSet& out, ExportReport* rep, std::string* why) {
    const Bytes st{static_dat.data(), static_dat.size()}, sm{streamed_dat.data(), streamed_dat.size()};
    if (static_dat.size() < 0x60 || st.u32(0) != 0x27) return fail(why, "not a version 0x27 STATIC.DAT");
    const int nunits = std::int16_t(st.u16(0x54));
    if (opt.section >= 0 && (opt.section >= nunits || streamed_dat.empty()))
        return fail(why, "no section " + std::to_string(opt.section) + " (the route has " + std::to_string(streamed_dat.empty() ? 0 : nunits) + " streamed units)");
    // Two passes of the renderer's reader: the high-detail meshes with everything placed on them, then the units' LOD
    // meshes (the far view); the second is appended to the first (same STATIC.DAT: same texture and material tables).
    b3asset::GsMemory gs;
    std::vector<b3asset::TrackMaterialInfo> mats;
    b3asset::Model m;
    std::vector<DrawInfo> info;
    std::vector<std::uint32_t> wraw;
    std::vector<std::array<float, 4>> lraw;
    std::size_t lod_draw0 = 0;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1 && streamed_dat.empty()) break;
        b3asset::TrackOptions to;
        to.streamed = true;
        to.lod = pass == 1;
        to.backdrops = pass == 0 && opt.section < 0;
        to.extra_backdrops = true;
        to.instances = to.props = pass == 0;
        to.lines = false;
        if (opt.section >= 0) { to.first_unit = opt.section; to.unit_count = 1; }
        b3asset::Model pm;
        std::vector<Batch> pending, all_batches;
        std::vector<DrawInfo> pinfo;
        to.on_batch = [&](const b3asset::VuMemory& vu, std::uint32_t itop, const std::map<std::uint32_t, int>& un, int, int) {
            auto it = un.find(0);
            if (!itop || it == un.end() || it->second != 0xC) return;   // as read_track: batches without positions add nothing
            Batch bt;
            bt.base = std::uint32_t(pm.vertices.size());
            bt.n = itop;
            for (std::uint32_t i = 0; i < itop; ++i) bt.w.push_back(vu.qw(3 * i)[3]);
            // type 5: the lighting vectors (V4-32 at 2, transposed by four: qw 3 (4g + c) + 2 lane j = component c of
            // vertex 4g + j), which the renderer's decode does not keep
            if (auto l = un.find(2); l != un.end() && l->second == 0xC)
                for (std::uint32_t i = 0; i < itop; ++i) {
                    std::array<float, 4> x{};
                    for (int c = 0; c < 4; ++c) x[std::size_t(c)] = vu.f(3 * (4 * (i / 4) + std::uint32_t(c)) + 2, int(i % 4));
                    bt.light.push_back(x);
                }
            pending.push_back(std::move(bt));
        };
        std::map<int, std::vector<std::uint32_t>> batches_of_draw;
        to.on_mesh = [&](const b3asset::TrackOptions::MeshRef& ref) {
            if (!ref.shared) {
                if (ref.draw >= 0) for (const Batch& bt : pending) batches_of_draw[ref.draw].push_back(bt.n);
                for (Batch& bt : pending) all_batches.push_back(std::move(bt));
                pending.clear();
            }
            if (ref.draw >= 0) {
                if (std::size_t(ref.draw) >= pinfo.size()) pinfo.resize(std::size_t(ref.draw) + 1);
                pinfo[std::size_t(ref.draw)].ref = ref;
            }
        };
        std::string err;
        if (!b3asset::read_track(st, sm, to, gs, pm, &err, pass == 0 ? &mats : nullptr)) return fail(why, err);
        if (pinfo.size() != pm.draws.size()) return fail(why, "internal: " + std::to_string(pinfo.size()) + " mesh records for " + std::to_string(pm.draws.size()) + " draws");
        // the shared copies of a decoded mesh: same first index
        std::map<std::uint32_t, int> first_draw;   // first_index -> the draw that decoded it
        for (std::size_t d = 0; d < pm.draws.size(); ++d)
            if (!pinfo[d].ref.shared && !first_draw.count(pm.draws[d].first_index)) first_draw[pm.draws[d].first_index] = int(d);
        for (std::size_t d = 0; d < pm.draws.size(); ++d) {
            auto it = first_draw.find(pm.draws[d].first_index);
            if (it != first_draw.end() && batches_of_draw.count(it->second)) pinfo[d].batches = batches_of_draw[it->second];
        }
        std::vector<std::uint32_t> pw(pm.vertices.size(), 0);   // raw w per vertex
        std::vector<std::array<float, 4>> pl(pm.vertices.size(), std::array<float, 4>{});   // type 5 lighting
        for (const Batch& bt : all_batches)
            for (std::uint32_t i = 0; i < bt.n && bt.base + i < pw.size(); ++i) {
                pw[bt.base + i] = bt.w[i];
                if (i < bt.light.size()) pl[bt.base + i] = bt.light[i];
            }
        if (pass == 0) {
            m = std::move(pm);
            info = std::move(pinfo);
            wraw = std::move(pw);
            lraw = std::move(pl);
            lod_draw0 = m.draws.size();
            continue;
        }
        const std::uint32_t vo = std::uint32_t(m.vertices.size()), io = std::uint32_t(m.indices.size());
        m.vertices.insert(m.vertices.end(), pm.vertices.begin(), pm.vertices.end());
        for (std::uint32_t i : pm.indices) m.indices.push_back(i + vo);
        for (b3asset::DrawRange dr : pm.draws) { dr.first_index += io; m.draws.push_back(dr); }
        for (const auto& kv : pm.stats) m.stats["LOD " + kv.first] = kv.second;
        info.insert(info.end(), pinfo.begin(), pinfo.end());
        wraw.insert(wraw.end(), pw.begin(), pw.end());
        lraw.insert(lraw.end(), pl.begin(), pl.end());
    }
    // instance / prop types by their first chain (STATIC +0x38 / +0x3C, 0x50 bytes each; the model pair at +0x20)
    std::map<std::size_t, int> inst_type, prop_type;
    {
        const int ni = std::int16_t(st.u16(0x34)), np = std::int16_t(st.u16(0x36));
        const std::size_t i0 = st.u32(0x38), p0 = st.u32(0x3C);
        for (int t = 0; i0 && t < ni; ++t) { const std::size_t pair = i0 + 0x50 * std::size_t(t) + 0x20; if (st.u32(pair)) inst_type[pair + st.u32(pair)] = t; }
        for (int t = 0; p0 && t < np; ++t) { const std::size_t pair = p0 + 0x50 * std::size_t(t) + 0x20; if (st.u32(pair)) prop_type[pair + st.u32(pair)] = t; }
    }
    for (std::size_t d = 0; d < m.draws.size(); ++d) {
        DrawInfo& di = info[d];
        const std::string& part = m.draws[d].part;
        char t[32];
        if (m.draws[d].transform >= 0) {
            di.kind = part == "prop" ? 2 : 1;
            const auto& tab = di.kind == 2 ? prop_type : inst_type;
            auto it = tab.find(di.ref.chain1);
            di.type = it == tab.end() ? -1 : it->second;
            std::snprintf(t, sizeof t, "section_%03d", di.ref.unit);
            di.group = t;
        } else if (part.rfind("unit ", 0) == 0) {
            std::snprintf(t, sizeof t, d >= lod_draw0 ? "section_%03d_lod" : "section_%03d", di.ref.unit);
            di.group = t;
        } else {
            std::string g = part;
            for (char& c : g) if (c == ' ') c = '_';
            di.group = g;   // backdrop_3, backdrop+28_0, water_0, water+30_0
        }
    }

    ExportReport r;
    // ---- textures ----
    std::vector<std::string> tex_png;
    for (std::size_t k = 0; k < m.textures.size(); ++k) {
        const std::string name = raster_file_stem(k, m.textures[k].name) + ".png";
        const b3asset::Image& im = m.textures[k].levels.at(0);
        out[name] = encode_png(im.w, im.h, im.rgba.data());
        tex_png.push_back(name);
    }
    r.textures = tex_png.size();
    // ENVIRO.DAT: the four sky rasters as PNG, the parameters in the sidecar
    Json env = Json::object();
    if (!enviro.empty()) {
        b3asset::Enviro e;
        std::string eerr;
        if (b3asset::read_enviro(Bytes{enviro.data(), enviro.size()}, gs, e, &eerr)) {
            const std::pair<const char*, const b3asset::DecodedRaster*> sky[4] = {
                {"sky_gradient", &e.gradient}, {"sky_clouds", &e.clouds}, {"sky_envmap", &e.sky_envmap}, {"sky_sun_corona", &e.sun_corona}};
            Json files = Json::object();
            for (const auto& s : sky) {
                if (s.second->levels.empty()) continue;
                const std::string name = stem + "_" + s.first + ".png";
                const b3asset::Image& im = s.second->levels[0];
                out[name] = encode_png(im.w, im.h, im.rgba.data());
                files.set(s.first, Json::str(name));
            }
            env.set("textures", std::move(files));
            env.set("fog_colour", Json::floats(e.block[0].fog_colour, 4));
            env.set("near_scale", Json::f32(e.block[0].near_scale));
            env.set("fog_max", Json::f32(e.block[0].fog_max));
            env.set("specular_scale", Json::f32(e.block[0].specular_scale));
            env.set("specular_colour", Json::floats(e.specular_colour, 4));
            env.set("sun_colour", Json::floats(e.sun_colour, 4));
            env.set("sun_travel", Json::floats(e.sun_travel, 3));
            env.set("corona_scale", Json::f32(e.corona_scale));
            env.set("header_hex", Json::str(hexbytes(enviro.data(), std::min<std::size_t>(0xB0, enviro.size()))));
            env.set("fnv1a64", Json::str(hex64(b3mod::fnv1a64(enviro.data(), enviro.size()))));
        } else {
            r.notes.push_back("ENVIRO.DAT did not decode: " + eerr);
        }
    }

    // ---- per draw: the vertex range ----
    struct Range { std::uint32_t v0 = 0, v1 = 0; };
    auto range_of = [&](const b3asset::DrawRange& dr) {
        Range rg{0xFFFFFFFFu, 0};
        for (std::uint32_t i = 0; i < dr.index_count; ++i) {
            const std::uint32_t v = m.indices[dr.first_index + i];
            rg.v0 = std::min(rg.v0, v);
            rg.v1 = std::max(rg.v1, v + 1);
        }
        return rg;
    };
    const std::size_t mat0 = st.u32(0x08);
    auto type_of = [&](int mi) { return mi >= 0 && std::size_t(mi) < mats.size() ? int(mats[std::size_t(mi)].type) : -1; };
    auto has_normals = [&](int mi) { const int t = type_of(mi); return t == 1 || t == 2; };
    // a draw's triangle-set hash (positions, UVs, raw colours)
    auto draw_hash = [&](const b3asset::DrawRange& dr, const Range& rg) {
        std::vector<std::array<float, 3>> pos;
        std::vector<std::uint8_t> extra;
        for (std::uint32_t v = rg.v0; v < rg.v1; ++v) {
            const b3asset::Vertex& x = m.vertices[v];
            pos.push_back({x.pos[0], x.pos[1], x.pos[2]});
            const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(x.uv);
            extra.insert(extra.end(), p, p + 8);
            const std::uint8_t* c = reinterpret_cast<const std::uint8_t*>(&x.rgba);
            extra.insert(extra.end(), c, c + 4);
        }
        std::vector<std::uint32_t> tri(m.indices.begin() + dr.first_index, m.indices.begin() + dr.first_index + dr.index_count);
        for (auto& i : tri) i -= rg.v0;
        return triangle_set_hash(pos, extra, 12, tri);
    };

    // ---- the sidecar ----
    Json side = Json::object();
    side.set("format", Json::str("b3modtool track export"));
    side.set("format_version", Json::integer(1));
    Json src = Json::object();
    src.set("path", Json::str(opt.source));
    src.set("static_size", Json::integer(std::int64_t(static_dat.size())));
    src.set("static_fnv1a64", Json::str(hex64(b3mod::fnv1a64(static_dat.data(), static_dat.size()))));
    src.set("streamed_size", Json::integer(std::int64_t(streamed_dat.size())));
    src.set("streamed_fnv1a64", Json::str(hex64(b3mod::fnv1a64(streamed_dat.data(), streamed_dat.size()))));
    side.set("source", std::move(src));
    side.set("section", Json::integer(opt.section));
    side.set("units", Json::integer(streamed_dat.empty() ? 0 : nunits));
    side.set("axes", Json::str("right-handed, Y up, metres: glTF positions are the file's (world space; instances and props in "
                               "object space under their placement matrix); OBJ the same, placed, v = 1 - v"));
    side.set("static_header_hex", Json::str(hexbytes(static_dat.data(), 0x60)));
    Json jm = Json::array();
    for (std::size_t k = 0; k < mats.size(); ++k) {
        const b3asset::TrackMaterialInfo& mi = mats[k];
        Json j = Json::object();
        j.set("index", Json::integer(std::int64_t(k)));
        j.set("type", Json::integer(mi.type));
        j.set("flags", Json::str(hex32(mi.flags)));
        j.set("test", Json::str(hex32(mi.test)));
        j.set("alpha", Json::str(hex32(mi.alpha)));
        j.set("alpha_fix", Json::integer(mi.alpha_fix));
        j.set("param4", Json::f32(mi.param4));
        j.set("param8", Json::f32(mi.param8));
        j.set("texture", mi.texture >= 0 ? Json::str(tex_png[std::size_t(mi.texture)]) : Json());
        if (st.has(mat0 + 0x28 * k, 0x28)) j.set("raw", Json::str(hexbytes(static_dat.data() + mat0 + 0x28 * k, 0x28)));
        jm.push(std::move(j));
    }
    side.set("materials", std::move(jm));
    Json janim = Json::array();
    for (const b3asset::TrackAnim& a : m.track_anims) {
        Json j = Json::object();
        j.set("material", Json::integer(a.material));
        j.set("flags", Json::str(hex32(a.flags)));
        Json fr = Json::array();
        for (int f : a.frames) fr.push(Json::str(f >= 0 && std::size_t(f) < tex_png.size() ? tex_png[std::size_t(f)] : "?"));
        j.set("frames", std::move(fr));
        j.set("f14", Json::f32(a.f14));
        j.set("f18", Json::f32(a.f18));
        janim.push(std::move(j));
    }
    side.set("animated_materials", std::move(janim));
    Json jt = Json::array();
    for (const auto& n : tex_png) jt.push(Json::str(n));
    side.set("textures", std::move(jt));
    if (env.size()) side.set("enviro", std::move(env));

    // ---- materials (glTF: per STATIC material, one- or two-sided) ----
    GltfBuilder g;
    const int sampler = g.add("samplers", [] {
        Json s = Json::object();
        s.set("magFilter", Json::integer(9729));
        s.set("minFilter", Json::integer(9987));
        s.set("wrapS", Json::integer(10497));
        s.set("wrapT", Json::integer(10497));
        return s;
    }());
    std::map<int, int> gl_tex;
    auto texture = [&](int t) {
        auto it = gl_tex.find(t);
        if (it != gl_tex.end()) return it->second;
        Json im = Json::object();
        im.set("uri", Json::str(tex_png[std::size_t(t)]));
        im.set("name", Json::str(m.textures[std::size_t(t)].name));
        const int ii = g.add("images", std::move(im));
        Json tj = Json::object();
        tj.set("source", Json::integer(ii));
        tj.set("sampler", Json::integer(sampler));
        tj.set("name", Json::str(m.textures[std::size_t(t)].name));
        return gl_tex[t] = g.add("textures", std::move(tj));
    };
    std::map<std::pair<int, bool>, int> gl_mat;
    auto material = [&](const b3asset::DrawRange& dr, int mi, bool instance) {
        // the track programs cull back faces for types 0, 1, 2, 6; instances and the others draw both sides
        // (docs/native-renderer.md §11.3)
        const int ty = type_of(mi);
        const bool two = instance || !(ty == 0 || ty == 1 || ty == 2 || ty == 6);
        const auto key = std::make_pair(mi, two);
        auto it = gl_mat.find(key);
        if (it != gl_mat.end()) return it->second;
        Json j = Json::object();
        const std::string tn = dr.texture >= 0 ? m.textures[std::size_t(dr.texture)].name : "untextured";
        j.set("name", Json::str("m" + std::to_string(mi) + "_" + tn + (two ? "_2s" : "")));
        Json pbr = Json::object();
        if (dr.texture >= 0) {
            Json bt = Json::object();
            bt.set("index", Json::integer(texture(dr.texture)));
            pbr.set("baseColorTexture", std::move(bt));
        }
        const float fac[4] = {1, 1, 1, dr.blend == b3asset::DrawRange::kBlend ? float(dr.tint >> 24) / 255.0f : 1.0f};
        pbr.set("baseColorFactor", Json::floats(fac, 4));
        pbr.set("metallicFactor", Json::f32(0.0f));
        pbr.set("roughnessFactor", Json::f32(ty == 1 || ty == 2 ? 0.4f : 0.9f));
        j.set("pbrMetallicRoughness", std::move(pbr));
        if (dr.blend == b3asset::DrawRange::kBlend) j.set("alphaMode", Json::str("BLEND"));
        else if (dr.blend == b3asset::DrawRange::kAlphaTest) {
            j.set("alphaMode", Json::str("MASK"));
            j.set("alphaCutoff", Json::f32(std::clamp(dr.alpha_ref, 0.0f, 1.0f)));
        } else j.set("alphaMode", Json::str("OPAQUE"));
        j.set("doubleSided", Json::boolean(two));
        Json ex = Json::object(), b3 = Json::object();
        b3.set("material", Json::integer(mi));
        b3.set("type", Json::integer(ty));
        if (mi >= 0 && std::size_t(mi) < mats.size()) {
            b3.set("test", Json::str(hex32(mats[std::size_t(mi)].test)));
            b3.set("alpha", Json::str(hex32(mats[std::size_t(mi)].alpha)));
            b3.set("alpha_fix", Json::integer(mats[std::size_t(mi)].alpha_fix));
            b3.set("flags", Json::str(hex32(mats[std::size_t(mi)].flags)));
        }
        if (dr.anim >= 0) b3.set("animated", Json::integer(dr.anim));
        ex.set("b3", std::move(b3));
        j.set("extras", std::move(ex));
        return gl_mat[key] = g.add("materials", std::move(j));
    };

    // ---- primitives ----
    Json jmeshes = Json::array();   // the sidecar's per-draw records
    std::map<std::uint32_t, Json> prim_of_first;   // decoded geometry -> its glTF primitive (shared by instances)
    auto make_prim = [&](std::size_t d) -> Json {
        const b3asset::DrawRange& dr = m.draws[d];
        const int mi = info[d].ref.material;
        const bool inst = dr.transform >= 0;
        const auto key = dr.first_index;
        if (inst) {
            auto it = prim_of_first.find(key);
            if (it != prim_of_first.end()) return it->second;
        }
        const Range rg = range_of(dr);
        std::vector<std::array<float, 3>> pos, nrm;
        std::vector<std::array<float, 2>> uv;
        std::vector<std::array<std::uint8_t, 4>> col;
        std::vector<std::array<float, 4>> rawc, rawn, light;
        std::vector<float> w;
        const bool normals = has_normals(mi);
        for (std::uint32_t v = rg.v0; v < rg.v1; ++v) {
            const b3asset::Vertex& x = m.vertices[v];
            pos.push_back(gltf_position(x.pos));
            uv.push_back(gltf_uv(x.uv[0], x.uv[1]));
            std::array<std::uint8_t, 4> c{}, cr{};
            for (int q = 0; q < 4; ++q) {
                cr[std::size_t(q)] = std::uint8_t(x.rgba >> (8 * q));
                // on screen: GS colour 0x80 = 1.0, and the present blit doubles the frame (docs/native-renderer.md §11):
                // RGB x 4, alpha x 2 (the stored bytes are in _B3_COLOR_R .. _A)
                c[std::size_t(q)] = std::uint8_t(std::min(255u, std::uint32_t(cr[std::size_t(q)]) * (q < 3 ? 4 : 2)));
            }
            if (type_of(mi) == 1 || type_of(mi) == 2) c[3] = 255;   // types 1 / 2: alpha from the shader, not the vertex
            col.push_back(c);
            rawc.push_back({float(cr[0]), float(cr[1]), float(cr[2]), float(cr[3])});
            w.push_back(float(wraw[v]));   // 0 or 0x8000: exact as a float
            if (type_of(mi) == 5) light.push_back(lraw[v]);
            if (normals) {
                const float l = std::sqrt(x.normal[0] * x.normal[0] + x.normal[1] * x.normal[1] + x.normal[2] * x.normal[2]);
                nrm.push_back(l > 1e-6f ? std::array<float, 3>{x.normal[0] / l, x.normal[1] / l, x.normal[2] / l} : std::array<float, 3>{0, 1, 0});
                std::array<float, 4> rn{};
                for (int q = 0; q < 3; ++q) rn[std::size_t(q)] = float(std::lround(x.normal[q] * 127.0f));
                rn[3] = float(std::int8_t(x.infl[0] & 0xFF));
                rawn.push_back(rn);
            }
        }
        std::vector<std::uint32_t> idx(m.indices.begin() + dr.first_index, m.indices.begin() + dr.first_index + dr.index_count);
        for (auto& i : idx) i -= rg.v0;
        static const char* const kRgba[] = {"R", "G", "B", "A"};
        static const char* const kXyzw[] = {"X", "Y", "Z", "W"};
        Json attr = Json::object();
        attr.set("POSITION", Json::integer(g.positions(pos)));
        if (normals) attr.set("NORMAL", Json::integer(g.vec3(nrm)));
        if (dr.texture >= 0) attr.set("TEXCOORD_0", Json::integer(g.vec2(uv)));
        if (!inst) {   // instances: the colour is the placement's (node extras tint_w), not the vertices'
            attr.set("COLOR_0", Json::integer(g.ubyte4(col, true)));
            g.scalar_attrs(attr, "_B3_COLOR", kRgba, rawc);
        }
        // the stored fields as float SCALARs (whole numbers but the lighting), one per component: a modelling tool keeps
        // float attributes (Blender drops the byte forms), and Blender's importer gives each its own values only when
        // they all have one form (docs/modding/b3modtool.md "Blender")
        if (normals) g.scalar_attrs(attr, "_B3_NRM", kXyzw, rawn);
        if (!light.empty()) g.scalar_attrs(attr, "_B3_LIGHT", kXyzw, light);
        attr.set("_B3_W", Json::integer(g.float1(w)));
        Json p = Json::object();
        p.set("attributes", std::move(attr));
        p.set("indices", Json::integer(g.indices(idx, pos.size())));
        p.set("mode", Json::integer(gl::kTriangles));
        p.set("material", Json::integer(material(dr, mi, inst)));
        Json ex = Json::object(), b3 = Json::object();
        b3.set("file", Json::str(info[d].ref.in_static ? "STATIC.DAT" : "STREAMED.DAT"));
        b3.set("chain1", Json::str(hex32(std::uint32_t(info[d].ref.chain1))));
        b3.set("chain2", Json::str(hex32(std::uint32_t(info[d].ref.chain2))));
        b3.set("material", Json::integer(mi));
        ex.set("b3", std::move(b3));
        p.set("extras", std::move(ex));
        ++r.primitives;
        r.vertices += pos.size();
        r.triangles += idx.size() / 3;
        // sidecar record
        Json s = Json::object();
        s.set("draw", Json::integer(std::int64_t(d)));
        s.set("group", Json::str(info[d].group));
        s.set("file", Json::str(info[d].ref.in_static ? "STATIC.DAT" : "STREAMED.DAT"));
        s.set("chain1", Json::str(hex32(std::uint32_t(info[d].ref.chain1))));
        s.set("chain2", Json::str(hex32(std::uint32_t(info[d].ref.chain2))));
        s.set("material", Json::integer(mi));
        s.set("unit", Json::integer(info[d].ref.unit));
        if (info[d].type >= 0) s.set(info[d].kind == 2 ? "prop_type" : "instance_type", Json::integer(info[d].type));
        s.set("vertices", Json::integer(std::int64_t(pos.size())));
        s.set("triangles", Json::integer(std::int64_t(idx.size() / 3)));
        Json b = Json::array();
        for (std::uint32_t n : info[d].batches) b.push(Json::integer(n));
        s.set("batches", std::move(b));
        s.set("tri_hash", Json::str(hex64(draw_hash(dr, rg))));
        jmeshes.push(std::move(s));
        if (inst) prim_of_first[key] = p;
        return p;
    };

    // ---- nodes ----
    auto node = [&](const std::string& name) {
        Json n = Json::object();
        n.set("name", Json::str(name));
        return n;
    };
    auto attach = [&](int parent, int child) {
        Json& p = g.doc.get("nodes")->a[std::size_t(parent)];
        Json* ch = p.get("children");
        if (!ch) ch = &p.set("children", Json::array());
        ch->push(Json::integer(child));
    };
    const int root = g.add("nodes", node(stem));
    // world-space groups, in draw order
    std::map<std::string, std::vector<std::size_t>> world;   // group -> draws
    std::vector<std::string> order;
    std::map<std::string, std::vector<std::size_t>> placed;   // group -> instance / prop draws
    for (std::size_t d = 0; d < m.draws.size(); ++d) {
        const std::string& gname = info[d].group;
        if (!world.count(gname) && !placed.count(gname)) order.push_back(gname);
        (m.draws[d].transform >= 0 ? placed : world)[gname].push_back(d);
    }
    std::sort(order.begin(), order.end());
    std::map<std::uint32_t, int> inst_mesh;   // first_index -> glTF mesh
    bool any_bound = false;
    auto grow_b = [&](const float p[3]) {
        if (!any_bound) { for (int c = 0; c < 3; ++c) r.bmin[c] = r.bmax[c] = p[c]; any_bound = true; }
        for (int c = 0; c < 3; ++c) { r.bmin[c] = std::min(r.bmin[c], p[c]); r.bmax[c] = std::max(r.bmax[c], p[c]); }
    };
    // OBJ text, when asked for
    std::string obj, mtl;
    std::size_t obj_base = 1;
    std::set<int> obj_mats;
    auto obj_draw = [&](std::size_t d, const std::string& oname, const std::array<float, 16>* M) {
        const b3asset::DrawRange& dr = m.draws[d];
        const Range rg = range_of(dr);
        const int mi = info[d].ref.material;
        obj += "o " + oname + "\nusemtl m" + std::to_string(mi) + "\n";
        obj_mats.insert(mi);
        char line[160];
        for (std::uint32_t v = rg.v0; v < rg.v1; ++v) {
            const b3asset::Vertex& x = m.vertices[v];
            const auto p = M ? gltf_apply(*M, x.pos, 1.0f) : std::array<float, 3>{x.pos[0], x.pos[1], x.pos[2]};
            grow_b(p.data());
            std::snprintf(line, sizeof line, "v %.9g %.9g %.9g %.4f %.4f %.4f\n", p[0], p[1], p[2], std::min(1.0, (x.rgba & 0xFF) / 128.0),
                          std::min(1.0, ((x.rgba >> 8) & 0xFF) / 128.0), std::min(1.0, ((x.rgba >> 16) & 0xFF) / 128.0));
            obj += line;
        }
        for (std::uint32_t v = rg.v0; v < rg.v1; ++v) {
            const auto t = obj_uv(m.vertices[v].uv[0], m.vertices[v].uv[1]);
            std::snprintf(line, sizeof line, "vt %.9g %.9g\n", t[0], t[1]);
            obj += line;
        }
        for (std::uint32_t i = 0; i + 2 < dr.index_count; i += 3) {
            const std::size_t a = obj_base + m.indices[dr.first_index + i] - rg.v0, b = obj_base + m.indices[dr.first_index + i + 1] - rg.v0,
                              c = obj_base + m.indices[dr.first_index + i + 2] - rg.v0;
            std::snprintf(line, sizeof line, "f %zu/%zu %zu/%zu %zu/%zu\n", a, a, b, b, c, c);
            obj += line;
        }
        obj_base += rg.v1 - rg.v0;
        ++r.primitives;
        r.vertices += rg.v1 - rg.v0;
        r.triangles += dr.index_count / 3;
    };
    for (const std::string& gname : order) {
        const int gn = g.add("nodes", node(gname));
        attach(root, gn);
        Json gb = Json::object();
        if (gname.rfind("section_", 0) == 0) gb.set("unit", Json::integer(std::stoi(gname.substr(8))));
        Json gex = Json::object();
        gex.set("b3", std::move(gb));
        g.doc.get("nodes")->a[std::size_t(gn)].set("extras", std::move(gex));
        if (world.count(gname)) {
            if (opt.obj) {
                for (std::size_t d : world[gname]) obj_draw(d, gname + "_d" + std::to_string(d), nullptr);
            } else {
                Json prims = Json::array();
                for (std::size_t d : world[gname]) {
                    prims.push(make_prim(d));
                    const Range rg = range_of(m.draws[d]);
                    for (std::uint32_t v = rg.v0; v < rg.v1; ++v) grow_b(m.vertices[v].pos);
                }
                Json mj = Json::object();
                mj.set("name", Json::str(gname));
                mj.set("primitives", std::move(prims));
                g.doc.get("nodes")->a[std::size_t(gn)].set("mesh", Json::integer(g.add("meshes", std::move(mj))));
                ++r.meshes;
            }
        }
        if (placed.count(gname)) {
            std::map<std::pair<int, int>, int> count;   // (kind, type) -> placements so far
            for (std::size_t d : placed[gname]) {
                const DrawInfo& di = info[d];
                const b3asset::DrawRange& dr = m.draws[d];
                const auto& M = m.transforms[std::size_t(dr.transform)];
                std::string base = std::string(di.kind == 2 ? "prop" : "instance") + (di.type >= 0 ? std::to_string(di.type) : std::string("x"));
                const int k = count[{di.kind, di.type}]++;
                const std::string pname = gname + "_" + base + "_" + std::to_string(k);
                if (opt.obj) { obj_draw(d, pname, &M); continue; }
                auto it = inst_mesh.find(dr.first_index);
                if (it == inst_mesh.end()) {
                    Json prims = Json::array();
                    prims.push(make_prim(d));
                    Json mj = Json::object();
                    mj.set("name", Json::str(base + "_model"));
                    mj.set("primitives", std::move(prims));
                    it = inst_mesh.emplace(dr.first_index, g.add("meshes", std::move(mj))).first;
                    ++r.meshes;
                }
                Json n = node(pname);
                n.set("matrix", Json::floats(M.data(), 16));
                n.set("mesh", Json::integer(it->second));
                Json b3 = Json::object();
                b3.set(di.kind == 2 ? "prop_type" : "instance_type", Json::integer(di.type));
                if (std::size_t(dr.transform) < m.transform_tints.size()) b3.set("tint_w", Json::floats(m.transform_tints[std::size_t(dr.transform)].data(), 3));
                for (const auto& pt : m.prop_transforms)
                    if (pt.first == dr.transform) b3.set("placement", Json::integer(pt.second));
                Json ex = Json::object();
                ex.set("b3", std::move(b3));
                n.set("extras", std::move(ex));
                attach(gn, g.add("nodes", std::move(n)));
                grow_b(M.data() + 12);
            }
        }
    }
    side.set("meshes", std::move(jmeshes));

    // ---- what is not exported ----
    std::map<std::string, std::uint64_t>& s = m.stats;
    r.notes.push_back("exported: the streamed units' high-detail meshes (one glTF mesh per section) and LOD meshes (the far "
                      "view: section_NNN_lod), the instance and prop "
                      "models at their placements, " + std::string(opt.section < 0 ? "the backdrops (+0x24, +0x28) and the water (+0x2C, +0x30)" : "(backdrops and water: only with --section all)") +
                      "; materials and textures from STATIC.DAT" + (enviro.empty() ? "" : "; the four ENVIRO.DAT sky textures (PNG) and its parameters (sidecar)"));
    if (s["line meshes skipped"]) r.notes.push_back("not exported: " + std::to_string(s["line meshes skipped"]) + " line meshes (material type 3, anti-aliased line strips; decoded, but glTF has no line width)");
    if (s["meshes with a bad material"]) r.notes.push_back("not exported: " + std::to_string(s["meshes with a bad material"]) + " meshes with a material index outside the table");
    if (s["batches without positions"]) r.notes.push_back("skipped: " + std::to_string(s["batches without positions"]) + " VU batches without positions");
    r.notes.push_back("not exported: the collision trees (STREAMED.DAT LOD block +0x90, CGtCollisionKDTree: the port does not "
                      "decode their layout) and the irradiance per collision vertex (+0x94), the unit boundary lines (+0x60) "
                      "(docs/track-format.md §2.4)");
    r.notes.push_back("not exported: the sky dome (the game generates it in code, docs/sky-enviro.md: only its textures are exported), "
                      "the water reflections (drawn by mirroring the water objects at run time), GAMEDATA.BGD (start lines, AI and traffic paths)");
    if (!m.track_anims.empty()) r.notes.push_back(std::to_string(m.track_anims.size()) + " animated materials: frame 0 is the material's texture; the frames are listed in the sidecar");

    if (opt.obj) {
        mtl = "# b3modtool track export: " + opt.source + "\n";
        for (int mi : obj_mats) {
            mtl += "newmtl m" + std::to_string(mi) + "\nKd 1 1 1\n";
            if (mi >= 0 && std::size_t(mi) < mats.size() && mats[std::size_t(mi)].texture >= 0)
                mtl += "map_Kd " + tex_png[std::size_t(mats[std::size_t(mi)].texture)] + "\n";
        }
        out[stem + ".mtl"] = Buf(mtl.begin(), mtl.end());
        obj = "# b3modtool track export (" + opt.source + "); right-handed, Y up, metres; vertex colours 0x80 = 1.0\nmtllib " + stem + ".mtl\n" + obj;
        out[stem + ".obj"] = Buf(obj.begin(), obj.end());
    } else {
        Json scene = Json::object();
        scene.set("name", Json::str(stem));
        Json sn = Json::array();
        sn.push(Json::integer(root));
        scene.set("nodes", std::move(sn));
        g.add("scenes", std::move(scene));
        g.doc.set("scene", Json::integer(0));
        Json aex = Json::object();
        aex.set("b3_axes", Json::str("right-handed, Y up, metres (the game's own space)"));
        aex.set("b3_sidecar", Json::str(stem + ".b3track.json"));
        g.doc.get("asset")->set("extras", std::move(aex));
        const std::string text = g.text(stem + ".bin");
        while (g.bin.size() % 4) g.bin.push_back(0);
        out[stem + ".gltf"] = Buf(text.begin(), text.end());
        out[stem + ".bin"] = g.bin;
        r.nodes = g.doc.get("nodes")->a.size();
    }
    Json notes = Json::array();
    for (const auto& n : r.notes) notes.push(Json::str(n));
    side.set("notes", std::move(notes));
    const std::string sj = side.dump(1) + "\n";
    out[stem + ".b3track.json"] = Buf(sj.begin(), sj.end());
    // the report
    std::string rt = "b3modtool track export: " + opt.source + (opt.section >= 0 ? ", section " + std::to_string(opt.section) : std::string(", all sections")) + "\n";
    char line[256];
    std::snprintf(line, sizeof line, "%zu sections (streamed units) in the route; %zu nodes, %zu meshes, %zu primitives, %llu vertices, %llu triangles, %zu textures\n",
                  std::size_t(streamed_dat.empty() ? 0 : nunits), r.nodes, r.meshes, r.primitives, (unsigned long long)r.vertices, (unsigned long long)r.triangles, r.textures);
    rt += line;
    std::snprintf(line, sizeof line, "bounds (%.1f %.1f %.1f) .. (%.1f %.1f %.1f) m\n", r.bmin[0], r.bmin[1], r.bmin[2], r.bmax[0], r.bmax[1], r.bmax[2]);
    rt += line;
    for (const auto& n : r.notes) rt += "- " + n + "\n";
    rt += "decoder statistics:\n";
    for (const auto& kv : s) {
        std::snprintf(line, sizeof line, "  %-40s %llu\n", kv.first.c_str(), (unsigned long long)kv.second);
        rt += line;
    }
    out[stem + "_report.txt"] = Buf(rt.begin(), rt.end());
    r.files = out.size();
    if (rep) *rep = r;
    return true;
}

}  // namespace b3modtool
