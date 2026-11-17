# rw_wip: the sky2 handlers SkySystem reaches (host-stub spec)

What the hardware-level functions behind `SkySystem` (0x001FE1A0, rw_wip/basky.cpp) write, who reads it, and what
they do to the hardware. The native platform's host stubs are written from this: a stub must leave exactly the guest
memory state that non-driver code reads, and must not touch hardware.

Source: retail asm `decomp-snap/native2/assembly/asm/rw/renderware.s` (+ `sce/libgraph.s`, `libdma.s`, `libkernl.s`).
Tags: [C] read from the asm (addresses cited), [L] likely, [G] guess. RW 3.1 names in brackets are a reading aid.

**Reader classification.** Readers come from a scan of every `%gp_rel/%lo/%hi(sym)` reference in the whole retail
asm (`build/rwwip_scratch/xref.py`), each function classified by the RW census layer
(`docs/results/boot1-2026-11-05/rw/rw_functions.tsv`: core / driver) or "game" (outside renderware.s); "fromgame=N"
means game code calls that driver function directly. The scan cannot see accesses through a pointer stored elsewhere;
where a global's address escapes (returned, stored, passed) that is said.

## Summary

| Handler (request) | Non-driver code reads from its writes |
|---|---|
| `SkyOpen` 0x001FC638 (0) | `currentMode` = 0x58 (request 0xA / 6 answers), `currentCameraFormat` (camera raster format via `SkyGetRasterFormat`), `engineStarted` = 0 (gates requests 7 / 0xC and RpSky* calls), `fogTable` = &skyLinearFog (RwRenderStateGet fog type via `SkyGetRenderState`). Fog tables: driver only. |
| `SkyStart` 0x001FD490 (2) | GS register shadows `skyFrame_1`, `skyZbuf_1`, `skyTest_1`, `skyXyoffset_1` (read by ~30 game functions); `_rwDMAFlipData` +0x18/+0x40 (DISPLAY), +0x338/+0x348, saved copies +0x368/+0x390/+0x3A8/+0x3B8 (CGtScreenCentre), +0x3C0..+0x3CC (PresentDrawBuffer writes them); `textureBankStart` (request 0xC, from RW core RwEngineGetTextureMemorySize 0x001E2B00, which no game code calls directly [C xref]); `engineStarted` = 1; `customMode` +0..+0x17 only when currentMode != -1 (request 6 for mode -1). Through its callees: `setInitialRenderState`'s shadows `skyAlpha_1` (= 0x44), `skyTex1_1`, `skyClamp_1`, `skyTextureRaster` (game reads/writes); `_rwDMADmaOpen`'s packet cursor `farFogPlane` 0x004E27C0 (inlined in 93 game functions; the table name is misplaced), `_gOldRasterShowRasterFunc` 0x004E27C4, `_rwDMAPktPtr`; libgraph's DISPLAY formula (sceGsDefDispEnv) behind +0x18/+0x40. Everything else driver only. |
| stop (3): `_rwDMADmaClose` 0x001F4578, then `skyTexCacheCloseCache` (ported, texcache.cpp) | `_rwDMAFlipData` +0x3C0..+0x3CC = 0 (in SkySystem), `engineStarted` = 0. `_rwDMADmaClose`'s own writes are driver only; `_rwDMAUnhook` busy-waits on ISR-cleared bytes 0x004E2830 / 0x004E27CF (a stub must keep them 0). |
| `SkyFinalizeStart` 0x001FC928 (0x11) | ported (basky.cpp). Its callees: `_rwSkyOverloadPipelines` (Im2D slots ourGlobals+0x28..0x34; via initVu1DispatchStuff `xMaxYMax128`, read by CB3CameraData::Construct), the rest driver only (see "Driver callees" section 8). |
| initiate stop (0x12) | all three callees ported (skyblit.cpp, basky.cpp, skyinst.cpp); they only call RwRasterDestroy. |
| close (1), focus (8), initpipeline (9) | nothing: return 1. |

The game also calls the system function directly through the engine's device copy (`ourGlobals + 0x10`, +4 =
SkySystem): `func_00227B80` (a video-mode change) issues 0x12 (initiate stop, 0x00227BE0) and 3 (stop, 0x00227BF8),
then RpSkyGetModeStructurePtr / SetVideoModeParams / RwEngineSetVideoMode(-1), then 2 (start, 0x00227CCC) and 0x11
(finalize start, 0x00227CE4) [C]. So SkyStart and the stop request run again after boot, from game code.

---

## SkyOpen 0x001FC638 (request 0) [RW 3.1 SkyOpen]

**Arguments**: none read [C] (a0 holds the jump-table target at entry; a1..a3 untouched). **Returns** 1 always
[C 0x1FC8E8].

**Hardware**
- [C 0x1FC678..0x1FC684] `mfc0 $v1, Status; Status |= 0x40000000; mtc0` — sets COP0 Status.CU2 (enables COP2 = VU0
  macro mode). Host: nothing (the port's VU0 emulation is always on).
- [C 0x1FC870..0x1FC8AC] `SyncDCache(t, t + 0x15F)` for each of skyLinearFog, skyExpFog, skyExp2Fog,
  skyMungedUserFog (write back the data cache over 0x160 bytes; the tables are 0xE0, the sync runs past them).
  Host: nothing.

**Memory writes** [C]
- Four fog VIF packets, 0xE0 bytes each (symbol sizes): `skyLinearFog` 0x00675F90, `skyExpFog` 0x00676070,
  `skyExp2Fog` 0x00676150, `skyMungedUserFog` 0x00676230. All four get the same three header qwords (sq):
  - +0x00: lo 0x00000000F000000D, hi 0x0500000001000102 (DMAtag END QWC 0xD IRQ 1; VIF STCYCL CL=2 WL=1
    (0x01000102); STMOD 0 (0x05000000)) [C 0x1FC688..0x1FC6E0]
  - +0x10: lo 0x3F3F3F3F20000000, hi 0x0000000071304001 (VIF STMASK + mask 0x3F3F3F3F; UNPACK 0x71 addr 0x4001
    num 0x30; word +0x1C is then overwritten by the table)
  - +0x70: lo 0, hi 0x7130420200000000 (UNPACK 0x71 addr 0x4202 num 0x30 at word +0x7C; +0x70..+0x7B are then
    overwritten by the table)
  skyMungedUserFog gets only these headers; the other three get 96 halfwords (loop i = 0..0x5F, s1 = 0x5F - i):
  halfword at +0x1C + s1 when s1 is even, +0x80 + (s1 - 1) when s1 is odd [C 0x1FC770..0x1FC860], value
  `(int)v << 4` (cvt.w.s truncates) with
  - linear: v = 255.0 - i * 2.6842 (D_004E0728)
  - exp:    v = 255.0 / powf(256.0, i * 0.010526316) (D_004E072C = 1/95)
  - exp2:   v = 255.0 / powf(256.0, i * (i * 0.00011080333)) (D_004E0730 = 1/95^2)
  (powf = func_0011E388, a tail jump to __ieee754_powf, retail newlib; the float ops are the EE FPU's.)
  Readers: driver only (SkyOpen, _rwSkySetRenderState / SkyGetRenderState take their addresses; the DMA code
  sends them). The host stub may skip them unless the driver's fog upload is ever run natively.
- `fogTable` (gp 0x004E2874) = &skyLinearFog (0x00675F90) [C 0x1FC8C0]. Readers: `SkyGetRenderState` (driver,
  reached from RW core RwRenderStateGet through the device's +0x14 slot: compares fogTable with the three tables to
  answer the fog type) and `_rwSkySetRenderState` (writes it). **Stub: set it.**
- `currentMode` (gp 0x004E2878) = 0x58 [C 0x1FC8C4]. Readers: SkySystem requests 0xA (GETMODE) / 7 and SkyStart
  (copies videoModes[currentMode] into customMode unless it is -1). **Stub: set it.**
- `engineStarted` (gp 0x004E2880) = 0 [C 0x1FC8C8]. Readers: SkySystem (7, 0xC), RpSkyRenderStateGet /
  RpSkyRenderStateSet / RpSkySelectDeepZBuffer (driver, called by game: fromgame 2/3/1), the frame-buffer raster
  functions. **Stub: set it.**
- `currentCameraFormat` (gp 0x004E287C) = videoModes[0x58].depth (word at videoModes + 0x848) == 0x20 ? 0x500 : 0x100
  [C 0x1FC8B0..0x1FC8D8]. Readers: SkySystem (writes), `SkyGetRasterFormat` (driver; reached from RW core raster
  creation through the standards table). **Stub: set it.**

**Callees**: SyncDCache (Sony libkernl, cache only), func_0011E388 / __ieee754_powf (libm, pure).

---

## SkyStart 0x001FD490 (request 2) [RW 3.1 SkyStart]

**Arguments**: none read [C]. **Returns** 0 when `skyCustomModeIsOk()` returns 0 [C 0x1FD88C] (nothing after
0x1FD87C is done) or when `_rwDMADmaOpen()` returns 0 [C 0x1FE0D4] (after skyTexCacheCloseCache); otherwise the
nonzero result of `_rwDMADmaOpen()` [C 0x1FE16C].

The steps, in order [C]:

1. **skyInitialClear** (0x00675E40, 0xD0 bytes, .bss so 0 before) is built as a GIF PACKED packet and SyncDCache'd
   (start, start + 0x14F) [C 0x1FD494..0x1FD774]:
   - +0x00 GIFtag: lo 0xC000000000008001 (NLOOP 1, EOP 1, PRE 0, FLG PACKED, NREG 12), hi 0x0000EEEEEEEEEEEE (12 x A+D)
     (read-modify-write of the zeroed image; bits 16..31 cleared by `sh $zero, 2`).
   - then 12 A+D pairs (data at +0x10*k, register at +0x10*k + 8): ALPHA_1 (0x42) = 0; FRAME_1 (0x4C) = 0x100000
     (FBP 0, FBW 16); PRMODECONT (0x1A) = 1; SCISSOR_1 (0x40) = 0x03FF000003FF0000 (0..0x3FF x 0..0x3FF);
     TEST_1 (0x47) = 0x30000 (ZTE 1, ZTST ALWAYS); XYOFFSET_1 (0x18) = 0; ZBUF_1 (0x4E) = 0x10A000100 (ZBP 0x100,
     PSM 0xA, ZMSK 1); PRIM (0x00) = 6 (sprite); RGBAQ (0x01) = 0; XYZ2 (0x05) = 0; RGBAQ (0x01) = 0;
     XYZ2 (0x05) = 0x3FFF3FFF — a 1024x1024 sprite clear of VRAM from 0.
   Readers: driver only. Hardware: sent in step 5.
2. `_rwSkyInitConfigSystem()` [C 0x1FD778]; `sceGsSyncV(0)` [C 0x1FD780] (waits for a vblank).
3. **customMode** (0x00675F10, 0x34 bytes) when `currentMode != -1` [C 0x1FD788..0x1FD878]: +0x00..+0x17 and
   +0x18..+0x2F both = videoModes[currentMode] (0x18 bytes each); then if (+0x24 & 0x100): +0x1C = +0x1C >> 1
   (sra), bytes +0x31 = +0x30 = 1, +0x18 = skyFSAA0VisibleWidth if that is nonzero else skyFSAA0VisibleWidth (gp
   0x004E18A8) = +0x18, byte +0x33 = 0; else bytes +0x33 = 1, +0x31 = +0x30 = 2. Byte +0x32 = 0 either way.
   With currentMode == -1 (the game's boot: RwEngineSetVideoMode(-1) after SetVideoModeParams filled customMode
   through RpSkyGetModeStructurePtr) customMode is left as the game wrote it.
   Readers of customMode: driver (SkyStart, SkySystem 6/7, SkyCreateFBRaster, SkyCreateDBRaster, setUpFSAA0,
   skyCustomModeIsOk, SkyGetRasterFormat, RpSkyResume); game only writes it (SetVideoModeParams: +0..+0x2F words,
   bytes +0x30..+0x33) through the pointer `RpSkyGetModeStructurePtr` returns. Non-driver readers of SkyStart's
   writes: RW core via request 6 (mode -1) reads +0..+0x17, written here only when currentMode != -1.
4. `skyCustomModeIsOk()` [C 0x1FD87C]; 0 -> return 0. `skyVideoMode` (gp 0x004E1808) = &customMode [C 0x1FD8A8]
   (readers: driver only, but RpSkyRenderStateGet (fromgame 2) reads it).
5. **GS reset and initial clear** [C 0x1FD894..0x1FD968], with m = customMode, omode = byte m+0x32,
   f2 = word m+0x24:
   - omode == 0x50: `sceGsResetGraph(0, 0, 0x50, 1)`; else f2 & 4: `(0, 1, omode, 1)`; else f2 & 2:
     `(0, 1, omode, 0)`; else f2 & 0x100: `(0, 1, omode, 1)`; else `(0, 0, omode, 1)`
     (mode SCE_GS_INIT_RESET, interlace, omode, ffmode).
   - `sceDmaSendN(sceDmaGetChan(2), &skyInitialClear, 0xD)` (GIF channel, 13 qwords), then `while
     (sceGsSyncPath(1, 0)) ;`, then **D_STAT (0x1000E010) = 4** (clears channel 2's interrupt status).
6. **_rwDMAFlipData** (0x006754C0, 0x3D0 bytes) = an sceGsDBuffDc (disp[2] +0x00 / +0x28, giftag0 +0x50, draw01
   +0x60, draw02 +0xE0, clear0 +0x160, giftag1 +0x1C0, draw11 +0x1D0, draw12 +0x250, clear1 +0x2D0..+0x32F) plus
   RW's words +0x330..+0x3CF [C offsets from the calls below and the SDK 3.0.3 libgraph.h layout]. With
   w1/h1/d1 = m+0x0/+0x4/+0x8, w2/h2/d2 = m+0x18/+0x1C/+0x20, psm(d) = d == 0x10 ? 0xA : 0,
   zpsm = zBufferDepth (gp 0x004E18A4, set by RpSkySelectDeepZBuffer) == 0x10 ? 0x3A : 0x31 [C 0x1FD978..0x1FDA9C]:
   - `sceGsDefDispEnv(flip+0x00, psm(d2), (short)w2, (short)h2, 0, 0)` and the same into flip+0x28;
   - `sceGsSetDefDrawEnv(flip+0x60, psm(d1), (short)w1, (short)h1, 2, zpsm)`, `sceGsSetDefDrawEnv2(flip+0xE0, ...)`,
     `sceGsSetDefDrawEnv(flip+0x1D0, ...)`, `sceGsSetDefDrawEnv2(flip+0x250, ...)` (same arguments; ztest 2).
     What these write: see "Sony library callees".
   - giftag0 (+0x50) and giftag1 (+0x1C0) zeroed then: NLOOP 16, EOP 1, FLG PACKED, NREG 1, REGS0 = A+D (lo
     0x1000000000008010, hi 0xE) [C 0x1FDAA0..0x1FDB30].
   - frame / Z buffer pointers [C 0x1FDB2C..0x1FDDCC]. With fb(w, h, d) = (((w + 63) & ~63) * (d == 16 ? (h + 63) &
     ~63 : (h + 31) & ~31) >> (d == 16 ? 1 : 0)) >> 11 (unsigned; in 2048-word pages), and only the low 9 bits
     (FBP / ZBP) of each target qword replaced by `t & 0x1FF`:
     - if byte m+0x33 != 0: t = fb(w1, h1, d1); flip+0x38 (disp[1].dispfb), +0x60 (draw01.frame1), +0xE0
       (draw02.frame2) FBP = t; t = 2t.
     - else: t = fb(w2, h2, d2); if byte m+0x31 == 2 { flip+0x38 FBP = t; t = 2t }; flip+0x1D0, +0x250 FBP = t;
       u = fb(w1, h1, d1); if byte m+0x30 == 2 then t = t + u; flip+0xE0, +0x60 FBP = t; t = t + u.
     - then flip+0x70, +0xF0, +0x1E0, +0x260 (the four zbuf qwords) ZBP = t, and
       **textureBankStart** (gp 0x004E288C) = (t + fb(w1, h1, zBufferDepth)) << 11 [C 0x1FDDCC].
   - if (f2 & 2) { if (f2 & 0x200) { flip+0x330..+0x33F = flip+0x10..+0x1F (disp[0].dispfb, .display),
     flip+0x340..+0x34F = flip+0x38..+0x47 (disp[1].dispfb, .display); bytes flip+0x29 = flip+0x01 = 0x80 (PMODE.ALP);
     disp[0].pmode (+0x00) |= 3 and disp[1].pmode (+0x28) |= 3 (EN1, EN2); disp[0].dispfb (+0x10) and
     disp[1].dispfb (+0x38): DBY (bits 43..53) = 1; disp[0].display (+0x18) and disp[1].display (+0x40): DH (bits
     44..54) = (DH - 1) & 0x7FF } ; SyncDCache(flip, flip + 0x44F) } [C 0x1FDDCC..0x1FDEF4]. Note the +0x330 block
     is copied before the edits.
7. `skyTexCacheOpenCache(textureBankStart, 0x100000 - textureBankStart)` [C 0x1FDF04]; `cacheOpened` (gp
   0x004E180C) = 1, `skyTexCacheRestoreRequired` (gp 0x004E18B4) = 0, `skySuspended` (gp 0x004E182C) = 0
   [C 0x1FDF14..0x1FDF20] (readers: driver only).
8. `sceDmaSend(sceDmaGetChan(1), &D_004B1400)` [C 0x1FDF2C] (VIF1 channel; the chain at 0x004B1400, in game .data:
   CNT {STCYCL 0x0404, STMASK 0, STMOD 0}, CNT {DIRECT 4: GIFtag NLOOP 3 EOP NREG 1 A+D; TEST_1 = 0x5140B,
   TEXA = 0x0000008000000000, ALPHA_1 = 0x44}, END), then `while (sceGsSyncPath(1, 0)) ;`.
9. [C 0x1FDF48..0x1FE064]: flip+0xD0 (draw01.test1) |= 0x140B and flip+0x240 (draw11.test1) |= 0x140B (ATE 1,
   ATST GEQUAL, AREF 0x40, AFAIL FB_ONLY); flip+0x1F0 (draw11.xyoffset1) = flip+0x80 (draw01.xyoffset1);
   **D_STAT (0x1000E010) = 2** (clears channel 1's interrupt status); saved copies: flip+0x350..+0x377 =
   flip+0x00..+0x27 (disp[0]), flip+0x378..+0x39F = flip+0x28..+0x4F (disp[1]), flip+0x3A0..+0x3BF =
   flip+0x330..+0x34F; SyncDCache(flip, flip + 0x44F).
10. `skyFrameBit` (gp 0x004E188C) = 1 [C 0x1FE078] (reader outside SkyStart: RpSkyTexCacheAccessRaster, driver,
    fromgame 5); `sceGsSwapDBuffDc(flip, 0)` [C 0x1FE07C] (puts disp[0] in the CRTC registers and sends draw01/02:
    hardware); `while (sceGsSyncPath(1, 0)) ;`; **D_STAT = 4**.
11. flip+0x3CC, +0x3C8, +0x3C4, +0x3C0 = 0 [C 0x1FE0AC..0x1FE0BC].
12. r = `_rwDMADmaOpen()`; r == 0 -> `skyTexCacheCloseCache()`, return 0 [C 0x1FE0B8..0x1FE0D8].
13. Register shadows [C 0x1FE0C8..0x1FE130] (all 64-bit sd): x = flip+0x80 (draw01.xyoffset1) with bit 35 cleared
    (OFY's 0.5-pixel bit); `skyYOff` (gp 0x004E1898) = (x >> 36) & 0xFFF, `skyXOff` (gp 0x004E1890) = (x >> 4) &
    0xFFF (driver only); **`skyFrame_1`** (gp 0x004E1838) = flip+0x60 (draw01.frame1, after step 6);
    **`skyZbuf_1`** (gp 0x004E1830) = flip+0x70 (draw01.zbuf1); **`skyTest_1`** (gp 0x004E1840) = flip+0xD0 (after
    step 9); **`skyXyoffset_1`** (gp 0x004E1848) = x (flip+0x80 itself keeps bit 35); `D_004E28B0` = 1 (readers:
    RpSkyRenderStateGet / Set, driver, fromgame 2/3).
    Readers of the four shadows: game (skyFrame_1: CB3Bloom, CB3SkyDomeRenderer, CB3TrackRenderer, Gt2dRenderer,
    CB3CameraData, func_00217E70, ...; skyZbuf_1: CB3Bloom, CB3SkyDomeRenderer, CB3GraphicsManager::Prepare,
    func_00227B80, CB3CameraData, CB3ParticleRenderer, CB3TrailRenderer, ...; skyTest_1: 10+ game Restore/Render
    functions; skyXyoffset_1: CB3Bloom, CB3GraphicsManager, Gt2dRenderer, CB3CameraData, CB3TrackRenderer) and the
    driver (RpSkyRenderStateGet/Set, the pipelines). **Stub: must set all four exactly.**
14. `setInitialRenderState()` [C 0x1FE12C].
15. byte `_rwDMAAsyncPktPtr` (gp 0x004E27CC) = (m+0xC & 4) ? 1 : 0 [C 0x1FE140..0x1FE148] (reader: func_001F43B0,
    driver, fromgame 3).
16. if (f2 & 0x100) `setUpFSAA0(flip)` [C 0x1FE15C].
17. `engineStarted` = 1 [C 0x1FE168]; return r.

**_rwDMAFlipData's non-driver readers** [C xref]: `Construct__15CGtScreenCentreFv` (+0x18 low word: DX = bits 0..11,
DY = bits 12..22), `MoveScreen__15CGtScreenCentreFff` (reads +0x368, +0x390, +0x3A8, +0x3B8 — the step 9 copies —
and +0x18, +0x40, +0x338, +0x348; writes +0x18, +0x40, +0x338, +0x348), `PresentDrawBuffer__18CB3GraphicsManagerFv`
(writes +0x3C0..+0x3CC); driver `RpSkyTexCacheAccessRaster` (fromgame 5) reads +0x60 / +0x1D0. **Stub: the
DISPLAY / DISPFB words (+0x10, +0x18, +0x38, +0x40), the +0x330 block and the +0x350..+0x3BF copies must hold
SkyStart's values; +0x60 / +0x1D0 / +0x70 too (frame / zbuf).**

**Hardware summary**: sceGsResetGraph (GS CSR reset, SMODE / SetGsCrt), sceGsSyncV, sceDmaSendN on channel 2 (GIF)
and sceDmaSend on channel 1 (VIF1), sceGsSyncPath polling, three stores to D_STAT 0x1000E010 (4, 2, 4),
sceGsSwapDBuffDc (PMODE / DISPFB / DISPLAY registers + a GIF transfer), SyncDCache, plus whatever the driver callees
do (below). None of these must run on the host.

---

## Stop request (3): _rwDMADmaClose 0x001F4578 [RW 3.1 _rwDMADmaClose]

SkySystem's case 3 (ported) calls `_rwDMADmaClose()`, clears `_rwDMAFlipData` +0x3C0..+0x3CC, calls
`skyTexCacheCloseCache()` (ported: texcache.cpp), then `engineStarted` = 0, `needCache` (gp 0x004E1810) = 1,
`cacheOpened` = 0 and returns 1 [C 0x1FE3A8..0x1FE3DC]. `needCache` has no other reference [C xref].

`_rwDMADmaClose` [C 0x1F4578..0x1F45BC]: **arguments** none; **returns** nothing defined (v0 = whatever was last
left). Steps: `_rwDMAUnhook()` (below); `rwDMAVIFGate` (gp 0x004E17D4) = 0; if `rwDMAbufSize` (gp 0x004E17E4) == 0:
`ourGlobals.field_138(rwDMAbuf)` (the RW free function; frees a buffer the driver allocated itself — a buffer the
game handed over with `_rwDMAPreAlloc` has a nonzero size and is not freed); then `rwDMAbuf` (gp 0x004E17E0) = 0,
`rwDMAbufSize` = 0, `rwDMAHYieldFunc` (gp 0x004E17D8) = 0, byte `rwDMAMinVsyncCnt` (gp 0x004E17DC) = 0. No hardware
access of its own.
Readers [C xref]: all driver — rwDMAVIFGate (_rwDMADmaOpen, _rwDMAAddImageUpload, _rwDMAPreAlloc), rwDMAbuf /
rwDMAbufSize (_rwDMADmaOpen, _rwDMAPreAlloc), rwDMAHYieldFunc (_rwDMAForceBufferSwap), rwDMAMinVsyncCnt (written
only, by func_001F4290 too). Note the game sets some of these through driver entry points (`_rwDMAPreAlloc`,
`_rwDMAYieldCallbackSet`, fromgame 2): a stub of the stop request must clear them so a later start behaves the same.

---

## Functions ported instead of documented (memory only)

| Function | File | Note |
|---|---|---|
| `_rwDeviceGetHandle` 0x001FF360 | basky.cpp | returns &dGSkyDevice.311 (0x0049AD38, spelled D_0049AD38) |
| `SkySystem` 0x001FE1A0 | basky.cpp | all 23 cases |
| `SkyFinalizeStart` 0x001FC928 | basky.cpp | its callees below |
| `_rpSkyDestroyFrameBufferRasters` 0x001FF370 | basky.cpp | RwRasterDestroy of skyDrawRaster / skyDisplayRaster |
| `_rwSkyRasterInitiateStop` 0x00205DA8 | skyblit.cpp | RwRasterDestroy of blitRaster[0..1] |
| `_rwSkyUnOverloadPipelines` 0x0020B238 | skyinst.cpp | empty |
| `skyTexCacheCloseCache` 0x0020CD08 | texcache.cpp | readers of skyTexCache: driver only |

---

## Driver callees (of SkyStart, the stop request and SkyFinalizeStart)

Note: `_rwDMAPreAlloc`'s second argument at the game's call 0x00227F88 is 0x180 (`addiu $a1, $zero, 0x180` in the delay slot) [C].

## sky2 driver start/stop handlers: retail RW 3.6 (SLUS-21050)

Source of truth: `decomp-snap/native2/assembly/asm/rw/renderware.s`. Addresses are retail VAs. gp = 0x004E8670
(checked: `%gp_rel(rwDMAVIFGate)` = 0x9164, which gives 0x004E17D4, the symbol-table value). Tags: [C] read from the asm
(the address is cited), [L] likely, [G] guess. Reader lists come from `xref.py` (%gp_rel/%lo/%hi only). **xref cannot see two
kinds of access:** reads through a pointer that has escaped, and the raw `-0x5Exx($gp)` offsets used by the hand-written ISRs
`rwDMAHandler` 0x1F1778 and `rwDMAVSYNCHandler` 0x1F1CE8. I decoded those ISR offsets by hand (§0).

Some symbol-table labels in the DMA area are clearly misassigned (they are RW 3.1 names placed on 3.6 addresses). Despite
that, the names are kept as the tables give them, and their *role* is given in [L]:
- `farFogPlane` 0x004E27C0 = **current packet write cursor** (`_rwDMAOpenVIFPkt` returns with it pointing at the slot to fill; every
  caller then does `sq …,0(farFogPlane); farFogPlane += 0x10`). Game code reads and advances it inline in 93 functions.
- `_gOldRasterShowRasterFunc` 0x004E27C4 = a second packet cursor (async/image side) [L]. The game's CGtTextureCache reads and writes it.
- `_rwDMAInAsyncPkt.35` 0x004E1900 / `.53` 0x004E1904 = texture-cache entry count (data = 0xC4) / a texcache counter [L].
- `rwDMAPURefList` 0x004E284C = **saved $gp for the ISRs** [C]: `_rwDMAHook` 0x1F3C1C stores `$gp` there, and both ISRs reload `$gp` from it
  (0x1F1D04, 0x1F17BC).
- `rwPendingCtrlPtr` 0x004E27F8 + `D_004E27FC` = a 2-entry array (the start of each half-buffer), indexed by the byte `rwDMADmaBufs+4`
  (the current-buffer index) [L, from `_rwDMAUnhook` 0x1F3E98..0x1F3EA4].
- `rwDMADispatchEnd` 0x004E2844 = base of the PU-reference ring (u32 pointers, `rwDMAPURefSize` entries) [L, from `_rwDMAUnhook`].

### 0. ISR-only byte flags (raw gp offsets, hidden from xref) [C]
| addr | label | written by | meaning for a host stub |
|---|---|---|---|
| 0x4E2830 (byte) | `rwDMAPktBase` | rwDMAHandler sb, rwDMAVSYNCHandler sb, `_rwDMADmaOpen`=0 | DMA-busy flag. `_rwDMAUnhook` spins until it is 0 |
| 0x4E27CF (byte) | `_rwDMAAsyncPktPtr+3` | both ISRs sb, `_rwDMADmaOpen`=0 | busy/flip-pending flag. `_rwDMAUnhook` spins until it is 0 |
| 0x4E27CE, 0x4E27CD, 0x4E27D0 (`_rwDMAGateSlot`) | | ISRs | ISR state |
| 0x4E2841 (byte) | `rwDMADispatchPtr+1` | `_rwDMAHook`=1, VSYNC ISR | vsync counter compared with `rwDMAMinVsyncCnt` 0x4E17DC |
| 0x4E2834 / 0x4E2838 / 0x4E283C | `rwDMAPktType` / `rwDMADispatching` / `rwDMAFlipSt` | ISRs | ISR state |

---

### 1. `_rwSkyInitConfigSystem` 0x1F4600 (0x64)
1. `void f(void)`. There are no args. v0 is left as `sceDmaReset`'s return. SkyStart 0x1FD778 ignores it [C].
2. Guest memory writes: none [C].
3. —
4. Hardware, in order [C 0x1F4604..0x1F4654]:
   - `VIF1_FBRST` 0x10003C10 = 1 (RST), then `VIF0_FBRST` 0x10003810 = 1 (RST), then `GIF_CTRL` 0x10003000 = 1 (RST).
   - COP2: `cfc2.ni vi28` | 0x200, then `ctc2.ni` (FBRST.RS1 = VU1 reset). Then `cfc2.ni vi28` | 0x2, then `ctc2.ni` (FBRST.RS0 = VU0 reset).
   - `sceGsResetPath()` (retail libgraph 0x102520) [C]: VIF1_FBRST=1; VIF1_ERR 0x10003C20=2; sync; vi28|=0x200; sync.p;
     2 qwords into VIF1_FIFO 0x10005000 from `init_vif_regs.12` 0x483F30 = {0x01000404 STCYCL 4/4, 0x20000000 STMASK, 0,
     0x05000000 STMOD 0}, {0x06000000 MSKPATH3 off, 0x03000000 BASE 0, 0x02000000 OFFSET 0, 0x04000000 ITOP 0}; GIF_CTRL=1.
   - `sceDmaReset(1)` (0x103D48) [C]: for each channel with `isclr[i]`, it zeroes SADR/CHCR/TADR/MADR/ASR1/ASR0. Then
     D_STAT 0x1000E010 = 0xFF1F (clears all CIS/SIS/MEIS/BEIS), then D_STAT = D_STAT & 0xFF1F0000 (toggles the set mask bits off),
     then `sceDmaPutEnv(zeroed 0x14-byte env)`, then D_CTRL 0x1000E000 |= 1 (DMAE). It returns the old D_CTRL&1.
5. Callees: sceGsResetPath and sceDmaReset (Sony, hardware only).

### 2. `skyCustomModeIsOk` 0x1FD138 (0x354)
`customMode` 0x675F10 (0x34 bytes) is the RwSkyVideoMode returned by `RpSkyGetModeStructurePtr` [L, RW 3.5 rwcore.h layout]:
draw RwVideoMode at +0x00 {width 0, height 4, depth 8, flags 0xC, refRate 0x10, format 0x14}, display at +0x18 {width 0x18,
height 0x1C, depth 0x20, flags 0x24, refRate 0x28, format 0x2C}, u8 numDrawBuffers 0x30, numDisplayBuffers 0x31, tvSystem 0x32,
inplace 0x33.
1. `RwBool f(void)`. It returns 1 or 0 [C]. The checks run in this order, and the first failure returns 0:
   1. If tvSystem(+0x32)==0, it is auto-filled from display.height (+0x1C): 224/448 give 2, 256/512 give 3, 480 gives 0x50. Any other height returns 0.
   2. tvSystem must be in {2, 3, 0x50}.
   3. If inplace(+0x33)!=0, the halfword at +0x30 must be 0x0202 (both buffer counts are 2).
   4. numDraw and numDisplay must each be in {1,2}.
   5. draw.depth and display.depth must each be in {16,32}.
   6. drawBytes = A64(draw.w)·(d16?A64(draw.h):A32(draw.h))·(d16?2:4). dispBytes is the same formula for display.
      zBytes uses draw w/h with `zBufferDepth` 0x4E18A4 instead of the depth. Here A64/A32 mean round up to 64/32.
   7. If inplace, drawBytes must be ≥ dispBytes.
   8. draw.format(+0x14): if it is non-zero it must be 0x100 for depth 16 and 0x500 for depth 32. If it is zero, it is set to that value.
      The same rule applies to display.format(+0x2C).
   9. The sum must be ≤ 0x400000. With inplace=0 the sum is dispBytes·numDisplay + drawBytes·numDraw + zBytes; with inplace≠0 it is drawBytes·numDraw + zBytes.
      Pass returns 1 (0x1FD484), fail returns 0 (0x1FD180).
2. Writes (only in the auto-fill cases; they persist even if a later check fails) [C]: `customMode+0x32` sb (2/3/0x50, at 0x1FD1A8/0x1FD1C0);
   `customMode+0x14` sw (0x100 if draw.depth==16, else 0x500; 0x1FD3D4); `customMode+0x2C` sw (same rule for display.depth; 0x1FD430).
3. Readers of `customMode`: driver only by symbol (SkyStart, SkySystem, RpSkyResume, setUpFSAA0, SkyCreateFBRaster/DBRaster,
   SkyGetRasterFormat). **The address escapes** through `RpSkyGetModeStructurePtr` 0x1FF3C8 (fromgame=2). Game code
   `SetVideoModeParams__18CB3GraphicsManagerFP14RwSkyVideoMode` 0x227A20 then fills all fields [C]: in each of its 3 modes it sets
   tvSystem (2 / 0x50 / 3), draw.format=0x500 and display.format=0x100. **So in B3 this function writes nothing** [C]. It is a pure check
   and returns 1 for all three game modes (640x448/32 + 640x448/16 NTSC, 640x448 + 640x480 for 480p, 640x512 + 640x512 PAL; z=32 because the game
   calls `RpSkySelectDeepZBuffer(1)` 0x227F34, so zBufferDepth=0x20; the largest total is 3,276,800 for PAL).
4. Hardware: none.
5. Callees: none.

### 3. `skyTexCacheOpenCache` 0x20CC08 (0x100)
1. `RwBool f(a0 = start, a1 = size)`. SkyStart 0x1FDF04 passes a0=`textureBankStart` (0x4E288C) and a1=0x100000−a0 [C]
   (GS word units [L]). It returns 0 only if the entry-array malloc fails, else 1 [C].
2. Writes [C]:
   - `skyTexCache` 0x49B040: +0x34=a0, +0x1C=a1, +0x18=a0. `_rwDMAInAsyncPkt.53` 0x4E1904 (sw) = 0.
   - If `skyTexCache+0`!=0 (data initial = 1): +0x14 = `malloc(N*12, 0x40411)` via `ourGlobals+0x134` (the same slot core `MallocWrapper`
     uses), with N=`_rwDMAInAsyncPkt.35` (data 0xC4, so 0x930 bytes). If NULL it returns 0 (+0x14=0). Otherwise +0x30=−1, +0x10=N,
     +0x20=&`_skyTexCacheUploadRaster` 0x20C6E8, +0x04=−1, +0x08=−1, +0x0C=N, +0x24=0, +0x2C=−1.
   - Else: +0x20=&`func_0020E4E0`.
   - `state` 0x6763C0 (4 qwords, GS A+D template for image upload): +0x00 {lo 0, hi 0x3F TEXFLUSH}; +0x10 {lo 0, hi 0x51 TRXPOS};
     +0x20 {lo 0, hi 0x53 TRXDIR}; +0x30 {lo 0x1000000000008005 = GIFtag NLOOP=5 EOP=1 PACKED NREG=1, hi 0xE = A+D}.
3. Readers: `skyTexCache` is read by **game-called driver functions** RpSkyTexCacheAccessRaster/AccessSpeculate (fg=5), Flush (4),
   GetStartAddress (3; returns +0x18), SetStartAddress (2; reads +0,+4,+8,+0xC,+0x10,+0x14,+0x18,+0x1C,+0x34), Disable (2),
   RasterUnlockAll (1), SetCallBack/SetReleaseCallBack (1). No direct game access. `_rwDMAInAsyncPkt.53` is read by
   RpSkyTexCacheAccessRaster (fg=5). `state` is driver-only (texcache upload builders).
4. Hardware: none.
5. Callees: the RW core allocator (memory only).

### 4. `_rwDMADmaOpen` 0x1F3F98 (0x2F0)
1. `RwBool f(void)`. It returns 1 on success, or 0 if the alloc fails or `_rwDMAHook` returns 0 [C]. Before the call, SkyStart 0x1FE0A8 writes D_STAT=4 and
   `_rwDMAFlipData+0x3C0..0x3CC`=0. **In B3 the buffer is game-supplied:** `_rwDMAPreAlloc` is called at 0x227F88 with a memory-manager block,
   so `rwDMAbufSize`!=0 and the malloc branch is skipped [C].
2. Writes [C]. Let S = `rwDMAbufSize` (if 0, S=0x100000) and G = (`rwDMAbuf`+0x7F)&~0x7F.
   - Only when `rwDMAbuf`==0: `rwDMAbuf`=malloc(S+0x80, 0x40411); `rwDMAbufSize`=0.
   - If `_rwDMAHook` fails: return 0. In that case, if `rwDMAbufSize`==0 it calls free(`rwDMAbuf`) via `ourGlobals+0x138`. `rwDMAbuf` is **not** cleared.
   - The state layout below uses R = `rwDMAPURefSize` (u16, read in the delay slot at 0x1F4008), S' = S−0x800−R·4, H = (S'>>1)&~0x7F
     and B = G+0x800 [C 0x1F4030..0x1F4094]:
     `rwDMAVIFGate`=G; `rwPendingCtrlPtr`=B; `D_004E27FC`=B+H; `rwDMADmaBufs`(word)=H; byte `rwDMADmaBufs+4`=0;
     `rwDMADispatchEnd`=B+2H; `rwDMAWaited`(u16)=0; `D_004E284A`(u16)=0; `rwDMABufTopPtr`=B; `rwDMABufDataPtr`=B+0x78;
     `rwDMABufAsyncTopPtr`=B+0x80; `rwDMADmaBufSize`=B+H; `rwDMABufDataTopPtr`=B+H; `farFogPlane`=0;
     `_gOldRasterShowRasterFunc`=0; `rwDMABufCtrlTopPtr`=0; `rwDMAPktRealBase`=0; `rwDMAAsyncPktRealBase`=0.
   - VIF gate G[0x000..0x7FF] is zeroed, then [C 0x1F40B8..0x1F41B0]:
     G+0x070 = words {0x11000000 FLUSH, 0, 0, 0x06000000 MSKPATH3(0)};
     G+0x200 = {0x06008000 MSKPATH3(1), 0, 0, 0};  G+0x210 = {FLUSH, 0x13000000 FLUSHA, FLUSH, FLUSHA};
     G+0x3F0 = {0x60000040 = DMAtag ret QWC=0x40, 0, 0, 0}; G+0x400..0x7FF = 0x40 qwords of {0, 0, 0x7F, 0}.
   - 4 DMA tag qwords (lo word / addr / VIF code / VIF code): `rwDMAFlushAGate` 0x675890 = {0x30000021 ref QWC=0x21, ADDR=G, FLUSH, FLUSHA};
     `rwDMAFlushAGateFlush` 0x6758B0 = {0x30000022 ref QWC=0x22, G, FLUSH, FLUSHA}; `rwDMAAFlush` 0x6758A0 = {0x10000000 cnt QWC=0, 0,
     FLUSH, FLUSHA}; `rwDMAANull` 0x6758C0 = {0x10000000, 0, 0, 0}.
   - Then: byte `rwDMAPktBase`=0; `_rwDMA_local_rwDMAPktPtr`=0x70000000 (scratchpad); `_rwDMA_local_rwDMAAsyncPktPtr`=0;
     `rwSPRBasePtr`=`rwSPRBaseInMem`=`rwPendingBase`=0; byte `_rwDMAAsyncPktPtr+3`=0; `_rwDMAPktPtr`=0; byte `rwDMADispatchPtr`=0.
3. Readers. **Game (inline):** `farFogPlane` (lw/sw in 93 game functions: Bloom, Fog, Im3d, Gt2dRenderer, TrackRenderer…);
   `_gOldRasterShowRasterFunc` (CGtTextureCache::AddRasterToCurrentDoubleBuffer / BuildPacketToSendRasterToGS);
   `_rwDMAPktPtr` (game *writes* it in CGtTextureCache::AddRasterToCache). **Driver functions the game calls:** `_rwDMAOpenVIFPkt` (fg=66) reads
   DmaBufs/BufTopPtr/BufDataPtr/BufAsyncTopPtr/BufDataTopPtr/PktRealBase/AsyncPktRealBase/local ptrs/`farFogPlane`/`_rwDMAPktPtr`;
   `_rwDMAForceBufferSwap` (2) reads PendingCtrlPtr[], DmaBufs, SPR*/Pending* and `rwDMAPktBase`; `func_001F43B0` (3) reads PendingCtrlPtr,
   DmaBufs, BufTopPtr, PktBase, `_rwDMAAsyncPktPtr`, DispatchPtr; `_rwDMAAddPURef` (1) reads DispatchEnd, `D_004E284A`, `rwDMAWaited`;
   `_rwDMAAddImageUpload` (1) reads VIFGate, DmaBufSize, and the 4 gate tags (by address); `RpSkyRenderStateSet` (3) and
   `RpSkyTexCacheAccessRaster` (5) read `farFogPlane`. No RW-core readers. G and the tag qwords are DMA'd by hardware (VIF1 ch1).
4. Hardware: `SyncDCache(G, G + 2·H + 0x7FF)` 0x1F423C [C]. Everything else is in `_rwDMAHook`.
5. Callees: the RW core malloc/free (memory only); `_rwDMAHook` (hardware, below).

#### 4a. `_rwDMAHook` 0x1F3C08 (0x130): returns 1 on success, 0 on failure [C]
- `rwDMAPURefList` = $gp (0x1F3C1C).
- `AddIntcHandler(2 /*VBlank start*/, rwDMAVSYNCHandler, 0)` goes to `rwVSYNCHandlerHid`. `AddDmacHandler(1 /*VIF1*/, rwDMAHandler, 0)` goes to
  `rwDMAHandlerVIF1Hid`. `AddDmacHandler(2 /*GIF*/, rwDMAHandler, 0)` goes to `rwDMAHandlerGIFHid`. If any of these returns −1, the
  earlier ones are removed, the ids are set to −1, and it returns 0.
- Byte `rwDMADispatchPtr+1` 0x4E2841 = 1. `rwDMAOwnVSYNC` = return of `EnableIntc(2)`; `rwDMAOwnVIF1` = return of `EnableDmac(1)`;
  `rwDMAOwnGIF` = return of `EnableDmac(2)` [C, stores in delay slots 0x1F3CA4/0x1F3CB0/0x1F3CB8; meaning "was newly enabled" is L].
- HW: `VIF0_ERR` 0x10003820 |= 2 (ME0: mask DMAtag-mismatch error), then **`VIF1_ERR` 0x10003C20 = that same value** (VIF0_ERR|2) [C 0x1F3CE8];
  `D_CTRL` 0x1000E000 &= 0xFFFFF8FD (clears RELE bit1 and RCYC bits 8-10).
- These globals are read only by `_rwDMAUnhook`, plus the ISRs (via `rwDMAPURefList`, the 0x4E2841 counter).

### 5. `setInitialRenderState` 0x1FC4C0 (0x178). Also called by RpSkyResume 0x1FE650
1. `void f(void)` [C].
2. Writes [C]: `skySrcBlend` 0x4E1880=2; `skyDestBlend` 0x4E1884=3; `skyAlpha_1` 0x4E1868 (sd) = sign-extended word at `skyBlendStates+0x50`
   0x49AC18 = **0x44** (ALPHA A=Cs B=Cd C=As D=Cd FIX=0); `skyTex1_1` 0x4E1860 (sd)=0x60; `gSkyCullState` 0x4E286C=2;
   `skyDitherState` 0x4E2884=1; `skyPrim_State` 0x4E1878 (sd)=8; `skyVertexAlpha` 0x4E2868=0; `skyAlphaTex` 0x4E2864=0;
   `skyTextureRaster` 0x4E2860=0; `skyClamp_1` 0x4E1858 (sd)=0.
   Packet: `_rwDMAOpenVIFPkt(0x80000000, 7)`, then 7 qwords at `farFogPlane` (it advances by 0x70): GIFtag {lo 0x1000000000008006: NLOOP=6
   EOP=1 PACKED NREG=1; hi 0xE A+D}; A+D (data, reg): (skyAlpha_1=0x44, 0x42 ALPHA_1), (0, 0x08 CLAMP_1), (0, 0x49 PABE),
   (0x0000008000000000 TA0=0 AEM=0 TA1=0x80, 0x3B TEXA), (0x6071243571603524, 0x44 DIMX), (1, 0x46 COLCLAMP).
   Then `_rwSkySetRenderState(6 ZTESTENABLE, 1)` and `(8 ZWRITEENABLE, 1)` (jump table 0x4B8420, cases at 0x1F4A00 / 0x1F4AA4) [C]:
   - case 6: `skyTest_1` 0x4E1840 = (skyTest_1 & ~0x70000) | 0x50000 (ZTE=1 ZTST=GEQUAL; FALSE would give 0x30000 ALWAYS). Packet
     OpenVIFPkt(0x80000000,2): GIFtag 0x1000000000008001/0xE, then (skyTest_1, 0x47 TEST_1). It returns 1.
   - case 8: `skyZbuf_1` 0x4E1830 &= ~(1<<32) (ZMSK=0). If (skyTest_1 & 0x10000)==0, skyTest_1 = (…&~0x70000)|0x30000. Packet
     OpenVIFPkt(0x80000000,3): GIFtag 0x1000000000008002/0xE, then (skyZbuf_1, 0x4E ZBUF_1), (skyTest_1, 0x47 TEST_1). It returns 1.
   skyTest_1/skyZbuf_1 come from SkyStart (0x1FE120/0x1FE124: `_rwDMAFlipData`+0xD0 / +0x70) just before.
3. Readers. **Game reads them directly:** `skyAlpha_1` (13 fns: Bloom/DirBlur/Fog/RadialBlur RestoreRenderstates, Gt2dRenderer Begin/End/Suspend,
   SunRenderer, ParticleRenderer, PopcornArray, SkyDome EndRender); `skyTex1_1` (10; Gt2dRenderer::SetTextureFilterMode *writes*);
   `skyClamp_1` (9); `skyTest_1` (10); `skyZbuf_1` (9; TrackRenderer::RestoreRenderStatesAfterRenderingTrack *writes*);
   `skyTextureRaster` (14, several *write*: VehicleRenderer/Gt2dRenderer SetTexture, CGtTextureCache Flush/ForceBufferSwap…).
   **Driver functions the game calls:** RpSkyRenderStateGet (fg=2: skyAlpha_1, skyTest_1, skyDitherState), RpSkyRenderStateSet (fg=3: writes them),
   RpSkyTexCacheAccessRaster (fg=5: skyTex1_1, skyTextureRaster), RpSkyTexCacheFlush/SetStartAddress (write skyTextureRaster).
   Driver-only: skySrcBlend, skyDestBlend, gSkyCullState, skyVertexAlpha, skyAlphaTex, skyPrim_State (SkyGetRenderState,
   `_rwSkySetRenderState`, openVU1SetupPkt, func_00208120). No RW-core readers by symbol.
4. Hardware: none directly. The packet is VIF1-DMA'd later. `_rwDMAOpenVIFPkt` itself can touch D8 (SPR→RAM) and D_STAT/D_PCR (see 4c).
5. Callees: `_rwDMAOpenVIFPkt` (driver, writes the DMA cursors), `_rwSkySetRenderState` (driver, the writes above).

#### 4c. `_rwDMAOpenVIFPkt` 0x1F3080 (0x544): summary [C for the list, L for semantics]
`(a0 type, a1 nQW)`. It reserves nQW+header qwords and leaves `farFogPlane` at the write slot. It returns 1, or 0 when even a fresh half-buffer
is too small (`rwDMADmaBufs` < nQW·16+0xC0). It writes `farFogPlane`, `_rwDMA_local_rwDMAAsyncPktPtr`, `_rwDMAPktPtr`, `rwDMAPktRealBase`,
`rwDMAAsyncPktRealBase` (0x1101 / 0x80001101), `rwDMABufTopPtr`/`DataPtr`/`AsyncTopPtr`/`CtrlTopPtr`. It writes the header qword through the
0x20000000 uncached alias: DMAtag {0x10000000|n, …, VIF 0x50000000|n DIRECT}. When the scratchpad holds data it flushes it:
if D8_CHCR 0x1000D000 & 0x100 then D_PCR 0x1000E020=0x100 and it spins on `bc0f`; then D_STAT=0x100, D8_SADR 0x1000D080=`_rwDMA_local_rwDMAPktPtr`,
D8_MADR 0x1000D010=`_rwDMA_local_rwDMAAsyncPktPtr`, D8_QWC 0x1000D020=bytes/16, D8_CHCR=0x100. Callees: `_rwDMAClosePkt`, `_rwDMAForceBufferSwap`.

### 6. `setUpFSAA0` 0x1FCA18 (0x720)
1. `void f(a0 = F)`. SkyStart 0x1FE15C passes F=&`_rwDMAFlipData` 0x6754C0, only if `customMode.display.flags & 0x100`
   (rwVIDEOMODE_PS2_FSAASHRINKBLIT [L, rw35 rwplcore.h]). B3 sets 0x103 for NTSC/PAL and 1 for 480p, so this runs for NTSC/PAL [C, 0x227A7C/0x227B04].
   v0 is undefined on return; SkyStart then sets `engineStarted`=1.
2. Writes [C]. First it computes A=fptoui(draw.w·8/disp.w+0.5), B=fptoui(draw.h·8/disp.h+0.5), C=fptoui(draw.h·16/disp.h+0.5)
   (B3 NTSC/PAL: A=8, B=8, C=16). Then for i=0..3 it builds dmaData_i = `dmaData1.295` 0x675900 / `2.296` 0x675A40 / `3.297` 0x675B80 /
   `4.298` 0x675CC0 (0x140 bytes each). Here t4 = (display.flags & 2) ? 0 : (i>>1)&1, which is always 0 in B3 (INTERLACE is set).
   The GS registers below are listed as (data, reg):
   - +0x00 DMAtag {0x70000013 = end, QWC=0x13; rest 0}. +0x10 GIFtag {0x1000000000008012: NLOOP=18 EOP=1 PACKED NREG=1; 0xE}.
   - +0x20 FRAME_1(0x4C) = (DFB.PSM<<24)|((disp.w>>6)<<16)|(DFB&0x1FF). DFB = t4 ? *(u64*)(F+0x360) : *(F+0x10) (a DISPFB value;
     PSM = bits 15-19). +0x30 XYOFFSET_1(0x18)=0. +0x40 SCISSOR_1(0x40)=0x7FFF00007FFF0000. +0x50 TEST_1(0x47)=0x30000.
   - +0x60 ZBUF_1(0x4E) = (((Z>>24)&0xF)<<24)|(Z&0x1FF)|1<<32 (ZMSK), where Z=*(u64*)(F+0x1E0). +0x70 DTHE(0x45) = disp.depth<24.
   - +0x80 CLAMP_1(0x08) = ((draw.h−1)<<34)|((draw.w−1)<<14)|0xA (REGION_CLAMP in both directions).
   - +0x90 TEX0_1(0x06) = 0xAA8000000 (TW=10 TH=10 TFX=1 TCC=0) | ((X>>24&0x3F)<<20) | ((draw.w>>6)<<14) | ((X&0x1FF)<<5), where
     X = (i&1) ? *(u64*)(F+0x1D0) : *(F+0x60) (FRAME values). +0xA0 TEX1_1(0x14)=0x61. +0xB0 PRIM(0x00)=0x16 (sprite, TME).
   - +0xC0 RGBAQ(0x01)=0x3F80000000000000 (Q=1.0). +0xD0 ST(0x02): S=float(A)·2^-14, T=float(t4?C:B)·2^-14. +0xE0 XYZ2(0x05)=0.
   - +0xF0 ST(0x02): S=float(draw.w·16+A)·2^-14, T=float(draw.h·16+(t4?C:B))·2^-14. +0x100 XYZ2(0x05) = (disp.h<<20)|(disp.w<<4).
   - +0x110 XYOFFSET_1 = `skyXyoffset_1`. +0x120 FRAME_1 = *(F+0x60). +0x130 ZBUF_1 = *(F+0x70).
   - It calls SyncDCache(dmaData_i, dmaData_i+0x1BF) each iteration (this over-flushes into the next buffer) [C 0x1FD0B4].
   - After the loop: F+0x3C0=&dmaData1, +0x3C4=&dmaData2, +0x3C8=&dmaData3, +0x3CC=&dmaData4 [C 0x1FD0EC..0x1FD0F8].
3. Readers. dmaData* is referenced by symbol only here. The pointers escape into F+0x3C0..0x3CC. **`rwDMAVSYNCHandler` 0x1F1ED4** loads
   F+0x3C0+4·idx and, if it is non-zero, kicks GIF DMA: D2_TADR 0x1000A030=ptr, D2_QWC 0x1000A020=0, D2_CHCR 0x1000A000=0x104 (chain, STR).
   `SkyRasterShowRaster` 0x1FFB04 sets `skyTexCacheRestoreRequired`=1 if any slot is non-zero. **Game:** `PresentDrawBuffer__18CB3GraphicsManagerFv`
   0x2271C0 overwrites all four slots with its own CBlit packet [C], and SkyStart/SkySystem zero them.
4. Hardware: SyncDCache ×4. The packets are consumed by GIF DMA (PATH3) from the VSYNC ISR.
5. Callees: `fptoui`, `__floatdisf` (runtime float conversion), SyncDCache.

### 7. `_rwDMAUnhook` 0x1F3D38 (0x260). Called by `_rwDMADmaClose` 0x1F4580 (SkySystem stop) and `RpSkySuspend` 0x1FF468
1. `void f(void)` [C].
2. Writes [C]:
   - Calls `_rwDMAClosePkt()`.
   - If `rwPendingBase`!=0: optionally waits for SPR DMA (see 4). Then, if `rwSPRBaseInMem`!=0, P=`rwPendingAsyncBase` gets
     P[0]=0x1121, P[1]=`rwSPRBasePtr`, P[2]=0x122, P[3]=`rwSPRBaseInMem` (u32 each) and `rwPendingAsyncBase`=P+8. Otherwise it sets
     P[0]=`rwPendingBase`, P[1]=`rwSPRBasePtr`. Then `rwSPRBasePtr`=`rwSPRBaseInMem`=`rwPendingBase`=`rwPendingAsyncBase`=0.
   - If `rwDMAWaited`(u16)!=`D_004E284A`(u16): both are set to 0, and it walks the ring from the old Waited index to the old 284A index
     (wrapping at `rwDMAPURefSize`). For each entry e=`rwDMADispatchEnd`[k]: n=*(e+4), then *(e+4)=0, then while n>0 it calls
     `_rwDMAAddPkt(e, (min(n,0xFFFF)<<12)|0x42)` and does n−=0xFFFF.
   - If `rwPendingCtrlPtr`[byte `rwDMADmaBufs+4`] != `rwDMABufTopPtr`, it calls `_rwDMAForceBufferSwap()`.
   - **Busy-wait** [C 0x1F3EC8..0x1F3EF4] while byte 0x4E2830 (`rwDMAPktBase`) or byte 0x4E27CF is non-zero. Only the ISRs clear these
     (§0), so a host stub must have them at 0 or skip the loop.
   - If `rwDMAOwnGIF`: DisableDmac(2) and `rwDMAOwnGIF`=0. If `rwDMAOwnVIF1`: DisableDmac(1) and it is set to 0. If `rwDMAOwnVSYNC`: DisableIntc(2) and it is set to 0.
   - RemoveDmacHandler(2, `rwDMAHandlerGIFHid`), then that id = −1. RemoveDmacHandler(1, `rwDMAHandlerVIF1Hid`), then = −1.
     RemoveIntcHandler(2, `rwVSYNCHandlerHid`), then = −1.
   - (`_rwDMADmaClose` afterwards: `rwDMAVIFGate`=0, free(`rwDMAbuf`) if `rwDMAbufSize`==0, then `rwDMAbuf`=`rwDMAbufSize`=`rwDMAHYieldFunc`=0 and
     `rwDMAMinVsyncCnt`=0.)
3. Readers: the same DMA-state readers as §4 (`_rwDMAOpenVIFPkt` fg=66, `_rwDMAForceBufferSwap` fg=2, `func_001F43B0` fg=3, `_rwDMAAddPURef`
   fg=1). The Own*/Hid globals are driver-only (Hook/Unhook).
4. Hardware: if D8_CHCR 0x1000D000 & 0x100 (STR): D_PCR 0x1000E020=0x100 (CPC8), sync, sync.p, then it spins on `bc0f` (COP0 condition) until
   SPR-from DMA is done. Kernel: DisableDmac(2/1), DisableIntc(2), RemoveDmacHandler(2/1), RemoveIntcHandler(2).
5. Callees: `_rwDMAClosePkt` 0x1F1F50 (driver; writes the cursor/pending globals, D8 SPR DMA, D_STAT/D_PCR), `_rwDMAAddPkt` 0x1F2620 (driver;
   writes BufTop/BufDataPtr/AsyncTop/SPR*/Pending*; D_PCR/bc0f), `_rwDMAForceBufferSwap` 0x1F2408 (driver; swaps halves: writes `farFogPlane`,
   `_gOldRasterShowRasterFunc`, Buf* ptrs, `rwDMADmaBufSize`, byte DmaBufs+4, `rwDMADispatching`, `rwDMAFlipSt`, SPR*/Pending*; di/mfc0 Status/ei,
   InvalidDCache, calls `rwDMAHandler` directly and `rwDMAHYieldFunc` via jalr).

---

### 8. SkyFinalizeStart 0x1FC928 callees
SkyFinalizeStart itself [C]: it returns s0 = (RasterFinaliseStart && CreateFrameBufferRasters). On failure it calls `_rwSkyUnOverloadPipelines`.
On success it calls `_rwMatrixSetMultFn(func_001FFE18)` and `_rwVectorSetMultFn(func_001FFE90, func_001FFEF0, func_001FFF80,
func_001FFFD8)`. Always: `skyProj` 0x675E00 = identity-ish (+0=+0x14=+0x28=1.0f; +4,+8,+0x10,+0x18,+0x20,+0x24,+0x30,+0x34,+0x38=0;
+0xC |= 0x20003), and `skyProjState` 0x4E2870=0xDEADDEAD. Both are read only by SkyCameraBeginUpdate.

#### 8a. `_rwSkyOverloadPipelines` 0x20B1E8 (0x50)
1. `void f(void)`. v0 = whatever `initVu1DispatchStuff` leaves [C].
2. `ourGlobals` 0x670BD0 +0x28=func_0020B240, +0x2C=func_0020B298, +0x30=func_0020B2F8, +0x34=func_0020B368. These are
   dOpenDevice.fpIm2DRenderLine/Triangle/Primitive/IndexedPrimitive [L, rw35 RwGlobals/RwDevice layout].
3. Readers: none found by symbol in game or core (the RwIm2D* macros would read them; no `%lo(ourGlobals+0x28..0x34)` load exists) [C for absence].
4. —  5. `initVu1DispatchStuff` 0x2089B0 (driver; memory + SyncDCache). It writes (qword words listed low to high) [C 0x2089B4..0x208D14]:
   - `prologHeader3D128` 0x49AFD0 {0x1000000D cnt13, 0, 0, 0x6C0D03F0 UNPACK V4-32 n=13 @0x3F0}; `prologHeader2D128` 0x49AFC0
     {0x10000003, 0, 0, 0x6C0303F9}; `gifTag128` 0x49AE50 {lo 0x3000400000000001 = NLOOP=1 EOP=0 PRE=1 PRIM=0 PACKED NREG=3, hi 0x412 =
     ST,RGBAQ,XYZF2}; `dma1End128` 0x49AE20 {0x10000001,0,0,0}; `dma1Ret128` 0x49AE40 {0x60000001,…0}; `dma1Header128` 0x49AE30
     {0x10000001,…}; `dma10Header128` 0x49AE10 {0x1000000A,…}; `runCmd128` 0x49AEA0 {0x11000000 FLUSH, 0x15000000 MSCAL 0, 0, 0};
     `contCmd128` 0x49AE00 {0x10000000 FLUSHE, 0x17000000 MSCNT, 0, 0}; `flushCmd128` 0x49AF80 {0, 0, FLUSH, 0}; `dmaEnd128` 0x49AF90
     {0x10000000,0,0,0}; `baseOffsetCmd128` 0x49AF70 {0x03000000 BASE 0, 0x020001E1 OFFSET 0x1E1, 0, 0}; `zero128` 0x49AEC0 = 0;
     `maskCmd128` 0x49AE70 {FLUSH, FLUSH, FLUSH, 0}; `unmaskCmd128` 0x49AEB0 {FLUSH, 0, 0, 0}.
   - `xMaxYMax128` 0x49AF30: f[0] = W/(K−(W>>1)) and f[1] = H/(K−(H>>1)), where W,H = `skyVideoMode`(=&customMode)->draw.width/height and K = `D_004E0718`
     = 2047.9374f (words 2,3 are not written). `colScaleTexCmd128` 0x49AFB0 = 4×`D_004E071C` (0.502f). `colScaleNoTexCmd128` 0x49AFA0 =
     {`D_004E0720`=1.0000392f ×3, 0.502f}.
   - `_rwDMAInPkt.81` 0x4E18EC = 39 and `maxCopyTriPacketSize` 0x4E18E8 = 39. Each is the largest n with 3·⌊(n+16)/16⌋+32+12(n+1) < 0x1FF,
     with the loops at 0x208C20/0x208C60 [C].
   - `nullLightBlock` 0x49AE80 = {0x10000001, 0, 0x01000404 STCYCL 4/4, 0x6C0103D4 UNPACK V4-32 n=1 @0x3D4}, +0x10..+0x1F=0.
     Then SyncDCache(nullLightBlock, +0x9F).
   - Readers: all driver-only (VU1 dispatch: openVU1SetupPkt(New), closeAndDispatchVU1Packet, addCopyVU1*, func_00208120/00209788/0020A6A8).
     **Exception: `xMaxYMax128` is read by game `Construct__13CB3CameraDataFP8RwCamera` (lqc2)**, and `_rwSkyPipeSetupForCamera` rewrites it.

#### 8b. `_rwSkyRasterFinaliseStart` 0x205D38 (0x6C)
1. `RwBool f(void)`. It returns 1 if both rasters are created, else 0 [C].
2. `skyCurRas` 0x4E2890=0; `blitRaster` 0x4E2898 = RwRasterCreate(0,0,0,0x80); if that is NULL it returns 0. `blitRaster+4` = RwRasterCreate(0,0,0,0x80);
   if that is NULL it calls RwRasterDestroy(blitRaster[0]), sets `blitRaster`=0 and returns 0. Otherwise `rasterNum` 0x4E28A0=0 and it returns 1. (0x80 = rwRASTERDONTALLOCATE [L].)
3. Readers: driver-only (`_rwSkyRasterRender*`, `_rwSkyRasterInitiateStop`, `_rwSkyRasterClear*`, spriteSetup, func_00205E30).
4. —  5. RwRasterCreate/RwRasterDestroy (RW core, which reaches the driver SkyRasterCreate).

#### 8c. `_rpSkyCreateFrameBufferRasters` 0x1FEB78 (0xD4)
1. `RwBool f(void)`. It returns 0 if `engineStarted`==0, else 1. It does not NULL-check the created rasters [C].
2. Writes, with F=`_rwDMAFlipData` [C]: `skyFakeCacheFrameBuffer0`(0x675F48)+4 = (F+0x60 & 0x1FF)<<11; `…FrameBuffer1`(0x675F58)+4 = (F+0x1D0 & 0x1FF)<<11;
   `skyFakeCacheDisplayBuffer0`(0x675F68)+4 = (F+0x10 & 0x1FF)<<11; `…DisplayBuffer1`(0x675F78)+4 = (F+0x38 & 0x1FF)<<11; the +0 of all four = 0.
   Then `skyDrawRaster` 0x4E1874 = SkyCreateFBRaster(), `skyDisplayRaster` 0x4E1870 = SkyCreateDBRaster(), then `_rpSkyFrameBufferRastersUpdate()`.
   - `SkyCreateFBRaster` 0x1FE7A8: r=RwRasterCreate(draw.w, draw.h, draw.depth, draw.format|2); r+0x20 (cType, u8)=0x85; r+0xC/r+0x10 are rounded
     up to a power of 2 (written only if they were not already a power of 2). With x = r+`skyRasterExt`: x+8 = (FRAME_1(`skyFrame_1`).PSM<<20)|(FBW<<14)|(log2 w<<26)|(log2 h<<30),
     x+0xC = (log2 h & 0xF)>>2, x+0x17=1, x+0x16=0.
   - `SkyCreateDBRaster` 0x1FE988: the same with display.* fields (format+0x2C|2) and the PSM/FBW taken from the DISPFB at F+0x10 (bits 15-19 / 9-14). It sets `bz5120`
     0x4E1814 (u8)=1 around the RwRasterCreate call (read by SkyRasterCreate) and then sets it back to 0.
   - `_rpSkyFrameBufferRastersUpdate` 0x1FE678 picks the fake-cache pair by `skyFrameBit`&1 (1 gives the *0 pair, 0 gives the *1 pair). Then, for each raster (display, then draw):
     with x=r+skyRasterExt, if x+0x58!=0 it clears *(x+0x58)+0 and x+0x58. Then v=fake+4 >> 6; x+8 = (x+8 & 0xFFFFC000)|(v&0x3FFF) (TBP0);
     x+0xC = (x+0xC & 0xFFF8001F)|(((v + x+0x10)&0x3FFF)<<5) (CBP); fake+0 = raster; x+0x58 = &fake.
3. Readers: `skyDrawRaster`/`skyDisplayRaster` are read by the driver (Destroy/Update) and by unreferenced getters `func_001FF850`/`func_001FF848`
   (no static references at all [C]). The fake caches are driver-only. The raster ext fields are read by game CGtTextureCache via `skyRasterExt`
   (they reach these rasters only if they get the camera raster pointer).
4. —  5. RwRasterCreate (core, reaching driver SkyRasterCreate), SkyCreateFBRaster/SkyCreateDBRaster/`_rpSkyFrameBufferRastersUpdate` (driver, memory only).

#### 8d. RW core setters
- `_rwMatrixSetMultFn` 0x1DAB10: `ourGlobals[matrixModule.off + 8]` = a0 (or func_001D8BF0 if NULL); returns 1. Read by core func_001DA3B0/func_001DAAF0 (the matrix-multiply path).
- `_rwVectorSetMultFn` 0x1DE550: `ourGlobals[vectorModule.off + 8/0xC/0x10/0x14]` = a0..a3 (defaults func_001DE7C0/5A8/EA28/E858). These are read by core
  RwV3dTransformPoint(+8)/Points(+0xC)/Vector(+0x10)/Vectors(+0x14) via jalr [C]. No direct game `jal` to those four was found.

---

## Sony library callees

## Sony SDK (2.8.0) functions called by sky2 start-up: exact guest effects for host stubs

Source: retail asm `decomp-snap/native2/assembly/asm/sce/{libgraph,libdma,libkernl}.s`, data
`asm/data/sce/libgraph.{data,rodata}.s`, `libmpeg.data.s` (holds `dch`). Tags: [C] read from the asm at the cited
address, [L] likely, [G] guess. SDK 3.0.3 `src/lib/graph/graphdev.c` and `src/lib/dma/libdma.c` agree with the asm
except where noted (the DTV480P branch, the GS-version-1 branch of `sceGsPutDispEnv`, and the `_GetGsDxDyOffset` path
are missing from the 3.0.3 source).

### 0. libgraph internal state: `gp.15` (sceGsGParam) @ 0x00483F20, 0x10 bytes

| off | type | SDK name | initial (.data) | written by | read by |
|---|---|---|---|---|---|
| 0x0 | s16 | sceGsInterMode | 1 | ResetGraph (mode 0/5) = `inter` as passed (whole short, not `&1`) | DefDispEnv, zbufaddr, SyncV |
| 0x2 | s16 | sceGsOutMode | 2 | ResetGraph = `omode` as passed (whole short) | DefDispEnv |
| 0x4 | s16 | sceGsFFMode | 1 | ResetGraph = `(ffmode != 0)` | DefDispEnv, zbufaddr |
| 0x6 | s16 | sceGsVersion | 3 | ResetGraph = `(GS_CSR >> 16) & 0xFF` (CSR.REV) | PutDispEnv (`== 1` test) |
| 0x8 | int | sceGsVSCfunc | 0 | ResetGraph: set 0 only if it was nonzero | ResetGraph, SyncV |
| 0xC | int | sceGsVSCid | 0 | ResetGraph: set 0 only if +8 was nonzero | ResetGraph |

[C] data at 0x483F20: `.word 0x00020001, 0x00030001, 0, 0`. [C] `sceGsGetGParam` (0x102510) just returns 0x483F20.

Readers (`xref.py gp.15` + `jal sceGsGetGParam`): **Sony-lib-internal only** [C]. The only %hi/%lo reference is
`sceGsGetGParam`; its only callers are `sceGsResetGraph` (x2), `sceGsDefDispEnv`, `sceGsPutDispEnv`, `sceGszbufaddr`,
`sceGsSyncV`, all in libgraph. No RW or game code reads it. `sceGsSyncVCallback` (the SDK's only writer of +8/+C)
is not linked, so +8/+C stay 0 for the whole run [C: no other writer], and the `VSync2` / `DisableIntc` /
`RemoveIntcHandler` paths below can never run in retail.

**Host-stub consequence:** a stub only has to keep a private gparam {inter, omode, ffmode, version}. No guest reader
exists outside libgraph, so if every libgraph entry point is stubbed, the guest copy at 0x483F20 never needs updating.
For fidelity it is cheap to write the four shorts anyway.

---

### 1. `sceGsResetGraph(short mode, short inter, short omode, short ffmode)` @ 0x00102380

Args: a0..a3, each sign-extended from 16 bits (0x102384-0x1023BC) [C]. Returns void (v0 is junk) [C].

| mode | effect [C] |
|---|---|
| 0 (0x1023EC) | 1. `*(u64*)GS_CSR(0x12001000) = 0x200` (CSR.RESET). 2. gp+0 = inter, gp+2 = omode. 3. read GS_CSR back: gp+6 = `(CSR>>16)&0xFF`. 4. `GsPutIMR(0xFF00)`. 5. gp+4 = `ffmode != 0`. 6. if gp+8 != 0 (never in retail): `DisableIntc(2)`, `RemoveIntcHandler(2, gp+0xC)`, gp+0xC = 0, gp+8 = 0. 7. tail-jump `SetGsCrt(inter&1, omode&0xFF, ffmode&1)`. |
| 1 (0x102480) | `*(u64*)GS_CSR = 0x100` (CSR.FLUSH) and nothing else. |
| 5 (0x102498) | Steps 2, 3, 5, 7 of mode 0: no CSR reset, no IMR write, no handler removal. |
| other | nothing |

Guest memory writes: gp.15 +0/+2/+4/+6 (s16 each), +8/+C (unreachable). No out-struct.

Hardware / syscalls:
- GS_CSR 0x12001000: write 0x200 (reset) or 0x100 (flush); 64-bit read for REV (bits 16-23) [C].
- `GsPutIMR` (0x10C160) = syscall **0x71**, a0 = 0xFF00: sets GS IMR (0x12001010) to mask all GS interrupts
  (SIGMSK/FINISHMSK/HSMSK/VSMSK/EDWMSK, bits 8-14, plus bit 15) [C syscall number; L meaning].
- `SetGsCrt` (0x10BA20) = syscall **0x02**: the BIOS programs the CRTC (SMODE1, SYNCH1/2, SYNCV, SMODE2, SRFSH) for
  (interlace, mode, ffmode) [C number; L meaning]. Nothing in guest RAM that game/RW reads.
- `DisableIntc(2)` (0x10C900: DIntr, syscall 0x15 `_DisableIntc`, EIntr) and `RemoveIntcHandler` = syscall 0x11:
  unreachable (above).

Retail callers: `SkyStart` (3 call sites, 0x1FD8E0/0x1FD904/0x1FD91C) and `RpSkyResume` (3) [C], all with mode 0:
- `customMode+0x32 == 0x50` -> `(0, 0, 0x50, 1)`
- else flags = `customMode+0x24`: `&4` -> `(0, 1, omode, 1)`; else `&2` -> `(0, 1, omode, 0)`;
  else `&0x100` -> `(0, 1, omode, 1)`; else `(0, 0, omode, 1)` (omode = byte `customMode+0x32`) [C 0x1FD894-0x1FD920].

Stub: store {inter, omode, ffmode!=0, version} into gp.15. Set version = the GS REV your host reports. It must not be 1,
or `sceGsPutDispEnv` takes the old-GS branch. Real GS REV is not 1 [G]; PCSX2 reports 0x1B [L]. Then switch the host
video mode.

### 2. `sceGsDefDispEnv(sceGsDispEnv *disp, short psm, short w, short h, short dx, short dy)` @ 0x00102638

Args: a0 = disp, a1 = psm, a2 = w, a3 = h, t0 = dx, t1 = dy (shorts) [C 0x10263C-0x102680]. Returns void.
Reads gp+0 (inter), +2 (omode), +4 (ffmode).

Display offsets: locals `off[0..3]` at sp+0..0xC.
- If omode is 2 or 3 (`(omode-2) < 2u`, 0x102694): all four are 0.
- Otherwise `checkModelVersion()` (0x102588) runs. It does `sceOpen("rom0:ROMVER")`, reads one byte at a time up
  to the NUL (at most 256), `sceClose`, then `atoi(buf + len - 9) > 20010608` (0x1315670).
  - If that is nonzero, call `_GetGsDxDyOffset(omode, &off0, &off1, &off2, &off3)` = syscall **0x80** (0x102628) [C].
    The BIOS fills the four offsets. Only off0 (DX) and off1 (DY) are used.
  - If `sceOpen` fails, it returns -1, which also counts as true [C 0x1025B0-0x1025B8].
  - If it is zero, all four offsets are 0.

Out-struct writes (all 64-bit `sd`) [C]:

| off | reg | value |
|---|---|---|
| 0x00 | PMODE | `0x66` (EN1=0 EN2=1 CRTMD=1 MMOD=1 AMOD=1 SLBG=0 ALP=0) (0x1026EC) |
| 0x08 | SMODE2 | inter==0 -> `2`; inter!=0 && ffmode!=0 -> `3`; inter!=0 && ffmode==0 -> `1` (INT bit0, FFMD bit1) (0x1026F0-0x10270C) |
| 0x10 | DISPFB2 | `(((w+63)>>6) & 0x3F) << 9  \|  (psm & 0xF) << 15`; FBP=DBX=DBY=0 (0x102710-0x10273C) |
| 0x18 | DISPLAY2 | depends on omode (below); **not written** for an unsupported omode |
| 0x20 | BGCOLOR | `0` (0x102980) |

DISPLAY formulas. The field layout is DX[0:11], DY[12:22], MAGH[23:26], MAGV[27:28], DW[32:43], DH[44:54]. MAGH,
DW and DH are not masked. `magh = (w + 0x9FF) / w` is a signed int division (`(2559+w)/w`). Here `inter` means
`gp+0 == 1` exactly (not just nonzero).

| omode | case | DX (masked 0xFFF) | DY (masked 0xFFF) | MAGH field | DW | DH |
|---|---|---|---|---|---|---|
| 2 NTSC | inter==1 (0x102754) | `dx*magh + off0 + 0x27C` | `dy + off1 + 0x32` | `magh-1` | `magh*w-1` | ffmode? `2h-1` : `h-1` |
| 2 NTSC | inter!=1 (0x1027BC) | `dx*magh + off0 + 0x27C` | `dy + off1 + 0x19` | `magh-1` | `magh*w-1` | `h-1` |
| 3 PAL | inter==1 (0x102808) | `dx*magh + off0 + 0x290` | `dy + off1 + 0x48` | `magh-1` | `magh*w-1` | ffmode? `2h-1` : `h-1` |
| 3 PAL | inter!=1 (0x102890) | `dx*magh + off0 + 0x290` | `dy + off1 + 0x24` | `magh-1` | `magh*w-1` | `h-1` |
| 0x50 DTV480P (0x102904) | any inter/ffmode | `((720-w)/2)*2 + off0 + 2*dx + 0xE8` (`/2` rounds toward 0) | `dy + off1 + 0x23` | `1` (constant 0x800000) | `2w-1` | `h-1` |
| other | `scePrintf("sceGsDefDispEnv:Not support displaymode for 0x%x!!\n", omode)` (0x102978); +0x18 untouched | | | | | |

For omode 2/3, off0 = off1 = 0 always (zeroed above). MAGV is always 0.

Worked values (psm 0 PSMCT32, w = 640, dx = dy = 0; `magh` = 3199/640 = 4):

- DISPFB = **0x0000000000001400** (FBW 10). PMODE = 0x66, BGCOLOR = 0 in both cases.
- **(a) omode 2, inter 1, ffmode 1 (FRAME)**: SMODE2 = **3**. DX = 636 (0x27C), DY = 50, MAGH field = 3, DW = 2559.
  - h = 224: DH = 447 -> DISPLAY = **0x001BF9FF0183227C** (the usual 448-line interlaced frame-mode set-up)
  - h = 448: DH = 895 -> DISPLAY = **0x0037F9FF0183227C** (DH 895 is double height, so frame mode expects h = 224
    per field buffer)
  - For comparison, ffmode 0 (FIELD): SMODE2 = 1; h = 448 -> 0x001BF9FF0183227C, h = 224 -> 0x000DF9FF0183227C.
- **(b) omode 0x50, inter 0, ffmode 1, h = 448**: SMODE2 = **2**. Assuming off0 = off1 = 0: DX = 80+232 = 312
  (0x138), DY = 35, MAGH field = 1, DW = 1279, DH = 447 -> DISPLAY = **0x001BF4FF00823138**.
  - Caveat [G]: on a BIOS whose ROMVER date is later than 2001-06-08 (every slim/late console, and any emulator
    running a late BIOS), the syscall 0x80 values are added to DX/DY. I did not find their value in this binary. It
    is BIOS data, probably 0 on a stock console [G]. A host stub should use 0 (the same result as the old-BIOS path)
    unless it wants to imitate a specific BIOS.

Hardware / syscalls: syscall 0x80 (only for omode not 2/3); IOP file I/O via `sceOpen`/`sceRead`/`sceClose`
(checkModelVersion, same condition). No register writes.

RW caller (SkyStart 0x1FD994, 0x1FD9C4) [C]:
`sceGsDefDispEnv(&_rwDMAFlipData[0x00 / 0x28], (customMode+0x20 == 16) ? 0xA : 0, (s16)customMode+0x18, (s16)customMode+0x1C, 0, 0)`.
`_rwDMAFlipData` = 0x006754C0 (the sceGsDBuffDc at its start: disp[0] +0x00, disp[1] +0x28).

Readers of the written DISPLAY words (`xref.py _rwDMAFlipData`):
- **Game** `Construct__15CGtScreenCentreFv` (0x2213F0) reads `_rwDMAFlipData+0x18` (disp[0].display) and pulls out
  DX (`&0xFFF`) and DY [C 0x2213FC-0x221410].
- **Game** `MoveScreen__15CGtScreenCentreFff` (0x2210A0) reads and rewrites +0x18 and +0x40 (disp[1].display).
- So DX/DY must match retail exactly, or screen-centre maths drift.
- **RW**: `rwDMAVSYNCHandler` (0x1F1CE8) passes disp[] to `sceGsPutDispEnv` every vsync (0x1F1D84, 0x1F1DA0). Other
  sky2 functions also read `_rwDMAFlipData` (SkyRasterShowRaster, SkyCameraClear, _rpSkyCreateFrameBufferRasters, ...).

### 3. `sceGsSetDefDrawEnv(sceGsDrawEnv1 *draw, short psm, short w, short h, short ztest, short zpsm)` @ 0x00102B38

Args: a0 = draw, a1 = psm, a2 = w, a3 = h, t0 = ztest, t1 = zpsm [C 0x102B3C-0x102BA4]. **Returns 8** (0x102CF8).

Out-struct writes, 64-bit, at 0x00..0x78. The struct holds value/address pairs, which is an A+D GIF payload of 8 qwords.

| off | value [C] |
|---|---|
| 0x00 | FRAME_1 = `((((w+63)>>6) & 0x3F) << 16) \| ((psm & 0xF) << 24)`; FBP = 0, FBMSK = 0 |
| 0x08 | `0x4C` (FRAME_1) |
| 0x10 | ZBUF_1 = `(s64)(s16)zbp \| ((zpsm & 0xF) << 24) \| (ztest==0 ? 1ull<<32 : 0)` (ZMSK = 1 when ztest == 0); zbp = `sceGszbufaddr(psm, w, h)`, sign-extended and not masked to 9 bits (0x102BC0-0x102C10) |
| 0x18 | `0x4E` (ZBUF_1) |
| 0x20 | XYOFFSET_1 = `((0x800 - (w>>1)) << 4) \| ((0x800 - (h>>1)) << 36)` (64-bit, `>>` arithmetic) |
| 0x28 | `0x18` |
| 0x30 | SCISSOR_1 = `((w-1) << 16) \| ((u64)(h-1) << 48)`; SCAX0 = SCAY0 = 0 |
| 0x38 | `0x40` |
| 0x40 | PRMODECONT: **read-modify-write** `old \| 1` (AC = 1) (0x102C48, 0x102C98) |
| 0x48 | `0x1A` |
| 0x50 | COLCLAMP: **read-modify-write** `old \| 1` (CLAMP = 1) |
| 0x58 | `0x46` |
| 0x60 | DTHE: **read-modify-write** `(psm & 2) ? old \| 1 : old & ~1` (0x102C88-0x102CC4) |
| 0x68 | `0x45` |
| 0x70 | TEST_1 = ztest ? `((ztest & 3) << 17) \| 0x10000` : `0x30000` (ZTE = 1, ZTST = ztest or ALWAYS) |
| 0x78 | `0x47` |

Then `sync`. The three RMW words keep whatever bits were already in guest memory (bitfield stores in the SDK), so a
stub must read-OR-write them too, not store 1.

#### Callee `sceGszbufaddr(short psm, short w, short h)` @ 0x00102A70 [C]

- `nw = (w+63)/64` and `nh = (psm & 2) ? (h+63)/64 : (h+31)/32` (C signed division; the `movn` selects +126 / +62
  for negative input).
- `ld gp+0` is ANDed with 0x0000FFFF0000FFFF and compared with 1, so the test is `inter == 1 && ffmode == 0` (FIELD).
- If that holds it returns `(short)(nw*nh)`, otherwise `(short)(2*nw*nh)`.
- No writes.

Worked values (psm 0, w = 640, FBW 10, FRAME_1 = **0x00000000000A0000**, XYOFFSET OFX = 0x6C00):

| case | zbp | XYOFFSET_1 | SCISSOR_1 |
|---|---|---|---|
| (a) inter 1, FRAME, h = 224 | 2*10*7 = **140 (0x8C)** | **0x0000790000006C00** | **0x00DF0000027F0000** |
| (a) inter 1, FRAME, h = 448 | 2*10*14 = **280 (0x118)** | **0x0000720000006C00** | **0x01BF0000027F0000** |
| (b) inter 0, h = 448 | **280 (0x118)** | 0x0000720000006C00 | 0x01BF0000027F0000 |

- Only inter 1 + FIELD gives nw*nh (70 for h = 224, 140 for h = 448).
- ZBUF_1 with zpsm 0x31 (PSMZ24) and ztest 2: `zbp | 0x01000000`, e.g. 0x0000000001000118. With ztest 0 add
  0x100000000.
- TEST_1: ztest 2 (GEQUAL) -> 0x50000; ztest 0 -> 0x30000.
- DTHE: psm 0 -> bit 0 cleared.

RW callers (SkyStart 0x1FD9FC -> `_rwDMAFlipData+0x60`, 0x1FDA64 -> +0x1D0) [C]: psm = `(customMode+8 == 16) ? 0xA : 0`,
w = `(s16)customMode+0`, h = `(s16)customMode+4`, ztest = **2**, zpsm = `(zBufferDepth == 16) ? 0x3A : 0x31`.
Readers: RW sky2 (SwapDBuffDc DMA, SkyCameraClear, SkyCameraBeginUpdate, RpSkyTexCacheRestore, ... `ld` on the flip
data) and game `PresentDrawBuffer__18CB3GraphicsManagerFv` (writes into flip data) [C xref].

No hardware access, no globals written.

### 4. `sceGsSetDefDrawEnv2(sceGsDrawEnv2 *draw, psm, w, h, ztest, zpsm)` @ 0x00103988

Same as section 3 except [C]:
- **FRAME_2 = `(((w>>6) & 0x3F) << 16) | ((psm&0xF) << 24)`, with no +63 rounding** (0x1039A4 `sra a2,a2,22`).
  The result is identical to FRAME_1 when w is a multiple of 64 (640 -> 0x000A0000).
- Address words: 0x08 = `0x4D` (FRAME_2), 0x18 = `0x4F` (ZBUF_2), 0x28 = `0x19` (XYOFFSET_2), 0x38 = `0x41`
  (SCISSOR_2), 0x48 = 0x1A, 0x58 = 0x46, 0x68 = 0x45, 0x78 = `0x48` (TEST_2).
- The other values, the RMW words, `sceGszbufaddr`, and the return value 8 are all the same as section 3.

RW callers: SkyStart 0x1FDA30 -> `_rwDMAFlipData+0xE0` (draw02), 0x1FDA98 -> +0x250 (draw12), with the same
arguments as section 3.

### 5. `sceGsSyncV(int mode)` @ 0x00102E08

- `mode` is unused [C].
- gp+8 == 0 is always true in retail, so it calls `VSync()` (0x10C2B0).
- Return value [C 0x102E34-0x102E8C]:
  - If gp+0 == 1, it returns `(GS_CSR >> 13) & 1` (CSR.FIELD after the vblank), sign-extended to 64 bits.
  - Otherwise it **returns 1**. The literal 1 used for the compare is still in v0 at return (0x102E38).
  - The VSync2 path returns the same: `(VSync2() >> 13) & 1` if interlaced, else 1.
- No guest memory writes.

#### Callee `VSync` @ 0x0010C2B0 (libkernl, not a syscall) [C]
1. `DIntr()` (0x114E60: `di` if Status.EIE).
2. `*(u32*)INTC_STAT(0x1000F000) = 4`, which clears the VBON (vblank-start, bit 2) flag.
3. `sync`; `EIntr()` if interrupts were enabled.
4. Busy-wait until `INTC_STAT & 4`.
5. Clear bit 2 again with DIntr/EIntr around it.

The return value is junk. The unreachable `VSync2` (0x10C340) uses syscall 0x73 `SetVSyncFlag` and returns the CSR
that the kernel latched.

Hardware: INTC_STAT 0x1000F000 (write 4, poll bit 2); GS_CSR 0x12001000 64-bit read; COP0 Status (`mfc0 $12`),
`di`/`ei`.

Callers: RW `SkyStart`, `RpSkyResume` (0x1FE558); RW Audio `RwaRPCOpen` (x2); game `func_001D3D70` (x2),
`func_00227B80`, `func_0026F860` (x2) [C].

Stub: wait for (or simulate) one vblank. Return the field parity if interlaced, else 1.

### 6. `sceGsSyncPath(int mode, u_short timeout)` @ 0x00102EA0

`timeout` is unused (a1 is reused as the loop counter at 0x102EA4) [C]. No guest memory writes.

- **mode != 0** (0x103134) returns a busy bitmask without waiting [C]:
  - bit0: D1_CHCR(0x10009000) & 0x100 (VIF1 DMA STR)
  - bit1: D2_CHCR(0x1000A000) & 0x100 (GIF DMA STR)
  - bit2: VIF1_STAT(0x10003C00) & 0x1F000003 (VPS | FQC)
  - bit3: COP2 `cfc2 vi29` (VPU-STAT) & 0x100 (VBS1, VU1 busy)
  - bit4: GIF_STAT(0x10003020) & 0xC00 (APATH)
- **mode 0** polls the five conditions above in that order.
  - One shared counter covers all five; after more than 0x1000000 iterations it gives up.
  - On timeout it prints the matching string and returns -1:
    - 0x4B1738 "sceGsSyncPath: DMA Ch.1 does not terminate". splat mislabels this address as
      `__RTTI__Q23std13bad_exception`.
    - 0x4B1818 Ch.2, 0x4B1848 VIF1, 0x4B1878 VU1, 0x4B18A0 GIF.
  - The timeout path then dumps D1_CHCR/D1_TADR(0x10009030)/D1_MADR(0x10009010)/D1_QWC(0x10009020),
    D2_CHCR/D2_TADR(0x1000A030)/D2_MADR(0x1000A010)/D2_QWC(0x1000A020), VIF1_STAT and GIF_STAT.
  - It returns 0 once everything is idle.

RW callers: SkyStart (3, all `while (sceGsSyncPath(1, 0)) ;` loops at 0x1FD948, 0x1FDF38, 0x1FE088), RpSkyResume (1),
SkyRasterLock (4) [C].

Stub: finish (flush) the pending host-side GIF/VIF work, then return 0.

### 7. `sceGsSwapDBuffDc(sceGsDBuffDc *db, int id)` @ 0x00103B68

1. `i = id & 1`.
2. `sceGsPutDispEnv(db + i*0x28)`.
3. `return sceGsPutDrawEnv(i ? db+0x1C0 : db+0x50)`, so it returns 0, or -1 if the DMA times out [C].

No guest memory writes of its own. Layout (sceGsDBuffDc): disp[0] +0x00, disp[1] +0x28, giftag0 +0x50, draw01 +0x60,
draw02 +0xE0, clear0 +0x160, giftag1 +0x1C0, draw11 +0x1D0, draw12 +0x250, clear1 +0x2D0 [C from SkyStart's offsets
+ SDK header].

Callers: RW `SkyStart` (0x1FE07C, `(&_rwDMAFlipData, 0)`), `RpSkyResume` (0x1FE600) [C].

#### Callee `sceGsPutDispEnv(sceGsDispEnv *disp)` @ 0x001029B0 [C]

Reads gp+6 (s16), then makes 64-bit GS privileged-register writes. No RAM writes.

- **gp+6 == 1** (old-GS branch):
  - PMODE 0x12000000 = disp+0
  - DISPFB1 0x12000070 = disp+0x10
  - DISPLAY1 0x12000080 = disp+0x18
  - EXTDATA 0x120000C0 = disp+0x20 (BGCOLOR value)
  - SMODE2 is not written.
- **else**:
  - PMODE 0x12000000 = +0
  - SMODE2 0x12000020 = +8
  - DISPFB2 0x12000090 = +0x10
  - DISPLAY2 0x120000A0 = +0x18
  - BGCOLOR 0x120000E0 = +0x20

Also called by RW `rwDMAVSYNCHandler` (0x1F1D84, 0x1F1DA0).

#### Callee `sceGsPutDrawEnv(sceGifTag *giftag)` @ 0x00102D20 [C]

1. Poll D2_CHCR(0x1000A000) & 0x100. After more than 0x1000000 polls, print "sceGsPutDrawEnv: DMA Ch.2 does not
   terminate" (0x4B1708) and return -1.
2. D2_QWC(0x1000A020) = `(giftag.NLOOP (u64 word 0 & 0x7FFF)) + 1`.
3. D2_MADR(0x1000A010) = `addr & 0x0FFFFFFF`, with `| 0x80000000` if `(addr & 0x70000000) == 0x70000000`
   (scratchpad).
4. D2_CHCR = **0x101** (DIR = from memory, MOD = normal, STR). This is a full-word store, not RMW.
5. Return 0. It does not wait for completion.

Stub: feed the GIF tag plus NLOOP A+D qwords to the host GS (the draw env 1/2 and clear packets).

### 8. `sceDmaGetChan(int id)` @ 0x00103D20 [C]

Returns `id < 10u ? dch[id] : NULL`.
- `dch` @ 0x00483F70 (10 words, in `asm/data/sce/libmpeg.data.s` by splat boundary):
  0x10008000 (VIF0), 0x10009000 (VIF1), 0x1000A000 (GIF), 0x1000B000 (fromIPU), 0x1000B400 (toIPU),
  0x1000C000 (SIF0), 0x1000C400 (SIF1), 0x1000C800 (SIF2), 0x1000D000 (fromSPR), 0x1000D400 (toSPR).
- No writes. `dch` readers: `sceDmaGetChan`, `sceDmaReset` (Sony-internal) [C xref].

Callers:
- RW: SkyStart x2 (id 2), RpSkyResume, SkyRasterLock x4.
- Game: GtIntersection functions (IntersectLineTriangle, IntersectFrustum x2, IntersectLineNearest x2, IntersectLine,
  IntersectSphereTriangle, func_00272EA0/00273070/00273400/00274ED0), BeginVehicleRendering__18CB3VehicleRendererFv,
  BeginInstanceRender__16CB3TrackRendererF..., func_001618C0, func_0026AE30 [C].

Stub: return the same guest constant. Callers use it only as an opaque channel pointer passed back to sceDma*, or for
direct register pokes.

### 9. `sceDmaSend(sceDmaChan *d, void *tag)` @ 0x00104050 [C]

1. `a = CheckAddress(tag)` (0x103CC0): if `(tag >> 28) == 7` then `(tag & 0x0FFFFFFF) | 0x80000000`, else unchanged.
2. `WaitDma(d)` (0x103BC8):
   - Poll `d->chcr & 0x100`, counting down from 0xFFFFFF.
   - Once the count goes negative, every further poll prints "libdma: sync timeout\n" (0x4B1AA0) and calls
     `sceDmaPause(d)`.
3. `if (d->tadr (+0x30) != 0xFFFFFFFF) d->tadr = a` (SDK `setADR`).
4. `d->qwc (+0x20) = 0`.
5. `d->chcr (+0x00) = (chcr & ~0xC) | 0x105`: MOD = 1 chain, DIR = 1, STR = 1; the other bits are kept, as with the
   SDK bitfield store.

Returns void (v0 = the CHCR value written). `d` is always a hardware channel here, so every write goes to a
register; nothing goes to RAM.

RW caller: SkyStart 0x1FDF2C, `sceDmaSend(GIF ch, D_004B1400)`, which is a rodata DMA chain [C].

Stub: walk the DMA chain from `tag` and feed the GIF.

### 10. `sceDmaSendN(sceDmaChan *d, void *addr, int qwc)` @ 0x001040B8 [C]

1. CheckAddress, then WaitDma, both as in section 9.
2. `if (d->madr (+0x10) != 0xFFFFFFFF) d->madr = a`.
3. `d->qwc (+0x20) = qwc`.
4. `d->chcr = (chcr & ~0xC) | 0x101` (normal mode, DIR = 1, STR).

Hardware-only writes.

RW callers: SkyStart 0x1FD93C, `sceDmaSendN(GIF ch, skyInitialClear (0x675E40), 0xD)`; RpSkyResume 0x1FE61C [C].
SkyStart then itself writes D_STAT 0x1000E010 = 4 (0x1FD968: clears the CIS2 channel-2 interrupt status). That store
is RW code, not libdma.

Stub: send `qwc` qwords from `addr` to the host GIF.

### 11. `SyncDCache(void *start, void *end)` @ 0x0010C718 [C]

1. `ie = Status($12) & 0x10000` (EIE); if it is set, call `DIntr()`.
2. Call `_sceSDC(start & ~0x3F, end & ~0x3F)` (0x10C670). For every D-cache index `i` = 0..0xFC0 step 0x40 and both
   ways:
   - `cache 0x10` (DXLTG, index load tag), then `mfc0 $28` (TagLo).
   - `line = (TagLo & 0xFFFFF000) + i`.
   - If `start <= line <= end`: `cache 0x14` (DXWBIN, write back + invalidate).
   - `sync` around each op.
3. If `ie`, tail-jump to `EIntr()`.

Returns void (v0 junk). No syscall. No guest-visible memory change beyond making RAM coherent.

Callers:
- RW: 32 sites in renderware.s (SkyOpen 0x1FC874.., SkyStart 0x1FDEF4, 0x1FE068, ...); RW Audio rwa x16.
- Game: B3DebrisRendererPS2, B3PopcornRendererPS2, GtFileSystemPS2DVD x2, func_00228400, func_002516C0,
  unit_002558A0 x2, unit_00264170, unit_003076D0.
- EA dirtysock x6; Sony libdbc x2, libmc2 x7, libpad2 x2; ezNetCnf x1.

Stub: no-op.

---

### Uncertain points
- `_GetGsDxDyOffset` (syscall 0x80, 0x102628) values for DTV480P. They only matter if omode is not 2/3 and ROMVER is
  later than 20010608. The value is BIOS-defined and not in the ELF; assume 0 [G].
- GS CSR.REV value stored in gp+6. It is only tested `== 1` by `sceGsPutDispEnv`; use a value that is not 1 [G/L].
- ROMVER parsing assumes the file holds a NUL within 256 bytes; on EOF `sceRead` leaves the byte as stack garbage
  [L]. This does not matter for a stub.
