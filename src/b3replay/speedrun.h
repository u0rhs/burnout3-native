// speedrun.h -- the speedrun timer (speedrun1; remaster-features §5 "Speedrun support", docs/pc-settings.md §7.3):
// an on-screen timer of the current event in GAME time, its lap splits, and the best time per event and car.
//
// Renderer only, read only. Every number is the game's own: CB3Score's race time (+0xC: the game timer minus the
// start time, 0 until the start signal, frozen at the finish), its lap times (+0x1C[8], the lap timer at each line),
// the lap timer (+0x2C8), the race state (+0x27C) and the finish state SetFinished gave (+0x8) -- capture.h ScoreState,
// read from the capture image each tick, never written. No wall clock anywhere: a paused game holds the timer because
// the game's timer holds. Times are printed as the game prints its own (the HUD's lap timer, func_00394660): the
// time rounded down to hundredths by func_00155280, then "%02d:%02d:%02d" of minutes, seconds, hundredths -- in the PS2's
// float arithmetic (the native game's: include/b3host/ps2float.h, add / sub / mul truncating), so the digits are the
// game's own.
//
// Tracker: fed every tick in order, it marks the event's attempts (a reset: the race state going back, the race
// time going back, leaving the event), the start signal, each lap's completion and the finish; a finish (state 1 or
// 4: a time) or a Time Attack lap gives a Result the caller keeps as the best time. Display: a pure function of the
// tick shown, the marks up to its poll and the best known at its poll -- so an offscreen shot of poll N shows N
// whatever later ticks have arrived.
//
// Best times: <prefs>/speedrun/<MODE>-<TRACK>-<CAR>-<LAPS>L.b3split (e.g. RACE-US_C1_V1-COMP_CAR1-3L.b3split).
// Format (little-endian), version 1: char[4] "B3SR", u32 version, u32 flags (0), u16 key bytes, the key text
// ("RACE US/C1_V1 COMP/CAR1.BGV 3"), f32 best event time (FLT_MAX none), u32 laps n (<= 64), f32 lap times[n] (the
// best event's splits), f32 best lap (FLT_MAX none), then u32 FNV-1a of every byte before it. Written to <path>.tmp,
// then renamed over <path>.
#ifndef B3REPLAY_SPEEDRUN_H
#define B3REPLAY_SPEEDRUN_H

#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "capture.h"

namespace b3replay::speedrun {

constexpr std::uint32_t kVersion = 1;
constexpr float kNone = FLT_MAX;   // the game's "no time" (func_00155A40 writes it for an out)

// The stage logics the timer runs in (their vtables [C config/symbol_addrs_b3names.txt __vt__*Logic]); crash modes and
// online logics have no timer.
enum class Mode : std::uint8_t { kNone, kRace, kTimeAttack, kEliminator, kRoadRage, kRace2p, kRoadRage2p };
Mode mode_of(std::uint32_t logic_vtable);
const char* mode_name(Mode m);   // "RACE", "TIMEATTACK", ...; "" for kNone

// func_00155280 (the game's rounding to hundredths) and the HUD lap timer's text (func_00394660): "mm:ss:cc"; the
// game's FLT_MAX prints 00:00:00.
float game_round(float t);
std::string time_text(float t);
// a difference ("+00:01:20" / "-00:00:35"), the same digits
std::string delta_text(float d);

struct Best {
    std::string key;                 // "RACE US/C1_V1 COMP/CAR1.BGV 3"
    float time = kNone;              // the best event time
    std::vector<float> splits;       // its lap times
    float best_lap = kNone;          // the best single lap
    bool operator==(const Best&) const = default;
};
std::vector<std::uint8_t> encode(const Best& b);
bool decode(const std::uint8_t* p, std::size_t n, Best& out, std::string* why = nullptr);
bool save(const std::string& path, const Best& b, std::string* why = nullptr);   // atomic (.tmp, then rename)
bool load(const std::string& path, Best& out, std::string* why = nullptr);
// The event's key and file name: "<MODE> <track> <car> <laps>", "<MODE>-<TRACK>-<CAR>-<laps>L.b3split"
std::string event_key(Mode m, const std::string& track, const std::string& car, int laps);
std::string file_name(Mode m, const std::string& track, const std::string& car, int laps);

// One tick for the tracker.
struct TickIn {
    std::uint32_t poll = 0;
    bool race = false;               // a race tick of a route (live: LiveScene::ready and the segment)
    ScoreState s;
    std::string track, car;          // "US/C1_V1", "COMP/CAR1.BGV" ("" unknown)
};

struct Mark {
    enum Kind : std::uint8_t { kReset, kStart, kLap, kFinish, kOut };
    std::uint32_t poll = 0;
    Kind kind = kReset;
    int lap = 0;                     // kLap: the lap completed (1..)
    float time = 0;                  // kLap: its time; kFinish: the event time
    // the best known when the mark was made (kNone: none): kReset / kStart / kFinish the event's best time, kLap the
    // best event's split of that lap; ref_lap the best single lap
    float ref = kNone, ref_lap = kNone;
};

// A time the caller may keep: an event finished (kind kFinish: time, splits) or a Time Attack lap (kLap: time).
struct Result {
    Mark::Kind kind = Mark::kFinish;
    Mode mode = Mode::kNone;
    std::string track, car;
    int laps = 0;
    float time = 0;
    std::vector<float> splits;
};

class Tracker {
public:
    // One tick, in order; `best`: the kept best of the tick's event (null: none). True with *out filled when the tick
    // gave a Result.
    bool step(const TickIn& in, const Best* best, Result* out);
    // The marks of the attempt shown at `poll`: from the last kReset at or before it, up to it.
    std::vector<Mark> marks_at(std::uint32_t poll) const;
    const std::vector<Mark>& marks() const { return marks_; }
    bool running() const { return phase_ == Phase::kRunning; }
    Mode mode() const { return mode_; }

private:
    enum class Phase { kIdle, kWaiting, kRunning, kDone };
    void mark(const Mark& m);
    Phase phase_ = Phase::kIdle;
    Mode mode_ = Mode::kNone;
    std::string track_, car_;
    int laps_total_ = 0;
    int last_state_ = -1, last_lap_ = -1, lap_count_ = 0;
    float last_time_ = 0;
    std::vector<float> splits_;
    std::vector<Mark> marks_;
};

// The text the panel shows for the tick `s` at `poll`: rows of (label, value, colour), the first row the main timer.
struct Row {
    std::string label, value;
    enum Colour : std::uint8_t { kWhite, kGrey, kGreen, kRed, kGold } colour = kWhite;
};
// setting 1: the event time (and the best), 2: also the lap splits. `marks`: Tracker::marks_at(the tick's poll).
std::vector<Row> rows(int setting, Mode mode, const ScoreState& s, const std::vector<Mark>& marks);

}  // namespace b3replay::speedrun

#endif
