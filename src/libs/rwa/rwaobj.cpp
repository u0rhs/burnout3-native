// rwaobj.o (librwacore.a) -- RWA objects: the per-tick execute tree, enumeration, destruction.
// Retail 0x292690-0x29332C [C E map]. Not ported yet: RwaObjDisconnectParamIndex 0x2926A0, _rwaObjCalcSize 0x292D80
// (not called in races14; RwaObjCreate has it inlined, func_00292E40_inl1).
#include "rwa/rwacore.h"

extern RwLinkList _instanceList;     // every object (RwaObj::link)
extern RwaObj *_rwaObj;              // the top-level object (null definition _rwaGnullObjDef)
extern RwaObjDef *_rwaGnullObjDef;
extern RwUInt32 D_004E2AD8;          // _moduleOpen of rwaobj.o [L name, research/retail_data_names.csv]
extern RwUInt32 D_004E2ADC;          // _moduleClosing [L name, retail_data_names.csv]: 1 while the module closes [C 0x29325C]
extern RwaFreeList D_01E7CF20;       // the objects' free list (static struct)
extern char D_01E7CEF0[];            // the top-level object's creation parameters [C func_00293280]
extern "C" void RwaObjDisconnectParamIndex(RwaObj *obj, RwUInt32 index, RwUInt32 kind);
extern "C" void _rwaObjDefFreeObj(RwaObj *obj, RwUInt32 p1, RwUInt32 p2);
extern "C" RwaObj *RwaObjCreate(RwaObjDef *def, RwaObj *parent, RwUInt32 flags, void *params, UnkFn_00292E40 allocFn,
                                RwUInt32 allocArg);
extern "C" RwaObjDef *_rwaObjDefCreateSupported(RwaObjDef *def, RwaObjDef *child);
extern "C" RwaObjDef *_rwaObjDefCreateInstanceAllocator(RwaObjDef *def, RwUInt32 size, RwBool useFreeList);
extern "C" void *_rwaObjDefAllocObj(RwaObjDef *def, RwUInt32 size);
extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b);
extern RwaUUID D_0049D450;           // @88 of rwaobj.o [C retail_data_names.csv]: the UUID of the definition whose
                                     // objects get aligned memory
extern "C" RwaFreeList *RwaFreeListCreate(RwUInt32 entrySize, RwUInt32 entriesPerBlock, RwUInt32 alignment,
                                          RwUInt32 preallocBlocks, RwaFreeList *fl, RwUInt32 hint);
extern "C" void RwaFreeListDestroy(RwaFreeList *fl);

// One step of the execute tree: unless `childrenOnly`, the object's own callback (field `which` of its definition),
// then each child's (the objects on its executeList, via their executeLink at +0x14). The object, or null when a
// callback returned null. A missing callback leaves the result as the previous call left it: retail tests v0 after the
// skipped jalr [C 0x292858]; ours starts from "succeeded" [L: the first call's v0 at entry is the caller's object
// (RwaCoreUpdateFinish 0x290D00 passes v0 as a0), non-null].
static RwaObj *func_00292820_inl1(RwaObj *obj, RwBool childrenOnly, RwBool finish)
{
    RwaObj *result = obj;
    RwaObj *last = obj;
    if (!childrenOnly) {
        RwaObjExecuteFunc f = finish ? obj->definition->finishExecuteFunc : obj->definition->startExecuteFunc;
        if (f)
            last = f(obj);
        if (!last)
            result = 0;
    }
    RwLLLink *n = obj->executeList.link.next;
    while (n != &obj->executeList.link) {
        RwaObj *child = (RwaObj *)((char *)n - 0x14);
        n = n->next;
        RwaObjExecuteFunc f = finish ? child->definition->finishExecuteFunc : child->definition->startExecuteFunc;
        if (f)
            last = f(child);
        if (!last)
            result = 0;
    }
    return result;
}

// 0x00292820
extern "C" RwaObj *_rwaObjFinishExecute(RwaObj *obj, RwBool childrenOnly)
{
    return func_00292820_inl1(obj, childrenOnly, 1);
}

// 0x002928D0
extern "C" RwaObj *_rwaObjStartExecute(RwaObj *obj, RwBool childrenOnly)
{
    return func_00292820_inl1(obj, childrenOnly, 0);
}

typedef RwBool (*UnkFn_00292980)(RwaObj *obj, void *arg);

// Calls `cb(obj, arg)` for every object until one returns 0. No result [C: the one caller, 0x291538, ignores v0].
// 0x00292980
extern "C" void RwaObjEnum(UnkFn_00292980 cb, void *arg)
{
    for (RwLLLink *n = _instanceList.link.next; n != &_instanceList.link; n = n->next)
        if (!cb((RwaObj *)((char *)n - 4), arg))
            return;
}

// An RwaObjEnum callback: when obj's definition is the one in search[1], store obj in search[0] and stop.
// 0x002929F0
extern "C" RwBool func_002929F0(RwaObj *obj, RwaObj **search)
{
    if (obj->definition != (RwaObjDef *)search[1])
        return 1;
    search[0] = obj;
    return 0;
}

// Destroys an object: its definition's destroy callback (a1 / a2 reach it unchanged, as retail leaves them), out of
// its parent's execute list, its children detached (their own links are left as they were), its parameter connections
// dropped when flag 1 is set, out of _instanceList, then freed by its definition with (p1, p2).
// 0x00292A20
extern "C" void RwaObjDestroy(RwaObj *obj, RwUInt32 p1, RwUInt32 p2)
{
    if (obj->definition->destroyFunc)
        obj->definition->destroyFunc(obj);
    if (obj->executeLink.next) {
        obj->executeLink.prev->next = obj->executeLink.next;
        obj->executeLink.next->prev = obj->executeLink.prev;
        obj->executeLink.prev = 0;
        obj->executeLink.next = 0;
    }
    RwLLLink *n = obj->executeList.link.next;
    while (n != &obj->executeList.link) {
        RwLLLink *next = n->next;
        n->prev->next = next;
        next->prev = n->prev;
        n = next;
    }
    if (obj->flags & 1) {
        RwUInt32 count = obj->definition->interfaceInfo->noInputs;
        for (RwUInt32 i = 0; i < count; i++)
            RwaObjDisconnectParamIndex(obj, i, 1);
        count = obj->definition->interfaceInfo->noOutputs;
        for (RwUInt32 i = 0; i < count; i++)
            RwaObjDisconnectParamIndex(obj, i, 2);
    }
    obj->link.prev->next = obj->link.next;
    obj->link.next->prev = obj->link.prev;
    _rwaObjDefFreeObj(obj, p1, p2);
}

// 0x00293240
extern "C" void _rwaObjCloseModule(void)
{
    D_004E2ADC = 1;
    RwaObjDestroy(_rwaObj, 0, 0);
    _rwaObj = 0;
    RwaFreeListDestroy(&D_01E7CF20);
    D_004E2AD8 = 0;
}

// The top-level object's memory (the allocator _rwaObjOpenModule gives RwaObjCreate).
// 0x00293280
extern "C" void *func_00293280(void)
{
    return D_01E7CEF0;
}

// 0x00293290
extern "C" RwBool _rwaObjOpenModule(void)
{
    _instanceList.link.next = &_instanceList.link;
    _instanceList.link.prev = &_instanceList.link;
    D_004E2ADC = 0;
    if (!RwaFreeListCreate(0x1C, 0x10, 0x40, 0, &D_01E7CF20, 0x4080C)) {
        D_004E2AD8 = 0;
        return 0;
    }
    D_004E2AD8 = 1;
    _rwaObj = RwaObjCreate(_rwaGnullObjDef, 0, 0, 0, (UnkFn_00292E40)func_00293280, 0);
    if (_rwaObj)
        return 1;
    RwaFreeListDestroy(&D_01E7CF20);
    D_004E2AD8 = 0;
    return 0;
}

// 0x00292690
extern "C" RwaObj *RwaObjGetTopLevelObject(void)
{
    return _rwaObj;
}

// The object's header: definition, flags, state = obj + stateOffset (computed as a 32-bit sum). Flag 1: the
// parameter connection lists after the state (the state size, + 4 with flag 2; inside the object at +0x28 / +0x2C when
// the state is null): a header {12-byte entries, 8-byte entries}, one 12-byte self-linked entry per input and one
// 8-byte self-linked link per output of the definition. Flag 2: a zero word after the state (or at +0x28). Then the
// execute list empty and the execute link cleared [C 0x292B70-0x292CF0].
// 0x00292B70
extern "C" void _RwaObjInit(RwaObj *obj, RwaObjDef *def, RwUInt32 flags, RwUInt32 stateOffset)
{
    obj->definition = def;
    obj->flags = flags;
    obj->state = (char *)((unsigned int)obj + stateOffset);
    RwUInt32 fl = obj->flags;
    if (fl & 1) {
        RwUInt32 *hdr;
        if (!obj->state) {
            hdr = (RwUInt32 *)((char *)obj + ((fl & 2) ? 0x2C : 0x28));
        } else {
            char *p = obj->state + (obj->definition->stateSizeAlign & 0x0FFFFFFF);
            if (fl & 2)
                p += 4;
            hdr = (RwUInt32 *)p;
        }
        hdr[0] = (unsigned int)(hdr + 2);
        hdr[1] = hdr[0] + 12 * def->interfaceInfo->noInputs;
        RwUInt32 base = hdr[0];
        RwUInt16 n = def->interfaceInfo->noInputs;
        for (RwUInt16 i = 0; (RwInt32)i < (RwInt32)n; i++) {
            RwUInt32 *e = (RwUInt32 *)(base + 12 * i);
            e[0] = (unsigned int)e;
            e[1] = (unsigned int)e;
            e[2] = e[0];
        }
        base = hdr[1];
        n = def->interfaceInfo->noOutputs;
        for (RwUInt16 i = 0; (RwInt32)i < (RwInt32)n; i++) {
            RwUInt32 *e = (RwUInt32 *)(base + 8 * i);
            e[0] = (unsigned int)e;
            e[1] = (unsigned int)e;
        }
    }
    if (obj->flags & 2) {
        RwUInt32 *end = (RwUInt32 *)((char *)obj + 0x28);
        if (obj->state)
            end = (RwUInt32 *)(obj->state + (obj->definition->stateSizeAlign & 0x0FFFFFFF));
        *end = 0;
    }
    obj->executeList.link.next = &obj->executeList.link;
    obj->executeList.link.prev = &obj->executeList.link;
    obj->executeLink.prev = 0;
    obj->executeLink.next = 0;
}

// The first object of `def` with flag 4: retail returns that object's flags word, not the object [C 0x292D54-0x292D64:
// the flags load overwrites the object register before the store]; 0 when there is none.
// 0x00292D00
extern "C" RwUInt32 _rwaObjFindFixed(RwaObjDef *def)
{
    RwUInt32 result = 0;
    for (RwLLLink *n = _instanceList.link.next; n != &_instanceList.link; n = n->next) {
        RwaObj *o = (RwaObj *)((char *)n - 4);
        if (o->definition == def && (o->flags & 4)) {
            result = o->flags;
            break;
        }
    }
    return result;
}

// An object's size for its definition and flags (1: parameter connection lists, 2: an extra word), and in *stateOffset
// where its state starts (0 without a state): the header (0x28) rounded up to the state alignment
// [C _rwaObjCalcSize 0x292D80, inlined at 0x292ED8].
static RwUInt32 func_00292E40_inl1(RwaObjDef *def, RwUInt32 flags, RwUInt32 *stateOffset)
{
    RwUInt32 size = 0x28;
    if (def->stateSizeAlign & 0x0FFFFFFF) {
        RwUInt32 mask = (1u << ((def->stateSizeAlign & 0xF0000000) >> 28)) - 1;
        *stateOffset = (mask + 0x28) & ~mask;
        size = *stateOffset + (def->stateSizeAlign & 0x0FFFFFFF);
    } else {
        *stateOffset = 0;
    }
    if (flags & 2)
        size += 4;
    if (flags & 1)
        size += 8 + 12 * def->interfaceInfo->noInputs + 8 * def->interfaceInfo->noOutputs;
    return size;
}

// A new object of `def` below `parent` (null: the top-level object; the top-level object itself is made first,
// without a parent). Its creation parameters are `params` or, when null, the definition's defaults (in a stack buffer
// up to 0x200 bytes, else RWA memory). Its memory comes from `allocFn(def, allocArg, params, size)` when given and it
// succeeds (object flag 0x20), else from the definition's instance allocator (made on first use). Objects of the
// definition with UUID @88 are realigned to the state alignment (the allocation's address kept before the object, flag
// 8, or after it, flag 0x10). Then initialised, linked into the parent's execute list, its state made by the
// definition's create callback (on a failure it is unlinked and freed: null) and linked first into _instanceList.
// 0x00292E40
extern "C" RwaObj *RwaObjCreate(RwaObjDef *def, RwaObj *parent, RwUInt32 flags, void *params, UnkFn_00292E40 allocFn,
                                RwUInt32 allocArg)
{
    B3_RETAIL_FRAME(0x2D0);
    char *defaults = B3_FRAME_LOCAL(char, 0xC0);   // a callee may keep or reach past a bounce window: retail's buffer
    void *tempParams = 0;
    RwaObj *obj = 0;
    flags &= 7;
    if (_rwaObj) {
        if (!parent)
            parent = _rwaObj;
        if (!_rwaObjDefCreateSupported(parent->definition, def))
            return 0;
    }
    if (!params) {
        if ((RwInt32)def->createParamSize < 0x201) {
            params = defaults;
        } else {
            tempParams = func_002934C0(def->createParamSize, 0x1080C);
            if (!tempParams)
                return 0;
            params = tempParams;
        }
        if (def->getParamsFunc)
            def->getParamsFunc(0, (RwUInt32 *)params);
        else
            params = 0;
    }
    RwUInt32 stateOffset;
    RwUInt32 size = func_00292E40_inl1(def, flags, &stateOffset);
    RwUInt32 align = 1u << ((def->stateSizeAlign & 0xF0000000) >> 28);
    if (align > 0x40)
        size += align;
    if (allocFn) {
        obj = (RwaObj *)allocFn(def, allocArg, params, size);
        if (obj)
            flags |= 0x20;
    }
    if (!obj) {
        if (def->instanceAllocator || _rwaObjDefCreateInstanceAllocator(def, size, (flags & 4) != 0))
            obj = (RwaObj *)_rwaObjDefAllocObj(def, size);
    }
    RwaUUID aligned = D_0049D450;
    if (RwaUUIDCompare(&aligned, def->uniqueID.uuid) == 0 && ((unsigned int)obj & (align - 1))) {
        RwUInt32 a = ((unsigned int)obj + (align - 1)) & ~(align - 1);
        if (a - (unsigned int)obj >= 4) {
            *(RwUInt32 *)(a - 4) = (unsigned int)obj;
            flags |= 8;
        } else {
            *(RwUInt32 *)(a + size) = (unsigned int)obj;
            flags |= 0x10;
        }
        obj = (RwaObj *)a;
    }
    if (!obj) {
        if (tempParams)
            func_00293440(tempParams);
        return 0;
    }
    obj->createObj = parent;
    _RwaObjInit(obj, def, flags, stateOffset);
    if (_rwaObj) {
        obj->executeLink.next = parent->executeList.link.next;
        obj->executeLink.prev = &parent->executeList.link;
        parent->executeList.link.next->prev = &obj->executeLink;
        parent->executeList.link.next = &obj->executeLink;
    }
    if (obj->definition->createFunc && !obj->definition->createFunc(obj, parent, params, allocFn, allocArg)) {
        if (tempParams)
            func_00293440(tempParams);
        if (_rwaObj) {
            obj->executeLink.prev->next = obj->executeLink.next;
            obj->executeLink.next->prev = obj->executeLink.prev;
        }
        _rwaObjDefFreeObj(obj, 0, 0);
        return 0;
    }
    if (tempParams)
        func_00293440(tempParams);
    obj->link.next = _instanceList.link.next;
    obj->link.prev = &_instanceList.link;
    _instanceList.link.next->prev = &obj->link;
    _instanceList.link.next = &obj->link;
    return obj;
}
