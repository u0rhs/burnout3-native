// The MWCC EE calling convention in b3host/call.h, checked against the evidence in docs/abi-mwcc-ee.md.
// Each test names the retail function (address) or compile probe (tools/abi_probe/probe*.dis.txt) whose register and
// stack use it reproduces. Both directions are covered: call_guest (native -> guest: the image a "guest function" in
// the null backend sees) and native_entry (guest -> native: an image built by hand the way the retail caller builds
// it).
#include "b3host/call.h"
#include "b3host/null_backend.h"
#include "test.h"

#include <cstddef>

using namespace b3host;
using detail::Plan;
using detail::Where;

// Layouts as the decomp declares them (sizes [C] from retail use; field names are placeholders).
struct V3 { float x, y, z, w; };                               // CGtV3d: 16 bytes
struct alignas(16) V3a { float x, y, z, w; };                  // the same, 16-aligned like the decomp's u_long128 member
struct Cell { int field_0; int field_4; };                     // CB3OnScreenKeyboardCell: 8 bytes, two ints
struct Rgba { unsigned char r, g, b, a; };                     // RwRGBA: 4 bytes
struct LoneF { float v; };                                     // probe1:F1
struct I3 { int a, b, c; };                                    // probe1:I3, 12 bytes
struct V2 { float x, y; };                                     // CGtV2d: 8 bytes
template <> struct b3host::abi::lone_float<Rgba> : std::false_type {};
template <> struct b3host::abi::lone_float<LoneF> : std::true_type {};
enum EHUDAlign_ : int { kAlign0 };   // fixed underlying type: values past the enumerators are valid

static std::uint64_t r64(const u_long128& q) { return detail::low64(q); }

// ---- compile-time layouts (no backend needed) ----
// probe2:t_mix: (int, float) x 10 -> a0-t3 / f12-f19, then a9 +0, f9 +8, a10 +16, f10 +24.
using MixPlan = Plan<false, 0, int, float, int, float, int, float, int, float, int, float, int, float, int, float,
                     int, float, int, float, int, float>;
static_assert(MixPlan::layout.slots[16].where == Where::Stack && MixPlan::layout.slots[16].offset == 0);
static_assert(MixPlan::layout.slots[17].where == Where::Stack && MixPlan::layout.slots[17].offset == 8);
static_assert(MixPlan::layout.slots[18].offset == 16 && MixPlan::layout.slots[19].offset == 24);
static_assert(MixPlan::layout.slots[15].where == Where::Fpr && MixPlan::layout.slots[15].index == 7);
static_assert(MixPlan::layout.stack_bytes == 32 && MixPlan::layout.frame_bytes == 32);
// probe2:t_q10: 9 ints, u_long128, int -> +0, +16 (16-aligned, +8 skipped), +32.
using Q10Plan = Plan<false, 0, int, int, int, int, int, int, int, int, int, u_long128, int>;
static_assert(Q10Plan::layout.slots[8].offset == 0 && Q10Plan::layout.slots[9].offset == 16 &&
              Q10Plan::layout.slots[10].offset == 32);
// probe2:t_9f1i: 9 floats then an int: the int is still a0, float 9 at +0.
using F9Plan = Plan<false, 0, float, float, float, float, float, float, float, float, float, int>;
static_assert(F9Plan::layout.slots[8].where == Where::Stack && F9Plan::layout.slots[8].offset == 0);
static_assert(F9Plan::layout.slots[9].where == Where::Gpr && F9Plan::layout.slots[9].index == 0);
// GetAlignPos__6CB3HUDF9EHUDAlign @ 0x1A02C0: CGtV2d result -> a0 hidden, this a1, the enum a2.
using AlignPlan = Plan<true, 8, gp<void>, EHUDAlign_>;
static_assert(AlignPlan::layout.slots[0].index == 1 && AlignPlan::layout.slots[1].index == 2);
// DrawPositionMarker__15CB3RacePosition... @ 0x1531C0: (this, V3d, const u16*, int, Car*, Car*, float, bool, bool,
// bool, float): ints a0-t3 then the third bool on the stack; floats f12, f13.
using DpmPlan = Plan<false, 0, gp<void>, V3, gp<const unsigned short>, int, gp<void>, gp<void>, float, bool, bool,
                     bool, float>;
static_assert(DpmPlan::layout.slots[1].kind.indirect && DpmPlan::layout.slots[1].index == 1);
static_assert(DpmPlan::layout.slots[7].index == 6 && DpmPlan::layout.slots[8].index == 7);
static_assert(DpmPlan::layout.slots[9].where == Where::Stack && DpmPlan::layout.slots[9].offset == 0);
static_assert(DpmPlan::layout.slots[6].where == Where::Fpr && DpmPlan::layout.slots[6].index == 0);
static_assert(DpmPlan::layout.slots[10].where == Where::Fpr && DpmPlan::layout.slots[10].index == 1);
// probe2:t_ff_mixed (int, {float}, float) -> a0, f12, f13; RwRGBA -> a GPR.
using LonePlan = Plan<false, 0, int, LoneF, float, Rgba>;
static_assert(LonePlan::layout.slots[1].where == Where::Fpr && LonePlan::layout.slots[1].index == 0);
static_assert(LonePlan::layout.slots[2].index == 1 && LonePlan::layout.slots[3].where == Where::Gpr &&
              LonePlan::layout.slots[3].index == 1);

// ---- native -> guest ----
TEST(stack_arguments_interleave_in_declaration_order) {   // probe2:t_mix / c_mix
    NullBackend be;
    BackendScope s(be);
    const gaddr sp0 = be.stack_pointer();
    be.define_function(0x00200000, [sp0](NullBackend& b, const CallArgs& a, CallResult&) {
        CHECK(a.sp == b.stack_pointer() && a.sp < sp0 && a.sp % 16 == 0);
        CHECK(a.n_gpr == 8 && a.n_fpr == 8);
        CHECK(r64(a.gpr[7]) == 8 && t::bitsf(a.fpr[7]) == 8.0f);
        CHECK(load<std::uint64_t>(a.sp + 0) == 9);                       // sd: full 64-bit slot
        CHECK(load<float>(a.sp + 8) == 9.0f);                            // sw at slot + 0
        CHECK(load<std::int64_t>(a.sp + 16) == -10);                     // sign-extended
        CHECK(load<float>(a.sp + 24) == 10.0f);
    });
    call_guest(0x00200000, 1, 1.0f, 2, 2.0f, 3, 3.0f, 4, 4.0f, 5, 5.0f, 6, 6.0f, 7, 7.0f, 8, 8.0f, 9, 9.0f, -10, 10.0f);
    CHECK(be.stack_pointer() == sp0);
}

TEST(quadword_on_the_stack_is_16_aligned) {                 // probe2:t_q10
    NullBackend be;
    BackendScope s(be);
    be.define_function(0x00200100, [](NullBackend&, const CallArgs& a, CallResult&) {
        CHECK(load<std::uint32_t>(a.sp + 0) == 9);
        CHECK(u128_equal(load<u_long128>(a.sp + 16), make_u128(1, 2, 3, 4)));
        CHECK(load<std::uint32_t>(a.sp + 32) == 11);
    });
    call_guest(0x00200100, 1, 2, 3, 4, 5, 6, 7, 8, 9, make_u128(1, 2, 3, 4), 11);
}

TEST(large_struct_goes_as_address_of_a_copy) {             // GetFwdFrac caller @ 0x14D770 / callee @ 0x146044
    NullBackend be;
    BackendScope s(be);
    be.define_function(0x00200200, [](NullBackend& b, const CallArgs& a, CallResult& r) {
        const gaddr p = a.gpr[1].w[0];                                   // a0 = this, a1 = &copy
        CHECK(p >= b.stack_pointer() && p % 16 == 0);                    // in the caller's frame, above the callee's
        CHECK(load<float>(p + 4) == 2.0f && load<float>(p + 12) == 4.0f);
        store<float>(p, 99.0f);                                          // the callee owns its copy
        r.f0 = t::fbits(load<float>(p + 8));
    });
    const V3 v{1, 2, 3, 4};
    CHECK(call_guest<float>(0x00200200, gp<void>::from_raw(0x00400000), v) == 3.0f);
    CHECK(v.x == 1.0f);
    // V3a (16-aligned) and a 12-byte struct (probe1:t_i3) take the same path
    be.define_function(0x00200210, [](NullBackend&, const CallArgs& a, CallResult& r) {
        r.v0 = detail::gpr64(load<std::uint32_t>(a.gpr[0].w[0] + 8) + load<std::uint32_t>(a.gpr[1].w[0] + 12));
    });
    CHECK(call_guest<int>(0x00200210, I3{1, 2, 30}, V3a{0, 0, 0, 0}) == 30);
}

TEST(small_structs_go_in_one_register) {                   // PressKey @ 0x34E990, DrawGlobeHalo @ 0x3B17F8, probes
    NullBackend be;
    BackendScope s(be);
    be.define_function(0x00200300, [](NullBackend&, const CallArgs& a, CallResult&) {
        CHECK(a.gpr[1].w[0] == 0x11u && a.gpr[1].w[1] == 0x22u);         // Cell: field_0 low word, field_4 high
        CHECK(a.gpr[2].w[0] == 0x04030201u);                             // RwRGBA image (r first)
        CHECK(t::bitsf(a.fpr[0]) == 0.5f && t::bitsf(a.fpr[1]) == 2.0f); // lone-float struct in f12, then the float
        CHECK(a.gpr[3].w[0] == t::fbits(1.0f) && a.gpr[3].w[1] == t::fbits(-1.0f));   // V2 {float, float}: a GPR
        CHECK(a.n_gpr == 4 && a.n_fpr == 2);
    });
    call_guest(0x00200300, gp<void>::from_raw(0x00400000), Cell{0x11, 0x22}, Rgba{1, 2, 3, 4}, LoneF{0.5f}, 2.0f,
               V2{1.0f, -1.0f});
}

TEST(struct_results_use_a_hidden_pointer) {                // GetTargetPosition @ 0x149E8C; GetAlignPos @ 0x1A02C0
    NullBackend be;
    BackendScope s(be);
    // CB3AITargetSpline::GetTargetPosition: a0 = result, a1 = this; v0 is not set.
    be.define_function(0x00200400, [](NullBackend&, const CallArgs& a, CallResult& r) {
        const gaddr self = a.gpr[1].w[0];
        store<V3>(a.gpr[0].w[0], load<V3>(self + 0x10));
        r.v0 = detail::gpr64(0xDEAD);                                    // must be ignored
    });
    const gaddr obj = 0x00400000;
    store<V3>(obj + 0x10, V3{5, 6, 7, 1});
    const V3 r = call_guest<V3>(0x00200400, gp<void>::from_raw(obj));
    CHECK(r.x == 5.0f && r.z == 7.0f && r.w == 1.0f);
    // an 8-byte CGtV2d result too, with the enum in a2
    be.define_function(0x00200410, [](NullBackend&, const CallArgs& a, CallResult&) {
        CHECK(a.gpr[1].w[0] == obj && a.gpr[2].w[0] == 3);
        store<V2>(a.gpr[0].w[0], V2{1.5f, 2.5f});
    });
    const V2 r2 = call_guest<V2>(0x00200410, gp<void>::from_raw(obj), static_cast<EHUDAlign_>(3));
    CHECK(r2.x == 1.5f && r2.y == 2.5f);
    // through a vtable: the result pointer comes before this
    const gaddr vt = 0x00410000;
    store<std::uint32_t>(obj + 0x20, vt);
    store<std::uint32_t>(vt + vslot(2), 0x00200400);
    const V3 r3 = vcall<V3>(obj, 0x20, vslot(2));
    CHECK(r3.y == 6.0f);
}

TEST(narrow_results_and_soft_doubles) {                    // probe2:r_b/c_ret, PressKey @ 0x34E9AC; probe1:t_d/r_d
    NullBackend be;
    BackendScope s(be);
    be.define_function(0x00200500, [](NullBackend&, const CallArgs&, CallResult& r) { r.v0 = make_u128(0x100, 0, 0, 0); });
    CHECK(call_guest<bool>(0x00200500) == false);                       // low byte 0: false despite upper bits
    CHECK(call_guest<unsigned char>(0x00200500) == 0);
    CHECK(call_guest<int>(0x00200500) == 0x100);
    be.define_function(0x00200510, [](NullBackend&, const CallArgs& a, CallResult& r) {
        CHECK(a.gpr[0].w[0] == 1);                                       // (int, double, float) -> a0, a1, f12
        CHECK(r64(a.gpr[1]) == 0x4000000000000000ull && t::bitsf(a.fpr[0]) == 3.0f);
        r.v0 = a.gpr[1];
    });
    CHECK(call_guest<double>(0x00200510, 1, 2.0, 3.0f) == 2.0);
}

TEST(varargs_promote_floats_to_soft_doubles) {             // probe2:c_va; sprintf caller @ 0x1300E0
    NullBackend be;
    BackendScope s(be);
    be.define_function(0x00200600, [](NullBackend&, const CallArgs& a, CallResult&) {
        CHECK(a.n_fpr == 0 && a.n_gpr == 6);
        CHECK(a.gpr[0].w[0] == 0x00480000u);                             // the format string
        CHECK(r64(a.gpr[1]) == 0x3FF8000000000000ull);                    // 1.5f as a double
        CHECK(r64(a.gpr[2]) == 7);
        CHECK(r64(a.gpr[3]) == 0x123456789ull);                           // long long, one GPR
        CHECK(r64(a.gpr[4]) == 0x4004000000000000ull);                    // 2.5f
        CHECK(r64(a.gpr[5]) == 99);                                       // 'c' promoted to int
    });
    call_guest_varargs(0x00200600, gp<const char>::from_raw(0x00480000), 1.5f, 7, 0x123456789ll, 2.5f, 'c');
}

// ---- guest -> native ----
// A native override shaped like DrawPositionMarker (11 parameters; the 9th integer on the stack).
static float g_dpm_float2;
static int native_dpm(gp<void> self, V3 pos, gp<const unsigned short> text, int idx, gp<void> a, gp<void> b, float f,
                      bool k1, bool k2, bool k3, float f2) {
    g_dpm_float2 = f2;
    return static_cast<int>(pos.y) + idx + (k1 ? 100 : 0) + (k2 ? 1000 : 0) + (k3 ? 10000 : 0) +
           static_cast<int>(f) + ((self.addr() && text.addr() && !a.addr() && !b.addr()) ? 0 : -1000000);
}
TEST(native_entry_reads_the_retail_image) {                // DrawSingleVehicleValue's call @ 0x1541E0-0x154200
    NullBackend be;
    BackendScope s(be);
    // the image the retail caller builds: a1 = &local V3d, a3 = -1, t0/t1 = 0, t2 = k, t3 = 1, f12 = f, f13 = 0.5;
    // the 9th integer slot (the third bool) at sp + 0.
    const gaddr sp = 0x01000000;
    store<V3>(sp + 0x40, V3{0, 20, 0, 1});
    store<std::uint64_t>(sp + 0, 0xFFFFFF01ull);                         // garbage above the low byte
    CallArgs a;
    a.sp = sp;
    a.gpr[0] = detail::gpr64(0x00400000);
    a.gpr[1] = detail::gpr64(sp + 0x40);
    a.gpr[2] = detail::gpr64(0x00400024);
    a.gpr[3] = detail::gpr64(~0ull);                                     // -1 sign-extended
    a.gpr[6] = make_u128(0x00000200, 0, 0, 0);                           // a bool register with only bit 9 set: false
    a.gpr[7] = detail::gpr64(1);
    a.fpr[0] = t::fbits(3.0f);
    a.fpr[1] = t::fbits(0.5f);
    CallResult r;
    native_entry<&native_dpm>(a, r);
    CHECK(static_cast<int>(r.v0.w[0]) == 20 - 1 + 0 + 1000 + 10000 + 3);
    CHECK(g_dpm_float2 == 0.5f);
}

// Struct result through the hidden pointer; struct by value; a stack quadword; a recorded store.
static V3 native_scaled(gp<void> self, V3 v, Cell c, int i1, int i2, int i3, int i4, int i5, u_long128 q, int last) {
    (void)self;
    const float k = static_cast<float>(c.field_0 + c.field_4 + i1 + i2 + i3 + i4 + i5 + static_cast<int>(q.w[3]) + last);
    return V3{v.x * k, v.y * k, v.z * k, v.w};
}
TEST(native_entry_struct_result_and_stack_roundtrip) {
    NullBackend be(kMainRamSize, true);
    BackendScope s(be);
    be.define_native(0x00210000, &native_entry<&native_scaled>);
    // through call_guest: this a0 is GPR 1 (a0 = result), V3 address a2, cell a3, i1..i4 t0-t3, i5 +0, q +16, last +32
    using P = Plan<true, 16, gp<void>, V3, Cell, int, int, int, int, int, u_long128, int>;
    static_assert(P::layout.slots[0].index == 1 && P::layout.slots[1].index == 2 && P::layout.slots[2].index == 3);
    static_assert(P::layout.slots[7].offset == 0 && P::layout.slots[8].offset == 16 && P::layout.slots[9].offset == 32);
    be.clear_logs();
    const V3 r = call_guest<V3>(0x00210000, gp<void>::from_raw(0x00400000), V3{1, 2, 3, 9}, Cell{1, 1}, 1, 1, 1, 1, 1,
                                make_u128(0, 0, 0, 2), 1);
    CHECK(r.x == 10.0f && r.y == 20.0f && r.z == 30.0f && r.w == 9.0f);
    bool result_recorded = false;
    for (const auto& st : be.store_log()) result_recorded |= st.len == 16;
    CHECK(result_recorded);
}

TEST(native_entry_masks_narrow_parameters) {               // SetActive @ 0x150EE4 (andi a2, a1, 0xFF)
    NullBackend be;
    BackendScope s(be);
    CallArgs a;
    a.gpr[0] = detail::gpr64(0xFFFFFFFFFFFF0100ull);
    a.gpr[1] = detail::gpr64(0x12345678ABCD0007ull);
    CallResult r;
    native_entry<+[](bool b, unsigned short u) { return (b ? 1000 : 0) + static_cast<int>(u); }>(a, r);
    CHECK(r.v0.w[0] == 7);                                               // bool false (low byte 0); u = 0x0007
}

TEST_MAIN("MWCC EE calling convention (b3host/call.h vs docs/abi-mwcc-ee.md)")
