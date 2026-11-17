// b3host/eeasm.h -- the host model behind decomp inline asm (EE core, COP1, MMI, VU0 macro mode), as
// tools/decomp2host/eeasm.py translates it (docs/decomp-on-host.md "P6: inline asm").
//
// The decomp writes VU0 code as GCC-style asm stand-ins (`asm("vsub.xyz %0, %1, %2" : "=j"(t) : "j"(a), "j"(b))`).
// eeasm.py parses each template at generation time and emits one C++ statement per instruction on this model:
//   - operand registers (%N) become C++ locals (u_long128 for "j"/"r", the FPR bits for "f");
//   - explicit registers ($vf1, $ACC, $Q, $I, R, $viN, $N, $fN) live in a per-thread State, because the decomp keeps
//     values in them from one asm statement to the next (`asm("qmtc2 %0, $vf1" : : "r"(x)); asm("vrsqrt $Q, $vf0w,
//     $vf1x" ...)`, B3AICar_001419F0.cpp:38-39). That is the VU0 register file as the asm sees it [C asm semantics].
// Arithmetic: b3vu / b3fp (b3host/ps2float.h): with B3_PS2_FLOAT=1 the PS2 float model bit for bit, with 0 host SIMD.
//
// GPR model: a register is 128 bits. 32-bit results are sign-extended into the low 64 bits and leave the upper 64
// unchanged (R5900: only MMI instructions write bits 127..64) [L PCSX2 writes UD[0] only]. A C operand of a
// narrower type enters a register the way MWCC loads it (32-bit values sign-extended, u8/u16 zero-extended, 64-bit
// as is); its upper 64 bits are undefined on the hardware and 0 here [G].
//
// Not modelled (eeasm.py refuses them at generation time): MAC/STATUS/CLIP flags
// (cfc2 of $vi16-$vi19), branches and labels inside asm, COP0/privileged instructions. VU0 microprograms: b3asm::vcallms
// below (a platform hook; vu0 lane), not yet translated by eeasm.py.
//
// Names: the instruction mnemonics are the EE's (Sony manuals); everything else here is this project's own.
#ifndef B3HOST_EEASM_H
#define B3HOST_EEASM_H

#include "b3host/gp.h"
#include "b3host/host.h"
#include "b3host/ps2f.h"
#include "b3host/ps2float.h"
#include "b3host/ps2types.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace b3asm {

using Q = u_long128;
using b3host::make_u128;

// ---------------------------------------------------------------------------------------------------------------
// The register file the asm names explicitly.
// ---------------------------------------------------------------------------------------------------------------
struct State {
    Q vf[32] = {};              // vf0 is read as (0, 0, 0, 1) and never written
    Q acc = {};                 // ACC
    std::uint32_t q = 0;        // Q (VDIV/VSQRT/VRSQRT; the macro-mode interpreter makes it visible at once)
    std::uint32_t i = 0;        // I ($vi21)
    std::uint32_t r = 0x3F800000u;   // R ($vi20): 23-bit LFSR shown as a float in [1, 2)
    std::uint16_t vi[16] = {};  // VI1..VI15 (VI0 reads 0)
    Q gpr[32] = {};             // EE GPRs named explicitly ($8, $t0, ...); $0 reads 0
    std::uint32_t fpr[32] = {}; // COP1 registers named explicitly ($f0, ...)
    std::uint8_t sa = 0;        // SA (QFSRV byte count)
    std::uint64_t lo = 0, hi = 0, lo1 = 0, hi1 = 0;   // LO/HI and the MMI pipeline-1 halves
    std::uint16_t cmsar0 = 0;   // vu0: CMSAR0 ($vi27), VCALLMSR's start pair
};
inline State& state() {
    thread_local State s;
    return s;
}

// ---------------------------------------------------------------------------------------------------------------
// Operands in and out of C++ expressions (what MWCC does when it binds a C value to a register).
// ---------------------------------------------------------------------------------------------------------------
inline Q q64(std::uint64_t lo) { return make_u128(static_cast<std::uint32_t>(lo), static_cast<std::uint32_t>(lo >> 32), 0, 0); }
inline std::uint64_t lo64(const Q& q) { return q.w[0] | (static_cast<std::uint64_t>(q.w[1]) << 32); }
inline std::uint64_t hi64(const Q& q) { return q.w[2] | (static_cast<std::uint64_t>(q.w[3]) << 32); }
inline std::uint64_t sx32(std::uint32_t v) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(v))); }
// A 64-bit result into a register: bits 127..64 kept.
inline void set64(Q& d, std::uint64_t v) { d.w[0] = static_cast<std::uint32_t>(v); d.w[1] = static_cast<std::uint32_t>(v >> 32); }
inline void set32(Q& d, std::uint32_t v) { set64(d, sx32(v)); }

// "r" inputs.
inline Q gpr_in(const Q& v) { return v; }
inline Q gpr_in(b3host::ps2f v) { return q64(sx32(std::bit_cast<std::uint32_t>(v.v))); }
inline Q gpr_in(float v) { return q64(sx32(std::bit_cast<std::uint32_t>(v))); }
inline Q gpr_in(bool v) { return q64(v ? 1u : 0u); }
template <class T> requires std::is_integral_v<T>
inline Q gpr_in(T v) {
    if constexpr (sizeof(T) == 8) return q64(static_cast<std::uint64_t>(v));
    else if constexpr (sizeof(T) == 4) return q64(sx32(static_cast<std::uint32_t>(v)));
    else if constexpr (std::is_signed_v<T>) return q64(static_cast<std::uint64_t>(static_cast<std::int64_t>(v)));
    else return q64(static_cast<std::uint64_t>(v));
}
template <class T> requires std::is_enum_v<T>
inline Q gpr_in(T v) { return gpr_in(static_cast<std::underlying_type_t<T>>(v)); }
template <class T> inline Q gpr_in(T* p) { return q64(b3host::guest_address(p)); }
template <class T> inline Q gpr_in(b3host::gp<T> p) { return q64(p.raw); }

// "=r" / "+r" outputs: the register's low bits, as MWCC stores them back to the C object.
inline void gpr_out(Q& d, const Q& v) { d = v; }
inline void gpr_out(b3host::ps2f& d, const Q& v) { d.v = std::bit_cast<float>(v.w[0]); }
inline void gpr_out(float& d, const Q& v) { d = std::bit_cast<float>(v.w[0]); }
inline void gpr_out(bool& d, const Q& v) { d = (v.w[0] & 0xFF) != 0; }
template <class T> requires std::is_integral_v<T>
inline void gpr_out(T& d, const Q& v) { d = static_cast<T>(lo64(v)); }
template <class T> requires std::is_enum_v<T>
inline void gpr_out(T& d, const Q& v) { d = static_cast<T>(static_cast<std::underlying_type_t<T>>(lo64(v))); }
template <class T> inline void gpr_out(T*& d, const Q& v) { d = static_cast<T*>(b3host::host_pointer(v.w[0], 1)); }
template <class T> inline void gpr_out(b3host::gp<T>& d, const Q& v) { d = b3host::gp<T>::from_raw(v.w[0]); }

// "j" (a VU0 vf register): a 128-bit value.
inline Q vf_in(const Q& v) { return v; }
inline void vf_out(Q& d, const Q& v) { d = v; }
// native3 (gen1's gap): a float in "j" (`vrinit $R, %0x` : : "j"(seed), func_0025E410 / func_0025EA70). MWCC brings an FPR
// value into a VU register through a GPR (mfc1 + qmtc2): the float's bits in lane x; the other lanes are the GPR's sign
// extension and stale upper bits [L], zero here (the forms seen read lane x only). Out: lane x's bits.
inline Q vf_in(float v) { Q q{}; q.w[0] = std::bit_cast<std::uint32_t>(v); return q; }
inline Q vf_in(b3host::ps2f v) { return vf_in(v.v); }
inline void vf_out(float& d, const Q& v) { d = std::bit_cast<float>(v.w[0]); }
inline void vf_out(b3host::ps2f& d, const Q& v) { d.v = std::bit_cast<float>(v.w[0]); }

// "f" (a COP1 register): the float's bits.
inline std::uint32_t fpr_in(b3host::ps2f v) { return std::bit_cast<std::uint32_t>(v.v); }
inline std::uint32_t fpr_in(float v) { return std::bit_cast<std::uint32_t>(v); }
inline void fpr_out(b3host::ps2f& d, std::uint32_t v) { d.v = std::bit_cast<float>(v); }
inline void fpr_out(float& d, std::uint32_t v) { d = std::bit_cast<float>(v); }

// Memory: a base held in a register (a guest address) plus an offset, or a C object ("m").
inline unsigned char* mem(std::uint64_t base, std::int32_t off, std::uint32_t len) {
    const b3host::gaddr a = static_cast<b3host::gaddr>(base) + static_cast<b3host::gaddr>(off);
    void* p = b3host::host_pointer(a, len);
    if (!p) b3host::fatal("b3asm: asm memory access outside guest RAM/scratchpad");
    return static_cast<unsigned char*>(p);
}
// A pointer-typed "r" operand used only as a memory base keeps its host pointer (it may point at a host local).
template <class T> inline unsigned char* host_base(T* p) { return reinterpret_cast<unsigned char*>(const_cast<std::remove_cv_t<T>*>(p)); }
template <class T> inline unsigned char* host_base(b3host::gp<T> p) { return static_cast<unsigned char*>(b3host::host_pointer(p.raw, 1)); }
template <class T> requires std::is_integral_v<T>
inline unsigned char* host_base(T guest) { return static_cast<unsigned char*>(b3host::host_pointer(static_cast<b3host::gaddr>(guest), 1)); }
template <class T> inline T ld(const unsigned char* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
template <class T> inline void st(unsigned char* p, const T& v) { std::memcpy(p, &v, sizeof v); }
// lq/sq/lqc2/sqc2 ignore the low four address bits [C EE Core Instruction Set Manual LQ: "the least-significant 4
// bits of the effective address are masked to zero"]. The mask applies to the *guest* address: a pointer into a
// window is masked relative to the window's base (guest 0 / 0x70000000, both 16-aligned in guest terms), whatever
// the host buffer's own alignment (a backend's scratchpad buffer need not be 16-byte aligned). A
// pointer to a host object (a C++ local) is used as is.
inline unsigned char* align16(unsigned char* p) {
    const b3host::Windows& w = b3host::detail::g_windows;
    if (w.ram && p >= w.ram && p < w.ram + w.ram_size) return w.ram + ((p - w.ram) & ~std::ptrdiff_t{15});
    if (w.scratchpad && p >= w.scratchpad && p < w.scratchpad + b3host::kScratchpadSize)
        return w.scratchpad + ((p - w.scratchpad) & ~std::ptrdiff_t{15});
    return p;
}
inline Q ldq(unsigned char* p) { return ld<Q>(align16(p)); }
inline void stq(unsigned char* p, const Q& v) { st<Q>(align16(p), v); }

// ---------------------------------------------------------------------------------------------------------------
// VU0 macro instructions. Masks are b3vu's (X=1 .. W=8). Every op takes the destination's previous value.
// ---------------------------------------------------------------------------------------------------------------
inline Q vf0() { return b3vu::vf0(); }
template <unsigned L> inline Q bc(const Q& a) { return b3vu::bc<L>(a); }
inline Q splat(std::uint32_t b) { return make_u128(b, b, b, b); }

template <unsigned M> B3VU_INLINE Q vadd(const Q& d, const Q& s, const Q& t) { return b3vu::add<M>(d, s, t); }
template <unsigned M> B3VU_INLINE Q vsub(const Q& d, const Q& s, const Q& t) { return b3vu::sub<M>(d, s, t); }
template <unsigned M> B3VU_INLINE Q vmul(const Q& d, const Q& s, const Q& t) { return b3vu::mul<M>(d, s, t); }
template <unsigned M> B3VU_INLINE Q vmadd(const Q& d, const Q& acc, const Q& s, const Q& t) { return b3vu::madd<M>(d, acc, s, t); }
template <unsigned M> B3VU_INLINE Q vmsub(const Q& d, const Q& acc, const Q& s, const Q& t) { return b3vu::msub<M>(d, acc, s, t); }
// VMAX / VMINI / VABS / VMOVE / VMR32 / VFTOI, VOPMULA / VOPMSUB, the random unit, CFC2 / CTC2 of VI and the MMI
// lane operations (pmulth below excepted): b3host/eeasm_ops.h (docs/specs/eeasm-ops.md).
}  // namespace b3asm
#include "b3host/eeasm_ops.h"
namespace b3asm {
// VITOFn: int -> float, scaled by 2^-n. [L VU manual: conversion truncates]; this follows the manual: cvt as COP1
// cvt.s.w (truncating), then an exact power-of-two scale.
inline std::uint32_t itof_bits(std::uint32_t bits, unsigned frac) {
    std::uint32_t f = b3fp::ref::cvt_s_w(static_cast<std::int32_t>(bits));
    if (frac && (f & 0x7F800000u)) {
        const int e = static_cast<int>((f >> 23) & 0xFF) - static_cast<int>(frac);
        f = e <= 0 ? (f & 0x80000000u) : ((f & 0x807FFFFFu) | (static_cast<std::uint32_t>(e) << 23));
    }
    return f;
}
template <unsigned M, unsigned N> B3VU_INLINE Q vitof(const Q& d, const Q& s) {
    B3VU_ENTER(b3host::vubind::kItof, M, d, &s, nullptr);
    Q r = b3vu_base_;
    for (unsigned k = 0; k < 4; ++k) if (M & (1u << k)) r.w[k] = itof_bits(s.w[k], N);
    B3VU_LEAVE(r);
    return r;
}
// FDIV unit -> Q (b3vu::q_div / q_sqrt / q_rsqrt)
inline std::uint32_t vdiv(std::uint32_t n, std::uint32_t d) { return std::bit_cast<std::uint32_t>(b3vu::q_div(std::bit_cast<float>(n), std::bit_cast<float>(d))); }
inline std::uint32_t vsqrt(std::uint32_t x) { return std::bit_cast<std::uint32_t>(b3vu::q_sqrt(std::bit_cast<float>(x & 0x7FFFFFFFu))); }
inline std::uint32_t vrsqrt(std::uint32_t n, std::uint32_t x) { return std::bit_cast<std::uint32_t>(b3vu::q_rsqrt(std::bit_cast<float>(n), std::bit_cast<float>(x))); }
template <unsigned M> inline Q fill(const Q& d, std::uint32_t v) {
    Q r = d;
    for (unsigned k = 0; k < 4; ++k) if (M & (1u << k)) r.w[k] = v;
    return r;
}

// vu0: VCALLMS / VCALLMSR (VU0 micro mode). The microprogram runs synchronously on this State's VF / VI / ACC / I / Q /
// R (the VU0 register file the asm names) through the platform's model (src/native/platform_vu0.h installs the hook;
// mini_check / tests install their own). `entry` is the pair index: VCALLMS's immediate / 8, VCALLMSR's CMSAR0
// [L PS2 VU macro manual]. Without a hook it is fatal. Values the decomp's asm keeps in compiler-chosen "j" operands
// (C++ locals here) are not seen by the microprogram: a vcallms unit needs its VU operands in explicit registers or
// bound to retail's (b3host/vubind.h).
using VcallmsHook = void (*)(State&, std::uint32_t entry);
inline VcallmsHook& vcallms_hook() {
    static VcallmsHook h = nullptr;
    return h;
}
inline void vcallms(State& s, std::uint32_t entry) {
    if (!vcallms_hook()) b3host::fatal("b3asm::vcallms: no VU0 micro-mode model installed (entry " + std::to_string(entry) + ")");
    vcallms_hook()(s, entry);
}
inline void vcallmsr(State& s) { vcallms(s, s.cmsar0); }

// vu0 (mac1's request): the decomp's DMA-wait idiom `sync; sync.p; L: nop x5; bc0f L` (bc0t: the inverse) spins until
// COP0 condition 0 -- the DMAC's CPCOND0 -- has the wanted value; eeasm.py turns the whole statement into
// wait_cpcond0(true / false). The platform installs the hook (HwModel::cpcond0); without one the wait returns at once
// (no DMA model: every transfer is complete). Every DMA in the native model completes at its kick, so a wait that never
// ends means a transfer the model holds (a PATH3 stall): fatal rather than a silent hang.
using Cpcond0Hook = bool (*)();
inline Cpcond0Hook& cpcond0_hook() {
    static Cpcond0Hook h = nullptr;
    return h;
}
inline void wait_cpcond0(bool want) {
    const Cpcond0Hook h = cpcond0_hook();
    if (!h) return;
    for (std::uint32_t n = 0; h() != want; ++n)
        if (n >= 1000000u) b3host::fatal(std::string("b3asm::wait_cpcond0: CPCOND0 never became ") + (want ? "true" : "false") + " (a DMA the model holds?)");
}

// PMULTH: rd = the even products' halves, LO/HI = all eight signed 16x16 products [EE Core Instruction Set Manual
// PMULTH]. rd.w0 = p0, rd.w1 = p2, rd.w2 = p4, rd.w3 = p6; LO = {p0, p1, p4, p5}; HI = {p2, p3, p6, p7}.
inline Q pmulth(State& st, const Q& s, const Q& t) {
    std::uint32_t p[8];
    for (unsigned k = 0; k < 8; ++k) {
        const auto a = static_cast<std::int16_t>(s.w[k / 2] >> ((k & 1) * 16));
        const auto b = static_cast<std::int16_t>(t.w[k / 2] >> ((k & 1) * 16));
        p[k] = static_cast<std::uint32_t>(static_cast<std::int32_t>(a) * static_cast<std::int32_t>(b));
    }
    st.lo = p[0] | (static_cast<std::uint64_t>(p[1]) << 32);
    st.hi = p[2] | (static_cast<std::uint64_t>(p[3]) << 32);
    st.lo1 = p[4] | (static_cast<std::uint64_t>(p[5]) << 32);
    st.hi1 = p[6] | (static_cast<std::uint64_t>(p[7]) << 32);
    return make_u128(p[0], p[2], p[4], p[6]);
}

// ---------------------------------------------------------------------------------------------------------------
// COP1 helpers (b3fp: the PS2 float model in faithful mode, docs/specs/ps2-float.md)
// ---------------------------------------------------------------------------------------------------------------
inline std::uint32_t fb(float f) { return std::bit_cast<std::uint32_t>(f); }
inline float ff(std::uint32_t b) { return std::bit_cast<float>(b); }
inline std::uint32_t add_s(std::uint32_t a, std::uint32_t b) { return fb(b3fp::add(ff(a), ff(b))); }
inline std::uint32_t sub_s(std::uint32_t a, std::uint32_t b) { return fb(b3fp::sub(ff(a), ff(b))); }
inline std::uint32_t mul_s(std::uint32_t a, std::uint32_t b) { return fb(b3fp::mul(ff(a), ff(b))); }
inline std::uint32_t div_s(std::uint32_t a, std::uint32_t b) { return fb(b3fp::div(ff(a), ff(b))); }
inline std::uint32_t sqrt_s(std::uint32_t a) { return fb(b3fp::sqrt(ff(a))); }
inline std::uint32_t rsqrt_s(std::uint32_t a, std::uint32_t b) { return fb(b3fp::rsqrt(ff(a), ff(b))); }
inline std::uint32_t max_s(std::uint32_t a, std::uint32_t b) { return b3fp::ref::max(a, b); }
inline std::uint32_t min_s(std::uint32_t a, std::uint32_t b) { return b3fp::ref::min(a, b); }
inline std::uint32_t cvt_s_w(std::uint32_t a) { return fb(b3fp::cvt_s_w(static_cast<std::int32_t>(a))); }
inline std::uint32_t cvt_w_s(std::uint32_t a) { return static_cast<std::uint32_t>(b3fp::cvt_w_s(ff(a))); }

}  // namespace b3asm

#endif
