// B3ROUTE.IRX as host C++ (src/audio/b3route.cpp, spec docs/audio-b3route.md) on synthetic sequences: the RPC command
// list and its libsd writes, the four-phase core-1 IRQ cycle beside RWA's core-0 clock, the Clean reads and command-6
// sends, the Fuzzy writes into core 1's MEMIN, and (with the SPU2-equivalent mixer and a stand-in for the EE's
// command-6 handler that follows func_002FC530's buffer logic) the end-to-end path with its 1024-sample latency.
// No disc data needed.
#include "test.h"

#include "../src/audio/b3route.h"
#include "../src/audio/spu2_mixer_impl.h"
#include "../src/audio/spu2_stub_mixer.h"

#include <algorithm>
#include <functional>
#include <map>
#include <vector>

namespace {

using b3native::B3Route;
using b3native::RwaIop;
namespace sd = b3audio::sd;

struct RwaFake final : b3native::RwaIopPlatform {
    void read_ee(uint32_t, void* d, uint32_t n) override { std::memset(d, 0, n); }
    void write_ee(uint32_t, const void*, uint32_t) override {}
    void reply(const uint32_t*) override {}
    int file_open(const char*) override { return 0; }
    int file_read(int, void*, uint32_t) override { return 0; }
    int file_seek(int, uint32_t) override { return 0; }
    void file_close(int) override {}
};

constexpr uint32_t kThis = 0x1E7AA40, kTarget = kThis + 0x240;   // CB3SoundDistortion and its Clean buffer [C 0x28B5D0]

// The EE: its memory, and the command-6 handler as func_002FC530 does it (frames X = this + 0x840, Y = this + 0x1040;
// opt even: X.h0 -> Y.h0, DSP into X.h0, send Y; odd: Y.h1 -> X.h1, DSP into Y.h1, send X) with a pluggable DSP on
// the 256 left / right samples (the game's Process x2 and pan).
struct Ee final : b3native::B3RoutePlatform {
    std::map<uint32_t, uint8_t> mem;
    B3Route* route = nullptr;
    RwaIop* iop = nullptr;
    bool answer = true;
    std::function<int16_t(int16_t)> dsp = [](int16_t x) { return x; };
    struct Sent { uint32_t pkt[6]; uint64_t t; std::vector<uint8_t> data; };
    std::vector<Sent> sent;
    std::vector<uint8_t> last_payload;

    void write_ee(uint32_t a, const void* s, uint32_t n) override {
        last_payload.assign(static_cast<const uint8_t*>(s), static_cast<const uint8_t*>(s) + n);
        for (uint32_t k = 0; k < n; ++k) mem[a + k] = static_cast<const uint8_t*>(s)[k];
    }
    int16_t r16(uint32_t a) { return static_cast<int16_t>(mem[a] | mem[a + 1] << 8); }
    void w16(uint32_t a, int16_t v) { mem[a] = static_cast<uint8_t>(v); mem[a + 1] = static_cast<uint8_t>(v >> 8); }
    void copy(uint32_t dst, uint32_t src, uint32_t n) { for (uint32_t k = 0; k < n; ++k) mem[dst + k] = mem[src + k]; }
    void send_cmd(const uint32_t* p, uint32_t size) override {
        CHECK(size == 0x18);
        Sent s{};
        std::memcpy(s.pkt, p, 0x18);
        s.t = iop ? iop->now() : 0;
        s.data = last_payload;
        sent.push_back(s);
        if (!answer) return;
        const uint32_t opt = p[3], X = kThis + 0x840, Y = kThis + 0x1040;
        uint32_t out_l, out_r, frame, dest;
        if (opt & 1) {
            copy(X + 0x200, Y + 0x200, 0x200);
            copy(X + 0x600, Y + 0x600, 0x200);
            out_l = Y + 0x200; out_r = Y + 0x600; frame = X; dest = opt - 1;
        } else {
            copy(Y, X, 0x200);
            copy(Y + 0x400, X + 0x400, 0x200);
            out_l = X; out_r = X + 0x400; frame = Y; dest = opt;
        }
        for (uint32_t k = 0; k < 256; ++k) {
            w16(out_l + 2 * k, dsp(r16(kTarget + 2 * k)));
            w16(out_r + 2 * k, dsp(r16(kTarget + 0x400 + 2 * k)));
        }
        std::vector<uint8_t> f(0x800);
        for (uint32_t k = 0; k < 0x800; ++k) f[k] = mem[frame + k];
        CHECK(route->sif_dma_to_iop(dest, f.data(), 0x800));   // isceSifSetDma({frame, dest, 0x800, 0}, 1)
    }
};

template <class M>
struct Rig {
    RwaFake rf;
    M mix;
    RwaIop iop;
    Ee ee;
    B3Route route;
    Rig() : iop(rf, mix), route(ee, iop) { ee.route = &route; ee.iop = &iop; }
    void rpc(std::vector<uint32_t> words, uint32_t fno = B3Route::kRpcId) {
        words.resize(0x100, 0);   // the EE always sends its whole 0x400-byte buffer
        route.call(fno, words.data(), 0x400, nullptr, 0);
    }
    void construct(uint32_t mask = 0) { rpc({1, 0x8000, kTarget, mask}); }
    uint32_t sw(uint16_t e) { return mix.get_switch(iop.now(), e); }
    uint16_t par(uint16_t e) { return mix.get_param(iop.now(), e); }
    const uint8_t* ram() { return mix.sound_ram(); }
};

template <class M>
void commands_and_routing() {
    Rig<M> r;
    r.construct();
    CHECK(r.route.ee_target() == kTarget);
    CHECK(r.sw(sd::S_VMIXL | 0) == 0xFFFFFF && r.sw(sd::S_VMIXR | 0) == 0xFFFFFF);
    CHECK(r.sw(sd::S_VMIXEL | 0) == 0 && r.sw(sd::S_VMIXER | 0) == 0);
    CHECK(r.sw(sd::S_VMIXEL | 1) == 0xFFFFFF);   // core 1 untouched (libsd's default as RWA leaves it)
    CHECK(r.par(sd::P_BVOLL | 0) == 0 && r.par(sd::P_BVOLR | 0) == 0);
    CHECK(r.par(sd::P_BVOLL | 1) == 0x7FFF && r.par(sd::P_BVOLR | 1) == 0x7FFF);
    CHECK(r.par(sd::P_MMIX | 0) == 0x0FF0 && r.par(sd::P_MMIX | 1) == 0x0FFC);   // written back unchanged
    // SetChannels + Start in one list (Update's flush): mask, then core 1 armed at 0x3800.
    r.rpc({2, 0x8050, 0x1000005, 0x8020});
    CHECK(r.sw(sd::S_VMIXEL | 0) == 5 && r.sw(sd::S_VMIXER | 0) == 5);
    CHECK(r.sw(sd::S_VMIXL | 0) == 0xFFFFFA && r.sw(sd::S_VMIXR | 0) == 0xFFFFFA);
    CHECK(r.mix.get_addr(r.iop.now(), sd::A_IRQA | 1) == 0x3800);
    r.iop.advance(512);
    CHECK(r.route.stats().irqs == 4);
    // Stop: every BVOL 0, core 1's IRQ off; no more phases; a later start alone does not restore BVOL.
    r.rpc({1, 0x30});
    CHECK(r.par(sd::P_BVOLL | 0) == 0 && r.par(sd::P_BVOLR | 1) == 0 && r.par(sd::P_BVOLL | 1) == 0);
    r.iop.advance(1024);
    CHECK(r.route.stats().irqs == 4);
    r.rpc({1, 0x8020});
    r.iop.advance(512);
    CHECK(r.route.stats().irqs == 8 && r.par(sd::P_BVOLL | 1) == 0);
    // 0x10 is a no-op, unknown ids skip one word, a wrong fno is reported and still applied, a short list stops.
    r.rpc({3, 0x10, 0x1234, 0x8050, 2}, 0x99);
    CHECK(r.route.stats().unknown_commands == 1 && r.route.stats().wrong_fno == 1);
    CHECK(r.sw(sd::S_VMIXEL | 0) == 2);
    const uint32_t partial[2] = {1, 0x8000};
    r.route.call(B3Route::kRpcId, partial, 8, nullptr, 0);
    CHECK(r.route.stats().truncated == 1 && r.route.ee_target() == kTarget);
}
TEST(commands_and_routing_stub) { commands_and_routing<b3audio::Spu2StubMixer>(); }
TEST(commands_and_routing_mixer) { commands_and_routing<b3audio::Spu2MixerImpl>(); }

template <class M>
void phase_cycle() {
    Rig<M> r;
    r.construct(1);
    r.iop.advance(511);   // start just before ring position 511: 32 phases in the next 4096 samples
    r.rpc({1, 0x8020});
    const uint64_t t0 = r.iop.now();
    r.iop.advance(4096);
    const auto& st = r.route.stats();
    // Every 128 samples from ring position 0: A / C send (A even opt, C odd), B / D write, A / C read.
    CHECK(st.irqs == 32 && st.phases[0] == 8 && st.phases[1] == 8 && st.phases[2] == 8 && st.phases[3] == 8);
    CHECK(st.sends == 16 && st.reads == 16 && st.writes == 16 && st.skipped_busy == 0 && st.unknown_irqa == 0);
    const auto& S = r.ee.sent;
    CHECK(S.size() == 16);
    const uint32_t fuzzy = r.route.module_base() + 0xE80;
    for (size_t k = 0; k < S.size(); ++k) {
        const bool a = (S[k].t & 511) == 1;   // the IRQ returns after the sample at ring position 0 / 256
        CHECK(a == (k % 2 == 0) && (S[k].t & 255) == 1 && S[k].t > t0);
        CHECK(S[k].pkt[3] == (fuzzy | (a ? 0u : 1u)));
        CHECK(S[k].pkt[0] == (0x18u | 0x600u << 8) && S[k].pkt[1] == kTarget && S[k].pkt[2] == 6);
        CHECK(S[k].pkt[4] == 0 && S[k].pkt[5] == 0 && S[k].data.size() == 0x600);
        if (k) CHECK(S[k].t - S[k - 1].t == 256);
    }
    CHECK(r.mix.get_addr(r.iop.now(), sd::A_IRQA | 1) == 0x3800);
    // RWA's own core-0 clock keeps running behind the hook (IRQA alternating 0x800 / 0xA00 each 256 samples).
    const uint32_t a0 = r.mix.get_addr(r.iop.now(), sd::A_IRQA | 0);
    r.iop.advance(256);
    const uint32_t a1 = r.mix.get_addr(r.iop.now(), sd::A_IRQA | 0);
    CHECK((a0 == 0x800 && a1 == 0xA00) || (a0 == 0xA00 && a1 == 0x800));
}
TEST(phase_cycle_stub) { phase_cycle<b3audio::Spu2StubMixer>(); }
TEST(phase_cycle_mixer) { phase_cycle<b3audio::Spu2MixerImpl>(); }

// Clean reads (0x600 bytes from 0x2A00 at A into CleanB, from 0x2800 at C into CleanA; the next send carries them) and
// Fuzzy writes (the EE's frame into 0x4800-0x4FFF at B / D). The stub mixer leaves the rings as written.
TEST(clean_reads_and_fuzzy_writes) {
    Rig<b3audio::Spu2StubMixer> r;
    std::vector<uint8_t> pat(0x800);
    for (uint32_t k = 0; k < 0x800; ++k) pat[k] = static_cast<uint8_t>(k * 7 + (k >> 8));
    r.mix.voice_trans(0, 1, 0x2800, pat.data(), 0x800);
    r.ee.answer = false;
    r.construct(1);
    r.rpc({1, 0x8020});
    r.iop.advance(1024);
    const auto& S = r.ee.sent;
    CHECK(S.size() == 4);
    CHECK(std::all_of(S[0].data.begin(), S[0].data.end(), [](uint8_t b) { return b == 0; }));   // CleanA not read yet
    CHECK(std::equal(S[1].data.begin(), S[1].data.end(), pat.begin() + 0x200));   // C sends CleanB = read at A (0x2A00)
    CHECK(std::equal(S[2].data.begin(), S[2].data.end(), pat.begin()));           // A sends CleanA = read at C (0x2800)
    CHECK(std::equal(S[3].data.begin(), S[3].data.end(), pat.begin() + 0x200));
    // The EE's DMA into Fuzzy, then the next B / D phase writes it to core 1's MEMIN.
    std::vector<uint8_t> frame(0x800);
    for (uint32_t k = 0; k < 0x800; ++k) frame[k] = static_cast<uint8_t>(0xA5 ^ k);
    CHECK(r.route.sif_dma_to_iop(r.route.module_base() + 0xE80, frame.data(), 0x800));
    CHECK(!r.route.sif_dma_to_iop(r.route.module_base() + 0x2000, frame.data(), 0x800));   // past the image
    CHECK(!r.route.sif_dma_to_iop(r.route.module_base() - 0x10, frame.data(), 4));
    CHECK(!std::equal(frame.begin(), frame.end(), r.ram() + 0x4800));
    r.iop.advance(256);
    CHECK(std::equal(frame.begin(), frame.end(), r.ram() + 0x4800));
    CHECK(r.route.stats().ee_dmas == 1);
}

// End to end with the SPU2-equivalent mixer: voice 0 (core 0, masked) plays only into core 0's wet bus; the stand-in
// EE "processes" it (identity, then halving); core 1's output carries it 1024 samples later through MEMIN (BVOL 0x7FFF,
// MVOL 0x3FFF) and nothing else (core 0's master volume is 0, the other voices are silent).
void end_to_end(std::function<int16_t(int16_t)> dsp, int16_t (*expect)(int16_t)) {
    Rig<b3audio::Spu2MixerImpl> r;
    r.ee.dsp = std::move(dsp);
    auto& m = r.mix;
    m.set_hard_clip(true);   // audioq: the hardware's output, sample for sample (the host's limiter is 1 ms behind)
    // A looping ADPCM block at 0x10000 (filter 0, shift 2; flags 3 = end + repeat, LSAX = SSA at key-on).
    std::vector<uint8_t> blk(16);
    blk[0] = 0x02;
    blk[1] = 0x03;
    for (int k = 2; k < 16; ++k) blk[k] = static_cast<uint8_t>(0x1F * k + 0x35);
    m.voice_trans(0, 1, 0x10000, blk.data(), 16);
    r.construct(1);
    m.set_param(r.iop.now(), sd::P_MVOLL | 1, 0x3FFF);
    m.set_param(r.iop.now(), sd::P_MVOLR | 1, 0x3FFF);
    const uint64_t t = r.iop.now();
    m.set_addr(t, sd::VA_SSA | sd::voice(0), 0x10000);
    m.set_param(t, sd::VP_PITCH | sd::voice(0), 0x0C00);
    m.set_param(t, sd::VP_ADSR1 | sd::voice(0), 0x00FF);
    m.set_param(t, sd::VP_ADSR2 | sd::voice(0), 0x0000);
    m.set_param(t, sd::VP_VOLL | sd::voice(0), 0x3FFF);
    m.set_param(t, sd::VP_VOLR | sd::voice(0), 0x2000);
    m.set_switch(t, sd::S_KON | 0, 1);
    r.iop.advance(300);
    r.rpc({1, 0x8020});
    // Per sample: core 0's wet bus as written to its ring (w0x1400 / w0x1600 + pos) and the output frame.
    const uint64_t start = r.iop.now(), n = 6000;
    std::vector<int16_t> wl(n), wr(n), ol(n), orr(n);
    std::vector<int16_t> buf(2 * 64);
    int16_t junk[2 * 4096];
    while (m.read_output(junk, 4096) != 0) {}
    auto ram16 = [&](uint32_t w) { return static_cast<int16_t>(r.ram()[2 * w] | r.ram()[2 * w + 1] << 8); };
    for (uint64_t s = 0; s < n; ++s) {
        r.iop.advance(1);
        const uint32_t pos = static_cast<uint32_t>((start + s) & 511);
        wl[s] = ram16(0x1400 + pos);
        wr[s] = ram16(0x1600 + pos);
        CHECK(m.read_output(buf.data(), 64) == 1);
        ol[s] = buf[0];
        orr[s] = buf[1];
    }
    // The wet bus carries the voice (and only it); the dry output of core 0 is not heard.
    int nonzero = 0;
    for (uint64_t s = 0; s < n; ++s) nonzero += wl[s] != 0 && wr[s] != 0;
    CHECK(nonzero > 4000);
    // Latency exactly 1024 samples once the pipeline is full (two ring periods after the start).
    auto out = [](int16_t x) { return static_cast<int16_t>((((x * 0x7FFF) >> 15) * 0x7FFE) >> 15); };
    int bad = 0, cmp = 0;
    for (uint64_t s = 2048; s < n; ++s) {
        ++cmp;
        bad += ol[s] != out(expect(wl[s - 1024])) || orr[s] != out(expect(wr[s - 1024]));
    }
    CHECK(cmp > 3000 && bad == 0);
    // ... and not at 1023 / 1025.
    int off = 0;
    for (uint64_t s = 2048; s < n; ++s) off += ol[s] != out(expect(wl[s - 1023]));
    CHECK(off > 1000);
    CHECK(r.route.stats().skipped_busy == 0);
}
TEST(end_to_end_identity) { end_to_end([](int16_t x) { return x; }, [](int16_t x) { return x; }); }
TEST(end_to_end_dsp) {
    end_to_end([](int16_t x) { return static_cast<int16_t>(x >> 1); }, [](int16_t x) { return static_cast<int16_t>(x >> 1); });
}

// Without the hook (B3Route gone) RWA's executor behaves as before: core-1 IRQs are never armed by it.
TEST(unhook_on_destruction) {
    RwaFake rf;
    b3audio::Spu2StubMixer mix;
    RwaIop iop(rf, mix);
    Ee ee;
    {
        B3Route route(ee, iop);
        ee.route = &route;
        std::vector<uint32_t> w = {2, 0x8000, kTarget, 0, 0x8020};
        w.resize(0x100);
        route.call(B3Route::kRpcId, w.data(), 0x400, nullptr, 0);
        iop.advance(512);
        CHECK(route.stats().irqs == 4);
        route.call(B3Route::kRpcId, std::vector<uint32_t>{1, 0x30}.data(), 8, nullptr, 0);
    }
    iop.advance(1024);   // no dangling hook
    CHECK(mix.get_addr(iop.now(), sd::A_IRQA | 1) == 0x3800);
}

}  // namespace

TEST_MAIN("b3route (B3ROUTE.IRX: RPC, core-1 IRQ cycle, Clean / Fuzzy transfers, EE round trip)")
