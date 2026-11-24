// traffic_cars.h -- trafficseries (2026-11-22): traffic vehicles driven as race cars, and the TRAFFIC / TRAFFIC SPECIAL
// series on SINGLE EVENT's SELECT CAR. The tables and rules: traffic_series.h.
//
// What the game does [C the decomp; addresses retail]:
// - A race car's model: CB3RaceCar::Prepare 0x152AF0 queues CreateVehiclePathFromIndex(GetVehicleIndexFromID(its id
//   +0x1960)) + ".bgv". Its engine sound: func_00288960 takes the sound models from that file (+0x16DC / +0x16E0) and
//   loads the bank "<path>hwd" / "lwd" (the player's high bank, the rivals' low); CB3SoundESMHigh / Low::Update read them
//   every frame (ApplyLag 0x2FAFB0: a null model is signal 11). Its tuning: each per-vehicle variable registers with
//   CGtCommsDatabase::AddEntry (float 0x21BB60, int 0x21BD80, vector 0x21B9B0; a2 the name, a3 the group, t0 the file
//   "../Export/ValueDB/<VehiclePhysics|Camera|VehicleAudio|VehicleMaterial|AI>/<VEHICLE>.cfg"), which writes the VDB.XML
//   default of CGtHash(name + group + "/" + file). Its picture: func_00371670 copies the front-end texture named after
//   its GtID (none for a traffic vehicle: CGtTexture::CreateCopy of a null texture, signal 11).
// - The race's cars: CB3MenuChoices 0x01E900B0 +0x80 (u64 a car: the player's, then the rivals'), read by
//   RequestOnePlayerGameMode 0x30C2C0. Single Event's Confirm picks the rivals with func_0030B9D0(choices, the player's
//   class) -> func_0030B6E0 (random race cars of that class: none of class 7 / 8, a modulo by zero).
// - SELECT CAR's carousel CB3CarouselComponent 0x01E8EC30 (car_select.h): the series is +0x258, the last one allowed is
//   set by +0x264 / +0x265 (heavy_series.h); func_0042C1A0 "can go right" (the page's Right then calls func_0042BEA0 and
//   plays the move sound; the prompts read it too), func_0042BD70 Left, func_0042CD60 the arrows. Each step rebuilds the
//   rows with func_0042B7C0 from GetCarCount / GetCarID / IsVehicleDriveable / IsCarUnlocked of the series' class, then
//   the page shows the row's car: its name func_003EAE40(func_003EB060(id), 1) (a race car's index; -1: string 0x4FF),
//   its picture GetCarTexture 0x4277C0 (by the GtID's name; none: the game's "!" picture D_01EA46FC), its model
//   (the same ".bgv" path), and the series' name GetClassString 0x42DA70.
// Ours (port-layer wrappers; each one is the game's call unless a traffic vehicle or class 7 / 8 is involved):
// - the disc (platform_disc.h set_alias): a traffic vehicle's CARn.BGV is a model file made of its .BTV and its donor's
//   engine sound models (traffic_series.h traffic_model, cached under the user's data folder), CARn.HWD / .LWD the
//   donor's banks;
// - AddEntry: a racing traffic vehicle's per-vehicle file registers as its donor's (its own suspension geometry kept;
//   VehiclePhysics only within CB3RaceCarPhysicsAttribs::Register 0x252C20, so the same model in the traffic on the road
//   keeps its own: traffic_series.h traffic_config); func_00371670: the donor's picture;
// - SELECT CAR (Single Event, while the series is on): classes 7 / 8 after the last series allowed (the steps, the
//   arrows, "can go right"), their cars (GetCarCount / GetCarID / IsVehicleDriveable), names (the traffic table's
//   strings D_004CDD40, as func_003EACC0 gives them), small pictures (the donor's) and series names (380, 381); the
//   carousel opens on the first
//   series when the remembered car is a traffic vehicle or class 7 / 8 (as the game does with a heavyweight it does not
//   allow); the rivals of a traffic vehicle are race cars of its donor's class;
// - a race with a traffic vehicle as the player's car keeps no records and does not count for the profile (records_guard.h,
//   0x136F30), like heavy_race.h's.
// The TUK-TUKS row (gameopts) and the tests set the race's cars directly: set_race_cars (B3_TRAFFIC_CARS for tests).
#ifndef B3NATIVE_TRAFFIC_CARS_H
#define B3NATIVE_TRAFFIC_CARS_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "b3port/fileio.h"
#include "car_select.h"
#include "hook_table.h"
#include "platform.h"        // vblank_count (the Crashbreaker log)
#include "platform_disc.h"
#include "race_options.h"   // RaceOptions::explicit_rival_pick (RIVALS' CARS)
#include "records_guard.h"
#include "save_store.h"
#include "traffic_series.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace b3native {

class TrafficCars {
public:
    // the series on SELECT CAR (native_play: on unless B3NATIVE_TRAFFIC_SERIES=0; a replay: its entry)
    void set_series_query(std::function<bool()> f) { series_on_ = std::move(f); }
    // The race's cars (gameopts' TUK-TUKS row, tests): the player's and / or every rival's car a traffic vehicle (its
    // table index, traffic_series.h; -1 leaves the game's), written at RequestOnePlayerGameMode (SINGLE EVENT)
    void set_race_cars(int player, int rivals) { force_player_ = player; force_rivals_ = rivals; }
    // "player=COMP/CAR18,rivals=COMP/CAR18" (B3_TRAFFIC_CARS); false when malformed
    bool configure(const std::string& spec);
    bool install(HookTable& hooks);
    // a SINGLE EVENT stage on now with a traffic vehicle as the player's car (records kept apart)
    static bool racing();
    // customseries (extra_cars.h): the installed instance (null: none), and SELECT CAR's last series going right with the
    // traffic series (the carousel's own last when they are not offered now)
    static TrafficCars* active() { return instance(); }
    int series_end();
    struct Counts { std::uint64_t models = 0, configs = 0, pictures = 0, records_skipped = 0, profile_skipped = 0; };
    const Counts& counts() const { return counts_; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static TrafficCars*& instance() { static TrafficCars* p = nullptr; return p; }
    static std::string alias(const std::string& p);
    static void request(const b3host::CallArgs& a, b3host::CallResult& r);
    static void new_mode(const b3host::CallArgs& a, b3host::CallResult& r);
    static void add(const b3host::CallArgs& a, b3host::CallResult& r, Entry next);
    static void add_float(const b3host::CallArgs& a, b3host::CallResult& r) { add(a, r, instance()->add_float_); }
    static void add_int(const b3host::CallArgs& a, b3host::CallResult& r) { add(a, r, instance()->add_int_); }
    static void add_vector(const b3host::CallArgs& a, b3host::CallResult& r) { add(a, r, instance()->add_vector_); }
    static void car_picture(const b3host::CallArgs& a, b3host::CallResult& r);
    static void car_count(const b3host::CallArgs& a, b3host::CallResult& r);
    static void car_id(const b3host::CallArgs& a, b3host::CallResult& r);
    static void car_thumbnail(const b3host::CallArgs& a, b3host::CallResult& r);
    static void race_physics(const b3host::CallArgs& a, b3host::CallResult& r);
    static void row_image(const b3host::CallArgs& a, b3host::CallResult& r);
    static void driveable(const b3host::CallArgs& a, b3host::CallResult& r);
    static void class_string(const b3host::CallArgs& a, b3host::CallResult& r);
    static void can_right(const b3host::CallArgs& a, b3host::CallResult& r);
    static void step_right(const b3host::CallArgs& a, b3host::CallResult& r);
    static void step_left(const b3host::CallArgs& a, b3host::CallResult& r);
    static void arrows(const b3host::CallArgs& a, b3host::CallResult& r);
    static void page_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void carousel_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void show_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void race_index(const b3host::CallArgs& a, b3host::CallResult& r);
    static void car_name(const b3host::CallArgs& a, b3host::CallResult& r);
    static void rivals(const b3host::CallArgs& a, b3host::CallResult& r);
    static void records(const b3host::CallArgs& a, b3host::CallResult& r);
    static void profile_counts(const b3host::CallArgs& a, b3host::CallResult& r);
    static void blast(const b3host::CallArgs& a, b3host::CallResult& r, Entry next);
    static void blast_radius(const b3host::CallArgs& a, b3host::CallResult& r) { blast(a, r, instance()->blast_radius_); }
    static void blast_strength(const b3host::CallArgs& a, b3host::CallResult& r) { blast(a, r, instance()->blast_strength_); }
    static void set_car_id(const b3host::CallArgs& a, b3host::CallResult& r);
    // tsdonor: another feature's cars outside the game's 67 race cars (gameopts' extra cars) get their donor's Crashbreaker
    // and AI series through these wrappers too: `f(id)` gives the donor race car's id, 0 for an id it does not own
    static void set_extra_donors(std::uint64_t (*f)(std::uint64_t)) { extra_donors() = f; }
    static std::uint64_t donor_of(std::uint64_t id) {
        if (const std::uint64_t d = traffic_donor_id(id)) return d;
        return extra_donors() ? extra_donors()(id) : 0;
    }
    static std::uint64_t (*&extra_donors())(std::uint64_t) { static std::uint64_t (*f)(std::uint64_t) = nullptr; return f; }
    bool offered() const;                     // classes 7 / 8 on the carousel now
    int last_class() const;                   // the last series the carousel's bytes allow
    void step(int to, int kind);              // the carousel to series `to` as func_0042BEA0 / 0x42BD70 step (kind 3 / 4)
    std::string model_file(const std::string& btv_path);
    bool makeable(int index);                 // its model file is made (or can be): offered / raced only then
    bool series_available(int c);             // a traffic series with a vehicle that can be made
    int next_class(int c);                    // traffic_next_class / prev over the available series
    int prev_class(int c);
    b3host::gaddr guest_string(const std::string& s);
    void note(const std::string& s);          // once per text

    Entry new_mode_ = nullptr;
    Entry request_ = nullptr, add_float_ = nullptr, add_int_ = nullptr, add_vector_ = nullptr, car_picture_ = nullptr;
    Entry car_count_ = nullptr, car_id_ = nullptr, driveable_ = nullptr, class_string_ = nullptr, can_right_ = nullptr;
    Entry step_right_ = nullptr, step_left_ = nullptr, arrows_ = nullptr, page_prepare_ = nullptr, carousel_prepare_ = nullptr;
    Entry show_prepare_ = nullptr, race_index_ = nullptr, car_name_ = nullptr, rivals_ = nullptr, records_ = nullptr;
    Entry profile_counts_ = nullptr, car_thumbnail_ = nullptr, row_image_ = nullptr;
    Entry blast_radius_ = nullptr, blast_strength_ = nullptr, set_car_id_ = nullptr;   // the donor's Crashbreaker, AI series
    bool in_thumbnail_ = false;
    bool in_race_physics_ = false;            // within CB3RaceCarPhysicsAttribs::Register (a race car's physics)
    Entry race_physics_ = nullptr;               // within GetCarTexture: GetCarID gives the donor's id (its picture)
    std::function<bool()> series_on_;
    int force_player_ = -1, force_rivals_ = -1;
    std::uint64_t race_player_ = 0;           // the player's car of the race the last one-player request made (0: none)
    std::vector<int> racing_;                 // the table indices of the traffic vehicles in the race being prepared
    bool in_page_prepare_ = false, car_select_ = false, in_show_ = false;
    int last_id_index_ = -1;                  // the traffic vehicle GetCarID gave last (-1: a race car)
    int name_index_ = -1;                     // func_003EB060's traffic vehicle, for the name lookup after it
    std::map<std::string, std::string> models_;
    std::map<std::string, b3host::gaddr> strings_;
    std::set<std::string> notes_;
    std::mutex m_;                            // the alias (the loader's threads) and the cache
    Counts counts_;
};

namespace traffic_cars_detail {
constexpr b3host::gaddr kChoices = 0x01E900B0u;          // CB3MenuChoices: +4 top menu, +8 mode, +0x30 player, +0x80 cars
constexpr b3host::gaddr kScene = 0x01E903E0u, kDestructObject = 0x001D7560u;
constexpr b3host::gaddr kRebuildRows = 0x0042B7C0u, kRowObjects = 0x0042DAE0u;
constexpr b3host::gaddr kOnePlayerStage = 0x0051AD00u, kSingleEventTag = 0x0051B920u, kCurrentMode = 0x0051BA88u;
inline std::uint64_t u64_of(const u_long128& q) { return q.w[0] | std::uint64_t(q.w[1]) << 32; }
inline u_long128 reg(std::uint64_t v) { u_long128 q{}; q.w[0] = std::uint32_t(v); q.w[1] = std::uint32_t(v >> 32); return q; }
inline void ret(b3host::CallResult& r, std::uint64_t v) { r.v0 = reg(v); }
inline std::string guest_text(std::uint32_t a, std::size_t max = 160) {
    std::string s;
    const b3host::gaddr g = a & 0x01FFFFFFu;
    for (b3host::gaddr q = g; g && q < g + max; ++q) {
        const char c = char(b3host::load<std::uint8_t>(q));
        if (!c) break;
        s += c;
    }
    return s;
}
inline bool carousel_self(const b3host::CallArgs& a) { return (a.gpr[0].w[0] & 0x01FFFFFFu) == kCarousel; }
inline bool carousel_idle() {   // the all-series mode, no move under way [C the steps' own test]
    return b3host::load<std::int32_t>(kCarousel + 0x25Cu) == 9 && b3host::load<std::int32_t>(kCarousel + 0x238u) == 0;
}
inline int player_car_index() {   // the traffic table's index of the current player's car (-1 a race car)
    const std::int32_t player = b3host::load<std::int32_t>(kChoices + 0x30u);
    return traffic_index(b3host::load<std::uint64_t>(kChoices + 0x80u + 8u * unsigned(player < 0 || player > 7 ? 0 : player)));
}
}  // namespace traffic_cars_detail

inline bool TrafficCars::configure(const std::string& spec) {
    std::size_t at = 0;
    while (at < spec.size()) {
        std::size_t end = spec.find(',', at);
        if (end == std::string::npos) end = spec.size();
        const std::string item = spec.substr(at, end - at);
        at = end + 1;
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) return false;
        const std::string v = traffic_upper_path(item.substr(eq + 1));
        const std::size_t sl = v.find('/');
        if (sl == std::string::npos) return false;
        const int i = traffic_index(gtid_compress(v.substr(0, sl) + v.substr(sl + 1)));
        if (i < 0) return false;
        if (item.compare(0, eq, "player") == 0) force_player_ = i;
        else if (item.compare(0, eq, "rivals") == 0) force_rivals_ = i;
        else return false;
    }
    return true;
}

inline bool TrafficCars::racing() {
    using namespace traffic_cars_detail;
    if ((b3host::load<std::uint32_t>(kCurrentMode) & 0x01FFFFFFu) != kOnePlayerStage) return false;
    if ((b3host::load<std::uint32_t>(kOnePlayerStage + 0x1B8u) & 0x01FFFFFFu) != kSingleEventTag) return false;
    if (b3host::load<std::int32_t>(kChoices + 4u) != 1) return false;
    // the race's own player car, as the request made it (a set car, TUK-TUKS', is not left in the choices: SELECT CAR
    // keeps the player's own)
    const TrafficCars* p = instance();
    return p && traffic_index(p->race_player_) >= 0;
}

inline bool TrafficCars::install(HookTable& hooks) {
    const std::vector<Hook> set = {
        {0x0030D1B0u, HookLayer::kPort, &new_mode, &new_mode_},
        {0x0030C2C0u, HookLayer::kPort, &request, &request_},
        {0x0021BB60u, HookLayer::kPort, &add_float, &add_float_},
        {0x0021BD80u, HookLayer::kPort, &add_int, &add_int_},
        {0x0021B9B0u, HookLayer::kPort, &add_vector, &add_vector_},
        {0x00371670u, HookLayer::kPort, &car_picture, &car_picture_},
        {0x00252C20u, HookLayer::kPort, &race_physics, &race_physics_},
        {0x00251B90u, HookLayer::kPort, &car_count, &car_count_},
        {0x00251C10u, HookLayer::kPort, &car_id, &car_id_},
        {0x004277C0u, HookLayer::kPort, &car_thumbnail, &car_thumbnail_},
        {0x00427080u, HookLayer::kPort, &row_image, &row_image_},
        {0x00251EF0u, HookLayer::kPort, &driveable, &driveable_},
        {0x0042DA70u, HookLayer::kPort, &class_string, &class_string_},
        {0x0042C1A0u, HookLayer::kPort, &can_right, &can_right_},
        {0x0042BEA0u, HookLayer::kPort, &step_right, &step_right_},
        {0x0042BD70u, HookLayer::kPort, &step_left, &step_left_},
        {0x0042CD60u, HookLayer::kPort, &arrows, &arrows_},
        {0x00318F50u, HookLayer::kPort, &page_prepare, &page_prepare_},
        {0x0042CFA0u, HookLayer::kPort, &carousel_prepare, &carousel_prepare_},
        {0x0042D2B0u, HookLayer::kPort, &show_prepare, &show_prepare_},
        {0x003EB060u, HookLayer::kPort, &race_index, &race_index_},
        {0x003EAE40u, HookLayer::kPort, &car_name, &car_name_},
        {0x0030B9D0u, HookLayer::kPort, &rivals, &rivals_},
        {kRecordsUpdate, HookLayer::kPort, &records, &records_},
        {0x00136F30u, HookLayer::kPort, &profile_counts, &profile_counts_},
        {0x003EAD60u, HookLayer::kPort, &blast_radius, &blast_radius_},
        {0x003EADD0u, HookLayer::kPort, &blast_strength, &blast_strength_},
        {0x00151100u, HookLayer::kPort, &set_car_id, &set_car_id_},
    };
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "traffic_cars: %08X is not native: no traffic vehicles as cars\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:traffic_cars", set, &error)) {
        std::fprintf(stderr, "traffic_cars: %s: no traffic vehicles as cars\n", error.c_str());
        return false;
    }
    instance() = this;
    platform::disc::set_alias(&alias);
    std::printf("traffic_cars: TRAFFIC (%zu) and TRAFFIC SPECIAL (%zu) on SELECT CAR in SINGLE EVENT (B3NATIVE_TRAFFIC_SERIES=0: "
                "retail's list)%s\n", traffic_series_cars(kTrafficClass).size(), traffic_series_cars(kTrafficSpecialClass).size(),
                force_player_ >= 0 || force_rivals_ >= 0 ? "; the race's cars set (player / rivals)" : "");
    std::fflush(stdout);
    return true;
}

inline void TrafficCars::note(const std::string& s) {
    if (!notes_.insert(s).second) return;
    std::printf("traffic_cars: %s\n", s.c_str());
    std::fflush(stdout);
}

// ---- the disc: a traffic vehicle's race car files
inline std::string TrafficCars::alias(const std::string& p) {
    static thread_local bool asking = false;   // (the disc's own answer for the same name, below)
    TrafficCars* t = instance();
    if (!t || asking) return {};
    std::string a = traffic_alias(p);
    if (a.empty()) return {};
    asking = true;
    const std::string real = platform::disc::resolve(p);   // a file of that name (the disc's, a mod's) is never shadowed
    asking = false;
    if (!real.empty()) return {};
    std::lock_guard<std::mutex> lk(t->m_);
    if (a.size() > 4 && a.compare(a.size() - 4, 4, ".BTV") == 0) {
        a = t->model_file(a);
        if (a.empty()) return {};
    }
    t->note("the disc's " + traffic_upper_path(p) + ": " + (a.compare(0, 5, "host:") == 0 ? a.substr(5) : a));
    return a;
}

inline std::string TrafficCars::model_file(const std::string& btv_path) {
    if (const auto it = models_.find(btv_path); it != models_.end()) return it->second;   // (a failure: until the next start)
    std::string out;
    for (const TrafficEntry& t : traffic_table()) {
        if (btv_path != std::string("PVEH/") + t.cls + "/" + t.car + ".BTV") continue;
        // (discread, pre4: resolve gives a path inside a disc image when the game data is an .iso / .bin / .chd -- the
        // owner's -- which a plain file read cannot open: every traffic vehicle was "not driveable"; the disc's reader
        // reads both kinds, as the game's own file reads do)
        auto read = [](const std::string& host, std::vector<std::uint8_t>& b) {
            return b3asset::disc::read_file(b3io::u8path(host), b) && !b.empty();
        };
        // the two files as the disc serves them (a mod's included: platform_disc.h resolve)
        std::vector<std::uint8_t> btv, bgv;
        const std::string donor = std::string("PVEH/") + t.donor_cls + "/" + t.donor_car + ".BGV";
        const std::string hb = platform::disc::resolve(btv_path), hd = platform::disc::resolve(donor);
        if (hb.empty() || hd.empty() || !read(hb, btv) || !read(hd, bgv)) {
            std::printf("traffic_cars: %s: its .BTV or %s's .BGV cannot be read: not driveable\n", t.name().c_str(), t.donor_name().c_str());
            break;
        }
        const std::vector<std::uint8_t> m = traffic_model(btv, bgv);
        if (m.empty()) {
            std::printf("traffic_cars: %s: its model reaches past %s's engine sound data: not driveable\n", t.name().c_str(), t.donor_name().c_str());
            break;
        }
        std::filesystem::path dir;
        if (const char* e = std::getenv("B3_TRAFFIC_CACHE"); e && *e) dir = b3io::u8path(e);
        else if (const std::filesystem::path u = save::user_data_dir(); !u.empty()) dir = u / "cache" / "traffic";   // (portable mode: b3io::user_dir)
        if (dir.empty()) {
            std::printf("traffic_cars: %s: no data folder for its model file: not driveable\n", t.name().c_str());
            break;
        }
        // named by the content of what it is made of (FNV-1a 64 of both files), so a mod's file never meets a stale one
        std::uint64_t h = 0xCBF29CE484222325ull;
        for (const std::vector<std::uint8_t>* b : {&btv, &bgv})
            for (std::uint8_t c : *b) h = (h ^ c) * 0x100000001B3ull;
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
        const std::filesystem::path f = dir / (t.name() + "_" + t.donor_name() + "_" + hex + ".bgv");
        std::error_code ec;
        if (std::filesystem::file_size(f, ec) != m.size() || ec) {   // made once (the game-derived file stays on this PC)
            std::filesystem::create_directories(dir, ec);
            const std::filesystem::path tmp = f.string() + ".tmp";
            {
                std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
                o.write(reinterpret_cast<const char*>(m.data()), std::streamsize(m.size()));
                o.flush();
                if (!o) {
                    std::printf("traffic_cars: %s: its model file cannot be written (%s): not driveable\n", t.name().c_str(), b3io::u8string(dir).c_str());
                    std::filesystem::remove(tmp, ec);
                    break;
                }
            }
            std::filesystem::rename(tmp, f, ec);   // (whole or not at all)
            if (ec) {
                std::printf("traffic_cars: %s: its model file cannot be put in place (%s): not driveable\n", t.name().c_str(), ec.message().c_str());
                std::filesystem::remove(tmp, ec);
                break;
            }
        }
        ++counts_.models;
        out = "host:" + b3io::u8string(f);
        break;
    }
    models_[btv_path] = out;
    return out;
}

inline bool TrafficCars::series_available(int c) {
    for (int i : traffic_series_cars(c))
        if (makeable(i)) return true;
    return false;
}
inline int TrafficCars::next_class(int c) {
    int n = traffic_next_class(c, last_class());
    while (n >= 0 && !series_available(n)) n = traffic_next_class(n, last_class());
    return n;
}
inline int TrafficCars::prev_class(int c) {
    int n = traffic_prev_class(c, last_class());
    while (n >= 0 && traffic_class(n) && !series_available(n)) n = traffic_prev_class(n, last_class());
    return n;
}

inline bool TrafficCars::makeable(int index) {
    if (index < 0 || index >= int(traffic_table().size())) return false;
    const TrafficEntry& t = traffic_table()[std::size_t(index)];
    std::lock_guard<std::mutex> lk(m_);
    return !model_file(std::string("PVEH/") + t.cls + "/" + t.car + ".BTV").empty();
}

inline b3host::gaddr TrafficCars::guest_string(const std::string& s) {
    if (const auto it = strings_.find(s); it != strings_.end()) return it->second;
    const b3host::gaddr a = guest_arena().alloc("port:traffic_cars", std::uint32_t(s.size() + 1), 4);
    if (!a) return 0;
    for (std::size_t i = 0; i < s.size(); ++i) b3host::store<std::uint8_t>(a + std::uint32_t(i), std::uint8_t(s[i]));
    b3host::store<std::uint8_t>(a + std::uint32_t(s.size()), 0);
    strings_[s] = a;
    return a;
}

// ---- the race
// CB3MenuChoices::RequestNewGameMode 0x30D1B0 (every mode's request: World Tour, one player, two players, crash party,
// online): no traffic vehicle races unless the one-player request below says so, so no other mode's registrations are
// ever redirected
inline void TrafficCars::new_mode(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    p->racing_.clear();
    p->race_player_ = 0;
    p->new_mode_(a, r);
}
// RequestOnePlayerGameMode (Single Event): the set cars (TUK-TUKS, tests; only when their model files can be made), then
// the race's traffic vehicles (their tuning registers as their donors')
inline void TrafficCars::request(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    p->racing_.clear();
    p->race_player_ = 0;
    if (b3host::load<std::int32_t>(kChoices + 4u) != 1) {
        p->request_(a, r);
        return;
    }
    // a set car (TUK-TUKS, tests) for this race only: the player's own choice is put back after the request, so SELECT
    // CAR still has it (Fable's review)
    const std::uint64_t own = b3host::load<std::uint64_t>(kChoices + 0x80u);
    const bool set_player = p->force_player_ >= 0 && p->makeable(p->force_player_);
    if (set_player) b3host::store<std::uint64_t>(kChoices + 0x80u, traffic_table()[std::size_t(p->force_player_)].id());
    if (p->force_rivals_ >= 0 && p->makeable(p->force_rivals_))
        for (unsigned i = 1; i < 8; ++i)
            b3host::store<std::uint64_t>(kChoices + 0x80u + 8u * i, traffic_table()[std::size_t(p->force_rivals_)].id());
    p->request_(a, r);   // (RIVALS' CARS writes its picks in here, before the game copies them: read the cars after it)
    p->race_player_ = b3host::load<std::uint64_t>(kChoices + 0x80u);
    for (unsigned i = 0; i < 8; ++i)
        if (const int k = traffic_index(b3host::load<std::uint64_t>(kChoices + 0x80u + 8u * i)); k >= 0) {
            if (std::find(p->racing_.begin(), p->racing_.end(), k) == p->racing_.end()) p->racing_.push_back(k);
            std::printf("traffic_cars: car %u of the race: %s (%s's engine sound and tuning)\n", i, traffic_table()[std::size_t(k)].name().c_str(),
                        traffic_table()[std::size_t(k)].donor_name().c_str());
        }
    if (set_player && own != p->race_player_) {
        b3host::store<std::uint64_t>(kChoices + 0x80u, own);
        std::printf("traffic_cars: the player's own car (%016llx) kept for SELECT CAR\n", static_cast<unsigned long long>(own));
    }
    std::fflush(stdout);
}

// AddEntry: a racing traffic vehicle's per-vehicle file registers as its donor's
inline void TrafficCars::add(const b3host::CallArgs& a, b3host::CallResult& r, Entry next) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    if (p->racing_.empty()) {
        next(a, r);
        return;
    }
    const std::string file = guest_text(a.gpr[4].w[0]);
    const std::string to = traffic_config(p->racing_, file, guest_text(a.gpr[2].w[0]), guest_text(a.gpr[3].w[0]), p->in_race_physics_);
    const b3host::gaddr s = to.empty() ? 0 : p->guest_string(to);
    if (!s) {
        next(a, r);
        return;
    }
    ++p->counts_.configs;
    p->note(file + " registers as " + to);
    if (std::getenv("B3_TRAFFIC_DEBUG"))
        std::printf("traffic_cars: %s %s %s at %08X\n", file.c_str(), guest_text(a.gpr[3].w[0]).c_str(), guest_text(a.gpr[2].w[0]).c_str(), a.gpr[1].w[0]);
    b3host::CallArgs b = a;
    b.gpr[4] = reg(s);
    next(b, r);
}

// CB3RaceCarPhysicsAttribs::Register 0x252C20 (a race car's physics variables): the scope of the VehiclePhysics redirect
inline void TrafficCars::race_physics(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    p->in_race_physics_ = true;
    p->race_physics_(a, r);
    p->in_race_physics_ = false;
}

// func_00371670(the race's pictures, the slot, the car's id, its colour): a traffic vehicle's is its donor's
inline void TrafficCars::car_picture(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    const int k = traffic_index(u64_of(a.gpr[2]));
    if (k < 0) {
        p->car_picture_(a, r);
        return;
    }
    ++p->counts_.pictures;
    b3host::CallArgs b = a;
    b.gpr[2] = reg(traffic_table()[std::size_t(k)].donor_id());
    p->car_picture_(b, r);
}

// func_003EAD60 / func_003EADD0(the car's id): its Crashbreaker radius / strength (CB3PhysicsManager::DoCrashBreaker
// func_001B2BA0), from the 67 race cars' table: 0.0 for any other id (a traffic vehicle's blast would do nothing). A
// traffic vehicle's (or an extra car's: set_extra_donors) are its donor's
inline void TrafficCars::blast(const b3host::CallArgs& a, b3host::CallResult& r, Entry next) {
    using namespace traffic_cars_detail;
    const std::uint64_t id = u64_of(a.gpr[0]), donor = donor_of(id);
    if (!donor) {
        next(a, r);
        return;
    }
    b3host::CallArgs b = a;
    b.gpr[0] = reg(donor);
    next(b, r);
    float v = 0;
    std::memcpy(&v, &r.f0, sizeof v);
    std::printf("traffic_cars: VBlank %llu: Crashbreaker %s of %016llx: its donor %016llx's, %.1f\n",
                static_cast<unsigned long long>(platform::vblank_count()), next == instance()->blast_radius_ ? "radius" : "strength",
                static_cast<unsigned long long>(id), static_cast<unsigned long long>(donor), double(v));
    std::fflush(stdout);
}

// CB3RaceCar::SetCarID 0x151100(car, id): car+0x1970 is the id's race series (the AI's catch-up row, B3AIControlData
// 0x143F50), 0 (Compact) for an id not in the race cars' table. A traffic vehicle's (an extra car's) is its donor's: the
// donor's id first (for the series), then its own
inline void TrafficCars::set_car_id(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    const std::uint64_t donor = donor_of(u64_of(a.gpr[1]));
    if (!donor) {
        p->set_car_id_(a, r);
        return;
    }
    b3host::CallArgs b = a;
    b.gpr[1] = reg(donor);
    p->set_car_id_(b, r);
    const b3host::gaddr car = a.gpr[0].w[0] & 0x01FFFFFFu;
    const std::int32_t series = b3host::load<std::int32_t>(car + 0x1970u);
    p->set_car_id_(a, r);
    b3host::store<std::int32_t>(car + 0x1970u, series);
}

// func_0030B9D0(choices, the player's class): the rivals of a traffic vehicle are race cars of its donor's class. SELECT
// OPTIONS' RIVALS' CARS picks with its own class (RaceOptions::explicit_rival_pick): that one is the race's, untouched
// ("Same as yours" copies the player's car: a traffic vehicle's rivals are then that vehicle, which races as theirs)
inline void TrafficCars::rivals(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    if (RaceOptions::explicit_rival_pick()) {
        p->rivals_(a, r);
        return;
    }
    const int k = player_car_index();
    if (k < 0 && !traffic_class(int(a.gpr[1].w[0]))) {
        p->rivals_(a, r);
        return;
    }
    b3host::CallArgs b = a;
    const int cls = k >= 0 ? traffic_class_of(traffic_table()[std::size_t(k)].donor_cls) : 0;
    b.gpr[1] = reg(std::uint32_t(cls));
    p->note("the rivals of a traffic vehicle: race cars of class " + std::to_string(cls));
    p->rivals_(b, r);
}

// CB3Records::Update 0x3D1090: none with a traffic vehicle as the player's car
inline void TrafficCars::records(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    if (!racing()) {
        p->records_(a, r);
        return;
    }
    const std::uint64_t kinds = records_without_kinds(p->records_, a, r);
    ++p->counts_.records_skipped;
    std::printf("traffic_cars: a traffic vehicle's race: no records kept (the kinds %llx not written)\n", static_cast<unsigned long long>(kinds));
    std::fflush(stdout);
}
// 0x136F30 (whether the race counts for the profile): not with a traffic vehicle
inline void TrafficCars::profile_counts(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    p->profile_counts_(a, r);
    if ((r.v0.w[0] & 0xFFu) == 0 || !racing()) return;
    if (p->counts_.profile_skipped++ == 0) {
        std::printf("traffic_cars: a traffic vehicle's race: the profile's totals and unlocks left as they were\n");
        std::fflush(stdout);
    }
    r.v0 = u_long128{};
}

// ---- SELECT CAR
inline bool TrafficCars::offered() const {
    using namespace traffic_cars_detail;
    return car_select_ && traffic_series_event(series_on_ && series_on_(), b3host::load<std::int32_t>(kChoices + 4u));
}
inline int TrafficCars::last_class() const {
    return carousel_last_class(b3host::load<std::uint8_t>(kCarousel + 0x265u) != 0, b3host::load<std::uint8_t>(kCarousel + 0x264u) != 0);
}
// the carousel to `to` as the game's own steps do [C func_0042BEA0 / func_0042BD70]: the series and row, the rows
// rebuilt, the shown cars' objects released, the row objects made, the move (3 right, 4 left)
inline void carousel_step(int to, int kind) {   // (customseries: extra_cars.h steps with it too)
    using namespace traffic_cars_detail;
    const b3host::gaddr c = kCarousel;
    b3host::store<std::int32_t>(c + 0x254u, b3host::load<std::int32_t>(c + 0x258u));
    b3host::store<std::int32_t>(c + 0x258u, to);
    b3host::store<std::int32_t>(c + 0x240u, b3host::load<std::int32_t>(c + 0x244u));
    b3host::store<std::int32_t>(c + 0x244u, 0);
    b3host::call_guest<void>(kRebuildRows, c);
    const std::int32_t level = std::int32_t(b3host::load<std::int16_t>(c + 8u)) + 1;
    for (unsigned i = 0; i < 4; ++i)
        for (b3host::gaddr at : {c + 0x1ECu + 4u * i, c + 0x1FCu + 4u * i}) {
            const std::uint32_t o = b3host::load<std::uint32_t>(at);
            if (!o) continue;
            b3host::call_guest<void>(kDestructObject, kScene, level, o);
            b3host::store<std::uint32_t>(at, 0);
        }
    b3host::call_guest<void>(kRowObjects, c);
    b3host::store<std::int32_t>(c + 0x238u, kind);
    b3host::store<std::uint32_t>(c + 0x23Cu, 0);
}
inline void TrafficCars::step(int to, int kind) { carousel_step(to, kind); }
inline int TrafficCars::series_end() {
    int c = last_class();
    if (!offered()) return c;
    for (int n = next_class(c); n >= 0; n = next_class(n)) c = n;
    return c;
}

// func_00318F50 (SELECT CAR's Prepare): the carousel it prepares is SELECT CAR's (not YOUR GARAGE's)
inline void TrafficCars::page_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    p->in_page_prepare_ = true;
    p->page_prepare_(a, r);
    p->in_page_prepare_ = false;
}
// func_0042CFA0 (the carousel's Prepare: a1 mode, a2 car row, a3 series, t0 row): SELECT CAR's from SELECT CAR's
// Prepare; a remembered traffic vehicle (or series 7 / 8) opens on the first series, as the game does with a heavyweight
inline void TrafficCars::carousel_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    p->car_select_ = p->in_page_prepare_;
    if (a.gpr[1].w[0] == 9u && std::int32_t(a.gpr[2].w[0]) == -1 && (traffic_class(int(a.gpr[3].w[0])) || player_car_index() >= 0)) {
        b3host::CallArgs b = a;
        b.gpr[3] = u_long128{};
        b.gpr[4] = u_long128{};
        p->note("SELECT CAR opens on the first series (a traffic vehicle remembered)");
        p->carousel_prepare_(b, r);
        return;
    }
    p->carousel_prepare_(a, r);
}
// GetCarCount(list, class): classes 7 / 8 are the traffic series (their own classes otherwise: none / the trams)
inline void TrafficCars::car_count(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    const int c = int(a.gpr[1].w[0]);
    if (!traffic_class(c) || !p->offered()) {
        p->car_count_(a, r);
        return;
    }
    traffic_cars_detail::ret(r, traffic_series_cars(c).size());
}
// GetCarID(list, class, i): the series' i-th traffic vehicle (remembered for IsVehicleDriveable)
inline void TrafficCars::car_id(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    const int c = int(a.gpr[1].w[0]);
    if (!traffic_class(c) || !p->offered()) {
        p->last_id_index_ = -1;
        p->car_id_(a, r);
        return;
    }
    const std::vector<int> cars = traffic_series_cars(c);
    const std::int32_t i = std::int32_t(a.gpr[2].w[0]);
    if (i < 0 || std::size_t(i) >= cars.size()) {
        p->last_id_index_ = -1;
        traffic_cars_detail::ret(r, 0);
        return;
    }
    p->last_id_index_ = cars[std::size_t(i)];
    const TrafficEntry& t = traffic_table()[std::size_t(p->last_id_index_)];
    traffic_cars_detail::ret(r, p->in_thumbnail_ ? t.donor_id() : t.id());
}
// GetCarTexture(class, i) and func_00427080's own copy of it (the carousel's small picture, by the car's GtID name; none: the game's "!" picture): a traffic
// vehicle shows its donor's, a race car of the same kind (a bus the City Bus, a big rig the Tractor Cab, a van the
// B-Team Van): the front end has no picture of a traffic vehicle
inline void TrafficCars::car_thumbnail(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    if (!traffic_class(int(a.gpr[0].w[0])) || !p->offered()) {
        p->car_thumbnail_(a, r);
        return;
    }
    p->in_thumbnail_ = true;
    p->car_thumbnail_(a, r);
    p->in_thumbnail_ = false;
}
// func_00427080 (a row's picture object: its Prepare looks the picture up as GetCarTexture does, inline; t1 the class)
inline void TrafficCars::row_image(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    if (!traffic_class(int(a.gpr[5].w[0])) || !p->offered()) {
        p->row_image_(a, r);
        return;
    }
    p->in_thumbnail_ = true;
    p->row_image_(a, r);
    p->in_thumbnail_ = false;
}
// IsVehicleDriveable(list, id): a traffic vehicle GetCarID just gave for a traffic series
inline void TrafficCars::driveable(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    const int k = traffic_index(traffic_cars_detail::u64_of(a.gpr[1]));
    if (k < 0 || k != p->last_id_index_ || !p->offered()) {
        p->driveable_(a, r);
        return;
    }
    traffic_cars_detail::ret(r, p->makeable(k) ? 1 : 0);   // (a model file that cannot be made: not offered, logged)
}
// GetClassString(class): the traffic series' names (the game's strings 380 / 381)
inline void TrafficCars::class_string(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    const int c = int(a.gpr[0].w[0]);
    if (!traffic_class(c) || !p->offered()) {
        p->class_string_(a, r);
        return;
    }
    traffic_cars_detail::ret(r, std::uint32_t(c == kTrafficClass ? kTrafficSeriesString : kTrafficSpecialString));
}
// func_0042C1A0 (can go right): also at the last series allowed (to TRAFFIC) and at TRAFFIC (to TRAFFIC SPECIAL)
inline void TrafficCars::can_right(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    if (carousel_self(a) && p->offered() && carousel_idle() &&
        p->next_class(b3host::load<std::int32_t>(kCarousel + 0x258u)) >= 0) {
        ret(r, 1);
        return;
    }
    p->can_right_(a, r);
}
// func_0042BEA0 (Right): the last series allowed -> TRAFFIC -> TRAFFIC SPECIAL
inline void TrafficCars::step_right(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    const int to = carousel_self(a) && p->offered() && carousel_idle()
                       ? p->next_class(b3host::load<std::int32_t>(kCarousel + 0x258u)) : -1;
    if (to < 0) {
        p->step_right_(a, r);
        return;
    }
    p->step(to, 3);
    p->note(std::string("SELECT CAR: ") + (to == kTrafficClass ? "TRAFFIC" : "TRAFFIC SPECIAL") + " (series " + std::to_string(to) + ")");
}
// func_0042BD70 (Left): TRAFFIC SPECIAL -> TRAFFIC -> the last series allowed
inline void TrafficCars::step_left(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    const int to = carousel_self(a) && p->offered() && carousel_idle()
                       ? p->prev_class(b3host::load<std::int32_t>(kCarousel + 0x258u)) : -1;
    if (to < 0) {
        p->step_left_(a, r);
        return;
    }
    p->step(to, 4);
}
// func_0042CD60 (the arrows): the right one lit where Right goes on
inline void TrafficCars::arrows(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace traffic_cars_detail;
    TrafficCars* p = instance();
    p->arrows_(a, r);
    if (!carousel_self(a) || !p->offered() || !carousel_idle() || b3host::load<std::int32_t>(kCarousel + 0x230u) == 1) return;
    if (p->next_class(b3host::load<std::int32_t>(kCarousel + 0x258u)) < 0) return;
    const std::uint32_t o = b3host::load<std::uint32_t>(kCarousel + 0x22Cu) & 0x01FFFFFFu;
    if (o) b3host::store<float>(o + 0x10u + 0xCu, 1.0f);   // (the game's "on": white, alpha 1)
}
// func_0042D2B0 (the carousel's show: the row's car, its name): a traffic vehicle's name from the traffic table
inline void TrafficCars::show_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    p->in_show_ = true;
    p->name_index_ = -1;
    p->show_prepare_(a, r);
    p->in_show_ = false;
}
// func_003EB060(id) (a race car's index; -1 for a traffic vehicle, as the game): a traffic vehicle is remembered for the
// name lookup the carousel makes with that -1 next (func_003EAE40(func_003EB060(id), kind) in its show and its row steps)
inline void TrafficCars::race_index(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    p->race_index_(a, r);
    p->name_index_ = std::int32_t(r.v0.w[0]) == -1 ? traffic_index(traffic_cars_detail::u64_of(a.gpr[0])) : -1;
}
// func_003EAE40(index, kind) (a race car's name): -1 right after a traffic vehicle's func_003EB060 -> its name in the
// traffic table [C func_003EACC0: kind 1 D_004CDD40[index]; 0 D_004CDE80]
inline void TrafficCars::car_name(const b3host::CallArgs& a, b3host::CallResult& r) {
    TrafficCars* p = instance();
    const int k = p->name_index_;
    p->name_index_ = -1;
    if (k < 0 || std::int32_t(a.gpr[0].w[0]) != -1) {
        p->car_name_(a, r);
        return;
    }
    const b3host::gaddr table = a.gpr[1].w[0] != 0 ? kTrafficNames : 0x004CDE80u;
    traffic_cars_detail::ret(r, b3host::load<std::uint32_t>(table + 4u * unsigned(k)));
}

}  // namespace b3native

#endif
