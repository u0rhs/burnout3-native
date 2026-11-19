// pad1: gyro steering (src/native/pad_gyro.h, input_bindings.h apply_gyro / GyroControls, input_keys_sdl.h
// feed_gyro_event) and DualSense adaptive triggers (src/native/pad_triggers.h), on synthetic SDL sensor events (SDL's
// headers only: no SDL library, no pad): the steering from tilt and turn, the dead zone, sensitivity, recentring, the
// stick values the report gets (mix / replace), off and not driving leaving the report as it was; the DS5 effect block's
// bytes and the effects over a race (brake / accelerator / boost / crash kick / pause).
#include "input_keys_sdl.h"
#include "pad_triggers.h"
#include "test.h"

#include <cmath>
#include <random>

using namespace b3native::input;
using namespace b3native::pad;

namespace {
constexpr float kG = 9.80665f;
float rad(float deg) { return deg * kPi / 180.0f; }
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

SDL_Event sensor(SDL_SensorType type, float x, float y, float z, std::uint64_t t_ns) {
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE;
    e.gsensor.which = 7;
    e.gsensor.sensor = type;
    e.gsensor.data[0] = x;
    e.gsensor.data[1] = y;
    e.gsensor.data[2] = z;
    e.gsensor.timestamp = t_ns + 123;
    e.gsensor.sensor_timestamp = t_ns;
    return e;
}
// A pad held upright and turned counter-clockwise (as seen by the player) by `deg`: gravity in its axes
SDL_Event upright_tilt(float deg, std::uint64_t t) { return sensor(SDL_SENSOR_ACCEL, kG * std::sin(rad(deg)), kG * std::cos(rad(deg)), 0, t); }
// A pad lying flat (face up) with its left side down by `deg`
SDL_Event flat_tilt(float deg, std::uint64_t t) { return sensor(SDL_SENSOR_ACCEL, kG * std::sin(rad(deg)), 0, kG * std::cos(rad(deg)), t); }

GyroControls gyro_on(std::uint8_t mode, std::uint8_t axis = 0) {
    GyroControls g;
    g.mode = mode;
    g.axis = axis;
    return g;
}
std::array<std::uint8_t, 32> report(GamepadState s) { return gamepad_sample(s, default_player(0).pad); }   // the 1.x layout
}  // namespace

// ---- the angle and the steering ----

TEST(roll_from_the_accelerometer) {
    for (float deg : {0.0f, 5.0f, -12.5f, 20.0f, 39.0f, -60.0f}) {
        GyroSteer a, b;
        feed_gyro_event(a, upright_tilt(deg, 1000));
        feed_gyro_event(b, flat_tilt(deg, 1000));
        CHECK(near(a.angle(GyroAxis::Roll), rad(deg)));   // held upright or flat in the lap: the same tilt
        CHECK(near(b.angle(GyroAxis::Roll), rad(deg)));
    }
    // counter-clockwise (left side down) steers left; 100 %: full lock at 40 degrees, the dead zone (2) rescaled away
    CHECK(near(gyro_steer_of(rad(20), GyroAxis::Roll, 100, 2), -(20.0f - 2) / (40.0f - 2)));
    CHECK(near(gyro_steer_of(rad(-20), GyroAxis::Roll, 100, 2), (20.0f - 2) / (40.0f - 2)));
    CHECK(near(gyro_steer_of(rad(-40), GyroAxis::Roll, 100, 2), 1.0f));
    CHECK(gyro_steer_of(rad(-75), GyroAxis::Roll, 100, 2) == 1.0f);   // past full lock: full
    CHECK(gyro_steer_of(rad(90), GyroAxis::Roll, 100, 0) == -1.0f);
}

TEST(dead_zone_and_sensitivity) {
    for (int dz = 0; dz <= kDeadZoneMax; ++dz) {
        CHECK(gyro_steer_of(rad(float(dz) * 0.999f), GyroAxis::Roll, 100, dz) == 0.0f);   // inside: straight
        CHECK(gyro_steer_of(rad(-float(dz) - 0.5f), GyroAxis::Roll, 100, dz) > 0.0f);     // just past it: a little
        CHECK(gyro_steer_of(rad(-float(dz) - 0.5f), GyroAxis::Roll, 100, dz) < 0.05f);
    }
    // sensitivity 200 %: full lock at 20 degrees of tilt, 15 of turn; 50 %: 80 / 60
    CHECK(near(gyro_steer_of(rad(-20), GyroAxis::Roll, 200, 0), 1.0f));
    CHECK(near(gyro_steer_of(rad(-10), GyroAxis::Roll, 200, 0), 0.5f));
    CHECK(near(gyro_steer_of(rad(-15), GyroAxis::Yaw, 200, 0), 1.0f));
    CHECK(near(gyro_steer_of(rad(-40), GyroAxis::Roll, 50, 0), 0.5f));
    CHECK(near(gyro_steer_of(rad(-30), GyroAxis::Yaw, 50, 0), 0.5f));
    // out of range values are clamped (300 % with a 10 degree dead zone: full lock never inside the dead zone)
    CHECK(gyro_steer_of(rad(-11), GyroAxis::Yaw, 1000, 50) > 0.0f);
    CHECK(gyro_steer_of(rad(-9.9f), GyroAxis::Yaw, 1000, 50) == 0.0f);
    for (int s = kSensitivityMin; s <= kSensitivityMax; s += kSensitivityStep)
        for (int dz = 0; dz <= kDeadZoneMax; ++dz)
            for (float deg = -100; deg <= 100; deg += 0.7f) {
                const float v = gyro_steer_of(rad(deg), GyroAxis::Roll, s, dz), w = gyro_steer_of(rad(deg), GyroAxis::Yaw, s, dz);
                CHECK(v >= -1 && v <= 1 && w >= -1 && w <= 1 && (deg > 0 ? v <= 0 : v >= 0));
            }
}

TEST(gyro_integration) {
    // without an accelerometer: the roll rate (z) and the yaw rate (y) integrated by the sensor's own timestamps
    GyroSteer g;
    std::uint64_t t = 5000000000ull;
    for (int k = 0; k <= 1000; ++k, t += 1000000) feed_gyro_event(g, sensor(SDL_SENSOR_GYRO, 0, -0.2f, 0.5f, t));   // 1 s at 1 kHz
    CHECK(near(g.angle(GyroAxis::Roll), 0.5f, 1e-3f));
    CHECK(near(g.angle(GyroAxis::Yaw), -0.2f, 1e-3f));
    // a gap in the samples counts at most 50 ms (a pad that went quiet does not jump)
    feed_gyro_event(g, sensor(SDL_SENSOR_GYRO, 0, 0, 1.0f, t + 2000000000ull));
    CHECK(near(g.angle(GyroAxis::Roll), 0.55f, 1e-3f));
    // with the accelerometer: the gyro follows the movement, the accelerometer holds the level (no drift)
    GyroSteer f;
    t = 0;
    feed_gyro_event(f, upright_tilt(0, t));
    for (int k = 0; k < 500; ++k) {   // turned to 30 degrees over 0.5 s (60 deg/s)
        t += 1000000;
        feed_gyro_event(f, upright_tilt(float(k + 1) * 0.06f, t));
        feed_gyro_event(f, sensor(SDL_SENSOR_GYRO, 0, 0, rad(60), t));
    }
    CHECK(near(f.angle(GyroAxis::Roll), rad(30), rad(0.5f)));
    for (int k = 0; k < 3000; ++k) {   // then held there 3 s while the gyro has a bias of 2 deg/s: no drift
        t += 1000000;
        feed_gyro_event(f, upright_tilt(30, t));
        feed_gyro_event(f, sensor(SDL_SENSOR_GYRO, 0, 0, rad(2), t));
    }
    CHECK(near(f.angle(GyroAxis::Roll), rad(30), rad(1)));
    // the yaw is the rate about gravity: with the pad flat, turning it about its z (now vertical) is turning
    GyroSteer y;
    t = 0;
    feed_gyro_event(y, flat_tilt(0, t));
    for (int k = 0; k <= 500; ++k, t += 1000000) feed_gyro_event(y, sensor(SDL_SENSOR_GYRO, 0, 0, rad(40), t));
    CHECK(near(y.angle(GyroAxis::Yaw), rad(20), rad(0.2f)));
    // other sensors are not taken
    SDL_Event o = sensor(SDL_SENSOR_GYRO_L, 1, 1, 1, 0);
    CHECK(!feed_gyro_event(y, o));
    o.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    CHECK(!feed_gyro_event(y, o));
}

TEST(recentre) {
    GyroSteer g;
    feed_gyro_event(g, upright_tilt(15, 1));
    const GyroControls on = gyro_on(1);
    GamepadState s;
    bool held = false;
    CHECK(!apply_gyro(s, g, on, true, false, held));
    CHECK(s.gyro_use == 1 && near(s.gyro_steer, -(15.0f - 2) / 38.0f));
    s = {};
    s.touchpad = true;   // the default recentre button
    CHECK(apply_gyro(s, g, on, true, false, held));
    CHECK(s.gyro_steer == 0.0f && near(g.angle(GyroAxis::Roll), 0));
    feed_gyro_event(g, upright_tilt(25, 2));   // 10 degrees further: steering again, from the new straight
    CHECK(!apply_gyro(s, g, on, true, false, held));   // still held: no second recentre
    CHECK(near(s.gyro_steer, -(10.0f - 2) / 38.0f));
    s.touchpad = false;
    CHECK(!apply_gyro(s, g, on, true, false, held));
    s.touchpad = true;
    CHECK(apply_gyro(s, g, on, true, false, held));   // pressed again
    // another button, and none
    GyroControls back = on;
    back.recentre = PadInput::Back;
    feed_gyro_event(g, upright_tilt(40, 3));
    s = {};
    s.touchpad = true;
    held = false;
    CHECK(!apply_gyro(s, g, back, true, false, held) && s.gyro_steer < 0);
    s.back = true;
    CHECK(apply_gyro(s, g, back, true, false, held));
    GyroControls none = on;
    none.recentre = PadInput::None;
    s = {};
    s.touchpad = s.back = s.misc1 = true;
    held = false;
    CHECK(!apply_gyro(s, g, none, true, false, held));
    // the turn (yaw) is zeroed where driving starts, and by the button
    GyroSteer y;
    for (int k = 0; k <= 100; ++k) feed_gyro_event(y, sensor(SDL_SENSOR_GYRO, 0, 1.0f, 0, std::uint64_t(k) * 1000000));
    CHECK(near(y.angle(GyroAxis::Yaw), 0.1f));
    s = {};
    held = false;
    apply_gyro(s, y, gyro_on(2, 1), true, true, held);
    CHECK(y.angle(GyroAxis::Yaw) == 0.0f && s.gyro_use == 2 && s.gyro_steer == 0.0f);
}

// ---- the report ----

TEST(gyro_stick_values) {
    GamepadState base;
    base.left_x = 20000;   // the stick part way right
    const std::uint8_t stick_only = report(base)[4];
    // tilt right 21 degrees: steer (21 - 2) / 38 = 0.5 right
    GyroSteer g;
    feed_gyro_event(g, upright_tilt(-21, 1));
    bool held = false;
    GamepadState mix = base;
    apply_gyro(mix, g, gyro_on(1), true, false, held);
    const float gs = mix.gyro_steer;
    CHECK(mix.gyro_use == 1 && near(gs, (21.0f - 2) / 38.0f));
    const float stick = (20000.0f / 32767.0f - 0.24f) / (1.0f - 0.24f);
    CHECK(report(mix)[4] == stick_x_byte(std::min(stick + gs, 1.0f)));
    CHECK(report(mix)[4] > stick_only);
    GamepadState rep = base;
    apply_gyro(rep, g, gyro_on(2), true, false, held);
    CHECK(report(rep)[4] == stick_x_byte(gs));
    // replace: the stick's steering is gone, even at full left; mix: they add (clamped)
    rep.left_x = -32768;
    CHECK(report(rep)[4] == stick_x_byte(gs));
    mix.left_x = 32767;
    CHECK(report(mix)[4] == 0xFF);
    // only byte 4 changes: every other byte is the stick-only report's
    for (std::size_t k = 0; k < 32; ++k)
        if (k != 4) CHECK(report(mix)[k] == report(base)[k] && report(rep)[k] == report(base)[k]);
    // inside the dead zone: replace centres, mix leaves the stick's value as it is
    GyroSteer z;
    feed_gyro_event(z, upright_tilt(1.5f, 1));
    GamepadState m2 = base, r2 = base;
    apply_gyro(m2, z, gyro_on(1), true, false, held);
    apply_gyro(r2, z, gyro_on(2), true, false, held);
    CHECK(report(m2) == report(base));
    CHECK(report(r2)[4] == 0x80);
    // player 2's bindings and remapped steering: the gyro takes the steering wherever it is bound
    PlayerControls swapped = default_player(1);
    swapped.pad[std::size_t(Ds2::SteerLeft)] = {PadInput::RightXNeg};
    swapped.pad[std::size_t(Ds2::SteerRight)] = {PadInput::RightXPos};
    GamepadState rs;
    rs.right_x = -32768;
    apply_gyro(rs, g, gyro_on(1), true, false, held);
    CHECK(gamepad_sample(rs, swapped.pad)[4] == stick_x_byte(std::clamp(-1.0f + gs, -1.0f, 1.0f)));
}

TEST(gyro_off_or_not_driving_leaves_the_report) {
    std::mt19937 r(5);
    GyroSteer g;
    feed_gyro_event(g, upright_tilt(-30, 1));
    for (int i = 0; i < 20000; ++i) {
        GamepadState s;
        s.left_x = std::int16_t(int(r() % 65536) - 32768);
        s.left_y = std::int16_t(int(r() % 65536) - 32768);
        s.right_trigger = std::int16_t(r() % 32768);
        s.touchpad = (r() & 1) != 0;
        s.south = (r() & 1) != 0;
        const auto want = report(s);
        bool held = false;
        GamepadState off = s, idle = s, empty = s;
        CHECK(!apply_gyro(off, g, GyroControls{}, true, true, held));   // gyro off: nothing, not even the recentre button
        apply_gyro(idle, g, gyro_on(std::uint8_t(1 + r() % 2), std::uint8_t(r() % 2)), false, false, held);   // not driving
        GyroSteer none;   // on and driving, but no sensor data yet
        apply_gyro(empty, none, gyro_on(2), true, true, held);
        CHECK(off.gyro_use == 0 && idle.gyro_use == 0 && empty.gyro_use == 0);
        CHECK(report(off) == want && report(idle) == want && report(empty) == want);
    }
}

// ---- adaptive triggers ----

TEST(trigger_effect_bytes) {
    const TriggerEffect r = trigger_resist(40, 180), v = trigger_vibrate(30, 200, 7), o = trigger_off();
    CHECK(r[0] == 0x01 && r[1] == 40 && r[2] == 180);
    CHECK(v[0] == 0x06 && v[1] == 30 && v[2] == 200 && v[3] == 7);
    CHECK(o[0] == 0x05);
    for (std::size_t k = 1; k < 11; ++k) CHECK(o[k] == 0 && (k < 3 || r[k] == 0) && (k < 4 || v[k] == 0));
    const Ds5Block b = ds5_trigger_block(r, v);   // left resist, right vibrate
    CHECK(b.size() == 47);
    CHECK(b[0] == 0x0C);   // ucEnableBits1: right (0x04) and left (0x08) trigger effects
    for (std::size_t k = 0; k < 11; ++k) CHECK(b[10 + k] == v[k] && b[21 + k] == r[k]);
    for (std::size_t k = 1; k < 10; ++k) CHECK(b[k] == 0);     // rumble, audio: untouched
    for (std::size_t k = 32; k < 47; ++k) CHECK(b[k] == 0);    // LEDs, the rest: untouched
    const Ds5Block off = ds5_triggers_off();
    CHECK(off[0] == 0x0C && off[10] == 0x05 && off[21] == 0x05);
    for (std::size_t k = 0; k < 47; ++k) CHECK(k == 0 || k == 10 || k == 21 || off[k] == 0);
    CHECK(scaled(180, 100) == 180 && scaled(180, 50) == 90 && scaled(180, 10) == 18 && scaled(3, 10) == 1 && scaled(0, 100) == 0 &&
          scaled(255, 150) == 255 && scaled(200, 0) == 0);
}

TEST(trigger_effects_over_a_race) {
    using R = TriggerRole;
    TriggerFx fx;
    TriggerGame g;
    std::uint64_t t = 1000;
    // menus: both free
    CHECK(fx.step(g, R::Brake, R::Accel, 100, t) == TriggerPair{});
    // driving: the brake firm, the accelerator light
    g.active = true;
    TriggerPair p = fx.step(g, R::Brake, R::Accel, 100, t += 16);
    CHECK(p.left == trigger_resist(kBrakeStart, kBrakeForce) && p.right == trigger_resist(0, kAccelForce));
    p = fx.step(g, R::Brake, R::Accel, 50, t += 16);   // TRIGGER STRENGTH 50 %
    CHECK(p.left == trigger_resist(kBrakeStart, kBrakeForce / 2) && p.right == trigger_resist(0, kAccelForce / 2));
    // swapped bindings (the left trigger accelerates): the effects follow the roles; an unbound trigger stays free
    p = fx.step(g, R::Accel, R::None, 100, t += 16);
    CHECK(p.left == trigger_resist(0, kAccelForce) && p.right == trigger_off());
    // boosting: the accelerator buzzes, harder with more boost; a trace of boost is not boosting
    g.boost = 0.02f;
    CHECK(fx.step(g, R::Brake, R::Accel, 100, t += 16).right == trigger_resist(0, kAccelForce));
    g.boost = 0.5f;
    p = fx.step(g, R::Brake, R::Accel, 100, t += 16);
    CHECK(p.right == trigger_vibrate(kBoostHz, std::uint8_t(kBoostAmpMin + (kBoostAmpMax - kBoostAmpMin) / 2), 0));
    CHECK(p.left == trigger_resist(kBrakeStart, kBrakeForce));
    g.boost = 1.0f;
    CHECK(fx.step(g, R::Brake, R::Accel, 100, t += 16).right == trigger_vibrate(kBoostHz, kBoostAmpMax, 0));
    // a crash: both kick for kKickMs, then free while crashed, then back
    g.crashed = true;
    const std::uint64_t crash = t += 16;
    p = fx.step(g, R::Brake, R::Accel, 100, crash);
    CHECK(p.left == trigger_vibrate(kKickHz, kKickAmp, 0) && p.right == p.left && fx.kicking(crash));
    CHECK(fx.step(g, R::Brake, R::Accel, 100, crash + kKickMs - 1).left == trigger_vibrate(kKickHz, kKickAmp, 0));
    p = fx.step(g, R::Brake, R::Accel, 100, crash + kKickMs);
    CHECK(p == TriggerPair{});
    CHECK(fx.step(g, R::Brake, R::Accel, 100, crash + 5000) == TriggerPair{});   // still crashed: no second kick
    g.crashed = false;
    g.boost = 0;
    CHECK(fx.step(g, R::Brake, R::Accel, 100, crash + 6000).left == trigger_resist(kBrakeStart, kBrakeForce));
    // paused during a kick: free at once, and the kick does not come back on resuming
    g.crashed = true;
    fx.step(g, R::Brake, R::Accel, 100, crash + 7000);
    g.active = false;
    CHECK(fx.step(g, R::Brake, R::Accel, 100, crash + 7010) == TriggerPair{});
    g.active = true;
    CHECK(fx.step(g, R::Brake, R::Accel, 100, crash + 7020) == TriggerPair{});   // crashed, kick cancelled
    // a crash that began while not active (in a menu, paused) does not kick when driving resumes
    TriggerFx f2;
    TriggerGame h;
    h.crashed = true;
    f2.step(h, R::Brake, R::Accel, 100, 0);
    h.active = true;
    CHECK(f2.step(h, R::Brake, R::Accel, 100, 10) == TriggerPair{} && !f2.kicking(10));
    // every strength gives forces within the byte and in order
    for (int s = 10; s <= 100; s += 10) {
        TriggerFx f;
        TriggerGame d;
        d.active = true;
        const TriggerPair q = f.step(d, R::Brake, R::Accel, s, 0);
        CHECK(q.left[2] >= 1 && q.left[2] <= kBrakeForce && q.right[2] >= 1 && q.right[2] <= kAccelForce);
    }
}

TEST(trigger_roles_from_the_bindings) {
    PlayerControls p = default_player(0);
    CHECK(trigger_role(p.pad, false) == 1 && trigger_role(p.pad, true) == 2);   // the left trigger brakes, the right accelerates
    p.pad[std::size_t(Ds2::Square)] = {PadInput::West, PadInput::RightTrigger};
    p.pad[std::size_t(Ds2::Cross)] = {PadInput::South, PadInput::LeftTrigger};
    CHECK(trigger_role(p.pad, false) == 2 && trigger_role(p.pad, true) == 1);
    p.pad[std::size_t(Ds2::Cross)] = {PadInput::South};
    p.pad[std::size_t(Ds2::Square)] = {PadInput::West};
    CHECK(trigger_role(p.pad, false) == 0 && trigger_role(p.pad, true) == 0);
    // padlayout: both console layouts' triggers are the pedals (a DualSense on Auto: the PlayStation layout)
    CHECK(trigger_role(layout_bindings(PadLayout::Xbox), false) == 1 && trigger_role(layout_bindings(PadLayout::Xbox), true) == 2);
    CHECK(trigger_role(pad_bindings(default_player(0), PadStyle::PlayStation), false) == 1 &&
          trigger_role(pad_bindings(default_player(0), PadStyle::PlayStation), true) == 2);
}

TEST_MAIN("pad1: gyro steering and DualSense adaptive triggers (src/native/pad_gyro.h, pad_triggers.h)")
