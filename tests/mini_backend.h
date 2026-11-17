// A b3host backend over tests/r5900_mini.h: guest memory is the mini's RAM (32 MB, no scratchpad), a call into guest
// code runs in the mini interpreter (nested calls from inside a hook included), and native overrides reached from
// guest code are mini hooks (what an interpreting runtime's dispatcher does), installed from a b3host::NativeOverride
// table. Used by tests/test_calls.cpp for the nested-call path (port7).
#ifndef B3_TESTS_MINI_BACKEND_H
#define B3_TESTS_MINI_BACKEND_H

#include "b3host/call.h"
#include "r5900_mini.h"

namespace mini {

class MiniBackend final : public b3host::Backend {
public:
    explicit MiniBackend(R5900& cpu, b3host::gaddr sp = 0x01F00000u) : cpu_(cpu), sp_(sp) {}
    b3host::Windows windows() override {
        return {cpu_.ram().data(), static_cast<std::uint32_t>(cpu_.ram().size()), with_spr ? cpu_.spr().data() : nullptr, false};
    }
    bool with_spr = false;   // expose the mini's scratchpad as the window (capture_replay; port8)
    void read_slow(b3host::gaddr a, void*, std::uint32_t) override { b3host::fatal("MiniBackend: slow read " + std::to_string(a)); }
    void write_slow(b3host::gaddr a, const void*, std::uint32_t) override { b3host::fatal("MiniBackend: slow write " + std::to_string(a)); }
    // vu0: b3hw hardware accesses of native code go to the mini's MMIO hook when it has one (mini_check's VIF0 model),
    // else they fail as a slow access did before
    void hw_write(b3host::gaddr a, unsigned width, const void* data) override {
        if (!cpu_.on_mmio) { write_slow(a, data, width); return; }
        std::uint8_t d[16] = {};
        std::memcpy(d, data, width);
        cpu_.on_mmio(a, width, true, d);
    }
    void hw_read(b3host::gaddr a, unsigned width, void* data) override {
        if (!cpu_.on_mmio) { read_slow(a, data, width); return; }
        cpu_.on_mmio(a, width, false, static_cast<std::uint8_t*>(data));
    }
    void record_store(b3host::gaddr, std::uint32_t) override {}
    std::uint32_t cop0_count() override { return static_cast<std::uint32_t>(cpu_.retired); }   // audio1: as the mini's mfc0 Count
    b3host::gaddr stack_pointer() override { return sp_; }
    void set_stack_pointer(b3host::gaddr sp) override { sp_ = sp; }
    void add_cost(std::uint32_t n) override { cost += n; }
    void call(b3host::gaddr function, const b3host::CallArgs& args, b3host::CallResult& result) override {
        ++guest_calls;
        b3asm::Q saved[8];
        std::uint32_t savedf[8];
        for (unsigned i = 0; i < 8; ++i) { saved[i] = cpu_.gpr128(4 + i); savedf[i] = cpu_.fpr(12 + i); }
        const std::uint64_t sp_before = cpu_.gpr_lo(29);
        for (unsigned i = 0; i < 8; ++i) { cpu_.set_gpr128(4 + i, args.gpr[i]); cpu_.set_fpr(12 + i, args.fpr[i]); }
        cpu_.set_gpr64(29, static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(args.sp))));
        if (vu_sync) state_to_cpu();
        const std::uint64_t n = cpu_.run_nested(function);
        if (vu_sync) cpu_to_state();
        if (static_cast<std::uint32_t>(cpu_.gpr_lo(29)) != args.sp) b3host::fatal("MiniBackend: callee did not restore sp");
        result.v0 = cpu_.gpr128(2);
        result.v1 = cpu_.gpr_lo(3);
        result.f0 = cpu_.fpr(0);
        cost += static_cast<std::uint32_t>(n);
        cpu_.set_gpr64(29, sp_before);
        (void)saved; (void)savedf;
    }
    // Guest code reaching an override: a hook that reads the EE argument registers, runs the entry, writes v0/f0.
    void hook_native(const b3host::NativeOverride& n) {
        const b3host::NativeOverride* p = &n;
        cpu_.hooks[n.address] = [this, p](R5900& c) {
            if (!p->enabled->load()) b3host::fatal("MiniBackend: a disabled override was reached from guest code");
            b3host::CallArgs a;
            for (unsigned i = 0; i < 8; ++i) { a.gpr[i] = c.gpr128(4 + i); a.fpr[i] = c.fpr(12 + i); }
            a.n_gpr = a.n_fpr = 8;
            a.sp = static_cast<std::uint32_t>(c.gpr_lo(29));
            const b3host::gaddr outer_sp = sp_;
            sp_ = a.sp;   // native code allocates below the guest sp it was entered with
            b3host::CallResult r;
            if (vu_sync) cpu_to_state();
            p->entry(a, r);
            if (vu_sync) state_to_cpu();
            sp_ = outer_sp;
            c.set_gpr128(2, r.v0);
            c.set_fpr(0, r.f0);
            ++hooked_calls;
        };
    }
    // native3: the guest's VU registers and b3asm::state() are one register file: copied
    // into the mini before guest code runs from native code, and back after (also around a hooked native entry).
    bool vu_sync = false;
    void state_to_cpu() { const b3asm::State& s = b3asm::state(); for (unsigned r = 1; r < 32; ++r) cpu_.vu.vf[r] = s.vf[r]; cpu_.vu.acc = s.acc; cpu_.vu.q = s.q; cpu_.vu.i = s.i; }
    void cpu_to_state() { b3asm::State& s = b3asm::state(); for (unsigned r = 1; r < 32; ++r) s.vf[r] = cpu_.vu.vf[r]; s.acc = cpu_.vu.acc; s.q = cpu_.vu.q; s.i = cpu_.vu.i; }
    std::uint64_t cost = 0;
    unsigned guest_calls = 0;
    unsigned hooked_calls = 0;

private:
    R5900& cpu_;
    b3host::gaddr sp_;
};

}  // namespace mini

#endif
