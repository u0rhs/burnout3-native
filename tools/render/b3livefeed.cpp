// b3livefeed: plays a scene capture into a b3live ring as the game's scene hook would publish it (docs/native-renderer.md
// §13), for testing the live renderer without the game: each file record is one poll, published as its delta, or as a
// keyframe (every block of the shadow image) when the ring has none yet or the reader asked for one -- the rules of
// src/gameview/scene_capture.cpp's live_publish.
//
//   b3livefeed <capture.b3scene> --name <ring> [--rate TICKS_PER_S (60; 0 = as fast as possible)] [--capacity-kb N]
//              [--wait-reader S (wait up to S s for the reader to attach before publishing)] [--linger S]
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "b3live/ring.h"

#include <zlib.h>

namespace {

constexpr std::uint32_t kPage = 0x1000;
struct Page { std::uint8_t bytes[kPage]; std::uint8_t valid[kPage / 16]; };

void put32(std::vector<std::uint8_t>& b, std::uint32_t v) { const std::size_t n = b.size(); b.resize(n + 4); std::memcpy(b.data() + n, &v, 4); }

}  // namespace

int main(int argc, char** argv) {
    std::string path, name;
    double rate = 60, wait_reader = 0, linger = 2;
    std::uint64_t capacity = b3live::kDefaultCapacity;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (s == "--name") name = next();
        else if (s == "--rate") rate = std::atof(next());
        else if (s == "--capacity-kb") capacity = std::uint64_t(std::atoll(next())) << 10;
        else if (s == "--wait-reader") wait_reader = std::atof(next());
        else if (s == "--linger") linger = std::atof(next());
        else path = s;
    }
    if (path.empty() || name.empty() || (capacity & (capacity - 1)) != 0) {
        std::fprintf(stderr, "usage: b3livefeed <capture> --name <ring> [--rate N] [--capacity-kb N (power of two)] [--wait-reader S] [--linger S]\n");
        return 2;
    }
    gzFile f = gzopen(path.c_str(), "rb");   // gzip-compressed (data/scene) or plain
    auto rd = [&](void* p, unsigned n) { return gzread(f, p, n) == int(n); };
    char magic[8];
    std::uint32_t fh[2];
    if (!f || !rd(magic, 8) || std::memcmp(magic, "B3SCENE", 8) != 0 || !rd(fh, 8)) {
        std::fprintf(stderr, "%s: not a scene capture\n", path.c_str());
        return 1;
    }
    b3live::Mapping m;
    std::string why;
    if (!m.create(name, capacity, &why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 1; }
    b3live::Header* h = m.header();
    if (wait_reader > 0) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(wait_reader);
        while (h->want_key.load() == 0 && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::map<std::uint32_t, Page> pages;
    std::vector<std::uint8_t> rec, key;
    std::uint64_t polls = 0, dropped = 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::uint32_t head[4];
    while (rd(head, 16)) {
        rec.assign(reinterpret_cast<std::uint8_t*>(head), reinterpret_cast<std::uint8_t*>(head) + 16);
        bool ok = head[0] == 0x4B434954;
        for (std::uint32_t r = 0; ok && r < head[3]; ++r) {
            std::uint32_t run[2];
            if (!rd(run, 8)) { ok = false; break; }
            const std::size_t at = rec.size();
            rec.resize(at + 8 + run[1]);
            std::memcpy(rec.data() + at, run, 8);
            if (!rd(rec.data() + at + 8, run[1])) { ok = false; break; }
            for (std::uint32_t k = 0; k < run[1]; k += 16) {
                Page& pg = pages[(run[0] + k) & ~(kPage - 1)];
                const std::uint32_t off = (run[0] + k) & (kPage - 1);
                std::memcpy(pg.bytes + off, rec.data() + at + 8 + k, 16);
                pg.valid[off / 16] = 1;
            }
        }
        if (!ok) break;
        if (rate > 0) std::this_thread::sleep_until(t0 + std::chrono::duration<double>(double(polls) / rate));
        ++polls;
        h->polls.fetch_add(1, std::memory_order_relaxed);
        const bool want = h->key_at.load(std::memory_order_relaxed) == b3live::kNoKey || h->want_key.exchange(0) != 0;
        if (!want) { if (!b3live::publish(m, b3live::kDelta, rec.data(), rec.size())) ++dropped; continue; }
        key.assign(rec.begin(), rec.begin() + 16);
        std::uint32_t runs = 0, run_start = 0, run_end = 0;
        std::size_t run_at = 0;
        for (const auto& [base, pg] : pages) {
            for (std::uint32_t k = 0; k < kPage / 16; ++k) {
                if (!pg.valid[k]) continue;
                const std::uint32_t b = base + 16 * k;
                if (runs == 0 || b != run_end) { run_at = key.size(); run_start = b; put32(key, b); put32(key, 0); ++runs; }
                key.insert(key.end(), pg.bytes + 16 * k, pg.bytes + 16 * k + 16);
                run_end = b + 16;
                const std::uint32_t len = run_end - run_start;
                std::memcpy(key.data() + run_at + 4, &len, 4);
            }
        }
        std::memcpy(key.data() + 12, &runs, 4);
        if (!b3live::publish(m, b3live::kKeyframe, key.data(), key.size())) ++dropped;
        std::printf("[b3livefeed] poll %u: keyframe of %u runs, %zu bytes\n", head[1], runs, key.size());
        std::fflush(stdout);
    }
    gzclose(f);
    h->closed.store(1, std::memory_order_release);
    std::printf("[b3livefeed] %llu polls, %llu records (%llu keyframes), %llu dropped; reader at %llu of %llu, %llu resyncs\n",
                static_cast<unsigned long long>(polls), static_cast<unsigned long long>(h->records.load()),
                static_cast<unsigned long long>(h->keyframes.load()), static_cast<unsigned long long>(dropped),
                static_cast<unsigned long long>(h->reader_pos.load()), static_cast<unsigned long long>(h->head.load()),
                static_cast<unsigned long long>(h->reader_resyncs.load()));
    std::fflush(stdout);
    // keep the mapping for the reader to drain the tail (POSIX: unlinked when this process closes it)
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(linger);
    while (h->reader_pos.load() != h->head.load() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return 0;
}
