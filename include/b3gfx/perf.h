// Frame timing capture (perf3, docs/native-renderer.md "Frame timing capture"): scoped CPU timers per stage of the frame,
// summarised once a second into the file named by B3_PERF_LOG. Header only; works on every platform (steady clock).
//
// Off (B3_PERF_LOG unset): every call is one load and a branch on a plain bool, no clock read, no memory traffic.
// On: a scope costs two clock reads and an add; the samples sit in fixed ring buffers (no allocation per frame); the
// file is written once a second by the render thread.
//
// Two kinds of stage:
//   frame stages (kFeedDrain ... kLimiter): summed per frame on the render thread (Scope / add), one sample per frame
//   game stages  (kGameStep ... kGameHandover): one sample per VBlank / poll, written by the game thread through
//                b3r_perf_add (src/b3replay/inproc.cpp), each ring with one writer
// Scopes nest and are exclusive: the time of an inner scope is taken out of the one around it, so the stages add up.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace b3perf {

enum Stage : int {
    // frame stages (render thread)
    kFrame = 0,        // the frame's wall time: from one frame's end to the next's (the rate; includes every stage below)
    kWaitSwapchain,    // SDL_WaitForGPUSwapchain (the low-latency modes' wait at the top of the loop)
    kFeedDrain,        // the game's records decoded into scene state
    kSceneUpdate,      // LiveScene::update (models, textures, uploads of new geometry)
    kDrawList,         // the draw list built from the tick on screen (view, HUD, overlays): top of the frame to draw_frame
    kDlInterp,         // inside kDrawList: LiveScene::frame + the blend (interpolation, camera, car state)
    kDlWorld,          // inside kDrawList: View::draw_list (the 3D items: track, cars, effects, particles)
    kDlHud,            // inside kDrawList: View::hud_list (the HUD and the 2D panels)
    kUpload,           // update_*/create_*: the CPU copy into the transfer buffer and the copy pass's recording
    kUploadSubmit,     // the upload command buffers' own SDL_SubmitGPUCommandBuffer
    kCmdScene,         // render_scene's command recording: the scene pass (the 3D draws, the sun, the pass splits)
    kCmdPost,          // the post passes' recording (zoom blur, bloom, depth of field)
    kCmdOverlay,       // the 2D stage's recording (draw_overlay)
    kCmdPresent,       // the present pass
    kAcquire,          // SDL_WaitAndAcquireGPUSwapchainTexture: a GPU-bound frame waits here (or in kSubmit)
    kSubmit,           // SDL_SubmitGPUCommandBuffer of the frame's command buffer
    kFence,            // SDL_WaitForGPUFences (offscreen draws, read-back)
    kLimiter,          // the frame limiter's wait (FRAME RATE LIMIT / battery saver), deliberate
    kStageFrameCount,
    // game stages (game thread; one sample per VBlank or poll)
    kGameStep = kStageFrameCount,   // the emulated game between two VBlank hooks (EE + VU + game code), without the pacing wait
    kGamePace,                      // the pacing wait (idle time: the game is faster than real time)
    kGamePoll,                      // the per-poll scene hook: guest reads, the record, the hand-over
    kGameHandover,                  // the hand-over alone (build the record's body is outside it; b3r_submit)
    kStageCount
};

enum Counter : int { kDraws = 0, kPasses, kSubmits, kUploads, kUploadBytes, kCounterCount };

inline const char* const kStageName[kStageCount] = {"frame",   "wait_swapchain", "feed_drain", "scene_update", "drawlist", "dl_interp", "dl_world", "dl_hud", "upload",
                                                    "upload_submit", "cmd_scene", "cmd_post", "cmd_overlay", "cmd_present", "acquire",
                                                    "submit",  "fence",          "limiter",    "game_step",    "game_pace", "game_poll",
                                                    "game_handover"};
inline const char* const kCounterName[kCounterCount] = {"draws", "passes", "submits", "uploads", "upload_bytes"};

inline std::uint64_t now_ns() {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

constexpr int kRing = 4096;   // samples kept per stage (a second at up to 4000 frames; the game's at 60 Hz needs 60)

struct State {
    bool on = false;
    std::thread::id tid;   // the render thread (init_from_env's caller): frame stages and counters from others are ignored
    std::FILE* file = nullptr;
    // frame stages: the current frame's sum (ns)
    std::uint64_t acc[kStageCount] = {};
    // rings of finished samples (ns, 32 bit: 4 s max; larger saturate)
    std::atomic<std::uint32_t> ring[kStageCount][kRing];
    std::atomic<std::uint64_t> head[kStageCount];   // samples ever written
    std::uint64_t read[kStageCount] = {};           // samples already summarised
    std::uint64_t counters[kCounterCount] = {};
    // the render thread's scope stack (exclusive nesting)
    int cur = -1;
    std::uint64_t cur_t0 = 0;
    // second bookkeeping
    std::uint64_t last_frame_ns = 0, sec_start_ns = 0;
    std::uint64_t frames = 0;
    bool header_done = false;
    std::string info;   // the first line's device description
    State() {
        for (auto& r : ring) for (auto& x : r) x.store(0, std::memory_order_relaxed);
        for (auto& h : head) h.store(0, std::memory_order_relaxed);
    }
};

inline State& st() {
    static State s;
    return s;
}
inline std::atomic<bool> g_on_flag{false};   // mirrors st().on (one relaxed load on the hot path; the game thread reads it too)
inline bool on() { return g_on_flag.load(std::memory_order_relaxed); }

// B3_PERF_LOG=<file>: starts the capture. Idempotent. Returns whether it is on.
inline bool init_from_env() {
    State& s = st();
    if (s.on) return true;
    const char* path = std::getenv("B3_PERF_LOG");
    if (!path || !*path) return false;
    s.file = std::fopen(path, "w");
    if (!s.file) {
        std::fprintf(stderr, "[b3perf] cannot write %s\n", path);
        return false;
    }
    s.on = true;
    s.tid = std::this_thread::get_id();
    g_on_flag.store(true, std::memory_order_relaxed);
    std::fprintf(stderr, "[b3perf] frame timing into %s\n", path);
    return true;
}

inline void push(int stage, std::uint64_t ns) {
    State& s = st();
    const std::uint64_t h = s.head[stage].load(std::memory_order_relaxed);
    s.ring[stage][h % kRing].store(std::uint32_t(std::min<std::uint64_t>(ns, 0xFFFFFFFFull)), std::memory_order_relaxed);
    s.head[stage].store(h + 1, std::memory_order_release);
}

// A game-thread sample (the stage's single writer).
inline void sample(int stage, std::uint64_t ns) {
    if (on()) push(stage, ns);
}

// Render thread: adds to the current frame's sum of a stage.
inline bool mine() { return std::this_thread::get_id() == st().tid; }
inline void add(int stage, std::uint64_t ns) {
    if (on() && mine()) st().acc[stage] += ns;
}
inline void count(int c, std::uint64_t n = 1) {
    if (on() && mine()) st().counters[c] += n;
}

// Exclusive scoped timer: its time goes to `stage`, minus the scopes nested in it.
struct Scope {
    int stage = -1, parent = -1;
    Scope(int stage_) {
        if (!on() || !mine()) return;
        stage = stage_;
        State& s = st();
        const std::uint64_t t = now_ns();
        parent = s.cur;
        if (parent >= 0) s.acc[parent] += t - s.cur_t0;
        s.cur = stage;
        s.cur_t0 = t;
    }
    void end() {   // early end (the destructor does nothing after)
        if (stage < 0) return;
        State& s = st();
        const std::uint64_t t = now_ns();
        s.acc[stage] += t - s.cur_t0;
        s.cur = parent;
        s.cur_t0 = t;
        stage = -1;
    }
    ~Scope() { end(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
// the stage currently being timed (-1: none), for a call that belongs to a different stage inside an upload
inline int current() { return on() && mine() ? st().cur : -1; }

inline void set_info(const std::string& s) { if (on()) st().info = s; }

inline double pct_ms(std::vector<std::uint32_t>& v, double q) {
    if (v.empty()) return 0;
    const std::size_t k = std::min(v.size() - 1, std::size_t(q * double(v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return double(v[k]) * 1e-6;
}

// Render thread, once per frame after everything (including the limiter's wait): closes the frame's sums into the rings
// and writes a line when a second has passed. `w` / `h`: the frame's pixel size.
inline void frame_end(int w, int h) {
    if (!on()) return;
    State& s = st();
    const std::uint64_t t = now_ns();
    if (s.last_frame_ns) s.acc[kFrame] = t - s.last_frame_ns;
    s.last_frame_ns = t;
    if (!s.sec_start_ns) s.sec_start_ns = t;
    if (s.acc[kFrame]) {   // (the first frame has no interval)
        for (int k = 0; k < kStageFrameCount; ++k) push(k, s.acc[k]);
        ++s.frames;
    }
    for (int k = 0; k < kStageFrameCount; ++k) s.acc[k] = 0;
    if (t - s.sec_start_ns < 1000000000ull) return;
    if (!s.header_done) {
        std::fprintf(s.file, "# b3perf 1 %s\n# t_s frames fps size", s.info.c_str());
        for (int k = 0; k < kStageCount; ++k) std::fprintf(s.file, " %s_avg %s_p95 %s_max", kStageName[k], kStageName[k], kStageName[k]);
        for (int c = 0; c < kCounterCount; ++c) std::fprintf(s.file, " %s", kCounterName[c]);
        std::fprintf(s.file, "\n# times in ms; frame stages sum over the frame (exclusive: they add up to frame); game_* are per VBlank / poll;"
                             " counters are the second's totals\n");
        s.header_done = true;
    }
    const double secs = double(t - s.sec_start_ns) * 1e-9;
    std::fprintf(s.file, "%.1f %llu %.1f %dx%d", double(t) * 1e-9, static_cast<unsigned long long>(s.frames), double(s.frames) / secs, w, h);
    std::vector<std::uint32_t> v;
    for (int k = 0; k < kStageCount; ++k) {
        const std::uint64_t head = s.head[k].load(std::memory_order_acquire);
        std::uint64_t from = s.read[k];
        if (head - from > std::uint64_t(kRing)) from = head - kRing;
        v.clear();
        double sum = 0;
        for (std::uint64_t i = from; i < head; ++i) {
            const std::uint32_t x = s.ring[k][i % kRing].load(std::memory_order_relaxed);
            v.push_back(x);
            sum += double(x);
        }
        s.read[k] = head;
        const double mx = v.empty() ? 0.0 : double(*std::max_element(v.begin(), v.end())) * 1e-6;
        std::fprintf(s.file, " %.3f %.3f %.3f", v.empty() ? 0.0 : sum / double(v.size()) * 1e-6, pct_ms(v, 0.95), mx);
    }
    for (int c = 0; c < kCounterCount; ++c) {
        std::fprintf(s.file, " %llu", static_cast<unsigned long long>(s.counters[c]));
        s.counters[c] = 0;
    }
    std::fprintf(s.file, "\n");
    std::fflush(s.file);
    s.frames = 0;
    s.sec_start_ns = t;
}

}  // namespace b3perf

#define B3PERF_SCOPE(stage) ::b3perf::Scope b3perf_scope_##stage(::b3perf::stage)
