// SDL3 events -> b3native::input::KeyPad (src/native/input_keys.h). Header-only so that only a windowed build that
// already links SDL3 pulls SDL in; the core stays testable without it.
//
// Use in the native game's window loop (per SDL event, then once per pad update / VBlank):
//   b3native::input::handle_sdl_event(keys, e);
//   keys.step(1.0f / 60.0f);
//   const auto s = keys.compose(gamepad_sample_or_nullptr);
//   b3native::platform::sif::set_pad_sample(0, s.data());
#ifndef B3NATIVE_INPUT_KEYS_SDL_H
#define B3NATIVE_INPUT_KEYS_SDL_H

#include "input_bindings.h"
#include "input_keys.h"
#include "pad_gyro.h"

#include <SDL3/SDL.h>

namespace b3native::input {

// the default map's scancodes are SDL's own (input_keys.h keeps SDL out of the core)
static_assert(sc::A == SDL_SCANCODE_A && sc::C == SDL_SCANCODE_C && sc::D == SDL_SCANCODE_D && sc::E == SDL_SCANCODE_E &&
              sc::Q == SDL_SCANCODE_Q && sc::S == SDL_SCANCODE_S && sc::V == SDL_SCANCODE_V && sc::W == SDL_SCANCODE_W &&
              sc::X == SDL_SCANCODE_X && sc::Z == SDL_SCANCODE_Z && sc::N1 == SDL_SCANCODE_1 && sc::N3 == SDL_SCANCODE_3 &&
              sc::Return == SDL_SCANCODE_RETURN && sc::Escape == SDL_SCANCODE_ESCAPE &&
              sc::Backspace == SDL_SCANCODE_BACKSPACE && sc::Tab == SDL_SCANCODE_TAB && sc::Space == SDL_SCANCODE_SPACE &&
              sc::Right == SDL_SCANCODE_RIGHT && sc::Left == SDL_SCANCODE_LEFT && sc::Down == SDL_SCANCODE_DOWN &&
              sc::Up == SDL_SCANCODE_UP && sc::KpEnter == SDL_SCANCODE_KP_ENTER && sc::LShift == SDL_SCANCODE_LSHIFT &&
              sc::RShift == SDL_SCANCODE_RSHIFT && sc::H == SDL_SCANCODE_H && sc::R == SDL_SCANCODE_R);

// remap1 (input_bindings.h): a gamepad's buttons are SDL_GamepadButton + 1 up to the touchpad
static_assert(int(PadInput::South) == SDL_GAMEPAD_BUTTON_SOUTH + 1 && int(PadInput::Touchpad) == SDL_GAMEPAD_BUTTON_TOUCHPAD + 1 &&
              int(PadInput::LeftShoulder) == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER + 1 && int(PadInput::DpadRight) == SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1 &&
              int(PadInput::LeftPaddle2) == SDL_GAMEPAD_BUTTON_LEFT_PADDLE2 + 1 && kPadButtonInputs == SDL_GAMEPAD_BUTTON_TOUCHPAD + 1);
inline PadInput pad_input_of_button(int sdl_button) {
    return sdl_button >= 0 && sdl_button < kPadButtonInputs ? PadInput(sdl_button + 1) : PadInput::None;
}

// A gamepad's state as the bindings read it (nativepad.cpp poll_gamepads, the launcher's Controllers page)
inline GamepadState gamepad_state(SDL_Gamepad* g) {
    GamepadState s;
    s.south = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH);
    s.east = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST);
    s.west = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_WEST);
    s.north = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_NORTH);
    s.back = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK);
    s.start = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START);
    s.left_shoulder = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    s.right_shoulder = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    s.dpad_up = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP);
    s.dpad_down = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    s.dpad_left = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    s.dpad_right = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    s.left_x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    s.left_y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    s.left_trigger = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    s.right_trigger = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    s.guide = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_GUIDE);
    s.left_stick = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    s.right_stick = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    s.misc1 = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_MISC1);
    s.touchpad = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_TOUCHPAD);
    s.paddles = {SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1), SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1),
                 SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2), SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2)};
    s.right_x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX);
    s.right_y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY);
    return s;
}

// rebind2: Ctrl or GUI (Cmd) held is a window shortcut, unless that modifier is itself bound to something in the pad's
// map (then it is a game key, and the keys pressed with it reach the game too)
inline bool shortcut_modifiers(const KeyPad& pad, unsigned mod) {
    const auto bound = [&](std::uint16_t a, std::uint16_t b) {
        for (const KeyBinding& k : pad.map())
            if (k.scancode == a || k.scancode == b) return true;
        return false;
    };
    if ((mod & SDL_KMOD_CTRL) && !bound(SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL)) return true;
    if ((mod & SDL_KMOD_GUI) && !bound(SDL_SCANCODE_LGUI, SDL_SCANCODE_RGUI)) return true;
    return false;
}

// Returns true if the event was a key / focus event the pad used. Key repeats are ignored; keys with Ctrl / Cmd held
// are left to the window (shortcuts), as is Alt+Enter (fullscreen). F11 is a key like any other (rebind2: the window's
// FULLSCREEN key is a port action, input_bindings.h, and may be any key).
inline bool handle_sdl_event(KeyPad& pad, const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = e.type == SDL_EVENT_KEY_DOWN;
        if (down && e.key.repeat) return true;
        if (down && shortcut_modifiers(pad, e.key.mod)) return false;
        if (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) return false;   // Alt + Enter: fullscreen
        pad.key(static_cast<std::uint16_t>(e.key.scancode), down);
        return true;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        pad.release_all();
        return true;
    default:
        return false;
    }
}

// pad1 (gyro steering, pad_gyro.h): a gamepad sensor event into the pad's steering state; true if it was one (gyro or
// accelerometer). The sensor's own timestamp (ns) paces the integration, else the event's.
inline bool feed_gyro_event(b3native::pad::GyroSteer& g, const SDL_Event& e) {
    if (e.type != SDL_EVENT_GAMEPAD_SENSOR_UPDATE) return false;
    const SDL_GamepadSensorEvent& s = e.gsensor;
    if (s.sensor == SDL_SENSOR_GYRO) {
        g.gyro(s.data, s.sensor_timestamp ? s.sensor_timestamp : s.timestamp);
        return true;
    }
    if (s.sensor == SDL_SENSOR_ACCEL) {
        g.accel(s.data);
        return true;
    }
    return false;
}

// A function key keeps its port action (screenshot...) with Ctrl / Cmd reported held: no shortcut sits on Ctrl / Cmd + F-key, and
// macOS leaves the modifier flags of its own Cmd+Shift screenshot keys set, which must not kill F12 for the rest of the run
inline bool function_key_ignores_mods(unsigned scancode) {
    return (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) || (scancode >= SDL_SCANCODE_F13 && scancode <= SDL_SCANCODE_F24);
}

}  // namespace b3native::input

#endif
