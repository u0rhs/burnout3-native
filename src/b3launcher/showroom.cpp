// The launcher's showroom, its pure parts: src/b3launcher/showroom.h.
#include "showroom.h"

#include "b3asset/discfs.h"
#include "../../tools/native/extra_series.h"   // customseries: the extra cars folders, as the game reads them
#include "../../tools/native/traffic_series.h"  // trafficseries: the traffic vehicles SELECT CAR offers

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <tuple>

namespace b3launcher::showroom {

namespace fs = std::filesystem;

namespace {

std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24;
}

void put_utf8(std::string& s, std::uint32_t c) {
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xC0 | (c >> 6)); s += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { s += char(0xE0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F)); }
    else { s += char(0xF0 | (c >> 18)); s += char(0x80 | ((c >> 12) & 0x3F)); s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F)); }
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t k = 0;
    while (k < s.size() && s[k] == ' ') ++k;
    return s.substr(k);
}

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

constexpr int kNames = 67;

struct Folder { const char* name; const char* kind; };
// the game's order (the string table's): the five classes, the specials, the heavyweights
constexpr Folder kFolders[] = {{"COMP", "Compact"}, {"MSCL", "Muscle"}, {"CUPE", "Coupe"}, {"SPRT", "Sports"},
                               {"SUPR", "Super"},   {"HSPC", "Special"}, {"HEVY", "Heavyweight"}};

}  // namespace

std::vector<std::string> read_strings(const std::vector<std::uint8_t>& b) {
    std::vector<std::string> out;
    if (b.size() < 0x10) return out;
    const std::uint32_t n = u32(b, 8), table = u32(b, 12);
    if (n == 0 || n > 100000 || table < 0x10 || std::uint64_t(table) + 4ull * n > b.size()) return out;
    out.reserve(n);
    for (std::uint32_t k = 0; k < n; ++k) {
        std::size_t at = u32(b, table + 4 * std::size_t(k));
        std::string s;
        if (at < b.size()) {
            for (; at + 1 < b.size(); at += 2) {
                std::uint32_t c = std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8;
                if (c == 0) break;
                if (c >= 0xD800 && c < 0xDC00 && at + 3 < b.size()) {   // a surrogate pair
                    const std::uint32_t lo = std::uint32_t(b[at + 2]) | std::uint32_t(b[at + 3]) << 8;
                    if (lo >= 0xDC00 && lo < 0xE000) {
                        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                        at += 2;
                    }
                }
                put_utf8(s, c);
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<std::string> car_names(const std::vector<std::string>& strings) {
    int first = -1, first_upper = -1;
    for (std::size_t k = 0; k < strings.size(); ++k) {
        const std::string t = trim(strings[k]);
        if (upper(t) != "COMPACT TYPE 1") continue;
        if (t != upper(t)) { first = int(k); break; }   // the title-case block
        if (first_upper < 0) first_upper = int(k);
    }
    if (first < 0) first = first_upper;
    if (first < 0 || std::size_t(first) + kNames > strings.size()) return {};
    std::vector<std::string> out;
    for (int k = 0; k < kNames; ++k) out.push_back(trim(strings[std::size_t(first + k)]));
    return out;
}

int name_index(const std::string& folder, int n) {
    const std::string f = upper(folder);
    for (int c = 0; c < 5; ++c)
        if (f == kFolders[c].name) return n >= 1 && n <= 10 ? c * 10 + n - 1 : -1;
    if (f == "HSPC") {   // the specials but the fire truck: 50 51 52 53 55 56
        if (n < 20 || n > 25) return -1;
        return n <= 23 ? 50 + (n - 20) : 55 + (n - 24);
    }
    if (f == "HEVY") {
        if (n == 36) return 54;   // the fire truck
        return n >= 1 && n <= 10 ? 57 + n - 1 : -1;
    }
    return -1;
}

std::string series_name(int series, const std::vector<std::string>& strings, const std::string& language) {
    if (b3native::extra_series(series)) {   // showpages: CUSTOM, CUSTOM 2, ..., BETA, BETA 2, ... (extra_cars.h's strings)
        const b3native::ExtraWords& w = b3native::extra_words(language);
        std::string n = b3native::extra_series_kind(series) == b3native::kCustomSeries ? w.custom : w.beta;
        if (const int page = b3native::extra_series_page(series); page > 0) n += " " + std::to_string(page + 1);
        return n;
    }
    // GetClassString 0x42DA70 [C]: 0..4 0xBEF..0xBF3, 5 0xBF5 (the specials), 6 0xBF4 (the heavyweights); trafficseries'
    // 380 / 381 (traffic_series.h)
    int id = -1;
    if (series >= 0 && series <= 4) id = 0xBEF + series;
    else if (series == kSeriesSpecial) id = 0xBF5;
    else if (series == kSeriesHeavy) id = 0xBF4;
    else if (series == kSeriesTraffic) id = 380;          // (traffic_series.h kTrafficSeriesString)
    else if (series == kSeriesTrafficSpecial) id = 381;   // (kTrafficSpecialString)
    if (id >= 0 && std::size_t(id) < strings.size() && !trim(strings[std::size_t(id)]).empty()) return upper(trim(strings[std::size_t(id)]));
    static const char* const kFallback[] = {"COMPACT SERIES", "MUSCLE SERIES", "COUPE SERIES", "SPORTS SERIES", "SUPER SERIES",
                                            "SPECIAL SERIES", "HEAVYWEIGHTS", "TRAFFIC", "TRAFFIC SPECIAL"};
    if (series >= 0 && series <= kSeriesTrafficSpecial) return kFallback[series];
    if (series == kSeriesCustom) return "CUSTOM";
    if (series == kSeriesBeta) return "BETA";
    return "SERIES " + std::to_string(series);
}

int car_series(const CarEntry& c) {
    if (c.kind == "Traffic") return kSeriesTraffic;
    if (c.kind == "Traffic special") return kSeriesTrafficSpecial;
    if (c.kind == "Custom") return b3native::extra_page_series(b3native::kCustomSeries, c.page);   // (showpages: its page)
    if (c.kind == "Beta") return b3native::extra_page_series(b3native::kBetaSeries, c.page);
    const int i = name_index(c.folder, c.number);
    if (i >= 50 && i <= 56) return kSeriesSpecial;
    for (int k = 0; k < int(std::size(kFolders)); ++k)
        if (c.folder == kFolders[k].name) return k;
    return 0;
}

std::vector<SeriesEntry> group_series(const std::vector<CarEntry>& cars, const std::vector<std::string>& strings, const std::string& language) {
    // the disc's, the traffic series, then CUSTOM's pages and BETA's (extra_series_order's order)
    std::vector<int> order = {0, 1, 2, 3, 4, kSeriesSpecial, kSeriesHeavy, kSeriesTraffic, kSeriesTrafficSpecial};
    for (int kind : {b3native::kCustomSeries, b3native::kBetaSeries})
        for (int page = 0; page <= b3native::kExtraPagesMax; ++page) order.push_back(b3native::extra_page_series(kind, page));
    std::vector<SeriesEntry> out;
    for (int id : order) {
        SeriesEntry e;
        e.id = id;
        for (std::size_t k = 0; k < cars.size(); ++k)
            if (car_series(cars[k]) == id) e.cars.push_back(k);
        if (e.cars.empty()) continue;
        e.name = series_name(id, strings, language);
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<CarEntry> list_cars(const fs::path& game_dir, std::string* note, std::vector<std::string>* strings_out) {
    namespace disc = b3asset::disc;
    std::vector<CarEntry> out;
    if (note) note->clear();
    const fs::path pveh = disc::resolve_ci(game_dir, "PVEH");
    if (pveh.empty()) {
        if (note) *note = "no PVEH folder in the game data";
        return out;
    }
    std::vector<std::string> names;
    std::vector<std::uint8_t> bin;
    const fs::path strings = disc::resolve_ci(game_dir, "DATA/GLOBALUS.BIN");
    std::vector<std::string> all;
    if (!strings.empty() && disc::read_file(strings, bin)) all = read_strings(bin);
    names = car_names(all);
    if (strings_out) *strings_out = all;
    if (names.empty() && note) *note = "the game's car names were not found (DATA/GLOBALUS.BIN): the cars show by class and number";
    for (const Folder& f : kFolders) {
        const fs::path dir = disc::resolve_ci(pveh, f.name);
        if (dir.empty()) continue;
        for (const disc::Entry& e : disc::list_dir(dir)) {
            if (e.dir) continue;
            const std::string u = upper(e.name);
            int n = 0;
            char tail[8] = {};
            if (u.size() < 8 || u.compare(0, 3, "CAR") != 0 || std::sscanf(u.c_str() + 3, "%d.%7s", &n, tail) != 2 || std::strcmp(tail, "BGV") != 0) continue;
            CarEntry c;
            c.folder = f.name;
            c.number = n;
            c.file = e.path;
            c.kind = f.kind;
            const int i = name_index(f.name, n);
            if (i >= 50 && i <= 56) c.kind = "Special";   // the fire truck too (HEVY CAR36)
            c.name = i >= 0 && std::size_t(i) < names.size() ? names[std::size_t(i)] : std::string(f.kind) + " " + std::to_string(n);
            c.label = "PVEH/" + std::string(f.name) + "/CAR" + std::to_string(n) + ".BGV";
            out.push_back(std::move(c));
        }
    }
    // the game's order: by the name's index (files it does not name after those, by folder and number)
    auto key = [](const CarEntry& c) {
        const int i = name_index(c.folder, c.number);
        int fo = 0;
        for (int k = 0; k < int(std::size(kFolders)); ++k) if (c.folder == kFolders[k].name) fo = k;
        return std::make_tuple(i < 0 ? 1 : 0, i < 0 ? fo : i, c.number);
    };
    std::sort(out.begin(), out.end(), [&](const CarEntry& a, const CarEntry& b) { return key(a) < key(b); });
    return out;
}

std::vector<std::string> traffic_names(const fs::path& game_dir) {
    namespace disc = b3asset::disc;
    std::vector<std::string> out;
    std::vector<std::uint8_t> elf, bin;
    const fs::path prog = disc::resolve_ci(game_dir, "SLUS_210.50"), strings = disc::resolve_ci(game_dir, "DATA/GLOBALUS.BIN");
    if (prog.empty() || strings.empty() || !disc::read_file(prog, elf) || !disc::read_file(strings, bin)) return out;
    const std::vector<std::string> table = read_strings(bin);
    auto u32 = [&](std::size_t o) { return std::uint32_t(elf[o] | elf[o + 1] << 8 | elf[o + 2] << 16) | std::uint32_t(elf[o + 3]) << 24; };
    if (elf.size() < 0x34 || std::memcmp(elf.data(), "\x7F" "ELF", 4) != 0) return out;
    const std::uint32_t va = b3native::kTrafficNames, n = 4u * std::uint32_t(b3native::kTrafficVehicles);
    const std::uint32_t phoff = u32(0x1C), phnum = std::uint32_t(elf[0x2C] | elf[0x2D] << 8);
    for (std::uint32_t k = 0; k < phnum && phoff + 32u * (k + 1) <= elf.size(); ++k) {   // the PT_LOAD holding it
        const std::size_t ph = phoff + 32u * k;
        const std::uint32_t off = u32(ph + 4), vaddr = u32(ph + 8), filesz = u32(ph + 16);
        if (u32(ph) != 1 || va < vaddr || std::uint64_t(va) + n > std::uint64_t(vaddr) + filesz || std::size_t(off) + (va - vaddr) + n > elf.size())
            continue;
        for (int i = 0; i < b3native::kTrafficVehicles; ++i) {
            const std::uint32_t id = u32(off + (va - vaddr) + 4u * std::uint32_t(i));
            if (id >= table.size()) return {};
            out.push_back(table[id]);
        }
        break;
    }
    return out;
}

std::vector<CarEntry> list_more_cars(const fs::path& game_dir, const fs::path& custom, const fs::path& beta, std::vector<std::string>* left_out) {
    namespace disc = b3asset::disc;
    std::vector<CarEntry> out;
    const std::vector<std::string> names = traffic_names(game_dir);
    for (int series : {b3native::kTrafficClass, b3native::kTrafficSpecialClass})
        for (int i : b3native::traffic_series_cars(series)) {
            const b3native::TrafficEntry& t = b3native::traffic_table()[std::size_t(i)];
            const std::string rel = std::string("PVEH/") + t.cls + "/" + t.car + ".BTV";
            const fs::path f = disc::resolve_ci(game_dir, rel);
            if (f.empty()) continue;
            CarEntry c;
            c.folder = t.cls;
            c.number = std::atoi(t.car + 3);
            c.file = f;
            c.kind = series == b3native::kTrafficClass ? "Traffic" : "Traffic special";
            c.name = std::size_t(i) < names.size() && !names[std::size_t(i)].empty() ? names[std::size_t(i)] : c.kind + " " + std::to_string(c.number);
            c.label = rel;
            out.push_back(std::move(c));
        }
    const b3native::ExtraScan x = b3native::extra_scan_all(custom, beta);
    for (const b3native::ExtraCar& e : x.cars) {
        CarEntry c;
        c.folder = e.cls();
        c.number = e.slot;
        c.file = b3io::u8path(e.model);
        c.kind = e.series == b3native::kBetaSeries ? "Beta" : "Custom";
        std::string nm = e.name;
        if (nm.empty()) {   // as the game names it: its donor's name with MOD
            const int k = name_index(e.donor_cls, std::atoi(e.donor_car.c_str() + 3));
            std::vector<std::uint8_t> bin;
            const fs::path strings = disc::resolve_ci(game_dir, "DATA/GLOBALUS.BIN");
            std::vector<std::string> cars;
            if (!strings.empty() && disc::read_file(strings, bin)) cars = car_names(read_strings(bin));
            nm = (k >= 0 && std::size_t(k) < cars.size() ? cars[std::size_t(k)] : e.donor_cls + "/" + e.donor_car) + " MOD";
        }
        c.name = nm;
        c.label = "cars/" + (e.series == b3native::kBetaSeries ? std::string() : std::string("custom/")) + e.source;
        c.page = e.page;   // showpages
        out.push_back(std::move(c));
    }
    if (left_out)
        for (const auto& [what, why] : x.refused) left_out->push_back(what + ": " + why);
    return out;
}

void Turntable::drag(float dx, float dy) {
    yaw = std::remainder(yaw + dx * 0.01f, 2.0f * 3.14159265358979f);
    pitch = std::clamp(pitch + dy * 0.006f, kMinPitch, kMaxPitch);
    hold = kHoldSeconds;
}

void Turntable::stick(float x, float y, float dt) {
    auto dead = [](float v) {
        const float a = std::fabs(v);
        return a <= 0.18f ? 0.0f : std::copysign((a - 0.18f) / 0.82f, v);
    };
    const float sx = dead(x), sy = dead(y);
    if (sx == 0 && sy == 0) return;
    yaw += sx * 2.2f * dt;
    pitch = std::clamp(pitch + sy * 1.2f * dt, kMinPitch, kMaxPitch);
    hold = kHoldSeconds;
}

void Turntable::wheel(float steps) { zoom = std::clamp(zoom * std::pow(0.9f, steps), kMinZoom, kMaxZoom); }

void Turntable::step(float dt) {
    if (hold > 0) {
        hold = std::max(0.0f, hold - dt);
        return;
    }
    if (spin) yaw += kSpinRate * dt;
    yaw = std::remainder(yaw, 2.0f * 3.14159265358979f);
}

}  // namespace b3launcher::showroom
