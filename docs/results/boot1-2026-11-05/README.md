# boot1 results (2026-11-05): the strict native boot on the largest generated set

Lane boot1 (milestone S0 -> S5/S6 of `docs/standalone.md`). Port tree: boot1 = main 3914446 + native2 efd7ef3 + the
boot1 commits. Decomp snapshot `../decomp-snap/native2` (= s0native, decomp 0ab506ea32). Captures and the ELF are read from a
local install, never copied.

## The native set

`config/native_select.txt` (tools/native/native_select.py): the 2,323 functions of config/game_overrides.txt
plus every decomp function that candidates.py generates and compiles alone (2,949 more). Generated together
with `--keep-going` into build/gen_native (not committed): **5,263 functions in 4,518 units**; 2 units fail only
together (`gen_native_failed.tsv`: B3PopcornRendererPS2.cpp, B3PlayerCameraManager.cpp; 2 of config/game_overrides.txt's functions among them).
`make native` links it; the table holds **5,336** entries (5,263 game + libm + 50 platform).

`generation_summary.txt` (candidates over all 5,649 functions with decomp C, after the boot1 generator fixes):
5,272 ok, 374 gen_fail, 3 compile_fail. Up from native2's 5,264 by main and the 7 static initialisers.

## Where the strict boot stops (`boot_strict.txt`)

crt0 (host code) -> `_InitSys` / `FlushCache` stubs -> main `func_0012EB30` -> static init (all 158, natively) ->
`Construct__7CB3GameFv` -> `ConstructHardware` (IOP start-up, stubs) -> `PreparePowerOff` -> `CGtThreadMgr::
EnableVBlankWait` (INTC) -> `func_00228160` -> `CB3GraphicsManager::OpenRenderWare` -> **`RwEngineInit` (RenderWare
3.6, library entry point, no C)**. The same stop with `--disc-args` (`boot_disc_args.txt`).

**Ran natively: 217 distinct table functions entered through dispatch, 1,529 calls** (99 matched game, 100 draft
game, 2 libm, 16 platform). A lower bound: direct C++ links between generated units bypass dispatch. Per function:
`entered_strict.tsv`.

## Static initialisers against retail

- Each alone from the fresh image (`sinit_each.txt`): **158 of 158 complete natively, 158 equal retail byte for
  byte** (all RAM below the stack; retail run in tests/r5900_mini.h). Before boot1: 126 of 158.
- All 158 in retail order through func_00100240 against retail's func_00100240 (`static_init_in_order.txt`): equal
  except **50 words, every one the w lane (+0xC) of a 16-byte vector global**: retail builds a CGtV3d on its stack
  (3 words) and copies it with `lq`/`sq`, so the 4th word is whatever earlier code left at that stack slot [C
  unit_004D5350 0x4D58A4-0x4D58C0: `sw` x3 to sp+0x10..0x18, `lq a1, 0x10(sp)`, `sq a1, D_004B7A60`]. Natively that
  word is 0 (each initialiser alone: equal, as the stack is fresh). The stale-lane class.

## Census (`census_summary.txt`, `census_missing_frontier.tsv`)

Static closure from `_start`: 6,002 of 9,092 functions reachable, **5,256 reachable and native** (s0native: 1,047;
merged main+native2 before boot1: 2,348). Frontier (not native, every path from boot native): 549. First on the way:
`func_0019DD00` (main's blank-frame call; draft: a `static float` data definition decomp2host refuses).

## Captures natively (`port7-disc_native.tsv`)

`build/native_check --tsv ... $P/captures/port7-disc` (946 functions) on the boot1 table, crossed with the
s0native verdict: **586 PASS** (s0native 280); 314 MISSING; 37 DIFF (12 of them newly reachable: nested callees now
native but unproven, mostly w lanes and function-pointer words); 6 ERROR; 1 CRASH; 2 ORACLE (the mini: COP0 in
ConstructHardware's syscalls, a REGIMM in Update__18CB3SoundDistortionFv).
