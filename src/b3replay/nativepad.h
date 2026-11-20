// nativepad.h -- the live window as the native game's pad (play1, native_play). With --native-pad the window's keys
// (src/native/input_keys: the default table, ramped steering) and the SDL gamepads (src/native/input_report.cpp
// gamepad_sample) become DS2 reports, which the game thread fetches once per VBlank through b3render's C ABI
// (b3r_pad_sample, src/b3replay/inproc.cpp) and hands to the platform's pad model (sif::set_pad_sample). Only 32 bytes
// cross.
// pad2 (2026-11-11, local 2-player): the first gamepad connected is player 1's (port 1, with the keyboard and mouse),
// the second player 2's (port 2, plugged in while that pad is connected); input_keys.h PadSeats has the rules. The
// prompts, photo mode and the PC SETTINGS page follow player 1's devices only.
//
// Threads: the window thread calls handle() / poll_gamepads(); the game thread calls sample(). Both under one mutex;
// the steering ramp advances on the game thread (dt = its VBlank period), so it follows game time, not frame rate.
// Viewer keys stay usable with Ctrl / Cmd (input_keys_sdl.h leaves those to the window).
#ifndef B3REPLAY_NATIVEPAD_H
#define B3REPLAY_NATIVEPAD_H

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "../native/input_bindings.h"
#include "../native/input_keys.h"
#include "../native/pad_actuator.h"
#include "../native/pad_gyro.h"
#include "../native/pad_triggers.h"
#include "prompts.h"

namespace b3replay {

class NativePad {
public:
    static NativePad& instance();

    // --native-pad: on (opens the SDL gamepad subsystem; call on the window thread after SDL_Init)
    void enable();
    bool enabled() const { return enabled_.load(); }

    // Window thread: one event; true if the pad used it (keys, focus loss, gamepads coming and going).
    bool handle(const SDL_Event& e);
    // Window thread, once per frame: the open gamepads' state -> their merged report.
    void poll_gamepads();

    // Game thread: the report for this VBlank (keys composed with the gamepads); the ramp advances by dt seconds.
    std::array<std::uint8_t, 32> sample(float dt);
    // pad2 (local 2-player): port 2's report for this VBlank into `out` -- the second gamepad's (input_keys.h PadSeats:
    // pads by connection order; the keyboard and mouse stay player 1's). False (out untouched) while no pad is in
    // player 2's seat: port 2 empty. With one gamepad or none it is always false (the behaviour before pad2).
    bool sample2(std::array<std::uint8_t, 32>& out);
    // mac5: the report the last sample() made (the PC SETTINGS page reads the window's pad while the game sees neutral)
    std::array<std::uint8_t, 32> last() const;
    // Scripted input (tests, b3r_pad_key): as a key event on the window.
    // padlayout (checks only): with B3NATIVE_TEST_PAD=xbox|ps|switch|generic a scripted pad of that SDL type is player 1's
    // (tools/native/race_check.sh SCENARIO=padlayout). Its inputs are script codes kTestPadCode + an SDL_GamepadButton
    // (0..20), + 32 / 33 the left / right trigger fully pressed, + 34 / 35 / 36 / 37 the right stick up / left / right /
    // down (garagecam1: 35-37); the pad goes through the
    // player's bindings (its layout for the type, in the game's context) as a window pad does. Without the variable
    // these codes are ignored.
    static constexpr std::uint16_t kTestPadCode = 1024;
    void key(std::uint16_t scancode, bool down);
    // devnav1: PageUp (bit 0) / PageDown (bit 1) held on the keyboard (the window's or a script's): the PC SETTINGS
    // page's previous / next section. No pad button: the game never sees them.
    int page_keys() const { return page_keys_.load(); }
    // settingsui: player 1's right stick pushed up (1) / down (2): the PC SETTINGS page's previous / next section on any
    // controller layout (the PS2 report's L2 / R2 carry nothing in the menus on the Xbox layout)
    int section_stick() const { return section_stick_.load(); }
    // mac4 (b3r_pad_context): the key map's context, from the game thread (driving: the race simulation runs)
    void set_driving(bool on);

    // The window was closed: the game is asked to end (native_play polls b3r_quit_requested).
    void request_quit() { quit_ = true; }
    bool quit_requested() const { return quit_.load(); }
    // mac5: the window has the keyboard focus (native_play holds the game while it has not)
    bool focused() const { return focused_.load(); }
    // mac5: PAUSE WHEN UNFOCUSED (PC SETTINGS): off, the window counts as focused for native_play's hold
    void set_hold_unfocused(bool on) { hold_unfocused_.store(on); }
    bool hold_unfocused() const { return hold_unfocused_.load(); }
    // Accessibility, BOOST: Toggle (PC SETTINGS; b3settings boost_mode 1; input_keys.h BoostLatch): opt-in, it changes
    // the reports the game reads. Off (Hold, the default) the reports are untouched.
    void set_boost_toggle(bool on) { boost_toggle_.store(on); }
    bool boost_toggle() const { return boost_toggle_.load(); }
    // speedrun1: INSTANT RESTART (PC SETTINGS; b3settings instant_restart 1; input_keys.h RestartMacro): opt-in, it
    // injects the pause menu's RESTART presses. F5 (a window key or a script's) or Select + Start on player 1's pad asks
    // for it; the game thread starts it at its next sample while driving (else it is dropped). Off: nothing changes.
    void set_instant_restart(bool on) { restart_on_.store(on); }
    bool instant_restart() const { return restart_on_.load(); }
    // carsel1: CAR SERIES ON L1 / R1 (PC SETTINGS; b3settings series_shoulders 1; input_keys.h ShoulderSeries): opt-in, it
    // changes the reports the game reads on the car select page. native_play reads it each VBlank (b3r_series_shoulders)
    // and translates there, where the game's current page is known. Off: nothing changes.
    void set_series_shoulders(bool on) { series_on_.store(on); }
    bool series_shoulders() const { return series_on_.load(); }
    bool restarting() const;
    std::uint64_t samples_taken() const { return taken_.load(); }
    // rumble1 (PC SETTINGS > CONTROLLER; src/native/pad_actuator.h): RUMBLE On / Off, its strength (percent) and the
    // triggers' motors. native_play reads rumble_on() each VBlank (b3r_rumble_mode: the game's actuator requests are
    // acknowledged while it is on) and hands the game's motors per port (set_motors, b3r_rumble; game thread). The window
    // thread gives them to the pads in poll_gamepads: port 1's to player 1's pads, port 2's to player 2's
    // (SDL_RumbleGamepad, SDL_RumbleGamepadTriggers with TRIGGER RUMBLE where the pad has them), refreshed while they
    // run; a port not handed for 250 ms (the game held or ended) and RUMBLE Off stop its motors.
    void set_rumble(bool on, int strength_percent, bool triggers) {
        rumble_on_.store(on);
        rumble_strength_.store(strength_percent);
        rumble_triggers_.store(triggers);
    }
    bool rumble_on() const { return rumble_on_.load(); }
    // 1.0.1 ANALOG TRIGGERS (input_keys.h GamepadTuning::analog_pedals): from PC SETTINGS, live
    void set_analog_pedals(bool on) { analog_pedals_.store(on); }
    void set_motors(int port, std::uint8_t small, std::uint8_t large);
    // pad1 (PC SETTINGS > CONTROLLER > ADAPTIVE TRIGGERS; src/native/pad_triggers.h): presentation only, off by default
    // (then no effect is ever sent). On: DualSense (PS5) pads get trigger effects from the game's state the window reads
    // (set_trigger_game, window thread, each frame: port 0 player 1's, 1 player 2's), in poll_gamepads: resistance on the
    // trigger bound to the brake, a light one on the accelerator's, which buzzes while boosting, a kick on both at a
    // crash; free while crashed, in menus, paused, with the PC page or photo mode open, and when turned off.
    void set_adaptive_triggers(bool on, int strength_percent) {
        triggers_on_.store(on);
        triggers_strength_.store(strength_percent);
    }
    bool adaptive_triggers() const { return triggers_on_.load(); }
    void set_trigger_game(int port, const b3native::pad::TriggerGame& g);
    // pad1: the pads as they were before the game (window thread, when the window closes): triggers free, sensors off
    void release_pads();
    // mac5: the device the prompts show (the button prompts, the video skip hint): the last one used -- a key press, a
    // gamepad's button, a gamepad connected, or a stick / trigger held past half way (prompts.h PromptDeviceTracker:
    // its hysteresis); the PlayStation's (the game's own prompts) until any input. A gamepad's kind is the label SDL
    // gives its bottom face button (Cross: PlayStation, B: Nintendo, A: Xbox, else generic). B3_PROMPT_DEVICE=keyboard|
    // playstation|xbox|nintendo|generic fixes it (previews, offscreen shots).
    PromptDevice device() const;
    // mac5: the key caps the keyboard's prompts show (the key map's keys, in the menus' or the driving context);
    // remap1: for a pad device, the glyph positions of player 1's gamepad bindings (the defaults: PromptKeys{})
    PromptKeys prompt_keys(PromptDevice d = PromptDevice::kKeyboard) const;

    // remap1 (src/native/input_bindings.h, controls.ini): the players' bindings. Player 1's keys and every player 1 pad,
    // player 2's pad; player 2's keys (none by default) drive port 2 too, which is then plugged in. The defaults give
    // the reports of before, byte for byte.
    void set_controls(const b3native::input::Controls& c);
    b3native::input::Controls controls() const;
    // padlayout (1.2.1, CONTROLLER LAYOUT): the SDL_GamepadType of the pad a player uses (0 player 1, 1 player 2): the
    // last of the player's pads to press a button, connect or push a stick / trigger past half way (else the first
    // open one); -1: none connected. Each pad drives its player's layout for its own type (Auto); this one is what the
    // prompts and the CONTROLS page show.
    int pad_type(int player) const;
    // remap1: the PC SETTINGS page's rebind: the next physical input of the device (a key down, a gamepad button, a stick
    // or trigger pushed past half way from rest) is taken instead of reaching the pad; Escape (or a right click; a
    // keyboard capture also a gamepad's East / Back) cancels. take_capture: true once with what came.
    struct Capture {
        bool cancel = false;
        b3native::input::PhysInput in;
    };
    void begin_capture(b3native::input::Device d);
    void end_capture();
    bool take_capture(Capture& out);
    bool capturing() const;
    // rebind2 (input_bindings.h PortAction): the port actions a key drives (a bit per PortAction; 0 during a rebind or
    // for a window shortcut such as Ctrl + key), and the gamepad ones that went down since the last call (Fullscreen,
    // Screenshot, Bug report; the window thread takes them once per frame). The window acts on them; Restart and photo
    // mode's act inside.
    std::uint64_t port_mask(std::uint16_t scancode, unsigned sdl_mod = 0) const;
    std::uint64_t take_port_pad();
    // hdkey: a script's HD TEXTURES key presses (key(): native_play --keys), each stamped with the first poll it applies to
    // (the newest poll handed to the renderer + 1, PhotoMode::game_poll), and taken by the render thread: those up to
    // `upto` (an offscreen shot of poll N shows the key's state at N), in order
    std::vector<std::uint32_t> take_hd_presses(std::uint32_t upto = 0xFFFFFFFFu);
    // todrace (tools/native/race_time.h): native_play says each VBlank whether a track page (Single Event's, Multiplayer's) is up
    // (b3r_race_time_page); while it is, the TIME OF DAY action's key and chord (PortAction::RaceTime) are taken from the
    // game's input (the game sees none of them) and each press is counted for native_play (b3r_race_time_presses)
    void set_race_time_page(bool up) { race_page_.store(up); }
    int take_race_time_presses() { return race_presses_.exchange(0); }
    // The first key bound to a port action as a label ("F9"; "-" when none): for the texts that name it
    std::string port_key_text(b3native::input::PortAction a) const;
    // Ctrl / GUI held, and that modifier is not itself bound to anything: a window shortcut, not a game key
    bool shortcut_mods(unsigned sdl_mod) const;

    // mac5 (remaster-features §8, menumouse.h): the mouse goes to MenuMouse (positions as fractions of the window); a
    // click or the wheel counts as the keyboard for the prompts (the key caps stay while the mouse is used), a key or a
    // pad button hides the cursor. Scripted (b3r_pad_mouse): kind 0 move to (x, y), 1 button n down, 2 up, 3 wheel n.
    void mouse(int kind, float x, float y, int n);
    // mac5: typed text for the game's on-screen keyboard (native_play's b3r_text_mode while the keyboard is up). On: the
    // window's text input (SDL_StartTextInput) feeds take_text, Backspace / Enter come as 8 / 10, and the keys that type
    // (letters, digits, space, punctuation, Shift, Backspace, Enter) no longer press pad buttons; arrows, Escape, Tab and
    // the rest still do. type(): scripted text (b3r_pad_text).
    void set_text_mode(bool on) { text_mode_.store(on); }
    bool text_mode() const { return text_mode_.load(); }
    // settingsui: a key that types while the game's keyboard is up (a letter, a digit, Enter ...: not a shortcut then);
    // the window's shortcuts check it first (a shortcut rebound to a letter does not fire while a name is typed)
    bool typing(std::uint16_t scancode, unsigned sdl_mod) const;
    void sync_text_input(SDL_Window* w);   // window thread, once per frame: SDL's text input follows the mode
    void type(const char* utf8);
    int take_text(char* out, int max);

private:
    std::array<std::uint8_t, 32> last_{};
    mutable std::mutex mu_;
    b3native::input::KeyPad keys_;
    b3native::input::KeyPad keys2_{std::vector<b3native::input::KeyBinding>{}};   // remap1: player 2's keys (none by default)
    b3native::input::Controls controls_ = b3native::input::default_controls();     // remap1 (under mu_)
    bool keys2_on_ = false;                                                         // player 2 has keys (under mu_)
    int capture_ = 0;                     // remap1: 0 none, 1 keyboard, 2 gamepad (under mu_)
    bool capture_have_ = false;
    Capture capture_out_;
    std::vector<std::pair<SDL_JoystickID, std::array<bool, 10>>> capture_rest_;   // per pad: each analog input seen at rest
    void capture_got(const Capture& c);   // (mu_ held)
    std::array<std::uint8_t, 32> pads_ = b3native::input::neutral_sample();    // player 1's pads merged
    bool have_pads_ = false;
    std::array<std::uint8_t, 32> pad2_ = b3native::input::neutral_sample();    // pad2: player 2's pad
    bool have_pad2_ = false;
    b3native::input::PadSeats seats_;     // pad2: which pad is whose (under mu_)
    b3native::input::BoostLatch boost2_;  // pad2: player 2's BOOST Toggle (under mu_)
    std::vector<SDL_Gamepad*> open_;
    std::atomic<bool> enabled_{false}, quit_{false}, focused_{true}, hold_unfocused_{true}, boost_toggle_{false};
    std::atomic<int> page_keys_{0};
    std::atomic<int> section_stick_{0};   // settingsui   // devnav1: PageUp (1) / PageDown (2) held
    b3native::input::BoostLatch boost_;   // (under mu_)
    b3native::input::RestartMacro restart_;   // speedrun1 (under mu_)
    bool restart_pending_ = false, combo_prev_ = false;   // (under mu_)
    std::atomic<bool> restart_on_{false};
    std::atomic<bool> series_on_{false};   // carsel1
    void ask_restart(const char* by);     // (mu_ held)
    void page_key(std::uint16_t scancode, bool down);   // devnav1: page_keys_ (the section keys: port actions)
    bool shortcut_mods_locked(unsigned sdl_mod) const;  // (mu_ held)
    std::uint64_t port_pad_prev_ = 0;                   // (under mu_) the gamepad port actions held at the last poll
    std::atomic<std::uint64_t> port_pad_edges_{0};
    std::vector<std::uint32_t> hd_presses_;   // hdkey (under mu_)
    std::atomic<bool> race_page_{false};      // todrace
    std::atomic<int> race_presses_{0};
    b3native::input::PageChord race_chord_;   // todrace: the chord's withholding, kept until released (under mu_)
    bool race_keys_safe_ = true, race_pad_safe_ = true;   // todrace: the bindings take nothing the pages read (under mu_)
    void race_time_press(const char* by);
    std::atomic<std::uint64_t> taken_{0};
    PromptDeviceTracker tracker_;   // mac5 (under mu_)
    std::array<SDL_JoystickID, 2> active_pad_{};   // padlayout: each player's last used pad (under mu_)
    std::array<int, 2> pad_type_{-1, -1};          // padlayout: its SDL_GamepadType, -1 none (under mu_)
    std::array<int, 2> layout_logged_{-2, -2};     // padlayout: the layout last logged per player (under mu_)
    int test_pad_type_ = -1;                       // padlayout: B3NATIVE_TEST_PAD's SDL type, -1 none (under mu_)
    b3native::input::GamepadState test_pad_;       // its state (under mu_)
    std::array<std::uint8_t, 32> test_report() const;   // (mu_ held)
    void pad_used(SDL_JoystickID id);              // (mu_ held)
    bool keyboard_used_ = false;
    std::atomic<bool> text_mode_{false};   // mac5
    bool composing_ = false;     // settingsui: an input method's composition is open (its Enter commits it, not Done)
    double committed_at_ = -1;   // ... and when one was last committed (the Enter that did it may come just after)
    bool text_started_ = false;            // (window thread)
    std::vector<char> text_;               // (under mu_)
    float wheel_acc_ = 0;                  // (window thread)
    // rumble1
    std::atomic<bool> rumble_on_{false}, rumble_triggers_{false};
    std::atomic<bool> analog_pedals_{true};   // 1.0.1
    std::atomic<int> rumble_strength_{100};
    b3native::pad::Motors motors_[2];      // (under mu_) the game's, per port
    std::uint64_t motors_ms_[2] = {0, 0};  // (under mu_) when they were handed (SDL ticks)
    struct Rumbling {
        SDL_JoystickID id = 0;
        b3native::pad::RumbleOut out;
        std::uint64_t at = 0;
    };
    std::vector<Rumbling> rumbling_;       // (window thread) what each pad was last given
    void give_rumble(const std::vector<std::pair<SDL_Gamepad*, int>>& pads);   // (window thread)
    // pad1: gyro steering (per pad: its sensors and steering state) and adaptive triggers (what each pad was given)
    struct Motion {
        SDL_JoystickID id = 0;
        bool sensors = false;              // gyro (and accelerometer) on: the player's gyro is on and the pad has one
        bool recentre_prev = false;
        b3native::pad::GyroSteer steer;
        std::uint64_t log_ms = 0;
        bool fx_sent = false;              // a trigger effect was ever sent (then the pad is put back free at the end)
        b3native::pad::Ds5Block fx_last{};
        b3native::pad::TriggerFx fx;
    };
    std::vector<Motion> motion_;           // (window thread)
    Motion& motion_of(SDL_JoystickID id);
    bool drive_prev_ = false;              // (window thread) the driving context at the last poll (yaw zeroed at a start)
    b3native::pad::TriggerGame trigger_game_[2];   // (under mu_)
    std::atomic<bool> triggers_on_{false};
    std::atomic<int> triggers_strength_{100};
    void give_triggers(const std::vector<std::pair<SDL_Gamepad*, int>>& pads);   // (window thread)
};

}  // namespace b3replay

#endif
