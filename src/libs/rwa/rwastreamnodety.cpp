// rwastreamnodety.o (librwacore.a; the E map truncates the object name) -- stream nodes: packet queues, the parameter
// shortcuts through an object's interface, and the node base object (state at obj + 0x40). Retail 0x2AD230-0x2AD8DC
// [C E map]. C port written from retail's code (audio1); proven by the checker on live captures.
//
// Node state (obj->state) [C _RwaStreamNodeCreate 0x2AD788-0x2AD818]: +0 the node interface (RwaObjInterfaceAttach),
// +0xC / +0x1C counts from the parameters (negative -> 1), +0x20 process state (1 stopped, 2 running, 3 paused [G]),
// +0x24 completions, +0x28 status (2 creating, 3 / 4 / 5 / 6 / 8 by process state), +0x2C completion callback
// {fn, arg}.
#include "rwa/rwacore.h"

// A packet queue (ring of 0x14-byte packets) [C RwaStreamPacketQueueInitialize].
struct UnkStruct_002AD2C0 {
    RwUInt8 field_0;              // 0x0 first used
    RwUInt8 field_1;              // 0x1 used
    RwUInt8 field_2;              // 0x2
    RwUInt8 field_3;              // 0x3 capacity
    RwUInt32 field_4;             // 0x4 the packets (EE address)
};
// A callback {arg0, fn, arg1} [C func_002AD460].
typedef RwUInt32 (*UnkFn_002AD460)(RwUInt32 a0, RwUInt32 a1);
struct UnkStruct_002AD460 {
    RwUInt32 field_0;
    UnkFn_002AD460 field_4;
    RwUInt32 field_8;
};
typedef void (*UnkFn_002AD6E0)(RwaObj *obj, RwUInt32 arg);
typedef RwUInt32 (*UnkFn_002AD500)(void);

extern RwaObjInterfaceDef D_01E81080;      // the stream node interface definition (static struct)
extern RwaUUID rwaSTREAMNODEINTERFACEID;
extern RwaObjInterfaceDefParam D_0049F350[3];
extern RwaObjInterfaceDefParam D_0049F390[2];
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefCreate(RwaObjInterfaceDef *def);
extern "C" RwBool RwaObjInterfaceDefDestroy(RwaObjInterfaceDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefAssignID(RwaObjInterfaceDef *def, RwaUUID *id, const char *name);
extern "C" void *RwaObjInterfaceDefSetup(RwaObjInterfaceDef *def, RwaObjInterfaceDefParam *inputs, RwUInt32 nInputs,
                                         RwaObjInterfaceDefParam *outputs, RwUInt32 nOutputs);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefFindByUUID(RwaUUID *id);
extern "C" RwaObjInterface *RwaObjInterfaceAttach(RwaObjInterfaceDef *intf, RwaObjDef *def, RwUInt32 p2, RwUInt32 p3);
extern "C" RwInt32 RwaObjInterfaceRemove(RwaObjInterface *intf);

// The word at p + off.
static RwUInt32 &func_002AD530_inl1(void *p, RwUInt32 off)
{
    return *(RwUInt32 *)((RwUInt8 *)p + off);
}

// 0x002AD230
extern "C" void RwaStreamPacketQueueFree(UnkStruct_002AD2C0 *q)
{
    RwUInt32 h = q->field_0 + 1;
    if (!(h < q->field_3))
        h -= q->field_3;
    q->field_1--;
    q->field_0 = (RwUInt8)h;
}

// The next free packet (its flag 8 cleared), or null when the queue is full. Retail then clears bit 3 of the word at
// address 0 [C 0x2AD2A8: the store goes through the null result], which C cannot express; ours leaves it.
// 0x002AD260
extern "C" RwUInt32 *RwaStreamPacketQueueAlloc(UnkStruct_002AD2C0 *q)
{
    if (q->field_1 == q->field_3)
        return 0;
    RwUInt32 i = q->field_0 + q->field_1;
    if (!(i < q->field_3))
        i -= q->field_3;
    q->field_1++;
    RwUInt32 *e = (RwUInt32 *)(q->field_4 + i * 20);
    e[0] &= ~8u;
    return e;
}

// 0x002AD2C0
extern "C" void RwaStreamPacketQueueInitialize(UnkStruct_002AD2C0 *q, RwUInt32 packets, RwUInt32 n)
{
    q->field_1 = 0;
    q->field_2 = 0;
    q->field_0 = 0;
    q->field_3 = (RwUInt8)n;
    q->field_4 = packets;
}

// Parameter shortcuts: the interface's getter / setter `index` called directly on the object; a getter's value comes
// back through a stack word (stale when the accessor does not write it).

// 0x002AD2E0
extern "C" RwUInt32 RwaObjIntGetParamUInt32(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index)
{
    RwUInt32 value;
    RwaOutputParamMap *e = &intf->outputParamMaps[index];
    e->getFunc(obj, e->paramIndex, &value);
    return value;
}

// 0x002AD320
extern "C" RwBool RwaObjIntSetParamReal(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, RwReal v)
{
    RwReal value = v;
    RwaInputParamMap *e = &intf->inputParamMaps[index];
    return e->setFunc(obj, e->paramIndex, &value);
}

// 0x002AD360
extern "C" RwBool RwaObjIntSetParamUInt32(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, RwUInt32 v)
{
    RwUInt32 value = v;
    RwaInputParamMap *e = &intf->inputParamMaps[index];
    return e->setFunc(obj, e->paramIndex, &value);
}

// 0x002AD3A0
extern "C" RwBool RwaObjIntSetParamBool(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, RwBool v)
{
    RwBool value = v;
    RwaInputParamMap *e = &intf->inputParamMaps[index];
    return e->setFunc(obj, e->paramIndex, &value);
}

// 0x002AD3E0
extern "C" RwUInt32 RwaObjIntGetParamPointer(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index)
{
    RwUInt32 value;   // a guest pointer
    RwaOutputParamMap *e = &intf->outputParamMaps[index];
    e->getFunc(obj, e->paramIndex, &value);
    return value;
}

// A getter with the caller's out pointer.
// 0x002AD420
extern "C" RwBool func_002AD420(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, void *out)
{
    RwaOutputParamMap *e = &intf->outputParamMaps[index];
    return e->getFunc(obj, e->paramIndex, out);
}

// A setter with the caller's value pointer.
// 0x002AD440
extern "C" RwBool func_002AD440(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, void *value)
{
    RwaInputParamMap *e = &intf->inputParamMaps[index];
    return e->setFunc(obj, e->paramIndex, value);
}

// Calls a callback {arg0, fn, arg1}.
// 0x002AD460
extern "C" RwUInt32 func_002AD460(UnkStruct_002AD460 *cb)
{
    return cb->field_4(cb->field_0, cb->field_8);
}

// Connects `from`'s output to `to`'s input: from's node getter 1 fills a connection word (seeded with `seed`), then
// to's node setter 1 gets `value` [C 0x2AD49C-0x2AD4D0: the same stack word].
// The stack object is two words [C 0x2AD480: frame 0x40, slot at sp+0x38, so sp+0x3C is its second word]: the getter
// writes out[1] (_rwaStreamNodeObjGetInputConnection: obj + 0x50) and the setter reads it (mac2: as a single RwUInt32
// the second word was whatever host stack followed `slot` -- it survived to the setter on x86 GCC by layout, not on
// aarch64 clang, where the callback went to a wrong address). The second word's first value is never read
// [G retail: stack; host: 0].
// 0x002AD480
extern "C" RwBool func_002AD480(RwaObj *to, RwUInt32 value, RwaObj *from, RwUInt32 seed)
{
    RwUInt32 slot[2] = {seed, 0};
    RwaObjInterface *fi = *(RwaObjInterface **)((RwUInt8 *)from + 0x40);
    fi->outputParamMaps[1].getFunc(from, fi->outputParamMaps[1].paramIndex, slot);
    slot[0] = value;
    RwaObjInterface *ti = *(RwaObjInterface **)((RwUInt8 *)to + 0x40);
    return ti->inputParamMaps[1].setFunc(to, ti->inputParamMaps[1].paramIndex, slot);
}

// Node getter: the input connection (the node's +0x50).
// 0x002AD4F0
extern "C" RwaObj *_rwaStreamNodeObjGetInputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *out)
{
    out[1] = (unsigned int)obj + 0x50;
    return obj;
}

// The no-op packet callback.
// 0x002AD610
extern "C" RwUInt32 func_002AD610(void)
{
    return 0;
}

// Node setter: the output connection {data, target}: target = {this node, the no-op callback, data}.
// 0x002AD500
extern "C" RwaObj *_rwaStreamNodeObjSetOutputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *conn)
{
    RwUInt32 *t = (RwUInt32 *)conn[1];
    t[0] = (unsigned int)obj;
    t = (RwUInt32 *)conn[1];
    *(UnkFn_002AD500 *)&t[1] = func_002AD610;
    ((RwUInt32 *)conn[1])[2] = conn[0];
    return obj;
}

// Node setter: the process state. 3 marks the status 4 (unless 5), 1 marks it 8 (unless 3); the node then moves to the
// head of its parent's execute list.
// 0x002AD530
extern "C" RwaObj *_rwaStreamNodeObjSetProcessState(RwaObj *obj, RwUInt32 param, RwUInt32 *value)
{
    RwUInt32 v = *value;
    char *st = obj->state;
    if (v == 3) {
        if (func_002AD530_inl1(st, 0x28) != 5)
            func_002AD530_inl1(st, 0x28) = 4;
    } else if (v == 1) {
        if (func_002AD530_inl1(st, 0x28) != 3)
            func_002AD530_inl1(st, 0x28) = 8;
    }
    func_002AD530_inl1(st, 0x20) = v;
    if (obj->executeLink.next) {
        obj->executeLink.prev->next = obj->executeLink.next;
        obj->executeLink.next->prev = obj->executeLink.prev;
    }
    RwaObj *parent = obj->createObj;
    obj->executeLink.next = parent->executeList.link.next;
    obj->executeLink.prev = &parent->executeList.link;
    parent->executeList.link.next->prev = &obj->executeLink;
    parent->executeList.link.next = &obj->executeLink;
    return obj;
}

// Node getter: the status.
// 0x002AD5E0
extern "C" RwaObj *func_002AD5E0(RwaObj *obj, RwUInt32 param, RwUInt32 *out)
{
    *out = func_002AD530_inl1(obj->state, 0x28);
    return obj;
}

// 0x002AD600
extern "C" RwUInt32 _rwaStreamNodeGetStatus(RwaObj *obj)
{
    return func_002AD530_inl1(obj->state, 0x28);
}

// 0x002AD620
extern "C" RwInt32 _RwaStreamNodeDestroy(RwaObj *obj)
{
    return RwaObjInterfaceRemove(*(RwaObjInterface **)((RwUInt8 *)obj + 0x40));
}

// The node's creation parameters: {-1, -1, 0, 1, 0, 0, 0}.
// 0x002AD630
extern "C" RwaObj *_RwaStreamNodeGetParams(RwaObj *obj, RwUInt32 *p)
{
    p[0] = 0xFFFFFFFF;
    p[1] = 0xFFFFFFFF;
    p[4] = 0;
    p[2] = 0;
    p[3] = 1;
    p[5] = 0;
    p[6] = 0;
    return obj;
}

// The status after processing (state at obj + 0x40): paused -> 5, running -> 6, stopped -> 3 (and off the execute
// list).
// 0x002AD660
extern "C" void _rwaStreamNodeProcessComplete(RwaObj *obj)
{
    RwUInt32 s = func_002AD530_inl1(obj, 0x60);
    if (s == 3) {
        func_002AD530_inl1(obj, 0x68) = 5;
    } else if (s == 2) {
        func_002AD530_inl1(obj, 0x68) = 6;
    } else if (s == 1) {
        if (obj->executeLink.next) {
            obj->executeLink.prev->next = obj->executeLink.next;
            obj->executeLink.next->prev = obj->executeLink.prev;
            obj->executeLink.prev = 0;
            obj->executeLink.next = 0;
        }
        func_002AD530_inl1(obj, 0x68) = 3;
    }
}

// Creation done (unless the status is 1): status from the process state (paused 4, running 6, stopped 3),
// completions + 1; then the completion callback (obj, arg) if any.
// 0x002AD6E0
extern "C" void _RwaStreamNodeCreationComplete(RwaObj *obj)
{
    char *st = obj->state;
    if (func_002AD530_inl1(st, 0x28) != 1) {
        RwUInt32 ps = func_002AD530_inl1(st, 0x20);
        if (ps == 3)
            func_002AD530_inl1(st, 0x28) = 4;
        else if (ps == 2)
            func_002AD530_inl1(st, 0x28) = 6;
        else if (ps == 1)
            func_002AD530_inl1(st, 0x28) = 3;
        func_002AD530_inl1(st, 0x24) = func_002AD530_inl1(st, 0x24) + 1;
    }
    RwUInt32 *cb = (RwUInt32 *)func_002AD530_inl1(st, 0x2C);
    if (cb)
        (*(UnkFn_002AD6E0 *)&cb[0])(obj, cb[1]);
}

// 0x002AD770
extern "C" RwaObj *_RwaStreamNodeCreate(RwaObj *obj, RwaObj *p1, RwInt32 *params)
{
    char *st = obj->state;
    func_002AD530_inl1(st, 0x1C) = params[1] >= 0 ? params[1] : 1;
    func_002AD530_inl1(st, 0xC) = params[0] >= 0 ? params[0] : 1;
    func_002AD530_inl1(st, 0x10) = 0;
    func_002AD530_inl1(st, 0x14) = 0;
    func_002AD530_inl1(st, 0x18) = 0;
    func_002AD530_inl1(st, 0x24) = 0;
    func_002AD530_inl1(st, 0x2C) = params[4];
    func_002AD530_inl1(st, 0x20) = 1;
    func_002AD530_inl1(st, 0x28) = 2;
    RwaObjInterface *i = RwaObjInterfaceAttach(RwaObjInterfaceDefFindByUUID(&rwaSTREAMNODEINTERFACEID), obj->definition,
                                               params[5], params[6]);
    func_002AD530_inl1(st, 0) = (unsigned int)i;
    func_002AD530_inl1(st, 4) = 0;
    func_002AD530_inl1(st, 8) = 0;
    return obj;
}

// 0x002AD840
extern "C" void func_002AD840(void *p)
{
    func_00293440(p);
}

// 0x002AD850
extern "C" void *func_002AD850(RwUInt32 size)
{
    return func_002934C0(size, 0x30810);
}

// 0x002AD860
extern "C" RwaObjInterfaceDef *func_002AD860(void)
{
    return (RwaObjInterfaceDef *)RwaObjInterfaceDefDestroy(&D_01E81080);
}

// The stream node interface definition.
// 0x002AD870
extern "C" RwaObjInterfaceDef *func_002AD870(void)
{
    if (!RwaObjInterfaceDefCreate(&D_01E81080))
        return 0;
    RwaObjInterfaceDefAssignID(&D_01E81080, &rwaSTREAMNODEINTERFACEID, 0);
    RwaObjInterfaceDefSetup(&D_01E81080, D_0049F350, 3, D_0049F390, 2);
    return &D_01E81080;
}
