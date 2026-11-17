// tests/libm_sweeps.h -- the input sweeps for the libm overrides, the same ones port2 checked the port against
// the retail code with (tests/test_libm.cpp, tests/test_libm_more.cpp; line numbers cited); used by
// tests/test_libm_cost.cpp.
// A sweep is xs (f12) x ys (f13, optional) x ints (a0, optional), every combination.
#ifndef B3_TESTS_LIBM_SWEEPS_H
#define B3_TESTS_LIBM_SWEEPS_H

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace b3tool {

using V = std::vector<std::uint32_t>;

struct Sweep {
    std::string label;
    V xs, ys;
    std::vector<std::int64_t> ints;
    std::size_t size() const { return xs.size() * (ys.empty() ? 1 : ys.size()) * (ints.empty() ? 1 : ints.size()); }
};

// tests/libm_oracle.h sweep_all_exponents / sweep_range.
inline V all_exponents(unsigned per_exp) {
    V v;
    for (std::uint32_t s = 0; s < 2; ++s)
        for (std::uint32_t e = 0; e < 256; ++e)
            for (unsigned k = 0; k < per_exp; ++k) {
                const std::uint32_t m = per_exp == 1 ? 0 : static_cast<std::uint32_t>((0x7FFFFFull * k) / (per_exp - 1));
                v.push_back((s << 31) | (e << 23) | m);
            }
    return v;
}
inline V range(std::uint32_t lo, std::uint32_t hi, std::uint32_t step) {
    V v;
    for (std::uint64_t b = lo; b <= hi; b += step) v.push_back(static_cast<std::uint32_t>(b));
    return v;
}
inline V negated(V v) { for (auto& x : v) x ^= 0x80000000u; return v; }
inline V cat(V a, const V& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
inline V both(const V& v) { return cat(v, negated(v)); }
inline std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }
// tests/test_libm_more.cpp:33-35
inline const V kSpecials = {0x00000000, 0x80000000, 0x00000001, 0x807FFFFF, 0x00800000, 0x80800000, 0x3F800000,
                            0xBF800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0xFFFFFFFF};

inline std::vector<Sweep> sweeps_for(const std::string& name) {
    const V all = all_exponents(65);
    std::vector<Sweep> s;
    if (name == "sinf" || name == "cosf") {   // tests/test_libm.cpp:33-54
        V dense = range(0, 0x43490F80, 4099);
        dense = cat(dense, negated(dense));
        V near;
        for (int k = 1; k < 120; ++k) {
            const std::uint32_t c = bits(static_cast<float>(k * 1.5707963267948966));
            for (int d = -300; d <= 300; ++d) near.push_back(c + static_cast<std::uint32_t>(d));
        }
        s = {{"all exponents", all, {}, {}}, {"dense |x| <= 0x43490F80", dense, {}, {}}, {"near k*pi/2", near, {}, {}}};
    } else if (name == "tanf") {   // tests/test_libm_more.cpp:93-99
        s = {{"all exponents", all, {}, {}},
             {"dense [0.5, 201]", both(range(0x3F000000, 0x43490F80, 1999)), {}, {}},
             {"near pi/4, 0.6744", both(cat(range(0x3F48F000, 0x3F4A0000, 7), range(0x3F2C0000, 0x3F2D0000, 7))), {}, {}},
             {"[201, 1e6]", both(range(0x43490F81, 0x49742400, 4999)), {}, {}},
             {"[1e6, exp 255]", both(range(0x49742400, 0x7FFFFFFF, 30011)), {}, {}}};
    } else if (name == "atanf") {   // tests/test_libm_more.cpp:37-51
        V edges;
        for (std::uint32_t b : {0x31000000u, 0x3EE00000u, 0x3F300000u, 0x3F980000u, 0x401C0000u, 0x50800000u})
            for (std::uint32_t d = 0; d < 64; ++d) edges.push_back(b - 32 + d);
        s = {{"all exponents", all, {}, {}},
             {"dense +", range(0x3EC00000, 0x40300000, 41), {}, {}},
             {"dense -", negated(range(0x3EC00000, 0x40300000, 83)), {}, {}},
             {"breakpoints", both(edges), {}, {}},
             {"tiny/huge", both(cat(range(0x30000000, 0x31800000, 1201), range(0x50000000, 0x51000000, 1201))), {}, {}}};
    } else if (name == "floorf" || name == "ceilf") {   // tests/test_libm.cpp:18-30
        s = {{"all exponents", all, {}, {}},
             {"dense -", negated(range(0x3F000000, 0x40800000, 211)), {}, {}},
             {"dense +", range(0x3F000000, 0x40800000, 211), {}, {}}};
    } else if (name == "fabsf") {   // tests/test_libm.cpp:19
        s = {{"all exponents", all, {}, {}}};
    } else if (name == "__ieee754_sqrtf") {   // tests/test_libm.cpp:22-24
        s = {{"all exponents", all, {}, {}}, {"dense [0.25, 4)", range(0x3E800000, 0x407FFFFF, 97), {}, {}}};
    } else if (name == "copysignf") {   // not swept by port2 on its own (an internal helper): every exponent pair
        const V g = cat(all_exponents(2), kSpecials);
        s = {{"exponent pairs", g, g, {}}};
    } else if (name == "scalbnf") {   // port2 checked it inside powf; here directly: every exponent x the n edges
        s = {{"all exponents x n", all, {}, {-400, -300, -254, -200, -150, -130, -127, -126, -125, -24, -1, 0, 1, 24, 125, 126,
                                               127, 128, 130, 200, 254, 300, 400}}};
    } else if (name == "func_0011E328" || name == "__ieee754_acosf" || name == "func_0011E340" || name == "__ieee754_asinf") {
        V d = range(0, 0x3F800010, 1531);   // tests/test_libm.cpp:56-66
        d = cat(d, negated(d));
        s = {{"all exponents", all, {}, {}}, {"dense [-1, 1] and just past", d, {}, {}}};
    } else if (name == "func_0011E358" || name == "__ieee754_atan2f") {   // tests/test_libm_more.cpp:53-64
        const V g = cat(all_exponents(2), kSpecials);
        const V v = both(range(0x3D000000, 0x40800000, 0x2FFF1));
        s = {{"exponent pairs", g, g, {}}, {"[-4,4]^2", v, v, {}},
             {"specials x spread", kSpecials, both(range(0x3E000000, 0x41000000, 0x400001)), {}}};
    } else if (name == "func_0011E370" || name == "__ieee754_logf") {   // tests/test_libm_more.cpp:144-164
        V near1, ij;
        for (std::uint32_t e = 1; e < 256; ++e) {
            for (std::uint32_t m = 0x7FFFF0; m <= 0x7FFFFF; ++m) near1.push_back((e << 23) | m);
            for (std::uint32_t m = 0; m <= 0x10; ++m) near1.push_back((e << 23) | m);
        }
        for (std::uint32_t e : {0x3Fu, 0x40u, 0x7Eu, 0x10u, 0xF0u})
            for (std::uint32_t b : {0x30A3D0u, 0x35C288u})
                for (std::uint32_t d = 0; d < 64; ++d) ij.push_back((e << 23) | (b - 32 + d));
        s = {{"all exponents + specials", cat(all, kSpecials), {}, {}},
             {"dense [0.5, 2]", range(0x3F000000, 0x40000000, 37), {}, {}},
             {"near 1", range(0x3F7E0000, 0x3F820000, 3), {}, {}},
             {"sqrt2 split", cat(range(0x3FB4F000, 0x3FB51000, 1), range(0x3F34F000, 0x3F351000, 1)), {}, {}},
             {"|f| < 2^-20", near1, {}, {}},
             {"i|j split", ij, {}, {}}};
    } else if (name == "func_0011E388" || name == "__ieee754_powf") {   // tests/test_libm_more.cpp:166-206
        V xs = cat(both(cat(range(0x3C000000, 0x41000000, 0xCCC1), range(0x3F7FFFF0, 0x3F800010, 1))), kSpecials);
        V ys;
        for (int k = -16; k <= 16; ++k) ys.push_back(bits(0.5f * static_cast<float>(k)));
        ys = cat(ys, both(range(0x3C000000, 0x41000000, 0x1C0001)));
        ys = cat(ys, both({0x4D000000, 0x4D000001, 0x4E000000, 0x4F800000, 0x4B800000, 0x4B7FFFFF, 0x4B7FFFFE, 0x2EDBE6FF,
                           0x00000001, 0x00800000, 0x7F7FFFFF, 0x7F800000, 0x7FFFFFFF, 0x3F7FFFFF}));
        const V ysmall = both({0x3F800000, 0x40000000, 0x3F000000, 0x40400000, 0x40490FDA, 0x3E800000, 0x41200000,
                               0x3DCCCCCD, 0x42C80000, 0x4D800000});
        V yb = range(0x42F00000, 0x43080000, 0x401);
        for (float v : {126.0f, 129.0f, 125.75f, 125.5f, 63.0f, 64.5f, 42.0f, 43.0f, 31.5f, 32.25f, 252.0f, 258.0f})
            for (int d = -2; d <= 2; ++d) yb.push_back(bits(v) + static_cast<std::uint32_t>(d));
        V yint;
        for (int k = 1; k <= 300; ++k) { yint.push_back(bits(static_cast<float>(k))); yint.push_back(bits(static_cast<float>(k)) + 1); }
        for (std::uint32_t b : {0x4B000000u, 0x4B000001u, 0x4B7FFFFFu, 0x4B800000u, 0x4AFFFFFFu, 0x4AFFFFFEu}) yint.push_back(b);
        s = {{"grid", xs, ys, {}},
             {"x exponents", all_exponents(17), ysmall, {}},
             {"over/underflow", both(range(0x3E800000, 0x40800000, 0x7FFF1)), both(range(0x42F80000, 0x43060000, 0x1801)), {}},
             {"exact boundaries", both({0x40000000, 0x3F000000, 0x40800000, 0x3E800000, 0x41000000, 0x3E000000, 0x40400000,
                                        0x3FC00000, 0x3F400000, 0x40490FDA, 0x3F3504F3, 0x3FB504F3}), both(yb), {}},
             {"x<0, integer y", negated(range(0x3E000000, 0x40400000, 0x1FFFF7)), both(yint), {}},
             {"huge y near 1", both(range(0x3F7FFFE0, 0x3F800020, 1)),
              both({0x4D000001, 0x4D800000, 0x4E000000, 0x4D7FFFFF, 0x4F000000, 0x5F000000}), {}}};
    } else if (name == "__kernel_tanf") {   // tests/test_libm_more.cpp:66-89
        V xs = cat(range(0x30000000, 0x3F490FDA, 40009), range(0x3F2C0000, 0x3F2D0000, 61));
        xs = cat(both(xs), {0x00000000, 0x80000000, 0x00000005, 0x80000005, 0x317FFFFF, 0x31800000});
        s = {{"|x| <= pi/4 x y x iy", xs,
              {0x00000000, 0x80000000, 0x32000000, 0xB2000000, 0x33A22168, 0xB3A22168, 0x2F123456, 0xAF654321}, {1, -1}}};
    } else if (name == "__kernel_sinf" || name == "__kernel_cosf") {
        // port2 reached these through sinf/cosf; here directly, over the same domain as __kernel_tanf above
        V xs = cat(range(0x30000000, 0x3F490FDA, 40009), range(0x3F2C0000, 0x3F2D0000, 61));
        xs = cat(both(xs), {0x00000000, 0x80000000, 0x00000005, 0x80000005, 0x317FFFFF, 0x31800000, 0x39000000, 0x3E800000});
        const V ys = {0x00000000, 0x80000000, 0x32000000, 0xB2000000, 0x33A22168, 0xB3A22168, 0x2F123456, 0xAF654321};
        if (name == "__kernel_sinf") s = {{"|x| <= pi/4 x y x iy", xs, ys, {0, 1}}};
        else s = {{"|x| <= pi/4 x y", xs, ys, {}}};
    }
    return s;
}

// The return address of one real call site per function (`jal` + 8, decomp assembly/asm), so ra is a genuine
// continuation. The __ieee754_* targets are reached by the wrappers' tail jumps; __ieee754_sqrtf by acosf's
// `jal __ieee754_sqrtf` at 0x00119EAC.
inline std::uint32_t call_site_return(const std::string& name) {
    struct Site { const char* name; std::uint32_t jal; };
    static constexpr Site kSites[] = {
        {"sinf", 0x0031A364}, {"cosf", 0x0031A370}, {"tanf", 0x004D5A8C}, {"atanf", 0x004D5ABC}, {"floorf", 0x0026B758},
        {"ceilf", 0x0023C04C}, {"fabsf", 0x00442B1C}, {"func_0011E328", 0x003635A8}, {"func_0011E340", 0x00359F34},
        {"func_0011E358", 0x001D97A0}, {"func_0011E370", 0x0026B9E0}, {"func_0011E388", 0x0012F7D0},
        {"__ieee754_acosf", 0x003635A8}, {"__ieee754_asinf", 0x00359F34}, {"__ieee754_atan2f", 0x001D97A0},
        {"__ieee754_logf", 0x0026B9E0}, {"__ieee754_powf", 0x0012F7D0}, {"__ieee754_sqrtf", 0x00119EAC},
    };
    for (const auto& s : kSites)
        if (name == s.name) return s.jal + 8;
    return 0x0031A364 + 8;   // [G] internal helpers (kernels, scalbnf, copysignf): any code address; sinf's site
}

}  // namespace b3tool

#endif
