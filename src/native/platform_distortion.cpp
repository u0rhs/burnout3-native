// The sound distortion's IOP side (S4 of the SIF lane, 2026-11-11; platform.h B3NATIVE_FIRST).
//
// CB3SoundDistortion (B3SoundDistPS2.cpp in the retail tables) drives Criterion's IOP module B3ROUTE.IRX, which routes
// the engine voices through the SPU2's second core for the distortion effect: the class queues commands in its own
// list (this + 0x1900: a count, then the commands -- 0x8000 start-up {EE address, voice mask}, 0x8050 voices {mask},
// 0x8020 on, 0x30 off) and sends the list with an RPC to server 0x150276 when it has something in it, from Construct,
// StartDistortion, StopDistortion and Update (SetChannels only queues). The PC's audio does not route voices through a
// second core (the SPU2-equivalent mixer has no B3ROUTE), so the list has no receiver; what the game sees of the RPC is
// its time and the list emptied. Construct / StartDistortion / StopDistortion are rows written from the decomp [C decomp
// src/b3/_unclassified/B3SoundDistPS2_002FD0E0.cpp, src/b3/gamesource/Sound/Dsp/B3SoundDistPS2.cpp, matched], their
// kernel / libsif-command / game calls made for real and the RPC (with libsif's status check before it) charged
// (platform_charge.h). Update (0x2FCA60) is game code -- the speaker gains from the car and the listener -- that ends
// with the same send: its row runs the generated function and answers the send for it (dist_update, below).
//
// engpan (2026-11-18): the lists now reach B3ROUTE (src/audio/b3route.h, beside RW Audio's executor in
// platform_rwa.cpp), and the send's IOP->EE half, SIF command 6 with B3ROUTE's sound blocks, reaches its handler
// func_002FC530: a row below, with the DSP block's distortion ProcessB2Distortion (both hand-written asm in retail, so
// not generated). Before, the routing never happened and the player car's engine-model voices played on the dry bus
// with the engine model's channel pans (one stereo layer hard left, one hard right: the bass on one side, the mids /
// highs on the other); on a PS2 they reach the output only through this round trip, mixed to mono and panned by the
// gains Update computes from the car and the listener.
#include "platform.h"
#include "platform_charge.h"
#include "platform_sif.h"

#include "b3host/call.h"

#include <cstdio>
#include <string>
#include <vector>

// the generated CB3SoundDistortion::Update (gen/native/units/Update__18CB3SoundDistortionFv.cpp)
void b3gen_002FCA60(b3host::gp<void> self);

namespace b3native::platform {
namespace {

using b3host::CallArgs;
using b3host::CallResult;
using b3host::gaddr;

inline std::uint32_t u32(const CallArgs& a, unsigned i) { return a.gpr[i].w[0]; }
inline std::uint32_t ld(gaddr p) { return b3host::load<std::uint32_t>(p); }
inline void st(gaddr p, std::uint32_t v) { b3host::store<std::uint32_t>(p, v); }
inline std::uint8_t ld8(gaddr p) { return b3host::load<std::uint8_t>(p); }
inline void st8(gaddr p, std::uint8_t v) { b3host::store<std::uint8_t>(p, v); }
inline void stf(gaddr p, float v) { b3host::store<float>(p, v); }
std::string hex(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

constexpr gaddr kSifInitRpc = 0x0010E880u, kSifBindRpc = 0x0010F018u, kSifCallRpc = 0x0010F1E8u,
                kSifCheckStatRpc = 0x0010F3E0u, kSifAddCmdHandler = 0x0010E408u,
                kDspConstruct = 0x002FC160u,   // CB3SoundDSPDataBlock::Construct()
                kDspSetType = 0x002FC150u,     // CB3SoundDSPDataBlock::SetType(EDSPType)
                kDspReset = 0x002FC0E0u,       // CB3SoundDSPDataBlock::Reset()
                kDistortHandler = 0x002FC530u, // the command-6 handler
                kSelectedCar = 0x0064ED5Cu,    // D_0064ED5C (signed byte): the player whose car is distorted
                kCars = 0x0055EFECu;           // D_0055EFEC: per player {car, ...}, 0x30 bytes
constexpr gaddr kDspProcess = 0x002FC070u,     // CB3SoundDSPDataBlock::Process(short *, short *)
                kFinalMix = 0x002FC820u,       // _FinalMix(int, short *, short *, int, int, int)
                kSifSetDmaI = 0x0010C1F0u;     // isceSifSetDma
constexpr std::uint32_t kServer = 0x00150276u;
// CB3SoundDistortion's fields [C the decomp's CB3SoundDistortion declarations]
constexpr std::uint32_t kOn = 0x184C, kFlag184F = 0x184F, kPlayer = 0x18D0, kList = 0x1900, kListCount = 0x1D00,
                        kListNext = 0x1D04, kClient = 0x1D10;

// The list's send, where the retail code checks the client (sceSifCheckStatRpc: never busy, a send completes at once)
// and makes the RPC: its time, the request in the SIF log, one SIF DMA; the list (0x400 bytes from its count word, as
// sceSifCallRpc sends it) to B3ROUTE (engpan, platform_rwa.cpp).
void send_list(const std::uint8_t (&list)[0x400]) {
    b3route_rpc(list, sizeof list);
    charge(kSifCheckStatRpc);
    if (std::FILE* f = sif_log())
        std::fprintf(f, "R %llu %llu %x %x %x %x\n", (unsigned long long)vblank_count(), (unsigned long long)virtual_cycles(),
                     kServer, kServer, 0x400u, 0u);
    sif::next_dma_id();
    charge(kSifCallRpc);
}
// `if (count) { send; count = 0; next = first command }`, the tail of every function that sends
void flush(gaddr self) {
    const gaddr count = ld(self + kListCount);
    if (ld(count) == 0) return;
    std::uint8_t list[0x400];
    b3host::copy_from_guest(list, self + kList, sizeof list);
    send_list(list);
    st(count, 0);
    st(self + kListNext, self + kList + 4);
}
void push(gaddr self, std::uint32_t word) {
    const gaddr at = ld(self + kListNext);
    st(self + kListNext, at + 4);
    st(at, word);
}

}  // namespace

// _FinalMix(int n, short *l, short *r, int, int gl, int gr) 0x2FC820 [C decomp src/b3/gamesource/Sound/Dsp/
// B3SoundDistPS2.cpp, hand-written MMI asm]: per sample, m = ((l + r) >> 1) * 0x7FFF >> 15, then l = m * gl' >> 15 and
// r = m * gr' >> 15 (packed back to 16 bits, no saturation), where g' = g / 2 (toward zero, pan gains in 16.16) and at
// most 0x7FFF (the multiplies take a word's low halfword, signed: pmulth). The fourth argument is unused. Retail runs 64
// samples a pass, n a multiple of 64 (always 0x100).
void dist_final_mix(const CallArgs& args, CallResult& res) {
    const std::uint32_t n = u32(args, 0);
    const gaddr l = u32(args, 1), r = u32(args, 2);
    const std::int32_t gl = static_cast<std::int32_t>(u32(args, 4)), gr = static_cast<std::int32_t>(u32(args, 5));
    std::int32_t a = gl / 2, b = gr / 2;
    if (a >= 0x8000) a = 0x7FFF;
    if (b >= 0x8000) b = 0x7FFF;
    const std::int32_t ga = static_cast<std::int16_t>(a), gb = static_cast<std::int16_t>(b);
    std::int16_t L[0x100], R[0x100];
    if (n != 0x100) stub_unexpected("_FinalMix", "a block of " + std::to_string(n) + " samples (the handler's is 0x100)");
    b3host::copy_from_guest(L, l, sizeof L);
    b3host::copy_from_guest(R, r, sizeof R);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::int32_t s = (std::int32_t{L[i]} + std::int32_t{R[i]}) >> 1;
        const std::int32_t m = static_cast<std::int16_t>((s * 0x7FFF) >> 15);
        L[i] = static_cast<std::int16_t>((m * ga) >> 15);
        R[i] = static_cast<std::int16_t>((m * gb) >> 15);
    }
    b3host::copy_to_guest(l, L, sizeof L);
    b3host::copy_to_guest(r, R, sizeof R);
    res = CallResult{};
}

// void func_002FC530(sceSifCmdHdr *pkt, CB3SoundDistortion *this) 0x2FC530, SIF command 6's handler (registered by
// Construct), in interrupt context [C decomp src/b3/_unclassified/func_002FC530.cpp, hand-written asm; spec
// docs/audio-b3route.md §6.2]. Input: B3ROUTE's 256 fresh left samples at this + 0x240, right at + 0x640. Two output
// frames in B3ROUTE's Fuzzy layout (L 0-255, L 256-511, R 0-255, R 256-511, 0x200 bytes each): X = this + 0x840, Y =
// this + 0x1040. pkt + 0xC = opt, Fuzzy's IOP address, | 1 for a second-half block:
//   even: X's first halves copied to Y, the two DSP blocks (left: this, right: this + 0x110) into X's first halves,
//         _FinalMix of them with the gains + 0x1840 / + 0x1844, then Y DMAed to opt;
//   odd:  Y's second halves copied to X, the DSP into Y's second halves, _FinalMix, X DMAed to opt - 1.
// The DMA is isceSifSetDma({frame, opt, 0x800, 0}, 1) (sif_set_dma: into B3ROUTE's image); retail then `ei`s.
void dist_cmd6_handler(const CallArgs& a, CallResult& r) {
    const gaddr pkt = u32(a, 0), self = u32(a, 1);
    std::uint32_t opt = ld(pkt + 0xC);
    const gaddr in_l = self + 0x240, in_r = self + 0x640, x = self + 0x840, y = self + 0x1040;
    std::uint8_t tmp[0x200];
    auto copy = [&](gaddr to, gaddr from) {
        b3host::copy_from_guest(tmp, from, sizeof tmp);
        b3host::copy_to_guest(to, tmp, sizeof tmp);
    };
    gaddr out_l, out_r, send;
    if (opt & 1) {
        opt -= 1;
        copy(x + 0x200, y + 0x200);
        copy(x + 0x600, y + 0x600);
        out_l = y + 0x200;
        out_r = y + 0x600;
        send = x;
    } else {
        copy(y, x);
        copy(y + 0x400, x + 0x400);
        out_l = x;
        out_r = x + 0x400;
        send = y;
    }
    b3host::call_guest<void>(kDspProcess, self, in_l, out_l);
    b3host::call_guest<void>(kDspProcess, self + 0x110u, in_r, out_r);
    b3host::call_guest<void>(kFinalMix, 0x100u, out_l, out_r, ld(self + 0x1848), ld(self + 0x1840), ld(self + 0x1844));
    charge(kSifSetDmaI);
    const void* src = b3host::host_pointer(send, 0x800);
    if (!src || !b3route_sif_dma(opt, src, 0x800)) {
        // without B3ROUTE (B3_B3ROUTE=0 never sends; an opt that is not its Fuzzy buffer): retail's DMA would go nowhere
        stub_note("func_002FC530", "the distorted frame's DMA has no IOP receiver: dropped");
    }
    sif::next_dma_id();
    r = CallResult{};
}

// engpan: unsigned int sceSifSetDma(sceSifDmaData *sdd, int len) / isceSifSetDma 0x10C1E0 / 0x10C1F0 (libsif; here
// because B3ROUTE is its one receiver) [SDK sif.h: sdd[i] = {data (EE address), addr (IOP address), size, mode}; the
// transfer's id, 0 when the queue is full]: the one caller in the game is the command-6 handler above, whose frame goes
// back into B3ROUTE's Fuzzy buffer (platform_rwa.cpp). The transfer completes at once (sceSifDmaStat,
// platform_sif.cpp); one to any other IOP address has no receiver and is fatal.
void sif_set_dma(const CallArgs& a, CallResult& r) {
    const gaddr sdd = u32(a, 0);
    const std::int32_t len = static_cast<std::int32_t>(u32(a, 1));
    for (std::int32_t i = 0; i < len; ++i) {
        const gaddr e = sdd + static_cast<std::uint32_t>(i) * 16u;
        const std::uint32_t data = ld(e), addr = ld(e + 4), size = ld(e + 8);
        const void* src = b3host::host_pointer(data, size);
        if (!src || !b3route_sif_dma(addr, src, size))
            stub_unexpected("sceSifSetDma", "a transfer of " + hex(size) + " bytes from " + hex(data) + " to IOP " + hex(addr) +
                                                ": no IOP side for it (B3ROUTE's Fuzzy buffer is the one receiver)");
    }
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(sif::next_dma_id())));
}

// void CB3SoundDSPDataBlock::ProcessB2Distortion(short *in, short *out) 0x2FBE20 [C decomp src/b3/_unclassified/
// B3SoundDSPBlock_002FBE20.cpp, hand-written asm, byte-identical]: `+0x44` samples (256) through a state-variable filter
// (state +0x14 band, +0x18 low; coefficients +0xC, +0x10) whose low-pass output, driven by +0x0, folds back at 18 bits,
// then gain +0x34, a clamp, the wet level (+0x3C * +0x38 >> 16) and the dry level (+0x40 * +0x38 >> 16) summed and
// clamped to 16 bits. Every multiply is MIPS `mult`'s low word, shifted right arithmetically by 12 (Q12); the adds are
// 32-bit (retail's trapping add / sub never trap here). Retail runs 64-sample blocks (the count a multiple of 64).
void dist_b2_distortion(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0), in = u32(a, 1), out = u32(a, 2);
    auto w = [&](std::uint32_t off) { return static_cast<std::int32_t>(ld(self + off)); };
    auto mul = [](std::int32_t p, std::int32_t q) {   // mult's LO (sign-extended), then sra 12
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(p) * static_cast<std::uint32_t>(q)) >> 12;
    };
    auto add = [](std::int32_t p, std::int32_t q) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(p) + static_cast<std::uint32_t>(q));
    };
    auto sub = [](std::int32_t p, std::int32_t q) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(p) - static_cast<std::uint32_t>(q));
    };
    auto clamp16 = [](std::int32_t v) { return v > 0x7FFF ? 0x7FFF : (v < -0x8000 ? -0x8000 : v); };
    std::int32_t s0 = w(0x14), s1 = w(0x18), s2 = w(0xC), s3 = w(0x10), s4 = w(0x34);
    const std::int64_t p38 = w(0x38);
    std::int32_t s5 = static_cast<std::int32_t>((std::int64_t{w(0x3C)} * p38) >> 16);
    std::int32_t s6 = static_cast<std::int32_t>((std::int64_t{w(0x40)} * p38) >> 16);
    const std::int32_t drive = static_cast<std::int32_t>(static_cast<std::uint32_t>(add(w(0x0), 0x10000)) << 1) >> 4;
    const std::uint32_t n = static_cast<std::uint32_t>(w(0x44));
    s2 >>= 4;
    s3 >>= 4;
    s4 >>= 4;
    s5 >>= 4;
    s6 >>= 4;
    if (n == 0 || (n & 0x3F) || n > 0x1000)
        stub_unexpected("CB3SoundDSPDataBlock::ProcessB2Distortion", "a block of " + std::to_string(n) + " samples (retail runs 64 at a time)");
    std::vector<std::int16_t> buf(n);
    b3host::copy_from_guest(buf.data(), in, n * 2u);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::int32_t xs = buf[i];
        s1 = add(mul(s2, s0), s1);
        std::int32_t a0 = sub(sub(xs, s1), mul(s3, s0));
        a0 = mul(a0, s2);
        s0 = add(a0, s0);
        a0 = mul(s1, drive);
        std::int32_t a1 = static_cast<std::int32_t>(static_cast<std::uint32_t>(add(0x10000, a0)) << 14) >> 31;
        a0 ^= a1;
        a0 ^= 0x20000 & a1;
        a0 = static_cast<std::int32_t>(static_cast<std::uint32_t>(a0) << 14) >> 14;
        a0 = clamp16(mul(a0, s4));
        a0 = mul(s5, a0);
        a0 = clamp16(add(mul(xs, s6), a0));
        buf[i] = static_cast<std::int16_t>(a0);
    }
    b3host::copy_to_guest(out, buf.data(), n * 2u);
    b3host::store<std::uint32_t>(self + 0x14, static_cast<std::uint32_t>(s0));
    b3host::store<std::uint32_t>(self + 0x18, static_cast<std::uint32_t>(s1));
    r = CallResult{};
}

// void CB3SoundDistortion::Construct(RwaSPU2Output *out, int voices) 0x2FD0E0
void dist_construct(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0);
    const std::uint32_t voices = u32(a, 2);
    st8(self + kOn, 0);
    st8(self + kFlag184F, 0);
    b3host::call_guest<void>(kSifInitRpc, 0);
    sif::next_dma_id();   // the bind: one request, answered the first time
    charge(kSifBindRpc);
    b3host::call_guest<void>(kSifAddCmdHandler, 6u, kDistortHandler, self);
    st(self + kListCount, self + kList);
    st(self + kListNext, self + kList + 4);
    st(self + 0x1D08, 0);
    st(self + 0x1D0C, 0);
    st(self + kList, 0);
    push(self, 0x8000u);   // start-up: {EE address of the distortion's buffers, the voices}
    const gaddr cmd = ld(self + kListNext);
    st(self + kListNext, cmd + 8);
    st(self + kList, ld(self + kList) + 1);
    st(cmd, self + 0x240);
    st(cmd + 4, voices);
    flush(self);
    for (std::uint32_t i = 0; i < 2; ++i) b3host::call_guest<void>(kDspConstruct, self + i * 0x110u);
    stf(self + 0x18F8, 50.0f);
    stf(self + 0x18FC, 10.0f);
    b3host::call_guest<void>(kDspSetType, self, 0);
    b3host::call_guest<void>(kDspSetType, self + 0x110u, 0);
    b3host::call_guest<void>(kDspReset, self);
    b3host::call_guest<void>(kDspReset, self + 0x110u);
    st(self + 0x1840, 0x10000u);
    st(self + 0x1844, 0x10000u);
    st(self + 0x1848, 0x10000u);
    r = CallResult{};
}

// void CB3SoundDistortion::StartDistortion() 0x2FCFC0
void dist_start(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0);
    r = CallResult{};
    if (ld8(self + kOn) != 0) return;
    st(self + kPlayer, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(ld8(kSelectedCar)))));
    push(self, 0x8020u);
    const gaddr count = ld(self + kListCount);
    st(count, ld(count) + 1);
    flush(self);
    st8(self + kOn, 1);
}

// void CB3SoundDistortion::StopDistortion() 0x2FCF00
void dist_stop(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0);
    r = CallResult{};
    if (ld8(self + kOn) == 0) return;
    push(self, 0x30u);
    const gaddr count = ld(self + kListCount);
    st(count, ld(count) + 1);
    st8(self + kOn, 0);
    flush(self);
}

// void CB3SoundDistortion::Update() 0x2FCA60: the generated function, whose last step sends the list when it holds
// commands (SetChannels') and the player has a car (the function returns at once without one). The row empties the list
// before the generated code reaches that test -- the code before it reads neither the list nor its count -- and takes
// the send's time where the retail code takes it, after the function's last call; then the list's next-command pointer
// as the retail code leaves it. A command queued while the function runs (another thread) would be fatal: the list's
// owner is the sound thread, which runs this function.
void dist_update(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0);
    const gaddr count = ld(self + kListCount);
    const std::int32_t player = static_cast<std::int32_t>(ld(self + kPlayer));
    const bool sends = ld(count) != 0 && ld(kCars + static_cast<std::uint32_t>(player) * 0x30u) != 0;
    std::uint8_t list[0x400];
    if (sends) {
        b3host::copy_from_guest(list, self + kList, sizeof list);
        st(count, 0);
    }
    b3gen_002FCA60(b3host::gp<void>::from_raw(self));
    if (sends) {
        if (ld(count) != 0) stub_unexpected("CB3SoundDistortion::Update", "a command queued while Update ran");
        send_list(list);
        st(self + kListNext, self + kList + 4);
    }
    r = CallResult{};
}

}  // namespace b3native::platform
