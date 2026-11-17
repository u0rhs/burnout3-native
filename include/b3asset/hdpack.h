// HD texture packs made for PCSX2's texture replacement (docs/native-renderer.md §18 "HD texture packs").
//
// PCSX2 names a replacement after a hash of the texture as the GS sees it [C PCSX2 source, read 2026-11-03:
// GSTextureReplacements.cpp TextureName / ParseReplacementName, GSTextureCache.cpp HashCacheKey::Create /
// HashTextureLevel, GSClut.cpp Read32]:
//   "<TEX0 hash>-<CLUT hash>-<bits>.dds" (hex, no zero padding; the CLUT part only for paletted formats), where
//   TEX0 hash = XXH3-64 (seed 0) over the texture's 256-byte GS memory blocks in block-row order, or, for textures
//               smaller than one block and for formats that use part of each 32-bit word (T8H, T4HL/HH, CT24, CT16),
//               over the texels expanded row by row (paletted: one index byte per texel; else RGBA8),
//   CLUT hash = XXH3-64 over the palette as 16 / 256 u32 colours in index order after CSA (raw GS alpha, 0x80 = 1),
//   bits      = PSM | TW << 6 | TH << 10 | (bit 14: an old TCC copy, ignored on load) | TEXA (TA0 << 15, AEM << 23,
//               TA1 << 24; only for CT24/CT16, else 0).
// The block hash does not depend on TBP0 (a texture moved in GS memory moves block by block), so the raster's own
// upload, replayed into GsMemory at its stored relative TBP, gives the same key as the game's texture cache slot.
// Replacement alpha uses PS2 units too (PCSX2's loaders write 0x80 for opaque), mapped here like GS alpha.
// Pack files are untrusted input: names are parsed strictly and DDS headers are bounds-checked.
#ifndef B3ASSET_HDPACK_H
#define B3ASSET_HDPACK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "b3asset/raster.h"

namespace b3asset {

// XXH3-64 with seed 0 and the default secret (xxHash spec, doc/xxhash_spec.md; own implementation).
std::uint64_t xxh3_64(const void* data, std::size_t len);

struct HdKey {
    std::uint64_t tex = 0, clut = 0;
    std::uint32_t bits = 0;   // bit 14 always clear
    bool operator==(const HdKey& o) const { return tex == o.tex && clut == o.clut && bits == o.bits; }
};
struct HdKeyHash {
    std::size_t operator()(const HdKey& k) const { return std::size_t(k.tex ^ (k.clut * 0x9E3779B97F4A7C15ull) ^ k.bits); }
};

// PCSX2's file name for a key (without ".dds"; bit 14 clear).
std::string hd_key_name(const HdKey& k);
// Parses a replacement file name ("<tex>-<clut>-<bits>.<ext>" or "<tex>-<bits>.<ext>"). Region ("-r<w>x<h>-") and
// mip-level names are not supported (false).
bool parse_hd_name(const std::string& filename, HdKey& out);

// Raw-block texture hash of one level (TEX0 fields PSM/TW/TH/TBP0/TBW) of what is in `gs` (PCSX2 HashTextureLevel
// without a region), fed into `bytes` (the hash input; XXH3 of the concatenation = PCSX2's streamed hash).
bool hd_texture_bytes(const GsMemory& gs, int psm, int tw_log2, int th_log2, std::uint32_t tbp, std::uint32_t tbw,
                      std::vector<std::uint8_t>& bytes);

// Key of the raster just replayed into `gs` by decode_raster / decode_raster_clut (same `cbp_add`).
// `levels` = 1: base level only (PCSX2 without hardware mipmapping); n > 1: base + the next n-1 stored mip levels
// hashed together (PCSX2 with full hardware mipmapping hashes the levels the draw's LOD range covers).
// `first` > 0: the key of stored mip level `first` (and on) as its own texture: PCSX2 without full hardware mipmapping
// samples one level per draw through that level's TEX0 (TBP/TBW from MIPTBP, TW/TH reduced), and dumps it so.
bool hd_key(const GsMemory& gs, const RasterHeader& h, std::uint32_t cbp_add, int levels, HdKey& out, int first = 0);

// Every key a pack may use for this raster, most detailed first: hash over levels first..first+count-1 with the TEX0
// size bits of level j <= first (`level` = j: the replacement then stands for level j's size). PCSX2's mipmapped draws
// name a texture with one level's TEX0 and hash a later range [C pack data: e.g. GL_rockblend1 (US/C3_V1) levels
// 2..4 hashed, named 64x64 and 128x128; hit counts in §18].
struct HdCandidate { HdKey key; int level = 0, first = 0, count = 1; };
std::vector<HdCandidate> hd_candidate_keys(const GsMemory& gs, const RasterHeader& h, std::uint32_t cbp_add);
// The first candidate the pack holds (nullptr: none); `which` receives it.
const std::string* hd_lookup(const struct HdPack& pack, const GsMemory& gs, const RasterHeader& h, std::uint32_t cbp_add,
                             HdCandidate* which = nullptr);

// A pack folder indexed by file name (recursive, as PCSX2 scans it); nothing is read until load_dds.
struct HdPack {
    std::string dir;
    std::unordered_map<HdKey, std::string, HdKeyHash> files;   // key -> path
    std::size_t skipped = 0;                                   // files whose names are not replacement names
    bool open(const std::string& folder, std::string* why = nullptr);
    // patches3: several folders as one pack, in priority order: a name the first folder has wins over the same name in a
    // later one (the mods' [textures] packs in load order, then the player's own pack). `dir` is the first folder.
    bool open(const std::vector<std::string>& folders, std::string* why = nullptr);
    bool add(const std::string& folder, std::string* why = nullptr);   // a folder below the ones already open
    std::vector<std::string> dirs;                                     // the folders open, in priority order
    const std::string* find(const HdKey& k) const;
};

// The replacement hook: while set (b3replay / b3view --hd-pack), decode_raster / decode_raster_clut look the raster
// up after decoding and, on a hit, replace `levels` with the pack's image cropped to the raster's own extent (a
// replacement covers TEX0's 2^TW x 2^TH; the decoded image covers width x height) as one level (the GPU builds the
// chain). The original decode's translucent / max_alpha stay. TCC 0 forces alpha 1 as in the decode; otherwise the
// replacement's alpha is held to the original's: 0 where the original's bilinear alpha is below 1/4, never above its
// maximum.
class HdTextures {
public:
    bool open(const std::string& folder, std::string* why = nullptr);
    bool open(const std::vector<std::string>& folders, std::string* why = nullptr);   // HdPack::open(folders)
    bool apply(const GsMemory& gs, DecodedRaster& r, std::uint32_t cbp_add);
    // Reads ahead: every raster in the files (all palette variants) is looked up and the hits' DDS files are decoded on
    // `threads` threads into the cache, so the decodes that follow find them there (reads over WSL's /mnt/c are ~7x
    // faster in parallel). Returns the number of files loaded.
    std::size_t prefetch(const std::vector<std::string>& files, int threads = 8);
    // Drops the decoded files (each decode keeps its own copy); later hits load again.
    void trim() { cache_.clear(); }
    HdPack pack;
    std::size_t lookups = 0, hits = 0, failures = 0, files_loaded = 0, masked = 0;
    bool raw_alpha = false;   // $B3_HD_RAW_ALPHA: the pack's alpha as is (as PCSX2 uses it)
    double load_ms = 0, key_ms = 0;
private:
    std::unordered_map<std::string, std::shared_ptr<const Image>> cache_;   // decoded DDS by path (nullptr: failed)
};
void set_hd_textures(HdTextures* t);   // nullptr = off (the default)
HdTextures* hd_textures();
// The viewers' switch: `dir` (--hd-pack) or else $B3_HD_PACK; `off` (--no-hd) wins. Opens the pack into a process-wide
// instance, sets the hook and reports on stderr. Returns the instance (nullptr: off or the folder failed).
// patches3: `before` are the active mods' [textures] packs (b3mod::Overlay::texture_packs), in load order: they come
// first and win over the player's pack for a texture both replace; `off` turns off the player's pack only (a mod's
// textures are part of the mod: they follow the mod's on / off switch).
HdTextures* hd_enable(const std::string& dir, bool off, const std::vector<std::string>& before = {});
// The folders hd_enable opens, in priority order: `before` (the mods' packs), then `dir` (else $B3_HD_PACK) unless `off`.
// showhd1: the launcher's showroom opens the same list with its own HdTextures (b3launcher/showroom_view.h).
std::vector<std::string> hd_folders(const std::string& dir, bool off, const std::vector<std::string>& before = {});
void hd_report();   // one stderr line: lookups, hits, files decoded, time (nothing when off)
void hd_prefetch(const std::vector<std::string>& files);   // HdTextures::prefetch when on
void hd_trim();                                            // HdTextures::trim when on

// Raster offsets in a file: a texture dictionary's directory, else a validated header scan (b3assetdump census).
std::vector<std::size_t> find_rasters(Bytes file);

// DDS reader: BC1 / BC2 / BC3 (DXT1/3/5, also via a DX10 header) and uncompressed 32-bit RGBA / BGRA, decoded to RGBA8
// with PS2 alpha mapped like GS alpha (0x80 and above -> 255, a -> 2a). Only the top level is read.
bool load_dds(const std::string& path, Image& out, std::string* why = nullptr);
bool decode_dds(const std::uint8_t* data, std::size_t size, Image& out, std::string* why = nullptr);

}  // namespace b3asset

#endif
