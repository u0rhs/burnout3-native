// extra_series.h -- customseries (2026-11-22): the CUSTOM series on SINGLE EVENT's SELECT CAR, players' car mods as extra
// cars beside the retail ones (never in place of one). The rules and tables as values (pure: tests/test_extra_series.cpp);
// the hooks are extra_cars.h.
//
// What the game has [C the decomp; addresses retail]:
// - The vehicle list CB3VehicleList D_005186A0 is PVEH/VLIST.BIN as loaded (CB3Game 0x004EE040 +0x2A660, 0x1000 bytes,
//   func_00133190's start-up): +4 the count (107: 67 race cars, 40 traffic vehicles), +8 s32 driveable[128], +0x208 s32
//   boost bar size[128], +0x408 u64 GtID[128] (B3DataLists.cpp: GetCarCount 0x251B90, GetCarID 0x251C10,
//   GetVehicleIndexFromID 0x251E60 (0 when not listed), GetVehicleBoostBarSize 0x251EA0, IsVehicleDriveable 0x251EF0,
//   CreateVehiclePathFromIndex 0x251F40: "PVEH/" + the GtID name's first four letters + "/" + the rest, DoesVehicleExist
//   0x252000). Room for 21 more.
// - GetClassFromID 0x251CB0 classes a car by its GtID name's first four letters (COMP 0 .. HEVY 6, TRAF 7, TSPC 8; any
//   other 9, which SELECT CAR's code takes for "no car": func_00318F50, func_0042CFA0). Its callers (all through dispatch):
//   GetCarCount / GetCarID, func_0030B6E0 (the random rivals: over the 67 race cars only, CB3HardCodedVehicleList),
//   func_00310200 (SELECT CAR's confirm: the rivals by the player's class, func_0030B9D0), func_00318F50 (SELECT CAR's
//   Prepare), func_0042CFA0 (the carousel's Prepare), func_0042DF40 (a car's row in its class).
// - A race car's file (.BGV, version 0x17): the header's +0x4C is its LOD blocks' offset (0x16F0 in every retail file and
//   every car mod read so far), +0x08 the file's size, +0x16DC / +0x16E0 its engine sound models (traffic_series.h
//   traffic_model). The prototype discs' files (versions 0x14 / 0x15) have the LOD blocks at 0x16E0 and no sound models:
//   another layout, which the BETA series' importer converts (not this file's).
// Ours: a car in the player's folder `<user data>/cars/custom/` gets a vehicle list entry of its own (GtID XCUS/CARn, n
// from 1 in the folder's order), the CUSTOM series after the traffic series on SELECT CAR (class 10: no class the game has),
// its donor's (the retail race car it was made to replace) tuning, engine sound, picture, rivals and boost bar; its own
// model (hull, frames, meshes). Its races keep their records apart (record bit 29) and do not count for the profile.
#ifndef B3NATIVE_EXTRA_SERIES_H
#define B3NATIVE_EXTRA_SERIES_H

#include "b3asset/car_layout.h"   // carmods: the version and layout the readers and the game take
#include "b3port/fileio.h"     // u8path / u8string
#include "traffic_series.h"   // gtid_compress, traffic_upper_path, traffic_model, traffic_class_of

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace b3native {

constexpr int kCustomSeries = 10;                 // SELECT CAR's CUSTOM series (no class of the game's: 9 is "no car")
constexpr int kBetaSeries = 11;                   // BETA (the launcher's Beta / Prototype cars import)
constexpr int kVehicleListMax = 128;              // [C] CB3VehicleList's arrays
constexpr int kExtraListRoom = 21;                // 128 - the disc's 107: the entries joined at once (extra_cars.h)
// carpacks: a series shows at most 20 cars [C func_0042B7C0: 20 rows], so CUSTOM and BETA come in pages of 19, each a
// series of its own on SELECT CAR: CUSTOM's pages are the series codes 10, 12, 14, ..., BETA's 11, 13, ... (codes only
// the port's wrappers ever see). The vehicle list holds only the cars of the page on screen, or of the race.
constexpr int kExtraPageCars = 19;   // (Fable's review: a page and the remembered cars -- the player's two -- fit the 21)
constexpr int kCustomCarsMax = 200;               // 11 pages (the scan reads each file: startup time, the launcher's lists)
constexpr int kBetaCarsMax = 40;                  // (the importer's discs give 10)
constexpr int kExtraPagesMax = (kCustomCarsMax + kExtraPageCars - 1) / kExtraPageCars;
constexpr int kExtraCarsMax = kCustomCarsMax + kBetaCarsMax;
inline int extra_page_series(int kind, int page) { return (kind == kBetaSeries ? kBetaSeries : kCustomSeries) + 2 * page; }
constexpr std::uint32_t kVehicleList = 0x005186A0u;
constexpr std::uint32_t kCarFileMax = 0x72000u;          // the game's vehicle block for a race car's file
// String ids the game's lookup func_00271A00 never has (the disc's are below 0xFA5; custom_soundtrack.h's from 0xF000):
// the series' names (kExtraStringBase + the series code - 10), then each extra car's name
constexpr std::uint32_t kExtraStringBase = 0xE000u;
constexpr std::uint32_t kExtraStringCustom = kExtraStringBase, kExtraStringBeta = kExtraStringBase + 1u;
constexpr std::uint32_t kExtraStringCar0 = kExtraStringBase + 0x100u;   // + the car's index

// The race car classes a donor may be of (GetClassFromID's prefixes 0..6) and their names on SELECT CAR
inline bool extra_donor_class(const std::string& cls) {
    static const char* const k[] = {"COMP", "MSCL", "CUPE", "SPRT", "SUPR", "HSPC", "HEVY"};
    for (const char* c : k)
        if (cls == c) return true;
    return false;
}
// "CAR7" -> true ("CAR" and 1..2 digits, not 0)
inline bool extra_car_leaf(const std::string& car) {
    if (car.size() < 4 || car.size() > 5 || car.compare(0, 3, "CAR") != 0) return false;
    for (std::size_t i = 3; i < car.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(car[i]))) return false;
    return car[3] != '0';
}

// What a car file says about itself: "" usable, else why not. The version and layout are carmods' check
// (b3asset/car_layout.h car_header_error: 0x15 .. 0x17, the first LOD block at 0x16F0 or later); an old-layout prototype
// disc's file is pointed to the launcher's Beta / Prototype cars (the BETA series' importer converts those)
// (`head`: at least its first 0x64 bytes; `file_size`: the whole file's -- carpacks: the scan reads only the header)
inline std::string extra_car_head_problem(const std::uint8_t* head, std::size_t head_size, std::uintmax_t file_size) {
    if (file_size < 0x1700 || head_size < b3asset::kCarHeaderBytes) return "too short for a vehicle file";
    const std::string e = b3asset::car_header_error(head, head_size);
    if (!e.empty())
        return b3asset::car_version_ok(std::uint32_t(head[0] | head[1] << 8 | head[2] << 16) | std::uint32_t(head[3]) << 24)
                   ? "a prototype disc's car file (another layout): use Beta / Prototype cars above"
                   : e;
    const std::uint32_t size = std::uint32_t(head[8] | head[9] << 8 | head[10] << 16) | std::uint32_t(head[11]) << 24;
    if (file_size > kCarFileMax) return "larger than the game's room for a car (" + std::to_string(file_size) + " bytes)";
    if (size > file_size) return "shorter than its header says (a cut file)";   // (longer: padding the game never reads)
    if (size < 0x1700u) return "its header's size is not a car file's";
    return {};
}
inline std::string extra_car_file_problem(const std::vector<std::uint8_t>& b) { return extra_car_head_problem(b.data(), b.size(), b.size()); }
// A car file's problem from its first 0x100 bytes and its size ("" usable)
inline std::string extra_car_path_problem(const std::filesystem::path& p) {
    std::error_code ec;
    const std::uintmax_t n = std::filesystem::file_size(p, ec);
    if (ec) return "cannot be read";
    std::vector<std::uint8_t> head(0x100, 0);
    std::ifstream f(p, std::ios::binary);
    if (!f) return "cannot be read";
    f.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()));
    return extra_car_head_problem(head.data(), std::size_t(f.gcount()), n);
}
// Its engine sound models are in the file (+0x16DC / +0x16E0 inside it); else the donor's are added (traffic_model)
inline bool extra_car_has_sound(const std::vector<std::uint8_t>& b) {
    if (b.size() < 0x16F0) return false;
    auto u32 = [&](std::size_t o) {
        return std::uint32_t(b[o]) | std::uint32_t(b[o + 1]) << 8 | std::uint32_t(b[o + 2]) << 16 | std::uint32_t(b[o + 3]) << 24;
    };
    const std::uint32_t hi = u32(0x16DC), lo = u32(0x16E0);
    return hi >= 0x16F0 && lo >= 0x16F0 && hi < b.size() && lo < b.size();
}

// One extra car as the folder gives it
struct ExtraCar {
    std::string source;       // its folder entry (for the log: "COMP/CAR7.BGV", "my car/car.ini")
    std::string model;        // the host file of its model
    std::string hwd, lwd;     // its own sound banks (host files; "" the donor's)
    std::string donor_cls, donor_car;   // "COMP", "CAR7"
    std::string name;         // "" its donor's name with " CUSTOM" (extra_cars.h: the game's string)
    int series = kCustomSeries;   // its kind: kCustomSeries or kBetaSeries
    int slot = 0;             // 1.. : its GtID XCUS/CAR<slot> (XBET for BETA)
    int page = 0;             // carpacks: its page of its kind (slot - 1) / 20
    std::string key, replaced_by;   // extrastart: a BETA car's car.ini "key" / "replaced_by" (the same car from a later disc)
    int code() const { return extra_page_series(series, page); }   // its SELECT CAR series
    std::string cls() const { return series == kBetaSeries ? "XBET" : "XCUS"; }
    std::string car() const { return "CAR" + std::to_string(slot); }
    std::string gtid_name() const { return cls() + car(); }          // "XCUSCAR1" (its configs' name too)
    std::string donor_name() const { return donor_cls + donor_car; }  // "COMPCAR7"
    std::uint64_t id() const { return gtid_compress(gtid_name()); }
    std::uint64_t donor_id() const { return gtid_compress(donor_name()); }
    std::string disc_base() const { return "PVEH/" + cls() + "/" + car() + "."; }   // CreateVehiclePathFromIndex's
};

// A folder entry: "COMP/CAR7.BGV" (a mod named after the car it replaces: that car is its donor) -> the donor's class and
// car; false for any other name
inline bool extra_named_entry(const std::string& rel, std::string* cls, std::string* car) {
    const std::string p = traffic_upper_path(rel);
    const std::size_t sl = p.find('/');
    if (sl == std::string::npos || p.find('/', sl + 1) != std::string::npos) return false;
    if (p.size() < sl + 5 || p.compare(p.size() - 4, 4, ".BGV") != 0) return false;
    const std::string c = p.substr(0, sl), k = p.substr(sl + 1, p.size() - 4 - sl - 1);
    if (!extra_donor_class(c) || !extra_car_leaf(k)) return false;
    *cls = c;
    *car = k;
    return true;
}

// car.ini's model / hwd / lwd: a plain file name in the car's own folder (no folder part, no "..", not absolute)
inline bool extra_plain_name(const std::string& s) {
    if (s.empty() || s == "." || s == ".." || s.size() > 255) return false;
    for (char c : s)
        if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) return false;
    return true;
}
// car.ini (a folder of its own): "name = ...", "donor = COMP/CAR7", "model = file.bgv" (default: the folder's one .BGV),
// "hwd = ...", "lwd = ..." (default: the donor's banks). Keys are case-insensitive; '#' / ';' start a comment line.
struct ExtraIni { std::string name, donor_cls, donor_car, model, hwd, lwd, problem, key, replaced_by; };
inline ExtraIni extra_parse_ini(const std::string& text) {
    ExtraIni r;
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#' || line[first] == ';') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            const std::size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        std::string key = trim(line.substr(0, eq));
        for (char& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
        const std::string v = trim(line.substr(eq + 1));
        if (key == "name") r.name = v;
        else if (key == "model") r.model = v;
        else if (key == "hwd") r.hwd = v;
        else if (key == "lwd") r.lwd = v;
        else if (key == "key") r.key = v;                   // extrastart (BETA's car.ini)
        else if (key == "replaced_by") r.replaced_by = v;
        else if (key == "donor") {
            const std::string d = traffic_upper_path(v);
            const std::size_t sl = d.find('/');
            if (sl == std::string::npos || !extra_donor_class(d.substr(0, sl)) || !extra_car_leaf(d.substr(sl + 1)))
                r.problem = "its donor \"" + v + "\" is not a race car (COMP/CAR7, HEVY/CAR2, ...)";
            else r.donor_cls = d.substr(0, sl), r.donor_car = d.substr(sl + 1);
        }
    }
    if (r.problem.empty() && r.donor_cls.empty()) r.problem = "car.ini names no donor (donor = COMP/CAR7: the car it was made to replace)";
    for (const std::string* f : {&r.model, &r.hwd, &r.lwd})
        if (r.problem.empty() && !f->empty() && !extra_plain_name(*f))
            r.problem = "car.ini's \"" + *f + "\" is not a file name in the car's own folder";
    return r;
}
// A name as SELECT CAR shows it: printable ASCII, upper case (the game's car names are), at most 32 characters
inline std::string extra_display_name(const std::string& s) {
    std::string o;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E) continue;
        o += char(std::toupper(u));
        if (o.size() == 32) break;   // (carpacks: "Euro-Circuit Racer (My car pack)", 30)
    }
    const std::size_t a = o.find_first_not_of(' '), b = o.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : o.substr(a, b - a + 1);
}

// The slots in order: the first `room` of `cars` (sorted by source) get slots 1.. (the rest: no room, `dropped`)
inline void extra_assign_slots(std::vector<ExtraCar>& cars, int room, std::vector<ExtraCar>* dropped) {
    std::stable_sort(cars.begin(), cars.end(), [](const ExtraCar& a, const ExtraCar& b) {
        return a.series != b.series ? a.series < b.series : traffic_upper_path(a.source) < traffic_upper_path(b.source);
    });
    if (room < 0) room = 0;
    if (int(cars.size()) > room) {
        if (dropped) dropped->assign(cars.begin() + room, cars.end());
        cars.resize(std::size_t(room));
    }
    int custom = 0, beta = 0;
    for (ExtraCar& c : cars) {
        c.slot = c.series == kBetaSeries ? ++beta : ++custom;
        c.page = (c.slot - 1) / kExtraPageCars;
    }
}

// A whole file, at most `max` bytes (its size checked before it is read: a car folder is the player's, untrusted)
constexpr std::uintmax_t kExtraReadMax = 0x100000u;   // a car file is at most kCarFileMax; a donor's the same
constexpr std::uintmax_t kExtraIniMax = 0x10000u;     // car.ini
constexpr std::uintmax_t kExtraBankMax = 0x80000u;    // an engine sound bank (the disc's 134: 34 KB .. 248 KB)
inline bool extra_read_file(const std::filesystem::path& p, std::vector<std::uint8_t>& b, std::uintmax_t max = kExtraReadMax) {
    std::error_code ec;
    const std::uintmax_t n = std::filesystem::file_size(p, ec);
    if (ec || n == 0 || n > max) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    b.resize(std::size_t(n));
    f.read(reinterpret_cast<char*>(b.data()), std::streamsize(n));
    return std::size_t(f.gcount()) == b.size();
}
// An engine sound bank (.HWD / .LWD) the game can be given: "" usable, else why not. Every bank on the disc starts
// 0x809, then 0x800 and 0x2C at +8 / +0xC [the disc's 134 banks]
inline std::string extra_bank_problem(const std::filesystem::path& p) {
    std::error_code ec;
    const std::uintmax_t n = std::filesystem::file_size(p, ec);
    if (ec) return "its sound bank cannot be read";
    if (n < 0x30 || n > kExtraBankMax) return "its sound bank is not one (" + std::to_string(n) + " bytes)";
    std::vector<std::uint8_t> b(16);
    std::ifstream f(p, std::ios::binary);
    if (!f.read(reinterpret_cast<char*>(b.data()), 16)) return "its sound bank cannot be read";
    auto u32 = [&](std::size_t o) { return std::uint32_t(b[o] | b[o + 1] << 8 | b[o + 2] << 16) | std::uint32_t(b[o + 3]) << 24; };
    if (u32(0) != 0x809u || u32(8) != 0x800u || u32(0xC) != 0x2Cu) return "its sound bank is not one the game reads";
    return {};
}
// A file beside `f` with its stem and the extension `ext` (".HWD"), names compared without case ("" none)
inline std::filesystem::path extra_sibling(const std::filesystem::path& f, const char* ext) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string want = traffic_upper_path(b3io::u8string(f.stem()) + ext);
    for (fs::directory_iterator it(f.parent_path(), ec), end; !ec && it != end; it.increment(ec))
        if (traffic_upper_path(b3io::u8string(it->path().filename())) == want && it->is_regular_file(ec)) return it->path();
    return {};
}
// The cars folder (the game and the launcher read it the same way): each class folder's files named after the car they
// replace (COMP/CAR7.BGV, with CAR7.HWD / .LWD beside it when it has its own banks), each folder with a car.ini; the
// slots in the folder's order (extra_assign_slots). `refused`: (the entry, why) for each file left out.
struct ExtraScan {
    std::vector<ExtraCar> cars;
    std::vector<std::pair<std::string, std::string>> refused;
};
inline ExtraScan extra_scan(const std::filesystem::path& dir, int series = kCustomSeries) {
    namespace fs = std::filesystem;
    ExtraScan out;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return out;
    auto refuse = [&](const std::string& what, const std::string& why) { out.refused.emplace_back(what, why); };
    auto bgv = [](const std::string& name) {
        const std::string u = traffic_upper_path(name);
        return u.size() > 4 && u.compare(u.size() - 4, 4, ".BGV") == 0;
    };
    auto banks_ok = [&](const ExtraCar& x, const std::string& what) {   // its own banks: served to the game as they are
        for (const std::string* bank : {&x.hwd, &x.lwd})
            if (!bank->empty())
                if (const std::string why = extra_bank_problem(b3io::u8path(*bank)); !why.empty()) { refuse(what, why); return false; }
        return true;
    };
    auto usable = [&](const fs::path& f, const std::string& what) {   // (its header and size: 200 cars read quickly)
        const std::string why = extra_car_path_problem(f);
        if (!why.empty()) { refuse(what, why); return false; }
        return true;
    };
    std::vector<ExtraCar> found;
    auto sorted_entries = [&](const fs::path& d) {
        std::vector<fs::path> v;
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string leaf = b3io::u8string(it->path().filename());
            if (!leaf.empty() && leaf[0] != '.') v.push_back(it->path());   // hidden: an import still being written
        }
        std::sort(v.begin(), v.end());
        return v;
    };
    auto ini_folder = [&](const fs::path& e, const std::string& rel) {   // a folder of its own: <rel>/car.ini
        const fs::path ini = e / "car.ini";
        const std::string what = rel + "/car.ini";
        std::vector<std::uint8_t> raw;
        if (!extra_read_file(ini, raw, kExtraIniMax)) { refuse(what, "cannot be read (or larger than 64 KB)"); return; }
        const ExtraIni c = extra_parse_ini(std::string(raw.begin(), raw.end()));
        if (!c.problem.empty()) { refuse(what, c.problem); return; }
        fs::path model = c.model.empty() ? fs::path() : e / b3io::u8path(c.model);
        if (model.empty()) {
            bool two = false;
            for (fs::directory_iterator jt(e, ec), end; !ec && jt != end; jt.increment(ec)) {
                if (!bgv(b3io::u8string(jt->path().filename()))) continue;
                if (!model.empty()) two = true;
                model = jt->path();
            }
            if (two) { refuse(what, "more than one .BGV: name it (model = ...)"); return; }
            if (model.empty()) { refuse(what, "no .BGV in its folder"); return; }
        }
        if (!usable(model, what)) return;
        ExtraCar x;
        x.source = what;
        x.model = b3io::u8string(model);
        x.donor_cls = c.donor_cls, x.donor_car = c.donor_car;
        x.name = extra_display_name(c.name);
        x.key = c.key, x.replaced_by = c.replaced_by;
        if (!c.hwd.empty()) x.hwd = b3io::u8string(e / b3io::u8path(c.hwd));
        if (!c.lwd.empty()) x.lwd = b3io::u8string(e / b3io::u8path(c.lwd));
        if (!banks_ok(x, what)) return;
        found.push_back(x);
    };
    auto class_folder = [&](const fs::path& e, const std::string& rel) {   // <rel> = (<pack>/)COMP: CAR7.BGV and its banks
        const std::string cls = b3io::u8string(e.filename());
        for (const fs::path& f : sorted_entries(e)) {
            const std::string name = b3io::u8string(f.filename());
            if (!bgv(name)) continue;
            const std::string what = rel + "/" + name;
            ExtraCar x;
            if (!extra_named_entry(cls + "/" + name, &x.donor_cls, &x.donor_car)) {
                refuse(what, "not named after a race car (COMP/CAR7.BGV, HEVY/CAR2.BGV, ...)");
                continue;
            }
            if (!usable(f, what)) continue;
            x.source = what;
            x.model = b3io::u8string(f);
            if (const fs::path h = extra_sibling(f, ".HWD"); !h.empty()) x.hwd = b3io::u8string(h);
            if (const fs::path l = extra_sibling(f, ".LWD"); !l.empty()) x.lwd = b3io::u8string(l);
            if (!banks_ok(x, what)) continue;
            found.push_back(x);
        }
    };
    const char* const kLoose = "a car file needs a folder named after the car it replaces (COMP/CAR7.BGV) or a folder with a car.ini";
    for (const fs::path& e : sorted_entries(dir)) {
        const std::string leaf = b3io::u8string(e.filename());
        if (!fs::is_directory(e, ec)) {
            if (bgv(leaf)) refuse(leaf, kLoose);
            continue;
        }
        if (fs::is_regular_file(e / "car.ini", ec)) { ini_folder(e, leaf); continue; }
        if (extra_donor_class(traffic_upper_path(leaf))) { class_folder(e, leaf); continue; }
        // carpacks: a pack's folder (the launcher's Import car pack: <pack>/<CLS>-CARn/car.ini), or a pack laid out as
        // the game's folders (<pack>/COMP/CAR7.BGV)
        for (const fs::path& sub : sorted_entries(e)) {
            const std::string rel = leaf + "/" + b3io::u8string(sub.filename());
            if (!fs::is_directory(sub, ec)) {
                if (bgv(rel)) refuse(rel, kLoose);
                continue;
            }
            if (fs::is_regular_file(sub / "car.ini", ec)) ini_folder(sub, rel);
            else if (extra_donor_class(traffic_upper_path(b3io::u8string(sub.filename())))) class_folder(sub, rel);
        }
    }
    for (ExtraCar& c : found) c.series = series;
    const int room = series == kBetaSeries ? kBetaCarsMax : kCustomCarsMax;
    std::vector<ExtraCar> dropped;
    extra_assign_slots(found, room, &dropped);
    for (const ExtraCar& d : dropped) refuse(d.source, "no room (" + std::to_string(room) + " cars at most)");
    out.cars = found;
    return out;
}
// extrastart: the BETA cars a later disc has too (b3asset::beta's table, replaced_by; tests/test_carpack.cpp checks the
// two agree): {earlier, later}
inline constexpr const char* kBetaReplaced[][2] = {{"jun08-mscl-car2", "jun21-mscl-car2"}};
// Both folders: CUSTOM's cars (the player's car mods) and BETA's (the launcher's Beta / Prototype cars import: one folder per
// car, NN-<key>/car.bgv + car.ini), the slots of each from 1 (kCustomCarsMax / kBetaCarsMax), in pages of 19
inline ExtraScan extra_scan_all(const std::filesystem::path& custom, const std::filesystem::path& beta) {
    ExtraScan a = extra_scan(custom, kCustomSeries);
    ExtraScan b = extra_scan(beta, kBetaSeries);
    // extrastart: a car whose later version is in the folder too (a removal that failed: a file in use) is left out, so
    // the same car never shows twice
    std::vector<ExtraCar> kept;
    for (const ExtraCar& c : b.cars) {
        bool later = false;
        std::string by = c.replaced_by;
        for (const auto& k : kBetaReplaced)   // (a car.ini written before extrastart has no replaced_by line)
            if (by.empty() && c.key == k[0]) by = k[1];
        if (!by.empty())
            for (const ExtraCar& o : b.cars) later = later || o.key == by;
        if (later) b.refused.emplace_back(c.source, "an earlier copy of " + c.name + " (its later version is there)");
        else kept.push_back(c);
    }
    b.cars = std::move(kept);
    for (auto& r : b.refused) a.refused.emplace_back("beta/" + r.first, r.second);
    for (ExtraCar& c : b.cars) c.source = "beta/" + c.source;
    a.cars.insert(a.cars.end(), b.cars.begin(), b.cars.end());
    std::vector<ExtraCar> dropped;
    extra_assign_slots(a.cars, kExtraCarsMax, &dropped);
    for (const ExtraCar& d : dropped) a.refused.emplace_back(d.source, "no room (" + std::to_string(kExtraCarsMax) + " extra cars at most)");
    return a;
}

// The launcher's Import from folder (customseries): the car mods in `from` copied into the CUSTOM folder `custom`, checked
// as the game reads them -- a folder with a car.ini (`from` itself or one inside it) copied as a folder of its own;
// each other .BGV in a folder named after its class and named after the car it replaces (COMP/CAR7.BGV, its .HWD / .LWD
// beside it) copied to that class folder. A car already in the folder is refused (Remove it there first); symbolic links
// are never followed or copied; a folder with a car.ini gives only its car's own files. Up to four folders deep. `done` / `refused`:
// what was copied, what was not and why. Nothing is written for a file refused.
struct ExtraImport {
    std::vector<std::string> done;
    std::vector<std::pair<std::string, std::string>> refused;
};
inline ExtraImport extra_import(const std::filesystem::path& from, const std::filesystem::path& custom) {
    namespace fs = std::filesystem;
    ExtraImport out;
    std::error_code ec;
    if (from.empty() || !fs::is_directory(from, ec)) {
        out.refused.emplace_back(b3io::u8string(from), "not a folder");
        return out;
    }
    const char* const kThere = "already in the custom cars folder (Remove it there first)";
    auto upper_ext = [](const fs::path& p) { return traffic_upper_path(b3io::u8string(p.extension())); };
    auto check = [&](const fs::path& f, const std::string& what) {
        std::vector<std::uint8_t> b;
        if (!extra_read_file(f, b)) { out.refused.emplace_back(what, "cannot be read (or larger than a car file can be)"); return false; }
        const std::string why = extra_car_file_problem(b);
        if (!why.empty()) { out.refused.emplace_back(what, why); return false; }
        return true;
    };
    auto copy = [&](const fs::path& src, const fs::path& dst) {
        std::error_code e2;
        fs::create_directories(dst.parent_path(), e2);
        const fs::path tmp = dst.string() + ".tmp";
        fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, e2);
        if (!e2) fs::rename(tmp, dst, e2);
        if (e2) fs::remove(tmp, e2);
        return !e2;
    };
    // what is in `from` (symbolic links never followed nor copied: only the folder's own files)
    std::vector<fs::path> ini_dirs, files;
    if (fs::is_regular_file(from / "car.ini", ec) && !fs::is_symlink(from / "car.ini", ec)) ini_dirs.push_back(from);
    else
        for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_symlink(ec)) { it.disable_recursion_pending(); continue; }
            if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
            if (it->is_directory(ec) && fs::is_regular_file(it->path() / "car.ini", ec) && !fs::is_symlink(it->path() / "car.ini", ec)) {
                ini_dirs.push_back(it->path());
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec) && upper_ext(it->path()) == ".BGV") files.push_back(it->path());
        }
    std::sort(ini_dirs.begin(), ini_dirs.end());
    std::sort(files.begin(), files.end());
    for (const fs::path& d : ini_dirs) {   // a folder of its own: checked as extra_scan reads it; its car's files copied
        const std::string name = b3io::u8string(d.filename());
        ExtraScan one;
        {
            ExtraScan all = extra_scan(d.parent_path());
            for (ExtraCar& c : all.cars) if (c.source == name + "/car.ini") one.cars.push_back(c);
            for (auto& x : all.refused) if (x.first.compare(0, name.size() + 1, name + "/") == 0) one.refused.push_back(x);
        }
        if (one.cars.empty()) {
            for (auto& x : one.refused) out.refused.push_back(x);
            if (one.refused.empty()) out.refused.emplace_back(name + "/car.ini", "no car in it");
            continue;
        }
        if (fs::exists(custom / name, ec)) { out.refused.emplace_back(name + "/", kThere); continue; }
        const ExtraCar& c = one.cars.front();
        std::vector<fs::path> mine = {d / "car.ini", b3io::u8path(c.model)};
        if (!c.hwd.empty()) mine.push_back(b3io::u8path(c.hwd));
        if (!c.lwd.empty()) mine.push_back(b3io::u8path(c.lwd));
        bool ok = true;
        for (const fs::path& f : mine) {
            if (fs::is_symlink(f, ec) || f.parent_path() != d) { ok = false; break; }   // (extra_plain_name: always in d)
            ok = copy(f, custom / name / f.filename()) && ok;
        }
        if (ok) out.done.push_back(name + "/");
        else {
            fs::remove_all(custom / name, ec);
            out.refused.emplace_back(name + "/", "could not be copied");
        }
    }
    for (const fs::path& f : files) {
        const std::string cls = traffic_upper_path(b3io::u8string(f.parent_path().filename()));
        const std::string rel = cls + "/" + b3io::u8string(f.filename());
        std::string dc, dcar;
        if (!extra_named_entry(rel, &dc, &dcar)) {
            out.refused.emplace_back(b3io::u8string(fs::relative(f, from, ec)),
                                     "not named after a race car in a folder named after its class (COMP/CAR7.BGV)");
            continue;
        }
        if (!check(f, rel)) continue;
        if (!extra_sibling(custom / dc / (dcar + ".BGV"), ".BGV").empty()) {   // (its name in any case)
            out.refused.emplace_back(rel, kThere);
            continue;
        }
        std::vector<std::pair<fs::path, const char*>> banks;
        bool bad_bank = false;
        for (const char* ext : {".HWD", ".LWD"})
            if (const fs::path bank = extra_sibling(f, ext); !bank.empty() && !fs::is_symlink(bank, ec)) {
                if (const std::string why = extra_bank_problem(bank); !why.empty()) { out.refused.emplace_back(rel, why); bad_bank = true; break; }
                banks.emplace_back(bank, ext);
            }
        if (bad_bank) continue;
        bool ok = copy(f, custom / dc / (dcar + ".BGV"));
        for (const auto& [bank, ext] : banks) ok = copy(bank, custom / dc / (dcar + ext)) && ok;
        if (ok) out.done.push_back(dc + "/" + dcar + ".BGV");
        else out.refused.emplace_back(rel, "could not be copied");
    }
    if (out.done.empty() && out.refused.empty()) out.refused.emplace_back(b3io::u8string(from.filename()), "no car files in it (.BGV)");
    return out;
}
// The launcher's Remove: an extra car's files in the CUSTOM folder (a class folder's .BGV and its banks, or the folder
// with its car.ini); false when nothing was removed
inline bool extra_remove(const std::filesystem::path& custom, const ExtraCar& c) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string src = c.source;
    if (src.size() > 8 && src.compare(src.size() - 8, 8, "/car.ini") == 0) {
        const fs::path d = custom / b3io::u8path(src.substr(0, src.size() - 8));
        return fs::remove_all(d, ec) > 0 && !ec;
    }
    const fs::path f = custom / b3io::u8path(src);
    std::vector<fs::path> banks;
    for (const char* ext : {".HWD", ".LWD"})
        if (fs::path b = extra_sibling(f, ext); !b.empty()) banks.push_back(b);
    bool any = fs::remove(f, ec);
    for (const fs::path& b : banks) any = fs::remove(b, ec) || any;
    return any;
}

// The index in `cars` of a GtID (-1 none)
inline int extra_index(const std::vector<ExtraCar>& cars, std::uint64_t id) {
    if (!id) return -1;
    for (std::size_t i = 0; i < cars.size(); ++i)
        if (cars[i].id() == id) return int(i);
    return -1;
}
// A series' cars (indices into `cars`) in SELECT CAR's order
inline std::vector<int> extra_series_cars(const std::vector<ExtraCar>& cars, int series) {
    std::vector<int> v;
    for (std::size_t i = 0; i < cars.size(); ++i)
        if (cars[i].code() == series) v.push_back(int(i));
    return v;
}
inline bool extra_series(int c) { return c >= kCustomSeries && c < kCustomSeries + 2 * (kExtraPagesMax + 1); }
inline int extra_series_kind(int c) { return (c - kCustomSeries) % 2 ? kBetaSeries : kCustomSeries; }
inline int extra_series_page(int c) { return (c - kCustomSeries) / 2; }
// SELECT CAR's extra series in order (CUSTOM's pages, then BETA's), only those with cars
inline std::vector<int> extra_series_order(const std::vector<ExtraCar>& cars) {
    std::vector<int> v;
    for (int kind : {kCustomSeries, kBetaSeries})
        for (int page = 0; page <= kExtraPagesMax; ++page) {
            const int c = extra_page_series(kind, page);
            for (const ExtraCar& x : cars)
                if (x.code() == c) { v.push_back(c); break; }
        }
    return v;
}

// A disc path the game asks for -> the file to read ("" none): an extra car's .BGV -> "host:" its model file (`model`:
// the one made for it, extra_cars.h), its .HWD / .LWD -> "host:" its own bank or its donor's disc path
inline std::string extra_alias(const std::vector<ExtraCar>& cars, const std::vector<std::string>& models, const std::string& path) {
    const std::string p = traffic_upper_path(path);
    for (std::size_t i = 0; i < cars.size(); ++i) {
        const ExtraCar& c = cars[i];
        const std::string base = c.disc_base();
        if (p.size() != base.size() + 3 || p.compare(0, base.size(), base) != 0) continue;
        const std::string ext = p.substr(base.size());
        if (ext == "BGV") return i < models.size() && !models[i].empty() ? "host:" + models[i] : std::string();
        if (ext == "HWD") return !c.hwd.empty() ? "host:" + c.hwd : "PVEH/" + c.donor_cls + "/" + c.donor_car + ".HWD";
        if (ext == "LWD") return !c.lwd.empty() ? "host:" + c.lwd : "PVEH/" + c.donor_cls + "/" + c.donor_car + ".LWD";
        return {};
    }
    return {};
}
// A per-vehicle config file an extra car racing now registers (`racing`: indices into `cars`) -> its donor's ("" keeps
// it). Every file (VehiclePhysics, Camera, VehicleAudio, VehicleMaterial, AI): the donor's handling whole (the mod's own
// values: a later option). Only an extra car's names are ever redirected (no retail car's file is named XCUS / XBET).
inline std::string extra_config(const std::vector<ExtraCar>& cars, const std::vector<int>& racing, const std::string& file) {
    const std::size_t sl = file.find_last_of("/\\");
    if (sl == std::string::npos) return {};
    std::string leaf;
    for (std::size_t i = sl + 1; i < file.size(); ++i) leaf += char(std::toupper(static_cast<unsigned char>(file[i])));
    for (int i : racing) {
        if (i < 0 || i >= int(cars.size())) continue;
        if (leaf != cars[std::size_t(i)].gtid_name() + ".CFG") continue;
        return file.substr(0, sl + 1) + cars[std::size_t(i)].donor_name() + ".cfg";
    }
    return {};
}

// SELECT CAR's series after `c` going right and left (-1 none): CUSTOM and BETA after `end`, the last series before them
// (the traffic series' last, or the carousel's own last); only the series with cars (`custom` / `beta`)
inline int extra_next_series(int c, int end, const std::vector<int>& order) {
    if (order.empty()) return -1;
    if (c == end) return order.front();
    for (std::size_t i = 0; i + 1 < order.size(); ++i)
        if (order[i] == c) return order[i + 1];
    return -1;
}
inline int extra_prev_series(int c, int end, const std::vector<int>& order) {
    for (std::size_t i = 0; i < order.size(); ++i)
        if (order[i] == c) return i == 0 ? end : order[i - 1];
    return -1;
}

// The series' names and the name's suffix of a car without its own name, in the game's text language (platform_disc.h
// language_text: "" / US / EN English, FR, GE, IT, SP, DU, SW), UTF-8
struct ExtraWords { const char* code; const char* custom; const char* beta; const char* mod; };
inline const ExtraWords& extra_words(const std::string& code) {
    static const ExtraWords k[] = {
        {"", "CUSTOM", "BETA", " MOD"},
        {"FR", "PERSONNALIS\xC3\x89" "ES", "B\xC3\x8ATA", " MOD"},
        {"GE", "EIGENE", "BETA", " MOD"},
        {"IT", "PERSONALIZZATE", "BETA", " MOD"},
        {"SP", "PERSONALIZADOS", "BETA", " MOD"},
        {"DU", "EIGEN", "B\xC3\x88TA", " MOD"},
        {"SW", "EGNA", "BETA", " MOD"},
    };
    for (const ExtraWords& w : k)
        if (code == w.code) return w;
    return k[0];
}
// UTF-8 -> UTF-16 (the game's strings; the BMP only, a broken sequence skipped)
inline std::u16string extra_utf16(const std::string& s) {
    std::u16string u;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = 0, n = 0;
        if (c < 0x80) cp = c, n = 1;
        else if ((c & 0xE0) == 0xC0) cp = c & 0x1F, n = 2;
        else if ((c & 0xF0) == 0xE0) cp = c & 0x0F, n = 3;
        else { ++i; continue; }
        if (i + n > s.size()) break;
        bool ok = true;
        for (unsigned k = 1; k < n; ++k) {
            const unsigned char d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) { ok = false; break; }
            cp = cp << 6 | (d & 0x3F);
        }
        i += ok ? n : 1;
        if (ok) u += char16_t(cp);
    }
    return u;
}

// The renderer's record word (src/b3replay/records.h options_folder; every bit's owner: tests/test_bit_owners.cpp): bit 29
// for a race with an extra car as the player's car, with the race's options when it has some (0x8002: the flag, TRAFFIC
// Normal): "options-extracars", one folder with the others
constexpr std::uint64_t kExtraRecordBit = 0x20000000ull;
inline std::uint64_t extra_record_word(std::uint64_t options = 0) { return (options ? options : 0x8002ull) | kExtraRecordBit; }

// FNV-1a 64 of a file's bytes (the replay's entry names each car by its content)
inline std::uint64_t extra_hash(const std::vector<std::uint8_t>& b) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (std::uint8_t c : b) h = (h ^ c) * 0x100000001B3ull;
    return h;
}
// carpacks: each car file's hash kept between starts (cache/extra/hashes.txt: "<hash> <size> <mtime> <path>" a line), so
// a start with 200 cars reads only the files that changed. hash(): the cached one when the size and time are the same,
// else the file read and hashed (0: it cannot be read).
class ExtraHashCache {
public:
    void load(const std::filesystem::path& f) {
        file_ = f;
        std::vector<std::uint8_t> raw;
        if (!extra_read_file(f, raw, 0x400000u)) return;
        std::string text(raw.begin(), raw.end());
        std::size_t at = 0;
        while (at < text.size()) {
            std::size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            const std::string line = text.substr(at, end - at);
            at = end + 1;
            unsigned long long h = 0, n = 0;
            long long t = 0;
            int used = 0;
            if (std::sscanf(line.c_str(), "%llx %llu %lld %n", &h, &n, &t, &used) == 3 && used > 0)
                map_[line.substr(std::size_t(used))] = Entry{h, n, t};
        }
    }
    std::uint64_t hash(const std::string& path) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const std::uintmax_t n = fs::file_size(b3io::u8path(path), ec);
        if (ec) return 0;
        const long long t = static_cast<long long>(fs::last_write_time(b3io::u8path(path), ec).time_since_epoch().count());
        if (ec) return 0;
        if (const auto it = map_.find(path); it != map_.end() && it->second.size == n && it->second.time == t) return it->second.hash;
        std::vector<std::uint8_t> b;
        if (!extra_read_file(b3io::u8path(path), b)) return 0;
        const std::uint64_t h = extra_hash(b);
        map_[path] = Entry{h, n, t};
        dirty_ = true;
        return h;
    }
    void save() {
        if (!dirty_ || file_.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(file_.parent_path(), ec);
        const std::filesystem::path tmp = file_.string() + ".tmp";
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            for (const auto& [p, e] : map_) {
                char head[64];
                std::snprintf(head, sizeof head, "%016llx %llu %lld ", static_cast<unsigned long long>(e.hash),
                              static_cast<unsigned long long>(e.size), e.time);
                o << head << p << '\n';
            }
        }
        std::filesystem::rename(tmp, file_, ec);
        if (ec) std::filesystem::remove(tmp, ec);
        dirty_ = false;
    }

private:
    struct Entry { std::uint64_t hash; std::uintmax_t size; long long time; };
    std::filesystem::path file_;
    std::map<std::string, Entry> map_;
    bool dirty_ = false;
};

}  // namespace b3native

#endif
