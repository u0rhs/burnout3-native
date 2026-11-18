#include "live.h"
#include "b3gfx/perf.h"   // perf3
#include "b3port/app_icon.h"   // the built-in window icon
#include "clock.h"
#include "fastforward.h"   // fastload1: FAST LOADING's display rule
#include "nativeaudio.h"   // play1
#include "nativepad.h"   // play1
#include "pcpage.h"     // mac5: PC SETTINGS
#include "photomode.h"  // photo mode
#include "ghost.h"      // ghost1: Time Attack ghosts
#include "speedrun.h"   // speedrun1: the speedrun timer
#include "speedrunpanel.h"
#include "menumouse.h"  // mac5: the mouse in the game's menus
#include "carlook.h"
#include "carlookpanel.h"
#include "tuneconsole.h"   // tune4: the tuning console's F9 overlay

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <future>
#include <map>
#include <mutex>
#include <thread>

#include "b3asset/hdpack.h"   // loadlag1: an HD pack keeps the route load on the render thread
#include "b3asset/png.h"
#include "fpspanel.h"
#include "skiphint.h"
#include "modoverlay.h"   // sdk2b: the code mods' 2D
#include "notice.h"   // mac5
#include "b3report/bug_report.h"   // test builds: F10's bug report
#include "b3report/sysinfo.h"
#include "b3report/version.h"
#include "pcsettings.h"

namespace b3replay {

namespace {

// (LiveFeed itself, with its tick window and segment gap: livefeed.cpp)
constexpr std::size_t kRouteTicks = 30;        // race ticks before the route is matched (camera samples)
constexpr std::size_t kRescoreTicks = 600;     // matched once more with 10 s of samples (V1 / V2 share size and units)

// The current segment's kept ticks as a timeline (frames never blend across segments).
struct SegTicks : Timeline {
    const LiveFeed& f;
    std::size_t lo, hi;
    SegTicks(const LiveFeed& feed, std::size_t a, std::size_t b) : f(feed), lo(a), hi(b) {}
    std::size_t first() const override { return lo; }
    std::size_t last() const override { return hi; }
    const Tick& at(std::size_t i) const override { return f.at(i); }
};

struct FeedIndex {   // route_samples' accessor
    const LiveFeed& f;
    const Tick& operator[](std::size_t i) const { return f.at(i); }
};

// The view of the live session: loads the route of each race as it appears (and again if 10 s of samples choose
// another), cars on first sight (View::car_scene).
struct LiveScene {
    const LiveOptions& o;
    LiveFeed feed;
    View view;
    std::uint64_t matched_seg = 0;
    bool rescored = false;
    double load_ms_total = 0;
    int loads = 0;

    explicit LiveScene(const LiveOptions& opt) : o(opt), feed(opt.name) {
        feed.inproc = o.inproc;
        lead_max = o.latency == "low" ? 1.25 * std::clamp(o.lead, 0.0, 1.0) : 0.0;   // lat1 (clock.h step_sched's reach)
        view.game = o.game;
        view.car_lod = o.car_lod;
        view.show_effects = o.effects;
        view.show_coronas = o.coronas;
        view.show_particles = o.particles;
        view.show_sun = o.sun;
        view.sun_occlusion = o.sun_test;
        view.show_blur = o.blur;
        view.radial_undecided = o.blur_radial;
        view.show_bloom = o.bloom;
        view.radial_scale = o.radial_scale;
        view.post_game = o.post_game;
        view.show_hud = o.hud;
        view.show_movies = o.movies;
        view.show_room = o.room;
        view.room_reflection = o.reflection;
        view.room_shadow = o.fe_shadow;
        view.show_globe = o.globe;
        view.room_env = o.room_env;
        view.hud_wide = o.hud_wide;
        view.fe_layout = o.fe_layout;
        view.movie_fill = o.movie_fill;
        view.whole_track = o.whole_track;
        view.hud_sharp = o.hud_sharp;
        view.hud_reproject = o.hud_reproject;
        view.cars = scan_cars(o.game);
        if (o.settings) apply_view(view, *o.settings);   // settings1
    }

    bool ready() const {
        const auto& s = feed.segment();
        return s.active && !view.route.empty() && view.route_size == s.route_size && view.route_units == s.route_units && matched_seg == s.id;
    }

    // gsoff1 (§28.6): the race's route from the STATIC.DAT header in guest memory, loaded as soon as the game has it (during
    // its loading screen, 160..290 ticks before the first race tick in race2 / roadrage1 / gsoff1mp2), so the race draws
    // from its first tick. Before, the route waited for 30 race ticks of camera samples and then loaded (~0.7 s) on this
    // thread: the race's first ~1.2 s showed only the 2D on black. Routes with fewer than 16 units are skipped: the front
    // end's own static track US/C5_V1 (4 units; every race route has 36..162) stays out, so front-end frames are unchanged.
    std::string header_route;              // the unique route of the latest header ("" none / ambiguous / not loaded)
    std::uint32_t header_base = 0;
    std::array<std::uint8_t, 0x60> header_seen{};
    bool header_any = false;
    // loadlag1: with `async_routes` (the window; an offscreen bench without shots) the header's route is read and
    // decoded on a worker (View::prepare_track) while this thread goes on drawing, and only its GPU half
    // (View::load_prepared) runs here when it is done. With FAST LOADING the 166 ticks between the header and the race's
    // first tick pass in ~0.4 s instead of 2.8 s, so a load on this thread (~0.5 s) froze the window on the loading
    // screen while the race was already running and audible. Offscreen shots keep the load on this thread (a shot's
    // poll must find the route loaded whatever the timing: the golden's shots).
    bool async_routes = false;
    std::future<TrackPrep> prep_job;
    std::string want_route;                // the header's route that is to be loaded ("" none)
    std::uint32_t want_poll = 0;           // the poll whose header named it
    std::uint64_t prep_t0 = 0;
    bool loading_route() const { return prep_job.valid(); }
    // the header's route not built yet: on the worker, or waiting for it (b3r_scene_wait's scene_busy)
    bool route_pending() const { return prep_job.valid() || (!want_route.empty() && want_route != view.route); }
    // While the race's route is still being built, the window keeps the last tick before the race (the loading screen)
    // instead of the race's 2D on black: the index to show at most (-1: no limit).
    long hold_before_race() const {
        const auto& s = feed.segment();
        if (!async_routes || !route_pending() || !s.active || feed.empty() || s.first <= feed.first()) return -1;
        return long(s.first) - 1;
    }
    bool finish_route(b3gfx::Device& dev, TrackPrep&& p, std::uint32_t poll, std::uint64_t t0) {
        const std::uint64_t t1 = b3live::now_ns();
        const double prep_ms = p.ms;
        const std::string route = p.route;
        view.clear(dev);
        std::string why;
        if (!view.load_prepared(dev, std::move(p), &why)) {
            std::fprintf(stderr, "[b3live] %s: %s\n", route.c_str(), why.c_str());
            view.clear(dev);
            return false;
        }
        const std::uint64_t t2 = b3live::now_ns();
        const double ms = double(t2 - t0) / 1e6;
        load_ms_total += ms;
        ++loads;
        if (async_routes)
            std::printf("[b3live] route %s from the game's STATIC.DAT header at poll %u, loaded in %.0f ms (decode %.0f ms on a worker, "
                        "%.0f ms on this thread)%s\n", route.c_str(), poll, ms, prep_ms, double(t2 - t1) / 1e6, feed.segment().active ? "" : " (before the race)");
        else
            std::printf("[b3live] route %s from the game's STATIC.DAT header at poll %u, loaded in %.0f ms%s\n", route.c_str(), poll, ms,
                        feed.segment().active ? "" : " (before the race)");
        std::fflush(stdout);
        return true;
    }
    void preload(b3gfx::Device& dev) {
        if (prep_job.valid() && prep_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {   // loadlag1
            TrackPrep p = prep_job.get();
            if (p.route == want_route && p.route != view.route) {
                if (finish_route(dev, std::move(p), want_poll, prep_t0)) header_route = want_route;
                else want_route.clear();   // (not again until the header changes, as before)
            }
        }
        if (!prep_job.valid() && !want_route.empty() && want_route != view.route && want_route != header_route) start_route();   // a newer header's
        if (feed.empty()) return;
        const Tick& t = feed.at(feed.last());
        if (!t.route_head_ok) return;
        if (header_any && t.route_base == header_base && t.route_head == header_seen) return;   // unchanged
        header_any = true;
        header_base = t.route_base;
        header_seen = t.route_head;
        header_route.clear();
        want_route.clear();
        if (t.route_units < 16) return;
        const std::string route = route_by_header(o.game, t);
        if (route.empty()) return;
        if (route != view.route) {
            // (an HD pack: on this thread, as before -- its lookups and its prefetch are not made for two threads)
            if (async_routes && !b3asset::hd_textures()) {
                want_route = route;
                want_poll = t.poll;
                if (!prep_job.valid()) start_route();
                return;   // header_route once it is loaded
            }
            const std::uint64_t t0 = b3live::now_ns();
            if (!finish_route(dev, View::prepare_track(route, view.game), t.poll, t0)) return;
        }
        header_route = route;
    }
    void start_route() {
        prep_t0 = b3live::now_ns();
        prep_job = std::async(std::launch::async, [route = want_route, game = view.game] {
            SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);   // below the game's thread (as the render thread on Windows)
            return View::prepare_track(route, game);
        });
    }

    void update(b3gfx::Device& dev) {
        if (o.route_header) preload(dev);
        scene_busy().store(async_routes && route_pending(), std::memory_order_relaxed);   // loadlag1 (fastforward.h)
        const auto& s = feed.segment();
        if (!s.active || feed.empty()) return;
        // gsoff1: the header's route is this segment's: drawn from its first tick, no sample matching or rescore
        if (matched_seg != s.id && !header_route.empty() && header_route == view.route && view.route_size == s.route_size &&
            view.route_units == s.route_units) {
            matched_seg = s.id;
            rescored = true;
            std::printf("[b3live] segment %llu (from poll %u): route %s from the header\n", static_cast<unsigned long long>(s.id),
                        feed.at(std::max(s.first, feed.first())).poll, view.route.c_str());
            std::fflush(stdout);
            return;
        }
        // (loadlag1: not while the header's route is on its way from the worker)
        const bool first_match = matched_seg != s.id && s.race_ticks >= kRouteTicks && !loading_route();
        const bool rescore = matched_seg == s.id && !rescored && s.race_ticks >= kRescoreTicks;
        if (!first_match && !rescore) return;
        const std::size_t lo = std::max(s.first, feed.first());
        std::vector<const Tick*> samples;
        for (const Tick* t : route_samples(FeedIndex{feed}, lo, feed.last())) if (race_tick(*t)) samples.push_back(t);
        const std::string route = find_route(o.game, s.route_size, s.route_units, samples);
        if (first_match) { matched_seg = s.id; rescored = false; } else rescored = true;
        if (route.empty()) {
            std::fprintf(stderr, "[b3live] no TRACKS/*/*/STATIC.DAT with size %u and %d units under %s\n", s.route_size, s.route_units, o.game.c_str());
            view.clear(dev);
            return;
        }
        if (route == view.route) return;
        const std::uint64_t t0 = b3live::now_ns();
        view.clear(dev);
        std::string why;
        if (!view.load_track(dev, route, &why)) {
            std::fprintf(stderr, "[b3live] %s: %s\n", route.c_str(), why.c_str());
            view.clear(dev);
            return;
        }
        const double ms = double(b3live::now_ns() - t0) / 1e6;
        load_ms_total += ms;
        ++loads;
        std::printf("[b3live] segment %llu (from poll %u): route %s loaded in %.0f ms%s\n", static_cast<unsigned long long>(s.id),
                    feed.at(lo).poll, route.c_str(), ms, rescore ? " (rescored with 10 s of camera samples)" : "");
        std::fflush(stdout);
    }

    // The frame at absolute tick position x (index + fraction), within the current segment.
    double lead_max = 0;   // lat1: ticks the display may run past the newest tick (extrapolated from the last two)
    // `cap` (perf1's correction blend): the frame as it was computed with the ticks up to index `cap` only
    bool frame(double x, bool interpolate, Frame& f, std::size_t cap = ~std::size_t(0)) const {
        if (!ready()) return false;
        const std::size_t lo = std::max(feed.segment().first, feed.first()), hi = std::min(feed.last(), cap);
        if (hi < lo) return false;
        x = std::clamp(x, double(lo), double(hi) + (interpolate && hi > lo ? lead_max : 0.0));
        if (x > double(hi)) {   // lat1: past the newest tick (frac in (1, 1 + lead] from ticks hi - 1 and hi)
            const SegTicks seg(feed, lo, hi);
            // the newest tick's own state (HUD, visibility, effects, which cars and props there are) ...
            f = frame_at(seg, hi, 0.0, true, kCameraKeyReach, true);
            // ... with the motion extrapolated from the last two ticks: cameras, cars matched by id and model, dynamic
            // props by placement, the displayed time (fades, scrolls, the blur's clock: tick + alpha stays continuous)
            const Frame e = frame_at(seg, hi - 1, x - double(hi - 1), true, kCameraKeyReach, true);
            f.tick = e.tick;
            f.alpha = e.alpha;
            f.cam = e.cam;
            if (f.split && e.split) f.cam2 = e.cam2;
            f.fx.now = e.fx.now;
            f.anim_now = e.anim_now;
            for (auto& v : f.vehicles)
                for (const auto& w : e.vehicles)
                    if (w.id == v.id && w.data == v.data) {   // the motion only (lerp's fields); the rest is the newest tick's
                        v.main = w.main;
                        v.body = w.body;
                        for (int k = 0; k < v.nwheel && k < w.nwheel; ++k) v.wheel[k] = w.wheel[k];
                        for (int k = 0; k < v.nbit && k < w.nbit; ++k) v.bit[k] = w.bit[k];
                        v.spin = w.spin;
                        v.steer = w.steer;
                        v.speed = w.speed;
                        if (v.zones_valid && w.zones_valid) {
                            std::memcpy(v.zone, w.zone, sizeof v.zone);
                            std::memcpy(v.bit_zone, w.bit_zone, sizeof v.bit_zone);
                        }
                        v.shadow.ess = w.shadow.ess;
                        std::memcpy(v.shadow.normal, w.shadow.normal, sizeof v.shadow.normal);
                        v.shadow.dist = w.shadow.dist;
                        break;
                    }
            for (auto& q : f.props)
                for (const auto& w : e.props) if (w.placement == q.placement) { q.m = w.m; break; }
            f.time_attack = e.time_attack;   // ghost1: the lap's time at the displayed instant
            f.lap_index = e.lap_index;
            f.lap_time = e.lap_time;
            view.apply_fov(f.cam, f.vehicles);   // perf1: as interpolated frames (the drawn-only FOV and its dolly)
            return true;
        }
        const std::size_t i = std::size_t(std::floor(x));
        f = frame_at(SegTicks(feed, lo, hi), i, x - double(i), interpolate, kCameraKeyReach, true);
        view.apply_fov(f.cam, f.vehicles);   // settings1 (drawn-only FOV)
        return true;
    }

    // menu1 (§22): outside a race on screen (front end, loading screens, a race's first half second before its route is
    // matched): the 2D of the tick at position x of the whole feed (the HUD frame is per tick, not interpolated).
    bool frontend_frame(double x, Frame& f) const {
        if (feed.empty()) return false;
        x = std::clamp(x, double(feed.first()), double(feed.last()));
        const std::size_t i = std::size_t(std::floor(x));
        const Tick& t = feed.at(i);
        if (!t.hud) return false;
        if (t.fe_car) {   // the car pages: the car and its orbiting camera interpolated as in a race (within the car's run)
            std::size_t lo = i, hi = i;
            while (lo > feed.first() && feed.at(lo - 1).fe_car) --lo;
            while (hi < feed.last() && feed.at(hi + 1).fe_car) ++hi;
            f = frame_at(SegTicks(feed, lo, hi), i, x - double(i), true);
            return true;
        }
        f = Frame{};
        f.tick = i;
        f.hud = t.hud;
        return true;
    }

    // display_aspect (water1): the output's width / height as shown (4/3 for the PS2-shaped 640 x 448 image)
    void params(const Frame& f, float aspect, bool fog, bool spec, b3gfx::FrameParams& fp, float display_aspect = 0) const {
        b3view::frame_params(view.sky, f.cam.m.r[3], f.cam.m.r[2], fog, spec, fp);
        game_view_proj(f.cam, aspect, fp.view_proj);
        view.car_frame(fp);
        view.post_frame(f, display_aspect > 0 ? display_aspect : aspect, fp);
    }

    // ghost1 (ghost.h): Time Attack ghosts. Every tick once, in order, to the recorder (the player's car and the game's
    // lap state, read from the tick); a completed lap better than the file of its track + car replaces it; the best lap
    // of the loaded route (any car whose file the disc has) is the one view.ghost draws. GHOST off: nothing (no files,
    // no draws). B3_GHOST_TEST_LAP=<seconds> (tests): a lap's first seconds count as a lap.
    ghost::Recorder ghost_rec{[] { const char* e = std::getenv("B3_GHOST_TEST_LAP"); return e ? float(std::atof(e)) : 0.0f; }()};
    ghost::Lap ghost_best;
    std::string ghost_track;       // the route the loaded best lap is for ("" none)
    bool ghost_was_on = false;
    std::size_t ghost_next = 0;    // the next feed index to record
    std::uint64_t ghost_laps = 0, ghost_saved = 0;
    const bool ghost_debug = std::getenv("B3GHOST_DEBUG") != nullptr;
    std::string ghost_debug_last;
    void ghost_load(const std::string& track) {
        view.ghost = nullptr;
        view.ghost_file = -1;
        ghost_track = track;
        ghost_best = ghost::Lap{};
        std::string path;
        if (track.empty() || !ghost::best_for_track(o.ghost_dir, track, ghost_best, &path)) return;
        for (std::size_t k = 0; k < view.cars.size(); ++k)
            if (ghost::slug(ghost::last_two(view.cars[k].path), false) == ghost::slug(ghost_best.car, false)) { view.ghost_file = int(k); break; }
        if (view.ghost_file < 0) {
            std::printf("[b3ghost] %s: its car %s is not on this disc: no ghost\n", path.c_str(), ghost_best.car.c_str());
            return;
        }
        view.ghost = &ghost_best;
        std::printf("[b3ghost] ghost for %s: %s, lap %.3f s%s, %zu samples (%s)\n", track.c_str(), ghost_best.car.c_str(), double(ghost_best.time),
                    ghost_best.test ? " (a test lap)" : "", ghost_best.samples.size(), path.c_str());
        std::fflush(stdout);
    }
    void ghost_completed(ghost::Lap& lap) {
        ++ghost_laps;
        lap.track = ghost_track;
        const std::string path = (std::filesystem::path(o.ghost_dir) / ghost::file_name(lap.track, lap.car)).string();
        ghost::Lap old;
        const bool have_old = ghost::load(path, old);
        const bool better = !have_old || (old.test && !lap.test) || (old.test == lap.test && lap.time < old.time);
        std::printf("[b3ghost] lap %llu completed on %s with %s: %.3f s%s, %zu samples%s\n", static_cast<unsigned long long>(ghost_laps),
                    lap.track.c_str(), lap.car.c_str(), double(lap.time), lap.test ? " (a test lap)" : "", lap.samples.size(),
                    better ? "" : have_old ? " (not better than the saved one)" : "");
        if (better) {
            std::string why;
            if (ghost::save(path, lap, &why)) {
                ++ghost_saved;
                std::printf("[b3ghost] saved to %s\n", path.c_str());
                ghost_load(ghost_track);   // the new best (if it is the track's best) from the next lap on
            } else
                std::fprintf(stderr, "[b3ghost] %s\n", why.c_str());
        }
        std::fflush(stdout);
    }
    // traffic-popin: B3_TRAFFIC_PROBE=1 -- every new race tick through view.h traffic_probe (traffic coming within the
    // 275 m draw radius and leaving it, the fade-in's ticks), whether or not a frame shows it (offscreen runs draw few)
    std::size_t traffic_probed = 0;
    void probe_traffic() {
        static const bool on = std::getenv("B3_TRAFFIC_PROBE") != nullptr;
        if (!on || feed.empty() || !feed.segment().active) return;
        const std::size_t lo = std::max(feed.segment().first, feed.first()), hi = std::min(feed.last(), feed.segment().last_race);
        const SegTicks seg(feed, lo, hi);
        for (std::size_t i = std::max(traffic_probed, lo + 1); i <= hi; ++i) traffic_probe(seg, i);
        traffic_probed = std::max(traffic_probed, hi + 1);
    }

    // after update(): the new ticks to the recorder, the route's best lap loaded when the route changes
    void ghosts() {
        const bool on = view.ghost_mode != 0 && !o.ghost_dir.empty();
        if (!on) {
            if (ghost_was_on) { view.ghost = nullptr; view.ghost_file = -1; ghost_track.clear(); ghost_best = ghost::Lap{}; ghost_rec.reset(); }
            ghost_was_on = false;
            ghost_next = feed.empty() ? 0 : feed.last() + 1;
            return;
        }
        const std::string track = ready() ? ghost::last_two(view.route) : std::string();
        if (!ghost_was_on || (!track.empty() && track != ghost_track)) ghost_load(track);
        ghost_was_on = true;
        if (feed.empty()) return;
        for (std::size_t i = std::max(ghost_next, feed.first()); i <= feed.last(); ++i) {
            const Tick& t = feed.at(i);
            if (ghost_debug) {   // B3GHOST_DEBUG=1: the lap state on change (the mode, the lap, the logic's vtable)
                char b[160];
                std::snprintf(b, sizeof b, "valid %d time attack %d lap %d of %d logic vtable %08X race %d route %s", int(t.lap.valid),
                              int(t.lap.time_attack), t.lap.index, t.lap.laps, t.logic_vtable, int(race_tick(t)), track.c_str());
                if (ghost_debug_last != b || t.poll % 300 == 0) {
                    std::printf("[b3ghost] poll %u: %s (lap time %.3f)\n", t.poll, b, double(t.lap.time));
                    ghost_debug_last = b;
                }
            }
            ghost::TickInput in;
            in.time_attack = !track.empty() && t.lap.valid && t.lap.time_attack && race_tick(t);
            in.index = t.lap.index;
            in.time = t.lap.time;
            for (const Vehicle& v : t.vehicles) {
                if (v.id != kPlayerRaceCar || v.kind != VehicleKind::kRaceCar || v.model < 0 || std::size_t(v.model) >= feed.models().heads.size()) continue;
                const int file = view.car_file(feed.models().heads[std::size_t(v.model)]);
                if (file < 0) break;
                in.have_car = true;
                in.car = ghost::last_two(view.cars[std::size_t(file)].path);
                in.paint = v.paint;
                in.nwheel = v.parts ? std::min(v.nwheel, 6) : 0;
                in.pose.main = v.main;
                in.pose.body = v.body;
                for (int k = 0; k < in.nwheel; ++k) {
                    in.pose.wheel[k] = v.wheel[k];
                    in.pose.wheel_speed = std::max(in.pose.wheel_speed, std::fabs(v.wheel_speed[k]));
                }
                break;
            }
            ghost::Lap done;
            if (ghost_rec.step(in, &done)) ghost_completed(done);
        }
        ghost_next = feed.last() + 1;
    }

    static std::uint32_t b3_f32_bits(float x) { std::uint32_t b; std::memcpy(&b, &x, 4); return b; }   // speedrun1: shot logs

    // speedrun1 (speedrun.h): the speedrun timer. Every tick once, in order, to the tracker (the game's CB3Score timing,
    // read from the tick; the player's car and the route); a finish (or a Time Attack lap) better than the kept best of
    // its event + car replaces it (<speedrun_dir>/..., or this run's memory without a folder). SPEEDRUN TIMER off:
    // nothing runs (no file read or written, nothing drawn).
    speedrun::Tracker sr_tracker;
    bool sr_was_on = false;
    std::map<std::string, speedrun::Best> sr_bests;   // by file name: the kept best (time kNone: none), loaded once
    const bool sr_debug = std::getenv("B3_SPEEDRUN_DEBUG") != nullptr;
    std::string sr_debug_last;
    std::string sr_car;   // the player's car as last known in this event (a tick without it keeps it)
    std::string player_car(const Tick& t) {   // the player's race car's file ("COMP/CAR1.BGV"), "" unknown
        for (const Vehicle& v : t.vehicles) {
            if (v.id != kPlayerRaceCar || v.kind != VehicleKind::kRaceCar || v.model < 0 || std::size_t(v.model) >= feed.models().heads.size()) continue;
            const int file = view.car_file(feed.models().heads[std::size_t(v.model)]);
            return file < 0 ? std::string() : ghost::last_two(view.cars[std::size_t(file)].path);
        }
        return {};
    }
    speedrun::Best& sr_best(speedrun::Mode m, const std::string& track, const std::string& car, int laps) {
        const std::string name = speedrun::file_name(m, track, car, laps);
        auto it = sr_bests.find(name);
        if (it != sr_bests.end()) return it->second;
        speedrun::Best b;
        b.key = speedrun::event_key(m, track, car, laps);
        if (!o.speedrun_dir.empty()) {
            const std::string path = (std::filesystem::path(o.speedrun_dir) / name).string();
            std::string why;
            speedrun::Best f;
            if (speedrun::load(path, f, &why) && f.key == b.key) {
                b = f;
                std::printf("[speedrun] best of %s: %s, best lap %s (%s)\n", b.key.c_str(), speedrun::time_text(b.time).c_str(),
                            speedrun::time_text(b.best_lap).c_str(), path.c_str());
            } else if (std::filesystem::exists(path)) {
                std::fprintf(stderr, "[speedrun] %s: %s (ignored)\n", path.c_str(), why.empty() ? "another event's key" : why.c_str());
            }
        }
        return sr_bests.emplace(name, b).first->second;
    }
    void sr_result(const speedrun::Result& r) {
        speedrun::Best& b = sr_best(r.mode, r.track, r.car, r.laps);
        bool better = false;
        if (r.kind == speedrun::Mark::kFinish) {
            std::printf("[speedrun] finished %s in %s (%.6f s)\n", b.key.c_str(), speedrun::time_text(r.time).c_str(), double(r.time));
            for (std::size_t k = 0; k < r.splits.size(); ++k)
                std::printf("[speedrun]   lap %zu: %s (%.6f s)\n", k + 1, speedrun::time_text(r.splits[k]).c_str(), double(r.splits[k]));
            if (r.time < b.time) { b.time = r.time; b.splits = r.splits; better = true; }
            for (float s : r.splits) if (s > 0 && s < b.best_lap) { b.best_lap = s; better = true; }
        } else {
            std::printf("[speedrun] lap of %s: %s (%.6f s)\n", b.key.c_str(), speedrun::time_text(r.time).c_str(), double(r.time));
            if (r.time < b.best_lap) { b.best_lap = r.time; better = true; }
        }
        if (!better) return;
        std::printf("[speedrun] a new best for %s\n", b.key.c_str());
        if (o.speedrun_dir.empty()) return;
        const std::string path = (std::filesystem::path(o.speedrun_dir) / speedrun::file_name(r.mode, r.track, r.car, r.laps)).string();
        std::string why;
        if (speedrun::save(path, b, &why)) std::printf("[speedrun] saved to %s\n", path.c_str());
        else std::fprintf(stderr, "[speedrun] %s\n", why.c_str());
    }
    // the tracker sees every tick as the feed keeps it (LiveFeed::on_push: none missed however long a frame or a shot
    // takes); setting 0: no hook, nothing kept
    void speedrun_ticks(int setting) {
        if (setting <= 0) {
            if (sr_was_on) { sr_tracker = speedrun::Tracker{}; sr_bests.clear(); sr_car.clear(); feed.on_push = nullptr; }
            sr_was_on = false;
            return;
        }
        if (!sr_was_on) feed.on_push = [this](const Tick& t) { sr_tick(t); };
        sr_was_on = true;
    }
    void sr_tick(const Tick& t) {
        const std::string track = !view.route.empty() ? ghost::last_two(view.route) : std::string();
        speedrun::TickIn in;
        in.poll = t.poll;
        in.race = !track.empty() && race_tick(t);
        in.s = t.score;
        in.track = track;
        in.car = player_car(t);
        if (!in.race) sr_car.clear();
        else if (in.car.empty()) in.car = sr_car;
        else sr_car = in.car;
        const speedrun::Mode m = t.score.valid ? speedrun::mode_of(t.score.logic) : speedrun::Mode::kNone;
        const speedrun::Best* best = in.race && m != speedrun::Mode::kNone && !in.car.empty() ? &sr_best(m, track, in.car, t.score.laps) : nullptr;
        if (sr_debug) {   // B3_SPEEDRUN_DEBUG=1: the game's timing on change (and every 300 polls)
            char b[200];
            std::snprintf(b, sizeof b, "logic %08X race %d state %d finish %d lap %d done %d laps %d", t.score.logic, in.race ? 1 : 0,
                          t.score.state, t.score.finish, t.score.lap, t.score.laps_done, t.score.laps);
            if (sr_debug_last != b || t.poll % 300 == 0) {
                std::printf("[speedrun] poll %u: %s, race time %.6f (%s), lap time %.6f, game timer %.6f\n", t.poll, b, double(t.score.time),
                            speedrun::time_text(t.score.time).c_str(), double(t.score.lap_time), double(t.now));
                sr_debug_last = b;
            }
        }
        speedrun::Result r;
        const std::size_t nm = sr_tracker.marks().size();
        if (sr_tracker.step(in, best, &r)) sr_result(r);
        for (std::size_t k = nm; k < sr_tracker.marks().size(); ++k) {
            const speedrun::Mark& mk = sr_tracker.marks()[k];
            static const char* const kind[] = {"reset", "start", "lap", "finish", "out"};
            std::string what = kind[mk.kind];
            if (mk.kind == speedrun::Mark::kLap) what += " " + std::to_string(mk.lap) + " " + speedrun::time_text(mk.time);
            if (mk.kind == speedrun::Mark::kFinish) what += " " + speedrun::time_text(mk.time);
            std::printf("[speedrun] poll %u: %s (race time %.6f, %s)\n", mk.poll, what.c_str(), double(t.score.time), in.car.c_str());
        }
    }
    // the panel's rows for the tick at feed index i (empty: no timer there)
    std::vector<speedrun::Row> speedrun_rows(int setting, std::size_t i) const {
        if (setting <= 0 || feed.empty() || i < feed.first() || i > feed.last() || !ready()) return {};
        const Tick& t = feed.at(i);
        if (!race_tick(t) || !t.score.valid) return {};
        return speedrun::rows(setting, speedrun::mode_of(t.score.logic), t.score, sr_tracker.marks_at(t.poll));
    }

    // Photo mode (photomode.h): the frame's scene from the free camera `pc` -- the 3D only (no HUD, no pause menu), as
    // b3replay's free camera draws it (a free view: the sky's open cap faded, the bloom of this view, no culling by the
    // game camera, the coronas facing `pc`), the cars at the detail their distance from `pc` asks for, no zoom blur (the
    // game's blur is of its own camera). The game's camera in `f` is left as it is; the view's switches are restored.
    void photo_frame(b3gfx::Device& dev, Frame& f, const Camera& pc, float aspect, bool fog, bool spec, b3gfx::FrameParams& fp,
                     std::vector<b3gfx::DrawItem>& items, float display_aspect = 0) {
        b3view::frame_params(view.sky, pc.m.r[3], pc.m.r[2], fog, spec, fp);
        game_view_proj(pc, aspect, fp.view_proj);
        view.car_frame(fp);
        for (int k = 0; k < 3; ++k) f.cam.lod_pos[k] = pc.m.r[3][k];   // the LOD choice (game_lod) from the free camera
        const bool blur = view.show_blur;
        view.show_blur = false;
        view.free_view = true;
        view.corona_view = &pc;
        view.post_frame(f, display_aspect > 0 ? display_aspect : aspect, fp);
        view.render_aspect = aspect;
        view.draw_list(dev, f, feed.models().heads, false, fp.eye, items);
        view.corona_view = nullptr;
        view.free_view = false;
        view.show_blur = blur;
    }
    // photo2 (photo mode's depth of field): the player's car's position in `f` (its focus target; nullptr: not there)
    static const float* player_car(const Frame& f) {
        for (const Vehicle& v : f.vehicles)
            if (v.id == kPlayerRaceCar && v.kind == VehicleKind::kRaceCar) return v.body.r[3];
        return nullptr;
    }
};

using DisplayClock = DisplayClockT<LiveFeed>;
double pct(std::vector<double> v, double p);   // below

// perf1 (docs/native-renderer.md §33): low mode's correction blend. Past the newest tick the camera and the cars are
// extrapolated from the last two ticks; when the next tick arrives the pose computed with it differs from the
// extrapolated one (the chase camera's bob and the cars' contacts are not linear: one tick's miss is 4 mm median, 24 cm
// p99 in the fri1 recording), and that difference used to show at once as a jump. Now it is kept as a rigid correction per
// object (C = new^-1 x old, row vectors: the shown pose new x C equals the old one at that instant) that decays to the
// identity with time constant `ms`; the camera and each car (all its world matrices, by id) get their own. A cut (a
// correction moving more than 2 m) or a new segment drops them. Off in smooth / newest mode and with ms 0.
struct MotionBlend {
    struct D34 { double r[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}; };   // row vectors, as Mat34, in double
    static D34 d(const Mat34& m) { D34 o; for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) o.r[i][j] = m.r[i][j]; return o; }
    static D34 mul(const D34& a, const D34& b) {   // apply a, then b
        D34 o;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 3; ++j)
                o.r[i][j] = a.r[i][0] * b.r[0][j] + a.r[i][1] * b.r[1][j] + a.r[i][2] * b.r[2][j] + (i == 3 ? b.r[3][j] : 0.0);
        return o;
    }
    static D34 inv(const D34& m) {   // affine inverse (3x3 by cofactors, then the translation)
        const auto& r = m.r;
        const double det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                           r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
        D34 o;
        if (std::fabs(det) < 1e-12) return o;
        o.r[0][0] = (r[1][1] * r[2][2] - r[1][2] * r[2][1]) / det;
        o.r[0][1] = (r[0][2] * r[2][1] - r[0][1] * r[2][2]) / det;
        o.r[0][2] = (r[0][1] * r[1][2] - r[0][2] * r[1][1]) / det;
        o.r[1][0] = (r[1][2] * r[2][0] - r[1][0] * r[2][2]) / det;
        o.r[1][1] = (r[0][0] * r[2][2] - r[0][2] * r[2][0]) / det;
        o.r[1][2] = (r[0][2] * r[1][0] - r[0][0] * r[1][2]) / det;
        o.r[2][0] = (r[1][0] * r[2][1] - r[1][1] * r[2][0]) / det;
        o.r[2][1] = (r[0][1] * r[2][0] - r[0][0] * r[2][1]) / det;
        o.r[2][2] = (r[0][0] * r[1][1] - r[0][1] * r[1][0]) / det;
        for (int j = 0; j < 3; ++j) o.r[3][j] = -(r[3][0] * o.r[0][j] + r[3][1] * o.r[1][j] + r[3][2] * o.r[2][j]);
        return o;
    }
    static void decay(D34& c, double k) {   // c -> I + (c - I) k
        static const double I[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
        for (int r = 0; r < 4; ++r) for (int j = 0; j < 3; ++j) c.r[r][j] = I[r][j] + (c.r[r][j] - I[r][j]) * k;
    }
    // how far the correction moves the object at `at` (its position): |at C - at| plus its rotation's largest entry
    static double moves(const D34& c, const float at[3]) {
        double m = 0;
        for (int j = 0; j < 3; ++j) {
            const double p = at[0] * c.r[0][j] + at[1] * c.r[1][j] + at[2] * c.r[2][j] + c.r[3][j];
            m = std::max(m, std::fabs(p - at[j]));
            for (int i = 0; i < 3; ++i) m = std::max(m, std::fabs(c.r[i][j] - (i == j ? 1.0 : 0.0)));
        }
        return m;
    }
    static Mat34 apply(const Mat34& m, const D34& c) {
        const D34 o = mul(d(m), c);
        Mat34 r;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) r.r[i][j] = float(o.r[i][j]);
        return r;
    }
    static float dist(const float a[3], const float b[3]) {
        return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    }

    double ms = 0;
    std::size_t newest = ~std::size_t(0);
    std::uint64_t seg = ~0ull, last_ns = 0;
    D34 cam;
    bool cam_on = false;
    std::map<std::uint32_t, D34> cars;
    void reset() { cam_on = false; cars.clear(); }
    void apply(const LiveScene& ls, double x, bool interpolate, Frame& f, std::uint64_t now) {
        const std::size_t hi = ls.feed.last();
        const std::uint64_t sid = ls.feed.segment().id;
        if (ms <= 0 || !interpolate || sid != seg) { reset(); seg = sid; newest = hi; last_ns = now; return; }
        const double k = std::exp(-double(now - last_ns) * 1e-6 / ms);
        last_ns = now;
        if (cam_on) { decay(cam, k); cam_on = f.cam.valid && moves(cam, f.cam.m.r[3]) > 1e-5; }
        for (auto it = cars.begin(); it != cars.end();) { decay(it->second, k); it = std::next(it); }
        if (newest < hi && newest >= ls.feed.first()) {   // a new tick: the jump between the pose with the old ticks and with it
            Frame g;
            if (ls.frame(x, interpolate, g, newest)) {
                // shown = new x C must equal the shown old x C_prev now: C = new^-1 x old x C_prev
                if (g.cam.valid && f.cam.valid && dist(g.cam.m.r[3], f.cam.m.r[3]) < 2.0f) {
                    cam = mul(inv(d(f.cam.m)), mul(d(g.cam.m), cam_on ? cam : D34{}));
                    cam_on = true;
                } else {
                    cam_on = false;
                }
                std::map<std::uint32_t, D34> next;
                for (const auto& v : f.vehicles) {
                    if (v.id == 0) continue;
                    for (const auto& w : g.vehicles)
                        if (w.id == v.id && w.data == v.data) {
                            if (dist(w.main.r[3], v.main.r[3]) >= 2.0f) break;
                            const auto prev = cars.find(v.id);
                            next[v.id] = mul(inv(d(v.main)), mul(d(w.main), prev != cars.end() ? prev->second : D34{}));
                            break;
                        }
                }
                cars.swap(next);
            }
        }
        newest = hi;
        if (cam_on) f.cam.m = apply(f.cam.m, cam);
        for (auto& v : f.vehicles) {
            const auto it = cars.find(v.id);
            if (it == cars.end()) continue;
            if (moves(it->second, v.main.r[3]) < 1e-5) continue;
            const D34& c = it->second;
            v.main = apply(v.main, c);
            v.body = apply(v.body, c);
            for (int j = 0; j < v.nwheel; ++j) v.wheel[j] = apply(v.wheel[j], c);
            for (int j = 0; j < v.nbit; ++j) v.bit[j] = apply(v.bit[j], c);
        }
    }
};

// lat1: the display position of the latency mode.
//   low (default): the schedule clock (DisplayClockT::step_sched; the feed's latch stamps), else step() with `delay`
//   smooth: the arrival clock with `delay` (1.5 ticks: the behaviour before lat1)
//   newest: the newest tick as it arrives, no blend (60 Hz steps)
enum class Latency { kLow, kSmooth, kNewest };
Latency latency_mode(const LiveOptions& o) {
    return o.latency == "smooth" ? Latency::kSmooth : o.latency == "newest" ? Latency::kNewest : Latency::kLow;
}
double display_x(DisplayClock& c, const LiveFeed& f, const LiveOptions& o, double delay, std::uint64_t now) {
    // fastload1 (fastforward.h, clock.h ff_newest): an unpaced load shows its newest tick, and the clock resumes after it
    // without a step back
    if (c.ff_newest(f, fast_forward().load(std::memory_order_relaxed), latency_mode(o) == Latency::kLow)) {
        c.age_ms = (double(now) - double(f.arrival_ns(f.last()))) * 1e-6;
        return c.x;
    }
    switch (latency_mode(o)) {
    case Latency::kSmooth: return c.ff_after(f, c.step(f, delay, now));
    case Latency::kNewest: {
        c.x = double(f.last());
        c.ff_floor = -1;
        c.init = false;
        c.starved = true;
        c.age_ms = c.latch_age_ms(f, c.x, now);
        if (c.age_ms < 0) c.age_ms = (double(now) - double(f.arrival_ns(f.last()))) * 1e-6;
        return c.x;
    }
    case Latency::kLow: break;
    }
    return c.ff_after(f, c.step_sched(f, o.margin_ms, delay, now, o.lead));
}

// lat1: --latency-log <csv>: per drawn frame (race and front end) and per scripted input event, host steady-clock times
// for latency analysis. Kept in memory, written when the loop ends.
//   F,<t_loop>,<t_clock>,<newest poll>,<its arrival>,<shown poll>,<shown frac x1e6>,<next poll>,<t_done>,<latch age us>
//   I,<event>,<feed poll>,<t_sent>,<scancode>,<down>
//   T,<poll>,<pushed>,<stamp>,<drained>          every tick received: the hook's hand-over, its latch stamp, the drain
// t_loop: the frame's loop start (events, drain); t_clock: the display clock's sample; t_done: offscreen: the read-back
// finished (GPU done); window: the present was submitted.
struct LatencyLog {
    struct Row { std::uint64_t loop, clock, newest_ns, done; std::uint32_t newest, shown, next; std::int32_t frac, age_us; float cam[3]; };
    std::vector<Row> rows;
    struct TickRow { std::uint32_t poll; std::uint64_t pushed, stamp, drained; };
    std::vector<TickRow> ticks;
    std::uint64_t seen_total = 0;
    void drained(const LiveFeed& f, std::uint64_t now) {   // after each drain: the ticks it added
        if (!on() || f.empty()) return;
        const std::uint64_t got = f.ticks_total - seen_total;
        seen_total = f.ticks_total;
        const std::size_t n = std::size_t(std::min<std::uint64_t>(got, f.last() - f.first() + 1));
        for (std::size_t i = f.last() + 1 - n; i <= f.last(); ++i) {
            ticks.push_back({f.at(i).poll, f.arrival_ns(i), f.sched_ns(i), now});
            // one tick of extrapolation against the tick that then came (the lead's prediction error): the camera position
            // and the first race car's, in a race with the camera moving that tick (no cut, no 2-tick hold)
            if (i < f.first() + 2) continue;
            const Tick &a = f.at(i - 2), &b = f.at(i - 1), &c = f.at(i);
            const auto miss = [](const float* pa, const float* pb, const float* pc, float& moved) {
                float e = 0, m = 0;
                for (int j = 0; j < 3; ++j) { const float p = 2 * pb[j] - pa[j]; e += (p - pc[j]) * (p - pc[j]); m += (pc[j] - pb[j]) * (pc[j] - pb[j]); }
                moved = std::sqrt(m);
                return std::sqrt(e);
            };
            float moved = 0;
            if (a.cam.valid && b.cam.valid && c.cam.valid) {
                const float e = miss(a.cam.m.r[3], b.cam.m.r[3], c.cam.m.r[3], moved);
                if (moved > 0.01f && moved < 5.0f && e < 5.0f) cam_err.push_back(e);
            }
            const auto car = [](const Tick& t) -> const Vehicle* { for (const auto& v : t.vehicles) if (v.kind == VehicleKind::kRaceCar) return &v; return nullptr; };
            const Vehicle *va = car(a), *vb = car(b), *vc = car(c);
            if (va && vb && vc && va->id == vb->id && vb->id == vc->id) {
                const float e = miss(va->main.r[3], vb->main.r[3], vc->main.r[3], moved);
                if (moved > 0.01f && moved < 5.0f) car_err.push_back(e);
            }
        }
    }
    std::vector<double> cam_err, car_err;   // m
    std::string path;
    bool on() const { return !path.empty(); }
    void frame(const LiveFeed& f, const Frame& fr, const DisplayClock& c, std::uint64_t loop, std::uint64_t clock, std::uint64_t done) {
        if (!on() || f.empty()) return;
        const std::size_t i = std::min(fr.tick, f.last());
        // an extrapolated frame (alpha > 1) shows tick i + 1 fully plus the extrapolated part
        const bool ex = fr.alpha > 1.0f && i < f.last();
        const std::uint32_t shown = ex ? f.at(i + 1).poll : f.at(i).poll, next = ex ? shown : i < f.last() ? f.at(i + 1).poll : f.at(i).poll;
        rows.push_back({loop, clock, f.arrival_ns(f.last()), done, f.at(f.last()).poll, shown, next,
                        std::int32_t(double(ex ? fr.alpha - 1.0f : fr.alpha) * 1e6), std::int32_t(std::clamp(c.latch_age_ms(f, double(i) + double(fr.alpha), done) * 1e3, -1e9, 1e9)),
                        {fr.cam.m.r[3][0], fr.cam.m.r[3][1], fr.cam.m.r[3][2]}});   // perf1: the drawn camera (motion per frame)
    }
    void write(const LiveOptions& o, const char* present, int fif) const {
        if (!on()) return;
        std::FILE* out = std::fopen(path.c_str(), "w");
        if (!out) { std::fprintf(stderr, "[b3live] cannot write %s\n", path.c_str()); return; }
        std::fprintf(out, "# b3lat-render 1 mode=%s margin_ms=%.2f delay=%.2f present=%s frames_in_flight=%d bench=%dx%d\n", o.latency.c_str(),
                     o.margin_ms, o.delay, present, fif, o.bench_w, o.bench_h);
        for (const Row& r : rows)
            std::fprintf(out, "F,%llu,%llu,%u,%llu,%u,%d,%u,%llu,%d,%.4f,%.4f,%.4f\n", static_cast<unsigned long long>(r.loop), static_cast<unsigned long long>(r.clock),
                         r.newest, static_cast<unsigned long long>(r.newest_ns), r.shown, r.frac, r.next, static_cast<unsigned long long>(r.done), r.age_us,
                         double(r.cam[0]), double(r.cam[1]), double(r.cam[2]));
        for (const TickRow& t : ticks)
            std::fprintf(out, "T,%u,%llu,%llu,%llu\n", t.poll, static_cast<unsigned long long>(t.pushed), static_cast<unsigned long long>(t.stamp),
                         static_cast<unsigned long long>(t.drained));
        std::fclose(out);
        std::printf("[b3live] latency log: %zu frames -> %s\n", rows.size(), path.c_str());
        if (!cam_err.empty() && !car_err.empty())
            std::printf("[b3live] one tick extrapolated vs the next tick (m): camera p50/p99/max %.4f/%.4f/%.3f (%zu ticks), player car %.4f/%.4f/%.3f (%zu)\n",
                        pct(cam_err, 50), pct(cam_err, 99), pct(cam_err, 100), cam_err.size(), pct(car_err, 50), pct(car_err, 99), pct(car_err, 100), car_err.size());
        std::fflush(stdout);
    }
};

// gsoff1 (§28.6): per race segment, when the first displayed frame showed the race, and how many displayed frames since the
// segment's first tick arrived showed something else (the 2D alone or nothing).
struct RaceStartLog {
    std::uint64_t seg = ~0ull, t0 = 0, handed = 0;
    std::uint32_t first_poll = 0;
    bool done = true;
    int other = 0;
    void frame(const LiveScene& ls, bool race, std::uint32_t shown_poll) {
        const auto& s = ls.feed.segment();
        if (s.active && s.id != seg) {
            seg = s.id;
            done = false;
            t0 = b3live::now_ns();
            const std::size_t i = std::max(s.first, ls.feed.first());
            first_poll = ls.feed.at(i).poll;
            handed = ls.feed.arrival_ns(i);   // loadlag1: when the game handed the tick over (a busy render thread drains it later)
            other = 0;
        }
        if (done) return;
        if (!race) { ++other; return; }
        const std::uint64_t now = b3live::now_ns();
        std::printf("[b3live] segment %llu: first race frame shows poll %u (segment from poll %u), %d displayed frames before it without the race, %.0f ms after the segment's first tick"
                    " (%.0f ms after the game handed it over)\n",
                    static_cast<unsigned long long>(seg), shown_poll, first_poll, other, double(now - t0) / 1e6,
                    handed && now > handed ? double(now - handed) / 1e6 : 0.0);
        std::fflush(stdout);
        done = true;
    }
};

const char* state_name(LiveFeed::State s) {
    switch (s) {
    case LiveFeed::State::kNoWriter: return "waiting for the game";
    case LiveFeed::State::kWaitKey: return "waiting for a keyframe";
    case LiveFeed::State::kSynced: return "live";
    case LiveFeed::State::kEnded: return "session ended";
    }
    return "?";
}

double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, std::size_t(p / 100.0 * double(v.size())))];
}

// hdkey: a notice line in the stroke font's letters (A-Z, 0-9, a few marks: glyphdraw.h): a translation's accented
// capitals as their base letters (UTF-8 Latin-1: É -> E, Ä -> A, ß -> SS), apostrophes left out
std::string notice_letters(const std::string& s) {
    static const char* const kBase[64] = {   // U+00C0 .. U+00FF
        "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I", "D", "N", "O", "O", "O", "O", "O", "x",
        "O", "U", "U", "U", "U", "Y", "TH", "SS", "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
        "D", "N", "O", "O", "O", "O", "O", "/", "O", "U", "U", "U", "U", "Y", "TH", "Y"};
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d >= 0x80 && d <= 0xBF) { out += kBase[d - 0x80]; ++i; continue; }
        }
        if (c != '\'' && c < 0x80) out += char(std::toupper(c));   // (other UTF-8: left out)
    }
    return out;
}

// 1.2 HD TEXTURES key (hdkey, issue #7): the HD pack's replacements or the original textures, at once -- the renderer
// keeps both on the GPU (Device::set_texture_original) and every draw samples the one shown. A switch for this run, to
// compare: the HD TEXTURES setting stays as chosen (it decides whether the pack is read at the start, and so what the
// next start shows). No player's pack loaded: nothing to switch (a mod's textures are part of the mod and stay).
// Returns the notice's lines, in our own text's language.
std::vector<std::string> hd_key(b3gfx::Device& dev, const b3settings::Settings& s, const std::string& language, const std::string& by) {
    const auto tr = [&](const char* t) { return notice_letters(b3settings::translate_text(t, language)); };
    if (!b3asset::hd_switchable()) {
        const bool off = !s.hd_pack_dir.empty() && !s.hd_textures;
        std::printf("[b3hd] HD TEXTURES key (%s): no HD texture pack in this run%s\n", by.c_str(),
                    off ? " (a folder is chosen; HD TEXTURES is off in the settings)" : "");
        std::fflush(stdout);
        std::vector<std::string> lines{tr("NO HD TEXTURE PACK")};
        if (off) lines.push_back(tr("TURN ON HD TEXTURES IN SETTINGS AND START AGAIN"));
        return lines;
    }
    dev.set_hd_shown(!dev.hd_shown());
    const b3gfx::Device::HdOriginals o = dev.hd_originals();
    std::printf("[b3hd] HD TEXTURES key (%s): %s; %zu original textures kept beside their replacements, %.2f MB (the replacements: %.2f MB)\n",
                by.c_str(), dev.hd_shown() ? "the HD pack shown" : "the original textures shown", o.textures, double(o.bytes) / (1024.0 * 1024.0),
                double(o.hd_bytes) / (1024.0 * 1024.0));
    std::fflush(stdout);
    return {tr(dev.hd_shown() ? "HD TEXTURES ON" : "HD TEXTURES OFF")};
}

}  // namespace

// perf (2026-11-12): where a window frame's time goes, per kind of frame (race / front end), printed when the window
// closes: the swapchain wait at the loop's top (low / newest latency), the CPU part (events, drain, frame, draw lists),
// the draw call (swapchain acquire, command recording, submit) and the whole loop. Host timing only.
struct WindowStats {
    struct Kind { std::vector<float> wait, cpu, draw, loop; double secs = 0; };
    Kind k[2];   // 0 race, 1 front end
    static constexpr std::size_t kMax = 4u << 20;
    void add(int kind, std::uint64_t t0, std::uint64_t t1, std::uint64_t t2, std::uint64_t t3) {
        Kind& c = k[kind];
        c.secs += double(t3 - t0) * 1e-9;
        if (c.loop.size() >= kMax) return;
        c.wait.push_back(float(double(t1 - t0) * 1e-6));
        c.cpu.push_back(float(double(t2 - t1) * 1e-6));
        c.draw.push_back(float(double(t3 - t2) * 1e-6));
        c.loop.push_back(float(double(t3 - t0) * 1e-6));
    }
    static double p(const std::vector<float>& v, double q) {
        std::vector<double> d(v.begin(), v.end());
        return pct(std::move(d), q);
    }
    static void print_decode(const LiveFeed& feed) {   // the records' decode on this thread (drain)
        std::vector<float> by[2];   // other ticks, race ticks
        for (float x : feed.decode_ms) by[x < 0 ? 1 : 0].push_back(std::fabs(x));
        for (int k = 0; k < 2; ++k) {
            const auto& v = by[k];
            if (v.empty()) continue;
            double total = 0;
            for (float x : v) total += x;
            std::printf("[b3live] feed decode (%s ticks): %zu records, %.0f ms in all; ms per record mean %.3f, p50/p95/p99/max %.3f/%.3f/%.3f/%.3f\n",
                        k ? "race" : "other", v.size(), total, total / double(v.size()), p(v, 50), p(v, 95), p(v, 99), p(v, 100));
        }
        std::fflush(stdout);
    }
    void print() const {
        for (int i = 0; i < 2; ++i) {
            const Kind& c = k[i];
            if (c.loop.empty()) continue;
            std::printf("[b3live] window %s frames: %zu, %.1f fps; ms p50/p95/p99: loop %.2f/%.2f/%.2f, swapchain wait %.2f/%.2f/%.2f,"
                        " CPU (events, drain, frame, lists) %.2f/%.2f/%.2f, draw (acquire, record, submit) %.2f/%.2f/%.2f\n",
                        i ? "front-end" : "race", c.loop.size(), c.secs > 0 ? double(c.loop.size()) / c.secs : 0.0, p(c.loop, 50), p(c.loop, 95),
                        p(c.loop, 99), p(c.wait, 50), p(c.wait, 95), p(c.wait, 99), p(c.cpu, 50), p(c.cpu, 95), p(c.cpu, 99), p(c.draw, 50),
                        p(c.draw, 95), p(c.draw, 99));
        }
        std::fflush(stdout);
    }
};

int run_live_offscreen(const LiveOptions& o) {
    b3gfx::Options opt;
    opt.msaa = o.msaa;
    opt.dither = o.dither;   // perf1
    if (o.settings) { filter_options(o.settings->filter, opt); scale_options(*o.settings, opt); }   // settings1
    if (o.settings && o.settings->dither) opt.dither = 1.0f;   // perf1: DITHER
    opt.driver = o.driver;
    std::string err;
    SDL_Window* hidden = nullptr;   // Metal: b3gfx::create_headless (render5)
    auto dev = b3gfx::create_headless(opt, &err, &hidden);
    if (!dev) { std::fprintf(stderr, "b3gfx: %s\n", err.c_str()); return 1; }
    std::printf("[b3live] offscreen: feed %s, driver %s, %zu shots, bench %dx%d\n", o.name.c_str(), dev->driver_name(), o.shots.size(),
                o.bench_w, o.bench_h);
    std::fflush(stdout);
    if (o.bench_w > 0 && b3perf::init_from_env()) {
        char inf[160];
        std::snprintf(inf, sizeof inf, "offscreen-bench driver=%s msaa=%d", dev->driver_name(), dev->msaa());
        b3perf::set_info(inf);
    }
    LiveScene ls(o);
    ls.async_routes = o.bench_w > 0 && o.shots.empty();   // loadlag1: the bench without shots draws as the window does
    Capture ref;
    if (!o.verify.empty()) {
        if (!ref.load(o.verify, &err, false)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        ls.feed.verify = &ref;
        for (std::size_t i = 0; i < ref.ticks.size(); ++i) ls.feed.verify_index[ref.ticks[i].poll] = i;
    }
    std::vector<bool> shot_done(o.shots.size(), false);
    int shots_written = 0;
    // tunenav1: the tuning console's favourites (offscreen: beside --settings, or B3TUNE_FAVOURITES, else kept in memory)
    TuneConsole::instance().set_user_dir(o.settings_path.empty() ? std::string() : std::filesystem::path(o.settings_path).parent_path().string());
    carlook::load_user(o.settings_path.empty() ? std::string() : std::filesystem::path(o.settings_path).parent_path().string());   // reflect2
    DisplayClock clock;
    clock.old_rule = o.clock_old;
    MotionBlend blend;   // perf1: low mode's correction blend (set below, once the latency mode is known)
    std::vector<double> t_build, t_draw, t_frame;   // ms per bench frame: drain + frame + draw list, GPU draw + read-back, whole
    std::vector<double> fe_build, fe_draw, fe_frame;   // menu1: the same for front-end frames
    std::vector<b3gfx::DrawItem> items;
    std::vector<b3gfx::Device::ViewPass> views;   // gsoff1 (§29): split-screen frames
    b3gfx::FrameParams sfp;
    std::vector<std::uint8_t> rgba;
    const std::uint64_t start = b3live::now_ns();
    std::uint64_t bench_frames = 0, bench_failed = 0;
    // display clock statistics of the bench frames (live2): per tick rate class (races 60, crashes 30)
    struct ClockStats { std::vector<double> lag_ms, speed; std::uint64_t frames = 0, starved = 0, backwards = 0; };
    ClockStats cstat[2];
    double prev_x = -1;
    std::uint64_t prev_ns = 0, prev_seg = ~0ull;
    RaceStartLog race_log;   // gsoff1 (bench frames)
    if (o.native_pad) NativePad::instance().enable();   // play1: headless, the pad takes scripted keys (b3r_pad_key)
    if (o.native_pad) PcPage::instance().controls_start(o.controls_path);   // remap1: the bindings (empty: the defaults)
    LatencyLog lat_log;   // lat1
    lat_log.path = o.latency_log;
    const Latency lat_mode = latency_mode(o);
    blend.ms = lat_mode == Latency::kLow && o.lead > 0 ? o.blend_ms : 0.0;   // perf1
    std::vector<double> sched_age;   // lat1: latch -> read-back done of the bench's race frames (ms)
    PcPage& pc_page = PcPage::instance();   // mac5
    if (o.settings) pc_page.settings() = *o.settings;
    NativePad::instance().set_boost_toggle(pc_page.settings().boost_mode == 1);   // accessibility: BOOST Toggle (opt-in)
    NativePad::instance().set_instant_restart(pc_page.settings().instant_restart == 1);   // speedrun1 (opt-in, input-injecting)
    NativePad::instance().set_series_shoulders(pc_page.settings().series_shoulders == 1);   // carsel1 (opt-in, input-changing)
    NativePad::instance().set_rumble(pc_page.settings().rumble == 1, pc_page.settings().rumble_strength, pc_page.settings().rumble_triggers);   // rumble1
    NativePad::instance().set_analog_pedals(pc_page.settings().analog_pedals);   // 1.0.1
    NativePad::instance().set_adaptive_triggers(pc_page.settings().adaptive_triggers == 1, pc_page.settings().adaptive_triggers_strength);   // pad1
    std::uint64_t pc_page_ns = 0;
    SkipHint skip_hint;   // mac5
    SpeedrunPanel speedrun_panel;   // speedrun1
    // hdkey: a script's HD TEXTURES key switches at the poll it was stamped with (NativePad::take_hd_presses), so a shot
    // of poll N shows the textures, and the key's notice, as the window would at N (the notice's age in game time)
    Notice hd_notice;
    std::uint32_t hd_notice_poll = 0;
    float hd_notice_age = 0.0f;
    // photo mode in game time (photomode.h): the free camera follows the game's polls, its keys stamped on the game
    // thread; a shot of poll N shows it at N however far the game has run ahead. The shots are taken in poll order
    // and the camera is never replayed past a shot still to be taken.
    if (o.native_pad) PhotoMode::instance().enable_game_time();
    std::vector<std::size_t> shot_order(o.shots.size());
    for (std::size_t k = 0; k < shot_order.size(); ++k) shot_order[k] = k;
    std::stable_sort(shot_order.begin(), shot_order.end(), [&](std::size_t a, std::size_t b) { return o.shots[a].first < o.shots[b].first; });
    auto photo_game_cam = [&](std::uint32_t p, Camera& cam) {   // the game's camera at poll p (else the newest)
        long i = ls.feed.index_of_poll(p);
        if (i < 0 || std::size_t(i) < ls.feed.segment().first) i = long(ls.feed.last());
        Frame pf;
        if (!ls.frame(double(i), false, pf) || pf.split || !pf.cam.valid) return false;
        cam = pf.cam;
        return true;
    };
    while (true) {
        const std::uint64_t f0 = b3live::now_ns();
        { B3PERF_SCOPE(kFeedDrain); ls.feed.drain(); }
        lat_log.drained(ls.feed, b3live::now_ns());   // lat1
        if (ls.feed.state() == LiveFeed::State::kNoWriter && o.wait_writer_s > 0 && double(f0 - start) * 1e-9 > o.wait_writer_s) {
            std::fprintf(stderr, "[b3live] no writer %s after %.0f s\n", o.name.c_str(), o.wait_writer_s);
            return 1;
        }
        { B3PERF_SCOPE(kSceneUpdate); ls.update(*dev); }
        scene_epoch().fetch_add(1, std::memory_order_release);   // loadlag1: a pass with the feed drained (fastforward.h)
        ls.probe_traffic();   // traffic-popin (B3_TRAFFIC_PROBE=1)
        ls.ghosts();   // ghost1
        ls.speedrun_ticks(PcPage::instance().settings().speedrun_timer);   // speedrun1
        // accessibility diagnostics (SCREEN FLASHES, a11y.h): B3_FLASH_TRACE=1 lists every tick's large light 2D draws as
        // they arrive (the shots alone see few ticks), with what Reduced makes of each
        if (static const bool flash_trace = std::getenv("B3_FLASH_TRACE") != nullptr; flash_trace && !ls.feed.empty()) {
            static std::size_t scanned = 0;
            for (std::size_t i = std::max(scanned + 1, ls.feed.first()); i <= ls.feed.last(); ++i) {
                scanned = i;
                const Tick& tk = ls.feed.at(i);
                if (tk.post.valid) {   // the game's own screen passes, on change: the bloom's strength, the crash pulse
                    static float last[5] = {-1, -1, -1, -1, -1};
                    const float now[5] = {tk.post.bloom_on ? 1.0f : 0.0f, tk.post.bloom_threshold, tk.post.bloom_intensity,
                                          tk.post.dir_amount, float(tk.post.branch)};
                    if (std::memcmp(now, last, sizeof now) != 0) {
                        std::memcpy(last, now, sizeof now);
                        std::printf("[a11y-trace] poll %u post: bloom %d threshold %.3f intensity %.3f feedback %.3f, dir blur %.3f, branch %d choice %d\n",
                                    tk.poll, tk.post.bloom_on ? 1 : 0, double(tk.post.bloom_threshold), double(tk.post.bloom_intensity),
                                    double(tk.post.bloom_feedback), double(tk.post.dir_amount), tk.post.branch, tk.post.choice);
                    }
                }
                if (!tk.hud) continue;
                for (std::size_t di = 0; di < tk.hud->draws.size(); ++di) {
                    const HudDraw& d = tk.hud->draws[di];
                    if (d.tri.empty() || d.video >= 0) continue;
                    std::string name;
                    const a11y::FlashDraw fd = ls.view.hud_flash_draw(*tk.hud, d, ls.feed.models().hud.textures, &name);
                    if (fd.coverage < 0.3f || (fd.light < 0.3f && name.rfind(a11y::kStaticFirstFrame, 0) != 0)) continue;
                    const a11y::FlashCut c = a11y::flash_cut(fd);
                    std::printf("[a11y-trace] poll %u draw %zu: %s cover %.2f light %.2f white %.2f alpha %.2f gs %02X abe %d -> %.3f%s\n", tk.poll, di,
                                name.empty() ? "(fill)" : name.c_str(), double(fd.coverage), double(fd.light), double(fd.white), double(fd.alpha), d.alpha,
                                d.abe ? 1 : 0, double(c.factor), c.scale_alpha ? " (alpha)" : "");
                }
            }
        }
        // mac5 debugging (the pause FOV blip): B3LIVE_CAM_PROBE=<from poll>:<to poll> prints, as each tick arrives, the
        // camera a window would show between the two newest ticks (x = last - 1 + 0 .. 1, interpolated as live)
        if (static const char* probe = std::getenv("B3LIVE_CAM_PROBE"); probe && ls.ready() && !ls.feed.empty()) {
            static std::size_t probed = 0;
            unsigned p0 = 0, p1 = 0;
            const std::size_t li = ls.feed.last();
            if (std::sscanf(probe, "%u:%u", &p0, &p1) == 2 && li != probed && li > ls.feed.segment().first) {
                probed = li;
                const std::uint32_t poll = ls.feed.at(li).poll;
                if (poll >= p0 && poll <= p1)
                    for (int k = 0; k < 4; ++k) {
                        Frame pf;
                        if (!ls.frame(double(li - 1) + 0.25 * k, true, pf)) continue;
                        std::printf("[camprobe] poll %u tpf %d x %.2f cam %.3f %.3f %.3f\n", poll, ls.feed.at(li).ticks_per_frame,
                                    0.25 * k, double(pf.cam.m.r[3][0]), double(pf.cam.m.r[3][1]), double(pf.cam.m.r[3][2]));
                    }
            }
        }
        // mac5 (remaster-features §3): the button prompts' device (BUTTON PROMPTS, else the pad's last used; offscreen
        // shots: B3_PROMPT_DEVICE) and the key caps
        ls.view.prompt_device = prompt_resolve(pc_page.settings().button_prompts, NativePad::instance().device());
        ls.view.prompt_keys = NativePad::instance().prompt_keys(ls.view.prompt_device);   // remap1: a pad's moved buttons too
        pc_page.set_prompts(ls.view.prompt_device, ls.view.prompt_keys);
        if (!ls.feed.empty()) ls.view.hud_fonts = ls.feed.at(ls.feed.last()).host_fonts;   // the title's Start glyph
        if (o.native_pad) {   // mac5: the PC SETTINGS page (tests: scripted keys reach it through NativePad as in a window)
            const std::uint64_t t = b3live::now_ns();
            const float dt = pc_page_ns ? float(double(t - pc_page_ns) * 1e-9) : 1.0f / 60.0f;
            pc_page_ns = t;
            const MenuMouse::PagePointer mp = MenuMouse::instance().page_pointer();   // mac5
            const float pa = o.size_set ? float(o.w) / float(o.h) : 4.0f / 3.0f;
            if (pc_page.frame(NativePad::instance().last(), dt, o.settings_path, &mp, pa)) {
                apply_view(ls.view, pc_page.settings());
                NativePad::instance().set_boost_toggle(pc_page.settings().boost_mode == 1);
                NativePad::instance().set_instant_restart(pc_page.settings().instant_restart == 1);
                NativePad::instance().set_series_shoulders(pc_page.settings().series_shoulders == 1);
                NativePad::instance().set_rumble(pc_page.settings().rumble == 1, pc_page.settings().rumble_strength, pc_page.settings().rumble_triggers);
                NativePad::instance().set_analog_pedals(pc_page.settings().analog_pedals);   // 1.0.1
                NativePad::instance().set_adaptive_triggers(pc_page.settings().adaptive_triggers == 1, pc_page.settings().adaptive_triggers_strength);
            }
            // mac5 (menumouse.h): scripted mouse events against the newest tick's 2D, laid out as a shot of it would be
            MenuMouse& mm = MenuMouse::instance();
            if (mm.pending() && !ls.feed.empty()) {
                const std::size_t li = ls.feed.last();
                const Tick& tk = ls.feed.at(li);
                const int mw = o.size_set ? o.w : 640, mh = o.size_set ? o.h : 448;
                std::vector<b3gfx::DrawItem> scratch;
                std::shared_ptr<const HudFrame> laid;
                if (ls.ready() && li >= ls.feed.segment().first) {   // a race tick (the pause menu: the race's 2D layout)
                    if (tk.hud) { ls.view.hud_list(*dev, tk.hud, ls.feed.models().hud.textures, pa, scratch, mh, mw, nullptr); laid = tk.hud; }
                } else {
                    Frame lf;
                    b3gfx::FrameParams lfp;
                    if (ls.frontend_frame(double(li), lf)) {
                        ls.view.frontend_frame(*dev, lf, ls.feed.models().heads, ls.feed.models().hud.textures, pa, mw, mh, lfp, scratch);
                        laid = lf.hud;
                    }
                }
                MenuTargets mt;
                if (laid && ls.view.hud_pieces_frame == laid)
                    mt = menu_targets_of(*laid, ls.view.hud_draw_pieces, tk.menu.get(), ls.feed.models().hud.textures);
                if (std::getenv("B3_MENU_MOUSE_TRACE")) {
                    std::printf("[b3mouse] poll %u: %zu rows, %zu prompts, menu %d, text %d\n", tk.poll, mt.rows.size(), mt.prompts.size(),
                                mt.menu ? 1 : 0, mt.text_entry ? 1 : 0);
                    if (tk.menu)
                        for (const b3live::MenuSelect& s : tk.menu->selects)
                            std::printf("[b3mouse]   record select %08X row %u of %u%s, rows %08X %08X ...\n", s.addr, s.index, s.count,
                                        s.idle ? " idle" : "", s.rows[0], s.rows[1]);
                    if (laid && std::getenv("B3_MENU_MOUSE_TRACE")[0] == '2')
                        for (const HudObject& ob : laid->objects) std::printf("[b3mouse]   object %08X pc %08X\n", ob.addr, ob.pc);
                    for (const MenuRowTarget& r : mt.rows)
                        std::printf("[b3mouse]   row %08X/%u %.3f,%.3f-%.3f,%.3f\n", r.select, r.row, double(r.box.x0), double(r.box.y0),
                                    double(r.box.x1), double(r.box.y1));
                    for (const MenuPromptTarget& q : mt.prompts)
                        std::printf("[b3mouse]   prompt %u %.3f,%.3f-%.3f,%.3f\n", q.button, double(q.box.x0), double(q.box.y0), double(q.box.x1),
                                    double(q.box.y1));
                    std::fflush(stdout);
                }
                mm.frame(mt, pc_page.active(), double(SDL_GetTicks()) / 1000.0);
            }
        }
        // tunenav1: B3TUNE_OVERLAY_SCRIPT drives the F9 overlay as keys would, on the game's newest poll (tuning_check.sh)
        if (!ls.feed.empty()) TuneConsole::instance().script_step(ls.feed.at(ls.feed.last()).poll);
        // photo mode (photomode.h; scripted keys reach it through NativePad): a race on screen can be photographed; the
        // free camera replayed in game time to the newest poll, or to the first shot still to be taken
        if (o.native_pad && !ls.feed.empty()) {
            PhotoMode& photo = PhotoMode::instance();
            const std::size_t li = ls.feed.last();
            photo.set_scene(ls.ready() && li >= ls.feed.segment().first && !pc_page.active());
            std::uint32_t to = ls.feed.at(li).poll;
            for (const std::size_t k : shot_order)
                if (!shot_done[k]) { to = std::min(to, o.shots[k].first); break; }
            if (to > photo.game_time_poll()) photo.advance_to(to, photo_game_cam);
        }
        const bool ended = ls.feed.state() == LiveFeed::State::kEnded;
        // shots: the tick of the poll once the camera keys after it have arrived (or the session ended)
        for (const std::size_t k : shot_order) {
            if (shot_done[k] || ls.feed.empty()) continue;
            const auto [poll, alpha] = o.shots[k];
            if (!ended && ls.feed.at(ls.feed.last()).poll < poll + std::uint32_t(kCameraKeyReach) + 1) continue;
            shot_done[k] = true;
            if (o.native_pad && poll > PhotoMode::instance().game_time_poll()) PhotoMode::instance().advance_to(poll, photo_game_cam);
            if (o.native_pad)   // hdkey: the script's HD TEXTURES presses up to this poll
                for (const std::uint32_t p : NativePad::instance().take_hd_presses(poll)) {
                    hd_notice.show(hd_key(*dev, pc_page.settings(), pc_page.ui_language(), "script, poll " + std::to_string(p)), 1.5f);
                    hd_notice_poll = p;
                    hd_notice_age = 0.0f;
                }
            const long idx = ls.feed.index_of_poll(poll);
            bool spec = o.specular;
            FpsView fps_shot = o.fps_counter ? FpsView::kStandard : FpsView::kOff;   // fpsoverlay2: the view the shot's settings ask for
            FpsCorner fps_shot_corner = FpsCorner::kTopLeft;
            FpsInfo fps_shot_info;   // settings1: the settings in effect at the shot's tick
            if (o.settings) {   // no record at the tick: the settings of the start, as the live window fills them
                fps_shot_info.preset = b3settings::preset_name(b3settings::matching_preset(*o.settings));
                fps_shot_info.msaa = dev->msaa();
                fps_shot_info.target_fps = o.settings->frame_cap > 0 ? float(o.settings->frame_cap) : 60.0f;
            } else {
                fps_shot_info.preset = b3settings::preset_name(b3settings::matching_preset(b3settings::Settings{}));
                fps_shot_info.msaa = dev->msaa();
            }
            if (idx >= 0) if (const auto& hs = ls.feed.at(std::size_t(idx)).host_settings) {
                const b3settings::Settings ps = b3settings::from_ini(hs->text);
                apply_view(ls.view, ps);
                spec = o.specular && ps.specular;
                if (ps.fps_view != 0) fps_shot = fps_view_from_setting(ps.fps_view);
                fps_shot_corner = fps_corner_from_setting(ps.fps_corner);
                fps_shot_info.preset = b3settings::preset_name(b3settings::matching_preset(ps));
                fps_shot_info.msaa = dev->msaa();
                fps_shot_info.target_fps = ps.frame_cap > 0 ? float(ps.frame_cap) : 60.0f;
                std::printf("[b3settings] shot poll %u: record %u, preset %s\n", poll, hs->serial, b3settings::preset_name(b3settings::matching_preset(ps)));
            }
            Frame f;
            const int w = o.size_set ? o.w : 640, h = o.size_set ? o.h : 448;
            b3gfx::FrameParams fp;
            items.clear();
            bool split_shot = false;   // gsoff1 (§29): `views` / `sfp` / `items` (the 2D) for draw_views_offscreen
            bool photo_shot = false;   // photo mode: the scene from its free camera at this poll (game time; no 2D)
            Camera photo_cam;
            HudProjection hp;
            std::size_t hud_lo = 0, hud_hi = 0;   // textsize1: the race HUD's items (B3_HUD_SIZE_SHOTS below)
            bool hud_variants = false;
            const bool race = idx >= 0 && ls.ready() && std::size_t(idx) >= ls.feed.segment().first;
            if (!race) {   // menu1: a front-end shot (2D only)
                if (idx < 0 || !ls.frontend_frame(double(idx) + double(alpha), f)) {
                    std::printf("[b3live] shot poll %u: not kept, or no 2D frame (index %ld)\n", poll, idx);
                    continue;
                }
                ls.view.frontend_frame(*dev, f, ls.feed.models().heads, ls.feed.models().hud.textures, o.size_set ? float(w) / float(h) : 4.0f / 3.0f, w, h, fp, items);
            } else if (ls.frame(double(idx) + double(alpha), true, f) && f.split) {
                // gsoff1 (§29): a split-screen shot, as b3replay --offscreen draws it (both views' blooms warmed first)
                split_shot = true;
                const float va = o.size_set ? 2.0f * float(w) / float(h) : 0.0f, ha = o.size_set ? float(w) / float(h) : 4.0f / 3.0f;
                if (ls.view.show_bloom && o.bloom_warmup > 0) {
                    ls.view.bloom_last = -1;
                    ls.view.view2_state.bloom_last = -1;
                    std::vector<std::uint8_t> scratch;
                    const std::size_t i = std::size_t(idx), lo = std::max(ls.feed.segment().first, ls.feed.first());
                    for (std::size_t k = i > lo + std::size_t(o.bloom_warmup) ? i - std::size_t(o.bloom_warmup) : lo; k < i; ++k) {
                        Frame wf;
                        if (!ls.frame(double(k), true, wf) || !wf.split || !wf.cam.valid) continue;
                        ls.view.split_frame(*dev, wf, ls.feed.models().heads, ls.feed.models().hud.textures, true, o.fog, spec, w, h, va, ha,
                                            views, sfp, items);
                        dev->draw_views_offscreen(w, h, views, sfp, items, scratch);
                    }
                }
                ls.view.split_frame(*dev, f, ls.feed.models().heads, ls.feed.models().hud.textures, true, o.fog, spec, w, h, va, ha,
                                    views, sfp, items);
            } else if (o.native_pad && f.cam.valid && PhotoMode::instance().camera_at(poll, f.cam, photo_cam)) {
                photo_shot = true;   // (a free view's bloom is its own frame's: no warm-up)
                ls.photo_frame(*dev, f, photo_cam, o.size_set ? float(w) / float(h) : f.cam.half_w / f.cam.half_h, o.fog, spec, fp, items,
                               o.size_set ? float(w) / float(h) : 4.0f / 3.0f);
                PhotoMode::instance().dof_at(poll, photo_cam, ls.player_car(f), fp.view_proj, fp.dof);   // photo2: off unless turned on
                if (fp.dof.on)
                    std::printf("[b3photo] shot poll %u: depth of field, blur radius %.3f of the height, autofocus at (%.3f, %.3f) x %.2f\n", poll,
                                double(fp.dof.radius), double(fp.dof.point[0]), double(fp.dof.point[1]), double(fp.dof.focus_scale));
                std::printf("[b3photo] shot poll %u: the free camera at (%.2f, %.2f, %.2f), field of view %.1f degrees\n", poll,
                            double(photo_cam.m.r[3][0]), double(photo_cam.m.r[3][1]), double(photo_cam.m.r[3][2]),
                            2.0 * std::atan(double(photo_cam.half_h)) * 180.0 / 3.14159265358979);
            } else {
            if (ls.view.show_bloom && o.bloom_warmup > 0) {
                // inproc1: the bloom's kept picture warmed over the ticks before, as b3replay --offscreen does it (post1,
                // --bloom-warmup), so a shot compares like for like with the offline frame
                ls.view.bloom_last = -1;
                std::vector<std::uint8_t> scratch;
                const std::size_t i = std::size_t(idx), lo = std::max(ls.feed.segment().first, ls.feed.first());
                for (std::size_t k = i > lo + std::size_t(o.bloom_warmup) ? i - std::size_t(o.bloom_warmup) : lo; k < i; ++k) {
                    Frame wf;
                    if (!ls.frame(double(k), true, wf) || !wf.cam.valid) continue;
                    b3gfx::FrameParams wp;
                    ls.params(wf, o.size_set ? float(w) / float(h) : wf.cam.half_w / wf.cam.half_h, o.fog, spec, wp,
                              o.size_set ? float(w) / float(h) : 4.0f / 3.0f);
                    std::vector<b3gfx::DrawItem> witems;
                    ls.view.render_aspect = o.size_set ? float(w) / float(h) : 0.0f;
                    ls.view.draw_list(*dev, wf, ls.feed.models().heads, true, wp.eye, witems);
                    dev->draw_offscreen(w, h, wp, witems, scratch);
                }
            }
            ls.frame(double(idx) + double(alpha), true, f);
            ls.params(f, o.size_set ? float(w) / float(h) : f.cam.half_w / f.cam.half_h, o.fog, spec, fp,
                      o.size_set ? float(w) / float(h) : 4.0f / 3.0f);
            ls.view.render_aspect = o.size_set ? float(w) / float(h) : 0.0f;
            ls.view.draw_list(*dev, f, ls.feed.models().heads, true, fp.eye, items);
            std::memcpy(hp.view_proj, fp.view_proj, sizeof hp.view_proj);
            hp.vehicles = &f.vehicles;
            hud_lo = items.size();
            ls.view.hud_list(*dev, f.hud, ls.feed.models().hud.textures, o.size_set ? float(w) / float(h) : 4.0f / 3.0f, items, h, w, &hp);
            hud_hi = items.size();
            hud_variants = true;
            }
            // sdk2b: the code mods' 2D of the shot's tick, after the game's HUD (not over photo mode's free view)
            if (idx >= 0 && !photo_shot)
                if (const auto& mo = ls.feed.at(std::size_t(idx)).mod_overlay) {
                    const std::size_t n = ModOverlay::instance().draw_list(*dev, w, h, *mo, items);
                    std::printf("[b3live] shot poll %u: mod overlay drawn (%zu of %zu items, record %u)\n", poll, n, mo->items.size(), mo->serial);
                }
            if (pc_page.active()) {   // mac5: the page as it is now (a shot is taken a few polls after its own)
                pc_page.set_clock(float(poll) / 60.0f);   // the focused row's pulse at the shot's poll, not at host time
                ls.view.host_ui_hud_textures = &ls.feed.models().hud.textures;
                ls.view.host_ui_list(*dev, pc_page.ui(ls.feed.at(ls.feed.last()).host_fonts, ls.feed.models().hud.textures),
                                     o.size_set ? float(w) / float(h) : 4.0f / 3.0f, items, h, w);
                std::printf("[b3live] shot poll %u: PC SETTINGS page drawn\n", poll);
            }
            // mac5: the video skip hint at the shot's tick (its opacity is the game's, in game time)
            if (idx >= 0 && !photo_shot && ls.feed.at(std::size_t(idx)).skip_hint && pc_page.settings().skip_hint) {
                const float op = float(ls.feed.at(std::size_t(idx)).skip_hint) / 255.0f;
                skip_hint.draw_list(*dev, w, h, op, ls.view.prompt_device, items);
                std::printf("[b3live] shot poll %u: skip hint drawn (opacity %.2f, %s)\n", poll, double(op),
                            prompt_device_name(ls.view.prompt_device));
            }
            // speedrun1: the speedrun timer of the shot's tick (its numbers the game's, the marks up to its poll)
            if (race && !photo_shot && !split_shot && !pc_page.active()) {
                const std::vector<speedrun::Row> rows = ls.speedrun_rows(pc_page.settings().speedrun_timer, std::size_t(idx));
                if (!rows.empty()) {
                    speedrun_panel.draw_list(*dev, w, h, rows, items);
                    std::string text;
                    for (const speedrun::Row& r : rows) text += (text.empty() ? "" : " | ") + r.label + " " + r.value;
                    // (the exact values, and the tick before's text: evidence against the game's own HUD lap timer in the shot)
                    const Tick& tk = ls.feed.at(std::size_t(idx));
                    const Tick* pv = std::size_t(idx) > ls.feed.first() ? &ls.feed.at(std::size_t(idx) - 1) : nullptr;
                    std::printf("[b3live] shot poll %u: speedrun timer drawn: %s (race time %.9g, lap time %.9g = %08X; ticks per frame %d; the "
                                "tick before: race %s, lap %.9g = %s)\n", poll, text.c_str(), double(tk.score.time), double(tk.score.lap_time),
                                ls.b3_f32_bits(tk.score.lap_time), tk.ticks_per_frame, pv ? speedrun::time_text(pv->score.time).c_str() : "-",
                                pv ? double(pv->score.lap_time) : 0.0, pv ? speedrun::time_text(pv->score.lap_time).c_str() : "-");
                }
            }
            // gsoff1: the FPS panel when the setting (or --fps-counter) asks for it (an offscreen shot counts no frames: 0)
            if (!photo_shot && fps_shot != FpsView::kOff) {
                FpsPanel none;
                fps_shot_info.width = w;
                fps_shot_info.height = h;
                none.draw_list(*dev, w, h, b3live::now_ns(), fps_shot, fps_shot_corner, ls.view.hud_size, fps_shot_info, nullptr, race, items);
                std::printf("[b3live] shot poll %u: FPS counter drawn (%s)\n", poll, fps_shot == FpsView::kMinimal ? "minimal" : fps_shot == FpsView::kStandard ? "standard" : "detailed");
            }
            if (!photo_shot && hd_notice.visible() && poll >= hd_notice_poll) {   // hdkey: the key's notice, aged to this poll
                const float age = float(poll - hd_notice_poll) / 60.0f;
                hd_notice.draw_list(*dev, w, h, std::max(0.0f, age - hd_notice_age), items);
                hd_notice_age = std::max(hd_notice_age, age);
                if (hd_notice.visible()) std::printf("[b3live] shot poll %u: HD TEXTURES notice drawn (%.2f s after the key)\n", poll, double(age));
            }
            char path[512];
            std::snprintf(path, sizeof path, "%s/live_%u_%g.png", o.out_dir.c_str(), poll, double(alpha));
            if (!(split_shot ? dev->draw_views_offscreen(w, h, views, sfp, items, rgba) : dev->draw_offscreen(w, h, fp, items, rgba)) ||
                !b3asset::write_png_rgba(path, w, h, rgba.data())) {
                std::printf("[b3live] shot poll %u: draw or write failed\n", poll);
                continue;
            }
            ++shots_written;
            std::printf("[b3live] shot poll %u (+%.2f): tick %ld, %s, route %s, %zu draws, %zu vehicles -> %s\n", poll, double(alpha), idx,
                        race ? (split_shot ? "race, split screen" : photo_shot ? "race (photo mode)" : "race") : "front end", ls.view.route.c_str(),
                        items.size(), f.vehicles.size(), path);
            // tunenav1: the F9 overlay as a script left it (live_<poll>_<alpha>_tune.png), and B3TUNE_OVERLAY_SHOTS' views of
            // it (live_<poll>_<alpha>_tune<k>.png), each over this shot's picture; after the shot, which stays without it, and
            // each with the bloom's kept picture started again (as the HUD size variants below: a draw carries into the next)
            if (!photo_shot && !pc_page.active()) {
                TuneConsole& tc = TuneConsole::instance();
                auto tune_shot = [&](const std::string& what, const char* suffix) {
                    std::vector<b3gfx::DrawItem> titems = items;
                    tc.draw_list(*dev, w, h, titems);
                    b3gfx::FrameParams tfp = fp;
                    if (tfp.bloom.on) { tfp.bloom.restart = true; tfp.bloom.renders = 1.0f; }
                    char tp[512];
                    std::snprintf(tp, sizeof tp, "%s/live_%u_%g_tune%s.png", o.out_dir.c_str(), poll, double(alpha), suffix);
                    if ((split_shot ? dev->draw_views_offscreen(w, h, views, sfp, titems, rgba) : dev->draw_offscreen(w, h, tfp, titems, rgba)) &&
                        b3asset::write_png_rgba(tp, w, h, rgba.data()))
                        std::printf("[b3live] shot poll %u: tuning overlay %s -> %s\n", poll, what.c_str(), tp);
                };
                if (const char* cl = std::getenv("B3_CARLOOK_PANEL_SHOT")) {   // reflect2: the CAR LOOK panel over this shot (docs/car-look.md)
                    CarLookPanel& cp = CarLookPanel::instance();
                    cp.set_enabled(true);
                    cp.open_now(true);
                    for (int k = 0; k < std::atoi(cl); ++k) cp.move(1);
                    std::vector<b3gfx::DrawItem> citems = items;
                    cp.draw_list(*dev, w, h, citems);
                    b3gfx::FrameParams cfp = fp;
                    if (cfp.bloom.on) { cfp.bloom.restart = true; cfp.bloom.renders = 1.0f; }
                    char cpth[512];
                    std::snprintf(cpth, sizeof cpth, "%s/live_%u_%g_carlook.png", o.out_dir.c_str(), poll, double(alpha));
                    if (dev->draw_offscreen(w, h, cfp, citems, rgba) && b3asset::write_png_rgba(cpth, w, h, rgba.data()))
                        std::printf("[b3live] shot poll %u: CAR LOOK panel -> %s\n", poll, cpth);
                    cp.open_now(false);
                    cp.set_enabled(false);
                }
                if (tc.open()) tune_shot("(as the script left it)", "");
                static const char* tune_shots = std::getenv("B3TUNE_OVERLAY_SHOTS");
                if (tune_shots && *tune_shots && tc.available()) {
                    const std::string list = tune_shots;
                    int k = 0;
                    for (std::size_t at = 0; at <= list.size(); ++k) {
                        const std::size_t comma = list.find(',', at);
                        const std::string st = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                        at = comma == std::string::npos ? list.size() + 1 : comma + 1;
                        if (tc.shot_state(st)) tune_shot(st, std::to_string(k).c_str());
                    }
                    tc.end_shots();
                }
            }
            // textsize1 (diagnostics): B3_HUD_SIZE_SHOTS=<level,...> (1 115 %, 2 130 %, 3 150 %) -- the same race shot again
            // with the HUD at those sizes (live_<poll>_<alpha>_hud<percent>.png; only the HUD's items are built again). The
            // bloom's kept picture is started again for each (it would carry the draw before: the 3D of two variants then
            // differs at its bright spots), so the variants' pictures differ only where the HUD does; level 0 gives the
            // 100 % picture on that footing (hud100) to compare against
            static const char* size_shots = std::getenv("B3_HUD_SIZE_SHOTS");
            if (size_shots && hud_variants && hud_hi <= items.size()) {
                const int keep = ls.view.hud_size;
                b3gfx::FrameParams vfp = fp;
                if (vfp.bloom.on) { vfp.bloom.restart = true; vfp.bloom.renders = 1.0f; }
                for (const char* p = size_shots; *p;) {
                    char* end = nullptr;
                    const long lvl = std::strtol(p, &end, 10);
                    if (end == p) break;
                    p = *end == ',' ? end + 1 : end;
                    if (lvl < 0 || lvl >= a11y::kHudSizeLevels) continue;
                    std::vector<b3gfx::DrawItem> vitems(items.begin(), items.begin() + std::ptrdiff_t(hud_lo));
                    ls.view.hud_size = int(lvl);
                    ls.view.hud_list(*dev, f.hud, ls.feed.models().hud.textures, o.size_set ? float(w) / float(h) : 4.0f / 3.0f, vitems, h, w, &hp);
                    vitems.insert(vitems.end(), items.begin() + std::ptrdiff_t(hud_hi), items.end());
                    std::snprintf(path, sizeof path, "%s/live_%u_%g_hud%d.png", o.out_dir.c_str(), poll, double(alpha), a11y::hud_size_percent(int(lvl)));
                    if (dev->draw_offscreen(w, h, vfp, vitems, rgba) && b3asset::write_png_rgba(path, w, h, rgba.data()))
                        std::printf("[b3live] shot poll %u: HUD SIZE %d%% -> %s\n", poll, a11y::hud_size_percent(int(lvl)), path);
                }
                ls.view.hud_size = keep;
            }
            std::fflush(stdout);
        }
        if (ended) break;
        // bench: one display frame as the window draws it (game camera, interpolated), offscreen with read-back
        if (o.bench_w > 0 && !ls.ready() && ls.feed.state() == LiveFeed::State::kSynced && !ls.feed.empty()) {
            // menu1: a front-end frame as the window draws it
            const std::uint64_t cnow = b3live::now_ns();
            double x = display_x(clock, ls.feed, o, o.delay, cnow);
            if (const long hold = ls.hold_before_race(); hold >= 0) x = std::min(x, double(hold));   // loadlag1
            Frame f;
            if (ls.frontend_frame(x, f)) {
                race_log.frame(ls, false, 0);
                b3gfx::FrameParams fp;
                items.clear();
                ls.view.frontend_frame(*dev, f, ls.feed.models().heads, ls.feed.models().hud.textures, float(o.bench_w) / float(o.bench_h), o.bench_w, o.bench_h, fp, items);
                const std::uint64_t f1 = b3live::now_ns();
                if (!dev->draw_offscreen(o.bench_w, o.bench_h, fp, items, rgba)) ++bench_failed;
                const std::uint64_t f2 = b3live::now_ns();
                lat_log.frame(ls.feed, f, clock, f0, cnow, f2);
                fe_build.push_back(double(f1 - f0) / 1e6);
                fe_draw.push_back(double(f2 - f1) / 1e6);
                fe_frame.push_back(double(f2 - f0) / 1e6);
            } else SDL_Delay(1);
        } else if (o.bench_w > 0 && ls.ready()) {
            const std::uint64_t cnow = b3live::now_ns();
            const double x = display_x(clock, ls.feed, o, o.delay, cnow);
            {
                ClockStats& c = cstat[clock.rate < 45.0 ? 1 : 0];
                ++c.frames;
                c.starved += clock.starved;
                c.lag_ms.push_back(clock.age_ms);
                if (prev_seg == ls.feed.segment().id && prev_ns && cnow > prev_ns) {
                    if (x < prev_x) ++c.backwards;
                    c.speed.push_back((x - prev_x) / (double(cnow - prev_ns) * 1e-9 * clock.rate));
                }
                prev_x = x; prev_ns = cnow; prev_seg = ls.feed.segment().id;
            }
            Frame f;
            b3perf::Scope drawlist_scope(b3perf::kDrawList);   // perf3 (the bench's frames: B3_PERF_LOG)
            {
                b3perf::Scope interp_scope(b3perf::kDlInterp);
                ls.frame(x, lat_mode != Latency::kNewest, f);
                blend.apply(ls, x, lat_mode != Latency::kNewest, f, cnow);   // perf1
                race_log.frame(ls, true, ls.feed.at(f.tick).poll);
            }
            b3gfx::FrameParams fp;
            items.clear();
            const float ba = float(o.bench_w) / float(o.bench_h);
            if (f.split) {   // gsoff1 (§29)
                ls.view.split_frame(*dev, f, ls.feed.models().heads, ls.feed.models().hud.textures, false, o.fog, o.specular, o.bench_w, o.bench_h,
                                    2.0f * ba, ba, views, sfp, items);
            } else {
            ls.params(f, ba, o.fog, o.specular, fp);
            ls.view.render_aspect = ba;
            {
                b3perf::Scope world_scope(b3perf::kDlWorld);
                ls.view.draw_list(*dev, f, ls.feed.models().heads, false, fp.eye, items);
            }
            HudProjection hp;
            std::memcpy(hp.view_proj, fp.view_proj, sizeof hp.view_proj);
            hp.vehicles = &f.vehicles;
            b3perf::Scope hud_scope(b3perf::kDlHud);
            ls.view.hud_list(*dev, f.hud, ls.feed.models().hud.textures, ba, items, o.bench_h, o.bench_w, &hp);
            hud_scope.end();
            }
            drawlist_scope.end();
            const std::uint64_t f1 = b3live::now_ns();
            if (!(f.split ? dev->draw_views_offscreen(o.bench_w, o.bench_h, views, sfp, items, rgba) : dev->draw_offscreen(o.bench_w, o.bench_h, fp, items, rgba))) ++bench_failed;
            const std::uint64_t f2 = b3live::now_ns();
            lat_log.frame(ls.feed, f, clock, f0, cnow, f2);
            if (const double a = clock.latch_age_ms(ls.feed, double(std::min(f.tick, ls.feed.last())) + double(f.alpha), f2); a >= 0) sched_age.push_back(a);
            t_build.push_back(double(f1 - f0) / 1e6);
            t_draw.push_back(double(f2 - f1) / 1e6);
            t_frame.push_back(double(f2 - f0) / 1e6);
            ++bench_frames;
            b3perf::frame_end(o.bench_w, o.bench_h);
        } else {
            SDL_Delay(1);
        }
    }
    const auto& f = ls.feed;
    std::printf("[b3live] feed %s ended: %llu records (%llu keyframes), %llu ticks, %llu resyncs; %d track loads (%.0f ms total); %d of %zu shots\n",
                o.name.c_str(), static_cast<unsigned long long>(f.records), static_cast<unsigned long long>(f.keyframes),
                static_cast<unsigned long long>(f.ticks_total), static_cast<unsigned long long>(f.resyncs), ls.loads, ls.load_ms_total,
                shots_written, o.shots.size());
    if (!o.verify.empty())
        std::printf("[b3live] verify against %s: %llu ticks identical, %llu differ, %llu polls not in the file\n", o.verify.c_str(),
                    static_cast<unsigned long long>(f.verified), static_cast<unsigned long long>(f.verify_differ),
                    static_cast<unsigned long long>(f.verify_missing));
    if (!fe_frame.empty()) {
        double total = 0;
        for (double v : fe_frame) total += v;
        std::printf("[b3live] bench %dx%d front end: %zu frames, %.1f fps; ms p50/p95/p99/max: frame %.2f/%.2f/%.2f/%.2f, CPU %.2f/%.2f/%.2f/%.2f,"
                    " GPU draw + read-back %.2f/%.2f/%.2f/%.2f\n", o.bench_w, o.bench_h, fe_frame.size(), 1000.0 * double(fe_frame.size()) / total,
                    pct(fe_frame, 50), pct(fe_frame, 95), pct(fe_frame, 99), pct(fe_frame, 100), pct(fe_build, 50), pct(fe_build, 95),
                    pct(fe_build, 99), pct(fe_build, 100), pct(fe_draw, 50), pct(fe_draw, 95), pct(fe_draw, 99), pct(fe_draw, 100));
    }
    if (bench_frames > 0) {
        double total = 0;
        for (double v : t_frame) total += v;
        std::printf("[b3live] bench %dx%d: %llu frames (%llu failed), %.1f fps while racing; ms p50/p95/p99/max: frame %.2f/%.2f/%.2f/%.2f,"
                    " CPU (drain + frame + draw list) %.2f/%.2f/%.2f/%.2f, GPU draw + read-back %.2f/%.2f/%.2f/%.2f\n",
                    o.bench_w, o.bench_h, static_cast<unsigned long long>(bench_frames), static_cast<unsigned long long>(bench_failed),
                    1000.0 * double(bench_frames) / total, pct(t_frame, 50), pct(t_frame, 95), pct(t_frame, 99), pct(t_frame, 100),
                    pct(t_build, 50), pct(t_build, 95), pct(t_build, 99), pct(t_build, 100), pct(t_draw, 50), pct(t_draw, 95),
                    pct(t_draw, 99), pct(t_draw, 100));
        for (int k = 0; k < 2; ++k) {
            ClockStats& c = cstat[k];
            if (!c.frames) continue;
            std::printf("[b3live] clock %s ticks (%s rule): %llu frames, delay %.1f ticks; age of the shown state ms p1/p50/p99/max %.1f/%.1f/%.1f/%.1f;"
                        " at the newest tick %llu (%.2f %%), steps backwards %llu; speed / tick rate p0.1/p1/p50/p99/p99.9 %.2f/%.2f/%.3f/%.2f/%.2f\n",
                        k ? "30 Hz (crash)" : "60 Hz (race)", o.clock_old ? "db55faa" : "live2 groups", static_cast<unsigned long long>(c.frames), o.delay, pct(c.lag_ms, 1),
                        pct(c.lag_ms, 50), pct(c.lag_ms, 99), pct(c.lag_ms, 100), static_cast<unsigned long long>(c.starved),
                        100.0 * double(c.starved) / double(c.frames), static_cast<unsigned long long>(c.backwards), pct(c.speed, 0.1),
                        pct(c.speed, 1), pct(c.speed, 50), pct(c.speed, 99), pct(c.speed, 99.9));
        }
        if (!sched_age.empty())   // lat1
            std::printf("[b3live] latency %s: latch -> frame done ms p5/p50/p95/p99 %.1f/%.1f/%.1f/%.1f, schedule margin %.1f ms\n", o.latency.c_str(),
                        pct(sched_age, 5), pct(sched_age, 50), pct(sched_age, 95), pct(sched_age, 99), clock.margin_ms);
    }
    WindowStats::print_decode(ls.feed);   // perf
    lat_log.write(o, "offscreen", 0);
    std::fflush(stdout);
    dev.reset();
    if (hidden) SDL_DestroyWindow(hidden);
    SDL_Quit();
    return shots_written == int(o.shots.size()) && f.verify_differ == 0 ? 0 : 1;
}

// Start-up stages of the live window, logged with a watchdog that reports a stage taking over 5 s (first windowed
// test, 2026-11-04: the window stopped responding before its first log line).
struct StageLog {
    std::atomic<const char*> stage{"start"};
    std::atomic<std::uint64_t> since{0};
    std::atomic<bool> done{false};
    std::thread watch;
    StageLog() {
        since = b3live::now_ns();
        watch = std::thread([this] {
            std::uint64_t reported = 0;
            while (!done) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                const std::uint64_t at = since, now = b3live::now_ns();
                if (!done && now - at > 5000000000ull && now - reported > 5000000000ull) {
                    std::printf("[b3live] watchdog: still in stage '%s' after %.1f s\n", stage.load(), double(now - at) * 1e-9);
                    std::fflush(stdout);
                    reported = now;
                }
            }
        });
    }
    ~StageLog() { finish(); }
    void enter(const char* s) {
        std::printf("[b3live] stage: %s\n", s);
        std::fflush(stdout);
        stage = s;
        since = b3live::now_ns();
    }
    void finish() {
        done = true;
        if (watch.joinable()) watch.join();
    }
};

// nofallback1: the game's stop (live.h set_game_stopped)
namespace {
std::mutex g_stop_m;
std::string g_stop_function;
std::atomic<bool> g_stopped{false};
}  // namespace
void set_game_stopped(const std::string& function) {
    std::lock_guard<std::mutex> lk(g_stop_m);
    g_stop_function = function.empty() ? std::string("unknown") : function;
    g_stopped.store(true, std::memory_order_release);
}
std::string game_stopped() {
    if (!g_stopped.load(std::memory_order_acquire)) return {};
    std::lock_guard<std::mutex> lk(g_stop_m);
    return g_stop_function;
}

// effects2: whether the PC runs on battery (BATTERY SAVER). SDL's power state; B3_FORCE_BATTERY=1 / 0 forces it (a test aid:
// the checks run on desktops)
static bool on_battery_power() {
    if (const char* e = std::getenv("B3_FORCE_BATTERY")) return e[0] == '1';
    return SDL_GetPowerInfo(nullptr, nullptr) == SDL_POWERSTATE_ON_BATTERY;
}

int run_live_window(const LiveOptions& o) {
    StageLog sl;
    sl.enter("SDL_Init");
    // The game's window keeps the focus when this one appears (the player plays there; this window only shows).
    // (inproc1: --activate when this is the only window)
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, o.activate ? "1" : "0");
    // the game's identity for the desktop (Wayland app-id, X11 class, the Windows / macOS app name)
    if (o.inproc) SDL_SetAppMetadata("Burnout 3 Native", b3port::version(), "b3port.native");
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    sl.enter("SDL_CreateWindow");
    // test builds: the native game's title carries the build's version (include/b3report/version.h)
    const std::string title_name = o.inproc ? std::string("Burnout 3 (native) ") + b3port::version() : std::string("b3live");
    SDL_Window* win = SDL_CreateWindow((title_name + (o.inproc && o.safe_mode ? " - SAFE MODE" : "")).c_str(), o.w, o.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                                             (o.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    if (o.inproc) {   // the game's window: the built-in B3 icon (taskbar, Dock, Alt-Tab)
        if (SDL_Surface* icon = b3port::app_icon_surface()) {
            SDL_SetWindowIcon(win, icon);
            SDL_DestroySurface(icon);
        }
    }
    sl.enter("GPU device and swapchain");
    b3gfx::Options opt;
    opt.msaa = o.msaa;
    opt.dither = o.dither;   // perf1
    if (o.settings) { filter_options(o.settings->filter, opt); scale_options(*o.settings, opt); }   // settings1
    if (o.settings && o.settings->dither) opt.dither = 1.0f;   // perf1: DITHER
    opt.driver = o.driver;
    opt.present = o.vsync ? b3gfx::PresentMode::kVsync : b3gfx::PresentMode::kUncapped;
    const Latency lat_mode = latency_mode(o);
    // lat1: the low-latency modes keep the GPU's frame queue short and wait for the swapchain before sampling the clock
    if (lat_mode != Latency::kSmooth) opt.frames_in_flight = o.frames_in_flight;
    std::string err;
    auto dev = b3gfx::Device::create(win, opt, &err);
    if (!dev) { std::fprintf(stderr, "b3gfx: %s\n", err.c_str()); return 1; }
    std::printf("[b3live] window: feed %s, driver %s, MSAA %dx, present %s, %d frames in flight, latency %s\n", o.name.c_str(), dev->driver_name(),
                dev->msaa(), dev->present_mode_name(), dev->frames_in_flight(), o.latency.c_str());
    // test builds: the system in the run log (bug reports: the same lines as their system-info.txt)
    const std::string gpu_desc = b3port::gpu_description(static_cast<SDL_GPUDevice*>(dev->sdl_gpu_device()));
    if (o.native_pad) {
        const std::string info = b3port::system_info(gpu_desc, false);
        for (std::size_t a = 0, b; a < info.size(); a = b + 1) {
            b = info.find('\n', a);
            if (b == std::string::npos) b = info.size();
            std::printf("[b3live] system %s\n", info.substr(a, b - a).c_str());
        }
    }
    std::fflush(stdout);
    sl.enter("feed and car scan");
    LiveScene ls(o);
    ls.async_routes = true;   // loadlag1: the route decoded on a worker, the window drawing meanwhile
    DisplayClock clock;
    clock.old_rule = o.clock_old;
    MotionBlend blend;   // perf1: low mode's correction blend
    blend.ms = lat_mode == Latency::kLow && o.lead > 0 ? o.blend_ms : 0.0;
    ls.view.effects.async_sparks = true;   // perf1: a crash's spark build on a worker, not in the frame
    bool running = true, interpolate = true, game_cull = false, fog = o.fog, specular = o.specular;
    // settings1: our PC settings (--settings at start, then each HSET record the game publishes when they change)
    double max_fps = o.max_fps;
    bool skip_hint_setting = true;
    // fpsoverlay2: the FPS COUNTER's view (the setting, or --fps-counter; the shortcut cycles it for the session) and its corner
    FpsView fps_view = o.fps_counter ? FpsView::kStandard : FpsView::kOff;
    FpsCorner fps_corner = FpsCorner::kTopLeft;
    FpsInfo fps_info;
    {
        const std::size_t paren = gpu_desc.find(" (");
        fps_info.gpu = gpu_desc.substr(0, paren);
        const std::string drv = dev->driver_name() ? dev->driver_name() : "";
        fps_info.backend = drv == "vulkan" ? "VULKAN" : drv == "direct3d12" ? "D3D12" : drv == "metal" ? "METAL" : drv;
        for (char& ch : fps_info.backend) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        for (char& ch : fps_info.gpu) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (gpu_desc.empty() || fps_info.gpu == "UNKNOWN GPU") fps_info.gpu.clear();
    }
    int prompts_setting = 0;   // mac5: SETTINGS > BUTTON PROMPTS
    std::uint32_t settings_serial = 0;
    // effects2: BATTERY SAVER (opt-in): while SDL reports the PC on battery the settings in effect are b3settings::on_battery's
    // copy (60 fps, sparks and debris at most Medium); the power state is read every couple of seconds
    b3settings::Settings cur_settings;
    bool battery_now = false;
    std::uint64_t battery_checked_ns = 0;
    auto apply_settings = [&](const b3settings::Settings& stored) {
        cur_settings = stored;
        battery_now = stored.battery_saver && on_battery_power();
        const b3settings::Settings s = battery_now ? b3settings::on_battery(stored) : stored;
        if (battery_now) std::printf("[b3settings] on battery: BATTERY SAVER holds the frame rate to 60 and the effects to Medium at most\n");
        apply_view(ls.view, s);
        NativePad::instance().set_hold_unfocused(s.pause_unfocused);   // mac5: native_play's focus hold
        NativePad::instance().set_boost_toggle(s.boost_mode == 1);    // accessibility: BOOST Toggle (opt-in, input-changing)
        NativePad::instance().set_instant_restart(s.instant_restart == 1);   // speedrun1: INSTANT RESTART (opt-in, input-injecting)
        NativePad::instance().set_series_shoulders(s.series_shoulders == 1);   // carsel1: CAR SERIES ON L1 / R1 (opt-in, input-changing)
        NativePad::instance().set_rumble(s.rumble == 1, s.rumble_strength, s.rumble_triggers);   // rumble1 (opt-in, game-visible)
        NativePad::instance().set_analog_pedals(s.analog_pedals);   // 1.0.1
        NativePad::instance().set_adaptive_triggers(s.adaptive_triggers == 1, s.adaptive_triggers_strength);   // pad1 (presentation only)
        interpolate = s.interpolation;
        specular = o.specular && s.specular;
        max_fps = s.frame_cap > 0 ? double(s.frame_cap) : o.max_fps;
        fps_view = fps_view_from_setting(s.fps_view);
        if (o.fps_counter && fps_view == FpsView::kOff) fps_view = FpsView::kStandard;
        fps_corner = fps_corner_from_setting(s.fps_corner);
        fps_info.preset = b3settings::preset_name(b3settings::matching_preset(stored));   // what the user chose, not the battery copy
        skip_hint_setting = s.skip_hint;   // mac5
        prompts_setting = s.button_prompts;
        dev->set_dither(o.dither > 0.0f || s.dither ? 1.0f : 0.0f);   // perf1: DITHER
        dev->set_render_scale(float(std::clamp(s.render_scale, 50, 100)) / 100.0f);   // effects2: RENDER SCALE
        dev->set_sharp_upscale(s.upscaling != 0);   // 1.1 UPSCALING
        const bool full = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
        if (full != (s.fullscreen || o.fullscreen)) SDL_SetWindowFullscreen(win, s.fullscreen || o.fullscreen);
    };
    if (o.settings) apply_settings(*o.settings);
    // mac5: the PC SETTINGS page (pcpage.h) starts from the settings loaded at start and saves to the same file
    PcPage& pc_page = PcPage::instance();
    if (o.settings) pc_page.settings() = *o.settings;
    std::uint64_t pc_page_ns = 0;
    // tunenav1: the tuning console's favourites and recently changed values, kept beside the settings
    TuneConsole::instance().set_user_dir(o.settings_path.empty() ? std::string() : std::filesystem::path(o.settings_path).parent_path().string());
    carlook::load_user(o.settings_path.empty() ? std::string() : std::filesystem::path(o.settings_path).parent_path().string());   // reflect2
    int loop_stage = 0;   // the first loop's steps are logged too
    sl.enter("first events");
    double delay = o.delay;
    int w = o.w, h = o.h, frames = 0;
    std::uint64_t title_at = b3live::now_ns(), ended_at = 0;
    std::vector<b3gfx::DrawItem> items;
    std::vector<b3gfx::Device::ViewPass> views;   // gsoff1 (§29): split-screen frames
    b3gfx::FrameParams sfp;
    bool shot_pending = false;   // mac5: F12 -- the next frame also drawn offscreen and saved as a PNG (screenshots/)
    // test builds: F10 (the native game) -- a bug report zip (include/b3report/bug_report.h) on the Desktop (else
    // Documents, else the user data folder), made on a thread; the banner says where
    bool report_pending = false;
    std::future<b3port::BugReportResult> report_job;
    // (a check: $B3_REPORT_AFTER=<s> presses F10 once, that many seconds after the window opened)
    double report_after = [] { const char* e = std::getenv("B3_REPORT_AFTER"); return e && *e ? std::atof(e) : -1.0; }();
    const std::uint64_t report_t0 = b3live::now_ns();
    std::vector<std::thread> shot_writers;
    // play1: with --native-pad the window is the native game's pad (nativepad.h): keys and gamepads go to the game,
    // the viewer's keys need Ctrl, and closing the window asks the game to end (native_play), the window going once
    // the feed has ended (or after 15 s).
    NativePad& npad = NativePad::instance();
    if (o.native_pad) npad.enable();
    if (o.native_pad) pc_page.controls_start(o.controls_path);   // remap1: the bindings (controls.ini; none: the defaults)
    if (o.native_audio) NativeAudio::instance().open();   // play1: the native game's sound (this thread: SDL's)
    std::uint64_t close_deadline = 0;
    bool cursor_shown = true;   // mac5: the system cursor (hidden until the mouse moves: menumouse.h)
    // photo mode (photomode.h; native_play only): the mouse in relative mode while it is on; the camera's frame time
    PhotoMode& photo = PhotoMode::instance();
    bool photo_was = false;
    std::uint64_t photo_ns = 0;
    FpsPanel fps_panel;   // gsoff1: the FPS COUNTER setting or --fps-counter
    SkipHint skip_hint;   // mac5: the video skip hint (native_play)
    SpeedrunPanel speedrun_panel;   // speedrun1: the speedrun timer
    Notice notice;        // mac5: safe mode's banner
    std::uint64_t notice_ns = 0;
    if (o.safe_mode) {
        std::vector<std::string> lines = o.safe_forced ? std::vector<std::string>{"SAFE MODE", "STARTED WITH LOW SETTINGS AS ASKED",
                                                                                  "CHANGE THEM IN SETTINGS AT ANY TIME"}
                                                       : std::vector<std::string>{"SAFE MODE", "THE LAST TWO STARTS DID NOT CLOSE CLEANLY",
                                                                                  "THIS RUN USES LOW SETTINGS - YOURS ARE KEPT", "CHANGE THEM IN SETTINGS"};
        if (o.mods_safe_off > 0)   // modkit1b: and without mods (native_play)
            lines.push_back(o.mods_safe_off == 1 ? "YOUR MOD IS OFF FOR THIS RUN" : "YOUR " + std::to_string(o.mods_safe_off) + " MODS ARE OFF FOR THIS RUN");
        notice.show(lines, 12.0f);
    }
    RaceStartLog race_log;
    FpsView fps_logged = FpsView::kOff;
    LatencyLog lat_log;   // lat1
    lat_log.path = o.latency_log;
    double lat_ema = -1;  // lat1: input -> screen estimate for the title and the FPS panel (ms)
    WindowStats wstats;   // perf
    if (b3perf::init_from_env()) {   // perf3: B3_PERF_LOG
        char inf[256];
        std::snprintf(inf, sizeof inf, "driver=%s msaa=%d present=%s frames_in_flight=%d gpu=\"%s\"", dev->driver_name(), dev->msaa(), dev->present_mode_name(),
                      dev->frames_in_flight(), gpu_desc.c_str());
        b3perf::set_info(inf);
    }
    while (running) {
        const std::uint64_t top_ns = b3live::now_ns();
        if (lat_mode != Latency::kSmooth && !close_deadline) dev->wait_swapchain();   // lat1: the frame starts when it can be shown
        const std::uint64_t loop_ns = b3live::now_ns();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {   // rebind2: the window's shortcuts, whatever keys they are on
                using b3native::input::PortAction;
                const std::uint64_t pm = npad.port_mask(std::uint16_t(e.key.scancode), e.key.mod);   // (0 during a rebind)
                if (pm >> int(PortAction::Screenshot) & 1) shot_pending = true;   // mac5
                else if ((e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && (npad.port_mask(std::uint16_t(e.key.scancode), 0) >> int(PortAction::Screenshot) & 1)) {
                    std::printf("[b3live] screenshot key ignored: Ctrl / Cmd is held\n");   // (a report of 'F12 does nothing' shows it)
                    std::fflush(stdout);
                }
                if ((pm >> int(PortAction::BugReport) & 1) && npad.enabled()) report_pending = true;
                if (pm >> int(PortAction::Fullscreen) & 1) SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                if (pm >> int(PortAction::FpsView) & 1) fps_view = fps_view_next(fps_view);   // fpsoverlay2 (logged below)
                if (pm >> int(PortAction::HdTextures) & 1) notice.show(hd_key(*dev, pc_page.settings(), pc_page.ui_language(), "key"), 1.5f);   // hdkey
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) SDL_ResetKeyboard();   // a modifier left held by an OS shortcut (macOS screenshots) is let go
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (npad.enabled()) {   // play1: the native game ends; the window follows its feed
                    if (!close_deadline) {
                        npad.request_quit();
                        close_deadline = b3live::now_ns() + 15000000000ull;
                        SDL_HideWindow(win);
                        std::printf("[b3pad] window closed: the native game is asked to end\n");
                        std::fflush(stdout);
                    }
                }
                else running = false;
            }
            else if (npad.enabled() && photo.handle(e)) {}   // photo mode's keys and mouse (before the game's pad)
            else if (npad.enabled() && !pc_page.active() && CarLookPanel::instance().handle(e)) {}   // reflect2: the CAR LOOK panel
            else if (npad.enabled() && !pc_page.active() && TuneConsole::instance().handle(e)) {}   // tune4: F9, the overlay's keys
            else if (npad.enabled() && !pc_page.active() && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                     (npad.port_mask(std::uint16_t(e.key.scancode), e.key.mod) >> int(b3native::input::PortAction::TuneOverlay) & 1)) {
                // the overlay key with no overlay in this run: say why instead of nothing (the console is installed at the start)
                if (pc_page.settings().developer_mods != 0)
                    notice.show({"THE TUNING OVERLAY STARTS WITH THE GAME", "START THE GAME AGAIN TO USE IT"}, 5.0f);
                else
                    notice.show({"THE TUNING OVERLAY NEEDS DEVELOPER MODS", "SETTINGS / INTERFACE / DEVELOPER - THEN START AGAIN"}, 6.0f);
                std::printf("[b3tune] overlay key: no overlay in this run (DEVELOPER MODS %s at the start)\n",
                            pc_page.settings().developer_mods != 0 ? "turned on since" : "off");
                std::fflush(stdout);
            }
            else if (npad.handle(e)) {}   // play1 (off without --native-pad)
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode k = e.key.key;
                if (k == SDLK_ESCAPE) running = false;
                else if (k == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT))   // Alt + Enter: always fullscreen (the FULLSCREEN key: above)
                    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                else if (k == SDLK_I) interpolate = !interpolate;
                else if (k == SDLK_V) game_cull = !game_cull;
                else if (k == SDLK_G) fog = !fog;
                else if (k == SDLK_F) ls.view.show_effects = !ls.view.show_effects;
                else if (k == SDLK_L) ls.view.show_coronas = !ls.view.show_coronas;
                else if (k == SDLK_O) ls.view.show_particles = !ls.view.show_particles;
                else if (k == SDLK_X) ls.view.show_sun = !ls.view.show_sun;
                else if (k == SDLK_B) ls.view.sun_occlusion = !ls.view.sun_occlusion;
                else if (k == SDLK_N) ls.view.show_blur = !ls.view.show_blur;
                else if (k == SDLK_T) ls.view.show_bloom = !ls.view.show_bloom;
                else if (k == SDLK_Z) { ls.view.post_game = !ls.view.post_game; ls.view.bloom_last = -1; }
                else if (k == SDLK_H) specular = !specular;
                else if (k == SDLK_U) ls.view.show_hud = !ls.view.show_hud;
                else if (k == SDLK_Y) ls.view.hud_wide = !ls.view.hud_wide;
                else if (k == SDLK_J) ls.view.hud_sharp = !ls.view.hud_sharp;
                else if (k == SDLK_K) ls.view.hud_reproject = !ls.view.hud_reproject;
                else if (k == SDLK_M) ls.view.show_movies = !ls.view.show_movies;   // menu1: movie pictures / placeholder
                else if (k == SDLK_R) ls.view.show_room = !ls.view.show_room;      // fe2: the showroom / the car on black
                else if (k == SDLK_LEFTBRACKET) delay = std::max(1.0, delay - 0.5);   // (latency smooth; low: its fallback)
                else if (k == SDLK_RIGHTBRACKET) delay = std::min(30.0, delay + 0.5);
            }
        }
        npad.poll_gamepads();   // play1 (off without --native-pad)
        if (const std::uint64_t pe = npad.take_port_pad()) {   // rebind2: the shortcuts' gamepad bindings
            using b3native::input::PortAction;
            if (pe >> int(PortAction::Screenshot) & 1) shot_pending = true;
            if (pe >> int(PortAction::BugReport) & 1) report_pending = true;
            if (pe >> int(PortAction::Fullscreen) & 1) SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
            if (pe >> int(PortAction::HdTextures) & 1) notice.show(hd_key(*dev, pc_page.settings(), pc_page.ui_language(), "gamepad"), 1.5f);   // hdkey
        }
        for (const std::uint32_t p : npad.take_hd_presses())   // hdkey: a script's key (native_play --keys)
            notice.show(hd_key(*dev, pc_page.settings(), pc_page.ui_language(), "script, poll " + std::to_string(p)), 1.5f);
        // mac5 (remaster-features §3): the prompts follow the device in use (or BUTTON PROMPTS' choice)
        ls.view.prompt_device = prompt_resolve(prompts_setting, npad.device());
        ls.view.prompt_keys = npad.prompt_keys(ls.view.prompt_device);   // the keyboard's caps; remap1: a pad's moved buttons
        pc_page.set_prompts(ls.view.prompt_device, ls.view.prompt_keys);
        if (loop_stage == 0) sl.enter("first feed drain");
        { B3PERF_SCOPE(kFeedDrain); ls.feed.drain(); }
        lat_log.drained(ls.feed, b3live::now_ns());   // lat1
        // the title's Start glyph needs the game's font records (b4e95443 set them only in the offscreen loop above, so
        // a window kept the plain text; seen 2026-11-10)
        if (!ls.feed.empty()) ls.view.hud_fonts = ls.feed.at(ls.feed.last()).host_fonts;
        if (!ls.feed.empty()) {   // settings1: a new HSET record applies at once (the latest tick's: settings are not timed)
            const auto& hs = ls.feed.at(ls.feed.last()).host_settings;
            if (hs && hs->serial != settings_serial) {
                settings_serial = hs->serial;
                std::vector<std::string> warn;
                const b3settings::Settings s = b3settings::from_ini(hs->text, &warn);
                apply_settings(s);
                std::printf("[b3settings] record %u: preset %s, fov %d, msaa %d (now %d)%s\n", hs->serial,
                            b3settings::preset_name(b3settings::matching_preset(s)), s.fov, s.msaa, dev->msaa(), warn.empty() ? "" : " (warnings)");
                std::fflush(stdout);
            }
        }
        if (o.native_pad) {   // mac5: the PC SETTINGS page, on the window's pad (the game sees neutral meanwhile)
            const std::uint64_t t = b3live::now_ns();
            const float dt = pc_page_ns ? float(double(t - pc_page_ns) * 1e-9) : 1.0f / 60.0f;
            pc_page_ns = t;
            const MenuMouse::PagePointer mp = MenuMouse::instance().page_pointer();   // mac5: the pointer on the page
            if (pc_page.frame(npad.last(), dt, o.settings_path, &mp, float(w) / float(h > 0 ? h : 1))) apply_settings(pc_page.settings());
            TuneConsole::instance().set_enabled(pc_page.settings().developer_mods != 0);   // tune4: F9 while DEVELOPER MODS is on
            if (!pc_page.active()) TuneConsole::instance().pad(npad.last());
            CarLookPanel::instance().set_enabled(pc_page.settings().developer_mods != 0);   // reflect2
            if (!pc_page.active()) CarLookPanel::instance().pad(npad.last());
        }
        if (loop_stage == 0) sl.enter("first scene update");
        { B3PERF_SCOPE(kSceneUpdate); ls.update(*dev); }
        scene_epoch().fetch_add(1, std::memory_order_release);   // loadlag1: a pass with the feed drained (fastforward.h)
        ls.probe_traffic();   // traffic-popin (B3_TRAFFIC_PROBE=1)
        ls.ghosts();   // ghost1
        ls.speedrun_ticks(PcPage::instance().settings().speedrun_timer);   // speedrun1
        if (npad.enabled()) {   // pad1: ADAPTIVE TRIGGERS' view of the game (the newest tick; read only; used only when on)
            b3native::pad::TriggerGame tg[2];
            if (!ls.feed.empty() && !close_deadline && !pc_page.active() && !photo.active()) {
                const Tick& t = ls.feed.at(ls.feed.last());
                const bool race = race_tick(t);
                const auto of = [race](const RadialBlurIn& b, bool view) {
                    b3native::pad::TriggerGame g;
                    g.active = race && view;
                    g.boost = b.valid && b.on ? std::clamp(b.amount, 0.0f, 1.0f) : 0.0f;
                    g.crashed = b.valid && b.crashed;
                    return g;
                };
                tg[0] = of(t.blur, true);
                tg[1] = of(t.view2.blur, t.split);   // player 2: the 2-player split screen's second view
            }
            npad.set_trigger_game(0, tg[0]);
            npad.set_trigger_game(1, tg[1]);
        }
        if (loop_stage == 0) sl.enter("first frame draw");
        const std::uint64_t now = b3live::now_ns();
        b3perf::Scope drawlist_scope(b3perf::kDrawList);   // perf3: to the draw_frame call below
        if (ls.feed.state() == LiveFeed::State::kEnded) {
            // nofallback1: a game that stopped on a missing function keeps its window (the message, F10) until it is closed
            const std::string stopped = npad.enabled() ? game_stopped() : std::string();
            if (!ended_at) {
                ended_at = now;
                if (!stopped.empty()) std::printf("[b3live] the game stopped (%s): the window stays until it is closed\n", stopped.c_str());
            }
            else if ((now - ended_at > 3000000000ull && stopped.empty()) || close_deadline) running = false;   // the session closed: the viewer follows
            if (!stopped.empty() && !close_deadline && !notice.visible() && !report_job.valid()) {
                std::string fn = stopped;
                for (char& ch : fn) ch = char(std::toupper(static_cast<unsigned char>(ch)));
                if (fn.size() > 56) fn = fn.substr(0, 53) + "...";
                notice.show({"THE GAME STOPPED", "THIS BUILD HAS NO NATIVE CODE FOR", fn, "THIS IS A BUG IN THE PORT",
                             npad.port_key_text(b3native::input::PortAction::BugReport) + ": MAKE A BUG REPORT", "THEN CLOSE THE WINDOW"}, 1.0e6f);
                notice_ns = 0;
            }
        }
        if (close_deadline && now > close_deadline) {
            std::printf("[b3input] the game has not ended 15 s after the window closed; the window goes\n");
            running = false;
        }
        if (close_deadline) { SDL_Delay(5); continue; }   // hidden: only the feed until the session ends
        b3gfx::FrameParams fp;
        items.clear();
        Frame f;
        bool drawn = false, photo_drawn = false;
        const bool photo_on = npad.enabled() && photo.active();
        if (photo_on != photo_was) {   // photo mode: the mouse looks around (relative mode) while it is on
            SDL_SetWindowRelativeMouseMode(win, photo_on);
            photo_was = photo_on;
        }
        const float photo_dt = photo_ns ? float(double(now - photo_ns) * 1e-9) : 0.0f;
        photo_ns = now;
        if (ls.ready()) {
            const double x = display_x(clock, ls.feed, o, delay, now);
            {
                b3perf::Scope interp_scope(b3perf::kDlInterp);
                drawn = ls.frame(x, interpolate && lat_mode != Latency::kNewest, f);
                if (drawn) blend.apply(ls, x, interpolate && lat_mode != Latency::kNewest, f, now);   // perf1
            }
            Camera pcam;
            if (photo_on && drawn && !f.split && f.cam.valid) photo.advance(photo_dt, f.cam);
            if (photo_on && drawn && !f.split && photo.camera(f.cam, pcam)) {   // photo mode: the scene from the free camera
                ls.photo_frame(*dev, f, pcam, float(w) / float(h > 0 ? h : 1), fog, specular, fp, items);
                photo.dof(pcam, ls.player_car(f), fp.view_proj, fp.dof);   // photo2: the depth of field (off unless turned on)
                photo_drawn = true;
            } else if (drawn && f.split) {   // gsoff1 (§29): the 2-player split screen, two views
                const float a = float(w) / float(h > 0 ? h : 1);
                ls.view.split_frame(*dev, f, ls.feed.models().heads, ls.feed.models().hud.textures, game_cull, fog, specular, w, h, 2.0f * a, a,
                                    views, sfp, items);
            } else if (drawn) {
                ls.params(f, float(w) / float(h > 0 ? h : 1), fog, specular, fp);
                ls.view.render_aspect = float(w) / float(h > 0 ? h : 1);
                {
                    b3perf::Scope world_scope(b3perf::kDlWorld);
                    ls.view.draw_list(*dev, f, ls.feed.models().heads, game_cull, fp.eye, items);
                }
                HudProjection hp;
                std::memcpy(hp.view_proj, fp.view_proj, sizeof hp.view_proj);
                hp.vehicles = &f.vehicles;
                b3perf::Scope hud_scope(b3perf::kDlHud);
                ls.view.hud_list(*dev, f.hud, ls.feed.models().hud.textures, float(w) / float(h > 0 ? h : 1), items, h, w, &hp);
                hud_scope.end();
            }
        }
        if (npad.enabled()) photo.set_scene(drawn && !f.split && f.cam.valid && !pc_page.active());   // photo mode: a race picture
        bool frontend = false;
        if (!drawn && ls.feed.state() == LiveFeed::State::kSynced && !ls.feed.empty()) {
            // menu1 (§22): front end, loading screens, results after the race view: the 2D of the tick the display
            // clock is at (the same delay as a race)
            double x = display_x(clock, ls.feed, o, delay, now);
            if (const long hold = ls.hold_before_race(); hold >= 0) x = std::min(x, double(hold));   // loadlag1
            frontend = ls.frontend_frame(x, f);
            if (frontend) ls.view.frontend_frame(*dev, f, ls.feed.models().heads, ls.feed.models().hud.textures, float(w) / float(h > 0 ? h : 1), w, h, fp, items);
        }
        if (o.native_pad) {   // mac5 (menumouse.h): the mouse against the frame on screen; the cursor; typed text
            MenuMouse& mm = MenuMouse::instance();
            const double now_s = double(SDL_GetTicks()) / 1000.0;
            if (mm.pending()) {
                MenuTargets mt;
                if ((drawn || frontend) && f.hud && ls.view.hud_pieces_frame == f.hud && !ls.feed.empty()) {
                    const auto& rec = ls.feed.at(std::min<std::size_t>(f.tick, ls.feed.last())).menu;
                    mt = menu_targets_of(*f.hud, ls.view.hud_draw_pieces, rec.get(), ls.feed.models().hud.textures);
                }
                mm.frame(mt, pc_page.active(), now_s);
            }
            const bool cursor = !photo_on && mm.cursor_wanted(now_s);   // (photo mode: none, the mouse looks)
            if (cursor != cursor_shown) {
                if (cursor) SDL_ShowCursor();
                else SDL_HideCursor();
                cursor_shown = cursor;
            }
            npad.sync_text_input(win);
        }
        race_log.frame(ls, drawn, drawn ? ls.feed.at(f.tick).poll : 0u);   // gsoff1
        if (!drawn && !frontend) {   // no feed yet, or no 2D: an empty frame
            b3gfx::mat4_identity(fp.view_proj);
            fp.clear[0] = fp.clear[1] = fp.clear[2] = 0.02f;
        }
        if (fps_view != fps_logged) {
            std::printf("[b3live] FPS counter %s\n", fps_view == FpsView::kOff ? "off" : fps_view == FpsView::kMinimal ? "minimal" : fps_view == FpsView::kStandard ? "standard" : "detailed");
            std::fflush(stdout);
            fps_logged = fps_view;
        }
        const bool fps_on = fps_view != FpsView::kOff;
        if (fps_on) {   // lat1: the latency line; the render size and MSAA; the limit is the graph's target
            fps_info.latency_ms = lat_ema >= 0 ? float(lat_ema) : -1.0f;
            fps_info.width = w;
            fps_info.height = h;
            fps_info.msaa = dev->msaa();
            fps_info.target_fps = max_fps > 0 ? float(max_fps) : 60.0f;
        }
        // sdk2b: the code mods' 2D of the tick on screen, after the game's HUD, under the page (not in photo mode)
        if (!photo_drawn && !ls.feed.empty()) {
            const std::size_t ti = drawn || frontend ? std::min<std::size_t>(f.tick, ls.feed.last()) : ls.feed.last();
            if (const auto& mo = ls.feed.at(ti).mod_overlay) ModOverlay::instance().draw_list(*dev, w, h, *mo, items);
        }
        if (pc_page.active() && !ls.feed.empty()) {   // mac5: the page over everything (the 640 x 448 box, as the 2D)
            ls.view.host_ui_hud_textures = &ls.feed.models().hud.textures;
            ls.view.host_ui_list(*dev, pc_page.ui(ls.feed.at(ls.feed.last()).host_fonts, ls.feed.models().hud.textures),
                                 float(w) / float(h > 0 ? h : 1), items, h, w);
        }
        // mac5: the video skip hint of the tick on screen (its opacity from the game), with the device in use
        if (skip_hint_setting && !pc_page.active() && !photo_drawn && !ls.feed.empty()) {
            const std::size_t ti = drawn || frontend ? std::min<std::size_t>(f.tick, ls.feed.last()) : ls.feed.last();
            if (const std::uint8_t op = ls.feed.at(ti).skip_hint) skip_hint.draw_list(*dev, w, h, float(op) / 255.0f, ls.view.prompt_device, items);
        }
        // speedrun1: the speedrun timer of the race tick on screen (not over the page, photo mode or split screen)
        if (drawn && !f.split && !photo_drawn && !pc_page.active() && !ls.feed.empty()) {
            const std::vector<speedrun::Row> rows = ls.speedrun_rows(pc_page.settings().speedrun_timer, std::min<std::size_t>(f.tick, ls.feed.last()));
            speedrun_panel.draw_list(*dev, w, h, rows, items);
        }
        auto banners = [&] {
            if (notice.visible()) {   // mac5: safe mode's banner
                const float ndt = notice_ns ? float(double(now - notice_ns) * 1e-9) : 0.0f;
                notice_ns = now;
                notice.draw_list(*dev, w, h, ndt, items);
            }
            if (!pc_page.active()) TuneConsole::instance().draw_list(*dev, w, h, items);   // tune4: the F9 overlay
            if (!pc_page.active()) CarLookPanel::instance().draw_list(*dev, w, h, items);   // reflect2
        };
        if (!photo_drawn) banners();   // (photo mode: after the F12 shot below, which takes the scene alone)
        if (!drawn) { B3PERF_SCOPE(kLimiter); SDL_Delay(frontend ? 1 : 5); }
        if (report_after >= 0 && double(b3live::now_ns() - report_t0) * 1e-9 >= report_after) {
            report_after = -1;
            report_pending = true;
        }
        if (report_pending) {   // test builds: F10
            report_pending = false;
            if (!report_job.valid()) {
                b3port::BugReportInput in;
                if (char* pref = SDL_GetPrefPath("b3port", "native")) { in.pref = std::filesystem::path(pref); SDL_free(pref); }
                const char* desk = SDL_GetUserFolder(SDL_FOLDER_DESKTOP);
                const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
                in.out_dir = b3port::report_folder({desk ? std::filesystem::path(desk) : std::filesystem::path(),
                                                    docs ? std::filesystem::path(docs) : std::filesystem::path(), in.pref});
                in.system_info = b3port::system_info(gpu_desc, false);
                in.origin = "the game (F10)";
                std::printf("[b3live] F10: a bug report into %s\n", in.out_dir.string().c_str());
                std::fflush(stdout);
                report_job = std::async(std::launch::async, [in] { return b3port::make_bug_report(in); });
                notice.show({"MAKING A BUG REPORT"}, 5.0f);
                notice_ns = 0;
            }
        }
        if (report_job.valid() && report_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const b3port::BugReportResult r = report_job.get();
            const std::string where = r.zip.parent_path().filename().string();
            std::printf("[b3live] bug report %s %s\n", r.ok ? "saved to" : "NOT written:", r.ok ? r.zip.string().c_str() : r.error.c_str());
            std::fflush(stdout);
            if (r.ok) notice.show({"BUG REPORT SAVED", r.zip.filename().string(), "IN " + where, "SEND IT WITH A FEW WORDS ON WHAT HAPPENED"}, 8.0f);
            else notice.show({"BUG REPORT FAILED", r.error}, 8.0f);
            notice_ns = 0;
        }
        if (shot_pending && w > 0 && h > 0) {   // mac5: F12, the frame as shown at the window's size, written on a thread
            shot_pending = false;
            std::vector<std::uint8_t> px;
            const bool ok = drawn && f.split ? dev->draw_views_offscreen(w, h, views, sfp, items, px) : dev->draw_offscreen(w, h, fp, items, px);
            std::string dir;
            if (char* pref = SDL_GetPrefPath("b3port", "native")) { dir = std::string(pref) + "screenshots"; SDL_free(pref); }
            if (ok && !dir.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                const std::time_t t = std::time(nullptr);
                char stamp[32] = "shot";
                if (const std::tm* tm = std::localtime(&t)) std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", tm);
                std::string path = dir + "/Burnout3-" + stamp + ".png";
                for (int n = 2; std::filesystem::exists(path, ec) && n < 100; ++n) path = dir + "/Burnout3-" + stamp + "-" + std::to_string(n) + ".png";
                const int sw = w, sh = h;
                shot_writers.emplace_back([path, sw, sh, px = std::move(px)] {
                    const bool wrote = b3asset::write_png_rgba(path, sw, sh, px.data());
                    std::printf("[b3live] screenshot %s %s\n", wrote ? "saved to" : "NOT written:", path.c_str());
                    std::fflush(stdout);
                });
                notice.show({"SCREENSHOT SAVED", std::filesystem::path(path).filename().string()}, 1.5f);
                notice_ns = 0;
            } else {
                notice.show({"SCREENSHOT FAILED"}, 1.5f);
                notice_ns = 0;
            }
        }
        if (photo_drawn) {   // photo mode: the banners and the help box over the picture, never in a screenshot
            banners();
            photo.draw_help(*dev, w, h, photo_dt, ls.view.prompt_device, ls.view.hud_size, items);
        }
        if (fps_on) {   // fpsoverlay2: after the F12 shot, which never holds it; clear of a notice, in the top left while photo mode's UI is up
            FpsPanel::Rect nbox, *avoid = nullptr;
            float nb[4];
            if (notice.visible() && notice.box(w, h, nb)) { nbox = {nb[0], nb[1], nb[2], nb[3]}; avoid = &nbox; }
            const bool photo_ui = photo_drawn && photo.ui() != PhotoUi::kHidden;
            fps_panel.draw_list(*dev, w, h, now, fps_view, fps_corner_in_photo(fps_corner, photo_ui), ls.view.hud_size, fps_info, avoid, drawn && !frontend && !photo_drawn && !f.split, items);
        }
        drawlist_scope.end();
        const std::uint64_t cpu_ns = b3live::now_ns();   // perf
        if (drawn && f.split) dev->draw_views(views, sfp, items, &w, &h);   // gsoff1 (§29): `items` holds the 2D
        else dev->draw_frame(fp, items, &w, &h);
        const std::uint64_t done_ns = b3live::now_ns();
        if (drawn || frontend) wstats.add(drawn ? 0 : 1, top_ns, loop_ns, cpu_ns, done_ns);
        fps_panel.frame(done_ns);
        if (drawn || frontend) {   // lat1: latch -> submitted frame, plus half a tick of the input's own wait for the latch
            lat_log.frame(ls.feed, f, clock, loop_ns, now, done_ns);
            const double age = clock.latch_age_ms(ls.feed, double(std::min(f.tick, ls.feed.last())) + double(f.alpha), done_ns);
            if (age >= 0 && age < 1000) lat_ema = lat_ema < 0 ? age + 8.3 : lat_ema + 0.02 * (age + 8.3 - lat_ema);
        }
        if (loop_stage == 0) { sl.enter("running"); sl.finish(); loop_stage = 1; }
        ++frames;
        if (cur_settings.battery_saver && now - battery_checked_ns >= 2000000000ull) {   // effects2: the power state changed?
            battery_checked_ns = now;
            const bool on_batt = on_battery_power();
            if (on_batt != battery_now) {   // only the battery-dependent values: the frame cap and the two particle levels
                battery_now = on_batt;
                const b3settings::Settings s = on_batt ? b3settings::on_battery(cur_settings) : cur_settings;
                ls.view.effects.sparks = s.sparks;
                ls.view.effects.popcorn = s.popcorn;
                max_fps = s.frame_cap > 0 ? double(s.frame_cap) : o.max_fps;
                fps_info.target_fps = max_fps > 0 ? float(max_fps) : 60.0f;
                if (on_batt) std::printf("[b3settings] on battery: BATTERY SAVER holds the frame rate to 60 and the effects to Medium at most\n");
                else std::printf("[b3settings] on mains power: the settings as chosen\n");
            }
        }
        if (max_fps > 0) {   // --max-fps / FRAME RATE LIMIT
            // 1.1: on a fixed schedule (each frame's deadline one period after the last one's), so the average is the
            // limit exactly; a frame that ran late starts a new schedule instead of rushing to catch up. With vsync at a
            // refresh the limit does not go above, vsync alone paces the frames: a second clock at the same rate beats
            // against the display's and drops a frame now and then (a 60 limit on a 60 Hz screen showed 55-58).
            static std::uint64_t next_ns = 0;
            static float refresh_hz = -1.0f;
            if (refresh_hz < 0.0f) {
                const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
                refresh_hz = dm && dm->refresh_rate > 0.0f ? dm->refresh_rate : 0.0f;
            }
            const bool vsync_paces = std::strcmp(dev->present_mode_name(), "vsync") == 0 && refresh_hz > 0.0f && max_fps >= double(refresh_hz) - 1.0;
            const std::uint64_t period = std::uint64_t(1e9 / max_fps), t_end = b3live::now_ns();
            if (vsync_paces) next_ns = 0;
            else {
                next_ns = next_ns == 0 || t_end > next_ns + period ? t_end + (t_end - now < period ? period - (t_end - now) : 0) : next_ns + period;
                if (next_ns > t_end) { B3PERF_SCOPE(kLimiter); SDL_DelayPrecise(next_ns - t_end); }
            }
        }
        b3perf::frame_end(w, h);   // perf3
        if (now - title_at >= 500000000ull) {
            const double secs = double(now - title_at) * 1e-9;
            const auto& fd = ls.feed;
            const std::string route = ls.view.route.empty() ? "no route" : std::filesystem::path(ls.view.route).filename().string();
            char title[400];
            char lat[96];   // lat1: the latency mode and its input -> screen estimate (latch age at submit + half a tick)
            int n = std::snprintf(lat, sizeof lat, "latency %s", o.latency.c_str());
            if (lat_ema >= 0) n += std::snprintf(lat + n, sizeof lat - std::size_t(n), " ~%.0f ms", lat_ema);
            if (clock.sched_on) std::snprintf(lat + n, sizeof lat - std::size_t(n), " (margin %.1f ms)", clock.margin_ms);
            else if (lat_mode != Latency::kNewest) std::snprintf(lat + n, sizeof lat - std::size_t(n), " (delay %.1f ticks)", delay);
            std::snprintf(title, sizeof title, "%s%s %s - %s%s - poll %u +%.2f - %s - %s - interp %s - %.0f fps (%.2f ms) - %dx%d - %s x%d - resyncs %llu",
                          o.safe_mode ? "SAFE MODE - " : "", title_name.c_str(), o.name.c_str(), state_name(fd.state()), fd.state() == LiveFeed::State::kSynced && !drawn ? (frontend ? " (front end)" : " (no race on screen)") : "",
                          drawn || frontend ? fd.at(f.tick).poll : 0u, drawn ? double(f.alpha) : 0.0, lat, route.c_str(), interpolate ? "on" : "off",
                          frames / secs, 1000.0 * secs / frames, w, h, dev->present_mode_name(), dev->frames_in_flight(), static_cast<unsigned long long>(fd.resyncs));
            SDL_SetWindowTitle(win, title);
            frames = 0;
            title_at = now;
        }
    }
    for (std::thread& t : shot_writers) t.join();   // mac5: the screenshots still being written
    lat_log.write(o, dev->present_mode_name(), dev->frames_in_flight());
    wstats.print();   // perf
    WindowStats::print_decode(ls.feed);
    if (o.native_audio) NativeAudio::instance().close();   // play1
    npad.release_pads();   // pad1: the DualSense's triggers free, the gyro sensors off
    dev.reset();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

}  // namespace b3replay
