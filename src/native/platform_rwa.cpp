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
#include "../audio/spu2_mixer_impl.h"
#include "../audio/spu2_stub_mixer.h"

#include "b3asset/discfs.h"
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
void rwa_catch_up() {
    if (!g_rwa) return;
    const std::uint64_t c = virtual_cycles();
    const std::uint64_t t = g_anchor_samples + (c > g_anchor_cycles ? (c - g_anchor_cycles) / 6144u : 0u);
    while (g_rwa->now() < t) {
        const std::uint64_t d = t - g_rwa->now();
        g_rwa->advance(static_cast<std::uint32_t>(d > 0x10000000u ? 0x10000000u : d));
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
        }
        rwa_catch_up();
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
            return t <= g_anchor_samples ? g_anchor_cycles : g_anchor_cycles + (t - g_anchor_samples) * 6144u;
        },
        [] { rwa_catch_up(); });
    add_vblank_hook([] {
        if (!g_rwa) return;
        rwa_catch_up();
        std::int16_t buf[2 * 2048];
        for (std::size_t n; (n = g_mixer->read_output(buf, 2048)) > 0;) {
            if (g_music_overlay) g_music_overlay(buf, n);   // the custom soundtrack (custom_music.h); headless too
            if (g_sink) g_sink->push(buf, n);
        }
    });
}

void set_music_overlay(void (*mix)(std::int16_t* lr, std::size_t frames)) { g_music_overlay = mix; }

}  // namespace b3native::platform
