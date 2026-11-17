// include/b3host/eeasm_ops.h -- the MMI lane operations and the VU0 macro helpers of b3asm, written from
// docs/specs/eeasm-ops.md and the Sony manuals (EE Core Instruction Set Manual, MMI chapter; VU
// Instruction Manual, macro instructions). Included by b3host/eeasm.h in the middle of namespace b3asm's definitions.
//
// Q is a 128-bit register: words w[0..3], w[0] least significant. Halfword k is bits 16k..16k+15 of the value, byte k
// bits 8k..8k+7 (shifts and masks below; no host byte order is assumed). In the MMI helpers "s" is rs and "t" is rt.
// Masked VU helpers: M is the port's lane mask (bit 0 = x = word 0 ... bit 3 = w = word 3); lanes outside M keep the
// destination's previous value, which B3VU_ENTER (b3host/vubind.h) provides as b3vu_base_.
// The plain helpers are [[gnu::always_inline]]: generated units use them about a thousand times, some in functions too
// large for the inliner's budget, and none may become an out-of-line call (eeasm-ops.md §5).
#ifndef B3HOST_EEASM_OPS_H
#define B3HOST_EEASM_OPS_H

#ifndef B3HOST_EEASM_H
#error "include b3host/eeasm.h, not b3host/eeasm_ops.h"
#endif

#include "b3host/vu_random.h"

#include <cstdint>

namespace b3asm {

namespace ops_detail {
[[gnu::always_inline]] inline std::uint32_t half(const Q& q, unsigned k) { return (q.w[k >> 1] >> ((k & 1u) * 16u)) & 0xFFFFu; }
[[gnu::always_inline]] inline std::uint32_t pack2h(std::uint32_t lo, std::uint32_t hi) { return (lo & 0xFFFFu) | (hi << 16); }
// PEXTLB's word: th / sh a halfword of rt / rs in bits 0..15 -> (rt byte, rs byte, rt byte, rs byte)
[[gnu::always_inline]] inline std::uint32_t pextlb_word(std::uint32_t th, std::uint32_t sh) {
    return (th & 0xFFu) | ((sh & 0xFFu) << 8) | ((th & 0xFF00u) << 8) | ((sh & 0xFF00u) << 16);
}
// PPACB's word: bytes 0 and 2 of lo, then bytes 0 and 2 of hi
[[gnu::always_inline]] inline std::uint32_t even_bytes(std::uint32_t lo, std::uint32_t hi) {
    return (lo & 0xFFu) | ((lo >> 8) & 0xFF00u) | ((hi & 0xFFu) << 16) | ((hi << 8) & 0xFF000000u);
}
[[gnu::always_inline]] inline std::int32_t sw(std::uint32_t v) { return static_cast<std::int32_t>(v); }
}  // namespace ops_detail

// ---------------------------------------------------------------------------------------------------------------
// VU0 macro helpers
// ---------------------------------------------------------------------------------------------------------------
// VMAX / VMINI: the PS2 order on the raw encodings (b3fp::ref::max / min), in both float build modes.
template <unsigned M> B3VU_INLINE Q vmax(const Q& d, const Q& s, const Q& t) {
    B3VU_ENTER(b3host::vubind::kMax, M, d, &s, &t);
    Q r = b3vu_base_;
    for (unsigned k = 0; k < 4; ++k) if (M & (1u << k)) r.w[k] = b3fp::ref::max(s.w[k], t.w[k]);
    B3VU_LEAVE(r);
    return r;
}

template <unsigned M> B3VU_INLINE Q vmini(const Q& d, const Q& s, const Q& t) {
    B3VU_ENTER(b3host::vubind::kMin, M, d, &s, &t);
    Q r = b3vu_base_;
    for (unsigned k = 0; k < 4; ++k) if (M & (1u << k)) r.w[k] = b3fp::ref::min(s.w[k], t.w[k]);
    B3VU_LEAVE(r);
    return r;
}

// VABS: the sign bit cleared.
template <unsigned M> B3VU_INLINE Q vabs(const Q& d, const Q& s) {
    B3VU_ENTER(b3host::vubind::kAbs, M, d, &s, nullptr);
    Q r = b3vu_base_;
    for (unsigned k = 0; k < 4; ++k) if (M & (1u << k)) r.w[k] = s.w[k] & 0x7FFFFFFFu;
    B3VU_LEAVE(r);
    return r;
}

template <unsigned M> B3VU_INLINE Q vmove(const Q& d, const Q& s) {
    B3VU_ENTER(b3host::vubind::kMove, M, d, &s, nullptr);
    Q r = b3vu_base_;
    for (unsigned k = 0; k < 4; ++k) if (M & (1u << k)) r.w[k] = s.w[k];
    B3VU_LEAVE(r);
    return r;
}

// VMR32: the source rotated down one lane (x <- y, y <- z, z <- w, w <- x), then masked.
template <unsigned M> B3VU_INLINE Q vmr32(const Q& d, const Q& s) {
    B3VU_ENTER(b3host::vubind::kMr32, M, d, &s, nullptr);
    Q r = b3vu_base_;
    for (unsigned k = 0; k < 4; ++k) if (M & (1u << k)) r.w[k] = s.w[(k + 1u) & 3u];
    B3VU_LEAVE(r);
    return r;
}

// VFTOIn: x * 2^n truncated toward zero to a signed 32-bit integer. Exponent field 0 gives 0; exponent field + n >= 158
// (|x| * 2^n >= 2^31, exponent 255 included) saturates by the sign bit. The scale is exact (a power of two).
[[gnu::always_inline]] inline std::uint32_t ftoi_bits(std::uint32_t bits, unsigned frac) {
    const unsigned e = (bits >> 23) & 0xFFu;
    const bool neg = (bits >> 31) != 0;
    if (e == 0) return 0;
    if (e + frac >= 158u) return neg ? 0x80000000u : 0x7FFFFFFFu;
    const std::uint32_t sig = (bits & 0x007FFFFFu) | 0x00800000u;   // 1.f as a 24-bit integer: value = sig * 2^(e-150)
    const int sh = static_cast<int>(e + frac) - 150;                 // <= 7 here
    std::uint32_t mag;
    if (sh >= 0) mag = sig << sh;                                    // < 2^31
    else if (sh > -24) mag = sig >> -sh;
    else mag = 0;
    return neg ? 0u - mag : mag;
}

template <unsigned M, unsigned N> B3VU_INLINE Q vftoi(const Q& d, const Q& s) {
    B3VU_ENTER(b3host::vubind::kFtoi, M, d, &s, nullptr);
    Q r = b3vu_base_;
    for (unsigned k = 0; k < 4; ++k) if (M & (1u << k)) r.w[k] = ftoi_bits(s.w[k], N);
    B3VU_LEAVE(r);
    return r;
}

// VOPMULA / VOPMSUB (the outer product: ACC.xyz = s.yzx * t.zxy, then d.xyz = ACC - s.yzx * t.zxy; w kept):
// b3vu's FMAC forms, which carry their own VU binding bracket.
B3VU_INLINE Q vopmula(const Q& acc, const Q& s, const Q& t) { return b3vu::opmula(acc, s, t); }

inline Q vopmsub(const Q& d, const Q& acc, const Q& s, const Q& t) { return b3vu::opmsub(d, acc, s, t); }

// The random unit (b3host/vu_random.h).
using b3vurand::r_init;
using b3vurand::r_next;

// CFC2 / CTC2 of VI0..VI15: 16-bit registers, read zero-extended; VI0 reads 0 and ignores writes.
[[gnu::always_inline]] inline std::uint32_t cfc2_vi(const State& s, unsigned k) {
    k &= 15u;
    return k == 0 ? 0u : static_cast<std::uint32_t>(s.vi[k]);
}

[[gnu::always_inline]] inline void ctc2_vi(State& s, unsigned k, std::uint32_t v) {
    k &= 15u;
    if (k != 0) s.vi[k] = static_cast<std::uint16_t>(v);
}

// ---------------------------------------------------------------------------------------------------------------
// MMI: copies and interleaves
// ---------------------------------------------------------------------------------------------------------------
// PCPYLD: low doubleword from rt, high doubleword from rs's low doubleword.
[[gnu::always_inline]] inline Q pcpyld(const Q& s, const Q& t) { return make_u128(t.w[0], t.w[1], s.w[0], s.w[1]); }

// PCPYUD: low doubleword from rs's high doubleword, high doubleword from rt's high doubleword.
[[gnu::always_inline]] inline Q pcpyud(const Q& s, const Q& t) { return make_u128(s.w[2], s.w[3], t.w[2], t.w[3]); }

[[gnu::always_inline]] inline Q pextlw(const Q& s, const Q& t) { return make_u128(t.w[0], s.w[0], t.w[1], s.w[1]); }

[[gnu::always_inline]] inline Q pextuw(const Q& s, const Q& t) { return make_u128(t.w[2], s.w[2], t.w[3], s.w[3]); }

// PEXTLH / PEXTUH: word i = (rt halfword i, rs halfword i), from halfwords 0..3 / 4..7.
[[gnu::always_inline]] inline Q pextlh(const Q& s, const Q& t) {
    using namespace ops_detail;
    return make_u128(pack2h(half(t, 0), half(s, 0)), pack2h(half(t, 1), half(s, 1)),
                     pack2h(half(t, 2), half(s, 2)), pack2h(half(t, 3), half(s, 3)));
}

[[gnu::always_inline]] inline Q pextuh(const Q& s, const Q& t) {
    using namespace ops_detail;
    return make_u128(pack2h(half(t, 4), half(s, 4)), pack2h(half(t, 5), half(s, 5)),
                     pack2h(half(t, 6), half(s, 6)), pack2h(half(t, 7), half(s, 7)));
}

// PEXTLB: halfword i = (rt byte i, rs byte i), i = 0..7. Word w takes rt and rs halfword w (bytes 2w, 2w + 1).
[[gnu::always_inline]] inline Q pextlb(const Q& s, const Q& t) {
    using ops_detail::pextlb_word;
    return make_u128(pextlb_word(t.w[0], s.w[0]), pextlb_word(t.w[0] >> 16, s.w[0] >> 16),
                     pextlb_word(t.w[1], s.w[1]), pextlb_word(t.w[1] >> 16, s.w[1] >> 16));
}

// PPACH: halfwords 0..3 = rt's even halfwords, 4..7 = rs's even halfwords.
[[gnu::always_inline]] inline Q ppach(const Q& s, const Q& t) {
    using namespace ops_detail;
    return make_u128(pack2h(t.w[0], t.w[1]), pack2h(t.w[2], t.w[3]), pack2h(s.w[0], s.w[1]), pack2h(s.w[2], s.w[3]));
}

// PPACB: bytes 0..7 = rt's even bytes, 8..15 = rs's even bytes.
[[gnu::always_inline]] inline Q ppacb(const Q& s, const Q& t) {
    using ops_detail::even_bytes;
    return make_u128(even_bytes(t.w[0], t.w[1]), even_bytes(t.w[2], t.w[3]), even_bytes(s.w[0], s.w[1]), even_bytes(s.w[2], s.w[3]));
}

// PPACW: words (rt.w0, rt.w2, rs.w0, rs.w2).
[[gnu::always_inline]] inline Q ppacw(const Q& s, const Q& t) { return make_u128(t.w[0], t.w[2], s.w[0], s.w[2]); }

// ---------------------------------------------------------------------------------------------------------------
// MMI: arithmetic and logic per lane
// ---------------------------------------------------------------------------------------------------------------
[[gnu::always_inline]] inline Q pmaxw(const Q& s, const Q& t) {
    using namespace ops_detail;
    Q r;
    for (unsigned k = 0; k < 4; ++k) r.w[k] = sw(s.w[k]) > sw(t.w[k]) ? s.w[k] : t.w[k];
    return r;
}

[[gnu::always_inline]] inline Q pminw(const Q& s, const Q& t) {
    using namespace ops_detail;
    Q r;
    for (unsigned k = 0; k < 4; ++k) r.w[k] = sw(s.w[k]) < sw(t.w[k]) ? s.w[k] : t.w[k];
    return r;
}

// PABSW: |x| per word; 0x80000000 saturates to 0x7FFFFFFF.
[[gnu::always_inline]] inline Q pabsw(const Q& t) {
    Q r;
    for (unsigned k = 0; k < 4; ++k) {
        const std::uint32_t v = t.w[k];
        r.w[k] = v == 0x80000000u ? 0x7FFFFFFFu : ((v >> 31) ? 0u - v : v);
    }
    return r;
}

// PCPYH: halfword 0 copied to halfwords 0..3, halfword 4 to halfwords 4..7.
[[gnu::always_inline]] inline Q pcpyh(const Q& t) {
    const std::uint32_t lo = (t.w[0] & 0xFFFFu) * 0x00010001u;
    const std::uint32_t hi = (t.w[2] & 0xFFFFu) * 0x00010001u;
    return make_u128(lo, lo, hi, hi);
}

[[gnu::always_inline]] inline Q paddw(const Q& s, const Q& t) { return make_u128(s.w[0] + t.w[0], s.w[1] + t.w[1], s.w[2] + t.w[2], s.w[3] + t.w[3]); }

[[gnu::always_inline]] inline Q psubw(const Q& s, const Q& t) { return make_u128(s.w[0] - t.w[0], s.w[1] - t.w[1], s.w[2] - t.w[2], s.w[3] - t.w[3]); }

// PADDH: 16-bit lanes, modulo 2^16 (no carry between halfwords).
[[gnu::always_inline]] inline Q paddh(const Q& s, const Q& t) {
    Q r;
    for (unsigned k = 0; k < 4; ++k) {
        const std::uint32_t lo = (s.w[k] + t.w[k]) & 0xFFFFu;
        const std::uint32_t hi = ((s.w[k] >> 16) + (t.w[k] >> 16)) << 16;
        r.w[k] = lo | hi;
    }
    return r;
}

[[gnu::always_inline]] inline Q pand(const Q& s, const Q& t) { return make_u128(s.w[0] & t.w[0], s.w[1] & t.w[1], s.w[2] & t.w[2], s.w[3] & t.w[3]); }

[[gnu::always_inline]] inline Q por(const Q& s, const Q& t) { return make_u128(s.w[0] | t.w[0], s.w[1] | t.w[1], s.w[2] | t.w[2], s.w[3] | t.w[3]); }

[[gnu::always_inline]] inline Q pxor(const Q& s, const Q& t) { return make_u128(s.w[0] ^ t.w[0], s.w[1] ^ t.w[1], s.w[2] ^ t.w[2], s.w[3] ^ t.w[3]); }

[[gnu::always_inline]] inline Q pnor(const Q& s, const Q& t) {
    return make_u128(~(s.w[0] | t.w[0]), ~(s.w[1] | t.w[1]), ~(s.w[2] | t.w[2]), ~(s.w[3] | t.w[3]));
}

// PSRAW / PSRLW / PSLLW: every word shifted by sa & 31.
[[gnu::always_inline]] inline Q psraw(const Q& t, unsigned sa) {
    const unsigned n = sa & 31u;
    Q r;
    for (unsigned k = 0; k < 4; ++k) r.w[k] = static_cast<std::uint32_t>(ops_detail::sw(t.w[k]) >> n);
    return r;
}

[[gnu::always_inline]] inline Q psrlw(const Q& t, unsigned sa) {
    const unsigned n = sa & 31u;
    return make_u128(t.w[0] >> n, t.w[1] >> n, t.w[2] >> n, t.w[3] >> n);
}

[[gnu::always_inline]] inline Q psllw(const Q& t, unsigned sa) {
    const unsigned n = sa & 31u;
    return make_u128(t.w[0] << n, t.w[1] << n, t.w[2] << n, t.w[3] << n);
}

// QFSRV: the 256-bit value rs:rt (rs the upper half) shifted right by 8 * (sa & 15) bits; the low 128 bits kept.
// The SA register holds a byte count (MTSAB / MTSAH in eeasm.py).
[[gnu::always_inline]] inline Q qfsrv(const Q& s, const Q& t, std::uint8_t sa) {
    const std::uint32_t v[8] = {t.w[0], t.w[1], t.w[2], t.w[3], s.w[0], s.w[1], s.w[2], s.w[3]};
    const unsigned b = sa & 15u;
    const unsigned ws = b >> 2;            // whole words
    const unsigned bs = (b & 3u) * 8u;     // remaining bits: 0, 8, 16, 24
    Q r;
    for (unsigned k = 0; k < 4; ++k) {
        const std::uint32_t lo = v[k + ws];
        r.w[k] = bs ? (lo >> bs) | (v[k + ws + 1] << (32u - bs)) : lo;   // k + ws + 1 <= 7
    }
    return r;
}

}  // namespace b3asm

#endif
