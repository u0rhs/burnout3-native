# Spec: the PS2 float model (`b3fp::ref`)

What this covers: the bit-level operations of namespace `b3fp::ref` in `include/b3host/ps2float_ref.h` (included by
`include/b3host/ps2float.h`): clamp, chop, add, sub, mul, madd, msub, min, max, the VU0 divide unit, the EE FPU
divide / square root / reciprocal square root, the two conversions and the three compares, plus the one-line
`b3fp::neg` / `b3fp::abs`, and the round-to-nearest variants `add_rn` .. `msub_rn` and `near_finish` (§5). Sources:
the public / manual sources cited here, `include/b3host/ps2types.h`, and the callers listed in §1.

## 0. Scope

Around it (not specified here; it calls the operations above): the rounding switch (`Rounding`, `RoundingModel`,
`rounding()`, `set_rounding()`, `rounding_name()`, namespace `sw`); the scalar wrappers `b3fp::add` ... `b3fp::min`
in both build modes; the fast mode (`B3_PS2_FLOAT=0`) including the SSE / NEON vector kernels; namespace `b3vu`;
`include/b3host/ps2float_hw.h` (`b3fp::hw`, `b3fp::vufast`); `include/b3host/fenv.h`; `include/b3host/ps2f.h`;
`include/b3libm/*`; `tools/vuref.py`.

## 1. Interface (must not change)

All functions are `inline` (or `constexpr` where possible), header-only, in namespace `b3fp::ref`, operating on
the raw IEEE binary32 encodings as `std::uint32_t`. Helpers `b3fp::bits(float)`, `b3fp::flt(uint32_t)` and the
constants `kSign 0x80000000`, `kExp 0x7F800000`, `kFmax 0x7F7FFFFF` are defined before `ref` and may be used.

| Name | Signature | Used by |
|---|---|---|
| clamp | `uint32_t clamp(uint32_t)` (constexpr) | ps2float_hw.h, vu0_aot_rt.h, b3libm, vu0_unit.cpp |
| chop | `uint32_t chop(double)` | ps2float_hw.h (vufast), vu0_aot_rt.h, eeasm.h (via cvt) |
| add, sub, mul | `uint32_t op(uint32_t a, uint32_t b)` | everything: every `ps2f` operator of generated code (through `sw`), the VU0 AOT code (gen/native/vu0, ≈1,700 uses through `b3fp::vufast`, which falls back to `ref` for far-apart add / sub, for the divide unit and with `B3_VU0_FLOAT=ref`), b3libm, the renderer |
| madd, msub | `uint32_t op(uint32_t acc, uint32_t a, uint32_t b)` | VU0 macro / micro code, b3libm, renderer |
| min, max | `uint32_t op(uint32_t, uint32_t)` (constexpr) | eeasm.h VMAX / VMINI / max.s / min.s, b3vu |
| vu_div | `uint32_t vu_div(uint32_t n, uint32_t d)` | VDIV, VU0 micro DIV, b3vu::q_div |
| vu_sqrt | `uint32_t vu_sqrt(uint32_t x)` | VSQRT, micro SQRT |
| vu_rsqrt | `uint32_t vu_rsqrt(uint32_t n, uint32_t r)` | VRSQRT, micro RSQRT |
| fpu_div, fpu_sqrt, fpu_rsqrt | as above | `b3fp::div / sqrt / rsqrt` (every `ps2f` division in generated code) |
| cvt_s_w | `uint32_t cvt_s_w(int32_t)` | `ps2f(int)`, eeasm itof, b3libm |
| cvt_w_s | `int32_t cvt_w_s(uint32_t)` | `(int)ps2f`, eeasm cvt.w.s |
| lt, le, eq | `bool op(uint32_t, uint32_t)` | ps2f comparisons |

Outside `ref`: `float b3fp::neg(float)` (flip bit 31) and `float b3fp::abs(float)` (clear bit 31), both modes.

The build flag `B3_PS2_FPU_DIVSQRT_CHOP` (default 0) must keep selecting the alternative in §4.4.

## 2. Notation

- An *encoding* is a 32-bit word: sign s (bit 31), biased exponent E (bits 30..23), fraction f (bits 22..0).
- `val(x)` for an encoding with 1 ≤ E ≤ 254 is (−1)^s · (1 + f/2^23) · 2^(E−127) — an ordinary binary32 value.
- `Fmax` = 0x7F7FFFFF (the largest finite binary32, (2 − 2^−23) · 2^127); `−Fmax` = 0xFF7FFFFF.
- `RN64(·)`: IEEE 754 binary64 arithmetic, round to nearest, ties to even (the host's `double` in its default
  environment). `RN32(·)`: the same in binary32 (the host's `float` operations in the default environment, gradual
  underflow on, Inf possible).

## 3. The hardware facts the model rests on (public / manual sources)

- [VU User's Manual v6.0 §2.1.1 p26] Exponent 0 is a zero whatever the fraction (no denormals); exponent 255 is an
  ordinary number, not Inf / NaN.
- [VU manual §2.2 p27] A 24-bit calculation (hidden bit included) whose result is truncated; conversions truncate
  toward zero.
- [EE Core User's Manual v6.0 §8.7 p164] The FPU rounds toward zero only and has no guard / round / sticky bits, so
  results may differ from IEEE round-to-zero in the last bit.
- [VU manual §2.3 p28, EE Core manual §8.5 p162] Exceptional results clamp: x/0 and 0/0 give ±MAX, exponent overflow
  gives ±MAX, underflow gives ±0, sqrt of a negative operand is taken of its magnitude.
- [EE Core manual table 8-5 p163] Signed zeros: (+0)+(−0) = +0, (−0)+(−0) = −0, (−0)−(+0) = −0; Min(−0,+0) = −0,
  Max(−0,+0) = +0.
- PCSX2's documentation of its "clamping modes" (the FPU / VU clamp settings: none, normal, extra, extra + preserve
  sign) describes the same idea of mapping exponent-255 operands to ±Fmax before arithmetic.

## 4. Required behaviour (bit exact)

The model is **not** the hardware in four places (D1–D4 below). The port's golden runs were recorded with this
model, so it is the behaviour to keep; each deviation is marked.

### 4.1 Operand normalisation: `clamp(x)`
- E = 0 → the signed zero with x's sign (fraction discarded).
- E = 255 → the signed Fmax with x's sign (fraction discarded). **D1**: the manual would treat it as a number
  ×2^128.
- otherwise x unchanged.
- No other operation in §4 sees an exponent-255 or exponent-0-nonzero operand except through `clamp`, unless stated.

### 4.2 Narrowing toward zero: `chop(r)` for a binary64 value r
Returns the binary32 encoding obtained by IEEE round-toward-zero of r, with flush-to-zero of tiny results:
- r > FLT_MAX → +Fmax; r < −FLT_MAX → −Fmax (IEEE RZ overflow behaviour);
- otherwise the binary32 value of largest magnitude not exceeding |r|, with r's sign;
- if that value has exponent field 0 (|value| < 2^−126, including exact zero), the result is the zero carrying
  r's sign bit (so r = −0.0 gives 0x80000000, r = +0.0 gives 0).
- Must not depend on the host's current rounding mode **as long as it is the default**; `chop` is only called in
  the default environment (ps2float_hw.h wraps it in `DefaultFloatEnv` where needed). A rounding-mode-independent
  implementation is welcome but not required.

### 4.3 FMAC operations (VU0 macro / micro and the FPU add / sub / mul / madd / msub)
Let A = val(clamp(a)), B = val(clamp(b)) (zeros as signed zeros).
- `add(a, b)` = chop(RN64(A + B)); `sub(a, b)` = chop(RN64(A − B)). **D3**: the binary64 sum is exact when the two
  exponents differ by at most 28 (24 + 28 + 1 bits fit in 53), so the result is then the RZ of the exact sum; when
  they differ by more, the binary64 rounding happens first and can land on the other side of a binary32 boundary
  (ties-to-even back to the larger operand; a power-of-two larger operand). This two-step result is the required one
  (the hardware, with no guard bits, may differ again; not modelled). Zero results: +0 except (−0)+(−0) and
  (−0)−(+0) and the like, i.e. exactly IEEE RN64's sign of zero, then chop keeps it.
- `mul(a, b)` = chop(exact A · B). The exact product of two 24-bit significands fits binary64, so chop(RN64(A·B))
  is the same value. A zero operand gives the zero whose sign is the XOR of the two signs. Products ≥ 2^128 give
  ±Fmax; products < 2^−126 in magnitude give the signed zero.
- `madd(c, a, b)` = add(c, mul(a, b)); `msub(c, a, b)` = sub(c, mul(a, b)). Not fused: the product is chopped to
  binary32 first [VU manual: MADD is MUL then ADD on the FMAC; the test row "madd −1 + (1+2^−12)^2 = 0x3A000000"].

### 4.4 Division and square roots
VU0 FDIV unit (Q register):
- `vu_div(n, d)`: if clamp(d) is a zero → (sign(n) XOR sign(d)) | Fmax, also for 0/0 [VU manual §2.3; the STATUS
  flags that tell them apart are not modelled]. Otherwise chop(RN64(N / D)) with N, D the clamped values (two
  roundings: binary64 nearest, then toward zero).
- `vu_sqrt(x)`: chop(RN64(sqrt(|X|))), X = val(clamp(x)); a zero gives +0 (the sign of a negative operand is dropped
  [VU manual §2.3]).
- `vu_rsqrt(n, r)`: if clamp(r) is a zero → the result's sign is sign(n) XOR sign(r); its magnitude is Fmax when
  clamp(n) is nonzero and 0 when it is zero (0/sqrt(0) = ±0, unlike vu_div's 0/0). Otherwise
  vu_div(n, vu_sqrt(r)): the square root chopped to binary32 first, then the division chopped (two truncations).

EE FPU (COP1), default build (`B3_PS2_FPU_DIVSQRT_CHOP=0`). **D4**: rounded to nearest in binary32, not truncated as
the manual says:
- `fpu_div(n, d)`: if d's *raw* exponent field is 0 (a zero or a denormal encoding) → (sign(n) XOR sign(d)) | Fmax.
  Otherwise q = RN32(N / D) with N, D clamped; result = clamp(q) (an overflow to Inf becomes ±Fmax, a denormal
  quotient becomes the signed zero). **D2**: the manual's table lists 0x7FFFFFFF for x/0; the model gives Fmax.
- `fpu_sqrt(x)`: raw exponent 0 → the zero with x's sign (sqrt(−0) = −0, also for a negative denormal encoding).
  Otherwise RN32(sqrt(|X|)), X clamped.
- `fpu_rsqrt(n, r)` (= n / sqrt(r)): raw exponent of r 0 → sign(r) | Fmax (the sign comes from r alone; n is not
  looked at, even when it is zero). Otherwise t = RN32(sqrt(|R|)), q = RN32(N / t), result clamp(q).

With `B3_PS2_FPU_DIVSQRT_CHOP=1` the three FPU operations keep the same special cases but compute their value in
binary64 and narrow once: div chop(RN64(N/D)); sqrt chop(RN64(sqrt|X|)); rsqrt chop(RN64(N / RN64(sqrt|R|))), each
passed through clamp.

### 4.5 Conversions
- `cvt_s_w(i)` (int → float, cvt.s.w / ITOF): chop of the exact integer value (|i| ≤ 2^31 is exact in binary64), i.e.
  truncation toward zero to 24 significant bits [VU manual §2.2]. Example: 16777219 → 0x4B800001 (host RN would
  give ...002).
- `cvt_w_s(x)` (float → int, cvt.w.s): if x's raw exponent field is ≤ 157 (|x| < 2^31) → the value truncated toward
  zero (a raw exponent 0 gives 0); otherwise (|x| ≥ 2^31, including exponent 255 whatever the fraction)
  INT32_MIN when the sign bit is set, else INT32_MAX [EE Core manual §8.5 "conversion overflow"].

### 4.6 Compares and min / max
- `lt / le / eq(a, b)`: the IEEE comparison of val(clamp(a)) and val(clamp(b)) (no NaN can occur; −0 equals +0; a
  denormal encoding equals zero; an exponent-255 encoding equals ±Fmax).
- `min / max(a, b)`: a total order on the **raw** encodings (no clamp, no arithmetic): every encoding with the sign
  bit set is below every encoding without it; among non-negative encodings the larger 31-bit magnitude field is
  larger; among negative ones the larger magnitude field is smaller. So −0 < +0, a +denormal encoding is above +0,
  0x7FFFFFFF is the largest. `min` returns the lower, `max` the higher; when the encodings are equal either is the
  same word. [EE Core table 8-5 for the zeros; the rest is the model's.]

## 5. The round-to-nearest variants (port15)
Operands clamped as in §4.1. `near_finish(r)`: f = RN32(r); exponent 255 (overflow to Inf) → ±Fmax; exponent 0 → the
signed zero; else f. `add_rn / sub_rn` = near_finish(RN64(A ± B)); `mul_rn`: a clamped zero operand → the XOR-signed
zero, else near_finish(exact A·B); `madd_rn / msub_rn` = add_rn / sub_rn(c, mul_rn(a, b)). Selected only through
`set_rounding()` (tests); the shipped default is `Rounding::Chop` for both units.

## 6. Performance constraints
- Header-only, `inline`; no function pointers, no `thread_local`, no environment access, no locks, no allocation.
- The common path of add / sub / mul / madd / msub must have no more data-dependent branches than: the two operand
  clamps and the final narrowing. In particular `mul` must stay at most one multiply (integer 24×24 or binary64) plus
  bit manipulation; `add` one binary64 add plus the narrowing.
- `clamp`, `min`, `max` must stay `constexpr`.
- Must compile cleanly with the project flags (`-ffp-contract=off`, `-frounding-math` on the VU0 units, GCC / clang,
  x86-64 and AArch64, MinGW) and not change what `ps2float_hw.h` relies on (§4.2 default-environment assumption).
- Measured gate (§7): unpaced `native_play` race rate within 2 % of the baseline (median of 3); the number of
  out-of-line call instructions to `b3fp::` symbols in `native_play` (objdump) not higher than the baseline's.

## 7. Acceptance checks

1. `make test` — every binary passes, in particular `test_ps2float` (the manual / Python-model rows), `test_fenv`
   (the host environment against ref), `test_vufloat` (hw against ref, x86 and AArch64 under qemu if available),
   `test_gtmath_ps2`, `test_libm`, `test_libm_more`, `test_libm_cost`, `test_eeasm` (both modes), `test_vu0_core`,
   `test_native_platform`.
2. `python3 -I tools/vuref.py` agrees with every [P] row (exact-rational model; independent of both implementations).
3. Test rows for every bullet of §4 with the expected value computed by hand or by `tools/vuref.py`
   (not by running the code under test).
4. Sweeps (a scratch tool, never committed) against `tools/vuref.py`'s model, in the default FP environment:
   - unary ops (vu_sqrt, fpu_sqrt, cvt_w_s, clamp) over all 2^32 inputs; cvt_s_w over all 2^32 integers;
   - binary ops (add, sub, mul, vu_div, fpu_div, fpu_rsqrt, vu_rsqrt, min, max, lt, le, eq, and the `_rn`
     variants): every pair of exponent fields (256 × 256) × 4 sign combinations × 2,000 random fractions, plus
     fractions {0, 1, 0x7FFFFF, 0x400000} exhaustively per exponent pair;
   - madd / msub: 10^9 random triples, stratified so that |E_c − E_product| covers −40..40;
   - `chop` on 10^9 random binary64 values in [−2^130, 2^130] plus all boundaries (±FLT_MAX, its successors,
     ±2^−126 and neighbours, ±0, denormal doubles).
   Pass: zero differing results.
5. Full native checks against the previous build (the baseline) on the same machine (tools/golden): `STRICT=1`
   race_check for race, pause, photo, music, takedown, timeattack, roadrage, crash, worldtour and every front-end tour
   (multiplayer, partycrash, online, ddrewards, ddrecords, ddsettings, crashnav, carselect, crashresults, nfsdemo),
   every run PASS with its shots byte-equal to the baseline's (photo's free-camera shots excepted, as usual);
   `B3NATIVE_RAM_HASH=1-23400:100` equal to the baseline in race and crash; `save_check.sh` PASS. At most 3
   native_play runs at once.
6. Performance as §6.
7. The VU0 paths that reach `ref` directly: a race with `B3_VU0_FLOAT=ref` (every VU0 operation on `ref`) STRICT PASS
   with shots equal to the baseline's run in the same mode, and a dev build's race with `B3_VU0_AOT=check` (hw against
   ref per launch) reporting no mismatch.
