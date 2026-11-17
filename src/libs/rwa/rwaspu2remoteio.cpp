// rwaspu2remoteio.o (librwaspu2.a) -- remote file operations on IOP files (three handles, 1-3): each operation is a
// 0x70-byte request sent to the IOP's request slot (opcode 0x32); the IOP answers by calling
// _rwaSPU2RemoteIOPFopsEECallback (opcode 0x33 / 0x47: RwaSPU2OutputCmdHandler calls the packet's function).
// Retail 0x2B4A00-0x2B5150 [C E map]. C port written from retail's code (audio1); proven by the checker on live
// captures where they exist.
#include "rwa/rwacore.h"

typedef void (*UnkFn_002B4FC0)(RwUInt32 result, RwUInt32 index);

// The request (RwaSPU2RemoteIOPFopsOperationData[3]), 0x70 bytes [C RwaSPU2RemoteIOPFopsSeek 0x2B4B70-0x2B4BD4,
// ReadToEE 0x2B4C70-0x2B4CEC, Read 0x2B4D78-0x2B4E18, Close 0x2B4EC0, RegisterForOpen 0x2B4F90].
struct UnkStruct_0049FE60 {
    RwUInt32 field_0;             // 0x00 the file's index (handle - 1)
    RwUInt32 field_4;             // 0x04 buffer: EE address (read to EE) or EE upload buffer (read for sound RAM)
    RwUInt32 field_8;             // 0x08 bytes
    RwUInt32 field_C;             // 0x0C file position: seek sets it, a read adds its offset first
    RwUInt32 field_10;            // 0x10 operation: 1 close, 2 read to EE, 3 read (sound-RAM upload), 4 seek
    UnkFn_002B4FC0 field_14;      // 0x14 the EE function the IOP calls back (_rwaSPU2RemoteIOPFopsEECallback)
    RwUInt32 field_18;            // 0x18 EE address of the result word (bytes done), or 0
    RwUInt32 field_1C;            // 0x1C flags: 2 open, 4 handle in use, 8 cleared by reads, 0x10 operation pending
    unsigned char pad_20[0x4];
    char field_24[0x4C];          // 0x24 the file name (RegisterForOpen)
};
// Per handle: the caller's completion callback (RwaSPU2RemoteIOPFopsUserCallbacks[3]), 0x14 bytes.
typedef void (*UnkFn_0049FFB0)(RwUInt32 handle, RwUInt32 result, RwUInt32 p2, RwUInt32 kind, RwUInt32 arg);
struct UnkStruct_0049FFB0 {
    UnkFn_0049FFB0 field_0;       // 0x00 callback, called once
    RwUInt32 field_4;             // 0x04 handle
    RwUInt32 field_8;             // 0x08 kind: 3 close, 4 seek, 5 read
    RwUInt32 field_C;             // 0x0C the caller's argument
    RwUInt32 *field_10;           // 0x10 result word, or null
};
// Per handle: a close callback {handle, function, data}, 0xC bytes [C RwaSPU2RemoteIOPFopsSetCloseCallback].
typedef void (*UnkFn_01E83620)(RwUInt32 handle, RwUInt32 p1, RwUInt32 p2, RwUInt32 p3, RwUInt32 data);
struct UnkStruct_01E83620 {
    RwUInt32 field_0;
    UnkFn_01E83620 field_4;
    RwUInt32 field_8;
};

extern UnkStruct_0049FE60 RwaSPU2RemoteIOPFopsOperationData[3];
extern UnkStruct_0049FFB0 RwaSPU2RemoteIOPFopsUserCallbacks[3];
extern UnkStruct_01E83620 D_01E83620[3];
extern RwUInt32 _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBuffer;
extern RwUInt32 _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBufferSize;
extern RwUInt32 _rwaGRwaSPU2RemoteIOPFopsDataIOPBuffer;   // the IOP's three request slots (0x70 each)
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" char *strcpy(char *dst, const char *src);
extern "C" void SyncDCache(void *start, void *end);
extern "C" void FlushCache(RwInt32 mode);
extern "C" RwUInt32 sceSifAllocSysMemory(RwInt32 mode, RwInt32 size, RwUInt32 addr);
extern "C" void RwaRPCTransfer(RwUInt32 iopAddr, void *ee, RwUInt32 size, RwUInt32 op, RwUInt32 unused,
                               RwUInt32 p5, RwUInt32 p6, RwUInt32 p7);
extern "C" void _rwaSPU2RemoteIOPFopsEECallback(RwUInt32 result, RwUInt32 index);

// 0x002B4A00
extern "C" RwUInt32 _rwaSPU2RemoteIOPFopsHasIOPFileBeenOpened(RwUInt32 handle)
{
    return RwaSPU2RemoteIOPFopsOperationData[handle - 1].field_1C & 2;
}

// 0x002B4A30
extern "C" void RwaSPU2RemoteIOPFopsSetWaveDataUploadBuffer(RwUInt32 buffer, RwUInt32 size)
{
    _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBuffer = buffer;
    _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBufferSize = size;
}

// 0x002B4A40
extern "C" void RwaSPU2RemoteIOPFopsCreateWaveDataUploadBuffer(RwUInt32 size)
{
    RwUInt32 b = sceSifAllocSysMemory(0, size, 0);
    _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBufferSize = size;
    _rwaGRwaSPU2RemoteIOPFopsWaveDataUploadBuffer = b;
}

// 1 when the handle has no operation pending. With `wait` retail spins on the flag it read once [C 0x2B4AA8: the
// loop does not reload it], so a pending operation hangs the caller.
// 0x002B4A80
extern "C" RwBool RwaSPU2RemoteIOPFopsPreviousOperationComplete(RwUInt32 handle, RwBool wait)
{
    UnkStruct_0049FE60 *op = &RwaSPU2RemoteIOPFopsOperationData[handle - 1];
    if (wait) {
        volatile RwUInt32 pending = op->field_1C & 0x10;
        while (pending) {
        }
    }
    return (op->field_1C & 0x10) ? 0 : 1;
}

// The request to the IOP's slot for this index (opcode 0x32, arg0 = index).
static void func_002B4B00_inl1(UnkStruct_0049FE60 *op, RwUInt32 i)
{
    SyncDCache(op, (char *)op + 0x6F);
    RwaRPCTransfer(_rwaGRwaSPU2RemoteIOPFopsDataIOPBuffer + i * 0x70, op, 0x70, 0x32, 0, 0, i, 0);
}

// 0x002B4B00
extern "C" void RwaSPU2RemoteIOPFopsSeek(RwUInt32 handle, RwUInt32 pos, UnkFn_0049FFB0 cb, RwUInt32 arg)
{
    RwUInt32 i = handle - 1;
    UnkStruct_0049FE60 *op = &RwaSPU2RemoteIOPFopsOperationData[i];
    UnkStruct_0049FFB0 *uc = &RwaSPU2RemoteIOPFopsUserCallbacks[i];
    op->field_1C |= 0x10;
    if (!(op->field_1C & 2))
        op->field_1C |= 2;
    uc->field_0 = cb;
    uc->field_4 = handle;
    op->field_0 = i;
    op->field_C = pos;
    uc->field_8 = 4;
    uc->field_C = arg;
    op->field_10 = 4;
    op->field_14 = _rwaSPU2RemoteIOPFopsEECallback;
    uc->field_10 = 0;
    op->field_18 = 0;
    func_002B4B00_inl1(op, i);
}

// 0x002B4C00
extern "C" void RwaSPU2RemoteIOPFopsReadToEE(RwUInt32 handle, RwUInt32 buffer, RwUInt32 size, RwUInt32 *done,
                                             UnkFn_0049FFB0 cb, RwUInt32 arg)
{
    RwUInt32 i = handle - 1;
    UnkStruct_0049FE60 *op = &RwaSPU2RemoteIOPFopsOperationData[i];
    UnkStruct_0049FFB0 *uc = &RwaSPU2RemoteIOPFopsUserCallbacks[i];
    op->field_1C |= 0x10;
    if (!(op->field_1C & 2))
        op->field_1C |= 2;
    op->field_0 = i;
    uc->field_0 = cb;
    op->field_4 = buffer;
    op->field_8 = size;
    op->field_10 = 2;
    op->field_14 = _rwaSPU2RemoteIOPFopsEECallback;
    op->field_18 = (unsigned int)done;
    uc->field_4 = handle;
    uc->field_8 = 5;
    uc->field_C = arg;
    uc->field_10 = done;
    op->field_1C &= ~8u;
    func_002B4B00_inl1(op, i);
}

// 0x002B4D20
extern "C" void RwaSPU2RemoteIOPFopsRead(RwUInt32 handle, RwUInt32 buffer, RwUInt32 offset, RwUInt32 size,
                                         RwUInt32 *done, UnkFn_0049FFB0 cb, RwUInt32 arg)
{
    RwUInt32 i = handle - 1;
    UnkStruct_0049FE60 *op = &RwaSPU2RemoteIOPFopsOperationData[i];
    UnkStruct_0049FFB0 *uc = &RwaSPU2RemoteIOPFopsUserCallbacks[i];
    if (!(op->field_1C & 2))
        op->field_1C |= 2;
    uc->field_0 = cb;
    uc->field_4 = handle;
    uc->field_8 = 5;
    uc->field_C = arg;
    uc->field_10 = done;
    op->field_1C |= 0x10;
    op->field_0 = i;
    op->field_C = op->field_C + offset;
    op->field_4 = buffer;
    op->field_8 = size;
    op->field_10 = 3;
    op->field_14 = _rwaSPU2RemoteIOPFopsEECallback;
    op->field_18 = (unsigned int)done;
    op->field_1C &= ~8u;
    func_002B4B00_inl1(op, i);
}

// 0x002B4E40
extern "C" void RwaSPU2RemoteIOPFopsSetCloseCallback(RwUInt32 handle, UnkFn_01E83620 fn, RwUInt32 data)
{
    UnkStruct_01E83620 *c = &D_01E83620[handle - 1];
    c->field_0 = handle;
    c->field_4 = fn;
    c->field_8 = data;
}

// 0x002B4E70
extern "C" void RwaSPU2RemoteIOPFopsClose(RwUInt32 handle)
{
    RwUInt32 i = handle - 1;
    UnkStruct_0049FE60 *op = &RwaSPU2RemoteIOPFopsOperationData[i];
    UnkStruct_0049FFB0 *uc = &RwaSPU2RemoteIOPFopsUserCallbacks[i];
    uc->field_4 = handle;
    op->field_0 = i;
    uc->field_8 = 3;
    op->field_10 = 1;
    func_002B4B00_inl1(op, i);
}

// A handle for a file name: the first free of three (in use = flag 4). With none free retail returns 0 and copies the
// name to entry -1, i.e. 0x4C bytes before the table [C 0x2B4F70-0x2B4F94].
// 0x002B4F20
extern "C" RwUInt32 RwaSPU2RemoteIOPFopsRegisterForOpen(const char *name)
{
    RwUInt32 h = 0;
    for (RwUInt32 i = 0; i < 3; i++) {
        if (!(RwaSPU2RemoteIOPFopsOperationData[i].field_1C & 4)) {
            RwaSPU2RemoteIOPFopsOperationData[i].field_1C |= 4;
            h = i + 1;
            break;
        }
    }
    strcpy((char *)RwaSPU2RemoteIOPFopsOperationData + (RwInt32)(h - 1) * 0x70 + 0x24, name);
    return h;
}

// The IOP's completion of request `index` (result = bytes done or status): pending cleared, the result stored, a close
// frees the handle (and runs its close callback once), a read to EE flushes the cache, then the caller's callback runs
// once with (handle, result, 2, kind, arg).
// 0x002B4FC0
extern "C" void _rwaSPU2RemoteIOPFopsEECallback(RwUInt32 result, RwUInt32 index)
{
    UnkStruct_0049FE60 *op = &RwaSPU2RemoteIOPFopsOperationData[index];
    UnkStruct_0049FFB0 *uc = &RwaSPU2RemoteIOPFopsUserCallbacks[index];
    UnkStruct_01E83620 *cc = &D_01E83620[index];
    op->field_1C &= ~0x10u;
    if (uc->field_10)
        *uc->field_10 = result;
    if (op->field_10 == 1) {
        RwUInt32 h = uc->field_4;
        UnkStruct_0049FE60 *f = &RwaSPU2RemoteIOPFopsOperationData[h - 1];
        f->field_1C &= ~4u;
        f->field_1C &= ~2u;
        if (cc->field_0 == h) {
            if (cc->field_4)
                cc->field_4(cc->field_0, 0, 1, 3, cc->field_8);
            cc->field_0 = 0;
            cc->field_4 = 0;
            cc->field_8 = 0;
        }
    }
    if (op->field_10 == 2)
        FlushCache(0);
    UnkFn_0049FFB0 fn = uc->field_0;
    if (!fn)
        return;
    RwUInt32 copy[3];
    memcpy(copy, &uc->field_4, 0xC);
    uc->field_0 = 0;
    fn(copy[0], result, 2, copy[1], copy[2]);
}
