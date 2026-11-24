// orbitcam.cpp -- the orbit camera in a race (orbitcam.h)
#include "orbitcam.h"

#include "garagecam.h"   // orbit_pivot, orbit_camera, yaw_right, camera_elevation

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace b3replay {

namespace {
constexpr float kPi = 3.14159265358979f;
float wrap(float a) { return std::remainder(a, 2.0f * kPi); }
constexpr float kMinReach = 2.0f;        // the car's vertical at least this far from the eye (m; not the in-car / bumper views)
constexpr float kReach = 25.0f;          // and at most this (a chase camera's)
constexpr float kMaxElevation = 1.2f;    // the eye's elevation over the pivot at most this (rad)
}  // namespace

float ChaseOrbit::dead(float v) {
    const float a = std::fabs(v);
    if (!(a > kDeadZone)) return 0.0f;
    return std::copysign(std::min(1.0f, (a - kDeadZone) / (1.0f - kDeadZone)), v);
}

bool ChaseOrbit::stick_counts(float dt, float x, float y) {
    if (std::fabs(x - ref_x) > kMoveDelta || std::fabs(y - ref_y) > kMoveDelta) {
        ref_x = x;
        ref_y = y;
        still = 0;
    } else {
        still += dt;
    }
    if (dead(x) == 0.0f && dead(y) == 0.0f) return false;
    return std::max(std::fabs(x), std::fabs(y)) >= kFirm || still < kIdleSeconds;
}

bool ChaseOrbit::step_look(float dt, float stick_x, float stick_y, float drag_x, float drag_y) {
    const bool counts = stick_counts(dt, stick_x, stick_y);   // (the drift guard)
    const bool stick = counts && stick_x * stick_x + stick_y * stick_y > kLookDeadZone * kLookDeadZone;
    const bool drag = drag_x != 0.0f || drag_y != 0.0f;
    if (stick) {   // where it points: right turns the view right, down looks behind
        target_yaw = std::atan2(stick_x, -stick_y);
        target_pitch = 0.0f;
    }
    if (drag) {    // the pointer moves the view itself, which then holds there
        yaw = wrap(yaw + drag_x * kDragYaw);
        pitch = std::clamp(pitch + drag_y * kDragPitch, -kPitchDown, kPitchUp);
        target_yaw = yaw;
        target_pitch = pitch;
    }
    const bool input = stick || drag;
    if (input) {
        engaged = true;
        hold = 0;
        returning = false;
    } else if (engaged) {
        hold += dt;
        if (hold >= kLookHold) {
            engaged = false;
            target_yaw = target_pitch = 0.0f;
            returning = true;
            from_yaw = yaw;
            from_pitch = pitch;
        }
    }
    const float a = 1.0f - std::pow(1.0f - kLookEase, dt * 60.0f);
    yaw = wrap(yaw + wrap(target_yaw - yaw) * a);
    pitch += (target_pitch - pitch) * a;
    if (!engaged && std::fabs(yaw) < 1e-3f && std::fabs(pitch) < 1e-3f) {   // back: the game's view exactly
        yaw = pitch = 0.0f;
        returning = false;
    }
    return input;
}

bool ChaseOrbit::step(float dt, float stick_x, float stick_y, float drag_x, float drag_y) {
    dt = std::clamp(dt, 0.0f, 0.25f);
    if (style == kLook) return step_look(dt, stick_x, stick_y, drag_x, drag_y);
    const bool counts = stick_counts(dt, stick_x, stick_y);
    const float sx = counts ? dead(stick_x) : 0.0f, sy = counts ? dead(stick_y) : 0.0f;
    const bool input = sx != 0.0f || sy != 0.0f || drag_x != 0.0f || drag_y != 0.0f;
    if (input) {
        idle = 0;
        returning = false;
        ret = 0;
        yaw = wrap(yaw + sx * kYawRate * dt + drag_x * kDragYaw);
        pitch = std::clamp(pitch + sy * kPitchRate * dt + drag_y * kDragPitch, -kPitchDown, kPitchUp);
        return true;
    }
    if (!active()) {
        idle = 0;
        returning = false;
        return false;
    }
    idle += dt;
    if (idle < kIdleSeconds) return false;
    if (!returning) {
        returning = true;
        ret = 0;
        from_yaw = yaw;
        from_pitch = pitch;
    }
    ret = std::min(1.0f, ret + dt / kReturnSeconds);
    const float e = 1.0f - ret * ret * (3.0f - 2.0f * ret);   // smoothstep: eases out of the turn and into the game's view
    yaw = ret >= 1.0f ? 0.0f : from_yaw * e;
    pitch = ret >= 1.0f ? 0.0f : from_pitch * e;
    if (ret >= 1.0f) returning = false;
    return false;
}

float wall_distance(const TrackRays& rays, const float from[3], const float eye[3], const float side[3]) {
    const float d[3] = {eye[0] - from[0], eye[1] - from[1], eye[2] - from[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(len > 1e-3f)) return len;
    float t = 2.0f;
    for (int k = side ? -1 : 0; k <= (side ? 1 : 0); ++k) {
        float to[3], h;
        for (int a = 0; a < 3; ++a) to[a] = eye[a] + (side ? float(k) * kWallMargin * side[a] : 0.0f);
        if (rays.segment(from, to, &h)) t = std::min(t, h);
    }
    if (t > 1.0f) return len;
    return std::min(len, std::max(kMinEyeDistance, t * len - kWallMargin));   // (never into the car)
}

float ease_wall(float prev, float raw, float dt) {
    if (prev < 0.0f || raw <= prev) return raw;   // nearer: at once
    return prev + (raw - prev) * (1.0f - std::exp(-std::max(0.0f, dt) / kWallOutSeconds));
}

bool apply_chase_orbit(Camera& c, const std::vector<Vehicle>& vehicles, std::uint32_t car_id, const float look[2], const OrbitDraw& draw) {
    if (!c.valid || (look[0] == 0.0f && look[1] == 0.0f)) return false;
    const float share = std::clamp(c.chase, 0.0f, 1.0f);
    if (!(share > 0.0f)) return false;
    const Vehicle* car = nullptr;
    for (const Vehicle& v : vehicles)
        if (v.id == car_id && v.kind == VehicleKind::kRaceCar) { car = &v; break; }
    float pivot[3];
    if (!car || !orbit_pivot(c, car->main, pivot)) return false;
    const float hx = c.m.r[3][0] - pivot[0], hz = c.m.r[3][2] - pivot[2];
    const float h = std::sqrt(hx * hx + hz * hz);
    if (!(h >= kMinReach) || h > kReach) return false;
    // the view turned right: the eye goes to the screen's left round the car (yaw_right: the turn that moves it right)
    const float yaw = -yaw_right(c, pivot) * look[0] * share;
    const float elev = camera_elevation(c, pivot);
    const float to = std::clamp(elev + look[1] * share, std::min(elev, 0.0f), std::max(elev, kMaxElevation));
    orbit_camera(c, pivot, yaw, to - elev);
    if (draw.wall >= 0.0f) {   // orbitcam2: not through a wall
        const float from[3] = {car->main.r[3][0], car->main.r[3][1] + kRayHeight, car->main.r[3][2]};
        const float d[3] = {c.m.r[3][0] - from[0], c.m.r[3][1] - from[1], c.m.r[3][2] - from[2]};
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > draw.wall && len > 1e-3f) {
            const float w = orbit_marker_away(look) * share;   // (none at the game's view: no jump as it comes back)
            const float k = (draw.wall / len - 1.0f) * w;
            for (int a = 0; a < 3; ++a) c.m.r[3][a] += d[a] * k;
        }
    }
    if (draw.front_zoom) {   // orbitcam2: narrower as the eye goes round to the car's front
        const float z = std::max(0.0f, -std::cos(look[0] * share)) * kFrontZoomDeg * (kPi / 180.0f);
        if (z > 0.0f && c.half_w > 0.0f && c.half_h > 0.0f) {
            const float half = std::atan(c.half_w);
            const float s = std::tan(std::max(0.05f, half - 0.5f * z)) / c.half_w;
            c.half_w *= s;
            c.half_h *= s;
        }
    }
    return true;
}

void ChaseRoll::step(float dt, const float at[3], const float right[3], float gain, float max_deg) {
    if (!have) {
        have = true;
        roll = 0.0f;
        for (int k = 0; k < 3; ++k) prev_at[k] = at[k];
        return;
    }
    const float side = (at[0] - prev_at[0]) * right[0] + (at[1] - prev_at[1]) * right[1] + (at[2] - prev_at[2]) * right[2];
    if (!(std::fabs(side) <= kCut)) {   // a cut (or no number): level again from the new view
        roll = 0.0f;
        for (int k = 0; k < 3; ++k) prev_at[k] = at[k];
        return;
    }
    if (!(dt >= kMinDt)) return;   // no game time: held
    const float target = std::clamp(-side / dt * gain, -max_deg, max_deg);
    roll += (target - roll) * std::min(kEase * dt, 1.0f);
    for (int k = 0; k < 3; ++k) prev_at[k] = at[k];
}

void roll_camera(Camera& c, float deg) {
    if (deg == 0.0f) return;
    const float a = deg * (kPi / 180.0f), cs = std::cos(a), sn = std::sin(a);
    for (int k = 0; k < 3; ++k) {
        const float r = c.m.r[0][k], u = c.m.r[1][k];
        c.m.r[0][k] = r * cs + u * sn;
        c.m.r[1][k] = u * cs - r * sn;
    }
}

bool chase_view(const Camera& c, const std::vector<Vehicle>& vehicles, std::uint32_t car_id) {
    if (!c.valid || !(c.chase > 0.0f)) return false;
    const Vehicle* car = nullptr;
    for (const Vehicle& v : vehicles)
        if (v.id == car_id && v.kind == VehicleKind::kRaceCar) { car = &v; break; }
    float pivot[3];
    if (!car || !orbit_pivot(c, car->main, pivot)) return false;
    const float hx = c.m.r[3][0] - pivot[0], hz = c.m.r[3][2] - pivot[2];
    const float h = std::sqrt(hx * hx + hz * hz);
    return h >= kMinReach && h <= kReach;
}

float orbit_marker_away(const float look[2]) {
    return std::clamp(std::max(std::fabs(look[0]), std::fabs(look[1])) / kOrbitMarkerFade, 0.0f, 1.0f);
}

OrbitCam& OrbitCam::instance() {
    static OrbitCam* o = new OrbitCam();   // never destroyed (the game thread may outlive static destruction)
    return *o;
}

void OrbitCam::set_roll(int level) {
    static const int env = [] { const char* e = std::getenv("B3_CAMERA_ROLL"); return e && *e >= '0' && *e <= '2' ? *e - '0' : -1; }();
    if (env >= 0) level = env;
    roll_level_.store(level < 0 ? 0 : level > 2 ? 2 : level);
}

void OrbitCam::set_window_stick(int p, float x, float y) {
    if (p < 0 || p >= kPlayers) return;
    std::lock_guard<std::mutex> l(mu_);
    win_x_[p] = x;
    win_y_[p] = y;
}

void OrbitCam::set_test_stick(float x, float y) {
    // checks only: B3_ORBITCAM_TEST_PLAYER=2 makes the scripted stick player 2's (view 2 of a split-screen race)
    static const bool p2 = [] { const char* e = std::getenv("B3_ORBITCAM_TEST_PLAYER"); return e && *e == '2'; }();
    std::lock_guard<std::mutex> l(mu_);
    if (p2) {
        win_x_[1] = x;
        win_y_[1] = y;
        return;
    }
    test_x_ = x;
    test_y_ = y;
}

void OrbitCam::add_drag(float dx, float dy) {
    std::lock_guard<std::mutex> l(mu_);
    drag_x_ += dx;
    drag_y_ += dy;
}

void OrbitCam::note_poll(std::uint32_t poll) {
    std::lock_guard<std::mutex> l(mu_);
    Input in;
    in.poll = poll;
    if (enabled_.load() && !blocked_.load() && driving_.load()) {
        for (int p = 0; p < kPlayers; ++p) {
            in.x[p] = win_x_[p];
            in.y[p] = win_y_[p];
        }
        if (test_x_ * test_x_ + test_y_ * test_y_ > in.x[0] * in.x[0] + in.y[0] * in.y[0]) {
            in.x[0] = test_x_;
            in.y[0] = test_y_;
        }
        in.dx = drag_x_;
        in.dy = drag_y_;
    }
    drag_x_ = drag_y_ = 0;
    bool same = in.dx == 0.0f && in.dy == 0.0f;
    for (int p = 0; p < kPlayers; ++p) same = same && in.x[p] == last_.x[p] && in.y[p] == last_.y[p];
    if (same) return;
    last_ = in;
    if (inputs_.size() >= 4096) inputs_.pop_front();
    inputs_.push_back(in);
}

void OrbitCam::on_tick(Tick& t) {
    static const bool log = [] { const char* e = std::getenv("B3_ORBITCAM_LOG"); return e && *e && *e != '0'; }();
    float dx = 0, dy = 0;
    {
        std::lock_guard<std::mutex> l(mu_);
        while (!inputs_.empty() && inputs_.front().poll <= t.poll) {
            for (int p = 0; p < kPlayers; ++p) {
                now_.x[p] = inputs_.front().x[p];
                now_.y[p] = inputs_.front().y[p];
            }
            dx += inputs_.front().dx;
            dy += inputs_.front().dy;
            inputs_.pop_front();
        }
    }
    // the field rate: VBlanks over the game's own timer (gWorld+0x20) while it runs, snapped to 50 (a PAL disc) or 59.94;
    // kept while the timer stands (the pause menu, where the view still eases back in VBlanks)
    if (prev_vblank_ != 0 && t.vblank > prev_vblank_) {
        const float dn = t.now - prev_now_;
        if (dn > 0.001f && dn < 0.25f) {
            const float r = float(t.vblank - prev_vblank_) / dn;
            if (std::fabs(r - 50.0f) < 4.0f) field_hz_ = 50.0f;
            else if (std::fabs(r - 59.94f) < 4.0f) field_hz_ = 59.94f;
        }
    }
    const float dt = prev_vblank_ != 0 && t.vblank > prev_vblank_ ? float(t.vblank - prev_vblank_) / field_hz_ : 0.0f;
    if (t.vblank != prev_vblank_) prev_now_ = t.now;   // (a split screen's second poll of a VBlank: the first's)
    prev_vblank_ = t.vblank;
    for (int p = 0; p < kPlayers; ++p) {
        t.race_orbit[p][0] = t.race_orbit[p][1] = 0.0f;
        // a race view of this player's: the race (not a car page), the view's camera, the player's car in the tick
        const Camera& cam = p == 0 ? t.cam : t.view2.cam;
        const std::uint32_t id = p == 0 ? kPlayerRaceCar : kPlayer2RaceCar;
        bool race = t.fe_car == 0 && cam.valid && (p == 0 || (t.split && t.view2.valid)) && enabled_.load();
        if (race) {
            race = false;
            for (const Vehicle& v : t.vehicles) race = race || (v.id == id && v.kind == VehicleKind::kRaceCar);
        }
        ChaseOrbit& o = orbit_[p];
        o.style = style_.load();   // orbitcam2 (ORBIT STYLE)
        if (p == 1 && t.split && t.view2.valid && t.fe_car == 0 && !race && !warned_p2_ && enabled_.load()) {   // (a check's line)
            warned_p2_ = true;
            std::printf("[orbitcam] poll %u: a split-screen tick without player 2's race car %08X: view 2 not orbited\n", t.poll, id);
            std::fflush(stdout);
        }
        if (p == 1 && race && !seen_p2_) {
            seen_p2_ = true;
            std::printf("[orbitcam] poll %u: split screen: player 2's race car %08X, view 2 orbits with player 2's stick\n", t.poll, id);
            std::fflush(stdout);
        }
        if (!race) {
            if (o.active()) {
                std::printf("[orbitcam] poll %u: player %d: no race view, the orbit ended\n", t.poll, p + 1);
                std::fflush(stdout);
            }
            o.reset();
            was_active_[p] = false;
            wall_d_[p] = -1.0f;
            continue;
        }
        const bool was_returning = o.returning;
        // gameopts (MIRROR MODE, the tick's race word: Frame::mirror): the picture is the world reversed left to right, so the
        // horizontal input turns the view the other way in the world and stays right on screen (as photo mode's)
        const float hx = (t.race_time.game_options & 1u) ? -1.0f : 1.0f;
        const float ddx = p == 0 ? hx * dx : 0.0f, ddy = p == 0 ? dy : 0.0f;
        const bool input = o.step(dt, hx * now_.x[p], now_.y[p], ddx, ddy);
        t.race_orbit[p][0] = o.yaw;
        t.race_orbit[p][1] = o.pitch;
        {   // orbitcam2: the walls between the car and this tick's turned eye, the distance eased in game time
            std::shared_ptr<const TrackRays> rays = o.active() && rays_source_ ? rays_source_() : nullptr;
            float raw = -1.0f;
            Camera turned = cam;
            const float look[2] = {o.yaw, o.pitch};
            const Vehicle* car = nullptr;
            for (const Vehicle& v : t.vehicles) if (v.id == id && v.kind == VehicleKind::kRaceCar) { car = &v; break; }
            if (rays && car && apply_chase_orbit(turned, t.vehicles, id, look)) {
                const float from[3] = {car->main.r[3][0], car->main.r[3][1] + kRayHeight, car->main.r[3][2]};
                float side[3] = {turned.m.r[0][0], turned.m.r[0][1], turned.m.r[0][2]};   // (across the view)
                const float sl = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
                for (float& v : side) v = sl > 1e-6f ? v / sl : 0.0f;
                raw = wall_distance(*rays, from, turned.m.r[3], sl > 1e-6f ? side : nullptr);
            }
            wall_d_[p] = raw < 0.0f ? -1.0f : ease_wall(wall_d_[p], raw, dt);
            t.race_wall[p] = wall_d_[p];
        }
        if (input && (!was_active_[p] || was_returning)) {
            std::printf("[orbitcam] poll %u: player %d: the view taken (%s)\n", t.poll, p + 1, ddx != 0.0f || ddy != 0.0f ? "mouse" : "right stick");
            std::fflush(stdout);
        }
        if (o.returning && !was_returning) {
            std::printf("[orbitcam] poll %u: player %d: the stick at rest %.2f s: the view eases back from yaw %.3f, pitch %.3f\n", t.poll, p + 1,
                        double(o.style == ChaseOrbit::kLook ? ChaseOrbit::kLookHold : ChaseOrbit::kIdleSeconds), double(o.from_yaw), double(o.from_pitch));
            std::fflush(stdout);
        }
        if (was_active_[p] && !o.active()) {
            std::printf("[orbitcam] poll %u: player %d: back to the game's chase view\n", t.poll, p + 1);
            std::fflush(stdout);
        }
        if (log && (input || o.active()))
            std::printf("[orbitcam] poll %u vblank %u: player %d stick %.2f %.2f drag %.3f %.3f: yaw %.4f pitch %.4f%s\n", t.poll, t.vblank, p + 1,
                        double(now_.x[p]), double(now_.y[p]), double(ddx), double(ddy), double(o.yaw), double(o.pitch), o.returning ? " (returning)" : "");
        was_active_[p] = o.active();
    }
    // camroll1: each view's chase camera roll, from the game's own camera of the tick (independent of the orbit)
    const int lv = roll_level_.load();
    for (int p = 0; p < kPlayers; ++p) {
        t.race_roll[p] = 0.0f;
        const Camera& cam = p == 0 ? t.cam : t.view2.cam;
        const std::uint32_t id = p == 0 ? kPlayerRaceCar : kPlayer2RaceCar;
        const bool view = lv > 0 && t.fe_car == 0 && (p == 0 || (t.split && t.view2.valid)) && chase_view(cam, t.vehicles, id);
        ChaseRoll& r = roll_[p];
        if (!view) {
            if (r.have) r.reset();
            continue;
        }
        const float rdt = r.have ? t.now - roll_now_[p] : 0.0f;
        roll_now_[p] = t.now;
        r.step(std::clamp(rdt, 0.0f, 0.25f), cam.m.r[2], cam.m.r[0], ChaseRoll::kGain[lv], ChaseRoll::kMaxDeg[lv]);
        t.race_roll[p] = r.roll * std::clamp(cam.chase, 0.0f, 1.0f);
        if (!rolled_[p] && std::fabs(r.roll) > 1.0f) {
            rolled_[p] = true;
            std::printf("[camroll] poll %u: player %d: the chase camera leans into a corner (%.2f deg, CAMERA ROLL %s)\n", t.poll, p + 1,
                        double(r.roll), lv == 1 ? "Subtle" : "Strong");
            std::fflush(stdout);
        }
        if (log) std::printf("[camroll] poll %u: player %d roll %.3f deg (share %.2f)\n", t.poll, p + 1, double(r.roll), double(cam.chase));
    }
    if (log) std::fflush(stdout);
}

}  // namespace b3replay
