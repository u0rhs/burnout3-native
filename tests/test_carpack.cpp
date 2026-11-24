// carpacks (src/b3launcher/carpack.h): "Import car pack..." -- a replacement-style car pack sorted by the owner's rules
// (no duplicates across the game, BETA and CUSTOM) and written as extra cars. Synthetic cars only (tests/synthetic_car.h).
#include "test.h"
#include "synthetic_car.h"

#include "../src/b3launcher/carpack.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace b3launcher::carpack;

synth::Buf game_car(std::uint8_t tint) {   // a synthetic car of the game's layout, its header's size its own
    synth::Buf b = synth::donor_car();
    synth::p32(b, 8, std::uint32_t(b.size()));
    b[b.size() - 1] = tint;   // (bytes past the model: another file, the same geometry)
    return b;
}
synth::Buf bank(std::uint8_t tint) {
    synth::Buf b(0x9000, 0);
    b[0] = 0x09, b[1] = 0x08, b[9] = 0x08, b[12] = 0x2C, b[0x100] = tint;
    return b;
}
struct Fixture {
    fs::path root = fs::path("build") / "test_carpack";
    fs::path custom = root / "custom", beta = root / "beta";
    std::map<std::string, synth::Buf> disc;
    Fixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(custom);
        fs::create_directories(beta);
        disc["PVEH/COMP/CAR1.BGV"] = game_car(1);
        disc["PVEH/COMP/CAR7.BGV"] = game_car(7);
        disc["PVEH/COMP/CAR7.HWD"] = bank(7);
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    DiscRead reader() const {
        return [this](const std::string& p, std::vector<std::uint8_t>& b) {
            const auto it = disc.find(p);
            if (it == disc.end()) return false;
            b = it->second;
            return true;
        };
    }
};
const Item* find(const Plan& p, const std::string& slot) {
    for (const Item& i : p.items)
        if (i.slot == slot) return &i;
    return nullptr;
}

TEST(the_rules) {
    Fixture f;
    const std::uint64_t print = b3asset::car_fingerprint({f.disc["PVEH/COMP/CAR7.BGV"].data(), f.disc["PVEH/COMP/CAR7.BGV"].size()});
    CHECK(print != 0);
    const std::vector<BetaPrint> prints = {{"jun21-hevy-car4", print ^ 1}};   // (no pack car has this geometry)
    std::vector<InFile> files = {
        {"Pack/PVEH/COMP/CAR1.BGV", game_car(1)},          // 1: the disc's own
        {"Pack/PVEH/COMP/CAR7.BGV", game_car(70)},         // 5: changed: a CUSTOM car
        {"Pack/PVEH/COMP/CAR7.HWD", bank(70)},             //    its own engine bank (the disc's differs)
        {"Pack/PVEH/COMP/CAR7.LWD", bank(7)},              //    (no disc bank to compare: taken too)
        {"Pack/PVEH/SPRT/CAR3.BGV", synth::old_car(0x15)}, // 4: an old-layout prototype file
        {"Pack/PVEH/TSPC/CAR1.BTV", game_car(5)},          // the trams: never
        {"Pack/PVEH/vlist.bin", {1, 2}},
        {"Pack/VDB.XML", {1, 2}},
        {"Pack/Guide.txt", {1}},
    };
    const Plan p = plan(files, "My Pack", f.reader(), [](const std::string& s) { return s == "COMP/CAR7" ? std::string("Assassin Compact") : ""; },
                        f.custom, f.beta, prints, {});
    CHECK(p.pack == "My Pack" && p.left_out.size() == 2);
    const Item* same = find(p, "COMP/CAR1");
    CHECK(same && same->dest == Dest::kSkip && same->why == "already in the game");
    const Item* mod = find(p, "COMP/CAR7");
    CHECK(mod && mod->dest == Dest::kCustom && mod->name == "Assassin Compact (My Pack)" && mod->hwd != SIZE_MAX && mod->lwd != SIZE_MAX &&
          mod->group == Group::kOther && mod->chosen);
    const Item* proto = find(p, "SPRT/CAR3");
    CHECK(proto && proto->dest == Dest::kPrototype);
    CHECK(!find(p, "TSPC/CAR1"));
    // written: one CUSTOM car folder the scan reads; a second import of the same pack: already imported
    const Done d = write(p, f.custom, f.beta);
    CHECK(d.imported.size() == 1);
    const b3native::ExtraScan s = b3native::extra_scan(f.custom);
    CHECK(s.cars.size() == 1 && s.cars[0].name == "ASSASSIN COMPACT (MY PACK)" && s.cars[0].donor_name() == "COMPCAR7" && !s.cars[0].hwd.empty());
    const Plan again = plan(files, "Other Pack", f.reader(), nullptr, f.custom, f.beta, prints, {});
    const Item* dup = find(again, "COMP/CAR7");
    CHECK(dup && dup->dest == Dest::kSkip && dup->why == "already imported");   // rule 3 (the same bytes from another pack)
}

TEST(beta_cars_by_geometry_once) {
    Fixture f;
    const synth::Buf a = game_car(40), b = game_car(41);   // two conversions of one car: other bytes, the same geometry
    const std::uint64_t print = b3asset::car_fingerprint({a.data(), a.size()});
    CHECK(print == b3asset::car_fingerprint({b.data(), b.size()}));
    const std::vector<BetaPrint> prints = {{"jun21-hevy-car4", print}};
    const Plan p = plan({{"P/PVEH/HEVY/CAR4.BGV", a}, {"P/PVEH/HEVY/CAR9.BGV", b}}, "P", f.reader(), nullptr, f.custom, f.beta, prints, {});
    CHECK(p.items.size() == 2 && p.items[0].dest == Dest::kBeta && p.items[0].beta_key == "jun21-hevy-car4" && p.items[0].name == "SUV");
    CHECK(p.items.size() == 2 && p.items[1].dest == Dest::kSkip && p.items[1].why == "already a BETA car (SUV)");   // (once a pack)
    const Done d = write(p, f.custom, f.beta);
    CHECK(d.imported.size() == 1);
    const auto have = b3asset::beta::imported(f.beta);
    CHECK(have.size() == 1 && have[0].key == "jun21-hevy-car4" && have[0].name == "SUV");
    std::ifstream ini(f.beta / have[0].folder / "car.ini");
    const std::string text((std::istreambuf_iterator<char>(ini)), std::istreambuf_iterator<char>());
    CHECK(text.find("pack = P\n") != std::string::npos);   // (a disc import replaces it)
    // a later pack with the car: already a BETA car
    const Plan later = plan({{"Q/PVEH/HEVY/CAR4.BGV", game_car(42)}}, "Q", f.reader(), nullptr, f.custom, f.beta, prints, {});
    CHECK(later.items.size() == 1 && later.items[0].why == "already a BETA car (SUV)");
}

// extrastart (pre4): the BETA cars a later disc has too -- the game's scan keeps a list of them beside betaimport's table
TEST(beta_replaced_pairs_match_the_table) {
    std::set<std::pair<std::string, std::string>> table, list;
    for (const b3asset::beta::Car& c : b3asset::beta::cars())
        if (*c.replaced_by) table.insert({c.key(), c.replaced_by});
    for (const auto& k : b3native::kBetaReplaced) list.insert({k[0], k[1]});
    CHECK(!table.empty() && table == list);
    for (const b3asset::beta::Car& c : b3asset::beta::cars())   // its car.ini says so (the game hides it beside the later one)
        if (*c.replaced_by) CHECK(b3asset::beta::car_ini(b3asset::beta::spec_of(c), 1, 1).find("replaced_by = " + std::string(c.replaced_by) + "\n") != std::string::npos);
}

// pre4: June 21's Muscle 2 imported (it replaced June 8's): a pack's conversion of June 8's is the same car again
TEST(earlier_copy_of_a_beta_car_skipped) {
    Fixture f;
    const synth::Buf a = game_car(60), b = game_car(61);   // (the same geometry)
    const std::uint64_t print = b3asset::car_fingerprint({a.data(), a.size()});
    const Plan p = plan({{"P/PVEH/MSCL/CAR2.BGV", a}}, "P", f.reader(), nullptr, f.custom, f.beta, {{"jun21-mscl-car2", print}}, {});
    CHECK(p.items.size() == 1 && p.items[0].dest == Dest::kBeta);
    CHECK(write(p, f.custom, f.beta).imported.size() == 1);
    const Plan q = plan({{"Q/PVEH/MSCL/CAR2.BGV", b}}, "Q", f.reader(), nullptr, f.custom, f.beta, {{"jun08-mscl-car2", print}}, {});
    CHECK(q.items.size() == 1 && q.items[0].dest == Dest::kSkip && q.items[0].why == "already a BETA car (Muscle 2)");
}

// pre4: a pack that carries a disc's own prototype file (Mod Beta's HEVY/CAR35.BTV, the Delivery Van) when that car is
// imported already: skipped, not converted again
TEST(prototype_file_of_an_imported_beta_car_skipped) {
    Fixture f;
    const synth::Buf old = synth::old_car(0x15);
    const std::uint64_t h = b3asset::beta::fnv1a64(old.data(), old.size());
    static const std::vector<b3asset::beta::Disc> discs = {{"jun21", "the test disc", 1}};
    static const std::vector<b3asset::beta::Car> cars = {{"jun21", "HEVY", "CAR35", true, h, "HEVY", "CAR5", 6, "Delivery Van", ""}};
    b3asset::beta::set_test_tables(&discs, &cars);
    const fs::path dir = f.beta / "01-jun21-hevy-car35";
    fs::create_directories(dir);
    std::ofstream(dir / "car.ini") << "name = Delivery Van\ndonor = HEVY/CAR5\nmodel = car.bgv\nkey = jun21-hevy-car35\n";
    std::ofstream(dir / "car.bgv") << "x";
    const Plan p = plan({{"P/PVEH/HEVY/CAR35.BTV", old}}, "P", f.reader(), nullptr, f.custom, f.beta, {}, {});
    CHECK(p.items.size() == 1 && p.items[0].dest == Dest::kSkip && p.items[0].why == "already a BETA car (Delivery Van)");
    std::error_code ec;
    fs::remove_all(dir, ec);
    const Plan q = plan({{"P/PVEH/HEVY/CAR35.BTV", old}}, "P", f.reader(), nullptr, f.custom, f.beta, {}, {});
    CHECK(q.items.size() == 1 && q.items[0].dest == Dest::kPrototype);   // (not imported: the converter, as before)
    b3asset::beta::set_test_tables(nullptr, nullptr);
}

// pre4: June 8's Muscle 2 left beside June 21's (its removal failed on Windows; a car.ini from before extrastart, no
// replaced_by): listed once by the launcher and the game, and the launcher's tidy removes it
TEST(earlier_copy_left_beside_its_later_version) {
    Fixture f;
    const std::pair<const char*, const char*> both[] = {{"07-jun21-mscl-car2", "jun21-mscl-car2"}, {"11-jun08-mscl-car2", "jun08-mscl-car2"}};
    std::uint8_t tint = 70;
    for (const auto& [folder, key] : both) {
        fs::create_directories(f.beta / folder);
        std::ofstream(f.beta / folder / "car.ini") << "name = Muscle 2\ndonor = MSCL/CAR2\nmodel = car.bgv\nkey = " << key << "\n";
        const synth::Buf c = game_car(tint++);
        std::ofstream(f.beta / folder / "car.bgv", std::ios::binary).write(reinterpret_cast<const char*>(c.data()), std::streamsize(c.size()));
    }
    const auto listed = b3asset::beta::imported(f.beta);
    CHECK(listed.size() == 1 && listed[0].key == "jun21-mscl-car2");
    const b3native::ExtraScan s = b3native::extra_scan_all(f.custom, f.beta);
    CHECK(s.cars.size() == 1 && s.cars[0].key == "jun21-mscl-car2");
    bool said = false;
    for (const auto& r : s.refused) said = said || r.second.find("an earlier copy of") != std::string::npos;
    CHECK(said);
    const auto t = b3asset::beta::tidy_replaced(f.beta);
    CHECK(t.size() == 1 && !fs::exists(f.beta / "11-jun08-mscl-car2") && fs::exists(f.beta / "07-jun21-mscl-car2"));
    CHECK(b3asset::beta::tidy_replaced(f.beta).empty());   // (nothing left to do)
}

TEST(known_pack_names_and_groups) {
    Fixture f;
    const synth::Buf c = game_car(90);
    const std::vector<KnownPack> packs = {{"Known", "Someone", {{b3native::extra_hash(c), "MSCL/CAR7", "Assassin Muscle Mod", Group::kNewModel}}}};
    const synth::Buf early = game_car(91);
    const std::vector<KnownPack> packs2 = {{"Known", "Someone", {{b3native::extra_hash(c), "MSCL/CAR7", "Assassin Muscle Mod", Group::kNewModel},
                                                                 {b3native::extra_hash(early), "MSCL/CAR1", "Muscle Type 1 Beta", Group::kEarly}}}};
    const Plan p = plan({{"x/MSCL/CAR7.BGV", c}, {"x/MSCL/CAR1.BGV", early}}, "x", f.reader(), nullptr, f.custom, f.beta, {}, packs2);
    CHECK(p.by == "Someone" && p.pack == "x");   // (the label: the player's own zip / folder name)
    CHECK(p.items.size() == 2 && p.items[0].name == "Assassin Muscle Mod" && p.items[0].chosen);
    CHECK(p.items.size() == 2 && p.items[1].group == Group::kEarly && !p.items[1].chosen);   // early versions: off by default
    std::string cls, car, ext;
    CHECK(car_path("a/b/hevy/car36.bgv", &cls, &car, &ext) && cls == "HEVY" && car == "CAR36" && ext == "BGV");
    CHECK(pack_folder_name(" ../a:b* ") == "ab" && pack_folder_name("...") == "Car pack");
    // the shipped table: one pack, 59 cars, HEVY/CAR7 named as a truck
    const auto& k = known_packs();
    CHECK(k.size() == 1 && k[0].cars.size() == 59);
    bool truck = false;
    for (const KnownCar& kc : k[0].cars) truck = truck || (std::string(kc.slot) == "HEVY/CAR7" && std::string(kc.name) == "Truck Beta");
    CHECK(truck && beta_prints().size() == 11);
}

}  // namespace

TEST_MAIN("carpacks: Import car pack (src/b3launcher/carpack.h)")
