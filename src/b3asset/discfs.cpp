// discfs.cpp -- see include/b3asset/discfs.h. ISO 9660 [ECMA-119]: the volume descriptors from logical sector 16
// (type 1 = the primary one: "CD001", the logical block size at +128, the root directory record at +156); a directory
// record: +0 length, +2 extent LBA (LE), +10 data length (LE), +25 flags (bit 1 directory, bit 7 not the file's last
// extent), +32 name length, +33 name ("NAME.EXT;1"; 0x00 / 0x01 = "." / ".."); records never cross a sector (a zero
// length byte: the rest of the sector is padding). A raw sector (2352 bytes) is sync 00 FF x10 00, header (mode at +15),
// then the 2048 data bytes at +16 (mode 1) or, after the 8-byte subheader, +24 (mode 2 form 1).
#include "b3asset/discfs.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace b3asset::disc {

namespace fs = std::filesystem;

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}
std::uint32_t le32(const std::uint8_t* p) { return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }

// Paths as UTF-8 with '/' separators (on Windows, from the wide path), and back
std::string u8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}
fs::path from_u8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string normal(const fs::path& p) {
    std::string s = u8(p.lexically_normal());
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}
std::vector<std::string> split(const std::string& rel) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : rel) {
        if (c == '/' || c == '\\') {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

// ---- the mod overlay (set_overlay): immutable once set; replaced as a whole ----
struct OverlayState {
    std::string root;                                   // normal() of the root
    std::unordered_map<std::string, fs::path> files;    // key -> host file
    std::set<std::string> dirs;                         // keys of the folders that hold an overlay file ("" the root)
    std::map<std::string, std::vector<std::pair<std::string, bool>>> kids;   // folder key -> (name, folder)
};
std::atomic<const OverlayState*> g_overlay{nullptr};

char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

// The overlay's key of a path below its root (false: no overlay, or not below it)
const OverlayState* overlay_key(const fs::path& p, std::string& key) {
    const OverlayState* o = g_overlay.load(std::memory_order_acquire);
    if (!o) return nullptr;
    const std::string s = normal(p);
    if (s == o->root) {
        key.clear();
        return o;
    }
    if (s.size() <= o->root.size() + 1 || s.compare(0, o->root.size(), o->root) != 0 || s[o->root.size()] != '/') return nullptr;
    key = s.substr(o->root.size() + 1);
    for (char& c : key) c = upper(c);
    return o;
}
const fs::path* overlay_file(const fs::path& p) {
    std::string key;
    const OverlayState* o = overlay_key(p, key);
    if (!o) return nullptr;
    const auto it = o->files.find(key);
    return it == o->files.end() ? nullptr : &it->second;
}
bool overlay_dir(const fs::path& p) {
    std::string key;
    const OverlayState* o = overlay_key(p, key);
    return o && o->dirs.count(key);
}

// ---- a host file, read at offsets (no shared position: any thread) ----
struct HostFile {
#ifdef _WIN32
    HANDLE h = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
    std::uint64_t size = 0;
    HostFile() = default;
    HostFile(const HostFile&) = delete;
    HostFile& operator=(const HostFile&) = delete;
    ~HostFile() {
#ifdef _WIN32
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
#else
        if (fd >= 0) ::close(fd);
#endif
    }
    bool open(const fs::path& p) {
#ifdef _WIN32
        h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER n;
        if (GetFileType(h) != FILE_TYPE_DISK || !GetFileSizeEx(h, &n) || n.QuadPart < 0) return false;
        size = static_cast<std::uint64_t>(n.QuadPart);
        return true;
#else
        fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        struct stat st;
        if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) return false;
        size = static_cast<std::uint64_t>(st.st_size);
        return true;
#endif
    }
    // up to n bytes at off: the count (short only at the end of the file), -1 on an error
    std::int64_t read(std::uint64_t off, void* dst, std::size_t n) const {
        std::size_t got = 0;
        auto* d = static_cast<std::uint8_t*>(dst);
        while (got < n) {
#ifdef _WIN32
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(n - got, 1u << 30));
            OVERLAPPED ov{};
            const std::uint64_t at = off + got;
            ov.Offset = static_cast<DWORD>(at);
            ov.OffsetHigh = static_cast<DWORD>(at >> 32);
            DWORD r = 0;
            if (!ReadFile(h, d + got, chunk, &r, &ov)) {
                if (GetLastError() == ERROR_HANDLE_EOF) break;
                return -1;
            }
#else
            const ssize_t r = ::pread(fd, d + got, std::min<std::size_t>(n - got, std::size_t(1) << 30), static_cast<off_t>(off + got));
            if (r < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
#endif
            if (r == 0) break;
            got += static_cast<std::size_t>(r);
        }
        return static_cast<std::int64_t>(got);
    }
};

// ---- an ISO 9660 image ----
struct Node {
    std::string name;   // without ";1" (and a file's trailing '.')
    bool dir = false;
    std::uint64_t size = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> extents;   // LBA, bytes
    std::vector<std::uint32_t> kids;
};

struct Image {
    std::string path;
    HostFile file;
    std::uint32_t stride = 2048, data_off = 0;   // physical sector size, the data's offset in it
    std::vector<Node> nodes;                     // [0] the root

    // `n` bytes of the 2048-byte logical sectors at byte `off`: the count, -1 on an error
    std::int64_t read(std::uint64_t off, void* dst, std::size_t n) const {
        if (stride == 2048) return file.read(off, dst, n);
        std::size_t got = 0;
        auto* d = static_cast<std::uint8_t*>(dst);
        while (got < n) {
            const std::uint64_t at = off + got, sector = at / 2048, in = at % 2048;
            const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(2048 - in, n - got));
            const std::int64_t r = file.read(sector * stride + data_off + in, d + got, k);
            if (r < 0) return -1;
            got += static_cast<std::size_t>(r);
            if (static_cast<std::size_t>(r) < k) break;
        }
        return static_cast<std::int64_t>(got);
    }
    bool read_exact(std::uint64_t off, void* dst, std::size_t n) const { return read(off, dst, n) == static_cast<std::int64_t>(n); }

    // the primary volume descriptor's layout: false when this is no ISO 9660 image
    bool probe(std::uint8_t pvd[2048]) {
        static const std::uint8_t sync[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
        std::uint8_t raw[16];
        if (file.size >= 17ull * 2352 && file.read(16ull * 2352, raw, 16) == 16 && std::memcmp(raw, sync, 12) == 0 &&
            (raw[15] == 1 || raw[15] == 2)) {
            stride = 2352;
            data_off = raw[15] == 1 ? 16 : 24;
        }
        for (std::uint32_t s = 16; s < 64; ++s) {
            if (!read_exact(std::uint64_t(s) * 2048, pvd, 2048) || std::memcmp(pvd + 1, "CD001", 5) != 0) return false;
            if (pvd[0] == 1) return (pvd[128] | pvd[129] << 8) == 2048;
            if (pvd[0] == 255) return false;
        }
        return false;
    }

    // the directory tree from the root record; false on a malformed image
    bool load_tree(const std::uint8_t* root_rec) {
        nodes.clear();
        Node root;
        root.dir = true;
        root.extents.push_back({le32(root_rec + 2), le32(root_rec + 10)});
        nodes.push_back(root);
        std::vector<std::uint32_t> todo{0};
        std::vector<std::uint32_t> seen;   // directory LBAs (a loop is malformed)
        while (!todo.empty()) {
            const std::uint32_t at = todo.back();
            todo.pop_back();
            const auto [lba, bytes] = nodes[at].extents.front();
            if (std::find(seen.begin(), seen.end(), lba) != seen.end()) return false;
            seen.push_back(lba);
            if (bytes > (64u << 20)) return false;
            std::vector<std::uint8_t> d((bytes + 2047u) & ~2047u);
            if (!read_exact(std::uint64_t(lba) * 2048, d.data(), d.size())) return false;
            std::int64_t multi = -1;   // the node a multi-extent file continues in
            for (std::size_t pos = 0; pos < bytes;) {
                const std::uint8_t len = d[pos];
                if (len == 0) { pos = (pos / 2048 + 1) * 2048; continue; }
                if (len < 34 || pos % 2048 + len > 2048) return false;
                const std::uint8_t* r = d.data() + pos;
                pos += len;
                const std::uint8_t nlen = r[32];
                if (33u + nlen > len) return false;
                if (nlen == 1 && (r[33] == 0 || r[33] == 1)) continue;   // "." / ".."
                std::string name(reinterpret_cast<const char*>(r + 33), nlen);
                const bool dir = (r[25] & 2) != 0;
                if (const auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
                if (!dir && !name.empty() && name.back() == '.') name.pop_back();
                if (name.empty()) continue;
                const std::uint32_t xl = le32(r + 2), xb = le32(r + 10);
                if (multi >= 0 && !dir && nodes[std::size_t(multi)].name == name) {   // the file's next extent
                    Node& m = nodes[std::size_t(multi)];
                    m.extents.push_back({xl, xb});
                    m.size += xb;
                } else {
                    if (nodes.size() >= (1u << 20)) return false;
                    Node n;
                    n.name = std::move(name);
                    n.dir = dir;
                    n.size = dir ? 0 : xb;
                    n.extents.push_back({xl, xb});
                    nodes.push_back(std::move(n));
                    const auto k = static_cast<std::uint32_t>(nodes.size() - 1);
                    nodes[at].kids.push_back(k);
                    if (dir) todo.push_back(k);
                    multi = k;
                }
                if (!(r[25] & 0x80)) multi = -1;
            }
        }
        return true;
    }

    // the node of `rel` below `from` (case-insensitive); -1: not there or ambiguous
    std::int64_t find(std::uint32_t from, const std::vector<std::string>& comps, std::size_t first = 0) const {
        std::uint32_t cur = from;
        for (std::size_t c = first; c < comps.size(); ++c) {
            const std::string& comp = comps[c];
            if (comp == ".") continue;
            if (comp == ".." || !nodes[cur].dir) return -1;
            std::int64_t hit = -1;
            for (const std::uint32_t k : nodes[cur].kids) {
                if (!ieq(nodes[k].name, comp)) continue;
                if (hit >= 0) return -1;   // two names that differ only in case
                hit = k;
            }
            if (hit < 0) return -1;
            cur = static_cast<std::uint32_t>(hit);
        }
        return cur;
    }
};

std::mutex g_m;
std::map<std::string, std::shared_ptr<const Image>> g_images;   // by the image's normal path

// The image at `p` (a regular file), mounted once; null when it is no ISO 9660 image
std::shared_ptr<const Image> mount(const fs::path& p) {
    const std::string key = normal(p);
    {
        std::lock_guard<std::mutex> l(g_m);
        if (const auto it = g_images.find(key); it != g_images.end()) return it->second;
    }
    auto img = std::make_shared<Image>();
    img->path = key;
    std::uint8_t pvd[2048];
    if (!img->file.open(p) || !img->probe(pvd) || !img->load_tree(pvd + 156)) return nullptr;
    std::lock_guard<std::mutex> l(g_m);
    return g_images.emplace(key, std::move(img)).first->second;   // another thread's, if it was first
}

struct Loc {
    enum Kind { kNone, kHost, kImage } kind = kNone;
    std::shared_ptr<const Image> img;
    std::uint32_t node = 0;
};
// Where `p` is: a host path that exists, a node in an image, or nothing. `root_as_dir`: an existing host file that is
// an image is that image's root (else the host file).
Loc locate(const fs::path& p, bool root_as_dir) {
    Loc out;
    if (p.empty()) return out;
    const std::string s = normal(p);
    auto in_image = [&](const std::shared_ptr<const Image>& img, const std::string& rest) {
        const std::int64_t n = img->find(0, split(rest));
        if (n >= 0) {
            out.kind = Loc::kImage;
            out.img = img;
            out.node = static_cast<std::uint32_t>(n);
        }
        return out;
    };
    {   // an image already open
        std::shared_ptr<const Image> hit;
        std::string rest;
        {
            std::lock_guard<std::mutex> l(g_m);
            for (const auto& [key, img] : g_images) {
                if (s.size() > key.size() && s.compare(0, key.size(), key) == 0 && s[key.size()] == '/') {
                    hit = img;
                    rest = s.substr(key.size() + 1);
                    break;
                }
                if (s == key && root_as_dir) { hit = img; break; }
            }
        }
        if (hit) return in_image(hit, rest);
    }
    std::error_code ec;
    const fs::file_status st = fs::status(p, ec);
    if (!ec && fs::exists(st)) {
        if (root_as_dir && fs::is_regular_file(st))
            if (auto img = mount(p)) return in_image(img, "");
        out.kind = Loc::kHost;
        return out;
    }
    // a path below an image file not opened yet: the first prefix that is a file
    for (std::size_t i = s.find('/', 1); i != std::string::npos; i = s.find('/', i + 1)) {
        const fs::path pre = from_u8(s.substr(0, i));
        const fs::file_status ps = fs::status(pre, ec);
        if (ec || !fs::exists(ps) || fs::is_directory(ps)) continue;   // (not there: a UNC "//server" prefix goes on)
        if (!fs::is_regular_file(ps)) return out;
        if (auto img = mount(pre)) return in_image(img, s.substr(i + 1));
        return out;
    }
    return out;
}

}  // namespace

// ---- files ----
struct File::Impl {
    std::string name;
    std::unique_ptr<HostFile> host;                                     // a host file, or
    std::shared_ptr<const Image> img;                                   // a file in this image
    std::vector<std::pair<std::uint32_t, std::uint32_t>> extents;       // (its LBA, bytes)
    std::uint64_t size = 0;
};

bool File::open(const fs::path& path) {
    impl_.reset();
    if (const fs::path* mod = overlay_file(path)) {   // a mod's file (set_overlay)
        auto f = std::make_shared<Impl>();
        f->name = mod->string();
        f->host = std::make_unique<HostFile>();
        if (!f->host->open(*mod)) return false;
        f->size = f->host->size;
        impl_ = std::move(f);
        return true;
    }
    const Loc l = locate(path, false);
    auto f = std::make_shared<Impl>();
    f->name = path.string();
    if (l.kind == Loc::kHost) {
        f->host = std::make_unique<HostFile>();
        if (!f->host->open(path)) return false;
        f->size = f->host->size;
    } else if (l.kind == Loc::kImage) {
        const Node& n = l.img->nodes[l.node];
        if (n.dir) return false;
        f->img = l.img;
        f->extents = n.extents;
        f->size = n.size;
    } else {
        return false;
    }
    impl_ = std::move(f);
    return true;
}
std::uint64_t File::size() const { return impl_ ? impl_->size : 0; }
const std::string& File::name() const {
    static const std::string none;
    return impl_ ? impl_->name : none;
}
std::int64_t File::read_at(std::uint64_t offset, void* dst, std::size_t n) const {
    if (!impl_) return -1;
    const Impl& f = *impl_;
    if (offset >= f.size) return 0;
    n = static_cast<std::size_t>(std::min<std::uint64_t>(n, f.size - offset));
    if (f.host) return f.host->read(offset, dst, n);
    std::size_t got = 0;
    std::uint64_t base = 0;   // the file offset where the extent starts
    for (const auto& [lba, bytes] : f.extents) {
        if (got == n) break;
        const std::uint64_t at = offset + got;
        if (at >= base + bytes) { base += bytes; continue; }
        const std::uint64_t in = at - base;
        const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(bytes - in, n - got));
        const std::int64_t r = f.img->read(std::uint64_t(lba) * 2048 + in, static_cast<std::uint8_t*>(dst) + got, k);
        if (r < 0) return -1;
        got += static_cast<std::size_t>(r);
        if (static_cast<std::size_t>(r) < k) break;   // the image ends early (truncated)
        base += bytes;
    }
    return static_cast<std::int64_t>(got);
}

// ---- queries ----
bool is_image(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && mount(path) != nullptr;
}
bool exists(const fs::path& path) { return overlay_file(path) || overlay_dir(path) || locate(path, false).kind != Loc::kNone; }
bool is_dir(const fs::path& path) {
    if (overlay_file(path)) return false;
    if (overlay_dir(path)) return true;
    const Loc l = locate(path, true);
    std::error_code ec;
    if (l.kind == Loc::kHost) return fs::is_directory(path, ec);
    return l.kind == Loc::kImage && l.img->nodes[l.node].dir;
}
bool is_file(const fs::path& path) {
    if (overlay_file(path)) return true;
    const Loc l = locate(path, false);
    std::error_code ec;
    if (l.kind == Loc::kHost) return fs::is_regular_file(path, ec);
    return l.kind == Loc::kImage && !l.img->nodes[l.node].dir;
}
std::int64_t file_size(const fs::path& path) {
    if (const fs::path* mod = overlay_file(path)) {
        std::error_code ec;
        const auto n = fs::file_size(*mod, ec);
        return ec ? -1 : static_cast<std::int64_t>(n);
    }
    const Loc l = locate(path, false);
    if (l.kind == Loc::kImage) return l.img->nodes[l.node].dir ? -1 : static_cast<std::int64_t>(l.img->nodes[l.node].size);
    std::error_code ec;
    if (l.kind != Loc::kHost || !fs::is_regular_file(path, ec)) return -1;
    const auto n = fs::file_size(path, ec);
    return ec ? -1 : static_cast<std::int64_t>(n);
}

bool read_file(const fs::path& path, std::vector<std::uint8_t>& out) {
    File f;
    if (!f.open(path)) return false;
    out.resize(static_cast<std::size_t>(f.size()));
    return f.read_at(0, out.data(), out.size()) == static_cast<std::int64_t>(out.size());
}

std::vector<Entry> list_dir(const fs::path& dir) {
    std::vector<Entry> out;
    const Loc l = locate(dir, true);
    if (l.kind == Loc::kImage) {
        const Node& d = l.img->nodes[l.node];
        for (const std::uint32_t k : d.kids) {
            const Node& n = l.img->nodes[k];
            out.push_back({n.name, dir / n.name, n.dir});
        }
    } else if (l.kind == Loc::kHost) {
        std::error_code ec;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            out.push_back({it->path().filename().string(), it->path(), it->is_directory(e2)});
        }
    }
    std::string key;
    if (const OverlayState* o = overlay_key(dir, key)) {   // the names only a mod has
        const auto k = o->kids.find(key);
        if (k != o->kids.end())
            for (const auto& [name, is_dir] : k->second)
                if (std::none_of(out.begin(), out.end(), [&](const Entry& e) { return ieq(e.name, name); }))
                    out.push_back({name, dir / from_u8(name), is_dir});
    }
    return out;
}

namespace {
fs::path resolve_disc(const fs::path& dir, const std::vector<std::string>& comps);
}

fs::path resolve_ci(const fs::path& dir, const std::string& relative) {
    const std::vector<std::string> comps = split(relative);
    for (const std::string& c : comps)
        if (c == "." || c == "..") return {};
    fs::path r = resolve_disc(dir, comps);
    if (r.empty() && g_overlay.load(std::memory_order_acquire)) {   // a path only a mod has
        fs::path cand = dir;
        for (const std::string& c : comps) cand /= from_u8(c);
        if (overlay_file(cand) || overlay_dir(cand)) r = cand;
    }
    return r;
}

namespace {
fs::path resolve_disc(const fs::path& dir, const std::vector<std::string>& comps) {
    const Loc l = locate(dir, true);
    if (l.kind == Loc::kNone) return {};
    fs::path cur = dir;
    std::shared_ptr<const Image> img = l.img;
    std::uint32_t node = l.node;
    for (std::size_t c = 0; c < comps.size(); ++c) {
        const std::string& comp = comps[c];
        if (img) {
            const std::int64_t n = img->find(node, comps, c);
            if (n < 0) return {};
            // the names as on the disc
            for (std::size_t k = c; k < comps.size(); ++k) {
                const std::int64_t m = img->find(node, {comps[k]});
                node = static_cast<std::uint32_t>(m);
                cur /= img->nodes[node].name;
            }
            return cur;
        }
        // a host folder: its own listing (as the platform always matched the extracted disc)
        std::error_code ec;
        fs::path match;
        bool found = false;
        for (fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec)) {
            if (!ieq(it->path().filename().string(), comp)) continue;
            if (found) return {};   // ambiguous
            match = it->path();
            found = true;
        }
        if (ec || !found) return {};
        cur = match;
        if (c + 1 < comps.size() && fs::is_regular_file(cur, ec)) {   // an image inside the folder
            img = mount(cur);
            if (!img) return {};
            node = 0;
        }
    }
    return cur;
}
}  // namespace

void set_overlay(const fs::path& root, const std::vector<std::pair<std::string, fs::path>>& files) {
    static std::mutex m;
    static std::vector<std::unique_ptr<OverlayState>> kept;   // never freed while a reader may hold one
    std::lock_guard<std::mutex> l(m);
    if (files.empty()) {
        g_overlay.store(nullptr, std::memory_order_release);
        return;
    }
    auto o = std::make_unique<OverlayState>();
    o->root = normal(root);
    o->dirs.insert("");
    std::set<std::pair<std::string, std::string>> seen;   // (folder key, name key)
    for (const auto& [key, host] : files) {
        o->files[key] = host;
        std::size_t at = 0;
        std::string parent;
        while (true) {
            const std::size_t slash = key.find('/', at);
            const std::string name = key.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
            const bool folder = slash != std::string::npos;
            if (seen.insert({parent, name}).second) o->kids[parent].push_back({name, folder});
            if (!folder) break;
            parent = key.substr(0, slash);
            o->dirs.insert(parent);
            at = slash + 1;
        }
    }
    g_overlay.store(o.get(), std::memory_order_release);
    kept.push_back(std::move(o));
}
bool has_overlay() { return g_overlay.load(std::memory_order_acquire) != nullptr; }

}  // namespace b3asset::disc
