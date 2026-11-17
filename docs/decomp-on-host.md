# Decomp game code on the host

How Criterion's decompiled game code (burnout3-retail-decomp, `src/b3/` matched units and `src/nonmatching/` drafts)
runs natively, without editing the decomp source.
Tags: **[C]** confirmed (cited), **[L]** likely, **[G]** guess.

## The problem

Decomp source is written for MWCC on the EE and must stay byte-identical there, so it is never edited for the host.
Compiled unchanged on x86-64 it breaks in five ways:

| # | What the decomp does | Why it breaks on the host |
|---|---|---|
| P1 | Raw `T*` struct members (`UnkStruct_Score_el *field_0;`): only 5 decomp files use `B3_PTR(T)` | 8-byte host pointers move every later field: the layout no longer matches guest memory |
| P2 | `extern T D_0064D364;` data at fixed guest addresses | becomes an unresolved host symbol |
| P3 | `float` arithmetic | host SSE rounds to nearest, keeps denormals/Inf/NaN; the EE truncates, flushes, clamps (b3host/ps2float.h D1-D4). Not bit-exact |
| P4 | MWCC's `long` is 64-bit [C decomp N102] | 32 bits on an LLP64 host (Windows) |
| P5 | MWCC-accepted idioms: a `goto` jumping over `T x = e;`, MWCC `#pragma`s, type punning, `[1]` trailing arrays | ill-formed or UB in ISO C++ / GCC |
| P6 | inline EE/VU0 asm (`asm("vsub.xyz ...")`), frequent as stand-ins for missing GtMath inlines | cannot compile for x86 |

## Options considered

| Option | For | Against |
|---|---|---|
| A. Shadow headers only (the GtMathPs2 approach, `test_decomp_unit`) | zero transform | only solves headers: P1-P5 sit in each unit's own declarations, which are per-unit (every unit declares only the fields it touches) |
| B. Map `B3_PTR(T)` to `gp<T>` in the decomp | clean, upstream | 5 of ~3,800 matched files use it; converting the rest is a decomp-wide header round that must keep 4,460 proven units identical. Long-term goal, not now |
| C. 32-bit host build (`-m32`) | pointers 4 bytes for free | guest RAM is not at host address 0 anyway, so pointer *values* would still be wrong |
| D. clang `__ptr32` | 4-byte pointers | zero-extends to host address = guest address (same problem as C); clang is not installed |
| E. **Source-to-source transform, generated from the unchanged decomp** | handles P1-P5 mechanically per unit; output is reproducible and committed as a snapshot; fails loudly on anything it does not understand | a parser for the decomp's regular style, not full C++; P6 needs a translator (open) |

**Chosen: E, with A for the shared headers.** `tools/decomp2host/decomp2host.py` generates host C++ from the decomp
(read-only), and the port's GtMathPs2 shadow headers (`include/gtmath/b3`, PS2-faithful VU0 model) serve every
`#include "b3/GtMath*.h"`. Generated files go to `gen/decomp` (committed, so a checkout builds without the decomp; regenerate, never hand-edit), rebuilt by `make`.

## The transform (decomp2host.py)

Selection: `config/game_overrides.txt` (`<address> matched|nonmatching`). Name and size come from the decomp's
`assembly/asm` `nonmatching <name>, <size>` lines, the source from `config/proven_units.txt` (matched) or
`src/nonmatching/<name>.cpp`, the FNV-1a 64 key from `orig/SLUS_210.50`.

| Step | Rewrite | Why it is safe |
|---|---|---|
| T1 | `float` -> `ps2f` (`include/b3host/ps2f.h`) | 4-byte trivially copyable struct: layouts hold. Every operator is one COP1 instruction through `b3fp` (the PS2 float model of docs/specs/ps2-float.md in faithful mode, host float in fast mode). Conversions to host `float` are bit copies, so the GtMath shadow headers take ps2f unchanged. int->float is cvt.s.w; unsigned->float follows MWCC's two-path code [C CalcFrequency @ 0x389588] |
| T2 | `long`/`unsigned long` -> `b3host::ps2_long`/`ps2_ulong` | MWCC long is 64-bit [C N102] |
| T3 | pointer members in struct/class/union bodies -> `b3host::gp<T>` (`T**` -> `gp<gp<T>>`), function-pointer members -> `b3host::gaddr` | 4 bytes, holds the guest address; `->`, `[]`, arithmetic, conversion to `T*` as a C pointer. Locals and parameters stay host pointers into the guest RAM window |
| T3b | outside aggregates, `T **` -> `b3host::gp<T> *` | in decomp code a pointer-to-pointer points at a pointer stored in guest memory (a member or array element); one pointing at a host local would fail to compile, not miscompile |
| T4 | `extern T D_XXXXXXXX;` -> `#define D_XXXXXXXX (b3host::guest_global<T>(0xXXXXXXXXu))` | the address is in the name; named globals come from the decomp's `config/symbol_addrs_b3names.txt`. A data *definition* in a unit makes the script fail (it would be host data) |
| T5 | out-of-line definitions other than the selected ones dropped (by the decomp's `// 0xADDR` marker, else the extern "C" name or Class::Name); inline helpers kept only if used | the rest of a merged TU may need things (asm, callees) the selected function does not |
| T6 | `#pragma` -> comment | MWCC optimizer pragmas mean nothing to GCC |
| T7 | the unit's own declarations wrapped in `namespace b3gen_<unit>` | two units' partial views of one class (each declares only its own fields) never meet (ODR). Skipped when a unit defines members of a class from a shared decomp header (consistent by construction) |
| T8 | in a function with a `goto`: `T x = e;` -> `T x; x = e;` | MWCC accepts jumping over an initialization; ISO C++ does not. Identical for trivial types |
| T9 | C-style casts to integer types `(int)x` -> `b3host::icast<int>(x)`, to pointer types `(T *)x` -> `b3host::pcast<T>(x)` (b3host/gp.h); the operand's C++ type picks the meaning at compile time | on the EE a pointer is its 32-bit guest address: pointer -> int gives the guest address (`guest_address`, or `gp::raw`), int -> pointer the object at that guest address; every other combination is the plain C cast (port6; closes port5 mismatch 3) |
| T10 | a unit's own `typedef unsigned int u_long128 __attribute__((mode(TI)));` -> dropped | the prelude's u_long128 (b3host/ps2types.h) is the host type |
| T11 | `volatile` on `ps2f` and on punning locals (`volatile union {...} s;`) -> dropped | a matching device for MWCC's scheduling; host locals need no volatile, and a volatile ps2f has no operators |
| T12 | a static data member of a class the unit declares (`static CGtV2d mSunPos;`) -> guest data at its mangled name's address (`mSunPos__14CB3SunRenderer` in symbol_addrs_b3names.txt); every use becomes `guest_global<T>(addr)` | static members live in retail's data; a member with no address is left (undefined reference at link time: loud) |
| T19 | (port14) inside a pointer cast, an element address `(T *)&X[i]` -> `b3host::pcast_elem<T>(X, i)` (b3host/gp.h): for a gp member, guest-address arithmetic (wraps like the EE's addu, no range check); anything else keeps `pcast<T>(&X[i])` | retail computes such an address without reading memory (Fixup__16CB3TriggerStructFUi 0x269D20 relocates members with `lw; addu; sw`); through `gp<T>::operator[]` it was a checked dereference, fatal when the relocated value lies outside RAM. 58 sites in 26 port7 units |
| T20 | (native2) a declaration with an initialiser at a switch body's own level (`case 4: int t0 = field_984;`) -> `T t0; t0 = ...;` (a leading `const` dropped) | MWCC accepts a jump to a later case label past it; GCC refuses even with -fpermissive (41 functions) |
| T21 | (native2) `B3_PTR(T)` (the decomp's include/b3/B3Ptr.h: `T*` for MWCC, `B3GuestPtr<T>` on a host) -> `b3host::gp<T>`; the prelude defines B3Ptr.h's `B3GuestToHost` / `B3HostToGuest` | the 2d headers (Gt2dObject.h, GtFreeList.h, Gt2dFont.h) use it; casts of `B3GuestPtr` failed in ~40 functions |
| T22 | (native2) C library names the decomp declares with signatures of its own (`void strcasecmp(int, int)`, `void strncat(char *, void *, int)`, `rand`, `sprintf`, ...) -> `b3guest_<name>` everywhere; the call stub resolves the prefix to retail's function | they clashed with the host's declarations (34 functions); the calls were guest calls already where they compiled |
| T23 | (native2) a decomp header's `#if defined(__MWERKS__)` block without #else (CV3dRandomiser.h's VU0 inlines) is kept: its asm is translated (P6) | 7 functions called those inlines |
| T24 | (native2) inside a decomp header, calls to a C-linkage game function the header itself declares (GtFreeList.h's inline members) -> `b3host::guest_fn<decltype(Name), addr>::call` | a unit's `#define Name (guest_fn...)` comes after the header, so the inline body kept the plain symbol, undefined at link once 1,988 functions were generated together |
| P6 | inline asm -> C++ on b3host/eeasm.h (`tools/decomp2host/eeasm.py`, below) | parsed per statement at generation time; unknown forms stop the generation |
| flags | `-fpermissive -fno-strict-aliasing -ffp-contract=off -fno-fast-math`, no LTO across units | type punning (`*(u_long128*)&v`), no host FMA (the EE never fuses) |

Not handled (the script stops with the location): asm the translator refuses (VU0 microprograms, branches/labels in
asm, COP0, `.word`, macro-built templates), static pointer data members, top-level data definitions, pointer members
it cannot parse (port6: any `(*name)` member that is neither a function pointer nor a pointer to an array).

Marker rule (port6): the decomp's `// 0xADDR` marker sometimes sits above an inline helper placed before the function
(B3VehicleDeform_003E18E0.cpp). A marked definition must carry the selected function's name; otherwise the marker moves
to the one unmarked definition that does (`fix_markers`). Before the fix the wrapper called the helper (checker:
"guest pointer outside RAM" on every capture).

## P6: inline asm (tools/decomp2host/eeasm.py, b3host/eeasm.h)

The decomp writes VU0 code as GCC-style asm stand-ins. Each statement is parsed at generation time (template string,
`"=j"/"+j"/"j"/"r"/"f"/"m"/"i"` operands, `%N` and `%Nx` references, `$vfN[f]`, `$ACC`, `$Q`, `$I`, `R`, `$viN`, `$N`,
`$fN`, `off(base)`) and replaced by a block with one statement per instruction:
- operand registers become locals (`b3asm::Q` for "j"/"r", the FPR bits for "f"), loaded from the C operand the way
  MWCC binds it (b3asm::gpr_in: 32-bit values sign-extended, u8/u16 zero-extended; pointers as guest addresses) and
  stored back for outputs (`gpr_out`/`vf_out`/`fpr_out`: the register's low bits). A pointer operand used only as a
  memory base keeps its host pointer (it may point at a host local);
- explicit registers live in `b3asm::state()` (per thread), because the decomp keeps values in `$vf1`, ACC, Q, I from
  one asm statement to the next;
- arithmetic is b3vu/b3fp (PS2-faithful model; fast: host SIMD/float).
  VMAX/VMINI are the total order on encodings in both modes; FTOI/ITOF, the FDIV unit (Q), the random unit, MMI
  (pextlw, pcpyld/ud, ppach/b/w, pmin/maxw, pabsw, psraw..., qfsrv, pmulth) and the COP1 forms are in the same model;
- a pure output ("=j") written under a partial mask has no defined other lanes on the hardware (whatever its register
  held); they are taken from the instruction's first source [G] (the GtMathPs2 shadow headers' convention). Retail's
  values there are stale register contents (the "undefined lanes");
- lq/sq/lqc2/sqc2 mask the guest address's low 4 bits (relative to the window base, not the host pointer).

Coverage (`eeasm.py --census`; results in git history): 211 distinct mnemonics occur in the
decomp's src/ and include/; 194 are supported; 98.6% of the asm statements translate. Refused: vcallms (24 uses),
labels/branches (bgtz, bnez, jr: 17 statements), `.word`/`.include`/`.p2align`, COP0 (di/ei/mfc0/bc0f), and
macro-built templates. Tests: tests/test_eeasm.cpp (model edges, plus tests/eeasm_cases.txt:
decomp-syntax statements translated at build time), tests/test_vu_units.cpp (a generated VU0 function against the
retail instruction words in tests/r5900_mini.h, which now decodes COP2/MMI itself), `eeasm.py --selftest` (refusals).

## Host memory facts found in port6

- A guest null pointer dereferenced *through gp* (`->`, `*`, `[]`) reads guest address 0 like retail (low RAM is
  readable on the EE); `get()` and the conversion to `T*` keep null as nullptr, so null checks behave.
- Wrappers: GtMathPs2 value types by value (CGtV2d in one GPR, the 16-byte ones by the caller's copy) and struct
  results through an explicit hidden-pointer first parameter returning void (the same registers as MWCC's §4); other
  struct results stop the generation. The generated table header includes the port's GtMathPs2.h for those types.

Wrappers: for each selected function the script emits `b3gen_<ADDR>`, whose signature uses only ABI-neutral types
(`b3host::gp<void>` for `this`/pointers/references/structs by value, `float`, integers, enums as int), so the table
header needs no decomp class. The body casts and calls the decomp function: `self.cast<CB3Score>()->Get...()`.
`b3host::native_entry` reads it in MWCC's convention (docs/abi-mwcc-ee.md: a struct by value > 8 bytes arrives as the
address of the caller's copy [C IntersectSphere @ 0x2BF2E4 `lq a2, 0(a0)`]; references are addresses). The entries go
into `B3PORT_GAME_OVERRIDES(X)` and from there into the native function table.

## Calls to other game functions (port7)

Generated code may now call any game function. Mechanism, from the call site outwards:

1. **Call stubs (decomp2host, generation time).** Each unit is compiled once for the host (Linux g++, `-O0`); every
   undefined symbol that is a game function gets a definition appended to the unit, so the decomp source stays
   unchanged:
   - C++ members and free functions (GCC-demangled names): `inline auto Q::Name(T0 p0, ...) const ->
     decltype(Name(p0, ...)) { return b3host::call_guest<decltype(Name(p0, ...))>(addr, b3host::ptr_arg(this),
     b3host::stub_arg<decltype(p0)>(p0), ...); }`. The trailing `decltype` gives the declared return type and picks the
     overload; `this` is left out for static members (declared `static` in the class body). The address comes from the
     MWCC mangling of the same signature (`mw_mangle`: CodeWarrior's scheme, e.g. `GetFwdFrac__9CB3AILaneCFUsQ29GtMathPs26CGtV3d`;
     u_long128 mangles as `1` [C `__ct__Q29GtMathPs26CGtV4dF1`, and probe3's `t_q10__Fiiiiiiiii1i`]), looked up in the
     decomp's function table, else by `Name__<class>` when unique;
   - `extern "C"` names (most decomp callees: `extern "C" void HandleNonFatalCollision__8CB3ScoreFP17CB3TrafficVehicle(
     CB3Score *, CB3TrafficVehicle *);`): `#define Name (b3host::guest_fn<decltype(Name), addr>::call)` after the last
     declaration, so every use calls through with the declared parameter types (`guest_fn` also covers C varargs);
   - de-virtualised methods (T13 below): `b3host::vcall_this<R>(this, offsetof(Class, b3_vptr), slot, args...)`, the
     target read from the guest vtable at run time;
   - a symbol that another selected unit defines (a member of a class from a shared decomp header) gets no stub and
     links to that native definition directly (an inline stub beside a strong definition is a multiple definition on
     PE/COFF). Such a direct call bypasses the switch in 2.
   A game symbol with no address, data, a destructor or an operator stops the generation for that unit (loud).
2. **Dispatch (`b3host::detail::dispatch`, call.h).** `call_guest`, `gfn` (function-pointer members, now callable) and
   `vcall` first look the target address up in the process's `b3host::NativeOverride` table (host.h; filled from the
   X-lists). An enabled native override runs directly with the register/stack image call.h built (the same ABI path
   both ways), its static cost is reported, and its `nested_calls` counter counts it. Otherwise `Backend::call` is
   reached.
3. **Original callees (`Backend::call`).** The native backend has none: the call is fatal (`MissingNative`, with the
   address and the call chain). tests/mini_backend.h runs the original on tests/r5900_mini.h, from the function with
   ra = 0x01FFFFF0 until it returns there with sp restored.

**Arguments (call.h).** `stub_arg<P>` maps each declared parameter: references and pointers through `ptr_arg`, the rest
by value (structs > 8 bytes as a caller copy on the guest stack, <= 8 in one GPR, ps2f as a float). `ptr_arg`: a
pointer into a guest window is its guest address; a pointer to host memory (a native caller's local: `&v`, a `const
T&` bound to a local, the decomp's `const void *pp = &local;` mirroring MWCC's by-copy convention) is passed as a copy
in a guest stack frame of max(sizeof(T), 256) bytes, and after the call exactly the bytes the callee changed are
copied back (non-const T) [L: exact unless the callee keeps the pointer or touches more than the window]. Wrappers (guest -> native) now also take structs <= 8 bytes by value (`by_value<T>`: the
GPR image; > 8 bytes the copy's address).

**New transforms.** T13: a class's first `virtual` declaration becomes `b3host::gaddr b3_vptr;` (MWCC puts the vptr
where the first virtual is declared [C decomp N119, probe3 `t_vcall`: `lw t9, 12(a0); lw t9, 8(t9)`]), `virtual` and
`= 0` are dropped, slot = the decomp's `vfunc_XX` name or declaration order in a base-less class [L]. T14: function-
pointer members and typedefs (also the decomp's `B3_FNPTR`) -> `b3host::gfn<R(Args)>` (4 bytes, callable;
`UnkFn_RenderCallback`). T15: the unit's own declarations of host C library functions (memcpy with `unsigned int`) are
dropped. T16: `extern "C" float sqrtf(float)` -> an inline sqrt.s model (MWCC's intrinsic [C retail 0x263D84]; the host
libm binding had another ABI on Windows: fix1's finding); other libm names a unit declares are guest calls (the native
libm overrides when enabled). T17 (port13): a call with two or more arguments that contain guest calls is rewritten at statement level so the arguments with calls are evaluated first, left to right, as MWCC does (retail func_00310200 0x3102C4 / 0x3102D4: jal func_0030CEC0, jal func_0030CE80, then the plain argument loads [C]); x86-64 GCC evaluated them right to left. Each such argument is bound in order (`auto&& __b3_seqN_K = (arg);`) and forwarded unchanged; the same call elsewhere (a condition, an initialiser) stops the generation. One site in the port7 set (func_00310200's inline 1); every other unit is generated byte-identical. T18 (port13): every data member declared with bound 1 (`T field_0[1];`, the decomp's form for an array of unknown length, which MWCC indexes as flat memory) becomes `b3host::open_array<T>` (b3host/gp.h: T[1]'s layout, indexing and decay through a pointer laundered by an empty asm). GCC treats an index past an interior array's bound as undefined (-fstrict-flex-arrays=0 covers only trailing arrays) and acted on it in CAsyncLoadManager::Update (0x13D250): the loop that looks for the next queued request (retail 0x13D3B8-0x13D3F0 reads entry a's field_4C) was compiled without that read, as if a were the entry just cleared, so ours skipped queued file loads [C: host object]. 1,107 members in 347 port7 units. Also: T4 global types get T1/T2 (a `const unsigned long` global was a host 32-bit long
on Windows, a `float` global used host float), T7 keeps a reopened `namespace GtMathPs2` visible, T8 sees consecutive
declarations, `const` and `unsigned short x = e;`, function-like asm macros with literal templates are expanded
(ZERO_V3), unit operator stand-ins that the shadow header also defines are dropped (ambiguity), header-declared static
data members are guest data in units too, and generated units compile with `-fno-aggressive-loop-optimizations
-fstrict-flex-arrays=0` (the decomp's `[1]  // element count unknown` arrays are indexed past 0: GCC otherwise uses
the out-of-bounds UB to end loops early, port7 func_00137F20; fix1 found the same in FillInNoGoData).

## Calls: port8 changes

- **C-linkage callees go through the guest ABI even when another selected unit defines them.** C linkage carries no
  parameter types, and the decomp's units declare one callee differently, which MWCC accepts because both sides only
  meet in registers: `func_00240A60` declares `extern "C" void func_00240E90();` for a definition taking `(self, p0)`
  (retail: a `j` with a0/a1 untouched); `func_0021B4E0` passes an `int` where `func_0028B220` takes a pointer;
  `func_003FE130` passes `void *` where `CGtFSM::Construct` takes `int`. Linked directly, the host call passed nothing,
  or a 32-bit int where a 64-bit host pointer is read, or truncated a host pointer (port7: 29 checker crashes, nested
  callees with a0 = the low half of a host address). Now each side marshals by its own declaration (the `guest_fn`
  macro, as for any other C callee) and `dispatch` still runs the native override. C++ callees keep the direct link
  (the mangled name pins the parameter types).
- **Pass-through argument registers** (`detail::t_incoming`, call.h): a call from native code starts from the argument
  registers the innermost override was entered with, so registers the call does not set keep those values, as MWCC
  leaves them [C func_001AA1A0: retail never sets a0 before `jal Construct__17CB3NetworkMessageFv`; the decomp calls
  it with no argument]. Exact before the override's first call; after a call retail's registers hold whatever the
  callee left (not modelled; they were zero before port8).
- **One window per host object** (`PtrArg`): pointers into one host object passed to one call share one guest copy,
  so they alias in the callee as on the EE [C GetStageTrackID 0x3E9A60 passes `&local`, `&local + 6/7/8` to
  `GtIDCompress__FPCc` (decomp N298; was packName40)]. A window starts `kBounceLead` (64) bytes before its pointer, because GCC builds argument temporaries
  right to left.

## Calls: native2 changes

- **Game functions as values (callbacks).** Decomp code hands game functions to guest code: qsort comparators,
  `CGt2dObject` render callbacks (`mpRenderCallback = &_Render`), FSM tables. On the host such a value is a host function
  pointer, so b3host keeps a registry host address -> guest code address (`b3host::register_code` / `code_address`,
  b3host/call.h). Registered: every free or static call stub (decomp2host emits `static const bool b3reg_... =
  register_code(&Stub, addr)` beside it), `guest_fn<..>::call` of a C name (an inline static member, on first
  instantiation), and every kept free / static definition beside its wrapper. `stub_arg` and `gfn`'s constructor turn a
  host function pointer into its guest address; one that is not registered is fatal with its host address (never a
  guess). 17 functions failed generation on the old static_assert, ~30 in all with the render callbacks.
- **Constructors and struct results.** A selected constructor (`__ct__...`) gets a wrapper that constructs in place in
  guest memory and returns `this` [C abi-mwcc-ee.md §6]. A class whose retail constructor stores a vptr the decomp class
  view does not declare fails the checker (`__ct__12CUnk0028B700Fv`: the vtable word stays 0): constructors are proven
  per function like everything else. Every struct / class result goes through the hidden pointer (§4), not only GtMath
  types; `long long` results are 64-bit like MWCC's long; `UnkEnum_*` and nested `Class::EName` enums are ints.
- **Namespace functions.** `namespace Gt2dRenderer { void SetTexture(CGtTexture *); }` mangles like a static member and
  has no `this`; the decomp also has a `class Gt2dRenderer` view with other statics, so a namespace counts when one of
  its blocks declares the function and no class body of that name does. Static-member detection reads every class body
  of the name on masked text (a T12 comment containing "static" made `CB3Score::Construct` look static).

## boot1 changes (2026-11-05)

- **T4 short names**: splat names low addresses with fewer digits (`D_10000`, `D_1FF0000` in main 0x12EB30); both
  are guest globals at that address. `D_x = D_x;` with `D_x` an array (MWCC accepts it, no code [C func_004D8D50])
  is dropped.
- **C-name `#define` placement**: the guest_fn macro goes after the last *declaration* outside any class / function
  body (`code_blocks`); a call inside a class's inline member had counted, so the macro came after uses that then
  linked the C symbol directly.
- **T25, static initialisers**: a selected function in 0x4D3E00-0x4DD81C (MWCC's `__sinit_*`, the ctor table's
  entries) whose matched unit is only global definitions becomes `extern "C" void func_X()` placement-constructing
  each dynamically initialised global at its guest address in unit order; brace-initialised constant data stays in
  the image (retail's initialiser does not store it). Proven per initialiser by `native_boot --sinit` (158 of 158
  equal retail).
- **T26, constructor vptr**: a constructor of a class with a T13 vptr (own virtuals or a polymorphic base) starts
  with `b3_vptr = <__vt__<len><Class>>` from the decomp's symbol tables, as MWCC stores it; a polymorphic class
  without a `__vt__` symbol stops the generation.
- **PtrArg and code addresses** (call.h): a game function cast to a data pointer is its registered guest code
  address (the array constructor's ctor argument), not a host-memory bounce.

- **Port tracks** (audio1 `rwa`, boot1 `rw`; `PORT_TRACKS` in decomp2host): a selection row `ADDR <track>` generates
  a retail library function written as C in this repo (`src/libs/rw`: RenderWare 3.6 core, `src/libs/rwa`: RW Audio),
  from the file whose line is exactly `// 0xADDR`; headers from `<dir>/include`; units named `<track>_<file>`; guest
  libc functions used as values name retail's. Names, sizes and FNV come from the decomp's asm as for game code. The
  native backend's build selection appends every port-track function (native_select.py `--base`).
- **Static guest data** (boot1): a top-level definition named by its address (`static float D_004E26BC;`, or with a
  literal initializer) becomes `extern` (T4: a guest global; the loaded image holds the .bss zero or the loader's
  value); a `static const` arithmetic scalar with a literal initializer stays a host constant. Anything else keeps the
  refusal. **icast of a game function** (`(T)MallocWrapper`) is its registered guest code address.

## Hardware hooks: b3hw (decided 2026-11-05; built in boot1)

Port-track C (src/libs/rw's sky2 driver first) reaches EE hardware where retail's asm does: GS privileged registers
(0x12000000-), DMA / VIF / GIF / INTC / timer registers (0x10000000-). It calls host hooks with a reserved prefix
instead, declared in `include/b3hw/hw.h`: `b3hw_write(address, width, lo, hi)` / `b3hw_read(address, width)` and the
inline `b3hw_write32/64/128`, `b3hw_read32/64`, in the asm's order with its values.
- **decomp2host** passes `b3hw_*` callees through unchanged in port-track units (`rw_*`, `rwa_*`) and refuses them in
  decomp units; `#include "b3hw/..."` is left as it is (a host header).
- **Backends** (`Backend::hw_write` / `hw_read`, host.h): the native backend hands them to the platform's hardware
  layer (`src/native/platform_hw.cpp`, `install_hw_model`; the state in `src/native/hw_model.h`):
  device state only -- it keeps exactly what the game reads back (registers and their write rules, DMA completion and
  its callback on the virtual clock, PATH3 masking as GIF_STAT state, the VBlank grid); it hands nothing to the
  renderer (which reads guest RAM) and walks no DMA chain; every register it does not model is fatal, naming it.
- **Proof**: `b3host::set_hw_trace` records every b3hw access in order; the mini's `on_mmio` hook (tests/r5900_mini.h)
  sees retail's hardware loads and stores (physical address, width, bytes) instead of failing, so native_check can
  compare the two traces as well as RAM. `ei` / `di` in decomp inline asm (`.word 0x42000038/9`) go to
  `Backend::set_interrupts_enabled` the same way: native only (the platform delivers interrupts), fatal elsewhere.

## gen1 changes (2026-11-06): the functions with decomp C that did not generate

Lane gen1 rescanned the 285 game functions with decomp C outside the native selection (snapshot boot2c, decomp
a96eafa6eb) and fixed the generator gaps that stopped them; each change has a case in tests/test_decomp2host.py (or
tests/test_b3host.cpp for the headers). Counts are functions that failed on that gap at the start.

| Change | Rewrite / rule | Why it is safe |
|---|---|---|
| extern "C" blocks | top_level_functions recurses into `extern "C" { ... }` as into namespaces (27: the ezNetCnf / ezNetCtl family, func_0012F460) | the marker and name lookups never saw definitions inside such a block |
| T3b guard | `a * *p` with `a` a value (a member, a `D_` global, a declared variable or parameter, never a declared type) is left alone (11) | it is a product with a dereference; `lScalingMat * *m` had become `gp<lScalingMat> *m` |
| T32 | a local `T *x` whose address is taken in its scope (`Find(&lane, &seg)`), or a local `T *a[N]` passed whole or by element address, is declared `b3host::gp<T>` (26) | on the EE that local is a 4-byte guest pointer, and the callee's `T **` is `gp<T> *` (T3b); single-declarator statements only, scope-aware |
| T33 | `__declspec(sdata)` dropped before T4 (7) | MWCC's small-data placement; the declaration is an ordinary extern |
| T34 | `&v.marValues[i]` -> `b3host::f32_addr(...)` (b3host/ps2f.h: converts to `ps2f *` and `float *`, constness kept) (6) | the shadow headers keep GtMathPs2 members host `float`, decomp locals are ps2f (T1): one 4-byte IEEE word either way |
| T35 | `T *&` -> `b3host::gp<T> &` (12), compiler-guided: applied by compile_for_stubs only to a unit that fails to bind a `T *&` (a gp member is an rvalue `T*`) and kept only if that error goes away; always on only for `T *const &` in an `extern "C"` prototype | the reference counterpart of T3b (a reference to a pointer stored in guest memory). Always on, it broke 4 units whose inline helpers bind a host local (`RwUInt8 *&cur` in rwa_rwastreamcreate, GtNATDataManager). The prototype rule changed func_0030F620 in the live selection: its callee func_00310180 reads the referenced pointer as a 4-byte guest word, where the bounce window held an 8-byte host pointer |
| T36 | a kept definition's function-pointer parameters -> `b3host::gfn<R(Args)>`, in the definition and the unit's declarations of that name; the wrapper takes the guest code address (4); a by-value parameter of a gfn typedef nested in its class (B3_FNPTR: CGt2dObject::UnkFn_RenderCallback) the same, qualified | a call through it is a guest call (dispatch runs a native override); a host function passed from native code converts to its registered guest address. A namespace-scope gfn typedef keeps by_value (RwaObjInterfaceAttach, unchanged) |
| T8b | the declarations GCC names in `jump to label ... crosses initialization of` notes are split as T8 does (4) | T8's form misses `const int t = ...` / `const T *const p = ...`; compiler-guided, so only units that do not compile change; the fixed text is cached with the symbol lists. Class types (a CGtV2d) stay refused |
| T10b | `__int128` -> u_long128 (3) | MWCC's own quadword type, used for u_long128 globals' elements |
| T11 | `(volatile [const] u_long128 *)` casts lose the volatile (6) | a scheduling device for MWCC; b3asm's operand loads and u_long128's copy take no volatile |
| T4 | arrays of rows (`T D_x[][5]`, `T D_x[4][64]`: the guest array's element is the row) and arrays of const pointers (`const char * const X[]`) (11) | the same guest array, typed as MWCC lays it out |
| T5 | a helper used only from a class body's inline member is kept (5); a dropped game-function definition that kept code calls keeps its declaration, so the call is a call stub (3) | before, the helper / the callee's only declaration disappeared |
| T7 | a forward declaration above the last #include of a struct the unit defines moves into the unit's namespace (1) | a global incomplete `::X` and the namespace's `X` were ambiguous after `using namespace` |
| namespaces | a namespace function the unit itself declares is one unless the unit's own text has a class body declaring it (CB3Globe2dObject::_Render's `B3Im3d::SetBackfaceCull`) | HEADER_TEXT holds every unit's headers in a generation: classes/B3Im3d.h (other units') made it a `this` call in the full build only |
| C linkage | a kept definition whose header (the unit's include closure) declares it `extern "C"` gets that linkage (11) | MWCC keeps the earlier declaration's linkage; inside the T7 namespace it was a second, C++, function |
| names | the decomp's `config/undefined_syms_manual.txt` (`Name = func_X;`, `Name = 0x...;`, PROVIDE) is a third name source (8) | the decomp's own linker aliases: functions C calls by name whose body is still asm, data such as kan8HeadingAwardIconImages |
| wrappers | `unsigned n` is `unsigned int` (5); parameter / registration types declared in the class or a base class are qualified (`CB3EffectsManager::CLighting`, `CGt2dObject::CGt2dObjectSpace`, nested enums) and an elaborated parameter type is not a member type (10); an empty C varargs definition wraps its fixed parameters, one using va_start is refused (2) | the wrapper is at namespace scope |
| stubs | a 4-byte struct a stub passes by value gets its `lone_float` decision when its body is scalar-only (CB3Globe2dObject::_Render's RwRGBA) | call.h's static_assert asked for it |
| call.h | `call_guest_varargs` is declared before `guest_fn`'s variadic form (B3PlayerCameraStatePlayer 0x447000: no ADL with plain arguments); a T18 `open_array` passed to `...` decays to its element's address (cbFormatAuthMessage 0x344E10's snprintf), on a forwarding path used only by such calls (a bit-field still goes by value) | the C semantics of an array argument |
| gp.h | `+=`, `-=`, `--` (guest-address arithmetic, wrapping like addu) and `gp<T>` -> `gp<const T>` (4) | C's pointer arithmetic and qualification conversion; no reinterpreting conversion is added |
| round 2: T5 | an unmarked helper named like a game function (`func_XXXXXXXX`, not static / inline / a member) is dropped like a marked definition (declaration kept), so the call is a stub | the 0x186850 draft carries its own func_00173D30, which B3TrafficVehicle.cpp also defines: a multiple definition in the combined native link |
| round 2: shadow GtMathPs2 | the decomp header's gapN / gapO additions: CGtV3dPlus::GtV3d / Plus / SetPlus(WAxisType) / Zero, Abs(CGtV4d), CGtV3d - WAxisType, TransformVector(CGtMatrix3x3), TransformPoints(CGtMatrix3x4), CGtMatrix3x4::operator*= (9) | each in b3vu with the decomp's instruction (the "=j" partial-write convention: other lanes from the first source); tests/test_gtmath.cpp in both float modes. CGtV2d's default constructor stays user-provided: made trivial, GCC's -O2 code of 10 live units changed |
| round 2: T9 | `(R (**)(Args))p` -> pcast<gfn<R(Args)>> (a vtable-slot read, 1); compiler-guided: a game function cast to a gfn passed where the callee takes a host function pointer -> `reinterpret_cast<R (*)(Args)>(&Name)` (1) | the dereference is the 4-byte guest code address; stub_arg turns the host function into the same registered guest address |
| round 2: T7, compiler-guided | in a unit that fails on its namespace's copy of a header name: a forward declaration of a header-declared class moves above the namespace, a namespace the header also declares gets `using namespace ::X;` and its stubs `::X::f` (7: CGtTexture vs Gt2dRenderer::SetTexture) | compiler-guided because a global forward declaration shares template instantiations (PtrArg<T>, deref_size) with units where T is complete (CGt2dFrame) |
| round 2: T36, gfn | the unit's extern "C" prototypes take gfn too where T36 applies (a received gfn is handed on, 2); an explicit gfn -> gfn conversion (a C cast between callback signatures, 2) | the same guest code address |
| round 2: wrappers | a GtMathPs2 value type the unit declares itself (no GtMath include) is named as the unit has it (1) | `struct CGtSphere { u_long128 mData; };` in func_00301B00 |
| round 2: T8b | GtMathPs2 values with a trivial default constructor (CGtV3d / CGtV4d / lanes) are split, direct-initialised ones too; the several-declarators check counts brackets only (split_top takes the `>` of `->` for one); a crossed struct with only an empty constructor and destructor loses them (1) | a matching device in the decomp (func_00257C70) |
| round 2: header instances | MWCC's out-of-line copies of GtMathPs2 header inlines (CGtV4d / CGtV2d / CGtMatrix2x3 operator=, the CGtMatrix2x3 copy constructor) without a definition in their decomp file generate in one synthetic unit (gtmath_instances) over the shadow header (4) | operator= / the copy constructor return `this`; a const member's struct result goes through the hidden pointer |
| rounds 3-4: guest_fn macro | after the first declaration when a call precedes the last one; later declarations wrapped in #undef / #define | a call before it named the plain C symbol and linked with host ABI (boot2's RwaObjIntGetParamPointer segfault) |
| rounds 3-4: -fpermissive | the host compile shows its diagnostics; pointer <-> integer conversions: hard error in port-track units, made explicit in decomp units (icast, pcast, an icast result unwrapped where a host pointer is needed), else refused | a host pointer had been truncated to 32 bits or an integer used as a host pointer, silently |
| rounds 3-4: T20 | a goto label is no declaration type | `c10_join: *out = f();` had become `c10_join:* out; out = f();` (func_00452ED0) |
| rounds 3-4: members, data, T4 / T12 | aggregates defined in a member's declaration, inline method definitions, function-pointer parameters / `R (**f)(...)` members; continued #define lines; anonymous extern structs; `T const X[]`; `&X` and static members in #define bodies | each a refusal or a compile error before |
| rounds 3-4: compiler-guided | `T *const *` -> gp<T> const *; a unit helper qualified with the unit's namespace from a header class's member; several declarators split when crossed | only units that do not compile otherwise change |
| rounds 3-4: wrappers, markers, gp | static vs member overloads by parameter count; markers with text after the address; gp<T> == gp<U> | |

Left after round 4 (56 of the 285; gen1 rounds 2-4). After round 2 (82): asm the translator refuses (vcallms 13, `.word` 7, bc0f / COP0 3, macro arguments
2: left alone by design), pointer members not understood (10), "result type asm void" (6), top-level data
definitions (3), callees / data without an address (8) and mistyped extern names (6: decomp side), CGtV2d locals crossed by
a goto (2: its constructor stays user-provided), eeasm's `"j"` float input (2, native3's area), and ~22 single cases
(a multi-declarator T8 split, a static member function wrapper, a draft calling another file's helper, static member
arrays of function pointers, CGt2dObject's mgu8RenderBatchData, ...). Decomp-side findings went back to the decomp
(gen1).

## T9 additions (port8)

| Form | Becomes | Found in |
|---|---|---|
| nested casts `(unsigned long)(unsigned int)p` | the inner cast is converted too (a cast's operand may start with a cast although a `)` precedes it) | CSparkBank::Construct 0x264170 stored truncated host pointers into DMA tags |
| `(T * const *)&field`, `(T **)p` | `pcast<gp<T> const>(...)`: a pointer stored in guest memory is a 4-byte guest address | UpdateTransition 0x19F4B0 (host crash), OnScreenKeyboard 0x351410, func_0012F720 |
| `((void (*)(int, int))*fp)(16, 1)` | `gfn<void(int, int)>::from_raw(icast<unsigned int>(*fp))(16, 1)`: a guest function address the game stored as data | OpenViewport 0x227360 (host crash), GtSoundStream, GtLinearMalloc |
| casts inside `#define` bodies | rewritten like code (a body is a text of its own) | func_00285C00's `PLAY(...)` macro, viBufReset's `HW(a)` (now a HostError instead of a host crash: MMIO) |

## Cost

A native frame reports its static cost (code_bytes / 4) after it returns: `detail::dispatch` and the platform's
`charge` do that directly. libm reports its block costs (b3libm, port5 fork, `tests/test_libm_cost.cpp`).

## Results (2026-10-31, port5)

- 14 functions generated (9 matched, 5 non-matching drafts); 13 executed in the race.
- Checker on live captures, tolerance 0: all 8 captured matched functions 192/192; drafts 118/120 (func_00220040's
  two failures are retail's stale-stack w lanes).
- Found by the experiment: a generator bug (static members declared in headers), a backend gap (no scratchpad
  window), a transform gap (int->pointer casts, func_002BD6A0), and a retail undefined-lane store (func_00220040).

Limits: the static-member check matches by name (an overload set mixing static and non-static members of one name
would need the parameter list compared); generated code cannot call guest functions (candidates are leaves); the
undefined lanes of "=j" partial writes cannot match retail's stale registers.

## Results (2026-11-01, port6)

- Inline asm translated (P6), T9-T12, wrapper and marker fixes; `tools/decomp2host/candidates.py --vu0 --compile`:
  104 of 118 leaf decomp functions that use VU0 in retail generate and compile.
- 73 of them added, 87 game functions in all.
- Checker on live captures: 56 of the 72 called new functions pass every capture.
- Found: a ps2f regression (operators dropped), the scratchpad alignment, a marker-association bug, a
  pointer-to-array member, guest null dereference, one-past-the-end casts, and retail's undefined VU lanes; drafts'
  own differences go back to the decomp (port6).
