// fastforward.h -- FAST LOADING's display rule (fastload1, tools/native/fast_load.h): while native_play runs a load
// unpaced (b3r_fast_forward(1), src/b3replay/inproc.cpp), its ticks arrive many times faster than real time and carry
// no latch stamp, so the display clock (clock.h) shows the newest tick at each frame (live.cpp display_x: the "newest"
// latency rule) instead of chasing them; at b3r_fast_forward(0) the clock starts again from the next ticks.
//
// loadlag1: and the renderer's word back. With FAST LOADING the race's first tick came before the renderer had its
// route (the ~160 ticks between the STATIC.DAT header and the race pass in ~0.2-0.4 s; the route takes ~0.4-0.7 s),
// so the race was heard under the loading screen. scene_busy(): the renderer is still building the route the header
// named; scene_epoch(): one more pass of its loop (feed drained, scene updated). native_play waits on them
// (b3r_scene_wait) at a fast load's end, on wall time only: the game's VBlanks and their state do not change.
#ifndef B3REPLAY_FASTFORWARD_H
#define B3REPLAY_FASTFORWARD_H

#include <atomic>
#include <cstdint>

namespace b3replay {

inline std::atomic<bool>& fast_forward() {
    static std::atomic<bool> on{false};
    return on;
}

inline std::atomic<bool>& scene_busy() {
    static std::atomic<bool> busy{false};
    return busy;
}

inline std::atomic<std::uint64_t>& scene_epoch() {
    static std::atomic<std::uint64_t> n{0};
    return n;
}

}  // namespace b3replay

#endif
