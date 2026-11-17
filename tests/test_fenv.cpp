// b3host::Ps2FloatEnv (b3host/fenv.h): plain host float arithmetic under the EE's rounding (round toward zero, denormals
// flushed) against the bit-level model b3fp::ref (b3host/ps2float.h), on x86-64 (MXCSR RZ + FTZ + DAZ) and AArch64
// (FPCR RMode = RZ + FZ: Apple Silicon, Linux arm64).
//
// Operands are random binary32 encodings with exponents 0..254 (exponent 255 is D1, where the model clamps first), so
// zeros, denormal inputs, overflow and underflow all occur. add/sub pairs keep their exponents within 24 of each
// other: past ~29 ref::add's double staging rounds before it truncates (D3), which a host RZ add does not.
// div/sqrt are compared with the truncated exact result (ref::chop of the double quotient / root: the double is close
// enough that truncating it is exact truncation); that is the B3_PS2_FPU_DIVSQRT_CHOP=1 model, not the default D4.
#include "b3host/fenv.h"
#include "b3host/ps2float.h"
#include "test.h"

#include <cmath>
#include <cstdint>

#ifndef B3HOST_HAVE_PS2_FLOAT_ENV
#error "b3host/fenv.h has no Ps2FloatEnv for this target"
#endif

using b3fp::bits;
using b3fp::flt;

namespace {

struct Rng {
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    std::uint32_t next() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return static_cast<std::uint32_t>(s >> 16);
    }
};

// a random encoding with exponent 0..254; one in eight is a denormal or a zero
std::uint32_t operand(Rng& r) {
    const std::uint32_t b = r.next(), sign = b & 0x80000000u, frac = r.next() & 0x7FFFFFu;
    if ((b & 7u) == 0) return sign | ((b & 8u) ? frac : 0u);
    return sign | ((1u + r.next() % 254u) << 23) | frac;
}
int exponent(std::uint32_t b) { return static_cast<int>((b >> 23) & 0xFF); }

constexpr int kN = 1 << 20;
// Opaque operands and results: nothing folds at compile time, and every host result is stored (volatile) while the
// environment is set -- without FENV_ACCESS a pure float operation could otherwise be scheduled past the mode switch.
// The model (b3fp::ref, which uses doubles) runs outside the environment.
volatile float g_a[256], g_b[256], g_r[256];

// One operation over kN pairs under the environment; returns the mismatches, printing the first few.
template <class Host, class Ref, class Pick>
int sweep(const char* what, Host host, Ref ref, Pick pick) {
    Rng r;
    int bad = 0;
    long n = 0;
    for (int base = 0; base < kN; base += 256) {
        for (int i = 0; i < 256; ++i) {
            std::uint32_t a, b;
            pick(r, a, b);
            g_a[i] = flt(a);
            g_b[i] = flt(b);
        }
        {
            b3host::Ps2FloatEnv env;
            for (int i = 0; i < 256; ++i) g_r[i] = host(g_a[i], g_b[i]);
        }
        for (int i = 0; i < 256; ++i) {
            const float a = g_a[i], b = g_b[i];
            const std::uint32_t got = bits(g_r[i]), want = ref(bits(a), bits(b));
            ++n;
            if (got != want && ++bad <= 4)
                std::printf("    %s 0x%08X, 0x%08X: host 0x%08X, ref 0x%08X\n", what, bits(a), bits(b), got, want);
        }
    }
    std::printf("    %-6s %ld operand pairs under Ps2FloatEnv: %d differ from b3fp::ref\n", what, n, bad);
    return bad;
}

void pick_any(Rng& r, std::uint32_t& a, std::uint32_t& b) { a = operand(r); b = operand(r); }
void pick_near(Rng& r, std::uint32_t& a, std::uint32_t& b) {   // exponents within 24 (or a zero / denormal side)
    do { a = operand(r); b = operand(r); } while (exponent(a) && exponent(b) && std::abs(exponent(a) - exponent(b)) > 24);
}

__attribute__((noinline)) float add_out_of_line(float a, float b) { return a + b; }

}  // namespace

TEST(add_sub_mul_equal_ref_chop) {
    CHECK(sweep("add", [](float a, float b) { return a + b; }, b3fp::ref::add, pick_near) == 0);
    CHECK(sweep("sub", [](float a, float b) { return a - b; }, b3fp::ref::sub, pick_near) == 0);
    CHECK(sweep("mul", [](float a, float b) { return a * b; }, b3fp::ref::mul, pick_any) == 0);
}

TEST(madd_is_two_truncations) {   // c + a*b with -ffp-contract=off: the product truncated, then the sum (ref::madd)
    Rng r;
    int bad = 0;
    for (int i = 0; i < 1 << 16; ++i) {
        std::uint32_t a, b, c;
        do { a = operand(r); b = operand(r); c = operand(r); } while (exponent(a) < 64 || exponent(a) > 190 || exponent(b) < 64 || exponent(b) > 190);
        const std::uint32_t p = b3fp::ref::mul(a, b);
        if (exponent(c) && exponent(p) && std::abs(exponent(c) - exponent(p)) > 24) continue;
        g_a[0] = flt(a); g_b[0] = flt(b); g_a[1] = flt(c);
        {
            b3host::Ps2FloatEnv env;
            const float fa = g_a[0], fb = g_b[0], fc = g_a[1];
            g_r[0] = fc + fa * fb;
        }
        bad += bits(g_r[0]) != b3fp::ref::madd(c, a, b);
    }
    CHECK(bad == 0);
}

TEST(div_sqrt_truncate) {
    CHECK(sweep("div", [](float a, float b) { return a / b; },
                [](std::uint32_t a, std::uint32_t b) {
                    // finite, non-zero divisor, no denormal operands (both flush: x/0 is Inf on the host, D2)
                    return b3fp::ref::chop(static_cast<double>(flt(a)) / static_cast<double>(flt(b)));
                },
                [](Rng& r, std::uint32_t& a, std::uint32_t& b) {
                    do { a = operand(r); b = operand(r); } while (exponent(a) == 0 || exponent(b) == 0);
                }) == 0);
    CHECK(sweep("sqrt", [](float a, float) { return std::sqrt(a); },
                [](std::uint32_t a, std::uint32_t) { return b3fp::ref::chop(std::sqrt(static_cast<double>(flt(a)))); },
                [](Rng& r, std::uint32_t& a, std::uint32_t& b) {
                    do { a = operand(r) & 0x7FFFFFFFu; } while (exponent(a) == 0);
                    b = 0;
                }) == 0);
}

TEST(denormals_flush_inputs_and_outputs) {
    g_a[0] = flt(0x00000001u);   // the smallest denormal
    g_b[0] = 0x1p100f;
    g_a[1] = 1.5f * 0x1p-126f;   // 1.5 FLT_MIN - FLT_MIN = 0.5 FLT_MIN: an exact denormal result
    g_b[1] = 0x1p-126f;
    {
        b3host::Ps2FloatEnv env;
        const float a = g_a[0], b = g_b[0], c = g_a[1], d = g_b[1];
        g_r[0] = a * b;      // input flushed: 0 * 2^100
        g_r[1] = c - d;      // output flushed
    }
    {
        const float a = g_a[0], b = g_b[0], c = g_a[1], d = g_b[1];
        g_r[2] = a * b;
        g_r[3] = c - d;
    }
    const float in_env = g_r[0], tiny_in = g_r[1], out_env = g_r[2], tiny_out = g_r[3];
    CHECK_BITS(bits(in_env), 0u);
    CHECK_BITS(bits(tiny_in), 0u);
    CHECK_BITS(bits(b3fp::ref::sub(bits(g_a[1]), bits(g_b[1]))), 0u);
    CHECK(bits(out_env) != 0u);    // the default environment keeps denormals
    CHECK(bits(tiny_out) == 0x00400000u);
}

TEST(environment_scoped_and_restored) {
    g_a[0] = 1.0f;
    g_b[0] = 3.0f * 0x1p-25f;   // 1 + 3*2^-25: RN 0x3F800001, RZ 0x3F800000 (test_ps2float edge_table)
    { const float a = g_a[0], b = g_b[0]; g_r[0] = a + b; }
    {
        b3host::Ps2FloatEnv env;
        const float a = g_a[0], b = g_b[0];
        g_r[1] = a + b;
        g_r[2] = add_out_of_line(a, b);   // the mode is the thread's, not the expression's
    }
    { const float a = g_a[0], b = g_b[0]; g_r[3] = a + b; }
    float in = g_r[1];
    const float before = g_r[0], in_call = g_r[2], after = g_r[3];
    CHECK_BITS(bits(before), 0x3F800001u);
    CHECK_BITS(bits(in), 0x3F800000u);
    CHECK_BITS(bits(in_call), 0x3F800000u);
    CHECK_BITS(bits(after), 0x3F800001u);
    {
        b3host::Ps2FloatEnv outer;
        { b3host::Ps2FloatEnv inner; }   // nested: the inner one restores the outer mode, not the default
        const float a = g_a[0], b = g_b[0];
        g_r[4] = a + b;
    }
    in = g_r[4];
    CHECK_BITS(bits(in), 0x3F800000u);
}

TEST(overflow_is_fmax) {   // RZ overflow gives the largest finite float, as the EE's clamp (no Inf)
    g_a[0] = 0x1p127f;
    g_b[0] = 4.0f;
    {
        b3host::Ps2FloatEnv env;
        const float a = g_a[0], b = g_b[0];
        g_r[0] = a * b;
        g_r[1] = -(a * b) - a * b;
    }
    const float m = g_r[0], s = g_r[1];
    CHECK_BITS(bits(m), b3fp::kFmax);
    CHECK_BITS(bits(s), b3fp::kSign | b3fp::kFmax);
}

TEST_MAIN("Ps2FloatEnv: host floats under the EE rounding vs b3fp::ref")
