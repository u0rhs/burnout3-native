// replaycams.h -- the replay viewer's cameras (replaycams, phase 2 of the replay viewer; 2026-11-22). Drawn only: while
// the game's replay plays, the view can come from angles of our own on the player's car or a rival's instead of the
// game's replay director (CB3PlayerCameraStateReplay 0x300BF0 [L]: trackside behaviours re-placed every 1-3 s by the
// car's speed, and on the race's events a cut to another state: a crash, a takedown). The guest is never written: the
// camera is the renderer's (finish_camera's frame on Tick::cam), and with the replay not playing, or the angle Game,
// nothing here changes the frame.
//
// The angles: on the player's car Game (the game's own: the default), Chase, Far chase, Bumper, Orbit, TV; on a rival's
// Chase, TV, Bumper (the game has no camera of its own on a rival).
// Chase and Far chase trail the car's level heading (eased, so a spin does not whip the view), Bumper rides the car's
// own frame, Orbit turns round the car, TV stands by the road ahead and watches the car go by (placed again once the car
// is past), zoomed so the car keeps its size.
#ifndef B3REPLAY_REPLAYCAMS_H
#define B3REPLAY_REPLAYCAMS_H

#include "capture.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace b3replay {

enum class ReplayAngle : std::uint8_t { kGame, kChase, kFarChase, kBumper, kOrbit, kTv };
// The angles offered (D-pad up / down cycles them): the player's car / a rival's
inline constexpr std::array<ReplayAngle, 6> kPlayerAngles = {ReplayAngle::kGame, ReplayAngle::kChase, ReplayAngle::kFarChase,
                                                             ReplayAngle::kBumper, ReplayAngle::kOrbit, ReplayAngle::kTv};
inline constexpr std::array<ReplayAngle, 3> kRivalAngles = {ReplayAngle::kChase, ReplayAngle::kTv, ReplayAngle::kBumper};
const char* replay_angle_name(ReplayAngle a);   // "GAME", "CHASE", "FAR CHASE", "BUMPER", "ORBIT", "TV"

// One watched car's view, stepped per tick with the car's frame (rows X, Y, Z, position) and the game time since the
// last tick (seconds). Pure: tests/test_replaycams.cpp.
struct ReplayView {
    static constexpr float kChaseBack = 6.5f, kChaseUp = 2.2f, kChaseLook = 3.0f;      // m
    static constexpr float kFarBack = 13.0f, kFarUp = 4.0f;
    static constexpr float kBumperAhead = 2.1f, kBumperUp = 0.75f;                    // the front of a car, at bonnet height
    static constexpr float kBumperMargin = 0.6f;                                       // kept from a wall at the nose
    static constexpr float kOrbitRadius = 7.5f, kOrbitUp = 2.4f, kOrbitRate = 0.35f;   // rad/s
    static constexpr float kTvAhead = 70.0f, kTvSide = 9.0f, kTvUp = 2.0f, kTvPast = 35.0f;
    static constexpr float kTvFill = 16.0f;                                            // the car keeps its size up to this far
    static constexpr float kHeadingRate = 5.0f;                                        // 1/s: the level heading's easing
    bool on = false;
    std::uint32_t car = 0;       // the watched car's id (the view starts again when it changes)
    ReplayAngle angle = ReplayAngle::kGame;
    float heading[3] = {0, 0, 1};    // the eased level heading
    float prev[3] = {};          // the car's position at the last step (the velocity)
    float vel[3] = {};
    float orbit = 0;             // Orbit's angle round the car (rad)
    bool tv_set = false;
    int tv_side = 1;             // TV: the side of the road it stands on (alternates)
    float tv[3] = {};            // TV: where it stands
    float eye[3] = {}, look[3] = {};
    float zoom = 1.0f;           // the view window's share of the game's
    int tv_hidden = 0;           // TV: ticks the car has been out of its sight
    static constexpr int kTvHiddenTicks = 20;   // ... then it is placed again
    void reset() { *this = ReplayView{}; }
    // `blocked` (may be empty): whether a wall stands on the segment a -> b, the fraction of the way in *t (the route's
    // drawn walls: trackrays.h). Chase and Far chase stop a margin before a wall; TV stands where it sees the car.
    using Blocked = std::function<bool(const float a[3], const float b[3], float* t)>;
    void step(ReplayAngle a, std::uint32_t car_id, float dt, const Mat34& car, const Blocked& blocked = {});
};

// Whether the game's replay plays this tick: replayview's Tick::replay == 2 (its host record 'HRV1'; tools/native/
// replay_view.h), or $B3_REPLAYCAMS_FORCE (diagnostics: the cameras in any race, for shots)
bool replay_on(const Tick& t);

class TrackRays;

class ReplayCams {
public:
    static ReplayCams& instance();
    // the route's walls (the orbit camera's source: View's track rays; render thread)
    void set_rays_source(std::function<std::shared_ptr<const TrackRays>()> f) { rays_source_ = std::move(f); }
    // told when the replay starts / stops playing (render thread; the live scene: photo mode's free camera allowed then)
    void set_on_playing(std::function<void(bool)> f) { on_playing_ = std::move(f); }
    // render thread (LiveFeed::push, every tick in order, after the finish camera): while the game's replay plays and the
    // angle is not Game, Tick::cam from the watched car's view
    void on_tick(Tick& t);
    // the actions (bindable; from the pad only during a replay): the angle -1 / +1, the watched car -1 / +1, from the
    // tick `poll` on (the game's poll the press came with: PhotoMode::game_poll() + 1, so a shot of a poll shows the
    // presses up to it however far behind the renderer is; 0: the next tick)
    void angle_step(int dir, std::uint32_t poll = 0);
    void car_step(int dir, std::uint32_t poll = 0);
    // REPLAY CAMERA CUTS: false (default) the game's crash / takedown cuts show as in the game; true the angle stays
    void set_stay(bool stay) { stay_.store(stay); }
    bool playing() const { return playing_.load(); }
    // the HUD line: the angle's name and the watched driver ("" while not playing)
    std::string label() const;
    // the HUD line's changes: +1 each time the replay starts or the angle or car changes; `poll`: the tick it changed at
    std::uint32_t label_serial(std::uint32_t* poll = nullptr) const;
    // offscreen shots (the feed runs ahead of the shot): the HUD line as it was at `poll` and the poll it changed at
    // ("" none: not playing then, or older than the history kept)
    std::string label_at(std::uint32_t poll, std::uint32_t* since) const;

private:
    std::mutex m_;
    ReplayView view_;
    int angle_i_[2] = {0, 0};        // per list (player / rival): the angle chosen
    struct Press { std::uint32_t poll; bool car; int dir; };
    std::vector<Press> pending_;     // the presses not yet due (render thread applies them by their poll)
    int car_i_ = 0;                  // the watched car's index among the race cars (0: the player's)
    std::string driver_;             // the watched car's name for the label
    std::uint32_t cut_ = 0;          // Tick::ReplayCamTick::cut
    std::uint32_t label_serial_ = 0, label_poll_ = 0;
    std::string label_key_;          // the last label (a change of it: a new serial)
    std::string label_text_;         // the HUD line as it was at that change
    struct LabelChange { std::uint32_t poll; std::string text; };
    std::vector<LabelChange> label_history_;   // the last kLabelHistory changes ("": the replay stopped)
    static constexpr std::size_t kLabelHistory = 64;
    bool forced_ = false;            // ($B3_REPLAYCAMS_FORCE's angle applied once)
    ReplayAngle last_angle_ = ReplayAngle::kGame;
    std::uint32_t last_car_ = 0;
    bool last_tv_set_ = false;
    float last_tv_[3] = {};
    std::atomic<bool> stay_{false}, playing_{false};
    float prev_now_ = 0;
    float half_w_ = 0, half_h_ = 0;  // the game's chase camera's view window (the last tick it was the chase camera)
    std::function<std::shared_ptr<const TrackRays>()> rays_source_;
    std::function<void(bool)> on_playing_;
};

}  // namespace b3replay

#endif
