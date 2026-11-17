// rwaspu2wave.o (librwaspu2.a) -- SPU2 waves: sound RAM (2 MB in 0x80-byte units, the first 0x5400 bytes reserved
// for the system; optional allocator hooks D_01E82A00/04/08), the input-area blocks, and the SPU2 wave definition's
// callbacks. Retail 0x2AFA20-0x2B074C [C E map]. C port written from retail's code (audio1); proven by the checker on
// live captures where they exist.
#include "rwa/rwacore.h"
#include "rwa/rwaspu2.h"

typedef RwUInt32 (*UnkFn_01E82A00)(RwUInt32 size);     // allocate -> handle
typedef void (*UnkFn_01E82A04)(RwUInt32 handle);       // free
typedef RwUInt32 (*UnkFn_01E82A08)(void);              // free bytes
extern UnkFn_01E82A00 D_01E82A00;
extern UnkFn_01E82A04 D_01E82A04;
extern UnkFn_01E82A08 D_01E82A08;
extern UnkStruct_002B4370 D_01E83210;      // the sound-RAM bitmap (base 0, 0x200000 bytes, unit 0x80)
extern RwUInt8 D_01E82A10[0x800];          // its bits (0x4000 units / 8)
extern RwUInt8 _rwaG_rwaSPU2InputAreaMemoryBitmap;
extern RwUInt32 _rwaSPU2DataInBuffer;      // bytes the last remote read delivered (0 = none yet)
extern RwUInt32 _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBuffer;      // EE buffer address
extern RwUInt32 _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBufferSize;
extern RwaUUID rwaSPU2WAVEID;
extern RwaUUID rwaWAVEFORMAT_PCM;
extern RwaUUID rwaSPU2OUTPUTID;
extern RwaUUID rwaWAVERENDERERID;
extern RwUInt8 _rwapcm16sm[0x14];          // RwaWaveDefFormat: PCM 16-bit mono (+4 bit depth)
extern RwUInt8 _rwavagm[0x14];             // RwaWaveDefFormat: VAG mono
extern RwaUniqueID D_004C1B00;             // the SPU2 wave definition's ID
extern RwUInt8 D_004E1C60[8];              // its two supported formats (pointers)
extern RwUInt8 _rendererSupportedFormats[8];
extern RwaWaveDef D_01E83230;              // the SPU2 wave definition (static struct)
extern "C" RwInt32 func_00114E60(void);    // DIntr
extern "C" RwInt32 EIntr(void);
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" void *func_00293430(RwUInt32 n, RwUInt32 size, RwUInt32 hint);   // RWA calloc
extern "C" RwInt32 RwaUUIDCompare(RwaUUID *a, RwaUUID *b);
extern "C" RwInt32 RwaWaveDefFormatWaveFormatCompare(void *defFormat, RwaWaveFormat *format);
extern "C" RwUInt32 _rwaWaveConvGetTargetSize(RwaWaveFormat *src, RwUInt32 data, RwaWaveFormat *dst);
extern "C" RwUInt32 _rwaAllocBitmapGetFree(UnkStruct_002B4760 *bm);
extern "C" void _rwaAllocBitmapFree(UnkStruct_002B4760 *bm, RwUInt32 *handle);
extern "C" RwBool _rwaMemoryBitmapAlloc(UnkStruct_002B4370 *mb, RwUInt32 size, RwUInt32 *handle);
extern "C" void _rwaMemoryBitmapReserve(UnkStruct_002B4370 *mb, RwUInt32 addr, RwUInt32 size, RwUInt32 used);
extern "C" void _rwaMemoryBitmapDestroy(UnkStruct_002B4370 *mb);
extern "C" UnkStruct_002B4370 *_rwaMemoryBitmapCreate(RwUInt32 base, RwUInt32 size, RwUInt32 unit,
                                                      UnkStruct_002B4370 *mb, RwUInt8 *bits);
extern "C" void _rwaMemoryBitmapHandleGetNewHandleFromOffset(UnkStruct_002B4370 *mb, RwUInt32 *h, RwUInt32 offset,
                                                              RwUInt32 size, RwUInt32 p4, RwUInt32 *out);
extern "C" RwUInt32 _rwaMemoryBitmapHandleGetOffset(UnkStruct_002B4370 *mb, RwUInt32 h1, RwUInt32 h2);
extern "C" RwUInt32 RwaRPCDmaBusy(void);
extern "C" void RwaRPCTransfer(RwUInt32 iopAddr, void *ee, RwUInt32 size, RwUInt32 op, RwUInt32 unused,
                               RwUInt32 p5, RwUInt32 p6, RwUInt32 p7);
extern "C" RwBool RwaSPU2RemoteIOPFopsPreviousOperationComplete(RwUInt32 file, RwBool wait);
extern "C" void RwaSPU2RemoteIOPFopsCreateWaveDataUploadBuffer(RwUInt32 size);
extern "C" void RwaSPU2RemoteIOPFopsRead(RwUInt32 file, RwUInt32 buffer, RwUInt32 offset, RwUInt32 size,
                                            RwUInt32 *done, RwUInt32 p5, RwUInt32 p6);
extern "C" RwaObjDef *_rwaObjDefFindByUUID(RwaObjDef *start, RwaUUID *id, RwaObjDef **p2);
extern "C" RwaWaveDef *RwaWaveDefCreate(RwaObjDef *parent, RwaWaveDef *def);
extern "C" RwaWaveDef *RwaWaveDefAssignID(RwaWaveDef *def, RwaUniqueID *id, const char *p2);
extern "C" void RwaWaveDefAssignSupportedFormats(RwaWaveDef *def, void *formats, RwUInt32 n);
extern "C" RwInt32 RwaWaveDefDestroy(RwaWaveDef *def);
extern "C" void RwaWaveDestroy(RwaWave *wave);
extern "C" RwaWave *RwaWaveCreate(RwaWaveDef *def, void *p1, void *p2, void *p3, RwUInt32 p4, void *p5);

// Frees input-area blocks (mask bits); returns the bits that were set.
// 0x002AFA20
extern "C" RwUInt32 _rwaSPU2InputAreaRAMFreeBlocks(RwUInt32 mask)
{
    func_00114E60();
    RwUInt32 old = _rwaG_rwaSPU2InputAreaMemoryBitmap;
    RwUInt32 keep = ~(mask & old);
    _rwaG_rwaSPU2InputAreaMemoryBitmap = (RwUInt8)(old & (keep & 0xFF));
    EIntr();
    return mask & keep;
}

// Reserves input-area blocks: all of mask, or none (0) when one is taken.
// 0x002AFA70
extern "C" RwUInt32 _rwaSPU2InputAreaRAMReserveBlocks(RwUInt32 mask)
{
    func_00114E60();
    if (mask & _rwaG_rwaSPU2InputAreaMemoryBitmap)
        mask = 0;
    _rwaG_rwaSPU2InputAreaMemoryBitmap |= (RwUInt8)mask;
    EIntr();
    return mask;
}

// Samples in a format's data: VAG (ADPCM) 28 per 16-byte block (mono; stereo half), 16-bit PCM bytes / 2 (mono) or
// / 4 (stereo); else 0.
// 0x002AFAC0
extern "C" RwUInt32 _rwaSPU2WaveGetSamples(RwaWaveFormat *fmt)
{
    RwUInt32 len = fmt->length;
    RwUInt32 n = 0;
    if (!RwaUUIDCompare(fmt->dataType, &rwaSPU2WAVEID)) {
        if (fmt->noChannels == 2)
            return ((len >> 4) * 7 * 4) >> 1;
        if (fmt->noChannels == 1)
            return (len >> 4) * 7 * 4;
        return n;
    }
    if (!RwaUUIDCompare(fmt->dataType, &rwaWAVEFORMAT_PCM) && fmt->bitDepth == _rwapcm16sm[4]) {
        if (fmt->noChannels == 1)
            n = len >> 1;
        else if (fmt->noChannels == 2)
            n = len >> 2;
    }
    return n;
}

// The sound-RAM address of a handle (with an allocator hook, the handle is the address).
// 0x002AFBC0
extern "C" RwUInt32 _rwaSPU2WaveRAMGetAddress(RwUInt32 handle)
{
    if (D_01E82A00)
        return handle;
    RwUInt32 mask = (1u << D_01E83210.field_0.field_8) - 1;
    return D_01E83210.field_C + D_01E83210.field_14 * (handle & mask);
}

// Free sound RAM, bytes.
// 0x002AFC10
extern "C" RwUInt32 RwaSPU2WaveRAMGetFree(void)
{
    if (D_01E82A08)
        return D_01E82A08();
    RwUInt32 units = _rwaAllocBitmapGetFree(&D_01E83210.field_0);
    return D_01E83210.field_14 * units;
}

// 0x002AFC60
extern "C" void _rwaSPU2WaveRAMFree(RwUInt32 handle)
{
    func_00114E60();
    if (D_01E82A04) {
        D_01E82A04(handle);
        EIntr();
        return;
    }
    _rwaAllocBitmapFree(&D_01E83210.field_0, &handle);
    EIntr();
}

// 0x002AFCC0
extern "C" RwUInt32 _rwaSPU2WaveRAMAlloc(RwUInt32 size)
{
    RwUInt32 h;
    func_00114E60();
    if (D_01E82A00) {
        h = D_01E82A00(size);
        EIntr();
        return h;
    }
    _rwaMemoryBitmapAlloc(&D_01E83210, size, &h);
    EIntr();
    return h;
}

// 0x002AFD30
extern "C" void RwaSPU2HWResourceRAMUnlock(RwUInt32 addr, RwUInt32 size)
{
    _rwaMemoryBitmapReserve(&D_01E83210, addr, size, 0);
}

// 0x002AFD50
extern "C" RwBool RwaSPU2HWResourceRAMLock(RwUInt32 addr, RwUInt32 size)
{
    _rwaMemoryBitmapReserve(&D_01E83210, addr, size, 1);
    return 1;
}

// 0x002AFD90
extern "C" void _rwaSPU2WaveRAMClose(void)
{
    _rwaMemoryBitmapReserve(&D_01E83210, 0, 0x5400, 0);
    _rwaMemoryBitmapDestroy(&D_01E83210);
}

// 0x002AFDD0
extern "C" void _rwaSPU2WaveRAMOpen(void)
{
    _rwaMemoryBitmapCreate(0, 0x200000, 0x80, &D_01E83210, D_01E82A10);
    _rwaMemoryBitmapReserve(&D_01E83210, 0, 0x5400, 1);
}

// Streams wave data from an IOP file to sound RAM in two calls: the first starts a remote read of up to the upload
// buffer's size into the EE upload buffer (returns 0); once the read has delivered (_rwaSPU2DataInBuffer bytes), the
// next call sends them (rounded up to 0x40) to sound RAM at handle's address + offset (opcode 5) and returns that
// count. 0 while an RPC DMA is busy or the previous file operation is pending.
// 0x002AFE20
extern "C" RwUInt32 _RwaSPU2WaveReadToSRAM(RwUInt32 file, RwUInt32 handle, RwUInt32 offset, RwUInt32 size)
{
    if (RwaRPCDmaBusy())
        return 0;
    RwUInt32 got = _rwaSPU2DataInBuffer;
    if (got) {
        if (!D_01E82A00) {
            RwUInt32 mask = (1u << D_01E83210.field_0.field_8) - 1;
            handle = D_01E83210.field_C + D_01E83210.field_14 * (handle & mask);
        }
        RwaRPCTransfer(handle + offset, (void *)_rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBuffer, (got + 0x3F) & ~0x3Fu, 5,
                       0, 0, 0, 0);
        RwUInt32 r = _rwaSPU2DataInBuffer;
        _rwaSPU2DataInBuffer = 0;
        return r;
    }
    if (!_rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBuffer)
        RwaSPU2RemoteIOPFopsCreateWaveDataUploadBuffer(0x10000);
    if (!(size < _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBufferSize))
        size = _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBufferSize;
    if (!RwaSPU2RemoteIOPFopsPreviousOperationComplete(file, 0))
        return 0;
    _rwaSPU2DataInBuffer = 0;
    RwaSPU2RemoteIOPFopsRead(file, _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBuffer, offset, size, &_rwaSPU2DataInBuffer,
                             0, 0);
    return 0;
}

// A wave's memory back: EE memory for an EE-resident non-VAG wave (format.reserved bit 0 [G meaning]), else its
// sound RAM.
static void func_002AFF80_inl1(RwaWave *wave)
{
    if ((wave->format.reserved & 1) && RwaUUIDCompare(wave->format.dataType, &rwaSPU2WAVEID)) {
        func_00293440((void *)wave->data);
        return;
    }
    RwUInt32 h = wave->data;
    func_00114E60();
    if (D_01E82A04) {
        D_01E82A04(h);
        EIntr();
        return;
    }
    _rwaAllocBitmapFree(&D_01E83210.field_0, &h);
    EIntr();
}

// 0x002AFF80
extern "C" void _RwaSPU2WaveFreeSRAM(RwaWave *wave)
{
    func_002AFF80_inl1(wave);
}

// Moves the wave's handle by `delta` bytes (fix = 1) or turns it back into an offset (fix = 0) [L: wave dictionary
// relocation]; with an allocator hook the handle is an address and moves by plain addition.
// 0x002B0020
extern "C" void _RwaSPU2WaveFixupSRAM(RwaWave *wave, RwUInt32 delta, RwBool fix)
{
    RwBool bitmap = !D_01E82A00;
    if (fix) {
        if (bitmap) {
            RwUInt32 out;
            _rwaMemoryBitmapHandleGetNewHandleFromOffset(&D_01E83210, &delta, wave->data, wave->format.length, 0x10, &out);
            wave->data = out;
        } else {
            wave->data = wave->data + delta;
        }
    } else if (bitmap) {
        wave->data = _rwaMemoryBitmapHandleGetOffset(&D_01E83210, delta, wave->data);
    } else {
        wave->data = wave->data - delta;
    }
}

extern "C" RwaWave *_RwaSPU2WaveCreate(RwaWave *wave, RwaObj *obj);
extern "C" RwaWave *func_002B0140(RwaWave *wave);   // upload (not ported: never captured)

// 0x002B00E0
extern "C" RwUInt32 _RwaSPU2WaveAllocateSRAM(RwaWave *wave)
{
    if (_RwaSPU2WaveCreate(wave, wave->obj))
        return wave->data;
    return 0;
}

// Upload-busy callback: never.
// 0x002B0120
extern "C" RwBool func_002B0120(RwaWave *wave)
{
    return 0;
}

// Download-busy callback: an RPC DMA is in flight.
// 0x002B0130
extern "C" RwUInt32 func_002B0130(RwaWave *wave)
{
    return RwaRPCDmaBusy();
}

// 0x002B0370
extern "C" void _RwaSPU2WaveDestroy(RwaWave *wave)
{
    func_002AFF80_inl1(wave);
}

// Creation: only a VAG-mono target or an EE-resident wave. EE-resident non-VAG data gets EE memory (PCM: twice the
// length, state 4, uncompressed length * 8; else the length, state 1); otherwise sound RAM for the target length,
// state 1, and the uncompressed length of its 16-bit PCM conversion.
// 0x002B0410
extern "C" RwaWave *_RwaSPU2WaveCreate(RwaWave *wave, RwaObj *obj)
{
    RwaWaveFormat *target = &wave->targetFormat;
    RwaWaveFormat *fmt = &wave->format;
    if (!RwaWaveDefFormatWaveFormatCompare(_rwavagm, target) && !(fmt->reserved & 1))
        return 0;
    if (RwaUUIDCompare(fmt->dataType, &rwaSPU2WAVEID) && (fmt->reserved & 1)) {
        if (RwaUUIDCompare(fmt->dataType, &rwaWAVEFORMAT_PCM)) {
            wave->data = (unsigned int)func_00293430(fmt->length, 1, 0x30806);
            wave->state = (void *)1;
            wave->uncompressedLength = fmt->length;
        } else {
            wave->data = (unsigned int)func_00293430(fmt->length << 1, 1, 0x30806);
            wave->state = (void *)4;
            wave->uncompressedLength = fmt->length << 3;
        }
        return wave->data ? wave : 0;
    }
    RwUInt32 size = target->length;
    RwUInt32 h;
    func_00114E60();
    if (D_01E82A00) {
        h = D_01E82A00(size);
        EIntr();
    } else {
        _rwaMemoryBitmapAlloc(&D_01E83210, size, &h);
        EIntr();
    }
    wave->data = h;
    wave->state = (void *)1;
    RwaWaveFormat pcm;
    memcpy(&pcm, target, 0x1C);
    pcm.flags = 0;
    pcm.bitDepth = 0x10;
    pcm.dataType = &rwaWAVEFORMAT_PCM;
    wave->uncompressedLength = _rwaWaveConvGetTargetSize(target, wave->data, &pcm);
    return wave->data ? wave : 0;
}

// 0x002B05C0
extern "C" RwBool RwaSPU2WaveUnregister(void)
{
    return RwaWaveDefDestroy(&D_01E83230) != 0;
}

// 0x002B05F0
extern "C" RwaWaveDef *RwaSPU2WaveRegister(void)
{
    RwaWaveDef *def = RwaWaveDefCreate(_rwaObjDefFindByUUID(0, &rwaSPU2OUTPUTID, 0), &D_01E83230);
    if (!def)
        return 0;
    RwaWaveDefAssignID(def, &D_004C1B00, 0);
    def->field_C = _RwaSPU2WaveCreate;
    def->field_10 = _RwaSPU2WaveDestroy;
    def->field_14 = func_002B0140;
    def->field_1C = func_002B0130;
    def->field_18 = func_002B0120;
    def->field_20 = _RwaSPU2WaveAllocateSRAM;
    def->field_2C = _RwaSPU2WaveFixupSRAM;
    def->field_24 = _RwaSPU2WaveFreeSRAM;
    def->field_28 = _RwaSPU2WaveReadToSRAM;
    RwaWaveDefAssignSupportedFormats(def, D_004E1C60, 2);
    RwaObjDef *renderer = _rwaObjDefFindByUUID(0, &rwaWAVERENDERERID, 0);
    if (renderer) {
        RwUInt32 *misc = (RwUInt32 *)renderer->miscData;
        misc[1] = (unsigned int)_rendererSupportedFormats;
        misc[2] = 2;
    }
    return def;
}

// 0x002B0710
extern "C" void RwaSPU2WaveDestroy(RwaWave *wave)
{
    RwaWaveDestroy(wave);   // a tail jump; no caller reads v0 [C 0x2ABE04], so no result (RwaWaveDestroy has none)
}

// 0x002B0720
extern "C" RwaWave *RwaSPU2WaveCreate(RwUInt32 p0, RwUInt32 p1, RwUInt32 p2, RwUInt32 p3, RwUInt32 p4)
{
    return RwaWaveCreate(&D_01E83230, (void *)p0, (void *)p1, (void *)p2, p3, (void *)p4);
}
