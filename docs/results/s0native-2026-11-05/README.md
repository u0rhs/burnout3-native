# s0native results (2026-11-05): the native backend (S0)

Design: `docs/standalone.md` §8. Code: `include/b3host/native_backend.h`, `src/b3host/native_backend.cpp`,
`src/native/` (the one table, the platform tier), `tools/native/` (gen_table, capture reader, native_check,
native_boot, census). Build: `make -j8 native` (Linux, g++; libzstd.so.1). Port tree at 1acff08 (gen/decomp from
decomp 271b0e3f3e: 1,033 functions); function table from decomp 0ab506ea32 (`../decomp-snap/s0native`). Captures
and the ELF are read from a local install, never copied.

## Deliverable 2: generated functions on captured states, natively

`build/native_check --tsv out.tsv $P/captures/port7-disc` (946 functions, one capture each). Each capture runs (a) the generated function under the native backend, every callee native or the
run stops, and (b) the retail function in `tests/r5900_mini.h` (oracle, b3fp::ref). 5 m 37 s, 946 captures.

| Native verdict | Functions |
|---|---:|
| PASS (result register + all RAM + scratchpad equal) | **276** |
| MISSING (a callee has no native version) | 655 |
| DIFF (w lanes, func_003548C0 f0 1 ulp, draft values) | 10 |
| ERROR (guest pointer outside RAM: GetTrackMatrix, viBufReset, GetStageTrackID, CGtSoundVoiceManager::Construct) | 4 |
| CRASH (Update__22CB3SoundRaceCarManagerFv: the draft's null + 0x1914, port8) | 1 |

**276 functions run natively and equal retail** [C
`check/port7-disc_native.tsv`]. With the platform tier's C runtime added afterwards (strcasecmp and the other
str*/mem* functions, the 64-bit libgcc helpers, `__register_global_object`, MWCC's array constructor
`func_00100270`; each checked against the retail function in the mini on random inputs,
`tests/test_native_platform.cpp`, fault injection caught): **280 PASS**, 651 MISSING, the same 15 failures
(`check/port7-disc_native_crt.tsv`). The blocked functions stop at 339 distinct missing callees (with the C runtime;
`check/missing_callees.tsv`: 236 game matched + 46 game draft = to generate; 12 game without C; 45 library entry
points: RWA 15, sce 14, RW 13, lg 2, runtime 1). Top: func_00131E40 (blocks 27), func_0022DA20 (23),
CGtSoundVoiceParams::Construct (21), CB3InputManager::GetAvailableControllers (16), `_rwDMAOpenVIFPkt` (14),
`strcasecmp` (12), CGtCameraBehaviour::Construct (12).

## Deliverable 3: how far boot gets natively

**Update (boot push, after the merge to main f627474/77e6af7).** crt0's library calls are platform-tier stubs now
(`src/native/platform.cpp`): `_InitSys` (a logged no-op: libkernl's timer/alarm/thread/TLB tables in .data
0x484140-0x485840 and .bss 0x4E3240-0x4EAF40 stay as the image left them; the stub log records it), `FlushCache` (modes 0 and 2; anything else fatal), `exit` (ends the program;
fatal if atexit handlers or a cleanup hook are registered). **Strict boot now passes crt0 and stops at main**
(`coverage/boot_strict.txt`): `func_0012EB30` has C (a draft) but does not generate: decomp2host cannot resolve the
extern `D_10000` (the decomp's name for the constant 0x10000, crt0's stack size). That is the first missing game
function. Sent to native2 with the two other generation gaps below.

**The static initialisers** (`coverage/sinit.tsv`: all 158 in retail order with slot, address, size, C status; 38,440
bytes): **not an S1 gap**, all 158 have C in the decomp (94 drafts, 64 matched); none is in gen/decomp. Measured
with a private generation of them plus the boot set (build/ only, not committed; decomp 0ab506ea32;
`build/gs/native_boot`): 149 of 158 generate; **126 run natively, each alone from the fresh image, and equal retail
byte for byte** (each also run from the same image in the mini, gp as crt0 sets it; all RAM below the stack
compared; retail writes 1,853 words across them) [C `coverage/sinit_private_gen.txt`]. The other 32 stop at a
missing callee: 14 at a guest-stack address, because the generated code passes a constructor as a host function
pointer to MWCC's array constructor (`(unsigned char *)__ct__13CB3Im3dVertexFv`; decomp2host must turn a function
used as a value into its guest address), 6 at `_CalculateUV__15CB3MenuImageUVs` (not in that selection), the rest
at their own ungenerated initialiser or a callee. In retail order (`--static-init`), static init therefore stops at
the first initialiser (`coverage/static_init_private_gen.txt`).

`build/native_boot` (`coverage/boot_strict.txt`): crt0 as host code, then each step through the table. **Strict boot
stops at the first call: `_InitSys` (libkernl, S5).** With crt0's library calls skipped (`--skip-crt0-lib`,
`coverage/boot_skip_lib_sinit.txt`): **main (func_0012EB30, a draft) has no generated version**, so no game code runs
from boot yet. Static initialisers alone (`--sinit`): 0 of 158 complete; all 158 have C (94 draft, 64 matched), none
is generated.

Static census (`tools/native/census.py`, `coverage/`): the call closure from `_start` through direct calls, code
pointers and the tables the code addresses (ctor table, vtables; heuristic [L]); library functions are leaves (replaced
whole). 6,002 of 9,092 functions reachable; 1,065 of them native today (with the C runtime). `todo_boot_order.tsv` is the to-do in
depth-first boot order (callees in code order, an approximation of execution order): **4,421 to generate** (game code
with C), **126 for the decomp** (game code without C, S1), **388 library entry points** for the platform tier / S5.
The first ones: `_InitSys` (platform), main, the first sinit func_004D3E00, `__ct__13CB3Im3dVertexFv`,
func_004D3E60, then the CGtInputManagerPS2Pad2 / DualShock functions.

| File | What |
|---|---|
| `check/port7-disc_native.tsv` | per function: native verdict, detail (first missing callee / differing words) |
| `check/port7-disc_native_crt.tsv` | the same with the C-runtime platform tier |
| `check/missing_callees.tsv` | first missing callee per capture, ranked by functions blocked |
| `coverage/boot_strict.txt`, `boot_static_init.txt`, `boot_skip_lib_sinit.txt` | native_boot runs (gen/decomp as committed) |
| `coverage/sinit.tsv` | the 158 static initialisers in retail order: slot, address, size, name, C status, generated |
| `coverage/sinit_private_gen.txt`, `static_init_private_gen.txt` | the same runs with the private generation (measurement) |
| `coverage/summary.txt` | reachable / native by kind and C status |
| `coverage/boot_order.tsv`, `todo_boot_order.tsv` | depth-first boot order; the non-native part with its provider |
| `coverage/reach.tsv`, `missing_frontier.tsv` | breadth-first reach with the first referrer; non-native functions whose path from boot is all native |

## Windows

`tools/native/build_win.sh` builds `native_check.exe` / `native_boot.exe` with the verified MSYS2 mingw-w64 toolchain
into `$WIN/s0native` (own folder; 968 generated units, 4 min at JOBS=8). On Windows each capture runs as
`native_check.exe --child <file>` (no fork). **The Windows batch over port7-disc gives the same verdict and detail as
Linux for all 946 captures** (280 PASS); the one crash is reported as exit 0xC0000005 instead of signal 11 [C
`$WIN/s0native/port7-disc-win.tsv` vs the Linux run]. `native_boot.exe` stops where the Linux one does.
