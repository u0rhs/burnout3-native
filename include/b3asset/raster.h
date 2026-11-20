// Serialized RenderWare PS2 rasters (RwRaster + sky2 raster extension + the game's CGtTexture tail) as stored in the
// game's files with file offsets in place of pointers: TXD dictionaries, track STATIC.DAT/ENVIRO.DAT, car BGV/BTV.
//
// Layout [C bytes of GLOBAL.TXD entry 1 and CAR1.BGV's texture section; L field meanings from RW 3.5/3.1 sky2 headers]:
//   +0x00 RwRaster: parent, cpPixels (+0x04), palette (+0x08), width/height/depth (+0x0C..), stride, cType (+0x20),
//         cFlags, privateFlags, cFormat (+0x23), originalPixels (+0x24: the upload block below), ...
//   +0x34 _SkyRasterExt: dmaRefCount, dmaClrCount, TEX0 (+0x3C, u64; TBP0/CBP relative to the texture's cache slot),
//         palOffset (+0x44, blocks: the CLUT base), mipmapKL (+0x48), maxMipLevel << 2 (+0x4A), MIPTBP1 (+0x4C),
//         MIPTBP2 (+0x54), sysMemSize, sysMemPalSize, nTexCacheSize (+0x64, words), cachePkts, lockedMipLevel,
//         flags (+0x6A: 1 new-style packets, 2 PSMT8 uploaded as CT32, 4 PSMT4 uploaded as CT16)
//   +0xA0 u8: number of palettes (car paint variants: CB3RaceCar::Prepare clamps the colour index with it [C decomp])
//   +0xA8 char[]: texture name
//   [+0x24] upload block: {u32 transfers, u32 stale pointer, u32 total transfers, 0}, then per transfer 4 qwords:
//         DMAcnt(2) + GIF A+D BITBLTBUF (relative DBP), DMAref(qwc, address relative to the raster) of GIF packets
//         (A+D TRXPOS/TRXREG/TRXDIR, then IMAGE data).
// Decoding replays exactly those transfers into a GS memory model and samples it with TEX0 / MIPTBP / the CLUT
// (gs_memory.h), so the swizzled upload forms (CT32-as-T8, CT16-as-T4, in-buffer palettes) need no special cases.
#ifndef B3ASSET_RASTER_H
#define B3ASSET_RASTER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "b3asset/gs_memory.h"

namespace b3asset {

struct Bytes {
    const std::uint8_t* p = nullptr;
    std::size_t n = 0;
    bool has(std::size_t off, std::size_t len) const { return off <= n && len <= n - off; }
    std::uint32_t u32(std::size_t off) const;
    std::uint64_t u64(std::size_t off) const;
    std::uint16_t u16(std::size_t off) const;
    float f32(std::size_t off) const;
};

struct RasterHeader {
    std::size_t offset = 0;
    std::uint32_t width = 0, height = 0, depth = 0;
    std::uint8_t ctype = 0, cformat = 0, flags = 0, palettes = 0;
    std::uint64_t tex0 = 0, miptbp1 = 0, miptbp2 = 0;
    std::uint32_t pal_offset = 0, levels = 1, upload = 0, transfers = 0;
    std::string name;
    int psm() const { return int(tex0 >> 20) & 0x3F; }
    int cpsm() const { return int(tex0 >> 51) & 0xF; }
    bool tcc() const { return (tex0 >> 34) & 1; }
    int tfx() const { return int(tex0 >> 35) & 3; }
};

// Reads and validates the header at `offset` (cType 4, TEX0 PSM/TW/TH consistent with the RwRaster fields, upload
// block inside the buffer). On failure returns false with a reason.
bool read_raster_header(Bytes file, std::size_t offset, RasterHeader& out, std::string* why = nullptr);

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;  // 8-bit RGBA, PS2 alpha 0x80 mapped to 0xFF (alpha > 0x80 clamps)
    // hdgpu: a compressed level instead (an HD pack replacement kept as BC blocks for the GPU, hdpack.h): `bc` = kBC1 /
    // kBC2 / kBC3 (bc.h), the blocks in `blocks` (shared by the decodes of one replacement; dropped once uploaded:
    // b3view::build_scene), `rgba` empty. 0: RGBA8 in `rgba`.
    int bc = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> blocks;
};

struct DecodedRaster {
    RasterHeader hdr;
    std::vector<Image> levels;  // level 0 first, as stored (the game's own mipmaps)
    std::uint8_t max_alpha = 0;  // raw PS2 alpha maximum over level 0 (0x80 = opaque; above 0x80 = "more than 1")
    bool translucent = false;    // any level-0 texel with raw alpha < 0x80
    bool hd = false;             // levels hold an HD pack replacement (hdpack.h set_hd_textures), not the decode
    std::vector<Image> original; // hdkey: with `hd`, the decode it replaced (a pack that keeps originals: the HD TEXTURES key)
};

// The CLUT offset (GS blocks past palOffset) of palette variant `palette`, by CB3VehicleRenderer::SetTexture 0x21FF60's
// rule [C matched]: the raster's u16 table at +0x90 when palette < its count (+0xA0), else 0 (the base CLUT). The table
// is not 4k: the 8-paint cars' is {0, 4, 16, 20, 8, 12, 24, 28} (GS block order), live2 / docs/native-renderer.md §20.
std::uint32_t palette_cbp_add(Bytes file, std::size_t offset, int palette);

// Replays the raster's upload transfers into `gs` (cleared first) and decodes every stored mip level.
// `palette` selects a CLUT variant for paletted rasters with several (palette_cbp_add).
bool decode_raster(Bytes file, std::size_t offset, GsMemory& gs, DecodedRaster& out, int palette = 0,
                   std::string* why = nullptr);

// The same with the CLUT at palOffset + `cbp_add` blocks (Gt2dRenderer::SetTexture's palettes: CBP + the raster's
// u16 table at +0x90 [C asm 0x219A88]).
bool decode_raster_clut(Bytes file, std::size_t offset, GsMemory& gs, DecodedRaster& out, std::uint32_t cbp_add,
                        std::string* why = nullptr);
// carpaint: decode_raster without the HD pack hook (hdpack.h set_hd_textures): the PS2 picture whatever pack is set.
bool decode_raster_ps2(Bytes file, std::size_t offset, GsMemory& gs, DecodedRaster& out, int palette = 0,
                       std::string* why = nullptr);

// Executes a GIF packet of `qwc` quadwords (PACKED A+D register writes, IMAGE data) against `gs`.
void run_gif(Bytes file, std::size_t at, std::size_t qwc, GsMemory& gs);

// RGBA8 colour from a GS colour value of storage format `psm` (CT32 / CT24 / CT16), alpha mapped 0x80 -> 0xFF.
std::uint32_t gs_color_to_rgba8(int psm, std::uint32_t v, bool tcc);

// Texture dictionary files (DATA/GLOBAL.TXD, FRONTEND.TXD): {u32 0x543C0000, u32 0xBCDEED81, u32 count, u32 dir}
// then per entry {u32 key, 0, u32 raster offset, 0} [C bytes].
struct TxdEntry { std::uint32_t key = 0; std::size_t offset = 0; };
bool read_txd(Bytes file, std::vector<TxdEntry>& out, std::string* why = nullptr);

}  // namespace b3asset

#endif
