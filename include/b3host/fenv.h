// b3host/fenv.h -- host FP environment for native game code: the EE's rounding for plain C float arithmetic.
//
// Game code outside GtMath uses ordinary C float operators. On the EE those compile to COP1 instructions that round
// toward zero and treat denormals as zero [C EE Core User's Manual v6.0 8.1 p161, 8.7 p164]. Running native code
// under MXCSR RZ + FTZ + DAZ gets the host close to that:
//   - add/sub/mul: IEEE round-toward-zero of the exact result -- equal to the PS2 float model (ps2float.h) except
//     for its double staging (ps2float.h D3) and its clamping of exponent-255 inputs (D1).
//   - div/sqrt: RZ, where the float model rounds to nearest (ps2float.h D4).
//   - x/0 still gives Inf and 0/0 NaN (the EE gives +-Fmax): no MXCSR setting fixes that [L]; such sites need
//     b3fp::div. Compile game code with -ffp-contract=off: the EE has no fused multiply-add (madd.s rounds the
//     product first, docs/specs/ps2-float.md §4.3) and contraction would change results.
// Use: `b3host::Ps2FloatEnv env;` at the entry of a native override (the backend's thunk can do it once).
//
// AArch64 (mac1: Apple Silicon, Linux arm64): FPCR RMode = 0b11 (round toward zero) + FZ. With FPCR.AH = 0 (the reset
// value; FEAT_AFP's FIZ/AH stay 0) FZ flushes denormal inputs *and* outputs to zero, i.e. x86's FTZ + DAZ
// [L Arm ARM DDI0487: FPCR RMode bits 23:22, FZ bit 24; FPRound pseudocode]. Arm tests a result for tininess before
// rounding, x86 after; under round-toward-zero the two agree (rounding toward zero never lifts a tiny value to FLT_MIN).
// Checked by tests/test_fenv.cpp against b3fp::ref (aarch64 under qemu-aarch64 and x86-64 here; on a Mac: docs/mac.md).
#ifndef B3HOST_FENV_H
#define B3HOST_FENV_H

#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#define B3HOST_HAVE_PS2_FLOAT_ENV 1

namespace b3host {

class Ps2FloatEnv {
public:
    static constexpr unsigned kRoundTowardZero = 0x6000;   // MXCSR RC = 11
    static constexpr unsigned kFlushToZero = 0x8000;       // FTZ
    static constexpr unsigned kDenormalsAreZero = 0x0040;  // DAZ
    // rt1: compiler barriers on both sides of the MXCSR write (with -frounding-math on the units that compute under it:
    // the float operations then cannot be scheduled across it either)
    Ps2FloatEnv() : saved_(_mm_getcsr()) {
        __asm__ __volatile__("" ::: "memory");
        _mm_setcsr((saved_ & ~0x6000u) | kRoundTowardZero | kFlushToZero | kDenormalsAreZero);
        __asm__ __volatile__("" ::: "memory");
    }
    ~Ps2FloatEnv() {
        __asm__ __volatile__("" ::: "memory");
        _mm_setcsr(saved_);
        __asm__ __volatile__("" ::: "memory");
    }
    Ps2FloatEnv(const Ps2FloatEnv&) = delete;
    Ps2FloatEnv& operator=(const Ps2FloatEnv&) = delete;
private:
    unsigned saved_;
};
// rt1: the host's default environment (round to nearest, no FTZ / DAZ) inside a Ps2FloatEnv scope -- for the few
// reference operations whose double staging assumes it (b3fp::hw::vu_div / vu_sqrt, ps2float_hw.h)
class DefaultFloatEnv {
public:
    DefaultFloatEnv() : saved_(_mm_getcsr()) {
        __asm__ __volatile__("" ::: "memory");
        _mm_setcsr(saved_ & ~(0x6000u | 0x8000u | 0x0040u));
        __asm__ __volatile__("" ::: "memory");
    }
    ~DefaultFloatEnv() {
        __asm__ __volatile__("" ::: "memory");
        _mm_setcsr(saved_);
        __asm__ __volatile__("" ::: "memory");
    }
    DefaultFloatEnv(const DefaultFloatEnv&) = delete;
    DefaultFloatEnv& operator=(const DefaultFloatEnv&) = delete;
private:
    unsigned saved_;
};

}  // namespace b3host
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#include <cstdint>
#define B3HOST_HAVE_PS2_FLOAT_ENV 1

namespace b3host {

class Ps2FloatEnv {
public:
    static constexpr std::uint64_t kRModeMask = std::uint64_t{3} << 22;
    static constexpr std::uint64_t kRoundTowardZero = std::uint64_t{3} << 22;   // FPCR RMode = 11 (RZ)
    static constexpr std::uint64_t kFlushToZero = std::uint64_t{1} << 24;      // FPCR FZ (inputs and outputs, AH = 0)
    static std::uint64_t read() { std::uint64_t v; __asm__ __volatile__("mrs %0, fpcr" : "=r"(v) : : "memory"); return v; }
    static void write(std::uint64_t v) { __asm__ __volatile__("msr fpcr, %0" : : "r"(v) : "memory"); }
    Ps2FloatEnv() : saved_(read()) { write((saved_ & ~kRModeMask) | kRoundTowardZero | kFlushToZero); }
    ~Ps2FloatEnv() { write(saved_); }
    Ps2FloatEnv(const Ps2FloatEnv&) = delete;
    Ps2FloatEnv& operator=(const Ps2FloatEnv&) = delete;
private:
    std::uint64_t saved_;
};
// rt1: see the x86 DefaultFloatEnv: FPCR RMode = 00 (nearest), FZ = 0
class DefaultFloatEnv {
public:
    DefaultFloatEnv() : saved_(Ps2FloatEnv::read()) { Ps2FloatEnv::write(saved_ & ~(Ps2FloatEnv::kRModeMask | Ps2FloatEnv::kFlushToZero)); }
    ~DefaultFloatEnv() { Ps2FloatEnv::write(saved_); }
    DefaultFloatEnv(const DefaultFloatEnv&) = delete;
    DefaultFloatEnv& operator=(const DefaultFloatEnv&) = delete;
private:
    std::uint64_t saved_;
};

}  // namespace b3host
#endif

#endif
