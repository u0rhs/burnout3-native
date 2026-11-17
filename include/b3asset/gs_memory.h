// GS local memory model (4 MiB) for decoding PS2 textures offline (docs/native-renderer.md "Asset decoders").
//
// The game's rasters are stored as the bytes the GS receives (host->local image transfers under a BITBLTBUF/TRXPOS/
// TRXREG, often uploaded as PSMCT32/PSMCT16 and sampled as PSMT8/PSMT4: RW sky2's "swizzled" rasters). Rather than
// re-deriving each swizzle, the decoder replays the transfers into this model and samples it with the texture's TEX0,
// exactly as the GS does. Addressing: GS User's Manual ch. 8 (page / block / column layout per pixel storage format),
// the same layouts as librw's swizzle() and paraLLEl-GS's swizzle_utils (cross-checked in tests/test_b3asset.cpp).
#ifndef B3ASSET_GS_MEMORY_H
#define B3ASSET_GS_MEMORY_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace b3asset {

enum Psm : std::uint8_t {
    PSMCT32 = 0x00, PSMCT24 = 0x01, PSMCT16 = 0x02, PSMCT16S = 0x0A,
    PSMT8 = 0x13, PSMT4 = 0x14, PSMT8H = 0x1B, PSMT4HL = 0x24, PSMT4HH = 0x2C,
};

// Bits per pixel of a storage format (0 for an unknown one).
int psm_bits(int psm);
const char* psm_name(int psm);

struct Bitbltbuf { std::uint32_t dbp = 0, dbw = 0, dpsm = 0; };

class GsMemory {
public:
    static constexpr std::size_t kSize = 4u << 20;
    GsMemory();

    // Byte address of pixel (x, y) in a buffer at block `bp` with width `bw` (units of 64 pixels). For the 4-bit formats
    // the result is a nibble address (byte = a >> 1, high nibble when a & 1).
    static std::uint32_t address(int psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y);

    std::uint32_t read(int psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y) const;
    void write(int psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y, std::uint32_t v);

    // Host->local transfer state (BITBLTBUF, TRXPOS, TRXREG; TRXDIR 0 starts it). Feed image data in any chunking.
    void set_bitbltbuf(std::uint64_t v);
    void set_trxpos(std::uint64_t v);
    void set_trxreg(std::uint64_t v);
    void start_transfer();
    // Returns the number of bytes consumed (stops when the TRXREG rectangle is full).
    std::size_t transfer(const std::uint8_t* data, std::size_t bytes);
    bool transfer_active() const { return active_; }

    // Clears what was written since the last clear (cheap reuse between rasters).
    void clear();
    const std::uint8_t* data() const { return mem_.data(); }

private:
    std::vector<std::uint8_t> mem_;
    std::size_t lo_ = kSize, hi_ = 0;  // dirty byte range
    Bitbltbuf buf_;
    std::uint32_t dsax_ = 0, dsay_ = 0, rrw_ = 0, rrh_ = 0, x_ = 0, y_ = 0;
    bool active_ = false;
    std::uint32_t pending_ = 0, pending_bits_ = 0;  // partial pixel across chunk edges
    void touch(std::size_t a, std::size_t n);
    void put_pixel(std::uint32_t v);
};

}  // namespace b3asset

#endif
