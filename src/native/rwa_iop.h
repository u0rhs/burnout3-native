// The native IOP command executor for RenderWare Audio (lane audio1; docs/audio.md §3.1, spec docs/audio-irx.md).
//
// Replaces RWA.IRX + LIBSD: the EE library (src/libs/rwa, proven C) sends its SIF command-0 packets here instead of
// to an IOP; the executor applies them as RWA.IRX does and answers with IOP->EE command-1 packets through the
// platform. Time is virtual only: 48 kHz samples given by advance(), never host time. Below it a Spu2Mixer
// (src/audio/spu2_mixer.h) takes libsd-level writes stamped with their sample time; by default a stub mixer.
//
// The platform (boot2: src/native/platform_sif.cpp) owns the EE memory, the IOP heap and the file back end. The
// executor owns the IOP RAM image RWA's addresses point into (2 MB, byte addressed): payloads of send_cmd land there,
// op-3 file reads and op-0x49 / 0x48 work there, uploads read from there. Module-image addresses it hands to the EE
// (file slots, snapshot area, staging buffer) are module_base + RWA.IRX's own offsets; heap addresses (the DMA work
// area, the mixer ring) come from the executor's own IRX-side arena (first fit from heap_base), where RWA.IRX calls
// AllocSysMemory. That arena is the IOP's, apart from the platform's EE-side sceSifAllocSysMemory heap (boot2: base
// 0x100000); RWA.IRX's work area is 0x75700 (heap_base) [C siflog navproof14s1, reply 0xA].
#ifndef B3_NATIVE_RWA_IOP_H
#define B3_NATIVE_RWA_IOP_H

#include <cstdint>
#include <memory>

namespace b3audio { class Spu2Mixer; }

namespace b3native {

struct RwaIopPlatform {   // services the platform gives the executor
    virtual void read_ee(uint32_t ee_addr, void* dst, uint32_t n) = 0;
    virtual void write_ee(uint32_t ee_addr, const void* src, uint32_t n) = 0;
    virtual void reply(const uint32_t packet[12]) = 0;   // an IOP->EE command 1 (the platform calls the EE's cmd-1 handler later)
    virtual int  file_open(const char* name) = 0;         // > 0 handle; <= 0 failure
    virtual int  file_read(int h, void* dst, uint32_t n) = 0;   // bytes read (0 at EOF); negative = retry
    virtual int  file_seek(int h, uint32_t pos) = 0;      // 0 on success
    virtual void file_close(int h) = 0;
    virtual ~RwaIopPlatform() = default;
};

// Choices the spec leaves open; defaults as documented in docs/audio.md §3.1 / the B0 report.
struct RwaIopOptions {
    // RWA.IRX's load address. 0x65130 = where PCSX2 places it (prior art, kRetailRwaLoadBase); the recorded
    // log's addresses agree (file slots 0x702D0 = base + 0xB1A0, snapshot area 0x73230 = base + 0xE100). The image
    // with bss spans [base, base + 0x104E0) (RWA.IRX LOAD MemSiz); the platform's heap must not overlap it.
    uint32_t module_base = 0x65130;
    // The IRX-side heap: first fit from here in 0x100-byte granules, up to the end of IOP RAM (2 MB). 0x75700 = the
    // recorded log's first block (the module image's end, 0x65130 + 0x104E0, rounded up).
    uint32_t heap_base = 0x75700;
    // QueryMaxFreeMemSize() for op 0x13 (reply 0x1B a0). The interface has no heap query; 0x96200 is what a
    // recorded retail run reported.
    uint32_t max_free_mem = 0x96200;
    // Channel-1 completion callback left installed after op 5: every later channel-1 transfer re-sends 0x0D
    // (spec §1, [C 0x7FA8]).
    bool spurious_0d = true;
    // Answer 0x17 with 0x1D even when the mixer is off (spec §10.5 recommendation; retail would hang the EE).
    bool always_answer_0x17 = true;
    // Sound-RAM transfer throughput in bytes per SPU2 IRQ (256 samples); 0 = op 5 / 0x46 / internal writes complete
    // at once (spec §10.6: fine for the EE). [L] RWA moves about 0x18C0 bytes per channel per IRQ.
    uint32_t upload_bytes_per_irq = 0;
    // Reverb work-area clear throughput, bytes per IRQ (the clear is asynchronous; only drops / delays reverb changes).
    uint32_t reverb_clear_bytes_per_irq = 0x3000;
    // The DelayThread(5800 us) between KOFF and KON in a tick with a key-on [C irx_00001CB8 0x16A8], in samples
    // (rounded up). A knob for comparisons only (0: key on with no gap; test B0).
    uint32_t keyon_gap_samples = 279;
    // Latency of one remote file operation (op 0x32), in samples, counted from when the file thread takes it.
    uint32_t file_latency_samples = 0;
};

struct RwaIopStats {
    uint64_t commands = 0, dropped = 0, replies = 0, spurious_0d = 0, unknown_ops = 0;
    uint64_t file_ops = 0, file_open_failures = 0, file_short_reads = 0, file_ops_without_table = 0;
    uint64_t reverb_changes = 0, reverb_changes_deferred = 0, reverb_clears = 0;
    uint64_t snapshots = 0, keyon_delays = 0, mixer_ticks = 0;
};

// A native IOP module that hooks the SPU2 interrupt in front of RWA's handler, as sceSdSetSpu2IntrHandler chaining
// does on the IOP (B3ROUTE, src/audio/b3route.h: docs/audio-b3route.md §2.1). Called at the IRQ's sample time with the
// cores that fired (bit c = core c), before RWA's handler; returns the cores RWA's handler is then called with.
struct Spu2IrqHook {
    virtual ~Spu2IrqHook() = default;
    virtual uint32_t spu2_irq(uint64_t t, uint32_t cores) = 0;
};

class RwaIop {
public:
    explicit RwaIop(RwaIopPlatform& p);                       // with its own stub mixer
    RwaIop(RwaIopPlatform& p, b3audio::Spu2Mixer& mixer, const RwaIopOptions& opt = {});
    RwaIop(RwaIopPlatform& p, const RwaIopOptions& opt);      // stub mixer, options
    ~RwaIop();
    RwaIop(const RwaIop&) = delete;
    RwaIop& operator=(const RwaIop&) = delete;

    void send_cmd(const uint32_t packet[12], uint32_t src_ee, uint32_t dst_iop, uint32_t size);   // sceSifSendCmd(cid 0)
    void advance(uint32_t samples);   // IOP / SPU2 time in 48 kHz samples (virtual time; never host time)

    // Inspection (tests, the platform's sceSifSetDma into RWA's buffers if ever needed).
    uint64_t now() const;                  // virtual time, samples since construction
    uint64_t next_event_time() const;      // boot2: the executor's next own event (its queue, the SPU2 IRQ), in samples
    uint8_t* iop_ram();                    // 2 MB, indexed by IOP address & 0x1FFFFF
    b3audio::Spu2Mixer& mixer();
    const RwaIopStats& stats() const;
    const RwaIopOptions& options() const;

    // Another IOP module beside RWA (B3ROUTE): the SPU2 IRQ chain (nullptr removes the hook) and whether a libsd
    // DMA channel's transfer queue is idle now (sceSdVoiceTransStatus(ch, CHECK) == 1).
    void set_spu2_irq_hook(Spu2IrqHook* hook);
    bool dma_channel_idle(int ch) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace b3native

#endif
