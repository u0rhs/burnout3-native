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
// scenewait (2026-11-24): this is the wait's normal cap; the renderer waits on past it, up to 10 s, only while the race's
// route or its vehicles are still being prepared (src/b3replay/fastforward.h scene_wait_more: a first race on a track
// with an empty HD texture cache), so the race is never shown before its scene.
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
        on = false;
        behind = 0;
        if (fl.fast || waiting) return kNow;
        on = true;
        if (fl.ended) { ++paced; base = now - period * std::int64_t(paced); return kNow; }
        // stutter: the first paced VBlank is due now (its stamp is now), the next one a period later
        if (first) { ++paced; base = now - period * std::int64_t(paced); return kNow; }
        ++paced;
        const std::int64_t due = base + period * std::int64_t(paced);
        if (now < due) return due;
        behind = now - due;
        if (behind > 250000000) { base = now - period * std::int64_t(paced); ++resyncs; }
        return kNow;
    }
    // A hold (the window lost the focus): the game goes on as if no time had passed.
    void hold_ended(std::int64_t now) { base = now - period * std::int64_t(paced); }
    // stutter: this VBlank's place on the schedule (its due time; 0: not paced: a fast load, before --pace-from). The
    // polls' latch stamps (gameview_native stamp): the renderer's schedule clock (src/b3replay/clock.h step_sched) shows
    // the game time by them, so they must be the schedule's steady 1/59.94 s grid, not the moment the game thread got to
    // its poll (that moved with the wake-up, the frame's work before the poll, a thread moved between cores: the shown
    // game time stepped back and forth at each tick, a judder with clean frame times). Re-anchored with the schedule
    // (a resync, a hold, the end of a fast load): one step there, as the game's own time.
    std::int64_t stamp_ns() const { return on ? base + period * std::int64_t(paced) : 0; }
    bool on = false;   // the last vblank() was paced
    // stutter: how late the last vblank() started against its due time (0: on time, not paced, re-anchored), ns; a
    // VBlank more than a period late is catching up (the one before it ran into its time: a stall), its stamp is marked
    // (include/b3live/sched_stamp.h) and a run of them is one stall in the log. Less late is ordinary lag: it counts in
    // the display's margin like any other (step_sched clamps a lag at a VBlank and caps the margin anyway).
    std::int64_t behind = 0;
    bool late() const { return behind > period; }
};

// trafmem: native_play's wait for an offscreen shot runner's full hold (b3r_feed_full / b3r_feed_hold). It waits while
// the hold is full and moving; a wait that runs out (kLimitNs without the hold moving: a renderer that stopped drawing)
// is not started again for that hold: the game goes on and the feed's byte bound drops the oldest ticks, until the hold
// moves (a shot taken), when waiting is allowed again.
struct FeedWait {
    static constexpr std::int64_t kLimitNs = 120000000000;   // 120 s without the hold moving
    static constexpr unsigned kNone = 0xFFFFFFFFu;
    unsigned gave_up = kNone;   // the hold a wait ran out on
    // Whether to wait now: the hold is full, and not the one a wait gave up on
    bool should_wait(bool full, unsigned hold) {
        if (hold != gave_up) gave_up = kNone;   // (the hold moved on: waiting is allowed again)
        return full && (gave_up == kNone || hold != gave_up);
    }
    // A wait ended because it ran out on `hold`
    void ran_out(unsigned hold) { gave_up = hold; }
};

// slowcpu (issue #15): the game behind real time. On a slow CPU the game's own work in a race can take longer than a
// VBlank; the pacer then lets it fall behind (and resyncs past 250 ms: slow motion). While it is behind, the renderer
// holds its frame rate to the game's (b3r_game_behind, src/b3replay/game_behind.h), so the CPU it and its driver use go
// to the game.
// On only for sustained lateness, never one hitch (Fable): kLateCount new delays within kLateWindow VBlanks -- a VBlank
// more than kLate late and later than the one before it (the catch-up VBlanks after one stall are each less late:
// one delay), or two resyncs within kResyncWindow. Off after a run of VBlanks on time (late under half a VBlank) as long
// as the hold: 3 s, and when it comes back within kRetrigger of a release, longer each time (10 s, 30 s, then the rest
// of the race), so a game that only keeps up thanks to the hold does not flip it on and off; kDecay VBlanks without a
// trigger take it one step back (an early 30 s hold does not stay for the whole race). reset() at a new race (a
// loading screen gone) starts again from 3 s.
struct BehindWatch {
    static constexpr std::int64_t kLate = 2 * 16683350;      // a VBlank more than two VBlanks late
    static constexpr int kLateCount = 3;
    static constexpr std::uint64_t kLateWindow = 60;         // 1 s
    static constexpr std::uint64_t kResyncWindow = 300;      // 5 s
    static constexpr std::int64_t kOnTime = 16683350 / 2;
    static constexpr std::uint64_t kRetrigger = 600;         // 10 s
    static constexpr std::uint64_t kDecay = 7200;            // 2 minutes
    static constexpr std::uint64_t kForever = ~std::uint64_t(0);
    static constexpr std::uint64_t kHolds[4] = {180, 600, 1800, kForever};   // 3 s, 10 s, 30 s, the rest of the race
    bool behind = false;
    int level = 0;                    // index into kHolds
    std::uint64_t on_time = 0, released_at = 0, triggered_at = 0;
    std::int64_t prev_late = 0;       // the last step's lateness (a new delay is later than it)
    bool released = false;            // released at least once since the last reset
    std::uint64_t late_at[kLateCount]{}, resync_at[2]{};
    int late_n = 0, resync_n = 0;     // how many of each are recorded (at most the array's size)
    std::uint64_t resyncs_seen = 0;
    std::uint64_t hold() const { return kHolds[level]; }
    // a paced VBlank `v` (`late`: Pacer::behind, `resyncs`: Pacer::resyncs); true when `behind` changed
    bool step(std::uint64_t v, std::int64_t late, std::uint64_t resyncs) {
        const bool resynced = resyncs != resyncs_seen;
        resyncs_seen = resyncs;
        if (late > kLate && late > prev_late) push(late_at, late_n, v);   // a new delay
        prev_late = late;
        if (resynced) push(resync_at, resync_n, v);
        if (!behind) {
            if (level > 0 && level < 3 && v - triggered_at >= kDecay) {   // two minutes without a trigger: one step back
                --level;
                triggered_at = v;
            }
            const bool lates = late_n == kLateCount && v - late_at[0] <= kLateWindow;
            const bool resyncs2 = resync_n == 2 && v - resync_at[0] <= kResyncWindow;
            if (!lates && !resyncs2) return false;
            if (released && v - released_at <= kRetrigger && level < 3) ++level;
            behind = true;
            on_time = 0;
            triggered_at = v;
            return true;
        }
        on_time = late < kOnTime && !resynced ? on_time + 1 : 0;
        if (hold() == kForever || on_time < hold()) return false;
        behind = false;
        released = true;
        released_at = v;
        on_time = 0;
        late_n = resync_n = 0;
        return true;
    }
    // a new race (a loading screen gone): from the first hold again; true when it was behind (released now)
    bool reset() {
        const bool was = behind;
        const std::uint64_t seen = resyncs_seen;
        *this = BehindWatch{};
        resyncs_seen = seen;
        return was;
    }

private:
    template <std::size_t N>
    static void push(std::uint64_t (&a)[N], int& n, std::uint64_t v) {   // the newest N, oldest first
        if (n < int(N)) { a[n++] = v; return; }
        for (std::size_t k = 1; k < N; ++k) a[k - 1] = a[k];
        a[N - 1] = v;
    }
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
