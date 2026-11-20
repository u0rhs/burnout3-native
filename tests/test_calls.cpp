// Calls between native and guest code through b3host/call.h, run for real (port7): the guest side is code compiled
// by the decomp's MWCC (tools/abi_probe/probe3.cpp, linked by link3.sh into probe3.bin at 0x01000000), executed by
// tests/r5900_mini.h through tests/mini_backend.h. So every layout call.h computes (docs/abi-mwcc-ee.md) meets the
// compiler's own code: native -> guest (call_guest: register/stack arguments, struct copies, struct results, ptr_arg
// bounce of host locals, vcall through an MWCC vtable), guest -> native (native_entry hooked at an address MWCC code
// calls), nested guest -> native -> guest, and the native-override dispatch of call_guest (b3host::NativeOverride).
// Part 2 (retail ELF needed): a generated non-leaf decomp function against retail, with its callee native or
// interpreted.
#include "b3host/call.h"
#include "b3host/ps2f.h"
#include "mini_backend.h"
#include "test.h"

#include <bit>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#ifdef B3_GEN_CALLS
#include "b3port/game_overrides.h"   // build/gen_calls (config/test_calls.txt)
#define HAVE_GEN_CALLS 1
#include <bit>
#include <random>
#endif

using namespace b3host;

namespace {
struct V { float x, y, z, w; };
struct I2 { int a, b; };
struct I3 { int a, b, c; };
constexpr gaddr REC = 0x01100000u, RECF = 0x01100100u, RECQ = 0x01100200u;

std::map<std::string, gaddr> load_probe(mini::R5900& cpu) {
    std::map<std::string, gaddr> sym;
    cpu.ram().assign(mini::R5900::kRamSize, 0);
    std::ifstream bin(B3_PROBE3_DIR "/probe3.bin", std::ios::binary);
    std::string code((std::istreambuf_iterator<char>(bin)), std::istreambuf_iterator<char>());
    if (code.empty()) b3host::fatal("probe3.bin missing");
    std::memcpy(cpu.ram().data() + 0x01000000u, code.data(), code.size());
    std::ifstream s(B3_PROBE3_DIR "/probe3.sym");
    std::string addr, name;
    while (s >> addr >> name) sym[name] = static_cast<gaddr>(std::stoul(addr, nullptr, 16));
    return sym;
}
int ri(mini::R5900& c, int k) { int v; std::memcpy(&v, c.ram().data() + REC + 4 * k, 4); return v; }
float rf(mini::R5900& c, int k) { float v; std::memcpy(&v, c.ram().data() + RECF + 4 * k, 4); return v; }

struct Rig {
    mini::R5900 cpu;
    std::map<std::string, gaddr> sym;
    mini::MiniBackend be{cpu};
    BackendScope scope{be};
    Rig() : sym(load_probe(cpu)) { bind(&be); }
    gaddr operator[](const char* n) {
        auto it = sym.find(n);
        if (it == sym.end()) b3host::fatal(std::string("probe3: no symbol ") + n);
        return it->second;
    }
};

// native entries for the guest -> native direction (what probe3's c_* call)
int g_ints[16];
float g_floats[16];
void host_mix(int a1, float f1, int a2, float f2, int a3, float f3, int a4, float f4, int a5, float f5, int a6, float f6,
              int a7, float f7, int a8, float f8, int a9, float f9, int a10, float f10) {
    const int is[] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10};
    const float fs[] = {f1, f2, f3, f4, f5, f6, f7, f8, f9, f10};
    for (int k = 0; k < 10; ++k) { g_ints[k] = is[k]; g_floats[k] = fs[k]; }
}
void host_structs(I2 s, V v, I3 t, int k, float f) {
    g_ints[0] = s.a; g_ints[1] = s.b; g_ints[2] = t.a; g_ints[3] = t.b; g_ints[4] = t.c; g_ints[5] = k;
    g_floats[0] = v.x; g_floats[1] = v.y; g_floats[2] = v.z; g_floats[3] = v.w; g_floats[4] = f;
}
V host_rv(int a, float s) { return V{s, s + 1.0f, static_cast<float>(a), -1.0f}; }
gaddr g_r_f = 0;
float host_f(float a, float b) { return call_guest<float>(g_r_f, a, b) * 10.0f; }   // nested: back into guest code
int host_get(gp<void> self, int a) { return a * 1000 + load<int>(self.raw + 0x10); }
}  // namespace
template <> struct b3host::abi::lone_float<I2> : std::false_type {};

// ---- native -> guest ----
// gen1 (T31 audit): a guest-stack local keeps 64 canary bytes above it in native builds; a callee writing past the
// decomp's view of the type is fatal at scope end, and the guest sp is restored either way
TEST(t31_local_canary) {
    Rig r;
    {
        GuestLocal<std::uint32_t> ok;
        *ok = 5;
        GuestLocalArray<std::uint16_t, 3> arr;
        (*arr)[2] = 9;
    }
    bool thrown = false;
    try {
        GuestLocal<std::uint32_t> l;
        static_cast<std::uint8_t*>(host_pointer(l.addr(), 8))[5] = 0;   // a callee filling more than 4 bytes
    } catch (...) {
        thrown = true;
    }
    CHECK(thrown && kLocalCanary == 64);
    CHECK(r.be.stack_pointer() == 0x01F00000u);
    {   // gen1: an array's Slack is writable (a sprintf into a buffer retail overflows too); the canary follows it
        GuestLocalArray<char, 4, 32> s;
        static_cast<std::uint8_t*>(host_pointer(s.addr(), 4 + 32))[4 + 31] = 1;
    }
    thrown = false;
    try {
        GuestLocalArray<char, 4, 32> s;
        static_cast<std::uint8_t*>(host_pointer(s.addr(), 4 + 33))[4 + 32] = 1;
    } catch (...) {
        thrown = true;
    }
    CHECK(thrown && r.be.stack_pointer() == 0x01F00000u);
}
// gen1 T39: b3host::ee_div / ee_mod against the EE model itself: `div` / `divu` then `mflo` / `mfhi` run in the mini,
// over zero divisors, INT_MIN / -1 and ordinary values
TEST(t39_ee_division_vs_mini) {
    Rig r;
    const gaddr code = 0x00E00000u;
    const std::uint32_t prog[4][4] = {
        {0x0085001Au, 0x00001012u, 0x03E00008u, 0},   // div $a0, $a1; mflo $v0; jr $ra; nop
        {0x0085001Au, 0x00001010u, 0x03E00008u, 0},   // div; mfhi $v0
        {0x0085001Bu, 0x00001012u, 0x03E00008u, 0},   // divu; mflo
        {0x0085001Bu, 0x00001010u, 0x03E00008u, 0},   // divu; mfhi
    };
    std::memcpy(r.cpu.ram().data() + code, prog, sizeof prog);
    const int vals[] = {0, 1, -1, 7, -7, 100, -100, 2147483647, (-2147483647 - 1), 3, -3};
    int bad = 0;
    for (int a : vals)
        for (int b : vals) {
            const int q = call_guest<int>(code, a, b), m = call_guest<int>(code + 16, a, b);
            const int qu = call_guest<int>(code + 32, a, b), mu = call_guest<int>(code + 48, a, b);
            if (b3host::ee_div(a, b) != q || b3host::ee_mod(a, b) != m) ++bad;
            if (b3host::ee_div(static_cast<unsigned>(a), static_cast<unsigned>(b)) != static_cast<unsigned>(qu) ||
                b3host::ee_mod(static_cast<unsigned>(a), static_cast<unsigned>(b)) != static_cast<unsigned>(mu)) ++bad;
        }
    CHECK(bad == 0);
    CHECK(b3host::ee_div(5, 0) == -1 && b3host::ee_div(-5, 0) == 1 && b3host::ee_mod(-5, 0) == -5 && b3host::ee_div(7.0f, 2.0f) == 3.5f);
    int x = 9;
    b3host::ee_div_assign(x, 0);
    CHECK(x == -1);
}
// gen1 T40: b3host::guest_static constructs the guest object in place once, then sets its guard byte (retail's order)
TEST(t40_guest_static) {
    Rig r;
    struct P { float x, y; };
    int runs = 0;
    auto init = [&] { ++runs; return P{1.0f, 0.9f}; };
    P& a = b3host::guest_static<P>(0x00E10000u, 0x00E10100u, init);
    P& b = b3host::guest_static<P>(0x00E10000u, 0x00E10100u, init);
    float gx;
    std::memcpy(&gx, r.cpu.ram().data() + 0x00E10000u, 4);
    CHECK(&a == &b && runs == 1 && gx == 1.0f && a.y == 0.9f && r.cpu.ram()[0x00E10100u] == 1);
}
TEST(call_guest_mix_20_args) {   // probe2:t_mix layout, now executed: a9 +0, f9 +8, a10 +16, f10 +24
    Rig r;
    call_guest<void>(r["t_mix__Fifififififififififif"], 1, 1.0f, 2, 2.0f, 3, 3.0f, 4, 4.0f, 5, 5.0f, 6, 6.0f, 7, 7.0f, 8,
                     8.0f, 9, 9.0f, 10, 10.0f);
    for (int k = 0; k < 10; ++k) { CHECK(ri(r.cpu, k) == k + 1); CHECK(rf(r.cpu, k) == static_cast<float>(k + 1)); }
    CHECK(r.be.stack_pointer() == 0x01F00000u);
    CHECK(r.be.cost > 0);
}
TEST(call_guest_structs_by_value) {   // I2 in one GPR, V and I3 by the address of a guest-stack copy
    Rig r;
    call_guest<void>(r["t_structs__F2I21V2I3if"], I2{11, 12}, V{0.5f, 0.25f, 0.125f, 2.0f}, I3{13, 14, 15}, 16, 17.0f);
    CHECK(ri(r.cpu, 0) == 11 && ri(r.cpu, 1) == 12 && ri(r.cpu, 2) == 13 && ri(r.cpu, 3) == 14 && ri(r.cpu, 4) == 15);
    CHECK(ri(r.cpu, 5) == 16);
    CHECK(rf(r.cpu, 0) == 0.5f && rf(r.cpu, 1) == 0.25f && rf(r.cpu, 2) == 0.125f && rf(r.cpu, 3) == 2.0f);
    CHECK(rf(r.cpu, 4) == 17.0f);
}
TEST(call_guest_quadword_on_stack) {   // probe2:t_q10: the u_long128 in a 16-aligned slot at +16
    Rig r;
    call_guest<void>(r["t_q10__Fiiiiiiiii1i"], 1, 2, 3, 4, 5, 6, 7, 8, 9, make_u128(0x11, 0x22, 0x33, 0x44), 11);
    CHECK(ri(r.cpu, 0) == 9 && ri(r.cpu, 1) == 11);
    u_long128 q;
    std::memcpy(&q, r.cpu.ram().data() + RECQ, 16);
    CHECK(q.w[0] == 0x11 && q.w[1] == 0x22 && q.w[2] == 0x33 && q.w[3] == 0x44);
}
TEST(call_guest_results) {   // long long in one GPR, struct via hidden a0, float in f0, bool masked
    Rig r;
    CHECK(call_guest<long long>(r["t_ll__Fxi"], 0x123456789ALL, 5) == 0x123456789FLL);
    CHECK(ri(r.cpu, 0) == 0x3456789A && ri(r.cpu, 1) == 0x12 && ri(r.cpu, 2) == 5);
    const V v = call_guest<V>(r["r_v__Fif"], 3, 2.5f);
    CHECK(v.x == 2.5f && v.y == 5.0f && v.z == 3.0f && v.w == 1.0f);
    CHECK(call_guest<float>(r["r_f__Fff"], 1.5f, 2.0f) == 4.0f);
    CHECK(static_cast<float>(call_guest<ps2f>(r["r_f__Fff"], ps2f(1.5f), ps2f(3.0f))) == 5.5f);   // ps2f = float class
    CHECK(call_guest<bool>(r["r_b__Fi"], 5) == true);
    CHECK(call_guest<bool>(r["r_b__Fi"], 2) == false);
    CHECK(r.be.stack_pointer() == 0x01F00000u);
}
TEST(ptr_arg_bounces_host_locals) {   // a host local passed by pointer: a guest-stack copy, copied back
    Rig r;
    V out{0, 0, 0, 0};
    int n = 3;
    call_guest<void>(r["w_ptr__FP1VPi"], ptr_arg(&out), ptr_arg(&n));
    CHECK(out.x == 1.5f && out.y == 2.5f && out.z == 3.5f && out.w == 4.5f);
    CHECK(n == 10);
    CHECK(r.be.stack_pointer() == 0x01F00000u);
    // a pointer into guest memory is passed as is
    call_guest<void>(r["w_ptr__FP1VPi"], ptr_arg(reinterpret_cast<V*>(r.cpu.ram().data() + 0x01300000u)),
                     ptr_arg(reinterpret_cast<int*>(r.cpu.ram().data() + 0x01300010u)));
    CHECK(load<float>(0x01300000u) == 1.5f && load<int>(0x01300010u) == 7);
    // stub_arg: references are addresses
    V ref{0, 0, 0, 0};
    int m = 1;
    int* pm = &m;
    call_guest<void>(r["w_ptr__FP1VPi"], stub_arg<V&>(ref), stub_arg<int*>(pm));
    CHECK(ref.w == 4.5f && m == 8);
    // void* to a host local (the decomp's `const void *pp = &v;`, MWCC's by-copy convention) and an array passed as
    // a pointer to its first element: the window covers both; only changed bytes come back
    V arr[3] = {};
    int guard[4] = {1, 2, 3, 4};
    (void)guard;
    call_guest<void>(r["w_ptr__FP1VPi"], ptr_arg(static_cast<void*>(&arr[1])), ptr_arg(&m));
    CHECK(arr[1].x == 1.5f && arr[1].w == 4.5f && arr[0].x == 0.0f && arr[2].x == 0.0f && m == 15);
    // port8: two pointers into one host object share one window, so they alias in the callee as on the EE: w_ptr writes
    // out->y = 2.5f, then *n += 7 reads it back through n = &out->y (GetStageTrackID passes &local and &local+6..8).
    // Both argument orders (GCC builds the temporaries right to left; the window starts kBounceLead bytes early).
    V al{0, 0, 0, 0};
    call_guest<void>(r["w_ptr__FP1VPi"], ptr_arg(&al), ptr_arg(reinterpret_cast<int*>(&al.y)));
    CHECK(std::bit_cast<std::uint32_t>(al.y) == std::bit_cast<std::uint32_t>(2.5f) + 7 && al.x == 1.5f && al.w == 4.5f);
    struct { int pad; V v; } al2{9, {0, 0, 0, 0}};
    call_guest<void>(r["w_ptr__FP1VPi"], ptr_arg(&al2.v), ptr_arg(&al2.pad));   // n before out
    CHECK(al2.pad == 16 && al2.v.z == 3.5f);
    // lane-noise: a window's guest copy is 16-aligned whatever the host address (the host stack layout never reaches
    // guest memory); a later pointer into an open window shares it only where its own type's alignment holds (lqc2 on
    // a pointer into a shared window), else it gets a window of its own
    struct alignas(16) V16 { float x, y, z, w; };
    struct alignas(16) { float a[2]; int b[2]; V16 v; } al3{};
    PtrArg<int> pb(&al3.b[0]);
    CHECK((pb.raw & 15u) == 0u && detail::t_bounce_n == 1);
    PtrArg<V16> pv(&al3.v);   // 8 bytes after b[0]: unaligned in pb's window
    CHECK((pv.raw & 15u) == 0u && detail::t_bounce_n == 2);
}
TEST(ptr_arg_window_overrun_is_fatal) {   // boot2: a callee writing past its bounce window fails loudly
    Rig r;
    struct Big { int a[200]; } big{};       // 800 bytes, passed as a pointer to its first element: window 256 + lead
    bool threw = false;
    try {
        PtrArg<int> p(&big.a[0]);
        // the callee fills more than the window (decomp1's SoundESMHigh::Update case): a store just past it
        store<std::uint32_t>(p.raw + kBounceWindow + 16u, 0x12345678u);
    } catch (const HostError&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(detail::t_bounce_n == 0 && r.be.stack_pointer() == 0x01F00000u);
    bool quiet = true;   // writes inside the window: no complaint
    try {
        PtrArg<int> p(&big.a[0]);
        store<std::uint32_t>(p.raw + 4u, 7u);
    } catch (const HostError&) {
        quiet = false;
    }
    CHECK(quiet && big.a[1] == 7);
}
TEST(pointer_argument_is_checked_for_its_whole_pointee) {   // modsec: a mod's b3.call / set_arg gives any address
    Rig r;
    struct Big { int a[200]; };   // 800 bytes
    const gaddr last = mini::R5900::kRamSize - 4u;   // the last word of RAM
    bool small_ok = true, big_threw = false;
    try {
        (void)detail::from_image<int*>(last);   // 4 bytes: inside
    } catch (const HostError&) {
        small_ok = false;
    }
    try {
        (void)detail::from_image<Big*>(last);   // 800 bytes from there: past the end of RAM
    } catch (const HostError&) {
        big_threw = true;
    }
    CHECK(small_ok && big_threw);
    CHECK(detail::from_image<Big*>(0) == nullptr);   // null stays null
}
TEST(ptr_arg_window_leaves_no_host_bytes) {   // gen1 (determinism): a closed window restores the guest stack under it
    Rig r;
    const gaddr sp = r.be.stack_pointer();
    for (gaddr a = sp - 0x400u; a < sp; a += 4) store<std::uint32_t>(a, 0x5A5A0000u | (a & 0xFFFFu));   // stale guest stack
    volatile std::uint64_t host_neighbour = 0x00005E21E4236391ull;   // host stack bytes around the object
    V out{0, 0, 0, 0};
    int n = 3;
    (void)host_neighbour;
    call_guest<void>(r["w_ptr__FP1VPi"], ptr_arg(&out), ptr_arg(&n));
    CHECK(out.x == 1.5f && n == 10);   // the call's results still come back
    bool clean = true;
    for (gaddr a = sp - 0x400u; a < sp; a += 4) clean = clean && load<std::uint32_t>(a) == (0x5A5A0000u | (a & 0xFFFFu));
    CHECK(clean);
}
TEST(ptr_arg_windows_closed) {   // after the scopes above, no window stays open
    Rig r;
    CHECK(detail::t_bounce_n == 0 && r.be.stack_pointer() == 0x01F00000u);
}
TEST(vcall_through_mwcc_vtable) {   // vptr at +0xC (K's first virtual), slot 0 at +8 [C probe3 t_vcall: lw t9,12(a0); lw t9,8(t9)]
    Rig r;
    const gaddr obj = 0x01300100u;
    store<std::uint32_t>(obj + 0xC, r["__vt__1K"]);
    store<int>(obj + 0x10, 100);
    CHECK(vcall<int>(obj, 0xC, vslot(0), 5) == 105);
    CHECK(call_guest<int>(r["t_vcall__FP1Ki"], gp<void>::from_raw(obj), 5) == 210);
}

// ---- guest -> native, and the override table ----
TEST(guest_calls_native_entries) {
    Rig r;
    std::atomic<bool> on[4] = {true, true, true, true};
    std::atomic<std::uint64_t> nested[4] = {};
    const NativeOverride table[] = {
        {r["ext_mix__Fifififififififififif"], &native_entry<&host_mix>, 0, &on[0], &nested[0]},
        {r["ext_structs__F2I21V2I3if"], &native_entry<&host_structs>, 0, &on[1], &nested[1]},
        {r["ext_rv__Fif"], &native_entry<&host_rv>, 0, &on[2], &nested[2]},
        {r["ext_f__Fff"], &native_entry<&host_f>, 0, &on[3], &nested[3]},
    };
    for (const auto& n : table) r.be.hook_native(n);
    call_guest<void>(r["c_mix__Fv"]);
    for (int k = 0; k < 10; ++k) { CHECK(g_ints[k] == k + 1); CHECK(g_floats[k] == static_cast<float>(k + 1)); }
    call_guest<void>(r["c_structs__Fv"]);
    CHECK(g_ints[0] == 11 && g_ints[1] == 12 && g_ints[2] == 13 && g_ints[3] == 14 && g_ints[4] == 15 && g_ints[5] == 16);
    CHECK(g_floats[0] == 0.5f && g_floats[1] == 0.25f && g_floats[2] == 0.125f && g_floats[3] == 2.0f && g_floats[4] == 17.0f);
    call_guest<void>(r["c_rv__Fv"]);   // struct result through the hidden pointer MWCC passes in a0
    CHECK(rf(r.cpu, 0) == 3.0f && rf(r.cpu, 1) == 4.0f && rf(r.cpu, 2) == 21.0f && rf(r.cpu, 3) == -1.0f);
    // nested: native -> guest c_nest -> native host_f -> guest r_f -> back out: (3*2+1)*10 + 0.5
    g_r_f = r["r_f__Fff"];
    CHECK(call_guest<float>(r["c_nest__Ff"], 3.0f) == 70.5f);
    CHECK(r.be.hooked_calls == 4);
    CHECK(r.be.stack_pointer() == 0x01F00000u);
}
TEST(call_guest_prefers_enabled_native_override) {   // b3host::NativeOverride: the dispatch call_guest/gfn/vcall share
    Rig r;
    std::atomic<bool> on{true};
    std::atomic<std::uint64_t> nested{0};
    const NativeOverride table[] = {{r["Get__1KFi"], &native_entry<&host_get>, 7, &on, &nested}};
    set_native_overrides(table, 1);
    const gaddr obj = 0x01300100u;
    store<std::uint32_t>(obj + 0xC, r["__vt__1K"]);
    store<int>(obj + 0x10, 100);
    const auto cost0 = r.be.cost;
    CHECK(vcall<int>(obj, 0xC, vslot(0), 5) == 5100);   // native, through the guest vtable's target address
    CHECK(nested.load() == 1 && r.be.cost == cost0 + 7);
    on = false;   // switched off: the original runs
    CHECK(vcall<int>(obj, 0xC, vslot(0), 5) == 105);
    CHECK(nested.load() == 1);
    gfn<int(gp<void>, int)> f = gfn<int(gp<void>, int)>::from_raw(r["Get__1KFi"]);
    on = true;
    CHECK(f(gp<void>::from_raw(obj), 2) == 2100);
    set_native_overrides(nullptr, 0);
}

// port8: a native override that calls with fewer arguments than the callee reads (the decomp's `func_00240E90();`
// for a callee taking (self, p0); MWCC leaves a0/a1 as the override was entered with them).
gaddr g_get = 0;
int host_passthrough() { return call_guest<int>(g_get); }                 // K::Get(this = a0, a = a1), passed through
int host_passthrough_a1(int a) { return call_guest<int>(g_get, a); }      // a0 passed through, a1 explicit
TEST(call_guest_passes_unset_argument_registers_through) {
    Rig r;
    g_get = r["Get__1KFi"];
    const gaddr obj = 0x01300100u;
    store<int>(obj + 0x10, 100);
    CallArgs in;
    in.gpr[0] = detail::gpr64(detail::sext32(obj));
    in.gpr[1] = detail::gpr64(5);
    in.n_gpr = 2;
    CallResult res;
    native_entry<&host_passthrough>(in, res);
    CHECK(static_cast<int>(res.v0.w[0]) == 105);
    in.gpr[0] = detail::gpr64(detail::sext32(obj));
    native_entry<&host_passthrough_a1>(in, res);   // explicit a0 = 5 (int), pass-through only for the rest
    CHECK(static_cast<int>(res.v0.w[0]) == 105);
    CHECK(detail::t_incoming == nullptr);           // restored after the entry
    // a nested native override entered through dispatch sees its own entry registers, not the outer ones
    std::atomic<bool> on{true};
    std::atomic<std::uint64_t> nested{0};
    const NativeOverride table[] = {{0x00123400u, &native_entry<&host_passthrough>, 1, &on, &nested}};
    set_native_overrides(table, 1);
    CHECK(call_guest<int>(0x00123400u, gp<void>::from_raw(obj), 7) == 107);   // dispatch -> native -> guest
    CHECK(nested.load() == 1);
    set_native_overrides(nullptr, 0);
}

#ifdef HAVE_GEN_CALLS
// ---- a generated non-leaf decomp function against retail ----
// CB3AILane::GetAvgRightwardsVector (0x00147CB0, matched) calls GetAvgForwardsVector (0x00147D00, a draft) with the
// address of a local CGtV3d: the call stub bounces it through the guest stack (ptr_arg). Its callee once runs as
// retail code in the mini (interpreted), once as our native override (dispatch). Both must equal retail exactly.
TEST(generated_nonleaf_vs_retail) {
#ifndef B3_RETAIL_ELF
    std::printf("    (skipped: no retail ELF)\n");
#else
    mini::R5900 ref, ours;
    if (!ref.load_elf(B3_RETAIL_ELF) || !ours.load_elf(B3_RETAIL_ELF)) { std::printf("    (skipped)\n"); return; }
    mini::MiniBackend be(ours);
    BackendScope scope(be);
    std::mt19937 rng(7);
    auto both32 = [&](gaddr a, std::uint32_t v) { ref.poke32(a, v); ours.poke32(a, v); };
    auto bothf = [&](gaddr a, float f) { both32(a, std::bit_cast<std::uint32_t>(f)); };
    std::atomic<bool> on{false};
    std::atomic<std::uint64_t> nested{0};
    const NativeOverride table[] = {{0x00147D00u, &native_entry<&b3gen_00147D00>, 98, &on, &nested}};
    set_native_overrides(table, 1);
    const gaddr pts = 0x00F00000u, pairs = 0x00F10000u, lane = 0x00F20000u, self = 0x00F30000u, out = 0x00F40000u;
    int bad[2] = {0, 0}, n_calls = 0;
    for (int it = 0; it < 400; ++it) {
        for (int k = 0; k < 64 * 4; ++k) bothf(pts + 4 * k, std::uniform_real_distribution<float>(-500.0f, 500.0f)(rng));
        for (int k = 0; k < 40; ++k) both32(pairs + 4 * k, (rng() % 64) | ((rng() % 64) << 16));
        const unsigned count = 3 + rng() % 30;
        both32(lane + 0, pairs);
        both32(lane + 0xC, count | ((rng() & 1u) << 16));
        both32(self + 4, lane);
        both32(0x0064D354u, pts);
        const unsigned s = rng() % count;
        const bool native = it & 1;
        on = native;
        ref.call_sp(0x00147CB0u, 0x01F00000u, {}, {self, s, out});
        b3gen_00147CB0(gp<void>::from_raw(self), static_cast<unsigned short>(s), gp<void>::from_raw(out));
        ++n_calls;
        for (gaddr a = out; a < out + 16; a += 4)
            if (ref.peek32(a) != ours.peek32(a)) { if (++bad[native] <= 3) std::printf("    it %d (%s): out+%u retail %08X ours %08X\n", it, native ? "native callee" : "interpreted callee", a - out, ref.peek32(a), ours.peek32(a)); break; }
    }
    CHECK(bad[0] == 0);
    CHECK(bad[1] == 0);
    CHECK(nested.load() == 200);
    CHECK(be.guest_calls == 200);
    CHECK(be.stack_pointer() == 0x01F00000u);
    set_native_overrides(nullptr, 0);
#endif
}
#endif

TEST_MAIN("calls between native and guest code (probe3 under r5900_mini; generated non-leaf vs retail)")
