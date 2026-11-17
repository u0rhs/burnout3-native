// r5900_mini.h -- TEST-ONLY: a minimal R5900 (EE core + COP1) interpreter that runs retail functions straight from
// orig/SLUS_210.50, so a host port can be compared with the retail instruction sequence bit for bit.
//
// Scope: exactly the instructions retail's libm (assembly/asm/runtime/libm.s in the decomp repo) uses -- integer ALU,
// 64-bit moves/shifts, loads/stores, branches including branch-likely, and the COP1 subset (add/sub/mul/div, abs,
// neg, mov, cvt.s.w, cvt.w.s, c.eq/lt/le, bc1f/t(l), mtc1/mfc1). Anything else is a hard error, so a run can never
// silently skip an instruction. Float semantics come from b3fp::ref (b3host/ps2float.h), the same model the port
// uses in faithful mode: the comparison therefore checks the port's *operation sequence*, not the float model.
// Not a general emulator: no exceptions, no COP0, no MMIO; one flat 32 MB RAM image.
//
// Extended for the inline-asm translator (port6, tools/decomp2host/eeasm.py): 128-bit GPRs (upper halves kept by
// 64-bit instructions), lq/sq, a subset of MMI, the COP1 accumulator ops and VU0 macro mode (COP2: every
// arithmetic form with dest masks and x/y/z/w/q/i broadcasts, ACC forms, vopmula/vopmsub, vdiv/vsqrt/vrsqrt into Q,
// vmove/vmr32/vabs/vftoi/vitof, the random unit, qmtc2/qmfc2/ctc2/cfc2, lqc2/sqc2). Decoded from the instruction
// words with the VU's own field layout (Sony VU User's Manual / EE Core Instruction Set Manual; COP2 opcode tables
// as PCSX2 lists them), independently of eeasm.py's mnemonic parser, so a retail function run here against the
// translated decomp source checks the translation (operand mapping, masks, broadcasts, ACC/Q chains). The float
// arithmetic itself is b3fp::ref / b3asm (the same model), as for the COP1 subset above.
#ifndef B3_TESTS_R5900_MINI_H
#define B3_TESTS_R5900_MINI_H

#include "b3host/eeasm.h"
#include "b3host/ps2float.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <functional>
#include <map>
#include <vector>

namespace mini {

class R5900 {
public:
    static constexpr std::uint32_t kRamSize = 32u << 20;
    static constexpr std::uint32_t kSentinel = 0x01FFFFF0u;   // ra for the outermost call; never executed
    static constexpr std::uint32_t kStackTop = 0x01FF0000u;

    // Loads the PT_LOAD segments of an EE ELF. Returns false if the file is missing.
    bool load_elf(const char* path) {
        std::FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        std::vector<std::uint8_t> d;
        std::uint8_t buf[65536];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
        std::fclose(f);
        ram_.assign(kRamSize, 0);
        const std::uint32_t phoff = rd32(d, 0x1C);
        const std::uint16_t phnum = static_cast<std::uint16_t>(d[0x2C] | (d[0x2D] << 8));
        for (unsigned i = 0; i < phnum; ++i) {
            const std::uint32_t p = phoff + 32 * i;
            if (rd32(d, p) != 1) continue;   // PT_LOAD
            const std::uint32_t off = rd32(d, p + 4), va = rd32(d, p + 8), fsz = rd32(d, p + 16);
            std::memcpy(&ram_[va & 0x01FFFFFF], &d[off], fsz);
        }
        image_ = ram_;
        return true;
    }

    std::uint64_t steps = 0;   // instructions executed by the last call (a branch and its delay slot count once)
    // Every executed instruction, delay slots included (a delay slot is its own step); a nullified branch-likely
    // slot is not executed. This is what an override must report as its cost.
    std::uint64_t retired = 0;

    // Guest memory words, for functions that take or return arrays (e.g. __kernel_rem_pio2f's x[] / y[]).
    void poke32(std::uint32_t a, std::uint32_t v) { st<std::uint32_t>(a, v); }
    std::uint32_t peek32(std::uint32_t a) { return ld<std::uint32_t>(a); }
    std::vector<std::uint8_t>& ram() { return ram_; }
    // The scratchpad (0x70000000 .. +16K; port8, for replays of live captures).
    std::vector<std::uint8_t>& spr() { return spr_; }
    std::uint64_t max_steps = 2000000;   // "runaway" beyond this many instructions in one run
    // VU0 state, for tests that seed or inspect it.
    b3asm::State vu;
    std::uint64_t gpr_hi(unsigned r) const { return gprhi_[r]; }
    std::uint64_t gpr_lo(unsigned r) const { return gpr_[r]; }
    std::uint32_t fpr(unsigned r) const { return fpr_[r]; }
    // Replay of a captured call: every register as captured, then run from `entry` until it returns (ra replaced
    // by the sentinel). tests/capture_replay.cpp.
    void set_gpr128(unsigned r, const b3asm::Q& q) { if (r) { gpr_[r] = b3asm::lo64(q); gprhi_[r] = b3asm::hi64(q); } }
    void set_fpr(unsigned r, std::uint32_t v) { fpr_[r] = v; }
    void set_facc(std::uint32_t v) { facc_ = v; }
    void run_from(std::uint32_t entry) {
        gpr_[31] = kSentinel;
        gprhi_[31] = 0;
        pc_ = entry;
        steps = retired = 0;
        run();
    }
    b3asm::Q gpr128(unsigned r) const { return Rq(r); }
    void set_gpr64(unsigned r, std::uint64_t v) { if (r) { gpr_[r] = v; gprhi_[r] = 0; } }
    // Native entries at fixed addresses (tests/test_calls.cpp: an interpreter's dispatch of an override): when pc
    // reaches a hooked address the hook runs instead of the code there, then execution continues at ra.
    std::map<std::uint32_t, std::function<void(R5900&)>> hooks;
    // native3 (the native boot's fallback, tools/native/native_fallback.h), all off by default:
    // - ext_ram / ext_spr: guest memory is these buffers (32 MB / 16 KB, the native backend's), not ram_ / spr_;
    // - hook_bits: a bitmap over RAM words (bit (pc & 0x1FFFFFF) >> 2): only a pc whose bit is set is looked up in hooks;
    // - on_tick(n) between instructions every tick_every retired instructions (n since the last tick): virtual time;
    // - count_source: mfc0 Count; status_eie / on_eie: Status.EIE is the caller's state (mfc0 reads it, ei / di /
    //   mtc0 report a change), so retail code and native code see one interrupt state.
    std::uint8_t* ext_ram = nullptr;
    std::uint8_t* ext_spr = nullptr;
    const std::vector<std::uint8_t>* hook_bits = nullptr;
    std::function<void(std::uint64_t)> on_tick;
    std::uint64_t tick_every = 0;
    std::function<std::uint32_t()> count_source;
    std::function<bool()> status_eie;
    std::function<void(bool)> on_eie;
    // native3: VCALLMS (entry = the pair index, (word >> 6) & 0x7FFF; VCALLMSR: CMSAR0): the VU0 microprogram runs on
    // this->vu (the platform's VU0 model, src/native/platform_vu0.h); pc() is the instruction's; unset: a failure
    std::function<void(R5900&, std::uint32_t)> on_vcallms;
    // vu0: CMSAR0 ($vi27, ctc2 / cfc2): VCALLMSR's start, in pairs as VCALLMS's immediate [L EE VU macro manual;
    // retail game code never uses either, C grep over the decomp's assembly/asm]
    std::uint32_t cmsar0 = 0;
    // vu0: a jump / branch to an address below 0x100000 (the EE kernel's area: the native platform's kernel is HLE)
    // fails at the branch instead of running whatever is there (the native fallback sets it)
    bool trap_low_pc = false;
    std::uint64_t total_retired = 0;
    // native3: the instructions retired since the last on_tick (and resets the mark): what is still to be charged
    std::uint64_t untick() { const std::uint64_t n = total_retired - tick_mark_; tick_mark_ = total_retired; return n; }
    std::uint64_t unticked() const { return total_retired - tick_mark_; }   // vu0: retired since the last tick (no reset)
    // A call into guest code made from native code, possibly while run() is inside a hook: runs `entry` to its return
    // (ra = sentinel), restores pc/ra/counters, returns the instructions it retired.
    std::uint64_t run_nested(std::uint32_t entry) {
        const std::uint32_t pc = pc_;
        const std::uint64_t ra = gpr_[31], ra_hi = gprhi_[31], st = steps, rt = retired;
        gpr_[31] = kSentinel;
        gprhi_[31] = 0;
        pc_ = entry;
        steps = retired = 0;
        run();
        const std::uint64_t n = retired;
        pc_ = pc;
        gpr_[31] = ra;
        gprhi_[31] = ra_hi;
        steps = st;
        retired = rt;
        return n;
    }
    // Calls with explicit stack pointer (tests place struct arguments in RAM first).
    std::uint64_t call_sp(std::uint32_t entry, std::uint32_t sp, std::initializer_list<std::uint32_t> fargs,
                          std::initializer_list<std::uint64_t> iargs = {}) {
        setup(entry, fargs, iargs);
        gpr_[29] = sp;
        run();
        return gpr_[2];
    }

    // Calls the function at `entry` with float args in f12.. and integer args in a0.., returns f0 bits.
    std::uint32_t call_f(std::uint32_t entry, std::initializer_list<std::uint32_t> fargs,
                         std::initializer_list<std::uint64_t> iargs = {}) {
        setup(entry, fargs, iargs);
        run();
        return fpr_[0];
    }
    // The same, returning v0 (integer/soft-double results).
    std::uint64_t call_i(std::uint32_t entry, std::initializer_list<std::uint32_t> fargs,
                         std::initializer_list<std::uint64_t> iargs = {}) {
        setup(entry, fargs, iargs);
        run();
        return gpr_[2];
    }

private:
    std::vector<std::uint8_t> ram_, image_, spr_ = std::vector<std::uint8_t>(0x4000);
    std::uint64_t hi_ = 0, lo_ = 0, hi1_ = 0, lo1_ = 0;   // HI/LO and the MMI pipeline-1 HI1/LO1 (low 64 bits)
    std::uint64_t gpr_[32] = {};
    std::uint64_t gprhi_[32] = {};   // bits 127..64 (MMI, lq/sq, qmtc2/qmfc2)
    std::uint32_t fpr_[32] = {};
    std::uint32_t facc_ = 0;         // COP1 ACC
    bool fcc_ = false;
    std::uint32_t pc_ = 0;
    std::uint64_t tick_mark_ = 0;

    static std::uint32_t rd32(const std::vector<std::uint8_t>& d, std::uint32_t o) {
        return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (static_cast<std::uint32_t>(d[o + 3]) << 24);
    }
    [[noreturn]] void fail(const char* what, std::uint32_t insn) {
        char m[160];
        std::snprintf(m, sizeof m, "r5900_mini: %s at pc 0x%08X (insn 0x%08X)", what, pc_, insn);
        throw std::runtime_error(m);
    }
    std::uint8_t* at(std::uint32_t a, std::uint32_t len) {
        const std::uint32_t seg = a >> 28;   // RAM and its mirrors as b3host::decode: kuseg/uncached/UCAB, kseg0/1
        std::uint32_t p = (seg == 0x0 || seg == 0x2 || seg == 0x3) ? (a & 0x0FFFFFFFu) : (a & 0x1FFFFFFFu);
        if (len == 16) p &= ~15u;   // lq/sq/lqc2/sqc2 ignore the low four address bits
        if ((a & 0xFFFFC000u) == 0x70000000u) {   // scratchpad
            const std::uint32_t o = (len == 16 ? a & ~15u : a) & 0x3FFFu;
            if (len != 16 && (a & (len - 1)) != 0) fail("bad memory access", a);
            return ext_spr ? ext_spr + o : &spr_[o];
        }
        if (p + len > kRamSize || (len != 16 && (a & (len - 1)) != 0)) fail("bad memory access", a);
        return ext_ram ? ext_ram + p : &ram_[p];
    }
    // boot1: EE hardware registers (physical 0x10000000-0x13FFFFFF: DMA / VIF / GIF / INTC / timers, GS privileged at
    // 0x12000000) go to on_mmio when it is set (address, width, write, the bytes), e.g. to log retail's accesses for
    // the b3hw proof; without it they stay a failure ("bad memory access"), as before.
    static bool hw_phys(std::uint32_t a, std::uint32_t& p) {
        const std::uint32_t seg = a >> 28;
        p = (seg == 0x0 || seg == 0x2 || seg == 0x3) ? (a & 0x0FFFFFFFu) : (a & 0x1FFFFFFFu);
        return (a & 0xFFFFC000u) != 0x70000000u && p >= 0x10000000u && p < 0x14000000u;
    }
    template <class T> T ld(std::uint32_t a) {
        T v;
        std::uint32_t p;
        if (on_mmio && hw_phys(a, p)) { on_mmio(p, sizeof(T), false, reinterpret_cast<std::uint8_t*>(&v)); return v; }
        if (on_rwatch && (a & 0x01FFFFFFu) + sizeof(T) > rwatch_lo && (a & 0x01FFFFFFu) < rwatch_hi) on_rwatch(*this, a & 0x01FFFFFFu, sizeof(T));
        std::memcpy(&v, at(a, sizeof(T)), sizeof(T));
        return v;
    }
    template <class T> void st(std::uint32_t a, T v) {
        std::uint32_t p;
        if (on_mmio && hw_phys(a, p)) { on_mmio(p, sizeof(T), true, reinterpret_cast<std::uint8_t*>(&v)); return; }
        if (on_watch && (a & 0x01FFFFFFu) + sizeof(T) > watch_lo && (a & 0x01FFFFFFu) < watch_hi) on_watch(*this, a & 0x01FFFFFFu, sizeof(T));
        std::memcpy(at(a, sizeof(T)), &v, sizeof(T));
        if (poison && (a & 0x01FFFFFFu) + sizeof(T) > poison_lo && (a & 0x01FFFFFFu) < poison_hi)
            for (const std::uint32_t w : *poison)
                if (w >= (a & 0x01FFFFFFu) && w < (a & 0x01FFFFFFu) + sizeof(T)) std::memcpy(at(w, 4), &poison_value, 4);
    }
public:
    // vu0 diagnostics: a store overlapping RAM [watch_lo, watch_hi) calls on_watch (before the store) with its address
    std::uint32_t watch_lo = 0, watch_hi = 0;
    std::function<void(R5900&, std::uint32_t, unsigned)> on_watch;
    // vu0: COP0 condition 0 for BC0F / BC0T (the DMAC's CPCOND0: ((~D_PCR.CPC | D_STAT.CIS) & 0x3FF) == 0x3FF [C ps2tek
    // index.html, DMAC D_PCR]); unset: asserted
    std::function<bool()> cpcond0;
    // vu0 diagnostics: a load overlapping RAM [rwatch_lo, rwatch_hi) calls on_rwatch (before the load)
    std::uint32_t rwatch_lo = 0, rwatch_hi = 0;
    std::function<void(R5900&, std::uint32_t, unsigned)> on_rwatch;
    // vu0 diagnostics: after a store overlapping one of these RAM words, the word is overwritten with poison_value (the
    // readers test of config/compare_mask.tsv: does any later reader use the value?)
    const std::vector<std::uint32_t>* poison = nullptr;
    std::uint32_t poison_lo = 0, poison_hi = 0, poison_value = 0x461C4000u;   // 10000.0f
private:
    static std::uint64_t sx32(std::uint32_t v) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(v))); }

    void setup(std::uint32_t entry, std::initializer_list<std::uint32_t> fargs, std::initializer_list<std::uint64_t> iargs) {
        std::memset(gpr_, 0, sizeof gpr_);
        std::memset(gprhi_, 0, sizeof gprhi_);
        std::memset(fpr_, 0, sizeof fpr_);
        facc_ = 0;
        fcc_ = false;
        unsigned i = 12;
        for (std::uint32_t f : fargs) fpr_[i++] = f;
        i = 4;
        for (std::uint64_t g : iargs) gpr_[i++] = g;
        gpr_[29] = kStackTop;
        gpr_[31] = kSentinel;
        pc_ = entry;
        steps = 0;
        retired = 0;
    }

    void run() {
        while (pc_ != kSentinel) {
            if (!probes.empty()) {
                auto pr = probes.find(pc_);
                if (pr != probes.end()) pr->second(*this);
            }
            if (on_tick && tick_every && total_retired - tick_mark_ >= tick_every) {
                const std::uint64_t n = total_retired - tick_mark_;
                tick_mark_ = total_retired;
                on_tick(n);
            }
            if (!hooks.empty() && (!hook_bits || ((pc_ & 0x1FFFFFFu) >> 2) / 8 < hook_bits->size() &&
                                                     ((*hook_bits)[((pc_ & 0x1FFFFFFu) >> 2) / 8] >> (((pc_ & 0x1FFFFFFu) >> 2) & 7) & 1))) {
                auto h = hooks.find(pc_);
                if (h != hooks.end()) {
                    h->second(*this);
                    pc_ = static_cast<std::uint32_t>(gpr_[31]);
                    continue;
                }
            }
            step_one();
        }
        gpr_[0] = 0;
    }

public:
    // One instruction at pc (a branch together with its delay slot, so pc never rests on a delay slot), as run()
    // executes it (a caller can drive its own dispatch loop with this). A `syscall` calls on_syscall
    // (pc then at the syscall; the callback decides what follows, e.g. a guest thread switch), or fails without one.
    std::function<void(R5900&)> on_syscall;
    std::function<void(std::uint32_t phys, unsigned width, bool write, std::uint8_t* data)> on_mmio;   // boot1
    // Every COP2 instruction and lqc2 as it executes (pc, word): tools/native/mini_check's VU binding record (native3).
    std::function<void(std::uint32_t, std::uint32_t)> on_cop2;
    // boot2: COP0 Status as mtc0 / ei / di leave it (mfc0 Status reads it; 0 by default, the old stand-in), and the
    // pseudo address on_mmio sees for cfc2 / ctc2 of VU control registers 28 (FBRST) / 29 (VPU-STAT) (include/b3hw/hw.h)
    std::uint32_t cop0_status = 0;
    std::function<void()> on_ei;   // boot2: `ei` turned interrupts on (pending ones are delivered)
    static constexpr std::uint32_t kCop2Ctrl = 0xC2000000u;
    std::uint32_t pc() const { return pc_; }
    void set_pc(std::uint32_t a) { pc_ = a; }
    void step_one() {
        if (++steps > max_steps) fail("runaway", 0);
        const std::uint32_t insn = ld<std::uint32_t>(pc_);
        if (on_syscall && (insn >> 26) == 0 && (insn & 63) == 0x0C) {
            ++retired;
            ++total_retired;
            pc_ += 4;
            on_syscall(*this);
            return;
        }
        ++retired;
        ++total_retired;
        std::uint32_t target = 0;
        const int br = exec(insn, target);   // 0: none, 1: taken (delay slot runs), 2: likely not taken (skip slot)
        if (br == 0) { pc_ += 4; return; }
        const std::uint32_t slot = pc_ + 4;
        if (br == 2) { pc_ = slot + 4; return; }
        pc_ = slot;                           // delay slot
        ++retired;
        ++total_retired;
        const std::uint32_t d = ld<std::uint32_t>(pc_);
        std::uint32_t dummy = 0;
        if (exec(d, dummy) != 0) fail("branch in a delay slot", d);
        if (br == 3) target = slot + 4;       // ordinary branch not taken: fall through after the slot
        if (trap_low_pc && target < 0x00100000u)   // vu0: retail game code never runs in the kernel area
            fail("control transfer below 0x100000 (target in the insn field; pc = the branch's delay slot)", target);
        pc_ = target;
    }
    // The register file (a guest thread's context for the tests' thread switches).
    struct Regs {
        std::uint64_t gpr[32], gprhi[32], hi, lo, hi1, lo1;
        std::uint32_t fpr[32], facc, pc;
        bool fcc;
    };
    Regs save_regs() const {
        Regs r;
        std::memcpy(r.gpr, gpr_, sizeof gpr_);
        std::memcpy(r.gprhi, gprhi_, sizeof gprhi_);
        std::memcpy(r.fpr, fpr_, sizeof fpr_);
        r.hi = hi_; r.lo = lo_; r.hi1 = hi1_; r.lo1 = lo1_; r.facc = facc_; r.fcc = fcc_; r.pc = pc_;
        return r;
    }
    // native3 (S2): continue from the loaded registers' pc (no sentinel set: a restored guest thread's code runs until
    // something ends it, e.g. an exception from a hook); probes run before the instruction at their pc executes (the
    // instruction then runs as usual), e.g. the poll point of a resumed snapshot
    void resume() { run(); }
    std::map<std::uint32_t, std::function<void(R5900&)>> probes;
    void load_regs(const Regs& r) {
        std::memcpy(gpr_, r.gpr, sizeof gpr_);
        std::memcpy(gprhi_, r.gprhi, sizeof gprhi_);
        std::memcpy(fpr_, r.fpr, sizeof fpr_);
        hi_ = r.hi; lo_ = r.lo; hi1_ = r.hi1; lo1_ = r.lo1; facc_ = r.facc; fcc_ = r.fcc; pc_ = r.pc;
    }

private:

    // Returns 0 (no control transfer), 1 (transfer to `target` after the delay slot), 2 (likely branch not taken:
    // the delay slot is nullified), 3 (ordinary branch not taken: the delay slot still runs).
    int exec(std::uint32_t i, std::uint32_t& target) {
        const unsigned op = i >> 26, rs = (i >> 21) & 31, rt = (i >> 16) & 31, rd = (i >> 11) & 31, sa = (i >> 6) & 31;
        const unsigned fn = i & 63;
        const std::uint32_t imm = i & 0xFFFF;
        const std::uint64_t simm = sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(imm))));
        auto R = [&](unsigned r) -> std::uint64_t { return r ? gpr_[r] : 0; };
        auto W = [&](unsigned r, std::uint64_t v) { if (r) gpr_[r] = v; };
        auto lo32 = [&](unsigned r) { return static_cast<std::uint32_t>(R(r)); };
        auto bto = [&](bool cond, bool likely) {
            target = pc_ + 4 + static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(imm)) * 4);
            return cond ? 1 : (likely ? 2 : 3);
        };
        const std::uint32_t ea = lo32(rs) + static_cast<std::uint32_t>(simm);
        switch (op) {
        case 0x00:
            switch (fn) {
            case 0x00: W(rd, sx32(lo32(rt) << sa)); return 0;                                           // sll
            case 0x02: W(rd, sx32(lo32(rt) >> sa)); return 0;                                           // srl
            case 0x03: W(rd, sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(lo32(rt)) >> sa))); return 0;  // sra
            case 0x04: W(rd, sx32(lo32(rt) << (lo32(rs) & 31))); return 0;                              // sllv
            case 0x06: W(rd, sx32(lo32(rt) >> (lo32(rs) & 31))); return 0;                              // srlv
            case 0x07: W(rd, sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(lo32(rt)) >> (lo32(rs) & 31)))); return 0;  // srav
            case 0x08: target = lo32(rs); return 1;                                                      // jr
            case 0x09: target = lo32(rs); W(rd, pc_ + 8); return 1;                                      // jalr
            case 0x0A: if (R(rt) == 0) W(rd, R(rs)); return 0;                                          // movz
            case 0x0B: if (R(rt) != 0) W(rd, R(rs)); return 0;                                          // movn
            case 0x0C: fail("syscall", i);
            case 0x0D: fail("break", i);
            case 0x0F: return 0;                                                                         // sync
            case 0x10: W(rd, hi_); return 0;                                                             // mfhi
            case 0x11: hi_ = R(rs); return 0;                                                            // mthi
            case 0x12: W(rd, lo_); return 0;                                                             // mflo
            case 0x13: lo_ = R(rs); return 0;                                                            // mtlo
            case 0x14: W(rd, R(rt) << (lo32(rs) & 63)); return 0;                                       // dsllv
            case 0x16: W(rd, R(rt) >> (lo32(rs) & 63)); return 0;                                       // dsrlv
            case 0x17: W(rd, static_cast<std::uint64_t>(static_cast<std::int64_t>(R(rt)) >> (lo32(rs) & 63))); return 0;  // dsrav
            case 0x18: case 0x19: mul32(fn == 0x18, lo32(rs), lo32(rt), lo_, hi_); W(rd, lo_); return 0;  // mult/multu (rd = LO)
            case 0x1A: case 0x1B: div32(fn == 0x1A, lo32(rs), lo32(rt), lo_, hi_); return 0;           // div/divu
            case 0x20: W(rd, sx32(lo32(rs) + lo32(rt))); return 0;                                      // add (no trap modelled)
            case 0x22: W(rd, sx32(lo32(rs) - lo32(rt))); return 0;                                      // sub
            case 0x2C: W(rd, R(rs) + R(rt)); return 0;                                                  // dadd
            case 0x2E: W(rd, R(rs) - R(rt)); return 0;                                                  // dsub
            case 0x21: W(rd, sx32(lo32(rs) + lo32(rt))); return 0;                                      // addu
            case 0x23: W(rd, sx32(lo32(rs) - lo32(rt))); return 0;                                      // subu
            case 0x24: W(rd, R(rs) & R(rt)); return 0;                                                  // and
            case 0x25: W(rd, R(rs) | R(rt)); return 0;                                                  // or
            case 0x26: W(rd, R(rs) ^ R(rt)); return 0;                                                  // xor
            case 0x27: W(rd, ~(R(rs) | R(rt))); return 0;                                               // nor
            case 0x2A: W(rd, static_cast<std::int64_t>(R(rs)) < static_cast<std::int64_t>(R(rt))); return 0;  // slt
            case 0x2B: W(rd, R(rs) < R(rt)); return 0;                                                  // sltu
            case 0x2D: W(rd, R(rs) + R(rt)); return 0;                                                  // daddu
            case 0x2F: W(rd, R(rs) - R(rt)); return 0;                                                  // dsubu
            case 0x38: W(rd, R(rt) << sa); return 0;                                                    // dsll
            case 0x3A: W(rd, R(rt) >> sa); return 0;                                                    // dsrl
            case 0x3B: W(rd, static_cast<std::uint64_t>(static_cast<std::int64_t>(R(rt)) >> sa)); return 0;  // dsra
            case 0x3C: W(rd, R(rt) << (sa + 32)); return 0;                                             // dsll32
            case 0x3E: W(rd, R(rt) >> (sa + 32)); return 0;                                             // dsrl32
            case 0x3F: W(rd, static_cast<std::uint64_t>(static_cast<std::int64_t>(R(rt)) >> (sa + 32))); return 0;  // dsra32
            case 0x28: W(rd, vu.sa); return 0;                                                       // mfsa (native3)
            case 0x29: vu.sa = static_cast<std::uint8_t>(R(rs)); return 0;                           // mtsa (native3)
            }
            fail("unsupported SPECIAL", i);
        case 0x01: {
            const auto s = static_cast<std::int64_t>(R(rs));
            switch (rt) {
            case 0: return bto(s < 0, false);    // bltz
            case 1: return bto(s >= 0, false);   // bgez
            case 2: return bto(s < 0, true);     // bltzl
            case 3: return bto(s >= 0, true);    // bgezl
            case 16: W(31, pc_ + 8); return bto(s < 0, false);    // bltzal
            case 17: W(31, pc_ + 8); return bto(s >= 0, false);   // bgezal
            // native3: mtsab / mtsah (SA as a byte count, as vu.sa keeps it for qfsrv) [C EE Core Instruction Set Manual]
            case 0x18: vu.sa = static_cast<std::uint8_t>((R(rs) ^ (i & 0xFFFFu)) & 0xFu); return 0;          // mtsab
            case 0x19: vu.sa = static_cast<std::uint8_t>(((R(rs) ^ (i & 0xFFFFu)) & 0x7u) * 2u); return 0;   // mtsah
            }
            fail("unsupported REGIMM", i);
        }
        case 0x02: target = (pc_ & 0xF0000000u) | ((i & 0x03FFFFFF) << 2); return 1;                    // j
        case 0x03: target = (pc_ & 0xF0000000u) | ((i & 0x03FFFFFF) << 2); W(31, pc_ + 8); return 1;    // jal
        case 0x04: return bto(R(rs) == R(rt), false);
        case 0x05: return bto(R(rs) != R(rt), false);
        case 0x06: return bto(static_cast<std::int64_t>(R(rs)) <= 0, false);
        case 0x07: return bto(static_cast<std::int64_t>(R(rs)) > 0, false);
        case 0x14: return bto(R(rs) == R(rt), true);
        case 0x15: return bto(R(rs) != R(rt), true);
        case 0x16: return bto(static_cast<std::int64_t>(R(rs)) <= 0, true);
        case 0x17: return bto(static_cast<std::int64_t>(R(rs)) > 0, true);
        case 0x08: W(rt, sx32(lo32(rs) + static_cast<std::uint32_t>(simm))); return 0;                   // addi
        case 0x18: W(rt, R(rs) + simm); return 0;                                                        // daddi
        case 0x09: W(rt, sx32(lo32(rs) + static_cast<std::uint32_t>(simm))); return 0;                   // addiu
        case 0x0A: W(rt, static_cast<std::int64_t>(R(rs)) < static_cast<std::int64_t>(simm)); return 0; // slti
        case 0x0B: W(rt, R(rs) < simm); return 0;                                                        // sltiu
        case 0x0C: W(rt, R(rs) & imm); return 0;                                                         // andi
        case 0x0D: W(rt, R(rs) | imm); return 0;                                                         // ori
        case 0x0E: W(rt, R(rs) ^ imm); return 0;                                                         // xori
        case 0x0F: W(rt, sx32(imm << 16)); return 0;                                                     // lui
        case 0x19: W(rt, R(rs) + simm); return 0;                                                        // daddiu
        case 0x20: W(rt, sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(ld<std::int8_t>(ea))))); return 0;  // lb
        case 0x21: W(rt, sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(ld<std::int16_t>(ea))))); return 0; // lh
        case 0x23: W(rt, sx32(ld<std::uint32_t>(ea))); return 0;                                         // lw
        case 0x24: W(rt, ld<std::uint8_t>(ea)); return 0;                                                // lbu
        case 0x25: W(rt, ld<std::uint16_t>(ea)); return 0;                                               // lhu
        case 0x27: W(rt, ld<std::uint32_t>(ea)); return 0;                                               // lwu
        case 0x28: st<std::uint8_t>(ea, static_cast<std::uint8_t>(R(rt))); return 0;                    // sb
        case 0x29: st<std::uint16_t>(ea, static_cast<std::uint16_t>(R(rt))); return 0;                  // sh
        case 0x2B: st<std::uint32_t>(ea, lo32(rt)); return 0;                                            // sw
        case 0x2F: case 0x33: return 0;                                                                  // cache, pref
        case 0x22: case 0x26: {                                                                          // lwl, lwr
            const unsigned b = ea & 3;
            const std::uint32_t m = ld<std::uint32_t>(ea & ~3u), r = lo32(rt);
            if (op == 0x22) W(rt, sx32((r & (0x00FFFFFFu >> (b * 8))) | (m << (24 - b * 8))));
            else if (b == 0) W(rt, sx32(m));
            else W(rt, (R(rt) & 0xFFFFFFFF00000000ull) | ((r & (0xFFFFFF00u << ((3 - b) * 8))) | (m >> (b * 8))));
            return 0;
        }
        case 0x2A: case 0x2E: {                                                                          // swl, swr
            const unsigned b = ea & 3;
            const std::uint32_t m = ld<std::uint32_t>(ea & ~3u), r = lo32(rt);
            if (op == 0x2A) st<std::uint32_t>(ea & ~3u, (m & (0xFFFFFF00u << (b * 8))) | (r >> (24 - b * 8)));
            else st<std::uint32_t>(ea & ~3u, (m & (0x00FFFFFFu >> ((3 - b) * 8))) | (r << (b * 8)));
            return 0;
        }
        case 0x1A: case 0x1B: {                                                                          // ldl, ldr
            const unsigned b = ea & 7;
            const std::uint64_t m = ld<std::uint64_t>(ea & ~7u), r = R(rt);
            if (op == 0x1A) W(rt, (r & (0x00FFFFFFFFFFFFFFull >> (b * 8))) | (m << (56 - b * 8)));
            else W(rt, (r & (0xFFFFFFFFFFFFFF00ull << ((7 - b) * 8))) | (m >> (b * 8)));
            return 0;
        }
        case 0x2C: case 0x2D: {                                                                          // sdl, sdr
            const unsigned b = ea & 7;
            const std::uint64_t m = ld<std::uint64_t>(ea & ~7u), r = R(rt);
            if (op == 0x2C) st<std::uint64_t>(ea & ~7u, (m & (0xFFFFFFFFFFFFFF00ull << (b * 8))) | (r >> (56 - b * 8)));
            else st<std::uint64_t>(ea & ~7u, (m & (0x00FFFFFFFFFFFFFFull >> ((7 - b) * 8))) | (r << (b * 8)));
            return 0;
        }
        case 0x31: fpr_[rt] = ld<std::uint32_t>(ea); return 0;                                           // lwc1
        case 0x37: W(rt, ld<std::uint64_t>(ea)); return 0;                                               // ld
        case 0x39: st<std::uint32_t>(ea, fpr_[rt]); return 0;                                            // swc1
        case 0x3F: st<std::uint64_t>(ea, R(rt)); return 0;                                               // sd
        case 0x11: return cop1(i, target);
        case 0x10:                                                                                       // COP0
            if (rs == 8) {   // vu0: BC0F / BC0T / BC0FL / BC0TL on CPCOND0 (no model: asserted -- every DMA completes at once)
                const bool c = cpcond0 ? cpcond0() : true;
                const bool want = (rt & 1) != 0, likely = (rt & 2) != 0;
                if (rt > 3) fail("unsupported COP0 branch", i);
                return bto(c == want, likely);
            }
            if (rs == 0 && rd == 9 && count_source) { W(rt, sx32(count_source())); return 0; }   // native3: mfc0 Count
            if (status_eie) cop0_status = (cop0_status & ~0x10000u) | (status_eie() ? 0x10000u : 0u);   // native3: one EIE
            if (rs == 0 && (rd == 9 || rd == 12)) { W(rt, rd == 9 ? sx32(static_cast<std::uint32_t>(retired)) : sx32(cop0_status)); return 0; }   // mfc0 Count / Status (replay stand-ins)
            // boot2: what retail library code does with COP0 on the sky2 start-up path: mtc0 Status (SkyOpen's CU2,
            // libkernl), mfc0 TagLo after a `cache` index load tag (SyncDCache: no line is ever dirty here, so 0),
            // ei / di (Status.EIE, bit 16)
            if (rs == 4 && rd == 12) {                                                                    // mtc0 Status
                const bool was = cop0_status & 0x10000u;
                cop0_status = static_cast<std::uint32_t>(R(rt));
                if (on_eie && was != static_cast<bool>(cop0_status & 0x10000u)) on_eie(cop0_status & 0x10000u);
                return 0;
            }
            if (rs == 0 && rd == 28) { W(rt, 0); return 0; }                                               // mfc0 TagLo
            if (rs == 0x10 && (fn == 0x38 || fn == 0x39)) {                                                // ei / di
                const bool was = cop0_status & 0x10000u;
                if (fn == 0x38) cop0_status |= 0x10000u; else cop0_status &= ~0x10000u;
                if (fn == 0x38 && !was && on_ei) on_ei();
                if (on_eie && was != (fn == 0x38)) on_eie(fn == 0x38);
                return 0;
            }
            fail("unsupported COP0", i);
        case 0x12: if (on_cop2) on_cop2(pc_, i); return cop2(i);
        case 0x1C: return mmi(i);
        case 0x1E: { const auto q = ld<b3asm::Q>(ea); if (rt) { gpr_[rt] = b3asm::lo64(q); gprhi_[rt] = b3asm::hi64(q); } return 0; }  // lq
        case 0x1F: st<b3asm::Q>(ea, Rq(rt)); return 0;                                                   // sq
        case 0x36: if (on_cop2) on_cop2(pc_, i); vu.vf[rt] = ld<b3asm::Q>(ea); if (rt == 0) vu.vf[0] = b3asm::vf0(); return 0;          // lqc2
        case 0x3E: st<b3asm::Q>(ea, VF(rt)); return 0;                                                   // sqc2
        }
        fail("unsupported opcode", i);
    }

    b3asm::Q Rq(unsigned r) const {
        return r ? b3asm::make_u128(static_cast<std::uint32_t>(gpr_[r]), static_cast<std::uint32_t>(gpr_[r] >> 32),
                                    static_cast<std::uint32_t>(gprhi_[r]), static_cast<std::uint32_t>(gprhi_[r] >> 32))
                 : b3asm::Q{};
    }
    void Wq(unsigned r, const b3asm::Q& q) { if (r) { gpr_[r] = b3asm::lo64(q); gprhi_[r] = b3asm::hi64(q); } }
    b3asm::Q VF(unsigned r) const { return r ? vu.vf[r] : b3asm::vf0(); }
    void WVF(unsigned r, const b3asm::Q& q) { if (r) vu.vf[r] = q; }

    // VU0 macro mode. DEST bits 24..21 = x y z w (VU order); b3vu masks are x=1 .. w=8.
    template <class F> static b3asm::Q lanes(unsigned dest, const b3asm::Q& d, F&& f) {
        b3asm::Q r = d;
        for (unsigned k = 0; k < 4; ++k)
            if (dest & (8u >> k)) r.w[k] = f(k);
        return r;
    }
    int cop2(std::uint32_t i) {
        namespace r = b3fp::ref;
        const unsigned rs = (i >> 21) & 31, rt = (i >> 16) & 31, rd = (i >> 11) & 31;
        if (!(i & (1u << 25))) {
            switch (rs) {
            case 0x01: Wq(rt, VF(rd)); return 0;                                              // qmfc2
            case 0x05: WVF(rd, Rq(rt)); return 0;                                             // qmtc2
            case 0x02: {                                                                      // cfc2
                std::uint32_t v;
                if (rd < 16) v = rd ? vu.vi[rd] : 0u;
                else if (rd == 20) v = vu.r & 0x007FFFFFu;
                else if (rd == 21) v = vu.i;
                else if (rd == 22) v = vu.q;
                else if (rd == 27) v = cmsar0;                                                  // vu0: CMSAR0
                else if ((rd == 28 || rd == 29) && on_mmio) on_mmio(kCop2Ctrl + rd, 4, false, reinterpret_cast<std::uint8_t*>(&v));   // boot2: FBRST / VPU-STAT
                else fail("cfc2 of an unmodelled VU control register", i);
                if (rt) gpr_[rt] = sx32(v);
                return 0;
            }
            case 0x06: {                                                                      // ctc2
                const std::uint32_t v = static_cast<std::uint32_t>(rt ? gpr_[rt] : 0);
                if (rd >= 1 && rd < 16) vu.vi[rd] = static_cast<std::uint16_t>(v);
                else if (rd == 20) vu.r = (v & 0x007FFFFFu) | 0x3F800000u;
                else if (rd == 21) vu.i = v;
                else if (rd == 22) vu.q = v;
                else if (rd == 27) cmsar0 = v & 0xFFFFu;                                         // vu0: CMSAR0
                else if (rd == 28 && on_mmio) { std::uint32_t w = v; on_mmio(kCop2Ctrl + rd, 4, true, reinterpret_cast<std::uint8_t*>(&w)); }   // boot2: FBRST
                else if (rd != 0) fail("ctc2 to an unmodelled VU control register", i);
                return 0;
            }
            }
            fail("unsupported COP2 transfer", i);
        }
        const unsigned dest = (i >> 21) & 15, ft = rt, fs = rd, fd = (i >> 6) & 31, fn = i & 63;
        const b3asm::Q S = VF(fs), T = VF(ft), A = vu.acc;
        auto bc = [&](unsigned lane) { return b3asm::splat(T.w[lane]); };
        const b3asm::Q Qv = b3asm::splat(vu.q), Iv = b3asm::splat(vu.i);
        // FMAC kernels by operation, on per-lane bits (faithful model regardless of build mode: tests compare bits
        // only in faithful mode, see test_vu_units.cpp)
        auto add = [&](const b3asm::Q& x, const b3asm::Q& y, unsigned k) { return r::add(x.w[k], y.w[k]); };
        auto sub = [&](const b3asm::Q& x, const b3asm::Q& y, unsigned k) { return r::sub(x.w[k], y.w[k]); };
        auto mul = [&](const b3asm::Q& x, const b3asm::Q& y, unsigned k) { return r::mul(x.w[k], y.w[k]); };
        auto madd = [&](const b3asm::Q& x, const b3asm::Q& y, unsigned k) { return r::madd(A.w[k], x.w[k], y.w[k]); };
        auto msub = [&](const b3asm::Q& x, const b3asm::Q& y, unsigned k) { return r::msub(A.w[k], x.w[k], y.w[k]); };
        auto mx = [&](const b3asm::Q& x, const b3asm::Q& y, unsigned k) { return r::max(x.w[k], y.w[k]); };
        auto mn = [&](const b3asm::Q& x, const b3asm::Q& y, unsigned k) { return r::min(x.w[k], y.w[k]); };
        auto to_fd = [&](auto op, const b3asm::Q& y) { WVF(fd, lanes(dest, VF(fd), [&](unsigned k) { return op(S, y, k); })); return 0; };
        auto to_acc = [&](auto op, const b3asm::Q& y) { vu.acc = lanes(dest, A, [&](unsigned k) { return op(S, y, k); }); return 0; };
        if (fn < 0x3C) {
            switch (fn) {
            case 0x00: case 0x01: case 0x02: case 0x03: return to_fd(add, bc(fn & 3));
            case 0x04: case 0x05: case 0x06: case 0x07: return to_fd(sub, bc(fn & 3));
            case 0x08: case 0x09: case 0x0A: case 0x0B: return to_fd(madd, bc(fn & 3));
            case 0x0C: case 0x0D: case 0x0E: case 0x0F: return to_fd(msub, bc(fn & 3));
            case 0x10: case 0x11: case 0x12: case 0x13: return to_fd(mx, bc(fn & 3));
            case 0x14: case 0x15: case 0x16: case 0x17: return to_fd(mn, bc(fn & 3));
            case 0x18: case 0x19: case 0x1A: case 0x1B: return to_fd(mul, bc(fn & 3));
            case 0x1C: return to_fd(mul, Qv);   case 0x1D: return to_fd(mx, Iv);
            case 0x1E: return to_fd(mul, Iv);   case 0x1F: return to_fd(mn, Iv);
            case 0x20: return to_fd(add, Qv);   case 0x21: return to_fd(madd, Qv);
            case 0x22: return to_fd(add, Iv);   case 0x23: return to_fd(madd, Iv);
            case 0x24: return to_fd(sub, Qv);   case 0x25: return to_fd(msub, Qv);
            case 0x26: return to_fd(sub, Iv);   case 0x27: return to_fd(msub, Iv);
            case 0x28: return to_fd(add, T);    case 0x29: return to_fd(madd, T);
            case 0x2A: return to_fd(mul, T);    case 0x2B: return to_fd(mx, T);
            case 0x2C: return to_fd(sub, T);    case 0x2D: return to_fd(msub, T);
            case 0x2F: return to_fd(mn, T);
            case 0x2E: {                                                                     // vopmsub (xyz)
                const b3asm::Q l = b3asm::make_u128(S.w[1], S.w[2], S.w[0], 0), rr = b3asm::make_u128(T.w[2], T.w[0], T.w[1], 0);
                WVF(fd, lanes(0xE, VF(fd), [&](unsigned k) { return r::msub(A.w[k], l.w[k], rr.w[k]); }));
                return 0;
            }
            case 0x38:                                                                       // vcallms (native3)
                if (!on_vcallms) fail("vcallms (VU0 micro mode) without a VU0 model", i);
                on_vcallms(*this, (i >> 6) & 0x7FFFu);
                return 0;
            case 0x39:                                                                       // vcallmsr (vu0): from CMSAR0
                if (!on_vcallms) fail("vcallmsr (VU0 micro mode) without a VU0 model", i);
                on_vcallms(*this, cmsar0 & 0xFFFFu);
                return 0;
            }
            fail("unsupported COP2 special1", i);
        }
        const unsigned f2 = ((i >> 4) & 0x7C) | (i & 3);
        const unsigned fsf = (i >> 21) & 3, ftf = (i >> 23) & 3;
        switch (f2) {
        case 0x00: case 0x01: case 0x02: case 0x03: return to_acc(add, bc(f2 & 3));
        case 0x04: case 0x05: case 0x06: case 0x07: return to_acc(sub, bc(f2 & 3));
        case 0x08: case 0x09: case 0x0A: case 0x0B: return to_acc(madd, bc(f2 & 3));
        case 0x0C: case 0x0D: case 0x0E: case 0x0F: return to_acc(msub, bc(f2 & 3));
        case 0x10: case 0x11: case 0x12: case 0x13: {                                      // vitof0/4/12/15
            static const unsigned n[4] = {0, 4, 12, 15};
            WVF(ft, lanes(dest, VF(ft), [&](unsigned k) { return b3asm::itof_bits(S.w[k], n[f2 & 3]); }));
            return 0;
        }
        case 0x14: case 0x15: case 0x16: case 0x17: {                                      // vftoi0/4/12/15
            static const unsigned n[4] = {0, 4, 12, 15};
            WVF(ft, lanes(dest, VF(ft), [&](unsigned k) { return b3asm::ftoi_bits(S.w[k], n[f2 & 3]); }));
            return 0;
        }
        case 0x18: case 0x19: case 0x1A: case 0x1B: return to_acc(mul, bc(f2 & 3));
        case 0x1C: return to_acc(mul, Qv);
        case 0x1D: WVF(ft, lanes(dest, VF(ft), [&](unsigned k) { return S.w[k] & 0x7FFFFFFFu; })); return 0;   // vabs
        case 0x1E: return to_acc(mul, Iv);
        case 0x20: return to_acc(add, Qv);  case 0x21: return to_acc(madd, Qv);
        case 0x22: return to_acc(add, Iv);  case 0x23: return to_acc(madd, Iv);
        case 0x24: return to_acc(sub, Qv);  case 0x25: return to_acc(msub, Qv);
        case 0x26: return to_acc(sub, Iv);  case 0x27: return to_acc(msub, Iv);
        case 0x28: return to_acc(add, T);   case 0x29: return to_acc(madd, T);
        case 0x2A: return to_acc(mul, T);   case 0x2C: return to_acc(sub, T);
        case 0x2D: return to_acc(msub, T);
        case 0x2E: {                                                                         // vopmula (xyz)
            const b3asm::Q l = b3asm::make_u128(S.w[1], S.w[2], S.w[0], 0), rr = b3asm::make_u128(T.w[2], T.w[0], T.w[1], 0);
            vu.acc = lanes(0xE, A, [&](unsigned k) { return r::mul(l.w[k], rr.w[k]); });
            return 0;
        }
        case 0x2F: return 0;                                                                 // vnop
        case 0x30: WVF(ft, lanes(dest, VF(ft), [&](unsigned k) { return S.w[k]; })); return 0;               // vmove
        case 0x31: WVF(ft, lanes(dest, VF(ft), [&](unsigned k) { return S.w[(k + 1) & 3]; })); return 0;     // vmr32
        case 0x38: vu.q = r::vu_div(S.w[fsf], T.w[ftf]); return 0;                          // vdiv
        case 0x39: vu.q = r::vu_sqrt(T.w[ftf] & 0x7FFFFFFFu); return 0;                     // vsqrt
        case 0x3A: vu.q = r::vu_rsqrt(S.w[fsf], T.w[ftf]); return 0;                        // vrsqrt
        case 0x3B: return 0;                                                                 // vwaitq
        case 0x40: case 0x41: {                                                              // vrnext / vrget
            if (f2 == 0x40) vu.r = b3asm::r_next(vu.r);
            WVF(ft, lanes(dest, VF(ft), [&](unsigned) { return vu.r; }));
            return 0;
        }
        case 0x42: vu.r = b3asm::r_init(S.w[fsf]); return 0;                                // vrinit
        case 0x43: vu.r = b3asm::r_init(S.w[fsf] ^ vu.r); return 0;                         // vrxor
        }
        fail("unsupported COP2 special2", i);
    }

    // mult/multu/div/divu (and the pipeline-1 forms): 32-bit operands, LO/HI sign-extended from 32 bits. Division by
    // zero and INT_MIN / -1 as the EE leaves them (LO = rs < 0 ? 1 : -1, HI = rs; LO = INT_MIN, HI = 0) [L].
    static void mul32(bool sgn, std::uint32_t a, std::uint32_t b, std::uint64_t& lo, std::uint64_t& hi) {
        const std::uint64_t p = sgn ? static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(a)) * static_cast<std::int32_t>(b))
                                    : static_cast<std::uint64_t>(a) * b;
        lo = sx32(static_cast<std::uint32_t>(p));
        hi = sx32(static_cast<std::uint32_t>(p >> 32));
    }
    static void div32(bool sgn, std::uint32_t a, std::uint32_t b, std::uint64_t& lo, std::uint64_t& hi) {
        if (b == 0) { lo = sx32(sgn && static_cast<std::int32_t>(a) < 0 ? 1u : 0xFFFFFFFFu); hi = sx32(a); return; }
        if (sgn) {
            if (a == 0x80000000u && b == 0xFFFFFFFFu) { lo = sx32(a); hi = 0; return; }
            lo = sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(a) / static_cast<std::int32_t>(b)));
            hi = sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(a) % static_cast<std::int32_t>(b)));
        } else {
            lo = sx32(a / b);
            hi = sx32(a % b);
        }
    }

    template <class F> static b3asm::Q bytes(const b3asm::Q& a, const b3asm::Q& b, F&& f) {
        b3asm::Q q;
        for (int k = 0; k < 16; ++k) {
            const std::uint8_t x = static_cast<std::uint8_t>(a.w[k / 4] >> (8 * (k % 4)));
            const std::uint8_t y = static_cast<std::uint8_t>(b.w[k / 4] >> (8 * (k % 4)));
            q.w[k / 4] = (k % 4 ? q.w[k / 4] : 0u) | (static_cast<std::uint32_t>(f(x, y)) << (8 * (k % 4)));
        }
        return q;
    }

    // Halfword lanes (movie2, for retail libmpeg): f(s.h[k], t.h[k]) as signed 16-bit.
    template <class F> static b3asm::Q halves(const b3asm::Q& a, const b3asm::Q& b, F&& f) {
        b3asm::Q q;
        for (int k = 0; k < 8; ++k) {
            const std::int16_t x = static_cast<std::int16_t>(a.w[k / 2] >> (16 * (k % 2)));
            const std::int16_t y = static_cast<std::int16_t>(b.w[k / 2] >> (16 * (k % 2)));
            const std::uint32_t v = static_cast<std::uint16_t>(f(x, y));
            q.w[k / 2] = (k % 2 ? q.w[k / 2] : 0u) | (v << (16 * (k % 2)));
        }
        return q;
    }

    // MMI subset (EE Core Instruction Set Manual opcode tables).
    int mmi(std::uint32_t i) {
        const unsigned rs = (i >> 21) & 31, rt = (i >> 16) & 31, rd = (i >> 11) & 31, sa = (i >> 6) & 31, fn = i & 63;
        const b3asm::Q S = Rq(rs), T = Rq(rt);
        auto R = [&](unsigned r) -> std::uint64_t { return r ? gpr_[r] : 0; };
        auto W = [&](unsigned r, std::uint64_t v) { if (r) gpr_[r] = v; };
        auto lo32 = [&](unsigned r) { return static_cast<std::uint32_t>(R(r)); };
        switch (fn) {
        case 0x08:   // MMI0
            switch (sa) {
            case 0x00: Wq(rd, b3asm::paddw(S, T)); return 0;
            case 0x01: Wq(rd, b3asm::psubw(S, T)); return 0;
            case 0x03: Wq(rd, b3asm::pmaxw(S, T)); return 0;
            case 0x04: Wq(rd, b3asm::paddh(S, T)); return 0;
            case 0x12: Wq(rd, b3asm::pextlw(S, T)); return 0;
            case 0x13: Wq(rd, b3asm::ppacw(S, T)); return 0;
            case 0x16: Wq(rd, b3asm::pextlh(S, T)); return 0;
            case 0x17: Wq(rd, b3asm::ppach(S, T)); return 0;
            case 0x1A: Wq(rd, b3asm::pextlb(S, T)); return 0;
            case 0x09: Wq(rd, bytes(S, T, [](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(x - y); })); return 0;   // psubb
            case 0x08: Wq(rd, bytes(S, T, [](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(x + y); })); return 0;   // paddb
            case 0x1B: Wq(rd, b3asm::ppacb(S, T)); return 0;
            case 0x06: Wq(rd, halves(S, T, [](std::int16_t x, std::int16_t y) { return x > y ? -1 : 0; })); return 0;   // pcgth
            case 0x07: Wq(rd, halves(S, T, [](std::int16_t x, std::int16_t y) { return x > y ? x : y; })); return 0;   // pmaxh
            }
            break;
        case 0x28:   // MMI1
            switch (sa) {
            case 0x01: Wq(rd, b3asm::pabsw(T)); return 0;
            case 0x03: Wq(rd, b3asm::pminw(S, T)); return 0;
            case 0x12: Wq(rd, b3asm::pextuw(S, T)); return 0;
            case 0x16: Wq(rd, b3asm::pextuh(S, T)); return 0;
            case 0x1B: Wq(rd, b3asm::qfsrv(S, T, vu.sa)); return 0;
            case 0x18: Wq(rd, bytes(S, T, [](std::uint8_t x, std::uint8_t y) { const unsigned v = x + y; return static_cast<std::uint8_t>(v > 255 ? 255 : v); })); return 0;   // paddub
            case 0x07: Wq(rd, halves(S, T, [](std::int16_t x, std::int16_t y) { return x < y ? x : y; })); return 0;   // pminh
            case 0x1A: {                                                                            // pextub
                b3asm::Q q;
                for (int k = 0; k < 4; ++k) q.w[k] = 0;
                for (int k = 0; k < 8; ++k) {
                    const std::uint32_t tb = (T.w[2 + k / 4] >> (8 * (k % 4))) & 0xFFu, sb = (S.w[2 + k / 4] >> (8 * (k % 4))) & 0xFFu;
                    q.w[k / 2] |= (tb | (sb << 8)) << (16 * (k % 2));
                }
                Wq(rd, q);
                return 0;
            }
            }
            break;
        case 0x09:   // MMI2
            switch (sa) {
            case 0x0E: Wq(rd, b3asm::pcpyld(S, T)); return 0;
            case 0x12: Wq(rd, b3asm::pand(S, T)); return 0;
            case 0x13: Wq(rd, b3asm::pxor(S, T)); return 0;
            case 0x1C: Wq(rd, b3asm::pmulth(vu, S, T)); return 0;
            }
            break;
        case 0x29:   // MMI3
            switch (sa) {
            case 0x0E: Wq(rd, b3asm::pcpyud(S, T)); return 0;
            case 0x1B: {                                                                            // pcpyh
                b3asm::Q q;
                const std::uint32_t lo = T.w[0] & 0xFFFFu, hi = T.w[2] & 0xFFFFu;
                q.w[0] = q.w[1] = lo | (lo << 16);
                q.w[2] = q.w[3] = hi | (hi << 16);
                Wq(rd, q);
                return 0;
            }
            case 0x12: Wq(rd, b3asm::por(S, T)); return 0;
            case 0x13: Wq(rd, b3asm::pnor(S, T)); return 0;
            }
            break;
        case 0x10: W(rd, hi1_); return 0;                                                           // mfhi1
        case 0x11: hi1_ = R(rs); return 0;                                                          // mthi1
        case 0x12: W(rd, lo1_); return 0;                                                           // mflo1
        case 0x13: lo1_ = R(rs); return 0;                                                          // mtlo1
        case 0x18: case 0x19: mul32(fn == 0x18, lo32(rs), lo32(rt), lo1_, hi1_); W(rd, lo1_); return 0;   // mult1/multu1
        case 0x1A: case 0x1B: div32(fn == 0x1A, lo32(rs), lo32(rt), lo1_, hi1_); return 0;              // div1/divu1
        case 0x00: case 0x01: {                                                                     // madd/maddu (rd = LO)
            const std::uint64_t acc = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi_)) << 32) | static_cast<std::uint32_t>(lo_);
            const std::uint64_t pr = fn == 0 ? static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(lo32(rs))) * static_cast<std::int32_t>(lo32(rt)))
                                             : static_cast<std::uint64_t>(lo32(rs)) * lo32(rt);
            const std::uint64_t v = acc + pr;
            lo_ = sx32(static_cast<std::uint32_t>(v)); hi_ = sx32(static_cast<std::uint32_t>(v >> 32)); W(rd, lo_);
            return 0;
        }
        case 0x34: Wq(rd, halves(T, T, [sa](std::int16_t x, std::int16_t) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(x) << (sa & 15)); })); return 0;   // psllh
        case 0x36: Wq(rd, halves(T, T, [sa](std::int16_t x, std::int16_t) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(x) >> (sa & 15)); })); return 0;   // psrlh
        case 0x37: Wq(rd, halves(T, T, [sa](std::int16_t x, std::int16_t) { return static_cast<std::int16_t>(x >> (sa & 15)); })); return 0;   // psrah
        case 0x3C: Wq(rd, b3asm::psllw(T, sa)); return 0;
        case 0x3E: Wq(rd, b3asm::psrlw(T, sa)); return 0;
        case 0x3F: Wq(rd, b3asm::psraw(T, sa)); return 0;
        }
        fail("unsupported MMI", i);
    }

    int cop1(std::uint32_t i, std::uint32_t& target) {
        namespace r = b3fp::ref;
        const unsigned fmt = (i >> 21) & 31, ft = (i >> 16) & 31, fs = (i >> 11) & 31, fd = (i >> 6) & 31, fn = i & 63;
        if (fmt == 0) { if (ft) gpr_[ft] = sx32(fpr_[fs]); return 0; }                                   // mfc1
        if (fmt == 4) { fpr_[fs] = static_cast<std::uint32_t>(ft ? gpr_[ft] : 0); return 0; }            // mtc1
        if (fmt == 2) { if (ft) gpr_[ft] = fs == 31 ? (fcc_ ? 0x00800000u : 0u) : 0u; return 0; }       // cfc1 (FCR31: C only)
        if (fmt == 6) { if (fs == 31) fcc_ = ((ft ? gpr_[ft] : 0) & 0x00800000u) != 0; return 0; }        // ctc1
        if (fmt == 8) {                                                                                   // bc1f/t(l)
            const unsigned k = ft & 3;
            const bool cond = (k & 1) ? fcc_ : !fcc_;
            target = pc_ + 4 + static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(i & 0xFFFF)) * 4);
            return cond ? 1 : ((k & 2) ? 2 : 3);
        }
        if (fmt == 20 && fn == 0x20) { fpr_[fd] = r::cvt_s_w(static_cast<std::int32_t>(fpr_[fs])); return 0; }   // cvt.s.w
        if (fmt != 16) fail("unsupported COP1 format", i);
        const std::uint32_t a = fpr_[fs], b = fpr_[ft];
        switch (fn) {
        case 0x00: fpr_[fd] = r::add(a, b); return 0;
        case 0x01: fpr_[fd] = r::sub(a, b); return 0;
        case 0x02: fpr_[fd] = r::mul(a, b); return 0;
        case 0x03: fpr_[fd] = r::fpu_div(a, b); return 0;
        case 0x05: fpr_[fd] = a & 0x7FFFFFFFu; return 0;                                                 // abs.s
        case 0x06: fpr_[fd] = a; return 0;                                                               // mov.s
        case 0x07: fpr_[fd] = a ^ 0x80000000u; return 0;                                                 // neg.s
        case 0x24: fpr_[fd] = static_cast<std::uint32_t>(r::cvt_w_s(a)); return 0;                       // cvt.w.s
        case 0x04: fpr_[fd] = r::fpu_sqrt(b); return 0;                                                  // sqrt.s fd, ft
        case 0x16: fpr_[fd] = r::fpu_rsqrt(a, b); return 0;                                              // rsqrt.s
        case 0x18: facc_ = r::add(a, b); return 0;                                                       // adda.s
        case 0x19: facc_ = r::sub(a, b); return 0;                                                       // suba.s
        case 0x1A: facc_ = r::mul(a, b); return 0;                                                       // mula.s
        case 0x1C: fpr_[fd] = r::madd(facc_, a, b); return 0;                                            // madd.s
        case 0x1D: fpr_[fd] = r::msub(facc_, a, b); return 0;                                            // msub.s
        case 0x1E: facc_ = r::madd(facc_, a, b); return 0;                                               // madda.s
        case 0x1F: facc_ = r::msub(facc_, a, b); return 0;                                               // msuba.s
        case 0x28: fpr_[fd] = r::max(a, b); return 0;                                                    // max.s
        case 0x29: fpr_[fd] = r::min(a, b); return 0;                                                    // min.s
        case 0x30: fcc_ = false; return 0;                                                               // c.f.s
        case 0x32: fcc_ = r::eq(a, b); return 0;                                                         // c.eq.s
        case 0x34: fcc_ = r::lt(a, b); return 0;                                                         // c.lt.s
        case 0x36: fcc_ = r::le(a, b); return 0;                                                         // c.le.s
        }
        fail("unsupported COP1.S", i);
    }
};

}  // namespace mini

#endif
