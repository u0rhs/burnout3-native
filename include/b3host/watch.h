// b3host/watch.h -- observers of chosen game functions (pic1, docs/native-renderer.md §32): the game view's hook points
// on the native backend.
//
// - g_watch_hook runs before (result null) and after each call of a function whose bit is set in g_watch_bits (one bit
//   per 4-byte code address from kWatchLo), for calls that go through b3host::traced_entry (native_backend.h): every
//   dispatched call (call_guest stubs, guest_fn macros, function pointers).
// - A call that a generated unit links to directly (a shared-header C++ callee, decomp2host call_stubs) never passes
//   traced_entry. For those functions decomp2host puts `b3host::watch_self<ADDR>(this, args...)` at the top of the
//   definition (config/native_watch.txt) and lists them in the generated b3port/native_watch.h; traced_entry then
//   leaves them to the prologue (a compile-time test, nothing at run time), so each call is reported once. The
//   prologue reports the entry only (result null).
// Observers only: no guest memory writes, no cost. With no hook set every check is one load and branch.
#ifndef B3HOST_WATCH_H
#define B3HOST_WATCH_H

#include "b3host/call.h"

#include <cstdint>
#include <exception>
#include <type_traits>

#if __has_include("b3port/native_watch.h")
#include "b3port/native_watch.h"
#endif
#ifndef B3PORT_NATIVE_WATCH
#define B3PORT_NATIVE_WATCH(X)
#endif

namespace b3host {

// boot2: the boot oracle (tools/native/boot_oracle.h) and native_boot's hooks watch a few functions' calls: g_oracle_hook
// runs before (result null) and after each call of a function in g_oracle_targets (up to four at once -- movie2: the
// poll point at UpdatePort and the movie log at sceMpegIsEnd in one run). g_oracle_target is the first slot (the
// single-target form; assigning it still works).
using OracleHook = void (*)(gaddr function, const CallArgs& args, const CallResult* result);
inline OracleHook g_oracle_hook = nullptr;
inline gaddr g_oracle_targets[4] = {};
inline gaddr& g_oracle_target = g_oracle_targets[0];
inline bool add_oracle_target(gaddr a) {
    for (gaddr& t : g_oracle_targets)
        if (t == a || t == 0) { t = a; return true; }
    return false;
}
inline bool oracle_wants(gaddr a) {
    return g_oracle_hook && a != 0 && (a == g_oracle_targets[0] || a == g_oracle_targets[1] || a == g_oracle_targets[2] ||
                                       a == g_oracle_targets[3]);
}

using WatchHook = void (*)(gaddr function, const CallArgs& args, const CallResult* result);
inline constexpr gaddr kWatchLo = 0x00100000u, kWatchHi = 0x00500000u;   // the retail code range
inline WatchHook g_watch_hook = nullptr;
inline const std::uint64_t* g_watch_bits = nullptr;

// The functions whose definitions report themselves (decomp2host's prologue).
template <gaddr A>
constexpr bool self_reporting() {
#define B3HOST_WATCH_SELF(a) || (A == (a))
    return false B3PORT_NATIVE_WATCH(B3HOST_WATCH_SELF);
#undef B3HOST_WATCH_SELF
}

inline bool watch_bit(gaddr a) {
    const std::uint32_t i = (a - kWatchLo) >> 2;
    return ((g_watch_bits[i >> 6] >> (i & 63)) & 1u) != 0;
}

// traced_entry's test: watched, and not left to the definition's prologue.
template <gaddr A>
inline bool watched() {
    if constexpr (A < kWatchLo || A >= kWatchHi || self_reporting<A>()) return false;
    else return g_watch_hook != nullptr && watch_bit(A);
}

namespace detail {
// One argument as the EE would hold it: pointers as guest addresses (0 for a host pointer outside the windows: the
// observer never makes a bounce copy, so it never writes guest memory), integers sign-extended, floats in an FPR.
template <class T>
inline void watch_arg(CallArgs& a, const T& v) {
    using D = std::remove_cv_t<T>;
    if constexpr (is_float_like<D>) {
        if (a.n_fpr < 8) a.fpr[a.n_fpr++] = std::bit_cast<std::uint32_t>(v);
    } else {
        std::uint64_t image;
        if constexpr (std::is_pointer_v<D>) {
            gaddr g = 0;
            try_guest_address(static_cast<const void*>(v), g);
            image = sext32(g);
        } else if constexpr (std::is_null_pointer_v<D>) {
            image = 0;
        } else if constexpr (is_gp<D>::value || is_gfn<D>::value || std::is_enum_v<D> || std::is_integral_v<D>) {
            image = image_of(v);
        } else {
            static_assert(!sizeof(D), "watch_self: a struct by value is not observed (decomp2host refuses it)");
        }
        if (a.n_gpr < 8) a.gpr[a.n_gpr++] = gpr64(image);
    }
}
}  // namespace detail

// decomp2host's prologue: reports the entry of the function at A with its arguments (this first for a member).
template <gaddr A, class... T>
inline void watch_self(const T&... args) {
    if (g_watch_hook == nullptr || A < kWatchLo || A >= kWatchHi || !watch_bit(A)) [[likely]] return;
    CallArgs a;
    (detail::watch_arg(a, args), ...);
    g_watch_hook(A, a, nullptr);
}

// boot2: the self-reporting prologue as a scope (decomp2host emits `auto b3_watch_scope = b3host::watch_scope<A>(...)`):
// the entry now, the exit when the definition returns (result: an empty CallResult -- the definition's value is not
// observed) -- to the watch hook and to the oracle hook (native_boot's poll point at UpdatePort's return,
// which native Update__22CGtInputManagerPS2Pad2Fv reaches by a direct link).
// Nothing is reported while an exception unwinds.
template <gaddr A>
struct WatchScope {
    CallArgs a{};
    bool w = false, o = false;
    WatchScope() = default;
    WatchScope(const WatchScope&) = delete;
    WatchScope(WatchScope&& x) noexcept : a(x.a), w(x.w), o(x.o) { x.w = x.o = false; }   // returned by value
    ~WatchScope() noexcept(false) {
        if ((!w && !o) || std::uncaught_exceptions()) return;
        const CallResult r{};
        if (w && g_watch_hook) g_watch_hook(A, a, &r);
        if (o && g_oracle_hook) g_oracle_hook(A, a, &r);
    }
};
template <gaddr A, class... T>
inline WatchScope<A> watch_scope(const T&... args) {
    WatchScope<A> s;
    s.w = g_watch_hook != nullptr && A >= kWatchLo && A < kWatchHi && watch_bit(A);
    s.o = oracle_wants(A);
    if (!s.w && !s.o) [[likely]] return s;
    (detail::watch_arg(s.a, args), ...);
    if (s.w) g_watch_hook(A, s.a, nullptr);
    if (s.o) g_oracle_hook(A, s.a, nullptr);
    return s;
}

}  // namespace b3host

#endif
