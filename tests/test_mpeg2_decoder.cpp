// test_mpeg2_decoder.cpp -- the native MPEG-2 decoder (src/native/mpeg2_decoder.h), mac5. The full comparison with
// ffmpeg over every movie on the disc is tools/native/mpeg2_check.cpp (103 of 110 within 2, the rest within 3, PSNR >=
// 63.7 dB; DATA/INTRO2.M2V is no MPEG video). Here: the error paths, chunking independence, the CSC against the IPU's
// formula, and (with the disc, B3_GAME_DIR) a real movie's picture count, order and content hash.
#include "test.h"

#include "mpeg2_decoder.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {
[[maybe_unused]] std::vector<std::uint8_t> read_file(const std::string& p) {
    std::vector<std::uint8_t> d;
    if (std::FILE* f = std::fopen(p.c_str(), "rb")) {
        std::uint8_t b[65536];
        std::size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n);
        std::fclose(f);
    }
    return d;
}
[[maybe_unused]] std::uint64_t fnv(std::uint64_t h, const std::vector<std::uint8_t>& v) {
    for (std::uint8_t c : v) { h ^= c; h *= 1099511628211ull; }
    return h;
}
struct Run { std::vector<b3native::Mpeg2Picture> pics; std::string error; };
Run decode(const std::vector<std::uint8_t>& d, std::size_t chunk) {
    b3native::Mpeg2Decoder dec;
    Run r;
    b3native::Mpeg2Picture p;
    for (std::size_t i = 0; i < d.size(); i += chunk) {
        dec.feed(d.data() + i, std::min(chunk, d.size() - i));
        while (dec.next_picture(p)) r.pics.push_back(std::move(p));
    }
    dec.finish();
    while (dec.next_picture(p)) r.pics.push_back(std::move(p));
    r.error = dec.error();
    return r;
}
}  // namespace

TEST(mpeg2_rejects_a_picture_before_a_sequence_header) {
    const std::vector<std::uint8_t> d = {0, 0, 1, 0, 0, 0x08, 0xFF, 0xF8, 0, 0, 1, 0xB7};   // a picture header, end
    const Run r = decode(d, d.size());
    CHECK(r.pics.empty());
    CHECK(r.error.find("before a sequence header") != std::string::npos);
}

// ---- crafted streams (review fixes 2026-11-19): a stream writer for tiny 4:2:0 frame pictures ----
struct BitW {
    std::vector<std::uint8_t> d;
    int nbit = 0;   // bits used in d.back() (0: start a new byte)
    void put(std::uint32_t v, int k) {
        for (int i = k - 1; i >= 0; --i) {
            if (nbit == 0) d.push_back(0);
            d.back() = std::uint8_t(d.back() | (((v >> i) & 1u) << (7 - nbit)));
            nbit = (nbit + 1) & 7;
        }
    }
    void bits(const char* s) { for (; *s; ++s) put(std::uint32_t(*s - '0'), 1); }
    void align() { nbit = 0; }
    void sc(int code) { align(); d.push_back(0); d.push_back(0); d.push_back(1); d.push_back(std::uint8_t(code)); }
};
void seq(BitW& w, int width, int height) {
    w.sc(0xB3);
    w.put(std::uint32_t(width), 12); w.put(std::uint32_t(height), 12);
    w.put(1, 4); w.put(3, 4); w.put(1000, 18); w.put(1, 1); w.put(100, 10); w.put(0, 1);
    w.put(0, 1); w.put(0, 1);   // default matrices
    w.sc(0xB5);
    w.put(1, 4); w.put(0x48, 8); w.put(1, 1); w.put(1, 2); w.put(0, 2); w.put(0, 2);   // 4:2:0, no size extension
    w.put(0, 12); w.put(1, 1); w.put(0, 8); w.put(1, 1); w.put(0, 7);
}
// picture header + picture coding extension; f: forward / backward, horizontal / vertical f_code
void pic(BitW& w, int type, const int (&f)[4]) {
    w.sc(0x00);
    w.put(0, 10); w.put(std::uint32_t(type), 3); w.put(0xFFFF, 16);
    if (type >= 2) w.put(7, 4);
    if (type == 3) w.put(7, 4);
    w.put(0, 1);
    w.sc(0xB5);
    w.put(8, 4);
    for (int i = 0; i < 4; ++i) w.put(std::uint32_t(f[i]), 4);
    w.put(0, 2); w.put(3, 2); w.put(0, 1); w.put(1, 1); w.put(0, 1); w.put(0, 1); w.put(0, 1); w.put(0, 1);
    w.put(0, 1); w.put(1, 1); w.put(1, 1); w.put(0, 1);
}
void slice(BitW& w, int row) { w.sc(1 + row); w.put(4, 5); w.put(0, 1); }
// an intra macroblock: DC differences 0, every block empty
void intra_mb(BitW& w, const char* inc) {
    w.bits(inc); w.bits("1");
    for (int k = 0; k < 4; ++k) w.bits("100" "10");
    for (int k = 0; k < 2; ++k) w.bits("00" "10");
}
// an I picture of mbw x mbh macroblocks, one slice a row
void i_picture(BitW& w, int mbw, int mbh) {
    pic(w, 1, {15, 15, 15, 15});
    for (int r = 0; r < mbh; ++r) {
        slice(w, r);
        for (int c = 0; c < mbw; ++c) intra_mb(w, "1");
    }
}
constexpr int kF1[4] = {1, 1, 15, 15};

TEST(mpeg2_crafted_stream_decodes) {   // the writer itself: two grey I pictures
    BitW w;
    seq(w, 32, 16);
    i_picture(w, 2, 1);
    i_picture(w, 2, 1);
    w.sc(0xB7);
    const Run r = decode(w.d, w.d.size());
    CHECK(r.error.empty());
    CHECK(r.pics.size() == 2);
    if (r.pics.size() == 2) CHECK(r.pics[0].width == 32 && r.pics[0].display_w == 32 && r.pics[0].y[0] == 128);
}

TEST(mpeg2_skipped_macroblocks_past_the_picture_are_rejected_before_any_store) {
    // a P picture whose second macroblock address increment skips past the last macroblock (directly, and through
    // macroblock_escape): an error, and nothing written outside the frame (the skipped ones are checked first; an
    // ASan build of this test showed the out-of-bounds store before the fix)
    for (const char* inc : {"011", "00000001000" "1"}) {
        BitW w;
        seq(w, 16, 16);
        i_picture(w, 1, 1);
        pic(w, 2, kF1);
        slice(w, 0);
        w.bits("1" "001" "1" "1");   // MB 0: MC not coded, vector (0, 0)
        w.bits(inc); w.bits("001" "1" "1");
        w.sc(0xB7);
        const Run r = decode(w.d, w.d.size());
        CHECK(r.error.find("macroblock past the picture") != std::string::npos);
    }
    {   // many escapes: refused once the increment passes the picture, not summed on
        BitW w;
        seq(w, 16, 16);
        i_picture(w, 1, 1);
        pic(w, 2, kF1);
        slice(w, 0);
        w.bits("1" "001" "1" "1");
        for (int i = 0; i < 2000; ++i) w.bits("00000001000");
        w.bits("1");
        w.sc(0xB7);
        const Run r = decode(w.d, 333);
        CHECK(r.error.find("macroblock past the picture") != std::string::npos);
    }
}

TEST(mpeg2_a_size_change_emits_the_held_picture_at_its_own_size) {
    // 16x16, then a new sequence header at 48x32: the held 16x16 picture comes out as 16x16 (it used to take the new
    // size and to_rgba read past its planes), the new one at 48x32
    BitW w;
    seq(w, 16, 16);
    i_picture(w, 1, 1);
    seq(w, 48, 32);
    i_picture(w, 3, 2);
    w.sc(0xB7);
    for (std::size_t chunk : {w.d.size(), std::size_t(7)}) {
        const Run r = decode(w.d, chunk);
        CHECK(r.error.empty());
        CHECK(r.pics.size() == 2);
        if (r.pics.size() != 2) continue;
        const auto& a = r.pics[0];
        const auto& b = r.pics[1];
        CHECK(a.width == 16 && a.height == 16 && a.display_w == 16 && a.display_h == 16 && a.y.size() == 256);
        CHECK(b.width == 48 && b.height == 32 && b.display_w == 48 && b.display_h == 32 && b.y.size() == 48 * 32);
        std::vector<std::uint8_t> rgba;
        b3native::Mpeg2Decoder::to_rgba(a, rgba);
        CHECK(rgba.size() == 16 * 16 * 4);
    }
    {   // a P picture right after the change has no reference of its size: an error, no mixed-size prediction
        BitW v;
        seq(v, 16, 16);
        i_picture(v, 1, 1);
        seq(v, 32, 16);
        pic(v, 2, kF1);
        slice(v, 0);
        v.bits("1" "001" "1" "1" "1" "001" "1" "1");
        v.sc(0xB7);
        const Run r = decode(v.d, v.d.size());
        CHECK(r.pics.size() == 1 && r.pics[0].width == 16 && r.pics[0].display_w == 16);
        CHECK(r.error.find("without a reference") != std::string::npos);
    }
}

TEST(mpeg2_to_rgba_reads_only_what_the_planes_hold) {
    b3native::Mpeg2Picture p;
    p.width = 16; p.height = 16; p.display_w = 64; p.display_h = 40;   // a display size past the coded one
    p.y.assign(256, 235); p.cb.assign(64, 128); p.cr.assign(64, 128);
    std::vector<std::uint8_t> rgba;
    b3native::Mpeg2Decoder::to_rgba(p, rgba);
    CHECK(rgba.size() == std::size_t(64) * 40 * 4);
    CHECK(rgba[0] > 200 && rgba[(std::size_t(39) * 64 + 63) * 4] == 0);
    p.y.resize(10);   // planes shorter than the coded size: nothing read, all 0
    b3native::Mpeg2Decoder::to_rgba(p, rgba);
    CHECK(rgba.size() == std::size_t(64) * 40 * 4 && rgba[0] == 0);
}

TEST(mpeg2_invalid_f_code_is_rejected) {
    {   // f_code 0 in the picture coding extension (it made the residual size -1: a 4-billion-bit read)
        BitW w;
        seq(w, 16, 16);
        i_picture(w, 1, 1);
        pic(w, 2, {0, 1, 15, 15});
        slice(w, 0);
        w.bits("1" "001" "010" "1");   // a non-zero motion code
        w.sc(0xB7);
        const Run r = decode(w.d, w.d.size());
        CHECK(r.error.find("invalid f_code 0") != std::string::npos);
    }
    {   // reserved 10..14
        BitW w;
        seq(w, 16, 16);
        i_picture(w, 1, 1);
        pic(w, 2, {12, 1, 15, 15});
        w.sc(0xB7);
        const Run r = decode(w.d, w.d.size());
        CHECK(r.error.find("invalid f_code 12") != std::string::npos);
    }
    {   // a vector in a direction marked unused (15)
        BitW w;
        seq(w, 16, 16);
        i_picture(w, 1, 1);
        pic(w, 2, {15, 15, 15, 15});
        slice(w, 0);
        w.bits("1" "001" "010" "1");
        w.sc(0xB7);
        const Run r = decode(w.d, w.d.size());
        CHECK(r.error.find("motion vector with f_code 15") != std::string::npos);
    }
}

TEST(mpeg2_csc_matches_the_ipu_formula) {
    // one 16x16 picture: Y 16..235 ramp, Cb / Cr extremes; to_rgba against the IPU's terms written out here
    b3native::Mpeg2Picture p;
    p.width = p.display_w = 16; p.height = p.display_h = 16;
    p.y.resize(256); p.cb.resize(64); p.cr.resize(64);
    for (int i = 0; i < 256; ++i) p.y[std::size_t(i)] = std::uint8_t(16 + (i * 219) / 255);
    for (int i = 0; i < 64; ++i) { p.cb[std::size_t(i)] = std::uint8_t(i * 4); p.cr[std::size_t(i)] = std::uint8_t(255 - i * 4); }
    std::vector<std::uint8_t> rgba;
    b3native::Mpeg2Decoder::to_rgba(p, rgba, 0, 0);
    CHECK(rgba.size() == 256 * 4);
    int bad = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const int cr1 = p.cr[std::size_t((y / 2) * 8 + x / 2)] - 128, cb1 = p.cb[std::size_t((y / 2) * 8 + x / 2)] - 128;
            const int y3 = (0x095 * (p.y[std::size_t(y * 16 + x)] - 16)) & ~0x3F;
            auto fin = [](int v) { v >>= 6; v = (v + 1) >> 1; return v < 0 ? 0 : v > 255 ? 255 : v; };
            const int R = fin(y3 + ((0x0CC * cr1) & ~0x3F)), G = fin(y3 - ((0x032 * cb1) & ~0x3F) - ((0x068 * cr1) & ~0x3F)),
                      B = fin(y3 + ((0x102 * cb1) & ~0x3F));
            const std::uint8_t* o = &rgba[std::size_t(y * 16 + x) * 4];
            bad += o[0] != R || o[1] != G || o[2] != B || o[3] != 0x80;
        }
    CHECK(bad == 0);
}

#ifdef B3_GAME_DIR
TEST(mpeg2_cri_rw30_decodes_the_same_in_any_chunking) {
    const std::vector<std::uint8_t> d = read_file(std::string(B3_GAME_DIR) + "/OVID/CRI_RW30.M2V");
    CHECK(!d.empty());
    if (d.empty()) return;
    const Run a = decode(d, 65536), b = decode(d, 777);
    CHECK(a.error.empty() && b.error.empty());
    CHECK(a.pics.size() == 238 && b.pics.size() == 238);   // ffmpeg: 238
    CHECK(a.pics.front().display_w == 640 && a.pics.front().display_h == 480);
    std::uint64_t ha = 14695981039346656037ull, hb = ha;
    bool order = true;
    for (std::size_t i = 0; i < a.pics.size() && i < b.pics.size(); ++i) {
        ha = fnv(fnv(fnv(ha, a.pics[i].y), a.pics[i].cb), a.pics[i].cr);
        hb = fnv(fnv(fnv(hb, b.pics[i].y), b.pics[i].cb), b.pics[i].cr);
        if (i && a.pics[i].type != 3 && a.pics[i - 1].type != 3)
            order = order && a.pics[i].decode_index > a.pics[i - 1].decode_index;
    }
    CHECK(ha == hb);   // chunking does not change a single sample
    CHECK(order);
    // picture 230 (the RenderWare logo, centred as ffmpeg has it): the logo's bright pixels around x 140..510, row ~235
    const auto& q = a.pics[230];
    int lo = 640, hi = -1;
    for (int x = 0; x < 640; ++x)
        for (int y = 200; y < 270; ++y)
            if (q.y[std::size_t(y) * std::size_t(q.width) + std::size_t(x)] > 60) { lo = std::min(lo, x); hi = std::max(hi, x); }
    CHECK(lo > 120 && lo < 170 && hi > 480 && hi < 530);
}
#endif

TEST_MAIN("mpeg2_decoder")
