#include "b3asset/png.h"

#include <algorithm>
#include <cstdio>

namespace b3asset {

namespace {
std::uint32_t crc32(const std::uint8_t* p, std::size_t n, std::uint32_t c = 0) {
    static std::uint32_t table[256];
    if (!table[1])
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
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
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace b3asset
