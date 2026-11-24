// hdcache.h -- the HD texture cache on disk (hdcache, 2026-11-24; docs/remaster-features.md §46): what an HD pack replacement
// became for the GPU (HdTextures::apply's result: the cropped, alpha-ruled image or its BC chain) kept in the user data
// folder, so a texture converted once is read back on later races instead of converted again (25-40 ms a car texture with
// the big packs: the mid-race stutter of a vehicle's first draw). Only what was drawn is kept ("convert only what is
// used"); an entry is byte-identical to a fresh conversion (tests/test_hdcache.cpp).
//
// Layout: <user data folder>/cache/hd/<2 hex>/<16 hex>.b3hc, one entry per replacement result. The name is the key: the
// replacement file's identity (its path, size, modification time and the hash of its first and last 4 KB: a changed pack
// file gets new keys), the raster's request (crop, TEX0 size, TCC, the decode's size), the alpha rule's input (the
// original's alpha, when the rule can apply) and the conversion's mode (BC device, HD TEXTURE MEMORY, raw alpha) -- see
// hdpack.cpp disk_key. Entries are
// written whole to a temporary name and renamed (a crash or a second game leaves no half entry), and read back with their
// size and hash checked (a damaged entry is deleted and converted again). The folder is held under a size cap (2 GB;
// $B3_HD_CACHE_MB): the entries used longest ago go first. $B3_HD_CACHE=0: no cache.
//
// This header: the format's constants and the folder's size / clearing (the launcher's HD TEXTURES page). Standard library
// only; the cache itself is hdpack.cpp's (hdpack.h set_hd_cache_dir).
#ifndef B3ASSET_HDCACHE_H
#define B3ASSET_HDCACHE_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace b3asset::hdcache {

inline constexpr std::uint32_t kFormat = 1;               // the entry layout (hdpack.cpp write_entry / read_entry)
inline constexpr std::uint64_t kDefaultCapMB = 2048;      // the folder's size cap
inline constexpr const char* kExtension = ".b3hc";
inline constexpr char kMagic[8] = {'B', '3', 'H', 'D', 'C', 'A', 'C', 'H'};

// <user data folder>/cache/hd
inline std::filesystem::path folder(const std::filesystem::path& user_dir) { return user_dir / "cache" / "hd"; }

struct Usage {
    std::uint64_t bytes = 0;
    std::size_t entries = 0;
};
// The entries in `dir` (its two-hex subfolders) and their size; nothing when it is not there
inline Usage usage(const std::filesystem::path& dir) {
    Usage u;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || it->path().extension() != kExtension) continue;
        const std::uintmax_t n = it->file_size(e2);
        if (e2) continue;
        u.bytes += n;
        ++u.entries;
    }
    return u;
}
// Deletes the entries (and leftover temporary files) in `dir`; the game converts what it draws again. False (with `why`)
// when something could not be deleted (a game running holds an entry open on Windows: it is deleted next time)
inline bool clear(const std::filesystem::path& dir, std::string* why = nullptr) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return true;
    std::size_t left = 0;
    for (std::filesystem::directory_iterator sub(dir, ec), end; !ec && sub != end; sub.increment(ec)) {
        std::error_code e2;
        if (!sub->is_directory(e2)) continue;
        for (std::filesystem::directory_iterator f(sub->path(), e2), fend; !e2 && f != fend; f.increment(e2)) {
            const std::string name = f->path().filename().string();
            if (f->path().extension() != kExtension && name.find(".tmp") == std::string::npos) continue;
            std::error_code e3;
            if (!std::filesystem::remove(f->path(), e3) || e3) ++left;
        }
        std::error_code e4;
        std::filesystem::remove(sub->path(), e4);   // (only when empty)
    }
    if (left && why) *why = std::to_string(left) + " entries could not be deleted (in use by a running game?)";
    return left == 0;
}

}  // namespace b3asset::hdcache

#endif  // B3ASSET_HDCACHE_H
