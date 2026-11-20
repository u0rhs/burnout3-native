// perf4: FRAMES IN FLIGHT Auto's decisions (include/b3gfx/frames_auto.h) on synthetic frame times, the warm-up list's file
// (src/b3replay/warmcache.h) and RENDER SCALE's supersampling cap (include/b3gfx/render_scale.h)
#include "test.h"

#include "b3gfx/frames_auto.h"
#include "b3gfx/render_scale.h"
#include "warmcache.h"

using b3gfx::FramesAuto;

namespace {
// up to `secs` seconds of frames of `ms` with `wait` of GPU waits each, stopping at the first change: the frames in flight
// it changed to (0: none) and when (`at`, s)
int run(FramesAuto& a, double secs, double ms, double wait, double target, double* at = nullptr) {
    for (double t = 0; t < secs * 1000.0; t += ms) {
        FramesAuto::Change c;
        if (a.frame(ms, wait, target, c)) {
            a.set_current(c.to);
            if (at) *at = t / 1000.0;
            return c.to;
        }
    }
    return 0;
}
}  // namespace

TEST(fif_holds_target_stays_at_one) {
    FramesAuto a;
    CHECK(run(a, 30, 16.6, 10.0, 16.67) == 0 && a.current() == 1);   // vsync at 60: the waits are the display's, the target holds
    FramesAuto b;
    CHECK(run(b, 30, 5.0, 3.0, 6.94) == 0 && b.current() == 1);      // 200 fps against a 144 Hz target
}

TEST(fif_gpu_bound_goes_to_two_then_probes) {
    FramesAuto a;
    double at = 0;
    CHECK(run(a, 5, 25.0, 12.0, 16.67, &at) == 2 && a.current() == 2 && at < 2.5);   // 40 fps, half the frame waiting: 2 in ~2 s
    // with 2 the target holds: after ~5 s it tries 1 again
    CHECK(run(a, 10, 16.6, 8.0, 16.67, &at) == 1 && a.current() == 1 && at > 4.0 && at < 7.0);
    // 1 fails again at once: back to 2, and the next probe waits twice as long
    CHECK(run(a, 5, 25.0, 12.0, 16.67, &at) == 2 && at < 3.5);
    CHECK(run(a, 20, 16.6, 8.0, 16.67, &at) == 1 && at > 9.0 && at < 12.0);
}

TEST(fif_cpu_bound_stays_or_returns) {
    FramesAuto a;
    CHECK(run(a, 10, 25.0, 1.0, 16.67) == 0 && a.current() == 1);   // slow but no GPU wait: the CPU's own work
    FramesAuto b;
    CHECK(run(b, 5, 25.0, 12.0, 16.67) == 2);
    CHECK(run(b, 6, 25.0, 1.0, 16.67) == 1 && b.current() == 1);      // 2 brought nothing: back to 1
    CHECK(run(b, 8, 25.0, 12.0, 16.67) == 0);                         // and not again for a while (the cool-down)
    CHECK(run(b, 10, 25.0, 12.0, 16.67) == 2);
}

TEST(fif_ignores_stalls_and_no_target) {
    FramesAuto a;
    CHECK(run(a, 10, 600.0, 590.0, 16.67) == 0);   // load stalls: not frames of the game's pace
    CHECK(run(a, 10, 25.0, 20.0, 0.0) == 0 && a.current() == 1);   // no target known
}

TEST(warm_cache_round_trip) {
    b3replay::warm::Cache c;
    c.overlay = {0x44, 0x248, 0xFFF};
    c.textures = {"fxskid", "corona01"};
    const auto back = b3replay::warm::from_text(b3replay::warm::to_text(c));
    CHECK(back.overlay == c.overlay && back.textures == c.textures);
    const auto bad = b3replay::warm::from_text("overlay 1000\noverlay zz\noverlay 42x\ntexture \ntexture a b\nnoise\r\noverlay 2a\r\ntexture ok\r\n");
    CHECK(bad.overlay.size() == 1 && bad.overlay.count(0x2A) && bad.textures.size() == 1 && bad.textures.count("ok"));
    // at most kMaxEntries of each, read and written
    b3replay::warm::Cache big;
    std::string text;
    for (int k = 0; k < 600; ++k) { big.overlay.insert(k); big.textures.insert("t" + std::to_string(k)); text += "texture n" + std::to_string(k) + "\n"; }
    const auto capped = b3replay::warm::from_text(b3replay::warm::to_text(big));
    CHECK(capped.overlay.size() == b3replay::warm::kMaxEntries && capped.textures.size() == b3replay::warm::kMaxEntries);
    CHECK(b3replay::warm::from_text(text).textures.size() == b3replay::warm::kMaxEntries);
}

TEST(supersample_cap) {
    using b3gfx::capped_scale;
    using b3gfx::scaled_bytes_per_pixel;
    const double b4 = scaled_bytes_per_pixel(4), b8 = scaled_bytes_per_pixel(8), b1 = scaled_bytes_per_pixel(1);
    CHECK(b1 == 12 + 16 + 5 + 4 + 2 + 8 && b4 == 48 + 8 + 16 + 5 + 4 + 2 + 8 && scaled_bytes_per_pixel(8, true) == b8 + 8);
    CHECK(scaled_bytes_per_pixel(4, false, true) == b4 + 16 && scaled_bytes_per_pixel(1, true, true) == b1 + 16);   // finalfix: photo blur
    CHECK(b3gfx::sun_shadow_map_mb(2048) == 32.0 && b3gfx::sun_shadow_map_mb(1024) == 8.0);                          // the shadow map
    CHECK(capped_scale(0.75f, 1920, 1080, b4, 2048, 16384) == 0.75f);   // below 1: as set
    CHECK(capped_scale(1.0f, 1920, 1080, b4, 2048, 16384) == 1.0f);
    CHECK(capped_scale(2.0f, 1920, 1080, b4, 2048, 16384) == 2.0f);     // 3840 x 2160 MSAA 4: ~690 MB
    CHECK(capped_scale(2.0f, 1920, 1080, b4, 512, 16384) < 2.0f);
    const float s = capped_scale(2.0f, 5120, 1440, b8, 2048, 16384);   // 10240 x 2880 MSAA 8 would be ~3.6 GB
    CHECK(s > 1.0f && s < 2.0f);
    CHECK(double(b3gfx::scaled_side(5120, s)) * b3gfx::scaled_side(1440, s) * b8 <= 2048.0 * 1048576.0 * 1.001);
    CHECK(capped_scale(2.0f, 9000, 2000, b1, 0, 16384) <= 16384.0f / 9000.0f);   // the side limit
    CHECK(capped_scale(2.0f, 16384, 1000, b1, 0, 16384) == 1.0f);               // nothing above 100 % fits
    CHECK(b3gfx::scaled_side(1280, 1.25f) == 1600 && b3gfx::scaled_side(720, 1.5f) == 1080);
    // the budget from the card: a quarter of its memory within 512 MB .. 4 GB, 1 GB unknown
    CHECK(b3gfx::supersample_budget_mb(0) == 1024 && b3gfx::supersample_budget_mb(16376) == 4094 &&
          b3gfx::supersample_budget_mb(2048) == 512 && b3gfx::supersample_budget_mb(65536) == 4096);
}

TEST_MAIN("perf4: frames in flight Auto, warm-up list, supersampling cap")
