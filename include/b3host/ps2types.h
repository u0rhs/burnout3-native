// b3host/ps2types.h -- PS2 (EE, MWCC) integer and 128-bit types for native code built on a 64-bit host.
//
// Names here are this project's own (b3host::*), except u_long128, whose name and host definition are the decomp's
// (burnout3-retail-decomp include/b3/GtMathPs2.h, host branch: `struct alignas(16) u_long128 { unsigned int w[4]; }`),
// kept identical so decomp headers and port headers agree on one type.
//
// Facts:
// - [C] MWCC for the EE has a 64-bit `long`/`unsigned long` (decomp docs/known.md N102: 64-bit `unsigned long` fields
//   are what reproduce retail's ld/sd moves). Windows (LLP64) has a 32-bit long and Linux (LP64) a 64-bit one, so
//   game code must not use `long` on the host: use b3host::ps2_long / ps2_ulong.
// - [C] `int`, pointers and `float` are 32-bit on the EE; guest pointers are 32-bit guest addresses (b3host/gp.h).
// - [C] u_long128 is a 128-bit EE GPR / VU0 register value (decomp: `mode(TI)` under MWCC); on the host it is a
//   16-byte, 16-aligned POD of four 32-bit words. Word i is lane i (x, y, z, w for VU vectors).
#ifndef B3HOST_PS2TYPES_H
#define B3HOST_PS2TYPES_H

#include <cstddef>
#include <cstdint>

#ifndef B3_U_LONG128_DEFINED
#define B3_U_LONG128_DEFINED
struct alignas(16) u_long128 { unsigned int w[4]; };
#endif

static_assert(sizeof(u_long128) == 16 && alignof(u_long128) == 16, "u_long128 must be the EE's 16-byte quadword");

namespace b3host {

using gaddr = std::uint32_t;          // a guest (EE virtual) address
using ps2_char = std::int8_t;         // MWCC `char` is signed [L: MIPS EABI default; check per use]
using ps2_short = std::int16_t;
using ps2_int = std::int32_t;
using ps2_long = std::int64_t;        // MWCC `long`            [C N102]
using ps2_ulong = std::uint64_t;      // MWCC `unsigned long`   [C N102]
using ps2_longlong = std::int64_t;
using ps2_ulonglong = std::uint64_t;
using ps2_float = float;              // binary32 layout; arithmetic semantics differ (b3host/ps2float.h)

// Sony SDK spellings (u_char .. u_long), as the EE's sys/types.h defines them [L: SDK 3.0.3 ee/include]. Not defined
// globally because <sys/types.h> (Linux) and <winsock2.h> (Windows: 32-bit u_long) own those global names.
namespace sce {
using u_char = unsigned char;
using u_short = unsigned short;
using u_int = unsigned int;
using u_long = ps2_ulong;
}  // namespace sce

static_assert(sizeof(ps2_long) == 8 && sizeof(ps2_ulong) == 8, "MWCC EE long is 64-bit");
static_assert(sizeof(int) == 4 && sizeof(float) == 4, "host int/float must match the EE");

inline u_long128 make_u128(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t w) {
    u_long128 r;
    r.w[0] = x; r.w[1] = y; r.w[2] = z; r.w[3] = w;
    return r;
}
inline bool u128_equal(const u_long128& a, const u_long128& b) {
    return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2] && a.w[3] == b.w[3];
}

}  // namespace b3host

#endif
