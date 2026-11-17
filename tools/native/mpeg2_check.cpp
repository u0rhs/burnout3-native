// mpeg2_check.cpp -- the native MPEG-2 decoder (src/native/mpeg2_decoder.h) against ffmpeg, picture by picture (mac5).
//
//   mpeg2_check FILE.m2v [--chunk N] [--dump N OUT.ppm]
//
// Decodes FILE in N-byte chunks (default 65536, as a stream from disc), reads ffmpeg's decode of the same file through
// a pipe (`ffmpeg -v error -i FILE -f rawvideo -pix_fmt yuv420p -`; nothing is written to disk) and compares every
// picture's Y / Cb / Cr over the display area. Prints one line: frames (ours / ffmpeg), the largest per-sample
// difference, the lowest PSNR (Y) and the worst picture, then "OK" (counts equal, max diff <= 2) or "DIFF". Exit 0 / 1.
#include "mpeg2_decoder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: mpeg2_check FILE.m2v [--chunk N] [--dump N OUT.ppm]\n"); return 2; }
    const std::string path = argv[1];
    std::size_t chunk = 65536;
    long dump_at = -1;
    std::string dump_path;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--chunk" && i + 1 < argc) chunk = std::strtoul(argv[++i], nullptr, 10);
        else if (a == "--dump" && i + 2 < argc) { dump_at = std::atol(argv[++i]); dump_path = argv[++i]; }
    }
    std::FILE* in = std::fopen(path.c_str(), "rb");
    if (!in) { std::fprintf(stderr, "mpeg2_check: cannot open %s\n", path.c_str()); return 2; }
    b3native::Mpeg2Decoder dec;
    std::vector<b3native::Mpeg2Picture> ours;
    std::vector<std::uint8_t> buf(chunk);
    b3native::Mpeg2Picture p;
    for (;;) {
        const std::size_t n = std::fread(buf.data(), 1, buf.size(), in);
        if (n) dec.feed(buf.data(), n);
        while (dec.next_picture(p)) ours.push_back(std::move(p));
        if (n < buf.size()) break;
        if (dec.failed()) break;
    }
    std::fclose(in);
    dec.finish();
    while (dec.next_picture(p)) ours.push_back(std::move(p));
    if (dec.failed()) { std::printf("%s: FAIL decoder: %s (after %zu pictures)\n", path.c_str(), dec.error().c_str(), ours.size()); return 1; }
    if (ours.empty()) { std::printf("%s: FAIL no pictures\n", path.c_str()); return 1; }
    if (dump_at >= 0 && std::size_t(dump_at) < ours.size()) {
        std::vector<std::uint8_t> rgba;
        b3native::Mpeg2Decoder::to_rgba(ours[std::size_t(dump_at)], rgba);
        if (std::FILE* o = std::fopen(dump_path.c_str(), "wb")) {
            const auto& q = ours[std::size_t(dump_at)];
            std::fprintf(o, "P6\n%d %d\n255\n", q.display_w, q.display_h);
            for (std::size_t k = 0; k < rgba.size(); k += 4) std::fwrite(&rgba[k], 1, 3, o);
            std::fclose(o);
        }
    }
    const int W = ours[0].display_w, H = ours[0].display_h;
    const std::string cmd = "ffmpeg -v error -i '" + path + "' -f rawvideo -pix_fmt yuv420p - 2>/dev/null";
    std::FILE* ff = popen(cmd.c_str(), "r");
    if (!ff) { std::printf("%s: FAIL cannot run ffmpeg\n", path.c_str()); return 1; }
    const std::size_t ys = std::size_t(W) * std::size_t(H), cs = std::size_t(W / 2) * std::size_t(H / 2);
    std::vector<std::uint8_t> ref(ys + 2 * cs);
    std::size_t nref = 0;
    int maxd = 0;
    long worst = -1;
    double minpsnr = 1e9;
    long psnr_worst = -1;
    while (std::fread(ref.data(), 1, ref.size(), ff) == ref.size()) {
        if (nref < ours.size()) {
            const auto& q = ours[nref];
            long double se = 0;
            int md = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const int d = int(q.y[std::size_t(y) * std::size_t(q.width) + std::size_t(x)]) - int(ref[std::size_t(y) * std::size_t(W) + std::size_t(x)]);
                    se += d * d;
                    md = std::max(md, std::abs(d));
                }
            for (int c = 0; c < 2; ++c) {
                const auto& pl = c ? q.cr : q.cb;
                const std::uint8_t* r = ref.data() + ys + std::size_t(c) * cs;
                for (int y = 0; y < H / 2; ++y)
                    for (int x = 0; x < W / 2; ++x)
                        md = std::max(md, std::abs(int(pl[std::size_t(y) * std::size_t(q.width / 2) + std::size_t(x)]) - int(r[std::size_t(y) * std::size_t(W / 2) + std::size_t(x)])));
            }
            const double mse = double(se) / double(ys);
            const double psnr = mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 99.0;
            static const bool verbose = std::getenv("MPEG2_VERBOSE") != nullptr;
            static int shown = 0;
            if (verbose && md > 2 && shown < 12) {
                ++shown;
                int bx = -1, by = -1, cnt = 0, worstmb = 0;
                for (int my = 0; my < H / 16; ++my)
                    for (int mx = 0; mx < W / 16; ++mx) {
                        int m = 0;
                        for (int y = 0; y < 16; ++y)
                            for (int x = 0; x < 16; ++x)
                                m = std::max(m, std::abs(int(q.y[std::size_t(my * 16 + y) * std::size_t(q.width) + std::size_t(mx * 16 + x)]) -
                                                         int(ref[std::size_t(my * 16 + y) * std::size_t(W) + std::size_t(mx * 16 + x)])));
                        if (m > 2) ++cnt;
                        if (m > worstmb) { worstmb = m; bx = mx; by = my; }
                    }
                std::printf("  picture %zu type %c tref %d decode %llu: max diff %d, PSNR %.1f; %d bad MBs, worst (%d, %d)\n", nref, " IPB"[q.type],
                            q.temporal_reference, static_cast<unsigned long long>(q.decode_index), md, psnr, cnt, bx, by);
                if (bx >= 0) {   // the first bad macroblock's six blocks: max diff and mean signed diff
                    for (int k = 0; k < 6; ++k) {
                        int bm = 0; long sd = 0;
                        for (int y = 0; y < 8; ++y)
                            for (int x = 0; x < 8; ++x) {
                                int a, r;
                                if (k < 4) {
                                    const int px = bx * 16 + (k & 1) * 8 + x, py = by * 16 + (k >> 1) * 8 + y;
                                    a = q.y[std::size_t(py) * std::size_t(q.width) + std::size_t(px)]; r = ref[std::size_t(py) * std::size_t(W) + std::size_t(px)];
                                } else {
                                    const int px = bx * 8 + x, py = by * 8 + y;
                                    a = (k == 4 ? q.cb : q.cr)[std::size_t(py) * std::size_t(q.width / 2) + std::size_t(px)];
                                    r = ref[ys + std::size_t(k - 4) * cs + std::size_t(py) * std::size_t(W / 2) + std::size_t(px)];
                                }
                                bm = std::max(bm, std::abs(a - r)); sd += a - r;
                            }
                        std::printf("    block %d: max %d, mean %+.1f\n", k, bm, double(sd) / 64.0);
                    }
                }
            }
            if (md > maxd) { maxd = md; worst = long(nref); }
            if (psnr < minpsnr) { minpsnr = psnr; psnr_worst = long(nref); }
        }
        ++nref;
    }
    pclose(ff);
    const bool ok = nref == ours.size() && maxd <= 2;
    std::printf("%s: %s %dx%d frames %zu / ffmpeg %zu, max diff %d (picture %ld), min PSNR %.1f dB (picture %ld)\n", path.c_str(),
                ok ? "OK" : "DIFF", W, H, ours.size(), nref, maxd, worst, minpsnr, psnr_worst);
    return ok ? 0 : 1;
}
