// rwaintf.o (librwacore.a) -- RWA object handles: parameter accessors through an object interface.
// Retail 0x294610-0x294D2C [C E map: research/retail_names.csv]. C port written from retail's code (audio1); every
// function is proven by the checker on live captures, never by inspection.
//
// Parameters (all accessors) [C callers]: (handle, parameter index, type tag, value). The type tag (a2) is never read
// here: GtSoundVirtualVoice 0x3875CC passes 2 to RwaObjHandleGetParamUInt32, the setter callers leave a2 as it is.
#include "rwa/rwacore.h"

extern "C" void *memset(void *dst, int c, unsigned int n);

// Getters: the value comes back through a stack slot the accessor writes; *ok (if given) = whether it succeeded.
// On failure retail returns the slot's stale stack word (the accessor did not write it) [C 0x294674 `lw v0, 0x3C(sp)`];
// ours returns 0 there [L: no caller reads the value without checking ok].

// 0x00294610
extern "C" void *RwaObjHandleGetParamPointer(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwBool *ok)
{
    RwUInt32 value = 0;   // a guest pointer, 4 bytes as the accessor writes it
    RwaOutputParamMap *e = &handle->objInterface->outputParamMaps[param];
    RwBool r = e->getFunc(handle->obj, e->paramIndex, &value);
    if (ok)
        *ok = r ? 1 : 0;
    return (void *)value;
}

// 0x00294690
extern "C" RwUInt32 RwaObjHandleGetParamUInt32(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwBool *ok)
{
    RwUInt32 value = 0;
    RwaOutputParamMap *e = &handle->objInterface->outputParamMaps[param];
    RwBool r = e->getFunc(handle->obj, e->paramIndex, &value);
    if (ok)
        *ok = r ? 1 : 0;
    return value;
}

// 0x00294710
extern "C" RwInt32 RwaObjHandleGetParamInt32(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwBool *ok)
{
    RwInt32 value = 0;
    RwaOutputParamMap *e = &handle->objInterface->outputParamMaps[param];
    RwBool r = e->getFunc(handle->obj, e->paramIndex, &value);
    if (ok)
        *ok = r ? 1 : 0;
    return value;
}

// Setters: a null handle is a no-op returning null; otherwise the handle, or null when the accessor fails.

// 0x00294790
extern "C" RwaObjHandle *RwaObjHandleSetParamPointer(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, void *value)
{
    if (!handle)
        return 0;
    RwaInputParamMap *e = &handle->objInterface->inputParamMaps[param];
    if (!e->setFunc(handle->obj, e->paramIndex, value))
        return 0;
    return handle;
}

// 0x002947F0
extern "C" RwaObjHandle *RwaObjHandleSetParamReal(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwReal value)
{
    if (!handle)
        return 0;
    RwaInputParamMap *e = &handle->objInterface->inputParamMaps[param];
    if (!e->setFunc(handle->obj, e->paramIndex, &value))
        return 0;
    return handle;
}

// 0x00294860
extern "C" RwaObjHandle *RwaObjHandleSetParamUInt32(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwUInt32 value)
{
    if (!handle)
        return 0;
    RwaInputParamMap *e = &handle->objInterface->inputParamMaps[param];
    if (!e->setFunc(handle->obj, e->paramIndex, &value))
        return 0;
    return handle;
}

// 0x002948D0
extern "C" RwaObjHandle *RwaObjHandleSetParamInt32(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwInt32 value)
{
    if (!handle)
        return 0;
    RwaInputParamMap *e = &handle->objInterface->inputParamMaps[param];
    if (!e->setFunc(handle->obj, e->paramIndex, &value))
        return 0;
    return handle;
}

// Data: the pointer goes to the accessor as is; no null check of the handle [C 0x294954 reads it first].
// 0x00294940
extern "C" RwaObjHandle *RwaObjHandleSetParamData(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, void *data)
{
    RwaInputParamMap *e = &handle->objInterface->inputParamMaps[param];
    if (!e->setFunc(handle->obj, e->paramIndex, data))
        return 0;
    return handle;
}

// ---- handles

// Creation parameters, 0x18 bytes; null means {2, 0, 0, 0, 0, 0} [C 0x294B94].
struct RwaObjHandleCreateParams {
    RwUInt32 field_0;
    unsigned char pad_4[0x8];
    RwUInt32 field_C;                  // 0xC to RwaObjInterfaceAttach
    RwUInt32 field_10;                 // 0x10 to RwaObjInterfaceAttach
    RwaObjHandle *field_14;            // 0x14 a caller's handle struct, or null (from the free list)
};
// What a release reports, 0x10 bytes.
struct RwaObjHandleReleaseInfo {
    RwUInt32 field_0;                  // 0x0 to RwaObjDestroy
    RwUInt32 field_4;                  // 0x4 to RwaObjDestroy
    RwaObjHandle *field_8;             // 0x8 the released handle
    RwaObjInterface *field_C;          // 0xC the interface, when RwaObjInterfaceRemove returned 1
};

extern RwaFreeList D_01E7CFE0;         // the handles' free list (static struct)
extern RwLinkList _handleList;
extern RwUInt32 D_004E2B00;            // _moduleOpen of rwaintf.o [L name, research/retail_data_names.csv]
extern "C" RwaObjInterface *RwaObjInterfaceAttach(RwaObjInterfaceDef *intfDef, RwaObjDef *def, RwUInt32 p2, RwUInt32 p3);
extern "C" RwInt32 RwaObjInterfaceRemove(RwaObjInterface *intf);
extern "C" void RwaObjDestroy(RwaObj *obj, RwUInt32 p1, RwUInt32 p2);
extern "C" void *RwaFreeListAlloc(RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListFree(RwaFreeList *fl, void *entry);
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);

// Drops one reference: an extra reference (refCount) first; otherwise the interface is removed, the object's count
// goes down (flag 2, set by every create, keeps the object), the handle is unlinked and, unless it is the caller's
// struct, freed. No result [L: v0 is whatever the last call left].
// 0x002949A0
extern "C" void RwaObjHandleRelease(RwaObjHandle *h, RwaObjHandleReleaseInfo *info)
{
    RwaObjHandleReleaseInfo local;
    memset(&local, 0, sizeof(local));
    if (!info)
        info = &local;
    if (h->refCount) {
        h->refCount--;
        return;
    }
    if (h->objInterface && RwaObjInterfaceRemove(h->objInterface) == 1)
        info->field_C = h->objInterface;
    RwaObj *obj = h->obj;
    RwUInt32 *count = func_00294AF0_inl1(obj);
    *count = *count - 1;
    if (*count == 0 && !(h->flags & 2))
        RwaObjDestroy(obj, info->field_0, info->field_4);
    h->link.prev->next = h->link.next;
    h->link.next->prev = h->link.prev;
    if (!(h->flags & 1))
        RwaFreeListFree(&D_01E7CFE0, h);
    info->field_8 = h;
}

// A handle on `obj` through an interface of definition `intfDef` (null: the object definition's default one); null when no handle can be had or the
// interface cannot be attached. Retail stores flags 0 into the free list's result before testing it for null [C
// 0x294BC8]; not modelled (the free list fails only when the allocator does).
// 0x00294AF0
extern "C" RwaObjHandle *RwaObjHandleCreateUsingObj(RwaObj *obj, RwaObjInterfaceDef *intfDef, RwaObjHandleCreateParams *params)
{
    RwaObjHandleCreateParams defaults;
    if (!params) {
        defaults.field_0 = 2;
        defaults.field_10 = 0;
        defaults.field_C = 0;
        defaults.field_14 = 0;
        params = &defaults;
    }
    RwaObjHandle *h = params->field_14;
    if (h) {
        h->flags = 1;
    } else {
        h = (RwaObjHandle *)RwaFreeListAlloc(&D_01E7CFE0, 0x3080C);
        if (!h)
            return 0;
        h->flags = 0;
    }
    h->obj = obj;
    h->objInterface = 0;
    h->refCount = 0;
    h->flags |= 2;
    if (!intfDef)
        intfDef = obj->definition->interfaceInfo->defaultInterface;
    if (intfDef) {
        h->objInterface = RwaObjInterfaceAttach(intfDef, obj->definition, params->field_C, params->field_10);
        if (!h->objInterface) {
            if (!(h->flags & 1))
                RwaFreeListFree(&D_01E7CFE0, h);
            return 0;
        }
    }
    RwUInt32 *count = func_00294AF0_inl1(h->obj);
    *count = *count + 1;
    h->link.prev = 0;
    h->link.next = 0;
    h->link.next = _handleList.link.next;
    h->link.prev = &_handleList.link;
    _handleList.link.next->prev = &h->link;
    _handleList.link.next = &h->link;
    return h;
}

// 0x00294C80
extern "C" void _rwaObjHandleCloseModule(void)
{
    while (_handleList.link.next != &_handleList.link)
        RwaObjHandleRelease((RwaObjHandle *)((char *)_handleList.link.next - 8), 0);
    RwaFreeListDestroy(&D_01E7CFE0);
    D_004E2B00 = 0;
}

// 0x00294CD0
extern "C" RwBool _rwaObjHandleOpenModule(void)
{
    _handleList.link.next = &_handleList.link;
    _handleList.link.prev = &_handleList.link;
    if (!RwaFreeListCreate(sizeof(RwaObjHandle), 0x40, 0x40, 0, &D_01E7CFE0, 0x4080C))
        return 0;
    D_004E2B00 = 1;
    return 1;
}
