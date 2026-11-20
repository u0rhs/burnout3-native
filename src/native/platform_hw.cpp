// The native platform's hardware layer (boot1 / boot2; include/b3hw/hw.h, decided 2026-11-05; device state
// only since the device-state work, 2026-11-10): what port-track C (the sky2 driver, Sony libgraph / libdma) writes to
// and reads from EE hardware registers. The state itself is src/native/hw_model.h (the retail oracle's minis use the
// dev model built on it, hw_model_dev.h); here it is bound to the native backend: a VBlank poll advances virtual time
// through the platform's interrupt delivery (platform_iop.cpp vblank), a DMAC completion's callback runs the kernel's
// DMAC handlers when the clock reaches it, a VIF0 upload loads its VU0 image, an unmodelled register is fatal, naming it.
#include "platform.h"
#include "platform_vu0.h"

#include "b3host/native_backend.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace b3native::platform {
namespace {

HwModel g_hw;

struct Model final : b3host::NativeBackend::HwModel {
    void write(b3host::gaddr a, unsigned width, const void* d) override {
        try {
            g_hw.write(a, width, static_cast<const std::uint8_t*>(d));
        } catch (const b3native::HwModel::Unmodelled& e) {
            stub_unexpected("hardware", e.what());
        }
    }
    void read(b3host::gaddr a, unsigned width, void* d) override {
        try {
            g_hw.read(a, width, static_cast<std::uint8_t*>(d));
        } catch (const b3native::HwModel::Unmodelled& e) {
            stub_unexpected("hardware", e.what());
        }
    }
};
Model g_model;

// layer_check (tools/native/layer_check.sh, the hardware layer's golden runs): B3NATIVE_DMA_LOG=<file> -- the DMA
// completions (`d`) and the DMAC callbacks delivered (`i`), each with its channel, on the model's clock (virtual EE
// cycles): one line per VBlank, `<vblank> <events> <FNV-1a 64 of every event's kind, channel and clock>`; with
// B3NATIVE_DMA_LOG_EVENTS=<first>-<last> also every event of those VBlanks, `<vblank> <clock> d|i <channel>` (where two
// runs part). Host-side only: nothing the game sees changes.
struct DmaLog {
    std::FILE* f = nullptr;
    unsigned long long lo = 1, hi = 0;
    std::uint64_t events = 0, hash = 0xCBF29CE484222325ull;
};
DmaLog& dma_log_state() {
    static DmaLog d = [] {
        DmaLog x;
        const char* e = std::getenv("B3NATIVE_DMA_LOG");
        if (!e || !*e) return x;
        x.f = std::fopen(e, "w");
        if (!x.f) stub_unexpected("B3NATIVE_DMA_LOG", std::string("cannot write ") + e);
        if (const char* r = std::getenv("B3NATIVE_DMA_LOG_EVENTS")) std::sscanf(r, "%llu-%llu", &x.lo, &x.hi);
        return x;
    }();
    return d;
}

}  // namespace

void dma_log(char kind, unsigned channel) {
    DmaLog& d = dma_log_state();
    if (!d.f) return;
    const std::uint64_t t = g_hw.clock ? g_hw.clock() : 0;
    for (const std::uint64_t x : {std::uint64_t(kind), std::uint64_t(channel), t}) d.hash = (d.hash ^ x) * 0x100000001B3ull;
    ++d.events;
    const std::uint64_t v = vblank_count();
    if (v >= d.lo && v <= d.hi)
        std::fprintf(d.f, "%llu %llu %c %u\n", static_cast<unsigned long long>(v), static_cast<unsigned long long>(t), kind, channel);
}

HwModel& hw_model() { return g_hw; }

void install_hw_model(b3host::NativeBackend& nb) {
    g_hw.on_vblank = [] { vblank(); };
    g_hw.on_dmac_interrupt = [](unsigned c) { return dmac_interrupt(c); };
    // vu0 (boot2-agreed): DMAC callbacks delivered kDmacCompletionDelay virtual EE cycles after the kick (hw_model.h)
    g_hw.clock = [] { return virtual_cycles() + (guest_pending_cycles() ? guest_pending_cycles()() : 0u); };
    g_hw.has_dmac_handler = [](unsigned c) { return dmac_handler_exists(c); };
    g_hw.on_path3_latch = [] { gate_latched(); };   // H3: the gate's wake-up (platform_iop.cpp)
    nb.on_interrupts_enabled = [] { g_hw.poll_interrupts(); time_check(); };
    arm_virtual_time(nb);
    // COP0 Count (audio1's b3hw_cop0_count): the virtual EE clock, the cost counter in EE cycles (a VBlank every
    // 4,915,200), never host time (docs/audio.md 5)
    nb.virtual_cycles = [&nb] { return nb.cost(); };
    // vu0 (SPR DMA): the scratchpad buffer for fromSPR / toSPR (RAM writes: write_ram, below)
    g_hw.scratchpad = [&nb] { return nb.scratchpad().data(); };
    g_hw.read_ram = [](std::uint32_t a, void* out, unsigned len) {
        try {
            std::memcpy(out, b3host::host_pointer_checked(a, len), len);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    // H4: VIF0 transfers (CGtVu0CodeManager's microcode uploads) load one of retail's four VU0 images, by address
    g_hw.on_vif0_kick = [](std::uint32_t chcr, std::uint32_t tadr) {
        vif0_upload(chcr, tadr, [](std::uint32_t a) {   // (read by the dev build's check only)
            std::uint32_t w = 0;
            std::memcpy(&w, b3host::host_pointer_checked(a, 4), 4);
            return w;
        });
    };
    // vu0: generated code's vcallms (b3asm::vcallms) runs on the same VU0
    b3asm::vcallms_hook() = [](b3asm::State& s, std::uint32_t e) { vu0_micro_call(s, e); };
    b3asm::cpcond0_hook() = [] { return hw_model().cpcond0(); };   // vu0: the decomp's bc0f DMA waits (eeasm idiom)
    g_hw.write_ram = [](std::uint32_t a, const void* in, unsigned len) {   // movie1: the IPU's fromIPU DMA
        try {
            std::memcpy(b3host::host_pointer_checked(a, len), in, len);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    nb.set_hw_model(&g_model);
    if (dma_log_state().f) {   // layer_check: B3NATIVE_DMA_LOG (above)
        g_hw.on_complete = [](unsigned c) { dma_log('d', c); };
        add_vblank_hook([] {
            DmaLog& d = dma_log_state();
            std::fprintf(d.f, "%llu %llu %016llx\n", static_cast<unsigned long long>(vblank_count()),
                         static_cast<unsigned long long>(d.events), static_cast<unsigned long long>(d.hash));
            d.events = 0;
            d.hash = 0xCBF29CE484222325ull;
        });
    }
}

}  // namespace b3native::platform
