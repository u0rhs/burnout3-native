// rwawavedef (librwacore.a, truncated in the E map) -- wave formats: frame sizes, samples per frame, sample counts,
// comparison, supported formats, lookup by UUID. Retail 0x29F410-0x2A05F0 [C E map]. Not ported yet: the format byte
// swap, RwaWaveFormatGetUUIDFromIndex, RwaWaveDefDestroy, the close module.
#include "rwa/rwacore.h"

extern RwaUUID rwaWAVEFORMAT_PCM;
extern RwaUUID rwaSPU2WAVEID;
extern RwaUUID rwaWAVEFORMAT_FLOAT;
extern RwaUUID rwaWAVEFORMAT_AC3;
extern RwaUUID rwaWAVEFORMAT_GCNADPCM;
extern RwaUUID rwaWAVEFORMAT_XADPCM;
extern RwaUUID rwaWAVEFORMAT_IMAADPCM;
extern RwaUUID rwaWAVEFORMAT_MP3;
extern RwaUUID rwaWAVEFORMAT_MP2;
extern RwaUUID rwaWAVEFORMAT_MPG;
extern float D_004E0744;   // 7.1111111 (64 / 9) [C retail .sdata]
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b);
extern "C" RwUInt32 RwaWaveFormatGetFrameSize(const RwaWaveFormat *format);

// divu as the EE does it when the divisor is 0: quotient 0xFFFFFFFF [L: the R5900 / PCSX2 rule; retail emits no
// divide-by-zero trap here]. C's / would be undefined.
static RwUInt32 func_0029F6D0_inl1(RwUInt32 a, RwUInt32 b)
{
    return b ? a / b : 0xFFFFFFFF;
}

// Samples in one coded frame, by format; ADPCM types (Xbox / IMA) of more than one channel from the mono figure
// divided by the frame-size ratio, as retail computes it. MPG gives 0 and an unknown type 0x180 [C 0x29F6A8 movz].
// 0x0029F520
extern "C" RwUInt32 RwaWaveFormatGetSamplesPerAudioFrame(const RwaWaveFormat *format)
{
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_PCM) == 0)
        return 1;
    if (RwaUUIDCompare(format->dataType, &rwaSPU2WAVEID) == 0)
        return 0x1C;
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_FLOAT) == 0)
        return 1;
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_AC3) == 0)
        return 0x600;
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_GCNADPCM) == 0)
        return 0xE;
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_XADPCM) == 0 ||
        RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_IMAADPCM) == 0) {
        if (format->noChannels == 1)
            return 0x40;
        RwaWaveFormat mono;
        memcpy(&mono, format, sizeof(RwaWaveFormat));
        mono.noChannels = 1;
        RwUInt32 ratio = func_0029F6D0_inl1(RwaWaveFormatGetFrameSize(&mono), RwaWaveFormatGetFrameSize(format));
        return func_0029F6D0_inl1(RwaWaveFormatGetSamplesPerAudioFrame(&mono), ratio);
    }
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_MP3) == 0)
        return 0x480;
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_MP2) == 0)
        return 0x480;
    return RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_MPG) == 0 ? 0 : 0x180;
}

// Samples per channel in `bytes` of data: frames * samples per frame, except stereo ADPCM (Xbox / IMA), which is
// (int)(7.1111 * bytes / frame size) in floats; divided by the channel count unless flag 4 (interleaved as one).
// 0x0029F6D0
extern "C" RwUInt32 RwaWaveFormatGetSamples(const RwaWaveFormat *format, RwUInt32 bytes)
{
    RwUInt32 spf = RwaWaveFormatGetSamplesPerAudioFrame(format);
    RwUInt32 frame = RwaWaveFormatGetFrameSize(format);
    RwUInt32 samples;
    if ((RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_XADPCM) == 0 ||
         RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_IMAADPCM) == 0) && format->noChannels == 2)
        samples = (RwInt32)(D_004E0744 * ((float)bytes / (float)frame));
    else
        samples = spf * func_0029F6D0_inl1(bytes, frame);
    if (format->flags & 4)
        return samples;
    return func_0029F6D0_inl1(samples, format->noChannels);
}

// Bytes per coded frame, by format: PCM bits / 8, SPU2 ADPCM 16, float 4, GameCube ADPCM 8, Xbox / IMA ADPCM 36 mono
// and 4 otherwise [C 0x2A02D4], MP3 144 * bitrate / rate (128000 and 48000 when not given), anything else 0.
// 0x002A0220
extern "C" RwUInt32 RwaWaveFormatGetFrameSize(const RwaWaveFormat *format)
{
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_PCM) == 0)
        return format->bitDepth >> 3;
    if (RwaUUIDCompare(format->dataType, &rwaSPU2WAVEID) == 0)
        return 0x10;
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_FLOAT) == 0)
        return 4;
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_GCNADPCM) == 0)
        return 8;
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_XADPCM) == 0 ||
        RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_IMAADPCM) == 0)
        return format->noChannels == 1 ? 0x24 : 4;
    if (RwaUUIDCompare(format->dataType, &rwaWAVEFORMAT_MP3) != 0)
        return 0;
    RwUInt32 bitrate = format->miscData ? *(RwUInt32 *)format->miscData : 0x1F400;
    RwUInt32 rate = format->sampleRate ? format->sampleRate : 0xBB80;
    return func_0029F6D0_inl1(bitrate * 144, rate);
}

// 1 when the formats are equal (type, rate, bit depth, channels, flags other than 2), else 0.
// 0x002A0380
extern "C" RwBool RwaWaveFormatCompare(const RwaWaveFormat *a, const RwaWaveFormat *b)
{
    if (RwaUUIDCompare(a->dataType, b->dataType) != 0)
        return 0;
    if (a->sampleRate != b->sampleRate)
        return 0;
    if (a->bitDepth != b->bitDepth || a->noChannels != b->noChannels)
        return 0;
    return (a->flags & ~2) == (b->flags & ~2) ? 1 : 0;
}

typedef RwBool (*UnkFn_00296140)(RwaObjDef *parent, RwaObjDef *def, void *arg);   // as rwaobjdef.cpp
extern "C" RwaObjDef *_rwaObjDefEnum(RwaObjDef *p0, RwaObjDef **p1, UnkFn_00296140 cb, void *arg, RwUInt32 p4);

// Whether a wave type format accepts a wave format: channels, bit depth, rate within [min, max], type, and flag 1.
static RwBool func_0029F840_inl1(const RwaWaveDefFormat *f, const RwaWaveFormat *fmt)
{
    return fmt->noChannels == f->noChannels && fmt->bitDepth == f->bitDepth && fmt->sampleRate >= f->minSampleRate &&
           fmt->sampleRate <= f->maxSampleRate && RwaUUIDCompare(fmt->dataType, f->dataType) == 0 &&
           (fmt->flags & 1) == (f->flags & 1);
}

// 1 when the type accepts the format (or accepts any: no format list), else 0.
// 0x0029F840
extern "C" RwBool _rwaWaveDefIsTargetFormatSupported(RwaWaveDef *def, const RwaWaveFormat *fmt)
{
    RwUInt32 n = def->field_34;
    if (n == 0)
        return 1;
    for (RwUInt32 i = 0; i < n; i++)
        if (func_0029F840_inl1(def->field_38[i], fmt))
            return 1;
    return 0;
}

// The serialised size of a format: 0x1C, + 16 for the type UUID, + the extra data.
// 0x0029FCA0
extern "C" RwUInt32 _rwaWaveFormatGetSize(const RwaWaveFormat *fmt)
{
    RwUInt32 size = 0x1C;
    if (fmt->dataType)
        size += 0x10;
    if (fmt->miscData)
        size += fmt->miscDataSize;
    return size;
}

// 0x0029FCD0
extern "C" void RwaWaveDefAssignSupportedFormats(RwaWaveDef *def, RwaWaveDefFormat **formats, RwUInt32 n)
{
    if (def->field_58 & 1) {
        if (def->field_38)
            func_00293440(def->field_38);
        def->field_58 &= ~1u;
    }
    def->field_38 = formats;
    def->field_34 = n;
    def->field_3C = 0;
}

// 0x0029FD50
extern "C" RwBool RwaWaveDefFormatWaveFormatCompare(const RwaWaveDefFormat *f, const RwaWaveFormat *fmt)
{
    return func_0029F840_inl1(f, fmt);
}

// The wave format a type format describes (rate, length and extra data are left as they are unless `clear`).
// 0x0029FE10
extern "C" void _rwaWaveDefFormatCopyToWaveFormat(const RwaWaveDefFormat *f, RwaWaveFormat *out, RwBool clear)
{
    if (clear) {
        out->sampleRate = 0;
        out->dataType = 0;
        out->length = 0;
        out->bitDepth = 0;
        out->noChannels = 0;
        out->flags = 0;
        out->reserved = 0;
        out->miscData = 0;
        out->miscDataSize = 0;
    }
    out->bitDepth = f->bitDepth;
    out->dataType = f->dataType;
    out->noChannels = f->noChannels;
    out->flags = 0;
    if (f->flags & 1)
        out->flags |= 1;
    if (f->flags & 4)
        out->flags |= 4;
}

// An _rwaObjDefEnum callback: the wave type with search[0]'s UUID among objDef's, stored in search[1] (1: found).
// 0x0029FEB0
extern "C" RwBool _RwaWaveDefFindByUUIDCallback(void *p0, RwaObjDef *objDef, RwUInt32 *search)
{
    for (RwLLLink *l = objDef->waveDefList.link.next; l != &objDef->waveDefList.link; l = l->next) {
        RwaWaveDef *wd = (RwaWaveDef *)((char *)l - 0x50);
        if (RwaUUIDCompare(*(RwaUUID **)wd, (RwaUUID *)search[0]) == 0) {
            search[1] = (unsigned int)wd;
            return 1;
        }
    }
    return 0;
}

// 0x0029FF30
extern "C" RwaWaveDef *RwaWaveDefFindByUUID(const RwaUUID *uuid)
{
    RwUInt32 search[2];   // {uuid, found}: guest words
    search[0] = (unsigned int)uuid;
    search[1] = 0;
    _rwaObjDefEnum(0, 0, (UnkFn_00296140)_RwaWaveDefFindByUUIDCallback, search, 1);
    return (RwaWaveDef *)search[1];
}

extern RwaUUID *_rwaGwaveFormats[9];

// 0x0029F820
extern "C" RwaUUID *RwaWaveFormatGetUUIDFromIndex(RwUInt32 index)
{
    return _rwaGwaveFormats[index];
}

extern RwaObjDef *_rwaGnullObjDef;
extern RwaFreeList D_01E7D4A0;     // the wave definitions' free list (static struct)
extern RwUInt32 D_004E2B90;        // _moduleOpen of rwawavedef [L name: config/symbol_addrs_b3names.txt lists it as a
                                   // conflict at this address]
extern "C" RwaUniqueID *_rwaUniqueIDInitialize(RwaUniqueID *id);
extern "C" void *_rwaUniqueIDAssignUUID(void *id, const RwaUUID *uuid);
extern "C" void *_rwaUniqueIDAssignName(void *id, const RwChar *name);
extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint);
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);

// A wave definition in `def` (the caller's: flags 0) or from the free list (flags 2), every callback and count
// cleared (+0x28 and +0x2C are left as they were [C: no stores]), linked first on `objDef`'s waveDefList (null: the
// null definition). The format-array free under flag 1 cannot trigger, since both paths set the flags first; kept.
// 0x002A0050
extern "C" RwaWaveDef *RwaWaveDefCreate(RwaObjDef *objDef, RwaWaveDef *def)
{
    if (def) {
        def->field_58 = 0;
    } else {
        def = (RwaWaveDef *)RwaFreeListAlloc(&D_01E7D4A0, 0x30805);
        if (!def)
            return 0;
        def->field_58 = 2;
    }
    _rwaUniqueIDInitialize((RwaUniqueID *)def);
    def->field_C = 0;
    def->field_10 = 0;
    def->field_14 = 0;
    def->field_1C = 0;
    def->field_20 = 0;
    def->field_24 = 0;
    def->field_18 = 0;
    def->field_30 = 0;
    def->field_44 = 0;
    def->field_48 = 0x40;
    def->field_4C = 0;
    if (def->field_58 & 1) {
        if (def->field_38)
            func_00293440(def->field_38);
        def->field_58 &= ~1u;
    }
    def->field_38 = 0;
    def->field_34 = 0;
    def->field_3C = 0;
    def->field_50.prev = 0;
    def->field_50.next = 0;
    if (!objDef)
        objDef = _rwaGnullObjDef;
    def->field_40 = objDef;
    def->field_50.next = objDef->waveDefList.link.next;
    def->field_50.prev = &objDef->waveDefList.link;
    objDef->waveDefList.link.next->prev = &def->field_50;
    objDef->waveDefList.link.next = &def->field_50;
    return def;
}

// 0x002A0170
extern "C" RwaWaveDef *RwaWaveDefAssignID(RwaWaveDef *def, const RwaUUID *uuid, const RwChar *name)
{
    if (!_rwaUniqueIDAssignUUID(def, uuid))
        return 0;
    if (!_rwaUniqueIDAssignName(def, name))
        return 0;
    return def;
}

// Frees a format's misc data unless it is the caller's (flag 2), then clears the pointer and size.
// 0x002A01D0
extern "C" void _rwaWaveFormatFreeMiscData(RwaWaveFormat *fmt)
{
    if (!(fmt->flags & 2) && fmt->miscData)
        func_00293440(fmt->miscData);
    fmt->miscData = 0;
    fmt->miscDataSize = 0;
}

typedef RwBool (*UnkFn_002A0490)(void);

// Calls each registration function in turn; 0 at the first that fails.
// 0x002A0490
extern "C" RwBool _rwaWaveDefRegister(UnkFn_002A0490 *funcs, RwUInt32 n)
{
    for (RwUInt32 i = 0; i < n; i++)
        if (!funcs[i]())
            return 0;
    return 1;
}

// 0x002A0630
extern "C" RwBool _rwaWaveDefOpenModule(void)
{
    D_004E2B90 = 1;
    if (RwaFreeListCreate(0x6C, 8, 0x40, 0, &D_01E7D4A0, 0x40805))
        return 1;
    D_004E2B90 = 0;
    return 0;
}

extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" void RwaEndianCopy(RwUInt32 *cursor, const void *src, RwUInt32 size);
extern "C" RwaUUID *_rwaUUIDSerialize(RwaUUID *uuid, RwaUUID *out, RwBool swap);

// A wave format to (`write`) or from a buffer: the 0x1C-byte struct (field by field, byte-reversed and with zeroed
// padding, when `swap`; in the buffer the type pointer is a 0/1 flag), then the type's UUID when it has one, then the
// misc data when it has some. Writing marks the format's misc data as not its own (flag 2). Reading points the misc
// data into the buffer and resolves the UUID to one of the 9 known formats (0 returned when it is none of them).
// 0x0029F970
extern "C" RwaWaveFormat *_rwaWaveFormatSerialize(RwaWaveFormat *fmt, void *buf, RwBool write, RwBool swap)
{
    if (write) {
        RwUInt32 cur = (unsigned int)buf;
        if (swap) {
            RwUInt32 hasType = fmt->dataType != 0;
            RwaEndianCopy(&cur, &fmt->sampleRate, 4);
            RwaEndianCopy(&cur, &hasType, 4);
            RwaEndianCopy(&cur, &fmt->length, 4);
            RwaEndianCopy(&cur, &fmt->bitDepth, 1);
            RwaEndianCopy(&cur, &fmt->noChannels, 1);
            memset((void *)cur, 0, 2);
            cur += 2;
            RwaEndianCopy(&cur, &fmt->miscData, 4);
            RwaEndianCopy(&cur, &fmt->miscDataSize, 4);
            RwaEndianCopy(&cur, &fmt->flags, 1);
            RwaEndianCopy(&cur, &fmt->reserved, 1);
            memset((void *)cur, 0, 2);
            cur += 2;
        } else {
            memcpy(buf, fmt, 0x1C);
            cur += 0x1C;
        }
        if (fmt->dataType) {
            _rwaUUIDSerialize(fmt->dataType, (RwaUUID *)cur, swap);
            cur += 0x10;
        }
        if (fmt->miscData) {
            memcpy((void *)cur, fmt->miscData, fmt->miscDataSize);
            fmt->flags |= 2;
        }
        return fmt;
    }
    RwUInt8 *p = (RwUInt8 *)buf;
    if (swap) {
        RwUInt32 cur = (unsigned int)fmt;
        RwaEndianCopy(&cur, p + 0x0, 4);
        RwaEndianCopy(&cur, p + 0x4, 4);
        RwaEndianCopy(&cur, p + 0x8, 4);
        RwaEndianCopy(&cur, p + 0xC, 1);
        RwaEndianCopy(&cur, p + 0xD, 1);
        memset((void *)cur, 0, 2);
        cur += 2;
        RwaEndianCopy(&cur, p + 0x10, 4);
        RwaEndianCopy(&cur, p + 0x14, 4);
        RwaEndianCopy(&cur, p + 0x18, 1);
        RwaEndianCopy(&cur, p + 0x19, 1);
        memset((void *)cur, 0, 2);
    } else {
        memcpy(fmt, buf, 0x1C);
    }
    p += 0x1C;
    if (fmt->dataType) {
        _rwaUUIDSerialize((RwaUUID *)p, (RwaUUID *)p, swap);
        RwaUUID *type = 0;
        for (RwUInt32 i = 0; i < 9; i++) {
            if (RwaUUIDCompare(_rwaGwaveFormats[i], (RwaUUID *)p) == 0) {
                type = _rwaGwaveFormats[i];
                break;
            }
        }
        fmt->dataType = type;
        if (!fmt->dataType)
            return 0;
        p += 0x10;
    }
    if (fmt->miscData)
        fmt->miscData = p;
    return fmt;
}

extern "C" void *RwaWaveDictUsingAllWaveDef(RwaWaveDef *waveDef);   // [C] reads a0 only (retail also sets a1 = 1)
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);
extern "C" RwaUniqueID *_rwaUniqueIDFreeData(RwaUniqueID *id);

// Calls each unregistration function in turn; 0 at the first that fails.
// 0x002A0420
extern "C" RwBool _rwaWaveDefUnregister(UnkFn_002A0490 *funcs, RwUInt32 n)
{
    for (RwUInt32 i = 0; i < n; i++)
        if (!funcs[i]())
            return 0;
    return 1;
}

// An _rwaObjDefEnum callback (second argument: the object definition): destroys every wave definition on its
// waveDefList that no wave dictionary uses (RwaWaveDictUsingAllWaveDef(def, 1) == 0): its destroy callback (+0x30), its
// own free list (+0x4C), the format array when flag 1, the unlink from the list, the unique ID, and the entry itself when
// flag 2. A definition still in use is skipped. Always 0 (continue). The first argument is not used [C].
// 0x002A0500
extern "C" RwBool _RwaWaveDefDestroyCallback(void *unused, RwaObjDef *objDef)
{
    RwLLLink *head = &objDef->waveDefList.link;
    RwLLLink *l = head->next;
    while (l != head) {
        RwLLLink *next = l->next;
        RwaWaveDef *def = (RwaWaveDef *)((RwUInt8 *)l - 0x50);
        if (RwaWaveDictUsingAllWaveDef(def)) {   // [C] retail also passes a1 = 1; the callee ignores it
            l = next;
            continue;
        }
        if (def->field_30)
            def->field_30(def);
        if (def->field_4C)
            RwaFreeListDestroy(def->field_4C);
        if (def->field_58 & 1)
            func_00293440(def->field_38);
        def->field_50.prev->next = def->field_50.next;
        def->field_50.next->prev = def->field_50.prev;
        _rwaUniqueIDFreeData((RwaUniqueID *)def);
        if (def->field_58 & 2) {
            RwaFreeListFree(&D_01E7D4A0, def);
        }
        l = next;
    }
    return 0;
}

// Destroys every unused wave definition of every object definition, then the definitions' free list.
// 0x002A05F0
extern "C" void _rwaWaveDefCloseModule(void)
{
    _rwaObjDefEnum(0, 0, (UnkFn_00296140)_RwaWaveDefDestroyCallback, 0, 1);
    RwaFreeListDestroy(&D_01E7D4A0);
    D_004E2B90 = 0;
}
