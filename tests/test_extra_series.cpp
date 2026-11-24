// customseries (tools/native/extra_series.h): players' car mods as extra cars and the CUSTOM series -- what a car file
// must be, the folder's names and car.ini, the slots and their GtIDs / disc paths, the disc names, the tuning files, the
// steps after the series to its left, the record word and its folder.
#include "test.h"

#include "extra_series.h"
#include "../src/native/disc_alias.h"
#include "../src/b3replay/records.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace b3native;

std::vector<std::uint8_t> bank_file(std::size_t size = 0x9000) {   // the header every bank on the disc has
    std::vector<std::uint8_t> b(size, 0);
    b[0] = 0x09, b[1] = 0x08, b[9] = 0x08, b[12] = 0x2C;
    return b;
}
std::vector<std::uint8_t> car_file(std::uint32_t version, std::uint32_t lods, std::size_t size, bool sound) {
    std::vector<std::uint8_t> b(size, 0);
    auto put = [&](std::size_t o, std::uint32_t v) { for (int k = 0; k < 4; ++k) b[o + std::size_t(k)] = std::uint8_t(v >> (8 * k)); };
    put(0, version);
    put(8, std::uint32_t(size));
    put(0x4C, lods);
    if (sound) put(0x16DC, std::uint32_t(size - 0x200)), put(0x16E0, std::uint32_t(size - 0x100));
    return b;
}

TEST(car_files_the_game_can_load) {
    CHECK(extra_car_file_problem(car_file(0x17, 0x16F0, 0x6E000, true)).empty());
    CHECK(extra_car_file_problem(car_file(0x15, 0x16F0, 0x6E000, true)).empty());   // a mod of an earlier build: the same layout
    CHECK(extra_car_file_problem(car_file(0x15, 0x16E0, 0x6E000, false)).find("prototype") != std::string::npos);
    CHECK(!extra_car_file_problem(car_file(0x18, 0x16F0, 0x6E000, true)).empty());
    CHECK(!extra_car_file_problem(car_file(0x17, 0x1234, 0x6E000, true)).empty());
    CHECK(!extra_car_file_problem(car_file(0x17, 0x16F0, 0x73000, true)).empty());  // past the game's room for a car
    std::vector<std::uint8_t> cut = car_file(0x17, 0x16F0, 0x6E000, true);
    cut.resize(0x6D000);
    CHECK(extra_car_file_problem(cut).find("cut") != std::string::npos);
    std::vector<std::uint8_t> padded = car_file(0x17, 0x16F0, 0x6E000, true);
    padded.resize(0x6E800);   // (a pack's: trailing bytes past the header's size)
    CHECK(extra_car_file_problem(padded).empty());
    CHECK(!extra_car_file_problem(std::vector<std::uint8_t>(0x100, 0)).empty());
    CHECK(extra_car_file_problem(car_file(0x17, 0x17E0, 0x6E000, true)).empty());   // BETA's import: its meshes kept 256-aligned
    CHECK(extra_car_has_sound(car_file(0x17, 0x16F0, 0x6E000, true)));
    CHECK(!extra_car_has_sound(car_file(0x17, 0x16F0, 0x6E000, false)));
}

TEST(a_mod_named_after_the_car_it_replaces) {
    std::string cls, car;
    CHECK(extra_named_entry("COMP/CAR7.BGV", &cls, &car) && cls == "COMP" && car == "CAR7");
    CHECK(extra_named_entry("hevy\\car12.bgv", &cls, &car) && cls == "HEVY" && car == "CAR12");
    CHECK(!extra_named_entry("TRAF/CAR1.BGV", &cls, &car));    // no race car class
    CHECK(!extra_named_entry("COMP/CAR0.BGV", &cls, &car));
    CHECK(!extra_named_entry("COMP/MYCAR.BGV", &cls, &car));
    CHECK(!extra_named_entry("CAR7.BGV", &cls, &car));
    CHECK(!extra_named_entry("COMP/CAR7.BTV", &cls, &car));
    CHECK(!extra_named_entry("A/COMP/CAR7.BGV", &cls, &car));
}

TEST(car_ini) {
    const ExtraIni a = extra_parse_ini("# my car\r\nName = Shotty GT\r\ndonor= mscl/car2\nmodel = gt.bgv\n; banks\nHWD=gt.hwd\n");
    CHECK(a.problem.empty() && a.name == "Shotty GT" && a.donor_cls == "MSCL" && a.donor_car == "CAR2" && a.model == "gt.bgv" &&
          a.hwd == "gt.hwd" && a.lwd.empty());
    CHECK(!extra_parse_ini("name = x\n").problem.empty());                 // no donor
    CHECK(!extra_parse_ini("donor = TSPC/CAR1\n").problem.empty());        // not a race car
    CHECK(!extra_parse_ini("donor = COMP\n").problem.empty());
    CHECK(extra_display_name("  Shotty gt\t\x01 ") == "SHOTTY GT");
    CHECK(extra_display_name(std::string(40, 'a')).size() == 32);
}

TEST(the_folder_as_players_leave_it) {
    namespace fs = std::filesystem;
    const fs::path d = fs::path("build") / "test_extra_series_cars";
    std::error_code ec;
    fs::remove_all(d, ec);
    auto put = [](const fs::path& f, const std::vector<std::uint8_t>& b) {
        fs::create_directories(f.parent_path());
        std::ofstream(f, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    };
    const std::vector<std::uint8_t> good = car_file(0x17, 0x16F0, 0x2000, true);
    put(d / "COMP" / "CAR7.BGV", good);
    put(d / "COMP" / "CAR7.HWD", bank_file());
    put(d / "SPRT" / "CAR4.BGV", good);
    put(d / "SPRT" / "CAR4.LWD", {1, 2, 3});                                // not a bank
    put(d / "esc" / "x.bgv", good);
    std::ofstream(d / "esc" / "car.ini") << "donor = COMP/CAR1\nmodel = ../COMP/CAR7.BGV\n";   // outside its folder
    put(d / "MSCL" / "CAR2.BGV", car_file(0x15, 0x16E0, 0x2000, false));   // a prototype disc's file
    put(d / "MSCL" / "FAST.BGV", good);                                     // not named after a car
    put(d / "CAR9.BGV", good);                                              // no class folder
    put(d / "gt" / "gt.bgv", good);
    std::ofstream(d / "gt" / "car.ini") << "name = My GT\ndonor = SPRT/CAR3\n";
    put(d / "nodonor" / "x.bgv", good);
    std::ofstream(d / "nodonor" / "car.ini") << "name = x\n";
    const ExtraScan s = extra_scan(d);
    CHECK(s.cars.size() == 2);
    CHECK(s.cars.size() == 2 && s.cars[0].source == "COMP/CAR7.BGV" && s.cars[0].slot == 1 && !s.cars[0].hwd.empty() && s.cars[0].lwd.empty());
    CHECK(s.cars.size() == 2 && s.cars[1].source == "gt/car.ini" && s.cars[1].name == "MY GT" && s.cars[1].donor_name() == "SPRTCAR3");
    CHECK(s.refused.size() == 6);
    std::set<std::string> what;
    for (const auto& [w, why] : s.refused) what.insert(w + ": " + why.substr(0, 12));
    CHECK(what.count("MSCL/CAR2.BGV: a prototype ") == 1);
    CHECK(what.count("MSCL/FAST.BGV: not named af") == 1);
    CHECK(what.count("CAR9.BGV: a car file n") == 1);
    CHECK(what.count("nodonor/car.ini: car.ini name") == 1);
    CHECK(what.count("SPRT/CAR4.BGV: its sound ba") == 1);
    CHECK(what.count("esc/car.ini: car.ini's \".") == 1);
    CHECK(extra_plain_name("car.bgv") && !extra_plain_name("../car.bgv") && !extra_plain_name("a/b.bgv") && !extra_plain_name("a\\b") &&
          !extra_plain_name("C:car.bgv") && !extra_plain_name("..") && !extra_plain_name(""));
    CHECK(extra_scan(d / "none").cars.empty());
    // BETA's folder (the launcher's import): NN-<key>/car.bgv + car.ini, an import being written hidden
    const fs::path beta = d.parent_path() / "test_extra_series_beta";   // (beside cars/custom, as cars/beta is)
    fs::remove_all(beta, ec);
    put(beta / "01-jun21-hevy-car4" / "car.bgv", car_file(0x17, 0x17E0, 0x2000, true));
    std::ofstream(beta / "01-jun21-hevy-car4" / "car.ini") << "# imported\nname = SUV\ndonor = HEVY/CAR2\nmodel = car.bgv\nkey = jun21-hevy-car4\n";
    put(beta / ".02-jun21-hevy-car10.partial" / "car.bgv", good);
    std::ofstream(beta / ".02-jun21-hevy-car10.partial" / "car.ini") << "donor = HEVY/CAR3\n";
    const ExtraScan all = extra_scan_all(d, beta);
    CHECK(all.cars.size() == 3);
    CHECK(all.cars.size() == 3 && all.cars[2].series == kBetaSeries && all.cars[2].slot == 1 && all.cars[2].gtid_name() == "XBETCAR1" &&
          all.cars[2].name == "SUV" && all.cars[2].source == "beta/01-jun21-hevy-car4/car.ini");
    CHECK(all.cars.size() == 3 && all.cars[0].series == kCustomSeries && all.cars[1].slot == 2);
    // carpacks: a pack's folder of car folders, and a pack laid out as the game's folders
    put(d / "Mod Pack" / "COMP-CAR4" / "car.bgv", good);
    std::ofstream(d / "Mod Pack" / "COMP-CAR4" / "car.ini") << "name = Compact 9\ndonor = COMP/CAR4\nmodel = car.bgv\n";
    put(d / "Other Pack" / "HEVY" / "CAR5.BGV", good);
    put(d / "Other Pack" / "loose.bgv", good);
    const ExtraScan packs = extra_scan(d);
    int in_packs = 0;
    for (const ExtraCar& c : packs.cars)
        in_packs += (c.source == "Mod Pack/COMP-CAR4/car.ini" && c.name == "COMPACT 9") + (c.source == "Other Pack/HEVY/CAR5.BGV" && c.donor_name() == "HEVYCAR5");
    CHECK(in_packs == 2);
    bool loose = false;
    for (const auto& [w, why] : packs.refused) loose = loose || w == "Other Pack/loose.bgv";
    CHECK(loose);
    fs::remove_all(d, ec);
    fs::remove_all(beta, ec);
}

TEST(import_and_remove) {   // the launcher's Import from folder / Remove
    namespace fs = std::filesystem;
    const fs::path d = fs::path("build") / "test_extra_series_import";
    std::error_code ec;
    fs::remove_all(d, ec);
    auto put = [](const fs::path& f, const std::vector<std::uint8_t>& b) {
        fs::create_directories(f.parent_path());
        std::ofstream(f, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    };
    const std::vector<std::uint8_t> good = car_file(0x17, 0x16F0, 0x2000, true);
    const fs::path mod = d / "Shotty pack", custom = d / "custom";
    put(mod / "PVEH" / "COMP" / "CAR2.BGV", good);
    put(mod / "PVEH" / "COMP" / "car2.hwd", bank_file());
    put(mod / "PVEH" / "MSCL" / "CAR5.BGV", car_file(0x15, 0x16E0, 0x2000, false));   // a prototype disc's
    put(mod / "readme" / "CAR9.BGV", good);                                            // no class folder
    put(mod / "gt" / "gt.bgv", good);
    put(mod / "gt" / "notes.txt", {1});                                                 // not the car's: not copied
    std::ofstream(mod / "gt" / "car.ini") << "name = GT\ndonor = SPRT/CAR3\n";
    const ExtraImport r = extra_import(mod, custom);
    CHECK(r.done.size() == 2 && r.refused.size() == 2);
    CHECK(fs::is_regular_file(custom / "COMP" / "CAR2.BGV") && fs::is_regular_file(custom / "COMP" / "CAR2.HWD"));
    CHECK(fs::is_regular_file(custom / "gt" / "car.ini") && fs::is_regular_file(custom / "gt" / "gt.bgv") && !fs::exists(custom / "gt" / "notes.txt"));
    {   // again: both are there already (refused, nothing replaced)
        const ExtraImport again = extra_import(mod, custom);
        int there = 0;
        for (const auto& [w, why] : again.refused) there += why.find("already in the custom cars folder") != std::string::npos;
        CHECK(again.done.empty() && there == 2);
    }
    CHECK(!fs::exists(custom / "MSCL"));
    bool proto = false;
    for (const auto& [w, why] : r.refused) proto = proto || (w == "MSCL/CAR5.BGV" && why.find("Beta / Prototype cars") != std::string::npos);
    CHECK(proto);
    const ExtraScan s = extra_scan(custom);
    CHECK(s.cars.size() == 2 && s.refused.empty());
    CHECK(s.cars.size() == 2 && extra_remove(custom, s.cars[0]) && extra_remove(custom, s.cars[1]));
    CHECK(extra_scan(custom).cars.empty() && !fs::exists(custom / "COMP" / "CAR2.HWD") && !fs::exists(custom / "gt"));
    CHECK(!extra_import(d / "none", custom).refused.empty());
    fs::remove_all(d, ec);
}

TEST(hash_cache) {   // carpacks: a file read again only when its size or time changed
    namespace fs = std::filesystem;
    const fs::path d = fs::path("build") / "test_extra_series_hash";
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    std::ofstream(d / "a.bgv", std::ios::binary) << "abc";
    ExtraHashCache c;
    c.load(d / "hashes.txt");
    const std::uint64_t h = c.hash((d / "a.bgv").string());
    CHECK(h == extra_hash({'a', 'b', 'c'}));
    c.save();
    ExtraHashCache again;
    again.load(d / "hashes.txt");
    CHECK(again.hash((d / "a.bgv").string()) == h);
    std::ofstream(d / "a.bgv", std::ios::binary) << "abcd";   // changed (its size)
    CHECK(again.hash((d / "a.bgv").string()) == extra_hash({'a', 'b', 'c', 'd'}));
    CHECK(again.hash((d / "none.bgv").string()) == 0);
    fs::remove_all(d, ec);
}

TEST(slots_ids_and_disc_names) {
    std::vector<ExtraCar> cars(3);
    cars[0].source = "MSCL/CAR2.BGV", cars[0].donor_cls = "MSCL", cars[0].donor_car = "CAR2";
    cars[1].source = "COMP/CAR7.BGV", cars[1].donor_cls = "COMP", cars[1].donor_car = "CAR7";
    cars[2].source = "z/car.ini", cars[2].donor_cls = "HEVY", cars[2].donor_car = "CAR9", cars[2].hwd = "/m/z/z.hwd";
    std::vector<ExtraCar> dropped;
    extra_assign_slots(cars, 2, &dropped);
    CHECK(cars.size() == 2 && dropped.size() == 1 && dropped[0].source == "z/car.ini");
    CHECK(cars[0].source == "COMP/CAR7.BGV" && cars[0].slot == 1 && cars[1].slot == 2);   // the folder's order
    CHECK(cars[0].gtid_name() == "XCUSCAR1" && cars[0].id() == gtid_compress("XCUSCAR1") && cars[0].donor_id() == gtid_compress("COMPCAR7"));
    CHECK(extra_index(cars, cars[1].id()) == 1 && extra_index(cars, gtid_compress("COMPCAR7")) == -1 && extra_index(cars, 0) == -1);
    // GetClassFromID's prefix is none of the game's (its class would be 9): ours answer 10
    CHECK(!extra_donor_class(cars[0].cls()));
    const std::vector<std::string> models = {"/c/CAR7.BGV", ""};
    CHECK(extra_alias(cars, models, "pveh/xcus/car1.bgv") == "host:/c/CAR7.BGV");
    CHECK(extra_alias(cars, models, "PVEH/XCUS/CAR1.HWD") == "PVEH/COMP/CAR7.HWD");
    CHECK(extra_alias(cars, models, "PVEH/XCUS/CAR2.BGV").empty());   // not makeable
    CHECK(extra_alias(cars, models, "PVEH/XCUS/CAR2.LWD") == "PVEH/MSCL/CAR2.LWD");
    CHECK(extra_alias(cars, models, "PVEH/COMP/CAR7.BGV").empty());
    CHECK(extra_alias(cars, models, "PVEH/XCUS/CAR12.BGV").empty());
    CHECK(cars[0].disc_base() == "PVEH/XCUS/CAR1.");
}

namespace alias_test {
std::string first(const std::string& p) { return p == "PVEH/XCUS/CAR1.BGV" ? "host:/a" : ""; }
std::string second(const std::string& p) { return p == "PVEH/XCUS/CAR1.BGV" || p == "PVEH/COMP/CAR18.BGV" ? "host:/b" : ""; }
}
TEST(disc_aliases_in_the_order_added) {   // trafficseries' and this series' providers on platform_disc's one list
    b3native::platform::disc::AliasList l;
    CHECK(l.ask("PVEH/XCUS/CAR1.BGV").empty());
    l.add(&alias_test::first);
    l.add(&alias_test::second);
    l.add(&alias_test::first);   // once each
    l.add(nullptr);
    CHECK(l.size() == 2);
    CHECK(l.ask("PVEH/XCUS/CAR1.BGV") == "host:/a");     // both answer: the first added wins
    CHECK(l.ask("PVEH/COMP/CAR18.BGV") == "host:/b");    // only the second answers
    CHECK(l.ask("PVEH/COMP/CAR1.BGV").empty());
}

TEST(tuning_files_are_the_donors) {
    std::vector<ExtraCar> cars(1);
    cars[0].donor_cls = "COMP", cars[0].donor_car = "CAR7", cars[0].slot = 1;
    CHECK(extra_config(cars, {0}, "../Export/ValueDB/VehiclePhysics/XCUSCAR1.cfg") == "../Export/ValueDB/VehiclePhysics/COMPCAR7.cfg");
    CHECK(extra_config(cars, {0}, "../Export/ValueDB/AI/xcuscar1.CFG") == "../Export/ValueDB/AI/COMPCAR7.cfg");
    CHECK(extra_config(cars, {}, "../Export/ValueDB/AI/XCUSCAR1.cfg").empty());       // not racing
    CHECK(extra_config(cars, {0}, "../Export/ValueDB/AI/COMPCAR7.cfg").empty());
}

TEST(the_series_after_the_ones_to_its_left) {
    const std::vector<int> cb = {kCustomSeries, kBetaSeries}, c = {kCustomSeries}, b = {kBetaSeries};
    CHECK(extra_next_series(8, 8, c) == kCustomSeries);
    CHECK(extra_next_series(6, 8, c) == -1);   // not yet at the end: the others' steps
    CHECK(extra_next_series(kCustomSeries, 8, c) == -1);
    CHECK(extra_next_series(kCustomSeries, 8, cb) == kBetaSeries);
    CHECK(extra_next_series(4, 4, b) == kBetaSeries);
    CHECK(extra_next_series(4, 4, {}) == -1);
    CHECK(extra_prev_series(kCustomSeries, 8, cb) == 8);
    CHECK(extra_prev_series(kBetaSeries, 5, cb) == kCustomSeries);
    CHECK(extra_prev_series(kBetaSeries, 5, b) == 5);
    CHECK(extra_prev_series(8, 8, cb) == -1);
    CHECK(extra_series(10) && extra_series(11) && extra_series(12) && !extra_series(9) && !extra_series(7));
}

TEST(pages_of_nineteen) {   // carpacks: CUSTOM / BETA in pages of 19, each a series of its own (a page + 2 remembered: 21)
    std::vector<ExtraCar> cars(43);
    for (std::size_t i = 0; i < cars.size(); ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "p/%03zu/car.ini", i);
        cars[i].source = n, cars[i].donor_cls = "COMP", cars[i].donor_car = "CAR1";
        cars[i].series = i < 40 ? kCustomSeries : kBetaSeries;
    }
    extra_assign_slots(cars, kExtraCarsMax, nullptr);
    CHECK(cars[0].code() == 10 && cars[18].code() == 10 && cars[19].code() == 12 && cars[38].code() == 14 && cars[39].code() == 14);
    CHECK(cars[40].code() == 11 && cars[42].code() == 11 && cars[42].gtid_name() == "XBETCAR3" && cars[39].gtid_name() == "XCUSCAR40");
    CHECK(extra_series_cars(cars, 12).size() == 19 && extra_series_cars(cars, 14).size() == 2 && extra_series_cars(cars, 11).size() == 3);
    CHECK(kExtraPageCars + 2 <= kExtraListRoom && kExtraPagesMax == 11);   // (200 CUSTOM cars: 11 pages)
    const std::vector<int> order = extra_series_order(cars);
    CHECK(order == std::vector<int>({10, 12, 14, 11}));
    CHECK(extra_next_series(8, 8, order) == 10 && extra_next_series(10, 8, order) == 12 && extra_next_series(14, 8, order) == 11 &&
          extra_next_series(11, 8, order) == -1);
    CHECK(extra_prev_series(11, 8, order) == 14 && extra_prev_series(12, 8, order) == 10 && extra_prev_series(10, 8, order) == 8);
    CHECK(extra_series_kind(14) == kCustomSeries && extra_series_page(14) == 2 && extra_series_kind(13) == kBetaSeries && extra_series_page(13) == 1);
    CHECK(gtid_compress("XCUSCAR200") != gtid_compress("XCUSCAR20") && std::string("XCUSCAR200").size() <= 12);   // (12 characters)
    // the cap: 200 CUSTOM cars
    std::vector<ExtraCar> many(205), dropped;
    for (std::size_t i = 0; i < many.size(); ++i) many[i].source = std::to_string(1000 + i);
    extra_assign_slots(many, kCustomCarsMax, &dropped);
    CHECK(many.size() == 200 && dropped.size() == 5 && many.back().page == 10 && many.back().code() == 30);
}

TEST(words_in_the_games_languages) {
    CHECK(std::string(extra_words("").custom) == "CUSTOM" && std::string(extra_words("US").custom) == "CUSTOM");
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
        CHECK(std::string(extra_words(code).code) == code);
        CHECK(!extra_utf16(extra_words(code).custom).empty() && extra_utf16(extra_words(code).custom).size() <= 15);   // as TRAFFIC SPECIAL
    }
    CHECK(extra_utf16("B\xC3\x8ATA") == u"B\u00CATA");
    CHECK(extra_utf16("a\xC3") == u"a");               // cut short
    CHECK(extra_utf16("\xE2\x82\xAC") == u"\u20AC");
}

TEST(record_word_and_folder) {
    CHECK(extra_record_word() == (0x8002ull | kExtraRecordBit));
    CHECK(b3replay::records::options_folder(extra_record_word()) == "options-extracars");
    CHECK(b3replay::records::options_folder(extra_record_word(0x8002ull | 0x10000000ull)) == "options-trafficcars-extracars");
    CHECK(extra_hash({}) == 0xCBF29CE484222325ull && extra_hash({1, 2}) != extra_hash({2, 1}));
}

}  // namespace

TEST_MAIN("customseries: players' car mods as extra cars, the CUSTOM series (tools/native/extra_series.h)")
