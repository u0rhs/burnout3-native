// b3assetdump: offline decoding of the game's render data (docs/native-renderer.md "Asset decoders").
//   b3assetdump txd <file.TXD> [outdir]                 decode every texture; PNGs into outdir
//   b3assetdump car <CARn.BGV|BTV> [outdir] [lod] [pal] decode a car; OBJ/MTL/PNG + a preview PNG into outdir
//   b3assetdump track <route dir> [outdir]              decode a track (STATIC.DAT + STREAMED.DAT); stats, preview PNGs
//   b3assetdump census <game dir>                       decode everything known over the whole install; totals
// Game data is read only; outputs go wherever outdir points (keep them out of git).
#include <algorithm>
#include <array>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "b3asset/car.h"
#include "b3asset/export.h"
#include "b3asset/png.h"
#include "b3asset/preview.h"
#include "b3asset/raster.h"
#include "b3asset/track.h"

using namespace b3asset;
namespace fs = std::filesystem;

namespace {

int cmd_txd(const std::string& path, const std::string& outdir) {
    std::vector<std::uint8_t> d;
    if (!read_file(path, d)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
    Bytes f{d.data(), d.size()};
    std::vector<TxdEntry> es;
    std::string why;
    if (!read_txd(f, es, &why)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), why.c_str()); return 1; }
    GsMemory gs;
    int ok = 0;
    for (std::size_t i = 0; i < es.size(); ++i) {
        DecodedRaster r;
        if (!decode_raster(f, es[i].offset, gs, r, 0, &why)) {
            std::printf("  entry %zu @0x%zx: %s\n", i, es[i].offset, why.c_str());
            continue;
        }
        ++ok;
        std::printf("  %3zu %-28s %4ux%-4u %-4s levels %u%s%s\n", i, r.hdr.name.c_str(), r.hdr.width, r.hdr.height,
                    psm_name(r.hdr.psm()), r.hdr.levels, r.translucent ? " translucent" : "",
                    r.max_alpha > 0x80 ? " alpha>1" : "");
        if (!outdir.empty()) {
            const std::string p = outdir + "/" + std::to_string(i) + "_" + r.hdr.name + ".png";
            write_png_rgba(p, r.levels[0].w, r.levels[0].h, r.levels[0].rgba.data());
        }
    }
    std::printf("%s: %d of %zu textures decoded\n", path.c_str(), ok, es.size());
    return ok == int(es.size()) ? 0 : 2;
}

int cmd_car(const std::string& path, const std::string& outdir, int lod, int pal) {
    std::vector<std::uint8_t> d;
    if (!read_file(path, d)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
    GsMemory gs;
    Model m;
    std::string why;
    CarOptions opt;
    opt.lod = lod;
    opt.palette = pal;
    if (!read_car(Bytes{d.data(), d.size()}, opt, gs, m, &why)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), why.c_str()); return 1; }
    for (auto& kv : m.stats) std::printf("  %-28s %llu\n", kv.first.c_str(), (unsigned long long)kv.second);
    std::printf("  bounds (%.3f %.3f %.3f) .. (%.3f %.3f %.3f)\n", m.bmin[0], m.bmin[1], m.bmin[2], m.bmax[0], m.bmax[1], m.bmax[2]);
    if (!outdir.empty()) {
        const std::string stem = fs::path(path).parent_path().filename().string() + "_" + fs::path(path).stem().string();
        write_obj(m, outdir, stem);
        for (int k = 0; k < 3; ++k) {
            PreviewView v;
            v.yaw_deg = 35.0f + 120.0f * k;
            const Image im = render_preview(m, 640, 400, v);
            write_png_rgba(outdir + "/" + stem + "_view" + std::to_string(k) + ".png", im.w, im.h, im.rgba.data());
        }
    }
    return 0;
}

bool load_track(const std::string& dir, Model& m, std::string* why, const TrackOptions& opt = TrackOptions{}) {
    std::vector<std::uint8_t> st, sm;
    if (!read_file(dir + "/STATIC.DAT", st)) { if (why) *why = "no STATIC.DAT"; return false; }
    read_file(dir + "/STREAMED.DAT", sm);
    GsMemory gs;
    return read_track(Bytes{st.data(), st.size()}, Bytes{sm.data(), sm.size()}, opt, gs, m, why);
}

int cmd_track(const std::string& dir, const std::string& outdir) {
    Model m;
    std::string why;
    if (!load_track(dir, m, &why)) { std::fprintf(stderr, "%s: %s\n", dir.c_str(), why.c_str()); return 1; }
    for (auto& kv : m.stats) std::printf("  %-36s %llu\n", kv.first.c_str(), (unsigned long long)kv.second);
    std::printf("  transforms %zu; bounds (%.1f %.1f %.1f) .. (%.1f %.1f %.1f)\n", m.transforms.size(), m.bmin[0], m.bmin[1],
                m.bmin[2], m.bmax[0], m.bmax[1], m.bmax[2]);
    if (!outdir.empty()) {
        const std::string stem = fs::path(dir).parent_path().filename().string() + "_" + fs::path(dir).filename().string();
        PreviewView v;
        v.pitch_deg = 80.0f;
        v.yaw_deg = 0.0f;
        const Image im = render_preview(m, 1024, 1024, v);
        write_png_rgba(outdir + "/" + stem + "_top.png", im.w, im.h, im.rgba.data());
    }
    return 0;
}

int cmd_census(const std::string& game) {
    GsMemory gs;
    std::uint64_t tex_ok = 0, tex_all = 0, cars = 0, cars_ok = 0, verts = 0, tris = 0, batches = 0;
    std::string why;
    for (const char* t : {"DATA/GLOBAL.TXD", "DATA/FRONTEND.TXD"}) {
        std::vector<std::uint8_t> d;
        if (!read_file(game + "/" + t, d)) continue;
        Bytes f{d.data(), d.size()};
        std::vector<TxdEntry> es;
        if (!read_txd(f, es, &why)) continue;
        for (const auto& e : es) {
            DecodedRaster r;
            ++tex_all;
            tex_ok += decode_raster(f, e.offset, gs, r, 0, &why);
        }
    }
    std::printf("TXD textures: %llu of %llu decoded\n", (unsigned long long)tex_ok, (unsigned long long)tex_all);
    // Rasters embedded in track and data files, found by a validated header scan (cType 4, TEX0 consistent with the
    // size, upload chain inside the file) until the track readers walk them structurally.
    std::map<std::string, std::array<std::uint64_t, 3>> embedded;  // kind -> files, rasters, decoded
    for (const auto& de : fs::recursive_directory_iterator(game)) {
        if (!de.is_regular_file()) continue;
        const std::string n = de.path().filename().string(), ext = de.path().extension().string();
        const std::string kind = (n == "STATIC.DAT" || n == "ENVIRO.DAT") ? n : (ext == ".BIN" && de.path().parent_path().filename() == "DATA") ? std::string("DATA/*.BIN") : "";
        if (kind.empty()) continue;
        std::vector<std::uint8_t> d;
        if (!read_file(de.path().string(), d)) continue;
        Bytes f{d.data(), d.size()};
        auto& e = embedded[kind];
        ++e[0];
        for (std::size_t o = 0; o + 0xC8 <= d.size(); o += 16) {
            if (d[o + 0x20] != 4) continue;
            RasterHeader h;
            if (!read_raster_header(f, o, h)) continue;
            ++e[1];
            DecodedRaster r;
            e[2] += decode_raster(f, o, gs, r, 0, &why);
        }
    }
    for (const auto& kv : embedded)
        std::printf("%-10s %4llu files: %6llu rasters found, %6llu decoded\n", kv.first.c_str(), (unsigned long long)kv.second[0],
                    (unsigned long long)kv.second[1], (unsigned long long)kv.second[2]);
    std::vector<fs::path> carfiles;
    for (const auto& de : fs::recursive_directory_iterator(game + "/PVEH")) {
        const std::string ext = de.path().extension().string();
        if (ext == ".BGV" || ext == ".BTV") carfiles.push_back(de.path());
    }
    std::sort(carfiles.begin(), carfiles.end());
    for (const auto& p : carfiles) {
        std::vector<std::uint8_t> d;
        if (!read_file(p.string(), d)) continue;
        ++cars;
        for (int lod = 0; lod < 4; ++lod) {
            Model m;
            CarOptions opt;
            opt.lod = lod;
            if (!read_car(Bytes{d.data(), d.size()}, opt, gs, m, &why)) {
                std::printf("  %s lod %d: %s\n", p.string().c_str(), lod, why.c_str());
                goto next;
            }
            if (lod == 3) { verts += m.vertices.size(); tris += m.indices.size() / 3; batches += m.stats["batches"]; }
        }
        ++cars_ok;
    next:;
    }
    // Tracks: every route folder with a STATIC.DAT, all parts.
    std::vector<fs::path> routes;
    for (const auto& de : fs::recursive_directory_iterator(game + "/TRACKS"))
        if (de.is_regular_file() && de.path().filename() == "STATIC.DAT") routes.push_back(de.path().parent_path());
    std::sort(routes.begin(), routes.end());
    std::map<std::string, std::uint64_t> tot;
    std::uint64_t routes_ok = 0;
    for (const auto& r : routes) {
        Model m;
        TrackOptions all;
        all.extra_backdrops = true;
        all.lines = true;
        if (!load_track(r.string(), m, &why, all)) { std::printf("  %s: %s\n", r.string().c_str(), why.c_str()); continue; }
        ++routes_ok;
        for (const auto& kv : m.stats) tot[kv.first] += kv.second;
        // The LOD meshes too (counted only).
        Model l;
        all.lod = true;
        all.backdrops = all.instances = all.props = false;
        if (load_track(r.string(), l, &why, all)) {
            tot["LOD meshes"] += l.stats["meshes"];
            tot["LOD batches"] += l.stats["batches"];
            tot["LOD vertices"] += l.vertices.size();
        }
    }
    std::printf("tracks: %llu of %zu routes decoded (all parts incl. +0x28 backdrops and lines):\n", (unsigned long long)routes_ok, routes.size());
    for (const auto& kv : tot) std::printf("  %-36s %llu\n", kv.first.c_str(), (unsigned long long)kv.second);
    std::printf("cars: %llu of %llu decoded at LOD 0-3; LOD 3 totals: %llu batches, %llu vertices, %llu triangles\n",
                (unsigned long long)cars_ok, (unsigned long long)cars, (unsigned long long)batches,
                (unsigned long long)verts, (unsigned long long)tris);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: b3assetdump txd <file> [outdir] | car <file> [outdir] [lod] [palette] | track <route dir> [outdir] | census <game dir>\n");
        return 1;
    }
    const std::string cmd = argv[1];
    const std::string out = argc > 3 ? argv[3] : "";
    if (!out.empty()) fs::create_directories(out);
    if (cmd == "txd") return cmd_txd(argv[2], out);
    if (cmd == "car") return cmd_car(argv[2], out, argc > 4 ? std::atoi(argv[4]) : 3, argc > 5 ? std::atoi(argv[5]) : 0);
    if (cmd == "track") return cmd_track(argv[2], out);
    if (cmd == "census") return cmd_census(argv[2]);
    std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
    return 1;
}
