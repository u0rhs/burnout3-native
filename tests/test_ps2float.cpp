// PS2-faithful float model: a table of edge cases with the expected result and where the expectation comes from.
//   [M] the manuals (research/ext/PS2Docs in the decomp repo): VU User's Manual v6.0, EE Core User's Manual v6.0
//   [S] the model's specification, docs/specs/ps2-float.md, section cited
//   [P] an independent Python model: exact rationals, clamped operands, truncation toward zero
//       (tools/vuref.py in this repo; its printed values are pasted here)
// Built with -DB3_PS2_FLOAT=1. Where the manual and the model disagree (ps2float.h D1-D4) the row says so; the
// model's behaviour is the required one.
#include "b3host/ps2float.h"
#include "test.h"

#include <climits>

using namespace b3fp;
using t::fbits;

static constexpr std::uint32_t ONE = 0x3F800000u, MONE = 0xBF800000u, FMAX = 0x7F7FFFFFu, NFMAX = 0xFF7FFFFFu;

struct Row {
    const char* what;
    std::uint32_t got;
    std::uint32_t expected;
    const char* source;
};

TEST(edge_table) {
    const Row rows[] = {
        // rounding: truncation toward zero, not nearest
        {"add 1 + 3*2^-25", ref::add(ONE, fbits(3.0f * 0x1p-25f)), 0x3F800000u, "[M] VU 2.2 p27 truncation; [P]; host RN 0x3F800001"},
        {"sub -1 - 3*2^-25", ref::sub(MONE, fbits(3.0f * 0x1p-25f)), 0xBF800000u, "[M] toward zero for negatives too; [P]"},
        {"mul 0.1f * 0.1f", ref::mul(fbits(0.1f), fbits(0.1f)), 0x3C23D70Au, "[P]; host RN 0x3C23D70B"},
        {"mul 1.5f * 1.1f", ref::mul(fbits(1.5f), fbits(1.1f)), 0x3FD33333u, "[P]; host RN 0x3FD33334"},
        {"cvt.s.w 16777219", ref::cvt_s_w(16777219), 0x4B800001u, "[M] VU 2.2 fixed->float truncates; [P]; host RN 0x4B800002"},
        // multiply-add is not fused: the product is truncated first
        {"madd -1 + (1+2^-12)^2", ref::madd(MONE, fbits(1.0f + 0x1p-12f), fbits(1.0f + 0x1p-12f)), 0x3A000000u,
         "[S] §4.3 (product chopped, then add); [P]; a fused FMA gives 0x3A000400"},
        // no denormals: exponent-0 operands are zero, denormal results flush to signed zero
        {"add denormal + 0", ref::add(0x00000001u, 0), 0x00000000u, "[M] VU 2.1.1 p26 table (E=0 -> 0)"},
        {"mul 2^-100 * 2^-100", ref::mul(fbits(0x1p-100f), fbits(0x1p-100f)), 0x00000000u, "[M] VU 2.3 p28 underflow -> +0"},
        {"mul -2^-100 * 2^-100", ref::mul(fbits(-0x1p-100f), fbits(0x1p-100f)), 0x80000000u, "[M] VU 2.3 p28 underflow -> -0"},
        // no Inf/NaN; overflow clamps
        {"add exp255 + 0", ref::add(0x7F800000u, 0), FMAX, "[S] §4.1 (D1: [M] VU 2.1.1 treats exp 255 as x2^128)"},
        {"mul Fmax * 2", ref::mul(FMAX, fbits(2.0f)), FMAX, "[M] VU 2.3 overflow -> +MAX; [S] §4.3 0x7F7FFFFF (D2)"},
        {"mul -Fmax * 2", ref::mul(NFMAX, fbits(2.0f)), NFMAX, "[M] VU 2.3 overflow -> -MAX; [S] §4.3"},
        {"add Fmax + Fmax", ref::add(FMAX, FMAX), FMAX, "[M] VU 2.3; [S] §4.3"},
        // VU0 FDIV unit (Q)
        {"vdiv 1 / 0", ref::vu_div(ONE, 0), FMAX, "[M] VU 2.3 zero division -> +MAX"},
        {"vdiv -1 / +0", ref::vu_div(MONE, 0), NFMAX, "[M] VU 2.3 -> -MAX"},
        {"vdiv 0 / 0", ref::vu_div(0, 0), FMAX, "[M] VU 2.3 0/0 -> +MAX"},
        {"vdiv +0 / -0", ref::vu_div(0, 0x80000000u), NFMAX, "[M] VU 2.3 '0 is valid sign'"},
        {"vdiv 1 / 3", ref::vu_div(ONE, fbits(3.0f)), 0x3EAAAAAAu, "[P]; host RN 0x3EAAAAAB"},
        {"vsqrt -4", ref::vu_sqrt(fbits(-4.0f)), 0x40000000u, "[M] VU 2.3 sqrt(x<0) -> sqrt|x|"},
        {"vsqrt 1.5", ref::vu_sqrt(fbits(1.5f)), 0x3F9CC470u, "[S] §4.4; [P]; host RN 0x3F9CC471"},
        {"vrsqrt 1, 2", ref::vu_rsqrt(ONE, fbits(2.0f)), 0x3F3504F3u, "[P] (two truncations as [S] §4.4)"},
        {"vrsqrt 1, 3", ref::vu_rsqrt(ONE, fbits(3.0f)), 0x3F13CD3Au, "[P]"},
        {"vrsqrt 1, 0", ref::vu_rsqrt(ONE, 0), FMAX, "[S] §4.4: x/sqrt(0) -> Fmax"},
        {"vrsqrt 0, 0", ref::vu_rsqrt(0, 0), 0x00000000u, "[S] §4.4: 0/sqrt(0) -> signed zero (unlike vdiv 0/0)"},
        // EE FPU (COP1)
        {"sqrt.s -4", ref::fpu_sqrt(fbits(-4.0f)), 0x40000000u, "[M] EE Core 8.5 p162 sqrt of |x|"},
        {"sqrt.s -0", ref::fpu_sqrt(0x80000000u), 0x80000000u, "[S] §4.4 fpu_sqrt (signed zero)"},
        {"div.s 1 / 0", ref::fpu_div(ONE, 0), FMAX, "[S] §4.4 Fmax (D2: [M] EE Core table 8-5 p163 lists 0x7FFFFFFF)"},
        {"div.s -1 / 0", ref::fpu_div(MONE, 0), NFMAX, "[M] EE Core 8.5 -Fmax; [S] §4.4"},
        {"rsqrt.s 1, -0", ref::fpu_rsqrt(ONE, 0x80000000u), NFMAX, "[S] §4.4 fpu_rsqrt: sign from ft only"},
        {"min.s -0, +0", ref::min(0x80000000u, 0), 0x80000000u, "[M] EE Core table 8-5 p163 Min(-0,+0) = -0"},
        {"max.s -0, +0", ref::max(0x80000000u, 0), 0x00000000u, "[M] EE Core table 8-5 Max(-0,+0) = +0"},
        {"min.s -1, -2", ref::min(MONE, fbits(-2.0f)), fbits(-2.0f), "[S] §4.6 ordering"},
        {"add +0 + -0", ref::add(0, 0x80000000u), 0x00000000u, "[M] EE Core table 8-5 (+0)+(-0) = +0"},
        {"add -0 + -0", ref::add(0x80000000u, 0x80000000u), 0x80000000u, "[M] EE Core table 8-5 (-0)+(-0) = -0"},
        {"sub -0 - +0", ref::sub(0x80000000u, 0), 0x80000000u, "[M] EE Core table 8-5 (-0)-(+0) = -0"},
    };
    for (const Row& r : rows) {
        ++t::checks();
        if (r.got != r.expected) {
            char m[300];
            std::snprintf(m, sizeof m, "%s: 0x%08X, expected 0x%08X (%s)", r.what, r.got, r.expected, r.source);
            t::fail(__FILE__, __LINE__, m);
        }
    }
}

TEST(cvt_w_s_truncates_and_saturates) {
    CHECK(ref::cvt_w_s(fbits(-2.7f)) == -2);                       // [M] EE Core: truncation
    CHECK(ref::cvt_w_s(fbits(3e9f)) == INT_MAX);                   // [M] conversion overflow -> max
    CHECK(ref::cvt_w_s(fbits(-3e9f)) == INT_MIN);
    CHECK(ref::cvt_w_s(0x7F800000u) == INT_MAX);                   // exp 255
    CHECK(ref::cvt_w_s(0x00000001u) == 0);                         // denormal
}

TEST(compare_clamps_operands) {
    CHECK(!ref::lt(0, 0x00000001u));          // denormal is zero: 0 < denormal is false  [B compare_s]
    CHECK(ref::eq(0x80000000u, 0x00000001u)); // -0 == denormal
    CHECK(ref::eq(0x7F800000u, FMAX));        // exp 255 == Fmax (D1)
}

TEST(vector_ops_keep_unmasked_lanes) {
    const u_long128 d = b3host::make_u128(1, 2, 3, 4);
    const u_long128 a = b3vu::make(1.0f, 2.0f, 3.0f, 4.0f);
    const u_long128 r = b3vu::add<b3vu::Y | b3vu::W>(d, a, a);
    CHECK(r.w[0] == 1 && r.w[2] == 3);
    CHECK_BITS(r.w[1], fbits(4.0f));
    CHECK_BITS(r.w[3], fbits(8.0f));
}

TEST(outer_product_is_cross_product) {
    const u_long128 a = b3vu::make(1.0f, 2.0f, 3.0f, 9.0f), b = b3vu::make(4.0f, 5.0f, 6.0f, 7.0f);
    const u_long128 acc = b3vu::opmula(b, a, b);
    const u_long128 r = b3vu::opmsub(b, acc, b, a);
    CHECK(b3vu::lane(r, 0) == -3.0f && b3vu::lane(r, 1) == 6.0f && b3vu::lane(r, 2) == -3.0f);
    CHECK(b3vu::lane(r, 3) == 7.0f);   // W keeps the destination [B execute_vu0_macro_outer_product_*]
}

// ---- one row per bullet of docs/specs/ps2-float.md §4 (expected values by hand, checked with tools/vuref.py's
// exact-rational model; the D3 rows are the spec's two-step rule, where the exact model differs on purpose) ----
static_assert(ref::clamp(0x7F800001u) == 0x7F7FFFFFu && ref::clamp(0x807FFFFFu) == 0x80000000u, "clamp is constexpr");
static_assert(ref::min(0x80000000u, 0u) == 0x80000000u && ref::max(0x80000000u, 0u) == 0u, "min / max are constexpr");

static void rows(const char* file, int line, std::initializer_list<Row> list) {
    for (const Row& r : list) {
        ++t::checks();
        if (r.got != r.expected) {
            char m[300];
            std::snprintf(m, sizeof m, "%s: 0x%08X, expected 0x%08X (%s)", r.what, r.got, r.expected, r.source);
            t::fail(file, line, m);
        }
    }
}
static std::uint32_t u(std::int32_t i) { return static_cast<std::uint32_t>(i); }

TEST(spec_4_1_clamp) {
    rows(__FILE__, __LINE__, {
        {"clamp +denormal", ref::clamp(0x00000001u), 0x00000000u, "§4.1 E=0 -> +0"},
        {"clamp -denormal", ref::clamp(0x807FFFFFu), 0x80000000u, "§4.1 E=0 -> -0"},
        {"clamp -0", ref::clamp(0x80000000u), 0x80000000u, "§4.1"},
        {"clamp +Inf pattern", ref::clamp(0x7F800000u), FMAX, "§4.1 D1"},
        {"clamp NaN pattern", ref::clamp(0x7FC00000u), FMAX, "§4.1 D1 fraction discarded"},
        {"clamp -exp255 max", ref::clamp(0xFFFFFFFFu), NFMAX, "§4.1 D1"},
        {"clamp FLT_MIN", ref::clamp(0x00800000u), 0x00800000u, "§4.1 unchanged"},
        {"clamp -Fmax", ref::clamp(NFMAX), NFMAX, "§4.1 unchanged"},
        {"clamp 1", ref::clamp(ONE), ONE, "§4.1 unchanged"},
    });
}

TEST(spec_4_2_chop) {
    rows(__FILE__, __LINE__, {
        {"chop +0", ref::chop(0.0), 0x00000000u, "§4.2"},
        {"chop -0", ref::chop(-0.0), 0x80000000u, "§4.2 sign of zero kept"},
        {"chop 0.1", ref::chop(0.1), 0x3DCCCCCCu, "§4.2 RZ; [P]; host RN 0x3DCCCCCD"},
        {"chop 1+2^-24", ref::chop(1.0 + 0x1p-24), ONE, "§4.2"},
        {"chop 1+2^-23-2^-52", ref::chop(1.0 + 0x1p-23 - 0x1p-52), ONE, "§4.2 just below a step"},
        {"chop 1+2^-23", ref::chop(1.0 + 0x1p-23), 0x3F800001u, "§4.2 exactly on a step"},
        {"chop -(2-2^-52)", ref::chop(-(2.0 - 0x1p-52)), 0xBFFFFFFFu, "§4.2 toward zero for negatives"},
        {"chop FLT_MAX", ref::chop(static_cast<double>(FLT_MAX)), FMAX, "§4.2"},
        {"chop FLT_MAX succ", ref::chop(std::nextafter(static_cast<double>(FLT_MAX), 1e300)), FMAX, "§4.2 RZ overflow"},
        {"chop 2^128-ulp", ref::chop(0x1p128 - 0x1p75), FMAX, "§4.2"},
        {"chop 2^128", ref::chop(0x1p128), FMAX, "§4.2 overflow"},
        {"chop -2^128", ref::chop(-0x1p128), NFMAX, "§4.2 overflow"},
        {"chop -1e300", ref::chop(-1e300), NFMAX, "§4.2 overflow"},
        {"chop +Inf", ref::chop(std::numeric_limits<double>::infinity()), FMAX, "§4.2 r > FLT_MAX"},
        {"chop 2^-126", ref::chop(0x1p-126), 0x00800000u, "§4.2 smallest normal kept"},
        {"chop -2^-126", ref::chop(-0x1p-126), 0x80800000u, "§4.2"},
        {"chop 2^-126 pred", ref::chop(std::nextafter(0x1p-126, 0.0)), 0x00000000u, "§4.2 flush to zero"},
        {"chop -2^-126 pred", ref::chop(-std::nextafter(0x1p-126, 0.0)), 0x80000000u, "§4.2 flush to signed zero"},
        {"chop 2^-127", ref::chop(0x1p-127), 0x00000000u, "§4.2 binary32 denormal value -> 0"},
        {"chop (2-2^-30)*2^-126", ref::chop((2.0 - 0x1p-30) * 0x1p-126), 0x00FFFFFFu, "§4.2"},
        {"chop double denormal", ref::chop(std::numeric_limits<double>::denorm_min()), 0x00000000u, "§4.2"},
        {"chop -double denormal", ref::chop(-std::numeric_limits<double>::denorm_min()), 0x80000000u, "§4.2"},
        {"chop 2^-1000", ref::chop(-0x1p-1000), 0x80000000u, "§4.2"},
    });
}

TEST(spec_4_3_add_sub) {
    rows(__FILE__, __LINE__, {
        {"add 1 - 2^-30", ref::add(ONE, fbits(-0x1p-30f)), 0x3F7FFFFFu, "§4.3 exact sum truncated; [P]"},
        {"add 1 - 2^-60", ref::add(ONE, fbits(-0x1p-60f)), ONE, "§4.3 D3: binary64 rounds to 1 first ([P] exact RZ 0x3F7FFFFF)"},
        {"add (1+2^-23) - 2^-53", ref::add(0x3F800001u, fbits(-0x1p-53f)), 0x3F800001u,
         "§4.3 D3: binary64 tie to even back up ([P] exact RZ 0x3F800000)"},
        {"add 1 + 2^-53", ref::add(ONE, fbits(0x1p-53f)), ONE, "§4.3"},
        {"add 1 + -1", ref::add(ONE, MONE), 0x00000000u, "§4.3 x + (-x) = +0"},
        {"sub -1 - -1", ref::sub(MONE, MONE), 0x00000000u, "§4.3 x - x = +0"},
        {"sub +0 - +0", ref::sub(0, 0), 0x00000000u, "§4.3"},
        {"sub +0 - -0", ref::sub(0, 0x80000000u), 0x00000000u, "§4.3"},
        {"sub -0 - -0", ref::sub(0x80000000u, 0x80000000u), 0x00000000u, "§4.3 RN64 sign"},
        {"add -den + -0", ref::add(0x80000001u, 0x80000000u), 0x80000000u, "§4.3 a negative denormal is -0"},
        {"add -den + +den", ref::add(0x80000001u, 0x00000001u), 0x00000000u, "§4.3 (-0)+(+0) = +0"},
        {"add -den + 1", ref::add(0x807FFFFFu, ONE), ONE, "§4.3"},
        {"add -Fmax + -Fmax", ref::add(NFMAX, NFMAX), NFMAX, "§4.3 overflow"},
        {"add exp255 + exp255", ref::add(0x7F800000u, 0x7FFFFFFFu), FMAX, "§4.3 D1 overflow"},
        {"add -exp255 + exp255", ref::add(0xFF800000u, 0x7F800000u), 0x00000000u, "§4.3 D1: -Fmax + Fmax = +0"},
        {"sub exp255 - -Fmax", ref::sub(0x7FFFFFFFu, NFMAX), FMAX, "§4.3"},
        {"sub (2^-126+2^-149) - 2^-126", ref::sub(0x00800001u, 0x00800000u), 0x00000000u, "§4.3 underflow -> +0"},
        {"sub 2^-126 - (2^-126+2^-149)", ref::sub(0x00800000u, 0x00800001u), 0x80000000u, "§4.3 underflow -> -0"},
        {"add 1.5 + 2^-126", ref::add(fbits(1.5f), 0x00800000u), fbits(1.5f), "§4.3"},
        {"sub 2^-126*3 - 2^-126", ref::sub(fbits(3 * 0x1p-126f), 0x00800000u), 0x01000000u, "§4.3"},
    });
}

TEST(spec_4_3_mul_madd) {
    const std::uint32_t a = fbits(1.0f + 0x1p-12f);
    rows(__FILE__, __LINE__, {
        {"mul -0.1 * 0.1", ref::mul(fbits(-0.1f), fbits(0.1f)), 0xBC23D70Au, "§4.3 truncation; [P]"},
        {"mul 0 * -Fmax", ref::mul(0, NFMAX), 0x80000000u, "§4.3 XOR-signed zero"},
        {"mul -0 * -0", ref::mul(0x80000000u, 0x80000000u), 0x00000000u, "§4.3"},
        {"mul den * -1", ref::mul(0x00000005u, MONE), 0x80000000u, "§4.3 denormal operand is 0"},
        {"mul exp255 * 0", ref::mul(0x7F800000u, 0), 0x00000000u, "§4.3 D1: Fmax * 0 = 0, no NaN"},
        {"mul -exp255 * 0", ref::mul(0xFF800000u, 0), 0x80000000u, "§4.3 D1"},
        {"mul -exp255 * -exp255", ref::mul(0xFFFFFFFFu, 0xFF800000u), FMAX, "§4.3 overflow"},
        {"mul 2^-63 * 2^-63", ref::mul(fbits(0x1p-63f), fbits(0x1p-63f)), 0x00800000u, "§4.3 exactly 2^-126 kept"},
        {"mul 2^-63 * pred(2^-63)", ref::mul(fbits(0x1p-63f), fbits(0x1.fffffep-64f)), 0x00000000u, "§4.3 below 2^-126 -> 0"},
        {"mul -2^-63 * pred(2^-63)", ref::mul(fbits(-0x1p-63f), fbits(0x1.fffffep-64f)), 0x80000000u, "§4.3"},
        {"mul 2^64 * 2^64", ref::mul(fbits(0x1p64f), fbits(0x1p64f)), FMAX, "§4.3 >= 2^128 -> Fmax"},
        {"mul 2^64 * pred(2^64)", ref::mul(fbits(0x1p64f), fbits(0x1.fffffep63f)), FMAX, "§4.3 exactly FLT_MAX; [P]"},
        {"mul -2^64 * 2^64", ref::mul(fbits(-0x1p64f), fbits(0x1p64f)), NFMAX, "§4.3"},
        {"msub 1 - a*a", ref::msub(ONE, a, a), 0xBA000000u, "§4.3 not fused; [P]"},
        {"madd -0 + -0*1", ref::madd(0x80000000u, 0x80000000u, ONE), 0x80000000u, "§4.3 (-0)+(-0)"},
        {"madd +0 + -0*1", ref::madd(0, 0x80000000u, ONE), 0x00000000u, "§4.3"},
        {"msub -0 - +0*1", ref::msub(0x80000000u, 0, ONE), 0x80000000u, "§4.3 (-0)-(+0)"},
        {"madd Fmax + Fmax*2", ref::madd(FMAX, FMAX, fbits(2.0f)), FMAX, "§4.3 product clamped, sum clamped"},
        {"msub -Fmax - Fmax*2", ref::msub(NFMAX, FMAX, fbits(2.0f)), NFMAX, "§4.3"},
        {"madd exp255 + -exp255*1", ref::madd(0x7F800000u, 0xFF800000u, ONE), 0x00000000u, "§4.3 D1"},
    });
}

TEST(spec_4_4_vu_unit) {
    rows(__FILE__, __LINE__, {
        {"vdiv den / den", ref::vu_div(0x00000001u, 0x00000001u), FMAX, "§4.4 0/0 -> +Fmax"},
        {"vdiv 1 / -den", ref::vu_div(ONE, 0x80000001u), NFMAX, "§4.4 signs XOR"},
        {"vdiv -0 / +den", ref::vu_div(0x80000000u, 0x00000003u), NFMAX, "§4.4"},
        {"vdiv -1 / -0", ref::vu_div(MONE, 0x80000000u), FMAX, "§4.4"},
        {"vdiv 1 / exp255", ref::vu_div(ONE, 0x7F800000u), 0x00000000u, "§4.4 1/Fmax < 2^-126 -> +0"},
        {"vdiv 0 / 1", ref::vu_div(0, ONE), 0x00000000u, "§4.4"},
        {"vdiv -0 / 1", ref::vu_div(0x80000000u, ONE), 0x80000000u, "§4.4"},
        {"vdiv 0 / -1", ref::vu_div(0, MONE), 0x80000000u, "§4.4"},
        {"vdiv -1 / 3", ref::vu_div(MONE, fbits(3.0f)), 0xBEAAAAAAu, "§4.4; [P]"},
        {"vdiv 1 / 10", ref::vu_div(ONE, fbits(10.0f)), 0x3DCCCCCCu, "§4.4; [P]; host RN 0x3DCCCCCD"},
        {"vdiv Fmax / 0.5", ref::vu_div(FMAX, fbits(0.5f)), FMAX, "§4.4 overflow"},
        {"vdiv -Fmax / 0.5", ref::vu_div(NFMAX, fbits(0.5f)), NFMAX, "§4.4 overflow"},
        {"vdiv 2^-126 / 2", ref::vu_div(0x00800000u, fbits(2.0f)), 0x00000000u, "§4.4 underflow"},
        {"vdiv exp255 / exp255", ref::vu_div(0x7F800000u, 0x7FFFFFFFu), ONE, "§4.4 D1"},
        {"vsqrt -0", ref::vu_sqrt(0x80000000u), 0x00000000u, "§4.4 a zero gives +0"},
        {"vsqrt -den", ref::vu_sqrt(0x80000001u), 0x00000000u, "§4.4"},
        {"vsqrt 4", ref::vu_sqrt(fbits(4.0f)), 0x40000000u, "§4.4"},
        {"vsqrt 2", ref::vu_sqrt(fbits(2.0f)), 0x3FB504F3u, "§4.4; [P]"},
        {"vsqrt exp255", ref::vu_sqrt(0x7FFFFFFFu), 0x5F7FFFFFu, "§4.4 sqrt(Fmax); [P]"},
        {"vsqrt -exp255", ref::vu_sqrt(0xFF800000u), 0x5F7FFFFFu, "§4.4"},
        {"vrsqrt -1, 0", ref::vu_rsqrt(MONE, 0), NFMAX, "§4.4 x/sqrt(0): sign XOR"},
        {"vrsqrt 1, -0", ref::vu_rsqrt(ONE, 0x80000000u), NFMAX, "§4.4"},
        {"vrsqrt -1, -den", ref::vu_rsqrt(MONE, 0x80000001u), FMAX, "§4.4"},
        {"vrsqrt -0, +0", ref::vu_rsqrt(0x80000000u, 0), 0x80000000u, "§4.4 0/sqrt(0) = signed 0"},
        {"vrsqrt +den, -0", ref::vu_rsqrt(0x00000001u, 0x80000000u), 0x80000000u, "§4.4 denormal n is 0"},
        {"vrsqrt 0, +den", ref::vu_rsqrt(0, 0x00000007u), 0x00000000u, "§4.4"},
        {"vrsqrt 1, -4", ref::vu_rsqrt(ONE, fbits(-4.0f)), 0x3F000000u, "§4.4 sqrt of |r|: sign from n"},
        {"vrsqrt -1, 4", ref::vu_rsqrt(MONE, fbits(4.0f)), 0xBF000000u, "§4.4"},
        {"vrsqrt 4, 0.25", ref::vu_rsqrt(fbits(4.0f), fbits(0.25f)), fbits(8.0f), "§4.4"},
        {"vrsqrt Fmax, 2^-126", ref::vu_rsqrt(FMAX, 0x00800000u), FMAX, "§4.4 overflow"},
        {"vrsqrt 0, 1", ref::vu_rsqrt(0, ONE), 0x00000000u, "§4.4"},
        {"vrsqrt -0, 1", ref::vu_rsqrt(0x80000000u, ONE), 0x80000000u, "§4.4"},
    });
}

TEST(spec_4_4_fpu) {
    rows(__FILE__, __LINE__, {
        {"div.s 1 / +den", ref::fpu_div(ONE, 0x00000001u), FMAX, "§4.4 raw exponent 0"},
        {"div.s den / den", ref::fpu_div(0x00000001u, 0x00000001u), FMAX, "§4.4"},
        {"div.s 0 / -den", ref::fpu_div(0, 0x80000001u), NFMAX, "§4.4 signs XOR"},
        {"div.s -1 / -0", ref::fpu_div(MONE, 0x80000000u), FMAX, "§4.4"},
        {"div.s Fmax / 0.5", ref::fpu_div(FMAX, fbits(0.5f)), FMAX, "§4.4 Inf -> Fmax"},
        {"div.s -Fmax / 0.5", ref::fpu_div(NFMAX, fbits(0.5f)), NFMAX, "§4.4"},
        {"div.s 2^-126 / 4", ref::fpu_div(0x00800000u, fbits(4.0f)), 0x00000000u, "§4.4 denormal quotient -> 0"},
        {"div.s -2^-126 / 4", ref::fpu_div(0x80800000u, fbits(4.0f)), 0x80000000u, "§4.4 -> -0"},
        {"div.s exp255 / Fmax", ref::fpu_div(0x7F800000u, FMAX), ONE, "§4.4 D1"},
        {"div.s den / 1", ref::fpu_div(0x00000001u, ONE), 0x00000000u, "§4.4 n clamped"},
        {"div.s -0 / 1", ref::fpu_div(0x80000000u, ONE), 0x80000000u, "§4.4"},
        {"div.s 1 / exp255", ref::fpu_div(ONE, 0xFF800000u), 0x80000000u, "§4.4 1/-Fmax is a denormal -> -0"},
        {"sqrt.s +0", ref::fpu_sqrt(0), 0x00000000u, "§4.4"},
        {"sqrt.s -den", ref::fpu_sqrt(0x80000001u), 0x80000000u, "§4.4 raw exponent 0 keeps sign"},
        {"sqrt.s +den", ref::fpu_sqrt(0x00000001u), 0x00000000u, "§4.4"},
        {"sqrt.s 4", ref::fpu_sqrt(fbits(4.0f)), 0x40000000u, "§4.4"},
        {"sqrt.s -exp255", ref::fpu_sqrt(0xFF800000u), 0x5F7FFFFFu, "§4.4 sqrt(Fmax); [P]"},
        {"rsqrt.s 0, 0", ref::fpu_rsqrt(0, 0), FMAX, "§4.4 n not looked at"},
        {"rsqrt.s -1, +0", ref::fpu_rsqrt(MONE, 0), FMAX, "§4.4 sign of r alone"},
        {"rsqrt.s -1, -den", ref::fpu_rsqrt(MONE, 0x80000005u), NFMAX, "§4.4"},
        {"rsqrt.s 1, 4", ref::fpu_rsqrt(ONE, fbits(4.0f)), 0x3F000000u, "§4.4"},
        {"rsqrt.s 1, -4", ref::fpu_rsqrt(ONE, fbits(-4.0f)), 0x3F000000u, "§4.4 sqrt of |r|"},
        {"rsqrt.s -1, 4", ref::fpu_rsqrt(MONE, fbits(4.0f)), 0xBF000000u, "§4.4"},
        {"rsqrt.s Fmax, 2^-126", ref::fpu_rsqrt(FMAX, 0x00800000u), FMAX, "§4.4 Inf -> Fmax"},
        {"rsqrt.s 2^-126, Fmax", ref::fpu_rsqrt(0x00800000u, FMAX), 0x00000000u, "§4.4 tiny -> 0"},
        {"rsqrt.s den, 1", ref::fpu_rsqrt(0x00000003u, ONE), 0x00000000u, "§4.4 n clamped"},
#if B3_PS2_FPU_DIVSQRT_CHOP
        {"div.s 1 / 3 (chop)", ref::fpu_div(ONE, fbits(3.0f)), 0x3EAAAAAAu, "§4.4 B3_PS2_FPU_DIVSQRT_CHOP=1; [P]"},
        {"sqrt.s 1.5 (chop)", ref::fpu_sqrt(fbits(1.5f)), 0x3F9CC470u, "§4.4 chop; [P]"},
        {"div.s (2-2^-23)*2^-126 / 2 (chop)", ref::fpu_div(0x00FFFFFFu, fbits(2.0f)), 0x00000000u, "§4.4 chop"},
#else
        {"div.s 1 / 3", ref::fpu_div(ONE, fbits(3.0f)), 0x3EAAAAABu, "§4.4 D4 RN32; [P]"},
        {"div.s 1 / 10", ref::fpu_div(ONE, fbits(10.0f)), 0x3DCCCCCDu, "§4.4 D4 RN32; [P]"},
        {"sqrt.s 1.5", ref::fpu_sqrt(fbits(1.5f)), 0x3F9CC471u, "§4.4 D4 RN32 (vsqrt gives 0x3F9CC470); [P]"},
        {"rsqrt.s 1, 2", ref::fpu_rsqrt(ONE, fbits(2.0f)), 0x3F3504F3u, "§4.4 D4 two RN32 steps; [P]"},
        {"rsqrt.s 1, 3", ref::fpu_rsqrt(ONE, fbits(3.0f)), 0x3F13CD3Au, "§4.4 D4; [P]"},
        {"div.s (2-2^-23)*2^-126 / 2", ref::fpu_div(0x00FFFFFFu, fbits(2.0f)), 0x00800000u,
         "§4.4 D4: RN32 rounds the tie up to 2^-126, which clamp keeps; [P]"},
#endif
    });
}

TEST(spec_4_5_conversions) {
    rows(__FILE__, __LINE__, {
        {"cvt.s.w 0", ref::cvt_s_w(0), 0x00000000u, "§4.5"},
        {"cvt.s.w 1", ref::cvt_s_w(1), ONE, "§4.5"},
        {"cvt.s.w -1", ref::cvt_s_w(-1), MONE, "§4.5"},
        {"cvt.s.w 16777217", ref::cvt_s_w(16777217), 0x4B800000u, "§4.5 truncated"},
        {"cvt.s.w -16777219", ref::cvt_s_w(-16777219), 0xCB800001u, "§4.5 toward zero; [P]"},
        {"cvt.s.w INT_MAX", ref::cvt_s_w(INT_MAX), 0x4EFFFFFFu, "§4.5 2^31-128; [P]; host RN 0x4F000000"},
        {"cvt.s.w INT_MIN", ref::cvt_s_w(INT_MIN), 0xCF000000u, "§4.5 exact"},
        {"cvt.w.s 2^31-128", u(ref::cvt_w_s(0x4EFFFFFFu)), u(2147483520), "§4.5 raw exponent 157"},
        {"cvt.w.s -(2^31-128)", u(ref::cvt_w_s(0xCEFFFFFFu)), u(-2147483520), "§4.5"},
        {"cvt.w.s 2^31", u(ref::cvt_w_s(0x4F000000u)), u(INT_MAX), "§4.5 overflow"},
        {"cvt.w.s -2^31", u(ref::cvt_w_s(0xCF000000u)), u(INT_MIN), "§4.5 overflow"},
        {"cvt.w.s -exp255", u(ref::cvt_w_s(0xFFFFFFFFu)), u(INT_MIN), "§4.5"},
        {"cvt.w.s exp255", u(ref::cvt_w_s(0x7FFFFFFFu)), u(INT_MAX), "§4.5"},
        {"cvt.w.s -den", u(ref::cvt_w_s(0x80000001u)), 0u, "§4.5"},
        {"cvt.w.s -0", u(ref::cvt_w_s(0x80000000u)), 0u, "§4.5"},
        {"cvt.w.s 0.999", u(ref::cvt_w_s(fbits(0.999f))), 0u, "§4.5 truncation"},
        {"cvt.w.s -0.999", u(ref::cvt_w_s(fbits(-0.999f))), 0u, "§4.5 truncation"},
        {"cvt.w.s 1.5", u(ref::cvt_w_s(fbits(1.5f))), 1u, "§4.5"},
        {"cvt.w.s -1.5", u(ref::cvt_w_s(fbits(-1.5f))), u(-1), "§4.5"},
        {"cvt.w.s 16777217.0", u(ref::cvt_w_s(0x4B800001u)), 16777218u, "§4.5 exact integer"},
    });
}

TEST(spec_4_6_compares_min_max) {
    CHECK(!ref::lt(0x80000000u, 0) && ref::le(0x80000000u, 0) && ref::eq(0, 0x80000000u));   // -0 == +0
    CHECK(!ref::lt(FMAX, 0x7F800000u) && ref::le(FMAX, 0x7F800000u));                         // exp 255 == Fmax
    CHECK(!ref::lt(0xFF800000u, NFMAX) && ref::eq(0xFFFFFFFFu, NFMAX));
    CHECK(ref::lt(MONE, ONE) && !ref::lt(ONE, MONE) && !ref::le(ONE, MONE));
    CHECK(ref::lt(0, 0x00800000u) && ref::lt(0x80800000u, 0x80000001u));
    CHECK(ref::eq(ONE, ONE) && !ref::eq(ONE, MONE) && ref::le(ONE, ONE) && !ref::lt(ONE, ONE));
    CHECK(ref::lt(0x7F7FFFFEu, 0x7FC00000u));                                                 // NaN pattern is Fmax
    rows(__FILE__, __LINE__, {
        {"min +den, +0", ref::min(0x00000001u, 0), 0x00000000u, "§4.6 a +denormal is above +0"},
        {"max +den, +0", ref::max(0, 0x00000001u), 0x00000001u, "§4.6"},
        {"max 0x7FFFFFFF, Fmax", ref::max(0x7FFFFFFFu, FMAX), 0x7FFFFFFFu, "§4.6 raw, no clamp"},
        {"min 0x7F800000, 0x7FC00000", ref::min(0x7FC00000u, 0x7F800000u), 0x7F800000u, "§4.6"},
        {"min 0xFFFFFFFF, -Fmax", ref::min(NFMAX, 0xFFFFFFFFu), 0xFFFFFFFFu, "§4.6 the lowest encoding"},
        {"max 0xFFFFFFFF, -0", ref::max(0xFFFFFFFFu, 0x80000000u), 0x80000000u, "§4.6"},
        {"min -den, -0", ref::min(0x80000000u, 0x80000001u), 0x80000001u, "§4.6 -den below -0"},
        {"max -0, -den", ref::max(0x80000001u, 0x80000000u), 0x80000000u, "§4.6"},
        {"max -1, +0", ref::max(MONE, 0), 0x00000000u, "§4.6"},
        {"min 1, 1", ref::min(ONE, ONE), ONE, "§4.6"},
        {"max -1, -2", ref::max(fbits(-2.0f), MONE), MONE, "§4.6"},
        {"min -0, +den", ref::min(0x00000001u, 0x80000000u), 0x80000000u, "§4.6"},
    });
}

TEST(neg_abs_flip_and_clear_bit_31) {
    CHECK_BITS(fbits(b3fp::neg(0.0f)), 0x80000000u);
    CHECK_BITS(fbits(b3fp::neg(-0.0f)), 0x00000000u);
    CHECK_BITS(fbits(b3fp::neg(1.0f)), MONE);
    CHECK_BITS(fbits(b3fp::neg(t::bitsf(0x7F800000u))), 0xFF800000u);
    CHECK_BITS(fbits(b3fp::neg(t::bitsf(0x00000001u))), 0x80000001u);
    CHECK_BITS(fbits(b3fp::abs(-0.0f)), 0x00000000u);
    CHECK_BITS(fbits(b3fp::abs(-2.5f)), fbits(2.5f));
    CHECK_BITS(fbits(b3fp::abs(t::bitsf(0xFFC00001u))), 0x7FC00001u);
    CHECK_BITS(fbits(b3fp::abs(t::bitsf(0x80000001u))), 0x00000001u);
}

// port15: the rounding switch. Default Chop (= ref::*, ps2-float.md §4.3); Nearest rounds the
// exact result to nearest with the same clamps.
TEST(rounding_switch_default_is_chop) {
    CHECK(rounding().vu0 == Rounding::Chop && rounding().cop1 == Rounding::Chop);
    CHECK_BITS(fbits(b3fp::add(1.0f, 3.0f * 0x1p-25f)), 0x3F800000u);
    const u_long128 a = b3vu::make(1.0f, 0.1f, 0, 0), b = b3vu::make(3.0f * 0x1p-25f, 0.1f, 0, 0);
    CHECK_BITS(b3vu::add<b3vu::X>(a, a, b).w[0], 0x3F800000u);
    CHECK_BITS(b3vu::mul<b3vu::Y>(a, a, b).w[1], 0x3C23D70Au);
}

TEST(rounding_switch_nearest) {
    const RoundingModel saved = rounding();
    set_rounding({Rounding::Nearest, Rounding::Nearest});
    CHECK_BITS(fbits(b3fp::add(1.0f, 3.0f * 0x1p-25f)), 0x3F800001u);           // host RN
    CHECK_BITS(fbits(b3fp::mul(0.1f, 0.1f)), 0x3C23D70Bu);                       // host RN
    const u_long128 a = b3vu::make(1.0f, 0.1f, -1.0f, 1.0f), b = b3vu::make(3.0f * 0x1p-25f, 0.1f, -3.0f * 0x1p-25f, 1.0f);
    const u_long128 r = b3vu::add<b3vu::X | b3vu::Z>(a, a, b);
    CHECK_BITS(r.w[0], 0x3F800001u);
    CHECK_BITS(r.w[2], 0xBF800001u);
    CHECK_BITS(r.w[3], fbits(1.0f));                                             // unmasked lane kept
    CHECK_BITS(b3vu::mul<b3vu::Y>(a, a, b).w[1], 0x3C23D70Bu);
    CHECK_BITS(ref::add_rn(FMAX, FMAX), FMAX);                                   // overflow -> Fmax
    CHECK_BITS(ref::mul_rn(0x00800000u, 0x3F000000u), 0u);                       // denormal result -> 0
    CHECK_BITS(ref::mul_rn(0x00000001u, MONE), 0x80000000u);                     // denormal operand -> signed 0
    CHECK_BITS(ref::add_rn(0x7F800000u, 0), FMAX);                               // exp 255 operand clamped (D1)
    CHECK_BITS(ref::madd_rn(MONE, fbits(1.0f + 0x1p-12f), fbits(1.0f + 0x1p-12f)), ref::madd(MONE, fbits(1.0f + 0x1p-12f), fbits(1.0f + 0x1p-12f)));
    set_rounding({Rounding::Chop, Rounding::Nearest});                           // one unit at a time
    CHECK_BITS(b3vu::add<b3vu::X>(a, a, b).w[0], 0x3F800000u);
    CHECK_BITS(fbits(b3fp::add(1.0f, 3.0f * 0x1p-25f)), 0x3F800001u);
    set_rounding(saved);
}

TEST_MAIN("ps2float (PS2-faithful model)")
