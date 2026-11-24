// vu0_aot_rt.h -- the runtime of the generated VU0 microprograms (vu0 lane, 2026-11-07; tools/native/vu0_aot.py).
//
// A generated program is retail's VU0 micro code translated pair by pair into C++ (gen/native/vu0/vu0_aot_gen.cpp,
// committed; `make vu0-aot` regenerates it). It reproduces b3asset::Vu1::run with pair_timing = true (the VU0 pair
// timing: one pair per cycle, no stalls) exactly, from a settled core to a settled core, as src/native/vu0_unit.cpp
// runs it:
//   - each pair commits what became visible at its cycle first (MAC / STATUS 4 pairs after the FMAC, CLIP 4 after
//     the CLIP, Q 7 / 13 after DIV-SQRT / RSQRT, P 12 / 18 after the EFU op), then the upper slot is computed from the
//     pre-pair state, then the lower slot runs (memory stores at once), then the lower result, the VI writes and the
//     upper result are written (upper wins a same-register write);
//   - a conditional branch reading a VI the previous pair wrote sees the value before that write (Vu1's branch_vi);
//   - E bit: the next pair runs, then the launch ends; branches take effect after their delay slot.
// Pending flags fit a 4-slot ring: one flag update per pair at most, visible 4 pairs later, and every cycle runs a
// pair, so the entry a cycle commits sits in slot (cycle & 3) and that slot is free again for the pair's own update.
// The interpreter stays the reference in dev builds (make VU0_INTERP=1: -DB3_VU0_INTERP=1): B3_VU0_AOT=0 runs it alone,
// =check runs both on every launch and compares (src/native/vu0_unit.cpp). The shipped game has no interpreter: a
// launch no generated program covers is a fatal error there.
#ifndef B3NATIVE_VU0_AOT_RT_H
#define B3NATIVE_VU0_AOT_RT_H

#include "b3host/host.h"
#include "b3host/ps2float.h"
#include "b3host/ps2float_hw.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace b3native::vu0aot {

// The settled VU0 state between two launches, which is all that crosses a launch boundary: b3asset::Vu1's architectural
// fields under the same names (VF / VI / ACC / I / Q / P / R, the flags, TPC), so the generated code reads as the
// interpreter does. Vu0Unit keeps one; a dev build's interpreter runs copy it in and out.
struct Core {
    std::uint32_t vf[32][4] = {{0, 0, 0, 0x3F800000u}};
    std::uint16_t vi[16] = {};
    std::uint32_t acc[4] = {}, I = 0, Q = 0, P = 0, R = 0x3F800000u;
    std::uint32_t mac = 0, status = 0, clip = 0;
    std::uint32_t tpc = 0;
    struct Stats {
        std::uint64_t pairs = 0, cycles = 0, q_read_pending = 0, p_read_pending = 0, branch_vi_backup = 0;
        std::uint64_t flag_reads_prelaunch = 0;
    } stats;
};

namespace fr4 = b3fp::vufast4;   // vu0lanes: the dest lanes at once (ps2float_hw.h)
namespace fr = b3fp::vufast;   // rt1: hw under Ps2FloatEnv (Vu0Unit sets it), or ref with B3_VU0_FLOAT=ref (ps2float_hw.h)

// the same as src/b3asset/vu1.cpp's (anonymous namespace there)
inline std::uint32_t lane_mac(std::uint32_t v, int lane) {
    std::uint32_t m = 0;
    if ((v & 0x7FFFFFFFu) == 0) m |= 1u << (3 - lane);
    if (v & 0x80000000u) m |= 1u << (7 - lane);
    return m;
}
inline std::uint32_t mac_to_status(std::uint32_t mac) {
    std::uint32_t s = 0;
    if (mac & 0x000F) s |= 1;
    if (mac & 0x00F0) s |= 2;
    if (mac & 0x0F00) s |= 4;
    if (mac & 0xF000) s |= 8;
    return s;
}
inline std::uint32_t ftoi(std::uint32_t b, int frac) {
    const double v = double(b3fp::flt(fr::clamp(b))) * double(1 << frac);
    if (v >= 2147483647.0) return 0x7FFFFFFFu;
    if (v <= -2147483648.0) return 0x80000000u;
    return std::uint32_t(std::int32_t(std::trunc(v)));
}
inline std::uint32_t itof(std::uint32_t b, int frac) { return fr::chop(double(std::int32_t(b)) / double(1 << frac)); }

struct Rt {
    Core& c;
    std::uint32_t* mem;   // VU0 data memory, 256 qwords
    std::uint64_t now = 0;
    std::uint64_t flag_commits = 0;
    struct Flag { bool v; std::uint32_t mac, st; } fl[4] = {};
    struct Clip { bool v; std::uint32_t bits; } cl[4] = {};
    bool qp = false, pp = false;
    std::uint64_t qr = 0, pr = 0;
    std::uint32_t qn = 0, qs = 0, pn = 0;
    struct ViWrite { std::uint64_t cycle; std::uint8_t reg; std::uint16_t before; bool eligible; } vh[8] = {};
    unsigned nvh = 0;

    Rt(Core& core, std::uint32_t* data) : c(core), mem(data) {}

    std::uint32_t* qw(std::uint32_t a) { return mem + (a & 255u) * 4; }
    void commit_q() {
        c.Q = qn;
        c.status = (c.status & ~0x30u) | qs | (qs << 6);
        qp = false;
    }
    void commit() {
        Flag& f = fl[now & 3];
        if (f.v) {
            c.mac = f.mac;
            c.status = (c.status & ~0xFu) | f.st | (f.st << 6);
            f.v = false;
            ++flag_commits;
        }
        Clip& k = cl[now & 3];
        if (k.v) { c.clip = ((c.clip << 6) | k.bits) & 0xFFFFFFu; k.v = false; }
        if (qp && qr <= now) commit_q();
        if (pp && pr <= now) { c.P = pn; pp = false; }
    }
    // vu0flags: commit() in parts, for the generator to emit only the ones that can fire at a pair (vu0_aot.py)
    void commit_flags() {
        Flag& f = fl[now & 3];
        if (f.v) {
            c.mac = f.mac;
            c.status = (c.status & ~0xFu) | f.st | (f.st << 6);
            f.v = false;
            ++flag_commits;
        }
    }
    void commit_clip() {
        Clip& k = cl[now & 3];
        if (k.v) { c.clip = ((c.clip << 6) | k.bits) & 0xFFFFFFu; k.v = false; }
    }
    void commit_q_due() { if (qp && qr <= now) commit_q(); }
    void commit_p_due() { if (pp && pr <= now) { c.P = pn; pp = false; } }
    // vu0flags: a VI write no conditional branch within 4 pairs can look behind (Rt::branch_vi): no history entry
    void set_vi(int r, std::uint16_t v, bool) {
        r &= 15;
        if (r != 0) c.vi[r] = v;
    }
    void push_flags(std::uint32_t m) { fl[now & 3] = {true, m, mac_to_status(m)}; }
    void push_clip(std::uint32_t bits) { cl[now & 3] = {true, bits}; }
    void clear_clip() { for (Clip& k : cl) k.v = false; }
    // the end of the launch: Vu1::settle's commit(~0): flags in order, then CLIP in order, then Q, then P
    void finish() {
        for (unsigned k = 0; k < 4; ++k) {
            Flag& f = fl[(now + k) & 3];
            if (f.v) { c.mac = f.mac; c.status = (c.status & ~0xFu) | f.st | (f.st << 6); f.v = false; ++flag_commits; }
        }
        for (unsigned k = 0; k < 4; ++k) {
            Clip& q = cl[(now + k) & 3];
            if (q.v) { c.clip = ((c.clip << 6) | q.bits) & 0xFFFFFFu; q.v = false; }
        }
        if (qp) commit_q();
        if (pp) { c.P = pn; pp = false; }
        c.stats.pairs += now;
        c.stats.cycles += now;
    }
    void write_vi(int r, std::uint16_t v, bool eligible) {
        r &= 15;
        if (r == 0) return;
        if (nvh == 8) { std::memmove(vh, vh + 1, 7 * sizeof vh[0]); --nvh; }
        vh[nvh++] = {now, std::uint8_t(r), c.vi[r], eligible};
        c.vi[r] = v;
    }
    std::uint16_t branch_vi(int r, bool in_delay) {
        r &= 15;
        std::uint16_t v = c.vi[r];
        if (r == 0 || in_delay || nvh == 0) return v;
        const ViWrite& n = vh[nvh - 1];
        if (n.cycle + 1 != now || n.reg != r || !n.eligible) return v;
        for (unsigned k = nvh; k-- > 0;) {
            if (now - vh[k].cycle > 4) break;
            if (vh[k].reg == r) {
                if (!vh[k].eligible) break;
                v = vh[k].before;
            }
        }
        if (v != c.vi[r]) ++c.stats.branch_vi_backup;
        return v;
    }
    void flag_read(std::uint64_t base) { if (flag_commits == base) ++c.stats.flag_reads_prelaunch; }
    std::uint32_t qread() { if (qp) ++c.stats.q_read_pending; return c.Q; }
};

// One generated image: its loaded pair range, the SHA-256 of those pairs' words (lower then upper word per pair,
// little-endian; tools/native/vu0_aot_images.sha256), which Vu0Unit matches the micro memory against, and its program.
struct Image {
    const char* name;
    std::uint32_t pairs;                 // pairs 0 .. pairs-1
    const char* sha256;                  // 64 lower-case hex digits
    bool (*run)(Core& c, std::uint32_t* data, std::uint32_t entry);   // false: entry outside the image
};
const Image* images(unsigned* n);   // gen/native/vu0/vu0_aot_gen.cpp

}  // namespace b3native::vu0aot

#endif
