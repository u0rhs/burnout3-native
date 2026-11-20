// b3host/host.h -- the host-neutral adapter between native Burnout 3 code and whatever runs the original code.
//
// Native game code (functions rebuilt from the decomp) sees only this interface. A backend implements it for one
// runtime: the native backend (b3host/native_backend.h, the shipped game) or the null/test backend
// (b3host/null_backend.h). Switching runtimes means writing one backend; game code is unchanged.
//
// Model:
// - Guest memory is the EE's address space. Main RAM and the scratchpad are plain bytes the backend exposes as
//   host windows (fast path, inline below); everything else (I/O registers, VU memory, GS registers, faults) goes
//   through the backend's slow path.
// - Every store into a window is reported to the backend (record_store) when the backend asks for it
//   (Windows::record_stores): a runtime with code-page or DMA bookkeeping needs it; the native backend doesn't.
// - Native code calls original (guest) code through Backend::call with the EE calling convention (b3host/call.h).
// - Native code reports the guest work it stands for (add_cost); runtimes that pace timers by retired instructions
//   use it, others ignore it.
//
// Threading: one bound backend per process, used from the thread that runs guest code (the EE is single-core).
//
// Tags: [C] confirmed from a cited source, [L] likely, [G] guess.
#ifndef B3HOST_HOST_H
#define B3HOST_HOST_H

#include "b3host/ps2types.h"

#include <atomic>
#include <cstdint>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace b3host {

// ---- EE address map ([C] ps2tek index.html "EE memory map": 0x00000000 main RAM, 0x20000000 uncached alias,
// 0x30100000 uncached-accelerated alias, 0x10000000 I/O, 0x11000000 VU memory, 0x12000000 GS privileged registers,
// 0x70000000 16 KB scratchpad; [L] KSEG0/KSEG1 (0x80000000/0xA0000000) mirror physical memory as on any MIPS) ----
inline constexpr gaddr kScratchpadBase = 0x70000000u;
inline constexpr std::uint32_t kScratchpadSize = 0x4000u;            // [C] ps2tek: 16 KB
inline constexpr std::uint32_t kMainRamSize = 32u * 1024u * 1024u;   // [C] ps2tek: 32 MB (retail console)

// Host views of the directly addressable guest memory, published by the bound backend.
struct Windows {
    std::uint8_t* ram = nullptr;        // guest physical 0 .. ram_size
    std::uint32_t ram_size = 0;
    std::uint8_t* scratchpad = nullptr; // guest 0x70000000 .. +kScratchpadSize (null: scratchpad via slow path)
    bool record_stores = false;         // call Backend::record_store after every direct store
};

// One call into guest code, or one entry from guest code into a native override, in EE register terms.
// [C] the EE ABI: integer/pointer args in a0-a3, t0-t3 (GPR 4-11), float args in
// f12-f19, results in v0 (and v1) or f0, return address in ra; callee-saved s0-s7, gp, sp, fp, f20-f31.
// [C docs/abi-mwcc-ee.md §1-§3] integer and float arguments are numbered independently; a 64-bit value (MWCC long,
// soft double) uses one GPR, a u_long128 one whole 128-bit GPR; arguments past 8 of a class are in 8-byte slots from
// the entry sp + 0 (b3host/call.h lays them out).
struct CallArgs {
    u_long128 gpr[8] = {};   // a0-a3, t0-t3 (full 128-bit registers; 32-bit values sign-extended to 64 [C §2])
    std::uint32_t fpr[8] = {}; // f12-f19, raw bits
    unsigned n_gpr = 0;
    unsigned n_fpr = 0;
    gaddr sp = 0;            // guest sp at the call (GPR 29): stack arguments start at sp + 0. call_guest sets it to
                             // the sp it calls with; a backend's override thunk sets it from the guest's GPR 29
};
struct CallResult {
    u_long128 v0 = {};       // whole register (128-bit results use all of it [L])
    std::uint64_t v1 = 0;
    std::uint32_t f0 = 0;    // raw bits
};

// Thrown by the adapter on contract violations (bad guest pointer, unknown guest function in the null backend...).
struct HostError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
// rt1: set by an interactive host only (native_play's idle skip); b3hw_idle_wait calls it from guest busy-waits. Null in
// every proof run (native_boot): the waits then spin as before.
inline void (*g_idle_hook)() = nullptr;
[[noreturn]] void fatal(const std::string& message);
// mac5 diagnostics: a line on stderr from port library code (src/libs/*), whose printf is the guest's; B3HOST_DIAG=1
// enables it (off: nothing printed)
void diag(const char* tag, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t e);
// genret: a generated function reached a path its decomp draft leaves without a value (a non-void function falling off
// its end, where retail's MWCC code returns whatever v0 held; decomp2host T47 returns a defined value there instead).
// One line on stderr per site (`site` is a string literal: its address is the key), so a run says whether it happens.
void draft_fall_off(const char* site);
// mac4: appended to every fatal() message (set by native_backend.cpp: the native call chain, the last foreign pointer;
// by a fallback host: the retail pc), so an error caught far up the stack (native_play's "main: ERROR") still says
// where it happened
inline std::string (*g_fatal_context)() = nullptr;
// mac4: a guest pointer that no window covers was dereferenced: fatal() with its address and length
[[noreturn]] void fatal_guest_pointer(gaddr address, std::uint32_t len);

class Backend {
public:
    virtual ~Backend() = default;

    // Memory windows (fast path). Must stay valid while the backend is bound.
    virtual Windows windows() = 0;

    // Slow path: any access the windows don't cover (MMIO, VU memory, an address past RAM). `len` is 1, 2, 4, 8 or
    // 16, naturally aligned. A backend raises HostError for an access the console would fault on.
    virtual void read_slow(gaddr address, void* dst, std::uint32_t len) = 0;
    virtual void write_slow(gaddr address, const void* src, std::uint32_t len) = 0;

    // Bookkeeping for a store native code made directly into a window (only called when windows().record_stores).
    // `address` is the canonical guest address (main RAM: physical, 0x00xxxxxx; scratchpad: 0x7000xxxx).
    virtual void record_store(gaddr address, std::uint32_t len) = 0;

    // The guest stack pointer (GPR 29). Native code allocates guest stack memory below it (GuestStackFrame,
    // b3host/call.h) when it must hand original code the address of a local.
    virtual gaddr stack_pointer() = 0;
    virtual void set_stack_pointer(gaddr sp) = 0;

    // Run the original (guest) function at `function` with `args` until it returns, then fill `result`.
    // The backend sets a0-a3/t0-t3/f12-f19 from `args`, ra to a sentinel, and runs the guest (interpreter, AOT or
    // recompiled code; a call may reach a native override again) until pc == sentinel with sp back at its entry value.
    // Callee-saved registers are the guest's own business. A backend that cannot nest calls raises HostError.
    virtual void call(gaddr function, const CallArgs& args, CallResult& result) = 0;

    // Guest work (retired EE instructions) the running native code stands for. Optional.
    virtual void add_cost(std::uint32_t /*instructions*/) {}

    // boot1: `ei` / `di` in decomp inline asm (an interrupt handler's epilogue, func_002370A0): COP0 Status.EIE. Only
    // the native backend models interrupts (its platform delivers them); any other backend cannot, so it is fatal.
    virtual void set_interrupts_enabled(bool /*enabled*/) {
        fatal("b3host: ei / di (COP0 Status.EIE) is modelled only by the native backend");
    }
    // boot2: COP0 Status.EIE as ei / di left it (port-track C: b3hw_eie, the asm's `mfc0 Status & 0x10000`)
    virtual bool interrupts_enabled() const {
        fatal("b3host: COP0 Status.EIE is modelled only by the native backend");
    }
    // audio1: COP0 Count (b3hw_cop0_count: RWA's timer). The native backend gives virtual EE time; any other backend
    // has no Count.
    virtual std::uint32_t cop0_count() {
        fatal("b3host: COP0 Count is modelled only by the native backend and the mini");
    }

    // boot1 (the hardware-hook decision): one access of a hardware register by port-track C (include/b3hw/hw.h:
    // b3hw_write / b3hw_read). Default: the backend's own slow path.
    // The native backend hands it to the platform's hardware model instead.
    virtual void hw_write(gaddr address, unsigned width, const void* data) { write_slow(address, data, width); }
    virtual void hw_read(gaddr address, unsigned width, void* data) { read_slow(address, data, width); }
};

// The b3hw trace (boot1): when set, every b3hw access appends here, in order, for the proof against retail's MMIO
// accesses logged in the mini (r5900_mini on_mmio).
struct HwAccess {
    gaddr address;
    unsigned width;
    bool write;
    std::uint64_t lo, hi;
};
namespace detail {
// mac3: the host stack the current code runs on (the thread's own stack: thread_stack(), host.cpp). ptr_arg windows are
// clamped to it (a read past a stack's top faults)
struct StackRange {
    const std::uint8_t* lo;
    const std::uint8_t* hi;
};
StackRange thread_stack();
}  // namespace detail
namespace detail {
inline std::vector<HwAccess>* g_hw_trace = nullptr;
}
inline void set_hw_trace(std::vector<HwAccess>* trace) { detail::g_hw_trace = trace; }

// ---- binding ----
namespace detail {
inline Backend* g_backend = nullptr;
inline Windows g_windows{};
}  // namespace detail

// Binds `backend` (null unbinds). Re-bind after a backend changes its windows.
inline void bind(Backend* backend) {
    detail::g_backend = backend;
    detail::g_windows = backend ? backend->windows() : Windows{};
}
inline Backend& backend() {
    if (!detail::g_backend) fatal("b3host: no backend bound");
    return *detail::g_backend;
}
struct BackendScope {   // RAII bind; restores the previous binding
    Backend* previous;
    explicit BackendScope(Backend& b) : previous(detail::g_backend) { bind(&b); }
    ~BackendScope() { bind(previous); }
    BackendScope(const BackendScope&) = delete;
    BackendScope& operator=(const BackendScope&) = delete;
};

// ---- address decoding ----
enum class Region : std::uint8_t { MainRam, Scratchpad, Other };
struct Decoded {
    Region region;
    std::uint32_t offset;   // into the window (MainRam: physical address; Scratchpad: offset from 0x70000000)
};
// Decodes `address` for an access of `len` bytes that would hit a window. Region::Other: use the slow path.
inline Decoded decode(gaddr address, std::uint32_t len) {
    const Windows& w = detail::g_windows;
    const std::uint32_t seg = address >> 28;
    if (seg == 0x7) {
        const std::uint32_t off = address - kScratchpadBase;
        // off == size only with len == 0: one past the end (a valid C pointer, never dereferenced)
        if (w.scratchpad && off <= kScratchpadSize && len <= kScratchpadSize - off) return {Region::Scratchpad, off};
        return {Region::Other, 0};
    }
    std::uint32_t phys;
    if (seg == 0x0 || seg == 0x2 || seg == 0x3) phys = address & 0x0FFFFFFFu;        // kuseg, uncached, UCAB [C]
    else if (seg == 0x8 || seg == 0x9 || seg == 0xA || seg == 0xB) phys = address & 0x1FFFFFFFu; // kseg0/1 [L]
    else return {Region::Other, 0};
    if (phys <= w.ram_size && len <= w.ram_size - phys) return {Region::MainRam, phys};
    return {Region::Other, 0};
}
// boot2 (2026-11-07): a "foreign" pointer -- the host form of a guest address outside the windows that code forms but
// need not dereference (CB3DebrisArray::Prepare 0x2F4940 keeps `(void *)-9999` in a dead local; on the EE a pointer is
// only its 32-bit value). kForeignTag | address is non-canonical on x86-64 (and outside arm64's 48-bit space): any
// access faults at once (SIGSEGV), and guest_address() gives the address back unchanged. The last one formed is kept
// for the crash report.
namespace detail {
inline constexpr std::uintptr_t kForeignTag = 0xB300000000000000ull;
inline thread_local gaddr t_last_foreign = 0;
inline thread_local bool t_any_foreign = false;
inline void* foreign_pointer(gaddr address) {
    t_last_foreign = address;
    t_any_foreign = true;
    return reinterpret_cast<void*>(kForeignTag | address);
}
inline bool is_foreign(const void* p) { return (reinterpret_cast<std::uintptr_t>(p) >> 32) == (kForeignTag >> 32); }
// boot2: ptr_arg bounce windows opened, per host call site (native builds, B3_BOUNCE_CANARY): each copies host memory
// around a host object onto the guest stack (mac2: host stack bytes, host pointers included, then sit in guest memory,
// which makes a stale guest read host-dependent). `site`: PtrArg's constructor passes its own return address (the
// call site in generated code when the constructor is not inlined; else its enclosing function's caller).
void note_bounce_open(std::uint32_t bytes, const void* site);
struct BounceSite { const void* site; std::uint64_t opens; std::uint32_t max_bytes; };
std::vector<BounceSite> bounce_sites();   // most opens first
}  // namespace detail
// Host pointer for `len` bytes at `address`, or null when the bytes are not in a window (MMIO etc.).
inline void* host_pointer(gaddr address, std::uint32_t len = 1) {
    const Decoded d = decode(address, len);
    if (d.region == Region::MainRam) return detail::g_windows.ram + d.offset;
    if (d.region == Region::Scratchpad) return detail::g_windows.scratchpad + d.offset;
    return nullptr;
}
// hostfix: the same, for host code that copies `len` bytes at once (memcpy into or out of guest memory): bytes that
// are not all in one window are a HostError (fatal_guest_pointer), never a null pointer handed to memcpy
inline void* host_pointer_checked(gaddr address, std::uint32_t len) {
    void* p = host_pointer(address, len);
    if (!p) fatal_guest_pointer(address, len);
    return p;
}
// lane-noise (2026-11-11): whether `bytes` at host pointer `p` lie in guest RAM or the scratchpad (inline: the GtMathPs2
// shadow header's constructors ask it, see zero_if_host)
inline bool in_guest_window(const void* p, std::size_t bytes) {
    const Windows& w = detail::g_windows;
    const auto* b = static_cast<const std::uint8_t*>(p);
    if (w.ram && b >= w.ram && b + bytes <= w.ram + w.ram_size) return true;
    return w.scratchpad && b >= w.scratchpad && b + bytes <= w.scratchpad + kScratchpadSize;
}
// lane-noise: an object constructed in host memory -- a local, a temporary, a named return value in its caller's
// return slot, which GCC's -ftrivial-auto-var-init never zeroes -- starts zeroed; one constructed in guest memory (a
// T31 guest-stack local, a member of a guest object) keeps the bytes it finds, as the retail constructor does
inline void zero_if_host(void* p, std::size_t bytes) {
    if (!in_guest_window(p, bytes)) std::memset(p, 0, bytes);
}
// The canonical guest address of a host pointer into a window (main RAM -> 0x00xxxxxx, scratchpad -> 0x7000xxxx);
// null -> 0. Any other host pointer is a contract violation (HostError): native code must not hand guest code the
// address of host memory.
gaddr guest_address(const void* host);
// The same without the contract check: false (and out = 0) for a host pointer outside the windows.
bool try_guest_address(const void* host, gaddr& out);

// ---- typed access (the only sanctioned way to write guest memory; reads may also use gp<T>::operator->) ----
template <class T>
inline T load(gaddr address) {
    static_assert(std::is_trivially_copyable_v<T>, "guest loads need a trivially copyable type");
    T value;
    const Decoded d = decode(address, sizeof(T));
    if (d.region == Region::MainRam) std::memcpy(&value, detail::g_windows.ram + d.offset, sizeof(T));
    else if (d.region == Region::Scratchpad) std::memcpy(&value, detail::g_windows.scratchpad + d.offset, sizeof(T));
    else backend().read_slow(address, &value, sizeof(T));
    return value;
}
template <class T>
inline void store(gaddr address, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>, "guest stores need a trivially copyable type");
    const Decoded d = decode(address, sizeof(T));
    if (d.region == Region::Other) { backend().write_slow(address, &value, sizeof(T)); return; }
    std::uint8_t* base = d.region == Region::MainRam ? detail::g_windows.ram : detail::g_windows.scratchpad;
    std::memcpy(base + d.offset, &value, sizeof(T));
    if (detail::g_windows.record_stores)
        detail::g_backend->record_store(d.region == Region::MainRam ? d.offset : kScratchpadBase + d.offset, sizeof(T));
}
// Report a store native code made through a raw host pointer (gp<T>::operator->, memcpy into a window).
inline void note_store(gaddr address, std::uint32_t len) {
    if (!detail::g_windows.record_stores) return;
    const Decoded d = decode(address, len);
    if (d.region == Region::Other) fatal("b3host::note_store: not a window address");
    detail::g_backend->record_store(d.region == Region::MainRam ? d.offset : kScratchpadBase + d.offset, len);
}
// Bulk copies (memcpy/memset equivalents) through the windows, recorded.
void copy_to_guest(gaddr dst, const void* src, std::uint32_t len);
void copy_from_guest(void* dst, gaddr src, std::uint32_t len);
void fill_guest(gaddr dst, std::uint8_t value, std::uint32_t len);

inline void add_cost(std::uint32_t instructions) { backend().add_cost(instructions); }

// ---- native overrides reachable from native code (nested calls, port7) ----
// When native code calls a guest function (b3host::call_guest, gfn, vcall) whose address has an enabled native
// override, the call goes straight to the override's entry with the same EE register/stack image, and the override's
// reported cost is added (b3host::add_cost); otherwise the backend runs the original. One table per process, sorted
// by address; `enabled` lets a runtime switch an entry off (an excluded or unregistered override: the original runs).
// `nested_calls` counts calls that arrived this way (they never pass a dispatcher). docs/decomp-on-host.md, "Calls".
struct CallArgs;
struct CallResult;
struct NativeOverride {
    gaddr address;
    void (*entry)(const CallArgs&, CallResult&);
    std::uint32_t cost;                          // guest instructions reported per call (static, as the thunk's)
    std::atomic<bool>* enabled;
    std::atomic<std::uint64_t>* nested_calls;
};
void set_native_overrides(const NativeOverride* table, std::size_t count);
const NativeOverride* native_override(gaddr function);   // null: none, or switched off

// FNV-1a 64 of `len` bytes: the key a replacement is registered under (Override::code_fnv1a64, over the
// original function's code bytes, b3host/call.h). Computed from orig/SLUS_210.50 at build time or from guest RAM at
// registration.
std::uint64_t fnv1a64(const void* data, std::size_t len);

}  // namespace b3host

#endif
