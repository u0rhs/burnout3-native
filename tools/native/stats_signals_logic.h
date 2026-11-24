// stats_signals_logic.h -- the pure parts of CAR STATS (car_stats.h) and TURN SIGNALS (turn_signals.h), for their tests
// (tests/test_stats_signals.cpp): the VDB key hash, a car's name from its GtID, the stats' formulas, the indicators' side and
// blinking. No game memory here.
#ifndef B3NATIVE_STATS_SIGNALS_LOGIC_H
#define B3NATIVE_STATS_SIGNALS_LOGIC_H

#include <cmath>
#include <cstdint>
#include <string>

namespace b3native {

namespace car_stats_detail {
// CGtHash::CalculateHash 0x0021BFA0 (docs/modding/file-formats/vdb.md): CRC-32's table, start -1, an arithmetic shift
inline std::int32_t gthash(const std::string& s) {
    static std::uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    std::int32_t crc = -1;
    for (unsigned char ch : s) crc = (crc >> 8) ^ std::int32_t(table[(ch ^ std::uint32_t(crc)) & 0xFFu]);
    return crc;
}
// GtIDConvertToString 0x0021AB40: 12 characters in base 40 (' ', '-', '/', 0-9, A-Z, '_'), trailing spaces trimmed
inline std::string gtid_string(std::uint64_t id) {
    char out[13] = {};
    for (int i = 11; i >= 0; --i) {
        const int d = int(id % 40u);
        out[i] = d == 39 ? '_' : d >= 13 ? char('A' + d - 13) : d >= 3 ? char('0' + d - 3) : d == 2 ? '/' : d == 1 ? '-' : ' ';
        id /= 40u;
    }
    std::string s(out);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
// The car's stats from its handling values (each a fraction of the bar; NaN when a value is missing)
inline float acceleration(float torque, float first, float final_gear, float mass) {
    if (!(mass > 0.0f)) return NAN;
    return (torque * first * final_gear / mass - 5.0f) / 9.0f;
}
inline float grip(float downforce, float max_angle) {
    return 0.15f + 0.85f * (0.6f * downforce / 3.0f + 0.4f * (max_angle - 10.0f) / 17.0f);
}
}  // namespace car_stats_detail

namespace turn_signals_detail {
// The signed turn (radians) between two level directions of the game's world: positive turns to the car's right
// (checked on US/C2_V2: its right-hand bend after the start, where CAMERA ROLL leans right too)
inline float turn(float ax, float az, float bx, float bz) { return std::atan2(ax * bz - az * bx, ax * bx + az * bz); }
// The indicator for a lane that turns by `t` radians ahead (0: none). The light bits: 0x40 lights the car's right-hand
// indicators (its light group 6), 0x20 the left (group 5) [seen in the shots: the rear right lamp before that bend]
constexpr std::uint8_t kLeft = 0x20, kRight = 0x40;
constexpr float kTurn = 0.45f;                         // about 26 degrees
inline std::uint8_t side(float t) { return t > kTurn ? kRight : t < -kTurn ? kLeft : 0; }
// Blinking: on for the first half of each period
constexpr float kPeriod = 0.7f;
inline bool lit(float now, int car) {
    const float p = std::fmod(now + 0.17f * float(car), kPeriod);
    return p >= 0.0f && p < 0.5f * kPeriod;
}
}  // namespace turn_signals_detail

}  // namespace b3native

#endif
