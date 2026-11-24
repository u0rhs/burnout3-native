// test_betaimport -- betaimport: b3asset::beta (include/b3asset/betaimport.h) on synthetic ISO 9660 images and a synthetic
// game folder (tests/iso_writer.h, tests/synthetic_car.h), with test tables in place of the known discs and cars. No
// game data. A disc is known by its VEHICLES/VLIST.BIN and the presence of its cars' files; each car's file must have
// the known hash; the output folder gets one folder per car ("NN-<key>": car.bgv, car.ini), as the extra-cars code reads them.
#include "b3asset/betaimport.h"
#include "b3asset/car.h"
#include "b3asset/carconvert.h"
#include "iso_writer.h"
#include "synthetic_car.h"
#include "test.h"

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace synth;
using b3asset::Bytes;
namespace beta = b3asset::beta;

namespace {

const fs::path kDir = "build/test_betaimport.d";

Buf vlist(std::uint8_t seed) {
    Buf v(4096, 0);
    v[0] = 6;
    v[4] = seed;
    return v;
}
std::uint64_t h(const Buf& b) { return beta::fnv1a64(b.data(), b.size()); }
bool put(const fs::path& p, const Buf& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(f);
}
std::string text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
Buf bytes(const fs::path& p) {
    const std::string s = text(p);
    return Buf(s.begin(), s.end());
}

// two discs (A, and B with one car that replaces A's), a game folder with the donors
struct World {
    Buf old14 = old_car(0x14), old15 = old_car(0x15), donor = donor_car();
    Buf vla = vlist(1), vlb = vlist(2);
    std::vector<beta::Disc> discs;
    std::vector<beta::Car> cars;
    fs::path game = kDir / "game";
    World() {
        discs = {{"discA", "disc A", h(vla)}, {"discB", "disc B", h(vlb)}};
        Buf traffic = old15;   // a .BTV on the disc: the same conversion
        cars = {
            {"discA", "HEVY", "CAR4", false, h(old14), "HEVY", "CAR4", 6, "Test SUV", ""},
            {"discA", "HEVY", "CAR35", true, h(traffic), "HEVY", "CAR5", 6, "Test Van", ""},
            {"discA", "MSCL", "CAR2", false, h(old15), "MSCL", "CAR2", 1, "Test Muscle", "discb-mscl-car2"},
            {"discA", "COMP", "CAR9", false, 0x1234ull, "COMP", "CAR9", 0, "Wrong Hash", ""},
            {"discB", "MSCL", "CAR2", false, h(old14), "MSCL", "CAR2", 1, "Test Muscle", ""},
        };
        beta::set_test_tables(&discs, &cars);
        fs::remove_all(kDir);
        for (const char* d : {"PVEH/HEVY/CAR4.BGV", "PVEH/HEVY/CAR5.BGV", "PVEH/MSCL/CAR2.BGV", "PVEH/COMP/CAR9.BGV"}) put(game / d, donor);
    }
    ~World() { beta::set_test_tables(nullptr, nullptr); }
    fs::path disc_a(const std::string& name) {
        isotest::Writer w;
        w.file("VEHICLES/VLIST.BIN", vla);
        w.file("VEHICLES/HEVY/CAR4.BGV", old14);
        w.file("VEHICLES/HEVY/CAR35.BTV", old15);
        w.file("VEHICLES/MSCL/CAR2.BGV", old15);
        w.file("VEHICLES/COMP/CAR9.BGV", old15);   // not the known file
        const fs::path p = kDir / name;
        fs::create_directories(kDir);
        return w.write(p.string()) ? p : fs::path();
    }
    fs::path disc_b(const std::string& name) {
        isotest::Writer w;
        w.file("VEHICLES/VLIST.BIN", vlb);
        w.file("VEHICLES/MSCL/CAR2.BGV", old14);
        const fs::path p = kDir / name;
        return w.write(p.string()) ? p : fs::path();
    }
};

}  // namespace

TEST(a_known_disc_imports_its_cars) {
    World w;
    const fs::path img = w.disc_a("a.iso"), out = kDir / "cars/beta";
    std::string why;
    const beta::Disc* d = beta::identify(img, &why);
    CHECK(d && std::string(d->key) == "discA");
    int steps = 0;
    const beta::Result r = beta::import_cars(img, w.game, out, [](const std::string&, void* n) { ++*static_cast<int*>(n); }, &steps);
    CHECK(r.ok() && r.disc == d && steps == 4);
    CHECK(r.imported.size() == 3 && r.skipped.size() == 1);
    CHECK(r.skipped.size() == 1 && r.skipped[0].find("disca-comp-car9: VEHICLES/COMP/CAR9.BGV differs from the known file") == 0);
    for (const char* k : {"01-disca-hevy-car4", "02-disca-hevy-car35", "03-disca-mscl-car2"}) {
        const Buf b = bytes(out / k / "car.bgv");
        CHECK(!b.empty() && b3asset::car_file_error(Bytes{b.data(), b.size()}).empty());
    }
    CHECK(!fs::exists(out / "04-disca-comp-car9") && !fs::exists(out / ".01-disca-hevy-car4.partial"));
    // the converted file is convert_prototype_car's
    Buf want;
    CHECK(b3asset::convert_prototype_car(Bytes{w.old14.data(), w.old14.size()}, Bytes{w.donor.data(), w.donor.size()}, want).empty());
    CHECK(bytes(out / "01-disca-hevy-car4/car.bgv") == want);
    // car.ini: the extra-cars code's keys (name, donor, model) and the import's
    const std::string ini = text(out / "01-disca-hevy-car4/car.ini");
    char hb[64];
    std::snprintf(hb, sizeof hb, "hash = %016llx\n", static_cast<unsigned long long>(h(want)));
    CHECK(ini.find("\nname = Test SUV\ndonor = HEVY/CAR4\nmodel = car.bgv\nkey = disca-hevy-car4\nsource = discA\n"
                   "from = VEHICLES/HEVY/CAR4.BGV\nseries = 6\nsize = " + std::to_string(want.size()) + "\n" + hb) != std::string::npos);
    const std::string van = text(out / "02-disca-hevy-car35/car.ini");
    CHECK(van.find("donor = HEVY/CAR5\n") != std::string::npos && van.find("from = VEHICLES/HEVY/CAR35.BTV\n") != std::string::npos);
    // the launcher's list, in folder order
    const auto list = beta::imported(out);
    CHECK(list.size() == 3 && list[0].name == "Test SUV" && list[0].key == "disca-hevy-car4" && list[2].folder == "03-disca-mscl-car2");
}

TEST(a_later_disc_replaces_the_same_car) {
    World w;
    const fs::path a = w.disc_a("a2.iso"), b = w.disc_b("b2.iso"), out = kDir / "cars2";
    CHECK(beta::import_cars(a, w.game, out).ok());
    CHECK(fs::exists(out / "03-disca-mscl-car2/car.bgv"));
    const beta::Result rb = beta::import_cars(b, w.game, out);
    CHECK(rb.ok() && rb.imported.size() == 1);
    CHECK(fs::exists(out / "05-discb-mscl-car2/car.bgv") && !fs::exists(out / "03-disca-mscl-car2"));   // the earlier one removed
    CHECK(fs::exists(out / "01-disca-hevy-car4/car.bgv"));   // disc A's other cars stay
    CHECK(beta::imported(out).size() == 3);
    // importing A again: its muscle car is skipped (B's is there)
    const beta::Result ra = beta::import_cars(a, w.game, out);
    CHECK(ra.ok() && ra.imported.size() == 2);
    bool said = false;
    for (const std::string& s : ra.skipped) said |= s.find("disca-mscl-car2: the same car from a later disc") == 0;
    CHECK(said && !fs::exists(out / "03-disca-mscl-car2"));
}

TEST(other_images_refused_with_a_reason) {
    World w;
    fs::create_directories(kDir);
    std::string why;
    {   // the game's own layout
        isotest::Writer x;
        x.file("PVEH/VLIST.BIN", w.vla);
        CHECK(x.write((kDir / "game.iso").string()));
        CHECK(!beta::identify(kDir / "game.iso", &why) && why.find("the game's own layout") != std::string::npos);
    }
    {   // a car list nobody knows
        isotest::Writer x;
        x.file("VEHICLES/VLIST.BIN", vlist(9));
        CHECK(x.write((kDir / "other.iso").string()));
        CHECK(!beta::identify(kDir / "other.iso", &why) && why.find("not one of the known prototypes") != std::string::npos);
    }
    {   // disc A's car list without its cars (an earlier demo)
        isotest::Writer x;
        x.file("VEHICLES/VLIST.BIN", w.vla);
        x.file("VEHICLES/SPRT/CAR1.BGV", w.old15);
        CHECK(x.write((kDir / "early.iso").string()));
        CHECK(!beta::identify(kDir / "early.iso", &why) && why.find("none of its cut cars") != std::string::npos);
        const beta::Result r = beta::import_cars(kDir / "early.iso", w.game, kDir / "cars3");
        CHECK(!r.ok() && r.imported.empty() && !fs::exists(kDir / "cars3"));
    }
    put(kDir / "notadisc.bin", Buf(5000, 7));
    CHECK(!beta::identify(kDir / "notadisc.bin", &why) && !why.empty());
    CHECK(!beta::identify(kDir / "missing.iso", &why) && !why.empty());
}

TEST(a_missing_donor_skips_that_car) {
    World w;
    fs::remove(w.game / "PVEH/HEVY/CAR5.BGV");
    const beta::Result r = beta::import_cars(w.disc_a("a3.iso"), w.game, kDir / "cars4");
    CHECK(r.ok() && r.imported.size() == 2);
    bool said = false;
    for (const std::string& s : r.skipped) said |= s.find("disca-hevy-car35: the game's PVEH/HEVY/CAR5.BGV (its donor) is missing") == 0;
    CHECK(said);
}

TEST(oversized_records_refused_before_reading) {   // Fable: a car file or VLIST.BIN larger than such a file can be
    World w;
    isotest::Writer x;
    x.file("VEHICLES/VLIST.BIN", w.vla);
    x.file("VEHICLES/HEVY/CAR4.BGV", Buf(0x80001, 1));   // over the car file ceiling
    x.file("VEHICLES/HEVY/CAR35.BTV", w.old15);
    fs::create_directories(kDir);
    CHECK(x.write((kDir / "big.iso").string()));
    const beta::Result r = beta::import_cars(kDir / "big.iso", w.game, kDir / "cars5");
    bool said = false;
    for (const std::string& s : r.skipped) said |= s.find("disca-hevy-car4: VEHICLES/HEVY/CAR4.BGV is 524,289 bytes, larger than") == 0;
    CHECK(r.ok() && said && r.imported.size() == 1);
    isotest::Writer y;
    y.file("VEHICLES/VLIST.BIN", Buf(65537, 0));
    CHECK(y.write((kDir / "bigvlist.iso").string()));
    std::string why;
    CHECK(!beta::identify(kDir / "bigvlist.iso", &why) && why.find("VLIST.BIN is 65,537 bytes") != std::string::npos);
}

// the owner (2026-11-22): car files one by one and folders of them
TEST(car_files_known_unknown_renamed_and_others) {
    World w;
    const fs::path in = kDir / "files", out = kDir / "cars6";
    Buf other = w.old15;
    other[0x14] ^= 0x5A;   // another prototype file (not in the table)
    put(in / "VEHICLES/HEVY/CAR4.BGV", w.old14);   // the table's (its hash)
    put(in / "mine/HEVY/CAR5.BGV", other);         // unknown: its path names HEVY/CAR5
    put(in / "renamed.bgv", other);                // unknown, the path does not say
    put(in / "retail/CAR1.BGV", synthetic_car());  // the game's own layout: a mod car
    put(in / "junk.btv", Buf(0x2000, 9));
    put(in / "big.bgv", Buf(0x80001, 1));
    const std::vector<fs::path> files = {in / "VEHICLES/HEVY/CAR4.BGV", in / "mine/HEVY/CAR5.BGV", in / "renamed.bgv", in / "retail/CAR1.BGV",
                                         in / "junk.btv", in / "big.bgv"};
    beta::Result r = beta::import_files(files, w.game, out);
    CHECK(r.imported.size() == 2 && r.skipped.size() == 4);
    CHECK(fs::exists(out / "01-disca-hevy-car4/car.bgv"));
    const std::string key = "hevy-car5-" + [&] { char b[24]; std::snprintf(b, sizeof b, "%016llx", (unsigned long long)h(other)); return std::string(b).substr(0, 8); }();
    const std::string ini = text(out / ("50-" + key) / "car.ini");
    CHECK(ini.find("name = Heavy 5 (prototype)\ndonor = HEVY/CAR5\nmodel = car.bgv\nkey = " + key + "\nsource = file\nfrom = CAR5.BGV\nseries = 6\n") !=
          std::string::npos);
    CHECK(r.needs_donor.size() == 1 && r.needs_donor[0] == in / "renamed.bgv");
    auto said = [&](const std::string& start, const std::string& part) {
        for (const std::string& x : r.skipped)
            if (x.rfind(start, 0) == 0 && x.find(part) != std::string::npos) return true;
        return false;
    };
    CHECK(said("renamed.bgv: ", "does not say which car"));
    CHECK(said("CAR1.BGV: ", "CUSTOM series"));
    CHECK(said("junk.btv: ", "not a vehicle file"));
    CHECK(said("big.bgv: ", "larger than"));
    // the player chose the renamed file's car: imported with it
    r = beta::import_files({in / "renamed.bgv"}, w.game, out, nullptr, nullptr, {{in / "renamed.bgv", "HEVY/CAR4"}});
    CHECK(r.imported.size() == 1 && r.needs_donor.empty());
    CHECK(fs::exists(out / ("50-hevy-car4-" + key.substr(10)) / "car.bgv"));
    // a donor the game has no file of: chosen again
    r = beta::import_files({in / "renamed.bgv"}, w.game, out, nullptr, nullptr, {{in / "renamed.bgv", "HEVY/CAR9"}});
    CHECK(r.imported.empty() && r.needs_donor.size() == 1 && r.skipped[0].find("the game has no HEVY/CAR9") != std::string::npos);
    // a donor string that is not "<class>/CARn" (Fable: nothing else may reach the output folder's name): asked again
    for (const char* bad : {"HEVY/../../x", "HEVY/CAR4/..", "HEVY/CARX", "HEVY/", "../HEVY/CAR4", "HEVY/CAR04"}) {
        r = beta::import_files({in / "renamed.bgv"}, w.game, out, nullptr, nullptr, {{in / "renamed.bgv", bad}});
        CHECK(r.imported.empty() && r.needs_donor.size() == 1);
    }
    CHECK(!fs::exists(kDir / "x") && !fs::exists(out.parent_path() / "x"));
    // importing the same car again replaces its folder (the earlier one moved aside, then deleted): one folder, no leftovers
    r = beta::import_files({in / "renamed.bgv"}, w.game, out, nullptr, nullptr, {{in / "renamed.bgv", "HEVY/CAR4"}});
    CHECK(r.imported.size() == 1);
    int dots = 0;
    for (const auto& e : fs::directory_iterator(out)) dots += e.path().filename().string()[0] == '.';
    CHECK(dots == 0);
    const auto ch = beta::donor_choices(w.game, "HEVY");
    CHECK(ch.size() == 2 && ch[0] == "HEVY/CAR4" && ch[1] == "HEVY/CAR5");
    CHECK(beta::donor_from_path("x/hevy/car12.bgv") == "HEVY/CAR12" && beta::donor_from_path("x/TRAF/CAR1.BTV").empty() &&
          beta::donor_from_path("x/HEVY/CAR0.BGV").empty() && beta::donor_from_path("renamed.bgv").empty());
}

TEST(a_folder_of_car_files) {
    World w;
    const fs::path in = kDir / "tree", out = in / "out";   // (the output inside the folder: not read back)
    Buf other = w.old15;
    other[0x14] ^= 0x33;
    put(in / "VEHICLES/HEVY/CAR4.BGV", w.old14);
    put(in / "VEHICLES/HEVY/CAR35.BTV", w.old15);   // the table's traffic van (its hash = old15's)
    put(in / "deep/er/MSCL/CAR7.BGV", other);       // unknown, and the game has no MSCL/CAR7: choose
    put(in / "notes.txt", Buf(10, 'a'));
    put(in / "VEHICLES/VLIST.BIN", w.vla);
    beta::Result r = beta::import_folder(in, w.game, out);
    CHECK(r.ok() && r.imported.size() == 2 && r.needs_donor.size() == 1 && r.other_files == 2 && !r.walk_stopped);
    r = beta::import_folder(in, w.game, out);   // again: the output folder is not taken as input
    CHECK(r.imported.size() == 2);
    CHECK(beta::imported(out).size() == 2);
    r = beta::import_folder(kDir / "game", w.game, kDir / "cars7");   // the game's own files: mod cars, nothing imported
    CHECK(r.imported.empty() && r.skipped.size() == 4);
    fs::create_directories(kDir / "empty");
    r = beta::import_folder(kDir / "empty", w.game, kDir / "cars7");
    CHECK(!r.ok() && r.error.find("no car files") != std::string::npos);
}

// betabytes: a car pack's files given as bytes and their path in the pack (the launcher's "Import car pack...")
TEST(car_bytes_from_a_pack) {
    World w;
    const fs::path out = kDir / "cars8";
    Buf other = w.old15;
    other[0x14] ^= 0x5A;   // a prototype file the table does not know
    const auto key8 = [&](const Buf& b) { char t[24]; std::snprintf(t, sizeof t, "%016llx", (unsigned long long)h(b)); return std::string(t).substr(0, 8); };
    // the table's car (its bytes): its name and folder, whatever the pack calls it
    beta::Result r = beta::import_bytes("PVEH/HEVY/CAR35.BTV", w.old15, w.game, out);
    CHECK(r.ok() && r.imported.size() == 1 && r.imported[0].rfind("disca-hevy-car35: Test Van", 0) == 0 && fs::exists(out / "02-disca-hevy-car35/car.bgv"));
    // an unknown prototype .BGV: the donor its path names
    r = beta::import_bytes("PVEH/MSCL/CAR2.BGV", other, w.game, out);
    CHECK(r.imported.size() == 1 && text(out / ("50-mscl-car2-" + key8(other)) / "car.ini").find("name = Muscle 2 (prototype)\ndonor = MSCL/CAR2\n") != std::string::npos);
    // an unknown traffic vehicle's .BTV (a slot the TRAFFIC series offers): trafficseries' donor, named by its slot
    r = beta::import_bytes("PVEH/HEVY/CAR35.BTV", other, w.game, out);
    CHECK(r.imported.size() == 1 && r.needs_donor.empty());
    const std::string tini = text(out / ("50-hevy-car35-" + key8(other)) / "car.ini");
    CHECK(tini.find("name = Heavy 35 (prototype)\ndonor = HEVY/CAR5\n") != std::string::npos && tini.find("from = CAR35.BTV\nseries = 6\n") != std::string::npos);
    // a trailer's slot is not offered: the player chooses (needs_donor holds the pack path)
    r = beta::import_bytes("PVEH/HEVY/CAR21.BTV", other, w.game, out);
    CHECK(r.imported.empty() && r.needs_donor.size() == 1 && r.needs_donor[0] == fs::path("PVEH/HEVY/CAR21.BTV"));
    r = beta::import_bytes("PVEH/HEVY/CAR21.BTV", other, w.game, out, "HEVY/CAR4");
    CHECK(r.imported.size() == 1 && fs::exists(out / ("50-hevy-car4-" + key8(other)) / "car.bgv"));
    // a chosen donor is never replaced by the traffic one, and is checked as before
    r = beta::import_bytes("PVEH/HEVY/CAR35.BTV", other, w.game, out, "HEVY/../x");
    CHECK(r.imported.empty() && r.needs_donor.size() == 1);
    // the game's own layout (a pack's changed retail car: the CUSTOM series), junk, too big, an empty path
    r = beta::import_bytes("PVEH/COMP/CAR1.BGV", synthetic_car(), w.game, out);
    CHECK(r.imported.empty() && r.skipped.size() == 1 && r.skipped[0].find("CUSTOM series") != std::string::npos);
    r = beta::import_bytes("PVEH/HEVY/CAR4.BGV", Buf(0x2000, 9), w.game, out);
    CHECK(r.imported.empty() && r.skipped.size() == 1);
    r = beta::import_bytes("PVEH/HEVY/CAR4.BGV", Buf(0x80001, 1), w.game, out);
    CHECK(r.imported.empty() && r.skipped.size() == 1 && r.skipped[0] == "CAR4.BGV: is 524,289 bytes, larger than such a file can be (at most 524,288)");
    r = beta::import_bytes("", other, w.game, out);
    CHECK(r.imported.empty() && r.needs_donor.size() == 1);
}

// betabytes (the owner's no-duplicates rule): a table car a car pack wrote (its folder, car.ini with "pack = ...") is
// replaced by the same car imported later from the disc or as a file, not kept beside it
TEST(a_pack_copy_is_replaced_by_the_disc_import) {
    World w;
    const fs::path img = w.disc_a("p.iso"), out = kDir / "cars9";
    const beta::Spec sp = beta::spec_of(w.cars[0]);   // the SUV, as the pack import writes it
    put(out / sp.folder / "car.bgv", w.donor);
    {
        std::ofstream f(out / sp.folder / "car.ini", std::ios::binary);
        f << beta::car_ini(sp, w.donor.size(), h(w.donor)) << "pack = Example Pack\n";
    }
    auto list = beta::imported(out);
    CHECK(list.size() == 1 && list[0].folder == "01-disca-hevy-car4" && list[0].pack == "Example Pack" && list[0].name == "Test SUV");
    CHECK(beta::import_cars(img, w.game, out).imported.size() == 3);
    list = beta::imported(out);
    CHECK(list.size() == 3 && list[0].folder == "01-disca-hevy-car4" && list[0].pack.empty());
    CHECK(text(out / sp.folder / "car.ini").find("pack =") == std::string::npos && bytes(out / sp.folder / "car.bgv") != w.donor);
    int dots = 0;
    for (const auto& e : fs::directory_iterator(out)) dots += e.path().filename().string()[0] == '.';
    CHECK(dots == 0);
    // and as a file (import_bytes): the same
    {
        std::ofstream f(out / sp.folder / "car.ini", std::ios::binary);
        f << beta::car_ini(sp, w.donor.size(), h(w.donor)) << "pack = Example Pack\n";
    }
    CHECK(beta::import_bytes("HEVY/CAR4.BGV", w.old14, w.game, out).imported.size() == 1);
    list = beta::imported(out);
    CHECK(list.size() == 3 && list[0].pack.empty());
}

TEST(the_known_tables) {   // the real tables: keys unique, every car's disc known, replaced_by names a car of a later disc
    beta::set_test_tables(nullptr, nullptr);
    std::set<std::string> keys;
    for (const beta::Car& c : beta::cars()) {
        CHECK(keys.insert(c.key()).second);
        CHECK(std::strlen(c.name) <= 24);   // the extra-cars code's limit
        bool disc = false;
        for (const beta::Disc& d : beta::discs()) disc |= c.disc == std::string(d.key);
        CHECK(disc && c.series >= 0 && c.series <= 6 && *c.name);
    }
    for (const beta::Car& c : beta::cars())
        if (*c.replaced_by) CHECK(keys.count(c.replaced_by) == 1);
    CHECK(beta::cars().size() == 11 && beta::discs().size() == 2);
}

TEST_MAIN("betaimport: prototype cars imported from a disc image (b3asset::beta)")
