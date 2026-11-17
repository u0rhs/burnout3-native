// b3modtool audio: PS-ADPCM (decode as the port's SPU2 mixer does, and an encoder), WAV, and the game's two audio
// containers (docs/modding/file-formats/audio.md): RWS streams (chunks 0x80D / 0x80E / 0x80F: segments x sub-streams,
// each channel in interleave-sized runs per block) and AWD / HWD / LWD wave banks (a memory image of the RwaWaveDict,
// then the waves' data). An import writes the same structure: only the changed items' lengths, and the offsets of
// what follows them, move. Frames of the original that decode to exactly the wanted samples are kept as they are, so
// an unedited WAV gives back the original bytes.
#include "b3modtool/modtool.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace b3modtool {

namespace {

bool fail(std::string* why, std::string what) { if (why) *why = std::move(what); return false; }
std::uint32_t rd32(const Buf& b, std::size_t o) { return std::uint32_t(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | std::uint32_t(b[o + 3]) << 24); }
std::uint16_t rd16(const Buf& b, std::size_t o) { return std::uint16_t(b[o] | b[o + 1] << 8); }
void wr32(Buf& b, std::size_t o, std::uint32_t v) { for (int i = 0; i < 4; ++i) b[o + std::size_t(i)] = std::uint8_t(v >> (8 * i)); }
void wr16(Buf& b, std::size_t o, std::uint16_t v) { b[o] = std::uint8_t(v); b[o + 1] = std::uint8_t(v >> 8); }
std::size_t align16(std::size_t n) { return (n + 15) & ~std::size_t(15); }
std::size_t align80(std::size_t n) { return (n + 0x7F) & ~std::size_t(0x7F); }
std::int32_t clamp16(std::int32_t v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

constexpr std::int32_t kF0[5] = {0, 60, 115, 98, 122}, kF1[5] = {0, 0, -52, -55, -60};
// a stream's padding frame: silence, flags 0x02 (every stream frame on the disc has 0x02; audio.md §6)
constexpr std::uint8_t kPadFrame[16] = {0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

std::string cstr_at(const Buf& b, std::size_t o, std::size_t* len = nullptr) {
    std::size_t e = o;
    while (e < b.size() && b[e]) ++e;
    if (len) *len = e < b.size() ? e - o : std::string::npos;
    return std::string(b.begin() + std::ptrdiff_t(std::min(o, b.size())), b.begin() + std::ptrdiff_t(e));
}

std::string safe_name(const std::string& s) {
    std::string o;
    for (char c : s) o += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return o.substr(0, 40);
}

bool read_rws(const Buf& b, AudioFile& af, std::string* why) {
    af = AudioFile{};
    af.kind = AudioFile::Rws;
    if (b.size() < 0x2C + 0x2C || rd32(b, 0) != 0x80D) return fail(why, "not an RWS stream (no chunk 0x80D)");
    if (std::uint64_t(rd32(b, 4)) + 12 != b.size()) return fail(why, "the 0x80D chunk is not the whole file (several streams in one file are not handled)");
    if (rd32(b, 12) != 0x80E) return fail(why, "no stream header chunk (0x80E) first in 0x80D");
    const std::size_t hdr_end = 24 + std::size_t(rd32(b, 16));
    if (hdr_end > b.size()) return fail(why, "the 0x80E chunk runs past the file");
    const std::size_t d = 0x2C;
    af.desc = d;
    bool bad = false;
    auto u = [&](std::size_t o) -> std::uint32_t { if (o + 4 > hdr_end) { bad = true; return 0; } return rd32(b, o); };
    af.nseg = u(d + 0xC);
    af.nsub = u(d + 0x14);
    af.block = u(d + 0x20);
    af.data_at = u(d + 0x24);
    if (bad || af.nseg == 0 || af.nseg > 100000 || af.nsub == 0 || af.nsub > 16 || af.block == 0)
        return fail(why, "the stream description is not understood");
    if (af.data_at < hdr_end + 12 || af.data_at > b.size() || rd32(b, af.data_at - 12) != 0x80F ||
        std::uint64_t(rd32(b, af.data_at - 8)) + af.data_at != b.size())
        return fail(why, "the audio data chunk (0x80F) is not where the description says, or not to the end of the file");
    std::size_t cur = d + 0x2C;
    auto skip_name = [&](std::string* out) {
        std::size_t len = 0;
        std::string s = cstr_at(b, cur, &len);
        if (len == std::string::npos || cur + len >= hdr_end) { bad = true; return; }
        if (out) *out = s;
        cur += align16(len + 1);
    };
    if (u(d)) cur += 16;
    if (u(d + 4)) skip_name(&af.name);
    const std::size_t seg0 = cur;
    cur += 32 * af.nseg;
    af.segs.resize(af.nseg);
    for (std::size_t i = 0; i < af.nseg; ++i) {
        AudioFile::Seg& s = af.segs[i];
        s.at = seg0 + 32 * i;
        s.size = u(s.at + 0x18);
        s.offset = u(s.at + 0x1C);
        if (u(s.at + 0x10) || u(s.at + 0x14)) return fail(why, "segment markers: not handled (none on the US disc)");
    }
    for (AudioFile::Seg& s : af.segs) {
        if (!u(s.at + 0xC)) return fail(why, "a segment without its per-sub-stream sizes: not handled");
        s.sizes_at = cur;
        cur += 4 * af.nsub;
    }
    for (AudioFile::Seg& s : af.segs) if (u(s.at)) cur += 16;
    std::vector<std::string> seg_names(af.nseg);
    for (std::size_t i = 0; i < af.nseg; ++i) if (u(af.segs[i].at + 4)) skip_name(&seg_names[i]);
    const std::size_t sub0 = cur;
    cur += 40 * af.nsub;
    af.subs.resize(af.nsub);
    std::vector<int> rate(af.nsub), chans(af.nsub);
    for (std::size_t i = 0; i < af.nsub; ++i) {
        AudioFile::Sub& f = af.subs[i];
        f.at = sub0 + 40 * i;
        f.bytes_per_block = u(f.at + 0x10);
        f.interleave = f.at + 0x1A <= hdr_end ? rd16(b, f.at + 0x18) : 0;
        if (f.at + 0x1C > hdr_end || rd16(b, f.at + 0x1A) != 16) return fail(why, "a sub-stream whose frame is not 16 bytes (not PS-ADPCM)");
        f.slice = u(f.at + 0x24);
    }
    for (AudioFile::Sub& f : af.subs) {
        if (!u(f.at + 0x14)) return fail(why, "a sub-stream without a wave format");
        f.fmt_at = cur;
        if (cur + 0x2C > hdr_end) return fail(why, "the stream description runs past its chunk");
        cur += 0x30;
    }
    if (bad) return fail(why, "the stream description runs past its chunk");
    for (std::size_t i = 0; i < af.nsub; ++i) {
        const AudioFile::Sub& f = af.subs[i];
        rate[i] = int(rd32(b, f.fmt_at));
        chans[i] = b[f.fmt_at + 0xD];
        if (b[f.fmt_at + 0xC] != 4 || chans[i] < 1 || chans[i] > 2) return fail(why, "a sub-stream that is not 4-bit mono / stereo");
        if (f.interleave == 0 || f.interleave % 16 || std::size_t(f.interleave) * std::size_t(chans[i]) != f.bytes_per_block ||
            std::size_t(f.slice) + f.bytes_per_block > af.block)
            return fail(why, "a sub-stream's slice / channel interleave does not fit its blocks");
    }
    const std::size_t data_size = b.size() - af.data_at;
    for (std::size_t i = 0; i < af.nseg; ++i) {
        const AudioFile::Seg& s = af.segs[i];
        if (s.size % af.block || std::uint64_t(s.offset) + s.size > data_size) return fail(why, "segment " + std::to_string(i) + " is not whole blocks inside the data");
        for (std::size_t k = 0; k < af.nsub; ++k) {
            AudioItem it;
            it.index = i;
            it.sub = k;
            it.name = seg_names[i];
            it.rate = rate[k];
            it.channels = chans[k];
            it.bytes = rd32(b, s.sizes_at + 4 * k);
            const std::size_t cap = std::size_t(s.size / af.block) * af.subs[k].interleave * std::size_t(chans[k]);
            if (it.bytes > cap || it.bytes % (16 * std::size_t(chans[k]))) return fail(why, "segment " + std::to_string(i) + ": its size table does not fit its blocks");
            af.items.push_back(it);
        }
    }
    return true;
}

bool read_awd(const Buf& b, AudioFile& af, std::string* why) {
    af = AudioFile{};
    af.kind = AudioFile::Awd;
    if (!is_audio_bank(b.data(), b.size())) return fail(why, "not a wave bank (.AWD / .HWD / .LWD)");
    af.header_size = rd32(b, 8);
    const std::size_t d = rd32(b, 0xC), hs = af.header_size;
    auto in_hdr = [&](std::size_t o, std::size_t n) { return o + n <= hs; };
    if (!in_hdr(d, 0x30)) return fail(why, "the wave dictionary is outside the header");
    if (rd32(b, d + 4) && in_hdr(rd32(b, d + 4), 1)) af.name = cstr_at(b, rd32(b, d + 4));
    std::size_t node = rd32(b, d + 0x10);
    const std::size_t last = rd32(b, d + 0xC);
    const std::size_t data_size = rd32(b, 0x14);
    for (std::size_t n = 0; node; ++n) {
        if (n > 10000 || !in_hdr(node, 12)) return fail(why, "the wave list is damaged");
        const std::size_t w = rd32(b, node + 8);
        if (!in_hdr(w, 0x5C)) return fail(why, "a wave is outside the header");
        AudioItem it;
        it.index = af.items.size();
        if (rd32(b, w + 4) && in_hdr(rd32(b, w + 4), 1)) it.name = cstr_at(b, rd32(b, w + 4));
        it.rate = int(rd32(b, w + 0x10));
        it.channels = b[w + 0x1D];
        it.bytes = rd32(b, w + 0x18);
        it.flags = rd32(b, w + 0x54);
        const std::size_t off = rd32(b, w + 0x4C);
        if (b[w + 0x1C] != 4 || it.channels != 1 || rd32(b, w + 0x14) != 0) return fail(why, "wave " + it.name + ": not 4-bit mono PS-ADPCM");
        if (it.bytes % 16 || off + it.bytes > data_size) return fail(why, "wave " + it.name + ": its data is outside the bank");
        it.loops = it.bytes >= 16 && (b[hs + off + 1] & 4);
        af.wave_at.push_back(w);
        af.items.push_back(it);
        if (node == last) break;
        node = rd32(b, node + 4);
    }
    return true;
}

// a WAV's samples on the item's rate and channel count
std::vector<std::vector<std::int16_t>> to_item(const Pcm& p, const AudioItem& it, std::vector<std::string>& notes, const std::string& label) {
    const std::size_t n = p.frames();
    std::vector<std::vector<std::int16_t>> ch(std::size_t(it.channels));
    for (int c = 0; c < it.channels; ++c) {
        ch[std::size_t(c)].resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            std::int32_t v;
            if (p.channels == it.channels) v = p.s[i * std::size_t(p.channels) + std::size_t(c)];
            else if (p.channels == 1) v = p.s[i];
            else { v = 0; for (int k = 0; k < p.channels; ++k) v += p.s[i * std::size_t(p.channels) + std::size_t(k)]; v /= p.channels; }
            ch[std::size_t(c)][i] = std::int16_t(v);
        }
    }
    if (p.channels != it.channels)
        notes.push_back(label + ": " + std::to_string(p.channels) + " channel(s) " + (p.channels == 1 ? "copied to" : "mixed to") + " " + std::to_string(it.channels));
    if (p.rate != it.rate && p.rate > 0 && n) {
        const std::size_t m = std::max<std::size_t>(1, std::size_t(std::llround(double(n) * it.rate / p.rate)));
        for (auto& c : ch) {
            std::vector<std::int16_t> o(m);
            for (std::size_t i = 0; i < m; ++i) {
                const double x = double(i) * p.rate / it.rate;
                const std::size_t a = std::min(std::size_t(x), n - 1), b2 = std::min(a + 1, n - 1);
                const double f = x - double(a);
                o[i] = std::int16_t(std::lround(c[a] * (1 - f) + c[b2] * f));
            }
            c.swap(o);
        }
        notes.push_back(label + ": resampled (linear) from " + std::to_string(p.rate) + " Hz to the item's " + std::to_string(it.rate) + " Hz");
    }
    return ch;
}

}  // namespace

// ---- PS-ADPCM ----

void adpcm_decode_frame(const std::uint8_t* f, AdpcmState& st, std::int16_t out[28]) {
    int shift = f[0] & 0xF, filter = f[0] >> 4;
    if (shift > 12) shift = 9;
    if (filter > 4) filter = 0;
    for (int i = 0; i < 28; ++i) {
        const int nib = (f[2 + i / 2] >> (4 * (i & 1))) & 0xF;
        const std::int32_t t = std::int32_t(std::int16_t(std::uint16_t(nib << 12))) >> shift;
        const std::int32_t s = clamp16(t + ((st.h1 * kF0[filter] + st.h2 * kF1[filter] + 32) >> 6));
        out[i] = std::int16_t(s);
        st.h2 = st.h1;
        st.h1 = s;
    }
}

std::vector<std::int16_t> adpcm_decode(const std::uint8_t* p, std::size_t n) {
    std::vector<std::int16_t> out(n / 16 * 28);
    AdpcmState st;
    for (std::size_t k = 0; k < n / 16; ++k) adpcm_decode_frame(p + 16 * k, st, &out[28 * k]);
    return out;
}

void adpcm_encode_frame(const std::int16_t in[28], std::uint8_t flags, AdpcmState& st, std::uint8_t out[16]) {
    std::int64_t best = INT64_MAX;
    int best_f = 0, best_s = 12;
    std::uint8_t best_n[28] = {};
    AdpcmState best_st = st;
    for (int f = 0; f < 5; ++f) {
        // the step from the open-loop residual (the input as the history)
        std::int32_t p1 = st.h1, p2 = st.h2, peak = 0;
        for (int i = 0; i < 28; ++i) {
            const std::int32_t r = in[i] - ((p1 * kF0[f] + p2 * kF1[f] + 32) >> 6);
            peak = std::max(peak, r < 0 ? -r : r);
            p2 = p1;
            p1 = in[i];
        }
        int pw = 0;   // step = 2^pw = 1 << (12 - shift)
        while (pw < 12 && (std::int64_t(7) << pw) < peak) ++pw;
        for (int d = -1; d <= 2; ++d) {
            const int q = pw + d;
            if (q < 0 || q > 12) continue;
            AdpcmState s = st;
            std::int64_t err = 0;
            std::uint8_t nib[28];
            for (int i = 0; i < 28 && err < best; ++i) {
                const std::int32_t pred = (s.h1 * kF0[f] + s.h2 * kF1[f] + 32) >> 6;
                const std::int32_t r = in[i] - pred;
                std::int32_t n = std::int32_t(std::lround(double(r) / double(1 << q)));
                n = std::max(-8, std::min(7, n));
                std::int32_t v = clamp16((n * (1 << q)) + pred);
                // a clamp may make the neighbour nibble better
                for (int alt = n - 1; alt <= n + 1; alt += 2) {
                    if (alt < -8 || alt > 7) continue;
                    const std::int32_t va = clamp16(alt * (1 << q) + pred);
                    if (std::abs(va - in[i]) < std::abs(v - in[i])) { n = alt; v = va; }
                }
                nib[i] = std::uint8_t(n & 0xF);
                err += std::int64_t(v - in[i]) * (v - in[i]);
                s.h2 = s.h1;
                s.h1 = v;
            }
            if (err < best) {
                best = err;
                best_f = f;
                best_s = 12 - q;
                std::memcpy(best_n, nib, 28);
                best_st = s;
            }
        }
    }
    out[0] = std::uint8_t(best_f << 4 | best_s);
    out[1] = flags;
    for (int i = 0; i < 14; ++i) out[2 + i] = std::uint8_t(best_n[2 * i] | best_n[2 * i + 1] << 4);
    st = best_st;
}

Buf adpcm_encode(const std::int16_t* pcm, std::size_t n, const std::uint8_t* orig, std::size_t orig_n, std::uint8_t def_flags, std::size_t* reused) {
    const std::size_t frames = std::max<std::size_t>(1, (n + 27) / 28), of = orig ? orig_n / 16 : 0;
    Buf out(frames * 16);
    AdpcmState st;
    std::size_t kept = 0;
    const std::uint8_t last_flag = of ? orig[16 * (of - 1) + 1] : def_flags;
    const std::uint8_t mid_flag = of >= 2 ? std::uint8_t(orig[16 * (of - 2) + 1] & ~4) : def_flags;
    for (std::size_t k = 0; k < frames; ++k) {
        std::int16_t want[28] = {};
        for (std::size_t i = 0; i < 28 && 28 * k + i < n; ++i) want[i] = pcm[28 * k + i];
        const std::uint8_t flags = k == frames - 1 ? last_flag : k + 1 < of ? orig[16 * k + 1] : of ? mid_flag : def_flags;
        if (k < of && orig[16 * k + 1] == flags) {
            AdpcmState s = st;
            std::int16_t got[28];
            adpcm_decode_frame(orig + 16 * k, s, got);
            if (std::memcmp(got, want, sizeof want) == 0) {
                std::memcpy(&out[16 * k], orig + 16 * k, 16);
                st = s;
                ++kept;
                continue;
            }
        }
        adpcm_encode_frame(want, flags, st, &out[16 * k]);
    }
    if (reused) *reused = kept;
    return out;
}

// ---- WAV ----

Buf write_wav(const Pcm& p) {
    const std::uint32_t data = std::uint32_t(p.s.size() * 2), ch = std::uint32_t(p.channels);
    Buf b(44 + data);
    std::memcpy(&b[0], "RIFF", 4);
    wr32(b, 4, 36 + data);
    std::memcpy(&b[8], "WAVEfmt ", 8);
    wr32(b, 16, 16);
    wr16(b, 20, 1);
    wr16(b, 22, std::uint16_t(ch));
    wr32(b, 24, std::uint32_t(p.rate));
    wr32(b, 28, std::uint32_t(p.rate) * 2 * ch);
    wr16(b, 32, std::uint16_t(2 * ch));
    wr16(b, 34, 16);
    std::memcpy(&b[36], "data", 4);
    wr32(b, 40, data);
    for (std::size_t i = 0; i < p.s.size(); ++i) wr16(b, 44 + 2 * i, std::uint16_t(p.s[i]));
    return b;
}

bool read_wav(const Buf& b, Pcm& out, std::string* why) {
    out = Pcm{};
    if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) return fail(why, "not a WAV file");
    int fmt = 0, bits = 0;
    std::size_t at = 12, data = 0, dlen = 0;
    bool have_fmt = false, have_data = false;
    while (at + 8 <= b.size()) {
        const std::size_t len = rd32(b, at + 4), body = at + 8;
        const std::size_t avail = std::min(len, b.size() - body);
        if (!std::memcmp(&b[at], "fmt ", 4) && avail >= 16) {
            fmt = rd16(b, body);
            out.channels = rd16(b, body + 2);
            out.rate = int(rd32(b, body + 4));
            bits = rd16(b, body + 14);
            if (fmt == 0xFFFE && avail >= 26) fmt = rd16(b, body + 24);
            have_fmt = true;
        } else if (!std::memcmp(&b[at], "data", 4)) {
            data = body;
            dlen = avail;   // a truncated data chunk: what is there
            have_data = true;
        }
        at = body + len + (len & 1);
    }
    if (!have_fmt || !have_data) return fail(why, "a WAV without its fmt / data chunk");
    if (out.channels < 1 || out.channels > 8 || out.rate <= 0) return fail(why, "a WAV with " + std::to_string(out.channels) + " channels at " + std::to_string(out.rate) + " Hz");
    const bool flt = fmt == 3;
    if (!(fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !(flt && (bits == 32 || bits == 64)))
        return fail(why, "WAV format " + std::to_string(fmt) + " with " + std::to_string(bits) + " bits: use 16-bit PCM (or 8 / 24 / 32-bit, or float)");
    const std::size_t bps = std::size_t(bits) / 8, n = dlen / bps / std::size_t(out.channels) * std::size_t(out.channels);
    out.s.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* p = &b[data + i * bps];
        std::int32_t v;
        if (flt) {
            double x;
            if (bits == 32) { float f; std::memcpy(&f, p, 4); x = f; } else std::memcpy(&x, p, 8);
            v = std::isfinite(x) ? std::int32_t(std::lround(std::max(-1.0, std::min(1.0, x)) * 32767.0)) : 0;
        } else if (bits == 8) v = (std::int32_t(p[0]) - 128) << 8;
        else if (bits == 16) v = std::int16_t(p[0] | p[1] << 8);
        else if (bits == 24) v = std::int32_t(std::uint32_t(p[0] << 8 | p[1] << 16 | std::uint32_t(p[2]) << 24)) >> 16;
        else v = std::int32_t(std::uint32_t(p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24)) >> 16;
        out.s[i] = std::int16_t(clamp16(v));
    }
    return true;
}

// ---- the containers ----

bool is_audio_bank(const std::uint8_t* p, std::size_t n) {
    if (n < 0x5C || p[0] != 0x09 || p[1] != 0x08 || p[2] || p[3] || p[4]) return false;
    auto u = [p](std::size_t o) { return std::uint32_t(p[o] | p[o + 1] << 8 | p[o + 2] << 16 | std::uint32_t(p[o + 3]) << 24); };
    const std::uint64_t hs = u(8), ds = u(0x14);
    return hs >= 0x5C && u(0xC) + 0x30 <= hs && hs + ds == n;
}

bool audio_read(const Buf& file, AudioFile& out, std::string* why) {
    if (is_audio_bank(file.data(), file.size())) return read_awd(file, out, why);
    return read_rws(file, out, why);
}

std::vector<Buf> audio_channels(const Buf& b, const AudioFile& af, const AudioItem& it) {
    std::vector<Buf> ch(std::size_t(it.channels));
    if (af.kind == AudioFile::Awd) {
        const std::size_t off = af.header_size + rd32(b, af.wave_at[it.index] + 0x4C);
        ch[0].assign(b.begin() + std::ptrdiff_t(off), b.begin() + std::ptrdiff_t(off + it.bytes));
        return ch;
    }
    const AudioFile::Seg& s = af.segs[it.index];
    const AudioFile::Sub& f = af.subs[it.sub];
    const std::size_t per = it.bytes / std::size_t(it.channels);
    for (std::size_t c = 0; c < ch.size(); ++c) {
        ch[c].reserve(per);
        for (std::size_t k = 0; ch[c].size() < per; ++k) {
            const std::size_t run = af.data_at + s.offset + k * af.block + f.slice + c * f.interleave;
            const std::size_t take = std::min<std::size_t>(f.interleave, per - ch[c].size());
            ch[c].insert(ch[c].end(), b.begin() + std::ptrdiff_t(run), b.begin() + std::ptrdiff_t(run + take));
        }
    }
    return ch;
}

Pcm audio_decode(const Buf& b, const AudioFile& af, const AudioItem& it) {
    Pcm p;
    p.rate = it.rate;
    p.channels = it.channels;
    const std::vector<Buf> ch = audio_channels(b, af, it);
    std::vector<std::vector<std::int16_t>> dec;
    for (const Buf& c : ch) dec.push_back(adpcm_decode(c.data(), c.size()));
    const std::size_t n = dec.empty() ? 0 : dec[0].size();
    p.s.resize(n * dec.size());
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t c = 0; c < dec.size(); ++c) p.s[i * dec.size() + c] = dec[c][i];
    return p;
}

std::string audio_item_stem(const AudioFile& af, const AudioItem& it) {
    char t[16];
    std::snprintf(t, sizeof t, "%03zu", it.index);
    std::string s = std::string(t) + "_" + safe_name(it.name.empty() ? (af.kind == AudioFile::Rws ? "segment" : "wave") : it.name);
    if (af.kind == AudioFile::Rws && af.nsub > 1) s += ".sub" + std::to_string(it.sub);
    return s;
}

bool audio_import(const Buf& orig, const AudioSource& get, Buf& out, AudioImportReport* rep, std::string* why) {
    AudioFile af;
    if (!audio_read(orig, af, why)) return false;
    AudioImportReport r;
    // the new PS-ADPCM of each changed item, per channel
    std::vector<std::vector<Buf>> repl(af.items.size());
    for (std::size_t i = 0; i < af.items.size(); ++i) {
        const AudioItem& it = af.items[i];
        ++r.items;
        Pcm p;
        if (!get || !get(it, p)) continue;
        ++r.replaced;
        const std::string label = audio_item_stem(af, it);
        const auto ch = to_item(p, it, r.notes, label);
        const std::vector<Buf> old = audio_channels(orig, af, it);
        std::vector<Buf> nw(ch.size());
        bool same = true;
        for (std::size_t c = 0; c < ch.size(); ++c) {
            std::size_t kept = 0;
            nw[c] = adpcm_encode(ch[c].data(), ch[c].size(), old[c].data(), old[c].size(), 0x02, &kept);
            r.frames_reused += kept;
            r.frames_encoded += nw[c].size() / 16 - kept;
            same = same && nw[c] == old[c];
        }
        if (same) { ++r.unchanged; continue; }
        ++r.reencoded;
        if (nw[0].size() != old[0].size()) ++r.length_changed;
        repl[i] = std::move(nw);
    }
    if (af.kind == AudioFile::Awd) {
        const std::size_t hs = af.header_size;
        // the waves in data order; each keeps the bytes after it (to the next wave) beyond its 0x80 alignment
        std::vector<std::size_t> order(af.items.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        auto off_of = [&](std::size_t i) { return std::size_t(rd32(orig, af.wave_at[i] + 0x4C)); };
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b2) { return off_of(a) < off_of(b2); });
        const std::size_t data_size = rd32(orig, 0x14);
        bool moves = false;
        for (std::size_t i = 0; i < repl.size(); ++i) moves = moves || (!repl[i].empty() && repl[i][0].size() != af.items[i].bytes);
        for (std::size_t j = 1; j < order.size() && moves; ++j)
            if (off_of(order[j - 1]) == off_of(order[j])) return fail(why, "two waves share their data: their length cannot change");
        out = orig;
        if (!moves) {
            for (std::size_t i = 0; i < repl.size(); ++i)
                if (!repl[i].empty()) std::memcpy(&out[hs + off_of(i)], repl[i][0].data(), repl[i][0].size());
        } else {
            Buf data(orig.begin() + std::ptrdiff_t(hs), orig.begin() + std::ptrdiff_t(hs + (order.empty() ? data_size : off_of(order[0]))));
            for (std::size_t j = 0; j < order.size(); ++j) {
                const std::size_t i = order[j], o = off_of(i), end = o + af.items[i].bytes;
                const std::size_t next = j + 1 < order.size() ? off_of(order[j + 1]) : data_size;
                const std::size_t start = data.size();
                if (end > next) return fail(why, "waves overlap in the bank's data: their length cannot change");
                if (!repl[i].empty()) {
                    data.insert(data.end(), repl[i][0].begin(), repl[i][0].end());
                    data.resize(align80(data.size()), 0);
                    const std::size_t keep_from = std::min(next, align80(end));
                    data.insert(data.end(), orig.begin() + std::ptrdiff_t(hs + keep_from), orig.begin() + std::ptrdiff_t(hs + next));
                    wr32(out, af.wave_at[i] + 0x18, std::uint32_t(repl[i][0].size()));   // format and target format lengths
                    wr32(out, af.wave_at[i] + 0x34, std::uint32_t(repl[i][0].size()));
                } else {
                    data.insert(data.end(), orig.begin() + std::ptrdiff_t(hs + o), orig.begin() + std::ptrdiff_t(hs + next));
                }
                wr32(out, af.wave_at[i] + 0x4C, std::uint32_t(start));
            }
            out.resize(hs);
            out.insert(out.end(), data.begin(), data.end());
            wr32(out, 0x14, std::uint32_t(data.size()));
            if (data.size() > data_size)
                r.notes.push_back("the bank's wave data grows from " + std::to_string(data_size) + " to " + std::to_string(data.size()) +
                                  " bytes: sound memory (2 MB for every bank and stream ring at once) may not hold it");
        }
    } else {
        // RWS: segment by segment; an unchanged segment's blocks are copied, a changed one is rebuilt
        Buf data;
        std::vector<std::uint32_t> new_off(af.nseg), new_size(af.nseg);
        std::vector<std::vector<std::uint32_t>> new_per(af.nseg, std::vector<std::uint32_t>(af.nsub));
        for (std::size_t si = 0; si < af.nseg; ++si) {
            const AudioFile::Seg& s = af.segs[si];
            bool changed = false;
            std::size_t blocks = 0;
            for (std::size_t k = 0; k < af.nsub; ++k) {
                const std::size_t ii = si * af.nsub + k;
                const AudioItem& it = af.items[ii];
                const std::size_t per_ch = repl[ii].empty() ? it.bytes / std::size_t(it.channels) : repl[ii][0].size();
                new_per[si][k] = std::uint32_t(per_ch * std::size_t(it.channels));
                blocks = std::max(blocks, (per_ch + af.subs[k].interleave - 1) / af.subs[k].interleave);
                changed = changed || !repl[ii].empty();
            }
            blocks = std::max<std::size_t>(blocks, 1);
            new_off[si] = std::uint32_t(data.size());
            if (!changed) {
                data.insert(data.end(), orig.begin() + std::ptrdiff_t(af.data_at + s.offset), orig.begin() + std::ptrdiff_t(af.data_at + s.offset + s.size));
                new_size[si] = s.size;
                continue;
            }
            Buf seg;
            if (blocks * af.block == s.size) {
                seg.assign(orig.begin() + std::ptrdiff_t(af.data_at + s.offset), orig.begin() + std::ptrdiff_t(af.data_at + s.offset + s.size));
            } else {
                seg.resize(blocks * af.block);
                for (std::size_t o = 0; o + 16 <= seg.size(); o += 16) std::memcpy(&seg[o], kPadFrame, 16);
            }
            for (std::size_t k = 0; k < af.nsub; ++k) {
                const std::size_t ii = si * af.nsub + k;
                const AudioItem& it = af.items[ii];
                const AudioFile::Sub& f = af.subs[k];
                const std::vector<Buf> chans = repl[ii].empty() ? audio_channels(orig, af, it) : repl[ii];
                const bool rewrite = !repl[ii].empty() || blocks * af.block != s.size;
                if (!rewrite) continue;
                for (std::size_t c = 0; c < chans.size(); ++c)
                    for (std::size_t bk = 0; bk < blocks; ++bk) {
                        const std::size_t run = bk * af.block + f.slice + c * f.interleave;
                        for (std::size_t o = 0; o < f.interleave; o += 16) {
                            const std::size_t src = bk * f.interleave + o;
                            std::memcpy(&seg[run + o], src + 16 <= chans[c].size() ? &chans[c][src] : kPadFrame, 16);
                        }
                    }
            }
            new_size[si] = std::uint32_t(seg.size());
            data.insert(data.end(), seg.begin(), seg.end());
        }
        out.assign(orig.begin(), orig.begin() + std::ptrdiff_t(af.data_at));
        out.insert(out.end(), data.begin(), data.end());
        for (std::size_t si = 0; si < af.nseg; ++si) {
            wr32(out, af.segs[si].at + 0x18, new_size[si]);
            wr32(out, af.segs[si].at + 0x1C, new_off[si]);
            for (std::size_t k = 0; k < af.nsub; ++k) wr32(out, af.segs[si].sizes_at + 4 * k, new_per[si][k]);
        }
        // the sub-stream's wave format length: segment 0's bytes of that sub-stream (or its whole size) on the disc
        for (std::size_t k = 0; k < af.nsub; ++k) {
            const std::uint32_t len = rd32(orig, af.subs[k].fmt_at + 8);
            if (len == rd32(orig, af.segs[0].sizes_at + 4 * k)) wr32(out, af.subs[k].fmt_at + 8, new_per[0][k]);
            else if (len == af.segs[0].size) wr32(out, af.subs[k].fmt_at + 8, new_size[0]);
        }
        if (out.size() > 0xFFFFFFFFull) return fail(why, "the stream would be over 4 GB");
        wr32(out, 4, std::uint32_t(out.size() - 12));
        wr32(out, af.data_at - 8, std::uint32_t(data.size()));
    }
    if (rep) *rep = std::move(r);
    return true;
}

}  // namespace b3modtool
