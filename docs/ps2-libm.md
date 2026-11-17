# PS2 libm: a bit-exact port for PS2-faithful mode

Decision (2026-10-31): port the PS2 libm bit-exactly for faithful mode. Code: `include/b3libm/`. Tests:
`tests/test_libm.cpp`, `tests/test_libm_more.cpp`. Tags: **[C]** shown by the cited evidence, **[L]** likely,
**[G]** guess.

## What retail links

- **newlib's single-precision fdlibm, compiled by gcc 2.x, not a Sony library.** [C] The E map (decomp
  `research/E_B3DEBUG_symbols.csv`) lists `LIBM.A ef_acos.o`, `kf_sin.o`, ... with `gcc2_compiled.` markers. The Sony
  SDK 3.0.3 sources (`~/sonysdk/sdk_303/usr/local/sce/ee/src/lib`) have no libm, so the reference is **the retail asm
  itself** (decomp `assembly/asm/runtime/libm.s`; the E and retail addresses coincide).
- **A no-denormal, no-Inf/NaN build.** The zero tests are `hx < 0x00800000`, and the Inf/NaN early-outs are compiled
  out. That is newlib's `_FLT_NO_DENORMALS` / `_FLT_LARGEST_EXPONENT_IS_NORMAL` configuration [L, from the shapes].
  [C] Examples: `sinf @ 0x11E138` goes straight to `__ieee754_rem_pio2f` for every |x| > pi/4;
  `__ieee754_sqrtf @ 0x11B438` returns x for `hx <= 0x7FFFFF` and rounds by `q += q & 1` with no float probe.
- **Constants differ from newlib's source.** gcc converted the decimal literals itself, and most land one unit below
  newlib's hex comments: `__kernel_sinf`'s S1 is 0xBE2AAAAA in retail and 0xbe2aaaab in newlib, `huge` is
  0x7149F2C9 rather than 0x7149f2ca. [C] Every constant in the port is the bit pattern the retail code loads, cited by
  address. Folded expressions are folded the same way: `acosf(-1)` returns 0x40490FDA (`@ 0x119C5C`).
- The game calls these. Call sites come from decomp `research/port/game_library_calls.tsv`; the wrappers
  `func_0011E3xx` are 5-instruction jumps [C asm]:

| Function | Retail | Calls | Port |
|---|---|---|---|
| sinf | 0x11E138 | 54 | `ps2_trig.h`, bit-exact |
| cosf | 0x11DE98 | 43 | `ps2_trig.h`, bit-exact |
| acosf (func_0011E328 -> `__ieee754_acosf` 0x119C20) | 0x11E328 | 37 | `ps2_trig.h`, bit-exact |
| atan2f (func_0011E358 -> `__ieee754_atan2f` 0x11A3E8) | 0x11E358 | 24 | `ps2_atan.h`, bit-exact |
| powf (func_0011E388 -> `__ieee754_powf` 0x11A8A8) | 0x11E388 | 18 | `ps2_exp.h`, bit-exact |
| fabsf | 0x11DF70 | 18 | `ps2_basic.h`, bit-exact |
| asinf (func_0011E340 -> `__ieee754_asinf` 0x11A048) | 0x11E340 | 17 | `ps2_trig.h`, bit-exact |
| tanf (+ `__kernel_tanf` 0x11D028) | 0x11E220 | 7 | `ps2_tan.h`, bit-exact |
| floorf | 0x11DF98 | 5 | `ps2_basic.h`, bit-exact |
| atanf | 0x11DAF0 | 5 | `ps2_atan.h`, bit-exact |
| ceil (double) | 0x11D2B0 | 3 | `ps2_basic.h`: `std::ceil` [L] |
| ceilf | 0x11DD80 | 2 | `ps2_basic.h`, bit-exact |
| logf (func_0011E370 -> `__ieee754_logf` 0x11A5C8) | 0x11E370 | 1 | `ps2_exp.h`, bit-exact |
| fabs (double) | 0x11D630 | 1 | `ps2_basic.h`, sign clear |
| internal: `__ieee754_rem_pio2f` 0x11B088, `__kernel_sinf` 0x11CF20, `__kernel_cosf` 0x11C568, `__ieee754_sqrtf` 0x11B438, scalbnf 0x11E070, copysignf 0x11DE58, `__kernel_rem_pio2f` 0x11C6B8 | | | `ps2_basic.h`, `ps2_trig.h`, `ps2_rempio2.h` |

## How it is written

`b3libm::ps2::F` (`ps2_core.h`) is a raw binary32 whose C++ operators *are* the COP1 instructions under
`b3fp::ref`, the PS2-faithful model (docs/specs/ps2-float.md; ps2float.h D1-D4): `+ - * /` are add.s, sub.s, mul.s and
div.s, comparisons are c.lt.s, c.le.s and c.eq.s, and `to_int`/`to_float` are cvt.w.s/cvt.s.w. Each function follows
the fdlibm structure, checked against the retail instruction sequence: branches, association order, and where gcc
wrote `2*v` as `v + v` (`acosf @ 0x11A028`; the two are bit-identical in the model anyway).

Modes (`ps2libm.h`): `b3libm::ps2::*` (always available), `b3libm::fast::*` (host libm, with Inf/NaN results clamped to
+-FLT_MAX / +FLT_MAX [G policy]), and `b3libm::sinf(float)`..., which pick `ps2::` when `B3_PS2_FLOAT=1`. The GtMath
shadow headers call `b3libm::` (CreateRotationMatrix2x3, UncompressRotation, Slerp, ACos*/ASin*).

## How it is verified

- **Oracle: the retail code itself.** `tests/r5900_mini.h` is a test-only R5900 subset interpreter: integer ALU,
  64-bit moves, loads/stores, branches including branch-likely, COP1 add/sub/mul/div/abs/neg/mov/cvt/compare. It runs
  the retail functions straight from `orig/SLUS_210.50` (the decomp repo; skipped when absent), with COP1 ops through
  the same `b3fp::ref`. A sweep compares port and retail **bit for bit**. Because the float model is shared, this
  proves the port performs **the same operations in the same order on the same constants** as retail. Any
  unsupported instruction is a hard error, so no instruction is silently skipped. A fault-injection check (S1 + 1 ulp)
  gives 637 mismatches in 21,631 inputs: the comparison has teeth.
- **Sweeps** (2026-10-31, all bit-exact): every exponent 0-255, both signs, 65 mantissas each (33,280 inputs), for each
  function. Dense ranges: sqrtf [0.25, 4) 345,922 inputs; floorf/ceilf [-4, 4] 4 x 119,270; sinf/cosf [0, 0x43490F80]
  both signs 2 x 550,800, plus 2 x 71,519 around k*pi/2 (k < 120) for the 2nd and 3rd reduction steps; acosf/asinf
  [-1, 1] and just past it 2 x 1,391,710.
- **`tests/test_libm_more.cpp`** (atanf, atan2f, `__kernel_tanf`, tanf, logf, powf, `__kernel_rem_pio2f`; all
  bit-exact, 8 tests / 245 checks, ~5 s): all-exponent sweeps; dense ranges over every algorithm split (atan's 7/16,
  11/16, 19/16, 39/16 and +-32 ulp around each; log near 1, the sqrt2 split, the |f| < 2^-20 branch; a 502k powf grid,
  overflow/underflow and the exact z = 129 / -126 boundaries, x < 0 with integer y up to 2^24, |y| > 2^27); a 1.08M
  atan2f exponent-pair grid with special encodings; tanf to exponent 255; `__kernel_rem_pio2f` called directly at prec
  0, 1, 2 (24k each). A one-ulp fault in atanf's aT[0] is caught by five sweeps. Not reached by any sweep:
  `__kernel_rem_pio2f`'s `z >= two8` branch (unreachable from rem_pio2f's inputs [L]) and powf's scalbnf
  below-2^-126 path (scalbnf itself is bit-exact).
- **Retail deviations from newlib's structure** (ported as retail has them, commented in the headers): powf's
  |y| > 2^27 series uses `x - 1`, not `ax - 1` (`0x11AAF8`); `huge*huge` folds to 0x7F7FFFFF (0x4B2BF4) and
  `tiny*tiny` to +0; atan2f's pi_lo is 0x34222168; atanf returns +-(atanhi[3]+atanlo[3]) for every |x| >= 2^34;
  results that IEEE gives as Inf/NaN come out model-dependent (logf(+-0) = 0xFF7FFFFF, logf(x<0) and powf's NaN cases =
  0/0, kernel_tanf(+-0, y, -1) = 1/0). **`__kernel_rem_pio2f` with prec 3 is broken in retail itself** (`init_jk` has 3
  entries; prec 3 reads jk = 0 and touches `iq[-1]`, giving a stale stack word): retail only passes prec 2 (`0x11B3F0`),
  so the port supports prec 0-2.
- **Independent checks (no ELF, no shared model):** retail's sqrtf is round-to-nearest for every normal input (the
  25th bit rounds and no ties occur with a remainder), so it equals host IEEE `sqrtf` (checked on every 37th
  encoding of exponents 1-254). floorf/ceilf equal host floor/ceil on normals. Hand-derived special values: zeros,
  tiny arguments, acos(+-1), the folded constants, `|x| > 1` -> 0/0 -> +MAX, sqrt(2^128) = 2^64 for exponent 255.

## What is proven

- **Proven [C]:** under the `b3fp::ref` model, the port equals the retail instruction sequence on every input tested
  above (and, for the integer-only helpers, independently of the model).
- **Not proven:** that `b3fp::ref` equals the hardware (D1-D4: exponent-255 clamping, MAX value, add/sub without
  guard bits, div.s rounding stay open against the manuals), and the check on captures from live play (needs a
  session in a window).
- **double `ceil`:** ported as `std::ceil` [L]. fdlibm's ceil is an integer algorithm whose only floating operations
  are the inexact probes through soft-float `dpadd`/`dpcmp`. Those were not run against retail (libgcc fp-bit).
- Hardware-faithful sub-mode (decision 2): libm needs no code of its own. It inherits whatever `b3fp::ref` does
  in that sub-mode (D1-D4), because every float operation goes through it.
