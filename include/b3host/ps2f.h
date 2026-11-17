// b3host/ps2f.h -- ps2f: the scalar float of decomp game code compiled for the host (docs/decomp-on-host.md).
//
// tools/decomp2host/decomp2host.py rewrites the keyword `float` in a decomp unit (and the decomp class headers it
// includes) to `ps2f`. Every operator on ps2f is one COP1 instruction through b3fp (b3host/ps2float.h): in
// PS2-faithful mode (B3_PS2_FLOAT=1) the PS2 float model bit for bit (truncating add/sub/mul, clamped operands,
// overflow to +-Fmax, div.s per D4); in fast mode host float with the PS2's safe x/0. Plain host `float` arithmetic
// would round to nearest and keep denormals/Inf, so it cannot be bit-exact against the EE [C b3host/ps2float.h D1-D4].
//
// Layout: 4 bytes, trivially copyable, standard layout, so a `float` member keeps its offset and a ps2f value is the
// same bit pattern the guest stores. Conversions to/from host float are bit copies (no rounding); that is what lets
// decomp code call the GtMathPs2 shadow headers (whose scalar API is `float`) unchanged.
//
// Operators are templates over the other operand so `ps2f op float`, `float op ps2f` and `ps2f op int` pick ours
// (exact match) over the built-in `float op float` reached through the conversion; ps2f op ps2f is a plain overload.
// Known gap: an expression whose operands are all host `float` (e.g. two GtMath scalar results combined directly)
// still uses host arithmetic; decomp2host.py reports such units it cannot see through (see docs/decomp-on-host.md).
//
// Integer conversions follow MWCC's code [C asm]: (float)int = cvt.s.w; (float)unsigned = cvt.s.w for values below
// 2^31, else cvt.s.w((u >> 1) | (u & 1)) then add.s x, x (CalcFrequency__12CGtSoundWaveCFv @ 0x389588-0x3895B4);
// (int)f = cvt.w.s.
#ifndef B3HOST_PS2F_H
#define B3HOST_PS2F_H

#include "b3host/ps2float.h"

#include <bit>
#include <cstdint>
#include <type_traits>

namespace b3host {

struct ps2f {
    float v;

    ps2f() = default;                                       // trivial, like a float member
    constexpr ps2f(float f) : v(f) {}                       // bit copy (a literal, or a GtMath result)
    template <class I> requires(std::is_integral_v<I> && !std::is_same_v<I, bool>)
    ps2f(I i) : v(from_int(i)) {}                           // (float)i
    // A class type with a conversion to float (the GtMathPs2 lane types XAxisType..WAxisType: one lane of a VU
    // register, a bit copy): one user-defined conversion, so `ps2f x = v.GetLength();` works as in the decomp, where
    // the lane type converts to float directly.
    template <class T> requires(std::is_class_v<T> && !std::is_same_v<std::remove_cv_t<T>, ps2f> &&
                                requires(const T& t) { static_cast<float>(t); })
    ps2f(const T& t) : v(static_cast<float>(t)) {}
    constexpr operator float() const { return v; }          // bit copy, for float parameters of the shadow headers
    template <class I> requires std::is_integral_v<I>
    explicit operator I() const { return static_cast<I>(b3fp::cvt_w_s(v)); }   // (int)f = cvt.w.s

    template <class I> static float from_int(I i) {
        if constexpr (std::is_signed_v<I> || sizeof(I) < 4) return b3fp::cvt_s_w(static_cast<std::int32_t>(i));
        else {
            const std::uint32_t u = static_cast<std::uint32_t>(i);
            if (static_cast<std::int32_t>(u) >= 0) return b3fp::cvt_s_w(static_cast<std::int32_t>(u));
            const float h = b3fp::cvt_s_w(static_cast<std::int32_t>((u >> 1) | (u & 1u)));
            return b3fp::add(h, h);
        }
    }

    ps2f operator-() const { return b3fp::neg(v); }        // neg.s
    ps2f operator+() const { return *this; }
    ps2f& operator+=(ps2f o) { v = b3fp::add(v, o.v); return *this; }
    ps2f& operator-=(ps2f o) { v = b3fp::sub(v, o.v); return *this; }
    ps2f& operator*=(ps2f o) { v = b3fp::mul(v, o.v); return *this; }
    ps2f& operator/=(ps2f o) { v = b3fp::div(v, o.v); return *this; }
};

static_assert(sizeof(ps2f) == 4 && alignof(ps2f) == 4 && std::is_trivially_copyable_v<ps2f> &&
              std::is_standard_layout_v<ps2f> && std::is_trivially_default_constructible_v<ps2f>);

namespace detail {
template <class T> constexpr bool ps2f_operand = std::is_arithmetic_v<T> || std::is_same_v<T, ps2f>;
template <class A, class B> constexpr bool ps2f_pair =
    (std::is_same_v<A, ps2f> && ps2f_operand<B>) || (std::is_same_v<B, ps2f> && ps2f_operand<A>);
template <class T> inline ps2f as_ps2f(T x) {
    if constexpr (std::is_same_v<T, ps2f>) return x;
    else if constexpr (std::is_floating_point_v<T>) return ps2f(static_cast<float>(x));
    else return ps2f(x);
}
inline bool eq(float a, float b) {
#if B3_PS2_FLOAT
    return b3fp::ref::eq(b3fp::bits(a), b3fp::bits(b));
#else
    return a == b;
#endif
}
}  // namespace detail

// Operator templates match only ps2f and arithmetic operands exactly (no conversions), so an operand of a class type
// with its own operators (the GtMathPs2 lane types: `lane - f` is one VU instruction) never reaches these through
// ps2f's converting constructor.
#define B3HOST_PS2F_BINARY(op, fn)                                                                                   \
    template <class A, class B> requires detail::ps2f_pair<A, B>                                                     \
    inline ps2f operator op(A a, B b) { return fn(detail::as_ps2f(a).v, detail::as_ps2f(b).v); }
B3HOST_PS2F_BINARY(+, b3fp::add)
B3HOST_PS2F_BINARY(-, b3fp::sub)
B3HOST_PS2F_BINARY(*, b3fp::mul)
B3HOST_PS2F_BINARY(/, b3fp::div)
#undef B3HOST_PS2F_BINARY

// c.lt.s / c.le.s / c.eq.s; a > b is c.lt.s with the operands swapped, as MWCC emits it.
#define B3HOST_PS2F_COMPARE(op, expr)                                                                                \
    template <class A, class B> requires detail::ps2f_pair<A, B>                                                     \
    inline bool operator op(A a, B b) {                                                                              \
        const float x = detail::as_ps2f(a).v, y = detail::as_ps2f(b).v;                                              \
        return expr;                                                                                                 \
    }
B3HOST_PS2F_COMPARE(<, b3fp::lt(x, y))
B3HOST_PS2F_COMPARE(<=, b3fp::le(x, y))
B3HOST_PS2F_COMPARE(>, b3fp::lt(y, x))
B3HOST_PS2F_COMPARE(>=, b3fp::le(y, x))
B3HOST_PS2F_COMPARE(==, detail::eq(x, y))
B3HOST_PS2F_COMPARE(!=, !detail::eq(x, y))
#undef B3HOST_PS2F_COMPARE

// Every arithmetic form must reach b3fp (a host float operator here would silently round to nearest).
namespace detail {
inline ps2f probe_ps2f();
static_assert(std::is_same_v<decltype(probe_ps2f() * probe_ps2f()), ps2f> && std::is_same_v<decltype(1.0f * probe_ps2f()), ps2f> &&
              std::is_same_v<decltype(probe_ps2f() + 1.0f), ps2f> && std::is_same_v<decltype(probe_ps2f() - 1), ps2f> &&
              std::is_same_v<decltype(2.0 / probe_ps2f()), ps2f>, "ps2f arithmetic must use the ps2f operators");
}  // namespace detail

// gen1 (decomp2host T34): the address of a GtMathPs2 float element, `&v.marValues[1]`. The shadow headers keep their
// members host `float`; decomp locals are ps2f after T1 (`ps2f *py = &pos.marValues[1];`). Both are one 4-byte IEEE
// word in guest layout, so the address converts to either pointer type (constness kept).
template <class V> struct f32_address {
    V* p;
    operator float*() const requires(!std::is_const_v<V>) { return static_cast<float*>(p); }
    operator ps2f*() const requires(!std::is_const_v<V>) { return static_cast<ps2f*>(p); }
    operator const float*() const { return static_cast<const float*>(p); }
    operator const ps2f*() const { return static_cast<const ps2f*>(p); }
};
inline f32_address<void> f32_addr(float* p) { return {p}; }
inline f32_address<void> f32_addr(ps2f* p) { return {p}; }
inline f32_address<const void> f32_addr(const float* p) { return {p}; }
inline f32_address<const void> f32_addr(const ps2f* p) { return {p}; }

}  // namespace b3host

#endif
