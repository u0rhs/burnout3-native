// garagecam.cpp -- the garage camera (garagecam.h)
#include "garagecam.h"

#include <algorithm>
#include <initializer_list>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace b3replay {

namespace {
constexpr float kPi = 3.14159265358979f;
float wrap(float a) { return std::remainder(a, 2.0f * kPi); }
void rot_y(float v[3], float cs, float sn) {
    const float x = v[0] * cs + v[2] * sn, z = -v[0] * sn + v[2] * cs;
    v[0] = x;
    v[2] = z;
}
void rot_axis(float v[3], const float k[3], float cs, float sn) {   // Rodrigues, k a unit vector
    const float kv[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
    const float kd = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
    for (int i = 0; i < 3; ++i) v[i] = v[i] * cs + kv[i] * sn + k[i] * kd * (1.0f - cs);
}
}  // namespace

float GarageOrbit::dead(float v) {
    const float a = std::fabs(v);
    if (!(a > kDeadZone)) return 0.0f;
    return std::copysign(std::min(1.0f, (a - kDeadZone) / (1.0f - kDeadZone)), v);
}

bool GarageOrbit::stick_counts(float dt, float stick_x, float stick_y) {
    if (std::fabs(stick_x - ref_x) > kMoveDelta || std::fabs(stick_y - ref_y) > kMoveDelta) {
        ref_x = stick_x;
        ref_y = stick_y;
        still = 0;
    } else {
        still += dt;
    }
    if (dead(stick_x) == 0.0f && dead(stick_y) == 0.0f) return false;
    return std::max(std::fabs(stick_x), std::fabs(stick_y)) >= kFirm || still < kIdleSeconds;
}

void GarageOrbit::step(float dt, float stick_x, float stick_y, float drag_x, float drag_y, float game_angle, float game_elevation, float right,
                       float max_elevation) {
    dt = std::clamp(dt, 0.0f, 0.25f);
    const bool counts = stick_counts(dt, stick_x, stick_y);
    const float sx = counts ? dead(stick_x) : 0.0f, sy = counts ? dead(stick_y) : 0.0f;
    const bool input = sx != 0.0f || sy != 0.0f || drag_x != 0.0f || drag_y != 0.0f;
    float turn = 0;   // the game's turn round the car since the last tick
    if (have_prev) turn = wrap(game_angle - prev_angle);
    if (std::fabs(turn) > kCut) turn = 0;   // a cut (a new car's camera): shown as the game makes it
    prev_angle = game_angle;
    have_prev = true;
    if (input) {
        engaged = true;
        idle = 0;
        resume = 0;
    } else if (engaged) {
        idle += dt;
        if (idle >= kIdleSeconds) resume = std::min(1.0f, resume + dt / kResumeSeconds);
    }
    if (!engaged) return;
    const float eased = resume * resume * (3.0f - 2.0f * resume);
    // held: the game's turn taken out (the view stays); resumed: it shows (the offsets stay as they are)
    const float r = right < 0.0f ? -1.0f : 1.0f;
    yaw = wrap(yaw - turn * (1.0f - eased) + r * (sx * kYawRate * dt - drag_x * kDragYaw));
    float lo = std::max(kMinElevation, game_elevation - kPitchDown);
    float hi = std::min({kMaxElevation, max_elevation, game_elevation + kPitchUp});
    lo = std::min(lo, game_elevation);   // (a game camera outside the range: no offset pushes it further out)
    hi = std::max(hi, game_elevation);
    pitch = std::clamp(game_elevation + pitch + sy * kPitchRate * dt + drag_y * kDragPitch, lo, hi) - game_elevation;
}

bool orbit_pivot(const Camera& c, const Mat34& car, float pivot[3]) {
    const float* e = c.m.r[3];
    float a[3] = {c.m.r[2][0], c.m.r[2][1], c.m.r[2][2]};
    const float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!(al > 1e-6f)) return false;
    for (float& v : a) v /= al;
    const float ah = a[0] * a[0] + a[2] * a[2];
    const float cx = car.r[3][0] - e[0], cz = car.r[3][2] - e[2];
    if (!(ah > 1e-4f) || !(cx * cx + cz * cz > 0.01f)) return false;
    const float t = (cx * a[0] + cz * a[2]) / ah;
    if (!(t > 0.0f)) return false;
    pivot[0] = car.r[3][0];
    pivot[1] = e[1] + t * a[1];
    pivot[2] = car.r[3][2];
    return true;
}

float camera_angle(const Camera& c, const Mat34& car) {
    // the eye's bearing from the car (about the vertical, as R_y turns it) less the car's heading
    const float eye = std::atan2(c.m.r[3][0] - car.r[3][0], c.m.r[3][2] - car.r[3][2]);
    const float heading = std::atan2(car.r[2][0], car.r[2][2]);
    return wrap(eye - heading);
}

float yaw_right(const Camera& c, const float pivot[3]) {
    const float px = c.m.r[3][0] - pivot[0], pz = c.m.r[3][2] - pivot[2];
    // d(eye)/d(yaw) for R_y is (pz, 0, -px); the screen's right is -r[0]
    return -(pz * c.m.r[0][0] - px * c.m.r[0][2]) < 0.0f ? -1.0f : 1.0f;
}

float eye_cap_elevation(const Camera& c, const float pivot[3], float car_y) {
    const float d[3] = {c.m.r[3][0] - pivot[0], c.m.r[3][1] - pivot[1], c.m.r[3][2] - pivot[2]};
    const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(dist > 1e-3f)) return 0.0f;
    return std::asin(std::clamp((car_y + GarageOrbit::kMaxEyeAboveCar - pivot[1]) / dist, -1.0f, 1.0f));
}

float camera_elevation(const Camera& c, const float pivot[3]) {
    const float dx = c.m.r[3][0] - pivot[0], dz = c.m.r[3][2] - pivot[2];
    return std::atan2(c.m.r[3][1] - pivot[1], std::sqrt(dx * dx + dz * dz));
}

void orbit_camera(Camera& c, const float pivot[3], float yaw, float pitch) {
    float p[3], l[3];
    for (int k = 0; k < 3; ++k) {
        p[k] = c.m.r[3][k] - pivot[k];
        l[k] = c.lod_pos[k] - pivot[k];
    }
    if (yaw != 0.0f) {
        const float cs = std::cos(yaw), sn = std::sin(yaw);
        rot_y(p, cs, sn);
        rot_y(l, cs, sn);
        for (int r = 0; r < 3; ++r) rot_y(c.m.r[r], cs, sn);
    }
    if (pitch != 0.0f) {
        const float hl = std::sqrt(p[0] * p[0] + p[2] * p[2]);
        if (hl > 1e-5f) {
            const float k[3] = {-p[2] / hl, 0.0f, p[0] / hl};   // cross(h, up): a positive turn raises the eye
            const float cs = std::cos(pitch), sn = std::sin(pitch);
            rot_axis(p, k, cs, sn);
            rot_axis(l, k, cs, sn);
            for (int r = 0; r < 3; ++r) rot_axis(c.m.r[r], k, cs, sn);
        }
    }
    for (int k = 0; k < 3; ++k) {
        c.m.r[3][k] = pivot[k] + p[k];
        c.lod_pos[k] = pivot[k] + l[k];
    }
}

void orbit_lerp(const float a[2], const float b[2], float frac, float out[2]) {
    out[0] = wrap(a[0] + std::remainder(b[0] - a[0], 2.0f * kPi) * frac);
    out[1] = a[1] + (b[1] - a[1]) * frac;
}

GarageCam& GarageCam::instance() {
    static GarageCam* g = new GarageCam();   // never destroyed (the game thread may outlive static destruction)
    return *g;
}

void GarageCam::set_window_stick(float x, float y) {
    std::lock_guard<std::mutex> l(mu_);
    win_x_ = x;
    win_y_ = y;
}

void GarageCam::set_test_stick(float x, float y) {
    std::lock_guard<std::mutex> l(mu_);
    test_x_ = x;
    test_y_ = y;
}

void GarageCam::add_drag(float dx, float dy) {
    std::lock_guard<std::mutex> l(mu_);
    drag_x_ += dx;
    drag_y_ += dy;
}

void GarageCam::note_poll(std::uint32_t poll) {
    std::lock_guard<std::mutex> l(mu_);
    Input in;
    in.poll = poll;
    if (enabled_.load() && !blocked_.load()) {
        const bool test = test_x_ * test_x_ + test_y_ * test_y_ > win_x_ * win_x_ + win_y_ * win_y_;
        in.x = test ? test_x_ : win_x_;
        in.y = test ? test_y_ : win_y_;
        in.dx = drag_x_;
        in.dy = drag_y_;
    }
    drag_x_ = drag_y_ = 0;
    if (in.x == last_.x && in.y == last_.y && in.dx == 0.0f && in.dy == 0.0f) return;
    last_ = in;
    if (inputs_.size() >= 4096) inputs_.pop_front();
    inputs_.push_back(in);
}

void GarageCam::on_tick(Tick& t) {
    if (!log_checked_) {
        const char* e = std::getenv("B3_GARAGECAM_LOG");
        log_ = e && *e && *e != '0';
        log_checked_ = true;
    }
    float dx = 0, dy = 0;
    {
        std::lock_guard<std::mutex> l(mu_);
        while (!inputs_.empty() && inputs_.front().poll <= t.poll) {
            now_.x = inputs_.front().x;
            now_.y = inputs_.front().y;
            dx += inputs_.front().dx;
            dy += inputs_.front().dy;
            inputs_.pop_front();
        }
    }
    const float dt = prev_vblank_ != 0 && t.vblank > prev_vblank_ ? float(t.vblank - prev_vblank_) / 59.94f : 0.0f;
    prev_vblank_ = t.vblank;
    t.fe_orbit[0] = t.fe_orbit[1] = 0.0f;
    const Vehicle* car = nullptr;
    if (t.fe_car != 0 && t.cam.valid)
        for (const Vehicle& v : t.vehicles) if (v.id == t.fe_car) car = &v;
    float pivot[3];
    if (!car || !enabled_.load() || !orbit_pivot(t.cam, car->main, pivot)) {
        if (orbit_.engaged) {
            std::printf("[garagecam] poll %u: off the car pages, the orbit ended\n", t.poll);
            std::fflush(stdout);
        }
        if (orbit_.have_prev) orbit_.reset();
        car_page_.store(false);
        return;
    }
    car_page_.store(true);
    const float angle = camera_angle(t.cam, car->main), elev = camera_elevation(t.cam, pivot);
    const bool was_engaged = orbit_.engaged;
    const float was_resume = orbit_.resume;
    const bool input = GarageOrbit::dead(now_.x) != 0.0f || GarageOrbit::dead(now_.y) != 0.0f || dx != 0.0f || dy != 0.0f;
    orbit_.step(dt, now_.x, now_.y, dx, dy, angle, elev, yaw_right(t.cam, pivot), eye_cap_elevation(t.cam, pivot, car->main.r[3][1]));
    t.fe_orbit[0] = orbit_.yaw;
    t.fe_orbit[1] = orbit_.pitch;
    if (orbit_.engaged && (!was_engaged || (input && was_resume > 0.0f))) {
        std::printf("[garagecam] poll %u: the view taken (%s), the game's spin held\n", t.poll, dx != 0.0f || dy != 0.0f ? "mouse" : "right stick");
        std::fflush(stdout);
    }
    if (orbit_.engaged && was_resume == 0.0f && orbit_.resume > 0.0f) {
        std::printf("[garagecam] poll %u: the stick at rest %.1f s: the game's spin resumes from yaw %.3f, pitch %.3f\n", t.poll,
                    double(GarageOrbit::kIdleSeconds), double(orbit_.yaw), double(orbit_.pitch));
        std::fflush(stdout);
    }
    if (log_ && (++log_every_ % 10 == 0 || input)) {
        const float ex = t.cam.m.r[3][0] - pivot[0], ey = t.cam.m.r[3][1] - pivot[1], ez = t.cam.m.r[3][2] - pivot[2];
        std::printf("[garagecam] poll %u vblank %u: game angle %.4f elevation %.4f distance %.3f, pivot %.3f %.3f %.3f, eye y %.3f, car y %.3f;"
                    " stick %.2f %.2f drag %.3f %.3f; yaw %.4f pitch %.4f resume %.2f\n",
                    t.poll, t.vblank, double(angle), double(elev), double(std::sqrt(ex * ex + ey * ey + ez * ez)), double(pivot[0]), double(pivot[1]), double(pivot[2]),
                    double(t.cam.m.r[3][1]), double(car->main.r[3][1]), double(now_.x), double(now_.y), double(dx), double(dy),
                    double(orbit_.yaw), double(orbit_.pitch), double(orbit_.resume));
        std::fflush(stdout);
    }
}

}  // namespace b3replay
