// The EE <-> IOP command channel and the IOP's memory (src/native/platform_sif.h; S4 of the SIF lane, 2026-11-11).
//
// On a PS2 the EE's libraries reach the IOP over SIF in two ways: RPC (libsif's sceSifBindRpc / sceSifCallRpc, a
// request to a server on the IOP) and raw SIF commands (sceSifSendCmd one way; the IOP's commands land in the handlers
// the EE registered with sceSifAddCmdHandler, in interrupt context). The platform answers every RPC user at its own
// entry points -- libcdvd, libkernl's file I/O, libdbc, lgdev (platform_devices.cpp, platform_dbc.cpp), the game's file
// system (platform_gtfs.cpp) and its sound distortion (platform_distortion.cpp) -- so no RPC is made and none is served
// here: the three RPC entry points are fatal. What stays is the command channel, RW Audio's boundary to its IOP side
// (platform_rwa.cpp), which also delivers the platform's completions to the game's handlers; the IOP heap whose
// addresses RW Audio's IOP side uses; and the module loader's answers.
//
// Retail functions answered here [C decomp assembly/asm/sce/libkernl.s; PS2 SDK sifcmd.h / sifrpc.h / sifdev.h for
// their contracts]: sceSifAddCmdHandler 0x10E408, sceSifRemoveCmdHandler 0x10E480, sceSifSetCmdBuffer 0x10E3D8,
// sceSifInitCmd 0x10E120, sceSifExitCmd 0x10E3A0, sceSifSendCmd 0x10E608 / isceSifSendCmd 0x10E648, sceSifDmaStat
// 0x10C1C0 / isceSifDmaStat 0x10C1D0, sceSifInitIopHeap 0x113028, sceSifAllocSysMemory 0x113120, sceSifFreeSysMemory
// 0x1131A0, sceSifLoadModule 0x113F78; fatal: sceSifBindRpc 0x10F018, sceSifCallRpc 0x10F1E8, sceSifCheckStatRpc
// 0x10F3E0, sceSifSearchModuleByName 0x113BE0, sceSifStopModule 0x113948, sceSifUnloadModule 0x113B50.
#include "platform.h"
#include "platform_sif.h"

#include "b3host/call.h"
#include "b3host/native_backend.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace b3native::platform {
namespace {

using b3host::CallArgs;
using b3host::CallResult;
using b3host::gaddr;

inline std::uint32_t u32(const CallArgs& a, unsigned i) { return a.gpr[i].w[0]; }
inline std::int32_t s32(const CallArgs& a, unsigned i) { return static_cast<std::int32_t>(a.gpr[i].w[0]); }
inline void ret(CallResult& r, std::int32_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(v)));
}
inline std::uint32_t ld(gaddr p) { return b3host::load<std::uint32_t>(p); }
inline void st(gaddr p, std::uint32_t v) { b3host::store<std::uint32_t>(p, v); }
std::string cstr(gaddr p, std::size_t max) {
    std::string s;
    while (p && s.size() < max) {
        const char c = static_cast<char>(b3host::load<std::uint8_t>(p++));
        if (!c) break;
        s += c;
    }
    return s;
}
std::string hex(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}
b3host::NativeBackend* nb() {
    auto* n = dynamic_cast<b3host::NativeBackend*>(&b3host::backend());
    if (!n) b3host::fatal("platform_sif: needs the native backend (its interrupt state)");
    return n;
}
// the game's $gp [C decomp config/b3_retail.yaml: _gp]: a command-table entry keeps the handler's $gp; every handler
// is game code
constexpr std::uint32_t kGameGp = 0x004E8670u;

}  // namespace

// B3NATIVE_SIF_LOG=<file>: the EE <-> IOP traffic in order, each line `<kind> <VBlank> <virtual EE cycles> ...`: `R` a
// request the retail code sends to an IOP service (server id, function, send / receive sizes; written by the rows that
// answer the game's file system and sound distortion where the retail code made the RPC), `C` a SIF command sent (cid,
// packet size, data size), `Q` a completion delivered to the EE (`cmd` and the cid, or `direct` and the function), `O`
// / `D` the file system's opens and reads (platform_gtfs.cpp).
std::FILE* sif_log() {
    static std::FILE* f = [] {
        const char* e = std::getenv("B3NATIVE_SIF_LOG");
        return e && *e ? std::fopen(e, "w") : nullptr;
    }();
    return f;
}

namespace sif {

// ======================================================================================================== DMA ids
namespace { std::int32_t g_next_dma = 1; }
std::int32_t next_dma_id() { return g_next_dma++; }

// ======================================================================================================== IOP heap
namespace {
constexpr std::uint32_t kHeapLo = 0x100000u, kHeapHi = 0x200000u, kUnit = 0x100u;
std::map<std::uint32_t, std::uint32_t> g_blocks;   // address -> bytes (whole units)
}  // namespace
std::uint32_t iop_alloc(std::uint32_t bytes) {
    if (bytes == 0 || bytes > kHeapHi - kHeapLo) return 0;
    const std::uint32_t need = (bytes + kUnit - 1u) / kUnit * kUnit;
    std::uint32_t at = kHeapLo;
    for (const auto& [addr, len] : g_blocks) {   // address order: the first gap that holds it
        if (addr >= at && addr - at >= need) break;
        at = addr + len;
    }
    if (at > kHeapHi || kHeapHi - at < need) return 0;
    g_blocks.emplace(at, need);
    return at;
}
int iop_free(std::uint32_t address) { return g_blocks.erase(address) ? 0 : -1; }

// ======================================================================================================== IOP -> EE
namespace {
struct Delivery {
    bool call = false;                 // a guest call (else a command for `cid`'s registered handler)
    std::uint32_t cid = 0;
    std::vector<std::uint8_t> packet;  // a command: copied to `where` before its handler runs
    gaddr where = 0;                   // a command: where its packet lands; a call: the function
    std::uint32_t regs[5] = {};        // a call: a0-a3, t0
};
struct Timed {
    std::uint64_t ready;
    Delivery d;
};
std::deque<Delivery> g_due;            // due now, in order
std::deque<Timed> g_timed;             // due later, ready times nondecreasing
bool g_channel_closed = false;         // sceSifExitCmd: no more IOP -> EE commands

// libsif's handler tables [C sceSifAddCmdHandler 0x10E408: 12-byte entries {func, data, gp}]: a user cid (bit 31 clear)
// indexes the table the game gave sceSifSetCmdBuffer (guest memory: RW Audio's, which the file system and the sound
// distortion register into too); a system cid libsif's own table, which the platform keeps.
gaddr g_user_table = 0;
std::uint32_t g_user_entries = 0;
std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> g_system;   // cid & 0x7FFFFFFF -> {func, data}
bool handler_for(std::uint32_t cid, std::uint32_t& func, std::uint32_t& data) {
    func = data = 0;
    if (cid & 0x80000000u) {
        const auto it = g_system.find(cid & 0x7FFFFFFFu);
        if (it != g_system.end()) { func = it->second.first; data = it->second.second; }
    } else if (g_user_table) {
        func = ld(g_user_table + cid * 12u);
        data = ld(g_user_table + cid * 12u + 4u);
    }
    return func != 0;
}
void timed_to_due() {
    if (g_timed.empty()) return;
    const std::uint64_t now = virtual_cycles();
    while (!g_timed.empty() && g_timed.front().ready <= now) {
        Timed t = std::move(g_timed.front());
        g_timed.pop_front();
        g_due.push_back(std::move(t.d));
    }
}
void add_timed(Timed t) {
    if (!g_timed.empty() && t.ready < g_timed.back().ready) t.ready = g_timed.back().ready;   // keep them in order
    g_timed.push_back(std::move(t));
    rearm_virtual_time();
}
}  // namespace

void queue_cmd_reply(std::uint32_t cid, const void* packet, std::uint32_t size, gaddr at) {
    Delivery d;
    d.cid = cid;
    d.packet.assign(static_cast<const std::uint8_t*>(packet), static_cast<const std::uint8_t*>(packet) + size);
    d.where = at ? at : kCmdReceiveBuffer;
    g_due.push_back(std::move(d));
}
void queue_guest_call(gaddr handler, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t t0) {
    Delivery d;
    d.call = true;
    d.where = handler;
    const std::uint32_t r[5] = {a0, a1, a2, a3, t0};
    std::memcpy(d.regs, r, sizeof r);
    g_due.push_back(std::move(d));
}
void queue_guest_call_at(std::uint64_t ready, gaddr handler, std::uint32_t a0, std::uint32_t a1) {
    Timed t{ready, {}};
    t.d.call = true;
    t.d.where = handler;
    t.d.regs[0] = a0;
    t.d.regs[1] = a1;
    add_timed(std::move(t));
}
std::uint64_t sif_next_ready() { return g_timed.empty() ? ~0ull : g_timed.front().ready; }
bool sif_pending() {
    timed_to_due();
    return !g_due.empty();
}
namespace { bool g_delivering = false; }   // a handler's own kernel calls (iSignalSema) come back here: not again
void sif_service() {
    timed_to_due();
    if (g_due.empty() || !nb()->interrupts_enabled()) return;
    if (g_delivering) return;
    g_delivering = true;
    struct Done { ~Done() { g_delivering = false; } } done;
    while (!g_due.empty()) {
        const Delivery d = std::move(g_due.front());
        g_due.pop_front();
        if (g_channel_closed) {
            stub_note("SIF command delivery", "a completion after sceSifExitCmd dropped (the EE's SIF0 handler is gone)");
            continue;
        }
        // interrupt context, as the kernel's DMAC (SIF0) interrupt runs libsif's command dispatch
        nb()->set_interrupts_enabled(false);
        isr_enter();
        if (std::FILE* f = sif_log())
            std::fprintf(f, "Q %llu %llu %s %x\n", (unsigned long long)vblank_count(), (unsigned long long)virtual_cycles(),
                         d.call ? "direct" : "cmd", d.call ? d.where : d.cid);
        if (d.call) {
            b3host::call_guest<int>(d.where, d.regs[0], d.regs[1], d.regs[2], d.regs[3], d.regs[4]);
        } else if (std::uint32_t func, data; handler_for(d.cid, func, data)) {
            b3host::copy_to_guest(d.where, d.packet.data(), static_cast<std::uint32_t>(d.packet.size()));
            b3host::call_guest<int>(func, d.where, data);
        } else {
            stub_note("SIF command delivery", "a command for a cid with no handler dropped (libsif skips an empty entry)");
        }
        isr_exit();
        nb()->set_interrupts_enabled(true);
    }
}

bool can_deliver_now() {
    return !g_delivering && !g_channel_closed && nb()->interrupts_enabled() && isr_depth() == 0;
}
bool deliver_cmd_now(std::uint32_t cid, const void* packet, std::uint32_t size) {
    std::uint32_t func, data;
    if (!can_deliver_now() || !handler_for(cid, func, data)) return false;
    g_delivering = true;
    struct Done { ~Done() { g_delivering = false; } } done;
    nb()->set_interrupts_enabled(false);
    isr_enter();
    if (std::FILE* f = sif_log())
        std::fprintf(f, "Q %llu %llu now %x\n", (unsigned long long)vblank_count(), (unsigned long long)virtual_cycles(), cid);
    b3host::copy_to_guest(kCmdReceiveBuffer, packet, size);
    b3host::call_guest<int>(func, kCmdReceiveBuffer, data);
    isr_exit();
    nb()->set_interrupts_enabled(true);
    return true;
}

// ======================================================================================================== EE -> IOP
namespace { std::map<std::uint32_t, CmdSink> g_sinks; }
void set_cmd_sink(std::uint32_t cid, CmdSink sink) {
    if (sink) g_sinks[cid] = std::move(sink);
    else g_sinks.erase(cid);
}

}  // namespace sif

using namespace sif;

// ======================================================================================================== commands
// void sceSifAddCmdHandler(unsigned int cid, sceSifCmdHandler func, void *data): a user cid's entry {func, data, $gp}
// written into the game's table (sceSifSetCmdBuffer), a system cid's kept by the platform. Callers: RwaSifCmdInit (1,
// RW Audio's replies), CGtFileSystemPS2DVD::Init (4 read complete, 5 error: platform_gtfs.cpp calls this row as the
// game's Init does), CB3SoundDistortion::Construct (6, platform_distortion.cpp likewise).
void sif_add_cmd_handler(const CallArgs& a, CallResult& r) {
    const std::uint32_t cid = u32(a, 0), func = u32(a, 1), data = u32(a, 2);
    if (cid & 0x80000000u) {
        g_system[cid & 0x7FFFFFFFu] = {func, data};
    } else {
        if (!g_user_table) stub_unexpected("sceSifAddCmdHandler", "cid " + std::to_string(cid) + " before sceSifSetCmdBuffer");
        if (cid >= g_user_entries)
            stub_unexpected("sceSifAddCmdHandler", "cid " + std::to_string(cid) + " past the table's " + std::to_string(g_user_entries) + " entries");
        st(g_user_table + cid * 12u, func);
        st(g_user_table + cid * 12u + 4u, data);
        st(g_user_table + cid * 12u + 8u, kGameGp);
    }
    r = CallResult{};
}
// void sceSifRemoveCmdHandler(unsigned int cid): the entry's function cleared
void sif_remove_cmd_handler(const CallArgs& a, CallResult& r) {
    const std::uint32_t cid = u32(a, 0);
    if (cid & 0x80000000u) g_system.erase(cid & 0x7FFFFFFFu);
    else if (g_user_table) st(g_user_table + cid * 12u, 0);
    r = CallResult{};
}
// sceSifCmdData *sceSifSetCmdBuffer(sceSifCmdData *table, int entries): the user table; the previous one returned
void sif_set_cmd_buffer(const CallArgs& a, CallResult& r) {
    const gaddr previous = g_user_table;
    g_user_table = u32(a, 0);
    g_user_entries = u32(a, 1);
    ret(r, static_cast<std::int32_t>(previous));
}
// void sceSifInitCmd(void): the channel is always there
void sif_init_cmd(const CallArgs&, CallResult& r) { r = CallResult{}; }
// void sceSifExitCmd(void): the EE stops taking IOP -> EE commands (the exit to another program: func_001D3D70)
void sif_exit_cmd(const CallArgs&, CallResult& r) {
    g_channel_closed = true;
    r = CallResult{};
}
// unsigned int sceSifSendCmd(cid, packet, psize, src, dest, size) / isceSifSendCmd: psize outside 0x10..0x70 -> 0;
// else the header written into the caller's packet (+0 the packet size in the low byte, the data size above it when
// there is data, +4 the data's IOP destination or 0, +8 the cid), the command handed to the cid's IOP side, the DMA's
// id returned. The one sender left is RW Audio (cid 0).
void sif_send_cmd(const CallArgs& a, CallResult& r) {
    const std::uint32_t cid = u32(a, 0), psize = u32(a, 2), size = u32(a, 5);
    const gaddr pkt = u32(a, 1), src = u32(a, 3), dest = u32(a, 4);
    if (psize < 0x10u || psize > 0x70u) { ret(r, 0); return; }
    const bool data = static_cast<std::int32_t>(size) > 0;
    st(pkt, (data ? size << 8 : 0u) | psize);
    st(pkt + 4, data ? dest : 0u);
    st(pkt + 8, cid);
    const std::int32_t id = next_dma_id();
    if (std::FILE* f = sif_log())
        std::fprintf(f, "C %llu %llu %x %x %x\n", (unsigned long long)vblank_count(), (unsigned long long)virtual_cycles(), cid, psize, size);
    const auto it = g_sinks.find(cid);
    if (it == g_sinks.end())
        stub_unexpected("sceSifSendCmd", "cid " + hex(cid) + ": no IOP side for it (RW Audio's cid 0 is the platform's one channel)");
    it->second(SentCmd{cid, pkt, psize, src, dest, data ? size : 0u, id});
    ret(r, id);
}
// int sceSifDmaStat(unsigned int id): < 0 once the transfer is done; a command's data is taken at the send
void sif_dma_stat(const CallArgs&, CallResult& r) { ret(r, -1); }

// ======================================================================================================== RPC
// No IOP RPC server exists: an RPC reaching libsif is a library or game path no row answers (the network libraries
// behind ONLINE, which a PC without a PS2 network adapter never reaches), and fatal, naming the service.
namespace {
const char* service_name(std::uint32_t id) {
    switch (id) {
    case 0x75488909u: case 0x75499128u: return "ezNetCnf (network configuration)";
    case 0x80001101u: return "netcnfif (network configuration)";
    case 0x00534E03u: return "DirtySock (online)";
    case 0x61766973u: return "Logitech lgkbm (USB keyboard)";
    case 0x50494C42u: return "Logitech lgcodec (USB headset)";
    case 0x00475453u: return "GTFSCDVD (the game's file system: answered at CGtFileSystemPS2DVD / CGtFilePS2DVD)";
    case 0x00150276u: return "B3ROUTE (the sound distortion: answered at CB3SoundDistortion)";
    default: return "a service the platform answers at its library's entry points, or an unknown one";
    }
}
}  // namespace
void sif_bind_rpc(const CallArgs& a, CallResult&) {
    stub_unexpected("sceSifBindRpc", "server " + hex(u32(a, 1)) + ", " + service_name(u32(a, 1)) + ": no IOP RPC server");
}
void sif_call_rpc(const CallArgs& a, CallResult&) {
    stub_unexpected("sceSifCallRpc", "client " + hex(u32(a, 0)) + " function " + hex(u32(a, 1)) + ": no IOP RPC server");
}
void sif_check_stat_rpc(const CallArgs& a, CallResult&) {
    stub_unexpected("sceSifCheckStatRpc", "client " + hex(u32(a, 0)) + ": no IOP RPC server");
}

// ======================================================================================================== heap
// int sceSifInitIopHeap(void): 0. void *sceSifAllocSysMemory(int type, int size, void *addr): an IOP address, 0 before
// sceSifInitIopHeap; type 0 (first fit) at no fixed address is what the game and RW Audio ask for (else 0, noted).
// int sceSifFreeSysMemory(void *addr): 0, -1 for an address no block starts at. Each is one request to the IOP on a
// PS2 (one SIF DMA). Callers: CB3Game's sound start-up func_0028B580 / func_0028AEC0, GtSoundBankManager, GtSoundStream,
// RW Audio.
namespace { bool g_heap_ready = false; }
void sif_init_iop_heap(const CallArgs&, CallResult& r) {
    g_heap_ready = true;
    next_dma_id();
    ret(r, 0);
}
void sif_alloc_sys_memory(const CallArgs& a, CallResult& r) {
    if (!g_heap_ready) { ret(r, 0); return; }
    next_dma_id();
    const std::int32_t type = s32(a, 0);
    const std::uint32_t bytes = u32(a, 1), at = u32(a, 2);
    if (type != 0 || at != 0) {
        stub_note("sceSifAllocSysMemory", "type " + std::to_string(type) + " / a fixed address: not served (0)");
        ret(r, 0);
        return;
    }
    ret(r, static_cast<std::int32_t>(iop_alloc(bytes)));
}
void sif_free_sys_memory(const CallArgs& a, CallResult& r) {
    if (!g_heap_ready) { ret(r, 0); return; }
    next_dma_id();
    ret(r, iop_free(u32(a, 0)));
}

// ======================================================================================================== modules
// int sceSifLoadModule(const char *path, int arglen, const char *args): CB3Game::PrepareHardware 0x1D41E0 loads its IOP
// modules from the disc's IOP folder. Their services are answered on the EE side, so nothing is loaded: each module
// gets the id it would have as the n-th one loaded (1, 2, ...; the game only tests the result's sign). One exception
// (fe1, 2026-11-10): the PC has no PS2 network adapter, so the adapter's Ethernet driver SMAP.IRX fails to load (-1) --
// the one failure PrepareHardware tolerates, clearing D_01E3D597 [C decomp PrepareHardware__7CB3GameFv]: the main menu
// then greys ONLINE and shows the no-adapter message box (string 105) instead of starting the network libraries; its
// text is the port's "Online play is coming soon." (tools/native/online_message.h; B3NATIVE_ONLINE_MESSAGE=0: the
// game's own). B3NATIVE_NET_ADAPTER=1: SMAP loads too (the online flow stops at its first network RPC, fatal).
void sif_load_module(const CallArgs& a, CallResult& r) {
    static std::int32_t loaded = 0;
    const std::string path = cstr(u32(a, 0), 0xFC);
    if (path.empty()) stub_unexpected("sceSifLoadModule", "an empty path");
    next_dma_id();
    ++loaded;   // the id is taken even by the module that fails, so the later ones keep theirs
    static const bool adapter = [] { const char* e = std::getenv("B3NATIVE_NET_ADAPTER"); return e && *e == '1'; }();
    const std::string smap = "\\SMAP.IRX;1";   // "cdrom0:\IOP\NETWORK\SMAP.IRX;1"
    if (!adapter && path.size() >= smap.size() && path.compare(path.size() - smap.size(), smap.size(), smap) == 0) {
        stub_note("sceSifLoadModule", "SMAP.IRX: no network adapter -> -1 (the game tests < 0 only)");
        ret(r, -1);
        return;
    }
    ret(r, loaded);
}
// sceSifSearchModuleByName / sceSifStopModule / sceSifUnloadModule: only the network configuration (ezNetCnf: 0x345260,
// 0x345670, 0x345740) calls them, behind ONLINE. Fatal.
void sif_search_module_by_name(const CallArgs& a, CallResult&) {
    stub_unexpected("sceSifSearchModuleByName", "\"" + cstr(u32(a, 0), 64) + "\" (the network configuration)");
}
void sif_stop_module(const CallArgs& a, CallResult&) {
    stub_unexpected("sceSifStopModule", "module " + std::to_string(s32(a, 0)) + " (the network configuration)");
}
void sif_unload_module(const CallArgs& a, CallResult&) {
    stub_unexpected("sceSifUnloadModule", "module " + std::to_string(s32(a, 0)) + " (the network configuration)");
}

}  // namespace b3native::platform
