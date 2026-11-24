// game_program.h -- ghvariant: the game programs (SLUS_210.50) the port accepts, by size and FNV-1a 64 of the whole file.
//
// The port runs the retail program's code natively (generated from the decomp), and loads the file only for its data. A
// file is accepted when it is the retail US program or a known variant of it that changes nothing the port runs or reads
// on the host side:
//   - retail: the NTSC-U disc's SLUS_210.50.
//   - online-server patch: the retail program patched for a community online server. 110 bytes differ, in 4 places: the
//     lobby host string (file 0x3C6F61) and the two TOSAC.txt URLs (file 0x3C9217 and 0x3D39D7), pointing at that server
//     instead of the original one, and one instruction of func_0026BEE0 (the DNAS result handler [C decomp src/b3/_unclassified/func_0026BEE0.cpp], vaddr
//     0x26BEF8: `sltu v0, zero, v0` -> `addiu v0, zero, 0`: its "DNAS still busy" test always false). None of them is in
//     what the host reads from the file (the sun-flare table 0x4BB610 / 0x4E1978, b3asset/sunflare.h; the font slots
//     0x4E1584..0x4E1598 and the fonts and dictionaries they point at, src/b3launcher/gameart.cpp); the instruction is
//     never run (native code), and the strings are read only by the game's online code, which the port never reaches
//     (no network adapter: online_message.h). With it the game behaves as with the retail file (tests/test_launcher.cpp
//     program_variants; a race run gives the same shots).
// Replays and records keep the retail fingerprint (kRetailProgramFnv) as the program's identity: the variants run the same
// game.
#ifndef B3PORT_GAME_PROGRAM_H
#define B3PORT_GAME_PROGRAM_H

#include <cstdint>
#include <string>

namespace b3port {

inline constexpr std::uint64_t kRetailProgramSize = 4073816;                 // [C] retail SLUS_210.50
inline constexpr std::uint64_t kRetailProgramFnv = 0x790a7666929b62eaull;    // [C] FNV-1a 64 of the whole file

struct KnownProgram {
    std::uint64_t size, fnv;
    const char* name;   // for logs: "the retail program", ...
    bool retail;
};
inline constexpr KnownProgram kKnownPrograms[] = {
    {kRetailProgramSize, kRetailProgramFnv, "the retail program", true},
    {kRetailProgramSize, 0xeeeadf1d5e0bcfcbull, "the retail program with a community online-server patch", false},
};

// The guest addresses where the known variants differ from the retail program (once loaded: the ELF's vaddrs), end
// exclusive: the replays' RAM checkpoints leave them out (native_play replay_ram_hash, "ram_hash" 2), so a recording made
// with one program plays back with the other. Addresses only: what the bytes are is the program's own.
struct ProgramRange { std::uint32_t begin, end; };
inline constexpr ProgramRange kVariantRanges[] = {
    {0x0026BEF8u, 0x0026BEFCu},   // func_0026BEE0's DNAS "busy" test
    {0x004C6E61u, 0x004C6E74u},   // the lobby host string
    {0x004C9117u, 0x004C9143u},   // a TOSAC.txt URL
    {0x004D38D7u, 0x004D3903u},   // the other TOSAC.txt URL
};

// A replay's "ram_hash" entry: no entry (every recording made before) the full hash (0); "2" the variants' places left out
// (1); anything else a form this build does not know (-1: native_play refuses the replay, naming it)
inline int ram_hash_form(const std::string* v) {
    if (!v) return 0;
    return *v == "2" ? 1 : -1;
}

// The known program with this size and FNV-1a 64; nullptr: not one the port runs
inline const KnownProgram* known_program(std::uint64_t size, std::uint64_t fnv) {
    for (const KnownProgram& p : kKnownPrograms)
        if (p.size == size && p.fnv == fnv) return &p;
    return nullptr;
}

}  // namespace b3port

#endif
