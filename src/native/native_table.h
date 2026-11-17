// native_table.h -- the native backend's one table: guest address -> native function (docs/standalone.md §8).
// Built from the X-lists of gen/decomp (B3PORT_GAME_OVERRIDES), b3port libm (B3PORT_LIBM_OVERRIDES) and the platform
// tier (src/native/platform.h), sorted, each entry wrapped by b3host::traced_entry; installed with
// b3host::set_native_overrides. The decomp's function list (build/native/functions.h) feeds the reports.
#ifndef B3NATIVE_NATIVE_TABLE_H
#define B3NATIVE_NATIVE_TABLE_H

#include "b3host/native_backend.h"

#include <cstddef>
#include <cstdint>

namespace b3native {

struct TableEntry {
    b3host::gaddr address;
    const char* name;
    const char* source;   // "game:<track>", "libm", "platform"
    char ret;             // the result register a caller reads: 'n' none (void or a struct
                          // through the hidden pointer), 'i' v0 (<= 4 bytes, sign-extended), 'l' v0 (8 bytes),
                          // 'q' v0 (128 bits), 'f' f0
};
// The entry for `address`, or null.
const TableEntry* entry_at(b3host::gaddr address);
// Installs the table (native overrides + function info) once; returns the entries in address order.
const TableEntry* install(std::size_t& count);
// The rows install() built (its entries' order): what the hook table (hook_table.h) copies and publishes. Null before
// install().
const b3host::NativeOverride* installed_rows(std::size_t& count);
// boot1: how many times each table entry was entered through dispatch since install (or the last reset), in the
// order of install()'s entries. Direct C++ links between generated units bypass dispatch and are not counted, so
// "entries with a nonzero count" is a lower bound on the distinct functions that ran natively.
std::uint64_t entered(std::size_t index);
void reset_entered();
// Function info for every retail function (the decomp's list).
const b3host::FunctionInfo* functions(std::size_t& count);

}  // namespace b3native

#endif
