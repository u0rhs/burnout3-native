// Control remapping (remap1, docs/remaster-features.md §3 "Custom mapping", docs/pc-settings.md §7.9): which physical
// input produces which PS2 button, per player (1, 2) and device class (keyboard, gamepad), saved as controls.ini in
// the user data folder. Mapped at the input layer: the game reads the same 32-byte DualShock 2 reports with the same
// value ranges as before; a binding only chooses which key or gamepad input drives a report byte. The pad recordings
// and replay files keep the reports (post-mapping), so a replay plays the same whatever the bindings are later.
//
// The defaults are today's mapping exactly: player 1's keyboard is default_key_map() (input_keys.h), every gamepad is
// gamepad_sample()'s layout, player 2's keyboard has no keys. With the defaults every report is byte for byte the one
// before remap1 (tests/test_input_bindings.cpp).
//
// The actions are the DS2's 16 buttons and the two sticks' half axes (Ds2 values 0..23). How an input drives one:
//   - a key or gamepad button on a button: its digital bit and full pressure; on a stick half axis: full deflection
//     (the keyboard's left stick X and D-pad left / right keep their steering ramp, input_keys.h);
//   - a trigger or stick half axis (past the stick dead zone, rescaled) on a button: its pressure follows the travel,
//     the digital bit from the trigger threshold on (GamepadTuning); on a stick half axis: the travel, unscaled.
//   Several inputs on one action: pressed if any is, the largest pressure / deflection.
// Keyboard bindings have a context (KeyCtx): menus, driving or both, as the default layout (W accelerates while driving
// and moves up in menus). The pages edit one context at a time ("view"): rebinding an action in the driving view leaves
// its menu keys as they are.
//
// No SDL and no I/O but load_controls / save_controls: the window adapters are input_keys_sdl.h.
#ifndef B3NATIVE_INPUT_BINDINGS_H
#define B3NATIVE_INPUT_BINDINGS_H

#include "input_keys.h"
#include "pad_gyro.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b3native::input {

constexpr int kActions = 24;   // Ds2::Select .. Ds2::RStickDown
constexpr int kPlayers = 2;
// The actions in the order the pages list them (face buttons, shoulders, Start / Select, D-pad, sticks, L3 / R3)
const std::array<Ds2, kActions>& action_order();
const char* action_key(Ds2 a);        // the ini's name: "cross", "dpad_left", "lstick_left", ...
const char* action_label(Ds2 a);      // "CROSS", "D-PAD LEFT", "LEFT STICK LEFT", ...
const char* action_use(Ds2 a, bool driving);   // what the game does with it ("accelerate"), "" when nothing known
std::optional<Ds2> parse_action(std::string_view s);

// A gamepad's physical inputs, SDL's standard layout: the buttons in SDL_GamepadButton's order (value - 1), then the
// sticks' half axes and the triggers.
enum class PadInput : std::uint8_t {
    None = 0,
    South, East, West, North, Back, Guide, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
    DpadUp, DpadDown, DpadLeft, DpadRight, Misc1, RightPaddle1, LeftPaddle1, RightPaddle2, LeftPaddle2, Touchpad,
    LeftXNeg, LeftXPos, LeftYNeg, LeftYPos, RightXNeg, RightXPos, RightYNeg, RightYPos, LeftTrigger, RightTrigger,
    Count
};
constexpr int kPadButtonInputs = 21;   // South .. Touchpad (SDL_GAMEPAD_BUTTON_SOUTH .. SDL_GAMEPAD_BUTTON_TOUCHPAD)
bool pad_input_analog(PadInput p);     // the half axes and the triggers
const char* pad_input_key(PadInput p); // the ini's name: "south", "left_x-", "right_trigger", ...
std::optional<PadInput> parse_pad_input(std::string_view s);
// The labels a page shows: by the pad's family (positions are SDL's; the labels on them differ)
enum class PadStyle : std::uint8_t { Generic, PlayStation, Xbox, Nintendo };
std::string pad_input_label(PadInput p, PadStyle style);
// The input's travel 0..1 in a state: a button 0 / 1, a stick half axis past the dead zone rescaled to 0..1 (as
// gamepad_sample's sticks), a trigger its travel
float pad_input_amount(const GamepadState& g, PadInput p, float stick_dead_zone);

// Keys (SDL scancodes = USB HID usages): a name for the ini ("Return", "Left Shift", "Comma", "sc100" for one without
// a name), parsed back in any case; a label for the pages ("ENTER", "L SHIFT", ",")
std::string key_name(std::uint16_t scancode);
std::optional<std::uint16_t> parse_key(std::string_view s);
std::string key_label(std::uint16_t scancode);

struct KeyBind {
    std::uint16_t scancode = 0;
    KeyCtx ctx = KeyCtx::Any;
    bool operator==(const KeyBind&) const = default;
};
// padlayout (1.2.1): a gamepad binding has a context too (A boosts while driving and confirms in menus). From a bare
// PadInput: both contexts (every binding before 1.2.1).
struct PadBind {
    PadInput in = PadInput::None;
    KeyCtx ctx = KeyCtx::Any;
    constexpr PadBind() = default;
    constexpr PadBind(PadInput i, KeyCtx c = KeyCtx::Any) : in(i), ctx(c) {}
    bool operator==(const PadBind&) const = default;
};
using PadBindings = std::array<std::vector<PadBind>, kActions>;   // per action (Ds2 value)

// padlayout (1.2.1): CONTROLLER LAYOUT, per player. Auto picks by the pad (pad_family): a PlayStation pad gets the
// PlayStation layout, every other pad (Xbox, Nintendo by position, generic) the Xbox layout. Xbox / PlayStation: that
// layout whatever the pad. Custom: the player's own bindings (PlayerControls::pad; never changed by the other choices).
// The layouts (docs/PLAYING.md "Controllers"), in PS2 buttons:
//   Xbox: RT Cross (accelerate) and LT Square (brake) in both contexts; while driving A R1 (boost), B R2 (Crashbreaker),
//         X L1 (look back), Y Triangle (camera), View / Back L2 (music), LS click Circle (horn); in menus A Cross
//         (confirm), B Triangle (back), X Square, Y Circle, View / Back Select; LB / RB L1 / R1 in both (look back and
//         boost too; the car series on L1 / R1 in menus); Start, the D-pad and the left stick as the game's.
//   PlayStation: the PS2 game's with the triggers as the pedals: Cross / R2 accelerate, Square / L2 brake (R2 / L2
//         analog), R3 (the right stick click) the Crashbreaker, L3 the music track skip; Circle, Triangle, L1, R1,
//         Start, Share / Create (Select) button for button; the right stick the game's right stick (up accelerates,
//         down brakes); both contexts. (The 1.x layout with the right stick added.)
enum class PadLayout : std::uint8_t { Auto, Xbox, PlayStation, Custom };
const char* pad_layout_key(PadLayout l);     // the ini's: "auto", "xbox", "playstation", "custom"
std::optional<PadLayout> parse_pad_layout(std::string_view s);
// A pad's family from its SDL_GamepadType value (no SDL here; nativepad.cpp static_asserts the values): PS3 / PS4 /
// PS5 PlayStation, Xbox 360 / One Xbox, the Switch Pro, Joy-Cons and GameCube Nintendo, the rest Generic
constexpr int kSdlPadXbox360 = 2, kSdlPadXboxOne = 3, kSdlPadPS3 = 4, kSdlPadPS4 = 5, kSdlPadPS5 = 6, kSdlPadSwitchPro = 7,
              kSdlPadJoyConPair = 10, kSdlPadGameCube = 11;
PadStyle pad_family(int sdl_gamepad_type);
// The layout a player's pad of this family gets (Auto resolved: Xbox, PlayStation or Custom)
PadLayout resolved_layout(PadLayout l, PadStyle family);
const PadBindings& layout_bindings(PadLayout l);   // Xbox / PlayStation (Auto: Xbox's; Custom: the 1.x defaults)
// pad1 (gyro steering, src/native/pad_gyro.h; docs/pc-settings.md §7.12): per player, OPT-IN and input-changing. Off by
// default: the reports are the ones before, byte for byte. While driving, on pads with a gyroscope: the pad's roll (tilt)
// or yaw (turn) steers, mixed with the stick's steering or in its place; the recentre button makes the present pose
// straight (it still drives what it is bound to as well).
struct GyroControls {
    std::uint8_t mode = 0;            // b3native::pad::GyroMode: 0 off, 1 mix, 2 replace
    std::uint8_t axis = 0;            // b3native::pad::GyroAxis: 0 roll, 1 yaw
    int sensitivity = 100;            // percent, 25..300 in steps of 25 (pad_gyro.h)
    int dead_zone = 2;                // degrees, 0..10
    PadInput recentre = PadInput::Touchpad;   // a button (gyro_recentre_choices()); None: no button
    bool operator==(const GyroControls&) const = default;
};
// One poll of a pad's gyro steering (the window's pad layer, nativepad.cpp; tests): the recentre button's press (a rising
// edge of `recentre_held`) recentres; yaw is zeroed when driving starts; while driving and with sensor data the state
// takes the steering (gyro_use = the mode). Off, or not driving: `s` is left as it is. True when it recentred.
bool apply_gyro(GamepadState& s, b3native::pad::GyroSteer& steer, const GyroControls& g, bool driving, bool driving_started,
                bool& recentre_held);
// The buttons the pages offer for RECENTRE, in their order (None first)
const std::array<PadInput, 8>& gyro_recentre_choices();
// The trigger roles for adaptive triggers (pad_triggers.h TriggerRole as 0 none, 1 brake, 2 accelerator): what each
// physical trigger drives with these bindings while driving (Square: the brake, Cross: the accelerator)
std::uint8_t trigger_role(const PadBindings& b, bool right);

// ---- port actions (rebind2): what the window does that is not a DualShock 2 button ----
// Not part of the game's reports: the window's own shortcuts (restart, fullscreen, screenshot, bug report, the tuning
// overlay, the PC SETTINGS page's sections) and photo mode's controls. One set for both players: any key (a key is a
// scancode; no list of keys), and a gamepad input on any player 1 pad. A gamepad binding is a chord: every input of it
// held at once (Restart: Select + Start, Photo mode: L3 + R3; one input for the rest). The defaults are today's keys and
// combinations.
enum class PortAction : std::uint8_t {
    Restart, Fullscreen, Screenshot, BugReport, TuneOverlay, PhotoMode, SectionPrev, SectionNext, FpsView,
    PhotoForward, PhotoBackward, PhotoLeft, PhotoRight, PhotoUp, PhotoDown,
    PhotoLookLeft, PhotoLookRight, PhotoLookUp, PhotoLookDown, PhotoRollLeft, PhotoRollRight,
    PhotoWider, PhotoNarrower, PhotoFocusNear, PhotoFocusFar, PhotoFast, PhotoSlow,
    PhotoDof, PhotoBlurLess, PhotoBlurMore, PhotoBlurCycle, PhotoTarget, PhotoReset, PhotoHelp, PhotoExit,
    CarLook,   // reflect2: the CAR LOOK panel (DEVELOPER MODS)
    HdTextures,   // 1.2 (hdkey): the HD texture pack or the original textures, at once (the renderer keeps both)
    Count
};
constexpr int kPortActions = int(PortAction::Count);
const char* port_key(PortAction a);                 // the ini's name: "restart", "photo_forward", ...
const char* port_label(PortAction a);               // "INSTANT RESTART", "PHOTO: MOVE FORWARD", ...
const char* port_use(PortAction a);                 // what it does, for the pages
bool port_photo(PortAction a);                      // one of photo mode's (conflicts only with the other port actions)
bool port_has_pad(PortAction a);                    // a gamepad binding is meaningful (not the tuning overlay, the sections)
bool port_analog(PortAction a);                     // a gamepad stick / trigger axis drives it (photo mode's movement and look)
std::optional<PortAction> parse_port_action(std::string_view s);

struct PortControls {
    std::array<std::vector<std::uint16_t>, kPortActions> keys;   // scancodes: any of them
    std::array<std::vector<PadInput>, kPortActions> pad;         // a chord: all of them held
    bool operator==(const PortControls&) const = default;
};
const PortControls& default_port();
// The actions a key drives (a bit per PortAction)
std::uint64_t port_actions_of_key(const PortControls& p, std::uint16_t scancode);
// A gamepad's travel 0..1 of the action's chord (the smallest of its inputs; 0 when unbound), the sticks past `dead_zone`
float port_pad_amount(const GamepadState& g, const PortControls& p, PortAction a, float dead_zone);

struct PlayerControls {
    std::array<std::vector<KeyBind>, kActions> keys;   // per action (Ds2 value)
    PadBindings pad;                                   // the Custom layout's bindings (1.x: the only ones)
    PadLayout layout = PadLayout::Auto;                // padlayout (1.2.1)
    GyroControls gyro;                                 // pad1
    bool operator==(const PlayerControls&) const = default;
    bool has_keys() const;                              // any key bound (player 2: the keyboard drives port 2)
};
struct Controls {
    std::array<PlayerControls, kPlayers> player;
    PortControls port;   // rebind2
    // padlayout: the pad family each player's page shows and edits (set_page_family); not saved, not compared
    std::array<PadStyle, kPlayers> family{};
    bool operator==(const Controls& o) const { return player == o.player && port == o.port; }
};
enum class Device : std::uint8_t { Keyboard, Gamepad };

Controls default_controls();
const PlayerControls& default_player(int player);   // 0 / 1
// The keyboard bindings as KeyPad's table (one row per key and action; per action in its order)
std::vector<KeyBinding> key_map(const PlayerControls& p);
// padlayout: the bindings a pad of this family drives for the player (the layout, Auto resolved)
const PadBindings& pad_bindings(const PlayerControls& p, PadStyle family);
// What "back to the default" gives a player's pad of this family: the layout's (Custom: Auto's)
const PadBindings& pad_default(const PlayerControls& p, PadStyle family);
// A gamepad's report with these bindings in the context `now` (Menu / Drive: the bindings of that context and of both;
// Any: all of them). With the 1.x defaults (the Custom layout's, every binding of both contexts): gamepad_sample(g, t),
// byte for byte, in either context.
std::array<std::uint8_t, 32> gamepad_sample(const GamepadState& g, const PadBindings& b, const GamepadTuning& t = {},
                                            KeyCtx now = KeyCtx::Drive);
// The player's pad of this family (pad_bindings)
std::array<std::uint8_t, 32> gamepad_sample(const GamepadState& g, const PlayerControls& p, const GamepadTuning& t = {},
                                            KeyCtx now = KeyCtx::Drive, PadStyle family = PadStyle::Generic);

// ---- controls.ini ----
// # comment
// version = 2
// [player1.keyboard]                  player1 / player2, keyboard / gamepad
// cross = Return, Keypad Enter, K, Space@menus, Up@driving, W@driving
// [player1.gamepad]
// layout = custom                     padlayout: auto / xbox / playstation / custom (version 3)
// cross = south, right_trigger
// r1 = south@driving, right_shoulder  a context as a key's (version 3)
// l2 = none
// [player1.gyro]                      pad1: gyro steering (only the lines that differ from the defaults)
// mode = mix                          off / mix / replace
// axis = roll                         roll / yaw
// sensitivity = 100                   percent, 25..300 (steps of 25)
// dead_zone = 2                       degrees, 0..10
// recentre = touchpad                 a gamepad button's name (pad_input_key), or none
// [port.keyboard]                     rebind2: the window's shortcuts and photo mode's controls (PortAction), both players
// restart = F5                        a key list (any key; "sc100" for one without a name)
// [port.gamepad]
// restart = back+start                a chord: every input held at once
// One line per action that differs from its default (the rest keep theirs); "none" (or nothing) unbinds it. A key's
// @menus / @driving gives its context (else both). Unknown sections, actions and inputs are reported and skipped; a
// line none of whose inputs is valid keeps the action's default.
// Version 2 (padr2): the stick clicks are L2 / R2's defaults; reading a version 1 file (or one without a version line),
// a stick click the file binds to another action is not also L2 / R2's (the clicks drove nothing by default then).
// Version 3 (padlayout, 1.2.1): [playerN.gamepad] layout = auto / xbox / playstation / custom, and a gamepad input's
// @menus / @driving (as a key's). The gamepad lines are the Custom layout's bindings (against the 1.x defaults), kept
// whatever the layout. Without a layout line (every file before 1.2.1): Custom when a gamepad line changes a binding (a
// player's remap stays exactly as it was), else Auto.
constexpr int kControlsVersion = 3;
std::string to_ini(const Controls& c);
Controls controls_from_ini(std::string_view text, std::vector<std::string>* warnings = nullptr);
// false: no file (c = the defaults)
bool load_controls(const std::string& path, Controls& c, std::vector<std::string>* warnings = nullptr);
bool save_controls(const std::string& path, const Controls& c, std::string* why = nullptr);

// ---- editing (the PC SETTINGS page and the launcher) ----
struct PhysInput {
    Device device = Device::Keyboard;
    std::uint16_t key = 0;            // keyboard
    PadInput pad = PadInput::None;    // gamepad
    bool operator==(const PhysInput&) const = default;
};
// The view a page edits: KeyCtx::Menu or KeyCtx::Drive (Any: both), for the keyboard and (padlayout) the gamepad.
// padlayout: the gamepad functions read and edit the bindings the page shows: the player's layout for a pad of
// `family` (set_page_family; Generic until set). Every gamepad edit (rebind, remove_last, reset_action) first makes the
// shown layout the player's own bindings, then changes them: the layout becomes Custom.
void set_page_family(Controls& c, int player, PadStyle family);
PadStyle page_family(const Controls& c, int player);
// The layout's name as a page shows it: "Xbox", "PlayStation", "Custom", or Auto's (auto_text): "Auto: Xbox controller",
// "Auto: PlayStation controller", "Auto: Nintendo, Xbox layout", "Auto: controller, Xbox layout", "Auto: no controller,
// Xbox layout"; `connected`: the player has a pad (of the family)
std::string layout_text(const PlayerControls& p, PadStyle family, bool connected);
std::string auto_text(PadStyle family, bool connected);
// The player's layout set (Xbox / PlayStation / Auto / Custom): the Custom bindings stay as they are
void set_layout(Controls& c, int player, PadLayout l);
std::vector<PhysInput> bound(const Controls& c, int player, Device d, Ds2 action, KeyCtx view);
std::string bound_text(const Controls& c, int player, Device d, Ds2 action, KeyCtx view, PadStyle style);   // "ENTER, Z"; "-"
// The action's inputs in the view differ from what "back to the default" gives (reset_action): the pages mark it
bool moved(const Controls& c, int player, Device d, Ds2 action, KeyCtx view);
std::string input_label(const PhysInput& in, PadStyle style);
// What else an input drives in the view: the other actions of this player (the keyboard: of both players) that have
// it in a context overlapping `view`
struct Use {
    int player = 0;
    Ds2 action = Ds2::Cross;
    bool port = false;                        // rebind2: a port action (player is 0 then)
    PortAction port_action = PortAction::Restart;
    bool operator==(const Use&) const = default;
};
std::vector<Use> uses(const Controls& c, int player, Device d, Ds2 action, const PhysInput& in, KeyCtx view);
enum class Resolve : std::uint8_t { Swap, Clear, Keep };
// Bind `in` to the action in the view: replacing what it had there (add: next to it). The other uses of `in`
// (uses()): Swap gives them the action's previous first input in the view (none: they lose it), Clear removes it from
// them, Keep leaves them.
void rebind(Controls& c, int player, Device d, Ds2 action, const PhysInput& in, KeyCtx view, Resolve r, bool add = false);
void remove_last(Controls& c, int player, Device d, Ds2 action, KeyCtx view);   // the last input shown in the view
void reset(Controls& c, int player, Device d);   // that player's device to its defaults (a gamepad: its gyro, Auto too)

// rebind2: one action back to its default (a keyboard action: in the view), and everything back (both players, the port
// actions, the gyro)
void reset_action(Controls& c, int player, Device d, Ds2 action, KeyCtx view);
void reset_all(Controls& c);
// The port actions' editing (a device's key list or chord). Conflicts: the other port actions that have the input and,
// for an action that is not photo mode's, the game's actions (both players) that have it. Swap gives the others the
// action's previous first input.
std::vector<PhysInput> bound_port(const Controls& c, PortAction a, Device d);
std::string bound_port_text(const Controls& c, PortAction a, Device d, PadStyle style);   // "F5"; "SELECT + START"; "-"
std::vector<Use> uses_port(const Controls& c, PortAction a, Device d, const PhysInput& in);
// Replace (add: a key next to the others; for a chord one more input held with the rest)
void rebind_port(Controls& c, PortAction a, Device d, const PhysInput& in, Resolve r, bool add = false);
void remove_last_port(Controls& c, PortAction a, Device d);
void reset_port(Controls& c, PortAction a, Device d);   // that action's device back to its default
// An action's name in a message: "PHOTO: MOVE FORWARD", or "PLAYER 2 CROSS" for a game action of player 2
std::string use_label(const Use& u);

// The only key a rebind does not take: Escape, which cancels the capture (it keeps its other jobs: it closes pages and
// leaves photo mode). Every other key, the modifier keys too, can be bound.
bool key_reserved(std::uint16_t scancode);

// The rebind flow both pages run: waiting for an input (with a timeout), then a conflict to resolve, then done.
class RebindFlow {
public:
    enum class State : std::uint8_t { Idle, Waiting, Conflict, Done, Cancelled };
    static constexpr float kTimeout = 6.0f;   // seconds waiting before it cancels by itself
    void begin(int player, Device d, Ds2 action, KeyCtx view, bool add);
    void begin_port(PortAction action, Device d, bool add);   // rebind2: a port action (is_port())
    bool is_port() const { return port_; }
    PortAction port_action() const { return port_action_; }
    // Each frame while waiting: dt seconds; returns the state
    State tick(float dt);
    // The input pressed (a keyboard flow takes keys only, a gamepad flow gamepad inputs only: the rest is ignored).
    // No other use: applied at once (Done); else Conflict.
    State input(Controls& c, const PhysInput& in);
    void cancel();
    // In Conflict: the choice shown (0 Swap, 1 Clear, 2 Keep both), moved by step(-1 / +1), applied by choose()
    void step(int dir);
    void set_choice(int c) { if (c >= 0 && c < 3) choice_ = c; }
    int choice() const { return choice_; }
    State choose(Controls& c);
    State state() const { return state_; }
    bool active() const { return state_ == State::Waiting || state_ == State::Conflict; }
    float remaining() const { return kTimeout - waited_; }
    const std::vector<Use>& conflicts() const { return conflicts_; }
    const PhysInput& pressed() const { return pressed_; }
    const std::optional<PhysInput>& swap_input() const { return swap_; }   // what Swap gives the others (none: they lose it)
    int player() const { return player_; }
    Device device() const { return device_; }
    Ds2 action() const { return action_; }
    KeyCtx view() const { return view_; }
    // The text a page shows while active: "PRESS A KEY FOR CROSS (WHILE DRIVING) ..." / the conflict and its choices
    std::string message(PadStyle style) const;

private:
    State state_ = State::Idle;
    int player_ = 0, choice_ = 0;
    Device device_ = Device::Keyboard;
    Ds2 action_ = Ds2::Cross;
    KeyCtx view_ = KeyCtx::Any;
    bool add_ = false;
    bool port_ = false;
    PortAction port_action_ = PortAction::Restart;
    float waited_ = 0;
    PhysInput pressed_;
    std::optional<PhysInput> swap_;   // what Swap gives the other uses
    std::vector<Use> conflicts_;
};

// The button glyph a prompt shows for each PS2 button with these gamepad bindings: the face / shoulder / stick /
// Start / Back position of the first input bound to it that has one, as the PS2 button found there by default (South
// = Cross, East = Circle, West = Square, North = Triangle, LB / RB = L1 / R1, LT / RT = L2 / R2, LS / RS = L3 / R3,
// Start, Back = Select). Index and value: Ds2 values of the 16 buttons; with the defaults every button is its own.
// padlayout: in the context `now` (its bindings first, then those of both contexts; Any: all, in order)
std::array<Ds2, 16> pad_glyphs(const PadBindings& b, KeyCtx now = KeyCtx::Any);

}  // namespace b3native::input

#endif
