// race_options.h -- raceopts (2026-11-21): per-race options as the game's own rows on SINGLE EVENT's SELECT OPTIONS page
// (RACE, TIME ATTACK, ROAD RAGE; CRASH has no option rows and is left as it is). The values and their rules:
// race_choice.h.
//
// The page [C the generated units; decomp src/nonmatching/func_00432AB0.cpp, func_00432560.cpp, func_004328C0.cpp,
// func_003C9340.cpp, func_003BE2A0.cpp, func_003BD760.cpp]:
// - SELECT OPTIONS is one object, 0x01E96F78 (race_time.h kSelectOptionsPage). Its Prepare func_00432AB0(page, mode)
//   (+0x20 the mode: 0 RACE, 1 TIME ATTACK, 2 ROAD RAGE, 3 CRASH) fills its rows at +0x28 (0x14 bytes each: +0 the
//   label (UTF-16), +4 a truncation buffer, +8 the values, +0xC their count, +0xD the kind, +0xE disabled), the
//   selection {u16 row, u16 count} at +0x24, and hands both to the options component D_01E8961C (func_003BE5A0 ->
//   func_003BE2A0: it keeps the two pointers, lays the rows out centred on y 240 at 32 a row, up to 9). Kinds [C
//   func_003BD760]: 3 an array of UTF-16 string pointers indexed by the value, 4 string ids, 5 a number, 6 OFF / ON.
//   The page's values: +0x6B4 region, +0x6B8 track (ids at +0xF0, 8 bytes each), +0x6C0 rivals (0..5, 5 on entry).
// - Its Update (state +0x6C4 0, func_004328C0): Confirm leads out (track id to +8, rivals to +0x18), Back, Up / Down
//   (the component's selection, wrapping over the count), Left / Right -> func_00432560, which maps the row to its value
//   through a per-mode table (D_004D19F8 ...: rows past the game's own would read past it) and steps it with
//   wrap-around, then SetOptionText (0x3BC5B0) and the menu sound (FEVerticalMove 0x2853F0).
// - The page's values are reset by func_00433230 (called by the SINGLE EVENT flow func_003C9210 on entering SELECT
//   MODE). Continue (func_003C9340) copies them to CB3MenuChoices 0x01E900B0 (+4 the game type, 1 Single Event; +8 the
//   mode; +0x18 the rivals; +0xE8 the track), from which RequestOnePlayerGameMode 0x30C2C0 builds the race's stage
//   record 0x0051AD00 (+0xA4 the AI cars: RACE the rivals, TIME ATTACK 0, ROAD RAGE min(rivals, 3); +0x1B8 0x0051B920
//   for Single Event, World Tour 0x0051B928; the game's current mode at 0x0051BA88).
//
// Ours (wrappers on the hook table's port layer; the game's code draws and navigates every row):
//   - the component's Prepare, for this page only (not CRASH): the page's rows copied into a private array in free guest
//     RAM (GuestArena::kRaceOptions, checked zero before first use) with our rows after them (kind 3, string lists),
//     the count raised; then each of our rows' text set as the page does its own;
//   - optpages: RACE and ROAD RAGE have more rows than the component's 9 holds with the page's own, so theirs come in
//     two pages (race_choice.h race_pages_for) with a last row MORE ("1 / 2"). The component is prepared with page 1 (the
//     largest; every Prepare starts there). Left / Right on MORE shows the other page in place: our rows' entries
//     rewritten, the count set (the page's +0x26, which the component reads through its selection pointer), the
//     selection put on MORE's new place (the component's idle Update moves the highlight there), each of the prepared
//     rows re-made by the component's own calls (func_003BC360: the row's enabled state and its label, re-laid out at its
//     place; SetOptionText: its value) -- rows past a shorter page's end are blanked (an empty label and value) and are
//     past the count, so never reached. Nothing of the component is released or prepared again;
//   - the page's change (func_00432560): Left / Right on our rows is ours (the same wrap-around, text and sound as the
//     game's); on its own rows the game's code, then the LAPS row's "(Default)" follows the track. ROAD RAGE's RIVALS
//     row sets the page's own rivals (+0x6C0: 1, 2, or 5 = the game's 3), so the game's Continue carries it;
//   - the page's reset (func_00433230): ours back to the defaults with the page's own (and native_play's on the main
//     menu, so nothing reaches World Tour or MULTIPLAYER);
//   - the race, only while the game's current mode is the Single Event stage of the page's mode:
//       LAPS: CB3OnePlayerStage's lap count (vtable +0x20, 0x134930 -> CB3GameData::GetNumLaps, the stage record's
//         +0x3B8): every reader (each car's CB3RaceCar::Prepare 0x152C1C -> score +0x2C4, the AI, the HUD) asks it;
//       TRAFFIC: the three readers of a valve's spawn rates (CB3TrafficValve +0x10: per valve lane 7 floats, vehicles a
//         minute per category) -- PrepareGenerationTimes 0x268270, UpdateGeneration 0x267800, FillLaneSection
//         0x266AE0 -- see the rates scaled for the call and put back after it (the track's data never changes, a
//         restart reads it as loaded); the game's own limits stay (254 vehicles, a lane queues at most 2);
//       records: with any gameplay option not the game's, the post-race records call (0x3D1090, CB3Records 0x01EA7810,
//         from 0x42F0F0 only) is made with no record kinds (a1: it then writes none; the results page's last-update
//         type / track set as a race without a record leaves them, so no RECORD tag shows), and the
//         profile's "this race counts" test (0x136F30: totals, unlocks, the signature takedowns) answers no.
// The values belong to the host, never saved; the replay holds the pads that set them (and "race_options" for the log).
// B3NATIVE_RACE_OPTS=0 leaves the page and the race as retail.
#ifndef B3NATIVE_RACE_OPTIONS_H
#define B3NATIVE_RACE_OPTIONS_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "hook_table.h"
#include "race_choice.h"
#include "race_time.h"       // race_time_name (a conditions value no longer offered, logged)
#include "records_guard.h"   // the records call without kinds (shared with elim1)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace b3native {

class RaceOptions {
public:
    // Off with B3NATIVE_RACE_OPTS=0; native_play also leaves it off for a replay recorded without it (the header option
    // "race_opts": a replay from before the rows, whose pads expect the game's own page)
    static bool requested();
    bool install(HookTable& hooks);

    // TIME OF DAY row on (native_play: EXTRAS' NIGHT; a replay's recorded value), asked at each Prepare, and the
    // setting's own value (true: night)
    void set_time_row(std::function<bool()> f) { time_row_query_ = std::move(f); }
    // elim1 (single_eliminator.h): SELECT MODE's ELIMINATOR was chosen (the game's mode is then RACE): no LAPS row (the
    // Eliminator's laps are its cars less one) and the stage's lap count left to the game
    void set_eliminator_query(std::function<bool()> f) { eliminator_query_ = std::move(f); }
    // MORE RIVALS (tools/native/more_rivals.h): RACE's own RIVALS row up to this many (the game's 5; 0 / 5: as the game)
    void set_rivals_max(std::function<int()> f) { rivals_max_ = std::move(f); }
    // extras (PC SETTINGS > EXTRAS > SELECT OPTIONS): the rows the player has on (bits by RaceRow index; none set: every row,
    // as before EXTRAS). Asked at each Prepare of the page (race_choice.h race_pages_masked)
    void set_rows_query(std::function<std::uint32_t()> f) { rows_query_ = std::move(f); }
    // elim1: TIMED ELIMINATION is On for the race on now (an Eliminator stage with the page's choice)
    bool timed_elimination() const { return choice_.timed != 0 && eliminator() && race_active(); }
    // gearbox: TRANSMISSION Manual for the race on now (tools/native/gearbox.h)
    bool manual_gears() const { return choice_.manual != 0 && race_active(); }
    void set_setting_night(std::function<bool()> f) { setting_night_ = std::move(f); }
    // condrows: the WEATHER row on (Clear / Rain; native_play: EXTRAS' RAIN; a replay's recorded value), asked at each
    // Prepare, and the setting's own WEATHER (true: rain)
    void set_weather_row(std::function<bool()> f) { weather_row_query_ = std::move(f); }
    void set_setting_rain(std::function<bool()> f) { setting_rain_ = std::move(f); }
    // options3: TRAFFIC PATTERN Varied's seed from this (native_play: the VBlank count, the same in a replay)
    void set_clock(std::function<std::uint64_t()> f) { clock_ = std::move(f); }
    // The game's text language (race_choice.h race_words' code; native_play: platform_disc.h language_text())
    void set_language(std::function<std::string()> f) { language_ = std::move(f); }
    // A track's own lap count in a SINGLE EVENT race (0: unknown): native_play reads the track's GAMEDATA.BGD
    void set_track_laps(std::function<int(std::uint64_t)> f) { track_laps_ = std::move(f); }

    const RaceChoice& choice() const { return choice_; }   // the page's
    int mode() const { return mode_; }                     // the mode the page last showed (-1: none since the reset)
    // The main menu (native_play, per VBlank): back to the defaults
    void reset() {
        if (choice_ != RaceChoice{}) changed_ = true;
        choice_ = RaceChoice{};
        shown_ = false;
        mode_ = -1;
    }
    // The game's current mode is the Single Event stage of the page's mode (guest RAM)
    bool race_active() const;
    // The record word of the race on now (race_choice.h record_word; 0: the game's own race)
    std::uint64_t race_record_word() const { return race_active() ? choice_.record_word() : 0; }
    // VBlank side: whether the choice changed since the last call (the log, the replay)
    bool take_changed() { const bool c = changed_; changed_ = false; return c; }
    unsigned rows_shown() const { return shown_ ? unsigned(rows_.size()) : 0u; }
    // the race side's counts (logs and checks)
    struct Counts {
        std::uint64_t laps_given = 0, traffic_scaled = 0, records_skipped = 0, profile_skipped = 0;
        std::uint64_t rows_rehidden = 0;   // optpages: a short page's blank rows put off the screen again after an Update
        int laps = 0, track_laps = 0;   // the last given, and the stage's own
    };
    const Counts& counts() const { return counts_; }
    // TEST ONLY (B3_RACEOPTS_TEST_FINISH=<ticks>, set before install; native_play reads it, off by default): a SINGLE EVENT
    // race of the page's mode gets the player's lap completed by the game's own IncrementLapCount (0x15A1F0) after each
    // <ticks> racing updates of the player's score (CB3Score::Update 0x15B000 wrapped); ROAD RAGE ends as on the player's
    // wreck (SetFinished 4). So a scripted run reaches the game's finish, results and records (a scripted pad never
    // drives a lap or wrecks on cue). Checks only.
    void set_test_finish(int ticks) { test_finish_ = ticks > 0 ? ticks : 0; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static RaceOptions*& instance();
    static void component_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void page_change(const b3host::CallArgs& a, b3host::CallResult& r);
    static void page_reset(const b3host::CallArgs& a, b3host::CallResult& r);
    static void component_update(const b3host::CallArgs& a, b3host::CallResult& r);   // optpages
    Entry component_update_ = nullptr;
    void blank_rows();
    static void stage_laps(const b3host::CallArgs& a, b3host::CallResult& r);
    static void valve_rates(const b3host::CallArgs& a, b3host::CallResult& r, Entry next);
    static void prepare_times(const b3host::CallArgs& a, b3host::CallResult& r) { valve_rates(a, r, instance()->prepare_times_); }
    static void update_generation(const b3host::CallArgs& a, b3host::CallResult& r) { valve_rates(a, r, instance()->update_generation_); }
    static void fill_lane(const b3host::CallArgs& a, b3host::CallResult& r) { valve_rates(a, r, instance()->fill_lane_); }
    static void records(const b3host::CallArgs& a, b3host::CallResult& r);
    static void profile_counts(const b3host::CallArgs& a, b3host::CallResult& r);
    static void score_update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void stage_cars(const b3host::CallArgs& a, b3host::CallResult& r);
    // options3: START, RIVAL STYLE, BURNOUT POINTS, TIME LIMIT
    static void rolling_start(const b3host::CallArgs& a, b3host::CallResult& r);
    static void ai_mode(const b3host::CallArgs& a, b3host::CallResult& r);
    static void rr_score_flags(const b3host::CallArgs& a, b3host::CallResult& r);
    static void rr_world_prepared(const b3host::CallArgs& a, b3host::CallResult& r);
    static void hud_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void in_championship(const b3host::CallArgs& a, b3host::CallResult& r);
    static void traffic_reset(const b3host::CallArgs& a, b3host::CallResult& r);
    void pick_rival_cars();   // options3: RIVALS' CARS (from stage_cars)
    static std::uint32_t game_random();             // mixedrivals: the game's CGtRandom (D_00518680), one step
    static std::vector<int> mixed_rival_classes();  // mixedrivals: the classes with an unlocked car
public:
    // options3: true while RIVALS' CARS makes its own pick (its call to func_0030B9D0 goes through dispatch): another
    // wrapper on 0x30B9D0 that changes the class (trafficseries: a traffic vehicle's donor class) passes this call through
    // untouched, so the player's explicit choice wins. With RIVALS' CARS at the game's value no such call is made, and
    // SELECT CAR's own pick is the other wrapper's to shape.
    static bool explicit_rival_pick() { return picking_rivals(); }
private:
    static bool& picking_rivals() { static bool b = false; return b; }
    bool region_ok();
    RaceRowModel model() const;
    void write_row(RaceRow row, b3host::gaddr entry);
    void set_text(unsigned row_index, RaceRow row);
    std::uint64_t page_track() const;

    Entry component_prepare_ = nullptr, page_change_ = nullptr, page_reset_ = nullptr, stage_laps_ = nullptr;
    Entry prepare_times_ = nullptr, update_generation_ = nullptr, fill_lane_ = nullptr, records_ = nullptr, profile_counts_ = nullptr;
    Entry score_update_ = nullptr, stage_cars_ = nullptr;
    Entry rolling_start_ = nullptr, ai_mode_ = nullptr, rr_score_flags_ = nullptr, rr_world_prepared_ = nullptr,
          hud_prepare_ = nullptr, in_championship_ = nullptr, traffic_reset_ = nullptr;
    std::function<std::uint64_t()> clock_;   // options3: TRAFFIC PATTERN Varied's seed source (native_play: the VBlank)
    bool in_hud_prepare_ = false;   // options3: CB3HUDManager::PrepareNormalComponents is running
    std::uint32_t seed_a_ = 0, seed_b_ = 0;   // replayview: the race's TRAFFIC PATTERN Varied seed (its replay takes it again)
    bool seed_set_ = false;
    bool told_start_ = false, told_style_ = false, told_points_ = false, told_hud_ = false;   // (one log line each)
    int test_finish_ = 0, test_ticks_ = 0;
    std::function<bool()> setting_night_, time_row_query_, eliminator_query_, weather_row_query_, setting_rain_;
    std::function<int()> rivals_max_;
    std::function<std::uint32_t()> rows_query_;
    bool eliminator() const { return eliminator_query_ && eliminator_query_(); }
    std::function<std::string()> language_;
    std::function<int(std::uint64_t)> track_laps_;
    RaceChoice choice_;
    bool region_checked_ = false, region_bad_ = false;
    bool shown_ = false, changed_ = false;
    unsigned game_rows_ = 0;
    int mode_ = -1, track_laps_now_ = 0;
    std::vector<RaceRow> rows_;     // the rows shown (the page's, MORE last)
    RacePages pages_;               // optpages: the mode's pages
    int page_ = 0;                  // the page shown
    unsigned prepared_rows_ = 0;    // our rows when the component was prepared (page 1's; the rest blanked on a shorter page)
    void show_page(int page);
    Counts counts_;
};

}  // namespace b3native

// ---- implementation (header-only: native_play.cpp is its one includer) ----
namespace b3native {
namespace race_options_detail {
constexpr b3host::gaddr kPage = 0x01E96F78u;            // [C] SELECT OPTIONS (race_time.h kSelectOptionsPage)
constexpr b3host::gaddr kComponent = 0x01E8961Cu;       // [C func_00432AB0] its options component
constexpr b3host::gaddr kComponentPrepare = 0x003BE5A0u, kPageChange = 0x00432560u, kPageReset = 0x00433230u;
constexpr b3host::gaddr kSetOptionText = 0x003BC5B0u, kMenuSound = 0x002853F0u, kSound = 0x01E7A800u;
// optpages: CB3VOptionsSelectComponent's row refresh (func_003BC360(component, row, enabled): the row's +0xE from it, its
// label prepared again at its place, its value colour; the game's own enable / disable of a row)
constexpr b3host::gaddr kRowRefresh = 0x003BC360u;
// its row label's and row value's Prepare at a given x [L func_003BE040(component, row, x, selected), func_003BD760(component,
// row, value, x)]: a blank row's objects put off the screen (x kOffScreen), a shown row's label back at the rows' x (row 0's
// label object's +0); the component's +0x14 label objects, +0xDC the highlighted row
constexpr b3host::gaddr kLabelPrepare = 0x003BE040u, kValuePrepare = 0x003BD760u;
constexpr float kOffScreen = 2000.0f;
// its Update (func_003BC7F0, per frame): after a page change it re-makes the row the highlight leaves (the old MORE place,
// blank now) at its place, however many Updates the move takes; while a page is short, after each Update a blank row whose
// label object is back on the screen (its +0, the x func_003BC360 re-makes it at) is put off it again
constexpr b3host::gaddr kComponentUpdate = 0x003BC7F0u;
constexpr b3host::gaddr kGetMenuLeft = 0x0022CDE0u, kGetMenuRight = 0x0022CD60u, kInput = 0x004F50C0u;
// the race [C RequestOnePlayerGameMode 0x30C2C0, func_00136F30, func_0042F0F0]
constexpr b3host::gaddr kMenuChoices = 0x01E900B0u;     // +4 the game type (1 Single Event), +8 the mode
constexpr b3host::gaddr kCurrentMode = 0x0051BA88u, kOnePlayerStage = kRaceOnePlayerStage;
constexpr b3host::gaddr kStageLaps = 0x00134930u;       // CB3OnePlayerStage's lap count (vtable +0x20)
constexpr b3host::gaddr kPrepareTimes = 0x00268270u, kUpdateGeneration = 0x00267800u, kFillLane = 0x00266AE0u;
constexpr b3host::gaddr kRecords = 0x003D1090u, kProfileCounts = 0x00136F30u;
constexpr b3host::gaddr kRequestOnePlayer = 0x0030C2C0u;   // CB3MenuChoices::RequestOnePlayerGameMode(this, p0)
// options3 [C the decomp]:
// - func_00135170 (the stage logics' vtable slot +0x88 in the race / Road Rage / Time Attack logics): returns 1, a
//   rolling start; START Standing answers 0 (the cars start standing)
// - CB3StageLogic::GetAIMode 0x134340: 0 for a race (the Road Rage logic has its own); 1 makes the rivals drive as Road
//   Rage's (RIVAL STYLE Road Rage)
// - CB3OfflineRoadRageLogic::GetScoreModeFlags 0x38C020: 2; bit 0 is "non-takedown Burnout points" (its 11 readers test
//   only that bit; the base logic returns -1): BURNOUT POINTS All answers 3 (Road Rage scores a race's points)
// - the time limit: CB3Score +0x2D0 (the race car's +0x1390) ends the race (SetFinished 2) when the race time reaches it
//   [C CB3Score::Update 0x15B000]; World Tour's timed Road Rage sets 180 s in CB3OfflineRoadRageLogic::OnWorldPrepared
//   0x38C3F0 and its HUD shows the timer (CB3HUDManager::PrepareNormalComponents 0x1A0AF0 prepares _gTimerComponentP1
//   instead of the score list when CB3Game::IsInChampionship 0x131CE0): a Single Event Road Rage with TIME LIMIT gets the
//   player's limit after OnWorldPrepared and that HUD (IsInChampionship answers yes inside PrepareNormalComponents only)
constexpr b3host::gaddr kRollingStart = 0x00135170u, kGetAIMode = 0x00134340u, kRrScoreFlags = 0x0038C020u;
constexpr b3host::gaddr kRrWorldPrepared = 0x0038C3F0u, kHudPrepareNormal = 0x001A0AF0u, kIsInChampionship = 0x00131CE0u;
constexpr b3host::gaddr kWorldPlayers = 0x0064D3A0u;   // the world's player cars (D_0064D3A0), each 0x27D0 from gWorld +0x12AD50
constexpr b3host::gaddr kPlayerCar0 = 0x00522660u + 0x12AD50u;
constexpr unsigned kPlayerCarStride = 0x27D0u, kCarTimeLimit = 0x1390u;
// - the traffic's random numbers: CB3TrafficSystem::ResetMemberVariables 0x1632C0 seeds them with two fixed words at the
//   system's +0x36348 / +0x3634C (gWorld +0x4DE0 = 0x527440: 0x0055D788 / 0x0055D78C, the generators' D_0055D788 / 78C) --
//   every race the same pattern. TRAFFIC PATTERN Varied writes a seed of the race start's VBlank there after it (a seed
//   per race)
constexpr b3host::gaddr kTrafficReset = 0x001632C0u, kTrafficSeedA = 0x0055D788u, kTrafficSeedB = 0x0055D78Cu;
constexpr b3host::gaddr kReplayRequested = 0x00665E52u;   // replayview: the replay asked for (the traffic is reset before
                                                          // the world's prepare copies it to "in playback")
// - RIVALS' CARS: SELECT CAR's Confirm (func_00310200, Single Event) picks the rivals' cars with func_0030B9D0(choices,
//   class): for each rival from +0x30 to +0x30 + +0x18 a car from func_0030B6E0 (the race vehicle list's cars of that
//   class -- EB3MenuCarClasses, 5 only its two special cars -- unlocked ones first, each at most 3 times; random) into
//   CB3MenuChoices +0x80 + 8 i and a colour 0..2 into +0xC0 + 4 i (another colour for a car already taken), the class
//   the player's own. RequestOnePlayerGameMode then copies them into the stage record. With RIVALS' CARS set, the pick
//   is made again just before that copy with the chosen class (+0x30 1, +0x18 5 for it: the player's slot kept, all
//   five filled for ROAD RAGE's 4 / 5), or the player's car for every rival ("Same as yours", the colours 0..2 in turn).
//   The explicit choice wins over another wrapper on 0x30B9D0 (RaceOptions::explicit_rival_pick).
constexpr b3host::gaddr kPickRivals = 0x0030B9D0u;
// mixedrivals: RIVALS' CARS Mixed -- each rival slot its own class, drawn from the game's random numbers (D_00518680, the
// CGtRandom func_0030B6E0 / func_0030B9D0 draw from: mnHigh, mnLow; Random() is inlined in retail, so the same step here)
// among the classes that have an unlocked car in the race vehicle list (the same test as func_0030B6E0's first pass:
// GetRaceVehicleGtID over its 0x43 entries, DoesVehicleExist on D_005186A0, GetClassFromID, IsCarUnlocked on the profile
// D_0051BF9C): Compact .. Super, Heavyweights only once one is unlocked; never class 5 (its two special cars). Then the
// game's own pick for that class, for that slot alone (+0x30 the slot, +0x18 1: unlocked cars first, each at most 3
// times counting the slots before it, a colour not already taken with that car). Guest state and the game's own random
// numbers only: a replay re-runs the same draws.
constexpr b3host::gaddr kGameRandom = 0x00518680u;   // CGtRandom {mnHigh, mnLow}
constexpr b3host::gaddr kRaceVehicleId = 0x003EB020u, kVehicleExists = 0x00252000u, kVehicleClass = 0x00251CB0u,
                        kCarUnlocked = 0x0013A660u, kVehicleList = 0x005186A0u, kProfilePtr = 0x0051BF9Cu;
constexpr int kRaceVehicles = 0x43;
// the test finish: player 0's CB3Score (gWorld 0x522660 + 0x12AD50 + 0x10C0: capture.h) and the game's lap count call
constexpr b3host::gaddr kScoreUpdate = 0x0015B000u, kIncrementLap = 0x0015A1F0u, kPlayerScore = 0x0064E470u;
constexpr b3host::gaddr kSetFinished = 0x00151780u;   // CB3RaceCar::SetFinished(car, state); the score's car at +0xC8
constexpr unsigned kRowBytes = 0x14u, kMaxComponentRows = unsigned(kRacePageMaxRows), kValveLanes = 4, kRates = 7;
constexpr std::uint8_t kKindStrings = 3;
constexpr std::int32_t kGameRivals = 5;                 // the page's rivals on entry (ROAD RAGE: the game makes it 3)
// the region: 9 rows, then per row kind its value pointers (16 each), then UTF-16 strings (0x30 bytes each), then a
// scratch buffer (native_play's track path), then per row kind the component's truncation buffer (a value wider than its
// box is cut there with the game's ellipsis, func_003BD760)
// condrows: the layout covers the 14 kinds up to MORE (kUniformKinds), as before; WEATHER (two values) has a block of its
// own after RIVALS' CARS' (below), so every other kind's strings stay where they were
constexpr unsigned kUniformKinds = unsigned(RaceRow::kPage) + 1u;
static_assert(kUniformKinds == 14 && unsigned(RaceRow::kWeather) == kUniformKinds && kRaceRowKinds == kUniformKinds + 1, "WEATHER the one kind after the layout's");
constexpr b3host::gaddr kRegion = GuestArena::kRaceOptions.address, kRows = kRegion,
                        kPtrs = kRegion + 0xC0u, kStrings = kPtrs + kUniformKinds * 8u * 4u,   // (trafficcheck: after every kind's pointers)
                        kRegionEnd = kRegion + GuestArena::kRaceOptions.size;
constexpr unsigned kPtrsPerRow = 8, kStringBytes = 0x30u;   // (options3 / trafficcheck: 13 row kinds, at most 8 values each: LAPS, RIVALS' CARS)
constexpr unsigned kStringSlots = kUniformKinds + kUniformKinds * kPtrsPerRow;   // the labels, then per row kind its values
constexpr b3host::gaddr kScratch = kStrings + kStringSlots * kStringBytes, kScratchBytes = 0x100u;
static_assert(kRows + kMaxComponentRows * kRowBytes <= kPtrs, "the rows fit before the value pointers");
static_assert(kPtrs + kUniformKinds * kPtrsPerRow * 4u <= kStrings, "every row kind's value pointers fit");
static_assert(kRaceMaxLaps <= int(kPtrsPerRow) && kRaceTrafficValues <= int(kPtrsPerRow), "every row's values fit");
// (a value of up to 23 units copied in, then cut and the ellipsis appended: 32 units each leaves room for both)
constexpr b3host::gaddr kTruncate = kScratch + kScratchBytes, kTruncateBytes = 0x40u;
static_assert(kTruncate + kUniformKinds * kTruncateBytes <= kRegionEnd, "the strings, the scratch and the truncation buffers fit the region");
// optpages: a blank row (a shorter page's rows past its end): an empty string and a value list of it
constexpr b3host::gaddr kBlank = kTruncate + kUniformKinds * kTruncateBytes, kBlankPtrs = kBlank + 4u;
static_assert(kBlankPtrs + 4u <= kRegionEnd, "the blank row fits the region");
// mixedrivals: RIVALS' CARS has one value more than a kind's kPtrsPerRow: its value pointers here (the others' layout
// unchanged), its values past the eighth in the strings after them
constexpr unsigned kCarsValues = unsigned(kRaceRivalCars);
constexpr b3host::gaddr kCarsPtrs = kBlankPtrs + 4u, kCarsExtra = kCarsPtrs + kCarsValues * 4u;
static_assert(kCarsValues > kPtrsPerRow && kCarsValues <= kPtrsPerRow + 1, "RIVALS' CARS' values past the eighth: one string after its pointers");
static_assert(kCarsExtra + (kCarsValues - kPtrsPerRow) * kStringBytes <= kRegionEnd, "RIVALS' CARS' pointers and its last value fit the region");
// condrows: WEATHER's block: its two value pointers, its label, its two values, its truncation buffer
constexpr unsigned kWeatherValues = 2;
constexpr b3host::gaddr kWeatherPtrs = kCarsExtra + (kCarsValues - kPtrsPerRow) * kStringBytes,
                        kWeatherLabel = kWeatherPtrs + kWeatherValues * 4u, kWeatherStrings = kWeatherLabel + kStringBytes,
                        kWeatherTruncate = kWeatherStrings + kWeatherValues * kStringBytes;
static_assert(kWeatherTruncate + kTruncateBytes <= kRegionEnd, "WEATHER's block fits the region");

inline b3host::gaddr string_slot(unsigned i) { return kStrings + i * kStringBytes; }
// UTF-8 text as the game's UTF-16 (the Basic Multilingual Plane; at most kStringBytes / 2 - 1 units, then 0)
inline void write_utf16(b3host::gaddr at, const std::string& text) {
    unsigned n = 0;
    for (std::size_t i = 0; i < text.size() && n + 1 < kStringBytes / 2; ++n) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t u = c;
        std::size_t len = 1;
        if (c >= 0xE0 && i + 2 < text.size()) { u = (c & 0x0Fu) << 12 | (text[i + 1] & 0x3Fu) << 6 | (text[i + 2] & 0x3Fu); len = 3; }
        else if (c >= 0xC0 && i + 1 < text.size()) { u = (c & 0x1Fu) << 6 | (text[i + 1] & 0x3Fu); len = 2; }
        b3host::store<std::uint16_t>(at + 2u * n, std::uint16_t(u));
        i += len;
    }
    b3host::store<std::uint16_t>(at + 2u * n, 0);
}
inline unsigned kind_index(RaceRow r) { return unsigned(r); }
// every row kind's label, values, value pointers and truncation buffer (options3: one layout for every kind -- trafficcheck's TRAFFIC CHECK the thirteenth --, elim1's
// TIMED ELIMINATION the fifth, gearbox's TRANSMISSION the sixth)
inline b3host::gaddr label_slot(RaceRow r) { return r == RaceRow::kWeather ? kWeatherLabel : string_slot(kind_index(r)); }
inline b3host::gaddr value_slot(RaceRow r, unsigned i) {
    if (r == RaceRow::kWeather) return kWeatherStrings + (i < kWeatherValues ? i : 0u) * kStringBytes;
    if (i >= kPtrsPerRow) return kCarsExtra + (i - kPtrsPerRow) * kStringBytes;   // (mixedrivals: RIVALS' CARS only)
    return string_slot(kUniformKinds + kind_index(r) * kPtrsPerRow + i);
}
inline b3host::gaddr ptrs_of(RaceRow r) {
    return r == RaceRow::kCars ? kCarsPtrs : r == RaceRow::kWeather ? kWeatherPtrs : kPtrs + kind_index(r) * kPtrsPerRow * 4u;
}
inline b3host::gaddr truncate_of(RaceRow r) { return r == RaceRow::kWeather ? kWeatherTruncate : kTruncate + kind_index(r) * kTruncateBytes; }
inline b3host::gaddr a0(const b3host::CallArgs& a) { return a.gpr[0].w[0] & 0x01FFFFFFu; }
}  // namespace race_options_detail

inline bool RaceOptions::requested() {
    const char* e = std::getenv("B3NATIVE_RACE_OPTS");
    return !(e && e[0] == '0' && e[1] == 0);
}

inline RaceOptions*& RaceOptions::instance() { static RaceOptions* p = nullptr; return p; }

inline bool RaceOptions::install(HookTable& hooks) {
    using namespace race_options_detail;
    const std::vector<Hook> set = {{kComponentPrepare, HookLayer::kPort, &component_prepare, &component_prepare_},
                                   {kPageChange, HookLayer::kPort, &page_change, &page_change_},
                                   {kPageReset, HookLayer::kPort, &page_reset, &page_reset_},
                                   {kStageLaps, HookLayer::kPort, &stage_laps, &stage_laps_},
                                   {kPrepareTimes, HookLayer::kPort, &prepare_times, &prepare_times_},
                                   {kUpdateGeneration, HookLayer::kPort, &update_generation, &update_generation_},
                                   {kFillLane, HookLayer::kPort, &fill_lane, &fill_lane_},
                                   {kRecords, HookLayer::kPort, &records, &records_},
                                   {kProfileCounts, HookLayer::kPort, &profile_counts, &profile_counts_},
                                   {kRequestOnePlayer, HookLayer::kPort, &stage_cars, &stage_cars_},
                                   {kRollingStart, HookLayer::kPort, &rolling_start, &rolling_start_},   // options3
                                   {kGetAIMode, HookLayer::kPort, &ai_mode, &ai_mode_},
                                   {kRrScoreFlags, HookLayer::kPort, &rr_score_flags, &rr_score_flags_},
                                   {kRrWorldPrepared, HookLayer::kPort, &rr_world_prepared, &rr_world_prepared_},
                                   {kHudPrepareNormal, HookLayer::kPort, &hud_prepare, &hud_prepare_},
                                   {kIsInChampionship, HookLayer::kPort, &in_championship, &in_championship_},
                                   {kTrafficReset, HookLayer::kPort, &traffic_reset, &traffic_reset_},
                                   {kComponentUpdate, HookLayer::kPort, &component_update, &component_update_}};   // optpages
    std::vector<Hook> all = set;
    if (test_finish_) all.push_back({kScoreUpdate, HookLayer::kPort, &score_update, &score_update_});
    for (const Hook& h : all)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "race_options: %08X is not native: SELECT OPTIONS left as retail\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:race_options", all, &error)) {
        std::fprintf(stderr, "race_options: %s: SELECT OPTIONS left as retail\n", error.c_str());
        return false;
    }
    instance() = this;
    std::printf("race_options: SELECT OPTIONS rows TRAFFIC, LAPS (RACE), RIVALS (ROAD RAGE)%s (B3NATIVE_RACE_OPTS=0: retail's page)\n",
                time_row_query_ && time_row_query_() ? (weather_row_query_ && weather_row_query_() ? ", TIME OF DAY, WEATHER" : ", TIME OF DAY")
                : weather_row_query_ && weather_row_query_() ? ", WEATHER" : "");
    if (test_finish_)
        std::printf("race_options: TEST (B3_RACEOPTS_TEST_FINISH=%d): the player's laps completed by the test, every %d racing updates\n",
                    test_finish_, test_finish_);
    return true;
}

inline bool RaceOptions::region_ok() {
    using namespace race_options_detail;
    if (region_checked_) return !region_bad_;
    region_checked_ = true;
    for (b3host::gaddr a = kRegion; a < kRegionEnd; a += 4)
        if (b3host::load<std::uint32_t>(a) != 0) {
            std::fprintf(stderr, "race_options: guest RAM %08X..%08X is in use (%08X): SELECT OPTIONS left as retail\n",
                         kRegion, kRegionEnd, a);
            region_bad_ = true;
            return false;
        }
    return true;
}

inline bool RaceOptions::race_active() const {
    using namespace race_options_detail;
    if (mode_ < 0 || mode_ > 2) return false;
    RaceStage s;
    s.current_mode = b3host::load<std::uint32_t>(kCurrentMode);
    s.tag = b3host::load<std::uint32_t>(kOnePlayerStage + 0x1B8u);
    s.game_type = b3host::load<std::int32_t>(kMenuChoices + 4u);
    s.mode = b3host::load<std::int32_t>(kMenuChoices + 8u);
    return race_options_apply(mode_, s);
}

inline std::uint64_t RaceOptions::page_track() const {
    using namespace race_options_detail;
    const int i = b3host::load<std::int32_t>(kPage + 0x6B8u);
    if (i < 0 || i >= 20) return 0;
    return b3host::load<std::uint64_t>(kPage + 0xF0u + 8u * unsigned(i));
}

inline RaceRowModel RaceOptions::model() const {
    RaceRowModel m;
    m.track_laps = track_laps_now_;
    m.night = setting_night_ && setting_night_();
    m.weather = weather_row_query_ && weather_row_query_();
    m.rain = m.weather && setting_rain_ && setting_rain_();
    if (language_) m.language = language_();
    m.pages = pages_.empty() ? 1 : int(pages_.size());
    m.page = page_;
    return m;
}

// The row's entry, its value strings and their pointer array
inline void RaceOptions::write_row(RaceRow row, b3host::gaddr e) {
    using namespace race_options_detail;
    const RaceRowModel m = model();
    const b3host::gaddr lbl = label_slot(row), ptrs = ptrs_of(row);
    write_utf16(lbl, m.label(row));
    const int n = m.count(row);
    for (int i = 0; i < n; ++i) {
        const b3host::gaddr s = value_slot(row, unsigned(i));
        write_utf16(s, m.text(row, i));
        b3host::store<std::uint32_t>(ptrs + 4u * unsigned(i), s);
    }
    b3host::store<std::uint32_t>(e + 0x0u, lbl);
    b3host::store<std::uint32_t>(e + 0x4u, truncate_of(row));   // a long value cut to its box
    b3host::store<std::uint32_t>(e + 0x8u, ptrs);
    b3host::store<std::uint8_t>(e + 0xCu, std::uint8_t(n));
    b3host::store<std::uint8_t>(e + 0xDu, kKindStrings);
    b3host::store<std::uint8_t>(e + 0xEu, m.disabled(row) ? 1 : 0);   // (as the game's TRACK row with one track)
    b3host::store<std::uint8_t>(e + 0xFu, 0);
    b3host::store<std::uint32_t>(e + 0x10u, 0);
}

inline void RaceOptions::set_text(unsigned row_index, RaceRow row) {
    using namespace race_options_detail;
    b3host::call_guest<void>(kSetOptionText, kComponent, int(row_index), model().index(row, choice_));
}

// func_003BE5A0(component, selection, rows): for SELECT OPTIONS (not CRASH), the page's rows and ours
inline void RaceOptions::component_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace race_options_detail;
    RaceOptions* p = instance();
    if (a0(a) != kComponent || (a.gpr[1].w[0] & 0x01FFFFFFu) != kPage + 0x24u || (a.gpr[2].w[0] & 0x01FFFFFFu) != kPage + 0x28u) {
        p->component_prepare_(a, r);
        return;
    }
    p->shown_ = false;
    const int mode = b3host::load<std::int32_t>(kPage + 0x20u);
    const unsigned n = b3host::load<std::uint16_t>(kPage + 0x26u);
    const bool time_row = p->time_row_query_ && p->time_row_query_();
    const bool weather_row = p->weather_row_query_ && p->weather_row_query_();
    RacePages pages = race_pages_for(mode, time_row, weather_row);
    {   // extras / condrows: a half whose row is off (NIGHT / RAIN switched off) back to the game's (Day, Clear)
        const RaceRowModel m = p->model();
        const int t = p->choice_.time;
        if (const int o = m.offered(t, time_row, weather_row); o != t) {
            std::printf("race_options: conditions %s no longer offered: %s\n", race_time_name(t), race_time_name(o));
            p->choice_.time = std::uint8_t(o);
        }
    }
    if (mode == 0 && p->eliminator()) {   // elim1: no LAPS for an Eliminator (and none chosen); TIMED ELIMINATION instead
        for (auto& pg : pages)
            for (RaceRow& r2 : pg)
                if (r2 == RaceRow::kLaps) r2 = RaceRow::kTimed;
        p->choice_.laps = 0;
    } else {
        p->choice_.timed = 0;   // (only an Eliminator has it)
    }
    if (p->rows_query_) {   // extras: only the rows the player has on; the others at the game's values (the race takes them)
        const RacePages masked = race_pages_masked(pages, mode, p->rows_query_());
        for (const auto& pg : pages)
            for (RaceRow r2 : pg) {
                bool kept = false;
                for (const auto& mg : masked) kept = kept || std::find(mg.begin(), mg.end(), r2) != mg.end();
                if (!kept) race_row_default(r2, p->choice_);
            }
        pages = masked;
    }
    // MORE RIVALS: RACE's own RIVALS row (the page's third, kind 5: a number; its value +0x6C0 between the shorts +0xC8
    // and +0xCA [C func_00432AB0 / func_00432560]) up to rivals_max_: the game's own row then steps and shows 6, 7 and
    // its Continue carries them (CB3MenuChoices +0x18 -> the stage record's AI cars, more_rivals.h). (extras: before the
    // page is left to the game alone, so it holds with none of our rows on too)
    const int rivals_mx = mode == 0 && !p->eliminator() && p->rivals_max_ ? p->rivals_max_() : 0;
    if (mode == 0 && !p->eliminator() && p->rivals_max_ && n >= 3 && p->region_ok()) {
        const int mx = rivals_mx;
        if (mx > kGameRivals && mx > int(b3host::load<std::uint16_t>(kPage + 0xCAu))) {
            b3host::store<std::uint16_t>(kPage + 0xCAu, std::uint16_t(mx));
            b3host::store<std::uint8_t>(kPage + 0x28u + 2u * kRowBytes + 0xCu, std::uint8_t(mx - b3host::load<std::uint16_t>(kPage + 0xC8u)));
        }
    }
    // extras: MORE RIVALS off (or an Eliminator) with a RIVALS of 6 / 7 kept from when it was on: back to the game's 5, the
    // row's range as the game made it (more_rivals.h clamps the race's own copy, CB3MenuChoices +0x18, too)
    if (mode == 0 && rivals_mx <= kGameRivals) {
        constexpr b3host::gaddr kChoicesRivals = 0x01E900B0u + 0x18u;   // CB3MenuChoices +0x18 (the race's copy)
        for (const b3host::gaddr at : {kPage + 0x6C0u, kChoicesRivals})
            if (b3host::load<std::int32_t>(at) > kGameRivals) {
                std::printf("race_options: RIVALS %d with MORE RIVALS off: the game's %d (%08X)\n", b3host::load<std::int32_t>(at),
                            kGameRivals, unsigned(at));
                b3host::store<std::int32_t>(at, kGameRivals);
            }
    }
    const std::vector<RaceRow> ours = race_page_rows(pages, 0);   // optpages: every Prepare shows page 1
    if (ours.empty()) std::printf("race_options: SELECT OPTIONS (mode %d) as the game's own: none of its EXTRAS rows on\n", mode);
    else {
        std::printf("race_options: SELECT OPTIONS (mode %d) with %zu page%s of rows:", mode, pages.size(), pages.size() > 1 ? "s" : "");
        for (const auto& pg : pages) for (RaceRow r2 : pg) std::printf(" %s;", race_row_label(r2));
        std::printf("\n");
    }
    if (ours.empty() || n == 0 || n + ours.size() > kMaxComponentRows || !p->region_ok()) {
        p->component_prepare_(a, r);
        return;
    }
    p->mode_ = mode;
    p->pages_ = pages;
    p->page_ = 0;
    p->rows_ = ours;
    p->prepared_rows_ = unsigned(ours.size());
    p->game_rows_ = n;
    p->track_laps_now_ = mode == 0 && p->track_laps_ ? p->track_laps_(p->page_track()) : 0;
    for (unsigned k = 0; k < n * kRowBytes; k += 4) b3host::store<std::uint32_t>(kRows + k, b3host::load<std::uint32_t>(kPage + 0x28u + k));
    for (std::size_t i = 0; i < ours.size(); ++i) p->write_row(ours[i], kRows + (n + unsigned(i)) * kRowBytes);
    b3host::store<std::uint16_t>(kPage + 0x26u, std::uint16_t(n + ours.size()));
    b3host::CallArgs b = a;
    b.gpr[2].w[0] = kRows;
    b.gpr[2].w[1] = 0;
    p->component_prepare_(b, r);
    p->shown_ = true;
    for (std::size_t i = 0; i < ours.size(); ++i) p->set_text(n + unsigned(i), ours[i]);
}

// func_00432560 (the page's change): Left / Right on our rows
inline void RaceOptions::page_change(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace race_options_detail;
    RaceOptions* p = instance();
    const bool page = a0(a) == kPage && p->shown_ && b3host::load<std::uint16_t>(kPage + 0x26u) == p->game_rows_ + p->rows_.size();
    const unsigned sel = page ? b3host::load<std::uint16_t>(kPage + 0x24u) : 0u;
    if (!page || sel < p->game_rows_ || sel >= p->game_rows_ + p->rows_.size()) {
        p->page_change_(a, r);
        if (page && p->mode_ == 0 && p->track_laps_) {   // the track may have moved: the LAPS row's default follows it
            const int d = p->track_laps_(p->page_track());
            if (d != p->track_laps_now_) {
                p->track_laps_now_ = d;
                RaceChoice c = p->choice_;
                if (c.laps && (c.laps == d || !p->model().lap_race())) c.laps = 0;
                if (c != p->choice_) { p->choice_ = c; p->changed_ = true; }
                for (std::size_t i = 0; i < p->rows_.size(); ++i)
                    if (p->rows_[i] == RaceRow::kLaps) {
                        p->write_row(RaceRow::kLaps, kRows + (p->game_rows_ + unsigned(i)) * kRowBytes);
                        p->set_text(p->game_rows_ + unsigned(i), RaceRow::kLaps);
                    }
            }
        }
        return;
    }
    const RaceRow row = p->rows_[sel - p->game_rows_];
    const RaceRowModel m = p->model();
    if (row == RaceRow::kPage) {   // optpages: MORE: the other page's rows in place
        const int pages = int(p->pages_.size());
        int k = p->page_;
        if (b3host::call_guest<int>(kGetMenuLeft, kInput, 0) & 0xFF) k = k - 1 < 0 ? pages - 1 : k - 1;
        else if (b3host::call_guest<int>(kGetMenuRight, kInput, 0) & 0xFF) k = k + 1 >= pages ? 0 : k + 1;
        if (k == p->page_ || pages < 2) return;
        p->show_page(k);
        b3host::call_guest<void>(kMenuSound, kSound, 1);
        return;
    }
    const int n = m.count(row), was = m.index(row, p->choice_);
    if (n <= 1) return;   // (a disabled row: nothing to step)
    int i = was;
    if (b3host::call_guest<int>(kGetMenuLeft, kInput, 0) & 0xFF) i = i - 1 < 0 ? n - 1 : i - 1;
    else if (b3host::call_guest<int>(kGetMenuRight, kInput, 0) & 0xFF) i = i + 1 >= n ? 0 : i + 1;
    if (i == was) return;
    m.set(row, i, p->choice_);
    if (row == RaceRow::kRivals)   // ROAD RAGE: the page's own rivals (the game's Continue carries them; 5: its 3; 4 / 5
        // are clamped to 3 by RequestOnePlayerGameMode, then raised in the stage record: stage_cars)
        b3host::store<std::int32_t>(kPage + 0x6C0u, p->choice_.rivals ? std::int32_t(p->choice_.rivals) : kGameRivals);
    p->changed_ = true;
    p->set_text(sel, row);
    b3host::call_guest<void>(kMenuSound, kSound, 1);
}

// optpages: page `k`'s rows in place of the shown ones (the component as prepared: see the header)
inline void RaceOptions::show_page(int k) {
    using namespace race_options_detail;
    page_ = k;
    rows_ = race_page_rows(pages_, k);
    const unsigned n = game_rows_, shown = unsigned(rows_.size());
    b3host::store<std::uint16_t>(kBlank, 0);
    b3host::store<std::uint32_t>(kBlankPtrs, kBlank);
    for (unsigned i = 0; i < prepared_rows_; ++i) {
        const b3host::gaddr e = kRows + (n + i) * kRowBytes;
        if (i < shown) {
            write_row(rows_[i], e);
            continue;
        }
        b3host::store<std::uint32_t>(e + 0x0u, kBlank);   // a blank row (past the count: never selected)
        b3host::store<std::uint32_t>(e + 0x4u, kBlank);
        b3host::store<std::uint32_t>(e + 0x8u, kBlankPtrs);
        b3host::store<std::uint8_t>(e + 0xCu, 1);
        b3host::store<std::uint8_t>(e + 0xDu, kKindStrings);
        b3host::store<std::uint8_t>(e + 0xEu, 1);
        b3host::store<std::uint8_t>(e + 0xFu, 0);
        b3host::store<std::uint32_t>(e + 0x10u, 0);
    }
    b3host::store<std::uint16_t>(kPage + 0x26u, std::uint16_t(n + shown));
    b3host::store<std::uint16_t>(kPage + 0x24u, std::uint16_t(n + shown - 1));   // the selection stays on MORE
    const RaceRowModel m = model();
    const b3host::gaddr label0 = b3host::load<std::uint32_t>(kComponent + 0x14u) & 0x01FFFFFFu;
    const float x = label0 ? b3host::load<float>(label0) : 0.0f;
    const std::int32_t lit = b3host::load<std::int32_t>(kComponent + 0xDCu);
    for (unsigned i = 0; i < prepared_rows_; ++i) {
        const int row = int(n + i);
        if (i >= shown) continue;   // (blank: blank_rows below)
        b3host::call_guest<void>(kLabelPrepare, kComponent, row, x, lit == row ? 1 : 0);   // (at the rows' x again)
        b3host::call_guest<void>(kRowRefresh, kComponent, row, !m.disabled(rows_[i]) ? 1 : 0);
        b3host::call_guest<void>(kSetOptionText, kComponent, row, m.index(rows_[i], choice_));
    }
    blank_rows();   // (and after the component's Updates while the page is short: component_update)
    std::printf("race_options: SELECT OPTIONS page %d / %d:", k + 1, int(pages_.size()));
    for (const RaceRow r : rows_) if (r != RaceRow::kPage) std::printf(" %s;", race_row_label(r));
    std::printf("\n");
    std::fflush(stdout);
}

// optpages: the rows past the shown page's end off the screen (past the count: never reached)
inline void RaceOptions::blank_rows() {
    using namespace race_options_detail;
    for (unsigned i = unsigned(rows_.size()); i < prepared_rows_; ++i) {
        const int row = int(game_rows_ + i);
        b3host::call_guest<void>(kLabelPrepare, kComponent, row, kOffScreen, 0);
        b3host::call_guest<void>(kValuePrepare, kComponent, row, 0, kOffScreen);
    }
}
// func_003BC7F0 (the options component's Update): after it, a short page's blank rows off the screen again if the Update
// re-made one
inline void RaceOptions::component_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace race_options_detail;
    RaceOptions* p = instance();
    p->component_update_(a, r);
    if (a0(a) != kComponent || !p->shown_ || p->rows_.size() >= p->prepared_rows_) return;
    for (unsigned i = unsigned(p->rows_.size()); i < p->prepared_rows_; ++i) {
        const b3host::gaddr label = b3host::load<std::uint32_t>(kComponent + 0x14u + 4u * (p->game_rows_ + i)) & 0x01FFFFFFu;
        if (label && b3host::load<float>(label) != kOffScreen) {
            ++p->counts_.rows_rehidden;
            std::printf("race_options: SELECT OPTIONS row %u re-made by the component's Update: off the screen again (%llu so far)\n",
                        p->game_rows_ + i, static_cast<unsigned long long>(p->counts_.rows_rehidden));
            std::fflush(stdout);
            p->blank_rows();
            return;
        }
    }
}

// func_00433230 (the page's reset, entering SELECT MODE): ours with the page's
inline void RaceOptions::page_reset(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace race_options_detail;
    RaceOptions* p = instance();
    p->page_reset_(a, r);
    if (a0(a) == kPage) p->reset();
}

// 0x134930 (CB3OnePlayerStage's lap count): the chosen laps in a SINGLE EVENT race of a lap track
inline void RaceOptions::stage_laps(const b3host::CallArgs& a, b3host::CallResult& r) {
    RaceOptions* p = instance();
    p->stage_laps_(a, r);
    if (p->choice_.laps == 0 || !p->race_active() || p->eliminator()) return;   // (elim1: the Eliminator's laps are the game's)
    const int game = std::int32_t(r.v0.w[0]);
    const int laps = race_laps(p->choice_, p->mode_, game);
    if (laps == 0) return;
    if (p->counts_.laps_given == 0 || p->counts_.laps != laps || p->counts_.track_laps != game) {
        std::printf("race_options: the race's laps %d (the track's %d)\n", laps, game);
        std::fflush(stdout);
    }
    ++p->counts_.laps_given;
    p->counts_.laps = laps;
    p->counts_.track_laps = game;
    r.v0.w[0] = std::uint32_t(laps);
    r.v0.w[1] = 0;
}

// a traffic valve's rate reader (a0 the valve): the valve lanes' rates scaled for the call, put back after it
inline void RaceOptions::valve_rates(const b3host::CallArgs& a, b3host::CallResult& r, Entry next) {
    using namespace race_options_detail;
    RaceOptions* p = instance();
    if (p->choice_.traffic == 2 || !p->race_active()) {
        next(a, r);
        return;
    }
    // The rates as they were, put back when this scope ends however it ends (a guest fault unwinding through the call
    // included): the track's data must never keep a scaled value
    struct Scaled {
        b3host::gaddr rates[kValveLanes] = {};
        std::uint32_t saved[kValveLanes][kRates] = {};
        unsigned n = 0;
        Scaled() = default;
        Scaled(const Scaled&) = delete;
        Scaled& operator=(const Scaled&) = delete;
        ~Scaled() {
            for (unsigned j = 0; j < n; ++j)
                for (unsigned k = 0; k < kRates; ++k) b3host::store<std::uint32_t>(rates[j] + 4u * k, saved[j][k]);
        }
    } s;
    const float scale = race_traffic_scale(p->choice_.traffic);
    const b3host::gaddr valve = a0(a), info = b3host::load<std::uint32_t>(valve + 0x40u) & 0x01FFFFFFu;
    const std::int32_t lanes = info ? b3host::load<std::int32_t>(info + 8u) : 0;
    for (std::int32_t i = 0; i < lanes && i < std::int32_t(kValveLanes); ++i) {
        const b3host::gaddr q = b3host::load<std::uint32_t>(valve + 0x10u + 4u * unsigned(i)) & 0x01FFFFFFu;
        if (q == 0) continue;
        bool seen = false;
        for (unsigned k = 0; k < s.n; ++k) seen = seen || s.rates[k] == q;
        if (seen) continue;
        s.rates[s.n] = q;
        for (unsigned k = 0; k < kRates; ++k) s.saved[s.n][k] = b3host::load<std::uint32_t>(q + 4u * k);
        ++s.n;   // (counted before the stores: a fault in them still restores this lane)
        for (unsigned k = 0; k < kRates; ++k) b3host::store<float>(q + 4u * k, b3host::load<float>(q + 4u * k) * scale);
    }
    if (s.n) ++p->counts_.traffic_scaled;
    next(a, r);
}

// CB3Records::Update 0x3D1090(records, the record kinds, a flag) [C func_003D1090: a1 the kinds (0 returns at once;
// 0x30 / 0x10 pick the tables), a2 only gates a second update; func_0042F0F0 passes (&D_01EA7810, +0x1C, +0xD5)]: no
// kinds with the race's gameplay options. Then as a race that set no record leaves it (as the Eliminator does): the
// update type "track" (0) and the race's track (the stage record's +0), so the results page's RECORD tags
// (func_00452ED0: GetTrackRecords(D_004E2EE8), the entries' new-record marks, cleared by the call) read this track's
// tables and show none (kinds 0 return before setting them: the tags would read a stale track)
inline void RaceOptions::records(const b3host::CallArgs& a, b3host::CallResult& r) {
    RaceOptions* p = instance();
    if (p->choice_.gameplay_default() || !p->race_active()) {
        p->records_(a, r);
        return;
    }
    const std::uint64_t kinds = records_without_kinds(p->records_, a, r);   // records_guard.h
    ++p->counts_.records_skipped;
    if (kinds == 0) return;   // (another guard took them first: elim1's)
    std::printf("race_options: the race's options (%s): the record kinds %llu not written (no records kept)\n",
                p->choice_.code().c_str(), static_cast<unsigned long long>(kinds));
    std::fflush(stdout);
}

// CB3MenuChoices::RequestOnePlayerGameMode 0x30C2C0 (this CB3MenuChoices 0x01E900B0) builds the stage record 0x0051AD00
// [C B3MenuChoices_0030C2C0.cpp]: ROAD RAGE gets min(rivals, 3) AI cars (+0xA4), their car ids +0x18 (u64[5]) and colours
// +0x78 (s32[5]) from the choices' +0x80 / +0xC0 (slot 0 the player's). For ROAD RAGE's 4 / 5 rivals: the record raised
// after the call, the extra cars' ids and colours taken from the same slots as RACE's 4th / 5th rival (the car select
// fills all five for Single Event); a slot without a car id leaves the game's 3.
inline void RaceOptions::stage_cars(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace race_options_detail;
    RaceOptions* p = instance();
    if (a0(a) == kMenuChoices && b3host::load<std::int32_t>(kMenuChoices + 4u) == 1 && p->choice_.cars != 0 &&
        b3host::load<std::int32_t>(kMenuChoices + 8u) == p->mode_ && (p->mode_ == 0 || p->mode_ == 2))
        p->pick_rival_cars();   // options3: before the game copies them
    p->stage_cars_(a, r);
    const int want = p->choice_.rivals;
    if (a0(a) != kMenuChoices || p->mode_ != 2 || want <= kRoadRageRivals || b3host::load<std::int32_t>(kMenuChoices + 4u) != 1 ||
        b3host::load<std::int32_t>(kMenuChoices + 8u) != 2)
        return;
    const b3host::gaddr rec = kOnePlayerStage;
    const int have = b3host::load<std::int32_t>(rec + 0xA4u);
    if (have != kRoadRageRivals || want > kRoadRageMaxRivals) return;
    for (int i = have; i < want; ++i)
        if (b3host::load<std::uint64_t>(kMenuChoices + 0x80u + 8u * unsigned(i + 1)) == 0) {
            std::printf("race_options: ROAD RAGE rivals %d: no car for rival %d in the choices: the game's %d\n", want, i + 1, have);
            std::fflush(stdout);
            return;
        }
    std::string ids;
    for (int i = have; i < want; ++i) {
        const std::uint64_t id = b3host::load<std::uint64_t>(kMenuChoices + 0x80u + 8u * unsigned(i + 1));
        b3host::store<std::uint64_t>(rec + 0x18u + 8u * unsigned(i), id);
        b3host::store<std::int32_t>(rec + 0x78u + 4u * unsigned(i), b3host::load<std::int32_t>(kMenuChoices + 0xC0u + 4u * unsigned(i + 1)));
        char b[48];
        std::snprintf(b, sizeof b, "%s%016llx", ids.empty() ? "" : ", ", static_cast<unsigned long long>(id));
        ids += b;
    }
    b3host::store<std::int32_t>(rec + 0xA4u, want);
    std::printf("race_options: ROAD RAGE with %d rivals (the game's %d; the cars added: %s)\n", want, have, ids.c_str());
    std::fflush(stdout);
}

// options3: func_00135170 (rolling start 1): standing (0) with START Standing
inline void RaceOptions::rolling_start(const b3host::CallArgs& a, b3host::CallResult& r) {
    RaceOptions* p = instance();
    p->rolling_start_(a, r);
    if (p->choice_.start && p->race_active()) {
        r.v0 = u_long128{};
        if (!p->told_start_) { p->told_start_ = true; std::printf("race_options: a standing start (START Standing)\n"); std::fflush(stdout); }
    }
}
// options3: CB3StageLogic::GetAIMode (a race's 0): the Road Rage AI (1) with RACE's RIVAL STYLE Road Rage
inline void RaceOptions::ai_mode(const b3host::CallArgs& a, b3host::CallResult& r) {
    RaceOptions* p = instance();
    p->ai_mode_(a, r);
    if (p->choice_.style && p->mode_ == 0 && p->race_active()) {
        r.v0 = u_long128{};
        r.v0.w[0] = 1;
        if (!p->told_style_) { p->told_style_ = true; std::printf("race_options: the rivals drive as Road Rage's (RIVAL STYLE)\n"); std::fflush(stdout); }
    }
}
// options3: CB3OfflineRoadRageLogic::GetScoreModeFlags (2): with bit 0, the race's Burnout points too (BURNOUT POINTS All)
inline void RaceOptions::rr_score_flags(const b3host::CallArgs& a, b3host::CallResult& r) {
    RaceOptions* p = instance();
    p->rr_score_flags_(a, r);
    if (p->choice_.points && p->mode_ == 2 && p->race_active()) {
        r.v0.w[0] |= 1u;
        if (!p->told_points_) { p->told_points_ = true; std::printf("race_options: ROAD RAGE gives a race's Burnout points (BURNOUT POINTS All)\n"); std::fflush(stdout); }
    }
}
// options3: CB3OfflineRoadRageLogic::OnWorldPrepared: the players' time limit (CB3Score +0x2D0) with TIME LIMIT
inline void RaceOptions::rr_world_prepared(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace race_options_detail;
    RaceOptions* p = instance();
    p->rr_world_prepared_(a, r);
    if (!p->choice_.limit || p->mode_ != 2 || !p->race_active()) return;
    const int sec = race_time_limit_seconds(p->choice_.limit);
    const std::int32_t players = std::clamp(b3host::load<std::int32_t>(kWorldPlayers), 0, 2);
    for (std::int32_t j = 0; j < players; ++j)
        b3host::store<float>(kPlayerCar0 + kPlayerCarStride * unsigned(j) + kCarTimeLimit, float(sec));
    std::printf("race_options: ROAD RAGE time limit %d s (%d player%s)\n", sec, players, players == 1 ? "" : "s");
    std::fflush(stdout);
}
// options3: CB3HUDManager::PrepareNormalComponents, and IsInChampionship inside it: World Tour's timed Road Rage HUD (the
// timer instead of the score list) for a Single Event Road Rage with TIME LIMIT
inline void RaceOptions::hud_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    RaceOptions* p = instance();
    const bool was = p->in_hud_prepare_;
    p->in_hud_prepare_ = true;
    p->hud_prepare_(a, r);
    p->in_hud_prepare_ = was;
}
// CB3Game::IsInChampionship 0x131CE0, answered yes only inside CB3HUDManager::PrepareNormalComponents 0x1A0AF0 in a ROAD
// RAGE race with a TIME LIMIT. Its uses there [C B3HUDManager_001A0AF0.cpp]: the Road Rage HUD (case 7) prepares
// _gTimerComponentP1 when it answers yes, and the lap-timer HUD (cases 4 / 5, Time Attack's) passes it to the lap timer
// component (func_00394870's last argument). The switch prepares one HUD type per race, so in a Road Rage race only the
// first is reached; outside PrepareNormalComponents (World Tour's own rules, results) it answers as the game's.
inline void RaceOptions::in_championship(const b3host::CallArgs& a, b3host::CallResult& r) {
    RaceOptions* p = instance();
    p->in_championship_(a, r);
    if (p->in_hud_prepare_ && p->choice_.limit && p->mode_ == 2 && p->race_active()) {
        r.v0 = u_long128{};
        r.v0.w[0] = 1;
        if (!p->told_hud_) { p->told_hud_ = true; std::printf("race_options: ROAD RAGE's HUD with the timer (TIME LIMIT)\n"); std::fflush(stdout); }
    }
}

// options3: RIVALS' CARS -- the rivals' cars picked again in CB3MenuChoices (the game's picker with the chosen class, or the
// player's car for each)
inline void RaceOptions::pick_rival_cars() {
    using namespace race_options_detail;
    const int cls = race_rival_class(choice_.cars);
    if (cls == -1) return;
    const std::int32_t keep_from = b3host::load<std::int32_t>(kMenuChoices + 0x30u), keep_n = b3host::load<std::int32_t>(kMenuChoices + 0x18u);
    b3host::store<std::int32_t>(kMenuChoices + 0x30u, 1);
    b3host::store<std::int32_t>(kMenuChoices + 0x18u, kRoadRageMaxRivals);
    std::string classes;   // mixedrivals: each slot's class, for the log
    if (cls == kRivalClassMixed) {
        const std::vector<int> pool = mixed_rival_classes();
        picking_rivals() = true;
        for (unsigned i = 1; i <= unsigned(kRoadRageMaxRivals); ++i) {
            // (none unlocked anywhere cannot happen -- the player's own car is -- but then the player's class, as the game)
            const int c = pool.empty() ? -1 : pool[game_random() % unsigned(pool.size())];
            b3host::store<std::int32_t>(kMenuChoices + 0x30u, std::int32_t(i));
            b3host::store<std::int32_t>(kMenuChoices + 0x18u, 1);
            if (c >= 0) b3host::call_guest<void>(kPickRivals, kMenuChoices, c);
            classes += (classes.empty() ? "" : " ") + std::to_string(c);
        }
        picking_rivals() = false;
        std::string avail;
        for (int c : pool) avail += (avail.empty() ? "" : " ") + std::to_string(c);
        classes = "; classes " + classes + " (from " + avail + ")";
    } else if (cls >= 0) {
        picking_rivals() = true;
        b3host::call_guest<void>(kPickRivals, kMenuChoices, cls);
        picking_rivals() = false;
    } else {
        const std::uint64_t mine = b3host::load<std::uint64_t>(kMenuChoices + 0x80u);
        const std::int32_t colour = b3host::load<std::int32_t>(kMenuChoices + 0xC0u);
        for (unsigned i = 1; i <= unsigned(kRoadRageMaxRivals); ++i) {
            b3host::store<std::uint64_t>(kMenuChoices + 0x80u + 8u * i, mine);
            b3host::store<std::int32_t>(kMenuChoices + 0xC0u + 4u * i, std::int32_t((colour + std::int32_t(i)) % 3));
        }
    }
    b3host::store<std::int32_t>(kMenuChoices + 0x30u, keep_from);
    b3host::store<std::int32_t>(kMenuChoices + 0x18u, keep_n);
    std::string ids;
    for (unsigned i = 1; i <= unsigned(kRoadRageMaxRivals); ++i) {
        char b[48];
        std::snprintf(b, sizeof b, "%s%016llx/%d", ids.empty() ? "" : ", ",
                      static_cast<unsigned long long>(b3host::load<std::uint64_t>(kMenuChoices + 0x80u + 8u * i)),
                      b3host::load<std::int32_t>(kMenuChoices + 0xC0u + 4u * i));
        ids += b;
    }
    std::printf("race_options: RIVALS' CARS %s: %s%s\n", race_words("").cars[choice_.cars], ids.c_str(), classes.c_str());
    std::fflush(stdout);
}

// mixedrivals: the game's next random number (CGtRandom::Random on D_00518680, inlined in retail: rotate, add)
inline std::uint32_t RaceOptions::game_random() {
    using namespace race_options_detail;
    std::uint32_t hi = b3host::load<std::uint32_t>(kGameRandom), lo = b3host::load<std::uint32_t>(kGameRandom + 4u);
    hi = (hi << 16) + (hi >> 16);
    hi += lo;
    lo += hi;
    b3host::store<std::uint32_t>(kGameRandom, hi);
    b3host::store<std::uint32_t>(kGameRandom + 4u, lo);
    return hi;
}

// mixedrivals: the classes with an unlocked car in the race vehicle list (func_0030B6E0's first pass), in class order
inline std::vector<int> RaceOptions::mixed_rival_classes() {
    using namespace race_options_detail;
    static constexpr int kClasses[] = {0, 1, 2, 3, 4, 6};   // Compact, Muscle, Coupe, Sports, Super, Heavyweights (EB3MenuCarClasses)
    bool have[7] = {};
    const b3host::gaddr profile = b3host::load<std::uint32_t>(kProfilePtr) & 0x01FFFFFFu;
    for (int i = 0; i < kRaceVehicles; ++i) {
        const std::uint64_t id = b3host::call_guest<std::uint64_t>(kRaceVehicleId, i);
        if (!b3host::call_guest<bool>(kVehicleExists, kVehicleList, id)) continue;
        const int c = b3host::call_guest<int>(kVehicleClass, id);
        if (c < 0 || c > 6 || c == 5 || have[c]) continue;
        if (b3host::call_guest<bool>(kCarUnlocked, profile, id)) have[c] = true;
    }
    std::vector<int> out;
    for (int c : kClasses)
        if (have[c]) out.push_back(c);
    return out;
}

// options3: CB3TrafficSystem::ResetMemberVariables: TRAFFIC PATTERN Varied's seed (splitmix64 of the VBlank; never 0)
inline void RaceOptions::traffic_reset(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace race_options_detail;
    RaceOptions* p = instance();
    p->traffic_reset_(a, r);
    if (!p->choice_.seed || !p->race_active() || (a0(a) + 0x36348u) != kTrafficSeedA) return;
    // (the request only: the playback flag stays set after a replay until the world's reset, and a RETRY from there is a
    // new race with a new seed)
    if (b3host::load<std::uint8_t>(kReplayRequested) != 0 && p->seed_set_) {   // replayview: the game's replay of the race: its seed again
        b3host::store<std::uint32_t>(kTrafficSeedA, p->seed_a_);
        b3host::store<std::uint32_t>(kTrafficSeedB, p->seed_b_);
        std::printf("race_options: TRAFFIC PATTERN Varied: the replay takes the race's seed %08X %08X\n", p->seed_a_, p->seed_b_);
        std::fflush(stdout);
        return;
    }
    std::uint64_t z = (p->clock_ ? p->clock_() : 0) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    const std::uint32_t sa = std::uint32_t(z) | 1u, sb = std::uint32_t(z >> 32) | 1u;
    b3host::store<std::uint32_t>(kTrafficSeedA, sa);
    b3host::store<std::uint32_t>(kTrafficSeedB, sb);
    p->seed_a_ = sa, p->seed_b_ = sb, p->seed_set_ = true;
    std::printf("race_options: TRAFFIC PATTERN Varied: seed %08X %08X\n", sa, sb);
    std::fflush(stdout);
}

// TEST ONLY: CB3Score::Update(score) -- after the game's, player 0's lap completed by the game's IncrementLapCount every
// test_finish_ updates while it races (+0x27C 2, +0x8 no finish yet, +0x14 past the start line)
inline void RaceOptions::score_update(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace race_options_detail;
    RaceOptions* p = instance();
    p->score_update_(a, r);
    const b3host::gaddr s = a0(a);
    static const bool any_race = std::getenv("B3_TEST_FINISH_ANY") != nullptr;   // replayview checks: any race (World Tour too)
    if (s != kPlayerScore || (!p->race_active() && !any_race)) return;
    if (b3host::load<std::int32_t>(s + 0x27Cu) != 2 || b3host::load<std::int32_t>(s + 0x8u) != 0) return;
    if (p->mode_ != 2 && b3host::load<std::int32_t>(s + 0x14u) < 0) return;
    if (++p->test_ticks_ % p->test_finish_ != 0) return;
    if (p->mode_ == 2) {   // ROAD RAGE ends on the player's wreck: its SetFinished(car, 4) [C RaceCarKilled 0x38C120]
        b3host::call_guest<void>(kSetFinished, b3host::load<std::uint32_t>(s + 0xC8u), 4);
        std::printf("race_options: TEST: ROAD RAGE finished by the test (SetFinished 4; finish state %d)\n", b3host::load<std::int32_t>(s + 0x8u));
        std::fflush(stdout);
        return;
    }
    b3host::call_guest<void>(kIncrementLap, kPlayerScore);
    std::printf("race_options: TEST: the player's lap %d completed by the test (laps %d, finish state %d)\n",
                b3host::load<std::int32_t>(s + 0x14u), b3host::load<std::int32_t>(s + 0x2C4u), b3host::load<std::int32_t>(s + 0x8u));
    std::fflush(stdout);
}

// 0x136F30 (whether the race counts for the profile): not with the race's gameplay options
inline void RaceOptions::profile_counts(const b3host::CallArgs& a, b3host::CallResult& r) {
    RaceOptions* p = instance();
    p->profile_counts_(a, r);
    if (p->choice_.gameplay_default() || !p->race_active() || (r.v0.w[0] & 0xFFu) == 0) return;
    if (p->counts_.profile_skipped++ == 0) {
        std::printf("race_options: the race's options (%s): the profile's totals and unlocks left as they were\n", p->choice_.code().c_str());
        std::fflush(stdout);
    }
    r.v0 = u_long128{};
}

}  // namespace b3native

#endif
