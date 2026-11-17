// The shared VU core (src/b3asset/vu1.cpp) in VU0's configuration (pc_mask 511, qw_mask 255), per instruction class,
// with the VU0 pair timing (pair_timing: the golden runs' timing) and the stall model: hand-assembled programs,
// hand-computed results (encodings: Sony VU Instruction Manual; where the two timings differ, the rule is stated per
// test). vu0 lane, 2026-11-07.
#include <cstring>
#include <utility>
#include <vector>

#include "b3asset/vif.h"
#include "b3asset/vu1.h"
#include "test.h"

using namespace b3asset;

namespace {

using u32 = std::uint32_t;
constexpr u32 X = 8, Y = 4, Z = 2, W = 1, XYZW = 15;
// upper: type 1 (op < 0x3C) and type 3 (op2 in bits 10..6 | 1..0, bits 5..2 = 1111)
u32 up1(u32 op, u32 dest, u32 ft, u32 fs, u32 fd) { return dest << 21 | ft << 16 | fs << 11 | fd << 6 | op; }
u32 up3(u32 op2, u32 dest, u32 ft, u32 fs) { return dest << 21 | ft << 16 | fs << 11 | (op2 >> 2) << 6 | 0x3C | (op2 & 3); }
constexpr u32 UNOP = 0x000002FFu, EBIT = 0x40000000u, IBIT = 0x80000000u;
// lower: type 1 (0x40 << 25), type 3 forms, type 0 (op << 25)
u32 lo1(u32 op, u32 it, u32 is, u32 id) { return 0x80000000u | it << 16 | is << 11 | id << 6 | op; }
u32 lo3(u32 op2, u32 dest, u32 ft, u32 fs) { return 0x80000000u | dest << 21 | ft << 16 | fs << 11 | (op2 >> 2) << 6 | 0x3C | (op2 & 3); }
constexpr u32 LNOP = 0x8000033Cu;   // move vf0, vf0
u32 lo0(u32 op, u32 dest, u32 t, u32 s, int imm11) { return op << 25 | dest << 21 | t << 16 | s << 11 | (u32(imm11) & 0x7FF); }
u32 lq(u32 dest, u32 ft, u32 is, int imm) { return lo0(0x00, dest, ft, is, imm); }
u32 sq(u32 dest, u32 fs, u32 it, int imm) { return lo0(0x01, dest, it, fs, imm); }
u32 iaddiu(u32 it, u32 is, u32 imm) { return 0x08u << 25 | ((imm >> 11) & 0xF) << 21 | it << 16 | is << 11 | (imm & 0x7FF); }
u32 isubiu(u32 it, u32 is, u32 imm) { return iaddiu(it, is, imm) | 1u << 25; }
u32 div(u32 fs, u32 fsf, u32 ft, u32 ftf) { return lo3(0x38, 0, ft, fs) | fsf << 21 | ftf << 23; }
u32 fmand(u32 it, u32 is) { return lo0(0x1A, 0, it, is, 0); }

struct Prog {
    std::vector<std::pair<u32, u32>> pairs;   // (lower, upper)
    void add(u32 lo, u32 up) { pairs.emplace_back(lo, up); }
    void load(VuMicroprogram& p, u32 at = 0) const {
        for (std::size_t k = 0; k < pairs.size(); ++k) {
            p.lower[(at + k) & 511] = pairs[k].first;
            p.upper[(at + k) & 511] = pairs[k].second;
        }
    }
};
struct Vu0Core {
    VuMemory m;
    Vu1 c{m};
    VuMicroprogram p;
    explicit Vu0Core(bool pair_timing) { c.pc_mask = 511; c.qw_mask = 255; c.pair_timing = pair_timing; }
    bool run(const Prog& g, u32 at = 0) {
        g.load(p, at);
        std::string why;
        const bool ok = c.run(p, at, 0, 0, nullptr, &why, 100000);
        if (!ok) std::fprintf(stderr, "run failed: %s\n", why.c_str());
        return ok;
    }
    void set_qw(u32 a, float x, float y, float z, float w) {
        m.w[a * 4 + 0] = t::fbits(x); m.w[a * 4 + 1] = t::fbits(y); m.w[a * 4 + 2] = t::fbits(z); m.w[a * 4 + 3] = t::fbits(w);
    }
    float f(u32 r, int l) const { return t::bitsf(c.vf[r][l]); }
};

}  // namespace

// FMAC classes: ADD, MULbc, MULAbc + MADDbc through ACC, OPMULA / OPMSUB (cross product), ITOF0 / FTOI0, MAX.
TEST(vu0_core_fmac_classes) {
    for (const bool pt : {true, false}) {
        Vu0Core v(pt);
        v.set_qw(0, 1, 2, 3, 4);
        v.set_qw(1, 10, 20, 40, 80);
        v.m.w[2 * 4 + 0] = 5; v.m.w[2 * 4 + 1] = u32(-3); v.m.w[2 * 4 + 2] = 0; v.m.w[2 * 4 + 3] = 7;
        Prog g;
        g.add(lq(XYZW, 1, 0, 0), UNOP);
        g.add(lq(XYZW, 2, 0, 1), UNOP);
        g.add(lq(XYZW, 9, 0, 2), UNOP);
        g.add(LNOP, up1(0x28, XYZW, 2, 1, 3));          // add vf3, vf1, vf2
        g.add(LNOP, up1(0x18 + 3, XYZW, 2, 1, 4));      // mulw vf4, vf1, vf2w
        g.add(LNOP, up3(0x18 + 0, XYZW, 2, 1));         // mulax ACC, vf1, vf2x
        g.add(LNOP, up1(0x08 + 1, XYZW, 2, 1, 6));      // maddy vf6, vf1, vf2y
        g.add(LNOP, up3(0x2E, X | Y | Z, 2, 1));        // opmula.xyz ACC, vf1, vf2
        g.add(LNOP, up1(0x2E, X | Y | Z, 1, 2, 7));     // opmsub.xyz vf7, vf2, vf1
        g.add(LNOP, up3(0x10, XYZW, 8, 9));             // itof0 vf8, vf9
        g.add(LNOP, up3(0x14, XYZW, 10, 3));            // ftoi0 vf10, vf3
        g.add(LNOP, up1(0x2B, XYZW, 2, 1, 11) | EBIT);  // max vf11, vf1, vf2   [E]
        g.add(LNOP, UNOP);
        CHECK(v.run(g));
        CHECK(v.f(3, 0) == 11 && v.f(3, 1) == 22 && v.f(3, 2) == 43 && v.f(3, 3) == 84);
        CHECK(v.f(4, 0) == 80 && v.f(4, 3) == 320);
        CHECK(v.f(6, 0) == 30 && v.f(6, 1) == 60 && v.f(6, 2) == 90 && v.f(6, 3) == 120);
        CHECK(v.f(7, 0) == 20 && v.f(7, 1) == -10 && v.f(7, 2) == 0);   // (1,2,3) x (10,20,40)
        CHECK_BITS(v.c.vf[7][3], 0u);                                    // w untouched
        CHECK(v.f(8, 0) == 5 && v.f(8, 1) == -3 && v.f(8, 2) == 0 && v.f(8, 3) == 7);
        CHECK_BITS(v.c.vf[10][0], 11u); CHECK_BITS(v.c.vf[10][3], 84u);
        CHECK(v.f(11, 0) == 10 && v.f(11, 3) == 80);
        CHECK(v.c.tpc == 13);
        if (pt) CHECK(v.c.stats.stall_cycles == 0);
        else CHECK(v.c.stats.stall_cycles > 0);   // add waits for the lq of vf2
    }
}

// Q: DIV's latency (7 pairs with no stalls; the same pairs in the stall model here, as nothing stalls after the DIV)
// and WAITQ paired with a Q reader: the pair timing stages the upper against the Q from before the pair, then WAITQ
// commits; the hardware stalls the whole pair [L VU manual], so the upper sees the new Q.
TEST(vu0_core_q_latency_and_waitq) {
    for (const bool pt : {true, false}) {
        Vu0Core v(pt);
        v.set_qw(0, 1, 2, 4, 16);
        v.c.Q = t::fbits(1.0f);
        Prog g;
        g.add(lq(XYZW, 1, 0, 0), UNOP);                  // 0
        g.add(LNOP, UNOP); g.add(LNOP, UNOP); g.add(LNOP, UNOP);   // 1..3 (vf1 ready at 4: no stall below)
        g.add(div(1, 0, 1, 2), UNOP);                    // 4: Q = vf1x / vf1z = 0.25, visible at 11
        for (int k = 5; k < 10; ++k) g.add(LNOP, UNOP);
        g.add(LNOP, up1(0x1C, X, 0, 1, 4));              // 10: mulq.x vf4, vf1, Q -> old Q
        g.add(LNOP, up1(0x1C, X, 0, 1, 3));              // 11: mulq.x vf3 -> 0.25
        g.add(div(1, 3, 1, 2), UNOP);                    // 12: Q = 16 / 4 = 4
        g.add(lo3(0x3B, 0, 0, 0), up1(0x1C, X, 0, 1, 5));   // 13: waitq | mulq.x vf5
        g.add(LNOP, up1(0x1C, X, 0, 1, 6) | EBIT);       // 14: mulq.x vf6 -> 4 in both
        g.add(LNOP, UNOP);
        CHECK(v.run(g));
        CHECK(v.f(4, 0) == 1.0f);
        CHECK(v.f(3, 0) == 0.25f);
        CHECK(v.f(5, 0) == (pt ? 0.25f : 4.0f));
        CHECK(v.f(6, 0) == 4.0f);
    }
}

// A DIV still in flight at the E bit: Q stays old until the launch is settled (Vu1::settle).
TEST(vu0_core_settle_commits_pending_q) {
    Vu0Core v(true);
    v.set_qw(0, 3, 1, 0, 0);
    v.c.Q = t::fbits(1.0f);
    Prog g;
    g.add(lq(XYZW, 1, 0, 0), UNOP);
    g.add(div(1, 0, 1, 1), UNOP | EBIT);   // Q = 3 / 1
    g.add(LNOP, UNOP);
    CHECK(v.run(g));
    CHECK(t::bitsf(v.c.Q) == 1.0f);
    v.c.settle();
    CHECK(t::bitsf(v.c.Q) == 3.0f);
}

// MAC flags: visible 4 pairs after the FMAC; FMAND before that sees the older value; after settle the next launch
// sees the last value at once (the MAC history is filled with it); a read before any flag update of the launch
// counts as a pre-launch read.
TEST(vu0_core_mac_flag_latency) {
    for (const bool pt : {true, false}) {
        Vu0Core v(pt);
        v.set_qw(0, 1, 2, 3, 4);
        Prog g;
        g.add(lq(XYZW, 1, 0, 0), UNOP);                         // 0
        g.add(LNOP, UNOP); g.add(LNOP, UNOP); g.add(LNOP, UNOP);
        g.add(iaddiu(1, 0, 0xFF), up1(0x2C, X, 1, 1, 2));        // 4: sub.x vf2, vf1, vf1 -> Zx (0x8)
        g.add(LNOP, UNOP);                                       // 5
        g.add(fmand(2, 1), UNOP);                                // 6: old MAC (0)
        g.add(fmand(3, 1), UNOP);                                // 7: old
        g.add(fmand(4, 1), UNOP | EBIT);                         // 8: 0x8
        g.add(LNOP, UNOP);
        CHECK(v.run(g));
        CHECK(v.c.vi[2] == 0 && v.c.vi[3] == 0 && v.c.vi[4] == 8);
        CHECK(v.c.stats.flag_reads_prelaunch == 2);
        v.c.settle();
        Prog h;
        h.add(fmand(5, 1), UNOP | EBIT);
        h.add(LNOP, UNOP);
        CHECK(v.run(h, 100));
        CHECK(v.c.vi[5] == 8);
        CHECK(v.c.stats.flag_reads_prelaunch == 3);
    }
}

// Integer ops and a counted loop; a conditional branch reading a VI written by the pair before it sees the value from
// before that write (microVU's rule, the core's branch_vi).
TEST(vu0_core_integer_and_branches) {
    for (const bool pt : {true, false}) {
        Vu0Core v(pt);
        Prog g;
        g.add(iaddiu(1, 0, 5), UNOP);                     // 0: vi1 = 5
        g.add(iaddiu(2, 0, 0), UNOP);                     // 1: vi2 = 0
        g.add(lo1(0x32, 2, 2, 1), UNOP);                  // 2: iaddi vi2, vi2, 1
        g.add(isubiu(1, 1, 1), UNOP);                     // 3: vi1 -= 1
        g.add(LNOP, UNOP);                                // 4
        g.add(lo0(0x29, 0, 1, 0, -4), UNOP);              // 5: ibne vi1, vi0, 2
        g.add(LNOP, UNOP);                                // 6: delay slot
        g.add(iaddiu(5, 0, 7), UNOP);                     // 7: vi5 = 7
        g.add(lo1(0x30, 2, 1, 3), UNOP);                  // 8: iadd vi3, vi1, vi2 = 5
        g.add(lo1(0x31, 5, 2, 4), UNOP);                  // 9: isub vi4, vi2, vi5 = -2
        g.add(lo1(0x34, 5, 4, 6), UNOP);                  // 10: iand vi6, vi4, vi5 = 6
        g.add(lo1(0x35, 5, 3, 7), UNOP);                  // 11: ior vi7, vi3, vi5 = 7
        // the backup rule
        g.add(iaddiu(8, 0, 1), UNOP);                     // 12: vi8 = 1
        for (int k = 13; k < 18; ++k) g.add(LNOP, UNOP);
        g.add(isubiu(8, 8, 1), UNOP);                     // 18: vi8 = 0
        g.add(lo0(0x28, 0, 8, 0, 2), UNOP);               // 19: ibeq vi8, vi0, 22 -> sees vi8 = 1: not taken
        g.add(LNOP, UNOP);                                // 20
        g.add(iaddiu(9, 0, 1), UNOP);                     // 21: runs
        g.add(LNOP, UNOP | EBIT);                         // 22
        g.add(LNOP, UNOP);
        CHECK(v.run(g));
        CHECK(v.c.vi[1] == 0 && v.c.vi[2] == 5);
        CHECK(v.c.vi[3] == 5 && v.c.vi[4] == 0xFFFE && v.c.vi[6] == 6 && v.c.vi[7] == 7);
        CHECK(v.c.vi[8] == 0 && v.c.vi[9] == 1);
        CHECK(v.c.stats.branch_vi_backup >= 1);
    }
}

// VU0's sizes: the pc wraps at 512 pairs, data addresses at 256 qwords (pc `& 0x1FF`, byte addresses
// `& 0x0FFF`); LQ / SQ / LQI / SQI / ILW / ISW / MTIR / MFIR / MOVE / MR32; the I bit (the upper reads the old I).
TEST(vu0_core_sizes_moves_and_ibit) {
    for (const bool pt : {true, false}) {
        Vu0Core v(pt);
        for (u32 l = 0; l < 4; ++l) v.c.vf[1][l] = 0x1000u + l;
        v.c.vf[1][0] = 0xFFFF8001u;   // MTIR takes the low 16 bits
        v.set_qw(47, 9, 8, 7, 6);
        v.set_qw(0, 0.5f, 0.25f, 0, 0);
        v.c.vi[8] = 255;
        v.c.I = t::fbits(3.0f);
        v.set_qw(3, 1, 2, 3, 4);
        Prog g;
        g.add(iaddiu(1, 0, 300), UNOP);                     // 510: vi1 = 300
        g.add(sq(XYZW, 1, 1, 0), UNOP);                     // 511: sq vf1, 0(vi1) -> qword 44
        g.add(lq(XYZW, 2, 1, 3), UNOP);                     // 0: lq vf2, 3(vi1) -> qword 47
        g.add(lo3(0x35, XYZW, 8, 1), UNOP);                 // 1: sqi vf1, (vi8++) -> qword 255
        g.add(lo3(0x34, XYZW, 9, 8), UNOP);                 // 2: lqi vf9, (vi8++) -> qword 256 & 255 = 0
        g.add(lo0(0x05, Y, 1, 0, 5), UNOP);                 // 3: isw.y vi1, 5(vi0)
        g.add(lo0(0x04, Y, 6, 0, 5), UNOP);                 // 4: ilw.y vi6, 5(vi0)
        g.add(lo3(0x3C, 0, 10, 1), UNOP);                   // 5: mtir vi10, vf1x
        g.add(lo3(0x3D, X | Y, 11, 10), UNOP);              // 6: mfir.xy vf11, vi10
        g.add(lo3(0x30, X | Z, 12, 2), UNOP);               // 7: move.xz vf12, vf2
        g.add(lo3(0x31, XYZW, 13, 2), UNOP);                // 8: mr32 vf13, vf2
        g.add(lq(XYZW, 14, 0, 3), UNOP);                    // 9: vf14 = (1, 2, 3, 4)
        for (int k = 10; k < 14; ++k) g.add(LNOP, UNOP);
        g.add(t::fbits(2.0f), up1(0x1E, X, 0, 14, 15) | IBIT);  // 14: muli.x vf15, vf14, I (old: 3)  | I = 2
        g.add(LNOP, up1(0x1E, Y, 0, 14, 15) | EBIT);        // 15: muli.y vf15, vf14, I (2)
        g.add(LNOP, UNOP);
        CHECK(v.run(g, 510));
        CHECK_BITS(v.m.w[44 * 4 + 1], 0x1001u);
        CHECK(v.f(2, 0) == 9 && v.f(2, 3) == 6);
        CHECK_BITS(v.m.w[255 * 4 + 2], 0x1002u);
        CHECK(v.f(9, 0) == 0.5f && v.c.vi[8] == 257);
        CHECK_BITS(v.m.w[5 * 4 + 1], 300u);
        CHECK(v.c.vi[6] == 300);
        CHECK(v.c.vi[10] == 0x8001);
        CHECK_BITS(v.c.vf[11][0], 0xFFFF8001u); CHECK_BITS(v.c.vf[11][2], 0u);
        CHECK(v.f(12, 0) == 9 && v.f(12, 2) == 7); CHECK_BITS(v.c.vf[12][1], 0u);
        CHECK(v.f(13, 0) == 8 && v.f(13, 1) == 7 && v.f(13, 2) == 6 && v.f(13, 3) == 9);
        CHECK(v.f(15, 0) == 3 && v.f(15, 1) == 4);
        CHECK(t::bitsf(v.c.I) == 2.0f);
        CHECK(v.c.tpc == 17);  // the pair after the delay slot at 16
    }
}

TEST_MAIN("test_vu0_core")
