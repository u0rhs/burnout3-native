// libm_cost_entries.h -- TEST-ONLY: the 23 libm override entries as (name, retail address, argument shape) plus a
// driver that walks port2's sweeps for one entry (tests/libm_sweeps.h) and
// calls the port and the retail code (tests/r5900_mini.h) on every input. Shared by tests/test_libm_cost.cpp (reported
// cost == retired instructions) and tests/libm_cost_dump.cpp (block-hit rows for tools/libm_cost/fit.py).
#ifndef B3_TESTS_LIBM_COST_ENTRIES_H
#define B3_TESTS_LIBM_COST_ENTRIES_H

#include "b3libm/ps2libm.h"
#include "libm_sweeps.h"
#include "r5900_mini.h"

#include <cstdint>
#include <string>
#include <vector>

namespace libm_cost {

enum class Shape { F, FF, FI, FFI };   // f12 | f12,f13 | f12,a0 | f12,f13,a0

struct Entry {
    const char* name;
    std::uint32_t address;
    Shape shape;
    bool wrapper;   // func_0011E3xx: 5 instructions of its own, then a tail jump into the __ieee754_* function
};

// Names and addresses as in include/b3port/libm_overrides.h.
inline const std::vector<Entry>& entries() {
    static const std::vector<Entry> e = {
        {"sinf", 0x0011E138u, Shape::F, false},           {"cosf", 0x0011DE98u, Shape::F, false},
        {"tanf", 0x0011E220u, Shape::F, false},           {"atanf", 0x0011DAF0u, Shape::F, false},
        {"floorf", 0x0011DF98u, Shape::F, false},         {"ceilf", 0x0011DD80u, Shape::F, false},
        {"fabsf", 0x0011DF70u, Shape::F, false},          {"copysignf", 0x0011DE58u, Shape::FF, false},
        {"scalbnf", 0x0011E070u, Shape::FI, false},       {"func_0011E328", 0x0011E328u, Shape::F, true},
        {"__ieee754_acosf", 0x00119C20u, Shape::F, false}, {"func_0011E340", 0x0011E340u, Shape::F, true},
        {"__ieee754_asinf", 0x0011A048u, Shape::F, false}, {"func_0011E358", 0x0011E358u, Shape::FF, true},
        {"__ieee754_atan2f", 0x0011A3E8u, Shape::FF, false}, {"func_0011E370", 0x0011E370u, Shape::F, true},
        {"__ieee754_logf", 0x0011A5C8u, Shape::F, false}, {"func_0011E388", 0x0011E388u, Shape::FF, true},
        {"__ieee754_powf", 0x0011A8A8u, Shape::FF, false}, {"__ieee754_sqrtf", 0x0011B438u, Shape::F, false},
        {"__kernel_sinf", 0x0011CF20u, Shape::FFI, false}, {"__kernel_cosf", 0x0011C568u, Shape::FF, false},
        {"__kernel_tanf", 0x0011D028u, Shape::FFI, false},
    };
    return e;
}

// The port function an entry runs (b3libm::ps2, raw bits in and out). A wrapper runs its target.
inline std::uint32_t run_port(const std::string& n, std::uint32_t x, std::uint32_t y, std::int64_t i) {
    using namespace b3libm::ps2;
    const F a{x}, b{y};
    const int k = static_cast<int>(i);
    if (n == "sinf") return sinf(a).b;
    if (n == "cosf") return cosf(a).b;
    if (n == "tanf") return tanf(a).b;
    if (n == "atanf") return atanf(a).b;
    if (n == "floorf") return floorf(a).b;
    if (n == "ceilf") return ceilf(a).b;
    if (n == "fabsf") return fabsf(a).b;
    if (n == "copysignf") return copysignf(a, b).b;
    if (n == "scalbnf") return scalbnf(a, k).b;
    if (n == "func_0011E328" || n == "__ieee754_acosf") return acosf(a).b;
    if (n == "func_0011E340" || n == "__ieee754_asinf") return asinf(a).b;
    if (n == "func_0011E358" || n == "__ieee754_atan2f") return atan2f(a, b).b;
    if (n == "func_0011E370" || n == "__ieee754_logf") return logf(a).b;
    if (n == "func_0011E388" || n == "__ieee754_powf") return powf(a, b).b;
    if (n == "__ieee754_sqrtf") return sqrtf(a).b;
    if (n == "__kernel_sinf") return kernel_sinf(a, b, k).b;
    if (n == "__kernel_cosf") return kernel_cosf(a, b).b;
    if (n == "__kernel_tanf") return kernel_tanf(a, b, k).b;
    throw std::logic_error("libm_cost: unknown entry " + n);
}

// Inputs added for the cost fit: retail paths the sweeps miss, so their block costs are pinned down too.
//  - sinf/cosf/tanf: x within 16 ulp of -pi/2 (__ieee754_rem_pio2f's second pio2 subtraction for x < 0, block 88;
//    the sweeps only reach the x > 0 one).
//  - powf: results at the bottom of the normal range, where the final scale goes through scalbnf (block 301).
inline std::vector<b3tool::Sweep> cost_extras(const std::string& name) {
    std::vector<b3tool::Sweep> s;
    if (name == "sinf" || name == "cosf" || name == "tanf")
        s.push_back({"near +-pi/2", b3tool::both(b3tool::range(0x3FC90FC0, 0x3FC90FEF, 1)), {}, {}});
    if (name == "func_0011E388" || name == "__ieee754_powf") {
        std::vector<std::uint32_t> xs, ys;
        for (std::uint32_t x : {0x40000000u, 0x3F000000u, 0x40800000u, 0x3E800000u, 0x41000000u, 0x3E000000u, 0x40400000u})
            for (int d = -3; d <= 3; ++d) xs.push_back(x + static_cast<std::uint32_t>(d));
        for (std::uint32_t y : b3tool::both({0x42FC0000u, 0x42FA0000u, 0x42A80000u, 0x427C0000u, 0x42540000u, 0x427B5555u}))
            for (int d = -64; d <= 64; ++d) ys.push_back(y + static_cast<std::uint32_t>(d));
        s.push_back({"results near 2^-126", b3tool::both(xs), ys, {}});
    }
    return s;
}

// Calls `fn(x, y, i)` for every input of the entry's sweeps and cost extras (y / i are 0 when the shape has none).
template <class Fn>
inline std::size_t for_each_input(const Entry& e, Fn&& fn) {
    std::size_t n = 0;
    std::vector<b3tool::Sweep> sweeps = b3tool::sweeps_for(e.name);
    for (b3tool::Sweep& x : cost_extras(e.name)) sweeps.push_back(std::move(x));
    for (const b3tool::Sweep& s : sweeps) {
        const std::vector<std::uint32_t> ys = s.ys.empty() ? std::vector<std::uint32_t>{0u} : s.ys;
        const std::vector<std::int64_t> is = s.ints.empty() ? std::vector<std::int64_t>{0} : s.ints;
        for (std::uint32_t x : s.xs)
            for (std::uint32_t y : ys)
                for (std::int64_t i : is) { fn(x, y, i); ++n; }
    }
    return n;
}

// The retail entry on one input, in the mini interpreter: returns f0, leaves m.retired.
inline std::uint32_t run_retail(mini::R5900& m, const Entry& e, std::uint32_t x, std::uint32_t y, std::int64_t i) {
    const auto gi = static_cast<std::uint64_t>(i);
    switch (e.shape) {
    case Shape::F: return m.call_f(e.address, {x});
    case Shape::FF: return m.call_f(e.address, {x, y});
    case Shape::FI: return m.call_f(e.address, {x}, {gi});
    default: return m.call_f(e.address, {x, y}, {gi});
    }
}

}  // namespace libm_cost

#endif
