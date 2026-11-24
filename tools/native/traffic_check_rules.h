// traffic_check_rules.h -- trafficcheck (2026-11-21): the rules of PC SETTINGS > GAME > GAMEPLAY's TRAFFIC CHECKING (the
// hooks: traffic_check.h). Plain values only, so the tests can hold them.
//
// Which races [C RequestOnePlayerGameMode 0x30C2C0, RequestTwoPlayerGameMode 0x30BFF0]: the game's current mode
// (*0x0051BA88) is a stage record; SINGLE EVENT's is 0x0051AD00 with +0x1B8 = 0x0051B920 (World Tour's carries
// 0x0051B928, Crash party's 0x0051B988), the two-player game's 0x0051AEC0 (+0x1B8 0x0051B920 too). The option applies to
// those two only (as gameopts' MIRROR MODE): World Tour, Crash party and the online game stay the game's. Crash mode
// (CB3Game::IsCrashMode) is never changed: its junctions are the game's own scoring.
//
// The option is taken when such a race becomes the current mode and kept until it is no longer (a restart keeps it; a
// change on the PC SETTINGS page applies from the next race). A race with it keeps its results apart from the records.
//
// Three values (TcMode): Off, Revenge and Extreme. REVENGE plays as Burnout Revenge does, with B3's equivalents:
//   - a human player's car (body type 2, not crashing, not AI-driven) at 75 mph or more whose hit on a traffic vehicle
//     the game itself makes a crash (VehicleTrafficResponse's fatal path: a slow bump stays the game's shunt);
//   - the traffic goes the player's way: its heading within 70 degrees of the car's;
//   - not a big rig, a bus or a trailer (categories 4 / 5 / 11) nor one of B3's special vehicles (6..10): cars, vans and
//     lorries are checked.
// EXTREME: any traffic the player's fatal hit is on is checked, at any speed: oncoming, crossing and heavy vehicles, a hit
// the game makes fatal because the car touches a wall (VehicleTrafficResponse's pinned rule, at any closing speed), and a
// wreck (crashed traffic: BodyBodyResponse's fatal touch) that is not another player's checked car now: the touch spared,
// the wreck the player's checked car, a check once per wreck.
// Both: a checker's own checked car never crashes it, past kOwnedSeconds too (a touch makes it the checker's again, no
// points: Revenge re-checks its checked traffic), nor does its chain: a car the checked car (or a car of its chain) wrecks or
// knocks is the checker's too (a checked car shoved into the car ahead and bouncing back, either of them). The chain's cars
// only protect: no takedowns, no shots of their own.
// Both: a hit the game only shunts (not fatal) is a check too from 75 mph when the rules allow one (Revenge: going your
// way, not heavy; Extreme: any): the traffic crashed and pushed as a fatal hit's.
// Why there is no speed floor in Extreme: the game's own fatal test is already one -- the closing speed along the contact's
// normal above 75 mph (krRaceCarCollisionHeight) times the car's +0x1534 (1, 0.5 in some modes, 0.25 in a Burnout's boost,
// less just after a slam); a car pinned against a wall crashes at any speed. So a fatal hit below 75 mph of the car's own
// speed is an oncoming / crossing car, a boost, or a wall: Revenge (its mrCheckingTrafficMinSpeed, 75 mph) crashes there.
// A checked car is the checker's for kOwnedSeconds: touching it again never crashes the checker, and a rival (any other
// race car, the other player too) that touches it crashes -- unless just reset (CB3RaceCarPhysics +0x152C >= 0, the
// game's own grace) -- and the takedown is credited to the checker the game's way (its takedown table).
// Scoring: TRAFFIC CHECK 50 and 30 boost (half B3's near miss of 60); a checked car that wrecks
// an oncoming car TRICK SHOT (50, 40 boost), a truck or bus SKILL SHOT (100, 60), an oncoming truck or bus EXTREME SHOT
// (150, 80), the same shot again only after 2.5 s. The chain's star levels are B3's near-miss levels: GOOD / GREAT /
// EXTREME CHECKING pop up as it reaches them; it ends kChainSeconds (5, as B3's near-miss chain) after its last
// event, or 1 s after EXTREME CHECKING, and the sign shows its total.
#ifndef B3NATIVE_TRAFFIC_CHECK_RULES_H
#define B3NATIVE_TRAFFIC_CHECK_RULES_H

#include <cstdint>
#include <cstdlib>
#include <string>

namespace b3native {

// The setting / the latch / the replay's entry: 0 off, 1 Revenge, 2 Extreme
enum TcMode : unsigned { kTcOff = 0u, kTcRevenge = 1u, kTcExtreme = 2u };
inline unsigned traffic_check_normal(unsigned m) { return m <= kTcExtreme ? m : kTcOff; }
inline const char* traffic_check_name(unsigned m) { return m == kTcRevenge ? "Revenge" : m == kTcExtreme ? "Extreme" : "off"; }
// The race's records word (src/b3replay/records.h options_folder; every owner: tests/test_bit_owners.cpp): bits 26-27 the
// mode (1 Revenge, 2 Extreme), with the flag (and TRAFFIC Normal) when no other option set the word: its records apart
constexpr std::uint64_t kTcRecordMask = 0x3ull << 26;
inline std::uint64_t traffic_check_record_word(std::uint64_t w, unsigned mode) {
    if (mode != kTcRevenge && mode != kTcExtreme) return w;
    if (!(w & 0x8000u)) w = 0x8002u;   // (the flag, TRAFFIC Normal: as raceopts' own word)
    return (w & ~kTcRecordMask) | std::uint64_t(mode) << 26;
}

constexpr std::uint32_t kTcCurrentMode = 0x0051BA88u;       // [C] D_0051BA88: CB3Game's current mode
constexpr std::uint32_t kTcOnePlayerStage = 0x0051AD00u;    // [C] the one-player stage record (Single Event, World Tour)
constexpr std::uint32_t kTcTwoPlayerStage = 0x0051AEC0u;    // [C] the two-player stage record
constexpr std::uint32_t kTcSingleEventTag = 0x0051B920u;    // [C] +0x1B8 of a Single Event / two-player stage

// The current mode (`current`, masked to the EE's 32 MB) and its +0x1B8 (`tag`) are a race the option applies to
inline bool traffic_check_stage(std::uint32_t current, std::uint32_t tag) {
    return (current == kTcOnePlayerStage || current == kTcTwoPlayerStage) && tag == kTcSingleEventTag;
}

// The check itself
constexpr float kTcMinSpeed = 33.528f;       // m/s (the car's +0xBC): 75 mph
constexpr float kTcSameWay = 0.34202f;        // cos 70 degrees: the largest contact angle
constexpr float kOwnedSeconds = 10.0f;        // a checked car is its checker's this long (the score's clock)
constexpr float kChainSeconds = 5.0f;         // the chain ends this long after its last event (as B3's near miss)
constexpr float kExtremeHold = 1.0f;          // ... or this long after EXTREME CHECKING
constexpr float kShotGrace = 2.5f;            // the same shot pops again only this long after the last
// [C] ETrafficVehicleCategories (the Revenge prototype's stabs; B3's CB3TrafficVehicle +0x173, a wreck's +0x2420)
inline bool traffic_heavy_category(int c) { return c == 4 || c == 5 || c == 11; }   // big rig, bus, trailer
inline bool traffic_checkable_category(int c) { return c >= 1 && c <= 3; }          // car, van, lorry
// Whether a fatal hit on traffic of `category` heading (tr_x, tr_z) by a car heading (car_x, car_z) is a check under `mode`
inline bool traffic_checkable(unsigned mode, int category, float car_x, float car_z, float tr_x, float tr_z);
// Whether a fatal hit at the car's `speed` (m/s, its +0xBC) may be a check under `mode`: Revenge from 75 mph, Extreme any
inline bool traffic_check_speed_ok(unsigned mode, float speed) {
    return mode == kTcExtreme || (mode == kTcRevenge && speed >= kTcMinSpeed);
}
// Whether a hit the game only shunts (VehicleTrafficResponse's non-fatal response) on traffic the rules let be checked is a
// check: from 75 mph in both modes (Revenge checks the traffic going its way from its mrCheckingTrafficMinSpeed whatever
// the impact; B3 makes a hit fatal only above 75 mph of closing speed, so most hits on traffic going your way are shunts)
inline bool traffic_check_shunt_ok(unsigned mode, float speed) {
    return (mode == kTcRevenge || mode == kTcExtreme) && speed >= kTcMinSpeed;
}
// A fatal touch of a wreck (crashed traffic) by a human player's car able to check: the game's (its crash, or a checked
// car of another player's taking it out), kOwn (its own checked car, of any age: spared, the checker's again, no points)
// or kCheck (Extreme, a wreck not another player's checked car now: spared, the player's checked car, a check)
enum class TcWreckTouch : std::uint8_t { kGame, kOwn, kCheck };
inline TcWreckTouch traffic_wreck_touch(unsigned mode, bool checked_by_this_car, bool checked_by_another_now) {
    if (mode != kTcRevenge && mode != kTcExtreme) return TcWreckTouch::kGame;
    if (checked_by_this_car) return TcWreckTouch::kOwn;
    if (mode == kTcExtreme && !checked_by_another_now) return TcWreckTouch::kCheck;
    return TcWreckTouch::kGame;
}
// The traffic's heading (unit, the ground plane) against the car's forward (unit): the same way
inline bool traffic_same_way(float car_x, float car_z, float tr_x, float tr_z) { return car_x * tr_x + car_z * tr_z >= kTcSameWay; }
// The wrecked traffic came the other way (its heading against the checker's forward)
inline bool traffic_oncoming(float car_x, float car_z, float tr_x, float tr_z) { return car_x * tr_x + car_z * tr_z <= -kTcSameWay; }

inline bool traffic_checkable(unsigned mode, int category, float car_x, float car_z, float tr_x, float tr_z) {
    if (mode == kTcExtreme) return true;
    if (mode != kTcRevenge) return false;
    return traffic_checkable_category(category) && traffic_same_way(car_x, car_z, tr_x, tr_z);
}

// The launch of a checked car (ground plane): its velocity becomes kLaunch times the checker's plus kSide
// along the contact's normal (from the checker to it, so the two part); the checker gives up a little of its closing speed
constexpr float kLaunch = 1.5f, kSide = 5.0f;              // -, m/s
constexpr float kRecoilGain = 0.1f, kRecoilMax = 3.0f;     // m/s
constexpr float kSpinGain = 0.25f;                         // of the launch's moment about the checked car
inline float traffic_recoil_speed(float closing) {
    const float s = kRecoilGain * (closing > 0.0f ? closing : 0.0f);
    return s > kRecoilMax ? kRecoilMax : s;
}

// The chain's events: their points and boost
enum class TcEvent : std::uint8_t { kCheck, kTrickShot, kSkillShot, kExtremeShot };
inline int traffic_event_points(TcEvent e) {
    switch (e) {
        case TcEvent::kCheck: return 50;
        case TcEvent::kTrickShot: return 50;
        case TcEvent::kSkillShot: return 100;
        case TcEvent::kExtremeShot: return 150;
    }
    return 0;
}
// the boost: a check half a near miss (B3's gmrNearMissBurnValue 60: 30), the shots 40 / 60 / 80
inline float traffic_event_boost(TcEvent e, float near_miss) {
    switch (e) {
        case TcEvent::kCheck: return 0.5f * near_miss;
        case TcEvent::kTrickShot: return 40.0f;
        case TcEvent::kSkillShot: return 60.0f;
        case TcEvent::kExtremeShot: return 80.0f;
    }
    return 0.0f;
}
inline const char* traffic_event_name(TcEvent e) {
    switch (e) {
        case TcEvent::kCheck: return "TRAFFIC CHECK";
        case TcEvent::kTrickShot: return "TRICK SHOT";
        case TcEvent::kSkillShot: return "SKILL SHOT";
        case TcEvent::kExtremeShot: return "EXTREME SHOT";
    }
    return "?";
}
// What a checked car's wreck of another traffic vehicle scores: an oncoming truck or bus EXTREME SHOT, a truck or bus
// SKILL SHOT, an oncoming car TRICK SHOT; a car going the same way nothing. False: no shot.
inline bool traffic_wreck_event(int category, bool oncoming, TcEvent& out) {
    const bool heavy = traffic_heavy_category(category);
    if (heavy) out = oncoming ? TcEvent::kExtremeShot : TcEvent::kSkillShot;
    else if (oncoming) out = TcEvent::kTrickShot;
    else return false;
    return true;
}

// The signs (CB3HUDPrimaryMessage, PERFECT LAP's): the chain's total, its levels, the shots
enum class TcSign : std::uint8_t { kTotal, kGood, kGreat, kExtremeChecking, kTrickShot, kSkillShot, kExtremeShot };
inline TcSign traffic_shot_sign(TcEvent e) {
    return e == TcEvent::kTrickShot ? TcSign::kTrickShot : e == TcEvent::kSkillShot ? TcSign::kSkillShot : TcSign::kExtremeShot;
}
// The level popup when the chain's star level goes from `was` to `now` (levels 1, 2, 3: GOOD / GREAT / EXTREME CHECKING);
// false: none
inline bool traffic_level_sign(int was, int now, TcSign& out) {
    if (now <= was || now < 1) return false;
    out = now == 1 ? TcSign::kGood : now == 2 ? TcSign::kGreat : TcSign::kExtremeChecking;
    return true;
}

// One player's chain: its events, points, star level and the time of the last event (the score's clock, seconds), and
// when each shot last popped
struct TcChain {
    int events = 0, points = 0, level = -1;
    float last = 0.0f, extreme_at = -1.0f;
    float shot_at[3] = {-100.0f, -100.0f, -100.0f};
    bool active() const { return events > 0; }
    // An event at `now`. A clock that went back (a restart) starts afresh.
    void add(TcEvent e, float now) {
        if (events > 0 && now < last) *this = TcChain{};
        ++events;
        points += traffic_event_points(e);
        last = now;
    }
    // A shot of this kind may pop at `now` (kShotGrace since the last one); the call takes it
    bool shot_ready(TcEvent e, float now) {
        if (e == TcEvent::kCheck) return true;
        float& t = shot_at[unsigned(e) - 1u];
        if (now - t < kShotGrace && now >= t) return false;
        t = now;
        return true;
    }
    // Whether the chain is over at `now` (kChainSeconds since its last event, kExtremeHold after EXTREME CHECKING, or the
    // clock went back)
    bool over(float now) const {
        if (events == 0) return false;
        if (now < last) return true;
        if (extreme_at >= 0.0f && now - extreme_at > kExtremeHold) return true;
        return now - last > kChainSeconds;
    }
};

// The star level of `events` against the thresholds (the game's near-miss levels: the highest i with t[i] <= events;
// -1 below the first), as CB3Score's near-miss chain sets its category's +0x11
inline int traffic_star_level(float events, const float* thresholds, int n) {
    for (int i = n - 1; i >= 0; --i)
        if (!(events < thresholds[i])) return i;
    return -1;
}

// A replay's "traffic_check" entry ("1" Revenge, "2" Extreme; anything else off)
inline unsigned traffic_check_entry(const char* v) {
    if (!v || !v[0] || v[1]) return kTcOff;
    return v[0] == '1' ? kTcRevenge : v[0] == '2' ? kTcExtreme : kTcOff;
}

// The latch: the option asked for when a race of those stages became the current mode, until it is not
class TrafficCheckLatch {
public:
    // `stage`: the current mode is such a race now; `wanted`: the mode asked for now (TcMode). True when the latch changed.
    bool step(bool stage, unsigned wanted) {
        const unsigned before = mode_;
        if (!stage) mode_ = kTcOff;
        else if (!in_stage_) mode_ = traffic_check_normal(wanted);
        in_stage_ = stage;
        return mode_ != before;
    }
    bool latched() const { return mode_ != kTcOff; }
    unsigned mode() const { return mode_; }
    bool in_stage() const { return in_stage_; }

private:
    bool in_stage_ = false;
    unsigned mode_ = kTcOff;
};

// The caption and the signs in the game's text language (platform_disc.h language_text(): "" the disc's English, else the
// imported language's code; race_choice.h race_words' codes). UTF-8, upper case as the game's captions; each sign at
// most 24 characters (its slot).
struct TcWords { const char* code; const char* caption; const char* sign[7]; };   // sign: TcSign order
inline const TcWords& traffic_check_words(const std::string& code) {
    static const TcWords kWords[] = {
        {"", "TRAFFIC CHECK", {"TRAFFIC CHECK!", "GOOD CHECKING!", "GREAT CHECKING!", "EXTREME CHECKING!", "TRICK SHOT!", "SKILL SHOT!", "EXTREME SHOT!"}},
        {"FR", "TRAFIC BOUSCUL\xC3\x89", {"TRAFIC BOUSCUL\xC3\x89 !", "BONNE BOUSCULADE !", "SUPER BOUSCULADE !", "BOUSCULADE EXTR\xC3\x8AME !",
                                         "COUP RUS\xC3\x89 !", "COUP DE MA\xC3\x8ETRE !", "COUP EXTR\xC3\x8AME !"}},
        {"GE", "VERKEHR GERAMMT", {"VERKEHR GERAMMT!", "GUT GERAMMT!", "STARK GERAMMT!", "EXTREM GERAMMT!", "TRICKSCHUSS!", "MEISTERSCHUSS!", "EXTREMSCHUSS!"}},
        {"IT", "TRAFFICO SPINTO", {"TRAFFICO SPINTO!", "BELLA SPINTA!", "GRANDE SPINTA!", "SPINTA ESTREMA!", "COLPO FURBO!", "COLPO DA MAESTRO!", "COLPO ESTREMO!"}},
        {"SP", "TR\xC3\x81" "FICO EMBESTIDO", {"\xC2\xA1TR\xC3\x81" "FICO EMBESTIDO!", "\xC2\xA1" "BUEN EMBISTE!", "\xC2\xA1GRAN EMBISTE!",
                                              "\xC2\xA1" "EMBISTE EXTREMO!", "\xC2\xA1TIRO CON TRUCO!", "\xC2\xA1TIRO MAESTRO!", "\xC2\xA1TIRO EXTREMO!"}},
        {"DU", "VERKEER GERAMD", {"VERKEER GERAMD!", "GOED GERAMD!", "GEWELDIG GERAMD!", "EXTREEM GERAMD!", "TRUCSCHOT!", "MEESTERSCHOT!", "EXTREEM SCHOT!"}},
        {"SW", "TRAFIK KNUFFAD", {"TRAFIK KNUFFAD!", "BRA KNUFF!", "GRYM KNUFF!", "EXTREM KNUFF!", "TRICKSKOTT!", "M\xC3\x84STARSKOTT!", "EXTREMSKOTT!"}},
    };
    for (const TcWords& w : kWords)
        if (code == w.code) return w;
    return kWords[0];
}

}  // namespace b3native

#endif
