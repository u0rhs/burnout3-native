// installer.cpp -- see installer.h. The .tar.gz: gzip (RFC 1952, zlib's inflate with its gzip wrapper, which checks
// the members' CRC and length) around a tar archive (POSIX ustar, or GNU tar's own form: its 'L' long names; pax 'x'
// records for path and size). The zip: b3mod::ZipReader.
//
// The swap is a list of steps written to <install>/.update-staging/swap-plan before the first one runs:
//   R <a> <b>   rename the file a to b (b must not exist; b's parent folder made if needed)
//   D <a> <b>   rename the folder a to b (the old previous-version/ to the trash)
//   N <a>       make the folder a (the new previous-version/)
//   C <a>       make the folder a in the install (the update brings a new folder)
// (paths relative to the install folder, '/' separated, UTF-8, tab separated). After each step its number is added to
// .update-staging/swap-done. Undoing walks back from the last step done: the steps in swap-done, and the one after them
// if what is on disk says it ran (its rename made before swap-done was written: "a gone and b there"; every step
// before it ran, so its source can only be gone because it ran). Each step undone adds "u <number>" to swap-done, and
// the one undo that may have run without its line is again told from the disk (only "a gone and b there" is moved
// back), so an undo cut short goes on where it stopped. That is what makes an install (or a roll back) cut short by a
// crash undoable at the next start.
#include "installer.h"

#include "b3mod/archive.h"
#include "b3mod/mods.h"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>
#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

namespace b3launcher::installer {

namespace {

using b3mod::path_from_utf8;
using b3mod::path_utf8;

constexpr const char* kPlanHeader = "burnout3-native swap plan 1";
constexpr const char* kManifestHeader = "burnout3-native update manifest 1";
constexpr const char* kPlan = "swap-plan";              // in .update-staging/
constexpr const char* kDone = "swap-done";              // in .update-staging/: the numbers of the steps done
constexpr const char* kFiles = "files";                 // .update-staging/files/: the unpacked package
constexpr const char* kNewManifest = "update-manifest"; // .update-staging/update-manifest: the next manifest
constexpr const char* kTopPrefix = "burnout3-native-";

bool fail(std::string* why, std::string w) {
    if (why) *why = std::move(w);
    return false;
}

Result refuse(std::string why) {
    Result r;
    r.why = std::move(why);
    return r;
}

std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// '/' separated, no trailing '/'
std::string normal_name(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string join(const std::string& a, const std::string& b) { return a.empty() ? b : a + "/" + b; }

fs::path at(const fs::path& root, const std::string& rel) { return root / path_from_utf8(rel); }

bool exists_nofollow(const fs::path& p) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p, ec));
}
bool is_dir_nofollow(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(fs::symlink_status(p, ec));
}

// a name the updater keeps for itself at the top of the install folder
bool reserved_top(const std::string& rel) {
    const std::string first = lower_ascii(rel.substr(0, rel.find('/')));
    return first == kPrevious || first.rfind(".update", 0) == 0;
}

// rename, never over an existing file (a POSIX rename and libstdc++'s MoveFileEx would replace it)
bool move(const fs::path& root, const std::string& from, const std::string& to, std::string* why) {
    const fs::path a = at(root, from), b = at(root, to);
    if (exists_nofollow(b)) return fail(why, "cannot move " + from + " to " + to + ": something is there already");
    std::error_code ec;
    for (int attempt = 0;; ++attempt) {
        fs::rename(a, b, ec);
        if (!ec) return true;
#ifdef _WIN32
        // a virus scanner or the search indexer opening a new file for a moment: try again
        if (attempt < 20) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
#endif
        std::string m = ec.message();
#ifdef _WIN32
        if (ec == std::errc::device_or_resource_busy) m = "another program has it open";
        else if (ec == std::errc::permission_denied) m = "access denied (another program has it open, or the folder needs administrator rights)";
#endif
        return fail(why, "cannot move " + from + " to " + to + ": " + m);
    }
}

bool write_file(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    f.close();
    return bool(f);
}

// a small text file, whole; false when it is missing or over `limit` bytes
bool read_file(const fs::path& p, std::size_t limit, std::string& out) {
    out.clear();
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(p, ec);
    if (ec || size > limit) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.resize(static_cast<std::size_t>(size));
    f.read(out.data(), static_cast<std::streamsize>(out.size()));
    out.resize(static_cast<std::size_t>(f.gcount()));
    return true;
}

void set_exec(const fs::path& p, bool exec) {
#ifndef _WIN32
    using P = fs::perms;
    const P mode = exec ? (P::owner_all | P::group_read | P::group_exec | P::others_read | P::others_exec)
                        : (P::owner_read | P::owner_write | P::group_read | P::others_read);
    std::error_code ec;
    fs::permissions(p, mode, fs::perm_options::replace, ec);
#else
    (void)p;
    (void)exec;
#endif
}

// "1.1.0+1a2b3c4d" from a release's VERSION.txt ("Burnout 3 native port <version>" on its first line); "" when none
std::string version_in(const fs::path& dir) {
    std::string t;
    read_file(dir / "VERSION.txt", 4096, t);
    t = t.substr(0, t.find('\n'));
    if (!t.empty() && t.back() == '\r') t.pop_back();
    const std::string head = "Burnout 3 native port ";
    if (t.rfind(head, 0) == 0) t = t.substr(head.size());
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (t.size() > 100) t.resize(100);
    for (const char c : t)
        if (static_cast<unsigned char>(c) < 0x20) return {};
    return t;
}

// ---- the package's entries ----

// Every entry of a package goes through add() (before anything of it is written): the checks of installer.h.
struct Checker {
    explicit Checker(const Limits& l) : lim(l) {}
    const Limits& lim;
    std::string top;                                // the top folder, as stored
    std::size_t count = 0;
    std::uint64_t total = 0;
    std::map<std::string, std::string> files_ci;    // lower case -> as stored
    std::set<std::string> dirs_ci;
    std::vector<std::string> files;                 // relative to the top folder

    // "" (rel: the path below the top folder, "" for the top folder itself) or why the entry is refused
    std::string add(const std::string& raw, bool dir, std::uint64_t size, std::string& rel) {
        rel.clear();
        if (++count > lim.max_entries) return "more than " + std::to_string(lim.max_entries) + " entries";
        if (const std::string bad = b3mod::unsafe_entry_name(raw); !bad.empty()) return raw + ": " + bad;
        const std::string n = normal_name(raw);
        if (!dir && raw.back() == '/') return raw + ": a file whose name ends in '/'";
        const std::size_t slash = n.find('/');
        const std::string t = n.substr(0, slash), rest = slash == std::string::npos ? std::string() : n.substr(slash + 1);
        if (top.empty()) {
            if (lower_ascii(t).rfind(kTopPrefix, 0) != 0 || t.size() == std::strlen(kTopPrefix))
                return "not a release package (its files are not in a burnout3-native-<version> folder): " + raw;
            top = t;
        } else if (t != top) {
            return raw + ": outside the package's folder " + top;
        }
        if (rest.empty()) return dir ? std::string() : raw + ": a file beside the package's folder";
        if (reserved_top(rest)) return raw + ": a name the updater keeps for itself";
        if (!dir) {
            if (size > lim.max_file) return raw + ": " + std::to_string(size) + " bytes, over the limit of " + std::to_string(lim.max_file);
            total += size;
            if (total > lim.max_total) return "more than " + std::to_string(lim.max_total) + " bytes in all";
        }
        const std::string l = lower_ascii(rest);
        for (std::size_t s = l.find('/'); s != std::string::npos; s = l.find('/', s + 1))
            if (!add_dir(l.substr(0, s))) return raw + ": a file and a folder of one name";
        if (dir) {
            if (!add_dir(l)) return raw + ": a file and a folder of one name";
        } else {
            if (dirs_ci.count(l)) return raw + ": a file and a folder of one name";
            if (!files_ci.emplace(l, rest).second) return raw + ": twice in the package (names that differ only in case are one file on Windows and macOS)";
            files.push_back(rest);
        }
        rel = rest;
        return {};
    }
    bool add_dir(const std::string& l) {
        if (files_ci.count(l)) return false;
        dirs_ci.insert(l);
        return true;
    }
};

bool make_parent(const fs::path& dst, std::string* why, const std::string& rel) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    if (ec) return fail(why, "cannot make the folder for " + rel + ": " + ec.message());
    return true;
}

std::string unpack_zip(const fs::path& package, const fs::path& dst, Checker& ck) {
    b3mod::ZipReader zip;
    std::string why;
    if (!zip.open(package, &why)) return why;
    if (zip.entries().size() > ck.lim.max_entries)
        return std::to_string(zip.entries().size()) + " entries: more than " + std::to_string(ck.lim.max_entries);
    std::vector<std::pair<const b3mod::ZipEntry*, std::string>> todo;
    for (const b3mod::ZipEntry& e : zip.entries()) {
        if (e.link()) return e.name + ": a link (not allowed in a package)";
        const unsigned host = e.made_by >> 8;
        if (host == 3 || host == 19) {   // Unix, OS X: st_mode in the high half; a plain file or folder only
            const std::uint32_t type = (e.external >> 16) & 0170000u;
            if (type != 0 && type != 0100000u && type != 0040000u) return e.name + ": not a plain file or folder";
        }
        if (e.flags & 1u) return e.name + ": encrypted (not supported)";
        if (e.method != 0 && e.method != 8) return e.name + ": compression method " + std::to_string(e.method) + " (only stored and deflate)";
        std::string rel;
        if (const std::string bad = ck.add(e.name, e.dir(), e.usize, rel); !bad.empty()) return bad;
        if (!rel.empty()) todo.emplace_back(&e, rel);
    }
    if (ck.files.empty()) return "an empty package";
    for (const auto& [e, rel] : todo) {
        const fs::path p = at(dst, rel);
        std::error_code ec;
        if (e->dir()) {
            fs::create_directories(p, ec);
            if (ec) return "cannot make " + rel + ": " + ec.message();
            continue;
        }
        if (!make_parent(p, &why, rel)) return why;
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out) return "cannot write " + rel;
        const bool ok = zip.read(*e, ck.lim.max_file, [&out](const std::uint8_t* d, std::size_t k) {
            out.write(reinterpret_cast<const char*>(d), static_cast<std::streamsize>(k));
            return bool(out);
        }, &why);
        out.close();
        if (!ok) return why;
        if (!out) return "cannot write " + rel;
        const unsigned host = e->made_by >> 8;
        set_exec(p, (host == 3 || host == 19) && ((e->external >> 16) & 0111u) != 0);
    }
    return {};
}

// A gzip stream's inflated bytes (one member or several, back to back)
class GzReader {
public:
    GzReader() = default;
    GzReader(const GzReader&) = delete;
    GzReader& operator=(const GzReader&) = delete;
    ~GzReader() {
        if (init_) inflateEnd(&z_);
    }
    bool open(const fs::path& p, std::string* why) {
        f_.open(p, std::ios::binary);
        if (!f_) return fail(why, "cannot read the package");
        if (inflateInit2(&z_, 15 + 16) != Z_OK) return fail(why, "zlib: inflateInit2 failed");
        init_ = true;
        return true;
    }
    // Up to n bytes; fewer only at the end of the data. -1: damaged (why).
    long long read_some(std::uint8_t* dst, std::size_t n, std::string* why) {
        z_.next_out = dst;
        z_.avail_out = uInt(n);
        while (z_.avail_out > 0 && !end_) {
            if (z_.avail_in == 0 && !refill()) {
                if (!started_member_) { end_ = true; break; }   // only between members: a clean end
                fail(why, "the package ends early (a download cut short?)");
                return -1;
            }
            started_member_ = true;
            const int rc = inflate(&z_, Z_NO_FLUSH);
            if (rc == Z_STREAM_END) {   // a member ends: another may follow
                started_member_ = false;
                inflateReset(&z_);
                continue;
            }
            if (rc == Z_BUF_ERROR && z_.avail_in == 0) continue;
            if (rc != Z_OK) {
                fail(why, "the package's compressed data is damaged");
                return -1;
            }
        }
        return static_cast<long long>(n - z_.avail_out);
    }
    bool read_exact(std::uint8_t* dst, std::size_t n, std::string* why) {
        const long long got = read_some(dst, n, why);
        if (got < 0) return false;
        if (static_cast<std::size_t>(got) != n) return fail(why, "the package ends early (a download cut short?)");
        return true;
    }
    bool skip(std::uint64_t n, std::string* why) {
        std::uint8_t buf[4096];
        while (n > 0) {
            const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(n, sizeof buf));
            if (!read_exact(buf, k, why)) return false;
            n -= k;
        }
        return true;
    }

private:
    bool refill() {
        f_.read(reinterpret_cast<char*>(in_.data()), static_cast<std::streamsize>(in_.size()));
        const std::size_t got = static_cast<std::size_t>(f_.gcount());
        if (got == 0) return false;
        z_.next_in = in_.data();
        z_.avail_in = uInt(got);
        return true;
    }
    std::ifstream f_;
    z_stream z_{};
    bool init_ = false, end_ = false, started_member_ = false;
    std::vector<std::uint8_t> in_ = std::vector<std::uint8_t>(1 << 16);
};

// a tar header's octal number (spaces / NULs around it); false when it is not one
bool tar_octal(const std::uint8_t* p, std::size_t n, std::uint64_t& v) {
    v = 0;
    std::size_t i = 0;
    while (i < n && p[i] == ' ') ++i;
    bool any = false;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; ++i) {
        if (v >> 60) return false;
        v = v * 8 + (p[i] - '0');
        any = true;
    }
    for (; i < n; ++i)
        if (p[i] != ' ' && p[i] != 0) return false;
    return any;
}

std::string tar_field(const std::uint8_t* p, std::size_t n) {
    std::size_t k = 0;
    while (k < n && p[k]) ++k;
    return std::string(reinterpret_cast<const char*>(p), k);
}

// pax records "<length> <key>=<value>\n": path and size taken, the rest ignored
bool pax_records(const std::string& d, std::string& path, std::uint64_t& size, bool& has_size) {
    std::size_t i = 0;
    while (i < d.size()) {
        if (d[i] == 0) break;   // padding
        std::size_t len = 0, j = i;
        while (j < d.size() && d[j] >= '0' && d[j] <= '9') {
            len = len * 10 + std::size_t(d[j] - '0');
            if (len > d.size()) return false;
            ++j;
        }
        if (j == i || j >= d.size() || d[j] != ' ' || len < 5 || i + len > d.size() || d[i + len - 1] != '\n') return false;
        const std::string rec = d.substr(j + 1, i + len - 1 - (j + 1));
        const std::size_t eq = rec.find('=');
        if (eq == std::string::npos) return false;
        const std::string key = rec.substr(0, eq), value = rec.substr(eq + 1);
        if (key == "path") path = value;
        if (key == "size") {
            std::uint64_t v = 0;
            if (value.empty() || value.size() > 18) return false;
            for (const char c : value) {
                if (c < '0' || c > '9') return false;
                v = v * 10 + std::uint64_t(c - '0');
            }
            size = v;
            has_size = true;
        }
        i += len;
    }
    return true;
}

std::string unpack_tgz(const fs::path& package, const fs::path& dst, Checker& ck) {
    GzReader gz;
    std::string why;
    if (!gz.open(package, &why)) return why;
    std::uint8_t h[512];
    std::string long_name, pax_path;
    std::uint64_t pax_size = 0;
    bool has_pax_size = false;
    std::vector<std::uint8_t> buf(1 << 16);
    for (;;) {
        if (!gz.read_exact(h, sizeof h, &why)) return why;
        if (std::all_of(h, h + sizeof h, [](std::uint8_t b) { return b == 0; })) break;   // the end
        std::uint64_t sum = 0, want = 0;
        for (std::size_t i = 0; i < sizeof h; ++i) sum += (i >= 148 && i < 156) ? ' ' : h[i];
        if (!tar_octal(h + 148, 8, want) || want != sum) return "the package is damaged (a tar header's checksum)";
        const bool posix = std::memcmp(h + 257, "ustar\0" "00", 8) == 0;
        const bool gnu = std::memcmp(h + 257, "ustar  \0", 8) == 0;
        if (!posix && !gnu) return "not a ustar archive";
        std::uint64_t size = 0, mode = 0;
        if (h[124] & 0x80) return "an entry over 8 GB";
        if (!tar_octal(h + 124, 12, size)) return "the package is damaged (a tar header's size)";
        if (!tar_octal(h + 100, 8, mode)) mode = 0644;
        const char type = static_cast<char>(h[156]);
        const std::uint64_t pad = (512 - size % 512) % 512;
        if (type == 'L' || type == 'x' || type == 'g') {   // the next entry's long name; pax records
            if (size > (1u << 20)) return "the package is damaged (a long name or pax record over 1 MB)";
            std::string d(static_cast<std::size_t>(size), '\0');
            if (!gz.read_exact(reinterpret_cast<std::uint8_t*>(d.data()), d.size(), &why) || !gz.skip(pad, &why)) return why;
            if (type == 'L') {
                long_name = d.substr(0, d.find('\0'));
                if (long_name.empty()) return "the package is damaged (an empty long name)";
            } else if (type == 'x') {
                if (!pax_records(d, pax_path, pax_size, has_pax_size)) return "the package is damaged (a pax record)";
            }
            continue;   // 'g' (global records): nothing the updater needs
        }
        std::string name = tar_field(h, 100);
        if (posix && h[345]) name = tar_field(h + 345, 155) + "/" + name;
        if (!long_name.empty()) name = long_name;
        if (!pax_path.empty()) name = pax_path;
        if (has_pax_size) size = pax_size;
        long_name.clear();
        pax_path.clear();
        has_pax_size = false;
        const std::uint64_t data_pad = (512 - size % 512) % 512;
        const bool file = type == '0' || type == '\0';
        if (!file && type != '5') {
            const char* what = type == '1' ? "a hard link" : type == '2' ? "a symbolic link" : type == '3' ? "a character device"
                             : type == '4' ? "a block device" : type == '6' ? "a FIFO" : type == 'K' ? "a link (long link name)"
                             : type == 'S' ? "a sparse file" : "not a plain file or folder";
            return (name.empty() ? std::string("an entry") : name) + ": " + what + " (not allowed in a package)";
        }
        if (name.empty()) return "the package is damaged (an entry without a name)";
        std::string rel;
        if (const std::string bad = ck.add(name, type == '5', file ? size : 0, rel); !bad.empty()) return bad;
        const fs::path p = at(dst, rel);
        std::error_code ec;
        if (type == '5') {
            if (!rel.empty()) {
                fs::create_directories(p, ec);
                if (ec) return "cannot make " + rel + ": " + ec.message();
            }
            if (!gz.skip(size + data_pad, &why)) return why;
            continue;
        }
        if (!make_parent(p, &why, rel)) return why;
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out) return "cannot write " + rel;
        for (std::uint64_t left = size; left > 0;) {
            const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(left, buf.size()));
            if (!gz.read_exact(buf.data(), k, &why)) return why;
            out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(k));
            left -= k;
        }
        out.close();
        if (!out) return "cannot write " + rel;
        if (!gz.skip(data_pad, &why)) return why;
        set_exec(p, (mode & 0111u) != 0);
    }
    // the rest (tar's zero records up to its record size) read too, so the gzip trailer's CRC and length are checked
    for (std::uint64_t rest = 0;;) {
        const long long got = gz.read_some(buf.data(), buf.size(), &why);
        if (got < 0) return why;
        if (got == 0) break;
        rest += static_cast<std::uint64_t>(got);
        if (rest > (4u << 20)) return "the package has more than 4 MB after the archive's end";
    }
    if (ck.files.empty()) return "an empty package";
    return {};
}

// ---- the plan ----

struct Step {
    char op = 'R';
    std::string a, b;
};

std::string plan_text(const std::vector<Step>& steps) {
    std::string t = std::string(kPlanHeader) + "\n";
    for (const Step& s : steps) {
        t += s.op;
        t += '\t' + s.a;
        if (s.op == 'R' || s.op == 'D') t += '\t' + s.b;
        t += '\n';
    }
    return t;
}

bool parse_plan(const std::string& text, std::vector<Step>& steps) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != kPlanHeader) return false;
    while (std::getline(in, line)) {
        if (line.size() < 3 || line[1] != '\t') return false;
        Step s;
        s.op = line[0];
        const std::string rest = line.substr(2);
        const std::size_t tab = rest.find('\t');
        const bool two = s.op == 'R' || s.op == 'D';
        if (!two && s.op != 'N' && s.op != 'C') return false;
        if (two != (tab != std::string::npos)) return false;
        s.a = two ? rest.substr(0, tab) : rest;
        if (two) s.b = rest.substr(tab + 1);
        if (!b3mod::unsafe_entry_name(s.a).empty() || (two && !b3mod::unsafe_entry_name(s.b).empty())) return false;
        steps.push_back(std::move(s));
    }
    return true;
}

bool run_step(const fs::path& root, const Step& s, std::string* why) {
    std::error_code ec;
    switch (s.op) {
    case 'R':
    case 'D':
        if (!exists_nofollow(at(root, s.a))) return fail(why, s.a + " is gone");
        fs::create_directories(at(root, s.b).parent_path(), ec);
        return move(root, s.a, s.b, why);
    case 'N':
    case 'C':
        if (s.op == 'C' && is_dir_nofollow(at(root, s.a))) return true;
        fs::create_directory(at(root, s.a), ec);
        if (ec) return fail(why, "cannot make the folder " + s.a + ": " + ec.message());
        return true;
    }
    return fail(why, "a bad step");
}

// a free name in the trash: .update-trash/<base>, <base>-2 ...
std::string trash_name(const fs::path& root, const std::string& base) {
    std::string n = std::string(kTrash) + "/" + base;
    for (int k = 2; exists_nofollow(at(root, n)); ++k) n = std::string(kTrash) + "/" + base + "-" + std::to_string(k);
    return n;
}

// The step log (swap-done): how many steps ran, which ones were undone since
struct Log {
    std::size_t done = 0;
    std::set<std::size_t> undone;
};
Log read_log(const fs::path& root) {
    Log g;
    std::string text, line;
    read_file(root / kStaging / kDone, 64u << 20, text);   // none: no step done
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (in.eof()) break;   // a last line without its '\n': cut short while written
        if (line.rfind("u ", 0) == 0) g.undone.insert(static_cast<std::size_t>(std::strtoull(line.c_str() + 2, nullptr, 10)));
        else ++g.done;
    }
    return g;
}

// Undoes the first `g.done` steps and, when what is on disk says so, the one after them (back to front), but not the
// ones `g.undone` lists. false: something could not be put back (why); the plan stays for another try then.
bool undo(const fs::path& root, const std::vector<Step>& steps, const Log& g, std::string* why) {
    bool ok = true;
    std::error_code ec;
    std::ofstream log(root / kStaging / kDone, std::ios::binary | std::ios::app);
    // the new previous-version/ is there only while the old one is in the trash (D not undone yet; before D ran, or
    // once its undo ran, previous-version/ is the old one)
    bool new_pv = true;
    for (const Step& s : steps)
        if (s.op == 'D') new_pv = exists_nofollow(at(root, s.b));
    for (std::size_t i = std::min(g.done + 1, steps.size()); ok && i-- > 0;) {
        if (g.undone.count(i)) continue;
        const Step* s = &steps[i];
        const fs::path a = at(root, s->a);
        switch (s->op) {
        case 'R':
            if (!exists_nofollow(a) && exists_nofollow(at(root, s->b))) {
                fs::create_directories(a.parent_path(), ec);
                std::string w;
                if (!move(root, s->b, s->a, &w)) {
                    fail(why, w);
                    ok = false;
                }
            }
            break;
        case 'C':   // made for the update: removed when empty again
            if (is_dir_nofollow(a) && fs::is_empty(a, ec)) fs::remove(a, ec);
            break;
        case 'N':   // the new previous-version/ (only folders left in it now) to the trash
            if (ok && new_pv && is_dir_nofollow(a)) {
                fs::create_directories(root / kTrash, ec);
                std::string w;
                if (!move(root, s->a, trash_name(root, "previous-version-new"), &w)) {
                    fail(why, w);
                    ok = false;
                }
            }
            break;
        case 'D':   // the old previous-version/ back
            if (ok && exists_nofollow(at(root, s->b))) {
                std::string w;
                if (!move(root, s->b, s->a, &w)) {
                    fail(why, w);
                    ok = false;
                }
            }
            break;
        }
        if (ok) {
            log << "u " << i << '\n';
            log.flush();
        }
    }
    return ok;
}

// .update-staging to the trash, the trash deleted (both paths made here, inside the install folder)
void discard(const fs::path& root) {
    std::error_code ec;
    if (exists_nofollow(root / kStaging)) {
        fs::create_directories(root / kTrash, ec);
        std::string w;
        if (!move(root, kStaging, trash_name(root, "staging"), &w)) return;   // tried again at the next start
    }
    fs::remove_all(root / kTrash, ec);
}

// the empty folders below `dir` removed (not `dir` itself; links not followed)
void prune_below(const fs::path& dir) {
    std::error_code ec;
    std::vector<fs::path> subs;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (is_dir_nofollow(it->path())) subs.push_back(it->path());
    for (const fs::path& s : subs) {
        prune_below(s);
        if (fs::is_empty(s, ec)) fs::remove(s, ec);
    }
}

// After a swap: no empty folders left in previous-version/, nor in the install where files went to previous-version/
// and nothing came back (a folder the update brought, after a roll back)
void prune(const fs::path& root, const std::vector<Step>& steps) {
    std::error_code ec;
    const std::string pv = std::string(kPrevious) + "/";
    for (const Step& s : steps) {
        if (s.op != 'R' || s.b.rfind(pv, 0) != 0 || s.a.rfind(pv, 0) == 0 || s.a.rfind(kStaging, 0) == 0) continue;
        if (exists_nofollow(at(root, s.a))) continue;
        for (std::string d = s.a; d.find('/') != std::string::npos;) {
            d = d.substr(0, d.rfind('/'));
            const fs::path p = at(root, d);
            if (!is_dir_nofollow(p) || !fs::is_empty(p, ec) || ec) break;
            fs::remove(p, ec);
        }
    }
    prune_below(root / kPrevious);
}

Result run_plan(const fs::path& root, const std::vector<Step>& steps, std::size_t files, const std::string& what) {
    const fs::path plan = root / kStaging / kPlan, tmp = root / kStaging / (std::string(kPlan) + ".tmp");
    std::error_code ec;
    if (!write_file(tmp, plan_text(steps))) {
        discard(root);
        return refuse("cannot write " + std::string(kStaging) + "/" + kPlan);
    }
    fs::rename(tmp, plan, ec);
    if (ec) {
        discard(root);
        return refuse("cannot write " + std::string(kStaging) + "/" + kPlan + ": " + ec.message());
    }
    std::string why;
    bool ok = true;
    std::size_t done = 0;
    std::ofstream log(root / kStaging / kDone, std::ios::binary | std::ios::trunc);
    if (!log) {
        discard(root);
        return refuse("cannot write " + std::string(kStaging) + "/" + kDone);
    }
    for (const Step& s : steps) {
        if (!run_step(root, s, &why)) {
            ok = false;
            break;
        }
        log << done++ << '\n';
        log.flush();
        if (!log) {   // without the log a crash could not be undone: stop here
            ok = false;
            why = "cannot write " + std::string(kStaging) + "/" + kDone;
            break;
        }
    }
    log.close();
    if (ok) {
        fs::remove(plan, ec);   // the commit: from here on the swap stays
        if (ec || exists_nofollow(plan)) {
            ok = false;
            why = "cannot remove " + std::string(kStaging) + "/" + kPlan + ": " + ec.message();
        }
    }
    if (!ok) {
        std::string w;
        Log g;
        g.done = done;
        if (!undo(root, steps, g, &w))
            return refuse(what + " failed: " + why + "; and not everything could be put back (" + w +
                          "): the launcher tries again at its next start");
        fs::remove(plan, ec);
        discard(root);
        return refuse(what + " failed, nothing was changed: " + why);
    }
    prune(root, steps);
    discard(root);
    Result r;
    r.ok = true;
    r.files = files;
    return r;
}

// ---- the manifest (previous-version/.update-manifest) ----
//   burnout3-native update manifest 1
//   installed <version>
//   previous <version>
//   <P><I> <relative path>     P: previous-version/ has the file, I: the install has it (1 / 0)

struct Entry {
    std::string rel;
    bool prev = false, inst = false;
};
struct Manifest {
    std::string installed, previous;
    std::vector<Entry> entries;
};

std::string manifest_text(const Manifest& m) {
    std::string t = std::string(kManifestHeader) + "\ninstalled " + m.installed + "\nprevious " + m.previous + "\n";
    for (const Entry& e : m.entries) t += std::string(e.prev ? "1" : "0") + (e.inst ? "1" : "0") + " " + e.rel + "\n";
    return t;
}

bool read_manifest(const fs::path& root, Manifest& m) {
    std::string text;
    if (!read_file(root / kPrevious / kManifest, 16u << 20, text)) return false;
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != kManifestHeader) return false;
    if (!std::getline(in, line) || line.rfind("installed ", 0) != 0) return false;
    m.installed = line.substr(10);
    if (!std::getline(in, line) || line.rfind("previous ", 0) != 0) return false;
    m.previous = line.substr(9);
    std::set<std::string> seen;
    while (std::getline(in, line)) {
        if (line.size() < 4 || (line[0] != '0' && line[0] != '1') || (line[1] != '0' && line[1] != '1') || line[2] != ' ') return false;
        Entry e;
        e.prev = line[0] == '1';
        e.inst = line[1] == '1';
        e.rel = line.substr(3);
        if (!b3mod::unsafe_entry_name(e.rel).empty() || e.rel.back() == '/' || reserved_top(e.rel)) return false;
        if (!seen.insert(lower_ascii(e.rel)).second) return false;
        m.entries.push_back(std::move(e));
    }
    return !m.entries.empty();
}

bool complete(const fs::path& root, const Manifest& m) {
    for (const Entry& e : m.entries) {
        const fs::path p = at(root / kPrevious, e.rel);
        if (e.prev && (!exists_nofollow(p) || is_dir_nofollow(p))) return false;
    }
    return true;
}

// the folders above `rel` the install lacks (added to `need`); "" or why one cannot be made (a file has its name)
std::string folders_for(const fs::path& root, const std::string& rel, std::set<std::string>& need) {
    for (std::size_t s = rel.find('/'); s != std::string::npos; s = rel.find('/', s + 1)) {
        const std::string d = rel.substr(0, s);
        std::error_code ec;
        const fs::file_status st = fs::status(at(root, d), ec);
        if (!fs::exists(st)) need.insert(d);
        else if (!fs::is_directory(st)) return d + ": the install folder has a file there, the update needs a folder";
    }
    return {};
}

bool begin(const fs::path& root, std::string* why) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return fail(why, "the install folder " + path_utf8(root) + " is not there");
    clean_leftovers(root);
    if (exists_nofollow(root / kStaging))
        return fail(why, "an earlier update was cut short and could not be undone yet (" + std::string(kStaging) + " is still there)");
    fs::create_directories(root / kStaging / kFiles, ec);
    if (ec) return fail(why, "cannot make " + std::string(kStaging) + ": " + ec.message());
    return true;
}

}  // namespace

Result install(const fs::path& package, const fs::path& install_dir, const Limits& limits) {
    const fs::path& root = install_dir;
    std::string why;
    std::uint8_t magic[4] = {};
    {
        std::ifstream f(package, std::ios::binary);
        if (!f) return refuse("cannot read the package " + path_utf8(package));
        f.read(reinterpret_cast<char*>(magic), sizeof magic);
    }
    const bool zip = magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4;
    const bool tgz = magic[0] == 0x1f && magic[1] == 0x8b;
    if (!zip && !tgz) return refuse("not a release package (a .zip or .tar.gz)");
    if (!begin(root, &why)) return refuse(why);
    const fs::path staged = root / kStaging / kFiles;
    Checker ck(limits);
    const std::string bad = zip ? unpack_zip(package, staged, ck) : unpack_tgz(package, staged, ck);
    if (!bad.empty()) {
        discard(root);
        return refuse(bad);
    }
    std::vector<std::string> files = ck.files;
    std::sort(files.begin(), files.end());
    // what the install folder has where the package's files go
    std::set<std::string> need;
    for (const std::string& rel : files) {
        if (is_dir_nofollow(at(root, rel))) {
            discard(root);
            return refuse(rel + ": the install folder has a folder there, the update has a file");
        }
        if (const std::string w = folders_for(root, rel, need); !w.empty()) {
            discard(root);
            return refuse(w);
        }
    }
    const fs::path pv = root / kPrevious;
    if (exists_nofollow(pv) && !is_dir_nofollow(pv)) {
        discard(root);
        return refuse(std::string(kPrevious) + " in the install folder is not a folder");
    }
    Manifest m;
    m.installed = version_in(staged);
    if (m.installed.empty()) m.installed = ck.top.substr(std::strlen(kTopPrefix));
    m.previous = version_in(root);
    if (m.previous.empty()) m.previous = "unknown";
    std::vector<Step> steps;
    if (exists_nofollow(pv)) steps.push_back({'D', kPrevious, trash_name(root, "previous-version")});
    steps.push_back({'N', kPrevious, {}});
    for (const std::string& d : need) steps.push_back({'C', d, {}});   // a set: a folder before the ones in it
    const std::string pv_rel = kPrevious, st_rel = std::string(kStaging) + "/" + kFiles;
    for (const std::string& rel : files) {
        const bool had = exists_nofollow(at(root, rel));
        if (had) steps.push_back({'R', rel, join(pv_rel, rel)});
        steps.push_back({'R', join(st_rel, rel), rel});
        m.entries.push_back({rel, had, true});
    }
    if (!write_file(root / kStaging / kNewManifest, manifest_text(m))) {
        discard(root);
        return refuse("cannot write " + std::string(kStaging) + "/" + kNewManifest);
    }
    steps.push_back({'R', std::string(kStaging) + "/" + kNewManifest, join(pv_rel, kManifest)});
    return run_plan(root, steps, files.size(), "the update");
}

bool can_roll_back(const fs::path& install_dir) {
    Manifest m;
    return read_manifest(install_dir, m) && complete(install_dir, m);
}

std::string previous_version(const fs::path& install_dir) {
    Manifest m;
    if (!read_manifest(install_dir, m) || !complete(install_dir, m)) return {};
    return m.previous.empty() ? "unknown" : m.previous;
}

Result roll_back(const fs::path& install_dir) {
    const fs::path& root = install_dir;
    std::string why;
    {
        Manifest m;
        std::error_code ec;
        if (fs::is_directory(root, ec)) clean_leftovers(root);
        if (!read_manifest(root, m) || !complete(root, m)) return refuse("there is no complete earlier version to go back to");
    }
    if (!begin(root, &why)) return refuse(why);
    Manifest m, n;
    if (!read_manifest(root, m) || !complete(root, m)) {
        discard(root);
        return refuse("there is no complete earlier version to go back to");
    }
    n.installed = m.previous;
    n.previous = m.installed;
    std::set<std::string> need;
    std::vector<Step> moves;
    std::size_t files = 0;
    const std::string pv_rel = kPrevious, park = std::string(kStaging) + "/" + kFiles;
    for (const Entry& e : m.entries) {
        const fs::path p = at(root, e.rel);
        if (is_dir_nofollow(p)) {
            discard(root);
            return refuse(e.rel + ": the install folder has a folder there");
        }
        const bool inst = exists_nofollow(p);   // what is there now (a file the player removed: nothing to move)
        if (e.prev)
            if (const std::string w = folders_for(root, e.rel, need); !w.empty()) {
                discard(root);
                return refuse(w);
            }
        if (e.prev && inst) {
            moves.push_back({'R', e.rel, join(park, e.rel)});
            moves.push_back({'R', join(pv_rel, e.rel), e.rel});
            moves.push_back({'R', join(park, e.rel), join(pv_rel, e.rel)});
        } else if (inst) {
            moves.push_back({'R', e.rel, join(pv_rel, e.rel)});
        } else if (e.prev) {
            moves.push_back({'R', join(pv_rel, e.rel), e.rel});
        }
        if (e.prev || inst) ++files;
        n.entries.push_back({e.rel, inst, e.prev});
    }
    std::vector<Step> steps;
    for (const std::string& d : need) steps.push_back({'C', d, {}});
    steps.insert(steps.end(), moves.begin(), moves.end());
    if (!write_file(root / kStaging / kNewManifest, manifest_text(n))) {
        discard(root);
        return refuse("cannot write " + std::string(kStaging) + "/" + kNewManifest);
    }
    steps.push_back({'R', join(pv_rel, kManifest), std::string(kStaging) + "/old-manifest"});
    steps.push_back({'R', std::string(kStaging) + "/" + kNewManifest, join(pv_rel, kManifest)});
    return run_plan(root, steps, files, "going back");
}

void clean_leftovers(const fs::path& install_dir) {
    const fs::path& root = install_dir;
    const fs::path plan = root / kStaging / kPlan;
    std::error_code ec;
    if (exists_nofollow(plan)) {   // a swap cut short: undone first
        std::string text, why;
        std::vector<Step> steps;
        if (!read_file(plan, 64u << 20, text) || !parse_plan(text, steps)) return;   // kept: nothing removed
        if (!undo(root, steps, read_log(root), &why)) return;                       // kept for the next try
        fs::remove(plan, ec);
        if (exists_nofollow(plan)) return;
    }
    discard(root);
}

}  // namespace b3launcher::installer
