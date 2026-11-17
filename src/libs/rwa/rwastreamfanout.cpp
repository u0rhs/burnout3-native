// rwastreamfanout.o (librwacore.a; truncated in the E map) -- the fan-out stream node: one input, n outputs; every
// upstream packet is handed to each output (a pending bit per output) and released upstream once all have completed
// it. Retail 0x2A6760-0x2A72C4 [C E map]. C port written from retail's code (audio1); proven by the checker on live
// captures.
//
// The node (state at obj + 0x40; offsets below from obj) [C _RwaStreamFanOutCreate 0x2A6D34-0x2A6F14]: +0x50 the
// input connection, +0x60 process state, +0x68 status, +0x70 the node interface, +0x74 the memory interface
// {alloc(size, arg), free(p, arg), arg}, +0x78 a copy of the stream format (0x2C bytes: +0x14 outputs n, +0x18 the
// per-output table, 0x28 each: +0x20 / +0x24 [G: output id / offset]), +0xA4 packets P, +0xA8 input exhausted, +0xAC
// the block: P pending masks (+0xAC), P upstream packets (+0xB0), n read and n completion positions (+0xB4 / +0xB8),
// n * P packets of 0x14 (+0xBC), n completion callbacks {fn, cookie} (+0xC0), n cookies {state, output} (+0xC4).
#include "rwa/rwacore.h"

typedef RwUInt32 (*UnkFn_002A6CE0_alloc)(RwUInt32 size, RwUInt32 arg);
typedef void (*UnkFn_002A6CE0_free)(RwUInt32 p, RwUInt32 arg);
struct UnkStruct_002A6CE0_mem {
    UnkFn_002A6CE0_alloc field_0;
    UnkFn_002A6CE0_free field_4;
    RwUInt32 field_8;
};
typedef void (*UnkFn_002A6870)(RwUInt32 packet, RwUInt32 arg);
struct UnkStruct_002A6870_cb {
    UnkFn_002A6870 field_0;
    RwUInt32 field_4;
};
typedef RwUInt32 (*UnkFn_002A6960)(RwaObj *obj, RwUInt32 output);
typedef void (*UnkFn_002A6E9C)(RwUInt32 packet, RwUInt32 *cookie);

extern RwaObjDef D_01E7DE90;               // the fan-out object definition (static struct)
extern RwaObjDefInterfaceInfo D_01E7DE70;  // its interface info [G type]
extern RwLinkList _streamfanoutAliasInfo;
extern RwaObjDef *_rwaGnullObjDef;
extern RwaUUID rwaSTREAMFANOUTID;
extern RwaUUID rwaSTREAMNODEINTERFACEID;
extern RwaInputParamDef _streamfanoutInputParams[3];
extern RwaOutputParamDef _streamfanoutOutputParams[2];
extern RwUInt8 _rwaGstreamNodeMemory[0xC];
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" RwaObj *_rwaStreamNodeObjGetInputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *out);
extern "C" RwUInt32 _rwaStreamNodeGetStatus(RwaObj *obj);
extern "C" RwaObj *_rwaStreamNodeObjSetOutputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *conn);
extern "C" RwaObj *_rwaStreamNodeObjSetProcessState(RwaObj *obj, RwUInt32 param, RwUInt32 *value);
extern "C" RwaObj *_RwaStreamNodeGetParams(RwaObj *obj, RwUInt32 *p);
extern "C" RwaObj *_RwaStreamNodeCreate(RwaObj *obj, RwaObj *p1, void *params);
extern "C" RwInt32 _RwaStreamNodeDestroy(RwaObj *obj);   // [C] reads a0 only
extern "C" void _RwaStreamNodeCreationComplete(RwaObj *obj);
extern "C" RwUInt32 func_002AD460(void *cb);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefFindByUUID(RwaUUID *id);
extern "C" RwaObjInterface *RwaObjInterfaceAttach(RwaObjInterfaceDef *intf, RwaObjDef *def, RwUInt32 p2, RwUInt32 p3);
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 p1, RwaObjDef *def, void *intfInfo, void *aliasInfo,
                                      RwUInt32 hint);
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, RwaUUID *id, const char *name);
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaInputParamDef *inputs, RwUInt32 nInputs,
                                       RwaOutputParamDef *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefDestroy(RwaObjDef *def);

// The word at p + off.
static RwUInt32 &func_002A6960_inl1(void *p, RwUInt32 off)
{
    return *(RwUInt32 *)((RwUInt8 *)p + off);
}
// Element i of the word array whose address is at p + off.
static RwUInt32 &func_002A6960_inl2(void *p, RwUInt32 off, RwUInt32 i)
{
    return ((RwUInt32 *)func_002A6960_inl1(p, off))[i];
}

// 0x002A6760
extern "C" RwaObj *_RwaStreamFanOutGetInputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *out)
{
    _rwaStreamNodeObjGetInputConnection(obj, 0, out);
    return obj;
}

// 0x002A6790
extern "C" RwaObj *_RwaStreamFanOutGetStatus(RwaObj *obj, RwUInt32 param, RwUInt32 *out)
{
    *out = _rwaStreamNodeGetStatus(obj);
    return obj;
}

extern "C" RwUInt32 _rwaStreamFanOutGetStreamPacket(RwaObj *obj, RwUInt32 output);

// The output connection; its puller becomes this fan-out's packet getter.
// 0x002A67D0
extern "C" RwaObj *_RwaStreamFanOutSetOutputConnection(RwaObj *obj, RwUInt32 param, RwUInt32 *conn)
{
    _rwaStreamNodeObjSetOutputConnection(obj, 0, conn);
    RwUInt32 *t = (RwUInt32 *)conn[1];
    t[2] = conn[0];
    *(UnkFn_002A6960 *)&((RwUInt32 *)conn[1])[1] = _rwaStreamFanOutGetStreamPacket;
    return obj;
}

// 0x002A6830
extern "C" RwaObj *_RwaStreamFanOutSetProcessState(RwaObj *obj, RwUInt32 param, RwUInt32 *value)
{
    RwUInt32 v = *value;
    _rwaStreamNodeObjSetProcessState(obj, 0, &v);
    return obj;
}

// An output finished with its current packet: its pending bit cleared; when no output is pending the upstream packet
// is completed (its callback {fn, arg} at +0x10) and the slot freed; the output's completion position advances.
// 0x002A6870
extern "C" void _RwaStreamFanOutPacketCompletionCallback(RwUInt32 packet, RwUInt32 *cookie)
{
    RwUInt8 *s = (RwUInt8 *)cookie[0];
    RwUInt32 o = cookie[1];
    RwUInt32 *r = &func_002A6960_inl2(s, 0x78, o);
    func_002A6960_inl2(s, 0x6C, *r) &= ~(1u << o);
    if (!(((1u << func_002A6960_inl1(s, 0x4C)) - 1) & func_002A6960_inl2(s, 0x6C, *r))) {
        RwUInt32 up = func_002A6960_inl2(s, 0x70, *r);
        UnkStruct_002A6870_cb *cb = (UnkStruct_002A6870_cb *)func_002A6960_inl1((void *)up, 0x10);
        cb->field_0(up, cb->field_4);
        func_002A6960_inl2(s, 0x70, *r) = 0;
    }
    *r = *r + 1;
    if (!(*r < func_002A6960_inl1(s, 0x64)))
        *r = 0;
}

// An output's next packet: the slot at its read position, pulling a new upstream packet into a free slot (all outputs
// pending); null when stopped (status 3), exhausted (+0xA8 = 1) or the output already took this slot's packet. The
// packet copies the upstream one's words 0 and 0xC, the output's id (+8) and data offset + the upstream +4.
// 0x002A6960
extern "C" RwUInt32 _rwaStreamFanOutGetStreamPacket(RwaObj *obj, RwUInt32 output)
{
    if (func_002A6960_inl1(obj, 0x68) == 3)
        return 0;
    RwUInt8 *s = (RwUInt8 *)obj + 0x40;
    RwUInt32 *slot = &func_002A6960_inl2(s, 0x74, output);
    if (!func_002A6960_inl2(s, 0x70, *slot)) {
        func_002A6960_inl2(s, 0x70, *slot) = func_002AD460((RwUInt8 *)obj + 0x50);
        if (!func_002A6960_inl2(s, 0x70, *slot)) {
            func_002A6960_inl1(s, 0x68) = 1;
            return 0;
        }
        func_002A6960_inl2(s, 0x6C, *slot) = 0xFFFFFFFF;
        func_002A6960_inl1(s, 0x68) = 0;
    }
    RwUInt32 idx = *slot;
    if (!((1u << output) & func_002A6960_inl2(s, 0x6C, idx)))
        return 0;
    RwUInt32 n = idx + output * func_002A6960_inl1(s, 0x64);
    RwUInt32 *pk = (RwUInt32 *)(func_002A6960_inl1(s, 0x7C) + n * 20);
    RwUInt32 *up = (RwUInt32 *)func_002A6960_inl2(s, 0x70, idx);
    pk[0] = up[0];
    pk[3] = ((RwUInt32 *)func_002A6960_inl2(s, 0x70, *slot))[3];
    RwUInt8 *tab = (RwUInt8 *)func_002A6960_inl1(s, 0x50) + output * 40;
    pk[2] = func_002A6960_inl1(tab, 0x20);
    tab = (RwUInt8 *)func_002A6960_inl1(s, 0x50) + output * 40;
    pk[1] = func_002A6960_inl1(tab, 0x24) + ((RwUInt32 *)func_002A6960_inl2(s, 0x70, *slot))[1];
    *slot = *slot + 1;
    if (!(*slot < func_002A6960_inl1(s, 0x64)))
        *slot = 0;
    return (unsigned int)pk;
}

// Creation parameters: the node's, with counts 1 / 1 and the default node memory interface.
// 0x002A6B20
extern "C" RwaObj *_RwaStreamFanOutGetParams(RwaObj *obj, RwUInt32 *p)
{
    _RwaStreamNodeGetParams(obj, p);
    p[0] = 1;
    p[1] = 1;
    p[7] = (unsigned int)_rwaGstreamNodeMemory;
    return obj;
}

// Finish execute: nothing.
// 0x002A6B70
extern "C" RwaObj *func_002A6B70(RwaObj *obj)
{
    return obj;
}

// Start execute: the status machine with the process state (1 stop, 2 run, 3 pause [G]) for nodes with packets.
// 0x002A6B80
extern "C" RwaObj *func_002A6B80(RwaObj *obj)
{
    if ((RwInt32)func_002A6960_inl1(obj, 0xA4) <= 0)
        return obj;
    RwUInt32 st = func_002A6960_inl1(obj, 0x68);
    RwUInt32 ps = func_002A6960_inl1(obj, 0x60);
    switch (st) {
    case 3:
        if (ps - 2 < 2)
            func_002A6960_inl1(obj, 0x68) = 4;
        break;
    case 4:
    case 5:
    case 6:
        if (func_002A6960_inl1(obj, 0xA8))
            func_002A6960_inl1(obj, 0x68) = 7;
        if (ps == 3)
            func_002A6960_inl1(obj, 0x68) = 4;
        else if (ps == 1)
            func_002A6960_inl1(obj, 0x68) = 3;
        break;
    case 7:
        if (!func_002A6960_inl1(obj, 0xA8) && ps == 2)
            func_002A6960_inl1(obj, 0x68) = 6;
        if (ps == 3)
            func_002A6960_inl1(obj, 0x68) = 4;
        if (ps == 1)
            func_002A6960_inl1(obj, 0x68) = 3;
        break;
    case 8:
        func_002A6960_inl1(obj, 0x68) = 3;
        break;
    default:
        break;
    }
    return obj;
}

// 0x002A6C80
extern "C" RwInt32 _RwaStreamFanOutDestroy(RwaObj *obj, RwUInt32 p1, RwUInt32 p2)
{
    UnkStruct_002A6CE0_mem *m = (UnkStruct_002A6CE0_mem *)func_002A6960_inl1(obj, 0x74);
    m->field_4(func_002A6960_inl1(obj, 0xAC), m->field_8);
    return _RwaStreamNodeDestroy(obj);   // [C] retail also passes a1 / a2 through; the callee ignores them
}

// 0x002A6CE0
extern "C" RwaObj *_RwaStreamFanOutCreate(RwaObj *obj, RwaObj *p1, RwUInt32 *params)
{
    if (!_RwaStreamNodeCreate(obj, p1, params))
        return 0;
    RwUInt8 *s = (RwUInt8 *)obj + 0x40;
    func_002A6960_inl1(obj, 0x70) =
        (unsigned int)RwaObjInterfaceAttach(RwaObjInterfaceDefFindByUUID(&rwaSTREAMNODEINTERFACEID), obj->definition, 0, 0);
    RwUInt32 n = func_002A6960_inl1((void *)params[2], 0x14);
    func_002A6960_inl1(s, 0x34) = params[7];
    memcpy(s + 0x38, (void *)params[2], 0x2C);
    func_002A6960_inl1(s, 0x64) = params[3];
    UnkStruct_002A6CE0_mem *m = (UnkStruct_002A6CE0_mem *)func_002A6960_inl1(s, 0x34);
    RwUInt32 P = func_002A6960_inl1(s, 0x64);
    func_002A6960_inl1(s, 0x6C) = m->field_0(P * 4 + P * 20 + n * (P * 20 + 0x18), m->field_8);
    RwUInt32 b = func_002A6960_inl1(s, 0x6C);
    if (!b)
        return 0;
    func_002A6960_inl1(s, 0x70) = b + func_002A6960_inl1(s, 0x64) * 4;
    func_002A6960_inl1(s, 0x74) = func_002A6960_inl1(s, 0x70) + func_002A6960_inl1(s, 0x64) * 4;
    func_002A6960_inl1(s, 0x78) = func_002A6960_inl1(s, 0x74) + n * 4;
    func_002A6960_inl1(s, 0x7C) = func_002A6960_inl1(s, 0x78) + n * 4;
    func_002A6960_inl1(s, 0x80) = func_002A6960_inl1(s, 0x7C) + func_002A6960_inl1(s, 0x64) * n * 20;
    func_002A6960_inl1(s, 0x84) = func_002A6960_inl1(s, 0x80) + n * 8;
    for (RwUInt32 i = 0; i < func_002A6960_inl1(s, 0x64); i++) {
        func_002A6960_inl2(s, 0x70, i) = 0;
        func_002A6960_inl2(s, 0x6C, i) = 0;
    }
    for (RwUInt32 o = 0; o < func_002A6960_inl1((void *)params[2], 0x14); o++) {
        func_002A6960_inl2(s, 0x74, o) = 0;
        func_002A6960_inl2(s, 0x78, o) = 0;
        RwUInt32 *ck = (RwUInt32 *)(func_002A6960_inl1(s, 0x84) + o * 8);
        ck[0] = (unsigned int)s;
        ck = (RwUInt32 *)(func_002A6960_inl1(s, 0x84) + o * 8);
        ck[1] = o;
        RwUInt32 *cb = (RwUInt32 *)(func_002A6960_inl1(s, 0x80) + o * 8);
        *(UnkFn_002A6E9C *)&cb[0] = _RwaStreamFanOutPacketCompletionCallback;
        cb = (RwUInt32 *)(func_002A6960_inl1(s, 0x80) + o * 8);
        cb[1] = func_002A6960_inl1(s, 0x84) + o * 8;
        for (RwUInt32 k = 0; k < func_002A6960_inl1(s, 0x64); k++) {
            RwUInt32 *pk = (RwUInt32 *)(func_002A6960_inl1(s, 0x7C) + (k + o * func_002A6960_inl1(s, 0x64)) * 20);
            pk[4] = func_002A6960_inl1(s, 0x80) + o * 8;
        }
    }
    func_002A6960_inl1(s, 0x68) = 0;
    _RwaStreamNodeCreationComplete(obj);
    return obj;
}

// 0x002A6F40
extern "C" RwaObjDef *func_002A6F40(void)
{
    return RwaObjDefDestroy(&D_01E7DE90);
}

// 0x002A6F50
extern "C" void _RwaStreamFanOutRegisterCommon(RwaObjDef *def)
{
    RwaObjDefAssignID(def, &rwaSTREAMFANOUTID, 0);
    func_00297C30_inl3(def, 0xC0, 0x40);
    def->createFunc = _RwaStreamFanOutCreate;
    def->destroyFunc = _RwaStreamFanOutDestroy;
    def->startExecuteFunc = func_002A6B80;
    def->finishExecuteFunc = func_002A6B70;
    def->getParamsFunc = _RwaStreamFanOutGetParams;
    def->createParamSize = 0x20;
}

// 0x002A7230
extern "C" RwaObjDef *RwaStreamFanOutRegister(void)
{
    RwaObjDef *def = RwaObjDefCreate(_rwaGnullObjDef, 0xC, &D_01E7DE90, &D_01E7DE70, &_streamfanoutAliasInfo, 0x3080F);
    if (!def)
        return 0;
    _RwaStreamFanOutRegisterCommon(def);
    if (RwaObjDefSetupParams(def, _streamfanoutInputParams, 3, _streamfanoutOutputParams, 2))
        return def;
    RwaObjDefDestroy(def);
    return 0;
}
