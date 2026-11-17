// rwaparam.o (librwacore.a) -- parameter types (RwaParamType) and the handles that name them by UUID or by pointer.
// Retail 0x291C40-0x29268C [C E map]. Not ported yet (not called in races14): _rwaParamTypeCloseModule 0x291F00,
// RwaParamTypeDestroy 0x2920D0.
#include "rwa/rwacore.h"

struct UnkStruct_00291C20;
extern UnkStruct_00291C20 D_01E7CED8;   // the symbol table of all parameter types (rwasymboltable.o)
extern "C" RwaParamType *_rwaSymbolTableFindParamTypeByUUID(UnkStruct_00291C20 *table, const RwaUUID *uuid);
extern "C" RwaParamType *RwaParamTypeCreateAtomic(RwInt32 type, void *rangeMin, void *rangeMax, RwaParamType *pt,
                                                  char *storage);
extern "C" RwUInt32 RwaSizeOf(RwUInt32 type);
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" RwaUniqueID *_rwaUniqueIDInitialize(RwaUniqueID *id);
extern "C" RwaUniqueID *_rwaUniqueIDAssignUUID(RwaUniqueID *id, RwaUUID *uuid);
extern "C" RwaUniqueID *_rwaUniqueIDAssignName(RwaUniqueID *id, const RwChar *name);
extern "C" UnkStruct_00291C20 *_rwaSymbolTableAddParamType(UnkStruct_00291C20 *table, RwaParamType *pt);
extern "C" void _rwaSymbolTableInit(UnkStruct_00291C20 *table);
extern "C" void RwaParamTypeDestroy(RwaParamType *pt, RwBool p1);
extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);

// _paramFreeLists [C name, retail_data_names.csv]: the free lists range values come from, one per value size
// (1, 2, 4, 8, 0x10 bytes) [C data 0x49D230].
struct UnkStruct_0049D230 {
    RwaFreeList *freeList;    // 0x0
    RwUInt32 size;            // 0x4
};
extern UnkStruct_0049D230 _paramFreeLists[5];
extern RwaFreeList D_01E7CEB0;   // the parameter types' own free list (0x28 entries)
extern RwBool _moduleOpen;       // of rwaparam.o [C name 0x4E2A98]
extern RwBool _moduleClosing;    // of rwaparam.o [C name 0x4E2AD0]

// One entry of a parameter type table (_paramTypeSetup, 15 of them), 0x20 bytes [C 0x291C78 `sll 5`; fields
// 0x291C84-0x291E40]. Not in the Revenge stabs.
struct UnkStruct_0049D260 {
    const RwChar *field_0;    // 0x00 the name
    RwUInt16 field_4;         // 0x04 the RwaType (0x1000 / 0x2000 / 0x4000 handled apart)
    unsigned char pad_6[2];
    RwUInt32 field_8;         // 0x08 the range size (bytes from field_C to the maximum); 0x2000: the maximum itself
    char *field_C;            // 0x0C the range minimum (the maximum follows it)
    RwaUUID *field_10;        // 0x10 its UUID
    RwaUUID *field_14;        // 0x14 the UUID of the type it is based on, or null
    RwaParamType *field_18;   // 0x18 the caller's struct, or null
    char *field_1C;           // 0x1C storage for the range values, or null
};
extern UnkStruct_0049D260 _paramTypeSetup[15];

// A handle that still names its type by UUID is resolved to the type; null when no type has that UUID.
// 0x00291EA0
extern "C" RwaParamTypeHandle *_rwaParamTypeHandleRefreshType(RwaParamTypeHandle *handle)
{
    if (handle->isUUID != 1)
        return handle;
    RwaParamType *pt = _rwaSymbolTableFindParamTypeByUUID(&D_01E7CED8, (const RwaUUID *)handle->paramType);
    if (!pt)
        return 0;
    handle->paramType = pt;
    handle->isUUID = 0;
    return handle;
}

// A new type based on `base` with the given range; a missing bound is taken from `base` while `p3` is nonzero. Retail
// re-reads the same `base` each time (it never walks further up the chain), so a base without that bound and with a
// parent type loops for ever [C 0x29227C-0x292294]; kept as it is.
// 0x00292230
// `p3` is the struct handed on to RwaParamTypeCreateAtomic and `storage` its storage (t0, passed on unchanged) [C].
extern "C" RwaParamType *RwaParamTypeCreateAlias(void *rangeMin, void *rangeMax, RwaParamType *base, RwaParamType *p3,
                                                 char *storage)
{
    RwInt16 type = base->type;
    while (!rangeMin && p3) {
        p3 = base->paramType;
        rangeMin = base->rangeMin;
    }
    while (!rangeMax && p3) {
        p3 = base->paramType;
        rangeMax = base->rangeMax;
    }
    RwaParamType *pt = RwaParamTypeCreateAtomic(type, rangeMin, rangeMax, p3, storage);
    if (!pt)
        return 0;
    pt->paramType = base;
    pt->type |= 0x4000;
    return pt;
}

// The free list for range values of `size` bytes, or null.
static RwaFreeList *func_002922D0_inl1(RwUInt32 size)
{
    for (RwUInt32 i = 0; i < 5; i++)
        if (size == _paramFreeLists[i].size)
            return _paramFreeLists[i].freeList;
    return 0;
}

// Gives the type's range values back to their free list, sized by the type's current RwaType (still the struct's old
// one when CreateAtomic calls this: it stores the type only at the end [C 0x292454, 0x292394]).
static void func_002922D0_inl2(RwaParamType *pt)
{
    if (pt->rangeMin) {
        RwaFreeList *fl = func_002922D0_inl1(RwaSizeOf(pt->type));
        if (fl)
            RwaFreeListFree(fl, pt->rangeMin);
    }
    if (pt->rangeMax) {
        RwaFreeList *fl = func_002922D0_inl1(RwaSizeOf(pt->type));
        if (fl)
            RwaFreeListFree(fl, pt->rangeMax);
    }
}

// A new atomic type: `pt` is the caller's struct (flag 1) or from the free list; the range values are copied into
// `storage` (flag 2: minimum then maximum) or into entries of the free list for their size. Added to the symbol table.
// Retail's failure handling falls through [C]: a struct from the free list gets flags 0 before the null test
// (0x2923C4); when a range value gets no memory the cleanup runs (the struct itself goes back to the free list when it
// is not the caller's) and the function carries on and copies into the null range (0x292558-0x292664). Kept as is.
// 0x002922D0
extern "C" RwaParamType *RwaParamTypeCreateAtomic(RwInt32 type, void *rangeMin, void *rangeMax, RwaParamType *pt,
                                                  char *storage)
{
    RwUInt32 size = RwaSizeOf(type);
    if (pt) {
        pt->flags = 1;
    } else {
        pt = (RwaParamType *)RwaFreeListAlloc(&D_01E7CEB0, 0x30803);
        pt->flags = 0;
    }
    if (!pt)
        return 0;
    pt->rangeMin = 0;
    pt->rangeMax = 0;
    if (storage) {
        pt->flags |= 2;
        if (rangeMin) {
            pt->rangeMin = storage;
            if (rangeMax)
                pt->rangeMax = storage + RwaSizeOf(type);
        } else if (rangeMax) {
            pt->rangeMax = storage;
        }
    } else {
        if (rangeMin) {
            RwaFreeList *fl = func_002922D0_inl1(RwaSizeOf(type));
            pt->rangeMin = fl ? RwaFreeListAlloc(fl, 0x30803) : 0;
            if (!pt->rangeMin && !(pt->flags & 2))
                func_002922D0_inl2(pt);
        }
        if (rangeMax) {
            RwaFreeList *fl = func_002922D0_inl1(RwaSizeOf(type));
            pt->rangeMax = fl ? RwaFreeListAlloc(fl, 0x30803) : 0;
            if (!pt->rangeMax) {
                if (!(pt->flags & 1))
                    RwaFreeListFree(&D_01E7CEB0, pt);
                if (!(pt->flags & 2))
                    func_002922D0_inl2(pt);
            }
        }
    }
    if (rangeMin)
        memcpy(pt->rangeMin, rangeMin, size);
    if (rangeMax)
        memcpy(pt->rangeMax, rangeMax, size);
    _rwaUniqueIDInitialize(&pt->uniqueID);
    pt->type = (RwInt16)type;
    pt->paramType = 0;
    _rwaSymbolTableAddParamType(&D_01E7CED8, pt);
    return pt;
}

// Creates the `n` parameter types of a table, each with its UUID and name. 0x1000: a type based on its base type's
// range (or a plain one without a base); 0x2000: the range 0 .. field_8, based on its base type when there is one
// (then flagged an alias of the base's RwaType); 0x4000: an alias of the base type; any other: an atomic type over
// field_C. On a failure the types made so far are destroyed (looked up by UUID) and the result is 0; else the table.
// 0x00291C40
extern "C" UnkStruct_0049D260 *_rwaParamTypeCreateByTypeDefs(UnkStruct_0049D260 *defs, RwUInt32 n)
{
    for (RwUInt32 i = 0; i < n; i++) {
        UnkStruct_0049D260 *e = &defs[i];
        RwaParamType *pt;
        if (e->field_4 == 0x2000) {
            RwaParamType *base = _rwaSymbolTableFindParamTypeByUUID(&D_01E7CED8, e->field_14);
            RwUInt32 range[2];
            range[1] = 0;
            range[0] = e->field_8;
            pt = RwaParamTypeCreateAtomic(0x2000, &range[1], &range[0], e->field_18, e->field_1C);
            if (pt) {
                RwUInt16 t = 0x2000;
                if (base) {
                    pt->paramType = base;
                    t = (RwUInt16)base->type | 0x4000;
                }
                pt->type |= t;
            }
        } else if (e->field_4 == 0x4000) {
            RwaParamType *base = _rwaSymbolTableFindParamTypeByUUID(&D_01E7CED8, e->field_14);
            RwUInt32 sz = RwaSizeOf(base->type);
            pt = RwaParamTypeCreateAlias(e->field_C, e->field_C + sz, base, e->field_18, e->field_1C);
        } else if (e->field_4 == 0x1000) {
            RwaParamType *base = 0;
            if (e->field_14)
                base = _rwaSymbolTableFindParamTypeByUUID(&D_01E7CED8, e->field_14);
            if (!base) {
                pt = RwaParamTypeCreateAtomic(0x1000, 0, 0, e->field_18, e->field_1C);
            } else {
                pt = RwaParamTypeCreateAtomic(0x1000, base->rangeMin, base->rangeMax, e->field_18, e->field_1C);
                if (pt) {
                    pt->type |= 0;
                    pt->paramType = base;
                }
            }
        } else {
            pt = RwaParamTypeCreateAtomic((RwInt16)e->field_4, e->field_C, e->field_C + e->field_8, e->field_18,
                                          e->field_1C);
        }
        if (!pt) {
            for (; i != 0; i--)
                RwaParamTypeDestroy(_rwaSymbolTableFindParamTypeByUUID(&D_01E7CED8, defs[i - 1].field_10), 1);
            return 0;
        }
        _rwaUniqueIDAssignUUID(&pt->uniqueID, e->field_10);
        _rwaUniqueIDAssignName(&pt->uniqueID, e->field_0);
    }
    return defs;
}

// Opens the module: the symbol table, the five range-value free lists, the types' free list and the built-in types
// (_paramTypeSetup). When a range-value free list cannot be made, the ones made before it are destroyed and the loop
// carries on from the second list (index 1, not 0) [C 0x29209C: `b 0x291FEC` with s0 + 1]; kept as is.
// 0x00291F90
extern "C" RwBool _rwaParamTypeOpenModule(void)
{
    _rwaSymbolTableInit(&D_01E7CED8);
    RwUInt32 i = 0;
    while (i < 5) {
        if (!RwaFreeListCreate(_paramFreeLists[i].size, 8, 0x40, 0, _paramFreeLists[i].freeList, 0x40803)) {
            for (; i != 0; i--)
                RwaFreeListDestroy(_paramFreeLists[i - 1].freeList);
        }
        i++;
    }
    _moduleOpen = 1;
    _moduleClosing = 0;
    if (RwaFreeListCreate(0x28, 0x10, 0x40, 0, &D_01E7CEB0, 0x40803)) {
        if (_rwaParamTypeCreateByTypeDefs(_paramTypeSetup, 15))
            return 1;
        RwaFreeListDestroy(&D_01E7CEB0);
    }
    for (i = 5; i != 0; i--)
        RwaFreeListDestroy(_paramFreeLists[i - 1].freeList);
    _moduleOpen = 0;
    return 0;
}
