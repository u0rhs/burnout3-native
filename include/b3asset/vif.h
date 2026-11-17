// VIF1 command interpreter for the game's pre-built geometry packets (offline decoding, docs/native-renderer.md).
//
// Runs a VIF code stream against a model of VU1 data memory (1024 quadwords, as 32-bit lanes): UNPACK in every format
// with STCYCL skipping/filling writes, STMOD offset/difference modes, STROW/STCOL, STMASK masking, sign/zero extension,
// ITOP; MSCAL/MSCALF/MSCNT are "kicks" reported to a callback together with the memory, which is what the VU1
// program would read. Unpack addresses with the FLG bit (TOPS-relative) are taken relative to 0, so every batch
// appears at the same place regardless of the PS2's double buffering. DIRECT/DIRECTHL payloads (GIF data) and MPG
// microcode are skipped and counted. Semantics: EE User's Manual ch. 6 (VIF).
#ifndef B3ASSET_VIF_H
#define B3ASSET_VIF_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace b3asset {

struct VuMemory {
    std::uint32_t w[1024 * 4] = {};
    const std::uint32_t* qw(std::uint32_t a) const { return &w[(a & 1023) * 4]; }
    float f(std::uint32_t a, int lane) const;
};

struct VifStats {
    std::map<std::string, std::uint64_t> unpacks;  // e.g. "V4-32", "V3-8 (mask)" -> count of UNPACK commands
    std::uint64_t kicks = 0, direct_qw = 0, mpg = 0, unknown = 0;
};

class VifInterpreter {
public:
    enum KickKind { kMscal, kMscalf, kMscnt, kEnd };
    using KickFn = std::function<void(const VuMemory&, std::uint32_t itop, KickKind kind, std::uint32_t pc)>;

    // Runs `bytes` of VIF codes at `p` (4-byte aligned stream; `phase` = its offset within a quadword, for DIRECT's
    // alignment). Returns false on a malformed stream (reason in `why`).
    bool run(const std::uint8_t* p, std::size_t bytes, const KickFn& on_kick, std::string* why = nullptr,
             std::size_t phase = 0);
    // Reports the pending unpacked data as a kEnd kick if anything was unpacked since the last kick.
    void flush(const KickFn& on_kick);
    void reset() { *this = VifInterpreter(); }

    VuMemory mem;
    VifStats stats;
    // vu0 (the VU1 / GS check, tools/native/vif1_gs_check): when set, MPG payloads (pair address, lower / upper word
    // pairs) and DIRECT / DIRECTHL payloads (quadwords) are handed out as they are skipped; unset: no change
    std::function<void(std::uint32_t pair_addr, const std::uint8_t* p, std::uint32_t pairs)> on_mpg;
    std::function<void(const std::uint8_t* p, std::size_t qw)> on_direct;
    std::uint32_t row[4] = {}, col[4] = {}, mask = 0, cl = 1, wl = 1, mode = 0, itop = 0;
    bool dirty = false;
    // UNPACK destinations of the current batch: start address -> format (cmd & 0xF), cleared after each kick, so a
    // reader can tell which attributes this batch wrote (VU memory keeps stale data from earlier batches).
    std::map<std::uint32_t, int> batch_unpacks;
};

}  // namespace b3asset

#endif
