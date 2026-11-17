// rwaobjdef.o (librwacore.a) -- object definitions: the definition tree, lookup by UUID, instance memory.
// Retail 0x2945C0-0x296F20 [C E map]. Not ported yet (none called in races14): RwaObjDefAliasDestroy,
// _RwaObjDefParamsFree, RwaObjDefDestroy, _rwaObjDefUnregister, _rwaObjDefCloseModule, func_00296F10.
#include "rwa/rwacore.h"

extern RwaObjDef *_rwaGnullObjDef;
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b);
extern "C" RwUInt32 _rwaObjCalcSize(RwaObjDef *def, RwUInt32 flags, RwUInt32 *out);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint);
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaHeapFree(void *heap, void *p);
extern "C" RwUInt32 RwaHeapAlloc(void *heap, RwUInt32 size);
extern "C" void *RwaHeapCreate(RwUInt32 entrySize, RwUInt32 count, RwUInt32 extra, RwUInt32 nBlocks, void *heap);
extern "C" void *_rwaUniqueIDAssignName(void *id, const RwChar *name);
extern "C" void *_rwaUniqueIDAssignUUID(void *id, RwaUUID *uuid);
extern "C" RwaObjInterfaceDef *_rwaObjInterfaceDefSupported(RwaObjInterfaceDef *def, RwaObjDef *objDef, RwUInt32 p2);

typedef RwBool (*UnkFn_00296140)(RwaObjDef *parent, RwaObjDef *def, void *arg);

// An _rwaObjDefEnum callback: 1 when `def` has this UUID.
// 0x00296070
extern "C" RwBool _RwaObjDefFindByUUIDCallback(RwaObjDef *parent, RwaObjDef *def, const RwaUUID *uuid)
{
    return RwaUUIDCompare(def->uniqueID.uuid, uuid) == 0;
}

// Walks the definition tree below `parent` (null: the null definition), depth first, `parent` itself first when
// `includeSelf`; the first definition for which cb(parent of the level, def, arg) returns 1 is returned. *out gets the
// definition itself when it is the starting one, else the parent of its level [C 0x296228 stores s5]; null when none.
// 0x00296140
extern "C" RwaObjDef *_rwaObjDefEnum(RwaObjDef *parent, RwaObjDef **out, UnkFn_00296140 cb, void *arg, RwBool includeSelf)
{
    if (!parent)
        parent = _rwaGnullObjDef;
    if (includeSelf == 1 && cb(parent, parent, arg) == 1) {
        if (out)
            *out = parent;
        return parent;
    }
    for (RwLLLink *l = parent->objDefList.link.next; l != &parent->objDefList.link; l = l->next) {
        RwaObjDef *def = (RwaObjDef *)((char *)l - 0x18);
        if (cb(parent, def, arg) == 1) {
            if (out)
                *out = parent;
            return def;
        }
        RwaObjDef *r = _rwaObjDefEnum(def, out, cb, arg, 0);
        if (r)
            return r;
    }
    if (out)
        *out = 0;
    return 0;
}

// The definition with this UUID below `parent` (searched with a copy of the UUID); *out as _rwaObjDefEnum leaves it.
// 0x00296260
extern "C" RwaObjDef *_rwaObjDefFindByUUID(RwaObjDef *parent, const RwaUUID *uuid, RwaObjDef **out)
{
    RwaUUID copy;
    memcpy(&copy, uuid, sizeof(RwaUUID));
    RwUInt32 found;   // a guest word, as _rwaObjDefEnum stores it
    RwaObjDef *r = _rwaObjDefEnum(parent, (RwaObjDef **)&found, (UnkFn_00296140)_RwaObjDefFindByUUIDCallback, &copy, 1);
    if (out)
        *out = (RwaObjDef *)found;
    return r;
}

// Frees an object's memory: the allocation starts before the object (flag 8: its address in the word below it; flag
// 0x10: in the word after the object's size), and goes back to the definition's instance free list (0x40) or heap
// (0x80), else to the RWA allocator; or to `freeFn(block, arg)` when given and the definition has no allocator or the
// object has flag 0x20.
typedef void (*UnkFn_00296360)(void *block, RwUInt32 arg);
// 0x00296360
extern "C" void _rwaObjDefFreeObj(RwaObj *obj, UnkFn_00296360 freeFn, RwUInt32 arg)
{
    RwaObjDef *def = obj->definition;
    RwUInt32 flags = obj->flags;
    void *block;
    if (flags & 8) {
        block = *((void **)obj - 1);
    } else {
        block = obj;
        if (flags & 0x10) {
            RwUInt32 tmp;
            RwUInt32 size = _rwaObjCalcSize(def, flags & 7, &tmp);
            block = *(void **)((char *)obj + size);
        }
    }
    void *alloc = def->instanceAllocator;
    if (!alloc || (freeFn && (obj->flags & 0x20))) {
        if (freeFn)
            freeFn(block, arg);
        return;
    }
    RwUInt32 dflags = def->flags;
    if (dflags & 0x40)
        RwaFreeListFree((RwaFreeList *)alloc, block);
    else if (dflags & 0x80)
        RwaHeapFree(alloc, block);
    else
        func_00293440(block);
}

// Memory for an object of `size` bytes: the definition's instance free list (0x40) or heap (0x80), else the RWA
// allocator, with the definition's memory hint.
// 0x00296480
extern "C" void *_rwaObjDefAllocObj(RwaObjDef *def, RwUInt32 size)
{
    if (def->flags & 0x40)
        return RwaFreeListAlloc((RwaFreeList *)def->instanceAllocator, def->memoryHint);
    if (def->flags & 0x80)
        return (void *)RwaHeapAlloc(def->instanceAllocator, size);
    return func_002934C0(size, def->memoryHint);
}

// An instance allocator for definitions that want more than one free-list entry: a free list of `size`-byte entries
// (useFreeList) or a heap, aligned to the state alignment. With fewer than 2 entries the allocator word is set to 1
// (not an allocator; the flags stay clear, so objects come from the RWA allocator) [C 0x2965A8]. The definition, or
// null when it had an allocator already or the creation failed.
// 0x002964E0
extern "C" RwaObjDef *_rwaObjDefCreateInstanceAllocator(RwaObjDef *def, RwUInt32 size, RwBool useFreeList)
{
    if (def->instanceAllocator)
        return 0;
    RwUInt32 n = def->noFreeListEntries;
    if ((RwInt32)n < 2) {
        def->instanceAllocator = (void *)1;
    } else {
        RwUInt32 align = 1u << ((def->stateSizeAlign & 0xF0000000) >> 28);
        if (useFreeList) {
            def->instanceAllocator = RwaFreeListCreate(size, n, align, 1, 0, 0x3080A);
            def->flags |= 0x40;
        } else {
            def->instanceAllocator = RwaHeapCreate(size, n, align, 1, 0);
            def->flags |= 0x80;
        }
    }
    return def->instanceAllocator ? def : 0;
}

// The definition's creation-parameter info into *out (its callback is called with a null object); null when it has
// no callback.
// 0x002965C0
extern "C" RwaObjDef *RwaObjDefGetDefaultCreationParams(RwaObjDef *def, RwUInt32 *out)
{
    if (!def->getParamsFunc)
        return 0;
    def->getParamsFunc(0, out);
    return def;
}

// 0x00296600
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, RwaUUID *uuid, const RwChar *name)
{
    _rwaUniqueIDAssignName(def, name);
    _rwaUniqueIDAssignUUID(def, uuid);
    return def;
}

// Makes `intfDef` the definition's default interface when the definition offers all of its parameters.
// 0x00296650
extern "C" RwaObjDef *RwaObjDefSetInterface(RwaObjDef *def, RwaObjInterfaceDef *intfDef)
{
    if (!_rwaObjInterfaceDefSupported(intfDef, def, 1))
        return 0;
    def->interfaceInfo->defaultInterface = intfDef;
    return def;
}

extern RwUInt8 D_01E7CED8[0x18];          // the parameter-type symbol table [G size]
extern "C" void *_rwaSymbolTableFindParamTypeByIndex(void *table, RwUInt32 index);
extern "C" RwaUniqueID *_rwaUniqueIDInitialize(RwaUniqueID *id);
extern "C" RwaObj *func_00295C70(RwaObj *obj);   // a parameter accessor doing nothing (returns the object)

// Every input (selector 1) or output parameter of the definition: the parameter type 0 of the symbol table, not a UUID,
// the no-op accessor, its unique ID cleared.
// 0x00295C80
extern "C" void _RwaObjDefInitParam(RwaObjDef *def, RwUInt32 selector)
{
    void *type = _rwaSymbolTableFindParamTypeByIndex(D_01E7CED8, 0);
    if (selector == 1) {
        RwaInputParamDef *p = def->interfaceInfo->inputParams;
        RwUInt16 n = def->interfaceInfo->noInputs;
        for (RwUInt16 i = 0; (RwInt32)i < (RwInt32)n; i++) {
            p[i].paramHandle.paramType = type;
            p[i].paramHandle.isUUID = 0;
            p[i].setFunc = func_00295C70;
            _rwaUniqueIDInitialize(&p[i].uniqueID);
        }
    } else {
        RwaOutputParamDef *p = def->interfaceInfo->outputParams;
        RwUInt16 n = def->interfaceInfo->noOutputs;
        for (RwUInt16 i = 0; (RwInt32)i < (RwInt32)n; i++) {
            p[i].paramHandle.paramType = type;
            p[i].paramHandle.isUUID = 0;
            p[i].getFunc = func_00295C70;
            _rwaUniqueIDInitialize(&p[i].uniqueID);
        }
    }
}

// The definition when `child` is in its list of definitions (RwaObjDef::objDefLink), else null.
// 0x002962D0
extern "C" RwaObjDef *_rwaObjDefCreateSupported(RwaObjDef *def, RwaObjDef *child)
{
    for (RwLLLink *l = def->objDefList.link.next; l != &def->objDefList.link; l = l->next)
        if ((RwaObjDef *)((char *)l - 0x18) == child)
            return def;
    return 0;
}

extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" void *_rwaUniqueIDFreeData(void *id);
extern "C" RwaParamTypeHandle *_rwaParamTypeHandleRefreshType(RwaParamTypeHandle *handle);
extern "C" RwaLLNode *RwaLListAddData(RwaLLNode *list, void *data);
extern "C" void RwaLListEmpty(RwaLLNode *list);
extern "C" RwaObjDef *_RwaObjDefParamsFree(RwaObjDef *def);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);
extern RwUInt32 _moduleOpening;      // 1 while _rwaObjDefOpenModule creates the null definition
extern RwUInt32 D_004E2B48;          // _moduleOpen of rwaobjdef.o [L name, research/retail_data_names.csv]
extern RwUInt32 D_004E2B50;          // _moduleClosing of rwaobjdef.o [L name, retail_data_names.csv]
extern RwaFreeList D_01E7D130;       // the definitions' free list (0x5C entries) [C 0x296DC0]
extern RwaFreeList D_01E7D100;       // the interface infos' free list (0x18 entries)
extern RwaFreeList D_01E7D0D0;       // the alias infos' free list (8 entries)
extern RwaObjDef D_01E7D070;         // the null definition (_rwaGnullObjDef), a static struct
extern RwaUUID rwaRWAOBJECTUUID;

// Gives the definition its parameters. With neither array: one RWA allocation of (nIn + nOut) entries, outputs after
// the inputs, and definition flag 1 cleared (the arrays are RWA's). Otherwise the caller's arrays, each entry's type
// handle refreshed (0 when one fails, leaving what was set so far) and a missing accessor set to the no-op one; flag 1
// set (the arrays are the caller's). An input array alone (outputs null) goes the second way too [C 0x295EB8].
// 0x00295E80
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaInputParamDef *in, RwUInt32 nIn, RwaOutputParamDef *out,
                                           RwUInt32 nOut)
{
    if (!in && !out) {
        def->flags &= ~1u;
        RwUInt32 n = (RwUInt16)nIn + (RwUInt16)nOut;
        def->interfaceInfo->inputParams = (RwaInputParamDef *)func_002934C0(24 * n, 0x3080A);
        RwaInputParamDef *p = def->interfaceInfo->inputParams;
        if (!p)
            return 0;
        def->interfaceInfo->outputParams = (RwaOutputParamDef *)((char *)p + 24 * (RwUInt16)nIn);
        return def;
    }
    for (RwUInt16 i = 0; (RwInt32)i < (RwInt32)(RwUInt16)nIn; i++) {
        RwaInputParamDef *e = &in[i];
        if (!_rwaParamTypeHandleRefreshType(&e->paramHandle))
            return 0;
        if (!e->setFunc)
            e->setFunc = func_00295C70;
    }
    def->interfaceInfo->inputParams = in;
    def->interfaceInfo->noInputs = (RwUInt16)nIn;
    for (RwUInt16 i = 0; (RwInt32)i < (RwInt32)(RwUInt16)nOut; i++) {
        RwaOutputParamDef *e = &out[i];
        if (!_rwaParamTypeHandleRefreshType(&e->paramHandle))
            return 0;
        if (!e->getFunc)
            e->getFunc = func_00295C70;
    }
    def->flags |= 1;
    def->interfaceInfo->outputParams = out;
    def->interfaceInfo->noOutputs = (RwUInt16)nOut;
    return def;
}

// An _rwaObjDefEnum callback over every definition: one with an interface info (flag 4) that offers all of the
// interface definition's parameters (search[0]) is added to the list search[1]. When adding fails the list is emptied
// and search[1] cleared; nothing more is added then. Always 0 (go on) [C 0x29611C].
// 0x002960A0
extern "C" RwBool _RwaObjDefBuildSupportedListCallback(RwaObjDef *parent, RwaObjDef *def, RwUInt32 *search)
{
    if (!(def->flags & 4))
        return 0;
    if (!search[1])
        return 0;
    if (_rwaObjInterfaceDefSupported((RwaObjInterfaceDef *)search[0], def, 0)) {
        if (!RwaLListAddData((RwaLLNode *)search[1], def)) {
            RwaLListEmpty((RwaLLNode *)search[1]);
            search[1] = 0;
        }
    }
    return 0;
}

// Fills `list` with every definition below `parent` (null: all) that supports the interface definition; 1 unless an
// add failed.
// 0x00296310
extern "C" RwBool _rwaObjDefBuildSupportedList(RwaObjDef *parent, RwaObjInterfaceDef *intfDef, RwaLLNode *list)
{
    RwUInt32 search[2];   // guest words, as the callback reads them
    search[0] = (unsigned int)intfDef;
    search[1] = (unsigned int)list;
    _rwaObjDefEnum(parent, 0, (UnkFn_00296140)_RwaObjDefBuildSupportedListCallback, search, 1);
    return search[1] != 0;
}

// A definition's initial state: parameters reset (flag 4), unique ID cleared, the interface info emptied (flag 4), the
// alias list self-linked (flag 8), no allocator, an empty state of alignment 1, 8 free-list entries, no callbacks.
// 0x00296890
extern "C" void _RwaObjDefInit(RwaObjDef *def)
{
    if (def->flags & 4) {
        _RwaObjDefInitParam(def, 1);
        _RwaObjDefInitParam(def, 2);
    }
    _rwaUniqueIDInitialize(&def->uniqueID);
    if (def->flags & 4) {
        RwaObjDefInterfaceInfo *info = def->interfaceInfo;
        info->interfaceList.link.next = &info->interfaceList.link;
        info->interfaceList.link.prev = &info->interfaceList.link;
        info->defaultInterface = 0;
        info->inputParams = 0;
        info->outputParams = 0;
        info->noInputs = 0;
        info->noOutputs = 0;
    }
    if (def->flags & 8) {
        RwUInt32 *alias = (RwUInt32 *)def->aliasInfo;
        alias[0] = (unsigned int)alias;
        alias[1] = (unsigned int)alias;
    }
    def->instanceAllocator = 0;
    def->stateSizeAlign &= 0xF0000000;
    def->stateSizeAlign &= 0x0FFFFFFF;
    def->stateSizeAlign |= (RwUInt32)(_rwMsbBit[1] - 1) << 28;
    def->noFreeListEntries = 8;
    def->createParamSize = 0;
    def->miscData = 0;
    def->objDefList.link.next = &def->objDefList.link;
    def->objDefList.link.prev = &def->objDefList.link;
    def->objDefLink.prev = 0;
    def->objDefLink.next = 0;
    def->waveDefList.link.next = &def->waveDefList.link;
    def->waveDefList.link.prev = &def->waveDefList.link;
    def->createFunc = 0;
    def->startExecuteFunc = 0;
    def->finishExecuteFunc = 0;
    def->destroyFunc = 0;
    def->unregisterFunc = 0;
    def->getParamsFunc = 0;
}

// Frees what a failed RwaObjDefCreate had allocated (the parts not the caller's).
static void func_002969C0_inl1(RwaObjDef *def)
{
    _RwaObjDefParamsFree(def);
    _rwaUniqueIDFreeData(def);
    RwUInt32 fl = def->flags;
    if ((fl & 8) && !(fl & 0x20))
        RwaFreeListFree(&D_01E7D0D0, def->aliasInfo);
    fl = def->flags;
    if ((fl & 4) && !(fl & 0x10))
        RwaFreeListFree(&D_01E7D100, def->interfaceInfo);
    if (def->flags & 2)
        RwaFreeListFree(&D_01E7D130, def);
}

// A new definition below `parent` (null: the null definition, except while the module opens it). `def`, `info` and
// `alias` are the caller's memory when given, else from RWA's free lists (flags 2, 0x10 / 0x20 tell which are the
// caller's). Flag 4 of `flags`: with an interface info; 8: with an alias list. Linked first in the parent's list.
// 0x002969C0
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 flags, RwaObjDef *def, RwaObjDefInterfaceInfo *info,
                                      void *alias, RwUInt32 memoryHint)
{
    if (!parent && !_moduleOpening)
        parent = _rwaGnullObjDef;
    if (def) {
        def->flags = 0;
    } else {
        def = (RwaObjDef *)RwaFreeListAlloc(&D_01E7D130, 0x3080A);
        if (!def)
            return 0;
        def->flags = 2;
    }
    def->memoryHint = memoryHint;
    if (flags & 4) {
        if (info) {
            def->interfaceInfo = info;
            def->flags |= 0x10;
        } else {
            def->interfaceInfo = (RwaObjDefInterfaceInfo *)RwaFreeListAlloc(&D_01E7D100, 0x3080A);
            if (!def->interfaceInfo) {
                func_002969C0_inl1(def);
                return 0;
            }
        }
        memset(def->interfaceInfo, 0, 0x18);
        def->flags |= 4;
    }
    if (flags & 8) {
        if (alias) {
            def->aliasInfo = alias;
            def->flags |= 0x20;
        } else {
            def->aliasInfo = RwaFreeListAlloc(&D_01E7D0D0, 0x3080A);
            if (!def->aliasInfo) {
                func_002969C0_inl1(def);
                return 0;
            }
        }
        memset(def->aliasInfo, 0, 8);
        def->flags |= 8;
    }
    _RwaObjDefInit(def);
    if (parent) {
        def->objDefLink.next = parent->objDefList.link.next;
        def->objDefLink.prev = &parent->objDefList.link;
        parent->objDefList.link.next->prev = &def->objDefLink;
        parent->objDefList.link.next = &def->objDefLink;
    }
    return def;
}

typedef RwBool (*UnkFn_00296D00)(void);

// Calls each of the `count` register functions in turn; 0 at the first that fails, else 1. They are called without
// arguments: retail's a0 is the table on the first call and whatever the previous callee left after that [C 0x296D2C
// jalr with a0 unset]; ours passes the entry registers each time [L: the tables' functions take no arguments,
// _objDefs 0x4E1C14, _objDefsInterfaces 0x49F5C0].
// 0x00296D00
extern "C" RwBool _rwaObjDefRegister(UnkFn_00296D00 *funcs, RwUInt32 count)
{
    for (RwUInt32 i = 0; i < count; i++)
        if (!funcs[i]())
            return 0;
    return 1;
}

// Opens the module: the three free lists (definitions, interface infos, alias infos) and the null definition (an
// empty name, rwaRWAOBJECTUUID, one free-list entry). 0 and everything undone on a failure.
// 0x00296DC0
extern "C" RwBool _rwaObjDefOpenModule(void)
{
    _moduleOpening = 1;
    D_004E2B50 = 0;
    if (RwaFreeListCreate(0x5C, 8, 0x40, 0, &D_01E7D130, 0x4080A)) {
        if (RwaFreeListCreate(0x18, 8, 0x40, 0, &D_01E7D100, 0x4080A)) {
            if (RwaFreeListCreate(8, 8, 0x40, 0, &D_01E7D0D0, 0x4080A)) {
                D_004E2B48 = 1;
                _rwaGnullObjDef = RwaObjDefCreate(0, 0, &D_01E7D070, 0, 0, 0x4080A);
                RwaObjDef *def = _rwaGnullObjDef;
                if (def) {
                    _rwaUniqueIDAssignName(def, 0);
                    _rwaUniqueIDAssignUUID(def, &rwaRWAOBJECTUUID);
                    _rwaGnullObjDef->noFreeListEntries = 1;
                    _moduleOpening = 0;
                    return 1;
                }
                RwaFreeListDestroy(&D_01E7D0D0);
            }
            RwaFreeListDestroy(&D_01E7D100);
        }
        RwaFreeListDestroy(&D_01E7D130);
    }
    _moduleOpening = 0;
    D_004E2B48 = 0;
    return 0;
}
