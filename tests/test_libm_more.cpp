// The bit-exact PS2 libm port, second batch (atanf, atan2f, __kernel_tanf, tanf, logf, powf, __kernel_rem_pio2f)
// against retail: each retail function runs in tests/r5900_mini.h straight from orig/SLUS_210.50 and must give the
// same bits as the port on every input of the sweep.
#include "b3libm/ps2_atan.h"
#include "b3libm/ps2_exp.h"
#include "b3libm/ps2_rempio2.h"
#include "b3libm/ps2_tan.h"
#include "libm_oracle.h"

using namespace b3libm::ps2;
using oracle::compare1;
using oracle::compare2;
using oracle::sweep_all_exponents;
using oracle::sweep_range;

static std::uint32_t U(F f) { return f.b; }

// Retail entry points (SLUS_210.50).
constexpr std::uint32_t kAtanf = 0x0011DAF0, kAtan2f = 0x0011A3E8, kAtan2fWrapper = 0x0011E358, kTanf = 0x0011E220,
                        kKernelTanf = 0x0011D028, kLogf = 0x0011A5C8, kPowf = 0x0011A8A8,
                        kKernelRemPio2f = 0x0011C6B8, kTwoOverPiAddr = 0x004B2BF8;

static std::vector<std::uint32_t> negated(std::vector<std::uint32_t> v) {
    for (std::uint32_t& x : v) x ^= 0x80000000u;
    return v;
}
static std::vector<std::uint32_t> cat(std::vector<std::uint32_t> a, const std::vector<std::uint32_t>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
static std::vector<std::uint32_t> both_signs(const std::vector<std::uint32_t>& v) { return cat(v, negated(v)); }
// Encodings every float function should see: zeros, denormal encodings, +-1, MAX, exponent 255.
static const std::vector<std::uint32_t> kSpecials = {
    0x00000000, 0x80000000, 0x00000001, 0x807FFFFF, 0x00800000, 0x80800000, 0x3F800000, 0xBF800000,
    0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0xFFFFFFFF};

TEST(atan_vs_retail) {
    const auto all = sweep_all_exponents(65);
    const auto port = [](std::uint32_t x) { return U(atanf(fb(x))); };
    compare1("atanf", kAtanf, all, port);
    // 7/16 .. 39/16 and a margin: every id branch (0x3EE00000, 0x3F300000, 0x3F980000, 0x401C0000)
    compare1("atanf dense +", kAtanf, sweep_range(0x3EC00000, 0x40300000, 41), port);
    compare1("atanf dense -", kAtanf, negated(sweep_range(0x3EC00000, 0x40300000, 83)), port);
    // the boundaries themselves, one encoding apart
    std::vector<std::uint32_t> edges;
    for (std::uint32_t b : {0x31000000u, 0x3EE00000u, 0x3F300000u, 0x3F980000u, 0x401C0000u, 0x50800000u})
        for (std::uint32_t d = 0; d < 64; ++d) { edges.push_back(b - 32 + d); }
    compare1("atanf breakpoints", kAtanf, both_signs(edges), port);
    compare1("atanf tiny/huge", kAtanf, both_signs(cat(sweep_range(0x30000000, 0x31800000, 1201),
                                                       sweep_range(0x50000000, 0x51000000, 1201))), port);
}

TEST(atan2_vs_retail) {
    const auto port = [](std::uint32_t y, std::uint32_t x) { return U(atan2f(fb(y), fb(x))); };
    // every exponent pair (2 mantissas, both signs) plus the special encodings: zeros, x == 1, k > 60, k < -60
    const auto grid = cat(sweep_all_exponents(2), kSpecials);
    compare2("atan2f exponents", kAtan2f, grid, grid, port);
    // ratio-dense: y, x in [-4, 4]
    std::vector<std::uint32_t> v = both_signs(sweep_range(0x3D000000, 0x40800000, 0x2FFF1));
    compare2("atan2f [-4,4]^2", kAtan2f, v, v, port);
    // the wrapper is a tail jump: same results
    compare2("atan2f wrapper", kAtan2fWrapper, kSpecials, both_signs(sweep_range(0x3E000000, 0x41000000, 0x400001)),
             port);
}

// __kernel_tanf directly: (x, y, iy), x in [-pi/4, pi/4] including |x| < 2^-28 and the 0.6744 split.
TEST(kernel_tanf_vs_retail) {
    mini::R5900* m = oracle::cpu();
    if (!m) { std::printf("  (kernel_tanf: retail ELF not found, oracle comparison skipped)\n"); return; }
    std::vector<std::uint32_t> xs = cat(sweep_range(0x30000000, 0x3F490FDA, 40009), sweep_range(0x3F2C0000, 0x3F2D0000, 61));
    xs = cat(both_signs(xs), {0x00000000, 0x80000000, 0x00000005, 0x80000005, 0x317FFFFF, 0x31800000});
    const std::vector<std::uint32_t> ys = {0x00000000, 0x80000000, 0x32000000, 0xB2000000, 0x33A22168, 0xB3A22168,
                                           0x2F123456, 0xAF654321};
    unsigned bad = 0;
    std::size_t n = 0;
    for (int iy : {1, -1})
        for (std::uint32_t x : xs)
            for (std::uint32_t y : ys) {
                ++n;
                const std::uint32_t want = m->call_f(kKernelTanf, {x, y}, {static_cast<std::uint64_t>(static_cast<std::int64_t>(iy))});
                const std::uint32_t got = U(kernel_tanf(fb(x), fb(y), iy));
                if (got != want && ++bad <= 5) {
                    char msg[200];
                    std::snprintf(msg, sizeof msg, "kernel_tanf(0x%08X, 0x%08X, %d): port 0x%08X, retail 0x%08X", x, y, iy, got, want);
                    t::fail(__FILE__, __LINE__, msg);
                }
            }
    oracle::finish("__kernel_tanf", bad, n);
}

TEST(tan_vs_retail) {
    const auto port = [](std::uint32_t x) { return U(tanf(fb(x))); };
    compare1("tanf", kTanf, sweep_all_exponents(65), port);
    compare1("tanf dense [0.5, 201]", kTanf, both_signs(sweep_range(0x3F000000, 0x43490F80, 1999)), port);
    compare1("tanf near pi/4, 0.6744", kTanf, both_signs(cat(sweep_range(0x3F48F000, 0x3F4A0000, 7),
                                                            sweep_range(0x3F2C0000, 0x3F2D0000, 7))), port);
    // large arguments: __ieee754_rem_pio2f -> __kernel_rem_pio2f (|x| > 0x43490F80), up to exponent 255
    compare1("tanf [201, 1e6]", kTanf, both_signs(sweep_range(0x43490F81, 0x49742400, 4999)), port);
    compare1("tanf [1e6, exp 255]", kTanf, both_signs(sweep_range(0x49742400, 0x7FFFFFFF, 30011)), port);
}

// __kernel_rem_pio2f directly, for prec 0, 1, 2 (retail calls it with prec 2 only; prec 3 is broken in retail, see
// ps2_rempio2.h): x[] is split the way
// __ieee754_rem_pio2f does it; x at 0x01FF8000, y at 0x01FF8100 in guest RAM, ipio2 = retail's two_over_pi.
TEST(kernel_rem_pio2f_vs_retail) {
    CHECK(kTwoOverPi[0] == 0xA2 && kTwoOverPi[197] == 0x6B);
    mini::R5900* m = oracle::cpu();
    if (!m) { std::printf("  (kernel_rem_pio2f: retail ELF not found, oracle comparison skipped)\n"); return; }
    for (std::uint32_t i = 0; i < 198; ++i) CHECK(m->peek32(kTwoOverPiAddr + 4 * i) == static_cast<std::uint32_t>(kTwoOverPi[i]));
    const auto xs = cat(sweep_range(0x43490F81, 0x4A000000, 12007), sweep_range(0x4A000000, 0x7FFFFFFF, 60013));
    constexpr std::uint32_t kX = 0x01FF8000, kY = 0x01FF8100;
    for (int prec = 0; prec < 3; ++prec) {
        unsigned bad = 0;
        for (std::uint32_t xb : xs) {
            const std::int32_t ix = static_cast<std::int32_t>(xb);
            const std::int32_t e0 = (ix >> 23) - 134;
            F z = from_word(ix - (e0 << 23));
            F tx[3];
            for (int i = 0; i < 2; ++i) { tx[i] = to_float(to_int(z)); z = (z - tx[i]) * fb(0x43800000); }
            tx[2] = z;
            int nx = 3;
            while (tx[nx - 1] == kZero) --nx;
            for (int i = 0; i < 3; ++i) { m->poke32(kX + 4 * i, tx[i].b); m->poke32(kY + 4 * i, 0xDEADBEEF); }
            const int want_n = static_cast<int>(m->call_i(kKernelRemPio2f, {},
                {kX, kY, static_cast<std::uint64_t>(static_cast<std::int64_t>(e0)), static_cast<std::uint64_t>(nx),
                 static_cast<std::uint64_t>(prec), kTwoOverPiAddr}));
            F y[3] = {fb(0xDEADBEEF), fb(0xDEADBEEF), fb(0xDEADBEEF)};
            const int got_n = kernel_rem_pio2f(tx, y, e0, nx, prec, kTwoOverPi);
            bool same = got_n == want_n;
            for (int i = 0; i < 3; ++i) same = same && y[i].b == m->peek32(kY + 4 * i);
            if (!same && ++bad <= 5) {
                char msg[200];
                std::snprintf(msg, sizeof msg, "kernel_rem_pio2f(x=0x%08X, prec %d): port n=%d y=%08X %08X %08X, retail n=%d y=%08X %08X %08X",
                              xb, prec, got_n, y[0].b, y[1].b, y[2].b, want_n, m->peek32(kY), m->peek32(kY + 4), m->peek32(kY + 8));
                t::fail(__FILE__, __LINE__, msg);
            }
        }
        char name[40];
        std::snprintf(name, sizeof name, "__kernel_rem_pio2f prec %d", prec);
        oracle::finish(name, bad, xs.size());
    }
}

TEST(log_vs_retail) {
    const auto port = [](std::uint32_t x) { return U(logf(fb(x))); };
    compare1("logf", kLogf, cat(sweep_all_exponents(65), kSpecials), port);
    compare1("logf dense [0.5, 2]", kLogf, sweep_range(0x3F000000, 0x40000000, 37), port);
    compare1("logf near 1", kLogf, sweep_range(0x3F7E0000, 0x3F820000, 3), port);
    compare1("logf sqrt2 split", kLogf, cat(sweep_range(0x3FB4F000, 0x3FB51000, 1), sweep_range(0x3F34F000, 0x3F351000, 1)),
             port);
    // |f| < 2^-20 branch for every exponent: mantissas 0x7FFFF0..0x7FFFFF and 0x000000..0x000010
    std::vector<std::uint32_t> near1;
    for (std::uint32_t e = 1; e < 256; ++e) {
        for (std::uint32_t mnt = 0x7FFFF0; mnt <= 0x7FFFFF; ++mnt) near1.push_back((e << 23) | mnt);
        for (std::uint32_t mnt = 0; mnt <= 0x10; ++mnt) near1.push_back((e << 23) | mnt);
    }
    compare1("logf |f| < 2^-20", kLogf, near1, port);
    // the i|j > 0 split (mantissa 0x30A3D0 .. 0x35C288)
    std::vector<std::uint32_t> ij;
    for (std::uint32_t e : {0x3Fu, 0x40u, 0x7Eu, 0x10u, 0xF0u})
        for (std::uint32_t b : {0x30A3D0u, 0x35C288u})
            for (std::uint32_t d = 0; d < 64; ++d) ij.push_back((e << 23) | (b - 32 + d));
    compare1("logf i|j split", kLogf, ij, port);
}

TEST(pow_vs_retail) {
    const auto port = [](std::uint32_t x, std::uint32_t y) { return U(powf(fb(x), fb(y))); };
    // x: (0, 8] spread, both signs, near 1, specials; y: integers and halves in [-8, 8], a spread, huge/small
    std::vector<std::uint32_t> xs = both_signs(cat(sweep_range(0x3C000000, 0x41000000, 0xCCC1),
                                                   sweep_range(0x3F7FFFF0, 0x3F800010, 1)));
    xs = cat(xs, kSpecials);
    std::vector<std::uint32_t> ys;
    for (int k = -16; k <= 16; ++k) ys.push_back(from_float(0.5f * static_cast<float>(k)).b);
    ys = cat(ys, both_signs(sweep_range(0x3C000000, 0x41000000, 0x1C0001)));
    ys = cat(ys, both_signs({0x4D000000, 0x4D000001, 0x4E000000, 0x4F800000, 0x4B800000, 0x4B7FFFFF, 0x4B7FFFFE,
                             0x2EDBE6FF, 0x00000001, 0x00800000, 0x7F7FFFFF, 0x7F800000, 0x7FFFFFFF, 0x3F7FFFFF}));
    compare2("powf grid", kPowf, xs, ys, port);
    // every exponent of x against a few y
    const std::vector<std::uint32_t> ysmall = both_signs({0x3F800000, 0x40000000, 0x3F000000, 0x40400000, 0x40490FDA,
                                                          0x3E800000, 0x41200000, 0x3DCCCCCD, 0x42C80000, 0x4D800000});
    compare2("powf x exponents", kPowf, sweep_all_exponents(17), ysmall, port);
    // the overflow/underflow boundaries (|z| near 128 / 126 in log2): x in [0.25, 4], y near +-126..+-130
    compare2("powf over/underflow", kPowf, both_signs(sweep_range(0x3E800000, 0x40800000, 0x7FFF1)),
             both_signs(sweep_range(0x42F80000, 0x43060000, 0x1801)), port);
    // exact boundaries z = 129 / -126 (i == FLT_UWORD_EXP_MAX / _MIN) and results below 2^-126 (scalbnf path):
    // x = 2^k (and a few others) with y dense around 126..130 / k, both signs
    std::vector<std::uint32_t> yb = sweep_range(0x42F00000, 0x43080000, 0x401);
    for (float v : {126.0f, 129.0f, 125.75f, 125.5f, 63.0f, 64.5f, 42.0f, 43.0f, 31.5f, 32.25f, 252.0f, 258.0f})
        for (int d = -2; d <= 2; ++d) yb.push_back(from_float(v).b + static_cast<std::uint32_t>(d));
    compare2("powf exact boundaries", kPowf,
             both_signs({0x40000000, 0x3F000000, 0x40800000, 0x3E800000, 0x41000000, 0x3E000000, 0x40400000,
                         0x3FC00000, 0x3F400000, 0x40490FDA, 0x3F3504F3, 0x3FB504F3}),
             both_signs(yb), port);
    // negative x with integer y (yisint odd/even via the j << (23-k) test), non-integers just off them, 2^23..2^24
    std::vector<std::uint32_t> yint;
    for (int k = 1; k <= 300; ++k) {
        const std::uint32_t b = from_float(static_cast<float>(k)).b;
        yint.push_back(b);
        yint.push_back(b + 1);
    }
    for (std::uint32_t b : {0x4B000000u, 0x4B000001u, 0x4B7FFFFFu, 0x4B800000u, 0x4AFFFFFFu, 0x4AFFFFFEu}) yint.push_back(b);
    compare2("powf x<0, integer y", kPowf, negated(sweep_range(0x3E000000, 0x40400000, 0x1FFFF7)), both_signs(yint), port);
    // |y| > 2^27 with x close to 1 (the t = x-1 series)
    compare2("powf huge y near 1", kPowf, both_signs(sweep_range(0x3F7FFFE0, 0x3F800020, 1)),
             both_signs({0x4D000001, 0x4D800000, 0x4E000000, 0x4D7FFFFF, 0x4F000000, 0x5F000000}), port);
}

// Self-contained checks (no ELF): retail bit patterns cited.
TEST(special_values) {
    CHECK_BITS(U(atan2f(kZero, fb(0xBF800000))), 0x40490FDA);        // atan2f(+0, -1) = retail's pi [C 0x11A44C]
    CHECK_BITS(U(atan2f(fb(0x80000000), fb(0xBF800000))), 0xC0490FDA);   // -pi [C 0x11A478]
    CHECK_BITS(U(atan2f(kOne, kZero)), 0x3FC90FDA);                   // pi/2 [C 0x11A4B4]
    CHECK_BITS(U(atan2f(fb(0xBF800000), kZero)), 0xBFC90FDA);
    CHECK_BITS(U(atan2f(fb(0x80000000), kOne)), 0x80000000);          // x == 1 -> atanf(y) = y for tiny y
    CHECK_BITS(U(atanf(fb(0x7F800000))), U(fb(0x3FC90FDA) + fb(0x33A22168)));   // no NaN/Inf test [C 0x11DB18]
    CHECK_BITS(U(atanf(kZero)), 0x00000000);
    CHECK_BITS(U(powf(fb(0x40000000), fb(0x40400000))), 0x41000000);  // 2^3 = 8 exactly
    CHECK_BITS(U(powf(fb(0xC0000000), fb(0x40400000))), 0xC1000000);  // (-2)^3 = -8
    CHECK_BITS(U(powf(fb(0x40800000), fb(0x3F000000))), 0x40000000);  // 4^0.5 -> sqrtf = 2
    CHECK_BITS(U(powf(fb(0x12345678), kZero)), 0x3F800000);           // x^0 = 1
    CHECK_BITS(U(powf(fb(0x3F7FFFF0), fb(0x4E000000))), 0x00000000);  // |y| > 2^27, x < 1: tiny*tiny folded to 0
    CHECK_BITS(U(powf(fb(0x3F7FFFF0), fb(0xCE000000))), 0x7F7FFFFF);  // huge*huge folded to MAX (0x4B2BF4)
    CHECK_BITS(U(logf(kOne)), 0x00000000);                            // log 1 = 0
    CHECK_BITS(U(logf(kZero)), 0xFF7FFFFF);                           // -MAX from 0x4B2B6C
    CHECK_BITS(U(tanf(kZero)), 0x00000000);
    CHECK_BITS(U(kernel_tanf(fb(0x80000000), kZero, -1)), U(kOne / kZero));   // 1/fabsf(-0)
}

TEST_MAIN("PS2 libm port vs retail, batch 2: atan, atan2, tan, log, pow, rem_pio2 (R5900 interpreter oracle)")
