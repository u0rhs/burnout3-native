// rwawavedict.o (librwacore.a) -- loading wave dictionaries (.AWD banks) asynchronously through the IOP's file
// operations, and freeing their sound RAM. Retail 0x29E730-0x29F410 [C E map].
// Not ported yet (not called in races14): RwaWaveDictFixup 0x29E740, _RwaWaveDictStreamHeaderByteSwap 0x29EA50.
#include "rwa/rwacore.h"

// RwaAsyncLoadStatus [C enum values: Revenge stabs]
enum UnkEnum_0029F1E0 {
    UnkEnum_0029F1E0_IDLE = 1,                    // rwaASYNCLOADSTATUS_IDLE
    UnkEnum_0029F1E0_OPENING = 2,                 // rwaASYNCLOADSTATUS_OPENING
    UnkEnum_0029F1E0_READINGHEADER = 0x30,        // rwaASYNCLOADSTATUS_READINGHEADER
    UnkEnum_0029F1E0_DUMPINGWAVES = 0x50,         // rwaASYNCLOADSTATUS_DUMPINGWAVES
    UnkEnum_0029F1E0_READINGWAVEDATA = 0x70,      // rwaASYNCLOADSTATUS_READINGWAVEDATA
    UnkEnum_0029F1E0_COMPLETE = 0x100,            // rwaASYNCLOADSTATUS_COMPLETE
    UnkEnum_0029F1E0_OBJECTSCOMPLETE = 0x300,     // rwaASYNCLOADSTATUS_OBJECTSCOMPLETE
    UnkEnum_0029F1E0_WAVEDATACOMPLETE = 0x500,    // rwaASYNCLOADSTATUS_WAVEDATACOMPLETE
    UnkEnum_0029F1E0_INSUFFICIENTWAVEMEMORY = 0x2000
};

// RwaWaveDictLoader [L], 0x48 bytes [C RwaWaveDictLoaderCreate 0x29F3E8; every field below is used at the cited
// addresses in this file].
struct RwaWaveDictLoader {
    RwaWaveDict *waveDict;      // 0x00 (inside the header buffer)
    RwUInt32 filePointer;       // 0x04 the IOP file (a handle from RwaSPU2RemoteIOPFopsRegisterForOpen, not a RAM address)
    RwUInt32 waveSize;          // 0x08
    RwUInt32 dataSize;          // 0x0C wave data bytes
    char *addr;                 // 0x10 the header buffer
    RwUInt32 size;              // 0x14 its size
    RwUInt32 dictFlag;          // 0x18 0x10 = RWA allocated the buffer, 0x20 = the caller's
    RwUInt32 waveRAMHandle;     // 0x1C where the wave data goes: a sound-RAM handle or an address (memorySpace)
    RwUInt32 waveRAMSize;       // 0x20
    RwUInt32 dataRead;          // 0x24
    RwUInt32 dataRemaining;     // 0x28
    RwInt32 loadStatus;         // 0x2C UnkEnum_0029F1E0
    RwUInt32 flags;             // 0x30 2 = RWA allocated the loader (but creation clears it again [C 0x29F3D8]), 4 = objects only
    RwUInt32 maximumReadSize;   // 0x34
    RwInt32 waveIndex;          // 0x38
    RwInt32 waveDataRead;       // 0x3C
    RwUInt32 offset;            // 0x40
    RwInt32 memorySpace;        // 0x44 RwaMemorySpace: 1 main, 2 IOP, 3 sound RAM
};

// The file header at the start of an .AWD, as the loader reads it (not in the stabs).
struct UnkStruct_0029EF00 {
    unsigned char pad_0[0x4];
    RwUInt8 field_4;            // 0x04 nonzero: the other byte order (swapped)
    unsigned char pad_5[0x3];
    RwUInt32 field_8;           // 0x08 header size
    RwUInt32 field_C;           // 0x0C offset of the dictionary in the header
    unsigned char pad_10[0x4];
    RwUInt32 field_14;          // 0x14 wave data size
    RwaUUID field_18;           // 0x18 the platform UUID it was built for
};

extern RwUInt32 _rwaGwaveDictLoadBufSize;
extern RwaUUID *rwaTHIS_HOST;
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" void RwaSPU2RemoteIOPFopsSetWaveDataUploadBuffer(RwUInt32 buffer, RwUInt32 size);
extern "C" RwUInt32 RwaSPU2RemoteIOPFopsRegisterForOpen(const RwChar *name);
extern "C" RwBool RwaSPU2RemoteIOPFopsPreviousOperationComplete(RwUInt32 file, RwUInt32 p1);
extern "C" void RwaSPU2RemoteIOPFopsReadToEE(RwUInt32 file, RwUInt32 dst, RwUInt32 size, RwUInt32 *p3, RwUInt32 p4, RwUInt32 p5);
extern "C" void RwaSPU2RemoteIOPFopsRead(RwUInt32 file, RwUInt32 dst, RwUInt32 p2, RwUInt32 p3, RwUInt32 *p4, RwUInt32 p5,
                                         RwUInt32 p6);
extern "C" void RwaSPU2RemoteIOPFopsClose(RwUInt32 file);
extern "C" void _RwaWaveSetFixedUp(RwaWave *wave, RwBool fixed);
extern "C" RwaWave *RwaWaveCreateLoadedWave(RwaWave *p0, void *p1, RwUInt32 p2, RwUInt32 p3, RwUInt32 flag, RwaWave *wave);
extern "C" RwaWave *RwaWaveDictFindWaveByIndex(RwaWaveDict *dict, RwUInt32 index);
extern "C" RwaWaveDict *_rwaWaveDictCreate(RwaWaveDict *dict);
extern "C" RwaUUID *RwaWaveDictFixup(RwaWaveDict *dict, void *base, RwBool swap);
extern "C" void _RwaWaveDictStreamHeaderByteSwap(UnkStruct_0029EF00 *header);
extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b);
typedef RwaWave *(*UnkFn_002ADF30)(RwaWave *wave, RwUInt32 arg);
extern "C" void RwaWaveDictForAllWaves(RwaWaveDict *dict, UnkFn_002ADF30 cb, RwUInt32 arg);
extern "C" void *func_00293470(RwUInt32 size, RwUInt32 hint);
extern "C" void func_00293450(void *p);

static RwaWaveDef *func_0029E900_inl1(RwaWave *wave)
{
    return wave->waveDef;
}

// A tail jump [C].
// 0x0029E730
extern "C" void RwaWaveDictSetWaveDataUploadBuffer(void *buffer, RwUInt32 size)
{
    RwaSPU2RemoteIOPFopsSetWaveDataUploadBuffer((RwUInt32)buffer, size);
}

// RwaWaveDictForAllWaves callbacks: undo / apply a wave's fixup, then mark it.
// 0x0029E900
extern "C" RwaWave *_rwaWaveDictUnfixWaveCallback(RwaWave *wave, RwUInt32 arg)
{
    func_0029E900_inl1(wave)->field_2C(wave, arg, 0);
    _RwaWaveSetFixedUp(wave, 0);
    return wave;
}

// 0x0029E940
extern "C" RwaWave *_rwaWaveDictFixupWaveCallback(RwaWave *wave, RwUInt32 arg)
{
    func_0029E900_inl1(wave)->field_2C(wave, arg, 1);
    _RwaWaveSetFixedUp(wave, 1);
    return wave;
}

// 0x0029EA20
extern "C" RwaWave *_rwaWaveDictFreeSRAMCallback(RwaWave *wave)
{
    func_0029E900_inl1(wave)->field_24(wave);
    return wave;
}

// Frees a dictionary's sound RAM: per wave (flagsAux 1), or the dictionary's one block through a stand-in wave whose
// data and target length are the block's (its other fields: retail's stack contents, ours zero [L: the free callback
// reads data and length only]).
// 0x0029E980
extern "C" void RwaWaveDictFreeSRAM(RwaWaveDict *dict)
{
    // boot2: `block`'s address goes to a guest function (field_24): a guest-stack local at retail's offset [C 0x29E980
    // addiu sp, -0x90; 0x29EA00 addiu a0, sp, 0x30] (natively a host local's address is no guest address)
    B3_RETAIL_FRAME(0x90);
    if (dict->flagsAux & 1) {
        RwaWaveDictForAllWaves(dict, (UnkFn_002ADF30)_rwaWaveDictFreeSRAMCallback, 0);
        return;
    }
    RwaWave *first = RwaWaveDictFindWaveByIndex(dict, 0);
    RwaWave &block = *B3_FRAME_LOCAL(RwaWave, 0x30);
    memset(&block, 0, sizeof(block));
    block.data = dict->waveRAMHandle;
    block.targetFormat.length = dict->waveRAMSize;
    block.format.reserved = 0;
    block.state = 0;
    RwaWaveDictForAllWaves(dict, _rwaWaveDictUnfixWaveCallback, dict->waveRAMHandle);
    dict->waveRAMHandle = 0;
    func_0029E900_inl1(first)->field_24(&block);
    dict->waveRAMHandle = 0;
    dict->waveRAMSize = 0;
}

// Starts loading the dictionary file `name`: opens it on the IOP, then reads its first 0x800 bytes (the header) into
// `buffer` (null: an RWA-allocated 0x800-byte buffer; a caller's must hold 0x800). The loader (null: allocated) is
// returned in its new state (OPENING while the IOP is busy, INSUFFICIENTWAVEMEMORY, or READINGHEADER).
// 0x0029EB70
extern "C" RwaWaveDictLoader *RwaWaveDictReadAsync(const RwChar *name, char *buffer, RwUInt32 bufferSize,
                                                   RwUInt32 waveRAMHandle, RwUInt32 waveRAMSize, RwUInt32 p5,
                                                   RwUInt32 maximumReadSize, RwaWaveDictLoader *loader)
{
    if (loader) {
        loader->flags = 1;
    } else {
        loader = (RwaWaveDictLoader *)func_002934C0(sizeof(RwaWaveDictLoader), 0x30808);
        loader->flags = 2;
    }
    loader->loadStatus = UnkEnum_0029F1E0_IDLE;
    loader->dataSize = 0;
    loader->waveSize = 0;
    loader->waveDict = 0;
    loader->filePointer = 0;
    loader->offset = 0;
    loader->dataRead = 0;
    loader->addr = 0;
    loader->waveRAMHandle = 0;
    loader->size = 0;
    loader->waveRAMSize = 0;
    loader->flags = 0;
    loader->maximumReadSize = 0;
    if (loader->loadStatus != UnkEnum_0029F1E0_OPENING)
        loader->filePointer = RwaSPU2RemoteIOPFopsRegisterForOpen(name);
    RwUInt32 file = loader->filePointer;
    if (!file)
        return loader;
    if (!RwaSPU2RemoteIOPFopsPreviousOperationComplete(file, 0)) {
        loader->loadStatus = UnkEnum_0029F1E0_OPENING;
        return loader;
    }
    if (!buffer) {
        buffer = (char *)func_00293470(0x800, 0x30808);
        bufferSize = 0x800;
        loader->dictFlag = 0x10;
    } else {
        if (bufferSize < 0x800) {
            loader->loadStatus = UnkEnum_0029F1E0_INSUFFICIENTWAVEMEMORY;
            return loader;
        }
        loader->dictFlag = 0x20;
    }
    RwaSPU2RemoteIOPFopsReadToEE(file, (unsigned int)buffer, 0x800, 0, 0, 0);
    loader->loadStatus = UnkEnum_0029F1E0_READINGHEADER;
    loader->dataRead = 0x800;
    loader->addr = buffer;
    loader->waveRAMHandle = waveRAMHandle;
    loader->size = bufferSize;
    loader->waveRAMSize = waveRAMSize;
    loader->maximumReadSize = maximumReadSize;
    loader->memorySpace = 3;
    return loader;
}

// A loaded dictionary's wave, as the RwaWaveDictForAllWaves callback of _rwaWaveDictLoaderLoadWaves creates it:
// info[0] 0 drops the wave's state; flag 2 from the loader's flags (info[1] & 1) or else from the wave's own flags.
// 0x0029ED40
extern "C" RwaWave *func_0029ED40(RwaWave *wave, RwUInt32 *info)
{
    if (info[0] == 0)
        wave->state = 0;
    RwUInt32 flag;
    if (info[1] & 1)
        flag = info[1] & 2;
    else
        flag = wave->flags & 2;
    return RwaWaveCreateLoadedWave(0, 0, 0, 0, flag, wave);
}

static RwUInt32 func_0029ED90_inl1(RwUInt32 a, RwUInt32 b)
{
    return a < b ? a : b;
}

// One read of wave data (at most maximumReadSize; into sound RAM also at most _rwaGwaveDictLoadBufSize), once the
// IOP's previous operation is done; the counters advance by what was read.
// 0x0029ED90
extern "C" RwaWaveDictLoader *_rwaWaveDictLoaderLoadData(RwaWaveDictLoader *loader)
{
    RwUInt32 size = loader->dataRemaining;
    RwUInt32 got = 0;
    if (!RwaSPU2RemoteIOPFopsPreviousOperationComplete(loader->filePointer, 0))
        return loader;
    if (loader->memorySpace == 3) {
        RwaWave *w = RwaWaveDictFindWaveByIndex(loader->waveDict, 0);
        RwUInt32 limit = func_0029ED90_inl1(_rwaGwaveDictLoadBufSize, loader->maximumReadSize);
        size = func_0029ED90_inl1(size, limit);
        got = func_0029E900_inl1(w)->field_28((void *)loader->filePointer, loader->waveRAMHandle, loader->offset, size);
    } else if (loader->memorySpace == 1) {
        size = func_0029ED90_inl1(size, loader->maximumReadSize);
        RwaSPU2RemoteIOPFopsReadToEE(loader->filePointer, loader->waveRAMHandle, size, 0, 0, 0);
        got = size;
    } else if (loader->memorySpace == 2) {
        size = func_0029ED90_inl1(size, loader->maximumReadSize);
        if (RwaSPU2RemoteIOPFopsPreviousOperationComplete(loader->filePointer, 0)) {
            RwaSPU2RemoteIOPFopsRead(loader->filePointer, loader->waveRAMHandle, loader->dataRead, loader->dataSize, 0,
                                     0, 0);
            got = size;
        }
    }
    loader->dataRead += got;
    loader->offset += got;
    loader->dataRemaining -= got;
    return loader;
}

// The header steps: READINGHEADER (byte-swap if needed; when the header is longer than what was read, grow an own
// buffer or fail on a caller's, read the rest: DUMPINGWAVES), then DUMPINGWAVES (the dictionary inside the header is
// set up, fixed up, its waves created; then OBJECTSCOMPLETE when only objects are wanted, else READINGWAVEDATA with
// sound RAM from the caller or allocated for the whole block, and the first data read).
// 0x0029EF00
extern "C" RwaWaveDictLoader *_rwaWaveDictLoaderLoadWaves(RwaWaveDictLoader *loader)
{
    B3_RETAIL_FRAME(0xF0);   // boot2: `info` / `block` below are guest-stack locals (they reach guest functions)
    RwBool swap = 0;
    RwUInt32 file = loader->filePointer;
    if (!RwaSPU2RemoteIOPFopsPreviousOperationComplete(file, 0))
        return loader;
    RwInt32 status = loader->loadStatus;
    UnkStruct_0029EF00 *h = (UnkStruct_0029EF00 *)loader->addr;
    if (status != UnkEnum_0029F1E0_DUMPINGWAVES) {
        if (status != UnkEnum_0029F1E0_READINGHEADER)
            return loader;
        if (h->field_4) {
            _RwaWaveDictStreamHeaderByteSwap(h);
            swap = 1;
        }
        RwUInt32 need = h->field_8;
        if (loader->dataRead < need) {
            if (!(loader->dictFlag & 0x20)) {
                UnkStruct_0029EF00 *grown = (UnkStruct_0029EF00 *)func_00293470(need, 0x30808);
                memcpy(grown, loader->addr, loader->size);
                func_00293450(loader->addr);
                loader->addr = (char *)grown;
                h = grown;
                loader->size = grown->field_8;
            } else if (loader->size < need) {
                loader->loadStatus = UnkEnum_0029F1E0_INSUFFICIENTWAVEMEMORY;
                return loader;
            }
            RwaSPU2RemoteIOPFopsReadToEE(file, (unsigned int)(loader->addr + loader->dataRead), h->field_8 - loader->dataRead, 0, 0, 0);
            loader->loadStatus = UnkEnum_0029F1E0_DUMPINGWAVES;
            loader->dataRead = loader->size;
            return loader;
        }
    }
    loader->waveDict = (RwaWaveDict *)(loader->addr + h->field_C);
    loader->waveSize = loader->dataRead;
    loader->dataSize = h->field_14;
    loader->dataRemaining = h->field_14;
    loader->waveIndex = 0;
    loader->waveDataRead = 0;
    RwaWaveDict *dict = loader->waveDict;
    RwaLLNode keep = dict->waveListHead;
    _rwaWaveDictCreate(dict);
    dict->flags = (RwUInt8)loader->dictFlag;
    dict->waveListHead = keep;
    dict->dumpAddr = (void *)loader->addr;
    RwaWaveDictFixup(dict, loader->addr, swap);
    // boot2: `info` and `block` reach guest functions by address: guest-stack locals at retail's offsets [C 0x29EF00
    // addiu sp, -0xF0; 0x29F0A4 addiu a2, sp, 0xE8; 0x29F160 / 0x29F180 addiu a0, sp, 0x80]
    RwUInt32 *info = B3_FRAME_LOCAL(RwUInt32, 0xE8);
    info[0] = RwaUUIDCompare(rwaTHIS_HOST, &h->field_18) == 0 ? 1 : 0;
    info[0] = 1;   // retail overwrites the comparison's result [C 0x29F08C]
    info[1] = loader->flags;
    RwaWaveDictForAllWaves(dict, (UnkFn_002ADF30)func_0029ED40, (unsigned int)info);
    if (loader->flags & 4) {
        loader->loadStatus = UnkEnum_0029F1E0_OBJECTSCOMPLETE;
        return loader;
    }
    loader->loadStatus = UnkEnum_0029F1E0_READINGWAVEDATA;
    if (loader->waveRAMHandle) {
        dict->waveRAMHandle = loader->waveRAMHandle;
        dict->waveRAMSize = loader->waveRAMSize;
    } else {
        RwUInt32 bytes = loader->dataSize;
        RwaWave &block = *B3_FRAME_LOCAL(RwaWave, 0x80);
        memcpy(&block, RwaWaveDictFindWaveByIndex(dict, 0), sizeof(RwaWave));
        block.targetFormat.length = bytes;
        RwUInt32 ram = func_0029E900_inl1(&block)->field_20(&block);
        if (ram) {
            dict->waveRAMHandle = ram;
            dict->waveRAMSize = block.targetFormat.length;
            loader->waveRAMHandle = dict->waveRAMHandle;
        } else {
            loader->waveRAMHandle = 0;
        }
        dict->flags |= 0x40;
    }
    _rwaWaveDictLoaderLoadData(loader);
    return loader;
}

// One step of the load; the status after it. Final and error states, and IDLE, do nothing. READINGWAVEDATA reads
// while data remains; when it is all read (and the IOP is done): WAVEDATACOMPLETE for objects-only loaders, else the
// waves are fixed up against the sound RAM, the file is closed, COMPLETE.
// 0x0029F1E0
extern "C" RwInt32 RwaWaveDictLoaderUpdate(RwaWaveDictLoader *loader)
{
    switch (loader->loadStatus) {
    case 0:
    case UnkEnum_0029F1E0_IDLE:
    case 0x4000:
    case UnkEnum_0029F1E0_INSUFFICIENTWAVEMEMORY:
    case 0x1000:
    case UnkEnum_0029F1E0_WAVEDATACOMPLETE:
    case UnkEnum_0029F1E0_OBJECTSCOMPLETE:
    case UnkEnum_0029F1E0_COMPLETE:
        return loader->loadStatus;
    case UnkEnum_0029F1E0_READINGWAVEDATA:
        if (loader->dataRemaining) {
            _rwaWaveDictLoaderLoadData(loader);
            return loader->loadStatus;
        }
        if (!RwaSPU2RemoteIOPFopsPreviousOperationComplete(loader->filePointer, 0))
            return loader->loadStatus;
        if (loader->flags & 4) {
            loader->loadStatus = UnkEnum_0029F1E0_WAVEDATACOMPLETE;
            return loader->loadStatus;
        }
        RwaWaveDictForAllWaves(loader->waveDict, _rwaWaveDictFixupWaveCallback, loader->waveDict->waveRAMHandle);
        RwaSPU2RemoteIOPFopsClose(loader->filePointer);
        loader->loadStatus = UnkEnum_0029F1E0_COMPLETE;
        return loader->loadStatus;
    case UnkEnum_0029F1E0_DUMPINGWAVES:
    case UnkEnum_0029F1E0_READINGHEADER:
        _rwaWaveDictLoaderLoadWaves(loader);
        return loader->loadStatus;
    default:
        return loader->loadStatus;
    }
}

// Frees the loader if RWA allocated it (flag 2: never set after creation, see RwaWaveDictLoader::flags).
// 0x0029F310
extern "C" void RwaWaveDictLoaderDestroy(RwaWaveDictLoader *loader)
{
    if (loader->flags & 2)
        func_00293440(loader);
}

static void func_0029F350_inl1(RwaWaveDictLoader *loader)
{
    loader->loadStatus = UnkEnum_0029F1E0_IDLE;
    loader->dataSize = 0;
    loader->waveSize = 0;
    loader->waveDict = 0;
    loader->filePointer = 0;
    loader->offset = 0;
    loader->dataRead = 0;
    loader->addr = 0;
    loader->waveRAMHandle = 0;
    loader->size = 0;
    loader->waveRAMSize = 0;
    loader->flags = 0;
    loader->maximumReadSize = 0;
}

// 0x0029F350
extern "C" void RwaWaveDictLoaderInitialise(RwaWaveDictLoader *loader)
{
    func_0029F350_inl1(loader);
}

// 0x0029F390
extern "C" RwaWaveDictLoader *RwaWaveDictLoaderCreate(RwaWaveDictLoader *loader)
{
    if (loader) {
        loader->flags = 1;
    } else {
        loader = (RwaWaveDictLoader *)func_002934C0(sizeof(RwaWaveDictLoader), 0x30808);
        loader->flags = 2;
    }
    func_0029F350_inl1(loader);
    return loader;
}

extern "C" void RwaUniqueIDByteSwap(void *id);
extern "C" void RwaLLNodeByteSwap(void *node);
extern "C" void RwaWaveByteSwap(RwaWave *wave);
extern "C" RwaUUID *RwaWaveFormatGetUUIDFromIndex(RwUInt32 index);

// A nonzero word gets base added (offsets in a loaded dictionary become addresses).
static void func_0029E740_inl1(RwUInt32 *w, RwUInt32 base)
{
    if (*w)
        *w = *w + base;
}

// Relocates a dictionary loaded at `base` (swap: byte-swap each piece first): the dictionary's name and list words,
// then every list node from the first to the last (prev, next, data) and its wave (UUID and name pointers, state,
// both formats' misc data), the wave's two data-type indices becoming UUID pointers. Returns the last UUID looked up
// [C 0x29E888: v0 of the final call].
// 0x0029E740
extern "C" RwaUUID *RwaWaveDictFixup(RwaWaveDict *dict, void *base, RwBool swap)
{
    RwUInt32 b = (unsigned int)base;
    RwUInt32 *d = (RwUInt32 *)dict;
    if (swap) {
        RwaUniqueIDByteSwap(dict);
        RwaLLNodeByteSwap(d + 3);
    }
    func_0029E740_inl1(d + 1, b);
    func_0029E740_inl1(d + 3, b);
    func_0029E740_inl1(d + 4, b);
    func_0029E740_inl1(d + 5, b);
    RwUInt32 *node = (RwUInt32 *)d[4];
    RwUInt32 *last = (RwUInt32 *)d[3];
    RwaUUID *r;
    RwUInt32 *cur;
    do {
        cur = node;
        if (swap)
            RwaLLNodeByteSwap(node);
        func_0029E740_inl1(node + 2, b);
        func_0029E740_inl1(node + 1, b);
        func_0029E740_inl1(node + 0, b);
        RwUInt32 *w = (RwUInt32 *)cur[2];
        node = (RwUInt32 *)node[1];
        if (swap)
            RwaWaveByteSwap((RwaWave *)w);
        func_0029E740_inl1(w + 0, b);
        func_0029E740_inl1(w + 1, b);
        func_0029E740_inl1(w + 0x14, b);
        func_0029E740_inl1(w + 8, b);
        func_0029E740_inl1(w + 0xF, b);
        w[5] = (unsigned int)RwaWaveFormatGetUUIDFromIndex(w[5]);
        r = RwaWaveFormatGetUUIDFromIndex(w[0xC]);
        w[0xC] = (unsigned int)r;
    } while (cur != last);
    return r;
}
