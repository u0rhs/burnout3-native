// traffic_series.h -- trafficseries (2026-11-22): the TRAFFIC series on SINGLE EVENT's SELECT CAR, traffic vehicles driven
// as race cars. The rules and tables as values (pure: tests/test_traffic_series.cpp); the hooks are traffic_cars.h.
//
// What the game has [C the decomp; addresses retail]:
// - The vehicle list D_005186A0 (PVEH/VLIST.BIN): the 67 race cars (.BGV + .HWD / .LWD) and the 40 traffic vehicles
//   (.BTV only): COMP CAR11-19, HEVY CAR11-35, TSPC CAR1-6. Their GtIDs are in the program's traffic table D_004CDC00
//   (40 u64, CB3HardCodedVehicleList::FindTrafficVehicle 0x3EAD20), in that order; func_003EACC0(index, 1) gives each
//   one's name string D_004CDD40[index] (GLOBALUS.BIN 1894..1928: Car, Taxi, Tuk Tuk, Pickup, Van, Bus, Big Rig, the
//   trailers, Minibus, Gritting Truck, Motorhome, Tram), in every language's table. CB3VehicleList::GetClassFromID
//   0x251CB0 classes a car by its name's first four letters: COMP 0, HEVY 6, TSPC 8; class 7 ("TRAF") has no vehicle.
//   The series names are strings too: 380 "TRAFFIC", 381 "TRAFFIC SPECIAL".
// - A .BTV is the race car's file format (version 0x17: LODs, 6 panel and 4 wheel frames, damage influences), without the
//   engine sound models a race car's sound reads from its model file (+0x16DC high, +0x16E0 low; func_00288960) and
//   without .HWD / .LWD sound banks. Its VDB.XML tuning has at most 9 values (mass, the suspension), no Camera,
//   VehicleAudio or AI file.
// The series: after the last series the event allows, TRAFFIC (class 7: the cars, taxis, the tuk tuk, pickups, vans, the
// minibus, 20) and TRAFFIC SPECIAL (class 8, which only the trams have otherwise: the buses, big rigs, the gritting truck,
// the motorhome, 7): the carousel holds 20 cars a series [C func_0042B7C0: two tables of 20 12-byte rows]. Not the seven
// trailers HEVY CAR24-30 (no cab) nor the trams. Each takes a race car's ("its donor's", by type and size) engine sound
// and tuning, with its own suspension geometry where it has a VDB file.
#ifndef B3NATIVE_TRAFFIC_SERIES_H
#define B3NATIVE_TRAFFIC_SERIES_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace b3native {

// GtIDCompress 0x21AEF0 [C]: up to 12 characters, base 40
inline std::uint64_t gtid_compress(const std::string& s) {
    std::uint64_t v = 0;
    std::size_t i = 0;
    for (; i < 12 && i < s.size(); ++i) {
        const char c = s[i];
        v *= 40;
        if (c == '_') v += 39;
        else if (c >= 'a') v += std::uint64_t(c - 0x54);
        else if (c >= 'A') v += std::uint64_t(c - 0x34);
        else if (c >= '0') v += std::uint64_t(c - 0x2D);
        else if (c == '/') v += 2;
        else if (c == '-') v += 1;
    }
    for (; i < 12; ++i) v *= 40;
    return v;
}

constexpr int kTrafficClass = 7;                 // [C] EB3MenuCarClasses' "TRAF": no vehicle of the disc has it
constexpr int kTrafficSpecialClass = 8;          // [C] "TSPC": the trams (not driveable)
constexpr int kTrafficSeriesString = 380;        // [C] GLOBALUS.BIN "TRAFFIC" (every language's table)
constexpr int kTrafficSpecialString = 381;       // [C] "TRAFFIC SPECIAL"
constexpr int kTrafficSeriesRows = 20;           // [C] func_0042B7C0: the rows of a series
constexpr int kTrafficVehicles = 40;             // [C] D_004CDC00's entries (FindTrafficVehicle 0x3EAD20)
constexpr std::uint32_t kTrafficIds = 0x004CDC00u, kTrafficNames = 0x004CDD40u;   // [C] u64[40], s32[40]

// A traffic vehicle (the traffic table's order) and its donor race car
struct TrafficEntry {
    const char* cls;          // "COMP"
    const char* car;          // "CAR18"
    const char* donor_cls;    // the race car whose engine sound and tuning it takes
    const char* donor_car;
    int series;               // its SELECT CAR series: kTrafficClass, kTrafficSpecialClass, 0 not offered
    bool own_geometry;        // its own suspension geometry (it has a VehiclePhysics VDB file)
    std::string name() const { return std::string(cls) + car; }            // "COMPCAR18" (its GtID's name, its configs')
    std::string donor_name() const { return std::string(donor_cls) + donor_car; }
    std::uint64_t id() const { return gtid_compress(name()); }
    std::uint64_t donor_id() const { return gtid_compress(donor_name()); }
};
// By type and size (the name strings and the VDB masses): cars -> compacts / a coupe, SUVs -> SUV Deluxe, pickups ->
// Heavy Pick Up, vans and the minibus -> B-Team Van, the big van and the motorhome -> Delivery Truck, buses -> City Bus,
// big rigs -> Tractor Cab, the gritting truck -> Trash Truck; trailers and trams (not offered) -> Tractor Cab / City Bus
inline const std::vector<TrafficEntry>& traffic_table() {
    static const std::vector<TrafficEntry> t = {
        {"COMP", "CAR11", "COMP", "CAR1", kTrafficClass, true},  {"COMP", "CAR12", "COMP", "CAR2", kTrafficClass, true},    // Car, Car
        {"COMP", "CAR13", "COMP", "CAR3", kTrafficClass, true},  {"COMP", "CAR14", "COMP", "CAR4", kTrafficClass, true},    // Car, Car
        {"COMP", "CAR15", "COMP", "CAR5", kTrafficClass, true},  {"COMP", "CAR16", "COMP", "CAR6", kTrafficClass, true},    // Taxi, Taxi
        {"COMP", "CAR17", "COMP", "CAR7", kTrafficClass, true},  {"COMP", "CAR18", "COMP", "CAR1", kTrafficClass, false},   // Taxi, Tuk Tuk
        {"COMP", "CAR19", "CUPE", "CAR1", kTrafficClass, true},                                                    // Car
        {"HEVY", "CAR11", "HEVY", "CAR3", kTrafficClass, true},  {"HEVY", "CAR12", "HEVY", "CAR3", kTrafficClass, true},    // Car, Car
        {"HEVY", "CAR13", "HEVY", "CAR3", kTrafficClass, true},  {"HEVY", "CAR14", "HEVY", "CAR1", kTrafficClass, true},    // Car, Pickup
        {"HEVY", "CAR15", "HEVY", "CAR1", kTrafficClass, true},  {"HEVY", "CAR16", "HEVY", "CAR5", kTrafficClass, true},    // Pickup, Van
        {"HEVY", "CAR17", "HEVY", "CAR5", kTrafficClass, true},  {"HEVY", "CAR18", "HEVY", "CAR6", kTrafficClass, true},    // Van, Van
        {"HEVY", "CAR19", "HEVY", "CAR9", kTrafficSpecialClass, true},  {"HEVY", "CAR20", "HEVY", "CAR9", kTrafficSpecialClass, true},    // Bus, Bus
        {"HEVY", "CAR21", "HEVY", "CAR9", kTrafficSpecialClass, true},  {"HEVY", "CAR22", "HEVY", "CAR7", kTrafficSpecialClass, true},    // Bus, Big Rig
        {"HEVY", "CAR23", "HEVY", "CAR7", kTrafficSpecialClass, true},                                                    // Big Rig
        {"HEVY", "CAR24", "HEVY", "CAR7", 0, true}, {"HEVY", "CAR25", "HEVY", "CAR7", 0, true},   // containers
        {"HEVY", "CAR26", "HEVY", "CAR7", 0, true}, {"HEVY", "CAR27", "HEVY", "CAR7", 0, true},   // container, flatbed
        {"HEVY", "CAR28", "HEVY", "CAR7", 0, true}, {"HEVY", "CAR29", "HEVY", "CAR7", 0, true},   // lumber, tanker
        {"HEVY", "CAR30", "HEVY", "CAR7", 0, true},                                                   // covered trailer
        {"HEVY", "CAR31", "HEVY", "CAR5", kTrafficClass, true},  {"HEVY", "CAR32", "HEVY", "CAR10", kTrafficSpecialClass, true},   // Minibus, Gritting
        {"HEVY", "CAR33", "HEVY", "CAR5", kTrafficClass, true},  {"HEVY", "CAR34", "HEVY", "CAR6", kTrafficSpecialClass, true},    // Van, Motorhome
        {"HEVY", "CAR35", "HEVY", "CAR5", kTrafficClass, false},                                                   // Van
        {"TSPC", "CAR1", "HEVY", "CAR9", 0, false}, {"TSPC", "CAR2", "HEVY", "CAR9", 0, false},   // trams
        {"TSPC", "CAR3", "HEVY", "CAR9", 0, true},  {"TSPC", "CAR4", "HEVY", "CAR9", 0, false},
        {"TSPC", "CAR5", "HEVY", "CAR9", 0, false}, {"TSPC", "CAR6", "HEVY", "CAR9", 0, false},
    };
    return t;
}
// The table's index of a GtID (the traffic table's), -1 none
inline int traffic_index(std::uint64_t id) {
    static const std::vector<std::uint64_t> ids = [] {
        std::vector<std::uint64_t> v;
        for (const TrafficEntry& t : traffic_table()) v.push_back(t.id());
        return v;
    }();
    if (id == 0) return -1;
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == id) return int(i);
    return -1;
}
// tsdonor: a traffic vehicle's id -> its donor race car's id (the one whose engine sound, tuning, Crashbreaker and AI
// series it takes); 0 for any other id. For gameopts' extra cars too (TrafficCars::set_extra_donors adds theirs)
inline std::uint64_t traffic_donor_id(std::uint64_t id) {
    const int k = traffic_index(id);
    return k < 0 ? 0 : traffic_table()[std::size_t(k)].donor_id();
}

// A series' vehicles in SELECT CAR's order (the table's)
inline std::vector<int> traffic_series_cars(int series) {
    std::vector<int> v;
    const auto& t = traffic_table();
    for (std::size_t i = 0; i < t.size(); ++i)
        if (t[i].series == series && series != 0) v.push_back(int(i));
    return v;
}
inline bool traffic_class(int c) { return c == kTrafficClass || c == kTrafficSpecialClass; }
// EB3MenuCarClasses of a race car class name (GetClassFromID's prefixes, the race classes)
inline int traffic_class_of(const char* cls) {
    static const char* const k[] = {"COMP", "MSCL", "CUPE", "SPRT", "SUPR", "HSPC", "HEVY"};
    for (int i = 0; i < 7; ++i)
        if (std::string(cls) == k[i]) return i;
    return 0;
}
// SELECT CAR's series after `c` going right (-1 none) and left, with the traffic series after the last one allowed
inline int traffic_next_class(int c, int last) { return c == last ? kTrafficClass : c == kTrafficClass ? kTrafficSpecialClass : -1; }
inline int traffic_prev_class(int c, int last) { return c == kTrafficSpecialClass ? kTrafficClass : c == kTrafficClass ? last : -1; }

// SELECT CAR: the series is offered in SINGLE EVENT (CB3MenuChoices +4 == 1), every mode, while on
inline bool traffic_series_event(bool on, int game_type) { return on && game_type == 1; }
// the last series the carousel's own bound allows [C func_0042C1A0: +0x265 0 -> 4, +0x264 0 -> 5, else 6]
inline int carousel_last_class(bool b265, bool b264) { return !b265 ? 4 : !b264 ? 5 : 6; }

// The model file a race car loads for a traffic vehicle [C func_00288960: a race car's engine sound models are its model
// file's +0x16DC (high) / +0x16E0 (low), file offsets fixed up at load, at the .BGV's tail (COMP/CAR1: 0x68210, 0x69E20,
// the file ending at 0x6A800); a .BTV has 0 there]: the .BTV, zeros up to the donor's first engine model offset, the
// donor's bytes from there to its end (unchanged, so their own offsets hold), the two offsets set. The header's size
// (+0x08) stays the .BTV's: the renderer knows a model by its header bytes 0..0x4C, so it draws the .BTV. Empty when the
// .BTV reaches past that offset (it would need rebasing) or a file is short.
inline std::vector<std::uint8_t> traffic_model(const std::vector<std::uint8_t>& btv, const std::vector<std::uint8_t>& bgv) {
    auto u32 = [](const std::vector<std::uint8_t>& b, std::size_t o) {
        return std::uint32_t(b[o]) | std::uint32_t(b[o + 1]) << 8 | std::uint32_t(b[o + 2]) << 16 | std::uint32_t(b[o + 3]) << 24;
    };
    auto put = [](std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
        for (int k = 0; k < 4; ++k) b[o + std::size_t(k)] = std::uint8_t(v >> (8 * k));
    };
    if (btv.size() < 0x16F0 || bgv.size() < 0x16F0) return {};
    const std::uint32_t hi = u32(bgv, 0x16DC), lo = u32(bgv, 0x16E0);
    const std::uint32_t from = std::min(hi ? hi : lo, lo ? lo : hi);
    if (!from || from >= bgv.size() || btv.size() > from) return {};
    std::vector<std::uint8_t> out(btv);
    out.resize(from, 0);
    out.insert(out.end(), bgv.begin() + from, bgv.end());
    put(out, 0x16DC, hi);
    put(out, 0x16E0, lo);
    return out;
}

inline std::string traffic_upper_path(const std::string& path) {
    std::string p;
    for (char c : path) p += c == '\\' ? '/' : char(std::toupper(static_cast<unsigned char>(c)));
    return p;
}
// A disc path a race car asks for -> the one to read ("" none): a traffic vehicle's CARn.BGV -> its .BTV (traffic_cars.h
// makes the model file of it), its CARn.HWD / .LWD -> its donor's. Those names are not on the disc otherwise.
inline std::string traffic_alias(const std::string& path) {
    const std::string p = traffic_upper_path(path);
    for (const TrafficEntry& t : traffic_table()) {
        const std::string base = std::string("PVEH/") + t.cls + "/" + t.car + ".";
        if (p.size() != base.size() + 3 || p.compare(0, base.size(), base) != 0) continue;
        const std::string ext = p.substr(base.size());
        if (ext == "BGV") return base + "BTV";
        if (ext == "HWD" || ext == "LWD") return std::string("PVEH/") + t.donor_cls + "/" + t.donor_car + "." + ext;
    }
    return {};
}
// A per-vehicle config file registered for a traffic vehicle racing now (`racing`: their table indices) -> the file the
// variable takes instead ("" keeps it): the donor's, except the suspension geometry (attach height, spring length) of a
// vehicle with its own VehiclePhysics file. Only the race car's own registrations: VehiclePhysics within
// CB3RaceCarPhysicsAttribs::Register 0x252C20 (`race_physics`; the traffic system registers the model's mass and
// suspension for the traffic on the road with CB3VehiclePhysicsAttribs::Register 0x252100), never VehicleMaterial (the
// traffic system's loader B3TrafficSystemLoadAndRender registers it, for the traffic); Camera, VehicleAudio and AI only
// race cars register [C the decomp's format strings and their users]
inline std::string traffic_config(const std::vector<int>& racing, const std::string& file, const std::string& name,
                                  const std::string& group, bool race_physics) {
    if (file.find("VehicleMaterial") != std::string::npos) return {};
    if (file.find("VehiclePhysics") != std::string::npos && !race_physics) return {};
    const std::size_t sl = file.find_last_of("/\\");
    if (sl == std::string::npos) return {};
    std::string leaf;
    for (std::size_t i = sl + 1; i < file.size(); ++i) leaf += char(std::toupper(static_cast<unsigned char>(file[i])));
    for (int i : racing) {
        if (i < 0 || i >= int(traffic_table().size())) continue;
        const TrafficEntry& t = traffic_table()[std::size_t(i)];
        if (leaf != t.name() + ".CFG") continue;
        const bool geometry = group.rfind("Physics/Suspension", 0) == 0 && (name == "Attatch Height" || name == "Spring Length");
        if (geometry && t.own_geometry && file.find("VehiclePhysics") != std::string::npos) return {};
        return file.substr(0, sl + 1) + t.donor_name() + ".cfg";
    }
    return {};
}

// The renderer's record word (src/b3replay/records.h options_folder; every bit's owner: tests/test_bit_owners.cpp): bit 28
// for a race with a traffic vehicle as the player's car, with the race's options when it has some (0x8002: the flag,
// TRAFFIC Normal): "options-trafficcars", one folder with the others
constexpr std::uint64_t kTrafficRecordBit = 0x10000000ull;
inline std::uint64_t traffic_record_word(std::uint64_t options = 0) { return (options ? options : 0x8002ull) | kTrafficRecordBit; }
// The replay's entry ("traffic_series" 0 / 1; a recording without it: off, the list it was made with)
inline bool traffic_series_entry(const char* v) { return v && v[0] == '1' && !v[1]; }

}  // namespace b3native

#endif
