// gameview_replay (pic1, docs/native-renderer.md §32): a scene capture played back through the game view's capture
// code, as a backend would drive it: the plumbing proof before the native boot reaches frames.
//
//   gameview_replay <capture.b3scene[.gz]> --out <file> [--polls N] [--speed S] [--render <libb3render.so | b3render.dll>
//                   --render-args "<b3replay live options>"]
//   gameview_replay --list-entries      the function entries the HUD capture hooks (one address a line)
//
// --speed S paces the polls by their VBlank numbers at S x real time (59.94 VBlanks a second), as a backend's frame
// scheduler releases them; the live renderer follows arrival times (its segments, display clock and the last 20 s it
// keeps), so shots need a paced feed. 0 (default): as fast as possible.
//
// The capture's records are applied, poll by poll, to a 32 MB arena at retail addresses: after record k the arena holds
// the bytes the original hook read at poll k (§12.1: a reader that applies every run up to poll k holds exactly them).
// Then b3gv::on_poll runs on the arena with a plain RAM view (b3gv::Memory, as the native backend passes its arena), so
// the scene capture re-reads the same addresses through the same pointers and writes its own record (B3PORT_SCENE_OUT,
// and B3PORT_RENDER: the in-process renderer, through the same C ABI as in native_play).
//
// The HUD part of a record comes from function-entry observations, which a file cannot replay. It is always the record's
// tail (b3hud_publish runs after every scene grab), so at the HUD's publish point the original record's remaining runs
// are passed through (b3gv::set_hud_publish_override), each as one host grab against the same shadow image. Checks:
//   - per poll, the re-captured scene runs equal the original record's leading runs byte for byte;
//   - the whole output file equals the input file (cmp): every record, the HUD tail included, so the delta writer's
//     shadow image matched the original's at every poll.
// With --render the renderer gets the same records; its --verify <capture> compares each tick it decodes with the file
// reader's tick of that poll, and --shots writes pictures to compare with b3replay --offscreen on the original.
// Reads the capture only; writes only --out (and what the renderer writes).
#include "b3gameview/gameview.h"

#include <zlib.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

std::uint32_t le32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }

// The capture reads its switches from the environment (as in native_play); mingw's C runtime has no setenv.
void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

struct Run { std::uint32_t addr, len; const std::uint8_t* bytes; };
struct Record {
    std::uint32_t poll = 0, vblank = 0;
    std::vector<Run> runs;
    const std::uint8_t* body = nullptr;   // after the 16-byte head
    std::size_t body_bytes = 0;
};

struct Replay {
    Record cur;
    std::uint64_t polls = 0, scene_same = 0, scene_differ = 0, passed_runs = 0;
    std::uint64_t first_differ = ~0ull;
};
Replay g;

// At the HUD's publish point: check the scene part against the original record's leading runs, then pass the rest
// through in order.
void pass_through(void*) {
    std::size_t bytes = 0;
    std::uint32_t runs = 0;
    const std::uint8_t* so_far = b3gv::record_so_far(&bytes, &runs);
    const bool same = runs <= g.cur.runs.size() && bytes <= g.cur.body_bytes && std::memcmp(so_far, g.cur.body, bytes) == 0 &&
                      (runs == g.cur.runs.size() ? bytes == g.cur.body_bytes
                                                 : g.cur.runs[runs].bytes - 8 == g.cur.body + bytes);
    if (same) ++g.scene_same;
    else {
        ++g.scene_differ;
        if (g.first_differ == ~0ull) {
            g.first_differ = g.cur.poll;
            std::fprintf(stderr, "gameview_replay: poll %u: the scene part differs (%u runs / %zu bytes re-captured; the original has %zu runs / %zu bytes)\n",
                         g.cur.poll, runs, bytes, g.cur.runs.size(), g.cur.body_bytes);
            if (std::getenv("GAMEVIEW_REPLAY_DEBUG")) {   // both run lists of the first differing poll
                for (std::size_t j = 0; j < g.cur.runs.size(); ++j)
                    std::fprintf(stderr, "  original run %zu: %08X +%u\n", j, g.cur.runs[j].addr, g.cur.runs[j].len);
                for (std::size_t off = 0, j = 0; off + 8 <= bytes; ++j) {
                    const std::uint32_t ra = le32(so_far + off), rn = le32(so_far + off + 4);
                    std::fprintf(stderr, "  re-captured run %zu: %08X +%u%s\n", j, ra, rn,
                                 j < g.cur.runs.size() && (ra != g.cur.runs[j].addr || rn != g.cur.runs[j].len || std::memcmp(so_far + off + 8, g.cur.runs[j].bytes, rn) != 0) ? "  <- differs" : "");
                    off += 8 + rn;
                }
            }
        }
        return;   // the output then differs too (cmp below)
    }
    for (std::size_t j = runs; j < g.cur.runs.size(); ++j) {
        const Run& r = g.cur.runs[j];
        b3scene_grab_host(r.addr, r.bytes, r.len);
        ++g.passed_runs;
    }
}

// A whole file, gzip-compressed or not (zlib reads plain files as they are; data/scene's captures are gzip -9).
bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    gzFile f = gzopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    std::vector<std::uint8_t> chunk(1 << 20);
    int n;
    while ((n = gzread(f, chunk.data(), unsigned(chunk.size()))) > 0) out.insert(out.end(), chunk.begin(), chunk.begin() + n);
    const bool ok = n == 0;
    gzclose(f);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    std::string in, out, render, render_args;
    std::uint64_t max_polls = 0;
    double speed = 0;
    for (int k = 1; k < argc; ++k) {
        const std::string s = argv[k];
        if (s == "--out" && k + 1 < argc) out = argv[++k];
        else if (s == "--polls" && k + 1 < argc) max_polls = std::strtoull(argv[++k], nullptr, 10);
        else if (s == "--speed" && k + 1 < argc) speed = std::atof(argv[++k]);
        else if (s == "--list-entries") {   // the HUD's hook points under the current switches (tools/native/watch_census.py)
            set_env("B3PORT_SCENE_OUT", "-");   // an output is configured, so the list is the one a capture run hooks
            std::size_t n = 0;
            const std::uint32_t* pcs = b3gv::entry_points(&n);
            for (std::size_t j = 0; j < n; ++j) std::printf("0x%08X\n", pcs[j]);
            return 0;
        }
        else if (s == "--render" && k + 1 < argc) render = argv[++k];
        else if (s == "--render-args" && k + 1 < argc) render_args = argv[++k];
        else if (in.empty() && s[0] != '-') in = s;
        else { std::fprintf(stderr, "gameview_replay: unknown argument %s\n", s.c_str()); return 2; }
    }
    if (in.empty() || out.empty()) {
        std::fprintf(stderr, "usage: gameview_replay <capture.b3scene[.gz]> --out <file> [--polls N] [--speed S] [--render <lib> --render-args \"...\"]\n");
        return 2;
    }
    std::vector<std::uint8_t> file;
    if (!read_file(in, file) || file.size() < 16 || std::memcmp(file.data(), "B3SCENE", 8) != 0) {
        std::fprintf(stderr, "gameview_replay: %s is not a scene capture\n", in.c_str());
        return 2;
    }
    // the capture's outputs, before anything reads the environment (the scene capture reads it once)
    set_env("B3PORT_SCENE_OUT", out.c_str());
    if (!render.empty()) {
        set_env("B3PORT_RENDER", render.c_str());
        set_env("B3PORT_RENDER_ARGS", render_args.c_str());
    } else {
        set_env("B3PORT_RENDER", nullptr);
    }
    set_env("B3PORT_SCENE_LIVE", nullptr);
    b3gv::set_host(b3gv::Host{});   // no schedule stamp (the renderer uses arrival times), no extra publishers
    b3gv::set_hud_publish_override(pass_through, nullptr);

    std::vector<std::uint8_t> arena(b3gv::kRamEnd, 0);
    b3gv::Memory mem;
    mem.ram = arena.data();
    mem.ram_size = b3gv::kRamEnd;

    std::size_t at = 16;
    std::uint64_t host_runs = 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::uint32_t vb0 = 0;
    while (at + 16 <= file.size()) {
        if (le32(&file[at]) != 0x4B434954U) { std::fprintf(stderr, "gameview_replay: bad record at byte %zu\n", at); return 1; }
        Record& r = g.cur;
        r.poll = le32(&file[at + 4]);
        r.vblank = le32(&file[at + 8]);
        const std::uint32_t nruns = le32(&file[at + 12]);
        at += 16;
        r.body = &file[at];
        r.runs.clear();
        for (std::uint32_t j = 0; j < nruns; ++j) {
            if (at + 8 > file.size()) { std::fprintf(stderr, "gameview_replay: truncated record (poll %u)\n", r.poll); return 1; }
            const std::uint32_t a = le32(&file[at]), n = le32(&file[at + 4]);
            if (at + 8 + n > file.size()) { std::fprintf(stderr, "gameview_replay: truncated run (poll %u)\n", r.poll); return 1; }
            r.runs.push_back({a, n, &file[at + 8]});
            if (a < b3gv::kRamEnd && n <= b3gv::kRamEnd - a) std::memcpy(arena.data() + a, &file[at + 8], n);
            else ++host_runs;   // host-only regions (HUD, movie, host UI, settings): passed through only
            at += 8 + n;
        }
        r.body_bytes = std::size_t(&file[0] + at - r.body);
        // The one value the capture derives instead of recording (fx1, scene_capture.cpp grab_effects): each listed
        // skid-mark emitter's +0x8 (func_002BD560 swaps +0x8 / +0xC every rendered frame; the emitter is captured only
        // when another field changed), which the hook reads to find the point data. Set here as the reader derives it
        // (src/b3replay/capture.cpp: trail system + (half +0x14634 & 1) x 0x9800 + slot x 0x130, the address bits only),
        // and put back after the poll, so the arena stays the captured image for the delta writer.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> patched;
        {
            constexpr std::uint32_t kTrails = 0x1D9D200U + 0x2600U;
            auto r32 = [&](std::uint32_t a) { return le32(arena.data() + a); };
            const std::uint32_t half = r32(kTrails + 0x14634U) & 1U;
            for (std::uint32_t t = 0; t < 5; ++t) {
                const std::uint32_t list = kTrails + 0x13C0CU + 0x208U * t, n = r32(list + 0x200);
                if (n == 0 || n > 0x80) continue;
                for (std::uint32_t j = 0; j < n; ++j) {
                    const std::uint32_t e = r32(list + 4 * j);
                    if (e < kTrails + 0x13000U || e + 0x14 > b3gv::kRamEnd) continue;
                    const std::uint32_t slot = (e - (kTrails + 0x13000U)) / 0x14U;
                    if (slot >= 0x80) continue;
                    const std::uint32_t old = r32(e + 8), d = (old & 0x30000000U) | (kTrails + half * 0x9800U + slot * 0x130U);
                    if (d == old) continue;
                    patched.emplace_back(e + 8, old);
                    std::memcpy(arena.data() + e + 8, &d, 4);
                }
            }
        }
        if (speed > 0) {   // the poll's VBlank at S x real time
            if (g.polls == 0) vb0 = r.vblank;
            const double s = double(r.vblank - vb0) / 59.94 / speed;
            std::this_thread::sleep_until(t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(s)));
        }
        b3gv::on_poll(r.poll, r.vblank, mem);
        for (auto it = patched.rbegin(); it != patched.rend(); ++it) std::memcpy(arena.data() + it->first, &it->second, 4);
        ++g.polls;
        if (max_polls && g.polls >= max_polls) break;
    }
    b3gv::finish();

    std::vector<std::uint8_t> back;
    if (!read_file(out, back)) { std::fprintf(stderr, "gameview_replay: cannot read back %s\n", out.c_str()); return 1; }
    const std::size_t want = max_polls ? at : file.size();
    std::size_t diff = 0;
    while (diff < back.size() && diff < want && back[diff] == file[diff]) ++diff;
    const bool identical = back.size() == want && diff == want;
    std::printf("gameview_replay: %llu polls; scene part: %llu identical, %llu differ%s; %llu runs passed through (HUD tail; %llu host-region runs in the input)\n",
                (unsigned long long)g.polls, (unsigned long long)g.scene_same, (unsigned long long)g.scene_differ,
                g.first_differ != ~0ull ? (" (first at poll " + std::to_string(g.first_differ) + ")").c_str() : "",
                (unsigned long long)g.passed_runs, (unsigned long long)host_runs);
    if (identical) std::printf("gameview_replay: output IDENTICAL to the input (%zu bytes)\n", want);
    else std::printf("gameview_replay: output DIFFERS from the input: %zu vs %zu bytes, first difference at byte %zu\n", back.size(), want, diff);
    return identical && g.scene_differ == 0 ? 0 : 1;
}
