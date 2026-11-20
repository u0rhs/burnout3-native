// xdvdfs.h -- the player's own Xbox disc image, read only (Import Xbox textures, b3asset/xboximport.h).
//
// XDVDFS (the Xbox DVD file system) as the disc images hold it: 2048-byte sectors, the volume descriptor at sector 32
// ("MICROSOFT*XBOX*MEDIA" at its start, again at +0x7EC (not required); the root directory's sector (u32) and size
// (u32) at +0x14).
// The game partition starts at offset 0 in a trimmed image (an "xiso") and further in, in a full disc dump: the
// offsets tried are 0, 0x18300000, 0xFD90000 and 0x2080000. A directory is a binary tree of entries inside its own
// sectors: u16 left and right child offsets (in 4-byte units from the directory's start, 0 none), u32 start sector,
// u32 size, u8 attributes (0x10 a directory), u8 name length, the name; 0xFFFF / 0xFFFF pads a sector's end.
//
// The image is untrusted input: every offset is checked against the directory and the image, a directory's entries
// are visited once each (a cycle ends), and the tree has caps (entries, depth, directory and file sizes).
//
// This is not b3asset/discfs.h: discfs is the game's own (PS2) disc for the whole process; this image is opened only
// while an import runs. One XdvdfsImage may be read from several threads (reads are positioned and locked).
#ifndef B3ASSET_XDVDFS_H
#define B3ASSET_XDVDFS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace b3asset {

class XdvdfsImage {
public:
    struct Entry {
        std::string path;              // '/' separated from the root, as the disc spells it ("Tracks/US/C1_V1/static.dat")
        std::uint64_t offset = 0;      // in the image file
        std::uint32_t size = 0;
        bool dir = false;
    };
    // Caps on what the tree may hold (an image within them is a real game disc several times over)
    static constexpr std::size_t kMaxEntries = 200000, kMaxDepth = 32;
    static constexpr std::uint32_t kMaxDirBytes = 16u << 20, kMaxFileBytes = 1024u << 20;

    XdvdfsImage() = default;
    ~XdvdfsImage() { close(); }
    XdvdfsImage(const XdvdfsImage&) = delete;
    XdvdfsImage& operator=(const XdvdfsImage&) = delete;

    // false with a reason in words: not readable, no XDVDFS volume ("not an Xbox disc image"), a damaged tree
    bool open(const std::filesystem::path& image, std::string* why = nullptr);
    void close();
    bool is_open() const { return f_ != nullptr; }
    std::uint64_t base() const { return base_; }            // where the game partition starts in the file
    std::uint64_t image_size() const { return size_; }
    const std::vector<Entry>& list() const { return entries_; }   // every file and folder, sorted by path (case-blind)
    // A file by its path, '/' or '\' separated, ASCII case-insensitive (nullptr: none, or it names a folder)
    const Entry* find(const std::string& path) const;
    // A whole file (false: not there, or a read failed)
    bool read(const std::string& path, std::vector<std::uint8_t>& out) const;
    bool read(const Entry& e, std::vector<std::uint8_t>& out) const;

    // ASCII-lower-case key of a path with '\' as '/' (find's matching)
    static std::string key(const std::string& path);

private:
    bool read_at(std::uint64_t off, void* dst, std::size_t n) const;
    bool walk(std::uint32_t sector, std::uint32_t size, const std::string& prefix, std::size_t depth, std::string* why);
    std::FILE* f_ = nullptr;
    mutable std::mutex m_;
    std::uint64_t base_ = 0, size_ = 0;
    std::vector<Entry> entries_;
    std::vector<std::pair<std::string, std::size_t>> index_;   // key -> entries_ index, sorted by key
    std::vector<std::uint32_t> dirs_seen_;                      // directory start sectors walked (a cycle ends)
};

}  // namespace b3asset

#endif
