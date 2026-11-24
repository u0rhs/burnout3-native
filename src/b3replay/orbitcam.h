// orbitcam.h -- the orbit camera in a race (orbitcam1, 2026-11-21): the right stick (or the mouse with its right button
// held) swings the chase camera round the player's car -- look left, right, behind, a little up or down -- and a moment
// after the stick is let go the view eases back to the game's chase view. Each player of a split-screen race has their
// own (their pad's right stick turns their view).
//
// Renderer only, as the garage camera (garagecam.h) whose turn about the car it reuses (orbit_pivot, orbit_camera): the
// game's camera is never written; the view drawn is the game's chase camera turned about the vertical through the car
// and tilted about the horizontal through the point it looks at, the distance kept. Only the game's chase cameras take
// it (Camera::chase, as CAMERA DISTANCE / HEIGHT: its takedown, crash and other cameras are drawn as the game frames
// them; a blend between them takes it in step), only with the player's car in front of the camera within kReach (so
// the bumper and in-car views, and the look-back view's own framing, are the game's; the look-back button keeps working:
// the orbit then turns the game's look-back view). The eye never goes below the point it turns about.
//
// The input (game time, as garagecam.h): the stick is stamped with the game's poll on the game thread (note_poll, from
// b3r_submit) and the offsets are stepped once per tick on the render thread (on_tick, from LiveFeed::push; dt from the
// ticks' VBlanks at the field rate the game's timer shows: 59.94, or 50 for a PAL disc), kept in Tick::race_orbit and interpolated between ticks. So an offscreen shot of poll N shows the
// view at N however far the game runs ahead, and with no input the frames are the game's, byte for byte.
//
// Which stick: each player's pads' physical right stick, only where it is free: a pad whose bindings (its layout for its
// type, in the driving context) use the right stick's axes -- the PlayStation layout's analog throttle and brake, or a
// Custom layout's -- keeps it the game's, and that pad does not orbit. The scripted test pad (B3NATIVE_TEST_PAD,
// nativepad.h kTestPadCode + 34..37) the same, as player 1's. Player 1's mouse: a drag with the right button held,
// pressed while driving (menumouse.h; its moves taken as they come, so stamped in game time as the stick). The input
// counts only while the game is in its driving context (no menu then; not the pause menu: there the view eases back),
// not while the PC SETTINGS page or photo mode has the input.
//
// Settings: PC SETTINGS > DISPLAY > CAMERA > ORBIT CAMERA (b3settings orbit_camera, default On): Off, none of it.
// Replays: a replay file records the game's pad (the PS2 report), which the orbit is not part of; while one plays, the
// viewer's own right stick (or mouse) orbits the view.
#ifndef B3REPLAY_ORBITCAM_H
#define B3REPLAY_ORBITCAM_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "capture.h"
#include "trackrays.h"

namespace b3replay {

// The pure part (tests: test_orbitcam): one player's look offsets
struct ChaseOrbit {
    static constexpr float kDeadZone = 0.18f;       // the stick's
    static constexpr float kYawRate = 3.2f;         // rad/s at full stick (behind in about a second)
    static constexpr float kPitchRate = 1.2f;       // rad/s at full stick
    static constexpr float kDragYaw = 4.0f;         // rad per window width dragged
    static constexpr float kDragPitch = 1.6f;       // rad per window height dragged
    static constexpr float kPitchUp = 0.35f;        // the eye at most this above the game's elevation (rad)
    static constexpr float kPitchDown = 0.10f;      // and this below it
    static constexpr float kIdleSeconds = 0.75f;    // the stick at rest this long: the view eases back
    static constexpr float kReturnSeconds = 0.5f;   // over this
    // the drift guard (as GarageOrbit's): a stick resting just past the dead zone counts only while it is firm or
    // has moved within the last kIdleSeconds
    static constexpr float kFirm = 0.6f;
    static constexpr float kMoveDelta = 0.05f;

    // orbitcam2: the LOOK style (ORBIT STYLE Look, the default; a right-stick camera as in Burnout Revenge): the stick's
    // direction is where the view looks (left / right the sides, down behind; up the game's view), the view moving
    // kLookEase of the remaining angle every 1/60 s (time-based), held kLookHold after the stick is let go, then eased
    // back the same way. Dead zone kLookDeadZone on the stick's length. No pitch from the stick (a mouse drag still
    // tilts). The SWING style (ORBIT STYLE Swing, orbitcam1's): the stick turns the view at kYawRate while held.
    enum Style : int { kLook = 0, kSwing = 1 };
    static constexpr float kLookEase = 0.10f;        // of the remaining angle per 1/60 s
    static constexpr float kLookHold = 0.4f;         // s: the view held after the stick is let go
    static constexpr float kLookDeadZone = 0.2f;     // the stick's length
    int style = kLook;
    bool engaged = false;           // look: the stick (or a drag) holds a target
    float hold = 0;                 // look: seconds since the stick was let go
    float target_yaw = 0, target_pitch = 0;

    float yaw = 0;     // the view's turn from the game's (rad, -pi..pi; positive: the view turned to the right)
    float pitch = 0;   // the eye's rise over the game's (rad; positive: up, looking down at the car)
    float idle = 0;    // seconds since the last input
    bool returning = false;
    float ret = 0;                    // the return's progress 0..1
    float from_yaw = 0, from_pitch = 0;   // where the return began
    float ref_x = 0, ref_y = 0, still = 0;   // the drift guard

    static float dead(float v);
    bool stick_counts(float dt, float x, float y);
    // One tick of dt seconds: the stick (-1..1, y down positive, dead zone not yet applied) and the drag since the last
    // tick (window fractions). Stick (or drag) right: the view turns right; down: the eye rises (as the garage camera)
    // Returns whether this tick's input counted.
    bool step(float dt, float stick_x, float stick_y, float drag_x, float drag_y);
    bool active() const { return yaw != 0.0f || pitch != 0.0f; }
    void reset() { const int s = style; *this = ChaseOrbit{}; style = s; }
    bool step_look(float dt, float stick_x, float stick_y, float drag_x, float drag_y);
};

// The race car a view chases: view 0 player 1's (kPlayerRaceCar), view 1 player 2's (the next player block)
constexpr std::uint32_t kPlayer2RaceCar = kPlayerRaceCar + 0x27D0u;
// Turn the drawn camera `c` by the look offsets `look` (yaw, pitch as ChaseOrbit's) about the car `car_id` of
// `vehicles`: the share of the game's chase camera (Camera::chase), the car ahead within kReach (else unchanged).
// True when the view was turned.
// orbitcam2: with `wall` (>= 0) the eye no further than that from a point kRayHeight above the car (the eased distance
// to the walls, OrbitCam::on_tick: Tick::race_wall; its place only, the view's direction kept), blended in by
// orbit_marker_away so the game's view comes back without a jump; with `front_zoom` the
// field of view narrows by up to kFrontZoomDeg as the eye goes round to the car's front (x -cos(yaw))
struct OrbitDraw {
    float wall = -1.0f;   // m from the ray's start (-1: no wall)
    bool front_zoom = false;
};
constexpr float kWallMargin = 0.4f;     // m
constexpr float kRayHeight = 1.0f;      // m above the car's origin
constexpr float kMinEyeDistance = 1.0f; // m: the eye no nearer the ray's start than this (a wall hugging the car)
constexpr float kFrontZoomDeg = 20.0f;  // degrees narrower, the eye in front of the car
bool apply_chase_orbit(Camera& c, const std::vector<Vehicle>& vehicles, std::uint32_t car_id, const float look[2], const OrbitDraw& draw = {});
// How far from `from` towards `eye` the eye may be: kWallMargin before the nearest wall of `rays` on the way (never
// nearer than kMinEyeDistance), the whole way when none; with `side` (a unit vector across the view) the rays to
// kWallMargin either side of the eye count too (a post or a sign's edge beside the line of sight)
float wall_distance(const TrackRays& rays, const float from[3], const float eye[3], const float side[3] = nullptr);
// The wall distance's easing (OrbitCam::on_tick, game time): a nearer wall at once (the eye never in it), a farther
// one over kWallOutSeconds (passing a row of posts does not pump the view in and out)
constexpr float kWallOutSeconds = 0.4f;
float ease_wall(float prev, float raw, float dt);
// How far the look offsets turn the view from the game's, for the HUD's markers the game projected with its own camera
// (Frame::orbit_away): 0 at the game's view, 1 from kOrbitMarkerFade rad on (either axis)
constexpr float kOrbitMarkerFade = 0.15f;
float orbit_marker_away(const float look[2]);
// How much of a lean the chase camera keeps while the view is turned by `away` (orbit_marker_away; camroll1's CAMERA
// ROLL: the roll fades out as the view turns from the game's, back as it returns)
inline float orbit_roll_keep(float away) { return 1.0f - (away < 0.0f ? 0.0f : away > 1.0f ? 1.0f : away); }

// camroll1 (CAMERA ROLL): the race chase camera leans into corners. Per tick, from the game's own camera of the view: the turn rate is the change of its at row
// projected on its right row over the game time since the last tick; the roll eases (kEase per second) towards
// -rate * gain degrees, at most max_deg; a jump of the at row over kCut (a cut) sets it to 0. The drawn camera's right
// and up rows are then turned about its at row by the roll (apply_chase_roll), times the tick's chase share
// (Camera::chase): the takedown, crash and other cameras are the game's, a blend between them takes it in step. Only
// the chase views with the player's car ahead within reach (as the orbit: not the bumper / in-car views). Drawn only:
// the game's camera, input and state are untouched.
struct ChaseRoll {
    static constexpr float kEase = 1.0f;     // per second (the roll's approach: a time constant of about a second)
    static constexpr float kCut = 0.1f;      // the at row's sideways change in one tick above this: a camera cut
    static constexpr float kMinDt = 0.001f;  // less game time than this (the pause): the roll held
    // CAMERA ROLL's levels (1 Subtle, 2 Strong): degrees of roll per rad/s of turn, and the most
    static constexpr float kGain[3] = {0.0f, 10.0f, 20.0f};
    static constexpr float kMaxDeg[3] = {0.0f, 4.0f, 8.0f};
    bool have = false;
    float prev_at[3] = {0.0f, 0.0f, 0.0f};
    float roll = 0.0f;   // degrees (positive: the right row turned towards the up row)
    // one tick: dt seconds of game time, the game camera's at and right rows
    void step(float dt, const float at[3], const float right[3], float gain, float max_deg);
    void reset() { *this = ChaseRoll{}; }
};
// How far a roll of `deg` degrees takes the view from the game's, for the HUD's markers the game projected with its own
// camera (Frame::orbit_away, as the orbit's): 0 level, 1 from kRollMarkerFade degrees on
constexpr float kRollMarkerFade = 1.0f;
inline float roll_marker_away(float deg) { const float a = deg < 0.0f ? -deg : deg; return a >= kRollMarkerFade ? 1.0f : a / kRollMarkerFade; }
// Turn the drawn camera `c` by `deg` degrees about its at row (right towards up)
void roll_camera(Camera& c, float deg);
// Whether the camera `c` frames the race car `car_id` as a chase camera does (its share > 0, the car ahead within reach)
bool chase_view(const Camera& c, const std::vector<Vehicle>& vehicles, std::uint32_t car_id);

class OrbitCam {
public:
    static constexpr int kPlayers = 2;
    static OrbitCam& instance();

    // PC SETTINGS > ORBIT CAMERA (default On); off: no input taken, the views the game's
    void set_enabled(bool on) { enabled_.store(on); }
    bool enabled() const { return enabled_.load(); }
    // camroll1: PC SETTINGS > CAMERA ROLL (0 Off, the default; 1 Subtle; 2 Strong); $B3_CAMERA_ROLL (0-2) wins (checks:
    // replay_check / baseline.sh with the roll on)
    void set_roll(int level);
    int roll() const { return roll_level_.load(); }
    // render thread, each frame: the PC SETTINGS page or photo mode has the input
    void set_blocked(bool on) { blocked_.store(on); }
    // game thread (b3r_pad_context via NativePad::set_driving): the game's driving context (the race runs)
    void set_driving(bool on) { driving_.store(on); }
    // ORBIT STYLE (ChaseOrbit::Style; default Look)
    void set_style(int s) { style_.store(s == ChaseOrbit::kSwing ? ChaseOrbit::kSwing : ChaseOrbit::kLook); }
    int style() const { return style_.load(); }
    // window thread: player p's pads' right stick (-1..1, y down positive; the larger deflection of the pads whose right
    // stick is free)
    void set_window_stick(int p, float x, float y);
    // the scripted test pad's right stick (player 1's; 0 0 when its layout binds the stick; B3_ORBITCAM_TEST_PLAYER=2,
    // checks only: player 2's, for a split-screen race's view 2)
    void set_test_stick(float x, float y);
    // the menu mouse (as its events come): a right-button drag by (dx, dy) window fractions
    void add_drag(float dx, float dy);
    // the input counts now: on, driving, not blocked (the menu mouse's right button then holds for a drag)
    bool takes_input() const { return enabled_.load() && driving_.load() && !blocked_.load(); }
    // orbitcam2: where the route's walls come from (render thread; the view's, null before a track); unset: no walls
    void set_rays_source(std::function<std::shared_ptr<const TrackRays>()> f) { rays_source_ = std::move(f); }
    // game thread (b3r_submit): the input as it is now applies from `poll` on
    void note_poll(std::uint32_t poll);
    // render thread (LiveFeed::push, every tick in order): the tick's offsets (Tick::race_orbit)
    void on_tick(Tick& t);

private:
    OrbitCam() = default;
    struct Input {
        std::uint32_t poll = 0;
        float x[kPlayers] = {}, y[kPlayers] = {};
        float dx = 0, dy = 0;
    };
    std::mutex mu_;
    std::atomic<bool> enabled_{true}, blocked_{false}, driving_{false};
    std::atomic<int> style_{ChaseOrbit::kLook};
    std::atomic<int> roll_level_{0};   // camroll1
    float win_x_[kPlayers] = {}, win_y_[kPlayers] = {}, test_x_ = 0, test_y_ = 0, drag_x_ = 0, drag_y_ = 0;   // (under mu_)
    Input last_;                 // the last stamped (under mu_)
    std::deque<Input> inputs_;   // stamped, not yet taken by the render thread (under mu_)
    // render thread
    ChaseOrbit orbit_[kPlayers];
    Input now_;
    std::uint32_t prev_vblank_ = 0;
    float prev_now_ = 0;          // the game's timer at prev_vblank_
    float field_hz_ = 59.94f;     // the VBlanks' rate (50: a PAL disc; from the game's timer)
    bool was_active_[kPlayers] = {};
    std::function<std::shared_ptr<const TrackRays>()> rays_source_;   // orbitcam2 (render thread)
    float wall_d_[kPlayers] = {-1.0f, -1.0f};                         // the eased wall distance (m; -1 none)
    bool warned_p2_ = false, seen_p2_ = false;   // the log's split-screen lines, once
    ChaseRoll roll_[kPlayers];   // camroll1 (render thread)
    float roll_now_[kPlayers] = {};   // the game's timer at the last roll step
    bool rolled_[kPlayers] = {};      // the log's line, once a race
};

}  // namespace b3replay

#endif
