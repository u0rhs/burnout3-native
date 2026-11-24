// b3host/ps2float_hw.h -- the PS2 float operations of b3fp::ref (ps2float.h) on the host's float unit (rt1,
// 2026-11-07: VU0 micro mode as C++ spent ~22% of a native race in ref's software operations).
//
// b3fp::hw::add / sub / mul / madd / msub return exactly ref's bits, for code that runs under b3host::Ps2FloatEnv
// (round toward zero, denormals flushed to zero in and out: x86 MXCSR RZ + FTZ + DAZ, AArch64 FPCR RMode RZ + FZ):
//   - operands: ref clamps exponent 255 to signed Fmax (done here) and exponent 0 to signed zero (DAZ / FZ does it);
//   - mul: ref truncates the exact 24 x 24-bit product, overflow -> signed Fmax, below FLT_MIN -> signed zero; RZ gives
//     the same: the truncation, RZ's overflow result is the largest finite value, and a result below FLT_MIN is
//     flushed (tininess before or after rounding agree under RZ);
//   - add / sub: ref truncates the double sum (chop(RN53(a + b))). With exponents at most 28 apart the double sum is
//     exact (24 + 28 bits and a carry fit in 53), so ref's result is RZ of the exact sum -- the host's add here. Further
//     apart the double's round-to-nearest can land on the other side of a binary32 boundary RZ stops at (an exact tie
//     rounded to even back to the larger operand; a power-of-two larger operand, whose lower neighbours are twice as
//     close; test_vufloat found both): those run ref itself in the default environment (rare: a tiny value added to a
//     large one). x - x is +0 in both;
//   - madd / msub: ref's product rounded first, then the sum (as the hardware, not fused), composed the same way.
// vu_div / vu_sqrt / vu_rsqrt (and other ref operations staged in double) depend on round to nearest: hw runs them as
// ref inside a b3host::DefaultFloatEnv scope, operands and result passing through volatiles so that no double
// operation moves across the mode switch.
// Checked against ref op by op (tests/test_vufloat.cpp: random and structured operands, x86 and AArch64) and per launch
// by B3_VU0_AOT=check.
//
// The operations are [[gnu::always_inline]]: the generated VU0 programs are too large for the inliner's own budget, and
// a call per operation (plus vufast's hw / ref test) cost ~12% of a race (perf, rt1). Inlining is safe: the units
// that compute under Ps2FloatEnv are compiled with -frounding-math (Makefile VU0_FP_ENV).
// The environment must be set before the code that uses hw is entered, in a function the compiler cannot inline across
// it: without FENV_ACCESS an inlined float operation may be scheduled before the MXCSR / FPCR write (clang on AArch64
// did, in test_vufloat's first version). Vu0Unit sets Ps2FloatEnv and then calls the program through a pointer.
//
// b3fp::vufast: the operations the VU0 C++ (src/native/vu0_aot_rt.h) calls: hw when B3_VU0_FLOAT is unset or "hw" (the
// caller then runs the launch under Ps2FloatEnv: Vu0Unit), ref when B3_VU0_FLOAT=ref (no environment change).
#ifndef B3HOST_PS2FLOAT_HW_H
#define B3HOST_PS2FLOAT_HW_H

#include "b3host/fenv.h"
#include "b3host/ps2float.h"

#include <cstdint>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>   // vu0lanes (vufast4)
#endif
#include <cstdlib>
#include <cstring>

namespace b3fp {
namespace hw {

[[gnu::always_inline]] inline std::uint32_t clamp255(std::uint32_t b) { return (b & kExp) == kExp ? (b & kSign) | kFmax : b; }

[[gnu::always_inline]] inline std::uint32_t mul(std::uint32_t a, std::uint32_t b) { return bits(flt(clamp255(a)) * flt(clamp255(b))); }
// ref's add / sub in the default environment (the double sum rounded to nearest, as ref assumes)
[[gnu::noinline]] inline std::uint32_t add_ref(std::uint32_t a, std::uint32_t b) {
    b3host::DefaultFloatEnv env;
    volatile std::uint32_t va = a, vb = b;
    volatile std::uint32_t r = ref::add(va, vb);
    return r;
}
[[gnu::noinline]] inline std::uint32_t sub_ref(std::uint32_t a, std::uint32_t b) {
    b3host::DefaultFloatEnv env;
    volatile std::uint32_t va = a, vb = b;
    volatile std::uint32_t r = ref::sub(va, vb);
    return r;
}
[[gnu::always_inline]] inline bool far_apart(std::uint32_t a, std::uint32_t b) {   // both non-zero exponents, more than 28 apart
    const int ea = static_cast<int>((a >> 23) & 0xFFu), eb = static_cast<int>((b >> 23) & 0xFFu);
    const int d = ea - eb;
    return ea != 0 && eb != 0 && (d > 28 || d < -28);
}
[[gnu::always_inline]] inline std::uint32_t add(std::uint32_t a, std::uint32_t b) {
    a = clamp255(a);
    b = clamp255(b);
    if (far_apart(a, b)) [[unlikely]] return add_ref(a, b);
    return bits(flt(a) + flt(b));
}
[[gnu::always_inline]] inline std::uint32_t sub(std::uint32_t a, std::uint32_t b) {
    a = clamp255(a);
    b = clamp255(b);
    if (far_apart(a, b)) [[unlikely]] return sub_ref(a, b);
    return bits(flt(a) - flt(b));
}
[[gnu::always_inline]] inline std::uint32_t madd(std::uint32_t acc, std::uint32_t a, std::uint32_t b) { return add(acc, mul(a, b)); }
[[gnu::always_inline]] inline std::uint32_t msub(std::uint32_t acc, std::uint32_t a, std::uint32_t b) { return sub(acc, mul(a, b)); }

#if defined(B3HOST_HAVE_PS2_FLOAT_ENV)
[[gnu::noinline]] inline std::uint32_t vu_div(std::uint32_t n, std::uint32_t d) {
    b3host::DefaultFloatEnv env;
    volatile std::uint32_t vn = n, vd = d;
    volatile std::uint32_t r = ref::vu_div(vn, vd);
    return r;
}
[[gnu::noinline]] inline std::uint32_t vu_sqrt(std::uint32_t x) {
    b3host::DefaultFloatEnv env;
    volatile std::uint32_t vx = x;
    volatile std::uint32_t r = ref::vu_sqrt(vx);
    return r;
}
[[gnu::noinline]] inline std::uint32_t vu_rsqrt(std::uint32_t n, std::uint32_t d) {
    b3host::DefaultFloatEnv env;
    volatile std::uint32_t vn = n, vd = d;
    volatile std::uint32_t r = ref::vu_rsqrt(vn, vd);
    return r;
}
#endif

}  // namespace hw

namespace vufast {
#if defined(B3HOST_HAVE_PS2_FLOAT_ENV)
// hw unless B3_VU0_FLOAT=ref (read once)
inline const bool kHw = [] {
    const char* e = std::getenv("B3_VU0_FLOAT");
    return !(e && std::strcmp(e, "ref") == 0);
}();
#else
inline const bool kHw = false;   // no host float environment to set: ref
#endif
using ref::chop;
using ref::clamp;
using ref::max;
using ref::min;
#if defined(B3HOST_HAVE_PS2_FLOAT_ENV)
[[gnu::always_inline]] inline std::uint32_t add(std::uint32_t a, std::uint32_t b) { return kHw ? hw::add(a, b) : ref::add(a, b); }
[[gnu::always_inline]] inline std::uint32_t sub(std::uint32_t a, std::uint32_t b) { return kHw ? hw::sub(a, b) : ref::sub(a, b); }
[[gnu::always_inline]] inline std::uint32_t mul(std::uint32_t a, std::uint32_t b) { return kHw ? hw::mul(a, b) : ref::mul(a, b); }
[[gnu::always_inline]] inline std::uint32_t madd(std::uint32_t c, std::uint32_t a, std::uint32_t b) { return kHw ? hw::madd(c, a, b) : ref::madd(c, a, b); }
[[gnu::always_inline]] inline std::uint32_t msub(std::uint32_t c, std::uint32_t a, std::uint32_t b) { return kHw ? hw::msub(c, a, b) : ref::msub(c, a, b); }
inline std::uint32_t vu_div(std::uint32_t n, std::uint32_t d) { return kHw ? hw::vu_div(n, d) : ref::vu_div(n, d); }
inline std::uint32_t vu_sqrt(std::uint32_t x) { return kHw ? hw::vu_sqrt(x) : ref::vu_sqrt(x); }
inline std::uint32_t vu_rsqrt(std::uint32_t n, std::uint32_t d) { return kHw ? hw::vu_rsqrt(n, d) : ref::vu_rsqrt(n, d); }
#else
using ref::add;
using ref::madd;
using ref::msub;
using ref::mul;
using ref::sub;
using ref::vu_div;
using ref::vu_rsqrt;
using ref::vu_sqrt;
#endif
}  // namespace vufast

// vu0lanes (1.4.1, issue #15): vufast's FMAC operations on the four lanes of a VU register at once, for the generated VU0
// programs (tools/native/vu0_aot.py emits them for an instruction with two or more dest lanes). out[l] is vufast's
// result for lane l, bit for bit, for every lane in `mask` (bit l: lane l, x = 0); the other lanes of out are unspecified.
// x86 (SSE2) under Ps2FloatEnv: a packed lane is the scalar operation on that lane -- the same rounding (RZ), the same
// flush (FTZ / DAZ), the same inputs -- so the four lanes run as one mulps / addps / subps after the four clamp255s (a
// compare and a blend); a dest lane whose exponents are more than 28 apart (hw's far_apart) is done again by hw::add /
// sub, which run ref there. madd / msub: the product rounded first, then the add, as hw::madd / msub. Elsewhere (no SSE2,
// or vufast running ref: B3_VU0_FLOAT=ref) lane by lane through vufast, as before.
namespace vufast4 {
#if defined(B3HOST_HAVE_PS2_FLOAT_ENV) && (defined(__SSE2__) || defined(_M_X64))
namespace detail {
[[gnu::always_inline]] inline __m128i load(const std::uint32_t* p) { return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p)); }
[[gnu::always_inline]] inline __m128i splat(std::uint32_t v) { return _mm_set1_epi32(static_cast<int>(v)); }
[[gnu::always_inline]] inline __m128i clamp255(__m128i b) {   // hw::clamp255 on each lane
    const __m128i e = _mm_set1_epi32(static_cast<int>(kExp));
    const __m128i is = _mm_cmpeq_epi32(_mm_and_si128(b, e), e);
    const __m128i cl = _mm_or_si128(_mm_and_si128(b, _mm_set1_epi32(static_cast<int>(kSign))), _mm_set1_epi32(static_cast<int>(kFmax)));
    return _mm_or_si128(_mm_and_si128(is, cl), _mm_andnot_si128(is, b));
}
[[gnu::always_inline]] inline int far_lanes(__m128i a, __m128i b) {   // hw::far_apart on each lane, as a 4-bit mask
    const __m128i ff = _mm_set1_epi32(0xFF), zero = _mm_setzero_si128();
    const __m128i ea = _mm_and_si128(_mm_srli_epi32(a, 23), ff), eb = _mm_and_si128(_mm_srli_epi32(b, 23), ff);
    const __m128i d = _mm_sub_epi32(ea, eb);
    const __m128i far = _mm_or_si128(_mm_cmpgt_epi32(d, _mm_set1_epi32(28)), _mm_cmplt_epi32(d, _mm_set1_epi32(-28)));
    const __m128i either_zero = _mm_or_si128(_mm_cmpeq_epi32(ea, zero), _mm_cmpeq_epi32(eb, zero));
    return _mm_movemask_ps(_mm_castsi128_ps(_mm_andnot_si128(either_zero, far)));
}
[[gnu::always_inline]] inline __m128i mul(__m128i a, __m128i b) {
    return _mm_castps_si128(_mm_mul_ps(_mm_castsi128_ps(clamp255(a)), _mm_castsi128_ps(clamp255(b))));
}
template <bool Sub>
[[gnu::always_inline]] inline void addsub(std::uint32_t* out, __m128i a, __m128i b, unsigned mask) {
    a = clamp255(a);
    b = clamp255(b);
    const __m128 fa = _mm_castsi128_ps(a), fb = _mm_castsi128_ps(b);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), _mm_castps_si128(Sub ? _mm_sub_ps(fa, fb) : _mm_add_ps(fa, fb)));
    if (const int f = far_lanes(a, b) & static_cast<int>(mask)) [[unlikely]] {   // (rare: a tiny value added to a large one)
        alignas(16) std::uint32_t la[4], lb[4];
        _mm_store_si128(reinterpret_cast<__m128i*>(la), a);
        _mm_store_si128(reinterpret_cast<__m128i*>(lb), b);
        for (unsigned l = 0; l < 4; ++l)
            if ((f >> l) & 1) out[l] = Sub ? hw::sub(la[l], lb[l]) : hw::add(la[l], lb[l]);
    }
}
}  // namespace detail
#define B3FP_VUFAST4_SIMD 1
#endif

// The lane-by-lane form (vufast, as the per-lane code was) is taken under B3_VU0_FLOAT=ref and on every platform
// without SSE2. a, b: a register's four lanes; s: one value for every lane (a broadcast field, Q or I); acc: ACC's lanes.
#if defined(B3FP_VUFAST4_SIMD)
[[gnu::always_inline]] inline bool simd() { return vufast::kHw; }
#else
[[gnu::always_inline]] inline constexpr bool simd() { return false; }
#endif

[[gnu::always_inline]] inline void add_vv(std::uint32_t* out, const std::uint32_t* a, const std::uint32_t* b, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { detail::addsub<false>(out, detail::load(a), detail::load(b), mask); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::add(a[l], b[l]);
}
[[gnu::always_inline]] inline void add_vs(std::uint32_t* out, const std::uint32_t* a, std::uint32_t s, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { detail::addsub<false>(out, detail::load(a), detail::splat(s), mask); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::add(a[l], s);
}
[[gnu::always_inline]] inline void sub_vv(std::uint32_t* out, const std::uint32_t* a, const std::uint32_t* b, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { detail::addsub<true>(out, detail::load(a), detail::load(b), mask); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::sub(a[l], b[l]);
}
[[gnu::always_inline]] inline void sub_vs(std::uint32_t* out, const std::uint32_t* a, std::uint32_t s, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { detail::addsub<true>(out, detail::load(a), detail::splat(s), mask); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::sub(a[l], s);
}
[[gnu::always_inline]] inline void mul_vv(std::uint32_t* out, const std::uint32_t* a, const std::uint32_t* b, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { _mm_storeu_si128(reinterpret_cast<__m128i*>(out), detail::mul(detail::load(a), detail::load(b))); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::mul(a[l], b[l]);
}
[[gnu::always_inline]] inline void mul_vs(std::uint32_t* out, const std::uint32_t* a, std::uint32_t s, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { _mm_storeu_si128(reinterpret_cast<__m128i*>(out), detail::mul(detail::load(a), detail::splat(s))); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::mul(a[l], s);
}
// madd / msub: ACC +/- a * b with the product rounded first (hw::madd = add(acc, mul(a, b)))
[[gnu::always_inline]] inline void madd_vv(std::uint32_t* out, const std::uint32_t* acc, const std::uint32_t* a, const std::uint32_t* b, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { detail::addsub<false>(out, detail::load(acc), detail::mul(detail::load(a), detail::load(b)), mask); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::madd(acc[l], a[l], b[l]);
}
[[gnu::always_inline]] inline void madd_vs(std::uint32_t* out, const std::uint32_t* acc, const std::uint32_t* a, std::uint32_t s, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { detail::addsub<false>(out, detail::load(acc), detail::mul(detail::load(a), detail::splat(s)), mask); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::madd(acc[l], a[l], s);
}
[[gnu::always_inline]] inline void msub_vv(std::uint32_t* out, const std::uint32_t* acc, const std::uint32_t* a, const std::uint32_t* b, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { detail::addsub<true>(out, detail::load(acc), detail::mul(detail::load(a), detail::load(b)), mask); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::msub(acc[l], a[l], b[l]);
}
[[gnu::always_inline]] inline void msub_vs(std::uint32_t* out, const std::uint32_t* acc, const std::uint32_t* a, std::uint32_t s, unsigned mask) {
#if defined(B3FP_VUFAST4_SIMD)
    if (simd()) { detail::addsub<true>(out, detail::load(acc), detail::mul(detail::load(a), detail::splat(s)), mask); return; }
#endif
    for (unsigned l = 0; l < 4; ++l) if ((mask >> l) & 1) out[l] = vufast::msub(acc[l], a[l], s);
}
}  // namespace vufast4
}  // namespace b3fp

#endif
