// PNG in and out for the texture tools (zlib). The writer: RGBA8, each row with the filter that makes it smallest
// (the usual heuristic). The reader: b3asset::decode_png (every PNG, to RGBA8).
#include "b3modtool/modtool.h"
#include "b3asset/png.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <zlib.h>

namespace b3modtool {

namespace {

void be32(Buf& o, std::uint32_t v) { o.push_back(std::uint8_t(v >> 24)); o.push_back(std::uint8_t(v >> 16)); o.push_back(std::uint8_t(v >> 8)); o.push_back(std::uint8_t(v)); }
void chunk(Buf& o, const char* type, const Buf& data) {
    be32(o, std::uint32_t(data.size()));
    const std::size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), data.begin(), data.end());
    be32(o, std::uint32_t(crc32(0, o.data() + start, uInt(o.size() - start))));
}
int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

}  // namespace

Buf encode_png(int w, int h, const std::uint8_t* rgba) {
    const std::size_t stride = std::size_t(w) * 4;
    Buf raw;
    raw.reserve((stride + 1) * std::size_t(h));
    Buf line(stride), best;
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* cur = rgba + std::size_t(y) * stride;
        const std::uint8_t* up = y ? cur - stride : nullptr;
        long best_sum = -1;
        int best_f = 0;
        for (int f = 0; f < 5; ++f) {
            long sum = 0;
            for (std::size_t i = 0; i < stride; ++i) {
                const int a = i >= 4 ? cur[i - 4] : 0, b = up ? up[i] : 0, c = (up && i >= 4) ? up[i - 4] : 0;
                int p = 0;
                switch (f) { case 1: p = a; break; case 2: p = b; break; case 3: p = (a + b) / 2; break; case 4: p = paeth(a, b, c); break; }
                line[i] = std::uint8_t(cur[i] - p);
                sum += line[i] < 128 ? line[i] : 256 - line[i];
            }
            if (best_sum < 0 || sum < best_sum) { best_sum = sum; best_f = f; best = line; }
        }
        raw.push_back(std::uint8_t(best_f));
        raw.insert(raw.end(), best.begin(), best.end());
    }
    uLongf zn = compressBound(uLong(raw.size()));
    Buf z(zn);
    if (compress2(z.data(), &zn, raw.data(), uLong(raw.size()), 9) != Z_OK) zn = 0;
    z.resize(zn);
    Buf o = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    Buf ihdr;
    be32(ihdr, std::uint32_t(w));
    be32(ihdr, std::uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(o, "IHDR", ihdr);
    chunk(o, "IDAT", z);
    chunk(o, "IEND", {});
    return o;
}

// texpack: the one PNG reader is b3asset's (the HD pack loads PNG replacements with it too); this one added Adam7
bool decode_png(const Buf& png, int& w, int& h, std::vector<std::uint8_t>& rgba, std::string* why) {
    return b3asset::decode_png(png.data(), png.size(), w, h, rgba, why);
}

}  // namespace b3modtool
