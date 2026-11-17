// zip.cpp -- see include/b3report/zip.h. Format: PKWARE APPNOTE 6.3 (local file header 0x04034b50, central directory
// header 0x02014b50, end of central directory 0x06054b50), version needed 2.0, general purpose bit 11 (UTF-8 names).
#include "b3report/zip.h"

#include <zlib.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>

namespace b3port {

namespace {

void put16(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(std::uint8_t(v));
    b.push_back(std::uint8_t(v >> 8));
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

// raw deflate (no zlib header: the ZIP method 8 stream); false when zlib fails
bool deflate_raw(const void* data, std::size_t n, std::vector<std::uint8_t>& out) {
    z_stream z{};
    if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    out.resize(deflateBound(&z, uLong(n)) + 16);
    z.next_in = static_cast<Bytef*>(const_cast<void*>(data));
    z.avail_in = uInt(n);
    z.next_out = out.data();
    z.avail_out = uInt(out.size());
    const int rc = deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return rc == Z_STREAM_END;
}

}  // namespace

std::uint32_t crc32_of(const void* data, std::size_t n, std::uint32_t crc) {
    const auto* p = static_cast<const Bytef*>(data);
    uLong c = crc;
    while (n > 0) {   // zlib's length is an uInt
        const uInt step = n > 0x40000000u ? 0x40000000u : uInt(n);
        c = crc32(c, p, step);
        p += step;
        n -= step;
    }
    return std::uint32_t(c);
}

std::uint32_t ZipWriter::dos_time(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    int year = tm.tm_year + 1900;
    if (year < 1980) return (1u << 5 | 1u) << 16;   // 1980-01-01 00:00, the format's first day
    if (year > 2107) year = 2107;
    const std::uint32_t date = std::uint32_t((year - 1980) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
    const std::uint32_t time = std::uint32_t(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
    return date << 16 | time;
}

ZipWriter::~ZipWriter() {
    if (f_) {
        std::fclose(f_);
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
}

bool ZipWriter::fail(std::string why) {
    if (error_.empty()) error_ = std::move(why);
    return false;
}

bool ZipWriter::write(const void* p, std::size_t n) {
    if (!f_) return fail("the archive is not open");
    if (n && std::fwrite(p, 1, n, f_) != n) return fail("cannot write " + path_.string());
    at_ += n;
    return true;
}

bool ZipWriter::open(const std::filesystem::path& path) {
    if (f_) return fail("already open");
    path_ = path;
#ifdef _WIN32
    f_ = _wfopen(path.c_str(), L"wb");
#else
    f_ = std::fopen(path.c_str(), "wb");
#endif
    if (!f_) return fail("cannot create " + path.string());
    at_ = 0;
    dir_.clear();
    error_.clear();
    return true;
}

bool ZipWriter::add(const std::string& name, const void* data, std::size_t n, std::time_t mtime, bool compress) {
    if (!f_) return fail("the archive is not open");
    if (name.empty() || name.size() > 0xFFFF) return fail("bad entry name");
    if (n >= 0xFFFFFFFFull || at_ >= 0xFFFFFFFFull) return fail(name + ": too large for a ZIP without ZIP64");
    Entry e;
    e.name = name;
    e.crc = crc32_of(data, n);
    e.usize = std::uint32_t(n);
    e.dos = dos_time(mtime);
    e.offset = std::uint32_t(at_);
    std::vector<std::uint8_t> packed;
    const void* body = data;
    std::size_t body_n = n;
    if (compress && n > 0 && deflate_raw(data, n, packed) && packed.size() < n) {
        e.method = 8;
        body = packed.data();
        body_n = packed.size();
    }
    e.csize = std::uint32_t(body_n);
    std::vector<std::uint8_t> h;
    put32(h, 0x04034b50);
    put16(h, 20);                 // version needed: 2.0 (deflate)
    put16(h, 1u << 11);           // UTF-8 name
    put16(h, e.method);
    put32(h, e.dos);              // time, then date (little-endian of the low half first)
    put32(h, e.crc);
    put32(h, e.csize);
    put32(h, e.usize);
    put16(h, std::uint32_t(name.size()));
    put16(h, 0);                  // no extra field
    if (!write(h.data(), h.size()) || !write(name.data(), name.size()) || !write(body, body_n)) return false;
    if (at_ >= 0xFFFFFFFFull) return fail("the archive is too large for a ZIP without ZIP64");
    dir_.push_back(std::move(e));
    return true;
}

bool ZipWriter::add_file(const std::string& name, const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail("cannot read " + file.string());
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::time_t mtime = std::time(nullptr);
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(file, ec);
    if (!ec) {   // file_clock -> system_clock (clock_cast is missing in some standard libraries: the offset at now)
        const auto sys = std::chrono::system_clock::now() +
                         std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - std::filesystem::file_time_type::clock::now());
        mtime = std::chrono::system_clock::to_time_t(sys);
    }
    return add(name, bytes.data(), bytes.size(), mtime);
}

bool ZipWriter::close() {
    if (!f_) return fail("the archive is not open");
    if (!error_.empty()) {
        std::fclose(f_);
        f_ = nullptr;
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        return false;
    }
    const std::uint64_t cd_start = at_;
    std::vector<std::uint8_t> cd;
    for (const Entry& e : dir_) {
        put32(cd, 0x02014b50);
        put16(cd, 0x0314);            // made by: version 2.0 (20), UNIX (3) -- external attributes as Unix modes
        put16(cd, 20);
        put16(cd, 1u << 11);
        put16(cd, e.method);
        put32(cd, e.dos);
        put32(cd, e.crc);
        put32(cd, e.csize);
        put32(cd, e.usize);
        put16(cd, std::uint32_t(e.name.size()));
        put16(cd, 0);                 // extra
        put16(cd, 0);                 // comment
        put16(cd, 0);                 // disk
        put16(cd, 0);                 // internal attributes
        put32(cd, 0100644u << 16);    // a regular file, rw-r--r--
        put32(cd, e.offset);
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }
    std::vector<std::uint8_t> end;
    put32(end, 0x06054b50);
    put16(end, 0);
    put16(end, 0);
    put16(end, std::uint32_t(dir_.size()));
    put16(end, std::uint32_t(dir_.size()));
    put32(end, std::uint32_t(cd.size()));
    put32(end, std::uint32_t(cd_start));
    put16(end, 0);
    const bool ok = dir_.size() <= 0xFFFF && cd_start + cd.size() < 0xFFFFFFFFull && write(cd.data(), cd.size()) && write(end.data(), end.size());
    const bool closed = std::fclose(f_) == 0;
    f_ = nullptr;
    if (!ok || !closed) {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        return fail("cannot finish " + path_.string());
    }
    return true;
}

}  // namespace b3port
