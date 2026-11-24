// The game's disc as the platform reads it (src/native/platform_disc.h).
#include "platform_disc.h"
#include "disc_alias.h"   // trafficseries / customseries
#include "b3port/fileio.h"   // fsrobust

#include "platform.h"

#include "b3asset/language.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
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
    // the host path as the rest of this layer passes it: UTF-8 (File::open_host: b3io::u8path)
    return b3io::u8string(f->host);
}

}  // namespace

void set_overlay(const b3mod::Overlay* overlay) { g_overlay = overlay && !overlay->empty() ? overlay : nullptr; }
AliasList g_aliases;   // trafficcars, customseries (disc_alias.h: asked in the order set)
void set_alias(std::string (*alias)(const std::string&)) { g_aliases.add(alias); }

namespace {
std::string g_lang_global, g_lang_head;   // the language's two files (host paths), empty: the game's own text
std::string g_lang_code;                  // its code (FR, IT...)
}  // namespace

std::string language_text() {
    if (g_lang_global.empty()) return {};
    if (g_overlay && !from_overlay("DATA/GLOBALUS.BIN").empty()) return {};   // a mod's GLOBALUS.BIN wins
    return g_lang_code;
}

bool set_language(const std::string& disc, const std::string& code) {
    g_lang_global.clear();
    g_lang_head.clear();
    g_lang_code.clear();
    if (disc.empty() || code.empty()) return false;
    const std::filesystem::path d = b3io::u8path(disc);   // 1.3.0 review: UTF-8 (a user data folder of any name)
    const std::filesystem::path g = b3asset::language::file(d, code, false), h = b3asset::language::file(d, code, true);
    if (g.empty() || h.empty() || !b3asset::language::supported(b3asset::language::normalise(code))) return false;
    g_lang_global = b3io::u8string(g);
    g_lang_head = b3io::u8string(h);
    g_lang_code = b3asset::language::normalise(code);
    return true;
}

namespace {
std::string& g_languages_root_set() { static std::string r; return r; }   // 1.3.0 set_languages_root
// 1.3.0: the DJ's voice (set_dj_voice)
std::string g_dj_root;     // host path (narrow), empty: the English DJ
std::string g_dj_code;
char g_dj_letter = 0;
std::mutex g_dj_m;
std::map<std::string, std::string> g_dj_files;   // an English bank's key -> the host file that answers for it ("": English)
}  // namespace

bool set_dj_voice(const std::string& dj_root, const std::string& code) {
    std::lock_guard<std::mutex> lk(g_dj_m);
    g_dj_root.clear();
    g_dj_code.clear();
    g_dj_letter = 0;
    g_dj_files.clear();
    const char letter = b3asset::language::dj_letter(code);
    if (dj_root.empty() || !letter) return false;
    const auto voices = b3asset::language::dj_on_disc(b3io::u8path(dj_root));
    if (voices.size() != 1 || voices[0].letter != letter) return false;
    g_dj_root = dj_root;
    g_dj_code = b3asset::language::normalise(code);
    g_dj_letter = letter;
    return true;
}

namespace {
std::string dj_file(const std::string& p, const std::filesystem::path& english_host);
}  // namespace

std::string dj_banks(const std::string& disc) {
    // every bank the language has, decided now (dj_file: the header checks), so the set is the run's from its start
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lk(g_dj_m);
        if (!g_dj_letter) return {};
        for (const auto& v : b3asset::language::dj_on_disc(b3io::u8path(g_dj_root)))
            for (const auto& f : v.files) {
                std::string k = f.key;
                k[k.rfind('/') + 1] = 'E';
                keys.push_back(k);
            }
    }
    const std::filesystem::path disc_root = b3io::u8path(disc);   // (root() is not set yet: it keeps its first value)
    std::string redirected;
    std::size_t n = 0;
    std::sort(keys.begin(), keys.end());
    for (const std::string& k : keys) {
        const std::filesystem::path e = b3asset::disc::resolve_ci(disc_root, k);
        if (e.empty() || !b3asset::disc::is_file(e)) continue;
        if (!dj_file(k, e).empty()) {
            redirected += k + "\n";
            ++n;
        }
    }
    std::uint64_t h = 1469598103934665603ull;   // FNV-1a 64 of the redirected banks' keys
    for (const unsigned char c : redirected) h = (h ^ c) * 1099511628211ull;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%zu:%016llx", n, static_cast<unsigned long long>(h));
    return buf;
}

namespace {
bool g_training_imported = true;   // 1.3.0: the imported training pictures may be used (a replay recorded without: false)
}  // namespace
void use_training_pictures(bool imported) { g_training_imported = imported; }
bool training_pictures(const std::string& code) {
    if (g_languages_root_set().empty() || b3asset::language::training_tag(code).empty()) return false;
    for (const char* kind : {"TRC_", "TRD_"}) {
        const std::filesystem::path f = b3asset::disc::resolve_ci(b3io::u8path(g_languages_root_set()) / "VIDEO" / b3asset::language::normalise(code),
                                                                  std::string("OVID/") + kind + b3asset::language::training_tag(code) + ".M2V");
        if (f.empty() || !b3asset::language::m2v_ok(f)) return false;
    }
    return true;
}

void set_languages_root(const std::string& root) { g_languages_root_set() = root; }

namespace {
// A localized training video (`p`: the disc path without device and version): the imported picture, else the English
// one on the disc; "" for any other path.
std::string training_file(const std::string& p) {
    namespace L = b3asset::language;
    std::string code, english;
    const std::string key = L::disc_key(p);
    if (!L::training_video(key, &code, &english)) return {};
    static std::mutex m;
    static std::map<std::string, std::string> done;
    std::lock_guard<std::mutex> lk(m);
    if (const auto it = done.find(key); it != done.end()) return it->second;
    std::string host;
    if (!g_languages_root_set().empty() && g_training_imported) {
        // (pack_root(pref, code, kTraining) = <pref>/languages/VIDEO/<code>)
        const std::filesystem::path f = b3asset::disc::resolve_ci(b3io::u8path(g_languages_root_set()) / "VIDEO" / code, key);
        if (!f.empty() && b3asset::disc::is_file(f) && L::m2v_ok(f)) host = b3io::u8string(f);
    }
    const bool imported = !host.empty();
    if (!imported && b3asset::disc::is_dir(b3io::u8path(root()))) {
        const std::filesystem::path e = b3asset::disc::resolve_ci(b3io::u8path(root()), english);
        if (!e.empty() && b3asset::disc::is_file(e)) host = b3io::u8string(e);
    }
    std::printf("native_play: training video %s: %s\n", key.c_str(),
                imported ? ("imported, " + host).c_str() : host.empty() ? "not found" : ("not imported: the English picture " + english).c_str());
    std::fflush(stdout);
    done.emplace(key, host);
    return host;
}
}  // namespace

std::string dj_voice() {
    std::lock_guard<std::mutex> lk(g_dj_m);
    return g_dj_code;
}

namespace {
// The language's file for an English DJ bank (`p`: the disc path without device and version; `english_host`: the
// disc's file), "" for any other path or when the bank stays English (said once per bank).
std::string dj_file(const std::string& p, const std::filesystem::path& english_host) {
    namespace L = b3asset::language;
    const std::string key = L::disc_key(p);
    const std::string target = L::dj_counterpart(key, g_dj_letter);
    if (target.empty()) return {};
    std::lock_guard<std::mutex> lk(g_dj_m);
    if (const auto it = g_dj_files.find(key); it != g_dj_files.end()) return it->second;
    std::string host, why;
    L::RwsHeader lh, eh;
    const std::filesystem::path f = L::dj_redirect(b3io::u8path(g_dj_root), g_dj_letter, key, english_host, &why, &lh);
    if (!f.empty()) {
        host = b3io::u8string(f);
        L::rws_read_header(english_host, eh);
    }
    if (host.empty())
        std::printf("native_play: dj voice: %s: the %s bank %s (%s): the English DJ for it\n", key.c_str(), g_dj_code.c_str(),
                    target.substr(target.rfind('/') + 1).c_str(), why.c_str());
    else
        std::printf("native_play: dj voice: %s -> %s (%u lines, the longest %llu bytes; English %llu)\n", key.c_str(), host.c_str(),
                    lh.segments, static_cast<unsigned long long>(lh.largest_segment), static_cast<unsigned long long>(eh.largest_segment));
    std::fflush(stdout);
    g_dj_files.emplace(key, host);
    return host;
}
}  // namespace

const std::string& root() {
    static const std::string r = [] {
        const auto e = b3io::getenv("B3_DISC_ROOT");   // fsrobust: UTF-8 (native_play sets it wide)
        return e ? *e : std::string("data/game");
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
    if (std::string a = g_aliases.ask(p); !a.empty()) {   // trafficcars / customseries: a vehicle's files under other names
        if (a.compare(0, 5, "host:") == 0) return a.substr(5);   // ("host:<path>": a host file)
        p = a;
    }
    if (g_overlay)   // the mod kit's overlay (mods.h): an active mod's file first
        if (std::string mod = from_overlay(p); !mod.empty()) return mod;
    if (!g_lang_global.empty()) {   // 1.0.1: the chosen language's pair from the player's European disc
        if (same_ascii_ci(p, "DATA/GLOBALUS.BIN") || same_ascii_ci(p, "DATA\\GLOBALUS.BIN")) return g_lang_global;
        if (same_ascii_ci(p, "DATA/HEADUS.BIN") || same_ascii_ci(p, "DATA\\HEADUS.BIN")) return g_lang_head;
    }
    if (std::string t = training_file(p); !t.empty()) return t;   // 1.3.0
    const std::filesystem::path disc_root = b3io::u8path(root());   // UTF-8 (B3_DISC_ROOT is read wide)
    if (!b3asset::disc::is_dir(disc_root))
        b3host::fatal("platform disc: " + root() + " is neither a folder of the disc's files nor a disc image (set B3_DISC_ROOT)" +
                      (b3asset::disc::image_error(disc_root).empty() ? std::string() : ": " + b3asset::disc::image_error(disc_root)));   // chd1
    const std::filesystem::path host = b3asset::disc::resolve_ci(disc_root, p);   // '\' or '/', names case-insensitive
    if (host.empty() || !b3asset::disc::is_file(host)) return {};
    if (g_dj_letter)   // 1.3.0: an English DJ bank in the chosen language
        if (std::string dj = dj_file(p, host); !dj.empty()) return dj;
    return b3io::u8string(host);
}

bool File::open(const std::string& guest_path) {
    const std::string host = resolve(guest_path);
    return !host.empty() && open_host(host);
}

bool File::open_host(const std::string& host) {
    b3asset::disc::File f;
    if (!f.open(b3io::u8path(host)) || f.size() > 0xFFFFFFFFull) return false;
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
// B3_DJ_LOG (platform_disc.h): one DJ bank's lines as the stream source reads them
struct DjTrace {
    std::string bank;                                          // the file's name ("G_DJRACE.RWS", with its folder)
    std::vector<std::pair<std::uint64_t, std::uint64_t>> lines;   // each line's file offset and size
    int line = -1;                                             // the line being read, -1 none
    std::uint64_t read = 0;                                    // its bytes read so far
};
struct Open {
    File f;
    std::uint64_t pos = 0;
    std::unique_ptr<DjTrace> dj;
};
std::map<int, Open> g_open;

bool dj_log() {
    static const bool on = [] { const char* e = std::getenv("B3_DJ_LOG"); return e && *e == '1'; }();
    return on;
}
std::unique_ptr<DjTrace> dj_trace(const File& f) {
    std::string name = f.host_path;
    for (char& ch : name)
        if (ch == '\\') ch = '/';
    const std::size_t slash = name.rfind('/');
    const std::string file = b3asset::language::disc_key(name.substr(slash == std::string::npos ? 0 : slash + 1));
    const bool movie = file == "MOVIE.RWS";   // the training narration (and the front end's other movie sounds)
    if (!movie && (file.size() < 8 || file[1] != '_' || file.compare(2, 2, "DJ") != 0 || file.compare(file.size() - 4, 4, ".RWS") != 0)) return nullptr;
    std::vector<std::uint8_t> b(static_cast<std::size_t>(std::min<std::uint64_t>(f.size, 0x10030)));
    if (b.empty()) return nullptr;
    f.read(0, b.data(), b.size());
    b3asset::language::RwsHeader h;
    if (!b3asset::language::rws_header(b.data(), b.size(), f.size, h)) return nullptr;
    auto t = std::make_unique<DjTrace>();
    const std::size_t up = slash == std::string::npos || slash == 0 ? std::string::npos : name.rfind('/', slash - 1);
    t->bank = up == std::string::npos ? file : b3asset::language::disc_key(name.substr(up + 1));
    const std::uint64_t data = 0x18 + h.header_size + 12;
    for (std::uint32_t k = 0; k < h.segments; ++k) {
        const std::size_t e = 0x18 + 0x60 + 0x20 * std::size_t(k);
        const auto u32 = [&](std::size_t at) { return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24; };
        t->lines.push_back({data + u32(e + 0x1C), u32(e + 0x18)});
    }
    return t;
}
void dj_line_end(DjTrace& t) {
    if (t.line < 0) return;
    std::printf("dj: %s line %d: %llu of %llu bytes read\n", t.bank.c_str(), t.line, static_cast<unsigned long long>(t.read),
                static_cast<unsigned long long>(t.lines[std::size_t(t.line)].second));
    t.line = -1;
}
void dj_seek(DjTrace& t, std::uint64_t pos) {
    for (std::size_t k = 0; k < t.lines.size(); ++k)
        if (pos == t.lines[k].first) {   // a line's start: the stream source begins it
            dj_line_end(t);
            t.line = int(k);
            t.read = 0;
            std::printf("dj: VBlank %llu: %s line %zu (%llu bytes) starts\n", static_cast<unsigned long long>(kernel().vblanks), t.bank.c_str(), k,
                        static_cast<unsigned long long>(t.lines[k].second));
            std::fflush(stdout);
            return;
        }
}
void dj_read(DjTrace& t, std::uint64_t pos, std::uint64_t n) {
    if (t.line < 0) return;
    const auto [at, size] = t.lines[std::size_t(t.line)];
    const std::uint64_t lo = std::max(pos, at), hi = std::min(pos + n, at + size);
    if (hi > lo) t.read += hi - lo;
}
}  // namespace

int open(const std::string& guest_path) {
    Open o;
    if (!o.f.open(guest_path)) return -1;
    if (dj_log()) o.dj = dj_trace(o.f);
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
    if (it->second.dj) dj_seek(*it->second.dj, it->second.pos);
    return base + offset;
}
std::int64_t read(int handle, void* dst, std::uint32_t bytes) {
    const auto it = g_open.find(handle);
    if (it == g_open.end()) return -1;
    Open& o = it->second;
    const std::uint64_t left = o.pos < o.f.size ? o.f.size - o.pos : 0;
    const std::uint64_t n = bytes < left ? bytes : left;
    if (n) o.f.read(o.pos, dst, static_cast<std::size_t>(n));
    if (o.dj) dj_read(*o.dj, o.pos, n);
    o.pos += n;
    return static_cast<std::int64_t>(n);
}
void close(int handle) {
    const auto it = g_open.find(handle);
    if (it != g_open.end() && it->second.dj) {
        dj_line_end(*it->second.dj);
        std::fflush(stdout);
    }
    g_open.erase(handle);
}

}  // namespace b3native::platform::disc
