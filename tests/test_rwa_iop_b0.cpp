// Test B0 (docs/audio.md §4): the native RWA IOP executor (src/audio/rwa_iop.cpp, stub mixer) replays a logged RW
// Audio command stream and is compared with the log's recorded replies and reply areas.
//
//   build/test_rwa_iop_b0 [log] [game dir] [--no-spurious-0d] [--verbose] [--real-mixer]
//   (--real-mixer: the SPU2-equivalent mixer, spu2_mixer_impl.h, instead of the stub)
//   --wav <file> [--wav-from <s>] [--wav-seconds <s>]: with --real-mixer, the mixer's 48 kHz output of the replay as a
//   WAV (game audio: write it outside the repo, e.g. %B3_WIN%\audio1\). Reverb needs the game's
//   IOP/LIBSD.IRX, loaded from the game dir.
//   --spu2-trace <file>: test B, the executor's voice-level writes (through the Spu2Mixer interface) reduced to the event
//   kinds of the recorded SPU2 trace, keyed by the snapshot index (the
//   n-th opcode-0x14 command fed, 1-based, as the trace's snap column): one line per event,
//     <snap> <poll> <t> KON core voice ssa lsa pitch voll volr adsr1 adsr2   (the voice's registers at the KON write;
//                                         lsa = the first LSAX written to that voice after it, '-' if none; that
//                                         LSAX write is not listed again as an LSAX line)
//     <snap> <poll> <t> KOFF core mask    (one line per voice bit; all-zero KOFF / KON writes are left out)
//     <snap> <poll> <t> PITCH|LSAX|VOLL|VOLR core voice value
//   (VOLL / VOLR: every write, sweep register values too).
//   defaults: $B3_SIF_LOG or build/siflog/navproof14s1.txt (a recorded SIF log),
//             $B3_GAME_DIR or data/game
// Skips (exit 0) when the log is absent. Log format: a SIF log (the S / U / P lines below).
//
// Replay:
//   - S lines with SIF command 0 go to RwaIop::send_cmd in log order; their payload (all <= 0x1000 bytes in the
//     log, written in full) is first stored in a sparse EE memory at the packet's source address, which the fake
//     platform's read_ee serves. Other SIF commands (0x8000000x: SIF RPC of other modules) are skipped.
//   - Time (the documented rule): the poll column counts the game's 60 Hz pad polls; each poll is 800 samples
//     (48 kHz / 60). Within a poll, and before the first nonzero poll (the log's start-up, poll 0), time follows
//     the step column at the run's mean steps per poll (measured in a first pass), capped at 799 samples into a
//     poll. The executor is advanced to each line's time before the line is handled.
//   - Files: request names (e.g. `tracks\_EATrax1.rws`) are resolved under the game dir, `\` as a separator,
//     each component matched case-insensitively (the install has upper-case names); a device prefix up to ':' is
//     dropped. RWA.IRX's own heap is the executor's arena (RwaIopOptions::heap_base 0x75700, the log's first block).
//     image), so the DMA work area matches the log's.
// Compared:
//   - replies, in order: the log's U lines (user handler) and the system replies among its P lines (9 .. 0x10),
//     against the executor's reply packets: opcode, p5 / thread, arg0, arg1 (all, and the subset the EE's handler
//     uses); a greedy alignment with lookahead reports missing / extra ones by opcode; timing as differences in
//     commands and samples;
//   - each A line (the reply area before 0x14 number k + 1) against the 0x680 bytes the executor sent with its
//     reply to 0x14 number k: PlayState and PacketID exact, LoopStartAddress, CurrentPosition as a distribution.
#include "../src/audio/spu2_mixer_impl.h"
#include "../src/audio/spu2_stub_mixer.h"
#include "../src/native/rwa_iop.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> unhex(const std::string& h) {
    std::vector<uint8_t> v(h.size() / 2);
    for (size_t k = 0; k < v.size(); ++k) v[k] = static_cast<uint8_t>(std::stoul(h.substr(2 * k, 2), nullptr, 16));
    return v;
}
std::array<uint32_t, 12> words(const std::vector<uint8_t>& b) {
    std::array<uint32_t, 12> w{};
    for (int k = 0; k < 12 && 4 * k + 3 < static_cast<int>(b.size()); ++k)
        w[k] = b[4 * k] | b[4 * k + 1] << 8 | b[4 * k + 2] << 16 | static_cast<uint32_t>(b[4 * k + 3]) << 24;
    return w;
}
uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

struct Reply {
    uint32_t op = 0, p5 = 0, a0 = 0, a1 = 0;
    uint64_t cmd = 0;   // EE->IOP commands fed before it
    uint64_t t = 0;     // samples
    uint64_t line = 0;
};

class Platform final : public b3native::RwaIopPlatform {
public:
    explicit Platform(std::string game) : game_(std::move(game)) {}
    b3native::RwaIop* iop = nullptr;
    uint64_t cmds = 0;
    std::vector<Reply> replies;
    std::vector<std::vector<uint8_t>> areas;   // the 0x680 bytes of each 0x14 reply
    uint64_t opens = 0, open_fail = 0, bytes_read = 0;
    std::map<std::string, std::string> resolved;   // request name -> path (for the report)

    uint8_t* byte(uint32_t a) {
        a &= 0x1FFFFFFF;
        auto& page = mem_[a >> 12];
        if (!page) page = std::make_unique<std::array<uint8_t, 4096>>();   // value-initialised: zeros
        return &(*page)[a & 0xFFF];
    }
    void store(uint32_t a, const std::vector<uint8_t>& d) {
        for (size_t k = 0; k < d.size(); ++k) *byte(a + static_cast<uint32_t>(k)) = d[k];
    }

    void read_ee(uint32_t a, void* dst, uint32_t n) override {
        auto* d = static_cast<uint8_t*>(dst);
        for (uint32_t k = 0; k < n; ++k) d[k] = *byte(a + k);
    }
    void write_ee(uint32_t a, const void* src, uint32_t n) override {
        const auto* s = static_cast<const uint8_t*>(src);
        for (uint32_t k = 0; k < n; ++k) *byte(a + k) = s[k];
    }
    void reply(const uint32_t p[12]) override {
        Reply r;
        r.op = p[4];
        r.p5 = p[5];
        r.a0 = p[9];
        r.a1 = p[10];
        r.cmd = cmds;
        r.t = iop ? iop->now() : 0;
        replies.push_back(r);
        if (p[4] == 0x14 && p[11] != 0) {
            std::vector<uint8_t> a(0x680);
            read_ee(p[7], a.data(), 0x680);
            areas.push_back(std::move(a));
        }
    }
    std::string resolve(const std::string& name) {
        std::string n = name;
        if (auto c = n.find(':'); c != std::string::npos) n = n.substr(c + 1);
        std::replace(n.begin(), n.end(), '\\', '/');
        fs::path cur = game_;
        size_t i = 0;
        while (i < n.size()) {
            size_t j = n.find('/', i);
            if (j == std::string::npos) j = n.size();
            const std::string comp = n.substr(i, j - i);
            i = j + 1;
            if (comp.empty()) continue;
            std::string lc = comp;
            for (char& ch : lc) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            bool found = false;
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(cur, ec)) {
                std::string en = e.path().filename().string();
                for (char& ch : en) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (en == lc) { cur = e.path(); found = true; break; }
            }
            if (!found) return {};
        }
        return cur.string();
    }
    int file_open(const char* name) override {
        ++opens;
        const std::string path = resolve(name);
        resolved[name] = path.empty() ? "(not found)" : path;
        FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
        if (!f) { ++open_fail; return 0; }
        files_[++next_] = f;
        return next_;
    }
    int file_read(int h, void* dst, uint32_t n) override {
        auto it = files_.find(h);
        if (it == files_.end()) return 0;
        const size_t got = std::fread(dst, 1, n, it->second);
        bytes_read += got;
        return static_cast<int>(got);
    }
    int file_seek(int h, uint32_t pos) override {
        auto it = files_.find(h);
        if (it == files_.end()) return -1;
        return std::fseek(it->second, static_cast<long>(pos), SEEK_SET) == 0 ? 0 : -1;
    }
    void file_close(int h) override {
        auto it = files_.find(h);
        if (it == files_.end()) return;
        std::fclose(it->second);
        files_.erase(it);
    }

private:
    std::string game_;
    std::unordered_map<uint32_t, std::unique_ptr<std::array<uint8_t, 4096>>> mem_;
    std::map<int, FILE*> files_;
    int next_ = 0;
};

// The arguments RwaSPU2OutputCmdHandler / _RwaRPCProcessSystemCommand use, per opcode (src/libs/rwa).
struct Use { bool p5, a0, a1; };
Use ee_use(uint32_t op, uint32_t a0) {
    switch (op) {
    case 0x14: case 0x39: case 0x12: case 0x1A: case 0x1D: return {false, false, false};
    case 0x33: case 0x47: case 0x19: return {op != 0x19, true, true};
    case 0x1C: return {false, true, false};
    case 0x1B: return {false, true, true};
    default:
        if ((op >= 0x2B && op <= 0x41 && (op & 1)) || (op >= 0x4B && op <= 0x51 && (op & 1)))
            return {true, true, a0 != 0};
        return {true, true, true};   // system replies 9 .. 0x10
    }
}

// Test B: a Spu2Mixer in front of the real one that records the voice-level writes (pass-through otherwise).
class TraceMixer final : public b3audio::Spu2Mixer {
public:
    TraceMixer(b3audio::Spu2Mixer& m, std::FILE* f) : in_(m), f_(f) {}
    uint64_t snap = 0, poll = 0;
    void finish() {
        for (int n = 0; n < 48; ++n) flush_kon(n);
        for (auto& l : lines_) std::fputs(l.c_str(), f_);
        lines_.clear();
    }
    void set_param(uint64_t t, uint16_t e, uint16_t v) override {
        in_.set_param(t, e, v);
        if (e & 0x80) return;   // core parameters
        const int hw = b3audio::sd::hw_voice(e);
        if (hw >= 48) return;
        Vr& r = vr_[hw];
        switch (e & 0xFF00) {
        case b3audio::sd::VP_VOLL: r.voll = v; emit(t, "VOLL", hw, v); break;
        case b3audio::sd::VP_VOLR: r.volr = v; emit(t, "VOLR", hw, v); break;
        case b3audio::sd::VP_PITCH: r.pitch = v; emit(t, "PITCH", hw, v); break;
        case b3audio::sd::VP_ADSR1: r.adsr1 = v; break;
        case b3audio::sd::VP_ADSR2: r.adsr2 = v; break;
        default: break;
        }
    }
    void set_addr(uint64_t t, uint16_t e, uint32_t a) override {
        in_.set_addr(t, e, a);
        const int hw = b3audio::sd::hw_voice(e);
        if (hw >= 48) return;
        if ((e & 0xFFC0) == (b3audio::sd::VA_SSA & 0xFFC0) || (e & 0xFF00) == 0x2000) vr_[hw].ssa = a;
        else if ((e & 0xFF00) == 0x2100) {
            if (pending_[hw] >= 0) {   // the KON's loop address
                char b[24];
                std::snprintf(b, sizeof b, "%x", a);
                auto& l = lines_[static_cast<size_t>(pending_[hw])];
                l.replace(l.find("@LSA@"), 5, b);
                pending_[hw] = -1;
                return;   // the trace carries it in its key-on event, not as an LSAX write
            }
            emit(t, "LSAX", hw, a);
        }
    }
    void set_switch(uint64_t t, uint16_t e, uint32_t v) override {
        in_.set_switch(t, e, v);
        const int core = e & 1;
        if ((e & 0xFF00) == b3audio::sd::S_KOFF && (v & 0xFFFFFF)) {
            for (int n = 0; n < 24; ++n)
                if (v & (1u << n)) line(t, "KOFF %x %x\n", core, 1u << n);
        } else if ((e & 0xFF00) == b3audio::sd::S_KON && (v & 0xFFFFFF)) {
            for (int n = 0; n < 24; ++n) {
                if (!(v & (1u << n))) continue;
                const int hw = core * 24 + n;
                flush_kon(hw);
                const Vr& r = vr_[hw];
                pending_[hw] = static_cast<int64_t>(lines_.size());
                line(t, "KON %x %x %x @LSA@ %x %x %x %x %x\n", core, n, r.ssa, r.pitch, r.voll, r.volr, r.adsr1, r.adsr2);
            }
        }
        if (lines_.size() > 4096 && std::all_of(pending_, pending_ + 48, [](int64_t p) { return p < 0; })) finish();
    }
    void set_core_attr(uint64_t t, uint16_t e, uint16_t v) override { in_.set_core_attr(t, e, v); }
    void set_effect_attr(uint64_t t, int c, const b3audio::Spu2EffectAttr& a) override { in_.set_effect_attr(t, c, a); }
    void voice_trans(uint64_t t, int ch, uint32_t a, const uint8_t* s, uint32_t n) override { in_.voice_trans(t, ch, a, s, n); }
    uint16_t get_param(uint64_t t, uint16_t e) override { return in_.get_param(t, e); }
    uint32_t get_addr(uint64_t t, uint16_t e) override { return in_.get_addr(t, e); }
    uint32_t get_switch(uint64_t t, uint16_t e) override { return in_.get_switch(t, e); }
    const uint8_t* sound_ram() const override { return in_.sound_ram(); }
    uint64_t run(uint64_t t, uint32_t* irq) override { return in_.run(t, irq); }
    size_t read_output(int16_t* lr, size_t n) override { return in_.read_output(lr, n); }

private:
    struct Vr { uint32_t ssa = 0; uint16_t pitch = 0, voll = 0, volr = 0, adsr1 = 0, adsr2 = 0; };
    b3audio::Spu2Mixer& in_;
    std::FILE* f_;
    Vr vr_[48];
    int64_t pending_[48] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::vector<std::string> lines_;
    void flush_kon(int hw) {
        if (pending_[hw] < 0) return;
        auto& l = lines_[static_cast<size_t>(pending_[hw])];
        l.replace(l.find("@LSA@"), 5, "-");
        pending_[hw] = -1;
    }
    template <class... A> void line(uint64_t t, const char* fmt, A... a) {
        char b[160];
        int k = std::snprintf(b, sizeof b, "%" PRIu64 " %" PRIu64 " %" PRIu64 " ", snap, poll, t);
        std::snprintf(b + k, sizeof b - static_cast<size_t>(k), fmt, a...);
        lines_.emplace_back(b);
    }
    void emit(uint64_t t, const char* kind, int hw, uint32_t v) {
        char fmt[32];
        std::snprintf(fmt, sizeof fmt, "%s %%x %%x %%x\n", kind);
        line(t, fmt, hw / 24, hw % 24, v);
    }
};

struct Hist {
    std::map<std::string, uint64_t> b;
    void add(int64_t d) {
        const int64_t a = d < 0 ? -d : d;
        const char* k = a == 0 ? "0" : a <= 4 ? "1..4" : a <= 28 ? "5..28" : a <= 112 ? "29..112" : a <= 448 ? "113..448"
                        : a <= 1792 ? "449..1792" : a <= 7168 ? "1793..7168" : ">7168";
        ++b[std::string(d < 0 && a ? "-" : d > 0 ? "+" : "") + k];
    }
    void print(const char* title) const {
        std::printf("  %s:", title);
        static const char* order[] = {"->7168", "-1793..7168", "-449..1792", "-113..448", "-29..112", "-5..28", "-1..4", "0",
                                      "+1..4", "+5..28", "+29..112", "+113..448", "+449..1792", "+1793..7168", "+>7168"};
        for (const char* k : order)
            if (auto it = b.find(k); it != b.end()) std::printf(" [%s] %" PRIu64, k, it->second);
        std::printf("\n");
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string log = std::getenv("B3_SIF_LOG") ? std::getenv("B3_SIF_LOG") : "build/siflog/navproof14s1.txt";
    std::string game = std::getenv("B3_GAME_DIR") ? std::getenv("B3_GAME_DIR") : "data/game";
    bool spurious = true, verbose = false, within = true, real_mixer = false;
    uint32_t gap = 279;
    int dump_voice = -1;
    uint64_t dump_from = 0;
    if (const char* d = std::getenv("B0_DUMP_VOICE")) dump_voice = std::atoi(d);
    if (const char* d = std::getenv("B0_DUMP_FROM")) dump_from = std::strtoull(d, nullptr, 0);
    int pos = 0;
    std::string wav_path, trace_path;
    double wav_from = 0, wav_seconds = 0;   // 0 = to the end
    bool no_reverb = false;                  // --no-reverb: libsd's presets not loaded (a silent wet path)
    std::vector<int16_t> wav;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-spurious-0d") spurious = false;
        else if (a == "--verbose") verbose = true;
        else if (a == "--no-gap") gap = 0;
        else if (a == "--real-mixer") real_mixer = true;
        else if (a == "--frame-start") within = false;
        else if (a == "--wav" && i + 1 < argc) wav_path = argv[++i];
        else if (a == "--spu2-trace" && i + 1 < argc) trace_path = argv[++i];
        else if (a == "--no-reverb") no_reverb = true;
        else if (a == "--wav-from" && i + 1 < argc) wav_from = std::atof(argv[++i]);
        else if (a == "--wav-seconds" && i + 1 < argc) wav_seconds = std::atof(argv[++i]);
        else if (pos == 0) { log = a; ++pos; }
        else { game = a; ++pos; }
    }
    std::ifstream in(log);
    if (!in) {
        std::printf("test_rwa_iop_b0: SKIP (no log at %s)\n", log.c_str());
        return 0;
    }
    std::printf("test_rwa_iop_b0: %s\n  game files: %s\n  spurious 0x0D: %s; key-on gap %u samples; time within a frame: %s\n", log.c_str(),
                game.c_str(), spurious ? "on" : "off", gap, within ? "from steps" : "frame start");

    // Pass 1: the time base.
    std::vector<std::string> lines;
    {
        std::string l;
        while (std::getline(in, l)) lines.push_back(std::move(l));
    }
    uint64_t step0 = 0, first_poll = 0, first_poll_step = 0, last_poll = 0, last_poll_step = 0;
    bool have0 = false;
    for (const auto& l : lines) {
        if (l.size() < 2 || l[0] == 'E') continue;
        uint64_t poll = 0, step = 0;
        if (std::sscanf(l.c_str() + 2, "%" SCNu64 " %" SCNu64, &poll, &step) != 2) continue;
        if (!have0) { step0 = step; have0 = true; }
        if (poll != 0) {
            if (first_poll == 0) { first_poll = poll; first_poll_step = step; }
            if (poll != last_poll) { last_poll = poll; last_poll_step = step; }
        }
    }
    const double steps_per_poll = (last_poll > first_poll) ? double(last_poll_step - first_poll_step) / double(last_poll - first_poll) : 1e6;
    const double spp = steps_per_poll / 800.0;   // steps per sample, pad-poll region
    // Start-up region (poll 0): one frame per 0x14 snapshot (the EE sends one per tick; the reference answers at once).
    uint64_t snap0_first = 0, snap0_last = 0, snap0_n = 0;
    for (const auto& l : lines) {
        if (l.size() < 2 || l[0] != 'S') continue;
        uint64_t poll = 0, step = 0;
        unsigned cmd = 1;
        char pk[128] = {};
        if (std::sscanf(l.c_str() + 2, "%" SCNu64 " %" SCNu64 " %x %*s %*s %*s %100s", &poll, &step, &cmd, pk) != 4) continue;
        if (poll != 0 || cmd != 0 || std::strncmp(pk + 32, "14000000", 8) != 0) continue;
        if (snap0_n++ == 0) snap0_first = step;
        snap0_last = step;
    }
    const double steps_per_snap0 = snap0_n > 1 ? double(snap0_last - snap0_first) / double(snap0_n - 1) : steps_per_poll;
    const double spp0 = steps_per_snap0 / 800.0;
    std::printf("  time base: %zu lines; pad polls %" PRIu64 "..%" PRIu64 " at %.0f steps per poll; before them %" PRIu64
                " snapshots at %.0f steps each; 800 samples per poll / snapshot\n",
                lines.size(), first_poll, last_poll, steps_per_poll, snap0_n, steps_per_snap0);

    Platform pf(game);
    b3native::RwaIopOptions opt;
    opt.spurious_0d = spurious;
    opt.keyon_gap_samples = gap;
    std::unique_ptr<b3audio::Spu2Mixer> mixer;
    if (real_mixer) {
        mixer = std::make_unique<b3audio::Spu2MixerImpl>();
        const std::string libsd = game + "/IOP/LIBSD.IRX";
        std::printf("  libsd reverb presets: %s\n", !no_reverb && b3audio::load_libsd_reverb_presets(libsd.c_str()) ? "loaded" : "not loaded (reverb silent)");
    }
    else mixer = std::make_unique<b3audio::Spu2StubMixer>();
    std::printf("  mixer: %s\n", real_mixer ? "SPU2-equivalent (spu2_mixer_impl)" : "stub");
    std::FILE* trace_file = nullptr;
    std::unique_ptr<TraceMixer> tracer;
    if (!trace_path.empty()) {
        trace_file = std::fopen(trace_path.c_str(), "wb");
        if (!trace_file) { std::printf("  cannot write %s\n", trace_path.c_str()); return 1; }
        tracer = std::make_unique<TraceMixer>(*mixer, trace_file);
        std::printf("  spu2 trace: %s\n", trace_path.c_str());
    }
    b3native::RwaIop iop(pf, tracer ? static_cast<b3audio::Spu2Mixer&>(*tracer) : *mixer, opt);
    pf.iop = &iop;

    std::vector<Reply> logr;
    uint64_t cmd_lines = 0, other_sif = 0, a_lines = 0;
    uint64_t poll_anchor = 0, frame_step = 0;
    uint64_t t_prev = 0, frame_t = 0;
    bool framed = false;
    // Reply-area comparison.
    uint64_t areas_compared = 0, areas_missing = 0, pid_eq = 0, ps_cmp = 0, ps_eq = 0, ls_cmp = 0, ls_eq = 0, rest_eq = 0;
    std::map<std::string, uint64_t> ps_pairs;   // "log->mine" for differing PlayStates
    Hist pos_playing, pos_other, pos_zero;
    std::vector<uint32_t> prev_log(48, 3), prev_mine(48, 3);
    uint64_t ends_log = 0, ends_mine = 0;
    uint64_t pos_cmp = 0, pos_eq = 0;
    int shown = 0;

    for (size_t ln = 0; ln < lines.size(); ++ln) {
        const std::string& l = lines[ln];
        if (l.size() < 2 || l[0] == 'E') continue;
        const char kind = l[0];
        char buf1[64];
        uint64_t poll = 0, step = 0;
        if (std::sscanf(l.c_str() + 2, "%" SCNu64 " %" SCNu64 " %63s", &poll, &step, buf1) < 2) continue;
        // time
        // Frames start at each 0x14 snapshot while poll is 0, at each new poll after; 800 samples per frame
        // (per poll step), the steps since the frame start add at most 799 samples.
        uint64_t t;
        const bool snap = kind == 'S' && l.find(" 0 ", 2) != std::string::npos && l.size() > 40 && [&] {
            unsigned cmd = 1;
            char pk[128] = {};
            return std::sscanf(l.c_str() + 2, "%*s %*s %x %*s %*s %*s %100s", &cmd, pk) == 2 && cmd == 0 &&
                   std::strncmp(pk + 32, "14000000", 8) == 0;
        }();
        if (!framed && !(poll == 0 && snap) && poll == 0) {
            t = static_cast<uint64_t>(double(step - step0) / spp0);
        } else {
            if (!framed) {
                framed = true;
                frame_t = static_cast<uint64_t>(double(step - step0) / (poll == 0 ? spp0 : spp));
                frame_step = step;
                poll_anchor = poll;
            } else if (poll == 0 && snap) {
                frame_t += 800;
                frame_step = step;
            } else if (poll != poll_anchor) {
                frame_t += 800 * (poll_anchor == 0 ? 1 : poll - poll_anchor);
                frame_step = step;
                poll_anchor = poll;
            }
            t = frame_t + (within ? std::min<uint64_t>(799, static_cast<uint64_t>(double(step - frame_step) / (poll == 0 ? spp0 : spp))) : 0);
        }
        t = std::max(t, t_prev);
        t_prev = t;
        if (tracer) tracer->poll = poll;
        if (t > iop.now()) iop.advance(static_cast<uint32_t>(t - iop.now()));
        if (!wav_path.empty()) {   // drain the mixer's output after every advance (its ring drops the oldest frames)
            int16_t buf[2 * 4096];
            for (size_t n; (n = mixer->read_output(buf, 4096)) > 0;) wav.insert(wav.end(), buf, buf + 2 * n);
        }

        // tokens
        std::vector<std::string> tok;
        {
            size_t i = 0;
            while (i < l.size()) {
                size_t j = l.find(' ', i);
                if (j == std::string::npos) j = l.size();
                tok.push_back(l.substr(i, j - i));
                i = j + 1;
            }
        }
        if (kind == 'S') {
            if (tok.size() < 9) continue;
            const uint32_t cmd = static_cast<uint32_t>(std::stoul(tok[3], nullptr, 16));
            if (cmd != 0) { ++other_sif; continue; }
            const uint32_t src = static_cast<uint32_t>(std::stoul(tok[4], nullptr, 16));
            const uint32_t dst = static_cast<uint32_t>(std::stoul(tok[5], nullptr, 16));
            const uint32_t size = static_cast<uint32_t>(std::stoul(tok[6], nullptr, 16));
            const auto w = words(unhex(tok[7]));
            if (size != 0 && src != 0) {
                if (tok[8].empty() || tok[8][0] == '#' || tok[8] == "-") {
                    std::printf("  line %zu: payload not in the log (%s); zeros used\n", ln + 1, tok[8].c_str());
                } else {
                    pf.store(src, unhex(tok[8]));
                }
            }
            ++cmd_lines;
            if (tracer && w[4] == 0x14) ++tracer->snap;
            pf.cmds = cmd_lines;   // replies made while this command runs count as after it, like the log's
            iop.send_cmd(w.data(), src, dst, size);
        } else if (kind == 'U') {
            if (tok.size() < 7) continue;
            Reply r;
            r.op = static_cast<uint32_t>(std::stoul(tok[3], nullptr, 16));
            r.a0 = static_cast<uint32_t>(std::stoul(tok[4], nullptr, 16));
            r.a1 = static_cast<uint32_t>(std::stoul(tok[5], nullptr, 16));
            r.p5 = static_cast<uint32_t>(std::stoul(tok[6], nullptr, 16));
            r.cmd = cmd_lines;
            r.t = t;
            r.line = ln + 1;
            logr.push_back(r);
        } else if (kind == 'P') {
            if (tok.size() < 4) continue;
            const auto w = words(unhex(tok[3]));
            if (w[4] >= 9 && w[4] <= 0x10) {
                Reply r;
                r.op = w[4];
                r.p5 = w[5];
                r.a0 = w[9];
                r.a1 = w[10];
                r.cmd = cmd_lines;
                r.t = t;
                r.line = ln + 1;
                logr.push_back(r);
            }
        } else if (kind == 'A') {
            ++a_lines;
            if (a_lines == 1) continue;   // before the first snapshot
            if (tok.size() < 4) continue;
            const auto ref = unhex(tok[3]);
            if (ref.size() < 0x680) continue;
            if (pf.areas.size() < a_lines - 1) { ++areas_missing; continue; }
            const auto& mine = pf.areas[a_lines - 2];
            ++areas_compared;
            if (rd32(&ref[0x670]) == rd32(&mine[0x670])) ++pid_eq;
            bool rest = true;
            for (uint32_t n = 0; n < 48; ++n) {
                const uint8_t* r = &ref[n * 0x20];
                const uint8_t* m = &mine[n * 0x20];
                const uint32_t rps = rd32(r), mps = rd32(m);
                ++ps_cmp;
                if (prev_log[n] == 4 && rps == 1) ++ends_log;
                if (prev_mine[n] == 4 && mps == 1) ++ends_mine;
                prev_log[n] = rps;
                prev_mine[n] = mps;
                if (rps == mps) ++ps_eq;
                else {
                    ++ps_pairs[std::to_string(rps) + "->" + std::to_string(mps)];
                    if (verbose && shown < 20) {
                        ++shown;
                        std::printf("  area %" PRIu64 " voice %u: PlayState log %u mine %u (start %x freq %u flags %x pos log %u mine %u)\n",
                                    a_lines - 1, n, rps, mps, rd32(r + 4), rd32(r + 12), rd32(r + 28), rd32(r + 20), rd32(m + 20));
                    }
                }
                ++ls_cmp;
                if (rd32(r + 8) == rd32(m + 8)) ++ls_eq;
                if ((dump_voice == -2 || (dump_voice >= 0 && static_cast<int>(n) == dump_voice)) && a_lines < 2000 + dump_from && a_lines >= dump_from)
                    std::printf("  dump area %" PRIu64 " t %" PRIu64 " v%u ps %u/%u start %x loop %x/%x freq %u flags %x pos log %u mine %u\n", a_lines - 1, t, n,
                                rps, mps, rd32(r + 4), rd32(r + 8), rd32(m + 8), rd32(r + 12), rd32(r + 28), rd32(r + 20), rd32(m + 20));
                if (rps == mps && rps != 3) {
                    ++pos_cmp;
                    const int64_t d = int64_t(int32_t(rd32(m + 20))) - int64_t(int32_t(rd32(r + 20)));
                    if (d == 0) ++pos_eq;
                    (rps == 4 ? pos_playing : rps == 0 ? pos_zero : pos_other).add(d);
                }
                for (int k : {4, 12, 16, 24, 28})
                    if (rd32(r + k) != rd32(m + k)) rest = false;
            }
            if (std::memcmp(&ref[0x600], &mine[0x600], 0x70) != 0) rest = false;
            if (rest) ++rest_eq;
        }
    }

    // Reply alignment.
    const auto& mine = pf.replies;
    struct OpStat { uint64_t log = 0, mine = 0, matched = 0, args = 0, ee_args = 0, missing = 0, extra = 0; };
    std::map<uint32_t, OpStat> ops;
    for (const auto& r : logr) ++ops[r.op].log;
    for (const auto& r : mine) ++ops[r.op].mine;
    Hist dcmd, dt;
    std::map<std::string, uint64_t> argdiff;
    size_t i = 0, j = 0;
    const size_t W = 64;
    while (i < logr.size() || j < mine.size()) {
        if (i < logr.size() && j < mine.size() && logr[i].op == mine[j].op) {
            OpStat& s = ops[logr[i].op];
            ++s.matched;
            const Reply& a = logr[i];
            const Reply& b = mine[j];
            if (a.p5 == b.p5 && a.a0 == b.a0 && a.a1 == b.a1) ++s.args;
            else {
                char k[96];
                std::snprintf(k, sizeof k, "op %02x log (%x,%x,%x) mine (%x,%x,%x)", a.op, a.p5, a.a0, a.a1, b.p5, b.a0, b.a1);
                ++argdiff[k];
            }
            const Use u = ee_use(a.op, a.a0);
            if ((!u.p5 || a.p5 == b.p5) && (!u.a0 || a.a0 == b.a0) && (!u.a1 || a.a1 == b.a1)) ++s.ee_args;
            dcmd.add(int64_t(b.cmd) - int64_t(a.cmd));
            dt.add(int64_t(b.t) - int64_t(a.t));
            ++i;
            ++j;
            continue;
        }
        // find the nearest resync
        size_t skip_mine = W + 1, skip_log = W + 1;
        if (i < logr.size())
            for (size_t k = j; k < mine.size() && k - j <= W; ++k)
                if (mine[k].op == logr[i].op) { skip_mine = k - j; break; }
        if (j < mine.size())
            for (size_t k = i; k < logr.size() && k - i <= W; ++k)
                if (logr[k].op == mine[j].op) { skip_log = k - i; break; }
        if (j < mine.size() && (i >= logr.size() || skip_mine <= skip_log)) {
            ++ops[mine[j].op].extra;
            ++j;
        } else {
            ++ops[logr[i].op].missing;
            ++i;
        }
    }

    const auto& st = iop.stats();
    std::printf("  fed: %" PRIu64 " RWA commands (SIF cmd 0), %" PRIu64 " other SIF commands skipped; virtual time %.1f s\n",
                cmd_lines, other_sif, double(iop.now()) / 48000.0);
    std::printf("  executor: %" PRIu64 " replies, %" PRIu64 " snapshots (%" PRIu64 " with the key-on gap), %" PRIu64 " spurious 0x0D, %" PRIu64
                " dropped, %" PRIu64 " unknown ops, %" PRIu64 " file ops (%" PRIu64 " short reads, %" PRIu64 " open failures), reverb changes %" PRIu64
                " (clears %" PRIu64 ", during a clear %" PRIu64 ")\n",
                st.replies, st.snapshots, st.keyon_delays, st.spurious_0d, st.dropped, st.unknown_ops, st.file_ops, st.file_short_reads,
                st.file_open_failures, st.reverb_changes, st.reverb_clears, st.reverb_changes_deferred);
    std::printf("  files: %" PRIu64 " opens, %" PRIu64 " failed, %" PRIu64 " bytes read\n", pf.opens, pf.open_fail, pf.bytes_read);
    for (const auto& [n, p] : pf.resolved) std::printf("    %-32s -> %s\n", n.c_str(), p.c_str());
    std::printf("  replies: log %zu, executor %zu\n", logr.size(), mine.size());
    std::printf("    op    log   mine  matched  args=  ee-args=  missing  extra\n");
    uint64_t tm = 0, ta = 0, te = 0, tmiss = 0, textra = 0;
    for (const auto& [op, s] : ops) {
        std::printf("    %02x %6" PRIu64 " %6" PRIu64 " %8" PRIu64 " %6" PRIu64 " %9" PRIu64 " %8" PRIu64 " %6" PRIu64 "\n", op, s.log, s.mine,
                    s.matched, s.args, s.ee_args, s.missing, s.extra);
        tm += s.matched; ta += s.args; te += s.ee_args; tmiss += s.missing; textra += s.extra;
    }
    std::printf("    all %5zu %6zu %8" PRIu64 " %6" PRIu64 " %9" PRIu64 " %8" PRIu64 " %6" PRIu64 "\n", logr.size(), mine.size(), tm, ta, te, tmiss, textra);
    int nshow = 0;
    for (const auto& [k, n] : argdiff) {
        if (nshow++ >= 12) { std::printf("    ... %zu kinds of argument difference\n", argdiff.size()); break; }
        std::printf("    args differ x%" PRIu64 ": %s\n", n, k.c_str());
    }
    dcmd.print("timing, executor - log, in commands");
    dt.print("timing, executor - log, in samples ");
    std::printf("  reply areas: %" PRIu64 " A lines, %" PRIu64 " compared, %" PRIu64 " without an executor reply yet\n", a_lines, areas_compared, areas_missing);
    std::printf("    PacketID equal %" PRIu64 " / %" PRIu64 "\n", pid_eq, areas_compared);
    std::printf("    PlayState equal %" PRIu64 " / %" PRIu64 " voice records\n", ps_eq, ps_cmp);
    for (const auto& [k, n] : ps_pairs) std::printf("      log->executor %s: %" PRIu64 "\n", k.c_str(), n);
    std::printf("    LoopStartAddress equal %" PRIu64 " / %" PRIu64 "\n", ls_eq, ls_cmp);
    std::printf("    other fields (echoed) equal in %" PRIu64 " / %" PRIu64 " areas\n", rest_eq, areas_compared);
    std::printf("    CurrentPosition (same PlayState, not 3): %" PRIu64 " compared, %" PRIu64 " exact\n", pos_cmp, pos_eq);
    pos_playing.print("  executor - log, PlayState 4");
    pos_other.print("  executor - log, PlayState 1/5");
    pos_zero.print("  executor - log, PlayState 0 ");
    std::printf("    voice ends (4 -> 1 between areas): log %" PRIu64 ", executor %" PRIu64 "\n", ends_log, ends_mine);
    if (tracer) {
        tracer->finish();
        std::fclose(trace_file);
    }
    if (!wav_path.empty()) {
        const size_t frames = wav.size() / 2, from = std::min(frames, static_cast<size_t>(wav_from * 48000));
        const size_t len = wav_seconds > 0 ? std::min(frames - from, static_cast<size_t>(wav_seconds * 48000)) : frames - from;
        if (std::FILE* f = std::fopen(wav_path.c_str(), "wb")) {
            auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
            auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
            const uint32_t bytes = static_cast<uint32_t>(len * 4);
            std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
            w32(16); w16(1); w16(2); w32(48000); w32(48000 * 4); w16(4); w16(16);
            std::fwrite("data", 1, 4, f); w32(bytes);
            std::fwrite(wav.data() + 2 * from, 4, len, f);
            std::fclose(f);
            std::printf("  wav: %s, %.1f s from %.1f s (%zu frames rendered in all)\n", wav_path.c_str(), len / 48000.0, from / 48000.0, frames);
        } else {
            std::printf("  wav: cannot write %s\n", wav_path.c_str());
        }
    }
    return 0;
}
