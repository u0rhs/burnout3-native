// carmatch.h -- which car file a captured vehicle data header is (View::match_car). taxihead (2026-11-22): the header's
// bytes 0x00..0x4C are not enough -- the three taxis (PVEH/COMP/CAR15 US, CAR16, CAR17 Asia) and the trailers HEVY/CAR24 /
// 25 share them, so the first file won and the Asia taxi was drawn as the US one. The words from 0x4C on (the LOD blocks'
// and the parts' offsets) tell them apart, relocated in guest memory: each is the file's, or the file's plus the one load
// address the game added to all of them.
#ifndef B3REPLAY_CARMATCH_H
#define B3REPLAY_CARMATCH_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace b3replay {

// The header words [0x4C, end) of a captured vehicle data (`head`, `end` bytes) against a file's: every word equal, or the
// file's plus one common load offset (the same for every relocated word). false: another file's.
inline bool head_tail_fits(const std::uint8_t* file, std::size_t file_size, const std::uint8_t* head, std::size_t end) {
    if (file_size < end) return false;
    bool have = false;
    std::uint32_t delta = 0;
    for (std::size_t k = 0x4C; k + 4 <= end; k += 4) {
        std::uint32_t f, h;
        std::memcpy(&f, file + k, 4);
        std::memcpy(&h, head + k, 4);
        if (f == h) continue;
        const std::uint32_t d = h - f;
        if (!have) { delta = d; have = true; }
        else if (d != delta) return false;
    }
    return true;
}

}  // namespace b3replay

#endif
