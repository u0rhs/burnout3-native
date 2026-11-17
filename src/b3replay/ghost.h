// ghost.h -- Time Attack ghosts (ghost1; remaster-features §5 "Ghosts for Burning Laps"): the player's best lap per track
// and car, recorded from the ticks the renderer already gets and drawn as a see-through car on later runs.
//
// Renderer only. The recorder reads each tick's player race car (Vehicle::id == kPlayerRaceCar: its world matrices) and
// the game's own lap state (capture.h LapState: CB3Score's lap and the lap timer, read from the capture image, never
// written); a lap that ran from its start line to the next crossing in Time Attack (the stage logic
// CB3OfflineBurningLapLogic) is a lap. The best one per track + car goes to <pref>/ghosts/<track>-<car>.b3ghost
// (written atomically). Playback finds the best lap of the track (any car) and draws it at the pose of the current
// lap's time (the game's timer, between ticks blended): the same car model (loaded from the disc as any car the
// renderer draws), screen-door faded, no shadow, lights, sound, collision or AI knowledge -- the game never knows.
//
// The file holds our own recorded transforms only (no game data): the track and car as names, the lap time, and per
// sample the lap time and the car's main / body / wheel matrices.
//
// Format (little-endian), version 1:
//   char[4] "B3GH", u32 version, u32 flags (bit 0: a test lap, B3_GHOST_TEST_LAP), f32 lap time (s), u32 samples,
//   u8 wheels (0..6), u8 paint, u16 0, u16 track bytes, u16 car bytes, then the track ("US/C1_V1") and the car
//   ("COMP/CAR1.BGV", its folder and file under PVEH) as text; then per sample: f32 lap time, then 2 + wheels
//   matrices (main, body, wheel k) as f32 position[3] and i16 rotation rows[3][3] (x 1/16384), then f32 wheel speed;
//   then u32 FNV-1a of every byte before it.
#ifndef B3REPLAY_GHOST_H
#define B3REPLAY_GHOST_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "capture.h"

namespace b3replay::ghost {

constexpr std::uint32_t kVersion = 1;
constexpr int kMaxLaps = 8;                    // CB3Score +0x14 wraps to 0 after 8 laps [C IncrementLapCount]
constexpr std::size_t kMaxSamples = 60 * 60 * 15;   // a lap of up to 15 minutes at 60 Hz
constexpr float kFade = 0.45f;                 // the screen-door coverage the ghost is drawn with (1 = solid)

struct Sample {
    float t = 0;                 // the lap's time (s)
    Mat34 main, body;
    Mat34 wheel[6];
    float wheel_speed = 0;       // Vehicle::wheel_speed's largest |value| (the wheel draw list's choice)
};

struct Lap {
    std::string track;           // "US/C1_V1": the route's region and folder under TRACKS
    std::string car;             // "COMP/CAR1.BGV": the car file's folder and name under PVEH
    int paint = 0;
    int nwheel = 0;
    float time = 0;              // the lap time (the game's timer at the line)
    bool test = false;           // a test lap (B3_GHOST_TEST_LAP: the lap's first N seconds; documented in docs/pc-settings.md)
    std::vector<Sample> samples; // lap time increasing, the first at 0
};

std::vector<std::uint8_t> encode(const Lap& lap);
bool decode(const std::uint8_t* p, std::size_t n, Lap& out, std::string* why = nullptr);
// atomic: written to <path>.tmp, then renamed over <path>
bool save(const std::string& path, const Lap& lap, std::string* why = nullptr);
bool load(const std::string& path, Lap& out, std::string* why = nullptr);

// "TRACKS/US/C1_V1" or any path ending in the region and the route folder -> "US/C1_V1"; a car file path -> "COMP/CAR1.BGV"
std::string last_two(const std::string& path);
// <track slug>-<car slug>.b3ghost ("US_C1_V1-COMP_CAR1.b3ghost"): letters, digits and '_' only
std::string file_name(const std::string& track, const std::string& car);
std::string slug(const std::string& s, bool drop_extension);
// The best (shortest) valid lap among <dir>/<track slug>-*.b3ghost; false if none
bool best_for_track(const std::string& dir, const std::string& track, Lap& out, std::string* path = nullptr);

// Time sync: the samples around lap time t (samples[i] .. samples[i + 1], blend u in [0, 1]). False when the lap has no
// samples or t is outside [0, the last sample's time].
struct At { std::size_t i = 0; float u = 0; };
bool locate(const Lap& lap, float t, At& at);

// The recorder: fed every tick in order. A lap is the samples from a lap's first tick (its index changed from the one
// before to the next one, or from -1 to 0 at the start) to the tick its index moves on to the next; a restart (index
// back to -1), leaving Time Attack, a timer going backwards or a gap of more than 2 s drops the lap in progress.
struct TickInput {
    bool time_attack = false;    // LapState valid and time_attack
    int index = -1;              // LapState::index
    float time = 0;              // LapState::time
    bool have_car = false;       // the player's car is in the tick (and its car file is known)
    Sample pose;                 // its matrices (t ignored)
    int paint = 0, nwheel = 0;
    std::string car;             // its car file ("COMP/CAR1.BGV")
};
class Recorder {
public:
    // test_seconds > 0 (B3_GHOST_TEST_LAP): a lap also ends when its time reaches that many seconds (a test lap)
    explicit Recorder(float test_seconds = 0) : test_(test_seconds) {}
    // true when a lap was completed at this tick (*done = it; the track is the caller's to fill in)
    bool step(const TickInput& in, Lap* done);
    void reset();
    bool recording() const { return rec_; }
    float test_seconds() const { return test_; }

private:
    float test_;
    int index_ = -2;             // -2: no tick seen yet (a lap already running is not recorded: its start was missed)
    bool rec_ = false;
    float last_t_ = 0;
    Lap cur_;
};

}  // namespace b3replay::ghost

#endif
