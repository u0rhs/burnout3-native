// An exported glTF read back into a b3asset::Model (include/b3modtool/export3d.h gltf_load_model): what `b3modtool
// preview` draws, and the check that compares an export with the renderer's own decode (tools/modding/model_export_check.cpp).
#include <cmath>
#include <filesystem>
#include <map>

#include "b3mod/mods.h"
#include "b3modtool/export3d.h"

namespace b3modtool {

namespace {
bool fail(std::string* why, const std::string& m) { if (why) *why = m; return false; }
}  // namespace

bool gltf_load_model(const std::string& path, b3asset::Model& out, const std::vector<std::string>& skip, std::string* why) {
    namespace fs = std::filesystem;
    out = b3asset::Model{};
    Buf text, bin;
    if (!read_host_file(path, text)) return fail(why, "cannot read " + path);
    Json doc;
    std::string err;
    if (!json_parse(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()), doc, &err)) return fail(why, path + ": " + err);
    const fs::path dir = b3mod::path_from_utf8(path).parent_path();
    const Json* bufs = doc.get("buffers");
    if (!bufs || bufs->size() != 1 || !bufs->a[0].get("uri")) return fail(why, "one external buffer expected");
    if (!read_host_file(b3mod::path_utf8(dir / b3mod::path_from_utf8(bufs->a[0].get("uri")->s)), bin)) return fail(why, "cannot read the .bin");
    const Json* nodes = doc.get("nodes");
    const Json* meshes = doc.get("meshes");
    const Json* mats = doc.get("materials");
    const Json* texs = doc.get("textures");
    const Json* imgs = doc.get("images");
    if (!nodes || !meshes) return fail(why, "no nodes or meshes");
    std::map<int, int> tex_of_image;
    auto texture = [&](int mat) -> int {
        if (!mats || mat < 0 || std::size_t(mat) >= mats->size()) return -1;
        const Json* pbr = mats->a[std::size_t(mat)].get("pbrMetallicRoughness");
        const Json* bt = pbr ? pbr->get("baseColorTexture") : nullptr;
        if (!bt || !texs || !imgs) return -1;
        const int ti = int(bt->get("index") ? bt->get("index")->as_int(-1) : -1);
        if (ti < 0 || std::size_t(ti) >= texs->size()) return -1;
        const int ii = int(texs->a[std::size_t(ti)].get("source") ? texs->a[std::size_t(ti)].get("source")->as_int(-1) : -1);
        if (ii < 0 || std::size_t(ii) >= imgs->size() || !imgs->a[std::size_t(ii)].get("uri")) return -1;
        auto it = tex_of_image.find(ii);
        if (it != tex_of_image.end()) return it->second;
        Buf png;
        b3asset::Texture t;
        t.name = imgs->a[std::size_t(ii)].get("uri")->s;
        b3asset::Image im;
        if (!read_host_file(b3mod::path_utf8(dir / b3mod::path_from_utf8(t.name)), png) || !decode_png(png, im.w, im.h, im.rgba)) return tex_of_image[ii] = -1;
        t.levels.push_back(std::move(im));
        out.textures.push_back(std::move(t));
        return tex_of_image[ii] = int(out.textures.size() - 1);
    };
    // walk the default scene's roots with their world matrices
    std::vector<std::pair<int, std::array<float, 16>>> stack;
    const std::array<float, 16> id{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const Json* scenes = doc.get("scenes");
    const int sc = int(doc.get("scene") ? doc.get("scene")->as_int() : 0);
    if (!scenes || std::size_t(sc) >= scenes->size() || !scenes->a[std::size_t(sc)].get("nodes")) return fail(why, "no scene");
    for (const Json& r : scenes->a[std::size_t(sc)].get("nodes")->a) stack.push_back({int(r.n), id});
    std::size_t guard = 0;
    while (!stack.empty()) {
        const auto [ni, parent] = stack.back();
        stack.pop_back();
        if (ni < 0 || std::size_t(ni) >= nodes->size() || ++guard > 10 * nodes->size()) return fail(why, "bad node hierarchy");
        const Json& n = nodes->a[std::size_t(ni)];
        const std::string name = n.get("name") ? n.get("name")->s : "";
        bool skipped = false;
        for (const std::string& s : skip) skipped = skipped || (!s.empty() && name.find(s) != std::string::npos);
        if (skipped) continue;
        const std::array<float, 16> M = gltf_mul(parent, gltf_node_matrix(n));
        if (const Json* ch = n.get("children"))
            for (const Json& c : ch->a) stack.push_back({int(c.n), M});
        const Json* mj = n.get("mesh");
        if (!mj) continue;
        const int mi = int(mj->as_int(-1));
        if (mi < 0 || std::size_t(mi) >= meshes->size()) return fail(why, "bad mesh");
        const Json* prims = meshes->a[std::size_t(mi)].get("primitives");
        for (const Json& p : prims ? prims->a : std::vector<Json>{}) {
            const Json* at = p.get("attributes");
            if (!at || !at->get("POSITION")) continue;
            std::vector<double> pos, nrm, uv, col, idx;
            int np = 0, nn = 0, nu = 0, nc = 0, ni2 = 0;
            if (!gltf_read_accessor(doc, bin, int(at->get("POSITION")->as_int()), pos, np, why) || np != 3) return false;
            if (at->get("NORMAL") && !gltf_read_accessor(doc, bin, int(at->get("NORMAL")->as_int()), nrm, nn, why)) return false;
            if (at->get("TEXCOORD_0") && !gltf_read_accessor(doc, bin, int(at->get("TEXCOORD_0")->as_int()), uv, nu, why)) return false;
            if (at->get("COLOR_0") && !gltf_read_accessor(doc, bin, int(at->get("COLOR_0")->as_int()), col, nc, why)) return false;
            const std::size_t nv = pos.size() / 3;
            if (p.get("indices")) {
                if (!gltf_read_accessor(doc, bin, int(p.get("indices")->as_int()), idx, ni2, why)) return false;
            } else {
                for (std::size_t k = 0; k < nv; ++k) idx.push_back(double(k));
            }
            const std::uint32_t base = std::uint32_t(out.vertices.size());
            for (std::size_t k = 0; k < nv; ++k) {
                b3asset::Vertex v{};
                const float pp[3] = {float(pos[3 * k]), float(pos[3 * k + 1]), float(pos[3 * k + 2])};
                const auto w = gltf_apply(M, pp, 1.0f);
                for (int c = 0; c < 3; ++c) v.pos[c] = w[std::size_t(c)];
                if (nn == 3) {
                    const float nv3[3] = {float(nrm[3 * k]), float(nrm[3 * k + 1]), float(nrm[3 * k + 2])};
                    const auto wn = gltf_apply(M, nv3, 0.0f);
                    for (int c = 0; c < 3; ++c) v.normal[c] = wn[std::size_t(c)];
                } else {
                    v.normal[1] = 1.0f;
                }
                if (nu == 2) { v.uv[0] = float(uv[2 * k]); v.uv[1] = float(uv[2 * k + 1]); }
                v.rgba = 0xFFFFFFFFu;
                if (nc >= 3) {
                    v.rgba = 0;
                    for (int c = 0; c < 4; ++c) {
                        const double x = c < nc ? col[k * std::size_t(nc) + std::size_t(c)] : 1.0;
                        v.rgba |= std::uint32_t(std::lround(std::min(1.0, std::max(0.0, x)) * 255.0)) << (8 * c);
                    }
                }
                out.grow_bounds(v.pos);
                out.vertices.push_back(v);
            }
            b3asset::DrawRange dr;
            dr.first_index = std::uint32_t(out.indices.size());
            for (double x : idx) {
                if (x < 0 || std::size_t(x) >= nv) return fail(why, "index out of range");
                out.indices.push_back(base + std::uint32_t(x));
            }
            dr.index_count = std::uint32_t(idx.size() / 3 * 3);
            out.indices.resize(dr.first_index + dr.index_count);
            const int mat = int(p.get("material") ? p.get("material")->as_int(-1) : -1);
            dr.texture = texture(mat);
            dr.part = name;
            dr.blend = b3asset::DrawRange::kOpaque;
            if (mats && mat >= 0 && std::size_t(mat) < mats->size()) {
                const Json& mm = mats->a[std::size_t(mat)];
                const std::string mode = mm.get("alphaMode") ? mm.get("alphaMode")->s : "OPAQUE";
                const Json* pbr = mm.get("pbrMetallicRoughness");
                const Json* f = pbr ? pbr->get("baseColorFactor") : nullptr;
                const float a = f && f->size() == 4 ? float(f->a[3].n) : 1.0f;
                if (mode == "BLEND") { dr.blend = b3asset::DrawRange::kBlend; dr.tint = 0x00FFFFFFu | std::uint32_t(std::lround(a * 255)) << 24; }
                else if (mode == "MASK") { dr.blend = b3asset::DrawRange::kAlphaTest; dr.alpha_ref = mm.get("alphaCutoff") ? float(mm.get("alphaCutoff")->n) : 0.5f; }
            }
            if (dr.index_count) out.draws.push_back(dr);
        }
    }
    out.stats["vertices"] = out.vertices.size();
    out.stats["triangles"] = out.indices.size() / 3;
    return true;
}

}  // namespace b3modtool
