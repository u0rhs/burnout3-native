// rwastreamcreate.o (librwacore.a; truncated in the E map) -- stream descriptions from their packed (file) form.
// Retail 0x2AD8E0-0x2ADDCC [C E map]. C port written from retail's code (audio1); proven by the checker on live
// captures.
#include "rwa/rwacore.h"

typedef RwUInt32 (*UnkFn_002AD940)(const RwChar *s);
extern RwUInt32 _rwaGblockAlign;
extern RwUInt8 ourGlobals[0x200];             // RenderWare's globals: +0x120 the string length [L]
extern RwaUUID rwaWAVEFORMAT_XADPCM;
extern RwaUUID rwaWAVEFORMAT_IMAADPCM;
extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b);
extern "C" RwaUniqueID *_rwaUniqueIDAssignUUID(RwaUniqueID *id, RwaUUID *uuid);
extern "C" RwaUniqueID *_rwaUniqueIDAssignName(RwaUniqueID *id, const RwChar *name);
extern "C" void *_rwaWaveFormatSerialize(void *dst, void *src, RwUInt32 p2, RwUInt32 p3);
extern "C" RwUInt32 _rwaWaveFormatGetSize(void *format);

// The word at p + off.
static RwUInt32 &func_002AD8E0_inl1(void *p, RwUInt32 off)
{
    return *(RwUInt32 *)((RwUInt8 *)p + off);
}

// A packed UUID at *cur into the unique ID at id (when it has one).
static void func_002AD8E0_inl2(RwUInt8 *id, RwUInt8 *&cur)
{
    if (func_002AD8E0_inl1(id, 0)) {
        _rwaUniqueIDAssignUUID((RwaUniqueID *)id, (RwaUUID *)cur);
        cur += 0x10;
    }
}

// A packed name at *cur (its length + 1, rounded up to _rwaGblockAlign) into the unique ID at id (when it has one).
static void func_002AD8E0_inl3(RwUInt8 *id, RwUInt8 *&cur)
{
    if (func_002AD8E0_inl1(id, 4)) {
        _rwaUniqueIDAssignName((RwaUniqueID *)id, (const RwChar *)cur);
        RwUInt32 a = _rwaGblockAlign - 1;
        RwUInt32 n = (*(UnkFn_002AD940 *)(ourGlobals + 0x120))((const RwChar *)cur);
        cur += ~a & (a + (n + 1));
    }
}

// Fixes up a packed stream description in place: its id, then the segments (+0x10, +0xC of 0x20 bytes: per-format
// sizes, ids, sub-entry tables of 0x14 bytes and their ids), then the formats (+0x18, +0x14 of 0x28 bytes: each wave
// format deserialised in place, then their ids). A stereo XADPCM / IMA ADPCM first format gets 0x40-sample frames of
// 0x24 bytes (+0xC / +0x1A, +0x18 = 4). 0 when a wave format does not deserialise.
// 0x002AD8E0
extern "C" RwUInt8 *_rwaStreamFormatCreateFromPack(RwUInt8 *d)
{
    RwUInt8 *cur = d + 0x2C;
    func_002AD8E0_inl2(d, cur);
    func_002AD8E0_inl3(d, cur);
    func_002AD8E0_inl1(d, 0x10) = (unsigned int)cur;
    cur += func_002AD8E0_inl1(d, 0xC) * 32;
    RwUInt32 sizes = func_002AD8E0_inl1(d, 0x14) * 4;
    for (RwUInt32 i = 0; i < func_002AD8E0_inl1(d, 0xC); i++) {
        RwUInt8 *seg = (RwUInt8 *)(func_002AD8E0_inl1(d, 0x10) + i * 32);
        if (func_002AD8E0_inl1(seg, 0xC)) {
            func_002AD8E0_inl1(seg, 0xC) = (unsigned int)cur;
            cur += sizes;
        }
    }
    for (RwUInt32 i = 0; i < func_002AD8E0_inl1(d, 0xC); i++)
        func_002AD8E0_inl2((RwUInt8 *)(func_002AD8E0_inl1(d, 0x10) + i * 32), cur);
    for (RwUInt32 i = 0; i < func_002AD8E0_inl1(d, 0xC); i++)
        func_002AD8E0_inl3((RwUInt8 *)(func_002AD8E0_inl1(d, 0x10) + i * 32), cur);
    for (RwUInt32 i = 0; i < func_002AD8E0_inl1(d, 0xC); i++) {
        RwUInt8 *seg = (RwUInt8 *)(func_002AD8E0_inl1(d, 0x10) + i * 32);
        if (func_002AD8E0_inl1(seg, 0x14)) {
            func_002AD8E0_inl1(seg, 0x14) = (unsigned int)cur;
            cur += func_002AD8E0_inl1(seg, 0x10) * 20;
        }
    }
    for (RwUInt32 i = 0; i < func_002AD8E0_inl1(d, 0xC); i++) {
        RwUInt8 *seg = (RwUInt8 *)(func_002AD8E0_inl1(d, 0x10) + i * 32);
        for (RwUInt32 j = 0; j < func_002AD8E0_inl1(seg, 0x10); j++)
            func_002AD8E0_inl2((RwUInt8 *)(func_002AD8E0_inl1(seg, 0x14) + j * 20), cur);
    }
    for (RwUInt32 i = 0; i < func_002AD8E0_inl1(d, 0xC); i++) {
        RwUInt8 *seg = (RwUInt8 *)(func_002AD8E0_inl1(d, 0x10) + i * 32);
        for (RwUInt32 j = 0; j < func_002AD8E0_inl1(seg, 0x10); j++)
            func_002AD8E0_inl3((RwUInt8 *)(func_002AD8E0_inl1(seg, 0x14) + j * 20), cur);
    }
    func_002AD8E0_inl1(d, 0x18) = (unsigned int)cur;
    cur += func_002AD8E0_inl1(d, 0x14) * 40;
    for (RwUInt32 i = 0; i < func_002AD8E0_inl1(d, 0x14); i++) {
        RwUInt8 *f = (RwUInt8 *)(func_002AD8E0_inl1(d, 0x18) + i * 40);
        if (func_002AD8E0_inl1(f, 0x14)) {
            func_002AD8E0_inl1(f, 0x14) = (unsigned int)cur;
            if (!_rwaWaveFormatSerialize(cur, cur, 0, 0))
                return 0;
            RwUInt32 a = _rwaGblockAlign - 1;
            cur += ~a & (a + _rwaWaveFormatGetSize(cur));
        }
    }
    RwUInt8 *f0 = (RwUInt8 *)func_002AD8E0_inl1(d, 0x18);
    RwaUUID *type = (RwaUUID *)func_002AD8E0_inl1((void *)func_002AD8E0_inl1(f0, 0x14), 4);
    if (!RwaUUIDCompare(type, &rwaWAVEFORMAT_XADPCM) ||
        !RwaUUIDCompare((RwaUUID *)func_002AD8E0_inl1((void *)func_002AD8E0_inl1(f0, 0x14), 4), &rwaWAVEFORMAT_IMAADPCM)) {
        f0 = (RwUInt8 *)func_002AD8E0_inl1(d, 0x18);
        if (((RwUInt8 *)func_002AD8E0_inl1(f0, 0x14))[0xD] == 2) {
            func_002AD8E0_inl1(f0, 0xC) = 0x40;
            *(RwUInt16 *)((RwUInt8 *)func_002AD8E0_inl1(d, 0x18) + 0x1A) = 0x24;
            *(RwUInt16 *)((RwUInt8 *)func_002AD8E0_inl1(d, 0x18) + 0x18) = 4;
        }
    }
    for (RwUInt32 i = 0; i < func_002AD8E0_inl1(d, 0x14); i++)
        func_002AD8E0_inl2((RwUInt8 *)(func_002AD8E0_inl1(d, 0x18) + i * 40), cur);
    for (RwUInt32 i = 0; i < func_002AD8E0_inl1(d, 0x14); i++)
        func_002AD8E0_inl3((RwUInt8 *)(func_002AD8E0_inl1(d, 0x18) + i * 40), cur);
    return d;
}
