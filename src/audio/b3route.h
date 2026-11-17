// B3ROUTE.IRX (Criterion's distortion router, IOP side) as host C++ (lane audio1; spec docs/audio-b3route.md, section
// numbers below are its own; addresses `irx_XXXXXXXX` are the module's functions the behaviour is taken from).
//
// No DSP here: B3ROUTE routes the voices of a 24-bit mask (core 0) to core 0's wet bus only (§4), and on a four-phase
// core-1 IRQ cycle (every 128 samples, §5) DMAs 256-sample blocks of that bus out of sound RAM to the EE (SIF
// command 6), whose handler (game code, func_002FC530) distorts them and DMAs a 512-sample frame back into the
// module's Fuzzy buffer, which B3ROUTE writes into core 1's MEMIN (played at BVOL 0x7FFF). It shares RwaIop's mixer and
// virtual time and hooks the SPU2 IRQ in front of RWA's handler, as the module's thread does at load (§2).
#ifndef B3_AUDIO_B3ROUTE_H
#define B3_AUDIO_B3ROUTE_H

#include "../native/rwa_iop.h"

#include <cstdint>
#include <vector>

namespace b3native {

// The module's two entries from the EE as the test (tests/test_b3route.cpp) drives them. The shipped game has no
// B3ROUTE: CB3SoundDistortion's command lists end at the game's own functions (src/native/platform_distortion.cpp),
// and the mixer does not route voices through a second core; this model is kept for that work.
struct SifRpcServer {
    virtual ~SifRpcServer() = default;
    // fno = sceSifCallRpc's function number; send / recv in host memory (recv may be null when recv_size is 0)
    virtual void call(uint32_t fno, const void* send, uint32_t send_size, void* recv, uint32_t recv_size) = 0;
};
struct SifIopMemory {
    virtual ~SifIopMemory() = default;
    // An EE -> IOP SIF DMA of n bytes to iop_addr: true if this owner holds [iop_addr, iop_addr + n) and took it.
    virtual bool sif_dma_to_iop(uint32_t iop_addr, const void* src, uint32_t n) = 0;
};

struct B3RoutePlatform {   // IOP -> EE, what isceSifSendCmd does for B3ROUTE
    virtual ~B3RoutePlatform() = default;
    // The command's SIF DMA part: n bytes to EE address ee_addr (lands before the EE's handler runs).
    virtual void write_ee(uint32_t ee_addr, const void* src, uint32_t n) = 0;
    // The command packet (sceSifCmdHdr + body; cid at word 2, 6 here; `size` bytes, 0x18): run the handler the EE
    // registered for that cid (sceSifAddCmdHandler(6, func_002FC530, this)) in interrupt context. It must run, and the
    // EE's isceSifSetDma back into Fuzzy land (SifIopMemory), before the IOP reaches the next B / D phase, 128
    // samples later (spec §9): best synchronously, from this call.
    virtual void send_cmd(const uint32_t* packet, uint32_t size) = 0;
};

struct B3RouteOptions {
    // The module's IOP load address [G]: chosen so that the RPC buffer (base + 0x2280) is the 0xF2700 a recorded retail
    // run's bind gave the EE for 0x150276. The EE only echoes base + 0xE80 back.
    uint32_t module_base = 0xF0480;
};

struct B3RouteStats {
    uint64_t rpc_calls = 0, wrong_fno = 0, commands = 0, unknown_commands = 0, truncated = 0;
    uint64_t irqs = 0, unknown_irqa = 0, phases[4] = {0, 0, 0, 0};
    uint64_t sends = 0, reads = 0, writes = 0, skipped_busy = 0, ee_dmas = 0;
};

class B3Route final : public SifRpcServer, public SifIopMemory, public Spu2IrqHook {
public:
    static constexpr uint32_t kRpcId = 0x150276;
    // Module-relative layout [C §1.1].
    static constexpr uint32_t kOffEeTarget = 0xE74, kOffFuzzy = 0xE80, kOffCleanB = 0x1680,
                              kOffCleanA = 0x1C80, kOffRpcBuf = 0x2280, kOffPacket = 0x26C0, kImageBytes = 0x26E0;
    static constexpr uint32_t kFuzzyBytes = 0x800, kCleanBytes = 0x600;
    // Sound RAM (libsd byte addresses) [C §5].
    static constexpr uint32_t kIrqaA = 0x3800, kIrqaB = 0x3900, kIrqaC = 0x3A00, kIrqaD = 0x3B00;
    static constexpr uint32_t kReadA = 0x2A00, kReadC = 0x2800, kMemin1 = 0x4800;

    // Hooks rwa's SPU2 IRQ (the module's thread, irx_0000045C); the destructor unhooks.
    B3Route(B3RoutePlatform& p, RwaIop& rwa, const B3RouteOptions& opt = {});
    ~B3Route() override;
    B3Route(const B3Route&) = delete;
    B3Route& operator=(const B3Route&) = delete;

    // SifRpcServer: the command list (irx_000002C0, §3), applied at rwa.now().
    void call(uint32_t fno, const void* send, uint32_t send_size, void* recv, uint32_t recv_size) override;
    // SifIopMemory: the EE's DMA into the module image (Fuzzy, from func_002FC530).
    bool sif_dma_to_iop(uint32_t iop_addr, const void* src, uint32_t n) override;
    // Spu2IrqHook: irx_00000170 (§2.1).
    uint32_t spu2_irq(uint64_t t, uint32_t cores) override;

    // Inspection.
    uint32_t module_base() const { return opt_.module_base; }
    uint32_t ee_target() const;     // [0xE74]
    const uint8_t* image() const { return img_.data(); }   // the module image, kImageBytes, module-relative
    const B3RouteStats& stats() const { return st_; }

private:
    void routing(uint64_t t, uint32_t mask);                 // irx_00000630
    void rearm(uint64_t t, uint32_t irqa);                   // the phase epilogue / irx_0000070C
    void send_clean(uint32_t off_clean, uint32_t opt_low);   // isceSifSendCmd(6, ...)
    void read_ring(uint64_t t, uint32_t spu, uint32_t off);  // sceSdVoiceTrans(0, READ, ...)
    void write_fuzzy(uint64_t t);                            // sceSdVoiceTrans(0, WRITE, Fuzzy, 0x4800, 0x800)
    uint32_t r32(uint32_t off) const;
    void w32(uint32_t off, uint32_t v);

    B3RoutePlatform& pf_;
    RwaIop& rwa_;
    B3RouteOptions opt_;
    std::vector<uint8_t> img_;
    B3RouteStats st_;
};

}  // namespace b3native

#endif
