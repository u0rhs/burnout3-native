// platform_sif.h -- the EE <-> IOP command channel and the IOP's memory (src/native/platform_sif.cpp), what the platform
// keeps of SIF since S4 of the SIF lane (2026-11-11). No SIF RPC is served: every IOP service the game uses is answered
// at its library's or the game's own entry points (platform_devices.cpp, platform_dbc.cpp, platform_gtfs.cpp,
// platform_distortion.cpp); sceSifBindRpc / sceSifCallRpc / sceSifCheckStatRpc are fatal.
//
// What remains:
//  - SIF commands: RW Audio's command channel (EE -> IOP command 0 to its IOP side, src/native/rwa_iop.h; IOP -> EE
//    command 1 back to the handler RW Audio registered) -- the platform's audio boundary -- and the delivery of
//    completions to the game's handlers in interrupt context: the file system's read completion (command 4,
//    platform_gtfs.cpp), the memory card's (platform_dbc.cpp), RW Audio's replies. A completion is queued and
//    delivered at the next safe point with interrupts enabled: the next dispatched call (platform_iop.cpp time_check),
//    a semaphore wait that would block, a VBlank.
//  - the IOP's heap (sceSifAllocSysMemory): RW Audio and the sound banks hand its addresses to RW Audio's IOP side.
//  - the kernel's SIF DMA ids (sceSifSendCmd's result).
#ifndef B3NATIVE_PLATFORM_SIF_H
#define B3NATIVE_PLATFORM_SIF_H

#include "b3host/host.h"

#include <cstdint>
#include <cstdio>
#include <functional>

namespace b3native::save { class ProfileCard; }

namespace b3native::platform {
// B3NATIVE_SIF_LOG=<file>: the EE <-> IOP traffic, one line per event (platform_sif.cpp); null when off
std::FILE* sif_log();
}  // namespace b3native::platform

namespace b3native::platform::sif {

// ---- the kernel's SIF DMA ids
// sceSifSendCmd returns the id of the SIF DMA that carries the command (nonzero, counting up from 1). A row that stands
// for retail code which sent a request to the IOP (an RPC: one SIF DMA) takes one too, so every later id -- RW Audio
// keeps the last one it got -- is the one the retail path gives.
std::int32_t next_dma_id();

// ---- the IOP heap (sceSifAllocSysMemory / sceSifFreeSysMemory)
// IOP RAM is 2 MB; the platform's heap is its upper MB, [0x100000, 0x200000), handed out first fit by address in
// 256-byte units (the IOP's allocation unit). 0: no room (or 0 bytes).
std::uint32_t iop_alloc(std::uint32_t bytes);
int iop_free(std::uint32_t address);   // 0, or -1 if no block starts there

// ---- SIF commands, EE -> IOP
// A command as sceSifSendCmd sent it: the packet in guest memory with the header written (+0 packet size | data size
// << 8, +4 the data's IOP destination, +8 cid), `extra` the EE source of the data (to `extra_dest` in IOP memory).
struct SentCmd {
    std::uint32_t cid;
    b3host::gaddr packet;
    std::uint32_t packet_size;
    b3host::gaddr extra;
    std::uint32_t extra_dest, extra_size;
    std::int32_t dma_id;   // what sceSifSendCmd returned
};
using CmdSink = std::function<void(const SentCmd&)>;
// The IOP side of command `cid` (replaces an earlier one; an empty function removes it). A command without one is fatal.
void set_cmd_sink(std::uint32_t cid, CmdSink sink);

// ---- IOP -> EE: completions delivered to the game's handlers in interrupt context
// libsif's command receive buffer (D_004E3EC0 through the uncached mirror), where an IOP -> EE command packet lands
constexpr b3host::gaddr kCmdReceiveBuffer = 0x204E3EC0u;
// A command packet for the handler the game registered for `cid` (sceSifAddCmdHandler): at delivery the packet is
// copied to `at` (default the receive buffer) and the handler runs as handler(at, its registered data). A cid with no
// handler at delivery is dropped (noted).
void queue_cmd_reply(std::uint32_t cid, const void* packet, std::uint32_t size, b3host::gaddr at = 0);
// A guest function called in interrupt context with a0..a3, t0 (RW Audio's reply handler, the card's completion).
void queue_guest_call(b3host::gaddr handler, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2 = 0,
                      std::uint32_t a3 = 0, std::uint32_t t0 = 0);
// The same, due once virtual EE time reaches `ready` (no earlier than the last timed one: they stay in order).
void queue_guest_call_at(std::uint64_t ready, b3host::gaddr handler, std::uint32_t a0, std::uint32_t a1);
std::uint64_t sif_next_ready();   // the next timed item's time, or ~0
// The safe point: every due completion delivered in order, if interrupts are on (else they wait for the next one).
void sif_service();
bool sif_pending();

// ---- the controller ports and the card slot (src/native/platform_dbc.cpp: libdbc answered at its entry points) ----
// The 32-byte pad report the game reads (libpad2's data, copied to CGtInputManagerPS2Pad2 +0x218 + port * 0x20): bytes
// 0-1 digital buttons active low, 2-5 sticks (4/5 = left X/Y), 6-17 button pressures, 18-21 0x80. Default: the
// neutral report (no button, sticks centred) for both ports.
void set_pad_sample(int port, const unsigned char sample[32]);
// Port 0 starts with a DualShock 2 plugged in, port 1 empty.
void set_pad_connected(int port, bool connected);
const unsigned char* neutral_pad_sample();   // the 32 neutral bytes
// rumble1 (remaster-features §3; src/native/pad_actuator.h): the pads' actuator requests (sceDbcSendData2, libvib).
// Off (the default): the first request is taken and left pending, every later one finds the channel busy -- the port
// before rumble1, no motor driven. On (PC SETTINGS > RUMBLE): the device manager answers each request (its RPC reply,
// a short virtual time after it) and the motor values are kept per port. Call between VBlanks (native_play's loop);
// a recording keeps the switches (pad_record.h) so a replay sees the same answers.
void set_actuator_ack(bool on);
bool actuator_ack();
// The motors the game last set on the pad in `port` (0 / 1) while acknowledging: small 0 / 1, large 0-255; `serial`
// counts that port's acknowledged requests.
void pad_motors(int port, unsigned char& small, unsigned char& large, std::uint32_t* serial = nullptr);
// The acknowledged requests so far (and in `busy` the ones dropped because the previous one was still in flight)
std::uint64_t actuator_requests(std::uint64_t* busy = nullptr);
// The device manager's VBlank: the card inserted when its time comes, settled saves written, the pads' reports (call at
// each VBlank).
void pad_vblank();

// save1 (remaster-features §8): the card's profile (null without $B3NATIVE_SAVES_ROOT); a pending save written now.
save::ProfileCard* save_profile_card();
void save_card_flush();

}  // namespace b3native::platform::sif

#endif
