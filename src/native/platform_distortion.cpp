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
// The send's IOP->EE half, SIF command 6 (B3ROUTE's sound blocks for func_002FC530 to distort), never comes, as before.
#include "platform.h"
#include "platform_charge.h"
#include "platform_sif.h"

#include "b3host/call.h"

#include <cstdio>

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

constexpr gaddr kSifInitRpc = 0x0010E880u, kSifBindRpc = 0x0010F018u, kSifCallRpc = 0x0010F1E8u,
                kSifCheckStatRpc = 0x0010F3E0u, kSifAddCmdHandler = 0x0010E408u,
                kDspConstruct = 0x002FC160u,   // CB3SoundDSPDataBlock::Construct()
                kDspSetType = 0x002FC150u,     // CB3SoundDSPDataBlock::SetType(EDSPType)
                kDspReset = 0x002FC0E0u,       // CB3SoundDSPDataBlock::Reset()
                kDistortHandler = 0x002FC530u, // the command-6 handler
                kSelectedCar = 0x0064ED5Cu,    // D_0064ED5C (signed byte): the player whose car is distorted
                kCars = 0x0055EFECu;           // D_0055EFEC: per player {car, ...}, 0x30 bytes
constexpr std::uint32_t kServer = 0x00150276u;
// CB3SoundDistortion's fields [C the decomp's CB3SoundDistortion declarations]
constexpr std::uint32_t kOn = 0x184C, kFlag184F = 0x184F, kPlayer = 0x18D0, kList = 0x1900, kListCount = 0x1D00,
                        kListNext = 0x1D04, kClient = 0x1D10;

// The list's send, where the retail code checks the client (sceSifCheckStatRpc: never busy, a send completes at once)
// and makes the RPC: its time, the request in the SIF log, one SIF DMA. The list itself has no receiver.
void send_list() {
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
    send_list();
    st(count, 0);
    st(self + kListNext, self + kList + 4);
}
void push(gaddr self, std::uint32_t word) {
    const gaddr at = ld(self + kListNext);
    st(self + kListNext, at + 4);
    st(at, word);
}

}  // namespace

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
    if (sends) st(count, 0);
    b3gen_002FCA60(b3host::gp<void>::from_raw(self));
    if (sends) {
        if (ld(count) != 0) stub_unexpected("CB3SoundDistortion::Update", "a command queued while Update ran");
        send_list();
        st(self + kListNext, self + kList + 4);
    }
    r = CallResult{};
}

}  // namespace b3native::platform
