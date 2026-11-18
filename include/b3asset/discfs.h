// discfs.h -- the game's disc files, read-only, from a folder of the disc's files OR from the disc image itself
// (src/b3asset/discfs.cpp). Every reader of the disc data (the platform's GTFS and RW Audio file reads, the ELF and
// SYSTEM.CNF, the renderer's tracks / cars / textures, the launcher's checklist and art) goes through these functions,
// so `--game` may name either.
//
// A path is either a host path, or a path inside a disc image: "<image file>/<path in the image>", where <image file>
// is an existing regular file holding an ISO 9660 volume -- a cooked image (.iso: 2048-byte sectors, as a PS2 DVD
// dumps) or a raw one (.bin: 2352-byte sectors, mode 1 or mode 2 form 1), or a CHD of either (chd1: .chd, known by its
// "MComprHD" magic, not its name: chdman createdvd's 2048-byte sectors, or createcd's CD frames, the volume on the first
// data track: MODE1 / MODE2 form 1 user data as from a raw .bin; read through libchdr, src/b3asset/chd_libchdr.c, its
// hunks decoded on demand, ~64 MB of them kept, and read ahead on up to 4 worker threads while reads go on in order).
// The image file itself is the disc's root directory. Inside an image, names match ASCII-case-insensitively and ";1"
// version suffixes are not part of them; files may be up to 4 GB (several extents too: ISO 9660 level 3), images any
// size (64-bit offsets).
//
// The image is opened once per process (its directory tree read at that time) and stays open; reads take positioned
// reads (pread / ReadFile at an offset), so any thread may read any file at the same time (a CHD: one hunk decompressed
// at a time under the image's lock, other threads' reads in between).
#ifndef B3ASSET_DISCFS_H
#define B3ASSET_DISCFS_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace b3asset::disc {

// `path` is a disc image file (an ISO 9660 volume in it). Not cached for a file that is not one.
bool is_image(const std::filesystem::path& path);
// chd1: `path` is a CHD file (its first bytes "MComprHD"), whether or not it holds a disc image this module reads
bool is_chd(const std::filesystem::path& path);
// chd1: a CHD file that is not read as a disc image because libchdr cannot open it or a part of its data cannot be read
// (damaged, cut short, a CHD that needs its parent): one English sentence naming it and why, for messages. Empty for
// every other path (a CHD that is read; one without an ISO 9660 volume is "not a disc image", as an .iso would be).
std::string image_error(const std::filesystem::path& path);

bool exists(const std::filesystem::path& path);
bool is_dir(const std::filesystem::path& path);    // a host folder, an image file (its root) or a folder in an image
bool is_file(const std::filesystem::path& path);   // a host file (an image file too) or a file in an image
std::int64_t file_size(const std::filesystem::path& path);   // -1: not a file

// Reads a whole file (host or in an image). false: not there, or an I/O error.
bool read_file(const std::filesystem::path& path, std::vector<std::uint8_t>& out);

struct Entry {
    std::string name;              // as on the disc (no ";1") or as the host names it
    std::filesystem::path path;    // dir / name
    bool dir = false;
    std::uint64_t size = 0;        // files only
};
// The entries of a folder: a host folder in its file system's order (as std::filesystem::directory_iterator), a folder
// in an image in the disc's order. Empty when it is not a folder.
std::vector<Entry> list_dir(const std::filesystem::path& dir);

// `dir` / `relative` with each component of `relative` ('/' or '\' separated) matched ASCII-case-insensitively against
// list_dir. Empty when a component is missing or ambiguous (two names differing only in case), or for "." / "..".
std::filesystem::path resolve_ci(const std::filesystem::path& dir, const std::string& relative);

// One open file: a host file or a file in an image. Copies share it; reads may come from any thread.
class File {
public:
    bool open(const std::filesystem::path& path);   // false: not a file
    void close() { impl_.reset(); }
    bool is_open() const { return impl_ != nullptr; }
    std::uint64_t size() const;
    // Up to `n` bytes at `offset` (none past the end): the count read, -1 on an I/O error.
    std::int64_t read_at(std::uint64_t offset, void* dst, std::size_t n) const;
    const std::string& name() const;   // the path as opened (messages)

    struct Impl;
private:
    std::shared_ptr<const Impl> impl_;
};

// ---- the mod kit's data overlay (include/b3mod/mods.h; docs/modding/DESIGN.md §1.2) ----
// Files that stand in for the disc's below one root (the game's folder or image as this program names it, e.g. the
// renderer's --game): `files` maps a path below `root` in the overlay's key form (b3mod::disc_key: '/' separated,
// ASCII upper case, "TRACKS/US/C2_V2/STATIC.DAT") to the host file that answers for it. While one is set, every query
// here for a path below `root` (compared lexically, after lexically_normal) sees these files first, whatever the case
// of the name: exists / is_file / file_size / read_file / File::open take the mod's file; is_dir is true for the folders
// that hold one; list_dir adds the names only the overlay has; resolve_ci finds them. Every other path is untouched.
// Empty `files`: no overlay (the default). Set it before the readers start; it is replaced as a whole.
void set_overlay(const std::filesystem::path& root, const std::vector<std::pair<std::string, std::filesystem::path>>& files);
bool has_overlay();

}  // namespace b3asset::disc

#endif
