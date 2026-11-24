// carstats1 / blinkers1 (tools/native/stats_signals_logic.h): CAR STATS' VDB key hash and car names, its GRIP / ACCELERATION formulas, and
// TURN SIGNALS' side and blinking. (The hooks themselves: race_check SCENARIO=carselect / blinkers / rrparts.)
#include "test.h"

#include "stats_signals_logic.h"

#include <cmath>

using namespace b3native;

// The hash on the two keys docs/modding/file-formats/vdb.md checks (and the empty string)
TEST(vdb_key_hash) {
    using car_stats_detail::gthash;
    CHECK(std::uint32_t(gthash("Minimum Air Distance (Metres)Score/Boost/Air/../Export/ValueDB/Score.cfg")) == 0x58C54868u);
    CHECK(std::uint32_t(gthash("../Export/ValueDB/Score.cfg")) == 0x9169203Eu);
    CHECK(std::uint32_t(gthash("")) == 0xFFFFFFFFu);
}

// A car's GtID (12 characters, base 40) back to its file name, trailing spaces trimmed
TEST(car_names) {
    auto id = [](const char* s) {
        std::uint64_t v = 0;
        for (int i = 0; i < 12; ++i) {
            const char c = *s ? *s++ : ' ';
            const int d = c == '_' ? 39 : c >= 'A' && c <= 'Z' ? 13 + (c - 'A') : c >= '0' && c <= '9' ? 3 + (c - '0') : c == '/' ? 2 : c == '-' ? 1 : 0;
            v = v * 40 + std::uint64_t(d);
        }
        return v;
    };
    CHECK(car_stats_detail::gtid_string(id("COMPCAR1")) == "COMPCAR1");
    CHECK(car_stats_detail::gtid_string(id("HSPCCAR25")) == "HSPCCAR25");
    CHECK(car_stats_detail::gtid_string(0) == "");
}

// The stats: a light, strong car accelerates more; downforce and steering give grip (our own made-up values here)
TEST(stat_formulas) {
    using namespace car_stats_detail;
    CHECK(acceleration(1000.0f, 3.5f, 3.5f, 1000.0f) > acceleration(1000.0f, 3.5f, 3.5f, 1500.0f));
    CHECK(std::fabs(acceleration(1000.0f, 2.0f, 7.0f, 1000.0f) - 1.0f) < 1e-5f);   // 14 -> (14 - 5) / 9
    CHECK(std::isnan(acceleration(1000.0f, 3.0f, 3.0f, 0.0f)));
    CHECK(std::fabs(grip(0.0f, 10.0f) - 0.15f) < 1e-5f && std::fabs(grip(3.0f, 27.0f) - 1.0f) < 1e-5f);
    CHECK(grip(1.0f, 15.0f) > grip(0.5f, 15.0f) && grip(1.0f, 20.0f) > grip(1.0f, 15.0f));
}

// The indicators: a bend past about 26 degrees lights its side; blinking half of each period, each car its own phase
TEST(turn_signal_side) {
    using namespace turn_signals_detail;
    const float pi = 3.14159265f;
    CHECK(side(turn(0, 1, 1, 0)) != 0 && side(turn(0, 1, -1, 0)) != 0 && side(turn(0, 1, 1, 0)) != side(turn(0, 1, -1, 0)));
    CHECK(side(turn(0, 1, std::sin(0.2f), std::cos(0.2f))) == 0);   // a gentle curve: none
    CHECK(side(1.0f) == kRight && side(-1.0f) == kLeft && kRight == 0x40 && kLeft == 0x20);
    CHECK(std::fabs(turn(1, 0, 0, 1) - pi / 2) < 1e-5f || std::fabs(turn(1, 0, 0, 1) + pi / 2) < 1e-5f);
    int on = 0;
    for (int k = 0; k < 700; ++k) on += lit(10.0f + 0.001f * float(k), 0) ? 1 : 0;
    CHECK(on > 330 && on < 370);
    CHECK(lit(10.0f, 0) != lit(10.0f, 2) || lit(10.1f, 0) != lit(10.1f, 2));
}

TEST_MAIN("car stats, turn signals")
