// archive.h -- installing a mod from a .zip or .b3mod archive (mod kit part 1b, docs/modding/DESIGN.md §1.2;
// src/b3mod/archive.cpp, tests/test_mods.cpp).
//
// A .b3mod is a zip archive. The mod's mod.ini is at the archive's top, or in its one top folder (an archive made by
// zipping the mod's folder): that folder is not kept. install_archive unpacks it into <mods folder>/<id>/ (the id from
// the archive's mod.ini), all or nothing: into a hidden folder first (".install-<id>", which discover() skips), checked,
// then renamed into place.
//
// Refused, with the reason, and nothing written:
//   - entry names that would land outside the mod's folder or that a file system cannot hold the same everywhere: an
//     absolute path ('/' or '\' first), a drive letter or any ':', "." or ".." components, an empty component, control
//     characters or <>"|?*, a component ending in '.' or ' ', Windows device names (CON, NUL, COM1 ...), names that are
//     not UTF-8;
//   - links (an entry whose attributes say symbolic link or reparse point), encrypted entries, compression other than
//     stored / deflate, ZIP64 archives, entries over InstallOptions::max_entry bytes (or whose data runs past the size
//     their header gives), more than max_total bytes or max_entries entries in all, a CRC that does not match;
//   - two entries whose names differ only in case (they would be one file on Windows and macOS);
//   - no mod.ini at the top (or in the one top folder), or one with errors (parse_manifest);
//   - a mod with the same id already installed, unless InstallOptions::replace (then its folder is replaced, whatever
//     its name); a folder named <id> that holds something else.
//
// zlib (inflate); otherwise the standard library.
#ifndef B3MOD_ARCHIVE_H
#define B3MOD_ARCHIVE_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace b3mod {

namespace fs = std::filesystem;

// One entry of a zip's central directory
struct ZipEntry {
    std::string name;              // as stored ('/' separated, UTF-8 when the archive says so)
    std::uint16_t made_by = 0, flags = 0, method = 0;
    std::uint32_t crc = 0, csize = 0, usize = 0, external = 0, local_offset = 0;
    bool dir() const { return !name.empty() && (name.back() == '/' || name.back() == '\\'); }
    bool link() const;             // a symbolic link (Unix attributes) or a reparse point (Windows attributes)
};

// A zip archive's directory, read from its end (the end of central directory record) without trusting the local
// headers for anything but where an entry's data starts.
class ZipReader {
public:
    bool open(const fs::path& path, std::string* why);
    const std::vector<ZipEntry>& entries() const { return entries_; }
    // Streams an entry's bytes (inflated) to `sink`, checking the size and the CRC; at most `limit` bytes.
    // `sink` returns false to stop (a write error). false with `why` on any problem.
    template <class F>
    bool read(const ZipEntry& e, std::uint64_t limit, F&& sink, std::string* why) const {
        return read_impl(e, limit, [](void* u, const std::uint8_t* p, std::size_t n) { return (*static_cast<F*>(u))(p, n); }, &sink, why);
    }
    bool read_all(const ZipEntry& e, std::uint64_t limit, std::vector<std::uint8_t>& out, std::string* why) const;

private:
    bool read_impl(const ZipEntry& e, std::uint64_t limit, bool (*sink)(void*, const std::uint8_t*, std::size_t), void* user,
                   std::string* why) const;
    fs::path path_;
    std::uint64_t file_size_ = 0;
    std::vector<ZipEntry> entries_;
};

// A name inside an archive (or a mod) that is safe to create below a folder on every system: "" when it is, else why
// not. Backslashes count as separators. A trailing '/' (a folder entry) is allowed.
std::string unsafe_entry_name(const std::string& name);

struct InstallOptions {
    bool replace = false;                          // an installed mod with the same id: replace it
    std::uint64_t max_entry = 2ull << 30;          // bytes in one file (2 GB)
    std::uint64_t max_total = 16ull << 30;         // bytes in all (16 GB)
    std::size_t max_entries = 100000;
};
struct InstallResult {
    bool ok = false;
    std::string id, version, name;
    fs::path dir;                                  // where it is now (ok)
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    bool replaced = false;                         // an installed mod with this id was replaced
    bool exists = false;                           // refused because the id is installed (offer "Replace")
    std::vector<std::string> errors;               // why it was refused (not ok)
    std::vector<std::string> warnings;             // the manifest's notes
};
InstallResult install_archive(const fs::path& archive, const fs::path& mods_root, const InstallOptions& opt = {});

}  // namespace b3mod

#endif
