// platform_vu0.h -- VU0 micro mode (native3, then vu0; the plan of 2026-11-07): VU0's 4 KB micro and data
// memories, VCALLMS / VCALLMSR, and VIF0's MPG, with the macro-mode registers (a b3asm::State: VF, VI, ACC, I, Q, R)
// as the register file. A launch runs retail's microprogram as generated C++ (src/native/vu0_unit.h: the shipped game
// has nothing else); dev builds (VU0_INTERP=1) also have the shared VU core (src/b3asset/vu1.cpp's Vu1 with VU0's
// sizes: pc_mask 511, qw_mask 255) as the reference. A launch runs synchronously to its E bit and the pipeline is
// drained (Vu1::settle).
//
// What the EE never sees from retail game code [C grep over the decomp's assembly/asm, 2026-11-07]: cfc2 / ctc2 of
// $vi16..$vi19 (STATUS, MAC, CLIP) or $vi27 (CMSAR0), vclip, bc2t / bc2f, vcallmsr. So MAC / STATUS / CLIP live only
// in the core, persisting from one launch to the next. Macro mode does not update them here (macro-mode ctc2 / FMAC
// writes of control 17 would refill the MAC history); a launch's flag read that still sees pre-launch flags is counted
// (B3_VU0_STATS=1 prints it; 0 means the difference cannot have mattered).
//
// Timing: B3_VU0_TIMING=pair (default; pair-counted latencies, no stalls: Vu1::pair_timing), hw (the core's
// stall model, the hardware's [L]), both (pair is used, hw runs on a copy and every launch whose registers or data
// memory differ is counted and the first few printed).
#ifndef B3NATIVE_PLATFORM_VU0_H
#define B3NATIVE_PLATFORM_VU0_H

#include "b3host/eeasm.h"

#include <cstdint>
#include <cstdio>
#include <functional>

namespace b3native::platform {

// The memories as bytes (micro: 512 64-bit pairs, lower word first; data: 256 qwords).
std::uint8_t* vu0_micro_bytes();   // 4096 bytes; call vu0_micro_changed() after writing
std::uint8_t* vu0_data_bytes();    // 4096 bytes
void vu0_micro_changed();
// VCALLMS entry (the pair index, (word >> 6) & 0x7FFF; VCALLMSR: CMSAR0): runs on `s` (VF, VI, ACC, I, Q, R) to the
// E bit. `caller_pc` is for the reports only.
void vu0_micro_call(b3asm::State& s, std::uint32_t entry, std::uint32_t caller_pc = 0);
std::uint64_t vu0_micro_calls();

// H4: a VIF0 transfer (HwModel::on_vif0_kick: CHCR, TADR) loads the retail VU0 image whose DMA chain is at TADR -- one
// of the four (C decomp assembly/assets/vu game_004787D0 / 00479610 / 0047F6D0 / 004839E0), the only uploads
// CGtVu0CodeManager makes -- by its address (Vu0Unit::vif0_upload); anything else ends the run. An interpreter build
// (dev) also walks the chain's words (read32: guest memory) through VIF0's MPG into micro memory and checks the
// SHA-256 path finds the same image.
void vif0_upload(std::uint32_t chcr, std::uint32_t tadr, const std::function<std::uint32_t(std::uint32_t)>& read32);

// Counters (B3_VU0_STATS=1 prints them at exit).
void vu0_report(std::FILE* out);
// vu0n: native_play --strict -- a launch no generated program covers ends the run in an interpreter build too
void vu0_set_strict(bool on);
// The end-of-run line: launches, generated, interpreted, without a generated program (Vu0Unit::summary)
void vu0_summary(std::FILE* out, const char* prefix);

}  // namespace b3native::platform

#endif
