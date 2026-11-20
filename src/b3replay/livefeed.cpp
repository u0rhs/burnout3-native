// LiveFeed (src/b3replay/live.h): the live session's records decoded into ticks, from the shared-memory ring (live1,
// §13) or the in-process feed (inproc1, §23). No SDL here (tests/test_inproc.cpp links it alone).
#include "live.h"
#include "garagecam.h"   // garagecam1

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace b3replay {

namespace {

constexpr std::size_t kKeepTicks = 1200;       // 20 s of decoded ticks (display delay and camera keys need a few)
constexpr std::size_t kGapTicks = 60;          // the offline reader's segment gap (capture.cpp)

}  // namespace

void VehicleMotion::apply(Tick& t) {
    const auto key = [](const Vehicle& v) { return std::uint64_t(v.id) << 32 | v.data; };
    const auto moved = [](const Mem& m, const Vehicle& v) {
        for (int k = 0; k < 3; ++k) if (std::fabs(v.main.r[3][k] - m.pos[k]) > 1e-5f) return true;
        return false;
    };
    bool world = false;   // something moves in this tick: the game is running
    for (const Vehicle& v : t.vehicles) {
        const auto it = mem_.find(key(v));
        if (it != mem_.end() && moved(it->second, v)) { world = true; break; }
    }
    for (Vehicle& v : t.vehicles) {
        auto [it, fresh] = mem_.try_emplace(key(v));
        Mem& m = it->second;
        const float* p = v.main.r[3];
        if (fresh) {
            std::memcpy(m.pos, p, sizeof m.pos);
            m.poll = t.poll;
            m.now = t.now;
        } else if (moved(m, v)) {
            const std::uint32_t gap = t.poll - m.poll;
            // the time between: the game's own timer (gWorld+0x20: a 50 Hz disc's ticks are 1/50 s), else the polls at 60
            const float dn = t.now - m.now;
            const float dt = dn > 0.001f && dn < 0.1f ? dn : float(gap) / 60.0f;
            float vel[3], s2 = 0;
            for (int k = 0; k < 3; ++k) {
                vel[k] = gap > 0 ? (p[k] - m.pos[k]) / dt : 0.0f;
                s2 += vel[k] * vel[k];
            }
            m.known = gap >= 1 && gap <= 4 && s2 < 150.0f * 150.0f;
            if (m.known) std::memcpy(m.vel, vel, sizeof m.vel);
            else std::memset(m.vel, 0, sizeof m.vel);
            std::memcpy(m.pos, p, sizeof m.pos);
            m.poll = t.poll;
            m.now = t.now;
            m.still = 0;
        } else if (world && ++m.still >= 3) {   // the others move, this one does not: stopped
            std::memset(m.vel, 0, sizeof m.vel);
            m.known = true;
            m.poll = t.poll;
            m.now = t.now;
        }
        m.seen = t.poll;
        std::memcpy(v.motion, m.vel, sizeof v.motion);
        v.motion_known = m.known;
    }
    if (!t.vehicles.empty())
        for (auto it = mem_.begin(); it != mem_.end();)
            it = t.poll - it->second.seen > 600u ? mem_.erase(it) : std::next(it);
}

long LiveFeed::index_of_poll(std::uint32_t poll) const {
    for (std::size_t k = ticks_.size(); k-- > 0;)
        if (ticks_[k].poll == poll) return long(base_ + k);
    return -1;
}

int LiveFeed::drain(int max_records) {
    if (inproc) return drain_inproc(max_records);
    if (state_ == State::kEnded) return 0;
    if (state_ == State::kNoWriter) {
        const std::uint64_t now = b3live::now_ns();
        if (now < next_open_ns_) return 0;
        next_open_ns_ = now + 250000000ull;
        if (!map_.open(name_, nullptr)) return 0;
        b3live::Header* h = map_.header();
        const std::uint64_t head = h->head.load(std::memory_order_acquire), key = h->key_at.load(std::memory_order_acquire);
        if (key != b3live::kNoKey && head - key <= h->capacity) {   // the latest keyframe is still in the ring
            pos_ = key;
            state_ = State::kSynced;
            image_.clear();
        } else {
            request_at_ = head;
            h->want_key.store(1, std::memory_order_release);
            state_ = State::kWaitKey;
        }
        std::fprintf(stderr, "[b3live] %s: writer found (%llu MB ring), %s\n", name_.c_str(),
                     static_cast<unsigned long long>(h->capacity >> 20), state_ == State::kSynced ? "starting at its keyframe" : "keyframe requested");
    }
    b3live::Header* h = map_.header();
    if (state_ == State::kWaitKey) {
        const std::uint64_t key = h->key_at.load(std::memory_order_acquire);
        if (key == b3live::kNoKey || key < request_at_) {
            const std::uint64_t now = b3live::now_ns();
            if (h->closed.load(std::memory_order_acquire)) state_ = State::kEnded;
            else if (now >= next_alive_ns_) { next_alive_ns_ = now + 1000000000ull; if (!b3live::writer_alive(h)) state_ = State::kEnded; }
            return 0;
        }
        pos_ = key;
        state_ = State::kSynced;
        image_.clear();
    }
    int n = 0;
    b3live::RecordHead rh{};
    auto resync = [&](const char* what) {
        ++resyncs;
        h->reader_resyncs.fetch_add(1, std::memory_order_relaxed);
        request_at_ = h->head.load(std::memory_order_acquire);
        h->want_key.store(1, std::memory_order_release);
        state_ = State::kWaitKey;
        seg_.active = false;
        std::fprintf(stderr, "[b3live] %s: %s at %llu, keyframe requested\n", name_.c_str(), what, static_cast<unsigned long long>(pos_));
    };
    while (n < max_records) {
        const int r = b3live::next_record(map_, pos_, rh, body_);
        if (r == 0) {
            if (h->closed.load(std::memory_order_acquire) && pos_ == h->head.load(std::memory_order_acquire)) state_ = State::kEnded;
            else if (const std::uint64_t now = b3live::now_ns(); now >= next_alive_ns_) {   // a game that ended without closing
                next_alive_ns_ = now + 1000000000ull;
                if (!b3live::writer_alive(h) && pos_ == h->head.load(std::memory_order_acquire)) {
                    std::fprintf(stderr, "[b3live] %s: the writer's process ended without closing the feed\n", name_.c_str());
                    state_ = State::kEnded;
                }
            }
            break;
        }
        if (r < 0) { resync("reader lapped"); break; }   // we fell a whole ring behind (or read a torn record)
        if (!decode(rh.kind, reinterpret_cast<const std::uint8_t*>(body_.data()), body_.size(), rh.ns)) { resync("malformed record"); break; }
        ++n;
    }
    h->reader_pos.store(pos_, std::memory_order_relaxed);
    return n;
}

// One record into the image and the kept ticks (both sources).
bool LiveFeed::decode(std::uint32_t kind, const std::uint8_t* body, std::size_t n, std::uint64_t ns, std::uint64_t sched) {
    const std::uint64_t t0 = b3live::now_ns();   // perf
    if (kind == b3live::kKeyframe) { image_.clear(); ++keyframes; }
    Tick t;
    if (!cap_.apply_record(image_, body, n, t)) return false;
    if (decode_ms.size() < (4u << 20)) decode_ms.push_back(float(double(b3live::now_ns() - t0) * 1e-6) * (race_tick(t) ? -1.0f : 1.0f));
    ++records;
    if (verify) {
        const auto it = verify_index.find(t.poll);
        std::string what;
        if (it == verify_index.end()) ++verify_missing;
        else if (same_tick(t, cap_, verify->ticks[it->second], *verify, &what)) ++verified;
        else if (++verify_differ <= 5) std::fprintf(stderr, "[b3live] verify: poll %u differs (%s)\n", t.poll, what.c_str());
    }
    push(std::move(t), ns, sched);
    return true;
}

// inproc1 (§23): the in-process feed. Records arrive in order and are never overwritten, so the only keyframe is the
// producer's first (or one after a dropped record, over the feed's byte cap).
int LiveFeed::drain_inproc(int max_records) {
    if (state_ == State::kEnded) return 0;
    const bool closed = inproc->closed();   // before take(): closed and nothing left = every record seen
    if (b3live::InprocRecord* got = inproc->take()) {
        if (!pending_) pending_ = got;
        else { b3live::InprocRecord* e = pending_; while (e->next) e = e->next; e->next = got; }
    }
    int n = 0;
    while (pending_ && n < max_records) {
        b3live::InprocRecord* r = pending_;
        pending_ = r->next;
        if (state_ != State::kSynced) {
            if (r->kind != b3live::kKeyframe) { inproc->release_one(r); continue; }   // after a drop: up to the next keyframe
            if (state_ == State::kNoWriter) std::fprintf(stderr, "[b3live] %s: in-process feed, first record\n", name_.c_str());
            state_ = State::kSynced;
            image_.clear();
        }
        const bool ok = decode(r->kind, r->body(), r->size, r->ns, r->sched);
        inproc->release_one(r);
        if (!ok) {
            ++resyncs;
            state_ = State::kWaitKey;
            seg_.active = false;
            inproc->request_key();
            std::fprintf(stderr, "[b3live] %s: malformed record, keyframe requested\n", name_.c_str());
            continue;
        }
        ++n;
    }
    if (!pending_ && closed && state_ != State::kEnded) state_ = State::kEnded;
    return n;
}

void LiveFeed::push(Tick&& t, std::uint64_t ns, std::uint64_t sched) {
    const std::size_t idx = base_ + ticks_.size();
    ++ticks_total;
    if (race_tick(t)) {
        if (!seg_.active || t.route_size != seg_.route_size || t.route_units != seg_.route_units || idx - seg_.last_race > kGapTicks)
            seg_ = Seg{true, seg_.id + 1, idx, idx, t.route_size, t.route_units, 0};
        seg_.last_race = idx;
        ++seg_.race_ticks;
    } else if (seg_.active && idx - seg_.last_race <= kGapTicks && !ticks_.empty()) {
        // a gap inside a race (pause, camera cut): the last race state, as capture.cpp fills segment gaps
        const auto poll = t.poll, vb = t.vblank;
        t = ticks_.back();
        t.poll = poll;
        t.vblank = vb;
    } else if (seg_.active) {
        seg_.active = false;
    }
    if (t.host_hold && !ticks_.empty()) {
        // autosave1 (tools/native/save_screens.h): the game is on its AUTOSAVE page and the player is not to see it: this
        // tick shows what the previous one did (the page's own draws are dropped), as a gap inside a race does above
        const auto poll = t.poll, vb = t.vblank;
        const auto skip = t.skip_hint;
        const HostRaceTime race_time = t.race_time;
        t = ticks_.back();
        t.poll = poll;
        t.vblank = vb;
        t.skip_hint = skip;
        t.race_time = race_time;   // todrace
        t.host_hold = true;
    }
    if (!warned_unbound_ && t.hud && t.hud->stats.unbound_texture) {   // font1: text would draw as solid blocks
        warned_unbound_ = true;
        std::fprintf(stderr, "[b3live] %s: poll %u: %u textured 2D triangles without a raster (the game's SetTexture calls not all"
                     " captured: a SetTexture path the capture does not hook?)\n", name_.c_str(), t.poll, t.hud->stats.unbound_texture);
    }
    GarageCam::instance().on_tick(t);   // garagecam1: the car pages' view (no input: 0 0)
    motion_.apply(t);   // photoblur: the vehicles' velocities (Vehicle::motion)
    ticks_.push_back(std::move(t));
    ns_.push_back(ns);
    sched_.push_back(sched);
    if (on_push) on_push(ticks_.back());   // speedrun1
    while (ticks_.size() > kKeepTicks) { ticks_.pop_front(); ns_.pop_front(); sched_.pop_front(); ++base_; }
}

}  // namespace b3replay
