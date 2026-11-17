// rwawave.o (librwacore.a) -- waves: creating one for loaded data, its target (played) format, destruction.
// Retail 0x291060-0x291A50 [C E map]. Not ported yet (not called in races14): RwaWaveByteSwap 0x291060,
// RwaWaveUploadBusy 0x291180, RwaWaveUpload 0x2911C0.
#include "rwa/rwacore.h"

extern RwaFreeList D_01E7CB30;     // waves (static struct)
extern RwUInt32 D_004E2A8C;        // _moduleOpen of rwawave.o [L name, research/retail_data_names.csv]
extern RwaUUID D_004C1B00;         // the default wave definition's UUID [C RwaWaveCreateLoadedWave 0x291510]
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" void _rwaWaveFormatFreeMiscData(RwaWaveFormat *format);
extern "C" void _rwaWaveDictRemoveAllWave(RwaWave *wave);
extern "C" void *_rwaUniqueIDFreeData(void *id);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);
extern "C" RwaWaveDef *RwaWaveDefFindByUUID(const RwaUUID *uuid);
extern "C" RwBool _rwaWaveDefIsTargetFormatSupported(RwaWaveDef *def, RwaWaveFormat *format);
extern "C" void _rwaWaveDefFormatCopyToWaveFormat(const RwaWaveDefFormat *f, RwaWaveFormat *out, RwBool clear);
extern "C" RwInt32 RwaWaveFormatCompare(const RwaWaveFormat *a, const RwaWaveFormat *b);
extern "C" RwUInt32 _rwaWaveConvGetTargetSize(const RwaWaveFormat *src, RwUInt32 p1, const RwaWaveFormat *dst);
typedef RwBool (*UnkFn_00292980)(RwaObj *obj, void *arg);
extern "C" void RwaObjEnum(UnkFn_00292980 cb, void *arg);
extern "C" RwBool func_002929F0(RwaObj *obj, RwaObj **search);

// Destroys a wave: its type's destroy callback, both formats' extra data, its dictionary entries, its ID, its state
// (flag 8), then the struct: from a free list (flag 1: the type's or the shared one), the caller's (flag 4), or the
// allocator's.
// 0x00291380
extern "C" void RwaWaveDestroy(RwaWave *wave)
{
    if (wave->waveDef->field_10)
        wave->waveDef->field_10(wave);
    _rwaWaveFormatFreeMiscData(&wave->format);
    _rwaWaveFormatFreeMiscData(&wave->targetFormat);
    _rwaWaveDictRemoveAllWave(wave);
    _rwaUniqueIDFreeData(wave);
    if (wave->flags & 8)
        func_00293440(wave->state);
    RwUInt32 flags = wave->flags;
    if (flags & 1) {
        if (wave->waveDef->field_4C)
            RwaFreeListFree(wave->waveDef->field_4C, wave);
        else
            RwaFreeListFree(&D_01E7CB30, wave);
    } else if (!(flags & 4)) {
        func_00293440(wave);
    }
}

// Sets the format a wave is played in: `target` if the type accepts it (when it checks), else the type's default
// format with the source's rate and extra data, else the source format itself; extra data is copied from the source
// when RwaWaveFormatCompare calls the two equal (it returns 1 for equal); the length is the converted size; the wave keeps its own copy of the extra data.
// With no target the default format is built in a stack local whose padding bytes (+0xE, +0x1A) are never written but
// are copied into the wave: retail's are stale stack bytes, ours are not [C 0x2916A4, 0x291628]. Nothing reads them
// (RwaWaveFormatCompare 0x2A0380 compares the fields).
// 0x00291580
extern "C" void _rwaWaveSetTargetFormat(RwaWave *wave, RwaWaveFormat *src, RwaWaveFormat *target)
{
    RwaWaveDef *def = wave->waveDef;
    RwaWaveFormat local;
    if (target && target->dataType && def->field_34) {
        if (!_rwaWaveDefIsTargetFormatSupported(def, target))
            return;
    } else {
        RwaWaveDefFormat *dflt = def->field_38 ? def->field_38[def->field_3C] : 0;
        if (dflt) {
            _rwaWaveDefFormatCopyToWaveFormat(dflt, &local, 1);
            local.sampleRate = src->sampleRate;
            local.miscData = src->miscData;
            local.miscDataSize = src->miscDataSize;
            target = &local;
        } else if (!target) {
            target = src;
        }
    }
    if (RwaWaveFormatCompare(src, target) != 0) {
        target->miscData = src->miscData;
        target->miscDataSize = src->miscDataSize;
    }
    target->length = _rwaWaveConvGetTargetSize(src, 0, target);
    wave->targetFormat.sampleRate = 0;
    wave->targetFormat.dataType = 0;
    wave->targetFormat.length = 0;
    wave->targetFormat.bitDepth = 0;
    wave->targetFormat.noChannels = 0;
    wave->targetFormat.flags = 0;
    wave->targetFormat.reserved = 0;
    wave->targetFormat.miscData = 0;
    wave->targetFormat.miscDataSize = 0;
    if (!target)
        return;
    memcpy(&wave->targetFormat, target, sizeof(RwaWaveFormat));
    if (!target->miscData || !target->miscDataSize)
        return;
    wave->targetFormat.miscData = func_002934C0(target->miscDataSize, 0x30806);
    if (!wave->targetFormat.miscData)
        return;
    memcpy(wave->targetFormat.miscData, target->miscData, target->miscDataSize);
}

// Completes a wave whose data came with a dictionary: its type (null: the default type), its object (null: the first
// object of the type's object definition), state memory, a playable target format, and flag 2 (positional) for mono
// waves when `positional` is set.
// 0x00291460
extern "C" RwaWave *RwaWaveCreateLoadedWave(RwaWaveDef *def, RwaObj *obj, RwUInt32 p2, RwUInt32 p3, RwBool positional,
                                            RwaWave *wave)
{
    if (!def)
        def = RwaWaveDefFindByUUID(&D_004C1B00);
    if (!obj) {
        RwUInt32 search[2];   // {found object, object definition}: guest words, as func_002929F0 reads them
        search[0] = 0;
        search[1] = (unsigned int)def->field_40;
        RwaObjEnum((UnkFn_00292980)func_002929F0, search);
        obj = (RwaObj *)search[0];
    }
    if (def->field_44 && !wave->state) {
        wave->state = func_002934C0(def->field_44, 0x30806);
        wave->flags |= 8;
    }
    wave->waveDef = def;
    wave->obj = obj;
    if (!_rwaWaveDefIsTargetFormatSupported(def, &wave->targetFormat))
        _rwaWaveSetTargetFormat(wave, &wave->format, 0);
    if (positional && wave->targetFormat.noChannels == 1)
        wave->flags |= 2;
    return wave;
}

// 0x002919D0
extern "C" void _rwaWaveCloseModule(void)
{
    RwaFreeListDestroy(&D_01E7CB30);
    D_004E2A8C = 0;
}

// 0x00291A00
extern "C" RwBool _rwaWaveOpenModule(void)
{
    if (!RwaFreeListCreate(sizeof(RwaWave), 0x10, 0x40, 0, &D_01E7CB30, 0x40806))
        return 0;
    D_004E2A8C = 1;
    return 1;
}

// Wave flag 0x10: the wave's data is fixed up (relocated).
// 0x00291350
extern "C" void _RwaWaveSetFixedUp(RwaWave *wave, RwBool fixed)
{
    if (fixed)
        wave->flags |= 0x10;
    else
        wave->flags &= ~0x10u;
}

extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint);
extern "C" RwaUniqueID *_rwaUniqueIDInitialize(RwaUniqueID *id);
extern "C" RwaWaveDict *RwaWaveDictGetCurrent(void);
extern "C" RwaWaveDict *RwaWaveDictAddWave(RwaWaveDict *dict, RwaWave *wave);

// A new wave of type `def` for object `obj` with the data format `format` (its extra data copied) and the played
// format chosen from `target` (_rwaWaveSetTargetFormat); flag 2 (positional) for a mono target when `positional`;
// state memory when the type wants it; then the type's create callback (with the object) and the current dictionary.
// The struct is `wave` (flag 4) or from a free list (flag 1: the type's own, made on first use, when the type has
// state; else the shared one). Null on a failure; when the extra data gets no memory the wave is left as it is [C
// 0x29191C].
// 0x00291710
extern "C" RwaWave *RwaWaveCreate(RwaWaveDef *def, RwaObj *obj, RwaWaveFormat *format, RwaWaveFormat *target,
                                  RwBool positional, RwaWave *wave)
{
    if (wave) {
        wave->flags = 0;
        wave->flags |= 4;
    } else {
        if (def->field_44) {
            if (!def->field_4C) {
                def->field_4C = RwaFreeListCreate(0x5C, 8, 1, 1, 0, 0x30806);
                if (!def->field_4C)
                    return 0;
            }
            wave = (RwaWave *)RwaFreeListAlloc(def->field_4C, 0x30806);
        } else {
            wave = (RwaWave *)RwaFreeListAlloc(&D_01E7CB30, 0x30806);
        }
        if (!wave)
            return 0;
        wave->flags = 0;
        wave->flags |= 1;
    }
    _rwaUniqueIDInitialize(&wave->uniqueID);
    wave->data = 0;
    wave->waveDef = def;
    wave->uncompressedLength = 0;
    wave->obj = obj;
    wave->format.sampleRate = 0;
    wave->format.dataType = 0;
    wave->format.length = 0;
    wave->format.bitDepth = 0;
    wave->format.noChannels = 0;
    wave->format.flags = 0;
    wave->format.reserved = 0;
    wave->format.miscData = 0;
    wave->format.miscDataSize = 0;
    memcpy(&wave->format, format, sizeof(RwaWaveFormat));
    if (format->miscData && format->miscDataSize) {
        wave->format.miscData = func_002934C0(format->miscDataSize, 0x30806);
        if (!wave->format.miscData)
            return 0;
        memcpy(wave->format.miscData, format->miscData, format->miscDataSize);
    }
    _rwaWaveSetTargetFormat(wave, format, target);
    if (wave->targetFormat.noChannels == 1)
        wave->flags |= positional ? 2 : 0;
    if (wave->waveDef->field_44) {
        wave->state = func_002934C0(def->field_44, 0x30806);
        wave->flags |= 8;
    } else {
        wave->state = 0;
    }
    if (wave->waveDef->field_C && !wave->waveDef->field_C(wave, obj)) {
        if (wave->flags & 8)
            func_00293440(wave->state);
        RwUInt32 flags = wave->flags;
        if (flags & 1) {
            if (wave->waveDef->field_4C)
                RwaFreeListFree(wave->waveDef->field_4C, wave);
            else
                RwaFreeListFree(&D_01E7CB30, wave);
        } else if (!(flags & 4)) {
            func_00293440(wave);
        }
        return 0;
    }
    RwaWaveDictAddWave(RwaWaveDictGetCurrent(), wave);
    return wave;
}
