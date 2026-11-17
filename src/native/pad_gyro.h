// pad_gyro.h -- gyro steering (pad1, 2026-11-13; remaster-features §5 "Modern controller features", docs/pc-settings.md
// §7.12). Pure (no SDL): src/b3replay/nativepad.cpp feeds it the pads' SDL sensor events (input_keys_sdl.h
// feed_gyro_event), tests/test_pad_motion.cpp synthetic ones.
//
// OPT-IN and INPUT-CHANGING (per player, controls.ini [playerN.gyro], off by default): while driving, the pad's tilt or
// turn gives a steering value -1..1 that the player's report takes in place of the left stick's X or added to it
// (input_bindings.cpp gamepad_sample). It is part of the report the game reads, so pad recordings and replay files keep
// the steering that resulted and play it back exactly; with gyro off the report is the one before, byte for byte.
//
// SDL's sensor axes for a pad held in front of you (SDL_sensor.h): +X right, +Y up, +Z towards you. The gyroscope gives
// rad/s, counter-clockwise positive as seen from the axis' positive end (values: pitch x, yaw y, roll z); the
// accelerometer m/s^2 with gravity, i.e. +g up at rest.
//   Roll (tilt, like a steering wheel): the pad's sideways tilt against gravity, atan2(a_x, hypot(a_y, a_z)) -- the same
//     for a pad held upright or flat in the lap -- smoothed with the gyroscope by a complementary filter (time constant
//     kRollTau): the angle follows the gyro's rate about the tilt axis, (w_z a_y - w_y a_z) / hypot(a_y, a_z), and is
//     drawn to the accelerometer's angle, so it never drifts. Level is straight; RECENTRE makes the pad's present tilt
//     straight. Without an accelerometer: the gyro's roll rate, integrated.
//   Yaw (turn, like aiming): the rate about gravity (w . a / |a|; the gyro's y without an accelerometer), integrated.
//     There is no absolute reference, so it is zeroed when driving starts and by RECENTRE.
//   Counter-clockwise (the pad's left side down, or turned left) steers left: the stick's X negative.
// steer(): the angle in degrees past the dead zone, rescaled so that full lock (kRollFullLock / kYawFullLock at 100 %
// sensitivity, divided by sensitivity / 100) is full steering.
#ifndef B3NATIVE_PAD_GYRO_H
#define B3NATIVE_PAD_GYRO_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace b3native::pad {

enum class GyroMode : std::uint8_t { Off = 0, Mix = 1, Replace = 2 };   // with the stick's steering / instead of it
enum class GyroAxis : std::uint8_t { Roll = 0, Yaw = 1 };

constexpr float kRollFullLock = 40.0f;   // degrees of tilt for full steering at 100 %
constexpr float kYawFullLock = 30.0f;    // degrees of turn
constexpr float kRollTau = 0.25f;        // seconds: the accelerometer's pull on the roll angle
constexpr int kSensitivityMin = 25, kSensitivityMax = 300, kSensitivityStep = 25, kSensitivityDefault = 100;
constexpr int kDeadZoneMax = 10, kDeadZoneDefault = 2;   // degrees
constexpr float kPi = 3.14159265358979f;

inline float wrap_pi(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

// The steering from an angle (radians, counter-clockwise positive): -1..1, right positive
inline float gyro_steer_of(float angle, GyroAxis axis, int sensitivity, int dead_zone) {
    const float deg = -angle * 180.0f / kPi;
    const float dz = float(std::clamp(dead_zone, 0, kDeadZoneMax));
    const float sens = float(std::clamp(sensitivity, kSensitivityMin, kSensitivityMax)) / 100.0f;
    const float full = std::max((axis == GyroAxis::Roll ? kRollFullLock : kYawFullLock) / sens, dz + 1.0f);
    const float m = std::clamp((std::fabs(deg) - dz) / (full - dz), 0.0f, 1.0f);
    return deg < 0 ? -m : m;
}

class GyroSteer {
public:
    // A gyroscope sample (rad/s, SDL's axes) at t_ns (the sensor's timestamp, nanoseconds)
    void gyro(const float w[3], std::uint64_t t_ns) {
        float dt = 0;
        if (have_gyro_ && t_ns > last_gyro_ns_) dt = std::min(float(double(t_ns - last_gyro_ns_) * 1e-9), 0.05f);
        have_gyro_ = true;
        last_gyro_ns_ = t_ns;
        if (have_accel_) {
            const float h = std::hypot(a_[1], a_[2]);
            const float n = std::sqrt(a_[0] * a_[0] + a_[1] * a_[1] + a_[2] * a_[2]);
            const float roll_rate = h > 1e-3f ? (w[2] * a_[1] - w[1] * a_[2]) / h : w[2];
            const float tilt = std::atan2(a_[0], h);
            if (!roll_init_) { roll_ = tilt; roll_init_ = true; }
            const float alpha = kRollTau / (kRollTau + dt);   // roll = alpha (roll + rate dt) + (1 - alpha) tilt
            roll_ = wrap_pi(tilt + alpha * wrap_pi(roll_ + roll_rate * dt - tilt));
            yaw_ += (n > 1e-3f ? (w[0] * a_[0] + w[1] * a_[1] + w[2] * a_[2]) / n : w[1]) * dt;
        } else {
            roll_ = wrap_pi(roll_ + w[2] * dt);
            roll_init_ = true;
            yaw_ += w[1] * dt;
        }
    }
    // An accelerometer sample (m/s^2, SDL's axes)
    void accel(const float a[3]) {
        if (!(a[0] * a[0] + a[1] * a[1] + a[2] * a[2] > 1.0f)) return;   // free fall / no reading: keep the last
        a_[0] = a[0];
        a_[1] = a[1];
        a_[2] = a[2];
        // no gyroscope (yet): the tilt is the accelerometer's alone; with one, its samples run the filter (gyro())
        if (!have_gyro_ || !roll_init_) { roll_ = std::atan2(a_[0], std::hypot(a_[1], a_[2])); roll_init_ = true; }
        have_accel_ = true;
    }
    // The pad's present pose is straight
    void recentre() {
        roll_centre_ = roll_;
        yaw_ = 0;
    }
    void zero_yaw() { yaw_ = 0; }
    void reset() { *this = GyroSteer{}; }
    // The angle from straight (radians, counter-clockwise positive)
    float angle(GyroAxis axis) const { return axis == GyroAxis::Roll ? wrap_pi(roll_ - roll_centre_) : yaw_; }
    float steer(GyroAxis axis, int sensitivity, int dead_zone) const { return gyro_steer_of(angle(axis), axis, sensitivity, dead_zone); }
    bool has_data() const { return have_gyro_ || have_accel_; }

private:
    bool have_gyro_ = false, have_accel_ = false, roll_init_ = false;
    std::uint64_t last_gyro_ns_ = 0;
    float a_[3] = {0, 9.80665f, 0};
    float roll_ = 0, roll_centre_ = 0, yaw_ = 0;
};

}  // namespace b3native::pad

#endif
