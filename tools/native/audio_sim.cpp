// tools/native/audio_sim.cpp (audioq, 2026-11-22): a captured game mix (B3NATIVE_AUDIO_RAW: raw s16 stereo 48 kHz)
// played through the sound's output stage on a simulated timeline, the old drift guard against the output queue
// (src/native/audio_queue.h), each into a WAV, with counts of what each did to the sound.
//
//   audio_sim <mix.raw> <out_dir> [seconds = 20]
//
// The timeline (the host's, against the device's clock): the game pushes 800 frames a VBlank at 59.94 Hz with up to
// 4 ms of jitter (a Windows sleep); at 6 s the game thread stalls 120 ms and then catches up (the missed VBlanks pushed
// at once); at 12 s a 300 ms stall (a load's hitch, the window's focus hold). The device pulls 480 frames every 10 ms
// (WASAPI's shared-mode period). Old: the SDL stream as the queue, regulate()'s 800-frame drops above target + band
// and silence padded to the cushion after an underrun (the code before audioq). New: the output queue pulled through
// an SDL stream at the queue's frequency ratio (as AudioDevice does). Counted in each output: inserted silences (runs
// of 64+ zero frames where the mix had sound), hard cuts (a step over 6000 between two samples that the mix does not
// have at that place), and the frames dropped. The WAVs hold the whole run (short: the given seconds).
#include "../../src/native/audio_queue.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

void write_wav(const std::string& path, const std::vector<int16_t>& lr) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "audio_sim: cannot write %s\n", path.c_str()); return; }
    const uint32_t data = uint32_t(lr.size() * 2), rate = 48000;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(data);
    std::fwrite(lr.data(), 2, lr.size(), f);
    std::fclose(f);
}

struct Stats { int silences = 0, cuts = 0; };
// what an output did to the sound: inserted silences and hard cuts (steps the mix never has)
Stats measure(const std::vector<int16_t>& out, int mix_max_step) {
    Stats s;
    int zero_run = 0;
    bool had_sound = false;
    for (size_t k = 0; k + 1 < out.size(); k += 2) {
        const bool zero = out[k] == 0 && out[k + 1] == 0;
        if (zero) ++zero_run;
        else {
            if (zero_run >= 64 && had_sound) ++s.silences;
            zero_run = 0;
            had_sound = true;
        }
        if (k >= 2) {
            const int step = std::max(std::abs(int(out[k]) - int(out[k - 2])), std::abs(int(out[k + 1]) - int(out[k - 1])));
            if (step > std::max(6000, mix_max_step + 2000)) ++s.cuts;
        }
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: audio_sim <mix.raw> <out_dir> [seconds]\n"); return 2; }
    const double seconds = argc > 3 ? std::atof(argv[3]) : 20.0;
    std::vector<int16_t> mix;
    {
        std::FILE* f = std::fopen(argv[1], "rb");
        if (!f) { std::fprintf(stderr, "audio_sim: cannot read %s\n", argv[1]); return 1; }
        int16_t b[4096];
        for (size_t n; (n = std::fread(b, 2, 4096, f)) > 0;) mix.insert(mix.end(), b, b + n);
        std::fclose(f);
    }
    const size_t mix_frames = mix.size() / 2;
    int mix_step = 0;
    for (size_t k = 2; k < mix.size(); ++k) mix_step = std::max(mix_step, std::abs(int(mix[k]) - int(mix[k - 2])));
    std::printf("audio_sim: %zu frames of mix (%.1f s), its largest step %d\n", mix_frames, mix_frames / 48000.0, mix_step);
    if (!SDL_Init(0)) { std::fprintf(stderr, "audio_sim: SDL_Init: %s\n", SDL_GetError()); return 1; }
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, 48000};

    // the timeline: when each VBlank's 800 frames are pushed (seconds on the device's clock)
    std::vector<double> push_at;
    {
        std::mt19937 rng(1);
        std::uniform_real_distribution<double> jitter(0.0, 0.004);
        double t = 0.05;
        const double vb = 1.0 / 59.94;
        for (size_t v = 0; (v + 1) * 800 <= mix_frames && t < seconds; ++v) {
            double at = t + jitter(rng);
            if (t >= 6.0 && t < 6.12) at = 6.12;   // a 120 ms stall, then the missed VBlanks at once
            if (t >= 12.0 && t < 12.3) at = 12.3;  // a 300 ms stall
            push_at.push_back(at);
            t += vb;
        }
    }

    for (int pipeline = 0; pipeline < 2; ++pipeline) {
        SDL_AudioStream* s = SDL_CreateAudioStream(&spec, &spec);
        b3native::OutputQueue q;
        std::vector<int16_t> out, block(480 * 2);
        uint64_t dropped = 0, padded = 0;
        std::vector<size_t> edges;   // output frames where the old guard spliced (a drop, a pad's start or end)
        double next_print = 1.0;
        size_t next = 0, vblank = 0;
        size_t before_push = 0;
        for (double dev = 0; dev < seconds; dev += 0.010) {   // the device's 10 ms periods
            while (next < push_at.size() && push_at[next] <= dev) {   // the game's pushes up to now
                const int16_t* lr = mix.data() + 2 * 800 * vblank;
                ++vblank;
                ++next;
                if (pipeline == 0) {   // old: SDL's queue and regulate(2880, 4800)
                    before_push = size_t(SDL_GetAudioStreamQueued(s) / 4);
                    SDL_PutAudioStreamData(s, lr, 800 * 4);
                    const size_t qf = size_t(SDL_GetAudioStreamQueued(s) / 4), target = 2880, band = 4800;
                    if (qf > target + band) {
                        std::vector<int16_t> sink(800 * 2);
                        for (size_t left = qf - target; left >= 800; left -= 800) {
                            if (SDL_GetAudioStreamData(s, sink.data(), 800 * 4) <= 0) break;
                            dropped += 800;
                        }
                        edges.push_back(out.size() / 2 + size_t(SDL_GetAudioStreamQueued(s) / 4) - 800);   // (where the splice plays)
                    } else if (before_push == 0) {
                        static const std::vector<int16_t> silence(800 * 2, 0);
                        edges.push_back(out.size() / 2 + size_t(SDL_GetAudioStreamQueued(s) / 4) - 800);   // the pad's start
                        for (size_t have = qf; have < target; have += 800) { SDL_PutAudioStreamData(s, silence.data(), 800 * 4); padded += 800; }
                    }
                } else {
                    q.push(lr, 800);
                    SDL_SetAudioStreamFrequencyRatio(s, float(q.ratio()));
                }
            }
            if (pipeline == 1) {   // the device's get-callback: the queue's frames until the stream has a period
                std::vector<int16_t> chunk(64 * 2);
                while (SDL_GetAudioStreamAvailable(s) < 480 * 4) {
                    q.pull(chunk.data(), 64);
                    SDL_PutAudioStreamData(s, chunk.data(), 64 * 4);
                }
            }
            const int got = SDL_GetAudioStreamData(s, block.data(), 480 * 4);
            const size_t gf = got > 0 ? size_t(got) / 4 : 0;
            std::fill(block.begin() + 2 * gf, block.end(), int16_t(0));   // a starved device plays silence
            if (pipeline == 0 && gf < 480 && dev > 0.2) padded += 480 - gf;
            out.insert(out.end(), block.begin(), block.end());
            if (pipeline == 1 && dev >= next_print) {
                if (std::getenv("AUDIO_SIM_TRACE")) std::printf("  t %.0f s: level %zu, ratio %.5f\n", dev, q.level(), q.ratio());
                next_print += 1.0;
            }
        }
        {   // the sound's level at the edges of every silence in the output (a cut: high; a fade: near 0)
            int worst = 0, n = 0;
            for (size_t k = 1; k + 1 < out.size() / 2; ++k) {
                const bool z0 = out[2 * (k - 1)] == 0 && out[2 * (k - 1) + 1] == 0, z1 = out[2 * k] == 0 && out[2 * k + 1] == 0;
                if (z0 == z1) continue;
                // an edge between sound and a silence of 64+ frames
                size_t run = 0;
                for (size_t j = z1 ? k : k - 1; j < out.size() / 2 && out[2 * j] == 0 && out[2 * j + 1] == 0 && run < 64; j = z1 ? j + 1 : j - 1) { ++run; if (j == 0) break; }
                if (run < 64) continue;
                const size_t sk = z1 ? k - 1 : k;
                const int a = std::max(std::abs(int(out[2 * sk])), std::abs(int(out[2 * sk + 1])));
                worst = std::max(worst, a);
                ++n;
            }
            std::printf("audio_sim:   %d sound / silence edges, the sound at the worst of them %d (a cut is the signal's own level; a fade ends near 0)\n", n, worst);
        }
        const Stats st = measure(out, mix_step);
        const char* name = pipeline == 0 ? "before_drift_guard" : "after_output_queue";
        std::printf("audio_sim: %-20s %zu frames out; inserted silences %d, hard cuts %d; dropped %llu frames, silence %llu frames%s\n", name,
                    out.size() / 2, st.silences, st.cuts, static_cast<unsigned long long>(pipeline == 0 ? dropped : q.dropped()),
                    static_cast<unsigned long long>(pipeline == 0 ? padded : q.padded()),
                    pipeline == 1 ? (" (late " + std::to_string(q.underruns()) + " times, ratio " + std::to_string(q.ratio()) + ")").c_str() : "");
        write_wav(std::string(argv[2]) + "/" + name + ".wav", out);
        SDL_DestroyAudioStream(s);
    }
    SDL_Quit();
    return 0;
}
