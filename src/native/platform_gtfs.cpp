// The game's file system on the disc (S4 of the SIF lane, 2026-11-11; platform.h B3NATIVE_FIRST).
//
// Criterion's GtFileSystem reads the disc through its PS2 back end, CGtFileSystemPS2DVD with one CGtFilePS2DVD per open
// file (GtFileSystemPS2DVD.cpp in the retail tables). On a PS2 that back end is the EE half of Criterion's IOP module
// GTFSCDVD.IRX: each operation is an RPC to it (server 0x475453), and a read or a seek ends when the module sends SIF
// command 4 back, whose handler (CmdReadCompleteHandler 0x1D2F50) marks the file ready and signals its semaphore.
// Here the back end's own functions are rows: the host is the drive. No request is built and no RPC made; what the
// game sees is what those functions leave on a PS2 -- the file objects' fields, the results, the bytes read -- and
// the read completion is still the interrupt it is there: SIF command 4, run in interrupt context by the platform's
// command channel (platform_sif.cpp) through the handler the game registered, at the first safe point after the read
// (the next dispatched call: before the reading thread reaches its Sync).
//
// Each row is the retail function as the decomp has it [C decomp src/b3/_unclassified/GtFileSystemPS2DVD_001D2FA0,
// _001D30B0, _001D3280, _001D3670, _001D36E0, _001D3850.cpp, matched to the retail asm], with its calls to the kernel
// (semaphores, the cache, the interrupt switches), to libsif's command handlers and to the game (the file name
// builders, the file's Sync) made for real, and the RPC replaced by the drive's answer plus the virtual time the
// retail RPC takes (platform_charge.h charge(sceSifCallRpc)): every call, interrupt and VBlank falls where it fell
// with the RPC. memcpy / strncpy are the host's, as in the generated functions the rows replace.
//
// The drive [L: from the requests' shapes in the EE code; files are addressed by name]: Init's first argument is the
// number of files the game keeps open at most (its file objects, PrepareHardware: 1), so the drive has that many
// handles, and a handle is the index of the file object the completion marks (the handler indexes the file array by
// it). Open: the lowest free handle (-1 if none, or no such file), the size and the size in sectors. Read: whole
// 2048-byte sectors from the handle's sector position to an EE address -- bytes past the end of the file read as 0 --
// and the position moves on (not past the end). Seek: the position set to a sector (the game seeks from the start
// only). Close frees the handle. Abort: a read here is complete when the row returns, so there is nothing to stop.
#include "platform.h"
#include "platform_charge.h"
#include "platform_disc.h"
#include "platform_sif.h"

#include "b3host/call.h"
#include "b3host/native_backend.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace b3native::platform {
namespace {

using b3host::CallArgs;
using b3host::CallResult;
using b3host::gaddr;

inline std::uint32_t u32(const CallArgs& a, unsigned i) { return a.gpr[i].w[0]; }
inline std::uint32_t ld(gaddr p) { return b3host::load<std::uint32_t>(p); }
inline void st(gaddr p, std::uint32_t v) { b3host::store<std::uint32_t>(p, v); }
inline std::int64_t ld64(gaddr p) { return b3host::load<std::int64_t>(p); }
inline void st64(gaddr p, std::int64_t v) { b3host::store<std::int64_t>(p, v); }
inline void ret(CallResult& r, std::int64_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(static_cast<std::uint64_t>(v));
}
inline void ret32(CallResult& r, std::int32_t v) { ret(r, v); }   // sign-extended, as a 32-bit result is held

// The retail functions and data [C decomp config/symbol_addrs.txt; the generated units' globals]
constexpr gaddr kCreateSema = 0x0010BE20u, kSignalSema = 0x0010BE40u, kWaitSema = 0x0010BE60u, kSyncDCache = 0x0010C718u,
                kFlushCache = 0x0010C0A0u, kDIntr = 0x00114E60u, kEIntr = 0x00114EB8u, kSifAddCmdHandler = 0x0010E408u,
                kSifBindRpc = 0x0010F018u, kSifCallRpc = 0x0010F1E8u, kSifSendCmd = 0x0010E608u, kSifDmaStat = 0x0010C1C0u,
                kGtFileSystemInit = 0x002129A0u,           // CGTFileSystem::Init(unsigned, const char *)
                kBuildFileName = 0x00212A80u,              // CGTFileSystem::BuildFileName(char *, int, const char *, const char *, char)
                kFileNameFromDevice = 0x00212B80u,         // CGTFileSystem::GetFileNameFromDeviceName(const char *)
                kCmdReadComplete = 0x001D2F50u, kCmdError = 0x001D2EE0u;   // CGtFileSystemPS2DVD's command handlers
constexpr gaddr kArgs = 0x0066DF80u,          // CGtFilePS2DVD::mRPCArguments (0x100 bytes)
                kResults = 0x0066E080u,       // CGtFilePS2DVD::mRPCResults (4 words)
                kCmdHdr = 0x0066E0C0u,        // CGtFilePS2DVD::mCmdHdr (the abort command's packet)
                kRpcSema = 0x004E2734u,       // CGtFilePS2DVD::mnRPCSemaphore
                kCmdHandle = 0x004E2738u,     // CGtFilePS2DVD::mCmdHandle
                kIopFileFuncs = 0x004E2730u,  // CGtFileSystemPS2DVD::mpIOPFileFuncs
                kDiscPrefix = 0x004B7DE8u;    // the folder Open puts in front of a name (D_004B7DE8)
constexpr std::uint32_t kServer = 0x00475453u;   // GTFSCDVD's RPC server id (the SIF log's request lines)
// Init's answer: the IOP address of the module's file functions, which the game hands to RW Audio's IOP side
// (RwaPS2SetIOPFileInterface) for its own file reads. RW Audio's IOP side reads files by name here (platform_rwa.cpp)
// and never calls through it; any nonzero value would do -- this one is kept so that
// guest memory and RW Audio's commands stay as the golden runs recorded them.
constexpr std::uint32_t kIopFileInterface = 0x00081F30u;

// CGtFilePS2DVD's fields [C the decomp's UnkStruct_001D30B0 / _001D3280 / _001D36E0]
constexpr std::uint32_t kfSystem = 0x04,   // the file system
                        kfSize = 0x08,     // long: the file's size
                        kfPos = 0x10,      // long: the position
                        kfState = 0x18,    // 0 closed, 1 ready, 2 busy (a read or seek in flight)
                        kfWritten = 0x1C,
                        kfAsync = 0x20,    // opened with flag 0x10: reads return without waiting
                        kfHandle = 0x28,   // the drive's handle
                        kfSema = 0x2C,     // signalled by the read completion
                        kfBufPos = 0x30,   // the position in the sector buffer (0x800: empty)
                        kfBuffer = 0x34;   // the file's 2048-byte sector buffer
constexpr std::uint32_t kFileStride = 0x38, kSector = 0x800u;

// ======================================================================================================== the drive
struct Slot {
    disc::File file;
    std::uint32_t sector = 0;   // the next sector a read takes
};
std::vector<Slot> g_slots;
std::uint32_t sector_count(const Slot& s) { return static_cast<std::uint32_t>((s.file.size + kSector - 1) / kSector); }

void log_request(std::uint32_t fno, std::uint32_t send_size, std::uint32_t recv_size) {
    if (std::FILE* f = sif_log())
        std::fprintf(f, "R %llu %llu %x %x %x %x\n", (unsigned long long)vblank_count(), (unsigned long long)virtual_cycles(),
                     kServer, fno, send_size, recv_size);
}
// The read / seek completion: SIF command 4 for the file system's handler, its packet the command header with the
// file's handle in the last word (the handler reads its low byte)
void complete(std::uint32_t handle) {
    const std::uint32_t packet[4] = {0x10u, 0u, 4u, handle};
    sif::queue_cmd_reply(4, packet, sizeof packet);
}
Slot& slot(std::uint32_t handle, const char* what) {
    if (handle >= g_slots.size() || !g_slots[handle].file.is_open())
        stub_unexpected(what, "handle " + std::to_string(handle) + " is not an open file (the game would wait for ever)");
    return g_slots[handle];
}

void drive_init(std::uint32_t files, gaddr path) {
    if (path && b3host::load<std::uint8_t>(path))
        stub_unexpected("GTFS init", "a path argument (the game passes none)");
    g_slots.assign(files, Slot{});
}
// {handle or -1, size, sectors, 0} into the results
void drive_open(const std::string& name) {
    b3host::fill_guest(kResults, 0, 16);
    std::uint32_t h = 0;
    while (h < g_slots.size() && g_slots[h].file.is_open()) ++h;
    // mac5: B3NATIVE_GTFS_FAIL=<text>: an open of a name containing it fails as a missing file (diagnostics: the car
    // select's static when a car never loads)
    static const char* const fail = std::getenv("B3NATIVE_GTFS_FAIL");
    const bool failed = fail && *fail && name.find(fail) != std::string::npos;
    if (h < g_slots.size() && !failed && g_slots[h].file.open(name)) {
        g_slots[h].sector = 0;
        st(kResults, h);
        st(kResults + 4, static_cast<std::uint32_t>(g_slots[h].file.size));
        st(kResults + 8, sector_count(g_slots[h]));
    } else {
        st(kResults, 0xFFFFFFFFu);
        stub_note("GTFS open", "a name not on the disc (or every handle busy): -1");
    }
    if (std::FILE* f = sif_log()) {   // the name, the answer and the game's call chain to the open
        const bool ok = static_cast<std::int32_t>(ld(kResults)) >= 0;
        std::fprintf(f, "O %s -> %s size %llu; chain", name.c_str(), ok ? "ok" : "-1",
                     static_cast<unsigned long long>(ok ? g_slots[h].file.size : 0));
        for (unsigned k = 0; k < b3host::native_chain::t_depth && k < b3host::native_chain::kMax; ++k)
            std::fprintf(f, " %08X", b3host::native_chain::t_chain[k]);
        std::fprintf(f, "\n");
    }
}
void drive_read(std::uint32_t handle, std::uint32_t sectors, gaddr dst) {
    Slot& s = slot(handle, "GTFS read");
    const std::uint64_t bytes = static_cast<std::uint64_t>(sectors) * kSector;
    if (bytes > 0xFFFFFFFFull || !b3host::host_pointer(dst, static_cast<std::uint32_t>(bytes)))
        stub_unexpected("GTFS read", "a destination outside EE RAM (" + std::to_string(sectors) + " sectors)");
    const std::uint64_t at = static_cast<std::uint64_t>(s.sector) * kSector;
    const std::uint64_t n = at < s.file.size ? std::min<std::uint64_t>(bytes, s.file.size - at) : 0;
    auto* host = static_cast<std::uint8_t*>(b3host::host_pointer(dst, static_cast<std::uint32_t>(bytes)));
    if (n) s.file.read(at, host, static_cast<std::size_t>(n));
    std::memset(host + n, 0, static_cast<std::size_t>(bytes - n));
    if (std::FILE* f = sif_log()) {   // mac5: the bytes read and their hash
        std::uint64_t h = 0xCBF29CE484222325ull;
        for (std::uint64_t k = 0; k < n; ++k) h = (h ^ host[k]) * 0x100000001B3ull;
        std::fprintf(f, "D off %llu bytes %llu of %llu fnv %016llx\n", (unsigned long long)at, (unsigned long long)n,
                     (unsigned long long)bytes, (unsigned long long)h);
    }
    s.sector = static_cast<std::uint32_t>(std::min<std::uint64_t>(sector_count(s), static_cast<std::uint64_t>(s.sector) + sectors));
    complete(handle);
}
void drive_seek(std::uint32_t handle, std::int32_t sector, std::uint32_t whence) {
    Slot& s = slot(handle, "GTFS seek");
    if (whence != 0) stub_unexpected("GTFS seek", "from " + std::to_string(whence) + " (the game seeks from the start)");
    s.sector = sector < 0 ? 0u : std::min<std::uint32_t>(static_cast<std::uint32_t>(sector), sector_count(s));
    complete(handle);
}
void drive_close(std::uint32_t handle) {
    if (handle < g_slots.size()) g_slots[handle] = Slot{};
}

// One request the retail code makes to the drive, in its place: the drive answers, and the RPC's virtual time is
// charged after the answer (the completion it queued is then delivered at the charge, as it was at sceSifCallRpc's
// return). The request is one SIF DMA (the kernel's DMA ids).
template <class F>
void request(std::uint32_t fno, std::uint32_t send_size, std::uint32_t recv_size, F&& answer) {
    log_request(fno, send_size, recv_size);
    answer();
    sif::next_dma_id();
    charge(kSifCallRpc);
}

std::int32_t rpc_sema() { return static_cast<std::int32_t>(ld(kRpcSema)); }
void wait_rpc() { b3host::call_guest<int>(kWaitSema, rpc_sema()); }
void signal_rpc() { b3host::call_guest<int>(kSignalSema, rpc_sema()); }
void file_sync(gaddr file) { b3host::vcall<int>(file, 0, 0x1C, 1); }   // CGtFilePS2DVD::Sync(1): waits for the completion
void sync_dcache(std::uint32_t lo, std::uint32_t hi) { b3host::call_guest<void>(kSyncDCache, lo, hi); }
// strncpy(dst, src, n) on guest memory: up to the NUL, the rest of n zeroed
void guest_strncpy(gaddr dst, gaddr src, std::uint32_t n) {
    std::uint32_t k = 0;
    for (; k < n; ++k) {
        const std::uint8_t c = b3host::load<std::uint8_t>(src + k);
        if (!c) break;
        b3host::store<std::uint8_t>(dst + k, c);
    }
    if (k < n) b3host::fill_guest(dst + k, 0, n - k);
}

}  // namespace

// bool CGtFileSystemPS2DVD::Init(unsigned n, CGtFilePS2DVD *files, CGtFileBufferPS2DVD *bufs, const char *name, int a,
// int b, int c, char *path) 0x1D3850: the n file objects get their sector buffers and semaphores, the request lock is
// made, the read-complete / error handlers registered (cid 4 / 5), the drive started; true when it answered.
void gtfs_init(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0), files = u32(a, 2), bufs = u32(a, 3), name = u32(a, 4), path = ld(a.sp);
    const std::uint32_t n = u32(a, 1), ia = u32(a, 5), ib = u32(a, 6), ic = u32(a, 7);
    st(self + 0x10, files);
    st(self + 0x14, bufs);
    for (std::uint32_t i = 0; i < n; ++i) st(files + i * kFileStride + kfBuffer, bufs + i * kSector);
    if (b3host::call_guest<int>(kGtFileSystemInit, self, n, name) == 0) { ret32(r, 0); return; }
    {
        b3host::GuestStackFrame sema(0x20);   // ee_sema_t: +4 max 1, +8 initial 0 (the request lock: 1)
        b3host::fill_guest(sema.addr(), 0, 0x18);
        st(sema.addr() + 4, 1);
        for (std::uint32_t j = 0; j < n; ++j)
            st(files + j * kFileStride + kfSema, static_cast<std::uint32_t>(b3host::call_guest<int>(kCreateSema, sema.addr())));
        st(sema.addr() + 8, 1);
        st(kRpcSema, static_cast<std::uint32_t>(b3host::call_guest<int>(kCreateSema, sema.addr())));
    }
    st(kCmdHandle, 0);
    b3host::call_guest<int>(kDIntr);
    b3host::call_guest<void>(kSifAddCmdHandler, 4u, kCmdReadComplete, self);
    b3host::call_guest<void>(kSifAddCmdHandler, 5u, kCmdError, self);
    b3host::call_guest<int>(kEIntr);
    sif::next_dma_id();   // the bind: one request, answered the first time
    charge(kSifBindRpc);
    st(kArgs + 0x0, n);
    st(kArgs + 0x4, ia);
    st(kArgs + 0x8, ib);
    st(kArgs + 0xC, ic);
    if (path) guest_strncpy(kArgs + 0x10, path, 0x20);
    else b3host::store<std::uint8_t>(kArgs + 0x10, 0);
    b3host::call_guest<void>(kFlushCache, 0);
    request(1, 0x30, 4, [&] {
        drive_init(n, path);
        st(kIopFileFuncs, kIopFileInterface);
    });
    ret32(r, ld(kIopFileFuncs) != 0 ? 1 : 0);
}

// int CGtFilePS2DVD::Open(CGTFileSystem *fs, const char *name, unsigned flags) 0x1D36E0: 0, or 5 (the name does not
// fit), 4 (a write mode), 2 (not on the disc)
void gtfs_open(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0), fs = u32(a, 1), name = u32(a, 2);
    const std::uint32_t flags = u32(a, 3);
    std::string full;
    {
        b3host::GuestStackFrame buf(0x40);
        const std::uint32_t base = b3host::call_guest<std::uint32_t>(kFileNameFromDevice, name);
        if (b3host::call_guest<std::uint32_t>(kBuildFileName, buf.addr(), 0x40, kDiscPrefix, base, static_cast<int>('\\')) == 0) {
            ret32(r, 5);
            return;
        }
        if (flags & 0xEu) { ret32(r, 4); return; }
        st(self + kfAsync, (flags & 0x10u) ? 1u : 0u);
        guest_strncpy(kArgs, buf.addr(), 0x100);
    }
    for (std::uint32_t k = 0; k < 0x40; ++k) {   // the request's name (0x40 bytes go to the IOP)
        const char c = static_cast<char>(b3host::load<std::uint8_t>(kArgs + k));
        if (!c) break;
        full += c;
    }
    wait_rpc();
    request(3, 0x40, 0x10, [&] { drive_open(full); });
    signal_rpc();
    const std::int32_t handle = static_cast<std::int32_t>(ld(kResults));
    if (handle < 0) { ret32(r, 2); return; }
    st(self + kfHandle, static_cast<std::uint32_t>(handle));
    st64(self + kfPos, 0);
    st64(self + kfSize, static_cast<std::int64_t>(ld(kResults + 4)));
    st(self + kfBufPos, kSector);
    st(self + kfSystem, fs);
    st(self + kfState, 1);
    st(self + kfWritten, 0);
    ret32(r, 0);
}

// void CGtFilePS2DVD::Close() 0x1D3670
void gtfs_close(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0);
    st(self + kfState, 0);
    st(kArgs, ld(self + kfHandle));
    wait_rpc();
    request(4, 0x10, 0, [&] { drive_close(ld(kArgs)); });
    signal_rpc();
    r = CallResult{};
}

// long CGtFilePS2DVD::SetPosition(long pos, EFileMoveMethod method) 0x1D30B0: a position inside the sector buffer moves
// in it; any other is a seek on the drive (waited for unless the file is asynchronous); the new position
void gtfs_set_position(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0);
    std::int64_t pos = static_cast<std::int64_t>(b3host::detail::low64(a.gpr[1]));
    const std::int32_t method = static_cast<std::int32_t>(u32(a, 2));
    const std::int64_t size = ld64(self + kfSize), cur = ld64(self + kfPos);
    if (method == 1) pos += size < cur ? size : cur;
    else if (method == 2) pos = size - pos;
    if (size < pos) pos = size;
    else if (pos < 0) pos = 0;
    const std::int32_t in_buf = static_cast<std::int32_t>(ld(self + kfBufPos));
    if (in_buf < static_cast<std::int32_t>(kSector)) {
        const std::int64_t lo = cur - in_buf;
        const std::int64_t hi = static_cast<std::int64_t>(static_cast<std::uint64_t>(cur) + static_cast<std::uint32_t>(kSector - in_buf));
        if (pos >= lo && pos <= hi) {
            st(self + kfBufPos, static_cast<std::uint32_t>(pos - lo));
            st64(self + kfPos, pos);
            ret(r, pos);
            return;
        }
    }
    st(self + kfBufPos, kSector);
    st64(self + kfPos, pos);
    st(self + kfState, 2);
    st(kArgs + 0, ld(self + kfHandle));
    st(kArgs + 4, static_cast<std::uint32_t>(static_cast<std::int32_t>(pos / kSector)));
    st(kArgs + 8, 0);
    wait_rpc();
    request(6, 0x10, 0, [&] { drive_seek(ld(kArgs), static_cast<std::int32_t>(ld(kArgs + 4)), ld(kArgs + 8)); });
    signal_rpc();
    if (ld(self + kfAsync) == 0) file_sync(self);
    ret(r, ld64(self + kfPos));
}

// int CGtFilePS2DVD::Read(void *p, unsigned n) 0x1D3280: the bytes read (up to the end of the file). An asynchronous
// file reads whole sectors straight to p and returns at once; otherwise the sector buffer serves the unaligned parts,
// aligned whole sectors go straight to p, and each drive read is waited for.
void gtfs_read(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0);
    std::uint32_t p = u32(a, 1) & 0x0FFFFFFFu, n = u32(a, 2);
    const std::int64_t size = ld64(self + kfSize), pos = ld64(self + kfPos);
    if (pos >= size) { ret32(r, 0); return; }
    std::int32_t result;
    if (size < pos + static_cast<std::int32_t>(n)) {
        const std::int64_t end = (size + 0x7FF) & static_cast<std::int64_t>(0xFFFFF800u);   // the last sector's end
        n = pos + static_cast<std::int64_t>(n) >= end ? static_cast<std::uint32_t>(static_cast<std::int32_t>(end - pos))
                                                       : static_cast<std::uint32_t>(static_cast<std::int32_t>(size - pos));
        result = static_cast<std::int32_t>(size - pos);
    } else {
        result = static_cast<std::int32_t>(n);
    }
    auto drive_read_into = [&](std::uint32_t sectors, std::uint32_t dst) {
        st(self + kfState, 2);
        st(kArgs + 0, ld(self + kfHandle));
        st(kArgs + 4, sectors);
        st(kArgs + 8, dst);
        wait_rpc();
        request(5, 0x10, 0, [&] { drive_read(ld(kArgs), ld(kArgs + 4), ld(kArgs + 8)); });
        signal_rpc();
    };
    if (ld(self + kfAsync) != 0) {
        sync_dcache(p, p + n - 1);
        st(self + kfState, 2);
        st64(self + kfPos, ld64(self + kfPos) + n);
        drive_read_into(n / kSector, p);
        ret32(r, result);
        return;
    }
    while (n != 0) {
        if (static_cast<std::int32_t>(ld(self + kfBufPos)) < static_cast<std::int32_t>(kSector)) {   // from the sector buffer
            const std::uint32_t buffer = ld(self + kfBuffer) | 0x30000000u;
            if (ld(self + kfBufPos) == 0) st(self + kfBufPos, static_cast<std::uint32_t>(static_cast<std::int32_t>(ld64(self + kfPos) % kSector)));
            const std::uint32_t off = ld(self + kfBufPos);
            std::uint32_t c = kSector - off;
            if (n < c) c = n;
            std::memcpy(b3host::host_pointer_checked(p, c), b3host::host_pointer_checked(buffer + off, c), c);
            n -= c;
            const std::int64_t at = ld64(self + kfPos);
            if (ld64(self + kfSize) < at + static_cast<std::int32_t>(c)) c = static_cast<std::uint32_t>(static_cast<std::int32_t>(ld64(self + kfSize) - at));
            st(self + kfBufPos, ld(self + kfBufPos) + c);
            p += c;
            st64(self + kfPos, ld64(self + kfPos) + static_cast<std::int32_t>(c));
        }
        if (n >= kSector && (p & 0xFu) == 0 && ld64(self + kfPos) % kSector == 0) {   // whole sectors straight to p
            const std::uint32_t sectors = n >> 11, bytes = sectors << 11;
            sync_dcache(p, p + bytes - 1);
            st(self + kfState, 2);
            st64(self + kfPos, ld64(self + kfPos) + bytes);
            drive_read_into(sectors, p);
            file_sync(self);
            p += bytes;
            n -= sectors * kSector;
        }
        if (n != 0) {   // the next sector into the buffer
            st(self + kfState, 2);
            drive_read_into(1, ld(self + kfBuffer));
            file_sync(self);
            st(self + kfBufPos, static_cast<std::uint32_t>(static_cast<std::int32_t>(ld64(self + kfPos) % kSector)));
        }
    }
    ret32(r, result);
}

// int CGtFilePS2DVD::Abort() 0x1D2FA0: for an asynchronous file with a read in flight, the abort command (SIF command 4
// with the handle | 0x100) goes to the drive; 1 then, else 0. A read here has completed by the time Abort can run, so
// the drive has nothing to stop; the row takes the command's time and leaves the game's command packet and handle as
// sceSifSendCmd would (the header in the packet's first three words, the DMA's id in mCmdHandle).
void gtfs_abort(const CallArgs& a, CallResult& r) {
    const gaddr self = u32(a, 0);
    if (ld(self + kfAsync) == 0 || ld(self + kfState) != 2) { ret32(r, 0); return; }
    if (ld(kCmdHandle) != 0) {
        charge(kSifDmaStat);   // the previous command's DMA: long done
        st(kCmdHandle, 0);
    }
    st(kCmdHdr + 0xC, b3host::load<std::uint8_t>(self + kfHandle) | 0x100u);
    st(kCmdHdr + 0, 0x10u);
    st(kCmdHdr + 4, 0u);
    st(kCmdHdr + 8, 4u);
    const std::int32_t id = sif::next_dma_id();
    if (std::FILE* f = sif_log())
        std::fprintf(f, "C %llu %llu %x %x %x\n", (unsigned long long)vblank_count(), (unsigned long long)virtual_cycles(), 4u, 0x10u, 0u);
    charge(kSifSendCmd);
    st(kCmdHandle, static_cast<std::uint32_t>(id));
    ret32(r, 1);
}

}  // namespace b3native::platform
