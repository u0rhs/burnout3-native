// archive.cpp -- see include/b3mod/archive.h. The zip format: PKWARE APPNOTE 6.3 (the end of central directory record
// 0x06054b50, central directory headers 0x02014b50, local file headers 0x04034b50); methods 0 (stored) and 8 (deflate,
// zlib's raw inflate).
#include "b3mod/archive.h"

#include "b3mod/mods.h"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace b3mod {

namespace {

std::uint16_t rd16(const std::uint8_t* p) { return std::uint16_t(p[0] | p[1] << 8); }
std::uint32_t rd32(const std::uint8_t* p) { return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }

bool fail(std::string* why, std::string w) {
    if (why) *why = std::move(w);
    return false;
}

bool read_at(std::ifstream& f, std::uint64_t at, void* dst, std::size_t n) {
    f.clear();
    f.seekg(static_cast<std::streamoff>(at));
    f.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
    return f && static_cast<std::size_t>(f.gcount()) == n;
}

std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool valid_utf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        int n = b < 0x80 ? 0 : (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || (n == 1 && b < 0xC2)) return false;
        ++i;
        for (int k = 0; k < n; ++k, ++i)
            if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return false;
    }
    return true;
}

// '/' separated, no trailing '/'
std::string normal_name(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

}  // namespace

bool ZipEntry::link() const {
    const unsigned host = made_by >> 8;
    if (host == 3 || host == 19) {   // Unix, OS X: st_mode in the high half
        const std::uint32_t mode = external >> 16;
        if ((mode & 0170000u) == 0120000u) return true;   // S_IFLNK
    }
    return (external & 0x400u) != 0 && (host == 0 || host == 10 || host == 11 || host == 14);   // FILE_ATTRIBUTE_REPARSE_POINT
}

bool ZipReader::open(const fs::path& path, std::string* why) {
    entries_.clear();
    path_ = path;
    std::error_code ec;
    file_size_ = fs::file_size(path, ec);
    if (ec) return fail(why, "cannot read the archive");
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail(why, "cannot read the archive");
    // the end of central directory record: 22 bytes and a comment of up to 65535
    const std::uint64_t tail = std::min<std::uint64_t>(file_size_, 22 + 65535);
    if (tail < 22) return fail(why, "not a zip archive (too short)");
    std::vector<std::uint8_t> t(static_cast<std::size_t>(tail));
    if (!read_at(f, file_size_ - tail, t.data(), t.size())) return fail(why, "cannot read the archive");
    std::int64_t at = -1;
    for (std::int64_t k = std::int64_t(t.size()) - 22; k >= 0; --k)
        if (rd32(&t[std::size_t(k)]) == 0x06054b50u && std::size_t(k) + 22 + rd16(&t[std::size_t(k) + 20]) == t.size()) {
            at = k;
            break;
        }
    if (at < 0) return fail(why, "not a zip archive (no end of central directory)");
    const std::uint8_t* e = &t[std::size_t(at)];
    const std::uint64_t eocd = file_size_ - tail + std::uint64_t(at);
    const std::uint16_t disk = rd16(e + 4), cd_disk = rd16(e + 6), here = rd16(e + 8), total = rd16(e + 10);
    const std::uint32_t cd_size = rd32(e + 12), cd_off = rd32(e + 16);
    if (at >= 20 && rd32(&t[std::size_t(at) - 20]) == 0x07064b50u) return fail(why, "a ZIP64 archive (not supported: make it with a plain zip tool, files under 4 GB)");
    if (total == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu) return fail(why, "a ZIP64 archive (not supported)");
    if (disk != 0 || cd_disk != 0 || here != total) return fail(why, "a multi-part archive (not supported)");
    if (std::uint64_t(cd_off) + cd_size > eocd) return fail(why, "a damaged archive (the directory is outside it)");
    std::vector<std::uint8_t> cd(cd_size);
    if (cd_size && !read_at(f, cd_off, cd.data(), cd.size())) return fail(why, "cannot read the archive's directory");
    std::size_t p = 0;
    for (unsigned k = 0; k < total; ++k) {
        if (p + 46 > cd.size() || rd32(&cd[p]) != 0x02014b50u) return fail(why, "a damaged archive (directory entry " + std::to_string(k + 1) + ")");
        ZipEntry z;
        z.made_by = rd16(&cd[p + 4]);
        z.flags = rd16(&cd[p + 8]);
        z.method = rd16(&cd[p + 10]);
        z.crc = rd32(&cd[p + 16]);
        z.csize = rd32(&cd[p + 20]);
        z.usize = rd32(&cd[p + 24]);
        const std::size_t nl = rd16(&cd[p + 28]), xl = rd16(&cd[p + 30]), cl = rd16(&cd[p + 32]);
        z.external = rd32(&cd[p + 38]);
        z.local_offset = rd32(&cd[p + 42]);
        if (p + 46 + nl + xl + cl > cd.size()) return fail(why, "a damaged archive (directory entry " + std::to_string(k + 1) + ")");
        z.name.assign(reinterpret_cast<const char*>(&cd[p + 46]), nl);
        if (z.csize == 0xFFFFFFFFu || z.usize == 0xFFFFFFFFu || z.local_offset == 0xFFFFFFFFu)
            return fail(why, "a ZIP64 entry (" + z.name + "): not supported");
        entries_.push_back(std::move(z));
        p += 46 + nl + xl + cl;
    }
    return true;
}

bool ZipReader::read_impl(const ZipEntry& e, std::uint64_t limit, bool (*sink)(void*, const std::uint8_t*, std::size_t), void* user,
                          std::string* why) const {
    if (e.flags & 1u) return fail(why, e.name + ": encrypted (not supported)");
    if (e.method != 0 && e.method != 8) return fail(why, e.name + ": compression method " + std::to_string(e.method) + " (only stored and deflate)");
    if (e.usize > limit) return fail(why, e.name + ": " + std::to_string(e.usize) + " bytes, over the limit of " + std::to_string(limit));
    std::ifstream f(path_, std::ios::binary);
    std::uint8_t h[30];
    if (!f || !read_at(f, e.local_offset, h, sizeof h) || rd32(h) != 0x04034b50u) return fail(why, e.name + ": a damaged archive (no local header)");
    const std::uint64_t data = std::uint64_t(e.local_offset) + 30 + rd16(h + 26) + rd16(h + 28);
    if (data + e.csize > file_size_) return fail(why, e.name + ": a damaged archive (the data runs past its end)");
    if (e.method == 0 && e.csize != e.usize) return fail(why, e.name + ": a damaged archive (stored sizes differ)");
    f.clear();
    f.seekg(static_cast<std::streamoff>(data));
    std::vector<std::uint8_t> in(1 << 16), out(1 << 16);
    std::uint64_t left = e.csize, done = 0;
    uLong crc = crc32(0, nullptr, 0);
    z_stream z{};
    if (e.method == 8 && inflateInit2(&z, -MAX_WBITS) != Z_OK) return fail(why, "zlib: inflateInit2 failed");
    bool ended = e.method == 0;
    auto give = [&](const std::uint8_t* p, std::size_t n) {
        done += n;
        if (done > e.usize || done > limit) return fail(why, e.name + ": more data than its header says (" + std::to_string(e.usize) + " bytes)");
        crc = crc32(crc, p, uInt(n));
        if (!sink(user, p, n)) return fail(why, e.name + ": cannot write it");
        return true;
    };
    bool ok = true;
    while (ok && left > 0) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(left, in.size()));
        f.read(reinterpret_cast<char*>(in.data()), static_cast<std::streamsize>(n));
        if (static_cast<std::size_t>(f.gcount()) != n) { ok = fail(why, e.name + ": cannot read its data"); break; }
        left -= n;
        if (e.method == 0) { ok = give(in.data(), n); continue; }
        z.next_in = in.data();
        z.avail_in = uInt(n);
        while (ok && z.avail_in > 0 && !ended) {
            z.next_out = out.data();
            z.avail_out = uInt(out.size());
            const int rc = inflate(&z, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) { ok = fail(why, e.name + ": damaged compressed data"); break; }
            ok = give(out.data(), out.size() - z.avail_out);
            if (rc == Z_STREAM_END) ended = true;
        }
    }
    if (e.method == 8) {
        while (ok && !ended) {   // all input given: what is still inside zlib
            z.next_out = out.data();
            z.avail_out = uInt(out.size());
            const int rc = inflate(&z, Z_FINISH);
            if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) { ok = fail(why, e.name + ": damaged compressed data"); break; }
            const std::size_t got = out.size() - z.avail_out;
            if (got) ok = give(out.data(), got);
            if (rc == Z_STREAM_END) ended = true;
            else if (!got) { ok = fail(why, e.name + ": the compressed data ends early"); break; }
        }
        inflateEnd(&z);
    }
    if (!ok) return false;
    if (done != e.usize) return fail(why, e.name + ": " + std::to_string(done) + " bytes, its header says " + std::to_string(e.usize));
    if (std::uint32_t(crc) != e.crc) return fail(why, e.name + ": the CRC does not match (a damaged archive)");
    return true;
}

bool ZipReader::read_all(const ZipEntry& e, std::uint64_t limit, std::vector<std::uint8_t>& out, std::string* why) const {
    out.clear();
    return read(e, limit, [&out](const std::uint8_t* p, std::size_t n) { out.insert(out.end(), p, p + n); return true; }, why);
}

std::string unsafe_entry_name(const std::string& raw) {
    if (raw.empty()) return "an empty name";
    if (!valid_utf8(raw)) return "the name is not UTF-8";
    for (const char ch : raw) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F) return "a control character in the name";
        if (c == ':') return "a drive letter or ':' in the name";
        if (std::string_view("<>\"|?*").find(ch) != std::string_view::npos) return std::string("'") + ch + "' in the name";
    }
    if (raw.front() == '/' || raw.front() == '\\') return "an absolute path";
    std::string s = raw;
    std::replace(s.begin(), s.end(), '\\', '/');
    if (s.back() == '/') s.pop_back();   // a folder entry
    if (s.empty()) return "an empty name";
    std::size_t at = 0;
    while (at <= s.size()) {
        std::size_t end = s.find('/', at);
        if (end == std::string::npos) end = s.size();
        const std::string c = s.substr(at, end - at);
        if (c.empty()) return "an empty folder name (\"//\")";
        if (c == "." || c == "..") return "a \"" + c + "\" component (leads out of the folder)";
        if (c.back() == '.' || c.back() == ' ') return "a name ending in '.' or ' '";
        std::string base = lower_ascii(c.substr(0, c.find('.')));
        while (!base.empty() && base.back() == ' ') base.pop_back();
        static const std::set<std::string> dev = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8",
                                                  "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
        if (dev.count(base)) return "a device name (" + c + ")";
        at = end + 1;
    }
    return {};
}

InstallResult install_archive(const fs::path& archive, const fs::path& mods_root, const InstallOptions& opt) {
    InstallResult r;
    auto refuse = [&r](std::string w) {
        r.errors.push_back(std::move(w));
        return r;
    };
    ZipReader zip;
    std::string why;
    if (!zip.open(archive, &why)) return refuse(why);
    if (zip.entries().size() > opt.max_entries)
        return refuse(std::to_string(zip.entries().size()) + " entries: more than " + std::to_string(opt.max_entries));
    // every entry checked before anything is written
    std::vector<const ZipEntry*> keep;
    std::map<std::string, std::string> files_ci;   // lower case name -> as stored (files)
    std::set<std::string> dirs_ci;                 // lower case folders, given or implied
    std::uint64_t total = 0;
    for (const ZipEntry& e : zip.entries()) {
        const std::string n = normal_name(e.name);
        if (n == "__MACOSX" || n.rfind("__MACOSX/", 0) == 0) continue;   // macOS Finder's resource forks
        if (const std::string bad = unsafe_entry_name(e.name); !bad.empty()) { r.errors.push_back(e.name + ": " + bad); continue; }
        if (e.link()) { r.errors.push_back(e.name + ": a link (not allowed in a mod)"); continue; }
        if (e.flags & 1u) { r.errors.push_back(e.name + ": encrypted (not supported)"); continue; }
        if (e.method != 0 && e.method != 8) { r.errors.push_back(e.name + ": compression method " + std::to_string(e.method) + " (only stored and deflate)"); continue; }
        if (e.usize > opt.max_entry) { r.errors.push_back(e.name + ": " + std::to_string(e.usize) + " bytes, over the limit of " + std::to_string(opt.max_entry)); continue; }
        total += e.usize;
        const std::string l = lower_ascii(n);
        for (std::size_t s = l.find('/'); s != std::string::npos; s = l.find('/', s + 1)) dirs_ci.insert(l.substr(0, s));
        if (e.dir()) { dirs_ci.insert(l); keep.push_back(&e); continue; }
        if (!files_ci.emplace(l, n).second) { r.errors.push_back(n + ": twice in the archive (names that differ only in case are one file on Windows and macOS)"); continue; }
        keep.push_back(&e);
    }
    for (const auto& [l, n] : files_ci)
        if (dirs_ci.count(l)) r.errors.push_back(n + ": both a file and a folder");
    if (total > opt.max_total) r.errors.push_back(std::to_string(total) + " bytes in all, over the limit of " + std::to_string(opt.max_total));
    if (!r.errors.empty()) return r;
    // the manifest: at the top, or in the one top folder
    std::string prefix;
    const ZipEntry* ini = nullptr;
    for (const ZipEntry* e : keep)
        if (!e->dir() && lower_ascii(normal_name(e->name)) == "mod.ini") ini = e;
    if (!ini) {
        std::set<std::string> tops;
        for (const ZipEntry* e : keep) {
            const std::string n = normal_name(e->name);
            tops.insert(lower_ascii(n.substr(0, n.find('/'))));
        }
        if (tops.size() == 1)
            for (const ZipEntry* e : keep) {
                const std::string n = normal_name(e->name);
                const std::size_t s = n.find('/');
                if (!e->dir() && s != std::string::npos && n.find('/', s + 1) == std::string::npos && lower_ascii(n.substr(s + 1)) == "mod.ini") {
                    ini = e;
                    prefix = n.substr(0, s + 1);
                }
            }
    }
    if (!ini) return refuse("no mod.ini at the top of the archive (or in its one top folder): not a mod");
    std::vector<std::uint8_t> text;
    if (!zip.read_all(*ini, 1 << 20, text, &why)) return refuse(why);
    ParsedManifest pm = parse_manifest(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()));
    r.id = pm.m.id;
    r.version = pm.m.version;
    r.name = pm.m.name;
    r.warnings = pm.warnings;
    for (const std::string& e : pm.errors) r.errors.push_back("mod.ini: " + e);
    if (!r.errors.empty()) return r;
    // where it goes
    std::error_code ec;
    fs::create_directories(mods_root, ec);
    if (!fs::is_directory(mods_root, ec)) return refuse("cannot make the mods folder " + path_utf8(mods_root));
    const std::vector<Mod> found = discover(mods_root);
    const Mod* old = find_mod(found, r.id);
    const fs::path target = mods_root / path_from_utf8(r.id);
    if (old && !opt.replace) {
        r.exists = true;
        return refuse("a mod with the id " + r.id + " is installed already (version " + old->manifest.version + ", folder " + old->folder + ")");
    }
    if (fs::exists(target, ec) && !(old && fs::equivalent(old->dir, target, ec)))
        return refuse("the mods folder has a folder named " + r.id + " that is not this mod: move it away first");
    // unpack into a hidden folder, then swap it in
    const fs::path tmp = mods_root / path_from_utf8(".install-" + r.id);
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (ec) return refuse("cannot make " + path_utf8(tmp) + ": " + ec.message());
    for (const ZipEntry* e : keep) {
        std::string n = normal_name(e->name);
        if (n.size() < prefix.size() || n.compare(0, prefix.size(), prefix) != 0) continue;
        n = n.substr(prefix.size());
        if (n.empty()) continue;
        const fs::path dst = tmp / path_from_utf8(n);
        if (e->dir()) {
            fs::create_directories(dst, ec);
            if (ec) { r.errors.push_back("cannot make " + n + ": " + ec.message()); break; }
            continue;
        }
        fs::create_directories(dst.parent_path(), ec);
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out) { r.errors.push_back("cannot write " + n); break; }
        const bool ok = zip.read(*e, opt.max_entry, [&out](const std::uint8_t* p, std::size_t k) {
            out.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(k));
            return bool(out);
        }, &why);
        out.close();
        if (!ok || !out) { r.errors.push_back(ok ? "cannot write " + n : why); break; }
        ++r.files;
        r.bytes += e->usize;
    }
    if (!r.errors.empty()) {
        fs::remove_all(tmp, ec);
        return r;
    }
    fs::path parked;
    if (old) {
        parked = mods_root / path_from_utf8(".remove-" + r.id);
        fs::remove_all(parked, ec);
        fs::rename(old->dir, parked, ec);
        if (ec) {
            fs::remove_all(tmp, ec);
            return refuse("cannot move the installed " + r.id + " aside (a file in it open?): " + ec.message());
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        const std::string m = ec.message();
        if (old) fs::rename(parked, old->dir, ec);
        fs::remove_all(tmp, ec);
        return refuse("cannot move the new mod into place: " + m);
    }
    if (old) {
        fs::remove_all(parked, ec);
        r.replaced = true;
    }
    r.dir = target;
    r.ok = true;
    return r;
}

}  // namespace b3mod
