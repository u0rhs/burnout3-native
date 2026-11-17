// rwaspu2out.o (librwaspu2.a) -- the SPU2 output: hardware voice allocation, the reverb environment, the per-update
// snapshot of every hardware voice sent to the IOP (opcode 0x14) and the handler of the IOP's requests. Retail
// 0x2B23B0-0x2B41D4 [C E map]. C port written from retail's code (audio1); proven by the checker on live captures
// where they exist (see the audio1 report for which functions wait on the IOP and stay unchecked).
//
// RPC opcodes this object sends (RwaRPCTransfer, rwarpc.o) [C call sites]: 0x04 mixer/IRQ buffer to the IOP,
// 0x11 create (p6 = NumVoices, p7 = reverb cores), 0x12 destroy, 0x13 query IOP memory, 0x14 snapshot (iop =
// DMAStateIOPAddr, ee = DMAStateToIOP, 0x680 bytes, p6 = PacketID), 0x15 send the reply area's EE address (p6),
// 0x16 / 0x17 mixer callback on / off, 0x20 / 0x21 / 0x23 voice+RAM request replies, 0x26 / 0x27 / 0x29 input-area
// replies, 0x2C free-voice pool, 0x30 file interface, 0x43 stream sync reply, 0x4E DMA channel lock.
#include "rwa/rwacore.h"
#include "rwa/rwaspu2.h"

// A voice + sound-RAM request from the IOP (opcodes 0x1E / 0x1F / 0x22), 0x40 bytes [C RwaSPU2OutputCmdHandler
// 0x2B3D28-0x2B3F44].
struct UnkStruct_01E83440 {
    struct UnkStruct_01E83440 *field_0;  // 0x00 itself (its EE address for the IOP)
    RwUInt32 field_4;             // 0x04 the IOP's address of its copy (reply destination)
    unsigned char pad_8[0x4];
    RwUInt32 field_C;             // 0x0C sound RAM bytes wanted
    RwUInt32 field_10;            // 0x10 hardware voices wanted
    RwUInt32 field_14;            // 0x14 core flags (1 core 0 only, 2 core 1 only)
    RwUInt32 field_18[2];         // 0x18 the voices granted, bit masks per core
    RwUInt32 field_20;            // 0x20 the granted sound-RAM address
    RwUInt32 field_24;            // 0x24 the granted sound-RAM handle (0 = none)
    unsigned char pad_28[0x18];
};
// An input-area request from the IOP (opcodes 0x24 / 0x25 / 0x28), 0x40 bytes [C 0x2B3F4C-0x2B4058].
struct UnkStruct_01E83400 {
    struct UnkStruct_01E83400 *field_0;  // 0x00 itself
    RwUInt32 field_4;             // 0x04 the IOP's address of its copy
    unsigned char pad_8[0x8];
    RwUInt32 field_10;            // 0x10 blocks
    RwUInt32 field_14;            // 0x14 result
    unsigned char pad_18[0x28];
};
// The stream sync transaction (opcode 0x42), 0x80 bytes [C 0x2B4060-0x2B40D4].
typedef void (*UnkFn_01E83558)(void *p0, RwUInt32 p1, void *p2, RwUInt32 p3);
struct UnkStruct_01E83540 {
    unsigned char pad_0[0x4];
    RwUInt32 field_4;             // 0x04 6 = run the callback
    unsigned char field_8[0x10];  // 0x08 the callback's first argument
    UnkFn_01E83558 field_18;      // 0x18 callback
    RwUInt32 field_1C;            // 0x1C its second argument
    unsigned char field_20[0x60]; // 0x20 its third argument
};

extern RwaSPU2OutputState *_rwaSPU2OutputState;
extern RwUInt32 D_01E83480[48];            // per hardware voice: 0 free, 1 reserved, else its RwaSPU2Voice
extern RwUInt32 _rwaSPU2UpdateCount;       // the snapshot PacketID
extern RwUInt32 _rwaSPU2OutputReverbCores; // 1 core 0, 2 core 1
extern RwUInt32 _rwaSPU2ResourceBitfield[2];
extern RwInt32 _rwaSPU2_IOPMemFree;
extern RwInt32 _rwaSPU2_IOPMemUsed;
extern RwUInt8 _rwaG_rwaSPU2InputAreaMemoryBitmap;
extern RwUInt32 _spu2streamSyncTransactionIOPEEAddr;
extern RwUInt32 _spu2streamSyncTransactionEEIOPAddr;
extern RwUInt32 D_004E2C7C;                // the IOP's acknowledgement of create / destroy (0x1A sets, 0x12 clears)
extern RwUInt32 D_004E2C80;                // mixer callback installed on the IOP (0x1D clears) [research name
                                           // _objInterfaceRegistered: a conflict, not this use]
extern RwUInt32 D_004E2C8C;                // sent with opcode 0x2C at destroy [research name _freeVoicePool]
extern RwUInt32 D_004E2C90;                // the IOP file interface [research name _freeVoiceNum]
extern RwUInt32 D_004E2C78;                // _objInterfaceRegistered of rwaspu2out.o [L name]
extern UnkStruct_01E83440 D_01E83440;
extern UnkStruct_01E83400 D_01E83400;
extern UnkStruct_01E83540 D_01E83540;
extern RwaEnvironment D_0049FE30;          // the default environment
extern RwReal D_004E06B8;                  // 0.3
extern RwReal D_004E0688;                  // 0.05
extern RwaObjDef D_01E83390;               // the SPU2 output object definition (static struct)
extern RwaObjDefInterfaceInfo D_01E83370;  // its interface info [G type]
extern RwLinkList _spu2outputAliasInfo;
extern RwaObjDef *_rwaGnullObjDef;
extern RwaUUID rwaSPU2OUTPUTID;
extern RwaInputParamDef _spu2outputInputParams[7];
extern RwaOutputParamDef _spu2outputOutputParams[6];
extern "C" void *memcpy(void *dst, const void *src, unsigned int n);
extern "C" RwInt32 func_00114E60(void);    // DIntr
extern "C" RwInt32 EIntr(void);
extern "C" void SyncDCache(void *start, void *end);
extern "C" void FlushCache(RwInt32 mode);
extern "C" RwInt32 GetThreadId(void);
extern "C" RwInt32 SleepThread(void);
extern "C" RwInt32 WakeupThread(RwInt32 id);
extern "C" RwReal func_0011E388(RwReal x, RwReal y);   // powf [G: x^y; called (10, mB / 1000)]
extern "C" void *func_00293430(RwUInt32 n, RwUInt32 size, RwUInt32 hint);   // RWA calloc
extern "C" void RwaRPCTransfer(RwUInt32 iopAddr, void *ee, RwUInt32 size, RwUInt32 op, RwUInt32 unused,
                               RwUInt32 p5, RwUInt32 p6, RwUInt32 p7);
typedef void (*RwaRPCCmdHandlerFunc)(void *userData, RwUInt32 op, RwUInt32 arg0, RwUInt32 arg1, RwInt32 thread);
extern "C" RwInt32 RwaRPCOpen(RwaRPCCmdHandlerFunc handler, void *userData, RwUInt32 priority);
extern "C" void RwaRPCClose(void);
extern "C" void RwaSifCmdInit(void);
extern "C" RwaObj *_rwaObjFinishExecute(RwaObj *obj, RwUInt32 p1);
extern "C" RwUInt32 _rwaSPU2WaveRAMAlloc(RwUInt32 size);
extern "C" void _rwaSPU2WaveRAMFree(RwUInt32 handle);
extern "C" RwUInt32 _rwaSPU2WaveRAMGetAddress(RwUInt32 handle);
extern "C" void _rwaSPU2WaveRAMOpen(void);
extern "C" void _rwaSPU2WaveRAMClose(void);
extern "C" RwInt32 RwaSPU2HWResourceRAMLock(RwUInt32 addr, RwUInt32 size);
extern "C" void RwaSPU2HWResourceRAMUnlock(RwUInt32 addr, RwUInt32 size);
extern "C" RwUInt32 _rwaSPU2InputAreaRAMReserveBlocks(RwUInt32 blocks);
extern "C" RwUInt32 _rwaSPU2InputAreaRAMFreeBlocks(RwUInt32 blocks);
extern "C" RwaObjDef *RwaObjDefCreate(RwaObjDef *parent, RwUInt32 p1, RwaObjDef *def, void *intfInfo, void *aliasInfo,
                                      RwUInt32 hint);
extern "C" RwaObjDef *RwaObjDefAssignID(RwaObjDef *def, RwaUUID *id, const char *name);
extern "C" RwaObjDef *RwaObjDefSetupParams(RwaObjDef *def, RwaInputParamDef *inputs, RwUInt32 nInputs,
                                       RwaOutputParamDef *outputs, RwUInt32 nOutputs);
extern "C" RwaObjDef *RwaObjDefSetInterface(RwaObjDef *def, RwaObjInterfaceDef *intf);
extern "C" RwaObjDef *RwaObjDefDestroy(RwaObjDef *def);
extern "C" RwaObj *RwaObjCreate(RwaObjDef *def, RwaObj *p1, RwUInt32 p2, void *p3, RwUInt32 p4, RwUInt32 p5);
extern "C" RwaObjInterfaceDef *RwaSPU2OutputInterfaceRegister(void);
extern "C" RwaObjInterfaceDef *RwaSPU2OutputInterfaceUnregister(void);
extern "C" void RwaSPU2OutputCmdHandler(RwaSPU2OutputState *st, RwUInt32 op, RwUInt32 arg0, RwUInt32 arg1,
                                        RwInt32 thread);

// Parameter getter: the speaker configuration.
// 0x002B23E0
extern "C" RwaSPU2Output *func_002B23E0(RwaSPU2Output *out, RwUInt32 param, RwInt32 *value)
{
    *value = out->state.DMAState.SpeakerConfig;
    return out;
}

// 0x002B23F0
extern "C" RwaSPU2Output *func_002B23F0(RwaSPU2Output *out, RwUInt32 param, RwInt32 *value)
{
    out->state.DMAState.SpeakerConfig = *value;
    return out;
}

// Parameter getter: the listener.
// 0x002B2470
extern "C" RwaSPU2Output *func_002B2470(RwaSPU2Output *out, RwUInt32 param, void **value)
{
    *value = out->state.Listener;
    return out;
}

// 0x002B2480
extern "C" RwaSPU2Output *func_002B2480(RwaSPU2Output *out, RwUInt32 param, void *value)
{
    out->state.Listener = value;
    return out;
}

// Asks the IOP for the DMA channel and sleeps until its reply (0x4F/... wake the thread).
// 0x002B2550
extern "C" RwInt32 RwaSPU2HWResourceDMAChannelLock(RwUInt32 channel)
{
    RwInt32 tid = GetThreadId();
    RwaRPCTransfer(0, 0, 0, 0x4E, 0, tid, channel, 0);
    return SleepThread();
}

// The per-core masks: the voice's bit set or cleared in all five [C 0x2B2D08-0x2B2DBC, 0x2B2818-0x2B28E4].
static void func_002B2BA0_inl1(RwUInt32 core, RwUInt32 bit, RwBool on)
{
    RwaSPU2DMAState *d = &_rwaSPU2OutputState->DMAState;
    if (on) {
        d->outputMixDryLeft[core] |= bit;
        d->outputMixDryRight[core] |= bit;
        d->outputMixWetLeft[core] |= bit;
        d->outputMixWetRight[core] |= bit;
        d->voiceSwitchMask[core] |= bit;
    } else {
        d->outputMixDryLeft[core] &= ~bit;
        d->outputMixDryRight[core] &= ~bit;
        d->outputMixWetLeft[core] &= ~bit;
        d->outputMixWetRight[core] &= ~bit;
        d->voiceSwitchMask[core] &= ~bit;
    }
}

// Binds hardware voice n to the SPU2 voice: its register block, voice number (core in bit 0) and core mask bit.
static void func_002B2BA0_inl2(RwaSPU2Voice *v, RwUInt32 n)
{
    RwaSPU2VoiceState *st = &v->state;
    D_01E83480[n] = (unsigned int)v;
    st->StateDMA = &_rwaSPU2OutputState->DMAState.VoiceState[n];
    _rwaSPU2OutputState->DMAState.UsedVoices[n] = 1;
    if (n < 0x18) {
        st->voiceNum = (RwUInt8)(n << 1);
        st->voiceBit = 1u << n;
    } else {
        st->voiceNum = (RwUInt8)(((n - 0x18) << 1) | 1);
        st->voiceBit = 1u << (n - 0x18);
    }
}

// The core bit of hardware voice n as retail computes it for a Dolby partner: 1 << (n - 24), shift taken mod 32, so
// a core-0 partner n gets bit n + 8 [C 0x2B277C, 0x2B2DDC].
static RwUInt32 func_002B26F0_inl1(RwUInt32 n)
{
    return 1u << ((n - 0x18) & 0x1F);
}

// Releases the voice's hardware voice (and its Dolby partner's). A voice attached to a fixed hardware voice (mode
// 0x40) leaves it reserved. Returns the voice, or null when it had none.
// 0x002B26F0
extern "C" RwaSPU2Voice *_RwaSPU2OutputFreeHWVoice(RwaSPU2Voice *v)
{
    RwaSPU2Voice *r = 0;
    func_00114E60();
    RwInt32 i;
    for (i = 0; i < 0x30; i++)
        if (D_01E83480[i] == (unsigned int)v)
            break;
    if (i < 0x30) {
        if (v->state.DolbyPartner) {
            RwUInt32 n = (RwUInt32)((char *)v->state.DolbyPartner - (char *)_rwaSPU2OutputState) >> 5;
            _rwaSPU2OutputState->DMAState.UsedVoices[n] = 0;
            v->state.DolbyPartner = 0;
            func_002B2BA0_inl1(n >= 0x18, func_002B26F0_inl1(n), 0);
        }
        if (v->state.mode & 0x40) {
            D_01E83480[i] = 1;
        } else {
            _rwaSPU2OutputState->DMAState.UsedVoices[i] = 0;
            D_01E83480[i] = 0;
        }
        r = v;
        func_002B2BA0_inl1(v->state.voiceNum & 1, v->state.voiceBit, 0);
    }
    EIntr();
    return r;
}

// The voice range creation flags allow: 1 alone = core 0 (0-23), 2 alone = core 1 (24-47), else all.
static void func_002B2BA0_inl3(RwUInt32 flags, RwInt32 *lo, RwInt32 *hi)
{
    if ((flags & 1) && !(flags & 2))
        *hi = 0x18;
    if ((flags & 2) && !(flags & 1))
        *lo = 0x18;
}

// Reserves `count` free hardware voices (first free in the range) for the IOP: masks[] get their bits per core; all
// or nothing (1 / 0).
// 0x002B2900
extern "C" RwBool _RwaSPU2OutputAllocateHWVoiceNumber(RwUInt32 flags, RwUInt32 count, RwUInt32 *masks)
{
    RwBool r = 1;
    RwInt32 lo = 0, hi = 0x30;
    func_00114E60();
    masks[1] = 0;
    masks[0] = 0;
    func_002B2BA0_inl3(flags, &lo, &hi);
    for (RwUInt32 i = lo; count && i < (RwUInt32)hi; i++) {
        if (D_01E83480[i])
            continue;
        D_01E83480[i] = 1;
        _rwaSPU2OutputState->DMAState.UsedVoices[i] = 1;
        if (i < 0x18)
            masks[0] |= 1u << i;
        else
            masks[1] |= 1u << (i - 0x18);
        count--;
    }
    if (count) {
        for (RwUInt8 k = 0; k < 0x18; k++) {
            RwUInt32 bit = 1u << k;
            if (masks[0] & bit) {
                D_01E83480[k] = 0;
                _rwaSPU2OutputState->DMAState.UsedVoices[k] = 0;
            }
            if (masks[1] & bit) {
                D_01E83480[k + 0x18] = 0;
                _rwaSPU2OutputState->DMAState.UsedVoices[k + 0x18] = 0;
            }
        }
        masks[1] = 0;
        r = 0;
        masks[0] = 0;
    }
    EIntr();
    return r;
}

// Takes reserved hardware voice n (D_01E83480[n] == 1) for the voice.
// 0x002B2AC0
extern "C" RwaSPU2Voice *_RwaSPU2OutputAttachHWVoice(RwaSPU2Voice *v, RwUInt32 n)
{
    RwaSPU2Voice *r = 0;
    func_00114E60();
    if (D_01E83480[n] == 1) {
        func_002B2BA0_inl2(v, n);
        r = v;
    }
    EIntr();
    return r;
}

// The first free hardware voice in the creation range; its five mask bits set. A Dolby pair (mode 0x20) also takes
// a partner (the next free one) or falls back to single-voice Dolby (0x20 -> 0x10).
// 0x002B2BA0
extern "C" RwaSPU2Voice *_RwaSPU2OutputAllocateHWVoice(RwaSPU2Voice *v, RwaObj *output, void *params)
{
    RwaSPU2VoiceState *st = &v->state;
    RwInt32 lo = 0, hi = 0x30;
    RwaSPU2Voice *r = 0;
    func_00114E60();
    func_002B2BA0_inl3(st->mode, &lo, &hi);
    for (RwInt32 i = lo; i < hi; i++) {
        if (D_01E83480[i] == 0) {
            func_002B2BA0_inl2(v, i);
            r = v;
            func_002B2BA0_inl1(st->voiceNum & 1, st->voiceBit, 1);
            break;
        }
    }
    if (r && (st->mode & 0x20)) {
        RwBool found = 0;
        for (RwInt32 n = lo; n < hi; n++) {
            if (D_01E83480[n] == 0) {
                D_01E83480[n] = 1;
                st->DolbyPartner = &_rwaSPU2OutputState->DMAState.VoiceState[n];
                _rwaSPU2OutputState->DMAState.UsedVoices[n] = 1;
                func_002B2BA0_inl1(n >= 0x18, func_002B26F0_inl1(n), 1);
                found = 1;
                break;
            }
        }
        if (!found) {
            st->mode &= ~0x20u;
            st->mode |= 0x10;
        }
    }
    EIntr();
    return r;
}

// The environment to the hardware reverb: ReverbType 1 off (room -10000); 9 / 8 echo-like (diffusion < 100 and
// reflections < 0; Delay = 127 * reflectionsDelay / 0.3, 8 also Feedback = 127 * 10^(reflections / 1000)); else by
// decay time and HF ratio: <= 2 s: 3 / 2 (HF <= 0.5 / above), <= 3 s: 4 / 10 (reflections < 401 / not), <= 4 s: 5 / 6
// (HF <= 1), longer: 6 / 7 (HF <= 0.5). Gain = 0x7FFF * 10^(room / 1000), both sides.
// 0x002B2E80
extern "C" void _rwaSPU2OutputSetEnvironment(RwaSPU2Output *out, RwaEnvironment *env)
{
    RwaSPU2OutputState *st = &out->state;
    memcpy(&st->Environment, env, 0x30);
    st->DMAState.Feedback = 0;
    st->DMAState.Delay = 0;
    if (env->room == -10000) {
        st->DMAState.ReverbType = 1;
    } else if (env->diffusion < 100.0f && env->reflections < 0) {
        st->DMAState.Delay = (RwInt32)(127.0f * (env->reflectionsDelay / D_004E06B8));
        if (env->reverbDelay < D_004E0688) {
            st->DMAState.ReverbType = 9;
        } else {
            st->DMAState.ReverbType = 8;
            RwReal p = func_0011E388(10.0f, (RwReal)env->reflections / 1000.0f);
            st->DMAState.Feedback = (RwInt32)(127.0f * p);
        }
    } else {
        RwReal decay = env->decayTime;
        if (!(decay <= 4.0f))
            st->DMAState.ReverbType = env->decayHFRatio <= 0.5f ? 6 : 7;
        else if (!(decay <= 3.0f))
            st->DMAState.ReverbType = env->decayHFRatio <= 1.0f ? 5 : 6;
        else if (!(decay <= 2.0f))
            st->DMAState.ReverbType = env->reflections < 0x191 ? 4 : 10;
        else
            st->DMAState.ReverbType = env->decayHFRatio <= 0.5f ? 3 : 2;
    }
    RwReal g = func_0011E388(10.0f, (RwReal)env->room / 1000.0f);
    st->DMAState.ReverbGainLeft = (RwInt16)(RwInt32)((RwReal)0x7FFF * g);
    st->DMAState.ReverbGainRight = st->DMAState.ReverbGainLeft;
}

// Creation-parameter info: {0x30, 1, 1} [G meaning].
// 0x002B30F0
extern "C" RwaObj *func_002B30F0(RwaObj *obj, RwUInt32 *out)
{
    out[0] = 0x30;
    out[1] = 1;
    out[2] = 1;
    return obj;
}

// The IOP's reply word at +off of DMAStateReply, read uncached (kseg1 alias | 0x20000000) [C 0x2B3134, 0x2B31C4].
static RwInt32 func_002B3110_inl1(RwaSPU2OutputState *st, RwUInt32 off)
{
    return *(volatile RwInt32 *)(((unsigned int)&st->DMAStateReply + off) | 0x20000000);
}

// Per update, once the IOP has acknowledged the previous snapshot (its reply PacketID equals ours): the working
// state is copied to DMAStateToIOP with the next PacketID and sent (opcode 0x14). Then, per hardware voice, the
// play state machine: a voice the IOP reports stopped (reply PlayState 1) becomes 3 unless a key-on (2) is pending;
// 2 (key-on sent) becomes 4 (playing) with CurrentPosition = TriggerPosition and the trigger cleared; 4 takes the
// IOP's CurrentPosition; any other state copies TriggerPosition into CurrentPosition.
// 0x002B3110
extern "C" RwaSPU2Output *_RwaSPU2OutputFinishExecute(RwaSPU2Output *out)
{
    RwaSPU2OutputState *st = &out->state;
    _rwaObjFinishExecute(&out->obj, 1);
    if (func_002B3110_inl1(st, 0x670) != (RwInt32)_rwaSPU2UpdateCount)
        return out;
    memcpy(&st->DMAStateToIOP, &st->DMAState, 0x680);
    _rwaSPU2UpdateCount++;
    st->DMAStateToIOP.PacketID = _rwaSPU2UpdateCount;
    SyncDCache(&st->DMAStateToIOP, (char *)st + 0xCFF);
    RwaRPCTransfer(st->DMAStateIOPAddr, &st->DMAStateToIOP, 0x680, 0x14, 0, 0, _rwaSPU2UpdateCount, 0);
    for (RwInt32 i = 0; i < (RwInt32)st->NumVoices; i++) {
        RwaSPU2VoiceStateDMA *v = &st->DMAState.VoiceState[i];
        RwUInt32 off = i * 0x20;
        if (func_002B3110_inl1(st, off) == 1 && v->PlayState != 2)
            v->PlayState = 3;
        if (v->PlayState == 4) {
            v->CurrentPosition = (RwUInt32)func_002B3110_inl1(st, off + 0x14);
        } else if (v->PlayState == 2) {
            v->PlayState = 4;
            v->CurrentPosition = v->TriggerPosition;
            v->TriggerPosition = 0;
        } else {
            v->CurrentPosition = v->TriggerPosition;
        }
    }
    return out;
}

// Mixer callback {callback, buffer length (samples), data}: installs it (buffer of length * 4 + 64 bytes, 64-aligned;
// opcode 0x16 with length * 8) or, with a null callback, removes it (opcode 0x17, waits until the IOP clears
// D_004E2C80). Nothing happens without a mixer buffer [C 0x2B25B8: tests MixerBufferMem first].
struct UnkStruct_002B25A0 {
    RwaOutputCallback field_0;   // 0x0
    RwUInt32 field_4;            // 0x4 buffer length
    RwUInt32 field_8;            // 0x8 callback data
};
// 0x002B25A0
extern "C" void _rwaSPU2OutputSetMixerCallback(RwaSPU2Output *out, UnkStruct_002B25A0 *p)
{
    RwaSPU2OutputState *st = &out->state;
    if (!st->MixerBufferMem)
        return;
    if (!p->field_0) {
        RwaRPCTransfer(0, 0, 0, 0x17, 0, 0, 0, 0);
        while (D_004E2C80)
            FlushCache(0);
        st->MixerCallbackData = 0;
        st->MixerBufferLength = 0;
        if (st->MixerBufferMem)
            func_00293440(st->MixerBufferMem);
        st->MixerBufferMem = 0;
        st->MixerBuffer = 0;
        st->MixerCallback = 0;
        return;
    }
    st->MixerCallbackData = p->field_8;
    st->MixerBufferLength = p->field_4;
    st->MixerBufferMem = (RwInt16 *)func_00293430((st->MixerBufferLength << 2) + 0x40, 1, 0x3080D);
    st->MixerBuffer = (RwInt16 *)(((unsigned int)st->MixerBufferMem + 0x3F) & ~0x3Fu);
    st->MixerCallback = p->field_0;
    if (st->MixerBufferMem) {
        RwaRPCTransfer(0, 0, 0, 0x16, 0, 0, st->MixerBufferLength << 3, 0);
        D_004E2C80 = 1;
    }
}

// 0x002B3250
extern "C" void _RwaSPU2OutputDestroy(RwaSPU2Output *out)
{
    UnkStruct_002B25A0 none;
    _spu2streamSyncTransactionIOPEEAddr = 0;
    _spu2streamSyncTransactionEEIOPAddr = 0;
    none.field_0 = 0;
    none.field_4 = 0;
    none.field_8 = 0;
    _rwaSPU2OutputSetMixerCallback(out, &none);
    out->state.Listener = 0;
    RwaRPCTransfer(0, 0, 0, 0x12, 0, 0, 0, 0);
    while (D_004E2C7C)
        FlushCache(0);
    if (_rwaSPU2OutputReverbCores & 1)
        RwaSPU2HWResourceRAMUnlock(0x1C0000, 0x20000);
    if (_rwaSPU2OutputReverbCores & 2)
        RwaSPU2HWResourceRAMUnlock(0x1E0000, 0x20000);
    _rwaSPU2WaveRAMClose();
    if (D_004E2C8C) {
        RwInt32 tid = GetThreadId();
        RwaRPCTransfer(0, 0, 0, 0x2C, 0, tid, (unsigned int)&D_004E2C8C, 0);
        SleepThread();
    }
    RwaRPCClose();
    _rwaSPU2OutputState = 0;
}

// Creation: the state, the reverb (cores from the parameters; the default environment), the RPC channel with
// RwaSPU2OutputCmdHandler, then three handshakes with the IOP, each waited for by polling: 0x11 (create; the IOP
// answers 0x1A), 0x13 (IOP memory; answers 0x1B with free / used), 0x15 (the reply area's EE address; answers 0x1C
// with its snapshot address). Sound RAM 0x1C0000 / 0x1E0000 (0x20000 bytes each) is locked for the reverb work areas.
// 0x002B3370
extern "C" RwaSPU2Output *_RwaSPU2OutputCreate(RwaSPU2Output *out, RwaObj *p1, RwaSPU2OutputCreationParams *params)
{
    RwaSPU2OutputState *st = &out->state;
    RwaEnvironment env;
    _rwaSPU2ResourceBitfield[0] = 0;
    _rwaSPU2OutputState = st;
    _rwaSPU2ResourceBitfield[1] = 0;
    _rwaSPU2UpdateCount = 0;
    env = D_0049FE30;
    st->Initialized = 0;
    st->IRQCallback = 0;
    st->MixerCallback = 0;
    st->MixerBuffer = 0;
    st->MixerBufferMem = 0;
    st->NumVoices = params->maxVoices;
    st->DMAState.SpeakerConfig = 1;
    st->DMAState.Gain = 0xFFFF;
    st->Listener = 0;
    st->DMAStateReply.PacketID = 0;
    st->DMAStateToIOP.PacketID = 0;
    st->DMAState.PacketID = 0;
    for (RwInt32 c = 0; c < 2; c++) {
        st->DMAState.outputMixDryLeft[c] = 0;
        st->DMAState.outputMixDryRight[c] = 0;
        st->DMAState.outputMixWetLeft[c] = 0;
        st->DMAState.outputMixWetRight[c] = 0;
        st->DMAState.voiceSwitchMask[c] = 0;
    }
    _rwaSPU2OutputReverbCores = 0;
    if (params->reverbEnableCore0)
        _rwaSPU2OutputReverbCores |= 1;
    if (params->reverbEnableCore1)
        _rwaSPU2OutputReverbCores |= 2;
    for (RwInt32 i = 0; i < 0x30; i++) {
        D_01E83480[i] = 0;
        st->DMAState.UsedVoices[i] = 0;
    }
    if (_rwaSPU2OutputReverbCores)
        _rwaSPU2OutputSetEnvironment(out, &env);
    RwaRPCOpen(RwaSPU2OutputCmdHandler, st, 0x16);
    D_004E2C7C = 0;
    RwaRPCTransfer(0, 0, 0, 0x11, 0, 0, st->NumVoices, _rwaSPU2OutputReverbCores);
    while (!D_004E2C7C)
        FlushCache(0);
    _rwaSPU2_IOPMemFree = -1;
    _rwaSPU2_IOPMemUsed = -1;
    RwaRPCTransfer(0, 0, 0, 0x13, 0, 0, 0, 0);
    while (_rwaSPU2_IOPMemFree == -1 && _rwaSPU2_IOPMemUsed == -1)
        FlushCache(0);
    st->DMAStateIOPAddr = 0;
    SyncDCache(&st->DMAStateReply, (char *)st + 0x137F);
    RwaRPCTransfer(0, 0, 0, 0x15, 0, 0, (unsigned int)&st->DMAStateReply, 0);
    while (!st->DMAStateIOPAddr)
        FlushCache(0);
    _rwaSPU2WaveRAMOpen();
    if (_rwaSPU2OutputReverbCores & 1)
        RwaSPU2HWResourceRAMLock(0x1C0000, 0x20000);
    if (_rwaSPU2OutputReverbCores & 2)
        RwaSPU2HWResourceRAMLock(0x1E0000, 0x20000);
    _rwaG_rwaSPU2InputAreaMemoryBitmap = 0;
    st->Initialized = 1;
    return out;
}

// 0x002B3630
extern "C" RwaObjDef *RwaSPU2OutputUnregister(void)
{
    if (D_004E2C78) {
        if (!RwaSPU2OutputInterfaceUnregister())
            return 0;
        D_004E2C78 = 0;
    }
    return RwaObjDefDestroy(&D_01E83390);
}

// 0x002B3680
extern "C" RwInt32 _RwaSPU2OutputRegisterCommon(RwaObjDef *def)
{
    RwaObjDefAssignID(def, &rwaSPU2OUTPUTID, 0);
    def->noFreeListEntries = 1;
    func_00297C30_inl3(def, 0x1400, 0x40);
    def->createFunc = _RwaSPU2OutputCreate;
    def->destroyFunc = _RwaSPU2OutputDestroy;
    def->startExecuteFunc = 0;
    def->finishExecuteFunc = _RwaSPU2OutputFinishExecute;
    def->getParamsFunc = func_002B30F0;
    def->createParamSize = 0xC;
    RwaSifCmdInit();
    return 0;   // [G] retail returns the stale v0 left by RwaSifCmdInit (a void function); no caller reads it
}

// 0x002B3960
extern "C" RwaObjDef *RwaSPU2OutputRegisterUsingInterface(void)
{
    RwaObjDef *def = RwaObjDefCreate(_rwaGnullObjDef, 0xC, &D_01E83390, &D_01E83370, &_spu2outputAliasInfo, 0x3080C);
    if (!def)
        return 0;
    _RwaSPU2OutputRegisterCommon(def);
    if (!RwaObjDefSetupParams(def, _spu2outputInputParams, 7, _spu2outputOutputParams, 6)) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjInterfaceDef *intf = RwaSPU2OutputInterfaceRegister();
    if (!intf) {
        RwaObjDefDestroy(def);
        return 0;
    }
    RwaObjDefSetInterface(def, intf);
    D_004E2C78 = 1;
    return def;
}

// 0x002B3A30
extern "C" RwaObj *RwaSPU2OutputCreate(RwUInt32 p0, RwUInt32 p1, RwUInt32 p2, RwUInt32 p3)
{
    return RwaObjCreate(&D_01E83390, 0, p1, (void *)p0, p2, p3);
}

// The IOP's tick (opcode 0x19): the IRQ callback, else the mixer callback, fills a buffer that goes to the IOP at
// iopAddr (opcode 0x04, `size` bytes; the mixer callback is asked for size / 4 samples).
// 0x002B3A50
extern "C" void RwaSPU2OutputTickUpdate(RwaSPU2OutputState *st, RwUInt32 iopAddr, RwUInt32 size)
{
    if (!st)
        return;
    if (st->IRQCallback) {
        RwaOutputMixerData *m = st->IRQCallback(size, st->IRQCallbackData);
        void *buf = m->busses[0];
        if (buf)
            RwaRPCTransfer(iopAddr, buf, size, 4, 0, 0, 0, 0);
        return;
    }
    if (!st->MixerCallback || !st->MixerBuffer)
        return;
    RwaOutputMixerData *m = st->MixerCallback(size >> 2, st->MixerCallbackData);
    void *buf = m->busses[0];
    if (!buf || !iopAddr)
        return;
    FlushCache(0);
    RwaRPCTransfer(iopAddr, buf, size, 4, 0, 0, 0, 0);
}

// Releases the voices a request's masks name (the IOP's voices back to free).
static void func_002B3B50_inl1(UnkStruct_01E83440 *r)
{
    func_00114E60();
    for (RwUInt32 n = 0; n < 0x30; n++) {
        RwBool hit = n < 0x18 ? (r->field_18[0] & (1u << n)) != 0 : (r->field_18[1] & (1u << (n - 0x18))) != 0;
        if (hit) {
            D_01E83480[n] = 0;
            _rwaSPU2OutputState->DMAState.UsedVoices[n] = 0;
        }
    }
    EIntr();
}

// The IOP's requests (RPC user handler, called from _RwaRPCProcessSystemCommand with the packet's 0x24 / 0x28 words
// and its 0x14 word = the waiting EE thread or a callback):
//   0x14 / 0x39 nothing; replies 0x2B-0x41 (odd), 0x4B-0x51 (odd): *(u32 *)arg0 = arg1 if arg0, wake the thread;
//   0x33 / 0x47 call `thread` as a function (arg0, arg1); 0x42 stream sync; 0x1E / 0x1F / 0x22 voice + RAM request
//   (copy / grant / release); 0x24 / 0x25 / 0x28 input-area request (copy / reserve / free); 0x1D mixer off done;
//   0x19 tick; 0x1C the snapshot address; 0x1B IOP memory free / used; 0x12 / 0x1A destroy / create acknowledged.
// 0x002B3B50
extern "C" void RwaSPU2OutputCmdHandler(RwaSPU2OutputState *st, RwUInt32 op, RwUInt32 arg0, RwUInt32 arg1,
                                        RwInt32 thread)
{
    switch (op) {
    case 0x39:
    case 0x14:
        return;
    case 0x51: case 0x4F: case 0x4D: case 0x4B: case 0x37: case 0x41: case 0x3F: case 0x3D: case 0x3B: case 0x35:
    case 0x31: case 0x2F: case 0x2D: case 0x2B:
        if (arg0)
            *(RwUInt32 *)arg0 = arg1;
        if (thread <= 0)
            return;
        WakeupThread(thread);
        return;
    case 0x33:
    case 0x47:
        ((void (*)(RwUInt32, RwUInt32))thread)(arg0, arg1);
        return;
    case 0x42:
        if (D_01E83540.field_4 == 6)
            D_01E83540.field_18(D_01E83540.field_8, D_01E83540.field_1C, D_01E83540.field_20, 0);
        SyncDCache(&D_01E83540, (char *)&D_01E83540 + 0x7F);
        RwaRPCTransfer(_spu2streamSyncTransactionIOPEEAddr, &D_01E83540, 0x80, 0x43, 0, thread, 0, 0);
        return;
    case 0x28: {
        RwUInt32 iop = D_01E83400.field_4;
        D_01E83400.field_14 = _rwaSPU2InputAreaRAMFreeBlocks(D_01E83400.field_10);
        SyncDCache(&D_01E83400, (char *)&D_01E83400 + 0x3F);
        RwaRPCTransfer(0, 0, 0, 0x29, 0, 0, iop, (unsigned int)&D_01E83400);
        return;
    }
    case 0x25: {
        RwUInt32 iop = D_01E83400.field_4;
        D_01E83400.field_14 = _rwaSPU2InputAreaRAMReserveBlocks(D_01E83400.field_10);
        SyncDCache(&D_01E83400, (char *)&D_01E83400 + 0x3F);
        RwaRPCTransfer(iop, &D_01E83400, 0x40, 0x27, 0, 0, iop, (unsigned int)&D_01E83400);
        return;
    }
    case 0x24:
        D_01E83400.field_0 = &D_01E83400;
        D_01E83400.field_4 = arg0;
        SyncDCache(&D_01E83400, (char *)&D_01E83400 + 0x3F);
        RwaRPCTransfer(0, 0, 0, 0x26, 0, 0, arg0, (unsigned int)&D_01E83400);
        return;
    case 0x22: {
        RwUInt32 iop = D_01E83440.field_4;
        func_002B3B50_inl1(&D_01E83440);
        _rwaSPU2WaveRAMFree(D_01E83440.field_24);
        SyncDCache(&D_01E83440, (char *)&D_01E83440 + 0x3F);
        RwaRPCTransfer(0, 0, 0, 0x23, 0, 0, iop, (unsigned int)&D_01E83440);
        return;
    }
    case 0x1F: {
        RwUInt32 iop = D_01E83440.field_4;
        if (_RwaSPU2OutputAllocateHWVoiceNumber(D_01E83440.field_14, D_01E83440.field_10, D_01E83440.field_18)) {
            D_01E83440.field_24 = _rwaSPU2WaveRAMAlloc(D_01E83440.field_C);
            if (D_01E83440.field_24)
                D_01E83440.field_20 = _rwaSPU2WaveRAMGetAddress(D_01E83440.field_24);
            else
                func_002B3B50_inl1(&D_01E83440);
        }
        SyncDCache(&D_01E83440, (char *)&D_01E83440 + 0x3F);
        RwaRPCTransfer(iop, &D_01E83440, 0x40, 0x21, 0, 0, iop, (unsigned int)&D_01E83440);
        return;
    }
    case 0x1E:
        D_01E83440.field_0 = &D_01E83440;
        D_01E83440.field_4 = arg0;
        SyncDCache(&D_01E83440, (char *)&D_01E83440 + 0x3F);
        RwaRPCTransfer(0, 0, 0, 0x20, 0, 0, arg0, (unsigned int)&D_01E83440);
        return;
    case 0x1D:
        D_004E2C80 = 0;
        return;
    case 0x19:
        RwaSPU2OutputTickUpdate(_rwaSPU2OutputState, arg0, arg1);
        return;
    case 0x1C:
        st->DMAStateIOPAddr = arg0;
        return;
    case 0x1B:
        _rwaSPU2_IOPMemFree = arg0;
        _rwaSPU2_IOPMemUsed = arg1;
        return;
    case 0x12:
        D_004E2C7C = 0;
        return;
    case 0x1A:
        D_004E2C7C = 1;
        return;
    default:
        return;
    }
}

// Sets the IOP file interface (opcode 0x30) and sleeps until the IOP's reply wakes the thread.
// 0x002B4180
extern "C" RwInt32 RwaPS2SetIOPFileInterface(RwUInt32 iface)
{
    D_004E2C90 = iface;
    RwInt32 tid = GetThreadId();
    RwaRPCTransfer(0, 0, 0, 0x30, 0, tid, iface, 0);
    return SleepThread();
}
