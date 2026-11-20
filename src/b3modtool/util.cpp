// b3modtool helpers: path patterns, file kinds, reading a ZIP (.b3mod) back, host files.
#include "b3modtool/modtool.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <zlib.h>

#include "b3asset/discfs.h"
#include "b3mod/mods.h"
#include "b3report/zip.h"

namespace b3modtool {

namespace {
bool fail(std::string* why, std::string what) { if (why) *why = std::move(what); return false; }
char up(char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c == '\\' ? '/' : c; }
bool match(std::string_view p, std::string_view s) {
    while (!p.empty()) {
        if (p.size() >= 2 && p[0] == '*' && p[1] == '*') {
            p.remove_prefix(2);
            if (!p.empty() && up(p[0]) == '/') {   // "**/": zero or more folders
                if (match(p.substr(1), s)) return true;
            }
            for (std::size_t i = 0; i <= s.size(); ++i)
                if (match(p, s.substr(i))) return true;
            return false;
        }
        if (p[0] == '*') {
            p.remove_prefix(1);
            for (std::size_t i = 0; i <= s.size(); ++i) {
                if (match(p, s.substr(i))) return true;
                if (i < s.size() && up(s[i]) == '/') break;
            }
            return false;
        }
        if (s.empty()) return false;
        if (p[0] == '?') { if (up(s[0]) == '/') return false; }
        else if (up(p[0]) != up(s[0])) return false;
        p.remove_prefix(1);
        s.remove_prefix(1);
    }
    return s.empty();
}
std::uint32_t le32(const std::uint8_t* p) { return std::uint32_t(p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24); }
std::uint16_t le16(const std::uint8_t* p) { return std::uint16_t(p[0] | p[1] << 8); }
}  // namespace

std::string to_upper_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = char(c - 32);
    return s;
}

bool glob_match(std::string_view pattern, std::string_view path) {
    while (!path.empty() && (path[0] == '/' || path[0] == '\\')) path.remove_prefix(1);
    while (!pattern.empty() && (pattern[0] == '/' || pattern[0] == '\\')) pattern.remove_prefix(1);
    if (pattern.find('/') == std::string_view::npos && pattern.find('\\') == std::string_view::npos) {
        const std::size_t cut = path.find_last_of("/\\");
        if (cut != std::string_view::npos) path.remove_prefix(cut + 1);
    }
    return match(pattern, path);
}

std::string file_kind(std::string_view name, const std::uint8_t* h, std::size_t n) {
    const std::string nm = to_upper_ascii(std::string(name));
    const std::size_t dot = nm.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string() : nm.substr(dot + 1);
    if (n >= 8 && le32(h) == 0x543C0000u && le32(h + 4) == 0xBCDEED81u) return "txd";
    if (is_string_table(h, n)) return "strings";
    if (n >= 4 && std::memcmp(h, "\x7F" "ELF", 4) == 0) return ext == "IRX" ? "iop module" : "program";
    if (ext == "RWS") return "rws audio stream";
    if (ext == "AWD") return "awd sound bank";
    if (ext == "HWD" || ext == "LWD") return "car sound";
    if (ext == "BGV" || ext == "BTV") return "car";
    if (ext == "BGD") return "track gamedata";
    if (ext == "M2V") return "movie";
    if (ext == "KFS") return "kfs";
    if (ext == "DAT") {
        const std::string base = nm.substr(nm.find_last_of('/') == std::string::npos ? 0 : nm.find_last_of('/') + 1);
        if (base == "STATIC.DAT" || base == "STREAMED.DAT" || base == "ENVIRO.DAT") return "track";
        return "dat";
    }
    if (ext == "XML" && nm.find("VDB") != std::string::npos) return "vdb tuning";
    if (ext == "BIN") return "bin";
    if (ext == "IMG" || ext == "IRX") return "iop module";
    if (ext == "ICO" || ext == "ICN" || ext == "SYS") return "memory card icon";
    if (ext == "CNF") return "config";
    return "";
}

bool zip_list(const Buf& z, std::vector<ZipEntry>& out, std::string* why) {
    out.clear();
    if (z.size() < 22) return fail(why, "not a ZIP file");
    std::size_t eocd = std::string::npos;
    for (std::size_t i = z.size() - 22 + 1; i-- > 0 && z.size() - i <= 22 + 0xFFFF;)
        if (le32(&z[i]) == 0x06054b50u) { eocd = i; break; }
    if (eocd == std::string::npos) return fail(why, "not a ZIP file (no end record)");
    const std::size_t count = le16(&z[eocd + 10]);
    const std::uint32_t dsize = le32(&z[eocd + 12]), doff = le32(&z[eocd + 16]);
    if (count == 0xFFFF || doff == 0xFFFFFFFFu) return fail(why, "ZIP64 archives are not read");
    if (doff > z.size() || dsize > z.size() - doff) return fail(why, "ZIP directory outside the file");
    std::size_t at = doff;
    for (std::size_t i = 0; i < count; ++i) {
        if (at + 46 > z.size() || le32(&z[at]) != 0x02014b50u) return fail(why, "damaged ZIP directory");
        ZipEntry e;
        e.flags = le16(&z[at + 8]);
        e.method = le16(&z[at + 10]);
        e.crc = le32(&z[at + 16]);
        e.csize = le32(&z[at + 20]);
        e.usize = le32(&z[at + 24]);
        const std::size_t nn = le16(&z[at + 28]), xn = le16(&z[at + 30]), cn = le16(&z[at + 32]);
        e.offset = le32(&z[at + 42]);
        if (at + 46 + nn > z.size()) return fail(why, "damaged ZIP directory");
        e.name.assign(reinterpret_cast<const char*>(&z[at + 46]), nn);
        if (e.csize == 0xFFFFFFFFu || e.usize == 0xFFFFFFFFu || e.offset == 0xFFFFFFFFu) return fail(why, e.name + ": ZIP64 entries are not read");
        out.push_back(std::move(e));
        at += 46 + nn + xn + cn;
    }
    return true;
}

bool zip_read(const Buf& z, const ZipEntry& e, Buf& out, std::string* why) {
    out.clear();
    if (e.flags & 1) return fail(why, e.name + ": encrypted");
    if (std::size_t(e.offset) + 30 > z.size() || le32(&z[e.offset]) != 0x04034b50u) return fail(why, e.name + ": damaged local header");
    const std::size_t data = std::size_t(e.offset) + 30 + le16(&z[e.offset + 26]) + le16(&z[e.offset + 28]);
    if (data > z.size() || e.csize > z.size() - data) return fail(why, e.name + ": data outside the file");
    if (e.method == 0) {
        if (e.csize != e.usize) return fail(why, e.name + ": stored sizes differ");
        out.assign(z.begin() + std::ptrdiff_t(data), z.begin() + std::ptrdiff_t(data + e.csize));
    } else if (e.method == 8) {
        out.resize(e.usize);
        z_stream s{};
        if (inflateInit2(&s, -15) != Z_OK) return fail(why, "zlib");
        s.next_in = const_cast<Bytef*>(&z[data]);
        s.avail_in = e.csize;
        s.next_out = out.data();
        s.avail_out = e.usize;
        const int rc = inflate(&s, Z_FINISH);
        const bool ok = rc == Z_STREAM_END && s.total_out == e.usize;
        inflateEnd(&s);
        if (!ok) return fail(why, e.name + ": does not inflate to its size (damaged)");
    } else {
        return fail(why, e.name + ": compression method " + std::to_string(e.method) + " (only stored and deflate are read)");
    }
    if (b3port::crc32_of(out.data(), out.size()) != e.crc) return fail(why, e.name + ": CRC error (damaged)");
    return true;
}

bool read_host_file(const std::string& path, Buf& out) { return b3asset::disc::read_file(b3mod::path_from_utf8(path), out); }   // (UTF-8)

bool write_host_file(const std::string& path, const Buf& data, std::string* why) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = b3mod::path_from_utf8(path);   // (UTF-8)
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
#ifdef _WIN32
    std::FILE* f = _wfopen(p.c_str(), L"wb");
#else
    std::FILE* f = std::fopen(p.c_str(), "wb");
#endif
    if (!f) return fail(why, "cannot create " + path);
    const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    if (std::fclose(f) != 0 || !ok) return fail(why, "cannot write " + path);
    return true;
}

}  // namespace b3modtool
