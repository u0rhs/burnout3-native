// The game's disc as the platform reads it (src/native/platform_disc.h).
#include "platform_disc.h"

#include "platform.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>

namespace b3native::platform::disc {
namespace {

bool same_ascii_ci(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return i == a.size() && !b[i];
}

const b3mod::Overlay* g_overlay = nullptr;

// The overlay's file for a disc path (already without the device and version), or "" (the disc's)
std::string from_overlay(const std::string& p) {
    const b3mod::ActiveMod* who = nullptr;
    const b3mod::OverlayFile* f = g_overlay->find(p, &who);
    if (!f) return {};
    static std::mutex m;
    static std::set<std::string> told;
    {
        std::lock_guard<std::mutex> lk(m);
        if (told.insert(f->key).second) {
            std::printf("mods: %s from %s (%llu bytes%s)\n", f->key.c_str(), who->id.c_str(), static_cast<unsigned long long>(f->size),
                        f->patch.empty() ? "" : ", its patch applied to your disc's file");
            std::fflush(stdout);
        }
    }
    // the host path as the rest of this layer passes it (File::open_host: std::string -> path, the narrow encoding)
    try {
        return f->host.string();
    } catch (...) {
        stub_note("mods", "a mod file's path the narrow encoding cannot hold: the disc's file");
        return {};
    }
}

}  // namespace

void set_overlay(const b3mod::Overlay* overlay) { g_overlay = overlay && !overlay->empty() ? overlay : nullptr; }

const std::string& root() {
    static const std::string r = [] {
        const char* e = std::getenv("B3_DISC_ROOT");
        return std::string(e && *e ? e : "data/game");
    }();
    return r;
}

std::string resolve(const std::string& guest_path) {
    std::string p = guest_path;
    // the device: the PS2's CD/DVD drive is "cdrom0:"; a path on any other device is not a disc file
    if (const std::size_t colon = p.find(':'); colon != std::string::npos) {
        if (!same_ascii_ci(p.substr(0, colon), "cdrom0")) return {};
        p.erase(0, colon + 1);
    }
    // ISO 9660's file version (";1"): not part of the name
    if (const std::size_t semi = p.rfind(';'); semi != std::string::npos) {
        bool digits = semi + 1 < p.size();
        for (std::size_t k = semi + 1; k < p.size(); ++k) digits = digits && p[k] >= '0' && p[k] <= '9';
        if (digits) p.resize(semi);
    }
    std::size_t start = 0;
    while (start < p.size() && (p[start] == '\\' || p[start] == '/')) ++start;
    p.erase(0, start);
    if (p.empty()) return {};
    if (g_overlay)   // the mod kit's overlay (mods.h): an active mod's file first
        if (std::string mod = from_overlay(p); !mod.empty()) return mod;
    if (!b3asset::disc::is_dir(root()))
        b3host::fatal("platform disc: " + root() + " is neither a folder of the disc's files nor a disc image (set B3_DISC_ROOT)");
    const std::filesystem::path host = b3asset::disc::resolve_ci(root(), p);   // '\' or '/', names case-insensitive
    if (host.empty() || !b3asset::disc::is_file(host)) return {};
    return host.string();
}

bool File::open(const std::string& guest_path) {
    const std::string host = resolve(guest_path);
    return !host.empty() && open_host(host);
}

bool File::open_host(const std::string& host) {
    b3asset::disc::File f;
    if (!f.open(host) || f.size() > 0xFFFFFFFFull) return false;
    file = std::move(f);
    host_path = host;
    size = file.size();
    return true;
}

void File::read(std::uint64_t offset, void* dst, std::size_t n) const {
    std::size_t done = 0;
    int last_errno = 0;
    for (int tries = 0; done < n && tries < 8; ++tries) {
        errno = 0;
        const std::int64_t got = file.read_at(offset + done, static_cast<std::uint8_t*>(dst) + done, n - done);
        if (got > 0) done += static_cast<std::size_t>(got);
        if (done < n) last_errno = errno ? errno : -1;
    }
    if (done < n)
        b3host::fatal("platform disc: reading " + host_path + " failed: " + std::to_string(done) + " of " + std::to_string(n) +
                      " bytes at " + std::to_string(offset) + " (file size " + std::to_string(size) + ", errno " +
                      std::to_string(last_errno) + ")");
    if (last_errno) stub_note("disc read", "a short host read retried and completed (the host's file system)");
}

// ---- RW Audio's IOP file reads
namespace {
struct Open {
    File f;
    std::uint64_t pos = 0;
};
std::map<int, Open> g_open;
}  // namespace

int open(const std::string& guest_path) {
    Open o;
    if (!o.f.open(guest_path)) return -1;
    int h = 0;
    while (g_open.count(h)) ++h;
    g_open.emplace(h, std::move(o));
    return h;
}
std::int64_t size(int handle) {
    const auto it = g_open.find(handle);
    return it == g_open.end() ? -1 : static_cast<std::int64_t>(it->second.f.size);
}
std::int64_t seek(int handle, std::int64_t offset, int whence) {
    const auto it = g_open.find(handle);
    if (it == g_open.end()) return -1;
    std::int64_t base = 0;
    if (whence == 1) base = static_cast<std::int64_t>(it->second.pos);
    else if (whence == 2) base = static_cast<std::int64_t>(it->second.f.size);
    else if (whence != 0) return -1;
    if (base + offset < 0) return -1;
    it->second.pos = static_cast<std::uint64_t>(base + offset);
    return base + offset;
}
std::int64_t read(int handle, void* dst, std::uint32_t bytes) {
    const auto it = g_open.find(handle);
    if (it == g_open.end()) return -1;
    Open& o = it->second;
    const std::uint64_t left = o.pos < o.f.size ? o.f.size - o.pos : 0;
    const std::uint64_t n = bytes < left ? bytes : left;
    if (n) o.f.read(o.pos, dst, static_cast<std::size_t>(n));
    o.pos += n;
    return static_cast<std::int64_t>(n);
}
void close(int handle) { g_open.erase(handle); }

}  // namespace b3native::platform::disc
