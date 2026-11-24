// stutter: a tick's schedule stamp (b3r_submit_at, include/b3live/inproc.h InprocRecord::sched): the host steady-clock
// time (ns) its VBlank was due on the pacer's schedule (tools/native/fast_load.h Pacer::stamp_ns; 0: none). Bit 0 marks a
// VBlank that started more than a period late (the game thread catching up after a stall): its arrival lag says how
// long the stall was, not how late a tick normally comes, so the display clock leaves it out of the margin
// (src/b3replay/clock.h step_sched). The time is the stamp with bit 0 cleared (1 ns: nothing else needs it cleared).
#ifndef B3LIVE_SCHED_STAMP_H
#define B3LIVE_SCHED_STAMP_H

#include <cstdint>

namespace b3live {

constexpr std::uint64_t kSchedLate = 1;
inline std::uint64_t sched_stamp(std::uint64_t ns, bool late) { return ns ? (ns & ~kSchedLate) | (late ? kSchedLate : 0) : 0; }
inline bool sched_late(std::uint64_t s) { return (s & kSchedLate) != 0; }

}  // namespace b3live

#endif
