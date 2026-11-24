// perfbisect.h -- fifauto (2026-11-21): B3_PERF_BISECT=<seconds>, a diagnostic for "why is this GPU-bound?" on a player's
// machine: during the race frames (the window, one view) the renderer turns its passes off one at a time, each for
// <seconds> (the first 1.5 s of a step not counted: the change settles), and logs per step the frame rate, the loop's
// times, the GPU's work on sampled frames (the time from a frame's submit to its fence, with nothing else in flight:
// Device::set_gpu_timing), the GPU waits and the presentation side's waits (the swapchain acquire: a free image /
// drawable). Then the table again at the end. Drawn only: the game never sees it. Off unless set.
#ifndef B3REPLAY_PERFBISECT_H
#define B3REPLAY_PERFBISECT_H

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace b3replay {

class PerfBisect {
public:
    // the steps, in order (what each turns off; live.cpp applies them)
    enum Step : int {
        kAll = 0,          // as set
        kBloomLean,        // the MSAA bloom's lean form: the scene's samples resolved, not stored and reloaded (tile GPUs)
        kSunShadows,       // SUN SHADOWS' caster pass and the receivers' variant
        kSurface,          // SURFACE DETAIL (relief, sheen, the AO and its depth pre-pass)
        kBloom,            // the bloom (and the MSAA scene pass's store / reload around it)
        kBoostBlur,        // the boost's radial / directional blur
        kSunCorona,        // the sun corona, its flares and its occlusion pass
        kEnvTrack,         // the car reflections' track pass (the env map's world part)
        kPostGame,         // the post passes at the GS's sizes (POST QUALITY Game)
        kAllOff,           // every one of the above
        kAllAgain,         // as set again (how much the scene itself moved)
        kSteps
    };
    static const char* name(int s) {
        static const char* const kNames[kSteps] = {"all on (as set)", "bloom's lean form (MSAA samples not stored / reloaded)", "SUN SHADOWS off", "SURFACE DETAIL off", "BLOOM off",
                                                   "boost blur off", "sun corona and flares off", "reflections' track pass off",
                                                   "post at the GS's sizes", "all of these off", "all on again"};
        return s >= 0 && s < kSteps ? kNames[s] : "done";
    }
    static constexpr double kSettle = 1.5;   // s at the start of a step not counted

    explicit PerfBisect(double step_s = 0) : step_s_(step_s > 0 ? std::max(step_s, 3.0) : 0) {}
    bool on() const { return step_s_ > 0; }
    bool done() const { return step_ >= kSteps; }
    int step() const { return done() ? -1 : step_; }

    // One race frame (the window, one view): its loop time, the GPU waits, the acquire's waits (ms), its GPU work when
    // sampled (< 0: not). Returns true when a step ended (its line is in `line`).
    bool frame(double loop_ms, double wait_ms, double acquire_ms, double gpu_ms, std::string& line) {
        if (!on() || done() || !(loop_ms > 0) || loop_ms > 500.0) return false;
        t_ += loop_ms * 1e-3;
        if (t_ >= kSettle) {
            cur_.loop.push_back(loop_ms);
            cur_.wait.push_back(wait_ms);
            cur_.acq.push_back(acquire_ms);
            if (gpu_ms >= 0) cur_.gpu.push_back(gpu_ms);
            cur_.secs += loop_ms * 1e-3;
        }
        if (t_ < step_s_) return false;
        line = summary(step_, cur_);
        lines_.push_back(line);
        cur_ = Acc{};
        t_ = 0;
        ++step_;
        return true;
    }
    // The race left the screen: the step starts again when it comes back (a step's numbers are one scene's)
    void pause() { cur_ = Acc{}; t_ = 0; }
    const std::vector<std::string>& lines() const { return lines_; }

private:
    struct Acc { std::vector<double> loop, wait, acq, gpu; double secs = 0; };
    static double pct(std::vector<double> v, double q) {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        const std::size_t i = std::min(v.size() - 1, std::size_t(q / 100.0 * double(v.size() - 1) + 0.5));
        return v[i];
    }
    static std::string summary(int s, const Acc& a) {
        char b[512];
        std::snprintf(b, sizeof b,
                      "step %d %s: %zu frames, %.1f fps; ms p50/p95: loop %.2f/%.2f, GPU work %.2f/%.2f (%zu sampled), "
                      "GPU waits %.2f/%.2f, acquire (drawable) %.2f/%.2f",
                      s, name(s), a.loop.size(), a.secs > 0 ? double(a.loop.size()) / a.secs : 0.0, pct(a.loop, 50), pct(a.loop, 95),
                      pct(a.gpu, 50), pct(a.gpu, 95), a.gpu.size(), pct(a.wait, 50), pct(a.wait, 95), pct(a.acq, 50), pct(a.acq, 95));
        return b;
    }
    double step_s_ = 0, t_ = 0;
    int step_ = 0;
    Acc cur_;
    std::vector<std::string> lines_;
};

}  // namespace b3replay

#endif
