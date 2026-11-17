// (2026-11-04) per-block / per-event totals of a .b3scene capture over a poll range, to compare two captures
// of the same content (two builds: do the capture hooks still see everything?).
//   b3scene_stats <file.b3scene> [poll_from poll_to] [--hud-digest]
// --hud-digest: also one line "hud <poll> <digest>" per tick: FNV-1a 64 of the decoded HUD draws of the tick's frame
// (texture key, blend / sampler state, scissor, target, object / marker / video links, every vertex) and its object,
// marker, video, front-end car and globe counts; 0 for a tick without a frame. Two captures of the same content compare
// line by line.
// Prints one "name value" line per counter: ticks, vehicles (and those with corona inputs), props, crash ramps /
// pickups, animated materials, trail emitters and points, scrape emitters and segments, spark arrays and records,
// popcorn arrays, and, over the distinct HUD frames (shared between ticks while unchanged), draws, objects,
// components, markers (hud4), videos / front-end cars / globes (menu1); ticks with a front-end car.
#include "../../src/b3replay/capture.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
int main(int argc, char** argv) {
    bool digest = false;
    long dump = -1;   // --hud-dump <poll>: every draw of that tick's HUD frame, one line each (debugging a digest difference)
    if (argc >= 2 && std::string(argv[argc - 1]) == "--hud-digest") { digest = true; --argc; }
    if (argc >= 3 && std::string(argv[argc - 2]) == "--hud-dump") { dump = std::strtol(argv[argc - 1], nullptr, 0); argc -= 2; }
    if (argc != 2 && argc != 4) { std::fprintf(stderr, "usage: b3scene_stats <file.b3scene> [poll_from poll_to]\n"); return 2; }
    const std::uint32_t from = argc == 4 ? std::uint32_t(std::strtoul(argv[2], nullptr, 0)) : 0u;
    const std::uint32_t to = argc == 4 ? std::uint32_t(std::strtoul(argv[3], nullptr, 0)) : 0xFFFFFFFFu;
    b3replay::Capture cap;
    std::string why;
    if (!cap.load(argv[1], &why, false, from, to)) { std::fprintf(stderr, "load failed: %s\n", why.c_str()); return 1; }
    unsigned long long n[32] = {};
    enum { TICKS, VEH, VEH_CORONA, PROPS, RAMPS, PICKUPS, ANIM, TRAILS, TRAIL_PTS, SCRAPES, SCRAPE_SEGS, SPARK_ARR,
           SPARK_REC_BYTES, POPCORN_ARR, POPCORN_REC_BYTES, HUD_FRAMES, HUD_DRAWS, HUD_OBJ, HUD_COMP, HUD_MARK, HUD_VIDEO,
           HUD_FECAR, HUD_GLOBE, FE_CAR_TICKS, EFFECTS_VALID, CONTACT_TICKS, POST_VALID, COUNT };
    const char* name[COUNT] = {"ticks", "vehicles", "vehicles_with_corona_inputs", "props", "crash_ramps", "crash_pickups",
        "anim_materials", "trail_emitters", "trail_points", "scrape_emitters", "scrape_segments", "spark_arrays",
        "spark_record_bytes", "popcorn_arrays", "popcorn_record_bytes", "hud_frames", "hud_draws", "hud_objects",
        "hud_components", "hud_markers", "hud_videos", "hud_fe_cars", "hud_globes", "ticks_with_fe_car",
        "ticks_effects_valid", "ticks_contact_valid", "ticks_post_valid"};
    std::set<const void*> frames;
    for (const auto& t : cap.ticks) {
        if (t.poll < from || t.poll > to) continue;
        ++n[TICKS];
        n[VEH] += t.vehicles.size();
        for (const auto& v : t.vehicles) n[VEH_CORONA] += v.corona.valid;
        n[PROPS] += t.props.size(); n[RAMPS] += t.crash.ramps.size(); n[PICKUPS] += t.crash.pickups.size();
        n[ANIM] += t.anim.size();
        for (const auto& e : t.trails) if (e) { ++n[TRAILS]; n[TRAIL_PTS] += unsigned(e->count); }
        n[EFFECTS_VALID] += t.effects_valid;
        n[POST_VALID] += t.post.valid;   // post1's block
        n[CONTACT_TICKS] += t.contact.scrapes_valid || t.contact.sparks_valid || t.contact.popcorn_valid;
        for (const auto& s : t.contact.scrapes) if (s) { ++n[SCRAPES]; n[SCRAPE_SEGS] += unsigned(s->count); }
        for (const auto& s : t.contact.sparks) if (s) { ++n[SPARK_ARR]; for (auto& b : s->rec) for (auto& r : b) n[SPARK_REC_BYTES] += r.size(); }
        for (const auto& p : t.contact.popcorn) if (p) { ++n[POPCORN_ARR]; n[POPCORN_REC_BYTES] += p->rec.size(); }
        n[FE_CAR_TICKS] += t.fe_car != 0;
        if (t.hud && frames.insert(t.hud.get()).second) {
            ++n[HUD_FRAMES]; n[HUD_DRAWS] += t.hud->draws.size(); n[HUD_OBJ] += t.hud->objects.size();
            n[HUD_COMP] += t.hud->components.size(); n[HUD_MARK] += t.hud->markers.size(); n[HUD_VIDEO] += t.hud->videos.size();
            n[HUD_FECAR] += t.hud->fe_cars.size(); n[HUD_GLOBE] += t.hud->globes.size();
        }
    }
    for (int i = 0; i < COUNT; ++i) std::printf("%s %llu\n", name[i], n[i]);
    for (const auto& t : cap.ticks) {
        if (dump < 0 || long(t.poll) != dump || !t.hud) continue;
        std::printf("frame %u: %zu draws, %zu objects\n", t.poll, t.hud->draws.size(), t.hud->objects.size());
        for (std::size_t i = 0; i < t.hud->draws.size(); ++i) {
            const auto& d = t.hud->draws[i];
            std::printf("draw %zu tex %016llx a %02x fix %02x abe %d tcc %d lin %d dec %d ao %d rm %d wm %d%d sc %d,%d,%d,%d fbp %u fbw %u obj %d mk %d vid %d n %zu",
                        i, (unsigned long long)d.texture, d.alpha, d.fix, d.abe, d.tcc, d.linear, d.tfx_decal, d.alpha_only, d.rgb_masked, d.wms, d.wmt, d.scissor[0], d.scissor[1],
                        d.scissor[2], d.scissor[3], d.fbp, d.fbw, d.object, d.marker, d.video, d.tri.size());
            for (std::size_t k = 0; k < d.tri.size(); ++k)
                std::printf(" | %.3f,%.3f %.4f,%.4f %02x%02x%02x%02x z%u", d.tri[k].x, d.tri[k].y, d.tri[k].u, d.tri[k].v,
                            d.tri[k].rgba[0], d.tri[k].rgba[1], d.tri[k].rgba[2], d.tri[k].rgba[3], d.tri[k].z);
            std::printf("\n");
        }
    }
    if (digest) {
        auto fnv = [](std::uint64_t h, const void* p, std::size_t k) {
            const auto* b = static_cast<const unsigned char*>(p);
            for (std::size_t i = 0; i < k; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
            return h;
        };
        for (const auto& t : cap.ticks) {
            if (t.poll < from || t.poll > to) continue;
            std::uint64_t h = 0;
            if (t.hud) {
                h = 1469598103934665603ULL;
                for (const auto& d : t.hud->draws) {
                    h = fnv(h, &d.texture, sizeof d.texture);
                    const unsigned char st[] = {d.alpha, d.fix, d.abe, d.alpha_only, d.rgb_masked, d.linear, d.tfx_decal, d.tcc, d.wms, d.wmt};
                    h = fnv(h, st, sizeof st); h = fnv(h, d.scissor, sizeof d.scissor);
                    h = fnv(h, &d.fbp, 4); h = fnv(h, &d.fbw, 4);
                    h = fnv(h, &d.object, 4); h = fnv(h, &d.marker, 4); h = fnv(h, &d.video, 4);
                    if (!d.tri.empty()) h = fnv(h, d.tri.data(), d.tri.size() * sizeof d.tri[0]);
                }
                const std::size_t c[5] = {t.hud->objects.size(), t.hud->markers.size(), t.hud->videos.size(),
                                          t.hud->fe_cars.size(), t.hud->globes.size()};
                h = fnv(h, c, sizeof c);
            }
            std::printf("hud %u %016llx\n", t.poll, static_cast<unsigned long long>(h));
        }
    }
    return 0;
}
