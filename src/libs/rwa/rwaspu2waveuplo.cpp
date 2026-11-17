// rwaspu2waveuplo.o (librwaspu2.a) -- wave uploads the IOP performs (sound RAM from IOP memory, reinterleaving).
// Retail 0x2B5160-0x2B52C4 [C E map]. C port written from retail's code (audio1); proven by the checker on live
// captures where they exist.
#include "rwa/rwacore.h"

// The upload request (opcode 0x46 to the IOP's wave-renderer upload area), 0x14 bytes [C 0x2B5218-0x2B5260].
struct UnkStruct_01E83650 {
    RwUInt32 field_0;             // 0x00 offset in the wave
    RwUInt32 field_4;             // 0x04 sound-RAM destination (the wave's address + offset)
    RwUInt32 field_8;             // 0x08 source (IOP)
    RwUInt32 field_C;             // 0x0C bytes
    RwUInt32 field_10;            // 0x10 the wave's target length (wave + 0x34)
};
typedef void (*UnkFn_002B52C0)(RwUInt32 arg0, RwUInt32 arg1);

extern UnkStruct_01E83650 D_01E83650;
extern RwBool _rwaGRwaSPU2WaveIOPUploadPending;
extern RwUInt32 _rwaGRPCWaveRendererUploadData;
extern "C" void SyncDCache(void *start, void *end);
extern "C" RwUInt32 _rwaSPU2WaveRAMGetAddress(RwUInt32 handle);
// Here p5 carries an EE function (called back by system opcodes 0xB-0x10) [C 0x2B5280].
extern "C" void RwaRPCTransfer(RwUInt32 iopAddr, void *ee, RwUInt32 size, RwUInt32 op, RwUInt32 unused,
                               UnkFn_002B52C0 p5, RwUInt32 p6, RwUInt32 p7);

// The upload's completion: no longer pending.
// 0x002B52C0
extern "C" void func_002B52C0(RwUInt32 arg0, RwUInt32 arg1)
{
    _rwaGRwaSPU2WaveIOPUploadPending = 0;
}

// 0x002B51F0
extern "C" RwaWave *_RwaSPU2WaveUploadFromIOP(RwaWave *wave, RwUInt32 offset, RwUInt32 src, RwUInt32 size)
{
    D_01E83650.field_0 = offset;
    D_01E83650.field_4 = offset + _rwaSPU2WaveRAMGetAddress(wave->data);
    D_01E83650.field_8 = src;
    D_01E83650.field_C = size;
    D_01E83650.field_10 = wave->targetFormat.length;
    SyncDCache(&D_01E83650, (char *)&D_01E83650 + 0x13);
    RwaRPCTransfer(_rwaGRPCWaveRendererUploadData, &D_01E83650, 0x14, 0x46, 0, func_002B52C0, 0, 0);
    _rwaGRwaSPU2WaveIOPUploadPending = 1;
    return wave;
}

// 0x002B51E0
extern "C" RwBool _RwaSPU2WaveGetUploadFromIOPPending(void)
{
    return _rwaGRwaSPU2WaveIOPUploadPending;
}
