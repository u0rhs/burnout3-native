// perf4: FRAMES IN FLIGHT Auto's decisions (include/b3gfx/frames_auto.h) on synthetic frame times, the warm-up list's file
// (src/b3replay/warmcache.h) and RENDER SCALE's supersampling cap (include/b3gfx/render_scale.h)
#include "test.h"

#include <cmath>
#include <vector>

#include "b3gfx/frames_auto.h"
#include "b3gfx/render_scale.h"
#include "b3gfx/scene_depth.h"
#include "b3gfx/upload_batch.h"
#include "b3gfx/driver_order.h"
#include "b3gfx/b3gfx.h"
#include "warmcache.h"
#include "perfbisect.h"

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

TEST(fif_gpu_bound_goes_to_two_and_stays_without_room) {
    FramesAuto a;
    double at = 0;
    CHECK(run(a, 5, 25.0, 12.0, 16.67, &at) == 2 && a.current() == 2 && at < 2.5);   // 40 fps, half the frame waiting: 2 in ~2 s
    // with 2 the target holds, but only just (no ample room): 1 is not tried again, however long
    CHECK(run(a, 600, 16.6, 8.0, 16.67) == 0 && a.current() == 2);
}

// The Mac report (1.3.0, M1 Max, 120 Hz, present immediate): at 1 ~9 ms frames (111 fps, 78 % GPU waits) against the
// 8.33 ms target; at 2 ~5.3 ms (190 fps). The old controller tried 1 every few seconds (22 changes in ten minutes).
TEST(fif_mac_report_settles_at_two) {
    FramesAuto a;
    int changes = 0;
    for (double t = 0; t < 600.0 * 1000.0;) {   // ten minutes of the scene: the frame time follows the frames in flight
        const double ms = a.current() == 1 ? 9.0 : 5.3, wait = a.current() == 1 ? 7.0 : 3.7;
        FramesAuto::Change c;
        if (a.frame(ms, wait, 8.333, c)) { a.set_current(c.to); ++changes; }
        t += ms;
    }
    CHECK(changes == 1 && a.current() == 2);   // once to 2, and it stays: 1 is expected to take 5.3 x 1.7 = 9 ms
    CHECK(a.ratio() > 1.6 && a.ratio() < 1.8);
}

// A scene that does allow 1 sometimes (2 with ample room, 1 still failing): the retries back off, no flapping
TEST(fif_failed_retries_back_off) {
    FramesAuto a;
    int changes = 0, retries = 0;
    for (double t = 0; t < 1800.0 * 1000.0;) {   // half an hour
        const double ms = a.current() == 1 ? 9.0 : 4.0, wait = a.current() == 1 ? 7.0 : 2.0;   // ratio 2.25 measured...
        FramesAuto::Change c;
        // ... but the scene gets lighter at 2 now and then (3 ms: 3 x 2.25 = 6.75 fits), so retries are allowed
        const double use = a.current() == 2 && std::fmod(t / 1000.0, 200.0) > 100.0 ? 3.0 : ms;
        if (a.frame(use, wait, 8.333, c)) {
            a.set_current(c.to);
            ++changes;
            if (c.to == 1) ++retries;
        }
        t += use;
    }
    CHECK(retries >= 1 && retries <= 5);        // at 30 s, then 60, 120, 240, 480 s of room at the most
    CHECK(changes <= 2 * retries + 1);
    CHECK(a.current() == 2 && a.retry_after() >= 60);
}

TEST(fif_retry_that_holds_stays_at_one) {
    FramesAuto a;
    CHECK(run(a, 5, 12.0, 8.0, 8.333) == 2);              // 1 misses 120 Hz
    CHECK(run(a, 120, 3.0, 1.0, 8.333) == 1);             // the scene got light at 2 (3 ms x the ratio, capped at 2.5: 7.5 ms fits): try 1
    CHECK(run(a, 120, 6.0, 3.0, 8.333) == 0 && a.current() == 1 && a.retry_after() == FramesAuto::kRetryFirst);   // 1 holds now
}

TEST(fif_failed_retry_returns_at_once) {
    FramesAuto a;
    CHECK(run(a, 5, 12.0, 8.0, 8.333) == 2);
    double at = 0;
    CHECK(run(a, 120, 3.0, 1.0, 8.333, &at) == 1 && at > 25 && at < 35);   // after ~30 s of room
    CHECK(run(a, 10, 12.0, 8.0, 8.333, &at) == 2 && at < 3.5);             // 1 misses again: 2 within the judging windows
    CHECK(a.retry_after() == 2 * FramesAuto::kRetryFirst);
    CHECK(run(a, 50, 3.0, 1.0, 8.333) == 0);                               // and not again before ~60 s of room
    CHECK(run(a, 20, 3.0, 1.0, 8.333) == 1);
}

// fifauto: B3_PERF_BISECT's steps -- each of its seconds of race frames (the first 1.5 s not counted), then the next; a
// pause (the race off the screen) starts the step again; off without the variable
TEST(perf_bisect_steps) {
    b3replay::PerfBisect off;
    std::string line;
    CHECK(!off.on() && !off.frame(5.0, 1.0, 1.0, -1, line));
    b3replay::PerfBisect b(4.0);
    CHECK(b.on() && b.step() == 0);
    int ended = 0;
    for (int i = 0; i < 10000 && !b.done(); ++i)
        if (b.frame(5.0, 2.0, 1.0, i % 4 == 0 ? 3.0 : -1.0, line)) {
            ++ended;
            if (ended == 1)   // 2.5 s of 5 ms frames counted (about 500), every 4th sampled
                CHECK(line.find("step 0 all on (as set): 50") == 0 && line.find("200.0 fps") != std::string::npos &&
                      line.find("GPU work 3.00/3.00 (12") != std::string::npos && line.find("acquire (drawable) 1.00/1.00") != std::string::npos);
        }
    CHECK(b.done() && ended == b3replay::PerfBisect::kSteps && b.lines().size() == std::size_t(b3replay::PerfBisect::kSteps));
    b3replay::PerfBisect c(4.0);
    for (int i = 0; i < 700; ++i) c.frame(5.0, 2.0, 1.0, -1, line);   // 3.5 s into step 0
    c.pause();
    bool early = false;
    for (int i = 0; i < 700; ++i) early = early || c.frame(5.0, 2.0, 1.0, -1, line);
    CHECK(!early && c.step() == 0);   // the step starts again after a pause
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

// Fable review: 1 fails on a light scene, then a long heavy scene at 2 (slower than 1 was on the light one, still GPU-bound):
// 2 is not taken for CPU-bound (1's frame time was another scene's) and stays; the back-off starts again once 2 helped
TEST(fif_heavy_scene_later_stays_at_two) {
    FramesAuto a;
    CHECK(run(a, 5, 12.0, 8.0, 8.333) == 2);              // light scene: 1 misses 120 Hz, 2 is taken
    CHECK(run(a, 20, 6.0, 3.0, 8.333) == 0);              // at 2 it is faster (helped)
    CHECK(a.backoff() == FramesAuto::kBackoffFirst);
    CHECK(run(a, 600, 14.0, 8.0, 8.333) == 0 && a.current() == 2);   // ten minutes of a heavier scene: stays at 2
    // a CPU-bound case right after a switch still goes back (fif_cpu_bound_stays_or_returns), with the first cool-down
    FramesAuto b;
    CHECK(run(b, 5, 25.0, 12.0, 16.67) == 2);
    CHECK(run(b, 6, 25.0, 1.0, 16.67) == 1);
    CHECK(b.backoff() == 2 * FramesAuto::kBackoffFirst);
    CHECK(run(b, 30, 25.0, 12.0, 16.67) == 2);            // after the cool-down, 2 again; this time it helps
    CHECK(run(b, 10, 14.0, 6.0, 16.67) == 0 && b.backoff() == FramesAuto::kBackoffFirst);
}

// A retry of 1 that misses only after its judging windows (before it has held kRetryHeld) lengthens the stretch too
TEST(fif_late_failed_retry_backs_off) {
    FramesAuto a;
    CHECK(run(a, 5, 12.0, 8.0, 8.333) == 2);
    CHECK(run(a, 120, 3.0, 1.0, 8.333) == 1);                         // the retry
    CHECK(run(a, 10, 6.0, 3.0, 8.333) == 0 && a.current() == 1);      // holds ten windows...
    CHECK(run(a, 10, 12.0, 8.0, 8.333) == 2);                         // ... then misses: back to 2
    CHECK(a.retry_after() == 2 * FramesAuto::kRetryFirst);
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
    CHECK(scaled_bytes_per_pixel(4, false, false, true) == b4 + 8 && scaled_bytes_per_pixel(4, false, true, true) == b4 + 24);   // gameopts: the mirror's copy
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

// RENDER SCALE's 2D split (render_scale.h scaled_overlay_end): the Crash Nav frame draws its 2D backdrop, then the globe's
// depth-tested batches, then the menu text. The scaled pass must take the backdrop with the globe (1.3.0 took the globe only
// and drew the backdrop over it afterwards: no globe at any scale but 100 %); the menu after the globe stays at full size.
TEST(render_scale_overlay_split_keeps_order) {
    struct It { bool overlay, overlay_ztest; };
    using b3gfx::scaled_overlay_end;
    const std::vector<It> crash_nav = {{false, false},  // the 3D (the front end's scene)
                                       {true, false},   // the blurred backdrop
                                       {true, true},    // the globe's batches
                                       {true, true},
                                       {true, false},   // the region list, the prompts
                                       {true, false}};
    CHECK(scaled_overlay_end(crash_nav) == 4);   // the backdrop and the globe with the 3D, in that order; the menu after
    const std::vector<It> race = {{false, false}, {true, false}, {true, false}};   // no depth-tested 2D: all at full size
    CHECK(scaled_overlay_end(race) == 0);
    CHECK(scaled_overlay_end(std::vector<It>{}) == 0);
    const std::vector<It> last = {{true, false}, {true, true}};
    CHECK(scaled_overlay_end(last) == 2);
    const std::vector<It> not_2d = {{true, false}, {false, true}};   // overlay_ztest counts on 2D items only
    CHECK(scaled_overlay_end(not_2d) == 0);
}

// uploadbatch (include/b3gfx/upload_batch.h): $B3_UPLOAD_BATCH wins ("0" off, "offscreen" on without a window), else the PC
// setting upload_batch; never without a window unless "offscreen" (a device without a window defragments at every submit)
TEST(upload_batch_wanted_rule) {
    using b3gfx::upload_batch_wanted;
    CHECK(upload_batch_wanted(nullptr, true, true));       // the default with a window: batched
    CHECK(!upload_batch_wanted(nullptr, false, true));     // pc_settings upload_batch = false
    CHECK(!upload_batch_wanted(nullptr, true, false));     // offscreen: never by default
    CHECK(!upload_batch_wanted("", true, false) && upload_batch_wanted("", true, true) && !upload_batch_wanted("", false, true));   // empty = unset
    CHECK(!upload_batch_wanted("0", true, true));          // the environment wins over the setting
    CHECK(upload_batch_wanted("1", false, true));
    CHECK(!upload_batch_wanted("1", true, false));         // "1" needs a window
    CHECK(upload_batch_wanted("offscreen", false, false)); // timing only
}

// d3d12 (include/b3gfx/driver_order.h): on Windows the default (Auto, "") is Vulkan by name, D3D12 only after it; a named
// API falls back to the other; off Windows the choice as given (SDL's own when empty)
TEST(default_gpu_driver_is_vulkan_on_windows) {
    using b3gfx::driver_attempts;
    using V = std::vector<std::string>;
    CHECK(driver_attempts("", true) == V({"vulkan", "direct3d12"}));
    CHECK(driver_attempts("", true).front() == "vulkan");
    CHECK(driver_attempts("vulkan", true) == V({"vulkan", "direct3d12"}));
    CHECK(driver_attempts("direct3d12", true) == V({"direct3d12", "vulkan"}));
    CHECK(driver_attempts("", false) == V({""}));
    CHECK(driver_attempts("vulkan", false) == V({"vulkan"}));
    CHECK(driver_attempts("metal", true) == V({"metal"}));
}

// whitesky (b3gfx.h keeps_frame_alpha): a blended draw the game makes with the frame alpha masked keeps the frame alpha;
// any other draw (opaque, or blended without the mask: the track's, the cars') writes its own
TEST(masked_draws_keep_the_frame_alpha) {
    using b3gfx::Blend;
    const auto item = [](Blend b, bool masked) { b3gfx::DrawItem it; it.blend = b; it.frame_alpha_masked = masked; return it; };
    CHECK(b3gfx::keeps_frame_alpha(item(Blend::kAlpha, true)));     // the clouds
    CHECK(!b3gfx::keeps_frame_alpha(item(Blend::kAlpha, false)));   // blended track draws write As (the specular's)
    CHECK(!b3gfx::keeps_frame_alpha(item(Blend::kOpaque, true)));   // (an opaque draw overwrites: no blend to mask)
    CHECK(!b3gfx::keeps_frame_alpha(item(Blend::kOpaque, false)));
}

// scenedepth (include/b3gfx/scene_depth.h): $B3_SCENE_DEPTH "0" / "1" wins, else pc_settings scene_depth 0 / 1, else the
// backend's default (off on every backend for now)
TEST(scene_depth_mode_rule) {
    using b3gfx::scene_depth_mode;
    using M = b3gfx::SceneDepthMode;
    // nightdepth: the default is night's frames on every backend
    for (const char* drv : {"vulkan", "direct3d12", "metal"}) CHECK(scene_depth_mode(nullptr, -1, drv) == M::kNight);
    CHECK(scene_depth_mode(nullptr, -1, nullptr) == M::kNight);
    CHECK(scene_depth_mode(nullptr, 1, "vulkan") == M::kOn && scene_depth_mode(nullptr, 0, "vulkan") == M::kOff);   // the ini key
    CHECK(scene_depth_mode("1", 0, "vulkan") == M::kOn && scene_depth_mode("0", 1, "vulkan") == M::kOff);           // the environment wins
    CHECK(scene_depth_mode("0", -1, "vulkan") == M::kOff);                                                           // (over the default too)
    CHECK(scene_depth_mode("", 1, "vulkan") == M::kOn && scene_depth_mode("yes", -1, "vulkan") == M::kNight);       // else ignored
    // a frame: by day only when on for every frame; at night in both
    using b3gfx::scene_depth_frame;
    CHECK(!scene_depth_frame(M::kOff, true) && !scene_depth_frame(M::kOff, false));
    CHECK(scene_depth_frame(M::kOn, true) && scene_depth_frame(M::kOn, false));
    CHECK(scene_depth_frame(M::kNight, true) && !scene_depth_frame(M::kNight, false));
    // (Fable) the pipelines at start: on for every frame, or night's with NIGHT on from the start; a day player none
    using b3gfx::scene_depth_forms_at_start;
    CHECK(scene_depth_forms_at_start(M::kOn, false) && scene_depth_forms_at_start(M::kNight, true));
    CHECK(!scene_depth_forms_at_start(M::kNight, false) && !scene_depth_forms_at_start(M::kOff, true));
}

TEST_MAIN("perf4: frames in flight Auto, warm-up list, supersampling cap")
