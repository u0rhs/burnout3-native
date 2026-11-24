# VU0 micro mode (vcallms) -- native3's handoff (2026-11-07), then the vu0 lane

## State (2026-11-10, vu0n: VU0 fully native)

- **The shipped game runs no VU0 microcode through an interpreter.** Every VCALLMS runs one of retail's four
  microprograms (0x4787D0 collision, 0x479610 track renderer, 0x47F6D0 vehicle renderer, 0x4839E0 traffic) as
  generated C++: `gen/native/vu0/vu0_aot_gen.cpp`, committed like the rest of the generated game code (the translated
  programs and each image's SHA-256; no microcode words; `tools/native/bundle_check.py --elf`: CLEAN). A fresh clone
  without the disc or the decomp builds and runs it. `make vu0-aot` regenerates it from the decomp snapshot (or
  `VU0_AOT_ELF=<SLUS_210.50>`; both give the same file) and fails, leaving it unchanged, when an image does not match
  `tools/native/vu0_aot_images.sha256`; `make native-bundle` regenerates it with the bundle.
- **Matching**: Vu0Unit finds the image in micro memory by the SHA-256 of its pairs (cached per micro-memory content).
- **The interpreter** (the shared VU core, src/b3asset/vu1.cpp) is compiled out of native_play / native_boot by default
  (`make play`, package_linux.sh, build_win.sh / package_win.sh): b3asset::Vu1 is not in the link (nm), and a launch that
  no generated program covers (an unknown image, an entry past its pairs) is a fatal error naming the image's SHA-256.
  `make VU0_INTERP=1` (dev builds) compiles it in for `B3_VU0_AOT=0` (interpreter only) and `B3_VU0_AOT=check`
  (both on every launch, compared); test_vu0_core and test_native_platform always have it.
- **Coverage**: native_play's end-of-run line `native_play: vu0: N launches: G generated, I interpreted, U without a
  generated program; aot on|check|off, interpreter compiled in|out`; `native_play --strict` makes an uncovered launch
  fatal in a dev build too; race_check fails on U > 0, on check differences, and with STRICT=1 on I > 0 (aot on) or a
  missing line, and prints the shots' SHA-256.
- **Proof** (2026-11-10, Linux): a dev build with `B3_VU0_AOT=check STRICT=1` over the nine race_check scenarios
  (race, pause, photo, music, takedown, timeattack, roadrage, crash, worldtour): 132,703,109 launches checked against
  the interpreter, 0 differences, 0 without a generated program (per scenario 10.1 M to 21.2 M). The default build
  (no interpreter), `STRICT=1`: all nine PASS, the same 132,703,109 launches all generated, 0 interpreted; every race
  shot identical to the build before this change (e5a04ae9); the photo scenario's free-camera shots (14492, 14842)
  differ between any two runs, of the old build too. bundle_build_check (gen/native, no decomp, no disc): PASS, the
  four programs linked and no b3asset::Vu1. make test, save_check: PASS; tools/native/build_win.sh (RENDER=1) builds,
  its native_play.exe with the four programs and no b3asset::Vu1 (nm). 5 SHA-256 lookups in a race (the rest cached).
- **Open**: run the scenarios on Windows and the Mac with the default build.
- **vu0flags (1.4.1, issue #15: VU0 micro mode ~25% of the game thread on a slow CPU, the flags and the per-pair commit a
  large part of it)**: `tools/native/vu0_aot.py` decides from each program's control flow what a pair needs at run time.
  A census of the four images: no STATUS reader (no FSEQ / FSAND / FSOR / FSSET), MAC read by FMEQ / FMAND / FMOR at 50
  pairs against 811 flag pushes, no CLIP; and no EE code reads VU0's vi16 / vi17 / vi18 (cfc2). So a flag push is
  generated only when its MAC can be observed: as the newest push at least 4 pairs before a MAC read on some path, or as
  the last push before the launch ends (the settled MAC, which crosses launches, stays exact). 74 of the 811 pushes are
  kept; STATUS's Z S U O bits and their sticky copies are not kept up by the others (nobody can read them); its I / D
  bits (DIV / SQRT / RSQRT) are. The commit at a pair checks the flag slot only where a kept push can be 4 pairs back
  (89 of 1346 pairs), Q / P only in an image with DIV / EFU ops, CLIP nowhere; a VI write keeps its history entry
  (Rt::branch_vi) only where a conditional branch can follow within 4 pairs. The control flow is over-approximated
  (after a branch's delay slot both the target and the next pair), so a kept push or a check is never missing.
  `B3_VU0_AOT=check` compares STATUS's I / D bits only (vu0_unit.cpp).
- **vu0lanes (1.4.1)**: an FMAC instruction (ADD / SUB / MUL / MADD / MSUB and their broadcast, Q and I forms) with two or
  more dest lanes is generated as one call of `b3fp::vufast4` (include/b3host/ps2float_hw.h) on the register's four lanes:
  on x86 under Ps2FloatEnv the four clamp255s as a compare and a blend, then one mulps / addps / subps (a packed lane is
  the scalar operation on that lane: the same RZ rounding, the same FTZ / DAZ, the same inputs), and a dest lane whose
  exponents are more than 28 apart done again by hw::add / sub (ref there); madd / msub keep the product rounded first.
  Elsewhere (no SSE2, or `B3_VU0_FLOAT=ref`) lane by lane as before. 398 such instructions in the four images; MAX / MIN,
  OPMSUB / OPMULA and single-lane ones stay per lane. tests/test_vufloat.cpp checks the four-lane forms against ref lane
  by lane (every operation and shape, every dest mask, exponent gaps 0..60).

## History

VU0 micro mode is not modelled anywhere in the port: `include/b3host/eeasm.h` says "Not modelled ...: VU0 microprograms
(vcallms)" and `tools/decomp2host/eeasm.py` refuses `vcallms` / `vcallmsr` at generation. It blocks the native race
(boot2's boot), the snapshot resume (S2 / S3, below) and mini_check's oracle on these functions.

## Where retail uses it [C `grep -c vcallms` over the decomp's `assembly/asm`: 36 sites in 13 files]

| sites | file (decomp assembly/asm/...) |
|---|---|
| 11 | game/unit_00272EA0.s (CGtCollisionKDTree::IntersectLine; mini_check ORACLE "unsupported COP2 special1 at pc 0x00273638") |
| 7 | game/unit_00274DC0.s |
| 4 | game/unit_0021E6F0.s (RenderVehicle: mini_check ORACLE at 0x0021FC3C) |
| 4 | b3/_unclassified/B3TrackRendererRuntimePS2_0026A240.s |
| 2 | b3/_unclassified/B3TrackRendererRuntimePS2_0026A610.s |
| 1 each | B3TrackRendererRuntimePS2_0026A1C0.s, B3TrackRendererRuntimePS2_0026A710.s, GtIntersectionPS2_00274C90.s, IntersectFrustum__14GtIntersectionFRC10CGtFrustumRC14CGtExtrudedBox.s, IntersectFrustum__14GtIntersectionFRC10CGtFrustumRC16CGtOrthogonalBox.s, func_0026AF50.s, game/unit_0026A7E0.s, game/unit_001618C0.s (func_001618C0, CB3TrafficSystem::Update's callee: the first resumed frame's stop) |

## Earlier state (vu0 lane, 2026-11-07; commits on branch vu0)

- **Programs** [C decomp asm]: retail uploads VU0 code through CGtVu0CodeManager (gmpCurrentVu0Code; VIF0 chain DMA,
  D0 CHCR 0x145, then sceDmaSync(0)) and switches between four images at run time: 0x4787D0 (GtIntersection /
  collision, 12 sites), 0x479610 (track renderer), 0x47F6D0 (vehicle renderer), 0x4839E0 (traffic). Each fits in 512
  pairs; every op they use is in the shared core; their FMAND / FMOR masks are Z / S bits only (<= 0xFF).
- **What retail's EE never does** [C grep]: cfc2 / ctc2 of $vi16-19 or $vi27, vclip, bc2t / bc2f, vcallmsr. So only
  VF / VI / ACC / I / Q / R cross a launch; MAC / STATUS / CLIP stay in the core between launches.
- **Shared core** (f60c226): src/b3asset/vu1.cpp's Vu1 with pc_mask 511 / qw_mask 255; `pair_timing` = a pair-count
  VU0 timing (no stalls, Q visible 7 / 13 pairs on, WAITQ and a new DIV commit the pending Q); `settle()` = a
  launch's drained end. VU1 output unchanged (180 outputs + corona, cmp).
- **Model** (21b40d1): src/native/vu0_unit.{h,cpp} `Vu0Unit`; src/native/platform_vu0 holds the platform's one.
  `B3_VU0_TIMING` unset (default: `pair_timing`) / `hw` (the stall model) / `both` (hw on a copy, differences printed);
  `B3_VU0_STATS=1` prints launches per (caller pc, entry), pairs, stalls, pre-launch flag reads.
  - EE side: the mini's VCALLMS / VCALLMSR (CMSAR0); generated code: `b3asm::vcallms(State&, entry)` (eeasm.h hook,
    installed by platform_hw / mini_check); eeasm.py translates `vcallms`, `.word 0x4A00xxx8`, `vcallmsr`, $vi27
    (8548281) and refuses a vcallms statement with a "j" operand.
  - VIF0: HwModel::on_vif0_kick (CHCR, TADR) -> Vu0Unit::vif0_upload: the image by its address (one of the four; the
    device-state work); dev builds (VU0_INTERP=1) also walk the chain through MPG into micro memory and check the
    SHA-256 path finds the same image. UNPACK / MSCAL fail loudly (not in retail's VU0 images). The 0x11000000 / 0x11004000 windows: not modelled (no retail access seen; boot2's
    place for them: an HwModel range forwarding to on_vu0_mem).
- **Proofs**: test_vu0_core (6 tests, 91 checks, both timings); test_native_platform vu0_vif0_upload_and_vcallms
  (retail's 0x4787D0 chain from the ELF through the walker == the decomp image). mini_check on the 20 captures native3's run stopped at vcallms: 1,954 launches, 0
  pair_timing-vs-hw differences, 0 pre-launch flag reads; with the vcallms functions themselves generated (ours: generated C
  through b3asm::vcallms): Update__16CB3TrafficSystemFv PASS (func_001618C0, the native race's first vcallms site).
  Remaining DIFFs are generated-code classes (stale VF lanes, frames).
- **Restore** (870dfd1, 4f3f2c7): the dump carries VU0 control 0-31 and the MAC history; the restore seeds R / I / Q /
  CMSAR0 / flags. navproof14 poll 3000 (snap/vu0/navproof14c) --retail: 205 launches, 2 VIF0 uploads, then the main
  thread waits in _rwDMAForceBufferSwap (RW's DMA was mid-dispatch at the snapshot: the restore's in-flight DMA).
- **Open**: 5 vcallms functions do not generate (decomp2host: EGtIntersectionResult top-level data); generated
  UploadVu0Code stores D0 through pcast (needs boot2's T37 MMIO transform); macro-mode MAC / STATUS not modelled (0 pre-launch
  reads so far); the core's MAC has no U / O bits (masks seen: Z / S only). Later: each program as plain C, checked against the interpreter.

## The plan (2026-11-07) and what exists

1. **Shared VU core from src/b3asset/vu1.cpp**, parameterised on the memories, VU0 on the macro-mode registers.
   - **Done, native3-rs d4f1de8**: `Vu1::pc_mask` / `qw_mask` (VU1's 2047 / 1023 as defaults) in run(): pc, branch /
     jump targets, `mem_qw`, `w()`. Not changed: load_textbin, read_gif_packet, XGKICK's `xg_addr & 1023`, XTOP /
     XITOP's `& 0x3FF` (VU1-only).
   - Proof so far: before / after `cmp`-identical outputs of `b3vucheck check TRACKS/AS --samples 50` (122 lines),
     `b3vucheck_car` (3 cars, `--shader`, 44 lines), test_b3asset (`26 tests, 6442 checks, 0 failures`), test_sparkshade
     (`4 tests, 32 checks, 0 failures`).
   - **Still to run before / after** (perf1, 2026-11-07: these are what executes Vu1::run; a b3replay render does not):
     test_popcornshade, b3vucheck_trail / _corona / _spark / _popcorn, `b3envdump --vu`.
2. **VU0 memories in the platform, VIF0 MPG upload; vcallms / vcallmsr in the mini; a b3asm op for eeasm.**
   - **WIP, UNPROVEN, native3-rs dbef249**: src/native/platform_vu0.{h,cpp}: VU0 micro bytes (512 pairs, lower word
     first) / data bytes (256 qwords), `vu0_micro_call(b3asm::State&, entry)` (the shared core with pc_mask 511,
     qw_mask 255; VF / VI / ACC / I / Q / R copied in and out; flags persist in the core); the mini's
     `on_vcallms` (COP2 special1 funct 0x38) wired by the fallback's minis; the restore loads vu0_micro.bin /
     vu0_data.bin. It builds; no run with it has finished (a retail resume was stopped at the handover).
   - **Missing**: VIF0 DMA in HwModel (channel 0 completes at once today, no chain walk; the dump shows chain mode,
     `hwreg 10008000 00000045`, TADR `004787d0` at navproof14 poll 3000): MPG into micro memory, UNPACK into data
     memory, MSCAL / MSCALF / MSCNT; VCALLMSR (CMSAR0, vi27); the EE windows 0x11000000 / 0x11004000 for direct
     access; `b3asm::vcallms(entry)` (a platform hook) and its eeasm.py translation (eeasm.h is included by every
     generated unit: tell boot2 / gen1 before the full rebuild).
   - **Dump**: native3 32ebffc writes vu0_micro.bin / vu0_data.bin.
3. **Check each site's program against retail where possible** (mini_check's oracle covers IntersectLine once the mini
   runs vcallms).
4. Later, not now: each microprogram as plain C, checked against the interpreter, for speed.

Note: the shared core models stalls / pending Q / flags (vu1.h header).
