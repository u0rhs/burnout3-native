// The console's small devices, answered at the EE library entry points the game calls (sif-lane, 2026-11-10;
// platform.h B3NATIVE_FIRST): the CD/DVD drive's start-up, media mode, ready state and real-time clock (libcdvd), the
// ROM version file and the network adapter's power switch (libkernl's file I/O), and the Logitech USB device library
// (lgdev: no wheel attached).
//
// On a PS2 each of these libraries is the EE half of an IOP module (CDVDFSV, FILEIO, lgdev.irx) and talks to it over
// SIF RPC. Here the host answers the library call itself: no request is built, no RPC is made, and the libraries' own
// bookkeeping (their RPC clients, semaphores and request buffers in .bss) does not exist. What the caller sees -- the
// result, the bytes written through its pointers -- is what the PS2 libraries give, and every row charges the virtual
// EE time of the retail code it replaces (platform_charge.h): the nested calls each retail path makes into the kernel
// and libsif, in their order, so events fall where they fell before.
//
// Behaviour [C decomp src/libs/sce/libcdvd.cpp, libkernl_fio.cpp, src/libs/lg/lgdev.cpp, all written from the retail
// asm; PS2SDK libcdvd.h / sifdev.h for the meaning of the values]; the device answers are the console's:
//   - sceCdInit(mode): the drive answers its init with no disc-type error, so the library returns 2 (its "neither
//     major version below 2" case: libcdvd.cpp sceCdInit with an all-zero reply). Mode 5 (SCECdEXIT) shuts the
//     library down (the semaphores go: the next call sets them up again).
//   - sceCdMmode(media): 1. sceCdDiskReady(mode): 2 (SCECdComplete: the disc is always there).
//   - sceCdReadClock(clock): 1, the 8 RTC bytes {0, sec, min, hour, 0, day, month, year} in BCD, the console's clock
//     (save_card.h rtc_now: fixed for tests, the player's time in a window run).
//   - sceCdPOffCallback(func, arg): the power-off handler kept here (power_off_handler); the previous one returned.
//   - sceOpen / sceRead / sceClose on "rom0:ROMVER" (libscf's GetRomName: the only file the game opens through
//     FILEIO): descriptor 0, the 16 bytes "0200AC20040614\n\0" (ROM 2.00, region A, console "C", 2004-06-14).
//   - sceDevctl("dev9x:", DDIOC_OFF): 0 (the network adapter's power-off before the console powers off or hands over to
//     another program; there is no adapter).
//   - lgDevInit / lgDevEnumerate: the USB library's start-up (the module version 1.11.27 it wants) and "no device"
//     (0x80000002) for every index.
// Anything else these rows are not written for is fatal (stub_unexpected), as before.
#include "platform.h"
#include "platform_charge.h"
#include "save_card.h"

#include "b3host/call.h"
#include "b3host/host.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace b3native::platform {
namespace {

using b3host::CallArgs;
using b3host::CallResult;
using b3host::gaddr;

// The retail functions whose virtual time the rows charge [C decomp config/symbol_addrs.txt].
constexpr gaddr kCreateSema = 0x0010BE20u, kDeleteSema = 0x0010BE30u, kSignalSema = 0x0010BE40u, kWaitSema = 0x0010BE60u,
                kPollSema = 0x0010BE70u, kGetThreadId = 0x0010BD10u, kReferThreadStatus = 0x0010BD20u,
                kInvalidDCache = 0x0010C858u, kSifInitRpc = 0x0010E880u, kSifBindRpc = 0x0010F018u,
                kSifCallRpc = 0x0010F1E8u, kSifCheckStatRpc = 0x0010F3E0u, kSifAddCmdHandler = 0x0010E408u,
                kSifRemoveCmdHandler = 0x0010E480u, kDcacheWriteback = 0x0010E7D0u, kPrintf = 0x001252C8u;
// The kernel's interrupt switches (platform_iop.cpp dintr / eintr): called for real, as the libraries do around their
// command-handler updates
constexpr gaddr kDIntr = 0x00114E60u, kEIntr = 0x00114EB8u;

inline std::uint32_t u32(const CallArgs& a, unsigned i) { return a.gpr[i].w[0]; }
inline std::int32_t s32(const CallArgs& a, unsigned i) { return static_cast<std::int32_t>(a.gpr[i].w[0]); }
inline void ret(CallResult& r, std::int32_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(v)));
}
std::string guest_string(gaddr p, std::size_t max) {
    std::string s;
    for (; p && s.size() < max; ++p) {
        const char c = static_cast<char>(b3host::load<std::uint8_t>(p));
        if (!c) break;
        s.push_back(c);
    }
    return s;
}
std::string hex(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}
// DIntr ... EIntr around a command-handler update: the interrupt state as the library leaves it, the time as it spends it
template <class F>
void with_interrupts_off(F&& f) {
    const int was = b3host::call_guest<int>(kDIntr);
    f();
    if (was) b3host::call_guest<int>(kEIntr);
}

// ======================================================================================================== libcdvd
// The library's state as far as its timing depends on it: whether its semaphores exist (made at its first command,
// gone after sceCdInit(5)) and which of its RPC clients are bound (each binds at its first use after sceCdInit).
struct Cdvd {
    bool sems = false;
    std::int32_t sema[4] = {-1, -1, -1, -1};   // its N-command, S-command, ready and callback semaphores
    bool scmd_bound = false;      // the S-command client (clock, media mode)
    bool ready_bound = false;     // the disc-ready client
    bool poff_installed = false;  // the power-off command handler (sceCdInit, or the first sceCdPOffCallback)
    gaddr poff_func = 0, poff_arg = 0;
} g_cd;

void cd_sem_init() {   // cmd_sem_init 0x11E700: four semaphores at the first command
    if (g_cd.sems) return;
    for (int k = 0; k < 4; ++k) g_cd.sema[k] = library_sema(1, k < 3 ? 1 : 0);
    g_cd.sems = true;
}
void cd_poff_install() {   // PowerOffCB 0x11E930
    with_interrupts_off([] { charge(kSifAddCmdHandler); });
    g_cd.poff_installed = true;
}
// _sceCd_scmd_prechk 0x11EFF8 on an idle library: the S-command semaphore taken (PollSema), the calling thread looked up,
// the client found idle (sceCdSyncS(1)), libsif started, the client bound the first time
void cd_scmd_begin() {
    cd_sem_init();
    charge({kPollSema, kReferThreadStatus, kSifCheckStatRpc, kSifInitRpc});
    if (!g_cd.scmd_bound) {
        charge(kSifBindRpc);
        g_cd.scmd_bound = true;
    }
}

// int sceCdInit(int mode) 0x11F218
void cd_init(const CallArgs& a, CallResult& r) {
    const std::int32_t mode = s32(a, 0);
    if (mode != 0 && mode != 5) stub_unexpected("sceCdInit", "init mode " + std::to_string(mode));
    charge({kSifCheckStatRpc, kSifInitRpc, kGetThreadId});
    g_cd.scmd_bound = g_cd.ready_bound = g_cd.poff_installed = false;   // every client binds again
    charge({kSifBindRpc, kDcacheWriteback, kSifCallRpc});
    if (mode != 5) {
        cd_sem_init();
        cd_poff_install();
    } else {   // cdvd_exit 0x11E7B8: the four semaphores deleted, the power-off handler removed
        for (std::int32_t& id : g_cd.sema) {
            library_sema_delete(id);
            id = -1;
        }
        with_interrupts_off([] { charge(kSifRemoveCmdHandler); });
        g_cd.sems = false;
    }
    ret(r, 2);
}

// int sceCdMmode(int media) 0x11FA48: SCECdCD 1 / SCECdDVD 2
void cd_mmode(const CallArgs& a, CallResult& r) {
    const std::int32_t media = s32(a, 0);
    if (media != 1 && media != 2) stub_unexpected("sceCdMmode", "media " + std::to_string(media));
    cd_scmd_begin();
    charge({kDcacheWriteback, kSifCallRpc, kSignalSema});
    ret(r, 1);
}

// int sceCdDiskReady(int mode) 0x11F6F0: the disc is in and readable
void cd_disk_ready(const CallArgs& a, CallResult& r) {
    const std::int32_t mode = s32(a, 0);
    if (mode != 0) stub_unexpected("sceCdDiskReady", "mode " + std::to_string(mode));
    cd_sem_init();
    charge({kPollSema, kSifInitRpc});
    if (!g_cd.ready_bound) {
        charge(kSifBindRpc);
        g_cd.ready_bound = true;
    }
    charge({kDcacheWriteback, kSifCallRpc, kSignalSema});
    ret(r, 2);
}

// int sceCdReadClock(sceCdCLOCK *clock) 0x11FB18: the drive's answer comes at the RPC, so the clock is read where the
// RPC was made
void cd_read_clock(const CallArgs& a, CallResult& r) {
    const gaddr clock = u32(a, 0);
    cd_scmd_begin();
    const save::ClockBcd c = save::rtc_now(vblank_count());
    b3host::store<std::uint8_t>(clock, 0);   // the status byte: the clock is running
    for (std::uint32_t k = 1; k < 8; ++k) b3host::store<std::uint8_t>(clock + k, c[k]);
    charge({kSifCallRpc, kSignalSema});
    ret(r, 1);
}

// int *sceCdPOffCallback(void (*func)(void *), void *addr) 0x11E858: the handler libcdvd would call on a power-off
// command; there is no such command natively (power_off_handler hands it to the host)
void cd_poff_callback(const CallArgs& a, CallResult& r) {
    if (!g_cd.poff_installed) cd_poff_install();
    const gaddr prev = g_cd.poff_func;
    with_interrupts_off([&] {
        g_cd.poff_func = u32(a, 0);
        g_cd.poff_arg = u32(a, 1);
    });
    ret(r, static_cast<std::int32_t>(prev));
}

// ======================================================================================================== file I/O
// libkernl's file I/O (sceOpen ...): the console's own files the game reads. One: rom0:ROMVER.
constexpr char kRomver[16] = {'0', '2', '0', '0', 'A', 'C', '2', '0', '0', '4', '0', '6', '1', '4', '\n', '\0'};
struct Fio {
    bool lock = false;      // the call lock's semaphore made (_sceFsSemInit)
    bool init = false;      // sceFsInit done (the server's version read)
    bool iob_sems = false;  // the descriptor table's semaphores made (_sceFsIobSemaMK)
    bool romver_open = false;
    std::uint32_t romver_pos = 0;
} g_fio;
void fio_iob_sems() {
    if (g_fio.iob_sems) return;
    library_sema(1, 1);   // the descriptor table's
    library_sema(1, 1);   // the no-wait queue's
    g_fio.iob_sems = true;
}
void fio_lock() {   // _sceFsWaitS
    if (!g_fio.lock) {
        library_sema(1, 1);
        g_fio.lock = true;
    }
    charge(kWaitSema);
}
void fio_init() {   // sceFsInit 0x10FFA0 (the first call of a function that needs it)
    if (g_fio.init) return;
    charge(kSifInitRpc);
    with_interrupts_off([] { charge({kSifAddCmdHandler, kSifAddCmdHandler}); });
    charge(kSifBindRpc);
    fio_iob_sems();
    charge({kWaitSema, kSignalSema, kSifCallRpc});
    g_fio.init = true;
}
void fio_iob_lookup() {   // get_iob / new_iob: under the table's semaphore
    fio_iob_sems();
    charge({kWaitSema, kSignalSema});
}
// a request's own semaphore, the call, the lock released, the answer waited for and the semaphore deleted
void fio_request(std::initializer_list<gaddr> before_call) {
    charge(kCreateSema);
    charge(before_call);
    charge({kSifCallRpc, kSignalSema, kWaitSema, kDeleteSema});
}

// int sceFsReset(void) 0x110238: after the IOP reboot the file service is set up again at the next call (the server's
// version read again); the lock and descriptor semaphores stay
void fio_reset(const CallArgs&, CallResult& r) {
    g_fio.init = false;
    ret(r, 0);
}
// int sceOpen(const char *name, int flags, ...) 0x110270
void fio_open(const CallArgs& a, CallResult& r) {
    const std::string name = guest_string(u32(a, 0), 0x400);
    if (name != "rom0:ROMVER" || u32(a, 1) != 1u)
        stub_unexpected("sceOpen", "\"" + name + "\" flags " + hex(u32(a, 1)) + " (the port's file I/O has rom0:ROMVER only)");
    if (g_fio.romver_open) stub_unexpected("sceOpen", "rom0:ROMVER opened twice");
    fio_lock();
    fio_init();
    fio_iob_lookup();
    fio_request({});
    charge({kWaitSema, kSignalSema});   // the descriptor's flags and handle stored
    g_fio.romver_open = true;
    g_fio.romver_pos = 0;
    ret(r, 0);   // the first descriptor
}
// int sceRead(int fd, void *buf, int size) 0x1108B0
void fio_read(const CallArgs& a, CallResult& r) {
    if (u32(a, 0) != 0 || !g_fio.romver_open) stub_unexpected("sceRead", "descriptor " + std::to_string(s32(a, 0)));
    const gaddr buf = u32(a, 1);
    const std::int32_t size = s32(a, 2);
    if (size < 0) stub_unexpected("sceRead", "size " + std::to_string(size));
    fio_iob_lookup();
    fio_lock();
    charge(kCreateSema);
    charge({kDcacheWriteback, kDcacheWriteback});
    const std::uint32_t n = std::min<std::uint32_t>(static_cast<std::uint32_t>(size), 16u - g_fio.romver_pos);
    for (std::uint32_t k = 0; k < n; ++k) b3host::store<std::uint8_t>(buf + k, static_cast<std::uint8_t>(kRomver[g_fio.romver_pos + k]));
    g_fio.romver_pos += n;
    charge({kSifCallRpc, kSignalSema, kWaitSema, kDeleteSema});
    ret(r, static_cast<std::int32_t>(n));
}
// int sceClose(int fd) 0x1104F8
void fio_close(const CallArgs& a, CallResult& r) {
    if (u32(a, 0) != 0 || !g_fio.romver_open) stub_unexpected("sceClose", "descriptor " + std::to_string(s32(a, 0)));
    fio_iob_lookup();
    fio_lock();
    fio_request({});
    g_fio.romver_open = false;
    ret(r, 0);
}
// int sceDevctl(const char *name, int cmd, void *arg, unsigned int arglen, void *buf, unsigned int buflen) 0x112A48.
// Callers [C asm]: CB3Game::CheckPowerOff 0x1D414C and func_001D3D70 0x1D3D94, both "dev9x:" cmd 0x4402 (DDIOC_OFF).
void fio_devctl(const CallArgs& a, CallResult& r) {
    const std::string name = guest_string(u32(a, 0), 0x400);
    if (name != "dev9x:" || u32(a, 1) != 0x4402u || u32(a, 3) != 0 || u32(a, 3 + 2) != 0)
        stub_unexpected("sceDevctl", "\"" + name + "\" cmd " + hex(u32(a, 1)));
    fio_lock();
    fio_init();
    fio_request({kDcacheWriteback});
    ret(r, 0);
}

// ======================================================================================================== lgdev
// The Logitech library's start-up writes one word the game polls itself (lgDevEnumHint 0x443C70 reads the word at
// D_004B0404, the uncached alias of the 0x40-byte hint area D_01ECE300 the IOP module would write), and its semaphore.
constexpr gaddr kLgHintPtr = 0x004B0404u, kLgHintArea = 0x01ECE300u, kLgSaved = 0x004B0400u;
constexpr gaddr kLgBannerFormat = 0x004D23F0u, kLgVersionText = 0x004B040Cu;   // printf("%s\n", D_004B040C[0])
bool g_lg_bound = false, g_lg_sema = false;
// int lgDevInit(void) 0x4438E0 (func_004438E0)
void lg_init(const CallArgs&, CallResult& r) {
    // the library's banner, printed for real: the C library's stdout takes its buffer from the heap at its first use,
    // and the heap must be laid out as the game's code then finds it
    b3host::call_guest<int>(kPrintf, kLgBannerFormat, b3host::load<std::uint32_t>(kLgVersionText));
    charge(kSifInitRpc);
    b3host::store<std::uint32_t>(kLgHintPtr, kLgHintArea | 0x20000000u);
    b3host::store<std::uint32_t>(kLgHintArea, 0);
    if (!g_lg_bound) {
        charge(kSifBindRpc);
        g_lg_bound = true;
    }
    charge({kInvalidDCache, kSifCallRpc});   // the module's version: 1.11.27, the one the library wants
    // the library's semaphore (D_004B0408, made once; no other lgdev function the game reaches without a device uses it)
    if (!g_lg_sema) {
        library_sema(1, 1);
        g_lg_sema = true;
    } else {
        b3host::call_guest<int>(kPrintf, 0x004D2500u);   // "already initialised"
    }
    if (b3host::load<std::uint32_t>(kLgSaved)) stub_unexpected("lgDevInit", "a device state saved across an IOP reboot");
    ret(r, 0);
}
// int lgDevEnumerate(unsigned int index, lgDevInfo *out) 0x443CC8: no device at any index
void lg_enumerate(const CallArgs& a, CallResult& r) {
    const gaddr out = u32(a, 1);
    if (!out) {
        b3host::call_guest<int>(kPrintf, 0x004D2560u);   // the null-argument message
        ret(r, static_cast<std::int32_t>(0x80000004u));
        return;
    }
    charge({kSifCheckStatRpc, kSifCallRpc});
    b3host::fill_guest(out, 0, 0x18);
    ret(r, static_cast<std::int32_t>(0x80000002u));
}

}  // namespace

void power_off_handler(b3host::gaddr& func, b3host::gaddr& arg) {
    func = g_cd.poff_func;
    arg = g_cd.poff_arg;
}

// the rows (platform.h B3NATIVE_FIRST)
void dev_cd_init(const CallArgs& a, CallResult& r) { cd_init(a, r); }
void dev_cd_mmode(const CallArgs& a, CallResult& r) { cd_mmode(a, r); }
void dev_cd_disk_ready(const CallArgs& a, CallResult& r) { cd_disk_ready(a, r); }
void dev_cd_read_clock(const CallArgs& a, CallResult& r) { cd_read_clock(a, r); }
void dev_cd_poff_callback(const CallArgs& a, CallResult& r) { cd_poff_callback(a, r); }
void dev_fs_reset(const CallArgs& a, CallResult& r) { fio_reset(a, r); }
void dev_open(const CallArgs& a, CallResult& r) { fio_open(a, r); }
void dev_read(const CallArgs& a, CallResult& r) { fio_read(a, r); }
void dev_close(const CallArgs& a, CallResult& r) { fio_close(a, r); }
void dev_devctl(const CallArgs& a, CallResult& r) { fio_devctl(a, r); }
void dev_lg_init(const CallArgs& a, CallResult& r) { lg_init(a, r); }
void dev_lg_enumerate(const CallArgs& a, CallResult& r) { lg_enumerate(a, r); }

}  // namespace b3native::platform
