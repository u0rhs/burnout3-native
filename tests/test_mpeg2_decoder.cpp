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
