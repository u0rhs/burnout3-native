// Exact cost of the libm overrides (include/b3libm/ps2_core.h "Exact cost", include/b3port/libm_overrides.h): every
// entry of the override table, called through its b3host::native_entry thunk with a NullBackend bound, must report
// (b3host::add_cost) exactly the instructions the retail function retires on the same input (tests/r5900_mini.h,
// delay slots included), stay within its ceiling, and return the same f0 bits. Inputs: port2's
// sweeps (tests/libm_sweeps.h) plus the cost extras
// (tests/libm_cost_entries.h).
#include "b3host/null_backend.h"
#include "b3port/libm_overrides.h"
#include "libm_cost_entries.h"
#include "libm_oracle.h"
#include "test.h"

#include <cstdio>
#include <string>

TEST(reported_cost_equals_retired_instructions) {
#ifndef B3_RETAIL_ELF
    std::printf("  (retail ELF not found: cost comparison skipped)\n");
#else
    static mini::R5900 m;
    if (!m.load_elf(B3_RETAIL_ELF)) { std::printf("  (retail ELF not readable: skipped)\n"); return; }
    b3host::NullBackend backend(1u << 20);
    const b3host::BackendScope scope(backend);
    std::size_t total = 0;
    for (const libm_cost::Entry& e : libm_cost::entries()) {
        const b3host::Override* o = nullptr;
        for (const auto& r : b3port::libm_overrides())
            if (std::string(r.name) == e.name) o = &r;
        CHECK(o != nullptr && o->address == e.address);
        if (!o) continue;
        std::size_t bad_cost = 0, bad_value = 0, over = 0;
        std::uint64_t max_retired = 0;
        const std::size_t n = libm_cost::for_each_input(e, [&](std::uint32_t x, std::uint32_t y, std::int64_t i) {
            b3host::CallArgs a;
            a.fpr[0] = x;
            a.fpr[1] = y;
            a.n_fpr = e.shape == libm_cost::Shape::F || e.shape == libm_cost::Shape::FI ? 1 : 2;
            a.gpr[0] = b3host::make_u128(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i >> 32), 0, 0);
            a.n_gpr = e.shape == libm_cost::Shape::FI || e.shape == libm_cost::Shape::FFI ? 1 : 0;
            b3host::CallResult r;
            const std::uint64_t before = backend.cost();
            o->entry(a, r);
            const std::uint64_t reported = backend.cost() - before;
            const std::uint32_t want = libm_cost::run_retail(m, e, x, y, i);
            if (m.retired > max_retired) max_retired = m.retired;
            if (r.f0 != want) ++bad_value;
            if (reported > o->cost) ++over;
            if (reported != m.retired && ++bad_cost <= 3) {
                char msg[200];
                std::snprintf(msg, sizeof msg, "%s(0x%08X, 0x%08X, %lld): reported %llu, retail retired %llu", e.name, x, y,
                              static_cast<long long>(i), static_cast<unsigned long long>(reported),
                              static_cast<unsigned long long>(m.retired));
                t::fail(__FILE__, __LINE__, msg);
            }
        });
        std::printf("    %-18s %8zu inputs: cost %s, values %s, max %4llu (ceiling %u)\n", e.name, n,
                    bad_cost ? "MISMATCH" : "exact", bad_value ? "MISMATCH" : "bit-exact",
                    static_cast<unsigned long long>(max_retired), o->cost);
        CHECK(bad_cost == 0);
        CHECK(bad_value == 0);
        CHECK(over == 0);
        total += n;
    }
    std::printf("    %zu inputs in total\n", total);
#endif
}

TEST_MAIN("test_libm_cost")
