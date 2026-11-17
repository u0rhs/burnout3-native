// mpeg2_decoder.h -- a native MPEG-2 video decoder for the game's movies (mac5, 2026-11-08): the replacement for the
// emulated IPU (src/native/ipu_model.h) and the retail libmpeg on top of it, so movies need no PS2 hardware model.
//
// Scope, from a survey of every .M2V on the disc (110 files): MPEG-2 Main Profile video, 4:2:0, frame pictures only
// (picture_structure 3), frame_pred_frame_dct 1 (frame prediction, frame DCT: no field modes), no concealment vectors,
// I / P / B pictures; per stream: intra_vlc_format, alternate_scan, q_scale_type, intra_dc_precision 8..10, loaded
// quantiser matrices. Anything outside that fails loudly (error()), naming the feature. The bitstream syntax is ISO
// 13818-2 [C]; the VLC tables, the scans, the non-linear quantiser scale and the IEEE 1180 integer IDCT are the ones the
// dev IPU model carries too (ipu_vlc.h, mpeg_dsp.h; test_ipu_model checks the IDCT against the double-precision one).
//
// Use: feed() the elementary stream in any chunks, then next_picture() until it returns false; finish() at the stream's
// end releases the last reference picture. Pictures come out in display order as Y / Cb / Cr planes (4:2:0).
// to_rgba() converts one picture as the IPU's CSC does (mpeg_dsp.h, shared with ipu_model.h), so colours match the console.
#ifndef B3NATIVE_MPEG2_DECODER_H
#define B3NATIVE_MPEG2_DECODER_H

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace b3native {

struct Mpeg2Picture {
    int width = 0, height = 0;         // the coded size (macroblock multiples) and the display size below
    int display_w = 0, display_h = 0;
    int type = 0;                      // 1 I, 2 P, 3 B
    int temporal_reference = 0;
    std::uint64_t decode_index = 0;    // the order it was decoded in
    std::vector<std::uint8_t> y, cb, cr;   // planes: y width x height, cb / cr (width / 2) x (height / 2)
};

class Mpeg2Decoder {
public:
    Mpeg2Decoder();
    ~Mpeg2Decoder();
    void feed(const std::uint8_t* data, std::size_t n);
    void finish();                     // no more data: the last picture and the held reference come out
    bool next_picture(Mpeg2Picture& out);
    void reset();                      // a new stream (a looping movie restarts here)
    bool ended() const;                // the sequence_end_code was decoded (its last picture is out)
    bool failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    std::uint64_t pictures_decoded() const { return decoded_; }

    // YCbCr -> RGBA32 (display size) as the IPU's CSC to RGB32 [C manual 8.6.1, mpeg_dsp.h csc_pixel]: the same
    // fixed-point terms and rounding; alpha from the thresholds the game sets (SETTH: below th0 black / 0, below th1
    // 0x40, else 0x80)
    static void to_rgba(const Mpeg2Picture& p, std::vector<std::uint8_t>& rgba, int th0 = 0, int th1 = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> m_;
    std::string error_;
    std::uint64_t decoded_ = 0;
};

}  // namespace b3native

#endif
