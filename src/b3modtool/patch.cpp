// b3patch: a binary delta of one file against the user's own copy (docs/modding/b3patch.md).
#include "b3modtool/modtool.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "b3mod/mods.h"

namespace b3modtool {

namespace {

constexpr char kMagic[8] = {'B', '3', 'P', 'A', 'T', 'C', 'H', 0};
constexpr std::uint8_t kEnd = 0, kCopy = 1, kInsert = 2;
constexpr std::size_t kBlock = 16;     // source blocks indexed for the match search
constexpr std::size_t kDiagMin = 12;   // a run on the last copy's diagonal worth a copy op

void put64(Buf& o, std::uint64_t v) { for (int i = 0; i < 8; ++i) o.push_back(std::uint8_t(v >> (8 * i))); }
void put32(Buf& o, std::uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(std::uint8_t(v >> (8 * i))); }
void put_varint(Buf& o, std::uint64_t v) {
    while (v >= 0x80) { o.push_back(std::uint8_t(v | 0x80)); v >>= 7; }
    o.push_back(std::uint8_t(v));
}

constexpr std::uint64_t kP = 0x100000001B3ull;
std::uint64_t block_hash(const std::uint8_t* p) {
    std::uint64_t h = 0;
    for (std::size_t i = 0; i < kBlock; ++i) h = h * kP + p[i];
    return h;
}

struct Writer {
    Buf& out;
    PatchInfo& info;
    void insert(const std::uint8_t* p, std::size_t n) {
        if (!n) return;
        out.push_back(kInsert);
        put_varint(out, n);
        out.insert(out.end(), p, p + n);
        info.inserted += n;
        ++info.ops;
    }
    void copy(std::uint64_t off, std::uint64_t n) {
        out.push_back(kCopy);
        put_varint(out, off);
        put_varint(out, n);
        info.copied += n;
        ++info.ops;
    }
};

}  // namespace

Buf make_patch(const Buf& src, const Buf& dst, PatchInfo* info_out) {
    PatchInfo info;
    info.src_size = src.size();
    info.src_fnv = b3mod::fnv1a64(src.data(), src.size());
    info.dst_size = dst.size();
    info.dst_fnv = b3mod::fnv1a64(dst.data(), dst.size());
    Buf out(kMagic, kMagic + 8);
    put32(out, kPatchVersion);
    put32(out, 0);
    put64(out, info.src_size);
    put64(out, info.src_fnv);
    put64(out, info.dst_size);
    put64(out, info.dst_fnv);
    Writer w{out, info};

    // the source's aligned blocks by hash (the first of equal blocks)
    std::unordered_map<std::uint64_t, std::uint32_t> index;
    if (src.size() >= kBlock) {
        index.reserve(src.size() / kBlock + 1);
        for (std::size_t s = 0; s + kBlock <= src.size(); s += kBlock) index.emplace(block_hash(&src[s]), std::uint32_t(s));
    }
    std::uint64_t pow = 1;   // kP^(kBlock-1)
    for (std::size_t i = 1; i < kBlock; ++i) pow *= kP;

    const std::size_t n = dst.size();
    std::size_t t = 0, pending = 0;   // dst[pending, t): bytes not covered yet
    std::int64_t diag = 0;            // source offset - target offset of the last copy
    std::uint64_t h = 0;
    bool h_valid = false;
    auto extend = [&](std::size_t s, std::size_t tt) {
        std::size_t len = 0;
        while (s + len < src.size() && tt + len < n && src[s + len] == dst[tt + len]) ++len;
        return len;
    };
    while (t < n) {
        std::size_t best_s = 0, best_len = 0;
        // 1. the last copy's diagonal (an edit in place leaves the rest where it was)
        const std::int64_t ds = std::int64_t(t) + diag;
        if (ds >= 0 && std::size_t(ds) < src.size() && src[std::size_t(ds)] == dst[t]) {
            const std::size_t len = extend(std::size_t(ds), t);
            if (len >= kDiagMin) { best_s = std::size_t(ds); best_len = len; }
        }
        // 2. a source block with this target block's hash
        if (!best_len && t + kBlock <= n && !index.empty()) {
            if (!h_valid) { h = block_hash(&dst[t]); h_valid = true; }
            auto it = index.find(h);
            if (it != index.end() && std::memcmp(&src[it->second], &dst[t], kBlock) == 0) {
                best_s = it->second;
                best_len = extend(best_s, t);
            }
        }
        if (best_len) {
            // grow the match backwards over bytes still pending
            while (t > pending && best_s > 0 && src[best_s - 1] == dst[t - 1]) { --t; --best_s; ++best_len; }
            w.insert(dst.data() + pending, t - pending);
            w.copy(best_s, best_len);
            diag = std::int64_t(best_s) - std::int64_t(t);
            t += best_len;
            pending = t;
            h_valid = false;
            continue;
        }
        // advance one byte (the rolling hash follows)
        if (h_valid && t + kBlock < n) h = (h - dst[t] * pow) * kP + dst[t + kBlock];
        else h_valid = false;
        ++t;
    }
    w.insert(dst.data() + pending, n - pending);
    out.push_back(kEnd);
    if (info_out) *info_out = info;
    return out;
}

}  // namespace b3modtool
