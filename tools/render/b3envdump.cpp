// b3envdump: offline decoding of a route's ENVIRO.DAT and the sky model (docs/sky-enviro.md).
//   b3envdump <route dir> [outdir]          parameters, fog curve; PNGs of the four rasters and the backdrop into outdir
//   b3envdump <route dir> --vu <vu dir>     also run the sky dome VU1 program 0x46A590 (decomp
//                                           assembly/assets/vu/game_0046A590.textbin.bin, read only) in the VU1
//                                           interpreter and compare its vertices with build_sky_layer()
// Game data and the decomp are read only; outputs go wherever outdir points (keep them out of git).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "b3asset/enviro.h"
#include "b3asset/export.h"
#include "b3asset/png.h"
#include "b3asset/vu1.h"

using namespace b3asset;

namespace {

void put(VuMemory& m, int q, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    m.w[q * 4] = a; m.w[q * 4 + 1] = b; m.w[q * 4 + 2] = c; m.w[q * 4 + 3] = d;
}
std::uint32_t fb(float f) { std::uint32_t v; std::memcpy(&v, &f, 4); return v; }
void putf(VuMemory& m, int q, float a, float b, float c, float d) { put(m, q, fb(a), fb(b), fb(c), fb(d)); }

// Runs the sky program for one component with a camera that maps world axis `axis` (0 = x, 2 = z) to screen x and y
// to screen y without perspective (w = 1), and returns per GS vertex (axis value, y, u, v, rgba).
struct Out { float a, y, u, v; std::uint8_t rgba[4]; };
bool run_sky(const VuMicroprogram& prog, int comp, int axis, std::vector<Out>& out) {
    // maDefinitions 0x4B9CA0 -> CB3SkyDomeHemisphereParams::Construct 0x21E490 (12 qwords sent to VU1 1003).
    struct D { float e0, e1; std::uint32_t flag; float sign, us, v0, v1; std::uint32_t segs, rings, mode, blend; };
    static const D defs[3] = {{0, 1, 0, -1, 1, 0.765625f, 1.015625f, 32, 12, 0, 0},
                              {0, 1, 0, 1, 1, 0.765625f, 0.015625f, 32, 12, 0, 0},
                              {0, 0.75f, 1, 1, 2, 1, 0, 64, 12, 0, 1}};
    const D& d = defs[comp];
    const float fa = 2 * d.e0 - d.e0 * d.e0, fbb = 2 * d.e1 - d.e1 * d.e1, k = 1 / (fbb - fa);
    VuMemory mem;
    put(mem, 1003, fb(1.0f / float(d.segs)), fb(d.sign), d.segs, d.flag);
    put(mem, 1004, fb((d.e1 - d.e0) / float(d.rings)), fb(d.e0), d.rings, d.mode);
    putf(mem, 1005, 0, d.us / float(d.segs), k * (d.v0 * fbb - d.v1 * fa), k * (d.v1 - d.v0));
    if (d.blend) put(mem, 1006, 0x40, 0x40, 0x40, 0x80); else put(mem, 1006, 0x80, 0x80, 0x80, comp == 0 ? 0x20 : 0);
    for (int q = 1007; q <= 1014; ++q) put(mem, q, 0, 0, 0x50 + std::uint32_t(q - 1007), 0);  // A+D slots (not decoded)
    const float W = 1000.0f;
    for (int i = 0; i < 3; ++i) putf(mem, 1015 + i, i == axis ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, 0, 0);
    putf(mem, 1018, 0.5f, 0.5f, 1, 1);    // translation row: w = 1, x/y cancel the program's -0.5 w
    putf(mem, 1019, W, W, 2048, 2048);
    putf(mem, 1020, 1e4f, 1e-4f, -0.0002f, 5.0f);
    putf(mem, 1021, 1e4f, 1e-4f, 1.000040054f, 9.999999747e-05f);
    put(mem, 1022, 0, 0x302A4000u, 0x512, 0);  // CB3SkyDomeRenderer::Construct mCameraParams[7]/[8] GIF tags [C]
    put(mem, 1023, 8, 0x10000000u, 0xE, 0);
    Vu1 vu(mem);
    std::string why;
    const auto kick = [&](std::uint32_t qa) {
        GifPacket p;
        if (!read_gif_packet(mem, qa, p, &why)) return;
        for (const GifVertex& v : p.verts)
            out.push_back({float((v.x / 16.0 - 2048.0) / W), float((v.y / 16.0 - 2048.0) / W), v.s, v.t, {v.r, v.g, v.b, v.a}});
    };
    if (!vu.run(prog, 0, 0, 0, kick, &why)) { std::fprintf(stderr, "VU1: %s\n", why.c_str()); return false; }
    return true;
}

int vu_check(const std::string& vu_dir) {
    std::vector<std::uint8_t> d;
    const std::string path = vu_dir + "/game_0046A590.textbin.bin";
    if (!read_file(path, d)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
    VuMicroprogram prog;
    std::string why;
    if (!prog.load_textbin(d, &why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
    int bad_total = 0;
    for (int comp = 0; comp < 3; ++comp) {
        std::vector<Out> ox, oz;
        if (!run_sky(prog, comp, 0, ox) || !run_sky(prog, comp, 2, oz) || ox.size() != oz.size()) return 1;
        const SkyLayer L = build_sky_layer(comp, 0.0f);
        // Match every GS vertex to the nearest model vertex (same u within 1e-4 and v); report the worst distances.
        double worst_pos = 0, worst_uv = 0;
        int unmatched = 0, colour_bad = 0;
        for (std::size_t i = 0; i < ox.size(); ++i) {
            const float p[3] = {ox[i].a, ox[i].y, oz[i].a};
            double best = 1e9, best_uv = 1e9;
            for (const SkyVertex& v : L.verts) {
                const double du = v.uv[0] - ox[i].u, dv = v.uv[1] - ox[i].v;
                const double duv = std::sqrt(du * du + dv * dv);
                if (duv > 1e-3) continue;
                const double dp = std::sqrt(std::pow(v.dir[0] - p[0], 2) + std::pow(v.dir[1] - p[1], 2) + std::pow(v.dir[2] - p[2], 2));
                if (dp < best) { best = dp; best_uv = duv; }
            }
            if (best > 1e8) { ++unmatched; continue; }
            worst_pos = std::max(worst_pos, best);
            worst_uv = std::max(worst_uv, best_uv);
            if (std::memcmp(ox[i].rgba, L.rgba, 4) != 0) ++colour_bad;
        }
        const bool ok = unmatched == 0 && colour_bad == 0 && worst_pos < 2e-3 && worst_uv < 1e-4;
        bad_total += ok ? 0 : 1;
        std::printf("sky component %d: %zu GS vertices (%zu model vertices): unmatched %d, colour mismatches %d, "
                    "max direction error %.2e, max uv error %.2e  %s\n", comp, ox.size(), L.verts.size(), unmatched,
                    colour_bad, worst_pos, worst_uv, ok ? "OK" : "MISMATCH");
    }
    return bad_total ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: b3envdump <route dir> [outdir] | <route dir> --vu <vu dir>\n");
        return 2;
    }
    const std::string route = argv[1];
    std::string outdir, vu_dir;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--vu" && i + 1 < argc) vu_dir = argv[++i];
        else outdir = argv[i];
    }
    std::vector<std::uint8_t> d;
    if (!read_file(route + "/ENVIRO.DAT", d)) { std::fprintf(stderr, "no ENVIRO.DAT in %s\n", route.c_str()); return 1; }
    GsMemory gs;
    Enviro e;
    std::string why;
    if (!read_enviro(Bytes{d.data(), d.size()}, gs, e, &why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
    const EnviroBlock& b = e.block[0];
    std::printf("ENVIRO.DAT %zu bytes (header +0xAC says %u)\n", d.size(), e.file_size);
    std::printf("fog colour      %.4f %.4f %.4f\n", b.fog_colour[0], b.fog_colour[1], b.fog_colour[2]);
    std::printf("near scale %.1f  fog max %.3f  fog sky %.3f  specular scale %.3f  sun w %.3f\n", b.near_scale, b.fog_max,
                b.fog_sky, b.specular_scale, b.sun_w);
    std::printf("specular colour %.4f %.4f %.4f\n", e.specular_colour[0], e.specular_colour[1], e.specular_colour[2]);
    std::printf("sun colour      %.4f %.4f %.4f\n", e.sun_colour[0], e.sun_colour[1], e.sun_colour[2]);
    std::printf("to sun          %.4f %.4f %.4f   corona scale %.3f\n", e.to_sun[0], e.to_sun[1], e.to_sun[2], e.corona_scale);
    const DecodedRaster* r[4] = {&e.gradient, &e.clouds, &e.sky_envmap, &e.sun_corona};
    const char* nm[4] = {"gradient", "clouds", "skyenv", "corona"};
    for (int k = 0; k < 4; ++k)
        if (!r[k]->levels.empty())
            std::printf("%-8s '%s' %ux%u psm 0x%x\n", nm[k], r[k]->hdr.name.c_str(), r[k]->hdr.width, r[k]->hdr.height, r[k]->hdr.psm());
    const FogModel f = fog_model(e);
    std::printf("fog: near %.4f far %.0f zScale %.1f zShift %.2f; amount at w =", f.near, f.far, f.z_scale, f.z_shift);
    for (float w : {10.0f, 50.0f, 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 9000.0f}) std::printf(" %g:%.3f", w, fog_amount(f, w));
    std::printf("\n");
    if (!outdir.empty()) {
        for (int k = 0; k < 4; ++k)
            if (!r[k]->levels.empty()) {
                const Image& im = r[k]->levels[0];
                write_png_rgba(outdir + "/" + nm[k] + ".png", im.w, im.h, im.rgba.data());
            }
        const Image bd = build_backdrop(e, e.to_sun);
        write_png_rgba(outdir + "/backdrop.png", bd.w, bd.h, bd.rgba.data());
        std::printf("PNGs written to %s\n", outdir.c_str());
    }
    if (!vu_dir.empty()) return vu_check(vu_dir);
    return 0;
}
