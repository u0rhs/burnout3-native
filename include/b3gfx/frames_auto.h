// perf4: FRAMES IN FLIGHT Auto (the window's GPU frame queue chosen while the game runs). The low-latency modes keep one
// frame in flight (lat1): the CPU builds a frame only once the GPU has finished the last one, so the two run one after the
// other. That costs nothing while the frame rate holds its target (the cap, or the display's refresh); when it does not and
// the frame's time goes to waiting for the GPU (wait_swapchain, the swapchain acquire, the submit), a second frame in flight
// lets the CPU build the next frame while the GPU draws this one. Auto starts at 1 and goes to 2 after about a second of
// that.
// fifauto (2026-11-21, a Mac report: 22 changes in ten minutes, 1 <-> 2 every few seconds, the game "framey"): once 1
// has failed the target, 2 stays. 1 is tried again only after a long stable stretch at 2 with ample headroom: every window
// of it well under the target (kAmple) AND the frame time 1 is expected to take -- this one's times the ratio measured
// when 1 last failed (its frame time over 2's right after) -- within the target. The stretch starts at kRetryFirst windows
// and doubles after each retry that fails (up to kRetryMax); a retry that holds for kRetryHeld windows brings it back to
// kRetryFirst. A retry that misses the target in its first windows goes back to 2 at once; one that misses later (before
// it has held) lengthens the stretch the same way. 2 goes back to 1 as CPU-bound only when its first judged windows
// (kHelpJudge) show no gain over 1's frame time; past them 2 counts as helping (the cool-down's back-off starts again), so
// a heavier scene later in a long stretch at 2 is never compared with 1's old frame time. (The device makes a change
// without a stall: DeviceOptions::frames_soft.)
// An explicit FRAMES IN FLIGHT (1..3) or --frames-in-flight is used as it is (no Auto). Renderer only: what the game
// computes never depends on it.
#ifndef B3GFX_FRAMES_AUTO_H
#define B3GFX_FRAMES_AUTO_H

#include <algorithm>

namespace b3gfx {

class FramesAuto {
public:
    struct Change {
        int to = 0;                 // the frames in flight wanted
        double fps = 0;             // the last window's mean frame rate
        double target_fps = 0;
        double wait_share = 0;      // the waits' share of the frame time, 0..1
        const char* why = "";
    };
    static constexpr double kWindowMs = 1000.0;   // one decision per this much frame time
    static constexpr double kWaitShare = 0.25;    // the waits' share of the frame that makes 1 -> 2
    static constexpr double kHolds = 1.05;        // a mean frame time within 5 % of the target holds it
    static constexpr double kAmple = 0.80;        // at 2, a window with room for a retry of 1: under 80 % of the target
    static constexpr int kRetryFirst = 30, kRetryMax = 600;   // windows (seconds) of ample room before a retry of 1
    static constexpr int kRetryJudge = 3;         // a retry missing the target in this many windows failed
    static constexpr int kRetryHeld = 30;         // ... and one holding this many windows succeeded
    static constexpr int kHelpJudge = 5;          // at 2, the windows (after the switch's) that judge whether 2 helped
    static constexpr int kBackoffFirst = 10;      // windows at 1 after 2 brought nothing (doubling while it keeps not helping)

    int current() const { return cur_; }
    // After the device took the change (or refused it: the current count stays as it was).
    void set_current(int n) { cur_ = std::clamp(n, 1, 3); }
    int retry_after() const { return retry_after_; }   // tests and logs
    double ratio() const { return ratio_; }            // 1's frame time over 2's (1.0: not measured yet)
    int backoff() const { return backoff_; }           // the next CPU-bound cool-down (windows)

    // One drawn window frame: its time (the loop's top to its end), the GPU waits in it, the target frame time (ms; <= 0:
    // none known, nothing changes). Returns true with `c` filled when the frames in flight should change to c.to.
    bool frame(double frame_ms, double wait_ms, double target_ms, Change& c) {
        if (!(frame_ms > 0) || frame_ms > 500.0) return false;   // a load or a stall: not a frame of the game's pace
        acc_ms_ += frame_ms;
        acc_wait_ += std::clamp(wait_ms, 0.0, frame_ms);
        ++acc_n_;
        if (acc_ms_ < kWindowMs) return false;
        const double mean = acc_ms_ / double(acc_n_), share = acc_wait_ / acc_ms_;
        acc_ms_ = acc_wait_ = 0;
        acc_n_ = 0;
        if (!(target_ms > 0)) return false;
        const bool holds = mean <= target_ms * kHolds;
        c.fps = 1000.0 / mean;
        c.target_fps = 1000.0 / target_ms;
        c.wait_share = share;
        const int since = since_switch_++;
        if (since == 0) return false;   // the window with the switch in it (its flush): not judged
        if (cur_ == 1) {
            if (holds) {
                if (++held_ >= kRetryHeld && retrying_) {   // the retry holds for good: the next one may come early again
                    retrying_ = false;
                    retry_after_ = kRetryFirst;
                }
                return false;
            }
            held_ = 0;
            if (retrying_ && since <= kRetryJudge) {   // the retry failed: 2 again (it held), the next retry later
                retrying_ = false;
                retry_after_ = std::min(retry_after_ * 2, kRetryMax);
                mean_at_1_ = mean;
                ratio_due_ = true;
                return to(2, c, "one frame missed the target again: back to two (the next try later)");
            }
            if (retrying_) {   // a later miss, before the retry held: a failed retry too (the next one later)
                retrying_ = false;
                retry_after_ = std::min(retry_after_ * 2, kRetryMax);
            }
            if (cooldown_ > 0) { --cooldown_; return false; }
            if (share < kWaitShare) return false;   // the CPU's own work (or the pacing): a second frame would not help
            mean_at_1_ = mean;
            ratio_due_ = true;
            return to(2, c, "the frame rate is below the target and the frame waits for the GPU");
        }
        if (cur_ == 2) {
            if (ratio_due_) {   // the first judged window at 2: what one frame in flight costs on this scene
                ratio_ = std::clamp(mean_at_1_ / mean, 1.0, 2.5);   // (2.5: a window gone wrong must not end retries)
                ratio_due_ = false;
            }
            if (since > kHelpJudge || holds) backoff_ = kBackoffFirst;   // 2 helped: a later CPU-bound verdict starts the back-off again
            if (holds) {
                const bool ample = mean <= target_ms * kAmple && mean * ratio_ <= target_ms;
                held_ = ample ? held_ + 1 : 0;   // (a window without room starts the stretch again)
                if (held_ >= retry_after_) {
                    retrying_ = true;
                    return to(1, c, "a long stretch with room to spare at two: trying one again");
                }
                return false;
            }
            held_ = 0;
            // 2 brought nothing (CPU-bound): back to 1, not again soon. Judged only in the first windows after the switch: 1's
            // frame time was measured on the scene of then
            if (since >= 3 && since <= kHelpJudge && mean > mean_at_1_ * 0.97) {
                cooldown_ = backoff_;
                backoff_ = std::min(backoff_ * 2, kRetryMax);
                return to(1, c, "a second frame did not raise the frame rate");
            }
        }
        return false;
    }

private:
    bool to(int n, Change& c, const char* why) {
        c.to = n;
        c.why = why;
        since_switch_ = 0;
        held_ = 0;
        return true;
    }
    int cur_ = 1;
    double acc_ms_ = 0, acc_wait_ = 0;
    int acc_n_ = 0;
    int since_switch_ = 1;
    int held_ = 0;
    int retry_after_ = kRetryFirst;
    int cooldown_ = 0, backoff_ = kBackoffFirst;
    bool retrying_ = false, ratio_due_ = false;
    double mean_at_1_ = 1e9, ratio_ = 1.0;
};

}  // namespace b3gfx

#endif
