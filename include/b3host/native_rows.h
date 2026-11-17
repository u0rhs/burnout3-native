// gen1: the native backend's table rows (src/native/native_table.cpp), shared by the table and the generated units.
// A native generation (DECOMP2HOST_NATIVE_ROWS=1) ends each unit with its game functions' rows,
// `extern const b3host::NativeRow b3rows_<unit>[] = { B3_NATIVE_ROW(...), ... };`, and lists the arrays in
// b3port/native_rows.h (B3PORT_NATIVE_ROWS(X): X(array, count) in the order of B3PORT_GAME_OVERRIDES). The traced_entry /
// native_entry instantiations of a function are then compiled with its unit, so a changed selection recompiles the
// changed units and a small table, not one translation unit holding every function's entry (boot2's iteration time).
#ifndef B3HOST_NATIVE_ROWS_H
#define B3HOST_NATIVE_ROWS_H

#include "b3host/call.h"
#include "b3host/native_backend.h"

#include <cstdint>
#include <type_traits>

namespace b3host {

struct NativeRow {
    gaddr address;
    void (*entry)(const CallArgs&, CallResult&);
    std::uint32_t cost;
    const char* name;
    const char* source;
    char ret;
};

template <class R>
constexpr char native_ret_kind() {
    using D = std::remove_cv_t<std::remove_reference_t<R>>;
    if constexpr (std::is_void_v<D> || detail::returns_indirect<D>) return 'n';
    else if constexpr (detail::is_float_like<D>) return 'f';
    // the host's struct by its elaborated name: a unit may declare its own `typedef ... u_long128` at namespace scope
    // (B3PhysicsManager_001B89D0), where the plain name is no type in this template
    else if constexpr (std::is_same_v<D, struct ::u_long128>) return 'q';
    else if constexpr (std::is_reference_v<R> || sizeof(D) <= 4) return 'i';
    else return 'l';
}
template <auto Fn>
constexpr char native_ret_of() { return native_ret_kind<typename detail::fn_traits<decltype(Fn)>::result>(); }

}  // namespace b3host

// One generated game function's row: what native_table.cpp's B3N_GAME made of a B3PORT_GAME_OVERRIDES entry.
#define B3_NATIVE_ROW(nm, addr, bytes, fn, track) \
    {addr, &b3host::traced_entry<addr, &b3host::native_entry<&fn>>, (bytes) / 4u, nm, "game:" #track, b3host::native_ret_of<&fn>()}

#endif
