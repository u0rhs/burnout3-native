// b3fp::hw (include/b3host/ps2float_hw.h) against b3fp::ref, operation by operation (rt1): hw under b3host::Ps2FloatEnv,
// ref under the host's default environment, as their callers run them. Operands: uniform random bits (every class:
// zeros, denormals, exponent 255), operands built around the add / sub thresholds (exponent gaps 0..60, both signs),
// the exponent extremes 0 / 1 / 253 / 254 / 255, near-cancellation (equal exponents, close significands), and madd /
// msub with random accumulators; div / sqrt / rsqrt (ref in a DefaultFloatEnv scope). Self-contained: no ELF, no disc.
#include "b3host/ps2float_hw.h"
#include "test.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>   // std::swap

#if defined(B3HOST_HAVE_PS2_FLOAT_ENV)
namespace {

std::mt19937_64 rng(20261107);
std::uint32_t rnd32() { return static_cast<std::uint32_t>(rng()); }
std::uint32_t make(std::uint32_t sign, int exp, std::uint32_t frac) {
    return (sign ? 0x80000000u : 0u) | (static_cast<std::uint32_t>(exp & 0xFF) << 23) | (frac & 0x7FFFFFu);
}
// a random operand of one of the interesting kinds
std::uint32_t operand(int base_exp = -1) {
    const std::uint32_t frac = rnd32() & 0x7FFFFFu, sign = rnd32() & 1u;
    switch (rnd32() % 8) {
    case 0: return rnd32();                                           // anything
    case 1: return make(sign, static_cast<int>(rnd32() % 5) == 4 ? 255 : static_cast<int>(rnd32() % 4), frac);   // 0..3, 255
    case 2: return make(sign, 250 + static_cast<int>(rnd32() % 6), frac);   // 250..255
    case 3: return make(sign, base_exp >= 0 ? base_exp : 1 + static_cast<int>(rnd32() % 254), frac & ~(rnd32() % 2 ? 0xFFFu : 0u));
    case 4: return make(sign, 127 + static_cast<int>(rnd32() % 9) - 4, rnd32() % 4 ? frac : 0);   // around 1.0
    default: return make(sign, 1 + static_cast<int>(rnd32() % 254), frac);
    }
}

// The hw operations run in functions the compiler cannot inline into the environment's scope, as in production (Vu0Unit
// sets Ps2FloatEnv, then calls the VU0 program through a pointer): an inlined float operation may be scheduled before the
// FPCR / MXCSR write (no FENV_ACCESS) -- clang on AArch64 did exactly that with an inlined hw::mul here
namespace w {
[[gnu::noinline]] std::uint32_t add(std::uint32_t a, std::uint32_t b) { return b3fp::hw::add(a, b); }
[[gnu::noinline]] std::uint32_t sub(std::uint32_t a, std::uint32_t b) { return b3fp::hw::sub(a, b); }
[[gnu::noinline]] std::uint32_t mul(std::uint32_t a, std::uint32_t b) { return b3fp::hw::mul(a, b); }
[[gnu::noinline]] std::uint32_t madd(std::uint32_t c, std::uint32_t a, std::uint32_t b) { return b3fp::hw::madd(c, a, b); }
[[gnu::noinline]] std::uint32_t msub(std::uint32_t c, std::uint32_t a, std::uint32_t b) { return b3fp::hw::msub(c, a, b); }
}  // namespace w
struct Count { std::uint64_t n = 0, bad = 0; std::uint32_t a = 0, b = 0, c = 0, want = 0, got = 0; };
void note(Count& k, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t want, std::uint32_t got) {
    ++k.n;
    if (want != got && k.bad++ == 0) { k.a = a; k.b = b; k.c = c; k.want = want; k.got = got; }
}
void report(const char* name, const Count& k) {
    if (k.bad) std::printf("    %s: %llu of %llu differ (first: %08X %08X %08X ref %08X hw %08X)\n", name,
                           static_cast<unsigned long long>(k.bad), static_cast<unsigned long long>(k.n), k.a, k.b, k.c, k.want, k.got);
    else std::printf("    %s: %llu operand sets, 0 differ\n", name, static_cast<unsigned long long>(k.n));
}

template <class Ref, class Hw>
void two(Count& k, std::uint32_t a, std::uint32_t b, Ref ref, Hw hw) {
    const std::uint32_t want = ref(a, b);
    std::uint32_t got;
    { b3host::Ps2FloatEnv env; got = hw(a, b); }
    note(k, a, b, 0, want, got);
}

}  // namespace

TEST(hw_add_sub_mul_against_ref) {
    namespace r = b3fp::ref;
    namespace h = b3fp::hw;
    Count add, sub, mul;
    for (int i = 0; i < 3000000; ++i) {
        std::uint32_t a = operand(), b = operand();
        if (i % 3 == 0) {   // a chosen exponent gap 0..60 (the 54 threshold and both sides of it)
            const int ea = 1 + static_cast<int>(rnd32() % 254), gap = static_cast<int>(rnd32() % 61);
            a = make(rnd32() & 1u, ea, rnd32());
            b = make(rnd32() & 1u, ea - gap >= 1 ? ea - gap : 1, rnd32());
            if (rnd32() & 1u) std::swap(a, b);
        } else if (i % 7 == 0) {   // near cancellation
            a = make(rnd32() & 1u, 1 + static_cast<int>(rnd32() % 254), rnd32());
            b = (a ^ 0x80000000u) ^ (rnd32() & 0xFu);
        }
        two(add, a, b, [](std::uint32_t x, std::uint32_t y) { return r::add(x, y); }, [](std::uint32_t x, std::uint32_t y) { return w::add(x, y); });
        two(sub, a, b, [](std::uint32_t x, std::uint32_t y) { return r::sub(x, y); }, [](std::uint32_t x, std::uint32_t y) { return w::sub(x, y); });
        two(mul, a, b, [](std::uint32_t x, std::uint32_t y) { return r::mul(x, y); }, [](std::uint32_t x, std::uint32_t y) { return w::mul(x, y); });
    }
    report("add", add);
    report("sub", sub);
    report("mul", mul);
    CHECK(add.bad == 0 && sub.bad == 0 && mul.bad == 0);
}

TEST(hw_madd_msub_against_ref) {
    namespace r = b3fp::ref;
    namespace h = b3fp::hw;
    Count madd, msub;
    for (int i = 0; i < 2000000; ++i) {
        const std::uint32_t c = operand(), a = operand(), b = operand();
        const std::uint32_t w1 = r::madd(c, a, b), w2 = r::msub(c, a, b);
        std::uint32_t g1, g2;
        { b3host::Ps2FloatEnv env; g1 = w::madd(c, a, b); g2 = w::msub(c, a, b); }
        note(madd, a, b, c, w1, g1);
        note(msub, a, b, c, w2, g2);
    }
    report("madd", madd);
    report("msub", msub);
    CHECK(madd.bad == 0 && msub.bad == 0);
}

TEST(hw_div_sqrt_rsqrt_against_ref) {
    namespace r = b3fp::ref;
    namespace h = b3fp::hw;
    Count dv, sq, rs;
    for (int i = 0; i < 300000; ++i) {
        const std::uint32_t a = operand(), b = i % 50 == 0 ? (rnd32() & 0x807FFFFFu) & (rnd32() % 2 ? 0x80000000u : 0xFFFFFFFFu) : operand();
        const std::uint32_t w1 = r::vu_div(a, b), w2 = r::vu_sqrt(a), w3 = r::vu_rsqrt(a, b);
        std::uint32_t g1, g2, g3;
        { b3host::Ps2FloatEnv env; g1 = h::vu_div(a, b); g2 = h::vu_sqrt(a); g3 = h::vu_rsqrt(a, b); }
        note(dv, a, b, 0, w1, g1);
        note(sq, a, 0, 0, w2, g2);
        note(rs, a, b, 0, w3, g3);
    }
    report("vu_div", dv);
    report("vu_sqrt", sq);
    report("vu_rsqrt", rs);
    CHECK(dv.bad == 0 && sq.bad == 0 && rs.bad == 0);
}
// The shape that broke before the hardening: the hw operations inlined right inside the environment's scope. With
// -frounding-math (this test and the units that compute under Ps2FloatEnv are compiled with it, Makefile VU0_FP_ENV) no
// float operation may move across the MXCSR / FPCR write
TEST(hw_inlined_in_the_scope) {
    namespace r = b3fp::ref;
    namespace h = b3fp::hw;
    Count add, sub, mul, madd, msub;
    for (int i = 0; i < 1000000; ++i) {
        const std::uint32_t c = operand(), a = operand(), b = operand();
        const std::uint32_t w1 = r::add(a, b), w2 = r::sub(a, b), w3 = r::mul(a, b), w4 = r::madd(c, a, b), w5 = r::msub(c, a, b);
        std::uint32_t g1, g2, g3, g4, g5;
        {
            b3host::Ps2FloatEnv env;
            g1 = h::add(a, b); g2 = h::sub(a, b); g3 = h::mul(a, b); g4 = h::madd(c, a, b); g5 = h::msub(c, a, b);
        }
        note(add, a, b, 0, w1, g1); note(sub, a, b, 0, w2, g2); note(mul, a, b, 0, w3, g3);
        note(madd, a, b, c, w4, g4); note(msub, a, b, c, w5, g5);
    }
    report("add (inlined)", add); report("sub (inlined)", sub); report("mul (inlined)", mul);
    report("madd (inlined)", madd); report("msub (inlined)", msub);
    CHECK(add.bad == 0 && sub.bad == 0 && mul.bad == 0 && madd.bad == 0 && msub.bad == 0);
}
// vu0lanes (1.4.1): b3fp::vufast4's four-lane forms (what the generated VU0 programs now call for 2+ dest lanes) against
// ref lane by lane: every operation, both operand shapes (a register / one value for all lanes), every dest mask,
// operands of the interesting kinds and per-lane exponent gaps 0..60 (the far-apart fallback inside a packed op)
namespace w4 {
using Q = std::uint32_t[4];
[[gnu::noinline]] void run(int op, bool vs, std::uint32_t* out, const std::uint32_t* acc, const std::uint32_t* a, const std::uint32_t* b,
                           std::uint32_t s, unsigned mask) {
    namespace f = b3fp::vufast4;
    switch (op) {
    case 0: vs ? f::add_vs(out, a, s, mask) : f::add_vv(out, a, b, mask); break;
    case 1: vs ? f::sub_vs(out, a, s, mask) : f::sub_vv(out, a, b, mask); break;
    case 2: vs ? f::mul_vs(out, a, s, mask) : f::mul_vv(out, a, b, mask); break;
    case 3: vs ? f::madd_vs(out, acc, a, s, mask) : f::madd_vv(out, acc, a, b, mask); break;
    default: vs ? f::msub_vs(out, acc, a, s, mask) : f::msub_vv(out, acc, a, b, mask); break;
    }
}
}  // namespace w4
TEST(vufast4_lanes_against_ref) {
    namespace r = b3fp::ref;
    const char* names[] = {"add", "sub", "mul", "madd", "msub"};
    Count k[5][2];
    std::uint64_t far_lanes = 0;
    for (int i = 0; i < 4000000; ++i) {
        const int op = i % 5;
        const bool vs = (i / 5) % 2;
        const unsigned mask = 1u + rnd32() % 15u;
        std::uint32_t acc[4], a[4], b[4], out[4] = {};
        for (unsigned l = 0; l < 4; ++l) {
            acc[l] = operand();
            if (rnd32() % 2) {   // a chosen gap between a and b on this lane
                const int ea = 1 + static_cast<int>(rnd32() % 254), gap = static_cast<int>(rnd32() % 61);
                a[l] = make(rnd32() & 1u, ea, rnd32());
                b[l] = make(rnd32() & 1u, ea - gap >= 1 ? ea - gap : 1, rnd32());
                if (rnd32() & 1u) std::swap(a[l], b[l]);
                far_lanes += gap > 28;
            } else {
                a[l] = operand();
                b[l] = operand();
            }
        }
        const std::uint32_t s = b[rnd32() % 4];
        {
            b3host::Ps2FloatEnv env;
            w4::run(op, vs, out, acc, a, b, s, mask);
        }
        for (unsigned l = 0; l < 4; ++l) {
            if (!((mask >> l) & 1)) continue;
            const std::uint32_t bb = vs ? s : b[l];
            const std::uint32_t want = op == 0 ? r::add(a[l], bb) : op == 1 ? r::sub(a[l], bb) : op == 2 ? r::mul(a[l], bb)
                                     : op == 3 ? r::madd(acc[l], a[l], bb) : r::msub(acc[l], a[l], bb);
            note(k[op][vs], a[l], bb, acc[l], want, out[l]);
        }
    }
    bool ok = true;
    for (int op = 0; op < 5; ++op)
        for (int v = 0; v < 2; ++v) {
            char n[24];
            std::snprintf(n, sizeof n, "%s_%s (4 lanes)", names[op], v ? "vs" : "vv");
            report(n, k[op][v]);
            ok = ok && k[op][v].bad == 0;
        }
    std::printf("    (%llu lanes with exponents more than 28 apart)\n", static_cast<unsigned long long>(far_lanes));
    CHECK(ok);
}
#else
TEST(hw_no_float_env) { std::printf("    (skipped: no host float environment; vufast uses ref)\n"); }
#endif

TEST_MAIN("b3fp::hw vs b3fp::ref (ps2float_hw.h)")
