// car_stats.h -- CAR STATS (carstats1, 2026-11-21): SELECT CAR shows GRIP and ACCELERATION bars beside the game's TOP
// SPEED and WEIGHT, worked out from each car's handling values (remaster-features §31).
//
// What the game has [C the generated units]: the stats component (its object passed to all four functions below)
// builds two stats in Prepare func_00435100: per stat a text object (layer +0x8: func_0033D650(obj, pos, D_004D1A48, 20,
// colour, font D_00665ED0 +8, the game's string 0x183 TOP SPEED / 0x185 WEIGHT)), a bar background and a bar fill (layers
// +0x9 / +0xA: CGt2dObjectImage::Prepare(obj, pos, D_004D1A50, D_004D1A58 x D_004D1AA0, colour D_004D1AD0 / D_004D1AC0, the
// "CarArrow" texture, uv D_004D1A80 .. D_004D1A88, 0)), at x = the column (D_01ECDD90[i].x: 0.481 / 0.789) x D_004D1AA0.x
// from D_004D1A90 (text) / D_004D1A98 (bars). func_00434FF0 sets the targets from the car viewer's values for the car on
// show (slot D_01E8EBDE: its Max Speed In Boost (v - 140) / 65 and Mass (Kg) (v - 500) / 2000, at least D_004E0924);
// func_00434DD0(dt) moves each fill 3 x dt towards its target and sizes it (width in tenths, uv to match); Release
// func_00434BF0 destroys the objects. The game's text has two more labels no retail page shows: 0x184 GRIP (between
// TOP SPEED and WEIGHT) and 0xA1C ACCELERATION, in every language file.
//
// Ours (port wrappers, the row on): after the game's Prepare, two more stats built the same way on the same layers and
// frame, in one row with the game's: GRIP and ACCELERATION first, the game's TOP SPEED and WEIGHT moved right to fit
// (its columns D_01ECDD90 written before its Prepare; back to its own 0.481 / 0.789 with the row off); after its target
// step, their targets from the car's own handling values in VDB.XML (read in the copy the game loaded, at the tools
// base + 0xE540: memory reads, the file's key hash; the car's name from its GtID): ACCELERATION from Torque x 1st Gear x
// Final Gear / Mass (Kg) (the cars span about 5.8 .. 13.7: (x - 5) / 9), GRIP from Downforce Coef (0 .. 3) and the
// steering's Max Angle (10 .. 27 degrees): 0.15 + 0.85 x (0.6 df / 3 + 0.4 (angle - 10) / 17); both at least the game's
// floor and at most 1; after its update, ours moved and sized as its own; before its Release, ours destroyed.
//
// Front end only: no race reads these objects. The game's guest RAM differs while the row is on (the objects, the
// calls), so the golden record pins it off (tools/golden/baseline.sh, B3NATIVE_CAR_STATS=0 there). The row is a live
// setting latched for replays ("car_stats"). B3NATIVE_CAR_STATS=0: not installed.
#ifndef B3NATIVE_CAR_STATS_H
#define B3NATIVE_CAR_STATS_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "stats_signals_logic.h"
#include "hook_table.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace b3native {


class CarStats {
public:
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_CAR_STATS");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    bool install(HookTable& hooks, bool (*on)());

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    struct V2 { float x, y; };
    struct V4 { float x, y, z, w; };
    struct Uv { float x, y, w, h; };
    static constexpr b3host::gaddr kPrepare = 0x00435100u, kRelease = 0x00434BF0u, kUpdate = 0x00434DD0u, kTargets = 0x00434FF0u;
    static constexpr b3host::gaddr kScene = 0x01E903E0u;
    static constexpr b3host::gaddr kConstruct = 0x001D7580u, kDestruct = 0x001D7560u;   // CGt2dScene::Construct/DestructObject
    static constexpr b3host::gaddr kText = 0x0033D650u, kImage = 0x001D60B0u, kSetUVs = 0x001D6080u;
    static constexpr b3host::gaddr kString = 0x00271A00u, kStrings = 0x0051BA44u;
    static constexpr b3host::gaddr kColumns = 0x01ECDD90u, kTextColour = 0x01E90240u, kFont = 0x00665ED8u, kTextures = 0x0051A868u;
    static constexpr b3host::gaddr kA48 = 0x004D1A48u, kA50 = 0x004D1A50u, kA58 = 0x004D1A58u, kA80 = 0x004D1A80u, kA88 = 0x004D1A88u,
                                   kA90 = 0x004D1A90u, kA98 = 0x004D1A98u, kAA0 = 0x004D1AA0u, kFill = 0x004D1AC0u, kBack = 0x004D1AD0u,
                                   kTextureName = 0x004D1AE0u, kFloor = 0x004E0924u;
    static constexpr b3host::gaddr kShown = 0x01E8EBDEu, kViewerState = 0x01E8EBD4u;   // the car viewer's slot on show
    static constexpr b3host::gaddr kCarIds = 0x00522660u + 0x131650u;                   // its two cars' GtIDs, 0x2460 apart
    static constexpr b3host::gaddr kToolBase = 0x004E2920u;                             // the VDB.XML copy at +0xE540
    static constexpr int kGrip = 0x184, kAcceleration = 0xA1C;   // the game's string ids
    // the row's four columns (fractions of D_004D1AA0.x from D_004D1A90 / D_004D1A98, as the game's D_01ECDD90): GRIP,
    // ACCELERATION, then the game's TOP SPEED and WEIGHT moved right a little (from 0.481 / 0.789) so the four fit
    static constexpr float kColumns4[4] = {-0.02f, 0.25f, 0.52f, 0.80f};
    static constexpr float kGameColumns[2] = {0.481f, 0.789f};   // the component's own (its Construct func_00435620)

    static CarStats*& instance() { static CarStats* p = nullptr; return p; }
    static void prepare(const b3host::CallArgs& a, b3host::CallResult& r);
    static void release(const b3host::CallArgs& a, b3host::CallResult& r);
    static void update(const b3host::CallArgs& a, b3host::CallResult& r);
    static void targets(const b3host::CallArgs& a, b3host::CallResult& r);
    void build(b3host::gaddr self);
    // The front-end scene's object pool (CGt2dScene +0x18, a CGtFreeListPool [C decomp GtFreeList units]: +8 the count,
    // +0x10 the free links, each link's +0 the next): one pool for all its layers. Ours are built only with at least
    // kPoolHeadroom more free than they take, so the game's own later objects on the page still find room.
    static constexpr int kPoolHeadroom = 16;
    static int pool_count() { return int(b3host::load<std::uint32_t>(kScene + 0x18u + 8u)); }
    static int pool_free() {
        const int n = pool_count();
        int free = 0;
        for (b3host::gaddr l = b3host::load<std::uint32_t>(kScene + 0x18u + 0x10u) & 0x01FFFFFFu; l != 0 && free <= n;
             l = b3host::load<std::uint32_t>(l) & 0x01FFFFFFu)
            ++free;
        return free;
    }
    void destroy(b3host::gaddr self);
    void size_fills(float dt);
    float vdb_value(const std::string& name, const std::string& group, const std::string& file, bool* found);
    b3host::gaddr texture();

    bool (*on_)() = nullptr;
    Entry prepare_ = nullptr, release_ = nullptr, update_ = nullptr, targets_ = nullptr;
    b3host::gaddr self_ = 0, text_[2] = {}, back_[2] = {}, fill_[2] = {};
    float cur_[2] = {0.1f, 0.1f}, target_[2] = {0.1f, 0.1f};
    std::uint64_t shown_id_ = ~0ull;
    int free_least_ = 1 << 30;   // the pool's fewest free links seen while ours are up
    bool logged_ = false;
};

inline bool CarStats::install(HookTable& hooks, bool (*on)()) {
    on_ = on;
    const std::vector<Hook> set = {{kPrepare, HookLayer::kPort, &prepare, &prepare_},
                                   {kRelease, HookLayer::kPort, &release, &release_},
                                   {kUpdate, HookLayer::kPort, &update, &update_},
                                   {kTargets, HookLayer::kPort, &targets, &targets_}};
    for (const Hook& h : set)
        if (!hooks.has_row(h.address)) {
            std::fprintf(stderr, "car_stats: %08X is not native: CAR STATS left as retail\n", h.address);
            return false;
        }
    std::string error;
    if (!hooks.install("port:car_stats", set, &error)) {
        std::fprintf(stderr, "car_stats: %s: CAR STATS left as retail\n", error.c_str());
        return false;
    }
    instance() = this;
    return true;
}

// The bar texture as the game's Prepare finds it (the texture list D_0051A868: entries 0x10 apart, the object at +8, its
// name at +0x90 +0x18; compared ignoring case with "CarArrow")
inline b3host::gaddr CarStats::texture() {
    using b3host::load;
    const b3host::gaddr list = load<std::uint32_t>(kTextures);
    if (!list) return 0;
    char want[16] = {};
    for (int i = 0; i < 15; ++i) want[i] = char(load<std::uint8_t>(kTextureName + std::uint32_t(i)));
    const int n = load<std::int32_t>(list + 8u);
    const b3host::gaddr arr = load<std::uint32_t>(list + 0xCu);
    for (int i = 0; i < n; ++i) {
        const b3host::gaddr t = load<std::uint32_t>(arr + 0x10u * std::uint32_t(i) + 8u);
        bool same = true;
        for (int k = 0; k < 16; ++k) {
            const char c = char(load<std::uint8_t>(t + 0x90u + 0x18u + std::uint32_t(k)));
            if (std::tolower(static_cast<unsigned char>(c)) != std::tolower(static_cast<unsigned char>(want[k]))) { same = false; break; }
            if (c == 0) break;
        }
        if (same) return t;
    }
    return 0;
}

inline void CarStats::build(b3host::gaddr self) {
    using b3host::call_guest;
    using b3host::load;
    const int free = pool_free();
    if (free < 6 + kPoolHeadroom) {
        std::printf("car_stats: the front-end scene's object pool has %d of %d free: GRIP / ACCELERATION not built this time\n",
                    free, pool_count());
        std::fflush(stdout);
        return;
    }
    free_least_ = free - 6;
    const std::int16_t layer = load<std::int16_t>(self + 8u);
    const b3host::gaddr frame = load<std::uint32_t>(self + 0x34u);
    const float aa0x = load<float>(kAA0), aa0y = load<float>(kAA0 + 4u);
    b3host::GuestLocal<V4> colour;
    for (int k = 0; k < 4; ++k) (&(*colour).x)[k] = load<float>(kTextColour + 4u * std::uint32_t(k));
    b3host::GuestLocal<V2> size;
    (*size).x = load<float>(kA58) * aa0x;
    (*size).y = load<float>(kA58 + 4u) * aa0y;
    const b3host::gaddr tex = texture();
    for (int i = 0; i < 2; ++i) {
        const float col = kColumns4[i];
        text_[i] = call_guest<std::uint32_t>(kConstruct, kScene, std::int32_t(layer), frame);
        back_[i] = call_guest<std::uint32_t>(kConstruct, kScene, std::int32_t(layer + 1), frame);
        fill_[i] = call_guest<std::uint32_t>(kConstruct, kScene, std::int32_t(layer + 2), frame);
        b3host::GuestLocal<V2> tpos, bpos;
        (*tpos).x = load<float>(kA90) + col * aa0x;
        (*tpos).y = load<float>(kA90 + 4u);
        (*bpos).x = load<float>(kA98) + col * aa0x;
        (*bpos).y = load<float>(kA98 + 4u);
        const b3host::gaddr label = call_guest<std::uint32_t>(kString, kStrings, i == 0 ? kGrip : kAcceleration);
        if (text_[i]) call_guest<void>(kText, text_[i], tpos.addr(), kA48, 20.0f, colour.addr(), load<std::uint32_t>(kFont), label);
        if (back_[i]) call_guest<void>(kImage, back_[i], bpos.addr(), kA50, size.addr(), kBack, tex, kA80, kA88, 0u);
        if (fill_[i]) call_guest<void>(kImage, fill_[i], bpos.addr(), kA50, size.addr(), kFill, tex, kA80, kA88, 0u);
        cur_[i] = target_[i] = load<float>(kFloor);
    }
    self_ = self;
    if (!logged_) {
        logged_ = true;
        std::printf("car_stats: SELECT CAR's GRIP and ACCELERATION bars (objects %08X %08X / %08X %08X; the game's text at %.3f %.3f, bars at "
                    "%.3f %.3f, scale %.3f %.3f, bar %.3f %.3f; four columns)\n", text_[0], fill_[0], text_[1], fill_[1],
                    double(load<float>(kA90)), double(load<float>(kA90 + 4u)), double(load<float>(kA98)), double(load<float>(kA98 + 4u)),
                    double(load<float>(kAA0)), double(load<float>(kAA0 + 4u)), double(load<float>(kA58)), double(load<float>(kA58 + 4u)));
        std::fflush(stdout);
    }
}

inline void CarStats::destroy(b3host::gaddr self) {
    using b3host::call_guest;
    std::printf("car_stats: SELECT CAR left: the front-end scene's object pool (%d) had at least %d free while the bars were up\n",
                pool_count(), std::min(free_least_, pool_free()));
    std::fflush(stdout);
    free_least_ = 1 << 30;
    const std::int16_t layer = b3host::load<std::int16_t>(self + 8u);
    for (int i = 0; i < 2; ++i) {
        if (text_[i]) call_guest<void>(kDestruct, kScene, std::int32_t(layer), text_[i]);
        if (back_[i]) call_guest<void>(kDestruct, kScene, std::int32_t(layer + 1), back_[i]);
        if (fill_[i]) call_guest<void>(kDestruct, kScene, std::int32_t(layer + 2), fill_[i]);
        text_[i] = back_[i] = fill_[i] = 0;
    }
    self_ = 0;
    shown_id_ = ~0ull;
}

// func_00434DD0's sizing of a fill, for ours: the width in tenths of the bar, the uv to match
inline void CarStats::size_fills(float dt) {
    using b3host::load;
    const float step = 3.0f * dt;
    for (int i = 0; i < 2; ++i) {
        float& c = cur_[i];
        const float t = target_[i];
        if (!(t <= c)) c = std::min(c + step, t);
        else if (t < c) c = std::max(c - step, 0.1f);
        if (!fill_[i]) continue;
        const float q = 0.1f * float(int(10.0f * (0.0001f + c)));
        const float sx = q * (load<float>(kAA0) * load<float>(kA58)), sy = load<float>(kAA0 + 4u) * load<float>(kA58 + 4u);
        b3host::store<float>(fill_[i] + 8u, sx);
        b3host::store<float>(fill_[i] + 0xCu, sy);
        b3host::GuestLocal<Uv> uv;
        (*uv).x = load<float>(kA80) + q * (load<float>(kA88) - load<float>(kA80));
        (*uv).y = load<float>(kA88 + 4u);
        (*uv).w = sx;
        (*uv).h = sy;
        b3host::call_guest<void>(kSetUVs, fill_[i], kA80, uv.addr());
    }
}

inline float CarStats::vdb_value(const std::string& name, const std::string& group, const std::string& file, bool* found) {
    using b3host::load;
    const b3host::gaddr base = load<std::uint32_t>(kToolBase);
    *found = false;
    if (!base) return 0.0f;
    const b3host::gaddr vdb = base + 0xE540u;
    if (load<std::uint32_t>(vdb) != 2u) return 0.0f;   // the file's version
    const std::uint32_t n = load<std::uint32_t>(vdb + 4u);
    if (n == 0 || n > 200000u) return 0.0f;
    const std::int32_t key = car_stats_detail::gthash(name + group + "/" + file);
    std::uint32_t lo = 0, hi = n;
    while (lo < hi) {
        const std::uint32_t mid = lo + (hi - lo) / 2;
        if (load<std::int32_t>(vdb + 0x14u + 8u * mid + 4u) < key) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= n || load<std::int32_t>(vdb + 0x14u + 8u * lo + 4u) != key) return 0.0f;
    *found = true;
    return load<float>(vdb + 0x14u + 8u * lo);
}

inline void CarStats::prepare(const b3host::CallArgs& a, b3host::CallResult& r) {
    CarStats* p = instance();
    // the game's two columns: moved right with the row on, the component's own otherwise (written only when they differ)
    const bool on = p->on_ && p->on_();
    for (int i = 0; i < 2; ++i) {
        const float want = on ? kColumns4[2 + i] : kGameColumns[i];
        if (b3host::load<float>(kColumns + 8u * std::uint32_t(i)) != want) b3host::store<float>(kColumns + 8u * std::uint32_t(i), want);
    }
    p->prepare_(a, r);
    if (p->on_ && p->on_() && !p->self_) p->build(a.gpr[0].w[0] & 0x01FFFFFFu);
}

inline void CarStats::release(const b3host::CallArgs& a, b3host::CallResult& r) {
    CarStats* p = instance();
    const b3host::gaddr self = a.gpr[0].w[0] & 0x01FFFFFFu;
    if (p->self_ && p->self_ == self) p->destroy(self);
    p->release_(a, r);
}

inline void CarStats::update(const b3host::CallArgs& a, b3host::CallResult& r) {
    CarStats* p = instance();
    p->update_(a, r);
    if (p->self_ && p->self_ == (a.gpr[0].w[0] & 0x01FFFFFFu)) {
        float dt;
        const std::uint32_t bits = a.fpr[0];   // (f12)
        std::memcpy(&dt, &bits, 4);
        p->size_fills(dt);
    }
}

inline void CarStats::targets(const b3host::CallArgs& a, b3host::CallResult& r) {
    using namespace car_stats_detail;
    using b3host::load;
    CarStats* p = instance();
    p->targets_(a, r);
    if (!p->self_ || p->self_ != (a.gpr[0].w[0] & 0x01FFFFFFu)) return;
    p->free_least_ = std::min(p->free_least_, pool_free());
    const float floor = load<float>(kFloor);
    const int slot = load<std::int8_t>(kShown);
    if (slot < 0 || slot > 1 || load<std::int32_t>(kViewerState) == 1) {
        p->target_[0] = p->target_[1] = floor;
        return;
    }
    const std::uint64_t id = load<std::uint64_t>(kCarIds + 0x2460u * std::uint32_t(slot));
    if (id == p->shown_id_) return;
    p->shown_id_ = id;
    const std::string car = gtid_string(id);
    const std::string file = "../Export/ValueDB/VehiclePhysics/" + car + ".cfg";
    bool f[6];
    const float torque = p->vdb_value("Torque", "Physics/Transmission/Engine", file, &f[0]);
    const float first = p->vdb_value("1st Gear", "Physics/Transmission/Gear Ratios", file, &f[1]);
    const float fin = p->vdb_value("Final Gear", "Physics/Transmission/Gear Ratios", file, &f[2]);
    const float mass = p->vdb_value("Mass (Kg)", "Physics/Vehicle", file, &f[3]);
    const float df = p->vdb_value("Downforce Coef", "Physics/Race Car/Misc", file, &f[4]);
    const float angle = p->vdb_value("Max Angle (degrees)", "Physics/Steering", file, &f[5]);
    const float g = f[4] && f[5] ? grip(df, angle) : NAN, ac = f[0] && f[1] && f[2] && f[3] ? acceleration(torque, first, fin, mass) : NAN;
    p->target_[0] = std::isfinite(g) ? std::clamp(g, floor, 1.0f) : floor;
    p->target_[1] = std::isfinite(ac) ? std::clamp(ac, floor, 1.0f) : floor;
    std::printf("car_stats: %s: GRIP %.2f, ACCELERATION %.2f\n", car.c_str(), double(p->target_[0]), double(p->target_[1]));
    std::fflush(stdout);
}

}  // namespace b3native

#endif
