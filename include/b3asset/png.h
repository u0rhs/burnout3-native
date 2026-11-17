// Minimal PNG writer (RGBA8, uncompressed deflate blocks) for offline dumps; no dependencies.
#ifndef B3ASSET_PNG_H
#define B3ASSET_PNG_H

#include <cstdint>
#include <string>
#include <vector>

namespace b3asset {

std::vector<std::uint8_t> encode_png_rgba(int w, int h, const std::uint8_t* rgba);
bool write_png_rgba(const std::string& path, int w, int h, const std::uint8_t* rgba);

}  // namespace b3asset

#endif
