// b3hdkeys: PCSX2 texture-replacement keys of the game's rasters, looked up in an HD pack (docs/native-renderer.md §18).
//   b3hdkeys xxh <file> [len...]                 XXH3-64 of the file's first `len` bytes (default: all), for oracle checks
//   b3hdkeys scan <pack dir> <file|dir>... [--matched out.txt] [--keys out.tsv] [--list]
//       every raster in the files (TXD directory, else a validated header scan), every palette variant, keys with the
//       base level only and with the stored mip levels; counts of pack hits; --matched writes the hit pack paths
//   b3hdkeys key <file> <raster offset hex> [cbp_add]   one raster's key, verbose
//   b3hdkeys compare <pack dir> <file>...             original vs replacement mean alpha / colour per hit (pack QA), and
//                                                     how many of the pack's files a decode finds (xboximport: all)
//   b3hdkeys import <xbox image> <game dir> <out dir> [--no-cars] [--threads n]   Import Xbox textures (b3asset/xboximport.h)
// Game and pack data are read only; outputs go wherever the arguments point (keep them out of git).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "b3asset/export.h"
#include "b3asset/hdpack.h"
#include "b3asset/png.h"
#include "b3asset/raster.h"
#include "b3asset/xboximport.h"

using namespace b3asset;
namespace fs = std::filesystem;

namespace {

std::vector<std::size_t> rasters_in(const std::vector<std::uint8_t>& d) { return find_rasters(Bytes{d.data(), d.size()}); }

int cmd_xxh(int argc, char** argv) {
    std::vector<std::uint8_t> d;
    if (!read_file(argv[2], d)) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    if (argc == 3) { std::printf("%zu %016llx\n", d.size(), (unsigned long long)xxh3_64(d.data(), d.size())); return 0; }
    for (int i = 3; i < argc; ++i) {
        const std::size_t n = std::min<std::size_t>(std::strtoull(argv[i], nullptr, 0), d.size());
        std::printf("%zu %016llx\n", n, (unsigned long long)xxh3_64(d.data(), n));
    }
    return 0;
}

int cmd_key(int argc, char** argv) {
    std::vector<std::uint8_t> d;
    if (!read_file(argv[2], d)) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    const std::size_t o = std::strtoull(argv[3], nullptr, 16);
    const std::uint32_t add = argc > 4 ? std::uint32_t(std::strtoul(argv[4], nullptr, 0)) : 0;
    GsMemory gs;
    DecodedRaster r;
    std::string why;
    if (!decode_raster_clut(Bytes{d.data(), d.size()}, o, gs, r, add, &why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
    const auto& h = r.hdr;
    std::printf("%s %ux%u %s cpsm %d csa %d csm %d tbp %llu tbw %llu levels %u tcc %d\n", h.name.c_str(), h.width, h.height,
                psm_name(h.psm()), h.cpsm(), int(h.tex0 >> 56) & 31, int(h.tex0 >> 55) & 1, (unsigned long long)(h.tex0 & 0x3FFF),
                (unsigned long long)((h.tex0 >> 14) & 0x3F), h.levels, int(h.tcc()));
    if (const char* out = std::getenv("B3HDKEYS_PNG"))   // the decoded level 0 as PNG (debugging)
        for (std::size_t l = 0; l < r.levels.size(); ++l)
            write_png_rgba(std::string(out) + std::to_string(l) + ".png", r.levels[l].w, r.levels[l].h, r.levels[l].rgba.data());
    for (int l = 1; l <= int(h.levels); ++l) {
        HdKey k;
        if (hd_key(gs, h, add, l, k)) std::printf("  levels %d: %s\n", l, hd_key_name(k).c_str());
        else std::printf("  levels %d: no key (format not handled)\n", l);
    }
    return 0;
}

// Mean colour / alpha (0..1) of an RGBA8 image.
void means(const Image& im, double m[4]) {
    m[0] = m[1] = m[2] = m[3] = 0;
    const std::size_t n = std::size_t(im.w) * im.h;
    for (std::size_t i = 0; i < n; ++i)
        for (int k = 0; k < 4; ++k) m[k] += im.rgba[i * 4 + k];
    for (int k = 0; k < 4; ++k) m[k] /= n ? 255.0 * double(n) : 1.0;
}

// Original decode vs the pack's replacement for every hit: mean alpha / colour, listed when they differ by `tol`.
int cmd_compare(int argc, char** argv) {
    HdPack pack;
    if (!pack.open(argv[2])) return 1;
    const double tol = 0.08;
    GsMemory gs;
    std::size_t hits = 0, listed = 0;
    std::set<std::string> found;   // xboximport: the pack files some decode finds (the import's check: all of them)
    for (int i = 3; i < argc; ++i) {
        std::vector<std::uint8_t> d;
        if (!read_file(argv[i], d)) continue;
        hd_each_raster(Bytes{d.data(), d.size()}, gs, [&](const HdRasterVariant& rv) {   // xboximport: the import's enumeration
            if (!rv.decoded) return true;
            const HdCandidate* hit = nullptr;
            const std::string* p = nullptr;
            for (const auto& k : rv.keys)
                if ((p = pack.find(k.key))) { hit = &k; break; }
            if (!p) return true;
            ++hits;
            found.insert(*p);
            const DecodedRaster& r = rv.raster;
            Image im;
            if (!load_dds(*p, im)) { std::printf("  %s: DDS unreadable\n", p->c_str()); return true; }
            double a[4], b[4];
            means(r.levels[0], a);
            means(im, b);
            const double da = b[3] - a[3], dc = (std::fabs(b[0] - a[0]) + std::fabs(b[1] - a[1]) + std::fabs(b[2] - a[2])) / 3;
            if (std::fabs(da) < tol && dc < tol) return true;
            ++listed;
            std::printf("%-26s %4ux%-4u %-4s pal %d tcc %d tfx %d  alpha %.3f -> %.3f  rgb %.2f %.2f %.2f -> %.2f %.2f %.2f  %s (%d:%d+%d)\n",
                        r.hdr.name.c_str(), r.hdr.width, r.hdr.height, psm_name(r.hdr.psm()), rv.variant, int(r.hdr.tcc()), r.hdr.tfx(), a[3], b[3],
                        a[0], a[1], a[2], b[0], b[1], b[2], fs::path(*p).filename().string().c_str(), hit->level, hit->first, hit->count);
            return true;
        });
    }
    std::printf("%zu hits, %zu differ by %.2f or more in mean alpha or colour\n", hits, listed, tol);
    std::size_t unfound = 0;
    for (const auto& [k, path] : pack.files)
        if (!found.count(path) && unfound++ < 20) std::printf("  not found by any decode: %s\n", path.c_str());
    std::printf("%zu of %zu pack files found by a decode\n", found.size(), pack.files.size());
    return 0;
}

int cmd_scan(int argc, char** argv) {
    HdPack pack;
    std::string why;
    if (!pack.open(argv[2], &why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
    std::printf("pack: %zu replacement files indexed, %zu other files\n", pack.files.size(), pack.skipped);
    std::vector<std::string> inputs;
    std::string matched_out, keys_out;
    bool list = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--matched" && i + 1 < argc) { matched_out = argv[++i]; continue; }
        if (a == "--keys" && i + 1 < argc) { keys_out = argv[++i]; continue; }
        if (a == "--list") { list = true; continue; }
        if (fs::is_directory(a)) {
            for (const auto& de : fs::recursive_directory_iterator(a))
                if (de.is_regular_file()) inputs.push_back(de.path().string());
        } else {
            inputs.push_back(a);
        }
    }
    std::sort(inputs.begin(), inputs.end());
    std::set<std::string> hits;
    FILE* keys = keys_out.empty() ? nullptr : std::fopen(keys_out.c_str(), "w");
    std::map<std::string, std::uint64_t> tot;
    GsMemory gs;
    for (const auto& path : inputs) {
        std::vector<std::uint8_t> d;
        if (!read_file(path, d)) continue;
        if (rasters_in(d).empty()) continue;
        Bytes f{d.data(), d.size()};
        std::uint64_t n = 0, nok = 0, base = 0, mip = 0, anyv = 0, nokey = 0;
        bool any = false, any_base = false, any_mip = false, decoded = false;
        bool open_raster = false;
        const auto close_raster = [&]() {   // the raster's counts, once its variants are all seen
            nok += decoded;
            base += any_base;
            mip += any_mip;
            anyv += any;
            any = any_base = any_mip = decoded = false;
        };
        n = hd_each_raster(f, gs, [&](const HdRasterVariant& rv) {   // xboximport: the import's enumeration
            if (rv.variant == 0 && open_raster) close_raster();
            open_raster = true;
            if (!rv.decoded) return true;
            decoded = true;
            const DecodedRaster& r = rv.raster;
            const auto& cands = rv.keys;
            if (cands.empty()) { ++nokey; return true; }
            const HdCandidate* first_hit = nullptr;
            for (const auto& c : cands) {
                const std::string* p = pack.find(c.key);
                if (p) { hits.insert(*p); if (!first_hit) first_hit = &c; }
                if (keys)
                    std::fprintf(keys, "%s\t%zx\t%s\t%d\t%d:%d+%d\t%s\t%d\n", path.c_str(), rv.offset, r.hdr.name.c_str(), rv.variant, c.level, c.first,
                                 c.count, hd_key_name(c.key).c_str(), p != nullptr);
            }
            const bool b0 = first_hit && first_hit->level == 0 && first_hit->first == 0 && first_hit->count == 1;
            any_base = any_base || b0;
            any_mip = any_mip || (first_hit && !b0);
            any = any || first_hit;
            if (list)
                std::printf("    %-28s %4ux%-4u %-4s pal %d/%d lv %u csa %d %s %s\n", r.hdr.name.c_str(), r.hdr.width, r.hdr.height,
                            psm_name(r.hdr.psm()), rv.variant, rv.variants, r.hdr.levels, int(r.hdr.tex0 >> 56) & 31, hd_key_name(cands[0].key).c_str(),
                            !first_hit ? "-" : b0 ? "HIT" : ("HIT(" + std::to_string(first_hit->level) + ":" + std::to_string(first_hit->first) + "+" +
                                                            std::to_string(first_hit->count) + ")").c_str());
            return true;
        });
        if (open_raster) close_raster();
        std::printf("%-70s rasters %4llu decoded %4llu  hit %4llu (base key %llu, mip-path key only %llu)%s\n", path.c_str(),
                    (unsigned long long)n, (unsigned long long)nok, (unsigned long long)anyv, (unsigned long long)base,
                    (unsigned long long)mip, nokey ? (" nokey " + std::to_string(nokey)).c_str() : "");
        tot["rasters"] += n;
        tot["hit"] += anyv;
    }
    std::printf("total: %llu rasters, %llu with a pack file; %zu distinct pack files hit of %zu\n", (unsigned long long)tot["rasters"],
                (unsigned long long)tot["hit"], hits.size(), pack.files.size());
    if (keys) std::fclose(keys);
    if (!matched_out.empty()) {
        if (FILE* fp = std::fopen(matched_out.c_str(), "w")) {
            for (const auto& h : hits) std::fprintf(fp, "%s\n", h.c_str());
            std::fclose(fp);
        }
    }
    return 0;
}

// xboximport: the launcher's Import Xbox textures without the launcher (local checks; the output is the player's own)
int cmd_import(int argc, char** argv) {
    b3asset::xboximport::Options o;
    for (int i = 5; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-cars") o.cars = false;
        else if (a == "--threads" && i + 1 < argc) o.threads = std::atoi(argv[++i]);
    }
    int last = -1;
    const auto r = b3asset::xboximport::run(argv[2], argv[3], argv[4], [&](const b3asset::xboximport::Progress& p) {
        const int pct = p.total ? p.done * 100 / p.total : 0;
        if (pct / 10 == last) return;
        last = pct / 10;
        std::fprintf(stderr, "  %s %d / %d (%s), %zu written\n", p.stage ? "building" : "reading", p.done, p.total, p.group.c_str(), p.written);
    }, nullptr, o);
    if (!r.ok) { std::fprintf(stderr, "import failed: %s\n", r.error.c_str()); return 1; }
    std::printf("%zu files read, %zu keys, %zu written (%zu car paints; %zu with the PS2 alpha alone), %.1f MB, %.1f s\n", r.files, r.keys,
                r.written, r.cars, r.alpha_ps2, double(r.bytes) / 1048576.0, r.seconds);
    for (int i = 0; i < b3asset::xboximport::kReasons; ++i)
        if (r.skipped[std::size_t(i)]) std::printf("  skipped, %s: %zu\n", b3asset::xboximport::reason_name(i), r.skipped[std::size_t(i)]);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "xxh" && argc >= 3) return cmd_xxh(argc, argv);
    if (cmd == "key" && argc >= 4) return cmd_key(argc, argv);
    if (cmd == "scan" && argc >= 4) return cmd_scan(argc, argv);
    if (cmd == "compare" && argc >= 4) return cmd_compare(argc, argv);
    if (cmd == "import" && argc >= 5) return cmd_import(argc, argv);
    std::fprintf(stderr, "usage: b3hdkeys xxh <file> [len...] | key <file> <offset hex> [cbp_add] | scan <pack dir> <file|dir>... [--matched out] [--list] | compare <pack dir> <file>... | import <xbox image> <game dir> <out dir> [--no-cars] [--threads n]\n");
    return 2;
}
