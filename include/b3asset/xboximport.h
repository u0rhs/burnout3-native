// xboximport.h -- Import Xbox textures (1.3.0): an HD texture pack built on the player's PC from their own Xbox disc
// image. Nothing from the Xbox disc ships with the port; the pack is written to <user data>/xbox-textures/ and loaded
// below the mods' packs and the player's own HD pack (b3asset/hdpack.h hd_folders).
//
// The Xbox version keeps the PS2 one's files, names and UV layouts; about a quarter of the track textures are 2x-4x
// larger there, with real extra detail (the study: b3-port-win/xboxtex-scratch/REPORT.md). An HD pack is keyed by
// the PS2 texture as the GS sees it (hdpack.h), so each Xbox texture is matched by name to the PS2 raster in the same
// file, and written under that raster's base key (hd_each_raster, the enumeration b3hdkeys checks with).
//
// The source per key (one file per distinct key; identical rasters across routes share one):
//   - skipped: the Xbox texture is not larger (or not a whole multiple of the PS2 size), the PS2 one is 32 texels or
//     less a side, its name starts with "water" (a 16x16 placeholder on the Xbox: its water is a shader), a non-car
//     raster with several palettes, or the colour check fails (the art differs): the Xbox texture box-scaled down to the
//     PS2 size and given a per-channel gain against the PS2 decode is under 20 dB PSNR, or a gain is outside 0.5..2;
//   - tracks, front end, global: a detail transfer that keeps the PS2 colours and adds the Xbox detail (the Xbox
//     textures are stored 1.1-1.5x darker): out = up(PS2) + gain * (Xbox - up(down(Xbox))), bilinear, wrapping on track
//     textures, after the visible colour is spread into transparent texels (no dark fringes on cut-outs), the colour
//     detail only where the Xbox texel and its blurred copy are mostly opaque; alpha: an opaque PS2 texture stays opaque,
//     else up(PS2) + Xbox - up(down(Xbox)) where the Xbox alpha agrees with the PS2 one (mean difference under 16 at the
//     PS2 size: the cut-out edges at the Xbox resolution), up(PS2) alone where it means something else;
//   - cars (PVEH/*.BGV, *.BTV: one P8 atlas each, 8 paint palettes): per PS2 paint key, a palette for the Xbox index
//     image from the PS2 decode (each Xbox index takes the most frequent PS2 colour and alpha over its texels), and a
//     texel whose mapped colour differs from the PS2 texel by more than 48 in a channel keeps the PS2 texel (where the
//     Xbox livery differs). Skipped when under 88% of the texels are within 24 of their index's colour, or the result
//     box-scaled down is under 26 dB against the PS2 decode.
// The result covers TEX0's 2^TW x 2^TH (the edge repeated past the raster's own size), alpha in PS2 units (0x80
// opaque), as DDS: BC1 when opaque, else BC3, the top level only (the loader builds the mips).
#ifndef B3ASSET_XBOXIMPORT_H
#define B3ASSET_XBOXIMPORT_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "b3asset/raster.h"

namespace b3asset::xboximport {

constexpr int kVersion = 1;                              // import.ini's importer version (a newer one offers a re-import)
constexpr std::uint64_t kNeedBytes = 200ull << 20;       // free space asked for (the pack is ~90 MB as BC)
constexpr std::uint64_t kMaxFileBytes = 64ull << 20;     // a file read whole, PS2 or Xbox (the USA disc's largest with
                                                         // textures: 12.6 MB, EU/M1_V2 static.dat; a damaged image's
                                                         // claim is not allocated, x8 threads)
inline const char* kFolderName = "xbox-textures";        // in the user data folder
inline const char* kInfoName = "import.ini";

// Why a key was not written
enum Reason : int {
    kNoXboxFile,       // the PS2 file has no Xbox counterpart
    kNoMatch,          // no Xbox texture of that name in the file
    kNotLarger,        // the Xbox one is not larger (or not a whole multiple of the PS2 size)
    kSmall,            // 32 texels or less a side
    kWater,            // the Xbox water placeholder
    kPaletteVariant,   // a non-car raster with several palettes
    kCheck,            // the layout / colour check failed (the art differs)
    kFormat,           // an Xbox format the decoder does not handle, or a decode failed
    kCarMap,           // the car palette mapping is not clean
    kReasons
};
const char* reason_name(int r);   // "not larger", ...

struct Progress {
    int stage = 0;             // 0 reading the files (keys and matches), 1 building the textures
    int done = 0, total = 0;   // files
    std::string group;         // the file being read: "tracks", "cars" or "front end"
    std::size_t written = 0;
};
struct Result {
    bool ok = false, cancelled = false;
    std::string error;                       // in words, when !ok and !cancelled
    std::size_t written = 0, cars = 0;       // files written (of them: car paints)
    std::size_t keys = 0;                    // distinct PS2 keys seen
    std::size_t files = 0;                   // PS2 files read
    std::size_t alpha_ps2 = 0;               // written with the PS2 alpha alone (the Xbox alpha disagrees)
    std::uint64_t bytes = 0;                 // written
    std::array<std::size_t, kReasons> skipped{};
    double seconds = 0;
};
// modsec: how a folder tree is deleted (the .partial / .old folders, Remove). The launcher passes b3mod::remove_tree
// (include/b3mod/archive.h: a link or junction in the tree is removed as the link, never what it points to); b3asset
// does not link b3mod, so the default is remove_tree_plain. True when nothing is left.
using RemoveTree = std::function<bool(const std::filesystem::path&)>;
bool remove_tree_plain(const std::filesystem::path& p);   // std::filesystem::remove_all

struct Options {
    int threads = 0;                         // 0: the machine's cores, at most 8
    bool cars = true;
    std::uint64_t need_bytes = kNeedBytes;   // free space required before starting
    std::uint64_t max_file_bytes = kMaxFileBytes;   // a larger file is not read (its textures are not imported)
    RemoveTree remove_tree;                         // empty: remove_tree_plain
};

// The whole import: `image` (an Xbox disc image of Burnout 3: Takedown), `game_dir` (the PS2 game's files: a folder or
// a disc image, b3asset/discfs.h) into `out_dir` (written as `out_dir`.partial, renamed when complete, replacing an
// earlier import). `progress` is called from the worker threads (one at a time). Cancel or an error leaves `out_dir`
// as it was and removes the .partial folder. Never throws: an exception anywhere in it (out of memory on a damaged
// image) is a failed result with a plain message.
Result run(const std::filesystem::path& image, const std::filesystem::path& game_dir, const std::filesystem::path& out_dir,
           const std::function<void(const Progress&)>& progress = {}, const std::atomic<bool>* cancel = nullptr,
           const Options& options = {});

// An import's folder as import.ini describes it
struct Status {
    bool present = false;          // the folder and its import.ini are there
    int version = 0;               // the importer's
    std::string image;             // the image's file name
    std::uint64_t image_size = 0;
    std::int64_t image_time = 0;   // its modification time (Unix seconds)
    std::int64_t date = 0;         // when it was imported (Unix seconds)
    std::size_t written = 0, cars = 0;
    std::uint64_t bytes = 0;
};
Status read_status(const std::filesystem::path& out_dir);
// Deletes an import's folder (and a .partial one left by a crash). false with a reason in words.
bool remove(const std::filesystem::path& out_dir, std::string* why = nullptr, const RemoveTree& remove_tree = {});
// Free bytes on the drive that holds `dir` (or its nearest existing parent); 0 when unknown
std::uint64_t free_bytes(const std::filesystem::path& dir);

// ---- the steps (public for the tests) ----
// RGBA8 images (Image::rgba, alpha 255 opaque)
Image box_down(const Image& src, int w, int h);                     // whole-number factors only
Image bilinear(const Image& src, int w, int h, bool wrap);          // texel centres, clamp or wrap at the edges
struct Check {
    double psnr = 0, gain[3] = {1, 1, 1};
    bool ok = false;
    bool alpha_agrees = false;   // the down-scaled Xbox alpha within 16 levels (mean) of the PS2 alpha
    bool ps2_opaque = false;     // the PS2 texture is opaque throughout
};
// The visible colour spread into the transparent texels (alpha under 8 / 255: 16 passes of the 4 neighbours' mean, then
// the mean visible colour); alpha unchanged
Image bleed(const Image& im, bool wrap);
// The layout / colour check of `xbox` (a whole multiple of `ps2`'s size) against `ps2`
Check colour_check(const Image& ps2, const Image& xbox);
// The detail transfer (needs a passing check's gains)
Image detail_transfer(const Image& ps2, const Image& xbox, const Check& c, bool wrap);
struct CarCheck { double consistency = 0, psnr = 0, fallback = 0; bool ok = false; };
// The car paint mapping: `index` (W x H Xbox palette indices, a whole multiple of `ps2`'s size)
Image car_map(const Image& ps2, const std::vector<std::uint8_t>& index, int w, int h, CarCheck& c);
// `im` grown to `w` x `h` (>= its size) with its edge texels repeated
Image extend(const Image& im, int w, int h);
// A DDS file of `im` (RGBA8, 255 opaque): alpha halved to PS2 units, BC1 when every texel is opaque else BC3, top
// level only. `fmt` receives kBC1 / kBC3.
std::vector<std::uint8_t> encode_dds(const Image& im, int* fmt = nullptr);

}  // namespace b3asset::xboximport

#endif
