// b3host/split_array.h -- the game's per-car arrays past their 6 elements (the more rivals trial).
//
// The game sizes its per-car arrays for 6 cars, inline in its objects (the world's car list, each race car's score,
// the physics manager, the traffic system ...): a 7th element lands on the next field. tools/native/split_arrays.py
// swaps the generated units' declarations of those arrays (config/more_rivals.tsv, found by rivals_census.py) for
// split_array<T, N, S>: the same layout as T[S] (S, the unit's declared size, may differ from N), the same flat memory for every index while
// the split is off (the game's own behaviour, out-of-range reads included), and while it is on the elements N.. in a
// side table keyed by the array's guest address. It does not decay to T*: every use that is not a subscript is a
// compile error, so the pass's census is checked by the compiler.
//
// The side storage lives on the host: zeroed at a race's start (reset()), deterministic (filled in the game's order).
#ifndef B3HOST_SPLIT_ARRAY_H
#define B3HOST_SPLIT_ARRAY_H

#include "b3host/call.h"
#include "b3host/gp.h"
#include "b3host/host.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>

namespace b3host {

namespace split {
inline bool on = false;              // the trial's option (more cars than 6 in this race)
inline std::uint64_t lookups = 0;    // side reads and writes (the log)
// The side storage is a shadow in guest memory 32 MB up: element N + k of an array at guest address A lives at
// A + k * sizeof(T) + kShadow, the shadow of the array's own element k (guest RAM is 65 MB while the trial's option is
// set, ram_bytes()). Arrays do not overlap in place, so their shadows do not either (with at most N extra elements;
// the flat address A + (N + k) * sizeof(T) would put a long array's overflow on the next array's), and an element's
// address is a real guest address the game may keep. A view of an array from element m (a global at list + 4) passes
// m so it meets the same bytes. The scratchpad's arrays have theirs at kShadowSpr.
inline constexpr gaddr kShadow = kMainRamSize, kShadowSpr = 2u * kMainRamSize;
// MORE RIVALS is on (the default): B3NATIVE_MORE_RIVALS=0 turns it off, guest RAM then the console's 32 MB
inline bool requested() {
    const char* e = std::getenv("B3NATIVE_MORE_RIVALS");
    return !(e && e[0] == '0' && e[1] == 0);
}
// guest RAM's reservation: the console's 32 MB, and with MORE RIVALS 33 MB more after it, no-access (a guard) until
// a race needs the shadow (ready())
inline std::uint32_t ram_bytes() { return requested() ? 2u * kMainRamSize + 0x100000u : kMainRamSize; }
// the shadow's guest RAM: grown (by the backend) at the first race that needs it; false when it cannot be
inline bool ready() {
    if (host_pointer(kShadowSpr + 0x4000u - 1u) != nullptr) return true;
    return backend().grow_ram() && host_pointer(kShadowSpr + 0x4000u - 1u) != nullptr;
}
inline void reset() {
    if (!ready()) return;
    std::memset(host_pointer(kShadow, kMainRamSize), 0, kMainRamSize);
    std::memset(host_pointer(kShadowSpr, 0x4000u), 0, 0x4000u);
}
inline gaddr shadow_of(gaddr at, std::size_t esize) {
    if (at < kMainRamSize) return at + kShadow;
    if (at >= 0x70000000u && at < 0x70004000u) return at - 0x70000000u + kShadowSpr;
    fatal_guest_pointer(at, std::uint32_t(esize));
}
// element `index` (>= n) of the array whose element 0 is at `host`
inline void* side(const void* host, std::size_t esize, std::size_t index, std::size_t n) {
    ++lookups;
    const gaddr at = shadow_of(guest_address(host) + gaddr((index - n) * esize), esize);
    void* p = host_pointer(at, std::uint32_t(esize));
    if (!p) fatal_guest_pointer(at, std::uint32_t(esize));
    return p;
}
// A per-car array that the game hands on as a base pointer and a count (the traffic system's CB3TrafficRaceCarInfo[6]
// in the scratchpad, followed by its parameter list): moved whole to guest RAM while the split is on
inline gaddr traffic_infos = 0;   // set by the trial's component (tools/native/more_rivals.h)
template <class T> inline T* moved(T* in_place, gaddr to) {
    return on && to ? static_cast<T*>(host_pointer(to, sizeof(T))) : in_place;
}
// a loop of the game's over its 6 per-car elements (the pass' "bound" rule: a `< 6` loop whose every subscript is a split
// array): 8 while the split is on, so cars 7-8's elements get the game's initial values too
inline int bound(int n) { return on ? n + 2 : n; }
// the guest address of element i of the array at `base` (in place below n or with the split off, else its shadow)
inline gaddr shadow_or_flat(gaddr base, std::size_t esize, std::size_t n, std::size_t i) {
    return !on || i < n ? base + gaddr(i * esize) : shadow_of(base + gaddr((i - n) * esize), esize);
}
// element i of the array at guest address `base` as raw byte arithmetic reaches it (`D_00522660 + i * 4 + 0x12AD28`)
inline char* flat(gaddr base, std::size_t esize, std::size_t n, std::size_t i) {
    if (__builtin_expect(!on || i < n, 1)) return static_cast<char*>(host_pointer(base + gaddr(i * esize), std::uint32_t(esize)));
    return static_cast<char*>(side(host_pointer(base), esize, i, n));
}
}  // namespace split

template <class T, std::size_t N, std::size_t S = N>
struct split_array {
    T e[S];
    T* flat() { T* p = e; __asm__("" : "+r"(p)); return p; }   // flat memory, as open_array (T18)
    const T* flat() const { const T* p = e; __asm__("" : "+r"(p)); return p; }
    template <class I> T& operator[](I i) {
        if (__builtin_expect(!split::on || static_cast<std::size_t>(i) < N, 1)) return flat()[i];
        return *static_cast<T*>(split::side(e, sizeof(T), static_cast<std::size_t>(i), N));
    }
    template <class I> const T& operator[](I i) const {
        if (__builtin_expect(!split::on || static_cast<std::size_t>(i) < N, 1)) return flat()[i];
        return *static_cast<const T*>(split::side(e, sizeof(T), static_cast<std::size_t>(i), N));
    }
    // the first N elements as a pointer: only for uses the pass has checked (a memset of the whole array, a copy)
    T* data() { return flat(); }
    const T* data() const { return flat(); }
};

// A per-car pool or list the extra cars extend in place (config/split_arrays.tsv `pool` / `flat`: the AI cars into the
// network cars, the physics bodies, the active-body list, the stage record's AI ids / colours into the network ones):
// T[S]'s layout, indexed through a pointer the compiler cannot bound (as open_array, T18), so an index past S is the flat
// memory the EE code reaches, never an assumption a loop stops at S (clang has no -fno-aggressive-loop-optimizations)
template <class T, std::size_t S>
struct flat_array {
    T e[S];
    T* flat() { T* p = e; __asm__("" : "+r"(p)); return p; }
    const T* flat() const { const T* p = e; __asm__("" : "+r"(p)); return p; }
    template <class I> T& operator[](I i) { return flat()[i]; }
    template <class I> const T& operator[](I i) const { return flat()[i]; }
    operator T*() { return flat(); }
    operator const T*() const { return flat(); }
    T* data() { return flat(); }
    const T* data() const { return flat(); }
};

// open_array<T> at a split array's place: T[1]'s layout (the unit's later fields keep their offsets)
template <class T, std::size_t N>
struct split_open {
    T e[1];
    T* flat() { T* p = e; __asm__("" : "+r"(p)); return p; }
    const T* flat() const { const T* p = e; __asm__("" : "+r"(p)); return p; }
    template <class I> T& operator[](I i) {
        if (__builtin_expect(!split::on || static_cast<std::size_t>(i) < N, 1)) return flat()[i];
        return *static_cast<T*>(split::side(e, sizeof(T), static_cast<std::size_t>(i), N));
    }
    template <class I> const T& operator[](I i) const {
        if (__builtin_expect(!split::on || static_cast<std::size_t>(i) < N, 1)) return flat()[i];
        return *static_cast<const T*>(split::side(e, sizeof(T), static_cast<std::size_t>(i), N));
    }
    T* data() { return flat(); }
    const T* data() const { return flat(); }
};

// a guest array global at a split array's place (`#define D_0064D388 (&b3host::guest_global<T>(0x0064D388u))`); M: the
// global is the array's element M (D_0064D38C, the car list's [1]), so its index i is the array's M + i
template <class T, std::size_t N, std::size_t M = 0>
struct split_global {
    gaddr at;
    T* flat() const { T* p = &guest_global<T>(at); __asm__("" : "+r"(p)); return p; }
    template <class I> T& operator[](I i) const {
        const std::size_t j = static_cast<std::size_t>(i) + M;
        if (__builtin_expect(!split::on || j < N, 1)) return flat()[i];
        return *static_cast<T*>(split::side(flat() - M, sizeof(T), j, N));
    }
    T* data() const { return flat(); }
};

// a per-car array on the guest stack (b3host::GuestLocalArray<T, N>, call.h): N elements, as the game's frame, with
// the split off; M while it is on (the frame then larger by the extra cars' elements)
template <class T, std::size_t N, std::size_t M>
class split_local_array {
public:
    using U = std::remove_cv_t<T>;
    split_local_array() : n_(split::on ? M : N), frame_(std::uint32_t(sizeof(U) * n_ + kLocalCanary), alignof(U) > 16 ? alignof(U) : 16),
        p_(static_cast<U*>(host_pointer(frame_.addr(), std::uint32_t(sizeof(U) * n_ + kLocalCanary)))) {
        std::uninitialized_default_construct_n(p_, n_);
        detail::local_canary_set(reinterpret_cast<std::uint8_t*>(p_ + n_));
    }
    ~split_local_array() noexcept(false) {
        detail::local_canary_check(reinterpret_cast<const std::uint8_t*>(p_ + n_), frame_.addr(), std::uint32_t(sizeof(U) * n_));
    }
    split_local_array(const split_local_array&) = delete;
    split_local_array& operator=(const split_local_array&) = delete;
    T (&operator*() const)[M] { return *reinterpret_cast<T (*)[M]>(p_); }
    gaddr addr() const { return frame_.addr(); }
private:
    std::size_t n_;
    GuestStackFrame frame_;
    U* p_;
};

}  // namespace b3host

#endif
