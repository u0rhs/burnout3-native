// font1: the live feed's resync rules (include/b3live/ring.h, b3replay --live: src/b3replay/live.cpp LiveFeed::drain)
// keep every texture across a lapped reader and a late join. data/scene/race2.b3scene is played into a real b3live
// ring (POSIX shared memory) by the writer's rules (src/gameview/scene_capture.cpp live_publish, as
// tools/render/b3livefeed.cpp): a delta per poll, a keyframe (every block of the shadow image) when the ring has none
// or the reader asked. Readers: one lapped on purpose after the menus' textures (the HUD font among them) were first
// grabbed, one joining after them. At poll 2000 (Select Car, after the lap: race2 writes 5.4 MB between polls 200
// and 2000) each must decode the HUD frame the file reader decodes:
// the same draws, every texture decoded with the same pixels.
#include <cstdlib>
#include "test.h"

#include <unistd.h>
#include <zlib.h>

#include <map>
#include <string>
#include <vector>

#include "b3live/ring.h"
#include "../src/b3replay/capture.h"
#include "../src/b3replay/hud.h"

namespace {

// a local capture (not in git: game-derived): $B3_TEST_CAPTURE, else data/scene/race2.b3scene; skipped when absent
const char* const kCapture = [] { const char* e = std::getenv("B3_TEST_CAPTURE"); return e && *e ? e : "data/scene/race2.b3scene"; }();
constexpr std::uint32_t kPoll = 2000;        // Select Car: small-font car name, stat labels and button labels
constexpr std::uint32_t kLastPoll = 2020;
constexpr std::uint64_t kCapacity = 4u << 20;   // race2's keyframes stay under 2 MB (half the ring) up to kLastPoll (1.5 MB)

std::vector<std::vector<std::uint8_t>> read_records(std::uint32_t last_poll) {
    std::vector<std::vector<std::uint8_t>> out;
    gzFile f = gzopen(kCapture, "rb");
    if (!f) return out;
    auto rd = [&](void* p, unsigned n) { return gzread(f, p, n) == int(n); };
    char magic[8];
    std::uint32_t fh[2], head[4];
    if (!rd(magic, 8) || std::memcmp(magic, "B3SCENE", 8) != 0 || !rd(fh, 8)) { gzclose(f); return out; }
    while (rd(head, 16) && head[0] == 0x4B434954 && head[1] <= last_poll) {
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

// The writer: scene_capture.cpp's shadow image and live_publish
struct Writer {
    struct Page { std::uint8_t bytes[0x1000]; std::uint8_t valid[0x100]; };
    std::map<std::uint32_t, Page> pages;
    b3live::Mapping m;
    std::uint64_t keyframes = 0, dropped = 0;
    void poll(const std::vector<std::uint8_t>& rec) {
        std::uint32_t head[4];
        std::memcpy(head, rec.data(), 16);
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
        b3live::Header* h = m.header();
        const bool key = h->key_at.load() == b3live::kNoKey || h->want_key.exchange(0) != 0;
        if (!key) { if (!b3live::publish(m, b3live::kDelta, rec.data(), rec.size())) ++dropped; return; }
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
        if (b3live::publish(m, b3live::kKeyframe, body.data(), body.size())) ++keyframes;
        else ++dropped;
    }
};

// The reader: LiveFeed::drain's rules (open at the latest keyframe or ask for one; a lap asks for one; a keyframe
// clears the image). Only the model and HUD tables live across resyncs (LiveFeed::cap_), as in the viewer.
struct Reader {
    b3live::Mapping m;
    bool open = false, synced = false;
    std::uint64_t pos = 0, request_at = 0, resyncs = 0;
    b3replay::Image image;
    b3replay::Capture cap;
    b3replay::Tick at_poll;
    bool have = false;
    std::uint32_t first_poll = 0;   // the poll of the first record taken (its keyframe)
    void drain(const std::string& name) {
        if (!open) {
            if (!m.open(name, nullptr)) return;
            open = true;
            b3live::Header* h = m.header();
            const std::uint64_t head = h->head.load(), key = h->key_at.load();
            if (key != b3live::kNoKey && head - key <= h->capacity) { pos = key; synced = true; image.clear(); }
            else { request_at = head; h->want_key.store(1); }
        }
        b3live::Header* h = m.header();
        if (!synced) {
            const std::uint64_t key = h->key_at.load();
            if (key == b3live::kNoKey || key < request_at) return;
            pos = key;
            synced = true;
            image.clear();
        }
        b3live::RecordHead rh{};
        std::string body;
        while (true) {
            const int r = b3live::next_record(m, pos, rh, body);
            if (r == 0) break;
            if (r < 0) { ++resyncs; request_at = h->head.load(); h->want_key.store(1); synced = false; break; }
            if (rh.kind == b3live::kKeyframe) image.clear();
            b3replay::Tick t;
            if (!cap.apply_record(image, reinterpret_cast<const std::uint8_t*>(body.data()), body.size(), t)) { ++resyncs; synced = false; break; }
            if (!first_poll) first_poll = t.poll;
            if (t.poll == kPoll) { at_poll = t; have = true; }
        }
    }
};

// The live tick's HUD frame against the file reader's: same frame, same draws, every texture decoded alike. Returns
// how many of the draws use the HUD font (GlobalFont).
int compare(const b3replay::Tick& a, const b3replay::Capture& ca, const b3replay::Tick& b, const b3replay::Capture& cb) {
    CHECK(a.hud && b.hud);
    if (!a.hud || !b.hud) return 0;
    CHECK(a.hud->serial == b.hud->serial);
    CHECK(a.hud->draws.size() == b.hud->draws.size());
    CHECK(a.hud->stats.unbound_texture == 0 && b.hud->stats.unbound_texture == 0);
    int font = 0;
    for (std::size_t k = 0; k < std::min(a.hud->draws.size(), b.hud->draws.size()); ++k) {
        const b3replay::HudDraw &x = a.hud->draws[k], &y = b.hud->draws[k];
        CHECK(x.texture == y.texture && x.tri.size() == y.tri.size());
        if (!x.texture || x.texture != y.texture) continue;
        const auto tx = ca.hud.textures.find(x.texture), ty = cb.hud.textures.find(y.texture);
        CHECK(tx != ca.hud.textures.end() && ty != cb.hud.textures.end());
        if (tx == ca.hud.textures.end() || ty == cb.hud.textures.end()) continue;
        CHECK(tx->second->ok == ty->second->ok);
        CHECK(tx->second->rgba == ty->second->rgba);
        if (tx->second->name == "GlobalFont") { CHECK(tx->second->ok); ++font; }
    }
    return font;
}

}  // namespace

TEST(live_lap_and_late_join_keep_the_hud_font) {
    const auto recs = read_records(kLastPoll);
    if (recs.size() < kLastPoll - 10) { std::printf("  (skipped: %s not readable)\n", kCapture); return; }
    // the file reader
    b3replay::Image fim;
    b3replay::Capture fcap;
    b3replay::Tick ftick;
    for (const auto& r : recs) {
        b3replay::Tick t;
        CHECK(fcap.apply_record(fim, r.data(), r.size(), t));
        if (t.poll == kPoll) ftick = t;
    }
    CHECK(ftick.poll == kPoll && ftick.hud);
    const std::string name = "b3test-livelap-" + std::to_string(getpid());
    Writer w;
    std::string why;
    CHECK(w.m.create(name, kCapacity, &why));
    if (!w.m.header()) return;
    Reader lapped, late;
    std::size_t font_textures_before_lap = 0;
    bool stalled = false, lapped_once = false;
    for (const auto& r : recs) {
        std::uint32_t poll;
        std::memcpy(&poll, r.data() + 4, 4);
        w.poll(r);
        // the lapped reader stalls from poll 200 (the menus' textures, the font's included, are in its tables by
        // then) until the writer is a whole ring ahead, then reads on: a lap, a keyframe request, a resync
        if (poll == 200) {
            stalled = true;
            for (const auto& [key, t] : lapped.cap.hud.textures) font_textures_before_lap += t->name == "GlobalFont" && t->ok;
        }
        if (stalled && w.m.header()->head.load() - lapped.pos > kCapacity) { stalled = false; lapped_once = true; }
        if (!stalled) lapped.drain(name);
        // the late reader opens the ring only after the font was grabbed (it starts at the latest keyframe, the lapped
        // reader's resync, or asks for one)
        if (poll >= 1900) late.drain(name);
    }
    lapped.drain(name);
    late.drain(name);
    CHECK(w.dropped == 0);
    CHECK(lapped_once && lapped.resyncs == 1);
    CHECK(font_textures_before_lap >= 1);
    CHECK(w.keyframes >= 2);   // the first poll, the lapped reader's resync (and the late reader's join, if asked)
    CHECK(late.first_poll > 200);   // the late reader's image never held the menus' first grabs
    CHECK(lapped.have && late.have);
    if (lapped.have) CHECK(compare(lapped.at_poll, lapped.cap, ftick, fcap) > 0);
    if (late.have) CHECK(compare(late.at_poll, late.cap, ftick, fcap) > 0);
}

TEST_MAIN("test_livelap")
