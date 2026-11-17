// vu0_unit.cpp -- see vu0_unit.h and platform_vu0.h.
#include "vu0_unit.h"

#include "sha256.h"
#include "b3host/fenv.h"
#include "b3host/host.h"

#if !defined(B3_VU0_INTERP)
#define B3_VU0_INTERP 0
#endif
#if B3_VU0_INTERP
#include "b3asset/vif.h"
#include "b3asset/vu1.h"
#include "hw_model_dev.h"   // the dev DMA chain walker (H4's check against the SHA-256 path)
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace b3native {
namespace {

std::string hex(std::uint32_t x) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", x);
    return b;
}
const char* const kNoInterp = "this build has no VU0 interpreter (the shipped game runs the generated programs only; "
                              "a dev build with it: make VU0_INTERP=1)";

Vu0Unit::Timing timing_from_env() {
    const char* e = std::getenv("B3_VU0_TIMING");
    if (!e || !*e || std::strcmp(e, "pair") == 0) return Vu0Unit::Timing::kPair;
    if (std::strcmp(e, "hw") == 0) return Vu0Unit::Timing::kHw;
    if (std::strcmp(e, "both") == 0) return Vu0Unit::Timing::kBoth;
    b3host::fatal(std::string("vu0: B3_VU0_TIMING: unknown value ") + e + " (pair, hw, both)");
}
Vu0Unit::Aot aot_from_env() {
    const char* e = std::getenv("B3_VU0_AOT");
    // without the interpreter the generated programs are the only way; with it, 0 stays the default (native_boot,
    // mini_check: the reference) and native_play sets 1
    if (!e || !*e) return B3_VU0_INTERP ? Vu0Unit::Aot::kOff : Vu0Unit::Aot::kOn;
    if (std::strcmp(e, "1") == 0) return Vu0Unit::Aot::kOn;
    Vu0Unit::Aot a;
    if (std::strcmp(e, "0") == 0) a = Vu0Unit::Aot::kOff;
    else if (std::strcmp(e, "check") == 0) a = Vu0Unit::Aot::kCheck;
    else b3host::fatal(std::string("vu0: B3_VU0_AOT: unknown value ") + e + " (0, 1, check)");
    if (!B3_VU0_INTERP) b3host::fatal(std::string("vu0: B3_VU0_AOT=") + e + " needs the interpreter: " + kNoInterp);
    return a;
}

void regs_in(vu0aot::Core& c, const b3asm::State& s) {
    for (unsigned r = 0; r < 32; ++r) for (unsigned l = 0; l < 4; ++l) c.vf[r][l] = s.vf[r].w[l];
    c.vf[0][0] = c.vf[0][1] = c.vf[0][2] = 0;
    c.vf[0][3] = 0x3F800000u;
    c.vi[0] = 0;
    for (unsigned r = 1; r < 16; ++r) c.vi[r] = s.vi[r];
    for (unsigned l = 0; l < 4; ++l) c.acc[l] = s.acc.w[l];
    c.I = s.i;
    c.Q = s.q;
    c.R = s.r;
}
void regs_out(const vu0aot::Core& c, b3asm::State& s) {
    for (unsigned r = 1; r < 32; ++r) for (unsigned l = 0; l < 4; ++l) s.vf[r].w[l] = c.vf[r][l];
    for (unsigned r = 1; r < 16; ++r) s.vi[r] = c.vi[r];
    for (unsigned l = 0; l < 4; ++l) s.acc.w[l] = c.acc[l];
    s.i = c.I;
    s.q = c.Q;
    s.r = c.R;
}

// one 64-bit hash of the whole micro memory (the lookup cache's key; a miss is settled by SHA-256)
std::uint64_t fnv64(const std::uint8_t* p, std::size_t n) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (std::size_t k = 0; k + 8 <= n; k += 8) {
        std::uint64_t w;
        std::memcpy(&w, p + k, 8);
        h = (h ^ w) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    return h;
}

#if B3_VU0_INTERP
template <class A, class B>
void copy_arch(const A& from, B& to) {
    std::memcpy(to.vf, from.vf, sizeof to.vf);
    std::memcpy(to.vi, from.vi, sizeof to.vi);
    std::memcpy(to.acc, from.acc, sizeof to.acc);
    to.I = from.I; to.Q = from.Q; to.P = from.P; to.R = from.R;
    to.mac = from.mac; to.status = from.status; to.clip = from.clip;
}
#endif

// the architectural state two runs of a launch can differ in: the first difference, or "" when equal
template <class A, class B>
std::string first_difference(const A& a, const std::uint32_t* am, const B& b, const std::uint32_t* bm) {
    for (unsigned r = 1; r < 32; ++r)
        for (unsigned l = 0; l < 4; ++l)
            if (a.vf[r][l] != b.vf[r][l]) return "vf" + std::to_string(r) + "[" + std::to_string(l) + "] " + hex(a.vf[r][l]) + " vs " + hex(b.vf[r][l]);
    for (unsigned r = 1; r < 16; ++r)
        if (a.vi[r] != b.vi[r]) return "vi" + std::to_string(r) + " " + hex(a.vi[r]) + " vs " + hex(b.vi[r]);
    for (unsigned l = 0; l < 4; ++l)
        if (a.acc[l] != b.acc[l]) return "acc[" + std::to_string(l) + "]";
    if (a.Q != b.Q) return "Q " + hex(a.Q) + " vs " + hex(b.Q);
    if (a.I != b.I) return "I";
    if (a.P != b.P) return "P";
    if (a.R != b.R) return "R";
    if (a.mac != b.mac) return "MAC " + hex(a.mac) + " vs " + hex(b.mac);
    if ((a.status & 0xFFF) != (b.status & 0xFFF)) return "STATUS " + hex(a.status) + " vs " + hex(b.status);
    if (a.clip != b.clip) return "CLIP " + hex(a.clip) + " vs " + hex(b.clip);
    for (unsigned k = 0; k < 256 * 4; ++k)
        if (am[k] != bm[k]) return "data qword " + std::to_string(k / 4) + " lane " + std::to_string(k % 4);
    if (a.tpc != b.tpc) return "tpc " + std::to_string(a.tpc) + " vs " + std::to_string(b.tpc);
    return "";
}

// rt1: the generated program's float operations on the host's unit (b3fp::vufast / hw: ps2float_hw.h)
void run_generated(const vu0aot::Image& img, vu0aot::Core& c, std::uint32_t* data, std::uint32_t entry) {
    [[maybe_unused]] std::optional<b3host::Ps2FloatEnv> fenv;
#if defined(B3HOST_HAVE_PS2_FLOAT_ENV)
    if (b3fp::vufast::kHw) fenv.emplace();
#endif
    if (!img.run(c, data, entry)) b3host::fatal(std::string("vu0: generated program ") + img.name + ": entry " + std::to_string(entry) + " refused");
}
}  // namespace

#if B3_VU0_INTERP
const bool Vu0Unit::kHasInterpreter = true;
// a build without the generated programs (mini_check, test programs) links this empty table; only interpreter builds
// may (without the interpreter the strong one in gen/native/vu0/vu0_aot_gen.cpp must be linked)
__attribute__((weak)) const vu0aot::Image* vu0aot::images(unsigned* n) { *n = 0; return nullptr; }

// the shared VU core with VU0's sizes, run on copies of the settled state (Vu0Unit::st_) and the data memory
struct Vu0Unit::Interp {
    b3asset::VuMemory mem, alt_mem;
    b3asset::VuMicroprogram prog;
    std::uint64_t prog_gen = 0;
    b3asset::Vu1 core{mem};
    std::unique_ptr<b3asset::Vu1> alt;   // B3_VU0_TIMING=both: the stall model on a copy
    Interp(bool pair_timing, bool both) {
        core.pc_mask = 511;
        core.qw_mask = 255;
        core.pair_timing = pair_timing;
        if (both) {
            alt = std::make_unique<b3asset::Vu1>(alt_mem);
            alt->pc_mask = 511;
            alt->qw_mask = 255;
            alt->pair_timing = false;
        }
    }
    void load(const std::uint8_t* micro, std::uint64_t gen) {
        if (prog_gen == gen) return;
        for (unsigned k = 0; k < 512; ++k) {
            std::memcpy(&prog.lower[k], micro + 8 * k, 4);
            std::memcpy(&prog.upper[k], micro + 8 * k + 4, 4);
        }
        prog.loaded = 512;
        prog_gen = gen;
    }
};
#else
const bool Vu0Unit::kHasInterpreter = false;
struct Vu0Unit::Interp {};
#endif

Vu0Unit::Vu0Unit() : Vu0Unit(timing_from_env()) {}
Vu0Unit::Vu0Unit(Timing t) : timing_(t) {
    aot_ = aot_from_env();
    if (aot_ != Aot::kOff && timing_ != Timing::kPair)
        b3host::fatal("vu0: the generated programs (B3_VU0_AOT=1 / check) reproduce the pair timing only (B3_VU0_TIMING=pair)");
#if B3_VU0_INTERP
    interp_ = std::make_unique<Interp>(timing_ != Timing::kHw, timing_ == Timing::kBoth);
#else
    if (timing_ != Timing::kPair) b3host::fatal(std::string("vu0: B3_VU0_TIMING=hw / both needs the interpreter: ") + kNoInterp);
#endif
}
Vu0Unit::~Vu0Unit() = default;

const vu0aot::Image* Vu0Unit::aot_lookup() {
    if (aot_gen_ == micro_gen_) return aot_image_;
    aot_gen_ = micro_gen_;
    const std::uint64_t key = fnv64(micro_, sizeof micro_);
    if (auto it = seen_.find(key); it != seen_.end()) return aot_image_ = it->second;
    ++sha_lookups_;
    aot_image_ = nullptr;
    unsigned n = 0;
    const vu0aot::Image* im = vu0aot::images(&n);
    for (unsigned k = 0; k < n && !aot_image_; ++k)
        if (sha256_hex(sha256(micro_, 8u * im[k].pairs)) == im[k].sha256) aot_image_ = &im[k];
    if (seen_.size() >= 256) seen_.clear();   // retail switches between four images: a handful of keys
    seen_.emplace(key, aot_image_);
    return aot_image_;
}

void Vu0Unit::no_program(std::uint32_t entry, std::uint32_t caller_pc, const vu0aot::Image* img) const {
    std::string m = "vu0: VCALLMS entry " + std::to_string(entry) + " from pc " + hex(caller_pc) + ": ";
    if (img) {
        m += "the micro memory holds image " + std::string(img->name) + " (" + img->sha256 + "), whose generated program covers pairs 0.." +
             std::to_string(img->pairs - 1) + " only";
    } else {
        const std::uint32_t up = upload_pairs_ ? upload_pairs_ : 512u;
        m += "no generated program for the VU0 micro image in micro memory (pairs 0.." + std::to_string(up - 1) + " sha256 " +
             sha256_hex(sha256(micro_, 8u * up)) + "; all 512 pairs sha256 " + sha256_hex(sha256(micro_, sizeof micro_)) + "); generated:";
        unsigned n = 0;
        const vu0aot::Image* im = vu0aot::images(&n);
        for (unsigned k = 0; k < n; ++k) m += std::string(k ? ", " : " ") + im[k].name + " (" + std::to_string(im[k].pairs) + " pairs, " + im[k].sha256 + ")";
        if (n == 0) m += " none (no gen/native/vu0/vu0_aot_gen.cpp linked)";
    }
    m += B3_VU0_INTERP ? "; native_play --strict: every launch must run a generated program" : std::string("; ") + kNoInterp;
    b3host::fatal(m);
}

void Vu0Unit::call(b3asm::State& s, std::uint32_t entry, std::uint32_t caller_pc) {
    ++calls_;
    const std::uint32_t e = entry & 511u;
    const vu0aot::Image* found = aot_lookup();
    const vu0aot::Image* img = found && e < found->pairs ? found : nullptr;
    if (!img) {
        ++uncovered_;
        if (!B3_VU0_INTERP || strict_) no_program(e, caller_pc, found);
        if (aot_ != Aot::kOff && uncovered_ == 1)
            std::fprintf(stderr, "vu0: launch %llu (entry %u from pc %s): no generated program, the interpreter runs it (counted)\n",
                         static_cast<unsigned long long>(calls_), e, hex(caller_pc).c_str());
    }
    regs_in(st_, s);
    EntryStats& es = by_entry_[(std::uint64_t(caller_pc) << 16) | (entry & 0xFFFFu)];
    ++es.calls;
    if (img && aot_ == Aot::kOn) {
        ++generated_;
        const std::uint64_t pairs0 = st_.stats.pairs, flag0 = st_.stats.flag_reads_prelaunch;
        run_generated(*img, st_, data_, e);
        es.pairs += st_.stats.pairs - pairs0;
        es.prelaunch_flag_reads += st_.stats.flag_reads_prelaunch - flag0;
        regs_out(st_, s);
        return;
    }
#if B3_VU0_INTERP
    ++interpreted_;
    Interp& in = *interp_;
    in.load(micro_, micro_gen_);
    b3asset::Vu1& c = in.core;
    copy_arch(st_, c);
    std::memcpy(in.mem.w, data_, sizeof data_);
    // check: the generated program on a copy, from the same state
    std::optional<vu0aot::Core> chk;
    alignas(16) std::uint32_t chk_data[256 * 4];
    if (img) {
        chk.emplace(st_);
        std::memcpy(chk_data, data_, sizeof data_);
    }
    if (in.alt) {
        copy_arch(st_, *in.alt);
        std::memcpy(in.alt_mem.w, data_, sizeof data_);
    }
    const std::uint64_t pairs0 = c.stats.pairs, stall0 = c.stats.stall_cycles, flag0 = c.stats.flag_reads_prelaunch;
    std::string why;
    // a runaway guard: 4096 pairs per launch as cycles here (stalls count), with headroom
    if (!c.run(in.prog, e, 0, 0, nullptr, &why, 4096u * 64u))
        b3host::fatal("vu0: VCALLMS microprogram at pair " + std::to_string(entry) + " from pc " + hex(caller_pc) + ": " + why);
    c.settle();
    copy_arch(c, st_);
    st_.tpc = c.tpc;
    std::memcpy(data_, in.mem.w, sizeof data_);
    es.pairs += c.stats.pairs - pairs0;
    es.stall_cycles += c.stats.stall_cycles - stall0;
    es.prelaunch_flag_reads += c.stats.flag_reads_prelaunch - flag0;
    if (chk) {
        ++aot_checked_;
        run_generated(*img, *chk, chk_data, e);
        const std::string d = first_difference(st_, data_, *chk, chk_data);
        if (!d.empty() && ++aot_diffs_ <= 20)
            std::fprintf(stderr, "vu0: aot vs interpreter differ: call %llu image %s entry %u from pc %s: %s\n",
                         static_cast<unsigned long long>(calls_), img->name, e, hex(caller_pc).c_str(), d.c_str());
    }
    if (in.alt) {
        const std::uint64_t astall0 = in.alt->stats.stall_cycles;
        if (!in.alt->run(in.prog, e, 0, 0, nullptr, &why, 4096u * 64u))
            b3host::fatal("vu0: VCALLMS microprogram (hw timing copy) at pair " + std::to_string(entry) + ": " + why);
        in.alt->settle();
        in.alt->tpc = c.tpc;   // (the comparison is of registers, flags and memory)
        es.stall_cycles += in.alt->stats.stall_cycles - astall0;
        const std::string d = first_difference(c, in.mem.w, *in.alt, in.alt_mem.w);
        if (!d.empty() && ++timing_diffs_ <= 20)
            std::fprintf(stderr, "vu0: timing pair vs hw differ: call %llu entry %u from pc %s: %s\n",
                         static_cast<unsigned long long>(calls_), entry, hex(caller_pc).c_str(), d.c_str());
    }
    regs_out(st_, s);
#else
    b3host::fatal("vu0: unreachable: no generated program and no interpreter");
#endif
}

#if B3_VU0_INTERP
// dev builds: one word of a VIF0 stream (the dev walker's, vif0_upload's check): MPG writes micro memory
void Vu0Unit::vif0_word(std::uint32_t addr, std::uint32_t w) {
    ++vif_words_;
    if (vif_mpg_left_) {   // MPG payload: lower word, then upper word, per pair
        if (!vif_mpg_have_lower_) { vif_mpg_lower_ = w; vif_mpg_have_lower_ = true; return; }
        const std::uint32_t a = (vif_mpg_addr_ & 511u) * 8u;
        std::memcpy(micro_ + a, &vif_mpg_lower_, 4);
        std::memcpy(micro_ + a + 4, &w, 4);
        vif_mpg_have_lower_ = false;
        ++vif_mpg_addr_;
        ++vif_mpg_pairs_;
        --vif_mpg_left_;
        ++micro_gen_;
        return;
    }
    const std::uint32_t cmd = (w >> 24) & 0x7Fu, num = (w >> 16) & 0xFFu, imm = w & 0xFFFFu;
    switch (cmd) {
    case 0x00: case 0x01: case 0x04: case 0x05: case 0x07: case 0x10:   // NOP STCYCL ITOP STMOD MARK FLUSHE
        return;
    case 0x4A: {   // MPG: num pairs (0 = 256) at pair address imm; the payload starts at the next 64-bit boundary
        if (((addr + 4u) & 7u) != 0) b3host::fatal("vu0: VIF0 MPG at " + hex(addr) + " with its payload off a 64-bit boundary");
        vif_mpg_left_ = num ? num : 256u;
        vif_mpg_addr_ = imm;
        vif_mpg_have_lower_ = false;
        if ((imm & 511u) == 0) upload_pairs_ = 0;   // (for the message of an unknown image: what this upload wrote)
        const std::uint32_t end = (imm & 511u) + vif_mpg_left_;
        upload_pairs_ = std::max(upload_pairs_, end > 512u ? 512u : end);
        return;
    }
    default:
        b3host::fatal("vu0: VIF0 code " + hex(w) + " at " + hex(addr) + " (only MPG and the no-effect codes are modelled)");
    }
}
#else
void Vu0Unit::vif0_word(std::uint32_t, std::uint32_t) {
    b3host::fatal(std::string("vu0: VIF0 words: the shipped game loads VU0 images by address (vif0_upload); ") + kNoInterp);
}
#endif

void Vu0Unit::vif0_upload(std::uint32_t chcr, std::uint32_t tadr, const std::function<std::uint32_t(std::uint32_t)>& read32) {
    ++vif0_uploads_;
    unsigned n = 0;
    const vu0aot::Image* im = vu0aot::images(&n);
    const vu0aot::Image* by_addr = nullptr;
    if (((chcr >> 2) & 3u) == 1u)   // chain mode: TADR is the image's DMA chain
        for (unsigned k = 0; k < n && !by_addr; ++k)
            if (std::strtoul(im[k].name, nullptr, 16) == (tadr & 0x01FFFFF0u)) by_addr = &im[k];
#if B3_VU0_INTERP
    // dev: the words through VIF0's MPG into micro memory (the interpreter's program), then the SHA-256 path's image
    // must be the address's (an address that is none of the four: both none)
    std::uint32_t regs[4] = {chcr, 0, 0, tadr};
    walk_source_chain(regs, "VIF0", read32, [this](std::uint32_t a, std::uint32_t w) { vif0_word(a, w); });
    const vu0aot::Image* by_sha = aot_lookup();
    ++vif0_checked_;
    if (by_sha != by_addr)
        b3host::fatal(std::string("vu0: VIF0 upload from ") + hex(tadr) + ": the address gives image " + (by_addr ? by_addr->name : "none") +
                      ", the micro memory's SHA-256 " + (by_sha ? by_sha->name : "none"));
#else
    (void)read32;
    if (!by_addr)
        b3host::fatal("vu0: VIF0 transfer CHCR " + hex(chcr) + " TADR " + hex(tadr) +
                      ": not an upload of one of retail's four VU0 images (the only VIF0 transfers the game makes)");
    ++micro_gen_;
    aot_gen_ = micro_gen_;   // the micro memory now holds this image: no lookup
    aot_image_ = by_addr;
    upload_pairs_ = by_addr->pairs;
#endif
}

void Vu0Unit::summary(std::FILE* out, const char* prefix) const {
    std::fprintf(out, "%svu0: %llu launches: %llu generated, %llu interpreted, %llu without a generated program; aot %s, interpreter %s",
                 prefix, static_cast<unsigned long long>(calls_), static_cast<unsigned long long>(generated_),
                 static_cast<unsigned long long>(interpreted_), static_cast<unsigned long long>(uncovered_),
                 aot_ == Aot::kOn ? "on" : aot_ == Aot::kCheck ? "check" : "off", B3_VU0_INTERP ? "compiled in" : "compiled out");
    if (aot_ == Aot::kCheck)
        std::fprintf(out, "; %llu checked against the interpreter, %llu differences", static_cast<unsigned long long>(aot_checked_),
                     static_cast<unsigned long long>(aot_diffs_));
    std::fprintf(out, "\n");
}

void Vu0Unit::report(std::FILE* out) const {
    std::fprintf(out, "vu0: %llu VIF0 uploads by address (%llu checked against the SHA-256 path)\n",
                 static_cast<unsigned long long>(vif0_uploads_), static_cast<unsigned long long>(vif0_checked_));
    std::fprintf(out, "vu0: %llu launches, timing %s, %llu timing differences; VIF0 %llu words, %llu MPG pairs; %llu image lookups by SHA-256\n",
                 static_cast<unsigned long long>(calls_),
                 timing_ == Timing::kHw ? "hw" : timing_ == Timing::kBoth ? "pair (+hw compared)" : "pair",
                 static_cast<unsigned long long>(timing_diffs_), static_cast<unsigned long long>(vif_words_),
                 static_cast<unsigned long long>(vif_mpg_pairs_), static_cast<unsigned long long>(sha_lookups_));
    summary(out, "");
    for (const auto& [k, e] : by_entry_)
        std::fprintf(out, "vu0: pc %08X entry %3u: %llu launches, %llu pairs, %llu stall cycles (hw), %llu pre-launch flag reads\n",
                     static_cast<unsigned>(k >> 16), static_cast<unsigned>(k & 0xFFFFu), static_cast<unsigned long long>(e.calls),
                     static_cast<unsigned long long>(e.pairs), static_cast<unsigned long long>(e.stall_cycles),
                     static_cast<unsigned long long>(e.prelaunch_flag_reads));
}

}  // namespace b3native
