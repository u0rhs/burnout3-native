// thumb_release.h -- paint3: which GPU ids of a car model loaded only for thumbnails may be given back (View::
// thumb_models_release), and when a thumbnail's picture job is given up.
//
// A model's buffers and textures are its own (load_car_model -> build_scene without `share`; Device::create_* never hands
// out an id twice), so nothing should be named by anything else; the check keeps it so: an id any other loaded model, the
// car globals or the showroom names is kept (and reported), never released.
#ifndef B3REPLAY_THUMB_RELEASE_H
#define B3REPLAY_THUMB_RELEASE_H

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "b3gfx/b3gfx.h"

namespace b3replay {

struct GpuIds {
    std::set<b3gfx::BufferId> buffers;
    std::set<b3gfx::TextureId> textures;
    void add_items(const std::vector<b3gfx::DrawItem>& items) {
        for (const b3gfx::DrawItem& d : items) {
            if (d.vertices != b3gfx::kNone) buffers.insert(d.vertices);
            if (d.indices != b3gfx::kNone) buffers.insert(d.indices);
            if (d.texture != b3gfx::kNone) textures.insert(d.texture);
        }
    }
    void add_textures(const std::vector<b3gfx::TextureId>& ts) {
        for (const b3gfx::TextureId t : ts) if (t != b3gfx::kNone) textures.insert(t);
    }
    void add_texture(b3gfx::TextureId t) { if (t != b3gfx::kNone) textures.insert(t); }
};

// The ids of `mine` that nothing in `others` names; `shared`: how many were named elsewhere (kept)
inline GpuIds releasable_ids(const GpuIds& mine, const std::vector<const GpuIds*>& others, std::size_t* shared = nullptr) {
    GpuIds out;
    std::size_t n = 0;
    for (const b3gfx::BufferId b : mine.buffers) {
        bool named = false;
        for (const GpuIds* o : others) named = named || (o && o->buffers.count(b));
        if (named) ++n;
        else out.buffers.insert(b);
    }
    for (const b3gfx::TextureId t : mine.textures) {
        bool named = false;
        for (const GpuIds* o : others) named = named || (o && o->textures.count(t));
        if (named) ++n;
        else out.textures.insert(t);
    }
    if (shared) *shared = n;
    return out;
}

// A picture job not done this many ticks after it was begun is given up (its fence never passes: a lost device): 3 s.
// (Ticks apart either way: a replay's seek moves the tick back.)
constexpr std::uint64_t kThumbJobTicks = 90;
inline bool thumb_job_expired(std::uint64_t begun, std::uint64_t now) { return (now >= begun ? now - begun : begun - now) >= kThumbJobTicks; }

}  // namespace b3replay

#endif
