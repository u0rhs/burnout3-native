// The replay viewer's cameras: src/b3replay/replaycams.h.
#include "replaycams.h"

#include "finishcam.h"   // finish_camera: the drawn camera from an eye, an aim and a zoom
#include "trackrays.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace b3replay {

namespace {

void norm3(float* v) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) for (int k = 0; k < 3; ++k) v[k] /= l;
}

}  // namespace

const char* replay_angle_name(ReplayAngle a) {
    switch (a) {
    case ReplayAngle::kGame: return "GAME";
    case ReplayAngle::kChase: return "CHASE";
    case ReplayAngle::kFarChase: return "FAR CHASE";
    case ReplayAngle::kBumper: return "BUMPER";
    case ReplayAngle::kOrbit: return "ORBIT";
    case ReplayAngle::kTv: return "TV";
    }
    return "";
}

void ReplayView::step(ReplayAngle a, std::uint32_t car_id, float dt, const Mat34& m, const Blocked& blocked) {
    const float* p = m.r[3];
    if (!on || car != car_id) {   // a new car (or the first step): start from its own heading, no velocity yet
        reset();
        on = true;
        car = car_id;
        for (int k = 0; k < 3; ++k) prev[k] = p[k];
        heading[0] = m.r[2][0], heading[1] = 0, heading[2] = m.r[2][2];
        norm3(heading);
        dt = 0;
    }
    if (angle != a) { tv_set = false; angle = a; }
    if (dt > 0) {
        for (int k = 0; k < 3; ++k) vel[k] = (p[k] - prev[k]) / dt;
        // the level heading eases towards the car's (its nose, not its travel: a car sliding sideways stays framed)
        float h[3] = {m.r[2][0], 0.0f, m.r[2][2]};
        norm3(h);
        const float w = 1.0f - std::exp(-kHeadingRate * dt);
        for (int k = 0; k < 3; ++k) heading[k] += (h[k] - heading[k]) * w;
        heading[1] = 0;
        norm3(heading);
    }
    for (int k = 0; k < 3; ++k) prev[k] = p[k];
    const float up[3] = {0, 1, 0};
    zoom = 1.0f;
    switch (a) {
    case ReplayAngle::kGame:
        break;
    case ReplayAngle::kChase:
    case ReplayAngle::kFarChase: {
        const bool far = a == ReplayAngle::kFarChase;
        const float back = far ? kFarBack : kChaseBack, high = far ? kFarUp : kChaseUp;
        for (int k = 0; k < 3; ++k) {
            eye[k] = p[k] - heading[k] * back + up[k] * high;
            look[k] = p[k] + heading[k] * kChaseLook + up[k] * 0.8f;
        }
        // a wall between the car and the eye: the eye a margin before it (never nearer than 2 m)
        if (blocked) {
            const float from[3] = {p[0], p[1] + 1.0f, p[2]};
            float h = 1.0f;
            if (blocked(from, eye, &h) && h < 1.0f) {
                const float s = std::max(h - 0.4f / std::max(back, 1.0f), 2.0f / back);
                for (int k = 0; k < 3; ++k) eye[k] = from[k] + (eye[k] - from[k]) * s;
            }
        }
        break;
    }
    case ReplayAngle::kBumper:   // the car's own frame: it pitches and rolls with the car
        for (int k = 0; k < 3; ++k) {
            eye[k] = p[k] + m.r[2][k] * kBumperAhead + m.r[1][k] * kBumperUp;
            look[k] = eye[k] + m.r[2][k] * 20.0f + m.r[1][k] * 0.2f;
        }
        // a wall at the car's nose (a car pressed against it): the eye back inside the car, kBumperMargin short of it
        if (blocked) {
            float from[3], to[3], h = 1.0f;
            for (int k = 0; k < 3; ++k) {
                from[k] = p[k] + m.r[1][k] * kBumperUp;
                to[k] = from[k] + m.r[2][k] * (kBumperAhead + kBumperMargin);
            }
            if (blocked(from, to, &h) && h < 1.0f) {
                const float d = std::max(h * (kBumperAhead + kBumperMargin) - kBumperMargin, 0.0f);
                for (int k = 0; k < 3; ++k) {
                    eye[k] = from[k] + m.r[2][k] * d;
                    look[k] = eye[k] + m.r[2][k] * 20.0f + m.r[1][k] * 0.2f;
                }
            }
        }
        break;
    case ReplayAngle::kOrbit: {
        orbit = std::remainder(orbit + kOrbitRate * dt, 6.2831853f);
        const float c = std::cos(orbit), s = std::sin(orbit);
        // round the car's level heading: 0 behind it, a quarter turn its side
        const float side[3] = {heading[2], 0.0f, -heading[0]};
        for (int k = 0; k < 3; ++k) {
            eye[k] = p[k] + (-heading[k] * c + side[k] * s) * kOrbitRadius + up[k] * kOrbitUp;
            look[k] = p[k] + up[k] * 0.6f;
        }
        break;
    }
    case ReplayAngle::kTv: {
        // the way the car goes (its velocity when it moves, else its heading), level
        float dir[3] = {vel[0], 0.0f, vel[2]};
        if (dir[0] * dir[0] + dir[2] * dir[2] < 1.0f) dir[0] = heading[0], dir[2] = heading[2];
        norm3(dir);
        // placed again once the car is kTvPast beyond it (along the way it goes), or too far from it
        const float to[3] = {p[0] - tv[0], 0.0f, p[2] - tv[2]};
        const float past = to[0] * dir[0] + to[2] * dir[2];
        const float d2 = to[0] * to[0] + to[2] * to[2];
        const float aim[3] = {p[0], p[1] + 0.6f, p[2]};
        float h = 1.0f;
        if (tv_set && blocked && blocked(tv, aim, &h)) ++tv_hidden;
        else tv_hidden = 0;
        if (!tv_set || past > kTvPast || d2 > (kTvAhead * 2) * (kTvAhead * 2) || tv_hidden > kTvHiddenTicks) {
            tv_side = tv_set ? -tv_side : 1;
            const float side[3] = {dir[2], 0.0f, -dir[0]};
            // where it sees the car's way: this side, the other, nearer, then over the road; the first that sees the
            // car from where it will stand (its sight of the car now)
            const float tries[4][3] = {{kTvAhead, kTvSide * float(tv_side), kTvUp}, {kTvAhead, -kTvSide * float(tv_side), kTvUp},
                                       {kTvAhead * 0.6f, kTvSide * 0.6f * float(tv_side), kTvUp}, {kTvAhead * 0.6f, 0.0f, kTvUp + 3.0f}};
            int pick = 0;
            for (int q = 0; q < 4 && blocked; ++q) {
                float cand[3];
                for (int k = 0; k < 3; ++k) cand[k] = p[k] + dir[k] * tries[q][0] + side[k] * tries[q][1] + up[k] * tries[q][2];
                float hh = 1.0f;
                if (!blocked(cand, aim, &hh)) { pick = q; break; }
                pick = 3;
            }
            for (int k = 0; k < 3; ++k) tv[k] = p[k] + dir[k] * tries[pick][0] + side[k] * tries[pick][1] + up[k] * tries[pick][2];
            if (tries[pick][1] < 0) tv_side = -tv_side;
            tv_set = true;
            tv_hidden = 0;
        }
        for (int k = 0; k < 3; ++k) {
            eye[k] = tv[k];
            look[k] = p[k] + up[k] * 0.6f;
        }
        const float dx = p[0] - tv[0], dy = p[1] - tv[1], dz = p[2] - tv[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        zoom = std::clamp(kTvFill / std::max(dist, 1.0f), 0.08f, 1.0f);   // the car keeps its size as it comes and goes
        break;
    }
    }
}

ReplayCams& ReplayCams::instance() {
    static ReplayCams* c = new ReplayCams();   // never destroyed (the game thread may outlive static destruction)
    return *c;
}

void ReplayCams::angle_step(int dir, std::uint32_t poll) {
    std::lock_guard<std::mutex> l(m_);
    pending_.push_back({poll, false, dir < 0 ? -1 : 1});
}

void ReplayCams::car_step(int dir, std::uint32_t poll) {
    std::lock_guard<std::mutex> l(m_);
    pending_.push_back({poll, true, dir < 0 ? -1 : 1});
}

bool replay_on(const Tick& t) {
    static const bool force = [] { const char* e = std::getenv("B3_REPLAYCAMS_FORCE"); return e && *e && *e != '0'; }();
    // replayview's state 2: its replay playing (a results page, the hold after an early leave: 1 / 0, the cameras stop)
    return (t.replay == 2 || (force && t.score.valid)) && t.fe_car == 0 && t.cam.valid && !t.split;
}

void ReplayCams::on_tick(Tick& t) {
    // (diagnostics: $B3_REPLAYCAMS_FORCE=<1..5> also picks the player's angle: 1 Chase .. 5 TV; =<n>,<car> a rival)
    // (=cycle: the player's five angles in turn, 2 s each, from Chase)
    static const int force_angle = [] { const char* e = std::getenv("B3_REPLAYCAMS_FORCE"); return e ? (std::strncmp(e, "cycle", 5) == 0 ? -1 : std::atoi(e)) : 0; }();
    static const int force_car = [] { const char* e = std::getenv("B3_REPLAYCAMS_FORCE"); const char* c = e ? std::strchr(e, ',') : nullptr; return c ? std::atoi(c + 1) : 0; }();
    t.replay_cam = Tick::ReplayCamTick{};
    const bool on = replay_on(t);
    if (on != playing_.load()) {
        std::printf("[replaycams] poll %u: the replay %s\n", t.poll, on ? "plays: the viewer's cameras on" : "stopped: the viewer's cameras off");
        std::fflush(stdout);
        if (on_playing_) on_playing_(on);   // (photo mode's free camera may open while it plays)
    }
    if (t.cam.valid && !(t.cam.crash > 0.0f) && t.cam.chase >= 0.999f && t.cam.half_w > 0) { half_w_ = t.cam.half_w; half_h_ = t.cam.half_h; }
    playing_.store(on);
    std::lock_guard<std::mutex> l(m_);
    if (!on) {
        if (!label_key_.empty()) {
            label_history_.push_back({t.poll, {}});
            if (label_history_.size() > kLabelHistory) label_history_.erase(label_history_.begin());
        }
        view_.reset();
        pending_.clear();
        label_key_.clear();
        car_i_ = 0;   // the next replay starts on the player's car (the angles chosen are kept)
        return;
    }
    if (force_angle < 0) { car_i_ = 0; angle_i_[0] = 1 + int(t.poll / 120u % 5u); }
    // the race cars: the player's first, then the others in the capture's order
    std::vector<const Vehicle*> cars;
    for (const Vehicle& v : t.vehicles) if (v.kind == VehicleKind::kRaceCar && v.id == kPlayerRaceCar) cars.push_back(&v);
    for (const Vehicle& v : t.vehicles) if (v.kind == VehicleKind::kRaceCar && v.id != kPlayerRaceCar) cars.push_back(&v);
    if (cars.empty()) return;
    const int n = int(cars.size());
    car_i_ = std::min(car_i_, n - 1);
    if (force_angle > 0 && !forced_ && n > force_car) {   // (once all of the race cars are there)
        forced_ = true;
        car_i_ = force_car;
        angle_i_[car_i_ == 0 ? 0 : 1] = car_i_ == 0 ? force_angle % 6 : (force_angle - 1) % 3;
    }
    std::size_t done = 0;   // the presses due by this tick, in order (each angle step on the car watched then)
    for (; done < pending_.size() && pending_[done].poll <= t.poll; ++done) {
        const Press& p = pending_[done];
        if (p.car) { car_i_ = ((car_i_ + p.dir) % n + n) % n; continue; }
        const int li = car_i_ == 0 ? 0 : 1;
        const int m = li == 0 ? int(kPlayerAngles.size()) : int(kRivalAngles.size());
        angle_i_[li] = ((angle_i_[li] + p.dir) % m + m) % m;
    }
    pending_.erase(pending_.begin(), pending_.begin() + std::ptrdiff_t(done));
    const Vehicle& car = *cars[std::size_t(car_i_)];
    const bool player = car.id == kPlayerRaceCar;
    const ReplayAngle a = player ? kPlayerAngles[std::size_t(angle_i_[0])] : kRivalAngles[std::size_t(angle_i_[1] % 3)];
    driver_.clear();
    for (char16_t c : car.name) driver_ += c < 0x80 ? char(c) : '?';
    if (driver_.empty()) driver_ = player ? "YOU" : "RIVAL " + std::to_string(car_i_);
    if (static const bool trace = std::getenv("B3_REPLAYCAMS_TRACE") != nullptr; trace && t.poll % 60 == 0) {   // (diagnostics)
        std::printf("[replaycams] trace poll %u: %s at %.0f %.0f %.0f (replay %u)\n", t.poll, driver_.c_str(), double(car.body.r[3][0]),
                    double(car.body.r[3][1]), double(car.body.r[3][2]), unsigned(t.replay));
        std::fflush(stdout);
    }
    {   // the HUD line's change
        const std::string key = std::string(replay_angle_name(a)) + "|" + driver_;
        if (key != label_key_) {
            label_key_ = key;
            label_text_ = std::string(replay_angle_name(a)) + "  " + driver_;
            label_history_.push_back({t.poll, label_text_});
            if (label_history_.size() > kLabelHistory) label_history_.erase(label_history_.begin());
            ++label_serial_;
            label_poll_ = t.poll;
            std::printf("[replaycams] poll %u: %s, %s (car %08X, %d of %d, at %.0f %.0f %.0f)\n", t.poll, replay_angle_name(a), driver_.c_str(),
                        unsigned(car.id), car_i_ + 1, n, double(car.body.r[3][0]), double(car.body.r[3][1]), double(car.body.r[3][2]));
            std::fflush(stdout);
        }
    }
    const float dt = view_.on ? std::clamp(t.now - prev_now_, 0.0f, 0.25f) : 0.0f;
    prev_now_ = t.now;
    if (a == ReplayAngle::kGame) { view_.reset(); last_angle_ = a; return; }
    std::shared_ptr<const TrackRays> rays = rays_source_ ? rays_source_() : nullptr;
    const ReplayView::Blocked blocked = rays && !rays->empty() ? ReplayView::Blocked([&rays](const float* x, const float* y, float* f) { return rays->segment(x, y, f); })
                                                                : ReplayView::Blocked{};
    view_.step(a, car.id, dt, car.main, blocked);
    // a cut: another car or angle, or the TV point placed again
    const bool tv_moved = a == ReplayAngle::kTv && (!last_tv_set_ || last_tv_[0] != view_.tv[0] || last_tv_[2] != view_.tv[2]);
    if (a != last_angle_ || car.id != last_car_ || tv_moved) ++cut_;
    last_angle_ = a;
    last_car_ = car.id;
    last_tv_set_ = view_.tv_set;
    for (int k = 0; k < 3; ++k) last_tv_[k] = view_.tv[k];
    // REPLAY CAMERA CUTS Game: the game's crash camera shows (its share of the view, Camera::crash) as in the game
    static const bool force_stay = [] { const char* e = std::getenv("B3_REPLAYCAMS_STAY"); return e && *e == '1'; }();   // (diagnostics)
    if (!stay_.load() && !force_stay && t.cam.crash > 0.0f) return;
    t.replay_cam.on = true;
    t.replay_cam.cut = cut_;
    for (int k = 0; k < 3; ++k) { t.replay_cam.eye[k] = view_.eye[k]; t.replay_cam.look[k] = view_.look[k]; }
    t.replay_cam.zoom = view_.zoom;
    t.replay_cam.half_w = half_w_ > 0 ? half_w_ : t.cam.half_w;
    t.replay_cam.half_h = half_h_ > 0 ? half_h_ : t.cam.half_h;
}

std::uint32_t ReplayCams::label_serial(std::uint32_t* poll) const {
    std::lock_guard<std::mutex> l(const_cast<std::mutex&>(m_));
    if (poll) *poll = label_poll_;
    return label_serial_;
}

std::string ReplayCams::label_at(std::uint32_t poll, std::uint32_t* since) const {
    std::lock_guard<std::mutex> l(const_cast<std::mutex&>(m_));
    for (auto it = label_history_.rbegin(); it != label_history_.rend(); ++it)
        if (it->poll <= poll) {
            if (since) *since = it->poll;
            return it->text;
        }
    return {};
}

std::string ReplayCams::label() const {
    if (!playing_.load()) return {};
    std::lock_guard<std::mutex> l(const_cast<std::mutex&>(m_));
    return label_text_;
}

}  // namespace b3replay
