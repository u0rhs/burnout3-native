// chd_writer.h -- a minimal CHD v5 writer for tests (chd1): libchdr only reads, so the tests make their own images
// (no disc data). The layout is MAME's (chd.cpp / chdman), as libchdr reads it (src/native/third_party/libchdr):
//   header (124 bytes, big-endian): "MComprHD", length 124, version 5, four compressor tags, logical bytes, map offset,
//     metadata offset, hunk bytes, unit bytes, raw / data / parent SHA-1 (zero: none checked; a parent SHA-1 that is
//     not zero makes it a CHD that needs its parent);
//   map, uncompressed (no compressor): one 32-bit entry per hunk, its file offset / hunk bytes (0: a hunk of zeros);
//   map, compressed: 16 bytes (compressed size, first hunk's offset (48-bit), CRC-16 of the decoded 12-byte entries,
//     length / self / parent bit widths), then a bit stream (MSB first): the Huffman tree of the 16 entry types (here
//     every code 4 bits, so a type's code is the type itself), each hunk's type, then per hunk its compressed length
//     and CRC-16 (type 0 = compressor 0), its CRC-16 (4: stored as is), or the hunk it repeats (5: self);
//   metadata: entries of tag, flags << 24 | length, next entry's offset (0: the last), data;
//   hunks: "zlib" = raw deflate of the hunk; "cdzl" (CD frames of 2352 + 96 bytes): a bit per frame (sync / ECC to
//     regenerate: none here), the sector part's compressed size (2 bytes below 64 KB hunks), deflate of the frames'
//     2352-byte sectors, deflate of their 96-byte subcode.
// CRC-16 is CCITT (polynomial 0x1021, from 0xFFFF).
#ifndef B3_TESTS_CHD_WRITER_H
#define B3_TESTS_CHD_WRITER_H

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace chdtest {

enum class Codec { kNone, kZlib, kCdZlib };

struct Options {
    Codec codec = Codec::kNone;
    std::uint32_t hunkbytes = 4096;
    std::uint32_t unitbytes = 2048;
    std::vector<std::pair<std::string, std::string>> metadata;   // (four-character tag, text; written with its NUL)
    bool parent = false;                                          // a parent SHA-1: a CHD that needs its parent
};

struct Image {
    std::vector<std::uint8_t> bytes;        // the .chd file
    std::vector<std::uint64_t> hunk_at;     // each hunk's file offset (0: none stored: zeros or a repeat)
    std::uint64_t first_hunk = 0;           // where the hunks' data starts
};

inline std::uint16_t crc16(const std::uint8_t* p, std::size_t n) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t k = 0; k < n; ++k) {
        crc = std::uint16_t(crc ^ (p[k] << 8));
        for (int b = 0; b < 8; ++b) crc = std::uint16_t(crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
inline std::uint32_t tag(const char* t) { return std::uint32_t(std::uint8_t(t[0])) << 24 | std::uint32_t(std::uint8_t(t[1])) << 16 | std::uint32_t(std::uint8_t(t[2])) << 8 | std::uint8_t(t[3]); }
inline void put_be(std::vector<std::uint8_t>& v, std::size_t at, std::uint64_t x, int bytes) {
    for (int k = 0; k < bytes; ++k) v[at + std::size_t(k)] = std::uint8_t(x >> (8 * (bytes - 1 - k)));
}
inline void add_be(std::vector<std::uint8_t>& v, std::uint64_t x, int bytes) {
    v.resize(v.size() + std::size_t(bytes));
    put_be(v, v.size() - std::size_t(bytes), x, bytes);
}

// raw deflate (no zlib header), as chdman's zlib codec
inline std::vector<std::uint8_t> deflate_raw(const std::uint8_t* p, std::size_t n) {
    z_stream z{};
    deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    std::vector<std::uint8_t> out(deflateBound(&z, uLong(n)) + 16);
    z.next_in = const_cast<Bytef*>(p);
    z.avail_in = uInt(n);
    z.next_out = out.data();
    z.avail_out = uInt(out.size());
    deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return out;
}

inline std::vector<std::uint8_t> compress_hunk(Codec c, const std::uint8_t* h, std::uint32_t hunkbytes) {
    if (c == Codec::kZlib) return deflate_raw(h, hunkbytes);
    const std::uint32_t frames = hunkbytes / 2448;
    std::vector<std::uint8_t> base, sub;
    for (std::uint32_t f = 0; f < frames; ++f) {
        base.insert(base.end(), h + f * 2448, h + f * 2448 + 2352);
        sub.insert(sub.end(), h + f * 2448 + 2352, h + f * 2448 + 2448);
    }
    const std::vector<std::uint8_t> zb = deflate_raw(base.data(), base.size()), zs = deflate_raw(sub.data(), sub.size());
    std::vector<std::uint8_t> out((frames + 7) / 8, 0);   // no frame's ECC to regenerate
    add_be(out, zb.size(), hunkbytes < 65536 ? 2 : 3);
    out.insert(out.end(), zb.begin(), zb.end());
    out.insert(out.end(), zs.begin(), zs.end());
    return out;
}

struct Bits {
    std::vector<std::uint8_t> bytes;
    int used = 0;   // bits used in the last byte
    void put(std::uint32_t v, int n) {
        for (int b = n - 1; b >= 0; --b) {
            if (used == 0) bytes.push_back(0);
            if ((v >> b) & 1) bytes.back() |= std::uint8_t(0x80 >> used);
            used = (used + 1) % 8;
        }
    }
};
inline int bits_for(std::uint64_t v) { int n = 1; while (n < 32 && (v >> n)) ++n; return n; }

// `data`: the logical bytes (the last hunk padded with zeros)
inline Image write(const std::vector<std::uint8_t>& data, const Options& o) {
    Image img;
    const std::uint32_t hb = o.hunkbytes;
    const std::uint32_t hunks = std::uint32_t((data.size() + hb - 1) / hb);
    std::vector<std::vector<std::uint8_t>> raw(hunks);
    for (std::uint32_t h = 0; h < hunks; ++h) {
        raw[h].assign(hb, 0);
        const std::size_t from = std::size_t(h) * hb;
        std::memcpy(raw[h].data(), data.data() + from, std::min<std::size_t>(hb, data.size() - from));
    }
    std::vector<std::uint8_t>& f = img.bytes;
    f.assign(124, 0);
    std::memcpy(f.data(), "MComprHD", 8);
    put_be(f, 8, 124, 4);
    put_be(f, 12, 5, 4);
    if (o.codec != Codec::kNone) put_be(f, 16, tag(o.codec == Codec::kZlib ? "zlib" : "cdzl"), 4);
    put_be(f, 32, data.size(), 8);
    put_be(f, 56, hb, 4);
    put_be(f, 60, o.unitbytes, 4);
    if (o.parent) f[104] = 0x5A;
    auto metadata = [&]() {   // after the map
        if (o.metadata.empty()) return;
        put_be(f, 48, f.size(), 8);
        for (std::size_t k = 0; k < o.metadata.size(); ++k) {
            const std::string& text = o.metadata[k].second;
            const std::size_t start = f.size();
            add_be(f, tag(o.metadata[k].first.c_str()), 4);
            add_be(f, std::uint32_t(0x01) << 24 | std::uint32_t(text.size() + 1), 4);   // flags: checksummed
            add_be(f, 0, 8);
            f.insert(f.end(), text.begin(), text.end());
            f.push_back(0);
            if (k + 1 < o.metadata.size()) put_be(f, start + 8, f.size(), 8);
        }
    };
    img.hunk_at.assign(hunks, 0);
    if (o.codec == Codec::kNone) {   // the plain map; hunks aligned to their size; a hunk of zeros is not stored
        put_be(f, 40, f.size(), 8);
        const std::size_t map = f.size();
        f.resize(map + std::size_t(hunks) * 4, 0);
        metadata();
        f.resize((f.size() + hb - 1) / hb * hb + hb, 0);   // (never offset 0: that is "zeros")
        img.first_hunk = f.size();
        for (std::uint32_t h = 0; h < hunks; ++h) {
            bool zero = true;
            for (std::uint8_t b : raw[h]) zero = zero && b == 0;
            if (zero) continue;
            img.hunk_at[h] = f.size();
            put_be(f, map + std::size_t(h) * 4, f.size() / hb, 4);
            f.insert(f.end(), raw[h].begin(), raw[h].end());
        }
        f.resize(f.size() + 16, 0);   // the map may not end the file
        return img;
    }
    // compressed: each hunk compressed (type 0), stored (4: when that is no smaller) or a repeat of an earlier one (5)
    struct Entry { std::uint8_t type; std::vector<std::uint8_t> body; std::uint32_t self = 0; std::uint16_t crc = 0; };
    std::vector<Entry> e(hunks);
    std::map<std::vector<std::uint8_t>, std::uint32_t> seen;
    std::uint32_t maxlen = 1, maxself = 1;
    for (std::uint32_t h = 0; h < hunks; ++h) {
        e[h].crc = crc16(raw[h].data(), hb);
        if (const auto it = seen.find(raw[h]); it != seen.end()) {
            e[h].type = 5;
            e[h].self = it->second;
            maxself = std::max(maxself, it->second);
            continue;
        }
        seen.emplace(raw[h], h);
        std::vector<std::uint8_t> c = compress_hunk(o.codec, raw[h].data(), hb);
        if (c.size() < hb) {
            e[h].type = 0;
            maxlen = std::max<std::uint32_t>(maxlen, std::uint32_t(c.size()));
            e[h].body = std::move(c);
        } else {
            e[h].type = 4;
            e[h].body = raw[h];
        }
    }
    const int lengthbits = bits_for(maxlen), selfbits = bits_for(maxself);
    Bits bits;
    for (int k = 0; k < 16; ++k) bits.put(4, 4);   // the tree: 16 codes of 4 bits
    for (const Entry& x : e) bits.put(x.type, 4);
    for (const Entry& x : e) {
        if (x.type == 0) { bits.put(std::uint32_t(x.body.size()), lengthbits); bits.put(x.crc, 16); }
        else if (x.type == 4) bits.put(x.crc, 16);
        else bits.put(x.self, selfbits);
    }
    const std::size_t mapoff = f.size();
    put_be(f, 40, mapoff, 8);
    f.resize(mapoff + 16 + bits.bytes.size(), 0);
    std::memcpy(f.data() + mapoff + 16, bits.bytes.data(), bits.bytes.size());
    metadata();
    img.first_hunk = f.size();
    // the decoded map (its CRC in the map's header): type, length (24), offset (48), CRC (16)
    std::vector<std::uint8_t> decoded(std::size_t(hunks) * 12, 0);
    std::uint64_t at = img.first_hunk;
    for (std::uint32_t h = 0; h < hunks; ++h) {
        std::uint8_t* d = &decoded[std::size_t(h) * 12];
        d[0] = e[h].type;
        std::uint64_t off = e[h].type == 5 ? e[h].self : at;
        const std::uint32_t len = e[h].type == 5 ? 0 : std::uint32_t(e[h].body.size());
        for (int k = 0; k < 3; ++k) d[1 + k] = std::uint8_t(len >> (8 * (2 - k)));
        for (int k = 0; k < 6; ++k) d[4 + k] = std::uint8_t(off >> (8 * (5 - k)));
        const std::uint16_t crc = e[h].type == 5 ? 0 : e[h].crc;
        d[10] = std::uint8_t(crc >> 8);
        d[11] = std::uint8_t(crc);
        if (e[h].type != 5) {
            img.hunk_at[h] = at;
            f.insert(f.end(), e[h].body.begin(), e[h].body.end());
            at += e[h].body.size();
        }
    }
    put_be(f, mapoff, bits.bytes.size(), 4);
    put_be(f, mapoff + 4, img.first_hunk, 6);
    put_be(f, mapoff + 10, crc16(decoded.data(), decoded.size()), 2);
    f[mapoff + 12] = std::uint8_t(lengthbits);
    f[mapoff + 13] = std::uint8_t(selfbits);
    f[mapoff + 14] = 0;
    f.resize(f.size() + 16, 0);
    return img;
}

// A CD's frames (2352 + 96 bytes each) from a cooked image's 2048-byte sectors, as a track of `type` would hold them:
// "MODE1" / "MODE2_FORM1" (the 2048 bytes first), "MODE2" (an 8-byte subheader, then them), "MODE1_RAW" / "MODE2_RAW"
// (sync, header with the mode, for mode 2 the subheader, then them; EDC / ECC left zero)
inline std::vector<std::uint8_t> cd_frames(const std::vector<std::uint8_t>& cooked, const std::string& type) {
    const std::size_t sectors = (cooked.size() + 2047) / 2048;
    std::vector<std::uint8_t> out(sectors * 2448, 0);
    const std::size_t off = type == "MODE1_RAW" ? 16 : type == "MODE2_RAW" ? 24 : type == "MODE2" ? 8 : 0;
    for (std::size_t s = 0; s < sectors; ++s) {
        std::uint8_t* fr = &out[s * 2448];
        if (off >= 16) {
            static const std::uint8_t sync[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
            std::memcpy(fr, sync, 12);
            fr[12] = std::uint8_t(s / 4500);   // MSF (BCD not needed by readers)
            fr[13] = std::uint8_t(s / 75 % 60);
            fr[14] = std::uint8_t(s % 75);
            fr[15] = off == 16 ? 1 : 2;
        }
        std::memcpy(fr + off, cooked.data() + s * 2048, std::min<std::size_t>(2048, cooked.size() - s * 2048));
        for (int k = 0; k < 96; ++k) fr[2352 + k] = std::uint8_t(s + std::size_t(k));   // subcode: not part of the data
    }
    return out;
}

}  // namespace chdtest

#endif
