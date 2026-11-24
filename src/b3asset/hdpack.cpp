#include "b3asset/hdpack.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <optional>
#include <thread>

#include "b3asset/bc.h"
#include "b3asset/hdcache.h"   // hdcache: the converted replacements on disk
#include "b3asset/export.h"
#include "b3asset/png.h"   // texpack: PNG replacements
#include "b3port/fileio.h"   // texpack: UTF-8 paths on Windows

namespace b3asset {

// ---- XXH3-64 (seed 0, default secret), from the xxHash specification ----

namespace {
using u64 = std::uint64_t;
using u32 = std::uint32_t;
using u8 = std::uint8_t;

constexpr u64 kP32_1 = 0x9E3779B1u, kP32_2 = 0x85EBCA77u, kP32_3 = 0xC2B2AE3Du;
constexpr u64 kP64_1 = 0x9E3779B185EBCA87ull, kP64_2 = 0xC2B2AE3D27D4EB4Full, kP64_3 = 0x165667B19E3779F9ull;
constexpr u64 kP64_4 = 0x85EBCA77C2B2AE63ull, kP64_5 = 0x27D4EB2F165667C5ull;
constexpr u64 kMX1 = 0x165667919E3779F9ull, kMX2 = 0x9FB21C651E98DF25ull;

constexpr u8 kSecret[192] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

inline u64 rd64(const u8* p) { u64 v; std::memcpy(&v, p, 8); return v; }  // little-endian hosts only (x86, arm64)
inline u32 rd32(const u8* p) { u32 v; std::memcpy(&v, p, 4); return v; }
inline u64 rotl64(u64 x, int r) { return (x << r) | (x >> (64 - r)); }
inline u64 bswap64(u64 x) {
    x = ((x & 0x00FF00FF00FF00FFull) << 8) | ((x >> 8) & 0x00FF00FF00FF00FFull);
    x = ((x & 0x0000FFFF0000FFFFull) << 16) | ((x >> 16) & 0x0000FFFF0000FFFFull);
    return (x << 32) | (x >> 32);
}
inline u64 mul_fold(u64 a, u64 b) {
    const unsigned __int128 r = static_cast<unsigned __int128>(a) * b;
    return u64(r) ^ u64(r >> 64);
}
inline u64 avalanche(u64 x) { x ^= x >> 37; x *= kMX1; return x ^ (x >> 32); }
inline u64 avalanche_xxh64(u64 x) { x ^= x >> 33; x *= kP64_2; x ^= x >> 29; x *= kP64_3; return x ^ (x >> 32); }
inline u64 mix16(const u8* d, const u8* s) { return mul_fold(rd64(d) ^ rd64(s), rd64(d + 8) ^ rd64(s + 8)); }

void accumulate(u64 acc[8], const u8* stripe, const u8* s) {
    for (int i = 0; i < 8; ++i) {
        const u64 d = rd64(stripe + 8 * i), v = d ^ rd64(s + 8 * i);
        acc[i ^ 1] += d;
        acc[i] += (v & 0xFFFFFFFFu) * (v >> 32);
    }
}
void scramble(u64 acc[8]) {
    for (int i = 0; i < 8; ++i) {
        acc[i] ^= acc[i] >> 47;
        acc[i] ^= rd64(kSecret + 128 + 8 * i);
        acc[i] *= kP32_1;
    }
}

u64 xxh3_large(const u8* p, std::size_t len) {
    u64 acc[8] = {kP32_3, kP64_1, kP64_2, kP64_3, kP64_4, kP32_2, kP64_5, kP32_1};
    constexpr std::size_t kStripes = (sizeof(kSecret) - 64) / 8, kBlock = 64 * kStripes;
    const std::size_t blocks = (len - 1) / kBlock;
    for (std::size_t b = 0; b < blocks; ++b) {
        for (std::size_t n = 0; n < kStripes; ++n) accumulate(acc, p + b * kBlock + n * 64, kSecret + n * 8);
        scramble(acc);
    }
    const std::size_t last = len - blocks * kBlock, stripes = (last - 1) / 64;
    for (std::size_t n = 0; n < stripes; ++n) accumulate(acc, p + blocks * kBlock + n * 64, kSecret + n * 8);
    accumulate(acc, p + len - 64, kSecret + sizeof(kSecret) - 71);
    u64 r = u64(len) * kP64_1;
    for (int i = 0; i < 4; ++i) r += mul_fold(acc[2 * i] ^ rd64(kSecret + 11 + 16 * i), acc[2 * i + 1] ^ rd64(kSecret + 19 + 16 * i));
    return avalanche(r);
}
}  // namespace

std::uint64_t xxh3_64(const void* data, std::size_t len) {
    const u8* p = static_cast<const u8*>(data);
    const u8* s = kSecret;
    if (len == 0) return avalanche_xxh64(rd64(s + 56) ^ rd64(s + 64));
    if (len <= 3) {
        const u32 c = u32(p[len - 1]) | u32(len) << 8 | u32(p[0]) << 16 | u32(p[len >> 1]) << 24;
        return avalanche_xxh64(u64(rd32(s) ^ rd32(s + 4)) ^ c);
    }
    if (len <= 8) {
        const u64 c = u64(rd32(p + len - 4)) | u64(rd32(p)) << 32;
        u64 v = (rd64(s + 8) ^ rd64(s + 16)) ^ c;
        v ^= rotl64(v, 49) ^ rotl64(v, 24);
        v *= kMX2;
        v ^= (v >> 35) + len;
        v *= kMX2;
        return v ^ (v >> 28);
    }
    if (len <= 16) {
        const u64 lo = (rd64(s + 24) ^ rd64(s + 32)) ^ rd64(p);
        const u64 hi = (rd64(s + 40) ^ rd64(s + 48)) ^ rd64(p + len - 8);
        return avalanche(u64(len) + bswap64(lo) + hi + mul_fold(lo, hi));
    }
    if (len <= 128) {
        u64 acc = u64(len) * kP64_1;
        const std::size_t rounds = ((len - 1) >> 5) + 1;
        for (std::size_t i = rounds; i-- > 0;) {
            acc += mix16(p + 16 * i, s + 32 * i);
            acc += mix16(p + len - 16 * i - 16, s + 32 * i + 16);
        }
        return avalanche(acc);
    }
    if (len <= 240) {
        u64 acc = u64(len) * kP64_1;
        const std::size_t chunks = len >> 4;
        for (std::size_t i = 0; i < 8; ++i) acc += mix16(p + 16 * i, s + 16 * i);
        acc = avalanche(acc);
        for (std::size_t i = 8; i < chunks; ++i) acc += mix16(p + 16 * i, s + 16 * (i - 8) + 3);
        acc += mix16(p + len - 16, s + 119);
        return avalanche(acc);
    }
    return xxh3_large(p, len);
}

// ---- keys ----

std::string hd_key_name(const HdKey& k) {
    char b[64];
    if (k.clut)
        std::snprintf(b, sizeof b, "%llx-%llx-%08x", (unsigned long long)k.tex, (unsigned long long)k.clut, k.bits);
    else
        std::snprintf(b, sizeof b, "%llx-%08x", (unsigned long long)k.tex, k.bits);
    return b;
}

namespace {
bool parse_hex(const std::string& s, u64& v, std::size_t max_digits) {
    if (s.empty() || s.size() > max_digits) return false;
    v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v << 4 | u64(d);
    }
    return true;
}
}  // namespace

bool parse_hd_name(const std::string& filename, HdKey& out) {
    const std::size_t dot = filename.find('.');
    if (dot == std::string::npos || dot == 0) return false;
    const std::string stem = filename.substr(0, dot);
    std::vector<std::string> parts;
    std::size_t a = 0;
    for (;;) {
        const std::size_t b = stem.find('-', a);
        parts.push_back(stem.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    if (parts.size() != 2 && parts.size() != 3) return false;
    u64 tex, clut = 0, bits;
    if (!parse_hex(parts[0], tex, 16) || !parse_hex(parts.back(), bits, 8)) return false;
    if (parts.size() == 3 && !parse_hex(parts[1], clut, 16)) return false;
    out.tex = tex;
    out.clut = clut;
    out.bits = u32(bits) & ~(1u << 14);
    return true;
}

namespace {
struct PsmShape { int bsx = 8, bsy = 8; int pal = 0; bool full = true; };
PsmShape shape(int psm) {
    PsmShape s;   // PCSX2 GSLocalMemory m_psm: blocks 8x8 unless below; fmsk full unless below
    switch (psm) {
    case PSMCT16: case PSMCT16S: s.bsx = 16; s.full = false; break;
    case PSMCT24: s.full = false; break;
    case PSMT8: s.bsx = s.bsy = 16; s.pal = 256; break;
    case PSMT4: s.bsx = 32; s.bsy = 16; s.pal = 16; break;
    case PSMT8H: s.pal = 256; s.full = false; break;
    case PSMT4HL: case PSMT4HH: s.pal = 16; s.full = false; break;
    default: break;
    }
    return s;
}
}  // namespace

bool hd_texture_bytes(const GsMemory& gs, int psm, int tw_log2, int th_log2, std::uint32_t tbp, std::uint32_t tbw,
                      std::vector<std::uint8_t>& bytes) {
    const PsmShape s = shape(psm);
    if (!psm_bits(psm) || tw_log2 > 10 || th_log2 > 10) return false;
    const std::uint32_t tw = 1u << tw_log2, th = 1u << th_log2;
    if (tw < std::uint32_t(s.bsx) || th < std::uint32_t(s.bsy) || !s.full) {
        // expanded: index bytes for the paletted formats, the 32-bit texel for CT32
        if (s.pal) {
            for (std::uint32_t y = 0; y < th; ++y)
                for (std::uint32_t x = 0; x < tw; ++x) bytes.push_back(std::uint8_t(gs.read(psm, tbp, tbw, x, y)));
            return true;
        }
        if (psm != PSMCT32) return false;   // CT24 / CT16 expand with TEXA: not needed by the packs seen so far
        for (std::uint32_t y = 0; y < th; ++y)
            for (std::uint32_t x = 0; x < tw; ++x) {
                const std::uint32_t v = gs.read(psm, tbp, tbw, x, y);
                for (int k = 0; k < 4; ++k) bytes.push_back(std::uint8_t(v >> (8 * k)));
            }
        return true;
    }
    const std::uint8_t* m = gs.data();
    for (std::uint32_t by = 0; by < th; by += s.bsy)
        for (std::uint32_t bx = 0; bx < tw; bx += s.bsx) {
            std::uint32_t a = GsMemory::address(psm, tbp, tbw, bx, by);
            if (psm == PSMT4) a >>= 1;
            a &= ~255u;
            bytes.insert(bytes.end(), m + a, m + a + 256);
        }
    return true;
}

bool hd_key(const GsMemory& gs, const RasterHeader& h, std::uint32_t cbp_add, int levels, HdKey& out, int first) {
    const int psm = h.psm();
    const PsmShape s = shape(psm);
    const int nlev = int(std::min<std::uint32_t>(h.levels, 7));
    if (first < 0 || first >= nlev) return false;
    // GSState::GetTex0Layer: a level's TEX0 = TBP/TBW from MIPTBP1/2, TW/TH reduced by the level (not below 0)
    const int tw = std::max(int(h.tex0 >> 26 & 0xF) - first, 0), th = std::max(int(h.tex0 >> 30 & 0xF) - first, 0);
    std::uint32_t tbp[7], tbw[7];
    tbp[0] = h.tex0 & 0x3FFF;
    tbw[0] = (h.tex0 >> 14) & 0x3F;
    for (int i = 0; i < 3; ++i) {
        tbp[1 + i] = (h.miptbp1 >> (20 * i)) & 0x3FFF;
        tbw[1 + i] = (h.miptbp1 >> (20 * i + 14)) & 0x3F;
        tbp[4 + i] = (h.miptbp2 >> (20 * i)) & 0x3FFF;
        tbw[4 + i] = (h.miptbp2 >> (20 * i + 14)) & 0x3F;
    }
    std::vector<std::uint8_t> bytes;
    levels = std::clamp(levels, 1, nlev - first);
    for (int l = 0; l < levels; ++l)
        if (!hd_texture_bytes(gs, psm, std::max(tw - l, 0), std::max(th - l, 0), tbp[first + l], tbw[first + l], bytes)) return false;
    out = HdKey{};
    out.tex = xxh3_64(bytes.data(), bytes.size());
    out.bits = std::uint32_t(psm) | std::uint32_t(tw) << 6 | std::uint32_t(th) << 10;
    if (psm == PSMCT24 || psm == PSMCT16 || psm == PSMCT16S)
        out.bits |= 0x80u << 24;   // TEXA TA1 = 0x80, TA0 = 0, AEM = 0 [G: RW sky2 defaults]
    if (s.pal) {
        const int cpsm = h.cpsm();
        if (cpsm != PSMCT32 && cpsm != PSMCT24) return false;   // CT16 palettes (Expand16 with TEXA): not seen yet
        if ((h.tex0 >> 55) & 1) return false;                   // CSM2: not seen yet
        const std::uint32_t csa = (h.tex0 >> 56) & 0x1F, cbp = h.pal_offset + cbp_add;
        std::uint32_t clut[256];
        for (int i = 0; i < s.pal; ++i) {
            // CLD loads the palette from CBP into the CLUT buffer (CSM1: 16x16 / 8x2, index bits 3 and 4 swapped);
            // CSA offsets the read inside the buffer (T8: clamped to the last group [PCSX2 ReadCLUT_T32_I8]).
            std::uint32_t e;
            if (s.pal == 16) e = std::uint32_t(i);
            else e = std::min<std::uint32_t>((std::uint32_t(i) & 0xF0) + ((csa & 15) << 4), 240) + (std::uint32_t(i) & 15);
            const std::uint32_t j = (e & 0xE7) | ((e & 0x08) << 1) | ((e & 0x10) >> 1);
            clut[i] = gs.read(PSMCT32, cbp, 1, j & 15, j >> 4);
        }
        out.clut = xxh3_64(clut, sizeof(std::uint32_t) * std::size_t(s.pal));
    }
    return true;
}

namespace {
// Walks the candidates in priority order (named level j, then hashed range f..f+c-1), each range hashed once; `fn`
// returns true to stop.
template <class Fn>
void for_each_candidate(const GsMemory& gs, const RasterHeader& h, std::uint32_t cbp_add, Fn fn) {
    const int nlev = int(std::min<std::uint32_t>(std::max<std::uint32_t>(h.levels, 1), 7));
    const int tw = int(h.tex0 >> 26 & 0xF), th = int(h.tex0 >> 30 & 0xF);
    HdKey memo[7][8];
    bool have[7][8] = {};
    for (int j = 0; j < nlev; ++j)
        for (int f = j; f < nlev; ++f)
            for (int c = 1; f + c <= nlev; ++c) {
                if (!have[f][c]) {
                    if (!hd_key(gs, h, cbp_add, c, memo[f][c], f)) return;
                    have[f][c] = true;
                }
                HdCandidate k;
                k.key = memo[f][c];
                k.key.bits = (k.key.bits & ~0x3FC0u) | std::uint32_t(std::max(tw - j, 0)) << 6 | std::uint32_t(std::max(th - j, 0)) << 10;
                k.level = j;
                k.first = f;
                k.count = c;
                if (fn(k)) return;
            }
}
}  // namespace

std::vector<HdCandidate> hd_candidate_keys(const GsMemory& gs, const RasterHeader& h, std::uint32_t cbp_add) {
    std::vector<HdCandidate> out;
    for_each_candidate(gs, h, cbp_add, [&](const HdCandidate& k) { out.push_back(k); return false; });
    return out;
}

const std::string* hd_lookup(const HdPack& pack, const GsMemory& gs, const RasterHeader& h, std::uint32_t cbp_add, HdCandidate* which) {
    if (pack.files.empty()) return nullptr;
    const std::string* found = nullptr;
    for_each_candidate(gs, h, cbp_add, [&](const HdCandidate& k) {
        found = pack.find(k.key);
        if (found && which) *which = k;
        return found != nullptr;
    });
    return found;
}

// ---- the replacement hook ----

namespace {
HdTextures* g_hd = nullptr;
double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
bool g_compressed = false;           // hdgpu: set_hd_compressed
thread_local bool t_compress = false;   // hdgpu: HdCompressScope
thread_local bool t_no_hook = false;    // hdgpu: prefetch's plain decodes (this thread only: the hook stays for the others)
thread_local int t_rows = 8;            // hdgpu: threads hd_rows and the chain builders use (1 on prefetch's own threads)
std::atomic<bool> g_encode{false};      // texpack: HD TEXTURE MEMORY Compressed (set_hd_encode)
std::atomic<std::size_t> g_mips_rebuilt{0};   // hdmips (HdTextures::mips_rebuilt)
std::atomic<std::size_t> g_bc7_mips_unused{0};   // texpack: BC7 files whose stored mips are not used (HdTextures::bc7_mips_unused)
std::atomic<std::size_t> g_encoded_files{0}, g_encoded_bytes{0};   // texpack: PNG / BC7 files encoded at load

// texpack: the RGBA8 decodes the sources hold, within a budget ($B3_HD_DECODED_MB, default 512): the oldest are dropped
// (a later need decodes the file's bytes again). Before, every PNG source kept its full-size decode for the run.
struct DecodedLru {
    std::mutex m;
    // one node per source holding a decode (Source::lru_node): taken out when its decode is dropped or the source goes,
    // so the total stays the decodes held and a later pop never frees a newer decode
    std::list<std::pair<const HdTextures::Source*, std::size_t>> order;
    std::size_t total = 0;
    std::uint64_t evicted = 0;   // vpreload: decodes dropped for the budget so far (hd_decoded_stats)
    std::size_t budget() {
        static const std::size_t b = [] {
            const char* e = std::getenv("B3_HD_DECODED_MB");
            const long mb = e ? std::atol(e) : 512;
            return std::size_t(mb > 0 ? mb : 512) << 20;
        }();
        return b;
    }
};
// hdexit (2026-11-23): never destroyed. HdTextures is a static (hd_enable) whose Sources unlist themselves from this list in
// their destructors; this list is made later (the first decode), so at exit it was destroyed first and those erases
// wrote into freed memory: "corrupted size vs. prev_size while consolidating" (SIGSEGV / SIGABRT) at the end of a run
// with an HD pack. A leaked singleton outlives every Source, whatever the order of the statics.
DecodedLru& decoded_lru() { static DecodedLru* const l = new DecodedLru; return *l; }

// texpack review: at most this many PNG / BC7 files decoded at once (each needs its full RGBA8 picture for a while, out of
// the budget above: 16 MB a 2048 picture, 64 MB a 4096 one)
struct DecodeGate {
    std::mutex m;
    std::condition_variable cv;
    int busy = 0;
    static constexpr int kMax = 4;
};
DecodeGate& decode_gate() { static DecodeGate g; return g; }
struct DecodeSlot {
    DecodeSlot() {
        DecodeGate& g = decode_gate();
        std::unique_lock<std::mutex> lk(g.m);
        g.cv.wait(lk, [&] { return g.busy < DecodeGate::kMax; });
        ++g.busy;
    }
    ~DecodeSlot() {
        DecodeGate& g = decode_gate();
        { const std::lock_guard<std::mutex> lk(g.m); --g.busy; }
        g.cv.notify_one();
    }
};

// hdcache (include/b3asset/hdcache.h): the converted replacements on disk. One worker writes the entries (whole, to a
// temporary name, then renamed), touches the ones read (their time is the LRU's) and holds the folder under its cap.
// Leaked, as decoded_lru: the HdTextures static may still use it while the statics go.
struct DiskEntry {
    std::vector<Image> levels;   // apply's result: one RGBA8 level, or the BC chain
    bool masked = false;         // the alpha rule changed texels (HdTextures::masked / encoded count it on a hit too)
};
struct DiskCache {
    std::mutex m;
    std::condition_variable cv, idle;
    std::filesystem::path dir;   // empty: off
    std::uint64_t cap = 0;
    struct Job { std::filesystem::path path; std::uint64_t key = 0, check = 0; std::shared_ptr<const DiskEntry> e; };
    std::deque<Job> q;           // entries to write (e null: a touch)
    std::size_t busy = 0;        // jobs taken and not done (flush)
    std::uint64_t pending = 0;   // the queued entries' bytes (held in memory until written: a cap, see disk_queue)
    bool started = false, scanned = false;
    std::uint64_t total = 0;     // the folder's size (after the first scan)
    std::atomic<std::uint64_t> hits{0}, misses{0}, written{0}, written_bytes{0}, corrupt{0}, evicted{0}, failed{0}, read_ns{0}, read_bytes{0};
};
DiskCache& disk() { static DiskCache* const d = new DiskCache; return *d; }

std::filesystem::path disk_fs(const std::string& s) {
#ifdef _WIN32
    return b3io::u8path(s);
#else
    return std::filesystem::path(s);
#endif
}

template <class T> void put(std::string& o, const T& v) { o.append(reinterpret_cast<const char*>(&v), sizeof v); }
template <class T> bool get(const std::uint8_t*& p, const std::uint8_t* end, T& v) {
    if (std::size_t(end - p) < sizeof v) return false;
    std::memcpy(&v, p, sizeof v);
    p += sizeof v;
    return true;
}

// The entry's bytes: magic, format, flags, key, check, level count; per level w, h, bc, byte count; the levels' bytes;
// then the size of all before and its xxh3 (read_entry checks both)
std::string entry_bytes(std::uint64_t key, std::uint64_t check, const DiskEntry& e) {
    std::string o;
    o.append(hdcache::kMagic, 8);
    put(o, hdcache::kFormat);
    put(o, std::uint32_t(e.masked ? 1u : 0u));
    put(o, key);
    put(o, check);
    put(o, std::uint32_t(e.levels.size()));
    put(o, std::uint32_t(0));
    for (const Image& l : e.levels) {
        put(o, std::int32_t(l.w));
        put(o, std::int32_t(l.h));
        put(o, std::int32_t(l.bc));
        put(o, std::int32_t(0));
        put(o, std::uint64_t(l.bc ? (l.blocks ? l.blocks->size() : 0) : l.rgba.size()));
    }
    for (const Image& l : e.levels) {
        if (l.bc) { if (l.blocks) o.append(reinterpret_cast<const char*>(l.blocks->data()), l.blocks->size()); }
        else o.append(reinterpret_cast<const char*>(l.rgba.data()), l.rgba.size());
    }
    put(o, std::uint64_t(o.size()));
    put(o, xxh3_64(o.data(), o.size() - 8));
    return o;
}
// false: not this key's entry, or damaged (short, a wrong size or hash)
bool read_entry(const std::vector<std::uint8_t>& d, std::uint64_t key, std::uint64_t check, DiskEntry& e) {
    if (d.size() < 8 + 4 + 4 + 8 + 8 + 8 + 16 || std::memcmp(d.data(), hdcache::kMagic, 8) != 0) return false;
    const std::size_t body = d.size() - 16;
    std::uint64_t n = 0, h = 0;
    std::memcpy(&n, d.data() + body, 8);
    std::memcpy(&h, d.data() + body + 8, 8);
    if (n != body || h != xxh3_64(d.data(), body)) return false;
    const std::uint8_t* p = d.data() + 8;
    const std::uint8_t* const end = d.data() + body;
    std::uint32_t fmt = 0, flags = 0, count = 0, pad = 0;
    std::uint64_t k = 0, c = 0;
    if (!get(p, end, fmt) || !get(p, end, flags) || !get(p, end, k) || !get(p, end, c) || !get(p, end, count) || !get(p, end, pad)) return false;
    if (fmt != hdcache::kFormat || k != key || c != check || count == 0 || count > 32) return false;
    struct L { std::int32_t w, h, bc, pad; std::uint64_t bytes; };
    std::vector<L> ls(count);
    for (L& l : ls)
        if (!get(p, end, l.w) || !get(p, end, l.h) || !get(p, end, l.bc) || !get(p, end, l.pad) || !get(p, end, l.bytes)) return false;
    e.levels.clear();
    e.masked = flags & 1u;
    for (const L& l : ls) {
        if (l.w <= 0 || l.h <= 0 || l.bytes > std::uint64_t(end - p)) return false;
        Image im;
        im.w = l.w;
        im.h = l.h;
        im.bc = l.bc;
        if (l.bc) im.blocks = std::make_shared<const std::vector<std::uint8_t>>(p, p + l.bytes);
        else {
            if (l.bytes != std::uint64_t(l.w) * std::uint64_t(l.h) * 4u) return false;
            im.rgba.assign(p, p + l.bytes);
        }
        p += l.bytes;
        e.levels.push_back(std::move(im));
    }
    return p == end;
}

std::uint64_t entry_size(const DiskEntry& e) {
    std::uint64_t n = 0;
    for (const Image& l : e.levels) n += l.bc ? (l.blocks ? l.blocks->size() : 0) : l.rgba.size();
    return n;
}

std::filesystem::path entry_path(const std::filesystem::path& dir, std::uint64_t key) {
    char b[32];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(key));
    return dir / std::string(b, 2) / (std::string(b) + hdcache::kExtension);
}

// The folder's entries by age, the oldest dropped until under 90 % of the cap (the worker, the lock not held)
std::uint64_t disk_evict(const std::filesystem::path& dir, std::uint64_t cap, bool evict, std::uint64_t& gone) {
    namespace fs = std::filesystem;
    struct F { fs::file_time_type t; std::uint64_t n; fs::path p; };
    std::vector<F> all;
    std::uint64_t total = 0;
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const fs::path& p = it->path();
        const fs::file_time_type t = it->last_write_time(e2);
        if (p.filename().string().find(".tmp") != std::string::npos) {   // a write a crash left: gone after an hour
            if (!e2 && now - t > std::chrono::hours(1)) fs::remove(p, e2);
            continue;
        }
        if (p.extension() != hdcache::kExtension) continue;
        const std::uint64_t n = it->file_size(e2);
        if (e2) continue;
        total += n;
        all.push_back({t, n, p});
    }
    if (!evict || total <= cap) return total;
    std::sort(all.begin(), all.end(), [](const F& a, const F& b) { return a.t < b.t; });
    for (const F& f : all) {
        if (total <= cap / 10 * 9) break;
        std::error_code e3;
        if (fs::remove(f.p, e3)) { total -= std::min(total, f.n); ++gone; }
    }
    return total;
}

void disk_worker() {
    DiskCache& d = disk();
    std::unique_lock<std::mutex> lk(d.m);
    for (;;) {
        d.cv.wait(lk, [&] { return !d.q.empty() || !d.scanned; });
        if (!d.scanned) {   // the folder's size once, the cap kept
            const std::filesystem::path dir = d.dir;
            const std::uint64_t cap = d.cap;
            lk.unlock();
            std::uint64_t gone = 0;
            const std::uint64_t total = disk_evict(dir, cap, true, gone);
            lk.lock();
            d.total = total;
            d.evicted += gone;
            d.scanned = true;
            continue;
        }
        DiskCache::Job j = std::move(d.q.front());
        d.q.pop_front();
        ++d.busy;
        const std::uint64_t held = j.e ? entry_size(*j.e) : 0;
        lk.unlock();
        namespace fs = std::filesystem;
        std::error_code ec;
        std::uint64_t added = 0;
        if (!j.e) {   // a touch: read just now (the LRU's age)
            fs::last_write_time(j.path, fs::file_time_type::clock::now(), ec);
        } else if (!fs::exists(j.path, ec)) {
            const std::string bytes = entry_bytes(j.key, j.check, *j.e);
            fs::create_directories(j.path.parent_path(), ec);
            static std::atomic<unsigned> serial{0};
            fs::path tmp = j.path;
            tmp += ".tmp" + std::to_string(std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFF)) + "_" +
                   std::to_string(serial.fetch_add(1));
            if (b3io::write_synced(tmp, bytes, nullptr, false) && b3io::commit_replace(tmp, j.path, nullptr, false)) {
                added = bytes.size();
                ++d.written;
                d.written_bytes += bytes.size();
            } else {
                fs::remove(tmp, ec);   // (another game wrote it first, or the disk is full: converted again next time)
                ++d.failed;
            }
        }
        lk.lock();
        d.total += added;
        if (added && d.total > d.cap) {   // over the cap: the oldest go
            const std::filesystem::path dir = d.dir;
            const std::uint64_t cap = d.cap;
            lk.unlock();
            std::uint64_t gone = 0;
            const std::uint64_t total = disk_evict(dir, cap, true, gone);
            lk.lock();
            d.total = total;
            d.evicted += gone;
        }
        --d.busy;
        d.pending -= std::min(d.pending, held);
        if (d.q.empty() && !d.busy) d.idle.notify_all();
    }
}

void disk_queue(DiskCache::Job j) {
    DiskCache& d = disk();
    {
        const std::lock_guard<std::mutex> lk(d.m);
        if (d.dir.empty()) return;
        // at most 512 MB waiting (an RGBA8 4096 x 4096 result is 64 MB): past it a result is not kept (a slow disk; it is
        // converted again next time)
        const std::uint64_t n = j.e ? entry_size(*j.e) : 0;
        if (j.e && d.pending + n > (std::uint64_t(512) << 20) && d.pending) return;
        d.pending += n;
        d.q.push_back(std::move(j));
        if (!d.started) {
            d.started = true;
            std::thread(disk_worker).detach();
        }
    }
    d.cv.notify_one();
}
}  // namespace

// hdgpu: a replacement file as read: its bytes (the BC blocks are used from here) and, when the RGBA8 path or the alpha
// rule needs it, the top level decoded (decode_dds; texpack: a PNG's, or the blocks an encoded PNG / BC7 became), held
// within the decoded budget (DecodedLru: dropped, it is decoded again from `data`)
struct HdTextures::Source : std::enable_shared_from_this<HdTextures::Source> {
    std::vector<std::uint8_t> data;
    DdsInfo info;
    bool opaque_file = false;   // texpack: a PNG without alpha (the original's alpha is taken: replace_)
    bool png = false;           // texpack: `data` is the PNG file
    bool raw_blocks = false;    // texpack: `data` is BC blocks (info.at 0): a PNG / BC7 encoded at load (HD TEXTURE MEMORY)
    mutable std::mutex m;
    mutable std::shared_ptr<const Image> rgba;
    std::shared_ptr<const Image> decoded() const {
        std::shared_ptr<const Image> out;
        std::size_t bytes = 0;
        {
            const std::lock_guard<std::mutex> lk(m);
            if (rgba) return rgba;
            auto im = std::make_shared<Image>();
            if (png) {
                if (!decode_replacement(data.data(), data.size(), *im)) return nullptr;
            } else if (raw_blocks) {
                im->w = info.w;
                im->h = info.h;
                im->rgba.resize(std::size_t(info.w) * std::size_t(info.h) * 4);
                bc_decode(info.fmt, data.data(), info.w, info.h, im->rgba.data());
                for (std::size_t i = 3; i < im->rgba.size(); i += 4) im->rgba[i] = im->rgba[i] >= 0x80 ? 0xFF : std::uint8_t(im->rgba[i] * 2);
            } else if (!decode_dds(data.data(), data.size(), *im)) {
                return nullptr;
            }
            bytes = im->rgba.size();
            rgba = im;
            out = std::move(im);
        }
        keep(bytes);
        return out;
    }
    void drop() const {   // the decode goes (and its place in the budget)
        unlist();
        const std::lock_guard<std::mutex> lk(m);
        rgba.reset();
    }
    ~Source() { unlist(); }
    // the budget's node of this source's decode (DecodedLru's lock): at most one
    mutable bool listed = false;
    mutable std::list<std::pair<const Source*, std::size_t>>::iterator node;
    void unlist() const {
        DecodedLru& l = decoded_lru();
        const std::lock_guard<std::mutex> lk(l.m);
        if (!listed) return;
        l.total -= std::min(node->second, l.total);
        l.order.erase(node);
        listed = false;
    }
    void keep(std::size_t bytes) const {   // the decode joins the budget (as the newest); the oldest others go
        DecodedLru& l = decoded_lru();
        std::vector<std::shared_ptr<const Source>> gone;
        {
            const std::lock_guard<std::mutex> lk(l.m);
            if (listed) {
                l.total -= std::min(node->second, l.total);
                l.order.erase(node);
            }
            node = l.order.emplace(l.order.end(), this, bytes);
            listed = true;
            l.total += bytes;
            while (l.total > l.budget() && l.order.size() > 1) {
                const auto [src, n] = l.order.front();
                l.order.pop_front();
                ++l.evicted;
                l.total -= std::min(n, l.total);
                src->listed = false;   // (the source is alive while listed: its destructor unlists it under this lock)
                if (auto sp = src->weak_from_this().lock()) gone.push_back(std::move(sp));
            }
        }
        for (const auto& sp : gone) {   // dropped outside the budget's lock (a decode in use stays alive in its user's pointer)
            const std::lock_guard<std::mutex> lk(sp->m);
            sp->rgba.reset();
        }
    }
};

void set_hd_compressed(bool on) { g_compressed = on; }
void set_hd_encode(bool on) { g_encode = on; }
bool hd_encode() { return g_encode; }
bool hd_compressed() { return g_compressed; }
HdCompressScope::HdCompressScope() : saved(t_compress) { t_compress = true; }
HdCompressScope::~HdCompressScope() { t_compress = saved; }

namespace {
std::shared_ptr<const HdTextures::Source> read_source_(const std::string& path, bool decode);
// texpack review: on the loader's threads a file too large for the memory left is logged and skipped (the original kept)
std::shared_ptr<const HdTextures::Source> read_source(const std::string& path, bool decode) {
    try {
        return read_source_(path, decode);
    } catch (const std::bad_alloc&) {
        std::fprintf(stderr, "hd pack: %s: out of memory reading it: skipped\n", path.c_str());
        return nullptr;
    }
}
std::shared_ptr<const HdTextures::Source> read_source_(const std::string& path, bool decode) {
    auto s = std::make_shared<HdTextures::Source>();
    if (!read_file(path, s->data)) return nullptr;
    std::string why;
    const bool png = s->data.size() >= 8 && s->data[0] == 0x89 && s->data[1] == 'P' && s->data[2] == 'N' && s->data[3] == 'G';
    if (!png && !parse_dds(s->data.data(), s->data.size(), s->info, &why)) {   // texpack: the reason, not a silent failure
        std::fprintf(stderr, "hd pack: %s: %s\n", path.c_str(), why.c_str());
        return nullptr;
    }
    if (!png && s->info.fmt == kBC7 && s->info.levels > 1) {
        // texpack (with hdmips): a BC7 file's stored mips are not read (it takes the RGBA8 path, or is encoded, from level 0):
        // reported as stored mips not used, counted on their own (bc7_mips_unused); its levels are built from level 0
        const std::size_t n = g_bc7_mips_unused.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 20)
            std::fprintf(stderr, "hd pack: %s: %d stored mip levels in a BC7 file are not used: levels 1 and below built from level 0 "
                         "instead%s\n", path.c_str(), s->info.levels - 1, n == 20 ? " (no more of these lines)" : "");
        s->info.levels = 1;
    }
    if (png || s->info.fmt == kBC7) {
        // texpack: a PNG or a BC7 file. HD TEXTURE MEMORY Compressed (with a BC device): encoded once here to BC1 (opaque) /
        // BC3 and then used as a BC file (the PS2 alpha mapped on the BC path, the alpha rule as for any file); else the
        // RGBA8 path, the file's bytes kept to decode again when the decode is dropped (DecodedLru)
        const DecodeSlot slot;   // (at most DecodeGate::kMax of these pictures at once)
        Image raw;   // the file's texels, alpha as stored
        if (png) {
            bool has_alpha = true;
            if (!decode_png(s->data.data(), s->data.size(), raw.w, raw.h, raw.rgba, &why, &has_alpha)) {
                std::fprintf(stderr, "hd pack: %s: %s\n", path.c_str(), why.c_str());
                return nullptr;
            }
            s->opaque_file = !has_alpha;
        } else {
            raw.w = s->info.w;
            raw.h = s->info.h;
            raw.rgba.resize(std::size_t(raw.w) * std::size_t(raw.h) * 4);
            bc_decode(kBC7, s->data.data() + s->info.at, raw.w, raw.h, raw.rgba.data());
        }
        if (g_compressed && g_encode) {
            bool opaque = true;
            for (std::size_t i = 3; i < raw.rgba.size() && opaque; i += 4) opaque = raw.rgba[i] == 0xFF;
            const int fmt = opaque ? kBC1 : kBC3;
            std::vector<std::uint8_t> blocks;
            bc_encode(fmt, raw.rgba.data(), raw.w, raw.h, blocks, t_rows);
            s->data = std::move(blocks);
            s->info = DdsInfo{fmt, raw.w, raw.h, 1, 0};
            s->raw_blocks = true;
            ++g_encoded_files;
            g_encoded_bytes += s->data.size();
            if (decode && !s->decoded()) return nullptr;
            return s;
        }
        if (png) {
            s->png = true;
            s->info = DdsInfo{kDdsRGBA, raw.w, raw.h, 1, 0};
        }
        auto im = std::make_shared<Image>(std::move(raw));
        for (std::size_t i = 3; i < im->rgba.size(); i += 4) im->rgba[i] = im->rgba[i] >= 0x80 ? 0xFF : std::uint8_t(im->rgba[i] * 2);   // (ps2_alpha)
        const std::size_t bytes = im->rgba.size();
        s->rgba = std::move(im);
        s->keep(bytes);
        return s;
    }
    if ((decode || s->info.fmt >= kDdsRGBA) && !s->decoded()) return nullptr;
    return s;
}

HdTextures::Job job_of(const RasterHeader& h) {
    HdTextures::Job j;
    j.width = int(h.width);
    j.height = int(h.height);
    j.tw = int(h.tex0 >> 26 & 0xF);
    j.th = int(h.tex0 >> 30 & 0xF);
    j.tcc = h.tcc();
    return j;
}

// The alpha rule (apply, below) may change the replacement: TCC 1, the pack's alpha not taken raw, and the original has
// alpha below 1/4 somewhere or a maximum below 0x80. (Holds for any rule that cuts only where the original's bilinear
// alpha is below 64 and clamps to its maximum: the bilinear of texels >= 64 is >= 64.)
bool rule_may_change(const DecodedRaster& r) {
    if (r.max_alpha < 0x80) return true;
    const std::vector<std::uint8_t>& o = r.levels[0].rgba;
    for (std::size_t i = 3; i < o.size(); i += 4)
        if (o[i] < 64) return true;
    return false;
}

std::string chain_key(const std::string& path, const HdTextures::Job& j) {
    char b[96];
    std::snprintf(b, sizeof b, "|%dx%d/%dx%d/%d", j.width, j.height, j.tw, j.th, j.tcc ? 1 : 0);
    return path + b;
}
std::string rule_key(const std::string& path, const HdTextures::Job& j, const DecodedRaster& r) {
    const Image& o = r.levels[0];
    char b[96];
    std::snprintf(b, sizeof b, "|rule %d %dx%d %016llx", int(r.max_alpha), o.w, o.h,
                  static_cast<unsigned long long>(xxh3_64(o.rgba.data(), o.rgba.size())));
    return chain_key(path, j) + b;
}

std::size_t chain_bytes(const std::vector<Image>& c) {
    std::size_t n = 0;
    for (const Image& l : c) n += l.blocks ? l.blocks->size() : 0;
    return n;
}

// One stored BC level for the GPU, with the alpha as decode_dds maps it (`tcc` 0: opaque). `to_bc3`: a BC1 level as BC3.
void convert_level(int fmt, const std::uint8_t* src, int src_bw, int bw, int bh, bool tcc, bool to_bc3, std::vector<std::uint8_t>& out) {
    const std::size_t ssz = bc_block_bytes(fmt), osz = to_bc3 ? 16 : ssz;
    out.resize(std::size_t(bw) * bh * osz);
    std::uint8_t px[16][4];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const std::uint8_t* b = src + (std::size_t(by) * src_bw + bx) * ssz;
            std::uint8_t* o = out.data() + (std::size_t(by) * bw + bx) * osz;
            if (fmt == kBC1) {
                const bool three = (b[0] | b[1] << 8) <= (b[2] | b[3] << 8);
                if (to_bc3) {   // the colour half decodes in 4-colour mode: a 3-colour block is fitted again
                    if (!three) {
                        static const std::uint8_t opaque[8] = {255, 255, 0, 0, 0, 0, 0, 0};
                        std::memcpy(o, opaque, 8);
                        std::memcpy(o + 8, b, 8);
                        continue;
                    }
                    bc_decode_block(kBC1, b, px);
                    std::uint8_t a[16];
                    for (int i = 0; i < 16; ++i) a[i] = px[i][3];
                    bc_encode_alpha(a, o);
                    bc_encode_color(px, o + 8, false);
                    continue;
                }
                std::memcpy(o, b, 8);
                if (!tcc && three) {   // a transparent texel would stay transparent: opaque black, as the decode forces it
                    bool hole = false;
                    for (int i = 0; i < 16; ++i) hole |= ((b[4 + i / 4] >> (2 * (i & 3))) & 3) == 3;
                    if (hole) {
                        bc_decode_block(kBC1, b, px);
                        for (auto& t : px) t[3] = 255;
                        bc_encode_color(px, o, false);
                    }
                }
                continue;
            }
            std::memcpy(o + 8, b + 8, 8);   // the colour half as stored
            if (!tcc) {
                if (fmt == kBC2) std::memset(o, 0xFF, 8);
                else { static const std::uint8_t opaque[8] = {255, 255, 0, 0, 0, 0, 0, 0}; std::memcpy(o, opaque, 8); }
                continue;
            }
            if (fmt == kBC2) {   // 4-bit a: a * 17 -> PS2 map -> (a >= 8 ? 15 : 2a) * 17, exact
                for (int i = 0; i < 8; ++i) {
                    const int lo = b[i] & 15, hi = b[i] >> 4;
                    o[i] = std::uint8_t((lo >= 8 ? 15 : 2 * lo) | (hi >= 8 ? 15 : 2 * hi) << 4);
                }
                continue;
            }
            if (b[0] <= 128 && b[1] <= 128) {   // endpoints doubled, indices kept: within one level of the map
                std::memcpy(o, b, 8);
                o[0] = std::uint8_t(std::min(2 * b[0], 255));
                o[1] = std::uint8_t(std::min(2 * b[1], 255));
                continue;
            }
            bc_decode_block(kBC3, b, px);
            std::uint8_t a[16];
            for (int i = 0; i < 16; ++i) a[i] = px[i][3] >= 0x80 ? 0xFF : std::uint8_t(px[i][3] * 2);
            bc_encode_alpha(a, o);
        }
}

// The rest of a chain from its last level: decoded, halved like the GPU's blit, encoded again, down to 1 x 1.
void finish_chain(std::vector<Image>& c, Image last, int fmt, int threads, std::size_t* built) {
    while (last.w > 1 || last.h > 1) {
        Image next;
        mip_down(last, next);
        Image l;
        l.w = next.w;
        l.h = next.h;
        l.bc = fmt;
        auto b = std::make_shared<std::vector<std::uint8_t>>();
        bc_encode(fmt, next.rgba.data(), next.w, next.h, *b, threads);
        l.blocks = std::move(b);
        c.push_back(std::move(l));
        last = std::move(next);
        if (built) ++*built;
    }
}
}  // namespace

namespace {
// hdgpu: fn(first, end) over [0, n) in strips on up to t_rows threads (this one included)
template <class Fn>
void hd_rows(int n, Fn fn) {
    const int k = std::clamp(n / 64, 1, t_rows);
    std::vector<std::thread> pool;
    for (int i = 1; i < k; ++i) pool.emplace_back([&fn, n, k, i] { fn(n * i / k, n * (i + 1) / k); });
    fn(0, n / k);
    for (auto& t : pool) t.join();
}
}  // namespace

bool HdTextures::crop(const Job& job, int w, int h, int min_w, int min_h, int& cw, int& ch) {
    // the raster's extent inside the 2^TW x 2^TH region the replacement covers
    const std::uint32_t tw = 1u << job.tw, th = 1u << job.th;
    cw = std::clamp(int((std::uint64_t(w) * std::uint32_t(job.width) + tw / 2) / tw), 1, w);
    ch = std::clamp(int((std::uint64_t(h) * std::uint32_t(job.height) + th / 2) / th), 1, h);
    return cw >= min_w && ch >= min_h;   // never lower resolution than the decode
}

std::size_t HdTextures::mips_rebuilt() { return g_mips_rebuilt.load(std::memory_order_relaxed); }
std::size_t HdTextures::bc7_mips_unused() { return g_bc7_mips_unused.load(std::memory_order_relaxed); }

std::shared_ptr<const std::vector<Image>> HdTextures::bc_chain(const Source& src, const Job& job, int threads, const std::string* path) {
    const DdsInfo& in = src.info;
    const int fmt = in.fmt;
    int cw, ch;
    if (fmt < kBC1 || fmt > kBC3 || !crop(job, in.w, in.h, 1, 1, cw, ch) || cw % 4 || ch % 4) return nullptr;
    const std::uint8_t* base = src.data.data() + in.at;
    const int sbw = (in.w + 3) / 4, bw = cw / 4, bh = ch / 4;
    // BC1 with transparent texels (3-colour blocks using index 3) and TCC 1: BC3 (the mips' partial alpha needs it)
    bool to_bc3 = false;
    if (fmt == kBC1 && job.tcc)
        for (int by = 0; by < bh && !to_bc3; ++by)
            for (int bx = 0; bx < bw && !to_bc3; ++bx) {
                const std::uint8_t* b = base + (std::size_t(by) * sbw + bx) * 8;
                if ((b[0] | b[1] << 8) > (b[2] | b[3] << 8)) continue;
                for (int i = 0; i < 16; ++i) to_bc3 |= ((b[4 + i / 4] >> (2 * (i & 3))) & 3) == 3;
            }
    const int ofmt = to_bc3 ? kBC3 : fmt;
    auto chain = std::make_shared<std::vector<Image>>();
    // the stored levels while the crop is the whole file (a crop's mips are built from its level 0)
    const int stored = cw == in.w && ch == in.h ? in.levels : 1;
    std::size_t off = in.at;
    for (int l = 0; l < stored; ++l) {
        const int lw = l ? std::max(in.w >> l, 1) : cw, lh = l ? std::max(in.h >> l, 1) : ch;
        Image lv;
        lv.w = lw;
        lv.h = lh;
        lv.bc = ofmt;
        auto b = std::make_shared<std::vector<std::uint8_t>>();
        const int lbw = l ? (lw + 3) / 4 : bw, lbh = l ? (lh + 3) / 4 : bh;
        convert_level(fmt, src.data.data() + off, l ? lbw : sbw, lbw, lbh, job.tcc, to_bc3, *b);
        lv.blocks = std::move(b);
        chain->push_back(std::move(lv));
        off += bc_level_bytes(fmt, l ? lw : in.w, l ? lh : in.h);
    }
    // hdmips: stored mips that are not the level above made smaller (black or empty levels in a pack's DDS: walls that
    // turn black at the distance that mip is drawn, moving with the camera) are dropped from the first such level on;
    // the rest is built from the last good level below, as for a file without stored mips
    if (chain->size() > 1) {
        const std::size_t good = bc_consistent_levels(*chain, job.tcc);
        if (good < chain->size()) {
            const std::size_t n = g_mips_rebuilt.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= 20)
                std::fprintf(stderr, "hd pack: %s: stored mip level %zu of %zu (%dx%d) is not the level above made smaller (black or "
                             "empty mips?): levels %zu and below built from level %zu instead%s\n", path ? path->c_str() : "a DDS file",
                             good, chain->size(), (*chain)[good].w, (*chain)[good].h, good, good - 1, n == 20 ? " (no more of these lines)" : "");
            chain->resize(good);
        }
    }
    const Image& last = chain->back();
    Image dec;
    dec.w = last.w;
    dec.h = last.h;
    dec.rgba.resize(std::size_t(dec.w) * dec.h * 4);
    bc_decode(ofmt, last.blocks->data(), dec.w, dec.h, dec.rgba.data());
    finish_chain(*chain, std::move(dec), ofmt, threads, nullptr);
    return chain;
}

std::shared_ptr<const std::vector<Image>> HdTextures::bc_rule_chain(const std::vector<Image>& plain, const Image& level0, int threads) {
    if (plain.empty() || plain[0].w != level0.w || plain[0].h != level0.h || level0.rgba.size() < std::size_t(level0.w) * level0.h * 4)
        return nullptr;
    auto chain = std::make_shared<std::vector<Image>>();
    Image cur = level0;   // the rule's RGBA8 at this level (the mips: halved like the GPU's blit)
    for (std::size_t lv = 0; lv < plain.size(); ++lv) {
        const Image& p = plain[lv];
        if (lv) {
            Image next;
            mip_down(cur, next);
            cur = std::move(next);
        }
        if (!p.blocks || p.w != cur.w || p.h != cur.h) return nullptr;
        const int w = cur.w, h = cur.h, bw = (w + 3) / 4, bh = (h + 3) / 4, fmt = p.bc;
        const std::size_t psz = bc_block_bytes(fmt);
        auto b = std::make_shared<std::vector<std::uint8_t>>(std::size_t(bw) * bh * 16);
        auto rows = [&](int r0, int r1) {
            std::uint8_t px[16][4], a[16];
            for (int by = r0; by < r1; ++by)
                for (int bx = 0; bx < bw; ++bx) {
                    const std::uint8_t* sb = p.blocks->data() + (std::size_t(by) * bw + bx) * psz;
                    std::uint8_t* o = b->data() + (std::size_t(by) * bw + bx) * 16;
                    for (int i = 0; i < 16; ++i) {
                        const int x = std::min(bx * 4 + (i & 3), w - 1), y = std::min(by * 4 + (i >> 2), h - 1);
                        std::memcpy(px[i], &cur.rgba[(std::size_t(y) * w + std::size_t(x)) * 4], 4);
                        a[i] = px[i][3];
                    }
                    bc_encode_alpha(a, o);
                    if (fmt != kBC1) { std::memcpy(o + 8, sb + 8, 8); continue; }   // the colour half as built
                    // BC1: a 4-colour block (or one colour) reads the same as BC3's colour half; a 3-colour one is fitted again
                    const bool four = (sb[0] | sb[1] << 8) > (sb[2] | sb[3] << 8);
                    bool one = (sb[0] | sb[1] << 8) == (sb[2] | sb[3] << 8);
                    for (int i = 0; i < 16 && one; ++i) one = ((sb[4 + i / 4] >> (2 * (i & 3))) & 3) < 2;
                    if (four || one) std::memcpy(o + 8, sb, 8);
                    else bc_encode_color(px, o + 8, false);
                }
        };
        const int n = std::clamp(threads, 1, std::max(1, bh / 8));
        std::vector<std::thread> pool;
        for (int k = 1; k < n; ++k) pool.emplace_back(rows, bh * k / n, bh * (k + 1) / n);
        rows(0, bh / n);
        for (auto& t : pool) t.join();
        Image l;
        l.w = w;
        l.h = h;
        l.bc = kBC3;
        l.blocks = std::move(b);
        chain->push_back(std::move(l));
    }
    return chain;
}

std::shared_ptr<const HdTextures::Source> HdTextures::source(const std::string& path) {
    auto it = cache_.find(path);
    if (it == cache_.end()) {
        const auto t1 = std::chrono::steady_clock::now();
        std::shared_ptr<const Source> s = read_source(path, !g_compressed);
        if (s) ++files_loaded;
        load_ms += ms_since(t1);
        it = cache_.emplace(path, std::move(s)).first;
    }
    return it->second;
}

void set_hd_textures(HdTextures* t) { g_hd = t; }
HdTextures* hd_textures() { return t_no_hook ? nullptr : g_hd; }
bool hd_switchable() { return g_hd && g_hd->originals_from < g_hd->pack.dirs.size(); }

std::vector<std::string> hd_folders(const std::string& dir, bool off, const std::vector<std::string>& before, const std::string& xbox) {
    std::string d = dir;
    if (d.empty())
        if (const char* e = std::getenv("B3_HD_PACK")) d = e;
    std::vector<std::string> folders = before;   // patches3: the mods' packs first, the player's last
    if (!off && !d.empty()) folders.push_back(d);
    if (!off && !xbox.empty() && xbox != d) folders.push_back(xbox);   // xboximport: below the player's own pack
    return folders;
}

namespace {
std::string g_xbox_folder;   // xboximport (set_hd_xbox_folder)
}
void set_hd_xbox_folder(const std::string& dir) { g_xbox_folder = dir; }
const std::string& hd_xbox_folder() { return g_xbox_folder; }

HdTextures* hd_enable(const std::string& dir, bool off, const std::vector<std::string>& before, const std::string& xbox) {
    static HdTextures instance;
    set_hd_textures(nullptr);
    const std::vector<std::string> folders = hd_folders(dir, off, before, xbox);
    if (folders.empty()) return nullptr;
    std::string names;
    for (const std::string& f : folders) names += (names.empty() ? "" : " + ") + f;
    const auto t0 = std::chrono::steady_clock::now();
    std::string why;
    if (!instance.open(folders, &why) || instance.pack.files.empty()) {
        std::fprintf(stderr, "hd pack %s: %s\n", names.c_str(), why.empty() ? "no replacement files" : why.c_str());
        return nullptr;
    }
    if (!why.empty()) std::fprintf(stderr, "hd pack: %s\n", why.c_str());
    std::fprintf(stderr, "hd pack %s: %zu replacements indexed (%zu DDS, %zu PNG; %zu other files) in %.0f ms\n", names.c_str(),
                 instance.pack.files.size(), instance.pack.dds_files, instance.pack.png_files, instance.pack.skipped, ms_since(t0));
    // hdkey: the player's pack (after the mods'; a folder that is not there is left out of dirs) and the Xbox import
    // below it keep the originals for the HD TEXTURES key: from the first folder that is not a mod's
    for (std::size_t i = 0; i < instance.pack.dirs.size(); ++i)
        if (std::find(before.begin(), before.end(), instance.pack.dirs[i]) == before.end()) { instance.originals_from = i; break; }
    set_hd_textures(&instance);
    return &instance;
}

void hd_prefetch(const std::vector<std::string>& files) {
    if (g_hd) g_hd->prefetch(files);
}
void hd_trim() {
    if (g_hd) g_hd->trim();
}

HdDecodedStats hd_decoded_stats() {
    DecodedLru& l = decoded_lru();
    const std::lock_guard<std::mutex> lk(l.m);
    return {l.total, l.budget(), l.evicted};
}

void set_hd_cache_dir(const std::string& dir, std::uint64_t cap_mb) {
    DiskCache& d = disk();
    const char* off = std::getenv("B3_HD_CACHE");
    const char* mb = std::getenv("B3_HD_CACHE_MB");
    const long long env_mb = mb ? std::atoll(mb) : 0;
    const std::lock_guard<std::mutex> lk(d.m);
    d.dir = (off && off[0] == '0' && !off[1]) || dir.empty() ? std::filesystem::path() : disk_fs(dir);
    d.cap = std::uint64_t(env_mb > 0 ? std::uint64_t(env_mb) : cap_mb ? cap_mb : hdcache::kDefaultCapMB) << 20;
    d.scanned = false;   // (the worker measures the folder first)
    if (!d.dir.empty() && d.started) d.cv.notify_one();
}
std::string hd_cache_dir() {
    DiskCache& d = disk();
    const std::lock_guard<std::mutex> lk(d.m);
#ifdef _WIN32
    return d.dir.empty() ? std::string() : b3io::u8string(d.dir);
#else
    return d.dir.string();
#endif
}
bool hd_cache_flush(int max_ms) {
    DiskCache& d = disk();
    std::unique_lock<std::mutex> lk(d.m);
    return d.idle.wait_for(lk, std::chrono::milliseconds(max_ms), [&] { return d.q.empty() && !d.busy; });
}
HdCacheStats hd_cache_stats() {
    DiskCache& d = disk();
    HdCacheStats s;
    s.hits = d.hits; s.misses = d.misses; s.written = d.written; s.written_bytes = d.written_bytes; s.corrupt = d.corrupt;
    s.evicted = d.evicted; s.failed = d.failed; s.read_bytes = d.read_bytes; s.read_ms = double(d.read_ns.load()) / 1e6;
    const std::lock_guard<std::mutex> lk(d.m);
    s.total = d.total;
    return s;
}
void hd_prefetch_data(const std::vector<std::pair<std::string, Bytes>>& files, const std::function<bool()>& go, double budget_ms,
                      bool* stopped) {
    if (g_hd) g_hd->prefetch_data(files, 8, go, budget_ms, stopped);
}

void hd_report() {
    if (!g_hd) return;
    if (!hd_cache_dir().empty()) {   // hdcache
        const bool all = hd_cache_flush();
        const HdCacheStats c = hd_cache_stats();
        std::fprintf(stderr, "hd pack: disk cache %llu read (%.1f MB, %.0f ms), %llu converted, %llu written (%.1f MB)%s, %llu damaged, %llu evicted, %llu not written; the folder %.1f MB\n",
                     (unsigned long long)c.hits, double(c.read_bytes) / 1048576.0, c.read_ms, (unsigned long long)c.misses, (unsigned long long)c.written,
                     double(c.written_bytes) / 1048576.0, all ? "" : " (some still queued)", (unsigned long long)c.corrupt, (unsigned long long)c.evicted,
                     (unsigned long long)c.failed, double(c.total) / 1048576.0);
    }
    std::fprintf(stderr, "hd pack: %zu rasters looked up, %zu replaced (%zu files decoded, %zu failed, %zu with alpha held to the original's coverage / maximum%s, %zu originals kept); keys %.0f ms, DDS %.0f ms\n",
                 g_hd->lookups, g_hd->hits, g_hd->files_loaded, g_hd->failures, g_hd->masked, g_hd->raw_alpha ? ", B3_HD_RAW_ALPHA: off" : "",
                 g_hd->originals, g_hd->key_ms, g_hd->load_ms);
    if (g_encoded_files)   // texpack: HD TEXTURE MEMORY
        std::fprintf(stderr, "hd pack: %zu PNG / BC7 files encoded to BC1 / BC3 at load (%.1f MB)\n", g_encoded_files.load(),
                     double(g_encoded_bytes.load()) / 1048576.0);
    if (g_compressed)   // hdgpu
        std::fprintf(stderr, "hd pack: %zu kept compressed (BC, %.1f MB with their mips; %zu re-encoded for the alpha rule), %zu as RGBA8; hook %.0f ms (BC chains built there %.0f ms)\n",
                     g_hd->compressed, double(g_hd->bc_bytes) / 1048576.0, g_hd->encoded, g_hd->hits - g_hd->compressed, g_hd->apply_ms, g_hd->bc_ms);
    if (const std::size_t n = HdTextures::bc7_mips_unused())   // texpack: their own line (they are not bad, only not read)
        std::fprintf(stderr, "hd pack: %zu BC7 files' stored mip levels not used: their levels built from level 0\n", n);
    if (const std::size_t n = HdTextures::mips_rebuilt())   // hdmips
        std::fprintf(stderr, "hd pack: %zu files' stored mip levels did not match their level 0 (black or empty mips): those levels built from the file's picture\n", n);
}

bool HdTextures::open(const std::string& folder, std::string* why) { return open(std::vector<std::string>{folder}, why); }

bool HdTextures::keeps_original(const std::string& path) const {
    if (originals_from >= pack.dirs.size()) return false;
    // the folder the file was found in: the longest of the folders its path starts with (a scan names its files below it)
    std::size_t best = SIZE_MAX, best_len = 0;
    for (std::size_t i = 0; i < pack.dirs.size(); ++i) {
        const std::string& d = pack.dirs[i];
        if (d.empty() || path.size() <= d.size() || path.compare(0, d.size(), d) != 0) continue;
        const char next = path[d.size()], last = d.back();
        if (last != '/' && last != '\\' && next != '/' && next != '\\') continue;
        if (d.size() >= best_len) { best = i; best_len = d.size(); }
    }
    return best != SIZE_MAX && best >= originals_from;
}

bool HdTextures::open(const std::vector<std::string>& folders, std::string* why) {
    const std::lock_guard<Lock> lk(mu_);
    cache_.clear();
    raw_alpha = std::getenv("B3_HD_RAW_ALPHA") != nullptr;
    prepared_.clear();
    lookups = hits = failures = files_loaded = masked = originals = 0;
    compressed = encoded = bc_bytes = bc_built = 0;
    originals_from = SIZE_MAX;
    load_ms = key_ms = bc_ms = apply_ms = 0;
    return pack.open(folders, why);
}

bool HdTextures::apply(const GsMemory& gs, DecodedRaster& r, std::uint32_t cbp_add) {
    const std::lock_guard<Lock> lk(mu_);
    const auto t_apply = std::chrono::steady_clock::now();
    const bool hit = apply_(gs, r, cbp_add);
    apply_ms += ms_since(t_apply);
    return hit;
}

bool HdTextures::apply_(const GsMemory& gs, DecodedRaster& r, std::uint32_t cbp_add) {
    if (r.levels.empty() || pack.files.empty()) return false;
    ++lookups;
    const auto t0 = std::chrono::steady_clock::now();
    const std::string* path = hd_lookup(pack, gs, r.hdr, cbp_add);
    key_ms += ms_since(t0);
    if (!path) return false;
    std::uint64_t dkey = 0, dcheck = 0;   // hdcache: the converted result from the disk, the file not read at all
    const bool disk_on = disk_key(*path, r, t_compress, dkey, dcheck);
    if (disk_on && disk_hit(*path, dkey, dcheck, r)) return true;
    const std::shared_ptr<const Source> file = source(*path);
    if (!file) { ++failures; return false; }
    const std::size_t masked_before = masked;
    const bool hit = replace_(*path, file, r, t_compress, masked);
    if (hit && disk_on) disk_store(dkey, dcheck, r, masked != masked_before);
    return hit;
}

// hdcache: bump when what replace_ / bc_chain / bc_rule_chain / the BC encoder (bc.h) produce changes, so entries made by
// the older code are not used (they age out of the cache)
constexpr int kConvVersion = 1;

bool HdTextures::disk_key(const std::string& path, const DecodedRaster& r, bool compress, std::uint64_t& key, std::uint64_t& check) {
    {
        const std::lock_guard<std::mutex> lk(disk().m);
        if (disk().dir.empty()) return false;
    }
    auto it = ident_.find(path);
    if (it == ident_.end()) {   // the file's identity once a run: a changed pack file gets new keys
        namespace fs = std::filesystem;
        std::error_code e1, e2;
        const fs::path p = disk_fs(path);
        const std::uintmax_t n = fs::file_size(p, e1);
        const auto t = fs::last_write_time(p, e2);
        std::string id;
        if (!e1 && !e2) {   // (Fable) + the first and last 4 KB's hash: the same size and time is not always the same file
            id = std::to_string(n) + "|" + std::to_string(static_cast<long long>(t.time_since_epoch().count()));
            std::uint8_t buf[8192];
            std::size_t got = 0;
            if (std::FILE* fp = b3io::fopen(p, "rb")) {
                got = std::fread(buf, 1, 4096, fp);
                if (n > 4096 && std::fseek(fp, n > 8192 ? long(n - 4096) : long(got), SEEK_SET) == 0) got += std::fread(buf + got, 1, 4096, fp);
                std::fclose(fp);
            }
            if (got == std::min<std::uintmax_t>(n, 8192)) {
                char hb[24];
                std::snprintf(hb, sizeof hb, "|%016llx", static_cast<unsigned long long>(xxh3_64(buf, got)));
                id += hb;
            } else {
                id.clear();   // (unreadable now: no cache for it)
            }
        }
        it = ident_.emplace(path, std::move(id)).first;
    }
    if (it->second.empty()) return false;
    const Job j = job_of(r.hdr);
    const Image& o = r.levels[0];
    char b[200];
    std::snprintf(b, sizeof b, "|%dx%d/%dx%d/%d|o %dx%d|m %d%d%d%d|v %u.%d", j.width, j.height, j.tw, j.th, j.tcc ? 1 : 0, o.w, o.h,
                  g_compressed ? 1 : 0, compress ? 1 : 0, g_encode.load() ? 1 : 0, raw_alpha ? 1 : 0, unsigned(hdcache::kFormat), kConvVersion);
    std::string k = path + "|" + it->second + b;
    if (j.tcc && !raw_alpha) {   // the alpha rule's input: the original's alpha and maximum (only they reach the result)
        std::vector<std::uint8_t> a(o.rgba.size() / 4);
        for (std::size_t i = 0; i < a.size(); ++i) a[i] = o.rgba[i * 4 + 3];
        char c[64];
        std::snprintf(c, sizeof c, "|a %d %016llx", int(r.max_alpha), static_cast<unsigned long long>(xxh3_64(a.data(), a.size())));
        k += c;
    }
    key = xxh3_64(k.data(), k.size());
    const std::string k2 = "check|" + k;
    check = xxh3_64(k2.data(), k2.size());
    return true;
}

bool HdTextures::disk_hit(const std::string& path, std::uint64_t key, std::uint64_t check, DecodedRaster& r) {
    DiskCache& d = disk();
    std::shared_ptr<const std::vector<Image>> chain;
    bool m = false;
    std::vector<Image> levels;
    if (auto p = disk_mem_.find(key); p != disk_mem_.end()) {
        chain = p->second.chain;
        m = p->second.masked;
    } else {
        std::filesystem::path f;
        {
            const std::lock_guard<std::mutex> lk(d.m);
            f = entry_path(d.dir, key);
        }
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::uint8_t> bytes;
        if (std::FILE* fp = b3io::fopen(f, "rb")) {
            std::fseek(fp, 0, SEEK_END);
            const long n = std::ftell(fp);
            std::fseek(fp, 0, SEEK_SET);
            if (n > 0) {
                bytes.resize(std::size_t(n));
                if (std::fread(bytes.data(), 1, bytes.size(), fp) != bytes.size()) bytes.clear();
            }
            std::fclose(fp);
        } else {
            ++d.misses;
            return false;
        }
        DiskEntry e;
        if (!read_entry(bytes, key, check, e)) {   // damaged (or another key's): converted again and written anew
            ++d.corrupt;
            ++d.misses;
            std::error_code ec;
            std::filesystem::remove(f, ec);
            return false;
        }
        d.read_ns += std::uint64_t(ms_since(t0) * 1e6);
        d.read_bytes += bytes.size();
        m = e.masked;
        if (e.levels.front().bc) {
            chain = std::make_shared<const std::vector<Image>>(std::move(e.levels));
            disk_mem_[key] = Prepared{chain, m};
        } else {
            levels = std::move(e.levels);
        }
        if (!touched_[key]) {   // the LRU's age: read now
            touched_[key] = true;
            disk_queue(DiskCache::Job{f, key, check, nullptr});
        }
    }
    ++d.hits;
    // as replace_'s end
    if (keeps_original(path)) {   // hdkey
        r.original = std::move(r.levels);
        ++originals;
    }
    r.levels.clear();
    masked += m;
    if (chain) {
        r.levels = *chain;   // the blocks are shared
        ++compressed;
        encoded += chain->front().bc == kBC3 && m;
        bc_bytes += chain_bytes(*chain);
    } else {
        r.levels = std::move(levels);
    }
    r.hd = true;
    ++hits;
    return true;
}

void HdTextures::disk_store(std::uint64_t key, std::uint64_t check, const DecodedRaster& r, bool m) {
    auto e = std::make_shared<DiskEntry>();
    e->levels = r.levels;   // (BC blocks shared; an RGBA8 level copied: the write is on the worker)
    e->masked = m;
    if (!e->levels.empty() && e->levels.front().bc) disk_mem_[key] = Prepared{std::make_shared<const std::vector<Image>>(e->levels), m};
    std::filesystem::path f;
    {
        const std::lock_guard<std::mutex> lk(disk().m);
        f = entry_path(disk().dir, key);
    }
    disk_queue(DiskCache::Job{std::move(f), key, check, std::move(e)});
}

// hdgpu: the replacement of `r` by `file` (found at `path_`): apply's own (`masked` = this->masked: counted), or prefetch's
// threads building an alpha rule's BC chain ahead (`compress`, `masked` a scratch: nothing counted; prepared_ gets it)
bool HdTextures::replace_(const std::string& path_, const std::shared_ptr<const Source>& file, DecodedRaster& r, bool compress,
                          std::size_t& masked) {
    const std::string* const path = &path_;
    const bool count = &masked == &this->masked;
    const RasterHeader& h = r.hdr;
    const Job job = job_of(h);
    int cw, ch;
    if (!crop(job, file->info.w, file->info.h, r.levels[0].w, r.levels[0].h, cw, ch)) { failures += count; return false; }
    // hdgpu: the BC chain when the device takes it and this decode's consumer only uploads it (HdCompressScope): the
    // file's blocks when the alpha rule below cannot change anything, else the rule's result (cached for the LOD model)
    bool bc = g_compressed && compress && file->info.fmt >= kBC1 && file->info.fmt <= kBC3 && cw % 4 == 0 && ch % 4 == 0;
    const bool rule = bc && h.tcc() && !raw_alpha && (rule_may_change(r) || file->opaque_file);   // (texpack: the game's alpha)
    std::shared_ptr<const std::vector<Image>> chain;
    bool chain_masked = false;
    const std::string key = bc ? (rule ? rule_key(*path, job, r) : chain_key(*path, job)) : std::string();
    if (bc) {
        std::unique_lock<Lock> lk(mu_);
        auto p = prepared_.find(key);
        if (p != prepared_.end()) {
            chain = p->second.chain;
            chain_masked = p->second.masked;
        } else if (!rule) {
            lk.unlock();
            const auto t1 = std::chrono::steady_clock::now();
            chain = bc_chain(*file, job, t_rows, path);
            if (count) bc_ms += ms_since(t1);
            lk.lock();
            prepared_[key] = Prepared{chain, false};
        }
        if (!chain && !rule) bc = false;
    }
    bool rule_changed = false;
    Image out;
    if (!chain) {
        const std::shared_ptr<const Image> dec = file->decoded();
        if (!dec) { failures += count; return false; }
        const Image& src = *dec;
        out.w = cw;
        out.h = ch;
        out.rgba.resize(std::size_t(cw) * ch * 4);
        for (int y = 0; y < ch; ++y)
            std::memcpy(&out.rgba[std::size_t(y) * cw * 4], &src.rgba[std::size_t(y) * src.w * 4], std::size_t(cw) * 4);
    }
    if (chain) {
        masked += chain_masked;
    } else if (!h.tcc()) {
        for (std::size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 0xFF;
    } else if (!raw_alpha) {
        // The replacement keeps to the original's alpha: transparent over the original's transparent texels (alpha 0)
        // where the original, bilinear-filtered, is below a quarter (the cut follows the filter's smooth contour around
        // them, so the replacement's softer outlines stay), and never above the original's maximum (PS2 units; some
        // rasters top out at 0x7F, packs store opaque as 0x80..0x8D). Low but nonzero alpha is never cut: blended overlays
        // (the sun haze over US/C2_V2's buildings, the light pattern in EU/P1_V2's tunnel, road and sign masks) keep the
        // pack's alpha [C: an identity pack (each texture's own decode at 1x) frames byte-identical to B3_HD_RAW_ALPHA=1 on
        // US/C2_V2 and EU/P1_V2 polls 10400 / 11100; the earlier cut of every texel below a quarter changed 9-61 % of
        // their pixels]. Why the cut: the game hides geometry on transparent texels (alpha-tested, AREF 0) and upscaled
        // outlines drift onto them; and the frame alpha it writes feeds later passes [C: race2's flag frames
        // Chgo_Flag1..10 (US/C3_V1, max 0x7F): with the pack's alpha a solid wedge appears under the gateway sign; the
        // coverage cut alone or the clamp alone leaves it, both together match. A cut only where the bilinear alpha is 0
        // also leaves it (the hidden triangle samples a transparent texel beside opaque ones: wedge region 20.1 levels
        // mean difference to no pack, as raw alpha; this rule 1.9, as the old quarter cut). A nearest-texel cut also fixes
        // it but turns foliage outlines into the original's texel squares; on textures with only transparent and opaque
        // texels this rule equals the old quarter cut].
        const Image& o = r.levels[0];
        const std::uint8_t limit = r.max_alpha >= 0x80 ? 0xFF : std::uint8_t(r.max_alpha * 2);
        auto oa = [&](int x, int y) {
            return float(o.rgba[(std::size_t((y % o.h + o.h) % o.h) * o.w + std::size_t((x % o.w + o.w) % o.w)) * 4 + 3]);
        };
        // hdgpu: the rows in strips on threads (hd_rows; the body below is kept as it was, not indented again)
        std::atomic<bool> any_rows{false};
        hd_rows(ch, [&](int y_begin, int y_end) {
        bool any = false;
        for (int y = y_begin; y < y_end; ++y) {
            const float fy = (float(y) + 0.5f) * float(o.h) / float(ch) - 0.5f;
            const int y0 = int(std::floor(fy));
            const float wy = fy - float(y0);
            for (int x = 0; x < cw; ++x) {
                std::uint8_t& a = out.rgba[(std::size_t(y) * cw + x) * 4 + 3];
                if (!a) continue;
                const float fx = (float(x) + 0.5f) * float(o.w) / float(cw) - 0.5f;
                const int x0 = int(std::floor(fx));
                const float wx = fx - float(x0);
                const float b = (oa(x0, y0) * (1 - wx) + oa(x0 + 1, y0) * wx) * (1 - wy) + (oa(x0, y0 + 1) * (1 - wx) + oa(x0 + 1, y0 + 1) * wx) * wy;
                const bool cut = b < 64.0f && oa(x0 + (wx >= 0.5f), y0 + (wy >= 0.5f)) == 0.0f;   // the nearest texel transparent
                const std::uint8_t lim = cut ? 0 : limit;
                if (file->opaque_file) {   // texpack: no alpha in the file: the original's, bilinear (cut and clamp as below)
                    const std::uint8_t ob = cut ? 0 : std::uint8_t(std::min<float>(float(lim), std::floor(b + 0.5f)));
                    if (a != ob) { a = ob; any = true; }
                    continue;
                }
                if (a > lim) { a = lim; any = true; }
            }
        }
        if (any) any_rows = true;
        });
        const bool any = any_rows;
        masked += any;
        rule_changed = any;
    }
    if (bc && !chain) {   // hdgpu: the rule ran on RGBA8: its result as BC3 when it changed texels, else the file's blocks
        const auto t1 = std::chrono::steady_clock::now();
        const std::string plain_key = chain_key(*path, job);   // the file's chain (prefetch builds it on its threads)
        std::shared_ptr<const std::vector<Image>> plain;
        {
            const std::lock_guard<Lock> lk(mu_);
            auto p = prepared_.find(plain_key);
            if (p != prepared_.end()) plain = p->second.chain;
        }
        if (!plain) {
            plain = bc_chain(*file, job, t_rows, path);
            const std::lock_guard<Lock> lk(mu_);
            prepared_[plain_key] = Prepared{plain, false};
        }
        chain = rule_changed && plain ? bc_rule_chain(*plain, out, t_rows) : plain;
        if (count) bc_ms += ms_since(t1);
        const std::lock_guard<Lock> lk(mu_);
        if (chain) prepared_[key] = Prepared{chain, rule_changed};
        chain_masked = rule_changed;
    }
    if (!count) return chain != nullptr;   // prefetch: built ahead, nothing replaced
    if (keeps_original(*path)) {   // hdkey
        r.original = std::move(r.levels);
        ++originals;
    }
    r.levels.clear();
    if (chain) {
        r.levels = *chain;   // the blocks are shared
        ++compressed;
        encoded += chain->front().bc == kBC3 && chain_masked;
        bc_bytes += chain_bytes(*chain);
    } else {
        r.levels.push_back(std::move(out));
    }
    r.hd = true;
    ++hits;
    return true;
}

HdNoHookScope::HdNoHookScope() : saved(t_no_hook) { t_no_hook = true; }
HdNoHookScope::~HdNoHookScope() { t_no_hook = saved; }

std::size_t hd_each_raster(Bytes f, GsMemory& gs, const std::function<bool(const HdRasterVariant&)>& fn, bool all_keys) {
    const HdNoHookScope no_hook;
    std::size_t n = 0;
    for (std::size_t o : find_rasters(f)) {
        RasterHeader h;
        if (!read_raster_header(f, o, h)) continue;
        ++n;
        const int variants = std::max<int>(1, h.palettes);
        for (int v = 0; v < variants; ++v) {
            HdRasterVariant rv;
            rv.offset = o;
            rv.variant = v;
            rv.variants = variants;
            rv.cbp_add = palette_cbp_add(f, o, v);
            rv.decoded = decode_raster_clut(f, o, gs, rv.raster, rv.cbp_add, &rv.why);
            if (rv.decoded) {
                if (all_keys) {
                    rv.keys = hd_candidate_keys(gs, rv.raster.hdr, rv.cbp_add);
                } else {
                    HdCandidate c;
                    if (hd_key(gs, rv.raster.hdr, rv.cbp_add, 1, c.key)) rv.keys.push_back(c);
                }
            } else {
                rv.raster = DecodedRaster{};
                rv.raster.hdr = h;
            }
            if (!fn(rv)) return n;
        }
    }
    return n;
}

std::vector<std::size_t> find_rasters(Bytes f) {
    std::vector<std::size_t> out;
    std::vector<TxdEntry> es;
    if (read_txd(f, es)) {
        for (const auto& e : es) out.push_back(e.offset);
        return out;
    }
    for (std::size_t o = 0; o + 0xC8 <= f.n; o += 16) {
        if (f.p[o + 0x20] != 4) continue;
        RasterHeader h;
        if (read_raster_header(f, o, h)) out.push_back(o);
    }
    return out;
}

std::size_t HdTextures::prefetch(const std::vector<std::string>& files, int threads) {
    if (pack.files.empty()) return 0;
    std::vector<std::vector<std::uint8_t>> held;   // (hdcache: the bytes form below does the work)
    std::vector<std::pair<std::string, Bytes>> data;
    held.reserve(files.size());
    for (const auto& path : files) {
        std::vector<std::uint8_t> d;
        if (!read_file(path, d)) continue;
        held.push_back(std::move(d));
        data.emplace_back(path, Bytes{held.back().data(), held.back().size()});
    }
    return prefetch_data(data, threads);
}

std::size_t HdTextures::prefetch_data(const std::vector<std::pair<std::string, Bytes>>& files, int threads,
                                      const std::function<bool()>& go, double budget_ms, bool* stopped) {
    if (stopped) *stopped = false;
    if (pack.files.empty()) return 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::atomic<bool> halt{false};   // hdcache (Fable): no new raster past the caller's wait or budget
    const auto stop = [&]() {
        if (halt.load(std::memory_order_relaxed)) return true;
        if ((go && !go()) || (budget_ms > 0 && ms_since(t0) > budget_ms)) halt = true;
        return halt.load(std::memory_order_relaxed);
    };
    struct NoHook {   // plain decodes here (on this thread only)
        bool saved = t_no_hook;
        NoHook() { t_no_hook = true; }
        ~NoHook() { t_no_hook = saved; }
    };
    std::optional<NoHook> no_hook(std::in_place);
    // hdgpu: per file, the BC chains its rasters will ask for (built on the threads as well) and whether the RGBA8
    // decode is needed (no BC device, or a raster the alpha rule may change)
    struct Todo { std::string path; std::vector<Job> jobs; std::vector<std::string> keys; bool decode = false; std::vector<DecodedRaster> rules; };
    std::vector<Todo> todo;
    std::unordered_map<std::string, std::size_t> at;
    GsMemory gs;
    std::filesystem::path disk_dir;   // hdcache: a raster whose converted result is on the disk needs nothing here
    {
        const std::lock_guard<std::mutex> lk(disk().m);
        disk_dir = disk().dir;
    }
    for (const auto& [name, f] : files) {
        if (stop()) break;
        for (std::size_t o : find_rasters(f)) {
            if (stop()) break;
            RasterHeader h;
            if (!read_raster_header(f, o, h)) continue;
            for (int v = 0; v < std::max<int>(1, h.palettes); ++v) {
                DecodedRaster r;
                const std::uint32_t add = palette_cbp_add(f, o, v);
                if (!decode_raster_clut(f, o, gs, r, add) || r.levels.empty()) continue;
                const std::string* p = hd_lookup(pack, gs, r.hdr, add);
                if (!p) continue;
                std::unique_lock<Lock> lk(mu_);
                if (cache_.count(*p)) continue;
                if (!disk_dir.empty()) {   // (the car and track rasters are converted under HdCompressScope: compress)
                    std::uint64_t key = 0, check = 0;
                    std::error_code ec;
                    if (disk_key(*p, r, g_compressed, key, check) && (disk_mem_.count(key) || std::filesystem::exists(entry_path(disk_dir, key), ec))) continue;
                }
                auto [it, fresh] = at.emplace(*p, todo.size());
                if (fresh) todo.push_back(Todo{*p, {}, {}, false});
                Todo& t = todo[it->second];
                const Job job = job_of(r.hdr);
                if (!g_compressed || (job.tcc && !raw_alpha && rule_may_change(r))) {
                    t.decode = true;   // the alpha rule runs on it (with BC: here, on the threads, onto this chain's colour)
                    if (!g_compressed) continue;
                    if (!prepared_.count(rule_key(*p, job, r))) {
                        DecodedRaster rc;
                        rc.hdr = r.hdr;
                        rc.levels.push_back(r.levels[0]);
                        rc.max_alpha = r.max_alpha;
                        t.rules.push_back(std::move(rc));
                    }
                }
                const std::string key = chain_key(*p, job);
                if (!prepared_.count(key) && std::find(t.keys.begin(), t.keys.end(), key) == t.keys.end()) {
                    t.jobs.push_back(job);
                    t.keys.push_back(key);
                }
            }
        }
    }
    no_hook.reset();
    struct Got { std::shared_ptr<const Source> src; };
    std::vector<Got> got(todo.size());
    std::size_t next = 0;
    std::mutex m;
    // (Fable) the workers: mu_ only briefly (prepared_'s stores, replace_'s own lookups), never ident_ / disk_mem_ /
    // touched_, disk_key or apply; prefetch_data holds no mu_ while they run (its scan locks per raster, the merge after
    // the join), so it must not be called with mu_ held (apply never calls it): the workers would wait on it forever
    auto work = [&]() {
        for (;;) {
            std::size_t i;
            {
                std::lock_guard<std::mutex> lk(m);
                if (next >= todo.size() || stop()) return;
                i = next++;
            }
            Todo& t = todo[i];
            got[i].src = read_source(t.path, t.decode);
            if (!got[i].src) continue;
            std::vector<std::shared_ptr<const std::vector<Image>>> chains;
            for (const Job& j : t.jobs) chains.push_back(bc_chain(*got[i].src, j, 1, &t.path));
            {
                const std::lock_guard<Lock> lk(mu_);
                for (std::size_t k = 0; k < chains.size(); ++k)
                    if (chains[k]) prepared_[t.keys[k]] = Prepared{chains[k], false};
            }
            const int rows = t_rows;
            t_rows = 1;   // (this pool is the parallelism)
            bool all = true;
            for (DecodedRaster& rc : t.rules) {
                std::size_t scratch = 0;
                all = replace_(t.path, got[i].src, rc, true, scratch) && all;
            }
            t_rows = rows;
            if (g_compressed && all && got[i].src->info.fmt <= kBC3) got[i].src->drop();   // built: the decode goes (decoded() redoes it)
        }
    };
    std::vector<std::thread> pool;
    const int n = std::clamp(threads, 1, 16);
    for (int k = 1; k < n; ++k) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
    const std::lock_guard<Lock> lk(mu_);
    std::size_t loaded = 0;
    for (std::size_t i = 0; i < todo.size(); ++i) {
        if (got[i].src) ++loaded;
        cache_.emplace(todo[i].path, std::move(got[i].src));
    }
    files_loaded += loaded;
    load_ms += ms_since(t0);
    if (stopped) *stopped = halt.load();
    return loaded;
}

// ---- pack index ----

bool HdPack::open(const std::string& folder, std::string* why) { return open(std::vector<std::string>{folder}, why); }

bool HdPack::open(const std::vector<std::string>& folders, std::string* why) {
    dir = folders.empty() ? std::string() : folders.front();
    dirs.clear();
    files.clear();
    skipped = dds_files = png_files = repeats_ = 0;
    bool any = false;
    std::string notes;
    for (const std::string& f : folders) {
        std::string w;
        if (add(f, &w)) any = true;
        if (!w.empty()) notes += (notes.empty() ? "" : "; ") + w;
    }
    if (why) *why = notes;
    return any;
}

namespace {
// texpack: the pack's folder and files as UTF-8 strings (the settings', the mods'); on Windows a narrow fs::path is the
// ANSI code page, so a folder with other letters (C:\Users\Jürgen\...) was not found: through b3io there
std::filesystem::path hd_path(const std::string& s) {
#ifdef _WIN32
    return b3io::u8path(s);
#else
    return std::filesystem::path(s);
#endif
}
std::string hd_string(const std::filesystem::path& p) {
#ifdef _WIN32
    return b3io::u8string(p);
#else
    return p.string();
#endif
}
}  // namespace

bool HdPack::add(const std::string& folder, std::string* why) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(hd_path(folder), ec)) { if (why) *why = "not a directory: " + folder; return false; }
    // texpack: .dds and .png (PCSX2 loads both; community packs ship PNG), in path order: a pack that holds one key in
    // several sub-folders (colour variants, an extracted archive beside its own copy) picks the same file on every
    // system (was: the directory's own order, which differs between file systems), and the repeats are counted
    std::vector<fs::path> found;
    for (fs::recursive_directory_iterator it(hd_path(folder), fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec)) found.push_back(it->path());
    }
    // texpack review: by the paths' UTF-8 bytes, the same order on every system (fs::path compares native strings)
    std::vector<std::pair<std::string, fs::path>> by_name;
    by_name.reserve(found.size());
    for (fs::path& p : found) by_name.emplace_back(hd_string(p), std::move(p));
    std::sort(by_name.begin(), by_name.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    found.clear();
    for (auto& e : by_name) found.push_back(std::move(e.second));
    std::size_t repeats = 0;
    std::string example;
    for (const fs::path& p : found) {
        HdKey k;
        const std::string name = hd_string(p.filename());
        std::string ext = hd_string(p.extension());
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if ((ext != ".dds" && ext != ".png") || !parse_hd_name(name, k)) { ++skipped; continue; }
        const auto r = files.emplace(k, hd_string(p));   // first one wins, as PCSX2's map insert (and an earlier folder's)
        if (r.second) {
            ext == ".png" ? ++png_files : ++dds_files;
        } else if (r.first->second.compare(0, folder.size(), folder) == 0) {   // twice in this folder (not a mod's over it)
            if (!repeats++) example = name + ": " + r.first->second + " used, " + hd_string(p) + " not";
        }
    }
    repeats_ += repeats;
    if (repeats) {
        const std::string note = std::to_string(repeats) + " replacement name" + (repeats == 1 ? "" : "s") + " found more than once in " +
                                 folder + " (the first in path order is used; e.g. " + example + ")";
        if (why) *why = note;
    }
    if (ec && why) *why = (why->empty() ? "" : *why + "; ") + "directory scan stopped: " + ec.message();
    dirs.push_back(folder);
    return true;
}

const std::string* HdPack::find(const HdKey& k) const {
    const auto it = files.find(k);
    return it == files.end() ? nullptr : &it->second;
}

// ---- DDS ----

namespace {
bool fail(std::string* why, const char* what) { if (why) *why = what; return false; }
bool fail(std::string* why, const std::string& what) { if (why) *why = what; return false; }

inline std::uint8_t ps2_alpha(std::uint32_t a) { return a >= 0x80 ? 0xFF : std::uint8_t(a * 2); }
}  // namespace

bool parse_dds(const std::uint8_t* d, std::size_t n, DdsInfo& out, std::string* why) {
    out = DdsInfo{};
    if (n < 128 || std::memcmp(d, "DDS ", 4) != 0 || rd32(d + 4) != 124) return fail(why, "not a DDS file");
    const std::uint32_t flags = rd32(d + 8), h = rd32(d + 12), w = rd32(d + 16), mips = rd32(d + 28), pf_flags = rd32(d + 80),
                        fourcc = rd32(d + 84);
    if (!w || !h || w > 16384 || h > 16384) return fail(why, "bad DDS size");
    std::size_t at = 128;
    int fmt = 0;
    auto cc = [](const char* s) { return std::uint32_t(s[0]) | std::uint32_t(s[1]) << 8 | std::uint32_t(s[2]) << 16 | std::uint32_t(s[3]) << 24; };
    if (pf_flags & 4) {   // DDPF_FOURCC
        if (fourcc == cc("DXT1")) fmt = kBC1;
        else if (fourcc == cc("DXT2") || fourcc == cc("DXT3")) fmt = kBC2;
        else if (fourcc == cc("DXT4") || fourcc == cc("DXT5")) fmt = kBC3;
        else if (fourcc == cc("DX10")) {
            if (n < 148) return fail(why, "truncated DX10 header");
            const std::uint32_t dxgi = rd32(d + 128);
            at = 148;
            if (dxgi == 71 || dxgi == 72) fmt = kBC1;
            else if (dxgi == 74 || dxgi == 75) fmt = kBC2;
            else if (dxgi == 77 || dxgi == 78) fmt = kBC3;
            else if (dxgi == 28 || dxgi == 29) fmt = kDdsRGBA;
            else if (dxgi == 87 || dxgi == 91) fmt = kDdsBGRA;
            else if (dxgi == 98 || dxgi == 99) fmt = kBC7;   // texpack: BC7 (decoded on the CPU)
            else if (dxgi >= 79 && dxgi <= 84) return fail(why, "BC4 / BC5 DDS (one- / two-channel: not read; PCSX2 does not either)");
            else if (dxgi >= 94 && dxgi <= 96) return fail(why, "BC6H DDS (HDR: not read)");
            else return fail(why, "unsupported DX10 format " + std::to_string(dxgi));
        } else if (fourcc == cc("ATI1") || fourcc == cc("ATI2") || fourcc == cc("BC4U") || fourcc == cc("BC5U") ||
                   fourcc == cc("BC4S") || fourcc == cc("BC5S")) {
            return fail(why, "BC4 / BC5 DDS (one- / two-channel: not read; PCSX2 does not either)");
        } else return fail(why, "unsupported DDS FourCC");
    } else if ((pf_flags & 0x40) && rd32(d + 88) == 32) {   // DDPF_RGB, 32 bits
        const std::uint32_t rm = rd32(d + 92), am = rd32(d + 104);
        const bool has_a = (pf_flags & 1) && am == 0xFF000000u;
        if (rm == 0x000000FFu) fmt = kDdsRGBA;
        else if (rm == 0x00FF0000u) fmt = kDdsBGRA;
        else return fail(why, "unsupported DDS RGB mask");
        if (!has_a) return fail(why, "DDS RGB without alpha not handled");
    } else {
        return fail(why, "unsupported DDS pixel format");
    }
    auto bytes = [&](std::uint32_t lw, std::uint32_t lh) {
        return fmt == kDdsRGBA || fmt == kDdsBGRA ? std::size_t(lw) * lh * 4 : bc_level_bytes(fmt, int(lw), int(lh));
    };
    if (at > n || n - at < bytes(w, h)) return fail(why, "DDS data truncated");
    out.fmt = fmt;
    out.w = int(w);
    out.h = int(h);
    out.at = at;
    if (flags & 0x20000) {   // DDSD_MIPMAPCOUNT: the levels the data holds whole (a short file keeps the ones before)
        std::size_t off = at + bytes(w, h);
        std::uint32_t lw = w, lh = h;
        for (std::uint32_t l = 1; l < std::min<std::uint32_t>(mips, 15) && (lw > 1 || lh > 1); ++l) {
            lw = std::max(lw / 2, 1u);
            lh = std::max(lh / 2, 1u);
            if (n - off < bytes(lw, lh)) break;
            off += bytes(lw, lh);
            ++out.levels;
        }
    }
    return true;
}

bool decode_dds(const std::uint8_t* d, std::size_t n, Image& out, std::string* why) {
    out = Image{};
    DdsInfo info;
    if (!parse_dds(d, n, info, why)) return false;
    const int fmt = info.fmt;
    const std::size_t w = std::size_t(info.w), h = std::size_t(info.h);
    out.w = info.w;
    out.h = info.h;
    out.rgba.resize(w * h * 4);
    const std::uint8_t* p = d + info.at;
    if (fmt == kDdsRGBA || fmt == kDdsBGRA) {
        for (std::size_t i = 0; i < w * h; ++i) {
            std::uint8_t* o = &out.rgba[i * 4];
            const std::uint8_t* s = p + i * 4;
            o[0] = fmt == kDdsRGBA ? s[0] : s[2];
            o[1] = s[1];
            o[2] = fmt == kDdsRGBA ? s[2] : s[0];
            o[3] = ps2_alpha(s[3]);
        }
        return true;
    }
    bc_decode(fmt, p, info.w, info.h, out.rgba.data());
    for (std::size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = ps2_alpha(out.rgba[i]);
    return true;
}

bool decode_replacement(const std::uint8_t* d, std::size_t n, Image& out, std::string* why, bool* opaque_file) {
    if (opaque_file) *opaque_file = false;
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') {   // texpack: PNG, its alpha read as a DDS's
        out = Image{};
        bool has_alpha = true;
        if (!decode_png(d, n, out.w, out.h, out.rgba, why, &has_alpha)) return false;
        if (opaque_file) *opaque_file = !has_alpha;
        for (std::size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = ps2_alpha(out.rgba[i]);
        return true;
    }
    return decode_dds(d, n, out, why);
}

bool load_replacement(const std::string& path, Image& out, std::string* why) {
    std::vector<std::uint8_t> d;
    if (!read_file(path, d)) return fail(why, "cannot read the replacement file");
    return decode_replacement(d.data(), d.size(), out, why);
}

bool load_dds(const std::string& path, Image& out, std::string* why) {
    std::vector<std::uint8_t> d;
    if (!read_file(path, d)) return fail(why, "cannot read DDS file");
    return decode_dds(d.data(), d.size(), out, why);
}

}  // namespace b3asset
