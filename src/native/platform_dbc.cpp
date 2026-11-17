// The controller ports and the memory card slot (sif-lane, 2026-11-10; platform.h B3NATIVE_FIRST): libdbc, the EE
// library both libpad2 (pads) and libmc2 (memory cards) sit on, answered at its own entry points.
//
// On a PS2 libdbc is the EE half of the IOP's device manager (DBCMAN): a "socket" is a device on a port, its state is
// one word per socket in a work area DBCMAN keeps up to date in EE memory, a pad's reports arrive by DMA in the two
// 0x80-byte buffers the socket was created with (the newer one has the larger counter at +0x7C), and a memory card
// answers page commands with a reply in the same kind of buffer and a completion callback. Every libdbc function is
// one SIF RPC to DBCMAN. Here the host is the device manager and answers the libdbc calls directly: no request is
// built, no RPC made, and libdbc's own .bss (its RPC clients, its request buffers, the work area and its copy) is not
// used. libpad2 and libmc2 stay the retail libraries (generated from the decomp): they see the same answers, and the
// same report and reply buffers in their memory, as from the PS2's device manager.
//
// What the device manager does here [C decomp src/libs/sce/libdbc.cpp, libpad2.cpp, libmc2.cpp for what the libraries
// read; G where noted]:
//   - Sockets: 16, the lowest free id for a new one (-21 when none); a socket remembers its type (1 a pad, 2 a memory
//     card), its port and its two buffers.
//   - Status words (sceDbcGetConnection: 1 connected, else 3; sceDbcGetDepNumber needs 1): a pad socket's is 1 while a
//     controller is plugged in its port (published at the next VBlank, and at the socket's creation when the port
//     already holds a pad: pad2's hot-plug); a card socket's is 1 once the card is
//     present (B3NATIVE_CARD_PRESENT_VBLANK, default 600 -- the card "inserted" a while after the boot).
//   - At every VBlank a plugged pad's report goes into the older of its socket's buffers: state 1, data length 32,
//     profile length 5, +4 = 1, the 32 report bytes at +0x1C (digital buttons active low, sticks, pressures), the
//     profile FF FF FF FF 78 after them (libpad2 decodes it to the DualShock 2's channels), the counter +1 at +0x7C. A
//     pad pulled out gets one buffer with state 0 (libpad2 then reports it gone and the game deletes the socket).
//   - sceDbcGetDepNumber of a plugged, published pad: 0 (the driver linked). sceDbcReceiveData (libpad2's second state
//     poll in a VBlank): the state byte of the newest buffer, one byte (1 before a new socket's first report).
//   - Memory card page commands (sceDbcSendData3, libmc2's core): the card is the save card (save_card.h / save_store.h:
//     the profile's .sav); the reply header {sequence, socket, result, 116 data bytes or -1} goes to the socket's next
//     buffer (a page read's to the first, the pages after it), and libmc2's completion function runs from the card's
//     interrupt after the card's time for the command [L: about 295,000 EE cycles a page, plus 90,000, plus 590,000 for
//     a block erase] -- each after the previous one.
//   - sceDbcSendData2 (rumble; libvib): by default the first request is taken and stays pending (no motor acknowledges
//     it), so later ones find the channel busy and return 0; with RUMBLE On (rumble1, sif::set_actuator_ack) each
//     request is answered and its motor values kept for the port's pad (pad_actuator.h).
// Each row charges the virtual EE time of the retail libdbc code it replaces (platform_charge.h): its calls into libsif
// and the kernel, in order; the status copy runs with interrupts off (DIntr / EIntr called for real, as libdbc does).
#include "platform.h"
#include "platform_charge.h"
#include "platform_sif.h"
#include "pad_actuator.h"
#include "save_card.h"
#include "save_store.h"

#include "b3host/call.h"
#include "b3host/host.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace b3native::platform {
namespace {

using b3host::CallArgs;
using b3host::CallResult;
using b3host::gaddr;

constexpr gaddr kSignalSema = 0x0010BE40u, kWaitSema = 0x0010BE60u, kSyncDCache = 0x0010C718u,
                kSifInitRpc = 0x0010E880u, kSifBindRpc = 0x0010F018u, kSifCallRpc = 0x0010F1E8u,
                kSifCheckStatRpc = 0x0010F3E0u;
constexpr gaddr kDIntr = 0x00114E60u, kEIntr = 0x00114EB8u;

inline std::uint32_t u32(const CallArgs& a, unsigned i) { return a.gpr[i].w[0]; }
inline std::int32_t s32(const CallArgs& a, unsigned i) { return static_cast<std::int32_t>(a.gpr[i].w[0]); }
inline void ret(CallResult& r, std::int32_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(v)));
}
inline std::uint32_t ld(gaddr p) { return b3host::load<std::uint32_t>(p); }
inline void st(gaddr p, std::uint32_t v) { b3host::store<std::uint32_t>(p, v); }
std::string hex(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

// ---- the device manager's state
constexpr std::uint32_t kPad = 1, kCard = 2;
constexpr std::int32_t kNoSocket = -21, kBadSocket = -22, kNotLinked = -12;
struct Socket {
    bool used = false;
    std::uint32_t type = 0, port = 0;
    gaddr buf[2] = {0, 0};
    bool reported = false;          // a pad: its last VBlank report said "plugged in"
    std::int32_t counter = 0;       // a pad: the last report's counter
    std::uint32_t sequence = 1;     // a card: the next reply's sequence number
    unsigned next_buf = 0;          // a card: the buffer of the next reply
};
Socket g_sock[16];
std::uint32_t g_status[16] = {};   // the status words, one per socket id (1: connected); a new socket finds its id's word
                                   // as the last one left it (a card socket sets it, a pad's comes at its first report)
bool g_started = false;   // sceDbcInit done
bool g_work = false;      // the work area registered (status words published from then on)
struct Port {
    bool plugged;
    std::uint8_t report[32];
};
// the neutral DualShock 2 report: no button down (active low), sticks centred, no pressure
constexpr std::uint8_t kNeutral[32] = {0xFF, 0xFF, 0x80, 0x7F, 0x80, 0x7F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0,    0,    0x80, 0x80, 0x80, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kProfile[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x78};
Port make_port(bool plugged) {
    Port p{plugged, {}};
    std::memcpy(p.report, kNeutral, sizeof p.report);
    return p;
}
Port g_ports[2] = {make_port(true), make_port(false)};   // a DualShock 2 in port 1, nothing in port 2

bool valid(std::int32_t s) { return s >= 0 && s < 16 && g_sock[s].used; }
bool pad_plugged(const Socket& s) { return s.used && s.type == kPad && s.port < 2 && g_ports[s.port].plugged; }
// the newer of a socket's two buffers (libpad2's rule: the second when the first's counter is smaller, signed)
gaddr newest(const Socket& s) {
    return static_cast<std::int32_t>(ld(s.buf[0] + 0x7C)) < static_cast<std::int32_t>(ld(s.buf[1] + 0x7C)) ? s.buf[1] : s.buf[0];
}
void publish(std::int32_t id, std::uint32_t v) {
    if (g_work) g_status[id] = v;
}

// ---- the memory card
// The card in the slot: the active profile's save card (save_store.h), when $B3NATIVE_SAVES_ROOT names a saves folder
save::ProfileCard* profile_card() {
    static save::ProfileCard* c = []() -> save::ProfileCard* {
        const char* root = std::getenv("B3NATIVE_SAVES_ROOT");
        if (!root || !*root) return nullptr;
        auto* p = new save::ProfileCard(root);
        std::string err;
        if (!p->attach(save::read_active_slot(root), &err))
            std::fprintf(stderr, "[save] %s: an empty card this run, nothing saved (the file is left as it is)\n", err.c_str());
        stub_note("save card", "profile slot " + std::to_string(p->slot()) + " of " + root + " in the card slot" +
                                   (p->persist() ? "; saves written to its .sav" : "; saves kept in memory (B3NATIVE_CARD_PERSIST=1 writes them)"));
        return p;
    }();
    return c;
}
save::SaveCard* card() {
    save::ProfileCard* p = profile_card();
    return p ? &p->device() : nullptr;
}
bool g_card_in = false;   // the card counts as inserted
std::uint64_t card_in_vblank() {
    static const std::uint64_t v = [] {
        const char* e = std::getenv("B3NATIVE_CARD_PRESENT_VBLANK");
        return e && *e ? std::strtoull(e, nullptr, 10) : 600ull;
    }();
    return v;
}
bool card_ready() { return g_card_in && card() && card()->attached(); }

// One page command (libmc2's core, _sceMcCore*: the command block it sends, from its +0: first page, page count, page
// size, ..., +0x20 block, +0x28 the data to write). The result and the reply's 116 data bytes.
std::uint32_t card_command(std::uint32_t op, gaddr cmd, gaddr read_to, std::uint8_t data[116]) {
    save::SaveCard& c = *card();
    std::memset(data, 0, 116);
    const std::uint32_t first = ld(cmd), count = ld(cmd + 4), page_size = ld(cmd + 8);
    const bool pages_ok = page_size == 512u && count >= 1 && count <= 16;
    switch (op) {
    case 1: {   // read pages
        if (!pages_ok) return 0;
        std::uint8_t pages[16 * 512];
        if (!c.read_pages(first, count, pages, count * 512u)) return 0;
        b3host::copy_to_guest(read_to, pages, count * 512u);
        return 1;
    }
    case 2: {   // write pages
        if (!pages_ok) return 0;
        std::uint8_t pages[16 * 512];
        b3host::copy_from_guest(pages, cmd + 0x28, count * 512u);
        return c.write_pages(first, pages, count * 512u) ? 1u : 0u;
    }
    case 3:     // the card's geometry: 512-byte pages, 16 to a block, 16384 pages
        data[1] = 2; data[2] = 16; data[5] = 0x40;
        return c.attached() ? 1u : 0u;
    case 4: return c.probe();
    case 6: case 12: return c.attached() ? 1u : 0u;
    case 9: return c.erase_block(ld(cmd + 0x20)) ? 1u : 0u;
    case 10: {   // the console's clock (the card's file times)
        const save::ClockBcd t = c.clock_fixed() ? c.clock_bcd() : save::rtc_now(vblank_count());
        std::memcpy(data, t.data(), t.size());
        return 1;
    }
    default: return 0;
    }
}

// ---- libdbc's calls into libsif / the kernel, as the rows charge them
void rpc() { charge(kSifCallRpc); }
// the status copy (sceDbcGetDepNumber / sceDbcGetConnection): the work area's cache lines, then interrupts off while
// the words are read, then on again (unconditionally, as libdbc)
std::uint32_t read_status(std::int32_t socket) {
    charge(kSyncDCache);
    b3host::call_guest<int>(kDIntr);
    const std::uint32_t v = socket >= 0 && socket < 16 ? g_status[socket] : 0;
    b3host::call_guest<int>(kEIntr);
    return v;
}

// int sceDbcInit(void) 0x215F70
void dbc_init(const CallArgs&, CallResult& r) {
    if (!g_started) {
        charge({kSifInitRpc, kSifBindRpc, kSifBindRpc, kSifBindRpc});
        rpc();                  // sceDbcGetModVersion: the manager's version 3.0 (the one libdbc wants)
        library_sema(0x7F, 1);  // libdbc's request semaphore
        rpc();                  // sceDbcSetWorkAddr
        g_work = true;
        g_started = true;
    }
    ret(r, 1);
}
// int sceDbcGetModVersion(void) 0x215F18
void dbc_get_mod_version(const CallArgs&, CallResult& r) {
    rpc();
    ret(r, 0x320);
}
// int sceDbcSetWorkAddr(void *addr) 0x2161F8
void dbc_set_work_addr(const CallArgs&, CallResult& r) {
    rpc();
    g_work = true;
    ret(r, 1);
}
// int sceDbcEnd(void) 0x2161B0: the library's semaphore deleted
void dbc_end(const CallArgs&, CallResult&) { stub_unexpected("sceDbcEnd", "the device library shut down (no caller in the game)"); }

// int sceDbcCreateSocket(param, void *buf0, void *buf1) 0x216268: param +4 the type, +8 the port
void dbc_create_socket(const CallArgs& a, CallResult& r) {
    if (!g_started) { ret(r, 0); return; }
    const gaddr param = u32(a, 0), buf0 = u32(a, 1), buf1 = u32(a, 2);
    const std::uint32_t type = ld(param + 4), port = ld(param + 8);
    if (type != kPad && type != kCard) stub_unexpected("sceDbcCreateSocket", "device type " + std::to_string(type));
    if (type == kPad && (buf0 & 63u)) stub_unexpected("sceDbcCreateSocket", "a pad buffer " + hex(buf0) + " not 64-byte aligned");
    std::int32_t id = kNoSocket;
    for (std::int32_t k = 0; k < 16 && id < 0; ++k)
        if (!g_sock[k].used) id = k;
    if (id >= 0) {
        Socket& s = g_sock[id];
        s = Socket{};
        s.used = true;
        s.type = type;
        s.port = port;
        s.buf[0] = buf0;
        s.buf[1] = buf1;
        if (type == kCard) publish(id, card_ready() ? 1u : 0u);
        // pad2 (a pad plugged in while the game runs): the manager knows the device in the port at once -- its status
        // is "connected" from the creation (its first report still comes at the next VBlank); without it the game's
        // UpdatePort (0x214610) deleted the new socket in the same frame and never saw the pad
        if (type == kPad && pad_plugged(s)) publish(id, 1u);
    }
    rpc();
    ret(r, id);
}
// int sceDbcDeleteSocket(int socket) 0x216340: 1, or -22 for no such socket
void dbc_delete_socket(const CallArgs& a, CallResult& r) {
    if (!g_started) { ret(r, 0); return; }
    const std::int32_t id = s32(a, 0);
    std::int32_t res = kBadSocket;
    if (valid(id)) {
        const Socket s = g_sock[id];
        g_sock[id] = Socket{};
        if (s.type == kCard || s.reported) publish(id, 0);
        res = 1;
    }
    rpc();
    ret(r, res);
}
// int sceDbcGetDepNumber(int socket) 0x2163C8: a connected socket's driver: 0 for a pad that has reported
void dbc_get_dep_number(const CallArgs& a, CallResult& r) {
    const std::int32_t id = s32(a, 0);
    if (read_status(id) != 1) { ret(r, kNotLinked); return; }
    const bool linked = valid(id) && pad_plugged(g_sock[id]);   // (pad2: also before its first report)
    rpc();
    ret(r, linked ? 0 : kNotLinked);
}
// int sceDbcGetConnection(int socket) 0x216510: 1 connected, 3 not
void dbc_get_connection(const CallArgs& a, CallResult& r) {
    ret(r, read_status(s32(a, 0)) == 1 ? 1 : 3);
}
// int sceDbcReceiveData(int socket, unsigned int desc, int *size, void *buf) 0x216D20: libpad2's state query (the
// descriptor 0x0101800C): the newest report's state byte
void dbc_receive_data(const CallArgs& a, CallResult& r) {
    const std::int32_t id = s32(a, 0);
    if (!valid(id) || !pad_plugged(g_sock[id]))
        stub_unexpected("sceDbcReceiveData", "socket " + std::to_string(id) + " is not a plugged-in pad");
    // pad2: before a new socket's first report (a pad plugged in while the game runs) the device's own state, 1
    const std::uint8_t state = g_sock[id].reported ? b3host::load<std::uint8_t>(newest(g_sock[id])) : std::uint8_t{1};
    rpc();
    st(u32(a, 2), 1u);
    b3host::store<std::uint8_t>(u32(a, 3), state);
    ret(r, 1);
}
// int sceDbcSendData2(int socket, unsigned int desc, int *size, void *data) 0x216A58 (libvib's actuator parameters;
// pad_actuator.h has what the game sends and how a PS2 answers). libdbc sends the request as a no-wait RPC unless the
// previous one is still in flight (then 0: dropped). Two modes (sif::set_actuator_ack):
//   - pending (the default): no device manager answers, so the first request is taken and stays in flight, and every
//     later one finds the channel busy (0) -- what the game's rumble code has always met natively; no motor is driven.
//   - acknowledging (PC SETTINGS > RUMBLE On, rumble1): the device manager answers each request kActuatorAckCycles
//     after it (the RPC's reply), and the motor values go to the port's pad (sif::pad_motors, the host's rumble).
// The game ignores the result and records the values as sent either way (UpdateVibration 0x217070), so the modes differ
// to the game only in that result and in the virtual time of the RPCs made (the accepted requests' sceSifCallRpc).
// The answer's time [G]: a SIF RPC round trip to the IOP, ~0.1 ms (no measurement; nothing in the game waits for it).
constexpr std::uint64_t kActuatorAckCycles = 29491;   // 0.1 ms of the EE's 294.912 MHz
pad::ActuatorChannel g_actuator;
pad::Motors g_motors[2];
std::uint32_t g_motor_serial[2] = {0, 0};
std::uint64_t g_actuator_requests = 0, g_actuator_busy = 0;
bool rumble_log() {
    static const bool on = [] {
        const char* e = std::getenv("B3NATIVE_RUMBLE_LOG");
        return e && *e && std::string(e) != "0";
    }();
    return on;
}
void dbc_send_data2(const CallArgs& a, CallResult& r) {
    const std::uint32_t size = ld(u32(a, 2));
    if (static_cast<std::int32_t>(size) > 0x1080) { ret(r, 0); return; }
    charge({kWaitSema, kSifCheckStatRpc});
    const std::uint64_t now = virtual_cycles();
    if (g_actuator.busy(now)) {
        charge(kSignalSema);
        if (g_actuator.ack()) ++g_actuator_busy;
        ret(r, 0);
        return;
    }
    rpc();
    charge(kSignalSema);
    g_actuator.send(now);
    if (!g_actuator.ack()) {
        stub_note("sceDbcSendData2", "a pad's actuator parameters taken and left pending (no rumble is driven)");
        ret(r, 1);
        return;
    }
    // acknowledging: the request's motors to its port's pad
    const std::int32_t id = s32(a, 0);
    if (!valid(id) || g_sock[id].type != kPad || g_sock[id].port > 1)
        stub_unexpected("sceDbcSendData2", "socket " + std::to_string(id) + " is not a pad");
    if (size > 0x80u) stub_unexpected("sceDbcSendData2", "an actuator block of " + hex(size) + " bytes");
    std::uint8_t block[0x80];
    b3host::copy_from_guest(block, u32(a, 3), size);
    const std::uint32_t port = g_sock[id].port;
    pad::Motors m = g_motors[port];
    if (!pad::decode_actuator_request(block, size, m))
        stub_unexpected("sceDbcSendData2", "an actuator block libvib does not make for a DualShock 2 (descriptor " + hex(u32(a, 1)) + ")");
    g_motors[port] = m;
    ++g_motor_serial[port];
    ++g_actuator_requests;
    if (g_actuator_requests == 1)
        std::printf("platform: rumble: the pads' actuator requests acknowledged (the first at VBlank %llu)\n",
                    static_cast<unsigned long long>(vblank_count()));
    if (rumble_log())
        std::printf("platform: rumble: VBlank %llu cycles %llu port %u small %u large %u\n", static_cast<unsigned long long>(vblank_count()),
                    static_cast<unsigned long long>(now), port + 1, unsigned(m.small), unsigned(m.large));
    ret(r, 1);
}
// int sceDbcSendData3(int socket, unsigned int cmd, int *size, void *data, void (*done)(void *)) 0x216BB8: one memory
// card command (libmc2's core); `done` runs (with 0) when the card has answered
void dbc_send_data3(const CallArgs& a, CallResult& r) {
    const std::int32_t id = s32(a, 0);
    const std::uint32_t op = u32(a, 1);
    const gaddr size = u32(a, 2), cmd = u32(a, 3), done = u32(a, 4);
    if (static_cast<std::int32_t>(ld(size)) > 0x2080) { ret(r, 0); return; }
    if (ld(size) != 0x2080u || !done || (done & 3u))
        stub_unexpected("sceDbcSendData3", "a command block of " + hex(ld(size)) + " bytes / completion " + hex(done));
    if (!valid(id) || g_sock[id].type != kCard) stub_unexpected("sceDbcSendData3", "socket " + std::to_string(id) + " is not a card");
    charge({kWaitSema, kSifCheckStatRpc});
    Socket& s = g_sock[id];
    save::SaveCard* const c = card();
    if (!c && op != 4u)
        stub_unexpected("sceDbcSendData3", "card command " + std::to_string(op) + " with no card in the slot (set B3NATIVE_SAVES_ROOT)");
    const gaddr reply = c && op == 1u ? s.buf[0] : s.buf[s.next_buf];
    if (!reply || (reply & 15u)) stub_unexpected("sceDbcSendData3", "a reply buffer " + hex(reply));
    std::uint8_t data[116];
    const std::uint32_t result = c ? card_command(op, cmd, s.buf[0] + 0x80u, data) : 0u;
    b3host::fill_guest(reply, 0, 0x80);
    st(reply, s.sequence);
    st(reply + 4, static_cast<std::uint32_t>(id));
    st(reply + 8, result);
    if (result) b3host::copy_to_guest(reply + 12, data, sizeof data);
    else st(reply + 12, 0xFFFFFFFFu);
    s.sequence = s.sequence == 0x7FFFFFFFu ? 1u : s.sequence + 1u;
    s.next_buf ^= 1u;
    if (!c) {
        sif::queue_guest_call(done, 0, 0);   // no card: the probe answers at once
    } else {
        const std::uint32_t pages = op == 1u || op == 2u ? std::min<std::uint32_t>(ld(cmd + 4), 16u) : 0u;
        const std::uint64_t busy = std::uint64_t{pages} * 295000u + (op == 9u ? 590000u : 0u) + 90000u;
        sif::queue_guest_call_at(virtual_cycles() + busy, done, 0, 0);
    }
    rpc();
    charge(kSignalSema);
    ret(r, 1);
}
// the device manager functions no part of the game reaches (the libraries' other requests): fatal
void dbc_unreached(const char* name) { stub_unexpected(name, "a device manager request the game does not make"); }
void dbc_init_socket(const CallArgs&, CallResult&) { dbc_unreached("sceDbcInitSocket"); }
void dbc_reset_socket(const CallArgs&, CallResult&) { dbc_unreached("sceDbcResetSocket"); }
void dbc_get_device_status(const CallArgs&, CallResult&) { dbc_unreached("sceDbcGetDeviceStatus"); }
void dbc_search_port_specific(const CallArgs&, CallResult&) { dbc_unreached("sceDbcSearchPortSpecific"); }
void dbc_sr_data(const CallArgs&, CallResult&) { dbc_unreached("sceDbcSRData"); }
void dbc_send_data(const CallArgs&, CallResult&) { dbc_unreached("sceDbcSendData"); }

}  // namespace

// the rows (platform.h B3NATIVE_FIRST)
void dbc_row_init(const CallArgs& a, CallResult& r) { dbc_init(a, r); }
void dbc_row_get_mod_version(const CallArgs& a, CallResult& r) { dbc_get_mod_version(a, r); }
void dbc_row_end(const CallArgs& a, CallResult& r) { dbc_end(a, r); }
void dbc_row_set_work_addr(const CallArgs& a, CallResult& r) { dbc_set_work_addr(a, r); }
void dbc_row_create_socket(const CallArgs& a, CallResult& r) { dbc_create_socket(a, r); }
void dbc_row_delete_socket(const CallArgs& a, CallResult& r) { dbc_delete_socket(a, r); }
void dbc_row_get_dep_number(const CallArgs& a, CallResult& r) { dbc_get_dep_number(a, r); }
void dbc_row_get_connection(const CallArgs& a, CallResult& r) { dbc_get_connection(a, r); }
void dbc_row_init_socket(const CallArgs& a, CallResult& r) { dbc_init_socket(a, r); }
void dbc_row_reset_socket(const CallArgs& a, CallResult& r) { dbc_reset_socket(a, r); }
void dbc_row_get_device_status(const CallArgs& a, CallResult& r) { dbc_get_device_status(a, r); }
void dbc_row_search_port_specific(const CallArgs& a, CallResult& r) { dbc_search_port_specific(a, r); }
void dbc_row_sr_data(const CallArgs& a, CallResult& r) { dbc_sr_data(a, r); }
void dbc_row_send_data(const CallArgs& a, CallResult& r) { dbc_send_data(a, r); }
void dbc_row_send_data2(const CallArgs& a, CallResult& r) { dbc_send_data2(a, r); }
void dbc_row_send_data3(const CallArgs& a, CallResult& r) { dbc_send_data3(a, r); }
void dbc_row_receive_data(const CallArgs& a, CallResult& r) { dbc_receive_data(a, r); }

namespace sif {
void set_pad_sample(int port, const unsigned char sample[32]) {
    if (port < 0 || port > 1) b3host::fatal("platform: set_pad_sample port " + std::to_string(port));
    std::memcpy(g_ports[port].report, sample, 32);
}
void set_pad_connected(int port, bool connected) {
    if (port < 0 || port > 1) b3host::fatal("platform: set_pad_connected port " + std::to_string(port));
    g_ports[port].plugged = connected;
}
const unsigned char* neutral_pad_sample() { return kNeutral; }
void set_actuator_ack(bool on) { g_actuator.set_ack(on, virtual_cycles(), kActuatorAckCycles); }
bool actuator_ack() { return g_actuator.ack(); }
void pad_motors(int port, unsigned char& small, unsigned char& large, std::uint32_t* serial) {
    if (port < 0 || port > 1) b3host::fatal("platform: pad_motors port " + std::to_string(port));
    small = g_motors[port].small;
    large = g_motors[port].large;
    if (serial) *serial = g_motor_serial[port];
}
std::uint64_t actuator_requests(std::uint64_t* busy) {
    if (busy) *busy = g_actuator_busy;
    return g_actuator_requests;
}
save::ProfileCard* save_profile_card() { return profile_card(); }
void save_card_flush() {
    std::string err;
    if (save::ProfileCard* p = profile_card(); p && !p->flush(&err)) std::fprintf(stderr, "[save] at exit: %s\n", err.c_str());
}

// The device manager's VBlank: the card inserted when its time comes, the profile's settled saves written, then every
// pad socket's status and report.
void pad_vblank() {
    if (!g_card_in && card() && vblank_count() >= card_in_vblank()) {
        g_card_in = true;
        for (std::int32_t id = 0; id < 16; ++id)
            if (g_sock[id].used && g_sock[id].type == kCard) publish(id, card_ready() ? 1u : 0u);
        stub_note("save card", "inserted at VBlank " + std::to_string(vblank_count()) + " (B3NATIVE_CARD_PRESENT_VBLANK)");
    }
    if (save::ProfileCard* p = profile_card()) p->tick(vblank_count());
    if (!g_work) return;
    for (std::int32_t id = 0; id < 16; ++id) {
        Socket& s = g_sock[id];
        if (!s.used || s.type != kPad) continue;
        const bool plugged = pad_plugged(s);
        if (!plugged && !s.reported) continue;
        publish(id, plugged ? 1u : 0u);
        const gaddr into = newest(s) == s.buf[0] ? s.buf[1] : s.buf[0];
        std::uint8_t rep[0x80] = {};
        if (plugged) {
            rep[0] = 1;
            rep[2] = 32;
            rep[3] = sizeof kProfile;
            std::memcpy(rep + 0x1C, g_ports[s.port].report, 32);
            std::memcpy(rep + 0x1C + 32, kProfile, sizeof kProfile);
        }
        rep[4] = 1;
        s.counter = s.counter == 0x7FFFFFFF ? 1 : s.counter + 1;
        for (unsigned k = 0; k < 4; ++k) rep[0x7C + k] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(s.counter) >> (8 * k));
        b3host::copy_to_guest(into, rep, sizeof rep);
        s.reported = plugged;
    }
}
}  // namespace sif

}  // namespace b3native::platform
