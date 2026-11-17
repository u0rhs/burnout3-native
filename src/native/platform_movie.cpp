// platform_movie.cpp -- the movies without the IPU (movie3): Sony libmpeg (sceMpeg*) and the SDK sample's ViBuf (viBuf*),
// the two libraries CGtVideoDecoder drives, replaced natively. The bitstream goes from the game's ring buffer straight
// into Mpeg2Decoder (src/native/mpeg2_decoder.h, checked against ffmpeg on every disc movie); pictures come out in the
// layout sceMpegGetPicture's RGB32 output has, which _Render__21CGt2dObjectVideoImage uploads (16-pixel-wide strips the
// full height: strip c at picture + c x rows x 1024, 64 bytes a row [L SetTileTexture], src/gameview/hud_capture.cpp).
// Nothing reaches the emulated IPU, its DMA channels 3 / 4 or the DMAC handlers libmpeg installs.
//
// Callers [C decomp drafts, src/nonmatching/func_003254F0.cpp Prepare, func_00325030.cpp Update,
// NodataCallback__15CGtVideoDecoder...cpp; func_00324F40 the delete; 0x325B90 / 0x325BB0 Restart / StopDMA]:
//   Prepare: viBufCreate(vb, data 1 MB, tags, 16 units, 0, 0); sceMpegCreate(mp, work, size); three callbacks (1 no
//            data, 2 stop DMA, 3 restart DMA); viBufReset; sceMpegReset; feeds 0x80000 bytes (BeginPut, the file's
//            read into ptr0, EndPut later) and waits for viBufCount > 0x10000.
//   Update:  feeds 0x10000 more while the file is idle; sceMpegGetPicture(mp, slot picture, mb count) up to twice;
//            sceMpegIsEnd -> viBufReset + sceMpegReset, then the movie closes or (flag 1) seeks to 0 and loops.
//   NodataCallback: called by libmpeg when the IPU's input runs dry; feeds or, at the file's end (+0x4C), sets +0x4D
//            (the movie ends after this picture) and puts 0x10000 more bytes.
// ViBuf [L SDK sample vibuf.c; field use C from the matched units vibuf_00325DC0..003268E0]: +0x00 data, +0x08 units of
// 64 KB, +0x0C dmaStart, +0x10 dmaN, +0x14 readBytes, +0x18 bytes, +0x40 semaphore, +0x44 active, +0x48 total bytes.
// Count = dmaN x 64 KB + readBytes; BeginPut's free space = (units - 2 - dmaN) x 64 KB - readBytes from the write point
// (dmaStart + dmaN) x 64 KB + readBytes. Natively the ring's bytes are consumed as the decoder takes them (no DMA
// units), so Count is the bytes not yet decoded and the fields are kept as these definitions give them.
#include "mpeg2_decoder.h"
#include "platform.h"

#include "b3host/call.h"
#include "b3host/native_backend.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

namespace b3native::platform {
namespace {

using b3host::CallArgs;
using b3host::CallResult;
using b3host::gaddr;

void ret(CallResult& r, std::int32_t v) {
    r = CallResult{};
    r.v0 = b3host::detail::gpr64(b3host::detail::sext32(static_cast<std::uint32_t>(v)));
}
gaddr arg(const CallArgs& a, unsigned k) { return a.gpr[k].w[0]; }
gaddr cached(gaddr p) { return p & 0x0FFFFFFFu; }   // the uncached (0x20000000) / accelerated (0x30000000) mirrors

constexpr std::uint32_t kUnit = 0x10000u;

// B3NATIVE_MOVIE_LOG=1: a line per movie start / end (pictures, the VBlank) on stderr
bool movie_log() {
    static const bool on = [] { const char* e = std::getenv("B3NATIVE_MOVIE_LOG"); return e && *e && *e != '0'; }();
    return on;
}

struct Ring {
    gaddr data = 0;
    std::uint32_t size = 0;    // bytes (units x 64 KB)
    std::uint32_t read = 0;    // the next byte the decoder takes
    std::uint32_t count = 0;   // bytes put and not yet decoded
    std::uint64_t total = 0;
};
std::map<gaddr, Ring> g_rings;   // by the ViBuf's address

struct Movie {
    Mpeg2Decoder dec;
    gaddr cb_fn[4] = {}, cb_data[4] = {};
    std::uint64_t pictures = 0;
    bool end = false;
    std::vector<std::uint8_t> rgba, strips;
    Ring* ring = nullptr;   // the ring its data comes from (the one ViBuf a CGtVideoDecoder owns)
    gaddr ring_at = 0;
};
std::map<gaddr, std::unique_ptr<Movie>> g_movies;   // by the sceMpeg's address

// the ViBuf's fields as the SDK's own code would leave them (no reader so far besides viBuf*; kept for a capture)
void publish(gaddr vb, const Ring& r) {
    const std::uint32_t units = r.count / kUnit, part = r.count % kUnit;
    const std::uint32_t start = (r.read / kUnit) % std::max(1u, r.size / kUnit);
    b3host::store<std::int32_t>(vb + 0x0C, static_cast<std::int32_t>(start));
    b3host::store<std::int32_t>(vb + 0x10, static_cast<std::int32_t>(units));
    b3host::store<std::int32_t>(vb + 0x14, static_cast<std::int32_t>(part));
    b3host::store<std::uint32_t>(vb + 0x48, static_cast<std::uint32_t>(r.total));
    b3host::store<std::uint32_t>(vb + 0x4C, static_cast<std::uint32_t>(r.total >> 32));
}
Ring& ring_of(const char* who, gaddr vb) {
    const auto it = g_rings.find(vb);
    if (it == g_rings.end()) stub_unexpected(who, "a ViBuf viBufCreate did not make");
    return it->second;
}
Movie& movie_of(const char* who, gaddr mp) {
    const auto it = g_movies.find(mp);
    if (it == g_movies.end()) stub_unexpected(who, "an sceMpeg sceMpegCreate did not make");
    return *it->second;
}

// The movie's ring: CGtVideoDecoder keeps its sceMpeg at +0x78 and its ViBuf at +0xC0 [C Prepare draft], so the ring is
// mp + 0x48. Any other pairing is not modelled.
Ring* ring_for(Movie& m, gaddr mp) {
    if (!m.ring) {
        const auto it = g_rings.find(mp + 0x48);
        if (it == g_rings.end()) stub_unexpected("sceMpegGetPicture", "no ViBuf at sceMpeg + 0x48 (CGtVideoDecoder's pair)");
        m.ring = &it->second;
        m.ring_at = mp + 0x48;
    }
    return m.ring;
}

// picture -> the RGB32 output: macroblock columns of 16 x (rows x 16) pixels, 64 bytes a row
void write_picture(Movie& m, const Mpeg2Picture& p, gaddr out, std::uint32_t mbs) {
    Mpeg2Decoder::to_rgba(p, m.rgba);
    const int w = p.display_w, h = p.display_h;
    const int mbw = (w + 15) / 16, mbh = (h + 15) / 16;
    const std::uint32_t n = std::min<std::uint32_t>(mbs, static_cast<std::uint32_t>(mbw * mbh));
    m.strips.assign(std::size_t(n) * 1024u, 0);
    for (std::uint32_t k = 0; k < n; ++k) {
        const int c = int(k) / mbh, r0 = (int(k) % mbh) * 16;
        std::uint8_t* dst = m.strips.data() + std::size_t(k) * 1024u;
        for (int y = 0; y < 16; ++y) {
            const int sy = std::min(h - 1, r0 + y);
            for (int x = 0; x < 16; ++x) {
                const int sx = std::min(w - 1, c * 16 + x);
                const std::uint8_t* s = m.rgba.data() + (std::size_t(sy) * w + sx) * 4;
                std::copy(s, s + 4, dst + (y * 16 + x) * 4);
            }
        }
    }
    b3host::copy_to_guest(cached(out), m.strips.data(), static_cast<std::uint32_t>(m.strips.size()));
}

// feed the ring's bytes to the decoder (in 64 KB pieces, as the DMA would)
bool feed_from_ring(Movie& m, Ring& r, std::vector<std::uint8_t>& tmp) {
    if (r.count == 0 || r.size == 0) return false;
    const std::uint32_t n = std::min({r.count, kUnit, r.size - r.read});
    tmp.resize(n);
    b3host::copy_from_guest(tmp.data(), r.data + r.read, n);
    r.read = (r.read + n) % r.size;
    r.count -= n;
    m.dec.feed(tmp.data(), n);
    return true;
}

// ---- ViBuf
// int viBufCreate(ViBuf *f, u_long128 *data, u_long128 *tag, int size (64 KB units), TimeStamp *ts, int n_ts)
void vibuf_create(const CallArgs& a, CallResult& r) {
    const gaddr vb = arg(a, 0), data = cached(arg(a, 1));
    const std::uint32_t units = arg(a, 3);
    if (units < 3 || units > 256) stub_unexpected("viBufCreate", "ring of " + std::to_string(units) + " units");
    Ring& g = g_rings[vb];
    g = Ring{};
    g.data = data;
    g.size = units * kUnit;
    b3host::store<std::uint32_t>(vb + 0x00, data);
    b3host::store<std::uint32_t>(vb + 0x04, arg(a, 2));
    b3host::store<std::int32_t>(vb + 0x08, static_cast<std::int32_t>(units));
    b3host::store<std::int32_t>(vb + 0x18, static_cast<std::int32_t>(g.size));
    b3host::store<std::int32_t>(vb + 0x44, 1);
    publish(vb, g);
    stub_note("viBufCreate", "movie3: the ring is the platform's (no DMA tags, no semaphore; the decoder reads the bytes)");
    ret(r, 1);
}
void vibuf_reset(const CallArgs& a, CallResult& r) {
    const gaddr vb = arg(a, 0);
    Ring& g = ring_of("viBufReset", vb);
    g.read = g.count = 0;
    g.total = 0;
    publish(vb, g);
    ret(r, 1);
}
// void viBufBeginPut(ViBuf *f, u_char **ptr0, int *len0, u_char **ptr1, int *len1)
void vibuf_begin_put(const CallArgs& a, CallResult& r) {
    const gaddr vb = arg(a, 0);
    const Ring& g = ring_of("viBufBeginPut", vb);
    const std::int32_t space = std::max<std::int32_t>(0, std::int32_t(g.size) - 2 * std::int32_t(kUnit) - std::int32_t(g.count));
    const std::uint32_t at = (g.read + g.count) % g.size;
    const std::int32_t len0 = std::min<std::int32_t>(space, std::int32_t(g.size - at));
    b3host::store<std::uint32_t>(arg(a, 1), g.data + at);
    b3host::store<std::int32_t>(arg(a, 2), len0);
    b3host::store<std::uint32_t>(arg(a, 3), g.data);
    b3host::store<std::int32_t>(arg(a, 4), space - len0);
    r = CallResult{};
}
// void viBufEndPut(ViBuf *f, int size)
void vibuf_end_put(const CallArgs& a, CallResult& r) {
    const gaddr vb = arg(a, 0);
    Ring& g = ring_of("viBufEndPut", vb);
    const std::int32_t n = static_cast<std::int32_t>(arg(a, 1));
    if (n < 0 || std::uint32_t(n) > g.size - g.count) stub_unexpected("viBufEndPut", "size " + std::to_string(n));
    g.count += std::uint32_t(n);
    g.total += std::uint32_t(n);
    publish(vb, g);
    r = CallResult{};
}
void vibuf_count(const CallArgs& a, CallResult& r) { ret(r, static_cast<std::int32_t>(ring_of("viBufCount", arg(a, 0)).count)); }
void vibuf_add_dma(const CallArgs& a, CallResult& r) {
    ring_of("viBufAddDMA", arg(a, 0));
    ret(r, 1);
}
void vibuf_nop(const CallArgs&, CallResult& r) { ret(r, 1); }   // Stop / RestartDMA: no DMA to hold
void vibuf_delete(const CallArgs& a, CallResult& r) {
    const gaddr vb = arg(a, 0);
    for (auto& [mp, m] : g_movies)
        if (m->ring_at == vb) { m->ring = nullptr; m->ring_at = 0; }
    g_rings.erase(vb);
    ret(r, 1);
}

// ---- libmpeg
void mpeg_init(const CallArgs&, CallResult& r) {
    stub_note("sceMpegInit", "movie3: libmpeg is native (platform_movie.cpp); the IPU and its DMAC handlers stay unused");
    ret(r, 0);
}
// sceMpeg *sceMpegCreate(sceMpeg *mp, u_char *work_area, int work_area_size)
void mpeg_create(const CallArgs& a, CallResult& r) {
    const gaddr mp = arg(a, 0);
    g_movies[mp] = std::make_unique<Movie>();
    for (unsigned k = 0; k < 0x48; k += 4) b3host::store<std::uint32_t>(mp + k, 0);
    ret(r, static_cast<std::int32_t>(mp));
}
void mpeg_delete(const CallArgs& a, CallResult& r) {
    g_movies.erase(arg(a, 0));
    ret(r, 0);
}
void mpeg_reset(const CallArgs& a, CallResult& r) {
    Movie& m = movie_of("sceMpegReset", arg(a, 0));
    if (movie_log())
        std::fprintf(stderr, "movie3: sceMpeg 0x%08X: reset after %llu pictures, VBlank %llu\n", arg(a, 0),
                     static_cast<unsigned long long>(m.pictures), static_cast<unsigned long long>(kernel().vblanks));
    m.dec.reset();
    m.end = false;
    m.pictures = 0;
    b3host::store<std::int32_t>(arg(a, 0) + 8, 0);
    ret(r, 0);
}
void mpeg_clear_ref(const CallArgs& a, CallResult& r) {
    movie_of("sceMpegClearRefBuff", arg(a, 0));
    ret(r, 1);
}
// sceMpegCallback sceMpegAddCallback(sceMpeg *mp, sceMpegCbType type, sceMpegCallback func, void *data)
void mpeg_add_callback(const CallArgs& a, CallResult& r) {
    Movie& m = movie_of("sceMpegAddCallback", arg(a, 0));
    const std::uint32_t type = arg(a, 1);
    if (type > 3) stub_unexpected("sceMpegAddCallback", "callback type " + std::to_string(type));
    const gaddr prev = m.cb_fn[type];
    m.cb_fn[type] = arg(a, 2);
    m.cb_data[type] = arg(a, 3);
    ret(r, static_cast<std::int32_t>(prev));
}
void mpeg_is_end(const CallArgs& a, CallResult& r) { ret(r, movie_of("sceMpegIsEnd", arg(a, 0)).end ? 1 : 0); }

// int sceMpegGetPicture(sceMpeg *mp, sceIpuRGB32 *rgb32, int mbcount): the next picture in display order
void mpeg_get_picture(const CallArgs& a, CallResult& r) {
    const gaddr mp = arg(a, 0), out = arg(a, 1);
    const std::uint32_t mbs = arg(a, 2);
    Movie& m = movie_of("sceMpegGetPicture", mp);
    Ring& g = *ring_for(m, mp);
    static std::vector<std::uint8_t> tmp;
    Mpeg2Picture p;
    unsigned starved = 0;
    for (;;) {
        if (m.dec.next_picture(p)) {
            write_picture(m, p, out, mbs);
            ++m.pictures;
            b3host::store<std::int32_t>(mp + 0, p.display_w);
            b3host::store<std::int32_t>(mp + 4, p.display_h);
            b3host::store<std::int32_t>(mp + 8, static_cast<std::int32_t>(m.pictures));
            ret(r, 0);
            return;
        }
        if (m.dec.failed()) {
            static bool told = false;
            if (!told) std::fprintf(stderr, "movie3: %s (the movie ends here)\n", m.dec.error().c_str()), told = true;
            m.end = true;
            ret(r, -1);
            return;
        }
        if (m.dec.ended()) {   // the sequence end, every picture out
            if (movie_log())
                std::fprintf(stderr, "movie3: sceMpeg 0x%08X: end, %llu pictures, VBlank %llu\n", mp,
                             static_cast<unsigned long long>(m.pictures), static_cast<unsigned long long>(kernel().vblanks));
            m.end = true;
            ret(r, 0);
            return;
        }
        if (feed_from_ring(m, g, tmp)) { starved = 0; continue; }
        // the ring is dry: libmpeg's no-data callback (type 1), as the IPU's input running out would call it
        if (m.cb_fn[1]) {
            const std::uint32_t before = g.count;
            b3host::call_guest<int>(m.cb_fn[1], mp, 0u, m.cb_data[1]);
            // CGtVideoDecoder's callback at the file's end sets +0x4D and puts 64 KB that are not the movie: the
            // stream ends with what was decoded (the game closes or loops the movie after this call)
            if (m.cb_data[1] && b3host::load<std::uint8_t>(m.cb_data[1] + 0x4D) != 0) {
                g.read = (g.read + g.count) % g.size;
                g.count = 0;
                publish(m.ring_at, g);
                m.dec.finish();
                if (!m.dec.next_picture(p)) { m.end = true; ret(r, 0); return; }
                write_picture(m, p, out, mbs);
                ++m.pictures;
                ret(r, 0);
                return;
            }
            if (g.count != before) { starved = 0; continue; }
        }
        // a read in flight: the callback is called again, as libmpeg keeps calling it until data comes
        if (++starved > 100000) {
            std::fprintf(stderr, "movie3: no movie data after %u calls of the no-data callback; the movie ends\n", starved);
            m.dec.finish();
            if (m.dec.next_picture(p)) { write_picture(m, p, out, mbs); ret(r, 0); return; }
            m.end = true;
            ret(r, 0);
            return;
        }
    }
}

}  // namespace

// the entries (platform.h B3NATIVE_MOVIE)
void movie_vibuf_create(const CallArgs& a, CallResult& r) { vibuf_create(a, r); }
void movie_vibuf_reset(const CallArgs& a, CallResult& r) { vibuf_reset(a, r); }
void movie_vibuf_begin_put(const CallArgs& a, CallResult& r) { vibuf_begin_put(a, r); }
void movie_vibuf_end_put(const CallArgs& a, CallResult& r) { vibuf_end_put(a, r); }
void movie_vibuf_count(const CallArgs& a, CallResult& r) { vibuf_count(a, r); }
void movie_vibuf_add_dma(const CallArgs& a, CallResult& r) { vibuf_add_dma(a, r); }
void movie_vibuf_nop(const CallArgs& a, CallResult& r) { vibuf_nop(a, r); }
void movie_vibuf_delete(const CallArgs& a, CallResult& r) { vibuf_delete(a, r); }
void movie_mpeg_init(const CallArgs& a, CallResult& r) { mpeg_init(a, r); }
void movie_mpeg_create(const CallArgs& a, CallResult& r) { mpeg_create(a, r); }
void movie_mpeg_delete(const CallArgs& a, CallResult& r) { mpeg_delete(a, r); }
void movie_mpeg_reset(const CallArgs& a, CallResult& r) { mpeg_reset(a, r); }
void movie_mpeg_clear_ref(const CallArgs& a, CallResult& r) { mpeg_clear_ref(a, r); }
void movie_mpeg_add_callback(const CallArgs& a, CallResult& r) { mpeg_add_callback(a, r); }
void movie_mpeg_is_end(const CallArgs& a, CallResult& r) { mpeg_is_end(a, r); }
void movie_mpeg_get_picture(const CallArgs& a, CallResult& r) { mpeg_get_picture(a, r); }

}  // namespace b3native::platform
