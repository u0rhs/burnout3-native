// libm_oracle.h -- TEST-ONLY helpers: run a retail libm function in tests/r5900_mini.h and compare it with a port,
// bit for bit, over input sweeps. The retail ELF comes from the decomp repo (B3_RETAIL_ELF, set by the Makefile when
// $(DECOMP)/orig/SLUS_210.50 exists); without it the oracle tests report "skipped" and only the self-contained checks
// run.
#ifndef B3_TESTS_LIBM_ORACLE_H
#define B3_TESTS_LIBM_ORACLE_H

#include "r5900_mini.h"
#include "test.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace oracle {

inline mini::R5900* cpu() {
    static mini::R5900 m;
    static int state = 0;   // 0 untried, 1 loaded, -1 missing
#ifdef B3_RETAIL_ELF
    if (state == 0) state = m.load_elf(B3_RETAIL_ELF) ? 1 : -1;
#else
    state = -1;
#endif
    return state == 1 ? &m : nullptr;
}

// Every exponent (0..255), both signs, `per_exp` mantissas per exponent (all-zero, all-one, and evenly spread), plus
// the edge encodings. Covers zeros, denormal encodings, exponent 255 and the clamping boundary.
inline std::vector<std::uint32_t> sweep_all_exponents(unsigned per_exp) {
    std::vector<std::uint32_t> v;
    for (std::uint32_t s = 0; s < 2; ++s)
        for (std::uint32_t e = 0; e < 256; ++e)
            for (unsigned k = 0; k < per_exp; ++k) {
                const std::uint32_t m = per_exp == 1 ? 0 : static_cast<std::uint32_t>((0x7FFFFFull * k) / (per_exp - 1));
                v.push_back((s << 31) | (e << 23) | m);
            }
    return v;
}
// Dense: every `step`-th encoding in [lo, hi] (raw bit patterns of one sign).
inline std::vector<std::uint32_t> sweep_range(std::uint32_t lo, std::uint32_t hi, std::uint32_t step) {
    std::vector<std::uint32_t> v;
    for (std::uint64_t b = lo; b <= hi; b += step) v.push_back(static_cast<std::uint32_t>(b));
    return v;
}

// One check per sweep (so check counts stay comparable with the other tests); the input count is printed.
inline void finish(const char* name, unsigned bad, std::size_t n) {
    ++t::checks();
    if (bad > 5) std::printf("  ... %s: %u mismatches in total\n", name, bad);
    std::printf("    %-24s %8zu inputs vs retail: %s\n", name, n, bad ? "MISMATCH" : "bit-exact");
}

// Compares port(x) with the retail function at `entry` for every x; reports the first few mismatches.
// Returns the number of inputs compared (0 when the ELF is missing).
inline std::size_t compare1(const char* name, std::uint32_t entry, const std::vector<std::uint32_t>& xs,
                            const std::function<std::uint32_t(std::uint32_t)>& port) {
    mini::R5900* m = cpu();
    if (!m) { std::printf("  (%s: retail ELF not found, oracle comparison skipped)\n", name); return 0; }
    unsigned bad = 0;
    for (std::uint32_t x : xs) {
        const std::uint32_t want = m->call_f(entry, {x});
        const std::uint32_t got = port(x);
        if (got != want && ++bad <= 5) {
            char msg[200];
            std::snprintf(msg, sizeof msg, "%s(0x%08X): port 0x%08X, retail 0x%08X", name, x, got, want);
            t::fail(__FILE__, __LINE__, msg);
        }
    }
    finish(name, bad, xs.size());
    return xs.size();
}
// Two-argument version (f12, f13).
inline std::size_t compare2(const char* name, std::uint32_t entry, const std::vector<std::uint32_t>& xs,
                            const std::vector<std::uint32_t>& ys,
                            const std::function<std::uint32_t(std::uint32_t, std::uint32_t)>& port) {
    mini::R5900* m = cpu();
    if (!m) { std::printf("  (%s: retail ELF not found, oracle comparison skipped)\n", name); return 0; }
    unsigned bad = 0;
    for (std::uint32_t x : xs)
        for (std::uint32_t y : ys) {
            const std::uint32_t want = m->call_f(entry, {x, y});
            const std::uint32_t got = port(x, y);
            if (got != want && ++bad <= 5) {
                char msg[200];
                std::snprintf(msg, sizeof msg, "%s(0x%08X, 0x%08X): port 0x%08X, retail 0x%08X", name, x, y, got, want);
                t::fail(__FILE__, __LINE__, msg);
            }
        }
    finish(name, bad, xs.size() * ys.size());
    return xs.size() * ys.size();
}

}  // namespace oracle

#endif
