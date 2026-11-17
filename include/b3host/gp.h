// b3host/gp.h -- gp<T>: a 32-bit guest pointer usable as a struct field, and gfn<Sig>: a guest function pointer.
//
// Objects shared with original code keep the PS2 layout, so a pointer field must stay 4 bytes on a 64-bit host.
// gp<T> stores the guest address and converts through the bound backend's windows (b3host/host.h).
//
// Relation to the decomp's include/b3/B3Ptr.h (host branch): B3_PTR(T) is B3GuestPtr<T>, which converts through
// `void* B3GuestToHost(unsigned)` / `unsigned B3HostToGuest(const void*)`. src/b3host/host.cpp defines those two over
// the same windows, so decomp headers compiled for the port work unchanged; gp<T> is the richer port-side type
// (typed loads/stores, arithmetic, casts) with the same 4-byte layout. A later decomp header round may map
// B3_PTR(T) to gp<T> directly.
//
// Rules:
// - The raw value is kept as read from guest memory (aliases such as 0x20xxxxxx survive a copy). Assigning a host
//   T* canonicalizes it (main RAM -> 0x00xxxxxx, scratchpad -> 0x7000xxxx).
// - get()/operator-> give a raw host pointer: fine for reads. A *store* through it bypasses the backend's store
//   bookkeeping, so it must be followed by note_store(), or use gp<T>::store / b3host::store. (Open question:
//   make member stores tracked automatically.)
// - get() of an address outside the windows (MMIO, VU memory) is a HostError: use load/store for those.
// - Pointer arithmetic steps by sizeof(T), which must equal the guest size of T (check with B3_CHECK_SIZE).
#ifndef B3HOST_GP_H
#define B3HOST_GP_H

#include "b3host/host.h"
#include "b3hw/hw.h"

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace b3host {

namespace detail {
inline void* guest_to_host_checked(gaddr address, std::uint32_t len) {
    if (address == 0) return nullptr;
    void* p = host_pointer(address, len);
    if (!p) fatal_guest_pointer(address, len);
    return p;
}
template <class T> constexpr std::uint32_t deref_size() {
    if constexpr (std::is_void_v<T>) return 1;
    else if constexpr (requires { sizeof(T); }) return static_cast<std::uint32_t>(sizeof(T));
    else return 1;   // an incomplete type (a unit's forward declaration): only the first byte is range-checked
}
}  // namespace detail

// T18 (decomp2host, port13): a member array the decomp declares with bound 1 (`T field_0[1];`, its form for an array
// of unknown length) is indexed past that bound, as flat memory, by MWCC. To GCC such an index is undefined unless the
// array is the last member (-fstrict-flex-arrays=0 covers only that case), and it uses that: in
// CAsyncLoadManager::Update (0x13D250) it took `field_0[a]` to be `field_0[h]` and forwarded a value it had just
// stored, so the loop that looks for the next queued request never read one (retail 0x13D3D8 reads every entry).
// open_array<T> has T[1]'s layout; indexing and decay go through a pointer the compiler cannot trace back to the
// one-element array.
template <class T>
struct open_array {
    T e[1];
    T* data() { T* p = e; __asm__("" : "+r"(p)); return p; }
    const T* data() const { const T* p = e; __asm__("" : "+r"(p)); return p; }
    template <class I> T& operator[](I i) { return data()[i]; }
    template <class I> const T& operator[](I i) const { return data()[i]; }
    operator T*() { return data(); }
    operator const T*() const { return data(); }
};
template <class T>
struct gp {
    using element_type = T;
    gaddr raw;   // public so gp<T> stays an aggregate-compatible, standard-layout 4-byte field

    gp() = default;                                   // trivial: uninitialized like a C pointer field
    constexpr gp(std::nullptr_t) : raw(0) {}
    gp(T* host) : raw(guest_address(host)) {}         // implicit, as assigning a pointer
    template <class U> gp(open_array<U>& a) : gp(static_cast<T*>(a.data())) {}   // T18: decays as U[1] would
    template <class U> gp(const open_array<U>& a) : gp(static_cast<T*>(a.data())) {}
    // `p = 0`: the literal 0 is a null pointer constant in decomp code (an exact match beats gp(T*)/gp(nullptr_t))
    template <class I> requires std::is_integral_v<I> constexpr gp(I zero) : raw(static_cast<gaddr>(zero)) {}
    // boot2: C's implicit T* -> void* (func_00131990 `self->field_2E8 = D_006662DC + 0x300;`, a void* field)
    template <class U> requires(std::is_void_v<T> && !std::is_void_v<U>) gp(gp<U> o) : raw(o.raw) {}
    // gen1: C's implicit T* -> const T* (`self->field_10[0].field_0 = self->field_338[0];`, func_00454D80): the same
    // pointee type with more cv-qualifiers, the same address
    template <class U> requires(!std::is_void_v<T> && !std::is_same_v<U, T> &&
                                std::is_same_v<std::remove_cv_t<U>, std::remove_cv_t<T>> && std::is_convertible_v<U*, T*>)
    constexpr gp(gp<U> o) : raw(o.raw) {}
    // boot2: a function returning `const char *` behind a guest-ABI wrapper (GetFileNameFromDeviceName returns a
    // pointer into its argument): explicit only, so const is never dropped implicitly
    template <class U> requires(std::is_void_v<T>) explicit gp(const U* host) : raw(guest_address(host)) {}
    static constexpr gp from_raw(gaddr address) { gp p(nullptr); p.raw = address; return p; }

    constexpr gaddr addr() const { return raw; }
    T* get() const { return static_cast<T*>(detail::guest_to_host_checked(raw, detail::deref_size<T>())); }
    // Dereferencing through the guest pointer itself (->, *, []) reads guest memory at its address, address 0
    // included: retail code that follows a null pointer reads low RAM without faulting (SetUpTrackExtents__8CB3World
    // reads field_12ACF4[0] before any track is loaded, port6 capture), and so does this. get() and the conversion
    // to T* keep null as nullptr, so null checks in decomp code behave.
    T* deref() const {
        void* p = host_pointer(raw, detail::deref_size<T>());
        if (!p) fatal_guest_pointer(raw, detail::deref_size<T>());
        return static_cast<T*>(p);
    }
    T* operator->() const requires(!std::is_void_v<T>) { return deref(); }
    template <class U = T> requires(!std::is_void_v<U>) U& operator*() const { return *deref(); }
    operator T*() const { return get(); }             // implicit, as the decomp's B3GuestPtr
    explicit constexpr operator bool() const { return raw != 0; }

    // Typed access through the backend (recorded stores; also works for MMIO addresses).
    template <class U = T> requires(!std::is_void_v<U>) U load() const { return b3host::load<U>(raw); }
    template <class U = T> requires(!std::is_void_v<U>) void store(const U& v) const { b3host::store<U>(raw, v); }
    // A field of the pointee, by byte offset (layouts recovered as offsets): gp<Foo>(p).field<float>(0x34).
    template <class F> gp<F> field(std::uint32_t offset) const { return gp<F>::from_raw(raw + offset); }

    template <class U> constexpr gp<U> cast() const { return gp<U>::from_raw(raw); }

    template <class U = T> requires(!std::is_void_v<U>) constexpr gp operator+(std::ptrdiff_t n) const {
        return from_raw(raw + static_cast<gaddr>(n * static_cast<std::ptrdiff_t>(sizeof(U))));
    }
    template <class U = T> requires(!std::is_void_v<U>) constexpr gp operator-(std::ptrdiff_t n) const {
        return from_raw(raw - static_cast<gaddr>(n * static_cast<std::ptrdiff_t>(sizeof(U))));
    }
    template <class U = T> requires(!std::is_void_v<U>) gp& operator++() { raw += sizeof(U); return *this; }
    template <class U = T> requires(!std::is_void_v<U>) gp operator++(int) { gp o = *this; raw += sizeof(U); return o; }   // boot2
    // gen1: compound assignment and decrement (`s->field_38 += d;`, `*(farFogPlane.field_0 += 13) = v;`): guest-address
    // arithmetic, wrapping like the EE's addu
    template <class U = T> requires(!std::is_void_v<U>) gp& operator+=(std::ptrdiff_t n) {
        raw += static_cast<gaddr>(n * static_cast<std::ptrdiff_t>(sizeof(U))); return *this;
    }
    template <class U = T> requires(!std::is_void_v<U>) gp& operator-=(std::ptrdiff_t n) {
        raw -= static_cast<gaddr>(n * static_cast<std::ptrdiff_t>(sizeof(U))); return *this;
    }
    template <class U = T> requires(!std::is_void_v<U>) gp& operator--() { raw -= sizeof(U); return *this; }
    template <class U = T> requires(!std::is_void_v<U>) gp operator--(int) { gp o = *this; raw -= sizeof(U); return o; }
    template <class U = T> requires(!std::is_void_v<U>) U& operator[](std::ptrdiff_t i) const { return *(*this + i).deref(); }

    friend constexpr bool operator==(gp a, gp b) { return a.raw == b.raw; }
    // gen1: two guest pointers of different pointee types (`arr[j] == e->field_C`, gp<void> against gp<T>: 0x1B7F30 was
    // ambiguous between the conversions), the same address as C's pointer comparison after its conversion to void *
    template <class U> requires(!std::is_same_v<std::remove_cv_t<U>, std::remove_cv_t<T>>)
    friend constexpr bool operator==(gp a, gp<U> b) { return a.raw == b.raw; }
    friend constexpr bool operator==(gp a, std::nullptr_t) { return a.raw == 0; }
    template <class I> requires std::is_integral_v<I>
    friend constexpr bool operator==(gp a, I b) { return a.raw == static_cast<gaddr>(b); }   // `p == 0`
    // a guest pointer against a host pointer (decomp code compares a member with a local): the same object?
    friend bool operator==(gp a, const T* b) {
        if (a.raw == 0 || b == nullptr) return a.raw == 0 && b == nullptr;
        return host_pointer(a.raw, 1) == static_cast<const void*>(b);
    }
    // mac1: the same comparison for any host pointer that converts to const T* (T* itself, or any U* when T is void),
    // as an exact match. Without it `gp<T> == T*` is ambiguous in ISO C++ between the operator above (the qualification
    // conversion) and the built-in T* == T* (through operator T*): GCC accepts it ("worst conversion is better") and
    // picks the operator above, clang rejects it. This template is what GCC already picked, for both compilers.
    template <class U> requires(std::is_convertible_v<U*, const T*>)
    friend bool operator==(gp a, U* b) { return a == static_cast<const T*>(b); }
};

// A guest global (data at a fixed guest address) as an lvalue: decomp2host.py turns `extern T D_0064D364;` into
// `#define D_0064D364 (b3host::guest_global<T>(0x0064D364u))` (docs/decomp-on-host.md).
template <class T>
inline T& guest_global(gaddr address) {
    return *static_cast<T*>(detail::guest_to_host_checked(address, detail::deref_size<T>()));
}

template <class S> struct is_open_array : std::false_type {};
template <class U> struct is_open_array<open_array<U>> : std::true_type {};

// T9 (decomp2host): C-style casts between integers and pointers in decomp code. On the EE a pointer is its 32-bit
// guest address, so `(int)p` is that address and `(T *)i` the object at guest address i. decomp2host rewrites
// `(int)x` -> icast<int>(x) and `(T *)x` -> pcast<T>(x) for every integer/pointer target type; the source type then
// picks the meaning at compile time (a plain C cast for every other combination).
template <class S> struct is_gp : std::false_type {};
template <class U> struct is_gp<gp<U>> : std::true_type {};
// boot2: `(T *&)x` -> ref_cast<T>(x): the pointer object x re-typed as a pointer to T (on the EE the same 4 bytes;
// CB3SoundDistortion::Construct's `*(((int *&)field_1D04)++) = 0x8000` steps a struct pointer member by an int). A gp<U>
// lvalue becomes gp<T>& over the same storage; a host pointer lvalue a T*&.
template <class P, class S> inline decltype(auto) ref_cast(S& s) {
    if constexpr (is_gp<std::remove_cv_t<S>>::value) return reinterpret_cast<gp<P>&>(s);
    else return reinterpret_cast<P*&>(s);
}

// boot1: the guest code address of a host function (call.h's registry; fatal when unregistered)
inline gaddr code_address_of(const void* host_function);
template <class I, class S> inline I icast(const S& s) {
    using D = std::remove_cv_t<S>;
    // boot1: `(T)Function` / `(T)&Function` of a game function is its guest code address (RwEngineInit stores
    // MallocWrapper into a callback slot through a cast)
    if constexpr (std::is_function_v<D>) return static_cast<I>(code_address_of(reinterpret_cast<const void*>(&s)));
    else if constexpr (std::is_pointer_v<D> && std::is_function_v<std::remove_pointer_t<D>>)
        return static_cast<I>(code_address_of(reinterpret_cast<const void*>(s)));
    else if constexpr (std::is_pointer_v<D>) return static_cast<I>(guest_address(s));      // host pointer -> guest address
    // an array (S is deduced as the array type, not decayed) is the guest address of its first element (port13: `(int)
    // field_48` with `unsigned char field_48[0x14]` in CGtSoundManager::Construct 0x385160 fell to the C cast below and
    // stored the low half of a host address, which the voice manager kept as its listener manager)
    else if constexpr (std::is_array_v<D>) return static_cast<I>(guest_address(&s[0]));
    else if constexpr (is_open_array<D>::value) return static_cast<I>(guest_address(s.data()));
    else if constexpr (is_gp<D>::value) return static_cast<I>(s.raw);
    else if constexpr (std::is_same_v<D, u_long128>)                                   // MWCC: the low bits
        return static_cast<I>(s.w[0] | (static_cast<std::uint64_t>(s.w[1]) << 32));
    else return (I)(s);
}
template <class P, class S> inline P* pcast(const S& s) {
    using D = std::remove_cv_t<S>;
    // boot2: `(void *)&Function` of a game function (CB3DebugManager::Construct 0x130B80 stores
    // RenderPerfMonDataCallback into a guest pointer) is its guest code address, as icast's rule above: the code's
    // bytes in guest memory (the image holds the text), so a later gp<> of it is that address
    if constexpr (std::is_function_v<D>) return (P*)host_pointer(code_address_of(reinterpret_cast<const void*>(&s)));
    else if constexpr (std::is_pointer_v<D> && std::is_function_v<std::remove_pointer_t<D>>)
        return (P*)host_pointer(code_address_of(reinterpret_cast<const void*>(s)));
    else if constexpr (std::is_pointer_v<D> || std::is_null_pointer_v<D>) return (P*)(s);
    else if constexpr (is_open_array<D>::value) return (P*)(s.data());
    else if constexpr (is_gp<D>::value) return (P*)(s.get());
    else if constexpr (std::is_integral_v<D> || std::is_enum_v<D>)                     // guest address -> host pointer
        // zero-length check: one past the end of a window is a valid C pointer (func_002294D0 forms
        // `(T *)0x70004000 - n * n`, the scratchpad's end, and indexes back from it)
    {
        // boot2: an address outside the windows becomes a foreign pointer (host.h): formed freely, as on the EE,
        // faulting only when accessed (was fatal here: `(void *)-9999` in CB3DebrisArray::Prepare 0x2F4940)
        const gaddr a = static_cast<gaddr>(s);
        if (a == 0) return nullptr;
        if (void* p = host_pointer(a, 0)) return static_cast<P*>(p);
        return static_cast<P*>(detail::foreign_pointer(a));
    }
    else return (P*)(s);
}

// gen1: `(T *)i + k` with i an integer (RenderBeforeVehicles 0x21C780's `(char *)s + 0x70002000`, the scratchpad window;
// func_002F34C0): on the EE the guest address i + k * sizeof(T), then the pointer. pcast<T>(i) + k was host pointer
// arithmetic from i's host mapping, which leaves the window (RAM and the scratchpad are separate host mappings): a wild
// pointer. decomp2host rewrites `pcast<T>(E) + K` / `- K` (K a literal) into this; E a pointer keeps host arithmetic.
template <class P, class S, class K> inline P* pcast_add(const S& s, K k) {
    using D = std::remove_cv_t<S>;
    if constexpr ((std::is_integral_v<D> || std::is_enum_v<D>) && !std::is_same_v<D, bool>) {
        using E = std::conditional_t<std::is_void_v<P>, char, P>;
        return pcast<P>(static_cast<gaddr>(static_cast<gaddr>(s) + static_cast<gaddr>(k) * static_cast<gaddr>(sizeof(E))));
    } else {
        return pcast<P>(s) + k;
    }
}

// T37 (decomp2host, boot2): `*(volatile T *)X` with X an address value (not `&object`): the decomp's direct hardware
// access (vibuf_00326770's `#define HW(a) (*(volatile unsigned int *)(a))`: the toIPU DMA channel 0x1000B4x0; the VIF0
// channel stores `*(volatile unsigned int *)0x10008000 = 0x145`). A reference to guest address X that decides per
// access: RAM / scratchpad is plain (volatile) memory; anything else one b3hw_read / b3hw_write at the physical
// address, as port-track C does (include/b3hw/hw.h): the hardware model (native) or the backend's own slow path,
// traced.
template <class T>
class mmio_ref {
    static_assert((std::is_integral_v<T> || std::is_floating_point_v<T>) && (sizeof(T) == 4 || sizeof(T) == 8 || sizeof(T) == 1 || sizeof(T) == 2),
                  "mmio_ref: a 1, 2, 4 or 8 byte scalar");
public:
    explicit mmio_ref(gaddr a) : a_(a) {}
    operator T() const {
        if (void* p = host_pointer(a_, sizeof(T))) { T v; std::memcpy(&v, p, sizeof(T)); return v; }
        if constexpr (sizeof(T) < 4) fatal("b3host::mmio_ref: a sub-word hardware load (not modelled)");
        const std::uint64_t r = b3hw_read(a_ & 0x1FFFFFFFu, sizeof(T) == 8 ? 8 : 4);
        T v;
        std::memcpy(&v, &r, sizeof(T));
        return v;
    }
    const mmio_ref& operator=(T v) const {
        if (void* p = host_pointer(a_, sizeof(T))) { std::memcpy(p, &v, sizeof(T)); note_store(a_, sizeof(T)); return *this; }
        if constexpr (sizeof(T) < 4) fatal("b3host::mmio_ref: a sub-word hardware store (not modelled)");
        std::uint64_t r = 0;
        std::memcpy(&r, &v, sizeof(T));
        b3hw_write(a_ & 0x1FFFFFFFu, sizeof(T) == 8 ? 8 : 4, r, 0);
        return *this;
    }
    const mmio_ref& operator=(const mmio_ref& o) const { return *this = static_cast<T>(o); }
    template <class U> const mmio_ref& operator|=(U v) const { return *this = static_cast<T>(static_cast<T>(*this) | v); }
    template <class U> const mmio_ref& operator&=(U v) const { return *this = static_cast<T>(static_cast<T>(*this) & v); }
    template <class U> const mmio_ref& operator^=(U v) const { return *this = static_cast<T>(static_cast<T>(*this) ^ v); }
    template <class U> const mmio_ref& operator+=(U v) const { return *this = static_cast<T>(static_cast<T>(*this) + v); }
    template <class U> const mmio_ref& operator-=(U v) const { return *this = static_cast<T>(static_cast<T>(*this) - v); }
private:
    gaddr a_;
};
template <class T, class S> inline mmio_ref<std::remove_cv_t<T>> mmio(const S& address) {
    if constexpr (std::is_integral_v<S> || std::is_enum_v<S>) return mmio_ref<std::remove_cv_t<T>>(static_cast<gaddr>(address));
    else if constexpr (is_gp<std::remove_cv_t<S>>::value) return mmio_ref<std::remove_cv_t<T>>(address.raw);
    else return mmio_ref<std::remove_cv_t<T>>(guest_address(address));   // a host pointer into the windows
}

// T19 (decomp2host, port14): `(P *)&x[i]`, the address of an element, which retail computes without touching memory
// (Fixup__16CB3TriggerStructFUi 0x269D20 relocates members by adding a base: `lw; addu; sw`). Through a guest pointer
// it is guest-address arithmetic (wrapping like the EE's addu; no range check, since nothing is read), giving a
// gp<P> (it converts to P* where one is needed, checked then); anything else keeps the plain form.
template <class P, class A, class I> inline auto pcast_elem(A&& a, I i) {
    using D = std::remove_cvref_t<A>;
    if constexpr (is_gp<D>::value) {
        using T = std::remove_cv_t<typename D::element_type>;
        return gp<P>::from_raw(static_cast<gaddr>(a.raw + static_cast<gaddr>(static_cast<std::int64_t>(i) * static_cast<std::int64_t>(sizeof(T)))));
    } else {
        return pcast<P>(&a[i]);
    }
}

static_assert(sizeof(gp<int>) == 4 && alignof(gp<int>) == 4, "gp<T> must keep the PS2's 4-byte pointer layout");
static_assert(std::is_trivially_copyable_v<gp<int>> && std::is_standard_layout_v<gp<int>>);
static_assert(std::is_trivially_default_constructible_v<gp<int>>);
static_assert(sizeof(gp<void>) == 4);
static_assert(sizeof(open_array<int>) == 4 && alignof(open_array<double>) == alignof(double));
static_assert(std::is_trivially_copyable_v<open_array<int>> && std::is_standard_layout_v<open_array<int>>);

}  // namespace b3host

#endif
