// platform_charge.h -- virtual EE time for a platform row that stands for more retail code than its own entry
// (sif-lane, 2026-11-10).
//
// Virtual EE time advances at native function entries: every call that goes through the dispatcher adds the callee's
// static cost (its retail size / 4: b3host/call.h dispatch, native_table.cpp) when it returns, and that is where a due
// VBlank, alarm or IOP completion is delivered (platform_iop.cpp time_check). A platform row that replaces a library
// function charges its own entry the same way, but the retail function's nested calls into other libraries (the
// kernel, libsif, ...) no longer happen. charge(fn) adds what one such nested call would have added -- fn's static
// cost, at that point of the row -- so the row keeps the virtual time, and the points where events can be delivered,
// of the code it replaces. Rows call it in the order the retail code made those calls.
#ifndef B3NATIVE_PLATFORM_CHARGE_H
#define B3NATIVE_PLATFORM_CHARGE_H

#include "b3host/host.h"

#include <cstdint>

namespace b3native::platform {

// The static cost of the retail function at `fn` (its size / 4 from the decomp's function list; fatal when the list has
// no function there).
std::uint32_t static_cost(b3host::gaddr fn);
// One nested call's worth of virtual time: as if the row had called fn and fn had returned at once.
void charge(b3host::gaddr fn);
// The same for several calls in order.
inline void charge(std::initializer_list<b3host::gaddr> fns) {
    for (b3host::gaddr f : fns) charge(f);
}

// A semaphore a library keeps for its whole life (its request lock and the like): made through the kernel's CreateSema
// (the call and its time as the library's), so the kernel hands out the same ids to everything made after it as on a
// PS2, where these libraries exist. The library's own use of it (its waits and signals) is then only charged.
std::int32_t library_sema(std::int32_t max_count, std::int32_t init_count);
void library_sema_delete(std::int32_t id);

}  // namespace b3native::platform

#endif
