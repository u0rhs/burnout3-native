// movie_skip.h -- the port-level movie skip (mac4, 2026-11-08; remaster feature, docs/remaster-features.md "Movie
// skip"): Start or Cross newly pressed while a movie plays ends it through the game's own end-of-movie path.
//
// The game's path [C decomp, retail asm]:
// - Every movie the game shows is decoded by the front end's CGtVideoDecoder, CB3FrontEnd (0x51A6A8) + 0x1C8 =
//   0x51A870 (func_00131100 / func_00130C70 address it as self->field_1C8; every .m2v Prepare has this decoder in
//   $a0, docs/movies.md §3).
// - Prepare (0x3254F0) sets +0x48 = 1 (playing), +0x49 = 0 (completed), +0x4A = 0, +0x4B = flags & 1 (loop).
// - Update (0x325030): at the stream's end (sceMpegIsEnd), a non-looping movie sets +0x4A = 1 (after viBufReset /
//   sceMpegReset); with +0x4A set, the next Update closes the file (vfunc_C) and, once it is closed, +0x48 = 0,
//   +0x49 = 1 (IsCompleted 0x324EE0), +0x4A = 0. The pages wait for IsCompleted.
// - Abort (0x324ED0) is `+0x4A = 1`: the game's own stop (func_00131100 calls it, then Update until IsCompleted).
// So the skip is Abort: +0x4A := 1 on a playing (+0x48), non-looping (+0x4B == 0) movie not already ending. Menu
// backgrounds are never skipped: looping ones, and every movie from TRACKS\ (the title screen's background is a
// non-looping TRACKS movie: skipping it ate the title's Start; a file name starting "tracks\" is a menu background,
// docs/movies.md). The file name comes from Prepare's $t0 (prepared()).
// The press that skipped is masked (Start / Cross released, the Cross pressure 0) until it is let go, so the page
// after the movie does not see it.
// Defaults [G, the project's choice 2026-11-08]: Start and Cross on the pad; the keyboard reaches them through the key map
// (Enter / Space = Cross, Escape = Start: src/native/input_keys.cpp).
#ifndef B3NATIVE_MOVIE_SKIP_H
#define B3NATIVE_MOVIE_SKIP_H

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>

namespace b3native {

class MovieSkip {
public:
    static constexpr std::uint32_t kDecoder = 0x0051A870u;   // [C] the front end's CGtVideoDecoder
    static constexpr std::uint32_t kPlaying = 0x48, kAbort = 0x4A, kLoop = 0x4B;
    static constexpr std::uint32_t kPrepare = 0x003254F0u;   // [C] Prepare__15CGtVideoDecoderFP15CGtLinearMallociiPCcfUi
    // pad report: bytes 0-1 buttons active low (libpad2 indices: Start = 3, Cross = 14), byte 12 the Cross pressure
    static bool start(const unsigned char* s) { return !(s[0] & 0x08); }
    static bool cross(const unsigned char* s) { return !(s[1] & 0x40); }

    bool enabled = true;
    bool exempt = true;   // the current movie is a menu background (unknown until a Prepare is seen: not skipped)
    // At Prepare's entry for kDecoder: the file name's guest address
    void prepared(std::uint32_t name) {
        char n[8] = {};
        for (int i = 0; i < 7 && load8; ++i) {
            const std::uint8_t c = load8(name + static_cast<std::uint32_t>(i));
            if (!c) break;
            n[i] = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c == '/' ? '\\' : c);
        }
        exempt = std::string(n).rfind("tracks\\", 0) == 0;
        ++prepares_;
        if (exempt) ++exempt_prepares_;
    }
    std::uint64_t prepares() const { return prepares_; }
    std::uint64_t exempt_prepares() const { return exempt_prepares_; }
    std::function<std::uint8_t(std::uint32_t)> load8;
    std::function<void(std::uint32_t, std::uint8_t)> store8;

    // padskip (pad_context.h): a film is on the front end's decoder (playing, not looping, not a menu background), skip
    // on or off
    bool film_playing() const {
        return !exempt && load8 && load8(kDecoder + kPlaying) != 0 && load8(kDecoder + kLoop) == 0;
    }
    // mac5: a movie the skip would end is playing (the hint's condition: apply()'s test without the press)
    bool skippable() const {
        return enabled && !exempt && load8 && load8(kDecoder + kPlaying) != 0 && load8(kDecoder + kLoop) == 0 &&
               load8(kDecoder + kAbort) == 0;
    }

    // Once per VBlank with the report the pad model is about to get (masked in place while the skip press is held).
    // True when this call skipped a movie.
    bool apply(unsigned char* s) {
        const bool down = start(s) || cross(s);
        const bool edge = down && !was_down_;
        was_down_ = down;
        bool skipped = false;
        if (edge && store8 && skippable()) {
            store8(kDecoder + kAbort, 1);   // CGtVideoDecoder::Abort
            ++skips_;
            masking_ = true;
            skipped = true;
        }
        if (masking_) {
            if (!down) masking_ = false;
            else {
                s[0] = static_cast<unsigned char>(s[0] | 0x08);
                s[1] = static_cast<unsigned char>(s[1] | 0x40);
                s[12] = 0;
            }
        }
        return skipped;
    }
    std::uint64_t skips() const { return skips_; }
    // gamemods1 (boot_boost.h, START AT): end the movie playing now through the same Abort, without a press (the boot's
    // films, while START AT is not the game's start). True when it ended one.
    bool abort_now() {
        if (!store8 || !skippable()) return false;
        store8(kDecoder + kAbort, 1);   // CGtVideoDecoder::Abort
        ++skips_;
        return true;
    }

private:
    bool was_down_ = false, masking_ = false;
    std::uint64_t skips_ = 0, prepares_ = 0, exempt_prepares_ = 0;
};

// The on-screen hint's opacity (mac5, remaster-features §2: "a small skip hint in a screen corner ... it may fade in
// after a moment or on the first input so it doesn't sit over the video from frame one"), per VBlank in game time, so
// a replay or an offscreen run shows it at the same polls. The hint is wanted from kDelay VBlanks after a skippable
// movie starts for kHold VBlanks, and again for kHold after any other press (a button, or a stick past half way) while
// it plays; the opacity ramps up in kIn VBlanks and down in kOut. native_play publishes it with the game view
// (gameview_native.h set_skip_hint); the renderer draws it (src/b3replay/skiphint.h). Presentation only: it reads the
// report, never changes it.
class SkipHintFade {
public:
    static constexpr int kDelay = 45, kHold = 300, kIn = 15, kOut = 30;   // [G] 0.75 s, 5 s, 0.25 s, 0.5 s at 59.94 Hz

    // `skippable`: MovieSkip::skippable() this VBlank; `s`: the pad report. Returns the opacity, 0..255.
    std::uint8_t step(bool skippable, const unsigned char* s) {
        const bool input = pressed(s) && !was_pressed_;
        was_pressed_ = pressed(s);
        if (!skippable) age_ = -1;
        else if (age_ < 0) { age_ = 0; from_ = kDelay; until_ = kDelay + kHold; }
        else ++age_;
        if (skippable && input) { from_ = std::min(from_, age_); until_ = std::max(until_, age_ + kHold); }
        const bool wanted = skippable && age_ >= from_ && age_ < until_;
        level_ = wanted ? std::min(255, level_ + (255 + kIn - 1) / kIn) : std::max(0, level_ - (255 + kOut - 1) / kOut);
        return static_cast<std::uint8_t>(level_);
    }

private:
    // any button down (bytes 0-1, active low) or a stick past half way (bytes 4-7, 0x80 centre)
    static bool pressed(const unsigned char* s) {
        if (s[0] != 0xFF || s[1] != 0xFF) return true;
        for (int k = 4; k < 8; ++k) if (s[k] < 0x40 || s[k] > 0xC0) return true;
        return false;
    }
    int age_ = -1, from_ = 0, until_ = 0, level_ = 0;
    bool was_pressed_ = false;
};

// movielog (issue #17): the log's movie lines, one per start instead of one per CGtVideoDecoder::Prepare call. The game
// calls Prepare again until the movie is ready (func_003AB0A0 busy-waits on it; CB3BackgroundComponent::Update once a
// frame); while the previous movie's file is still open (its close ends at the next VBlank) that is ~3000 calls in one
// VBlank, and a line and a flush for each made it seconds of wall time (late VBlanks, resyncs, the sound dry). A call
// for the same movie within kSameStart VBlanks of the last is the same start: counted, and summed up once
// (retried()) when the next start comes.
struct MovieLog {
    static constexpr std::uint64_t kSameStart = 2;
    static constexpr std::uint64_t kUsualCalls = 2;   // the open, then its buffer filled: not worth a line
    std::string last;
    std::uint64_t last_v = 0, calls = 0;
    // One Prepare call at VBlank `v`: true = a new start (log its line); `*retried` = the previous start's calls when it
    // needed more than kUsualCalls (0 = none to report)
    bool call(const std::string& name, std::uint64_t v, std::uint64_t* retried) {
        *retried = 0;
        if (calls && name == last && v >= last_v && v - last_v <= kSameStart) {
            ++calls;
            last_v = v;
            return false;
        }
        if (calls > kUsualCalls) *retried = calls;
        prev = last;
        last = name;
        last_v = v;
        calls = 1;
        return true;
    }
    std::string prev;   // the start *retried was counted for
};

}  // namespace b3native

#endif
