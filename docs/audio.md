# Native audio (S7): RW Audio as C, the IOP command executor, the SPU2-equivalent mixer

Design note, lane audio1 (2026-11-05), for milestone S7 of `docs/standalone.md` (§3 there is the plan this refines).
Decided 2026-11-03 (final): RenderWare Audio's EE library is ported as readable, behaviour-equivalent C and proven
like game code; below it a native executor of the IOP command protocol and a numeric SPU2 model with reverb. No IOP
code and no SPU2 registers are emulated. Goal: voice allocation, priorities, panning and stream state identical to
the PS2. Tags: **[C]** confirmed (artefact cited), **[L]** likely, **[G]** guess.

## 1. Layers and where the line is drawn

```
 game (CB3Sound*, CGtSound*: decomp C, native already)
   | 51 Rwa* entry points, 111 call sites
 RW Audio EE library (702 functions; 424 called in races14)            <- C in src/libs/rwa, proven per function
   objects / interfaces / parameters, virtual voices + voice manager,      (mini_check on captures)
   listeners (3D: distance, Doppler, pan), wave dictionaries, streams,
   SPU2 output + SPU2 voices (EE side: computes every hardware voice's state each tick)
   | RwaRPCTransfer: 0x30-byte SIF command packets + DMA payloads      <- test B0: command stream
 IOP command executor (native C++, replaces RWA.IRX + LIBSD; no SIF)
   applies the per-tick voice snapshot (opcode 0x14), wave uploads into sound RAM, stream file I/O and
   double-buffering, replies (IOP -> EE commands) delivered at defined poll points
   | voice register writes: KON/KOFF, SSA/LSA, pitch, ADSR, volumes, reverb setup
 SPU2-equivalent mixer (native C++)                                     <- test E
   2 x 24 voices: PS-ADPCM, Gaussian 4-tap resampling, ADSR, sweeps, PMON, noise, core chaining,
   saturated bus mixing, reverb; 48 kHz stepped in virtual time (800 samples per 60 Hz tick)
   | SDL3 audio stream (device-queue pacing)
```

Why the line is at the SIF packet [C asm, inventory]: everything above it is EE code that decides *what* plays, and the
EE side already computes each hardware voice's complete state every tick: `_RwaSPU2OutputFinishExecute` (0x2B3110)
sends one 0x680-byte `RwaOutputSPU2DMAStateTag` (opcode 0x14): 48 `RwaSPU2VoiceStateDMA` (0x20 bytes each), the used
flags, dry/wet output masks per core, reverb type / gains / delay / feedback, a packet id [L layout: Revenge stabs;
size [C] 0x2B3190 `addiu a2, zero, 0x680`]. Voice stealing, priorities, pan law, pitch and 3D attenuation are all EE
side (helper findings, audio1: weight 0x294F60, stealing 0x295730, pan 0x2B0CB0, pitch 0x2B1330
[C]). So the IOP side is mostly *apply this state*, upload waves, run streams: small and fixed [L until RWA.IRX is
read] -> RWA.IRX is read: `docs/audio-irx.md` (the executor's spec, per opcode, with evidence). Because the EE
library runs as proven C, the executor's input is exactly retail's packet stream.

## 2. The SIF command surface (what the executor must accept) [C asm `librwaspu2.a rwarpc.o` and callers]

- **Channel**: SIF *commands*, not RPC. EE -> IOP: `sceSifSendCmd(0, packet, 0x30, src, dst, size)`; packets of 0x30
  bytes, opcode at +0x10, arguments +0x14..+0x2C; a payload goes as a DMA chunk of at most 0xFFC00 bytes (more in
  several packets); while the IOP's staging RAM is busy (`_RPCIOPRAMBusy`) transfers of opcodes 7 / 8 queue in a 24-entry
  ring that `_RwaRPCProcessSystemCommand` drains. IOP -> EE: command id 1 into a 24-entry ring (`_RwaRPCSifIntHandler`),
  processed on RWA's own thread (`_RwaRPCUpdateThread`).
- **Opcodes EE -> IOP** (`RwaRPCTransfer` a3; 27 call sites): 0x01 / 0x02 open; 0x04 tick update with data; 0x05 wave
  read to sound RAM (from an IOP buffer); 0x07 bank upload (staged); 0x11 output create (+ reverb cores); 0x12
  destroy; 0x13; **0x14 per-tick voice snapshot**; 0x15; 0x16 / 0x17 mixer callback; 0x2C free voice pool; 0x30 IOP file
  interface; 0x32 stream file ops (seek / read / read to EE / close, 0x70-byte request); 0x46 wave upload from IOP; 0x48
  reinterleave; 0x49 NULL packet; 0x4E DMA channel lock.
- **IOP -> EE**: system replies (version, DMA work area, IOP buffer addresses, completions with callbacks, RAM-busy
  release) and `RwaSPU2OutputCmdHandler` (0x2B3B50) requests, answered with 0x20 / 0x21 / 0x23 / 0x26 / 0x27 / 0x29 /
  0x43.
- **IOP heap**: `sceSifAllocSysMemory` x5 / `FreeSysMemory` x3 (wave upload buffer, stream source, NULL packet).
- Separate module: `B3ROUTE.IRX` (RPC id 0x150276, bound by `CB3SoundDistortion::Construct` 0x2FD108) for the
  distortion effect: a router, no DSP (core 0 wet bus -> EE command 6 -> the game's DSP -> core 1 MEMIN, on a core-1
  IRQ cycle); spec `docs/audio-b3route.md`, native `src/audio/b3route.{h,cpp}` beside the executor.

### 2.1 The protocol as the EE side drives it [C: the ported C in src/libs/rwa (rwarpc, rwaspu2out, rwaspu2remoteio, rwaspu2wave), checked on captures; read in audio1]

**Packet** (0x30 bytes, SIF command 0; the EE's handler is command 1): +0x10 opcode, +0x14 thread or callback, +0x18
EE address, +0x1C IOP address, +0x20 total size, +0x24 arg0, +0x28 arg1, +0x2C last-chunk flag. Payloads go as chunks
of at most 0xFFC00 bytes, one packet each. Opcodes 7 / 8 queue while `_RPCIOPRAMBusy`; opcode 7 reads into the DMA work
area in 0x200000 chunks, continued by system reply 0xF. System replies 0xB-0xE, 0x10 end a transfer: the packet's
callback runs with (arg0, arg1) and the IOP RAM is released.

**Start-up handshake** (`RwaRPCOpen`, `_RwaSPU2OutputCreate`; each step polls for the IOP's answer): op 1 -> system
reply 9 (arg0 = version 0x115); op 2 -> reply 0xA (arg0 = DMA work area, its +0x400 = wave upload data; arg1 = the
remote-file buffer); **0x11** (p6 = voices, p7 = reverb cores: bit 0 core 0, bit 1 core 1) -> **0x1A**; **0x13** ->
**0x1B** (arg0 = IOP memory free, arg1 = used); **0x15** (p6 = the EE reply area, state + 0xD00, 0x680 bytes) ->
**0x1C** (arg0 = where the IOP wants the snapshots). Then sound RAM 0x1C0000 / 0x1E0000 (0x20000 each) is locked per
reverb core. Destroy: 0x12 -> 0x12.

**Per tick: the 0x680 snapshot** (opcode 0x14, `_RwaSPU2OutputFinishExecute`): sent only when the reply's PacketID
equals `_rwaSPU2UpdateCount` (one snapshot in flight): `RwaRPCTransfer(DMAStateIOPAddr, &DMAStateToIOP, 0x680, 0x14,
p6 = PacketID)`. Layout = Revenge's `RwaOutputSPU2DMAStateTag` (every offset confirmed):

| Offset | Content |
|---|---|
| 0x000 | VoiceState[48], 0x20 bytes each; hardware voice n: core = n >= 24, voiceNum = (n % 24) * 2 \| core, bit 1 << (n % 24) |
| 0x600 | UsedVoices[48] |
| 0x630 / 0x638 / 0x640 / 0x648 / 0x650 | per core: dry L / dry R / wet L / wet R / voice-switch masks |
| 0x658 | ReverbType (§ reverb below) |
| 0x65C / 0x65E | ReverbGain L / R (s16) |
| 0x660 | Gain (0xFFFF = 1) |
| 0x664 | SpeakerConfig |
| 0x668 / 0x66C | Delay / Feedback |
| 0x670 | PacketID |

Per voice: +0x00 PlayState (EE writes 2 = key on, 1 = key off, 5 = idle on an attached voice; on sending, 2 becomes 4
= playing with CurrentPosition = TriggerPosition and the trigger cleared); +0x04 StartAddress (sound RAM: base + 0x80 *
unit); +0x08 LoopStartAddress (= start, or start + the loop offset with update flag 0x200); +0x0C Frequency (int)(Hz *
FrequencyScale3D): **Hz, not a pitch register** (the IOP converts); +0x10 Volumes (int)(0x7FFF * L * vol) << 16 \|
(int)(0x7FFF * R * vol); +0x14 CurrentPosition; +0x18 TriggerPosition; +0x1C Flags (1 loop, 2 / 4 phase invert for a
Dolby rear voice). **What the IOP must write back** in the reply area (read uncached, `| 0x20000000`): per voice
PlayState 1 when the voice ended, CurrentPosition while it plays, and the PacketID at +0x670 as the acknowledgement.
A Dolby partner's mask bit is 1 << ((n - 24) & 31), so a core-0 partner gets bit n + 8 (a retail bug, kept).

**IOP requests** (`RwaSPU2OutputCmdHandler`, called with (state, op, arg0 = packet+0x24, arg1 = packet+0x28, thread =
packet+0x14)): 0x14 / 0x39 nothing; odd 0x2B-0x41 and 0x4B-0x51 replies (`*(u32 *)arg0 = arg1`, WakeupThread(thread)
when > 0); 0x33 / 0x47 call `thread` as a function (arg0, arg1): remote-file completions; **0x19 tick**:
TickUpdate(iop = arg0, size = arg1), the mixer callback fills bus 0, back with op 4; 0x1A / 0x12 create / destroy done;
0x1B memory; 0x1C snapshot address; 0x1D mixer off done; 0x1E voice + RAM request (reply 0x20); 0x1F grant `count`
voices in a core range and `size` bytes of sound RAM (reply 0x21 with the 0x40-byte block: masks +0x18 / +0x1C,
address +0x20, handle +0x24); 0x22 release (reply 0x23); 0x24 / 0x25 / 0x28 input areas (replies 0x27 / 0x29); 0x42
stream sync (calls block+0x18 when block+4 == 6; reply 0x43 with the 0x80-byte block).

**Remote file operations** (opcode 0x32): 0x70-byte requests at IOP buffer + index * 0x70, p6 = index (0-2; handle =
index + 1): +0x00 index, +0x04 EE buffer, +0x08 size, +0x0C file position, +0x10 operation (1 close, 2 read to EE
(EE flushes its cache afterwards), 3 read into the upload buffer for sound RAM, 4 seek), +0x14 EE callback
(`_rwaSPU2RemoteIOPFopsEECallback(result, index)`, invoked by op 0x33 / 0x47), +0x18 EE address of the result word, +0x1C
flags (2 open, 4 in use, 0x10 pending), +0x24 file name (0x4C bytes). Streaming into sound RAM is two calls: an op-3 read
of up to the upload buffer (0x10000 by default), then opcode **5** with the bytes rounded up to 0x40 to sound RAM +
offset. 0x46: an IOP -> sound RAM upload {offset, sound-RAM address, IOP source, bytes, target length}.

**Reverb** (`_rwaSPU2OutputSetEnvironment`, I3DL2-style parameters): room -10000 -> type 1 (off); diffusion < 100 and
reflections < 0 -> echo, Delay = 127 * reflectionsDelay / 0.3, type 9 if reverbDelay < 0.05 else 8 with Feedback = 127
* 10^(reflections / 1000); otherwise by decay time / HF ratio: <= 2 s: HF <= 0.5 -> 3 else 2; <= 3 s: reflections < 401
-> 4 else 10; <= 4 s: HF <= 1 -> 5 else 6; longer: HF <= 0.5 -> 6 else 7. Gain L = R = 0x7FFF * 10^(room / 1000). So
the mixer's reverb is the SPU2's preset set (types map to libsd's modes [L]) plus echo delay / feedback.

**Sound RAM**: 2 MB in 0x80-byte units, first fit, the first 0x5400 bytes reserved; a handle = start unit | count <<
shift. Sample counts: VAG 28 samples per 16 bytes (halved for stereo).

### 2.2 Streams as the EE side drives them [C: the ported stream C (rwastreamsource, rwastreamcontai, rwawaverenderer, rwstream, rwastreamfilequ), checked on captures and synthetic variants; read in audio1]

- **The ring**: on the captured run, music streams use the IOP mode (source +0x18C == 2). The ring buffer is in IOP
  memory (+0x150 = 0x106000, from `sceSifAllocSysMemory`): 16 blocks of 0x2000 bytes. Reads are queued 8 blocks at a
  time (+0x198), with 8 blocks kept free (+0x19C).
- **One file operation in flight per file**, in strict queue order: a request goes out only when
  `RwaSPU2RemoteIOPFopsPreviousOperationComplete(file, 0)` returns 1 and no source operation flag (0x5FF0 / 0x40000) is
  set. The queue is a ring of 0x14-byte entries {0, buffer, bytes, file position, 1 seek / 4 read}.
- **Read** = 0x32 op 3: `RwaSPU2RemoteIOPFopsRead(file, iopRing + idx * blockSize, 0x1000, bytes, 0, cb, src)`.
  - A read never crosses the ring's end (it is split there).
  - Open: the third argument (0x1000) is added to the request's +0x0C "file position" by the remote-ops code. For an
    IOP-memory destination that word may mean something else; RWA.IRX decides.
- **Seek** = op 4: `Seek(file, segment+0x1C + desc+0x24 + block * blockSize, cb, src)`. It happens at open, on loop
  wrap (no blocks left and loop flag 0x8000) and on SetStreamPos.
- **Completion** (the executor's job): callback(handle, bytes read, status, kind, arg). Status 4 is an error; the bytes
  must be whole blocks, because the EE marks one block ready per blockSize and starts the next queued request from
  inside the callback.
- **Opening a stream** reads through the RwStream reader with op-2 reads to EE:
  - chunk 0x80D, then 0x80E, then its 0x14-byte header, then the id and name (compared with the wanted UUID or name),
    then the description; a mismatch skips to the next 0x80D;
  - after `_rwaStreamFormatCreateFromPack` it seeks past the chunk and reads the 0x80F header.
  - The reader works in 0x800-byte sectors through a one-sector cache (a direct read when the destination is 64-byte
    aligned and at least 0x800 bytes), with an op-4 seek to `pos & ~0x7FF` before any read away from the current
    position.
- **Renderers feeding voices**: renderers on one list (state +0xB0) sync once per tick by RwaCoreGetTickTime.
  - Start: the feed voices start only when every renderer has samples queued (+0x60 != 0).
  - Packet pull: a renderer pulls a new packet only when all have taken the same number, or when it is behind.
  - Packet release: a packet goes back upstream only when no IOP upload is pending (`_RwaSPU2WaveGetUploadFromIOPPending`,
    one global flag: one IOP upload at a time across all feeds).
  - Uploads: `_RwaOsWaveUploadForRenderer(feed, block * bytesPerBlockPerFeed, packetData + feedIdx *
    bytesPerBlockPerFeed, bytesPerBlockPerFeed)`, one per feed per block.
- **What the executor must keep current**: clear the upload-pending flag on completion (the 0x46 reply), and keep each
  feed voice's state block up to date: +0 state (1 stopped, 2 playing, 3 treated as stopped), +0x14 samples played
  (wrapping over the ring). The EE writes +0x18 (the start sample) on Start.
- **`_rwaStreamGetStreamPos`** (read by game code at 4 sites):
  - played = (voice state 1 or 3) ? the kept position (+0x54) : voice +0x14;
  - block = played / samplesPerRingBlock (+0xA4; 0x1C00 in the capture, 2 feeds);
  - result = tracking entry[block] (0x10 bytes from the packet's position at upload time) with its third word +=
    played - block * samplesPerRingBlock, accumulated into the renderer's copy at +0x6C.
  - So what the game sees per tick depends on when the executor updates voice +0x14. It must follow RWA.IRX's
    timing.

## 3. Interfaces (native side)

```cpp
namespace b3audio {
// One EE->IOP command as RwaRPCTransfer builds it (the 0x30 packet) plus its DMA payload (if any).
struct RwaCommand { std::uint32_t words[12]; std::span<const std::byte> payload; std::uint32_t dst; };
// The IOP side of RWA, natively. No SIF, no IOP memory map: its "IOP RAM" is a host buffer with the same
// addresses the EE side was told (system reply), so EE code that stores IOP addresses keeps working.
class RwaIop {
public:
    void submit(const RwaCommand& c);              // called by the platform's sceSifSendCmd(0, ...) replacement
    void tick(std::uint32_t samples);              // advance IOP time: stream workers, deferred uploads
    bool next_reply(RwaCommand& out);              // IOP->EE command id 1, delivered by the completion rule
    VoiceTrace& trace();                           // per-tick register-level record
};
// The SPU2-equivalent: voices as objects, no register file.
class Spu2Mixer {
public:
    void write_voice(int core, int voice, VoiceParam p, std::uint32_t value);   // pitch, ADSR1/2, VOLL/R, SSA, LSA
    void key_on(int core, std::uint32_t mask);  void key_off(int core, std::uint32_t mask);
    void write_core(int core, CoreParam p, std::uint32_t value);                 // MVOL, EVOL, reverb, VMIX, PMON, NON
    void upload(std::uint32_t sound_ram_addr, std::span<const std::byte> adpcm);
    void render(std::int16_t* stereo, std::uint32_t frames);                       // 48 kHz
};
}
```

### 3.1 The executor as RWA.IRX does it (spec: docs/audio-irx.md; build order)

**State.** Rather than an IOP memory map, the executor keeps the module's observable state:
- the snapshot / state area (the address the EE was told in reply 0x1C), with a 0x680-byte mirror;
- three remote-file slots (0x70-byte requests, the EE writes them through opcode 0x32's DMA);
- the upload staging buffer;
- sound RAM (2 MB, byte addressed as libsd's);
- per hardware voice: the last applied pitch / ADSR / volume / loop address;
- the reverb cache (type, gains, delay, feedback per core);
- the mixer ring when op 0x16 enabled it.
IOP addresses handed to the EE (work area, file slots, snapshot area) are fixed constants chosen once.

**Time.** One virtual clock: the SPU2 core-0 IRQ every 256 samples at 48 kHz (5.33 ms). Every IOP-side delay is
counted in this clock and in sample time, never in host time:
- the 5.8 ms key-off-to-key-on gap of a tick with a key-on;
- the ~83 µs loop-address fix-up;
- op 0x19's mixer cadence (one tick per MixerBufferLength samples while the mixer is on);
- upload and file completion latencies.
Replies are queued with their due time and delivered at the EE's poll points in due order (the standalone's
scheduler, standalone §2.2).

**Per opcode** (§1 of the spec, in its order):
- 1 / 2 / 0x11 / 0x12 / 0x13 / 0x15 / 0x17 / 0x2C / 0x30 / 0x4A / 0x4C / 0x4E / 0x50: state plus the fixed reply.
  0x13 reports QueryMaxFreeMemSize as a constant 0x100000.
- **0x14, the voice snapshot**, in RWA.IRX's order:
  1. per voice: key-off of PlayState 1 / 2;
  2. SSA = Start + Trigger * 16 / 28; ADSR1 = 0x00FF (0x1BFF with a trigger), ADSR2 = 0x0007;
  3. pitch = (Hz << 12) / 48000 (signed divide, 16 bits; only on change, not in state 5);
  4. master / reverb / VMIX;
  5. KOFF, the gap, the keyed-on voices' volumes ((+-v >> 1) & 0x7FFF; Flags 4 / 2 invert L / R), KON;
  6. the write-back: PlayState 2 -> 4, CurrentPosition (Trigger on the key-on tick, then (NAX - Start) * 28 / 16),
     PlayState 4 -> 1 when ENVX == 0, PacketID echoed;
  7. the whole 0x680 bytes back as op 0x14;
  8. LSAX = LoopStart or 0x5010 (the silent end block).
  Volume sweeps while playing come from the IRQ-driven sweep (every second IRQ, the rate table of §2.5).
- 0x05 / 0x46 uploads: into sound RAM, blocking the command queue until done. Reply 0xD / 0x47; 0x46 patches the VAG
  flags into a looping ring first. Keep the spurious 0x0D after later channel-1 transfers (harmless, observable).
- 0x32 file ops: one FIFO worker over the platform file back end, as RWA's stdio-shaped table.
  - Op 3 reads at the current position, rounded up to 0x800; +0x0C is ignored.
  - The completion is 0x33 with the *requested* size (seek: the new position; close: 0); op 2's data comes with it.
- 0x16 / 0x48 / 0x49 / 0x36 and the even 0x34-0x40: no reply. 0x49 = memset of the NULL packet.
- Dead in this module (handled but never sent by the IOP): 0x1E / 0x1F / 0x22 / 0x24 / 0x25 / 0x28 / 0x42.
- The receive ring drops packets beyond 8 outstanding. The EE never has more than a few in flight [L]; the B0 log
  will show it.

**The B0 log** (a logged command stream of the navproof14s1 session, `$P/siflog/navproof14s1.txt`, 54 MB):
- 19,034 EE -> IOP commands: op 5 x 12,289 packets, 0x14 x 5,440, 0x46 x 982, 0x32 x 312, plus the one-time
  start-up ops;
- 5,440 reply-area snapshots;
- IOP -> EE replies: 0x14 x 5,440, 0x47 x 982, 0x33 x 312, 0xD x 154 (op 5's completions, one per transfer [L]),
  0x1A, 0x1B, 0x1C, 0x31, 0x4F, 9, 0xA.

Every reply pairs with its command as the RWA.IRX spec says [C].

**Status: the protocol layer with a stub mixer is built** (audio1, 2026-11-06):
- Code: src/native/rwa_iop.h (the interface agreed with boot2, whose platform_sif implements RwaIopPlatform),
  src/audio/rwa_iop.cpp, spu2_mixer.h, spu2_stub_mixer.*.
- One discrete-event loop in virtual time: the RPC thread with its 8-entry ring, the IRQ every 256 samples, the file
  thread, transfers.
- Tests:
  - `test_rwa_iop`, in `make test`: synthetic.
  - `make test_rwa_iop_b0`: the B0 replay of $P/siflog/navproof14s1.txt; skips without the log.
- **The executor's defaults follow RWA.IRX** (our rule: PS2-identical): the 5.8 ms key-off-to-key-on gap, the
  spurious 0x0D after channel-1 transfers, 0x1A arg0 and 0x31 arg1 = 0 [C 0x93C4; neither is read by the EE].
  `RwaIopOptions` switches the gap and the spurious 0x0D off as a test mode, like the frozen COP0 Count.
- Choices where the spec is open (the helper's list, recorded in rwa_iop.cpp):
  - module base 0x65130, as PCSX2 loads it;
  - QueryMaxFreeMemSize 0x96200;
  - delays rounded up to whole samples;
  - the sweep on even IRQs [G];
  - guards where retail would hang (file read retries, failed opens, op 0x48 under 4 bytes).

**Status: the SPU2-equivalent mixer** (audio1, 2026-11-06): src/audio/spu2_mixer_impl.{h,cpp},
spu2_tables.inc.
- What it models:
  - PS-ADPCM, 4-tap Gaussian (psx-spx's table), ADSR, volume sweeps, PMON, noise;
  - MEMIN, core chaining, the 24 kHz reverb with psx-spx's FIR;
  - IRQs on voice fetches / transfers / the ring.
- The executor's clock now comes from the mixer's IRQ (`run()`), with the stub or the real mixer.
- **libsd's reverb presets are Sony data: never in the repo.** `load_libsd_reverb_presets(path)` reads them at run
  time from the game's own IOP/LIBSD.IRX (.data 0x4A68 sizes, 0x4A90 rows; FNV-1a 64 check). Without them every
  reverb mode keeps a silent wet path. The standalone loads them at start-up from the disc; tests skip their reverb
  checks when the file is absent.
- Tests in `make test`:
  - test_spu2_mixer: 14 tests, 75 checks; reverb skipped without the game files;
  - test_rwa_iop runs every case with both mixers.
- Where sources disagree, what it chose:
  - LSAX latch: the SPU2 manual's lock after a software write;
  - one-shot end: psx-spx (to LSAX, ENDX, mute);
  - pitch clip 0x4000; Gaussian index bits 4-11; ADPCM shift 13-15 as 9;
  - libsd's ECHO / DELAY presets over psx-spx's rows.
  Full list in spu2_mixer_impl.cpp's header.

**Mixer interface.** The executor emits libsd-level writes (SetParam / SetAddr / SetSwitch / SetCoreAttr /
SetEffectAttr / VoiceTrans), each stamped with its virtual sample time. `Spu2Mixer` consumes them; the executor reads
back NAX and ENVX from the mixer at the write-back step, which keeps the two halves in step without host timing.

**Build order:**
1. **The protocol layer with a stub mixer**: NAX advanced by pitch * elapsed samples, ENVX by a reduced ADSR. Tested
   offline against the B0 log: replay the S lines, compare
   the replies (P lines: opcodes, arguments, order) and the reply area per snapshot (A lines: PlayState and PacketID
   exactly, positions within the stub's precision).
2. **The SPU2-equivalent mixer** (ADPCM, Gaussian, ADSR, sweeps, reverb), test E.
3. **The standalone wiring**: sceSifSendCmd and the handler replaced at the platform layer; replies at poll points.

### 3.2 The standalone's audio output (src/native/audio_device.*)

- `b3native::AudioDevice`: an SDL3 playback stream, S16 stereo 48 kHz, on the default device. Without a device the run
  continues silent (logged once).
- `b3audio::pump(iop, mixer, device, samples)`: one virtual frame. It advances the executor by the frame's samples
  (from virtual EE time) and pushes everything the mixer rendered. Content is deterministic; only the device's
  consumption is host-timed.
- **Pacing: the platform loop's frame pacer stays the master.** Audio as the master clock would couple frame timing
  to the device and fight docs/frame-rate.md's uncapped rendering.
- **The output queue (audioq, 2026-11-22; src/native/audio_queue.h).** The game mixes 800 frames a virtual VBlank,
  so 47952 frames a second at the paced 59.94 Hz, while the device plays 48000 on its own clock: a steady 0.1 % gap,
  plus what the host's pacing adds (a catch-up after a hitch) or misses (a stall, the window's focus hold). Until
  1.3.0 `regulate()` kept SDL's queue near 60 ms by dropping or padding whole 800-frame frames: the 0.1 % gap alone
  ran the queue dry about once a minute, and each drop or pad was a cut in the sound or a 17-50 ms hole with hard
  edges (a player's Windows log, 284 s: 12 drops and 13 pads). Now the device's stream pulls from our own queue (an
  SDL get callback) and:
  - **rate**: the stream reads the queue a fraction of a percent slower or faster (`SDL_SetAudioStreamFrequencyRatio`,
    SDL's resampler, at most 0.5 % off: under 9 cents), from a P + I control of the queue's smoothed level against
    the target (the integral only near it, so a burst does not wind it up). It settles at 47952 / 48000;
  - **late**: when the queue holds less than the device asks for (the game thread late), the frames it has fade out
    (the last 128 at most, 2.7 ms), then silence until the queue holds the target again, which comes in with a
    128-frame fade in (no click either way). A device pulling large blocks may leave the queue low between pulls:
    that is not a starve. audioq2: when the game is late (no push for 25 ms of output) and a block would leave fewer
    than 128 frames, the starve begins at that block's end, its last 128 frames faded (the few left over, which would
    play after the gap, are dropped), so the fade is never a few frames' cut;
  - **burst**: above target + 150 ms the oldest frames are dropped down to the target with a 128-frame crossfade.
  The target is 60 ms, at least the device's block plus 128 frames and two VBlanks (WASAPI's shared period is 480
  frames, 10 ms; a device pulling 2048 / 4096 / 8192 frames gets 3776 / 5824 / 9920, and never starves while the game
  is on time, one late VBlank included: `queue_large_device_blocks`). The rate's reach is 0.5 % on purpose: a game
  running slower than that for long (a PC that cannot hold 59.94 VBlanks a second, so the game itself is slow) still
  empties the queue now and then, each time a fade out and in (where the old guard padded hard-edged silences); a
  wider reach (3 %) would be half a semitone of pitch drift in the music, to hide a game already running slow. The
  content is not changed and the game sees nothing of it (offscreen runs have no device). Measured with
  `tools/native/audio_sim.cpp` (a captured race mix through both on the same timeline: 59.94 Hz pushes with 4 ms of
  jitter, a 120 ms and a 300 ms stall, a 10 ms 480-frame device): the old guard's sound/silence edges cut the sound
  at up to 9129 (of 32767), the queue's at 37; 19200 frames dropped in 800-frame cuts against 7520 crossfaded.
  Tests: `test_audio_device` (`queue_rate_follows_the_game`: 3 minutes at 59.94 Hz, no underrun, the ratio within
  0.03 % of 47952 / 48000; `queue_stall_fades_out_and_in`, `queue_burst_is_crossfaded`: no step larger than the
  tone's own; `queue_pull_at_the_edge`: pulls at n + 1 and n + 127 queued, nothing written past the block, also clean
  under ASan / UBSan; `queue_large_device_blocks`). The device's callback fills a buffer sized at the open, in pieces
  (no allocation on the device's thread).
- **The output's clipping (audioq).** The SPU2 clamps every sum to 16 bits (the mixer does as the hardware): a race's
  loud moments (a crash over the engines and the music) sum far past full scale and the clamp holds the wave at 32767
  for up to a millisecond (a captured race: 341 runs in 38 s, 0.05 % of the samples at full scale, RMS -11.8 dBFS),
  heard as crackle. A static soft knee does not help (the sums overshoot so far that it flattens too): the host's
  output (core 1's final output, which nothing game-visible reads: no SPU RAM ring, no register) is now made from the
  same sums without the dry and output clamps and goes through a peak limiter (`Spu2MixerImpl::Limiter`): it runs 1 ms
  (49 frames) behind the mix, the two channels' gain ramps down within that millisecond to keep the louder at 29000
  when its peak comes, and comes back over 80 ms; the last safety is a soft knee (the identity up to 29000, tanh above).
  Where the mix stays in range it is the hardware's values, 1 ms later; the SPU RAM rings, the reverb's input and
  every reply to the game are the hardware's as before (the golden's audio logs are RW Audio's commands and replies).
  PC SETTINGS > AUDIO > OUTPUT > SOUND OUTPUT chooses: Clean (the default, the limiter) or Original (the hardware's
  clamped output with no delay, the old sound; ini `sound_output` 0 / 1, row id 93; native_play passes it to the
  mixer each VBlank: `b3r_sound_output`, `platform::set_sound_output_original`). `B3_AUDIO_HARD_CLIP=1` forces
  Original (the exact-timing tests use it). audioq2: switching it starts the limiter's 1 ms of look-ahead afresh (no
  frames from before the switch are played after it). Tests: `test_spu2_mixer` (`output_limiter`: 2x over full scale comes out at most 29055 with no repeated crest
  samples, released in quiet to the identity; `soft_limit_curve`; `memin_through_both_cores` both ways).
- **The voices' resampling (checked, audioq).** The SPU2's Gaussian 4-tap from psx-spx's table (as PCSX2), checked
  as THD+N of a tone played from its own rate at 48 kHz (`test_spu2_mixer voice_resampling_quality`): 1 kHz from 22050,
  32000 or 44100 Hz sources -68 to -72 dB (linear interpolation -50 to -62, nearest -22 to -28); high tones near the
  source's Nyquist -29 to -36 dB (the Gaussian's own roll-off and images, the hardware's). The music is the game's
  32 kHz PS-ADPCM (4 bits a sample): its noise floor is the disc's, not the port's (CUSTOM MUSIC plays the player's
  own files).
- Start-up: `b3audio::load_libsd_reverb_presets(<disc>/IOP/LIBSD.IRX)`.
- Test: `make test_audio_device` (SDL3's dummy driver; not in `make test`, like `make render`).
  - Linux note: the SDL3 package's libpulse has its own RUNPATH (/usr/lib/x86_64-linux-gnu/pulseaudio), which hides
    the executable's RPATH for libpulse's own dependencies. Runs set LD_LIBRARY_PATH to the package's pulseaudio
    folder.

- **Placement**: `src/audio/` (executor, mixer, RWS reader), host-only C++; the EE C stays in `src/libs/rwa`.
- **Threading** (standalone §2.2): the game thread runs the EE library and calls `submit`; `RwaIop::tick` runs at the
  frame scheduler's tick points (virtual time); replies are applied at the start of a poll, in due order.
  RWA's own thread (`_RwaRPCUpdateThread`, `_RwaThreadCreate`) becomes a function the scheduler runs at those points.
  Rendering samples (mixer) may run on a host worker from a lock-free queue of register writes stamped with the tick.
- **Streams**: `.RWS` (0x080D containers, PS-ADPCM, 32 kHz stereo music) and `.AWD` banks are read through the
  platform's file back end; the executor's 0x32 handling serves the EE's stream source exactly as RWA.IRX does
  (request -> data into the agreed buffer -> completion reply). `_rwaStreamGetStreamPos` is read by game code (4 sites):
  its value per tick must match the PS2's, so completion timing follows the completion rule.

## 4. Test plan (standalone §3.3, with what each needs)

| Level | Compared | Against | Tolerance | Needs |
|---|---|---|---|---|
| **A0** EE C, per function | v0/f0/all RAM per capture | retail in r5900_mini (mini_check) | exact | captures: `a1races14` (1 per called function) + `a1races14-deep` (8 per function, every 211th call) [C, done] |
| **B0** command stream | every EE->IOP packet (48 bytes + payload hash) and IOP->EE reply, with poll index | the B0 log (`make test_rwa_iop_b0`) | exact (EE side is proven C) | the log |
| **E** reverb | tails on chosen scenes | PCSX2 recording or a real PS2 capture | by ear + spectral check [G] | the capture |

Order: A0 first; B0 gives the executor its input/output pairs offline, so the executor can be built and tested
without a game run (replay the logged packets, compare its replies). Then the mixer against E.

## 5. Open items found so far

- **B3ROUTE's distortion** (docs/audio-b3route.md; the native build is PS2-faithful; in the shipped game since engpan,
  2026-11-18, §8 there):
  - In play, the distortion runs **throughout every race**:
    - `CB3SoundGameModeManager::Update` calls StartDistortion every frame in race states (state byte +0x52CC >= 0xB,
      race cars > 0) [C 0x289D3C];
    - in navproof14s1: SetChannels(mask 0xFF) once, StartDistortion 4,071 times, polls 1206-5517, i.e. the whole race
      [C the B0 log's `D` lines].
  - The routed voices are the high engine-sound model's (SetChannels from `CB3SoundESMHigh::Start` 0x287DC0) [L: the
    mask's source not traced].
  - CB3SoundDistortion::Update positions the effect from the player car against the listener (pan and distance gains,
    `CalculateGainPair` / `CalculateDistance`). The EE's DSP (`CB3SoundDSPDataBlock::Process`,
    `ProcessB2Distortion`) shapes it.
  - **So on a PS2 the player's car engine is heard through this EE distortion and EE panning, 21.3 ms later, and not
    on the dry bus (VMIX cleared for the masked voices).**

- **Capture depth** [C helper, fault injection]: with one capture per function several deep paths (voice stealing,
  3D, Dolby) are never exercised by A0; the deep capture run covers them.
- **COP0 Count: a platform-timer decision for the standalone** [C]. `RwaTimerGetValue` (0x293540) and func_00293550 /
  func_00293560 read `mfc0 $9` (Count); `RwaTimerGetFrequency` returns 0x11940000 (294.912 MHz).
  `_rwaVoiceManagerUpdateTimeStamp` (0x295B30) turns Count deltas into milliseconds (`lasttime$49` 0x4E2B30), and
  `_rwaVoiceManagerUpdate` advances every voice that holds no hardware voice by elapsed ms x frequency / 1000 (it wraps
  if looping, else stops at the end) [C helper reading, 0x295730].
  - With a frozen Count, virtual voices never advance. On a PS2 they do, so which voices get stolen / restarted would
    differ from the hardware.
  - The standalone must supply these three timer functions from the platform tier. Two options:
    - (a) frozen;
    - (b) virtual EE time (ticks x 294,912,000 / 60): what the PS2 does.
  - **Decision (2026-11-05)**: the standalone uses (b), virtual EE time (PS2-like), as the product behaviour;
    (a), a frozen Count, is a test mode only. Count is never host
    wall time (replays stay deterministic).
- **A stack local's address stored in a struct** (`RwaVoiceManagerRegister` 0x2954E0: the bump cursor at sp+0xAC in the
  creation parameters): settled by decomp2host T27 (`B3_RETAIL_FRAME` / `B3_FRAME_LOCAL`, retail's own frame on the
  guest stack), used by it and by the three container functions, RwaObjCreate and _RwaWaveConvFindPath.
- **RWA.IRX** is read: docs/audio-irx.md. Its open questions: NAX granularity, the 0x19 ring half, the op-0x30 file
  table's target, sound RAM 0x5000-0x500F after init, libsd's reverb presets, upload throughput.

## 6. Writing the EE C so decomp2host keeps retail's layouts (lessons from batches 1-3)

- **Locals whose address reaches guest code must have guest layout.** A host `T *x` passed as `&x` is 8 bytes;
  `RwaObj *search[2]` handed to `RwaObjEnum` was 16 bytes of host pointers (RwaWaveCreateLoadedWave: checker DIFF).
  Use `RwUInt32` words and cast (`(unsigned int)p`, `(T *)w`); pointer *parameters* and struct members are fine (T3,
  T3b make them `gp<T>`).
- **Handles are not addresses.** Sound-RAM handles (start | count << shift), SPU2 / IOP addresses: `RwUInt32`. A
  `void *` holding one is range-checked as a guest pointer by the wrappers and stubs (checker ERROR "guest pointer
  outside RAM" once the value is >= 32 MB; silently accepted below that).
- **Integer casts of pointers**: write `(unsigned int)p`, not `(RwUInt32)p` (T9 recognises the builtin names).
- **Shared types go in a header.** A struct a unit defines lands in that unit's namespace (T7); a header's forward
  declaration of the same name is another type (RwaUUID: "parse error in template argument list").
- **No const pointer results** (`gp<void>` from `const T *` does not convert): return `T *`.
- **Stale registers are behaviour.** Retail sometimes returns or tests whatever v0 held (an accessor's unwritten stack
  slot, a skipped callback's result). Each such place is commented with the address and the choice made [L].
- **Tail jumps keep a1-a3**: a C call from native code starts from the registers the override was entered with
  (call.h pass-through), so `j callee` ports as `return callee(a0)` even when the callee reads more arguments.
- **The checker**: one early capture per function is shallow; spread-out captures (8 per function, every 211th call)
  reach deeper paths. `mini_check --kernel --sif` models the kernel and SIF calls that leave EE memory alone.
