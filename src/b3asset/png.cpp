#include "b3asset/png.h"
#include "b3port/fileio.h"   // fsrobust: UTF-8 paths

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace b3asset {

namespace {
struct CrcTable {
    std::uint32_t t[256];
    CrcTable() {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
    }
};
std::uint32_t crc32(const std::uint8_t* p, std::size_t n, std::uint32_t c = 0) {
    static const CrcTable tab;   // (built once, thread-safe: the HD pack decodes PNGs on several threads)
    const std::uint32_t* table = tab.t;
    c = ~c;
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
void be32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    o.push_back(v >> 24); o.push_back(v >> 16); o.push_back(v >> 8); o.push_back(v);
}
void chunk(std::vector<std::uint8_t>& o, const char* type, const std::vector<std::uint8_t>& data) {
    be32(o, std::uint32_t(data.size()));
    const std::size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), data.begin(), data.end());
    be32(o, crc32(o.data() + start, o.size() - start));
}
}  // namespace

std::vector<std::uint8_t> encode_png_rgba(int w, int h, const std::uint8_t* rgba) {
    std::vector<std::uint8_t> raw;
    raw.reserve(std::size_t(w * 4 + 1) * h);
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + std::size_t(y) * w * 4, rgba + std::size_t(y + 1) * w * 4);
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    for (std::size_t i = 0; i < raw.size() || i == 0; i += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - i);
        z.push_back(i + n >= raw.size() ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8); z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
        if (raw.empty()) break;
    }
    be32(z, (b << 16) | a);
    std::vector<std::uint8_t> o = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    be32(ihdr, w); be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(o, "IHDR", ihdr);
    chunk(o, "IDAT", z);
    chunk(o, "IEND", {});
    return o;
}

bool write_png_rgba(const std::string& path, int w, int h, const std::uint8_t* rgba) {
    const auto png = encode_png_rgba(w, h, rgba);
    FILE* f = b3io::fopen(b3io::u8path(path), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace b3asset

// ---- reading (texpack: HD pack replacements in PNG, as PCSX2 loads them) ----

// miniz (inflate only), compiled into every program with b3asset through src/b3asset/chd_libchdr.c: no zlib needed
extern "C" int mz_uncompress(unsigned char* dest, unsigned long* dest_len, const unsigned char* source, unsigned long source_len);

namespace b3asset {

namespace {
bool png_fail(std::string* why, std::string what) { if (why) *why = std::move(what); return false; }
std::uint32_t rd_be32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }
int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
}  // namespace

bool decode_png(const std::uint8_t* png, std::size_t size, int& w, int& h, std::vector<std::uint8_t>& rgba, std::string* why, bool* has_alpha) {
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    w = h = 0;
    rgba.clear();
    if (size < 8 || std::memcmp(png, sig, 8) != 0) return png_fail(why, "not a PNG file");
    std::size_t at = 8;
    int depth = 0, ctype = -1, interlace = 0;
    std::vector<std::uint8_t> idat, plte, trns;
    bool end = false, header = false;
    while (!end) {
        if (at + 12 > size) return png_fail(why, "PNG cut off");
        const std::uint32_t len = rd_be32(png + at);
        if (len > size - at - 12) return png_fail(why, "PNG cut off");
        const std::uint8_t* type = png + at + 4;
        const std::uint8_t* d = png + at + 8;
        if (crc32(type, std::size_t(len) + 4) != rd_be32(d + len)) return png_fail(why, "PNG chunk CRC error (the file is damaged)");
        if (!std::memcmp(type, "IHDR", 4)) {
            if (len < 13) return png_fail(why, "bad PNG header");
            w = int(std::min<std::uint32_t>(rd_be32(d), 0x7FFFFFFF));
            h = int(std::min<std::uint32_t>(rd_be32(d + 4), 0x7FFFFFFF));
            depth = d[8];
            ctype = d[9];
            interlace = d[12];
            header = true;
            if (d[10] || d[11] || interlace > 1) return png_fail(why, "unknown PNG compression / filter / interlace method");
        } else if (!std::memcmp(type, "PLTE", 4)) {
            plte.assign(d, d + len);
        } else if (!std::memcmp(type, "tRNS", 4)) {
            trns.assign(d, d + len);
        } else if (!std::memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), d, d + len);
        } else if (!std::memcmp(type, "IEND", 4)) {
            end = true;
        } else if (!(type[0] & 0x20)) {
            return png_fail(why, std::string("PNG has a critical chunk this reader does not know: ") + std::string(reinterpret_cast<const char*>(type), 4));
        }
        at += 12 + std::size_t(len);
    }
    if (!header || w <= 0 || h <= 0 || w > 16384 || h > 16384) { w = h = 0; return png_fail(why, "bad PNG size"); }
    int channels;
    switch (ctype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return png_fail(why, "unknown PNG colour type");
    }
    const bool depth_ok = depth == 8 || depth == 16 || ((ctype == 0 || ctype == 3) && (depth == 1 || depth == 2 || depth == 4));
    if (!depth_ok || (ctype == 3 && depth == 16)) return png_fail(why, "bad PNG bit depth");
    if (ctype == 3 && plte.size() < 3) return png_fail(why, "paletted PNG without a palette");
    if (has_alpha) *has_alpha = ctype == 4 || ctype == 6 || !trns.empty();
    const std::size_t bpp_bits = std::size_t(channels) * depth, bpp = std::max<std::size_t>(1, bpp_bits / 8);
    // the passes: one (the whole image) or Adam7's seven sub-images, each filtered row by row on its own
    struct Pass { int x0, y0, dx, dy; };
    static const Pass kWhole[1] = {{0, 0, 1, 1}};
    static const Pass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    const Pass* passes = interlace ? kAdam7 : kWhole;
    const int npass = interlace ? 7 : 1;
    std::size_t total = 0;
    for (int p = 0; p < npass; ++p) {
        const std::size_t pw = std::size_t((w - passes[p].x0 + passes[p].dx - 1) / passes[p].dx);
        const std::size_t ph = std::size_t((h - passes[p].y0 + passes[p].dy - 1) / passes[p].dy);
        if (pw && ph) total += ((pw * bpp_bits + 7) / 8 + 1) * ph;
    }
    // deflate packs at most ~1032 bytes into one (texpack review): a file whose picture would need more is refused before
    // anything that size is allocated (a tiny file claiming 16384 x 16384 asked for gigabytes)
    if (total / 1032 > idat.size() + 64) return png_fail(why, "PNG image data too short for its size (damaged or not a real picture)");
    std::vector<std::uint8_t> raw(total);
    unsigned long rn = static_cast<unsigned long>(raw.size());
    if (mz_uncompress(raw.data(), &rn, idat.data(), static_cast<unsigned long>(idat.size())) != 0 || rn != raw.size())
        return png_fail(why, "PNG image data does not decompress to its size (damaged file)");
    rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    std::size_t off = 0;
    for (int p = 0; p < npass; ++p) {
        const Pass& ps = passes[p];
        const int pw = (w - ps.x0 + ps.dx - 1) / ps.dx, ph = (h - ps.y0 + ps.dy - 1) / ps.dy;
        if (pw <= 0 || ph <= 0) continue;
        const std::size_t stride = (std::size_t(pw) * bpp_bits + 7) / 8;
        std::vector<std::uint8_t> prev(stride, 0), cur(stride);
        for (int y = 0; y < ph; ++y, off += stride + 1) {
            const std::uint8_t* row = &raw[off];
            const int f = row[0];
            if (f > 4) { rgba.clear(); return png_fail(why, "bad PNG filter type"); }
            for (std::size_t i = 0; i < stride; ++i) {
                const int a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
                int pr = 0;
                switch (f) { case 1: pr = a; break; case 2: pr = b; break; case 3: pr = (a + b) / 2; break; case 4: pr = paeth(a, b, c); break; }
                cur[i] = std::uint8_t(row[1 + i] + pr);
            }
            auto bits = [&](std::size_t idx) -> unsigned {   // sample idx as stored (depth 1 / 2 / 4 / 8 / 16)
                if (depth == 8) return cur[idx];
                if (depth == 16) return unsigned(cur[2 * idx] << 8 | cur[2 * idx + 1]);
                const std::size_t bit = idx * std::size_t(depth);
                return (cur[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
            };
            auto s8 = [&](std::size_t idx) -> std::uint8_t {   // to 8 bits
                const unsigned v = bits(idx);
                if (depth == 8) return std::uint8_t(v);
                if (depth == 16) return std::uint8_t(v >> 8);
                return std::uint8_t(v * 255 / ((1u << depth) - 1));
            };
            const std::size_t oy = std::size_t(ps.y0) + std::size_t(y) * std::size_t(ps.dy);
            for (int x = 0; x < pw; ++x) {
                const std::size_t ox = std::size_t(ps.x0) + std::size_t(x) * std::size_t(ps.dx);
                std::uint8_t* o = &rgba[(oy * std::size_t(w) + ox) * 4];
                const std::size_t xi = std::size_t(x);
                switch (ctype) {
                case 0:
                    o[0] = o[1] = o[2] = s8(xi);
                    o[3] = trns.size() >= 2 && bits(xi) == unsigned(trns[0] << 8 | trns[1]) ? 0 : 255;
                    break;
                case 2:
                    o[0] = s8(3 * xi); o[1] = s8(3 * xi + 1); o[2] = s8(3 * xi + 2);
                    o[3] = trns.size() >= 6 && bits(3 * xi) == unsigned(trns[0] << 8 | trns[1]) &&
                                   bits(3 * xi + 1) == unsigned(trns[2] << 8 | trns[3]) && bits(3 * xi + 2) == unsigned(trns[4] << 8 | trns[5])
                               ? 0 : 255;
                    break;
                case 3: {
                    const unsigned i = bits(xi);
                    if (3 * std::size_t(i) + 2 >= plte.size()) { rgba.clear(); return png_fail(why, "PNG palette index outside the palette"); }
                    o[0] = plte[3 * i]; o[1] = plte[3 * i + 1]; o[2] = plte[3 * i + 2];
                    o[3] = i < trns.size() ? trns[i] : 255;
                    break;
                }
                case 4:
                    o[0] = o[1] = o[2] = s8(2 * xi);
                    o[3] = s8(2 * xi + 1);
                    break;
                default:
                    for (int k = 0; k < 4; ++k) o[k] = s8(4 * xi + std::size_t(k));
                    break;
                }
            }
            std::swap(prev, cur);
        }
    }
    return true;
}

}  // namespace b3asset
