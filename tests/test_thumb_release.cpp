// paint3 (src/b3replay/thumb_release.h): a thumbnail-only car model gives back only the GPU ids nothing else names, and a
// picture job is given up after kThumbJobTicks.
#include "test.h"

#include "thumb_release.h"

#include <vector>

namespace {

using b3replay::GpuIds;

b3gfx::DrawItem item(b3gfx::BufferId v, b3gfx::BufferId i, b3gfx::TextureId t) {
    b3gfx::DrawItem d;
    d.vertices = v;
    d.indices = i;
    d.texture = t;
    return d;
}

TEST(own_ids_released) {
    GpuIds mine, other, globals;
    mine.add_items({item(10, 11, 20), item(10, 11, 21), item(10, 11, b3gfx::kNone)});
    mine.add_textures({20, 21, 22});
    other.add_items({item(30, 31, 40)});
    other.add_textures({40});
    globals.add_texture(1);
    globals.add_texture(2);
    std::size_t shared = 99;
    const GpuIds r = b3replay::releasable_ids(mine, {&other, &globals}, &shared);
    CHECK(shared == 0);
    CHECK(r.buffers == (std::set<b3gfx::BufferId>{10, 11}));
    CHECK(r.textures == (std::set<b3gfx::TextureId>{20, 21, 22}));
}

TEST(shared_ids_kept) {
    GpuIds mine, other, globals;
    mine.add_items({item(10, 11, 20), item(10, 11, 2)});   // texture 2: a car globals slot (wheels / glass / chrome)
    mine.add_textures({20, 21});
    other.add_items({item(30, 11, 21)});                   // buffer 11 and texture 21: another model's (another palette)
    globals.add_texture(2);
    std::size_t shared = 0;
    const GpuIds r = b3replay::releasable_ids(mine, {&other, &globals}, &shared);
    CHECK(shared == 3);
    CHECK(r.buffers == (std::set<b3gfx::BufferId>{10}));
    CHECK(r.textures == (std::set<b3gfx::TextureId>{20}));
}

TEST(job_expiry) {
    CHECK(!b3replay::thumb_job_expired(1000, 1000));
    CHECK(!b3replay::thumb_job_expired(1000, 1000 + b3replay::kThumbJobTicks - 1));
    CHECK(b3replay::thumb_job_expired(1000, 1000 + b3replay::kThumbJobTicks));
    CHECK(b3replay::thumb_job_expired(1000, 5000));
    CHECK(b3replay::thumb_job_expired(5000, 1000));   // the tick moved back (a seek)
    CHECK(!b3replay::thumb_job_expired(1000, 990));
}

}  // namespace

TEST_MAIN("thumbnail-only models' GPU ids, picture job expiry (paint3)")
