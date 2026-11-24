// Minimal PNG writer (RGBA8, uncompressed deflate blocks) for offline dumps, and a reader (texpack: HD pack replacements
// in PNG, as PCSX2 loads them; b3modtool's texture tools); no dependencies beyond the miniz b3asset already carries.
#ifndef B3ASSET_PNG_H
#define B3ASSET_PNG_H

#include <cstdint>
#include <string>
#include <vector>

namespace b3asset {

std::vector<std::uint8_t> encode_png_rgba(int w, int h, const std::uint8_t* rgba);
bool write_png_rgba(const std::string& path, int w, int h, const std::uint8_t* rgba);
// Any PNG (every colour type and bit depth, palette and tRNS, Adam7 interlacing; 16-bit samples to their high byte) to
// RGBA8 rows, top first. Untrusted input: chunk CRCs checked, sizes bounded (16384), the data must decompress to its size.
// `has_alpha` (optional): the file carries alpha (an alpha channel, or tRNS); without, every texel reads opaque.
bool decode_png(const std::uint8_t* png, std::size_t size, int& w, int& h, std::vector<std::uint8_t>& rgba, std::string* why = nullptr,
                bool* has_alpha = nullptr);

}  // namespace b3asset

#endif
