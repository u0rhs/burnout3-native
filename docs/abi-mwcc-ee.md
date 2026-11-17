# The MWCC 3.0.1b119 EE calling convention (as Burnout 3 retail uses it)

What `call_guest` / `native_entry` (`include/b3host/call.h`) must reproduce so that native code and original code can
call each other. Established 2026-10-31 from two kinds of evidence:

- **Retail**: `orig/SLUS_210.50` asm in the decomp repo (`assembly/asm/...`), with the parameter types from the
  function's mangled E-map name (`config/symbol_addrs_b3names.txt`) and, where one exists, the matched source. Cited as
  `name @ address`.
- **Probe**: our own small C++ files compiled with the decomp's compiler (MWCC 3.0.1b119p, the decomp's flags
  `-O4 -str readonly -Cpp_exceptions off -inline level=8`, under wibo). Sources, the script and the disassembly are in
  `tools/abi_probe/` (`cc.sh probe1.cpp` reproduces `probe1.dis.txt`). Cited as `probe1:function`. A probe shows what
  this compiler does. Where retail has the same case, both agree.

Tags: **[C]** shown by the cited asm, **[L]** likely (inference), **[G]** guess. "Slot" below means one argument
position.

## 1. Registers

| Role | Registers | Evidence |
|---|---|---|
| Integer-class arguments 1-8 | a0-a3, t0-t3 (GPR 4-11), full 64-bit registers | [C] the caller of `BuildPacket__5CBlitFUiUiUiUiUiUiUiUlb @ 0x22719C-0x2271B4` sets a1-a3, t1-t3 (a0, t0 earlier) and puts arguments 9 and 10 on the stack; `DrawPositionMarker__15CB3RacePosition... @ 0x1531C0` reads a0-a3, t0-t3 |
| Float arguments 1-8 | f12-f19 | [C] `probe1:c_all` loads f12..f19 for `t_f10`; `DrawPositionMarker @ 0x153248/0x15324C` (f13, f12) |
| Integer and float slots are numbered **independently** | the n-th integer-class argument takes the n-th GPR whatever floats precede it | [C] `DrawPositionMarker(V3d, const u16*, int, Car*, Car*, float, bool, bool, bool, float)`: the bools are t2, t3, stack, the floats f12, f13 (`@ 0x1531D8, 0x1531E8`); `probe1:t_mixed`, `probe2:t_ff_mixed` |
| Integer-class results | v0, 64-bit (`long`, `long long`, soft `double`); a `u_long128` result uses the whole 128-bit v0 | [C] `GetStageGtID__25CB3HardCodedRaceStageListFii @ 0x3EA0D4` (`ld v0`); `CopyTextureAs32Bit__18CB3GraphicsManagerFP10CGtTextureUiUi @ 0x2263D8` (`paddub v0, s0, zero` = 128-bit move); `probe1:r_ll`, `probe1:r_d` |
| Float result | f0 | [C] `GetFwdFrac__9CB3AILaneCFUsQ29GtMathPs26CGtV3d @ 0x146140` (`min.s f0` in the delay slot of the exit) |
| Callee-saved | s0-s7, fp, ra (saved with `sq`/`sd`), f20-f31 (`swc1`) | [C] every prologue above, e.g. `FirstDownsample__8CB3BloomFUiUiUiUiUiUiUiUiUi @ 0x2653B0` saves s0-s7, fp as quadwords |

## 2. Argument classes

| C++ type | Class | How | Evidence |
|---|---|---|---|
| `int`, `short`, `char`, `bool`, enums, pointers | integer | value in a GPR. Callers sign-extend 32-bit values to 64 bits; `unsigned int` is sign-extended too (loaded with `lw`) | [C] `addiu a3, zero, -0x1` for an `int` `@ 0x1541F0`; `lw a2, 0x4(s0)` for a `Ui` to `BuildPacket @ 0x22719C` |
| narrow types (`bool`, `char`, `short`, `unsigned short`) | integer | **the callee masks or re-extends**: it never trusts the upper bits | [C] `SetActive__10CB3RaceCarFb @ 0x150EE4` (`andi a2, a1, 0xFF`); `GetFwdFrac @ 0x146058` (`andi s0, a1, 0xFFFF`) |
| `long`, `unsigned long`, `long long` (64-bit) | integer | one GPR, all 64 bits | [C] `IsCarUnlocked__10CB3ProfileFUl @ 0x13A678` (`daddu s0, a1, zero`); `probe1:r_ll` |
| `double` | integer | **soft-float**: the IEEE binary64 bits in one GPR; arithmetic goes through `dpadd`/`dpmul`/`fptodp`/`dptofp` | [C] `probe1:t_d` (`double` in a1 between an `int` a0 and a `float` f12; caller builds 2.0 as `0x4000<<48` in a1); `RenderPerfMonDataCallback... @ 0x1300E0` (`fptodp` result v0 to a2 for `sprintf`). No named `double` parameter exists in the 2,604 parsed retail signatures |
| `float` | float | next of f12-f19 | [C] above |
| `u_long128` | integer | one whole 128-bit GPR | [C] `__ct__Q29GtMathPs26CGtV4dF1 @ 0x130740` (proven unit `B3DebugManager_00130740`): `sq a1, 0(a0)` |
| struct/class, trivially copyable, **size <= 8** | integer | packed into one GPR as its memory image (little-endian: the first member is the low word) | [C] `PressKey__28CB3OnScreenKeyboardComponentF23CB3OnScreenKeyboardCell @ 0x34E990` (`sd a1` then `lw 0x68`/`lw 0x6C`); its caller `func_00350CF0 @ 0x350D00` (`ldr`/`ldl a1`, an unaligned 8-byte load of the cell); `DrawGlobeHalo__16CB3Globe2dObjectFQ29GtMathPs26CGtV3d6RwRGBAf @ 0x3B17F8` (4-byte `RwRGBA` in a1); probes: 8-byte `{float, float}`, `{float, int}`, `{int, float}`, 4-byte `{int}`, `{short, char}`, 2-byte `{u8, u8}` (`probe1:t_f2/t_i1/t_c2`, `probe2:t_fi/t_if/t_s3`); a user default constructor does not change it (`probe2:t_a`) |
| struct whose only member is one `float` (size 4) | **float** | next of f12-f19, like a plain `float` | [C] `probe1:t_f1` (callee tail-calls `sinkf(float)` with no move; caller loads f12), `probe2:t_ff_mixed` (`int, {float}, float` -> a0, f12, f13). No retail instance found |
| struct/class, **size > 8** | integer | **the caller copies it to a temporary in its own frame and passes the copy's address** in the next GPR; the callee may read or keep that copy | [C] the 16-byte `CGtV3d`: caller `@ 0x14D770` (`lq v0, 0x30(v0)`, `sq v0, 0xF0(sp)`, `addiu a2, sp, 0xF0`), callee `GetFwdFrac @ 0x146044` (`lq v0, 0(a2)`); `CGtV4d` (`DrawLine__15CB3DebugManagerFRC7CGtLineQ29GtMathPs26CGtV4d @ 0x130768`), `CGtSphere` (`IntersectSphere__14GtIntersectionFQ29GtMathPs29CGtSphereQ29GtMathPs29CGtSphere @ 0x2BF534`: both spheres via a0, a1), `CGtPlane` (`ClipEdgesByPlane__20CB3ConvexHullCollide... @ 0x2B78A4`); probes: 12-byte `{int x3}` (`probe1:t_i3`, caller `addiu a0, sp, 72`), 16-byte `{u_long128}` (`probe2:t_q`, `lq a0, 0(a0)`) |
| struct/class **not trivially copyable** (a user copy constructor, a user destructor, or virtual functions), any size | integer | by address of a caller-made copy, as for size > 8 | [C] `probe1:t_cv`, `probe2:t_b` (copy ctor), `probe2:t_c` (dtor; caller `probe2:c_q` makes the copy at sp+40), `probe2:t_d` (virtual) |

## 3. Stack arguments (past the 8 GPRs or the 8 FPRs)

- There is **no home area** for register arguments: the first stack argument is at the caller's `sp + 0` (the callee's
  entry sp). [C] `CopyToScreen__8CB3BloomFUiUiUiUiUiUiUiUi @ 0x264A38` (frameless leaf: `lwu t5, 0x0(sp)` is the 9th
  integer argument).
- **8-byte slots, in declaration order, shared by both classes.** An argument goes to the stack only when its own class
  (GPRs or FPRs) is full. Integers and floats then interleave in the order they are declared. [C]
  `FirstDownsample__8CB3BloomFUiUiUiUiUiUiUiUiUi @ 0x2655D4/0x2655E4`: arguments 9 and 10 at entry sp+0 and +8 (frame
  0xA0: `0xA0($sp)`, `0xA8($sp)`); `probe2:t_mix` (`int, float` x10): a9 at +0, f9 at +8, a10 at +16, f10 at +24;
  `probe2:t_9f1i`: 9 floats then an `int`, so the int is in a0 and float 9 at +0; `probe1:t_f10`: floats 9 and 10 at
  +0, +8.
- **What is written into a slot.** Integer-class: the 64-bit extended register value (`sd`). Float: 4 bytes at slot+0
  (`sw`/`swc1`), the upper half untouched. The callee reads its type's width at slot+0 (`lbu` for a bool, `lw`/`lwu` for
  32-bit, `ld` for 64-bit). [C] the caller of `BuildPacket__5CBlitFUiUiUiUiUiUiUiUlb @ 0x227188/0x227194` (`sd` of the
  `Ul` and the `bool`); the callee `@ 0x22B6B4` (`ld`) and `@ 0x22B6F4` (`lbu`); `probe2:c_mix` (`sd` ints, `sw`
  floats).
- **`u_long128` on the stack: a 16-byte slot, 16-aligned** (relative to the entry sp, which is 16-aligned). A skipped
  half-slot stays unused. [C] `probe2:t_q10`: int 9 at +0, the quadword at +16 (`lq a0, 32(sp)` in a 16-byte frame),
  the next int at +32; `probe1:t_q9` (quadword at +0, next at +16).
- Small structs (<= 8 bytes) take one 8-byte slot (their memory image at slot+0). The address of a by-copy struct is
  an ordinary integer slot. [C] `probe1:t_s9` (`{int}` at +0, `{int x3}` address at +8, `int` at +16); `probe2:t_i2s`
  (`{int, int}` at +0, read `lw 20(sp)` = +4).
- The caller leaves argument slots it has no value for unwritten (the decomp passes fewer arguments than the
  signature). [C] `DrawSingleVehicleValue__15CB3RacePositionFP17CB3VehiclePhysicsiifb @ 0x1541FC` calls
  `DrawPositionMarker` without storing the 9th argument (decomp source note in `B3RacePosition_001540A0.cpp`).

## 4. Struct results

- **Every struct/class result, whatever its size, is returned through a hidden pointer passed as the first integer
  argument (a0).** `this` and every other argument move one GPR along. The callee stores the result through it and
  **does not return the pointer in v0**. [C] `GetTargetPosition__17CB3AITargetSplineFv @ 0x149E8C` (16-byte `CGtV3d`:
  `sq v1, 0x0(a0)`, `this` in a1, v0 not written); its caller `@ 0x19D8E0` (`addiu a0, sp, 0x40`, a slot in its frame);
  `GetAlignPos__6CB3HUDF9EHUDAlign @ 0x1A02C0` (8-byte `CGtV2d`: result a0, `this` a1, the enum a2); probes: 8-byte
  `{float, float}` (`probe1:r_f2`), 4-byte `{int}` and `{float}` (`probe1:r_i1`, `probe1:r_f1`), 2-byte (`probe1:r_c2`),
  16-byte (`probe1:r_v`: the `int` argument then in a1).
- Narrow scalar results: the callee extends (`andi 0xFF`, `dsll32/dsra32`), and **the caller re-masks anyway**: v0's
  upper bits are not to be trusted for `bool`/`char`/`short`. [C] `probe2:r_b/r_s/r_uc` and `probe2:c_ret`; retail
  `PressKey @ 0x34E9AC` (`andi v1, v0, 0xFF` after the `bool` call `IsControlPressed`).

## 5. Varargs

- A variadic callee spills a1-t3 (everything after its named arguments) as consecutive 8-byte slots ending at its
  entry sp. The register arguments and the caller's stack arguments therefore form one array. [C] `func_0027E540 @
  0x27E544-0x27E55C` (`void (int, ...)`: `sd a1..t3` at sp+0x18..0x48 of its 0x50 frame).
- At a variadic call, `float` is promoted to `double`, a software double, in the **next GPR**, not an FPR. `char` is
  promoted to int. [C] `probe2:c_va` (`vsink("x", f, i, ll, 1.5f, 'c')`: a1 = `fptodp(f)`, a2 = i, a3 = ll, t0 =
  `0x3FF8<<48` (1.5 as a double), t1 = 99); retail `sprintf` callers `@ 0x1300E0` (`jal fptodp`, result v0 to a2).

## 6. Classes, `this`, virtual calls

- `this` is the first integer argument (after the hidden result pointer, §4). [C] every member function above.
- The vptr is at the offset of the class's first declared virtual. The vtable is `{RTTI* or 0, 0, slot0, slot1, ...}`,
  with slot 0 at +8 [C the decomp's `docs/known.md` N102, N119]. A virtual call loads the vptr from `this + vptr_offset` and
  the target from `vtable + slot`, and passes **`this` unchanged** (the object's start, not the vptr's address). [C]
  `SetActive__10CB3RaceCarFb @ 0x150EFC` (`lw t9, 0x1910(a0)`, `lw t9, 0x28(t9)`, `jalr t9`, a0 unchanged).
- **No `this` adjustment exists in retail.** Every one of the 2,780 words of the `.vtables` range (0x4DDAA0-0x4E0610)
  is a code address (1,904), zero (874), or one of the two RTTI pointers of `std::exception`/`std::bad_exception`. No
  vtable has a non-zero offset word, no secondary (multiple-inheritance) vtable exists, and no this-adjusting thunk
  (`addiu a0, a0, -N; j`) appears in the game code. [C] scan of `orig/SLUS_210.50` (the same check over the 113 sized
  `__vt__*` symbols: header words `{0|RTTI, 0}`, body all code). MWCC's multiple-inheritance thunk layout is therefore
  irrelevant to this binary, and was not probed.
- A constructor returns `this` in v0. [C] `__ct__Q29GtMathPs26CGtV4dF1 @ 0x130744` (`daddu v0, a0, zero`).

## 7. What `call.h` does with this (2026-10-31)

| Case | Before | Now |
|---|---|---|
| struct by value <= 8 bytes, trivially copyable | rejected | one GPR (memory image). For a **4-byte** struct the caller must say whether it is a lone `float` (FPR) or not: specialise `b3host::abi::lone_float<T>` (true or false). Without that it is a compile error, because C++ cannot see member types |
| struct by value > 8 bytes, trivially copyable | rejected | `call_guest` copies it into a 16-aligned temporary on the guest stack and passes the address. `native_entry` loads it from the pointer |
| struct not trivially copyable | rejected | still rejected: MWCC passes it by address [C], but the adapter cannot byte-copy a non-trivially-copyable host object. Pass a `gp<T>` to a guest copy explicitly |
| struct result (any size) | rejected | hidden pointer in a0. `call_guest` gives the callee a guest-stack slot and loads the result. `native_entry` stores the result through the pointer (a recorded store) and leaves v0 alone |
| more than 8 integer or 8 float arguments | runtime `fatal` | stack slots as in §3, laid out at compile time. `call_guest` writes them below a new guest sp. `native_entry` reads them from `CallArgs::sp` (the entry sp, set by the backend) |
| `u_long128` past 8 GPRs | runtime `fatal` | 16-byte, 16-aligned stack slot |
| `double` | rejected | soft-float bits in one GPR (argument and result) |
| `bool`/`char`/`short` results and parameters | whole 64-bit value converted | truncated to the type's width; `bool` = low byte != 0 (§2, §4) |
| varargs | n/a | `call_guest_varargs<R>(addr, args...)` applies the default promotions (`float` -> soft `double` in a GPR, narrow ints -> `int`). Every `float` it is given is promoted, so it is for callees whose named parameters are not `float` (the printf family). There is no variadic `native_entry`: C++ has no portable way to define such an override |
| `this` adjustment | not modelled | still not modelled, and now known to be unnecessary for this binary (§6) |

Still unknown, so still rejected: a struct with more than 8 bytes passed *through varargs*, a variadic override, and
a struct of size 4 without an `abi::lone_float` decision. Not probed (no use found): bit-fields in small structs, and
structs over 8 bytes with alignment above 16.
