// xboxtex.h -- the Xbox version's textures, found in its files and decoded to RGBA8 (Import Xbox textures,
// b3asset/xboximport.h). The Xbox disc is the player's own; nothing from it is shipped.
//
// The Xbox game keeps its textures in the same containers as the PS2 one (TXDs with the "00 00 3C 54 81 ED DE BC"
// directory: count at +8, then {id, pad, offset, pad} per texture from +16; static.dat, enviro.dat and the car files
// with the textures serialised in place). Each texture is an Xbox D3D texture resource followed by the game's raster
// tail [study: xboxtex-scratch REPORT.md §2]:
//   +0x00 Common   0x00040001 (resource type 4: a texture; the reference count bits)
//   +0x04 Data     the pixels' offset from the entry (0x80)
//   +0x08 Lock     0
//   +0x0C Format   bits 4-7 the dimension (2), 8-15 the format, 16-19 the mip count, 20-23 log2 width, 24-27 log2 height
//   +0x10 Size     0 for swizzled / compressed formats; linear ones: width-1 (0-11), height-1 (12-23), pitch/64-1 (24-31)
//   +0x14..+0x30   palette offsets from the entry (P8: one 256 x BGRA palette each; the cars hold one per paint)
//   +0x38 / +0x3C  the raster's width / height     +0x48 the name (32 bytes, NUL padded)     +0x69 the palette count
// Formats seen: DXT1 (0x0C), DXT3 (0x0E), DXT5 (0x0F), all without swizzle, and P8 (0x0B), swizzled: the texels in
// Morton order, x and y bits interleaved (x first) while both have bits left.
// The file is untrusted input: every offset and size is checked against it, and a texture larger than 2048 texels a
// side is not taken (the USA disc's largest is 1024 x 512).
#ifndef B3ASSET_XBOXTEX_H
#define B3ASSET_XBOXTEX_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace b3asset::xbox {

enum Format : int { kP8 = 0x0B, kDXT1 = 0x0C, kDXT3 = 0x0E, kDXT5 = 0x0F };

struct Texture {
    std::size_t offset = 0;        // the entry in the file
    std::size_t data = 0;          // its pixels (file offset)
    int format = 0;                // the Xbox format code (Format, or another the decoder does not handle)
    int levels = 0, width = 0, height = 0;
    std::vector<std::size_t> palettes;   // P8: each palette's file offset (256 x BGRA, checked inside the file)
    std::string name;
    bool decodable() const { return format == kP8 || format == kDXT1 || format == kDXT3 || format == kDXT5; }
};

const char* format_name(int format);   // "DXT1", "P8", "0x12"

// The texture at `offset` (false: not a texture header there, or one that does not fit in the file)
bool parse(const std::uint8_t* file, std::size_t size, std::size_t offset, Texture& out);
// Every texture in the file: the TXD directory's entries when the file starts with one, else a validated scan for
// texture headers (4-byte aligned: Common 0x00040001, Lock 0, a 2D texture, pixels inside the file, a printable name)
std::vector<Texture> scan(const std::uint8_t* file, std::size_t size);

// Level 0 as RGBA8 (w * h * 4, row by row; alpha as stored, 255 opaque). P8 with palette `palette` (0 when there are
// none of that number). false: a format not handled, or the data is not all inside the file.
bool decode(const std::uint8_t* file, std::size_t size, const Texture& t, std::vector<std::uint8_t>& rgba, int palette = 0);
// P8 only: level 0's palette indices, w * h, row by row (unswizzled)
bool indices(const std::uint8_t* file, std::size_t size, const Texture& t, std::vector<std::uint8_t>& out);

// The swizzled index of texel (x, y) in a w x h power-of-two texture (Morton order, x bit first)
std::size_t swizzle(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h);
// Its inverse for the tests' writers: swizzled `src` (w * h texels of `bpp` bytes) row by row into `dst`
void unswizzle(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t w, std::uint32_t h, std::size_t bpp);

}  // namespace b3asset::xbox

#endif
