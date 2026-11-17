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

TEST_MAIN("test_inproc")
