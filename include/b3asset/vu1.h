// VU1 micro-mode interpreter and GIF PATH1 packet reader, for the offline "shader accuracy" check
// (docs/native-renderer.md §11): run one of the game's VU1 microprograms on decoded mesh input and read back what it
// would send to the GS.
//
// Semantics (VU User's Manual ch. 2-6, VU Instruction Manual; research/ext/PS2Docs in the decomp):
//   - Arithmetic: b3fp::ref (PS2 float model: no denormals/Inf/NaN, truncating; include/b3host/ps2float.h).
//   - Timing, enough to reproduce software-pipelined code: one pair per cycle; an instruction stalls until every VF
//     lane it reads is ready (FMAC and VF-writing lower ops: 4 cycles); MAC/STATUS/CLIP flags become visible 4 cycles
//     after the FMAC/CLIP issued; Q after 7 (DIV, SQRT) or 13 (RSQRT) cycles, P after the EFU latency, both read
//     without interlock (old value while pending), WAITQ/WAITP and a second DIV/EFU op stall.
//   - Conditional branches reading a VI written by the immediately preceding pair see the value from before that
//     write (and before any other write to it within the last 4 cycles): PCSX2 microVU's rule.
//   - Within a pair both slots read the state before the pair; if both write the same VF the upper result wins.
//   - E bit: the next pair (delay slot) runs, then the program stops; TPC = the pair after the delay slot, which is
//     where MSCNT continues.
//   - XGKICK snapshots the GIF packet at that moment (the real transfer runs concurrently; the game's programs
//     double-buffer their output, so the snapshot is what reaches the GS).
#ifndef B3ASSET_VU1_H
#define B3ASSET_VU1_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "b3asset/vif.h"

namespace b3asset {

// One microprogram image: instruction address -> (lower, upper) words, from a decomp `*.textbin.bin` (DMA chain with
// VIF MPG commands) or a raw blob.
struct VuMicroprogram {
    std::uint32_t lower[2048] = {}, upper[2048] = {};
    std::uint32_t loaded = 0;  // instructions loaded
    bool load_textbin(const std::vector<std::uint8_t>& image, std::string* why = nullptr);
};

// GS output of one XGKICK: primitives as vertices with the register values in effect when each vertex kick happened.
struct GifVertex {
    std::uint32_t prim = 0;            // PRIM register (type in bits 0-2, IIP, TME, FGE, ABE, AA1, FST, CTXT)
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
    float q = 1.0f, s = 0.0f, t = 0.0f;  // ST/Q as floats (ST register; Q from RGBAQ)
    std::uint32_t u = 0, v = 0;        // UV register (FST=1)
    std::uint32_t x = 0, y = 0, z = 0; // XYZ: x/y 12.4 fixed point, z 32 or 24 bits
    std::uint8_t f = 0;                // fog (XYZF2/3)
    bool adc = false;                  // XYZ*2 ADC bit / XYZ*3: no drawing kick
};
struct GifPacket {
    std::vector<GifVertex> verts;
    std::vector<std::pair<std::uint8_t, std::uint64_t>> ad;  // A+D register writes (reg, value) in order
    std::uint32_t qwords = 0;
};
// Reads the GIF packet at qword `addr` of VU memory (PACKED and REGLIST tags, until EOP). Returns false on a
// malformed packet.
bool read_gif_packet(const VuMemory& mem, std::uint32_t addr, GifPacket& out, std::string* why = nullptr);

struct Vu1Stats {
    std::uint64_t pairs = 0, cycles = 0, stall_cycles = 0, xgkicks = 0;
    std::uint64_t q_read_pending = 0, p_read_pending = 0, branch_vi_backup = 0;
    // vu0: FMAND / FMOR / FMEQ / FSAND / FSEQ / FSOR reads while no flag update of the current run had become visible
    // (they see flags from before the launch: macro mode's, which the port does not model)
    std::uint64_t flag_reads_prelaunch = 0;
    std::map<std::string, std::uint64_t> notes;
};

class Vu1 {
public:
    explicit Vu1(VuMemory& mem) : mem_(mem) { reset(); }
    void reset();

    // Runs from instruction `pc` until the program ends (E bit) or `max_cycles`. `top`/`itop` are what XTOP/XITOP
    // read. Returns false on an error (unknown opcode, cycle limit) with the reason in `why`.
    using KickFn = std::function<void(std::uint32_t qw_addr)>;
    bool run(const VuMicroprogram& prog, std::uint32_t pc, std::uint32_t top, std::uint32_t itop, const KickFn& on_kick,
             std::string* why = nullptr, std::uint64_t max_cycles = 2000000);
    std::uint32_t tpc = 0;  // where the last run stopped (MSCNT continues here)
    // native3: the unit's memory sizes as address masks (VU1: 2048 micro instructions, 1024 data qwords; VU0: 512 /
    // 256, its 4 KB memories). The same core runs VU0 micro mode (vcallms) on the macro-mode registers (tools/native).
    std::uint32_t pc_mask = 2047, qw_mask = 1023;
    // vu0: the VU0 pair timing (the golden runs' VU0 timing): one pair per cycle and no stalls,
    // so MAC flags show 4 pairs and Q 7 / 13 pairs after their op; WAITQ makes a pending Q visible at once and a new
    // DIV / SQRT / RSQRT first commits the pending one. Off (default): the stall model above (VU1's proven behaviour).
    bool pair_timing = false;
    // vu0: the end of a launch as the EE sees it: everything in flight (flags, CLIP, Q, P) committed, no VF result or
    // FDIV / EFU still busy, no VI write history (the next launch starts from settled state, as on the hardware
    // between two VCALLMS). VU1 callers do not use it.
    void settle();

    // Architectural state (visible values).
    std::uint32_t vf[32][4];
    std::uint16_t vi[16];
    std::uint32_t acc[4], I, Q, P, R;
    std::uint32_t mac = 0, status = 0, clip = 0;
    Vu1Stats stats;

private:
    struct PendingFlags { std::uint64_t ready; std::uint32_t mac, status; };
    struct PendingClip { std::uint64_t ready; std::uint32_t bits; };
    VuMemory& mem_;
    std::uint64_t cycle_ = 0;
    std::uint64_t vf_ready_[32][4];
    std::vector<PendingFlags> pflags_;
    std::vector<PendingClip> pclip_;
    bool q_pending_ = false, p_pending_ = false;
    std::uint64_t q_ready_ = 0, p_ready_ = 0, fdiv_busy_ = 0, efu_busy_ = 0;
    std::uint32_t q_next_ = 0, p_next_ = 0, q_status_ = 0;
    struct ViWrite { std::uint64_t cycle; std::uint8_t reg; std::uint16_t before; bool eligible; };
    std::vector<ViWrite> vi_hist_;
    std::uint64_t pflag_commits_ = 0;   // flag updates made visible so far (flag_reads_prelaunch)
    void commit(std::uint64_t now);
    void commit_q();
    std::uint32_t& w(std::uint32_t qw, int lane) { return mem_.w[(qw & qw_mask) * 4 + lane]; }
};

}  // namespace b3asset

#endif
