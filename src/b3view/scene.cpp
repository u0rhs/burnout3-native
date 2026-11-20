#include "scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <array>

#include "b3asset/car.h"
#include "b3asset/track.h"
#include "b3asset/discfs.h"
#include "b3asset/export.h"

static_assert(sizeof(b3asset::Vertex) == sizeof(b3gfx::Vertex), "b3asset and b3gfx vertex layouts must agree");

namespace b3view {

namespace {
std::string upper_ext(const std::string& path) {
    const auto dot = path.find_last_of('.');
    std::string e = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (char& c : e) c = char(std::toupper(static_cast<unsigned char>(c)));
    return e;
}
}  // namespace

// Region: an area folder (TRACKS/<area>) whose route folders hold STATIC.DAT. The routes to merge: the V1 of each
// route (V2 is the same roads with another unit numbering, docs/native-renderer.md), sorted by name.
std::vector<std::string> region_routes(const std::string& dir, const std::string& first) {
    namespace fs = std::filesystem;
    // iso1: a folder of the disc's files or a folder in the disc image (b3asset/discfs.h)
    if (b3asset::disc::exists(fs::path(dir) / "STATIC.DAT") || !b3asset::disc::is_dir(dir)) return {};
    std::vector<std::string> out;
    for (const auto& e : b3asset::disc::list_dir(dir)) {
        const std::string& name = e.name;
        if (!e.dir || !b3asset::disc::exists(e.path / "STATIC.DAT")) continue;
        if (name.size() > 3 && name.compare(name.size() - 3, 3, "_V2") == 0) continue;
        out.push_back(e.path.string());
    }
    std::sort(out.begin(), out.end());
    if (!first.empty())
        std::stable_partition(out.begin(), out.end(), [&](const std::string& r) { return fs::path(r).filename().string() == first; });
    return out;
}

std::string route_dir(const std::string& path) {
    const std::string ext = upper_ext(path);
    if (ext == "BGV" || ext == "BTV") return "";
    if (const auto routes = region_routes(path, ""); !routes.empty()) return routes.front();  // its ENVIRO.DAT
    return ext == "DAT" ? path.substr(0, path.find_last_of("/\\")) : path;
}

namespace {

// Appends `src` to `dst`, skipping triangles an earlier route already brought: where routes share a road their units
// carry the same geometry at the same place, but not always cut into the same draws, so the test is per triangle (its
// three world-space corners to 5 cm, in any order). A draw left with no triangle is dropped. Textures are shared by
// name and size; only the vertices of kept triangles are copied. `seen` holds the earlier routes' triangles.
std::uint64_t tri_key(const float p[3][3]) {
    long q[3][3];
    for (int v = 0; v < 3; ++v)
        for (int k = 0; k < 3; ++k) q[v][k] = std::lround(p[v][k] * 20.0f);
    std::array<std::array<long, 3>, 3> s{{{q[0][0], q[0][1], q[0][2]}, {q[1][0], q[1][1], q[1][2]}, {q[2][0], q[2][1], q[2][2]}}};
    std::sort(s.begin(), s.end());
    std::uint64_t h = 1469598103934665603ull;
    for (const auto& v : s)
        for (long x : v) { h ^= std::uint64_t(x); h *= 1099511628211ull; h ^= h >> 29; }
    return h;
}

// Exclusive mode: one version of each place. Routes model shared places once per route, and a single route models a
// junction it passes twice once per pass, in units far apart in its order (EU C4 units 3 and 12: 170 shared 4 m ground
// cells; M2 45 / 106: 295) -- the game streams only one of them at a time. `owners` maps a 4 m ground cell (x, z) to the
// (route, unit) whose ground (|normal.y| > 0.7) claimed it first; a triangle whose centre lies in a cell owned by
// another route, or by a unit of the same route more than one step away (neighbours share their seams), is dropped.
// Draws outside units (props, backdrops, water) are kept and claim nothing.
std::uint64_t cell_key(float x, float z) {
    return (std::uint64_t(std::uint32_t(std::lround(std::floor(x / 4.0f)))) << 32) | std::uint32_t(std::lround(std::floor(z / 4.0f)));
}

void merge_route(b3asset::Model& dst, const b3asset::Model& src, std::unordered_set<std::uint64_t>& seen,
                 std::map<std::tuple<std::string, int, int>, int>& tex_index, long& kept, long& dropped,
                 std::unordered_map<std::uint64_t, std::pair<int, int>>* owners, int route) {
    std::vector<int> tex_map(src.textures.size(), -1);
    const int tbase = int(dst.transforms.size());
    dst.transforms.insert(dst.transforms.end(), src.transforms.begin(), src.transforms.end());
    dst.transform_tints.insert(dst.transform_tints.end(), src.transform_tints.begin(), src.transform_tints.end());
    dst.transform_tints.resize(dst.transforms.size());
    std::vector<std::int64_t> vmap(src.vertices.size(), -1);
    std::vector<std::uint64_t> added;
    for (const auto& d : src.draws) {
        int unit = -1;
        if (std::sscanf(d.part.c_str(), "unit %d", &unit) != 1) unit = -1;
        const std::array<float, 16>* m = d.transform >= 0 && std::size_t(d.transform) < src.transforms.size() ? &src.transforms[std::size_t(d.transform)] : nullptr;
        std::vector<std::uint32_t> keep;
        for (std::uint32_t i = 0; i + 2 < d.index_count; i += 3) {
            float p[3][3];
            for (int v = 0; v < 3; ++v) {
                const float* q = src.vertices[src.indices[d.first_index + i + std::uint32_t(v)]].pos;
                if (m) for (int k = 0; k < 3; ++k) p[v][k] = (*m)[std::size_t(k)] * q[0] + (*m)[std::size_t(4 + k)] * q[1] + (*m)[std::size_t(8 + k)] * q[2] + (*m)[std::size_t(12 + k)];
                else for (int k = 0; k < 3; ++k) p[v][k] = q[k];
            }
            const std::uint64_t key = tri_key(p);
            if (seen.count(key)) { ++dropped; continue; }
            if (owners && unit >= 0) {
                const std::uint64_t c = cell_key((p[0][0] + p[1][0] + p[2][0]) / 3, (p[0][2] + p[1][2] + p[2][2]) / 3);
                const auto o = owners->find(c);
                if (o != owners->end() && (o->second.first != route || std::abs(o->second.second - unit) > 1)) { ++dropped; continue; }
                if (o == owners->end()) {
                    float e1[3], e2[3];
                    for (int k = 0; k < 3; ++k) { e1[k] = p[1][k] - p[0][k]; e2[k] = p[2][k] - p[0][k]; }
                    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (len > 0 && std::fabs(n[1]) / len > 0.7f) owners->emplace(c, std::make_pair(route, unit));
                }
            }
            added.push_back(key);
            ++kept;
            for (int v = 0; v < 3; ++v) keep.push_back(src.indices[d.first_index + i + std::uint32_t(v)]);
        }
        if (keep.empty()) continue;
        b3asset::DrawRange nd = d;
        if (d.texture >= 0 && std::size_t(d.texture) < src.textures.size()) {
            int& t = tex_map[std::size_t(d.texture)];
            if (t < 0) {
                const auto& tx = src.textures[std::size_t(d.texture)];
                const auto tk = std::make_tuple(tx.name, tx.levels.empty() ? 0 : tx.levels[0].w, tx.levels.empty() ? 0 : tx.levels[0].h);
                auto it = tex_index.find(tk);
                if (it == tex_index.end()) {
                    it = tex_index.emplace(tk, int(dst.textures.size())).first;
                    dst.textures.push_back(tx);
                }
                t = it->second;
            }
            nd.texture = t;
        }
        if (d.transform >= 0) nd.transform = d.transform + tbase;
        nd.anim = -1;   // regions keep no animation table (frame 0, no scroll)
        nd.first_index = std::uint32_t(dst.indices.size());
        nd.index_count = std::uint32_t(keep.size());
        for (const std::uint32_t v : keep) {
            if (vmap[v] < 0) { vmap[v] = std::int64_t(dst.vertices.size()); dst.vertices.push_back(src.vertices[v]); }
            dst.indices.push_back(std::uint32_t(vmap[v]));
        }
        dst.draws.push_back(nd);
    }
    seen.insert(added.begin(), added.end());  // after the route: its own coplanar layers stay
    dst.waypoints.insert(dst.waypoints.end(), src.waypoints.begin(), src.waypoints.end());
    for (int k = 0; k < 3; ++k) { dst.bmin[k] = std::min(dst.bmin[k], src.bmin[k]); dst.bmax[k] = std::max(dst.bmax[k], src.bmax[k]); }
    dst.colour_scale = src.colour_scale;
    for (const auto& [k, v] : src.stats) dst.stats[k] += v;
}

}  // namespace

bool load_model(const std::string& path, const LoadOptions& opt, b3asset::Model& out, std::string& kind, std::string* why) {
    const std::string ext = upper_ext(path);
    b3asset::GsMemory gs;
    if (ext == "BGV" || ext == "BTV") {
        std::vector<std::uint8_t> data;
        if (!b3asset::read_file(path, data)) { if (why) *why = "cannot read " + path; return false; }
        kind = "car";
        b3asset::CarOptions co;
        co.lod = opt.lod;
        co.palette = opt.palette;
        return b3asset::read_car(b3asset::Bytes{data.data(), data.size()}, co, gs, out, why);
    }
    // Region: every route of an area folder in one world, shared pieces once (merge_route).
    if (const auto routes = region_routes(path, opt.region_first); !routes.empty()) {
        kind = "track";
        out = b3asset::Model{};
        std::unordered_set<std::uint64_t> seen;
        std::unordered_map<std::uint64_t, std::pair<int, int>> owners;
        int route_no = 0;
        std::map<std::tuple<std::string, int, int>, int> tex_index;
        long kept = 0, dropped = 0;
        for (const auto& r : routes) {
            std::vector<std::uint8_t> st, sm;
            if (!b3asset::read_file(r + "/STATIC.DAT", st)) continue;
            b3asset::read_file(r + "/STREAMED.DAT", sm);
            b3asset::TrackOptions to;
            to.lod = opt.track_lod;
            to.lines = true;   // lines1: the wires (type-3 line meshes)
            b3asset::GsMemory g;
            b3asset::Model m;
            std::string w;
            if (!b3asset::read_track(b3asset::Bytes{st.data(), st.size()}, b3asset::Bytes{sm.data(), sm.size()}, to, g, m, &w)) {
                std::fprintf(stderr, "region: %s skipped: %s\n", r.c_str(), w.c_str());
                continue;
            }
            const long k0 = kept, d0 = dropped;
            merge_route(out, m, seen, tex_index, kept, dropped, opt.region_exclusive ? &owners : nullptr, route_no++);
            std::printf("region: %s: %ld triangles kept, %ld already present\n", r.c_str(), kept - k0, dropped - d0);
        }
        std::printf("region (%s): %zu routes, %ld triangles, %ld duplicates dropped, %zu draws, %zu textures, %zu vertices\n", opt.region_exclusive ? "one version of each place" : "all routes, shared triangles once", routes.size(),
                    kept, dropped, out.draws.size(), out.textures.size(), out.vertices.size());
        if (out.draws.empty()) { if (why) *why = "region " + path + ": no route decoded"; return false; }
        return true;
    }
    // Tracks: a route folder, or its STATIC.DAT / STREAMED.DAT.
    std::string dir = path;
    if (ext == "DAT") dir = path.substr(0, path.find_last_of("/\\"));
    std::vector<std::uint8_t> st, sm;
    if (b3asset::read_file(dir + "/STATIC.DAT", st)) {
        kind = "track";
        b3asset::read_file(dir + "/STREAMED.DAT", sm);
        b3asset::TrackOptions to;
        to.lod = opt.track_lod;
        to.lines = true;   // lines1: the wires (type-3 line meshes, drawn by b3gfx as screen-space quads)
        return b3asset::read_track(b3asset::Bytes{st.data(), st.size()}, b3asset::Bytes{sm.data(), sm.size()}, to, gs, out, why);
    }
    if (why) *why = "unsupported file " + path + " (b3view reads PVEH/*/CARn.BGV|BTV and TRACKS/<area>/<route>)";
    return false;
}

namespace {

// Track draw -> the game model [docs/native-renderer.md §11]: material type and parameters for the VU1 math, GS TEST_1,
// the ALPHA blend (As or FIX), back-face culling unless flag 0x20, instance colours from the placement.
void track_item(const b3asset::Model& m, const b3asset::DrawRange& d, b3gfx::DrawItem& it) {
    const std::uint32_t type = d.material & 0xFFFF, flags = d.material >> 16;
    it.shading = b3gfx::Shading::kTrack;
    it.track_type = int(type);
    it.track_param[0] = d.param[0];
    it.track_param[1] = d.param[1];
    it.gs_test = d.colour;  // the reader keeps TEST_1 in the colour word for tracks
    it.textured = d.texture >= 0;
    // FIX blends (ALPHA C = FIX, FIX < 0x80) were mapped to a tint alpha by the reader: blend by that constant.
    it.blend_fix = (d.blend == b3asset::DrawRange::kBlend && ((d.tint >> 24) & 0xFF) != 0xFF) ? ((d.tint >> 24) & 0xFF) / 255.0f : -1.0f;
    for (float& c : it.tint) c = 1.0f;
    // Flag 0x20 selects the programs without the back-face test (0x4745C0, 0x472EB0, 0x4803F0); instances (0x471FA0)
    // and lines never cull [C §11.3].
    it.cull = !(flags & 0x20) && (type == 0 || type == 1 || type == 2 || type == 6);
    if (it.blend == b3gfx::Blend::kAlphaTest) it.blend = b3gfx::Blend::kOpaque;  // the shader tests with TEST_1
    if (d.transform >= 0 && std::size_t(d.transform) < m.transform_tints.size()) {
        // VU0 0x479610: RGBA = ftoi0(128.5 * w lanes), alpha 255 / 128 / 4 by flags 0x4 / 0x8 [C 0x00CA-0x00D6].
        const auto& t = m.transform_tints[std::size_t(d.transform)];
        for (int k = 0; k < 3; ++k) it.instance[k] = std::floor(128.5f * t[std::size_t(k)]) / 128.0f;
        it.instance[3] = (flags & 0x4) ? 255.0f : (flags & 0x8) ? 128.0f : 4.0f;
    }
}

// Sky layer -> vertex/index buffers + draw (kSky). Vertex colour 0x40: the dome texture is built in display units by
// build_backdrop and the clouds' colour is 0x40, so the half-intensity frame holds half the texel [docs/sky-enviro.md].
// `fade_top` (perf1, free-camera views only): the alpha falls linearly to 0 over the layer's top `fade_top` rings.
bool sky_item(b3gfx::Device& dev, const b3asset::SkyLayer& l, b3gfx::TextureId tex, b3gfx::DrawItem& it, int rings = 0, int fade_top = 0) {
    std::vector<b3gfx::Vertex> v(l.verts.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        b3gfx::Vertex& o = v[i];
        o = b3gfx::Vertex{};
        for (int k = 0; k < 3; ++k) o.pos[k] = l.verts[i].dir[k];
        o.uv[0] = l.verts[i].uv[0];
        o.uv[1] = l.verts[i].uv[1];
        o.rgba = 0x00404040u | std::uint32_t(l.rgba[3]) << 24;  // alpha as sent (the frame alpha the specular reads)
        if (fade_top > 0 && rings > 0) {   // vertex i of a segment is ring i (build_sky_layer's order)
            const int ring = int(i % std::size_t(rings + 1));
            const float k = std::clamp(float(rings - ring) / float(fade_top), 0.0f, 1.0f);
            o.rgba = 0x00404040u | std::uint32_t(std::lround(float(l.rgba[3]) * k)) << 24;
        }
    }
    it = b3gfx::DrawItem{};
    it.vertices = dev.create_vertex_buffer(v.data(), v.size());
    it.indices = dev.create_index_buffer(l.indices.data(), l.indices.size());
    if (it.vertices == b3gfx::kNone || it.indices == b3gfx::kNone) return false;
    it.index_count = std::uint32_t(l.indices.size());
    it.texture = tex;
    it.shading = b3gfx::Shading::kSky;
    it.blend = l.blend ? b3gfx::Blend::kAlpha : b3gfx::Blend::kOpaque;
    it.lighting = 0;
    return true;
}

}  // namespace

bool build_scene(b3gfx::Device& dev, b3asset::Model&& model, bool prelit, Scene& out, std::string* why, const Scene* share) {
    out = Scene{};
    out.model = std::move(model);
    out.prelit = prelit;
    const auto& m = out.model;
    if (m.vertices.empty()) { if (why) *why = "model has no vertices"; return false; }
    const b3gfx::BufferId vb = dev.create_vertex_buffer(reinterpret_cast<const b3gfx::Vertex*>(m.vertices.data()), m.vertices.size());
    const b3gfx::BufferId ib = dev.create_index_buffer(m.indices.data(), m.indices.size());
    if (vb == b3gfx::kNone || ib == b3gfx::kNone) { if (why) *why = "GPU buffer creation failed"; return false; }
    std::vector<b3gfx::TextureId> tex;
    bool same = share && share->textures.size() == m.textures.size() && share->model.textures.size() == m.textures.size();
    for (std::size_t i = 0; same && i < m.textures.size(); ++i) {
        const auto& a = m.textures[i];
        const auto& b = share->model.textures[i];
        same = a.name == b.name && a.levels.size() == b.levels.size() && !a.levels.empty() && a.levels[0].w == b.levels[0].w &&
               a.levels[0].h == b.levels[0].h;
    }
    if (same) tex = share->textures;
    else
        for (const auto& t : m.textures) tex.push_back(upload_texture(dev, t.levels, t.original));   // hdkey: and its original
    out.textures = tex;
    // hdgpu: compressed HD levels are on the GPU now: their CPU copy goes (the sizes stay: the LOD model's share check)
    for (auto& t : out.model.textures)
        for (auto& l : t.levels)
            if (l.bc) l.blocks.reset();
    for (const auto& d : m.draws) {
        b3gfx::DrawItem it;
        it.vertices = vb;
        it.indices = ib;
        it.first_index = d.first_index;
        it.index_count = d.index_count;
        it.texture = d.texture >= 0 && std::size_t(d.texture) < tex.size() ? tex[d.texture] : b3gfx::kNone;
        it.blend = d.blend == b3asset::DrawRange::kBlend ? b3gfx::Blend::kAlpha
                 : d.blend == b3asset::DrawRange::kAlphaTest ? b3gfx::Blend::kAlphaTest : b3gfx::Blend::kOpaque;
        for (int k = 0; k < 4; ++k) it.tint[k] = ((d.tint >> (8 * k)) & 0xFF) / 255.0f;
        it.lighting = prelit ? 0.0f : 1.0f;
        it.alpha_ref = d.alpha_ref;
        if (d.transform >= 0 && std::size_t(d.transform) < m.transforms.size())
            std::copy(m.transforms[d.transform].begin(), m.transforms[d.transform].end(), it.model);
        if (prelit) track_item(m, d, it);
        {   // perf4: the draw's vertices' box (the sun's occlusion pass leaves out what lies outside its box)
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            const std::size_t end = std::min<std::size_t>(std::size_t(d.first_index) + d.index_count, m.indices.size());
            for (std::size_t i = d.first_index; i < end; ++i) {
                const std::uint32_t vi = m.indices[i];
                if (vi >= m.vertices.size()) continue;
                const float* p = m.vertices[vi].pos;
                for (int a = 0; a < 3; ++a) { lo[a] = std::min(lo[a], p[a]); hi[a] = std::max(hi[a], p[a]); }
            }
            if (lo[0] <= hi[0] && lo[1] <= hi[1] && lo[2] <= hi[2] && std::isfinite(lo[0] + lo[1] + lo[2] + hi[0] + hi[1] + hi[2]))
                for (int a = 0; a < 3; ++a) { it.bounds[a] = lo[a]; it.bounds[3 + a] = hi[a]; }
        }
        if (d.anim >= 0 && std::size_t(d.anim) < m.track_anims.size()) out.anim_items.push_back(int(out.items.size()));
        out.items.push_back(it);
    }
    char buf[256];
    std::snprintf(buf, sizeof buf, "%zu vertices, %zu triangles, %zu draws, %zu textures", m.vertices.size(),
                  m.indices.size() / 3, m.draws.size(), m.textures.size());
    out.description = buf;
    return true;
}

void animate(Scene& scene, const std::vector<b3asset::TrackAnimState>& states) {
    const auto& m = scene.model;
    for (const int k : scene.anim_items) {
        const auto& d = m.draws[std::size_t(k)];
        if (std::size_t(d.anim) >= states.size()) continue;
        const b3asset::TrackAnim& a = m.track_anims[std::size_t(d.anim)];
        const b3asset::TrackAnimState& s = states[std::size_t(d.anim)];
        if (std::size_t(k) + scene.item_base >= scene.items.size()) continue;
        b3gfx::DrawItem& it = scene.items[std::size_t(k) + scene.item_base];
        if (s.cur >= 0 && std::size_t(s.cur) < a.frames.size() && std::size_t(a.frames[std::size_t(s.cur)]) < scene.textures.size())
            it.texture = scene.textures[std::size_t(a.frames[std::size_t(s.cur)])];
        // ReallySetMaterial sends the scroll for flag 0x200 whatever the texture count [C nonmatching :187]
        it.uv_scroll = (a.flags & 0x200) ? b3asset::track_uv_scroll(s.t) : 0.0f;
    }
}

const std::vector<b3asset::TrackAnimState>& AnimClock::at(const b3asset::Model& m, double seconds) {
    const auto& an = m.track_anims;
    if (state_.size() != an.size() || seconds < double(ticks_) / 60.0) {
        state_.clear();
        for (const auto& a : an) state_.push_back(b3asset::track_anim_prepare(a));
        ticks_ = 0;
    }
    while (double(ticks_ + 1) / 60.0 <= seconds) {
        ++ticks_;
        for (std::size_t k = 0; k < an.size(); ++k) b3asset::track_anim_update(an[k], state_[k], float(double(ticks_) / 60.0));
    }
    out_ = state_;
    if (seconds > 0)
        for (std::size_t k = 0; k < an.size(); ++k) b3asset::track_anim_update(an[k], out_[k], float(seconds));
    return out_;
}

void WaterTable::build(const Scene& sc, const std::vector<std::uint8_t>& st, const std::vector<std::uint8_t>& sm) {
    surface.clear(); reflect.clear(); unit.clear();
    for (std::size_t i = 0; i < sc.model.draws.size(); ++i) {
        const std::string& p = sc.model.draws[i].part;
        int k = 0;
        if (std::sscanf(p.c_str(), "water+30 %d", &k) == 1) reflect[k].push_back(int(i));
        else if (std::sscanf(p.c_str(), "water %d", &k) == 1) surface[k].push_back(int(i));
    }
    if (st.size() < 0x60) return;
    std::int16_t nu = 0;
    std::memcpy(&nu, st.data() + 0x54, 2);
    std::uint32_t utab = 0;
    std::memcpy(&utab, st.data() + 0x58, 4);
    for (int u = 0; u < nu; ++u) {
        std::array<float, 3> w = {-1, -1, 0};
        std::uint32_t hb = 0;
        if (std::size_t(utab) + 16u * std::size_t(u) + 4 <= st.size()) std::memcpy(&hb, st.data() + utab + 16 * std::size_t(u), 4);
        if (std::size_t(hb) + 0x74 <= sm.size()) {
            w[0] = float(std::int8_t(sm[hb + 0x6E]));
            w[1] = float(std::int8_t(sm[hb + 0x6F]));
            std::memcpy(&w[2], sm.data() + hb + 0x70, 4);
        }
        unit.push_back(w);
    }
}

bool WaterTable::is_water(int item) const {   // `item`: a model.draws index
    for (const auto* t : {&surface, &reflect})
        for (const auto& [g, v] : *t)
            if (std::find(v.begin(), v.end(), item) != v.end()) return true;
    return false;
}

void WaterTable::append(const Scene& sc, int u, std::vector<b3gfx::DrawItem>& out, int skip) const {
    if (u < 0 || std::size_t(u) >= unit.size() || skip == 1) return;
    const auto& uw = unit[std::size_t(u)];
    if (const auto it = reflect.find(int(uw[1])); uw[1] >= 0 && it != reflect.end() && skip != 3)
        for (int k : it->second) {
            b3gfx::DrawItem d = sc.items[std::size_t(k) + sc.item_base];
            // VU1 0x47E300 at 0x0055-0x005C: view matrix y column negated, translation + 2 level x y column, i.e. every
            // vertex at (x, 2 level - y, z) [C asm]; column-major model matrix
            const float m[16] = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 2.0f * uw[2], 0, 1};
            std::memcpy(d.model, m, sizeof m);
            d.track_type = 0;      // type 7 = the type-0 program (0x4745C0 + the mirror) [C]
            d.cull = false;        // no back-face test (and the mirror flips the winding)
            out.push_back(d);
        }
    if (const auto it = surface.find(int(uw[0])); uw[0] >= 0 && it != surface.end() && skip != 2)
        for (int k : it->second) out.push_back(sc.items[std::size_t(k) + sc.item_base]);
}

int WaterTable::nearest_unit(const b3asset::Model& m, const float eye[3]) {
    int best = -1;
    float bd = 0;
    for (std::size_t i = 0; i < m.waypoints.size(); ++i) {
        const auto& c = m.waypoints[i];
        const float dx = c[0] - eye[0], dz = c[2] - eye[2];
        const float d = dx * dx + dz * dz;
        if (best < 0 || d < bd) { best = int(i); bd = d; }
    }
    return best;
}

bool add_enviro(b3gfx::Device& dev, const std::string& route, Scene& scene) {
    const bool ok = read_route_enviro(route, scene.enviro);
    return ok && add_enviro(dev, scene);
}

bool read_route_enviro(const std::string& route, b3asset::Enviro& out) {
    std::vector<std::uint8_t> d;
    if (!b3asset::read_file(route + "/ENVIRO.DAT", d)) return false;
    b3asset::GsMemory gs;
    std::string why;
    if (!b3asset::read_enviro(b3asset::Bytes{d.data(), d.size()}, gs, out, &why)) {
        std::fprintf(stderr, "ENVIRO.DAT: %s\n", why.c_str());
        return false;
    }
    return true;
}

b3gfx::TextureId upload_backdrop(b3gfx::Device& dev, const b3asset::Enviro& env, const float to_sun[3]) {
    const b3asset::Image back = b3asset::build_backdrop(env, to_sun);
    std::vector<b3asset::Image> original;
    if (!env.gradient.original.empty()) {   // hdkey: the backdrop of the original gradient, for the HD TEXTURES key
        b3asset::Enviro e = env;
        e.gradient.levels = env.gradient.original;
        original.push_back(b3asset::build_backdrop(e, to_sun));
    }
    return upload_texture(dev, {back}, original);
}

bool add_enviro(b3gfx::Device& dev, Scene& scene) {
    scene.has_enviro = true;
    scene.fog = b3asset::fog_model(scene.enviro);
    const b3gfx::TextureId back_tex = upload_backdrop(dev, scene.enviro, scene.enviro.to_sun);
    const b3gfx::TextureId cloud_tex = upload_texture(dev, scene.enviro.clouds.levels, scene.enviro.clouds.original);
    // Frame order [C CB3World::Render B3WorldPS2_00276300.cpp:121-125]: sky components 0 and 1, (sun blob,) clouds.
    std::vector<b3gfx::DrawItem> sky;
    int cloud_at = -1;
    for (int c = 0; c < 3; ++c) {
        if (c == 2 && cloud_tex == b3gfx::kNone) continue;
        b3gfx::DrawItem it;
        const b3asset::SkyLayer layer = b3asset::build_sky_layer(c, b3asset::cloud_u_offset(0.0f));
        if (sky_item(dev, layer, c == 2 ? cloud_tex : back_tex, it)) {
            if (c == 2 && sky_item(dev, layer, cloud_tex, scene.cloud_free, 12, 3)) cloud_at = int(sky.size());   // perf1: 12 rings [C maDefinitions]
            sky.push_back(it);
        }
    }
    scene.cloud_item = cloud_at;   // the sky goes in front of items: its index there is its index in `sky`
    scene.items.insert(scene.items.begin(), sky.begin(), sky.end());
    scene.item_base += sky.size();
    return true;
}

void frame_params(const Scene& scene, const float eye[3], const float fwd[3], bool fog, bool specular,
                  b3gfx::FrameParams& fp) {
    for (int i = 0; i < 3; ++i) fp.eye[i] = eye[i];
    // Right-handed camera right = fwd x up (+Y).
    float r[3] = {-fwd[2], 0.0f, fwd[0]};
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (rl > 0) { r[0] /= rl; r[2] /= rl; }
    for (int i = 0; i < 3; ++i) fp.right[i] = r[i];
    if (!scene.prelit) return;
    fp.present_scale = 255.0f / 128.0f;
    if (!scene.has_enviro) {
        const float s[3] = {0.3f, 0.8f, -0.52f};
        const float l = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        for (int i = 0; i < 3; ++i) fp.to_sun[i] = s[i] / l;
        return;
    }
    const b3asset::Enviro& e = scene.enviro;
    fp.line_far = scene.fog.far;   // lines1: the camera's far plane (the line meshes' own fog)
    for (int i = 0; i < 3; ++i) {
        fp.to_sun[i] = e.to_sun[i];
        // RenderFullScreenSpecular: min(colour * 127.5 * intensity, 255) in GS bytes [C 0x2274B0].
        if (specular) fp.specular[i] = std::min(255.0f, e.specular_colour[i] * 127.5f * e.block[0].specular_scale) / 255.0f;
    }
    if (fog) {
        fp.fog[0] = scene.fog.z_scale;
        fp.fog[1] = scene.fog.z_shift;
        fp.fog[2] = scene.fog.fog_max;
        fp.fog[3] = scene.fog.fog_sky;
        for (int i = 0; i < 3; ++i) fp.fog_colour[i] = 0.5f * scene.fog.colour[i];  // half-intensity frame
        fp.fog_colour[3] = 1.0f;
    }
    // The game clears to black behind the sky [G]; the sky covers the background.
    fp.clear[0] = fp.clear[1] = fp.clear[2] = 0.0f;
}

}  // namespace b3view
