// The SPU2-equivalent mixer (spu2_mixer_impl.h). Sources and tags as listed there.
//
// Where the sources disagree or are silent, the choice made here:
//   - Decode timing: the ADPCM decoder runs ahead of the pitch counter, one halfword (4 samples) per sample while at
//     most kDecodeAhead samples are queued; NAX is the next halfword to decode (never the header: after the last
//     halfword the block-end actions run at once and NAX = next block + 2). Block-end actions (ENDX, jump to LSAX,
//     the mute of a non-repeating end) happen when the decoder leaves the block, i.e. ~13-16 samples before the
//     last samples are heard [L: B0 NAX granularity; SPU2 2.1.1 says NAX advances "as decoding
//     advances"].
//   - LSAX: key-on copies SSA to LSAX [spx]; a block with LOOP/START latches its address into LSAX unless LSAX was
//     written since the key-on [SPU2 1.3.3; spx says it always latches; SPU2 chosen, it is the SPU2's manual];
//     writes within 4 samples of the key-on are ignored [SPU2 2.1.1].
//   - Non-repeating end (flags 1): ENDX, jump to LSAX, envelope off at level 0 [spx "End+Mute"; SPU2 1.3.2 / 2.1.4]
//     (SPU2 2.7 adds that such voices "free-run the entire local memory"; not modelled: they follow LSAX as spx says,
//     which is what the B0 log shows for idle voices: NAX 0x5002 / 0x5012).
//   - ADPCM: filters 0-4 [spx]; filters 5-15 decode as filter 0 [G]; shift 13-15 act as 9 [spx, XA note
//     "reportedly same" for SPU]; prediction (h1*f0 + h2*f1 + 32) >> 6
//     (arithmetic shift; spx writes "/64").
//   - Pitch: step > 0x3FFF becomes 0x4000 [spx; SPU2 2.1.2 gives 0x3FFF as the maximum]; PMON uses the previous
//     voice's OUTX of the previous sample [SPU2 2.1.3 "1 Ts before"].
//   - Gaussian: the 512-entry table of spx (rows sum to 0x7F7F..0x7F81), index = counter bits 4-11 [spx; one spx
//     line says bits 3-11].
//   - Envelope / sweep rate: spx's cycles / step rule, counted per 48 kHz sample (the SPU2 manual's rate table is
//     the same rule in 44.1 kHz time: rate 0x28 = 2340 samples = 53 ms there); the counter restarts at each phase;
//     rate 0x7F never moves [SPU2 4.1 "infinity"]. Exponential decrease: step * level >> 15 (floor, so it reaches
//     0). Levels saturate to 0..0x7FFF (SPU2 2.x says ENVX shows a negative value for 1 Ts in linear decrement; not
//     modelled, ENVX reads 0).
//   - Volume sweeps: magnitude stepped like the envelope, sign = the phase bit (exponential decrease keeps the
//     current sign: spx "phase bit has no effect") [SPU2 VOLL/R +lin / -lin X bit; spx].
//   - Mixing: per voice (out * vol) >> 15 into the four buses by VMIX; buses saturated to s16 after the 24 voices,
//     after adding MEMIN (BVOL) / core-0 input (AVOL) by MMIX, after the effect return (EVOL) and after MVOL
//     [L: SPU2 2.4 - 2.6 order; the saturation points are not documented]. Core 0's output reaches core 1 in the
//     same sample [G].
//   - Reverb: spx's formula on the SPU2 work area ESA..EEA (word addresses, wrap inside it), at 24 kHz with spx's
//     39-tap FIR (down: >> 15; up: zero-stuffed, >> 14 for unity gain [L]); register offsets x 4 words and ESA = EEA
//     end - size + 1, as libsd writes them [libsd 0x110 / 0xD08]. With the effect disabled the work area is not
//     written and the SAME / DIFF stages are skipped [spx "Reverb Disable"]; reverb accesses then raise no IRQ
//     [SPU2 2.7].
//   - IRQ: any armed core's IRQA against ring accesses (sd::ring_irq_pos), ADPCM header / halfword fetches, reverb
//     accesses and transfers ([addr, addr + n) and the end pointer addr + n + 2, SPU2 2.7); the IRQ disarms its core
//     until IRQ_ENABLE is written again [L libsd / RWA handler 0x6464].
//   - Noise: spx's LFSR, libsd NOISE_CLK (6 bits) = shift << 2 | step [L].
#include "spu2_mixer_impl.h"
#include "b3port/fileio.h"   // fsrobust: UTF-8 paths
#include <vector>
#include <array>
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace b3audio {

namespace {
#include "spu2_tables.inc"

constexpr int32_t clamp16(int32_t v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }
constexpr uint32_t kMask = Spu2MixerImpl::kRamBytes - 1;
constexpr uint32_t floor16(uint32_t a) { return a & (Spu2MixerImpl::kRamBytes - 16); }   // sceSdSetAddr [libsd 0x37DC]

// Reverb register indices, psx-spx order (spu2_tables.inc).
enum Rv {
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2,
    mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1,
    mLAPF2, mRAPF2, vLIN, vRIN
};
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Building blocks

void Spu2MixerImpl::env_step(int32_t& level, uint32_t& cnt, int rate, bool dec, bool expo) {
    if (rate >= 0x7F) return;
    const int shift = rate >> 2;
    const int32_t base = dec ? -8 + (rate & 3) : 7 - (rate & 3);
    uint32_t cycles = 1u << (shift > 11 ? shift - 11 : 0);
    int32_t step = base * (1 << (shift < 11 ? 11 - shift : 0));
    if (expo && !dec && level > 0x6000) cycles <<= 2;
    if (expo && dec) step = (step * level) >> 15;
    if (++cnt < cycles) return;
    cnt = 0;
    level = std::clamp(level + step, 0, 0x7FFF);
}

void Spu2MixerImpl::Adsr::tick() {
    switch (phase) {
    case Attack:
        env_step(level, cnt, (adsr1 >> 8) & 0x7F, false, adsr1 & 0x8000);
        if (level >= 0x7FFF) { phase = Decay; cnt = 0; }
        break;
    case Decay:
        env_step(level, cnt, ((adsr1 >> 4) & 0xF) << 2, true, true);
        if (level <= ((adsr1 & 0xF) + 1) * 0x800) { phase = Sustain; cnt = 0; }
        break;
    case Sustain:
        env_step(level, cnt, (adsr2 >> 6) & 0x7F, adsr2 & 0x4000, adsr2 & 0x8000);
        break;
    case Release:
        env_step(level, cnt, (adsr2 & 0x1F) << 2, true, adsr2 & 0x20);
        if (level <= 0) mute();
        break;
    case Off: break;
    }
}

void Spu2MixerImpl::Volume::write(uint16_t v) {
    reg = v;
    cnt = 0;
    if (!(v & 0x8000)) level = static_cast<int16_t>(v << 1);   // fixed: volume / 2, 15-bit signed [SPU2, spx]
}

void Spu2MixerImpl::Volume::tick() {
    if (!(reg & 0x8000)) return;
    const bool expo = reg & 0x4000, dec = reg & 0x2000;
    const bool neg = (expo && dec) ? level < 0 : (reg & 0x1000) != 0;
    int32_t mag = std::min(level < 0 ? -level : level, 0x7FFF);
    env_step(mag, cnt, reg & 0x7F, dec, expo);
    level = neg ? -mag : mag;
}

void Spu2MixerImpl::decode_halfword(const uint8_t* block, int hw, int32_t& h1, int32_t& h2, int16_t out[4]) {
    static const int32_t f0[5] = {0, 60, 115, 98, 122}, f1[5] = {0, 0, -52, -55, -60};
    int shift = block[0] & 0xF;
    if (shift > 12) shift = 9;
    int filter = (block[0] >> 4) & 0xF;
    if (filter > 4) filter = 0;
    const uint32_t d = static_cast<uint32_t>(block[2 + 2 * hw] | block[3 + 2 * hw] << 8);
    for (int i = 0; i < 4; ++i) {
        const int32_t t = static_cast<int16_t>(static_cast<uint16_t>(((d >> (4 * i)) & 0xF) << 12)) >> shift;
        const int32_t s = clamp16(t + ((h1 * f0[filter] + h2 * f1[filter] + 32) >> 6));
        out[i] = static_cast<int16_t>(s);
        h2 = h1;
        h1 = s;
    }
}

void Spu2MixerImpl::decode_block(const uint8_t* block, int32_t& h1, int32_t& h2, int16_t out[28]) {
    for (int hw = 0; hw < 7; ++hw) decode_halfword(block, hw, h1, h2, out + 4 * hw);
}

const int16_t* Spu2MixerImpl::gauss_table() { return kGauss; }
const int16_t* Spu2MixerImpl::reverb_fir() { return kReverbFir; }

int16_t Spu2MixerImpl::soft_limit(int64_t v) {
    const int64_t a = v < 0 ? -v : v;
    if (a <= kSoftKnee) return static_cast<int16_t>(v);
    const double room = 32767.0 - kSoftKnee;
    const double y = kSoftKnee + room * std::tanh(double(a - kSoftKnee) / room);   // slope 1 at the knee, 32767 at infinity
    const int32_t r = std::min(32767, int32_t(std::lround(y)));
    return static_cast<int16_t>(v < 0 ? -r : r);
}

void Spu2MixerImpl::Limiter::step(int64_t l, int64_t r, int16_t& out_l, int16_t& out_r) {
    static const double kRelease = 1.0 - std::exp(-1.0 / (kReleaseSeconds * 48000.0));
    static const double kAttack = 1.0 - std::exp(-5.0 / kLookahead);   // ~99 % of the way within the lookahead
    // the oldest frame goes out; the new one takes its place
    const int64_t ol = ring[pos][0], orr = ring[pos][1];
    ring[pos][0] = l;
    ring[pos][1] = r;
    const int64_t peak = std::max(l < 0 ? -l : l, r < 0 ? -r : r);
    need[pos] = peak > kSoftKnee ? double(kSoftKnee) / double(peak) : 1.0;
    pos = (pos + 1) % (kLookahead + 1);
    double gmin = 1.0;
    for (const double n : need) gmin = std::min(gmin, n);   // the lowest gain the coming millisecond needs
    if (gmin < g) g += (gmin - g) * kAttack;
    else if (g < 1.0) g = 1.0 - g < 1e-6 ? 1.0 : std::min(1.0, g + (1.0 - g) * kRelease);   // release (to exactly 1)
    if (g >= 1.0) {   // (the identity: the hardware's value whenever it was in range)
        out_l = soft_limit(ol);
        out_r = soft_limit(orr);
        return;
    }
    out_l = soft_limit(std::llround(double(ol) * g));
    out_r = soft_limit(std::llround(double(orr) * g));
}

int32_t Spu2MixerImpl::gauss(uint32_t i, int32_t oldest, int32_t older, int32_t old, int32_t newest) {
    i &= 0xFF;
    return ((kGauss[0x0FF - i] * oldest) >> 15) + ((kGauss[0x1FF - i] * older) >> 15) +
           ((kGauss[0x100 + i] * old) >> 15) + ((kGauss[i] * newest) >> 15);
}

// ---------------------------------------------------------------------------------------------------------------
// Construction, time, output

Spu2MixerImpl::Spu2MixerImpl(size_t output_frames)
    : ram_(kRamBytes, 0), out_(std::max<size_t>(1, output_frames) * 2, 0), out_cap_(std::max<size_t>(1, output_frames)) {
    const char* e = std::getenv("B3_SPU_STALE_LOG");
    stale_log_ = e && *e && *e != '0';
    {   // audioq: B3_AUDIO_HARD_CLIP=1: the output clamped as the hardware's (no soft limit)
        const char* h = std::getenv("B3_AUDIO_HARD_CLIP");
        hard_env_ = hard_clip_ = h && *h == '1';
    }
    if (stale_log_) { wgen_.assign(kRamBytes / 16, 0); pgen_.assign(kRamBytes / 16, 0); }
}

void Spu2MixerImpl::advance(uint64_t t) {
    while (t_ < t) render_one();
}

uint64_t Spu2MixerImpl::run(uint64_t t, uint32_t* irq_cores) {
    *irq_cores = 0;
    while (pending_ == 0 && t_ < t) render_one();
    if (pending_ != 0) {
        *irq_cores = pending_;
        pending_ = 0;
        ++irq_count_;
    }
    return t_;
}

size_t Spu2MixerImpl::read_output(int16_t* lr, size_t max_frames) {
    const size_t n = std::min(max_frames, out_size_);
    for (size_t k = 0; k < n; ++k) {
        const size_t i = (out_head_ + k) % out_cap_;
        lr[2 * k] = out_[2 * i];
        lr[2 * k + 1] = out_[2 * i + 1];
    }
    out_head_ = (out_head_ + n) % out_cap_;
    out_size_ -= n;
    return n;
}

void Spu2MixerImpl::touch(uint32_t a) {
    for (int c = 0; c < 2; ++c) {
        Core& k = core_[c];
        if (k.irq_enable && k.irqa == a) {
            k.irq_enable = false;
            pending_ |= 1u << c;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Voices

namespace {
std::vector<std::pair<uint32_t, uint32_t>> g_stream_rings;   // [base, end) in bytes
bool in_stream_ring(uint32_t a) {
    for (const auto& r : g_stream_rings) if (a >= r.first && a < r.second) return true;
    return false;
}
}  // namespace
void spu2_mark_stream_ring(uint32_t base, uint32_t len) {
    for (const auto& r : g_stream_rings) if (r.first == base && r.second == base + len) return;
    g_stream_rings.emplace_back(base, base + len);
    if (std::getenv("B3_SPU_STALE_LOG")) std::fprintf(stderr, "spu2: stream ring 0x%05X..0x%05X\n", base, base + len);
}
void Spu2MixerImpl::enter_block(Voice& v, uint32_t addr) {
    v.block = floor16(addr);
    touch(v.block);
    if (stale_log_) {
        static const struct Tr { uint32_t lo = 0, hi = 0; double from = 0, to = 0; } tr = [] {
            Tr x; const char* e = std::getenv("B3_SPU_TRACE_RING");
            if (e) std::sscanf(e, "%x,%x,%lf,%lf", &x.lo, &x.hi, &x.from, &x.to);
            return x; }();
        const double ts = double(t_) / 48000.0;
        if (tr.hi && (v.block & kMask) >= tr.lo && (v.block & kMask) < tr.hi && ts >= tr.from && ts < tr.to && (v.block & 0xFF) == 0)
            std::fprintf(stderr, "spu2 trace: t %.3f play 0x%05X env %d lvl %d nax 0x%05X\n", ts, v.block & kMask, int(v.env.phase), int(v.env.level), v.nax & kMask);
        const uint32_t b = (v.block & kMask) >> 4;
        const bool audible = v.env.phase != Adsr::Off && v.env.level > 0;
        if (audible && v.pitch == 0x0AAA && wgen_[b] >= 2 && pgen_[b] == wgen_[b] + 1 && in_stream_ring(v.block & kMask) &&
            std::getenv("B3_SPU_STALE_32K")) {   // mac5: the 32 kHz (music / speech) voices' stale blocks, per voice per second
            static int last_s = -1, last_v = -1;
            static unsigned n = 0;
            const int sec = int(t_ / 48000), vi = int(&v - &core_[0].v[0]);
            if (sec != last_s || vi != last_v) {
                if (n) std::fprintf(stderr, "spu2: 32k stale voice %d second %d (VBlank ~%d): %u blocks\n", last_v, last_s, last_s * 60, n);
                last_s = sec; last_v = vi; n = 0;
            }
            ++n;
        }
        if (audible && wgen_[b] >= 2 && pgen_[b] == wgen_[b] + 1 && in_stream_ring(v.block & kMask)) {
            ++stale_count_;
            if (t_ >= stale_last_t_ + 48000 || stale_count_ == 1) {   // at most a line a second
                std::fprintf(stderr, "spu2: stale replay: block 0x%05X (uploaded %u times) played again without a new upload, "
                             "t %.2f s (VBlank ~%.0f), %llu so far; pitch 0x%04X env %d level %d voice %d\n", v.block & kMask, wgen_[b],
                             double(t_) / 48000.0, double(t_) / 800.0, static_cast<unsigned long long>(stale_count_), v.pitch,
                             int(v.env.phase), int(v.env.level),
                             int(&v - &core_[0].v[0]) < 24 && int(&v - &core_[0].v[0]) >= 0 ? int(&v - &core_[0].v[0]) : 100 + int(&v - &core_[1].v[0]));
                stale_last_t_ = t_;
            }
        }
        if (audible) pgen_[b] = wgen_[b] + 1;   // a silent pass (released, off) is not a play
    }
    if ((ram_[v.block + 1] & 4) && !v.lsax_written) v.lsax = v.block;   // LOOP/START
    v.nax = v.block + 2;
}

void Spu2MixerImpl::decode_next(Core& k, int vi) {
    Voice& v = k.v[vi];
    const uint32_t blk = v.block;
    touch(v.nax);
    int16_t s4[4] = {0, 0, 0, 0};
    if (v.env.phase != Adsr::Off) decode_halfword(&ram_[blk], static_cast<int>((v.nax - blk) / 2 - 1), v.h1, v.h2, s4);
    for (int i = 0; i < 4; ++i) v.q[(v.wr + i) & 31] = s4[i];
    v.wr += 4;
    v.nax += 2;
    if (v.nax < blk + 16) return;
    const uint8_t flags = ram_[blk + 1];
    uint32_t next = blk + 16;
    if (flags & 1) {   // LOOP/END
        k.endx |= 1u << vi;
        next = v.lsax;
        if (!(flags & 2)) v.env.mute();
    }
    enter_block(v, next & kMask);
}

void Spu2MixerImpl::noise_tick(Core& k) {
    const int shift = (k.noise_clk >> 2) & 0xF, step = (k.noise_clk & 3) + 4;
    k.noise_timer -= step;
    const uint16_t l = k.noise_level;
    const uint16_t parity = ((l >> 15) ^ (l >> 12) ^ (l >> 11) ^ (l >> 10) ^ 1) & 1;
    if (k.noise_timer < 0) {
        k.noise_level = static_cast<uint16_t>(l << 1 | parity);
        k.noise_timer += 0x20000 >> shift;
        if (k.noise_timer < 0) k.noise_timer += 0x20000 >> shift;
    }
}

void Spu2MixerImpl::voice_sample(Core& k, int vi, int32_t prev_outx, int32_t mix[4]) {
    Voice& v = k.v[vi];
    if (v.vol[0].sweeping()) v.vol[0].tick();
    if (v.vol[1].sweeping()) v.vol[1].tick();
    if (v.wr - v.rd <= kDecodeAhead) decode_next(k, vi);
    v.outx = 0;
    if (v.env.phase != Adsr::Off) {
        const int32_t s = (k.non >> vi & 1) ? static_cast<int16_t>(k.noise_level)
                                            : gauss((v.counter >> 4) & 0xFF, v.q[v.rd & 31], v.q[(v.rd + 1) & 31],
                                                    v.q[(v.rd + 2) & 31], v.q[(v.rd + 3) & 31]);
        v.env.tick();
        v.outx = (s * v.env.level) >> 15;
    }
    uint32_t step = v.pitch;
    if (vi > 0 && (k.pmon >> vi & 1)) {   // [spx "Pitch Counter"]
        const int32_t f = prev_outx + 0x8000;
        step = static_cast<uint32_t>((static_cast<int32_t>(static_cast<int16_t>(v.pitch)) * f) >> 15) & 0xFFFF;
    }
    if (step > 0x3FFF) step = 0x4000;
    v.counter += step;
    v.rd += v.counter >> 12;
    v.counter &= 0xFFF;
    if (v.outx == 0) return;
    const int32_t l = (v.outx * v.vol[0].level) >> 15, r = (v.outx * v.vol[1].level) >> 15;
    const uint32_t bit = 1u << vi;
    if (k.vmix[0] & bit) mix[0] += l;
    if (k.vmix[1] & bit) mix[1] += r;
    if (k.vmix[2] & bit) mix[2] += l;
    if (k.vmix[3] & bit) mix[3] += r;
}

void Spu2MixerImpl::key_on(int c, uint32_t mask) {
    Core& k = core_[c];
    for (int vi = 0; vi < 24; ++vi) {
        if (!(mask & (1u << vi))) continue;
        Voice& v = k.v[vi];
        if (stale_log_ && in_stream_ring(v.ssa & kMask))
            std::fprintf(stderr, "spu2: key on core %d voice %d ssa 0x%05X pitch 0x%04X t %.2f s (VBlank ~%.0f)\n", c, vi,
                         v.ssa & kMask, v.pitch, double(t_) / 48000.0, double(t_) / 800.0);
        v.env.key_on();
        v.lsax = v.ssa;   // [spx "Key ON ... copies Voice Start Address to Voice Repeat Address"]
        v.lsax_written = false;
        v.kon_t = t_;
        v.keyed = true;
        v.h1 = v.h2 = 0;
        v.q.fill(0);
        v.rd = v.wr = 0;
        v.counter = 0;
        v.outx = 0;
        k.endx &= ~(1u << vi);
        enter_block(v, v.ssa);
    }
}

void Spu2MixerImpl::key_off(int c, uint32_t mask) {
    for (int vi = 0; vi < 24; ++vi)
        if (mask & (1u << vi)) {
            Voice& v = core_[c].v[vi];
            if (stale_log_ && in_stream_ring(v.block & kMask))
                std::fprintf(stderr, "spu2: key off core %d voice %d at 0x%05X t %.2f s (VBlank ~%.0f)\n", c, vi, v.block & kMask,
                             double(t_) / 48000.0, double(t_) / 800.0);
            v.env.key_off();
        }
}

// ---------------------------------------------------------------------------------------------------------------
// Reverb [spx "SPU Reverb Formula"] at 24 kHz on the core's work area.

void Spu2MixerImpl::reverb_step(Core& k, int32_t in_l, int32_t in_r, int32_t& out_l, int32_t& out_r) {
    const uint32_t esa = (k.esa >> 1) & 0xFFFFF, eea = (k.eea >> 1) & 0xFFFFF;
    out_l = out_r = 0;
    if (eea < esa) return;
    const int64_t size = static_cast<int64_t>(eea) - esa + 1;
    const bool en = k.fx_enable;
    const auto addr = [&](int64_t off) -> uint32_t {
        int64_t r = (static_cast<int64_t>(k.rev_cur) - esa + off) % size;
        if (r < 0) r += size;
        return static_cast<uint32_t>(esa + r);
    };
    const auto rd = [&](int64_t off) -> int32_t {
        const uint32_t a = addr(off);
        if (en) touch((a * 2) & kMask);
        return static_cast<int16_t>(rd16(a));
    };
    const auto wr = [&](int64_t off, int32_t v) {
        const uint32_t a = addr(off);
        touch((a * 2) & kMask);
        wr16(a, clamp16(v));
    };
    const auto vol = [&](int i) -> int32_t { return static_cast<int16_t>(k.rv[i]); };
    const auto ofs = [&](int i) -> int64_t { return static_cast<int64_t>(k.rv[i]) * 4; };   // 8-byte units -> words
    const auto mul = [](int32_t a, int32_t b) -> int32_t { return (a * b) >> 15; };

    const int32_t lin = mul(vol(vLIN), in_l), rin = mul(vol(vRIN), in_r);
    if (en) {
        const auto same_diff = [&](int m, int d, int32_t in) {
            const int32_t prev = rd(ofs(m) - 1);
            wr(ofs(m), mul(in + mul(rd(ofs(d)), vol(vWALL)) - prev, vol(vIIR)) + prev);
        };
        same_diff(mLSAME, dLSAME, lin);
        same_diff(mRSAME, dRSAME, rin);
        same_diff(mLDIFF, dRDIFF, lin);
        same_diff(mRDIFF, dLDIFF, rin);
    }
    int32_t l = mul(vol(vCOMB1), rd(ofs(mLCOMB1))) + mul(vol(vCOMB2), rd(ofs(mLCOMB2))) +
                mul(vol(vCOMB3), rd(ofs(mLCOMB3))) + mul(vol(vCOMB4), rd(ofs(mLCOMB4)));
    int32_t r = mul(vol(vCOMB1), rd(ofs(mRCOMB1))) + mul(vol(vCOMB2), rd(ofs(mRCOMB2))) +
                mul(vol(vCOMB3), rd(ofs(mRCOMB3))) + mul(vol(vCOMB4), rd(ofs(mRCOMB4)));
    const auto apf = [&](int32_t x, int m, int d, int v) -> int32_t {
        const int32_t t = rd(ofs(m) - ofs(d));
        x = clamp16(x - mul(vol(v), t));
        if (en) wr(ofs(m), x);
        return clamp16(mul(x, vol(v)) + t);
    };
    l = apf(l, mLAPF1, dAPF1, vAPF1);
    r = apf(r, mRAPF1, dAPF1, vAPF1);
    l = apf(l, mLAPF2, dAPF2, vAPF2);
    r = apf(r, mRAPF2, dAPF2, vAPF2);
    out_l = clamp16(l);
    out_r = clamp16(r);
}

// ---------------------------------------------------------------------------------------------------------------
// One output sample

void Spu2MixerImpl::render_one() {
    const uint32_t pos = static_cast<uint32_t>(t_ & 511), hi = static_cast<uint32_t>(t_ & 63);
    for (int c = 0; c < 2; ++c) {
        Core& k = core_[c];
        noise_tick(k);
        if (k.mvol[0].sweeping()) k.mvol[0].tick();
        if (k.mvol[1].sweeping()) k.mvol[1].tick();
        int32_t mix[4] = {0, 0, 0, 0};
        int32_t prev = 0;   // the previous voice's OUTX before this sample (PMON, 1 Ts before)
        for (int vi = 0; vi < 24; ++vi) {
            const int32_t old = k.v[vi].outx;
            voice_sample(k, vi, prev, mix);
            prev = old;
        }
        const int32_t vdl = clamp16(mix[0]), vdr = clamp16(mix[1]), vwl = clamp16(mix[2]), vwr = clamp16(mix[3]);
        // Output rings [SPU2 2.3]: voice 1 / 3 OUTX, then the mixed voice buses (MEMOUT).
        const uint32_t vo = c ? 0xC00 : 0x400, mo = c ? 0x1800 : 0x1000, mi = c ? 0x2400 : 0x2000;
        wr16(vo + pos, k.v[1].outx);
        wr16(vo + 0x200 + pos, k.v[3].outx);
        wr16(mo + pos, vdl);
        wr16(mo + 0x200 + pos, vdr);
        wr16(mo + 0x400 + pos, vwl);
        wr16(mo + 0x600 + pos, vwr);
        // Sound data input (MEMIN) through BVOL; core 1's external input = core 0's output through AVOL [SPU2 2.4].
        const int32_t min_l = (static_cast<int16_t>(rd16(mi + pos)) * k.bvol[0]) >> 15;
        const int32_t min_r = (static_cast<int16_t>(rd16(mi + 0x200 + pos)) * k.bvol[1]) >> 15;
        int32_t sin_l = 0, sin_r = 0;
        int64_t hsin_l = 0, hsin_r = 0;   // audioq: core 0's unclamped output through AVOL (the host's path)
        if (c == 1) {
            wr16(0x800 + pos, core_[0].out_l);
            wr16(0xA00 + pos, core_[0].out_r);
            sin_l = (core_[0].out_l * k.avol[0]) >> 15;
            sin_r = (core_[0].out_r * k.avol[1]) >> 15;
            hsin_l = (core_[0].host_l * k.avol[0]) >> 15;
            hsin_r = (core_[0].host_r * k.avol[1]) >> 15;
        }
        const uint16_t m = k.mmix;   // sdmacro SD_MMIX_*: 0x800 MSNDL .. 0x100 MSNDER, 0x80 MINL .., 0x8 SINL ..
        const int32_t dry_l = clamp16((m & 0x800 ? vdl : 0) + (m & 0x80 ? min_l : 0) + (m & 0x8 ? sin_l : 0));
        const int32_t dry_r = clamp16((m & 0x400 ? vdr : 0) + (m & 0x40 ? min_r : 0) + (m & 0x4 ? sin_r : 0));
        const int32_t wet_l = clamp16((m & 0x200 ? vwl : 0) + (m & 0x20 ? min_l : 0) + (m & 0x2 ? sin_l : 0));
        const int32_t wet_r = clamp16((m & 0x100 ? vwr : 0) + (m & 0x10 ? min_r : 0) + (m & 0x1 ? sin_r : 0));
        // Effect: 48 -> 24 kHz FIR, the reverb on even samples, 24 -> 48 kHz FIR, EVOL.
        k.fin_l[hi] = static_cast<int16_t>(wet_l);
        k.fin_r[hi] = static_cast<int16_t>(wet_r);
        int32_t rev_l = 0, rev_r = 0;
        const bool active = k.fx_enable || k.evol[0] != 0 || k.evol[1] != 0;
        if ((t_ & 1) == 0) {
            int32_t ol = 0, orr = 0;
            if (active) {
                int32_t il = 0, ir = 0;
                for (int j = 0; j < 39; ++j) {
                    if (kReverbFir[j] == 0) continue;
                    il += kReverbFir[j] * k.fin_l[(t_ - j) & 63];
                    ir += kReverbFir[j] * k.fin_r[(t_ - j) & 63];
                }
                reverb_step(k, clamp16(il >> 15), clamp16(ir >> 15), ol, orr);
            }
            k.fout_l[hi] = static_cast<int16_t>(ol);
            k.fout_r[hi] = static_cast<int16_t>(orr);
            const uint32_t esa = (k.esa >> 1) & 0xFFFFF, eea = (k.eea >> 1) & 0xFFFFF;
            k.rev_cur = (k.rev_cur + 1 > eea || k.rev_cur < esa) ? esa : k.rev_cur + 1;
        } else {
            k.fout_l[hi] = 0;
            k.fout_r[hi] = 0;
        }
        if (active) {
            int32_t yl = 0, yr = 0;
            for (int j = 0; j < 39; ++j) {
                if (kReverbFir[j] == 0) continue;
                yl += kReverbFir[j] * k.fout_l[(t_ - j) & 63];
                yr += kReverbFir[j] * k.fout_r[(t_ - j) & 63];
            }
            rev_l = clamp16(yl >> 14);
            rev_r = clamp16(yr >> 14);
        }
        int32_t out_l = clamp16(dry_l + ((rev_l * k.evol[0]) >> 15));
        int32_t out_r = clamp16(dry_r + ((rev_r * k.evol[1]) >> 15));
        out_l = clamp16((out_l * k.mvol[0].level) >> 15);
        out_r = clamp16((out_r * k.mvol[1].level) >> 15);
        if (k.mute) out_l = out_r = 0;
        k.out_l = out_l;
        k.out_r = out_r;
        {   // audioq: the host's path, the same sums without the dry and output clamps (the wet path is the hardware's)
            const int64_t hd_l = (m & 0x800 ? int64_t(mix[0]) : 0) + (m & 0x80 ? min_l : 0) + (m & 0x8 ? hsin_l : 0);
            const int64_t hd_r = (m & 0x400 ? int64_t(mix[1]) : 0) + (m & 0x40 ? min_r : 0) + (m & 0x4 ? hsin_r : 0);
            k.host_l = k.mute ? 0 : ((hd_l + ((rev_l * k.evol[0]) >> 15)) * k.mvol[0].level) >> 15;
            k.host_r = k.mute ? 0 : ((hd_r + ((rev_r * k.evol[1]) >> 15)) * k.mvol[1].level) >> 15;
        }
    }
    // The rings were accessed at `pos` this sample.
    for (int c = 0; c < 2; ++c) {
        Core& k = core_[c];
        if (k.irq_enable && sd::ring_irq_pos(k.irqa) == static_cast<int>(pos)) {
            k.irq_enable = false;
            pending_ |= 1u << c;
        }
    }
    // Output frame (core 1).
    size_t i = (out_head_ + out_size_) % out_cap_;
    if (out_size_ == out_cap_) {   // full: drop the oldest
        out_head_ = (out_head_ + 1) % out_cap_;
        ++dropped_;
    } else {
        ++out_size_;
    }
    // audioq: the host's output through the limiter (the hardware's clamped value with hard_clip_)
    if (hard_clip_) {
        out_[2 * i] = static_cast<int16_t>(core_[1].out_l);
        out_[2 * i + 1] = static_cast<int16_t>(core_[1].out_r);
    } else {
        limiter_.step(core_[1].host_l, core_[1].host_r, out_[2 * i], out_[2 * i + 1]);
    }
    ++t_;
}

// ---------------------------------------------------------------------------------------------------------------
// libsd-level writes and reads

void Spu2MixerImpl::set_param(uint64_t t, uint16_t entry, uint16_t value) {
    advance(t);
    regs_[entry] = value;
    const uint16_t p = entry & 0xFF00;
    if (p <= sd::VP_VOLXR && !(entry & 0x80)) {
        Voice& v = voice(entry);
        switch (p) {
        case sd::VP_VOLL: v.vol[0].write(value); break;
        case sd::VP_VOLR: v.vol[1].write(value); break;
        case sd::VP_PITCH: v.pitch = value; break;
        case sd::VP_ADSR1: v.env.adsr1 = value; break;
        case sd::VP_ADSR2: v.env.adsr2 = value; break;
        default: break;   // ENVX / VOLX: read-only here
        }
        return;
    }
    Core& k = core_[entry & 1];
    switch (entry & 0xFFFE) {
    case sd::P_MMIX: k.mmix = value; break;
    case sd::P_MVOLL: k.mvol[0].write(value); break;
    case sd::P_MVOLR: k.mvol[1].write(value); break;
    case sd::P_EVOLL: k.evol[0] = static_cast<int16_t>(value); break;
    case sd::P_EVOLR: k.evol[1] = static_cast<int16_t>(value); break;
    case sd::P_AVOLL: k.avol[0] = static_cast<int16_t>(value); break;
    case sd::P_AVOLR: k.avol[1] = static_cast<int16_t>(value); break;
    case sd::P_BVOLL: k.bvol[0] = static_cast<int16_t>(value); break;
    case sd::P_BVOLR: k.bvol[1] = static_cast<int16_t>(value); break;
    default: break;
    }
}

void Spu2MixerImpl::set_addr(uint64_t t, uint16_t entry, uint32_t addr) {
    advance(t);
    const uint16_t pv = entry & 0xFFC0;
    if (pv == sd::VA_SSA || pv == sd::VA_LSAX || pv == sd::VA_NAX) {
        Voice& v = voice(entry);
        if (pv == sd::VA_SSA) {
            v.ssa = floor16(addr);
        } else if (pv == sd::VA_LSAX) {
            if (v.keyed && t_ < v.kon_t + 4) return;   // within 4 Ts of the key-on: disregarded [SPU2 2.1.1]
            v.lsax = floor16(addr);
            v.lsax_written = true;
        } else {
            enter_block(v, addr);   // [L] the decoder continues at the written block
        }
        return;
    }
    regs_[entry] = addr;
    Core& k = core_[entry & 1];
    switch (entry & 0xFF00) {
    case sd::A_ESA:
        k.esa = floor16(addr);
        k.rev_cur = (k.esa >> 1) & 0xFFFFF;   // writing ESA sets the buffer address [spx]
        break;
    case sd::A_EEA: k.eea = (addr & 0x1E0000) | 0x1FFFF; break;   // libsd writes the upper bits only [libsd 0x37D4]
    case sd::A_TSA: k.tsa = floor16(addr); break;
    case sd::A_IRQA: k.irqa = floor16(addr); break;
    default: break;
    }
}

void Spu2MixerImpl::set_switch(uint64_t t, uint16_t entry, uint32_t value) {
    advance(t);
    value &= 0xFFFFFF;
    regs_[entry] = value;
    const int c = entry & 1;
    Core& k = core_[c];
    switch (entry & 0xFF00) {
    case sd::S_PMON: k.pmon = value; break;
    case sd::S_NON: k.non = value; break;
    case sd::S_KON: k.kon = value; key_on(c, value); break;
    case sd::S_KOFF: k.koff = value; key_off(c, value); break;
    case sd::S_VMIXL: k.vmix[0] = value; break;
    case sd::S_VMIXR: k.vmix[1] = value; break;
    case sd::S_VMIXEL: k.vmix[2] = value; break;
    case sd::S_VMIXER: k.vmix[3] = value; break;
    default: break;   // ENDX is the hardware's
    }
}

void Spu2MixerImpl::set_core_attr(uint64_t t, uint16_t entry, uint16_t value) {
    advance(t);
    regs_[static_cast<uint16_t>(0x8000 | entry)] = value;
    Core& k = core_[entry & 1];
    switch (entry & 0x3E) {
    case sd::C_EFFECT_ENABLE: k.fx_enable = value != 0; break;
    case sd::C_IRQ_ENABLE: k.irq_enable = value != 0; break;
    case sd::C_MUTE_ENABLE: k.mute = value != 0; break;
    case sd::C_NOISE_CLK: k.noise_clk = value & 0x3F; break;
    case sd::C_SPDIF_MODE: k.spdif = value; break;
    default: break;
    }
}

// sceSdSetEffectAttr [libsd 0xC2C]: the mode's preset; ECHO (7) / DELAY (8) take delay and feedback; EVOL = depth;
// ESA = the EEA end - size + 1. Modes outside 0..9 (RWA's -1 for ReverbType 0) are refused (libsd returns -100).
namespace {
std::array<ReverbPreset, 10> g_presets{};
bool g_presets_loaded = false;
bool g_presets_warned = false;
}  // namespace

// LIBSD.IRX: an ELF; .data's section header gives its file offset. The span used is .data 0x4A68..0x4A90 + 10 * 0x44
// (sizes, then rows), FNV-1a 64 0x896325285530ecfb in the retail module [C audio1 2026-11-06].
bool load_libsd_reverb_presets(const char* path) {
    std::FILE* f = path ? b3io::fopen(b3io::u8path(path), "rb") : nullptr;   // fsrobust: a UTF-8 path
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    return load_libsd_reverb_presets_bytes(d);
}
// iso1: the module's bytes as read (the disc image's too: platform_rwa.cpp reads it through b3asset/discfs.h)
bool load_libsd_reverb_presets_bytes(const std::vector<uint8_t>& d) {
    auto u16 = [&](size_t o) { return static_cast<uint32_t>(d[o] | d[o + 1] << 8); };
    auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
    if (d.size() < 0x34 || u32(0) != 0x464C457Fu) return false;
    // fsrobust: the offsets in 64 bits (a corrupt header's 32-bit sums wrapped past the checks); a section header is
    // read up to +0x14
    const uint64_t shoff = u32(0x20), shent = u16(0x2E), shnum = u16(0x30), shstr = u16(0x32);
    if (shent < 0x14 || shoff + shnum * shent > d.size() || shstr >= shnum) return false;
    const uint64_t strtab = u32(size_t(shoff + shstr * shent + 0x10));
    uint64_t data_addr = 0, data_off = 0;
    bool found = false;
    for (uint64_t k = 0; k < shnum; ++k) {
        const size_t h = size_t(shoff + k * shent);
        const uint64_t name = strtab + u32(h);
        if (name + 6 <= d.size() && std::memcmp(&d[size_t(name)], ".data", 6) == 0) {
            data_addr = u32(h + 0x0C);
            data_off = u32(h + 0x10);
            found = true;
        }
    }
    if (!found) return false;
    if (0x4A68u < data_addr) return false;
    const uint64_t lo64 = data_off + (0x4A68u - data_addr), hi64 = data_off + (0x4A90u - data_addr) + 10 * 0x44;
    if (hi64 > d.size()) return false;
    const size_t lo = size_t(lo64), hi = size_t(hi64);
    uint64_t fnv = 0xcbf29ce484222325ull;
    for (size_t o = lo; o < hi; ++o) fnv = (fnv ^ d[o]) * 0x100000001b3ull;
    if (fnv != 0x896325285530ecfbull) return false;
    for (uint32_t m = 0; m < 10; ++m) {
        g_presets[m].size8 = u32(lo + 4 * m);
        const size_t row = size_t(data_off + (0x4A90u - data_addr) + m * 0x44 + 4);
        for (int r = 0; r < 32; ++r) g_presets[m].r[r] = static_cast<uint16_t>(u16(row + 2 * r));
    }
    g_presets_loaded = true;
    return true;
}

bool libsd_reverb_presets_loaded() { return g_presets_loaded; }

void Spu2MixerImpl::set_effect_attr(uint64_t t, int core, const Spu2EffectAttr& a) {
    advance(t);
    Core& k = core_[core & 1];
    k.fx = a;
    const uint32_t mode = static_cast<uint32_t>(a.mode) & 0xFF;
    if (mode >= 10) return;
    static const ReverbPreset kSilent{2, {}};   // without libsd's presets: a silent wet path (work area as OFF's)
    if (!g_presets_loaded && !g_presets_warned) {
        g_presets_warned = true;
        std::fprintf(stderr, "spu2: libsd reverb presets not loaded (load_libsd_reverb_presets); reverb silent\n");
    }
    const ReverbPreset& pr = g_presets_loaded ? g_presets[mode] : kSilent;
    std::copy(std::begin(pr.r), std::end(pr.r), k.rv.begin());
    if (mode == 7 || mode == 8) {
        const uint32_t d1 = static_cast<uint32_t>(a.delay) + 1, d5 = d1 << 5;
        k.rv[mLSAME] = static_cast<uint16_t>((d1 << 6) - k.rv[dAPF1]);
        k.rv[mRSAME] = static_cast<uint16_t>(d5 - k.rv[dAPF2]);
        k.rv[dLSAME] = static_cast<uint16_t>(k.rv[dRSAME] + d5);
        k.rv[mLCOMB1] = static_cast<uint16_t>(k.rv[mRCOMB1] + d5);
        k.rv[mLAPF1] = static_cast<uint16_t>(k.rv[mLAPF2] + d5);
        k.rv[mRAPF1] = static_cast<uint16_t>(k.rv[mRAPF2] + d5);
        k.rv[vWALL] = static_cast<uint16_t>(static_cast<uint32_t>(a.feedback) * 258);
    }
    k.evol[0] = a.depth_l;
    k.evol[1] = a.depth_r;
    k.esa = k.eea - (pr.size8 * 8 - 1);
    k.rev_cur = (k.esa >> 1) & 0xFFFFF;
}

void Spu2MixerImpl::voice_trans(uint64_t t, int, uint32_t spu_addr, const uint8_t* src, uint32_t n) {
    advance(t);
    for (uint32_t k = 0; k < n; ++k) ram_[(spu_addr + k) & kMask] = src ? src[k] : 0;
    if (stale_log_ && n) {
        const char* e = std::getenv("B3_SPU_TRACE_RING");
        unsigned lo = 0, hi = 0; double from = 0, to = 0;
        if (e) std::sscanf(e, "%x,%x,%lf,%lf", &lo, &hi, &from, &to);
        const double ts = double(t_) / 48000.0;
        if (hi && (spu_addr & kMask) < hi && ((spu_addr + n) & kMask) > lo && ts >= from && ts < to)
            std::fprintf(stderr, "spu2 trace: t %.3f upload 0x%05X..0x%05X\n", ts, spu_addr & kMask, (spu_addr + n) & kMask);
    }
    if (stale_log_ && n)
        for (uint32_t b = (spu_addr & kMask) >> 4, e = ((spu_addr + n - 1) & kMask) >> 4;; b = (b + 1) & (kMask >> 4)) {
            ++wgen_[b];
            if (b == e) break;
        }
    for (int c = 0; c < 2; ++c) {   // the transfer's accesses and its end pointer TSA + size + 1 words [SPU2 2.7]
        Core& k = core_[c];
        if (!k.irq_enable) continue;
        const uint32_t rel = (k.irqa - spu_addr) & kMask;
        if (rel < n || rel == n + 2) {
            k.irq_enable = false;
            pending_ |= 1u << c;
        }
    }
}

uint16_t Spu2MixerImpl::get_param(uint64_t t, uint16_t entry) {
    advance(t);
    const uint16_t p = entry & 0xFF00;
    if (p <= sd::VP_VOLXR && !(entry & 0x80)) {
        const Voice& v = voice(entry);
        switch (p) {
        case sd::VP_VOLL: return v.vol[0].reg;
        case sd::VP_VOLR: return v.vol[1].reg;
        case sd::VP_PITCH: return v.pitch;
        case sd::VP_ADSR1: return v.env.adsr1;
        case sd::VP_ADSR2: return v.env.adsr2;
        case sd::VP_ENVX: return static_cast<uint16_t>(v.env.level);
        case sd::VP_VOLXL: return static_cast<uint16_t>(v.vol[0].level);
        case sd::VP_VOLXR: return static_cast<uint16_t>(v.vol[1].level);
        default: break;
        }
    }
    const Core& k = core_[entry & 1];
    switch (entry & 0xFFFE) {   // EVOL also follows sceSdSetEffectAttr's depth
    case sd::P_EVOLL: return static_cast<uint16_t>(k.evol[0]);
    case sd::P_EVOLR: return static_cast<uint16_t>(k.evol[1]);
    default: break;
    }
    const auto it = regs_.find(entry);
    return it == regs_.end() ? 0 : static_cast<uint16_t>(it->second);
}

uint32_t Spu2MixerImpl::get_addr(uint64_t t, uint16_t entry) {
    advance(t);
    const uint16_t pv = entry & 0xFFC0;
    if (pv == sd::VA_SSA || pv == sd::VA_LSAX || pv == sd::VA_NAX) {
        const Voice& v = voice(entry);
        return pv == sd::VA_SSA ? v.ssa : (pv == sd::VA_LSAX ? v.lsax : v.nax);
    }
    const Core& k = core_[entry & 1];
    switch (entry & 0xFF00) {
    case sd::A_ESA: return k.esa;
    case sd::A_EEA: return k.eea;
    case sd::A_TSA: return k.tsa;
    case sd::A_IRQA: return k.irqa;
    default: break;
    }
    const auto it = regs_.find(entry);
    return it == regs_.end() ? 0 : it->second;
}

uint32_t Spu2MixerImpl::get_switch(uint64_t t, uint16_t entry) {
    advance(t);
    const Core& k = core_[entry & 1];
    switch (entry & 0xFF00) {
    case sd::S_ENDX: return k.endx;
    case sd::S_PMON: return k.pmon;
    case sd::S_NON: return k.non;
    case sd::S_VMIXL: return k.vmix[0];
    case sd::S_VMIXR: return k.vmix[1];
    case sd::S_VMIXEL: return k.vmix[2];
    case sd::S_VMIXER: return k.vmix[3];
    default: break;
    }
    const auto it = regs_.find(entry);
    return it == regs_.end() ? 0 : it->second;
}

}  // namespace b3audio
