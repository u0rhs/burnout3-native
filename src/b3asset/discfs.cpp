// discfs.cpp -- see include/b3asset/discfs.h. ISO 9660 [ECMA-119]: the volume descriptors from logical sector 16
// (type 1 = the primary one: "CD001", the logical block size at +128, the root directory record at +156); a directory
// record: +0 length, +2 extent LBA (LE), +10 data length (LE), +25 flags (bit 1 directory, bit 7 not the file's last
// extent), +32 name length, +33 name ("NAME.EXT;1"; 0x00 / 0x01 = "." / ".."); records never cross a sector (a zero
// length byte: the rest of the sector is padding). A raw sector (2352 bytes) is sync 00 FF x10 00, header (mode at +15),
// then the 2048 data bytes at +16 (mode 1) or, after the 8-byte subheader, +24 (mode 2 form 1).
// chd1: a CHD (MAME's "compressed hunks of data", chdman createdvd / createcd; "MComprHD" at 0) is read through libchdr
// (src/b3asset/chd_libchdr.c): its data in hunks, decoded on demand into a cache (ChdCache, read ahead on worker threads
// for sequential reads). A DVD CHD's data is the cooked image (2048-byte sectors); a CD CHD's is frames of 2352 + 96
// (subcode) bytes per track, as its CD track metadata ("CHT2" / "CHTR": TRACK, TYPE, FRAMES, PREGAP, PGTYPE) lays them
// out: each track's frames padded to a multiple of 4, its pregap stored first when PGTYPE starts with 'V'. The volume
// is on the first data track.
#include "b3asset/discfs.h"

#include "../native/third_party/libchdr/include/libchdr/chd.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>
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

// ---- a CHD image's data (libchdr) ----
bool is_chd_magic(const HostFile& f) {
    char magic[8];
    return f.size >= 8 && f.read(0, magic, 8) == 8 && std::memcmp(magic, "MComprHD", 8) == 0;
}

// libchdr reads the file through these (its own position; only ever called under Chd::m or while opening)
struct ChdIo {
    HostFile file;
    std::uint64_t pos = 0;
};
const core_file_callbacks kChdIo = {
    [](void* p) -> std::uint64_t { return static_cast<ChdIo*>(p)->file.size; },
    [](void* dst, std::size_t size, std::size_t count, void* p) -> std::size_t {
        ChdIo& io = *static_cast<ChdIo*>(p);
        if (size == 0 || count > SIZE_MAX / size) return 0;
        const std::int64_t r = io.file.read(io.pos, dst, size * count);
        if (r <= 0) return 0;
        io.pos += static_cast<std::uint64_t>(r);
        return static_cast<std::size_t>(r) / size;
    },
    [](void* p) -> int { delete static_cast<ChdIo*>(p); return 0; },
    [](void* p, std::int64_t off, int whence) -> int {
        ChdIo& io = *static_cast<ChdIo*>(p);
        const std::int64_t from = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<std::int64_t>(io.pos)
                                : whence == SEEK_END ? static_cast<std::int64_t>(io.file.size) : -1;
        if (from < 0 || (off < 0 && -off > from)) return -1;
        io.pos = static_cast<std::uint64_t>(from + off);
        return 0;
    },
};

// A CHD's decoded hunks, shared by the threads that read the image and its read-ahead workers (which hold it while they
// run: they are never joined, so a program's exit never waits on one). Every hunk is decoded once while it is kept:
// a reader that needs one another thread is decoding waits for it; one only queued for read-ahead it decodes itself.
// Sequential reads (up to kStreams at a time: a load and RW Audio's streams interleave) queue the hunks after them for
// the workers, each with its own libchdr handle (chd_read is not thread-safe per handle), so LZMA / FLAC decoding runs
// beside the game. It changes when the bytes arrive, never which: the game's virtual time does not see it.
struct ChdCache {
    static constexpr std::size_t kStreams = 4;
    // libchdr's compressed read window per handle: consecutive hunks' compressed bytes in one host read (a host file
    // system with a cost per read, e.g. a network or WSL mount, then reads far fewer times)
    static constexpr std::size_t kReadWindow = 1u << 20, kReaderWindow = 256u << 10;   // workers' / the readers' handle
    static constexpr std::size_t kRun = 8;   // a worker takes up to this many consecutive queued hunks at a time
    std::string path;
    fs::path file;
    std::uint32_t hunkbytes = 0, hunks = 0;
    std::size_t cap = 0;     // hunks kept (decoded)
    std::uint32_t depth = 0; // hunks read ahead of a sequential stream

    std::mutex m;
    std::condition_variable cv;
    enum State : std::uint8_t { kQueued, kDecoding, kReady };
    struct Slot {
        State state = kQueued;
        std::vector<std::uint8_t> data;
        std::list<std::uint32_t>::iterator lru;   // kReady only
    };
    std::unordered_map<std::uint32_t, Slot> slots;
    std::list<std::uint32_t> lru;                 // the ready hunks, most recently used first
    std::deque<std::uint32_t> queue;              // read-ahead, oldest first
    std::vector<std::vector<std::uint8_t>> spare; // evicted hunks' buffers
    struct Stream { std::uint32_t last = UINT32_MAX, ahead = 0; std::uint64_t used = 0; };
    Stream streams[kStreams];
    std::uint64_t tick = 0;
    bool stop = false, told = false;
    std::atomic<bool> ahead_on{false};            // set once the image is mounted (not while it is probed)
    int workers = 0, workers_max = 0;

    chd_file* main = nullptr;                     // the readers' decoder
    std::mutex main_m;

    ~ChdCache() {
        if (main) chd_close(main);
    }

    // a buffer for a hunk (under m)
    std::vector<std::uint8_t> buffer() {
        std::vector<std::uint8_t> b;
        if (!spare.empty()) { b = std::move(spare.back()); spare.pop_back(); }
        b.resize(hunkbytes);
        return b;
    }
    // hunk h decoded into slot data (under m): the oldest ready hunks go while more than cap are kept
    void store(std::uint32_t h, std::vector<std::uint8_t>&& data) {
        Slot& s = slots[h];
        s.data = std::move(data);
        s.state = kReady;
        lru.push_front(h);
        s.lru = lru.begin();
        while (lru.size() > cap) {
            const std::uint32_t old = lru.back();
            lru.pop_back();
            const auto it = slots.find(old);
            spare.push_back(std::move(it->second.data));
            slots.erase(it);
        }
        if (spare.size() > 16) spare.resize(16);
    }
    static chd_error decode(chd_file* f, std::uint32_t h, std::uint8_t* dst) {
        chd_error e = CHDERR_NONE;
        for (int tries = 0; tries < 3; ++tries)   // a short read of a busy host file system (as platform_disc retries)
            if ((e = chd_read(f, h, dst)) == CHDERR_NONE) break;
        return e;
    }

    // a read of hunk h (under m): a stream that reads on from its last hunk has the next `depth` ones queued
    void note(std::uint32_t h, const std::shared_ptr<ChdCache>& self) {
        if (depth == 0 || !ahead_on.load(std::memory_order_relaxed)) return;
        ++tick;
        Stream* hit = nullptr;
        for (Stream& s : streams)
            if (s.last != UINT32_MAX && (h == s.last || h == s.last + 1)) { hit = &s; break; }
        if (!hit) {   // a new stream in the least recently used one's place
            hit = &streams[0];
            for (Stream& s : streams)
                if (s.used < hit->used) hit = &s;
            hit->last = h;
            hit->ahead = h;
            hit->used = tick;
            return;
        }
        const bool moved = h != hit->last;
        hit->last = h;
        hit->used = tick;
        if (!moved) return;
        const std::uint64_t to = std::min<std::uint64_t>(std::uint64_t(h) + depth, hunks - 1);
        bool queued = false;
        for (std::uint64_t k = std::max<std::uint64_t>(hit->ahead, h) + 1; k <= to; ++k) {
            const auto n = static_cast<std::uint32_t>(k);
            if (slots.count(n)) continue;
            slots[n].state = kQueued;
            queue.push_back(n);
            queued = true;
        }
        hit->ahead = static_cast<std::uint32_t>(std::max<std::uint64_t>(hit->ahead, to));
        if (!queued) return;
        if (workers < workers_max) {   // started on the first read-ahead, each with its own handle
            ++workers;
            std::thread([self]() { self->work(); }).detach();
        }
        cv.notify_all();
    }

    void work() {
        chd_file* f = nullptr;
        auto io = std::make_unique<ChdIo>();
        if (!io->file.open(file) || chd_open_core_file_callbacks(&kChdIo, io.release(), CHD_OPEN_READ, nullptr, &f) != CHDERR_NONE) f = nullptr;
        if (f) chd_set_cache_budget(f, kReadWindow);
        std::unique_lock<std::mutex> l(m);
        std::vector<std::uint32_t> run;
        while (f) {
            cv.wait(l, [&] { return stop || !queue.empty(); });
            if (stop) break;
            // the first queued hunk a reader has not taken, and the ones queued right after it that follow on from it
            run.clear();
            while (!queue.empty() && run.size() < kRun) {
                const std::uint32_t h = queue.front();
                if (!run.empty() && h != run.back() + 1) break;
                queue.pop_front();
                const auto it = slots.find(h);
                if (it == slots.end() || it->second.state != kQueued) { if (!run.empty()) break; continue; }   // a reader took it
                it->second.state = kDecoding;
                run.push_back(h);
            }
            for (const std::uint32_t h : run) {   // in order: the handle's read window serves them
                std::vector<std::uint8_t> b = buffer();
                l.unlock();
                const chd_error e = decode(f, h, b.data());
                l.lock();
                if (e == CHDERR_NONE) store(h, std::move(b));
                else slots.erase(h);   // a reader decodes it again (and reports it)
                cv.notify_all();
            }
        }
        --workers;
        l.unlock();
        if (f) chd_close(f);
    }

    // `k` bytes of hunk h from `in` (under l): false when it cannot be decoded (reported once)
    bool copy(std::uint32_t h, std::size_t in, std::size_t k, std::uint8_t* dst, std::unique_lock<std::mutex>& l) {
        for (;;) {
            auto it = slots.find(h);
            if (it != slots.end() && it->second.state == kReady) {
                lru.splice(lru.begin(), lru, it->second.lru);
                std::memcpy(dst, it->second.data.data() + in, k);
                return true;
            }
            if (it != slots.end() && it->second.state == kDecoding) {   // another thread's: wait for it
                cv.wait(l);
                continue;
            }
            Slot& s = slots[h];   // absent, or only queued: decoded here
            s.state = kDecoding;
            std::vector<std::uint8_t> b = buffer();
            l.unlock();
            chd_error e;
            {
                std::lock_guard<std::mutex> g(main_m);
                e = decode(main, h, b.data());
            }
            l.lock();
            if (e != CHDERR_NONE) {
                slots.erase(h);
                cv.notify_all();
                if (!told)
                    std::fprintf(stderr, "discfs: %s: hunk %u of the CHD image cannot be read (libchdr: %s): the image is damaged\n", path.c_str(), h,
                                 chd_error_string(e));
                told = true;
                return false;
            }
            store(h, std::move(b));
            cv.notify_all();
        }
    }
};

struct Chd {
    std::string path;
    chd_file* chd = nullptr;   // (the cache's main decoder: metadata at open)
    std::uint32_t hunkbytes = 0;
    std::uint64_t logical = 0;   // the data's bytes
    // a CD image: the volume's track (frames of 2352 + 96 bytes), its first sector's frame and its sectors' layout
    bool cd = false;
    std::uint64_t first_frame = 0;
    std::uint32_t data_off = 0;   // the 2048 user bytes' offset in a frame (0: cooked 2048, 8: 2336, 16 / 24: raw)
    bool raw = false;             // 2352-byte sectors (MODE1_RAW / MODE2_RAW): the sync and mode byte decide
    std::shared_ptr<ChdCache> cache;

    Chd() = default;
    Chd(const Chd&) = delete;
    Chd& operator=(const Chd&) = delete;
    ~Chd() {
        if (!cache) {
            if (chd) chd_close(chd);
            return;
        }
        std::lock_guard<std::mutex> l(cache->m);   // the workers end (and close their handles); the cache goes with the last
        cache->stop = true;
        cache->cv.notify_all();
    }
    bool damaged() const {
        std::lock_guard<std::mutex> l(cache->m);
        return cache->told;
    }
    void read_ahead() { cache->ahead_on.store(true, std::memory_order_relaxed); }

    // the image at `p`: null and `why` when libchdr cannot open it (or it is no CD / DVD image)
    static std::unique_ptr<Chd> open(const fs::path& p, std::string& why) {
        auto io = std::make_unique<ChdIo>();
        if (!io->file.open(p)) { why = "the file cannot be opened"; return nullptr; }
        auto c = std::make_unique<Chd>();
        c->path = p.string();
        const chd_error e = chd_open_core_file_callbacks(&kChdIo, io.release(), CHD_OPEN_READ, nullptr, &c->chd);   // owns io now
        if (e != CHDERR_NONE) {
            c->chd = nullptr;
            why = e == CHDERR_REQUIRES_PARENT ? "it is a CHD that needs its parent CHD (make a CHD of the whole disc)"
                  : e == CHDERR_UNSUPPORTED_VERSION || e == CHDERR_UNSUPPORTED_FORMAT
                      ? std::string("a kind of CHD this port does not read (libchdr: ") + chd_error_string(e) + ")"
                      : std::string("the image is damaged or cut short (libchdr: ") + chd_error_string(e) + ")";
            return nullptr;
        }
        const chd_header* h = chd_get_header(c->chd);
        c->hunkbytes = h->hunkbytes;
        c->logical = std::min<std::uint64_t>(h->logicalbytes, std::uint64_t(h->totalhunks) * h->hunkbytes);
        if (c->hunkbytes == 0 || c->logical == 0) { why = "an empty CHD"; return nullptr; }
        if (!c->cd_layout(why)) return nullptr;
        auto k = std::make_shared<ChdCache>();
        k->path = c->path;
        k->file = p;
        k->hunkbytes = c->hunkbytes;
        k->hunks = h->totalhunks;
        k->cap = std::max<std::size_t>(64, (64u << 20) / c->hunkbytes);   // ~64 MB of decoded hunks
        if (const char* kb = std::getenv("B3_CHD_CACHE_KB"); kb && *kb)   // tests: a small cache (eviction, read-ahead)
            k->cap = std::max<std::size_t>(8, std::size_t(std::strtoul(kb, nullptr, 10)) * 1024 / c->hunkbytes);
        k->depth = static_cast<std::uint32_t>(std::min<std::size_t>(k->cap / 4, std::max<std::size_t>(4, (2u << 20) / c->hunkbytes)));   // ~2 MB
        const unsigned hw = std::thread::hardware_concurrency();
        k->workers_max = hw > 1 ? static_cast<int>(std::min(hw - 1, 4u)) : 0;
        if (k->workers_max == 0) k->depth = 0;
        k->main = c->chd;
        chd_set_cache_budget(k->main, ChdCache::kReaderWindow);   // (its reads are mostly the first hunk of a new place)
        c->cache = std::move(k);
        return c;
    }

    // a CD's track metadata: the first data track (false and `why`: a CD image without one)
    bool cd_layout(std::string& why) {
        struct Track { int num = 0; std::string type, pgtype; std::uint32_t frames = 0, pregap = 0; };
        std::vector<Track> tracks;
        for (const bool v2 : {true, false}) {
            for (std::uint32_t k = 0; k < 256; ++k) {
                char meta[256] = {};
                std::uint32_t len = 0;
                if (chd_get_metadata(chd, v2 ? CDROM_TRACK_METADATA2_TAG : CDROM_TRACK_METADATA_TAG, k, meta, sizeof meta - 1, &len, nullptr,
                                     nullptr) != CHDERR_NONE)
                    break;
                meta[std::min<std::uint32_t>(len, sizeof meta - 1)] = 0;
                Track t;
                char type[32] = {}, sub[32] = {}, pgtype[32] = {}, pgsub[32] = {};
                int num = 0, frames = 0, pregap = 0, postgap = 0;
                const int n = v2 ? std::sscanf(meta, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d", &num, type,
                                               sub, &frames, &pregap, pgtype, pgsub, &postgap)
                                 : std::sscanf(meta, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d", &num, type, sub, &frames);
                if (n < 4 || frames < 0 || pregap < 0) { why = "its CD track list is damaged"; return false; }
                t.num = num;
                t.type = type;
                t.pgtype = v2 && n >= 6 ? pgtype : "";
                t.frames = static_cast<std::uint32_t>(frames);
                t.pregap = v2 && n >= 5 ? static_cast<std::uint32_t>(pregap) : 0;
                tracks.push_back(std::move(t));
            }
            if (!tracks.empty()) break;
        }
        if (tracks.empty()) return true;   // no CD tracks: the data is the image itself (a DVD)
        cd = true;
        std::stable_sort(tracks.begin(), tracks.end(), [](const Track& a, const Track& b) { return a.num < b.num; });
        std::uint64_t frame = 0;
        for (const Track& t : tracks) {
            const std::uint32_t skip = !t.pgtype.empty() && t.pgtype[0] == 'V' ? t.pregap : 0;   // the pregap's frames in the file
            const std::string& y = t.type;
            int off = -1;
            if (y == "MODE1" || y == "MODE1/2048" || y == "MODE2_FORM1" || y == "MODE2/2048") off = 0;
            else if (y == "MODE2" || y == "MODE2/2336" || y == "MODE2_FORM_MIX") off = 8;
            else if (y == "MODE1_RAW" || y == "MODE1/2352") off = 16;
            else if (y == "MODE2_RAW" || y == "MODE2/2352") off = 24;
            if (off >= 0 && skip <= t.frames) {
                first_frame = frame + skip;
                data_off = static_cast<std::uint32_t>(off);
                raw = off >= 16;
                return true;
            }
            frame += t.frames + (4 - t.frames % 4) % 4;   // chdman pads each track to 4 frames
        }
        why = "a CD image without a data track";
        return false;
    }

    // up to n bytes of the data at off: the count (short only at the end), -1 when a hunk cannot be read
    std::int64_t read(std::uint64_t off, void* dst, std::size_t n) const {
        if (off >= logical) return 0;
        n = static_cast<std::size_t>(std::min<std::uint64_t>(n, logical - off));
        auto* d = static_cast<std::uint8_t*>(dst);
        std::size_t got = 0;
        ChdCache& c = *cache;
        std::unique_lock<std::mutex> l(c.m);
        while (got < n) {   // one hunk at a time: other threads' reads go between (while one decodes)
            const std::uint64_t pos = off + got;
            const auto h = static_cast<std::uint32_t>(pos / hunkbytes);
            const std::size_t in = static_cast<std::size_t>(pos % hunkbytes), k = std::min<std::size_t>(hunkbytes - in, n - got);
            c.note(h, cache);
            if (!c.copy(h, in, k, d + got, l)) return -1;
            got += k;
            if (got < n) { l.unlock(); l.lock(); }
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
    std::unique_ptr<Chd> chd;                    // chd1: a CHD: its decompressed data stands for the file's bytes
    std::uint64_t base = 0;                      // the volume's first sector in those bytes (a CD CHD: its track's)
    std::uint32_t stride = 2048, data_off = 0;   // physical sector size, the data's offset in it
    std::vector<Node> nodes;                     // [0] the root

    // the image's bytes (a CHD: decompressed): up to n at off, the count, -1 on an error
    std::int64_t bytes(std::uint64_t off, void* dst, std::size_t n) const { return chd ? chd->read(off, dst, n) : file.read(off, dst, n); }
    std::uint64_t size() const { return chd ? chd->logical : file.size; }

    // `n` bytes of the 2048-byte logical sectors at byte `off`: the count, -1 on an error
    std::int64_t read(std::uint64_t off, void* dst, std::size_t n) const {
        if (stride == 2048 && base == 0) return bytes(off, dst, n);
        std::size_t got = 0;
        auto* d = static_cast<std::uint8_t*>(dst);
        while (got < n) {
            const std::uint64_t at = off + got, sector = at / 2048, in = at % 2048;
            const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(2048 - in, n - got));
            const std::int64_t r = bytes(base + sector * stride + data_off + in, d + got, k);
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
        if (chd && chd->cd) {   // a CD CHD's track: frames of 2352 + 96 bytes; a raw track's sector 16 says mode 1 or 2
            stride = 2352 + 96;
            base = chd->first_frame * stride;
            data_off = chd->data_off;
            if (chd->raw && bytes(base + 16ull * stride, raw, 16) == 16 && std::memcmp(raw, sync, 12) == 0 && (raw[15] == 1 || raw[15] == 2))
                data_off = raw[15] == 1 ? 16 : 24;
        } else if (size() >= 17ull * 2352 && bytes(16ull * 2352, raw, 16) == 16 && std::memcmp(raw, sync, 12) == 0 &&
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
std::map<std::string, std::string> g_chd_errors;                // chd1: why a CHD file did not mount, by its normal path

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
    if (!img->file.open(p)) return nullptr;
    std::string why;
    if (is_chd_magic(img->file)) {   // chd1: by its magic, whatever its name
        img->chd = Chd::open(p, why);
        // a hunk that cannot be read: damaged; else no ISO 9660 volume in it: no disc image, as for an .iso
        const bool ok = img->chd && img->probe(pvd) && img->load_tree(pvd + 156);
        if (!ok && img->chd && img->chd->damaged()) why = "a part of its data cannot be read: the image is damaged or cut short";
        if (ok) img->chd->read_ahead();   // (not while it was probed: a file that is no image starts no workers)
        std::lock_guard<std::mutex> l(g_m);
        if (!why.empty()) g_chd_errors[key] = why;
        else g_chd_errors.erase(key);
        if (!ok) return nullptr;
    } else if (!img->probe(pvd) || !img->load_tree(pvd + 156)) {
        return nullptr;
    }
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
bool is_chd(const fs::path& path) {
    HostFile f;
    return f.open(path) && is_chd_magic(f);
}
std::string image_error(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || !is_chd(path) || mount(path)) return {};
    std::lock_guard<std::mutex> l(g_m);
    const auto it = g_chd_errors.find(normal(path));
    return it == g_chd_errors.end() ? std::string() : "The CHD image " + path.string() + " cannot be read: " + it->second + ".";
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
