// b3replay --live: the native renderer fed live from a running game session (docs/native-renderer.md §13). The game
// process's scene hook (src/gameview/scene_capture.cpp, B3PORT_SCENE_LIVE=<name>) publishes every retail poll into
// the shared-memory ring <name> (include/b3live/ring.h); this process reads it, rebuilds the same guest image and ticks
// a capture file would give (capture.h), follows the session's races (route and car matching as each track loads), and
// draws through the same view path as the offline replay (view.h), uncapped, interpolated between the 60 Hz ticks.
#ifndef B3REPLAY_LIVE_H
#define B3REPLAY_LIVE_H

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "b3live/inproc.h"
#include "b3live/ring.h"
#include "capture.h"
#include "view.h"
#include "b3settings/settings.h"

namespace b3replay {

// photoblur (photo mode's motion blur): each vehicle's velocity as the ticks come, into Vehicle::motion. A vehicle's
// velocity is its position's change over the game's timer (Tick::now; polls / 60 where it did not advance) between the
// last two ticks in which it moved (1 to 4 polls apart;
// a longer gap or a jump of more than 150 m/s leaves it unknown). While no vehicle moves (the game paused) every
// velocity is held; while others move, a vehicle still for 3 ticks has stopped (0). Vehicles are told apart by their
// guest address and model; one not seen for 10 s is forgotten.
class VehicleMotion {
public:
    void apply(Tick& t);
    void clear() { mem_.clear(); }
private:
    struct Mem {
        float pos[3] = {}, vel[3] = {};
        float now = 0;   // Tick::now (the game's timer) at `poll`
        std::uint32_t poll = 0, seen = 0;
        int still = 0;
        bool known = false;
    };
    std::map<std::uint64_t, Mem> mem_;
};

// The ring reader: keeps the decoded ticks of the last ~30 s with absolute indices (one per received poll) and the
// current race segment.
// trafmem: set while an offscreen shot runner's hold keeps more than LiveFeed::kHoldWaitBytes of ticks (b3r_feed_full)
std::atomic<bool>& feed_hold_full();
// trafmem: the hold's poll (UINT32_MAX: none) and the kept ticks' mean bytes, published at each trim, so b3r_feed_full can
// also count the ticks the game has pushed that the renderer has not decoded yet (they join the hold as it decodes them)
std::atomic<std::uint32_t>& feed_hold_poll();
std::atomic<std::size_t>& feed_tick_bytes();

class LiveFeed : public Timeline {
public:
    explicit LiveFeed(std::string name) : name_(std::move(name)) {}
    ~LiveFeed() { if (inproc) inproc->release(pending_); }
    LiveFeed(const LiveFeed&) = delete;
    LiveFeed& operator=(const LiveFeed&) = delete;

    enum class State { kNoWriter, kWaitKey, kSynced, kEnded };
    State state() const { return state_; }
    const std::string& name() const { return name_; }

    // Opens the ring if needed and decodes every published record (at most `max_records`). Returns the records taken.
    int drain(int max_records = 1 << 30);
    // speedrun1: called with each tick as it is kept (in order, every tick, before older ones are let go), so a reader
    // that must see every tick does even when a drain takes more than the ticks kept. Unset: nothing is called.
    std::function<void(const Tick&)> on_push;
    // inproc1 (§23): the records come from this in-process feed (b3render.dll inside the game process) instead of the
    // shared-memory ring `name`. Set before the first drain.
    b3live::Inproc* inproc = nullptr;

    // Timeline: the received ticks still kept.
    std::size_t first() const override { return base_; }
    std::size_t last() const override { return base_ + ticks_.size() - 1; }
    const Tick& at(std::size_t i) const override { return ticks_[i - base_]; }
    bool empty() const { return ticks_.empty(); }
    std::uint64_t arrival_ns(std::size_t i) const { return ns_[i - base_]; }
    // lat1: the tick's schedule stamp (the host time its input was latched; b3r_submit_at), 0 if the producer sent none
    std::uint64_t sched_ns(std::size_t i) const { return sched_[i - base_]; }
    // The tick index holding `poll` (-1 if not kept).
    long index_of_poll(std::uint32_t poll) const;

    // The race segment the latest ticks belong to (as the offline reader cuts segments: track on, camera, race car;
    // gaps up to 60 ticks filled with the last race tick; a route change or a resync starts a new one).
    struct Seg {
        bool active = false;      // a race is on (or in a gap of at most 60 ticks)
        std::uint64_t id = 0;     // increments with every new segment
        std::size_t first = 0, last_race = 0;
        std::uint32_t route_size = 0;
        int route_units = 0;
        std::size_t race_ticks = 0;
    };
    const Seg& segment() const { return seg_; }
    // The segment holding tick `idx` (the current one or one of the last kPastSegments ended ones, up to its last race
    // tick and the gap after it); none for a tick outside every race (offscreen shots: a poll the renderer reaches
    // after the next segment began, e.g. a Crash junction's retry)
    static constexpr std::size_t kPastSegments = 8;
    std::optional<Seg> segment_at(std::size_t idx) const;

    const Capture& models() const { return cap_; }
    std::uint64_t records = 0, keyframes = 0, resyncs = 0, ticks_total = 0;
    // perf (2026-11-12): host time per decoded record (ms; the first 4M), the decode's cost on the reader's thread;
    // negative for race ticks (capture.h race_tick)
    std::vector<float> decode_ms;
    // --verify: every decoded tick compared with the file reader's tick of the same poll (capture.h same_tick)
    const Capture* verify = nullptr;
    std::map<std::uint32_t, std::size_t> verify_index;   // poll -> index into verify->ticks
    std::uint64_t verified = 0, verify_differ = 0, verify_missing = 0;

private:
    void push(Tick&& t, std::uint64_t ns, std::uint64_t sched);
    int drain_inproc(int max_records);
    bool decode(std::uint32_t kind, const std::uint8_t* body, std::size_t n, std::uint64_t ns, std::uint64_t sched = 0);

    std::string name_;
    State state_ = State::kNoWriter;
    b3live::Mapping map_;
    std::uint64_t pos_ = 0, request_at_ = 0;
    std::uint64_t next_open_ns_ = 0, next_alive_ns_ = 0;
    Image image_;
    Capture cap_;   // model table (heads) and counters; its ticks stay empty
    std::deque<Tick> ticks_;
    std::deque<std::uint64_t> ns_, sched_;
    std::deque<std::size_t> bytes_of_;   // trafmem: each kept tick's bytes (tick_bytes), and their sum
    std::size_t bytes_ = 0;
    std::size_t base_ = 0;
public:
    // the decoded ticks kept (the live window: 1200, 20 s); the offscreen shot runner keeps more race ticks (front-end ones
    // stay at 1200: they carry movie pictures) (live.cpp: nightfix, a
    // split-screen shot's 24 bloom warm-up frames at ~0.45 s each in the software rasteriser let the game run past the
    // next shot's poll before it was taken)
    std::size_t keep_ticks = 1200;
    // sparkdet (2026-11-22): the offscreen shot runner's hold -- race ticks from this poll on are not dropped while a shot
    // still needs them (its bloom warm-up: the ticks before the shot), so a shot's warm-up is the same ticks however far
    // the game has run ahead (an unpaced replay could pass keep_ticks before a slow shot was taken: a shortened warm-up,
    // a different bloom trail in the sparks). kHoldCap bounds it. UINT32_MAX: no hold.
    std::atomic<std::uint32_t> hold_poll{0xFFFFFFFFu};
    static constexpr std::size_t kHoldCap = 20000;
    // trafmem (TRAFFIC Extreme: 70-130 vehicles of 4 KB, up to ~0.55 MB a tick; the hold piled up toward 20000 of them,
    // and even 3600 were ~1.9 GB, over the checks' 10 GB address-space cap with the ~8 GB the process maps besides): the
    // kept ticks are bounded by bytes too. Past the window's own 1200 ticks, keep_ticks' longer window keeps ticks
    // within kHoldWaitBytes; the hold keeps its ticks past that up to kHoldBytes, and from kHoldWaitBytes on, while the
    // hold is what keeps them, feed_hold_full() is set and native_play holds the game (unpaced, offscreen: wall time
    // only, b3r_feed_full) until the shot is taken and the hold moves on; past kHoldBytes the oldest go anyway (a later
    // shot's warm-up may then be short: logged once). A window's 1200 ticks are never cut for bytes.
    static constexpr std::size_t kHoldWaitBytes = std::size_t(1) << 30, kHoldBytes = std::size_t(5) << 28;   // 1 GiB, 1.25 GiB
    static constexpr std::size_t kWindowTicks = 1200;
    // (checks only: $B3_FEED_HOLD_MB=<n> sets the wait's bound to n MB, the drop's a quarter above it)
    static std::size_t hold_wait_bytes() {
        static const std::size_t v = [] {
            const char* e = std::getenv("B3_FEED_HOLD_MB");
            const unsigned long long mb = e && *e ? std::strtoull(e, nullptr, 10) : 0;
            return mb ? std::size_t(mb) << 20 : kHoldWaitBytes;
        }();
        return v;
    }
    static std::size_t hold_bytes() { return hold_wait_bytes() == kHoldWaitBytes ? kHoldBytes : hold_wait_bytes() + hold_wait_bytes() / 4; }
    static std::size_t tick_bytes(const Tick& t);
    // The hold's rule for the oldest kept tick (a race tick past the window): kept while it is at or after hold_poll and
    // the hold is within kHoldCap ticks and kHoldBytes; the game waits (feed_hold_full) once a kept hold reaches
    // kHoldWaitBytes
    static bool hold_keeps(std::uint32_t front_poll, std::uint32_t hold_poll, std::size_t ticks, std::size_t bytes) {
        return front_poll >= hold_poll && ticks <= kHoldCap && bytes <= hold_bytes();
    }
    static bool hold_full(bool held, std::size_t bytes) { return held && bytes >= hold_wait_bytes(); }
    // b3r_feed_full's look ahead: the ticks from the hold to the game's newest poll, decoded or not, at the kept ticks'
    // mean size, reach the wait's bound
    static bool hold_ahead_full(std::uint32_t hold, std::uint32_t game_poll, std::size_t tick_bytes) {
        return hold != 0xFFFFFFFFu && game_poll > hold && std::uint64_t(game_poll - hold) * tick_bytes >= hold_wait_bytes();
    }
    std::size_t kept_bytes() const { return bytes_; }
    // The window's trim (after each tick; the shot runner calls it again when it moves the hold, so a game waiting on
    // feed_hold_full() goes on without a new tick)
    void trim();
private:
    Seg seg_;
    std::deque<Seg> past_;   // the last kPastSegments ended segments, oldest first
    std::string body_;
    bool warned_unbound_ = false;
    bool warned_hold_ = false;   // sparkdet
    VehicleMotion motion_;   // photoblur
    b3live::InprocRecord* pending_ = nullptr;   // inproc: taken, not yet decoded (oldest first)
};

struct LiveOptions {
    std::string name, game, driver, out_dir;
    int msaa = 4, w = 1600, h = 900, car_lod = -1;   // car_lod -1: the game's choice
    bool size_set = false, vsync = false, fullscreen = false, fog = true, specular = true, effects = true;
    bool coronas = true;                 // --no-coronas / L (corona1)
    bool particles = true;               // --no-particles / O (fx2: scrapes, sparks, popcorn)
    bool sun = true, sun_test = true;    // --no-sun / X, --no-sun-test / B (water1)
    bool blur = true;                    // --no-blur / N (post1 §25: the zoom blur the game picks; water1 §21's radial one)
    bool blur_radial = false;            // --blur-radial (captures before post1)
    bool bloom = true;                   // --no-bloom / T (post1)
    float radial_scale = 0.5f;           // --radial-blur S (gsoff1 §25.8)
    bool post_game = false;              // --post game | pc / Z (post1)
    bool hud = true;   // --no-hud / U (hud1)
    bool movies = true;   // --no-movies / M (menu1): movie pictures, else the placeholder
    bool room = true, reflection = true, fe_shadow = true;   // --no-showroom / R, --no-reflection, --no-fe-shadow (fe2)
    bool globe = true;                   // --no-globe (fe2)
    bool room_env = true;                // --no-room-env (fe2)
    bool hud_wide = true;   // --hud-layout wide|43 / Y (hud2)
    int fe_layout = 2;      // --fe-layout panels (fe3) | 43 (wide1: the centred 4:3 picture) | edges (hud2)
    bool movie_fill = true; // --movie-fill blur | black (fe3)
    bool whole_track = true; // --track-units all | resident (mac5)
    bool hud_sharp = true;  // --hud-filter sharp|game / J (hud2)
    bool hud_reproject = true;  // --hud-markers frame|tick / K (hud4): world-anchored markers projected per frame
    double delay = 1.5;                  // display delay behind the latest tick, in ticks (the game's own tick rate)
    bool clock_old = false;              // --clock-db55faa: the display clock before live2 (comparisons; §20)
    double max_fps = 0;                  // window: frame cap (0 = uncapped; a cap leaves GPU time to the game's own renderer)
    std::vector<std::pair<std::uint32_t, float>> shots;   // offscreen: (poll, alpha) frames to write as PNG
    int bench_w = 0, bench_h = 0;        // offscreen: also render continuously at this size and report frame times
    double wait_writer_s = 0;            // offscreen: give up if no writer after this long (0 = wait forever)
    std::string verify;                  // offscreen: the same session's capture file, to compare every tick with
    b3live::Inproc* inproc = nullptr;    // inproc1 (§23): the in-process feed instead of the ring `name`
    int bloom_warmup = 24;               // offscreen shots: the bloom warmed over this many ticks before (as --offscreen; inproc1)
    bool activate = false;               // --activate (inproc1): the window takes the focus when shown (the only window: GS off)
    bool native_pad = false;             // --native-pad (play1): keys / gamepads as the native game's pad (nativepad.h)
    bool native_audio = false;           // --native-audio (play1): the native game's sound (nativeaudio.h; window only)
    bool fps_counter = false;            // --fps-counter (gsoff1): the frame-rate panel always (else as the FPS COUNTER setting says)
    bool route_header = true;            // --no-route-header (gsoff1): the route only from 30 race ticks of samples (before §28.6)
    std::optional<b3settings::Settings> settings;   // --settings (settings1): our PC settings at start; HSET records follow
    std::string settings_path;   // mac5: --settings' file, where the PC SETTINGS page saves (native: a default in the user's prefs)
    std::string controls_path;   // remap1: --controls' file (controls.ini; native window: in the user's prefs; empty: defaults)
    std::string ghost_dir;       // ghost1 (ghost.h): where Time Attack ghosts are kept (empty: none recorded or shown)
    std::string speedrun_dir;    // speedrun1 (speedrun.h): where best times are kept (empty: this run's only, in memory)
    bool replay_playback = false;  // media1 (--replay-playback, records.h): a replay plays: no ghost or best time written
    std::function<std::string()> gameplay_set;   // media1 (records.h): b3mod::gameplay_set_hash of the active set ("" none)
    // mac5: safe mode (include/b3settings/safemode.h): the window says so for a few seconds and in its title
    bool safe_mode = false;
    bool safe_forced = false;   // asked for (--safe-mode), not two unclean runs
    int mods_safe_off = 0;      // modkit1b: safe mode left this many mods off (native_play): the notice says so
    // lat1: --latency low|smooth|newest. low (default): the display follows the ticks'
    // latch stamps with an adaptive margin (clock.h step_sched; `delay` without stamps), 1 GPU frame in flight, the
    // swapchain waited for before the clock is sampled. smooth: the arrival clock with `delay` and SDL's frame queue (before
    // lat1). newest: the newest tick at once, not blended.
    std::string latency = "low";
    bool latency_auto = true;            // perf4: the latency mode is the default (no --latency, LATENCY MODE Auto): only then
                                         // may FRAMES IN FLIGHT Auto add a second frame (a chosen Low keeps one)
    double margin_ms = 1.0;              // --margin MS: the schedule clock's least margin
    double lead = 0.0;                   // --lead T: low mode's extrapolation past the newest tick, 0..1 ticks (mac5: 0)
    float dither = 0.0f;                 // --dither (perf1, §33.8): the present pass's ordered dither, output steps (0 = off)
    double blend_ms = 0.0;               // --blend MS (perf1, §33): low mode's correction blend time constant (0 = off, the
                                         // default: measured in the renderer it made the motion worse, §33.3)
    int frames_in_flight = -1;           // --frames-in-flight N (low / newest; 0: SDL's default 2); -1: Auto, from 1 (perf4,
                                         // include/b3gfx/frames_auto.h)
    std::string latency_log;             // --latency-log <csv> (lat1)
};

int run_live_window(const LiveOptions& o);
int run_live_offscreen(const LiveOptions& o);

// nofallback1 (b3r_game_stopped, from native_play's game thread): the native game stopped on a function this build has
// no native code for ("<address> <name>"). The window then stays when the feed ends, with the message and F10's bug
// report, until it is closed (instead of closing 3 s after the end). Empty: not stopped.
void set_game_stopped(const std::string& function);
std::string game_stopped();

}  // namespace b3replay

#endif
