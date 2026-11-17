// Keyboard -> PS2 DualShock 2 report for the native game (mac1; the Mac has no controller). docs/mac.md "Keys".
//
// The core here has no SDL and no I/O (unit-tested in tests/test_input_keys.cpp): keys go in as SDL scancodes (USB HID
// usages, the numbers SDL_Scancode uses), a 32-byte DS2 report comes out -- the sample that
// b3native::platform::sif::set_pad_sample(port, sample) hands to the pad model (platform_sif.h). The SDL3 event
// adapter is src/native/input_keys_sdl.h.
//
// Report layout (docs/specs/input.md §1): bytes 0-1 digital buttons, active low, bit
// = libpad2 button index (SCE_PAD2_SELECT 0 .. SCE_PAD2_SQUARE 15, SDK libpad2.h); bytes 2-5 sticks (2/3 right X/Y,
// 4/5 left X/Y; neutral 0x80 / 0x7F); bytes 6-17 pressures (right, left, up, down, triangle, circle, cross, square,
// L1, R1, L2, R2); bytes 18-21 0x80.
// What the game reads [C retail asm]: CGtInputDevicePS2DualShock2::Update (0x214CD0) fills its controls from
// scePad2GetButtonInfo; CB3ControllerMapping::GetAccelerator (0x12FA40) = control 2 = ANALOG_CROSS, GetBrake
// (0x12F970) = control 1 = ANALOG_SQUARE, GetSteering (0x12F850) = control 24 = STICK_LX plus controls 6 / 7 =
// ANALOG_LEFT / ANALOG_RIGHT (the D-pad's pressures). So steering is ramped on both: the left stick's X and the D-pad
// left / right pressures (their digital bits are set at once, for menus).
//
// Steering ramp: a held steer key moves its axis to full in `ramp_up` seconds, a released one back to 0 in
// `ramp_down`; reversing passes through 0. step(dt) advances it (call once per pad update, e.g. each VBlank).
// A real pad and the keys combine (compose): a button is pressed if either has it, a pressure is the larger, a stick
// axis is whichever is further from centre.
//
// Context (mac4, 2026-11-08): a binding applies in menus, while driving, or in both (KeyCtx). The host says which
// (KeyPad::set_driving; native_play: driving = the game read the player's accelerator, GetAccelerator 0x12FA40, in the
// last 4 VBlanks), so W / Up accelerate in a race and move up in a menu, and Up never presses Cross (confirm) there.
// What the game reads while driving [C, from the asm of the functions named here]: Cross accelerate (GetAccelerator
// 0x12FA40), Square brake / reverse (GetBrake 0x12F970), the left stick X and
// the D-pad left / right pressures steering (0x12F850), R1 boost and, held while crashing, Impact Time + Aftertouch
// (0x12F160 -> StartBurn; GetCrashingDrivingControls), the left stick Y / D-pad up-down Aftertouch (0x12F720), L1
// look back (0x12F240), Triangle change camera (0x12F330), R2 Crashbreaker (0x12F610), Circle horn (0x12F6B0), L2
// music track skip (0x12F460 [L]), Start pause (0x12FB10).
#ifndef B3NATIVE_INPUT_KEYS_H
#define B3NATIVE_INPUT_KEYS_H

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace b3native::input {

// One DS2 control a key can drive. The first 16 are libpad2's button indices (SCE_PAD2_*).
enum class Ds2 : std::uint8_t {
    Select = 0, L3, R3, Start, Up, Right, Down, Left, L2, R2, L1, R1, Triangle, Circle, Cross, Square,
    SteerLeft,    // left stick X toward -1, ramped (and the D-pad left pressure when bound with Left: see below)
    SteerRight,
    StickUp,      // left stick Y, full at once
    StickDown,
    RStickLeft,   // remap1: the right stick's half axes, full at once (the default map binds none)
    RStickRight,
    RStickUp,
    RStickDown,
    Count
};
const char* ds2_name(Ds2 c);   // "cross", "steer_left", ... (for a settings file later)

// SDL scancodes used by the default map (SDL3 SDL_scancode.h = USB HID usage page 7).
namespace sc {
inline constexpr std::uint16_t A = 4, C = 6, D = 7, E = 8, H = 11, Q = 20, R = 21, S = 22, V = 25, W = 26, X = 27,
                               Z = 29, N1 = 30, N3 = 32, Return = 40, Escape = 41, Backspace = 42, Tab = 43, Space = 44,
                               Right = 79, Left = 80, Down = 81, Up = 82, KpEnter = 88, LShift = 225, RShift = 229;
}

enum class KeyCtx : std::uint8_t { Any, Menu, Drive };
struct KeyBinding {
    std::uint16_t scancode;
    Ds2 control;
    KeyCtx ctx = KeyCtx::Any;
};
// The default layout (docs/mac.md §5): one row per key -> control (in a context); a key may drive several controls and
// a control may have several keys. Left / Right / A / D drive the ramped D-pad left / right (steering + menus) and the
// ramped stick.
const std::vector<KeyBinding>& default_key_map();

struct RampTimes {
    float ramp_up = 0.12f;     // seconds from centre to full deflection
    float ramp_down = 0.06f;   // seconds from full back to centre (release, or the first half of a reversal)
};

class KeyPad {
public:
    explicit KeyPad(std::vector<KeyBinding> map = default_key_map(), RampTimes ramp = {});
    void set_map(std::vector<KeyBinding> map) { map_ = std::move(map); }
    const std::vector<KeyBinding>& map() const { return map_; }

    void key(std::uint16_t scancode, bool down);   // a key went down / up (repeats are ignored)
    void release_all();                            // focus lost: every key up, axes back to centre at once
    void step(float dt);                           // advance the steering ramp by dt seconds
    void set_driving(bool on) { driving_ = on; }   // the bindings' context: driving or menus (default menus)
    bool driving() const { return driving_; }
    float steer() const { return steer_; }         // -1 .. 1, the ramped steering value

    // The keyboard's own report (neutral where no key is held).
    std::array<std::uint8_t, 32> sample() const;
    // The keyboard combined with a real pad's report (`pad` may be nullptr: the keyboard alone).
    std::array<std::uint8_t, 32> compose(const std::uint8_t* pad) const;

    bool held(Ds2 c) const;   // any key bound to c is down

private:
    std::vector<KeyBinding> map_;
    RampTimes ramp_;
    std::array<bool, 512> down_{};
    bool driving_ = false;
    float steer_ = 0;                   // the left stick's X
    std::array<float, 2> dpad_{};       // the D-pad left / right pressures, 0..1
};

// An SDL gamepad in SDL's standard layout (no SDL here: the adapter fills it). Axes as SDL gives them: sticks
// -32768..32767 (y: down positive), triggers 0..32767.
struct GamepadState {
    bool south = false, east = false, west = false, north = false, back = false, start = false;
    bool left_shoulder = false, right_shoulder = false, dpad_up = false, dpad_down = false, dpad_left = false,
         dpad_right = false;
    std::int16_t left_x = 0, left_y = 0, left_trigger = 0, right_trigger = 0;
    // remap1: the rest of SDL's standard layout (the default bindings use none of them; input_bindings.h can)
    bool guide = false, left_stick = false, right_stick = false, misc1 = false, touchpad = false;
    std::array<bool, 4> paddles{};   // SDL's order: right 1, left 1, right 2, left 2
    std::int16_t right_x = 0, right_y = 0;
    // pad1 (gyro steering, pad_gyro.h; opt-in): the window's pad layer sets these while the player's gyro is on and they
    // drive. gyro_use 0 (the default): the steering is the bindings' alone, the report as before byte for byte; 1: the
    // gyro's steering (-1..1, right positive) added to it (clamped); 2: in its place.
    std::uint8_t gyro_use = 0;
    float gyro_steer = 0;
};
struct GamepadTuning {   // user-visible settings (docs/specs/input.md §3)
    float stick_dead_zone = 0.24f;     // fraction of the stick's travel ignored around centre (clamped to 0..0.95)
    float trigger_threshold = 0.25f;   // trigger travel from which Cross / Square count as pressed (clamped to 0.01..1)
    // 1.0.1: ANALOG PEDALS. An analog input (a trigger) on Cross (accelerate) or Square (brake) drives the right stick's
    // vertical axis instead of the button: the game's analog throttle and brake (CB3ControllerMapping::GetAccelerator
    // 0x12FA40 / GetBrake 0x12F970 add control 20 / 21, the right stick up / down, through DeadzoneAxis and a 4th power;
    // the button counts as full from 0.1). Both pedals past the threshold together (a powerslide): both buttons, as
    // before. Off: the buttons with the trigger's pressure, as 1.0.0.
    bool analog_pedals = true;
};
// 1.0.1: the right stick's travel (0..1) that makes the game's throttle / brake follow a pedal's travel (0..1) in a
// straight line: DeadzoneAxis 0x216E40 (max 0.9, dead zone 0.1 [C GtInputDevicePS2DualShock 0x2173F0]) then x^4, so
// the stick is sent at 0.1 + 0.8 * travel^(1/4); 0 for no travel.
float pedal_stick(float travel);
// A gamepad's DS2 report with the default bindings (docs/specs/input.md §3): South cross, East circle, West square, North triangle, shoulders L1 / R1,
// Start, Back select, the D-pad, the left stick; the right trigger also drives cross (analog throttle) and the left
// trigger square (analog brake), pressed from the threshold on. Stick axes past the dead zone, rescaled to full.
std::array<std::uint8_t, 32> gamepad_sample(const GamepadState& g, const GamepadTuning& t = {});

// Two reports combined (KeyPad::compose's rule): a button pressed on either, the larger pressure, each stick axis the
// one further from centre; bytes 18-31 from `b`.
std::array<std::uint8_t, 32> merge_samples(const std::uint8_t* a, const std::uint8_t* b);

// Boost toggle (accessibility, PC SETTINGS > INTERFACE > ACCESSIBILITY > BOOST: Toggle; b3settings boost_mode 1). OPT-IN
// and INPUT-CHANGING: off (Hold, the default) apply() leaves the report as it is, byte for byte. On, while driving, a
// press of R1 (boost; any key or pad button bound to it) latches it: the report then carries R1 held (its digital bit
// and full pressure) until the next press, whatever the button does meanwhile. The latch drops when the driving
// context ends (menus, the results, a crash's own camera if the game stops reading the accelerator), when Start is
// pressed (the pause menu) or when the toggle is switched off; a press that drops it is not passed on.
class BoostLatch {
public:
    // `r`: the composed report for this VBlank (changed in place when latched)
    void apply(std::array<std::uint8_t, 32>& r, bool toggle, bool driving);
    bool latched() const { return latched_; }

private:
    bool prev_ = false, latched_ = false;
};

// Instant restart (speedrun1; PC SETTINGS > INTERFACE > SPEEDRUN > INSTANT RESTART: On; b3settings instant_restart 1).
// OPT-IN and INPUT-INJECTING: the event is restarted by the game's own pause menu -- the macro presses Start (the pause
// menu opens on RESUME), Down (RESTART), Cross (the game's "restart?" dialog), then the dialog's YES and Cross, each a
// press of kHold VBlanks at a fixed VBlank of the macro (the VBlank grid: one apply() per pad sample). While it runs
// the report is the macro's alone (neutral but its button: the player's hands do not reach the game); after its last
// step the player's reports pass again. Never a write of game memory: the game reads presses, as from a pad. Off (or
// never triggered) apply() leaves the report as it is, byte for byte. Steps [C: the pause page's choices,
// func_00342090 (CB3PausePage::UpdateSubStateMakeChoice [L]): row 1 opens dialog string 0x136; the timings and the
// dialog's default from runs, docs/pc-settings.md §7.3].
class RestartMacro {
public:
    struct Step { int at; Ds2 button; };
    static constexpr int kHold = 6;          // VBlanks each press is held (3 front-end polls at the menus' 30 Hz)
    static const std::vector<Step>& steps();
    static int length();                     // VBlanks from the trigger to the report given back
    // Start it (true), unless it is running already (false).
    bool trigger();
    bool active() const { return t_ >= 0; }
    int vblank() const { return t_; }        // VBlanks since the trigger (-1: idle)
    // Per pad sample (VBlank): while active, `r` becomes the macro's report and the macro advances; true while active.
    bool apply(std::array<std::uint8_t, 32>& r);
    void cancel() { t_ = -1; }

private:
    int t_ = -1;
};

// Car series on the shoulders (carsel1; PC SETTINGS > INTERFACE > CONTROLLER > CAR SERIES ON L1 / R1: On; b3settings
// series_shoulders 1). OPT-IN and INPUT-CHANGING. The car select page changes the series (the car class: its carousel's
// column) with Left / Right and the car with Up / Down; it reads no shoulder button [C: CB3CarSelectPage::Update
// 0x318A80 reads Back, Confirm, Up, Down, Left, Right and Buddies (Square: the paint) only]. On, while that page is the
// menu's current page, a press of L1 is given to the game as Left and a press of R1 as Right (the D-pad's digital bit
// and full pressure; the shoulder's own bit and pressure cleared), from the press until its release, so the game's own
// series change runs: its sound, its animation, its auto-repeat while held. Only a press that begins on the page is
// translated (a shoulder held into the page passes as it is), and the translation ends when the page goes away or the
// row is switched off (the shoulder then passes as it is: never a Left / Right held into a race). Off (or not on the
// page) apply() leaves the report as it is, byte for byte.
class ShoulderSeries {
public:
    // `r`: the composed report for this VBlank (changed in place); `on`: the row; `page`: the car select page is up.
    // True when the report was changed.
    bool apply(std::array<std::uint8_t, 32>& r, bool on, bool page);
    bool translating(int side) const { return side ? right_ : left_; }   // 0: L1 -> Left, 1: R1 -> Right
    int presses() const { return presses_; }   // shoulder presses translated so far

private:
    bool left_ = false, right_ = false, prev_l_ = false, prev_r_ = false;
    int presses_ = 0;
};

// pad2 (2026-11-11): which window gamepad drives which controller port (local 2-player). The keyboard and mouse are
// always player 1 (port 1, always plugged in). Gamepads take seats in connection order: the first is player 1's (its
// report composed with the keys), the second player 2's (port 2, plugged in while it is seated). A seat is kept while
// its pad stays connected, as a PS2 pad stays in its port: a pad removed leaves its seat empty (player 2's: port 2
// unplugged, the game's own controller-removed handling) and the next pad seated fills the lowest empty seat. Pads past
// two wait (their reports composed into player 1's, as every pad was before pad2) and the oldest waiting one takes a
// seat that empties. One pad (or none): player 1 only, port 2 absent -- the behaviour before pad2. No SDL here: a pad
// is its SDL instance id (SDL_JoystickID, never 0).
class PadSeats {
public:
    enum class Role : std::uint8_t { None, Player1, Player2, Waiting };
    void added(std::uint32_t id);     // a pad connected (again: ignored)
    void removed(std::uint32_t id);   // a pad gone (an unknown one: ignored)
    Role role(std::uint32_t id) const;
    std::uint32_t seat(int player) const { return seats_[player & 1]; }   // 0 = empty
    // player 1's report takes this pad (player 1's seat or a waiting pad), player 2's report takes only player 2's seat
    bool drives_player1(std::uint32_t id) const { const Role r = role(id); return r == Role::Player1 || r == Role::Waiting; }
    bool port2_connected() const { return seats_[1] != 0; }
    bool any_player1_pad() const { return seats_[0] != 0 || !waiting_.empty(); }
    std::size_t pads() const { return (seats_[0] != 0) + (seats_[1] != 0) + waiting_.size(); }

private:
    std::array<std::uint32_t, 2> seats_{};
    std::vector<std::uint32_t> waiting_;   // connection order
};

// The 32 neutral bytes (equal to platform_sif's neutral_pad_sample()).
std::array<std::uint8_t, 32> neutral_sample();
// The report byte (6..17) of a button's pressure; -1 for Select, L3, R3, Start (digital only). input_report.cpp
// (with the stick bytes and gamepad_sample).
int pressure_byte(Ds2 c);
// Stick bytes from -1..1 (docs/specs/input.md §2; y: -1 = up).
std::uint8_t stick_x_byte(float v);
std::uint8_t stick_y_byte(float v);

}  // namespace b3native::input

#endif
