// The libm override table as backend-neutral b3host::Override records (b3port/libm_overrides.h).
#include "b3port/libm_overrides.h"

#include <array>

namespace b3port {

namespace {
// Override::cost carries the X-list's ceiling here: the per-call cost is exact and dynamic (the wrappers report it
// through b3host::add_cost), so the record's number is the upper bound for a dispatcher's limits.
#define B3PORT_X(name, address, bytes, fnv, ceiling, fn) \
    b3host::Override{address, bytes, fnv, name, ceiling, &b3host::native_entry<&fn>},
constexpr std::array kOverrides{B3PORT_LIBM_OVERRIDES(B3PORT_X)};
#undef B3PORT_X
}  // namespace

std::span<const b3host::Override> libm_overrides() { return kOverrides; }

}  // namespace b3port
