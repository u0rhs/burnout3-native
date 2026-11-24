// The SPU2-equivalent mixer (src/audio/spu2_mixer_impl.cpp): hand-made vectors from the documented formats and
// formulas (psx-spx, the SPU2 Overview manual, retail LIBSD.IRX; see spu2_mixer_impl.h). No disc data needed.
#include <cmath>
#include <cstdio>
#include <string>
#include "test.h"

#include "../src/audio/spu2_mixer_impl.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace {

using b3audio::Spu2MixerImpl;
using Adsr = Spu2MixerImpl::Adsr;
namespace sd = b3audio::sd;

// One 16-byte ADPCM block: header (shift | filter << 4), flags, 14 data bytes.
std::vector<uint8_t> block(uint8_t shift_filter, uint8_t flags, uint8_t fill) {
    std::vector<uint8_t> b(16, fill);
    b[0] = shift_filter;
    b[1] = flags;
    return b;
}
void put(Spu2MixerImpl& m, uint32_t addr, const std::vector<uint8_t>& b) { m.voice_trans(m.time(), 1, addr, b.data(), static_cast<uint32_t>(b.size())); }
uint16_t entry(uint16_t p, int hw) { return static_cast<uint16_t>(p | sd::voice(hw)); }
int16_t ram16(const Spu2MixerImpl& m, uint32_t word) { const uint8_t* r = m.sound_ram(); return static_cast<int16_t>(r[word * 2] | r[word * 2 + 1] << 8); }
void run_to(Spu2MixerImpl& m, uint64_t t) {
    uint32_t c = 0;
    while (m.time() < t) m.run(t, &c);
}
// Key a voice on now: SSA, pitch, ADSR, volume.
void key_on(Spu2MixerImpl& m, int hw, uint32_t ssa, uint16_t pitch, uint16_t adsr1 = 0x00FF, uint16_t adsr2 = 0x0000, uint16_t vol = 0x3FFF) {
    const uint64_t t = m.time();
    m.set_addr(t, entry(sd::VA_SSA, hw), ssa);
    m.set_param(t, entry(sd::VP_PITCH, hw), pitch);
    m.set_param(t, entry(sd::VP_ADSR1, hw), adsr1);
    m.set_param(t, entry(sd::VP_ADSR2, hw), adsr2);
    m.set_param(t, entry(sd::VP_VOLL, hw), vol);
    m.set_param(t, entry(sd::VP_VOLR, hw), vol);
    m.set_switch(t, static_cast<uint16_t>(sd::S_KON | (hw >= 24)), 1u << (hw % 24));
}
// libsd's default graph (as RWA's sceSdInit leaves it) plus unity master volumes.
void graph(Spu2MixerImpl& m) {
    for (uint16_t c = 0; c < 2; ++c) {
        for (uint16_t s : {sd::S_VMIXL, sd::S_VMIXEL, sd::S_VMIXR, sd::S_VMIXER}) m.set_switch(0, static_cast<uint16_t>(s | c), 0xFFFFFF);
        m.set_param(0, static_cast<uint16_t>(sd::P_MMIX | c), c ? 0x0FFC : 0x0FF0);
        m.set_param(0, static_cast<uint16_t>(sd::P_MVOLL | c), 0x3FFF);
        m.set_param(0, static_cast<uint16_t>(sd::P_MVOLR | c), 0x3FFF);
        m.set_param(0, static_cast<uint16_t>(sd::P_BVOLL | c), 0x7FFF);
        m.set_param(0, static_cast<uint16_t>(sd::P_BVOLR | c), 0x7FFF);
    }
    m.set_param(0, sd::P_AVOLL | 1, 0x7FFF);
    m.set_param(0, sd::P_AVOLR | 1, 0x7FFF);
}
std::vector<int16_t> drain(Spu2MixerImpl& m) {
    std::vector<int16_t> v(2 * 200000);
    v.resize(2 * m.read_output(v.data(), 200000));
    return v;
}

// ---- ADPCM [psx-spx "SPU ADPCM Samples", CD-XA decode_28_nibbles; filters 0-4] ----

TEST(adpcm_filter0_shifts) {
    // shift 0, filter 0: nibble << 12. Byte 0x71: 1st sample nibble 1, 2nd nibble 7; 0x8F: -1, -8.
    std::vector<uint8_t> b = block(0x00, 0, 0);
    b[2] = 0x71; b[3] = 0x8F;
    int32_t h1 = 0, h2 = 0;
    int16_t s[28];
    Spu2MixerImpl::decode_block(b.data(), h1, h2, s);
    CHECK(s[0] == 4096 && s[1] == 28672 && s[2] == -4096 && s[3] == -32768 && s[4] == 0);
    b[0] = 0x04;   // shift 4: 7 << 12 >> 4
    h1 = h2 = 0;
    Spu2MixerImpl::decode_block(b.data(), h1, h2, s);
    CHECK(s[1] == 1792 && s[3] == -2048);
    b[0] = 0x0D;   // shift 13 acts as 9: 28672 >> 9
    h1 = h2 = 0;
    Spu2MixerImpl::decode_block(b.data(), h1, h2, s);
    CHECK(s[1] == 56);
}

TEST(adpcm_prediction_filters) {
    std::vector<uint8_t> b = block(0x2C, 0, 0);   // shift 12, filter 2 (115, -52)
    b[2] = 0xF1;                                    // nibbles 1, -1
    int32_t h1 = 1000, h2 = 500;
    int16_t s[4];
    Spu2MixerImpl::decode_halfword(b.data(), 0, h1, h2, s);
    CHECK(s[0] == 1392);   // 1 + (1000*115 - 500*52 + 32) >> 6 = 1 + 1391
    CHECK(s[1] == 1688);   // -1 + (1392*115 - 1000*52 + 32) >> 6 = -1 + 1689
    b = block(0x4C, 0, 0);                          // filter 4 (122, -60): rounding of a negative prediction
    h1 = -1000; h2 = 0;
    Spu2MixerImpl::decode_halfword(b.data(), 0, h1, h2, s);
    CHECK(s[0] == -1906);  // (-122000 + 32) >> 6 = -1906 (arithmetic shift)
    b = block(0x10, 0, 0x77);                       // filter 1 (60): saturation
    h1 = 32767; h2 = 0;
    Spu2MixerImpl::decode_halfword(b.data(), 0, h1, h2, s);
    CHECK(s[0] == 32767 && h1 == 32767);           // 28672 + 30719 saturates
    b = block(0x10, 0, 0);                          // filter 1 with history 1000: 938 per step
    h1 = 1000; h2 = 0;
    Spu2MixerImpl::decode_halfword(b.data(), 0, h1, h2, s);
    CHECK(s[0] == 938 && s[1] == 879);              // (1000*60 + 32) >> 6, (938*60 + 32) >> 6 = 56312 >> 6
}

// ---- Gaussian [psx-spx "4-Point Gaussian Interpolation"] ----

TEST(gauss_table_and_interpolation) {
    const int16_t* g = Spu2MixerImpl::gauss_table();
    int lo = 1 << 20, hi = -1;
    for (int i = 0; i < 256; ++i) {
        const int s = g[0xFF - i] + g[0x1FF - i] + g[0x100 + i] + g[i];
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    CHECK(lo == 0x7F7F && hi == 0x7F81);   // psx-spx: "they do sum up to 7F7Fh..7F81h"
    CHECK(g[0] == -1 && g[0xFF] == 0x12C7 && g[0x100] == 0x1307 && g[0x1FF] == 0x59B3);
    // i = 0, all four 0x4000: 4807/2 + 22963/2 + 4871/2 - 1/2 (floors) = 2403 + 11481 + 2435 - 1
    CHECK(Spu2MixerImpl::gauss(0, 0x4000, 0x4000, 0x4000, 0x4000) == 16318);
    // i = 0x80, only `old` = 0x4000: g[0x180] / 2 = 0x3E4C / 2
    CHECK(Spu2MixerImpl::gauss(0x80, 0, 0, 0x4000, 0) == 7974);
    const int16_t* f = Spu2MixerImpl::reverb_fir();
    int fs = 0;
    for (int i = 0; i < 39; ++i) fs += f[i];
    CHECK(fs == 0x7FFE && f[19] == 0x4000 && f[18] == 0x2806 && f[0] == -1);   // half-band: centre 0.5, odd taps 0.5
}

// audioq: the voices' resampling quality (a sound at its own rate played at 48 kHz by the pitch counter): the SPU2's
// Gaussian 4-tap against linear and nearest, as THD+N of a pure tone (the fundamental fitted out, the rest measured;
// dB below the tone). The Gaussian is the hardware's (the table and formula above); this records what it gives.
namespace {
double thdn_db(int kind, double src_rate, double tone) {
    const int n_in = 48000;
    std::vector<int32_t> in(n_in + 8);
    for (int k = 0; k < n_in + 8; ++k) in[k] = int32_t(std::lround(12000.0 * std::sin(2 * M_PI * tone * k / src_rate)));
    const uint32_t pitch = uint32_t(std::lround(src_rate / 48000.0 * 4096.0));   // the voice's pitch register
    std::vector<double> out;
    uint64_t counter = 0;
    while (true) {
        const uint64_t pos = counter >> 12;
        if (pos + 4 >= uint64_t(n_in)) break;
        const uint32_t frac = uint32_t(counter & 0xFFF);
        const int32_t a = in[pos], b = in[pos + 1], c = in[pos + 2], d = in[pos + 3];
        double y;
        if (kind == 0) y = Spu2MixerImpl::gauss(frac >> 4, a, b, c, d);   // the newest at pos + 3, as the voice's window
        else if (kind == 1) y = b + (c - b) * (frac / 4096.0);            // linear between the window's middle two
        else y = frac < 2048 ? b : c;                                     // nearest
        out.push_back(y);
        counter += pitch;
    }
    // the tone's frequency at the output: tone x (pitch / 4096) x 48000 / src_rate; fit sin / cos / DC, measure the rest
    const double f = tone * (pitch / 4096.0) * 48000.0 / src_rate / 48000.0;
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0, y1 = 0, n = double(out.size()), sm = 0, cm = 0;
    for (size_t k = 0; k < out.size(); ++k) {
        const double sn = std::sin(2 * M_PI * f * k), cs = std::cos(2 * M_PI * f * k);
        ss += sn * sn; cc += cs * cs; sc += sn * cs; ys += out[k] * sn; yc += out[k] * cs; y1 += out[k]; sm += sn; cm += cs;
    }
    (void)sc; (void)sm; (void)cm;
    const double A = ys / ss, B = yc / cc, D = y1 / n;
    double sig = 0, res = 0;
    for (size_t k = 0; k < out.size(); ++k) {
        const double fit = A * std::sin(2 * M_PI * f * k) + B * std::cos(2 * M_PI * f * k) + D;
        sig += fit * fit;
        res += (out[k] - fit) * (out[k] - fit);
    }
    return 10.0 * std::log10(res / sig);
}
}  // namespace

TEST(voice_resampling_quality) {
    struct Case { double rate, tone; };
    const Case cases[] = {{22050, 1000}, {22050, 5000}, {32000, 1000}, {32000, 8000}, {44100, 1000}, {44100, 12000}};
    for (const Case& c : cases) {
        const double g = thdn_db(0, c.rate, c.tone), l = thdn_db(1, c.rate, c.tone), nn = thdn_db(2, c.rate, c.tone);
        std::printf("  %5.0f Hz source, %5.0f Hz tone: THD+N Gaussian %6.1f dB, linear %6.1f dB, nearest %6.1f dB\n", c.rate, c.tone, g, l, nn);
        CHECK(g < nn);              // better than nearest everywhere
        if (c.tone <= 1000) CHECK(g < -60.0);   // a low tone: clean
    }
}

// ---- ADSR [psx-spx "Envelope Operation"; SPU2 Overview 4.1] ----

TEST(adsr_attack_curves) {
    Adsr e;
    e.adsr1 = 0x1BFF;   // AR 0x1B linear (shift 6, step +4: +128 per sample), DR 15, SL 15 (RWA's trigger key-on)
    e.key_on();
    for (int k = 0; k < 100; ++k) e.tick();
    CHECK(e.level == 12800 && e.phase == Adsr::Attack);
    for (int k = 0; k < 156; ++k) e.tick();
    CHECK(e.level == 0x7FFF && e.phase == Adsr::Decay);
    e.tick();
    CHECK(e.phase == Adsr::Sustain);   // SL 15 = 0x8000: decay ends at once
    Adsr x;
    x.adsr1 = 0x80FF;   // AR 0 exponential: +14336 per sample, every 4th sample above 0x6000
    x.key_on();
    const int32_t want[6] = {14336, 28672, 28672, 28672, 28672, 0x7FFF};
    bool ok = true;
    for (int k = 0; k < 6; ++k) { x.tick(); ok = ok && x.level == want[k]; }
    CHECK(ok);
    // The manual's +lin table (44.1 kHz time): rate 0x28 = 53 ms for 0 -> 1. Here: 32767 / 14 per sample.
    Adsr r;
    r.adsr1 = 0x28FF;
    r.key_on();
    int n = 0;
    while (r.phase == Adsr::Attack && n < 100000) { r.tick(); ++n; }
    CHECK(n == 2341);
    CHECK_NEAR(n / 44.1, 53.0, 0.5);
}

TEST(adsr_decay_sustain_release) {
    Adsr e;
    e.adsr1 = 0x0047;   // AR 0 linear, DR 4 (rate 16: shift 4, step -8 << 7, exponential), SL 7 (0x4000)
    e.adsr2 = 0x0C00 | 0x4000 | 7;   // SR 0x30 linear decrease (shift 12: -8 every 2 samples), RR 7 linear
    e.key_on();
    for (int k = 0; k < 3; ++k) e.tick();
    CHECK(e.level == 0x7FFF && e.phase == Adsr::Decay);
    e.tick();
    CHECK(e.level == 31743);   // 32767 + (-1024 * 32767 >> 15) = 32767 - 1024
    e.tick();
    CHECK(e.level == 30751);   // -1024 * 31743 >> 15 = -992
    int n = 0;
    while (e.phase == Adsr::Decay && n < 10000) { e.tick(); ++n; }
    CHECK(e.phase == Adsr::Sustain && e.level <= 0x4000 && e.level > 0x4000 - 1024);
    const int32_t s0 = e.level;
    for (int k = 0; k < 10; ++k) e.tick();
    CHECK(e.level == s0 - 40);   // -8 per 2 samples
    e.key_off();
    // release RR 7 linear: -128 per sample to 0 (RWA's DelayThread(5800) assumption: about 256 samples)
    const int32_t r0 = e.level;
    n = 0;
    while (e.phase != Adsr::Off && n < 100000) { e.tick(); ++n; }
    CHECK(n == (r0 + 127) / 128 && e.level == 0);
    // exponential release reaches 0 (the floor of step * level >> 15 stays <= -1)
    Adsr x;
    x.adsr1 = 0x00FF;
    x.adsr2 = 0x0020 | 0x00;   // RR 0 exponential
    x.key_on();
    for (int k = 0; k < 4; ++k) x.tick();
    x.key_off();
    n = 0;
    while (x.phase != Adsr::Off && n < 1000000) { x.tick(); ++n; }
    CHECK(x.phase == Adsr::Off && n < 1000000);
    // rate 0x7F never moves (SPU2 Overview 4.1: "infinity")
    Adsr h;
    h.adsr1 = 0x7FFF;
    h.key_on();
    for (int k = 0; k < 1000; ++k) h.tick();
    CHECK(h.level == 0 && h.phase == Adsr::Attack);
}

TEST(volume_fixed_and_sweeps) {
    Spu2MixerImpl::Volume v;
    v.write(0x3FFF);
    CHECK(v.level == 0x7FFE);
    v.write(0x4000);
    CHECK(v.level == -32768);   // 15-bit two's complement, x2: phase inverted
    v.write(0x0800);
    v.write(0x8000 | 0x20);     // linear increase, rate 0x20 (shift 8, +7 << 3 = +56 per sample)
    for (int k = 0; k < 10; ++k) v.tick();
    CHECK(v.level == 0x1000 + 560);
    v.write(0x7800);            // -0x1000 (phase inverted)
    v.write(0x8000 | 0x1000 | 0x20);   // linear increase, negative phase: toward -1.0
    for (int k = 0; k < 10; ++k) v.tick();
    CHECK(v.level == -(0x1000 + 560));
    v.write(0xA000 | 0x1000 | 0x20);   // linear decrease, negative phase: toward 0
    for (int k = 0; k < 50; ++k) v.tick();
    CHECK(v.level == -(0x1000 + 560 - 50 * 64));   // -8 << 3 = -64 per sample
    for (int k = 0; k < 50; ++k) v.tick();
    CHECK(v.level == 0);
}

// ---- Voices: render, loop / end, LSAX ----

TEST(voice_render_dc_block) {
    Spu2MixerImpl m;
    std::vector<uint8_t> b = block(0x04, 2, 0x77);   // shift 4, every nibble 7: DC 1792; flags 2 (no end)
    for (uint32_t a = 0x6000; a < 0x6400; a += 16) put(m, a, b);
    key_on(m, 1, 0x6000, 0x1000);   // core 0 voice 1: its OUTX is written to the ring at word 0x400 + pos
    run_to(m, 40);
    CHECK(m.envelope(1).level == 0x7FFF);
    const int16_t out = ram16(m, 0x400 + 39);
    // (gauss of four 1792s) * 0x7FFF >> 15: the table rows sum to 0x7F7F..0x7F81
    CHECK(out >= (1792 * 0x7F7F >> 15) - 5 && out <= (1792 * 0x7F81 >> 15));
    CHECK(ram16(m, 0x600 + 39) == 0);   // voice 3 silent
}

TEST(voice_end_without_loop) {
    Spu2MixerImpl m;
    put(m, 0x6000, block(0x04, 0, 0x77));
    put(m, 0x6010, block(0x04, 1, 0x77));   // end, no repeat: ENDX, jump to LSAX, envelope off
    put(m, 0x5000, block(0x0C, 0, 0));
    put(m, 0x5010, block(0x0C, 1, 0));      // RWA's silent end block
    key_on(m, 0, 0x6000, 0x1000);
    CHECK(m.lsax(0) == 0x6000);             // key-on copies SSA to LSAX
    run_to(m, 8);
    m.set_addr(m.time(), entry(sd::VA_LSAX, 0), 0x5010);
    CHECK(m.lsax(0) == 0x5010);
    run_to(m, 20);
    CHECK(m.endx(0) == 0 && m.envelope(0).level == 0x7FFF);
    // the decoder (12 samples ahead) leaves the end block when playback is at sample 56 - 12 - 4
    uint64_t t_end = 0;
    for (uint64_t t = 21; t < 80 && !t_end; ++t) {
        run_to(m, t);
        if (m.endx(0) & 1) t_end = t;
    }
    CHECK(t_end >= 38 && t_end <= 44);
    CHECK(m.get_param(m.time(), entry(sd::VP_ENVX, 0)) == 0 && m.envelope(0).phase == Adsr::Off);
    CHECK(m.get_addr(m.time(), entry(sd::VA_NAX, 0)) == 0x5012);   // at LSAX's block, past its header
    CHECK(m.get_switch(m.time(), sd::S_ENDX) == 1);
    run_to(m, 200);   // keyed-off voices keep running: 0x5010 ends on itself
    const uint32_t nax = m.nax(0);
    CHECK(nax >= 0x5012 && nax < 0x5020);
    key_on(m, 0, 0x6000, 0x1000);
    CHECK(m.endx(0) == 0);   // key-on clears ENDX
}

TEST(voice_loop_and_lsax) {
    Spu2MixerImpl m;
    put(m, 0x6000, block(0x04, 2, 0x11));   // A: normal
    put(m, 0x6010, block(0x04, 6, 0x22));   // B: loop start + repeat
    put(m, 0x6020, block(0x04, 3, 0x33));   // C: end + repeat
    key_on(m, 2, 0x6000, 0x1000);
    run_to(m, 40);
    CHECK(m.lsax(2) == 0x6010);              // latched at B
    uint32_t lo = 0xFFFFFF, hi = 0;
    for (uint64_t t = 41; t < 400; ++t) {
        run_to(m, t);
        lo = std::min(lo, m.nax(2));
        hi = std::max(hi, m.nax(2));
    }
    CHECK((m.endx(0) & 4) != 0);             // ENDX set on every pass over C
    CHECK(m.envelope(2).level == 0x7FFF);    // repeating: not muted
    CHECK(lo == 0x6012 && hi == 0x602E);     // loops over B..C only
    // LSAX written (4 samples or more after the key-on) wins over the loop-start flag until the next key-on
    key_on(m, 2, 0x6000, 0x1000);
    const uint64_t k0 = m.time();
    m.set_addr(k0 + 2, entry(sd::VA_LSAX, 2), 0x5000);   // within 4 Ts: disregarded
    CHECK(m.lsax(2) == 0x6000);
    m.set_addr(k0 + 4, entry(sd::VA_LSAX, 2), 0x6000);
    lo = 0xFFFFFF;
    for (uint64_t t = k0 + 60; t < k0 + 400; ++t) {
        run_to(m, t);
        lo = std::min(lo, m.nax(2));
    }
    CHECK(m.lsax(2) == 0x6000 && lo == 0x6002);   // loops from A now
}

TEST(pitch_modulation_and_noise) {
    Spu2MixerImpl m;
    m.set_core_attr(0, sd::C_NOISE_CLK | 0, 0x3F);   // fastest noise clock (set first: the noise timer keeps its count)
    for (uint32_t a = 0x6000; a < 0x8000; a += 16) put(m, a, block(0x00, 2, 0x77));   // DC +28672
    key_on(m, 4, 0x6000, 0x0800);   // the modulator
    key_on(m, 5, 0x6000, 0x0800);   // modulated: step = 0x800 * (OUTX4 + 0x8000) >> 15, about 0x1700
    key_on(m, 7, 0x6000, 0x0800);   // reference, no modulation
    m.set_switch(0, sd::S_PMON, 1u << 5);
    run_to(m, 600);
    const uint32_t d5 = m.nax(5) - 0x6000, d7 = m.nax(7) - 0x6000;
    CHECK(d7 > 0 && d5 > d7 * 16 / 10 && d5 < d7 * 2);
    // noise: voice 9 sounds the core's LFSR instead of its ADPCM; values change, positions still advance
    // noise: voice 1 (its OUTX is on the ring) sounds the core's LFSR instead of its ADPCM; its position advances
    key_on(m, 1, 0x6000, 0x1000);
    m.set_switch(m.time(), sd::S_NON, 1u << 1);
    int changes = 0;
    int16_t prev = 0;
    const uint64_t t0 = m.time();
    run_to(m, t0 + 200);
    for (uint64_t t = t0 + 10; t < t0 + 200; ++t) {
        const int16_t v = ram16(m, 0x400 + (t & 511));
        if (v != prev) ++changes;
        prev = v;
    }
    CHECK(changes > 20);
    CHECK(m.nax(1) > 0x6000);
}

// ---- Mixing: MEMIN, core chaining, saturation, output ----

TEST(memin_through_both_cores) {
    Spu2MixerImpl m;
    m.set_hard_clip(true);   // audioq: the hardware's output, sample for sample (the host's limiter is 1 ms behind)
    graph(m);
    const uint8_t s[2] = {0x00, 0x40};   // 0x4000 at MEMIN core 0 left, ring position 5
    m.voice_trans(0, 0, 0x4000 + 2 * 5, s, 2);
    run_to(m, 8);
    std::vector<int16_t> o = drain(m);
    CHECK(o.size() == 16);
    // BVOL 0x7FFF, MVOL0 0x7FFE, AVOL1 0x7FFF, MVOL1 0x7FFE, each (x * v) >> 15
    int32_t x = (0x4000 * 0x7FFF) >> 15;
    x = (x * 0x7FFE) >> 15;
    x = (x * 0x7FFF) >> 15;
    x = (x * 0x7FFE) >> 15;
    CHECK(o.size() == 16 && o[10] == x && o[11] == 0 && o[8] == 0 && o[12] == 0);
    // saturation: core 1's own MEMIN (0x7FFF) + core 0's chained 0x7FFF -> 32767 before MVOL1
    for (const bool hard : {true, false}) {
        Spu2MixerImpl n;
        n.set_hard_clip(hard);   // audioq: the hardware's clamp, or the host's soft limit of the unclamped sum
        graph(n);
        const uint8_t f[2] = {0xFF, 0x7F};
        n.voice_trans(0, 0, 0x4000 + 2 * 3, f, 2);
        n.voice_trans(0, 0, 0x4800 + 2 * 3, f, 2);
        n.set_param(0, sd::P_MMIX | 1, 0x0FFC | 0x00F0);
        n.set_param(0, sd::P_MVOLL | 0, 0x3FFF);
        run_to(n, 4);
        o = drain(n);
        if (hard) CHECK(o.size() == 8 && o[6] == ((32767 * 0x7FFE) >> 15));
        else CHECK(o.size() == 8 && o[6] == 0);   // the limiter: 1 ms behind (the saturated sum comes after; output_limiter)
    }
}

// audioq: the host's soft limit: the identity up to the knee, smooth and monotonic above, never past full scale
// audioq: the limiter: in range it is the identity (and stays so after a peak once released); over the knee the gain
// drops at once (nothing over the knee, no flat top) and comes back over kReleaseSeconds
TEST(output_limiter) {
    using L = Spu2MixerImpl::Limiter;
    L lim;
    int16_t l, r;
    std::vector<int> outs;
    for (int v = -29000; v <= 29000; v += 1013) {   // in range: the identity, kLookahead + 1 frames later
        lim.step(v, -v, l, r);
        outs.push_back(l);
    }
    for (int k = 0; k <= L::kLookahead; ++k) { lim.step(0, 0, l, r); outs.push_back(l); }
    bool same = true;
    for (size_t k = 0; k + L::kLookahead + 1 < outs.size() && k < 58; ++k) same = same && outs[k + L::kLookahead + 1] == -29000 + int(k) * 1013;   // (kLookahead + 1 behind)
    CHECK(same && lim.g == 1.0);
    // a burst 2x over full scale: a 440 Hz sine of amplitude 65000 for 20 ms, in silence
    int maxout = 0, repeated = 0, prev = -1;
    for (int k = 0; k < 2000; ++k) {
        const int64_t x = k < 960 ? std::llround(65000.0 * std::sin(2 * M_PI * 440.0 * k / 48000.0)) : 0;
        lim.step(x, x, l, r);
        const int a = std::abs(int(l));
        maxout = std::max(maxout, a);
        if (a == prev && a > 27000) ++repeated;
        prev = a;
    }
    std::printf("  limiter: 2x over full scale: out at most %d, %d repeated crest samples, gain %.3f\n", maxout, repeated, lim.g);
    CHECK(maxout <= Spu2MixerImpl::kSoftKnee + 100 && repeated < 6);
    for (int k = 0; k < 96000; ++k) lim.step(1000, 1000, l, r);   // two seconds of quiet: released
    CHECK(lim.g == 1.0 && l == 1000);
}

TEST(soft_limit_curve) {
    for (int v = -29000; v <= 29000; v += 997) CHECK(Spu2MixerImpl::soft_limit(v) == v);
    int prev = Spu2MixerImpl::kSoftKnee;
    for (int64_t v = Spu2MixerImpl::kSoftKnee; v < 200000; v += 50) {
        const int y = Spu2MixerImpl::soft_limit(v);
        CHECK(y >= prev && y <= 32767 && y - prev <= 50);
        CHECK(Spu2MixerImpl::soft_limit(-v) == -y);
        prev = y;
    }
    CHECK(Spu2MixerImpl::soft_limit(32767) < 32767 && Spu2MixerImpl::soft_limit(32767) > 31500);
    CHECK(Spu2MixerImpl::soft_limit(1000000) == 32767);
}

// ---- Reverb [psx-spx "SPU Reverb Formula"; LIBSD.IRX presets] ----

double energy(const std::vector<int16_t>& o, size_t from, size_t to) {
    double e = 0;
    for (size_t k = from; k < to && k < o.size(); ++k) e += double(o[k]) * o[k];
    return e;
}

// libsd's presets come from the game's own LIBSD.IRX (never in the repo): B3_GAME_DIR or data/game.
bool presets() {
    if (b3audio::libsd_reverb_presets_loaded()) return true;
    const char* g = std::getenv("B3_GAME_DIR");
    const std::string p = std::string(g ? g : "data/game") + "/IOP/LIBSD.IRX";
    if (b3audio::load_libsd_reverb_presets(p.c_str())) return true;
    std::printf("  (reverb checks skipped: no LIBSD.IRX at %s)\n", p.c_str());
    return false;
}

std::vector<int16_t> click_through_reverb(int mode) {
    Spu2MixerImpl m(1u << 17);
    graph(m);
    m.set_param(0, sd::P_MMIX | 0, 0x0030);   // core 0: MEMIN to the wet path only
    m.set_addr(0, sd::A_EEA | 0, 0x1DFFFF);
    b3audio::Spu2EffectAttr a;
    a.mode = mode;
    a.depth_l = a.depth_r = 0x7FFF;
    m.set_effect_attr(0, 0, a);
    m.set_core_attr(0, sd::C_EFFECT_ENABLE | 0, 1);
    const uint8_t s[2] = {0x00, 0x40};
    m.voice_trans(0, 0, 0x4000 + 2 * 10, s, 2);   // click: left 0x4000 at sample 10
    m.voice_trans(0, 0, 0x4400 + 2 * 10, s, 2);   // and right
    run_to(m, 100);
    const uint8_t z[2] = {0, 0};
    m.voice_trans(100, 0, 0x4000 + 2 * 10, z, 2);  // played once
    m.voice_trans(100, 0, 0x4400 + 2 * 10, z, 2);
    run_to(m, 96000);
    return drain(m);
}

TEST(reverb_click_tail_and_off) {
    if (!presets()) return;
    const std::vector<int16_t> o = click_through_reverb(2);   // STUDIO_A
    CHECK(o.size() == 2 * 96000);
    int peak = 0;
    for (int16_t v : o) peak = std::max(peak, std::abs(int(v)));
    CHECK(peak > 100 && peak < 0x4000);                         // a tail, bounded by the click
    CHECK(energy(o, 0, 2 * 10) == 0);                           // nothing before the click
    const double e1 = energy(o, 0, 2 * 12000), e2 = energy(o, 2 * 12000, 2 * 24000), e4 = energy(o, 2 * 72000, 2 * 96000);
    CHECK(e1 > 0 && e2 < e1 && e4 < e1 / 1000);                  // decaying
    const std::vector<int16_t> off = click_through_reverb(0);   // mode 0 (OFF): silence on the wet path
    CHECK(off.size() == 2 * 96000 && energy(off, 0, off.size()) == 0);
    const std::vector<int16_t> bad = click_through_reverb(-1);  // RWA's ReverbType 0: refused by libsd, no preset
    CHECK(energy(bad, 0, bad.size()) == 0);
}

TEST(libsd_corrupt_section_table) {
    // fsrobust: section offsets that wrapped in 32-bit sums passed the bounds checks and were read far outside the file
    auto elf = [](std::uint32_t shoff, std::uint16_t shent, std::uint16_t shnum) {
        std::vector<uint8_t> d(0x10000, 0);
        d[0] = 0x7F; d[1] = 'E'; d[2] = 'L'; d[3] = 'F';
        for (int k = 0; k < 4; ++k) d[0x20 + k] = uint8_t(shoff >> (8 * k));
        d[0x2E] = uint8_t(shent); d[0x2F] = uint8_t(shent >> 8);
        d[0x30] = uint8_t(shnum); d[0x31] = uint8_t(shnum >> 8);
        return d;
    };
    CHECK(!b3audio::load_libsd_reverb_presets_bytes(elf(0xFFFFFF00u, 0x100, 0x100)));   // shoff + shnum * shent wraps to 0xFF00
    std::vector<uint8_t> d = elf(0x100, 0x28, 1);   // one section; its name offset + the string table wraps
    for (int k = 0; k < 4; ++k) d[0x100 + 0x10 + k] = uint8_t(0xFFFFFFFAu >> (8 * k));   // strtab = 0xFFFFFFFA, name 0
    CHECK(!b3audio::load_libsd_reverb_presets_bytes(d));
}

TEST(reverb_libsd_attributes) {
    if (!presets()) return;
    Spu2MixerImpl m;
    m.set_addr(0, sd::A_EEA | 0, 0x1DFFFF);
    b3audio::Spu2EffectAttr a;
    a.mode = 2;
    a.depth_l = 0x1234;
    a.depth_r = -5;
    m.set_effect_attr(0, 0, a);
    CHECK(m.esa(0) == 0x1DFFFF - (0x3E8 * 8 - 1));   // ESA = EEA end - size + 1 (STUDIO_A 0x1F40 bytes)
    CHECK(m.get_param(0, sd::P_EVOLL | 0) == 0x1234 && m.get_param(0, sd::P_EVOLR | 0) == 0xFFFB);   // EVOL = depth
    CHECK(m.reverb_regs(0)[2] == 0x70F0 && m.reverb_regs(0)[10] == 0x03E4);
    // ECHO with delay 127: libsd's delay rule reproduces its own preset's addresses; feedback 127 -> vWALL 127 * 258
    a.mode = 7;
    a.delay = 127;
    a.feedback = 127;
    m.set_effect_attr(0, 0, a);
    const auto& r = m.reverb_regs(0);
    CHECK(r[10] == 0x1FFD && r[11] == 0x0FFD && r[16] == 0x1009 && r[12] == 0x1009 && r[26] == 0x1008 && r[27] == 0x1004);
    CHECK(r[7] == 0x7FFE);
    a.delay = 0;   // delay 0: mLSAME = 64 - dAPF1
    m.set_effect_attr(0, 0, a);
    CHECK(m.reverb_regs(0)[10] == 64 - 3 && m.reverb_regs(0)[16] == 0x0009 + 32);
    CHECK(m.esa(0) == 0x1DFFFF - (0x3008 * 8 - 1));
}

// ---- IRQ ----

TEST(irq_ring_fetch_transfer) {
    Spu2MixerImpl m;
    uint32_t c = 0;
    m.set_addr(0, sd::A_IRQA | 0, 0x800);   // core 0 voice 1 output ring, position 0
    m.set_core_attr(0, sd::C_IRQ_ENABLE | 0, 1);
    CHECK(m.run(1000, &c) == 1 && c == 1);   // sample 0 wrote position 0
    CHECK(m.run(1000, &c) == 1000 && c == 0);   // disarmed until re-enabled
    m.set_addr(1000, sd::A_IRQA | 0, 0xA00);   // position 256
    m.set_core_attr(1000, sd::C_IRQ_ENABLE | 0, 0);
    m.set_core_attr(1000, sd::C_IRQ_ENABLE | 0, 1);
    CHECK(m.run(5000, &c) == 1281 && c == 1);   // the next sample at position 256 is 1280
    // voice fetch: a voice entering the block at IRQA (core 1's IRQA, hit by a core 0 voice)
    Spu2MixerImpl v;
    for (uint32_t a = 0x6000; a < 0x6100; a += 16) put(v, a, block(0x04, 2, 0));
    v.set_addr(0, sd::A_IRQA | 1, 0x6040);
    v.set_core_attr(0, sd::C_IRQ_ENABLE | 1, 1);
    key_on(v, 0, 0x6000, 0x1000);
    const uint64_t t = v.run(500, &c);
    CHECK(c == 2 && t > 40 && t < 112);   // the 5th block, reached by the decoder ahead of playback
    // a transfer over IRQA: reported by the next run at once
    Spu2MixerImpl w;
    w.set_addr(0, sd::A_IRQA | 0, 0x9000);
    w.set_core_attr(0, sd::C_IRQ_ENABLE | 0, 1);
    run_to(w, 10);
    const std::vector<uint8_t> d(0x100, 0);
    w.voice_trans(10, 1, 0x8F80, d.data(), 0x100);
    CHECK(w.run(100, &c) == 10 && c == 1);
}

}  // namespace

TEST_MAIN("spu2_mixer (SPU2-equivalent mixer)")
