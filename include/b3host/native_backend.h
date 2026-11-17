// b3host/native_backend.h -- the native backend (milestone S0, docs/standalone.md §8): b3host on its own.
//
// - Guest memory is an arena: 32 MB main RAM at guest physical 0 and the 16 KB scratchpad at 0x70000000, both plain
//   host buffers published as b3host windows (guest pointers stay 32-bit offsets: gp<T>, host_pointer).
// - The retail ELF is read at run time from the user's install (load_elf) as data only: its PT_LOAD bytes are copied
//   to their addresses, the rest of the segment's memsz is zero (what crt0 leaves). Nothing in it is executed.
// - Every call into guest code that b3host's dispatch (call.h) cannot resolve to an enabled NativeOverride reaches
//   NativeBackend::call, which throws MissingNative. Every access outside the windows (MMIO, VU memory, past RAM)
//   throws NativeFault. There is no interpreter (native3: unless a fallback is set, see `fallback`): the first missing function ends the run with a
//   report (address, decomp name, kind, C status, arguments, the chain of native functions that led there).
// - The native call chain: table entries are wrapped by traced_entry<addr, entry>, which pushes the guest address of
//   each native function entered through dispatch. Direct C++ links between generated units are not on the chain.
//
// Tags: [C] confirmed from a cited source, [L] likely, [G] guess.
#ifndef B3HOST_NATIVE_BACKEND_H
#define B3HOST_NATIVE_BACKEND_H

#include "b3host/host.h"
#include "b3host/watch.h"   // pic1

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace b3host {

// One retail function, from the decomp (tools/native/gen_table.py). kind: game, crt0, runtime, rw, rwa, sce, lg, ea;
// c: matched, draft, none.
struct FunctionInfo {
    gaddr address;
    std::uint32_t size;
    const char* kind;
    const char* c;
    const char* name;
};
// The table reports use (sorted by address). Not owned.
void set_function_info(const FunctionInfo* table, std::size_t count);
const FunctionInfo* function_at(gaddr entry);          // exact entry, or null
const FunctionInfo* function_containing(gaddr address); // the function whose code holds `address`, or null
// "name [kind, c]" or "unknown"
std::string describe_function(gaddr address);

// The chain of native functions entered through dispatch (outermost first), on the thread running guest code.
namespace native_chain {
inline constexpr unsigned kMax = 256;
inline thread_local gaddr t_chain[kMax];
inline thread_local unsigned t_depth = 0;
struct Scope {
    explicit Scope(gaddr a) { if (t_depth < kMax) t_chain[t_depth] = a; ++t_depth; }
    ~Scope() { --t_depth; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
std::vector<gaddr> snapshot();
std::string format(const std::vector<gaddr>& chain);
}  // namespace native_chain

// boot2: the oracle hook (g_oracle_hook / g_oracle_targets / oracle_wants) lives in b3host/watch.h

// pic1: observers of a SET of functions (the game view's hook points, docs/native-renderer.md §32): b3host/watch.h
// (g_watch_hook, g_watch_bits, watched<A>(): false for the functions decomp2host's prologue reports itself). Off (null)
// by default; the oracle hook above is independent of it.

template <gaddr A, void (*Entry)(const CallArgs&, CallResult&)>
void traced_entry(const CallArgs& a, CallResult& r) {
    native_chain::Scope s(A);
    if (!self_reporting<A>() && oracle_wants(A)) [[unlikely]] {   // boot2: a self-reporting definition reports itself
        if (watched<A>()) g_watch_hook(A, a, nullptr);
        g_oracle_hook(A, a, nullptr);
        Entry(a, r);
        g_oracle_hook(A, a, &r);
        if (watched<A>()) g_watch_hook(A, a, &r);
        return;
    }
    if (watched<A>()) [[unlikely]] {
        g_watch_hook(A, a, nullptr);
        Entry(a, r);
        g_watch_hook(A, a, &r);
        return;
    }
    Entry(a, r);
}

// A call landed on an address with no native function.
struct MissingNative : HostError {
    gaddr address;
    CallArgs args;
    std::vector<gaddr> chain;
    MissingNative(gaddr address, const CallArgs& args);
};
// An access outside the windows (MMIO, VU memory, past RAM).
struct NativeFault : HostError {
    gaddr address;
    std::uint32_t len;
    bool write;
    std::vector<gaddr> chain;
    NativeFault(gaddr address, std::uint32_t len, bool write);
};

class NativeBackend final : public Backend {
public:
    NativeBackend();

    // The retail ELF image (its PT_LOAD segments, zero-filled to memsz). False with `error` set on failure.
    bool load_elf(const std::string& path, std::string& error);
    // iso1: the same from the file's bytes (the caller read them: a disc folder or a disc image, b3asset/discfs.h);
    // `name` for the messages
    bool load_elf_bytes(const std::vector<std::uint8_t>& elf, const std::string& name, std::string& error);
    // Clears RAM and the scratchpad.
    void clear();

    Windows windows() override;
    [[noreturn]] void read_slow(gaddr address, void* dst, std::uint32_t len) override;
    [[noreturn]] void write_slow(gaddr address, const void* src, std::uint32_t len) override;
    void record_store(gaddr, std::uint32_t) override {}
    gaddr stack_pointer() override { return sp_; }
    void set_stack_pointer(gaddr sp) override { sp_ = sp; }
    // A guest call with no native function: `fallback` when set (native3: retail in the mini, tools/native/
    // native_fallback.h; native_boot installs it unless B3NATIVE_STRICT=1), else MissingNative as before.
    void call(gaddr function, const CallArgs& args, CallResult& result) override;
    std::function<void(gaddr, const CallArgs&, CallResult&)> fallback;
    void add_cost(std::uint32_t instructions) override {
        cost_ += instructions;
        if (cost_ >= time_event_ && on_time) on_time();   // boot2: virtual EE time (the platform's VBlanks)
    }
    // boot2: on_time runs when the cost counter reaches time_event (the platform sets both: its next VBlank)
    std::function<void()> on_time;
    std::uint64_t time_event_ = ~0ull;
    // boot1: hardware registers go to the platform's model (src/native/platform_hw.cpp); none set: fatal (NativeFault)
    struct HwModel {
        virtual ~HwModel() = default;
        virtual void write(gaddr address, unsigned width, const void* data) = 0;
        virtual void read(gaddr address, unsigned width, void* data) = 0;
    };
    void set_hw_model(HwModel* model) { hw_model_ = model; }
    void hw_write(gaddr address, unsigned width, const void* data) override;
    void hw_read(gaddr address, unsigned width, void* data) override;
    // boot1: COP0 Status.EIE as ei / di leave it; the platform's frame scheduler delivers interrupts only when set
    void set_interrupts_enabled(bool enabled) override {
        const bool was = interrupts_enabled_;
        interrupts_enabled_ = enabled;
        if (enabled && !was && on_interrupts_enabled) on_interrupts_enabled();   // boot2: pending interrupts
    }
    std::function<void()> on_interrupts_enabled;
    bool interrupts_enabled() const override { return interrupts_enabled_; }
    // audio1: COP0 Count = the low 32 bits of the platform's virtual EE cycles (the platform sets virtual_cycles;
    // docs/audio.md §5: the standalone's timer is virtual EE time, never host time); none set: fatal
    std::function<std::uint64_t()> virtual_cycles;
    std::uint32_t cop0_count() override {
        if (!virtual_cycles) b3host::fatal("native backend: COP0 Count read with no virtual clock (set virtual_cycles)");
        return static_cast<std::uint32_t>(virtual_cycles());
    }

    // Runs the function at `function` natively (an enabled NativeOverride), as guest code calling it would, with sp
    // = args.sp. Throws MissingNative if there is none. `result` keeps what the function does not set.
    void run(gaddr function, const CallArgs& args, CallResult& result);

    std::vector<std::uint8_t>& ram() { return ram_; }
    std::vector<std::uint8_t>& scratchpad() { return spr_; }
    std::uint64_t cost() const { return cost_; }

private:
    std::vector<std::uint8_t> ram_;
    std::vector<std::uint8_t> spr_;
    gaddr sp_ = 0;
    std::uint64_t cost_ = 0;
    HwModel* hw_model_ = nullptr;
    bool interrupts_enabled_ = true;   // [L] crt0 runs `ei` before main (crt0.s 0x1001F8)
};

}  // namespace b3host

#endif
