// inproc1 (docs/native-renderer.md §23): the in-process feed (include/b3live/inproc.h) into LiveFeed (the renderer's
// reader, src/b3replay/livefeed.cpp). data/scene/race2.b3scene is played by the hook's rules (src/gameview/scene_capture.cpp's
// inproc_publish: a keyframe = every block of the shadow image when want_key(), else the poll's delta)
// from a producer thread into the feed, while the consumer drains it as the render thread does. Every tick LiveFeed
// decodes must equal the file reader's tick of the same poll (capture.h same_tick, LiveFeed::verify).
//   1. no cap reached: every poll arrives, one keyframe, no drop;
//   2. a tiny cap with a slow consumer: records are dropped, the producer sends keyframes after them, and every tick
//      that does arrive is still identical (no tick decoded from a delta after a gap).
#include <cstdlib>
#include "test.h"

#include <zlib.h>

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "b3live/inproc.h"
#include "../src/b3replay/capture.h"
#include "../src/b3replay/live.h"
#include "../src/gameview/grab_block.h"

namespace {

// a local capture (not in git: game-derived): $B3_TEST_CAPTURE, else data/scene/race2.b3scene; skipped when absent
const char* const kCapture = [] { const char* e = std::getenv("B3_TEST_CAPTURE"); return e && *e ? e : "data/scene/race2.b3scene"; }();

std::vector<std::vector<std::uint8_t>> read_records() {
    std::vector<std::vector<std::uint8_t>> out;
    gzFile f = gzopen(kCapture, "rb");
    if (!f) return out;
    auto rd = [&](void* p, unsigned n) { return gzread(f, p, n) == int(n); };
    char magic[8];
    std::uint32_t fh[2], head[4];
    if (!rd(magic, 8) || std::memcmp(magic, "B3SCENE", 8) != 0 || !rd(fh, 8)) { gzclose(f); return out; }
    while (rd(head, 16) && head[0] == 0x4B434954) {
        std::vector<std::uint8_t> rec(reinterpret_cast<std::uint8_t*>(head), reinterpret_cast<std::uint8_t*>(head) + 16);
        bool ok = true;
        for (std::uint32_t r = 0; ok && r < head[3]; ++r) {
            std::uint32_t run[2];
            if (!rd(run, 8)) { ok = false; break; }
            const std::size_t at = rec.size();
            rec.resize(at + 8 + run[1]);
            std::memcpy(rec.data() + at, run, 8);
            ok = rd(rec.data() + at + 8, run[1]);
        }
        if (!ok) break;
        out.push_back(std::move(rec));
    }
    gzclose(f);
    return out;
}

// The hook's shadow image and inproc_publish (scene_capture.cpp build_body)
struct Producer {
    struct Page { std::uint8_t bytes[0x1000]; std::uint8_t valid[0x100]; };
    std::map<std::uint32_t, Page> pages;
    b3live::Inproc& q;
    std::uint64_t keyframes = 0, dropped = 0;
    explicit Producer(b3live::Inproc& feed) : q(feed) {}
    void poll(const std::vector<std::uint8_t>& rec) {
        for (std::size_t at = 16; at + 8 <= rec.size();) {
            std::uint32_t run[2];
            std::memcpy(run, rec.data() + at, 8);
            for (std::uint32_t k = 0; k < run[1]; k += 16) {
                Page& pg = pages[(run[0] + k) & ~0xFFFu];
                const std::uint32_t off = (run[0] + k) & 0xFFFu;
                std::memcpy(pg.bytes + off, rec.data() + at + 8 + k, 16);
                pg.valid[off / 16] = 1;
            }
            at += 8 + run[1];
        }
        if (!q.want_key()) { if (!q.push(b3live::kDelta, rec.data(), rec.size())) ++dropped; return; }
        std::vector<std::uint8_t> body(rec.begin(), rec.begin() + 16);
        std::uint32_t runs = 0, run_start = 0, run_end = 0;
        std::size_t run_at = 0;
        for (const auto& [base, pg] : pages)
            for (std::uint32_t k = 0; k < 0x100; ++k) {
                if (!pg.valid[k]) continue;
                const std::uint32_t b = base + 16 * k;
                if (runs == 0 || b != run_end) {
                    run_at = body.size();
                    run_start = b;
                    body.resize(body.size() + 8);
                    std::memcpy(body.data() + run_at, &b, 4);
                    ++runs;
                }
                body.insert(body.end(), pg.bytes + 16 * k, pg.bytes + 16 * k + 16);
                run_end = b + 16;
                const std::uint32_t len = run_end - run_start;
                std::memcpy(body.data() + run_at + 4, &len, 4);
            }
        std::memcpy(body.data() + 12, &runs, 4);
        if (q.push(b3live::kKeyframe, body.data(), body.size())) ++keyframes;
        else ++dropped;
    }
};

struct Result { std::uint64_t records = 0, verified = 0, differ = 0, missing = 0, keyframes = 0, dropped = 0, ticks = 0; bool ended = false; };

Result run(const std::vector<std::vector<std::uint8_t>>& recs, const b3replay::Capture& ref, std::uint64_t cap, int consumer_sleep_us) {
    b3live::Inproc q(cap);
    b3replay::LiveFeed feed("inproc-test");
    feed.inproc = &q;
    feed.verify = &ref;
    for (std::size_t i = 0; i < ref.ticks.size(); ++i) feed.verify_index[ref.ticks[i].poll] = i;
    Producer p(q);
    std::thread prod([&] {
        for (const auto& r : recs) p.poll(r);
        q.close();
    });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (feed.state() != b3replay::LiveFeed::State::kEnded && std::chrono::steady_clock::now() < until) {
        if (feed.drain() == 0 || consumer_sleep_us) std::this_thread::sleep_for(std::chrono::microseconds(consumer_sleep_us ? consumer_sleep_us : 200));
    }
    prod.join();
    feed.drain();
    Result r;
    r.records = feed.records;
    r.verified = feed.verified;
    r.differ = feed.verify_differ;
    r.missing = feed.verify_missing;
    r.keyframes = feed.keyframes;
    r.dropped = p.dropped;
    r.ticks = feed.ticks_total;
    r.ended = feed.state() == b3replay::LiveFeed::State::kEnded;
    CHECK(q.dropped.load() == p.dropped);
    CHECK(q.keyframes.load() == p.keyframes);
    return r;
}

}  // namespace

TEST(inproc_feed_matches_the_capture_file) {
    const auto recs = read_records();
    if (recs.size() < 5000) { std::printf("  (skipped: %s not readable)\n", kCapture); return; }
    b3replay::Capture ref;
    std::string err;
    CHECK(ref.load(kCapture, &err, false));
    if (ref.ticks.size() != recs.size()) { CHECK(ref.ticks.size() == recs.size()); return; }
    const Result r = run(recs, ref, 512ull << 20, 0);
    std::printf("  no cap: %llu records, %llu ticks identical, %llu differ, %llu missing, %llu keyframes, %llu dropped\n",
                static_cast<unsigned long long>(r.records), static_cast<unsigned long long>(r.verified), static_cast<unsigned long long>(r.differ),
                static_cast<unsigned long long>(r.missing), static_cast<unsigned long long>(r.keyframes), static_cast<unsigned long long>(r.dropped));
    CHECK(r.ended);
    CHECK(r.records == recs.size());
    CHECK(r.verified == recs.size());
    CHECK(r.differ == 0 && r.missing == 0);
    CHECK(r.keyframes == 1 && r.dropped == 0);
}

TEST(inproc_feed_drops_resync_at_keyframes) {
    const auto recs = read_records();
    if (recs.size() < 5000) { std::printf("  (skipped: %s not readable)\n", kCapture); return; }
    b3replay::Capture ref;
    std::string err;
    CHECK(ref.load(kCapture, &err, false));
    // race2's keyframes stay under 2.5 MB; a 3 MB cap with a consumer that sleeps 2 ms per drain drops records
    const Result r = run(recs, ref, 3ull << 20, 2000);
    std::printf("  3 MB cap, slow reader: %llu records, %llu ticks identical, %llu differ, %llu keyframes, %llu dropped\n",
                static_cast<unsigned long long>(r.records), static_cast<unsigned long long>(r.verified), static_cast<unsigned long long>(r.differ),
                static_cast<unsigned long long>(r.keyframes), static_cast<unsigned long long>(r.dropped));
    CHECK(r.ended);
    CHECK(r.dropped > 0);
    CHECK(r.keyframes > 1);
    CHECK(r.records > 0 && r.records + r.dropped == recs.size());
    CHECK(r.verified == r.records);
    CHECK(r.differ == 0 && r.missing == 0);
}

// gsoff1 (§28.6): the route from the STATIC.DAT header in guest memory. Two routes with the same size and unit count
// (as a route's V1 / V2) differ in header words; in guest memory the game has added the load address to the offsets.
TEST(route_by_header_tells_v1_from_v2) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("b3_route_hdr_" + std::to_string(::getpid()));
    std::vector<std::uint8_t> a(0x1000, 0), b(0x1000, 0);
    const auto put = [](std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) { std::memcpy(d.data() + at, &v, 4); };
    for (auto* d : {&a, &b}) { put(*d, 4, 0x1000); std::int16_t n = 40; std::memcpy(d->data() + 0x54, &n, 2); }
    put(a, 0x38, 0x200); put(b, 0x38, 0x240);   // an offset that differs between the two
    for (const auto& [dir, d] : {std::pair{"V1", &a}, std::pair{"V2", &b}}) {
        fs::create_directories(root / "TRACKS" / "XX" / dir);
        std::FILE* f = std::fopen((root / "TRACKS" / "XX" / dir / "STATIC.DAT").string().c_str(), "wb");
        std::fwrite(d->data(), 1, d->size(), f);
        std::fclose(f);
    }
    b3replay::Tick t;
    t.route_size = 0x1000;
    t.route_units = 40;
    t.route_base = 0x01015880;
    t.route_head_ok = true;
    std::memcpy(t.route_head.data(), b.data(), 0x60);
    {
        const std::uint32_t reloc = 0x240 + t.route_base;   // the game relocated +0x38
        std::memcpy(t.route_head.data() + 0x38, &reloc, 4);
    }
    CHECK(b3replay::route_by_header(root.string(), t) == (root / "TRACKS" / "XX" / "V2").string());
    std::uint32_t other = 0x200 + t.route_base;
    std::memcpy(t.route_head.data() + 0x38, &other, 4);
    CHECK(b3replay::route_by_header(root.string(), t) == (root / "TRACKS" / "XX" / "V1").string());
    other = 0x999;   // matches neither: no route
    std::memcpy(t.route_head.data() + 0x38, &other, 4);
    CHECK(b3replay::route_by_header(root.string(), t).empty());
    t.route_head_ok = false;
    CHECK(b3replay::route_by_header(root.string(), t).empty());
    std::error_code ec;
    fs::remove_all(root, ec);
}

// scene_capture.cpp's grab from a host buffer (the split screen's view-1 copy: a 16-byte head and 8-byte entries, so an
// odd entry count ends mid-block): only the buffer's bytes are read, the rest of the last block is 0
TEST(grab_block_reads_only_the_buffer_and_zero_pads) {
    std::vector<std::uint8_t> big(64, 0xEE);   // the buffer is the first 24 bytes; the rest is "past the end"
    for (int i = 0; i < 24; ++i) big[std::size_t(i)] = std::uint8_t(i + 1);
    const std::uint32_t a = 0x02000000U;
    std::uint8_t blk[16];
    b3gv::grab_block(big.data(), a, 24, a, blk);
    bool first = true;
    for (int i = 0; i < 16; ++i) first = first && blk[i] == std::uint8_t(i + 1);
    CHECK(first);
    b3gv::grab_block(big.data(), a, 24, a + 16, blk);
    bool second = true;
    for (int i = 0; i < 16; ++i) second = second && blk[i] == (i < 8 ? std::uint8_t(17 + i) : 0);
    CHECK(second);   // 8 bytes of the buffer, then 0 (not the 0xEE after it)
    b3gv::grab_block(big.data() + 4, a + 4, 8, a, blk);   // unaligned start: bytes 4..11 of the block
    bool mid = true;
    for (int i = 0; i < 16; ++i) mid = mid && blk[i] == (i >= 4 && i < 12 ? std::uint8_t(i + 1) : 0);
    CHECK(mid);
}

// trafmem: the shot hold's rule -- a race tick at or after the hold kept up to kHoldCap ticks and kHoldBytes (TRAFFIC
// Extreme: ~0.3 MB a tick, the old tick-only cap let it reach ~10 GB); the game waits from kHoldWaitBytes on, below the
// drop bound, so with native_play's wait nothing is dropped
TEST(shot_hold_bounded_by_bytes) {
    using F = b3replay::LiveFeed;
    CHECK(F::hold_keeps(500, 400, 5000, std::size_t(1) << 30));                    // held: after the hold, within both bounds
    CHECK(!F::hold_keeps(399, 400, 5000, std::size_t(1) << 30));                   // before the hold: trimmed as the window
    CHECK(!F::hold_keeps(500, 400, F::kHoldCap + 1, std::size_t(1) << 30));        // the tick count bound (as before)
    CHECK(!F::hold_keeps(500, 400, 5000, F::kHoldBytes + 1));                      // the byte bound
    CHECK(!F::hold_keeps(500, 0xFFFFFFFFu, 5000, 0));                              // no hold
    CHECK(F::kHoldWaitBytes < F::kHoldBytes && F::hold_wait_bytes() == F::kHoldWaitBytes && F::hold_bytes() == F::kHoldBytes);   // the wait before any drop
    CHECK(!F::hold_full(true, F::kHoldWaitBytes - 1) && F::hold_full(true, F::kHoldWaitBytes) && !F::hold_full(false, F::kHoldBytes));
    // the look ahead: the ticks up to the game's newest poll, decoded or not
    CHECK(!F::hold_ahead_full(0xFFFFFFFFu, 100000, 1 << 20));                                   // no hold: no wait
    CHECK(!F::hold_ahead_full(10000, 10500, 500000));                                          // 500 ticks of 0.5 MB: 250 MB
    CHECK(F::hold_ahead_full(10000, 12200, 500000));                                           // 2200 of them: over 1 GiB
    CHECK(!F::hold_ahead_full(12300, 12200, 500000));                                          // the hold ahead of the game
    b3replay::Tick t;
    t.vehicles.resize(70);   // TRAFFIC Extreme's ~70 vehicles: ~0.3 MB
    CHECK(F::tick_bytes(t) >= 70 * sizeof(b3replay::Vehicle) && F::tick_bytes(t) < 400000);
}

TEST_MAIN("test_inproc")

// photoblur: the vehicles' velocities as the ticks come (VehicleMotion, into Vehicle::motion): from the last ticks in
// which a car moved; held while nothing moves (the pause); 0 for a car that stops while the others drive on
TEST(vehicle_motion_holds_through_a_pause) {
    using b3replay::Tick;
    using b3replay::Vehicle;
    b3replay::VehicleMotion vm;
    float hz = 60;   // the game's timer per poll
    const auto tick = [&hz](std::uint32_t poll, float ax, float bx) {
        Tick t;
        t.poll = poll;
        t.now = float(poll) / hz;
        Vehicle a, b;
        a.id = 1; a.data = 10; a.main.r[3][0] = ax;
        b.id = 2; b.data = 10; b.main.r[3][0] = bx;
        t.vehicles = {a, b};
        return t;
    };
    Tick t = tick(100, 0, 50);
    vm.apply(t);
    CHECK(!t.vehicles[0].motion_known);   // first seen: not known yet
    t = tick(101, 0.5f, 50.25f);
    vm.apply(t);
    CHECK(t.vehicles[0].motion_known);
    CHECK_NEAR(t.vehicles[0].motion[0], 30.0, 1e-3);   // 0.5 m in a poll: 30 m/s
    CHECK_NEAR(t.vehicles[1].motion[0], 15.0, 1e-3);
    t = tick(103, 1.5f, 50.25f);   // two polls apart (a skipped poll); car 2 stands while car 1 drives
    vm.apply(t);
    CHECK_NEAR(t.vehicles[0].motion[0], 30.0, 1e-3);
    CHECK_NEAR(t.vehicles[1].motion[0], 15.0, 1e-3);   // one still tick: not stopped yet
    for (std::uint32_t p = 104; p < 107; ++p) {
        t = tick(p, 1.5f + 0.5f * float(p - 103), 50.25f);
        vm.apply(t);
    }
    CHECK(t.vehicles[1].motion_known && t.vehicles[1].motion[0] == 0.0f);   // still while the other moves: stopped
    // the pause: nothing moves for a long time; the velocities are held
    for (std::uint32_t p = 107; p < 2000; ++p) {
        t = tick(p, 3.0f, 50.25f);
        vm.apply(t);
    }
    CHECK(t.vehicles[0].motion_known);
    CHECK_NEAR(t.vehicles[0].motion[0], 30.0, 1e-3);
    // resumed after the pause: the first step spans the pause (unknown), the next is known again
    t = tick(2000, 3.5f, 50.25f);
    vm.apply(t);
    CHECK(!t.vehicles[0].motion_known && t.vehicles[0].motion[0] == 0.0f);
    t = tick(2001, 4.0f, 50.25f);
    vm.apply(t);
    CHECK_NEAR(t.vehicles[0].motion[0], 30.0, 1e-3);
    // a jump (a reset onto the track) is not a speed
    t = tick(2002, 400.0f, 50.25f);
    vm.apply(t);
    CHECK(!t.vehicles[0].motion_known);
    // a 50 Hz disc: 0.5 m a tick is 25 m/s by the game's timer
    hz = 50;
    b3replay::VehicleMotion pal;
    t = tick(3000, 0, 0);
    pal.apply(t);
    t = tick(3001, 0.5f, 0);
    pal.apply(t);
    CHECK_NEAR(t.vehicles[0].motion[0], 25.0, 1e-2);
}
