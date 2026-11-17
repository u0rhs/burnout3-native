// include/b3host/vu_random.h -- the VU0 random unit (the R register), written from docs/specs/eeasm-ops.md §4.
// Dependency-free: used by b3host/eeasm_ops.h (VRINIT / VRXOR / VRNEXT / CTC2 $vi20) and by the renderer's boost
// coronas (src/b3replay/coronas.cpp boost_random).
//
// R is a 23-bit linear-feedback shift register that reads as a float in [1, 2): the register word is the exponent and
// sign of 1.0 (0x3F800000) with the 23-bit state as its fraction.
//   VRINIT: the state becomes the low 23 bits of the source word.
//   VRNEXT: the state shifts left by one (bit 22 drops out); the new bit 0 is bit 4 XOR bit 22 of the old state.
#ifndef B3HOST_VU_RANDOM_H
#define B3HOST_VU_RANDOM_H

#include <cstdint>

namespace b3vurand {

inline constexpr std::uint32_t kOne = 0x3F800000u;       // 1.0f: the fixed upper bits of the register word
inline constexpr std::uint32_t kStateMask = 0x007FFFFFu;  // the 23 state bits

inline constexpr std::uint32_t r_init(std::uint32_t src) { return kOne | (src & kStateMask); }

inline constexpr std::uint32_t r_next(std::uint32_t r) {
    const std::uint32_t s = r & kStateMask;
    const std::uint32_t feedback = ((s >> 4) ^ (s >> 22)) & 1u;
    return kOne | ((s << 1) & kStateMask) | feedback;
}

}  // namespace b3vurand

#endif
