// more_rivals.h -- the more rivals trial: 8 cars (7 rivals) in a Single Event RACE (B3NATIVE_MORE_RIVALS=7).
//
// The game's car count (6) is designed into fixed per-car arrays (tools/decomp2host/split_census.py lists them, config/split_arrays.tsv);
// the generated units hold those as b3host::split_array (decomp2host's split_arrays.py pass), whose elements 6+ live in a shadow of guest
// memory 32 MB up (guest RAM is 65 MB while the option is set). This component does the rest, only while the race is
// a Single Event RACE with the game's 5 rivals chosen:
//   - the stage record (func_001346C0's self, 0x0051AD00): AI cars +0xA4 raised to 5 + extra; their car IDs (+0x18
//     u64[]) and colours (+0x78 s32[]) for the extra cars flow past the game's 5 into the network fields (+0x40, +0x8C),
//     unused offline, where the game's own loops read them;
//   - the world's pools: the game reaches AI car i at +0x12FCF0 + i * 0x2460 and its physics at physics manager
//     +0x256A0 + i * 0x1580, so AI cars 5.. land on the network cars and their physics (unused offline): those are
//     made AI cars there: the bytes of AI car 0 / its physics as they are at the first setup (the objects' vtables
//     come from the game's static init, which the Construct functions do not repeat), then Construct__8CB3AICarFi
//     0x143E80 / Construct__15CB3AICarPhysicsFv 0x1D2D10; the network objects are put back the same way (their first
//     setup's bytes, then Construct__13CB3NetworkCarFi / Construct__20CB3NetworkCarPhysicsFv) when a race without the
//     option is set up;
//   - the car models (memory block 1, one 456 KB block per car index, 6 in the layout): indices 6.. get the arena's
//     last 1 MB, which only layout 2 uses (AcquireBlock / ReleaseBlock of CB3MemoryManagerPS2, 0x2224C0 / 0x2223C0);
//   - the start grid (6 hand-placed spots per stage, CB3GameData +0x370): spots 6.. carry on the grid's stagger
//     behind its last rank (each rear spot moved back by its column's rank step), the spot's other fields copied;
//   - the traffic system's race car infos (scratchpad, 6 x 0x70, then its parameter list): moved whole to the region
//     while the race has the cars (split_array.h split::moved);
//   - the engine sound: CB3SoundRaceCarManager (0x01E77550) has 5 CB3SoundAICar (+0x540, 0x160 each) that the AI cars
//     ask for (RequestRaceCar 0x2EEFE0, from CB3AICar::PrepareSound); when they are taken, cars 7.. get one of 2 more
//     in the region (the pool's first as the race starts, its bank / voices cleared, then Construct__13CB3SoundAICarFv
//     0x28BF40, +0xB8 its index); each loads its bank and takes its voices as the game's own;
//   - records and the profile: a race with the cars writes no records and does not count (raceopts' rule: the
//     records call 0x3D1090 with no kinds, 0x136F30 answers no);
//   - TEST (B3NATIVE_MORE_RIVALS_TEST_FINISH=<n>): the player's lap completed every n racing score updates, so a
//     scripted run reaches the results (raceopts' test hook); _TEST_FINISH_AI=<n> the same for every AI car (the AI
//     finish first: the player's last place);
//   - the functions with a per-car array on the guest stack, replaced while the race has the cars (the same steps
//     with the array in the region): CB3AIDriver::CalcDifficultyIndex 0x144D00.
// Off (the default) nothing is installed, guest RAM is 32 MB and the split arrays are flat memory: retail.
#ifndef B3NATIVE_MORE_RIVALS_H
#define B3NATIVE_MORE_RIVALS_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "b3host/split_array.h"
#include "b3gameview/gameview.h"
#include "hook_table.h"
#include "more_rivals_grid.h"
#include "records_guard.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace b3native {

namespace more_rivals_detail {
constexpr b3host::gaddr kStageSetup = 0x001346C0u;     // func_001346C0: the stage record -> the world
constexpr b3host::gaddr kAcquire = 0x002224C0u, kRelease = 0x002223C0u;   // CB3MemoryManagerPS2 (this, block, index)
constexpr b3host::gaddr kStartPoint = 0x0015E8C0u;     // CB3StartPoint::Prepare(slot, CB3StartPositionStruct*)
constexpr b3host::gaddr kAICar = 0x00143E80u, kNetCar = 0x00150240u, kAIPhys = 0x001D2D10u, kNetPhys = 0x00269CF0u;
constexpr b3host::gaddr kWorld = 0x00522660u, kAIPool = kWorld + 0x12FCF0u, kNetPool = kWorld + 0x13B2D0u;
constexpr b3host::gaddr kPhysics = kWorld + 0x3C910u, kAIBodies = kPhysics + 0x256A0u, kNetBodies = kPhysics + 0x2C220u;
constexpr std::uint32_t kAICarBytes = 0x2460u, kNetCarBytes = 0x1AA0u, kAIBodyBytes = 0x1580u, kNetBodyBytes = 0x1560u;
constexpr b3host::gaddr kStartSlots = kWorld + 0x125B40u + 0x370u;   // CB3GameData +0x370: CB3StartPoint[6]
constexpr b3host::gaddr kMenuChoices = 0x01E900B0u;
constexpr b3host::gaddr kDifficulty = 0x00144D00u;     // CB3AIDriver::CalcDifficultyIndex (a 6-car stack array)
constexpr b3host::gaddr kQsort = 0x00125338u, kDifficultyCmp = 0x00144E20u;
// CB3CrashActionDirector::Prepare (this): clears its per-car crash table (+0x110, 6 x 0x3C; split, cad.car) for 6 cars
// CB3HUDScoreList::ActualPrepare (this): 2 objects per listed car into +0x20 [12] (HUD mode 9 only)
constexpr b3host::gaddr kScoreListPrepare = 0x003CB090u;
constexpr b3host::gaddr kCrashPrepare = 0x001B2760u, kCrashCars = 0x110u, kCrashCarSize = 0x3Cu;
constexpr b3host::gaddr kRecords = kRecordsUpdate, kProfileCounts = 0x00136F30u;   // (records_guard.h; race_options.h)
constexpr b3host::gaddr kScoreUpdate = 0x0015B000u, kIncrementLap = 0x0015A1F0u, kPlayerScore = 0x0064E470u;
constexpr b3host::gaddr kSoundCars = 0x01E77550u, kSoundAIPool = kSoundCars + 0x540u;   // CB3SoundRaceCarManager
constexpr b3host::gaddr kRequestSoundCar = 0x002EEFE0u, kSoundAICarConstruct = 0x0028BF40u;
constexpr std::uint32_t kSoundAICarBytes = 0x160u;
constexpr b3host::gaddr kLaneCount = 0x0064D358u, kLanePoints = 0x0064D354u, kStageRecord = 0x0051AD00u;
constexpr b3host::gaddr kRequest = 0x0030C2C0u, kTrackList = 0x005196A0u, kCreateTrackPath = 0x00251A30u;
constexpr b3host::gaddr kCarCount = 0x0064D37Cu, kCarList = 0x0064D388u, kSlotCount = 0x0064D360u, kSlots = 0x0064D350u;
// the arena's last 1 MB: layout 2's (layout 1, the race's, ends at arena + 0x15F0000; the arena is 0x16F0000 bytes)
constexpr b3host::gaddr kRegion = 0x0067D880u + 0x15F0000u, kRegionEnd = 0x0067D880u + 0x16F0000u;
constexpr std::uint32_t kModelBlock = 0x72000u, kSpotBytes = 0x50u;
// the region: the 2 model blocks, then (past layout 0's last block, which ends at region + 0x50480) the 2 spots +0x0,
// CalcDifficultyIndex's array +0x200, the traffic infos +0x400 (8 x 0x70), the 2 engine sounds +0x800 (2 x 0x160), the
// 2 engine sound models +0xB00 (2 x 0x90), the capture's copy of cars 7 and 8's pointers +0xC80 (b3gv::kMoreCars)
constexpr b3host::gaddr kExtraSound = kRegion + 2u * kModelBlock + 0x800u;
// the capture's copy of cars 7 and 8's pointers (b3gv::kMoreCars, include/b3gameview/gameview.h) at +0xC80
static_assert(b3gv::kMoreCars == kRegion + 2u * kModelBlock + 0xC80u, "gameview.h's kMoreCars is the region's +0xC80");
// the engine sound models: CB3SoundESMManager 0x01E78170, its 5 CB3SoundESMLow at +0xC8 (0x90 each) [C RequestESM 0x2FAE90]
constexpr b3host::gaddr kEsmManager = 0x01E78170u, kEsmLowPool = kEsmManager + 0xC8u, kRequestEsm = 0x002FAE90u;
constexpr b3host::gaddr kEsmLowConstruct = 0x00287940u;
constexpr std::uint32_t kEsmLowBytes = 0x90u;
constexpr b3host::gaddr kExtraEsm = kRegion + 2u * kModelBlock + 0xB00u;   // 2 x CB3SoundESMLow
constexpr int kGameCars = 6, kGameAI = 5;
inline b3host::gaddr a(const b3host::CallArgs& c, int i) { return c.gpr[i].w[0]; }
}  // namespace more_rivals_detail

class MoreRivals {
public:
    // On unless B3NATIVE_MORE_RIVALS=0 (then guest RAM is the console's 32 MB too: split_array.h ram_bytes)
    static bool requested() { return b3host::split::requested(); }
    // TEST: B3NATIVE_MORE_RIVALS=6 / 7 gives a RACE with the game's 5 rivals chosen that many (scripted checks that go
    // through the game's page untouched); else the race's own count (SELECT OPTIONS' RIVALS, up to kMaxRivals)
    static int forced() {
        const char* e = std::getenv("B3NATIVE_MORE_RIVALS");
        const int n = e && *e ? std::atoi(e) : 0;
        return n >= 5 && n <= 7 ? n : 0;   // 5: the game's own field with the test hooks (comparison runs)
    }
    static constexpr int kMaxRivals = 7;
    // extras (PC SETTINGS > EXTRAS > MORE RIVALS): whether a race may have more than 5 rivals now (none set: always);
    // off, a RIVALS of 6 / 7 kept from before is the game's 5 (the race record and CB3MenuChoices +0x18)
    void set_on_query(std::function<bool()> f) { on_query_ = std::move(f); }
    bool install(HookTable& hooks);
    // elim1: an Eliminator (single_eliminator.h selected()) keeps the game's frame: a stale RIVALS above 5 is taken as 5
    void set_eliminator_query(std::function<bool()> f) { eliminator_ = std::move(f); }
    // the race's rivals past the game's 5 (0: the game's own race)
    int extra() const { return active_ ? extra_ : 0; }
    bool active() const { return active_; }

private:
    static MoreRivals*& instance() { static MoreRivals* p = nullptr; return p; }
    static void stage_setup(const b3host::CallArgs& c, b3host::CallResult& r);
    static void acquire(const b3host::CallArgs& c, b3host::CallResult& r);
    static void release(const b3host::CallArgs& c, b3host::CallResult& r);
    static void start_point(const b3host::CallArgs& c, b3host::CallResult& r);
    static void difficulty(const b3host::CallArgs& c, b3host::CallResult& r);
    static void debug_segment(const b3host::CallArgs& c, b3host::CallResult& r);
    static void records(const b3host::CallArgs& c, b3host::CallResult& r);
    static void profile_counts(const b3host::CallArgs& c, b3host::CallResult& r);
    static void crash_prepare(const b3host::CallArgs& c, b3host::CallResult& r);
    static void score_list(const b3host::CallArgs& c, b3host::CallResult& r);
    static void score_update(const b3host::CallArgs& c, b3host::CallResult& r);
    void restore_network();
    void synthesise_grid();
    std::vector<GridQuad> road();
    void grid_check(bool placed);
    static void request(const b3host::CallArgs& c, b3host::CallResult& r);
    static void request_sound(const b3host::CallArgs& c, b3host::CallResult& r);
    static void request_esm(const b3host::CallArgs& c, b3host::CallResult& r);

    std::vector<unsigned char> ai_car_, ai_body_, net_cars_, net_bodies_;   // the first setup's bytes
    int extra_ = 0;            // cars past the game's 6 (this race)
    int forced_ = 0;           // TEST: B3NATIVE_MORE_RIVALS
    std::function<bool()> eliminator_;
    bool active_ = false;      // this race has them
    bool race_ = false;        // this race is a Single Event RACE with the game's 5 rivals chosen
    std::function<bool()> on_query_;   // extras
    bool overwritten_ = false; // the network pools hold AI cars
    HookEntry stage_next_ = nullptr, acquire_next_ = nullptr, release_next_ = nullptr, start_next_ = nullptr,
              difficulty_next_ = nullptr, debug_next_ = nullptr, records_next_ = nullptr, profile_next_ = nullptr,
              score_next_ = nullptr, crash_prepare_next_ = nullptr, score_list_next_ = nullptr;
    int test_finish_ = 0, test_ticks_ = 0, test_finish_ai_ = 0;
    std::map<b3host::gaddr, int> ai_ticks_;
    int test_track_ = -1;      // TEST: the track list's index (-2: list them)
    bool test_grid_ = false, grid_checked_ = false, test_init_ = false;
    std::set<b3host::gaddr> init_logged_;
    std::map<b3host::gaddr, unsigned> td_seen_;
    int test_shadow_ = 0, shadow_ticks_ = 0, shadow_dumps_ = 0;
    void dump_shadow();
    int grid_ticks_ = 0;
    int heading_row_ = 2;
    float heading_sign_ = 1.0f;
    HookEntry request_next_ = nullptr, sound_next_ = nullptr, esm_next_ = nullptr;
    unsigned debug_left_ = 40;
};

inline bool MoreRivals::install(HookTable& hooks) {
    using namespace more_rivals_detail;
    forced_ = forced();
    for (b3host::gaddr at : {kStageSetup, kAcquire, kRelease, kStartPoint, kAICar, kNetCar, kAIPhys, kNetPhys, kDifficulty,
                             kRecords, kProfileCounts, kScoreUpdate, kRequestSoundCar, kSoundAICarConstruct, kRequestEsm, kEsmLowConstruct, kCrashPrepare, kScoreListPrepare})
        if (!hooks.has_row(at)) {
            std::fprintf(stderr, "more_rivals: %08X is not native: off\n", at);
            return false;
        }

    std::vector<Hook> specs = {{kStageSetup, HookLayer::kPort, &stage_setup, &stage_next_},
                               {kAcquire, HookLayer::kPort, &acquire, &acquire_next_},
                               {kRelease, HookLayer::kPort, &release, &release_next_},
                               {kStartPoint, HookLayer::kPort, &start_point, &start_next_},
                               {kDifficulty, HookLayer::kPort, &difficulty, &difficulty_next_}};
    specs.push_back({kRecords, HookLayer::kPort, &records, &records_next_});
    specs.push_back({kRequestSoundCar, HookLayer::kPort, &request_sound, &sound_next_});
    specs.push_back({kRequestEsm, HookLayer::kPort, &request_esm, &esm_next_});
    specs.push_back({kProfileCounts, HookLayer::kPort, &profile_counts, &profile_next_});
    specs.push_back({kCrashPrepare, HookLayer::kPort, &crash_prepare, &crash_prepare_next_});
    specs.push_back({kScoreListPrepare, HookLayer::kPort, &score_list, &score_list_next_});
    if (const char* e = std::getenv("B3NATIVE_MORE_RIVALS_TEST_FINISH")) test_finish_ = std::atoi(e);
    if (const char* e = std::getenv("B3NATIVE_MORE_RIVALS_TEST_TRACK"); e && *e) test_track_ = std::strcmp(e, "list") == 0 ? -2 : std::atoi(e);
    if (test_track_ != -1) specs.push_back({kRequest, HookLayer::kPort, &request, &request_next_});
    if (const char* e = std::getenv("B3NATIVE_MORE_RIVALS_TEST_GRID"); e && *e == '1') test_grid_ = true;
    if (const char* e = std::getenv("B3NATIVE_MORE_RIVALS_TEST_FINISH_AI"); e && *e) test_finish_ai_ = std::atoi(e);
    if (const char* e = std::getenv("B3NATIVE_MORE_RIVALS_TEST_INIT"); e && *e == '1') test_init_ = true;
    if (const char* e = std::getenv("B3NATIVE_MORE_RIVALS_TEST_SHADOW"); e && *e) test_shadow_ = std::atoi(e);
    if (test_finish_ > 0 || test_grid_ || test_finish_ai_ > 0 || test_init_ || test_shadow_ > 0) specs.push_back({kScoreUpdate, HookLayer::kPort, &score_update, &score_next_});
    if (std::getenv("B3NATIVE_MORE_RIVALS_DEBUG")) specs.push_back({0x0013ED50u, HookLayer::kPort, &debug_segment, &debug_next_});
    std::string error;
    if (!hooks.install("port:more_rivals", specs, &error)) {
        std::fprintf(stderr, "more_rivals: %s: off\n", error.c_str());
        return false;
    }
    instance() = this;
    b3host::split::traffic_infos = kRegion + 2u * kModelBlock + 0x400u;   // 8 x 0x70 (the traffic system's race car infos)
    if (forced_) std::printf("more_rivals: TEST: a Single Event RACE with the game's 5 rivals has %d (B3NATIVE_MORE_RIVALS)\n", forced_);
    std::printf("more_rivals: RACE with up to %d rivals (SELECT OPTIONS' RIVALS); guest RAM grows by 33 MB (the split arrays' "
                "shadow) at the first such race\n", kMaxRivals);
    return true;
}

// A copy of a game object made into another (an extra AI car, its physics, its engine sound): words that point into the
// source object are moved onto the copy (its own sub-objects and self pointers; Construct resets some, not all of them).
// Returns how many it moved.
inline int rebase_copy(b3host::gaddr dst, b3host::gaddr from, std::uint32_t n) {
    int moved = 0;
    for (std::uint32_t k = 0; k + 4 <= n; k += 4) {
        const std::uint32_t w = b3host::load<std::uint32_t>(dst + k);
        if (w >= from && w < from + n) { b3host::store<std::uint32_t>(dst + k, w - from + dst); ++moved; }
    }
    return moved;
}

inline void MoreRivals::restore_network() {
    using namespace more_rivals_detail;
    // AI cars 5.. covered network cars 0..2 and their physics: construct those back (the world's Construct does so once)
    std::memcpy(b3host::host_pointer(kNetPool, 3 * kNetCarBytes), net_cars_.data(), net_cars_.size());
    std::memcpy(b3host::host_pointer(kNetBodies, 3 * kNetBodyBytes), net_bodies_.data(), net_bodies_.size());
    for (int i = 0; i < 3; ++i) {
        b3host::call_guest<void>(kNetCar, kNetPool + i * kNetCarBytes, i);
        b3host::call_guest<void>(kNetPhys, kNetBodies + i * kNetBodyBytes);
    }
    overwritten_ = false;
}

inline void MoreRivals::stage_setup(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    const b3host::gaddr rec = a(c, 0);
    auto keep = [](std::vector<unsigned char>& v, b3host::gaddr at, std::uint32_t n) {
        const auto* p = static_cast<const unsigned char*>(b3host::host_pointer(at, n));
        v.assign(p, p + n);
    };
    if (m.ai_car_.empty()) {   // the first setup (the front end's, at boot): the objects as the game's static init made them
        keep(m.ai_car_, kAIPool, kAICarBytes);
        keep(m.ai_body_, kAIBodies, kAIBodyBytes);
        keep(m.net_cars_, kNetPool, 3 * kNetCarBytes);
        keep(m.net_bodies_, kNetBodies, 3 * kNetBodyBytes);
    }
    if (b3host::load<std::int32_t>(rec + 0x1BCu) != 0) {   // a pending setup (the function's own test)
        const bool race = b3host::load<std::int32_t>(kMenuChoices + 4u) == 1 && b3host::load<std::int32_t>(kMenuChoices + 8u) == 0 &&
                          b3host::load<std::int32_t>(rec + 0xA0u) == 1 && b3host::load<std::int32_t>(rec + 0xA8u) == 0;
        int ai = b3host::load<std::int32_t>(rec + 0xA4u);
        if (race && ai > kGameAI && m.eliminator_ && m.eliminator_()) {   // an Eliminator: the game's 5 at most
            b3host::store<std::int32_t>(rec + 0xA4u, kGameAI);
            std::printf("more_rivals: an ELIMINATOR keeps the game's 5 rivals (RIVALS %d)\n", ai);
            ai = kGameAI;
        }
        if (race && m.forced_ && ai == kGameAI && !(m.eliminator_ && m.eliminator_())) ai = m.forced_;   // TEST
        if (race && ai > kGameAI && m.on_query_ && !m.on_query_()) {   // extras: MORE RIVALS off: the game's 5, kept so
            std::printf("more_rivals: MORE RIVALS off: the race keeps the game's 5 rivals (RIVALS %d)\n", ai);
            b3host::store<std::int32_t>(rec + 0xA4u, kGameAI);
            if (b3host::load<std::int32_t>(kMenuChoices + 0x18u) > kGameAI) b3host::store<std::int32_t>(kMenuChoices + 0x18u, kGameAI);
            ai = kGameAI;
        }
        bool want = race && ai > kGameAI && ai <= kMaxRivals;
        if (!want && ai > kGameAI) {   // any other setup (an Eliminator, a World Tour event, another mode of the request's
            // race family, which copies CB3MenuChoices +0x18 unclamped [C 0x30C2C0 case 0]): never more than the game's 5,
            // whatever RIVALS was left at (the game's own values are 5 at most, so this never fires without MORE RIVALS)
            std::printf("more_rivals: a setup that is not a Single Event RACE keeps the game's 5 rivals (%d asked)\n", ai);
            b3host::store<std::int32_t>(rec + 0xA4u, kGameAI);
            ai = kGameAI;
        }
        if (want && !b3host::split::ready()) {   // the shadow's guest RAM (reserved at boot, committed now)
            std::fprintf(stderr, "more_rivals: guest RAM could not grow for the shadow: the race keeps the game's 5 rivals\n");
            b3host::store<std::int32_t>(rec + 0xA4u, kGameAI);
            ai = kGameAI;
            want = false;
        }
        m.race_ = race && ai >= kGameAI;
        m.grid_checked_ = false;
        m.grid_ticks_ = 0;
        m.ai_ticks_.clear();
        m.init_logged_.clear();
        m.td_seen_.clear();
        m.extra_ = want ? ai - kGameAI : 0;
        if (m.overwritten_ && !want) m.restore_network();
        b3host::split::on = want;
        m.active_ = want;
        if (want) {
            b3host::split::reset();
            b3host::store<std::int32_t>(rec + 0xA4u, ai);
            // the rivals past the game's 5: rivals 3 and 4's cars in another paint (at most 6 models in a race: the
            // physics attribute table and the engine sound banks have 6 slots); ids +0x18 (u64), colours +0x78 (s32)
            for (int k = kGameAI; k < ai; ++k) {
                const int from = k - 3;
                b3host::store<std::uint64_t>(rec + 0x18u + 8u * k, b3host::load<std::uint64_t>(rec + 0x18u + 8u * from));
                b3host::store<std::int32_t>(rec + 0x78u + 4u * k, (b3host::load<std::int32_t>(rec + 0x78u + 4u * from) + 1) & 3);
                std::memcpy(b3host::host_pointer(kAIPool + k * kAICarBytes, kAICarBytes), m.ai_car_.data(), kAICarBytes);
                const int moved_car = rebase_copy(kAIPool + k * kAICarBytes, kAIPool, kAICarBytes);
                b3host::call_guest<void>(kAICar, kAIPool + k * kAICarBytes, k);
                b3host::store<std::uint32_t>(kAIPool + k * kAICarBytes + 0x1918u, 0);   // no engine sound yet (PrepareSound asks)
                std::memcpy(b3host::host_pointer(kAIBodies + k * kAIBodyBytes, kAIBodyBytes), m.ai_body_.data(), kAIBodyBytes);
                const int moved_body = rebase_copy(kAIBodies + k * kAIBodyBytes, kAIBodies, kAIBodyBytes);
                b3host::call_guest<void>(kAIPhys, kAIBodies + k * kAIBodyBytes);
                std::printf("more_rivals: AI car %d: car id %016llx colour %d, at %08X (physics %08X; %d / %d pointers into the "
                            "copied objects moved onto it)\n", k,
                            static_cast<unsigned long long>(b3host::load<std::uint64_t>(rec + 0x18u + 8u * k)),
                            b3host::load<std::int32_t>(rec + 0x78u + 4u * k), kAIPool + k * kAICarBytes, kAIBodies + k * kAIBodyBytes,
                            moved_car, moved_body);
            }
            for (int k = 0; k < m.extra_ && k < 2; ++k) {   // the extra engine sounds, free
                const b3host::gaddr o = kExtraSound + std::uint32_t(k) * kSoundAICarBytes;
                // the pool's first as it is now (built by the manager's Construct; released by the last race), its bank,
                // waves and voices cleared (CB3SoundAICar::Release 0x28BB30 leaves them so), then the game's Construct
                std::memcpy(b3host::host_pointer(o, kSoundAICarBytes), b3host::host_pointer(kSoundAIPool, kSoundAICarBytes), kSoundAICarBytes);
                rebase_copy(o, kSoundAIPool, kSoundAICarBytes);
                b3host::store<std::uint32_t>(o + 0x4u, 0);   // its engine sound model: its own (CB3SoundAICar::Prepare asks)
                for (b3host::gaddr f = 0x140u; f < 0x160u; f += 4) b3host::store<std::uint32_t>(o + f, 0);
                b3host::call_guest<void>(kSoundAICarConstruct, o);
                b3host::store<std::int32_t>(o + 0xB8u, kGameAI + k);
                b3host::store<std::uint8_t>(o + 0xBDu, 0);
            }
            for (int k = 0; k < m.extra_ && k < 2; ++k) {   // the extra engine sound models, free (the pool's first, built)
                const b3host::gaddr o = kExtraEsm + std::uint32_t(k) * kEsmLowBytes;
                std::memcpy(b3host::host_pointer(o, kEsmLowBytes), b3host::host_pointer(kEsmLowPool, kEsmLowBytes), kEsmLowBytes);
                rebase_copy(o, kEsmLowPool, kEsmLowBytes);
                b3host::call_guest<void>(kEsmLowConstruct, o);
                b3host::store<std::uint8_t>(o + 0x55u, 0);
            }
            m.overwritten_ = true;
            std::fflush(stdout);
        }
    }
    m.stage_next_(c, r);
    if (m.active_)   // the capture's copy of cars 7 and 8 (the game's list holds them in its shadow: SetCarNumbers is done)
        for (int k = 0; k < m.extra_; ++k)
            b3host::store<std::uint32_t>(b3gv::kMoreCars + 4u * std::uint32_t(k),
                                         b3host::load<std::uint32_t>(b3host::split::shadow_or_flat(kCarList, 4, kGameCars, std::size_t(kGameCars + k))));
}

inline void MoreRivals::acquire(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    const int block = int(a(c, 1)), idx = int(a(c, 2));
    if (m.active_ && block == 1 && idx >= kGameCars && idx < kGameCars + m.extra_) {
        const b3host::gaddr at = kRegion + std::uint32_t(idx - kGameCars) * kModelBlock;
        r.v0 = u_long128{};
        r.v0.w[0] = at;
        std::printf("more_rivals: car %d's model block at %08X\n", idx, at);
        std::fflush(stdout);
        return;
    }
    m.acquire_next_(c, r);
}

inline void MoreRivals::release(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    const int block = int(a(c, 1)), idx = int(a(c, 2));
    if (block == 1 && idx >= kGameCars && idx < kGameCars + 2) {
        r.v0 = u_long128{};
        r.v0.w[0] = 1;
        return;
    }
    m.release_next_(c, r);
}

// Prepare clears its 6 cars' crash state (elements 0..5: bytes +0x0..+0x1A through its +0xF0 view, +0x1C..+0x3B through
// its +0x12C view; byte +0x1B left); cars 7-8's elements (in the shadow) are cleared the same way. A race's setup
// zeroes the whole shadow (split::reset), so this is for a restart, which prepares again without a new setup
inline void MoreRivals::crash_prepare(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    m.crash_prepare_next_(c, r);
    if (!b3host::split::on) return;
    const b3host::gaddr self = a(c, 0);
    std::printf("more_rivals: the crash director prepared: cars 7-8's crash state cleared\n");
    m.init_logged_.clear();   // TEST_INIT: the tables again after a restart
    m.shadow_ticks_ = 0;      // TEST_SHADOW: counted from each prepare (a race's start, a restart)
    for (int k = 0; k < 2; ++k) {
        const b3host::gaddr el = b3host::split::shadow_or_flat(self + kCrashCars, kCrashCarSize, kGameCars, std::size_t(kGameCars + k));
        for (b3host::gaddr o = 0; o < kCrashCarSize; ++o)
            if (o != 0x1Bu) b3host::store<std::uint8_t>(el + o, 0);
    }
}

// The HUD's score list (HUD mode 9: CB3HUDManager::PrepareNormalComponents 0x1A0AF0 case 9) has room for 6 listed cars
// (+0x20 [12], 2 objects each); a race with the extra cars never has that HUD (the log says so if one ever did)
inline void MoreRivals::score_list(const b3host::CallArgs& c, b3host::CallResult& r) {
    MoreRivals& m = *instance();
    if (m.active_)
        std::printf("more_rivals: the HUD score list prepared in a race with %d cars (HUD mode %d)\n", more_rivals_detail::kGameCars + m.extra_,
                    b3host::load<std::int32_t>(0x0066A3D0u + 0x174u));
    m.score_list_next_(c, r);
}

inline void MoreRivals::start_point(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    m.start_next_(c, r);
    if (m.active_ && a(c, 0) == kStartSlots + 4u * (kGameCars - 1)) m.synthesise_grid();
}

// CB3SoundRaceCarManager::RequestRaceCar 0x2EEFE0 (type: 1 AI, 2 network): the game's pool first, then the extras
inline void MoreRivals::request_sound(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    m.sound_next_(c, r);
    const int type = int(a(c, 1));
    if (!m.active_ || r.v0.w[0] != 0 || (type != 1 && type != 2)) return;
    for (int k = 0; k < m.extra_ && k < 2; ++k) {
        const b3host::gaddr o = kExtraSound + std::uint32_t(k) * kSoundAICarBytes;
        if (b3host::load<std::uint8_t>(o + 0xBDu) == 0) {
            b3host::store<std::uint8_t>(o + 0xBDu, 1);
            r.v0 = u_long128{};
            r.v0.w[0] = o;
            std::printf("more_rivals: engine sound %d for a car past the game's (at %08X)\n", kGameAI + k, o);
            std::fflush(stdout);
            return;
        }
    }
}

// CB3SoundESMManager::RequestESM 0x2FAE90 (false: an AI's CB3SoundESMLow): the game's pool first, then the extras
// (released by RelinquishESM 0x2FAE70: +0x55 0, as the game's)
inline void MoreRivals::request_esm(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    m.esm_next_(c, r);
    if (!m.active_ || r.v0.w[0] != 0 || (a(c, 1) & 0xFFu) != 0) return;
    for (int k = 0; k < m.extra_ && k < 2; ++k) {
        const b3host::gaddr o = kExtraEsm + std::uint32_t(k) * kEsmLowBytes;
        if (b3host::load<std::uint8_t>(o + 0x55u) == 0) {
            b3host::store<std::uint8_t>(o + 0x55u, 1);
            r.v0 = u_long128{};
            r.v0.w[0] = o;
            return;
        }
    }
}

// CB3AIDriver::CalcDifficultyIndex (0x144D00): the cars sorted by their stage slot's difficulty (+0x97) into a
// 12-byte-per-car array (the retail frame's local holds 6), the driver's car's place in that order returned
inline void MoreRivals::difficulty(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    if (!m.active_) { m.difficulty_next_(c, r); return; }
    const b3host::gaddr self = a(c, 0), buf = kRegion + 2u * kModelBlock + 0x200u;
    const int n = b3host::load<std::int32_t>(kCarCount), slots = b3host::load<std::int32_t>(kSlotCount);
    const b3host::gaddr t2 = b3host::load<std::uint32_t>(kSlots);
    for (int i = 0; i < n; ++i) {
        const int v1 = i < slots ? b3host::load<std::uint8_t>(t2 + 0x98u * i + 0x97u) : 0;
        const b3host::gaddr car = b3host::load<std::uint32_t>(b3host::split::shadow_or_flat(kCarList, 4, kGameCars, i));
        b3host::store<std::int32_t>(buf + 12u * i, i);
        b3host::store<std::int32_t>(buf + 12u * i + 4u, v1);
        b3host::store<std::uint8_t>(buf + 12u * i + 8u, b3host::load<std::int32_t>(car + 0x1914u) == 1);
    }
    b3host::call_guest<void>(kQsort, buf, n, 12, kDifficultyCmp);
    const int mine = b3host::load<std::int8_t>(b3host::load<std::uint32_t>(self + 0x7A0u) + 0x19ACu);
    int out = 0;
    for (int i = 0; i < n; ++i)
        if (mine == b3host::load<std::int32_t>(buf + 12u * i)) { out = i; break; }
    r.v0 = u_long128{};
    r.v0.w[0] = std::uint32_t(out);
}

// CB3Records::Update 0x3D1090 (records, kinds, flag): no kinds while the race has the cars, then the last update as a
// race that set no record leaves it (race_options.h)
inline void MoreRivals::records(const b3host::CallArgs& c, b3host::CallResult& r) {
    MoreRivals& m = *instance();
    if (!m.active_) { m.records_next_(c, r); return; }
    const std::uint64_t kinds = records_without_kinds(m.records_next_, c, r);   // records_guard.h
    std::printf("more_rivals: the race had %d cars: the record kinds %llu not written\n", more_rivals_detail::kGameCars + m.extra_,
                static_cast<unsigned long long>(kinds));
    std::fflush(stdout);
}

// 0x136F30 (whether the race counts for the profile): not with the extra cars
inline void MoreRivals::profile_counts(const b3host::CallArgs& c, b3host::CallResult& r) {
    MoreRivals& m = *instance();
    m.profile_next_(c, r);
    if (m.active_) r.v0 = u_long128{};
}

// TEST (B3NATIVE_MORE_RIVALS_TEST_SHADOW=n): at the player's n-th score update after each prepare, the shadow's non-zero
// bytes as runs (gaps under 16 bytes joined) by the guest address they stand for (world +offset where in CB3World), with
// a hash of each: a fresh start's and a restart's at the same tick show any table a restart's prepares do not cover
inline void MoreRivals::dump_shadow() {
    ++shadow_dumps_;
    const auto* p = static_cast<const unsigned char*>(b3host::host_pointer(b3host::split::kShadow, b3host::kMainRamSize));
    std::uint32_t runs = 0, bytes = 0;
    for (std::uint32_t i = 0; i < b3host::kMainRamSize;) {
        if (!p[i]) { ++i; continue; }
        std::uint32_t j = i, last = i;
        std::uint64_t h = 1469598103934665603ull;
        while (j < b3host::kMainRamSize && j - last < 16) {
            if (p[j]) last = j;
            ++j;
        }
        for (std::uint32_t q = i; q <= last; ++q) h = (h ^ p[q]) * 1099511628211ull;
        const std::uint32_t at = i, n = last - i + 1;
        if (at >= 0x00522660u && at < 0x00522660u + 0x143860u)
            std::printf("more_rivals: TEST shadow %d: world+%06X..+%X %016llX\n", shadow_dumps_, at - 0x00522660u, n, static_cast<unsigned long long>(h));
        else
            std::printf("more_rivals: TEST shadow %d: %08X..+%X %016llX\n", shadow_dumps_, at, n, static_cast<unsigned long long>(h));
        ++runs;
        bytes += n;
        i = last + 1;
    }
    std::printf("more_rivals: TEST shadow %d: %u runs, %u bytes\n", shadow_dumps_, runs, bytes);
}

// TEST: CB3Score::Update 0x15B000, after the game's: the player's lap completed every test_finish_ updates while it
// races (+0x27C 2, +0x8 no finish yet, +0x14 past the start line)
inline void MoreRivals::score_update(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    const b3host::gaddr s = a(c, 0);
    if (m.test_init_ && m.active_ && m.race_) {   // TEST_INIT: a takedown to credit (+0x4D8 by the victim's index set, before
        // the update's CalculateScoreForTakedowns credits it and clears it), the
        // network player count CalculateScoreForTakedowns walks for its flags (CGtNetworkPlayerManager 0x01E32AE0 +0x28)
        auto& seen = m.td_seen_[s];
        for (std::size_t v = 0; v < 8; ++v) {
            const bool on = b3host::load<float>(b3host::split::shadow_or_flat(s + 0x4D8u, 4, kGameCars, v)) >= 0.0f;
            if (on && !(seen >> v & 1u))
                std::printf("more_rivals: TEST takedown: score %08X, victim %zu (network players %d)\n", s, v,
                            b3host::load<std::int32_t>(0x01E32B08u));
            seen = on ? seen | (1u << v) : seen & ~(1u << v);
        }
    }
    m.score_next_(c, r);
    if (!m.race_) return;
    if (m.test_shadow_ > 0 && m.active_ && s == kPlayerScore && ++m.shadow_ticks_ == m.test_shadow_) m.dump_shadow();
    if (m.test_init_ && m.active_ && m.init_logged_.insert(s).second) {
        // TEST (B3NATIVE_MORE_RIVALS_TEST_INIT): a score's per-opponent tables at its first update, victims 0..7 (7-8 in
        // the shadow): the crash escape times +0x440 / +0x488 / +0x4A0 and the takedown time +0x4D8 (the game's 'none'
        // -1.0, CB3Score::Prepare's loop, bounded by split::bound(6))
        std::printf("more_rivals: TEST score %08X at its first update:", s);
        for (b3host::gaddr off : {0x440u, 0x488u, 0x4A0u, 0x4D8u}) {
            std::printf(" +%03X", off);
            for (std::size_t v = 0; v < 8; ++v)
                std::printf(" %g", double(b3host::load<float>(b3host::split::shadow_or_flat(s + off, 4, kGameCars, v))));
        }
        std::printf("\n");
    }
    if (s != kPlayerScore) {   // TEST: an AI car's laps (B3NATIVE_MORE_RIVALS_TEST_FINISH_AI), so it finishes first
        if (m.test_finish_ai_ <= 0 || b3host::load<std::int32_t>(s + 0x27Cu) != 2 || b3host::load<std::int32_t>(s + 0x8u) != 0 ||
            b3host::load<std::int32_t>(s + 0x14u) < 0)
            return;
        if (++m.ai_ticks_[s] % m.test_finish_ai_ != 0) return;
        b3host::call_guest<void>(kIncrementLap, s);
        return;
    }
    if (m.test_grid_ && !m.grid_checked_) {   // the first update: the cars as placed on the grid
        m.grid_checked_ = true;
        m.grid_check(true);
    }
    if (m.test_grid_ && b3host::load<std::int32_t>(s + 0x27Cu) == 2 && ++m.grid_ticks_ == 300) m.grid_check(false);
    if (m.test_finish_ <= 0) return;
    if (b3host::load<std::int32_t>(s + 0x27Cu) != 2 || b3host::load<std::int32_t>(s + 0x8u) != 0 || b3host::load<std::int32_t>(s + 0x14u) < 0) return;
    if (++m.test_ticks_ % m.test_finish_ != 0) return;
    b3host::call_guest<void>(kIncrementLap, kPlayerScore);
    std::printf("more_rivals: TEST: the player's lap %d completed by the test\n", b3host::load<std::int32_t>(s + 0x14u));
    std::fflush(stdout);
}

// debugging (B3NATIVE_MORE_RIVALS_DEBUG): CB3AITarget::UpdateCurrentSegment's car, its lane and segment
inline void MoreRivals::debug_segment(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    const b3host::gaddr map = a(c, 0), car = b3host::load<std::uint32_t>(map + 0x454u);
    const int idx = b3host::load<std::int8_t>(car + 0x19ACu);
    const b3host::gaddr ts = 0x00527440u;
    const int n = b3host::load<std::uint8_t>(b3host::split::shadow_or_flat(ts + 0x36356u, 1, kGameCars, idx));
    const b3host::gaddr row = b3host::split::shadow_or_flat(ts + 0x36218u, 25, kGameCars, idx);
    bool odd = n > 25;
    for (int i = 0; i < n && i < 25; ++i) {
        const int v = b3host::load<std::uint8_t>(row + i);
        const b3host::gaddr t = ts + 0x127D0u + 0x180u * std::uint32_t(v);
        const std::uint32_t p = b3host::load<std::uint32_t>(t + 0x114u);
        if (p && p >= 0x02000000u) odd = true;
    }
    if (odd || m.debug_left_) {
        if (m.debug_left_) --m.debug_left_;
        std::printf("more_rivals: debug: map %08X car %d count %d row %08X:", map, idx, n, row);
        for (int i = 0; i < n && i < 25; ++i) std::printf(" %d", b3host::load<std::uint8_t>(row + i));
        std::printf("%s\n", odd ? " ODD" : "");
        std::fflush(stdout);
    }
    m.debug_next_(c, r);
}

// The AI lanes' drivable segments as quads (more_rivals_grid.h): the lane count 0x0064D358, the points 0x0064D354
// (16 bytes each), lane i at world +0x2C + 8 i (+4: its CB3AILaneStruct: +0 the u16 point pairs, a pair per segment,
// +8 the 10-byte segment records (+3 & 7 == 5: not drivable), +0xC the segment count, +0xE 1 for a loop) [C
// CB3AILane::FindClosestDriveableAILaneSegment 0x148A80, LaneIsDriveable 0x148C20]
inline std::vector<GridQuad> MoreRivals::road() {
    using namespace more_rivals_detail;
    std::vector<GridQuad> out;
    const int lanes = b3host::load<std::int32_t>(kLaneCount);
    const b3host::gaddr pts = b3host::load<std::uint32_t>(kLanePoints);
    if (lanes <= 0 || lanes > 4096 || !pts) return out;
    auto point = [&](unsigned i) {
        return GridV3{b3host::load<float>(pts + 16u * i), b3host::load<float>(pts + 16u * i + 4u), b3host::load<float>(pts + 16u * i + 8u)};
    };
    for (int i = 0; i < lanes; ++i) {
        const b3host::gaddr ls = b3host::load<std::uint32_t>(kWorld + 0x2Cu + 8u * std::uint32_t(i) + 4u);
        if (!ls) continue;
        const b3host::gaddr pairs = b3host::load<std::uint32_t>(ls), recs = b3host::load<std::uint32_t>(ls + 8u);
        const unsigned n = b3host::load<std::uint16_t>(ls + 0xCu);
        const bool loop = b3host::load<std::uint8_t>(ls + 0xEu) != 0;
        for (unsigned j = 0; j < n; ++j) {
            if ((b3host::load<std::uint8_t>(recs + 10u * j + 3u) & 7u) == 5u) continue;
            const unsigned k = j + 1 < n ? j + 1 : loop ? 0 : n;
            if (k == n) continue;
            out.push_back({point(b3host::load<std::uint16_t>(pairs + 4u * j)), point(b3host::load<std::uint16_t>(pairs + 4u * j + 2u)),
                           point(b3host::load<std::uint16_t>(pairs + 4u * k)), point(b3host::load<std::uint16_t>(pairs + 4u * k + 2u))});
        }
    }
    return out;
}

// The grid's spots: CB3StartPositionStruct, 0x50 bytes; +0x00 a 4 x 4 float matrix (rows q0..q3, as CB3PhysicsManager::
// AddRaceCar copies it; the position in q3), then the stream unit / AI lane fields. The new spots: more_rivals_grid.h
// (validated on the lanes); each a copy of its source spot with the position moved.
inline void MoreRivals::synthesise_grid() {
    using namespace more_rivals_detail;
    const std::vector<GridQuad> lanes = road();
    std::vector<GridSpot> spots(kGameCars);
    b3host::gaddr src[kGameCars];
    for (int i = 0; i < kGameCars; ++i) {
        src[i] = b3host::load<std::uint32_t>(kStartSlots + 4u * i);
        spots[i].pos = {b3host::load<float>(src[i] + 0x30u), b3host::load<float>(src[i] + 0x34u), b3host::load<float>(src[i] + 0x38u)};
    }
    // the heading: the matrix row the lane runs along at spot 0 (signed as the lane), else the row the spots spread on
    auto row = [&](int i, int r) {
        return GridV3{b3host::load<float>(src[i] + 0x10u * r), b3host::load<float>(src[i] + 0x10u * r + 4u), b3host::load<float>(src[i] + 0x10u * r + 8u)};
    };
    GridV3 lane_dir{};
    for (const GridQuad& q : lanes)
        if (grid_in_triangle(spots[0].pos.x, spots[0].pos.z, q.l0, q.r0, q.r1) || grid_in_triangle(spots[0].pos.x, spots[0].pos.z, q.l0, q.r1, q.l1)) {
            lane_dir = q.dir();
            break;
        }
    int best = 2;
    float bestv = -1;
    for (int r = 0; r < 3; ++r) {
        float v = 0;
        if (lane_dir.flat_len() > 0) v = std::fabs(row(0, r).dot(lane_dir));
        else for (int i = 1; i < kGameCars; ++i) v += std::fabs((spots[i].pos - spots[0].pos).dot(row(0, r)));
        if (v > bestv) { bestv = v; best = r; }
    }
    heading_row_ = best;
    const float sign = lane_dir.flat_len() > 0 && row(0, best).dot(lane_dir) < 0 ? -1.0f : 1.0f;
    heading_sign_ = sign;
    for (int i = 0; i < kGameCars; ++i) spots[i].heading = row(i, best) * sign;
    auto on_road = [&](const GridV3& p, const GridV3& h) { return grid_on_road(lanes, p, h); };
    int game_on = 0;
    for (const GridSpot& sp : spots) game_on += on_road(sp.pos, sp.heading);
    std::vector<GridSpot> extra = grid_extra_spots(spots, extra_, on_road);
    std::printf("more_rivals: grid: %zu lane quads; the game's spots on the road: %d of %d; heading row %d%s; new spots %zu of %d\n",
                lanes.size(), game_on, kGameCars, best, sign < 0 ? " (negated)" : "", extra.size(), extra_);
    for (int n = 0; n < extra_; ++n) {
        GridSpot sp;
        bool checked = n < int(extra.size());
        if (checked) sp = extra[n];
        else {   // none on the road: the first candidates as they are (the grid check reports it)
            const std::vector<GridSpot> c = grid_candidates(spots);
            sp = c.empty() ? GridSpot{spots[0].pos - spots[0].heading * (10.0f * float(n + 1)), spots[0].heading, 0} : c[std::size_t(n) % c.size()];
        }
        const b3host::gaddr out = kRegion + 2u * kModelBlock + std::uint32_t(n) * kSpotBytes;
        std::memcpy(b3host::host_pointer(out, kSpotBytes), b3host::host_pointer(src[sp.source], kSpotBytes), kSpotBytes);
        b3host::store<float>(out + 0x30u, sp.pos.x);
        b3host::store<float>(out + 0x34u, sp.pos.y);
        b3host::store<float>(out + 0x38u, sp.pos.z);
        const b3host::gaddr slot = b3host::split::shadow_or_flat(kStartSlots, 4, kGameCars, std::size_t(kGameCars + n));
        b3host::store<std::uint32_t>(slot, out);
        std::printf("more_rivals: spot %d: from spot %d at (%.2f %.2f %.2f)%s\n", kGameCars + n, sp.source, sp.pos.x, sp.pos.y,
                    sp.pos.z, checked ? "" : " UNCHECKED (no candidate on the road)");
    }
    std::fflush(stdout);
}

// TEST (B3NATIVE_MORE_RIVALS_TEST_TRACK=<n> | list): CB3MenuChoices::RequestOnePlayerGameMode 0x30C2C0, RACE: the track
// (+0xE8) the track list's n-th (CB3TrackList 0x005196A0: +4 the count, +0x408 the ids; CreateTrackPathFromIndex 0x251A30
// its path); "list" prints them
inline void MoreRivals::request(const b3host::CallArgs& c, b3host::CallResult& r) {
    using namespace more_rivals_detail;
    MoreRivals& m = *instance();
    const b3host::gaddr mc = a(c, 0);
    const int n = b3host::load<std::int32_t>(kTrackList + 4u);
    if (m.test_track_ == -2 || (m.test_track_ >= 0 && b3host::load<std::int32_t>(mc + 8u) == 0)) {
        if (m.test_track_ == -2) {
            b3host::GuestLocalArray<char, 256> buf;
            for (int i = 0; i < n; ++i) {
                b3host::store<std::uint8_t>(buf.addr(), 0);
                b3host::call_guest<void>(kCreateTrackPath, kTrackList, i, buf.addr(), 256);
                std::printf("more_rivals: TEST: track %d %016llx %s\n", i,
                            static_cast<unsigned long long>(b3host::load<std::uint64_t>(kTrackList + 0x408u + 8u * std::uint32_t(i))),
                            static_cast<const char*>(b3host::host_pointer(buf.addr(), 256)));
            }
        } else if (m.test_track_ < n) {
            b3host::store<std::uint64_t>(mc + 0xE8u, b3host::load<std::uint64_t>(kTrackList + 0x408u + 8u * std::uint32_t(m.test_track_)));
        }
        std::fflush(stdout);
    }
    m.request_next_(c, r);
}

// TEST (B3NATIVE_MORE_RIVALS_TEST_GRID=1): the cars as placed (the player's score's first update after the setup), every car
// on the road (more_rivals_grid.h: in a drivable lane quad, within 2.5 m of its height, the heading row the grid used),
// 3 m from every other; then (information) 300 racing updates in, the cars off the road
inline void MoreRivals::grid_check(bool placed) {
    using namespace more_rivals_detail;
    const std::vector<GridQuad> lanes = road();
    const int n = b3host::load<std::int32_t>(kCarCount);
    std::vector<GridV3> pos(std::size_t(std::max(n, 0)));
    std::string why;
    for (int i = 0; i < n; ++i) {
        const b3host::gaddr car = b3host::load<std::uint32_t>(b3host::split::shadow_or_flat(kCarList, 4, kGameCars, std::size_t(i)));
        pos[i] = {b3host::load<float>(car + 0x30u), b3host::load<float>(car + 0x34u), b3host::load<float>(car + 0x38u)};
        const GridV3 h = GridV3{b3host::load<float>(car + 0x10u * heading_row_), b3host::load<float>(car + 0x10u * heading_row_ + 4u),
                                b3host::load<float>(car + 0x10u * heading_row_ + 8u)} * heading_sign_;
        char b[160];
        if (!grid_on_road(lanes, pos[i], h)) {
            std::snprintf(b, sizeof b, " car %d off the road at (%.1f %.1f %.1f);", i, pos[i].x, pos[i].y, pos[i].z);
            why += b;
        }
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if ((pos[i] - pos[j]).flat_len() < 3.0f) {
                char b[96];
                std::snprintf(b, sizeof b, " cars %d and %d %.1f m apart;", i, j, (pos[i] - pos[j]).flat_len());
                why += b;
            }
    const char* path = static_cast<const char*>(b3host::host_pointer(kStageRecord + 0xACu, 256));
    if (placed) std::printf("more_rivals: grid check %s: %s (%d cars)%s\n", path, why.empty() ? "PASS" : "FAIL", n, why.c_str());
    else std::printf("more_rivals: grid after 300 racing updates %s: %s%s\n", path, why.empty() ? "every car on the road" : "", why.c_str());
    std::fflush(stdout);
}

}  // namespace b3native

#endif
