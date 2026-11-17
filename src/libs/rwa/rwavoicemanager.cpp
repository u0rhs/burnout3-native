// rwavoicemanager.o (librwacore.a) -- the virtual voice manager: virtual voices sorted by weight, real (hardware) voices
// given to the heaviest ones each update. Retail 0x294E20-0x295C78 [C E map]. C port written from retail's code
// (audio1); every function is proven by the checker on live captures.
//
// Lists [C]: _freeList holds stopped voices, _usedList the started ones in ascending weight (head = lightest). Each
// update walks _usedList from the head: while the real voices the walked voices take stay below
// _numVirtualRealVoices - _numRealVoices (the excess), a voice loses its real voice; every later one gets one.
#include "rwa/rwacore.h"
#include "rwa/rwavirtualvoice.h"

extern RwUInt32 D_004E2B10;                 // _moduleOpen of rwavoicemanager.o [L name, retail_data_names.csv]
extern RwBool _registered;
extern RwaObjHandle *_output;               // the output object's handle
extern void *_listener;                     // the output's listener (output parameter 1)
extern RwUInt32 _numRealVoices;
extern RwInt32 _numVirtualVoices;
extern RwInt32 _numVirtualRealVoices;       // real voices the started virtual voices would take
extern RwUInt32 _rwaVoiceManagerTimeStamp;  // milliseconds
extern RwUInt32 D_004E2B30;                 // lasttime$49 [L name]: the timer value of the last time-stamp update
extern RwaObjHandle **_realVoice;           // handles of the real voices
extern RwLLLink _usedList;
extern RwLLLink _freeList;
extern struct RwaUUID rwaSPU2OUTPUTID;
extern "C" RwInt32 RwaObjHandleGetParamInt32(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwBool *ok);
extern "C" RwUInt32 RwaObjHandleGetParamUInt32(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwBool *ok);
extern "C" void *RwaObjHandleGetParamPointer(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwBool *ok);
extern "C" RwaObjHandle *RwaObjHandleSetParamPointer(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, void *value);
extern "C" RwaObjHandle *RwaObjHandleSetParamReal(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwReal value);
extern "C" RwaObjHandle *RwaObjHandleSetParamUInt32(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwUInt32 value);
extern "C" RwaObjHandle *RwaObjHandleSetParamInt32(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, RwInt32 value);
extern "C" RwaObjHandle *RwaObjHandleSetParamData(RwaObjHandle *handle, RwUInt32 param, RwUInt32 type, void *data);
extern "C" void _rwaListenerManagerCalculateVoiceAttributes(void *listener, void *voice, UnkStruct_002AE520_in *in,
                                                               UnkStruct_002AE520_out *out, RwUInt32 p4, RwUInt32 p5);
extern "C" RwUInt32 RwaWaveFormatGetSamples(RwaWaveFormat *format, RwUInt32 length);
extern "C" RwUInt32 RwaTimerGetValue(void);
extern "C" RwUInt32 RwaTimerGetFrequency(void);
extern "C" void *RwaVirtualVoiceRegisterUsingInterface(void);
extern "C" void *RwaVirtualVoiceUnregister(void);
extern "C" void _RwaVoiceManagerStopVoice(RwaVirtualVoiceState *v);
extern "C" void _RwaVoiceManagerReInsertVoice(RwaVirtualVoiceState *v);

// Real-voice parameters the manager sets (the SPU2 voice interface's input indices) [C call sites below]:
// 0 position (data), 1 velocity (data), 4 min distance, 5 max distance, 6 frequency, 7 gain (0-1), 8 play (1/0),
// 9 play position, 0xA loop, 0xB wave, 0xC pan, 0xD reverb send (0-1), 0xE gain array.

// mode 0 = start (re-start if already in the used list), 1 = stop, 2 = re-sort if started.
// 0x00294E20
extern "C" void RwaVoiceManagerUpdateVoice(RwaVirtualVoiceState *v, RwUInt32 mode)
{
    if (mode == 2) {
        if (v->Flags & 1)
            _RwaVoiceManagerReInsertVoice(v);
    } else if (mode == 0) {
        if (v->Flags & 1)
            _RwaVoiceManagerStopVoice(v);
        _RwaVoiceManagerReInsertVoice(v);
        v->NumRealVoices = 1;
        v->Flags |= 2;
        v->WaveStartPosition = 0;
        v->NumRealVoices = v->Wave->format.noChannels;
        if ((v->Wave->flags & 2) != 0) {
            // a 3D voice on an SPU2 output in output mode 4 takes two real voices [L: Dolby Pro Logic pairs]
            if (RwaObjHandleGetParamInt32(_output, 3, 2, 0) == 4 &&
                _output->obj->definition->uniqueID.uuid == &rwaSPU2OUTPUTID)
                v->NumRealVoices = 2;
        }
        _numVirtualVoices++;
        _numVirtualRealVoices += v->NumRealVoices;
    } else if (mode == 1) {
        _RwaVoiceManagerStopVoice(v);
    }
}

// Recomputes the weight and moves the voice to its place in the used list (inserting it if it is not there).
// weight = 255 * (priority/255 * bias/255 + audible * (1 - bias/255)), audible = gain/255 times the listener's volume
// scale for a 3D voice, 1 otherwise; 0 when the gain or the audible volume is 0.
// 0x00294F60
extern "C" void _RwaVoiceManagerReInsertVoice(RwaVirtualVoiceState *v)
{
    v->Weight = 0;
    if (v->Gain > 0) {
        RwReal audible;
        if ((v->Wave->flags & 2) != 0) {
            UnkStruct_002AE520_in in;
            UnkStruct_002AE520_out out;
            in.field_0 = v->Position;
            in.field_C = v->Velocity;
            in.field_18 = v->MinDistance;
            in.field_1C = v->MaxDistance;
            // the voice argument is the state's address - 0xFA0 [C 0x2951D0], seen only by the nearest-mode callback
            _rwaListenerManagerCalculateVoiceAttributes(_listener, (char *)v - 0xFA0, &in, &out, 0, 1);
            v->ListenerRelativePosition = in.field_0;
            v->ListenerRelativeVelocity = in.field_C;
            audible = out.field_0 * (v->Gain / 255.0f);
        } else {
            audible = 1.0f;
        }
        if (!(audible <= 0.0f)) {
            audible = audible * 255.0f;
            RwReal bias = v->Bias / 255.0f;
            RwReal rest = 1.0f - bias;
            RwReal prio = 255.0f * (v->Priority / 255.0f);
            v->Weight = (RwUInt8)(RwUInt32)(prio * bias + audible * rest);
        }
    }
    if (v->Flags & 1) {
        // already listed: walk from the voice towards its place, comparing with the neighbours' weights
        RwReal w = v->Weight / 255.0f;
        RwLLLink *at = &v->link;
        for (;;) {
            RwLLLink *next = at->next;
            RwLLLink *prev = at->prev;
            RwReal wn = next == &_usedList ? 2.0f : ((RwaVirtualVoiceState *)next)->Weight / 255.0f;
            RwReal wp = prev == &_usedList ? -1.0f : ((RwaVirtualVoiceState *)prev)->Weight / 255.0f;
            if (!(w <= wn)) {
                at = next;
                continue;
            }
            if (w < wp) {
                at = prev;
                continue;
            }
            break;
        }
        if (at != &v->link) {
            v->link.prev->next = v->link.next;
            v->link.next->prev = v->link.prev;
            v->link.prev = at->prev;
            v->link.next = at;
            at->prev->next = &v->link;
            at->prev = &v->link;
        }
    } else {
        // from the free list: before the first heavier voice
        v->link.prev->next = v->link.next;
        v->link.next->prev = v->link.prev;
        RwLLLink *at = _usedList.next;
        while (at != &_usedList) {
            if ((RwInt32)v->Weight < (RwInt32)((RwaVirtualVoiceState *)at)->Weight)
                break;
            at = at->next;
        }
        v->link.prev = at->prev;
        v->link.next = at;
        at->prev->next = &v->link;
        at->prev = &v->link;
    }
    v->Flags |= 1;
}

// Stops a started voice: its real voice (if any) stops and drops its wave and gain array; the voice goes to the head
// of the free list with its position back at the start position.
// 0x00295390
extern "C" void _RwaVoiceManagerStopVoice(RwaVirtualVoiceState *v)
{
    if (!(v->Flags & 1))
        return;
    v->link.prev->next = v->link.next;
    v->link.next->prev = v->link.prev;
    RwaObjHandle *rv = v->RealVoice;
    if (rv) {
        if (RwaObjHandleGetParamInt32(rv, 8, 2, 0))
            RwaObjHandleSetParamInt32(rv, 8, 1, 0);
        RwaObjHandleSetParamPointer(rv, 0xB, 1, 0);
        RwaObjHandleSetParamPointer(rv, 0xE, 1, 0);
        v->RealVoice = 0;
    }
    v->link.next = _freeList.next;
    v->link.prev = &_freeList;
    _freeList.next->prev = &v->link;
    _freeList.next = &v->link;
    _numVirtualVoices--;
    _numVirtualRealVoices -= v->NumRealVoices;
    v->WavePosition = v->WaveStartPosition;
    v->Flags &= 0xFC;
    v->Weight = 0;
    v->NumRealVoices = 0;
}

// 0x002954A0
extern "C" RwBool RwaVoiceManagerRegisterVoice(RwaVirtualVoice *vv)
{
    RwaVirtualVoiceState *v = &vv->state;
    v->link.prev = 0;
    v->link.next = 0;
    v->link.next = _freeList.next;
    v->link.prev = &_freeList;
    _freeList.next->prev = &v->link;
    _freeList.next = &v->link;
    v->Flags &= 0xFE;
    return 1;
}

// The bump allocator RwaVoiceManagerRegister gives the handle creation: returns *cursor, advances it by size.
// 0x00295720
extern "C" RwUInt32 func_00295720(RwUInt32 size, RwUInt32 *cursor)
{
    RwUInt32 p = *cursor;
    *cursor = p + size;
    return p;
}

extern struct RwaUUID rwaOUTPUTINTERFACEID;
extern struct RwaUUID rwaVOICE3DINTERFACEID;
extern "C" RwaObjInterfaceDef *RwaObjInterfaceDefFindByUUID(const struct RwaUUID *uuid);
extern "C" RwaObjHandle *RwaObjHandleCreateUsingObj(RwaObj *obj, RwaObjInterfaceDef *intfDef, void *params);
extern "C" void RwaObjHandleRelease(RwaObjHandle *h, void *info);

// Registers the output object and its `n` real (hardware) voices: one RWA allocation holds the handle table (n words),
// then each handle with its interface (the output's through the output interface, each voice's through the 3D voice
// interface), handed out by the bump allocator func_00295720. Then the output gets the voice table (input 5) and the
// listener is read from it (output 1). 0 on a failure: the handles made so far are released (the allocation itself is
// freed only when the output's handle failed) [C 0x2956A4, 0x2956CC].
// 0x002954E0
extern "C" RwBool RwaVoiceManagerRegister(RwaObj *outputObj, RwaObj **voiceObjs, RwUInt32 n)
{
    B3_RETAIL_FRAME(0xB0);
    // handle-creation parameters [C rwaintf.cpp RwaObjHandleCreateParams]: flags, -, -, allocator, its cursor's address,
    // the handle's memory; the cursor itself; the voice table value {count, table}; all where retail keeps them, since
    // the creation keeps the cursor's address
    RwUInt32 *params = B3_FRAME_LOCAL(RwUInt32, 0x80);
    RwUInt32 *table = B3_FRAME_LOCAL(RwUInt32, 0xA0);
    RwUInt32 *cursor = B3_FRAME_LOCAL(RwUInt32, 0xAC);
    RwaObjInterfaceDef *outDef = RwaObjInterfaceDefFindByUUID(&rwaOUTPUTINTERFACEID);
    RwaObjInterfaceDef *voiceDef = RwaObjInterfaceDefFindByUUID(&rwaVOICE3DINTERFACEID);
    RwUInt32 voiceSize = 0x28 + 8 * voiceDef->noInputParams + 8 * voiceDef->noOutputParams;
    RwUInt32 outSize = 24 * (n + 1) + (0x1C + 8 * outDef->noInputParams + 8 * outDef->noOutputParams);
    char *block = (char *)func_002934C0(4 * n + (voiceSize + outSize), 0x3080E);
    if (!block)
        return 0;
    params[0] = 2;
    params[3] = (unsigned int)func_00295720;
    params[2] = 0;
    params[4] = (unsigned int)cursor;
    *cursor = (unsigned int)(block + 0x18);
    params[1] = 0;
    params[5] = (unsigned int)block;
    _output = RwaObjHandleCreateUsingObj(outputObj, outDef, params);
    if (!_output) {
        func_00293440(block);
        return 0;
    }
    _realVoice = (RwaObjHandle **)*cursor;
    RwUInt32 next = *cursor + 4 * n;
    RwUInt32 i;
    for (i = 0; i < n; i++) {
        *cursor = next + 0x18;
        params[5] = next;
        _realVoice[i] = RwaObjHandleCreateUsingObj(voiceObjs[i], voiceDef, params);
        if (!_realVoice[i]) {
            while (i-- != 0)
                RwaObjHandleRelease(_realVoice[i], 0);
            RwaObjHandleRelease(_output, 0);
            _numRealVoices = 0;
            _output = 0;
            return 0;
        }
        next = *cursor;
    }
    _registered = 1;
    _numRealVoices = n;
    table[0] = n;
    table[1] = (unsigned int)_realVoice;
    RwaObjHandleSetParamPointer(_output, 5, 1, table);
    _listener = RwaObjHandleGetParamPointer(_output, 1, 2, 0);
    return 1;
}

// Per update: give real voices to the heaviest started voices, take them from the rest, and advance the play
// positions of voices without one (by elapsed ms * frequency / 1000; wrapped when looping, else the voice stops at
// the end).
// 0x00295730
extern "C" void _rwaVoiceManagerUpdate(void)
{
    if (!_output)
        return;
    RwInt32 taken = 0;
    RwLLLink *node = _usedList.next;
    while (node != &_usedList) {
        RwaVirtualVoiceState *v = (RwaVirtualVoiceState *)node;
        RwLLLink *next = node->next;
        RwaObjHandle *rv = v->RealVoice;
        if (taken < _numVirtualRealVoices - (RwInt32)_numRealVoices) {
            // too light: no real voice
            RwUInt8 flags = v->Flags;
            if (flags & 0x10) {
                _RwaVoiceManagerStopVoice(v);
                flags = v->Flags;
            } else {
                if (rv) {
                    if (flags & 8)
                        v->WavePosition = v->WaveStartPosition;
                    else
                        v->WavePosition = RwaObjHandleGetParamUInt32(rv, 9, 2, 0);
                    RwaObjHandleSetParamInt32(rv, 8, 1, 0);
                    RwaObjHandleSetParamPointer(rv, 0xB, 1, 0);
                    RwaObjHandleSetParamPointer(v->RealVoice, 0xE, 1, 0);
                    v->LastPosTime = _rwaVoiceManagerTimeStamp;
                    v->RealVoice = 0;
                }
                flags = v->Flags;
            }
            if ((flags & 1) && !(flags & 8)) {
                RwUInt32 samples = RwaWaveFormatGetSamples(&v->Wave->format, v->Wave->format.length);
                RwUInt32 dt = _rwaVoiceManagerTimeStamp - v->LastPosTime;
                v->WavePosition += (RwInt32)((RwReal)dt * v->Frequency / 1000.0f);
                if (v->WavePosition >= samples) {
                    if (v->Flags & 4) {
                        while (v->WavePosition >= samples)
                            v->WavePosition -= samples;
                    } else {
                        _RwaVoiceManagerStopVoice(v);
                        v->RealVoice = 0;
                    }
                }
                v->LastPosTime = _rwaVoiceManagerTimeStamp;
            }
        } else if (!rv) {
            rv = (RwaObjHandle *)RwaObjHandleGetParamPointer(_output, 4, 2, 0);
            v->RealVoice = rv;
            if (rv) {
                RwaObjHandleSetParamPointer(rv, 0xB, 1, v->Wave);
                RwaObjHandleSetParamReal(rv, 6, 1, v->Frequency);
                RwaObjHandleSetParamReal(rv, 7, 1, v->Gain / 255.0f);
                RwaObjHandleSetParamInt32(rv, 0xA, 1, (v->Flags & 4) != 0);
                if (!(v->Flags & 8))
                    RwaObjHandleSetParamUInt32(rv, 9, 1, v->WavePosition);
                RwaObjHandleSetParamReal(rv, 0xC, 1, (RwReal)v->Pan);
                RwaObjHandleSetParamData(rv, 0, 1, &v->Position);
                RwaObjHandleSetParamData(rv, 1, 1, &v->Velocity);
                RwaObjHandleSetParamReal(rv, 4, 1, v->MinDistance);
                RwaObjHandleSetParamReal(rv, 5, 1, v->MaxDistance);
                RwaObjHandleSetParamReal(rv, 0xD, 1, v->ReverbSendGain / 255.0f);
                RwaObjHandleSetParamInt32(rv, 8, 1, 1);
                RwaObjHandleSetParamPointer(rv, 0xE, 1, v->GainArray);
            }
        }
        taken += v->NumRealVoices;
        node = next;
    }
}

// Advances the millisecond time stamp by the timer ticks since the last call (the timer wraps at 2^32).
// 0x00295B30
extern "C" void _rwaVoiceManagerUpdateTimeStamp(void)
{
    RwUInt32 t = RwaTimerGetValue();
    RwUInt32 f = RwaTimerGetFrequency();
    RwUInt32 d;
    if (t < D_004E2B30)
        d = (0xFFFFFFFF - D_004E2B30) + t;
    else
        d = t - D_004E2B30;
    RwReal r = (RwReal)d / (RwReal)f;
    D_004E2B30 = t;
    _rwaVoiceManagerTimeStamp += (RwInt32)(1000.0f * r);
}

// 0x00295C10
extern "C" void _rwaVoiceManagerCloseModule(void)
{
    RwaVirtualVoiceUnregister();
    D_004E2B10 = 0;
}

// 0x00295C30
extern "C" RwBool _rwaVoiceManagerOpenModule(void)
{
    D_004E2B10 = 1;
    _freeList.next = &_freeList;
    _freeList.prev = &_freeList;
    _usedList.next = &_usedList;
    _usedList.prev = &_usedList;
    RwaVirtualVoiceRegisterUsingInterface();
    return 1;
}
