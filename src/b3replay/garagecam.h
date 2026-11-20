// garagecam.h -- the garage camera (remaster-features §5.2, 2026-11-19): on the front end's car pages (SELECT CAR, YOUR
// GARAGE, any page CB3World::RenderFrontEnd draws a car on: Tick::fe_car) the right stick, or a left-button drag on the
// car, turns the view around the car; a second after the stick is let go the game's own spin goes on from there.
//
// Renderer only. The game turns its camera round the car by itself (the car pages' orbiting camera); we never write
// the guest. The view is the game's camera turned about the car: yaw about the vertical through the car, pitch about
// the horizontal through the point the camera looks at, by offsets kept per tick (Tick::fe_orbit). The distance to that
// point is kept: no zoom, so the camera cannot be pushed into the car or the room. The height is clamped: never under
// the game's own (its circle round the car is clear all the way round; below it the eye would meet the room's traffic
// cones), and the eye at most kMaxEyeAboveCar over the car. The front end's room (US/C5_V1's backdrops, the car at its
// origin; measured 2026-11-19 from the geometry): within 7.75 m of the car's vertical nothing between 1.06 m (the cones'
// tips, in the band the eye travels; the game's eye passes at 1.145 m) and 3.29 m (the lowest ceiling beam), so the
// eye stays in 1.145 .. 2.71 m. (A first cut allowed 0.40 rad up: the eye reached 4 m, above the beams and the
// ceiling, which then crossed the picture.)
//
// While the stick is pushed (or a drag moves), the view holds still against the game's spin and follows the stick
// instead; kIdleSeconds after the last input the game's spin is eased back in over kResumeSeconds, from the angle the
// view has then (the offsets stay: no snap back to the game's angle). Leaving the car pages ends it (the next visit
// starts with the game's view). With no input ever, the offsets stay 0 and the frames are the game's, byte for byte.
//
// Game time, as photo mode's (photomode.h): the input is stamped with the game's poll on the game thread (note_poll,
// from b3r_submit: the stick as the window's pads or a script's test pad have it, the drags since the last poll) and
// the render thread steps the offsets once per tick as the feed keeps it (on_tick, in poll order, dt from the ticks'
// VBlanks). So an offscreen shot of poll N shows the view at N however far the game runs ahead.
//
// Input: player 1's right stick (every player 1 pad, the physical stick before the bindings: on the PlayStation layout
// the game's right stick too, which its menus never read [C: CB3MenuInput::Update 0x21A5F0 reads the left stick, 0x18 /
// 0x19, and gaeMenubuttons 0x4B9BC0's buttons, controls 0-13]), a scripted test pad's (B3NATIVE_TEST_PAD; nativepad.h
// kTestPadCode + 34..37), and the menu mouse's drags on a car page (menumouse.h: there a left click without a drag is
// still the page's Cross). PC SETTINGS > CAR > GARAGE > GARAGE CAMERA Off (b3settings garage_camera 0): none
// of it (the mouse's click on a car page presses Cross on the button's press again, as before).
#ifndef B3REPLAY_GARAGECAM_H
#define B3REPLAY_GARAGECAM_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>

#include "capture.h"

namespace b3replay {

// The pure part (tests: test_garagecam)
struct GarageOrbit {
    static constexpr float kIdleSeconds = 1.0f;      // the stick at rest this long: the game's spin comes back
    static constexpr float kResumeSeconds = 0.6f;    // eased in over this
    static constexpr float kDeadZone = 0.18f;        // the stick's (as the launcher's showroom turntable)
    static constexpr float kYawRate = 2.2f;          // rad/s at full stick
    static constexpr float kPitchRate = 1.0f;        // rad/s at full stick
    static constexpr float kDragYaw = 3.6f;          // rad per window width dragged
    static constexpr float kDragPitch = 1.6f;        // rad per window height dragged
    static constexpr float kPitchUp = 0.40f;         // the elevation at most this above the game's
    static constexpr float kPitchDown = 0.0f;        // and this below it (none: the room's cones)
    static constexpr float kMinElevation = 0.03f;    // never lower than this over the horizontal (rad)
    static constexpr float kMaxElevation = 0.80f;    // nor higher than this
    static constexpr float kMaxEyeAboveCar = 2.4f;   // the eye at most this over the car's origin (m; under the beams)
    static constexpr float kCut = 0.35f;             // a game turn larger than this in one tick is a cut, not the spin
    // The drift guard (padpaint, 2026-11-19): a worn stick resting just past the dead zone must not hold the view for
    // ever. A deflection counts as input while it is firm (past kFirm on either axis), or while the stick moves (a change
    // over kMoveDelta from where it last moved) and for kIdleSeconds after; a stick sitting still at a smaller
    // deflection stops counting then (the game's spin resumes), until it moves again
    static constexpr float kFirm = 0.6f;
    static constexpr float kMoveDelta = 0.05f;

    bool engaged = false;   // input seen in this run of car ticks
    float yaw = 0;          // the view's turn from the game's camera about the vertical (rad, -pi..pi)
    float pitch = 0;        // its elevation above the game's (rad)
    float idle = 0;         // seconds since the last input
    float resume = 1;       // 0: the view held (the game's spin taken out), 1: the game's spin fully back
    bool have_prev = false;
    float prev_angle = 0;   // the game camera's angle round the car at the last tick
    float ref_x = 0, ref_y = 0;   // the drift guard: the stick where it last moved
    float still = 0;              // seconds since it moved (kMoveDelta)

    // The stick's value past the dead zone, rescaled to 0..1 (sign kept)
    static float dead(float v);
    // Whether this tick's stick counts as input (the drift guard above; dead zone and all), and the guard stepped
    bool stick_counts(float dt, float stick_x, float stick_y);
    // One tick of dt seconds: the stick (-1..1, y down positive, dead zone not yet applied), the drag since the last
    // tick (window fractions), the game camera's angle round the car (camera_angle), its elevation there, and the yaw's
    // sign that moves the camera to the screen's right (yaw_right). Stick right: the camera goes right round the car;
    // stick down: it rises (as the launcher's turntable); a drag turns the car with the pointer.
    // `max_elevation`: the elevation the eye may reach at all (eye_cap_elevation: kMaxEyeAboveCar)
    void step(float dt, float stick_x, float stick_y, float drag_x, float drag_y, float game_angle, float game_elevation,
              float right = 1.0f, float max_elevation = kMaxElevation);
    void reset() { *this = GarageOrbit{}; }
};

// The view's pivot for camera `c` and the car's matrix `car`: on the vertical through the car's position, at the height
// where the camera's view ray passes it. False when the camera looks straight up / down or sits on that vertical.
bool orbit_pivot(const Camera& c, const Mat34& car, float pivot[3]);
// The game camera's angle round the car relative to the car's heading (its bearing from the car about the vertical, as
// orbit_camera's yaw turns it, less the car's), its elevation over the pivot (radians), and the sign of a yaw that moves
// it to the screen's right (+1 / -1)
float camera_angle(const Camera& c, const Mat34& car);
float camera_elevation(const Camera& c, const float pivot[3]);
float yaw_right(const Camera& c, const float pivot[3]);
// The elevation over `pivot` at which camera `c`'s eye (kept at its distance from the pivot) is kMaxEyeAboveCar over the
// car's origin `car_y`
float eye_cap_elevation(const Camera& c, const float pivot[3], float car_y);
// `c` turned about `pivot`: `yaw` about the vertical (R_y: x' = x cos + z sin, z' = z cos - x sin), then
// `pitch` about the horizontal across the view (positive: up). The distance to the pivot and what the camera looks at
// (relative to it) are kept; the rows stay orthonormal; the LOD position follows.
void orbit_camera(Camera& c, const float pivot[3], float yaw, float pitch);
// The offsets between two ticks' (yaw by its shorter way round)
void orbit_lerp(const float a[2], const float b[2], float frac, float out[2]);

class GarageCam {
public:
    static GarageCam& instance();

    // PC SETTINGS > GARAGE CAMERA (default On); off: no input taken, the views the game's
    void set_enabled(bool on) { enabled_.store(on); }
    bool enabled() const { return enabled_.load(); }
    // render thread, each frame: the PC SETTINGS page or photo mode has the input (the stick is not ours then)
    void set_blocked(bool on) { blocked_.store(on); }
    // window thread: player 1's pads' right stick (-1..1, y down positive; the larger deflection of the pads)
    void set_window_stick(float x, float y);
    // game thread: the scripted test pad's right stick (nativepad.h kTestPadCode + 34..37)
    void set_test_stick(float x, float y);
    // render thread (the menu mouse, on a car page): a drag by (dx, dy) window fractions
    void add_drag(float dx, float dy);
    // game thread (b3r_submit): the input as it is now applies from `poll` on
    void note_poll(std::uint32_t poll);
    // render thread (LiveFeed::push, every tick in order): the tick's offsets (Tick::fe_orbit); not a car tick: the
    // orbit ends
    void on_tick(Tick& t);
    // render thread: a car page is on screen (the menu mouse drags instead of clicking there)
    bool car_page() const { return car_page_.load(); }

private:
    GarageCam() = default;
    struct Input {
        std::uint32_t poll = 0;
        float x = 0, y = 0, dx = 0, dy = 0;
    };
    std::mutex mu_;
    std::atomic<bool> enabled_{true}, blocked_{false}, car_page_{false};
    float win_x_ = 0, win_y_ = 0, test_x_ = 0, test_y_ = 0, drag_x_ = 0, drag_y_ = 0;   // (under mu_)
    Input last_;                 // the last stamped (under mu_)
    std::deque<Input> inputs_;   // stamped, not yet taken by the render thread (under mu_)
    // render thread
    GarageOrbit orbit_;
    Input now_;
    std::uint32_t prev_vblank_ = 0;
    bool log_ = false, log_checked_ = false;
    int log_every_ = 0;
};

}  // namespace b3replay

#endif
