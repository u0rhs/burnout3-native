// betaimport.cpp -- the BETA SERIES import (include/b3asset/betaimport.h).
#include "b3asset/betaimport.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

#include "b3asset/car.h"   // car_file_error
#include "b3asset/carconvert.h"
#include "b3asset/discfs.h"
#include "b3port/fileio.h"
#include "../../tools/native/traffic_series.h"   // betabytes: a traffic vehicle's donor race car (trafficseries' table)

namespace b3asset::beta {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string hex64(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}
std::string thousands(std::size_t n) {
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(std::size_t(i), ",");
    return s;
}
const std::vector<Disc>* g_test_discs = nullptr;
const std::vector<Car>* g_test_cars = nullptr;

}  // namespace

void set_test_tables(const std::vector<Disc>* d, const std::vector<Car>* c) {
    g_test_discs = d;
    g_test_cars = c;
}

const std::vector<Disc>& discs() {
    if (g_test_discs) return *g_test_discs;
    static const std::vector<Disc> d = {
        {"jun08", "the June 8, 2004 PS2 prototype (Pre-Alpha)", 0x0153f0851da5d678ull},
        {"jun21", "the June 21, 2004 PS2 demo", 0xa854568d54b4208bull},
    };
    return d;
}

// The cars the game no longer has (the prototype research: each model compared with every car of the game), each
// with the game car it takes its engine sound and handling from (its own id where the game has a car file of it; the
// van, a traffic vehicle, the game's van's donor as the TRAFFIC series has it)
const std::vector<Car>& cars() {
    if (g_test_cars) return *g_test_cars;
    static const std::vector<Car> c = {
        {"jun21", "HEVY", "CAR4", false, 0xce448acbe6a9e9a1ull, "HEVY", "CAR4", 6, "SUV", ""},
        {"jun21", "HEVY", "CAR5", false, 0xcb92f6060fb6ec2eull, "HEVY", "CAR5", 6, "Van", ""},
        {"jun21", "HEVY", "CAR10", false, 0x9664976e016af30dull, "HEVY", "CAR10", 6, "Garbage Truck", ""},
        {"jun21", "HEVY", "CAR36", false, 0x0ae980b2b39435feull, "HEVY", "CAR36", 6, "Early Fire Truck", ""},
        {"jun21", "HEVY", "CAR35", true, 0x4f77149567464cb8ull, "HEVY", "CAR5", 6, "Delivery Van", ""},
        {"jun21", "COMP", "CAR9", false, 0xa280aa2cdb8ac10aull, "COMP", "CAR9", 0, "Compact 9", ""},
        {"jun21", "MSCL", "CAR2", false, 0xbe3165536f9a9ad8ull, "MSCL", "CAR2", 1, "Muscle 2", ""},
        {"jun21", "MSCL", "CAR5", false, 0x64acfb270f6d9df8ull, "MSCL", "CAR5", 1, "Muscle 5", ""},
        {"jun21", "SUPR", "CAR4", false, 0x17f3a0e351d3acd1ull, "SUPR", "CAR4", 4, "Super 4", ""},
        {"jun08", "COMP", "CAR9", false, 0xf9ed9853694c5f7full, "COMP", "CAR9", 0, "Compact 9 (June 8)", ""},
        {"jun08", "MSCL", "CAR2", false, 0xd408a5b1b6c9f923ull, "MSCL", "CAR2", 1, "Muscle 2", "jun21-mscl-car2"},
    };
    return c;
}

std::string Car::key() const { return lower(std::string(disc) + "-" + cls + "-" + car); }
std::string Car::disc_path() const { return std::string("VEHICLES/") + cls + "/" + car + (traffic ? ".BTV" : ".BGV"); }
std::string Car::donor_path() const { return std::string("PVEH/") + donor_cls + "/" + donor_car + ".BGV"; }
std::string Car::donor_id() const { return std::string(donor_cls) + donor_car; }

std::uint64_t fnv1a64(const std::uint8_t* p, std::size_t n) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

// Size ceilings on what is read from an image or a folder, checked before reading (a damaged or crafted image cannot
// make the import allocate what it names): VLIST.BIN 4 KB on both discs, a prototype car file at most 448 KB (the
// game's car block, 0x72000, and some room), a car.ini a few hundred bytes
constexpr std::int64_t kMaxVlist = 64 * 1024, kMaxCarFile = 0x80000, kMaxIni = 64 * 1024;
bool read_capped(const std::filesystem::path& p, std::int64_t cap, std::vector<std::uint8_t>& out, std::string* why) {
    const std::int64_t n = disc::file_size(p);
    if (n < 0) {
        if (why) *why = "cannot be read";
        return false;
    }
    if (n > cap) {
        if (why) *why = "is " + thousands(std::size_t(n)) + " bytes, larger than such a file can be (at most " + thousands(std::size_t(cap)) + ")";
        return false;
    }
    if (!disc::read_file(p, out)) {
        if (why) *why = "cannot be read";
        return false;
    }
    return true;
}

const Disc* identify(const std::filesystem::path& image, std::string* why) {
    auto say = [&](const std::string& s) {
        if (why) *why = s;
        return nullptr;
    };
    if (!disc::is_dir(image)) {
        if (const std::string e = disc::image_error(image); !e.empty()) return say(e);
        return say("not a disc image (an .iso, a .bin or a .chd) or a disc folder");
    }
    const std::filesystem::path vl = disc::resolve_ci(image, "VEHICLES/VLIST.BIN");
    std::vector<std::uint8_t> v;
    std::string vwhy;
    if (!vl.empty() && !read_capped(vl, kMaxVlist, v, &vwhy) && disc::file_size(vl) > kMaxVlist)
        return say("its VEHICLES/VLIST.BIN " + vwhy + ": not one of the known prototypes");
    if (vl.empty() || v.empty()) {
        if (disc::list_dir(image).empty())
            return say("no files can be read from it (not a PS2 disc image: an Xbox disc's cars are Xbox models, which the PS2 game cannot use)");
        if (!disc::resolve_ci(image, "PVEH/VLIST.BIN").empty())
            return say("a disc in the game's own layout (the final game, or a July 2004 prototype, whose cars are the game's own): "
                       "nothing to import");
        if (!disc::resolve_ci(image, "VEHICLES").empty()) return say("a disc with car files but no VLIST.BIN: not one of the known prototypes");
        return say("not a Burnout 3 prototype disc (no VEHICLES/VLIST.BIN)");
    }
    const std::uint64_t h = fnv1a64(v.data(), v.size());
    for (const Disc& d : discs()) {
        if (d.vlist != h) continue;
        // the car list alone is not enough: the April and May 2004 demos share the June 21 demo's, with three of the
        // game's own cars on the disc. The disc is that one when it has the known cars' files.
        for (const Car& c : cars())
            if (c.disc == std::string(d.key) && !disc::resolve_ci(image, c.disc_path()).empty()) return &d;
        return say(std::string("a demo with the car list of ") + d.label + " but none of its cut cars (an earlier demo: its few "
                   "cars are the game's own): nothing to import");
    }
    return say("a Burnout 3 build whose car list is not one of the known prototypes' (the June 8 prototype, the June 21 "
               "demo); its cars are the game's own or are not supported");
}

std::string Car::folder() const {
    const std::vector<Car>& t = cars();
    std::size_t i = 0;
    while (i < t.size() && t[i].key() != key()) ++i;
    char b[24];
    std::snprintf(b, sizeof b, "%02zu-", i + 1);
    return b + key();
}

std::string car_ini(const Spec& c, std::size_t size, std::uint64_t hash) {
    std::string s = "# A car of the BETA series: cut from the game, converted from the player's own " + c.what +
                    "\n# (Burnout 3 PC port: Import prototype cars). The game reads it from this folder.\n";
    s += "name = " + c.name + "\n";
    s += "donor = " + c.donor + "\n";
    s += "model = car.bgv\n";
    s += "key = " + c.key + "\n";
    if (!c.replaced_by.empty()) s += "replaced_by = " + c.replaced_by + "\n";   // extrastart: the game hides it beside that one
    s += "source = " + c.source + "\n";
    s += "from = " + c.from + "\n";
    s += "series = " + std::to_string(c.series) + "\n";
    s += "size = " + std::to_string(size) + "\n";
    s += "hash = " + hex64(hash) + "\n";
    return s;
}

Spec spec_of(const Car& c) {
    Spec s;
    s.folder = c.folder();
    s.key = c.key();
    s.name = c.name;
    s.donor = std::string(c.donor_cls) + "/" + c.donor_car;
    s.series = c.series;
    s.source = c.disc;
    s.from = c.disc_path();
    s.replaced_by = c.replaced_by;
    for (const Disc& d : discs())
        if (c.disc == std::string(d.key)) s.what = std::string("disc image of ") + d.label;
    return s;
}

std::vector<Imported> imported(const std::filesystem::path& out_dir) {
    std::vector<Imported> v;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(out_dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string f = b3io::u8string(it->path().filename());
        if (f.empty() || f[0] == '.' || !it->is_directory(ec)) continue;
        std::vector<std::uint8_t> ini;
        if (!read_capped(it->path() / "car.ini", kMaxIni, ini, nullptr) || !std::filesystem::is_regular_file(it->path() / "car.bgv", ec)) continue;
        Imported c;
        c.folder = f;
        std::string line;
        for (std::size_t i = 0; i <= ini.size(); ++i) {
            if (i < ini.size() && ini[i] != '\n') {
                if (ini[i] != '\r') line += char(ini[i]);
                continue;
            }
            if (line.rfind("name = ", 0) == 0) c.name = line.substr(7);
            if (line.rfind("key = ", 0) == 0) c.key = line.substr(6);
            if (line.rfind("source = ", 0) == 0) c.source = line.substr(9);
            if (line.rfind("from = ", 0) == 0) c.from = line.substr(7);
            if (line.rfind("pack = ", 0) == 0) c.pack = line.substr(7);
            line.clear();
        }
        v.push_back(std::move(c));
    }
    // extrastart: an earlier copy whose later version is here too (a removal that failed) is not listed
    std::vector<Imported> kept;
    for (const Imported& c : v) {
        bool later = false;
        for (const Car& t : cars())
            if (t.key() == c.key && *t.replaced_by)
                for (const Imported& o : v) later = later || o.key == t.replaced_by;
        if (!later) kept.push_back(c);
    }
    v = std::move(kept);
    std::sort(v.begin(), v.end(), [](const Imported& x, const Imported& y) { return x.folder < y.folder; });   // (extra_scan's order)
    return v;
}

std::vector<std::string> tidy_replaced(const std::filesystem::path& out_dir) {
    std::vector<std::string> done;
    std::error_code ec;
    for (const Car& t : cars()) {
        if (!*t.replaced_by) continue;
        std::string later;
        for (const Car& o : cars())
            if (o.key() == t.replaced_by) later = o.folder();
        if (later.empty() || !std::filesystem::exists(out_dir / later / "car.bgv", ec) || !std::filesystem::exists(out_dir / t.folder(), ec)) continue;
        std::filesystem::remove_all(out_dir / t.folder(), ec);
        done.push_back(t.folder() + (ec ? ": could not be removed (" + ec.message() + ")" : ": removed (" + std::string(t.replaced_by) + " is imported)"));
    }
    return done;
}

namespace {

// Converts `old` with the game's donor and writes the car's folder whole (a hidden temporary folder, then renamed into
// place: the game reads the folders at start). Adds a line to r.imported or r.skipped; true when written.
bool write_car(const Spec& c, const std::vector<std::uint8_t>& old, const std::filesystem::path& game, const std::filesystem::path& out_dir,
               Result& r) {
    namespace fs = std::filesystem;
    const std::string donor_path = "PVEH/" + c.donor + ".BGV";
    std::vector<std::uint8_t> donor, out;
    std::string rwhy;
    const fs::path dp = disc::resolve_ci(game, donor_path);
    if (dp.empty() || !read_capped(dp, std::int64_t(kCarMemoryBlock), donor, &rwhy)) {
        r.skipped.push_back(c.key + ": the game's " + donor_path + " (its donor) " + (dp.empty() ? std::string("is missing") : rwhy));
        return false;
    }
    CarConvertInfo info;
    if (const std::string e = convert_prototype_car(Bytes{old.data(), old.size()}, Bytes{donor.data(), donor.size()}, out, &info);
        !e.empty()) {
        r.skipped.push_back(c.key + ": " + e);
        return false;
    }
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    const fs::path final_dir = out_dir / c.folder, tmp = out_dir / ("." + c.folder + ".partial");
    fs::remove_all(tmp, ec);
    std::string werr;
    const std::string_view data(reinterpret_cast<const char*>(out.data()), out.size());
    bool ok = fs::create_directory(tmp, ec) && b3io::replace_file(tmp / "car.bgv", data, &werr) &&
              b3io::replace_file(tmp / "car.ini", car_ini(c, out.size(), fnv1a64(out.data(), out.size())), &werr);
    if (ok) {   // the old folder renamed aside, the new one into place, then the old one deleted (put back on a failure)
        const fs::path old_dir = out_dir / ("." + c.folder + ".old");
        fs::remove_all(old_dir, ec);
        const bool had = fs::exists(final_dir, ec);
        if (had) fs::rename(final_dir, old_dir, ec);
        if (had && ec) {
            ok = false;
            werr = "the earlier copy cannot be moved aside: " + ec.message();
        } else {
            fs::rename(tmp, final_dir, ec);
            ok = !ec;
            if (ec) {
                werr = ec.message();
                if (had) fs::rename(old_dir, final_dir, ec);   // the earlier copy back
            } else if (had) {
                fs::remove_all(old_dir, ec);
            }
        }
    }
    if (!ok) {
        fs::remove_all(tmp, ec);
        r.skipped.push_back(c.key + ": not written: " + (werr.empty() ? std::string("the folder cannot be made") : werr));
        return false;
    }
    r.imported.push_back(c.key + ": " + c.name + " (" + thousands(out.size()) + " bytes)");
    return true;
}

}  // namespace

Result import_cars(const std::filesystem::path& image, const std::filesystem::path& game, const std::filesystem::path& out_dir,
                   void (*progress)(const std::string&, void*), void* ctx) {
    namespace fs = std::filesystem;
    Result r;
    std::string why;
    r.disc = identify(image, &why);
    if (!r.disc) {
        r.error = why;
        return r;
    }
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (!fs::is_directory(out_dir, ec)) {
        r.error = "cannot make the folder " + b3io::u8string(out_dir) + (ec ? ": " + ec.message() : std::string());
        return r;
    }
    std::vector<const Car*> todo;
    for (const Car& c : cars())
        if (c.disc == std::string(r.disc->key)) todo.push_back(&c);
    auto folder_of_key = [](const std::string& k) {
        for (const Car& c : cars())
            if (c.key() == k) return c.folder();
        return std::string();
    };
    int n = 0;
    for (const Car* c : todo) {
        const std::string key = c->key();
        if (progress) progress(std::to_string(++n) + "/" + std::to_string(todo.size()) + " " + key, ctx);
        try {   // (one car's failure, e.g. a folder it may not write, skips that car with the reason)
        if (*c->replaced_by && fs::exists(out_dir / folder_of_key(c->replaced_by) / "car.bgv", ec)) {
            r.skipped.push_back(key + ": the same car from a later disc is already imported (" + c->replaced_by + ")");
            continue;
        }
        std::vector<std::uint8_t> old;
        const fs::path src = disc::resolve_ci(image, c->disc_path());
        std::string rwhy;
        if (src.empty() || !read_capped(src, kMaxCarFile, old, &rwhy)) {
            r.skipped.push_back(key + ": " + c->disc_path() + (src.empty() ? std::string(" is not on the image") : " " + rwhy));
            continue;
        }
        if (fnv1a64(old.data(), old.size()) != c->hash) {
            r.skipped.push_back(key + ": " + c->disc_path() + " differs from the known file (a damaged image?)");
            continue;
        }
        if (!write_car(spec_of(*c), old, game, out_dir, r)) continue;
        // the same car from an earlier disc, imported before: this one replaces it
        for (const Car& o : cars())
            if (o.replaced_by == key && fs::exists(out_dir / o.folder(), ec)) {
                fs::remove_all(out_dir / o.folder(), ec);
                if (ec) r.skipped.push_back(o.key() + ": its earlier copy could not be removed (" + ec.message() + "): it is left out of the list");
            }
        } catch (const std::exception& e) {
            r.skipped.push_back(key + ": " + e.what());
        } catch (...) {
            r.skipped.push_back(key + ": an unknown error");
        }
    }
    return r;
}

// ---- car files and folders (the owner, 2026-11-22): a prototype car file however the player has it ----

namespace {

constexpr std::size_t kMaxFolderFiles = 4000;   // a folder walk stops after so many entries (an unrelated big folder)

struct RaceClass {
    const char* cls;
    int series;
    const char* word;   // its name in "<word> <n> (prototype)"
};
const RaceClass* race_class(const std::string& cls) {
    static const RaceClass k[] = {{"COMP", 0, "Compact"}, {"MSCL", 1, "Muscle"}, {"CUPE", 2, "Coupe"}, {"SPRT", 3, "Sport"},
                                  {"SUPR", 4, "Super"},   {"HSPC", 5, "Special"}, {"HEVY", 6, "Heavy"}};
    for (const RaceClass& c : k)
        if (cls == c.cls) return &c;
    return nullptr;
}
std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
// "CAR4" (1..2 digits, not 0): its number, else 0
int car_number(const std::string& leaf) {
    if (leaf.size() < 4 || leaf.size() > 5 || leaf.compare(0, 3, "CAR") != 0 || leaf[3] == '0') return 0;
    int n = 0;
    for (std::size_t i = 3; i < leaf.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(leaf[i]))) return 0;
        n = n * 10 + (leaf[i] - '0');
    }
    return n;
}
bool car_extension(const std::filesystem::path& p) {
    const std::string e = upper(b3io::u8string(p.extension()));
    return e == ".BGV" || e == ".BTV";
}

}  // namespace

std::string donor_from_path(const std::filesystem::path& file) {
    const std::string cls = upper(b3io::u8string(file.parent_path().filename())), leaf = upper(b3io::u8string(file.stem()));
    if (!race_class(cls) || !car_number(leaf)) return {};
    return cls + "/" + leaf;
}

std::vector<std::string> donor_choices(const std::filesystem::path& game, const std::string& cls) {
    std::vector<std::pair<int, std::string>> v;
    for (const disc::Entry& e : disc::list_dir(disc::resolve_ci(game, "PVEH/" + cls))) {
        const std::string n = upper(e.name);
        if (e.dir || n.size() < 5 || n.substr(n.size() - 4) != ".BGV") continue;
        if (const int k = car_number(n.substr(0, n.size() - 4))) v.push_back({k, cls + "/" + n.substr(0, n.size() - 4)});
    }
    std::sort(v.begin(), v.end());
    std::vector<std::string> out;
    for (const auto& p : v) out.push_back(p.second);
    return out;
}

namespace {

// betabytes: a traffic vehicle's file (.BTV at a slot the TRAFFIC series offers: COMP CAR11-19, HEVY CAR11-35; not the
// trailers or trams) -> its donor race car ("HEVY/CAR5", trafficseries' table) and its slot ("HEVY/CAR35"); else empty
struct TrafficSlot {
    std::string donor, slot;
};
TrafficSlot traffic_slot(const std::filesystem::path& f) {
    if (upper(b3io::u8string(f.extension())) != ".BTV") return {};
    const std::string cls = upper(b3io::u8string(f.parent_path().filename())), leaf = upper(b3io::u8string(f.stem()));
    for (const b3native::TrafficEntry& t : b3native::traffic_table())
        if (t.series != 0 && cls == t.cls && leaf == t.car) return {std::string(t.donor_cls) + "/" + t.donor_car, cls + "/" + leaf};
    return {};
}

// One car file's bytes (`f`: its path, for its label's file name and the donor its folder and name say; `chosen`: the
// player's donor, else none): as import_files takes each file
void import_one(const std::filesystem::path& f, const std::string& label, const std::vector<std::uint8_t>& old, const std::string& chosen,
                const std::filesystem::path& game, const std::filesystem::path& out_dir, Result& r) {
    namespace fs = std::filesystem;
    const std::uint64_t h = fnv1a64(old.data(), old.size());
    const Car* known = nullptr;
    for (const Car& c : cars())
        if (c.hash == h) {
            known = &c;
            break;
        }
    if (known) {   // a cut car the table knows: its name and donor
        std::error_code ec;
        if (*known->replaced_by) {
            bool later = false;
            for (const Car& c : cars())
                if (c.key() == known->replaced_by) later = fs::exists(out_dir / c.folder() / "car.bgv", ec);
            if (later) {
                r.skipped.push_back(label + ": the same car from a later disc is already imported (" + known->replaced_by + ")");
                return;
            }
        }
        if (write_car(spec_of(*known), old, game, out_dir, r))
            for (const Car& o : cars())
                if (o.replaced_by == known->key() && fs::exists(out_dir / o.folder(), ec)) {
                    fs::remove_all(out_dir / o.folder(), ec);
                    if (ec) r.skipped.push_back(o.key() + ": its earlier copy could not be removed (" + ec.message() + "): it is left out of the list");
                }
        return;
    }
    const Bytes b{old.data(), old.size()};
    if (!car_is_prototype(b)) {
        const std::string e = car_file_error(b);
        if (e.empty())
            r.skipped.push_back(label + ": a car file of the game's own layout, not a prototype's: a mod car, for the CUSTOM series "
                                "(cars/custom)");
        else
            r.skipped.push_back(label + ": " + e);
        return;
    }
    // a prototype file the table does not know: the donor the player chose, else the one its path names (a traffic
    // vehicle's slot: its race car donor, as trafficseries drives it)
    const TrafficSlot ts = chosen.empty() ? traffic_slot(f) : TrafficSlot{};
    std::string donor = chosen;
    if (donor.empty()) donor = !ts.donor.empty() ? ts.donor : donor_from_path(f);
    const std::size_t slash = donor.find('/');
    const RaceClass* rc = slash == std::string::npos ? nullptr : race_class(donor.substr(0, slash));
    // (a chosen donor string is checked as one the path gives: a race class and "CARn", so nothing else reaches the
    // output folder's name)
    if (!rc || !car_number(donor.substr(slash + 1)) || disc::resolve_ci(game, "PVEH/" + donor + ".BGV").empty()) {
        r.needs_donor.push_back(f);
        r.skipped.push_back(label + (donor.empty() ? std::string(": its name does not say which car it is: choose it")
                                                   : ": the game has no " + donor + " to take its engine and handling from: choose another"));
        return;
    }
    // its name and folder by its own slot: the donor's, or a traffic vehicle's ("Heavy 35 (prototype)", its donor HEVY/CAR5)
    const std::string slot = ts.slot.empty() ? donor : ts.slot;
    const std::size_t ss = slot.find('/');
    const RaceClass* sc = race_class(slot.substr(0, ss));
    Spec sp;
    sp.key = lower(slot.substr(0, ss) + "-" + slot.substr(ss + 1)) + "-" + hex64(h).substr(0, 8);
    sp.folder = "50-" + sp.key;   // after the known cars, in SELECT CAR's order
    sp.name = std::string((sc ? sc : rc)->word) + " " + std::to_string(car_number(slot.substr(ss + 1))) + " (prototype)";
    sp.donor = donor;
    sp.series = rc->series;
    sp.source = "file";
    sp.from = label;
    sp.what = "prototype car file " + label;
    write_car(sp, old, game, out_dir, r);
}

}  // namespace

Result import_files(const std::vector<std::filesystem::path>& files, const std::filesystem::path& game, const std::filesystem::path& out_dir,
                    void (*progress)(const std::string&, void*), void* ctx, const std::vector<FileDonor>& donors) {
    namespace fs = std::filesystem;
    Result r;
    int n = 0;
    for (const fs::path& f : files) {
        std::string label = "file " + std::to_string(n + 1);   // (until its name is read: that may throw too)
        try {
            label = b3io::u8string(f.filename());
            if (progress) progress(std::to_string(++n) + "/" + std::to_string(files.size()) + " " + label, ctx);
            std::vector<std::uint8_t> old;
            std::string why;
            if (!read_capped(f, kMaxCarFile, old, &why)) {
                r.skipped.push_back(label + ": " + why);
                continue;
            }
            std::string chosen;
            for (const FileDonor& d : donors)
                if (d.file == f) chosen = d.donor;
            import_one(f, label, old, chosen, game, out_dir, r);
        } catch (const std::exception& e) {
            r.skipped.push_back(label + ": " + e.what());
        } catch (...) {
            r.skipped.push_back(label + ": an unknown error");
        }
    }
    return r;
}

Result import_bytes(const std::string& rel, const std::vector<std::uint8_t>& bytes, const std::filesystem::path& game,
                    const std::filesystem::path& out_dir, const std::string& donor) {
    Result r;
    std::string label = rel;
    try {
        const std::filesystem::path f = b3io::u8path(rel);
        label = b3io::u8string(f.filename());
        if (bytes.size() > std::size_t(kMaxCarFile)) {
            r.skipped.push_back(label + ": is " + thousands(bytes.size()) + " bytes, larger than such a file can be (at most " + thousands(std::size_t(kMaxCarFile)) + ")");
            return r;
        }
        import_one(f, label, bytes, donor, game, out_dir, r);
    } catch (const std::exception& e) {
        r.skipped.push_back(label + ": " + e.what());
    } catch (...) {
        r.skipped.push_back(label + ": an unknown error");
    }
    return r;
}

Result import_folder(const std::filesystem::path& dir, const std::filesystem::path& game, const std::filesystem::path& out_dir,
                     void (*progress)(const std::string&, void*), void* ctx, const std::vector<FileDonor>& donors) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::size_t seen = 0, others = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        Result r;
        r.error = "not a folder: " + b3io::u8string(dir);
        return r;
    }
    const fs::path out_abs = fs::weakly_canonical(out_dir, ec);
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (++seen > kMaxFolderFiles) break;
        try {   // (a name that cannot be converted: that entry is left out)
            if (it->is_directory(ec)) {
                if (fs::weakly_canonical(it->path(), ec) == out_abs) it.disable_recursion_pending();   // (not our own output)
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            if (car_extension(it->path())) files.push_back(it->path());
            else ++others;
        } catch (...) {
            ++others;
        }
    }
    std::sort(files.begin(), files.end());
    Result r = import_files(files, game, out_dir, progress, ctx, donors);
    if (files.empty()) r.error = "no car files (.BGV, .BTV) in " + b3io::u8string(dir) + " or its folders";
    r.other_files = others;
    r.walk_stopped = seen > kMaxFolderFiles;
    return r;
}

}  // namespace b3asset::beta
