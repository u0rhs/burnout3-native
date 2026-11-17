// The bit-exact PS2 libm port (include/b3libm) against retail: each retail function runs in tests/r5900_mini.h
// straight from orig/SLUS_210.50 and must give the same bits as the port on every input of the sweep.
#include "b3libm/ps2libm.h"
#include "libm_oracle.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace b3libm::ps2;
using oracle::compare1;
using oracle::sweep_all_exponents;
using oracle::sweep_range;

static std::uint32_t U(F f) { return f.b; }

TEST(basic_helpers_vs_retail) {
    const auto all = sweep_all_exponents(65);
    compare1("fabsf", 0x0011DF70, all, [](std::uint32_t x) { return U(fabsf(fb(x))); });
    compare1("floorf", 0x0011DF98, all, [](std::uint32_t x) { return U(floorf(fb(x))); });
    compare1("ceilf", 0x0011DD80, all, [](std::uint32_t x) { return U(ceilf(fb(x))); });
    compare1("__ieee754_sqrtf", 0x0011B438, all, [](std::uint32_t x) { return U(sqrtf(fb(x))); });
    // dense: [0.25, 4) for sqrt (both exponent parities), [-4, 4] for floor/ceil
    compare1("__ieee754_sqrtf dense", 0x0011B438, sweep_range(0x3E800000, 0x407FFFFF, 97), [](std::uint32_t x) { return U(sqrtf(fb(x))); });
    std::vector<std::uint32_t> neg = sweep_range(0x3F000000, 0x40800000, 211);
    for (std::uint32_t& x : neg) x |= 0x80000000u;
    compare1("floorf dense -", 0x0011DF98, neg, [](std::uint32_t x) { return U(floorf(fb(x))); });
    compare1("ceilf dense -", 0x0011DD80, neg, [](std::uint32_t x) { return U(ceilf(fb(x))); });
    compare1("floorf dense +", 0x0011DF98, sweep_range(0x3F000000, 0x40800000, 211), [](std::uint32_t x) { return U(floorf(fb(x))); });
    compare1("ceilf dense +", 0x0011DD80, sweep_range(0x3F000000, 0x40800000, 211), [](std::uint32_t x) { return U(ceilf(fb(x))); });
}

TEST(trig_vs_retail) {
    const auto all = sweep_all_exponents(65);
    // sweeps that stay below the large-argument path (|x| <= 0x43490F80) when __kernel_rem_pio2f is not ported
    std::vector<std::uint32_t> small;
    for (std::uint32_t x : all) if (B3LIBM_HAVE_KERNEL_REM_PIO2F || (x & 0x7FFFFFFFu) <= 0x43490F80u) small.push_back(x);
    compare1("sinf", 0x0011E138, small, [](std::uint32_t x) { return U(sinf(fb(x))); });
    compare1("cosf", 0x0011DE98, small, [](std::uint32_t x) { return U(cosf(fb(x))); });
    // dense over every branch of the reduction: [0, 0x43490F80] both signs (step 4099: ~276k inputs each)
    std::vector<std::uint32_t> dense = sweep_range(0, 0x43490F80, 4099);
    const std::size_t n = dense.size();
    for (std::size_t i = 0; i < n; ++i) dense.push_back(dense[i] | 0x80000000u);
    compare1("sinf dense", 0x0011E138, dense, [](std::uint32_t x) { return U(sinf(fb(x))); });
    compare1("cosf dense", 0x0011DE98, dense, [](std::uint32_t x) { return U(cosf(fb(x))); });
    // near multiples of pi/2, where the medium path's second and third iterations run
    std::vector<std::uint32_t> near;
    for (int k = 1; k < 120; ++k) {
        const std::uint32_t c = t::fbits(static_cast<float>(k * 1.5707963267948966));
        for (int d = -300; d <= 300; ++d) near.push_back(c + static_cast<std::uint32_t>(d));
    }
    compare1("sinf near k*pi/2", 0x0011E138, near, [](std::uint32_t x) { return U(sinf(fb(x))); });
    compare1("cosf near k*pi/2", 0x0011DE98, near, [](std::uint32_t x) { return U(cosf(fb(x))); });
}

TEST(inverse_trig_vs_retail) {
    const auto all = sweep_all_exponents(65);
    compare1("acosf", 0x0011E328, all, [](std::uint32_t x) { return U(acosf(fb(x))); });   // the wrapper
    compare1("asinf", 0x0011E340, all, [](std::uint32_t x) { return U(asinf(fb(x))); });
    // dense over [-1, 1] (every branch: tiny, < 0.5, 0.5..0.975, > 0.975) plus the edges past 1
    std::vector<std::uint32_t> d = sweep_range(0, 0x3F800010, 1531);
    const std::size_t n = d.size();
    for (std::size_t i = 0; i < n; ++i) d.push_back(d[i] | 0x80000000u);
    compare1("acosf dense", 0x00119C20, d, [](std::uint32_t x) { return U(acosf(fb(x))); });
    compare1("asinf dense", 0x0011A048, d, [](std::uint32_t x) { return U(asinf(fb(x))); });
}

// ---- self-contained (no ELF): independent expectations ----
// sqrtf: retail's integer square root with `q += q & 1` is round-to-nearest for every normal input [derivation: the
// 25th result bit is the rounding bit; ties are impossible with a non-zero remainder], so host IEEE sqrtf is an
// independent oracle on exponents 1..254. Exponent 255 is a number on the PS2 integer path: 2^128 -> 2^64.
TEST(sqrtf_equals_ieee_round_to_nearest) {
    unsigned bad = 0;
    for (std::uint32_t b = 0x00800000u; b < 0x7F800000u; b += 37) {
        const float host = std::sqrt(t::bitsf(b));
        if (sqrtf(fb(b)).b != t::fbits(host) && ++bad <= 3) CHECK_BITS(sqrtf(fb(b)).b, t::fbits(host));
    }
    CHECK(bad == 0);
    CHECK_BITS(sqrtf(fb(0x7F800000u)).b, 0x5F800000u);   // sqrt(2^128) = 2^64 (exponent 255 is a number)
    CHECK_BITS(sqrtf(fb(0x00000001u)).b, 0x00000001u);   // denormal encodings come back unchanged (zero test)
    CHECK_BITS(sqrtf(fb(0x80000000u)).b, 0x80000000u);   // -0
    CHECK_BITS(sqrtf(fb(0xC0800000u)).b, 0x7F7FFFFFu);   // sqrt(-4) = (x-x)/(x-x) = 0/0 -> +MAX (ps2float.h D2)
}

// floorf/ceilf: integer algorithms; the inexact probe `x + 1e30 > 0` is always true, so for normal inputs they are
// exactly floor/ceil.
TEST(floor_ceil_equal_host_on_normals) {
    unsigned bad = 0;
    for (std::uint32_t e = 1; e < 255; ++e)
        for (std::uint32_t m = 0; m < 0x800000u; m += 0x1FFFF)
            for (std::uint32_t sgn = 0; sgn < 2; ++sgn) {
                const std::uint32_t b = (sgn << 31) | (e << 23) | m;
                const float x = t::bitsf(b);
                if (floorf(fb(b)).b != t::fbits(std::floor(x)) || ceilf(fb(b)).b != t::fbits(std::ceil(x))) ++bad;
            }
    CHECK(bad == 0);
    CHECK_BITS(floorf(fb(0x80000001u)).b, 0x80000001u);  // a negative denormal encoding is returned as is (-0 on the PS2)
    CHECK_BITS(ceilf(fb(0x00000001u)).b, 0x00000001u);
    CHECK_BITS(floorf(fb(0x7F800000u)).b, 0x7F800000u);  // exponent 255: |x| >= 2^23, returned
}

// Special values, each derived by hand from the retail code path (address) and the FPU model.
TEST(special_values) {
    CHECK_BITS(sinf(fb(0)).b, 0u);
    CHECK_BITS(sinf(fb(0x80000000u)).b, 0x80000000u);
    CHECK_BITS(sinf(fb(0x31000000u)).b, 0x31000000u);    // |x| < 2^-27: (int)x == 0 -> x  [__kernel_sinf 0x11CF58]
    CHECK_BITS(cosf(fb(0)).b, 0x3F800000u);              // [__kernel_cosf 0x11C5A8]
    CHECK_BITS(cosf(fb(0x31000000u)).b, 0x3F800000u);
    CHECK_BITS(acosf(fb(0x3F800000u)).b, 0u);            // acos(1) = 0
    CHECK_BITS(acosf(fb(0xBF800000u)).b, 0x40490FDAu);   // acos(-1): the folded constant [0x119C5C]
    CHECK_BITS(acosf(fb(0)).b, 0x3FC90FDAu);             // |x| <= 2^-57 -> folded pio2_hi + pio2_lo [0x119CA8]
    CHECK_BITS(acosf(fb(0x40000000u)).b, 0x7F7FFFFFu);   // |x| > 1: 0/0 -> +MAX
    CHECK_BITS(asinf(fb(0x40000000u)).b, 0x7F7FFFFFu);
    CHECK_BITS(asinf(fb(0x3F800000u)).b, 0x3FC90FDAu);   // 1*pio2_hi + 1*pio2_lo, truncated: pio2_lo < 1/2 ulp
    CHECK_BITS(asinf(fb(0xBF800000u)).b, 0xBFC90FDAu);
    CHECK_BITS(asinf(fb(0x30000000u)).b, 0x30000000u);   // tiny: x
    // exponent 255 is an ordinary number to the integer paths, and clamped to MAX by the FPU model's operations:
    // sinf(0x7F800000) must not throw or loop (large-argument path) when the kernel is ported
    if (B3LIBM_HAVE_KERNEL_REM_PIO2F) { volatile std::uint32_t r = sinf(fb(0x7F800000u)).b; (void)r; CHECK(true); }
}

TEST(fast_mode_is_host_libm_without_inf_nan) {
    CHECK(b3libm::fast::sinf(0.5f) == static_cast<float>(std::sin(0.5f)));
    CHECK(b3libm::fast::acosf(2.0f) == FLT_MAX);         // host NaN -> +MAX, as the PS2 build's 0/0
    CHECK(b3libm::fast::logf(0.0f) == -FLT_MAX);         // host -Inf -> -MAX
    CHECK(b3libm::fast::powf(10.0f, 100.0f) == FLT_MAX);
    // fast vs faithful: close (the faithful port truncates; differences are a few ulp)
    double worst = 0;
    for (float x = -10.0f; x < 10.0f; x += 0.001f) {
        worst = std::max(worst, std::fabs(static_cast<double>(b3libm::fast::sinf(x)) - to_host(sinf(from_float(x)))));
        worst = std::max(worst, std::fabs(static_cast<double>(b3libm::fast::cosf(x)) - to_host(cosf(from_float(x)))));
    }
    CHECK(worst < 1e-6);
    double worst_a = 0;
    for (float x = -1.0f; x <= 1.0f; x += 0.0001f)
        worst_a = std::max(worst_a, std::fabs(static_cast<double>(b3libm::fast::acosf(x)) - to_host(acosf(from_float(x)))));
    CHECK(worst_a < 1e-6);
}

TEST_MAIN("PS2 libm port vs retail (R5900 interpreter oracle)")
