// The Xbox version's textures (include/b3asset/xboxtex.h).
#include "b3asset/xboxtex.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "b3asset/bc.h"

namespace b3asset::xbox {

namespace {
std::uint32_t le32(const std::uint8_t* p) { return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }
bool fits(std::size_t size, std::size_t off, std::size_t n) { return off <= size && n <= size - off; }
constexpr std::size_t kHeader = 0x80;     // header and tail up to the pixels
constexpr int kMaxLog2 = 11;              // 2048 texels a side (the USA disc's largest: 1024, World_Map)
constexpr int kMaxPalettes = 8;           // +0x14 .. +0x30

std::size_t data_bytes(const Texture& t) {
    const std::size_t w = std::size_t(t.width), h = std::size_t(t.height);
    switch (t.format) {
    case kDXT1: return ((w + 3) / 4) * ((h + 3) / 4) * 8;
    case kDXT3:
    case kDXT5: return ((w + 3) / 4) * ((h + 3) / 4) * 16;
    case kP8: return w * h;
    default: return 0;
    }
}
}  // namespace

const char* format_name(int format) {
    switch (format) {
    case kP8: return "P8";
    case kDXT1: return "DXT1";
    case kDXT3: return "DXT3";
    case kDXT5: return "DXT5";
    default: {
        static thread_local char buf[8];
        std::snprintf(buf, sizeof buf, "0x%02x", unsigned(format) & 0xFF);
        return buf;
    }
    }
}

std::size_t swizzle(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    std::size_t i = 0;
    int bit = 0;
    std::uint32_t mx = w - 1, my = h - 1;
    while (mx || my) {
        if (mx) {
            if (x & 1) i |= std::size_t(1) << bit;
            ++bit;
            x >>= 1;
            mx >>= 1;
        }
        if (my) {
            if (y & 1) i |= std::size_t(1) << bit;
            ++bit;
            y >>= 1;
            my >>= 1;
        }
    }
    return i;
}

void unswizzle(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t w, std::uint32_t h, std::size_t bpp) {
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            std::memcpy(dst + (std::size_t(y) * w + x) * bpp, src + swizzle(x, y, w, h) * bpp, bpp);
}

bool parse(const std::uint8_t* file, std::size_t size, std::size_t o, Texture& out) {
    if (!fits(size, o, kHeader)) return false;
    const std::uint8_t* e = file + o;
    const std::uint32_t common = le32(e), data = le32(e + 4), fmt = le32(e + 12), lin = le32(e + 16);
    if ((common & 0x00070000u) != 0x00040000u || data == 0 || (fmt & 0xF0u) != 0x20u) return false;
    Texture t;
    t.offset = o;
    t.format = int(fmt >> 8 & 0xFF);
    t.levels = int(fmt >> 16 & 0xF);
    const int lw = int(fmt >> 20 & 0xF), lh = int(fmt >> 24 & 0xF);
    if (lin) {   // a linear format (not decoded here)
        t.width = int(lin & 0xFFF) + 1;
        t.height = int(lin >> 12 & 0xFFF) + 1;
    } else {
        if (lw > kMaxLog2 || lh > kMaxLog2) return false;
        t.width = 1 << lw;
        t.height = 1 << lh;
    }
    if (!fits(size, o, data)) return false;
    t.data = o + data;
    for (int i = 0; i < 32 && e[0x48 + i]; ++i) {
        const char c = char(e[0x48 + i]);
        if (c < 0x20 || c > 0x7E) return false;
        t.name += c;
    }
    if (t.name.empty()) return false;
    if (t.decodable()) {
        if (lin || !fits(size, t.data, data_bytes(t))) return false;
        if (t.format == kP8) {
            const int count = std::clamp(int(e[0x69]), 1, kMaxPalettes);
            for (int i = 0; i < count; ++i) {
                const std::uint32_t po = le32(e + 0x14 + 4 * i);
                if (po == 0 || !fits(size, o, po) || !fits(size, o + po, 1024)) break;
                t.palettes.push_back(o + po);
            }
            if (t.palettes.empty()) return false;
        }
    }
    out = std::move(t);
    return true;
}

std::vector<Texture> scan(const std::uint8_t* file, std::size_t size) {
    std::vector<Texture> out;
    static const std::uint8_t kTxd[8] = {0x00, 0x00, 0x3C, 0x54, 0x81, 0xED, 0xDE, 0xBC};
    if (size >= 16 && std::memcmp(file, kTxd, 8) == 0) {
        const std::uint32_t n = le32(file + 8);
        for (std::uint32_t i = 0; i < n && fits(size, 16 + std::size_t(i) * 16, 16); ++i) {
            Texture t;
            if (parse(file, size, le32(file + 16 + std::size_t(i) * 16 + 8), t)) out.push_back(std::move(t));
        }
        return out;
    }
    for (std::size_t o = 0; size >= kHeader && o <= size - kHeader; o += 4) {
        const std::uint8_t* e = file + o;
        if (le32(e) != 0x00040001u || le32(e + 8) != 0 || (le32(e + 12) & 0xF0u) != 0x20u || le32(e + 4) == 0) continue;
        Texture t;
        if (parse(file, size, o, t)) out.push_back(std::move(t));
    }
    return out;
}

bool indices(const std::uint8_t* file, std::size_t size, const Texture& t, std::vector<std::uint8_t>& out) {
    if (t.format != kP8 || !fits(size, t.data, data_bytes(t))) return false;
    out.assign(std::size_t(t.width) * t.height, 0);
    unswizzle(file + t.data, out.data(), std::uint32_t(t.width), std::uint32_t(t.height), 1);
    return true;
}

bool decode(const std::uint8_t* file, std::size_t size, const Texture& t, std::vector<std::uint8_t>& rgba, int palette) {
    if (!t.decodable() || !fits(size, t.data, data_bytes(t))) return false;
    const std::size_t n = std::size_t(t.width) * t.height;
    rgba.assign(n * 4, 0);
    if (t.format == kP8) {
        if (t.palettes.empty()) return false;
        const std::size_t po = t.palettes[std::size_t(palette) < t.palettes.size() ? std::size_t(palette) : 0];
        if (!fits(size, po, 1024)) return false;
        std::vector<std::uint8_t> idx;
        if (!indices(file, size, t, idx)) return false;
        const std::uint8_t* pal = file + po;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t* c = pal + 4 * idx[i];   // B G R A
            rgba[i * 4 + 0] = c[2];
            rgba[i * 4 + 1] = c[1];
            rgba[i * 4 + 2] = c[0];
            rgba[i * 4 + 3] = c[3];
        }
        return true;
    }
    const int fmt = t.format == kDXT1 ? kBC1 : t.format == kDXT3 ? kBC2 : kBC3;
    bc_decode(fmt, file + t.data, t.width, t.height, rgba.data());
    return true;
}

}  // namespace b3asset::xbox
