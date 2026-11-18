// photomode.h -- photo mode (remaster-features §5 "Photo mode", 2026-11-10): from the race's pause menu, a free camera
// over the frozen scene, the HUD and the pause menu hidden, for screenshots (F12).
//
// Renderer only. The camera is ours (a Camera built from our position / angles, handed to the draw as a free view, as
// b3replay's C free camera); the guest is never written. While photo mode is on, the game sees a neutral pad
// (b3r_settings_active, as for the PC SETTINGS page), so the pause menu does not react to the keys and pad that drive
// the camera; the game is paused already, so nothing in the simulation moves. With photo mode never entered, nothing
// here runs: the frames are the same as before it.
//
// Entering: P on the keyboard, or L3 + R3 together on a gamepad (the game's pad report carries neither: no pad
// binding), while the race's pause menu is up (native_play tells us: b3r_set_paused, from the pause page's Update) and
// the PC SETTINGS page is closed. A crash, Impact Time or a Takedown cam is reached the same way: pause there first.
// Leaving: P or Escape, or B / Circle or L3 + R3 on a gamepad; the game's camera is back at once (we never changed
// it). A pad's buttons still held at the exit stay withheld from the game until released (B would close the pause
// menu otherwise).
//
// Keys: WASD move, E / Q up / down, Shift fast, Alt slow, the mouse (relative mode) or the arrow keys look, the wheel
// or - / = the field of view, Z / C roll, R the game's camera again, H the on-screen display (photohud.h: all, readouts, none). Pad: left stick move, right stick
// look, RT / LT up / down, LB / RB roll, D-pad up / down the field of view, A / Cross held fast, X / Square the game's
// camera again, Y / Triangle the same. Held, the field of view zooms by a ratio (x2 or x0.5 in 0.6 s), the roll
// turns 1.2 rad/s and the focus 1.5 stops/s, each up to twice as fast after a second held; Shift / A x2, Alt x0.25
// (devnav1, 2026-11-13: photomode.cpp step()).
//
// Depth of field (photo2, 2026-11-12; PhotoDof below, b3gfx FrameParams::Dof): off until turned on, F or Back / Select;
// the blur strength (the aperture, levels 1-6, 4 at first) V / B or Start (cycling); the focus nearer / farther N / M or
// the D-pad left / right (held: 1.5 stops a second, one stop x2 the distance, up to 4 stops each way); what is in focus G or L3
// alone: the player's car (the GPU's autofocus at the car's middle on screen; the centre while the car is behind the
// camera or off screen) or the screen centre. The setting stays for the rest of the run (photo mode entered again
// keeps it). Renderer only: the frame is blurred, nothing else; a screenshot (F12) includes it.
//
// Threads: key() comes from the window thread (SDL events) or the game thread (scripted keys, b3r_pad_key); pad()
// and handle() from the window thread; advance() / camera() from the render thread; set_game_paused() and
// set_game_poll() from the game thread. One mutex.
//
// Game time (offscreen live mode, live.cpp; 2026-11-11): unpaced, the game runs ahead of the renderer by a varying
// number of polls (hundreds while shots are drawn), so a camera moved by the keys held when the render thread looks,
// by the polls drained since it last looked, and photographed as it is when the shot is drawn, depended on the two
// threads' speeds (race_check's photo scenario failed on an idle PC: the camera had taken one 0.25 s step with W
// held before shot B, and none after it). In game time the camera is a function of the game's polls alone: entering,
// leaving, R and every key change are stamped with the first poll they apply to (the newest poll handed to the
// renderer + 1, on the game thread that also hands the polls over); the render thread replays them a poll (1/60 s)
// at a time up to the poll it asks for, and a shot of poll N shows the camera at N. The window's viewer keeps
// advance() / camera() (real time).
#ifndef B3REPLAY_PHOTOMODE_H
#define B3REPLAY_PHOTOMODE_H

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "b3gfx/b3gfx.h"
#include "../native/input_bindings.h"
#include "capture.h"
#include "notice.h"
#include "photohud.h"
#include "prompts.h"

namespace b3replay {

// The window's gamepads merged (NativePad::poll_gamepads): sticks -1..1 (y down positive, as SDL), triggers 0..1
// rebind2: the fields are photo mode's roles, set by the window from the gamepad bindings of the photo actions
// (input_bindings.h PortAction::Photo*; the defaults are the layout in the text above): lx right - left, ly back -
// forward, rx look right - left, ry look down - up, lt down, rt up (-1..1 / 0..1, dead zone applied by the camera);
// south fast, slow, west game camera, north help, east leave, l1 / r1 roll left / right, up / down narrower / wider
// view, left / right focus nearer / farther, back depth of field, start blur levels, blur_less / blur_more, target the
// focus target (acts when released alone), toggle the photo mode chord (L3 + R3: enters and leaves).
struct PhotoPad {
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    bool south = false, east = false, west = false, north = false, l1 = false, r1 = false, target = false, toggle = false;
    bool up = false, down = false, left = false, right = false, start = false, back = false;
    bool slow = false, blur_less = false, blur_more = false;
    bool connected = false;
    bool any_button() const {
        return south || east || west || north || l1 || r1 || target || toggle || up || down || left || right || start || back || slow ||
               blur_less || blur_more || lt > 0.25f || rt > 0.25f;
    }
};

// The depth of field's pure parts (tests: test_photomode)
struct PhotoDof {
    static constexpr int kLevels = 6;
    static constexpr float kMaxStops = 4.0f;
    bool on = false;
    int level = 4;            // blur strength 1..kLevels
    int target = 0;           // 0: the player's car, 1: the screen centre
    float stops = 0;          // the focus offset: the distance x 2^stops (-kMaxStops..kMaxStops)
    // the largest blur circle's radius, a fraction of the output height (0 when off)
    float radius() const;
    void toggle() { on = !on; }
    void blur(int dir);       // the level -1 / +1, clamped; 0: the next level, cycling (the pad's Start)
    void next_target() { target = 1 - target; stops = 0; }
    void focus(float dstops); // nearer (< 0) / farther, clamped
    // the frame's parameters from camera `cam`; `car`: the player's car's position (nullptr: none); `view_proj`: the
    // frame's (column-major). The car in front of the camera and on screen: the GPU's autofocus at the car's middle as
    // the frame shows it (the depth of what is seen there: its nearest panels, not its centre; x 2^stops); without
    // `view_proj` the car's distance along the view. Else the autofocus at the screen centre. `subject`: 0 off, 1 the
    // car, 2 the centre.
    b3gfx::FrameParams::Dof params(const Camera& cam, const float* car, const float* view_proj = nullptr, int* subject = nullptr) const;
    // the help box's status ("DOF OFF", "DOF 4   FOCUS CAR", "DOF 2   FOCUS CENTRE X1.41"); subject 0: by the target
    std::string status(int subject) const;
};

// The free camera's pure parts (tests: test_photomode)
struct PhotoCam {
    float pos[3] = {};
    float yaw = 0, pitch = 0, roll = 0;   // radians; yaw 0 looks along +z, pitch up positive, roll clockwise positive
    float fov = 1.0f;                     // vertical field of view, radians
    // devnav1: how long the field of view, the roll and the focus keys have been held one way (their speed-up)
    struct Hold {
        int dir = 0;
        float held = 0;
    };
    Hold hold_fov, hold_roll, hold_focus;
    void forward(float f[3]) const;
    void right(float r[3]) const;         // screen right, level (no roll)
    // from a game camera: its position, its `at` as yaw / pitch, no roll, its vertical field of view
    void from(const Camera& c);
    // as a game camera (rows: screen left, up, at, position; the view window from `fov` at `game`'s aspect); the
    // rest (LOD inputs, the game's screen matrix, near / far) is `game`'s, the near plane at most 0.1 m
    Camera camera(const Camera& game) const;
};

// rebind2: the strip's table from the bindings: the first key of each action (two where the table has an alternative), the
// first button of each control as a glyph or a text chip
PhotoKeys photo_keys_from(const b3native::input::PortControls& b);

class PhotoMode {
public:
    static PhotoMode& instance();

    // game thread (native_play, b3r_set_paused): the race's pause menu is up. Off: photo mode ends.
    void set_game_paused(bool on);
    bool game_paused() const;
    // game thread (native_play: the pause menu's PHOTO MODE row, photorow1): enter as P does (stamped in game time).
    // False when it cannot be entered now (not paused, no single-view race picture) or is on already.
    bool request_enter(const char* how);
    // render thread: a race picture (one view) is on screen and no page (PC SETTINGS) over it: it can be photographed
    void set_scene(bool on);

    // A key (an SDL scancode) down / up from the window or a script. Returns true when photo mode took it (a key down
    // while on, or the key that turned it on); key ups are recorded and still passed on (the pad releases its keys).
    // `mods`: SDL_Keymod of the event (Ctrl / Cmd keys stay the viewer's).
    bool key(std::uint16_t scancode, bool down, bool repeat = false, unsigned mods = 0);
    // window thread: one SDL event (keys, the mouse while on). True when taken.
    bool handle(const SDL_Event& e);
    // window thread, once per frame: the gamepads (L3 + R3 enters / leaves; the camera's controls while on)
    void pad(const PhotoPad& p);
    // rebind2: the keys of photo mode's actions (the PortAction::Photo* ones, and PhotoMode's own key to enter and
    // leave); the defaults are the keys of the text above. Any thread; a change applies from the next key or step. The
    // controls strip (photohud.h) shows them: keys_ is made from the bindings (photo_keys_from).
    void set_bindings(const b3native::input::PortControls& b);

    bool active() const;
    // the game sees a neutral pad: photo mode on, or a pad button that left it still held
    bool holds_input() const;

    // render thread: advance the camera by dt seconds (the first call after entering starts it from `game`)
    void advance(float dt, const Camera& game);
    // render thread: the free camera as a game camera (false when off or not started)
    bool camera(const Camera& game, Camera& out) const;
    // Game time (see above). Game thread (b3r_submit): the newest poll handed to the renderer.
    void set_game_poll(std::uint32_t poll);
    // render thread, before the game can reach photo mode: stamp and keep the events from now on
    void enable_game_time();
    // render thread: replay the events up to `poll` (never backwards). `game_cam(p, cam)`: the game's camera at poll p
    // (the free camera starts there when photo mode is entered, or R pressed, at p)
    void advance_to(std::uint32_t poll, const std::function<bool(std::uint32_t, Camera&)>& game_cam);
    // render thread: the poll the replay has reached
    std::uint32_t game_time_poll() const;
    // any thread: the newest poll handed to the renderer (set_game_poll; hdkey stamps a script's HD TEXTURES key with it)
    std::uint32_t game_poll() const;
    // render thread: the free camera at `poll` as a game camera (false when photo mode is off there, not started, or
    // the replay is not at `poll`)
    bool camera_at(std::uint32_t poll, const Camera& game, Camera& out) const;

    // render thread: the depth of field of the window's photo frame from camera `cam` (`car`: the player's car's position
    // or nullptr); off: `out` stays off. And of the game-time shot at `poll` (as camera_at).
    void dof(const Camera& cam, const float* car, const float* view_proj, b3gfx::FrameParams::Dof& out);
    bool dof_at(std::uint32_t poll, const Camera& cam, const float* car, const float* view_proj, b3gfx::FrameParams::Dof& out) const;
    PhotoDof dof_state() const;

    // render thread: the photo UI (photohud.h: the readouts at the top right, the controls strip at the bottom), as
    // H / Y has it set; never part of a screenshot (drawn after it). `device` / `hud_size` are the window's button
    // prompts device and HUD SIZE level.
    void draw_help(b3gfx::Device& dev, int w, int h, float dt, PromptDevice device, int hud_size, std::vector<b3gfx::DrawItem>& out);
    PhotoUi ui() const;
    const PhotoKeys& keys() const { return keys_; }
    // the controls as plain text for the keyboard or the pad (logs, tests), with the field of view in degrees and the
    // depth of field's status (`subject` as PhotoDof::params gives it)
    static std::vector<std::string> help_lines(bool pad_prompts, int fov_deg, const PhotoDof& dof = PhotoDof{}, int subject = 0);

private:
    PhotoMode() = default;
    bool can_enter_locked() const;
    void enter_locked(const char* how);
    void leave_locked(const char* how);
    void cycle_ui_locked();

    mutable std::mutex mu_;
    bool paused_ = false, scene_ = false, active_ = false, started_ = false;
    PhotoUi ui_ = PhotoUi::kFull;
    bool ui_fresh_ = false;      // photo mode was entered: the UI slides in again (draw_help)
    PhotoKeys keys_;
    b3native::input::PortControls bind_ = b3native::input::default_port();   // rebind2 (under mu_)
    bool pad_latch_ = false;     // the pad button that left photo mode is still held
    bool pad_both_ = true;       // the toggle chord (L3 + R3) was down at the last pad() (an edge enters / leaves)
    bool l3_solo_ = false;       // the target button (L3) is held and the toggle chord has not been pressed with it (released: the focus target)
    PhotoDof dof_;               // the window's depth of field (key(), pad(), advance())
    int subject_ = 0;            // what it last focused on (dof()), for the help box
    PhotoPad pad_, pad_prev_;
    std::array<bool, 512> held_{};
    float mouse_dx_ = 0, mouse_dy_ = 0, wheel_ = 0;
    bool reset_ = false;
    PhotoCam cam_;
    std::uint64_t serial_ = 0;   // entries (logs)
    // game time
    struct Event {
        enum Kind : std::uint8_t { kEnter, kLeave, kKey, kReset };
        std::uint32_t poll;   // the first poll it applies to
        Kind kind;
        std::uint16_t scancode;
        bool down;
    };
    void record_locked(Event::Kind kind, std::uint16_t scancode = 0, bool down = false);
    bool game_time_ = false;
    std::uint32_t game_poll_ = 0;
    std::deque<Event> events_;
    struct Replay {
        bool active = false, started = false;
        std::uint32_t poll = 0, start_poll = 0;
        std::array<bool, 512> held{};
        PhotoCam cam;
        PhotoDof dof;
    } replay_;
    PhotoHud hud_;   // render thread
};

}  // namespace b3replay

#endif
