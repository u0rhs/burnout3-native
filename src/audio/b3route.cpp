// B3ROUTE.IRX as host C++ (b3route.h; spec docs/audio-b3route.md, its section numbers below).
#include "b3route.h"

#include "spu2_mixer.h"

#include <algorithm>
#include <cstring>

namespace b3native {

namespace sd = b3audio::sd;

namespace {
constexpr uint16_t kIrqEnable1 = sd::C_IRQ_ENABLE | 1, kIrqa1 = sd::A_IRQA | 1;
constexpr uint32_t kCmdInit = 0x8000, kCmdChannels = 0x8050, kCmdStart = 0x8020, kCmdStop = 0x30, kCmdNop = 0x10;
}  // namespace

B3Route::B3Route(B3RoutePlatform& p, RwaIop& rwa, const B3RouteOptions& opt)
    : pf_(p), rwa_(rwa), opt_(opt), img_(kImageBytes, 0) {
    // irx_0000045C: the old handler / argument it saves are RWA's (§2); here the chain is RwaIop's hook.
    rwa_.set_spu2_irq_hook(this);
}

B3Route::~B3Route() { rwa_.set_spu2_irq_hook(nullptr); }

uint32_t B3Route::r32(uint32_t off) const {
    uint32_t v = 0;
    for (int k = 3; k >= 0; --k) v = v << 8 | img_[off + k];
    return v;
}
void B3Route::w32(uint32_t off, uint32_t v) {
    for (int k = 0; k < 4; ++k) img_[off + k] = static_cast<uint8_t>(v >> (8 * k));
}
uint32_t B3Route::ee_target() const { return r32(kOffEeTarget); }

// ---- the RPC server, irx_000002C0 (§3) ----
void B3Route::call(uint32_t fno, const void* send, uint32_t send_size, void* recv, uint32_t recv_size) {
    (void)recv;
    (void)recv_size;   // nothing is answered (the EE asks for no reply data)
    ++st_.rpc_calls;
    if (fno != kRpcId) ++st_.wrong_fno;   // "Wrong func!", then carries on [C 0x2DC]
    // The request lands in the 0x400-byte receive buffer at 0x2280.
    const uint32_t n = std::min<uint32_t>(send_size, 0x400);
    std::memcpy(img_.data() + kOffRpcBuf, send, n);
    const uint32_t words = n / 4;
    auto word = [&](uint32_t i) { return r32(kOffRpcBuf + 4 * i); };
    if (words == 0) return;
    const uint64_t t = rwa_.now();
    uint32_t count = word(0), i = 1;
    for (; count != 0; --count) {
        // Each command's words must be inside the request (retail does not check; a native read must not overrun).
        auto need = [&](uint32_t k) {
            if (i + k <= words) return true;
            ++st_.truncated;
            return false;
        };
        if (!need(1)) return;
        ++st_.commands;
        switch (word(i)) {
        case kCmdInit:   // irx_00000630(mask), then [0xE74] = EE buffer [C 0x360-0x378]
            if (!need(3)) return;
            routing(t, word(i + 2));
            w32(kOffEeTarget, word(i + 1));
            i += 3;
            break;
        case kCmdChannels:
            if (!need(2)) return;
            routing(t, word(i + 1));
            i += 2;
            break;
        case kCmdStart:   // irx_0000070C: arm core 1 at 0x3800
            rearm(t, kIrqaA);
            i += 1;
            break;
        case kCmdStop: {   // irx_00000768: BVOL L/R of both cores 0, then core 1's IRQ off
            auto& mx = rwa_.mixer();
            mx.set_param(t, sd::P_BVOLL | 0, 0);
            mx.set_param(t, sd::P_BVOLR | 0, 0);
            mx.set_param(t, sd::P_BVOLL | 1, 0);
            mx.set_param(t, sd::P_BVOLR | 1, 0);
            mx.set_core_attr(t, kIrqEnable1, 0);
            i += 1;
            break;
        }
        case kCmdNop:   // irx_00000704
            i += 1;
            break;
        default:   // "Received unknown SIF cmd %i!", one word
            ++st_.unknown_commands;
            i += 1;
            break;
        }
    }
}

// irx_00000630 (§4).
void B3Route::routing(uint64_t t, uint32_t mask) {
    auto& mx = rwa_.mixer();
    const uint32_t m = mask & 0xFFFFFF, nm = ~mask & 0xFFFFFF;
    mx.set_param(t, sd::P_MMIX | 0, mx.get_param(t, sd::P_MMIX | 0));   // written back unchanged [C 0x644-0x668]
    mx.set_param(t, sd::P_MMIX | 1, mx.get_param(t, sd::P_MMIX | 1));
    mx.set_param(t, sd::P_BVOLL | 0, 0);
    mx.set_param(t, sd::P_BVOLR | 0, 0);
    mx.set_param(t, sd::P_BVOLL | 1, 0x7FFF);
    mx.set_param(t, sd::P_BVOLR | 1, 0x7FFF);
    mx.set_switch(t, sd::S_VMIXL | 0, nm);
    mx.set_switch(t, sd::S_VMIXR | 0, nm);
    mx.set_switch(t, sd::S_VMIXEL | 0, m);
    mx.set_switch(t, sd::S_VMIXER | 0, m);
}

void B3Route::rearm(uint64_t t, uint32_t irqa) {
    auto& mx = rwa_.mixer();
    mx.set_core_attr(t, kIrqEnable1, 0);
    mx.set_addr(t, kIrqa1, irqa);
    mx.set_core_attr(t, kIrqEnable1, 1);
}

// ---- the SPU2 interrupt, irx_00000170 (§2.1) and the phases (§5) ----
uint32_t B3Route::spu2_irq(uint64_t t, uint32_t cores) {
    if (cores & 2) {
        ++st_.irqs;
        const uint32_t irqa = rwa_.mixer().get_addr(t, kIrqa1);
        switch (irqa) {
        case kIrqaA:   // irx_000007E0
            ++st_.phases[0];
            send_clean(kOffCleanA, 0);
            read_ring(t, kReadA, kOffCleanB);
            rearm(t, kIrqaB);
            break;
        case kIrqaB:   // irx_000008BC
            ++st_.phases[1];
            write_fuzzy(t);
            rearm(t, kIrqaC);
            break;
        case kIrqaC:   // irx_00000944
            ++st_.phases[2];
            send_clean(kOffCleanB, 1);
            read_ring(t, kReadC, kOffCleanA);
            rearm(t, kIrqaD);
            break;
        case kIrqaD:   // irx_00000A24
            ++st_.phases[3];
            write_fuzzy(t);
            rearm(t, kIrqaA);
            break;
        default:
            // "Warning, unknown address on B3dist core!" and RWA's handler with the bits as they came. (Retail then
            // calls it a second time with bit 1 cleared if bit 0 is set too; one call here.)
            ++st_.unknown_irqa;
            return cores;
        }
    }
    return (cores & 1) ? (cores & ~2u) : 0;
}

void B3Route::send_clean(uint32_t off_clean, uint32_t opt_low) {
    ++st_.sends;
    const uint32_t ee = ee_target();
    // The packet at 0x26C0: B3ROUTE writes +0xC (opt = Fuzzy's IOP address, | 1 for a second-half block); sifcmd
    // fills the header: psize | dsize << 8, the EE destination, cid [L SDK sifcmd.h sceSifCmdHdr].
    w32(kOffPacket + 0xC, (opt_.module_base + kOffFuzzy) | opt_low);
    w32(kOffPacket + 0x0, 0x18 | kCleanBytes << 8);
    w32(kOffPacket + 0x4, ee);
    w32(kOffPacket + 0x8, 6);
    uint32_t pkt[6];
    for (uint32_t k = 0; k < 6; ++k) pkt[k] = r32(kOffPacket + 4 * k);
    pf_.write_ee(ee, img_.data() + off_clean, kCleanBytes);   // the SIF DMA lands before the handler runs
    pf_.send_cmd(pkt, 0x18);
}

void B3Route::read_ring(uint64_t t, uint32_t spu, uint32_t off) {
    (void)t;
    if (!rwa_.dma_channel_idle(0)) {
        ++st_.skipped_busy;
        return;
    }
    ++st_.reads;
    // The mixer has run up to t (the IRQ's sample); the transfer completes at once.
    const uint8_t* ram = rwa_.mixer().sound_ram();
    for (uint32_t k = 0; k < kCleanBytes; ++k) img_[off + k] = ram[(spu + k) & 0x1FFFFF];
}

void B3Route::write_fuzzy(uint64_t t) {
    if (!rwa_.dma_channel_idle(0)) {
        ++st_.skipped_busy;
        return;
    }
    ++st_.writes;
    rwa_.mixer().voice_trans(t, 0, kMemin1, img_.data() + kOffFuzzy, kFuzzyBytes);
}

// ---- EE -> IOP DMA into the module image ----
bool B3Route::sif_dma_to_iop(uint32_t iop_addr, const void* src, uint32_t n) {
    const uint32_t a = iop_addr & 0x1FFFFF, base = opt_.module_base & 0x1FFFFF;
    if (a < base || a - base > kImageBytes || n > kImageBytes - (a - base)) return false;
    ++st_.ee_dmas;
    std::memcpy(img_.data() + (a - base), src, n);
    return true;
}

}  // namespace b3native
