// zip.h -- a small ZIP archive writer for bug reports (bug_report.h): entries compressed with deflate (zlib, raw
// deflate) or stored when that does not make them smaller, UTF-8 names, DOS times in local time, one central directory.
// No ZIP64: an archive (and each entry) stays under 4 GB, which a bug report's capped logs always do.
// Every entry is built in memory and written with its sizes known (no data descriptors), so any unzip tool, Windows
// Explorer included, reads it.
#ifndef B3REPORT_ZIP_H
#define B3REPORT_ZIP_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace b3port {

// CRC-32 (the ZIP / zlib polynomial); `crc` continues an earlier value
std::uint32_t crc32_of(const void* data, std::size_t n, std::uint32_t crc = 0);

class ZipWriter {
public:
    ZipWriter() = default;
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;
    ~ZipWriter();   // an archive not close()d is removed

    // Creates (replaces) the archive. false: error() says why.
    bool open(const std::filesystem::path& path);
    // An entry `name` ('/' separated, UTF-8) with `n` bytes, modified at `mtime` (local time in the archive).
    // `compress` false: stored as is.
    bool add(const std::string& name, const void* data, std::size_t n, std::time_t mtime, bool compress = true);
    bool add(const std::string& name, const std::string& text, std::time_t mtime) { return add(name, text.data(), text.size(), mtime); }
    // A file from disk, under `name`; its own modification time.
    bool add_file(const std::string& name, const std::filesystem::path& file);
    // Writes the central directory and closes the file. false: the archive is incomplete (and removed).
    bool close();

    const std::string& error() const { return error_; }
    std::size_t entries() const { return dir_.size(); }

    // The DOS date and time of `t` (local), as the ZIP headers hold them: (date << 16) | time
    static std::uint32_t dos_time(std::time_t t);

private:
    struct Entry {
        std::string name;
        std::uint32_t crc = 0, csize = 0, usize = 0, offset = 0, dos = 0;
        std::uint16_t method = 0;
    };
    bool fail(std::string why);
    bool write(const void* p, std::size_t n);

    std::FILE* f_ = nullptr;
    std::filesystem::path path_;
    std::uint64_t at_ = 0;
    std::vector<Entry> dir_;
    std::string error_;
};

}  // namespace b3port

#endif
