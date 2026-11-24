// The launcher's showroom (photo2; src/b3launcher/showroom.h): the game's string table, the car names' block and the
// files they belong to, the car list of a game folder, the turntable. With B3_GAME_DIR (the user's disc folder): the
// real list.
#include "test.h"

#include "showroom.h"
#include "../tools/native/extra_series.h"   // (showpages: the pages' size and codes)

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace b3launcher::showroom;

// A string table as DATA/GLOBALUS.BIN lays it out: an 8-byte head, the count, the table's offset, the offsets, the
// UTF-16LE strings
std::vector<std::uint8_t> table(const std::vector<std::u16string>& strings) {
    std::vector<std::uint8_t> b(16, 0);
    auto put = [&](std::size_t at, std::uint32_t v) { for (int k = 0; k < 4; ++k) b[at + std::size_t(k)] = std::uint8_t(v >> (8 * k)); };
    put(8, std::uint32_t(strings.size()));
    put(12, 16);
    b.resize(16 + 4 * strings.size());
    for (std::size_t k = 0; k < strings.size(); ++k) {
        put(16 + 4 * k, std::uint32_t(b.size()));
        for (char16_t c : strings[k]) { b.push_back(std::uint8_t(c)); b.push_back(std::uint8_t(c >> 8)); }
        b.push_back(0);
        b.push_back(0);
    }
    return b;
}

std::vector<std::u16string> names_block(bool title_case) {
    std::vector<std::u16string> v;
    const char16_t* classes[5] = {u"Compact", u"Muscle", u"Coupe", u"Sports", u"Super"};
    for (auto* c : classes)
        for (int k = 1; k <= 10; ++k) {
            std::u16string s = std::u16string(c) + u" Car " + std::u16string(1, char16_t(u'0' + k % 10));
            if (k == 1) s = std::u16string(c) + u" Type 1";
            v.push_back(s);
        }
    for (int k = 0; k < 17; ++k) v.push_back(u"Other " + std::u16string(1, char16_t(u'A' + k)) + u" ");
    if (!title_case)
        for (auto& s : v)
            for (auto& c : s) if (c >= u'a' && c <= u'z') c = char16_t(c - 32);
    return v;
}

TEST(string_table) {
    const auto s = read_strings(table({u"Race", u"Été", u"", u"\U0001F600"}));
    CHECK(s.size() == 4);
    CHECK(s[0] == "Race");
    CHECK(s[1] == "\xC3\x89t\xC3\xA9");
    CHECK(s[2].empty());
    CHECK(s[3] == "\xF0\x9F\x98\x80");
    CHECK(read_strings({}).empty());
    std::vector<std::uint8_t> bad = table({u"a"});
    bad[12] = 0xF0;   // the table outside the file
    CHECK(read_strings(bad).empty());
}

TEST(the_names_block) {
    // the upper-case block first, then the title-case one (as on the disc): the title-case one is taken
    std::vector<std::u16string> all{u"Pure Scores", u"Crashed Out"};
    for (auto& s : names_block(false)) all.push_back(s);
    for (auto& s : names_block(true)) all.push_back(s);
    auto n = car_names(read_strings(table(all)));
    CHECK(n.size() == 67);
    CHECK(n[0] == "Compact Type 1");
    CHECK(n[10] == "Muscle Type 1");
    CHECK(n[66] == "Other Q");   // trailing spaces trimmed
    // upper case only
    all.resize(2 + 67);
    n = car_names(read_strings(table(all)));
    CHECK(n.size() == 67);
    CHECK(n[0] == "COMPACT TYPE 1");
    // cut short, or not there
    all.resize(2 + 30);
    CHECK(car_names(read_strings(table(all))).empty());
    CHECK(car_names({"a", "b"}).empty());
}

TEST(file_to_name) {
    CHECK(name_index("COMP", 1) == 0);
    CHECK(name_index("comp", 10) == 9);
    CHECK(name_index("MSCL", 1) == 10);
    CHECK(name_index("CUPE", 5) == 24);
    CHECK(name_index("SPRT", 10) == 39);
    CHECK(name_index("SUPR", 1) == 40);
    CHECK(name_index("COMP", 11) == -1);   // traffic (CAR11.BTV and on) is not a player car
    CHECK(name_index("HSPC", 20) == 50);
    CHECK(name_index("HSPC", 23) == 53);
    CHECK(name_index("HEVY", 36) == 54);   // the fire truck
    CHECK(name_index("HSPC", 24) == 55);
    CHECK(name_index("HSPC", 25) == 56);
    CHECK(name_index("HEVY", 1) == 57);
    CHECK(name_index("HEVY", 10) == 66);
    CHECK(name_index("TSPC", 1) == -1);
    // every index once
    std::vector<int> seen(67, 0);
    for (const char* f : {"COMP", "MSCL", "CUPE", "SPRT", "SUPR"}) for (int k = 1; k <= 10; ++k) ++seen[std::size_t(name_index(f, k))];
    for (int k = 20; k <= 25; ++k) ++seen[std::size_t(name_index("HSPC", k))];
    for (int k = 1; k <= 10; ++k) ++seen[std::size_t(name_index("HEVY", k))];
    ++seen[std::size_t(name_index("HEVY", 36))];
    for (int c : seen) CHECK(c == 1);
}

TEST(car_list_of_a_folder) {
    const fs::path root = fs::path("build") / "test_showroom_game";
    std::error_code ec;
    fs::remove_all(root, ec);
    for (const char* f : {"PVEH/COMP", "PVEH/HEVY", "PVEH/TSPC", "DATA"}) fs::create_directories(root / f);
    for (const char* f : {"PVEH/COMP/CAR2.BGV", "PVEH/COMP/CAR1.BGV", "PVEH/COMP/CAR1.HWD", "PVEH/COMP/CAR11.BTV", "PVEH/HEVY/CAR36.BGV",
                          "PVEH/HEVY/CAR1.BGV", "PVEH/TSPC/CAR1.BTV"})
        std::ofstream(root / f) << "x";
    std::string note;
    auto cars = list_cars(root, &note);
    CHECK(cars.size() == 4);
    CHECK(!note.empty());   // no string table: class and number
    if (cars.size() == 4) {
        CHECK(car_series(cars[0]) == 0 && car_series(cars[2]) == kSeriesSpecial && car_series(cars[3]) == kSeriesHeavy);   // showroom2
        CHECK(cars[0].folder == "COMP" && cars[0].number == 1);
        CHECK(cars[0].name == "Compact 1");
        CHECK(cars[1].number == 2);
        CHECK(cars[2].folder == "HEVY" && cars[2].number == 36);   // the specials before the heavyweights
        CHECK(cars[2].kind == "Special");
        CHECK(cars[3].folder == "HEVY" && cars[3].number == 1);
        CHECK(cars[3].kind == "Heavyweight");
    }
    {
        std::vector<std::u16string> all{u"x"};
        for (auto& s : names_block(true)) all.push_back(s);
        const auto t = table(all);
        std::ofstream(root / "DATA/GLOBALUS.BIN", std::ios::binary).write(reinterpret_cast<const char*>(t.data()), std::streamsize(t.size()));
    }
    cars = list_cars(root, &note);
    CHECK(note.empty());
    CHECK(cars.size() == 4 && cars[0].name == "Compact Type 1" && cars[2].name == "Other E" && cars[3].name == "Other H");
    // showroom2: the series with cars, in SELECT CAR's order; the extra cars' by their kind (customseries' list_more_cars)
    CarEntry t, cu;
    t.kind = "Traffic", t.folder = "COMP", t.number = 18;
    cu.kind = "Custom", cu.folder = "COMP", cu.number = 7;
    cars.push_back(cu);
    cars.push_back(t);
    const auto series = group_series(cars, {});
    CHECK(series.size() == 5);
    if (series.size() == 5) {
        CHECK(series[0].id == 0 && series[0].name == "COMPACT SERIES" && series[0].cars.size() == 2);
        CHECK(series[1].id == kSeriesSpecial && series[2].id == kSeriesHeavy);
        CHECK(series[3].id == kSeriesTraffic && series[3].name == "TRAFFIC" && series[3].cars == std::vector<std::size_t>{5});   // (before CUSTOM)
        CHECK(series[4].id == kSeriesCustom && series[4].name == "CUSTOM" && series[4].cars == std::vector<std::size_t>{4});
    }
    CHECK(list_cars(root / "nothing", &note).empty());
    CHECK(!note.empty());
    fs::remove_all(root, ec);
}

// showpages: CUSTOM and BETA in SELECT CAR's pages of 19 (carpacks), each a series of its own, named as the game names
// them (extra_words in the launcher's language, " 2" on from the second page); CUSTOM's pages before BETA's
TEST(custom_and_beta_pages) {
    std::vector<CarEntry> cars;
    for (int k = 0; k < 40; ++k) {   // a pack of 40: pages of 19, 19 and 2 (as extra_assign_slots gives them)
        CarEntry c;
        c.kind = "Custom", c.folder = "COMP", c.number = k + 1, c.page = k / b3native::kExtraPageCars;
        cars.push_back(c);
    }
    CarEntry b;
    b.kind = "Beta", b.folder = "SPRT", b.number = 1;
    cars.push_back(b);
    const auto series = group_series(cars, {});
    CHECK(series.size() == 4);
    if (series.size() == 4) {
        CHECK(series[0].id == kSeriesCustom && series[0].name == "CUSTOM" && series[0].cars.size() == 19 && series[0].cars.front() == 0);
        CHECK(series[1].id == b3native::extra_page_series(b3native::kCustomSeries, 1) && series[1].name == "CUSTOM 2" && series[1].cars.size() == 19 &&
              series[1].cars.front() == 19);
        CHECK(series[2].name == "CUSTOM 3" && series[2].cars == (std::vector<std::size_t>{38, 39}));
        CHECK(series[3].id == kSeriesBeta && series[3].name == "BETA" && series[3].cars == std::vector<std::size_t>{40});
    }
    const auto fr = group_series(cars, {}, "FR");   // (UTF-8, as extra_words gives it)
    CHECK(fr.size() == 4 && fr[0].name == "PERSONNALIS\xC3\x89" "ES" && fr[2].name == "PERSONNALIS\xC3\x89" "ES 3" && fr[3].name == "B\xC3\x8ATA");
    CHECK(car_series(cars[0]) == kSeriesCustom && car_series(cars[39]) == b3native::extra_page_series(b3native::kCustomSeries, 2) &&
          car_series(b) == kSeriesBeta);
    CHECK(series_name(b3native::extra_page_series(b3native::kBetaSeries, 1), {}) == "BETA 2");
}

// showroom2: the series' names (the game's strings, else the fallback)
TEST(series_names) {
    std::vector<std::string> strings(0xC00);
    strings[0xBEF] = "Compact Series";
    strings[0xBF4] = "HEAVYWEIGHTS";
    strings[0xBF5] = "SPECIAL SERIES ";
    strings[380] = "TRAFFIC";
    CHECK(series_name(0, strings) == "COMPACT SERIES");
    CHECK(series_name(kSeriesSpecial, strings) == "SPECIAL SERIES" && series_name(kSeriesHeavy, strings) == "HEAVYWEIGHTS");
    CHECK(series_name(1, strings) == "MUSCLE SERIES");   // (none in the table: the fallback)
    CHECK(series_name(kSeriesTrafficSpecial, {}) == "TRAFFIC SPECIAL" && series_name(kSeriesCustom, {}) == "CUSTOM" &&
          series_name(kSeriesBeta, {}) == "BETA");
}

TEST(turntable) {
    Turntable t;
    const float y0 = t.yaw;
    t.step(1.0f);
    CHECK_NEAR(t.yaw - y0, Turntable::kSpinRate, 1e-6);
    t.drag(100, 0);   // a drag turns it and holds the spin
    const float y1 = t.yaw;
    t.step(1.0f);
    CHECK(t.yaw == y1);
    t.step(Turntable::kHoldSeconds);
    t.step(1.0f);
    CHECK(t.yaw != y1);
    t.drag(0, 1e6f);
    CHECK(t.pitch == Turntable::kMaxPitch);
    t.drag(0, -1e6f);
    CHECK(t.pitch == Turntable::kMinPitch);
    t.wheel(100);
    CHECK(t.zoom == Turntable::kMinZoom);
    t.wheel(-100);
    CHECK(t.zoom == Turntable::kMaxZoom);
    const float p0 = t.pitch;
    t.stick(0.1f, 0.1f, 1.0f);   // within the dead zone: nothing
    CHECK(t.pitch == p0);
    t.stick(0, 1, 0.1f);
    CHECK(t.pitch > p0);
    t.spin = false;
    t.hold = 0;
    const float y2 = t.yaw;
    t.step(1.0f);
    CHECK(t.yaw == y2);
    t.reset();
    CHECK(t.spin && t.zoom == 1.0f);
}

// The user's disc (B3_GAME_DIR): the 67 player cars, named
TEST(the_real_disc) {
    const char* g = std::getenv("B3_GAME_DIR");
    if (!g || !*g) { std::printf("  (B3_GAME_DIR not set: skipped)\n"); return; }
    std::string note;
    const auto cars = list_cars(g, &note);
    std::printf("  %zu cars%s%s\n", cars.size(), note.empty() ? "" : ", ", note.c_str());
    CHECK(cars.size() == 67);
    CHECK(note.empty());
    if (cars.size() != 67) return;
    CHECK(cars[0].name == "Compact Type 1");
    for (const auto& c : cars) CHECK(!c.name.empty() && c.name.find("Heavyweight ") != 0);
    // showroom2: the game's series names (the disc's seven; TRAFFIC and the player's cars come with customseries' list)
    std::vector<std::string> strings;
    (void)list_cars(g, &note, &strings);
    const auto series = group_series(cars, strings);
    CHECK(series.size() == 7);
    if (series.size() == 7) CHECK(series[0].name == "COMPACT SERIES" && series[5].name == "SPECIAL SERIES" && series[6].name == "HEAVYWEIGHTS");
    std::printf("  series:");
    for (const auto& se : series) std::printf(" %s (%zu)", se.name.c_str(), se.cars.size());
    std::printf("\n");
    CHECK(series_name(kSeriesTraffic, strings) == "TRAFFIC" && series_name(kSeriesTrafficSpecial, strings) == "TRAFFIC SPECIAL");
    // with customseries' extra cars (no custom or beta folder: the traffic vehicles), grouped by their kinds
    auto all = cars;
    for (auto& c : list_more_cars(g, fs::path("build") / "test_showroom_no_custom", fs::path("build") / "test_showroom_no_beta")) all.push_back(c);
    const auto more = group_series(all, strings);
    CHECK(more.size() == 9);
    if (more.size() == 9)
        CHECK(more[7].id == kSeriesTraffic && more[7].name == "TRAFFIC" && more[7].cars.size() == 20 && more[8].id == kSeriesTrafficSpecial &&
              more[8].name == "TRAFFIC SPECIAL" && more[8].cars.size() == 7);
    std::printf("  first %s, last %s\n", cars.front().name.c_str(), cars.back().name.c_str());
}

}  // namespace

TEST_MAIN("launcher showroom (src/b3launcher/showroom.h)")
