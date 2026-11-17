// vu0_unit.h -- one VU0 in micro mode (vu0 lane, 2026-11-07; from native3's platform_vu0): the 4 KB micro and data
// memories, a launch (VCALLMS / VCALLMSR) on a b3asm::State, VIF0's MPG, timing modes and counters. The native platform
// keeps one (src/native/platform_vu0.h, which documents the semantics); mini_check keeps one per side (retail's mini and
// ours), seeded from a capture. Errors are b3host::fatal.
//
// vu0n (2026-11-10, goal: VU0 fully native): every launch runs one of retail's four microprograms as generated C++
// (gen/native/vu0/vu0_aot_gen.cpp, src/native/vu0_aot_rt.h), found by the SHA-256 of the micro memory's image. The
// interpreter (the shared VU core, src/b3asset/vu1.cpp) is compiled in only with -DB3_VU0_INTERP=1 (make VU0_INTERP=1:
// dev builds, tests, mini_check); without it a launch no generated program covers (an unknown image, an entry past the
// image's pairs) is a fatal error naming the image's hash, and B3_VU0_AOT=0 / check and B3_VU0_TIMING=hw / both are
// refused. The class layout does not depend on the flag (the interpreter's state is behind a pointer).
#ifndef B3NATIVE_VU0_UNIT_H
#define B3NATIVE_VU0_UNIT_H

#include "vu0_aot_rt.h"
#include "b3host/eeasm.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace b3native {

class Vu0Unit {
public:
    enum class Timing { kPair, kHw, kBoth };
    // B3_VU0_AOT: 1 (the default; the only mode without the interpreter): a launch runs the generated program of the
    // image in micro memory; 0 (interpreter builds): the interpreter only; check (interpreter builds): the interpreter
    // runs (and stays authoritative) and the generated program runs on a copy of the state and data memory, compared
    // after every launch (first differences printed, counted in the report).
    enum class Aot { kOff, kOn, kCheck };
    static const bool kHasInterpreter;   // compiled with B3_VU0_INTERP=1
    Vu0Unit();                      // timing from B3_VU0_TIMING (pair by default)
    explicit Vu0Unit(Timing t);
    ~Vu0Unit();
    std::uint8_t* micro_bytes() { micro_changed(); upload_pairs_ = 512; return micro_; }   // 512 pairs, lower word first
    std::uint8_t* data_bytes() { return reinterpret_cast<std::uint8_t*>(data_); }   // 256 qwords
    void micro_changed() { ++micro_gen_; }
    void call(b3asm::State& s, std::uint32_t entry, std::uint32_t caller_pc = 0);
    void vif0_word(std::uint32_t addr, std::uint32_t w);   // dev builds (B3_VU0_INTERP): one VIF0 word, MPG to micro memory
    // H4 (the device-state work): a VIF0 transfer (CHCR, TADR; CGtVu0CodeManager's chain, CHCR 0x145) loads the
    // retail VU0 image whose DMA chain is at TADR -- one of the four the generated programs cover (0x4787D0,
    // 0x479610, 0x47F6D0, 0x4839E0) -- and the launches run its program; any other transfer is fatal. No microcode
    // word is read or kept: the micro memory's bytes are not written. In an interpreter build (dev, B3_VU0_INTERP=1)
    // the chain's words are also walked into micro memory through VIF0's MPG (vif0_word, from guest memory via
    // read32) and the SHA-256 path's image must be the one the address gives (fatal otherwise).
    void vif0_upload(std::uint32_t chcr, std::uint32_t tadr, const std::function<std::uint32_t(std::uint32_t)>& read32);
    std::uint64_t vif0_uploads() const { return vif0_uploads_; }
    std::uint64_t vif0_checked() const { return vif0_checked_; }
    // strict (native_play --strict): a launch without a generated program is fatal in an interpreter build too
    void set_strict(bool on) { strict_ = on; }
    Aot aot() const { return aot_; }
    std::uint64_t calls() const { return calls_; }
    std::uint64_t timing_diffs() const { return timing_diffs_; }
    std::uint64_t aot_launches() const { return generated_; }      // results from the generated programs
    std::uint64_t interpreted() const { return interpreted_; }     // results from the interpreter
    std::uint64_t uncovered() const { return uncovered_; }         // launches no generated program covers
    std::uint64_t aot_checked() const { return aot_checked_; }
    std::uint64_t aot_diffs() const { return aot_diffs_; }
    void report(std::FILE* out) const;   // B3_VU0_STATS=1: everything, per (caller pc, entry)
    // one line for an end-of-run log: "<prefix>vu0: N launches: G generated, I interpreted, U without a generated
    // program; aot on|check|off, interpreter compiled in|out[; C checked against the interpreter, D differences]"
    void summary(std::FILE* out, const char* prefix) const;

    struct EntryStats { std::uint64_t calls = 0, pairs = 0, stall_cycles = 0, prelaunch_flag_reads = 0; };

private:
    alignas(16) std::uint32_t data_[256 * 4] = {};
    alignas(16) std::uint8_t micro_[4096] = {};
    vu0aot::Core st_;                     // the settled state between launches (flags persist across them)
    Timing timing_;
    Aot aot_;
    bool strict_ = false;
    std::uint64_t calls_ = 0, timing_diffs_ = 0;
    std::uint64_t generated_ = 0, interpreted_ = 0, uncovered_ = 0, aot_checked_ = 0, aot_diffs_ = 0;
    std::map<std::uint64_t, EntryStats> by_entry_;   // (caller pc << 16) | entry
    std::uint32_t vif_mpg_left_ = 0, vif_mpg_addr_ = 0, vif_mpg_lower_ = 0;
    bool vif_mpg_have_lower_ = false;
    std::uint64_t vif_mpg_pairs_ = 0, vif_words_ = 0;
    std::uint32_t upload_pairs_ = 0;      // pairs 0 .. upload_pairs_-1 written since the last MPG to pair 0
    std::uint64_t micro_gen_ = 1, aot_gen_ = 0;   // aot_image_ is valid for micro generation aot_gen_
    const vu0aot::Image* aot_image_ = nullptr;    // the generated image the micro memory holds, or null
    std::unordered_map<std::uint64_t, const vu0aot::Image*> seen_;   // a hash of all 4 KB -> its match (or null)
    std::uint64_t sha_lookups_ = 0;
    std::uint64_t vif0_uploads_ = 0, vif0_checked_ = 0;
    const vu0aot::Image* aot_lookup();
    [[noreturn]] void no_program(std::uint32_t entry, std::uint32_t caller_pc, const vu0aot::Image* img) const;
    struct Interp;                        // the interpreter cores (B3_VU0_INTERP=1 only; null otherwise)
    std::unique_ptr<Interp> interp_;
};

}  // namespace b3native

#endif
