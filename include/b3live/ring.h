// b3live ring: the in-memory feed from the game's scene hook to the live native renderer (docs/native-renderer.md
// §13). One writer (the game process: src/gameview/scene_capture.cpp's live_publish, once per retail poll) and one reader (the
// renderer process: b3replay --live) share a named memory mapping; no file is involved.
//
// The writer never waits. Each poll it appends one record to a byte ring and publishes it; a reader that falls a whole
// ring behind (a track load, a stalled window) loses records, notices it, and asks for a keyframe. A record carries
// exactly the bytes of a scene-capture record (scene_capture.cpp's file format: 'TICK', poll, vblank, run count, runs),
// either the poll's delta runs or, as a keyframe, every block of the writer's shadow image. A reader that starts at a
// keyframe and applies every later delta holds, at every poll, the same image a file reader of the same session holds
// (the shadow is the union of everything written so far), so it decodes the same ticks.
//
// Layout of the mapping: Header, then `capacity` data bytes (a power of two). A record in the ring: RecordHead, then
// RecordHead::size - sizeof(RecordHead) body bytes, the whole padded to 8 bytes; positions are monotonic byte counts
// (taken modulo the capacity), so a record may wrap. Publication: the writer first raises `reserved` to the record's
// end, writes the bytes, then raises `head`. A reader that copied [r, r + n) keeps it only if afterwards
// reserved - capacity <= r (nothing it copied was being overwritten).
//
// Platforms: Win32 named file mappings (Local\<name>; the game and the Windows renderer) and POSIX shared memory
// (/<name>; Linux / macOS, for b3livefeed tests).
#ifndef B3LIVE_RING_H
#define B3LIVE_RING_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near   // minwindef.h's legacy macros break ordinary identifiers (b3asset/enviro.h)
#undef far
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace b3live {

constexpr char kMagic[8] = {'B', '3', 'L', 'I', 'V', 'E', '1', 0};
constexpr std::uint32_t kVersion = 1;
// font1: 128 MB (was 32): the front end's movie pictures stream ~27 MB/s with the live feed (menu1; each changed
// picture once, only its changed blocks), so 32 MB held ~1.2 s of menus and a reader lapped at the first stall (races14,
// 2026-11-04); 128 MB holds ~4.7 s of movie, minutes of race deltas (2-10 KB per poll)
constexpr std::uint64_t kDefaultCapacity = 128ull << 20;
constexpr std::uint64_t kNoKey = ~0ull;

enum : std::uint32_t { kDelta = 1, kKeyframe = 2 };

struct Header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t header_size;
    std::uint64_t capacity;
    std::atomic<std::uint64_t> reserved;    // writer: end of the record being written
    std::atomic<std::uint64_t> head;        // writer: end of the last published record
    std::atomic<std::uint64_t> key_at;      // writer: start of the latest keyframe record (kNoKey: none yet)
    std::atomic<std::uint32_t> want_key;    // reader sets 1: the writer sends a keyframe at its next poll
    std::atomic<std::uint32_t> closed;      // writer: 1 after its last record (session end)
    std::atomic<std::uint64_t> records, keyframes, polls;   // writer statistics
    std::atomic<std::uint64_t> reader_pos;  // reader: its position (statistics only; the writer never reads it to wait)
    std::atomic<std::uint64_t> reader_resyncs;
    std::atomic<std::uint32_t> writer_pid;  // the writer's process (a reader treats its end like `closed`: a killed game)
    std::uint8_t pad[60];
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "lock-free 64-bit atomics");

struct RecordHead {
    std::uint32_t size;    // whole record: this head + body (not the padding)
    std::uint32_t kind;    // kDelta / kKeyframe
    std::uint64_t ns;      // writer's steady clock (ns) when the record was published; same machine as the reader
};
static_assert(sizeof(RecordHead) == 16, "record head");

inline std::uint64_t now_ns() {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// A mapped ring (writer: create; reader: open). Not copyable.
class Mapping {
public:
    Mapping() = default;
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
    ~Mapping() { close(); }

    Header* header() const { return hdr_; }
    std::uint8_t* data() const { return hdr_ ? reinterpret_cast<std::uint8_t*>(hdr_) + sizeof(Header) : nullptr; }

    bool create(const std::string& name, std::uint64_t capacity, std::string* why) {
        const std::uint64_t total = sizeof(Header) + capacity;
#ifdef _WIN32
        const std::string full = "Local\\" + name;
        h_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, DWORD(total >> 32), DWORD(total & 0xFFFFFFFFu), full.c_str());
        if (!h_) { if (why) *why = "CreateFileMapping " + full + " failed (" + std::to_string(GetLastError()) + ")"; return false; }
        void* p = MapViewOfFile(h_, FILE_MAP_ALL_ACCESS, 0, 0, SIZE_T(total));
        if (!p) { if (why) *why = "MapViewOfFile failed"; close(); return false; }
#else
        name_ = "/" + name;
        shm_unlink(name_.c_str());
        const int fd = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd < 0 || ftruncate(fd, off_t(total)) != 0) { if (fd >= 0) ::close(fd); if (why) *why = "shm_open " + name_ + " failed"; return false; }
        void* p = mmap(nullptr, size_t(total), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (p == MAP_FAILED) { if (why) *why = "mmap failed"; return false; }
        owner_ = true;
#endif
        size_ = total;
        hdr_ = static_cast<Header*>(p);
        std::memset(static_cast<void*>(hdr_), 0, sizeof(Header));
        hdr_->version = kVersion;
        hdr_->header_size = sizeof(Header);
        hdr_->capacity = capacity;
        hdr_->key_at.store(kNoKey);
#ifdef _WIN32
        hdr_->writer_pid.store(std::uint32_t(GetCurrentProcessId()));
#else
        hdr_->writer_pid.store(std::uint32_t(getpid()));
#endif
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(hdr_->magic, kMagic, 8);
        return true;
    }

    // false while the writer has not created it yet (callers retry)
    bool open(const std::string& name, std::string* why) {
#ifdef _WIN32
        const std::string full = "Local\\" + name;
        h_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, full.c_str());
        if (!h_) { if (why) *why = "no mapping " + full; return false; }
        void* p = MapViewOfFile(h_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!p) { if (why) *why = "MapViewOfFile failed"; close(); return false; }
        MEMORY_BASIC_INFORMATION mi{};
        VirtualQuery(p, &mi, sizeof mi);
        size_ = mi.RegionSize;
#else
        name_ = "/" + name;
        const int fd = shm_open(name_.c_str(), O_RDWR, 0600);
        if (fd < 0) { if (why) *why = "no shared memory " + name_; return false; }
        // mac3: the object's size from fstat, not lseek(SEEK_END): macOS does not seek on a POSIX shared-memory object
        // (so every reader failed to open: test_livelap on an M1); its st_size may be page-rounded (>= total)
        struct stat st {};
        const off_t n = fstat(fd, &st) == 0 ? st.st_size : off_t(-1);
        void* p = n > off_t(sizeof(Header)) ? mmap(nullptr, size_t(n), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
        ::close(fd);
        if (p == MAP_FAILED) { if (why) *why = "mmap failed"; return false; }
        size_ = std::uint64_t(n);
#endif
        hdr_ = static_cast<Header*>(p);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (std::memcmp(hdr_->magic, kMagic, 8) != 0 || hdr_->version != kVersion || hdr_->header_size != sizeof(Header) ||
            sizeof(Header) + hdr_->capacity > size_ || (hdr_->capacity & (hdr_->capacity - 1)) != 0) {
            if (why) *why = "not a version 1 b3live ring (or not initialised yet)";
            close();
            return false;
        }
        return true;
    }

    void close() {
#ifdef _WIN32
        if (hdr_) UnmapViewOfFile(hdr_);
        if (h_) CloseHandle(h_);
        h_ = nullptr;
#else
        if (hdr_) munmap(hdr_, size_t(size_));
        if (owner_) shm_unlink(name_.c_str());
        owner_ = false;
#endif
        hdr_ = nullptr;
    }

private:
    Header* hdr_ = nullptr;
    std::uint64_t size_ = 0;
#ifdef _WIN32
    HANDLE h_ = nullptr;
#else
    std::string name_;
    bool owner_ = false;
#endif
};

// False once the writer's process has ended (closed or not).
inline bool writer_alive(const Header* h) {
    const std::uint32_t pid = h->writer_pid.load(std::memory_order_relaxed);
    if (pid == 0) return true;
#ifdef _WIN32
    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!p) return false;
    const bool alive = WaitForSingleObject(p, 0) == WAIT_TIMEOUT;
    CloseHandle(p);
    return alive;
#else
    return kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

inline void ring_copy_in(std::uint8_t* data, std::uint64_t cap, std::uint64_t pos, const void* src, std::size_t n) {
    const std::uint64_t at = pos & (cap - 1);
    const std::size_t first = std::size_t(std::min<std::uint64_t>(n, cap - at));
    std::memcpy(data + at, src, first);
    if (first < n) std::memcpy(data, static_cast<const std::uint8_t*>(src) + first, n - first);
}
inline void ring_copy_out(const std::uint8_t* data, std::uint64_t cap, std::uint64_t pos, void* dst, std::size_t n) {
    const std::uint64_t at = pos & (cap - 1);
    const std::size_t first = std::size_t(std::min<std::uint64_t>(n, cap - at));
    std::memcpy(dst, data + at, first);
    if (first < n) std::memcpy(static_cast<std::uint8_t*>(dst) + first, data, n - first);
}

// Writer side: appends one record (body = a scene-capture record) and publishes it. Never blocks. False if the record
// cannot fit the ring at all (it is dropped; a reader then resyncs on the next keyframe).
inline bool publish(Mapping& m, std::uint32_t kind, const void* body, std::size_t n) {
    Header* h = m.header();
    const std::uint64_t cap = h->capacity;
    const std::uint64_t size = sizeof(RecordHead) + n, padded = (size + 7) & ~7ull;
    if (padded > cap / 2) return false;
    const std::uint64_t pos = h->head.load(std::memory_order_relaxed);
    h->reserved.store(pos + padded, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);   // `reserved` is visible before any byte is overwritten
    RecordHead rh{std::uint32_t(size), kind, now_ns()};
    ring_copy_in(m.data(), cap, pos, &rh, sizeof rh);
    ring_copy_in(m.data(), cap, pos + sizeof rh, body, n);
    if (kind == kKeyframe) {
        h->key_at.store(pos, std::memory_order_relaxed);
        h->keyframes.fetch_add(1, std::memory_order_relaxed);
    }
    h->records.fetch_add(1, std::memory_order_relaxed);
    h->head.store(pos + padded, std::memory_order_release);
    return true;
}

// Reader side: the next whole record after `pos`, copied out and checked against overwriting. Returns 1 with the record
// (pos advanced), 0 when nothing new is published, -1 when the reader was lapped (pos is then meaningless: resync).
inline int next_record(Mapping& m, std::uint64_t& pos, RecordHead& rh, std::string& body) {
    Header* h = m.header();
    const std::uint64_t cap = h->capacity;
    const std::uint64_t head = h->head.load(std::memory_order_acquire);
    if (pos == head) return 0;
    if (head - pos > cap || pos > head) return -1;
    ring_copy_out(m.data(), cap, pos, &rh, sizeof rh);
    const std::uint64_t padded = (std::uint64_t(rh.size) + 7) & ~7ull;
    if (rh.size < sizeof rh || pos + padded > head) return -1;   // a torn head (overwritten while read): resync
    body.resize(rh.size - sizeof rh);
    ring_copy_out(m.data(), cap, pos + sizeof rh, body.data(), body.size());
    std::atomic_thread_fence(std::memory_order_acquire);
    if (h->reserved.load(std::memory_order_relaxed) > pos + cap) return -1;   // overwritten while we copied
    pos += padded;
    return 1;
}

}  // namespace b3live

#endif
