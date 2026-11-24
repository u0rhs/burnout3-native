// turn_signals.h -- TURN SIGNALS (blinkers1, 2026-11-21): the rival racers' indicators blink before a bend, on every
// track (remaster-features §31).
//
// What the game has [C the generated units]: each race car keeps an indicator byte at +0x18EF: CB3RaceCar::Update
// 0x152770 clears it every frame (no retail code sets it; Prepare clears it too), and
// CB3RaceCarPhysics::Update ORs it into the car's light bits (EssentialParams +0x58) with the brake (0x10) and reverse
// (0x80) lights. The lights draw from those bits (func_00257080 [C asm]; the port's coronas.cpp): 0x20 lights the car's
// light group 5, 0x40 group 6 -- the indicators (left / right), still in every car's light table.
//
// Ours: a port wrapper on CB3RaceCar::Update that, after the game's update, for each AI racer (+0x1914 == 1) with its
// body on (+0x210 == 0), looks along its own AI lane (+0x18B4 the CB3AILane, +0x18B8 its segment; the lane's point pairs
// as lane_pair_fix.h reads them) from 15 to kAhead metres on: when the lane turns more than kTurn from the car's
// segment there, the indicator on that side blinks (kPeriod, half on; each car's phase its own) until the bend is
// passed. Memory reads only: no guest call, so the game's timing is the game's. The byte feeds the lights alone; the
// race is the game's (records kept).
//
// The row (CAR > CAR LOOK > TURN SIGNALS, default Off) is a live setting latched for replays ("turn_signals").
// B3NATIVE_TURN_SIGNALS=0: not installed.
#ifndef B3NATIVE_TURN_SIGNALS_H
#define B3NATIVE_TURN_SIGNALS_H

#include "b3host/call.h"
#include "b3host/host.h"
#include "stats_signals_logic.h"
#include "hook_table.h"
#include "platform.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace b3native {


class TurnSignals {
public:
    static constexpr float kNear = 15.0f, kAhead = 70.0f;   // the stretch of lane looked at (m)
    static bool requested() {
        const char* e = std::getenv("B3NATIVE_TURN_SIGNALS");
        return !(e && e[0] == '0' && e[1] == 0);
    }
    bool install(HookTable& hooks, bool (*on)()) {
        on_ = on;
        if (!hooks.has_row(kCarUpdate)) return false;
        std::string error;
        std::vector<Hook> set = {{kCarUpdate, HookLayer::kPort, &car_update, &next_}};
        if (!hooks.install("port:turn_signals", set, &error)) {
            std::fprintf(stderr, "turn_signals: %s: TURN SIGNALS left as retail\n", error.c_str());
            return false;
        }
        instance() = this;
        return true;
    }
    std::uint64_t blinks() const { return blinks_; }

private:
    using Entry = void (*)(const b3host::CallArgs&, b3host::CallResult&);
    static constexpr b3host::gaddr kCarUpdate = 0x00152770u;   // CB3RaceCar::Update(car)
    static constexpr b3host::gaddr kPoints = 0x0064D354u;      // the lanes' points (CGtV3d, 16 bytes each)
    static constexpr b3host::gaddr kNow = 0x00522680u;         // the game's timer (gWorld +0x20)
    static TurnSignals*& instance() { static TurnSignals* p = nullptr; return p; }

    static void car_update(const b3host::CallArgs& a, b3host::CallResult& r) {
        TurnSignals* p = instance();
        p->next_(a, r);
        if (p->on_ && p->on_()) p->signal(a.gpr[0].w[0] & 0x01FFFFFFu);
    }
    // The level midpoint of the lane's point pair `s`
    static bool mid(b3host::gaddr pairs, b3host::gaddr points, unsigned s, float& x, float& z) {
        using b3host::load;
        const std::uint16_t i = load<std::uint16_t>(pairs + 4u * s), j = load<std::uint16_t>(pairs + 4u * s + 2u);
        if (i >= 0x8000u || j >= 0x8000u) return false;
        x = 0.5f * (load<float>(points + 16u * i) + load<float>(points + 16u * j));
        z = 0.5f * (load<float>(points + 16u * i + 8u) + load<float>(points + 16u * j + 8u));
        return std::isfinite(x) && std::isfinite(z);
    }
    void signal(b3host::gaddr car) {
        using namespace turn_signals_detail;
        using b3host::load;
        // checks only: B3_TURN_SIGNALS_TEST_PLAYER=1 gives player 1's car (+0x1914 0) the indicators too, to see them up close
        static const bool test_player = [] { const char* e = std::getenv("B3_TURN_SIGNALS_TEST_PLAYER"); return e && *e == '1'; }();
        if (car == 0 || load<std::uint8_t>(car + 0x210u) != 0) return;
        const std::int32_t who = load<std::int32_t>(car + 0x1914u);
        if (who != 1 && !(test_player && who == 0 && load<std::int32_t>(car + 0x27C0u) == 0)) return;
        const b3host::gaddr lane = load<std::uint32_t>(car + 0x18B4u);
        const b3host::gaddr points = load<std::uint32_t>(kPoints);
        if (lane == 0 || points == 0) return;
        const b3host::gaddr ls = load<std::uint32_t>(lane + 4u);
        if (ls == 0) return;
        const b3host::gaddr pairs = load<std::uint32_t>(ls);
        const unsigned n = load<std::uint16_t>(ls + 0xCu);
        const unsigned seg = load<std::uint16_t>(car + 0x18B8u);
        if (pairs == 0 || n < 3 || seg + 1 >= n) return;
        float x0, z0, x1, z1;
        if (!mid(pairs, points, seg, x0, z0) || !mid(pairs, points, seg + 1, x1, z1)) return;
        const float hx = x1 - x0, hz = z1 - z0;
        if (hx * hx + hz * hz < 1e-4f) return;
        float dist = 0, px = x1, pz = z1, most = 0;
        for (unsigned s = seg + 2; s < n && dist < kAhead; ++s) {
            float x, z;
            if (!mid(pairs, points, s, x, z)) break;
            const float dx = x - px, dz = z - pz, d = std::sqrt(dx * dx + dz * dz);
            dist += d;
            if (dist >= kNear && d > 1e-3f) {
                const float t = turn(hx, hz, dx, dz);
                if (std::fabs(t) > std::fabs(most)) most = t;
            }
            px = x;
            pz = z;
        }
        const std::uint8_t bit = side(most);
        if (!bit) return;
        const int index = load<std::int8_t>(car + 0x19ACu);
        if (!lit(load<float>(kNow), index)) return;
        b3host::store<std::uint8_t>(car + 0x18EFu, std::uint8_t(load<std::uint8_t>(car + 0x18EFu) | bit));
        static const bool log = [] { const char* e = std::getenv("B3_TURN_SIGNALS_LOG"); return e && *e == '1'; }();
        if (log && who == 0)
            std::printf("turn_signals: VBlank %llu: player 1 at segment %u: %s (%.0f degrees ahead)\n",
                        static_cast<unsigned long long>(platform::vblank_count()), seg, bit == kLeft ? "left" : "right", double(most * 57.29578f));
        if (blinks_++ == 0) {
            std::printf("turn_signals: a racer's indicator before a bend (car %d, segment %u, %.0f degrees ahead)\n", index, seg,
                        double(most * 57.29578f));
            std::fflush(stdout);
        }
    }

    bool (*on_)() = nullptr;
    Entry next_ = nullptr;
    std::uint64_t blinks_ = 0;
};

}  // namespace b3native

#endif
