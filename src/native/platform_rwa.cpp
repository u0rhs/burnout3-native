// The native platform's side of RW Audio's IOP executor (boot2 with audio1, 2026-11-06; src/native/rwa_iop.h, audio1's
// src/audio/rwa_iop.cpp): RWA's EE library sends SIF command 0 (0x30-byte packets, sceSifSendCmd) to RWA.IRX on the
// IOP; here they go to the executor, its IOP->EE command-1 replies are queued for the handler the EE registered
// (_RwaRPCSifIntHandler, through sceSifAddCmdHandler(1)) and delivered at the platform's next safe point in interrupt
// context (platform_sif sif_service), its remote-file requests read the disc by name (platform_disc.h), and IOP time
// advances with virtual EE time: one VBlank (4,915,200 EE cycles) = 800 samples at 48 kHz (294.912 MHz / 48 kHz =
// 6,144 cycles per sample).
#include "platform.h"
#include "audio_sink.h"
#include "platform_disc.h"
#include "platform_sif.h"
#include "rwa_iop.h"
#include "../audio/b3route.h"
#include "../audio/spu2_mixer_impl.h"
#include "../audio/spu2_stub_mixer.h"

#include "b3asset/discfs.h"
#include "b3host/call.h"
#include "b3host/host.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <memory>
#include <string>

namespace b3native::platform {
namespace {

std::unique_ptr<b3audio::Spu2Mixer> g_mixer;
// mac5 (the Mac's looping DJ voice-over and music): sound-RAM uploads take the IRX's time, 0x18C0 bytes less 0x80 a
// job per channel per IRQ wake-up [docs/audio-irx.md §5], instead of completing at once. With instant uploads the
// 0x47 completion of an op-0x46 upload was ready during the send itself, and a reply delivery point inside
// RwaRPCTransfer (time_check at a clock boundary, the semaphore wait) could run its callback func_002B52C0 (pending = 0)
// before _RwaSPU2WaveUploadFromIOP set _rwaGRwaSPU2WaveIOPUploadPending = 1 after the send [C 0x2B51F0]: the flag
// stuck at 1, every wave renderer's upload waited for ever (_RwaWaveRendererFinishExecute: busy), the stream voices
// looped their rings (replay of mac-report 6d8f0f07: the DJ stream from VBlank 18476, 57 s). On a PS2 the upload
// takes milliseconds, so the flag is always set first. B3_RWA_INSTANT_UPLOAD=1 restores the instant model.
RwaIopOptions product_rwa_options() {
    RwaIopOptions o{};
    const char* e = std::getenv("B3_RWA_INSTANT_UPLOAD");
    if (!(e && *e && *e != '0')) o.upload_bytes_per_irq = 0x18C0 - 0x80;
    return o;
}
std::unique_ptr<RwaIop> g_rwa;
// boot2 (2026-11-07, the restore's 3040 transient): IOP / SPU2 time follows virtual EE time continuously, one sample
// per 6,144 EE cycles (every audio timeline derives from the EE cycle counter), from an anchor (the executor's
// creation); before, 800 samples were added at each VBlank, so a command sent mid-frame saw stream positions up to a
// frame old (RWA's EE refills came a poll late)
std::uint64_t g_anchor_cycles = 0, g_anchor_samples = 0;
// safept (2026-11-18): where a catch-up runs. The clock's time check (the executor's time source) and a VBlank are where
// the port delivers every interrupt, so B3ROUTE's command 6 may be delivered there too (when sif::can_deliver_now).
// Inside a platform row the game called (its cid-0 send, the distortion RPC) the IOP advances, but no handler runs.
enum class At { time_check, vblank, row };
bool route_on();
bool route_held();
bool route_deliver(At at, std::uint64_t iop_target);
constexpr std::uint64_t kRouteSlice = 64;   // samples: B3ROUTE's phases are 128 apart (engfix, RoutePlatform below)
void rwa_catch_up(At at) {
    if (!g_rwa) return;
    // engfix: not again from inside the catch-up (B3ROUTE's command-6 handler runs between slices, and its calls reach
    // the clock's time checks)
    static bool busy = false;
    if (busy) return;
    busy = true;
    struct Done { ~Done() { busy = false; } } done;
    const std::uint64_t c = virtual_cycles();
    const std::uint64_t t = g_anchor_samples + (c > g_anchor_cycles ? (c - g_anchor_cycles) / 6144u : 0u);
    const std::uint64_t slice = route_on() ? kRouteSlice : 0x10000000u;
    for (;;) {
        // safept: a held command 6 is answered here, between slices, or the IOP waits at this boundary for a point
        // that can deliver it (the time source is due at once meanwhile)
        if (route_held() && !route_deliver(at, t)) return;
        if (g_rwa->now() >= t) return;
        const std::uint64_t d = t - g_rwa->now();
        g_rwa->advance(static_cast<std::uint32_t>(d > slice ? slice : d));
    }
}
std::FILE* rwa_log() {
    static std::FILE* f = [] { const char* e = std::getenv("B3NATIVE_RWA_LOG"); return e && *e ? std::fopen(e, "w") : nullptr; }();
    return f;
}

struct Platform final : RwaIopPlatform {
    void read_ee(std::uint32_t a, void* dst, std::uint32_t n) override {
        std::memcpy(dst, b3host::host_pointer(a & 0x1FFFFFFFu, n), n);
    }
    void write_ee(std::uint32_t a, const void* src, std::uint32_t n) override {
        if (n == 0x680u)
            if (std::FILE* f = rwa_log()) {   // the 0x14 reply area (the siflog's A lines)
                std::fprintf(f, "A %llu ", (unsigned long long)log_poll);
                for (std::uint32_t k = 0; k < n; ++k) std::fprintf(f, "%02x", static_cast<const std::uint8_t*>(src)[k]);
                std::fprintf(f, "\n");
            }
        std::memcpy(b3host::host_pointer(a & 0x1FFFFFFFu, n), src, n);
    }
    void reply(const std::uint32_t packet[12]) override {
        if (std::FILE* f = rwa_log())   // sync1: v= VBlank, c= virtual EE cycles (as the siflog's c=)
            std::fprintf(f, "U %llu %x %x %x t=%llu v=%llu c=%llu\n", (unsigned long long)log_poll, packet[4], packet[9], packet[10],
                         (unsigned long long)(g_rwa ? g_rwa->now() : 0), (unsigned long long)kernel().vblanks,
                         (unsigned long long)virtual_cycles());
        sif::queue_cmd_reply(1, packet, 0x30);
    }
    // the executor's handles are > 0; the disc's from 0
    int file_open(const char* name) override {
        const int h = disc::open(name);
        return h < 0 ? -1 : h + 1;
    }
    int file_read(int h, void* dst, std::uint32_t n) override {
        return static_cast<int>(disc::read(h - 1, dst, n));
    }
    int file_seek(int h, std::uint32_t pos) override { return disc::seek(h - 1, pos, 0) < 0 ? -1 : 0; }
    void file_close(int h) override { disc::close(h - 1); }
};

Platform g_platform;
b3audio::AudioSink* g_sink = nullptr;

// engpan (2026-11-18): B3ROUTE.IRX, Criterion's distortion router (docs/audio-b3route.md, src/audio/b3route.h), beside
// the executor as on a PS2 (loaded right after RWA.IRX). CB3SoundESMHigh::Start sends the player car's engine-model
// voices (SetChannels' mask) to core 0's wet bus only; B3ROUTE hands each 256-sample block of that bus to the EE (SIF
// command 6), whose handler (func_002FC530, platform_distortion.cpp) runs the two DSP blocks -- the left channel through
// one, the right through the other -- and mixes them to one mono signal panned by the car's gains (func_002FC820), and
// B3ROUTE plays the result through core 1's sound-data input. Without it (the port before engpan) those voices played
// dry with the engine model's channel pans: the hard-left and hard-right layers apart, bass on one side and the mids /
// highs on the other.
//
// engfix (2026-11-18): the EE's answer must be in B3ROUTE's Fuzzy buffer before the next B / D phase, 128 samples after
// the command (docs/audio-b3route.md §9). Queued for the next safe point (as engpan first did) it often was not: the IOP
// catches up hundreds of samples at a time (the VBlank hook after an idle jump to the VBlank, a command's catch-up), so
// several phases ran in one advance with no EE in between -- 44 % of the Fuzzy writes repeated the previous frame, and
// a second Clean block overwrote the first before the handler read it (blocks dropped and repeated: the engine quiet,
// crackling at the 128-sample seams). Now the command is held here and delivered between IOP slices of at most
// kRouteSlice samples (rwa_catch_up), so every block is answered before its B / D phase.
//
// safept (2026-11-18): engfix delivered it wherever the catch-up ran -- also inside the game's cid-0 send and the
// distortion RPC rows, from the time source with interrupts disabled, and inside another handler -- so the handler and
// its generated helpers could run nested where no interrupt is ever delivered (race: 127 times inside the thread
// manager's VBlank handler, interrupts off). Now only at a point that delivers interrupts (a time check or a VBlank,
// sif::can_deliver_now) and while no ptr_arg window is open on the thread (b3host/call.h PtrArg: host-stack bytes
// copied onto the guest stack and written back after the call; race: 226 deliveries inside the car physics,
// CollideWheels / DoWheelLineCollision, had one open). Elsewhere the IOP stops at the slice boundary after the send and
// waits there (race: 8 samples at most, never past kRouteMaxWait), so the answer still lands at the same IOP time: the
// sound is byte-identical to engfix's (core 0's wet bus and core 1's MEMIN over a whole race).
constexpr std::uint64_t kRouteMaxWait = 800;   // samples (a VBlank): past it, the command is queued and the IOP goes on
struct RoutePlatform final : B3RoutePlatform {
    void write_ee(std::uint32_t a, const void* src, std::uint32_t n) override { b3host::copy_to_guest(a, src, n); }
    void send_cmd(const std::uint32_t* packet, std::uint32_t size) override {
        if (held) late();   // two sends with no catch-up boundary between them (cannot happen with 64-sample slices)
        std::memcpy(pkt, packet, std::min<std::uint32_t>(size, sizeof pkt));
        pkt_size = std::min<std::uint32_t>(size, sizeof pkt);
        held = true;
    }
    // libsif's dispatch of the command, in interrupt context: now, at a point that delivers interrupts (no handler
    // registered, or the channel closed: queued, and dropped there with a note, as before)
    bool deliver(At at, std::uint64_t iop_target) {
        if (at != At::row && sif::can_deliver_now() && b3host::detail::t_bounce_n == 0) {
            held = false;
            ++(at == At::vblank ? stats.at_vblank : stats.at_time_check);
            if (!sif::deliver_cmd_now(6, pkt, pkt_size)) sif::queue_cmd_reply(6, pkt, pkt_size);
            return true;
        }
        const std::uint64_t wait = iop_target > g_rwa->now() ? iop_target - g_rwa->now() : 0;
        if (wait > stats.max_wait) stats.max_wait = wait;
        if (wait <= kRouteMaxWait) {
            ++(at == At::row ? stats.waits_row : stats.waits_point);
            return false;
        }
        late();
        return true;
    }
    void late() {   // the old way (engpan): at the next safe point, after the IOP has gone on (a repeated frame)
        held = false;
        ++stats.late;
        sif::queue_cmd_reply(6, pkt, pkt_size);
    }
    std::uint32_t pkt[8] = {};
    std::uint32_t pkt_size = 0;
    bool held = false;
    struct {   // safept: printed with B3_B3ROUTE_STATS=1 (print_route_stats)
        std::uint64_t at_time_check = 0, at_vblank = 0, waits_point = 0, waits_row = 0, late = 0, max_wait = 0;
    } stats;
};
RoutePlatform g_route_platform;
std::unique_ptr<B3Route> g_route;
std::vector<std::vector<std::uint8_t>> g_route_early;   // command lists sent before the executor exists
bool route_enabled() {
    static const bool on = [] { const char* e = std::getenv("B3_B3ROUTE"); return !(e && *e == '0'); }();
    return on;
}
bool route_on() { return g_route != nullptr; }
bool route_held() { return g_route_platform.held; }
bool route_deliver(At at, std::uint64_t iop_target) { return g_route_platform.deliver(at, iop_target); }
void route_call(const std::uint8_t* list, std::uint32_t bytes) {
    g_route->call(B3Route::kRpcId, list, bytes, nullptr, 0);
}
// safept: B3_B3ROUTE_STATS=1 prints the module's counts and where command 6 was delivered, at exit (stderr)
void print_route_stats() {
    if (!g_route) return;
    const B3RouteStats& r = g_route->stats();
    const auto& p = g_route_platform.stats;
    std::fprintf(stderr,
                 "B3ROUTE sends %llu ee_dmas %llu writes %llu fresh %llu stale %llu skipped_busy %llu | command 6 "
                 "delivered at time checks %llu, VBlanks %llu; waits at a point %llu, in a row %llu; late %llu; max "
                 "wait %llu samples\n",
                 (unsigned long long)r.sends, (unsigned long long)r.ee_dmas, (unsigned long long)r.writes,
                 (unsigned long long)r.writes_fresh, (unsigned long long)r.writes_stale,
                 (unsigned long long)r.skipped_busy, (unsigned long long)p.at_time_check,
                 (unsigned long long)p.at_vblank, (unsigned long long)p.waits_point, (unsigned long long)p.waits_row,
                 (unsigned long long)p.late, (unsigned long long)p.max_wait);
}
// the executor's creation (its first command): B3ROUTE hooks its SPU2 interrupt, then takes any earlier lists
void create_route() {
    if (!route_enabled() || g_route) return;
    if (const char* e = std::getenv("B3_B3ROUTE_STATS"); e && *e == '1') std::atexit(print_route_stats);
    g_route = std::make_unique<B3Route>(g_route_platform, *g_rwa);
    for (const auto& l : g_route_early) route_call(l.data(), static_cast<std::uint32_t>(l.size()));
    g_route_early.clear();
}
void (*g_music_overlay)(std::int16_t*, std::size_t) = nullptr;   // set_music_overlay


}  // namespace

void install_rwa_iop(b3audio::AudioSink* sink) {
    g_sink = sink;
    // boot2 (2026-11-07): the SPU2-equivalent mixer by default -- the stub answered ENVX / NAX reads (RW Audio's voice
    // state, which the EE reads back through replies) unlike an SPU2, so the mixer is part of the game's behaviour,
    // the same with or without a sink. The reverb presets come from the user's LIBSD.IRX (never in the repo).
    const char* m = std::getenv("B3NATIVE_MIXER");
    if (m && std::string(m) == "stub") {
        g_mixer = std::make_unique<b3audio::Spu2StubMixer>();
        stub_note("RW Audio mixer", "B3NATIVE_MIXER=stub: the stub mixer (positions and the clock, no sound)");
    } else {
        g_mixer = std::make_unique<b3audio::Spu2MixerImpl>();
        const std::string libsd = disc::root() + "/IOP/LIBSD.IRX";
        std::vector<std::uint8_t> irx;   // iso1: from the disc folder or the disc image (b3asset/discfs.h)
        if (!b3asset::disc::read_file(libsd, irx) || !b3audio::load_libsd_reverb_presets_bytes(irx))
            stub_note("RW Audio mixer", "libsd reverb presets not loaded from " + libsd + ": reverb silent");
    }
    sif::set_cmd_sink(0, [](const sif::SentCmd& c) {
        if (!g_rwa) {
            g_rwa = std::make_unique<RwaIop>(g_platform, *g_mixer, product_rwa_options());
            g_anchor_cycles = virtual_cycles();
            g_anchor_samples = 0;
            create_route();
        }
        rwa_catch_up(At::row);
        std::uint32_t pkt[12];
        std::memcpy(pkt, b3host::host_pointer(c.packet, 0x30), 0x30);
        if (std::FILE* f = rwa_log()) {
            std::fprintf(f, "S %llu %x t=%llu v=%llu c=%llu ", (unsigned long long)log_poll, pkt[4], (unsigned long long)g_rwa->now(), (unsigned long long)kernel().vblanks, (unsigned long long)virtual_cycles());
            for (int k = 0; k < 12; ++k) std::fprintf(f, "%08x", pkt[k]);
            std::fprintf(f, " ");
            if (c.extra && c.extra_size && c.extra_size <= 0x1000u)
                if (const auto* pl = static_cast<const std::uint8_t*>(b3host::host_pointer(c.extra & 0x1FFFFFFFu, c.extra_size)))
                    for (std::uint32_t k = 0; k < c.extra_size; ++k) std::fprintf(f, "%02x", pl[k]);
            std::fprintf(f, "\n");
        }
        g_rwa->send_cmd(pkt, c.extra, c.extra_dest, c.extra_size);
    });
    // boot2 (sync1's case: a 0x33 due 4 samples after its command came a VBlank late): the executor's own next event
    // armed on the virtual clock, so it runs at its sample time, not at the next command or VBlank
    set_extra_time_source(
        [] {
            if (!g_rwa) return ~std::uint64_t{0};
            const std::uint64_t t = g_rwa->next_event_time();
            if (t == ~std::uint64_t{0}) return ~std::uint64_t{0};
            if (route_held()) return std::uint64_t{0};   // safept: a held command 6: due at the next time check
            return t <= g_anchor_samples ? g_anchor_cycles : g_anchor_cycles + (t - g_anchor_samples) * 6144u;
        },
        [] { rwa_catch_up(At::time_check); });
    add_vblank_hook([] {
        if (!g_rwa) return;
        rwa_catch_up(At::vblank);
        std::int16_t buf[2 * 2048];
        for (std::size_t n; (n = g_mixer->read_output(buf, 2048)) > 0;) {
            if (g_music_overlay) g_music_overlay(buf, n);   // the custom soundtrack (custom_music.h); headless too
            if (g_sink) g_sink->push(buf, n);
        }
    });
}

void b3route_rpc(const void* list, std::uint32_t bytes) {
    if (!route_enabled()) return;
    const auto* p = static_cast<const std::uint8_t*>(list);
    if (!g_route) {
        g_route_early.emplace_back(p, p + bytes);
        return;
    }
    rwa_catch_up(At::row);
    route_call(p, bytes);
}

bool b3route_sif_dma(std::uint32_t iop_addr, const void* src, std::uint32_t n) {
    return g_route && g_route->sif_dma_to_iop(iop_addr, src, n);
}

void set_music_overlay(void (*mix)(std::int16_t* lr, std::size_t frames)) { g_music_overlay = mix; }

}  // namespace b3native::platform
