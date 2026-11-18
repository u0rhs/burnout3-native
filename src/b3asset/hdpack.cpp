#include "b3asset/hdpack.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

#include "b3asset/export.h"

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
}  // namespace

void set_hd_textures(HdTextures* t) { g_hd = t; }
HdTextures* hd_textures() { return g_hd; }
bool hd_switchable() { return g_hd && g_hd->originals_from < g_hd->pack.dirs.size(); }

std::vector<std::string> hd_folders(const std::string& dir, bool off, const std::vector<std::string>& before) {
    std::string d = dir;
    if (d.empty())
        if (const char* e = std::getenv("B3_HD_PACK")) d = e;
    std::vector<std::string> folders = before;   // patches3: the mods' packs first, the player's last
    if (!off && !d.empty()) folders.push_back(d);
    return folders;
}

HdTextures* hd_enable(const std::string& dir, bool off, const std::vector<std::string>& before) {
    static HdTextures instance;
    set_hd_textures(nullptr);
    const std::vector<std::string> folders = hd_folders(dir, off, before);
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
    std::fprintf(stderr, "hd pack %s: %zu replacements indexed (%zu other files) in %.0f ms\n", names.c_str(), instance.pack.files.size(),
                 instance.pack.skipped, ms_since(t0));
    // hdkey: the player's pack (the last folder, after the mods'; a folder that is not there is left out of dirs) keeps
    // the originals for the HD TEXTURES key
    if (folders.size() > before.size() && instance.pack.dirs.back() == folders.back()) instance.originals_from = instance.pack.dirs.size() - 1;
    set_hd_textures(&instance);
    return &instance;
}

void hd_prefetch(const std::vector<std::string>& files) {
    if (g_hd) g_hd->prefetch(files);
}
void hd_trim() {
    if (g_hd) g_hd->trim();
}

void hd_report() {
    if (!g_hd) return;
    std::fprintf(stderr, "hd pack: %zu rasters looked up, %zu replaced (%zu files decoded, %zu failed, %zu with alpha held to the original's coverage / maximum%s, %zu originals kept); keys %.0f ms, DDS %.0f ms\n",
                 g_hd->lookups, g_hd->hits, g_hd->files_loaded, g_hd->failures, g_hd->masked, g_hd->raw_alpha ? ", B3_HD_RAW_ALPHA: off" : "",
                 g_hd->originals, g_hd->key_ms, g_hd->load_ms);
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
    cache_.clear();
    raw_alpha = std::getenv("B3_HD_RAW_ALPHA") != nullptr;
    lookups = hits = failures = files_loaded = masked = originals = 0;
    originals_from = SIZE_MAX;
    load_ms = key_ms = 0;
    return pack.open(folders, why);
}

bool HdTextures::apply(const GsMemory& gs, DecodedRaster& r, std::uint32_t cbp_add) {
    if (r.levels.empty() || pack.files.empty()) return false;
    ++lookups;
    const auto t0 = std::chrono::steady_clock::now();
    const std::string* path = hd_lookup(pack, gs, r.hdr, cbp_add);
    key_ms += ms_since(t0);
    if (!path) return false;
    auto it = cache_.find(*path);
    if (it == cache_.end()) {
        const auto t1 = std::chrono::steady_clock::now();
        auto im = std::make_shared<Image>();
        std::string why;
        std::shared_ptr<const Image> v;
        if (load_dds(*path, *im, &why)) { v = std::move(im); ++files_loaded; }
        load_ms += ms_since(t1);
        it = cache_.emplace(*path, std::move(v)).first;
    }
    if (!it->second) { ++failures; return false; }
    const Image& src = *it->second;
    const RasterHeader& h = r.hdr;
    const std::uint32_t tw = 1u << (h.tex0 >> 26 & 0xF), th = 1u << (h.tex0 >> 30 & 0xF);
    // the raster's extent inside the 2^TW x 2^TH region the replacement covers
    const int cw = std::clamp(int((std::uint64_t(src.w) * h.width + tw / 2) / tw), 1, src.w);
    const int ch = std::clamp(int((std::uint64_t(src.h) * h.height + th / 2) / th), 1, src.h);
    if (cw < r.levels[0].w || ch < r.levels[0].h) { ++failures; return false; }   // never lower resolution than the decode
    Image out;
    out.w = cw;
    out.h = ch;
    out.rgba.resize(std::size_t(cw) * ch * 4);
    for (int y = 0; y < ch; ++y)
        std::memcpy(&out.rgba[std::size_t(y) * cw * 4], &src.rgba[std::size_t(y) * src.w * 4], std::size_t(cw) * 4);
    if (!h.tcc()) {
        for (std::size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 0xFF;
    } else if (!raw_alpha) {
        // The replacement keeps to the original's alpha: transparent where the original, bilinear-filtered, is below a
        // quarter (around the centre of each transparent texel; the cut follows the filter's smooth contours, so the
        // replacement's softer outlines stay), and never above the original's maximum (PS2 units; some rasters top out at
        // 0x7F, packs store opaque as 0x80..0x8D). The game hides geometry on transparent texels (alpha-tested, AREF 0)
        // and upscaled outlines drift onto them; and the frame alpha it writes feeds later passes [C: race2's flag frames
        // Chgo_Flag1..10 (US/C3_V1, max 0x7F): with the pack's alpha a solid wedge appears under the gateway sign; the
        // coverage cut alone or the clamp alone leaves it (wedge region 28.4 / 29.0 levels mean difference to the
        // original), both together match (2.2: the HD detail). A nearest-texel cut also fixes it but turns foliage
        // outlines into the original's texel squares].
        const Image& o = r.levels[0];
        const std::uint8_t limit = r.max_alpha >= 0x80 ? 0xFF : std::uint8_t(r.max_alpha * 2);
        auto oa = [&](int x, int y) {
            return float(o.rgba[(std::size_t((y % o.h + o.h) % o.h) * o.w + std::size_t((x % o.w + o.w) % o.w)) * 4 + 3]);
        };
        bool any = false;
        for (int y = 0; y < ch; ++y) {
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
                const std::uint8_t lim = b < 64.0f ? 0 : limit;
                if (a > lim) { a = lim; any = true; }
            }
        }
        masked += any;
    }
    if (keeps_original(*path)) {   // hdkey
        r.original = std::move(r.levels);
        ++originals;
    }
    r.levels.clear();
    r.levels.push_back(std::move(out));
    r.hd = true;
    ++hits;
    return true;
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
    const auto t0 = std::chrono::steady_clock::now();
    HdTextures* saved = hd_textures();
    set_hd_textures(nullptr);   // plain decodes here
    std::vector<std::string> todo;
    GsMemory gs;
    for (const auto& path : files) {
        std::vector<std::uint8_t> d;
        if (!read_file(path, d)) continue;
        const Bytes f{d.data(), d.size()};
        for (std::size_t o : find_rasters(f)) {
            RasterHeader h;
            if (!read_raster_header(f, o, h)) continue;
            for (int v = 0; v < std::max<int>(1, h.palettes); ++v) {
                DecodedRaster r;
                const std::uint32_t add = palette_cbp_add(f, o, v);
                if (!decode_raster_clut(f, o, gs, r, add)) continue;
                if (const std::string* p = hd_lookup(pack, gs, r.hdr, add))
                    if (!cache_.count(*p) && std::find(todo.begin(), todo.end(), *p) == todo.end()) todo.push_back(*p);
            }
        }
    }
    set_hd_textures(saved);
    std::vector<std::shared_ptr<const Image>> got(todo.size());
    std::size_t next = 0;
    std::mutex m;
    auto work = [&]() {
        for (;;) {
            std::size_t i;
            {
                std::lock_guard<std::mutex> lk(m);
                if (next >= todo.size()) return;
                i = next++;
            }
            auto im = std::make_shared<Image>();
            if (load_dds(todo[i], *im)) got[i] = std::move(im);
        }
    };
    std::vector<std::thread> pool;
    const int n = std::clamp(threads, 1, 16);
    for (int k = 1; k < n; ++k) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
    std::size_t loaded = 0;
    for (std::size_t i = 0; i < todo.size(); ++i) {
        if (got[i]) ++loaded;
        cache_.emplace(todo[i], std::move(got[i]));
    }
    files_loaded += loaded;
    load_ms += ms_since(t0);
    return loaded;
}

// ---- pack index ----

bool HdPack::open(const std::string& folder, std::string* why) { return open(std::vector<std::string>{folder}, why); }

bool HdPack::open(const std::vector<std::string>& folders, std::string* why) {
    dir = folders.empty() ? std::string() : folders.front();
    dirs.clear();
    files.clear();
    skipped = 0;
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

bool HdPack::add(const std::string& folder, std::string* why) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) { if (why) *why = "not a directory: " + folder; return false; }
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        HdKey k;
        const std::string name = it->path().filename().string();
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".dds" || !parse_hd_name(name, k)) { ++skipped; continue; }
        files.emplace(k, it->path().string());   // first one wins, as PCSX2's map insert (and an earlier folder's)
    }
    if (ec && why) *why = "directory scan stopped: " + ec.message();
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

inline std::uint8_t ps2_alpha(std::uint32_t a) { return a >= 0x80 ? 0xFF : std::uint8_t(a * 2); }

void rgb565(std::uint16_t c, std::uint8_t o[3]) {
    const std::uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    o[0] = std::uint8_t((r << 3) | (r >> 2));
    o[1] = std::uint8_t((g << 2) | (g >> 4));
    o[2] = std::uint8_t((b << 3) | (b >> 2));
}

// One BC1 colour block into 4x4 RGBA (alpha 255, or 0 for BC1's 3-colour transparent index when `bc1_alpha`).
void color_block(const std::uint8_t* b, std::uint8_t px[16][4], bool bc1_alpha) {
    const std::uint16_t c0 = std::uint16_t(b[0] | b[1] << 8), c1 = std::uint16_t(b[2] | b[3] << 8);
    std::uint8_t pal[4][4];
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
    const bool four = c0 > c1 || !bc1_alpha;
    for (int k = 0; k < 3; ++k) {
        if (four) {
            pal[2][k] = std::uint8_t((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = std::uint8_t((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        } else {
            pal[2][k] = std::uint8_t((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = 0;
        }
    }
    if (!four) pal[3][3] = 0;
    const std::uint32_t idx = std::uint32_t(b[4]) | std::uint32_t(b[5]) << 8 | std::uint32_t(b[6]) << 16 | std::uint32_t(b[7]) << 24;
    for (int i = 0; i < 16; ++i) std::memcpy(px[i], pal[(idx >> (2 * i)) & 3], 4);
}

void bc3_alpha(const std::uint8_t* b, std::uint8_t px[16][4]) {
    std::uint32_t a[8];
    a[0] = b[0];
    a[1] = b[1];
    if (a[0] > a[1]) {
        for (int i = 1; i < 7; ++i) a[1 + i] = ((7 - i) * a[0] + i * a[1] + 3) / 7;
    } else {
        for (int i = 1; i < 5; ++i) a[1 + i] = ((5 - i) * a[0] + i * a[1] + 2) / 5;
        a[6] = 0;
        a[7] = 255;
    }
    u64 bits = 0;
    for (int i = 0; i < 6; ++i) bits |= u64(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) px[i][3] = std::uint8_t(a[(bits >> (3 * i)) & 7]);
}
}  // namespace

bool decode_dds(const std::uint8_t* d, std::size_t n, Image& out, std::string* why) {
    out = Image{};
    if (n < 128 || std::memcmp(d, "DDS ", 4) != 0 || rd32(d + 4) != 124) return fail(why, "not a DDS file");
    const std::uint32_t h = rd32(d + 12), w = rd32(d + 16), pf_flags = rd32(d + 80), fourcc = rd32(d + 84);
    if (!w || !h || w > 16384 || h > 16384) return fail(why, "bad DDS size");
    std::size_t at = 128;
    enum { kNone, kBC1, kBC2, kBC3, kRGBA, kBGRA } fmt = kNone;
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
            else if (dxgi == 28 || dxgi == 29) fmt = kRGBA;
            else if (dxgi == 87 || dxgi == 91) fmt = kBGRA;
            else return fail(why, "unsupported DX10 format (BC7 and others not decoded)");
        } else return fail(why, "unsupported DDS FourCC");
    } else if ((pf_flags & 0x40) && rd32(d + 88) == 32) {   // DDPF_RGB, 32 bits
        const std::uint32_t rm = rd32(d + 92), am = rd32(d + 104);
        const bool has_a = (pf_flags & 1) && am == 0xFF000000u;
        if (rm == 0x000000FFu) fmt = kRGBA;
        else if (rm == 0x00FF0000u) fmt = kBGRA;
        else return fail(why, "unsupported DDS RGB mask");
        if (!has_a) return fail(why, "DDS RGB without alpha not handled");
    } else {
        return fail(why, "unsupported DDS pixel format");
    }
    const std::size_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    const std::size_t need = (fmt == kRGBA || fmt == kBGRA) ? std::size_t(w) * h * 4 : bw * bh * (fmt == kBC1 ? 8 : 16);
    if (at > n || n - at < need) return fail(why, "DDS data truncated");
    out.w = int(w);
    out.h = int(h);
    out.rgba.resize(std::size_t(w) * h * 4);
    const std::uint8_t* p = d + at;
    if (fmt == kRGBA || fmt == kBGRA) {
        for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
            std::uint8_t* o = &out.rgba[i * 4];
            const std::uint8_t* s = p + i * 4;
            o[0] = fmt == kRGBA ? s[0] : s[2];
            o[1] = s[1];
            o[2] = fmt == kRGBA ? s[2] : s[0];
            o[3] = ps2_alpha(s[3]);
        }
        return true;
    }
    const std::size_t bsz = fmt == kBC1 ? 8 : 16;
    std::uint8_t px[16][4];
    for (std::size_t by = 0; by < bh; ++by)
        for (std::size_t bx = 0; bx < bw; ++bx) {
            const std::uint8_t* b = p + (by * bw + bx) * bsz;
            if (fmt == kBC1) {
                color_block(b, px, true);
            } else {
                color_block(b + 8, px, false);
                if (fmt == kBC3) bc3_alpha(b, px);
                else
                    for (int i = 0; i < 16; ++i) {
                        const std::uint32_t a4 = (b[i / 2] >> (4 * (i & 1))) & 15;
                        px[i][3] = std::uint8_t(a4 * 17);
                    }
            }
            for (int i = 0; i < 16; ++i) {
                const std::size_t x = bx * 4 + std::size_t(i & 3), y = by * 4 + std::size_t(i >> 2);
                if (x >= w || y >= h) continue;
                std::uint8_t* o = &out.rgba[(y * w + x) * 4];
                o[0] = px[i][0];
                o[1] = px[i][1];
                o[2] = px[i][2];
                o[3] = ps2_alpha(px[i][3]);
            }
        }
    return true;
}

bool load_dds(const std::string& path, Image& out, std::string* why) {
    std::vector<std::uint8_t> d;
    if (!read_file(path, d)) return fail(why, "cannot read DDS file");
    return decode_dds(d.data(), d.size(), out, why);
}

}  // namespace b3asset
