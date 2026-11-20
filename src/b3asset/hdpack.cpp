#include "b3asset/hdpack.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

#include "b3asset/bc.h"
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
bool g_compressed = false;           // hdgpu: set_hd_compressed
thread_local bool t_compress = false;   // hdgpu: HdCompressScope
thread_local bool t_no_hook = false;    // hdgpu: prefetch's plain decodes (this thread only: the hook stays for the others)
thread_local int t_rows = 8;            // hdgpu: threads hd_rows and the chain builders use (1 on prefetch's own threads)
}  // namespace

// hdgpu: a replacement file as read: its bytes (the BC blocks are used from here) and, when the RGBA8 path or the alpha
// rule needs it, the top level decoded (decode_dds)
struct HdTextures::Source {
    std::vector<std::uint8_t> data;
    DdsInfo info;
    mutable std::shared_ptr<const Image> rgba;
    const Image* decoded() const {
        if (!rgba) {
            auto im = std::make_shared<Image>();
            if (!decode_dds(data.data(), data.size(), *im)) return nullptr;
            rgba = std::move(im);
        }
        return rgba.get();
    }
};

void set_hd_compressed(bool on) { g_compressed = on; }
bool hd_compressed() { return g_compressed; }
HdCompressScope::HdCompressScope() : saved(t_compress) { t_compress = true; }
HdCompressScope::~HdCompressScope() { t_compress = saved; }

namespace {
std::shared_ptr<const HdTextures::Source> read_source(const std::string& path, bool decode) {
    auto s = std::make_shared<HdTextures::Source>();
    if (!read_file(path, s->data) || !parse_dds(s->data.data(), s->data.size(), s->info)) return nullptr;
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

std::shared_ptr<const std::vector<Image>> HdTextures::bc_chain(const Source& src, const Job& job, int threads) {
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
    std::fprintf(stderr, "hd pack %s: %zu replacements indexed (%zu other files) in %.0f ms\n", names.c_str(), instance.pack.files.size(),
                 instance.pack.skipped, ms_since(t0));
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

void hd_report() {
    if (!g_hd) return;
    std::fprintf(stderr, "hd pack: %zu rasters looked up, %zu replaced (%zu files decoded, %zu failed, %zu with alpha held to the original's coverage / maximum%s, %zu originals kept); keys %.0f ms, DDS %.0f ms\n",
                 g_hd->lookups, g_hd->hits, g_hd->files_loaded, g_hd->failures, g_hd->masked, g_hd->raw_alpha ? ", B3_HD_RAW_ALPHA: off" : "",
                 g_hd->originals, g_hd->key_ms, g_hd->load_ms);
    if (g_compressed)   // hdgpu
        std::fprintf(stderr, "hd pack: %zu kept compressed (BC, %.1f MB with their mips; %zu re-encoded for the alpha rule), %zu as RGBA8; hook %.0f ms (BC chains built there %.0f ms)\n",
                     g_hd->compressed, double(g_hd->bc_bytes) / 1048576.0, g_hd->encoded, g_hd->hits - g_hd->compressed, g_hd->apply_ms, g_hd->bc_ms);
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
    const std::shared_ptr<const Source> file = source(*path);
    if (!file) { ++failures; return false; }
    return replace_(*path, file, r, t_compress, masked);
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
    const bool rule = bc && h.tcc() && !raw_alpha && rule_may_change(r);
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
            chain = bc_chain(*file, job, t_rows);
            if (count) bc_ms += ms_since(t1);
            lk.lock();
            prepared_[key] = Prepared{chain, false};
        }
        if (!chain && !rule) bc = false;
    }
    bool rule_changed = false;
    Image out;
    if (!chain) {
        const Image* dec = file->decoded();
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
            plain = bc_chain(*file, job, t_rows);
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
    const auto t0 = std::chrono::steady_clock::now();
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
                if (!decode_raster_clut(f, o, gs, r, add) || r.levels.empty()) continue;
                const std::string* p = hd_lookup(pack, gs, r.hdr, add);
                if (!p) continue;
                std::unique_lock<Lock> lk(mu_);
                if (cache_.count(*p)) continue;
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
    auto work = [&]() {
        for (;;) {
            std::size_t i;
            {
                std::lock_guard<std::mutex> lk(m);
                if (next >= todo.size()) return;
                i = next++;
            }
            Todo& t = todo[i];
            got[i].src = read_source(t.path, t.decode);
            if (!got[i].src) continue;
            std::vector<std::shared_ptr<const std::vector<Image>>> chains;
            for (const Job& j : t.jobs) chains.push_back(bc_chain(*got[i].src, j));
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
            if (g_compressed && all && got[i].src->info.fmt <= kBC3) got[i].src->rgba.reset();   // built: the decode goes (decoded() redoes it)
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
            else return fail(why, "unsupported DX10 format (BC7 and others not decoded)");
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
        return fmt >= kDdsRGBA ? std::size_t(lw) * lh * 4 : bc_level_bytes(fmt, int(lw), int(lh));
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

bool load_dds(const std::string& path, Image& out, std::string* why) {
    std::vector<std::uint8_t> d;
    if (!read_file(path, d)) return fail(why, "cannot read DDS file");
    return decode_dds(d.data(), d.size(), out, why);
}

}  // namespace b3asset
