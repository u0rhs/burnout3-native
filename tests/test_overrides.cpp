// The libm override table (b3port/libm_overrides.h), backend-neutral checks:
//  - every key (address, code_bytes, code_fnv1a64) recomputed from orig/SLUS_210.50;
//  - every entry called through its b3host::native_entry thunk (EE registers in, f0 out) and compared bit for bit
//    with the retail function at the same address, run in tests/r5900_mini.h. That catches a wrong address or a wrong
//    argument mapping (atan2f's y/x order, the int in a0) in the table itself.
#include "b3host/null_backend.h"
#include "b3port/libm_overrides.h"
#include "libm_oracle.h"
#include "test.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

const b3host::Override* find(const char* name) {
    for (const auto& o : b3port::libm_overrides())
        if (std::string(o.name) == name) return &o;
    return nullptr;
}

std::uint32_t run_entry(const b3host::Override& o, std::initializer_list<std::uint32_t> f, std::initializer_list<std::int64_t> g = {}) {
    b3host::CallArgs a;
    unsigned i = 0;
    for (std::uint32_t v : f) a.fpr[i++] = v;
    a.n_fpr = i;
    i = 0;
    for (std::int64_t v : g) a.gpr[i++] = b3host::make_u128(static_cast<std::uint32_t>(v), static_cast<std::uint32_t>(v >> 32), 0, 0);
    a.n_gpr = i;
    b3host::CallResult r;
    static b3host::NullBackend backend(1u << 20);   // the entries report their cost (b3host::add_cost)
    const b3host::BackendScope scope(backend);
    o.entry(a, r);
    return r.f0;
}

// A small spread of inputs per class: every 8th exponent, both signs, 5 mantissas, plus the specials.
std::vector<std::uint32_t> inputs() {
    std::vector<std::uint32_t> v;
    for (std::uint32_t s = 0; s < 2; ++s)
        for (std::uint32_t e = 0; e < 256; e += 8)
            for (std::uint32_t m : {0u, 1u, 0x2AAAAAu, 0x555555u, 0x7FFFFFu}) v.push_back((s << 31) | (e << 23) | m);
    for (std::uint32_t x : {0x3F800000u, 0xBF800000u, 0x3FC90FDBu, 0x40490FDBu, 0x7F7FFFFFu, 0x7F800000u, 0xFFFFFFFFu})
        v.push_back(x);
    return v;
}

}  // namespace

TEST(table_keys_match_the_retail_elf) {
#ifdef B3_RETAIL_ELF
    std::ifstream f(B3_RETAIL_ELF, std::ios::binary);
    const std::vector<unsigned char> elf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(elf.size() > 0x3E2780);
    for (const auto& o : b3port::libm_overrides()) {
        const std::size_t off = o.address - 0x100000u + 0x100u;   // PT_LOAD 0: vaddr 0x100000 at file offset 0x100 [C]
        CHECK(off + o.code_bytes <= elf.size());
        const std::uint64_t h = b3host::fnv1a64(elf.data() + off, o.code_bytes);
        if (h != o.code_fnv1a64) {
            char m[160];
            std::snprintf(m, sizeof m, "%s: fnv1a64 0x%016llx, table 0x%016llx", o.name, static_cast<unsigned long long>(h),
                          static_cast<unsigned long long>(o.code_fnv1a64));
            t::fail(__FILE__, __LINE__, m);
        }
        ++t::checks();
    }
#else
    std::printf("  (retail ELF not found: key check skipped)\n");
#endif
}

TEST(table_has_23_unique_addresses) {
    const auto all = b3port::libm_overrides();
    CHECK(all.size() == 23);
    for (std::size_t i = 0; i < all.size(); ++i)
        for (std::size_t j = i + 1; j < all.size(); ++j) CHECK(all[i].address != all[j].address);
}

TEST(entries_equal_retail_through_native_entry) {
    mini::R5900* m = oracle::cpu();
    if (!m) { std::printf("  (retail ELF not found: entry comparison skipped)\n"); return; }
    const auto xs = inputs();
    unsigned bad = 0;
    std::size_t n = 0;
    auto cmp = [&](const char* name, std::uint32_t got, std::uint32_t want, std::uint32_t x, std::uint32_t y) {
        ++n;
        if (got != want && ++bad <= 8) {
            char msg[200];
            std::snprintf(msg, sizeof msg, "%s(0x%08X, 0x%08X): entry 0x%08X, retail 0x%08X", name, x, y, got, want);
            t::fail(__FILE__, __LINE__, msg);
        }
    };
    for (const auto& o : b3port::libm_overrides()) {
        const std::string name = o.name;
        const bool two_floats = name == "copysignf" || name == "func_0011E358" || name == "__ieee754_atan2f" ||
                                name == "func_0011E388" || name == "__ieee754_powf" || name == "__kernel_cosf";
        const bool float_int = name == "scalbnf";
        const bool kernel_iy = name == "__kernel_sinf" || name == "__kernel_tanf";
        for (std::uint32_t x : xs) {
            if (two_floats) {
                for (std::uint32_t y : {0x00000000u, 0x3F800000u, 0xC0000000u, 0x3E99999Au, 0x7F7FFFFFu, 0x34000000u})
                    cmp(o.name, run_entry(o, {x, y}), m->call_f(o.address, {x, y}), x, y);
            } else if (float_int) {
                for (std::int64_t k : {-300, -130, -1, 0, 1, 25, 130, 300})
                    cmp(o.name, run_entry(o, {x}, {k}), m->call_f(o.address, {x}, {static_cast<std::uint64_t>(k)}), x,
                        static_cast<std::uint32_t>(k));
            } else if (kernel_iy) {
                if (((x >> 23) & 0xFF) > 126) continue;   // the kernels are only called with |x| < pi/4
                for (std::int64_t iy : {0, 1, -1})
                    cmp(o.name, run_entry(o, {x, 0x32000000u}, {iy}),
                        m->call_f(o.address, {x, 0x32000000u}, {static_cast<std::uint64_t>(iy)}), x,
                        static_cast<std::uint32_t>(iy));
            } else {
                cmp(o.name, run_entry(o, {x}), m->call_f(o.address, {x}), x, 0);
            }
        }
    }
    oracle::finish("23 override entries", bad, n);
    CHECK(bad == 0);
}

TEST(wrapper_names_resolve) {
    // a wrapper and its __ieee754_* target are separate entries now: the wrapper reports its own 5 instructions too
    CHECK(find("func_0011E328") && find("__ieee754_acosf") && find("func_0011E328")->entry != find("__ieee754_acosf")->entry);
    CHECK(find("sinf") && find("sinf")->address == 0x0011E138u);
}

TEST_MAIN("test_overrides")
