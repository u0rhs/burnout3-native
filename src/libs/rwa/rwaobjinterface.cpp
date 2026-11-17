// rwaobjinterface (librwacore.a, object name truncated in the E map) -- RWA interface definitions (named parameter
// sets identified by UUIDs) and the interfaces an object definition gets when one is attached to it.
// Retail 0x293A60-0x29454C [C E map].
#include "rwa/rwacore.h"

extern RwLinkList _objInterfaceDefList;
extern RwUInt32 D_004E2AF4;            // _moduleOpen of rwaobjinterface [L name, research/retail_data_names.csv]
extern RwaFreeList D_01E7CF80;         // the definitions' free list (static struct)
extern "C" void *memset(void *dst, int c, unsigned int n);
extern "C" RwInt32 RwaUUIDCompare(const struct RwaUUID *a, const struct RwaUUID *b);
extern "C" void *_rwaUniqueIDInitialize(void *id);
extern "C" void *_rwaUniqueIDFreeData(void *id);
extern "C" void *_rwaUniqueIDAssignUUID(void *id, const struct RwaUUID *uuid);
extern "C" void *_rwaUniqueIDAssignName(void *id, const char *name);
extern "C" void *_rwaParamTypeHandleRefreshType(void *handle);
extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);

typedef RwBool (*UnkFn_00294420)(void);   // the module callbacks of _rwaObjInterfaceRegister (no arguments [L])

// The definition with this UUID, or null.
// 0x00293A60
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefFindByUUID(const struct RwaUUID *uuid)
{
    if (!uuid)
        return 0;
    for (RwLLLink *n = _objInterfaceDefList.link.next; n != &_objInterfaceDefList.link; n = n->next) {
        RwaObjInterfaceDef *def = (RwaObjInterfaceDef *)((char *)n - 0x20);
        if (RwaUUIDCompare(def->uniqueID.uuid, uuid) == 0)
            return def;
    }
    return 0;
}

// Destroys a definition: its callback, its and its parameters' identifiers, the parameter memory unless the caller's,
// its link, and the struct unless the caller's. Always 1.
// 0x00293AE0
extern "C" RwBool RwaObjInterfaceDefDestroy(RwaObjInterfaceDef *def)
{
    if (def->unregFunc)
        def->unregFunc(def);
    _rwaUniqueIDFreeData(def);
    RwUInt32 n = def->noInputParams;
    for (RwUInt32 i = 0; i < n; i++)
        _rwaUniqueIDFreeData(&def->inputParams[i]);
    n = def->noOutputParams;
    for (RwUInt32 i = 0; i < n; i++)
        _rwaUniqueIDFreeData(&def->outputParams[i]);
    if (!(def->flags & 1))
        func_00293440(def->inputParams);
    def->link.prev->next = def->link.next;
    def->link.next->prev = def->link.prev;
    if (!(def->flags & 2))
        RwaFreeListFree(&D_01E7CF80, def);
    return 1;
}

// A new definition in `def` (the caller's struct, flags 2) or from the free list (flags 0), linked first in
// _objInterfaceDefList; null when the free list fails.
// 0x00293C00
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def)
{
    if (def) {
        def->flags = 2;
    } else {
        def = (RwaObjInterfaceDef *)RwaFreeListAlloc(&D_01E7CF80, 0x3080B);
        if (!def)
            return 0;
        def->flags = 0;
    }
    def->noInputParams = 0;
    def->noOutputParams = 0;
    def->inputParams = 0;
    def->outputParams = 0;
    def->unregFunc = 0;
    _rwaUniqueIDInitialize(def);
    def->link.prev = 0;
    def->link.next = 0;
    def->link.next = _objInterfaceDefList.link.next;
    def->link.prev = &_objInterfaceDefList.link;
    _objInterfaceDefList.link.next->prev = &def->link;
    _objInterfaceDefList.link.next = &def->link;
    return def;
}

// 0x00293CB0
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, const struct RwaUUID *uuid,
                                                          const char *name)
{
    _rwaUniqueIDAssignUUID(def, uuid);
    _rwaUniqueIDAssignName(def, name);
    return def;
}

// The definition's parameters: the caller's two arrays (each entry's type handle refreshed; 0 when one fails), or, when
// either array is null, one zeroed allocation for nSet + nGet entries (the counts are then left as they were [C
// 0x293E18-0x293E74]). Earlier parameter memory is freed unless it was the caller's.
// 0x00293CF0
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *setters,
                                                       RwUInt32 nSet, RwaObjInterfaceDefParam *getters, RwUInt32 nGet)
{
    if (def->inputParams && def->outputParams) {
        if (!(def->flags & 1))
            func_00293440(def->inputParams);
        def->inputParams = 0;
        def->outputParams = 0;
    }
    if (setters && getters) {
        for (RwUInt32 i = 0; i < nSet; i++) {
            setters[i].uniqueID.flags = 0;
            if (!_rwaParamTypeHandleRefreshType(&setters[i].paramType))
                return 0;
        }
        for (RwUInt32 i = 0; i < nGet; i++) {
            getters[i].uniqueID.flags = 0;
            if (!_rwaParamTypeHandleRefreshType(&getters[i].paramType))
                return 0;
        }
        def->inputParams = setters;
        def->outputParams = getters;
        def->noInputParams = (RwUInt16)nSet;
        def->noOutputParams = (RwUInt16)nGet;
        def->flags |= 1;
        return def;
    }
    RwUInt32 bytes = (nSet + nGet) * sizeof(RwaObjInterfaceDefParam);
    def->inputParams = (RwaObjInterfaceDefParam *)func_002934C0(bytes, 0x3080B);
    if (!def->inputParams)
        return 0;
    def->outputParams = def->inputParams + nSet;
    memset(def->inputParams, 0, bytes);
    def->flags &= ~1u;
    return def;
}

// Whether `objDef` offers every parameter of `def` (each settable one among its settable ones, each readable one
// among its readable ones, by UUID); a2 (always 1 at the one caller) is not read. Null or `def`.
// 0x00293EA0
extern "C" RwaObjInterfaceDef *_rwaObjInterfaceDefSupported(RwaObjInterfaceDef *def, RwaObjDef *objDef, RwUInt32 p2)
{
    RwaObjDefInterfaceInfo *info = objDef->interfaceInfo;
    RwBool ok = 1;
    RwUInt32 n = def->noInputParams, m = info->noInputs;
    for (RwUInt32 i = 0; i < n; i++) {
        RwBool found = 0;
        for (RwUInt32 j = 0; j < m; j++) {
            if (RwaUUIDCompare(info->inputParams[j].uniqueID.uuid, def->inputParams[i].uniqueID.uuid) == 0) {
                found = 1;
                break;
            }
        }
        if (!found)
            ok = 0;
    }
    n = def->noOutputParams;
    m = objDef->interfaceInfo->noOutputs;
    for (RwUInt32 i = 0; i < n; i++) {
        RwBool found = 0;
        for (RwUInt32 j = 0; j < m; j++) {
            if (RwaUUIDCompare(info->outputParams[j].uniqueID.uuid, def->outputParams[i].uniqueID.uuid) == 0) {
                found = 1;
                break;
            }
        }
        if (!found)
            ok = 0;
    }
    return ok ? def : 0;
}

// Drops an attachment: 0 while extra attachments remain (one is dropped); else the interface is unlinked and, unless
// from the caller's allocator, freed: 1.
// 0x00294040
extern "C" RwInt32 RwaObjInterfaceRemove(RwaObjInterface *intf)
{
    if (intf->refCount) {
        intf->refCount = intf->refCount - 1;
        return 0;
    }
    intf->link.prev->next = intf->link.next;
    intf->link.next->prev = intf->link.prev;
    if (!(intf->flags & 1))
        func_00293440(intf);
    return 1;
}

typedef void *(*UnkFn_002940B0)(RwUInt32 size, RwUInt32 arg);

// The interface of `def` on `objDef`: an attached one counts one more attachment; otherwise, when `objDef` offers all
// of def's parameters, a new one (from `alloc(size, allocArg)` if given, else the RWA allocator) whose tables map each
// parameter to the object definition's accessor and index, matched by UUID (the last match wins: retail does not stop
// at the first [C 0x29432C -> 0x294200]). Null when unsupported or out of memory.
// 0x002940B0
extern "C" RwaObjInterface *RwaObjInterfaceAttach(RwaObjInterfaceDef *def, RwaObjDef *objDef, UnkFn_002940B0 alloc,
                                                  RwUInt32 allocArg)
{
    RwaObjDefInterfaceInfo *info = objDef->interfaceInfo;
    for (RwLLLink *n = info->interfaceList.link.next; n != &info->interfaceList.link; n = n->next) {
        RwaObjInterface *found = (RwaObjInterface *)((char *)n - 0x14);
        if (found->interfaceDef == def) {
            found->refCount = found->refCount + 1;
            return found;
        }
    }
    if (!_rwaObjInterfaceDefSupported(def, objDef, 1))
        return 0;
    RwUInt32 size = sizeof(RwaObjInterface) + def->noInputParams * 8 + def->noOutputParams * 8;
    RwaObjInterface *intf;
    if (alloc) {
        intf = (RwaObjInterface *)alloc(size, allocArg);
        if (intf)
            intf->flags = 1;
    } else {
        intf = (RwaObjInterface *)func_002934C0(size, 0x3080B);
        if (intf)
            intf->flags = 0;
    }
    if (!intf)
        return 0;
    intf->inputParamMaps = (RwaInputParamMap *)(intf + 1);
    intf->outputParamMaps = (RwaOutputParamMap *)(intf->inputParamMaps + def->noInputParams);
    intf->interfaceDef = def;
    intf->refCount = 0;
    RwUInt16 nSet = def->noInputParams, mSet = objDef->interfaceInfo->noInputs;
    for (RwUInt16 i = 0; i < nSet; i++) {
        for (RwUInt16 j = 0; j < mSet; j++) {
            if (RwaUUIDCompare(info->inputParams[j].uniqueID.uuid, def->inputParams[i].uniqueID.uuid) == 0) {
                intf->inputParamMaps[i].paramIndex = j;
                intf->inputParamMaps[i].setFunc = info->inputParams[j].setFunc;
            }
        }
    }
    RwUInt16 nGet = def->noOutputParams, mGet = objDef->interfaceInfo->noOutputs;
    for (RwUInt16 i = 0; i < nGet; i++) {
        for (RwUInt16 j = 0; j < mGet; j++) {
            if (RwaUUIDCompare(info->outputParams[j].uniqueID.uuid, def->outputParams[i].uniqueID.uuid) == 0) {
                intf->outputParamMaps[i].paramIndex = j;
                intf->outputParamMaps[i].getFunc = info->outputParams[j].getFunc;
            }
        }
    }
    intf->link.next = info->interfaceList.link.next;
    intf->link.prev = &info->interfaceList.link;
    info->interfaceList.link.next->prev = &intf->link;
    info->interfaceList.link.next = &intf->link;
    return intf;
}

// Runs each module's unregister callback; 0 at the first that fails, else 1.
// 0x002943B0
extern "C" RwBool _rwaObjInterfaceUnregister(UnkFn_00294420 *funcs, RwUInt32 n)
{
    for (RwUInt32 i = 0; i < n; i++)
        if (!funcs[i]())
            return 0;
    return 1;
}

// Runs each module's register callback; 0 at the first that fails, else 1.
// 0x00294420
extern "C" RwBool _rwaObjInterfaceRegister(UnkFn_00294420 *funcs, RwUInt32 n)
{
    for (RwUInt32 i = 0; i < n; i++)
        if (!funcs[i]())
            return 0;
    return 1;
}

// 0x00294490
extern "C" void _rwaObjInterfaceCloseModule(void)
{
    while (_objInterfaceDefList.link.next != &_objInterfaceDefList.link)
        RwaObjInterfaceDefDestroy((RwaObjInterfaceDef *)((char *)_objInterfaceDefList.link.next - 0x20));
    _objInterfaceDefList.link.next = &_objInterfaceDefList.link;
    _objInterfaceDefList.link.prev = &_objInterfaceDefList.link;
    RwaFreeListDestroy(&D_01E7CF80);
    D_004E2AF4 = 0;
}

// 0x002944F0
extern "C" RwBool _rwaObjInterfaceOpenModule(void)
{
    _objInterfaceDefList.link.next = &_objInterfaceDefList.link;
    _objInterfaceDefList.link.prev = &_objInterfaceDefList.link;
    if (!RwaFreeListCreate(sizeof(RwaObjInterfaceDef), 0x10, 0x40, 0, &D_01E7CF80, 0x4080B))
        return 0;
    D_004E2AF4 = 1;
    return 1;
}
