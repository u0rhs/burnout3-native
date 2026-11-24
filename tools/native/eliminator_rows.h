// eliminator_rows.h -- elim1 (2026-11-21): ELIMINATOR in SINGLE EVENT's SELECT MODE, the pure part (the guest glue is
// single_eliminator.h): the mode list with the row added, and which of the game's modes a row is.
//
// The game's SELECT MODE page [C func_003C9210, the decomp]: a CB3GenericVSelectPage (up to 10 rows) with the title
// string 350 (SELECT MODE) and four rows, strings 351 RACE, 353 TIME ATTACK, 352 ROAD RAGE, 357 CRASH; the row index
// is the game's Single Event mode (CB3MenuChoices +8: 0 race, 1 time attack, 2 road rage, 3 crash). ELIMINATOR (string
// 273, World Tour's event name, in every language's table) goes between ROAD RAGE and CRASH: row 3, CRASH row 4. The
// game itself only ever sees its four modes: the ELIMINATOR row is mode 0 (race) with the port's flag set, CRASH's row
// is shown to the game as 3.
#ifndef B3NATIVE_ELIMINATOR_ROWS_H
#define B3NATIVE_ELIMINATOR_ROWS_H

#include <cstdint>

namespace b3native {

struct EliminatorRows {
    static constexpr std::uint32_t kTitle = 350;                       // SELECT MODE
    static constexpr std::uint32_t kGameRows[4] = {351, 353, 352, 357};   // RACE, TIME ATTACK, ROAD RAGE, CRASH
    static constexpr std::uint32_t kEliminator = 273;                  // ELIMINATOR (World Tour's event name)
    static constexpr std::uint32_t kEliminatorHelp = 282;              // LAST PLACE IN EACH LAP IS ELIMINATED
    static constexpr unsigned kRow = 3, kGameCount = 4, kCount = 5, kModeRace = 0, kModeCrash = 3;

    // The page as the game built it (title, count, rows): only then is the row added
    static bool is_game_list(std::uint32_t title, unsigned count, const std::uint32_t* rows) {
        if (title != kTitle || count != kGameCount) return false;
        for (unsigned i = 0; i < kGameCount; ++i)
            if (rows[i] != kGameRows[i]) return false;
        return true;
    }
    // The five rows; flags (disabled, two byte arrays of the page) move with their rows, ELIMINATOR's enabled
    static void build(std::uint32_t* rows, std::uint8_t* flags_a, std::uint8_t* flags_b) {
        rows[4] = rows[3];
        rows[3] = kEliminator;
        flags_a[4] = flags_a[3];
        flags_a[3] = 0;
        flags_b[4] = flags_b[3];
        flags_b[3] = 0;
    }
    // The row the page opens on, from the game's mode (+8 as func_003C9210 set it) and the port's flag
    static unsigned row_of(unsigned game_row, bool eliminator) {
        if (game_row == kModeCrash) return kCount - 1;
        if (game_row == kModeRace && eliminator) return kRow;
        return game_row;
    }
    // Confirm on a row: the game's row for it, and whether it is ELIMINATOR
    static unsigned game_row_of(unsigned row, bool& eliminator) {
        eliminator = row == kRow;
        if (row == kRow) return kModeRace;
        if (row == kCount - 1) return kModeCrash;
        return row;
    }
    // The AI cars: the RIVALS row's value (0..5); Eliminator's laps are the cars less one (CB3OfflineLapEliminatorLogic::
    // OnWorldPrepared 0x38BE40), so it needs a rival: 0 is raised to 1 (World Tour's Eliminators have 5)
    static unsigned rivals(unsigned chosen) { return chosen < 1 ? 1u : chosen > 5 ? 5u : chosen; }
    // The renderer's record word for an Eliminator (src/b3replay/records.h, the 'HTD1' record's options; raceopts' word
    // with bit 14): its ghosts and best times go to "options-eliminator", never the race's. `options`: raceopts' word of
    // the race (0: the game's own options, i.e. bit 15 and TRAFFIC Normal)
    static constexpr std::uint16_t kRecordBit = 0x4000u;
    static std::uint64_t record_word(std::uint64_t options = 0) {
        return (options ? options : 0x8002u) | kRecordBit;
    }
};

// TIMED ELIMINATION (raceopts' row for an Eliminator, race_choice.h): the last car is out every `interval` seconds of race
// time (the game timer less the start time), not at each lap; the pure part of single_eliminator.h's timer.
struct TimedElimination {
    float interval = 24.0f;
    bool started = false;
    float next_due = 0;
    struct Step {
        bool eliminate = false;   // the last car goes out now
        int seconds_left = -1;    // the countdown to show (whole seconds, rounded up); -1: none
    };
    void reset() { started = false; next_due = 0; }
    // Once per logic update: `racing` the start signal has gone (the score's race state 2 or later), `elapsed` the race
    // time; `cars_left` the cars still in (no elimination with one left: the game ends the race then)
    Step step(bool racing, float elapsed, int cars_left) {
        Step s;
        if (!started) {
            if (!racing) return s;
            started = true;
            next_due = interval;
        }
        if (cars_left < 2) return s;
        if (elapsed >= next_due) {
            s.eliminate = true;
            next_due += interval;
        }
        const float left = next_due - elapsed;
        s.seconds_left = left <= 0 ? 0 : int(left + 0.999f);
        return s;
    }
    // The car to put out: the highest race position (1 = first) among those still in; -1 when none
    static int last_car(const int* position, const bool* in, int n) {
        int best = -1;
        for (int i = 0; i < n; ++i)
            if (in[i] && (best < 0 || position[i] > position[best])) best = i;
        return best;
    }
};

}  // namespace b3native

#endif
