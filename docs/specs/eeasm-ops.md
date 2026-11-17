# Spec: MMI and VU0 macro-mode operation helpers (`b3asm`)

What this covers: the operation helpers in `include/b3host/eeasm_ops.h` (included by `include/b3host/eeasm.h`) and
the random unit in the dependency-free `include/b3host/vu_random.h`, which `tools/decomp2host/eeasm.py` emits calls
to. Sources: the EE Core Instruction Set Manual (MMI chapter), the VU Instruction Manual (macro instructions),
`tests/eeasm_cases.txt`, and `docs/specs/ps2-float.md` (the float operations underneath).

## 0. Scope

Specified here:
- the MMI lane helpers: `pcpyld pcpyud pextlw pextuw pextlh pextuh pextlb ppach ppacb ppacw pmaxw pminw pabsw pcpyh
  paddw psubw paddh pand por pxor pnor psraw psrlw psllw qfsrv` (and `pmulth`, from the manual);
- the VU0 macro helpers `vmax vmini vabs vmove vmr32 ftoi_bits / vftoi`, the outer-product lane rule (`vopmula`,
  `vopmsub`, which forward to `b3vu::opmula / opmsub`), the random unit `r_init / r_next`, the VI register access
  `cfc2_vi / ctc2_vi`;
- the random-unit step used by `src/b3replay/coronas.cpp` `boost_random` (renderer), through `vu_random.h`.

Around it (not specified here): `State`, `state()`, the operand binders (`gpr_in`, `gpr_out`, `vf_in`, `vf_out`,
`fpr_in`, `fpr_out`, `q64`, `lo64`, `hi64`, `sx32`, `set64`, `set32`), memory (`mem`, `host_base`, `ld`, `st`,
`align16`, `ldq`, `stq`), `vf0`, `bc`, `splat`, `fill`, `vadd vsub vmul vmadd vmsub` (thin forwards to `b3vu`),
`itof_bits / vitof`, `vdiv vsqrt vrsqrt` (forwards to `b3vu::q_*`, whose arithmetic is ps2-float.md's), the `vcallms`
/ `wait_cpcond0` hooks, the COP1 helpers (`add_s` ... `cvt_w_s`, forwards to `b3fp`).

## 1. Interface (must not change)

Types: `using Q = u_long128` — four 32-bit words `w[0..3]`, word 0 least significant (`b3host::make_u128(w0, w1, w2,
w3)`). Halfword k of a Q is bits 16k..16k+15 of the 128-bit value (k = 0..7); byte k likewise (k = 0..15). "rs", "rt"
are the instruction's source registers, "rd" the result.

| Function | Signature |
|---|---|
| 2-operand MMI (`pcpyld` ... `pnor`, the 19 of `eeasm.py` MMI3) | `Q name(const Q& rs, const Q& rt)` |
| `pabsw`, `pcpyh` | `Q name(const Q& rt)` |
| `psraw`, `psrlw`, `psllw` | `Q name(const Q& rt, unsigned sa)` |
| `qfsrv` | `Q qfsrv(const Q& rs, const Q& rt, std::uint8_t sa_bytes)` |
| `pmulth` | `Q pmulth(State&, const Q& rs, const Q& rt)` (writes `lo hi lo1 hi1`) |
| `vmax`, `vmini` | `template <unsigned M> Q name(const Q& d, const Q& s, const Q& t)` |
| `vabs`, `vmove`, `vmr32` | `template <unsigned M> Q name(const Q& d, const Q& s)` |
| `ftoi_bits` | `std::uint32_t ftoi_bits(std::uint32_t bits, unsigned frac)` |
| `vftoi` | `template <unsigned M, unsigned N> Q vftoi(const Q& d, const Q& s)` |
| `vopmula` | `Q vopmula(const Q& acc, const Q& s, const Q& t)` |
| `vopmsub` | `Q vopmsub(const Q& d, const Q& acc, const Q& s, const Q& t)` |
| `r_init`, `r_next` | `std::uint32_t r_init(std::uint32_t src)`, `std::uint32_t r_next(std::uint32_t r)` |
| `cfc2_vi`, `ctc2_vi` | `std::uint32_t cfc2_vi(const State&, unsigned k)`, `void ctc2_vi(State&, unsigned k, std::uint32_t)` |

Masked VU ops: `M` is the port's lane mask (bit 0 = x = word 0 ... bit 3 = w = word 3; note the VU's own DEST field
orders them the other way). Lanes outside M keep the destination's previous value. Each masked VU helper must keep
the `B3VU_ENTER(kind, M, d, &s, &t)` / `B3VU_LEAVE(r)` bracket of `b3host/vubind.h` (the binding to retail's VU
registers in native builds): the result is computed on top of the base value `b3vu_base_` that `B3VU_ENTER`
defines (the bound register's previous value, or `d` when unbound), and `B3VU_LEAVE` is given the final Q. Kinds:
`vubind::kMax kMin kAbs kMove kMr32 kFtoi`. All helpers `B3VU_INLINE` / `inline`.

## 2. MMI semantics [EE Core Instruction Set Manual, MMI instructions]

Copies and interleaves (no arithmetic):
- PCPYLD: rd low doubleword = rt low doubleword; rd high doubleword = rs low doubleword.
- PCPYUD: rd low doubleword = rs high doubleword; rd high doubleword = rt high doubleword.
- PEXTLW: rd words = (rt.w0, rs.w0, rt.w1, rs.w1). PEXTUW: (rt.w2, rs.w2, rt.w3, rs.w3).
- PEXTLH: for i = 0..3, rd halfword 2i = rt halfword i, rd halfword 2i+1 = rs halfword i.
- PEXTUH: the same from halfwords 4..7 of rt and rs.
- PEXTLB: for i = 0..7, rd byte 2i = rt byte i, rd byte 2i+1 = rs byte i.
- PPACH: rd halfwords 0..3 = rt halfwords 0, 2, 4, 6; rd halfwords 4..7 = rs halfwords 0, 2, 4, 6.
- PPACB: rd bytes 0..7 = rt bytes 0, 2, ..., 14; rd bytes 8..15 = rs bytes 0, 2, ..., 14.
- PPACW: rd words = (rt.w0, rt.w2, rs.w0, rs.w2).
- PCPYH: rd halfwords 0..3 = rt halfword 0; rd halfwords 4..7 = rt halfword 4.

Arithmetic and logic, lane-wise, no saturation unless stated:
- PADDW / PSUBW: 32-bit lanes, modulo 2^32. PADDH: 16-bit lanes, modulo 2^16 (PADDSH is the saturating form; not
  used).
- PMAXW / PMINW: signed 32-bit compare per lane; equal lanes give that value.
- PABSW: signed 32-bit absolute value per lane, with 0x80000000 → 0x7FFFFFFF (saturates) [manual: PABSW].
- PAND / POR / PXOR / PNOR: bitwise per 128 bits (PNOR = NOT (rs OR rt)).
- PSRAW / PSRLW / PSLLW: each 32-bit lane shifted by `sa & 31` (arithmetic right, logical right, left).
- QFSRV: the 256-bit value (rs:rt) — rs in the upper half — shifted right by 8 · (sa_bytes & 15) bits, the low 128
  bits kept. The model's SA register holds a **byte** count (eeasm.py's MTSAB stores `(rs & 15) XOR (imm & 15)`,
  MTSAH `((rs & 7) XOR (imm & 7)) · 2`).
- PMULTH: the eight signed 16 × 16 → 32 products p_k = rs.h_k · rt.h_k (k = 0..7). rd words = (p0, p2, p4, p6);
  LO = p0 | p1 << 32, HI = p2 | p3 << 32, LO1 = p4 | p5 << 32, HI1 = p6 | p7 << 32 (as 64-bit words in `State`).

Upper-half rule (unchanged, own code): only MMI results write bits 127..64 of a GPR; 32-bit results sign-extend into
the low doubleword and keep the upper one (`set32`).

## 3. VU0 macro helpers [VU Instruction Manual, macro mode]

- VMAX / VMINI: per masked lane, `b3fp::ref::max / min` of the raw encodings (ps2-float.md §4.6), in **both** float
  build modes (no arithmetic is involved).
- VABS: masked lanes get the source with bit 31 cleared.
- VMOVE: masked lanes copied.
- VMR32: rotate by one lane: result x = s.y, y = s.z, z = s.w, w = s.x; then masked.
- VFTOIn (n = 0, 4, 12, 15; `ftoi_bits(bits, n)`): the value x · 2^n truncated toward zero to a signed 32-bit integer;
  if |x| · 2^n ≥ 2^31 — equivalently exponent field + n ≥ 158 — or the exponent field is 255 (whatever the fraction),
  the result saturates by x's sign bit: 0x7FFFFFFF when clear, 0x80000000 when set. An exponent-0 encoding gives 0.
  The scale by 2^n is exact (a power of two), so no rounding question arises below the saturation limit.
- VOPMULA: ACC.x = s.y·t.z, ACC.y = s.z·t.x, ACC.z = s.x·t.y (each `b3vu` FMAC multiply, i.e. ps2-float.md's mul in
  faithful mode), ACC.w unchanged. VOPMSUB: d.x = ACC.x − s.y·t.z, d.y = ACC.y − s.z·t.x, d.z = ACC.z − s.x·t.y
  (FMAC msub: product chopped first), d.w unchanged [VU manual OPMULA / OPMSUB]. Together they form s × t (cross
  product) with the w lane kept.
- CFC2 / CTC2 of VI1..VI15: a read returns the 16-bit VI register zero-extended; VI0 reads 0; a write stores the low
  16 bits; a write to VI0 is ignored.

## 4. The random unit (R register) — `vu_random.h`

R is a 23-bit linear-feedback shift register shown as a float in [1, 2): the register word is always
0x3F800000 OR the 23-bit state.
- VRINIT (`r_init(src)`): state = the low 23 bits of src. VRXOR is translated by eeasm.py as r_init(src XOR R).
- VRNEXT (`r_next(r)`): with s = the 23-bit state of r, the new state is (s shifted left by one, keeping 23 bits)
  with bit 0 = bit 4 of s XOR bit 22 of s. VRGET reads R unchanged.
- Behavioural reference: PCSX2's VU random unit (its `AdvanceLFSR`) implements the same register; no Sony document
  consulted for this audit states the feedback taps [L]. The golden runs depend on exactly this step (38 VRNEXT sites in the
  generated game code, and the renderer's boost coronas), so the taps above are required.
- `src/b3replay/coronas.cpp boost_random` must call the shared `r_init / r_next` (no private copy of the step).

## 5. Performance constraints
- All helpers header-only and inline (VU ones `B3VU_INLINE`, i.e. always_inline in native builds); no tables, no
  allocation, no environment access. Per-lane loops over compile-time masks (the compiler must be able to drop
  unmasked lanes). MMI helpers take and return Q by value / const reference as today.
- Generated code (`gen/native`) uses `b3asm` about 7,600 times (pcpyld 735, vadd 729, vmul 608, pminw 59, pextlw 53,
  ...): no helper may become out-of-line. Gate: the objdump count of calls to `b3asm::` symbols in `native_play` not
  above the baseline's; unpaced race rate within 2 % (median of 3).

## 6. Acceptance checks

1. `make test`: `test_eeasm` in both float modes (the `eeasm_cases.txt` translations and the edge rows), `eeasm.py
   --selftest`, `test_calls`, `test_game_units`, `test_vu_units`, `test_native_platform`, `test_vu0_core`.
2. New rows for every bullet of §2–§4 with hand-computed expectations: every MMI op on patterned inputs
   (0x00112233 ... so lane mix-ups show), QFSRV for all 16 SA values, the shifts for 0, 1, 31 and 32+, PABSW on
   0x80000000 / 0xFFFFFFFF / 0x7FFFFFFF, VFTOI on 2^31 − 1 boundaries for each n, VMR32 with every mask, the LFSR from
   a known seed for 64 steps (computed by hand / a few lines of independent Python), CFC2 of VI0.

3. Sweeps (a scratch tool against §2–§4): 10^8 random Q pairs per MMI op and every SA / shift amount; all 2^32
   encodings × n ∈ {0, 4, 12, 15} for `ftoi_bits`; all 2^23 LFSR states for `r_next` and all 2^32 sources for
   `r_init`; every mask (16) × 10^6 random inputs for the masked VU helpers, with the binding off. Pass: zero
   differences.
4. The native checks of ps2-float.md §7 (race_check STRICT, every scenario and tour, shots equal to the golden
   recording) and the renderer's coronas: the race's live shots (`SHOTS`) equal to the golden recording's.
