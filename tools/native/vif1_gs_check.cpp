// vif1_gs_check -- the VU1 / GS check (vu0 lane, 2026-11-07; step 2): two runs' DMA streams
// (native_boot B3NATIVE_DMA_DUMP text: "<poll> <channel> <guest addr> <word>") through the same model of what reaches
// the GS, compared poll by poll:
//   - VIF1 (channel 1): b3asset::VifInterpreter (UNPACK, STCYCL, STMOD, STROW / STCOL, STMASK, ITOP), its MPG payloads
//     loaded into a VU1 micro memory, MSCAL / MSCALF / MSCNT running b3asset::Vu1 (VU1's stall model) on the same data
//     memory; every XGKICK's GIF packet (read_gif_packet's length, raw quadwords) and every DIRECT / DIRECTHL payload
//     is a GS record;
//   - GIF (channel 2, PATH3): the words themselves.
// The whole dump is one continuous stream per channel (VIF and VU state carry across transfers and polls). A kick to
// micro code the window has not uploaded (the snapshot holds no VU1 memories) is recorded as such, not run.
// Equal GS records = the two streams draw the same thing, whatever their other words (e.g. unused matrix w lanes).
//
//   build/vif1_gs_check <dump A> <dump B> [-v]
#include "b3asset/vif.h"
#include "b3asset/vu1.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Poll { std::vector<std::uint8_t> vif1, gif; std::uint32_t last1 = 0, last2 = 0; };

bool load(const char* path, std::map<unsigned long long, Poll>& out) {
    std::FILE* f = std::fopen(path, "r");
    if (!f) return false;
    unsigned long long poll;
    unsigned ch;
    unsigned addr, w;
    while (std::fscanf(f, "%llu %u %x %x", &poll, &ch, &addr, &w) == 4) {
        Poll& p = out[poll];
        std::vector<std::uint8_t>& v = ch == 1 ? p.vif1 : p.gif;
        // the FIFO is 128 bits wide: keep each word at its guest address's position within a quadword (a TTE tag's
        // upper half arrives without its lower half), padding with VIF NOPs (zero words) at a discontinuity
        std::uint32_t& last = ch == 1 ? p.last1 : p.last2;
        if (ch == 1 && addr != last + 4)
            while ((v.size() & 15) != (addr & 15)) v.insert(v.end(), 4, std::uint8_t{0});
        last = addr;
        const std::size_t n = v.size();
        v.resize(n + 4);
        std::memcpy(v.data() + n, &w, 4);
    }
    std::fclose(f);
    return true;
}

struct Record { std::string kind; std::vector<std::uint32_t> words; };

struct Model {
    b3asset::VifInterpreter vif;
    b3asset::VuMicroprogram prog;
    std::vector<bool> loaded = std::vector<bool>(2048, false);
    std::unique_ptr<b3asset::Vu1> vu;
    std::vector<Record>* out = nullptr;
    std::uint64_t kicks = 0, kicks_unloaded = 0, xgkicks = 0, errors = 0;
    Model() {
        vu = std::make_unique<b3asset::Vu1>(vif.mem);
        vif.on_mpg = [this](std::uint32_t a, const std::uint8_t* p, std::uint32_t pairs) {
            for (std::uint32_t k = 0; k < pairs; ++k) {
                const std::uint32_t pa = (a + k) & 2047;
                std::memcpy(&prog.lower[pa], p + 8 * k, 4);
                std::memcpy(&prog.upper[pa], p + 8 * k + 4, 4);
                loaded[pa] = true;
            }
            prog.loaded = 2048;
        };
        vif.on_direct = [this](const std::uint8_t* p, std::size_t qw) {
            Record r{"direct", std::vector<std::uint32_t>(qw * 4)};
            std::memcpy(r.words.data(), p, qw * 16);
            out->push_back(std::move(r));
        };
    }
    void kick(const b3asset::VuMemory& mem, std::uint32_t itop, b3asset::VifInterpreter::KickKind kind, std::uint32_t pc) {
        ++kicks;
        const std::uint32_t start = kind == b3asset::VifInterpreter::kMscnt ? vu->tpc : (pc & 2047);
        if (!loaded[start]) {
            ++kicks_unloaded;
            out->push_back({"kick to unloaded code at " + std::to_string(start), {}});
            return;
        }
        std::string why;
        auto on_kick = [this, &mem](std::uint32_t qa) {
            ++xgkicks;
            b3asset::GifPacket pk;
            std::string w2;
            Record r{"xgkick", {}};
            if (!b3asset::read_gif_packet(mem, qa, pk, &w2)) {
                r.kind = "xgkick (unreadable: " + w2 + ")";
                pk.qwords = 1;
            }
            for (std::uint32_t k = 0; k < pk.qwords; ++k) {
                const std::uint32_t* q = mem.qw(qa + k);
                r.words.insert(r.words.end(), q, q + 4);
            }
            out->push_back(std::move(r));
        };
        if (!vu->run(prog, start, 0, itop, on_kick, &why, 4000000)) {
            ++errors;
            out->push_back({"vu1 error at " + std::to_string(start) + ": " + why, {}});
        }
    }
    void poll(const Poll& p, std::vector<Record>& recs) {
        out = &recs;
        std::string why;
        if (!p.vif1.empty() &&
            !vif.run(p.vif1.data(), p.vif1.size(), [this](const b3asset::VuMemory& m, std::uint32_t it, b3asset::VifInterpreter::KickKind k, std::uint32_t pc) { kick(m, it, k, pc); }, &why))
            recs.push_back({"vif1 error: " + why, {}});
        if (!p.gif.empty()) {
            Record r{"path3", std::vector<std::uint32_t>(p.gif.size() / 4)};
            std::memcpy(r.words.data(), p.gif.data(), p.gif.size());
            recs.push_back(std::move(r));
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: vif1_gs_check <dump A> <dump B> [-v]\n"); return 2; }
    const bool verbose = argc > 3 && std::string(argv[3]) == "-v";
    std::map<unsigned long long, Poll> a, b;
    if (!load(argv[1], a) || !load(argv[2], b)) { std::fprintf(stderr, "cannot read the dumps\n"); return 2; }
    Model ma, mb;
    std::uint64_t polls = 0, equal_polls = 0, recs_total = 0, recs_differ = 0, raw_equal = 0;
    for (const auto& [poll, pa] : a) {
        auto it = b.find(poll);
        if (it == b.end()) { std::printf("poll %llu: missing in B\n", poll); continue; }
        ++polls;
        if (pa.vif1 == it->second.vif1 && pa.gif == it->second.gif) ++raw_equal;
        std::vector<Record> ra, rb;
        ma.poll(pa, ra);
        mb.poll(it->second, rb);
        std::uint64_t d = 0;
        const std::size_t n = std::max(ra.size(), rb.size());
        for (std::size_t k = 0; k < n; ++k) {
            ++recs_total;
            const bool same = k < ra.size() && k < rb.size() && ra[k].kind == rb[k].kind && ra[k].words == rb[k].words;
            if (same) continue;
            ++d;
            ++recs_differ;
            if (verbose || recs_differ <= 10) {
                std::printf("poll %llu record %zu: A %s (%zu words) / B %s (%zu words)", poll, k,
                            k < ra.size() ? ra[k].kind.c_str() : "-", k < ra.size() ? ra[k].words.size() : 0,
                            k < rb.size() ? rb[k].kind.c_str() : "-", k < rb.size() ? rb[k].words.size() : 0);
                if (k < ra.size() && k < rb.size())
                    for (std::size_t j = 0; j < std::min(ra[k].words.size(), rb[k].words.size()); ++j)
                        if (ra[k].words[j] != rb[k].words[j]) { std::printf("; first word %zu: %08X vs %08X", j, ra[k].words[j], rb[k].words[j]); break; }
                std::printf("\n");
            }
        }
        if (!d) ++equal_polls;
        if (std::getenv("VIF1_GS_KINDS")) {
            std::map<std::string, int> kinds;
            for (const Record& r : ra) ++kinds[r.kind.substr(0, 40)];
            for (const auto& [k, c] : kinds) std::printf("  poll %llu A: %d x %s\n", poll, c, k.c_str());
        }
        if (ra.size() != rb.size()) std::printf("poll %llu: %zu vs %zu records\n", poll, ra.size(), rb.size());
    }
    std::printf("vif1_gs_check: %llu polls (%llu with equal raw streams), %llu with equal GS records; %llu records, %llu differ\n",
                (unsigned long long)polls, (unsigned long long)raw_equal, (unsigned long long)equal_polls,
                (unsigned long long)recs_total, (unsigned long long)recs_differ);
    std::printf("vif1_gs_check: A: %llu kicks (%llu to code not uploaded in the window), %llu XGKICKs, %llu VU1 errors; B: %llu kicks (%llu), %llu XGKICKs, %llu errors\n",
                (unsigned long long)ma.kicks, (unsigned long long)ma.kicks_unloaded, (unsigned long long)ma.xgkicks, (unsigned long long)ma.errors,
                (unsigned long long)mb.kicks, (unsigned long long)mb.kicks_unloaded, (unsigned long long)mb.xgkicks, (unsigned long long)mb.errors);
    return recs_differ ? 1 : 0;
}
