// fast_load.h -- PC SETTINGS > GAME > STARTING AND LOADING > FAST LOADING (fastload1, 2026-11-12; docs/remaster-features.md "Fast
// loading", docs/pc-settings.md §7.8): native_play stops pacing the guest to real time while the game shows its loading
// screen, so a load takes as long as this PC needs, not as long as the PS2 needed.
//
// The game is not changed: native_play's pacing only sleeps between virtual VBlanks (the guest runs on virtual time,
// platform.h), so the loading screen's VBlanks, and every byte of guest RAM at each of them, are the same paced or not;
// they only come sooner in wall-clock time. What changes is presentation only:
//   - the loading screen's own animations (the LOADING strip, the car icons, the bar) run as fast as the load does: the
//     renderer shows the newest frame at each of its own frames (b3r_fast_forward: the display clock's "newest" rule)
//     and the ticks of a fast load carry no latch stamp, so its clock starts afresh at the first paced tick;
//   - the load's sound (the game mixes on virtual time, 800 frames per VBlank) would arrive faster than the device plays
//     it, and the device's drift guard would cut it into pieces. A load is silent but for the tail of what played
//     before it (3-8 VBlanks of the menu's or the race's sound [C B3NATIVE_AUDIO_RAW, crashresults: every load]), so a
//     load goes fast from its first quiet VBlank (the mix's peak below kQuietPeak; at the latest kMaxTail VBlanks after
//     the page came up): the tail plays out in real time, the silence is skipped. From there the sound is muted on
//     purpose (AudioGate: a 1-VBlank fade out, the drops, a 1-VBlank fade in at the end).
// Pacing resumes at the first VBlank that is not loading, re-anchored there (no catch-up burst).
//
// What "loading" is, from the game's own state [C decomp]: CB3MenuPageManager D_01E90240's current page (its +0x1F0,
// 0x01E90430) is CB3LoadingScreenPage D_01E9583C. The game tests exactly this itself: CB3MenuPageManager::Update
// (0x30D7C0, src/nonmatching/Update__18CB3MenuPageManagerFv.cpp: the controller-removed check skips the loading page)
// and SetMenuPage (0x30D5C0, B3MenuPageManager_0030D5C0.cpp: leaving the loading page releases it, phase 6). The page is
// set by the menu flow's loading state (func_003A2100 case 3: CB3LoadingScreenPage::Prepare 0x321780 on D_01E9583C);
// its Update (0x321690) runs each frame while it is up. D_01E900B0 picks its look [C Update: the bar only at 0; observed
// in race_check runs]: 0 the boot's LOADING band with its bar (fe.awd), 2 the picture / tip page into a race, 1 and 3
// the LOADING strip with the two car icons (after a race; between a crash event's parts).
#ifndef B3NATIVE_FAST_LOAD_H
#define B3NATIVE_FAST_LOAD_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace b3native {

constexpr std::uint32_t kMenuCurrentPage = 0x01E90430u;     // [C] CB3MenuPageManager D_01E90240 +0x1F0 (field_1F0)
constexpr std::uint32_t kLoadingScreenPage = 0x01E9583Cu;   // [C] the CB3LoadingScreenPage object (vtable 0x4DE460)
constexpr std::uint32_t kLoadingKind = 0x01E900B0u;         // [C] D_01E900B0: the page's look (above)
constexpr int kQuietPeak = 512;                              // a VBlank's mix below this (-36 dBFS) is quiet
constexpr std::uint64_t kMaxTail = 30;                       // VBlanks of a load paced at most, for its sound's tail

// The loading screen is up: the page manager's current page word (guest RAM at kMenuCurrentPage).
inline bool loading_page_up(std::uint32_t current_page) { return (current_page & 0x01FFFFFFu) == kLoadingScreenPage; }

// The per-VBlank decision. step() once per VBlank with the setting, the detector and whether the last VBlank's mix
// was quiet; the result says whether this VBlank is paced and whether a fast span began or ended here (native_play
// re-anchors its pacing at an end). A span begins at the first quiet VBlank of a load (or kMaxTail VBlanks into it)
// and lasts to the load's end.
class FastLoad {
public:
    struct Step {
        bool fast = false;    // this VBlank is not paced
        bool began = false;   // the first fast VBlank of a span
        bool ended = false;   // the first paced VBlank after a span (re-anchor here, then pace from the next one)
    };
    // `enabled`: FAST LOADING on and the run paced; `loading`: loading_page_up() this VBlank; `quiet`: the mix's peak
    // of the VBlank before was below kQuietPeak.
    Step step(bool enabled, bool loading, bool quiet = true) {
        Step s;
        if (!loading) tail_ = 0;
        else if (!fast_) ++tail_;
        s.fast = enabled && loading && (fast_ || quiet || tail_ > kMaxTail);
        s.began = s.fast && !fast_;
        s.ended = !s.fast && fast_;
        if (s.began) { ++spans_; span_vblanks_ = 0; }
        if (s.fast) { ++vblanks_; ++span_vblanks_; }
        fast_ = s.fast;
        return s;
    }
    bool fast() const { return fast_; }
    std::uint64_t spans() const { return spans_; }
    std::uint64_t vblanks() const { return vblanks_; }              // fast VBlanks over the run
    std::uint64_t span_vblanks() const { return span_vblanks_; }    // of the current / last span

private:
    bool fast_ = false;
    std::uint64_t tail_ = 0;   // paced VBlanks of the current load so far
    std::uint64_t spans_ = 0, vblanks_ = 0, span_vblanks_ = 0;
};

// loadlag1 (a player report, Windows: "the loading screen is still there but you can hear the race starting underneath
// it"): the renderer builds the race's route from the STATIC.DAT header the game reads during the load (~160 ticks
// before the race; src/b3replay/live.cpp LiveScene::preload). With a fast load those ticks pass in ~0.2-0.4 s and the
// build takes ~0.4-0.7 s, so the race began (and was heard) before its picture could be drawn. At a fast span's end
// native_play waits for the renderer (b3r_scene_wait), before the race's first VBlank, at most this long: never past
// the time the load takes at real time (FAST LOADING off: `paced_s`, minus the `spent_s` already gone), and never over
// kSceneHoldMaxMs. Wall time only, like the pacer's sleeps: the game's VBlanks and their state are the same.
constexpr int kSceneHoldMaxMs = 3000;
inline int scene_hold_ms(double paced_s, double spent_s) {
    const double ms = (paced_s - spent_s) * 1000.0;
    return ms <= 0 ? 0 : ms >= kSceneHoldMaxMs ? kSceneHoldMaxMs : int(ms);
}

// native_play's VBlank pacer (play1's rule, moved here so that it can be tested with a fake clock): VBlank n of the
// paced stretch is due at base + n periods; a host more than 250 ms behind resyncs (counted) instead of bursting.
// Times in nanoseconds of any steady clock. vblank() returns the deadline to wait for, or kNow: go on at once.
struct Pacer {
    static constexpr std::int64_t kNow = std::numeric_limits<std::int64_t>::min();
    std::int64_t period = 16683350;   // 1 / 59.94 Hz
    std::int64_t base = 0;
    std::uint64_t paced = 0, resyncs = 0;
    // `waiting`: --pace-from not reached yet (unpaced); `first`: --pace-from's first paced VBlank (base = now, as play1);
    // `fl`: this VBlank's FastLoad step (a fast VBlank is not paced; the first one after re-anchors at once, so the
    // VBlanks after a load keep their real-time spacing from there: no catch-up burst).
    std::int64_t vblank(std::int64_t now, bool waiting, bool first, const FastLoad::Step& fl) {
        if (fl.fast || waiting) return kNow;
        if (fl.ended) { ++paced; base = now - period * std::int64_t(paced); return kNow; }
        if (first) { base = now; ++paced; return kNow; }
        ++paced;
        const std::int64_t due = base + period * std::int64_t(paced);
        if (now < due) return due;
        if (now - due > 250000000) { base = now - period * std::int64_t(paced); ++resyncs; }
        return kNow;
    }
    // A hold (the window lost the focus): the game goes on as if no time had passed.
    void hold_ended(std::int64_t now) { base = now - period * std::int64_t(paced); }
};

// The sound of a fast load: muted. One VBlank's frames (s16 stereo) at a time; set_muted() per VBlank. The VBlank that
// mutes is pushed fading out (so the cut does not click), the following ones are dropped, the first one after is pushed
// fading in. apply() returns the frames to push (nullptr: drop them); a faded copy lives in the gate.
class AudioGate {
public:
    void set_muted(bool m) {
        if (m == muted_) return;
        muted_ = m;
        edge_ = true;
    }
    bool muted() const { return muted_; }
    std::uint64_t dropped() const { return dropped_; }
    const std::int16_t* apply(const std::int16_t* lr, std::size_t n) {
        if (!edge_ && !muted_) return lr;
        if (!edge_) { dropped_ += n; return nullptr; }
        edge_ = false;   // the VBlank of a change: a linear ramp over its frames (out when muting, in when not)
        buf_.assign(lr, lr + 2 * n);
        for (std::size_t k = 0; k < n; ++k) {
            const double t = n > 1 ? double(k) / double(n - 1) : 1.0;
            const double g = muted_ ? 1.0 - t : t;
            buf_[2 * k] = std::int16_t(double(buf_[2 * k]) * g);
            buf_[2 * k + 1] = std::int16_t(double(buf_[2 * k + 1]) * g);
        }
        return buf_.data();
    }

private:
    bool muted_ = false, edge_ = false;
    std::uint64_t dropped_ = 0;
    std::vector<std::int16_t> buf_;
};

}  // namespace b3native

#endif
