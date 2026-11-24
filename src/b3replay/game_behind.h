// game_behind.h -- slowcpu (issue #15): the game behind real time (native_play's pacer, tools/native/fast_load.h
// BehindWatch, through b3r_game_behind in src/b3replay/inproc.cpp), and the frame limit the renderer holds to meanwhile.
//
// On a slow CPU (the report: a 4-thread APU of 2012) the game's work in a race can take longer than a VBlank and the game
// runs slower than real time. A renderer drawing 144 frames a second then takes CPU the game needs (its own thread and
// the GPU driver's), so while the game is behind the renderer draws no faster than the game: 60 frames a second, or the
// player's FRAME RATE LIMIT when that is lower. Once the game has kept up for a few seconds, the limit is the player's
// again. Drawing only: the game is not touched.
#ifndef B3REPLAY_GAME_BEHIND_H
#define B3REPLAY_GAME_BEHIND_H

#include <atomic>

namespace b3replay {

inline std::atomic<bool>& game_behind() {
    static std::atomic<bool> v{false};
    return v;
}

constexpr double kBehindFps = 60.0;   // the game's own rate (59.94 VBlanks a second), as FRAME RATE LIMIT's 60

// The frame limit in effect: `max_fps` the player's (0: none), `behind` the game's state
inline double behind_limit(double max_fps, bool behind) {
    if (!behind) return max_fps;
    return max_fps > 0 && max_fps < kBehindFps ? max_fps : kBehindFps;
}

}  // namespace b3replay

#endif
