// The host model behind decomp inline asm (b3host/eeasm.h) and the translator's output (tools/decomp2host/eeasm.py).
//   - model edge cases with their source: [S] the specs docs/specs/eeasm-ops.md / ps2-float.md (section cited),
//     [M] the Sony manuals; bit-exact rows run in the PS2-faithful build only;
//   - build/eeasm_cases.h: tests/eeasm_cases.txt (decomp-syntax asm statements) translated at build time, run in both
//     float modes on exact small-number inputs;
//   - an unknown instruction must fail at generation time: `make test` also runs `eeasm.py --selftest`, which checks
//     that unknown instructions, branches, vcallms, directives and macro-built templates are refused.
#include "b3host/eeasm.h"
#include "test.h"

#include <bit>
#include <utility>

namespace b3asm {
inline float lane_f(const Q& q, unsigned i) { return std::bit_cast<float>(q.w[i]); }
}  // namespace b3asm

namespace {
u_long128 V(float x, float y, float z, float w) { return b3vu::make(x, y, z, w); }
u_long128 I4(int a, int b, int c, int d) {
    return b3host::make_u128(static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c),
                             static_cast<std::uint32_t>(d));
}
}  // namespace

#define EXPECT_V(q, x, y, z, w) do { const u_long128 q_ = (q); \
    CHECK(b3asm::lane_f(q_, 0) == (float)(x)); CHECK(b3asm::lane_f(q_, 1) == (float)(y)); \
    CHECK(b3asm::lane_f(q_, 2) == (float)(z)); CHECK(b3asm::lane_f(q_, 3) == (float)(w)); } while (0)
#define EXPECT_I(q, a, b, c, d) do { const u_long128 q_ = (q); \
    CHECK_BITS(q_.w[0], (std::uint32_t)(a)); CHECK_BITS(q_.w[1], (std::uint32_t)(b)); \
    CHECK_BITS(q_.w[2], (std::uint32_t)(c)); CHECK_BITS(q_.w[3], (std::uint32_t)(d)); } while (0)

#include "eeasm_cases.h"

TEST(fdiv_unit_edges) {
#if B3_PS2_FLOAT
    CHECK_BITS(b3asm::vsqrt(0x3FC00000u), 0x3F9CC470u);                 // [S] ps2-float §4.4: sqrt(1.5) chops (host RN ...471)
    CHECK_BITS(b3asm::vdiv(0x3F800000u, 0u), 0x7F7FFFFFu);              // [S] ps2-float §4.4: x/0 -> +Fmax
    CHECK_BITS(b3asm::vdiv(0xBF800000u, 0u), 0xFF7FFFFFu);              //     sign from the operands
    CHECK_BITS(b3asm::vdiv(0u, 0u), 0x7F7FFFFFu);                       //     0/0 -> Fmax too (STATUS tells them apart)
    CHECK_BITS(b3asm::vrsqrt(0u, 0u), 0u);                              // [S] ps2-float §4.4: 0/sqrt(0) -> 0
    CHECK_BITS(b3asm::vrsqrt(0x3F800000u, 0u), 0x7F7FFFFFu);            //     x/sqrt(0) -> Fmax
    CHECK_BITS(b3asm::vsqrt(0xC0800000u), 0x40000000u);                 // [M] VU 2.3: sqrt of a negative = sqrt(|x|)
#endif
    CHECK(std::bit_cast<float>(b3asm::vdiv(std::bit_cast<std::uint32_t>(8.0f), std::bit_cast<std::uint32_t>(2.0f))) == 4.0f);
}

TEST(ftoi_itof) {
    CHECK_BITS(b3asm::ftoi_bits(std::bit_cast<std::uint32_t>(1.5f), 0), 1u);          // [S] eeasm-ops §3 VFTOIn: toward 0
    CHECK_BITS(b3asm::ftoi_bits(std::bit_cast<std::uint32_t>(-1.5f), 0), 0xFFFFFFFFu);
    CHECK_BITS(b3asm::ftoi_bits(std::bit_cast<std::uint32_t>(1.5f), 4), 24u);         // scaled by 2^4
    CHECK_BITS(b3asm::ftoi_bits(std::bit_cast<std::uint32_t>(3e9f), 0), 0x7FFFFFFFu); // saturates by sign
    CHECK_BITS(b3asm::ftoi_bits(std::bit_cast<std::uint32_t>(-3e9f), 0), 0x80000000u);
    CHECK_BITS(b3asm::ftoi_bits(0x7FFFFFFFu, 12), 0x7FFFFFFFu);                       // exponent 255 (a host NaN)
    CHECK_BITS(b3asm::ftoi_bits(0x00000001u, 15), 0u);                                // denormal encoding -> 0
    CHECK(std::bit_cast<float>(b3asm::itof_bits(16, 4)) == 1.0f);
    CHECK(std::bit_cast<float>(b3asm::itof_bits(static_cast<std::uint32_t>(-12), 0)) == -12.0f);
#if B3_PS2_FLOAT
    CHECK_BITS(b3asm::itof_bits(16777219u, 0), 0x4B800001u);                          // truncates, as cvt.s.w [M]
#endif
}

TEST(max_mini_total_order) {
    // [S] eeasm-ops §3, ps2-float §4.6: integer order on the encodings; -0 < +0
    const auto m = b3asm::vmax<15>(b3asm::Q{}, I4(0, int(0x80000000), int(0xBF800000), 0x3F800000), I4(int(0x80000000), 0, int(0xC0000000), 0x7FFFFFFF));
    EXPECT_I(m, 0, 0, 0xBF800000, 0x7FFFFFFF);
    const auto n = b3asm::vmini<15>(b3asm::Q{}, I4(0, int(0x80000000), int(0xBF800000), 0x3F800000), I4(int(0x80000000), 0, int(0xC0000000), 0x7FFFFFFF));
    EXPECT_I(n, 0x80000000, 0x80000000, 0xC0000000, 0x3F800000);
}

TEST(random_unit) {
    // [S] eeasm-ops §4: vrinit: R = 0x3F800000 | (src & 0x7FFFFF); vrnext: feedback = bit 4 ^ bit 22, shifted into bit 0
    CHECK_BITS(b3asm::r_init(0xFFFFFFFFu), 0x3FFFFFFFu);
    CHECK_BITS(b3asm::r_next(0x3F800010u), 0x3F800021u);   // bit 4 set: feedback 1
    CHECK_BITS(b3asm::r_next(0x3FC00000u), 0x3F800001u);   // bit 22 set: shifted out, feedback 1
    CHECK_BITS(b3asm::r_next(0x3F800001u), 0x3F800002u);
}

TEST(mmi_lanes) {
    EXPECT_I(b3asm::pabsw(I4(int(0x80000000), -1, 5, 0)), 0x7FFFFFFF, 1, 5, 0);       // [S] eeasm-ops §2: pabsw saturates
    EXPECT_I(b3asm::qfsrv(I4(5, 6, 7, 8), I4(1, 2, 3, 4), 4), 2, 3, 4, 5);             // [S] eeasm-ops §2: funnel shift by SA bytes
    const auto p = b3asm::pmulth(b3asm::state(), I4(0x0002FFFF, 0, 0, 0x00030004), I4(0x00030002, 0, 0, 0x00050006));
    EXPECT_I(p, 0xFFFFFFFE, 0, 0, 24);                                                  // signed 16x16: -1*2, h6 = 4*6
    CHECK(b3asm::state().lo == ((std::uint64_t{6} << 32) | 0xFFFFFFFEu));               // LO = {p0, p1 = 2*3}
    EXPECT_I(b3asm::ppacb(I4(0x00110022, 0x00330044, 0, 0), I4(0x00550066, 0x00770088, 0, 0)), 0x77885566, 0, 0x33441122, 0);
}

// ---- eeasm-ops spec rows (docs/specs/eeasm-ops.md §6 item 2): hand-computed from the spec's §2-§4 [M].
namespace {
// Patterned operands: every byte distinct, so a lane / halfword / byte mix-up shows.
const u_long128 kS = b3host::make_u128(0x00112233u, 0x44556677u, 0x8899AABBu, 0xCCDDEEFFu);   // rs
const u_long128 kT = b3host::make_u128(0x01234567u, 0x89ABCDEFu, 0xFEDCBA98u, 0x76543210u);   // rt
}  // namespace

TEST(mmi_copies_and_interleaves) {
    EXPECT_I(b3asm::pcpyld(kS, kT), 0x01234567, 0x89ABCDEF, 0x00112233, 0x44556677);
    EXPECT_I(b3asm::pcpyud(kS, kT), 0x8899AABB, 0xCCDDEEFF, 0xFEDCBA98, 0x76543210);
    EXPECT_I(b3asm::pextlw(kS, kT), 0x01234567, 0x00112233, 0x89ABCDEF, 0x44556677);
    EXPECT_I(b3asm::pextuw(kS, kT), 0xFEDCBA98, 0x8899AABB, 0x76543210, 0xCCDDEEFF);
    EXPECT_I(b3asm::pextlh(kS, kT), 0x22334567, 0x00110123, 0x6677CDEF, 0x445589AB);
    EXPECT_I(b3asm::pextuh(kS, kT), 0xAABBBA98, 0x8899FEDC, 0xEEFF3210, 0xCCDD7654);
    EXPECT_I(b3asm::pextlb(kS, kT), 0x22453367, 0x00011123, 0x66CD77EF, 0x448955AB);
    EXPECT_I(b3asm::ppach(kS, kT), 0xCDEF4567, 0x3210BA98, 0x66772233, 0xEEFFAABB);
    EXPECT_I(b3asm::ppacb(kS, kT), 0xABEF2367, 0x5410DC98, 0x55771133, 0xDDFF99BB);
    EXPECT_I(b3asm::ppacw(kS, kT), 0x01234567, 0xFEDCBA98, 0x00112233, 0x8899AABB);
    EXPECT_I(b3asm::pcpyh(kT), 0x45674567, 0x45674567, 0xBA98BA98, 0xBA98BA98);
    EXPECT_I(b3asm::pcpyh(kS), 0x22332233, 0x22332233, 0xAABBAABB, 0xAABBAABB);
}

TEST(mmi_arithmetic_and_logic) {
    EXPECT_I(b3asm::paddw(kS, kT), 0x0134679A, 0xCE013466, 0x87766553, 0x4332210F);   // modulo 2^32
    EXPECT_I(b3asm::psubw(kS, kT), 0xFEEDDCCC, 0xBAA99888, 0x89BCF023, 0x5689BCEF);
    EXPECT_I(b3asm::paddh(kS, kT), 0x0134679A, 0xCE003466, 0x87756553, 0x4331210F);   // no carry between halfwords
    EXPECT_I(b3asm::paddh(I4(int(0xFFFFFFFF), 0x7FFF8000, 0, 0), I4(1, 0x00018000, 0, 0)), 0xFFFF0000, 0x80000000, 0, 0);
    EXPECT_I(b3asm::pmaxw(kS, kT), 0x01234567, 0x44556677, 0xFEDCBA98, 0x76543210);   // signed compare
    EXPECT_I(b3asm::pminw(kS, kT), 0x00112233, 0x89ABCDEF, 0x8899AABB, 0xCCDDEEFF);
    EXPECT_I(b3asm::pmaxw(I4(int(0x80000000), 7, -1, 0x7FFFFFFF), I4(0x7FFFFFFF, 7, 0, int(0x80000000))), 0x7FFFFFFF, 7, 0, 0x7FFFFFFF);
    EXPECT_I(b3asm::pminw(I4(int(0x80000000), 7, -1, 0x7FFFFFFF), I4(0x7FFFFFFF, 7, 0, int(0x80000000))), 0x80000000, 7, 0xFFFFFFFF, 0x80000000);
    EXPECT_I(b3asm::pabsw(I4(int(0x80000000), int(0xFFFFFFFF), 0x7FFFFFFF, int(0x80000001))), 0x7FFFFFFF, 1, 0x7FFFFFFF, 0x7FFFFFFF);
    EXPECT_I(b3asm::pabsw(kT), 0x01234567, 0x76543211, 0x01234568, 0x76543210);
    EXPECT_I(b3asm::pand(kS, kT), 0x00010023, 0x00014467, 0x8898AA98, 0x44542210);
    EXPECT_I(b3asm::por(kS, kT), 0x01336777, 0xCDFFEFFF, 0xFEDDBABB, 0xFEDDFEFF);
    EXPECT_I(b3asm::pxor(kS, kT), 0x01326754, 0xCDFEAB98, 0x76451023, 0xBA89DCEF);
    EXPECT_I(b3asm::pnor(kS, kT), 0xFECC9888, 0x32001000, 0x01224544, 0x01220100);
}

TEST(mmi_shifts) {
    for (unsigned base : {0u, 32u, 64u}) {   // the amount is taken modulo 32
        EXPECT_I(b3asm::psraw(kT, base + 0), 0x01234567, 0x89ABCDEF, 0xFEDCBA98, 0x76543210);
        EXPECT_I(b3asm::psraw(kT, base + 1), 0x0091A2B3, 0xC4D5E6F7, 0xFF6E5D4C, 0x3B2A1908);
        EXPECT_I(b3asm::psraw(kT, base + 31), 0, 0xFFFFFFFF, 0xFFFFFFFF, 0);
        EXPECT_I(b3asm::psrlw(kT, base + 0), 0x01234567, 0x89ABCDEF, 0xFEDCBA98, 0x76543210);
        EXPECT_I(b3asm::psrlw(kT, base + 1), 0x0091A2B3, 0x44D5E6F7, 0x7F6E5D4C, 0x3B2A1908);
        EXPECT_I(b3asm::psrlw(kT, base + 31), 0, 1, 1, 0);
        EXPECT_I(b3asm::psllw(kT, base + 0), 0x01234567, 0x89ABCDEF, 0xFEDCBA98, 0x76543210);
        EXPECT_I(b3asm::psllw(kT, base + 1), 0x02468ACE, 0x13579BDE, 0xFDB97530, 0xECA86420);
        EXPECT_I(b3asm::psllw(kT, base + 31), 0x80000000, 0x80000000, 0, 0);
    }
    EXPECT_I(b3asm::psraw(kT, 4), 0x00123456, 0xF89ABCDE, 0xFFEDCBA9, 0x07654321);
    EXPECT_I(b3asm::psrlw(kT, 16), 0x0123, 0x89AB, 0xFEDC, 0x7654);
    EXPECT_I(b3asm::psllw(kT, 8), 0x23456700, 0xABCDEF00, 0xDCBA9800, 0x54321000);
}

TEST(mmi_qfsrv_every_sa) {
    // Oracle: the 32 bytes of rs:rt (rt the low half), read from byte sa on.
    std::uint8_t b[32];
    for (unsigned k = 0; k < 16; ++k) {
        b[k] = static_cast<std::uint8_t>(kT.w[k / 4] >> (8 * (k % 4)));
        b[16 + k] = static_cast<std::uint8_t>(kS.w[k / 4] >> (8 * (k % 4)));
    }
    for (unsigned sa = 0; sa < 32; ++sa) {   // 16..31: only sa & 15 counts
        const u_long128 r = b3asm::qfsrv(kS, kT, static_cast<std::uint8_t>(sa));
        for (unsigned w = 0; w < 4; ++w) {
            std::uint32_t e = 0;
            for (unsigned j = 0; j < 4; ++j) e |= static_cast<std::uint32_t>(b[(sa & 15) + 4 * w + j]) << (8 * j);
            CHECK_BITS(r.w[w], e);
        }
    }
    EXPECT_I(b3asm::qfsrv(kS, kT, 0), 0x01234567, 0x89ABCDEF, 0xFEDCBA98, 0x76543210);
    EXPECT_I(b3asm::qfsrv(kS, kT, 1), 0xEF012345, 0x9889ABCD, 0x10FEDCBA, 0x33765432);
    EXPECT_I(b3asm::qfsrv(kS, kT, 4), 0x89ABCDEF, 0xFEDCBA98, 0x76543210, 0x00112233);
    EXPECT_I(b3asm::qfsrv(kS, kT, 15), 0x11223376, 0x55667700, 0x99AABB44, 0xDDEEFF88);
    EXPECT_I(b3asm::qfsrv(kS, kT, 16), 0x01234567, 0x89ABCDEF, 0xFEDCBA98, 0x76543210);
}

TEST(ftoi_boundaries) {
    for (unsigned n : {0u, 4u, 12u, 15u}) {
        const std::uint32_t below = ((157u - n) << 23) | 0x7FFFFFu;   // the largest float with |x| * 2^n < 2^31
        const std::uint32_t at = (158u - n) << 23;                    // x * 2^n = 2^31
        CHECK_BITS(b3asm::ftoi_bits(below, n), 0x7FFFFF80u);
        CHECK_BITS(b3asm::ftoi_bits(below | 0x80000000u, n), 0x80000080u);
        CHECK_BITS(b3asm::ftoi_bits(at, n), 0x7FFFFFFFu);
        CHECK_BITS(b3asm::ftoi_bits(at | 0x80000000u, n), 0x80000000u);
        CHECK_BITS(b3asm::ftoi_bits(0x7F800000u, n), 0x7FFFFFFFu);    // exponent 255, any fraction
        CHECK_BITS(b3asm::ftoi_bits(0xFFFFFFFFu, n), 0x80000000u);
        CHECK_BITS(b3asm::ftoi_bits(0x00000000u, n), 0u);
        CHECK_BITS(b3asm::ftoi_bits(0x80000000u, n), 0u);             // -0
        CHECK_BITS(b3asm::ftoi_bits(0x807FFFFFu, n), 0u);             // exponent 0: 0
        CHECK_BITS(b3asm::ftoi_bits(0x3F800000u, n), 1u << n);        // 1.0
        CHECK_BITS(b3asm::ftoi_bits(0xBF800000u, n), 0u - (1u << n));
        CHECK_BITS(b3asm::ftoi_bits((127u - n) << 23, n), 1u);        // 2^-n
        CHECK_BITS(b3asm::ftoi_bits(((127u - n) << 23) | 0x80000000u, n), 0xFFFFFFFFu);
        CHECK_BITS(b3asm::ftoi_bits(((126u - n) << 23) | 0x7FFFFFu, n), 0u);   // just below 2^-n: truncated to 0
    }
    CHECK_BITS(b3asm::ftoi_bits(0x3FFFFFFFu, 0), 1u);                  // 1.99999988
    CHECK_BITS(b3asm::ftoi_bits(0x4B7FFFFFu, 0), 0x00FFFFFFu);         // 16777215
    CHECK_BITS(b3asm::ftoi_bits(0x4B7FFFFFu, 4), 0x0FFFFFF0u);
    CHECK_BITS(b3asm::ftoi_bits(0x3FC00000u, 12), 0x1800u);            // 1.5 * 4096
    CHECK_BITS(b3asm::ftoi_bits(0xC1200000u, 15), 0xFFFB0000u);        // -10 * 32768
    CHECK_BITS(b3asm::ftoi_bits(0x00800000u, 15), 0u);                 // smallest normal
}

namespace {
template <unsigned M> void check_masked_vu_ops() {
    const u_long128 d = b3host::make_u128(0xD0D0D0D0u, 0xD1D1D1D1u, 0xD2D2D2D2u, 0xD3D3D3D3u);
    const u_long128 s = b3host::make_u128(0xBF800000u, 0x40000000u, 0xC0400000u, 0x40800000u);   // -1, 2, -3, 4
    auto pick = [&](unsigned k, std::uint32_t v) { return (M & (1u << k)) ? v : d.w[k]; };
    // VMR32: x <- y, y <- z, z <- w, w <- x
    EXPECT_I(b3asm::vmr32<M>(d, s), pick(0, s.w[1]), pick(1, s.w[2]), pick(2, s.w[3]), pick(3, s.w[0]));
    EXPECT_I(b3asm::vmove<M>(d, s), pick(0, s.w[0]), pick(1, s.w[1]), pick(2, s.w[2]), pick(3, s.w[3]));
    EXPECT_I(b3asm::vabs<M>(d, s), pick(0, 0x3F800000u), pick(1, 0x40000000u), pick(2, 0x40400000u), pick(3, 0x40800000u));
    EXPECT_I((b3asm::vftoi<M, 4>(d, s)), pick(0, 0xFFFFFFF0u), pick(1, 32u), pick(2, 0xFFFFFFD0u), pick(3, 64u));
    const u_long128 t = b3host::make_u128(0x80000000u, 0x40400000u, 0xBF800000u, 0x7FFFFFFFu);  // -0, 3, -1, max encoding
    EXPECT_I(b3asm::vmax<M>(d, s, t), pick(0, 0x80000000u), pick(1, 0x40400000u), pick(2, 0xBF800000u), pick(3, 0x7FFFFFFFu));
    EXPECT_I(b3asm::vmini<M>(d, s, t), pick(0, 0xBF800000u), pick(1, 0x40000000u), pick(2, 0xC0400000u), pick(3, 0x40800000u));
}
template <unsigned... M> void check_every_mask(std::integer_sequence<unsigned, M...>) { (check_masked_vu_ops<M>(), ...); }
}  // namespace

TEST(vu_masked_helpers_every_mask) {
    check_every_mask(std::make_integer_sequence<unsigned, 16>{});
    // VMR32 of the patterned value, full mask: a rotation by one lane
    EXPECT_I(b3asm::vmr32<15>(b3asm::Q{}, kS), 0x44556677, 0x8899AABB, 0xCCDDEEFF, 0x00112233);
    // the signed zeros under VMAX / VMINI: -0 < +0
    EXPECT_I(b3asm::vmax<15>(b3asm::Q{}, I4(0, int(0x80000000), 0, 0), I4(int(0x80000000), 0, 0, 0)), 0, 0, 0, 0);
    EXPECT_I(b3asm::vmini<15>(b3asm::Q{}, I4(0, int(0x80000000), 0, 0), I4(int(0x80000000), 0, 0, 0)), 0x80000000, 0x80000000, 0, 0);
}

TEST(vu_outer_product) {
    // s x t with w kept: OPMULA then OPMSUB with the operands swapped (the decomp's cross-product idiom)
    const u_long128 s = V(1, 2, 3, 9), t = V(4, 5, 6, 9);
    const u_long128 acc = b3asm::vopmula(V(0, 0, 0, 7), s, t);
    EXPECT_V(acc, 12, 12, 5, 7);   // s.y*t.z, s.z*t.x, s.x*t.y; ACC.w kept
    EXPECT_V(b3asm::vopmsub(V(0, 0, 0, 11), acc, t, s), -3, 6, -3, 11);
}

TEST(vi_registers) {
    b3asm::State s;
    CHECK_BITS(b3asm::cfc2_vi(s, 0), 0u);
    b3asm::ctc2_vi(s, 0, 0x1234u);                 // ignored
    CHECK_BITS(b3asm::cfc2_vi(s, 0), 0u);
    b3asm::ctc2_vi(s, 3, 0xFFFF8765u);             // low 16 bits stored, read back zero-extended
    CHECK_BITS(b3asm::cfc2_vi(s, 3), 0x8765u);
    b3asm::ctc2_vi(s, 15, 0x00012345u);
    CHECK_BITS(b3asm::cfc2_vi(s, 15), 0x2345u);
    CHECK_BITS(b3asm::cfc2_vi(s, 3), 0x8765u);
    for (unsigned k = 1; k < 15; ++k) if (k != 3) CHECK_BITS(b3asm::cfc2_vi(s, k), 0u);
}

TEST(random_unit_sequence) {
    // 64 VRNEXT steps from a known seed (independent Python: s = ((s << 1) & 0x7FFFFF) | ((s >> 4 ^ s >> 22) & 1))
    static const std::uint32_t kSeq[64] = {
        0x3FA468ADu, 0x3FC8D15Au, 0x3F91A2B4u, 0x3FA34569u, 0x3FC68AD2u, 0x3F8D15A4u, 0x3F9A2B48u, 0x3FB45690u,
        0x3FE8AD21u, 0x3FD15A43u, 0x3FA2B487u, 0x3FC5690Eu, 0x3F8AD21Du, 0x3F95A43Bu, 0x3FAB4877u, 0x3FD690EFu,
        0x3FAD21DFu, 0x3FDA43BFu, 0x3FB4877Eu, 0x3FE90EFDu, 0x3FD21DFAu, 0x3FA43BF4u, 0x3FC877E9u, 0x3F90EFD3u,
        0x3FA1DFA7u, 0x3FC3BF4Eu, 0x3F877E9Du, 0x3F8EFD3Bu, 0x3F9DFA77u, 0x3FBBF4EFu, 0x3FF7E9DEu, 0x3FEFD3BCu,
        0x3FDFA778u, 0x3FBF4EF0u, 0x3FFE9DE1u, 0x3FFD3BC3u, 0x3FFA7787u, 0x3FF4EF0Fu, 0x3FE9DE1Fu, 0x3FD3BC3Eu,
        0x3FA7787Cu, 0x3FCEF0F9u, 0x3F9DE1F2u, 0x3FBBC3E5u, 0x3FF787CAu, 0x3FEF0F95u, 0x3FDE1F2Au, 0x3FBC3E55u,
        0x3FF87CABu, 0x3FF0F957u, 0x3FE1F2AEu, 0x3FC3E55Du, 0x3F87CABAu, 0x3F8F9575u, 0x3F9F2AEBu, 0x3FBE55D6u,
        0x3FFCABADu, 0x3FF9575Bu, 0x3FF2AEB6u, 0x3FE55D6Cu, 0x3FCABAD9u, 0x3F9575B2u, 0x3FAAEB65u, 0x3FD5D6CAu,
    };
    std::uint32_t r = b3asm::r_init(0xFF923456u);   // state = low 23 bits = 0x123456
    CHECK_BITS(r, 0x3F923456u);
    for (unsigned k = 0; k < 64; ++k) {
        r = b3asm::r_next(r);
        CHECK_BITS(r, kSeq[k]);
    }
    CHECK_BITS(b3asm::r_next(0x00123456u), 0x3FA468ADu);   // only the 23 state bits of the input count
    CHECK_BITS(b3asm::r_init(0u), 0x3F800000u);
    CHECK_BITS(b3asm::r_next(0x3F800000u), 0x3F800000u);   // the all-zero state stays put
    CHECK_BITS(b3vurand::r_next(b3vurand::r_init(0x00400010u)), 0x3F800020u);   // bits 4 and 22: feedback 0
}

TEST(gpr_binding) {
    // 32-bit values enter a register sign-extended (MWCC's lw) [C docs/abi-mwcc-ee.md §2]; u16 zero-extended
    const auto a = b3asm::gpr_in(-2);
    CHECK(b3asm::lo64(a) == 0xFFFFFFFFFFFFFFFEull);
    const auto b = b3asm::gpr_in(static_cast<unsigned short>(0xFFFF));
    CHECK(b3asm::lo64(b) == 0xFFFFull);
    const auto c = b3asm::gpr_in(-1.0f);
    CHECK(b3asm::lo64(c) == 0xFFFFFFFFBF800000ull);
    b3asm::Q d = I4(1, 2, 3, 4);
    b3asm::set32(d, 0x80000000u);                       // a 32-bit result: bits 127..64 kept
    EXPECT_I(d, 0x80000000, 0xFFFFFFFF, 3, 4);
}

TEST_MAIN("b3asm model and eeasm.py translations")
