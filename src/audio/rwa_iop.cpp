// The native IOP command executor for RenderWare Audio: RWA.IRX's protocol layer as host C++ (lane audio1).
// Spec: docs/audio-irx.md (section numbers below are its own); design: docs/audio.md §3.1. Addresses `irx_XXXXXXXX`
// are RWA.IRX's module-relative functions the behaviour is taken from.
//
// Threads become one discrete-event loop in virtual time (48 kHz samples):
//   - the RPC thread takes packets FIFO from an 8-entry ring (§1.1) and runs each to completion; a command that
//     blocks (DelayThread, a transfer it waits for) parks the thread until its continuation event runs;
//   - the SPU2 core-0 IRQ (§3.1), raised by the mixer (Spu2Mixer::run) when its output ring passes IRQA 0x800 /
//     0xA00, i.e. every 256 samples, drives the volume sweep (every second IRQ) and the mixer stream;
//   - the file thread runs one queued file operation at a time (§6);
//   - transfers complete at once, or after a modelled number of IRQs (RwaIopOptions).
// Events at the same time run in the order they were scheduled; an IRQ due at the same time runs first.
#include "../native/rwa_iop.h"

#include "spu2_mixer.h"
#include "spu2_stub_mixer.h"
namespace b3audio { void spu2_mark_stream_ring(uint32_t base, uint32_t len); }   // spu2_mixer_impl.cpp (mac5 diagnostics)

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace b3native {

using b3audio::Spu2Mixer;
namespace sd = b3audio::sd;

namespace {

constexpr uint32_t kIopRamBytes = 0x200000, kIopMask = kIopRamBytes - 1;
// Module-relative offsets [C spec §8, §1.3]: file request slots, the state / snapshot area, the op-2 staging buffer.
constexpr uint32_t kOffSlots = 0xB1A0, kOffState = 0xE100, kOffStaging = 0xECA0;
constexpr uint32_t kSlotBytes = 0x70, kStateBytes = 0x680, kWorkAreaBytes = 0x414;
constexpr uint32_t kVersion = 0x115;
constexpr uint64_t kIrqPeriod = 256;   // [L §3.1] IRQA alternating 0x800 / 0xA00: 256 samples per IRQ
constexpr uint32_t kMaxChunk = 0xFFC00, kSpuChunk = 0x200000;
// DelayThread(us) in samples, rounded up (48 samples per ms).
constexpr uint64_t us_to_samples(uint64_t us) { return (us * 48 + 999) / 1000; }
constexpr uint64_t kLoopFixDelay = us_to_samples(83);  // 0x53 us -> 4 samples
constexpr uint64_t kLockPoll = us_to_samples(5334);    // 0x14D6 us [C irx_000046A8]

// The 0x40 bytes op 0x11 writes to sound RAM 0x5010 [C RWA.IRX .data 0xAB00].
const uint8_t kSilentBlocks[0x40] = {
    0x0C, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x0C, 0x07, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x07, 0x00, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,
    0x07, 0x00, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,
};

// The sweep rate table R[1..256] (u32 words at .data 0xAB48; R[256] = word 0xAF44) [C §2.5, rwa_data.dis].
uint16_t sweep_rate(int d) {
    struct Run { int last; uint16_t v; };
    static const Run runs[] = {{1, 0x43}, {2, 0x3D}, {3, 0x3A}, {4, 0x38}, {5, 0x37}, {6, 0x35}, {7, 0x34},
                               {9, 0x33}, {11, 0x32}, {13, 0x31}, {14, 0x30}, {18, 0x2F}, {23, 0x2E}, {25, 0x2D},
                               {29, 0x2C}, {37, 0x2B}, {44, 0x2A}, {51, 0x29}, {59, 0x28}, {74, 0x27}, {88, 0x26},
                               {101, 0x25}, {118, 0x24}, {143, 0x23}, {181, 0x22}, {209, 0x21}, {227, 0x20},
                               {256, 0x1F}};
    d = std::clamp(d, 1, 256);   // [G] beyond 256 retail reads unrelated .data; EE volumes keep d <= 256
    for (const Run& r : runs)
        if (d <= r.last) return r.v;
    return 0x1F;
}

// Per hardware voice, the "HV" record at module 0xC840 + n * 0x30 [C §2].
struct Hv {
    uint32_t n = 0;
    uint16_t entry = 0;      // libsd voice entry (n >= 24) | (n % 24) << 1
    int32_t freq = 0;        // +0x08 last Frequency
    int32_t tl = 0, tr = 0;  // +0x10 / +0x14 target L / R
    int32_t ll = 0, lr = 0;  // +0x18 / +0x1C last volume L / R
    uint32_t flags = 0;      // +0x20: 2 R inverted, 4 L inverted, 8 L sweeping, 0x10 R sweeping
    uint32_t f24 = 0;        // +0x24 bit 0 forces targets to 0 (never set by RWA [L])
};

struct FileObj {
    uint32_t cursor = 0;     // mac4: the file position (seeks set it, reads advance it): B3_RWA_FILE_LOG only
    int handle = 0;
    bool failed = false;
};

struct FileOp {
    int kind = 0;            // the file thread's kinds [C irx_000003EC]: 1 open, 3 seek, 4 read
    std::shared_ptr<FileObj> obj;
    uint32_t req = 0;        // IOP address of the 0x70-byte request (the callback's argument)
    std::string name;        // open
    uint32_t buf = 0, size = 0, pos = 0;
};

}  // namespace

// RWA.IRX's AllocSysMemory: first fit in 0x100-byte granules over [base, 2 MB) (see RwaIopOptions::heap_base).
struct IrxArena {
    uint32_t base = 0;
    std::map<uint32_t, uint32_t> used;   // address -> bytes
    uint32_t alloc(uint32_t n) {
        n = (n + 0xFFu) & ~0xFFu;
        uint32_t at = base;
        for (const auto& [a, len] : used) {
            if (a >= at + n) break;
            at = a + len;
        }
        if (at + n > 0x200000u) return 0;
        used[at] = n;
        return at;
    }
    void free(uint32_t a) { used.erase(a); }
};

struct RwaIop::Impl {
    RwaIopPlatform& pf;
    std::unique_ptr<Spu2Mixer> own_mixer;
    Spu2Mixer* mx;
    RwaIopOptions opt;
    Spu2IrqHook* irq_hook = nullptr;   // a module hooked in front of RWA's SPU2 handler (B3ROUTE)
    RwaIopStats st;
    std::vector<uint8_t> ram;

    // Fixed addresses.
    uint32_t slots, state, staging, work_area = 0;

    // Event loop.
    uint64_t now = 0, seq = 0, next_irq = 1, irq_index = 0;   // next_irq: predicted (last IRQ + 256)
    uint32_t irqa0 = 0x800;   // core 0's armed IRQA, alternating 0x800 / 0xA00
    std::map<std::pair<uint64_t, uint64_t>, std::function<void()>> events;

    // RPC thread.
    std::deque<std::array<uint32_t, 12>> rx;
    bool rpc_busy = false, pumping = false;

    // Output state (IOP-private part of the state area, +0x680..) [C irx_000017C0].
    uint32_t num_voices = 0, rev_cores = 0, reply_addr = 0, mem_const = 0;
    uint32_t kon[2] = {0, 0}, koff[2] = {0, 0};
    uint32_t vmix_cache[4][2] = {};   // dry L, dry R, wet L, wet R per core (+0x6A8..+0x6C4)
    uint32_t gain_cache = 0, revtype_cache = 0, speaker_cache = 0, delay_cache = 0, feedback_cache = 0;
    int16_t revgain_cache[2] = {0, 0};
    b3audio::Spu2EffectAttr rev_attr;
    bool rev_sema_free = false, sweep_on = false, suspended = false;
    std::array<Hv, 48> hv{};

    // Transfers: logical channels 0 / 1 (RWA's transfer queues), the channel-1 completion callback's packet copy.
    uint64_t ch_free[2] = {0, 0};
    bool dma_locked[2] = {false, false};
    std::array<uint32_t, 12> cb_pkt{};
    bool cb_set = false;
    bool genuine_pending = false;

    // Mixer stream (ops 0x16 / 0x17).
    IrxArena heap;
    uint32_t ring_alloc = 0, ring = 0, ring_bytes = 0, ring_pos = 0, memin_slot = 0, tick_half = 1;   // tick_half: .data 0xAB40 = 1

    // File thread.
    bool file_table = false, file_busy = false;
    std::deque<FileOp> fq;
    std::array<std::shared_ptr<FileObj>, 4> slot_file{};

    Impl(RwaIopPlatform& p, Spu2Mixer* m, const RwaIopOptions& o)
        : pf(p), mx(m), opt(o), ram(kIopRamBytes, 0) {
        if (mx == nullptr) {
            own_mixer = std::make_unique<b3audio::Spu2StubMixer>();
            mx = own_mixer.get();
        }
        slots = opt.module_base + kOffSlots;
        state = opt.module_base + kOffState;
        staging = opt.module_base + kOffStaging;
        for (uint32_t n = 0; n < 48; ++n) {
            hv[n].n = n;
            hv[n].entry = sd::voice(static_cast<int>(n));
        }
        module_start();
    }

    // ---- memory ----
    uint8_t& b(uint32_t a) { return ram[a & kIopMask]; }
    uint32_t r32(uint32_t a) {
        uint32_t v = 0;
        for (int k = 3; k >= 0; --k) v = v << 8 | b(a + k);
        return v;
    }
    void w32(uint32_t a, uint32_t v) {
        for (int k = 0; k < 4; ++k) b(a + k) = static_cast<uint8_t>(v >> (8 * k));
    }
    int16_t r16s(uint32_t a) { return static_cast<int16_t>(b(a) | b(a + 1) << 8); }
    void copy_in(uint32_t a, const uint8_t* src, uint32_t n) {
        for (uint32_t k = 0; k < n; ++k) b(a + k) = src[k];
    }
    std::vector<uint8_t> copy_out(uint32_t a, uint32_t n) {
        std::vector<uint8_t> v(n);
        for (uint32_t k = 0; k < n; ++k) v[k] = b(a + k);
        return v;
    }
    uint32_t vs(uint32_t n) const { return state + n * 0x20; }   // VoiceState[n]: +0 PlayState +4 Start +8 LoopStart
                                                                 // +0xC Frequency +0x10 Volumes +0x14 CurrentPosition
                                                                 // +0x18 TriggerPosition +0x1C Flags

    // ---- event loop ----
    void at(uint64_t t, std::function<void()> fn) { events.emplace(std::make_pair(std::max(t, now), seq++), std::move(fn)); }
    // The clock is the mixer's SPU2 IRQ (§3.1): the mixer runs up to the next event (or t) and stops after the sample
    // that hit the armed IRQA. An IRQ and an event at the same time: the IRQ first.
    void run_until(uint64_t t) {
        for (;;) {
            const uint64_t te = events.empty() ? UINT64_MAX : events.begin()->first.first;
            uint32_t cores = 0;
            const uint64_t ti = mx->run(std::min(te, t), &cores);
            if (cores != 0) {
                now = std::max(now, ti);
                if (irq_hook != nullptr) cores = irq_hook->spu2_irq(now, cores);   // B3ROUTE's handler runs first
                if (cores & 1) next_irq = now + kIrqPeriod;
                if (cores != 0) irq(cores);
                continue;
            }
            if (te > t) break;
            auto it = events.begin();
            auto fn = std::move(it->second);
            now = te;
            events.erase(it);
            fn();
        }
        now = std::max(now, t);
    }
    uint64_t irq_after(uint64_t t, uint64_t n_irqs) const {   // the n-th IRQ at or after t (n >= 1), predicted
        uint64_t first = next_irq;
        while (first < t) first += kIrqPeriod;
        return first + (n_irqs - 1) * kIrqPeriod;
    }

    // ---- replies (irx_000083BC) ----
    void reply_packet(const std::array<uint32_t, 12>& p) {
        ++st.replies;
        pf.reply(p.data());
    }
    void send(uint32_t dst, uint32_t src, uint32_t size, uint32_t op, uint32_t p5, uint32_t a0, uint32_t a1) {
        if (op == 5) {   // IOP-internal sound-RAM write on channel 1, no packet
            transfer(1, dst, src, size, nullptr);
            return;
        }
        if (op == 6) return;   // "rwaRPCDMASRAMTOIOP not implemented", no packet
        if (op == 3) op = 0xB;
        const bool payload = !(op == 4 || op == 7 || op == 8 || op == 0x19) && size != 0;
        uint32_t left = payload ? size : 0, s = src, d = dst;
        do {
            const uint32_t chunk = std::min(left, kMaxChunk);
            std::array<uint32_t, 12> p{};
            p[0] = 0x30 | (chunk << 8);   // SIF header: psize | dsize << 8, dest, cid 1, opt
            p[1] = chunk ? d : 0;
            p[2] = 1;
            p[4] = op;
            p[5] = p5;
            p[6] = src;
            p[7] = dst;
            p[8] = size;
            p[9] = a0;
            p[10] = a1;
            p[11] = chunk == left ? 1 : 0;
            if (chunk) {
                const std::vector<uint8_t> data = copy_out(s, chunk);
                pf.write_ee(d, data.data(), chunk);   // the SIF DMA lands before the EE's handler runs
            }
            reply_packet(p);
            left -= chunk;
            s += chunk;
            d += chunk;
        } while (left);
    }

    // ---- transfers to sound RAM (RWA's queues, irx_00006AB8) ----
    // Logical channel ch; the data is taken now, lands in sound RAM at completion; then channel 1's completion
    // callback (irx_000068D0 -> irx_00007FA8 when set) and `done`.
    void transfer(int ch, uint32_t spu, uint32_t iop_src, uint32_t n, std::function<void()> done) {
        auto data = std::make_shared<std::vector<uint8_t>>(copy_out(iop_src, n));
        uint64_t t = std::max(now, ch_free[ch]);
        if (opt.upload_bytes_per_irq != 0 && n != 0) {
            uint32_t bpi = opt.upload_bytes_per_irq;
            if (dma_locked[0] || dma_locked[1]) bpi = std::max<uint32_t>(1, bpi / 2);   // one DMA channel left
            t = irx_after_bytes(t, n, bpi);
        }
        ch_free[ch] = t;
        at(t, [this, ch, spu, data, done = std::move(done)] {
            mx->voice_trans(now, ch, spu, data->data(), static_cast<uint32_t>(data->size()));
            if (ch == 1) channel1_complete();
            if (done) done();
        });
    }
    uint64_t irx_after_bytes(uint64_t t, uint32_t n, uint32_t bpi) const { return irq_after(t, (n + bpi - 1) / bpi); }

    void channel1_complete() {   // irx_00007FA8 with the packet copy at 0xDFC0
        if (!cb_set) return;
        auto& c = cb_pkt;
        switch (c[4]) {
        case 5:
        case 6: {
            const uint32_t chunk = std::min(c[8], kSpuChunk);
            if (chunk == 0) {
                if (!genuine_pending) {
                    if (!opt.spurious_0d) return;
                    ++st.spurious_0d;
                }
                genuine_pending = false;
                send(0, 0, 0, c[4] == 5 ? 0xD : 0xE, c[5], c[9], c[10]);
            } else {   // the next chunk
                const uint32_t dst = c[7], src = c[6];
                c[7] += chunk;
                c[6] += chunk;
                c[8] -= chunk;
                if (c[4] == 5) transfer(1, dst, src, chunk, nullptr);
                else read_spu_to_iop(src, dst, chunk);
            }
            return;
        }
        case 0xF: {
            std::array<uint32_t, 12> p = c;
            p[0] = 0x30;
            p[2] = 1;
            reply_packet(p);
            return;
        }
        default: return;   // 0x10 after op 8: nothing more
        }
    }
    void read_spu_to_iop(uint32_t spu, uint32_t iop, uint32_t n) {
        const uint8_t* sram = mx->sound_ram();
        for (uint32_t k = 0; k < n; ++k) b(iop + k) = sram[(spu + k) & 0x1FFFFF];
        at(now, [this] { channel1_complete(); });
    }

    // ---- module start (irx_00008C84 -> RwaRPCOpen irx_000074DC, sceSdInit) ----
    void module_start() {
        heap.base = opt.heap_base;
        work_area = heap.alloc(kWorkAreaBytes);   // AllocSysMemory(0, 0x414, 0) [C 0x766C]
        // libsd's own init [C LIBSD 0x16A8-0x17F0, L that sceSdInit(0) takes this path]: every voice to dry and
        // wet, MMIX core 0 = 0x0FF0, core 1 = 0x0FFC, AVOL core 1 = 0x7FFF.
        for (uint16_t c = 0; c < 2; ++c) {
            for (uint16_t s : {sd::S_VMIXL, sd::S_VMIXEL, sd::S_VMIXR, sd::S_VMIXER})
                mx->set_switch(now, static_cast<uint16_t>(s | c), 0xFFFFFF);
            mx->set_param(now, static_cast<uint16_t>(sd::P_MMIX | c), c ? 0x0FFC : 0x0FF0);
        }
        mx->set_param(now, sd::P_AVOLL | 1, 0x7FFF);
        mx->set_param(now, sd::P_AVOLR | 1, 0x7FFF);
        // RwaRPCOpen -> irx_00003E14 -> irx_0000618C(0, 0xAF58, 2) -> 0x5FCC: per core IRQ_ENABLE 0, IRQA, IRQ_ENABLE.
        // Core 0: IRQA 0x800, enabled [C §3.1]; core 1 is left disarmed [G: its table entry is not read; RWA only
        // counts core-1 IRQs].
        mx->set_core_attr(now, sd::C_IRQ_ENABLE | 0, 0);
        mx->set_addr(now, sd::A_IRQA | 0, irqa0);
        mx->set_core_attr(now, sd::C_IRQ_ENABLE | 0, 1);
    }

    // ---- receive / RPC thread ----
    void receive(const uint32_t* pkt) {
        ++st.commands;
        if (rx.size() >= 8) {   // [C irx_00007F04] dropped silently when 8 are pending
            ++st.dropped;
            return;
        }
        std::array<uint32_t, 12> p{};
        std::copy(pkt, pkt + 12, p.begin());
        rx.push_back(p);
        pump();
    }
    void pump() {
        if (pumping) return;
        pumping = true;
        while (!rpc_busy && !rx.empty()) {
            const auto p = rx.front();
            dispatch(p);
            if (!rpc_busy) rx.pop_front();
        }
        pumping = false;
    }
    // The RPC thread sleeps until t, then continues with `cont` (which may block again).
    void block_until(uint64_t t, std::function<void()> cont) {
        rpc_busy = true;
        at(t, [this, cont = std::move(cont)] {
            rpc_busy = false;
            cont();
            if (!rpc_busy) {
                rx.pop_front();
                pump();
            }
        });
    }

    // irx_0000792C
    void dispatch(const std::array<uint32_t, 12>& p) {
        const uint32_t op = p[4];
        switch (op) {
        case 0x32:
            ++st.file_ops;
            file_dispatch(slots + p[9] * kSlotBytes);
            return;
        case 1: send(0, 0, 0, 9, 0, kVersion, 0); return;
        case 2: send(0, 0, 0, 0xA, 0, work_area, slots); return;
        case 3: send(p[7], p[6], p[8], 3, p[5], p[9], p[10]); return;
        case 4:
            if (p[11] != 0) send(0, 0, 0, 0xC, p[5], p[9], p[10]);
            return;
        case 5:
        case 6: {   // sound RAM write (5) / read to IOP (6), chunks of 0x200000; the RPC thread waits for op 5
            const uint32_t chunk = std::min(p[8], kSpuChunk);
            cb_pkt = p;
            cb_pkt[8] -= chunk;
            cb_pkt[7] += chunk;
            cb_pkt[6] += chunk;
            cb_set = true;
            genuine_pending = true;
            if (op == 6) {
                read_spu_to_iop(p[7], p[6], chunk);
                return;
            }
            transfer(1, p[7], p[6], chunk, nullptr);
            // irx_00008804(..., 1, 1): wait for the channel before and after the (first) chunk
            block_until(ch_free[1], [] {});
            return;
        }
        case 7: {   // staged EE -> sound RAM through the 0x414-byte work area [C 0x766C]; unused by B3
            const uint32_t chunk = std::min(p[8], kSpuChunk);
            const uint32_t dst = p[7];
            cb_pkt = p;
            cb_pkt[4] = 0xF;
            cb_pkt[8] -= chunk;
            cb_pkt[7] += chunk;
            cb_pkt[6] += chunk;
            cb_pkt[11] = cb_pkt[8] == 0 ? 1 : 0;
            cb_set = true;
            transfer(1, dst, work_area, chunk, nullptr);
            return;
        }
        case 8: {   // sound RAM -> EE; unused by B3. Done at once; reply 0x10 with the packet copy.
            const uint8_t* sram = mx->sound_ram();
            std::vector<uint8_t> data(p[8]);
            for (uint32_t k = 0; k < p[8]; ++k) data[k] = sram[(p[6] + k) & 0x1FFFFF];
            if (!data.empty()) pf.write_ee(p[7], data.data(), p[8]);
            cb_pkt = p;
            cb_pkt[4] = 0x10;
            cb_set = true;
            std::array<uint32_t, 12> r = cb_pkt;
            r[0] = 0x30;
            r[2] = 1;
            reply_packet(r);
            return;
        }
        default: user(op, p[9], p[10], p[5]); return;
        }
    }

    // irx_000091B0
    void user(uint32_t op, uint32_t a0, uint32_t a1, uint32_t p5) {
        switch (op) {
        case 0x49:   // NULL packet: memset in IOP memory
            for (uint32_t k = 0; k < a1; ++k) b(a0 + k) = 0;
            return;
        case 0x48: reinterleave(a0, a1); return;
        case 0x46: upload_from_iop(p5); return;
        case 0x50:
            dma_locked[a0 & 1] = false;
            send(0, 0, 0, 0x51, p5, 0, 0);
            return;
        case 0x4E: {   // lock: poll every 5334 us until the channel is idle [C irx_000046A8]
            dma_locked[a0 & 1] = true;
            uint64_t t = now;
            while (t < ch_free[0] || t < ch_free[1]) t += kLockPoll;
            if (t == now) {
                send(0, 0, 0, 0x4F, p5, 0, 0);
            } else {
                block_until(t, [this, p5] { send(0, 0, 0, 0x4F, p5, 0, 0); });
            }
            return;
        }
        case 0x4C: resume(); send(0, 0, 0, 0x4D, p5, 0, 0); return;
        case 0x4A: suspended = true; send(0, 0, 0, 0x4B, p5, 0, 0); return;
        case 0x43: return;   // WakeupThread(p5)
        case 0x40: case 0x3E: case 0x3C: case 0x38: case 0x3A: case 0x34: case 0x36:   // 0x36: dead list [C]
            return;
        case 0x2C: send(0, 0, 0, 0x2D, p5, a0, a1); return;
        case 0x2A: send(0, 0, 0, 0x2B, p5, a0, 1); return;
        case 0x30: {
            if (file_table) {   // irx_000007F0: the file thread drains its queue and stops
                while (!fq.empty()) file_run_front();
            }
            file_table = a0 != 0;
            send(0, 0, 0, 0x31, p5, 0, 0);
            if (file_table) file_kick();
            return;
        }
        case 0x29: case 0x27: case 0x23: case 0x21: return;   // WakeupThread(a0[2])
        case 0x26: case 0x20: w32(a0, a1); return;              // *a0 = a1, WakeupThread(a0[2])
        case 0x15:
            reply_addr = a0;
            send(0, 0, 0, 0x1C, 0, state, 0);
            return;
        case 0x17: mixer_off(); return;
        case 0x16: mixer_on(a0); return;
        case 0x14: snapshot(); return;
        case 0x13: send(0, 0, 0, 0x1B, 0, opt.max_free_mem, mem_const); return;
        case 0x12:   // irx_00001AF8: stop the sweep thread and its IRQ client
            sweep_on = false;
            send(0, 0, 0, 0x12, 0, 0, 0);
            return;
        case 0x11: create(a0, a1); return;
        default: ++st.unknown_ops; return;
        }
    }

    // ---- op 0x11 (irx_000017C0, irx_00002E34) §2.1 ----
    void create(uint32_t nv, uint32_t cores) {
        mem_const = 0x100000;
        num_voices = nv;
        rev_cores = cores;
        reply_addr = 0;
        kon[0] = kon[1] = koff[0] = koff[1] = 0;
        std::memset(vmix_cache, 0, sizeof vmix_cache);
        mx->set_param(now, sd::P_MVOLL, 0x3FFF);
        mx->set_param(now, sd::P_MVOLR, 0x3FFF);
        set_gain(0);
        rev_sema_free = true;
        static const uint8_t zeros[0x800] = {};
        mx->voice_trans(now, 0, 0x4000, zeros, 0x800);   // sceSdVoiceTrans(ch, 8 = write by IO): synchronous
        mx->voice_trans(now, 1, 0x4800, zeros, 0x800);
        mx->voice_trans(now, 0, 0x5010, kSilentBlocks, sizeof kSilentBlocks);
        for (uint16_t c = 0; c < 2; ++c) {
            mx->set_param(now, static_cast<uint16_t>(sd::P_BVOLL | c), 0x7FFF);
            mx->set_param(now, static_cast<uint16_t>(sd::P_BVOLR | c), 0x7FFF);
        }
        for (int n = 0; n < 48; ++n) {
            const uint16_t e = sd::voice(n);
            mx->set_param(now, static_cast<uint16_t>(sd::VP_PITCH | e), 0);
            mx->set_addr(now, static_cast<uint16_t>(sd::VA_SSA | e), 0x5000);
            mx->set_addr(now, static_cast<uint16_t>(sd::VA_LSAX | e), 0x5000);
            mx->set_param(now, static_cast<uint16_t>(sd::VP_ADSR1 | e), 0x000F);
            mx->set_param(now, static_cast<uint16_t>(sd::VP_ADSR2 | e), 0x0005);
        }
        sweep_on = true;
        mx->set_addr(now, sd::A_ESA | 0, 0x1C0000);
        mx->set_addr(now, sd::A_EEA | 0, 0x1DFFFF);
        mx->set_addr(now, sd::A_ESA | 1, 0x1E0000);
        mx->set_addr(now, sd::A_EEA | 1, 0x1FFFFF);
        for (uint16_t c = 0; c < 2; ++c) {
            mx->set_param(now, static_cast<uint16_t>(sd::P_EVOLL | c), 0);
            mx->set_param(now, static_cast<uint16_t>(sd::P_EVOLR | c), 0);
        }
        reverb_change(1);
        set_speaker(1);
        set_gain(0xFFFF);
        for (uint32_t n = 0; n < std::min<uint32_t>(nv, 48); ++n) {   // irx_00002E34
            const uint32_t v = vs(n);
            w32(v + 4, 0x5000);
            w32(v + 8, 0x5000);
            w32(v + 0, 3);
            w32(v + 0xC, 0x5622);
            w32(v + 0x1C, 0);
            Hv& h = hv[n];
            h.freq = 0x5622;
            h.flags = 0;
            h.tl = h.tr = 0x7FFF;
            mx->set_addr(now, static_cast<uint16_t>(sd::VA_SSA | h.entry), r32(v + 4));
            set_pitch(h);
            set_volumes(h);
        }
        send(0, 0, 0, 0x1A, 0, 0, 0);   // [C 0x93C4] before the asynchronous reverb clear finishes
    }

    void set_gain(uint32_t g) {   // irx_00002224
        gain_cache = g;
        mx->set_param(now, sd::P_MVOLL | 1, static_cast<uint16_t>(g >> 2));
        mx->set_param(now, sd::P_MVOLR | 1, static_cast<uint16_t>(g >> 2));
    }
    void set_speaker(uint32_t cfg) {   // irx_000028A8
        speaker_cache = cfg;
        mx->set_core_attr(now, sd::C_SPDIF_MODE | 0, cfg == 5 ? 1 : 0);
    }
    void set_revgain(int16_t l, int16_t r) {   // irx_00002810
        revgain_cache[0] = l;
        revgain_cache[1] = r;
        for (uint16_t c = 0; c < 2; ++c) {
            if (!(rev_cores & (1u << c))) continue;
            mx->set_param(now, static_cast<uint16_t>(sd::P_EVOLL | c), static_cast<uint16_t>(l));
            mx->set_param(now, static_cast<uint16_t>(sd::P_EVOLR | c), static_cast<uint16_t>(r));
        }
    }
    // irx_000025A8 §4. The attribute block is written before the semaphore poll, so a change that arrives while a
    // clear runs is not lost: the running clear applies the newest attributes when it ends (irx_00002778 reads
    // +0x6D0 then) [C 0x25A8]; it only gets no work-area clear of its own.
    void reverb_change(uint32_t type) {
        if (!(rev_cores & 3)) return;
        ++st.reverb_changes;
        revtype_cache = r32(state + 0x658);
        rev_attr.mode = static_cast<int32_t>(type) - 1;
        rev_attr.depth_l = revgain_cache[0];
        rev_attr.depth_r = revgain_cache[1];
        rev_attr.delay = static_cast<int32_t>(delay_cache);
        rev_attr.feedback = static_cast<int32_t>(feedback_cache);
        if (!rev_sema_free) {
            ++st.reverb_changes_deferred;
            return;
        }
        rev_sema_free = false;
        ++st.reverb_clears;
        for (uint16_t c = 0; c < 2; ++c) {
            if (!(rev_cores & (1u << c))) continue;
            mx->set_param(now, static_cast<uint16_t>(sd::P_EVOLL | c), 0);
            mx->set_param(now, static_cast<uint16_t>(sd::P_EVOLR | c), 0);
        }
        for (uint16_t c = 0; c < 2; ++c)
            if (rev_cores & (1u << c)) mx->set_core_attr(now, static_cast<uint16_t>(sd::C_EFFECT_ENABLE | c), 0);
        const uint32_t addr = (rev_cores & 1) ? 0x1C0000 : 0x1E0000;
        const uint32_t bytes = (rev_cores & 3) == 3 ? 0x40000 : 0x20000;
        const uint64_t done = opt.reverb_clear_bytes_per_irq ? irx_after_bytes(now, bytes, opt.reverb_clear_bytes_per_irq) : now;
        at(done, [this, addr, bytes] {
            static const std::vector<uint8_t> zeros(0x40000, 0);
            mx->voice_trans(now, 0, addr, zeros.data(), bytes);
            for (int c = 0; c < 2; ++c) {   // irx_00002778
                if (!(rev_cores & (1u << c))) continue;
                mx->set_effect_attr(now, c, rev_attr);
                mx->set_core_attr(now, static_cast<uint16_t>(sd::C_EFFECT_ENABLE | c), rev_attr.mode != -1 ? 1 : 0);
            }
            rev_sema_free = true;
        });
    }

    // ---- op 0x14 §2.2 - 2.4 ----
    void set_pitch(const Hv& h) {   // irx_0000319C: (Frequency << 12) / 48000, signed, 16 bits
        const int32_t f = static_cast<int32_t>(r32(vs(h.n) + 0xC));
        const int32_t sh = static_cast<int32_t>(static_cast<uint32_t>(f) << 12);
        mx->set_param(now, static_cast<uint16_t>(sd::VP_PITCH | h.entry), static_cast<uint16_t>((sh / 48000) & 0xFFFF));
    }
    void set_volumes(Hv& h) {   // irx_0000325C: keyed-on voices, fixed volume, no sweep
        const uint32_t flags = r32(vs(h.n) + 0x1C);
        int32_t v = h.tl;
        h.ll = v;
        h.flags &= ~8u;
        if (flags & 4) { v = -v; h.flags |= 4; } else h.flags &= ~4u;
        mx->set_param(now, static_cast<uint16_t>(sd::VP_VOLL | h.entry), static_cast<uint16_t>((v >> 1) & 0x7FFF));
        v = h.tr;
        h.lr = v;
        h.flags &= ~0x10u;
        if (flags & 2) { v = -v; h.flags |= 2; } else h.flags &= ~2u;
        mx->set_param(now, static_cast<uint16_t>(sd::VP_VOLR | h.entry), static_cast<uint16_t>((v >> 1) & 0x7FFF));
    }
    void mark(uint32_t* masks, const Hv& h) { masks[h.entry & 1] |= 1u << ((h.entry >> 1) & 0x1F); }

    void voice_tick(Hv& h) {   // irx_00002F0C
        const uint32_t v = vs(h.n);
        {   // mac5 diagnostics: B3_RWA_VOICE_TRACE=<n>,<m> prints those voice records when they change
            static int ta = -2, tb = -2;
            if (ta == -2) { ta = tb = -1; if (const char* e = std::getenv("B3_RWA_VOICE_TRACE")) std::sscanf(e, "%d,%d", &ta, &tb); }
            static const uint32_t tstart = [] { const char* e = std::getenv("B3_RWA_VOICE_TRACE_START"); return e ? uint32_t(std::strtoul(e, nullptr, 16)) : 0u; }();
            const bool by_start = tstart && (r32(v + 4) == tstart || r32(v + 4) == tstart + 0x4000);
            if (int(h.n) == ta || int(h.n) == tb || by_start) {
                static uint32_t last[48][8];
                uint32_t cur[8];
                for (int k = 0; k < 8; ++k) cur[k] = r32(v + 4 * k);
                if (std::memcmp(cur, last[h.n], sizeof cur) != 0) {
                    std::memcpy(last[h.n], cur, sizeof cur);
                    std::fprintf(stderr, "rwa voice %u: t %.2f s play %u start 0x%X lsa 0x%X freq 0x%X vol 0x%X pos %u +18 %u flags 0x%X\n", h.n,
                                 double(now) / 48000.0, cur[0], cur[1], cur[2], cur[3], cur[4], cur[5], cur[6], cur[7]);
                }
            }
        }
        uint32_t play = r32(v);
        if (r32(v + 4) == 0 && play == 2) w32(v, play = 1);
        if (play == 2 || play == 1) mark(koff, h);
        const int32_t freq = static_cast<int32_t>(r32(v + 0xC));
        if (freq != h.freq && play != 5) set_pitch(h);
        h.freq = freq;
        const uint32_t vols = r32(v + 0x10);
        h.tl = static_cast<int32_t>(vols) >> 16;
        h.tr = static_cast<int32_t>(vols & 0xFFFF);
        if (play == 2) {   // irx_000030A0
            const uint32_t start = r32(v + 4);
            if (start > 0x500F) {
                const uint32_t trig = r32(v + 0x18);
                mx->set_addr(now, static_cast<uint16_t>(sd::VA_SSA | h.entry), start + (trig << 4) / 28);
                mx->set_param(now, static_cast<uint16_t>(sd::VP_ADSR1 | h.entry), static_cast<uint16_t>((trig ? 0x1B : 0) << 8 | 0xFF));
                mx->set_param(now, static_cast<uint16_t>(sd::VP_ADSR2 | h.entry), 7);
                mark(kon, h);
                if (!(r32(v + 0x1C) & 1)) w32(v + 8, 0x5000);
            }
        } else if (play == 4) {
            if (static_cast<int16_t>(mx->get_param(now, static_cast<uint16_t>(sd::VP_ENVX | h.entry))) == 0) {
                mark(koff, h);
                w32(v, 1);
            } else if (!(r32(v + 0x1C) & 1)) {
                mx->set_addr(now, static_cast<uint16_t>(sd::VA_LSAX | h.entry), 0x5010);
            }
        }
    }

    void write_switch(uint16_t sw, uint32_t* masks) {   // irx_00001B9C (the 42 us spacing never applies [C])
        mx->set_switch(now, sw, masks[0]);
        mx->set_switch(now, static_cast<uint16_t>(sw | 1), masks[1]);
        masks[0] = masks[1] = 0;
    }

    int32_t position(const Hv& h) {   // irx_000031D0
        const uint32_t v = vs(h.n);
        if (r32(v) == 2) return static_cast<int32_t>(r32(v + 0x18));
        int32_t d = static_cast<int32_t>(mx->get_addr(now, static_cast<uint16_t>(sd::VA_NAX | h.entry)) - r32(v + 4));
        if (d < 0) d = 0;
        d = static_cast<int32_t>(static_cast<uint32_t>(d) * 28u);
        if (d < 0) d += 15;
        return d >> 4;
    }

    void snapshot() {
        ++st.snapshots;
        for (uint32_t n = 0; n < std::min<uint32_t>(num_voices, 48); ++n) voice_tick(hv[n]);
        // irx_00001CB8, the masks read once at entry
        const uint32_t k0 = kon[0], k1 = kon[1], f0 = koff[0], f1 = koff[1];
        if (gain_cache != r32(state + 0x660)) set_gain(r32(state + 0x660));
        const int16_t gl = r16s(state + 0x65C), gr = r16s(state + 0x65E);
        if (revgain_cache[0] != gl || revgain_cache[1] != gr) set_revgain(gl, gr);
        if (revtype_cache != r32(state + 0x658) || delay_cache != r32(state + 0x668) || feedback_cache != r32(state + 0x66C)) {
            delay_cache = r32(state + 0x668);
            feedback_cache = r32(state + 0x66C);
            reverb_change(r32(state + 0x658));
        }
        if (speaker_cache != r32(state + 0x664)) set_speaker(r32(state + 0x664));
        static const uint16_t vmix_sw[4] = {sd::S_VMIXL, sd::S_VMIXR, sd::S_VMIXEL, sd::S_VMIXER};   // dry L, dry R, wet L, wet R
        for (uint32_t c = 0; c < 2; ++c) {
            const uint32_t sw = r32(state + 0x650 + 4 * c);
            for (int k = 0; k < 4; ++k) {
                const uint32_t want = r32(state + 0x630 + 8 * k + 4 * c);
                if (vmix_cache[k][c] == want) continue;
                vmix_cache[k][c] = want;
                const uint16_t e = static_cast<uint16_t>(vmix_sw[k] | c);
                mx->set_switch(now, e, want | (~sw & mx->get_switch(now, e)));
            }
        }
        write_switch(sd::S_KOFF, koff);
        if ((f0 & k0) || (f1 & k1)) {   // DelayThread(5800): the RR-7 release of re-triggered voices
            ++st.keyon_delays;
            if (opt.keyon_gap_samples == 0) snapshot_apply(k0, k1, f0, f1);
            else block_until(now + opt.keyon_gap_samples, [this, k0, k1, f0, f1] { snapshot_apply(k0, k1, f0, f1); });
        } else {
            snapshot_apply(k0, k1, f0, f1);
        }
    }

    void snapshot_apply(uint32_t k0, uint32_t k1, uint32_t f0, uint32_t f1) {
        for (uint32_t n = 0; n < 24; ++n) {
            const uint32_t bit = 1u << n;
            if ((f0 & bit) && !(k0 & bit)) {
                w32(vs(n) + 8, 0x5000);
                mx->set_addr(now, static_cast<uint16_t>(sd::VA_LSAX | n << 1), 0x5000);
            }
            if ((f1 & bit) && !(k1 & bit)) {
                w32(vs(n + 24) + 8, 0x5000);
                mx->set_addr(now, static_cast<uint16_t>(sd::VA_LSAX | n << 1 | 1), 0x5000);
            }
        }
        for (uint32_t n = 0; n < 24; ++n)
            if (k0 & (1u << n)) set_volumes(hv[n]);
        for (uint32_t n = 0; n < 24; ++n)
            if (k1 & (1u << n)) set_volumes(hv[n + 24]);
        kon[0] = k0;   // the masks were cleared by nothing since entry; write and clear them
        kon[1] = k1;
        write_switch(sd::S_KON, kon);
        for (uint32_t n = 0; n < std::min<uint32_t>(num_voices, 48); ++n) {   // write-back
            const uint32_t v = vs(n);
            if (r32(v) != 3) w32(v + 0x14, static_cast<uint32_t>(position(hv[n])));
            if (r32(v) == 2) w32(v, 4);
        }
        if (reply_addr != 0) send(reply_addr, state, kStateBytes, 0x14, 0, 0, 0);
        block_until(now + kLoopFixDelay, [this, k0, k1] {   // DelayThread(83), then the loop addresses
            for (uint32_t n = 0; n < 24; ++n) {
                for (uint32_t c = 0; c < 2; ++c) {
                    if (!((c ? k1 : k0) & (1u << n))) continue;
                    const uint32_t v = vs(n + 24 * c);
                    const uint32_t lsa = (r32(v + 0x1C) & 1) ? r32(v + 8) : 0x5010;
                    mx->set_addr(now, static_cast<uint16_t>(sd::VA_LSAX | n << 1 | c), lsa);
                }
            }
        });
    }

    // ---- the IRQ: sweep (§2.5) and the mixer stream (§3.2) ----
    void irq(uint32_t cores) {
        if (!(cores & 1)) return;   // core 1: "Dropped access IRQ" (counted only) [C 0x6274]; never armed here
        // irx_00006274 (0x6418): IRQA alternating 0x800 / 0xA00, IRQ_ENABLE 0 then 1 (re-arm).
        irqa0 = irqa0 == 0x800 ? 0xA00 : 0x800;
        mx->set_addr(now, sd::A_IRQA | 0, irqa0);
        mx->set_core_attr(now, sd::C_IRQ_ENABLE | 0, 0);
        mx->set_core_attr(now, sd::C_IRQ_ENABLE | 0, 1);
        const uint64_t k = irq_index++;
        if (ring != 0) mixer_step();
        // The sweep client (mask 0xAA against a bit rotating 0x80 -> 0x40 -> ... -> 1) [C], phase [G]: even IRQs.
        if (sweep_on && !suspended && ((0x80u >> (k % 8)) & 0xAA)) {
            for (uint32_t n = 0; n < 48; ++n)
                if (b(state + 0x600 + n) != 0) sweep(hv[n]);
        }
    }

    void sweep(Hv& h) {   // irx_00003358
        const uint32_t vflags = r32(vs(h.n) + 0x1C);
        int32_t tl = h.tl, tr = h.tr;
        if ((h.flags & 4) != (vflags & 4) || (h.f24 & 1)) tl = 0;
        if ((h.flags & 2) != (vflags & 2) || (h.f24 & 1)) tr = 0;
        const uint16_t vol_e[2] = {static_cast<uint16_t>(sd::VP_VOLL | h.entry), static_cast<uint16_t>(sd::VP_VOLR | h.entry)};
        const uint16_t volx_e[2] = {static_cast<uint16_t>(sd::VP_VOLXL | h.entry), static_cast<uint16_t>(sd::VP_VOLXR | h.entry)};
        const uint32_t sweep_bit[2] = {8, 0x10}, phase_bit[2] = {4, 2};
        int32_t* last[2] = {&h.ll, &h.lr};
        const int32_t target[2] = {tl, tr};
        for (int s = 0; s < 2; ++s) {
            if (!(h.flags & sweep_bit[s])) continue;
            const int16_t c = static_cast<int16_t>(mx->get_param(now, volx_e[s]));
            int32_t m;
            if (!(static_cast<uint16_t>(c) & 0x8000)) {
                m = c;
                if (h.flags & phase_bit[s]) mx->set_param(now, vol_e[s], static_cast<uint16_t>((-static_cast<int32_t>(c) >> 1) & 0x7FFF));
            } else {
                m = (c == -32768) ? 0x7FFF : static_cast<int16_t>(-c);
                if (!(h.flags & phase_bit[s])) mx->set_param(now, vol_e[s], static_cast<uint16_t>((m >> 1) & 0xFFFF));
            }
            const int32_t t = target[s];
            if (t - m < 0x181 && -0x181 < t - m) {
                *last[s] = t;
                h.flags &= ~sweep_bit[s];
                int32_t v = t;
                if (vflags & phase_bit[s]) { h.flags |= phase_bit[s]; v = -t; } else h.flags &= ~phase_bit[s];
                mx->set_param(now, vol_e[s], static_cast<uint16_t>((v >> 1) & 0x7FFF));
            } else {
                *last[s] = m;
            }
        }
        for (int s = 0; s < 2; ++s) {
            int32_t t = target[s];
            const int32_t d = (t - *last[s]) >> 7;
            if (d == 0) {
                if ((h.flags & phase_bit[s]) != (vflags & phase_bit[s])) {
                    *last[s] = t;
                    h.flags &= ~sweep_bit[s];
                    if (vflags & phase_bit[s]) { t = -t; h.flags |= phase_bit[s]; } else h.flags &= ~phase_bit[s];
                    mx->set_param(now, vol_e[s], static_cast<uint16_t>((t >> 1) & 0x7FFF));
                }
            } else {
                uint32_t u = d < 0 ? (sweep_rate(-d) + 1u) | 0xA000u : sweep_rate(d) | 0x8000u;
                u &= 0xFFFF;
                if (h.flags & phase_bit[s]) u |= 0x1000;
                mx->set_param(now, vol_e[s], static_cast<uint16_t>(u));
                h.flags |= sweep_bit[s];
            }
        }
    }

    // §3.2: one IRQ step = 0x200 bytes left then 0x200 right into MEMIN core 0 (0x4000 / 0x4200, 0x4400 / 0x4600);
    // 0x19 when the cursor reaches the middle or the start: a0 = ring + k * N/2, a1 = N/2, k from 1, flipping.
    void mixer_step() {
        ++st.mixer_ticks;
        const auto l = copy_out(ring + ring_pos, 0x200), r = copy_out(ring + ring_pos + 0x200, 0x200);
        mx->voice_trans(now, 0, 0x4000 + memin_slot * 0x200, l.data(), 0x200);
        mx->voice_trans(now, 0, 0x4400 + memin_slot * 0x200, r.data(), 0x200);
        memin_slot ^= 1;
        ring_pos += 0x400;
        if (ring_pos >= ring_bytes) ring_pos = 0;
        if (ring_pos == 0 || ring_pos == ring_bytes / 2) {   // irx_00001690
            send(0, 0, 0, 0x19, 0, ring + tick_half * (ring_bytes >> 1), ring_bytes >> 1);
            tick_half = 1 - tick_half;
        }
    }
    void mixer_on(uint32_t n) {   // irx_00002C90
        if ((n & 0x3FF) != 0 || ring != 0 || n == 0) return;
        ring_alloc = heap.alloc(n);
        if (ring_alloc == 0) return;
        ring = (ring_alloc + 0x7F) & ~0x7Fu;
        ring_bytes = n;
        ring_pos = 0;
        memin_slot = 0;
        for (uint32_t k = 0; k < n; ++k) b(ring + k) = 0;
    }
    void mixer_off() {   // irx_00002D48
        if (ring == 0) {
            if (opt.always_answer_0x17) send(0, 0, 0, 0x1D, 0, 0, 0);
            return;
        }
        heap.free(ring_alloc);
        ring = ring_alloc = ring_bytes = 0;
        // memset(0xBC00, 0, 0x800) and two internal op-5 writes from it (channel 1)
        const uint32_t zero_buf = opt.module_base + 0xBC00;
        for (uint32_t k = 0; k < 0x800; ++k) b(zero_buf + k) = 0;
        send(0x4000, zero_buf, 0x800, 5, 0, 0, 0);
        send(0x4800, zero_buf, 0x800, 5, 0, 0, 0);
        // the 0x1D goes after the writes are queued; with the immediate model they complete first
        at(ch_free[1], [this] { send(0, 0, 0, 0x1D, 0, 0, 0); });
        block_until(ch_free[1], [] {});
    }

    void resume() {   // op 0x4C [G: register save/restore not modelled]: bump every cache so the next snapshot rewrites
        suspended = false;
        ++gain_cache;
        ++revtype_cache;
        ++delay_cache;
        ++feedback_cache;
        ++speaker_cache;
        ++revgain_cache[0];
        ++revgain_cache[1];
        for (auto& k : vmix_cache) { ++k[0]; ++k[1]; }
        for (Hv& h : hv) ++h.freq;
    }

    // ---- op 0x46 (irx_00009F94) §5 ----
    void upload_from_iop(uint32_t p5) {
        const uint32_t prm = work_area + 0x400;
        const uint32_t o = r32(prm), dst = r32(prm + 4), s = r32(prm + 8), bytes = r32(prm + 12), t = r32(prm + 16);
        const uint32_t e = o + bytes;
        const bool first = (o < 2) && (e != 0) && b(s + 1) == 0;
        b(s + 1) = 0;
        b(s + bytes - 15) = 0;
        if (o < 2 && e != 0) b(s + 1 - o) = 6;
        const uint32_t u4 = t - 15;
        if (o <= u4 && u4 <= e) b(s + u4 - o) = 3;
        const uint32_t u6 = t - 31;
        if (first && o <= u6 && u6 <= e) b(s + u6 - o) = 3;
        if (t > bytes) b3audio::spu2_mark_stream_ring(dst - o, t);   // mac5 diagnostics: a ring longer than this piece
        transfer(1, dst, s, (bytes + 0x3F) & ~0x3Fu, nullptr);
        block_until(ch_free[1], [this, p5] { send(0, 0, 0, 0x47, p5, 0, 0); });
    }

    // ---- op 0x48 (irx_0000A0A8) §7.2 ----
    void reinterleave(uint32_t table, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t rec = table + i * 0x24;
            const uint32_t reps = r32(rec);
            for (uint32_t k = 0; k < reps; ++k) {
                const uint32_t blk = r32(rec + 12), step = r32(rec + 16), chunk = r32(rec + 32);
                uint32_t src = r32(rec + 4), dst = r32(rec + 8);
                uint32_t a = blk - r32(rec + 20), bb = blk - r32(rec + 24), n = r32(rec + 28) & 0xFFFF;
                while (n != 0) {
                    uint32_t ua = a & 0xFFFF, ub = bb & 0xFFFF;
                    if (!ua) { src += step; ua = blk & 0xFFFF; }
                    if (!ub) { dst += step; ub = blk & 0xFFFF; }
                    const uint32_t words = chunk >> 2;   // [G] 0 words: retail's Duff loop would run ~2^32 times
                    for (uint32_t w = 0; w < words; ++w) w32(dst + 4 * w, r32(src + 4 * w));
                    a = ua - (chunk & 0xFFFF);
                    bb = ub - (chunk & 0xFFFF);
                    n = (n - chunk) & 0xFFFF;
                    src += chunk;
                    dst += chunk;
                    if (words == 0 && (chunk & 0xFFFF) == 0) break;
                }
                w32(rec + 4, r32(rec + 4) + blk);
                w32(rec + 8, r32(rec + 8) + blk);
            }
        }
    }

    // ---- remote file ops §6 (irx_00009E14, irx_00009CCC, the file thread irx_000003EC) ----
    uint32_t slot_index(uint32_t req) { return r32(req) & 3; }   // [G] retail indexes its 3-entry table unchecked
    void file_dispatch(uint32_t req) {   // irx_00009E14
        const uint32_t idx = slot_index(req);
        w32(slots + idx * kSlotBytes + 0x1C, r32(slots + idx * kSlotBytes + 0x1C) | 0x20);
        std::shared_ptr<FileObj> obj = slot_file[idx];
        if (!obj) {   // implicit open of the slot's name, then this request again
            FileOp op;
            op.kind = 1;
            op.obj = std::make_shared<FileObj>();
            op.req = req;
            char name[0x4D] = {};
            for (uint32_t k = 0; k < 0x4C; ++k) name[k] = static_cast<char>(b(req + 0x24 + k));
            op.name = name;
            file_queue(std::move(op));
            return;
        }
        switch (r32(req + 0x10)) {
        case 1: {   // close: synchronous, drops the file's queued operations (irx_00001100)
            fq.erase(std::remove_if(fq.begin(), fq.end(), [&](const FileOp& f) { return f.obj == obj; }), fq.end());
            if (!obj->failed) pf.file_close(obj->handle);
            slot_file[idx] = nullptr;
            file_done(nullptr, 0, 0, 3, req);
            return;
        }
        case 2: file_queue({4, obj, req, {}, staging, r32(req + 8), 0}); return;
        case 4: file_queue({3, obj, req, {}, 0, 0, r32(req + 0xC)}); return;
        case 3:
            if (r32(req + 0x1C) & 8) file_queue({3, obj, req, {}, 0, 0, r32(req + 0xC)});
            else file_queue({4, obj, req, {}, r32(req + 4), (r32(req + 8) + 0x7FF) & ~0x7FFu, 0});
            return;
        default: return;
        }
    }
    void file_queue(FileOp op) {
        if (!file_table) ++st.file_ops_without_table;
        fq.push_back(std::move(op));
        file_kick();
    }
    void file_kick() {
        if (file_busy || fq.empty() || !file_table) return;
        file_busy = true;
        at(now + opt.file_latency_samples, [this] {
            file_busy = false;
            if (!fq.empty()) file_run_front();
            file_kick();
        });
    }
    // mac4: B3_RWA_FILE_LOG=1 prints every remote file operation with its IOP time (samples), slot, position and size
    // (the music stream's reads: a stalled or repeating stream shows as reads that stop or go back)
    static bool file_log() { static const bool on = [] { const char* e = std::getenv("B3_RWA_FILE_LOG"); return e && *e == '1'; }(); return on; }
    void file_run_front() {
        FileOp op = std::move(fq.front());
        fq.pop_front();
        FileObj& f = *op.obj;
        if (file_log()) {
            std::printf("rwa file: t %llu slot %u kind %d%s%s pos %u size %u handle %d at %u\n", static_cast<unsigned long long>(now),
                        slot_index(op.req), op.kind, op.kind == 1 ? " open " : "", op.kind == 1 ? op.name.c_str() : "", op.pos,
                        op.size, f.handle, f.cursor);
            std::fflush(stdout);
        }
        switch (op.kind) {
        case 1: {
            const int h = pf.file_open(op.name.c_str());
            if (h <= 0) {   // the object is deleted after the callback stored it: a retail bug; we keep it, failed
                ++st.file_open_failures;
                f.failed = true;
                file_done(op.obj, 0, 2, 2, op.req);
            } else {
                f.handle = h;
                file_done(op.obj, 0, 0, 2, op.req);
            }
            return;
        }
        case 3: {
            const int r = f.failed ? -1 : pf.file_seek(f.handle, op.pos);
            if (r == 0) f.cursor = op.pos;
            if (r == 0) file_done(op.obj, op.pos, 0, 4, op.req);
            else file_done(op.obj, 0, 4, 4, op.req);
            return;
        }
        case 4: {
            int got = 0;
            if (!f.failed) {
                std::vector<uint8_t> tmp(op.size);
                // [C 0x3EC] retried while negative; bounded here [G] so a broken back end cannot hang the host
                for (int tries = 0; tries < 1000; ++tries) {
                    got = pf.file_read(f.handle, tmp.data(), op.size);
                    if (got >= 0) break;
                }
                if (got < 0) got = 0;
                f.cursor += static_cast<uint32_t>(got);
                copy_in(op.buf, tmp.data(), static_cast<uint32_t>(got));
            }
            if (static_cast<uint32_t>(got) != op.size) ++st.file_short_reads;
            file_done(op.obj, static_cast<uint32_t>(got), 0, 5, op.req);
            return;
        }
        default: return;
        }
    }
    // irx_00009CCC(obj, bytes, status, kind, req): kind 2 open done, 3 close, 4 seek done, 5 read done, 1 alloc fail
    void file_done(const std::shared_ptr<FileObj>& obj, uint32_t bytes, uint32_t status, int kind, uint32_t req) {
        const uint32_t idx = slot_index(req);
        const uint32_t flags_at = slots + idx * kSlotBytes + 0x1C;
        w32(flags_at, r32(flags_at) & ~0x20u);
        uint32_t a0 = status;
        if (kind == 4) {
            a0 = bytes;
            if (r32(req + 0x10) == 3) {   // the seek before an op-3 read (flag 8): clear it, read next
                w32(req + 0x1C, r32(req + 0x1C) & ~8u);
                file_dispatch(req);
                return;
            }
        } else if (kind == 5) {
            a0 = r32(req + 8);   // the requested size, even on a short read [C 0x9D68]
            if (r32(req + 0x10) == 2) {
                send(r32(req + 4), staging, a0, 0x33, r32(req + 0x14), a0, idx);
                return;
            }
        } else if (kind == 2) {
            slot_file[idx] = obj;
            file_dispatch(req);
            return;
        } else if (kind == 3) {
            a0 = 0;
        }
        send(0, 0, 0, 0x33, r32(req + 0x14), a0, idx);
    }
};

RwaIop::RwaIop(RwaIopPlatform& p) : impl_(std::make_unique<Impl>(p, nullptr, RwaIopOptions{})) {}
RwaIop::RwaIop(RwaIopPlatform& p, b3audio::Spu2Mixer& mixer, const RwaIopOptions& opt)
    : impl_(std::make_unique<Impl>(p, &mixer, opt)) {}
RwaIop::RwaIop(RwaIopPlatform& p, const RwaIopOptions& opt) : impl_(std::make_unique<Impl>(p, nullptr, opt)) {}
RwaIop::~RwaIop() = default;

void RwaIop::send_cmd(const uint32_t packet[12], uint32_t src_ee, uint32_t dst_iop, uint32_t size) {
    Impl& m = *impl_;
    if (size != 0 && src_ee != 0) {   // the SIF DMA part lands at its IOP destination before the handler runs
        std::vector<uint8_t> data(size);
        m.pf.read_ee(src_ee, data.data(), size);
        m.copy_in(dst_iop, data.data(), size);
    }
    m.receive(packet);
    m.run_until(m.now);   // whatever the command started at this instant (file ops, immediate completions)
}

void RwaIop::advance(uint32_t samples) { impl_->run_until(impl_->now + samples); }
uint64_t RwaIop::now() const { return impl_->now; }
// boot2 (sync1: a reply the executor schedules a few samples ahead arrived a whole VBlank late): the sample time of the
// executor's next own event -- its event queue's front or the next SPU2 IRQ -- for the platform's virtual clock
uint64_t RwaIop::next_event_time() const {
    const uint64_t te = impl_->events.empty() ? UINT64_MAX : impl_->events.begin()->first.first;
    return std::min(te, impl_->next_irq);
}
uint8_t* RwaIop::iop_ram() { return impl_->ram.data(); }
b3audio::Spu2Mixer& RwaIop::mixer() { return *impl_->mx; }
const RwaIopStats& RwaIop::stats() const { return impl_->st; }
const RwaIopOptions& RwaIop::options() const { return impl_->opt; }
void RwaIop::set_spu2_irq_hook(Spu2IrqHook* hook) { impl_->irq_hook = hook; }
bool RwaIop::dma_channel_idle(int ch) const { return (ch & 1) == ch && impl_->ch_free[ch] <= impl_->now; }

}  // namespace b3native
