// rwastream.o (librwacore.a) -- streams: the game-facing stream object (a stream container at +0x104 driven by
// commands at +0x174) and its parameter calls. Retail 0x2A28A0-0x2A3728 [C E map]. C port written from retail's code
// (audio1); proven by the checker on live captures where they exist.
//
// The stream object [C offsets at the cited calls; no stabs layout]: +0x40 the stream-position request copy (8 bytes),
// +0x48 its 0x10-byte target copy, +0x58 a segment name copy (0x80), +0xD8 a media sub-block copy (7 words),
// +0xF4 a segment UUID copy, +0x104 the container state, +0x110 the container's format holder, +0x118 the source
// object, +0x16C the container info (+4 = streams), +0x174 the command block, +0x24C the status, +0x310 a media copy
// (6 x 8 bytes), +0x340 cleared at creation.
//
// Every call that touches the container takes _updateThreadAccess when RWA runs its own threads (context 2). The
// setters' v0 is whatever the last callee left and no caller reads it [C the game's call sites], so they are void.
#include "rwa/rwacore.h"

extern RwInt32 _rwaGprogramContext;
extern RwInt32 _updateThreadAccess;
extern RwInt32 _exitThreadSemaphore;
extern RwUInt32 _exitThread;
extern RwLLLink _streamList;
extern RwLinkList _streamAliasInfo;
extern RwaObjDef D_01E7DE10;               // the stream object definition (static struct)
extern RwaObjDefInterfaceInfo D_01E7DDF0;  // its interface info [G type]
extern RwaUUID rwaSTREAMID;
extern RwaUUID rwaSTREAMINTERFACEID;
extern RwaUUID rwaSPU2OUTPUTID;
extern RwaInputParamDef _streamInputParams[6];
extern RwaOutputParamDef _streamOutputParams[10];
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" char *strncpy(char *dst, const char *src, unsigned int n);
extern "C" RwInt32 WaitSema(RwInt32 id);
extern "C" RwInt32 SignalSema(RwInt32 id);
extern "C" RwInt32 DeleteSema(RwInt32 id);
extern "C" RwInt32 *RwaSemaphoreCreate(RwInt32 *sema, RwInt32 init, RwInt32 max);
extern "C" RwUInt32 RwaObjIntGetParamPointer(void *obj, void *intf, RwUInt32 index);   // boot2: as rwastreamnodety.cpp defines it (a guest word in, a guest word out)
extern "C" RwUInt32 __rwaStreamSourceGetSegmentIndexByName(void *list, RwUInt32 name);
extern "C" RwUInt32 __rwaStreamSourceGetSegmentIndexByUUID(void *list, RwUInt32 uuid);
extern "C" RwUInt32 _rwaStreamContainerGetSubStreamVoice(void *cs, void *info, void *req);
extern "C" RwUInt32 _rwaStreamContainerGetStreamPos(void *cs, void *info);
extern "C" void _rwaStreamContainerCommandSetStreamGain(void *cmd, void *info, RwInt32 index, RwReal gain);
extern "C" RwBool _rwaStreamContainerCommandSetPitch(void *cmd, void *info, RwReal pitch);
extern "C" void _rwaStreamContainerCommandSetPlayState(void *cmd, RwUInt32 state);
extern "C" void _rwaStreamContainerCommandSetMedia(void *cmd, RwUInt32 media);
extern "C" void _rwaStreamContainerCommandSetLoopSegment(void *cmd, RwUInt32 segment);
extern "C" void _rwaStreamContainerCommandSetStreamPos(void *cmd, RwUInt32 pos);
extern "C" RwUInt32 _rwaStreamContainerExecute(void *cs);
extern "C" void _rwaStreamContainerStateDestroy(void *cs, void *info);
extern "C" RwaObj *_rwaStreamContainerCreate(void *cs, RwUInt32 p1, RwUInt32 rate, RwUInt32 p3, RwUInt32 p4, RwUInt32 p5);
extern "C" RwaObjDef *_rwaObjDefFindByUUID(RwaObjDef *start, RwaUUID *id, RwaObjDef **p2);
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 p1, RwaObjDef *def, void *intfInfo, void *aliasInfo,
                                      RwUInt32 hint);
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, RwaUUID *id, const char *name);
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaInputParamDef *inputs, RwUInt32 nInputs,
                                       RwaOutputParamDef *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefSetInterface(RwaObjDef *def, RwaObjInterfaceDef *intf);
extern "C" RwaObjDef *RwaObjDefDestroy(RwaObjDef *def);
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefFindByUUID(RwaUUID *id);
extern "C" RwaObj *RwaObjCreate(RwaObjDef *def, RwaObj *p1, RwUInt32 p2, void *p3, RwUInt32 p4, RwUInt32 p5);

static void func_002A2C00_inl1(void)
{
    if (_rwaGprogramContext == 2)
        WaitSema(_updateThreadAccess);
}

static void func_002A2C00_inl2(void)
{
    if (_rwaGprogramContext == 2)
        SignalSema(_updateThreadAccess);
}

// The word at p + off.
static RwUInt32 &func_002A28C0_inl2(RwUInt8 *p, RwUInt32 off)
{
    return *(RwUInt32 *)(p + off);
}

// The pointer word at p + off.
static RwUInt8 *func_002A28C0_inl3(RwUInt8 *p, RwUInt32 off)
{
    return (RwUInt8 *)*(RwUInt32 *)(p + off);
}

// The source's stream description (its parameter field_70, kind 2): +0xC a word func_002A2B10 reports, +0x10 the
// segments (0x20 each: +0 UUID, +4 name, +0xC -> sample count), +0x14 the segment count or 0, +0x18 the format
// (+0xC multiplier, +0x14 -> sub-format (+0 rate, +0xD channels, +0x18 flags), +0x1A u16 divisor).
static RwUInt8 *func_002A28C0_inl1(RwUInt8 *src)
{
    return (RwUInt8 *)RwaObjIntGetParamPointer(src, (void *)func_002A28C0_inl2(src, 0x70), 2);
}

// A sub-stream's voice (the container's sub-stream req[1], walked from whichever end of the list is nearer).
// 0x002A28A0
extern "C" RwUInt32 func_002A28A0(RwUInt8 *stream, void *req)
{
    return _rwaStreamContainerGetSubStreamVoice(stream + 0x104, stream + 0x16C, req);
}

// A segment's length. req: +0 segment (index, or name / UUID by kind), +4 kind (1 name, 2 UUID, 3 index; becomes 3),
// +8 out: the length, +0xC units (1 seconds as a float, 2 samples), +0x10 out: the segment count. The length is
// sample count * field_C / field_1A, divided by the channel count unless sub-format flag 4. An unknown segment
// clears kind, units and count.
// 0x002A28C0
extern "C" void func_002A28C0(RwUInt8 *stream, RwUInt32 *req)
{
    RwUInt32 idx = 0;
    func_002A2C00_inl1();
    RwUInt32 kind = req[1];
    RwUInt8 *src = func_002A28C0_inl3(stream, 0x118);
    if (kind == 3)
        idx = req[0];
    else if (kind == 2)
        idx = __rwaStreamSourceGetSegmentIndexByUUID(src + 0x74, req[0]);
    else if (kind == 1)
        idx = __rwaStreamSourceGetSegmentIndexByName(src + 0x74, req[0]);
    RwUInt8 *p = func_002A28C0_inl1(src);
    RwUInt32 seg = func_002A28C0_inl2(p, 0x10) + idx * 32;
    if (!seg || !func_002A28C0_inl2(p, 0x14)) {
        req[1] = 0;
        req[3] = 0;
        req[4] = 0;
        func_002A2C00_inl2();
        return;
    }
    req[2] = *(RwUInt32 *)func_002A28C0_inl2((RwUInt8 *)seg, 0xC);
    req[4] = func_002A28C0_inl2(p, 0x14);
    req[1] = 3;
    req[0] = idx;
    RwUInt8 *fmt = func_002A28C0_inl3(p, 0x18);
    RwUInt8 *sub = func_002A28C0_inl3(fmt, 0x14);
    RwUInt32 q = (req[2] * func_002A28C0_inl2(fmt, 0xC)) / *(RwUInt16 *)(fmt + 0x1A);
    if (sub[0x18] & 4)
        q = q / 1;
    else
        q = q / sub[0xD];
    req[2] = q;
    if (req[3] != 2 && req[3] == 1) {
        RwReal rate = (RwReal)func_002A28C0_inl2(sub, 0);
        RwReal len = (RwReal)req[2];
        *(RwReal *)&req[2] = len / rate;
    }
    func_002A2C00_inl2();
}

// The source description's field_C.
// 0x002A2B10
extern "C" RwUInt32 func_002A2B10(RwUInt8 *stream)
{
    func_002A2C00_inl1();
    RwUInt32 r = func_002A28C0_inl2(func_002A28C0_inl1(func_002A28C0_inl3(stream, 0x118)), 0xC);
    func_002A2C00_inl2();
    return r;
}

// Pitch.
// 0x002A2B80
extern "C" void func_002A2B80(RwUInt8 *stream, RwReal pitch)
{
    func_002A2C00_inl1();
    _rwaStreamContainerCommandSetPitch(stream + 0x174, stream + 0x16C, pitch);
    func_002A2C00_inl2();
}

// Gain {stream index or -1, gain}.
// 0x002A2C00
extern "C" void _rwaStreamSetStreamGain(RwUInt8 *stream, RwUInt32 *gain)
{
    func_002A2C00_inl1();
    _rwaStreamContainerCommandSetStreamGain(stream + 0x174, stream + 0x16C, (RwInt32)gain[0], *(RwReal *)&gain[1]);
    func_002A2C00_inl2();
}

// 0x002A2C80
extern "C" RwUInt32 _rwaStreamGetStatus(RwUInt8 *stream)
{
    return *(RwUInt32 *)(stream + 0x24C);
}

// 0x002A2C90
extern "C" void _rwaStreamSetPlayState(RwUInt8 *stream, RwUInt32 state)
{
    func_002A2C00_inl1();
    _rwaStreamContainerCommandSetPlayState(stream + 0x174, state);
    func_002A2C00_inl2();
}

// Media: copied into the stream (6 x 8 bytes at +0x310, its sub-block of 7 words at +0xD8; the copy's +8 then points
// at +0xD8 even when the media had none), then queued. A null media is queued as null.
// 0x002A2D00
extern "C" void _rwaStreamSetMedia(RwUInt8 *stream, RwUInt32 *media)
{
    func_002A2C00_inl1();
    if (media) {
        if (media[2]) {
            RwUInt32 *sub = (RwUInt32 *)media[2];
            RwUInt32 *d = (RwUInt32 *)(stream + 0xD8);
            for (RwInt32 k = 0; k < 7; k++)
                d[k] = sub[k];
        }
        RwUInt32 *d = (RwUInt32 *)(stream + 0x310);
        for (RwInt32 k = 0; k < 6; k++) {
            RwUInt32 a = media[0], b = media[1];
            d[0] = a;
            d[1] = b;
            media += 2;
            d += 2;
        }
        func_002A28C0_inl2(stream, 0x318) = (unsigned int)(stream + 0xD8);
        media = (RwUInt32 *)(stream + 0x310);
    }
    _rwaStreamContainerCommandSetMedia(stream + 0x174, (RwUInt32)media);
    func_002A2C00_inl2();
}

// 0x002A2DE0
extern "C" void _rwaStreamSetLoopSegment(RwUInt8 *stream, RwUInt32 segment)
{
    func_002A2C00_inl1();
    _rwaStreamContainerCommandSetLoopSegment(stream + 0x174, segment);
    func_002A2C00_inl2();
}

// The play position. out: +0 segment (index, or its UUID / name for kind 2 / 1), +4 kind, +8 position (units +0xC:
// 1 = seconds, samples / the format's rate as a float; 2 = samples; else left), from the container's first sub-stream
// position {segment, ?, samples}; 0 / 0 when there is none. The lock is released before the conversion.
// 0x002A2E50
extern "C" void _rwaStreamGetStreamPos(RwUInt8 *stream, RwUInt32 *out)
{
    func_002A2C00_inl1();
    RwUInt32 *pos = (RwUInt32 *)_rwaStreamContainerGetStreamPos(stream + 0x104, stream + 0x16C);
    if (!pos) {
        out[2] = 0;
        out[0] = 0;
        func_002A2C00_inl2();
        return;
    }
    func_002A2C00_inl2();
    RwUInt8 *list = func_002A28C0_inl3(stream, 0x118) + 0x74;
    RwUInt32 units = out[3];
    if (units == 2) {
        out[2] = pos[2];
    } else if (units == 1) {
        RwUInt8 *holder = func_002A28C0_inl3(func_002A28C0_inl3(stream, 0x110), 0x18);
        RwReal rate = (RwReal)func_002A28C0_inl2(func_002A28C0_inl3(holder, 0x14), 0);
        RwReal samples = (RwReal)pos[2];
        *(RwReal *)&out[2] = samples / rate;
    }
    RwUInt32 kind = out[1];
    if (kind == 3)
        out[0] = pos[0];
    else if (kind == 2)
        out[0] = func_002A28C0_inl2(func_002A28C0_inl3(func_002A28C0_inl3(list, 0x124), 0x10) + pos[0] * 32, 0);
    else if (kind == 1)
        out[0] = func_002A28C0_inl2(func_002A28C0_inl3(func_002A28C0_inl3(list, 0x124), 0x10) + pos[0] * 32, 4);
}

// The play position to seek to: pos = {target*, ?}, target = {segment name / UUID / index, kind (1 / 2 / 3), ...}
// (0x10 bytes); the stream keeps copies (the name, up to 0x80, at +0x58; the UUID at +0xF4) and queues them.
// 0x002A3040
extern "C" void _rwaStreamSetStreamPos(RwUInt8 *stream, RwUInt32 *pos)
{
    memcpy(stream + 0x40, pos, 8);
    memcpy(stream + 0x48, (void *)pos[0], 0x10);
    func_002A28C0_inl2(stream, 0x40) = (unsigned int)(stream + 0x48);
    RwUInt32 *t = (RwUInt32 *)pos[0];
    if (t[1] == 1)
        strncpy((char *)(stream + 0x58), (const char *)t[0], 0x80);
    else if (t[1] == 2)
        memcpy(stream + 0xF4, (void *)t[0], 0x10);
    func_002A2C00_inl1();
    _rwaStreamContainerCommandSetStreamPos(stream + 0x174, (RwUInt32)(stream + 0x40));
    func_002A2C00_inl2();
}

// Creation-parameter info: none.
// 0x002A3130
extern "C" RwaObj *func_002A3130(RwaObj *obj, RwUInt32 *out)
{
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    return obj;
}

// 0x002A3150
extern "C" RwaObj *_RwaStreamFinishExecute(RwaObj *obj)
{
    _rwaStreamContainerExecute((RwUInt8 *)obj + 0x104);
    return obj;
}

// 0x002A3180
extern "C" void _RwaStreamDestroy(RwaObj *obj)
{
    func_002A2C00_inl1();
    _rwaStreamContainerStateDestroy((RwUInt8 *)obj + 0x104, (RwUInt8 *)obj + 0x16C);
    func_002A2C00_inl2();
}

// Creation: the container at 48000 Hz with the parameters' three words.
// 0x002A31F0
extern "C" RwaObj *_RwaStreamCreate(RwaObj *obj, RwaObj *p1, RwUInt32 *params)
{
    func_002A28C0_inl2((RwUInt8 *)obj, 0x340) = 0;
    RwaObj *r = _rwaStreamContainerCreate((RwUInt8 *)obj + 0x104, (RwUInt32)p1, 0xBB80, params[0], params[1], params[2]);
    return r ? obj : 0;
}

// 0x002A3240
extern "C" RwaObjDef *RwaStreamUnregister(void)
{
    DeleteSema(_updateThreadAccess);
    DeleteSema(_exitThreadSemaphore);
    return RwaObjDefDestroy(&D_01E7DE10);
}

// The definition (state 0x304 bytes, 0x40-aligned), _streamList empty, the update-access semaphore (1) and the
// thread-exit semaphore (0); a failed second one deletes the first.
// 0x002A3270
extern "C" void _RwaStreamRegisterCommon(RwaObjDef *def)
{
    RwaObjDefAssignID(def, &rwaSTREAMID, 0);
    func_00297C30_inl3(def, 0x304, 0x40);
    def->createFunc = _RwaStreamCreate;
    def->destroyFunc = _RwaStreamDestroy;
    def->finishExecuteFunc = _RwaStreamFinishExecute;
    def->getParamsFunc = func_002A3130;
    def->createParamSize = 0xC;
    _streamList.next = &_streamList;
    _streamList.prev = &_streamList;
    if (!RwaSemaphoreCreate(&_updateThreadAccess, 1, 1))
        return;
    if (RwaSemaphoreCreate(&_exitThreadSemaphore, 0, 1)) {
        _exitThread = 0;
        return;
    }
    DeleteSema(_updateThreadAccess);
}

// 0x002A35A0
extern "C" RwaObjDef *RwaStreamRegisterUsingInterface(void)
{
    RwaObjDef *parent = _rwaObjDefFindByUUID(0, &rwaSPU2OUTPUTID, 0);
    RwaObjDef *def = RwaObjDefCreate(parent, 0xC, &D_01E7DE10, &D_01E7DDF0, &_streamAliasInfo, 0x3080F);
    if (!def)
        return 0;
    _RwaStreamRegisterCommon(def);
    if (!RwaObjDefSetupParams(def, _streamInputParams, 6, _streamOutputParams, 0xA)) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjDefSetInterface(def, RwaObjInterfaceDefFindByUUID(&rwaSTREAMINTERFACEID));
    return def;
}

// 0x002A3670
extern "C" RwaObj *RwaStreamCreate(RwUInt32 p0, RwUInt32 p1, RwUInt32 p2, RwUInt32 p3, RwUInt32 p4)
{
    return RwaObjCreate(&D_01E7DE10, (RwaObj *)p0, p2, (void *)p1, p3, p4);
}

extern "C" RwInt32 EIntr(void);
extern "C" RwInt32 func_00114E60(void);   // DIntr
extern "C" RwInt32 _rwaLockedCountCompare(RwInt32 *count, RwInt32 value);
extern "C" RwUInt32 RwaObjIntGetParamPointer(void *obj, void *intf, RwUInt32 index);   // boot2: as rwastreamnodety.cpp defines it (a guest word in, a guest word out)

// ---- the object-interface parameter accessors (obj, parameter index, value) of the stream object: each returns obj.
// Setters take the update-access lock around the container command [C]. The value pointers are the callers' words.

// Sub-stream voice: the container's sub-stream req[1] (see func_002A28A0).
// 0x002A2320
extern "C" RwaObj *_RwaStreamGetSubStreamVoice(RwUInt8 *stream, RwUInt32 param, RwUInt32 *req)
{
    _rwaStreamContainerGetSubStreamVoice(stream + 0x104, stream + 0x16C, req);
    return (RwaObj *)stream;
}

// Sub-stream info: req[2] = the sub-format index; req[3] = its channel count (1 when its flag 4 is set), 0 for an
// index outside the source description's formats (count +0x14, array +0x18, 0x28 bytes each).
// 0x002A2350
extern "C" RwaObj *_RwaStreamGetSubStreamInfo(RwUInt8 *stream, RwUInt32 param, RwUInt32 *req)
{
    func_002A2C00_inl1();
    RwUInt8 *p = func_002A28C0_inl1(func_002A28C0_inl3(stream, 0x118));
    RwUInt32 idx = req[2];
    RwUInt32 count = func_002A28C0_inl2(p, 0x14);
    RwUInt32 arr = func_002A28C0_inl2(p, 0x18);
    if (idx < count && arr + idx * 40) {
        RwUInt8 *sub = func_002A28C0_inl3((RwUInt8 *)(arr + idx * 40), 0x14);
        req[3] = (sub[0x18] & 4) ? 1 : sub[0xD];
    } else {
        req[3] = 0;
    }
    func_002A2C00_inl2();
    return (RwaObj *)stream;
}

// Segment info: func_002A28C0.
// 0x002A2430
extern "C" RwaObj *_RwaStreamGetSegmentInfo(RwUInt8 *stream, RwUInt32 param, RwUInt32 *req)
{
    func_002A28C0(stream, req);
    return (RwaObj *)stream;
}

// The number of segments (the source description's +0xC) into *out; the store follows the unlock.
// 0x002A2460
extern "C" RwaObj *_RwaStreamGetNoSegments(RwUInt8 *stream, RwUInt32 param, RwUInt32 *out)
{
    func_002A2C00_inl1();
    RwUInt32 n = func_002A28C0_inl2(func_002A28C0_inl1(func_002A28C0_inl3(stream, 0x118)), 0xC);
    func_002A2C00_inl2();
    *out = n;
    return (RwaObj *)stream;
}

// Getter: the pitch word at +0x138.
// 0x002A24F0
extern "C" RwaObj *func_002A24F0(RwUInt8 *stream, RwUInt32 param, RwReal *out)
{
    *out = *(RwReal *)(stream + 0x138);
    return (RwaObj *)stream;
}

// Setter: pitch (the value is read before the lock).
// 0x002A2500
extern "C" RwaObj *_RwaStreamSetPitch(RwUInt8 *stream, RwUInt32 param, RwReal *value)
{
    RwReal pitch = *value;
    func_002A2C00_inl1();
    _rwaStreamContainerCommandSetPitch(stream + 0x174, stream + 0x16C, pitch);
    func_002A2C00_inl2();
    return (RwaObj *)stream;
}

// Getter: the gain of stream out[0] into out[1] (the gains are the words from +0x18C).
// 0x002A2580
extern "C" RwaObj *func_002A2580(RwUInt8 *stream, RwUInt32 param, RwUInt32 *out)
{
    out[1] = func_002A28C0_inl2(stream, 0x18C + out[0] * 4);
    return (RwaObj *)stream;
}

// Setter: gain {stream index or -1, gain}.
// 0x002A25A0
extern "C" RwaObj *_RwaStreamSetStreamGain(RwUInt8 *stream, RwUInt32 param, RwUInt32 *gain)
{
    func_002A2C00_inl1();
    _rwaStreamContainerCommandSetStreamGain(stream + 0x174, stream + 0x16C, (RwInt32)gain[0], *(RwReal *)&gain[1]);
    func_002A2C00_inl2();
    return (RwaObj *)stream;
}

// Getters of the words at +0x254, +0x250 and +0x24C (the status).
// 0x002A2620
extern "C" RwaObj *func_002A2620(RwUInt8 *stream, RwUInt32 param, RwUInt32 *out)
{
    *out = func_002A28C0_inl2(stream, 0x254);
    return (RwaObj *)stream;
}

// 0x002A2630
extern "C" RwaObj *func_002A2630(RwUInt8 *stream, RwUInt32 param, RwUInt32 *out)
{
    *out = func_002A28C0_inl2(stream, 0x250);
    return (RwaObj *)stream;
}

// 0x002A2640
extern "C" RwaObj *func_002A2640(RwUInt8 *stream, RwUInt32 param, RwUInt32 *out)
{
    *out = func_002A28C0_inl2(stream, 0x24C);
    return (RwaObj *)stream;
}

// Setter: play state (the value is read before the lock).
// 0x002A2650
extern "C" RwaObj *_RwaStreamSetPlayState(RwUInt8 *stream, RwUInt32 param, RwUInt32 *value)
{
    RwUInt32 state = *value;
    func_002A2C00_inl1();
    _rwaStreamContainerCommandSetPlayState(stream + 0x174, state);
    func_002A2C00_inl2();
    return (RwaObj *)stream;
}

// Setter: media (the lock is taken first; the copy is _rwaStreamSetMedia's).
// 0x002A26D0
extern "C" RwaObj *_RwaStreamSetMedia(RwUInt8 *stream, RwUInt32 param, RwUInt32 *media)
{
    func_002A2C00_inl1();
    if (media) {
        if (media[2]) {
            RwUInt32 *sub = (RwUInt32 *)media[2];
            RwUInt32 *d = (RwUInt32 *)(stream + 0xD8);
            for (RwInt32 k = 0; k < 7; k++)
                d[k] = sub[k];
        }
        RwUInt32 *d = (RwUInt32 *)(stream + 0x310);
        for (RwInt32 k = 0; k < 6; k++) {
            RwUInt32 a = media[0], b = media[1];
            d[0] = a;
            d[1] = b;
            media += 2;
            d += 2;
        }
        func_002A28C0_inl2(stream, 0x318) = (unsigned int)(stream + 0xD8);
        media = (RwUInt32 *)(stream + 0x310);
    }
    _rwaStreamContainerCommandSetMedia(stream + 0x174, (RwUInt32)media);
    func_002A2C00_inl2();
    return (RwaObj *)stream;
}

// Setter: loop segment (the value is read before the lock).
// 0x002A27C0
extern "C" RwaObj *_RwaStreamSetLoopSegment(RwUInt8 *stream, RwUInt32 param, RwUInt32 *value)
{
    RwUInt32 segment = *value;
    func_002A2C00_inl1();
    _rwaStreamContainerCommandSetLoopSegment(stream + 0x174, segment);
    func_002A2C00_inl2();
    return (RwaObj *)stream;
}

// 0x002A2840
extern "C" RwaObj *_RwaStreamGetStreamPos(RwUInt8 *stream, RwUInt32 param, RwUInt32 *out)
{
    _rwaStreamGetStreamPos(stream, out);
    return (RwaObj *)stream;
}

// 0x002A2870
extern "C" RwaObj *_RwaStreamSetStreamPos(RwUInt8 *stream, RwUInt32 param, RwUInt32 *pos)
{
    _rwaStreamSetStreamPos(stream, pos);
    return (RwaObj *)stream;
}

// A completion callback (a0 unused, a1 = a counted object): its count (+0x3C) goes down by one, with interrupts off
// around it in context 2 (EIntr only when DIntr reported them enabled, 1); when the count then compares equal to 0
// (_rwaLockedCountCompare) it is left alone, otherwise +0x38 is set to 1. No result. In context 2 DIntr's result is used
// by the later check; in any other context the check cannot run, so the uninitialised `was` is never read [G: 0].
// 0x002A36A0
extern "C" void func_002A36A0(RwUInt32 unused, RwUInt8 *o)
{
    RwInt32 was = 0;
    if (_rwaGprogramContext == 2)
        was = func_00114E60();
    func_002A28C0_inl2(o, 0x3C) = func_002A28C0_inl2(o, 0x3C) - 1;
    if (_rwaGprogramContext == 2) {
        if (was == 1)
            EIntr();
    }
    if (!_rwaLockedCountCompare((RwInt32 *)(o + 0x3C), 0))
        func_002A28C0_inl2(o, 0x38) = 1;
}
