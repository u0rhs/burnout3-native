// b3host/vubind.h -- VU0 register binding of native VU operations (native3, item 3: the "=j" operand binding).
//
// Retail's partial-mask VU writes keep the other lanes of the physical register MWCC chose (`vadd.xyz $vf1, $vf0,
// $vf0; sqc2 $vf1, k(a0)` stores vf1's stale w: func_001D0A90, func_001AB010), and every VU write leaves that register
// for later code to find. Ours computes on C++ values (the GtMathPs2 shadow headers, eeasm's "=j" locals), so its
// partial writes take the other lanes from the destination object or the first source, and the guest's VU registers are
// never written: the stale-lane class (port6 class 7; port14 2.1).
//
// With B3_VU_BIND, every VU operation with a destination (b3vu::add .. abs / opmula / opmsub, eeasm's vmove / vmr32 /
// vftoi / vitof / vmax / vmini / vabs) calls enter() and leave(), out of line, so the return address of enter() names
// the operation's site (each inlined copy has its own call). A table, per binary, maps a site to the retail
// instruction it stands for: its destination, its source registers and whether a source is a broadcast (bc / Q / I
// forms):
//   Model:  enter() copies the non-broadcast source values into b3asm::state().vf[fs/ft] (what retail's lqc2 / an
//           earlier op left there) and returns state().vf[dst] as the destination's previous value; leave() writes the
//           result there (ACC: state().acc). With the guest's VU registers copied into state() at an override's entry
//           and back at its exit (B3PORT_VU0_SYNC), the stale lanes and the registers left behind are retail's for
//           every bound site.
//   Record: enter() appends (site, kind, mask) to t_trace; tools/native/mini_check aligns that with the retail run's
//           COP2 instructions on captures and writes the table (--vu-record).
// Off (or no B3_VU_BIND): the operations are unchanged.
#ifndef B3HOST_VUBIND_H
#define B3HOST_VUBIND_H

#include "b3host/ps2types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b3host::vubind {

enum class Mode : std::uint8_t { Off = 0, Record = 1, Model = 2 };
inline Mode g_mode = Mode::Off;

enum Kind : std::uint8_t {
    kAdd = 1, kSub, kMul, kMadd, kMsub, kMax, kMin, kAbs, kMove, kMr32, kFtoi, kItof, kOpmula, kOpmsub, kKinds
};
// One event of ours' run: kind != 0 a native operation (a = site, mask = b3vu lanes x=1 .. w=8); kind 0 a COP2
// instruction an interpreted callee ran (a = pc, b = the instruction word), appended by the checker's mini.
struct Ev {
    std::uint32_t a, b;
    std::uint8_t kind, mask, has;   // has: 1 s, 2 t recorded (the operation's source values, for the value-checked alignment)
    u_long128 s, t;
};
std::vector<Ev>& trace();   // this thread's

const u_long128& enter(unsigned kind, unsigned mask, const u_long128& d, const u_long128* s, const u_long128* t) noexcept;
void leave(const u_long128& r) noexcept;

// Table lines "<site hex> <dst> <fs> <ft> <flags>" (dst 32 = ACC; flags 1: fs is a broadcast, 2: ft is a broadcast, 4:
// the base is ours' d, i.e. retail loaded the register just before, the result is still written to it).
bool load_table(const char* path, std::string* error);
std::size_t table_size();
std::uint32_t site_of(const void* return_address);   // image offset of a code address

// A retail COP2 instruction decoded for alignment: kind (0: no VU register destination: moves, Q unit, flags), mask
// (b3vu order), dst (32 = ACC), fs, ft, flags as in the table.
struct Retail { std::uint8_t kind, mask, dst, fs, ft, flags, bcl; };   // bcl: broadcast lane of ft (4: Q / I, 5: none)
Retail decode(std::uint32_t word);

// Record: one call's retail COP2 stream (every COP2 instruction and lqc2: pc, word, the fs / ft registers before it)
// aligned with ours' events of the same call; each native site votes for the retail instruction it stands for
// (value-checked: same kind and lanes, the same source values in the written lanes). write_votes appends
// "V site word pc votes base_d" lines and "A aligned failed" (tools/native/vubind_table.py makes the table).
struct RetailEv { std::uint32_t pc, word; u_long128 s, t; };
void align_and_vote(const std::vector<RetailEv>& R, const std::vector<Ev>& O, bool why = false, bool dump = false);
bool write_votes(const char* path);

}  // namespace b3host::vubind

#if B3_VU_BIND
// Each inlined copy must be a site of its own: the operations are always inlined into their callers.
#define B3VU_INLINE __attribute__((always_inline)) inline
#define B3VU_ENTER(K, M, d, s, t) const ::u_long128& b3vu_base_ = ::b3host::vubind::enter((K), (M), (d), (s), (t))
#define B3VU_LEAVE(r) ::b3host::vubind::leave(r)
#else
#define B3VU_INLINE inline
#define B3VU_ENTER(K, M, d, s, t) const ::u_long128& b3vu_base_ = (d)
#define B3VU_LEAVE(r) ((void)0)
#endif

#endif
