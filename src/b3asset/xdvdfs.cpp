// The player's Xbox disc image, read only (include/b3asset/xdvdfs.h).
#include "b3asset/xdvdfs.h"

#include <algorithm>
#include <cstring>

#include "b3port/fileio.h"

namespace b3asset {

namespace {
constexpr std::uint32_t kSector = 2048;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";   // 20 characters
constexpr std::uint64_t kBases[] = {0, 0x18300000ull, 0xFD90000ull, 0x2080000ull};

std::uint16_t le16(const std::uint8_t* p) { return std::uint16_t(p[0] | p[1] << 8); }
std::uint32_t le32(const std::uint8_t* p) { return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }

bool seek64(std::FILE* f, std::uint64_t off) {
#ifdef _WIN32
    return _fseeki64(f, static_cast<long long>(off), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(off), SEEK_SET) == 0;
#endif
}
std::uint64_t size64(std::FILE* f) {
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) return 0;
    const long long n = _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) return 0;
    const off_t n = ftello(f);
#endif
    return n > 0 ? std::uint64_t(n) : 0;
}
}  // namespace

std::string XdvdfsImage::key(const std::string& path) {
    std::string k;
    k.reserve(path.size());
    for (char c : path) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c == '/' && (k.empty() || k.back() == '/')) continue;   // leading / doubled separators
        k += c;
    }
    while (!k.empty() && k.back() == '/') k.pop_back();
    return k;
}

void XdvdfsImage::close() {
    if (f_) std::fclose(f_);
    f_ = nullptr;
    base_ = size_ = 0;
    entries_.clear();
    index_.clear();
    dirs_seen_.clear();
}

bool XdvdfsImage::read_at(std::uint64_t off, void* dst, std::size_t n) const {
    if (!f_ || off > size_ || n > size_ - off) return false;
    const std::lock_guard<std::mutex> lk(m_);
    return seek64(f_, off) && std::fread(dst, 1, n, f_) == n;
}

bool XdvdfsImage::open(const std::filesystem::path& image, std::string* why) {
    close();
    const auto fail = [&](const std::string& w) {
        if (why) *why = w;
        close();
        return false;
    };
    f_ = b3io::fopen(image, "rb");
    if (!f_) return fail("cannot open the file");
    size_ = size64(f_);
    bool found = false;
    std::uint8_t vd[kSector];
    for (std::uint64_t b : kBases) {
        if (!read_at(b + 32ull * kSector, vd, kSector)) continue;
        if (std::memcmp(vd, kMagic, 20) == 0) {   // (the copy at +0x7EC is not required)
            base_ = b;
            found = true;
            break;
        }
    }
    if (!found) return fail("not an Xbox disc image (no XDVDFS volume in it)");
    const std::uint32_t root = le32(vd + 0x14), root_size = le32(vd + 0x18);
    std::string w;
    if (!walk(root, root_size, "", 0, &w)) return fail(w);
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return key(a.path) < key(b.path); });
    index_.reserve(entries_.size());
    for (std::size_t i = 0; i < entries_.size(); ++i) index_.emplace_back(key(entries_[i].path), i);
    std::sort(index_.begin(), index_.end());
    return true;
}

bool XdvdfsImage::walk(std::uint32_t sector, std::uint32_t size, const std::string& prefix, std::size_t depth, std::string* why) {
    if (size == 0) return true;   // an empty folder
    if (depth > kMaxDepth) { *why = "the disc's folders nest too deep (a damaged image?)"; return false; }
    if (size > kMaxDirBytes) { *why = "a folder on the disc is too large (a damaged image?)"; return false; }
    if (std::find(dirs_seen_.begin(), dirs_seen_.end(), sector) != dirs_seen_.end()) return true;   // a cycle: read once
    dirs_seen_.push_back(sector);
    std::vector<std::uint8_t> d(size);
    if (!read_at(base_ + std::uint64_t(sector) * kSector, d.data(), d.size())) {
        *why = "a folder on the disc lies outside the image (cut short?)";
        return false;
    }
    struct Sub { std::string path; std::uint32_t sector, size; };
    std::vector<Sub> subs;
    std::vector<std::uint32_t> stack{0};
    std::vector<bool> seen(size / 4 + 1, false);
    while (!stack.empty()) {
        const std::uint32_t o = stack.back();
        stack.pop_back();
        if (o + 14 > size || seen[o / 4]) continue;
        seen[o / 4] = true;
        const std::uint8_t* e = d.data() + o;
        const std::uint16_t l = le16(e), r = le16(e + 2);
        if (l == 0xFFFF && r == 0xFFFF) continue;   // sector padding
        const std::uint32_t s = le32(e + 4), n = le32(e + 8);
        const std::uint8_t attr = e[12], nl = e[13];
        if (l) stack.push_back(std::uint32_t(l) * 4);
        if (r) stack.push_back(std::uint32_t(r) * 4);
        if (nl == 0 || o + 14 + nl > size) continue;
        std::string name(reinterpret_cast<const char*>(e + 14), nl);
        if (name == "." || name == ".." || name.find_first_of(std::string("/\\\0", 3)) != std::string::npos) continue;
        if (entries_.size() >= kMaxEntries) { *why = "too many files on the disc (a damaged image?)"; return false; }
        Entry en;
        en.path = prefix + name;
        en.offset = base_ + std::uint64_t(s) * kSector;
        en.size = n;
        en.dir = (attr & 0x10) != 0;
        if (en.dir) {
            entries_.push_back(en);
            subs.push_back({en.path + "/", s, n});
        } else if (n <= kMaxFileBytes && en.offset <= size_ && n <= size_ - en.offset) {
            entries_.push_back(en);   // (a file past the image's end is left out: the image is cut short there)
        }
    }
    for (const Sub& sub : subs)
        if (!walk(sub.sector, sub.size, sub.path, depth + 1, why)) return false;
    return true;
}

const XdvdfsImage::Entry* XdvdfsImage::find(const std::string& path) const {
    const std::string k = key(path);
    auto it = std::lower_bound(index_.begin(), index_.end(), k, [](const std::pair<std::string, std::size_t>& a, const std::string& b) { return a.first < b; });
    if (it == index_.end() || it->first != k) return nullptr;
    const Entry& e = entries_[it->second];
    return e.dir ? nullptr : &e;
}

bool XdvdfsImage::read(const Entry& e, std::vector<std::uint8_t>& out) const {
    if (e.dir) return false;
    out.resize(e.size);
    if (e.size == 0) return true;
    return read_at(e.offset, out.data(), out.size());
}

bool XdvdfsImage::read(const std::string& path, std::vector<std::uint8_t>& out) const {
    const Entry* e = find(path);
    return e && read(*e, out);
}

}  // namespace b3asset
