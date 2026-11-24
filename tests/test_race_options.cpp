// raceopts (tools/native/race_choice.h, race_options.h): SELECT OPTIONS' TRAFFIC / LAPS / RIVALS rows -- the rows per mode,
// the values as the page shows and steps them, the laps' limit, when the options reach a race (the lifetime), and the
// records kept apart (the record word, the renderer's folder: src/b3replay/records.h).
#include "test.h"

#include <algorithm>

#include "race_choice.h"
#include "../src/b3replay/records.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using b3native::RaceChoice;
using b3native::RaceRow;
using b3native::RaceRowModel;

TEST(rows_per_mode) {
    // optpages: RACE and ROAD RAGE in two pages with MORE last; TIME ATTACK on one; the rows before options3's first as
    // they were (a recording's presses reach the same rows); every page fits with the page's own rows, page 1 the largest
    using V = std::vector<RaceRow>;
    using R = RaceRow;
    const b3native::RacePages race = b3native::race_pages_for(0, false), ta = b3native::race_pages_for(1, false),
                              rr = b3native::race_pages_for(2, false);
    CHECK(race.size() == 2 && ta.size() == 1 && rr.size() == 2 && b3native::race_pages_for(3, true).empty());   // CRASH: none
    CHECK(b3native::race_page_rows(race, 0) == (V{R::kTraffic, R::kLaps, R::kGearbox, R::kStart, R::kCars, R::kPage}));
    CHECK(b3native::race_page_rows(race, 1) == (V{R::kCheck, R::kStyle, R::kSeed, R::kPage}));
    CHECK(b3native::race_page_rows(ta, 0) == (V{R::kTraffic, R::kGearbox, R::kSeed, R::kStart, R::kCheck}));   // no MORE
    CHECK(b3native::race_page_rows(rr, 0) == (V{R::kRivals, R::kTraffic, R::kGearbox, R::kLimit, R::kPoints, R::kStart, R::kPage}));
    CHECK(b3native::race_page_rows(rr, 1) == (V{R::kCars, R::kSeed, R::kCheck, R::kPage}));
    // TIME OF DAY when on: TIME ATTACK after TRANSMISSION; RACE / ROAD RAGE on the last page
    CHECK(b3native::race_page_rows(b3native::race_pages_for(1, true), 0) == (V{R::kTraffic, R::kGearbox, R::kTime, R::kSeed, R::kStart, R::kCheck}));
    CHECK(b3native::race_page_rows(b3native::race_pages_for(0, true), 1) == (V{R::kCheck, R::kStyle, R::kSeed, R::kTime, R::kPage}));
    CHECK(b3native::race_page_rows(b3native::race_pages_for(2, true), 1) == (V{R::kCars, R::kSeed, R::kCheck, R::kTime, R::kPage}));
    // condrows: WEATHER after TIME OF DAY (or in its place without it); TIME ATTACK's one page fits both exactly
    CHECK(b3native::race_page_rows(b3native::race_pages_for(1, true, true), 0) ==
          (V{R::kTraffic, R::kGearbox, R::kTime, R::kWeather, R::kSeed, R::kStart, R::kCheck}));
    CHECK(b3native::race_page_rows(b3native::race_pages_for(1, false, true), 0) == (V{R::kTraffic, R::kGearbox, R::kWeather, R::kSeed, R::kStart, R::kCheck}));
    CHECK(b3native::race_page_rows(b3native::race_pages_for(0, true, true), 1) == (V{R::kCheck, R::kStyle, R::kSeed, R::kTime, R::kWeather, R::kPage}));
    CHECK(b3native::race_page_rows(b3native::race_pages_for(0, false, true), 1) == (V{R::kCheck, R::kStyle, R::kSeed, R::kWeather, R::kPage}));
    CHECK(b3native::race_page_rows(b3native::race_pages_for(2, true, true), 1) == (V{R::kCars, R::kSeed, R::kCheck, R::kTime, R::kWeather, R::kPage}));
    for (int m = 0; m < 3; ++m)
        for (const bool t : {false, true})
        for (const bool w : {false, true}) {
            CHECK(b3native::race_pages_fit(m, t, w));
            const b3native::RacePages pg = b3native::race_pages_for(m, t, w);
            V all;
            for (const V& v : pg) all.insert(all.end(), v.begin(), v.end());
            for (std::size_t a = 0; a < all.size(); ++a)   // each row once
                for (std::size_t b = a + 1; b < all.size(); ++b) CHECK(all[a] != all[b]);
            const V v = b3native::race_page_rows(pg, 0);
            const V before = m == 0 ? V{R::kTraffic, R::kLaps, R::kGearbox}
                           : m == 1 ? V{R::kTraffic, R::kGearbox} : V{R::kRivals, R::kTraffic, R::kGearbox};
            CHECK(V(v.begin(), v.begin() + std::ptrdiff_t(before.size())) == before);   // never the earlier rows
            CHECK(b3native::race_page_rows(pg, 7) == v);   // (a page out of range: page 1)
        }
    // MORE's values: "1 / 2", "2 / 2" (no default tag), the index the page shown
    RaceRowModel m;
    m.pages = 2;
    m.page = 1;
    CHECK(m.count(R::kPage) == 2 && m.text(R::kPage, 0) == "1 / 2" && m.text(R::kPage, 1) == "2 / 2" && m.index(R::kPage, RaceChoice{}) == 1);
    RaceChoice c;
    m.set(R::kPage, 0, c);
    CHECK(c == RaceChoice{});   // (not a value of the race)
    CHECK(std::string(b3native::race_row_label(R::kPage)) == "MORE" && std::string(b3native::race_row_label(R::kPage, "GE")) == "MEHR");
}

// extras (PC SETTINGS > EXTRAS > SELECT OPTIONS): any subset of the rows, packed again -- every mask of the 13 row kinds,
// every mode, with and without TIME OF DAY: exactly the rows on, in their order, every page fitting the component beside
// the game's rows (MORE on each when two), page 1 the largest; every row on: the pages as before; none: the game's page
TEST(rows_masked_any_subset) {
    using R = b3native::RaceRow;
    using V = std::vector<R>;
    for (int m = 0; m < 4; ++m)
        for (bool t : {false, true})
        for (bool w : {false, true}) {
            const b3native::RacePages full = b3native::race_pages_for(m, t, w);
            CHECK(b3native::race_pages_masked(full, m, 0x1FFFu) == full);   // every row on: unchanged
            if (m == 3) { CHECK(b3native::race_pages_masked(full, m, 0x1FFFu).empty()); continue; }
            V order;
            for (const auto& pg : full) for (R r : pg) order.push_back(r);
            for (std::uint32_t mask = 0; mask < 0x2000u; ++mask) {
                const b3native::RacePages p = b3native::race_pages_masked(full, m, mask);
                V want;
                for (R r : order) if (r == R::kTime || r == R::kWeather || (mask >> unsigned(r) & 1u)) want.push_back(r);
                V got;
                for (const auto& pg : p) got.insert(got.end(), pg.begin(), pg.end());
                if (got != want) { CHECK(false); return; }
                if (want.empty()) { CHECK(p.empty() && b3native::race_page_rows(p, 0).empty()); continue; }
                const std::size_t first = b3native::race_page_rows(p, 0).size();
                for (int k = 0; k < int(p.size()); ++k) {
                    const std::size_t n = b3native::race_page_rows(p, k).size();
                    if (b3native::kRacePageGameRows[m] + int(n) > b3native::kRacePageMaxRows || n > first || p.size() > 2) { CHECK(false); return; }
                }
            }
        }
    // e.g. RACE with TRAFFIC and TRAFFIC CHECK only: one page, no MORE
    const b3native::RacePages few = b3native::race_pages_masked(b3native::race_pages_for(0, false), 0, (1u << 0) | (1u << 12));
    CHECK(few.size() == 1 && b3native::race_page_rows(few, 0) == (V{R::kTraffic, R::kCheck}));
}

// condrows (the owner's, 2026-11-23): EXPERIMENTAL NIGHT gives a TIME OF DAY row (Day / Night), RAIN a WEATHER row
// (Clear / Rain), both: both rows (Night + Rain is night rain), neither: none. One stored value underneath (race_time.h:
// 1 Day, 2 Night, 3 Rain, 4 Night rain; the setting's own pair 0), so recordings and the race are as before.
TEST(conditions_rows_per_toggle) {
    using b3native::RaceChoice;
    using b3native::RaceRow;
    using b3native::RaceRowModel;
    RaceRowModel night;   // NIGHT alone: TIME OF DAY, Day / Night
    CHECK(night.count(RaceRow::kTime) == 2 && std::string(night.label(RaceRow::kTime)) == "TIME OF DAY");
    CHECK(night.text(RaceRow::kTime, 0) == "Day (Default)" && night.text(RaceRow::kTime, 1) == "Night");
    RaceChoice c;
    night.set(RaceRow::kTime, 1, c);
    CHECK(c.time == 2 && night.index(RaceRow::kTime, c) == 1);
    night.set(RaceRow::kTime, 0, c);
    CHECK(c.time == 0);
    RaceRowModel rain;    // RAIN (the WEATHER row shown)
    rain.weather = true;
    CHECK(rain.count(RaceRow::kWeather) == 2 && std::string(rain.label(RaceRow::kWeather)) == "WEATHER");
    CHECK(rain.text(RaceRow::kWeather, 0) == "Clear (Default)" && rain.text(RaceRow::kWeather, 1) == "Rain");
    CHECK(rain.index(RaceRow::kWeather, c) == 0);
    rain.set(RaceRow::kWeather, 1, c);
    CHECK(c.time == 3 && rain.index(RaceRow::kWeather, c) == 1 && rain.index(RaceRow::kTime, c) == 0);
    rain.set(RaceRow::kWeather, 0, c);
    CHECK(c.time == 0);
    // both rows: every combination, set in either order, is the one value; the indexes read it back
    for (int n = 0; n < 2; ++n)
        for (int w = 0; w < 2; ++w)
            for (bool time_first : {false, true}) {
                RaceChoice x;
                x.time = 4;   // (whatever was there before)
                if (time_first) { rain.set(RaceRow::kTime, n, x); rain.set(RaceRow::kWeather, w, x); }
                else { rain.set(RaceRow::kWeather, w, x); rain.set(RaceRow::kTime, n, x); }
                const int want = n == 0 && w == 0 ? 0 : (n ? 2 : 1) + (w ? 2 : 0);
                CHECK(x.time == want && rain.index(RaceRow::kTime, x) == n && rain.index(RaceRow::kWeather, x) == w);
                CHECK(x.gameplay_default() && x.record_word() == 0);   // drawn only: the race counts as any (the owner's)
            }
    // neither: no rows (race_pages_for without them)
    for (int m = 0; m < 3; ++m)
        for (const auto& pg : b3native::race_pages_for(m, false, false))
            for (RaceRow r : pg) CHECK(r != RaceRow::kTime && r != RaceRow::kWeather);
}

// A stored value whose toggle is off falls back at the page's Prepare: the half of a row not shown back to the setting's
// (Day, Clear), no row at all: 0
TEST(conditions_offered_per_combination) {
    using b3native::RaceRowModel;
    for (bool t : {false, true})
        for (bool w : {false, true}) {
            RaceRowModel m;
            m.weather = w;
            for (int v = 0; v <= 4; ++v) {
                const bool n = v == 2 || v == 4, r = v == 3 || v == 4;
                const bool kn = t && n, kr = w && r;
                const int want = v == 0 || (!t && !w) || (!kn && !kr) ? 0 : (kn ? 2 : 1) + (kr ? 2 : 0);
                CHECK(m.offered(v, t, w) == want);
            }
        }
    RaceRowModel m;
    m.weather = true;
    CHECK(m.offered(4, false, true) == 3 && m.offered(4, true, false) == 2 && m.offered(4, true, true) == 4 && m.offered(4, false, false) == 0);
    CHECK(m.offered(1, true, true) == 0);   // (a stored Day is the setting's own: 0)
}

TEST(conditions_rows_words_and_defaults) {
    using b3native::RaceChoice;
    using b3native::RaceRow;
    using b3native::RaceRowModel;
    RaceRowModel m;
    m.weather = true;
    m.night = m.rain = true;   // the settings' own Night and Rain: each row's default, stored 0
    CHECK(m.index(RaceRow::kTime, RaceChoice{}) == 1 && m.index(RaceRow::kWeather, RaceChoice{}) == 1);
    CHECK(m.text(RaceRow::kTime, 1) == "Night (Default)" && m.text(RaceRow::kWeather, 1) == "Rain (Default)" && m.text(RaceRow::kWeather, 0) == "Clear");
    RaceChoice d;
    m.set(RaceRow::kTime, 0, d);
    CHECK(d.time == 3);   // Day, in the rain
    m.set(RaceRow::kTime, 1, d);
    CHECK(d.time == 0);
    RaceChoice p;
    CHECK(RaceChoice::parse("t2 l0 r0 d4", p) && p.time == 4 && !RaceChoice::parse("t2 l0 r0 d5", p));
    RaceChoice q;
    q.time = 4;
    b3native::race_row_default(RaceRow::kWeather, q);
    CHECK(q.time == 2);
    q.time = 3;   // Day in the rain: Day, as a value of its own (0 would be the settings' pair, which may be Night)
    b3native::race_row_default(RaceRow::kWeather, q);
    CHECK(q.time == 1);
    for (std::uint8_t v : {std::uint8_t(0), std::uint8_t(1), std::uint8_t(2)}) {   // no rain half: unchanged
        q.time = v;
        b3native::race_row_default(RaceRow::kWeather, q);
        CHECK(q.time == v);
    }
    RaceRowModel nm;   // the settings' Night: the row's own edit (WEATHER to Clear) gives the same Day / Night
    nm.weather = true;
    nm.night = true;
    RaceChoice dr;
    dr.time = 3;
    nm.set(RaceRow::kWeather, 0, dr);
    CHECK(dr.time == 1);
    RaceRowModel f;
    f.weather = true;
    f.language = "FR";
    CHECK(std::string(f.label(RaceRow::kWeather)) == "M\xC3\x89T\xC3\x89O" && f.text(RaceRow::kWeather, 0) == "D\xC3\xA9gag\xC3\xA9 (d\xC3\xA9" "faut)" &&
          f.text(RaceRow::kWeather, 1) == "Pluie");
    f.language = "SW";
    CHECK(std::string(f.label(RaceRow::kWeather)) == "V\xC3\x84" "DER" && f.text(RaceRow::kWeather, 0) == "Klart (standard)");
    for (const char* code : {"", "FR", "GE", "IT", "SP", "DU", "SW"}) {   // every word fits a string slot (23 UTF-16 units)
        RaceRowModel x;
        x.weather = true;
        x.language = code;
        CHECK(b3native::race_utf16_units(x.label(RaceRow::kTime)) <= 23 && b3native::race_utf16_units(x.label(RaceRow::kWeather)) <= 23);
        for (bool night : {false, true})
            for (bool rain : {false, true}) {   // each value as the default too (its tag)
                x.night = night;
                x.rain = rain;
                for (int i = 0; i < 2; ++i)
                    CHECK(b3native::race_utf16_units(x.text(RaceRow::kTime, i)) <= 23 && b3native::race_utf16_units(x.text(RaceRow::kWeather, i)) <= 23);
            }
    }
}

TEST(traffic_check_row) {
    // trafficcheck: TRAFFIC CHECK Off (the game's) / Revenge / Extreme, in the choice code as " x1" / " x2", the last tag
    // (older codes: none); its records bits are traffic_check_rules.h's, the choice's word only flagged
    RaceRowModel m;
    RaceChoice c;
    CHECK(m.count(RaceRow::kCheck) == 3 && m.index(RaceRow::kCheck, c) == 0 && m.text(RaceRow::kCheck, 0) == "Off (Default)" &&
          m.text(RaceRow::kCheck, 1) == "Revenge" && m.text(RaceRow::kCheck, 2) == "Extreme");
    m.set(RaceRow::kCheck, 2, c);
    CHECK(c.check == 2 && !c.gameplay_default() && c.record_word() == 0x8002u && c.code() == "t2 l0 r0 d0 x2");
    RaceChoice p;
    CHECK(RaceChoice::parse("t2 l0 r0 d0 x2", p) && p == c);
    CHECK(RaceChoice::parse("t3 l5 r0 d0 e1 c3 x1", p) && p.check == 1 && p.timed == 1 && p.cars == 3 && p.traffic == 3 && p.laps == 5);
    CHECK(RaceChoice::parse("t2 l0 r0 d0", p) && p.check == 0 && p.timed == 0);   // a code from before the row
    CHECK(!RaceChoice::parse("t2 l0 r0 d0 x3", p) && !RaceChoice::parse("t2 l0 r0 d0 x1 e1", p));
    RaceChoice e;
    e.timed = 1;
    e.check = 1;
    CHECK(e.code() == "t2 l0 r0 d0 e1 x1" && RaceChoice::parse(e.code(), p) && p == e);
    RaceChoice d;
    m.set(RaceRow::kCheck, 1, d);
    b3native::race_row_default(RaceRow::kCheck, d);
    CHECK(d == RaceChoice());
    m.language = "FR";
    CHECK(m.text(RaceRow::kCheck, 2) == "Extr\xC3\xAAme" && std::string(b3native::race_row_label(RaceRow::kCheck, "FR")) == "BOUSCULADE");
}

TEST(defaults_are_the_games) {
    const RaceChoice c;
    CHECK(c.gameplay_default() && c.record_word() == 0 && c.code() == "t2 l0 r0 d0");
    RaceRowModel m;
    m.track_laps = 3;
    CHECK(m.index(RaceRow::kTraffic, c) == 2 && m.text(RaceRow::kTraffic, 2) == "Normal (Default)");
    CHECK(m.index(RaceRow::kLaps, c) == 2 && m.text(RaceRow::kLaps, 2) == "3 (Default)" && m.text(RaceRow::kLaps, 4) == "5");
    CHECK(m.index(RaceRow::kRivals, c) == 2 && m.text(RaceRow::kRivals, 2) == "3 (Default)");
    CHECK(m.index(RaceRow::kTime, c) == 0 && m.text(RaceRow::kTime, 0) == "Day (Default)" && m.text(RaceRow::kTime, 1) == "Night");
    m.night = true;   // the setting's Night: "Night (Default)"
    CHECK(m.index(RaceRow::kTime, c) == 1 && m.text(RaceRow::kTime, 1) == "Night (Default)" && m.text(RaceRow::kTime, 0) == "Day");
    RaceChoice t = c;
    m.set(RaceRow::kTime, 0, t);   // Day against a Night setting: a choice of its own, not gameplay
    CHECK(t.time == 1 && t.gameplay_default() && t.record_word() == 0);
}

TEST(stepping_and_the_default_kept_as_the_games) {
    RaceRowModel m;
    m.track_laps = 3;
    RaceChoice c;
    m.set(RaceRow::kTraffic, 0, c);
    CHECK(c.traffic == 0 && !c.gameplay_default());
    m.set(RaceRow::kLaps, 4, c);
    CHECK(c.laps == 5);
    m.set(RaceRow::kLaps, 2, c);   // back on the track's own: stored as "the track's"
    CHECK(c.laps == 0);
    m.set(RaceRow::kRivals, 0, c);
    CHECK(c.rivals == 1);
    m.set(RaceRow::kRivals, 2, c);
    CHECK(c.rivals == 0);
    CHECK(m.count(RaceRow::kRivals) == 5);   // ROAD RAGE: up to 5 (the 6-car frame), 3 the game's
    m.set(RaceRow::kRivals, 4, c);
    CHECK(c.rivals == 5 && m.text(RaceRow::kRivals, 4) == "5" && m.index(RaceRow::kRivals, c) == 4);
    m.set(RaceRow::kRivals, 2, c);
    CHECK(c.rivals == 0);
    m.set(RaceRow::kTraffic, 2, c);
    CHECK(c.gameplay_default());
}

TEST(laps_never_past_eight) {
    RaceRowModel m;
    m.track_laps = 3;
    CHECK(m.count(RaceRow::kLaps) == 8 && b3native::kRaceMaxLaps == 8);   // IncrementLapCount wraps after 8
    RaceChoice c;
    CHECK(!RaceChoice::parse("t2 l9 r0 d0", c) && RaceChoice::parse("t2 l8 r0 d0", c) && c.laps == 8);
    m.track_laps = 1;   // point to point: the row disabled, the track's value
    CHECK(m.disabled(RaceRow::kLaps) && m.text(RaceRow::kLaps, 0) == "1 (Default)");
    m.track_laps = 0;   // unknown
    CHECK(m.disabled(RaceRow::kLaps) && m.text(RaceRow::kLaps, 0) == "-");
    c = RaceChoice{};
    c.laps = 5;
    CHECK(b3native::race_laps(c, 0, 3) == 5);
    CHECK(b3native::race_laps(c, 1, 0) == 0 && b3native::race_laps(c, 2, 3) == 0);   // TIME ATTACK, ROAD RAGE: theirs
    CHECK(b3native::race_laps(c, 0, 1) == 0);                                          // not a lap race
    c.laps = 0;
    CHECK(b3native::race_laps(c, 0, 3) == 0);
}

TEST(lifetime_single_event_stage_of_the_pages_mode_only) {
    b3native::RaceStage s;
    s.current_mode = b3native::kRaceOnePlayerStage;
    s.tag = b3native::kRaceSingleEventTag;
    s.game_type = 1;
    s.mode = 0;
    CHECK(b3native::race_options_apply(0, s));
    CHECK(!b3native::race_options_apply(-1, s));   // reset (SELECT MODE, the main menu): nothing applies
    CHECK(!b3native::race_options_apply(2, s));    // another mode than the page's
    CHECK(!b3native::race_options_apply(3, s));    // CRASH: never
    b3native::RaceStage w = s;
    w.tag = 0x0051B928u;                           // World Tour's record
    CHECK(!b3native::race_options_apply(0, w));
    w = s;
    w.game_type = 2;                               // MULTIPLAYER
    CHECK(!b3native::race_options_apply(0, w));
    w = s;
    w.current_mode = 0x0051AEC0u;                  // another game mode object
    CHECK(!b3native::race_options_apply(0, w));
}

TEST(records_kept_apart) {
    RaceChoice c;
    c.traffic = 0;
    c.laps = 5;
    const std::uint64_t w = c.record_word();
    CHECK(w != 0 && b3replay::records::options_folder(w) == "options-traffic-off-laps-5");
    c = RaceChoice{};
    c.rivals = 1;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-rivals-1");
    c.rivals = 5;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-rivals-5");
    c = RaceChoice{};
    c.traffic = 3;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-traffic-heavy");
    CHECK(b3replay::records::options_folder(0).empty());
    b3replay::records::Run r;
    CHECK(b3replay::records::folder("g", r) == "g");   // the game's own race: the player's folder
    r.race_options = w;
    CHECK(b3replay::records::folder("g", r) == "g/options-traffic-off-laps-5");
    r.gameplay_set = "0123456789abcdef";
    CHECK(b3replay::records::folder("g", r) == "g/modded-0123456789abcdef/options-traffic-off-laps-5");
    CHECK(b3replay::records::writes("g", r));   // kept, apart
    r.playback = true;
    CHECK(!b3replay::records::writes("g", r));  // a replay: never
}

TEST(words_per_language) {
    RaceRowModel m;
    m.track_laps = 3;
    CHECK(m.text(RaceRow::kTraffic, 0) == "Off" && std::string(b3native::race_row_label(RaceRow::kTime)) == "TIME OF DAY");
    m.language = "FR";
    CHECK(m.text(RaceRow::kTraffic, 1) == "L\xC3\xA9ger" && m.text(RaceRow::kTraffic, 2) == "Normal (d\xC3\xA9" "faut)");
    CHECK(m.text(RaceRow::kLaps, 2) == "3 (d\xC3\xA9" "faut)" && std::string(b3native::race_row_label(RaceRow::kLaps, "FR")) == "TOURS");
    m.language = "GE";
    CHECK(m.text(RaceRow::kTime, 1) == "Nacht" && m.text(RaceRow::kRivals, 2) == "3 (Standard)");
    CHECK(std::string(b3native::race_row_label(RaceRow::kRivals, "GE")) == "RIVALEN");
    // every label and value, its default tag included, fits a string slot (23 UTF-16 units), each of the 13 row kinds,
    // the day and the night default (i18ncheck: TRANSMISSION's and RIVALS' CARS' first values were cut in FR / IT / SP)
    const auto units = [](const std::string& s) { int n = 0; for (unsigned char ch : s) n += (ch & 0xC0) != 0x80; return n; };
    for (const char* c : {"", "FR", "GE", "IT", "SP", "DU", "SW"})
        for (const bool night : {false, true}) {
            m.language = c;
            m.night = night;
            for (unsigned k = 0; k < b3native::kRaceRowKinds; ++k) {
                const RaceRow r = RaceRow(k);
                CHECK(units(b3native::race_row_label(r, c)) <= 23);
                for (int i = 0; i < m.count(r); ++i)
                    if (units(m.text(r, i)) > 23) {
                        std::printf("  %s row %u value %d: %s\n", c, k, i, m.text(r, i).c_str());
                        CHECK(false);
                    }
            }
        }
    m.night = false;
    m.language = "JA";   // not one of them: English
    CHECK(m.text(RaceRow::kTraffic, 4) == "Heavy");
}

// options3: TRAFFIC's six presets in the page's order (Busy x1.5, Heavy x2, Extreme x3: 40 / 30 / 20 s), the
// stored values unchanged (Heavy 3), and the race mods' rows
TEST(traffic_presets_and_race_mods) {
    RaceRowModel m;
    RaceChoice c;
    CHECK(m.count(RaceRow::kTraffic) == 6 && m.index(RaceRow::kTraffic, c) == 2);
    const char* names[6] = {"Off", "Light", "Normal (Default)", "Busy", "Heavy", "Extreme"};
    const float scales[6] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f};
    for (int i = 0; i < 6; ++i) {
        CHECK(m.text(RaceRow::kTraffic, i) == names[i]);
        RaceChoice t;
        m.set(RaceRow::kTraffic, i, t);
        CHECK(m.index(RaceRow::kTraffic, t) == i && b3native::race_traffic_scale(t.traffic) == scales[i]);
    }
    m.set(RaceRow::kTraffic, 4, c);
    CHECK(c.traffic == 3);   // Heavy keeps its value (records' folders, older codes)
    CHECK(m.text(RaceRow::kStart, 0) == "Rolling (Default)" && m.text(RaceRow::kStart, 1) == "Standing");
    CHECK(m.text(RaceRow::kStyle, 1) == "Road Rage" && m.text(RaceRow::kPoints, 1) == "All");
    CHECK(m.count(RaceRow::kLimit) == 5 && m.text(RaceRow::kLimit, 0) == "Off (Default)" && m.text(RaceRow::kLimit, 4) == "5:00");
    CHECK(b3native::race_time_limit_seconds(3) == 180 && b3native::race_time_limit_seconds(0) == 0);
    RaceChoice g;
    m.set(RaceRow::kStart, 1, g);
    CHECK(!g.gameplay_default() && g.record_word() != 0);
    m.set(RaceRow::kStart, 0, g);
    CHECK(g.gameplay_default());
}

TEST(records_folders_32_bits) {
    RaceChoice c;
    c.traffic = 5;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-traffic-extreme");
    c.traffic = 4;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-traffic-busy");
    c.traffic = 3;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-traffic-heavy");   // as before
    c = RaceChoice{};
    c.start = 1, c.style = 1;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-standing-start-roadrage-ai");
    c = RaceChoice{};
    c.points = 1, c.limit = 3, c.rivals = 5;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-rivals-5-all-points-time-limit-3");
    CHECK(c.record_word() > 0xFFFFu);   // (the high half: HTD1's race word, gameview_native.cpp)
    c = RaceChoice{};
    c.seed = 1;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-varied-traffic");
}

// options3: RIVALS' CARS -- the classes the game's picker takes, the words, the records folder
TEST(rival_cars) {
    CHECK(b3native::race_rival_class(0) == -1 && b3native::race_rival_class(1) == 0 && b3native::race_rival_class(5) == 4 &&
          b3native::race_rival_class(6) == 6 && b3native::race_rival_class(7) == -2 &&
          b3native::race_rival_class(8) == b3native::kRivalClassMixed);
    RaceRowModel m;
    CHECK(m.count(RaceRow::kCars) == 9 && m.text(RaceRow::kCars, 0) == "Your class (Default)" &&
          m.text(RaceRow::kCars, 6) == "Heavyweights" && m.text(RaceRow::kCars, 7) == "Same as yours" && m.text(RaceRow::kCars, 8) == "Mixed");
    RaceChoice c;
    m.set(RaceRow::kCars, 6, c);
    CHECK(c.cars == 6 && m.index(RaceRow::kCars, c) == 6 && !c.gameplay_default());
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-rival-cars-heavyweights");
    c.cars = 7;
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-rival-cars-same");
    // mixedrivals: Mixed its own bit (38), the class bits clear; its code tag c8 round-trips
    c.cars = 8;
    CHECK(c.record_word() == (b3native::kRaceWordFlag | 2u | b3native::kRaceWordCarsMixed));
    CHECK(b3replay::records::options_folder(c.record_word()) == "options-rival-cars-mixed");
    RaceChoice d;
    CHECK(c.code() == "t2 l0 r0 d0 c8" && RaceChoice::parse(c.code(), d) && d == c);
    for (const char* lang : {"", "FR", "GE", "IT", "SP", "DU", "SW"}) CHECK(b3native::race_words(lang).cars[8] != nullptr && *b3native::race_words(lang).cars[8]);
}

TEST(code_round_trip) {
    RaceChoice c, d;
    c.traffic = 1, c.laps = 7, c.rivals = 2, c.time = 2, c.start = 1, c.style = 1, c.points = 1, c.limit = 4, c.seed = 1, c.cars = 6;
    // options3's tags only when not the game's, after elim1's e and gearbox's m (TIME LIMIT is k: m is TRANSMISSION's)
    CHECK(c.code() == "t1 l7 r2 d2 s1 a1 p1 k4 v1 c6" && RaceChoice::parse(c.code(), d) && d == c);
    c.manual = 1, c.timed = 1;
    CHECK(c.code() == "t1 l7 r2 d2 e1 m1 s1 a1 p1 k4 v1 c6" && RaceChoice::parse(c.code(), d) && d == c);
    c = RaceChoice{};
    c.limit = 2;
    CHECK(c.code() == "t2 l0 r0 d0 k2" && RaceChoice::parse(c.code(), d) && d == c);
    // dev-era codes as they were
    CHECK(RaceChoice::parse("t3 l0 r0 d0", d) && d.traffic == 3 && d.start == 0 && d.limit == 0);
    CHECK(RaceChoice::parse("t2 l0 r0 d0 e1 m1", d) && d.timed == 1 && d.manual == 1 && d.limit == 0);
    CHECK(RaceChoice::parse("t2 l0 r0 d0 m1", d) && d.manual == 1);
    // refused: out of range, out of order, twice, unknown
    CHECK(!RaceChoice::parse("t6 l0 r0 d0", d) && !RaceChoice::parse("t2 l0 r3 d0", d) && !RaceChoice::parse("t2 l0 r6 d0", d) &&
          !RaceChoice::parse("t2 l0 r0 d0 s2", d) && !RaceChoice::parse("t2 l0 r0 d0 k5", d) && !RaceChoice::parse("t2 l0 r0 d0 c9", d) &&
          !RaceChoice::parse("t2 l0 r0 d0 m1 e1", d) && !RaceChoice::parse("t2 l0 r0 d0 s1 s1", d) &&
          !RaceChoice::parse("t2 l0 r0 d0 z1", d) && !RaceChoice::parse("t2 l0 r0 d0 x3", d) && !RaceChoice::parse("x", d));
    CHECK(RaceChoice::parse("t2 l0 r5 d0", d) && d.rivals == 5);
}

TEST(bgd_stage_laps) {
    std::vector<std::uint8_t> f(0x1000, 0);
    auto w32 = [&](std::size_t o, std::uint32_t v) { std::memcpy(&f[o], &v, 4); };
    const std::uint64_t a = 0x1122334455667788ull, b = 0x99AABBCCDDEEFF00ull;
    w32(0x260, 2);
    std::memcpy(&f[0x08], &a, 8);
    std::memcpy(&f[0x10], &b, 8);
    w32(0x198, 0x800);
    w32(0x19C, 0xC00 - 0x3B8 - 4);   // the second header near the end
    w32(0x800 + 0x3B8, 3);
    w32(0xC00 - 4, 2);
    CHECK(b3native::race_bgd_laps(f.data(), f.size(), a) == 3);
    CHECK(b3native::race_bgd_laps(f.data(), f.size(), b) == 2);
    CHECK(b3native::race_bgd_laps(f.data(), f.size(), 42) == 0);
    CHECK(b3native::race_bgd_laps(f.data(), 0x700, a) == 0);
    w32(0x198, 0x0FFF);   // a header outside the file
    CHECK(b3native::race_bgd_laps(f.data(), f.size(), a) == 0);
}

}  // namespace

TEST_MAIN("SELECT OPTIONS per-race options (raceopts)")
