// b3host/call.h -- calls between native code and original (guest) code, in both directions.
//
//   call_guest<R>(address, args...)   native -> original: marshal C++ args into EE registers and stack, run, convert
//   call_guest_varargs<R>(address, args...)  the same for a variadic callee (default promotions applied)
//   gfn<R(Args...)>                   a 4-byte guest function pointer field, callable
//   vcall<R>(obj, vptr_off, slot, ...) a virtual call through the guest vtable of an object built by original code
//   GuestStackFrame                   guest stack memory for locals whose address original code needs
//   native_entry<Fn>                  original -> native: unpack EE registers/stack into Fn's C++ parameters
//
// The calling convention is MWCC 3.0.1b119's for the EE, established from retail asm and compile probes in
// docs/abi-mwcc-ee.md (section numbers below refer to it). In short:
//   - integer-class arguments (integers, enums, pointers, gp<T>, gfn<...>, soft-float double, u_long128, small structs,
//     addresses of struct copies) take a0-a3,t0-t3; float arguments take f12-f19; the two are numbered independently
//     (§1). 32-bit values are sign-extended to 64 bits, as MWCC callers do (§2).
//   - past 8 of a class: 8-byte stack slots from the entry sp + 0, in declaration order across both classes; a
//     u_long128 takes a 16-aligned 16-byte slot (§3).
//   - a trivially copyable struct of <= 8 bytes goes in one GPR as its memory image, except a struct holding a lone
//     float (4 bytes), which goes in an FPR: for a 4-byte struct say which with abi::lone_float<T> (§2). A larger struct
//     goes as the address of a caller-made copy (§2): call_guest puts the copy on the guest stack.
//   - a struct result of any size comes back through a hidden pointer in a0; v0 is not the pointer (§4).
//   - narrow results and parameters (bool, char, short) are truncated to their width; bool = low byte != 0 (§2, §4).
// Rejected at compile time: long double, non-trivially-copyable structs by value (MWCC passes those by address of a
// copy too, but the adapter cannot byte-copy them: pass a gp<T> to a guest copy), structs through varargs, a 4-byte
// struct without an abi::lone_float decision.
#ifndef B3HOST_CALL_H
#define B3HOST_CALL_H

#include "b3host/eediv.h"   // gen1 T39: ee_div / ee_mod for generated decomp code
#include "b3host/gp.h"

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <type_traits>
#include <utility>

namespace b3host {

template <class Sig> struct gfn;
struct ps2f;   // b3host/ps2f.h: the decomp's float on the host; a float-class value here (f12-f19, f0)
template <class T> class PtrArg;

namespace abi {
// For a 4-byte struct passed by value: does it hold exactly one float member? MWCC passes such a struct in an FPR and
// every other small struct in a GPR [C docs/abi-mwcc-ee.md §2, probe1:t_f1 vs t_i1]. C++ cannot inspect members, so
// each 4-byte struct used by value needs `template <> struct b3host::abi::lone_float<T> : std::true_type/false_type {};`
template <class T> struct lone_float { static constexpr int value = -1; };   // -1: undecided
}  // namespace abi

// ---- guest stack frames ----
// Allocates `bytes` below the current guest sp (aligned down to `align`) and moves sp there, so original code called
// meanwhile neither overwrites nor sees it as free; the destructor restores sp. Frames must nest (LIFO).
class GuestStackFrame {
public:
    explicit GuestStackFrame(std::uint32_t bytes, std::uint32_t align = 16)
        : saved_(backend().stack_pointer()), base_((saved_ - bytes) & ~(align - 1u)) {
        backend().set_stack_pointer(base_);
    }
    ~GuestStackFrame() { backend().set_stack_pointer(saved_); }
    GuestStackFrame(const GuestStackFrame&) = delete;
    GuestStackFrame& operator=(const GuestStackFrame&) = delete;
    gaddr addr() const { return base_; }
    template <class T> gp<T> as() const { return gp<T>::from_raw(base_); }
private:
    gaddr saved_;
    gaddr base_;
};
// boot2 (decomp2host T31): a local moved onto the guest stack is constructed there as its declaration would construct it
// -- default-initialisation (a user constructor runs; a trivial type keeps the bytes it finds, as an uninitialised
// local does) -- and not merely given an address (func_003EF920's local: its constructor zeroes field_3C, which the
// function then tests; a bare reference read an earlier frame's bytes there and dereferenced them)
template <class T> inline T* construct(gaddr a) {
    return ::new (host_pointer(a, static_cast<std::uint32_t>(sizeof(T)))) T;
}
template <class T, std::size_t N> inline T (*construct_array(gaddr a))[N] {
    T* p = static_cast<T*>(host_pointer(a, static_cast<std::uint32_t>(sizeof(T) * N)));
    std::uninitialized_default_construct_n(p, N);
    return reinterpret_cast<T (*)[N]>(p);
}

// gen1 (T31 audit): a local moved onto the guest stack with the declaration's own lifetime. The holder owns the guest
// frame (the guest sp moves down for its scope), constructs T there as the declaration would -- default-initialised
// (`T x;`), from its initialiser (`T x = e;`: copy_init, `T x = {a, b};`: brace_init, `T x(a, b);`: direct) -- and
// destroys it when the scope ends, before the frame gives the stack back: destructors at scope exit, a loop body's
// local constructed and destroyed per iteration, early returns, exceptions. Arrays: each element default-initialised,
// destroyed in reverse order.
// gen1 T40: a function-local static the decomp annotates with its guest object and guard
// (`static CGtV2d sScale(1.0f, 0.9f); // D_004B7300 (guard D_004E26C0)`, func_0019DD00): the guest object itself,
// constructed in place the first time its guard byte is zero, then the guard set to 1 -- retail's sequence
// [C func_0019DD00 0x19E4F4: swc1 x, D_004B7300; swc1 y, D_004B7304; sb 1, D_004E26C0]. The initialiser is a callable,
// run once (as a C++ static's). No destructor is registered (the decomp's annotated statics are trivially destructible).
template <class T, class F>
T& guest_static(gaddr object, gaddr guard, F&& init) {
    static_assert(std::is_trivially_destructible_v<T>, "guest_static: a static with a destructor needs its registration");
    auto* g = static_cast<std::uint8_t*>(host_pointer(guard, 1));
    T* p = static_cast<T*>(host_pointer(object, static_cast<std::uint32_t>(sizeof(T))));
    if (*g == 0) {
        ::new (static_cast<void*>(p)) T(init());
        *g = 1;
    }
    return *p;
}
// A guest callee given the local's address may fill more than the decomp's view of T declares (views keep only the fields
// a function touches): native builds (-DB3_BOUNCE_CANARY=1) keep kLocalCanary bytes of a fixed pattern above the object
// and a change is fatal when the scope ends, as for ptr_arg's bounce windows (else the writes land on older guest frames)
#ifndef B3_BOUNCE_CANARY
#define B3_BOUNCE_CANARY 0
#endif
inline constexpr std::uint32_t kLocalCanary = B3_BOUNCE_CANARY ? 64 : 0;
namespace detail {
inline void local_canary_set(void* p) { if (kLocalCanary) std::memset(p, 0xB3, kLocalCanary); }
inline void local_canary_check(const void* p, gaddr a, std::size_t size) {
    if (!kLocalCanary) return;
    const auto* c = static_cast<const std::uint8_t*>(p);
    for (std::uint32_t k = 0; k < kLocalCanary; ++k)
        if (c[k] != 0xB3) {
            char m[200];
            std::snprintf(m, sizeof m, "b3host: a guest callee wrote past a T31 guest-stack local (guest %08X, %zu bytes; "
                          "byte +%u past it): the decomp's view of the local's type is smaller than what the callee fills",
                          a, size, k);
            if (!std::uncaught_exceptions()) fatal(m);
            return;
        }
}
}  // namespace detail
struct copy_init_t {};
struct brace_init_t {};
inline constexpr copy_init_t copy_init{};
inline constexpr brace_init_t brace_init{};
template <class T>
class GuestLocal {
public:
    using U = std::remove_cv_t<T>;
    GuestLocal() : frame_(sizeof(U) + kLocalCanary, alignof(U) > 16 ? alignof(U) : 16), p_(::new (host()) U) { guard(); }
    template <class A> GuestLocal(copy_init_t, A&& a)
        : frame_(sizeof(U) + kLocalCanary, alignof(U) > 16 ? alignof(U) : 16), p_(::new (host()) U(static_cast<A&&>(a))) { guard(); }
    template <class... A> GuestLocal(brace_init_t, A&&... a)
        : frame_(sizeof(U) + kLocalCanary, alignof(U) > 16 ? alignof(U) : 16), p_(::new (host()) U{static_cast<A&&>(a)...}) { guard(); }
    template <class A0, class... A> requires(!std::is_same_v<std::remove_cvref_t<A0>, copy_init_t> &&
                                             !std::is_same_v<std::remove_cvref_t<A0>, brace_init_t>)
    explicit GuestLocal(A0&& a0, A&&... a)
        : frame_(sizeof(U) + kLocalCanary, alignof(U) > 16 ? alignof(U) : 16), p_(::new (host()) U(static_cast<A0&&>(a0), static_cast<A&&>(a)...)) { guard(); }
    ~GuestLocal() noexcept(false) {
        if constexpr (!std::is_trivially_destructible_v<U>) p_->~U();
        detail::local_canary_check(reinterpret_cast<const std::uint8_t*>(p_) + sizeof(U), frame_.addr(), sizeof(U));
    }
    GuestLocal(const GuestLocal&) = delete;
    GuestLocal& operator=(const GuestLocal&) = delete;
    T& operator*() const { return *p_; }
    gaddr addr() const { return frame_.addr(); }
private:
    void* host() { return host_pointer(frame_.addr(), static_cast<std::uint32_t>(sizeof(U) + kLocalCanary)); }
    void guard() { detail::local_canary_set(reinterpret_cast<std::uint8_t*>(p_) + sizeof(U)); }
    GuestStackFrame frame_;
    U* p_;
};
// gen1: Slack bytes after the array that a callee may write without tripping the canary -- a sprintf into a buffer
// that retail overflows too (func_003B78D0's "%d/" into char[4]: its NUL lands in the next stack slot on the EE)
template <class T, std::size_t N, std::size_t Slack = 0>
class GuestLocalArray {
public:
    using U = std::remove_cv_t<T>;
    GuestLocalArray() : frame_(sizeof(U) * N + Slack + kLocalCanary, alignof(U) > 16 ? alignof(U) : 16),
        p_(static_cast<U*>(host_pointer(frame_.addr(), static_cast<std::uint32_t>(sizeof(U) * N + Slack + kLocalCanary)))) {
        std::uninitialized_default_construct_n(p_, N);
        if constexpr (Slack != 0) std::memset(reinterpret_cast<std::uint8_t*>(p_ + N), 0, Slack);
        detail::local_canary_set(reinterpret_cast<std::uint8_t*>(p_ + N) + Slack);
    }
    ~GuestLocalArray() noexcept(false) {
        if constexpr (!std::is_trivially_destructible_v<U>)
            for (std::size_t i = N; i-- > 0;) p_[i].~U();
        detail::local_canary_check(reinterpret_cast<const std::uint8_t*>(p_ + N) + Slack, frame_.addr(), sizeof(U) * N + Slack);
    }
    GuestLocalArray(const GuestLocalArray&) = delete;
    GuestLocalArray& operator=(const GuestLocalArray&) = delete;
    T (&operator*() const)[N] { return *reinterpret_cast<T (*)[N]>(p_); }
    gaddr addr() const { return frame_.addr(); }
private:
    GuestStackFrame frame_;
    U* p_;
};

namespace detail {
template <class T> struct is_gp : std::false_type {};
template <class T> struct is_gp<gp<T>> : std::true_type {};
template <class T> struct is_gfn : std::false_type {};
template <class S> struct is_gfn<gfn<S>> : std::true_type {};

template <class T> struct is_ptrarg : std::false_type {};
template <class T> struct is_ptrarg<PtrArg<T>> : std::true_type {};

template <class T> using bare = std::remove_cv_t<std::remove_reference_t<T>>;

// float and ps2f (4 bytes, a float's bits) are the float class [C §1, §2].
template <class D> constexpr bool is_float_like = std::is_same_v<D, float> || std::is_same_v<D, ps2f>;

// A struct passed by value in the sense of §2 (not a type the adapter treats as a scalar).
template <class D> constexpr bool is_value_struct =
    std::is_class_v<D> && !std::is_same_v<D, u_long128> && !is_gp<D>::value && !is_gfn<D>::value &&
    !std::is_same_v<D, ps2f> && !is_ptrarg<D>::value;

enum class Cls : std::uint8_t { Gpr, Fpr, Gpr128 };
struct ArgKind {
    Cls cls = Cls::Gpr;
    bool indirect = false;     // passed as the address of a copy (struct > 8 bytes)
    std::uint32_t size = 0;    // bytes of the copy (indirect only)
};

template <class D>
constexpr ArgKind kind_of() {
    if constexpr (is_float_like<D>) return {Cls::Fpr, false, 0};
    else if constexpr (std::is_same_v<D, long double>) static_assert(!sizeof(D), "long double has no EE equivalent");
    else if constexpr (std::is_same_v<D, double>) return {Cls::Gpr, false, 0};          // soft-float bits [C §2]
    else if constexpr (std::is_same_v<D, u_long128>) return {Cls::Gpr128, false, 0};   // [C §2]
    else if constexpr (is_gp<D>::value || is_gfn<D>::value || is_ptrarg<D>::value || std::is_pointer_v<D> ||
                       std::is_null_pointer_v<D> || std::is_enum_v<D>) return {Cls::Gpr, false, 0};
    else if constexpr (std::is_integral_v<D>) {
        static_assert(sizeof(D) <= 8, "integer argument wider than a GPR");
        return {Cls::Gpr, false, 0};
    } else if constexpr (is_value_struct<D>) {
        static_assert(std::is_trivially_copyable_v<D>,
                      "struct by value that is not trivially copyable: MWCC passes it by address of a copy [C "
                      "docs/abi-mwcc-ee.md §2], but the adapter cannot byte-copy it; pass a gp<T> to a guest copy");
        static_assert(alignof(D) <= 16, "struct alignment above 16 is not established (docs/abi-mwcc-ee.md §7)");
        if constexpr (sizeof(D) > 8) return {Cls::Gpr, true, static_cast<std::uint32_t>(sizeof(D))};   // [C §2]
        else if constexpr (sizeof(D) == 4) {
            static_assert(abi::lone_float<D>::value != -1,
                          "4-byte struct by value: specialise b3host::abi::lone_float<T> (true if its only member is "
                          "a float: MWCC passes it in an FPR; false otherwise: a GPR) [C docs/abi-mwcc-ee.md §2]");
            return {abi::lone_float<D>::value ? Cls::Fpr : Cls::Gpr, false, 0};
        } else return {Cls::Gpr, false, 0};
    } else static_assert(!sizeof(D), "argument type not supported by the EE call marshalling");
}

// Where one argument lives.
enum class Where : std::uint8_t { Gpr, Fpr, Stack };
struct Slot {
    ArgKind kind{};
    Where where = Where::Gpr;
    std::uint32_t index = 0;        // register number within its class (Gpr: 0 = a0, Fpr: 0 = f12)
    std::uint32_t offset = 0;       // Stack: byte offset from the entry sp
    std::uint32_t copy_offset = 0;  // indirect: offset of the copy in call_guest's frame
};

constexpr std::uint32_t align_up(std::uint32_t v, std::uint32_t a) { return (v + a - 1) & ~(a - 1); }

// The layout of one call, computed at compile time. `Hidden`: a struct result, whose pointer takes a0 [C §4];
// `RetSize`: its size (for the frame).
template <bool Hidden, std::uint32_t RetSize, class... A>
struct Plan {
    static constexpr std::size_t N = sizeof...(A);
    struct Layout {
        std::array<Slot, N> slots{};
        std::uint32_t n_gpr = 0, n_fpr = 0;
        std::uint32_t stack_bytes = 0;   // outgoing stack argument area
        std::uint32_t result_offset = 0; // hidden result slot in the frame
        std::uint32_t frame_bytes = 0;   // everything call_guest allocates on the guest stack
    };
    static constexpr Layout compute() {
        Layout l;
        const ArgKind kinds[N + 1] = {kind_of<A>()..., ArgKind{}};
        std::uint32_t gi = Hidden ? 1 : 0, fi = 0, so = 0;
        for (std::size_t i = 0; i < N; ++i) {
            Slot& s = l.slots[i];
            s.kind = kinds[i];
            if (s.kind.cls == Cls::Fpr) {
                if (fi < 8) { s.where = Where::Fpr; s.index = fi++; continue; }
            } else if (gi < 8) { s.where = Where::Gpr; s.index = gi++; continue; }
            // [C §3] 8-byte slots in declaration order; a quadword is 16-aligned and 16 bytes
            if (s.kind.cls == Cls::Gpr128) so = align_up(so, 16);
            s.where = Where::Stack;
            s.offset = so;
            so += s.kind.cls == Cls::Gpr128 ? 16 : 8;
        }
        l.n_gpr = gi;
        l.n_fpr = fi;
        l.stack_bytes = so;
        std::uint32_t at = align_up(so, 16);
        for (std::size_t i = 0; i < N; ++i)
            if (l.slots[i].kind.indirect) { l.slots[i].copy_offset = at; at = align_up(at + l.slots[i].kind.size, 16); }
        if (Hidden) { l.result_offset = at; at = align_up(at + RetSize, 16); }
        l.frame_bytes = at;
        return l;
    }
    static constexpr Layout layout = compute();
};

inline std::uint64_t sext32(std::uint32_t v) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(v)));
}
inline u_long128 gpr64(std::uint64_t v) {
    return make_u128(static_cast<std::uint32_t>(v), static_cast<std::uint32_t>(v >> 32), 0, 0);
}
inline std::uint64_t low64(const u_long128& q) { return q.w[0] | (static_cast<std::uint64_t>(q.w[1]) << 32); }

// The register image of a scalar or small-struct argument: integer class as a 64-bit value (32-bit values
// sign-extended [C §2]), float class as 32 raw bits (in the low word).
template <class D>
inline std::uint64_t image_of(const D& v) {
    if constexpr (is_float_like<D>) return std::bit_cast<std::uint32_t>(v);
    else if constexpr (std::is_same_v<D, double>) return std::bit_cast<std::uint64_t>(v);
    else if constexpr (is_gp<D>::value || is_gfn<D>::value || is_ptrarg<D>::value) return sext32(v.raw);
    else if constexpr (std::is_pointer_v<D> || std::is_null_pointer_v<D>) return sext32(guest_address(v));
    else if constexpr (std::is_enum_v<D>) return image_of(static_cast<std::underlying_type_t<D>>(v));
    else if constexpr (std::is_same_v<D, bool>) return v ? 1u : 0u;
    else if constexpr (std::is_integral_v<D>) {
        if constexpr (sizeof(D) == 8) return static_cast<std::uint64_t>(v);
        else if constexpr (std::is_signed_v<D>) return static_cast<std::uint64_t>(static_cast<std::int64_t>(v));
        else return sext32(static_cast<std::uint32_t>(v));   // unsigned 32-bit: sign-extended like `lw` [C §2]
    } else {                                                  // small struct: its memory image, sign-extended
        static_assert(is_value_struct<D> && sizeof(D) <= 8);
        std::uint64_t r = 0;
        std::memcpy(&r, &v, sizeof(D));
        if constexpr (sizeof(D) < 8) {
            constexpr int sh = 64 - 8 * static_cast<int>(sizeof(D));
            r = static_cast<std::uint64_t>(static_cast<std::int64_t>(r << sh) >> sh);
        }
        return r;
    }
}

// The inverse: a C++ value from its register image.
template <class D>
inline D from_image(std::uint64_t b) {
    if constexpr (is_float_like<D>) return std::bit_cast<D>(static_cast<std::uint32_t>(b));
    else if constexpr (std::is_same_v<D, double>) return std::bit_cast<double>(b);
    else if constexpr (is_gp<D>::value || is_gfn<D>::value) { D p; p.raw = static_cast<gaddr>(b); return p; }
    else if constexpr (std::is_pointer_v<D>) return static_cast<D>(guest_to_host_checked(static_cast<gaddr>(b), 1));
    else if constexpr (std::is_enum_v<D>) return static_cast<D>(from_image<std::underlying_type_t<D>>(b));
    else if constexpr (std::is_same_v<D, bool>) return (b & 0xFFu) != 0;   // masked to the low byte [C §2, §4]
    else if constexpr (std::is_integral_v<D>) return static_cast<D>(b);    // truncation = the callee's andi/extend
    else {
        D v;
        std::memcpy(&v, &b, sizeof(D));
        return v;
    }
}

// Writes argument `v` (slot `s`) into the register image `a` or the frame at `base`.
template <class D>
inline void put(CallArgs& a, gaddr base, const Slot& s, const D& v) {
    if constexpr (std::is_same_v<D, u_long128>) {
        if (s.where == Where::Gpr) a.gpr[s.index] = v;
        else store<u_long128>(base + s.offset, v);
    } else {
        std::uint64_t img;
        if constexpr (is_value_struct<D> && (sizeof(D) > 8)) {
            const gaddr copy = base + s.copy_offset;
            store<D>(copy, v);             // the caller-made copy [C §2]
            img = sext32(copy);
        } else {
            img = image_of(v);
        }
        if (s.where == Where::Gpr) a.gpr[s.index] = gpr64(img);
        else if (s.where == Where::Fpr) a.fpr[s.index] = static_cast<std::uint32_t>(img);
        else if (s.kind.cls == Cls::Fpr) store<std::uint32_t>(base + s.offset, static_cast<std::uint32_t>(img)); // sw [C §3]
        else store<std::uint64_t>(base + s.offset, img);                                                       // sd [C §3]
    }
}

// Reads argument slot `s` of type D from an entry image (the inverse of put).
template <class D>
inline D get(const CallArgs& a, const Slot& s) {
    if constexpr (std::is_same_v<D, u_long128>) {
        return s.where == Where::Gpr ? a.gpr[s.index] : load<u_long128>(a.sp + s.offset);
    } else {
        std::uint64_t img;
        if (s.where == Where::Gpr) img = low64(a.gpr[s.index]);
        else if (s.where == Where::Fpr) img = a.fpr[s.index];
        else if (s.kind.cls == Cls::Fpr) img = load<std::uint32_t>(a.sp + s.offset);
        else img = load<std::uint64_t>(a.sp + s.offset);
        if constexpr (is_value_struct<D> && (sizeof(D) > 8)) return load<D>(static_cast<gaddr>(img));
        else return from_image<D>(img);
    }
}

// A struct result (not a reference to one: that is a pointer in v0).
template <class R> constexpr bool returns_indirect = !std::is_reference_v<R> && is_value_struct<bare<R>>;
template <class R> constexpr std::uint32_t ret_size() {
    if constexpr (returns_indirect<R>) {
        static_assert(std::is_trivially_copyable_v<bare<R>>, "struct result must be trivially copyable");
        return static_cast<std::uint32_t>(sizeof(R));
    } else return 0;
}

template <class R>
inline R convert_result(const CallResult& r) {
    using D = bare<R>;
    if constexpr (std::is_void_v<D>) return;
    else if constexpr (std::is_reference_v<R>) {   // a reference result is the object's address in v0
        auto* p = static_cast<std::remove_reference_t<R>*>(guest_to_host_checked(static_cast<gaddr>(r.v0.w[0]), 1));
        if (!p) fatal("b3host: a guest function returned a null reference");
        return *p;
    }
    else if constexpr (is_float_like<D>) return std::bit_cast<D>(r.f0);
    else if constexpr (std::is_same_v<D, u_long128>) return r.v0;
    else if constexpr (std::is_same_v<D, long double>) static_assert(!sizeof(D), "long double has no EE equivalent");
    else return from_image<D>(low64(r.v0));
}
template <class R>
inline void store_result(CallResult& out, const R& v) {
    using D = bare<R>;
    if constexpr (std::is_reference_v<R>) out.v0 = gpr64(sext32(guest_address(&v)));
    else if constexpr (is_float_like<D>) out.f0 = std::bit_cast<std::uint32_t>(v);
    else if constexpr (std::is_same_v<D, u_long128>) out.v0 = v;
    else out.v0 = gpr64(image_of(v));
}

// The argument registers the innermost native override was entered with (set by native_entry). A call from native
// code starts from them, so registers its own arguments do not set keep the values the override was entered with,
// as on the EE, where MWCC leaves a0-t3/f12-f19 alone unless it passes something there [C port8: the decomp declares
// some C-linkage callees with fewer parameters than they take: func_00240A60 is `func_00240E90();` (retail: `j`
// with a0/a1 untouched), whose definition reads a0 and a1]. Exact while the override has made no call yet (after a
// call, retail's argument registers hold whatever the callee left: not modelled; before port8 they were zero).
inline thread_local const CallArgs* t_incoming = nullptr;
struct IncomingScope {
    const CallArgs* saved;
    explicit IncomingScope(const CallArgs& a) : saved(t_incoming) { t_incoming = &a; }
    ~IncomingScope() { t_incoming = saved; }
    IncomingScope(const IncomingScope&) = delete;
    IncomingScope& operator=(const IncomingScope&) = delete;
};

// One call with its register/stack image: an enabled native override at `function` runs directly and its static cost
// is reported after it returns, as its thunk would; anything else goes to the backend, which runs the original (host.h,
// NativeOverride). A self-costing override (cost 0: libm) adds its exact count itself.
inline void dispatch(gaddr function, const CallArgs& a, CallResult& r) {
    if (const NativeOverride* n = native_override(function)) {
        n->nested_calls->fetch_add(1, std::memory_order_relaxed);
        n->entry(a, r);
        if (n->cost != 0) b3host::add_cost(n->cost);
        return;
    }
    backend().call(function, a, r);
}

// The call itself: lay out the frame (stack arguments, struct copies, result slot), run, convert.
template <class R, class... A>
inline R do_call(gaddr function, const A&... args) {
    using P = Plan<returns_indirect<R>, ret_size<R>(), bare<A>...>;
    constexpr const auto& L = P::layout;
    auto run = [&](gaddr base) -> R {
        CallArgs a;
        if (const CallArgs* in = t_incoming) {   // pass-through (above)
            for (unsigned i = 0; i < 8; ++i) { a.gpr[i] = in->gpr[i]; a.fpr[i] = in->fpr[i]; }
        }
        a.n_gpr = L.n_gpr;
        a.n_fpr = L.n_fpr;
        a.sp = base;
        if constexpr (returns_indirect<R>) a.gpr[0] = gpr64(sext32(base + L.result_offset));   // [C §4]
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (put(a, base, L.slots[I], args), ...);
        }(std::index_sequence_for<A...>{});
        CallResult r;
        dispatch(function, a, r);
        if constexpr (returns_indirect<R>) return load<bare<R>>(base + L.result_offset);
        else return convert_result<R>(r);
    };
    if constexpr (L.frame_bytes == 0) {
        return run(backend().stack_pointer());
    } else {
        GuestStackFrame frame(L.frame_bytes);
        return run(frame.addr());
    }
}

// Default argument promotions at a variadic call [C docs/abi-mwcc-ee.md §5]: float -> double (soft, in a GPR),
// integers narrower than int -> int.
template <class T>
inline decltype(auto) promote(const T& v) {   // native2: by reference when unchanged (a PtrArg must not be copied)
    using D = bare<T>;
    static_assert(!is_value_struct<D> && !std::is_same_v<D, u_long128>,
                  "structs and quadwords through varargs are not established (docs/abi-mwcc-ee.md §7)");
    if constexpr (std::is_same_v<D, float>) return static_cast<double>(v);
    else if constexpr (std::is_integral_v<D> && sizeof(D) < sizeof(int)) return static_cast<int>(v);
    else return v;
}

template <class F> struct fn_traits;
template <class R, class... A> struct fn_traits<R (*)(A...)> {
    using result = R;
    using args = std::tuple<bare<A>...>;
};
}  // namespace detail

// native -> original. Stack arguments, struct copies and a struct result live in a guest stack frame for the call.
template <class R = void, class... A>
inline R call_guest(gaddr function, const A&... args) {
    return detail::do_call<R>(function, args...);
}

// A pointer argument for a guest callee (generated stubs wrap every pointer and reference argument in ptr_arg).
// A pointer into a guest window is passed as its guest address. A pointer to host memory (a native caller's local:
// decomp code passes `&v`, binds a `const T&` to a local, or hands over `const void *pp = &local` the way MWCC passes a
// struct copy) is passed as the address of a copy in a guest stack frame: a window of max(sizeof(T), kBounceWindow)
// bytes from the host pointer, so arrays passed as a pointer to their first element and void* / incomplete pointees
// are covered too. After the call, unless T is const, exactly the bytes the callee changed are copied back (so bytes
// past the object, read only for the window, are never written). [L: exact unless the callee keeps the pointer after
// returning or touches more than the window; either shows as a difference in a run.]
inline constexpr std::uint32_t kBounceWindow = 256;
// port8: pointers into one host object passed in the same call share one window, so the callee sees them alias as
// they do on the EE [C GetStageTrackID 0x3E9A60 passes &local, &local+6, +7, +8 to GtIDCompress__FPCc (N298; was packName40); separate copies ended
// in "guest pointer outside RAM"]. A window starts kBounceLead bytes before its host pointer (GCC builds the argument
// temporaries right to left, so &local+8 may come before &local; rounded so the window keeps host alignment mod 16) and stays open while its owner (the first PtrArg
// that needed it) lives; a later PtrArg whose bytes lie inside an open window reuses it at the same offset.
inline constexpr std::uint32_t kBounceLead = 64;
// boot2 (2026-11-06): a callee that writes past its window would lose those writes silently (the bytes past
// the window are never copied back) and scribble on the older guest stack. Each window is followed by kBounceCanary
// bytes of a fixed pattern in the same frame; the owner checks them after the call and a change is fatal (decomp1's
// case: SoundESMHigh::Update passed one 0x50-byte output of an array to a callee that fills twelve).
// Native builds only (-DB3_BOUNCE_CANARY=1: Makefile NGENFLAGS / NATIVE_FLAGS): where retail code runs on the same
// guest stack, the canary's bytes change the leftovers that code reading uninitialised stack sees (navproof14,
// 2026-11-06)
#ifndef B3_BOUNCE_CANARY
#define B3_BOUNCE_CANARY 0
#endif
inline constexpr std::uint32_t kBounceCanary = B3_BOUNCE_CANARY ? 64 : 0;
inline constexpr std::uint8_t kBounceCanaryByte = 0xB3;
namespace detail {
// lane-noise: the alignment a bounced object's guest copy keeps (PtrArg): its type's, at most 16; 16 for void and
// incomplete types (arrays of anything, structs whose view is partial)
template <class T> constexpr std::uint32_t bounce_align() {
    if constexpr (std::is_void_v<T> || !requires { alignof(T); }) return 16u;
    else return alignof(T) < 16u ? static_cast<std::uint32_t>(alignof(T)) : 16u;
}
struct BounceWindow {
    const std::uint8_t* host;
    std::uint32_t size;
    gaddr raw;
    bool write_back;   // some non-const pointer used it
};
// trivial types only: MinGW emits a duplicate TLS init function per unit for an inline thread_local with a constructor
inline constexpr unsigned kMaxBounceWindows = 64;
inline thread_local BounceWindow t_bounce[kMaxBounceWindows];
inline thread_local unsigned t_bounce_n = 0;
}  // namespace detail
namespace detail {
// the host function -> guest code address registry (native2; see register_code below)
inline std::unordered_map<const void*, gaddr>& code_registry() {
    static std::unordered_map<const void*, gaddr> m;
    return m;
}
}  // namespace detail
template <class T>
class PtrArg {
public:
    gaddr raw = 0;
    explicit PtrArg(T* p) : host_(const_cast<std::remove_cv_t<T>*>(p)) {
        if (try_guest_address(p, raw)) return;
        // boot1: a game function cast to a data pointer (`(unsigned char *)__ct__13CB3Im3dVertexFv`, MWCC's array
        // constructor func_00100270's ctor argument in the static initialisers [C decomp func_004D3E00]) is its guest
        // code address, never a copy of host code bytes on the guest stack
        if (const auto& m = detail::code_registry(); !m.empty()) {
            const auto it = m.find(static_cast<const void*>(host_));
            if (it != m.end()) { raw = it->second; return; }
        }
        std::uint32_t need = 1;
        if constexpr (!std::is_void_v<T> && requires { sizeof(T); }) need = static_cast<std::uint32_t>(sizeof(T));
        const auto* h = static_cast<const std::uint8_t*>(static_cast<const void*>(host_));
        // lane-noise (2026-11-11): a window's guest copy starts kBounceLead bytes before the object, so its guest address
        // is 16-aligned whatever the host address, and its frame is sized from the object alone. Both used to follow
        // the host address (a lead of kBounceLead + (h & 15), a frame 16 bytes larger then): an unaligned host local
        // moved its guest address and every guest frame of the callee with the host build's stack layout, which reached
        // kernel arguments and the dead guest stack -- read later as the stale lanes of guest-stack locals [C: two builds
        // that differ only in -fno-tree-sra, race: iSignalSema's leftover a3 0x01FFF9F8 against 0x01FFF9EC, VBlank
        // 22401: 838 words in MB 31 and the scrape packets' w lanes (CB3Im3dVertex's pad word, T31) 0xB3B3B3B3 against
        // 14.0]. A later pointer into an open window shares it (aliasing as on the EE) only when its own alignment (its
        // type's, at most 16; 16 for void and incomplete types) holds there: else it gets a window of its own (lqc2
        // needs 16-byte alignment: a CGtV4d 8 bytes after a CGtV2d in one window broke RenderSpritesTex).
        constexpr std::uint32_t kAlign = detail::bounce_align<T>();
        for (unsigned k = detail::t_bounce_n; k-- > 0;) {
            detail::BounceWindow* it = &detail::t_bounce[k];
            const gaddr g = it->raw + static_cast<gaddr>(h - it->host);
            if (h >= it->host && h + need <= it->host + it->size && (g & (kAlign - 1u)) == 0) {
                raw = g;
                if constexpr (!std::is_const_v<T>) it->write_back = true;
                return;
            }
        }
        std::uint32_t lead = kBounceLead;
        std::uint32_t n = (lead + (kBounceWindow > need ? kBounceWindow : need) + 15u) & ~15u;
        const std::uint32_t frame_bytes = n;
        // mac3: a pointer into the current host stack gets a window inside that stack: a local near a stack's top must
        // not be read past its end (macOS maps nothing after the block). A clamped lead keeps the host address mod 16
        // (the bounds are page-aligned)
        const detail::StackRange st = detail::thread_stack();
        if (st.hi && h >= st.lo && h + need <= st.hi) {
            if (static_cast<std::uint32_t>(h - st.lo) < lead) {
                n -= lead - static_cast<std::uint32_t>(h - st.lo);
                lead = static_cast<std::uint32_t>(h - st.lo);
            }
            if (static_cast<std::size_t>(st.hi - (h - lead)) < n) n = static_cast<std::uint32_t>(st.hi - (h - lead));
        }
        frame_.emplace(frame_bytes + kBounceCanary);   // the window at [0, n), its canary after it, the rest left as found
        base_ = h - lead;
        size_ = n;
        // mac1: snapshot first, then give the guest that snapshot. The window covers this object too (a temporary
        // next to the local), and libc++'s vector::assign sets before_'s own pointers before it copies: snapshotting
        // after the guest copy left the two differing there, and ~PtrArg "wrote back" stale pointers into before_
        // (free(): invalid size, test_nonatomic with libc++ -- Apple's library). libstdc++ sets them after copying.
        before_.assign(base_, base_ + n);
        // gen1 (determinism, 2026-11-07): the guest bytes under the window (and its canary), put back when it closes.
        // The window is a copy of host memory around the object (kBounceLead before, kBounceWindow in all): host stack
        // neighbours, saved registers, return addresses, heap pointers. Left on the guest stack, retail code read them
        // later as its stale stack: UpdateSuspension's wheel matrix w lanes (vu0: 0x1FFF6B0 / 0x1FFF9B8 / 0x1FFFB00 held
        // 0x000078D5... heap and 0x00005E21... PIE addresses, a new value per run, ASLR off too)
        guest_under_.resize(n + kBounceCanary);
        copy_from_guest(guest_under_.data(), frame_->addr(), n + kBounceCanary);
        copy_to_guest(frame_->addr(), before_.data(), n);
        if constexpr (kBounceCanary != 0) {
            std::uint8_t canary[kBounceCanary ? kBounceCanary : 1];
            std::memset(canary, kBounceCanaryByte, kBounceCanary);
            copy_to_guest(frame_->addr() + n, canary, kBounceCanary);
        }
        raw = frame_->addr() + lead;
        if constexpr (kBounceCanary != 0) detail::note_bounce_open(n, __builtin_return_address(0));   // boot2: the per-site count (native builds)
        if (detail::t_bounce_n == detail::kMaxBounceWindows) fatal("b3host: more than 64 open ptr_arg windows");
        detail::t_bounce[detail::t_bounce_n++] = {base_, n, frame_->addr(), !std::is_const_v<T>};
    }
    ~PtrArg() noexcept(false) {
        if (!frame_) return;
        const bool wb = detail::t_bounce[--detail::t_bounce_n].write_back;   // owners end in reverse order of construction (LIFO, as their frames)
        if constexpr (kBounceCanary != 0) {
            std::uint8_t canary[kBounceCanary ? kBounceCanary : 1];
            copy_from_guest(canary, frame_->addr() + size_, kBounceCanary);
            for (std::uint32_t k = 0; k < kBounceCanary; ++k)
                if (canary[k] != kBounceCanaryByte) {
                    char m[200];
                    std::snprintf(m, sizeof m, "b3host: a guest callee wrote past a ptr_arg bounce window (guest %08X, %u "
                                  "bytes, host %p; byte +%u past it): the host object is larger than the window -- make "
                                  "the caller's local a guest-stack local (T27 / T31)", frame_->addr(), size_,
                                  static_cast<const void*>(base_), k);
                    if (!std::uncaught_exceptions()) fatal(m);
                    break;
                }
        }
        if (wb) {
            std::vector<std::uint8_t> after(size_);
            copy_from_guest(after.data(), frame_->addr(), size_);
            auto* h = const_cast<std::uint8_t*>(base_);
            for (std::uint32_t k = 0; k < size_; ++k)
                if (after[k] != before_[k]) h[k] = after[k];
        }
        copy_to_guest(frame_->addr(), guest_under_.data(), static_cast<std::uint32_t>(guest_under_.size()));   // gen1: no host bytes stay
    }
    PtrArg(const PtrArg&) = delete;
    PtrArg& operator=(const PtrArg&) = delete;
private:
    std::remove_cv_t<T>* host_;
    const std::uint8_t* base_ = nullptr;
    std::uint32_t size_ = 0;
    std::optional<GuestStackFrame> frame_;
    std::vector<std::uint8_t> before_;
    std::vector<std::uint8_t> guest_under_;   // gen1: the guest stack bytes the window covered
};
template <class T> inline PtrArg<T> ptr_arg(T* p) { return PtrArg<T>(p); }

// A struct parameter by value of a generated wrapper (decomp2host), from its integer-class register image
// [C docs/abi-mwcc-ee.md §2]: > 8 bytes, the address of the caller's copy; <= 8 bytes, the memory image itself.
// A 4-byte struct may instead be a lone float in an FPR (§2): not decidable here, so refused.
template <class T>
inline T by_value(std::uint64_t image) {
    if constexpr (detail::is_gfn<T>::value) {
        return T::from_raw(static_cast<gaddr>(image));   // native2: a guest function pointer typedef (T14), 4 bytes
    } else {
    // native2: a 4-byte struct is taken from the GPR when decomp2host decided it is not a lone float (lone_float == 0)
    static_assert(sizeof(T) != 4 || abi::lone_float<T>::value == 0,
                  "by_value: a 4-byte struct may be a lone float (FPR), see abi::lone_float");
    if constexpr (sizeof(T) > 8) {
        return load<T>(static_cast<gaddr>(image));
    } else {
        T v;
        std::memcpy(&v, &image, sizeof(T));
        return v;
    }
    }
}

// ---- guest code addresses of host functions (native2) ----
// Decomp code hands game functions to guest code as values: qsort comparators, CGt2dObject render callbacks
// (`mpRenderCallback = &_Render`), FSM tables. On the host such a value is a host function pointer: the call stub
// of a guest function (decomp2host registers each free / static stub with its guest address), guest_fn<..>::call of a
// C name (registered on first instantiation), or a kept native definition (registered beside its wrapper). Handing an
// unregistered host function to guest code is fatal (with its host address), never a guess.
inline bool register_code(const void* host, gaddr guest) {
    detail::code_registry().emplace(host, guest);
    return true;
}
inline gaddr code_address_of(const void* host_function) {   // declared in gp.h (icast)
    if (host_function == nullptr) return 0;
    const auto& m = detail::code_registry();
    const auto it = m.find(host_function);
    if (it == m.end()) fatal("b3host: a host function with no guest code address cast to an integer");
    return it->second;
}
template <class F> requires std::is_function_v<F>
inline gaddr code_address(F* f) {
    if (f == nullptr) return 0;
    const auto& m = detail::code_registry();
    const auto it = m.find(reinterpret_cast<const void*>(f));
    if (it == m.end()) fatal("b3host: a host function with no guest code address handed to guest code");
    return it->second;
}

// One argument of a generated call stub, by the callee's declared parameter type P (decltype of the stub's
// parameter): pointers and references go through ptr_arg (a reference is its object's address [C abi-mwcc-ee.md
// §2]), everything else by value as call_guest marshals it. A host function pointer cannot be handed to guest code.
template <class P, class V>
inline decltype(auto) stub_arg(V& v) {
    using D = std::remove_reference_t<P>;
    if constexpr (std::is_pointer_v<D> && std::is_function_v<std::remove_pointer_t<D>>)
        return static_cast<std::uint32_t>(code_address(static_cast<D>(v)));   // native2: its guest code address (above)
    else if constexpr (std::is_reference_v<P>) return PtrArg<std::remove_reference_t<P>>(&v);
    else if constexpr (std::is_pointer_v<D>) return PtrArg<std::remove_pointer_t<D>>(v);
    else return static_cast<const D&>(v);
}

// A guest function called under its C name (the decomp's `extern "C" R Name__...(...)` declarations): decomp2host
// defines `#define Name (b3host::guest_fn<decltype(Name), 0xADDR>::call)` after the declaration, so every use calls
// the guest function (or its native override) with the declared parameter types.
template <class Sig, gaddr Addr> struct guest_fn;
// gen1: declared before guest_fn's variadic form uses it (defined below): a dependent call, but its arguments are
// b3host values and gaddr, so argument-dependent lookup at instantiation found nothing (B3PlayerCameraStatePlayer
// 0x447000's printf-style callee did not compile at -O2)
template <class R = void, class... A>
inline R call_guest_varargs(gaddr function, const A&... args);
template <class R, class... A, gaddr Addr>
struct guest_fn<R(A...), Addr> {
    static R call(A... a) { (void)reg; return call_guest<R>(Addr, stub_arg<A>(a)...); }
    static inline const bool reg = register_code(reinterpret_cast<const void*>(&call), Addr);   // native2
};
// gen1: a T18 open_array member passed to `...` (`snprintf(buf, n, fmt, t->field_17C)`, cbFormatAuthMessage 0x344E10)
// decays to its element pointer as the C array it stands for does; taken by value it was a 1-byte struct copy
// (only calls with such an argument take the forwarding path: by value, as before, a bit-field can still be passed)
namespace detail {
template <class T> struct is_open_array : std::false_type {};
template <class T> struct is_open_array<open_array<T>> : std::true_type {};
template <class... V> inline constexpr bool any_open_array = (is_open_array<std::remove_cvref_t<V>>::value || ...);
template <class T> inline T* vararg_decay(open_array<T>& x) { return x.data(); }
template <class T> inline const T* vararg_decay(const open_array<T>& x) { return x.data(); }
template <class X> inline X&& vararg_decay(X&& x) { return static_cast<X&&>(x); }
}  // namespace detail
template <class R, class... A, gaddr Addr>
struct guest_fn<R(A..., ...), Addr> {
    template <class... V> requires(!detail::any_open_array<V...>) static R call(A... a, V... v) {
        return call_guest_varargs<R>(Addr, stub_arg<A>(a)..., stub_arg<V>(v)...);
    }
    template <class... V> requires(detail::any_open_array<V...>) static R call(A... a, V&&... v) {
        return call_decayed(a..., detail::vararg_decay(static_cast<V&&>(v))...);
    }
    template <class... W> static R call_decayed(A... a, W... w) {
        return call_guest_varargs<R>(Addr, stub_arg<A>(a)..., stub_arg<W>(w)...);
    }
};
// native -> a variadic original (the printf family): every argument gets the default promotions, so a float named
// parameter would be passed wrongly; use call_guest for callees with named float parameters.
template <class R, class... A>
inline R call_guest_varargs(gaddr function, const A&... args) {
    return detail::do_call<R>(function, detail::promote(args)...);
}

template <class R, class... A>
struct gfn<R(A...)> {
    gaddr raw;
    gfn() = default;
    constexpr gfn(std::nullptr_t) : raw(0) {}
    // `f = 0` (a null pointer constant in decomp code), as gp<T>
    template <class I> requires std::is_integral_v<I> constexpr gfn(I zero) : raw(static_cast<gaddr>(zero)) {}
    // native2: a host function as a guest function pointer (its registered guest code address, above); MWCC takes any
    // function's address for a callback slot whatever its declared signature
    template <class F> requires std::is_function_v<F> gfn(F* f) : raw(code_address(f)) {}
    static constexpr gfn from_raw(gaddr a) { gfn f(nullptr); f.raw = a; return f; }
    // gen1: a cast between two function-pointer types (`((GameCB)p0)(...)`, CGtLobbyPS2DirtySock::ForAllGamesFound: the
    // decomp's callback typedef and parameter disagree) is the same guest code address, as in C; explicit only
    template <class S> requires(!std::is_same_v<gfn<S>, gfn>) explicit constexpr gfn(gfn<S> o) : raw(o.raw) {}
    explicit constexpr operator bool() const { return raw != 0; }
    // through call_guest: an enabled native override at the target runs directly, else the original (dispatch)
    R operator()(A... args) const { return call_guest<R>(raw, stub_arg<A>(args)...); }
    friend constexpr bool operator==(gfn a, gfn b) { return a.raw == b.raw; }
    friend constexpr bool operator==(gfn a, std::nullptr_t) { return a.raw == 0; }
    template <class I> requires std::is_integral_v<I>
    friend constexpr bool operator==(gfn a, I b) { return a.raw == static_cast<gaddr>(b); }
};
static_assert(sizeof(gfn<void()>) == 4 && std::is_trivially_copyable_v<gfn<void()>>);

// ---- virtual calls on objects built by original code ----
// [C] decomp docs/known.md N119: MWCC puts the vptr where the class's first virtual is declared, and the first slot
// is at vtable + 8 (component vtables are {0, 0, Update, Release}, N102). `slot_offset` is the byte offset into the
// vtable, the number in the decomp's vfunc_XX placeholder names; vslot(n) gives the n-th declared virtual.
// `this` is passed unchanged (the object's start, whatever vptr_offset is) [C SetActive__10CB3RaceCarFb @ 0x150EFC].
// No this-adjustment is ever needed for this binary: retail has no secondary vtables or adjusting thunks
// [C docs/abi-mwcc-ee.md §6, scan of the whole .vtables range].
constexpr std::uint32_t vslot(unsigned index) { return 8u + 4u * index; }
inline gaddr vtable_entry(gaddr object, std::uint32_t vptr_offset, std::uint32_t slot_offset) {
    const gaddr vtable = load<std::uint32_t>(object + vptr_offset);
    return load<std::uint32_t>(vtable + slot_offset);
}
template <class R = void, class... A>
inline R vcall(gaddr object, std::uint32_t vptr_offset, std::uint32_t slot_offset, const A&... args) {
    return call_guest<R>(vtable_entry(object, vptr_offset, slot_offset), gp<void>::from_raw(object), args...);
}
// The same for a generated stub of a de-virtualised decomp method (decomp2host T13): `self` is the method's `this`
// (a host pointer into guest memory, bounced if it is a host local), args are already stub_arg-mapped.
template <class R = void, class T, class... A>
inline R vcall_this(T* self, std::uint32_t vptr_offset, std::uint32_t slot_offset, const A&... args) {
    const PtrArg<T> s(self);
    return call_guest<R>(vtable_entry(s.raw, vptr_offset, slot_offset), s, args...);
}

// ---- original -> native (override thunks) ----
// native_entry<&Fn> has the backend-neutral override signature; a backend adapts it to its own handler type
// (an interpreter backend: read a0..t3/f12..f19 and sp into CallArgs, run, write v0/f0, set pc = ra).
// Fn's parameters are read with the same layout call_guest writes (stack arguments from CallArgs::sp). A struct
// result is stored through the hidden pointer in a0 (a recorded store) and v0 is left alone, as MWCC's callees do
// [C docs/abi-mwcc-ee.md §4]. Variadic overrides are not supported.
using NativeEntry = void (*)(const CallArgs&, CallResult&);
template <auto Fn>
inline void native_entry(const CallArgs& args, CallResult& result) {
    const detail::IncomingScope incoming(args);
    using T = detail::fn_traits<decltype(Fn)>;
    using Rt = typename T::result;
    [&]<class... P>(std::tuple<P...>*) {
        using Pl = detail::Plan<detail::returns_indirect<Rt>, detail::ret_size<Rt>(), P...>;
        constexpr const auto& L = Pl::layout;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            std::tuple<P...> params{detail::get<P>(args, L.slots[I])...};   // braced init: read left to right
            if constexpr (std::is_void_v<Rt>) std::apply(Fn, params);
            else if constexpr (detail::returns_indirect<Rt>)
                store<detail::bare<Rt>>(args.gpr[0].w[0], std::apply(Fn, params));
            else detail::store_result<Rt>(result, std::apply(Fn, params));
        }(std::index_sequence_for<P...>{});
    }(static_cast<typename T::args*>(nullptr));
}

// One override: replaces the original function at `address` (whose code is `code_bytes` long with FNV-1a 64
// `code_fnv1a64`, so a different game revision is never patched) by `entry`. `cost` = guest instructions reported per
// call [L: the retail function's static instruction count is a safe ceiling].
struct Override {
    gaddr address;
    std::uint32_t code_bytes;
    std::uint64_t code_fnv1a64;
    const char* name;            // the decomp's symbol name (mangled), for logs and disable lists
    std::uint32_t cost;
    NativeEntry entry;
};

}  // namespace b3host

#endif
