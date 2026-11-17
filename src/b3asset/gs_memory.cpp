#include "b3asset/gs_memory.h"

#include <algorithm>
#include <cstring>

namespace b3asset {

int psm_bits(int psm) {
    switch (psm) {
    case PSMCT32: case PSMCT24: case PSMT8H: case PSMT4HL: case PSMT4HH: return 32;
    case PSMCT16: case PSMCT16S: return 16;
    case PSMT8: return 8;
    case PSMT4: return 4;
    default: return 0;
    }
}

const char* psm_name(int psm) {
    switch (psm) {
    case PSMCT32: return "CT32"; case PSMCT24: return "CT24"; case PSMCT16: return "CT16"; case PSMCT16S: return "CT16S";
    case PSMT8: return "T8"; case PSMT4: return "T4"; case PSMT8H: return "T8H"; case PSMT4HL: return "T4HL";
    case PSMT4HH: return "T4HH"; default: return "?";
    }
}

namespace {
// Block numbers inside a page. 32-bit / 8-bit pages are 8x4 blocks, 16-bit / 4-bit pages 4x8 blocks.
inline std::uint32_t block32(std::uint32_t bx, std::uint32_t by) {
    return (bx & 1) | ((by & 1) << 1) | ((bx & 2) << 1) | ((by & 2) << 2) | ((bx & 4) << 2);
}
inline std::uint32_t block16(std::uint32_t bx, std::uint32_t by) {
    return (by & 1) | ((bx & 1) << 1) | ((by & 2) << 1) | ((bx & 2) << 2) | ((by & 4) << 2);
}
inline std::uint32_t block16s(std::uint32_t bx, std::uint32_t by) {
    return (by & 1) | ((bx & 1) << 1) | (by & 4) | ((by & 2) << 2) | ((bx & 2) << 3);
}
inline std::uint32_t blk(std::uint32_t bp, std::uint32_t page, std::uint32_t b) {
    return ((bp + page * 32 + b) & 0x3FFF) * 256;  // block address space wraps at 4 MiB
}
}  // namespace

std::uint32_t GsMemory::address(int psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y) {
    x &= 2047;
    y &= 2047;
    switch (psm) {
    case PSMCT32: case PSMCT24: case PSMT8H: case PSMT4HL: case PSMT4HH: {
        const std::uint32_t page = (y >> 5) * bw + (x >> 6);
        const std::uint32_t pix = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((x & 4) << 1);
        return blk(bp, page, block32((x >> 3) & 7, (y >> 3) & 3)) + ((y >> 1) & 3) * 64 + pix * 4;
    }
    case PSMCT16: case PSMCT16S: {
        const std::uint32_t page = (y >> 6) * bw + (x >> 6);
        const std::uint32_t b = psm == PSMCT16 ? block16((x >> 4) & 3, (y >> 3) & 7) : block16s((x >> 4) & 3, (y >> 3) & 7);
        const std::uint32_t hw = ((x >> 3) & 1) | ((x & 1) << 1) | ((y & 1) << 2) | ((x & 2) << 2) | ((x & 4) << 2);
        return blk(bp, page, b) + ((y >> 1) & 3) * 64 + hw * 2;
    }
    case PSMT8: {
        const std::uint32_t page = (y >> 6) * (bw >> 1) + (x >> 7);
        const std::uint32_t col = (y >> 2) & 3, px = x & 15, py = y & 3;
        const std::uint32_t pix = ((py & 2) >> 1) | ((px & 8) >> 2) | ((px & 1) << 2) | ((py & 1) << 3) | ((px & 2) << 3) |
                                  (((px & 4) << 3) ^ ((py & 2) << 4) ^ ((col & 1) << 5));
        return blk(bp, page, block32((x >> 4) & 7, (y >> 4) & 3)) + col * 64 + pix;
    }
    case PSMT4: {
        const std::uint32_t page = (y >> 7) * (bw >> 1) + (x >> 7);
        const std::uint32_t col = (y >> 2) & 3, px = x & 31, py = y & 3;
        const std::uint32_t nib = ((py & 2) >> 1) | ((px & 8) >> 2) | ((px & 16) >> 2) | ((px & 1) << 3) | ((py & 1) << 4) |
                                  ((px & 2) << 4) | (((px & 4) << 4) ^ ((py & 2) << 5) ^ ((col & 1) << 6));
        return (blk(bp, page, block16((x >> 5) & 3, (y >> 4) & 7)) + col * 64) * 2 + nib;
    }
    default:
        return 0;
    }
}

GsMemory::GsMemory() : mem_(kSize, 0) {}

void GsMemory::touch(std::size_t a, std::size_t n) {
    lo_ = std::min(lo_, a);
    hi_ = std::max(hi_, a + n);
}

void GsMemory::clear() {
    if (hi_ > lo_) std::memset(mem_.data() + lo_, 0, hi_ - lo_);
    lo_ = kSize;
    hi_ = 0;
    active_ = false;
}

std::uint32_t GsMemory::read(int psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y) const {
    const std::uint32_t a = address(psm, bp, bw, x, y);
    const std::uint8_t* m = mem_.data();
    switch (psm) {
    case PSMCT32: { std::uint32_t v; std::memcpy(&v, m + a, 4); return v; }
    case PSMCT24: return m[a] | (m[a + 1] << 8) | (m[a + 2] << 16);
    case PSMCT16: case PSMCT16S: return m[a] | (m[a + 1] << 8);
    case PSMT8: return m[a];
    case PSMT4: return (m[a >> 1] >> ((a & 1) * 4)) & 0xF;
    case PSMT8H: return m[a + 3];
    case PSMT4HL: return m[a + 3] & 0xF;
    case PSMT4HH: return m[a + 3] >> 4;
    default: return 0;
    }
}

void GsMemory::write(int psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y, std::uint32_t v) {
    const std::uint32_t a = address(psm, bp, bw, x, y);
    std::uint8_t* m = mem_.data();
    switch (psm) {
    case PSMCT32: std::memcpy(m + a, &v, 4); touch(a, 4); break;
    case PSMCT24: m[a] = v; m[a + 1] = v >> 8; m[a + 2] = v >> 16; touch(a, 3); break;
    case PSMCT16: case PSMCT16S: m[a] = v; m[a + 1] = v >> 8; touch(a, 2); break;
    case PSMT8: m[a] = v; touch(a, 1); break;
    case PSMT4: {
        std::uint8_t& b = m[a >> 1];
        b = (a & 1) ? (b & 0x0F) | ((v & 0xF) << 4) : (b & 0xF0) | (v & 0xF);
        touch(a >> 1, 1);
        break;
    }
    case PSMT8H: m[a + 3] = v; touch(a + 3, 1); break;
    case PSMT4HL: m[a + 3] = (m[a + 3] & 0xF0) | (v & 0xF); touch(a + 3, 1); break;
    case PSMT4HH: m[a + 3] = (m[a + 3] & 0x0F) | ((v & 0xF) << 4); touch(a + 3, 1); break;
    default: break;
    }
}

void GsMemory::set_bitbltbuf(std::uint64_t v) {
    buf_.dbp = (v >> 32) & 0x3FFF;
    buf_.dbw = (v >> 48) & 0x3F;
    buf_.dpsm = (v >> 56) & 0x3F;
}
void GsMemory::set_trxpos(std::uint64_t v) {
    dsax_ = (v >> 32) & 0x7FF;
    dsay_ = (v >> 48) & 0x7FF;
}
void GsMemory::set_trxreg(std::uint64_t v) {
    rrw_ = v & 0xFFF;
    rrh_ = (v >> 32) & 0xFFF;
}
void GsMemory::start_transfer() {
    x_ = y_ = 0;
    pending_ = pending_bits_ = 0;
    active_ = rrw_ && rrh_ && psm_bits(buf_.dpsm);
}

void GsMemory::put_pixel(std::uint32_t v) {
    write(buf_.dpsm, buf_.dbp, buf_.dbw, dsax_ + x_, dsay_ + y_, v);
    if (++x_ == rrw_) {
        x_ = 0;
        if (++y_ == rrh_) active_ = false;
    }
}

std::size_t GsMemory::transfer(const std::uint8_t* data, std::size_t bytes) {
    // Transfer pixel size: CT24 sends 3 bytes, the H formats send their 8 / 4 bits only.
    const int bits = buf_.dpsm == PSMCT24 ? 24 : buf_.dpsm == PSMT8H ? 8
                   : (buf_.dpsm == PSMT4HL || buf_.dpsm == PSMT4HH) ? 4 : psm_bits(buf_.dpsm);
    std::size_t i = 0;
    while (active_ && i < bytes) {
        if (bits == 4) {
            put_pixel(data[i] & 0xF);
            if (active_) put_pixel(data[i] >> 4);
            ++i;
            continue;
        }
        pending_ |= std::uint32_t(data[i++]) << pending_bits_;
        pending_bits_ += 8;
        if (int(pending_bits_) == bits) {
            put_pixel(pending_);
            pending_ = pending_bits_ = 0;
        }
    }
    return i;
}

}  // namespace b3asset
