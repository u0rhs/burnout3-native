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

// vpreload (2026-11-24): the scene wait (b3r_scene_wait, native_play at a fast load's end, the game held) also asks the
// renderer to load the race's vehicle models then (View::preload_vehicles), not at their first draw in the race (each a
// frame's hitch: 35-50 ms with an HD pack). Asked counts the waits, done the ones the render thread has answered.
inline std::atomic<std::uint32_t>& vehicles_preload_asked() {
    static std::atomic<std::uint32_t> n{0};
    return n;
}
inline std::atomic<std::uint32_t>& vehicles_preload_done() {
    static std::atomic<std::uint32_t> n{0};
    return n;
}
// (Fable) true while a scene wait is waiting: the render thread preloads only then, and stops between models once the
// wait has ended (its cap, the game's quit); the wait marks its request answered as it ends, however it ends
inline std::atomic<bool>& vehicles_preload_open() {
    static std::atomic<bool> open{false};
    return open;
}

inline std::atomic<std::uint64_t>& scene_epoch() {
    static std::atomic<std::uint64_t> n{0};
    return n;
}

// scenewait (Fable): what the wait may go on for past its cap, set by the renderer: the route's decode running on its
// worker or its GPU half on the render thread (route_building), the vehicle preload running (vehicles_preload_running),
// and the window away (minimised / hidden / occluded: no extension then, the player is not watching)
inline std::atomic<bool>& route_building() {
    static std::atomic<bool> on{false};
    return on;
}
inline std::atomic<bool>& vehicles_preload_running() {
    static std::atomic<bool> on{false};
    return on;
}
inline std::atomic<bool>& window_away() {
    static std::atomic<bool> on{false};
    return on;
}

// scenewait (2026-11-24, a player report with an HD pack: "a couple of hitches at the start of the race"): the first
// race on a track with an empty HD texture cache built its route in 3.5 s (509 replacements converted), so the wait's
// cap (native_play's: the load's real-time length, at most 3 s) ended first: the race was shown 0.6 s before its scene
// and the vehicle preload, which runs inside the wait, never ran. Past the caller's cap the wait goes on while the scene
// is actually being made (`building`: the route's decode or GPU half running, the preload running, or the route still
// pending with the render loop going round -- never with the window away), up to kSceneBuildMaxMs in all; done, it ends
// at once (a failed route, a stalled loop or a minimised window end it near the cap). A warm cache is far below the cap (route ~0.4 s, preload ~0.3 s), and a run without the scene
// wait (unpaced shot runs: the golden) is not touched.
constexpr int kSceneBuildMaxMs = 10000;
inline bool scene_wait_more(int waited_ms, int max_ms, bool building) {
    return waited_ms < max_ms || (building && waited_ms < kSceneBuildMaxMs);
}

}  // namespace b3replay

#endif
