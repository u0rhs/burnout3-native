// finishcam.h -- FINISH CAMERA (finishcam1, 2026-11-21): when the player crosses the finish line the race view cycles
// through 12 "set pieces" of camera angles round the finish line, 5 s each, instead of the game's one trackside shot
// (remaster-features §27).
//
// The shots' camera offsets: the disc's tuning file DATA/VDB.XML has 36 vector values under keys no retail code
// registers; kShotKeys lists those keys (our list, found by matching the file's unregistered vectors), grouped in 12 set
// pieces of 2-4 shots (kSetPieceShots). They are read at run time from the player's own disc; nothing of the disc is in
// the repo. The rest of a shot is ours: the shots of a set piece share its 5 s evenly; each is a still camera set by
// its offset from where the car crossed the line, aiming at the car while it is near the line, then at the line
// (FinishView).
//
// When: view 0's player camera manager is in the state the game enters at the finish (Tick::cam_state ==
// kFinishCameraState, its trackside "tripod" shot) and the race's score has finished (Tick::score.finish 1 or 4; not an
// out, 2 / 3, as a World Tour ELIMINATOR's elimination; not a Road Rage's end), the
// race view (not a car page), one view (not the split screen), and the player's car in the tick. From the first such
// tick (the finish) the set pieces run in the game's time (its timer: the pause holds them); any other camera state ends
// it, so the game's replays, crash and takedown cameras are the game's.
//
// Drawn only: the game's camera, its finish state, the results and their timing are untouched; the renderer draws the
// frame from this camera instead (Tick::finish_cam, interpolated between ticks within a shot, cut between shots).
// Settings: PC SETTINGS > DISPLAY > CAMERA > FINISH CAMERA (b3settings finish_camera: 0 Game, the default; 1 Set
// pieces); $B3_FINISH_CAMERA (0 / 1) wins (checks). Without the keys in VDB.XML (another disc) it stays the game's.
#ifndef B3REPLAY_FINISHCAM_H
#define B3REPLAY_FINISHCAM_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "capture.h"

namespace b3replay {

struct FinishShots {
    static constexpr int kSetPieces = 12;
    static constexpr int kShots = 36;
    static constexpr float kSetPieceSeconds = 5.0f;
    static const std::uint32_t kShotKeys[kShots];         // the VDB.XML keys of the shots' camera offsets
    static const std::uint8_t kSetPieceShots[kSetPieces];  // shots per set piece (sum kShots)
    float offset[kShots][3] = {};   // read from VDB.XML: x across the car, y up, z along its heading (metres)
    int found = 0;                  // how many of kShotKeys the file has (kShots: usable)
    bool ok() const { return found == kShots; }
    // Read the offsets from VDB.XML's bytes (the tuning file's value records, docs/modding/file-formats/vdb.md)
    bool parse(const std::vector<std::uint8_t>& vdb);
    // The shot at `t` seconds into the sequence: the set piece (0-11, round again after the 12th), the shot (0-35) and
    // the seconds into that shot
    static void at(float t, int& set_piece, int& shot, float& into);
};

// One player's finish camera: stepped per tick with the car's frame and the game time since the last tick. Every shot
// is a camera set by its offset from where (and which way) the car crossed the line, in that frame levelled, and still
// for the shot: the PS2 keeps the track streamed round the game's own finish shot, so the set pieces stay there. It aims
// at the player's car while the car is within kFollow of the line, then eases back to the line (where the rivals come
// through), and zooms in past kZoomFrom.
struct FinishView {
    static constexpr float kAim = 0.6f;          // the aim this far above the car's origin / the line (m)
    static constexpr float kFollow = 100.0f;     // the car followed while this near the line (m, level)
    static constexpr float kAimRate = 4.0f;      // the aim eases towards its point at this rate (1/s)
    static constexpr float kZoomFrom = 12.0f;    // the view zooms in past this distance (m) ...
    static constexpr float kMinZoom = 0.3f;      // ... to at most this share of the game's view width
    bool on = false;
    float t = 0;            // seconds since the finish (game time)
    int shot = -1;          // the shot drawn (cuts when it changes)
    float eye[3] = {}, look[3] = {};
    float zoom = 1.0f;      // the view window's share of the game's
    float start[3] = {};    // where the car crossed the line
    float ax[3] = {1, 0, 0}, az[3] = {0, 0, 1};   // across and along its heading then, level
    float roam = 0;         // how far the car is from the line now (m, level)
    // camfixes (2026-11-22): a point-to-point race (the line far from the start): the cars do not come round again, so
    // each shot follows the car instead of watching the line: its offset in the car's level frame as the shot begins
    // (no swing with the steering), the eye travelling with the car, aimed at it
    bool follow = false;
    void reset() { *this = FinishView{}; }
    // One tick: dt seconds of game time, the car's frame (rows X, Y, Z, position)
    void step(const FinishShots& shots, float dt, const Mat34& car);
    // camfixes: a race is point-to-point when its finish is this far (level) from where the player's car waited for
    // the start (a circuit's grid is within a few car lengths of its line)
    static constexpr float kPointToPoint = 300.0f;
    // camfixes: on a point-to-point track the route ends soon after its line (the world modelled beyond it is a few
    // hundred metres at most: the US/P2_V2 shots past ~250 m showed the void): the set pieces follow the car this far
    // from the line, then the game's own finish shot takes over for the rest of the finish
    static constexpr float kFollowEnd = 200.0f;
};

// The drawn camera `c` (the game's finish shot) from a FinishView's eye, aim and zoom
void finish_camera(Camera& c, const float eye[3], const float look[3], float zoom);

class FinishCam {
public:
    static FinishCam& instance();
    // PC SETTINGS > FINISH CAMERA (0 Game, 1 Set pieces); $B3_FINISH_CAMERA wins
    void set_level(int level);
    int level() const { return level_.load(); }
    // The disc's DATA/VDB.XML (the disc folder or image `game`), once
    void load(const std::string& game);
    void load_vdb(const std::vector<std::uint8_t>& vdb);   // (load's parse; tests: a made-up file)
    bool ready() const { return shots_.ok(); }
    // render thread (LiveFeed::push, every tick in order): Tick::finish_cam
    void on_tick(Tick& t);

private:
    FinishCam() = default;
    std::atomic<int> level_{0};
    bool loaded_ = false;
    FinishShots shots_;
    FinishView view_;
    float prev_now_ = 0;
    bool have_start_ = false;   // camfixes: the player's car at the countdown (the point-to-point test)
    bool p2p_done_ = false;     // camfixes: a point-to-point finish handed back to the game's shot (kFollowEnd)
    float start_pos_[3] = {};
    int logged_shot_ = -1;
};

}  // namespace b3replay

#endif
