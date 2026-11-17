// b3host adapter: types, address decoding, gp<T>, typed access + store recording, call marshalling in both
// directions, virtual calls, guest stack frames, the decomp B3Ptr.h bridge -- all on the null backend.
#include "b3host/call.h"
#include "b3host/ps2f.h"
#include "b3host/null_backend.h"
#include "test.h"

#include <cstddef>

using namespace b3host;

// The decomp's include/b3/B3Ptr.h host-branch functions (defined in src/b3host/host.cpp).
void* B3GuestToHost(unsigned int guest);
unsigned int B3HostToGuest(const void* host);

// A struct laid out as original code sees it (4-byte pointer fields).
struct Node {
    gp<Node> next;     // +0
    float value;       // +4
    gp<void> data;     // +8
    std::uint32_t id;  // +12
};
static_assert(sizeof(Node) == 16 && offsetof(Node, value) == 4 && offsetof(Node, data) == 8);

TEST(types) {
    static_assert(sizeof(ps2_ulong) == 8 && sizeof(u_long128) == 16 && alignof(u_long128) == 16);
    static_assert(sizeof(gp<Node>) == 4 && sizeof(gfn<int(int)>) == 4);
    CHECK(fnv1a64("", 0) == 0xcbf29ce484222325ull);    // FNV-1a 64 test vectors
    CHECK(fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull);
}

TEST(address_decoding) {
    NullBackend be;
    BackendScope s(be);
    CHECK(decode(0x00100000, 4).region == Region::MainRam);
    CHECK(decode(0x20100000, 4).offset == 0x00100000);     // uncached alias
    CHECK(decode(0x30100000, 4).offset == 0x00100000);     // UCAB alias
    CHECK(decode(0x80100000, 4).offset == 0x00100000);     // kseg0
    CHECK(decode(0x70000010, 4).region == Region::Scratchpad && decode(0x70000010, 4).offset == 0x10);
    CHECK(decode(0x70003FFE, 4).region == Region::Other);  // runs off the scratchpad
    CHECK(decode(0x10003000, 4).region == Region::Other);  // I/O register
    CHECK(decode(0x01FFFFFC, 4).region == Region::MainRam);
    CHECK(decode(0x01FFFFFE, 4).region == Region::Other);  // past the end of 32 MB
}

TEST(load_store_and_recording) {
    NullBackend be(kMainRamSize, /*record_stores=*/true);
    BackendScope s(be);
    store<std::uint32_t>(0x00100000, 0xDEADBEEF);
    CHECK(load<std::uint32_t>(0x20100000) == 0xDEADBEEF);   // read back through an alias
    CHECK(be.ram()[0x100000] == 0xEF);                     // little endian, as the EE
    store<float>(0x70000020, 1.5f);
    CHECK(load<float>(0x70000020) == 1.5f);
    CHECK(be.store_log().size() == 2);
    CHECK(be.store_log()[0].address == 0x00100000 && be.store_log()[0].len == 4);
    CHECK(be.store_log()[1].address == 0x70000020);
    // MMIO goes through the slow path and is not "recorded" as a RAM store
    store<std::uint32_t>(0x10003000, 0x1234);
    CHECK(load<std::uint32_t>(0x10003000) == 0x1234);
    CHECK(be.slow_log().size() == 2 && be.slow_log()[0].write && !be.slow_log()[1].write);
    CHECK(be.store_log().size() == 2);
    // bulk helpers
    const char msg[] = "takedown";
    copy_to_guest(0x00200000, msg, sizeof msg);
    char back[sizeof msg] = {};
    copy_from_guest(back, 0x20200000, sizeof msg);
    CHECK(std::memcmp(back, msg, sizeof msg) == 0);
    fill_guest(0x00200000, 0xAA, 4);
    CHECK(load<std::uint32_t>(0x00200000) == 0xAAAAAAAAu);
    CHECK(be.store_log().size() == 4);
}

TEST(gp_pointers) {
    NullBackend be;
    BackendScope s(be);
    // two nodes in guest RAM, linked by guest addresses
    const gaddr a = 0x00300000, b = 0x00300010;
    gp<Node> pa = gp<Node>::from_raw(a), pb = gp<Node>::from_raw(0x20300010);   // b through the uncached alias
    pa->next = pb;                         // copying a gp keeps the raw value (alias survives)
    pa->value = 2.5f;
    pb->next = nullptr;
    pb->id = 7;
    CHECK(load<std::uint32_t>(a) == 0x20300010);
    CHECK(pa->next->id == 7);
    CHECK(!pa->next->next);
    CHECK(pa->next == pb && pa->next != pa);
    // assigning a host pointer canonicalizes it
    Node* hb = pb.get();
    pa->next = hb;
    CHECK(load<std::uint32_t>(a) == b);
    // arithmetic and fields by offset
    CHECK((pa + 1).addr() == b);
    CHECK(pa.field<float>(4).load() == 2.5f);
    pa.field<std::uint32_t>(12).store(99);
    CHECK(pa->id == 99);
    // scratchpad pointers
    gp<Node> ps = gp<Node>::from_raw(0x70000100);
    ps->value = 1.0f;
    CHECK(guest_address(ps.get()) == 0x70000100);
    // errors: dereferencing MMIO, converting host memory
    gp<Node> mmio = gp<Node>::from_raw(0x10000000);
    CHECK_THROWS((void)mmio->value);
    Node host_local{};
    CHECK_THROWS((void)gp<Node>(&host_local));
    // gp<T> survives memcpy into a guest struct and back (trivially copyable, 4 bytes)
    Node n{};
    n.next = pb;
    copy_to_guest(0x00400000, &n, sizeof n);
    CHECK(load<std::uint32_t>(0x00400000) == 0x20300010);
    // icast of a member array is its guest address (port13: CGtSoundManager::Construct's `(int)field_48`), also through
    // open_array (T18); pcast of one stays the host pointer
    struct Owner { std::uint32_t pad; unsigned char inner[0x14]; open_array<std::uint32_t> tail; };
    Owner* o = gp<Owner>::from_raw(0x00300100).get();
    CHECK(icast<int>(o->inner) == 0x00300104);
    CHECK(icast<unsigned int>(o->tail) == 0x00300118);
    CHECK(pcast<unsigned char>(o->inner) == &o->inner[0]);
    o->tail[3] = 5;
    CHECK(load<std::uint32_t>(0x00300124) == 5);
    // T19 (port14): `(P *)&p[i]` through a guest pointer is address arithmetic (Fixup__16CB3TriggerStructFUi adds a base
    // to stored offsets): no range check, wraps like addu; `&p[i]` through operator[] would be a checked dereference
    struct Rec { gp<Node> field_0; };
    Rec* r = gp<Rec>::from_raw(0x00300200).get();
    r->field_0 = gp<Node>::from_raw(0x01F00000);
    CHECK_THROWS((void)&r->field_0[0x02000000u / sizeof(Node)]);
    r->field_0 = pcast_elem<Node>(r->field_0, 0x00200000u / sizeof(Node));   // 0x02100000: outside RAM, stored as is
    CHECK(load<std::uint32_t>(0x00300200) == 0x02100000);
    gp<unsigned char> bytes = gp<unsigned char>::from_raw(0xFFFFFFF0u);
    CHECK(pcast_elem<unsigned char>(bytes, 0x20u).addr() == 0x10);
    CHECK(pcast_elem<Node>(o->inner, 4) == pcast<Node>(&o->inner[4]));   // a plain array keeps the plain form
    // boot2: an integer outside the windows casts to a foreign pointer (CB3DebrisArray::Prepare's `(void *)-9999`):
    // no fatal at the cast, its value back through icast / guest_address, a gp of it holds the value
    void* f = pcast<void>(-9999);
    CHECK(f != nullptr && detail::is_foreign(f));
    CHECK(icast<int>(f) == -9999);
    CHECK(guest_address(f) == 0xFFFFD8F1u);
    CHECK(gp<void>(f).raw == 0xFFFFD8F1u);
    CHECK(pcast<void>(0x00300200) == host_pointer(0x00300200));   // RAM keeps the window pointer
    CHECK(pcast<void>(0) == nullptr);
}

TEST(b3ptr_bridge) {
    NullBackend be;
    BackendScope s(be);
    CHECK(B3GuestToHost(0) == nullptr);
    CHECK(B3GuestToHost(0x00100000) == be.ram().data() + 0x100000);
    CHECK(B3HostToGuest(be.ram().data() + 0x1234) == 0x1234);
    CHECK(B3HostToGuest(nullptr) == 0);
    CHECK_THROWS(B3GuestToHost(0x12000000));
}

// --- calls ---
static int g_seen_a0 = 0;
TEST(call_guest_marshalling) {
    NullBackend be;
    BackendScope s(be);
    // a "guest function" checking the register image
    be.define_function(0x00150000, [](NullBackend&, const CallArgs& a, CallResult& r) {
        g_seen_a0 = static_cast<int>(a.gpr[0].w[0]);
        // int -1 is sign-extended to 64 bits; unsigned 0x80000000 too (MIPS64 convention [L])
        CHECK(a.gpr[1].w[0] == 0xFFFFFFFFu && a.gpr[1].w[1] == 0xFFFFFFFFu);
        CHECK(a.gpr[2].w[0] == 0x80000000u && a.gpr[2].w[1] == 0xFFFFFFFFu);
        CHECK(a.gpr[3].w[0] == 0x89ABCDEFu && a.gpr[3].w[1] == 0x01234567u);   // 64-bit MWCC long in one GPR
        CHECK(a.gpr[4].w[2] == 3 && a.gpr[4].w[3] == 4);                       // u_long128: one whole GPR
        CHECK(a.n_fpr == 2 && t::bitsf(a.fpr[0]) == 1.5f && t::bitsf(a.fpr[1]) == -2.0f);
        CHECK(a.n_gpr == 6 && a.gpr[5].w[0] == 0x00100000);                     // gp<T>: its guest address
        r.v0.w[0] = 42;
        r.f0 = t::fbits(0.25f);
    });
    const int r = call_guest<int>(0x00150000, 7, -1, 1.5f, 0x80000000u, ps2_ulong{0x0123456789ABCDEFull},
                                  make_u128(1, 2, 3, 4), -2.0f, gp<Node>::from_raw(0x00100000));
    CHECK(r == 42 && g_seen_a0 == 7);
    CHECK(call_guest<float>(0x00150000, 7, -1, 1.5f, 0x80000000u, ps2_ulong{0x0123456789ABCDEFull},
                            make_u128(1, 2, 3, 4), -2.0f, gp<Node>::from_raw(0x00100000)) == 0.25f);
    CHECK_THROWS(call_guest(0x00999990));   // nothing there
}

// A native override, entered from "guest" code through native_entry (the same path a backend thunk takes).
static float native_scale(gp<Node> node, float k, int add) {
    node->value = node->value * k + static_cast<float>(add);
    note_store(node.addr() + 4, 4);
    return node->value;
}
TEST(native_entry_roundtrip) {
    NullBackend be(kMainRamSize, true);
    BackendScope s(be);
    be.define_native(0x00160000, &native_entry<&native_scale>);
    gp<Node> n = gp<Node>::from_raw(0x00500000);
    n->value = 2.0f;
    // original -> native: the "guest" calls 0x160000 with a0 = node, f12 = 3, a1 = 1
    const float r = call_guest<float>(0x00160000, n, 3.0f, 1);
    CHECK(r == 7.0f && n->value == 7.0f);
    CHECK(be.store_log().size() == 1 && be.store_log()[0].address == 0x00500004);
    // gfn<> field: a guest function pointer stored in guest memory, called from native code
    store<std::uint32_t>(0x00500100, 0x00160000);
    const auto fn = load<gfn<float(gp<Node>, float, int)>>(0x00500100);
    CHECK(fn(n, 2.0f, 0) == 14.0f);
}

// Virtual calls: an object built by "original code" with a guest vtable {0, 0, Update, Release} (decomp N102/N119).
TEST(virtual_call_through_guest_vtable) {
    NullBackend be;
    BackendScope s(be);
    const gaddr vtable = 0x00600000, object = 0x00610000;
    store<std::uint32_t>(vtable + 0, 0);
    store<std::uint32_t>(vtable + 4, 0);
    store<std::uint32_t>(vtable + vslot(0), 0x00170000);   // Update(float)
    store<std::uint32_t>(vtable + vslot(1), 0x00170100);   // Release()
    store<std::uint32_t>(object + 0, vtable);              // vptr at +0
    store<std::uint32_t>(object + 4, 0);                    // a counter field
    be.define_function(0x00170000, [](NullBackend&, const CallArgs& a, CallResult&) {
        const gaddr self = a.gpr[0].w[0];
        store<std::uint32_t>(self + 4, load<std::uint32_t>(self + 4) + static_cast<std::uint32_t>(t::bitsf(a.fpr[0])));
    });
    be.define_function(0x00170100, [](NullBackend&, const CallArgs& a, CallResult& r) { r.v0.w[0] = a.gpr[0].w[0]; });
    vcall(object, 0, vslot(0), 5.0f);
    vcall(object, 0, vslot(0), 2.0f);
    CHECK(load<std::uint32_t>(object + 4) == 7);
    CHECK(vcall<gaddr>(object, 0, 0x0C) == object);         // slot by byte offset (vfunc_0C)
}

// Passing the address of a local to original code: the local lives on the guest stack.
TEST(guest_stack_frame) {
    NullBackend be;
    BackendScope s(be);
    const gaddr sp0 = be.stack_pointer();
    be.define_function(0x00180000, [](NullBackend& b, const CallArgs& a, CallResult&) {
        const gaddr out = a.gpr[0].w[0];
        CHECK(out >= b.stack_pointer());                    // the callee's frame is below ours
        store<float>(out, 3.0f);
        store<float>(out + 4, 4.0f);
    });
    {
        GuestStackFrame frame(8);
        CHECK(frame.addr() < sp0 && frame.addr() % 16 == 0);
        call_guest(0x00180000, frame.as<float>());
        CHECK(load<float>(frame.addr()) == 3.0f && load<float>(frame.addr() + 4) == 4.0f);
        {
            GuestStackFrame inner(32);
            CHECK(inner.addr() + 32 <= frame.addr());
        }
        CHECK(be.stack_pointer() == frame.addr());
    }
    CHECK(be.stack_pointer() == sp0);
    // a guest function that breaks the sp contract is reported
    be.define_function(0x00180100, [](NullBackend& b, const CallArgs&, CallResult&) { b.set_stack_pointer(b.stack_pointer() - 16); });
    CHECK_THROWS(call_guest(0x00180100));
}

TEST(cost_reporting) {
    NullBackend be;
    BackendScope s(be);
    add_cost(12);
    add_cost(30);
    CHECK(be.cost() == 42);
}

TEST(unbound_backend_is_an_error) {
    bind(nullptr);
    CHECK_THROWS(backend());
}

// gen1: a C varargs guest call whose arguments are all plain values (call_guest_varargs found by declaration, not ADL:
// B3PlayerCameraStatePlayer 0x447000), and a T18 open_array argument decaying to its element's guest address
// (cbFormatAuthMessage 0x344E10's snprintf)
extern "C" int gen1_varargs_callee(int, ...);
TEST(guest_fn_varargs) {
    NullBackend be;
    BackendScope s(be);
    static std::uint32_t seen[3];
    be.define_function(0x00150200, [](NullBackend&, const CallArgs& a, CallResult& r) {
        seen[0] = a.gpr[0].w[0]; seen[1] = a.gpr[1].w[0]; seen[2] = a.n_gpr;
        r.v0.w[0] = 5;
    });
    using F = guest_fn<decltype(gen1_varargs_callee), 0x00150200u>;
    CHECK(F::call(7, 9) == 5 && seen[0] == 7 && seen[1] == 9 && seen[2] == 2);
    struct Rec { unsigned char pad[8]; open_array<char> name; };
    Rec* rec = gp<Rec>::from_raw(0x00100200).get();
    CHECK(F::call(1, rec->name) == 5 && seen[0] == 1 && seen[1] == 0x00100208);
}

// gen1: gp compound assignment / decrement (guest-address arithmetic) and the cv-adding conversion
TEST(gp_compound_and_cv) {
    gp<std::uint32_t> p = gp<std::uint32_t>::from_raw(0x00100100);
    p += 3;
    CHECK(p.raw == 0x0010010C);
    p -= 1;
    CHECK(p.raw == 0x00100108);
    CHECK((--p).raw == 0x00100104);
    CHECK((p--).raw == 0x00100104 && p.raw == 0x00100100);
    gp<const std::uint32_t> c = p;   // T* -> const T*
    CHECK(c.raw == 0x00100100);
    static_assert(!std::is_convertible_v<gp<const std::uint32_t>, gp<std::uint32_t>>, "const is never dropped");
    static_assert(!std::is_convertible_v<gp<std::uint32_t>, gp<std::uint16_t>>, "no reinterpretation");
}

// gen1 (decomp2host T34): a GtMathPs2 float element's address is a ps2f* or a float*, constness kept
TEST(f32_addr_conversions) {
    float v[2] = {1.0f, 2.0f};
    ps2f* a = f32_addr(&v[1]);
    float* b = f32_addr(&v[1]);
    const ps2f* c = f32_addr(static_cast<const float*>(&v[0]));
    CHECK(static_cast<void*>(a) == static_cast<void*>(&v[1]) && b == &v[1]);
    CHECK(static_cast<const void*>(c) == static_cast<const void*>(&v[0]) && static_cast<float>(*a) == 2.0f);
    static_assert(!std::is_convertible_v<f32_address<const void>, ps2f*>, "const is never dropped");
}

// gen1: a C cast between two function-pointer types is the same guest code address (explicit only)
TEST(gfn_cast_between_signatures) {
    gfn<bool(char*, int)> a = gfn<bool(char*, int)>::from_raw(0x00338C10);
    gfn<int(char*, int, int)> b = (gfn<int(char*, int, int)>)a;
    CHECK(b.raw == 0x00338C10);
    static_assert(!std::is_convertible_v<gfn<bool(char*, int)>, gfn<int(char*, int, int)>>, "explicit only");
}

// gen1 (T31 audit): GuestLocal / GuestLocalArray keep the original declaration's lifetime on the guest stack
static int g_ctor = 0, g_dtor = 0, g_last_dtor = -1;
struct Tracked {
    int v;
    Tracked() : v(7) { ++g_ctor; }
    explicit Tracked(int a) : v(a) { ++g_ctor; }
    Tracked(int a, int b) : v(a * 10 + b) { ++g_ctor; }
    Tracked(const Tracked& o) : v(o.v + 100) { ++g_ctor; }
    ~Tracked() { ++g_dtor; g_last_dtor = v; }
};
struct Agg { int a; short b; };
static gaddr t31_early(bool stop, gaddr* inner) {
    GuestLocal<Tracked> l;
    *inner = l.addr();
    if (stop) return backend().stack_pointer();   // the frame is still live here
    return 0;
}
TEST(t31_guest_local_lifetime) {
    NullBackend be;
    BackendScope s(be);
    const gaddr sp0 = be.stack_pointer();
    g_ctor = g_dtor = 0;
    {
        GuestLocal<Tracked> a;                       // default-initialised: the user constructor runs
        CHECK((*a).v == 7 && g_ctor == 1 && a.addr() % 16 == 0 && be.stack_pointer() == a.addr() && a.addr() < sp0);
        {
            GuestLocal<Tracked> b(copy_init, Tracked(3));   // `Tracked b = Tracked(3);`: one construction from the value
            CHECK((*b).v == 103 && b.addr() < a.addr());
            GuestLocal<Tracked> c(4, 5);                     // `Tracked c(4, 5);`
            CHECK((*c).v == 45);
        }
        CHECK(g_dtor == 3 && g_last_dtor == 103 && be.stack_pointer() == a.addr());   // c then b destroyed, LIFO
    }
    CHECK(g_dtor == 4 && be.stack_pointer() == sp0);
    for (int i = 0; i < 3; i++) {                    // a loop body's local: constructed and destroyed per iteration
        GuestLocal<Tracked> l(i);
        CHECK((*l).v == i && g_ctor == 5 + i && g_dtor == 4 + i);
    }
    CHECK(g_dtor == 7 && be.stack_pointer() == sp0);
    gaddr inner = 0;
    CHECK(t31_early(true, &inner) == inner && g_dtor == 8 && be.stack_pointer() == sp0);   // early return
    {
        GuestLocal<Agg> g(brace_init, 1, short(2));  // `Agg g = {1, 2};`
        CHECK((*g).a == 1 && (*g).b == 2);
        GuestLocal<const Tracked> k(9);              // `const Tracked k(9);`
        CHECK((*k).v == 9);
        g_last_dtor = -1;
        {
            GuestLocalArray<Tracked, 3> arr;
            (*arr)[0].v = 1; (*arr)[1].v = 2; (*arr)[2].v = 3;
            CHECK(g_ctor == 12 && arr.addr() % 16 == 0);
        }
        CHECK(g_last_dtor == 1);                     // elements destroyed in reverse order: [0] last
    }
    CHECK(be.stack_pointer() == sp0);
}
// gen1: pcast_add -- an integer base is guest-address arithmetic (the scratchpad window from a RAM-range int), a pointer
// base host arithmetic
TEST(pcast_add_guest_arithmetic) {
    NullBackend be;
    BackendScope scope(be);
    char* spr = pcast_add<char>(0, 0x70002000);
    CHECK(spr == reinterpret_cast<char*>(be.scratchpad().data()) + 0x2000);   // the scratchpad window, not RAM + 0x70002000
    std::uint32_t* w = pcast_add<std::uint32_t>(0x00100000, 4);
    CHECK(w == static_cast<std::uint32_t*>(host_pointer(0x00100010u, 4)));
    char* p = static_cast<char*>(host_pointer(0x00100000u, 16));
    CHECK(pcast_add<char>(p, 8) == p + 8);
}
// gen1: gp<T> == gp<U> compares the guest addresses
TEST(gp_compare_types) {
    gp<void> a = gp<void>::from_raw(0x00100100);
    gp<std::uint32_t> b = gp<std::uint32_t>::from_raw(0x00100100), c = gp<std::uint32_t>::from_raw(0x00100104);
    CHECK(a == b && b == a && !(a == c) && a != c);
}

TEST_MAIN("b3host adapter (null backend)")
