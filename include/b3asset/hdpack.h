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
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
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

// A pack folder indexed by file name (recursive, as PCSX2 scans it; texpack: .dds and .png, in path order, the first of a
// name used); nothing is read until load_dds.
struct HdPack {
    std::string dir;
    std::unordered_map<HdKey, std::string, HdKeyHash> files;   // key -> path
    std::size_t skipped = 0;                                   // files whose names are not replacement names
    std::size_t dds_files = 0, png_files = 0;                  // texpack: the replacements by kind (.dds, .png)
    std::size_t repeats_ = 0;                                  // texpack: names found again in the same folder (not used)
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
// hdgpu: with hd_compressed() set (the renderer's device samples BC) and inside an HdCompressScope (track and car
// textures: uploaded through b3view::upload_texture, no texels needed on the CPU), a BC1 / BC2 / BC3 replacement whose
// crop is whole blocks stays compressed: `levels` gets the full chain as BC levels (Image::bc), level 0 the file's blocks
// with the PS2 alpha mapped (exact for BC1 / BC2, BC3 alpha within one level), the mips the file stores, the rest built
// like the GPU's mip blit (decode, 2x2, encode: bc.h). A raster whose original alpha can change the replacement (TCC 1,
// original alpha below 1/4 somewhere or a maximum below 0x80) goes through the same alpha rule on RGBA8; when the rule
// changes texels the result is encoded as BC3, else the file's blocks are used as they are. RGBA32 files, crops that are
// not whole blocks and $B3_HD_RGBA stay on the RGBA8 path.
// texpack: a PNG without alpha (RGB or grey, no tRNS: it carries no alpha at all) takes the original's alpha, bilinear,
// under the same cut and clamp: community packs saved without their alpha (car atlases, whose alpha is the reflection
// mask) keep the game's look. $B3_HD_RAW_ALPHA: opaque, as PCSX2.
class HdTextures {
public:
    bool open(const std::string& folder, std::string* why = nullptr);
    bool open(const std::vector<std::string>& folders, std::string* why = nullptr);   // HdPack::open(folders)
    bool apply(const GsMemory& gs, DecodedRaster& r, std::uint32_t cbp_add);
    // Reads ahead: every raster in the files (all palette variants) is looked up and the hits' DDS files are decoded on
    // `threads` threads into the cache, so the decodes that follow find them there (reads over WSL's /mnt/c are ~7x
    // faster in parallel). Returns the number of files loaded.
    std::size_t prefetch(const std::vector<std::string>& files, int threads = 8);
    // Drops the files read and the chains built (each decode keeps its own copy or reference); later hits load again.
    void trim() {
        const std::lock_guard<Lock> lk(mu_);
        cache_.clear();
        prepared_.clear();
    }
    HdPack pack;
    // hdkey (the HD TEXTURES key): a hit from pack.dirs[originals_from] or a later folder keeps the decode it replaces in
    // DecodedRaster::original (the renderer uploads both and switches between them with no reload). hd_enable sets it
    // to the player's pack (after the mods' packs, which are part of their mods and always shown); none: SIZE_MAX.
    std::size_t originals_from = SIZE_MAX;
    std::size_t lookups = 0, hits = 0, failures = 0, files_loaded = 0, masked = 0, originals = 0;
    std::size_t compressed = 0, encoded = 0;    // hdgpu: hits kept as BC; of them, level 0 re-encoded (the alpha rule)
    std::size_t bc_bytes = 0, bc_built = 0;     // hdgpu: BC bytes handed out (all levels), mip levels built
    bool raw_alpha = false;   // $B3_HD_RAW_ALPHA: the pack's alpha as is (as PCSX2 uses it)
    double load_ms = 0, key_ms = 0, bc_ms = 0, apply_ms = 0;  // bc_ms: chains built in apply (not in prefetch); apply_ms: in apply
    // hdgpu: the pieces apply uses (public for the tests)
    struct Source;                               // a replacement file as read
    struct Job { int width = 0, height = 0, tw = 0, th = 0; bool tcc = true; };   // what a raster asks of it
    // The extent of `job` in a w x h replacement (0: lower resolution than the decode `min_w` x `min_h`)
    static bool crop(const Job& job, int w, int h, int min_w, int min_h, int& cw, int& ch);
    // The BC chain of the file's blocks for `job` with the alpha as the RGBA8 path maps it (no alpha rule); empty: not
    // possible (not BC, or the crop is not whole blocks)
    // `path` (hdmips): named in the log when the file's stored mips are not used
    static std::shared_ptr<const std::vector<Image>> bc_chain(const Source& src, const Job& job, int threads = 1,
                                                              const std::string* path = nullptr);
    // hdmips: files (this process) whose stored mip levels did not match their level 0 and were built from it instead
    static std::size_t mips_rebuilt();
    static std::size_t bc7_mips_unused();   // texpack: BC7 files whose stored mip levels were not used (level 0 only)
    // The alpha rule's result `level0` (RGBA8: the crop decoded, only its alpha changed) as a BC3 chain: the colour halves
    // of `plain` (bc_chain of the same crop: the rule leaves colour alone), the alpha of `level0` and its mips (built like
    // the GPU's blit) encoded
    static std::shared_ptr<const std::vector<Image>> bc_rule_chain(const std::vector<Image>& plain, const Image& level0, int threads = 1);
private:
    bool apply_(const GsMemory& gs, DecodedRaster& r, std::uint32_t cbp_add);
    bool replace_(const std::string& path, const std::shared_ptr<const Source>& file, DecodedRaster& r, bool compress, std::size_t& masked);
    // hdgpu: apply, prefetch's merge and trim hold it: a route decoded on a worker (live.cpp loadlag1) while the render
    // thread decodes its own rasters (HUD, effects) through the same hook
    struct Lock {   // (a fresh one on copy / assignment: HdTextures stays assignable, as the showroom resets its own)
        std::recursive_mutex m;
        Lock() = default;
        Lock(const Lock&) {}
        Lock& operator=(const Lock&) { return *this; }
        void lock() { m.lock(); }
        void unlock() { m.unlock(); }
    } mu_;
    bool keeps_original(const std::string& path) const;   // a hit at `path` is from originals_from or later
    std::shared_ptr<const Source> source(const std::string& path);   // cache_ or read now (nullptr: failed)
    std::unordered_map<std::string, std::shared_ptr<const Source>> cache_;   // files by path (nullptr: failed)
    // hdgpu: BC chains by path and job (and, for the alpha rule, the original's alpha)
    struct Prepared { std::shared_ptr<const std::vector<Image>> chain; bool masked = false; };
    std::unordered_map<std::string, Prepared> prepared_;
    // hdcache (hdcache.h): the disk key of `r`'s replacement by the file at `path` (false: no cache, or the file's identity
    // unknown); a hit installs the entry's levels as replace_ would; a miss's result is queued for the disk
    bool disk_key(const std::string& path, const DecodedRaster& r, bool compress, std::uint64_t& key, std::uint64_t& check);
    bool disk_hit(const std::string& path, std::uint64_t key, std::uint64_t check, DecodedRaster& r);
    void disk_store(std::uint64_t key, std::uint64_t check, const DecodedRaster& r, bool masked);
    // hdcache: guarded by mu_, as cache_ and prepared_ (apply holds it for the whole call, from any thread: the render
    // thread, the route worker, the paint-map worker; prefetch_data takes it around its own uses)
    std::unordered_map<std::string, std::string> ident_;      // a replacement file's identity (size, time, head / tail hash)
    std::unordered_map<std::uint64_t, Prepared> disk_mem_;      // BC chains read from the disk this run (shared)
    std::unordered_map<std::uint64_t, bool> touched_;           // entries read this run (touched once: the LRU's age)
public:
    // hdcache: the prefetch of car files' rasters from their bytes (a car file read from a disc image has no path):
    // `files` (name, bytes); as prefetch otherwise. It takes no new raster once `go` says no or `budget_ms` (> 0) has
    // passed (`stopped` then true): what it did not convert is converted at its first draw, and cached, as before.
    std::size_t prefetch_data(const std::vector<std::pair<std::string, Bytes>>& files, int threads = 8,
                              const std::function<bool()>& go = {}, double budget_ms = 0, bool* stopped = nullptr);
private:
};
// hdgpu: the renderer's device samples BC1 / BC2 / BC3 (b3gfx::Device::supports_bc): set once it exists
void set_hd_compressed(bool on);
bool hd_compressed();
// texpack (HD TEXTURE MEMORY): PNG and BC7 replacements encoded to BC1 (opaque) / BC3 when they are read, on the loader's
// threads, and then used as BC files (with set_hd_compressed: the device samples BC); off: RGBA8 (full quality, as PCSX2)
void set_hd_encode(bool on);
bool hd_encode();
// hdgpu: while one is alive on a thread, the hook may hand that thread's decodes BC levels (their consumer uploads them
// and reads no texels: read_track's and read_car's textures)
struct HdCompressScope {
    HdCompressScope();
    ~HdCompressScope();
    HdCompressScope(const HdCompressScope&) = delete;
    HdCompressScope& operator=(const HdCompressScope&) = delete;
    bool saved;
};
void set_hd_textures(HdTextures* t);   // nullptr = off (the default)
HdTextures* hd_textures();             // (nullptr on a thread inside HdTextures::prefetch's own decodes)
// hdkey: the hook is set and keeps the originals of a player's pack (HdTextures::originals_from): the HD TEXTURES key
// has something to switch
bool hd_switchable();
// The viewers' switch: `dir` (--hd-pack) or else $B3_HD_PACK; `off` (--no-hd) wins. Opens the pack into a process-wide
// instance, sets the hook and reports on stderr. Returns the instance (nullptr: off or the folder failed).
// patches3: `before` are the active mods' [textures] packs (b3mod::Overlay::texture_packs), in load order: they come
// first and win over the player's pack for a texture both replace; `off` turns off the player's pack only (a mod's
// textures are part of the mod: they follow the mod's on / off switch).
// xboximport: `xbox` (the Import Xbox textures folder, when the player has it on) comes last, below the player's own pack:
// anything the player chose wins. It is the player's too: `off` (HD TEXTURES) turns it off and the HD TEXTURES key
// switches it with the player's pack.
HdTextures* hd_enable(const std::string& dir, bool off, const std::vector<std::string>& before = {}, const std::string& xbox = {});
// The folders hd_enable opens, in priority order: `before` (the mods' packs), then `dir` (else $B3_HD_PACK) unless `off`.
// showhd1: the launcher's showroom opens the same list with its own HdTextures (b3launcher/showroom_view.h).
std::vector<std::string> hd_folders(const std::string& dir, bool off, const std::vector<std::string>& before = {}, const std::string& xbox = {});
// xboximport: the run's Import Xbox textures folder when there is one, whether or not it is on (the settings page's
// XBOX TEXTURES row); set by the program at start
void set_hd_xbox_folder(const std::string& dir);
const std::string& hd_xbox_folder();
// vpreload: the decoded pictures held for re-use (their bytes, the budget, the decodes dropped for it so far)
struct HdDecodedStats { std::size_t bytes = 0, budget = 0; std::uint64_t evicted = 0; };
HdDecodedStats hd_decoded_stats();
void hd_report();   // one stderr line: lookups, hits, files decoded, time (nothing when off)
// hdcache (hdcache.h): the converted replacements kept in `dir` (UTF-8; <user data folder>/cache/hd), under a cap of
// `cap_mb` (0: hdcache::kDefaultCapMB; $B3_HD_CACHE_MB wins); "" or $B3_HD_CACHE=0: none (the default)
void set_hd_cache_dir(const std::string& dir, std::uint64_t cap_mb = 0);
std::string hd_cache_dir();
// Waits until the queued writes are on the disk (at most `max_ms`; tests, the end of a run); true when all are
bool hd_cache_flush(int max_ms = 2000);
struct HdCacheStats { std::uint64_t hits = 0, misses = 0, written = 0, written_bytes = 0, corrupt = 0, evicted = 0, failed = 0, read_bytes = 0, total = 0; double read_ms = 0; };
HdCacheStats hd_cache_stats();
// hdcache: hd_prefetch for files in memory (HdTextures::prefetch_data) when on
void hd_prefetch_data(const std::vector<std::pair<std::string, Bytes>>& files, const std::function<bool()>& go = {},
                      double budget_ms = 0, bool* stopped = nullptr);
void hd_prefetch(const std::vector<std::string>& files);   // HdTextures::prefetch when on
void hd_trim();                                            // HdTextures::trim when on

// Raster offsets in a file: a texture dictionary's directory, else a validated header scan (b3assetdump census).
std::vector<std::size_t> find_rasters(Bytes file);

// xboximport: one palette variant of one raster in a file, as an HD pack keys it. b3hdkeys (scan / compare) and the
// Xbox texture import (b3asset/xboximport.h) both enumerate the rasters through hd_each_raster, so the keys the import
// names its files after are the keys the checker (and the game's lookup) finds.
struct HdRasterVariant {
    std::size_t offset = 0;             // the raster in the file (find_rasters)
    int variant = 0, variants = 1;      // the palette variant (the cars and the car-select pictures have several)
    std::uint32_t cbp_add = 0;          // palette_cbp_add(file, offset, variant)
    bool decoded = false;               // else `why` says why and `raster` holds no levels
    std::string why;
    DecodedRaster raster;               // the PS2 decode, without the HD hook (whatever pack is set)
    std::vector<HdCandidate> keys;      // hd_candidate_keys (all_keys), else the base key alone; empty: no key
    // The base-level key (level 0, first 0, count 1: the one-level hash a pack file is named after); nullptr: none
    const HdCandidate* base() const {
        return !keys.empty() && keys[0].level == 0 && keys[0].first == 0 && keys[0].count == 1 ? &keys[0] : nullptr;
    }
};
// Every raster of `file` (find_rasters' order) and each of its palette variants (at least one), decoded on this thread
// with the hook off. `fn` returns false to stop. Returns the raster headers read. `gs` holds the variant's upload
// while `fn` runs.
std::size_t hd_each_raster(Bytes file, GsMemory& gs, const std::function<bool(const HdRasterVariant&)>& fn, bool all_keys = true);
// A thread's decodes without the HD hook while one is alive (the hook stays for other threads)
struct HdNoHookScope {
    HdNoHookScope();
    ~HdNoHookScope();
    HdNoHookScope(const HdNoHookScope&) = delete;
    HdNoHookScope& operator=(const HdNoHookScope&) = delete;
    bool saved;
};

// DDS reader: BC1 / BC2 / BC3 (DXT1/3/5, also via a DX10 header) and uncompressed 32-bit RGBA / BGRA, decoded to RGBA8
// with PS2 alpha mapped like GS alpha (0x80 and above -> 255, a -> 2a). Only the top level is read.
bool load_dds(const std::string& path, Image& out, std::string* why = nullptr);
bool decode_dds(const std::uint8_t* data, std::size_t size, Image& out, std::string* why = nullptr);
// texpack: a replacement file of either kind PCSX2 loads, DDS or PNG (by its signature), to RGBA8 with the PS2 alpha
// mapping a DDS gets (PNG: b3asset/png.h decode_png)
// `opaque_file` (optional): a PNG without alpha (colour type 0 / 2, no tRNS): it says nothing about alpha (HdTextures takes
// the game's own alpha for it)
bool decode_replacement(const std::uint8_t* data, std::size_t size, Image& out, std::string* why = nullptr, bool* opaque_file = nullptr);
bool load_replacement(const std::string& path, Image& out, std::string* why = nullptr);
// hdgpu: the header: format (kBC1..kBC3 from bc.h, kDdsRGBA / kDdsBGRA), size, the levels stored (1 without a mip
// count; only the ones the data holds whole) and where the top level starts
enum : int { kDdsRGBA = 4, kDdsBGRA = 5 };
struct DdsInfo { int fmt = 0, w = 0, h = 0, levels = 1; std::size_t at = 0; };
bool parse_dds(const std::uint8_t* data, std::size_t size, DdsInfo& out, std::string* why = nullptr);

}  // namespace b3asset

#endif
