// extra_cars.h -- customseries (2026-11-22): players' car mods as extra cars, the CUSTOM series on SINGLE EVENT's SELECT
// CAR (after the traffic series). The rules and tables: extra_series.h; the SELECT CAR mechanics this follows: traffic_cars.h.
//
// Ours (port-layer wrappers, installed only when the folder has a usable car; each one is the game's call otherwise):
// - the vehicle list: one entry per extra car appended once the game has loaded PVEH/VLIST.BIN (CB3Game +0x2C660 set):
//   its GtID (XCUS/CARn), driveable, its donor's boost bar size;
// - GetClassFromID 0x251CB0: an extra car's class is its series (10); GetCarCount / GetCarID: series 10 lists the extra
//   cars (offered: SINGLE EVENT's SELECT CAR, as traffic_cars.h), class 9 none (as the disc's list has);
// - SELECT CAR's steps, arrows and "can go right": CUSTOM after the last series to its left (the traffic series' last, or
//   the carousel's own last); its name and the cars' names: string ids the game's lookup func_00271A00 never has
//   (kExtraStringBase), answered here; the small pictures and the race's pictures: the donor's;
// - the disc (platform_disc.h set_alias): PVEH/XCUS/CARn.BGV the car's file (with its donor's engine sound models added
//   when it has none, cached under the user's data folder), .HWD / .LWD its own banks or its donor's;
// - AddEntry: a racing extra car's per-vehicle files register as its donor's (its handling); the rivals of an extra car
//   are race cars of its donor's class (RIVALS' CARS' own pick untouched: "Same as yours" gives them the extra car);
// - a race with an extra car as the player's car keeps no records and does not count for the profile; any request that
//   is not SINGLE EVENT's has an extra car of a choice slot replaced by its donor (they race in SINGLE EVENT only).
// B3_EXTRA_CARS ("player=XCUS/CAR1,rivals=XCUS/CAR1"): the race's cars set (tests).
#ifndef B3NATIVE_EXTRA_CARS_H
#define B3NATIVE_EXTRA_CARS_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "b3port/fileio.h"
#include "car_select.h"
#include "extra_series.h"
#include "hook_table.h"
#include "platform_disc.h"
#include "race_options.h"
#include "records_guard.h"
#include "save_store.h"
#include "traffic_cars.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

class ExtraCars {
public:
    // The cars of the two folders (custom_dir, beta_dir: extra_series.h extra_scan_all); each refused file is logged
    void scan(const std::filesystem::path& custom, const std::filesystem::path& beta);
    static std::filesystem::path custom_dir();   // B3_CUSTOM_CARS, else <user data>/cars/custom
    static std::filesystem::path beta_dir();     // B3_BETA_CARS, else <user data>/cars/beta (the launcher's import)
    // tsdonor (TrafficCars::set_extra_donors): an extra car's GtID -> its donor's (0: not an extra car), so the
    // Crashbreaker's radius / strength and the AI catch-up series are the donor's
    static std::uint64_t donor_of(std::uint64_t id);
    // paint3 (carousel_models.h chain_carousel_resolver): the model of SELECT CAR's row `i` of series `c` while offered
    // ("XCUSCAR1"), "" otherwise
    std::string thumb_model(int c, int i) const;
    const std::vector<ExtraCar>& cars() const { return cars_; }
    // "XCUS/CAR1=<hash16>;..." (the replay's entry): which file each slot is
    std::string entry() const;
    void set_series_query(std::function<bool()> f) { series_on_ = std::move(f); }
    bool configure(const std::string& spec);   // B3_EXTRA_CARS (tests): player=, rivals=, seed=
    bool install(HookTable& hooks);
    void tick();                               // once a VBlank: whether the game has its vehicle list yet
    static bool racing();                      // a SINGLE EVENT stage on now with an extra car as the player's car
    // Fable's review: each race request's extra cars, hashed from their files' bytes as the race takes them (not the
    // scan's cache): "XCUS/CAR43=<fnv>;..." ("" none). native_play records it with the replay and compares on playback.
    unsigned race_serial() const { return race_serial_; }
    const std::string& race_entry() const { return race_entry_; }
    struct Counts { std::uint64_t listed = 0, configs = 0, pictures = 0, names = 0, records_skipped = 0, profile_skipped = 0, demoted = 0; };
    const Counts& counts() const { return counts_; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static ExtraCars*& instance() { static ExtraCars* p = nullptr; return p; }
    static std::string alias(const std::string& p);
    static void class_of(const b3host::CallArgs& a, b3host::CallResult& r);
    static void new_mode(const b3host::CallArgs& a, b3host::CallResult& r);
    static void request(const b3host::CallArgs& a, b3host::CallResult& r);
    static void add(const b3host::CallArgs& a, b3host::CallResult& r, Entry next);
    static void add_float(const b3host::CallArgs& a, b3host::CallResult& r) { add(a, r, instance()->add_float_); }
    static void add_int(const b3host::CallArgs& a, b3host::CallResult& r) { add(a, r, instance()->add_int_); }
    static void add_vector(const b3host::CallArgs& a, b3host::CallResult& r) { add(a, r, instance()->add_vector_); }
    static void car_picture(const b3host::CallArgs& a, b3host::CallResult& r);
    static void car_count(const b3host::CallArgs& a, b3host::CallResult& r);
    static void car_id(const b3host::CallArgs& a, b3host::CallResult& r);
    static void car_thumbnail(const b3host::CallArgs& a, b3host::CallResult& r);
    static void row_image(const b3host::CallArgs& a, b3host::CallResult& r);
    static void class_string(const b3host::CallArgs& a, b3host::CallResult& r);
    static void can_right(const b3host::CallArgs& a, b3host::CallResult& r);
    static void step_right(const b3host::CallArgs& a, b3host::CallResult& r);
    static void step_left(const b3host::CallArgs& a, b3host::CallResult& r);
    static void arrows(const b3host::CallArgs& a, b3host::CallResult& r);
    static void page_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void carousel_prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void race_index(const b3host::CallArgs& a, b3host::CallResult& r);
    static void car_name(const b3host::CallArgs& a, b3host::CallResult& r);
    static void lookup(const b3host::CallArgs& a, b3host::CallResult& r);
    static void rivals(const b3host::CallArgs& a, b3host::CallResult& r);
    static void records(const b3host::CallArgs& a, b3host::CallResult& r);
    static void profile_counts(const b3host::CallArgs& a, b3host::CallResult& r);
    bool offered() const;
    int end_series() const;                    // the last series to the CUSTOM series' left
    int next_series(int c) const { return extra_next_series(c, end_series(), order_); }
    int prev_series(int c) const { return extra_prev_series(c, end_series(), order_); }
    std::vector<int> order_;                   // carpacks: SELECT CAR's extra series in order (extra_series_order)
    std::string model_file(int k);
    std::u16string name_of(int k);
    void note(const std::string& s);

    std::vector<ExtraCar> cars_;
    std::vector<std::string> models_;          // per car: the host file the game reads ("" not made yet / not makeable)
    std::vector<bool> made_;
    std::vector<std::uint64_t> hashes_;
    Entry class_of_ = nullptr, new_mode_ = nullptr, request_ = nullptr, add_float_ = nullptr, add_int_ = nullptr;
    Entry add_vector_ = nullptr, car_picture_ = nullptr, car_count_ = nullptr, car_id_ = nullptr, car_thumbnail_ = nullptr;
    Entry row_image_ = nullptr, class_string_ = nullptr, can_right_ = nullptr, step_right_ = nullptr, step_left_ = nullptr;
    Entry arrows_ = nullptr, page_prepare_ = nullptr, carousel_prepare_ = nullptr, race_index_ = nullptr, car_name_ = nullptr;
    Entry lookup_ = nullptr, rivals_ = nullptr, records_ = nullptr, profile_counts_ = nullptr;
    std::function<bool()> series_on_;
    std::string force_player_, force_rivals_;
    std::vector<int> racing_;
    // The vehicle list's extra entries are there only while needed (SELECT CAR on CUSTOM / BETA, a race with an extra
    // car): the list one entry longer costs every GetCarCount / GetCarID walk EE time, so a player who never opens the
    // series plays on the disc's list, byte for byte and cycle for cycle (Fable's review, the golden-style RAM check)
    // carpacks: the cars joined to the list (indices into cars_): a page's (SELECT CAR on it) or a race's; the same set
    // again changes nothing, another one puts the disc's list back first
    void list_set(const std::vector<int>& cars);
    // Fable's review: the player's remembered car (the choices' slot: an extra car chosen before) stays in the list with
    // whatever is joined -- SELECT CAR on another page, stepped out of the series -- so nothing that shows it (the
    // following menus, SELECT CAR opening on it) ever looks up an id the list lacks (GetVehicleIndexFromID: 0, the first
    // car). A page's 20 and that one: the list's 21.
    std::vector<int> remembered() const;       // the remembered extra cars (kRemembered), each once
    void demote();                             // outside SINGLE EVENT: the slots' extra cars their donors (the player's kept)
    std::uint64_t kept_id_ = 0, kept_donor_ = 0;   // the player's extra car while another mode races its donor
    std::vector<int> with_player(const std::vector<int>& set) const;
    // The list's cars: `set` (a page, the race's cars, none) and the remembered ones (with_player); kept as the base the
    // list is rebuilt from when a remembered car changes (tick: a save restored, a choice confirmed)
    void list_base(std::vector<int> set) { base_ = std::move(set); list_set(with_player(base_)); }
    void list_page(int series) { list_base(extra_series_cars(cars_, series)); }
    void list_off(bool check = true);   // (check: list_check's log; not between list_set's restore and its join)
    void list_check(const char* what);         // B3_EXTRA_CHECK=1: the game's own lookups of the joined set, logged
    std::vector<int> joined_;
    std::vector<int> base_;
    std::vector<int> remembered_seen_;
    std::uint64_t seed_at_ = 0;                // B3_EXTRA_TEST_SEED (tests): "<VBlank>:<GtID name>:<slot mask>"
    std::string seed_car_;
    unsigned seed_mask_ = 0;
    unsigned race_serial_ = 0;
    std::string race_entry_;
    bool ready_ = false;                       // the game has its list (CB3Game +0x2C660); disc_count_ its entries
    std::int32_t disc_count_ = 0;
    std::vector<std::uint8_t> saved_;          // the slots' bytes before list_set (driveable, boost bar, GtID)
    bool listed_ = false, in_page_prepare_ = false, car_select_ = false, in_thumbnail_ = false;
    int name_index_ = -1;
    std::map<std::uint32_t, b3host::gaddr> texts_;   // string id -> its UTF-16 text in guest RAM
    std::map<std::string, b3host::gaddr> files_;     // a donor's config file name in guest RAM
    std::set<std::string> notes_;
    std::mutex m_;
    Counts counts_;
};

namespace extra_cars_detail {
constexpr b3host::gaddr kChoices = 0x01E900B0u;          // CB3MenuChoices: +4 game type, +0x30 player, +0x80 cars
constexpr b3host::gaddr kSelectCarChosen = 0x01E94EF8u;  // [C] SELECT CAR's chosen car (its confirm copies it to +0x80)
// Fable's review: the cars a menu may show without SELECT CAR's series on screen: SELECT CAR's own (the page's player's:
// MULTIPLAYER's G1 / G2 use the one page), the choices' player 1 (+0x80) and player 2 (+0x88)
constexpr b3host::gaddr kRemembered[] = {kSelectCarChosen, kChoices + 0x80u, kChoices + 0x88u};
constexpr b3host::gaddr kListLoaded = 0x004EE040u + 0x2C660u;   // [C] func_00133190: pveh/vlist.bin's load done
constexpr b3host::gaddr kOnePlayerStage = 0x0051AD00u, kSingleEventTag = 0x0051B920u, kCurrentMode = 0x0051BA88u;
constexpr b3host::gaddr kStrings = 0x0051BA44u;          // the game's string table (func_00271A00's a0)
constexpr b3host::gaddr kRaceIndex = 0x003EB060u, kRaceName = 0x003EAE40u, kLookup = 0x00271A00u;
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
inline bool carousel_idle() {
    return b3host::load<std::int32_t>(kCarousel + 0x25Cu) == 9 && b3host::load<std::int32_t>(kCarousel + 0x238u) == 0;
}
inline std::uint64_t player_car() {
    const std::int32_t player = b3host::load<std::int32_t>(kChoices + 0x30u);
    return b3host::load<std::uint64_t>(kChoices + 0x80u + 8u * unsigned(player < 0 || player > 7 ? 0 : player));
}
}  // namespace extra_cars_detail

inline std::filesystem::path ExtraCars::custom_dir() {
    if (const char* e = std::getenv("B3_CUSTOM_CARS"); e && *e) return b3io::u8path(e);
    const std::filesystem::path u = save::user_data_dir();
    return u.empty() ? std::filesystem::path() : u / "cars" / "custom";
}

inline std::filesystem::path ExtraCars::beta_dir() {
    if (const char* e = std::getenv("B3_BETA_CARS"); e && *e) return b3io::u8path(e);
    const std::filesystem::path u = save::user_data_dir();
    return u.empty() ? std::filesystem::path() : u / "cars" / "beta";
}
inline std::uint64_t ExtraCars::donor_of(std::uint64_t id) {
    const ExtraCars* p = instance();
    if (!p) return 0;
    const int k = extra_index(p->cars_, id);
    return k < 0 ? 0 : p->cars_[std::size_t(k)].donor_id();
}
inline std::string ExtraCars::thumb_model(int c, int i) const {
    if (!extra_series(c) || !offered()) return {};
    const std::vector<int> cars = extra_series_cars(cars_, c);
    if (i < 0 || std::size_t(i) >= cars.size()) return {};
    return cars_[std::size_t(cars[std::size_t(i)])].gtid_name();
}

inline void ExtraCars::scan(const std::filesystem::path& custom, const std::filesystem::path& beta) {
    const ExtraScan found = extra_scan_all(custom, beta);
    for (const auto& [what, why] : found.refused) std::printf("extra_cars: %s: %s: not offered\n", what.c_str(), why.c_str());
    cars_ = found.cars;
    order_ = extra_series_order(cars_);
    models_.assign(cars_.size(), {});
    made_.assign(cars_.size(), false);
    hashes_.assign(cars_.size(), 0);
    ExtraHashCache cache;   // carpacks: only the files changed since the last start are read
    if (const std::filesystem::path u = save::user_data_dir(); !u.empty()) cache.load(u / "cache" / "extra" / "hashes.txt");
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        hashes_[i] = cache.hash(cars_[i].model);
        std::printf("extra_cars: %s/%s: %s (%s's handling, engine sound%s)\n", cars_[i].cls().c_str(), cars_[i].car().c_str(),
                    cars_[i].source.c_str(), cars_[i].donor_name().c_str(), cars_[i].hwd.empty() ? "" : " banks its own");
    }
    cache.save();
    std::fflush(stdout);
}

inline std::string ExtraCars::entry() const {
    std::string s;
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(hashes_[i]));
        s += (s.empty() ? "" : ";") + cars_[i].cls() + "/" + cars_[i].car() + "=" + cars_[i].donor_name() + ":" + hex;
    }
    return s;
}

inline bool ExtraCars::configure(const std::string& spec) {
    std::size_t at = 0;
    while (at < spec.size()) {
        std::size_t end = spec.find(',', at);
        if (end == std::string::npos) end = spec.size();
        const std::string item = spec.substr(at, end - at);
        at = end + 1;
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) return false;
        if (item.compare(0, eq, "seed") == 0) {   // seed=<VBlank>:<CLS/CARn>:<mask: 1 player 1, 2 player 2, 4 SELECT CAR's own,
                                                  // 8 then demoted as another mode's request does>
            char car[32] = {};
            unsigned long long at_v = 0;
            if (std::sscanf(item.c_str() + eq + 1, "%llu:%31[^:]:%x", &at_v, car, &seed_mask_) != 3 || !at_v) return false;
            const std::string v = traffic_upper_path(car);
            const std::size_t sl = v.find('/');
            if (sl == std::string::npos) return false;
            seed_at_ = at_v, seed_car_ = v.substr(0, sl) + v.substr(sl + 1);
            continue;
        }
        const std::string v = traffic_upper_path(item.substr(eq + 1));
        const std::size_t sl = v.find('/');
        if (sl == std::string::npos) return false;
        const std::string name = v.substr(0, sl) + v.substr(sl + 1);
        if (item.compare(0, eq, "player") == 0) force_player_ = name;
        else if (item.compare(0, eq, "rivals") == 0) force_rivals_ = name;
        else return false;
    }
    return true;
}

inline bool ExtraCars::racing() {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    if (!p) return false;
    if ((b3host::load<std::uint32_t>(kCurrentMode) & 0x01FFFFFFu) != kOnePlayerStage) return false;
    if ((b3host::load<std::uint32_t>(kOnePlayerStage + 0x1B8u) & 0x01FFFFFFu) != kSingleEventTag) return false;
    if (b3host::load<std::int32_t>(kChoices + 4u) != 1) return false;
    return extra_index(p->cars_, b3host::load<std::uint64_t>(kChoices + 0x80u)) >= 0;
}

inline bool ExtraCars::install(HookTable& hooks) {
    using namespace extra_cars_detail;
    if (cars_.empty()) return false;
    const std::vector<Hook> set = {
        {0x00251CB0u, HookLayer::kPort, &class_of, &class_of_},
        {0x0030D1B0u, HookLayer::kPort, &new_mode, &new_mode_},
        {0x0030C2C0u, HookLayer::kPort, &request, &request_},
        {0x0021BB60u, HookLayer::kPort, &add_float, &add_float_},
        {0x0021BD80u, HookLayer::kPort, &add_int, &add_int_},
        {0x0021B9B0u, HookLayer::kPort, &add_vector, &add_vector_},
        {0x00371670u, HookLayer::kPort, &car_picture, &car_picture_},
        {0x00251B90u, HookLayer::kPort, &car_count, &car_count_},
        {0x00251C10u, HookLayer::kPort, &car_id, &car_id_},
        {0x004277C0u, HookLayer::kPort, &car_thumbnail, &car_thumbnail_},
        {0x00427080u, HookLayer::kPort, &row_image, &row_image_},
        {0x0042DA70u, HookLayer::kPort, &class_string, &class_string_},
        {0x0042C1A0u, HookLayer::kPort, &can_right, &can_right_},
        {0x0042BEA0u, HookLayer::kPort, &step_right, &step_right_},
        {0x0042BD70u, HookLayer::kPort, &step_left, &step_left_},
        {0x0042CD60u, HookLayer::kPort, &arrows, &arrows_},
        {0x00318F50u, HookLayer::kPort, &page_prepare, &page_prepare_},
        {0x0042CFA0u, HookLayer::kPort, &carousel_prepare, &carousel_prepare_},
        {kRaceIndex, HookLayer::kPort, &race_index, &race_index_},
        {kRaceName, HookLayer::kPort, &car_name, &car_name_},
        {kLookup, HookLayer::kPort, &lookup, &lookup_},
        {0x0030B9D0u, HookLayer::kPort, &rivals, &rivals_},
        {kRecordsUpdate, HookLayer::kPort, &records, &records_},
        {0x00136F30u, HookLayer::kPort, &profile_counts, &profile_counts_},
    };
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "extra_cars: %08X is not native: no extra cars\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:extra_cars", set, &error)) {
        std::fprintf(stderr, "extra_cars: %s: no extra cars\n", error.c_str());
        return false;
    }
    instance() = this;
    platform::disc::set_alias(&alias);
    TrafficCars::set_extra_donors(&donor_of);   // tsdonor: the Crashbreaker and the AI catch-up series by the donor
    std::printf("extra_cars: CUSTOM (%zu) and BETA (%zu) on SELECT CAR in SINGLE EVENT, in pages of 19 (B3NATIVE_CUSTOM_SERIES=0: not offered)%s\n",
                std::size_t(std::count_if(cars_.begin(), cars_.end(), [](const ExtraCar& c) { return c.series == kCustomSeries; })),
                std::size_t(std::count_if(cars_.begin(), cars_.end(), [](const ExtraCar& c) { return c.series == kBetaSeries; })),
                force_player_.empty() && force_rivals_.empty() ? "" : "; the race's cars set");
    std::fflush(stdout);
    return true;
}

inline void ExtraCars::note(const std::string& s) {
    if (!notes_.insert(s).second) return;
    std::printf("extra_cars: %s\n", s.c_str());
    std::fflush(stdout);
}

// Once the game has its vehicle list: an entry per extra car (GtID, driveable, the donor's boost bar size)
inline void ExtraCars::tick() {
    using namespace extra_cars_detail;
    if (ready_) {
        if (seed_at_ && b3native::platform::vblank_count() >= seed_at_) {   // (tests: an extra car remembered, as a save would)
            seed_at_ = 0;
            for (std::size_t k = 0; k < cars_.size(); ++k)
                if (cars_[k].gtid_name() == seed_car_)
                    for (unsigned i = 0; i < 8; ++i)
                        if (i < 3 && seed_mask_ >> i & 1u) b3host::store<std::uint64_t>(i < 2 ? kChoices + 0x80u + 8u * i : kSelectCarChosen, cars_[k].id());
            std::printf("extra_cars: TEST: %s remembered (slots %x)\n", seed_car_.c_str(), seed_mask_);
            if (seed_mask_ & 8u) demote();   // (8: as another mode's race request leaves it)
        }
        // the kept car back once the game type is SINGLE EVENT's again, where the slot still has the donor it was given
        if (kept_id_ && b3host::load<std::int32_t>(kChoices + 4u) == 1) {
            const int k = extra_index(cars_, kept_id_);
            int back = 0;
            for (const b3host::gaddr at : {kSelectCarChosen, kChoices + 0x80u})
                if (b3host::load<std::uint64_t>(at) == kept_donor_) b3host::store<std::uint64_t>(at, kept_id_), ++back;
            std::printf("extra_cars: VBlank %llu: SINGLE EVENT again: %s/%s remembered again (%d slots; %s)\n",
                        static_cast<unsigned long long>(b3native::platform::vblank_count()), k >= 0 ? cars_[std::size_t(k)].cls().c_str() : "?",
                        k >= 0 ? cars_[std::size_t(k)].car().c_str() : "?", back, back ? "its donor still there" : "another car chosen since");
            kept_id_ = kept_donor_ = 0;
        }
        // Fable's review: a remembered car changed (a save restored, a choice confirmed, a race's rivals): the list again
        // with it (the same set: nothing written)
        if (std::vector<int> now = remembered(); now != remembered_seen_) {
            remembered_seen_ = std::move(now);
            list_base(base_);
        }
        return;
    }
    if (!b3host::load<std::uint8_t>(kListLoaded)) return;
    const std::int32_t n = b3host::load<std::int32_t>(kVehicleList + 4u);
    if (n <= 0 || n >= kVehicleListMax) return;
    ready_ = true;
    disc_count_ = n;
    std::printf("extra_cars: the game's vehicle list: %d entries (the extra cars join it while SELECT CAR shows their series or "
                "a race has one)\n", n);
    std::fflush(stdout);
}
// The cars `set` joined after the disc's entries: GtID, driveable, the donor's boost bar size (the slots' bytes kept)
inline void ExtraCars::list_set(const std::vector<int>& set) {
    using namespace extra_cars_detail;
    if (!ready_) return;
    if (listed_ && set == joined_) return;
    list_off(set.empty());
    if (set.empty()) return;
    const std::int32_t n = disc_count_;
    saved_.clear();
    for (std::int32_t i = n; i < kVehicleListMax; ++i)
        for (b3host::gaddr a : {kVehicleList + 8u + 4u * unsigned(i), kVehicleList + 0x208u + 4u * unsigned(i)})
            for (unsigned k = 0; k < 4; ++k) saved_.push_back(b3host::load<std::uint8_t>(a + k));
    for (std::int32_t i = n; i < kVehicleListMax; ++i)
        for (unsigned k = 0; k < 8; ++k) saved_.push_back(b3host::load<std::uint8_t>(kVehicleList + 0x408u + 8u * unsigned(i) + k));
    int at = n;
    for (int k : set) {
        if (k < 0 || std::size_t(k) >= cars_.size()) continue;
        const ExtraCar& c = cars_[std::size_t(k)];
        if (at >= kVehicleListMax) {
            note("the vehicle list is full: " + c.cls() + "/" + c.car() + " not listed");
            break;
        }
        std::int32_t donor = -1;
        for (std::int32_t i = 0; i < n; ++i)
            if (b3host::load<std::uint64_t>(kVehicleList + 0x408u + 8u * unsigned(i)) == c.donor_id()) donor = i;
        if (donor < 0) {
            note(c.cls() + "/" + c.car() + ": its donor " + c.donor_name() + " is not in the game's vehicle list: not listed");
            continue;
        }
        b3host::store<std::int32_t>(kVehicleList + 8u + 4u * unsigned(at), 1);
        b3host::store<std::int32_t>(kVehicleList + 0x208u + 4u * unsigned(at), b3host::load<std::int32_t>(kVehicleList + 0x208u + 4u * unsigned(donor)));
        b3host::store<std::uint64_t>(kVehicleList + 0x408u + 8u * unsigned(at), c.id());
        ++at;
    }
    b3host::store<std::int32_t>(kVehicleList + 4u, at);
    listed_ = true;
    joined_ = set;
    ++counts_.listed;
    std::printf("extra_cars: the vehicle list: %d entries (the disc's %d and %d extra: %s/%s .. %s/%s)\n", at, n, at - n,
                cars_[std::size_t(set.front())].cls().c_str(), cars_[std::size_t(set.front())].car().c_str(),
                cars_[std::size_t(set.back())].cls().c_str(), cars_[std::size_t(set.back())].car().c_str());
    std::fflush(stdout);
    list_check("joined");
}
// B3_EXTRA_CHECK=1 (checks only; the calls cost EE time): the game's own list lookups -- DoesVehicleExist 0x252000 and
// GetVehicleIndexFromID 0x251E60 -- for every extra car: the joined ones found at their entries, the others absent
inline std::vector<int> ExtraCars::remembered() const {
    using namespace extra_cars_detail;
    std::vector<int> out;
    for (const b3host::gaddr at : kRemembered)
        if (const int k = extra_index(cars_, b3host::load<std::uint64_t>(at)); k >= 0 && std::find(out.begin(), out.end(), k) == out.end())
            out.push_back(k);
    return out;
}
// `set` and the remembered cars not in it: those first in room (at most 3: a page of 19 and the player's two always fit;
// a third, player 2's, takes a page's last car only when all three are different extra cars off the page)
inline std::vector<int> ExtraCars::with_player(const std::vector<int>& set) const {
    std::vector<int> extra;
    for (int k : remembered())
        if (std::find(set.begin(), set.end(), k) == set.end()) extra.push_back(k);
    std::vector<int> out(set.begin(), set.begin() + std::ptrdiff_t(std::min<std::size_t>(set.size(), std::size_t(kExtraListRoom) - extra.size())));
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}
inline void ExtraCars::list_check(const char* what) {
    using namespace extra_cars_detail;
    static const bool on = [] { const char* e = std::getenv("B3_EXTRA_CHECK"); return e && e[0] == '1'; }();
    if (!on) return;
    int found = 0, absent = 0, wrong = 0;
    for (std::size_t k = 0; k < cars_.size(); ++k) {
        const bool in = listed_ && std::find(joined_.begin(), joined_.end(), int(k)) != joined_.end();
        const std::uint64_t id = cars_[k].id();
        const int exists = b3host::call_guest<std::int32_t>(0x00252000u, kVehicleList, id);
        const int index = b3host::call_guest<std::int32_t>(0x00251E60u, kVehicleList, id);
        const bool ok = in ? exists == 1 && index >= disc_count_ && b3host::load<std::uint64_t>(kVehicleList + 0x408u + 8u * unsigned(index)) == id
                           : exists == 0;
        (ok ? (in ? found : absent) : wrong) += 1;
    }
    std::string mine;   // the remembered cars: SELECT CAR's own, the choices' players 1 and 2
    for (const b3host::gaddr at : kRemembered) {
        const int k = extra_index(cars_, b3host::load<std::uint64_t>(at));
        mine += mine.empty() ? "" : ", ";
        mine += k < 0 ? "a game car" : std::find(joined_.begin(), joined_.end(), k) != joined_.end() && listed_ ? "listed" : "NOT listed";
    }
    std::printf("extra_cars: check (%s): %d joined found at their entries, %d others absent, %d wrong; the remembered cars %s\n", what, found,
                absent, wrong, mine.c_str());
    std::fflush(stdout);
}
// The disc's list again, byte for byte
inline void ExtraCars::list_off(bool check) {
    using namespace extra_cars_detail;
    if (!listed_) return;
    std::size_t k = 0;
    for (std::int32_t i = disc_count_; i < kVehicleListMax; ++i)
        for (b3host::gaddr a : {kVehicleList + 8u + 4u * unsigned(i), kVehicleList + 0x208u + 4u * unsigned(i)})
            for (unsigned q = 0; q < 4; ++q) b3host::store<std::uint8_t>(a + q, saved_[k++]);
    for (std::int32_t i = disc_count_; i < kVehicleListMax; ++i)
        for (unsigned q = 0; q < 8; ++q) b3host::store<std::uint8_t>(kVehicleList + 0x408u + 8u * unsigned(i) + q, saved_[k++]);
    b3host::store<std::int32_t>(kVehicleList + 4u, disc_count_);
    listed_ = false;
    joined_.clear();
    std::printf("extra_cars: the vehicle list: the disc's %d entries again\n", disc_count_);
    std::fflush(stdout);
    if (check) list_check("the disc's");
}

// ---- the disc
inline std::string ExtraCars::alias(const std::string& p) {
    ExtraCars* t = instance();
    if (!t) return {};
    const std::string u = traffic_upper_path(p);
    if (u.compare(0, 10, "PVEH/XCUS/") != 0 && u.compare(0, 10, "PVEH/XBET/") != 0) return {};
    std::lock_guard<std::mutex> lk(t->m_);
    for (std::size_t i = 0; i < t->cars_.size(); ++i)
        if (u.compare(0, t->cars_[i].disc_base().size(), t->cars_[i].disc_base()) == 0 && u.size() > 4 &&
            u.compare(u.size() - 4, 4, ".BGV") == 0)
            t->model_file(int(i));
    const std::string a = extra_alias(t->cars_, t->models_, p);
    if (!a.empty()) t->note("the disc's " + u + ": " + (a.compare(0, 5, "host:") == 0 ? a.substr(5) : a));
    return a;
}

// The file the game reads for car k: its own (with engine sound models), or it with its donor's added (cached)
inline std::string ExtraCars::model_file(int k) {
    using namespace extra_cars_detail;
    if (k < 0 || std::size_t(k) >= cars_.size()) return {};
    if (made_[std::size_t(k)]) return models_[std::size_t(k)];
    made_[std::size_t(k)] = true;
    const ExtraCar& c = cars_[std::size_t(k)];
    std::vector<std::uint8_t> b;
    if (!extra_read_file(b3io::u8path(c.model), b) || !extra_car_file_problem(b).empty()) {
        std::printf("extra_cars: %s: cannot be read now: not driveable\n", c.source.c_str());
        return {};
    }
    if (extra_car_has_sound(b)) return models_[std::size_t(k)] = c.model;
    std::vector<std::uint8_t> donor;
    const std::string hd = platform::disc::resolve("PVEH/" + c.donor_cls + "/" + c.donor_car + ".BGV");
    if (hd.empty() || !b3asset::disc::read_file(b3io::u8path(hd), donor) || donor.empty()) {   // (discread: a disc image too)
        std::printf("extra_cars: %s: %s's .BGV cannot be read: not driveable\n", c.source.c_str(), c.donor_name().c_str());
        return {};
    }
    const std::vector<std::uint8_t> m = traffic_model(b, donor);
    if (m.empty()) {
        std::printf("extra_cars: %s: no engine sound models of its own and its model reaches past %s's: not driveable\n", c.source.c_str(),
                    c.donor_name().c_str());
        return {};
    }
    const std::filesystem::path u = save::user_data_dir();
    if (u.empty()) return {};
    const std::filesystem::path dir = u / "cache" / "extra";
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(extra_hash(b) ^ extra_hash(donor)));
    const std::filesystem::path f = dir / (c.gtid_name() + "_" + c.donor_name() + "_" + hex + ".bgv");
    std::error_code ec;
    if (std::filesystem::file_size(f, ec) != m.size() || ec) {
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path tmp = f.string() + ".tmp";
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            o.write(reinterpret_cast<const char*>(m.data()), std::streamsize(m.size()));
            if (!o) { std::filesystem::remove(tmp, ec); return {}; }
        }
        std::filesystem::rename(tmp, f, ec);
        if (ec) { std::filesystem::remove(tmp, ec); return {}; }
    }
    note(c.source + ": " + c.donor_name() + "'s engine sound models added (" + b3io::u8string(f) + ")");
    return models_[std::size_t(k)] = b3io::u8string(f);
}

// ---- classes and the race
// GetClassFromID(id): an extra car's is its series
inline void ExtraCars::class_of(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    const int k = extra_index(p->cars_, u64_of(a.gpr[0]));
    if (k < 0) {
        p->class_of_(a, r);
        return;
    }
    ret(r, std::uint32_t(p->cars_[std::size_t(k)].series));
}
// RequestNewGameMode (every mode's request): an extra car in a slot of any request but SINGLE EVENT's one-player one
// races as its donor (they are offered there only; the game keeps the last car chosen across modes)
inline void ExtraCars::new_mode(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    p->racing_.clear();
    if (b3host::load<std::int32_t>(kChoices + 4u) != 1) p->demote();
    p->new_mode_(a, r);
}
// Outside SINGLE EVENT every slot's extra car races as its donor, and SELECT CAR's own is the donor too (the race's list
// stays the disc's). Fable's review: the player's own extra car (SELECT CAR's, else player 1's) is kept here and is
// remembered again when the game type is SINGLE EVENT's once more (tick), where its donor still is
inline void ExtraCars::demote() {
    using namespace extra_cars_detail;
    for (const b3host::gaddr at : {kSelectCarChosen, kChoices + 0x80u})
        if (const int k = extra_index(cars_, b3host::load<std::uint64_t>(at)); k >= 0 && !kept_id_) {
            kept_id_ = cars_[std::size_t(k)].id(), kept_donor_ = cars_[std::size_t(k)].donor_id();
            std::printf("extra_cars: %s/%s kept for SINGLE EVENT (this mode races it as %s)\n", cars_[std::size_t(k)].cls().c_str(),
                        cars_[std::size_t(k)].car().c_str(), cars_[std::size_t(k)].donor_name().c_str());
        }
    for (unsigned i = 0; i < 8; ++i) {
        const int k = extra_index(cars_, b3host::load<std::uint64_t>(kChoices + 0x80u + 8u * i));
        if (k < 0) continue;
        b3host::store<std::uint64_t>(kChoices + 0x80u + 8u * i, cars_[std::size_t(k)].donor_id());
        ++counts_.demoted;
        note("an extra car outside SINGLE EVENT races as its donor " + cars_[std::size_t(k)].donor_name());
    }
    if (const int k = extra_index(cars_, b3host::load<std::uint64_t>(kSelectCarChosen)); k >= 0)
        b3host::store<std::uint64_t>(kSelectCarChosen, cars_[std::size_t(k)].donor_id());
    list_base({});
}
// RequestOnePlayerGameMode (SINGLE EVENT): the set cars (tests), then the race's extra cars (their tuning their donors')
inline void ExtraCars::request(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    p->racing_.clear();
    if (b3host::load<std::int32_t>(kChoices + 4u) != 1) {
        p->request_(a, r);
        return;
    }
    auto find = [&](const std::string& name) {
        for (std::size_t i = 0; i < p->cars_.size(); ++i)
            if (p->cars_[i].gtid_name() == name && !p->model_file(int(i)).empty()) return int(i);
        return -1;
    };
    if (const int k = p->force_player_.empty() ? -1 : find(p->force_player_); k >= 0)
        b3host::store<std::uint64_t>(kChoices + 0x80u, p->cars_[std::size_t(k)].id());
    if (const int k = p->force_rivals_.empty() ? -1 : find(p->force_rivals_); k >= 0)
        for (unsigned i = 1; i < 8; ++i) b3host::store<std::uint64_t>(kChoices + 0x80u + 8u * i, p->cars_[std::size_t(k)].id());
    std::vector<int> any;   // the list's entries for the request: the extra cars of every slot (the race's own: below)
    for (unsigned i = 0; i < 8; ++i)
        if (const int k = extra_index(p->cars_, b3host::load<std::uint64_t>(kChoices + 0x80u + 8u * i)); k >= 0 &&
            std::find(any.begin(), any.end(), k) == any.end())
            any.push_back(k);
    p->list_set(any);
    p->request_(a, r);   // (RIVALS' CARS writes its picks in here: the cars read after it)
    // the slots the race uses: the player's and its AI cars' (the stage record's count, as the request made it); a slot
    // past them may keep an earlier race's car (an extra car after "Same as yours", the attract demo's)
    const std::int32_t ai = b3host::load<std::int32_t>(kOnePlayerStage + 0xA4u);
    const unsigned used = unsigned(1 + std::clamp(ai, 0, 7));
    for (unsigned i = 0; i < used; ++i)
        if (const int k = extra_index(p->cars_, b3host::load<std::uint64_t>(kChoices + 0x80u + 8u * i)); k >= 0) {
            if (std::find(p->racing_.begin(), p->racing_.end(), k) == p->racing_.end()) p->racing_.push_back(k);
            std::printf("extra_cars: car %u of the race: %s/%s, %s (%s's handling and engine sound)\n", i, p->cars_[std::size_t(k)].cls().c_str(),
                        p->cars_[std::size_t(k)].car().c_str(), p->cars_[std::size_t(k)].source.c_str(), p->cars_[std::size_t(k)].donor_name().c_str());
        }
    p->list_base(p->racing_);   // the race's own extra cars and the remembered ones (none: the disc's list)
    p->race_entry_.clear();
    for (int k : p->racing_) {   // (their bytes now: the replay's check)
        std::vector<std::uint8_t> b;
        const ExtraCar& c = p->cars_[std::size_t(k)];
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(extra_read_file(b3io::u8path(c.model), b) ? extra_hash(b) : 0));
        p->race_entry_ += (p->race_entry_.empty() ? "" : ";") + c.cls() + "/" + c.car() + "=" + hex;
    }
    ++p->race_serial_;
    if (const char* e = std::getenv("B3_EXTRA_CHECK"); e && e[0] == '1')
        for (int k : p->racing_) {   // (checks: the game's own lookups of a race car, as its race prepares)
            const ExtraCar& c = p->cars_[std::size_t(k)];
            const int index = b3host::call_guest<std::int32_t>(0x00251E60u, kVehicleList, c.id());
            const int boost = b3host::call_guest<std::int32_t>(0x00251EA0u, kVehicleList, c.id());
            const int donor_boost = b3host::call_guest<std::int32_t>(0x00251EA0u, kVehicleList, c.donor_id());
            std::printf("extra_cars: check (race): %s/%s at entry %d, boost bar %d (its donor %s's %d)\n", c.cls().c_str(), c.car().c_str(),
                        index, boost, c.donor_name().c_str(), donor_boost);
        }
    std::fflush(stdout);
}
// AddEntry: a racing extra car's per-vehicle file registers as its donor's
inline void ExtraCars::add(const b3host::CallArgs& a, b3host::CallResult& r, Entry next) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    if (p->racing_.empty()) {
        next(a, r);
        return;
    }
    const std::string file = guest_text(a.gpr[4].w[0]);
    const std::string to = extra_config(p->cars_, p->racing_, file);
    if (to.empty()) {
        next(a, r);
        return;
    }
    b3host::gaddr s = 0;
    if (const auto it = p->files_.find(to); it != p->files_.end()) s = it->second;
    else if ((s = guest_arena().alloc("port:extra_cars", std::uint32_t(to.size() + 1), 4)) != 0) {
        for (std::size_t i = 0; i < to.size(); ++i) b3host::store<std::uint8_t>(s + std::uint32_t(i), std::uint8_t(to[i]));
        b3host::store<std::uint8_t>(s + std::uint32_t(to.size()), 0);
        p->files_[to] = s;
    }
    if (!s) {
        next(a, r);
        return;
    }
    ++p->counts_.configs;
    p->note(file + " registers as " + to);
    b3host::CallArgs b = a;
    b.gpr[4] = reg(s);
    next(b, r);
}
// func_00371670 (the race's pictures): an extra car's is its donor's
inline void ExtraCars::car_picture(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    const int k = extra_index(p->cars_, u64_of(a.gpr[2]));
    if (k < 0) {
        p->car_picture_(a, r);
        return;
    }
    ++p->counts_.pictures;
    b3host::CallArgs b = a;
    b.gpr[2] = reg(p->cars_[std::size_t(k)].donor_id());
    p->car_picture_(b, r);
}
// func_0030B9D0(choices, the player's class): an extra car's rivals are race cars of its donor's class (RIVALS' CARS'
// own pick untouched)
inline void ExtraCars::rivals(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    if (RaceOptions::explicit_rival_pick() || !extra_series(int(a.gpr[1].w[0]))) {
        p->rivals_(a, r);
        return;
    }
    const int k = extra_index(p->cars_, player_car());
    const int cls = k >= 0 ? traffic_class_of(p->cars_[std::size_t(k)].donor_cls.c_str()) : 0;
    b3host::CallArgs b = a;
    b.gpr[1] = reg(std::uint32_t(cls));
    p->note("the rivals of an extra car: race cars of class " + std::to_string(cls));
    p->rivals_(b, r);
}
inline void ExtraCars::records(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    if (!racing()) {
        p->records_(a, r);
        return;
    }
    const std::uint64_t kinds = records_without_kinds(p->records_, a, r);
    ++p->counts_.records_skipped;
    std::printf("extra_cars: an extra car's race: no records kept (the kinds %llx not written)\n", static_cast<unsigned long long>(kinds));
    std::fflush(stdout);
}
inline void ExtraCars::profile_counts(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    p->profile_counts_(a, r);
    if ((r.v0.w[0] & 0xFFu) == 0 || !racing()) return;
    if (p->counts_.profile_skipped++ == 0) {
        std::printf("extra_cars: an extra car's race: the profile's totals and unlocks left as they were\n");
        std::fflush(stdout);
    }
    r.v0 = u_long128{};
}

// ---- SELECT CAR
inline bool ExtraCars::offered() const {
    using namespace extra_cars_detail;
    return ready_ && car_select_ && traffic_series_event(series_on_ && series_on_(), b3host::load<std::int32_t>(kChoices + 4u));
}
inline int ExtraCars::end_series() const {
    if (TrafficCars* t = TrafficCars::active()) return t->series_end();
    return carousel_last_class(b3host::load<std::uint8_t>(kCarousel + 0x265u) != 0, b3host::load<std::uint8_t>(kCarousel + 0x264u) != 0);
}
inline void ExtraCars::page_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    p->in_page_prepare_ = true;
    p->page_prepare_(a, r);
    p->in_page_prepare_ = false;
}
// func_0042CFA0 (the carousel's Prepare): SELECT CAR's from SELECT CAR's Prepare; a remembered extra car (or series 10 /
// 11) while the series is not offered opens on the first series
inline void ExtraCars::carousel_prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    p->car_select_ = p->in_page_prepare_;
    const bool ours = extra_series(int(a.gpr[3].w[0])) || extra_index(p->cars_, player_car()) >= 0;
    if (ours && (!p->offered() || (a.gpr[1].w[0] == 9u && std::int32_t(a.gpr[2].w[0]) == -1))) {
        b3host::CallArgs b = a;
        b.gpr[3] = u_long128{};
        b.gpr[4] = u_long128{};
        p->note("SELECT CAR opens on the first series (an extra car remembered)");
        p->carousel_prepare_(b, r);
        return;
    }
    if (ours) {   // SELECT CAR opening on an extra car's series: its page joined
        const int c = int(a.gpr[3].w[0]);
        const int k = extra_index(p->cars_, player_car());
        p->list_page(extra_series(c) ? c : k >= 0 ? p->cars_[std::size_t(k)].code() : kCustomSeries);
    }
    p->carousel_prepare_(a, r);
}
// GetCarCount(list, class): series 10 / 11 the extra cars while offered (else none); class 9 none, as the disc's list
inline void ExtraCars::car_count(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    const int c = int(a.gpr[1].w[0]);
    if (c == 9 && p->listed_) { extra_cars_detail::ret(r, 0); return; }   // (none, as the disc's list has)
    if (!extra_series(c)) { p->car_count_(a, r); return; }
    extra_cars_detail::ret(r, p->offered() ? extra_series_cars(p->cars_, c).size() : 0u);
}
// GetCarID(list, class, i): series 10 / 11's i-th extra car (its donor's within the small pictures' lookups)
inline void ExtraCars::car_id(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    const int c = int(a.gpr[1].w[0]);
    if (c == 9 && p->listed_) { extra_cars_detail::ret(r, 0); return; }
    if (!extra_series(c)) { p->car_id_(a, r); return; }
    const std::vector<int> cars = extra_series_cars(p->cars_, c);
    const std::int32_t i = std::int32_t(a.gpr[2].w[0]);
    if (!p->offered() || i < 0 || std::size_t(i) >= cars.size()) { extra_cars_detail::ret(r, 0); return; }
    const ExtraCar& x = p->cars_[std::size_t(cars[std::size_t(i)])];
    extra_cars_detail::ret(r, p->in_thumbnail_ ? x.donor_id() : x.id());
}
inline void ExtraCars::car_thumbnail(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    if (!extra_series(int(a.gpr[0].w[0]))) { p->car_thumbnail_(a, r); return; }
    p->in_thumbnail_ = true;
    p->car_thumbnail_(a, r);
    p->in_thumbnail_ = false;
}
inline void ExtraCars::row_image(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    if (!extra_series(int(a.gpr[5].w[0]))) { p->row_image_(a, r); return; }
    p->in_thumbnail_ = true;
    p->row_image_(a, r);
    p->in_thumbnail_ = false;
}
// GetClassString(class): the series' names (our string ids)
inline void ExtraCars::class_string(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    const int c = int(a.gpr[0].w[0]);
    if (!extra_series(c)) { p->class_string_(a, r); return; }
    extra_cars_detail::ret(r, kExtraStringBase + std::uint32_t(c - kCustomSeries));
}
inline void ExtraCars::can_right(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    if (carousel_self(a) && p->offered() && carousel_idle() && p->next_series(b3host::load<std::int32_t>(kCarouselSeries)) >= 0) {
        ret(r, 1);
        return;
    }
    p->can_right_(a, r);
}
inline void ExtraCars::step_right(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    const int to = carousel_self(a) && p->offered() && carousel_idle() ? p->next_series(b3host::load<std::int32_t>(kCarouselSeries)) : -1;
    if (to < 0) { p->step_right_(a, r); return; }
    p->list_page(to);   // (IsCarUnlocked: a car the list does not have is locked)
    carousel_step(to, 3);
    p->note("SELECT CAR: " + std::string(extra_series_kind(to) == kCustomSeries ? "CUSTOM" : "BETA") +
            (extra_series_page(to) ? " " + std::to_string(extra_series_page(to) + 1) : "") + " (series " + std::to_string(to) + ")");
}
inline void ExtraCars::step_left(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    const int to = carousel_self(a) && p->offered() && carousel_idle() ? p->prev_series(b3host::load<std::int32_t>(kCarouselSeries)) : -1;
    if (to < 0) { p->step_left_(a, r); return; }
    if (extra_series(to)) p->list_page(to);
    carousel_step(to, 4);
    if (!extra_series(to)) p->list_base({});   // (the disc's list, and the remembered extra cars if any)
}
inline void ExtraCars::arrows(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    p->arrows_(a, r);
    if (!carousel_self(a) || !p->offered() || !carousel_idle() || b3host::load<std::int32_t>(kCarousel + 0x230u) == 1) return;
    if (p->next_series(b3host::load<std::int32_t>(kCarouselSeries)) < 0) return;
    const std::uint32_t o = b3host::load<std::uint32_t>(kCarousel + 0x22Cu) & 0x01FFFFFFu;
    if (o) b3host::store<float>(o + 0x10u + 0xCu, 1.0f);
}
// func_003EB060(id) (a race car's index; -1 for an extra car, as the game): remembered for the name lookup after it
inline void ExtraCars::race_index(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    p->race_index_(a, r);
    p->name_index_ = std::int32_t(r.v0.w[0]) == -1 ? extra_index(p->cars_, extra_cars_detail::u64_of(a.gpr[0])) : -1;
}
// func_003EAE40(index, kind) (a race car's name's string id): -1 right after an extra car's func_003EB060 -> ours
inline void ExtraCars::car_name(const b3host::CallArgs& a, b3host::CallResult& r) {
    ExtraCars* p = instance();
    const int k = p->name_index_;
    p->name_index_ = -1;
    if (k < 0 || std::int32_t(a.gpr[0].w[0]) != -1) { p->car_name_(a, r); return; }
    ++p->counts_.names;
    extra_cars_detail::ret(r, kExtraStringCar0 + std::uint32_t(k));
}
// An extra car's name: its car.ini's, else its donor's with " MOD" (extra_words)
inline std::u16string ExtraCars::name_of(int k) {
    using namespace extra_cars_detail;
    const ExtraCar& c = cars_[std::size_t(k)];
    std::u16string u;
    if (!c.name.empty()) {
        for (char ch : c.name) u += char16_t(static_cast<unsigned char>(ch));
        return u;
    }
    const std::int32_t index = b3host::call_guest<std::int32_t>(kRaceIndex, c.donor_id());
    name_index_ = -1;
    if (index >= 0) {
        const std::uint32_t sid = b3host::call_guest<std::uint32_t>(kRaceName, index, 1);
        const std::uint32_t text = b3host::call_guest<std::uint32_t>(kLookup, kStrings, sid) & 0x01FFFFFFu;
        for (b3host::gaddr q = text; text && q < text + 2 * 40; q += 2) {
            const std::uint16_t ch = b3host::load<std::uint16_t>(q);
            if (!ch) break;
            u += char16_t(ch);
        }
    }
    if (u.empty()) for (char ch : c.donor_name()) u += char16_t(ch);
    return u + extra_utf16(extra_words(platform::disc::language_text()).mod);
}
// func_00271A00(strings, id): our string ids (the series' names, the cars' names)
inline void ExtraCars::lookup(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace extra_cars_detail;
    ExtraCars* p = instance();
    const std::uint32_t id = a.gpr[1].w[0];
    const bool series = id >= kExtraStringBase && extra_series(int(id - kExtraStringBase) + kCustomSeries);
    const bool car = id >= kExtraStringCar0 && id < kExtraStringCar0 + std::uint32_t(p->cars_.size());
    if ((!series && !car) || (a.gpr[0].w[0] & 0x01FFFFFFu) != kStrings) { p->lookup_(a, r); return; }
    if (const auto it = p->texts_.find(id); it != p->texts_.end()) { ret(r, it->second); return; }
    std::u16string u;
    if (series) {
        const ExtraWords& w = extra_words(platform::disc::language_text());
        const int c = int(id - kExtraStringBase) + kCustomSeries;   // CUSTOM, CUSTOM 2, ..., BETA, BETA 2
        u = extra_utf16(extra_series_kind(c) == kCustomSeries ? w.custom : w.beta);
        if (extra_series_page(c) > 0)
            for (char ch : " " + std::to_string(extra_series_page(c) + 1)) u += char16_t(ch);
    } else u = p->name_of(int(id - kExtraStringCar0));
    const b3host::gaddr at = guest_arena().alloc("port:extra_cars", std::uint32_t(2 * (u.size() + 1)), 4);
    if (!at) { p->lookup_(a, r); return; }
    for (std::size_t i = 0; i < u.size(); ++i) b3host::store<std::uint16_t>(at + 2u * std::uint32_t(i), std::uint16_t(u[i]));
    b3host::store<std::uint16_t>(at + 2u * std::uint32_t(u.size()), 0);
    p->texts_[id] = at;
    ret(r, at);
}

}  // namespace b3native

#endif
