// carpack.h -- carpacks (2026-11-22): the launcher's "Import car pack..." -- a community car pack made to overwrite the
// disc's car files (a PCSX2 replacement pack: <pack>/PVEH/<CLS>/CARn.{BGV,BTV,HWD,LWD}, a zip or a folder) imported as
// extra cars, never in place of the game's. Header-only (tests/test_carpack.cpp); the launcher's page and its worker are
// main.cpp's.
//
// No duplicates across the game's cars, BETA and CUSTOM (the owner's rule):
//   1. a file identical to the player's disc file at its path: skipped ("already in the game");
//   2. a car that is one of the BETA series' cut cars -- by its geometry (b3asset/carprint.h: the pack's author converted
//      them, so their bytes are not the BETA import's): into BETA as that car (its folder, name and donor), skipped when
//      that car is already imported (from a disc or an earlier pack); a disc import later replaces the pack's copy
//      (betaimport, its car.ini's "pack =" line);
//   3. a file already imported (the same bytes, in any car folder): skipped;
//   4. an old-layout prototype file: Beta / Prototype cars' converter (betaimport);
//   5. any other changed car: a CUSTOM car of its own, cars/custom/<pack>/<CLS>-CARn/ (car.bgv, car.ini: its name, the
//      slot's car as its donor, its own engine banks when the pack's differ from the disc's).
// The names: the table here (keyed by the file's FNV-1a 64: car names only; a car of the game in new paint takes
// " (<pack>)"), else "<the slot's car name> (<pack>)"; <pack> is the player's own zip / folder name. The pack's
// vehicle list and handling (VDB.XML) are never applied: listed as left out.
#ifndef B3LAUNCHER_CARPACK_H
#define B3LAUNCHER_CARPACK_H

#include "b3asset/betaimport.h"
#include "b3asset/car_layout.h"
#include "b3asset/carconvert.h"
#include "b3asset/carprint.h"
#include "b3port/fileio.h"
#include "../../tools/native/extra_series.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace b3launcher::carpack {

namespace fs = std::filesystem;

enum class Group { kBeta, kCut, kNewModel, kEarly, kRecolour, kOther };
inline const char* group_name(Group g) {
    switch (g) {
    case Group::kBeta: return "Cut cars (BETA series)";
    case Group::kCut: return "Cut cars";
    case Group::kNewModel: return "New models";
    case Group::kEarly: return "Early versions";
    case Group::kRecolour: return "New paint";
    default: return "Other cars";
    }
}
// on by default: the cars the game does not have in any form; early versions and new paint of the game's cars: off
inline bool group_default(Group g) { return g == Group::kBeta || g == Group::kCut || g == Group::kNewModel || g == Group::kOther; }

// A known pack's car: its file's FNV-1a 64, its slot, its name on SELECT CAR, its group
struct KnownCar {
    std::uint64_t hash;
    const char* slot;
    const char* name;
    Group group;
};
struct KnownPack {
    const char* name;
    const char* by;   // (none: the pack's makers are credited in the README and the launcher's Credits only)
    std::vector<KnownCar> cars;
};
inline const std::vector<KnownPack>& known_packs() {
    static const std::vector<KnownPack> k = {
        {"", "", {   // (no pack name in the table: the label is the player's own zip / folder name)
            {0x879b0802cf1c52c7ull, "COMP/CAR1", "Compact Type 1 Beta", Group::kEarly},
            {0xa02fe66cfc83611cull, "COMP/CAR10", "Dominator Compact", Group::kRecolour},
            {0xfb714e9ba5f36b33ull, "COMP/CAR2", "Compact Type 2 Beta", Group::kEarly},
            {0x10e627067b466ac0ull, "COMP/CAR3", "Compact Type 3 Beta", Group::kEarly},
            {0xf032566be0a9cc14ull, "COMP/CAR4", "Compact 9", Group::kCut},
            {0xb91f5bada531caf9ull, "COMP/CAR5", "Modified Compact Beta", Group::kEarly},
            {0x2028929f66438705ull, "COMP/CAR6", "Custom Compact", Group::kRecolour},
            {0x1b35a14f9cd1326cull, "COMP/CAR7", "Assassin Compact Beta", Group::kEarly},
            {0x52213c3709b49d1cull, "COMP/CAR8", "Compact Prototype Beta", Group::kEarly},
            {0xc529c7e152697781ull, "COMP/CAR9", "Compact 9 (June 8)", Group::kCut},
            {0x3fd2ef61effdc194ull, "CUPE/CAR1", "Coupe Type 1", Group::kRecolour},
            {0xcc585927ff85ca14ull, "CUPE/CAR10", "Dominator Coupe", Group::kRecolour},
            {0x507070531e2322a3ull, "CUPE/CAR2", "Coupe Type 2 Beta", Group::kEarly},
            {0x3a2f2c56596f71ffull, "CUPE/CAR3", "Coupe Type 3 Beta", Group::kEarly},
            {0xe6b87b69167003eeull, "CUPE/CAR4", "Tuned Coupe", Group::kRecolour},
            {0x9aed3b6bb9694892ull, "CUPE/CAR5", "Modified Coupe Beta", Group::kEarly},
            {0x66f87784f0516df8ull, "CUPE/CAR6", "Custom Coupe Mod", Group::kNewModel},
            {0x6638ad5a781f24e4ull, "CUPE/CAR7", "Assassin Coupe Beta", Group::kEarly},
            {0x395343229cc43f08ull, "CUPE/CAR8", "Coupe Prototype", Group::kRecolour},
            {0x7e5f342b5c058d68ull, "CUPE/CAR9", "Coupe DX Beta", Group::kEarly},
            {0xac9a2c465914b372ull, "HEVY/CAR1", "Heavy Pickup Beta", Group::kEarly},
            {0xe411aeffe1055b6aull, "HEVY/CAR10", "Garbage Truck", Group::kCut},
            {0x9792dc87f6e5f848ull, "HEVY/CAR2", "4WD Racer Beta", Group::kEarly},
            {0xcd4925bc3850adabull, "HEVY/CAR3", "SUV Deluxe Beta", Group::kEarly},
            {0xb482746b114c842aull, "HEVY/CAR36", "Early Fire Truck", Group::kCut},
            {0x9164b03fe8cd4dabull, "HEVY/CAR4", "SUV", Group::kCut},
            {0xa6576411e9c4702eull, "HEVY/CAR5", "Van", Group::kCut},
            {0x9c4ecdf0e58f02edull, "HEVY/CAR6", "Delivery Truck Beta", Group::kEarly},
            {0x96d074a36405ae20ull, "HEVY/CAR7", "Truck Beta", Group::kEarly},
            {0x3414f091379b53ddull, "HEVY/CAR8", "Longnose Cab", Group::kRecolour},
            {0x8b2cb473726d9ff5ull, "HEVY/CAR9", "City Bus", Group::kRecolour},
            {0x85a3c1a9295743bdull, "HSPC/CAR22", "Euro-Circuit Racer", Group::kRecolour},
            {0xfdfe7b0562f3599eull, "MSCL/CAR1", "Muscle Type 1 Beta", Group::kEarly},
            {0xb5864c4464b9c438ull, "MSCL/CAR10", "Dominator Muscle", Group::kRecolour},
            {0x2f4a489eca3e2d1eull, "MSCL/CAR2", "Muscle Type 2 Beta", Group::kEarly},
            {0x6a19ba533fd0055bull, "MSCL/CAR3", "Muscle Type 3 Beta", Group::kEarly},
            {0xf4171bf003e443dfull, "MSCL/CAR4", "Muscle 9", Group::kCut},
            {0xda37ca6ea2f27bd6ull, "MSCL/CAR5", "Modified Muscle Beta", Group::kEarly},
            {0x50fd8b67e497a77eull, "MSCL/CAR6", "Custom Muscle Beta", Group::kEarly},
            {0x1a85ce1d5bcda3a2ull, "MSCL/CAR7", "Assassin Muscle Mod", Group::kNewModel},
            {0x4531633674755486ull, "MSCL/CAR8", "Muscle Prototype", Group::kRecolour},
            {0xeed0e59473813e3full, "MSCL/CAR9", "Muscle DX Beta", Group::kEarly},
            {0x67dda78b70b2f826ull, "SPRT/CAR1", "Sports Type 1 Mod", Group::kNewModel},
            {0x64fd99992c247298ull, "SPRT/CAR10", "Dominator Sports", Group::kRecolour},
            {0xed1dbe705e43356dull, "SPRT/CAR2", "Sports Type 2 Beta", Group::kEarly},
            {0x7fcbf7f6a4a3e56full, "SPRT/CAR3", "Sports Type 3", Group::kRecolour},
            {0x997d07af47b37b13ull, "SPRT/CAR4", "Tuned Sports", Group::kRecolour},
            {0x05e1663e4dac484eull, "SPRT/CAR5", "Modified Sports Beta", Group::kEarly},
            {0xbe837ad95a8d8424ull, "SPRT/CAR7", "Assassin Sports", Group::kRecolour},
            {0xf2a68c53ca4b8c35ull, "SPRT/CAR8", "Sports 3 Beta", Group::kCut},
            {0x69e93a4f0d58662cull, "SPRT/CAR9", "Sports DX", Group::kRecolour},
            {0x3c1c66f774ca7551ull, "SUPR/CAR1", "Super Type 1 Beta", Group::kEarly},
            {0x611b1d5f1a8b0819ull, "SUPR/CAR10", "Dominator Super", Group::kRecolour},
            {0x756edede0803004dull, "SUPR/CAR2", "Super Type 2 Beta", Group::kEarly},
            {0x555c71e5e2a9348cull, "SUPR/CAR3", "Super Type 3 Beta", Group::kEarly},
            {0x164b926d65245d44ull, "SUPR/CAR5", "Modified Super Beta", Group::kEarly},
            {0x7ade5bd136cd5bb8ull, "SUPR/CAR6", "Custom Super", Group::kRecolour},
            {0x7da02ad70a246ad7ull, "SUPR/CAR8", "Super Prototype", Group::kRecolour},
            {0x74cb1766c20e0220ull, "SUPR/CAR9", "Super 4", Group::kCut},
        }},
    };
    return k;
}
// The BETA series' cars by geometry (b3asset::car_fingerprint of betaimport's conversion of each disc car; a pack's own
// conversion of the same car has the same one: measured on a pack's nine)
struct BetaPrint {
    const char* key;   // b3asset::beta::Car::key()
    std::uint64_t print;
};
inline const std::vector<BetaPrint>& beta_prints() {
    static const std::vector<BetaPrint> k = {
        {"jun21-hevy-car4", 0x6d02253b9234bbddull},  {"jun21-hevy-car5", 0x1515d388fa0b33ccull},
        {"jun21-hevy-car10", 0x88a02ce7e6c1d7f3ull}, {"jun21-hevy-car36", 0xd1779740ae9215e7ull},
        {"jun21-hevy-car35", 0x7af10a6c49874bc7ull}, {"jun21-comp-car9", 0x4cdc00fe92e4e3ccull},
        {"jun21-mscl-car2", 0x83f43d1191a8066bull},  {"jun21-mscl-car5", 0x4a49e6e4b5cd1838ull},
        {"jun21-supr-car4", 0x4cae27f6bbf862e8ull},  {"jun08-comp-car9", 0xc4b1d0117c6ede86ull},
        {"jun08-mscl-car2", 0x5a0ab699c69a276bull},
    };
    return k;
}

// A file of the pack: its path in the pack ("My pack/PVEH/COMP/CAR7.BGV") and its bytes
struct InFile {
    std::string rel;
    std::vector<std::uint8_t> bytes;
};
enum class Dest { kSkip, kCustom, kBeta, kPrototype };
struct Item {
    std::string rel, slot;     // its path in the pack; "COMP/CAR7"
    std::string name;
    Group group = Group::kOther;
    Dest dest = Dest::kSkip;
    std::string why;           // kSkip: why ("already in the game", ...)
    std::size_t file = SIZE_MAX, hwd = SIZE_MAX, lwd = SIZE_MAX;   // indices into Plan::files
    std::string beta_key;      // kBeta: the BETA car it is
    std::uint64_t hash = 0;
    bool chosen = true;        // the player's choice (the launcher's group toggles)
};
struct Plan {
    std::string pack, by;      // its folder name under cars/custom; its author when the pack is known
    std::vector<InFile> files;
    std::vector<Item> items;
    std::vector<std::pair<std::string, std::string>> left_out;   // (a pack file, why): the vehicle list, VDB.XML, ...
};

// "My pack/PVEH/comp/car7.bgv" -> class "COMP", car "CAR7", extension "BGV" (false: not a car or bank of a class folder)
inline bool car_path(const std::string& rel, std::string* cls, std::string* car, std::string* ext) {
    std::string p;
    for (char c : rel) p += c == '\\' ? '/' : char(std::toupper(static_cast<unsigned char>(c)));
    const std::size_t s2 = p.find_last_of('/');
    if (s2 == std::string::npos || s2 == 0) return false;
    const std::size_t s1 = p.find_last_of('/', s2 - 1);
    const std::string c = p.substr(s1 == std::string::npos ? 0 : s1 + 1, s2 - (s1 == std::string::npos ? 0 : s1 + 1));
    const std::string leaf = p.substr(s2 + 1);
    const std::size_t dot = leaf.find_last_of('.');
    if (dot == std::string::npos) return false;
    *cls = c, *car = leaf.substr(0, dot), *ext = leaf.substr(dot + 1);
    return (*ext == "BGV" || *ext == "BTV" || *ext == "HWD" || *ext == "LWD") && b3native::extra_car_leaf(*car) &&
           (b3native::extra_donor_class(c) || c == "TSPC");
}
// A folder name for the pack (printable, no separators; "Car pack" when nothing is left)
inline std::string pack_folder_name(const std::string& s) {
    std::string o;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || std::string("/\\:*?\"<>|").find(c) != std::string::npos) continue;
        o += c;
        if (o.size() == 48) break;
    }
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    while (!o.empty() && (o.front() == ' ' || o.front() == '.')) o.erase(0, 1);
    return o.empty() ? std::string("Car pack") : o;
}

using DiscRead = std::function<bool(const std::string& disc_path, std::vector<std::uint8_t>& bytes)>;   // "PVEH/COMP/CAR7.BGV"
using SlotName = std::function<std::string(const std::string& slot)>;   // "COMP/CAR7" -> "Assassin Compact" ("" none)

// The plan for a pack: every car file sorted by the rules above (nothing written). `prints` / `packs`: the tables (tests
// give their own).
inline Plan plan(std::vector<InFile> files, const std::string& pack_name, const DiscRead& disc, const SlotName& slot_name,
                 const fs::path& custom, const fs::path& beta, const std::vector<BetaPrint>& prints = beta_prints(),
                 const std::vector<KnownPack>& packs = known_packs()) {
    Plan out;
    out.pack = pack_folder_name(pack_name);
    out.files = std::move(files);
    // what is imported already: every car file's bytes in both folders, the BETA cars by key
    std::set<std::uint64_t> have;
    for (const b3native::ExtraCar& c : b3native::extra_scan_all(custom, beta).cars) {
        std::vector<std::uint8_t> b;
        if (b3native::extra_read_file(b3io::u8path(c.model), b)) have.insert(b3native::extra_hash(b));
    }
    std::set<std::string> beta_have;
    for (const b3asset::beta::Imported& i : b3asset::beta::imported(beta)) beta_have.insert(i.key);
    // extrastart: a BETA car counts as imported when its later version is (June 8's Muscle 2 once June 21's is in): the
    // pack's copy of the earlier one is then the same car again
    auto beta_done = [&beta_have](const std::string& key) {
        if (beta_have.count(key)) return true;
        for (const b3asset::beta::Car& c : b3asset::beta::cars())
            if (c.key() == key && *c.replaced_by && beta_have.count(c.replaced_by)) return true;
        return false;
    };
    // the pack's own name and author when its files are a known pack's
    const KnownPack* known = nullptr;
    std::size_t best = 0;
    for (const KnownPack& k : packs) {
        std::size_t n = 0;
        for (const InFile& f : out.files) {
            const std::uint64_t h = b3native::extra_hash(f.bytes);
            for (const KnownCar& c : k.cars) n += c.hash == h;
        }
        if (n > best) best = n, known = &k;
    }
    if (known) out.by = known->by;   // (the label stays the player's own zip / folder name)
    std::set<std::uint64_t> seen;   // (the same file twice in a pack: once)
    for (std::size_t i = 0; i < out.files.size(); ++i) {
        const InFile& f = out.files[i];
        std::string cls, car, ext;
        if (!car_path(f.rel, &cls, &car, &ext)) {
            std::string u;
            for (char c : f.rel) u += char(std::toupper(static_cast<unsigned char>(c)));
            if (u.size() >= 9 && u.compare(u.size() - 9, 9, "VLIST.BIN") == 0)
                out.left_out.emplace_back(f.rel, "the pack's vehicle list: not used (the game's own is kept)");
            else if (u.size() >= 7 && u.compare(u.size() - 7, 7, "VDB.XML") == 0)
                out.left_out.emplace_back(f.rel, "the pack's handling and camera values: not applied (the game's own are kept)");
            continue;
        }
        if (ext == "HWD" || ext == "LWD" || cls == "TSPC") continue;   // (banks: with their car below; the trams: never)
        Item it;
        it.rel = f.rel, it.slot = cls + "/" + car, it.file = i;
        it.hash = b3native::extra_hash(f.bytes);
        std::vector<std::uint8_t> retail;
        const std::string disc_path = "PVEH/" + cls + "/" + car + "." + ext;
        const bool on_disc = disc && disc(disc_path, retail);
        if (on_disc && retail == f.bytes) {   // rule 1
            it.why = "already in the game";
            out.items.push_back(it);
            continue;
        }
        if (!seen.insert(it.hash).second || have.count(it.hash)) {   // rule 3
            it.why = "already imported";
            out.items.push_back(it);
            continue;
        }
        if (b3asset::car_is_prototype({f.bytes.data(), f.bytes.size()})) {   // rule 4
            // extrastart: the bytes of a disc's BETA car already imported (the pack carries the disc's own file): skipped
            const std::uint64_t fnv = b3asset::beta::fnv1a64(f.bytes.data(), f.bytes.size());
            const b3asset::beta::Car* same = nullptr;
            for (const b3asset::beta::Car& c : b3asset::beta::cars())
                if (c.hash == fnv) same = &c;
            if (same && beta_done(same->key())) {
                it.beta_key = same->key();
                it.name = same->name;
                it.group = Group::kBeta;
                it.why = "already a BETA car (" + it.name + ")";
                out.items.push_back(it);
                continue;
            }
            if (same) beta_have.insert(same->key());   // (the same car twice in a pack: once)
            it.dest = Dest::kPrototype;
            it.group = Group::kBeta;
            it.name = it.slot + " (prototype)";
            it.chosen = group_default(it.group);
            out.items.push_back(it);
            continue;
        }
        if (const std::string why = b3native::extra_car_file_problem(f.bytes); !why.empty()) {
            it.why = why;
            out.items.push_back(it);
            continue;
        }
        const std::uint64_t print = b3asset::car_fingerprint({f.bytes.data(), f.bytes.size()});
        const BetaPrint* bp = nullptr;
        for (const BetaPrint& b : prints)
            if (print && b.print == print) bp = &b;
        if (bp) {   // rule 2
            it.beta_key = bp->key;
            it.group = Group::kBeta;
            for (const b3asset::beta::Car& c : b3asset::beta::cars())
                if (c.key() == it.beta_key) it.name = c.name;
            if (beta_done(it.beta_key)) {
                it.why = "already a BETA car (" + it.name + ")";
                out.items.push_back(it);
                continue;
            }
            beta_have.insert(it.beta_key);   // (the same car twice in a pack: once)
            it.dest = Dest::kBeta;
            it.chosen = group_default(it.group);
            out.items.push_back(it);
            continue;
        }
        // rule 5: a CUSTOM car, its own engine banks when the pack's differ from the disc's
        it.dest = Dest::kCustom;
        it.group = Group::kOther;
        if (known)
            for (const KnownCar& c : known->cars)
                if (c.hash == it.hash) {
                    it.name = c.name, it.group = c.group;
                    if (c.group == Group::kRecolour) it.name += " (" + out.pack + ")";   // the game's car, new paint: "<car> (<pack>)"
                }
        if (it.name.empty()) {
            const std::string n = slot_name ? slot_name(it.slot) : std::string();
            it.name = (n.empty() ? it.slot : n) + " (" + out.pack + ")";
        }
        for (std::size_t j = 0; j < out.files.size(); ++j) {
            std::string c2, k2, e2;
            if (!car_path(out.files[j].rel, &c2, &k2, &e2) || c2 != cls || k2 != car || (e2 != "HWD" && e2 != "LWD")) continue;
            std::vector<std::uint8_t> rb;
            if (disc && disc("PVEH/" + cls + "/" + car + "." + e2, rb) && rb == out.files[j].bytes) continue;   // (the disc's)
            (e2 == "HWD" ? it.hwd : it.lwd) = j;
        }
        it.chosen = group_default(it.group);
        out.items.push_back(it);
    }
    return out;
}

// What an import did: each car imported, each left out and why
struct Done {
    std::vector<std::string> imported;
    std::vector<std::pair<std::string, std::string>> skipped;
};
inline bool write_whole(const fs::path& dst, const std::vector<std::pair<std::string, const std::vector<std::uint8_t>*>>& files) {
    std::error_code ec;
    const fs::path tmp = dst.parent_path() / ("." + b3io::u8string(dst.filename()) + ".partial");
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    for (const auto& [name, bytes] : files) {
        std::ofstream o(tmp / b3io::u8path(name), std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(bytes->data()), std::streamsize(bytes->size()));
        if (!o) { fs::remove_all(tmp, ec); return false; }
    }
    fs::rename(tmp, dst, ec);
    if (ec) { fs::remove_all(tmp, ec); return false; }
    return true;
}
// The chosen cars written: CUSTOM's into cars/custom/<pack>/<CLS>-CARn/, BETA's into cars/beta/<its folder>/ (betaimport's
// car.ini with "pack = <pack>"), old-layout files through betaimport (`proto`: the launcher's call of import_bytes; null:
// left for Beta / Prototype cars)
using ProtoImport = std::function<std::string(const Item&, const std::vector<std::uint8_t>& bytes)>;   // "" done, else why
inline Done write(const Plan& p, const fs::path& custom, const fs::path& beta, const ProtoImport& proto = nullptr) {
    Done d;
    std::error_code ec;
    for (const Item& it : p.items) {
        if (it.dest == Dest::kSkip) { d.skipped.emplace_back(it.rel, it.why); continue; }
        if (!it.chosen) { d.skipped.emplace_back(it.rel, "not chosen"); continue; }
        const std::vector<std::uint8_t>& bytes = p.files[it.file].bytes;
        if (it.dest == Dest::kPrototype) {
            const std::string why = proto ? proto(it, bytes) : std::string("an old-layout prototype file: import it with Beta / Prototype cars");
            if (why.empty()) d.imported.push_back(it.rel + " (BETA)");
            else d.skipped.emplace_back(it.rel, why);
            continue;
        }
        if (it.dest == Dest::kBeta) {
            const b3asset::beta::Car* car = nullptr;
            for (const b3asset::beta::Car& c : b3asset::beta::cars())
                if (c.key() == it.beta_key) car = &c;
            if (!car) { d.skipped.emplace_back(it.rel, "not a BETA car"); continue; }
            const fs::path dst = beta / car->folder();
            if (fs::exists(dst, ec)) { d.skipped.emplace_back(it.rel, "already a BETA car (" + it.name + ")"); continue; }
            const std::string ini = b3asset::beta::car_ini(b3asset::beta::spec_of(*car), bytes.size(), it.hash) + "pack = " + p.pack + "\n";
            const std::vector<std::uint8_t> ini_b(ini.begin(), ini.end());
            fs::create_directories(beta, ec);
            if (write_whole(dst, {{"car.bgv", &bytes}, {"car.ini", &ini_b}})) d.imported.push_back(it.rel + " -> BETA " + it.name);
            else d.skipped.emplace_back(it.rel, "could not be written");
            continue;
        }
        const std::string cls = it.slot.substr(0, it.slot.find('/')), car = it.slot.substr(it.slot.find('/') + 1);
        const fs::path dst = custom / b3io::u8path(p.pack) / (cls + "-" + car);
        if (fs::exists(dst, ec)) { d.skipped.emplace_back(it.rel, "already imported"); continue; }
        std::string ini = "# A car of " + p.pack +
                          " (Burnout 3 PC port: Import car pack). The game reads it from this folder.\n";
        ini += "name = " + b3native::extra_display_name(it.name) + "\ndonor = " + it.slot + "\nmodel = car.bgv\n";
        std::vector<std::pair<std::string, const std::vector<std::uint8_t>*>> out = {{"car.bgv", &bytes}};
        if (it.hwd != SIZE_MAX) ini += "hwd = car.hwd\n", out.emplace_back("car.hwd", &p.files[it.hwd].bytes);
        if (it.lwd != SIZE_MAX) ini += "lwd = car.lwd\n", out.emplace_back("car.lwd", &p.files[it.lwd].bytes);
        const std::vector<std::uint8_t> ini_b(ini.begin(), ini.end());
        out.emplace_back("car.ini", &ini_b);
        fs::create_directories(dst.parent_path(), ec);
        if (write_whole(dst, out)) d.imported.push_back(it.rel + " -> CUSTOM " + b3native::extra_display_name(it.name));
        else d.skipped.emplace_back(it.rel, "could not be written");
    }
    return d;
}

}  // namespace b3launcher::carpack

#endif
