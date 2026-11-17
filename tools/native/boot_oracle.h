// boot_oracle.h -- retail code in tests/r5900_mini.h with the platform's hardware and kernel (boot2), and the
// comparison of a native run with it: RAM, scratchpad, the result register, and the hardware access traces (the
// native b3hw trace against the mini's on_mmio log: same order, address, width, direction and value).
//
// MiniPlatform gives one mini R5900 an HwModelDev and a KernelModel of its own (src/native/hw_model_dev.h; copies of the
// native side's when a comparison starts from a live native state): on_mmio is served by the model and logged;
// system calls by the kernel model as the native platform tier answers them (docs/specs/ee-kernel.md §10); a VBlank a poll needs, or a DMAC interrupt, runs the retail handlers nested in the mini (registers saved
// and restored around them, interrupts off, the handler's gp, a stack below the interrupted one's frame) exactly where
// the native platform runs the native handlers. Anything else ends the retail run (no verdict: ORACLE).
#ifndef B3NATIVE_BOOT_ORACLE_H
#define B3NATIVE_BOOT_ORACLE_H

#include "b3host/host.h"
#include "hw_model_dev.h"
#include "r5900_mini.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace b3native {

struct MiniPlatform {
    mini::R5900& cpu;
    HwModelDev hw;   // the dev model: retail code in the mini may reach the IPU (mpeg_play)
    KernelModel k;
    std::vector<b3host::HwAccess> trace;
    // movie2: the on_mmio log is for comparisons over short runs. A long run must turn it off: movie1's mpeg_play kept
    // it on while libmpeg polled IPU_CTRL on a stalled IPU for up to 4e9 steps (32 bytes per access, ~17.6 GB, the
    // process died 2026-11-06). Past trace_limit entries the run ends (no verdict) instead of growing without bound.
    bool keep_trace = true;
    std::size_t trace_limit = std::size_t{1} << 25;   // 32 Mi entries = 1 GiB [G: far above any boot-path comparison]
    void log_access(const b3host::HwAccess& a) {
        if (!keep_trace) return;
        if (trace.size() >= trace_limit) throw std::runtime_error("mini platform: hardware access log limit reached");
        trace.push_back(a);
    }
    std::uint32_t isr_sp = 0x01FF4000u;   // [G] the interrupt handlers' stack (the stack range is not compared)

    // call between instructions: a deferred DMAC completion (ee-kernel.md §8) whose time has come on the mini's clock
    void tick() {
        if (hw.clock && hw.next_completion_ready() <= cpu.retired) hw.poll_interrupts();
    }

    explicit MiniPlatform(mini::R5900& c) : cpu(c) {}

    void install() {
        // boot2: the mini's own clock (retired instructions) for the deferred DMAC completions; a copy of the native
        // model brought the native clock's closure (and its queued ready times): re-based, then replaced. The handler
        // check reads this platform's own kernel copy.
        hw.rebase_completions(hw.clock ? hw.clock() : 0, cpu.retired);
        hw.clock = [this] { return cpu.retired; };
        hw.has_dmac_handler = [this](unsigned c) {
            for (const auto& h : k.dmac)
                if (h.cause == c) return true;
            return false;
        };
        hw.on_vblank = [this] { vblank(); };
        hw.on_dmac_interrupt = [this](unsigned ch) { return dmac_interrupt(ch); };
        cpu.on_ei = [this] { hw.poll_interrupts(); };
        hw.on_dma = nullptr;
        hw.read_ram = [this](std::uint32_t a, void* out, unsigned len) {
            if ((a & 0xFFFFC000u) == 0x70000000u) {
                if ((a & 0x3FFFu) + len > cpu.spr().size()) return false;
                std::memcpy(out, cpu.spr().data() + (a & 0x3FFFu), len);
                return true;
            }
            const std::uint32_t p = a & 0x1FFFFFFFu;
            if (p + len > cpu.ram().size()) return false;
            std::memcpy(out, cpu.ram().data() + p, len);
            return true;
        };
        hw.write_ram = [this](std::uint32_t a, const void* in, unsigned len) {   // movie1: the IPU's fromIPU DMA
            if ((a & 0xFFFFC000u) == 0x70000000u) {
                if ((a & 0x3FFFu) + len > cpu.spr().size()) return false;
                std::memcpy(cpu.spr().data() + (a & 0x3FFFu), in, len);
                return true;
            }
            const std::uint32_t p = a & 0x1FFFFFFFu;
            if (p + len > cpu.ram().size()) return false;
            std::memcpy(cpu.ram().data() + p, in, len);
            return true;
        };
        cpu.on_mmio = [this](std::uint32_t p, unsigned w, bool wr, std::uint8_t* d) {
            if (wr) {
                b3host::HwAccess a{p, w, true, 0, 0};
                std::memcpy(&a.lo, d, w >= 8 ? 8 : w);
                if (w == 16) std::memcpy(&a.hi, d + 8, 8);
                log_access(a);
                hw.write(p, w, d);
            } else {
                hw.read(p, w, d);
                b3host::HwAccess a{p, w, false, 0, 0};
                std::memcpy(&a.lo, d, w >= 8 ? 8 : w);
                log_access(a);
            }
        };
        cpu.on_syscall = [this](mini::R5900& c) { syscall(c); };
    }

    // one handler call in the mini, as the kernel makes it: (cause / channel, argument), interrupts off, then back
    void run_handler(const KernelModel::Handler& h, std::uint32_t cause) {
        const mini::R5900::Regs saved = cpu.save_regs();
        const std::uint32_t status = cpu.cop0_status;
        cpu.cop0_status &= ~0x10000u;
        cpu.set_gpr64(4, cause);
        cpu.set_gpr64(5, h.argument);
        cpu.set_gpr64(28, h.gp);
        cpu.set_gpr64(29, isr_sp);
        cpu.run_nested(h.handler);
        cpu.load_regs(saved);
        cpu.cop0_status = status;
    }
    // One INTC cause (docs/specs/ee-kernel.md §6.3): only when the cause is enabled and has a handler; its status
    // bit acknowledged first, then each handler of the cause in list order (on a copy: handlers may change the list);
    // afterwards, with interrupts on, the DMAC interrupts raised meanwhile.
    void deliver(std::uint32_t cause) {
        if (cause > 31u || !(k.intc_mask & (1u << cause))) return;
        bool any = false;
        for (const auto& h : k.intc)
            if (h.cause == cause) { any = true; break; }
        if (!any) return;
        hw.ack_intc(cause);
        const std::vector<KernelModel::Handler> list = k.intc;
        for (const auto& h : list)
            if (h.cause == cause) run_handler(h, cause);
        if (cpu.cop0_status & 0x10000u) hw.poll_interrupts();
    }
    // A VBlank a poll needs (the interrupt part of §6.2): counted, VBON set, then VBlank start (2) and end (3). As the
    // native platform's, it cannot come with interrupts disabled (the retail run ends: no verdict).
    void vblank() {
        if (!(cpu.cop0_status & 0x10000u)) throw std::runtime_error("mini platform: a VBlank needed with interrupts disabled");
        ++k.vblanks;
        hw.vblank_start();
        deliver(2);
        deliver(3);
    }
    // A pending DMAC channel interrupt (§7): refused while interrupts are off (inside a handler too); else its status
    // bit acknowledged and each handler of the channel called in list order (on a copy).
    bool dmac_interrupt(unsigned ch) {
        if (!(cpu.cop0_status & 0x10000u)) return false;
        hw.ack_dmac(ch);
        const std::vector<KernelModel::Handler> list = k.dmac;
        for (const auto& h : list)
            if (h.cause == ch) run_handler(h, ch);
        return true;
    }

    // The system calls retail library code makes on the paths the oracle runs, answered as the native platform tier
    // answers them (§10; src/native/platform_kernel.cpp, platform_threads.cpp, platform_malloc.cpp). The call number is
    // v1 (negative: the interrupt-context form), the arguments a0..a3, the result v0. Anything else ends the retail run.
    void syscall(mini::R5900& c) {
        const auto n = static_cast<std::int64_t>(c.gpr_lo(3));
        auto a = [&c](unsigned i) { return static_cast<std::uint32_t>(c.gpr_lo(4 + i)); };
        auto ret = [&c](std::int32_t v) { c.set_gpr64(2, static_cast<std::uint64_t>(static_cast<std::int64_t>(v))); };
        auto fail = [n](const std::string& why) {
            throw std::runtime_error("mini platform: syscall " + std::to_string(n) + ": " + why);
        };
        auto set_bit = [](std::uint32_t& mask, std::uint32_t bit, bool on) {
            const bool was = ((mask >> bit) & 1u) != 0;
            if (on) mask |= 1u << bit;
            else mask &= ~(1u << bit);
            return was != on ? 1 : 0;
        };
        const auto gp = static_cast<std::uint32_t>(c.gpr_lo(28));
        switch (n) {
        case 0x02: break;   // SetGsCrt: nothing the oracle compares
        case 0x10: ret(KernelModel::add(k.intc, a(0), a(1), a(2), a(3), gp)); break;   // AddIntcHandler(2)
        case 0x11: ret(KernelModel::remove(k.intc, a(0), static_cast<std::int32_t>(a(1)))); break;   // RemoveIntcHandler
        case 0x12:   // AddDmacHandler(2)
            if (a(0) > 9u) fail("AddDmacHandler channel " + std::to_string(a(0)));
            ret(KernelModel::add(k.dmac, a(0), a(1), a(2), a(3), gp));
            break;
        case 0x13: ret(a(0) > 9u ? -1 : KernelModel::remove(k.dmac, a(0), static_cast<std::int32_t>(a(1)))); break;
        case 0x14:
        case 0x15:   // _EnableIntc / _DisableIntc
            if (a(0) > 31u) fail("INTC cause " + std::to_string(a(0)));
            ret(set_bit(k.intc_mask, a(0), n == 0x14));
            break;
        case 0x16:
        case 0x17:   // _EnableDmac / _DisableDmac (D_STAT's mask half follows)
            if (a(0) > 9u) fail("DMAC channel " + std::to_string(a(0)));
            ret(set_bit(k.dmac_mask, a(0), n == 0x16));
            hw.set_dmac_mask(k.dmac_mask);
            break;
        case 0x20: {   // CreateThread(ee_thread_t*): the record only (no scheduling)
            const std::uint32_t p = a(0);
            if (p & 3u) fail("CreateThread parameter block not word aligned");
            ret(k.create_thread(c.peek32(p + 4), c.peek32(p + 8), c.peek32(p + 12), c.peek32(p + 16),
                                static_cast<std::int32_t>(c.peek32(p + 20))));
            break;
        }
        case 0x22: ret(k.start_thread(static_cast<std::int32_t>(a(0)), a(1))); break;   // _StartThread
        case 0x29: {   // ChangeThreadPriority: the main thread's only
            const auto id = static_cast<std::int32_t>(a(0));
            if (id != 0 && id != 1) fail("ChangeThreadPriority of thread " + std::to_string(id));
            if (a(1) > 127u) { ret(-1); break; }
            const std::int32_t previous = k.main_priority;
            k.main_priority = static_cast<std::int32_t>(a(1));
            ret(previous);
            break;
        }
        case 0x2F: ret(1); break;   // GetThreadId: the main thread
        case 0x33:
        case -0x34:   // WakeupThread / iWakeupThread: the main thread's only (a wakeup counted)
            if (a(0) != 1u) fail("WakeupThread of thread " + std::to_string(a(0)));
            if (k.wakeups != 0xFFFFFFFFu) ++k.wakeups;
            ret(0);
            break;
        case 0x3E: ret(static_cast<std::int32_t>(0x01FFFD60u)); break;   // EndOfHeap
        case 0x40: {   // CreateSema(ee_sema_t*)
            const std::uint32_t p = a(0);
            if (p & 3u) fail("CreateSema parameter block not word aligned");
            ret(k.create_sema(static_cast<std::int32_t>(c.peek32(p + 8)), static_cast<std::int32_t>(c.peek32(p + 4)),
                              c.peek32(p + 16), c.peek32(p + 20)));
            break;
        }
        case 0x42:
        case -0x43:
        case 0x44: ret(static_cast<std::int32_t>(a(0))); break;   // SignalSema / iSignalSema / WaitSema: never contended
        case 0x64: ret(0); break;   // FlushCache
        case 0x71:
        case -0x71: {   // GsPutIMR: the mask set, v0 the previous one
            const std::uint64_t previous = hw.gs_imr();
            hw.set_gs_imr(a(0));
            c.set_gpr64(2, previous);
            break;
        }
        default: fail("not modelled by the oracle");
        }
    }
};

inline std::string hw_access_text(const b3host::HwAccess& a) {
    char b[96];
    if (a.width == 16)
        std::snprintf(b, sizeof b, "%s %08X/16 %016llX:%016llX", a.write ? "W" : "R", a.address,
                      (unsigned long long)a.hi, (unsigned long long)a.lo);
    else
        std::snprintf(b, sizeof b, "%s %08X/%u %llX", a.write ? "W" : "R", a.address, a.width, (unsigned long long)a.lo);
    return b;
}

// The two traces: "" when equal, else the first difference (index, retail, native) and the lengths.
inline std::string compare_hw_traces(const std::vector<b3host::HwAccess>& retail, const std::vector<b3host::HwAccess>& native) {
    const std::size_t n = std::min(retail.size(), native.size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto& r = retail[i];
        const auto& v = native[i];
        if (r.address != v.address || r.width != v.width || r.write != v.write || r.lo != v.lo || (r.width == 16 && r.hi != v.hi))
            return "hw access " + std::to_string(i) + ": retail " + hw_access_text(r) + " vs native " + hw_access_text(v) +
                   " (" + std::to_string(retail.size()) + " vs " + std::to_string(native.size()) + " accesses)";
    }
    if (retail.size() != native.size())
        return "hw accesses: retail " + std::to_string(retail.size()) + " vs native " + std::to_string(native.size()) +
               (retail.size() > n ? ", retail's next " + hw_access_text(retail[n]) : ", native's next " + hw_access_text(native[n]));
    return "";
}

}  // namespace b3native

#endif
