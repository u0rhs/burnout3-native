// b3host/hook_route.h -- the prologue decomp2host puts at the top of each definition listed in config/mod_hooks.txt
// (mod kit Phase 2a, docs/modding/hooks.md "What a hook sees").
//
// A hook on the native table (src/native/hook_table.h) runs for calls that go through dispatch. A generated unit that
// calls a C++ function another selected unit defines (a member of a class from a shared decomp header) links to that
// definition directly, so the hook never sees the call. For a listed function the generator adds, as the first
// statements of its definition,
//
//     b3host::RouteScope b3_route_scope;
//     if (b3host::route_call<0xADDR>(b3_route_scope)) return b3host::call_guest<R>(0xADDR, <this, arguments>);
//
// route_call is false (the body runs, nothing else happens) unless a hook sits on ADDR. With a hook it tells the two
// ways in apart: dispatch reached the definition through the table row's traced_entry<ADDR, ...>, which pushed ADDR
// on the native chain (b3host/native_backend.h) -- the body runs and the scope marks that chain depth as taken; any
// other way in (a direct C++ call, a direct recursive call from the body itself) goes through dispatch, so the hook
// layers run and their bottom `next` comes back here through the row. With no hook on a listed function the cost is
// one load and branch; unlisted functions are not touched. A chain deeper than native_chain::kMax runs the body (the
// chain no longer records the entry).
#ifndef B3HOST_HOOK_ROUTE_H
#define B3HOST_HOOK_ROUTE_H

#include "b3host/native_backend.h"

#include <cstdint>

namespace b3host {

inline constexpr gaddr kRouteLo = 0x00100000u, kRouteHi = 0x00600000u;   // the retail code range (and the libraries')
// One bit per 4-byte code address from kRouteLo: a hook is above the row (the hook table sets it; null: no hooks)
inline const std::uint64_t* g_route_bits = nullptr;
// The native chain depth whose dispatched entry a routed definition has taken (0: none)
inline thread_local unsigned t_route_taken = 0;

inline bool route_hooked(gaddr a) {
    if (g_route_bits == nullptr || a < kRouteLo || a >= kRouteHi || (a & 3u) != 0) return false;
    const std::uint32_t i = (a - kRouteLo) >> 2;
    return ((g_route_bits[i >> 6] >> (i & 63)) & 1u) != 0;
}

struct RouteScope {
    unsigned saved = 0;
    bool active = false;
    RouteScope() = default;
    RouteScope(const RouteScope&) = delete;
    RouteScope& operator=(const RouteScope&) = delete;
    ~RouteScope() {
        if (active) t_route_taken = saved;
    }
};

// true: the caller returns call_guest's result (the call goes through dispatch); false: run the body
template <gaddr A>
inline bool route_call(RouteScope& s) {
    if (!route_hooked(A)) [[likely]] return false;
    const unsigned d = native_chain::t_depth;
    if (d == 0) return true;
    if (d > native_chain::kMax) return false;
    if (native_chain::t_chain[d - 1] == A && t_route_taken != d) {   // dispatch's entry, not taken yet
        s.saved = t_route_taken;
        s.active = true;
        t_route_taken = d;
        return false;
    }
    return true;
}

}  // namespace b3host

#endif
