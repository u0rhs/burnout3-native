// PNG in and out for the texture tools (zlib). The writer: RGBA8, each row with the filter that makes it smallest
// (the usual heuristic). The reader: every non-interlaced PNG, to RGBA8.
#include "b3modtool/modtool.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <zlib.h>

namespace b3modtool {

namespace {

bool fail(std::string* why, std::string what) { if (why) *why = std::move(what); return false; }
void be32(Buf& o, std::uint32_t v) { o.push_back(std::uint8_t(v >> 24)); o.push_back(std::uint8_t(v >> 16)); o.push_back(std::uint8_t(v >> 8)); o.push_back(std::uint8_t(v)); }
std::uint32_t rd32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
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

bool decode_png(const Buf& png, int& w, int& h, std::vector<std::uint8_t>& rgba, std::string* why) {
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (png.size() < 8 || std::memcmp(png.data(), sig, 8) != 0) return fail(why, "not a PNG file");
    std::size_t at = 8;
    int depth = 0, ctype = -1, interlace = 0;
    Buf idat, plte, trns;
    w = h = 0;
    bool end = false;
    while (!end) {
        if (at + 12 > png.size()) return fail(why, "PNG cut off");
        const std::uint32_t len = rd32(&png[at]);
        if (len > png.size() - at - 12) return fail(why, "PNG cut off");
        const std::uint8_t* type = &png[at + 4];
        const std::uint8_t* d = &png[at + 8];
        if (std::uint32_t(crc32(0, type, len + 4)) != rd32(d + len)) return fail(why, "PNG chunk CRC error (the file is damaged)");
        if (!std::memcmp(type, "IHDR", 4)) {
            if (len < 13) return fail(why, "bad PNG header");
            w = int(rd32(d));
            h = int(rd32(d + 4));
            depth = d[8];
            ctype = d[9];
            interlace = d[12];
            if (d[10] || d[11]) return fail(why, "unknown PNG compression / filter method");
        } else if (!std::memcmp(type, "PLTE", 4)) {
            plte.assign(d, d + len);
        } else if (!std::memcmp(type, "tRNS", 4)) {
            trns.assign(d, d + len);
        } else if (!std::memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), d, d + len);
        } else if (!std::memcmp(type, "IEND", 4)) {
            end = true;
        } else if (!(type[0] & 0x20)) {
            return fail(why, std::string("PNG has a critical chunk this reader does not know: ") + std::string(reinterpret_cast<const char*>(type), 4));
        }
        at += 12 + len;
    }
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return fail(why, "bad PNG size");
    if (interlace) return fail(why, "interlaced PNG: save it without interlacing (Adam7 is not read)");
    int channels;
    switch (ctype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return fail(why, "unknown PNG colour type");
    }
    const bool depth_ok = depth == 8 || depth == 16 || ((ctype == 0 || ctype == 3) && (depth == 1 || depth == 2 || depth == 4));
    if (!depth_ok || (ctype == 3 && depth == 16)) return fail(why, "bad PNG bit depth");
    if (ctype == 3 && plte.size() < 3) return fail(why, "paletted PNG without a palette");
    const std::size_t bpp_bits = std::size_t(channels) * depth;
    const std::size_t stride = (std::size_t(w) * bpp_bits + 7) / 8;
    const std::size_t bpp = std::max<std::size_t>(1, bpp_bits / 8);
    Buf raw((stride + 1) * std::size_t(h));
    uLongf rn = uLongf(raw.size());
    if (uncompress(raw.data(), &rn, idat.data(), uLong(idat.size())) != Z_OK || rn != raw.size())
        return fail(why, "PNG image data does not decompress to its size (damaged file)");
    Buf prev(stride, 0), cur(stride);
    rgba.assign(std::size_t(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = &raw[std::size_t(y) * (stride + 1)];
        const int f = row[0];
        if (f > 4) return fail(why, "bad PNG filter type");
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
            int p = 0;
            switch (f) { case 1: p = a; break; case 2: p = b; break; case 3: p = (a + b) / 2; break; case 4: p = paeth(a, b, c); break; }
            cur[i] = std::uint8_t(row[1 + i] + p);
        }
        auto sample = [&](std::size_t idx) -> unsigned {   // channel sample idx of this row, scaled to 8 bits
            if (depth == 8) return cur[idx];
            if (depth == 16) return cur[2 * idx];
            const std::size_t bit = idx * depth;
            const unsigned v = (cur[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
            return ctype == 3 ? v : v * 255 / ((1u << depth) - 1);
        };
        auto raw16 = [&](std::size_t idx) -> unsigned { return depth == 16 ? unsigned(cur[2 * idx] << 8 | cur[2 * idx + 1]) : sample(idx); };
        for (int x = 0; x < w; ++x) {
            std::uint8_t* o = &rgba[(std::size_t(y) * w + x) * 4];
            switch (ctype) {
            case 0: {
                const unsigned g = sample(x);
                o[0] = o[1] = o[2] = std::uint8_t(g);
                o[3] = 255;
                if (trns.size() >= 2) {
                    const unsigned t = unsigned(trns[0] << 8 | trns[1]);
                    const unsigned v = depth == 16 ? raw16(x) : (depth == 8 ? cur[x] : (cur[std::size_t(x) * depth / 8] >> (8 - depth - std::size_t(x) * depth % 8)) & ((1u << depth) - 1));
                    if (v == t) o[3] = 0;
                }
                break;
            }
            case 2:
                o[0] = std::uint8_t(sample(3 * std::size_t(x)));
                o[1] = std::uint8_t(sample(3 * std::size_t(x) + 1));
                o[2] = std::uint8_t(sample(3 * std::size_t(x) + 2));
                o[3] = 255;
                if (trns.size() >= 6 && raw16(3 * std::size_t(x)) == unsigned(trns[0] << 8 | trns[1]) &&
                    raw16(3 * std::size_t(x) + 1) == unsigned(trns[2] << 8 | trns[3]) && raw16(3 * std::size_t(x) + 2) == unsigned(trns[4] << 8 | trns[5]))
                    o[3] = 0;
                break;
            case 3: {
                const unsigned i = sample(x);
                if (3 * i + 2 >= plte.size()) return fail(why, "PNG palette index outside the palette");
                o[0] = plte[3 * i]; o[1] = plte[3 * i + 1]; o[2] = plte[3 * i + 2];
                o[3] = i < trns.size() ? trns[i] : 255;
                break;
            }
            case 4:
                o[0] = o[1] = o[2] = std::uint8_t(sample(2 * std::size_t(x)));
                o[3] = std::uint8_t(sample(2 * std::size_t(x) + 1));
                break;
            default:
                for (int k = 0; k < 4; ++k) o[k] = std::uint8_t(sample(4 * std::size_t(x) + k));
                break;
            }
        }
        std::swap(prev, cur);
    }
    return true;
}

}  // namespace b3modtool
