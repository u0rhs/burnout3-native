// Virtual EE time for platform rows (src/native/platform_charge.h).
#include "platform_charge.h"

#include "b3host/call.h"
#include "b3host/native_backend.h"

#include <cstdio>

namespace b3native::platform {

std::uint32_t static_cost(b3host::gaddr fn) {
    const b3host::FunctionInfo* f = b3host::function_at(fn);   // the decomp's function list (native_table.cpp installs it)
    if (!f) {
        char b[96];
        std::snprintf(b, sizeof b, "platform charge: no retail function at 0x%08X", fn);
        b3host::fatal(b);
    }
    return f->size / 4u;   // as native_table.cpp's platform entries and the generated rows (bytes / 4)
}

void charge(b3host::gaddr fn) {
    b3host::add_cost(static_cost(fn));   // as b3host::detail::dispatch after a callee that returns at once
}

std::int32_t library_sema(std::int32_t max_count, std::int32_t init_count) {
    b3host::GuestStackFrame f(0x20);   // ee_sema_t: +4 max, +8 init, +0x10 attr, +0x14 option
    b3host::fill_guest(f.addr(), 0, 0x18);
    b3host::store<std::int32_t>(f.addr() + 4, max_count);
    b3host::store<std::int32_t>(f.addr() + 8, init_count);
    return b3host::call_guest<int>(0x0010BE20u, f.addr());
}
void library_sema_delete(std::int32_t id) { b3host::call_guest<int>(0x0010BE30u, id); }

}  // namespace b3native::platform
