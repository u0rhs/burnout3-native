// gen1 T39: integer division and remainder with the EE's results, for generated decomp code.
// The EE's DIV / DIVU never trap: a zero divisor leaves LO = rs < 0 ? 1 : -1 (DIVU: all ones) and HI = rs, and
// INT_MIN / -1 leaves LO = INT_MIN, HI = 0 [L: tests/r5900_mini.h div32, the port's EE model; the EE Core manual
// leaves the zero-divisor result undefined, so retail's observed behaviour is what the model encodes]. MWCC emits a
// bare `div $zero, a, b` with no check (retail func_00432280 0x432338: `divu $zero, $a0, $s2`), so retail code that
// divides by a zero count (TrafficValve::GetRandomVehicleID, func_00432280 / func_00432410) runs on; x86's idiv raises
// SIGFPE there (and on INT_MIN / -1), arm64's sdiv gives 0. decomp2host T39 rewrites `a / b` and `a % b` with a
// divisor that is not a non-zero integer literal into ee_div / ee_mod; for operands that are not both integral (floats,
// ps2f, GtMath types) they are the plain operator, so only integer division changes.
// 64-bit operands (MWCC: __divdi3 / __moddi3) follow the same rule [G: the runtime's zero-divisor result not checked].
#ifndef B3HOST_EEDIV_H
#define B3HOST_EEDIV_H

#include <limits>
#include <type_traits>

namespace b3host {
namespace detail {
template <class A, class B, class R>
inline constexpr bool ee_int_div = std::is_integral_v<R> && !std::is_class_v<std::remove_cvref_t<A>> &&
                                   !std::is_class_v<std::remove_cvref_t<B>>;
}  // namespace detail

template <class A, class B>
constexpr auto ee_div(A&& a, B&& b) -> decltype(a / b) {
    using R = decltype(a / b);
    if constexpr (detail::ee_int_div<A, B, R>) {
        const R x = static_cast<R>(a), y = static_cast<R>(b);   // the usual arithmetic conversions (R is their type)
        if (y == 0) {
            if constexpr (std::is_signed_v<R>) return x < 0 ? R(1) : R(-1);
            else return static_cast<R>(~R(0));
        }
        if constexpr (std::is_signed_v<R>)
            if (y == R(-1) && x == std::numeric_limits<R>::min()) return x;
        return x / y;
    } else {
        return a / b;
    }
}

template <class A, class B>
constexpr auto ee_mod(A&& a, B&& b) -> decltype(a % b) {
    using R = decltype(a % b);
    if constexpr (detail::ee_int_div<A, B, R>) {
        const R x = static_cast<R>(a), y = static_cast<R>(b);
        if (y == 0) return x;
        if constexpr (std::is_signed_v<R>)
            if (y == R(-1)) return R(0);   // INT_MIN % -1 (and every x % -1)
        return x % y;
    } else {
        return a % b;
    }
}

// `a /= b`, `a %= b` (a evaluated once)
template <class A, class B>
constexpr A& ee_div_assign(A& a, B&& b) {
    a = ee_div(a, static_cast<B&&>(b));
    return a;
}
template <class A, class B>
constexpr A& ee_mod_assign(A& a, B&& b) {
    a = ee_mod(a, static_cast<B&&>(b));
    return a;
}

}  // namespace b3host

#endif
