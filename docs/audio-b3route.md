# B3ROUTE.IRX (Criterion's distortion router, IOP side): behaviour spec

Lane audio1, 2026-11-06. Read-only study of the retail module (`perf/install/game/IOP/B3ROUTE.IRX`, 8,520 bytes, sha256
`00d686c6...`) and of its EE client `CB3SoundDistortion` in the decomp (read only). Helper
files (disc-derived, never committed) in a local `build/audio1/irx/`: `b3route_annot.s` (objdump
listing with imports, relocations and strings resolved) and `b3route_annot.py` (the script that makes it).

Addresses are module-relative (the module is relocatable from 0; 213 `.rel.text` entries). `irx_XXXXXXXX` are the
module's own functions (it has no symbols). SDK names only for SDK functions, resolved through the SDK 3.0.3 `.ilb`
files. Tags: **[C]** read from the code at the cited address, **[L]** likely (hardware or SDK semantics, or an
inference that fits all the code), **[G]** guess. libsd entries are decoded with `sdmacro.h` (entry = param | voice << 1
| core); sound-RAM addresses are libsd **byte** addresses, `w0x...` marks a 16-bit word address.

## 0. Summary

B3ROUTE does no signal processing. It is a **router** between the SPU2 and the EE [C, whole module]:
1. it sends the voices named by a 24-bit channel mask (core 0) to core 0's **wet (effect) bus only**;
2. every 256 samples it DMAs the last 256 stereo samples of core 0's wet-bus output ring out of sound RAM ("Clean")
   and sends them to the EE with SIF command 6;
3. the EE's command-6 handler (`func_002FC530`, game code) runs the distortion DSP (`CB3SoundDSPDataBlock::Process`
   twice, then a mono-sum pan) and DMAs a whole 512-sample stereo frame ("Fuzzy") back into B3ROUTE's IOP buffer;
4. every 256 samples (offset by 128) B3ROUTE writes that frame into **core 1's sound-data input (MEMIN)**, played at
   BVOL 0x7FFF through core 1's MMIX.

Its clock is the **SPU2 core-1 IRQ** cycling through four ring addresses (every 128 samples), hooked in front of
RWA.IRX's SPU2 interrupt handler. The EE talks to it by SIF **RPC** (server and function number 0x150276) with a
command list; it answers nothing. End-to-end latency of the distorted voices: **1024 samples (21.3 ms)** [L, §6.3].

## 1. The module [C]

- ELF type 0xFF80 (IRX), MIPS R3000, entry 0x560, gp 0x8E60; `.iopmod`: no module-info pointer (0xFFFFFFFF), text
  0xAB0, data 0x3C0, bss field 0, version 0, empty name. LOAD segment FileSiz 0xE70, **MemSiz 0x26E0** (bss
  0xE70-0x26DF). `.comment` "MW MIPS C Compiler (2.4.1.01)", "PlayStation2_IOP"; a `.line` (DWARF 1 line) section is
  present.
- Imports (stub address, library, index, SDK name):

| Stub | Import | Stub | Import |
|---|---|---|---|
| 0x014 | sysmem 14 `Kprintf` | 0x0EC | thbase 20 `GetThreadId` |
| 0x038 | intrman 8 `CpuDisableIntr` (unused) | 0x0F4 | thbase 24 `SleepThread` |
| 0x040 | intrman 9 `CpuEnableIntr` | 0x118 | libsd 5 `sceSdSetParam` |
| 0x048 | intrman 17 `CpuSuspendIntr` | 0x120 | libsd 6 `sceSdGetParam` |
| 0x050 | intrman 18 `CpuResumeIntr` | 0x128 | libsd 7 `sceSdSetSwitch` |
| 0x074 | loadcore 5 `FlushDcache` | 0x130 | libsd 9 `sceSdSetAddr` |
| 0x098 | sifcmd 13 `isceSifSendCmd` | 0x138 | libsd 10 `sceSdGetAddr` |
| 0x0A0 | sifcmd 14 `sceSifInitRpc` | 0x140 | libsd 11 `sceSdSetCoreAttr` |
| 0x0A8 | sifcmd 17 `sceSifRegisterRpc` | 0x148 | libsd 17 `sceSdVoiceTrans` |
| 0x0B0 | sifcmd 19 `sceSifSetRpcQueue` | 0x150 | libsd 19 `sceSdVoiceTransStatus` |
| 0x0B8 | sifcmd 22 `sceSifRpcLoop` | 0x158 | libsd 27 `sceSdSetSpu2IntrHandler` |
| 0x0DC | thbase 4 `CreateThread` | 0x160 | libsd 29 `sceSdGetSpu2IntrHandlerArgument` |
| 0x0E4 | thbase 6 `StartThread` | | |

  No hardware register is touched directly (no `lui 0xBF8x/0xBF9x`); no `sceSdInit`, `SetEffectAttr`, `BlockTrans`,
  `SetTransIntrHandler`, `AllocSysMemory` or semaphores.

### 1.1 Memory map (module-relative) [C: relocated references]

| Address | Size | What |
|---|---|---|
| 0x0E70 | 4 | the SPU2 interrupt handler that was installed before B3ROUTE's (RWA's) |
| 0x0E74 | 4 | the EE buffer the Clean blocks go to (set by command 0x8000) |
| 0x0E80 | 0x800 | **Fuzzy**: the EE's processed frame, 512 L samples then 512 R samples (s16) |
| 0x1680 | 0x600 | **CleanB** (the debug print's name), filled at phase A |
| 0x1C80 | 0x600 | **CleanA**, filled at phase C |
| 0x2280 | 0x400 | the RPC receive buffer (`sceSifRegisterRpc` buff) |
| 0x2680 | 4 | the server thread's id |
| 0x2684 | 4 | the SPU2 interrupt handler argument found at hook time (RWA's) |
| 0x26C0 | 0x18 | the command-6 packet (`sceSifCmdHdr` + 8 bytes never written, i.e. zero) |

The names CleanA / CleanB / Fuzzy are the module's own (`"Local CleanA[%x] CleanB[%x] Fuzzy[%x]"`, 0xBA4, printed
with 0x1C80 / 0x1680 / 0xE80 [C 0x3A0-0x3B8]).

## 2. Threads and start-up [C]

- **Entry 0x560** (module start): `sceSifInitRpc(0)`, `FlushDcache`, `CpuEnableIntr`, three Kprintf ("is 10:54",
  "routing module entry point", " gp_IC0 / gp_IF" with 0x1C80 / 0xE80 as values), then `CreateThread({attr 0x02000000,
  option 0, entry irx_0000045C, stack 0x800, priority 0x20})`, `StartThread(id, 0)`; returns 0 (resident) or 1 with
  "failed to start" if CreateThread fails.
- **Server thread `irx_0000045C`** (priority 0x20):
  1. `FlushDcache`, `CpuEnableIntr`, `CpuSuspendIntr`;
  2. **hooks the SPU2 interrupt**: `arg = sceSdGetSpu2IntrHandlerArgument()` (saved at 0x2684); `old =
     sceSdSetSpu2IntrHandler(irx_00000170, arg)` (saved at 0xE70); Kprintf "Replaced interrupt handler at %08x with
     data %08x" (old != 0) or "created interrupt handler" (old == 0); `CpuResumeIntr`;
  3. RPC: `sceSifInitRpc(0)`, `sceSifSetRpcQueue(&qd, GetThreadId())`, `sceSifRegisterRpc(&sd, 0x150276,
     irx_000002C0, buff 0x2280, cfunc 0, cbuff 0, &qd)`, `sceSifRpcLoop(&qd)`; then `SleepThread` forever.
- No other thread. All SPU2 work after set-up runs **inside the SPU2 interrupt handler** (`irx_00000170`), with
  `CpuSuspendIntr` around the IRQ re-arm.
- **Order against RWA** [C EE 0x1D4240 loop over D_0049A120: RWA.IRX is entry 7, B3ROUTE.IRX entry 8, loaded one after
  the other with `sceSifLoadModule`]; RWA installs its own handler at its module start (`irx_000074DC` -> 0x3E14 ->
  0x5E78 -> 0x5F88, RWA spec §1.1) [C RWA 0x771C, 0x3F94, 0x5EB0, 0x5FB8]. So B3ROUTE's "old" handler is
  **RWA's `irx_00005E20` with RWA's argument** [L: assumes each module's start completes before the next loads, as
  `sceSifLoadModule` returns after the start routine].

### 2.1 The interrupt handler `irx_00000170(core_bits, arg)` [C 0x170-0x2BC]
```
if (core_bits & 2):                                   /* core 1 */
    switch sceSdGetAddr(SD_A_IRQA | 1):
      0x3800: phase_A()   0x3900: phase_B()   0x3A00: phase_C()   0x3B00: phase_D()
      default: Kprintf("Warning, unknown address on B3dist core!"); old(core_bits, saved_arg)   /* no null check */
if (core_bits & 1):                                   /* core 0 */
    core_bits &= ~2
    if old: old(core_bits, arg) else Kprintf("B3DIST:NO INTERRUPT HANDLER!")
return 0
```
The "Got dual core interrupt" message (0xAFC) is unreachable: it tests bits 0 and 1 after bit 1 was cleared [C 0x280].
Core-0 IRQs therefore reach RWA's handler with bit 1 removed; core-1 IRQs at the four addresses never reach RWA.

## 3. The RPC server `irx_000002C0(fno, buf, size)` [C 0x2C0-0x458]

- Server id **0x150276** [C `lui 0x15 / ori 0x276` at 0x528/0x538]. The function checks `fno == 0x150276` and prints
  "Wrong func!" otherwise, but carries on [C 0x2DC]. (The EE passes the server id as the function number.)
- `buf` (0x400 bytes at 0x2280) = `{count, commands...}`: `count` commands, each an id word followed by its arguments,
  parsed in order (count not bounds-checked). Each one prints "%i/%i got command ID of %i".
- **Return value**: none set (v0 is whatever the last call left) [C]; harmless, the EE asks for no reply data
  (`rsize` 0).

| Id | Words | Action |
|---|---|---|
| **0x8000** init | 3: id, EE buffer, channel mask | `irx_00000630(mask)` (§4), then `[0xE74] = EE buffer`; three debug prints (buffer, mask; the four local addresses; "Meminterrupt A:3800 B:3900 C:3A00 D:3B00") |
| **0x8050** channels | 2: id, channel mask | `irx_00000630(mask)` |
| **0x8020** start | 1 | `irx_0000070C`: `CpuSuspendIntr`; `SetCoreAttr(SD_C_IRQ_ENABLE|1, 0)`; `SetAddr(SD_A_IRQA|1, 0x3800)`; `SetCoreAttr(SD_C_IRQ_ENABLE|1, 1)`; `CpuResumeIntr`; "started distortion" |
| **0x30** stop | 1 | `irx_00000768`: BVOLL/BVOLR core 0 = 0, BVOLL/BVOLR core 1 = 0 (entries 0x0F80, 0x1080, 0x0F81, 0x1081, in that order); `SetCoreAttr(SD_C_IRQ_ENABLE|1, 0)` (interrupts suspended); "Disabled interrupts on core 1" |
| **0x10** | 1 | `irx_00000704`: returns at once (no-op) |
| other | 1 | "Received unknown SIF cmd %i!", skips one word |

Neither start nor stop touches VMIX, MMIX or the transfer in flight; stop does not restore the voices' routing
(§4) and start does not restore BVOL [C]. So after a stop, a start alone leaves core 1's MEMIN at BVOL 0 (silent)
until the next 0x8000 / 0x8050 [C IOP; L that the EE's command order makes this matter, §7].

## 4. The routing set-up `irx_00000630(mask)` [C 0x630-0x700]

With `m = mask & 0xFFFFFF`, in this order:
1. `MMIX core 0 = GetParam(MMIX core 0)`, `MMIX core 1 = GetParam(MMIX core 1)`: written back unchanged (the value is
   only masked to 16 bits) [C 0x644-0x668]. With libsd's defaults that RWA leaves (core 0 0x0FF0, core 1 0x0FFC [L RWA
   spec §2.1]) core 1 hears its MEMIN dry and wet (MINL/MINR/MINEL/MINER), core 0 its own MEMIN too.
2. **BVOLL/BVOLR core 0 = 0** (0x0F80, 0x1080), **BVOLL/BVOLR core 1 = 0x7FFF** (0x0F81, 0x1081).
3. **VMIXL core 0 = ~m, VMIXR core 0 = ~m, VMIXEL core 0 = m, VMIXER core 0 = m** (entries 0x1800, 0x1A00, 0x1900,
   0x1B00; 24-bit). Masked voices go to core 0's wet bus only, all others to its dry bus only. Core 1's voices are
   untouched.

Consequences [L]: core 0's MEMIN (where RWA's own mixer stream plays, RWA spec §3.2, op 0x16) is muted from the init
command on; the masked voices reach the output only through the EE round trip (core 0's wet bus otherwise only feeds
core 0's reverb, whose return is at RWA's EVOL); the other core-0 voices lose their reverb send.

## 5. The clock: core-1 IRQ, four phases [C code; L period]

Start (0x8020) arms core 1's IRQ at **0x3800**. Each phase handler ends with the same re-arm (`CpuSuspendIntr`;
`SetCoreAttr(IRQ_ENABLE|1, 0)`; `SetAddr(IRQA|1, next)`; `SetCoreAttr(IRQ_ENABLE|1, 1)`; `CpuResumeIntr`), so IRQA
cycles **0x3800 -> 0x3900 -> 0x3A00 -> 0x3B00 -> 0x3800** forever until stop.

Byte 0x3800-0x3B00 = w0x1C00 / w0x1C80 / w0x1D00 / w0x1D80, inside core 1's wet-bus output ring w0x1C00-0x1DFF that the
SPU2 writes one word per sample [L SPU2 Overview 2.3, as modelled in spu2_mixer_impl.cpp; the address only matters as
a ring position]. So the phases fire at ring positions **0, 128, 256, 384**: one IRQ every **128 samples** (2.67 ms),
a full A-D cycle every 512 samples. Phases A / C coincide with RWA's core-0 IRQs (IRQA 0x800 / 0xA00 = positions 0 /
256), so those arrive with both core bits set; B3ROUTE runs first, then RWA's handler [C §2.1].

"Transfer idle" below = `sceSdVoiceTransStatus(0, SD_TRANS_STATUS_CHECK) == 1` [C; L that 1 = the last transfer on
channel 0 completed]. A transfer is skipped, not delayed, when the channel is busy; the re-arm happens anyway.

| Phase (IRQA) | Function | SIF send to the EE | Sound-RAM transfer (channel 0) |
|---|---|---|---|
| A 0x3800 (pos 0) | `irx_000007E0` | cmd 6: **CleanA** (0x1C80), 0x600 bytes, to `[0xE74]`; packet +0xC (opt) = **module + 0xE80** | if idle: **read** 0x600 bytes from **0x2A00** into **CleanB** (0x1680) |
| B 0x3900 (pos 128) | `irx_000008BC` | - | if idle: **write** 0x800 bytes from **Fuzzy** (0xE80) to **0x4800** |
| C 0x3A00 (pos 256) | `irx_00000944` | cmd 6: **CleanB** (0x1680), 0x600 bytes, to `[0xE74]`; opt = **module + 0xE81** | if idle: **read** 0x600 bytes from **0x2800** into **CleanA** (0x1C80) |
| D 0x3B00 (pos 384) | `irx_00000A24` | - | if idle: **write** Fuzzy -> 0x4800, 0x800 bytes |

The send is `isceSifSendCmd(6, pkt 0x26C0, 0x18, src, [0xE74], 0x600)` (interrupt-context form; the SIF DMA of the
0x600 bytes lands at the EE before its handler runs [L SIF semantics]); only +0xC of the packet is written by B3ROUTE,
the header's psize / dsize / daddr / fcode by sifcmd [L], +0x10..+0x17 stay zero. Sends are not conditional on
anything; reads are `sceSdVoiceTrans(0, SD_TRANS_MODE_READ, ...)`, writes `(0, SD_TRANS_MODE_WRITE, ...)` [C modes 1 /
0 at 0x858 / 0x8EC].

### 5.1 The sound-RAM areas [L layout; C addresses]

| Bytes | Words | What (SPU2 fixed areas; ps2tek "MEMIN", SPU2 Overview 2.3 as spu2_mixer_impl.cpp has them) |
|---|---|---|
| 0x2800-0x2BFF | w0x1400-0x15FF | core 0 wet-bus output, left (512-sample ring) |
| 0x2C00-0x2FFF | w0x1600-0x17FF | core 0 wet-bus output, right |
| 0x3800-0x3BFF | w0x1C00-0x1DFF | core 1 wet-bus output, left: used only as the IRQ timer |
| 0x4800-0x4BFF | w0x2400-0x25FF | core 1 MEMIN left (ring read one word per sample) |
| 0x4C00-0x4FFF | w0x2600-0x27FF | core 1 MEMIN right |

The 0x600-byte reads take three 256-sample quarters in one DMA: at A (from 0x2A00) left 256-511, right 0-255 (stale),
right 256-511; at C (from 0x2800) left 0-255, left 256-511 (stale), right 0-255. In both, **bytes 0-0x1FF are the
256 fresh left samples and 0x400-0x5FF the 256 fresh right samples** (the half the SPU2 has just finished writing);
0x200-0x3FF is stale and ignored by the EE [C EE: it reads +0 and +0x400 only, §6.1]. The writes replace the **whole**
core-1 MEMIN ring (both halves, both channels) twice per 512 samples. B3ROUTE's areas never contain RWA's IRQA (0x800 /
0xA00) and RWA's transfers never touch 0x3800-0x3BFF [L RWA spec §5: uploads go above 0x5000].

## 6. The EE side [C decomp, read only]

### 6.1 `CB3SoundDistortion` (B3SoundDistPS2.cpp; object at D_01E75640 + 0x5400 = 0x1E7AA40 [C 0x28B5D0])
- **Command queue**: +0x1900 is the 0x400-byte RPC send buffer, +0x1D00 points at its count word (+0x1900), +0x1D04 is
  the write cursor (from +0x1904); +0x1D10 the `sceSifClientData`. A command bumps the count once per command (not
  per word). **Flush**: if count != 0 and `sceSifCheckStatRpc(&cd) != 1`, `sceSifCallRpc(&cd, 0x150276, mode, +0x1900,
  0x400, 0, 0, 0, 0)`; then count = 0 and the cursor reset **whether or not the call was made** (a busy client drops
  the queued commands) [C StopDistortion 0x2FCF00, StartDistortion 0x2FCFC0, Construct 0x2FD0E0; Update 0x2FCA60 per its
  non-matching C].
- **Construct** (0x2FD0E0, called once from `func_0028B580` 0x28B5D8 during sound start-up, mask argument **0** [C
  0x28B5DC]): binds 0x150276 (spins until bound), `sceSifAddCmdHandler(6, func_002FC530, this)`, queues **{0x8000,
  this + 0x240, 0}** and flushes **blocking** (mode 0). So at boot B3ROUTE routes no voice to the wet bus, mutes core
  0's MEMIN and sets core 1's BVOL to 0x7FFF; the EE buffer is this + 0x240 = 0x1E7AC80.
- **SetChannels(mask)** (0x2FD090; from `CB3SoundESMHigh::Start` 0x287DC0, the mask built one bit per engine-sound
  voice with a DSP type [L]): only while not started; queues {0x8050, mask}; no flush (the next Update / Start / Stop
  sends it).
- **StartDistortion** (0x2FCFC0; called by `CB3SoundGameModeManager::Update` 0x289D3C every frame while its manager's
  count field +0xA0 > 0 [L "in a race"]): only if not started; queues 0x8020 and flushes **blocking**; marks started.
- **Update** (0x2FCA60, right after Start every frame): computes the pan gains +0x1840 / +0x1844 (16.16) from the
  listener and the car, then flushes whatever is queued, **non-blocking** (mode 1).
- **StopDistortion** (0x2FCF00; from the same Update when the count is 0): only if started; queues 0x30 and flushes
  non-blocking; marks stopped.
- The EE never sends 0x10.

### 6.2 The command-6 handler `func_002FC530(pkt, this)` [C, hand-written MMI asm in the decomp]
Interrupt context. Input: the 0x600 bytes at this + 0x240 (left = +0x240, 256 samples; right = +0x640). Two output
frames of 0x800 bytes in Fuzzy layout: X = this + 0x840, Y = this + 0x1040 (each: L 0-255, L 256-511, R 0-255, R
256-511 in 0x200-byte quarters). With `opt = pkt+0xC`:
- **opt even** (phase A: a first-half block): copy X.L0 -> Y.L0 and X.R0 -> Y.R0; DSP into **X.L0 / X.R0**
  (`CB3SoundDSPDataBlock::Process(this, in L, out)`, `Process(this + 0x110, in R, out)`, then `func_002FC820(0x100,
  L, R, ...)`: L = R = ((L + R) / 2) panned by the gains +0x1840 / +0x1844); DMA **Y** to IOP `opt`.
- **opt odd** (phase C: a second-half block): copy Y.L1 -> X.L1, Y.R1 -> X.R1; DSP into **Y.L1 / Y.R1**; DMA **X** to
  IOP `opt - 1`.
- The DMA: `isceSifSetDma({src = frame, dest = Fuzzy, size 0x800, attr 0}, 1)` [C 0x2FC7B8-0x2FC7E4].
So every frame the IOP receives holds, per half, the most recent block processed for that half *before* the current
event's block: the newest block goes out with the next event (double buffering, so the IOP never reads a half that
is being written).

### 6.3 Latency [L, from §5 and §6.2]
A sample written to core 0's wet ring at position p in period k (512 samples) is read at C(k) or A(k+1), processed at
the next event, sent back in the frame of the event after that, written at the following B/D and read from core 1's
MEMIN at position p in period k + 2: **exactly 1024 samples** at every ring position, provided each EE answer lands in
Fuzzy before the next B or D (128 samples after its A or C). A late answer only makes that write repeat the previous
frame's contents for the late half.

## 7. Interactions with RWA.IRX
- **SPU2 interrupt chain** (§2): B3ROUTE in front, RWA behind. Core-1 IRQs are B3ROUTE's; RWA's handler (which only
  counts core-1 IRQs) sees bit 1 only for an unknown IRQA [C].
- **RWA suspend / resume (EE ops 0x4A / 0x4C)** would break the chain [C RWA 0x5ECC / 0x5F88]: suspend reinstalls the
  handler RWA found at its start (not B3ROUTE's) and disables both cores' IRQs; resume reinstalls RWA's handler and
  restores both cores' IRQA / enable. Core-1 IRQs then go to RWA (counted, not re-armed) and B3ROUTE stops; core 1's
  MEMIN keeps looping its last frame at BVOL 0x7FFF until a 0x30. **B3's EE never sends 0x4A / 0x4C** [L: no
  `RwaRPCTransfer` call site with them in the ported EE library src/libs/rwa], so this is dormant.
- **DMA channel 0** is shared: B3ROUTE checks the status before each of its transfers but RWA installed a channel-0
  transfer-completion handler (RWA spec §5), which also fires for B3ROUTE's transfers [L libsd semantics; effect on
  RWA's channel-0 queue not studied]. B3ROUTE skips a transfer when RWA's is in flight.
- **Core 0's MEMIN** (RWA's mixer stream, ops 0x16 / 0x17) is at BVOL 0 from Construct on (§4).

## 8. The platform side
boot2's platform_sif (`b3route_call`) completed the two RPC call shapes the EE makes at the bind's buffer address
0xF2700: the init call {1, 0x8000, target, 0} (records the EE target) and a "routing" call with count 2 (records three
raw words), and signalled the client's semaphore; it sent no command 6. Since S4 of the SIF lane (2026-11-11) the port makes no RPC for it at all:
CB3SoundDistortion's functions are platform rows (src/native/platform_distortion.cpp) that take the RPC's time and
empty the list. Until engpan (2026-11-18) the list had no receiver in the shipped game, so the engine-model voices
SetChannels names played on core 0's dry bus with the engine model's own channel pans (each stereo layer's left half
hard left, its right half hard right: on headphones the bass on one side, the mids / highs on the other), where a PS2
plays them only through this round trip, as one mono signal panned by the car's gains.

Since engpan the shipped game has the module (§9) beside RW Audio's executor (src/native/platform_rwa.cpp, created
with it; lists sent earlier are kept and applied then):
- each list the rows send (0x400 bytes from the count word, as sceSifCallRpc sends it) goes to `B3Route::call` at
  the current IOP time;
- command 6 is held by the platform and delivered to the cid-6 handler in interrupt context (platform_sif
  `deliver_cmd_now`) between IOP slices: with B3ROUTE the executor catches up in slices of at most 64 samples, so every
  block is answered before its B / D phase, 128 samples on (§9's requirement). engfix (2026-11-18): engpan first
  queued it for the next safe point like RW Audio's replies, but the IOP catches up hundreds of samples at a time (the
  VBlank hook after the idle loop jumps to the VBlank, a command's catch-up), so several phases ran in one advance: in a
  race 44 % of the Fuzzy writes repeated the previous frame and a second Clean block overwrote the first before the
  handler read it (the engine quiet and crackling, a jump at every 128-sample seam 3.8 times the average step;
  correlation with the engine voices 0.685). Since: no stale write (`B3RouteStats::writes_stale` 0 of 7,500), seams
  1.02 times the average step, correlation 0.989 at exactly 1024 samples. safept (2026-11-18): delivered only where
  the port delivers interrupts -- a time check or a VBlank with interrupts on, no handler or SIF delivery running
  (`sif::can_deliver_now`), and no ptr_arg window open on the thread -- never inside the game's cid-0 send or the
  distortion RPC rows; elsewhere the IOP waits at the slice boundary after the send until such a point (race: 8 samples
  at most), so the answers land at the same IOP times and the sound is unchanged. `B3_B3ROUTE_STATS=1` prints the
  counts at exit (fresh / stale writes, deliveries, waits);
- the handler func_002FC530 and the DSP block's `ProcessB2Distortion` (0x2FBE20; both hand-written asm in retail, so
  not generated) and its mono mix `_FinalMix` (0x2FC820, hand-written MMI asm too) are platform rows written from the
  retail instructions; `CB3SoundDSPDataBlock::Process` and the type-2 filter func_002FB890 are the generated ones.
  libsif's sceSifSetDma / isceSifSetDma is a row (platform_distortion.cpp) that delivers into B3ROUTE's image (and is
  fatal for any other IOP address). The three rows were compared call by call against the retail code
  (`B3NATIVE_DIFF=...` in a NATIVE_FALLBACK=1 build, races): no difference.
- In the race the engine model uses DSP type 1 (ProcessB2Distortion) on both blocks; the gains at the player car are
  about 0xC000 / 0xC000 (16.16).
- `B3_B3ROUTE=0` leaves the module out (the port before engpan), for comparisons.

## 9. Native implementation (src/audio/b3route.{h,cpp})
The SPU2 model already has everything B3ROUTE uses (the output rings, MEMIN with BVOL, core-1 IRQA on ring positions,
sound-RAM reads / writes); no mixer insert is needed: **the DSP is game code on the EE**, run as the EE runs it.
`b3native::B3Route`:
- is the RPC server for 0x150276 (`SifRpcServer`, declared in b3route.h; driven by its test only);
- hooks the executor's SPU2 IRQ in front of RWA's (`RwaIop::set_spu2_irq_hook`), as the module's thread does, and
  shares RwaIop's mixer and virtual time;
- owns the module image (0x26E0 bytes at `module_base`); the EE's `isceSifSetDma` into Fuzzy reaches it through
  `SifIopMemory::sif_dma_to_iop`;
- sends command 6 through its platform (`B3RoutePlatform`: write the 0x600 bytes to the EE, deliver the 0x18-byte
  packet to the EE's cid-6 handler).

Choices: transfers complete at once (the channel-busy test asks RwaIop whether its channel-0 queue is idle);
`module_base` defaults to **0xF0480** so that the RPC buffer (base + 0x2280) is the 0xF2700 the platform's bind gives
the EE (boot2's server table) [G: the real load address is not known; any address works because the EE
only echoes opt back]; the image [0xF0480, 0xF2B60) overlaps that table's other made-up server addresses
(0xF1000-0xF2B00), harmless while the platform answers those RPCs natively (none is a DMA target). The unknown-IRQA path passes the IRQ on to RWA once (retail calls RWA's handler a second time
when bit 0 is also set; unreachable in practice). **Platform requirement**: the EE's command-6 handler must run, and
its DMA land, before the IOP clock passes the next B / D (128 samples): deliver command 6 synchronously from the IRQ
hook, or advance the IOP in slices of at most 128 samples with a safe point between; otherwise blocks are dropped
(the EE input buffer is overwritten by the next send).

Tests: `build/test_b3route` in `make test` (8 cases, 12,408 checks, 2026-11-06): the command list and its libsd writes
(stub and SPU2-equivalent mixer), the phase cycle (32 phases in 4096 samples, sends every 256 samples at ring positions
0 / 256 with opt Fuzzy / Fuzzy | 1, RWA's core-0 clock still alternating), the Clean read offsets and Fuzzy writes,
and the end-to-end path with the real mixer and a stand-in EE handler following func_002FC530's buffer logic: core 1's
output equals the masked voice's core-0 wet bus (identity, and halved) **exactly 1024 samples later** at every sample
compared (3,952 x 2), and not at 1023.

## 10. Open
- The MEMOUT ring layout (w0x1400 / w0x1600 = core 0 wet L / R) is the mixer's reading of the SPU2 Overview, not
  re-read here (the PDF has no text tools in this environment); B3ROUTE's own 0x200-offset reads only make sense with
  it [L].
- Whether the hardware reads MEMIN when no ADMA runs (B3ROUTE and RWA both rely on it; the mixer models it) [L].
- The EE-side mask -> hardware voice mapping (`CB3SoundESMHigh::Start` builds it in voice-list order, first voice in
  the highest bit) [G].
- How RWA's channel-0 completion handler reacts to B3ROUTE's transfers (§7) [not studied].
- B3ROUTE's real IOP load address under PCSX2 [G: GTFSCDVD, the next module, sits at 0x78C30 in a PCSX2 savestate, so B3ROUTE is probably just above RWA's image, around 0x75730].
