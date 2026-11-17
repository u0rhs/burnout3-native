// The launcher's showroom (photo2; src/b3launcher/showroom.h): the game's string table, the car names' block and the
// files they belong to, the car list of a game folder, the turntable. With B3_GAME_DIR (the user's disc folder): the
// real list.
#include "test.h"

#include "showroom.h"

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
    CHECK(list_cars(root / "nothing", &note).empty());
    CHECK(!note.empty());
    fs::remove_all(root, ec);
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
    std::printf("  first %s, last %s\n", cars.front().name.c_str(), cars.back().name.c_str());
}

}  // namespace

TEST_MAIN("launcher showroom (src/b3launcher/showroom.h)")
