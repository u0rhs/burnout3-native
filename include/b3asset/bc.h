// hdgpu: BC1 / BC2 / BC3 (DXT1 / DXT3 / DXT5) blocks, decoded and encoded on the CPU (own code, from the format as the
// D3D / Vulkan specifications define it: 4x4 texel blocks, 8 bytes for BC1, 16 for BC2 / BC3 with the alpha half first).
// The HD pack keeps its replacements compressed on the GPU (hdpack.h); these are the CPU steps around that: reading a
// block, building the mip levels the pack does not store (decode, downsample like the GPU's own mip blit, encode again),
// and re-encoding the blocks whose alpha the loader changes.
#ifndef B3ASSET_BC_H
#define B3ASSET_BC_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace b3asset {

struct Image;

// Formats as Image::bc / b3gfx::TextureLevel::bc carry them (0: RGBA8).
enum : int { kBcNone = 0, kBC1 = 1, kBC2 = 2, kBC3 = 3 };

inline std::size_t bc_block_bytes(int fmt) { return fmt == kBC1 ? 8 : 16; }
// Bytes of a w x h level (partial blocks at the edges count whole).
inline std::size_t bc_level_bytes(int fmt, int w, int h) {
    return std::size_t((w + 3) / 4) * std::size_t((h + 3) / 4) * bc_block_bytes(fmt);
}

// One block to 4x4 RGBA, row by row, alpha as stored (no PS2 mapping). BC1's 3-colour mode (c0 <= c1) gives index 3
// as transparent black; BC2 / BC3 colour halves always decode in 4-colour mode (D3D10+ rule).
void bc_decode_block(int fmt, const std::uint8_t* block, std::uint8_t px[16][4]);
// A whole level (`w` x `h` texels, `rgba` w * h * 4 bytes).
void bc_decode(int fmt, const std::uint8_t* blocks, int w, int h, std::uint8_t* rgba);

// Encoders (fast: principal axis fit plus one least-squares pass). `px` row by row.
// BC1 colour block: 4-colour mode, or with `punch` and any texel alpha < 128, 3-colour mode with those texels on index 3.
void bc_encode_color(const std::uint8_t px[16][4], std::uint8_t out[8], bool punch);
// BC3 alpha block (8- or 6-interpolant mode, whichever is closer).
void bc_encode_alpha(const std::uint8_t a[16], std::uint8_t out[8]);
// A whole level; `threads` > 1 splits it by block rows.
void bc_encode(int fmt, const std::uint8_t* rgba, int w, int h, std::vector<std::uint8_t>& out, int threads = 1);

// The next mip level as the GPU's linear blit builds it (SDL_GenerateMipmapsForGPUTexture): max(w/2, 1) x max(h/2, 1),
// each texel the bilinear sample at its centre (an exact 2x2 average for even sizes).
void mip_down(const Image& src, Image& dst);

}  // namespace b3asset

#endif
