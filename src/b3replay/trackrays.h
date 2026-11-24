// trackrays.h -- ray tests against the route's walls (orbitcam2, 2026-11-21): the orbit camera (orbitcam.h) stops a
// margin before a wall or barrier instead of looking through it.
//
// Renderer only: the triangles are the drawn track's (View::hi: the units' high meshes and their placed instances, as
// the renderer loaded them from STATIC.DAT / STREAMED.DAT), not the game's collision world (CB3StreamedTrack's
// LOD-and-collision blocks, which CB3CameraUtils::CollisionResolveCameraWorld 0x2F6F60 tests the game's own cameras
// against: guest state the renderer does not read). The drawn walls are the collision walls closely enough for a
// camera: kept are the opaque triangles that are not near-horizontal (|normal.y| <= kFlat: walls, barriers, buildings,
// signs, pillars; not the road, the pavements or the roofs), left out the alpha-tested and blended draws (foliage,
// fences' see-through parts, glass, decals) and the props (they move: cones, signs knocked over), the backdrops (far
// away) and the crash barriers (shown only in some events).
//
// A uniform grid of kCell-metre squares in x / z holds each triangle in the squares its x / z box covers; a segment test
// walks the squares the segment crosses (2D DDA) and tests their triangles (both sides), the nearest hit kept.
// Built on a worker as the track loads (View::load_prepared); a query waits for it if it is not done yet (the first
// look of a race; offscreen shots stay the same however fast the build was).
#ifndef B3REPLAY_TRACKRAYS_H
#define B3REPLAY_TRACKRAYS_H

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <unordered_map>
#include <vector>

namespace b3replay {

class TrackRays {
public:
    static constexpr float kCell = 4.0f;    // metres
    static constexpr float kFlat = 0.8f;    // |normal.y| above this: a floor or a ceiling, left out

    struct Tri { float v[9]; };   // world space: three corners

    // The grid of `tris` (replaces the current one)
    void build(std::vector<Tri> tris);
    void clear() { *this = TrackRays{}; }
    bool empty() const { return tris_.empty(); }
    std::size_t triangles() const { return tris_.size(); }
    double ms[2] = {0, 0};   // the build's times (the triangles collected, the grid; the log)
    // The nearest hit on the segment a -> b: its fraction 0..1 of the way (true), or false when nothing is in the way.
    // One thread at a time (the render thread): a query stamps the triangles it has tested
    bool segment(const float a[3], const float b[3], float* t) const;

    // The near-vertical test of one triangle (the build's filter)
    static bool keep(const float v[9]);
    // Moller-Trumbore, both sides: the hit's fraction of the segment a + t (b - a), 0 <= t <= 1
    static bool hit(const Tri& tri, const float a[3], const float d[3], float* t);

private:
    static std::int64_t key(int cx, int cz) { return (std::int64_t(cx) << 32) ^ std::int64_t(std::uint32_t(cz)); }
    std::vector<Tri> tris_;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> cells_;
    mutable std::vector<std::uint32_t> stamp_;   // per triangle: the query that last tested it (each tested once a query)
    mutable std::uint32_t query_ = 0;
    // whether triangle `v`'s x / z shape overlaps square (cx, cz)
    static bool covers(const float v[9], int cx, int cz);
};

// The route's TrackRays as a worker builds them: start() at the track's load with the triangles' source, get() the
// grid (waiting for the build), reset() before the scene they read goes away
class TrackRaysJob {
public:
    void start(std::function<std::vector<TrackRays::Tri>()> collect);
    const TrackRays* get();   // null when none was started (waits for the build: some 30 ms of CPU after the load)
    std::shared_ptr<const TrackRays> shared() { get(); return rays_; }
    void reset();
    ~TrackRaysJob() { reset(); }

private:
    std::future<std::shared_ptr<TrackRays>> job_;
    std::shared_ptr<TrackRays> rays_;
    bool started_ = false;
};

}  // namespace b3replay

#endif
