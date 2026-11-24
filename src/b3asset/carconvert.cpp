// carconvert.cpp -- betaimport: a prototype disc's vehicle file to the game's layout (include/b3asset/carconvert.h).
#include "b3asset/carconvert.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "b3asset/car.h"   // car_file_error, car_layout.h

namespace b3asset {

namespace {

std::string hex(std::size_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "0x%zX", v);
    return b;
}
std::uint32_t rd(Bytes f, std::size_t at) { return f.has(at, 4) ? f.u32(at) : 0; }
void wr(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) b[at + std::size_t(k)] = std::uint8_t(v >> (8 * k));
}
std::uint32_t rdv(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24;
}
std::size_t align(std::size_t v, std::size_t a) { return (v + a - 1) / a * a; }

// The extent of everything the game's FixUp reaches in a vehicle file of either layout (`abs_slots`: a LOD block's list
// offsets are from the file's start, version 0x14). Fails on an offset outside the file.
struct Walk {
    Bytes f;
    bool abs_slots = false;
    std::size_t end = 0;
    int lists = 0, packets = 0;
    std::string why;
    // base + a 32-bit offset as the game adds them (wrapping: an offset may lead backwards)
    static std::size_t at32(std::size_t base, std::uint32_t rel) { return std::size_t(std::uint32_t(base + rel)); }
    bool reach(std::size_t at, std::size_t len, const char* what) {
        if (!f.has(at, len)) {
            why = std::string(what) + " at " + hex(at) + " outside the file";
            return false;
        }
        end = std::max(end, at + len);
        return true;
    }
    bool packet(std::size_t at) {
        if (!reach(at, 16, "a mesh packet")) return false;
        const std::uint32_t tag = f.u32(at);
        if ((tag & 0xFFFF0000u) != 0x60000000u) {
            why = "the mesh at " + hex(at) + " is not a DMA RET packet";
            return false;
        }
        ++packets;
        return reach(at, 16 + std::size_t(tag & 0xFFFF) * 16, "a mesh packet");
    }
    // a mesh list {s8 count, s8 glass, 2, u32 entries from the list, u32}; entries {mesh, continuation (from the entry), ..}
    // `valid`: (the +0x64 / +0x68 lists) the entries before the first that is not a mesh are kept, the count is set
    // there (one prototype file's list counts a second entry of stray bytes); else such an entry fails the file
    bool list(std::size_t at, int* valid = nullptr) {
        if (!reach(at, 12, "a mesh list")) return false;
        ++lists;
        const int n = std::int8_t(f.p[at]);
        if (n < 0) {
            why = "the mesh list at " + hex(at) + " has a negative count";
            return false;
        }
        const std::size_t ents = at32(at, f.u32(at + 4));
        if (n && !reach(ents, 16 * std::size_t(n), "a mesh list's entries")) return false;
        if (valid) *valid = n;
        for (int i = 0; i < n; ++i) {
            const std::size_t e = ents + 16 * std::size_t(i);
            const std::size_t keep_end = end;
            const int keep_packets = packets;
            const std::uint32_t c = f.u32(e + 4);
            if (packet(at32(e, f.u32(e))) && (!c || packet(at32(e, c)))) continue;
            if (!valid) return false;
            *valid = i;   // this entry and the rest dropped
            end = keep_end;
            packets = keep_packets;
            why.clear();
            break;
        }
        return true;
    }
    bool lod_block(std::size_t at) {
        if (!reach(at, 72, "a LOD block")) return false;
        for (int s = 0; s < 18; ++s) {
            const std::uint32_t o = f.u32(at + 4 * std::size_t(s));
            if (!o) continue;
            if (abs_slots && o < at) {
                why = "the LOD block at " + hex(at) + " has a list before it (" + hex(o) + ")";
                return false;
            }
            if (!list(abs_slots ? o : at32(at, o))) return false;
        }
        return true;
    }
    bool texture(std::size_t at) {
        RasterHeader h;
        std::string e;
        if (!read_raster_header(f, at, h, &e)) {
            why = "the texture at " + hex(at) + ": " + e;
            return false;
        }
        if (!reach(at, 0xC8, "the texture")) return false;
        const std::size_t up = at + h.upload;
        if (!reach(up, 16 + 64 * std::size_t(h.transfers), "the texture's upload")) return false;
        for (std::uint32_t k = 0; k < h.transfers; ++k) {
            const std::size_t t = up + 16 + 64 * std::size_t(k);
            if (!reach(at32(at, f.u32(t + 52)), std::size_t(f.u32(t + 48) & 0xFFFF) * 16, "the texture's pixels")) return false;
        }
        return true;
    }
};

// The donor's engine-sound structures: [from, n) with +0x16DC / +0x16E0 in it, each walked as func_002FB240 fixes
// it up (+0x3C entries from the structure, x count at +0x40; an entry's +8 sub-entries from the entry, x count at +0xC;
// a sub-entry's +0 from the sub-entry; signed: the sub-entries lie before their entries), every target inside [from, n).
std::string donor_sound(Bytes d, std::size_t& from) {
    if (const std::string e = car_file_error(d); !e.empty()) return "the donor: " + e;
    if (d.u32(0) != 0x17) return "the donor is not a game car file (version " + hex(d.u32(0)) + ")";
    const std::uint32_t hi = rd(d, 0x16DC), lo = rd(d, 0x16E0);
    if (!hi && !lo) return "the donor has no engine sound (+0x16DC / +0x16E0 are 0: a traffic vehicle's file?)";
    from = std::min(hi ? hi : lo, lo ? lo : hi);
    if (from < 0x16F0 || from >= d.n) return "the donor's engine sound at " + hex(from) + " is outside its data";
    auto inside = [&](std::int64_t at, std::size_t len) { return at >= std::int64_t(from) && d.has(std::size_t(at), len); };
    auto rel = [&](std::size_t base, std::size_t at) { return std::int64_t(base) + std::int32_t(d.u32(at)); };
    for (std::uint32_t s : {hi, lo}) {
        if (!s) continue;
        if (!inside(s, 0x44)) return "the donor's engine sound structure at " + hex(s) + " is outside its data";
        if (d.u32(s + 0x3C)) {
            const std::int64_t ents = rel(s, s + 0x3C);
            const unsigned n = d.p[s + 0x40];
            if (!inside(ents, 16 * std::size_t(n))) return "the donor's engine sound entries are outside its data";
            for (unsigned i = 0; i < n; ++i) {
                const std::size_t e = std::size_t(ents) + 16 * std::size_t(i);
                if (!d.u32(e + 8)) continue;
                const std::int64_t subs = rel(e, e + 8);
                const unsigned m = d.p[e + 0xC];
                if (!inside(subs, 8 * std::size_t(m))) return "the donor's engine sound sub-entries are outside its data";
                for (unsigned j = 0; j < m; ++j) {
                    const std::size_t g = std::size_t(subs) + 8 * std::size_t(j);
                    if (d.u32(g) && !inside(rel(g, g), 1)) return "the donor's engine sound data is outside it";
                }
            }
        }
    }
    // nothing else of the donor's in that tail (it is copied alone): the offsets FixUp moves all lie before it
    for (std::size_t k = 0; k < 5; ++k)
        if (const std::uint32_t o = d.u32(0x4C + 4 * k); o >= from) return "the donor's LOD block lies in its engine sound's tail";
    for (std::size_t at : {std::size_t(0x60), std::size_t(0x64), std::size_t(0x68), std::size_t(0x16C0), std::size_t(0x16C4)})
        if (rd(d, at) >= from) return "the donor's data at +" + hex(at) + " lies in its engine sound's tail";
    for (std::size_t k = 0; k < 18; ++k)
        if (rd(d, 0x1664 + 4 * k) >= from) return "the donor's lights lie in its engine sound's tail";
    return "";
}

}  // namespace

bool car_is_prototype(Bytes f) {
    if (!f.has(0, kCarHeaderBytes)) return false;
    const std::uint32_t v = f.u32(0);
    return (v == 0x14 || v == 0x15) && car_first_lod_block(f.p, f.n) == kCarOldFirstLodBlock;
}

std::string convert_prototype_car(Bytes f, Bytes donor, std::vector<std::uint8_t>& out, CarConvertInfo* info) {
    out.clear();
    if (!f.has(0, kCarOldFirstLodBlock + 72)) return "too short for a prototype vehicle file";
    const std::uint32_t version = f.u32(0);
    if (version != 0x14 && version != 0x15) return "not a prototype vehicle file (version " + hex(version) + "; 0x14 / 0x15 are converted)";
    if (car_first_lod_block(f.p, f.n) != kCarOldFirstLodBlock)
        return "not the prototype layout (its first LOD block at " + hex(car_first_lod_block(f.p, f.n)) + ", not 0x16E0)";
    std::size_t esm_from = 0;
    if (const std::string e = donor_sound(donor, esm_from); !e.empty()) return e;

    // where the data ends
    Walk w;
    w.f = f;
    w.abs_slots = version == 0x14;
    w.end = kCarOldFirstLodBlock;
    int blocks = 0;
    for (std::size_t k = 0; k < 5; ++k) {
        const std::uint32_t o = f.u32(0x4C + 4 * k);
        if (!o) continue;
        ++blocks;
        if (o < kCarOldFirstLodBlock) return "a LOD block at " + hex(o) + " inside the header";
        if (!w.lod_block(o)) return w.why;
    }
    if (const std::uint32_t t = f.u32(0x60)) {
        if (t < kCarOldFirstLodBlock) return "the texture at " + hex(t) + " inside the header";
        if (!w.texture(t)) return w.why;
    }
    int valid[2] = {-1, -1};   // the +0x64 / +0x68 lists' entries kept (-1: as the file has it)
    for (int k = 0; k < 2; ++k)
        if (const std::uint32_t o = f.u32(0x64 + 4 * std::size_t(k))) {
            if (o < kCarOldFirstLodBlock) return "the mesh list at +" + hex(0x64 + 4 * std::size_t(k)) + " inside the header";
            if (!w.list(o, &valid[k])) return w.why;
            if (valid[k] == std::int8_t(f.p[o])) valid[k] = -1;
        }
    for (std::size_t k = 0; k < 18; ++k)
        if (const std::uint32_t o = f.u32(0x1664 + 4 * k)) {
            if (o < kCarOldFirstLodBlock) return "a light group inside the header";
            if (!w.reach(o, 0x30 * std::size_t(f.p[0x16AC + k]), "a light group")) return w.why;
        }
    const std::uint32_t t16c0 = f.u32(0x16C0), t16c4 = f.u32(0x16C4);
    const int n16c0 = std::int8_t(f.p[0x16C8]);
    if (t16c0 || t16c4) {
        // a table whose targets' sizes are not known: everything up to the file's end is kept
        if ((t16c0 && t16c0 < kCarOldFirstLodBlock) || (t16c4 && t16c4 < kCarOldFirstLodBlock) || n16c0 < 0)
            return "the +0x16C0 table inside the header";
        if (t16c0 && !w.reach(t16c0, 12 * std::size_t(n16c0), "the +0x16C0 table")) return w.why;
        for (int i = 0; t16c0 && i < n16c0; ++i)
            if (const std::uint32_t o = f.u32(t16c0 + 12 * std::size_t(i) + 8); o < kCarOldFirstLodBlock || o >= f.n)
                return "a +0x16C0 table entry outside the file";
        w.end = f.n;
    }
    std::size_t last = f.n;
    while (last > w.end && !f.p[last - 1]) --last;   // any data past the structures (kept)
    // the data's end rounded up to 16 bytes: past the file's own end when its length is not a multiple of 16 (a file cut
    // after its last structure); those bytes are zeros, as the game reads past a file's end
    const std::size_t data_end = align(std::max(w.end, last), 16);

    // the converted file: the header, the gap, the data moved by the shift, the donor's engine sound
    const std::uint32_t S = kCarConvertShift;
    out.assign(f.p, f.p + kCarOldFirstLodBlock);
    out.resize(kCarOldFirstLodBlock + S, 0);
    out.insert(out.end(), f.p + kCarOldFirstLodBlock, f.p + std::min(data_end, f.n));
    out.resize(kCarOldFirstLodBlock + S + (data_end - kCarOldFirstLodBlock), 0);
    auto moved = [&](std::uint32_t o) { return o ? o + S : 0u; };
    wr(out, 0x00, 0x17);
    for (std::size_t k = 0; k < 5; ++k) {
        const std::uint32_t o = f.u32(0x4C + 4 * k);
        if (!o) continue;
        wr(out, 0x4C + 4 * k, o + S);
        if (version == 0x14)
            for (std::size_t s = 0; s < 18; ++s)
                if (const std::uint32_t v = f.u32(o + 4 * s)) wr(out, o + S + 4 * s, v - o);   // from the block
    }
    for (std::size_t at : {std::size_t(0x60), std::size_t(0x64), std::size_t(0x68), std::size_t(0x16C0), std::size_t(0x16C4)})
        wr(out, at, moved(f.u32(at)));
    for (std::size_t k = 0; k < 18; ++k) wr(out, 0x1664 + 4 * k, moved(f.u32(0x1664 + 4 * k)));
    for (int k = 0; k < 2; ++k)
        if (valid[k] >= 0) out[f.u32(0x64 + 4 * std::size_t(k)) + S] = std::uint8_t(valid[k]);
    for (int i = 0; t16c0 && i < n16c0; ++i) {
        const std::size_t e = t16c0 + S + 12 * std::size_t(i) + 8;
        wr(out, e, rdv(out, e) + S);
    }
    const std::size_t esm_at = align(out.size(), 16);
    out.resize(esm_at, 0);
    out.insert(out.end(), donor.p + esm_from, donor.p + donor.n);
    const std::uint32_t hi = donor.u32(0x16DC), lo = donor.u32(0x16E0);
    wr(out, 0x16DC, hi ? std::uint32_t(esm_at + (hi - esm_from)) : 0);
    wr(out, 0x16E0, lo ? std::uint32_t(esm_at + (lo - esm_from)) : 0);
    std::size_t size = align(out.size(), 0x800);
    if (size > kCarMemoryBlock) size = align(out.size(), 16);
    if (size > kCarMemoryBlock) {
        const std::size_t n = size;
        out.clear();
        return "too big for the game's car memory (" + std::to_string(n) + " bytes, at most " + std::to_string(kCarMemoryBlock) + ")";
    }
    out.resize(size, 0);
    wr(out, 0x08, std::uint32_t(size));
    if (const std::string e = car_file_error(Bytes{out.data(), out.size()}); !e.empty()) {
        out.clear();
        return "the converted file is not taken: " + e;
    }
    if (info) {
        info->version = version;
        info->data_end = data_end;
        info->esm_at = esm_at;
        info->esm_size = donor.n - esm_from;
        info->size = size;
        info->lod_blocks = blocks;
        info->lists = w.lists;
        info->packets = w.packets;
        info->dropped_entries = (valid[0] >= 0 ? std::int8_t(f.p[f.u32(0x64)]) - valid[0] : 0) + (valid[1] >= 0 ? std::int8_t(f.p[f.u32(0x68)]) - valid[1] : 0);
    }
    return "";
}

}  // namespace b3asset
