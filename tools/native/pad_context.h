// pad_context.h -- padskip (issue #14, 2026-11-23): the pad's and the keys' context (input_keys.h KeyCtx: the driving
// bindings or the menus'), from what the game does this VBlank.
//
// The context was "driving" while the game called CB3ControllerMapping::GetAccelerator 0x12FA40 in the last 4 VBlanks
// (only CB3PlayerCarPhysics::GetDrivingControls 0x1BD020 and GetCrashingDrivingControls call it [C]). But the game's
// replays re-run the player's car through the same calls (CB3ReplaySystem plays the recording back: replay_view.h), so
// a replay counted as driving: Crash mode's CRASH RESULTS (CONTINUE / RETRY over the crash's replay) took an Xbox-layout
// pad's A as boost (R1) and Cross only from the right trigger -- a pedal under ANALOG PEDALS, so no button -- which left
// both triggers (the powerslide rule: CROSS SQUARE) as the only way on; the keyboard's Up was Cross there, Space boost.
// Now:
// - driving: the accelerator read lately, no replay playing (the world's "in playback" byte D_00665E53, replay_view.h
//   kPlaying), no film on the front end's decoder (movie_skip.h MovieSkip::film_playing);
// - playback: the accelerator read lately in a replay: the menus' bindings, the orbit camera still takes the right stick
//   (orbitcam.h; the right stick is bound to no game control in menus);
// - menus: the rest.
#ifndef B3NATIVE_PAD_CONTEXT_H
#define B3NATIVE_PAD_CONTEXT_H

#include <cstdint>

namespace b3native {

constexpr std::uint32_t kReplayPlayback = 0x00665E53u;   // [C] the world's replay flag "in playback" (replay_view.h kPlaying)
constexpr std::uint64_t kAccelRecentVBlanks = 4;         // the crash ticks run at 30 Hz: a call every 2 VBlanks there

enum class PadContext : int { kMenus = 0, kDriving = 1, kPlayback = 2 };   // (b3r_pad_context's argument)

// `accel_recent`: GetAccelerator called within the last kAccelRecentVBlanks; `playback`: kReplayPlayback set; `film`: a
// film on the front end's decoder
inline PadContext pad_context(bool accel_recent, bool playback, bool film) {
    if (!accel_recent || film) return PadContext::kMenus;
    return playback ? PadContext::kPlayback : PadContext::kDriving;
}

inline const char* pad_context_name(PadContext c) {
    return c == PadContext::kDriving ? "driving" : c == PadContext::kPlayback ? "menus (replay playing)" : "menus";
}

}  // namespace b3native

#endif
