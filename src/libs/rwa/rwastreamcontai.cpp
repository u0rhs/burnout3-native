// rwastreamcontai.o (librwacore.a; the E map truncates the object name) -- the stream container: commands, per-update
// state machine, status, render-chain walks, the open state machine and chain instancing. Retail
// 0x2A3730-0x2A6750 [C E map]. C port written from retail's code (audio1); proven by the checker on live captures.
#include "rwa/rwacore.h"

// A container command block (the stream's +0x174) [C the commands 0x2A65B0-0x2A6740]: flags at +0 (4 media, 8 stream
// position, 0x10 gain, 0x20 pitch, 0x40 loop segment, 0x80 play state pending), then the values, one gain per stream
// from +0x18.
struct UnkStruct_002A65F0 {
    RwUInt32 field_0;             // 0x00 pending flags
    RwUInt32 field_4;             // 0x04 play state (1 after media / initialisation)
    RwUInt32 field_8;             // 0x08 media
    RwUInt32 field_C;             // 0x0C stream position request
    RwReal field_10;              // 0x10 pitch (1)
    RwUInt32 field_14;            // 0x14 loop segment
    RwReal field_18[1];           // 0x18 gains: UnkStruct_002A65F0_info::field_4 of them
};
struct UnkStruct_002A65F0_info {
    RwUInt32 field_0;             // bit 0: busy (_rwaStreamContainerCommandSetPitch refuses)
    RwInt32 field_4;              // the number of streams
};
// The container state (the stream's +0x104): +8 state (5 = playing [G]), +0x20 / +0x24 the sub-stream list (first /
// last link; a link is a node's +0x44, its +0x2C the node interface).
typedef RwaObj UnkStruct_002A6320_node;

// One stream's gain, or every stream's for index -1 (count re-read per stream; no bounds check on an index).
// 0x002A65F0
extern "C" void _rwaStreamContainerCommandSetStreamGain(UnkStruct_002A65F0 *cmd, UnkStruct_002A65F0_info *info,
                                                        RwInt32 index, RwReal gain)
{
    if (index != -1) {
        cmd->field_18[index] = gain;
    } else {
        for (RwInt32 i = 0; i < info->field_4; i++)
            cmd->field_18[i] = gain;
    }
    cmd->field_0 |= 0x10;
}

// 1 when set; 0 (nothing written) while busy.
// 0x002A6650
extern "C" RwBool _rwaStreamContainerCommandSetPitch(UnkStruct_002A65F0 *cmd, UnkStruct_002A65F0_info *info,
                                                     RwReal pitch)
{
    if (info->field_0 & 1)
        return 0;
    cmd->field_0 |= 0x20;
    cmd->field_10 = pitch;
    return 1;
}

// The node of a container link.
static RwaObj *func_002A6320_inl1(RwUInt32 link)
{
    return (RwaObj *)(link - 0x44);
}

extern "C" RwBool func_002AD420(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, void *out);
extern "C" RwUInt32 RwaObjIntGetParamPointer(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index);

// The voice of sub-stream req[1] (node getter 2 into req), walking from the nearer end of the list.
// 0x002A6320
extern "C" RwBool _rwaStreamContainerGetSubStreamVoice(RwUInt8 *cs, UnkStruct_002A65F0_info *info, RwUInt32 *req)
{
    RwUInt32 n = (RwUInt32)info->field_4;
    RwUInt32 i = req[1];
    RwUInt32 link;
    if (i < (n >> 1)) {
        link = *(RwUInt32 *)(cs + 0x20);
        for (RwUInt32 k = 0; k != i; k++)
            link = *(RwUInt32 *)link;
    } else {
        link = *(RwUInt32 *)(cs + 0x24);
        for (RwUInt32 k = n - 1; k != i; k--)
            link = *(RwUInt32 *)(link + 4);
    }
    return func_002AD420(func_002A6320_inl1(link), *(RwaObjInterface **)(link + 0x2C), 2, req);
}

// The first sub-stream's position (node getter 3) while playing (state 5), else null.
// 0x002A63C0
extern "C" RwUInt32 _rwaStreamContainerGetStreamPos(RwUInt8 *cs, UnkStruct_002A65F0_info *info)
{
    if (*(RwUInt32 *)(cs + 8) != 5)
        return 0;
    RwUInt32 n = (RwUInt32)info->field_4;
    RwUInt32 link;
    if ((n >> 1) != 0) {
        link = *(RwUInt32 *)(cs + 0x20);
    } else {
        link = *(RwUInt32 *)(cs + 0x24);
        for (RwUInt32 k = n - 1; k != 0; k--)
            link = *(RwUInt32 *)(link + 4);
    }
    return RwaObjIntGetParamPointer(func_002A6320_inl1(link), *(RwaObjInterface **)(link + 0x2C), 3);
}

// 0x002A65B0
extern "C" void _rwaStreamContainerCommandSetPlayState(UnkStruct_002A65F0 *cmd, RwUInt32 state)
{
    cmd->field_4 = state;
    cmd->field_0 |= 0x80;
}

// 0x002A65D0
extern "C" void _rwaStreamContainerCommandSetLoopSegment(UnkStruct_002A65F0 *cmd, RwUInt32 segment)
{
    cmd->field_14 = segment;
    cmd->field_0 |= 0x40;
}

// 0x002A6690
extern "C" void _rwaStreamContainerCommandSetStreamPos(UnkStruct_002A65F0 *cmd, RwUInt32 pos)
{
    cmd->field_C = pos;
    cmd->field_0 |= 8;
}

// New media: play state 1, pitch 1, no position request or loop segment.
// 0x002A66B0
extern "C" void _rwaStreamContainerCommandSetMedia(UnkStruct_002A65F0 *cmd, RwUInt32 media)
{
    cmd->field_0 |= 4;
    cmd->field_8 = media;
    cmd->field_0 &= ~8u;
    cmd->field_C = 0;
    cmd->field_10 = 1.0f;
    cmd->field_4 = 1;
    cmd->field_14 = 0;
}

// 0x002A66F0
extern "C" void _rwaStreamContainerInitializeGains(UnkStruct_002A65F0 *cmd, UnkStruct_002A65F0_info *info)
{
    for (RwUInt32 i = 0; i < (RwUInt32)info->field_4; i++)
        cmd->field_18[i] = 1.0f;
}

// 0x002A6720
extern "C" void _rwaStreamContainerCommandInitialize(UnkStruct_002A65F0 *cmd)
{
    cmd->field_0 = 0;
    cmd->field_4 = 1;
    cmd->field_8 = 0;
    cmd->field_C = 0;
    cmd->field_10 = 1.0f;
    cmd->field_14 = 0;
}

// 0x002A6750
extern "C" RwaObj *func_002A6750(RwaObj *obj)
{
    return obj;
}

extern "C" RwBool RwaObjIntSetParamUInt32(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, RwUInt32 v);
extern "C" RwBool RwaObjIntSetParamBool(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, RwBool v);
extern "C" RwUInt32 RwaObjIntGetParamUInt32(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index);
extern "C" void RwaObjDestroy(RwaObj *obj, RwUInt32 p1, RwUInt32 p2);
extern "C" RwUInt32 _rwaStreamContainerStateFinishExecute(RwUInt8 *cs, RwUInt8 *info, RwUInt8 *cmd);
extern "C" RwUInt32 _RwaStreamContainerGetStatus(RwUInt8 *cs, RwUInt8 *info);
extern "C" RwUInt32 _RwaStreamContainerGetRenderStatus(RwUInt8 *cs);
extern "C" void _rwaOsStreamContainerStateUpdateCurrentStatus(RwUInt8 *cs, RwUInt32 info, RwUInt8 *status);
extern "C" RwUInt8 *_rwaStreamContainerStateCreate(RwUInt8 *cs, RwUInt8 *info, RwUInt8 *cmd, RwUInt32 p3, RwUInt32 p4,
                                                   RwUInt32 p5, RwUInt32 p6, RwUInt32 p7);

// The word at p + off.
static RwUInt32 &func_002A6470_inl1(void *p, RwUInt32 off)
{
    return *(RwUInt32 *)((RwUInt8 *)p + off);
}

// A node's setter 0 (its node interface at +0x40).
// 0x002A5550
extern "C" RwBool func_002A5550(RwaObj *node, RwUInt32 v)
{
    return RwaObjIntSetParamUInt32(node, *(RwaObjInterface **)((RwUInt8 *)node + 0x40), 0, v);
}

// A node's setter 2.
// 0x002A5560
extern "C" RwBool func_002A5560(RwaObj *node, RwBool v)
{
    return RwaObjIntSetParamBool(node, *(RwaObjInterface **)((RwUInt8 *)node + 0x40), 2, v);
}

// A render-chain enumeration callback: marks the container (5th argument) with flag 2.
// 0x002A5B90
extern "C" void func_002A5B90(RwUInt32 a0, RwUInt32 a1, RwUInt32 a2, RwUInt32 a3, RwUInt8 *cs)
{
    func_002A6470_inl1(cs, 0x5C) |= 2;
}

// A chain enumeration callback: the node leaves its list (link at +0x44) and is destroyed with the info's two words.
// 0x002A5F10
extern "C" void func_002A5F10(RwaObj *node, RwUInt8 *p)
{
    RwLLLink *l = (RwLLLink *)((RwUInt8 *)node + 0x44);
    l->prev->next = l->next;
    l->next->prev = l->prev;
    RwaObjDestroy(node, func_002A6470_inl1(p, 0x54), func_002A6470_inl1(p, 0x58));
}

// Per update: the state machine, then (unless being destroyed, flag 1) the status words for the status report:
// +0x148 the container status, +0x14C the source's buffered amount while preparing / playing (states 3-5; else 0),
// +0x150 the render status.
// 0x002A6470
extern "C" RwUInt32 _rwaStreamContainerExecute(RwUInt8 *cs)
{
    RwUInt32 r = _rwaStreamContainerStateFinishExecute(cs, cs + 0x68, cs + 0x70);
    if (func_002A6470_inl1(cs, 0x5C) & 1)
        return r;
    func_002A6470_inl1(cs, 0x148) = _RwaStreamContainerGetStatus(cs, cs + 0x68);
    RwUInt32 st = func_002A6470_inl1(cs, 8);
    if (st == 5 || st == 4 || st == 3) {
        RwaObj *src = (RwaObj *)func_002A6470_inl1(cs, 0x14);
        func_002A6470_inl1(cs, 0x14C) =
            RwaObjIntGetParamUInt32(src, (RwaObjInterface *)func_002A6470_inl1(src, 0x70), 3);
    } else {
        func_002A6470_inl1(cs, 0x14C) = 0;
    }
    func_002A6470_inl1(cs, 0x150) = _RwaStreamContainerGetRenderStatus(cs);
    _rwaOsStreamContainerStateUpdateCurrentStatus(cs, (RwUInt32)(cs + 0x68), cs + 0x148);
    return r;
}

// 0x002A6550
extern "C" RwUInt8 *_rwaStreamContainerCreate(RwUInt8 *cs, RwUInt32 p1, RwUInt32 p2, RwUInt32 p3, RwUInt32 p4,
                                              RwUInt32 p5)
{
    RwUInt8 *r = _rwaStreamContainerStateCreate(cs, cs + 0x68, cs + 0x70, p1, p2, p3, p4, p5);
    if (r) {
        func_002A6470_inl1(r, 0x14C) = 0;
        func_002A6470_inl1(r, 0x150) = 0;
        func_002A6470_inl1(r, 0x148) = 0;
    }
    return r;
}

typedef void (*UnkFn_002A5F40)(RwaObj *node, RwUInt32 arg);
extern RwInt32 _rwaGprogramContext;
extern RwaUUID rwaSTREAMFANOUTID;
extern "C" RwInt32 RwaUUIDCompare(const RwaUUID *a, const RwaUUID *b);
extern "C" RwInt32 _rwaLockedCountCompare(RwInt32 *count, RwInt32 value);
extern "C" RwBool RwaObjIntSetParamReal(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, RwReal v);
extern "C" RwBool func_002AD440(RwaObj *obj, RwaObjInterface *intf, RwUInt32 index, void *value);
extern "C" RwBool RwaSPU2RemoteIOPFopsPreviousOperationComplete(RwUInt32 handle, RwBool wait);
typedef void (*UnkFn_002A5884)(RwUInt32 a0, RwUInt32 a1, RwUInt32 a2, RwUInt32 a3, RwUInt8 *cs);
extern "C" void RwaSPU2RemoteIOPFopsSetCloseCallback(RwUInt32 handle, UnkFn_002A5884 fn, RwUInt32 data);
extern "C" void _RwaStreamContainerOpenStateMachine(RwUInt8 *cs, RwUInt8 *info, RwUInt8 *cmd);
extern "C" RwInt32 func_00114E60(void);   // DIntr
extern "C" RwInt32 EIntr(void);

// The node of a container link (its +0x44).
static RwaObj *func_002A5F40_inl1(RwUInt8 *link)
{
    return (RwaObj *)(link - 0x44);
}

// Walks every render chain (cs +0x20, linked at node +0x44) upstream (node +0x50), calling fn(node, arg) on each node
// below the shared part; returns the first shared node (the source cs +0x14, or a fan-out). Without fn: the first
// chain's shared node.
// 0x002A5F40
extern "C" RwaObj *_rwaStreamContainerEnumRenderChains(RwUInt8 *cs, UnkFn_002A5F40 fn, RwUInt32 arg)
{
    RwaObj *shared = 0;
    for (RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(cs, 0x20); l != cs + 0x20;) {
        RwaObj *n = func_002A5F40_inl1(l);
        l = (RwUInt8 *)func_002A6470_inl1(l, 0);
        for (;;) {
            RwaObj *up = (RwaObj *)func_002A6470_inl1(n, 0x50);
            if ((RwaObj *)func_002A6470_inl1(cs, 0x14) == n ||
                !RwaUUIDCompare(*(RwaUUID **)func_002A6470_inl1(n, 0), &rwaSTREAMFANOUTID)) {
                shared = n;
                break;
            }
            if (fn)
                fn(n, arg);
            if (!up)
                break;
            n = up;
        }
        if (!fn)
            return shared;
    }
    return shared;
}

// The status reported while playing (state 5): from the render chains' node statuses (param 0) for the play state
// (1 stop: 3 when all stopped, else 8; 2 play: 7 when any finished, 3 when all stopped, else 6; 3 pause: 5 when all
// paused, else 4); 2 while opening (states 2-4), 0 otherwise.
// 0x002A5D40
extern "C" RwUInt32 _RwaStreamContainerGetRenderStatus(RwUInt8 *cs)
{
    RwUInt32 st = func_002A6470_inl1(cs, 8);
    if (st != 5)
        return (st == 4 || st == 3 || st == 2) ? 2 : 0;
    RwUInt32 ps = func_002A6470_inl1(cs, 0x38);
    RwUInt8 *head = cs + 0x20;
    RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(cs, 0x20);
    if (ps == 1) {
        for (; l != head; l = (RwUInt8 *)func_002A6470_inl1(l, 0))
            if (RwaObjIntGetParamUInt32(func_002A5F40_inl1(l), (RwaObjInterface *)func_002A6470_inl1(l - 4, 0), 0) != 3)
                return 8;
        return 3;
    }
    if (ps == 2) {
        RwUInt32 r = 6;
        RwBool stopped = 1;
        for (; l != head; l = (RwUInt8 *)func_002A6470_inl1(l, 0)) {
            RwUInt32 v = RwaObjIntGetParamUInt32(func_002A5F40_inl1(l), (RwaObjInterface *)func_002A6470_inl1(l - 4, 0), 0);
            if (v == 7) {
                r = 7;
                break;
            }
            if (v != 3)
                stopped = 0;
        }
        if (r == 6 && stopped == 1)
            r = 3;
        return r;
    }
    if (ps == 3) {
        for (; l != head; l = (RwUInt8 *)func_002A6470_inl1(l, 0))
            if (RwaObjIntGetParamUInt32(func_002A5F40_inl1(l), (RwaObjInterface *)func_002A6470_inl1(l - 4, 0), 0) != 5)
                return 4;
        return 5;
    }
    return 0;
}

// The container status: 2 while opening, 9 failed (state 6), else from the render status and the source's status
// (getter 3) for the play state: stop -> 3 when both stopped else 8; play -> the render status; pause -> 5 when both
// paused else 4.
// 0x002A5BA0
extern "C" RwUInt32 _RwaStreamContainerGetStatus(RwUInt8 *cs, RwUInt8 *info)
{
    RwUInt32 st = func_002A6470_inl1(cs, 8);
    if (st != 5) {
        if (st == 4 || st == 3 || st == 2)
            return 2;
        if (st == 6)
            return 9;
        return 0;
    }
    RwUInt32 r = _RwaStreamContainerGetRenderStatus(cs);
    st = func_002A6470_inl1(cs, 8);
    RwUInt32 src = 0;
    if (st == 5 || st == 4 || st == 3) {
        RwaObj *s = (RwaObj *)func_002A6470_inl1(cs, 0x14);
        src = RwaObjIntGetParamUInt32(s, (RwaObjInterface *)func_002A6470_inl1(s, 0x70), 3);
    }
    RwUInt32 ps = func_002A6470_inl1(cs, 0x38);
    if (ps == 1)
        return (src == 3 && r == 3) ? 3 : 8;
    if (ps == 2)
        return r;
    if (ps == 3)
        return ((src == 5 || src == 3) && r == 5) ? 5 : 4;
    return 0;
}

// The shared part (from `n` upstream): setter 0 of each node.
static void func_002A5570_inl1(RwaObj *n, RwUInt32 v)
{
    while (n) {
        RwaObj *up = (RwaObj *)func_002A6470_inl1(n, 0x50);
        RwaObjIntSetParamUInt32(n, (RwaObjInterface *)func_002A6470_inl1(n, 0x40), 0, v);
        n = up;
    }
}

// The node leaves its list (link at +0x44) and is destroyed with the container's two words (+0x54 / +0x58).
static void func_002A5570_inl2(RwUInt8 *cs, RwaObj *n)
{
    RwLLLink *l = (RwLLLink *)((RwUInt8 *)n + 0x44);
    l->prev->next = l->next;
    l->next->prev = l->prev;
    RwaObjDestroy(n, func_002A6470_inl1(cs, 0x54), func_002A6470_inl1(cs, 0x58));
}

// Every render chain and every other node (cs +0x18) destroyed; no source.
static void func_002A5570_inl3(RwUInt8 *cs)
{
    RwaObj *n = _rwaStreamContainerEnumRenderChains(cs, (UnkFn_002A5F40)func_002A5F10, (unsigned int)cs);
    while (n) {
        RwaObj *up = (RwaObj *)func_002A6470_inl1(n, 0x50);
        func_002A5570_inl2(cs, n);
        n = up;
    }
    RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(cs, 0x18);
    while (l != cs + 0x18) {
        RwaObj *o = func_002A5F40_inl1(l);
        l = (RwUInt8 *)func_002A6470_inl1(l, 0);
        func_002A5570_inl2(cs, o);
    }
    func_002A6470_inl1(cs, 0x14) = 0;
}

// The play state to every node (render chains and the shared part); entering play (2) snapshots the stream count.
static void func_002A5570_inl4(RwUInt8 *cs, RwUInt8 *info, RwUInt32 ps)
{
    if (ps == 2) {
        RwInt32 was = 0;
        if (_rwaGprogramContext == 2)
            was = func_00114E60();
        func_002A6470_inl1(cs, 0x3C) = func_002A6470_inl1(info, 4);
        if (_rwaGprogramContext == 2 && was == 1)
            EIntr();
    }
    func_002A5570_inl1(_rwaStreamContainerEnumRenderChains(cs, (UnkFn_002A5F40)func_002A5550, ps), ps);
}

// Per update: a pending destroy (cmd flag 4, once nothing holds the container) tears the chains down -- an IOP-file
// source first waits for its file operation and closes through func_002A5B90 (flag 1 then 2 back to state 2); the
// open state machine below state 5; then the pending commands in order: stream position (8: flush to every chain,
// play state, the source's setter 0), pitch (0x20: each chain's rate = format rate * pitch, truncated), gains (0x10),
// loop segment (0x40), play state (0x80). Returns 1 when the open finished in this call.
// 0x002A5570
extern "C" RwUInt32 _rwaStreamContainerStateFinishExecute(RwUInt8 *cs, RwUInt8 *info, RwUInt8 *cmd)
{
    RwUInt32 opened = 0;
    if ((func_002A6470_inl1(cmd, 0) & 4) && !_rwaLockedCountCompare((RwInt32 *)(cs + 4), 0)) {
        RwUInt32 f = func_002A6470_inl1(cs, 0x5C);
        if (f & 1) {
            if (!(f & 2))
                return 0;
            func_002A6470_inl1(cs, 0x5C) &= ~1u;
            func_002A6470_inl1(cs, 0x5C) &= ~2u;
            func_002A6470_inl1(cs, 8) = 2;
            func_002A6470_inl1(cmd, 0) &= ~4u;
        } else {
            RwUInt8 *src = (RwUInt8 *)func_002A6470_inl1(cs, 0x14);
            if (src && func_002A6470_inl1(src, 0xF8)) {
                if (RwaSPU2RemoteIOPFopsPreviousOperationComplete(func_002A6470_inl1(src, 0xF8), 0)) {
                    RwaSPU2RemoteIOPFopsSetCloseCallback(func_002A6470_inl1(src, 0xF8), func_002A5B90, (unsigned int)cs);
                    func_002A6470_inl1(cs, 0x5C) |= 1;
                    func_002A5570_inl3(cs);
                    return 0;
                }
            } else {
                func_002A5570_inl3(cs);
                func_002A6470_inl1(cs, 8) = 2;
                func_002A6470_inl1(cmd, 0) &= ~4u;
            }
        }
    }
    RwUInt32 st = func_002A6470_inl1(cs, 8);
    if (st < 5) {
        _RwaStreamContainerOpenStateMachine(cs, info, cmd);
        st = func_002A6470_inl1(cs, 8);
        if (st < 5)
            return 0;
        opened = 1;
    }
    if (st == 6)
        return opened;
    if (func_002A6470_inl1(cmd, 0) & 8) {
        RwUInt32 *pos = (RwUInt32 *)func_002A6470_inl1(cmd, 0xC);
        RwUInt32 flush = pos[1];
        if (flush || func_002A6470_inl1(cs, 0x38) != 1) {
            RwaObj *n = _rwaStreamContainerEnumRenderChains(cs, (UnkFn_002A5F40)func_002A5560, flush);
            RwUInt32 v = ((RwUInt32 *)func_002A6470_inl1(cmd, 0xC))[1];
            while (n) {
                RwaObj *up = (RwaObj *)func_002A6470_inl1(n, 0x50);
                RwaObjIntSetParamBool(n, (RwaObjInterface *)func_002A6470_inl1(n, 0x40), 2, v);
                n = up;
            }
        }
        func_002A5570_inl4(cs, info, func_002A6470_inl1(cs, 0x38));
        ((RwUInt32 *)func_002A6470_inl1(cmd, 0xC))[1] = 0;
        RwaObj *src = (RwaObj *)func_002A6470_inl1(cs, 0x14);
        func_002AD440(src, (RwaObjInterface *)func_002A6470_inl1(src, 0x70), 0, (void *)func_002A6470_inl1(cmd, 0xC));
        func_002A6470_inl1(cmd, 0) &= ~8u;
    }
    if (func_002A6470_inl1(cmd, 0) & 0x20) {
        *(RwReal *)(cs + 0x34) = *(RwReal *)(cmd + 0x10);
        RwUInt8 *fmts = (RwUInt8 *)func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x18);
        RwUInt32 i = 0;
        for (RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(cs, 0x20); l != cs + 0x20; l = (RwUInt8 *)func_002A6470_inl1(l, 0)) {
            RwUInt32 rate = *(RwUInt32 *)func_002A6470_inl1(fmts + i * 40, 0x14);
            RwReal fr = (RwReal)rate;
            RwReal pitch = *(RwReal *)(cs + 0x34);
            RwReal r = fr * pitch;
            RwaObjIntSetParamUInt32(func_002A5F40_inl1(l), (RwaObjInterface *)func_002A6470_inl1(l, 0x2C), 0, (int)r);
            i++;
        }
        func_002A6470_inl1(cmd, 0) &= ~0x20u;
    }
    if (func_002A6470_inl1(cmd, 0) & 0x10) {
        RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(cs, 0x20);
        for (RwUInt32 i = 0; i < func_002A6470_inl1(info, 4); i++) {
            RwaObjIntSetParamReal(func_002A5F40_inl1(l), (RwaObjInterface *)func_002A6470_inl1(l, 0x2C), 1,
                                  *(RwReal *)(cmd + 0x18 + i * 4));
            l = (RwUInt8 *)func_002A6470_inl1(l, 0);
        }
        func_002A6470_inl1(cmd, 0) &= ~0x10u;
    }
    if (func_002A6470_inl1(cmd, 0) & 0x40) {
        func_002A6470_inl1(cs, 0x30) = func_002A6470_inl1(cmd, 0x14);
        RwaObj *src = (RwaObj *)func_002A6470_inl1(cs, 0x14);
        RwaObjIntSetParamBool(src, (RwaObjInterface *)func_002A6470_inl1(src, 0x70), 1, func_002A6470_inl1(cmd, 0x14));
        func_002A6470_inl1(cmd, 0) &= ~0x40u;
    }
    if (func_002A6470_inl1(cmd, 0) & 0x80) {
        RwUInt32 ps = func_002A6470_inl1(cmd, 4);
        if (func_002A6470_inl1(cs, 0x38) != ps) {
            func_002A5570_inl4(cs, info, ps);
            func_002A6470_inl1(cs, 0x38) = func_002A6470_inl1(cmd, 4);
        }
        func_002A6470_inl1(cmd, 0) &= ~0x80u;
    }
    return opened;
}

// ---- creation / open ----
extern "C" RwInt32 *_rwaLockedCountCreate(RwInt32 *count, RwInt32 value);
extern "C" void _RwaStreamContainerNodeCreationComplete(RwaObj *node, RwInt32 *counts);
extern "C" RwBool _RwaStreamContainerInstantiateSource(RwUInt8 *cs, RwUInt32 *media);
extern "C" RwBool _RwaStreamContainerInstantiateRenderChains(RwUInt8 *cs, RwUInt8 *info);
extern "C" void _RwaStreamContainerSetDefaultRendererParams(RwUInt8 *cs, RwUInt8 *info);
extern "C" void func_002A36A0(RwUInt32 unused, RwUInt8 *o);
extern "C" RwaObj *RwaObjCreate(RwaObjDef *def, RwaObj *p1, RwUInt32 p2, RwUInt32 *params, RwUInt32 p4, RwUInt32 p5);
extern "C" RwInt32 RwaWaveDefFormatWaveFormatCompare(void *defFormat, RwUInt8 *format);
extern "C" void *func_002934C0(RwUInt32 size, RwUInt32 hint);
extern "C" void func_00293440(void *p);
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);

// count += d with interrupts off (with RWA threads).
static void func_002A60F0_inl1(RwInt32 *count, RwInt32 d)
{
    RwInt32 was = 0;
    if (_rwaGprogramContext == 2)
        was = func_00114E60();
    *count = *count + d;
    if (_rwaGprogramContext == 2 && was == 1)
        EIntr();
}

// A container node finished its creation: status 1 (failed) counts into counts[0]; counts[1] (pending) drops.
// 0x002A60F0
extern "C" void _RwaStreamContainerNodeCreationComplete(RwaObj *node, RwInt32 *counts)
{
    if (RwaObjIntGetParamUInt32(node, (RwaObjInterface *)func_002A6470_inl1(node, 0x40), 0) == 1)
        func_002A60F0_inl1(&counts[0], 1);
    func_002A60F0_inl1(&counts[1], -1);
}

// The container state: counts at +0 (failed nodes), +4 (pending nodes), +0x3C; empty node lists (+0x18 others,
// +0x20 chains); the node completion callback {_RwaStreamContainerNodeCreationComplete, cs} at +0x28; play state 1,
// pitch 1, state 6 (idle); +0x48 / +0x4C / +0x50 / +0x54 / +0x58 the creation arguments; +0x44 = 0x20.
// 0x002A61E0
extern "C" RwUInt8 *_rwaStreamContainerStateCreate(RwUInt8 *cs, RwUInt8 *info, RwUInt8 *cmd, RwUInt32 p3, RwUInt32 p4,
                                                   RwUInt32 p5, RwUInt32 p6, RwUInt32 p7)
{
    if (!_rwaLockedCountCreate((RwInt32 *)cs, 0))
        return 0;
    if (!_rwaLockedCountCreate((RwInt32 *)(cs + 4), 0))
        return 0;
    if (!_rwaLockedCountCreate((RwInt32 *)(cs + 0x3C), 0))
        return 0;
    func_002A6470_inl1(info, 0) = 0;
    func_002A6470_inl1(info, 4) = 0;
    _rwaStreamContainerCommandInitialize((UnkStruct_002A65F0 *)cmd);
    func_002A6470_inl1(cs, 0x18) = (unsigned int)(cs + 0x18);
    func_002A6470_inl1(cs, 0x1C) = (unsigned int)(cs + 0x18);
    func_002A6470_inl1(cs, 0x20) = (unsigned int)(cs + 0x20);
    func_002A6470_inl1(cs, 0x24) = (unsigned int)(cs + 0x20);
    func_002A6470_inl1(cs, 0x28) = (unsigned int)_RwaStreamContainerNodeCreationComplete;
    func_002A6470_inl1(cs, 0x2C) = (unsigned int)cs;
    func_002A6470_inl1(cs, 0x38) = 1;
    func_002A6470_inl1(cs, 0x30) = 0;
    func_002A6470_inl1(cs, 0x34) = 0x3F800000;
    func_002A6470_inl1(cs, 8) = 6;
    func_002A6470_inl1(cs, 0xC) = 0;
    func_002A6470_inl1(cs, 0x14) = 0;
    func_002A6470_inl1(cs, 0x48) = p3;
    func_002A6470_inl1(cs, 0x40) = 0;
    func_002A6470_inl1(cs, 0x44) = 0x20;
    func_002A6470_inl1(cs, 0x10) = 0;
    func_002A6470_inl1(cs, 0x4C) = p4;
    func_002A6470_inl1(cs, 0x50) = p5;
    func_002A6470_inl1(cs, 0x54) = p6;
    func_002A6470_inl1(cs, 0x58) = p7;
    func_002A6470_inl1(cs, 0x5C) = 0;
    return cs;
}

// The open: state 2 (media set: counts reset, play state 1, the source instantiated -> 3; no media -> 6), 3 (once the
// source's creation completed without failure: the format (source getter 2), the render chains -> 4), 4 (a failed
// node -> 6; once nothing is pending: gains, default renderer rates -> 5). On failure every node is destroyed and
// the state is 6.
// 0x002A3730
extern "C" void _RwaStreamContainerOpenStateMachine(RwUInt8 *cs, RwUInt8 *info, RwUInt8 *cmd)
{
    RwBool fail = 0;
    RwUInt32 st = func_002A6470_inl1(cs, 8);
    if (st == 6 || st == 5) {
        fail = 1;
    } else if (st == 4) {
        goto state4;
    } else if (st == 3) {
        if (_rwaLockedCountCompare((RwInt32 *)cs, 0) > 0) {
            fail = 1;
        } else if (!(_rwaLockedCountCompare((RwInt32 *)(cs + 4), 0) > 0)) {
            RwaObj *src = (RwaObj *)func_002A6470_inl1(cs, 0x14);
            func_002A6470_inl1(cs, 0xC) = RwaObjIntGetParamPointer(src, (RwaObjInterface *)func_002A6470_inl1(src, 0x70), 2);
            if (!_RwaStreamContainerInstantiateRenderChains(cs, info)) {
                fail = 1;
            } else {
                func_002A6470_inl1(cs, 8) = 4;
                goto state4;
            }
        }
    } else if (st == 2) {
        if (!func_002A6470_inl1(cmd, 8)) {
            func_002A6470_inl1(cs, 8) = 6;
        } else {
            RwInt32 was = 0;
            if (_rwaGprogramContext == 2)
                was = func_00114E60();
            func_002A6470_inl1(cs, 0) = 0;
            if (_rwaGprogramContext == 2 && was == 1)
                EIntr();
            if (_rwaGprogramContext == 2)
                was = func_00114E60();
            func_002A6470_inl1(cs, 4) = 1;
            if (_rwaGprogramContext == 2 && was == 1)
                EIntr();
            func_002A6470_inl1(cs, 0x38) = 1;
            if (_RwaStreamContainerInstantiateSource(cs, (RwUInt32 *)func_002A6470_inl1(cmd, 8)))
                func_002A6470_inl1(cs, 8) = 3;
            else
                fail = 1;
        }
    } else {
        fail = 1;
    }
    goto done;
state4:
    if (_rwaLockedCountCompare((RwInt32 *)cs, 0) > 0) {
        func_002A6470_inl1(cs, 8) = 6;
        fail = 1;
    }
    if (!(_rwaLockedCountCompare((RwInt32 *)(cs + 4), 0) > 0)) {
        _rwaStreamContainerInitializeGains((UnkStruct_002A65F0 *)cmd, (UnkStruct_002A65F0_info *)info);
        _RwaStreamContainerSetDefaultRendererParams(cs, info);
        func_002A6470_inl1(cs, 8) = 5;
    }
done:
    if (!fail)
        return;
    RwaObj *n = _rwaStreamContainerEnumRenderChains(cs, (UnkFn_002A5F40)func_002A5F10, (unsigned int)cs);
    while (n) {
        RwaObj *up = (RwaObj *)func_002A6470_inl1(n, 0x50);
        func_002A5570_inl2(cs, n);
        n = up;
    }
    RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(cs, 0x18);
    while (l != cs + 0x18) {
        RwaObj *o = func_002A5F40_inl1(l);
        l = (RwUInt8 *)func_002A6470_inl1(l, 0);
        func_002A5570_inl2(cs, o);
    }
    func_002A6470_inl1(cs, 8) = 6;
}

// Each chain's renderer (when its definition's misc data has a rate list, +0xC) gets the stream format's rate,
// rounded for an output rate grid (info flag 2, cs +0x40 / +0x44 / +0x4C) as retail computes it.
// 0x002A3A10
extern "C" void _RwaStreamContainerSetDefaultRendererParams(RwUInt8 *cs, RwUInt8 *info)
{
    RwUInt32 i = 0;
    for (RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(cs, 0x20); l != cs + 0x20; l = (RwUInt8 *)func_002A6470_inl1(l, 0), i++) {
        RwaObj *node = func_002A5F40_inl1(l);
        RwUInt8 *misc = (RwUInt8 *)node->definition->miscData;
        if (!func_002A6470_inl1(misc, 0xC))
            continue;
        RwUInt8 *f = (RwUInt8 *)(func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x18) + i * 40);
        RwUInt32 rate = *(RwUInt32 *)func_002A6470_inl1(f, 0x14);
        if ((func_002A6470_inl1(info, 0) & 2) && func_002A6470_inl1(cs, 0x44) < 0x20) {
            RwUInt32 v = func_002A6470_inl1(cs, 0x40);
            RwUInt32 k = 2;
            while (rate == v && v) {
                k--;
                v >>= 1;
            }
            RwUInt32 sh = (func_002A6470_inl1(cs, 0x44) - k) & 31;
            RwUInt32 grid = func_002A6470_inl1(cs, 0x4C);
            rate = (((rate << sh) / grid) * grid) >> sh;
        }
        RwaObjIntSetParamUInt32(node, (RwaObjInterface *)func_002A6470_inl1(node, 0x70), 0, rate);
    }
}

// Creates a chain's renderer: its creation parameters completed (+0x2C the container's +0x60, +0x1C `format`, +0xC
// the container's +0x10, +0x24 / +0x28 the completion callback func_002A36A0 and the container, +0x30 1 unless the
// source is an IOP stream), one more pending node, then RwaObjCreate(rend +4, ...).
// 0x002A45B0
extern "C" RwaObj *_RwaStreamContainerInstanceRenderer(RwUInt8 *cs, RwUInt8 *rend, RwUInt32 format, RwUInt32 *params)
{
    params[0xB] = (unsigned int)(cs + 0x60);
    params[7] = format;
    params[3] = func_002A6470_inl1(cs, 0x10);
    params[0xA] = (unsigned int)cs;
    params[9] = (unsigned int)func_002A36A0;
    params[0xC] = (func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0x14), 0x200) == 2) ? 0 : 1;
    func_002A60F0_inl1((RwInt32 *)(cs + 4), 1);
    return RwaObjCreate((RwaObjDef *)func_002A6470_inl1(rend, 4), (RwaObj *)func_002A6470_inl1(cs, 0x48), 4, params,
                        func_002A6470_inl1(cs, 0x50), func_002A6470_inl1(cs, 0x58));
}

// The first renderer definition on the list (walked through +4; definition at +8) with a wave format (misc data +4,
// count +8) of the format's type, bytes per sample (+0xC), flag 1 and channels, and a rate range holding its rate.
// 0x002A5060
extern "C" RwaObjDef *_RwaStreamContainerFindRendererByWaveFormat(RwUInt8 *list, RwUInt8 *wf)
{
    for (RwUInt8 *e = (RwUInt8 *)func_002A6470_inl1(list, 4); e != list; e = (RwUInt8 *)func_002A6470_inl1(e, 4)) {
        RwaObjDef *def = (RwaObjDef *)func_002A6470_inl1(e, 8);
        RwUInt8 *misc = (RwUInt8 *)def->miscData;
        for (RwUInt32 i = 0; i < func_002A6470_inl1(misc, 8); i++) {
            RwUInt8 *rf = (RwUInt8 *)((RwUInt32 *)func_002A6470_inl1(misc, 4))[i];
            RwBool a = (wf[0x18] & 1) != 0;
            RwBool b = (rf[0x10] & 1) != 0;
            if (RwaUUIDCompare((RwaUUID *)func_002A6470_inl1(wf, 4), (RwaUUID *)func_002A6470_inl1(rf, 0)))
                continue;
            if (wf[0xC] != rf[4] || b != a || wf[0xD] != rf[5])
                continue;
            RwUInt32 rate = func_002A6470_inl1(wf, 0);
            if (func_002A6470_inl1(rf, 0xC) < rate || rate < func_002A6470_inl1(rf, 8))
                continue;
            return def;
        }
    }
    return 0;
}

// For each stream format: an entry (0x54 bytes: +4 the renderer definition, +8 a1, +0xC the wave format, +0x44 a2,
// +0x48 the format index, linked at +0x4C at the head of `list`) when a renderer supports it. On an allocation
// failure the list is emptied and freed, 0.
// 0x002A4A70
extern "C" RwBool _RwaStreamContainerPreinstanceRenderers(RwUInt8 *cs, RwUInt32 a1, RwUInt32 a2, RwUInt8 *list,
                                                          RwUInt8 *defs)
{
    for (RwUInt32 i = 0; i < func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x14); i++) {
        RwUInt8 *wf = (RwUInt8 *)func_002A6470_inl1(
            (void *)(func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x18) + i * 40), 0x14);
        RwaObjDef *def = _RwaStreamContainerFindRendererByWaveFormat(defs, wf);
        if (!def)
            continue;
        RwUInt8 *e = (RwUInt8 *)func_002934C0(0x54, 0x3080F);
        if (e) {
            func_002A6470_inl1(e, 0x4C) = func_002A6470_inl1(list, 0);
            func_002A6470_inl1(e, 0x50) = (unsigned int)list;
            ((RwUInt32 *)func_002A6470_inl1(list, 0))[1] = (unsigned int)(e + 0x4C);
            func_002A6470_inl1(list, 0) = (unsigned int)(e + 0x4C);
            if (wf)
                memcpy(e + 0xC, wf, 0x1C);
            func_002A6470_inl1(e, 4) = (unsigned int)def;
            func_002A6470_inl1(e, 8) = a1;
            func_002A6470_inl1(e, 0x48) = i;
            func_002A6470_inl1(e, 0x44) = a2;
            func_002A6470_inl1(e, 0) = 0;
            continue;
        }
        for (RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(list, 0); l != list; l = (RwUInt8 *)func_002A6470_inl1(list, 0)) {
            RwUInt8 *x = l - 0x4C;
            RwUInt32 next = func_002A6470_inl1(l, 0);
            if (next) {
                *(RwUInt32 *)func_002A6470_inl1(x, 0x50) = next;
                ((RwUInt32 *)func_002A6470_inl1(x, 0x4C))[1] = func_002A6470_inl1(x, 0x50);
            }
            func_00293440(x);
        }
        return 0;
    }
    return 1;
}

// ---- chain instancing ----
extern RwaUUID rwaSTREAMSOURCEID;
extern RwaUUID rwaSTREAMSOURCEINTERFACEID;
extern RwaUUID rwaSTREAMNODEINTERFACEID;
extern RwaUUID rwaSTREAMRENDERERINTERFACEID;
extern RwaUUID rwaSTREAMCODECINTERFACEID;
extern RwUInt8 ourGlobals[0x200];             // RenderWare's globals: +0x120 the string length [L]
typedef RwUInt32 (*UnkFn_002A5450)(const RwChar *s);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefFindByUUID(RwaUUID *id);
extern "C" RwaObjDef *_rwaObjDefFindByUUID(RwaObjDef *parent, const RwaUUID *uuid, RwaObjDef **out);
extern "C" RwBool _rwaObjDefBuildSupportedList(RwaObjDef *parent, RwaObjInterfaceDef *intfDef, RwaLLNode *list);
extern "C" RwaObjDef *RwaObjDefGetDefaultCreationParams(RwaObjDef *def, RwUInt32 *out);
extern "C" void RwaLListEmpty(RwaLLNode *list);
extern "C" RwChar *func_002934D0(const RwChar *s);   // the file name's extension
extern "C" RwInt32 _rwamemicmp(const RwInt8 *a, const RwInt8 *b, RwUInt32 n);
extern "C" RwBool func_002AD480(RwaObj *to, RwUInt32 value, RwaObj *from, RwUInt32 seed);
extern "C" void _rwaWaveDefFormatCopyToWaveFormat(void *defFormat, RwUInt8 *out, RwBool clear);
extern "C" RwBool _RwaStreamContainerPreinstanceCodecs(RwUInt8 *wf, RwUInt8 *target, RwUInt32 index,
                                                       RwaObjInterfaceDef *codecIntf, RwaLLNode *codecs,
                                                       RwLinkList *chain, RwUInt8 *cs, RwUInt32 *out);
extern "C" RwUInt32 RwaWaveFormatGetSamplesPerAudioFrame(RwUInt8 *format);
extern "C" RwUInt32 RwaWaveFormatGetFrameSize(RwUInt8 *format);
extern "C" RwBool _RwaStreamContainerPreinstanceChains(RwUInt8 *cs, RwaObjInterfaceDef *rendIntf,
                                                       RwaObjInterfaceDef *codecIntf, RwUInt8 *root, RwLinkList *list,
                                                       RwaLLNode *renderers);

// Links the object's +0x44 link first into the container list at cs + off (+0x18 the other nodes, +0x20 the chains).
static void func_002A3B20_inl1(RwUInt8 *cs, RwUInt32 off, RwaObj *obj)
{
    func_002A6470_inl1(obj, 0x44) = func_002A6470_inl1(cs, off);
    func_002A6470_inl1(obj, 0x48) = (unsigned int)(cs + off);
    func_002A6470_inl1((void *)func_002A6470_inl1(cs, off), 4) = (unsigned int)obj + 0x44;
    func_002A6470_inl1(cs, off) = (unsigned int)obj + 0x44;
}

// Frees every chain entry (0x54 bytes, linked at +0x4C) on `list`, unlinking each first.
static void func_002A3B20_inl2(RwLinkList *list)
{
    for (RwLLLink *l = list->link.next; l != &list->link; l = list->link.next) {
        RwUInt8 *e = (RwUInt8 *)l - 0x4C;
        if (l->next) {
            func_002A6470_inl1((void *)func_002A6470_inl1(e, 0x50), 0) = (unsigned int)l->next;
            func_002A6470_inl1((void *)func_002A6470_inl1(e, 0x4C), 4) = func_002A6470_inl1(e, 0x50);
        }
        func_00293440(e);
    }
}

// Creates the container's source node for the media (+4: 3 a stream source by UUID; 2 a source that supports the wave
// format at +8; 1 a source whose file extension list (misc +4, count +8) holds the extension of the file name at +0):
// its creation parameters from the media and the container, linked into the container's node list (+0x18) and kept
// at +0x14. Type 2 takes the last definition that matches, type 1 the first [C 0x2A54D8, 0x2A54B8]. 0 on a failure.
// 0x002A5190
extern "C" RwBool _RwaStreamContainerInstantiateSource(RwUInt8 *cs, RwUInt32 *media)
{
    B3_RETAIL_FRAME(0xC0);
    // the supported-definition lists' heads: their nodes point back to them, so they are where retail has them
    RwaLLNode *byFormat = B3_FRAME_LOCAL(RwaLLNode, 0xA0);
    RwaLLNode *byName = B3_FRAME_LOCAL(RwaLLNode, 0xB0);
    RwUInt32 *found = B3_FRAME_LOCAL(RwUInt32, 0xBC);
    RwaObjDef *def = 0;
    RwUInt32 type = media[1];
    if (type == 3) {
        def = _rwaObjDefFindByUUID(0, &rwaSTREAMSOURCEID, (RwaObjDef **)found);
    } else if (type == 2) {
        if (media[2]) {
            byFormat->prev = byFormat;
            byFormat->next = byFormat;
            byFormat->data = 0;
            if (!_rwaObjDefBuildSupportedList(0, RwaObjInterfaceDefFindByUUID(&rwaSTREAMSOURCEINTERFACEID), byFormat))
                return 0;
            for (RwaLLNode *n = byFormat->next; n != byFormat; n = n->next) {
                RwaObjDef *d = (RwaObjDef *)n->data;
                RwUInt8 *misc = (RwUInt8 *)d->miscData;
                for (RwUInt32 k = 0; k < func_002A6470_inl1(misc, 0x10); k++) {
                    if (RwaWaveDefFormatWaveFormatCompare((void *)((RwUInt32 *)func_002A6470_inl1(misc, 0xC))[k],
                                                          (RwUInt8 *)media[2])) {
                        def = d;
                        break;
                    }
                }
            }
            RwaLListEmpty(byFormat);
        }
    } else if (type == 1) {
        byName->prev = byName;
        byName->next = byName;
        byName->data = 0;
        if (!_rwaObjDefBuildSupportedList(0, RwaObjInterfaceDefFindByUUID(&rwaSTREAMSOURCEINTERFACEID), byName))
            return 0;
        const RwChar *name = (const RwChar *)media[0];
        RwChar *ext = func_002934D0(name);
        if (ext == name) {
            RwaLListEmpty(byName);
            return 0;
        }
        for (RwaLLNode *n = byName->next; n != byName && !def; n = n->next) {
            RwaObjDef *d = (RwaObjDef *)n->data;
            RwUInt8 *misc = (RwUInt8 *)d->miscData;
            for (RwUInt32 k = 0; k < func_002A6470_inl1(misc, 8); k++) {
                const RwChar *e = (const RwChar *)((RwUInt32 *)func_002A6470_inl1(misc, 4))[k];
                RwUInt32 len = (*(UnkFn_002A5450 *)(ourGlobals + 0x120))(e);
                if (_rwamemicmp((const RwInt8 *)((RwUInt32 *)func_002A6470_inl1(misc, 4))[k], (const RwInt8 *)ext,
                                len) == 0) {
                    def = d;
                    break;
                }
            }
        }
        RwaLListEmpty(byName);
    }
    if (!def)
        return 0;
    RwUInt32 *p = (RwUInt32 *)func_002934C0(def->createParamSize, 0x1080F);
    if (!p)
        return 0;
    if (!RwaObjDefGetDefaultCreationParams(def, p)) {
        func_00293440(p);
        return 0;
    }
    p[0] = 0;
    p[1] = 1;
    p[2] = 0;
    p[4] = (unsigned int)(cs + 0x28);
    p[0xB] = media[0];
    p[0xC] = media[1];
    p[0xA] = media[2];
    p[0xD] = media[3];
    p[0xE] = media[4];
    p[0x10] = media[8];
    p[0x11] = media[7];
    p[0x17] = media[9];
    func_002A6470_inl1(cs, 0x10) = media[10];
    p[0x14] = media[11] ? media[10] : 0;
    p[0xF] = 0;
    p[0x12] = media[5];
    p[0x13] = media[6];
    p[0x16] = 0;
    p[0x15] = 0;
    RwaObj *src = RwaObjCreate(def, 0, 0, p, func_002A6470_inl1(cs, 0x50), func_002A6470_inl1(cs, 0x58));
    if (!src) {
        func_00293440(p);
        return 0;
    }
    func_002A3B20_inl1(cs, 0x18, src);
    func_002A6470_inl1(cs, 0x14) = (unsigned int)src;
    func_00293440(p);
    return 1;
}

// For each stream format with no chain entry on `list` yet: a chain of codec entries from a renderer (the supported
// renderers in `renderers`, tried by their misc level +0x10, lowest first) back to the format, found by
// _RwaStreamContainerPreinstanceCodecs over the renderer's input formats (misc +4, count +8), then one more entry for
// the format itself (+4 the renderer, +8 rendIntf, +0xC the format found, +0x44 the chain's last entry, +0x48 the
// format index) and the whole chain moved onto `list`; the chain's first entry gets `root` as its parent. The
// supported codecs are listed once (the list is not emptied on success [C 0x2A4884]). A renderer is picked as the
// last with a level at least the current one (retail compares the current minimum, not the best so far
// [C 0x2A4798]). 0 when a format has no chain or memory runs out.
// 0x002A46A0
extern "C" RwBool _RwaStreamContainerPreinstanceChains(RwUInt8 *cs, RwaObjInterfaceDef *rendIntf,
                                                       RwaObjInterfaceDef *codecIntf, RwUInt8 *root, RwLinkList *list,
                                                       RwaLLNode *renderers)
{
    B3_RETAIL_FRAME(0x110);
    RwUInt8 *wf = B3_FRAME_LOCAL(RwUInt8, 0xD0);             // a wave format (0x1C bytes)
    RwLinkList *chain = B3_FRAME_LOCAL(RwLinkList, 0xF8);    // the chain being built (entries point back to it)
    RwaLLNode *codecs = B3_FRAME_LOCAL(RwaLLNode, 0x100);    // the supported codecs (nodes point back to it)
    RwBool listed = 0;
    for (RwUInt32 i = 0; i < func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x14); i++) {
        RwBool have = 0;
        for (RwLLLink *l = list->link.next; l != &list->link; l = l->next) {
            if (func_002A6470_inl1((RwUInt8 *)l - 0x4C, 0x48) == i) {
                have = 1;
                break;
            }
        }
        if (have)
            continue;
        if (!codecIntf)
            return 0;
        if (!listed) {
            codecs->prev = codecs;
            codecs->next = codecs;
            codecs->data = 0;
            if (!_rwaObjDefBuildSupportedList(0, codecIntf, codecs))
                return 0;
            listed = 1;
        }
        chain->link.prev = &chain->link;
        chain->link.next = &chain->link;
        RwUInt32 level = 0;
        RwBool done = 0;
        RwUInt8 *fmt = (RwUInt8 *)func_002A6470_inl1(
            (void *)(func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x18) + i * 40), 0x14);
        RwaObjDef *rend;
        do {
            rend = 0;
            RwUInt32 best = 0xFFFFFFFF;
            for (RwaLLNode *n = renderers->next; n != renderers; n = n->next) {
                RwaObjDef *d = (RwaObjDef *)n->data;
                RwUInt32 v = func_002A6470_inl1((RwUInt8 *)d->miscData, 0x10);
                if (!(v < level) && !(best < level)) {
                    rend = d;
                    best = v;
                }
            }
            if (!rend)
                break;
            RwUInt8 *misc = (RwUInt8 *)rend->miscData;
            level = func_002A6470_inl1(misc, 0x10) + 1;
            for (RwUInt32 k = 0; k < func_002A6470_inl1(misc, 8); k++) {
                func_002A6470_inl1(wf, 0) = 0;
                func_002A6470_inl1(wf, 4) = 0;
                func_002A6470_inl1(wf, 8) = 0;
                wf[0xC] = 0;
                wf[0xD] = 0;
                wf[0x18] = 0;
                wf[0x19] = 0;
                func_002A6470_inl1(wf, 0x10) = 0;
                func_002A6470_inl1(wf, 0x14) = 0;
                func_002A6470_inl1(wf, 0) = func_002A6470_inl1(fmt, 0);
                _rwaWaveDefFormatCopyToWaveFormat((void *)((RwUInt32 *)func_002A6470_inl1(misc, 4))[k], wf, 0);
                if (_RwaStreamContainerPreinstanceCodecs(fmt, wf, i, codecIntf, codecs, chain, cs, 0)) {
                    done = 1;
                    break;
                }
            }
        } while (!done);
        if (!done) {
            RwaLListEmpty(codecs);
            return 0;
        }
        func_002A6470_inl1((RwUInt8 *)chain->link.next - 0x4C, 0x44) = (unsigned int)root;
        RwUInt8 *last = (RwUInt8 *)chain->link.prev - 0x4C;
        RwUInt8 *e = (RwUInt8 *)func_002934C0(0x54, 0x3080F);
        if (e) {
            func_002A6470_inl1(e, 0x4C) = (unsigned int)chain->link.next;
            func_002A6470_inl1(e, 0x50) = (unsigned int)chain;
            chain->link.next->prev = (RwLLLink *)(e + 0x4C);
            chain->link.next = (RwLLLink *)(e + 0x4C);
            memcpy(e + 0xC, wf, 0x1C);
            func_002A6470_inl1(e, 4) = (unsigned int)rend;
            func_002A6470_inl1(e, 8) = (unsigned int)rendIntf;
            func_002A6470_inl1(e, 0x48) = i;
            func_002A6470_inl1(e, 0x44) = (unsigned int)last;
            func_002A6470_inl1(e, 0) = 0;
        }
        if (!e) {
            RwaLListEmpty(codecs);
            func_002A3B20_inl2(chain);
            return 0;
        }
        for (RwLLLink *l = chain->link.next; l != &chain->link;) {
            RwLLLink *next = l->next;
            RwUInt8 *x = (RwUInt8 *)l - 0x4C;
            l->prev->next = next;
            l->next->prev = l->prev;
            l = next;
            func_002A6470_inl1(x, 0x50) = 0;
            func_002A6470_inl1(x, 0x4C) = 0;
            func_002A6470_inl1(x, 0x4C) = (unsigned int)list->link.next;
            func_002A6470_inl1(x, 0x50) = (unsigned int)list;
            list->link.next->prev = (RwLLLink *)(x + 0x4C);
            list->link.next = (RwLLLink *)(x + 0x4C);
        }
    }
    return 1;
}

// Builds the container's render chains: a root (a fan-out node for more than one stream, else the source itself, in
// a root entry on the stack), one renderer per stream format (_RwaStreamContainerPreinstanceRenderers, else a codec
// chain from _RwaStreamContainerPreinstanceChains), then every chain is created from its top: each entry's object
// (renderers through _RwaStreamContainerInstanceRenderer, sorted by format index into the chain list +0x20; codecs
// with the formats of their entry, into +0x18), connected to its parent (on the root's output for its format), and
// the chain's entries freed. Info flags: 1 a renderer without a rate list, 3 two different stream rates; +0x40 / +0x44
// the highest stream rate and the smallest rate list. 0 on a failure (the chain entries freed; the supported renderer
// list leaks when it cannot be built [C 0x2A3C54], as in retail).
// 0x002A3B20
extern "C" RwBool _RwaStreamContainerInstantiateRenderChains(RwUInt8 *cs, RwUInt8 *info)
{
    B3_RETAIL_FRAME(0x1B0);
    RwUInt32 *cp = B3_FRAME_LOCAL(RwUInt32, 0xE0);            // a codec's creation parameters (to 0x140)
    RwUInt8 *rootEntry = B3_FRAME_LOCAL(RwUInt8, 0x140);      // the root entry when there is no fan-out (0x54 bytes)
    RwaLLNode *renderers = B3_FRAME_LOCAL(RwaLLNode, 0x198);  // the supported renderers
    RwLinkList *chain = B3_FRAME_LOCAL(RwLinkList, 0x1A8);    // every chain entry
    func_002A6470_inl1(cs, 0x40) = 0;
    func_002A6470_inl1(cs, 0x44) = 0x20;
    RwUInt8 *fmts = (RwUInt8 *)func_002A6470_inl1(cs, 0xC);
    if (!fmts)
        return 0;
    func_002A6470_inl1(info, 4) = func_002A6470_inl1(fmts, 0x14);
    chain->link.next = &chain->link;
    chain->link.prev = &chain->link;
    RwUInt8 *root;
    if (func_002A6470_inl1(info, 4) >= 2) {
        RwaObjInterfaceDef *nodeIntf = RwaObjInterfaceDefFindByUUID(&rwaSTREAMNODEINTERFACEID);
        RwaObjDef *fanOut = _rwaObjDefFindByUUID(0, &rwaSTREAMFANOUTID, 0);
        RwUInt8 *e = (RwUInt8 *)func_002934C0(0x54, 0x3080F);
        if (e) {
            func_002A6470_inl1(e, 0x4C) = (unsigned int)chain->link.next;
            func_002A6470_inl1(e, 0x50) = (unsigned int)chain;
            chain->link.next->prev = (RwLLLink *)(e + 0x4C);
            chain->link.next = (RwLLLink *)(e + 0x4C);
            func_002A6470_inl1(e, 4) = (unsigned int)fanOut;
            func_002A6470_inl1(e, 8) = (unsigned int)nodeIntf;
            func_002A6470_inl1(e, 0x48) = 0xFFFFFFFF;
            func_002A6470_inl1(e, 0x44) = 0;
            func_002A6470_inl1(e, 0) = 0;
        }
        if (!e)
            return 0;
        root = e;
    } else {
        func_002A6470_inl1(rootEntry, 0x44) = 0;
        func_002A6470_inl1(rootEntry, 8) = (unsigned int)RwaObjInterfaceDefFindByUUID(&rwaSTREAMSOURCEINTERFACEID);
        root = rootEntry;
        func_002A6470_inl1(rootEntry, 0) = func_002A6470_inl1(cs, 0x14);
        func_002A6470_inl1(rootEntry, 4) = func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0x14), 0);
        func_002A6470_inl1(rootEntry, 0x48) = 0xFFFFFFFF;
        for (RwUInt32 off = 0xC; off <= 0x28; off += 0x1C) {   // its two wave formats cleared
            func_002A6470_inl1(rootEntry, off + 0) = 0;
            func_002A6470_inl1(rootEntry, off + 4) = 0;
            func_002A6470_inl1(rootEntry, off + 8) = 0;
            rootEntry[off + 0xC] = 0;
            rootEntry[off + 0xD] = 0;
            rootEntry[off + 0x18] = 0;
            rootEntry[off + 0x19] = 0;
            func_002A6470_inl1(rootEntry, off + 0x10) = 0;
            func_002A6470_inl1(rootEntry, off + 0x14) = 0;
        }
        func_002A6470_inl1(rootEntry, 0x50) = 0;
        func_002A6470_inl1(rootEntry, 0x4C) = 0;
    }
    RwaObjInterfaceDef *rendIntf = RwaObjInterfaceDefFindByUUID(&rwaSTREAMRENDERERINTERFACEID);
    RwaObjInterfaceDef *codecIntf = RwaObjInterfaceDefFindByUUID(&rwaSTREAMCODECINTERFACEID);
    renderers->prev = renderers;
    renderers->next = renderers;
    renderers->data = 0;
    if (!_rwaObjDefBuildSupportedList(0, rendIntf, renderers))
        return 0;
    if (!_RwaStreamContainerPreinstanceRenderers(cs, (unsigned int)rendIntf, (unsigned int)root, (RwUInt8 *)chain,
                                                 (RwUInt8 *)renderers)) {
        RwaLListEmpty(renderers);
        return 0;
    }
    if (!_RwaStreamContainerPreinstanceChains(cs, rendIntf, codecIntf, root, chain, renderers)) {
        func_002A3B20_inl2(chain);
        RwaLListEmpty(renderers);
        return 0;
    }
    RwaLListEmpty(renderers);
    // the creation parameter size, the stream rates and the renderers' rate lists over every entry
    RwUInt32 paramSize = 0, maxRate = 0, minList = 0xFFFFFFFF, rate0 = 0;
    RwUInt8 *sf = (RwUInt8 *)func_002A6470_inl1(fmts, 0x18);
    for (RwLLLink *l = chain->link.next; l != &chain->link; l = l->next) {
        RwUInt8 *e = (RwUInt8 *)l - 0x4C;
        if (func_002A6470_inl1(e, 8) == (unsigned int)rendIntf) {
            RwUInt32 list = func_002A6470_inl1((RwUInt8 *)((RwaObjDef *)func_002A6470_inl1(e, 4))->miscData, 0xC);
            if (!list) {
                func_002A6470_inl1(info, 0) |= 1;
            } else {
                RwUInt32 r = func_002A6470_inl1((void *)func_002A6470_inl1(sf + func_002A6470_inl1(e, 0x48) * 40, 0x14), 0);
                if (maxRate < r)
                    maxRate = r;
                if (list < minList)
                    minList = list;
            }
            RwUInt32 r = func_002A6470_inl1((void *)func_002A6470_inl1(sf + func_002A6470_inl1(e, 0x48) * 40, 0x14), 0);
            if (rate0 < r) {
                if (rate0)
                    func_002A6470_inl1(info, 0) |= 3;
                RwUInt8 *sf2 = (RwUInt8 *)func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x18);
                rate0 = func_002A6470_inl1((void *)func_002A6470_inl1(sf2 + func_002A6470_inl1(e, 0x48) * 40, 0x14), 0);
            }
        }
        RwUInt32 sz = ((RwaObjDef *)func_002A6470_inl1(e, 4))->createParamSize;
        if (paramSize < sz)
            paramSize = sz;
    }
    func_002A6470_inl1(cs, 0x44) = minList;
    func_002A6470_inl1(cs, 0x40) = maxRate;
    RwUInt32 *params = (RwUInt32 *)func_002934C0(paramSize, 0x1080F);
    if (!params) {
        func_002A3B20_inl2(chain);
        return 0;
    }
    if (root != rootEntry) {
        // the fan-out node, fed by the source
        if (!RwaObjDefGetDefaultCreationParams((RwaObjDef *)func_002A6470_inl1(root, 4), params)) {
            func_00293440(params);
            func_002A3B20_inl2(chain);
            return 0;
        }
        func_002A60F0_inl1((RwInt32 *)(cs + 4), 1);
        params[2] = func_002A6470_inl1(cs, 0xC);
        params[3] = func_002A6470_inl1(cs, 0x10);
        params[4] = (unsigned int)(cs + 0x28);
        func_002A6470_inl1(root, 0) = (unsigned int)RwaObjCreate((RwaObjDef *)func_002A6470_inl1(root, 4), 0, 4, params,
                                                                 func_002A6470_inl1(cs, 0x50),
                                                                 func_002A6470_inl1(cs, 0x58));
        RwaObj *fan = (RwaObj *)func_002A6470_inl1(root, 0);
        if (!fan) {
            func_00293440(params);
            func_002A3B20_inl2(chain);
            return 0;
        }
        func_002A3B20_inl1(cs, 0x18, fan);
        func_002AD480((RwaObj *)func_002A6470_inl1(cs, 0x14), 0, (RwaObj *)func_002A6470_inl1(root, 0), 0);
    }
    func_002A6470_inl1(cs, 0x60) = (unsigned int)(cs + 0x60);
    func_002A6470_inl1(cs, 0x64) = (unsigned int)(cs + 0x60);
    for (;;) {
        // the next renderer entry; its chain is created from the entry below the first one that has an object
        RwUInt8 *rendEntry = 0;
        for (RwLLLink *l = chain->link.next; l != &chain->link; l = l->next) {
            if (func_002A6470_inl1((RwUInt8 *)l - 0x4C, 8) == (unsigned int)rendIntf) {
                rendEntry = (RwUInt8 *)l - 0x4C;
                break;
            }
        }
        if (!rendEntry)
            break;
        RwUInt8 *below = 0;
        RwUInt8 *e;
        do {
            e = rendEntry;
            while (e) {
                if (func_002A6470_inl1(e, 0)) {
                    e = below;
                    break;
                }
                below = e;
                e = (RwUInt8 *)func_002A6470_inl1(e, 0x44);
            }
            if (!RwaObjDefGetDefaultCreationParams((RwaObjDef *)func_002A6470_inl1(e, 4), params)) {
                func_00293440(params);
                func_002A3B20_inl2(chain);
                return 0;
            }
            params[2] = func_002A6470_inl1(cs, 0xC);
            params[4] = (unsigned int)(cs + 0x28);
            if (func_002A6470_inl1(e, 8) == (unsigned int)rendIntf) {
                // a renderer: a codec above it hands it the codec's output format for the creation
                RwUInt8 *s = (RwUInt8 *)(func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x18) +
                                         func_002A6470_inl1(e, 0x48) * 40);
                RwUInt32 savedFmt = 0, savedSamples = 0, savedFrame = 0;
                RwBool fromCodec =
                    func_002A6470_inl1((void *)func_002A6470_inl1(e, 0x44), 8) == (unsigned int)codecIntf;
                if (fromCodec) {
                    savedFmt = func_002A6470_inl1(s, 0x14);
                    savedSamples = func_002A6470_inl1(s, 0xC);
                    savedFrame = *(RwUInt16 *)(s + 0x1A);
                    func_002A6470_inl1(s, 0x14) = (unsigned int)(e + 0xC);
                    func_002A6470_inl1(s, 0xC) = RwaWaveFormatGetSamplesPerAudioFrame((RwUInt8 *)func_002A6470_inl1(s, 0x14));
                    *(RwUInt16 *)(s + 0x1A) = (RwUInt16)RwaWaveFormatGetFrameSize((RwUInt8 *)func_002A6470_inl1(s, 0x14));
                }
                func_002A6470_inl1(e, 0) = (unsigned int)_RwaStreamContainerInstanceRenderer(cs, e, (unsigned int)s, params);
                if (func_002A6470_inl1((void *)func_002A6470_inl1(e, 0x44), 8) == (unsigned int)codecIntf) {
                    func_002A6470_inl1(s, 0x14) = savedFmt;
                    *(RwUInt16 *)(s + 0x1A) = (RwUInt16)savedFrame;
                    func_002A6470_inl1(s, 0xC) = savedSamples;
                }
                RwaObj *obj = (RwaObj *)func_002A6470_inl1(e, 0);
                if (!obj) {
                    func_00293440(params);
                    func_002A3B20_inl2(chain);
                    return 0;
                }
                func_002A6470_inl1(obj, 0x64) = func_002A6470_inl1(e, 0x48);
                // into the chain list after the last chain of a lower or equal format index
                RwUInt32 first = func_002A6470_inl1(cs, 0x20);
                RwUInt8 *after = 0;
                for (RwUInt8 *l = (RwUInt8 *)first; l != cs + 0x20; l = (RwUInt8 *)func_002A6470_inl1(l, 0)) {
                    if (func_002A6470_inl1(e, 0x48) < func_002A6470_inl1(l, 0x20))
                        break;
                    after = l;
                }
                if (after) {
                    func_002A6470_inl1(obj, 0x44) = func_002A6470_inl1(after, 0);
                    func_002A6470_inl1(obj, 0x48) = (unsigned int)after;
                    func_002A6470_inl1((void *)func_002A6470_inl1(after, 0), 4) = (unsigned int)obj + 0x44;
                    func_002A6470_inl1(after, 0) = (unsigned int)obj + 0x44;
                } else {
                    func_002A6470_inl1(obj, 0x44) = first;
                    func_002A6470_inl1(obj, 0x48) = (unsigned int)(cs + 0x20);
                    func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0x20), 4) = (unsigned int)obj + 0x44;
                    func_002A6470_inl1(cs, 0x20) = (unsigned int)obj + 0x44;
                }
            } else {
                // a codec: its input / output formats from the entry
                RwaObjDef *def = (RwaObjDef *)func_002A6470_inl1(e, 4);
                RwUInt32 index = func_002A6470_inl1(e, 0x48);
                RwaObjDefGetDefaultCreationParams(def, cp);
                cp[2] = func_002A6470_inl1(cs, 0xC);
                cp[0x16] = func_002A6470_inl1(cs, 0x10);
                cp[0x15] = func_002A6470_inl1(
                    (void *)(func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x18) + index * 40), 0x20);
                memcpy(cp + 7, e + 0xC, 0x1C);
                memcpy(cp + 0xE, e + 0x28, 0x1C);
                func_002A6470_inl1(e, 0) = (unsigned int)RwaObjCreate(def, 0, 4, cp, func_002A6470_inl1(cs, 0x50),
                                                                      func_002A6470_inl1(cs, 0x58));
                RwaObj *obj = (RwaObj *)func_002A6470_inl1(e, 0);
                if (!obj) {
                    func_00293440(params);
                    func_002A3B20_inl2(chain);
                    return 0;
                }
                func_002A3B20_inl1(cs, 0x18, obj);
            }
            RwUInt8 *parent = (RwUInt8 *)func_002A6470_inl1(e, 0x44);
            func_002AD480((RwaObj *)func_002A6470_inl1(parent, 0), parent == root ? func_002A6470_inl1(e, 0x48) : 0,
                          (RwaObj *)func_002A6470_inl1(e, 0), 0);
        } while (e != rendEntry);
        // the chain is made: its entries up to the root go
        for (RwUInt8 *x = rendEntry; x != root;) {
            RwUInt8 *up = (RwUInt8 *)func_002A6470_inl1(x, 0x44);
            if (func_002A6470_inl1(x, 0x4C)) {
                func_002A6470_inl1((void *)func_002A6470_inl1(x, 0x50), 0) = func_002A6470_inl1(x, 0x4C);
                func_002A6470_inl1((void *)func_002A6470_inl1(x, 0x4C), 4) = func_002A6470_inl1(x, 0x50);
            }
            func_00293440(x);
            x = up;
        }
    }
    // every chain's +0x64 (its format index while sorting) becomes 1
    for (RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(cs, 0x20); l != cs + 0x20; l = (RwUInt8 *)func_002A6470_inl1(l, 0))
        func_002A6470_inl1(l, 0x20) = 1;
    func_00293440(params);
    func_002A3B20_inl2(chain);
    return 1;
}

// A chain entry (0x54 bytes, linked first on `chain` at +0x4C): +0xC / +0x28 copies of the two wave formats (when
// given), +4 the codec definition, +8 its interface, +0x44 the next entry down the chain, +0x48 the stream format index.
// 0 when the allocation fails.
static RwUInt8 *func_002A4BF0_inl1(RwLinkList *chain, RwUInt8 *wf, RwUInt8 *target, RwaObjDef *def, RwUInt32 intf,
                                   RwUInt32 index, RwUInt32 inner)
{
    RwUInt8 *e = (RwUInt8 *)func_002934C0(0x54, 0x3080F);
    if (!e)
        return 0;
    func_002A6470_inl1(e, 0x4C) = (unsigned int)chain->link.next;
    func_002A6470_inl1(e, 0x50) = (unsigned int)chain;
    chain->link.next->prev = (RwLLLink *)(e + 0x4C);
    chain->link.next = (RwLLLink *)(e + 0x4C);
    if (wf)
        memcpy(e + 0xC, wf, 0x1C);
    if (target)
        memcpy(e + 0x28, target, 0x1C);
    func_002A6470_inl1(e, 4) = (unsigned int)def;
    func_002A6470_inl1(e, 8) = intf;
    func_002A6470_inl1(e, 0x48) = index;
    func_002A6470_inl1(e, 0x44) = inner;
    func_002A6470_inl1(e, 0) = 0;
    return e;
}

// For stream format `index`, a chain of codec entries from the wave format `wf` to `target` (the renderer's input format),
// built from the supported codec definitions `codecs` (RwaLLNode data = definition) and pushed first onto `chain`. A
// definition already on the chain for this index is skipped. A definition with no format list (misc +4 null) is probed
// by creating and destroying an object with default parameters (the formats, the cs words and the index's record
// fill the 0x5C-byte block at frame +0xD0), and then gets an entry (no *out written on this path [C 0x2A4DA8-0x2A4E08]);
// otherwise each of its formats (misc +4 array of {in, out} format pairs, count +8) whose input matches `wf` either goes
// straight to `target` (one entry), or goes through a recursion with the format `wf` converted to (frame +0x130), the
// entry's +0x44 holding the inner entry the recursion reports in its *out. If the entry cannot be allocated after the
// recursion the chain's entries of this index are freed and 0 is returned. *out (when given) = the new entry.
// The frame word +0x14C is read after the recursion even on the probe path, where retail never wrote it: the guest stack
// word is read as it is (so no extra store) [G: matches retail whatever it held].
// 0x002A4BF0
extern "C" RwBool _RwaStreamContainerPreinstanceCodecs(RwUInt8 *wf, RwUInt8 *target, RwUInt32 index,
                                                       RwaObjInterfaceDef *codecIntf, RwaLLNode *codecs,
                                                       RwLinkList *chain, RwUInt8 *cs, RwUInt32 *out)
{
    B3_RETAIL_FRAME(0x150);
    RwUInt32 *pp = B3_FRAME_LOCAL(RwUInt32, 0xD0);        // default creation parameters (0x5C bytes + ...)
    RwUInt8 *lwf = B3_FRAME_LOCAL(RwUInt8, 0x130);        // a wave format (0x1C bytes)
    RwUInt32 *innerOut = B3_FRAME_LOCAL(RwUInt32, 0x14C); // the recursion's *out
    for (RwaLLNode *c = codecs->next; c != codecs; c = c->next) {
        RwaObjDef *def = (RwaObjDef *)c->data;
        RwUInt8 *misc = (RwUInt8 *)def->miscData;
        RwBool have = 0;
        for (RwLLLink *l = chain->link.next; l != &chain->link; l = l->next) {
            if (func_002A6470_inl1((RwUInt8 *)l - 0x4C, 4) == (unsigned int)def &&
                func_002A6470_inl1((RwUInt8 *)l - 0x4C, 0x48) == index) {
                have = 1;
                break;
            }
        }
        if (have)
            continue;
        if (!func_002A6470_inl1(misc, 4)) {
            RwaObjDefGetDefaultCreationParams(def, pp);
            pp[2] = func_002A6470_inl1(cs, 0xC);
            pp[0x16] = func_002A6470_inl1(cs, 0x10);
            pp[0x15] = func_002A6470_inl1(
                (void *)(func_002A6470_inl1((void *)func_002A6470_inl1(cs, 0xC), 0x18) + index * 40), 0x20);
            memcpy((RwUInt8 *)pp + 0x1C, wf, 0x1C);
            memcpy((RwUInt8 *)pp + 0x38, target, 0x1C);
            RwaObj *probe = RwaObjCreate(def, 0, 4, pp, func_002A6470_inl1(cs, 0x50), func_002A6470_inl1(cs, 0x58));
            if (!probe)
                continue;
            RwaObjDestroy(probe, func_002A6470_inl1(cs, 0x54), func_002A6470_inl1(cs, 0x58));
            return func_002A4BF0_inl1(chain, wf, target, def, (unsigned int)codecIntf, index, 0) != 0;
        }
        for (RwUInt32 fp = 0; fp < func_002A6470_inl1(misc, 8); fp++) {
            RwUInt32 *e = (RwUInt32 *)((RwUInt32 *)func_002A6470_inl1(misc, 4))[fp];
            if (!RwaWaveDefFormatWaveFormatCompare((void *)e[0], wf))
                continue;
            if (RwaWaveDefFormatWaveFormatCompare((void *)e[1], target)) {
                RwUInt8 *n = func_002A4BF0_inl1(chain, wf, target, def, (unsigned int)codecIntf, index, 0);
                if (!n)
                    return 0;
                if (out)
                    *out = (unsigned int)n;
                return 1;
            }
            RwUInt32 rate = func_002A6470_inl1(wf, 0);
            func_002A6470_inl1(lwf, 4) = 0;
            func_002A6470_inl1(lwf, 8) = 0;
            lwf[0xC] = 0;
            lwf[0xD] = 0;
            lwf[0x18] = 0;
            func_002A6470_inl1(lwf, 0) = rate;
            lwf[0x19] = 0;
            func_002A6470_inl1(lwf, 0x10) = 0;
            func_002A6470_inl1(lwf, 0x14) = 0;
            _rwaWaveDefFormatCopyToWaveFormat((void *)e[1], lwf, 1);
            if (!_RwaStreamContainerPreinstanceCodecs(lwf, target, index, codecIntf, codecs, chain, cs, innerOut))
                continue;
            RwUInt32 inner = *innerOut;
            RwUInt8 *n = func_002A4BF0_inl1(chain, wf, target, def, (unsigned int)codecIntf, index, inner);
            if (n) {
                if (out)
                    *out = (unsigned int)n;
                return 1;
            }
            // allocation failed: every entry of this index on the chain is freed
            RwLLLink *l = chain->link.next;
            while (l != &chain->link) {
                RwUInt8 *x = (RwUInt8 *)l - 0x4C;
                l = l->next;
                if (func_002A6470_inl1(x, 0x48) != index)
                    continue;
                RwLLLink *xl = (RwLLLink *)(x + 0x4C);
                if (xl->next) {
                    xl->prev->next = xl->next;
                    xl->next->prev = xl->prev;
                }
                func_00293440(x);
            }
            return 0;
        }
    }
    return 0;
}

// Destroys the container's nodes: first every render chain (from its end upstream, each unlinked from the container's
// list at +0x44 and destroyed with the container's two allocator words +0x54 / +0x58), then every other node on the list
// at +0x18. The source (+0x14) is not cleared. Second argument unused.
// 0x002A6020
extern "C" void _rwaStreamContainerStateDestroy(RwUInt8 *cs, void *info)
{
    RwaObj *n = _rwaStreamContainerEnumRenderChains(cs, (UnkFn_002A5F40)func_002A5F10, (unsigned int)cs);
    while (n) {
        RwaObj *up = (RwaObj *)func_002A6470_inl1(n, 0x50);
        func_002A5570_inl2(cs, n);
        n = up;
    }
    RwUInt8 *l = (RwUInt8 *)func_002A6470_inl1(cs, 0x18);
    while (l != cs + 0x18) {
        RwaObj *o = func_002A5F40_inl1(l);
        l = (RwUInt8 *)func_002A6470_inl1(l, 0);
        func_002A5570_inl2(cs, o);
    }
}
