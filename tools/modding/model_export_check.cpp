// model_export_check: the census of mod kit part 3c's export (docs/modding/b3modtool.md "car export", "track export").
//   build/model_export_check <disc> <out dir> [route ...]
// Exports every player car (all LODs) and the given routes (default: all) into <out dir> as `b3modtool car export` /
// `track export` do, checks each glTF (gltf_check), reads it back (gltf_load_model) and compares it with the renderer's
// own decode of the same file (b3asset::read_car / read_track): vertex and triangle counts and bounds; for cars also a
// picture of each (b3asset::render_preview, same camera) and the share of pixels that differ. Writes nothing but
// <out dir>; the exported files are the game's data (keep them out of git, delete them after).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "b3asset/car.h"
#include "b3asset/gs_memory.h"
#include "b3asset/preview.h"
#include "b3asset/track.h"
#include "b3mod/mods.h"
#include "b3modtool/export3d.h"

using namespace b3modtool;
namespace fs = std::filesystem;

namespace {

bool write_all(const FileSet& fsx, const std::string& dir) {
    std::string why;
    for (const auto& [name, bytes] : fsx)
        if (!write_host_file(b3mod::path_utf8(fs::path(dir) / name), bytes, &why)) { std::fprintf(stderr, "%s\n", why.c_str()); return false; }
    return true;
}

bool check_gltf(const FileSet& fsx, const std::string& stem, std::string& msg) {
    Json doc;
    const Buf& t = fsx.at(stem + ".gltf");
    std::vector<std::string> errors;
    if (!json_parse(std::string_view(reinterpret_cast<const char*>(t.data()), t.size()), doc, &msg)) return false;
    if (!gltf_check(doc, fsx.at(stem + ".bin"), errors)) { msg = errors.front(); return false; }
    return true;
}

double bounds_diff(const b3asset::Model& a, const b3asset::Model& b) {
    double d = 0;
    for (int c = 0; c < 3; ++c) d = std::max({d, std::fabs(double(a.bmin[c]) - b.bmin[c]), std::fabs(double(a.bmax[c]) - b.bmax[c])});
    return d;
}

// share of pixels whose colour differs by more than 8 in any channel
double pixel_diff(const b3asset::Image& a, const b3asset::Image& b) {
    if (a.w != b.w || a.h != b.h) return 1.0;
    std::size_t n = 0, bad = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4, ++n)
        for (int c = 0; c < 3; ++c)
            if (std::abs(int(a.rgba[i + std::size_t(c)]) - int(b.rgba[i + std::size_t(c)])) > 8) { ++bad; break; }
    return n ? double(bad) / double(n) : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: model_export_check <disc> <out dir> [TRACKS/<area>/<route> ...]\n");
        return 2;
    }
    const std::string game = argv[1], out = argv[2];
    std::vector<std::string> want;
    for (int i = 3; i < argc; ++i) want.push_back(argv[i]);
    int failures = 0;
    // ---- cars ----
    std::size_t cars = 0, cars_valid = 0, lods_equal = 0, lods = 0, pics = 0;
    double worst_pix = 0, worst_bounds = 0;
    std::string note;
    for (const CarListEntry& c : car_list(game, false, &note)) {
        Buf file;
        if (!read_host_file(c.path, file)) { std::printf("%s: cannot read\n", c.key.c_str()); ++failures; continue; }
        std::string stem = c.key.substr(5, c.key.find('.') - 5);
        std::replace(stem.begin(), stem.end(), '/', '_');
        CarExportOptions o;
        o.name = c.name;
        o.source = c.key;
        FileSet fsx;
        ExportReport rep;
        std::string why;
        ++cars;
        if (!car_export(file, stem, o, fsx, &rep, &why)) { std::printf("%s: export failed: %s\n", c.key.c_str(), why.c_str()); ++failures; continue; }
        const std::string dir = b3mod::path_utf8(fs::path(out) / "cars" / stem);
        if (!write_all(fsx, dir)) return 1;
        std::string msg;
        const bool valid = check_gltf(fsx, stem, msg);
        cars_valid += valid;
        if (!valid) { std::printf("%s: glTF check: %s\n", c.key.c_str(), msg.c_str()); ++failures; }
        std::string line;
        for (int lod = 0; lod < 4; ++lod) {
            b3asset::GsMemory gs;
            b3asset::Model ref, got;
            b3asset::CarOptions co;
            co.lod = lod;
            if (!b3asset::read_car(b3asset::Bytes{file.data(), file.size()}, co, gs, ref, &why)) { std::printf("%s LOD %d: read_car: %s\n", c.key.c_str(), lod, why.c_str()); ++failures; continue; }
            std::vector<std::string> skip = {"_B1_", "_B2_", "extra_lists", "lights"};
            for (int k = 0; k < 5; ++k) if (k != lod) skip.push_back("LOD" + std::to_string(k));
            if (!gltf_load_model(b3mod::path_utf8(fs::path(dir) / (stem + ".gltf")), got, skip, &why)) { std::printf("%s: read back: %s\n", c.key.c_str(), why.c_str()); ++failures; continue; }
            ++lods;
            const double bd = bounds_diff(ref, got);
            worst_bounds = std::max(worst_bounds, bd);
            const bool same = ref.vertices.size() == got.vertices.size() && ref.indices.size() == got.indices.size() && bd < 1e-4;
            lods_equal += same;
            char t[160];
            std::snprintf(t, sizeof t, " L%d %zu/%zu v %zu/%zu t%s", lod, got.vertices.size(), ref.vertices.size(), got.indices.size() / 3, ref.indices.size() / 3, same ? "" : " DIFFER");
            line += t;
            if (!same) ++failures;
            if (lod == 3) {   // the same picture from both (unshaded: the export's normals are unit length, the decode's raw)
                b3asset::PreviewView v;
                v.shaded = false;
                const b3asset::Image a = b3asset::render_preview(ref, 320, 200, v), b = b3asset::render_preview(got, 320, 200, v);
                const double pd = pixel_diff(a, b);
                write_host_file(b3mod::path_utf8(fs::path(dir) / "check_decoder.png"), encode_png(a.w, a.h, a.rgba.data()));
                write_host_file(b3mod::path_utf8(fs::path(dir) / "check_export.png"), encode_png(b.w, b.h, b.rgba.data()));
                worst_pix = std::max(worst_pix, pd);
                ++pics;
                std::snprintf(t, sizeof t, "; picture %.3f%% px differ", 100 * pd);
                line += t;
            }
        }
        std::printf("%-20s %-26s %s%s\n", c.key.c_str(), c.name.c_str(), valid ? "valid" : "INVALID", line.c_str());
    }
    std::printf("cars: %zu exported, %zu glTF valid; %zu of %zu LODs read back with the decoder's vertex and triangle counts and bounds "
                "(worst bounds difference %.2g m); LOD 3 pictures: worst %.3f%% of pixels differ (%zu cars)\n",
                cars, cars_valid, lods_equal, lods, worst_bounds, 100 * worst_pix, pics);
    // ---- routes ----
    std::size_t routes = 0, routes_valid = 0, routes_equal = 0, sections = 0;
    std::uint64_t tris_all = 0;
    for (const RouteEntry& r : route_list(game)) {
        if (!want.empty() && std::find(want.begin(), want.end(), r.key) == want.end()) continue;
        Buf st, sm, en;
        if (!read_host_file(r.static_path, st)) { ++failures; continue; }
        if (!r.streamed_path.empty()) read_host_file(r.streamed_path, sm);
        if (!r.enviro_path.empty()) read_host_file(r.enviro_path, en);
        std::string stem = r.key.substr(7);
        std::replace(stem.begin(), stem.end(), '/', '_');
        TrackExportOptions o;
        o.source = r.key;
        FileSet fsx;
        ExportReport rep;
        std::string why;
        ++routes;
        if (!track_export(st, sm, en, stem, o, fsx, &rep, &why)) { std::printf("%s: export failed: %s\n", r.key.c_str(), why.c_str()); ++failures; continue; }
        const std::string dir = b3mod::path_utf8(fs::path(out) / "tracks" / stem);
        if (!write_all(fsx, dir)) return 1;
        std::string msg;
        const bool valid = check_gltf(fsx, stem, msg);
        routes_valid += valid;
        if (!valid) { std::printf("%s: glTF check: %s\n", r.key.c_str(), msg.c_str()); ++failures; }
        // the renderer's decode: triangles drawn (instances once per placement), high and LOD passes
        auto drawn = [&](bool lod) {
            b3asset::GsMemory gs;
            b3asset::Model m;
            b3asset::TrackOptions to;
            to.extra_backdrops = true;
            to.lod = lod;
            if (lod) to.backdrops = to.instances = to.props = false;
            std::uint64_t t = 0;
            if (lod && sm.empty()) return t;
            if (!b3asset::read_track(b3asset::Bytes{st.data(), st.size()}, b3asset::Bytes{sm.data(), sm.size()}, to, gs, m, &why)) return std::uint64_t(~0ull);
            for (const auto& d : m.draws) t += d.index_count / 3;
            return t;
        };
        const std::uint64_t hi = drawn(false), lo = drawn(true);
        b3asset::Model got, got_hi;
        if (!gltf_load_model(b3mod::path_utf8(fs::path(dir) / (stem + ".gltf")), got, {}, &why) ||
            !gltf_load_model(b3mod::path_utf8(fs::path(dir) / (stem + ".gltf")), got_hi, {"_lod"}, &why)) {
            std::printf("%s: read back: %s\n", r.key.c_str(), why.c_str());
            ++failures;
            continue;
        }
        const std::uint64_t g_all = got.indices.size() / 3, g_hi = got_hi.indices.size() / 3;
        const bool same = g_hi == hi && g_all - g_hi == lo;
        routes_equal += same;
        if (!same) ++failures;
        int units = 0;
        if (!sm.empty() && st.size() > 0x56) units = std::int16_t(st[0x54] | st[0x55] << 8);
        sections += std::size_t(units);
        tris_all += g_all;
        std::printf("%-18s %s; %3d sections, %zu meshes, %zu nodes, %llu vertices; triangles placed: high %llu (decoder %llu), LOD %llu (decoder %llu)%s; "
                    "bounds (%.0f %.0f %.0f) .. (%.0f %.0f %.0f)\n",
                    r.key.c_str(), valid ? "valid" : "INVALID", units, rep.meshes, rep.nodes, (unsigned long long)rep.vertices,
                    (unsigned long long)g_hi, (unsigned long long)hi, (unsigned long long)(g_all - g_hi), (unsigned long long)lo, same ? "" : " DIFFER",
                    got.bmin[0], got.bmin[1], got.bmin[2], got.bmax[0], got.bmax[1], got.bmax[2]);
    }
    std::printf("routes: %zu exported (%zu sections), %zu glTF valid, %zu with the decoder's triangle counts; %llu triangles placed in all\n",
                routes, sections, routes_valid, routes_equal, (unsigned long long)tris_all);
    std::printf("model_export_check: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
