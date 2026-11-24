// trigger_rumble.h -- issue #16 (1.4.0): TRIGGER RUMBLE on Xbox One / Series controllers on Windows.
//
// SDL 3.4.2's default Windows controller path (XInput) has no trigger motors: SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN
// is false for an Xbox pad there, so the port never sends SDL_RumbleGamepadTriggers (src/b3replay/nativepad.cpp). SDL's
// RawInput driver (SDL_HINT_JOYSTICK_RAWINPUT, default off) with its XInput / Windows.Gaming.Input correlation
// (SDL_HINT_JOYSTICK_RAWINPUT_CORRELATE_XINPUT, default on; set too, to say what is meant) reaches the trigger motors
// (SDL 3.4.2's RAWINPUT_JoystickInit reads the first once; RawInput then takes the XInput-capable pads only, and the
// XInput driver adds none). SDL_SetHint is normal priority: an environment variable SDL_JOYSTICK_RAWINPUT=0 (or
// SDL_JOYSTICK_RAWINPUT_CORRELATE_XINPUT=0) still wins, on purpose: a player can turn the path off without the row.
// The first is read when the joystick subsystem starts (the second at each pad's open), so both are set before SDL_Init:
// the game's window (src/b3replay/live.cpp) and the launcher (src/b3launcher/main.cpp, its Controllers page's rumble
// test) set them from pc_settings.ini's rumble_triggers, and only when it is on: with TRIGGER RUMBLE off (the default) the controller
// path is the one every 1.3.x release used. Not on other systems (their default drivers already have the trigger motors
// where the pad does). The row applies at the next start on Windows (b3settings page.cpp next_start_row).
// Header only; needs SDL3's headers.
#ifndef B3PORT_TRIGGER_RUMBLE_H
#define B3PORT_TRIGGER_RUMBLE_H

#include <SDL3/SDL_hints.h>

namespace b3port {

// The rule, pure (tests): the RawInput controller path is asked for exactly on Windows with TRIGGER RUMBLE on
constexpr bool trigger_rumble_rawinput(bool rumble_triggers, bool windows) { return windows && rumble_triggers; }

// Before SDL_Init: sets both hints when the rule says so. Returns whether it did (for the log line).
inline bool apply_trigger_rumble_hints(bool rumble_triggers) {
#ifdef _WIN32
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    if (!trigger_rumble_rawinput(rumble_triggers, windows)) return false;
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT_CORRELATE_XINPUT, "1");
    return true;
}

}  // namespace b3port

#endif
