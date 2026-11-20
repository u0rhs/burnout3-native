// grab_block.h -- one 16-byte block of a host buffer for the scene capture's grab (scene_capture.cpp): the capture works
// in whole 16-byte blocks, and a host buffer (the split screen's view-1 copy, an 8-byte entry list after a 16-byte head)
// need not end on one. Only [a, a + n) comes from the buffer; the rest of the block is 0, so nothing is read past the
// buffer and the .b3scene output does not depend on what lies after it.
#ifndef B3GV_GRAB_BLOCK_H
#define B3GV_GRAB_BLOCK_H

#include <cstdint>
#include <cstring>

namespace b3gv {

// out = the block at address b (16-aligned) of the buffer `host` that holds addresses [a, a + n)
inline void grab_block(const std::uint8_t* host, std::uint32_t a, std::uint32_t n, std::uint32_t b, std::uint8_t out[16]) {
    std::memset(out, 0, 16);
    const std::uint64_t lo = b > a ? b : a, end = std::uint64_t(a) + n, hi = std::uint64_t(b) + 16 < end ? std::uint64_t(b) + 16 : end;
    if (lo < hi) std::memcpy(out + (lo - b), host + (lo - a), std::size_t(hi - lo));
}

}  // namespace b3gv

#endif
