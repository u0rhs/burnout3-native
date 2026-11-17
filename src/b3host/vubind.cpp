// VU0 register binding (b3host/vubind.h; native3 item 3). Not compiled with B3_VU_BIND itself: nothing here is a site.
#include "b3host/vubind.h"
#include "b3host/eeasm.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
extern "C" char __ImageBase;
#elif defined(__APPLE__)
#include <dlfcn.h>   // mac1: Mach-O has no __executable_start; the image base comes from dladdr (its mach header)
#else
extern "C" char __executable_start;
#endif

namespace b3host::vubind {

namespace {
struct Bind { std::uint8_t dst, fs, ft, flags; };
std::unordered_map<std::uint32_t, Bind> g_table;
thread_local int t_pending = -1;   // the bound destination of the operation between enter() and leave()
}  // namespace

std::vector<Ev>& trace() {
    thread_local std::vector<Ev> t;
    return t;
}

std::uint32_t site_of(const void* ra) {
#ifdef _WIN32
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ra) - reinterpret_cast<std::uintptr_t>(&__ImageBase));
#elif defined(__APPLE__)
    static const std::uintptr_t base = [] {
        Dl_info info{};
        return dladdr(reinterpret_cast<const void*>(&site_of), &info) ? reinterpret_cast<std::uintptr_t>(info.dli_fbase) : 0;
    }();
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ra) - base);
#else
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ra) - reinterpret_cast<std::uintptr_t>(&__executable_start));
#endif
}

__attribute__((noinline)) const u_long128& enter(unsigned kind, unsigned mask, const u_long128& d, const u_long128* s,
                                                 const u_long128* t) noexcept {
    t_pending = -1;
    if (g_mode == Mode::Off) return d;
    const std::uint32_t site = site_of(__builtin_return_address(0));
    if (g_mode == Mode::Record) {
        Ev e{site, 0, static_cast<std::uint8_t>(kind), static_cast<std::uint8_t>(mask), 0, {}, {}};
        if (s) { e.s = *s; e.has |= 1; }
        if (t) { e.t = *t; e.has |= 2; }
        trace().push_back(e);
        return d;
    }
    const auto it = g_table.find(site);
    if (it == g_table.end()) return d;
    const Bind& b = it->second;
    b3asm::State& st = b3asm::state();
    if (s != nullptr && b.fs != 0 && !(b.flags & 1)) st.vf[b.fs] = *s;
    if (t != nullptr && b.ft != 0 && !(b.flags & 2)) st.vf[b.ft] = *t;
    if (b.dst == 32) { t_pending = 32; return d; }   // ACC: the C value is the accumulator
    if (b.dst == 0) return d;
    t_pending = b.dst;
    if (b.flags & 4) return d;   // the register held what ours' d holds (retail loaded it): only the result is tracked
    return st.vf[b.dst];
}

__attribute__((noinline)) void leave(const u_long128& r) noexcept {
    if (t_pending < 0) return;
    b3asm::State& st = b3asm::state();
    if (t_pending == 32) st.acc = r;
    else st.vf[t_pending] = r;
    t_pending = -1;
}

bool load_table(const char* path, std::string* error) {
    std::FILE* f = std::fopen(path, "r");
    if (f == nullptr) { if (error) *error = std::string("cannot open ") + path; return false; }
    char line[128];
    while (std::fgets(line, sizeof line, f) != nullptr) {
        if (line[0] == '#' || line[0] == '\n') continue;
        unsigned site, dst, fs, ft, flags;
        if (std::sscanf(line, "%x %u %u %u %u", &site, &dst, &fs, &ft, &flags) != 5 || dst > 32 || fs > 31 || ft > 31) {
            std::fclose(f);
            if (error) *error = "bad line";
            return false;
        }
        g_table[site] = {static_cast<std::uint8_t>(dst), static_cast<std::uint8_t>(fs), static_cast<std::uint8_t>(ft),
                         static_cast<std::uint8_t>(flags)};
    }
    std::fclose(f);
    g_mode = Mode::Model;
    return true;
}
std::size_t table_size() { return g_table.size(); }

// COP2 macro instructions (EE Core Instruction Set Manual; the field layout as tests/r5900_mini.h executes them).
Retail decode(std::uint32_t i) {
    Retail r{0, 0, 0, 0, 0, 0, 5};
    if ((i >> 26) != 0x12 || !(i & (1u << 25))) return r;   // not a COP2 operation (a transfer: qmtc2 / qmfc2 / cfc2 / ctc2)
    const unsigned dest = (i >> 21) & 15, ft = (i >> 16) & 31, fs = (i >> 11) & 31, fd = (i >> 6) & 31, fn = i & 63;
    // VU DEST x=8 .. w=1 -> b3vu x=1 .. w=8
    const std::uint8_t mask = static_cast<std::uint8_t>(((dest & 8) >> 3) | ((dest & 4) >> 1) | ((dest & 2) << 1) | ((dest & 1) << 3));
    unsigned bcl = 5;
    auto set = [&](std::uint8_t k, unsigned d, unsigned s, unsigned t, std::uint8_t fl) {
        r = {k, mask, static_cast<std::uint8_t>(d), static_cast<std::uint8_t>(s), static_cast<std::uint8_t>(t), fl,
             static_cast<std::uint8_t>(fl & 2 ? bcl : 5)};
        return r;
    };
    if (fn < 0x3C) {
        static const std::uint8_t bcop[7] = {kAdd, kSub, kMadd, kMsub, kMax, kMin, kMul};
        if (fn < 0x1C) { bcl = fn & 3; return set(bcop[fn >> 2], fd, fs, ft, 2); }
        bcl = 4;
        switch (fn) {
        case 0x1C: return set(kMul, fd, fs, 0, 2);  case 0x1D: return set(kMax, fd, fs, 0, 2);
        case 0x1E: return set(kMul, fd, fs, 0, 2);  case 0x1F: return set(kMin, fd, fs, 0, 2);
        case 0x20: return set(kAdd, fd, fs, 0, 2);  case 0x21: return set(kMadd, fd, fs, 0, 2);
        case 0x22: return set(kAdd, fd, fs, 0, 2);  case 0x23: return set(kMadd, fd, fs, 0, 2);
        case 0x24: return set(kSub, fd, fs, 0, 2);  case 0x25: return set(kMsub, fd, fs, 0, 2);
        case 0x26: return set(kSub, fd, fs, 0, 2);  case 0x27: return set(kMsub, fd, fs, 0, 2);
        case 0x28: return set(kAdd, fd, fs, ft, 0); case 0x29: return set(kMadd, fd, fs, ft, 0);
        case 0x2A: return set(kMul, fd, fs, ft, 0); case 0x2B: return set(kMax, fd, fs, ft, 0);
        case 0x2C: return set(kSub, fd, fs, ft, 0); case 0x2D: return set(kMsub, fd, fs, ft, 0);
        case 0x2E: return set(kOpmsub, fd, fs, ft, 0);
        case 0x2F: return set(kMin, fd, fs, ft, 0);
        }
        return r;
    }
    const unsigned f2 = ((i >> 4) & 0x7C) | (i & 3);
    bcl = (f2 < 0x10 || (f2 >= 0x18 && f2 < 0x1C)) ? (f2 & 3) : 4;
    if (f2 < 0x10) { static const std::uint8_t a[4] = {kAdd, kSub, kMadd, kMsub}; return set(a[f2 >> 2], 32, fs, ft, 2); }
    if (f2 < 0x14) return set(kItof, ft, fs, 0, 0);
    if (f2 < 0x18) return set(kFtoi, ft, fs, 0, 0);
    if (f2 < 0x1C) return set(kMul, 32, fs, ft, 2);
    switch (f2) {
    case 0x1C: return set(kMul, 32, fs, 0, 2);  case 0x1D: return set(kAbs, ft, fs, 0, 0);
    case 0x1E: return set(kMul, 32, fs, 0, 2);
    case 0x20: return set(kAdd, 32, fs, 0, 2);  case 0x21: return set(kMadd, 32, fs, 0, 2);
    case 0x22: return set(kAdd, 32, fs, 0, 2);  case 0x23: return set(kMadd, 32, fs, 0, 2);
    case 0x24: return set(kSub, 32, fs, 0, 2);  case 0x25: return set(kMsub, 32, fs, 0, 2);
    case 0x26: return set(kSub, 32, fs, 0, 2);  case 0x27: return set(kMsub, 32, fs, 0, 2);
    case 0x28: return set(kAdd, 32, fs, ft, 0); case 0x29: return set(kMadd, 32, fs, ft, 0);
    case 0x2A: return set(kMul, 32, fs, ft, 0); case 0x2C: return set(kSub, 32, fs, ft, 0);
    case 0x2D: return set(kMsub, 32, fs, ft, 0);
    case 0x2E: return set(kOpmula, 32, fs, ft, 0);
    case 0x30: return set(kMove, ft, fs, 0, 0);
    case 0x31: return set(kMr32, ft, fs, 0, 0);
    }
    return r;   // vnop, Q unit, vwaitq, R ops: no VU register destination ours binds
}

// ---- the alignment of ours' operations with retail's COP2 stream (mini_check --vu-record) ----
namespace {
std::mutex g_votes_mutex;
std::map<std::uint32_t, std::map<std::uint32_t, std::uint32_t>> g_votes;   // site -> retail word -> votes
std::map<std::uint32_t, std::uint32_t> g_base_d;                          // site -> votes for "the base is ours' d"
std::map<std::uint32_t, std::uint32_t> g_site_pc;                         // site -> a retail pc (for the report)
unsigned g_align_ok = 0, g_align_fail = 0, g_unmatched_ops = 0;
// Does ours' operation e compute what retail instruction r (word w, sources rs / rt) computed? Same kind and lanes,
// and the source values agree in the written lanes (ft through its broadcast lane; Q / I sources are not checked).
bool same_op(const Ev& e, const RetailEv& r) {
    const Retail d = decode(r.word);
    if (d.kind != e.kind || d.mask != e.mask) return false;
    for (unsigned k = 0; k < 4; ++k) {
        if (!(e.mask & (1u << k))) continue;
        const bool op3 = e.kind == kOpmula || e.kind == kOpmsub;
        if (e.has & 1) {
            const unsigned ls = op3 ? (k + 1) % 3 : k;   // opmula / opmsub read fs.yzx
            if (e.s.w[op3 ? ls : k] != r.s.w[op3 ? ls : k]) return false;
        }
        if ((e.has & 2) && d.ft != 0 && d.bcl != 4) {
            if (op3) { if (e.t.w[(k + 2) % 3] != r.t.w[(k + 2) % 3]) return false; }
            else if (e.t.w[k] != r.t.w[d.bcl < 4 ? d.bcl : k]) return false;
        }
    }
    return true;
}
bool align(const std::vector<RetailEv>& R, const std::vector<Ev>& O, bool why, bool dump) {
    if (dump) {   // --vu-dump: both streams
        for (const RetailEv& r : R) { const Retail d = decode(r.word); std::printf("  R %08X %08X k%u m%u d%u s%u t%u\n", r.pc, r.word, d.kind, d.mask, d.dst, d.fs, d.ft); }
        for (const Ev& e : O) std::printf("  O %s %x %08X k%u m%u\n", e.kind ? "op" : "interp", e.a, e.b, e.kind, e.mask);
    }
    std::size_t i = 0;
    std::vector<std::pair<std::uint32_t, std::size_t>> got;   // site -> index into R
    std::vector<char> matched(R.size(), 0);
    for (std::size_t j = 0; j < O.size(); ++j) {
        const Ev& e = O[j];
        const std::size_t i0 = i;
        if (e.kind == 0) {
            while (i < R.size() && !(R[i].pc == e.a && R[i].word == e.b)) ++i;
            if (i < R.size()) matched[i] = 1;
        } else {
            // a native operation: the next retail operation of its kind and mask within a window; none there (ours does
            // something retail does otherwise, e.g. on the FPU): the operation stays unbound and the stream position stays
            std::size_t k = i;
            for (; k < R.size() && k < i + 4096; ++k)
                if (same_op(e, R[k])) break;
            if (k >= R.size() || k >= i + 4096) { ++g_unmatched_ops; continue; }
            i = k;
            got.push_back({e.a, i});
            matched[i] = 1;
        }
        if (i >= R.size()) {
            if (why) {   // --vu-why: where the alignment stopped
                std::printf("  vu-align: ours event %zu of %zu (%s kind %u mask %u site %x) not found after retail %zu of %zu:", j, O.size(),
                            e.kind ? "op" : "interp", e.kind, e.mask, e.a, i0, R.size());
                for (std::size_t k = i0; k < R.size() && k < i0 + 6; ++k) {
                    const Retail d = decode(R[k].word);
                    std::printf(" [%08X %08X k%u m%u]", R[k].pc, R[k].word, d.kind, d.mask);
                }
                std::printf("\n");
            }
            return false;
        }
        ++i;
    }
    // Which previous value the destination had: the last earlier write to that register in the retail stream. An lqc2 /
    // qmtc2 (MWCC loading the object, often the destination itself), or an operation ours has no event for, means the
    // register's content is not one ours tracks: the base stays ours' d (the C value) for that site.
    auto writes = [&](std::size_t j, unsigned n) {
        const std::uint32_t w = R[j].word;
        if ((w >> 26) == 0x36) return ((w >> 16) & 31) == n ? 1 : 0;                      // lqc2
        if ((w >> 26) == 0x12 && !(w & (1u << 25))) return (((w >> 21) & 31) == 5 && ((w >> 11) & 31) == n) ? 1 : 0;   // qmtc2
        const Retail d = decode(w);
        return d.kind != 0 && d.dst == n ? 2 : 0;
    };
    for (const auto& [site, k] : got) {
        ++g_votes[site][R[k].word];
        g_site_pc[site] = R[k].pc;
        const Retail d = decode(R[k].word);
        if (d.dst == 0 || d.dst == 32) continue;
        for (std::size_t j = k; j-- > 0;) {
            const int w = writes(j, d.dst);
            if (w == 0) continue;
            if (w == 1 || !matched[j]) ++g_base_d[site];
            break;
        }
    }
    return true;
}
// The votes, appended to `path` ("V site word pc votes" lines; tools/native/vubind_table.py makes the table, so that
// several runs over parts of the captures add up).
}  // namespace

void align_and_vote(const std::vector<RetailEv>& R, const std::vector<Ev>& O, bool why, bool dump) {
    const std::lock_guard<std::mutex> lock(g_votes_mutex);
    if (align(R, O, why, dump)) ++g_align_ok; else ++g_align_fail;
}

bool write_votes(const char* path) {
    const std::lock_guard<std::mutex> lock(g_votes_mutex);
    std::FILE* f = std::fopen(path, "a");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path); return false; }
    for (const auto& [site, words] : g_votes)
        for (const auto& [w, n] : words) std::fprintf(f, "V %x %08x %08x %u %u\n", site, w, g_site_pc[site], n, g_base_d[site]);
    std::fprintf(f, "A %u %u\n", g_align_ok, g_align_fail);
    std::fclose(f);
    std::fprintf(stderr, "vubind: %zu sites voted; %u calls aligned, %u not; %u native operations without a retail match -> %s\n",
                 g_votes.size(), g_align_ok, g_align_fail, g_unmatched_ops, path);
    return true;
}

}  // namespace b3host::vubind
