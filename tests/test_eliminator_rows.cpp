// elim1: ELIMINATOR in SINGLE EVENT's SELECT MODE, the pure part (tools/native/eliminator_rows.h): the page's rows with
// the row added, and which of the game's modes each row is.
#include "test.h"

#include "eliminator_rows.h"
#include "records.h"
#include "race_choice.h"

using b3native::EliminatorRows;

TEST(only_the_games_own_list_gets_the_row) {
    std::uint32_t rows[4] = {351, 353, 352, 357};
    CHECK(EliminatorRows::is_game_list(350, 4, rows));
    CHECK(!EliminatorRows::is_game_list(350, 5, rows));   // already ours
    CHECK(!EliminatorRows::is_game_list(349, 4, rows));   // another page's title
    rows[2] = 354;
    CHECK(!EliminatorRows::is_game_list(350, 4, rows));
}

TEST(eliminator_between_road_rage_and_crash) {
    std::uint32_t rows[10] = {351, 353, 352, 357};
    std::uint8_t a[10] = {0, 0, 0, 1}, b[10] = {0, 0, 0, 2};
    EliminatorRows::build(rows, a, b);
    CHECK(rows[0] == 351 && rows[1] == 353 && rows[2] == 352 && rows[3] == 273 && rows[4] == 357);
    CHECK(a[3] == 0 && b[3] == 0 && a[4] == 1 && b[4] == 2);   // CRASH's flags move with it
}

TEST(rows_and_modes_round_trip) {
    for (unsigned row = 0; row < EliminatorRows::kCount; ++row) {
        bool elim = false;
        const unsigned game = EliminatorRows::game_row_of(row, elim);
        CHECK(game < 4);
        CHECK(elim == (row == 3));
        CHECK(EliminatorRows::row_of(game, elim) == row);
    }
    CHECK(EliminatorRows::row_of(0, false) == 0);   // RACE
    CHECK(EliminatorRows::row_of(3, true) == 4);    // CRASH whatever the flag
}

TEST(eliminator_needs_a_rival) {
    CHECK(EliminatorRows::rivals(0) == 1);
    CHECK(EliminatorRows::rivals(1) == 1);
    CHECK(EliminatorRows::rivals(5) == 5);
    CHECK(EliminatorRows::rivals(9) == 5);
}

TEST(eliminator_records_kept_apart) {
    using b3replay::records::options_folder;
    CHECK(options_folder(EliminatorRows::record_word()) == "options-eliminator");   // TRAFFIC Normal: no traffic part
    CHECK(options_folder(EliminatorRows::record_word(0x8000u | 0u | 5u << 2)) == "options-eliminator-traffic-off-laps-5");
    CHECK(options_folder(EliminatorRows::record_word(0x8000u)) == "options-eliminator-traffic-off");   // raceopts' TRAFFIC Off
    CHECK(options_folder(0) == "");
    b3replay::records::Run r;
    r.race_options = EliminatorRows::record_word();
    CHECK(b3replay::records::folder("ghosts", r) != "ghosts");
}

TEST(timed_elimination_every_interval) {
    b3native::TimedElimination t;
    auto st = t.step(false, 0.0f, 6);   // the countdown before the start: nothing
    CHECK(!st.eliminate && st.seconds_left == -1);
    st = t.step(true, 0.0f, 6);
    CHECK(!st.eliminate && st.seconds_left == 24);
    st = t.step(true, 23.5f, 6);
    CHECK(!st.eliminate && st.seconds_left == 1);
    st = t.step(true, 24.0f, 6);
    CHECK(st.eliminate && st.seconds_left == 24);   // out at 24 s, the next at 48 s
    st = t.step(true, 47.99f, 5);
    CHECK(!st.eliminate && st.seconds_left == 1);
    st = t.step(true, 48.0f, 5);
    CHECK(st.eliminate);
    st = t.step(true, 72.0f, 1);   // one car left: the game ends the race, no more
    CHECK(!st.eliminate && st.seconds_left == -1);
    t.reset();
    CHECK(!t.started);
}

TEST(timed_elimination_takes_the_last_car_still_in) {
    const int pos[6] = {6, 4, 5, 3, 2, 1};
    bool in[6] = {true, true, true, true, true, true};
    CHECK(b3native::TimedElimination::last_car(pos, in, 6) == 0);
    in[0] = false;   // the player out: the rival in 5th
    CHECK(b3native::TimedElimination::last_car(pos, in, 6) == 2);
    bool none[6] = {};
    CHECK(b3native::TimedElimination::last_car(pos, none, 6) == -1);
}

TEST(timed_elimination_choice_code_and_records) {
    b3native::RaceChoice c;
    CHECK(c.code() == "t2 l0 r0 d0");   // (Off: the code as before, old replays' entries the same)
    c.timed = 1;
    CHECK(c.code() == "t2 l0 r0 d0 e1" && !c.gameplay_default());
    b3native::RaceChoice d;
    CHECK(b3native::RaceChoice::parse("t2 l0 r0 d0 e1", d) && d.timed == 1);
    CHECK(b3native::RaceChoice::parse("t0 l3 r0 d0", d) && d.timed == 0 && d.traffic == 0 && d.laps == 3);
    CHECK(!b3native::RaceChoice::parse("t2 l0 r0 d0 e2", d));
    CHECK(b3replay::records::options_folder(EliminatorRows::record_word(c.record_word())) == "options-eliminator-timed");
    b3native::RaceRowModel m;
    CHECK(m.count(b3native::RaceRow::kTimed) == 2 && m.text(b3native::RaceRow::kTimed, 0) == "Off (Default)" &&
          m.text(b3native::RaceRow::kTimed, 1) == "On");
    b3native::RaceChoice e;
    m.set(b3native::RaceRow::kTimed, 1, e);
    CHECK(e.timed == 1 && m.index(b3native::RaceRow::kTimed, e) == 1);
}

TEST(transmission_manual_choice_code_and_records) {   // gearbox
    b3native::RaceChoice c;
    c.manual = 1;
    CHECK(c.code() == "t2 l0 r0 d0 m1" && !c.gameplay_default());
    c.timed = 1;
    CHECK(c.code() == "t2 l0 r0 d0 e1 m1");
    b3native::RaceChoice d;
    CHECK(b3native::RaceChoice::parse("t2 l0 r0 d0 e1 m1", d) && d.timed == 1 && d.manual == 1);
    CHECK(b3native::RaceChoice::parse("t0 l0 r0 d0 m1", d) && d.manual == 1 && d.timed == 0 && d.traffic == 0);
    CHECK(!b3native::RaceChoice::parse("t2 l0 r0 d0 m1 e1", d));   // (the tags' order)
    CHECK(!b3native::RaceChoice::parse("t2 l0 r0 d0 m2", d));
    b3native::RaceChoice m;
    m.manual = 1;
    CHECK(b3replay::records::options_folder(m.record_word()) == "options-manual");
    CHECK(b3replay::records::options_folder(EliminatorRows::record_word(m.record_word())) == "options-eliminator-manual");
    b3native::RaceRowModel rm;
    CHECK(rm.count(b3native::RaceRow::kGearbox) == 2 && rm.text(b3native::RaceRow::kGearbox, 0) == "Automatic (Default)" &&
          rm.text(b3native::RaceRow::kGearbox, 1) == "Manual");
    b3native::RaceChoice e;
    rm.set(b3native::RaceRow::kGearbox, 1, e);
    CHECK(e.manual == 1 && rm.index(b3native::RaceRow::kGearbox, e) == 1);
}

TEST_MAIN("ELIMINATOR in SINGLE EVENT's SELECT MODE (elim1)")
