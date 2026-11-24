// game_option_rules.h -- gameopts (2026-11-21): the rules of PC SETTINGS > GAME > GAMEPLAY's two options, MIRROR MODE and
// CRASHBREAKER IN EVERY MODE (the hooks: game_options.h). Plain values only, so the tests can hold them.
//
// Which races [C RequestOnePlayerGameMode 0x30C2C0, RequestTwoPlayerGameMode 0x30BFF0, RequestCrashPartyGameMode
// 0x30BE20]: the game's current mode (*0x0051BA88) is a stage record. SINGLE EVENT's is 0x0051AD00 with +0x1B8 =
// 0x0051B920 (World Tour's carries 0x0051B928, Crash party's 0x0051B988); the two-player game's is 0x0051AEC0 (+0x1B8
// 0x0051B920 too). The options apply to those two only: World Tour, Crash party and the online game stay the game's
// (a World Tour event played mirrored or with the breaker could not count for the tour, and it must).
//
// The options are taken when such a race becomes the current mode and kept until it is no longer (a restart keeps them;
// a change on the PC SETTINGS page applies from the next race). The record word (records kept apart) is the options that
// change that race: MIRROR MODE in any of its modes, the Crashbreaker outside Crash mode (Crash mode has its own), and any
// of the PARTY MODES but TUK-TUKS (party_modes.h: GAME > PARTY MODES) in any mode (a TUK-TUKS race is a traffic
// vehicle's: trafficseries keeps it apart with its own bit 28).
#ifndef B3NATIVE_GAME_OPTION_RULES_H
#define B3NATIVE_GAME_OPTION_RULES_H

#include <cstdint>
#include <cstdlib>
#include <string>

namespace b3native {

enum : unsigned { kGameOptMirror = 1u, kGameOptCrashbreaker = 2u, kGameOptAll = 3u, kGameOptParty = 4u /* record bits only */ };
// The options word (the setting's, the latch's, the replay's "game_options"): bits 0..1 the two options, bits 2..3
// CRASHBREAKER AFTER's vehicle count (0..3: the vehicles the player's crash must hit before the breaker is ready), only
// with the Crashbreaker on (else 0, so a word with neither option is 0)
// party1: bits 4..11 PARTY MODES (party_modes.h): 4..5 CRAZY AI PHYSICS (0 off, 1 Basic, 2 Local Wildlife, 3 Mayhem),
// 6 TUK-TUKS, 7..8 MOON JUMP (0 off, 1 Low, 2 Medium, 3 High), 9 CRASH ON DEMAND, 10 AI DRIVER BUTTON, 11 KAMIKAZE TRAFFIC
// gamemods1: bit 12 the Crashbreaker's Full blast, only with the Crashbreaker (CRASHBREAKER IN EVERY MODE: Full -- the
// rivals and the other player caught too, game_options.h blast)
constexpr unsigned kGameOptCarsShift = 2u, kGameOptCrashFull = 0x1000u, kGameOptWord = 0x1FFFu;
constexpr unsigned kPartyCrazyShift = 4u, kPartyTuk = 1u << 6, kPartyMoonShift = 7u, kPartyCrash = 1u << 9,
                   kPartyAiDriver = 1u << 10, kPartyKamikaze = 1u << 11, kPartyMask = 0xFF0u;
inline unsigned game_options_normal(unsigned w) {
    w &= kGameOptWord;
    return (w & kGameOptCrashbreaker) ? w : w & ~(3u << kGameOptCarsShift | kGameOptCrashFull);
}
// gamemods1: true while CRASHBREAKER IN EVERY MODE: Full's blast crashes a race car (its own StartRaceCarCrashing call,
// pair 0, info 0: game_options.h blast); other wrappers on the crash start (party_modes.h: a flip) leave that crash alone
inline bool& game_options_full_blast_flag() { static bool b = false; return b; }
inline bool game_options_full_blast() { return game_options_full_blast_flag(); }
inline unsigned party_crazy(unsigned w) { return w >> kPartyCrazyShift & 3u; }
inline unsigned party_moon(unsigned w) { return w >> kPartyMoonShift & 3u; }
// The PARTY MODES' part of the word from the settings' values
inline unsigned party_word(int crazy, bool tuk, int moon, bool crash, bool ai_driver, bool kamikaze) {
    const auto c3 = [](int v) { return unsigned(v < 0 ? 0 : v > 3 ? 3 : v); };
    return c3(crazy) << kPartyCrazyShift | (tuk ? kPartyTuk : 0u) | c3(moon) << kPartyMoonShift | (crash ? kPartyCrash : 0u) |
           (ai_driver ? kPartyAiDriver : 0u) | (kamikaze ? kPartyKamikaze : 0u);
}
// MOON JUMP's lift a frame for its level (world units)
inline float party_moon_lift(unsigned level) { return level >= 3 ? 1.0f : level == 2 ? 0.5f : level == 1 ? 0.25f : 0.0f; }
inline unsigned game_options_cars(unsigned w) { return w >> kGameOptCarsShift & 3u; }
inline unsigned game_options_word(bool mirror, bool crashbreaker, int cars, bool full = false) {
    return game_options_normal((mirror ? kGameOptMirror : 0u) | (crashbreaker ? kGameOptCrashbreaker : 0u) |
                               unsigned(cars < 0 ? 0 : cars > 3 ? 3 : cars) << kGameOptCarsShift | (full ? kGameOptCrashFull : 0u));
}

constexpr std::uint32_t kGoCurrentMode = 0x0051BA88u;       // [C] D_0051BA88: CB3Game's current mode
constexpr std::uint32_t kGoOnePlayerStage = 0x0051AD00u;    // [C] the one-player stage record (Single Event, World Tour)
constexpr std::uint32_t kGoTwoPlayerStage = 0x0051AEC0u;    // [C] the two-player stage record
constexpr std::uint32_t kGoSingleEventTag = 0x0051B920u;    // [C] +0x1B8 of a Single Event / two-player stage
constexpr std::uint32_t kGoMenuChoices = 0x01E900B0u;       // [C] CB3MenuChoices: +8 the mode (0 RACE .. 3 CRASH)
constexpr int kGoCrashMode = 3;

// The current mode (`current`, masked to the EE's 32 MB) and its +0x1B8 (`tag`) are a race the options apply to
inline bool game_options_stage(std::uint32_t current, std::uint32_t tag) {
    return (current == kGoOnePlayerStage || current == kGoTwoPlayerStage) && tag == kGoSingleEventTag;
}
// The options that keep a race's results apart (0: the game's own race)
inline unsigned game_options_record_bits(unsigned latched, bool crash_mode) {
    // (TUK-TUKS: the race is a traffic vehicle's, kept apart by trafficseries' bit 28, not this one)
    return (latched & kGameOptMirror) | (crash_mode ? 0u : latched & kGameOptCrashbreaker) |
           ((latched & kPartyMask & ~kPartyTuk) ? kGameOptParty : 0u);
}
// The race's record word with these options folded in (src/b3replay/records.h options_folder; the word raceopts,
// elim1 and heavyseries share: bit 15 the flag, TRAFFIC Normal when nothing else set it): bit 11 MIRROR MODE, bit 12 the
// Crashbreaker outside Crash mode, bit 24 any PARTY MODE (`record_bits`: game_options_record_bits)
constexpr std::uint64_t kPartyRecordBit = 0x01000000ull;   // party1 (tests/test_bit_owners.cpp)
inline std::uint64_t game_options_record_word(std::uint64_t w, unsigned record_bits) {   // (options3: the word is 64 bits)
    if (!(record_bits & (kGameOptAll | kGameOptParty))) return w;
    return (w ? w : 0x8002u) | ((record_bits & kGameOptMirror) ? 0x0800u : 0u) | ((record_bits & kGameOptCrashbreaker) ? 0x1000u : 0u) |
           ((record_bits & kGameOptParty) ? kPartyRecordBit : 0u);
}
// A replay's "game_options" entry (the word, "0" .. "8191"; anything else: none)
inline unsigned game_options_entry(const char* v) {
    if (!v || !*v) return 0;
    char* end = nullptr;
    const long n = std::strtol(v, &end, 10);
    return end && *end == 0 && n >= 0 && n <= long(kGameOptWord) ? game_options_normal(unsigned(n)) : 0u;
}
inline std::string party_name(unsigned bits) {
    static const char* const crazy[] = {"", "Basic", "Local Wildlife", "Mayhem"};
    static const char* const moon[] = {"", "Low", "Medium", "High"};
    std::string s;
    const auto add = [&](const std::string& t) { s += (s.empty() ? "" : ", ") + t; };
    if (party_crazy(bits)) add(std::string("CRAZY AI PHYSICS ") + crazy[party_crazy(bits)]);
    if (bits & kPartyTuk) add("TUK-TUKS");
    if (party_moon(bits)) add(std::string("MOON JUMP ") + moon[party_moon(bits)]);
    if (bits & kPartyCrash) add("CRASH ON DEMAND");
    if (bits & kPartyAiDriver) add("AI DRIVER BUTTON");
    if (bits & kPartyKamikaze) add("KAMIKAZE TRAFFIC");
    return s;
}
inline std::string game_options_name(unsigned bits) {
    if (!(bits & (kGameOptAll | kPartyMask))) return "none";
    std::string s;
    if (bits & kGameOptMirror) s = "MIRROR MODE";
    if (bits & kGameOptCrashbreaker)
        s += std::string(s.empty() ? "" : " + ") + "CRASHBREAKER IN EVERY MODE" + ((bits & kGameOptCrashFull) ? " (Full" : " (traffic") +
             ", after " + std::to_string(game_options_cars(bits)) + (game_options_cars(bits) == 1 ? " vehicle)" : " vehicles)");
    if (bits & kPartyMask) s += std::string(s.empty() ? "" : " + ") + "PARTY MODES (" + party_name(bits) + ")";
    return s;
}

// The latch: the options wanted when a race of those stages became the current mode, until it is not
class GameOptionLatch {
public:
    // `stage`: the current mode is such a race now; `wanted`: the options asked for now. True when the latched value changed.
    bool step(bool stage, unsigned wanted) {
        const unsigned before = latched_;
        if (!stage) latched_ = 0;
        else if (!in_stage_) latched_ = game_options_normal(wanted);
        in_stage_ = stage;
        return latched_ != before;
    }
    unsigned latched() const { return latched_; }
    bool in_stage() const { return in_stage_; }

private:
    bool in_stage_ = false;
    unsigned latched_ = 0;
};

}  // namespace b3native

#endif
