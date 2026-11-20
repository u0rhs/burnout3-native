// perf4: FRAMES IN FLIGHT Auto (the window's GPU frame queue chosen while the game runs). The low-latency modes keep one
// frame in flight (lat1): the CPU builds a frame only once the GPU has finished the last one, so the two run one after the
// other. That costs nothing while the frame rate holds its target (the cap, or the display's refresh); when it does not and
// the frame's time goes to waiting for the GPU (wait_swapchain, the swapchain acquire, the submit), a second frame in flight
// lets the CPU build the next frame while the GPU draws this one. Auto starts at 1, goes to 2 after about a second of
// that, and tries 1 again once 2 holds the target with room to spare (backing off when 1 then fails again at once).
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

    int current() const { return cur_; }
    // After the device took the change (or refused it: the current count stays as it was).
    void set_current(int n) { cur_ = std::clamp(n, 1, 3); }

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
                if (++held_ >= 30) probe_after_ = kProbeMin;   // 1 holds for good: the next probe comes early again
                return false;
            }
            held_ = 0;
            if (cooldown_ > 0) { --cooldown_; return false; }
            if (share < kWaitShare) return false;   // the CPU's own work (or the pacing): a second frame would not help
            if (probing_ && since <= 3) probe_after_ = std::min(probe_after_ * 2, kProbeMax);   // the probe failed at once
            probing_ = false;
            mean_at_1_ = mean;
            return to(2, c, "the frame rate is below the target and the frame waits for the GPU");
        }
        if (cur_ == 2) {
            if (holds) {
                if (++held_ >= probe_after_) {
                    probing_ = true;
                    return to(1, c, "the target holds with a second frame: trying one again");
                }
                return false;
            }
            held_ = 0;
            if (since >= 3 && mean > mean_at_1_ * 0.97) {   // 2 brought nothing (CPU-bound): back to 1, not again soon
                cooldown_ = backoff_;
                backoff_ = std::min(backoff_ * 2, kProbeMax);
                return to(1, c, "a second frame did not raise the frame rate");
            }
        }
        return false;
    }

private:
    static constexpr int kProbeMin = 5, kProbeMax = 120;   // windows (seconds)
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
    int probe_after_ = kProbeMin;
    int cooldown_ = 0, backoff_ = 10;
    bool probing_ = false;
    double mean_at_1_ = 1e9;
};

}  // namespace b3gfx

#endif
