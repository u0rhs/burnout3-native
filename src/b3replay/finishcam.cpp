// finishcam.cpp -- FINISH CAMERA (finishcam.h)
#include "finishcam.h"

#include "b3asset/discfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace b3replay {

// The shots' camera offsets in VDB.XML (keys as the file stores them, unsigned), set piece by set piece. Found by
// matching: no retail code registers these keys; their names are not known.
const std::uint32_t FinishShots::kShotKeys[kShots] = {
    0xFBE9D2E7u, 0xE2ED7A71u, 0xB46E3FDFu, 0x666FBCE2u, 0xE4FA0E4Cu, 0x0BD5E061u, 0x524536FEu, 0xD9D4635Cu, 0x8044B5C3u,
    0x7B8159FBu, 0x8BD9D0ECu, 0x999C5461u, 0x4F605496u, 0x4B9DD75Cu, 0xC80E62ACu, 0xD71DBE07u, 0x0CAE5C71u, 0x64928550u,
    0xB982E84Au, 0x6B836B77u, 0xA0E3D995u, 0x5604D2ACu, 0x72E25AA8u, 0x84055191u, 0x46C8D0B4u, 0xC1681073u, 0x94C95389u,
    0xC472EFA6u, 0x16736C9Bu, 0xCD53DB04u, 0x2259E687u, 0x7EDCE053u, 0xF05865BAu, 0xE46C538Du, 0x61CF3CF8u, 0x366DD0B0u,
};
const std::uint8_t FinishShots::kSetPieceShots[kSetPieces] = {2, 3, 4, 2, 4, 2, 3, 4, 3, 3, 3, 3};

namespace {
std::uint32_t rd32(const std::vector<std::uint8_t>& d, std::size_t o) {
    std::uint32_t v;
    std::memcpy(&v, d.data() + o, 4);
    return v;
}
float rdf(const std::vector<std::uint8_t>& d, std::size_t o) {
    float v;
    std::memcpy(&v, d.data() + o, 4);
    return v;
}
void norm(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) for (int k = 0; k < 3; ++k) v[k] /= l;
}
}  // namespace

bool FinishShots::parse(const std::vector<std::uint8_t>& d) {
    found = 0;
    // header: version, value records, (unused), file records, the file records' offset; value records {u32 value,
    // s32 key} from 0x14; a vector's value is the offset of its 16 bytes in the blocks between them
    if (d.size() < 0x14) return false;
    const std::uint32_t n = rd32(d, 4), files = rd32(d, 0x10);
    if (n == 0 || 0x14 + std::uint64_t(n) * 8 > files || files > d.size()) return false;
    const std::uint32_t blocks_lo = 0x14 + n * 8;
    for (int i = 0; i < kShots; ++i) {
        // the records are sorted by signed key: a binary search
        const std::int32_t key = std::int32_t(kShotKeys[i]);
        std::uint32_t lo = 0, hi = n;
        while (lo < hi) {
            const std::uint32_t mid = lo + (hi - lo) / 2;
            const std::int32_t k = std::int32_t(rd32(d, 0x14 + std::size_t(mid) * 8 + 4));
            if (k < key) lo = mid + 1;
            else hi = mid;
        }
        if (lo >= n || std::int32_t(rd32(d, 0x14 + std::size_t(lo) * 8 + 4)) != key) continue;
        const std::uint32_t at = rd32(d, 0x14 + std::size_t(lo) * 8);
        if (at < blocks_lo || at + 12 > files || (at & 3) != 0) continue;
        bool sane = true;
        for (int k = 0; k < 3; ++k) {
            offset[i][k] = rdf(d, at + 4 * std::size_t(k));
            sane = sane && std::isfinite(offset[i][k]) && std::fabs(offset[i][k]) < 500.0f;
        }
        if (sane) ++found;
    }
    return ok();
}

void FinishShots::at(float t, int& set_piece, int& shot, float& into) {
    if (!(t >= 0.0f)) t = 0.0f;
    const int round = int(t / kSetPieceSeconds);
    set_piece = round % kSetPieces;
    const float in_piece = t - float(round) * kSetPieceSeconds;
    int first = 0;
    for (int k = 0; k < set_piece; ++k) first += kSetPieceShots[k];
    const int n = kSetPieceShots[set_piece];
    const float each = kSetPieceSeconds / float(n);
    const int j = std::min(n - 1, int(in_piece / each));
    shot = first + j;
    into = in_piece - float(j) * each;
}

void FinishView::step(const FinishShots& shots, float dt, const Mat34& car) {
    if (!on) {
        on = true;
        t = 0;
        shot = -1;
        float x[3] = {car.r[0][0], 0.0f, car.r[0][2]}, z[3] = {car.r[2][0], 0.0f, car.r[2][2]};
        norm(x);
        norm(z);
        for (int k = 0; k < 3; ++k) {
            start[k] = car.r[3][k];
            ax[k] = x[k];
            az[k] = z[k];
        }
    }
    t += std::max(dt, 0.0f);
    int sp = 0, s = 0;
    float into = 0;
    FinishShots::at(t, sp, s, into);
    const float rx = car.r[3][0] - start[0], rz = car.r[3][2] - start[2];
    roam = std::sqrt(rx * rx + rz * rz);
    float aim[3];
    for (int k = 0; k < 3; ++k) aim[k] = (follow || roam <= kFollow ? car.r[3][k] : start[k]) + (k == 1 ? kAim : 0.0f);
    if (follow) {   // camfixes: point-to-point: the shot travels with the car
        if (s != shot) {   // a cut: the shot's frame from the car's level heading now
            float x[3] = {car.r[0][0], 0.0f, car.r[0][2]}, z[3] = {car.r[2][0], 0.0f, car.r[2][2]};
            norm(x);
            norm(z);
            for (int k = 0; k < 3; ++k) { ax[k] = x[k]; az[k] = z[k]; }
        }
        const float* o = shots.offset[s];
        for (int k = 0; k < 3; ++k) eye[k] = car.r[3][k] + ax[k] * o[0] + az[k] * o[2] + (k == 1 ? o[1] : 0.0f);
    }
    if (s != shot) {   // a cut: the shot's camera, aimed at once
        shot = s;
        const float* o = shots.offset[s];
        for (int k = 0; k < 3; ++k) {
            if (!follow) eye[k] = start[k] + ax[k] * o[0] + az[k] * o[2] + (k == 1 ? o[1] : 0.0f);
            look[k] = aim[k];
        }
    } else {
        const float fa = std::min(1.0f, kAimRate * dt);
        for (int k = 0; k < 3; ++k) look[k] += (aim[k] - look[k]) * fa;
    }
    const float d = std::sqrt((look[0] - eye[0]) * (look[0] - eye[0]) + (look[1] - eye[1]) * (look[1] - eye[1]) +
                              (look[2] - eye[2]) * (look[2] - eye[2]));
    zoom = d > kZoomFrom ? std::max(kMinZoom, kZoomFrom / d) : 1.0f;
}

void finish_camera(Camera& c, const float eye[3], const float look[3], float zoom) {
    float at[3] = {look[0] - eye[0], look[1] - eye[1], look[2] - eye[2]};
    norm(at);
    // the game's row 0 is the screen's left: up x at
    float left[3] = {at[2], 0.0f, -at[0]};
    if (left[0] * left[0] + left[2] * left[2] < 1e-8f) left[0] = 1.0f;   // straight down / up: any level row
    norm(left);
    const float up[3] = {at[1] * left[2] - at[2] * left[1], at[2] * left[0] - at[0] * left[2], at[0] * left[1] - at[1] * left[0]};
    for (int k = 0; k < 3; ++k) {
        c.m.r[0][k] = left[k];
        c.m.r[1][k] = up[k];
        c.m.r[2][k] = at[k];
        c.m.r[3][k] = eye[k];
        c.lod_pos[k] = eye[k];
    }
    c.half_w *= zoom;
    c.half_h *= zoom;
    c.chase = 0.0f;
}

FinishCam& FinishCam::instance() {
    static FinishCam* f = new FinishCam();   // never destroyed (the game thread may outlive static destruction)
    return *f;
}

void FinishCam::set_level(int level) {
    static const int env = [] { const char* e = std::getenv("B3_FINISH_CAMERA"); return e && (*e == '0' || *e == '1') ? *e - '0' : -1; }();
    if (env >= 0) level = env;
    level_.store(level == 1 ? 1 : 0);
}

void FinishCam::load(const std::string& game) {
    if (loaded_) return;
    loaded_ = true;
    std::vector<std::uint8_t> d;
    const std::filesystem::path p = b3asset::disc::resolve_ci(std::filesystem::path(game), "DATA/VDB.XML");
    if (!b3asset::disc::read_file(p, d)) {
        std::printf("[finishcam] no DATA/VDB.XML in %s: FINISH CAMERA stays the game's\n", game.c_str());
        std::fflush(stdout);
        return;
    }
    load_vdb(d);
}

void FinishCam::load_vdb(const std::vector<std::uint8_t>& d) {
    loaded_ = true;
    shots_.parse(d);
    std::printf("[finishcam] VDB.XML: %d of %d set-piece shots%s\n", shots_.found, FinishShots::kShots,
                shots_.ok() ? "" : " (not this disc's file: FINISH CAMERA stays the game's)");
    std::fflush(stdout);
}

void FinishCam::on_tick(Tick& t) {
    static const bool log = [] { const char* e = std::getenv("B3_FINISHCAM_LOG"); return e && *e && *e != '0'; }();
    static std::uint32_t last_state = 0;
    if (log && t.cam_state != last_state) {   // (checks: the camera manager's state as it changes)
        std::printf("[finishcam] poll %u: camera state %08X -> %08X (score finish %d)\n", t.poll, last_state, t.cam_state, t.score.finish);
        std::fflush(stdout);
        last_state = t.cam_state;
    }
    t.finish_cam = Tick::FinishCamTick{};
    // camfixes: where the player's car waits for the start (the countdown, state 0 / 1, before the race time runs)
    if (t.score.valid && (t.score.state == 0 || t.score.state == 1) && t.fe_car == 0)
        for (const Vehicle& v : t.vehicles)
            if (v.id == kPlayerRaceCar && v.kind == VehicleKind::kRaceCar) {
                for (int k = 0; k < 3; ++k) start_pos_[k] = v.main.r[3][k];
                have_start_ = true;
                break;
            }
    if (t.fe_car != 0) have_start_ = false;   // (the front end: the next race's start comes)
    const Vehicle* car = nullptr;
    // the game's finish state, and the race's score finished (1, or 4 the other finish; not an out, 2 / 3): the state's
    // vtable alone is the decomp names table's CB3PlayerCameraStateInGame, so it is not taken for a finish by itself
    // (Road Rage's end is not a finish line: its logics, __vt__23CB3OfflineRoadRageLogic 0x4DEE00 and
    // __vt__26CB3OnlineRoadRageRaceLogic 0x4DEAA0, keep the game's shot)
    const bool road_rage = t.score.logic == 0x4DEE00u || t.score.logic == 0x4DEAA0u;
    const bool finished = t.score.valid && (t.score.finish == 1 || t.score.finish == 4) && !road_rage;
    if (level_.load() == 1 && shots_.ok() && finished && !t.split && t.fe_car == 0 && t.cam.valid && t.cam_state == kFinishCameraState)
        for (const Vehicle& v : t.vehicles)
            if (v.id == kPlayerRaceCar && v.kind == VehicleKind::kRaceCar) { car = &v; break; }
    if (car && p2p_done_) return;   // camfixes: this finish is the game's shot from here
    if (!car) {
        p2p_done_ = false;
        if (view_.on) {
            std::printf("[finishcam] poll %u: the game's camera again (state %08X)\n", t.poll, t.cam_state);
            std::fflush(stdout);
        }
        view_.reset();
        logged_shot_ = -1;
        return;
    }
    const float dt = view_.on ? std::clamp(t.now - prev_now_, 0.0f, 0.25f) : 0.0f;
    prev_now_ = t.now;
    if (!view_.on) {   // camfixes: the finish: point-to-point when the line is far from where the car started
        const float dx = car->main.r[3][0] - start_pos_[0], dz = car->main.r[3][2] - start_pos_[2];
        const float far = std::sqrt(dx * dx + dz * dz);
        view_.follow = have_start_ && far > FinishView::kPointToPoint;
        std::printf("[finishcam] poll %u: the finish %.0f m from the start: %s\n", t.poll, double(far),
                    !have_start_ ? "no start seen (the line watched)" : view_.follow ? "point to point, the set pieces follow the car" : "a circuit, the set pieces watch the line");
        std::fflush(stdout);
    }
    view_.step(shots_, dt, car->main);
    if (view_.follow && view_.roam > FinishView::kFollowEnd) {   // camfixes: past the end of the route's world
        std::printf("[finishcam] poll %u: point to point: the car %.0f m past the line, the game's finish shot from here\n", t.poll, double(view_.roam));
        std::fflush(stdout);
        p2p_done_ = true;
        view_.reset();
        logged_shot_ = -1;
        return;
    }
    t.finish_cam.on = true;
    t.finish_cam.shot = view_.shot;
    t.finish_cam.zoom = view_.zoom;
    for (int k = 0; k < 3; ++k) {
        t.finish_cam.eye[k] = view_.eye[k];
        t.finish_cam.look[k] = view_.look[k];
    }
    if (view_.shot != logged_shot_) {
        int sp = 0, s = 0;
        float into = 0;
        FinishShots::at(view_.t, sp, s, into);
        std::printf("[finishcam] poll %u: set piece %d shot %d (%.1f s after the finish; offset %.2f %.2f %.2f; the car %.1f m from the finish)\n",
                    t.poll, sp + 1, view_.shot + 1, double(view_.t), double(shots_.offset[view_.shot][0]), double(shots_.offset[view_.shot][1]),
                    double(shots_.offset[view_.shot][2]), double(view_.roam));
        std::fflush(stdout);
        logged_shot_ = view_.shot;
    }
}

}  // namespace b3replay
