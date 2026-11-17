# RWA.IRX (IOP side of RenderWare Audio, SPU2 output): behaviour spec for a native IOP command executor

Lane audio1, 2026-11-05 (spot-checked: the pitch formula at 0x319C [C]). Read-only study of
the retail module (`perf/install/game/IOP/RWA.IRX`, 64,588 bytes,
module name `renderware_audio_spu2`, version 0x115, entry 0x8C84). Helper files, disc-derived and never committed,
in a local `build/audio1/irx/`:

| File | What |
|---|---|
| `rwa_imports.txt` | import stub address -> module / export index -> SDK name (from the SDK 3.0.3 `.ilb` files) |
| `rwa_annot.s` | objdump listing, `jal` targets named, `lui`/`addiu` constants and bss/data addresses annotated |
| `rwa_ghidra_pseudo.c` | Ghidra 12.1.4 pseudo-C of every function (raw image at base 0; reading aid only, names are `irx_<addr>` or SDK names) |
| `rwa_relrefs.txt` | every relocated address reference (`.rel.text` HI16/LO16 pairs, `.rel.data` words) |
| `rwa_text.dis`, `rwa_data.dis` | plain objdump of `.text` / `.data` |

Addresses below are module-relative (the module is relocatable from 0; PCSX2 places it at IOP 0x65130). `irx_XXXXXXXX` = RWA's own functions (no names exist). Evidence tags: **[C]** read
from the code at the cited address, **[L]** likely (hardware semantics from ps2tek / libsd / psx-spx knowledge, or an
inference that fits all the code), **[G]** guess. libsd entries are decoded with `sdmacro.h` (entry = param | voice<<1
| core; `0x2040` = SD_VA_SSA, `0x2140` = LSAX, `0x2240` = NAX, `0x0x00` VOLL, `0x01xx` VOLR, `0x02xx` PITCH, `0x03xx`
ADSR1, `0x04xx` ADSR2, `0x05xx` ENVX, `0x06xx` VOLXL, `0x07xx` VOLXR, `0x0980/0x0A80` MVOLL/R, `0x0B80/0x0C80`
EVOLL/R, `0x0F80/0x1080` BVOLL/R, `0x1500` KON, `0x1600` KOFF, `0x1800/0x1900/0x1A00/0x1B00` VMIXL/VMIXEL/VMIXR/VMIXER,
`0x1C00/0x1D00` ESA/EEA, `0x1F00` IRQA). All sound-RAM addresses are libsd **byte** addresses.

## 0. Architecture in one paragraph

[C] RWA.IRX has **no export table** and touches **no hardware register directly** (no `lui 0xBF8x/0xBF9x` anywhere):
everything goes through 16 libsd imports (`sceSdInit, SetParam, GetParam, SetSwitch, GetSwitch, SetAddr, GetAddr,
SetCoreAttr, GetCoreAttr, VoiceTrans, VoiceTransStatus, SetEffectAttr, SetTransIntrHandler, SetSpu2IntrHandler,
GetTransIntrHandlerArgument, GetSpu2IntrHandlerArgument`; no `sceSdBlockTrans`, no `ProcBatch`), sifcmd/sifman,
thbase/thevent/thsemap, sysmem, intrman. `ioman` is imported but **never called** (no reference to its stubs
0x8954-0x8974). Threads (priority, entry): module start thread -> 50 (`ChangeThreadPriority` 0x755C); RPC thread
0x72 (`irx_00008280`, created 0x75E8); idle heartbeat 0x73 (`irx_00008ED8`); file thread 0x7B (`irx_000003EC`);
volume-sweep thread 10 (`irx_00001720`); two DMA-channel threads 12 (`irx_00004804`, table 0xAF48); IRQ/callback
dispatch threads 9 (`irx_00006678`). The SPU2 core-0 IRQ is RWA's only clock (section 3).

## 1. The command dispatcher (EE -> IOP) and every reply

### 1.1 Receive path [C]
- `irx_000074DC` (RwaRPCOpen, called from the entry with handler `irx_000091B0`, user data = state 0xE100, RPC thread
  priority 0x72): `sceSifInitCmd`, `sceSifSetCmdBuffer(0xE010, 8)`, `sceSifAddCmdHandler(0, irx_00007F04, 0)`.
- SIF command 0 handler `irx_00007F04` (interrupt): copies the 0x30-byte packet into an **8-entry ring** at 0xDE40 and
  `iSignalSema`. **When 8 are pending the packet is dropped silently** [C 0x7F04 `if (tail < head + 8)`].
- RPC thread `irx_00008280` takes packets FIFO and calls `irx_0000792C`; after each it prints "RPC message queue too
  short! Lag = %d" when 8 are still pending. Every packet's payload (the SIF DMA part) has already landed at its
  destination before the handler runs (SIF semantics).
- `irx_0000792C` handles the system opcodes; any other opcode goes to `irx_000091B0(user = 0xE100, op, arg0 =
  pkt+0x24, arg1 = pkt+0x28, p5 = pkt+0x14)`.

### 1.2 Send path [C `irx_000083BC`]
`irx_000083BC(dst, src, size, op, -, p5, a0, a1)` builds `{+0x10 op, +0x14 p5, +0x18 src, +0x1C dst, +0x20 size, +0x24
a0, +0x28 a1, +0x2C last}` and `sceSifSendCmd(1, pkt, 0x30, src, dst, chunk)` (EE command 1), payload in chunks of at
most 0xFFC00 with +0x2C = 1 on the last; serialized by a semaphore (0xDFF0). Special-cased send opcodes: 3 becomes 0xB;
4 / 7 / 8 / 0x19 carry no payload; **5 = an IOP-internal sound-RAM write** (`irx_00006C84(1,1)` wait channel idle, then
`irx_00006AB8(1, write, src, dst, size)`, no packet); 6 = prints "rwaRPCDMASRAMTOIOP not implemented", no packet. The
"queued" mode (2) of this function is never selected on the IOP, so the post-command queue drain at the end of
`irx_0000792C` never sends anything [C].

### 1.3 System opcodes (`irx_0000792C`)

| EE op | What the IOP does | Reply (IOP -> EE) |
|---|---|---|
| 0x01 | nothing | **0x09**, a0 = 0x115 (version), a1 = 0 [C 0x79D4] |
| 0x02 | nothing | **0x0A**, a0 = DMA work area (AllocSysMemory, 0x414 bytes, section 8), a1 = module + 0xB1A0 (the three 0x70-byte remote-file request slots) [C 0x7A10] |
| 0x03 | IOP -> EE copy: data from pkt+0x18 to pkt+0x1C, size +0x20 | **0x0B** with the data (p5 = +0x14, a0/a1 = +0x24/+0x28), chunked. Not sent by B3's EE |
| 0x04 | the payload (mixer data) is already in IOP memory | **0x0C** (p5, a0, a1 echoed) only on the last chunk (+0x2C != 0) [C] |
| 0x05 | sound RAM write: src = pkt+0x18 (IOP address), dst = pkt+0x1C (sound RAM), size +0x20 (chunks of 0x200000): waits for channel 1 idle, sets channel 1's completion callback to `irx_00007FA8(&copy)`, transfers, **waits for completion** (RPC thread blocked) [C `irx_00008804(...,1,1)`] | **0x0D** (p5 = +0x14, a0/a1 = +0x24/+0x28, last = 1) from the completion callback, after all chunks are in sound RAM [C 0x7FA8] |
| 0x06 | sound RAM -> IOP memory (`irx_00008884`, read) | **0x0E** when done. Not sent by B3 |
| 0x07 | staged EE -> sound RAM: the chunk (<= 0x200000) was DMA'd into the DMA work area; the IOP writes it to sound RAM at +0x1C and answers each chunk with the packet itself as **0x0F** (addresses advanced, +0x20 decremented, +0x2C = last) so the EE sends the next chunk. **The work area is 0x414 bytes**, so a real op 7 would overrun it [C 0x766C]. Not sent by B3 (`RwaRPCTransfer` has no op-7 call site in the ported EE C) | 0x0F per chunk |
| 0x08 | sound RAM -> EE via the work area, chunked | **0x10** at the end. Not sent by B3 |
| 0x32 | remote file operation on slot pkt+0x24 (section 6) | **0x33** later |
| other | `irx_000091B0` (below) | |

### 1.4 User opcodes (`irx_000091B0`, a0 = pkt+0x24, a1 = pkt+0x28, p5 = pkt+0x14)

| EE op | What the IOP does [C unless noted] | Reply |
|---|---|---|
| 0x11 | output create: a0 = NumVoices, a1 = reverb core mask (bit 0 core 0, bit 1 core 1). Init (`irx_000017C0`, section 2.1), then per voice n < NumVoices `irx_00002E34` | **0x1A** (a0 = a1 = 0) [0x93C4], sent before the asynchronous reverb work-area clear finishes |
| 0x12 | destroy: stop the sweep thread, remove its IRQ client, delete semaphores (`irx_00001AF8`) | **0x12** [0x943C] |
| 0x13 | query | **0x1B**, a0 = `QueryMaxFreeMemSize()`, a1 = 0x100000 (constant at state+0x688) [0x9478] |
| 0x14 | per-tick snapshot (section 2): `irx_00002F0C` for each voice n < NumVoices, then `irx_00001CB8` | the whole 0x680-byte state back to the EE reply area as op **0x14** (section 2.4) |
| 0x15 | a0 = EE address of the EE's reply area, stored at state+0x694 | **0x1C**, a0 = IOP address of the snapshot area (module + 0xE100), a1 = 0 [0x9528] |
| 0x16 | mixer on, a0 = ring bytes (= MixerBufferLength << 3); section 3.2. Ignored unless `(a0 & 0x3FF) == 0` and the mixer is off | **none** |
| 0x17 | mixer off (only if on): stop channel 0's stream, free the ring, write 0x800 zero bytes to sound RAM 0x4000 and 0x4800 (internal op-5 writes) | **0x1D** only if the mixer was on [0x2E18]. If 0x16 had been ignored, no 0x1D ever comes and the EE's `while (D_004E2C80)` waits forever [C, retail hazard] |
| 0x2A | create the transaction semaphore, transaction mode on | **0x2B**, p5 echoed, a0 = a0, a1 = success. Not sent by B3's EE |
| 0x2C | delete that semaphore, mode off | **0x2D**, p5 echoed, a0, a1 echoed [0x96F0] (EE: `*a0 = a1`, wake thread p5) |
| 0x30 | IOP file interface: a0 = IOP address of a 4-function table (section 6). Stops the file thread if one ran, stores a0, starts the file thread if a0 != 0 | **0x31**, p5 echoed, a0 = a1 = 0 [0x95DC] |
| 0x36 | builds a transaction from an 8-word template (0xB178) and links it into a list (0xE074) that **no code ever drains** (dead) | none |
| 0x34 0x38 0x3A 0x3C 0x3E 0x40 | ignored | none |
| 0x20 / 0x26 | `*(u32 *)a0 = a1; WakeupThread(((u32 *)a0)[2])` | none |
| 0x21 0x23 0x27 0x29 | `WakeupThread(((u32 *)a0)[2])` | none |
| 0x43 | `WakeupThread(p5)` | none |
| 0x46 | wave upload from IOP memory, parameters = the 0x14 bytes the EE DMA'd to work area + 0x400 (section 5) | **0x47**, p5 = the EE callback, a0 = a1 = 0 [0x9A40], after the upload is in sound RAM |
| 0x48 | reinterleave in IOP memory: a0 = table of 0x24-byte records, a1 = count (`irx_0000A0A8`, section 7.2) | none |
| 0x49 | `memset(a0, 0, a1)` in IOP memory ("NULL packet") | none |
| 0x4A | suspend: save MMIX, MVOL, BVOL (if a0/a1 bit 24 clear), core attrs 2/6/SPDIF (`irx_0000290C`); take the heartbeat semaphore; restore libsd's SPU2-IRQ handler and disable IRQs; stop both DMA channels' handlers | **0x4B** [0x9910] |
| 0x4C | resume: bump every cached snapshot value by 1 so the next snapshot rewrites everything; re-run `irx_00002F0C` for voices not named in the a0/a1 masks; restore the saved registers; reinstall RWA's handlers; release the heartbeat | **0x4D** [0x999C] |
| 0x4E | DMA channel lock, a0 = channel: mark locked, **poll every 5334 us until the channel is idle**, give libsd's original transfer-interrupt handler back | **0x4F** [0x99D0] (p5 = EE thread to wake) |
| 0x50 | unlock: reinstall RWA's transfer handler, clear the lock | **0x51** [0x9A04] |
| anything else | ignored | none |

[C] **No IOP code sends 0x19 except the mixer stream (section 3), and nothing sends 0x1E, 0x1F, 0x22, 0x24, 0x25,
0x28 or 0x42**: the only `irx_000083BC` call sites (grep of `li a3,N` before `jal 0x83BC`) use opcodes 5, 9, 0xA, 0xB,
0xC, 0xD, 0xE, 0x12, 0x14, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x2B, 0x2D, 0x31, 0x33, 0x47, 0x4B, 0x4D, 0x4F, 0x51 and the
generic sender `irx_000090EC`, which is reachable only from the dead op-0x36 list. So the voice / sound-RAM grant
protocol (0x1E/0x1F/0x22 -> 0x20/0x21/0x23), the input-area protocol (0x24/0x25/0x28) and stream sync (0x42 -> 0x43)
are present on both sides but **never started by this IOP module**.

**Spurious 0x0D** [C]: channel 1's completion callback (`irx_00006A7C(1, irx_00007FA8, &copy)`, set by ops 5-8) is
never cleared, and `irx_000068D0` calls it on **every** channel-1 transfer completion. After an op 5, each later
channel-1 transfer (op 0x46 uploads, op 0x17's zero writes) re-runs `irx_00007FA8` with the old copy (opcode 5,
remaining 0) and sends another **0x0D** with that op 5's p5 / a0 / a1. B3's op 5 has p5 = 0, so on the EE this only
clears `_RPCIOPRAMBusy` (already 0): harmless, but it is EE-visible traffic.

## 2. The 0x14 snapshot, per hardware voice

The EE's 0x680 bytes are DMA'd straight into the IOP's live state at module + 0xE100 (the address the 0x1C reply
named), overwriting the IOP's copy; the IOP then edits it in place and sends it back. IOP-private state follows at
+0x680 (caches +0x698..+0x6CC, reverb attr +0x6D0, clear jobs +0x6E4, semaphore +0xB64, cached Gain +0xB68, ReverbType
+0xB6C, ReverbGain +0xB70/+0xB72, SpeakerConfig +0xB74, Delay +0xB78, Feedback +0xB7C) [C `irx_000017C0`,
`irx_00001CB8`]. Per voice n (0..NumVoices-1) a 0x30-byte "HV" record at 0xC840 + n*0x30: +0 n, +4 libsd voice entry
`(n >= 24) | (n % 24) << 1`, +8 last Frequency, +0x10 / +0x14 target L / R, +0x18 / +0x1C last volume L / R, +0x20
flags (2 R inverted, 4 L inverted, 8 L sweeping, 0x10 R sweeping), +0x24 bit 0 forces targets to 0 (never set in this
module [L]), +0x28 -> VoiceState[n], +0x2C -> state [C `irx_00002E34`].

### 2.1 Fixed set-up (op 0x11) [C `irx_000017C0`, `irx_00002E34`]
- MVOL core 0 L/R = 0x3FFF; MVOL core 1 L/R = Gain >> 2 (0 first, then Gain 0xFFFF -> 0x3FFF at the end).
- Zero 0x800 bytes at sound RAM 0x4000 (channel 0) and 0x4800 (channel 1) with `sceSdVoiceTrans(ch, 8 = write by IO)`.
- Upload 0x40 bytes to sound RAM **0x5010**: 0x5010 `0C 01 00..` (one silent ADPCM block, flags 1 = end, no repeat),
  0x5020 `0C 07 00..` (silent block that loops on itself), 0x5030 / 0x5040 `07 00 77..` (unreferenced). Only
  **0x5000** and **0x5010** are used anywhere (grep of 20480 / 20496). 0x5000-0x500F is not written by RWA ([G]
  libsd's silent area; RWA never keys a voice on there).
- BVOL (sound-data-input volume) L/R = 0x7FFF on both cores.
- All 48 hardware voices: PITCH 0, SSA = LSAX = 0x5000, ADSR1 = 0x000F, ADSR2 = 0x0005.
- Sweep thread (priority 10) + its IRQ client (section 3.1).
- ESA/EEA core 0 = 0x1C0000 / 0x1DFFFF, core 1 = 0x1E0000 / 0x1FFFFF; EVOL L/R = 0 on both cores.
- Reverb type 1 (section 4), SpeakerConfig 1, Gain 0xFFFF.
- Per voice n < NumVoices: VoiceState[n] = {PlayState 3, Start 0x5000, LoopStart 0x5000, Frequency 0x5622 (22050),
  Flags 0}; SSA = 0x5000; pitch from 22050 (= 0x759); volume L = R = 0x7FFF (written as 0x3FFF).
- libsd's own init (LIBSD.IRX, for the graph) [C LIBSD 0x16A8-0x1730, 0x1760-0x17F0; L that sceSdInit(0) takes this
  path]: VMIXL/VMIXEL/VMIXR/VMIXER = 0xFFFFFF on both cores (every voice to dry and wet), MMIX core 0 = 0x0FF0 (voices
  and MEMIN to dry and wet), MMIX core 1 = 0x0FFC (+ core-0 input to dry only), AVOL core 1 = 0x7FFF, everything else 0.

### 2.2 Per voice, in voice order (`irx_00002F0C`) [C]
Let V = VoiceState[n], H = HV[n]:
1. If `V.StartAddress == 0 && V.PlayState == 2` then V.PlayState = 1.
2. PlayState 2 or 1: set this voice's **KOFF** bit (state +0x6A0 / +0x6A4 per core, bit n % 24) [`irx_0000306C`].
3. If `V.Frequency != H.freq && V.PlayState != 5`: **PITCH = (V.Frequency << 12) / 48000 & 0xFFFF** (signed 32-bit
   multiply-shift, C integer division = truncation) [`irx_0000319C`]. H.freq = V.Frequency always (so a frequency
   changed while in state 5 is never applied later).
4. Targets: H.L = (s32)V.Volumes >> 16 (arithmetic), H.R = V.Volumes & 0xFFFF (unsigned).
5. PlayState 2 (key on) [`irx_000030A0`], only if `V.StartAddress > 0x500F` (unsigned):
   - **SSA = V.StartAddress + (u32)(V.TriggerPosition << 4) / 28** (byte address, not rounded to a block; [L] the
     hardware uses the 16-byte block it falls in, i.e. block floor(T/28));
   - **ADSR1 = (T ? 0x1B : 0) << 8 | 0xFF** (AR 0 or 27 linear, DR 15, SL 15), **ADSR2 = 0x0007** (sustain linear
     increase rate 0, release linear rate 7) - the only ADSR values ever used, written now (before the KOFF below,
     so a re-triggered voice's old note releases with RR 7);
   - set the **KON** bit (+0x698 / +0x69C);
   - if `!(V.Flags & 1)`: V.LoopStartAddress = 0x5000.
6. PlayState 4 (playing): if `ENVX == 0` (`sceSdGetParam(voice|0x500)`): KOFF bit and **V.PlayState = 1** (the voice
   ended). Else if not looping (`!(V.Flags & 1)`): **LSAX = 0x5010** (every tick).
7. PlayState 3 (free) / 5 (idle on an attached voice): nothing more.

### 2.3 Apply (`irx_00001CB8`), after all voices [C]
The KON / KOFF masks are read once at entry. Order:
1. `Gain != cache` -> MVOL core 1 L/R = Gain >> 2 (`irx_00002224`).
2. `ReverbGain L/R != cache` -> EVOL L/R = the raw s16 values on each reverb core (`irx_00002810`).
3. `ReverbType`, `Delay` or `Feedback` changed -> cache Delay / Feedback, reverb change (section 4).
4. `SpeakerConfig != cache` -> `sceSdSetCoreAttr(SD_C_SPDIF_MODE core 0, cfg == 5 ? 1 : 0)` (PCM vs bitstream; [G]
   irrelevant for a PC port).
5. Per core c, each of dry L (+0x630+4c), dry R (+0x638), wet L (+0x640), wet R (+0x648) that differs from its cache:
   `VMIXx = new | (~switch & current)` with switch = +0x650+4c, read-modify-write under `CpuSuspendIntr`. Note the
   mapping: dry L -> VMIXL, dry R -> VMIXR, wet L -> VMIXEL, wet R -> VMIXER.
6. **KOFF**: `sceSdSetSwitch(KOFF core 0, mask0)`, `(KOFF core 1, mask1)`, both always written, masks cleared
   (`irx_00001B9C`; it delays 42 us if the same switch was written less than 41 SysClock ticks before - practically
   never).
7. If any voice is in both masks (every key-on is, by 2.2 step 2): **DelayThread(5800 us)** (lets the RR-7 release
   finish: [L] linear release rate 7 = -128 per sample, about 256 samples = 5.3 ms).
8. Voices keyed off but not on: V.LoopStartAddress = 0x5000, LSAX = 0x5000.
9. Voices keyed on: volumes set immediately, no sweep (`irx_0000325C`): L' = (V.Flags & 4) ? -H.L : H.L, **VOLL =
   (L' >> 1) & 0x7FFF**; R' = (V.Flags & 2) ? -H.R : H.R, **VOLR = (R' >> 1) & 0x7FFF** (fixed-volume mode, 15-bit
   two's complement, so phase invert = negative volume [L]); H.last = H.target, H.flags phase bits = Flags, sweep bits
   cleared.
10. **KON** both cores (same helper, masks cleared).
11. Write-back, voices n < NumVoices: if PlayState != 3: **V.CurrentPosition** = (PlayState == 2) ? V.TriggerPosition :
    `max(0, NAX - V.StartAddress) * 28 / 16` with NAX = `sceSdGetAddr(voice | 0x2240)` (bytes) [`irx_000031D0`]; then
    PlayState 2 -> **4**. (State 1 and 5 voices also get a NAX position; after the voice jumped to 0x5010 the result is
    clamped to 0.)
12. If the EE reply address (+0x694) is set: send the 0x680 bytes (section 2.4).
13. DelayThread(83 us).
14. Voices keyed on: **LSAX = (V.Flags & 1) ? V.LoopStartAddress : 0x5010** (after KON, so it overrides the loop
    address the hardware latched at key on).

### 2.4 What goes back, and when [C]
`irx_000083BC(dst = EE reply area, src = state, 0x680, op 0x14)`: one packet, opcode 0x14, payload 0x680 (the EE's
handler ignores op 0x14; the data lands in its reply area). Content = what the EE sent, edited in place: PlayState
(2 -> 4, 4 -> 1 on ENVX 0, 2 -> 1 for StartAddress 0), CurrentPosition (every voice not in state 3),
LoopStartAddress (0x5000 for non-looping key-ons and for key-off-only voices); everything else, **PacketID +0x670
included, is echoed unchanged**. It is sent from inside the 0x14 command, before the next command is taken: about 5.9
ms after the snapshot arrives when any voice keys on (step 7), otherwise immediately (plus libsd call time). Voice end
is detected on the snapshot **after** the envelope reached 0 (the EE sends 4, the IOP answers 1).

### 2.5 Volume changes while playing (sweep thread `irx_00001720` -> `irx_00003358`) [C]
Runs every second SPU2 IRQ (section 3.1) at priority 10, for every n < 48 with **UsedVoices[n] (state +0x600) != 0**,
independent of the 0x14 traffic. Per channel (L shown; R is the same with VOLR, VOLXR 0x700, phase flag 2, sweep flag
0x10):
- t = H.L, forced to 0 when H's phase bit differs from V.Flags & 4 (phase change pending) or H+0x24 bit 0.
- If sweeping (flag 8): c = (s16)VOLXL. If c < 0: m = (c == -32768) ? 32767 : -c, and if H is not phase-inverted write
  VOLL = (m >> 1) fixed; else m = c, and if H is inverted write VOLL = (-c >> 1) & 0x7FFF fixed. If |t - m| <= 384:
  arrived: H.last = t, clear flag 8, H phase bit = V.Flags & 4, VOLL = (+-t >> 1) & 0x7FFF fixed. Else H.last = m.
- d = (t - H.last) >> 7 (arithmetic). d == 0: if the phase bit differs, set it, H.last = t, VOLL fixed as above.
  d != 0: **VOLL = 0x8000 | R[d]** (linear increase) for d > 0, **0xA000 | (R[-d] + 1)** (linear decrease) for d < 0,
  | 0x1000 when H is phase-inverted; set flag 8. R[] = the table at 0xAB48 (index 1..255; [C] values:
  1:0x43 2:0x3D 3:0x3A 4:0x38 5:0x37 6:0x35 7:0x34 8-9:0x33 10-11:0x32 12-13:0x31 14:0x30 15-18:0x2F 19-23:0x2E
  24-25:0x2D 26-29:0x2C 30-37:0x2B 38-44:0x2A 45-51:0x29 52-59:0x28 60-74:0x27 75-88:0x26 89-101:0x25 102-118:0x24
  119-143:0x23 144-181:0x22 182-209:0x21 210-227:0x20 228-255:0x1F). The SPU2 sweep envelope semantics (rate ->
  step / period, the same formulas as ADSR) are needed to reproduce the curve [L ps2tek / psx-spx].

## 3. The clock, the tick and the mixer bus

### 3.1 The clock: SPU2 core-0 IRQ [C code, L period]
`irx_0000618C(0, table 0xAF58, 2)`: IRQA core 0 = 0x800, IRQ enabled; the handler (`sceSdSetSpu2IntrHandler(
irx_00005E20)` -> dispatch thread -> `irx_00006274`) re-arms with IRQA alternating **0x800 / 0xA00** each IRQ. [L]
Byte 0x800 is the start of core 0's voice-1 output ring (0x400 bytes, 512 samples) that the SPU2 writes at 48 kHz, so
the IRQ fires every **256 samples = 5.333 ms (187.5 Hz)**; this fits the MEMIN step of 0x200 bytes per channel per IRQ
(3.2). Clients, sorted by mask: the DMA manager (`irx_000042CC`, mask 0x100: every IRQ, both channels) and the sweep
thread (mask 0xAA against a bit that rotates 0x80 -> 0x40 -> ... -> 1: **every second IRQ, 512 samples**). Core-1
IRQs are only counted ("Dropped access IRQ" warning, 0x6274). The heartbeat thread (`irx_00008ED8`,
DelayThread(16700 us)) does nothing but toggle a semaphore [C].

### 3.2 The tick (op 0x19) and the mixer bus (op 4) [C `irx_00002C90`, `irx_00007188`, `irx_00007028`, `irx_00001690`]
- Op 0x19 exists only while the mixer is on. Op 0x16 (N = a0 bytes, N % 0x400 == 0) allocates N (AllocSysMemory,
  aligned up to 0x80), zeroes it and starts RWA's own block-transfer emulation on channel 0 in loop mode
  (`irx_00007188(0, 0x10, ring, N)`; libsd's BlockTrans is not used).
- Ring format: consumed 0x400 bytes per step = **0x200 bytes (256 s16 samples) left, then 0x200 bytes right**,
  copied into core 0's sound-data input (MEMIN) at 0x4000 / 0x4200 (left, alternating) and 0x4400 / 0x4600 (right)
  [C `irx_00005318`, index 3/4 -> 0x4000/0x4400 + 0x200 for odd slots]. Played through BVOL 0x7FFF and MMIX core 0
  0x0FF0 (dry and wet) [L].
- When the stream cursor reaches the ring's start or its middle, `irx_00001690` sends **0x19, a0 = ring + k * N/2,
  a1 = N/2**, k starting at **1** (data 0xAB40 = 1) and flipping each time. One tick per N/2 bytes = N/8 = 
  MixerBufferLength samples of output time (the EE asks its mixer for `size >> 2` = MixerBufferLength samples). [L]
  The half named is the one the transfer engine is about to (re)queue, i.e. the EE has about one 256-sample step to
  deliver it.
- The EE answers with op 4: `size` bytes to a0; the IOP does nothing with op 4 except the 0x0C reply.
- Op 0x17 stops the stream, frees the ring, zeroes 0x4000-0x47FF and 0x4800-0x4FFF, then 0x1D.

### 3.3 The 0x14 tick is not IOP-timed [C]
Nothing on the IOP schedules op 0x14: it is processed when it arrives (the EE sends one per EE tick, only after the
previous reply's PacketID matched), and the reply goes back from the same command. The EE's tick rate is its own.

## 4. Reverb [C `irx_000025A8`, `irx_00002778`, `irx_00002810`; mode names from sdmacro.h]
- Only cores in the op-0x11 mask (state +0x690) are touched. Both get the same parameters.
- On a change of ReverbType / Delay / Feedback (snapshot 0x658 / 0x668 / 0x66C), if a mask core exists: cache
  ReverbType; `attr = {mode = ReverbType - 1, depth_L = cached ReverbGain L, depth_R = R, delay = Delay, feedback =
  Feedback}`; then **only if the previous change has finished** (`PollSema(+0xB64)` succeeds; otherwise the change
  is dropped for good because the cache already holds it): EVOL L/R = 0 and effect disable on mask cores; zero-fill the
  work area from 0x1C0000 (or 0x1E0000 when core 0 is not a reverb core), 0x20000 bytes (0x40000 when both cores), in
  0x800-byte DMA jobs from a zero buffer, at most 16 queued; when the last job completes: `sceSdSetEffectAttr(core,
  &attr)` and `SD_C_EFFECT_ENABLE = (mode != -1)` on each mask core, release the semaphore.
- **ReverbType -> libsd mode**: 1 OFF, 2 ROOM, 3 STUDIO_A, 4 STUDIO_B, 5 STUDIO_C, 6 HALL, 7 SPACE, 8 ECHO, 9 DELAY,
  10 PIPE (mode = type - 1; type 0 would be -1 = effect off). Delay / Feedback (0..127 from the EE) matter for ECHO /
  DELAY in libsd [L].
- ReverbGain L/R changes alone: EVOL L/R = the raw s16 at once (no clear). EVOL is otherwise set by
  `sceSdSetEffectAttr`'s depth_L / depth_R (the gains cached when the change started) [L libsd semantics].
- Reverb presets themselves (the SPU2 reverb register sets per mode) live in LIBSD.IRX, not RWA [L].

## 5. Sound RAM uploads

| Path | Transfer | Blocking | Completion |
|---|---|---|---|
| op 0x05 | `irx_00006AB8(1, write, IOP src, SPU dst, size)` = a job in RWA's transfer queues, DMA by `sceSdVoiceTrans(ch, 0)` [C 0x3BDC] | RPC thread blocked until done [C `irx_00008804(...,1,1)`] | **0x0D** from the channel-1 callback after the last chunk [C 0x7FA8]; chunks of 0x200000 |
| op 0x46 | params {offset, SPU dst, IOP src, bytes, target length} at work area + 0x400; VAG flag patch, then upload `(bytes + 0x3F) & ~0x3F` bytes to SPU dst on channel 1 | blocked until done | **0x47** (p5 = EE callback, a0 = a1 = 0) [C 0x9A40]; also a spurious 0x0D (section 1) |
| op 0x07 | staged in the 0x414-byte work area | not blocking (the next chunk waits for channel 1) | 0x0F per chunk. Unused by B3 |
| op 0x49 | not an upload: `memset(a0, 0, a1)` in IOP memory | synchronous | none |

The 0x46 flag patch [C `irx_00009F94`], with o = offset, s = src (IOP), b = bytes, t = target length, e = o + b:
```
first = (o < 2) && (e != 0) && (s[1] == 0);   /* tested before the writes below */
s[1] = 0;                 /* flag byte of the first block of this piece   */
s[b - 15] = 0;            /* flag byte of its last block                  */
if (o < 2 && e != 0)            s[1 - o] = 6;               /* ring offset 1: loop start + repeat */
if (o <= t - 15 && t - 15 <= e) s[t - 15 - o] = 3;          /* last block of the ring: end + repeat */
if (first && o <= t - 31 && t - 31 <= e) s[t - 31 - o] = 3; /* also the block before it */
```
(unsigned arithmetic). This turns a streamed wave of length t into an endless sound-RAM ring.
- **Upload-pending flag**: EE side only: `_rwaGRwaSPU2WaveIOPUploadPending = 1` when it sends 0x46, cleared by
  `func_002B52C0`, which the EE's 0x47 handler calls as the packet's p5 [C EE C `rwaspu2waveuplo.cpp`, IOP 0x9A40].
  `_RPCIOPRAMBusy` is set only by EE ops 7 / 8, cleared by 0xB-0xE / 0xF / 0x10.
- **Throughput** [L]: the channel threads move at most 0x18C0 bytes (minus 0x80 per job) per batch, a batch
  per IRQ-driven wake-up (`irx_00004A94`, budget at channel +0x48 = 0x18C0), i.e. roughly 6 KB per 5.33 ms per
  channel; a 0x10000-byte upload takes on the order of 10 IRQs (50 ms). Both channels serve the shared queues; the
  mixer stream has its own ring on channel 0.

## 6. Remote file ops (op 0x32) [C `irx_00009E14`, `irx_00009CCC`, `irx_000003EC`, `irx_00000BD4`..`irx_00001100`]
- **File API**: not ioman. Op 0x30 gives an IOP table `F` that the file thread calls as: `F[0](name, "rb")` -> handle
  (0 = fail); `F[1](handle)` close; `F[2](buf, 1, size, handle)` -> bytes, **retried in a busy loop while negative**;
  `F[3](handle, pos, 0)` -> 0 on success (seek set). stdio-shaped [C 0x3EC-0x6BC]. (B3's table presumably comes from
  its GtFs IRX [G].)
- The EE DMAs the 0x70-byte request to slot module + 0xB1A0 + index*0x70 first; op 0x32 a0 = index. The IOP: slot
  flags |= 0x20 (IOP-local, cleared on completion). If the slot has no open file it first queues an open of the slot's
  name (+0x24) and re-dispatches the same request when the open finishes (on open failure the code still stores the
  failed object, a retail bug). Then by +0x10:
  - 1 close: unqueue, `F[1]`, forget the handle -> 0x33 with result 0.
  - 2 read to EE: `F[2]` into the module staging buffer (module + 0xECA0, about 0x1800 bytes before other bss [L])
    for +0x08 bytes **at the current file position** -> 0x33 **carrying the data**: DMA of +0x08 bytes (the requested
    size, not the bytes read) from the staging buffer to EE +0x04.
  - 3 read into IOP memory: `F[2]` straight into IOP address +0x04, **size rounded up to 0x800** (`(size + 0x7FF) &
    ~0x7FF`: the file position advances by the rounded size and up to 0x7FF bytes past the buffer are written), at the
    current position. **+0x0C is ignored**: a seek to +0x0C happens only when flag 8 is set in +0x1C, and the EE
    clears flag 8 on every read [C 0x9ED8, EE `RwaSPU2RemoteIOPFopsRead`]. -> 0x33.
  - 4 seek: `F[3](handle, +0x0C, 0)` -> 0x33 with the new position (0 on failure).
- **The completion**: always opcode **0x33** (never 0x47): p5 = request +0x14 (the EE callback), **a0 = result**, a1 =
  index. Result: read (2 and 3) = **+0x08, the requested size, even on a short read or EOF** [C 0x9D68]; seek = new
  position; close = 0; allocation failure = 2. The IOP never writes the EE result word (+0x18); the EE callback does.
- **In flight**: one file thread, one FIFO queue (head 0xB32C), so operations of all three slots run one at a time in
  arrival order; each file object also has a semaphore, so a second operation on the same file waits for the first.

## 7. Stream sync and the grant protocols
### 7.1 [C] Dead in this module
0x42 -> 0x43 (stream sync), 0x1E -> 0x20, 0x1F -> 0x21, 0x22 -> 0x23 (voices + sound RAM), 0x24 -> 0x26, 0x25 ->
0x27, 0x28 -> 0x29 (input areas): the IOP has handlers for the EE's answers (wake-ups, section 1.4) but no code that
sends the requests. The executor never has to start them.
### 7.2 Reinterleave (op 0x48) [C `irx_0000A0A8`]
For each of a1 records r (0x24 bytes, u32 fields r[0..8]) at IOP a0, repeated r[0] times:
`blk = r[3], step = r[4], chunk = r[8], src = r[1], dst = r[2], a = blk - r[5], b = blk - r[6], n = (u16)r[7];`
`while (n) { ua = a & 0xFFFF; if (!ua) { src += step; ua = blk & 0xFFFF; } ub = b & 0xFFFF; if (!ub) { dst += step;
ub = blk & 0xFFFF; } copy chunk >> 2 words src -> dst; a = ua - (chunk & 0xFFFF); b = ub - (chunk & 0xFFFF);
n = (n - chunk) & 0xFFFF; src += chunk; dst += chunk; }` then `r[1] += blk; r[2] += blk`. Not sent by B3's ported EE
C.

## 8. IOP memory the EE sees
| Address reported | Where it comes from |
|---|---|
| 0x0A a0: DMA work area | `AllocSysMemory(0, 0x414, 0)` in RwaRPCOpen [C 0x766C]; +0x400 = the 0x14-byte 0x46 parameter block |
| 0x0A a1: file request slots | module data + 0xB1A0 (3 x 0x70) [C] |
| 0x1C a0: snapshot / state | module bss + 0xE100 [C] |
| 0x19 a0: mixer ring halves | `AllocSysMemory(0, N, 0)` at op 0x16, +0x7F & ~0x7F [C 0x2C90] |
| 0x1B a0 / a1 | `QueryMaxFreeMemSize()` / 0x100000 constant [C] |
Fixed (module-relative) vs heap: the slots, the state and the staging buffer are in the module image; the work area
and the ring are heap. The EE also allocates IOP memory itself (`sceSifAllocSysMemory`: the upload buffer 0x10000, the
stream rings) and passes those addresses in op 3 / 0x46 requests.

## 9. Other EE-observable state
- PlayState / CurrentPosition / LoopStartAddress edits and the PacketID echo (2.4); the ~5.9 ms key-on latency of the
  reply; end detection by ENVX == 0 one snapshot late; positions from NAX ([G] NAX granularity within a block: to be
  measured, test B0 / B).
- Op 0x19 cadence = one per MixerBufferLength samples, k alternating starting at 1.
- File results report the requested size; op-3 reads advance the file by the size rounded to 0x800.
- Spurious 0x0D after channel-1 transfers; reverb changes dropped while a clear runs; 0x16 with a size not a multiple
  of 0x400 is ignored (and 0x17 then never answers).
- The 8-packet receive ring drops packets beyond 8 outstanding.
- No ENDX use: voice end = envelope 0 [C, ENDX never read].

## 10. Executor API implications (what a native executor must do, per opcode, in order)
State: an IOP RAM model (2 MB array, so EE-given IOP addresses for uploads / file reads / mixer data work), a sound
RAM (2 MB), 48 voices with SSA/LSAX/NAX/pitch/ADSR/VOLL/VOLR/VOLX/ENVX, KON/KOFF, VMIX masks, per-core MVOL/EVOL/BVOL/
MMIX/AVOL, reverb mode per core, and a 48 kHz output clock that produces an "IRQ" every 256 samples.

1. **0x01** -> reply 0x09 (0x115, 0). **0x02** -> reply 0x0A (work area address with 0x414 bytes behind it, slot base).
2. **0x11** -> do 2.1 (silent blocks at 0x5010/0x5020, voice defaults, the libsd default graph, MVOL/BVOL, reverb
   type 1 on the mask cores with the work area zeroed), reply 0x1A. **0x13** -> 0x1B (free, 0x100000). **0x15** ->
   remember the EE reply address, reply 0x1C (snapshot area). **0x12** -> stop, reply 0x12.
3. **0x14** -> the payload is the new snapshot: run 2.2 for n < NumVoices in order, then 2.3 steps 1-11 in order (the
   5.8 ms delay may be modelled as "the KON takes effect 5.8 ms of output time after the KOFF"), send the 0x680 back
   (op 0x14 to the reply address), then step 14 (LSAX) 83 us later. Keep the reply timing: the EE does not send the
   next snapshot until the reply's PacketID matches.
4. **Every 256 output samples** (the IRQ): advance uploads / reverb clears; **every 512**: run 2.5 for voices with
   UsedVoices != 0 (needs the hardware sweep model for 0x8000/0xA000 volumes).
5. **0x16** -> if N % 0x400 == 0 and off: allocate N in IOP RAM, zero, start feeding MEMIN core 0 from it (256 L + 256
   R per step), send **0x19 (ring + k*N/2, N/2)** each N/2 bytes consumed, k = 1, 0, 1, ...; **0x04** -> data is
   already in IOP RAM (write it there), reply 0x0C on the last chunk; **0x17** -> stop, zero MEMIN, reply 0x1D.
   Recommendation: answer 0x17 with 0x1D even after an ignored 0x16 (retail would hang the EE).
6. **0x05** -> copy IOP RAM -> sound RAM, reply 0x0D (p5/a0/a1 echoed) once the data is "in" (immediately is fine for
   the EE, which does not poll for it); decide whether to reproduce the spurious 0x0D after 0x46 (harmless for B3).
7. **0x46** -> read the 0x14 block from work area + 0x400, apply the flag patch to IOP RAM, copy to sound RAM, reply
   0x47 (p5, 0, 0). The EE's upload-pending flag clears on that reply; keep a realistic delay if stream timing tests
   (B0/B) show the game depends on it [L ~ size / 6 KB per 5.33 ms].
8. **0x30** -> install the host file layer, reply 0x31 (wakes the EE thread in p5). **0x32** -> section 6 exactly:
   implicit open, FIFO one at a time, op 3 reads round up to 0x800 at the current position and ignore +0x0C, op 2 DMAs
   +0x08 bytes to the EE, reply 0x33 (p5 = request +0x14, a0 = requested size / position / 0, a1 = index).
9. **0x48** -> 7.2; **0x49** -> memset IOP RAM; **0x2C** -> 0x2D (p5, a0, a1 echoed); **0x2A** -> 0x2B; **0x4A/0x4C**
   -> 0x4B/0x4D; **0x4E/0x50** -> 0x4F/0x51 (p5 = the EE thread); **0x20/0x21/0x23/0x26/0x27/0x29/0x43**, **0x34-0x40
   even**, **0x36** -> no reply.
10. Never originate 0x1E/0x1F/0x22/0x24/0x25/0x28/0x42.

## Open questions
- NAX granularity (positions) and whether the sweep / ADSR model should follow PCSX2 or real hardware: measure on the
  reference run (test B0 / B logs the IOP -> EE words per tick).
- The exact half the 0x19 tick names relative to what MEMIN is playing (one step of slack, [L]); matters only for
  latency, not content.
- What B3's file table (op 0x30) points to on the IOP (GtFs IRX?) - the executor replaces it anyway.
- 0x5000-0x500F content after sceSdInit ([G] silence; never keyed on).
- libsd's `sceSdSetEffectAttr` details (does it set ESA from EEA and the mode size; ECHO/DELAY use of delay/feedback)
  and the reverb preset tables: read LIBSD.IRX if the reverb must match.
