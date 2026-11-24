// trackrays.cpp -- ray tests against the route's walls (trackrays.h)
#include "trackrays.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <exception>

namespace b3replay {

bool TrackRays::keep(const float v[9]) {
    const float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]}, e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(l > 1e-8f)) return false;   // degenerate
    return std::fabs(n[1]) / l <= kFlat;
}

bool TrackRays::hit(const Tri& tri, const float a[3], const float d[3], float* t) {
    const float* v = tri.v;
    const float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]}, e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
    const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-9f) return false;
    const float inv = 1.0f / det;
    const float s[3] = {a[0] - v[0], a[1] - v[1], a[2] - v[2]};
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const float w = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (w < 0.0f || u + w > 1.0f) return false;
    const float tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    if (tt < 0.0f || tt > 1.0f) return false;
    *t = tt;
    return true;
}

bool TrackRays::covers(const float v[9], int cx, int cz) {
    // separating axes in x / z: the square's two, the triangle's three edge normals
    const float x0 = float(cx) * kCell, x1 = x0 + kCell, z0 = float(cz) * kCell, z1 = z0 + kCell;
    const float px[3] = {v[0], v[3], v[6]}, pz[3] = {v[2], v[5], v[8]};
    if (std::max({px[0], px[1], px[2]}) < x0 || std::min({px[0], px[1], px[2]}) > x1) return false;
    if (std::max({pz[0], pz[1], pz[2]}) < z0 || std::min({pz[0], pz[1], pz[2]}) > z1) return false;
    const float sx[4] = {x0, x1, x1, x0}, sz[4] = {z0, z0, z1, z1};
    for (int e = 0; e < 3; ++e) {
        const float nx = -(pz[(e + 1) % 3] - pz[e]), nz = px[(e + 1) % 3] - px[e];
        if (nx == 0.0f && nz == 0.0f) continue;   // (a vertical triangle's edge seen end-on)
        float tmin = 1e30f, tmax = -1e30f, smin = 1e30f, smax = -1e30f;
        for (int k = 0; k < 3; ++k) { const float d = nx * px[k] + nz * pz[k]; tmin = std::min(tmin, d); tmax = std::max(tmax, d); }
        for (int k = 0; k < 4; ++k) { const float d = nx * sx[k] + nz * sz[k]; smin = std::min(smin, d); smax = std::max(smax, d); }
        if (tmax < smin || smax < tmin) return false;
    }
    return true;
}

void TrackRays::build(std::vector<Tri> tris) {
    tris_ = std::move(tris);
    cells_.clear();
    stamp_.assign(tris_.size(), 0);
    query_ = 0;
    for (std::uint32_t i = 0; i < tris_.size(); ++i) {
        const float* v = tris_[i].v;
        const float x0 = std::min({v[0], v[3], v[6]}), x1 = std::max({v[0], v[3], v[6]});
        const float z0 = std::min({v[2], v[5], v[8]}), z1 = std::max({v[2], v[5], v[8]});
        const int cx0 = int(std::floor(x0 / kCell)), cx1 = int(std::floor(x1 / kCell));
        const int cz0 = int(std::floor(z0 / kCell)), cz1 = int(std::floor(z1 / kCell));
        // a large triangle (a long wall, a tunnel's side) goes in the squares its x / z shape covers, not its whole box
        const bool big = (cx1 - cx0 + 1) * (cz1 - cz0 + 1) > 16;
        if (std::int64_t(cx1 - cx0 + 1) * std::int64_t(cz1 - cz0 + 1) > 1000000) continue;   // (not a track's)
        for (int cx = cx0; cx <= cx1; ++cx)
            for (int cz = cz0; cz <= cz1; ++cz)
                if (!big || covers(v, cx, cz)) cells_[key(cx, cz)].push_back(i);
    }
}

bool TrackRays::segment(const float a[3], const float b[3], float* t) const {
    if (tris_.empty()) return false;
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    // the squares the segment's x / z line crosses (DDA), each once
    int cx = int(std::floor(a[0] / kCell)), cz = int(std::floor(a[2] / kCell));
    const int ex = int(std::floor(b[0] / kCell)), ez = int(std::floor(b[2] / kCell));
    const int sx = d[0] > 0 ? 1 : -1, sz = d[2] > 0 ? 1 : -1;
    const float inf = 1e30f;
    float tmx = std::fabs(d[0]) > 1e-9f ? ((float(cx + (sx > 0 ? 1 : 0)) * kCell) - a[0]) / d[0] : inf;
    float tmz = std::fabs(d[2]) > 1e-9f ? ((float(cz + (sz > 0 ? 1 : 0)) * kCell) - a[2]) / d[2] : inf;
    const float tdx = std::fabs(d[0]) > 1e-9f ? kCell / std::fabs(d[0]) : inf, tdz = std::fabs(d[2]) > 1e-9f ? kCell / std::fabs(d[2]) : inf;
    float best = 2.0f;
    if (++query_ == 0) {   // (the counter wrapped: every stamp is old)
        std::fill(stamp_.begin(), stamp_.end(), 0u);
        query_ = 1;
    }
    for (int guard = 0; guard < 256; ++guard) {
        if (const auto it = cells_.find(key(cx, cz)); it != cells_.end())
            for (std::uint32_t i : it->second) {
                if (stamp_[i] == query_) continue;   // (in a square tested before this query)
                stamp_[i] = query_;
                float h;
                if (hit(tris_[i], a, d, &h) && h < best) best = h;
            }
        if (cx == ex && cz == ez) break;
        if (tmx < tmz) {
            if (tmx > best || tmx > 1.0f) break;
            cx += sx;
            tmx += tdx;
        } else {
            if (tmz > best || tmz > 1.0f) break;
            cz += sz;
            tmz += tdz;
        }
    }
    if (best > 1.0f) return false;
    *t = best;
    return true;
}

void TrackRaysJob::start(std::function<std::vector<TrackRays::Tri>()> collect) {
    reset();
    started_ = true;
    job_ = std::async(std::launch::async, [collect = std::move(collect)] {
        const auto t0 = std::chrono::steady_clock::now();
        auto r = std::make_shared<TrackRays>();
        try {
            std::vector<TrackRays::Tri> tris = collect();
            const auto t1 = std::chrono::steady_clock::now();
            r->build(std::move(tris));
            r->ms[0] = std::chrono::duration<double, std::milli>(t1 - t0).count();
            r->ms[1] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        } catch (const std::exception& e) {   // (out of memory: no walls rather than no game)
            std::fprintf(stderr, "[orbitcam] the route's walls not built (%s): the orbit camera does not stop at walls\n", e.what());
            r = std::make_shared<TrackRays>();
        }
        return r;
    });
}

const TrackRays* TrackRaysJob::get() {
    if (!started_) return nullptr;
    if (!rays_ && job_.valid()) {
        const auto w0 = std::chrono::steady_clock::now();
        rays_ = job_.get();
        const double waited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count();
        std::printf("[orbitcam] the route's walls for the orbit camera: %zu triangles (collected in %.0f ms, the grid in %.0f ms; waited %.0f ms)\n",
                    rays_ ? rays_->triangles() : std::size_t(0), rays_ ? rays_->ms[0] : 0.0, rays_ ? rays_->ms[1] : 0.0, waited);
        std::fflush(stdout);
    }
    return rays_.get();
}

void TrackRaysJob::reset() {
    if (job_.valid()) job_.wait();   // (the worker reads the scene that is about to go)
    job_ = {};
    rays_.reset();
    started_ = false;
}

}  // namespace b3replay
